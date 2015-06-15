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
	struct kvpair* pairs;
	u_int numpairs;
};

/* kvmap_foreach() callback used for flattening a kvmap into a pair array */
static void flattencb(const char* key, const char* value, void* arg)
{
	struct kvmflatten_ctx* ctx = arg;

	ctx->pairs = xrealloc(ctx->pairs, (ctx->numpairs + 1) * sizeof(*ctx->pairs));
	ctx->pairs[ctx->numpairs].key = xstrdup(key);
	ctx->pairs[ctx->numpairs].value = xstrdup(value);
	ctx->numpairs++;
}

/*
 * Turn a kvmap into an array of struct kvpairs, returning the number of
 * kvpairs in *numpairs.
 */
struct kvpair* flatten_kvmap(const struct kvmap* kvm, u_int* numpairs)
{
	struct kvmflatten_ctx ctx = { .pairs = NULL, .numpairs = 0, };

	kvmap_foreach(kvm, flattencb, &ctx);

	*numpairs = ctx.numpairs;

	return ctx.pairs;
}

/* Inverse of flatten_kvmap(). */
struct kvmap* unflatten_kvmap(const struct kvpair* pairs, u_int numpairs)
{
	int i;
	struct kvmap* kvm = new_kvmap();

	for (i = 0; i < numpairs; i++)
		kvmap_put(kvm, pairs[i].key, pairs[i].value);

	return kvm;
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

/*
 * Adapted from Ted Unangst's public-domain explicit_bzero.c (originally in
 * OpenBSD libc I think, now also elsewhere).
 *
 * Indirect bzero through a volatile pointer to hopefully avoid dead-store
 * optimisation eliminating the call.
 */
static void (* volatile enthrall_bzero)(void *, size_t) = bzero;

void explicit_bzero(void* p, size_t n)
{
	enthrall_bzero(p, n);
}
