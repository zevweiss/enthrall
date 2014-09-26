
#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>

#define PROT_VERSION 0

enum {
	MT_READY = 1,
	MT_SHUTDOWN,
	MT_MOVEREL,
	MT_CLICKEVENT,
	MT_GETCLIPBOARD,
	MT_SETCLIPBOARD,
};

typedef uint32_t msgtype_t;

struct ready_msg {
	uint32_t prot_vers;
};

struct shutdown_msg {
};

struct moverel_msg {
	int32_t dx;
	int32_t dy;
};

struct clickevent_msg {
	uint32_t button;
	uint32_t pressrel;
};

struct getclipboard_msg {
};

struct setclipboard_msg {
	uint32_t length;
	/* message is followed by a 'length'-byte payload */
};

struct message {
	msgtype_t type;
	union {
		struct ready_msg ready;
		struct shutdown_msg shutdown;
		struct moverel_msg moverel;
		struct clickevent_msg clickevent;
		struct getclipboard_msg getclipboard;
		struct setclipboard_msg setclipboard;
	};
};

int send_message(int fd, const struct message* msg);
int receive_message(int fd, struct message* msg);

#endif /* PROTO_H */
