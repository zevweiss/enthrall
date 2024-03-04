#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <mach/mach.h>
#include <mach/mach_time.h>

#include <CoreGraphics/CGEvent.h>
#include <CoreGraphics/CGDirectDisplay.h>
#include <IOKit/hidsystem/event_status_driver.h>

/*
 * Because unfortunately I can't currently figure out how to get just
 * Pasteboard.h (which is in HIServices within ApplicationServices).  Sigh.
 */
#include <Carbon/Carbon.h>

#include "types.h"
#include "misc.h"
#include "platform.h"
#include "osx-keycodes.h"
#include "events.h"

#if CGFLOAT_IS_DOUBLE
#define cground lround
#else
#define cground lroundf
#endif

/*
 * Selected from:
 *   https://developer.apple.com/Library/mac/documentation/Miscellaneous/Reference/UTIRef/Articles/System-DeclaredUniformTypeIdentifiers.html
 */
#define PLAINTEXT CFSTR("public.utf8-plain-text")

static mach_timebase_info_data_t mach_timebase;

static PasteboardRef clipboard;

static struct rectangle screen_dimensions = {
	.x = { .min = 0, .max = 0, },
	.y = { .min = 0, .max = 0, },
};

struct xypoint screen_center;

static mousepos_handler_t* mousepos_handler;

static uint64_t double_click_threshold_us;

static CGEventFlags modflags;

#define MEDIAN(x, y) ((x) + (((y)-(x)) / 2))

struct gamma_table {
	CGGammaValue* red;
	CGGammaValue* green;
	CGGammaValue* blue;
	uint32_t numents;
};

struct displayinfo {
	CGDirectDisplayID id;
	struct rectangle bounds;
	struct gamma_table orig_gamma;
	struct gamma_table alt_gamma;
};

struct displayinfo* displays;
static uint32_t num_displays;

static void setup_gamma_table(struct gamma_table* gt, uint32_t size)
{
	gt->numents = size;
	gt->red = xmalloc(size * sizeof(*gt->red));
	gt->green = xmalloc(size * sizeof(*gt->green));
	gt->blue = xmalloc(size * sizeof(*gt->blue));
}

static void clear_gamma_table(struct gamma_table* gt)
{
	xfree(gt->red);
	xfree(gt->green);
	xfree(gt->blue);
	memset(gt, 0, sizeof(*gt));
}

static void init_display(struct displayinfo* d, CGDirectDisplayID id)
{
	uint32_t numents;
	CGError cgerr;
	CGRect bounds;

	d->id = id;
	setup_gamma_table(&d->orig_gamma, CGDisplayGammaTableCapacity(d->id));
	setup_gamma_table(&d->alt_gamma, d->orig_gamma.numents);

	cgerr = CGGetDisplayTransferByTable(d->id, d->orig_gamma.numents, d->orig_gamma.red,
	                                    d->orig_gamma.green, d->orig_gamma.blue, &numents);
	if (cgerr) {
		initerr("CGGetDisplayTransferByTable() failed (%d)\n", cgerr);
		initerr("brightness adjustment will be disabled\n");
		clear_gamma_table(&d->orig_gamma);
		clear_gamma_table(&d->alt_gamma);
	} else if (numents != d->orig_gamma.numents) {
		initerr("CGGetDisplayTransferByTable() behaves strangely: %u != %u\n",
		        numents, d->orig_gamma.numents);
		assert(numents < d->orig_gamma.numents);
		d->orig_gamma.numents = numents;
		d->alt_gamma.numents = numents;
	}

	bounds = CGDisplayBounds(d->id);
	d->bounds.x.min = CGRectGetMinX(bounds);
	d->bounds.x.max = CGRectGetMaxX(bounds);
	d->bounds.y.min = CGRectGetMinY(bounds);
	d->bounds.y.max = CGRectGetMaxY(bounds);

	if (d->bounds.x.min < screen_dimensions.x.min)
		screen_dimensions.x.min = d->bounds.x.min;
	if (d->bounds.x.max > screen_dimensions.x.max)
		screen_dimensions.x.max = d->bounds.x.max;
	if (d->bounds.y.min < screen_dimensions.y.min)
		screen_dimensions.y.min = d->bounds.y.min;
	if (d->bounds.y.max > screen_dimensions.y.max)
		screen_dimensions.y.max = d->bounds.y.max;
}

/*
 * HACK: More API stupidity from Apple means you can't hide the cursor if
 * you're not the foreground application...unless you know their secret
 * handshake to allow doing that, which requires calling these undeclared,
 * undocumented functions.
 *
 * References:
 *   http://lists.apple.com/archives/carbon-dev/2006/Jan/msg00555.html
 *   http://stackoverflow.com/questions/3885896/globally-hiding-cursor-from-background-app
 */
typedef int CGSConnectionID;
extern void CGSSetConnectionProperty(CGSConnectionID, CGSConnectionID, CFStringRef, CFBooleanRef);
extern CGSConnectionID _CGSDefaultConnection(void);

/* "128 displays oughta be enough for anyone..." */
#define MAX_DISPLAYS 128

