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

	struct ssh_config sshcfg;

	struct kvmap* params;

	/* connection state */
	connstate_t state;
	pid_t sshpid;

	/*
	 * How many times (since the last successful one) this remote's
	 * connection has failed.
	 */
	int failcount;

	struct msgchan msgchan;

	struct remote* next;
};

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

struct focus_target {
	enum {
		FT_DIRECTION,
		FT_NODE,
	} type;
	union {
		direction_t dir;
		struct noderef nr;
	};
};

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

struct hotkey {
	char* key_string;
	struct action action;
	struct hotkey* next;
};

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

struct mouse_switch {
	enum {
		MS_NONE = 0,
		MS_MULTITAP,
	} type;
	int num;
	uint64_t window;
};

struct link {
	struct {
		struct noderef nr;
		direction_t dir;
	} a, b;

	struct link* next;
};

struct config {
	char* remote_shell;
	char* bind_address;
	struct remote* remotes;
	struct link* topology;
	struct hotkey* hotkeys;

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
