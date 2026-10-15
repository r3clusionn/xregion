/* xregion: select a screen region with the mouse and print its geometry.
 *
 * A small replacement for hacksaw/slop in screenshot pipelines:
 *     g=$(xregion) && grab -g "$g" out.png
 * Drag to select a rectangle; click (without dragging) to select the window
 * under the pointer; Esc or the right button cancels (exit status 1).
 *
 * The rectangle and the guide lines are thin override-redirect windows filled
 * with one colour, so it needs no compositor and works at any X depth (24 or 30).
 * The rectangle is drawn just outside the selection, so it can never end up
 * in the screenshot.
 */
#define _POSIX_C_SOURCE 200809L
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static Display *dpy;
static Window root, edge[4], guide[2]; /* edges: top, bottom, left, right; guides: vertical, horizontal */
static int sw, sh;

static void usage(void)
{
	fputs("usage: xregion [-f format] [-c colour] [-t thickness] [-g guide-thickness] [-n]\n"
	      "  -f  output format: %x %y %w %h, %g = %wx%h+%x+%y (default), %i = window id, %% = %\n"
	      "  -c  line colour, #rrggbb (default #7f7f7f)\n"
	      "  -t  rectangle line thickness in pixels (default 2)\n"
	      "  -g  guide line thickness in pixels (default 1)\n"
	      "  -n  no guide lines\n", stderr);
	exit(2);
}

static void nap(long ms)
{
	struct timespec ts = { 0, ms * 1000000L };
	nanosleep(&ts, NULL);
}

static Window bar(unsigned long pixel)
{
	XSetWindowAttributes a = { .override_redirect = True, .background_pixel = pixel };
	return XCreateWindow(dpy, root, 0, 0, 1, 1, 0, CopyFromParent, InputOutput, CopyFromParent,
			     CWOverrideRedirect | CWBackPixel, &a);
}

static void place(Window w, int x, int y, int width, int height)
{
	if (width < 1) width = 1;
	if (height < 1) height = 1;
	XMoveResizeWindow(dpy, w, x, y, (unsigned)width, (unsigned)height);
}

/* the rectangle, drawn outside the selection [x, x+w) x [y, y+h) */
static void draw_rect(int x, int y, int w, int h, int t)
{
	place(edge[0], x - t, y - t, w + 2 * t, t);
	place(edge[1], x - t, y + h, w + 2 * t, t);
	place(edge[2], x - t, y, t, h);
	place(edge[3], x + w, y, t, h);
}

/* The topmost window at (x, y) on the root, leaving out xregion's own lines (the
 * guides cross right under the pointer, so the click's subwindow would be one). */
static Window window_at(int x, int y)
{
	Window r, p, *kids = NULL, found = None;
	unsigned n = 0;
	if (!XQueryTree(dpy, root, &r, &p, &kids, &n)) return None;
	for (unsigned i = n; i-- > 0 && found == None;) { /* stacking order: topmost last */
		int ours = 0;
		for (int j = 0; j < 4; j++) ours |= kids[i] == edge[j];
		for (int j = 0; j < 2; j++) ours |= kids[i] == guide[j];
		XWindowAttributes a;
		if (ours || !XGetWindowAttributes(dpy, kids[i], &a) || a.map_state != IsViewable || a.class != InputOutput) continue;
		if (x >= a.x && y >= a.y && x < a.x + a.width + 2 * a.border_width && y < a.y + a.height + 2 * a.border_width)
			found = kids[i];
	}
	if (kids) XFree(kids);
	return found;
}

static void draw_guides(int x, int y, int g)
{
	place(guide[0], x - g / 2, 0, g, sh);
	place(guide[1], 0, y - g / 2, sw, g);
}

