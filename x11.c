#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <math.h>
#include <search.h>

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XTest.h>
#include <X11/extensions/Xrandr.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xfixes.h>

#include "types.h"
#include "misc.h"
#include "platform.h"
#include "x11-keycodes.h"

static Display* xdisp = NULL;
static Window xrootwin;
static Window xwin;
static Pixmap cursor_pixmap;
static Cursor xcursor_blank;

static Atom et_selection_data;
static Atom utf8_string_atom;
static Atom targets_atom;

static Time last_xevent_time;

struct crtc_gamma {
	XRRCrtcGamma* orig;
	XRRCrtcGamma* alt;
};

static struct {
	int opcode;
	int errbase;
	int evbase;
	XRRScreenConfiguration* config;
	XRRScreenResources* resources;
	struct crtc_gamma* crtc_gammas;
} xrr;

static struct {
	int opcode;
	int errbase;
	int evbase;
} xi2;

static struct {
	const char* name;
	Atom atom;
} clipboard_xatoms[] = {
	{ "PRIMARY", XA_PRIMARY, },
	{ "CLIPBOARD", None, }, /* filled in in platform_init() */
};

static char* clipboard_text;
static Time xselection_owned_since;

/* Clipboard contents are potentially sensitive, so wipe before freeing. */
static void clear_clipboard_cache(void)
{
	if (clipboard_text)
		explicit_bzero(clipboard_text, strlen(clipboard_text));
	xfree(clipboard_text);
	clipboard_text = NULL;
	xselection_owned_since = 0;
}

/* Mask combining currently-applied modifiers and mouse buttons */
static unsigned int xstate;

#define MouseButtonMask \
	(Button1Mask|Button2Mask|Button3Mask|Button4Mask|Button5Mask)

static struct rectangle screen_dimensions;

/*
 * Point at which we position the mouse pointer when we've got it grabbed
 * (i.e. when we're the master and a remote is active)
 */
static struct xypoint grabbed_restpos;

/* Handler to fire when mouse pointer hits a screen edge */
static edgeevent_handler_t* edgeevent_handler;

struct scheduled_call {
	void (*fn)(void* arg);
	void* arg;
	uint64_t calltime;
	struct scheduled_call* next;
};

static struct scheduled_call* scheduled_calls;

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

/* Some of these may get removed to account for NumLock, etc. */
static unsigned int relevant_modmask = \
	(ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask);

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

static int keygrab_err;

static int xerr_keygrab(Display* d, XErrorEvent* xev)
{
	if (!keygrab_err)
		keygrab_err = xev->error_code;
	return 0;
}

static int set_keygrab(KeyCode kc, unsigned int orig_mask, int grab)
{
	int ni, si, ci;
	int (*prev_errhandler)(Display*, XErrorEvent*);
	unsigned int modmask;
	unsigned int nlk_mask = get_mod_mask(XK_Num_Lock);
	unsigned int slk_mask = get_mod_mask(XK_Scroll_Lock);
	unsigned int clk_mask = LockMask;

	XSync(xdisp, False);
	keygrab_err = 0;
	prev_errhandler = XSetErrorHandler(xerr_keygrab);

	/* Grab with all combinations of NumLock, ScrollLock and CapsLock */
	for (ni = 0; ni < (nlk_mask ? 2 : 1); ni++) {
		for (si = 0; si < (slk_mask ? 2 : 1); si++) {
			for (ci = 0; ci < (clk_mask ? 2 : 1); ci++) {
				modmask = (ci ? clk_mask : 0)
					| (si ? slk_mask : 0)
					| (ni ? nlk_mask : 0);
				if (grab)
					XGrabKey(xdisp, kc, modmask|orig_mask, xrootwin,
					         True, GrabModeAsync, GrabModeAsync);
				else
					XUngrabKey(xdisp, kc, modmask|orig_mask,
					           xrootwin);

				if (keygrab_err)
					goto out;
			}
		}
	}

out:
	XSync(xdisp, False);
	XSetErrorHandler(prev_errhandler);

	return keygrab_err;
}

static int grab_key(KeyCode kc, unsigned int modmask)
{
	int status = set_keygrab(kc, modmask, 1);
	if (status)
		set_keygrab(kc, modmask, 0);
	return status;
}

