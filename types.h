#ifndef COMMONDEFS_H
#define COMMONDEFS_H

#include <unistd.h>
#include <stdint.h>

#include "events.h"

/* Screen position (e.g. for the mouse pointer), with 0,0 at the top left. */
struct xypoint {
	int32_t x;
	int32_t y;
};

struct range {
	int32_t min;
	int32_t max;
};

/* An area of screen space (used for recording screen dimensions) */
struct rectangle {
	struct range x;
	struct range y;
};

typedef enum {
	MB_LEFT = 1,
	MB_RIGHT,
	MB_CENTER,
	MB_SCROLLUP,
	MB_SCROLLDOWN,
	/* Remember to update this if/when needed... */
	NUM_MOUSEBUTTONS = MB_SCROLLDOWN,
} mousebutton_t;

/* Platform-independent internal representation of a keyboard key */
typedef uint32_t keycode_t;

/* Whether a given keystroke/mouse-click is a press or release */
typedef enum {
	PR_PRESS = 1,
	PR_RELEASE,
} pressrel_t;

/* Different states a remote connection can be in at any given time */
typedef enum {
	CS_NEW = 0,
	CS_SETTINGUP,
	CS_FAILED,
	CS_PERMFAILED,
	CS_CONNECTED,
} connstate_t;

typedef enum {
	NO_DIR = -1,
	LEFT = 0,
	RIGHT,
	UP,
	DOWN,
	NUM_DIRECTIONS,
} direction_t;

#define for_each_direction(d) for (d = LEFT; d < NUM_DIRECTIONS; d++)

typedef uint32_t dirmask_t;

#define LEFTMASK (1U << LEFT)
#define RIGHTMASK (1U << RIGHT)
#define UPMASK (1U << UP)
#define DOWNMASK (1U << DOWN)

#define ALLDIRS_MASK (LEFTMASK|RIGHTMASK|UPMASK|DOWNMASK)

/* Configuration options used to set command-line arguments when invoking ssh */
struct ssh_config {
	char* remoteshell;
	int port;
	char* bindaddr;
	char* identityfile;
	char* username;
	char* remotecmd;
};

/* Types of "edge events" (mouse pointer arriving at or leaving a screen edge) */
typedef enum {
	EE_DEPART,
	EE_ARRIVE,
} edgeevent_t;

/* How long a history of edge events we track */
#define EDGESTATE_HISTLEN 8

/*
 * Circular buffer containing recent history of mouse-pointer
 * arrival/departure events at a given screen edge, in strict alternation
 */
struct edge_state {
	uint64_t event_times[EDGESTATE_HISTLEN];
	edgeevent_t last_evtype;
	/* Where in the circular buffer the last event is */
	unsigned int evidx;
};

#include "msgchan.h"
#include "proto.h"
#include "kvmap.h"

struct node {
	char* name;

	/* Bounds of the node's effective logical screen */
	struct rectangle dimensions;

	/* Neighboring node in each direction */
	struct node* neighbors[NUM_DIRECTIONS];

	/* History of mouse arrivals/departures at each screen edge */
	struct edge_state edgehist[NUM_DIRECTIONS];

	/* Bitmask of which screen edges the mouse pointer is currently at */
	dirmask_t edgemask;

	/* Pointer to the remote info for this node (NULL for master) */
	struct remote* remote;
};

struct remote {
	struct node node;

	/* Used for graph topology check */
	int reachable;

	char* hostname;

	/* how to invoke ssh to this remote */
	struct ssh_config sshcfg;

	/* miscellaneous extra parameters from config file */
	struct kvmap* params;

	/* connection state */
	connstate_t state;

	/* pid of the ssh process we're connected via */
	pid_t sshpid;

	/*
	 * How many times (since the last successful one) this remote's
	 * connection has failed.
	 */
	int failcount;

	/* timer for determing when to next attempt a reconnect */
	timer_ctx_t reconnect_timer;

	/* msgchan by which the master exchanges messages with this remote */
	struct msgchan msgchan;

	/* for linking into a list of remotes */
	struct remote* next;
};

/*
 * A reference to a node; starts as a string (the node's name) after
 * config-file parsing and then gets resolved to an actual node during
 * setup/initialization.
 */
struct noderef {
	enum {
		/* initial state before a name gets resolved to a node */
		NT_TMPNAME,
		NT_NODE,
	} type;
	union {
		char* name;
		struct node* node;
	};
};

/* Things that can go in a 'focus' hotkey action. */
struct focus_target {
	enum {
		FT_DIRECTION,
		FT_NODE,
		FT_PREVIOUS,
	} type;
	union {
		direction_t dir;
		struct noderef nr;
	};
};

/* Actions that can be assigned to a hotkey */
struct action {
	enum {
		AT_FOCUS,
		AT_RECONNECT,
		AT_QUIT,
	} type;
	union {
		struct focus_target target;
	};
};

/* A user-configured hotkey */
struct hotkey {
	/* Platform-dependent string encoding the key(s) */
	char* key_string;

	/* Action to perform when pressed */
	struct action action;

	/* for linking into a list of hotkeys */
	struct hotkey* next;
};

/* Different ways focus can be visually indicated */
struct focus_hint {
	enum {
		FH_NONE = 0,
		FH_DIM_INACTIVE,
		FH_FLASH_ACTIVE,
	} type;
	float brightness;
	uint64_t duration;
	int fade_steps;
};

/* Configurable ways of switching focus with the mouse */
struct mouse_switch {
	enum {
		MS_NONE = 0,
		MS_MULTITAP,
	} type;
	int num;
	uint64_t window;
};

/* A link in the node topology graph */
struct link {
	struct {
		struct noderef nr;
		direction_t dir;
	} a, b;

	struct link* next;
};

/* Options for log message destination */
struct logfile {
	enum {
		LF_STDERR = 0,
		LF_FILE,
		LF_SYSLOG,
		LF_NONE,
	} type;
	char* path;
};

struct config {
	char* remote_shell;
	char* bind_address;
	struct remote* remotes;
	struct link* topology;
	struct hotkey* hotkeys;

	struct {
		struct logfile file;
		unsigned int level;
	} log;

	struct {
		int max_tries;
		uint64_t max_interval;
	} reconnect;

	struct focus_hint focus_hint;
	struct mouse_switch mouseswitch;

	enum {
		NS_NO,
		NS_YES,
		NS_HOTKEYONLY,
	} show_nullswitch;

	/* default SSH settings, optionally overridden per-remote */
	struct ssh_config ssh_defaults;

	struct node master;
};

#endif /* COMMONDEFS_H */
