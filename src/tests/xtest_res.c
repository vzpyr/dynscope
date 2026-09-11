#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

static Display *dpy;
static Window win;
static int square = 1;

static void do_switch(void) {
	if (square) {
		XResizeWindow(dpy, win, 1280, 720);
		printf("SWITCH -> 1280x720 (16:9)\n");
	} else {
		XResizeWindow(dpy, win, 300, 300);
		printf("SWITCH -> 300x300 (1:1)\n");
	}
	square = !square;
	XFlush(dpy);
}

int main(void) {
	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) { fprintf(stderr, "no display\n"); return 1; }
	int screen = DefaultScreen(dpy);
	win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 100, 100, 300, 300, 1,
		BlackPixel(dpy, screen), WhitePixel(dpy, screen));
	XStoreName(dpy, win, "xtest-res");
	XSelectInput(dpy, win, ExposureMask | StructureNotifyMask | PointerMotionMask);
	static char cross_bits[] = {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81};
	Pixmap cross = XCreateBitmapFromData(dpy, win, cross_bits, 8, 8);
	Pixmap mask = XCreateBitmapFromData(dpy, win, cross_bits, 8, 8);
	XColor fg = {0, 0, 0xFFFF, 0};
	XColor bg = {0, 0, 0, 0};
	XDefineCursor(dpy, win, XCreatePixmapCursor(dpy, cross, mask, &fg, &bg, 4, 4));
	XMapWindow(dpy, win);
	XFlush(dpy);
	setvbuf(stdout, NULL, _IONBF, 0);

	time_t last = time(NULL);
	while (1) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(ConnectionNumber(dpy), &fds);
		struct timeval tv = {0, 100000};
		select(ConnectionNumber(dpy) + 1, &fds, NULL, NULL, &tv);

		while (XPending(dpy)) {
			XEvent ev;
			XNextEvent(dpy, &ev);
			if (ev.type == ConfigureNotify)
				printf("EVENT Configure %ux%u\n", ev.xconfigure.width, ev.xconfigure.height);
			fflush(stdout);
		}

		time_t now = time(NULL);
		if (now - last >= 5) {
			last = now;
			do_switch();
		}
	}
	return 0;
}
