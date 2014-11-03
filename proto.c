
#include <errno.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <math.h>

#include <assert.h>

#include "misc.h"
#include "proto.h"

/*
 * Conversion from floats to and from a 32-bit fixed point wire-format
 * representation with one sign bit, 15 bits of integer magnitude and 16 bits
 * of fraction (with denominator 2^16).
 */

static float fixed_to_float(uint32_t fx)
{
	uint32_t i = (fx >> 16) & 0x7fff;
	uint32_t num = fx & 0xffff;
	uint32_t denom = 1U << 16;
	float sign = (fx & (1U << 31)) ? -1.0 : 1.0;

	return sign * ((float)i + ((float)num / (float)denom));
}

static uint32_t float_to_fixed(float orig_fp)
{
	float f_ipart, f_fpart;
	uint32_t ipart, fpart;
	uint32_t sign = 0;
	float fp = orig_fp;

	if (fp < 0.0) {
		sign = 1U << 31;
		fp = fabs(fp);
	}

	f_fpart = modff(fp, &f_ipart);

	if (f_ipart > (float)0x7fff) {
		elog("float_to_fixed() got out-of-range argument: %f\n", orig_fp);
		abort();
	}

	fpart = lrintf(f_fpart * (float)(1 << 16));
	assert(!(fpart & ~0xffff));

	ipart = lrintf(f_ipart);
	assert(!(ipart & ~0x7ffff));

	return sign | (ipart << 16) | fpart;
}

static const size_t payload_sizes[] = {
	[MT_SETUP] = sizeof(uint32_t),
	[MT_READY] = 4 * sizeof(uint32_t),
	[MT_SHUTDOWN] = 0,
	[MT_MOVEREL] = 2 * sizeof(int32_t),
	[MT_MOVEABS] = 2 * sizeof(uint32_t),
	[MT_MOUSEPOS] = 2 * sizeof(uint32_t),
	[MT_CLICKEVENT] = 2 * sizeof(uint32_t),
	[MT_KEYEVENT] = 2 * sizeof(uint32_t),
	[MT_GETCLIPBOARD] = 0,
	[MT_SETCLIPBOARD] = 0,
	[MT_LOGMSG] = 0,
	[MT_SETBRIGHTNESS] = sizeof(uint32_t),
};

static void flatten_setup(const struct message* msg, void* buf)
{
	*(uint32_t*)buf = htonl(msg->setup.prot_vers);
}

static void unflatten_setup(const void* buf, struct message* msg)
{
	msg->setup.prot_vers = ntohl(*(uint32_t*)buf);
}

static void flatten_ready(const struct message* msg, void* buf)
{
	uint32_t* u32b = buf;
	u32b[0] = htonl(msg->ready.screendim.x.min);
	u32b[1] = htonl(msg->ready.screendim.x.max);
	u32b[2] = htonl(msg->ready.screendim.y.min);
	u32b[3] = htonl(msg->ready.screendim.y.max);
}

