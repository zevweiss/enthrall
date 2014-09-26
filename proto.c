
#include <stdint.h>
#include <arpa/inet.h>

#include <assert.h>

#include "misc.h"
#include "proto.h"

static const size_t payload_sizes[] = {
	[MT_READY] = sizeof(uint32_t),
	[MT_SHUTDOWN] = 0,
	[MT_MOVEREL] = 2 * sizeof(int32_t),
	[MT_CLICKEVENT] = 2 * sizeof(uint32_t),
	[MT_GETCLIPBOARD] = 0,
	[MT_SETCLIPBOARD] = sizeof(uint32_t),
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

static void flatten_getclipboard(const struct message* msg, void* buf)
{
}

static void unflatten_getclipboard(const void* buf, struct message* msg)
{
}

static void flatten_setclipboard(const struct message* msg, void* buf)
{
	*(uint32_t*)buf = htonl(msg->setclipboard.length);
}

static void unflatten_setclipboard(const void* buf, struct message* msg)
{
	msg->setclipboard.length = ntohl(*(uint32_t*)buf);
}

static void (*const flatteners[])(const struct message*, void*) = {
	[MT_READY] = flatten_ready,
	[MT_SHUTDOWN] = flatten_shutdown,
	[MT_MOVEREL] = flatten_moverel,
	[MT_CLICKEVENT] = flatten_clickevent,
	[MT_GETCLIPBOARD] = flatten_getclipboard,
	[MT_SETCLIPBOARD] = flatten_setclipboard,
};

static void (*const unflatteners[])(const void*, struct message*) = {
	[MT_READY] = unflatten_ready,
	[MT_SHUTDOWN] = unflatten_shutdown,
	[MT_MOVEREL] = unflatten_moverel,
	[MT_CLICKEVENT] = unflatten_clickevent,
	[MT_GETCLIPBOARD] = unflatten_getclipboard,
	[MT_SETCLIPBOARD] = unflatten_setclipboard,
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