int platform_init(struct kvmap* params, mousepos_handler_t* mouse_handler)
{
	CGDirectDisplayID displayids[MAX_DISPLAYS];
	CGError cgerr;
	OSStatus status;
	uint32_t i;
	kern_return_t kr;
	NXEventHandle nxevh;

	kr = mach_timebase_info(&mach_timebase);
	if (kr != KERN_SUCCESS) {
		initerr("mach_timebase_info() failed: %s\n", mach_error_string(kr));
		return -1;
	}

	nxevh = NXOpenEventStatus();
	double_click_threshold_us = NXClickTime(nxevh) * 1000000;
	NXCloseEventStatus(nxevh);

	cgerr = CGGetOnlineDisplayList(ARR_LEN(displayids), displayids,
	                               &num_displays);
	if (cgerr) {
		initerr("CGGetOnlineDisplayList() failed (%d)\n", cgerr);
		return -1;
	}

	displays = xmalloc(num_displays * sizeof(*displays));

	/* Initialize to "normal" gamma */
	CGDisplayRestoreColorSyncSettings();

	for (i = 0; i < num_displays; i++)
		init_display(&displays[i], displayids[i]);

	screen_center.x = MEDIAN(screen_dimensions.x.min, screen_dimensions.x.max);
	screen_center.y = MEDIAN(screen_dimensions.y.min, screen_dimensions.y.max);

	status = PasteboardCreate(kPasteboardClipboard, &clipboard);
	if (status != noErr) {
		initerr("PasteboardCreate() failed (%d)\n", status);
		return -1;
	}

	osx_keycodes_init();

	if (opmode == MASTER)
		CGSSetConnectionProperty(_CGSDefaultConnection(), _CGSDefaultConnection(),
		                         CFSTR("SetsCursorInBackground"), kCFBooleanTrue);

	mousepos_handler = mouse_handler;

	return 0;
}

void platform_exit(void)
{
	uint32_t i;

	osx_keycodes_exit();

	CFRelease(clipboard);
	CGDisplayRestoreColorSyncSettings();

	for (i = 0; i < num_displays; i++) {
		clear_gamma_table(&displays[i].orig_gamma);
		clear_gamma_table(&displays[i].alt_gamma);
	}
}

/*
 * There are, as far as I can see, two approaches to setting up global
 * hotkeys.  One approach uses a global event tap and sniffs keyboard events
 * searching for one that matches a bound hotkey (this is the one that's
 * currently implemented and in use).  The other involves calling
 * InstallApplicationEventHandler() and RegisterEventHotKey() -- to get the
 * callbacks set up with these, however, we'd apparently need to use
 * RunApplicationEventLoop() instead of CFRunLoops.  That API is deprecated,
 * however (the declaration of RunApplicationEventLoop() is #ifdef'd out on
 * 64-bit builds in the system headers, though the symbol is still present in
 * the libraries so a manual declaration seems to work), and furthermore I
 * don't know how (or if it's possible) to integrate the two different event
 * loops, so I've stuck with the CFRunLoop/CGEventTap approach.  The
 * pre-filtered direct callbacks provided by the RegisterEventHotKey()
 * approach seems much nicer than manually filtering them out of the stream of
 * all global keystrokes, so though I've left some vestiges of that code
 * around "just in case"...
 */
#define EVENTTAP_HOTKEYS

struct osxhotkey {
	CGKeyCode keycode;
	CGEventFlags modmask;
	hotkey_callback_t callback;
	void* arg;
#ifndef EVENTTAP_HOTKEYS
	EventHotKeyRef evref;
#endif
};

static struct osxhotkey* hotkeys;
static unsigned int num_hotkeys;

struct hotkey_context {
	uint32_t modmask;
};

static struct osxhotkey* find_hotkey(uint32_t keycode, uint32_t modmask)
{
	struct osxhotkey* hk;

	for (hk = hotkeys; hk < hotkeys + num_hotkeys; hk++) {
		if (hk->keycode == keycode && hk->modmask == modmask) {
			return hk;
		}
	}

	return NULL;
}

static struct osxhotkey* do_hotkey(uint32_t keycode, uint32_t modmask)
{
	struct hotkey_context hkctx = { .modmask = modmask, };
	struct osxhotkey* hk = find_hotkey(keycode, modmask);

	if (hk)
		hk->callback(&hkctx, hk->arg);

	return hk;
}

#ifndef EVENTTAP_HOTKEYS
static EventHandlerUPP hotkey_handler_upp;
static EventHandlerRef hotkey_handlerref;

static OSStatus hotkey_handler_fn(EventHandlerCallRef next, EventRef ev, void* arg)
{
	EventHotKeyID hkid;
	OSStatus status;
	struct osxhotkey* hk;
	struct hotkey_context ctx;

	status = GetEventParameter(ev, kEventParamDirectObject, typeEventHotKeyID,
	                           NULL, sizeof(hkid), NULL, &hkid);
	if (status) {
		errlog("GetEventParameter() failed in hotkey_handler_fn()\n");
		abort();
	}

	if (hkid.id >= num_hotkeys) {
		errlog("Out-of-bounds hotkey ID in hotkey_handler_fn()\n");
		abort();
	}

	hk = &hotkeys[hkid.id];
	ctx.modmask = hk->modmask;
	hk->callback(&ctx, hk->arg);

	return noErr;
}
#endif