static void unflatten_ready(const void* buf, struct message* msg)
{
	const uint32_t* u32b = buf;
	msg->ready.screendim.x.min = ntohl(u32b[0]);
	msg->ready.screendim.x.max = ntohl(u32b[1]);
	msg->ready.screendim.y.min = ntohl(u32b[2]);
	msg->ready.screendim.y.max = ntohl(u32b[3]);
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

static void flatten_moveabs(const struct message* msg, void* buf)
{
	uint32_t* u32b = buf;
	u32b[0] = htonl(msg->moveabs.pt.x);
	u32b[1] = htonl(msg->moveabs.pt.y);
}

static void unflatten_moveabs(const void* buf, struct message* msg)
{
	const uint32_t* u32b = buf;
	msg->moveabs.pt.x = ntohl(u32b[0]);
	msg->moveabs.pt.y = ntohl(u32b[1]);
}

static void flatten_mousepos(const struct message* msg, void* buf)
{
	uint32_t* u32b = buf;
	u32b[0] = htonl(msg->mousepos.pt.x);
	u32b[1] = htonl(msg->mousepos.pt.y);
}

static void unflatten_mousepos(const void* buf, struct message* msg)
{
	const uint32_t* u32b = buf;
	msg->mousepos.pt.x = ntohl(u32b[0]);
	msg->mousepos.pt.y = ntohl(u32b[1]);
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

static void flatten_keyevent(const struct message* msg, void* buf)
{
	uint32_t* u32b = buf;
	u32b[0] = htonl(msg->keyevent.keycode);
	u32b[1] = htonl(msg->keyevent.pressrel);
}

static void unflatten_keyevent(const void* buf, struct message* msg)
{
	const uint32_t* u32b = buf;
	msg->keyevent.keycode = ntohl(u32b[0]);
	msg->keyevent.pressrel = ntohl(u32b[1]);
}

static void flatten_getclipboard(const struct message* msg, void* buf)
{
}

static void unflatten_getclipboard(const void* buf, struct message* msg)
{
}

static void flatten_setclipboard(const struct message* msg, void* buf)
{
}

static void unflatten_setclipboard(const void* buf, struct message* msg)
{
}

static void flatten_logmsg(const struct message* msg, void* buf)
{
}

static void unflatten_logmsg(const void* buf, struct message* msg)
{
}

static void flatten_setbrightness(const struct message* msg, void* buf)
{
	uint32_t* u32b = buf;
	u32b[0] = htonl(float_to_fixed(msg->setbrightness.brightness));
}

static void unflatten_setbrightness(const void* buf, struct message* msg)
{
	const uint32_t* u32b = buf;
	msg->setbrightness.brightness = fixed_to_float(ntohl(u32b[0]));
}

static void (*const flatteners[])(const struct message*, void*) = {
	[MT_SETUP] = flatten_setup,
	[MT_READY] = flatten_ready,
	[MT_SHUTDOWN] = flatten_shutdown,
	[MT_MOVEREL] = flatten_moverel,
	[MT_MOVEABS] = flatten_moveabs,
	[MT_MOUSEPOS] = flatten_mousepos,
	[MT_CLICKEVENT] = flatten_clickevent,
	[MT_KEYEVENT] = flatten_keyevent,
	[MT_GETCLIPBOARD] = flatten_getclipboard,
	[MT_SETCLIPBOARD] = flatten_setclipboard,
	[MT_LOGMSG] = flatten_logmsg,
	[MT_SETBRIGHTNESS] = flatten_setbrightness,
};

static void (*const unflatteners[])(const void*, struct message*) = {
	[MT_SETUP] = unflatten_setup,
	[MT_READY] = unflatten_ready,
	[MT_SHUTDOWN] = unflatten_shutdown,
	[MT_MOVEREL] = unflatten_moverel,
	[MT_MOVEABS] = unflatten_moveabs,
	[MT_MOUSEPOS] = unflatten_mousepos,
	[MT_CLICKEVENT] = unflatten_clickevent,
	[MT_KEYEVENT] = unflatten_keyevent,
	[MT_GETCLIPBOARD] = unflatten_getclipboard,
	[MT_SETCLIPBOARD] = unflatten_setclipboard,
	[MT_LOGMSG] = unflatten_logmsg,
	[MT_SETBRIGHTNESS] = unflatten_setbrightness,
};

static void flatten_message(const struct message* msg, void* buf)
{
	flatteners[msg->type](msg, buf);
}

static void unflatten_message(const void* buf, struct message* msg)
{
	unflatteners[msg->type](buf, msg);
}

void unparse_message(const struct message* msg, struct partsend* ps)
{
	size_t plsize;
	void* p;

	assert(msg->type < ARR_LEN(payload_sizes));

	plsize = payload_sizes[msg->type];

	ps->msg_len = MSGHDR_SIZE + plsize + msg->extra.len;

	ps->msgbuf = xmalloc(ps->msg_len);

	p = ps->msgbuf;
	*(msgtype_t*)p = htonl(msg->type);
	p += sizeof(msgtype_t);
	*(uint32_t*)p = htonl(msg->extra.len);
	p += sizeof(uint32_t);

	assert(p - ps->msgbuf == MSGHDR_SIZE);

	flatten_message(msg, p);
	p += plsize;

	memcpy(p, msg->extra.buf, msg->extra.len);
}

int drain_msgbuf(int fd, struct partsend* ps)
{
	ssize_t status;

	while (ps->bytes_sent < ps->msg_len) {
		status = write(fd, ps->msgbuf + ps->bytes_sent,
		               ps->msg_len - ps->bytes_sent);
		if (status < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 0;
			else
				return -errno;
		}
		ps->bytes_sent += status;
	}

	xfree(ps->msgbuf);
	ps->msgbuf = NULL;
	ps->msg_len = 0;
	ps->bytes_sent = 0;

	return 1;
}

int write_message(int fd, const struct message* msg)
{
	int status;
	struct partsend ps = {
		.msgbuf = NULL,
		.msg_len = 0,
		.bytes_sent = 0,
	};

	unparse_message(msg, &ps);

	do {
		status = drain_msgbuf(fd, &ps);
		if (status < 0)
			return status;
	} while (!status);

	return 0;
}

/*
 * Wire format:
 *
 *   message type (u32)
 *   extra-buf length (u32)
 *   main payload (length determined by type)
 *   extra-buf payload
 */

int fill_msgbuf(int fd, struct partrecv* pr)
{
	ssize_t status;
	size_t msgsize, to_read;
	msgtype_t type;
	void* hdrbuf;

	while (pr->bytes_recvd < MSGHDR_SIZE) {
		status = read(fd, pr->hdrbuf + pr->bytes_recvd,
		              MSGHDR_SIZE - pr->bytes_recvd);
		if (status < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 0;
			else
				return -errno;
		} else if (status == 0) {
			return -EINVAL;
		}
		pr->bytes_recvd += status;
	}

	hdrbuf = &pr->hdrbuf;
	type = ntohl(*(msgtype_t*)hdrbuf);
	if (type >= ARR_LEN(payload_sizes))
		return -EINVAL;
	msgsize = MSGHDR_SIZE + payload_sizes[type]
		+ ntohl(*(uint32_t*)(hdrbuf + sizeof(msgtype_t)));

	to_read = msgsize - pr->bytes_recvd;

	if (!pr->plbuf) {
		assert(to_read == msgsize - MSGHDR_SIZE);
		pr->plbuf = xmalloc(msgsize - MSGHDR_SIZE);
	}

	while (to_read > 0) {
		status = read(fd, pr->plbuf + (pr->bytes_recvd - MSGHDR_SIZE), to_read);
		if (status < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 0;
			else
				return -errno;
		} else if (status == 0) {
			return -EINVAL;
		}
		pr->bytes_recvd += status;
		to_read -= status;
	}

	return 1;
}

void parse_message(struct partrecv* pr, struct message* msg)
{
	size_t plsize;
	void* p = pr->hdrbuf;

	msg->type = ntohl(*(msgtype_t*)p);
	p += sizeof(msgtype_t);
	msg->extra.len = ntohl(*(uint32_t*)p);
	p += sizeof(uint32_t);

	assert((char*)p - pr->hdrbuf == MSGHDR_SIZE);

	if (msg->type >= ARR_LEN(payload_sizes))
		abort();

	plsize = payload_sizes[msg->type];

	unflatten_message(pr->plbuf, msg);

	if (msg->extra.len) {
		msg->extra.buf = xmalloc(msg->extra.len);
		memcpy(msg->extra.buf, pr->plbuf + plsize, msg->extra.len);
	} else {
		msg->extra.buf = NULL;
	}

	xfree(pr->plbuf);
	pr->plbuf = NULL;
	pr->bytes_recvd = 0;
}

int read_message(int fd, struct message* msg)
{
	int status;
	struct partrecv pr = {
		.bytes_recvd = 0,
		.plbuf = NULL,
	};

	do {
		status = fill_msgbuf(fd, &pr);
		if (status < 0)
			return status;
	} while (!status);

	parse_message(&pr, msg);
	xfree(pr.plbuf);

	return 0;
}

struct message* new_message(msgtype_t type)
{
	struct message* msg = xmalloc(sizeof(*msg));

	msg->type = type;
	msg->extra.buf = NULL;
	msg->extra.len = 0;
	msg->next = NULL;

	return msg;
}

void free_message(struct message* msg)
{
	xfree(msg->extra.buf);
	xfree(msg);
}
