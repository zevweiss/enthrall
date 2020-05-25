/*
 * XDR-based core wire protocol.
 *
 * Note that this is not the *entirety* of the wire protocol; see comment in
 * message.c.
 *
 * Note also that while some messages are described as expecting a reply, the
 * only place where the reply is a strict requirement is in the initial
 * SETUP/READY handshake sequence; beyond that the protocol is completely
 * stateless (though if everything's operating correctly the expected replies
 * *should* be given).
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
	MT_MOVEREL,
	MT_MOVEABS,
	MT_MOUSEPOS,
	MT_CLICKEVENT,
	MT_KEYEVENT,
	MT_GETCLIPBOARD,
	MT_SETCLIPBOARD,
	MT_LOGMSG,
	MT_SETBRIGHTNESS,
	MT_EDGEEVENT
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

/* A simple key-value pair. */
struct kvpair {
	string key<>;
	string value<>;
};

/*
 * SETUP: the first message sent by the master to each remote upon
 * establishing a connection.  Contains various initialization parameters,
 * including log level and an unstructured kvmap of miscellaneous other things
 * (like the DISPLAY environment variable for X11 remotes).
 *
 * Should trigger a READY in reply.
 */
struct setup_body {
	uint32_t prot_vers;
	uint32_t loglevel;
	kvpair params<>;
};

/*
 * READY: the first message sent by a newly-alive remote in response to
 * receiving a SETUP from the master.  Informs the master of the remote's
 * display dimensions.
 *
 * No reply expected.
 */
struct ready_body {
	rectangle screendim;
};

/*
 * MOVEREL: sent by the master to a remote to instruct it move the mouse
 * pointer relative to its current position.
 *
 * Should trigger a MOUSEPOS in reply.
 */
struct moverel_body {
	int32_t dx;
	int32_t dy;
};

/*
 * MOVEABS: sent by the master to a remote to instruct it to move the mouse
 * pointer to an absolute position.
 *
 * No reply expected.
 */
struct moveabs_body {
	xypoint pt;
};

/*
 * MOUSEPOS: sent by remotes to the master in response to a MOVEREL to inform
 * the master of the mouse pointer's new position (post-MOVEREL).
 *
 * No reply expected.
 */
struct mousepos_body {
	xypoint pt;
};

/*
 * CLICKEVENT: sent by the master to a remote to generate a mouse-click event.
 *
 * No reply expected.
 */
struct clickevent_body {
	uint32_t button;
	uint32_t pressrel;
};

/*
 * KEYEVENT: sent by the master to a remote to generate a keypress event.
 *
 * No reply expected.
 */
struct keyevent_body {
	uint32_t keycode;
	uint32_t pressrel;
};

/*
 * GETCLIPBOARD: sent by the master to a remote to retrieve the contents of
 * the remote's clipboard.
 *
 * Should trigger a SETCLIPBOARD in reply.
 *
 * GETCLIPBOARD messages have no body content.
 */

/*
 * SETCLIPBOARD: sent from the master to a remote when switching focus to that
 * remote, or from a remote to the master in response to a GETCLIPBOARD.
 * Instructs the recipient to set its clipboard to the supplied content.
 *
 * No reply expected.
 */
struct setclipboard_body {
	string text<>;
};

/*
 * LOGMSG: sent by remotes to the master to write a message to the log.
 * Log-level filtering is done on the remotes (so that this already-chatty
 * protocol doesn't become wastefully more so), so LOGMSG messages are
 * unconditionally written to the log.
 *
 * No reply expected.
 */
struct logmsg_body {
	string msg<>;
};

/*
 * SETBRIGHTNESS: sent by the master to a remote to instruct it to set its
 * screen brightness to the given level.
 *
 * No reply expected.
 */
struct setbrightness_body {
	float brightness;
};

/*
 * EDGEEVENT: sent by remotes to the master to report the mouse pointer
 * hitting or leaving the edge of the screen.
 *
 * No reply expected.
 */
struct edgeevent_body {
	/* edgeevent_t EE_* value */
	uint32_t type;

	/* direction_t value */
	uint32_t dir;

	/* distance/speed indicator (only when type == EE_ARRIVE) */
	int32_t delta;

	/* current mouse position */
	struct xypoint pos;
};

union msgbody switch (msgtype_t type) {
case MT_SETUP:
	setup_body setup;
case MT_READY:
	ready_body ready;
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
case MT_EDGEEVENT:
	edgeevent_body edgeevent;
};
