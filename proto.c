
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>

#include <assert.h>

#include "misc.h"
#include "proto.h"

static int write_all(int fd, const void* buf, size_t len)
{
	ssize_t status;
	size_t written = 0;

	while (written < len) {
		status = write(fd, buf + written, len - written);
		if (status < 0) {
			if (status != EINTR)
				return -1;
		} else {
			written += status;
		}
	}

	return 0;
}

static int read_all(int fd, void* buf, size_t len)
{
	ssize_t status;
	size_t done = 0;

	while (done < len) {
		status = read(fd, buf + done, len - done);
		if (status < 0) {
			if (status != EINTR)
				return -1;
		} else if (status == 0) {
			return -1;
		} else {
			done += status;
		}
	}

	return 0;
}

static const size_t payload_sizes[] = {
	[MT_READY] = sizeof(uint32_t),
	[MT_SHUTDOWN] = 0,
	[MT_MOVEREL] = 2 * sizeof(int32_t),
	[MT_CLICKEVENT] = 2 * sizeof(uint32_t),
};

static size_t message_flatsize(const struct message* msg)
{
	assert(msg->type < ARR_LEN(payload_sizes));
	return sizeof(msgtype_t) + payload_sizes[msg->type];
}

static void flatten_ready(const struct message* msg, void* buf)
{
	*(uint32_t*)buf = htonl(msg->ready.prot_vers);
}

static void unflatten_ready(const void* buf, struct message* msg)
{
	msg->ready.prot_vers = ntohl(*(uint32_t*)buf);
}

static void flatten_shutdown(const struct message* msg, void* buf)
{
}

static void unflatten_shutdown(const void* buf, struct message* msg)
{
}

static void flatten_moverel(const struct message* msg, void* buf)
{
	uint32_t* u32b = buf;
	u32b[0] = htonl(msg->moverel.dx);
	u32b[1] = htonl(msg->moverel.dy);
}

static void unflatten_moverel(const void* buf, struct message* msg)
{
	const uint32_t* u32b = buf;
	msg->moverel.dx = ntohl(u32b[0]);
	msg->moverel.dy = ntohl(u32b[1]);
}

static void flatten_clickevent(const struct message* msg, void* buf)
{
	uint32_t* u32b = buf;
	u32b[0] = htonl(msg->clickevent.button);
	u32b[1] = htonl(msg->clickevent.pressrel);
}

static void unflatten_clickevent(const void* buf, struct message* msg)
{
	const uint32_t* u32b = buf;
	msg->clickevent.button = ntohl(u32b[0]);
	msg->clickevent.pressrel = ntohl(u32b[1]);
}

static void (*const flatteners[])(const struct message*, void*) = {
	[MT_READY] = flatten_ready,
	[MT_SHUTDOWN] = flatten_shutdown,
	[MT_MOVEREL] = flatten_moverel,
	[MT_CLICKEVENT] = flatten_clickevent,
};

static void (*const unflatteners[])(const void*, struct message*) = {
	[MT_READY] = unflatten_ready,
	[MT_SHUTDOWN] = unflatten_shutdown,
	[MT_MOVEREL] = unflatten_moverel,
	[MT_CLICKEVENT] = unflatten_clickevent,
};

static void flatten_message(const struct message* msg, void* buf)
{
	flatteners[msg->type](msg, buf);
}

static void unflatten_message(const void* buf, struct message* msg)
{
	unflatteners[msg->type](buf, msg);
}

int send_message(int fd, const struct message* msg)
{
	char msgbuf[1024];
	size_t msgsize = message_flatsize(msg);

	/* Temporary hack */
	assert(msgsize <= sizeof(msgbuf));

	*(msgtype_t*)msgbuf = htonl(msg->type);
	flatten_message(msg, msgbuf + sizeof(msgtype_t));

	return write_all(fd, msgbuf, msgsize);
}

int receive_message(int fd, struct message* msg)
{
	int status;
	char msgbuf[1024];
	size_t plsize;

	status = read_all(fd, &msg->type, sizeof(msgtype_t));
	if (status)
		return status;

	msg->type = ntohl(msg->type);

	if (msg->type >= ARR_LEN(payload_sizes))
		return -1;

	plsize = payload_sizes[msg->type];

	/* Temporary hack */
	assert(plsize < sizeof(msgbuf));

	status = read_all(fd, msgbuf, plsize);
	if (status)
		return status;

	unflatten_message(msgbuf, msg);

	return 0;
}
