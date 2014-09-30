#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#include "types.h"
#include "misc.h"
#include "platform.h"
#include "x11-keycodes.h"

#include "proto.h"

static Display* xdisp;
static Window xrootwin;
static Window xwin;
static Pixmap cursor_pixmap;
static Cursor xcursor_blank;

static Atom et_selection_data;
static Atom utf8_string_atom;

static Time last_xevent_time;

#define ET_XSELECTION XA_PRIMARY
static char* clipboard_text;
static Time xselection_owned_since;

static struct {
	int32_t x, y;
} screen_dimensions;

struct xypoint screen_center;

struct xhotkey {
	KeyCode key;
	unsigned int modmask;

	hotkey_callback_t callback;
	void* arg;

	struct xhotkey* next;
};

static struct xhotkey* xhotkeys = NULL;

static const struct {
	const char* name;
	unsigned int mask;
} xmodifiers[] = {
	[ShiftMapIndex]   = { "shift",    ShiftMask,   },
	[LockMapIndex]    = { "lock",     LockMask,    },
	[ControlMapIndex] = { "control",  ControlMask, },
	[Mod1MapIndex]    = { "mod1",     Mod1Mask,    },
	[Mod2MapIndex]    = { "mod2",     Mod2Mask,    },
	[Mod3MapIndex]    = { "mod3",     Mod3Mask,    },
	[Mod4MapIndex]    = { "mod4",     Mod4Mask,    },
	[Mod5MapIndex]    = { "mod5",     Mod5Mask,    },
};

static unsigned int get_mod_mask(KeySym modsym)
{
	int i;
	unsigned int modmask = 0;
	KeyCode nlkc = XKeysymToKeycode(xdisp, modsym);
	XModifierKeymap* modmap = XGetModifierMapping(xdisp);

	for (i = 0; i < 8 * modmap->max_keypermod; i++) {
		if (modmap->modifiermap[i] == nlkc) {
			modmask = xmodifiers[i / modmap->max_keypermod].mask;
			break;
		}
	}

	XFreeModifiermap(modmap);

	return modmask;
}

static void grab_key(KeyCode kc, unsigned int orig_mask)
{
	int ni, si, ci;
	unsigned int modmask;
	unsigned int nlk_mask = get_mod_mask(XK_Num_Lock);
	unsigned int slk_mask = get_mod_mask(XK_Scroll_Lock);
	unsigned int clk_mask = LockMask;

	/* Grab with all combinations of NumLock, ScrollLock and CapsLock */
	for (ni = 0; ni < (nlk_mask ? 2 : 1); ni++) {
		for (si = 0; si < (slk_mask ? 2 : 1); si++) {
			for (ci = 0; ci < (clk_mask ? 2 : 1); ci++) {
				modmask = (ci ? clk_mask : 0)
					| (si ? slk_mask : 0)
					| (ni ? nlk_mask : 0);
				XGrabKey(xdisp, kc, modmask | orig_mask, xrootwin,
				         True, GrabModeAsync, GrabModeAsync);
			}
		}
	}

	XFlush(xdisp);
}

static inline int match_hotkey(const struct xhotkey* hk, const XKeyEvent* kev)
{
	return kev->keycode == hk->key && kev->state == hk->modmask;
}

static const struct xhotkey* find_hotkey(const XKeyEvent* kev)
{
	const struct xhotkey* k;

	for (k = xhotkeys; k; k = k->next) {
		if (match_hotkey(k, kev))
			return k;
	}

	return NULL;
}

struct hotkey_context {
	const XKeyEvent* event;
	char keymap_state[32];
};

static int do_hotkey(const XKeyEvent* kev)
{
	struct hotkey_context ctx = { .event = kev, };
	const struct xhotkey* k = find_hotkey(kev);

	if (k) {
		/*
		 * Possibly racy I think?  Maybe check that keymap state
		 * hasn't changed since we got the hotkey event?
		 */
		XQueryKeymap(xdisp, ctx.keymap_state);
		k->callback(&ctx, k->arg);
	}

	return !!k;
}

