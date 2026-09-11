#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
	Display *dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "xtest_grab: cannot open display\n");
		return 1;
	}

	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);
	Window win = XCreateSimpleWindow(dpy, root, 0, 0, 800, 600, 0, BlackPixel(dpy, screen), BlackPixel(dpy, screen));
	XStoreName(dpy, win, "xtest_grab");
	XSelectInput(dpy, win, ExposureMask | KeyPressMask | KeyReleaseMask | ButtonPressMask | ButtonReleaseMask | PointerMotionMask);
	XMapWindow(dpy, win);
	XFlush(dpy);

	int auto_grab = getenv("XTEST_GRAB_AUTO") != NULL;

	Pixmap empty = XCreatePixmap(dpy, win, 1, 1, 1);
	XColor color;
	color.pixel = 0;
	color.red = 0;
	color.green = 0;
	color.blue = 0;
	XColor exact;
	XAllocColor(dpy, DefaultColormap(dpy, screen), &exact);
	Cursor hidden_cursor = XCreatePixmapCursor(dpy, empty, empty, &exact, &exact, 0, 0);
	Cursor visible = XCreateFontCursor(dpy, XC_crosshair);

	int grabbed = 0;
	int mx = 400;
	int my = 300;

	if (auto_grab) {
		for (int i = 0; i < 200; i++) {
			XWindowAttributes attrs;
			XGetWindowAttributes(dpy, win, &attrs);
			if (attrs.map_state == IsViewable)
				break;
			usleep(10000);
		}
		int status = XGrabPointer(dpy, win, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, win, hidden_cursor, CurrentTime);
		grabbed = (status == GrabSuccess);
		if (grabbed)
			XDefineCursor(dpy, win, hidden_cursor);
		fprintf(stderr, "xtest_grab: auto-grab status=%d active=%d\n", status, grabbed);
	}

	GC gc = XCreateGC(dpy, win, 0, NULL);
	XSetForeground(dpy, gc, WhitePixel(dpy, screen));

	int running = 1;
	while (running) {
		XEvent ev;
		XNextEvent(dpy, &ev);
		switch (ev.type) {
		case Expose:
			XSetForeground(dpy, gc, BlackPixel(dpy, screen));
			XFillRectangle(dpy, win, gc, 0, 0, 800, 600);
			XSetForeground(dpy, gc, WhitePixel(dpy, screen));
			XDrawString(dpy, win, gc, 20, 40, "g: grab+confine+hide  u: ungrab  p: warp center  q: quit", 62);
			XFlush(dpy);
			break;
		case KeyPress: {
			KeySym sym = XLookupKeysym(&ev.xkey, 0);
			if (sym == XK_g) {
				int status = XGrabPointer(dpy, win, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync, win, hidden_cursor, CurrentTime);
				grabbed = (status == GrabSuccess);
				if (grabbed)
					XDefineCursor(dpy, win, hidden_cursor);
				fprintf(stderr, "xtest_grab: grab status=%d active=%d\n", status, grabbed);
			} else if (sym == XK_u) {
				XUngrabPointer(dpy, CurrentTime);
				grabbed = 0;
				XDefineCursor(dpy, win, visible);
				fprintf(stderr, "xtest_grab: ungrabbed\n");
			} else if (sym == XK_p) {
				XWarpPointer(dpy, None, win, 0, 0, 0, 0, 400, 300);
				XFlush(dpy);
			} else if (sym == XK_q) {
				running = 0;
			}
			break;
		}
		case MotionNotify: {
			int x = ev.xmotion.x;
			int y = ev.xmotion.y;
			int dx = x - mx;
			int dy = y - my;
			mx = x;
			my = y;
			if (grabbed) {
				mx = 400;
				my = 300;
				XWarpPointer(dpy, None, win, 0, 0, 0, 0, 400, 300);
				XFlush(dpy);
			}
			fprintf(stderr, "xtest_grab: motion dx=%d dy=%d pos=(%d,%d) grabbed=%d\n", dx, dy, x, y, grabbed);
			break;
		}
		case ButtonPress:
			fprintf(stderr, "xtest_grab: button %d press\n", ev.xbutton.button);
			break;
		case ButtonRelease:
			fprintf(stderr, "xtest_grab: button %d release\n", ev.xbutton.button);
			break;
		}
	}

	XFreeCursor(dpy, hidden_cursor);
	XFreeCursor(dpy, visible);
	XFreePixmap(dpy, empty);
	XFreeGC(dpy, gc);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
