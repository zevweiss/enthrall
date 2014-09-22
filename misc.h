#ifndef MISC_H
#define MISC_H

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>

#define ARR_LEN(a) (sizeof(a) / sizeof(a[0]))

#define LOOKUP(key, table) (assert(key < ARR_LEN(table)), table[key])

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

#endif /* MISC_H */
