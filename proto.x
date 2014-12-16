/*
 * XDR-based core wire protocol.
 *
 * Note that this is not the *entirety* of the wire protocol; see comment in
 * message.c.
 */

/* HACK: glibc's rpcgen generates XDR code with unused variables. */
#ifdef RPC_XDR
%#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

#ifdef __APPLE__
typedef u_int32_t uint32_t;
#endif

enum msgtype_t {
	MT_SETUP = 1,
	MT_READY,
	MT_SHUTDOWN,
	MT_MOVEREL,
	MT_MOVEABS,
	MT_MOUSEPOS,
	MT_CLICKEVENT,
	MT_KEYEVENT,
	MT_GETCLIPBOARD,
	MT_SETCLIPBOARD,
	MT_LOGMSG,
	MT_SETBRIGHTNESS
};

/* Screen position (e.g. for the mouse pointer), with 0,0 at the top left. */
struct xypoint {
	int32_t x;
	int32_t y;
};

struct range {
	int32_t min;
	int32_t max;
};

/* An area of screen space (used for screen dimensions) */
struct rectangle {
	range x;
	range y;
};

struct kvpair {
	string key<>;
	string value<>;
};

struct setup_body {
	uint32_t prot_vers;
	uint32_t loglevel;
	kvpair params<>;
};

struct ready_body {
	rectangle screendim;
};

/* SHUTDOWN messages have no body content */

struct moverel_body {
	int32_t dx;
	int32_t dy;
};

struct moveabs_body {
	xypoint pt;
};

struct mousepos_body {
	xypoint pt;
};

struct clickevent_body {
	uint32_t button;
	uint32_t pressrel;
};

struct keyevent_body {
	uint32_t keycode;
	uint32_t pressrel;
};

/* GETCLIPBOARD messages have no body content */

struct setclipboard_body {
	string text<>;
};

struct logmsg_body {
	string msg<>;
};

struct setbrightness_body {
	float brightness;
};

union msgbody switch (msgtype_t type) {
case MT_SETUP:
	setup_body setup;
case MT_READY:
	ready_body ready;
case MT_SHUTDOWN:
	void;
case MT_MOVEREL:
	moverel_body moverel;
case MT_MOVEABS:
	moveabs_body moveabs;
case MT_MOUSEPOS:
	mousepos_body mousepos;
case MT_CLICKEVENT:
	clickevent_body clickevent;
case MT_KEYEVENT:
	keyevent_body keyevent;
case MT_GETCLIPBOARD:
	void;
case MT_SETCLIPBOARD:
	setclipboard_body setclipboard;
case MT_LOGMSG:
	logmsg_body logmsg;
case MT_SETBRIGHTNESS:
	setbrightness_body setbrightness;
};
