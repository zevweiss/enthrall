#ifndef MISC_H
#define MISC_H

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>

#include "types.h"

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

void elog(const char* fmt, ...);

extern struct remote* active_remote;
void send_keyevent(keycode_t kc, pressrel_t pr);
void send_moverel(int32_t dx, int32_t dy);
void send_clickevent(mousebutton_t button, pressrel_t pr);

int read_all(int fd, void* buf, size_t len);
int write_all(int fd, const void* buf, size_t len);

void set_fd_nonblock(int fd, int nb);
void set_fd_cloexec(int fd, int ce);

char* expand_word(const char* wd);

#endif /* MISC_H */
