#include <stdlib.h>
#include <stdio.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>

#include "types.h"
#include "misc.h"
#include "platform.h"

#include "proto.h"

static Display* xdisp;
static Window xrootwin;
static Pixmap cursor_pixmap;
static Cursor xcursor_blank;

static struct {
	int32_t x, y;
} screen_dimensions;

static struct xypoint screen_center;

struct hotkey {
	KeySym sym;
	unsigned int modmask;
};

#define SWITCHMODMASK (ControlMask|Mod1Mask|Mod4Mask)

static const struct hotkey switchkeys[] = {
	[LEFT] = { .sym = XK_a, .modmask = SWITCHMODMASK, },
	[RIGHT] = { .sym = XK_d, .modmask = SWITCHMODMASK, },
	[UP] = { .sym = None, .modmask = 0, },
	[DOWN] = { .sym = None, .modmask = 0, },
};

static inline int match_hotkey(const struct hotkey* hk, const XKeyEvent* kev)
{
	return kev->keycode == XKeysymToKeycode(xdisp, hk->sym)
		&& kev->state == hk->modmask;
}

static direction_t switch_direction(const XKeyEvent* kev)
{
	direction_t d;
	for_each_direction (d) {
		if (match_hotkey(&switchkeys[d], kev))
			return d;
	}
	return -1;
}

static void transfer_clipboard(struct remote* from, struct remote* to)
{
	char* cliptext;
	struct message msg;

	if (from) {
		msg.type = MT_GETCLIPBOARD;
		send_message(from->sock, &msg);
		receive_message(from->sock, &msg);
		if (msg.type != MT_SETCLIPBOARD) {
			fprintf(stderr, "remote '%s' misbehaving, disconnecting\n",
			        from->alias);
			disconnect_remote(from, CS_FAILED);
		}
		cliptext = xmalloc(msg.setclipboard.length + 1);
		read_all(from->sock, cliptext, msg.setclipboard.length);
		cliptext[msg.setclipboard.length] = '\0';
	} else {
		cliptext = get_clipboard_text();
		assert(strlen(cliptext) <= UINT32_MAX);
	}

	if (to) {
		msg.type = MT_SETCLIPBOARD;
		msg.setclipboard.length = strlen(cliptext);
		send_message(to->sock, &msg);
		write_all(to->sock, cliptext, msg.setclipboard.length);
	} else
		set_clipboard_text(cliptext);

	xfree(cliptext);
}

static struct xypoint saved_master_mousepos;

static void switch_to_neighbor(direction_t dir)
{
	struct remote* switch_to = active_remote;
	struct neighbor* n = &(active_remote ? active_remote->neighbors : config->neighbors)[dir];

	switch (n->type) {
	case NT_NONE:
		return;

	case NT_MASTER:
		switch_to = NULL;
		break;

	case NT_REMOTE:
		switch_to = n->node;
		if (switch_to->state != CS_CONNECTED) {
			fprintf(stderr, "remote '%s' not connected, can't switch to\n",
			        switch_to->alias);
			return;
		}
		break;

	default:
		fprintf(stderr, "unexpected neighbor type %d\n", n->type);
		return;
	}

	if (active_remote && !switch_to) {
		ungrab_inputs();
		set_mousepos(saved_master_mousepos);
	} else if (!active_remote && switch_to) {
		saved_master_mousepos = get_mousepos();
		grab_inputs();
	}

	transfer_clipboard(active_remote, switch_to);

	active_remote = switch_to;
}

static unsigned int get_mod_mask(KeySym modsym)
{
	static const unsigned int modifier_masks[] = {
		ShiftMask, LockMask, ControlMask, Mod1Mask,
		Mod2Mask, Mod3Mask, Mod4Mask, Mod5Mask,
	};
	int i;
	unsigned int modmask = 0;
	KeyCode nlkc = XKeysymToKeycode(xdisp, modsym);
	XModifierKeymap* modmap = XGetModifierMapping(xdisp);

	for (i = 0; i < 8 * modmap->max_keypermod; i++) {
		if (modmap->modifiermap[i] == nlkc) {
			modmask = modifier_masks[i / modmap->max_keypermod];
			break;
		}
	}

	XFreeModifiermap(modmap);

	return modmask;
}

