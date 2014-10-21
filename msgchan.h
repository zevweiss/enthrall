/*
 * Bidirectional message channels.
 */

#ifndef MSGCHAN_H
#define MSGCHAN_H

#include "proto.h"

struct msgchan {
	int send_fd, recv_fd;

	/* For buffering partial inbound & outbound messages */
	struct partrecv recv_msgbuf;
	struct partsend send_msgbuf;

	struct {
		struct message* head;
		struct message* tail;
		int num_queued;
	} sendqueue;
};

void mc_clear(struct msgchan* mc);

int mc_enqueue_message(struct msgchan* mc, struct message* msg);
struct message* mc_dequeue_message(struct msgchan* mc);

int send_message(struct msgchan* mc);
int recv_message(struct msgchan* mc, struct message* msg);

static inline int mc_have_outbound_data(const struct msgchan* mc)
{
	return mc->send_msgbuf.msgbuf || mc->sendqueue.head;
}

static inline void mc_init(struct msgchan* mc, int send_fd, int recv_fd)
{
	mc_clear(mc);
	mc->send_fd = send_fd;
	mc->recv_fd = recv_fd;
}

static inline void mc_close(struct msgchan* mc)
{
	mc_clear(mc);
	close(mc->send_fd);
	if (mc->recv_fd != mc->send_fd)
		close(mc->recv_fd);
}


#endif /* MSGCHAN_H */
