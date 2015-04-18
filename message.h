
#ifndef PROTO_H
#define PROTO_H

#include <stdlib.h>
#include <stdint.h>

#include "types.h"

#include "proto.h"

#define PROT_VERSION 0

struct message {
	struct msgbody body;

	/*
	 * Whether this message's body was filled in by XDR and thus should be
	 * passed to xdr_free() for freeing instead passing individual members
	 * to xfree().
	 */
	int from_xdr;

	/* For linking into a list (doesn't exist on the wire) */
	struct message* next;
};

/* Shorthand macro for accessing message body members */
#define MB(m, t) ((m)->body.msgbody_u.t)

/*
 * How many bytes we will always unconditionally read at the start of a
 * message (the initial fixed-size part).
 */
#define MSGHDR_SIZE (sizeof(uint32_t))

/* Buffer used for storing an incoming (possibly incomplete) message */
struct partrecv {
	char hdrbuf[MSGHDR_SIZE];
	void* plbuf;
	size_t bytes_recvd;
};

/* Buffer for storing an outgoing (possibly only partially-sent) message */
struct partsend {
	void* buf;
	size_t len;
	size_t bytes_sent;
};

struct message* new_message(msgtype_t type);
void free_message(struct message* msg);
void free_msgbody(struct message* msg);

int fill_msgbuf(int fd, struct partrecv* pr);
int parse_message(struct partrecv* pr, struct message* msg);

void unparse_message(const struct message* msg, struct partsend* ps);
int drain_msgbuf(int fd, struct partsend* ps);

#endif /* PROTO_H */
