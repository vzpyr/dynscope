#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>

int main(void) {
	Display *dpy = XOpenDisplay(NULL);
	if (dpy == NULL) { fprintf(stderr, "no display\n"); return 1; }
	int screen = DefaultScreen(dpy);
	Window win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 100, 100, 400, 300, 1,
		BlackPixel(dpy, screen), WhitePixel(dpy, screen));
	XStoreName(dpy, win, "xtest3");
	XSelectInput(dpy, win, ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
		PointerMotionMask | EnterWindowMask | LeaveWindowMask | FocusChangeMask);
	static char cross_bits[] = {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81};
	Pixmap cross = XCreateBitmapFromData(dpy, win, cross_bits, 8, 8);
	Pixmap mask = XCreateBitmapFromData(dpy, win, cross_bits, 8, 8);
	XColor fg = {0, 0, 0xFFFF, 0};
	XColor bg = {0, 0, 0, 0};
	XDefineCursor(dpy, win, XCreatePixmapCursor(dpy, cross, mask, &fg, &bg, 4, 4));
	XMapWindow(dpy, win);
	XFlush(dpy);
	setvbuf(stdout, NULL, _IONBF, 0);

	int last_x = -1, last_y = -1;
	Window last_child = None;
	time_t start = time(NULL);
	int warped = 0;

	while (1) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(ConnectionNumber(dpy), &fds);
		struct timeval tv = {0, 100000};
		select(ConnectionNumber(dpy) + 1, &fds, NULL, NULL, &tv);

		while (XPending(dpy)) {
			XEvent ev;
			XNextEvent(dpy, &ev);
			if (ev.type == KeyPress)      printf("EVENT KeyPress kc=%u\n", (unsigned)ev.xkey.keycode);
			if (ev.type == ButtonPress)   printf("EVENT ButtonPress btn=%u at %d,%d\n", (unsigned)ev.xbutton.button, ev.xbutton.x, ev.xbutton.y);
			if (ev.type == ButtonRelease) printf("EVENT ButtonRelease btn=%u\n", (unsigned)ev.xbutton.button);
			if (ev.type == MotionNotify)  printf("EVENT Motion at %d,%d\n", ev.xmotion.x, ev.xmotion.y);
			if (ev.type == EnterNotify)   printf("EVENT Enter at %d,%d\n", ev.xcrossing.x, ev.xcrossing.y);
			if (ev.type == LeaveNotify)   printf("EVENT Leave\n");
			if (ev.type == FocusIn)       printf("EVENT FocusIn\n");
			fflush(stdout);
		}

		Window root, child;
		int rx, ry, wx, wy;
		unsigned int m;
		if (XQueryPointer(dpy, RootWindow(dpy, screen), &root, &child, &rx, &ry, &wx, &wy, (unsigned int[]){0})) {
			if (rx != last_x || ry != last_y || child != last_child) {
				printf("QUERY root=(%d,%d) child=%lu\n", rx, ry, (unsigned long)child);
				last_x = rx; last_y = ry; last_child = child;
			}
		}

		if (!warped && time(NULL) - start >= 3) {
			warped = 1;
			printf("SELF-WARP to (200,150)\n");
			XWarpPointer(dpy, None, win, 0, 0, 0, 0, 200, 150);
			XFlush(dpy);
		}
	}
	return 0;
}
