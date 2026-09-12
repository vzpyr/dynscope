#define _POSIX_C_SOURCE 200809L

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: xtest_clip set|pset|get|pget [text]\n");
		return 64;
	}

	Display *dpy = XOpenDisplay(NULL);
	if (dpy == NULL) {
		fprintf(stderr, "xtest_clip: cannot open display\n");
		return 1;
	}

	Window win = XCreateSimpleWindow(dpy, DefaultRootWindow(dpy), 0, 0, 100, 100, 0, 0, 0);
	XSelectInput(dpy, win, StructureNotifyMask | PropertyChangeMask);
	XStoreName(dpy, win, "xtest_clip");
	XMapWindow(dpy, win);
	XFlush(dpy);

	while (1) {
		XEvent ev;
		XNextEvent(dpy, &ev);
		if (ev.type == MapNotify)
			break;
	}

	Atom clipboard = XInternAtom(dpy, "CLIPBOARD", False);
	Atom primary = XA_PRIMARY;
	Atom utf8 = XInternAtom(dpy, "UTF8_STRING", False);
	Atom targets = XInternAtom(dpy, "TARGETS", False);
	Atom text_atom = XInternAtom(dpy, "TEXT", False);

	if (strcmp(argv[1], "set") == 0 || strcmp(argv[1], "pset") == 0) {
		if (argc < 3) {
			fprintf(stderr, "xtest_clip: missing text\n");
			return 64;
		}
		Atom sel = strcmp(argv[1], "set") == 0 ? clipboard : primary;
		XSetSelectionOwner(dpy, sel, win, CurrentTime);
		if (XGetSelectionOwner(dpy, sel) != win) {
			fprintf(stderr, "xtest_clip: failed to acquire selection\n");
			return 1;
		}
		fprintf(stderr, "xtest_clip: owning %s with: %s\n", argv[1], argv[2]);

		char *text = strdup(argv[2]);
		size_t text_len = strlen(text);
		while (1) {
			XEvent ev;
			XNextEvent(dpy, &ev);
			if (ev.type == SelectionRequest) {
				XSelectionRequestEvent *req = &ev.xselectionrequest;
				XEvent reply;
				memset(&reply, 0, sizeof(reply));
				reply.xselection.type = SelectionNotify;
				reply.xselection.display = req->display;
				reply.xselection.selection = req->selection;
				reply.xselection.requestor = req->requestor;
				reply.xselection.time = req->time;
				reply.xselection.target = req->target;
				reply.xselection.property = None;
				if (req->target == targets) {
					Atom list[4] = {targets, utf8, XA_STRING, text_atom};
					XChangeProperty(dpy, req->requestor, req->property, XA_ATOM, 32, PropModeReplace, (unsigned char *)list, 4);
					reply.xselection.property = req->property;
				} else if (req->target == utf8 || req->target == XA_STRING || req->target == text_atom) {
					XChangeProperty(dpy, req->requestor, req->property, req->target, 8, PropModeReplace, (unsigned char *)text, (int)text_len);
					reply.xselection.property = req->property;
				}
				XSendEvent(dpy, req->requestor, False, NoEventMask, &reply);
				XFlush(dpy);
			}
		}
	}

	if (strcmp(argv[1], "get") == 0 || strcmp(argv[1], "pget") == 0) {
		Atom sel = strcmp(argv[1], "get") == 0 ? clipboard : primary;
		XConvertSelection(dpy, sel, utf8, XA_CUT_BUFFER0, win, CurrentTime);
		while (1) {
			XEvent ev;
			XNextEvent(dpy, &ev);
			if (ev.type != SelectionNotify)
				continue;
			XSelectionEvent *sev = &ev.xselection;
			if (sev->property == None) {
				fprintf(stderr, "xtest_clip: conversion failed\n");
				return 1;
			}
			Atom type;
			int fmt;
			unsigned long nitems, bytes_after;
			unsigned char *data = NULL;
			XGetWindowProperty(dpy, win, XA_CUT_BUFFER0, 0, 1 << 20, True, AnyPropertyType, &type, &fmt, &nitems, &bytes_after, &data);
			if (data != NULL) {
				printf("%.*s\n", (int)nitems, (char *)data);
				XFree(data);
			}
			return 0;
		}
	}

	return 0;
}
