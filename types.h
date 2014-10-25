#ifndef COMMONDEFS_H
#define COMMONDEFS_H

#include <unistd.h>
#include <stdint.h>

/* Screen position (e.g. for the mouse pointer), with 0,0 at the top left. */
struct xypoint {
	int32_t x;
	int32_t y;
};

struct range {
	int32_t min;
	int32_t max;
};

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

typedef enum {
	PR_PRESS = 1,
	PR_RELEASE,
} pressrel_t;

typedef enum {
	CS_NEW = 0,
	CS_SETTINGUP,
	CS_FAILED,
	CS_PERMFAILED,
	CS_CONNECTED,
	CS_DISCONNECTED,
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

typedef enum {
	NT_NONE = 0,
	NT_REMOTE,
	NT_MASTER,

	/* initial state before a name gets resolved to a remote */
	NT_REMOTE_TMPNAME,
} nodereftype_t;

struct noderef {
	nodereftype_t type;
	union {
		char* name;
		struct remote* node;
	};
};

struct ssh_config {
	char* remoteshell;
	int port;
	char* bindaddr;
	char* identityfile;
	char* username;
	char* remotecmd;
};

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

struct remote {
	char* alias;

	/* Used for graph topology check */
	int reachable;

	char* hostname;

	struct ssh_config sshcfg;

	struct kvmap* params;

	/* neighbors */
	struct noderef neighbors[NUM_DIRECTIONS];

	/* History of mouse arrivals/departures at each screen edge */
	struct edge_state edgehist[NUM_DIRECTIONS];

	/* connection state */
	connstate_t state;
	pid_t sshpid;

	/*
	 * How many times (since the last successful one) this remote's
	 * connection has failed.
	 */
	int failcount;

	/* When we'll next make a reconnection attempt (absolute microseconds) */
	uint64_t next_reconnect_time;

	struct msgchan msgchan;

	/*
	 * List of messages to be sent in the future, sorted in order
	 * increasing sendtime.  A linked list is obviously an inefficient way
	 * to go about this, but it's (at least at time of writing...) low-use
	 * enough that it shouldn't matter.
	 */
	struct message* scheduled_messages;

	struct remote* next;
};

typedef enum {
	AT_SWITCH,
	AT_SWITCHTO,
	AT_RECONNECT,
} actiontype_t;

struct action {
	actiontype_t type;
	union {
		direction_t dir;
		struct noderef node;
	};
};

struct hotkey {
	char* key_string;
	struct action action;
	struct hotkey* next;
};

struct switch_indication {
	enum {
		SI_NONE = 0,
		SI_DIM_INACTIVE,
		SI_FLASH_ACTIVE,
	} type;
	float brightness;
	uint64_t duration;
	int fade_steps;
};

struct mouse_switch {
	enum {
		MS_NONE = 0,
		MS_MULTITAP,
	} type;
	int num;
	uint64_t window;
};

struct master {
	struct noderef neighbors[NUM_DIRECTIONS];
	struct edge_state edgehist[NUM_DIRECTIONS];
};

struct config {
	char* remote_shell;
	char* bind_address;
	struct remote* remotes;
	struct hotkey* hotkeys;

	struct switch_indication switch_indication;
	struct mouse_switch mouseswitch;

	/* default SSH settings, optionally overridden per-remote */
	struct ssh_config ssh_defaults;

	struct master master;
};

#endif /* COMMONDEFS_H */
