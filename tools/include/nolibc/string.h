/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * string function definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_STRING_H
#define _NOLIBC_STRING_H

#include "std.h"

static void *malloc(size_t len);

/*
 * As much as possible, please keep functions alphabetically sorted.
 */

static __attribute__((unused))
int memcmp(const void *s1, const void *s2, size_t n)
{
	size_t ofs = 0;
	int c1 = 0;

	while (ofs < n && !(c1 = ((unsigned char *)s1)[ofs] - ((unsigned char *)s2)[ofs])) {
		ofs++;
	}
	return c1;
}

#ifndef NOLIBC_ARCH_HAS_MEMMOVE
/* might be ignored by the compiler without -ffreestanding, then found as
 * missing.
 */
__attribute__((weak,unused,section(".text.nolibc_memmove")))
void *memmove(void *dst, const void *src, size_t len)
{
	size_t dir, pos;

	pos = len;
	dir = -1;

	if (dst < src) {
		pos = -1;
		dir = 1;
	}

	while (len) {
		pos += dir;
		((char *)dst)[pos] = ((const char *)src)[pos];
		len--;
	}
	return dst;
}
#endif /* #ifndef NOLIBC_ARCH_HAS_MEMMOVE */

#ifndef NOLIBC_ARCH_HAS_MEMCPY
/* must be exported, as it's used by libgcc on ARM */
__attribute__((weak,unused,section(".text.nolibc_memcpy")))
void *memcpy(void *dst, const void *src, size_t len)
{
	size_t pos = 0;

	while (pos < len) {
		((char *)dst)[pos] = ((const char *)src)[pos];
		pos++;
	}
	return dst;
}
#endif /* #ifndef NOLIBC_ARCH_HAS_MEMCPY */

#ifndef NOLIBC_ARCH_HAS_MEMSET
/* might be ignored by the compiler without -ffreestanding, then found as
 * missing.
 */
__attribute__((weak,unused,section(".text.nolibc_memset")))
void *memset(void *dst, int b, size_t len)
{
	char *p = dst;

	while (len--) {
		/* prevent gcc from recognizing memset() here */
		__asm__ volatile("");
		*(p++) = b;
	}
	return dst;
}
#endif /* #ifndef NOLIBC_ARCH_HAS_MEMSET */

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
int strcmp(const char *a, const char *b)
{
	unsigned int c;
	int diff;

	while (!(diff = (unsigned char)*a++ - (c = (unsigned char)*b++)) && c)
		;
	return diff;
}

static __attribute__((unused))
char *strcpy(char *dst, const char *src)
{
	char *ret = dst;

	while ((*dst++ = *src++));
	return ret;
}

/* this function is only used with arguments that are not constants or when
 * it's not known because optimizations are disabled. Note that gcc 12
 * recognizes an strlen() pattern and replaces it with a jump to strlen(),
 * thus itself, hence the asm() statement below that's meant to disable this
 * confusing practice.
 */
static __attribute__((unused))
size_t strlen(const char *str)
{
	size_t len;

	for (len = 0; str[len]; len++)
		__asm__("");
	return len;
}

/* do not trust __builtin_constant_p() at -O0, as clang will emit a test and
 * the two branches, then will rely on an external definition of strlen().
 */
#if defined(__OPTIMIZE__)
#define nolibc_strlen(x) strlen(x)
#define strlen(str) ({                          \
	__builtin_constant_p((str)) ?           \
		__builtin_strlen((str)) :       \
		nolibc_strlen((str));           \
})
#endif

static __attribute__((unused))
size_t strnlen(const char *str, size_t maxlen)
{
	size_t len;

	for (len = 0; (len < maxlen) && str[len]; len++);
	return len;
}

static __attribute__((unused))
char *strdup(const char *str)
{
	size_t len;
	char *ret;

	len = strlen(str);
	ret = malloc(len + 1);
	if (__builtin_expect(ret != NULL, 1))
		memcpy(ret, str, len + 1);

	return ret;
}

static __attribute__((unused))
char *strndup(const char *str, size_t maxlen)
{
	size_t len;
	char *ret;

	len = strnlen(str, maxlen);
	ret = malloc(len + 1);
	if (__builtin_expect(ret != NULL, 1)) {
		memcpy(ret, str, len);
		ret[len] = '\0';
	}

	return ret;
}

static __attribute__((unused))
size_t strlcat(char *dst, const char *src, size_t size)
{
	size_t len;
	char c;

	for (len = 0; dst[len];	len++)
		;

	for (;;) {
		c = *src;
		if (len < size)
			dst[len] = c;
		if (!c)
			break;
		len++;
		src++;
	}

	return len;
}

static __attribute__((unused))
size_t strlcpy(char *dst, const char *src, size_t size)
{
	size_t len;
	char c;

	for (len = 0;;) {
		c = src[len];
		if (len < size)
			dst[len] = c;
		if (!c)
			break;
		len++;
	}
	return len;
}

static __attribute__((unused))
char *strncat(char *dst, const char *src, size_t size)
{
	char *orig = dst;

	while (*dst)
		dst++;

	while (size && (*dst = *src)) {
		src++;
		dst++;
		size--;
	}

	*dst = 0;
	return orig;
}

static __attribute__((unused))
int strncmp(const char *a, const char *b, size_t size)
{
	unsigned int c;
	int diff = 0;

	while (size-- &&
	       !(diff = (unsigned char)*a++ - (c = (unsigned char)*b++)) && c)
		;

	return diff;
}

static __attribute__((unused))
char *strncpy(char *dst, const char *src, size_t size)
{
	size_t len;

	for (len = 0; len < size; len++)
		if ((dst[len] = *src))
			src++;
	return dst;
}

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

/* make sure to include all global symbols */
#include "nolibc.h"

#endif /* _NOLIBC_STRING_H */
