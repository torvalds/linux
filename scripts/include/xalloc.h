/* SPDX-License-Identifier: GPL-2.0-only */

#ifndef XALLOC_H
#define XALLOC_H

#include <stdlib.h>
#include <string.h>

static inline void *xmalloc(size_t size)
{
	void *p = malloc(size);

	if (!p)
		exit(1);
	return p;
}

static inline void *xcalloc(size_t nmemb, size_t size)
{
	void *p = calloc(nmemb, size);

	if (!p)
		exit(1);
	return p;
}

static inline void *xrealloc(void *p, size_t size)
{
	p = realloc(p, size);
	if (!p)
		exit(1);
	return p;
}

static inline char *xstrdup(const char *s)
{
	char *p = strdup(s);

	if (!p)
		exit(1);
	return p;
}

static inline char *xstrndup(const char *s, size_t n)
{
	char *p = strndup(s, n);

	if (!p)
		exit(1);
	return p;
}

#endif /* XALLOC_H */
