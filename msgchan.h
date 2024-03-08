/*
 * Bidirectional async message channels.
 *
 * On the sending path, buffers messages if the output file descriptor blocks.
 *
 * On the receiving path, calls a handler function when a message is received.
 */

#ifndef MSGCHAN_H
#define MSGCHAN_H

#include "message.h"
#include "events.h"

struct msgchan;

typedef void (*mc_recv_cb_t)(struct msgchan* chan, struct message* msg, void* arg);
typedef void (*mc_err_cb_t)(struct msgchan* chan, void* arg, int err);

struct msgchan {
	struct {
		int fd;
		struct fdmon_ctx* mon;
	} send, recv;

	/* For buffering partial inbound & outbound messages */
	struct partrecv recv_msgbuf;
	struct partsend send_msgbuf;

	/* Callbacks */
	struct {
		/* Called when a message is received */
		mc_recv_cb_t recv;

		/* Called on error */
		mc_err_cb_t err;

		/* Opaque argument passed to callbacks */
		void* arg;
	} cb;

	/* Buffer of pending messages to be sent */
	struct {
		struct message* head;
		struct message* tail;
		int num_queued;
	} sendqueue;
};

void mc_clear(struct msgchan* mc);

int mc_enqueue_message(struct msgchan* mc, struct message* msg);

void mc_init(struct msgchan* mc, int send_fd, int recv_fd,
             mc_recv_cb_t recv_cb, mc_err_cb_t err_cb, void* cb_arg);
void mc_close(struct msgchan* mc);

#endif /* MSGCHAN_H */
