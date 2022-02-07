/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * minimal stdio function definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_STDIO_H
#define _NOLIBC_STDIO_H

#include "std.h"
#include "arch.h"
#include "types.h"
#include "sys.h"
#include "stdlib.h"
#include "string.h"

#ifndef EOF
#define EOF (-1)
#endif

static __attribute__((unused))
int getchar(void)
{
	unsigned char ch;

	if (read(0, &ch, 1) <= 0)
		return EOF;
	return ch;
}

static __attribute__((unused))
int putchar(int c)
{
	unsigned char ch = c;

	if (write(1, &ch, 1) <= 0)
		return EOF;
	return ch;
}

static __attribute__((unused))
int puts(const char *s)
{
	size_t len = strlen(s);
	ssize_t ret;

	while (len > 0) {
		ret = write(1, s, len);
		if (ret <= 0)
			return EOF;
		s += ret;
		len -= ret;
	}
	return putchar('\n');
}

#endif /* _NOLIBC_STDIO_H */