keycode_t* get_hotkey_modifiers(hotkey_context_t ctx)
{
	int i, bit;
	keycode_t etk;
	KeyCode kc;
	KeySym sym;
	int maxmods = ARR_LEN(xmodifiers) * 2; /* kludge */
	keycode_t* modkeys = xmalloc((maxmods + 1) * sizeof(*modkeys));
	int modcount = 0;

	for (i = 0; i < ARR_LEN(ctx->keymap_state); i++) {
		if (!ctx->keymap_state[i])
			continue;

		for (bit = 0; bit < CHAR_BIT; bit++) {
			if (ctx->keymap_state[i] & (1 << bit)) {
				kc = (i * CHAR_BIT) + bit;
				sym = XKeycodeToKeysym(xdisp, kc, 0);
				if (!IsModifierKey(sym))
					continue;
				etk = keysym_to_keycode(sym);
				if (etk != ET_null) {
					modkeys[modcount++] = etk;
					if (modcount == maxmods)
						goto out;
				}
			}
		}
	}
out:
	modkeys[modcount] = ET_null;

	return modkeys;
}

static int parse_keystring(const char* ks, KeyCode* kc, unsigned int* modmask)
{
	size_t klen;
	int i, status;
	KeySym sym;
	const char* k = ks;
	/* Scratch string buffer large enough to hold any substring of 'ks' */
	char* tmp = xmalloc(strlen(ks)+1);

	*kc = 0;
	*modmask = 0;

	while (*k) {
		klen = strcspn(k, "+");
		memcpy(tmp, k, klen);
		tmp[klen] = '\0';

		for (i = 0; i < ARR_LEN(xmodifiers); i++) {
			if (!strcasecmp(xmodifiers[i].name, tmp)) {
				*modmask |= xmodifiers[i].mask;
				break;
			}
		}
		/* If we found a modifer key, move on to the next key */
		if (i < ARR_LEN(xmodifiers))
			goto next;

		sym = XStringToKeysym(tmp);
		if (sym == NoSymbol) {
			fprintf(stderr, "Invalid key: '%s'\n", tmp);
			status = -1;
			goto out;
		}

		if (!IsModifierKey(sym)) {
			if (*kc) {
				fprintf(stderr, "Invalid hotkey '%s': multiple "
				        "non-modifier keys\n", ks);
				status = -1;
				goto out;
			}
			*kc = XKeysymToKeycode(xdisp, sym);
			if (!*kc) {
				fprintf(stderr, "No keycode for keysym '%s'\n", tmp);
				status = -1;
				goto out;
			}
		} else {
			fprintf(stderr, "'%s' is not a valid hotkey key\n", tmp);
			status = -1;
			goto out;
		}

	next:
		k += klen;
		if (*k) {
			assert(*k == '+');
			k += 1;
		}
	}

	status = 0;

out:
	xfree(tmp);
	return status;
}

int bind_hotkey(const char* keystr, hotkey_callback_t cb, void* arg)
{
	struct xhotkey* k;
	KeyCode kc;
	unsigned int modmask;
	XKeyEvent kev;

	if (parse_keystring(keystr, &kc, &modmask))
		return -1;

	/*
	 * Mock up a fake XKeyEvent to search for collisions with
	 * already-existing hotkey bindings.
	 */
	kev.keycode = kc;
	kev.state = modmask;

	if (find_hotkey(&kev)) {
		fprintf(stderr, "hotkey '%s' conflicts with an existing hotkey binding\n",
		        keystr);
		return -1;
	}

	k = xmalloc(sizeof(*k));
	k->key = kc;
	k->modmask = modmask;
	k->callback = cb;
	k->arg = arg;
	k->next = xhotkeys;

	xhotkeys = k;

	grab_key(kc, modmask);

	return 0;
}

