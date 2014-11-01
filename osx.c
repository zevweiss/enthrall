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
#include "proto.h"
#include "platform.h"
#include "osx-keycodes.h"

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
	gt->red = xmalloc(size * sizeof(gt->red));
	gt->green = xmalloc(size * sizeof(gt->green));
	gt->blue = xmalloc(size * sizeof(gt->blue));
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
		elog("CGGetDisplayTransferByTable() failed (%d)\n", cgerr);
		elog("brightness adjustment will disabled\n");
		clear_gamma_table(&d->orig_gamma);
		clear_gamma_table(&d->alt_gamma);
	} else if (numents != d->orig_gamma.numents) {
		elog("CGGetDisplayTransferByTable() behaves strangely: %u != %u\n",
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

/* "128 displays oughta be enough for anyone..." */
#define MAX_DISPLAYS 128

int platform_init(int* fd, mousepos_handler_t* mouse_handler)
{
	CGDirectDisplayID displayids[MAX_DISPLAYS];
	CGError cgerr;
	OSStatus status;
	uint32_t i;
	kern_return_t kr;
	NXEventHandle nxevh;

	kr = mach_timebase_info(&mach_timebase);
	if (kr != KERN_SUCCESS) {
		elog("mach_timebase_info() failed: %s\n", mach_error_string(kr));
		return -1;
	}

	nxevh = NXOpenEventStatus();
	double_click_threshold_us = NXClickTime(nxevh) * 1000000;
	NXCloseEventStatus(nxevh);

	cgerr = CGGetOnlineDisplayList(ARR_LEN(displayids), displayids,
	                               &num_displays);
	if (cgerr) {
		elog("CGGetOnlineDisplayList() failed (%d)\n", cgerr);
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
		elog("PasteboardCreate() failed (%d)\n", status);
		return -1;
	}

	mousepos_handler = mouse_handler;

	*fd = -1;

	return 0;
}

void platform_exit(void)
{
	uint32_t i;

	CFRelease(clipboard);
	CGDisplayRestoreColorSyncSettings();

	for (i = 0; i < num_displays; i++) {
		clear_gamma_table(&displays[i].orig_gamma);
		clear_gamma_table(&displays[i].alt_gamma);
	}
}

int bind_hotkey(const char* keystr, hotkey_callback_t cb, void* arg)
{
	elog("OSX bind_hotkey() not yet implemented\n");
	return 1;
}

keycode_t* get_current_modifiers(void)
{
	elog("OSX get_current_modifiers() NYI\n");
	return NULL;
}

keycode_t* get_hotkey_modifiers(hotkey_context_t ctx)
{
	elog("OSX get_hotkey_modifiers() not yet implemented\n");
	return NULL;
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
		elog("CGSetDisplayTransferByTable() failed (%d)\n", err);
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
		elog("out-of-range CGFloat: %g\n", f);
		abort();
	}

	return cground(f);
}

static CGPoint get_mousepos_cgpoint(void)
{
	CGPoint cgpt;
	CGEventRef ev = CGEventCreate(NULL);

	if (!ev) {
		elog("CGEventCreate failed\n");
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
		elog("CGGetDisplaysWithPoint() failed: %d\n", err);
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
			elog("mouse position (%g,%g) off any display?\n", curpos.x, curpos.y);
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
		elog("CGEventCreateMouseEvent failed\n");
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

static struct click_history click_histories[NUM_MOUSEBUTTONS];

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
	CGEventType cgtype;
	CGMouseButton cgbtn;
	int32_t scrollamt;
	CGEventRef ev;

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
		elog("unhandled click event button %u\n", button);
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
		elog("CGEventCreateMouseEvent failed\n");
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

	cgkc = keycode_to_cgkeycode(key);
	if (cgkc == kVK_NULL) {
		elog("keycode %u not mapped\n", key);
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
			elog("CGEventCreate() failed\n");
			abort();
		}
		CGEventSetType(ev, kCGEventFlagsChanged);
		CGEventSetFlags(ev, modflags);
		CFRelease(ev);
	}

	ev = CGEventCreateKeyboardEvent(NULL, cgkc, pr == PR_PRESS);
	if (!ev) {
		elog("CGEventCreateKeyboardEvent() failed\n");
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
		elog("PasteboardGetItemIdentifier(1) failed (%d)\n", status);
		return NULL;
	}

	status = PasteboardCopyItemFlavorData(clipboard, itemid, PLAINTEXT, &data);
	if (status != noErr) {
		elog("PasteboardCopyItemFlavorData(PLAINTEXT) failed (%d)\n", status);
		return NULL;
	}

	len = CFDataGetLength(data);
	txt = malloc(len+1);
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
		elog("CFDataCreate() failed\n");
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
		elog("PasteboardPutItemFlavor() failed (%d)\n", status);
		ret = -1;
	}

	CFRelease(data);

	return ret;
}

int grab_inputs(void)
{
	elog("grab_inputs() NYI on OSX\n");
	return -1;
}

void ungrab_inputs(void)
{
	elog("ungrab_inputs() NYI on OSX\n");
}

void process_events(void)
{
	elog("process_events() NYI on OSX\n");
}