int bind_hotkey(const char* keystr, hotkey_callback_t cb, void* arg)
{
	CGKeyCode kc;
	CGEventFlags modmask;
	struct osxhotkey* hk;
#ifndef EVENTTAP_HOTKEYS
	EventTypeSpec evtype;
	EventHotKeyID hkid = { .signature = 'enth', .id = num_hotkeys, };
#endif

	if (parse_keystring(keystr, &kc, &modmask))
		return -1;

	for (hk = hotkeys; hk < hotkeys + num_hotkeys; hk++) {
		if (hk->modmask == modmask && hk->keycode == kc) {
			initerr("hotkey '%s' conflicts with an earlier hotkey binding\n",
			        keystr);
			return -1;
		}
	}

#ifndef EVENTTAP_HOTKEYS
	if (!num_hotkeys) {
		hotkey_handler_upp = NewEventHandlerUPP(hotkey_handler_fn);
		evtype.eventClass = kEventClassKeyboard;
		evtype.eventKind = kEventHotKeyPressed;

		InstallApplicationEventHandler(hotkey_handler_upp, 1, &evtype, NULL,
		                               &hotkey_handlerref);
	}
#endif

	hotkeys = xrealloc(hotkeys, ++num_hotkeys * sizeof(*hotkeys));
	hk = &hotkeys[num_hotkeys-1];

	hk->keycode = kc;
	hk->modmask = modmask;
	hk->callback = cb;
	hk->arg = arg;

#ifndef EVENTTAP_HOTKEYS
	/*
	 * NOTE: if this is to be used, hk->modmask will need to be a mask of
	 * cmdKey and friends, not kCGEventFlagMask* as it is now.
	 */
	RegisterEventHotKey(hk->keycode, hk->modmask, hkid, GetApplicationEventTarget(),
	                    kEventHotKeyExclusive, &hk->evref);
#endif

	return 0;
}

keycode_t* get_current_modifiers(void)
{
	return modmask_to_etkeycodes(modflags);
}

keycode_t* get_hotkey_modifiers(hotkey_context_t ctx)
{
	return modmask_to_etkeycodes(ctx->modmask);
}

uint64_t get_microtime(void)
{
	uint64_t t = mach_absolute_time();
	return ((t * mach_timebase.numer) / mach_timebase.denom) / 1000;
}

void get_screen_dimensions(struct rectangle* d)
{
	*d = screen_dimensions;
}

static void set_gamma_table(CGDirectDisplayID disp, const struct gamma_table* gt)
{
	CGError err;

	if (!gt->numents)
		return;

	err = CGSetDisplayTransferByTable(disp, gt->numents, gt->red, gt->green, gt->blue);
	if (err)
		errlog("CGSetDisplayTransferByTable() failed (%d)\n", err);
}

/*
 * The identity macro (ahem), for use as 'defloat' in MAKE_GAMMA_SCALE_FN,
 * because CGGammaValue is a float to start with.
 */
#define id(x) x

static MAKE_GAMMA_SCALE_FN(gamma_scale, CGGammaValue, id);

static void scale_gamma_table(const struct gamma_table* from, struct gamma_table* to,
                              float scale)
{
	uint32_t i;

	assert(from->numents == to->numents);

	for (i = 0; i < to->numents; i++) {
		to->red[i] = gamma_scale(from->red, from->numents, i, scale);
		to->green[i] = gamma_scale(from->green, from->numents, i, scale);
		to->blue[i] = gamma_scale(from->blue, from->numents, i, scale);
	}
}

void set_display_brightness(float f)
{
	uint32_t i;

	for (i = 0; i < num_displays; i++) {
		scale_gamma_table(&displays[i].orig_gamma, &displays[i].alt_gamma, f);
		set_gamma_table(displays[i].id, &displays[i].alt_gamma);
	}
}

static inline uint32_t cgfloat_to_u32(CGFloat f)
{
	if (f > UINT32_MAX || f < 0) {
		errlog("out-of-range CGFloat: %g\n", f);
		abort();
	}

	return cground(f);
}

static CGPoint get_mousepos_cgpoint(void)
{
	CGPoint cgpt;
	CGEventRef ev = CGEventCreate(NULL);

	if (!ev) {
		errlog("CGEventCreate failed\n");
		abort();
	}

	cgpt = CGEventGetLocation(ev);
	CFRelease(ev);

	return cgpt;
}

#define NO_MOUSEBUTTON 0

static int get_pt_display(CGPoint pt, CGDirectDisplayID* d)
{
	uint32_t numdisplays;
	CGError err;

	err = CGGetDisplaysWithPoint(pt, !!d, d, &numdisplays);
	if (err) {
		errlog("CGGetDisplaysWithPoint() failed: %d\n", err);
		abort();
	}
	return !!numdisplays;
}