int platform_init(void)
{
	char bitmap[1] = { 0, };
	XColor black = { .red = 0, .green = 0, .blue = 0, };
	unsigned long blackpx;

	xdisp = XOpenDisplay(NULL);
	if (!xdisp) {
		fprintf(stderr, "X11 init: failed to open display\n");
		return -1;
	}

	screen_dimensions.x = WidthOfScreen(XScreenOfDisplay(xdisp, XDefaultScreen(xdisp)));
	screen_dimensions.y = HeightOfScreen(XScreenOfDisplay(xdisp, XDefaultScreen(xdisp)));

	screen_center.x = screen_dimensions.x / 2;
	screen_center.y = screen_dimensions.y / 2;

	xrootwin = XDefaultRootWindow(xdisp);

	blackpx = BlackPixel(xdisp, XDefaultScreen(xdisp));
	xwin = XCreateSimpleWindow(xdisp, xrootwin, 0, 0, 1, 1, 0, blackpx, blackpx);

	et_selection_data = XInternAtom(xdisp, "ET_SELECTION_DATA", False);
	utf8_string_atom = XInternAtom(xdisp, "UTF8_STRING", False);

	/* Create the blank cursor used when grabbing input */
	cursor_pixmap = XCreatePixmapFromBitmapData(xdisp, xrootwin, bitmap, 1, 1, 0, 0, 1);
	xcursor_blank = XCreatePixmapCursor(xdisp, cursor_pixmap, cursor_pixmap,
	                                    &black, &black, 0, 0);

	/* Clear any key grabs (not that any should exist, really...) */
	XUngrabKey(xdisp, AnyKey, AnyModifier, xrootwin);

	return XConnectionNumber(xdisp);
}

void platform_exit(void)
{
	XFreeCursor(xdisp, xcursor_blank);
	XFreePixmap(xdisp, cursor_pixmap);
	XDestroyWindow(xdisp, xwin);
	XCloseDisplay(xdisp);
}

uint64_t get_microtime(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts)) {
		perror("clock_gettime");
		abort();
	}
	return (ts.tv_sec * 1000000) + (ts.tv_nsec / 1000);
}

struct xypoint get_mousepos(void)
{
	Window xchildwin;
	int child_x, child_y, tmp_x, tmp_y;
	unsigned int tmp_mask;
	struct xypoint pt;
	Bool onscreen = XQueryPointer(xdisp, xrootwin, &xrootwin, &xchildwin,
	                              &tmp_x, &tmp_y, &child_x, &child_y,
	                              &tmp_mask);

	if (!onscreen) {
		fprintf(stderr, "X11 pointer not on screen?\n");
		abort();
	}

	pt.x = tmp_x;
	pt.y = tmp_y;

	return pt;
}

void set_mousepos(struct xypoint pt)
{
	XWarpPointer(xdisp, None, xrootwin, 0, 0, 0, 0, pt.x, pt.y);
	XFlush(xdisp);
}

void move_mousepos(int32_t dx, int32_t dy)
{
	XWarpPointer(xdisp, None, None, 0, 0, 0, 0, dx, dy);
	XFlush(xdisp);
}

void do_clickevent(mousebutton_t button, pressrel_t pr)
{
	fprintf(stderr, "x11 clickevent not yet implemented\n");
}

void do_keyevent(keycode_t key, pressrel_t pr)
{
	fprintf(stderr, "x11 keyevent not yet implemented\n");
}

static inline const char* grab_failure_message(int status)
{
	switch (status) {
	case AlreadyGrabbed: return "AlreadyGrabbed";
	case GrabInvalidTime: return "GrabInvalidTime";
	case GrabFrozen: return "GrabFrozen";
	default: return "(unknown error)";
	}
}

#define PointerEventsMask (PointerMotionMask|ButtonPressMask|ButtonReleaseMask)

int grab_inputs(void)
{
	int status = XGrabKeyboard(xdisp, xrootwin, False, GrabModeAsync,
	                           GrabModeAsync, CurrentTime);
	if (status) {
		fprintf(stderr, "Failed to grab keyboard: %s",
		        grab_failure_message(status));
		return status;
	}

	status = XGrabPointer(xdisp, xrootwin, False, PointerEventsMask,
	                      GrabModeAsync, GrabModeAsync, None, xcursor_blank, CurrentTime);

	if (status) {
		XUngrabKeyboard(xdisp, CurrentTime);
		fprintf(stderr, "Failed to grab pointer: %s",
		        grab_failure_message(status));
		return status;
	}

	XFlush(xdisp);

	return status;
}