static inline int match_hotkey(const struct xhotkey* hk, const XKeyEvent* kev)
{
	return kev->keycode == hk->key &&
		(kev->state & relevant_modmask) == (hk->modmask & relevant_modmask);
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

#define XKEYMAP_SIZE 32

struct hotkey_context {
	char keymap_state[XKEYMAP_SIZE];
};

static int do_hotkey(const XKeyEvent* kev)
{
	struct hotkey_context ctx;
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

static keycode_t* get_keymap_modifiers(const char* keymap_state)
{
	int i, bit;
	keycode_t etk;
	KeyCode kc;
	KeySym sym;
	int maxmods = ARR_LEN(xmodifiers) * 2; /* kludge */
	keycode_t* modkeys = xmalloc((maxmods + 1) * sizeof(*modkeys));
	int modcount = 0;

	for (i = 0; i < XKEYMAP_SIZE; i++) {
		if (!keymap_state[i])
			continue;

		for (bit = 0; bit < CHAR_BIT; bit++) {
			if (keymap_state[i] & (1 << bit)) {
				kc = (i * CHAR_BIT) + bit;
				sym = XkbKeycodeToKeysym(xdisp, kc, 0, 0);
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

keycode_t* get_current_modifiers(void)
{
	char keystate[XKEYMAP_SIZE];
	XQueryKeymap(xdisp, keystate);
	return get_keymap_modifiers(keystate);
}

keycode_t* get_hotkey_modifiers(hotkey_context_t ctx)
{
	return get_keymap_modifiers(ctx->keymap_state);
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
			initerr("Invalid key: '%s'\n", tmp);
			status = -1;
			goto out;
		}

		if (!IsModifierKey(sym)) {
			if (*kc) {
				initerr("Invalid hotkey '%s': multiple non-modifier "
				        "keys\n", ks);
				status = -1;
				goto out;
			}
			*kc = XKeysymToKeycode(xdisp, sym);
			if (!*kc) {
				initerr("No keycode for keysym '%s'\n", tmp);
				status = -1;
				goto out;
			}
		} else {
			initerr("'%s' is not a valid hotkey key\n", tmp);
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
	int status;
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
		initerr("hotkey '%s' conflicts with an earlier hotkey binding\n",
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

	status = grab_key(kc, modmask);

	switch (status) {
	case 0:
		break;

	case BadAccess:
		initerr("Failed to bind hotkey \"%s\" (already bound by another "
		        "process?)\n", keystr);
		break;

	case BadValue:
		initerr("Invalid hotkey \"%s\" (?)\n", keystr);
		break;

	default:
		initerr("Failed to bind hotkey \"%s\" for mysterious reasons...\n",
		        keystr);
		break;
	}

	return status ? -1 : 0;
}

static int xrr_init(void)
{
	int i;
	int evt, err;
	int maj, min;

	if (!XQueryExtension(xdisp, "RANDR", &xrr.opcode, &evt, &err)) {
		initerr("XRandR extension not found\n");
		return -1;
	}

	if (!XRRQueryExtension(xdisp, &xrr.evbase, &xrr.errbase)
	    || !XRRQueryVersion(xdisp, &maj, &min)) {
		initerr("XRandR extension unavailable\n");
		return -1;
	}

	debug("XRandR extension version %d.%d\n", maj, min);

	xrr.resources = XRRGetScreenResources(xdisp, xrootwin);
	if (!xrr.resources) {
		initerr("XRRGetScreenResources() failed\n");
		return -1;
	}

	xrr.crtc_gammas = xmalloc(xrr.resources->ncrtc * sizeof(*xrr.crtc_gammas));

	for (i = 0; i < xrr.resources->ncrtc; i++) {
		xrr.crtc_gammas[i].orig = XRRGetCrtcGamma(xdisp, xrr.resources->crtcs[i]);
		xrr.crtc_gammas[i].alt = XRRAllocGamma(xrr.crtc_gammas[i].orig->size);
	}

	XRRSelectInput(xdisp, xrootwin, RRScreenChangeNotifyMask);

	return 0;
}

static void xrr_exit(void)
{
	int i;
	struct scheduled_call* sc;

	for (i = 0; i < xrr.resources->ncrtc; i++) {
		XRRFreeGamma(xrr.crtc_gammas[i].orig);
		XRRFreeGamma(xrr.crtc_gammas[i].alt);
	}
	xfree(xrr.crtc_gammas);

	XRRFreeScreenResources(xrr.resources);
	XRRFreeScreenConfigInfo(xrr.config);

	while (scheduled_calls) {
		sc = scheduled_calls;
		scheduled_calls = sc->next;
		xfree(sc);
	}
}

static int xi2_init(void)
{
	Status status;
	unsigned char rawmask[XIMaskLen(XI_LASTEVENT)];
	int maj = 2, min = 0;
	XIEventMask ximask;

	if (!XQueryExtension(xdisp, "XInputExtension", &xi2.opcode, &xi2.evbase,
	                     &xi2.errbase)) {
		initerr("XInputExtension unavailable\n");
		return -1;
	}

	if (XIQueryVersion(xdisp, &maj, &min)) {
		initerr("XIQueryVersion() failed\n");
		return -1;
	}

	debug("XInput extension version %d.%d\n", maj, min);

	memset(rawmask, 0, sizeof(rawmask));
	ximask.mask = rawmask;
	ximask.mask_len = sizeof(rawmask);
	ximask.deviceid = XIAllMasterDevices;
	XISetMask(ximask.mask, XI_BarrierHit);
	XISetMask(ximask.mask, XI_BarrierLeave);
	status = XISelectEvents(xdisp, xrootwin, &ximask, 1);

	return status ? -1 : 0;
}

static int xtst_init(void)
{
	int evbase, errbase;
	int maj, min;

	if (!XTestQueryExtension(xdisp, &evbase, &errbase, &maj, &min)) {
		initerr("XTest extension unavailable\n");
		return -1;
	}

	debug("XTest extension version %d.%d\n", maj, min);

	return 0;
}

static int xfixes_init(void)
{
	int maj, min;

	if (!XFixesQueryVersion(xdisp, &maj, &min) || maj < 5) {
		initerr("XFixes too old (have %d.%d, need 5.0+)\n", maj, min);
		return -1;
	}

	debug("XFixes extension version %d.%d\n", maj, min);

	return 0;
}

struct ptrbar {
	PointerBarrier bar;
	direction_t dir;
};

/* Root of a tsearch() tree, so we can look up a struct ptrbar from a PointerBarrier */
static void* pointer_barriers = NULL;

static int pbcmp(const void* va, const void* vb)
{
	const struct ptrbar* a = va;
	const struct ptrbar* b = vb;
	return (a->bar > b->bar) ? 1 : (a->bar < b->bar) ? -1 : 0;
}

/* Add pb to pointer_barriers */
static inline void add_ptrbar(const struct ptrbar* pb)
{
	struct ptrbar* old = tsearch(pb, &pointer_barriers, pbcmp);
	if (*(struct ptrbar**)old != pb)
		errlog("PointerBarrier %lu already in pbmap", pb->bar);
}

/* Lookup pbid in pointer_barriers */
static inline struct ptrbar* find_ptrbar(PointerBarrier pbid)
{
	void* v;
	struct ptrbar k = { .bar = pbid, };
	v = tfind(&k, &pointer_barriers, pbcmp);
	return v ? *(struct ptrbar**)v : NULL;
}

static void make_barrier(int x1, int y1, int x2, int y2, int directions)
{
	struct ptrbar* pb;
	PointerBarrier pbid = XFixesCreatePointerBarrier(xdisp, xrootwin, x1, y1, x2, y2,
	                                                 directions, 0, NULL);
	debug2("screen edge pointer barrier (%d, %d), (%d, %d) = %lu\n", x1, y1, x2, y2, pbid);

	pb = xmalloc(sizeof(*pb));
	pb->bar = pbid;

	switch (directions) {
	case BarrierPositiveX:
		pb->dir = LEFT;
		break;
	case BarrierNegativeX:
		pb->dir = RIGHT;
		break;
	case BarrierPositiveY:
		pb->dir = UP;
		break;
	case BarrierNegativeY:
		pb->dir = DOWN;
		break;
	default:
		errlog("pointer barrier direction mask (%d) not a recognized direction",
		       directions);
		abort();
	}

	add_ptrbar(pb);
}

static int point_within_crtc(XRRCrtcInfo* ci, int x, int y)
{
	int xmin = ci->x, xmax = ci->x + ci->width - 1;
	int ymin = ci->y, ymax = ci->y + ci->height - 1;
	return x >= xmin && x <= xmax && y >= ymin && y <= ymax;
}

static int point_on_any_crtc(int ncrtc, XRRCrtcInfo** crtcinfos, int x, int y)
{
	for (int i = 0; i < ncrtc; i++) {
		if (point_within_crtc(crtcinfos[i], x, y))
			return 1;
	}

	return 0;
}

/*
 * Traverse a line (assumed to be a screen edge) between XSTART,YSTART and
 * XEND,YEND creating barriers along it in direction DIR for any segments for
 * which the next pixel over in the opposite direction of DIR is not within
 * any of the NCRTC crtcs in CRTCINFOS.
 */
static void scan_edge(int ncrtc, XRRCrtcInfo** crtcinfos, int xstart, int ystart,
                      int xend, int yend, int dir)
{
	int x, y; /* current point along the line */
	int dx, dy; /* x/y delta for moving along the line we're scanning */
	int qx, qy; /* x/y delta for the neighbor point whose existence we query */
	int within_external_edge = 0; /* state bit */
	int barstart_x, barstart_y; /* where the current barrier started */

	int xmin = xstart < xend ? xstart : xend;
	int xmax = xstart > xend ? xstart : xend;
	int ymin = ystart < yend ? ystart : yend;
	int ymax = ystart > yend ? ystart : yend;

	switch (dir) {
	case BarrierPositiveX:
		assert(xstart == xend);
		assert(ystart != yend);
		dx = 0;
		dy = yend > ystart ? 1 : -1;
		qx = -1;
		qy = 0;
		break;

	case BarrierPositiveY:
		assert(xstart != xend);
		assert(ystart == yend);
		dx = xend > xstart ? 1 : -1;
		dy = 0;
		qx = 0;
		qy = -1;
		break;

	case BarrierNegativeX:
		assert(xstart == xend);
		assert(ystart != yend);
		dx = 0;
		dy = yend > ystart ? 1 : -1;
		qx = 1;
		qy = 0;
		break;

	case BarrierNegativeY:
		assert(xstart != xend);
		assert(ystart == yend);
		dx = xend > xstart ? 1 : -1;
		dy = 0;
		qx = 0;
		qy = 1;
		break;

	default:
		bug("bad direction %d in %s\n", dir, __func__);
		return;
	}

	for (x = xstart, y = ystart;
	     x >= xmin && x <= xmax && y >= ymin && y <= ymax;
	     x += dx, y += dy) {
		int nx = x + qx, ny = y + qy; /* neighbor x/y */

		/*
		 * If the neighbor point doesn't exist on any screen, we must
		 * be somewhere along an external screen edge.
		 */
		int external_edge = !point_on_any_crtc(ncrtc, crtcinfos, nx, ny);

		if (!within_external_edge && external_edge) {
			/* record barrier starting point */
			barstart_x = x;
			barstart_y = y;
		} else if (within_external_edge && !external_edge) {
			/*
			 * Okay, we've hit the end of an external-edge
			 * segment; now pop up a barrier along it.
			 */
			int barend_x = x - dx, barend_y = y - dy;
			make_barrier(barstart_x, barstart_y, barend_x, barend_y, dir);
		}

		within_external_edge = external_edge;
	}

	/*
	 * If we found the beginning of an external-edge segment but never ran
	 * off the end of it, it must run all the way to the edge of the
	 * screen; handle that here.
	 */
	if (within_external_edge) {
		int barend_x = x - dx, barend_y = y - dy;
		make_barrier(barstart_x, barstart_y, barend_x, barend_y, dir);
	}
}

static void setup_crtc_barriers(XRRCrtcInfo* ci, int ncrtc, XRRCrtcInfo** crtcinfos)
{
	unsigned int xmin = ci->x, xmax = ci->x + ci->width - 1;
	unsigned int ymin = ci->y, ymax = ci->y + ci->height - 1;

	scan_edge(ncrtc, crtcinfos, xmin, ymin, xmin, ymax, BarrierPositiveX);
	scan_edge(ncrtc, crtcinfos, xmax, ymin, xmax, ymax, BarrierNegativeX);
	scan_edge(ncrtc, crtcinfos, xmin, ymin, xmax, ymin, BarrierPositiveY);
	scan_edge(ncrtc, crtcinfos, xmin, ymax, xmax, ymax, BarrierNegativeY);
}

static void setup_displays(void)
{
	int i;
	XRRCrtcInfo** crtcinfos;
	unsigned long pixels, maxpixels = 0;
	XRRScreenResources* resources = XRRGetScreenResources(xdisp, xrootwin);

	crtcinfos = xmalloc(resources->ncrtc * sizeof(*crtcinfos));

	for (i = 0; i < resources->ncrtc; i++)
		crtcinfos[i] = XRRGetCrtcInfo(xdisp, resources, resources->crtcs[i]);

	for (i = 0; i < resources->ncrtc; i++) {
		/*
		 * For some reason there seems to be some magical N+1th
		 * pseudo-CRTC with width == 0 and height == 0; let's not try
		 * to set up pointer barriers around that one.
		 */
		if (crtcinfos[i]->width > 0 && crtcinfos[i]->height > 0)
			setup_crtc_barriers(crtcinfos[i], resources->ncrtc, crtcinfos);
		else
			debug("skipping %dx%d crtc %d\n", crtcinfos[i]->width, crtcinfos[i]->height, i);

		/* Set grabbed_restpos to the center of the largest display */
		pixels = crtcinfos[i]->width * crtcinfos[i]->height;
		if (pixels > maxpixels) {
			maxpixels = pixels;
			grabbed_restpos.x = crtcinfos[i]->x + (crtcinfos[i]->width / 2);
			grabbed_restpos.y = crtcinfos[i]->y + (crtcinfos[i]->height / 2);
			debug("grabbed_restpos set to %d,%d\n", grabbed_restpos.x, grabbed_restpos.y);
		}
	}

	for (i = 0; i < resources->ncrtc; i++)
		XRRFreeCrtcInfo(crtcinfos[i]);

	free(crtcinfos);

	XSync(xdisp, False);
	XRRFreeScreenResources(resources);
}

static void destroy_pointer_barriers(void)
{
	struct ptrbar* pb;

	while (pointer_barriers) {
		pb = *(struct ptrbar**)pointer_barriers;
		tdelete(pb, &pointer_barriers, pbcmp);
		XFixesDestroyPointerBarrier(xdisp, pb->bar);
		free(pb);
	}
}

static void log_xerr(unsigned int level, Display* d, XErrorEvent* xev, const char* pfx)
{
	char errbuf[1024];

	XGetErrorText(d,  xev->error_code, errbuf, sizeof(errbuf));
	errbuf[sizeof(errbuf)-1] = '\0';

	mlog(level, "%s X Error: request %hhu.%hhu -> %s\n", pfx, xev->request_code,
	     xev->minor_code, errbuf);
}

static int xerr_abort(Display* d, XErrorEvent* xev)
{
	log_xerr(LL_ERROR, d, xev, "Fatal");
	abort();
}

int platform_init(struct kvmap* params, mousepos_handler_t* mouse_handler,
                  edgeevent_handler_t* edge_handler)
{
	int status;
	unsigned int i;
	Atom atom;
	char bitmap[1] = { 0, };
	XColor black = { .red = 0, .green = 0, .blue = 0, };
	unsigned long blackpx;

	if (params && kvmap_get(params, "DISPLAY"))
		setenv("DISPLAY", kvmap_get(params, "DISPLAY"), 1);

	XSetErrorHandler(xerr_abort);

	x11_keycodes_init();

	xdisp = XOpenDisplay(NULL);
	if (!xdisp) {
		initerr("X11 init: failed to open display\n");
		return -1;
	}

	screen_dimensions.x.min = 0;
	screen_dimensions.x.max = WidthOfScreen(XScreenOfDisplay(xdisp, XDefaultScreen(xdisp))) - 1;
	screen_dimensions.y.min = 0;
	screen_dimensions.y.max = HeightOfScreen(XScreenOfDisplay(xdisp, XDefaultScreen(xdisp))) - 1;

	xrootwin = XDefaultRootWindow(xdisp);

	blackpx = BlackPixel(xdisp, XDefaultScreen(xdisp));
	xwin = XCreateSimpleWindow(xdisp, xrootwin, 0, 0, 1, 1, 0, blackpx, blackpx);

	XStoreName(xdisp, xwin, "enthrall");

	et_selection_data = XInternAtom(xdisp, "ET_SELECTION_DATA", False);
	utf8_string_atom = XInternAtom(xdisp, "UTF8_STRING", False);
	targets_atom = XInternAtom(xdisp, "TARGETS", False);

	for (i = 0; i < ARR_LEN(clipboard_xatoms); i++) {
		if (clipboard_xatoms[i].atom == None) {
			atom = XInternAtom(xdisp, clipboard_xatoms[i].name, False);
			clipboard_xatoms[i].atom = atom;
		}
	}

	/* Create the blank cursor used when grabbing input */
	cursor_pixmap = XCreatePixmapFromBitmapData(xdisp, xrootwin, bitmap, 1, 1, 0, 0, 1);
	xcursor_blank = XCreatePixmapCursor(xdisp, cursor_pixmap, cursor_pixmap,
	                                    &black, &black, 0, 0);

	/* Clear any key grabs (not that any should exist, really...) */
	XUngrabKey(xdisp, AnyKey, AnyModifier, xrootwin);

	/*
	 * Remove scroll lock and num lock from the set of modifiers we pay
	 * attention to in matching hotkey bindings
	 */
	relevant_modmask &= ~(get_mod_mask(XK_Scroll_Lock)
	                      | get_mod_mask(XK_Num_Lock));

	edgeevent_handler = edge_handler;

	status = xrr_init();
	if (!status)
		status = xi2_init();
	if (!status)
		status = xtst_init();
	if (!status)
		status = xfixes_init();

	if (!status)
		setup_displays();

	return status;
}

void platform_exit(void)
{
	struct xhotkey* hk;

	set_display_brightness(1.0);

	destroy_pointer_barriers();

	xrr_exit();
	XFreeCursor(xdisp, xcursor_blank);
	XFreePixmap(xdisp, cursor_pixmap);
	XDestroyWindow(xdisp, xwin);
	XCloseDisplay(xdisp);
	x11_keycodes_exit();

	while (xhotkeys) {
		hk = xhotkeys;
		xhotkeys = hk->next;
		xfree(hk);
	}

	clear_clipboard_cache();
}

#if defined(CLOCK_MONOTONIC_RAW)
#define CGT_CLOCK CLOCK_MONOTONIC_RAW
#elif defined(CLOCK_UPTIME_PRECISE)
#define CGT_CLOCK CLOCK_UPTIME_PRECISE
#else
#error no CGT_CLOCK!
#endif

uint64_t get_microtime(void)
{
	struct timespec ts;
	if (clock_gettime(CGT_CLOCK, &ts)) {
		perror("clock_gettime");
		abort();
	}
	return (ts.tv_sec * 1000000) + (ts.tv_nsec / 1000);
}

void get_screen_dimensions(struct rectangle* d)
{
	*d = screen_dimensions;
}

static struct xypoint get_mousepos_and_mask(unsigned int* mask)
{
	Window xchildwin, root_ret;
	int child_x, child_y, tmp_x, tmp_y;
	struct xypoint pt;
	Bool onscreen = XQueryPointer(xdisp, xrootwin, &root_ret, &xchildwin,
	                              &tmp_x, &tmp_y, &child_x, &child_y,
	                              mask);
	assert(root_ret == xrootwin);

	if (!onscreen) {
		errlog("X11 pointer not on screen?\n");
		abort();
	}

	pt.x = tmp_x;
	pt.y = tmp_y;

	*mask &= relevant_modmask;

	return pt;
}

struct xypoint get_mousepos(void)
{
	unsigned int tmpmask;
	return get_mousepos_and_mask(&tmpmask);
}

void set_mousepos(struct xypoint pt)
{
	XTestFakeMotionEvent(xdisp, -1, pt.x, pt.y, CurrentTime);
	XFlush(xdisp);
}

void move_mousepos(int32_t dx, int32_t dy)
{
	XTestFakeRelativeMotionEvent(xdisp, dx, dy, CurrentTime);
	XFlush(xdisp);
}

static const mousebutton_t pi_mousebuttons[] = {
	[Button1] = MB_LEFT,
	[Button2] = MB_CENTER,
	[Button3] = MB_RIGHT,
	[Button4] = MB_SCROLLUP,
	[Button5] = MB_SCROLLDOWN,
};

static const struct {
	unsigned int button, mask;
} x11_mousebuttons[] = {
	[MB_LEFT]       = { Button1, Button1Mask, },
	[MB_CENTER]     = { Button2, Button2Mask, },
	[MB_RIGHT]      = { Button3, Button3Mask, },
	[MB_SCROLLUP]   = { Button4, Button4Mask, },
	[MB_SCROLLDOWN] = { Button5, Button5Mask, },
};

void do_clickevent(mousebutton_t button, pressrel_t pr)
{
	XTestFakeButtonEvent(xdisp, LOOKUP(button, x11_mousebuttons).button,
	                     pr == PR_PRESS, CurrentTime);
	XFlush(xdisp);

	/* Update modifier/mousebutton state */
	if (pr == PR_PRESS)
		xstate |= LOOKUP(button, x11_mousebuttons).mask;
	else
		xstate &= ~LOOKUP(button, x11_mousebuttons).mask;
}

static unsigned int modmask_for_xkeycode(KeyCode xkc)
{
	KeySym sym = XkbKeycodeToKeysym(xdisp, xkc, 0, 0);

	if (!IsModifierKey(sym))
		return 0;
	else
		return get_mod_mask(sym);
}

void do_keyevent(keycode_t key, pressrel_t pr)
{
	unsigned int modmask;
	KeyCode xkc = keycode_to_xkeycode(xdisp, key);

	XTestFakeKeyEvent(xdisp, xkc, pr == PR_PRESS, CurrentTime);
	XFlush(xdisp);

	modmask = modmask_for_xkeycode(xkc);
	if (modmask) {
		if (pr == PR_PRESS)
			xstate |= modmask;
		else
			xstate &= ~modmask;
	}
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

static struct xypoint saved_mousepos;

#define PointerEventsMask (PointerMotionMask|ButtonPressMask|ButtonReleaseMask)

int grab_inputs(void)
{
	int status;

	saved_mousepos = get_mousepos();

	status = XGrabKeyboard(xdisp, xrootwin, False, GrabModeAsync,
	                       GrabModeAsync, CurrentTime);
	if (status) {
		errlog("Failed to grab keyboard: %s\n", grab_failure_message(status));
		return status;
	}

	status = XGrabPointer(xdisp, xrootwin, False, PointerEventsMask,
	                      GrabModeAsync, GrabModeAsync, None, xcursor_blank, CurrentTime);

	if (status) {
		XUngrabKeyboard(xdisp, CurrentTime);
		errlog("Failed to grab pointer: %s\n", grab_failure_message(status));
		return status;
	}

	set_mousepos(grabbed_restpos);

	XSync(xdisp, False);

	return status;
}

void ungrab_inputs(int restore_mousepos)
{
	XUngrabKeyboard(xdisp, CurrentTime);
	XUngrabPointer(xdisp, CurrentTime);
	if (restore_mousepos)
		set_mousepos(saved_mousepos);
	XSync(xdisp, False);
}

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

static int is_known_clipboard_xatom(Atom atom)
{
	int i;

	if (atom == None)
		return 0;

	for (i = 0; i < ARR_LEN(clipboard_xatoms); i++) {
		if (clipboard_xatoms[i].atom == atom)
			return 1;
	}

	return 0;
}

static void handle_selection_request(const XSelectionRequestEvent* req)
{
	Atom property;
	Atom supported_targets[] = { targets_atom, utf8_string_atom, XA_STRING,  };

	/*
	 * ICCCM sec. 2.2:
	 *
	 * "If the specified property is None , the requestor is an obsolete
	 *  client. Owners are encouraged to support these clients by using
	 *  the specified target atom as the property name to be used for the
	 *  reply."
	 */
	property = (req->property == None) ? req->target : req->property;

	if (!clipboard_text
	    || (req->time != CurrentTime && req->time < xselection_owned_since)
	    || req->owner != xwin || !is_known_clipboard_xatom(req->selection)) {
		property = None;
	} else if (req->target == targets_atom) {
		/* Tell the requesting client what selection formats we support */
		XChangeProperty(xdisp, req->requestor, property, XA_ATOM, 32,
		                PropModeReplace, (unsigned char*)supported_targets,
		                ARR_LEN(supported_targets));
	} else if (req->target == XA_STRING || req->target == utf8_string_atom) {
		/* Send the requested data back to the requesting window */
		XChangeProperty(xdisp, req->requestor, property, req->target, 8,
		                PropModeReplace, (unsigned char*)clipboard_text,
		                strlen(clipboard_text));
	} else {
		property = None;
	}

	/* Acknowledge that the transfer has been made (or failed) */
	if (!send_selection_notify(req, property))
		errlog("Failed to send SelectionNotify to requestor\n");
}

static void handle_keyevent(XKeyEvent* kev, pressrel_t pr)
{
	KeySym sym;
	keycode_t kc;

	sym = XLookupKeysym(kev, 0);
	kc = keysym_to_keycode(sym);

	if (kc == ET_null) {
		warn("No mapping for keysym %lu (%s)\n", sym, XKeysymToString(sym));
		return;
	}

	if (!is_remote(focused_node)) {
		vinfo("keyevent (%s %s, modmask=%#x) with no focused remote\n",
		      XKeysymToString(sym), pr == PR_PRESS ? "pressed" : "released",
		      kev->state);
		return;
	}

	send_keyevent(focused_node->remote, kc, pr);
}

static void handle_grabbed_mousemove(XMotionEvent* mev)
{
	if (mev->x_root == grabbed_restpos.x
	    && mev->y_root == grabbed_restpos.y)
		return;

	send_moverel(focused_node->remote, mev->x_root - grabbed_restpos.x,
	             mev->y_root - grabbed_restpos.y);

	if (abs(mev->x_root - grabbed_restpos.x) > 1
	    || abs(mev->y_root - grabbed_restpos.y) > 1)
		set_mousepos(grabbed_restpos);
}

static void handle_barrier_hit(XIBarrierEvent* ev)
{
	double d;
	unsigned int mask;
	struct xypoint pos;
	struct ptrbar* pb = find_ptrbar(ev->barrier);

	if (pb)
		debug2("BarrierHit [%lu], dir: %u, delta: %.2f/%.2f\n", ev->barrier,
		       pb->dir, ev->dx, ev->dy);
	else {
		errlog("can't find PointerBarrier %lu!", ev->barrier);
		return;
	}

	switch (pb->dir) {
	case LEFT:
		d = -ev->dx;
		break;
	case RIGHT:
		d = ev->dx;
		break;
	case UP:
		d = -ev->dy;
		break;
	case DOWN:
		d = ev->dy;
		break;
	default:
		errlog("invalid pointer barrier direction (%u)", pb->dir);
		return;
	}

	/*
	 * Apparent movement *away* from the barrier on a *hit* event seems to
	 * happen sometimes; ignore it.  (<= instead of < to also ignore
	 * motion purely along the barrier edge.)
	 */
	if (d <= 0.0)
		return;

	pos = get_mousepos_and_mask(&mask);
	if (edgeevent_handler && !mask)
		edgeevent_handler(EE_ARRIVE, pb->dir, lround(d), pos);
}

static void handle_barrier_leave(XIBarrierEvent* ev)
{
	unsigned int mask;
	struct xypoint pos;
	struct ptrbar* pb = find_ptrbar(ev->barrier);

	if (pb)
		debug2("BarrierLeave [%lu], dir: %u, delta: %.2f/%.2f\n", ev->barrier,
		       pb->dir, ev->dx, ev->dy);
	else {
		errlog("can't find PointerBarrier %lu!", ev->barrier);
		return;
	}

	pos = get_mousepos_and_mask(&mask);
	if (edgeevent_handler && !mask)
		edgeevent_handler(EE_DEPART, pb->dir, 0, pos);
}

static void handle_event(XEvent* ev)
{

	switch (ev->type) {
	case MotionNotify:
		if (is_remote(focused_node))
			handle_grabbed_mousemove(&ev->xmotion);
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
		if (!is_remote(focused_node))
			vinfo("ButtonPress with no focused remote\n");
		else
			send_clickevent(focused_node->remote,
			                LOOKUP(ev->xbutton.button, pi_mousebuttons),
			                PR_PRESS);
		break;

	case ButtonRelease:
		if (!is_remote(focused_node))
			vinfo("ButtonRelease with no focused remote\n");
		else
			send_clickevent(focused_node->remote,
			                LOOKUP(ev->xbutton.button, pi_mousebuttons),
			                PR_RELEASE);
		break;

	case SelectionRequest:
		handle_selection_request(&ev->xselectionrequest);
		break;

	case SelectionClear:
		if (ev->xselectionclear.window == xwin
		    && is_known_clipboard_xatom(ev->xselectionclear.selection)) {
			clear_clipboard_cache();
		}
		break;

	case SelectionNotify:
		vinfo("unexpected SelectionNotify event\n");
		break;

	case GenericEvent:
		if (ev->xcookie.extension != xi2.opcode)
			vinfo("unexpected GenericEvent type: %d\n", ev->xcookie.type);
		else if (!XGetEventData(xdisp, &ev->xcookie))
			vinfo("XGetEventData() failed on xi2 GenericEvent\n");
		else {
			switch (ev->xcookie.evtype) {
			case XI_BarrierHit:
				handle_barrier_hit(ev->xcookie.data);
				break;
			case XI_BarrierLeave:
				handle_barrier_leave(ev->xcookie.data);
				break;
			default:
				vinfo("unexpected xi2 evtype: %d\n", ev->xcookie.evtype);
			}
			XFreeEventData(xdisp, &ev->xcookie);
		}
		break;

	case MappingNotify:
		if (ev->xmapping.request == MappingKeyboard
		    || ev->xmapping.request == MappingModifier) {
			debug("refreshing X keyboard mapping\n");
			XRefreshKeyboardMapping(&ev->xmapping);
		} else if (ev->xmapping.request == MappingPointer)
			debug("received MappingPointer notification\n");
		else
			vinfo("received unknown MappingNotify request %d\n",
			      ev->xmapping.request);
		break;

	case MapNotify:
	case UnmapNotify:
	case DestroyNotify:
	case ConfigureNotify:
	case ClientMessage:
	case ReparentNotify:
		/* ignore */
		break;

	default:
		if (ev->type == xrr.evbase + RRScreenChangeNotify) {
			vinfo("RRScreenChangeNotify event received, re-detecting screens\n");
			destroy_pointer_barriers();
			setup_displays();
		} else {
			vinfo("unexpected XEvent type: %d\n", ev->type);
		}
		break;
	}
}

static void process_events(void)
{
	XEvent ev;

	while (XPending(xdisp)) {
		get_xevent(&ev);
		handle_event(&ev);
	}
}

/* The longest we'll wait for a SelectionNotify event before giving up */
#define SELECTION_TIMEOUT_US 100000

char* get_clipboard_text(void)
{
	XEvent ev;
	Atom selection_atom = clipboard_xatoms[0].atom;
	Atom proptype;
	int propformat;
	unsigned long nitems, bytes_remaining;
	unsigned char* prop;
	char* text;
	uint64_t before;

	/*
	 * If we (think we) own the selection, just go ahead and use it
	 * without going through all the X crap.
	 */
	if (xselection_owned_since != 0 && clipboard_text)
		return xstrdup(clipboard_text);

	/* FIXME: delete et_selection_data from xwin before requestion conversion */
	XConvertSelection(xdisp, selection_atom, utf8_string_atom, et_selection_data,
	                  xwin, last_xevent_time);
	XFlush(xdisp);

	before = get_microtime();

	while (get_microtime() - before < SELECTION_TIMEOUT_US) {
		get_xevent(&ev);
		if (ev.type != SelectionNotify) {
			handle_event(&ev);
			continue;
		}

		if (ev.xselection.property == None)
			return xstrdup("");

		if (ev.xselection.selection != selection_atom)
			warn("unexpected selection (%lu) in SelectionNotify event\n",
			     ev.xselection.selection);
		if (ev.xselection.property != et_selection_data)
			warn("unexpected property (%lu) in SelectionNotify event\n",
			     ev.xselection.property);
		if (ev.xselection.requestor != xwin)
			warn("unexpected requestor (%lu) in SelectionNotify event\n",
			     ev.xselection.requestor);
		if (ev.xselection.target != XA_STRING && ev.xselection.target != utf8_string_atom)
			warn("unexpected target (%lu) in SelectionNotify event\n",
			     ev.xselection.target);

		XGetWindowProperty(ev.xselection.display, ev.xselection.requestor,
		                   ev.xselection.property, 0, (1L << 24), True,
		                   AnyPropertyType, &proptype, &propformat, &nitems,
		                   &bytes_remaining, &prop);

		if (proptype != XA_STRING && proptype != utf8_string_atom)
			warn("selection window property has unexpected type (%lu)\n",
			     proptype);
		if (bytes_remaining)
			warn("%lu bytes remaining of selection window property\n",
			     bytes_remaining);
		if (propformat != 8) {
			warn("selection window property has unexpected format (%d)\n",
			     propformat);
			return xstrdup("");
		}

		text = xmalloc(nitems + 1);
		memcpy(text, prop, nitems);
		text[nitems] = '\0';

		XFree(prop);
		return text;
	}

	errlog("timed out waiting for selection\n");
	return xstrdup("");
}

int set_clipboard_text(const char* text)
{
	int i;
	Atom atom;
	Window newowner;

	clear_clipboard_cache();
	clipboard_text = xstrdup(text);

	for (i = 0; i < ARR_LEN(clipboard_xatoms); i++) {
		atom = clipboard_xatoms[i].atom;
		XSetSelectionOwner(xdisp, atom, xwin, CurrentTime);
		newowner = XGetSelectionOwner(xdisp, atom);
		if (newowner != xwin) {
			errlog("failed to take ownership of X selection: "
			       "expected 0x%lx, got 0x%lx\n", xwin, newowner);
			return -1;
		}
	}

	xselection_owned_since = last_xevent_time;

	return 0;
}

static MAKE_GAMMA_SCALE_FN(gamma_scale, unsigned short, lrintf);

static void scale_gamma(const XRRCrtcGamma* from, XRRCrtcGamma* to, float f)
{
	int i;

	assert(from->size == to->size);

	for (i = 0; i < to->size; i++) {
		to->red[i] = gamma_scale(from->red, from->size, i, f);
		to->green[i] = gamma_scale(from->green, from->size, i, f);
		to->blue[i] = gamma_scale(from->blue, from->size, i, f);
	}
}

void set_display_brightness(float f)
{
	int i;

	for (i = 0; i < xrr.resources->ncrtc; i++) {
		scale_gamma(xrr.crtc_gammas[i].orig, xrr.crtc_gammas[i].alt, f);
		XRRSetCrtcGamma(xdisp, xrr.resources->crtcs[i], xrr.crtc_gammas[i].alt);
	}
	XFlush(xdisp);
}

timer_ctx_t schedule_call(void (*fn)(void* arg), void* arg, uint64_t delay)
{
	struct scheduled_call* call;
	struct scheduled_call** prevnext;
	struct scheduled_call* newcall = xmalloc(sizeof(*newcall));

	newcall->fn = fn;
	newcall->arg = arg;
	newcall->calltime = get_microtime() + delay;

	for (prevnext = &scheduled_calls, call = *prevnext;
	     call;
	     prevnext = &call->next, call = call->next) {
		if (newcall->calltime < call->calltime)
			break;
	}

	newcall->next = call;
	*prevnext = newcall;

	return newcall;
}

int cancel_call(timer_ctx_t timer)
{
	struct scheduled_call* call;
	struct scheduled_call** prevnext;
	struct scheduled_call* target = timer;

	for (prevnext = &scheduled_calls, call = *prevnext;
	     call;
	     prevnext = &call->next, call = call->next) {
		if (call == target) {
			*prevnext = call->next;
			xfree(call);
			return 1;
		}
	}

	return 0;
}

struct fdmon_ctx {
	int fd;
	fdmon_callback_t readcb, writecb;
	void* arg;
	uint32_t flags;
	int refcount;

	struct fdmon_ctx* next;
	struct fdmon_ctx* prev;
};

static struct {
	struct fdmon_ctx* head;
	struct fdmon_ctx* tail;
} monitored_fds = {
	.head = NULL,
	.tail = NULL,
};

struct fdmon_ctx* fdmon_register_fd(int fd, fdmon_callback_t readcb,
                                    fdmon_callback_t writecb, void* arg)
{
	struct fdmon_ctx* ctx = xmalloc(sizeof(*ctx));

	ctx->fd = fd;
	ctx->readcb = readcb;
	ctx->writecb = writecb;
	ctx->arg = arg;
	ctx->flags = 0;
	ctx->refcount = 1;

	ctx->next = monitored_fds.head;
	if (ctx->next)
		ctx->next->prev = ctx;
	monitored_fds.head = ctx;

	ctx->prev = NULL;

	if (!monitored_fds.tail)
		monitored_fds.tail = ctx;

	return ctx;
}

static void fdmon_unref(struct fdmon_ctx* ctx)
{
	assert(ctx->refcount > 0);
	ctx->refcount -= 1;

	if (ctx->refcount)
		return;

	if (!ctx->prev)
		monitored_fds.head = ctx->next;

	if (!ctx->next)
		monitored_fds.tail = ctx->prev;

	if (ctx->next)
		ctx->next->prev = ctx->prev;

	if (ctx->prev)
		ctx->prev->next = ctx->next;

	xfree(ctx);
}

static void fdmon_ref(struct fdmon_ctx* ctx)
{
	assert(ctx->refcount > 0);
	ctx->refcount += 1;
}

void fdmon_unregister(struct fdmon_ctx* ctx)
{
	fdmon_unmonitor(ctx, FM_READ|FM_WRITE);
	fdmon_unref(ctx);
}

void fdmon_monitor(struct fdmon_ctx* ctx, uint32_t flags)
{
	if (flags & ~(FM_READ|FM_WRITE)) {
		errlog("invalid fdmon flags: %u\n", flags);
		abort();
	}

	ctx->flags |= flags;
}

void fdmon_unmonitor(struct fdmon_ctx* ctx, uint32_t flags)
{
	if (flags & ~(FM_READ|FM_WRITE)) {
		errlog("invalid fdmon flags: %u\n", flags);
		abort();
	}

	ctx->flags &= ~flags;
}

static void run_scheduled_calls(uint64_t when)
{
	struct scheduled_call* call;

	while (scheduled_calls && scheduled_calls->calltime <= when) {
		call = scheduled_calls;
		scheduled_calls = call->next;
		call->fn(call->arg);
		xfree(call);
	}
}

static struct timeval* get_select_timeout(struct timeval* tv, uint64_t now_us)
{
	uint64_t maxwait_us;

	if (scheduled_calls) {
		maxwait_us = scheduled_calls->calltime - now_us;
		tv->tv_sec = maxwait_us / 1000000;
		tv->tv_usec = maxwait_us % 1000000;
		return tv;
	} else {
		return NULL;
	}
}

static void handle_fds(void)
{
	int status, nfds = 0;
	fd_set rfds, wfds;
	struct timeval sel_wait;
	uint64_t now_us;
	struct fdmon_ctx* mfd;
	struct fdmon_ctx* next_mfd;
	int xfd = xdisp ? XConnectionNumber(xdisp) : -1;

	FD_ZERO(&rfds);
	FD_ZERO(&wfds);

	now_us = get_microtime();

	run_scheduled_calls(now_us);

	if (xfd >= 0)
		fdset_add(xfd, &rfds, &nfds);

	for (mfd = monitored_fds.head; mfd; mfd = mfd->next) {
		if (mfd->flags & FM_READ)
			fdset_add(mfd->fd, &rfds, &nfds);
		if (mfd->flags & FM_WRITE)
			fdset_add(mfd->fd, &wfds, &nfds);
	}

	status = select(nfds, &rfds, &wfds, NULL, get_select_timeout(&sel_wait, now_us));
	if (status < 0 && errno != EINTR) {
		perror("select");
		exit(1);
	}

	for (mfd = monitored_fds.head; mfd; mfd = next_mfd) {
		/*
		 * Callbacks could unregister mfd, so we ref/unref it around
		 * the body of this loop
		 */
		fdmon_ref(mfd);

		if ((mfd->flags & FM_READ) && FD_ISSET(mfd->fd, &rfds))
			mfd->readcb(mfd, mfd->arg);

		if ((mfd->flags & FM_WRITE) && FD_ISSET(mfd->fd, &wfds))
			mfd->writecb(mfd, mfd->arg);

		next_mfd = mfd->next;
		fdmon_unref(mfd);
	}

	if (xfd >= 0
	    && (FD_ISSET(xfd, &rfds) || XPending(xdisp)))
		process_events();
}

void run_event_loop(void)
{
	for (;;)
		handle_fds();
}
