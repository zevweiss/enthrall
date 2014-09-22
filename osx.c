#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#include <CoreGraphics/CGEvent.h>

#include "types.h"
#include "proto.h"
#include "platform.h"

#if CGFLOAT_IS_DOUBLE
#define cground lround
#else
#define cground lroundf
#endif

int platform_init(void)
{
	return 0;
}

void platform_exit(void)
{
	return;
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

static void set_mousepos_cgpoint(CGPoint cgpt)
{
	CGEventRef ev = CGEventCreateMouseEvent(NULL, kCGEventMouseMoved, cgpt, 0);
	if (!ev) {
		fprintf(stderr, "CGEventCreateMouseEvent failed\n");
		abort();
	}
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

void set_mousepos(struct xypoint pt)
{
	set_mousepos_cgpoint(CGPointMake((CGFloat)pt.x, (CGFloat)pt.y));
}

void move_mousepos(int32_t dx, int32_t dy)
{
	CGPoint pt = get_mousepos_cgpoint();

	pt.x += dx;
	pt.y += dy;

	set_mousepos_cgpoint(pt);
}

void do_clickevent(mousebutton_t button, pressrel_t pr)
{
	CGEventType cgtype;
	CGMouseButton cgbtn;
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
	default:
		fprintf(stderr, "unhandled click event button %u\n", button);
		return;
	}

	ev = CGEventCreateMouseEvent(NULL, cgtype, get_mousepos_cgpoint(), cgbtn);
	if (!ev) {
		fprintf(stderr, "CGEventCreateMouseEvent failed\n");
		abort();
	}
	CGEventPost(kCGHIDEventTap, ev);
	CFRelease(ev);
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

int remote_mode(void)
{
	fprintf(stderr, "remote_mode() NYI on OSX\n");
	return -1;
}
