/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __SUBCMD_UTIL_H
#define __SUBCMD_UTIL_H

#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <linux/compiler.h>

static inline void report(const char *prefix, const char *err, va_list params)
{
	char msg[1024];
	vsnprintf(msg, sizeof(msg), err, params);
	fprintf(stderr, " %s%s\n", prefix, msg);
}

static __noreturn inline void die(const char *err, ...)
{
	va_list params;

	va_start(params, err);
	report(" Fatal: ", err, params);
	va_end(params);
	exit(128);
}

#define zfree(ptr) ({ free(*ptr); *ptr = NULL; })

#define alloc_nr(x) (((x)+16)*3/2)

/*
 * Realloc the buffer pointed at by variable 'x' so that it can hold
 * at least 'nr' entries; the number of entries currently allocated
 * is 'alloc', using the standard growing factor alloc_nr() macro.
 *
 * DO NOT USE any expression with side-effect for 'x' or 'alloc'.
 */
#define ALLOC_GROW(x, nr, alloc) \
	do { \
		if ((nr) > alloc) { \
			if (alloc_nr(alloc) < (nr)) \
				alloc = (nr); \
			else \
				alloc = alloc_nr(alloc); \
			x = xrealloc((x), alloc * sizeof(*(x))); \
		} \
	} while(0)

static inline void *xrealloc(void *ptr, size_t size)
{
	void *ret = realloc(ptr, size);
	if (!ret)
		die("Out of memory, realloc failed");
	return ret;
}

#define astrcatf(out, fmt, ...)						\
({									\
	char *tmp = *(out);						\
	if (asprintf((out), "%s" fmt, tmp ?: "", ## __VA_ARGS__) == -1)	\
		die("asprintf failed");					\
	free(tmp);							\
})

static inline void astrcat(char **out, const char *add)
{
	char *tmp = *out;

	if (asprintf(out, "%s%s", tmp ?: "", add) == -1)
		die("asprintf failed");

	free(tmp);
}

#endif /* __SUBCMD_UTIL_H */
