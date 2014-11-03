
#include "misc.h"
#include "msgchan.h"

static struct message* mc_dequeue_message(struct msgchan* mc)
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

	fdmon_monitor(mc->send.mon, FM_WRITE);

	return mc->sendqueue.num_queued > MAX_SEND_BACKLOG ? -1 : 0;
}

/*
 * Returns positive if some data was sent, zero if nothing was queued, and
 * negative on error.
 */
static int send_message(struct msgchan* mc)
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

	status = drain_msgbuf(mc->send.fd, &mc->send_msgbuf);

	return status < 0 ? status : 1;
}

static int recv_message(struct msgchan* mc, struct message* msg)
{
	int status;

	status = fill_msgbuf(mc->recv.fd, &mc->recv_msgbuf);
	if (status <= 0)
		return status;

	parse_message(&mc->recv_msgbuf, msg);
	return 1;
}

static void mc_read_cb(struct fdmon_ctx* ctx, void* arg)
{
	struct msgchan* mc;
	struct message msg;
	int status;

	mc = arg;

	status = recv_message(mc, &msg);
	if (!status)
		return;
	else if (status < 0)
		mc->cb.err(mc, mc->cb.arg);
	else {
		mc->cb.recv(mc, &msg, mc->cb.arg);
		if (msg.extra.len)
			xfree(msg.extra.buf);
	}
}

static inline int mc_have_outbound_data(const struct msgchan* mc)
{
	return mc->send_msgbuf.msgbuf || mc->sendqueue.head;
}

static void mc_write_cb(struct fdmon_ctx* ctx, void* arg)
{
	int status;
	struct msgchan* mc = arg;

	if (!mc_have_outbound_data(mc)) {
		elog("mc_write_cb() with no outbound data??\n");
		fdmon_unmonitor(ctx, FM_WRITE);
		return;
	}

	status = send_message(mc);
	if (status < 0)
		mc->cb.err(mc, mc->cb.arg);
	else
		assert(status == 1);

	if (mc_have_outbound_data(mc))
		fdmon_monitor(ctx, FM_WRITE);
	else
		fdmon_unmonitor(ctx, FM_WRITE);
}

void mc_init(struct msgchan* mc, int send_fd, int recv_fd, mc_recv_cb_t recv_cb,
             mc_err_cb_t err_cb, void* cb_arg)
{
	mc_clear(mc);
	mc->send.fd = send_fd;
	mc->recv.fd = recv_fd;

	set_fd_nonblock(mc->send.fd, 1);
	if (mc->recv.fd != mc->send.fd)
		set_fd_nonblock(mc->recv.fd, 1);

	mc->send.mon = fdmon_register_fd(mc->send.fd, NULL, mc_write_cb, mc);
	mc->recv.mon = fdmon_register_fd(mc->recv.fd, mc_read_cb, NULL, mc);

	mc->cb.recv = recv_cb;
	mc->cb.err = err_cb;
	mc->cb.arg = cb_arg;

	fdmon_monitor(mc->recv.mon, FM_READ);
}

void mc_close(struct msgchan* mc)
{
	mc_clear(mc);

	fdmon_unregister(mc->send.mon);
	fdmon_unregister(mc->recv.mon);

	close(mc->send.fd);
	if (mc->recv.fd != mc->send.fd)
		close(mc->recv.fd);
}
