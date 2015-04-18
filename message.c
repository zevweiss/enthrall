
#include <errno.h>
#include <stdint.h>
#include <arpa/inet.h>
#include <math.h>

#include <assert.h>

#include "misc.h"
#include "message.h"

/*
 * glibc's xdr routines use char*, BSD/OSX use void*.  Solarish appears to use
 * an old K&R-style declaration without specific types; usage at call-sites
 * (e.g. usr/src/lib/libnsl/rpc/svc_vc.c) seems to be "one of each".
 */
#ifdef __GLIBC__
typedef char* xdrrec_ptr_t;
#else
typedef void* xdrrec_ptr_t;
#endif

/* Dummy callback for xdrrec_create(3) that just counts bytes */
static int xdr_tracksize(xdrrec_ptr_t handle, xdrrec_ptr_t buf, int len)
{
	int* pos = (int*)handle;
	*pos += len;
	return len;
}

/*
 * HACK: the XDR API doesn't seem to have any direct way to answer the
 * question "how many bytes will this thing take up after encoding?" (e.g. for
 * determining how large of a buffer to use with xdrmem_create(3)).  So here
 * we hack one together using xdrrec_create(3) and a special callback that
 * just counts bytes without actually doing anything.
 *
 * Note that the size it returns isn't precise (due to this note from xdr(3):
 * "Warning: this XDR stream implements an intermediate record stream.
 * Therefore there are additional bytes in the stream to provide record
 * boundary information."), but it does provide an upper bound on encoded size
 * and is thus usable for memory-allocation purposes.
 */
static size_t xdr_msgbody_len(const struct message* msg)
{
	XDR xdrs;
	int pos = 0;

	xdrrec_create(&xdrs, 0, 0, (char*)&pos, NULL, xdr_tracksize);
	xdrs.x_op = XDR_ENCODE;

	if (!xdr_msgbody(&xdrs, (struct msgbody*)&msg->body)) {
		fprintf(stderr, "xdr_msgbody() failed in xdr_msgbody_len()\n");
		abort();
	}

	if (!xdrrec_endofrecord(&xdrs, 1)) {
		fprintf(stderr, "xdrrec_endofrecord() failed in xdr_msgbody_len()\n");
		abort();
	}

	xdr_destroy(&xdrs);

	return pos;
}

/*
 * Wire protocol
 * =============
 *
 * The raw transmitted form of each message has two top-level components:
 *
 *  - a length descriptor (u32, network order)
 *  - an XDR message body
 *
 * The length descriptor contains the length of only the XDR message body
 * itself; it does not include the four bytes that it itself takes up.
 *
 * Why not just have it be straight XDR?  Because the XDR API does not, as far
 * as I can tell, offer any interface that would integrate nicely with an
 * async-IO/O_NOBLOCK/select(2)-based event-loop IO scheme.
 */

/* Flatten a message struct into a wire-protocol format byte array */
void unparse_message(const struct message* msg, struct partsend* ps)
{
	XDR xdrs;
	unsigned int pos;
	size_t xdrlen = xdr_msgbody_len(msg);

	ps->len = xdrlen + MSGHDR_SIZE;
	ps->buf = xmalloc(ps->len);
	*(uint32_t*)ps->buf = htonl(xdrlen);

	xdrmem_create(&xdrs, ps->buf + MSGHDR_SIZE, xdrlen, XDR_ENCODE);

	if (!xdr_msgbody(&xdrs, (struct msgbody*)&msg->body)) {
		fprintf(stderr, "xdr_msgbody() failed in unparse_message()\n");
		abort();
	}

	pos = xdr_getpos(&xdrs);

	/* This is probably always the case.  See comment on xdr_msgbody_len(). */
	if (pos != xdrlen) {
		assert(pos < xdrlen);
		ps->len = pos + MSGHDR_SIZE;
		ps->buf = xrealloc(ps->buf, ps->len);
		*(uint32_t*)ps->buf = htonl(pos);
	}

	xdr_destroy(&xdrs);
}

/*
 * Drain data in the given partsend buffer out via the given file descriptor.
 * Returns 1 if the buffer is successfully emptied, 0 if data remains and
 * further writes to the file descriptor would block, and negative on error.
 */
int drain_msgbuf(int fd, struct partsend* ps)
{
	ssize_t status;

	while (ps->bytes_sent < ps->len) {
		status = write(fd, ps->buf + ps->bytes_sent,
		               ps->len - ps->bytes_sent);
		if (status < 0) {
			if (errno == EAGAIN || errno == EWOULDBLOCK)
				return 0;
			else
				return -errno;
		}
		ps->bytes_sent += status;
	}

	xfree(ps->buf);
	ps->buf = NULL;
	ps->len = 0;
	ps->bytes_sent = 0;

	return 1;
}

/*
 * Try to read a message (or the remainder of a partially-received one) into
 * the given partrecv buffer from the given file descriptor.  Returns 1 if the
 * buffer has been filled with a complete message, 0 if the message is
 * incomplete and further reads on the file descriptor would block, and
 * negative on error.
 */
int fill_msgbuf(int fd, struct partrecv* pr)
{
	ssize_t status, to_read;
	uint32_t msgsize;
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

	hdrbuf = pr->hdrbuf;
	msgsize = ntohl(*(uint32_t*)hdrbuf);

	to_read = msgsize;

	if (!pr->plbuf) {
		assert(pr->bytes_recvd == MSGHDR_SIZE);
		pr->plbuf = xmalloc(msgsize);
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

/*
 * "Unflatten" the wire-protocol byte array in the given partrecv buffer into
 * a message struct, returning zero on success and negative on error.
 */
int parse_message(struct partrecv* pr, struct message* msg)
{
	XDR xdrs;

	xdrmem_create(&xdrs, pr->plbuf, pr->bytes_recvd - MSGHDR_SIZE, XDR_DECODE);
	if (!xdr_msgbody(&xdrs, &msg->body)) {
		fprintf(stderr, "xdr_msgbody() failed in parse_message() (invalid input?)\n");
		return -1;
	}
	msg->from_xdr = 1;
	xdr_destroy(&xdrs);

	xfree(pr->plbuf);
	pr->plbuf = NULL;
	pr->bytes_recvd = 0;

	return 0;
}

/* Allocate and return a new message of the given type. */
struct message* new_message(msgtype_t type)
{
	struct message* msg = xmalloc(sizeof(*msg));

	msg->body.type = type;
	msg->next = NULL;
	msg->from_xdr = 0;

	return msg;
}

/*
 * Free any dynamically-allocated members of the given message, but not the
 * message itself (e.g. for stack-allocated messages).
 */
void free_msgbody(struct message* msg)
{
	int i;

	if (msg->from_xdr) {
		xdr_free((xdrproc_t)xdr_msgbody, (caddr_t)&msg->body);
	} else {
		switch (msg->body.type) {
		case MT_SETCLIPBOARD:
			xfree(MB(msg, setclipboard).text);
			break;

		case MT_SETUP:
			for (i = 0; i < MB(msg, setup).params.params_len; i++) {
				xfree(MB(msg, setup).params.params_val[i].key);
				xfree(MB(msg, setup).params.params_val[i].value);
			}
			xfree(MB(msg, setup).params.params_val);
			break;

		case MT_LOGMSG:
			xfree(MB(msg, logmsg).msg);
			break;

		default:
			break;
		}
	}
}

/* Free all memory associated with the given message. */
void free_message(struct message* msg)
{
	free_msgbody(msg);
	xfree(msg);
}