void ungrab_inputs(void)
{
	XUngrabKeyboard(xdisp, CurrentTime);
	XUngrabPointer(xdisp, CurrentTime);
	XFlush(xdisp);
}

static struct xypoint last_seen_mousepos;

static const mousebutton_t pi_mousebuttons[] = {
	[Button1] = MB_LEFT,
	[Button2] = MB_CENTER,
	[Button3] = MB_RIGHT,
	[Button4] = MB_SCROLLUP,
	[Button5] = MB_SCROLLDOWN,
};

static void get_xevent(XEvent* e)
{
	XNextEvent(xdisp, e);

	/* This is kind of gross... */

#define GETTIME(type, structname) \
	case type: last_xevent_time = e->x##structname.time; break

	switch (e->type) {
		GETTIME(KeyPress, key);
		GETTIME(KeyRelease, key);
		GETTIME(ButtonPress, button);
		GETTIME(ButtonRelease, button);
		GETTIME(MotionNotify, motion);
		GETTIME(PropertyNotify, property);
		GETTIME(SelectionClear, selectionclear);
		GETTIME(SelectionRequest, selectionrequest);
		GETTIME(SelectionNotify, selection);
#undef GETTIME
	default: break;
	}
}

char* get_clipboard_text(void)
{
	XEvent ev;
	Atom proptype;
	int propformat;
	unsigned long nitems, bytes_remaining;
	unsigned char* prop;
	char* text;

	/*
	 * If we (think we) own the selection, just go ahead and use it
	 * without going through all the X crap.
	 */
	if (xselection_owned_since != 0 && clipboard_text)
		return xstrdup(clipboard_text);

	/* FIXME: delete et_selection_data from xwin before requestion conversion */
	XConvertSelection(xdisp, ET_XSELECTION, XA_STRING, et_selection_data,
	                  xwin, last_xevent_time);
	XFlush(xdisp);

	for (;;) {
		get_xevent(&ev);
		if (ev.type != SelectionNotify) {
			fprintf(stderr, "dropping type %d event while awaiting selection...\n",
			        ev.type);
			continue;
		}

		if (ev.xselection.property == None)
			return xstrdup("");

		if (ev.xselection.selection != ET_XSELECTION)
			fprintf(stderr, "unexpected selection in SelectionNotify event\n");
		if (ev.xselection.property != et_selection_data)
			fprintf(stderr, "unexpected property in SelectionNotify event\n");
		if (ev.xselection.requestor != xwin)
			fprintf(stderr, "unexpected requestor in SelectionNotify event\n");
		if (ev.xselection.target != XA_STRING)
			fprintf(stderr, "unexpected target in SelectionNotify event\n");

		XGetWindowProperty(ev.xselection.display, ev.xselection.requestor,
		                   ev.xselection.property, 0, (1L << 24), True,
		                   AnyPropertyType, &proptype, &propformat, &nitems,
		                   &bytes_remaining, &prop);

		if (proptype != XA_STRING && proptype != utf8_string_atom)
			fprintf(stderr, "selection window property has unexpected type\n");
		if (bytes_remaining)
			fprintf(stderr, "%lu bytes remaining of selection window property\n",
			        bytes_remaining);
		if (propformat != 8) {
			fprintf(stderr, "selection window property has unexpected format (%d)\n",
			        propformat);
			return xstrdup("");
		}

		text = xmalloc(nitems + 1);
		memcpy(text, prop, nitems);
		text[nitems] = '\0';

		XFree(prop);
		return text;
	}

	return NULL;
}

int set_clipboard_text(const char* text)
{
	xfree(clipboard_text);
	clipboard_text = xstrdup(text);

	XSetSelectionOwner(xdisp, ET_XSELECTION, xwin, last_xevent_time);

	if (XGetSelectionOwner(xdisp, ET_XSELECTION) != xwin) {
		fprintf(stderr, "failed to take ownership of X selection\n");
		return -1;
	}

	xselection_owned_since = last_xevent_time;

	return 0;
}