static void post_mouseevent(CGPoint cgpt, CGEventType type, CGMouseButton button)
{
	CGDirectDisplayID disp;
	CGPoint curpos;
	CGRect bounds;
	CGEventRef ev;

	if (!get_pt_display(cgpt, &disp)) {
		curpos = get_mousepos_cgpoint();
		if (!get_pt_display(curpos, &disp)) {
			vinfo("mouse position (%g,%g) off any display?\n", curpos.x, curpos.y);
			disp = CGMainDisplayID();
		}
	}

	/*
	 * Why the subtraction of 0.1 on the max-bound checks here?  Stupidly
	 * enough, without them OSX's pointer-at-edge-of-screen detection
	 * breaks (your auto-hiding Dock won't pop up, for example).
	 *
	 * Try as I might, I still have yet to see *any* sense whatsoever in
	 * tracking the mouse position in floating point.  Whither sanity,
	 * Apple?  WTF?
	 */

	bounds = CGDisplayBounds(disp);
	if (cgpt.x < CGRectGetMinX(bounds))
		cgpt.x = CGRectGetMinX(bounds);
	if (cgpt.x > CGRectGetMaxX(bounds))
		cgpt.x = CGRectGetMaxX(bounds) - 0.1;
	if (cgpt.y < CGRectGetMinY(bounds))
		cgpt.y = CGRectGetMinY(bounds);
	if (cgpt.y > CGRectGetMaxY(bounds))
		cgpt.y = CGRectGetMaxY(bounds) - 0.1;

	ev = CGEventCreateMouseEvent(NULL, type, cgpt, button);
	if (!ev) {
		errlog("CGEventCreateMouseEvent failed\n");
		abort();
	}
	CGEventSetFlags(ev, modflags|kCGEventFlagMaskNonCoalesced);
	CGEventPost(kCGHIDEventTap, ev);
	CFRelease(ev);
}

struct xypoint get_mousepos(void)
{
	struct xypoint pt;
	CGPoint cgpt = get_mousepos_cgpoint();

	pt.x = cgfloat_to_u32(cgpt.x);
	pt.y = cgfloat_to_u32(cgpt.y);

	return pt;
}

static inline int mouse_button_held(CGMouseButton btn)
{
	return CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, btn);
}

static uint64_t last_mouse_move;

static void set_mousepos_cgpoint(CGPoint cgpt)
{
	post_mouseevent(cgpt, kCGEventMouseMoved, NO_MOUSEBUTTON);
	last_mouse_move = get_microtime();
}

void set_mousepos(struct xypoint pt)
{
	set_mousepos_cgpoint(CGPointMake((CGFloat)pt.x, (CGFloat)pt.y));
}

/* Variant of set_mousepos() that doesn't trigger additional events */
static void set_mousepos_silent(struct xypoint pt)
{
	CGPoint cgpt = { .x = (CGFloat)pt.x, .y = (CGFloat)pt.y, };
	CGWarpMouseCursorPosition(cgpt);
}

void move_mousepos(int32_t dx, int32_t dy)
{
	CGPoint pt = get_mousepos_cgpoint();

	pt.x += dx;
	pt.y += dy;

	/* Sigh...why can't Quartz figure this out by itself? */
	if (mouse_button_held(kCGMouseButtonLeft))
		post_mouseevent(pt, kCGEventLeftMouseDragged, kCGMouseButtonLeft);
	else if (mouse_button_held(kCGMouseButtonRight))
		post_mouseevent(pt, kCGEventRightMouseDragged, kCGMouseButtonRight);
	else if (mouse_button_held(kCGMouseButtonCenter))
		post_mouseevent(pt, kCGEventOtherMouseDragged, kCGMouseButtonCenter);
	else
		set_mousepos_cgpoint(pt);
}

struct click_history {
	uint64_t last_press;
	uint64_t last_release;
	int count;
};

static struct click_history click_histories[MB__MAX_+1];

/*
 * 1: single-click, 2: double-click, 3: triple-click.
 *
 * See kCGMouseEventClickState:
 *   https://developer.apple.com/library/mac/documentation/Carbon/Reference/QuartzEventServicesRef/Reference/reference.html#jumpTo_71
 */
static int64_t click_type(mousebutton_t btn, pressrel_t pr)
{
	int64_t type;
	uint64_t now_us = get_microtime();
	struct click_history* hist = &click_histories[btn];
	uint64_t* prev = (pr == PR_PRESS) ? &hist->last_press : &hist->last_release;

	/*
	 * This may look sort of weird, but it's my best approximation of what
	 * Apple seems (empirically) to be doing with real-native-hardware
	 * clicks (at least for now).
	 */

	if ((now_us - *prev) > double_click_threshold_us || last_mouse_move > *prev) {
		hist->count = 1;
		type = (pr == PR_PRESS) ? 1 : 0;
	} else if (pr == PR_PRESS) {
		hist->count++;
		type = hist->count > 3 ? 2 : hist->count;
	} else {
		type = hist->count;
	}

	*prev = now_us;

	return type;
}

