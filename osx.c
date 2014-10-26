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

static CGDirectDisplayID display;

static PasteboardRef clipboard;

static struct rectangle screenbounds;

struct xypoint screen_center;

static dirmask_t mouse_edgemask;

static mouse_edge_change_handler_t* mouse_edge_handler;

static uint64_t double_click_threshold_us;

static CGEventFlags modflags;

#define MEDIAN(x, y) ((x) + (((y)-(x)) / 2))

struct gamma_table {
	CGGammaValue* red;
	CGGammaValue* green;
	CGGammaValue* blue;
	uint32_t numents;
};

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

static struct gamma_table orig_gamma, alt_gamma;

int platform_init(int* fd, mouse_edge_change_handler_t* edge_handler)
{
	CGError cgerr;
	OSStatus status;
	CGDirectDisplayID active_displays[16];
	CGRect bounds;
	uint32_t num_active_displays;
	kern_return_t kr;
	NXEventHandle nxevh;
	uint32_t numgaments;

	kr = mach_timebase_info(&mach_timebase);
	if (kr != KERN_SUCCESS) {
		elog("mach_timebase_info() failed: %s\n", mach_error_string(kr));
		return -1;
	}

	nxevh = NXOpenEventStatus();
	double_click_threshold_us = NXClickTime(nxevh) * 1000000;
	NXCloseEventStatus(nxevh);

	cgerr = CGGetActiveDisplayList(ARR_LEN(active_displays), active_displays,
	                               &num_active_displays);
	if (cgerr) {
		elog("CGGetActiveDisplayList() failed (%d)\n", cgerr);
		return -1;
	}

	if (num_active_displays != 1) {
		elog("Support for num_displays != 1 NYI (have %u)\n", num_active_displays);
		return -1;
	}

	display = active_displays[0];

	bounds = CGDisplayBounds(display);
	screenbounds.x.min = CGRectGetMinX(bounds);
	screenbounds.x.max = CGRectGetMaxX(bounds) - 1;
	screenbounds.y.min = CGRectGetMinY(bounds);
	screenbounds.y.max = CGRectGetMaxY(bounds) - 1;

	screen_center.x = MEDIAN(screenbounds.x.min, screenbounds.x.max);
	screen_center.y = MEDIAN(screenbounds.y.min, screenbounds.y.max);

	status = PasteboardCreate(kPasteboardClipboard, &clipboard);
	if (status != noErr) {
		elog("PasteboardCreate() failed (%d)\n", status);
		return -1;
	}

	/* Initialize to "normal" gamma */
	CGDisplayRestoreColorSyncSettings();

	setup_gamma_table(&orig_gamma, CGDisplayGammaTableCapacity(display));
	setup_gamma_table(&alt_gamma, orig_gamma.numents);

	cgerr = CGGetDisplayTransferByTable(display, orig_gamma.numents, orig_gamma.red,
	                                    orig_gamma.green, orig_gamma.blue, &numgaments);
	if (cgerr) {
		elog("CGGetDisplayTransferByTable() failed (%d)\n", cgerr);
		elog("brightness adjustment will disabled\n", cgerr);
		clear_gamma_table(&orig_gamma);
		clear_gamma_table(&alt_gamma);
	} else if (numgaments != orig_gamma.numents) {
		elog("CGGetDisplayTransferByTable() behaves strangely: %u != %u\n",
		     numgaments, orig_gamma.numents);
		assert(numgaments < orig_gamma.numents);
		orig_gamma.numents = numgaments;
		alt_gamma.numents = numgaments;
	}

	mouse_edge_handler = edge_handler;

	*fd = -1;
	return 0;
}

void platform_exit(void)
{
	CFRelease(clipboard);
	CGDisplayRestoreColorSyncSettings();
	clear_gamma_table(&orig_gamma);
	clear_gamma_table(&alt_gamma);
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

static void set_gamma_table(CGDirectDisplayID disp, const struct gamma_table* gt)
{
	CGError err;

	if (!gt->numents)
		return;

	err = CGSetDisplayTransferByTable(disp, gt->numents, gt->red, gt->green, gt->blue);
	if (err)
		elog("CGSetDisplayTransferByTable() failed (%d)\n", err);
}

static inline CGGammaValue scale_gamma(CGGammaValue g, float scale)
{
	CGGammaValue res = g * scale;
	if (res > 1.0)
		return 1.0;
	else if (res < 0.0)
		return 0.0;
	else
		return res;
}

static void scale_gamma_table(const struct gamma_table* from, struct gamma_table* to,
                              float scale)
{
	uint32_t i;

	assert(from->numents == to->numents);

	for (i = 0; i < to->numents; i++) {
		to->red[i] = scale_gamma(from->red[i], scale);
		to->green[i] = scale_gamma(from->green[i], scale);
		to->blue[i] = scale_gamma(from->blue[i], scale);
	}
}

void set_display_brightness(float f)
{
	scale_gamma_table(&orig_gamma, &alt_gamma, f);
	set_gamma_table(display, &alt_gamma);
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

static void post_mouseevent(CGPoint cgpt, CGEventType type, CGMouseButton button)
{
	CGEventRef ev;

	cgpt.x = (cgpt.x > screenbounds.x.max) ? screenbounds.x.max : cgpt.x;
	cgpt.y = (cgpt.y > screenbounds.y.max) ? screenbounds.y.max : cgpt.y;

	cgpt.x = (cgpt.x < screenbounds.x.min) ? screenbounds.x.min : cgpt.x;
	cgpt.y = (cgpt.y < screenbounds.y.min) ? screenbounds.y.min : cgpt.y;

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

static inline int any_mouse_buttons_held(void)
{
	return mouse_button_held(kCGMouseButtonLeft)
		|| mouse_button_held(kCGMouseButtonRight)
		|| mouse_button_held(kCGMouseButtonCenter);
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

void set_mousepos_screenrel(float xpos, float ypos)
{
	struct xypoint pt = {
		.x = lrint(xpos * (float)screenbounds.x.max),
		.y = lrint(ypos * (float)screenbounds.y.max),
	};

	assert(xpos >= 0.0 && xpos <= 1.0);
	assert(ypos >= 0.0 && ypos <= 1.0);

	set_mousepos(pt);
}

/* FIXME: deduplicating this and x11.c's version would be nice. */
static dirmask_t get_mouse_edgemask(struct xypoint pt)
{
	dirmask_t mask = 0;

	if (pt.x == screenbounds.x.min)
		mask |= LEFTMASK;
	if (pt.x == screenbounds.x.max)
		mask |= RIGHTMASK;
	if (pt.y == screenbounds.y.min)
		mask |= UPMASK;
	if (pt.y == screenbounds.y.max)
		mask |= DOWNMASK;

	return mask;
}

/* This is also basically identical to x11.c's version */
static void check_mouse_edge(struct xypoint pt)
{
	float xpos, ypos;
	dirmask_t curmask = get_mouse_edgemask(pt);

	if (curmask != mouse_edgemask && mouse_edge_handler) {
		xpos = (float)pt.x / (float)screenbounds.x.max;
		ypos = (float)pt.y / (float)screenbounds.y.max;
		mouse_edge_handler(mouse_edgemask, curmask, xpos, ypos);
	}

	mouse_edgemask = curmask;
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

	if (opmode == REMOTE && !any_mouse_buttons_held())
		check_mouse_edge(get_mousepos());
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
