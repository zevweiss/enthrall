
#include "misc.h"
#include "msgchan.h"

struct message* mc_dequeue_message(struct msgchan* mc)
{
	struct message* msg;

	msg = mc->sendqueue.head;

	if (msg) {
		mc->sendqueue.head = msg->next;
		if (!msg->next)
			mc->sendqueue.tail = NULL;
		mc->sendqueue.num_queued -= 1;
	}

	return msg;
}


void mc_clear(struct msgchan* mc)
{
	struct message* msg;

	while ((msg = mc_dequeue_message(mc)))
		free_message(msg);

	xfree(mc->send_msgbuf.msgbuf);
	mc->send_msgbuf.msgbuf = NULL;
	mc->send_msgbuf.bytes_sent = 0;
	mc->send_msgbuf.msg_len = 0;

	xfree(mc->recv_msgbuf.plbuf);
	mc->recv_msgbuf.plbuf = NULL;
	mc->recv_msgbuf.bytes_recvd = 0;
}

#define MAX_SEND_BACKLOG 64

int mc_enqueue_message(struct msgchan* mc, struct message* msg)
{
	msg->next = NULL;
	if (mc->sendqueue.tail)
		mc->sendqueue.tail->next = msg;
	mc->sendqueue.tail = msg;
	if (!mc->sendqueue.head)
		mc->sendqueue.head = msg;
	mc->sendqueue.num_queued += 1;

	return mc->sendqueue.num_queued > MAX_SEND_BACKLOG ? -1 : 0;
}

/*
 * Returns positive if some data was sent, zero if nothing was queued, and
 * negative on error.
 */
int send_message(struct msgchan* mc)
{
	int status;
	struct message* msg;

	if (!mc->send_msgbuf.msgbuf) {
		msg = mc_dequeue_message(mc);
		if (!msg)
			return 0;
		unparse_message(msg, &mc->send_msgbuf);
		free_message(msg);
	}

	status = drain_msgbuf(mc->send_fd, &mc->send_msgbuf);

	return status < 0 ? status : 1;
}

int recv_message(struct msgchan* mc, struct message* msg)
{
	int status;

	status = fill_msgbuf(mc->recv_fd, &mc->recv_msgbuf);
	if (status <= 0)
		return status;

	parse_message(&mc->recv_msgbuf, msg);
	return 1;
}
