#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/select.h>
#include <time.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xrandr.h>

static Display *dpy;
static Window win;
static int switched = 0;

static int ignore_x_error(Display *display, XErrorEvent *event) {
	(void)display;
	printf("RES: X error ignored opcode=%u minor=%u\n", (unsigned)event->request_code, (unsigned)event->minor_code);
	return 0;
}



static void do_mode_switch(void) {
	Window root = DefaultRootWindow(dpy);
	XRRScreenResources *res = XRRGetScreenResourcesCurrent(dpy, root);
	if (res == NULL) {
		printf("RES: no randr resources\n");
		return;
	}

	RROutput out = None;
	for (int i = 0; i < res->noutput; i++) {
		XRROutputInfo *info = XRRGetOutputInfo(dpy, res, res->outputs[i]);
		if (info != NULL && info->connection == RR_Connected && info->ncrtc > 0) {
			out = res->outputs[i];
			XRRFreeOutputInfo(info);
			break;
		}
		XRRFreeOutputInfo(info);
	}
	if (out == None) {
		printf("RES: no connected output\n");
		XRRFreeScreenResources(res);
		return;
	}

	XRROutputInfo *oinfo = XRRGetOutputInfo(dpy, res, out);
	RRCrtc crtc = oinfo->crtc;
	XRRFreeOutputInfo(oinfo);

	RRMode mode = None;
	for (int i = 0; i < res->nmode; i++) {
		if (res->modes[i].width == 800 && res->modes[i].height == 600) {
			mode = res->modes[i].id;
			break;
		}
	}
	if (mode == None) {
		printf("RES: mode 800x600 not listed\n");
		XRRFreeScreenResources(res);
		return;
	}

	XRRCrtcInfo *cinfo = XRRGetCrtcInfo(dpy, res, crtc);
	Status s = XRRSetCrtcConfig(dpy, res, crtc, cinfo->timestamp, 0, 0, mode, RR_Rotate_0, &out, 1);
	XRRFreeCrtcInfo(cinfo);
	printf("RES: RRSetCrtcConfig status=%d\n", (int)s);
	fflush(stdout);

	if (s == Success) {
		XRRSetScreenSize(dpy, root, 800, 600, 800 * 254 / 960, 600 * 254 / 960);
		printf("RES: switched to 800x600, resizing window\n");
		XResizeWindow(dpy, win, 800, 600);
	}
	XRRFreeScreenResources(res);
	XFlush(dpy);
}

int main(void) {
	dpy = XOpenDisplay(NULL);
	if (dpy == NULL) { fprintf(stderr, "no display\n"); return 1; }
	int screen = DefaultScreen(dpy);
	int event_base = 0, error_base = 0;
	XSetErrorHandler(ignore_x_error);
	if (!XRRQueryExtension(dpy, &event_base, &error_base))
		printf("RES: no randr extension\n");

	win = XCreateSimpleWindow(dpy, RootWindow(dpy, screen), 100, 100, 400, 300, 1,
		BlackPixel(dpy, screen), WhitePixel(dpy, screen));
	XStoreName(dpy, win, "xtest-res");
	XSelectInput(dpy, win, ExposureMask | KeyPressMask | StructureNotifyMask | PointerMotionMask);
	static char cross_bits[] = {0x81, 0x42, 0x24, 0x18, 0x18, 0x24, 0x42, 0x81};
	Pixmap cross = XCreateBitmapFromData(dpy, win, cross_bits, 8, 8);
	Pixmap mask = XCreateBitmapFromData(dpy, win, cross_bits, 8, 8);
	XColor fg = {0, 0, 0xFFFF, 0};
	XColor bg = {0, 0, 0, 0};
	XDefineCursor(dpy, win, XCreatePixmapCursor(dpy, cross, mask, &fg, &bg, 4, 4));
	XMapWindow(dpy, win);
	XFlush(dpy);
	setvbuf(stdout, NULL, _IONBF, 0);

	time_t start = time(NULL);
	int last_x = -1, last_y = -1;

	while (1) {
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(ConnectionNumber(dpy), &fds);
		struct timeval tv = {0, 100000};
		select(ConnectionNumber(dpy) + 1, &fds, NULL, NULL, &tv);

		while (XPending(dpy)) {
			XEvent ev;
			XNextEvent(dpy, &ev);
			if (ev.type == ConfigureNotify) {
				printf("EVENT Configure %ux%u\n", ev.xconfigure.width, ev.xconfigure.height);
				if (ev.xconfigure.width != 800 && !switched) {
					do_mode_switch();
					switched = 1;
				}
			}
			if (ev.type == KeyPress) printf("EVENT KeyPress kc=%u\n", (unsigned)ev.xkey.keycode);
			if (ev.type == MotionNotify) {
				if (ev.xmotion.x != last_x || ev.xmotion.y != last_y) {
					printf("EVENT Motion at %d,%d\n", ev.xmotion.x, ev.xmotion.y);
					last_x = ev.xmotion.x; last_y = ev.xmotion.y;
				}
			}
			fflush(stdout);
		}

		if (!switched && time(NULL) - start >= 3) {
			switched = 1;
			do_mode_switch();
		}
	}
	return 0;
}
