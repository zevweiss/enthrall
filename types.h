#ifndef COMMONDEFS_H
#define COMMONDEFS_H

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
} mousebutton_t;

typedef enum {
	PR_PRESS = 1,
	PR_RELEASE,
} pressrel_t;

typedef enum {
	CS_NEW = 0,
	CS_SETTINGUP,
	CS_FAILED,
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
} neighbortype_t;

struct neighbor {
	neighbortype_t type;
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
	struct neighbor neighbors[NUM_DIRECTIONS];

	/* connection state */
	connstate_t state;
	pid_t sshpid;
	int sock;

	struct remote* next;
};

struct config {
	char* remote_shell;
	char* bind_address;
	struct remote* remotes;

	/* master's neighbors */
	struct neighbor neighbors[NUM_DIRECTIONS];
};

#endif /* COMMONDEFS_H */
