#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "child.h"
#include "dynscope.h"

#define KILL_GRACE_MS 2000

static int g_forward_signal = 0;

static void supervisor_handle_signal(int signal_number) {
	(void)signal_number;
	g_forward_signal = 1;
}

static void supervisor_reset_signals(void) {
	sigset_t empty;
	sigemptyset(&empty);
	sigprocmask(SIG_SETMASK, &empty, NULL);
	signal(SIGPIPE, SIG_DFL);
}

static void supervisor_forward_signal(pid_t game) {
	kill(-game, SIGTERM);
	kill(game, SIGTERM);
}

static int supervisor_wait_for_game(pid_t game) {
	int status = 0;
	while (waitpid(game, &status, 0) < 0) {
		if (errno != EINTR)
			return -1;
		if (g_forward_signal) {
			supervisor_forward_signal(game);
			g_forward_signal = 0;
		}
	}
	return status;
}

static void supervisor_run(int pid_fd, int status_fd, char **argv) {
	struct sigaction sa = {0};
	sa.sa_handler = supervisor_handle_signal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);

	sigset_t empty;
	sigemptyset(&empty);
	sigprocmask(SIG_SETMASK, &empty, NULL);

	pid_t supervisor_pid = getpid();
	prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);

	pid_t game = fork();
	if (game < 0)
		_exit(1);
	if (game == 0) {
		prctl(PR_SET_PDEATHSIG, SIGKILL, 0, 0, 0);
		if (getppid() != supervisor_pid)
			_exit(127);
		setpgid(0, 0);
		supervisor_reset_signals();
		execvp(argv[0], argv);
		_exit(127);
	}
	setpgid(game, game);

	uint32_t message = (uint32_t)game;
	if (write(pid_fd, &message, sizeof(message)) != sizeof(message)) {
		kill(game, SIGKILL);
		while (waitpid(game, NULL, 0) < 0 && errno == EINTR) {
		}
		_exit(1);
	}

	int status = supervisor_wait_for_game(game);
	if (status < 0)
		_exit(1);

	message = (uint32_t)status;
	if (write(status_fd, &message, sizeof(message)) != sizeof(message))
		_exit(1);
	_exit(0);
}

static void child_finish(struct dynscope *ds, int wait_status) {
	struct child *child = &ds->child;
	if (child->finished)
		return;
	child->finished = true;

	if (child->kill_timer != NULL) {
		wl_event_source_remove(child->kill_timer);
		child->kill_timer = NULL;
	}

	int code = 1;
	if (wait_status >= 0 && WIFEXITED(wait_status)) {
		code = WEXITSTATUS(wait_status);
	} else if (wait_status >= 0 && WIFSIGNALED(wait_status)) {
		int signal_number = WTERMSIG(wait_status);
		if (child->terminated_by_us && (signal_number == SIGTERM || signal_number == SIGKILL))
			code = 0;
		else
			code = 128 + signal_number;
	}

	ds->exit_code = code;
	ds->running = false;
}

static int child_status_event(int fd, uint32_t mask, void *data) {
	(void)mask;
	struct dynscope *ds = data;

	uint32_t message = 0;
	ssize_t n = read(fd, &message, sizeof(message));
	if (n == (ssize_t)sizeof(message))
		child_finish(ds, (int)message);
	else if (n == 0)
		child_finish(ds, -1);
	return 1;
}

static int child_pid_event(int fd, uint32_t mask, void *data) {
	(void)mask;
	struct dynscope *ds = data;

	uint32_t message = 0;
	ssize_t n = read(fd, &message, sizeof(message));
	if (n == (ssize_t)sizeof(message))
		ds->child.game = (pid_t)message;
	return 1;
}

static int child_kill_timer(void *data) {
	struct dynscope *ds = data;
	struct child *child = &ds->child;

	if (child->game > 0) {
		kill(-child->game, SIGKILL);
		kill(child->game, SIGKILL);
	}
	if (child->supervisor > 0)
		kill(child->supervisor, SIGKILL);
	return 0;
}

void child_terminate(struct dynscope *ds) {
	struct child *child = &ds->child;
	if (!child->spawned || child->finished)
		return;
	child->terminated_by_us = true;

	if (child->game > 0) {
		kill(-child->game, SIGTERM);
		kill(child->game, SIGTERM);
	} else if (child->supervisor > 0) {
		kill(child->supervisor, SIGTERM);
	}

	child->kill_timer = wl_event_loop_add_timer(child->loop, child_kill_timer, ds);
	if (child->kill_timer != NULL)
		wl_event_source_timer_update(child->kill_timer, KILL_GRACE_MS);
}

int child_spawn(struct dynscope *ds, char **argv) {
	struct child *child = &ds->child;
	child->game = -1;
	child->pid_fd = -1;
	child->status_fd = -1;

	int pid_fds[2] = {-1, -1};
	int status_fds[2] = {-1, -1};

	if (pipe2(pid_fds, O_CLOEXEC) < 0)
		return -1;
	if (pipe2(status_fds, O_CLOEXEC) < 0) {
		close(pid_fds[0]);
		close(pid_fds[1]);
		return -1;
	}

	pid_t supervisor = fork();
	if (supervisor < 0) {
		close(pid_fds[0]);
		close(pid_fds[1]);
		close(status_fds[0]);
		close(status_fds[1]);
		return -1;
	}
	if (supervisor == 0) {
		close(pid_fds[0]);
		close(status_fds[0]);
		supervisor_run(pid_fds[1], status_fds[1], argv);
		_exit(1);
	}

	close(pid_fds[1]);
	close(status_fds[1]);
	child->supervisor = supervisor;
	child->pid_fd = pid_fds[0];
	child->status_fd = status_fds[0];
	child->pid_src = wl_event_loop_add_fd(child->loop, child->pid_fd, WL_EVENT_READABLE, child_pid_event, ds);
	child->status_src = wl_event_loop_add_fd(child->loop, child->status_fd, WL_EVENT_READABLE, child_status_event, ds);
	child->spawned = true;
	return 0;
}
