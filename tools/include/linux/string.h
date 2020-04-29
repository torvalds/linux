/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_STRING_H_
#define _TOOLS_LINUX_STRING_H_

#include <linux/types.h>	/* for size_t */
#include <string.h>

void *memdup(const void *src, size_t len);

char **argv_split(const char *str, int *argcp);
void argv_free(char **argv);

int strtobool(const char *s, bool *res);

/*
 * glibc based builds needs the extern while uClibc doesn't.
 * However uClibc headers also define __GLIBC__ hence the hack below
 */
#if defined(__GLIBC__) && !defined(__UCLIBC__)
// pragma diagnostic was introduced in gcc 4.6
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wredundant-decls"
#endif
extern size_t strlcpy(char *dest, const char *src, size_t size);
#if __GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6)
#pragma GCC diagnostic pop
#endif
#endif

char *str_error_r(int errnum, char *buf, size_t buflen);

char *strreplace(char *s, char old, char new);

/**
 * strstarts - does @str start with @prefix?
 * @str: string to examine
 * @prefix: prefix to look for.
 */
static inline bool strstarts(const char *str, const char *prefix)
{
	return strncmp(str, prefix, strlen(prefix)) == 0;
}

extern char * __must_check skip_spaces(const char *);

extern char *strim(char *);

#endif /* _TOOLS_LINUX_STRING_H_ */