void do_clickevent(mousebutton_t button, pressrel_t pr)
{
	int32_t scrollamt;
	CGEventRef ev;
	/* superfluous initializations to silence warnings from dumb old compilers */
	CGEventType cgtype = kCGEventNull;
	CGMouseButton cgbtn = kCGMouseButtonLeft;

	switch (button) {
	case MB_LEFT:
		cgtype = (pr == PR_PRESS) ? kCGEventLeftMouseDown : kCGEventLeftMouseUp;
		cgbtn = kCGMouseButtonLeft;
		break;

	case MB_CENTER:
		/*
		 * kCGEventCenterMouse{Up,Down} don't exist...
		 *
		 * Having the button encoded in both the event type and also
		 * separately in another argument seems like pretty crappy API
		 * design to me, especially when the values available for the
		 * two don't match up.  Sigh.
		 */
		cgtype = (pr == PR_PRESS) ? kCGEventOtherMouseDown : kCGEventOtherMouseUp;
		cgbtn = kCGMouseButtonCenter;
		break;

	case MB_RIGHT:
		cgtype = (pr == PR_PRESS) ? kCGEventRightMouseDown : kCGEventRightMouseUp;
		cgbtn = kCGMouseButtonRight;
		break;

	case MB_SCROLLUP:
	case MB_SCROLLDOWN:
		if (pr == PR_RELEASE)
			return;
		scrollamt = (button == MB_SCROLLDOWN) ? -1 : 1;
		break;

	default:
		warn("unhandled click event button %u\n", button);
		return;
	}

	if (button == MB_SCROLLUP || button == MB_SCROLLDOWN)
		ev = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine, 1, scrollamt);
	else {
		ev = CGEventCreateMouseEvent(NULL, cgtype, get_mousepos_cgpoint(), cgbtn);
		if (ev)
			CGEventSetIntegerValueField(ev, kCGMouseEventClickState,
			                            click_type(button, pr));
	}

	if (!ev) {
		errlog("CGEventCreateMouseEvent failed\n");
		abort();
	}
	CGEventSetFlags(ev, modflags|kCGEventFlagMaskNonCoalesced);
	CGEventPost(kCGHIDEventTap, ev);
	CFRelease(ev);
}

static CGEventFlags key_eventflag(CGKeyCode cgk)
{
	switch (cgk) {
	case kVK_Control:
	case kVK_RightControl:
		return kCGEventFlagMaskControl;

	case kVK_Shift:
	case kVK_RightShift:
		return kCGEventFlagMaskShift;

	case kVK_Option:
	case kVK_RightOption:
		return kCGEventFlagMaskAlternate;

	case kVK_Command:
		return kCGEventFlagMaskCommand;

	default:
		return 0;
	}
}

void do_keyevent(keycode_t key, pressrel_t pr)
{
	CGEventRef ev;
	CGKeyCode cgkc;
	CGEventFlags flags;

	cgkc = etkeycode_to_cgkeycode(key);
	if (cgkc == kVK_NULL) {
		warn("keycode %u not mapped\n", key);
		return;
	}

	if (is_modifier_key(key)) {
		flags = key_eventflag(cgkc);
		if (pr == PR_PRESS)
			modflags |= flags;
		else
			modflags &= ~flags;

		ev = CGEventCreate(NULL);
		if (!ev) {
			errlog("CGEventCreate() failed\n");
			abort();
		}
		CGEventSetType(ev, kCGEventFlagsChanged);
		CGEventSetFlags(ev, modflags);
		CFRelease(ev);
	}

	ev = CGEventCreateKeyboardEvent(NULL, cgkc, pr == PR_PRESS);
	if (!ev) {
		errlog("CGEventCreateKeyboardEvent() failed\n");
		abort();
	}

	flags = modflags;
	if (is_keypad_key(key))
		flags |= kCGEventFlagMaskNumericPad;
	CGEventSetFlags(ev, flags);

	CGEventPost(kCGHIDEventTap, ev);
	CFRelease(ev);
}

char* get_clipboard_text(void)
{
	OSStatus status;
	PasteboardItemID itemid;
	CFDataRef data;
	char* txt;
	size_t len;

	/*
	 * To avoid error -25130 (badPasteboardSyncErr):
	 *
	 * "The pasteboard has been modified and must be synchronized before
	 *  use."
	 */
	PasteboardSynchronize(clipboard);

	status = PasteboardGetItemIdentifier(clipboard, 1, &itemid);
	if (status != noErr) {
		errlog("PasteboardGetItemIdentifier(1) failed (%d)\n", status);
		return xstrdup("");
	}

	status = PasteboardCopyItemFlavorData(clipboard, itemid, PLAINTEXT, &data);
	if (status != noErr) {
		errlog("PasteboardCopyItemFlavorData(PLAINTEXT) failed (%d)\n", status);
		return xstrdup("");
	}

	len = CFDataGetLength(data);
	txt = xmalloc(len+1);
	memcpy(txt, CFDataGetBytePtr(data), len);
	txt[len] = '\0';

	CFRelease(data);

	return txt;
}