static Status send_selection_notify(const XSelectionRequestEvent* req, Atom property)
{
	XEvent ev;
	XSelectionEvent* resp = &ev.xselection;

	resp->type = SelectionNotify;
	resp->display = req->display;
	resp->requestor = req->requestor;
	resp->selection = req->selection;
	resp->target = req->target;
	resp->property = property;
	resp->time = req->time;

	return XSendEvent(xdisp, req->requestor, False, 0, &ev);
}

static void handle_selection_request(const XSelectionRequestEvent* req)
{
	Atom property;

	if (!clipboard_text
	    || (req->time != CurrentTime && req->time < xselection_owned_since)
	    || req->selection != ET_XSELECTION || req->owner != xwin) {
		property = None;
	} else if (req->target != XA_STRING) {
		property = None;
	} else {
		/*
		 * ICCCM sec. 2.2:
		 *
		 * "If the specified property is None , the requestor is an obsolete
		 *  client. Owners are encouraged to support these clients by using
		 *  the specified target atom as the property name to be used for the
		 *  reply."
		 */
		property = (req->property == None) ? req->target : req->property;

		/* Send the requested data back to the requesting window */
		XChangeProperty(xdisp, req->requestor, property, req->target, 8,
		                PropModeReplace, (unsigned char*)clipboard_text,
		                strlen(clipboard_text));
	}

	/* Acknowledge that the transfer has been made (or failed) */
	if (!send_selection_notify(req, property))
		fprintf(stderr, "Failed to send SelectionNotify to requestor\n");
}

static void handle_keyevent(XKeyEvent* kev, pressrel_t pr)
{
	KeySym sym;
	keycode_t kc;

	if (!active_remote) {
		fprintf(stderr, "keyevent with no active remote\n");
		return;
	}

	sym = XLookupKeysym(kev, 0);
	kc = keysym_to_keycode(sym);

	if (kc == ET_null) {
		fprintf(stderr, "No mapping for keysym %lu (%s)\n", sym,
		        XKeysymToString(sym));
		return;
	}

	send_keyevent(kc, pr);
}

static void handle_event(XEvent* ev)
{
	switch (ev->type) {
	case MotionNotify:
		if (ev->xmotion.x_root == screen_center.x
		    && ev->xmotion.y_root == screen_center.y)
			break;

		send_moverel(ev->xmotion.x_root - last_seen_mousepos.x,
		             ev->xmotion.y_root - last_seen_mousepos.y);

		if (abs(ev->xmotion.x_root - screen_center.x) > 1
		    || abs(ev->xmotion.y_root - screen_center.y) > 1) {
			set_mousepos(screen_center);
			last_seen_mousepos = screen_center;
		} else {
			last_seen_mousepos = (struct xypoint){ .x = ev->xmotion.x_root, .y = ev->xmotion.y_root, };
		}
		break;

	case KeyPress:
		if (!do_hotkey(&ev->xkey))
			handle_keyevent(&ev->xkey, PR_PRESS);
		break;

	case KeyRelease:
		if (!find_hotkey(&ev->xkey))
			handle_keyevent(&ev->xkey, PR_RELEASE);
		break;

	case ButtonPress:
		send_clickevent(LOOKUP(ev->xbutton.button, pi_mousebuttons), PR_PRESS);
		break;

	case ButtonRelease:
		send_clickevent(LOOKUP(ev->xbutton.button, pi_mousebuttons), PR_RELEASE);
		break;

	case SelectionRequest:
		handle_selection_request(&ev->xselectionrequest);
		break;

	case SelectionClear:
		if (ev->xselectionclear.window == xwin
		    && ev->xselectionclear.selection == ET_XSELECTION) {
			xfree(clipboard_text);
			clipboard_text = NULL;
			xselection_owned_since = 0;
		}
		break;

	case SelectionNotify:
		fprintf(stderr, "unexpected SelectionNotify event\n");
		break;

	default:
		fprintf(stderr, "unexpected XEvent type: %d\n", ev->type);
		break;
	}
}

void process_events(void)
{
	XEvent ev;

	while (XPending(xdisp)) {
		get_xevent(&ev);
		handle_event(&ev);
	}
}
