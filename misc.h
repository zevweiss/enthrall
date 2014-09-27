#ifndef MISC_H
#define MISC_H

#include <stdlib.h>
#include <stdio.h>
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

static inline void xfree(void* p)
{
	free(p);
}

extern struct config* config;
extern struct remote* active_remote;

int read_all(int fd, void* buf, size_t len);
int write_all(int fd, const void* buf, size_t len);

void disconnect_remote(struct remote* rmt, connstate_t state);

#endif /* MISC_H */