int set_clipboard_text(const char* text)
{
	OSStatus status;
	CFDataRef data;
	int ret = 0;

	data = CFDataCreate(NULL, (UInt8*)text, strlen(text));
	if (!data) {
		errlog("CFDataCreate() failed\n");
		return -1;
	}

	/*
	 * To avoid error -25135 (notPasteboardOwnerErr):
	 *
	 * "The application did not clear the pasteboard before attempting to
	 *  add flavor data."
	 */
	PasteboardClear(clipboard);

	status = PasteboardPutItemFlavor(clipboard, (PasteboardItemID)data,
	                                 PLAINTEXT, data, 0);
	if (status != noErr) {
		errlog("PasteboardPutItemFlavor() failed (%d)\n", status);
		ret = -1;
	}

	CFRelease(data);

	return ret;
}

static struct xypoint saved_mousepos;

int grab_inputs(void)
{
	saved_mousepos = get_mousepos();

	if (CGDisplayHideCursor(kCGDirectMainDisplay) != kCGErrorSuccess)
		return 1;

	if (CGAssociateMouseAndMouseCursorPosition(false) != kCGErrorSuccess) {
		CGDisplayShowCursor(kCGDirectMainDisplay);
		return 1;
	}

	return 0;
}

void ungrab_inputs(int restore_mousepos)
{
	CGAssociateMouseAndMouseCursorPosition(true);
	if (restore_mousepos)
		set_mousepos_silent(saved_mousepos);
	CGDisplayShowCursor(kCGDirectMainDisplay);
}

struct fdmon_ctx {
	int fd;
	fdmon_callback_t readcb, writecb;
	void* arg;
	uint32_t flags;
	int refcount;

	CFFileDescriptorRef fdref;
	CFRunLoopSourceRef rlsrc;
};

static void fdmon_ref(struct fdmon_ctx* ctx)
{
	assert(ctx->refcount > 0);
	ctx->refcount += 1;
}

static void fdmon_unref(struct fdmon_ctx* ctx)
{
	assert(ctx->refcount > 0);
	ctx->refcount -= 1;
	if (ctx->refcount)
		return;

	CFFileDescriptorDisableCallBacks(ctx->fdref, kCFFileDescriptorReadCallBack
	                                            |kCFFileDescriptorWriteCallBack);
	CFRunLoopRemoveSource(CFRunLoopGetMain(), ctx->rlsrc, kCFRunLoopCommonModes);

	CFRunLoopSourceInvalidate(ctx->rlsrc);
	CFFileDescriptorInvalidate(ctx->fdref);

	CFRelease(ctx->fdref);
	CFRelease(ctx->rlsrc);

	xfree(ctx);
}

static void fdmon_set_enabled_callbacks(struct fdmon_ctx* ctx)
{
	CFOptionFlags cf_en = 0, cf_dis = 0;

	if (ctx->flags & FM_READ)
		cf_en |= kCFFileDescriptorReadCallBack;
	else
		cf_dis |= kCFFileDescriptorReadCallBack;

	if (ctx->flags & FM_WRITE)
		cf_en |= kCFFileDescriptorWriteCallBack;
	else
		cf_dis |= kCFFileDescriptorWriteCallBack;

	if (cf_en)
		CFFileDescriptorEnableCallBacks(ctx->fdref, cf_en);
	if (cf_dis)
		CFFileDescriptorDisableCallBacks(ctx->fdref, cf_dis);
}

static void fdmon_callback(CFFileDescriptorRef fdref, CFOptionFlags types, void* arg)
{
	struct fdmon_ctx* ctx = arg;

	/* Callbacks could free ctx, so grab a reference here */
	fdmon_ref(ctx);

	if (types & kCFFileDescriptorReadCallBack)
		ctx->readcb(ctx, ctx->arg);

	if (types & kCFFileDescriptorWriteCallBack)
		ctx->writecb(ctx, ctx->arg);

	/* Callbacks are one-shot only; re-enable the next one(s) here */
	fdmon_set_enabled_callbacks(ctx);

	fdmon_unref(ctx);
}

struct fdmon_ctx* fdmon_register_fd(int fd, fdmon_callback_t readcb,
                                    fdmon_callback_t writecb, void* arg)
{
	CFFileDescriptorContext fdctx = {
		.version = 0,
		.retain = NULL,
		.release = NULL,
		.copyDescription = NULL,
	};
	struct fdmon_ctx* ctx = xmalloc(sizeof(*ctx));

	fdctx.info = ctx;

	ctx->fd = fd;
	ctx->readcb = readcb;
	ctx->writecb = writecb;
	ctx->arg = arg;
	ctx->flags = 0;
	ctx->refcount = 1;
	ctx->fdref = CFFileDescriptorCreate(kCFAllocatorDefault, ctx->fd, false,
	                                    fdmon_callback, &fdctx);

	if (!ctx->fdref) {
		errlog("CFFileDescriptorCreate() failed\n");
		abort();
	}

	ctx->rlsrc = CFFileDescriptorCreateRunLoopSource(kCFAllocatorDefault,
	                                                 ctx->fdref, 0);
	if (!ctx->rlsrc) {
		errlog("CFFileDescriptorCreateRunLoopSource() failed\n");
		abort();
	}

	CFRunLoopAddSource(CFRunLoopGetMain(), ctx->rlsrc, kCFRunLoopCommonModes);

	return ctx;
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

	fdmon_set_enabled_callbacks(ctx);
}

