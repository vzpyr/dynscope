#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <X11/extensions/XInput2.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(void) {
	Display *dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "xtest_rel: cannot open display\n");
		return 1;
	}

	int xi_opcode, xi_event, xi_error;
	if (!XQueryExtension(dpy, "XInputExtension", &xi_opcode, &xi_event, &xi_error)) {
		fprintf(stderr, "xtest_rel: XI2 not available\n");
		return 1;
	}
	int major = 2, minor = 2;
	if (XIQueryVersion(dpy, &major, &minor) != Success) {
		fprintf(stderr, "xtest_rel: XI2 version unavailable\n");
		return 1;
	}

	int screen = DefaultScreen(dpy);
	Window root = RootWindow(dpy, screen);
	Window win = XCreateSimpleWindow(dpy, root, 0, 0, 800, 600, 0, BlackPixel(dpy, screen), BlackPixel(dpy, screen));
	XStoreName(dpy, win, "xtest_rel");
	XSelectInput(dpy, win, ExposureMask | KeyPressMask);
	XMapWindow(dpy, win);
	XFlush(dpy);

	XIEventMask raw_mask;
	unsigned char mask_data[XIMaskLen(XI_LASTEVENT)] = {0};
	raw_mask.deviceid = XIAllMasterDevices;
	raw_mask.mask_len = sizeof(mask_data);
	raw_mask.mask = mask_data;
	XISetMask(mask_data, XI_RawMotion);
	XISetMask(mask_data, XI_RawButtonPress);
	XISetMask(mask_data, XI_RawButtonRelease);
	if (XISelectEvents(dpy, root, &raw_mask, 1) != Success) {
		fprintf(stderr, "xtest_rel: XISelectEvents failed\n");
		return 1;
	}

	Pixmap empty = XCreatePixmap(dpy, win, 1, 1, 1);
	XColor color = {0, 0, 0, 0, 0, 0};
	Cursor hidden_cursor = XCreatePixmapCursor(dpy, empty, empty, &color, &color, 0, 0);

	int grabbed = 0;
	double sum_dx = 0;
	double sum_dy = 0;
	long events = 0;

	GC gc = XCreateGC(dpy, win, 0, NULL);
	XSetForeground(dpy, gc, WhitePixel(dpy, screen));

	for (int i = 0; i < 200; i++) {
		XWindowAttributes attrs;
		XGetWindowAttributes(dpy, win, &attrs);
		if (attrs.map_state == IsViewable)
			break;
		usleep(10000);
	}

	int status = XGrabPointer(dpy, win, True, 0, GrabModeAsync, GrabModeAsync, win, hidden_cursor, CurrentTime);
	fprintf(stderr, "xtest_rel: grab status=%d\n", status);
	XFlush(dpy);

	int running = 1;
	while (running) {
		XEvent ev;
		XNextEvent(dpy, &ev);
		if (ev.type == Expose) {
			XDrawString(dpy, win, gc, 20, 40, "raw XI2 relative motion probe - q: quit+release", 51);
			XFlush(dpy);
			continue;
		}
		if (ev.type == KeyPress) {
			KeySym sym = XLookupKeysym(&ev.xkey, 0);
			if (sym == XK_q) {
				running = 0;
				continue;
			}
		}
		if (ev.xcookie.type == GenericEvent && ev.xcookie.extension == xi_opcode) {
			if (!XGetEventData(dpy, &ev.xcookie))
				continue;
			XIRawEvent *raw = ev.xcookie.data;
			if (raw->evtype == XI_RawMotion && raw->valuators.values != NULL) {
				double *vals = raw->valuators.values;
				int nval = 0;
				for (int i = 0; i < raw->valuators.mask_len * 8; i++) {
					if (XIMaskIsSet(raw->valuators.mask, i))
						nval = i + 1;
				}
				double dx = nval > 0 ? vals[0] : 0;
				double dy = nval > 1 ? vals[1] : 0;
				double ux = nval > 2 ? vals[2] : 0;
				double uy = nval > 3 ? vals[3] : 0;
				sum_dx += ux;
				sum_dy += uy;
				events++;
				if (events % 20 == 0)
					fprintf(stderr, "xtest_rel: n=%ld dx=%.2f dy=%.2f unaccel=(%.2f,%.2f) sum=(%.1f,%.1f)\n", events, dx, dy, ux, uy, sum_dx, sum_dy);
			} else if (raw->evtype == XI_RawButtonPress) {
				fprintf(stderr, "xtest_rel: raw button %ld press\n", raw->detail);
			}
			XFreeEventData(dpy, &ev.xcookie);
		}
	}

	fprintf(stderr, "xtest_rel: done events=%ld sum=(%.1f,%.1f)\n", events, sum_dx, sum_dy);
	XUngrabPointer(dpy, CurrentTime);
	XFreeCursor(dpy, hidden_cursor);
	XFreePixmap(dpy, empty);
	XFreeGC(dpy, gc);
	XDestroyWindow(dpy, win);
	XCloseDisplay(dpy);
	return 0;
}
