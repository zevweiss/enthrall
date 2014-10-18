
#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>

#define PROT_VERSION 0

enum {
	MT_READY = 1,
	MT_SHUTDOWN,
	MT_MOVEREL,
	MT_CLICKEVENT,
	MT_KEYEVENT,
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

struct keyevent_msg {
	uint32_t keycode;
	uint32_t pressrel;
};

struct getclipboard_msg {
};

struct setclipboard_msg {
	/* message's "extra" buffer contains clipboard contents */
};


/*
 * Note that the on-the-wire message format (in order to reduce the number of
 * syscalls it takes to read a message) is ordered differently than the
 * members of this struct (which is arranged for in-memory ease of use).
 */
struct message {
	/* What type of message this is (tag for the union below) */
	msgtype_t type;

	/* Primary message payload */
	union {
		struct ready_msg ready;
		struct shutdown_msg shutdown;
		struct moverel_msg moverel;
		struct clickevent_msg clickevent;
		struct keyevent_msg keyevent;
		struct getclipboard_msg getclipboard;
		struct setclipboard_msg setclipboard;
	};

	/* Extra data accompanying message (e.g. clipboard contents) */
	struct {
		size_t len;
		void* buf;
	} extra;
};

int send_message(int fd, const struct message* msg);
int receive_message(int fd, struct message* msg);

#endif /* PROTO_H */