void fdmon_unmonitor(struct fdmon_ctx* ctx, uint32_t flags)
{
	if (flags & ~(FM_READ|FM_WRITE)) {
		errlog("invalid fdmon flags: %u\n", flags);
		abort();
	}

	ctx->flags &= ~flags;

	fdmon_set_enabled_callbacks(ctx);
}

struct timerinfo {
	CFRunLoopTimerRef timer;
	void (*cbfn)(void* arg);
	void* cbarg;
};

static void timer_callback(CFRunLoopTimerRef timer, void* info)
{
	struct timerinfo* ti = info;

	ti->cbfn(ti->cbarg);

	CFRunLoopRemoveTimer(CFRunLoopGetMain(), ti->timer, kCFRunLoopCommonModes);
	CFRelease(ti->timer);

	xfree(ti);
}

timer_ctx_t schedule_call(void (*fn)(void* arg), void* arg, uint64_t delay)
{
	struct timerinfo* ti;
	CFAbsoluteTime firetime;
	CFRunLoopTimerContext timer_ctx = {
		.version = 0,
		.retain = NULL,
		.release = NULL,
		.copyDescription = NULL,
	};

	firetime = CFAbsoluteTimeGetCurrent() + ((CFAbsoluteTime)delay / 1000000.0);

	ti = xmalloc(sizeof(*ti));

	timer_ctx.info = ti;
	ti->cbfn = fn;
	ti->cbarg = arg;
	ti->timer = CFRunLoopTimerCreate(kCFAllocatorDefault, firetime, 0, 0, 0,
	                                 timer_callback, &timer_ctx);

	CFRunLoopAddTimer(CFRunLoopGetMain(), ti->timer, kCFRunLoopCommonModes);

	return ti;
}

int cancel_call(timer_ctx_t timer)
{
	struct timerinfo* ti = timer;
	CFStringRef mode = kCFRunLoopCommonModes;
	CFRunLoopRef loop = CFRunLoopGetMain();

	if (!CFRunLoopContainsTimer(loop, ti->timer, mode))
		return 0;

	CFRunLoopRemoveTimer(loop, ti->timer, mode);
	CFRelease(ti->timer);

	xfree(ti);

	return 1;
}

static void check_assistive_device_access(void)
{
#ifdef MAC_OS_X_VERSION_10_9
	const void* axdictkeys[] = { (void*)kAXTrustedCheckOptionPrompt, };
	const void* axdictvals[] = { (void*)kCFBooleanTrue, };
	CFDictionaryRef axdict = CFDictionaryCreate(NULL, axdictkeys, axdictvals, 1, NULL, NULL);

	if (!axdict)
		initdie("failed to create CFDictionaryRef\n");

	if (!AXIsProcessTrustedWithOptions(axdict)) {
		initerr("Not trusted for assistive device access.\n");
		/*
		 * Annoyingly, the dialog AXIsProcessTrustedWithOptions() pops
		 * up happens asynchronously, and the API doesn't offer any
		 * way to make sure it appears before the original process
		 * (this code here) exits, so here we just sleep while it
		 * (hopefully) pops up its little window, though of course the
		 * sleep time is an arbitrary made-up number (because what
		 * else can you do when there's an unavoidable race condition
		 * built in to the system library?).
		 */
		initerr("And here we sleep asynchronously, hoping Apple's stupid API has "
		        "popped up the message window before we exit...\n");
		sleep(5);
		initerr("(Giving up and exiting.)\n");
		exit(1);
	}
#else
	if (!AXAPIEnabled()) {
		initerr("Assistive device access is disabled; "
		        "can't access keyboard events.\n");
		initerr("(You can enable this in the Accessibility pane of "
		        "System Preferences.\n)");
		exit(1);
	}
#endif
}

#define MODFLAG_MASK (kCGEventFlagMaskShift|kCGEventFlagMaskControl\
                      |kCGEventFlagMaskAlternate|kCGEventFlagMaskCommand)

static void handle_keyevent(CGEventRef ev, pressrel_t pr)
{
	CGKeyCode keycode = CGEventGetIntegerValueField(ev, kCGKeyboardEventKeycode);
	keycode_t etkc = cgkeycode_to_etkeycode(keycode);

	assert(is_remote(focused_node));

	send_keyevent(focused_node->remote, etkc, pr);
}

static void handle_flagschanged(CGEventFlags oldflags, CGEventFlags newflags)
{
	int i;
	pressrel_t pr;
	CGEventFlags changed = oldflags ^ newflags;

	assert(is_remote(focused_node));

	for (i = 0; i < osx_modifiers.num; i++) {
		if (osx_modifiers.keys[i].mask & changed) {
			pr = (osx_modifiers.keys[i].mask & oldflags) ? PR_RELEASE : PR_PRESS;
			send_keyevent(focused_node->remote, osx_modifiers.keys[i].etkey, pr);
		}
	}
}

static void handle_grabbed_mousemove(CGEventRef ev)
{
	int32_t dx, dy;

	dx = CGEventGetIntegerValueField(ev, kCGMouseEventDeltaX);
	dy = CGEventGetIntegerValueField(ev, kCGMouseEventDeltaY);

	assert(is_remote(focused_node));

	send_moverel(focused_node->remote, dx, dy);
}

