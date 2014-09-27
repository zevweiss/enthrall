#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <CoreGraphics/CGEvent.h>
#include <CoreGraphics/CGDirectDisplay.h>

/*
 * Because unfortunately I can't currently figure out how to get just
 * Pasteboard.h (which is in HIServices within ApplicationServices).  Sigh.
 */
#include <Carbon/Carbon.h>

#include "types.h"
#include "misc.h"
#include "proto.h"
#include "platform.h"

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

static PasteboardRef clipboard;

static struct rectangle screenbounds;

int platform_init(void)
{
	CGError cgerr;
	OSStatus status;
	CGDirectDisplayID active_displays[16];
	CGRect bounds;
	uint32_t num_active_displays;

	cgerr = CGGetActiveDisplayList(ARR_LEN(active_displays), active_displays,
	                               &num_active_displays);
	if (cgerr) {
		fprintf(stderr, "CGGetActiveDisplayList() failed (%d)\n", cgerr);
		return -1;
	}

	if (num_active_displays != 1) {
		fprintf(stderr, "Support for num_displays != 1 NYI (have %u)\n",
		        num_active_displays);
		return -1;
	}

	bounds = CGDisplayBounds(active_displays[0]);
	screenbounds.x.min = CGRectGetMinX(bounds);
	screenbounds.x.max = CGRectGetMaxX(bounds) - 1;
	screenbounds.y.min = CGRectGetMinY(bounds);
	screenbounds.y.max = CGRectGetMaxY(bounds) - 1;

	status = PasteboardCreate(kPasteboardClipboard, &clipboard);
	if (status != noErr) {
		fprintf(stderr, "PasteboardCreate() failed (%d)\n", status);
		return -1;
	}

	return 0;
}

void platform_exit(void)
{
	CFRelease(clipboard);
}

static inline uint32_t cgfloat_to_u32(CGFloat f)
{
	if (f > UINT32_MAX || f < 0) {
		fprintf(stderr, "out-of-range CGFloat: %g\n", f);
		abort();
	}

	return cground(f);
}

static CGPoint get_mousepos_cgpoint(void)
{
	CGPoint cgpt;
	CGEventRef ev = CGEventCreate(NULL);

	if (!ev) {
		fprintf(stderr, "CGEventCreate failed\n");
		abort();
	}

	cgpt = CGEventGetLocation(ev);
	CFRelease(ev);

	return cgpt;
}

#define NO_MOUSEBUTTON 0

static void send_mouseevent(CGPoint cgpt, CGEventType type, CGMouseButton button)
{
	CGEventRef ev;

	cgpt.x = (cgpt.x > screenbounds.x.max) ? screenbounds.x.max : cgpt.x;
	cgpt.y = (cgpt.y > screenbounds.y.max) ? screenbounds.y.max : cgpt.y;

	cgpt.x = (cgpt.x < screenbounds.x.min) ? screenbounds.x.min : cgpt.x;
	cgpt.y = (cgpt.y < screenbounds.y.min) ? screenbounds.y.min : cgpt.y;

	ev = CGEventCreateMouseEvent(NULL, type, cgpt, button);
	if (!ev) {
		fprintf(stderr, "CGEventCreateMouseEvent failed\n");
		abort();
	}
	CGEventPost(kCGHIDEventTap, ev);
	CFRelease(ev);
}

static void set_mousepos_cgpoint(CGPoint cgpt)
{
	send_mouseevent(cgpt, kCGEventMouseMoved, NO_MOUSEBUTTON);
}

struct xypoint get_mousepos(void)
{
	struct xypoint pt;
	CGPoint cgpt = get_mousepos_cgpoint();

	pt.x = cgfloat_to_u32(cgpt.x);
	pt.y = cgfloat_to_u32(cgpt.y);

	return pt;
}

void set_mousepos(struct xypoint pt)
{
	set_mousepos_cgpoint(CGPointMake((CGFloat)pt.x, (CGFloat)pt.y));
}

static inline int mouse_button_held(CGMouseButton btn)
{
	return CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, btn);
}

void move_mousepos(int32_t dx, int32_t dy)
{
	CGPoint pt = get_mousepos_cgpoint();

	pt.x += dx;
	pt.y += dy;

	/* Sigh...why can't Quartz figure this out by itself? */
	if (mouse_button_held(kCGMouseButtonLeft))
		send_mouseevent(pt, kCGEventLeftMouseDragged, kCGMouseButtonLeft);
	else if (mouse_button_held(kCGMouseButtonRight))
		send_mouseevent(pt, kCGEventRightMouseDragged, kCGMouseButtonRight);
	else if (mouse_button_held(kCGMouseButtonCenter))
		send_mouseevent(pt, kCGEventOtherMouseDragged, kCGMouseButtonCenter);
	else
		set_mousepos_cgpoint(pt);
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
		fprintf(stderr, "unhandled click event button %u\n", button);
		return;
	}

	if (button == MB_SCROLLUP || button == MB_SCROLLDOWN)
		ev = CGEventCreateScrollWheelEvent(NULL, kCGScrollEventUnitLine, 1, scrollamt);
	else
		ev = CGEventCreateMouseEvent(NULL, cgtype, get_mousepos_cgpoint(), cgbtn);
	if (!ev) {
		fprintf(stderr, "CGEventCreateMouseEvent failed\n");
		abort();
	}
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
		fprintf(stderr, "PasteboardGetItemIdentifier(1) failed (%d)\n", status);
		return NULL;
	}

	status = PasteboardCopyItemFlavorData(clipboard, itemid, PLAINTEXT, &data);
	if (status != noErr) {
		fprintf(stderr, "PasteboardCopyItemFlavorData(PLAINTEXT) failed (%d)\n", status);
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
		fprintf(stderr, "CFDataCreate() failed\n");
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
		fprintf(stderr, "PasteboardPutItemFlavor() failed (%d)\n", status);
		ret = -1;
	}

	CFRelease(data);

	return ret;
}

int grab_inputs(void)
{
	fprintf(stderr, "grab_inputs() NYI on OSX\n");
	return -1;
}

void ungrab_inputs(void)
{
	fprintf(stderr, "ungrab_inputs() NYI on OSX\n");
}

void process_events(void)
{
	fprintf(stderr, "process_events() NYI on OSX\n");
}
