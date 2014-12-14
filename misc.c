#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <wordexp.h>

#include "misc.h"
#include "kvmap.h"
#include "platform.h"

/* Return the current value of fd's O_NONBLOCK flag */
int get_fd_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL);

	if (flags == -1) {
		perror("fcntl");
		abort();
	}

	return !!(flags & O_NONBLOCK);
}

/* Set fd's O_NONBLOCK flag to 'nb' */
void set_fd_nonblock(int fd, int nb)
{
	int flags = fcntl(fd, F_GETFL);

	if (flags == -1) {
		perror("fcntl");
		abort();
	}

	if (nb)
		flags |= O_NONBLOCK;
	else
		flags &= ~O_NONBLOCK;

	if (fcntl(fd, F_SETFL, flags)) {
		perror("fcntl");
		abort();
	}
}

/* Set fd's FD_CLOEXEC flag to 'ce' */
void set_fd_cloexec(int fd, int ce)
{
	int flags = fcntl(fd, F_GETFD);

	if (flags == -1) {
		perror("fcntl");
		abort();
	}

	if (ce)
		flags |= FD_CLOEXEC;
	else
		flags &= ~FD_CLOEXEC;

	if (fcntl(fd, F_SETFD, flags)) {
		perror("fcntl");
		abort();
	}
}

/*
 * Perform shell-like word expansion on a string (so we can have conveniences
 * like "~" to refer to home directories in paths in config files).
 */
char* expand_word(const char* wd)
{
	char* ret;
	wordexp_t exp;

	/*
	 * OSX's wordexp(3) sadly just ignores these flags, but I guess we
	 * might as well try...
	 */
	if (wordexp(wd, &exp, WRDE_NOCMD|WRDE_UNDEF) || exp.we_wordc != 1)
		return NULL;

	ret = xstrdup(exp.we_wordv[0]);

	wordfree(&exp);

	return ret;
}

struct kvmflatten_ctx {
	char* buf;
	size_t len;
};

/* kvmap_foreach() callback used for flattening a kvmap into a buffer */
static void flattencb(const char* key, const char* value, void* arg)
{
	struct kvmflatten_ctx* ctx = arg;
	size_t klen = strlen(key), vlen = strlen(value);
	size_t newlen = ctx->len + klen + 1 + vlen + 1;

	ctx->buf = xrealloc(ctx->buf, newlen);
	strcpy(ctx->buf + ctx->len, key);
	ctx->len += klen + 1;
	strcpy(ctx->buf + ctx->len, value);
	ctx->len += vlen + 1;
}

/*
 * Turn a kvmap into a flat buffer of concatenated NUL-terminated strings
 * (e.g. "key1\0value1\0key2\0value2\0"), returning the total combined length
 * of the buffer in *len.
 */
void* flatten_kvmap(const struct kvmap* kvm, size_t* len)
{
	struct kvmflatten_ctx ctx = { .buf = NULL, .len = 0, };

	kvmap_foreach(kvm, flattencb, &ctx);

	*len = ctx.len;

	return ctx.buf;
}

/*
 * Non-POSIX.1-2008-compliant systems (e.g. Mac OS X 10.6) may not have
 * strnlen(3), sadly.
 */
#ifdef NEED_COMPAT_STRNLEN
size_t strnlen(const char *s, size_t maxlen)
{
	size_t i;

	for (i = 0; i < maxlen && s[i]; i++);

	return i;
}
#endif

/* Inverse of flatten_kvmap(). */
struct kvmap* unflatten_kvmap(const void* buf, size_t len)
{
	const char* k;
	const char* v;
	const char* p = buf;
	size_t klen, vlen, remaining = len;
	struct kvmap* kvm = new_kvmap();

	while (remaining > 0) {
		k = p;
		klen = strnlen(p, remaining);
		if (klen == remaining)
			goto err;
		p += klen + 1;
		remaining -= klen + 1;

		v = p;
		vlen = strnlen(p, remaining);
		if (vlen == remaining)
			goto err;
		p += vlen + 1;
		remaining -= vlen + 1;

		kvmap_put(kvm, k, v);
	}

	return kvm;

err:
	destroy_kvmap(kvm);
	return NULL;
}

/*
 * Small helper to tack a NUL-terminator onto a buffer (presumably from a
 * message) containing a string and set the clipboard from it.
 */
void set_clipboard_from_buf(const void* buf, size_t len)
{
	char* tmp;

	/*
	 * extra intermediate malloc()ed area just to tack on the NUL
	 * terminator here is a bit inefficient...
	 */
	tmp = xmalloc(len + 1);
	memcpy(tmp, buf, len);
	tmp[len] = '\0';
	set_clipboard_text(tmp);
	xfree(tmp);
}
