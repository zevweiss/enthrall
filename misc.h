#ifndef MISC_H
#define MISC_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <sys/select.h>

#include "types.h"

#ifdef __GNUC__
#define __printf(a, b) __attribute__((format(printf, a, b)))
#else
#define __printf(a, b)
#endif

#ifndef __GLIBC_PREREQ
#define __GLIBC_PREREQ(...) 0
#endif

#define ARR_LEN(a) (sizeof(a) / sizeof(a[0]))

#define LOOKUP(key, table) (assert(key < ARR_LEN(table)), \
                            assert(key >= 0), \
                            table[key])

static inline void* xmalloc(size_t s)
{
	void* p = malloc(s);
	if (!p) {
		perror("malloc");
		abort();
	}
	return p;
}

static inline void* xcalloc(size_t s)
{
	void* p = calloc(1, s);
	if (!p) {
		perror("calloc");
		abort();
	}
	return p;
}

static inline void* xrealloc(void* p, size_t s)
{
	void* newp = realloc(p, s);
	if (!newp) {
		perror("realloc");
		abort();
	}
	return newp;
}

static inline char* xstrdup(const char* str)
{
	char* s = strdup(str);
	if (!s) {
		perror("strdup");
		abort();
	}
	return s;
}

static inline char* xvasprintf(const char* fmt, va_list va)
{
	char* ret;
	int status = vasprintf(&ret, fmt, va);

	if (status < 0) {
		perror("xvasprintf");
		abort();
	}

	return ret;
}

static inline char* xasprintf(const char* fmt, ...)
{
	va_list va;
	char* ret;

	va_start(va, fmt);
	ret = xvasprintf(fmt, va);
	va_end(va);

	return ret;
}

static inline void xfree(void* p)
{
	free(p);
}

static inline void fdset_add(int fd, fd_set* set, int* nfds)
{
	FD_SET(fd, set);
	if (fd >= *nfds)
		*nfds = fd + 1;
}

enum {
	LL_BUG = 1,
	LL_ERROR,
	LL_WARN,
	LL_INFO,
	LL_VERBOSE,
	LL_DEBUG,
	LL_DEBUG2,
};

__printf(1, 2) void initerr(const char* fmt, ...);
#define initdie(...) do { initerr(__VA_ARGS__); exit(1); } while (0)

__printf(2, 3) void mlog(unsigned int level, const char* fmt, ...);

#define errlog(fmt, ...) mlog(LL_ERROR, "Error: "fmt, ##__VA_ARGS__)
#define warn(fmt, ...) mlog(LL_WARN, "Warning: "fmt, ##__VA_ARGS__)
#define bug(fmt, ...) mlog(LL_BUG, "BUG (please report!): "fmt, ##__VA_ARGS__)
#define info(fmt, ...) mlog(LL_INFO, fmt, ##__VA_ARGS__)
#define vinfo(fmt, ...) mlog(LL_VERBOSE, fmt, ##__VA_ARGS__)
#define debug(fmt, ...) mlog(LL_DEBUG, fmt, ##__VA_ARGS__)
#define debug2(fmt, ...) mlog(LL_DEBUG2, fmt, ##__VA_ARGS__)

typedef enum {
	MASTER,
	REMOTE,
} opmode_t;

extern opmode_t opmode;

extern struct node* focused_node;

static inline int is_remote(struct node* n)
{
	return !!n->remote;
}

static inline int is_master(struct node* n)
{
	return !n->remote;
}

void run_remote(void);
void set_loglevel(unsigned int level);
extern struct msgchan stdio_msgchan;

void send_keyevent(struct remote* rmt, keycode_t kc, pressrel_t pr);
void send_moverel(struct remote* rmt, int32_t dx, int32_t dy);
void send_clickevent(struct remote* rmt, mousebutton_t button, pressrel_t pr);
void send_setbrightness(struct remote* rmt, float f);

int get_fd_nonblock(int fd);
void set_fd_nonblock(int fd, int nb);
void set_fd_cloexec(int fd, int ce);

char* expand_word(const char* wd);

struct kvpair* flatten_kvmap(const struct kvmap* kvm, u_int* numpairs);
struct kvmap* unflatten_kvmap(const struct kvpair* pairs, u_int numpairs);

void set_clipboard_from_buf(const void* buf, size_t len);

void explicit_bzero(void* p, size_t n);

/*
 * Make a function to produce a gamma value for index 'idx' in a gamma table
 * by scaling (by compressing/expanding the X axis and interpolating, not just
 * multiplying the absolute value along the Y axis, so as to preserve relative
 * RGB curves) the values in the given 'from' array ('numents' long) 'scale'.
 *
 * This is done this way because OSX uses floats (with a "CGGammaValue"
 * typedef) for its gamma tables, whereas X11 uses unsigned shorts.
 */
#define MAKE_GAMMA_SCALE_FN(name, type, defloat) \
	type name(type* from, int numents, int idx, float scale) \
	{ \
		float f_idx, f_loidx, frac; \
		int loidx; \
		float lo, hi; \
		\
		assert(scale >= 0.0); \
		\
		f_idx = (float)idx * scale; \
		\
		frac = modff(f_idx, &f_loidx); \
		loidx = lrintf(f_loidx); \
		\
		if (loidx >= numents - 1) \
			return from[numents-1]; \
		\
		lo = (float)(from[loidx]); \
		hi = (float)(from[loidx+1]); \
		\
		return defloat(lo + (frac * (hi - lo))); \
	}

#endif /* MISC_H */