static void grab_key_by_sym(KeySym sym, unsigned int orig_mask, int flush)
{
	int ni, si, ci;
	unsigned int modmask;
	unsigned int nlk_mask = get_mod_mask(XK_Num_Lock);
	unsigned int slk_mask = get_mod_mask(XK_Scroll_Lock);
	unsigned int clk_mask = LockMask;
	KeyCode code = XKeysymToKeycode(xdisp, sym);

	if (!code) {
		fprintf(stderr, "keysym %lu not defined\n", sym);
		return;
	}

	/* Grab with all combinations of NumLock, ScrollLock and CapsLock */
	for (ni = 0; ni < (nlk_mask ? 2 : 1); ni++) {
		for (si = 0; si < (slk_mask ? 2 : 1); si++) {
			for (ci = 0; ci < (clk_mask ? 2 : 1); ci++) {
				modmask = (ci ? clk_mask : 0)
					| (si ? slk_mask : 0)
					| (ni ? nlk_mask : 0);
				XGrabKey(xdisp, code, modmask | orig_mask, xrootwin,
				         True, GrabModeAsync, GrabModeAsync);
			}
		}
	}

	if (flush)
		XFlush(xdisp);
}

int platform_init(void)
{
	char bitmap[1] = { 0, };
	XColor black = { .red = 0, .green = 0, .blue = 0, };

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

	/* Create the blank cursor used when grabbing input */
	cursor_pixmap = XCreatePixmapFromBitmapData(xdisp, xrootwin, bitmap, 1, 1, 0, 0, 1);
	xcursor_blank = XCreatePixmapCursor(xdisp, cursor_pixmap, cursor_pixmap,
	                                    &black, &black, 0, 0);

	/* Grab hotkeys */
	XUngrabKey(xdisp, AnyKey, AnyModifier, xrootwin);
	grab_key_by_sym(XK_a, ControlMask|Mod1Mask|Mod4Mask, 0);
	grab_key_by_sym(XK_d, ControlMask|Mod1Mask|Mod4Mask, 1);

	return XConnectionNumber(xdisp);
}

void platform_exit(void)
{
	XFreeCursor(xdisp, xcursor_blank);
	XFreePixmap(xdisp, cursor_pixmap);
	XCloseDisplay(xdisp);
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
#define KeyEventsMask (KeyPressMask|KeyReleaseMask)

#define RemoteModeEventMask (PointerEventsMask|KeyEventsMask)
#define MasterModeEventMask (PointerMotionMask)

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

static void handle_event(const XEvent* ev)
{
	struct message msg;
	direction_t dir;

	switch (ev->type) {
	case MotionNotify:
		if (ev->xmotion.x_root == screen_center.x
		    && ev->xmotion.y_root == screen_center.y)
			break;

		msg.type = MT_MOVEREL;
		msg.moverel.dx = ev->xmotion.x_root - last_seen_mousepos.x;
		msg.moverel.dy = ev->xmotion.y_root - last_seen_mousepos.y;
		send_message(active_remote->sock, &msg);

		if (abs(ev->xmotion.x_root - screen_center.x) > 100
		    || abs(ev->xmotion.y_root - screen_center.y) > 100) {
			set_mousepos(screen_center);
			last_seen_mousepos = screen_center;
		} else {
			last_seen_mousepos = (struct xypoint){ .x = ev->xmotion.x_root, .y = ev->xmotion.y_root, };
		}
		break;

	case KeyPress:
		dir = switch_direction(&ev->xkey);
		if (dir != NO_DIR)
			switch_to_neighbor(dir);
		else
			printf("XKeyPressedEvent: %d\n", ev->xkey.keycode);
		break;

	case KeyRelease:
		printf("XKeyReleasedEvent: %d\n", ev->xkey.keycode);
		break;

	case ButtonPress:
		msg.type = MT_CLICKEVENT;
		msg.clickevent.button = LOOKUP(ev->xbutton.button, pi_mousebuttons);
		msg.clickevent.pressrel = PR_PRESS;
		send_message(active_remote->sock, &msg);
		break;

	case ButtonRelease:
		msg.type = MT_CLICKEVENT;
		msg.clickevent.button = LOOKUP(ev->xbutton.button, pi_mousebuttons);
		msg.clickevent.pressrel = PR_RELEASE;
		send_message(active_remote->sock, &msg);
		break;

	default:
		printf("Unknown XEvent type: %d\n", ev->type);
		break;
	}
}

void process_events(void)
{
	XEvent ev;

	while (XPending(xdisp)) {
		XNextEvent(xdisp, &ev);
		handle_event(&ev);
	}
}
