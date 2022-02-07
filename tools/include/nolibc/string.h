/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * string function definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_STRING_H
#define _NOLIBC_STRING_H

#include "std.h"

/*
 * As much as possible, please keep functions alphabetically sorted.
 */

static __attribute__((unused))
int memcmp(const void *s1, const void *s2, size_t n)
{
	size_t ofs = 0;
	char c1 = 0;

	while (ofs < n && !(c1 = ((char *)s1)[ofs] - ((char *)s2)[ofs])) {
		ofs++;
	}
	return c1;
}

static __attribute__((unused))
void *memmove(void *dst, const void *src, size_t len)
{
	ssize_t pos = (dst <= src) ? -1 : (long)len;
	void *ret = dst;

	while (len--) {
		pos += (dst <= src) ? 1 : -1;
		((char *)dst)[pos] = ((char *)src)[pos];
	}
	return ret;
}

/* must be exported, as it's used by libgcc on ARM */
__attribute__((weak,unused))
void *memcpy(void *dst, const void *src, size_t len)
{
	return memmove(dst, src, len);
}

static __attribute__((unused))
void *memset(void *dst, int b, size_t len)
{
	char *p = dst;

	while (len--)
		*(p++) = b;
	return dst;
}

static __attribute__((unused))
char *strchr(const char *s, int c)
{
	while (*s) {
		if (*s == (char)c)
			return (char *)s;
		s++;
	}
	return NULL;
}

static __attribute__((unused))
char *strcpy(char *dst, const char *src)
{
	char *ret = dst;

	while ((*dst++ = *src++));
	return ret;
}

/* this function is only used with arguments that are not constants */
static __attribute__((unused))
size_t nolibc_strlen(const char *str)
{
	size_t len;

	for (len = 0; str[len]; len++);
	return len;
}

#define strlen(str) ({                          \
	__builtin_constant_p((str)) ?           \
		__builtin_strlen((str)) :       \
		nolibc_strlen((str));           \
})

static __attribute__((unused))
char *strrchr(const char *s, int c)
{
	const char *ret = NULL;

	while (*s) {
		if (*s == (char)c)
			ret = s;
		s++;
	}
	return (char *)ret;
}

#endif /* _NOLIBC_STRING_H */
