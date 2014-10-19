#ifndef COMMONDEFS_H
#define COMMONDEFS_H

#include <unistd.h>
#include <stdint.h>

#include "proto.h"

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

struct remote {
	char* alias;

	/* Used for graph topology check */
	int reachable;

	/* remote shell parameters */
	char* hostname;
	char* username;
	int port;
	char* remotecmd;

	/* neighbors */
	struct noderef neighbors[NUM_DIRECTIONS];

	/* connection state */
	connstate_t state;
	pid_t sshpid;
	int sock;

	/*
	 * How many times (since the last successful one) this remote's
	 * connection has failed.
	 */
	int failcount;

	/* When we'll next make a reconnection attempt (absolute microseconds) */
	uint64_t next_reconnect_time;

	/* For buffering partial inbound & outbound messages */
	struct partrecv recv_msgbuf;
	struct partsend send_msgbuf;

	struct {
		struct message* head;
		struct message* tail;
		int num_queued;
	} sendqueue;

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

struct config {
	char* remote_shell;
	char* bind_address;
	struct remote* remotes;
	struct hotkey* hotkeys;

	/* master's neighbors */
	struct noderef neighbors[NUM_DIRECTIONS];
};

#endif /* COMMONDEFS_H */