int main(int argc, char **argv)
{
	const char *fmt = "%g", *colour = "#7f7f7f";
	int thick = 2, gthick = 1, guides = 1;
	for (int i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "-n")) guides = 0;
		else if (i + 1 < argc && !strcmp(argv[i], "-f")) fmt = argv[++i];
		else if (i + 1 < argc && !strcmp(argv[i], "-c")) colour = argv[++i];
		else if (i + 1 < argc && !strcmp(argv[i], "-t")) thick = atoi(argv[++i]);
		else if (i + 1 < argc && !strcmp(argv[i], "-g")) gthick = atoi(argv[++i]);
		else usage();
	}
	if (thick < 1 || gthick < 1) usage();
	for (const char *p = fmt; *p; p++) /* reject unknown codes before grabbing anything */
		if (*p == '%' && (!*++p || !strchr("xywhgi%", *p))) usage();

	if (!(dpy = XOpenDisplay(NULL))) {
		fputs("xregion: can't open display\n", stderr);
		return 1;
	}
	int scr = DefaultScreen(dpy);
	root = RootWindow(dpy, scr);
	sw = DisplayWidth(dpy, scr);
	sh = DisplayHeight(dpy, scr);

	/* the colour as a pixel of the root visual (10 bits per channel at depth 30) */
	XColor col;
	char hex[64];
	snprintf(hex, sizeof hex, "%s%s", colour[0] == '#' ? "" : "#", colour);
	if (!XParseColor(dpy, DefaultColormap(dpy, scr), colour[0] == '#' ? colour : hex, &col) ||
	    !XAllocColor(dpy, DefaultColormap(dpy, scr), &col)) {
		fprintf(stderr, "xregion: bad colour %s\n", colour);
		return 2;
	}
	for (int i = 0; i < 4; i++) edge[i] = bar(col.pixel);
	for (int i = 0; i < 2; i++) guide[i] = bar(col.pixel);

	/* XCreateFontCursor goes through libXcursor when it's there: the themed crosshair at XCURSOR_SIZE */
	Cursor cross = XCreateFontCursor(dpy, XC_crosshair);
	unsigned evmask = ButtonPressMask | ButtonReleaseMask | PointerMotionMask;
	/* launched from a key binding, the window manager may still hold the keyboard: retry for 1 s */
	int gp = GrabNotViewable, gk = GrabNotViewable;
	for (int i = 0; i < 100; i++) {
		if (gp != GrabSuccess) gp = XGrabPointer(dpy, root, False, evmask, GrabModeAsync, GrabModeAsync, None, cross, CurrentTime);
		if (gk != GrabSuccess) gk = XGrabKeyboard(dpy, root, False, GrabModeAsync, GrabModeAsync, CurrentTime);
		if (gp == GrabSuccess && gk == GrabSuccess) break;
		nap(10);
	}
	if (gp != GrabSuccess) {
		fputs("xregion: can't grab the pointer\n", stderr);
		return 1;
	}

	Window rw, cw;
	int px, py, wx, wy;
	unsigned mask;
	XQueryPointer(dpy, root, &rw, &cw, &px, &py, &wx, &wy, &mask);
	if (guides) {
		draw_guides(px, py, gthick);
		XMapRaised(dpy, guide[0]);
		XMapRaised(dpy, guide[1]);
	}

	int pressed = 0, x0 = 0, y0 = 0, x = 0, y = 0, w = 0, h = 0, cancel = 0, done = 0;
	Window picked = None;
	KeyCode esc = XKeysymToKeycode(dpy, XK_Escape);
	XEvent ev;
	while (!done) {
		XNextEvent(dpy, &ev);
		switch (ev.type) {
		case KeyPress:
			if (ev.xkey.keycode == esc) cancel = done = 1;
			break;
		case ButtonPress:
			if (ev.xbutton.button == Button3) { cancel = done = 1; break; }
			if (ev.xbutton.button != Button1) break;
			pressed = 1;
			x0 = ev.xbutton.x_root;
			y0 = ev.xbutton.y_root;
			picked = window_at(x0, y0);
			/* hide the guides now, so the windows under them have repainted by the release */
			if (guides) XUnmapWindow(dpy, guide[0]), XUnmapWindow(dpy, guide[1]);
			break;
		case MotionNotify: {
			while (XCheckTypedEvent(dpy, MotionNotify, &ev)) {} /* only the latest position */
			int mx = ev.xmotion.x_root, my = ev.xmotion.y_root;
			if (!pressed) {
				if (guides) draw_guides(mx, my, gthick);
				break;
			}
			x = mx < x0 ? mx : x0;
			y = my < y0 ? my : y0;
			w = abs(mx - x0) + 1;
			h = abs(my - y0) + 1;
			draw_rect(x, y, w, h, thick);
			for (int i = 0; i < 4; i++) XMapRaised(dpy, edge[i]);
			break;
		}
		case ButtonRelease:
			if (!pressed || ev.xbutton.button != Button1) break;
			{
				int mx = ev.xbutton.x_root, my = ev.xbutton.y_root;
				x = mx < x0 ? mx : x0;
				y = my < y0 ? my : y0;
				w = abs(mx - x0) + 1;
				h = abs(my - y0) + 1;
			}
			if (w <= 3 && h <= 3) { /* a click: the window under the pointer (its inside, without the border) */
				XWindowAttributes a;
				if (picked != None && XGetWindowAttributes(dpy, picked, &a)) {
					x = a.x + a.border_width;
					y = a.y + a.border_width;
					w = a.width;
					h = a.height;
				} else {
					picked = root;
					x = y = 0;
					w = sw;
					h = sh;
				}
			} else
				picked = None;
			done = 1;
			break;
		}
	}
	XUngrabPointer(dpy, CurrentTime);
	XUngrabKeyboard(dpy, CurrentTime);
	for (int i = 0; i < 4; i++) XDestroyWindow(dpy, edge[i]);
	for (int i = 0; i < 2; i++) XDestroyWindow(dpy, guide[i]);
	XSync(dpy, False);
	XCloseDisplay(dpy);
	if (cancel) return 1;

	/* keep the selection on the screen */
	if (x < 0) w += x, x = 0;
	if (y < 0) h += y, y = 0;
	if (x + w > sw) w = sw - x;
	if (y + h > sh) h = sh - y;
	if (w < 1 || h < 1) return 1;

	for (const char *p = fmt; *p; p++) {
		if (*p != '%') { putchar(*p); continue; }
		switch (*++p) {
		case 'x': printf("%d", x); break;
		case 'y': printf("%d", y); break;
		case 'w': printf("%d", w); break;
		case 'h': printf("%d", h); break;
		case 'g': printf("%dx%d+%d+%d", w, h, x, y); break;
		case 'i': printf("0x%lx", (unsigned long)(picked ? picked : root)); break;
		case '%': putchar('%'); break;
		}
	}
	putchar('\n');
	return 0;
}
