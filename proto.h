
#ifndef PROTO_H
#define PROTO_H

#include <stdlib.h>
#include <stdint.h>

#define PROT_VERSION 0

enum {
	MT_SETUP = 1,
	MT_READY,
	MT_SHUTDOWN,
	MT_MOVEREL,
	MT_CLICKEVENT,
	MT_KEYEVENT,
	MT_GETCLIPBOARD,
	MT_SETCLIPBOARD,
	MT_LOGMSG,
	MT_SETBRIGHTNESS,
};

typedef uint32_t msgtype_t;

struct setup_msg {
	uint32_t prot_vers;
	/* extra data buffer contains additional configuration parameters */
};

struct ready_msg {
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

struct logmsg_msg {
	/* "extra" buffer has string to be logged */
};

struct setbrightness_msg {
	float brightness;
};

/*
 * How many bytes we will always unconditionally read at the start of a
 * message (the initial fixed-size part).
 */
#define MSGHDR_SIZE (sizeof(msgtype_t) /* type */ \
                     + sizeof(uint32_t) /* extra-buf size */)

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
		struct setup_msg setup;
		struct ready_msg ready;
		struct shutdown_msg shutdown;
		struct moverel_msg moverel;
		struct clickevent_msg clickevent;
		struct keyevent_msg keyevent;
		struct getclipboard_msg getclipboard;
		struct setclipboard_msg setclipboard;
		struct logmsg_msg logmsg;
		struct setbrightness_msg setbrightness;
	};

	/* Extra data accompanying message (e.g. clipboard contents) */
	struct {
		size_t len;
		void* buf;
	} extra;

	/* Time at which this message should be sent (not on-wire) */
	uint64_t sendtime;

	/* For linking into a list (doesn't exist on-wire) */
	struct message* next;
};

struct partrecv {
	char hdrbuf[MSGHDR_SIZE];
	void* plbuf;
	size_t bytes_recvd;
};

struct partsend {
	void* msgbuf;
	size_t msg_len;
	size_t bytes_sent;
};

struct message* new_message(msgtype_t type);
void free_message(struct message* msg);

int write_message(int fd, const struct message* msg);
int read_message(int fd, struct message* msg);

int fill_msgbuf(int fd, struct partrecv* pr);
void parse_message(struct partrecv* pr, struct message* msg);

void unparse_message(const struct message* msg, struct partsend* ps);
int drain_msgbuf(int fd, struct partsend* ps);

#endif /* PROTO_H */