static void handle_local_mousemove(CGEventRef ev)
{
	CGPoint loc = CGEventGetLocation(ev);

	/* FIXME: don't trigger if mouse buttons held */
	if (mousepos_handler)
		mousepos_handler((struct xypoint){ .x = cground(loc.x), .y = cground(loc.y), });
}

/*
 * This is kind of simple-minded in comparison to the level of detail
 * available from the scroll-wheel CGEvent, but in practice all that extra
 * information doesn't really translate to other systems very well, so here we
 * are (this approach seems to work pretty acceptably).
 */
static void handle_scrollevent(CGEventRef ev)
{
	mousebutton_t mb;
	double scroll_units;

	scroll_units = CGEventGetDoubleValueField(ev, kCGScrollWheelEventFixedPtDeltaAxis1);

	if (fabs(scroll_units) < 0.0001)
		return;

	mb = scroll_units < 0.0 ? MB_SCROLLDOWN : MB_SCROLLUP;

	send_clickevent(focused_node->remote, mb, PR_PRESS);
	send_clickevent(focused_node->remote, mb, PR_RELEASE);
}

static CFMachPortRef evtapport;

static CGEventRef evtap_callback(CGEventTapProxy tapprox, CGEventType evtype, CGEventRef ev,
                                 void* arg)
{
	static uint64_t known_unknowns = 0; /* waxing Rumsfeldian... */
	uint32_t modmask, keycode;
	CGEventFlags old_modflags = modflags, evflags = CGEventGetFlags(ev);

	modflags = evflags;

	if (evtype == kCGEventKeyDown || evtype == kCGEventKeyUp) {
		keycode = CGEventGetIntegerValueField(ev, kCGKeyboardEventKeycode);
		modmask = evflags & MODFLAG_MASK;
		if ((evtype == kCGEventKeyDown ? do_hotkey : find_hotkey)(keycode, modmask))
			return NULL;
	}

	if (evtype == kCGEventMouseMoved || evtype == kCGEventLeftMouseDragged
	    || evtype == kCGEventRightMouseDragged || evtype == kCGEventOtherMouseDragged) {
		if (is_remote(focused_node)) {
			handle_grabbed_mousemove(ev);
			return NULL;
		} else {
			handle_local_mousemove(ev);
			return ev;
		}
	}

	if (is_master(focused_node))
		return ev;

	switch (evtype) {
	case kCGEventKeyDown:
		handle_keyevent(ev, PR_PRESS);
		break;

	case kCGEventKeyUp:
		handle_keyevent(ev, PR_RELEASE);
		break;

	case kCGEventLeftMouseDown:
		send_clickevent(focused_node->remote, MB_LEFT, PR_PRESS);
		break;

	case kCGEventLeftMouseUp:
		send_clickevent(focused_node->remote, MB_LEFT, PR_RELEASE);
		break;

	case kCGEventRightMouseDown:
		send_clickevent(focused_node->remote, MB_RIGHT, PR_PRESS);
		break;

	case kCGEventRightMouseUp:
		send_clickevent(focused_node->remote, MB_RIGHT, PR_RELEASE);
		break;

	case kCGEventOtherMouseDown:
		send_clickevent(focused_node->remote, MB_CENTER, PR_PRESS);
		break;

	case kCGEventOtherMouseUp:
		send_clickevent(focused_node->remote, MB_CENTER, PR_RELEASE);
		break;

	case kCGEventScrollWheel:
		handle_scrollevent(ev);
		break;

	case kCGEventFlagsChanged:
		handle_flagschanged(old_modflags, modflags);
		break;

	case kCGEventTapDisabledByUserInput:
		warn("Unexpected event: kCGEventTapDisabledByUserInput? (Re-enabling...)");
		/* Fallthrough */
	case kCGEventTapDisabledByTimeout:
		CGEventTapEnable(evtapport, true);
		break;

	default:
		if ((evtype < (sizeof(known_unknowns) * CHAR_BIT) - 1)
		    && !(known_unknowns & (1ULL << evtype))) {
			warn("CGEvent type %u unknown\n", evtype);
			known_unknowns |= 1ULL << evtype;
		}
		break;
	}

	return NULL;
}

static void setup_event_tap(void)
{
	CFRunLoopSourceRef tapsrc;

	check_assistive_device_access();

	evtapport = CGEventTapCreate(kCGHIDEventTap, kCGHeadInsertEventTap,
	                             kCGEventTapOptionDefault, kCGEventMaskForAllEvents,
	                             evtap_callback, NULL);
	if (!evtapport)
		initdie("Can't create event tap!\n");

	tapsrc = CFMachPortCreateRunLoopSource(NULL, evtapport, 0);
	if (!tapsrc)
		initdie("CFMachPortCreateRunLoopSource() failed\n");

	CFRunLoopAddSource(CFRunLoopGetMain(), tapsrc, kCFRunLoopCommonModes);

	/* CFRunLoopAddSource() retains tapsrc, so we release it here */
	CFRelease(tapsrc);
}

void run_event_loop(void)
{
	if (opmode == MASTER)
		setup_event_tap();

	CFRunLoopRun();
}
