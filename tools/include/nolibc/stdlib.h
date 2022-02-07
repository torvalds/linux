/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * stdlib function definitions for NOLIBC
 * Copyright (C) 2017-2021 Willy Tarreau <w@1wt.eu>
 */

#ifndef _NOLIBC_STDLIB_H
#define _NOLIBC_STDLIB_H

#include "std.h"
#include "arch.h"
#include "types.h"
#include "sys.h"

/*
 * As much as possible, please keep functions alphabetically sorted.
 */

static __attribute__((unused))
long atol(const char *s)
{
	unsigned long ret = 0;
	unsigned long d;
	int neg = 0;

	if (*s == '-') {
		neg = 1;
		s++;
	}

	while (1) {
		d = (*s++) - '0';
		if (d > 9)
			break;
		ret *= 10;
		ret += d;
	}

	return neg ? -ret : ret;
}

static __attribute__((unused))
int atoi(const char *s)
{
	return atol(s);
}

/* performs the opposite of atol() using a user-fed buffer. The buffer must be
 * at least 21 bytes long (large enough for "-9223372036854775808").
 */
static __attribute__((unused))
const char *ltoa_r(long in, char *buffer)
{
	char       *pos = buffer + 21 - 1;
	int         neg = in < 0;
	unsigned long n = neg ? -in : in;

	*pos-- = '\0';
	do {
		*pos-- = '0' + n % 10;
		n /= 10;
		if (pos < buffer)
			return pos + 1;
	} while (n);

	if (neg)
		*pos-- = '-';
	return pos + 1;
}

/* performs the opposite of atol() using a statically allocated buffer */
static __attribute__((unused))
const char *ltoa(long in)
{
	/* large enough for -9223372036854775808 */
	static char buffer[21];
	return ltoa_r(in, buffer);
}

static __attribute__((unused))
int msleep(unsigned int msecs)
{
	struct timeval my_timeval = { msecs / 1000, (msecs % 1000) * 1000 };

	if (sys_select(0, 0, 0, 0, &my_timeval) < 0)
		return (my_timeval.tv_sec * 1000) +
			(my_timeval.tv_usec / 1000) +
			!!(my_timeval.tv_usec % 1000);
	else
		return 0;
}

/* This one is not marked static as it's needed by libgcc for divide by zero */
__attribute__((weak,unused))
int raise(int signal)
{
	return kill(getpid(), signal);
}

static __attribute__((unused))
unsigned int sleep(unsigned int seconds)
{
	struct timeval my_timeval = { seconds, 0 };

	if (sys_select(0, 0, 0, 0, &my_timeval) < 0)
		return my_timeval.tv_sec + !!my_timeval.tv_usec;
	else
		return 0;
}

static __attribute__((unused))
int tcsetpgrp(int fd, pid_t pid)
{
	return ioctl(fd, TIOCSPGRP, &pid);
}

#endif /* _NOLIBC_STDLIB_H */
