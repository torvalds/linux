/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/* nolibc.h
 * Copyright (C) 2017-2018 Willy Tarreau <w@1wt.eu>
 */

/*
 * This file is designed to be used as a libc alternative for minimal programs
 * with very limited requirements. It consists of a small number of syscall and
 * type definitions, and the minimal startup code needed to call main().
 * All syscalls are declared as static functions so that they can be optimized
 * away by the compiler when not used.
 *
 * Syscalls are split into 3 levels:
 *   - The lower level is the arch-specific syscall() definition, consisting in
 *     assembly code in compound expressions. These are called my_syscall0() to
 *     my_syscall6() depending on the number of arguments. The MIPS
 *     implementation is limited to 5 arguments. All input arguments are cast
 *     to a long stored in a register. These expressions always return the
 *     syscall's return value as a signed long value which is often either a
 *     pointer or the negated errno value.
 *
 *   - The second level is mostly architecture-independent. It is made of
 *     static functions called sys_<name>() which rely on my_syscallN()
 *     depending on the syscall definition. These functions are responsible
 *     for exposing the appropriate types for the syscall arguments (int,
 *     pointers, etc) and for setting the appropriate return type (often int).
 *     A few of them are architecture-specific because the syscalls are not all
 *     mapped exactly the same among architectures. For example, some archs do
 *     not implement select() and need pselect6() instead, so the sys_select()
 *     function will have to abstract this.
 *
 *   - The third level is the libc call definition. It exposes the lower raw
 *     sys_<name>() calls in a way that looks like what a libc usually does,
 *     takes care of specific input values, and of setting errno upon error.
 *     There can be minor variations compared to standard libc calls. For
 *     example the open() call always takes 3 args here.
 *
 * The errno variable is declared static and unused. This way it can be
 * optimized away if not used. However this means that a program made of
 * multiple C files may observe different errno values (one per C file). For
 * the type of programs this project targets it usually is not a problem. The
 * resulting program may even be reduced by defining the NOLIBC_IGNORE_ERRNO
 * macro, in which case the errno value will never be assigned.
 *
 * Some stdint-like integer types are defined. These are valid on all currently
 * supported architectures, because signs are enforced, ints are assumed to be
 * 32 bits, longs the size of a pointer and long long 64 bits. If more
 * architectures have to be supported, this may need to be adapted.
 *
 * Some macro definitions like the O_* values passed to open(), and some
 * structures like the sys_stat struct depend on the architecture.
 *
 * The definitions start with the architecture-specific parts, which are picked
 * based on what the compiler knows about the target architecture, and are
 * completed with the generic code. Since it is the compiler which sets the
 * target architecture, cross-compiling normally works out of the box without
 * having to specify anything.
 *
 * Finally some very common libc-level functions are provided. It is the case
 * for a few functions usually found in string.h, ctype.h, or stdlib.h. Nothing
 * is currently provided regarding stdio emulation.
 *
 * The macro NOLIBC is always defined, so that it is possible for a program to
 * check this macro to know if it is being built against and decide to disable
 * some features or simply not to include some standard libc files.
 *
 * Ideally this file should be split in multiple files for easier long term
 * maintenance, but provided as a single file as it is now, it's quite
 * convenient to use. Maybe some variations involving a set of includes at the
 * top could work.
 *
 * A simple static executable may be built this way :
 *      $ gcc -fno-asynchronous-unwind-tables -fno-ident -s -Os -nostdlib \
 *            -static -include nolibc.h -o hello hello.c -lgcc
 *
 * A very useful calling convention table may be found here :
 *      http://man7.org/linux/man-pages/man2/syscall.2.html
 *
 * This doc is quite convenient though not necessarily up to date :
 *      https://w3challs.com/syscalls/
 *
 */
#ifndef _NOLIBC_H
#define _NOLIBC_H

#include "std.h"
#include "arch.h"
#include "types.h"
#include "sys.h"

/* Used by programs to avoid std includes */
#define NOLIBC

static __attribute__((unused))
int tcsetpgrp(int fd, pid_t pid)
{
	return ioctl(fd, TIOCSPGRP, &pid);
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

/* some size-optimized reimplementations of a few common str* and mem*
 * functions. They're marked static, except memcpy() and raise() which are used
 * by libgcc on ARM, so they are marked weak instead in order not to cause an
 * error when building a program made of multiple files (not recommended).
 */

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

static __attribute__((unused))
void *memset(void *dst, int b, size_t len)
{
	char *p = dst;

	while (len--)
		*(p++) = b;
	return dst;
}

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
char *strcpy(char *dst, const char *src)
{
	char *ret = dst;

	while ((*dst++ = *src++));
	return ret;
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
int isdigit(int c)
{
	return (unsigned int)(c - '0') <= 9;
}

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

static __attribute__((unused))
const char *ltoa(long in)
{
	/* large enough for -9223372036854775808 */
	static char buffer[21];
	char       *pos = buffer + sizeof(buffer) - 1;
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

__attribute__((weak,unused))
void *memcpy(void *dst, const void *src, size_t len)
{
	return memmove(dst, src, len);
}

/* needed by libgcc for divide by zero */
__attribute__((weak,unused))
int raise(int signal)
{
	return kill(getpid(), signal);
}

/* Here come a few helper functions */

static __attribute__((unused))
void FD_ZERO(fd_set *set)
{
	memset(set, 0, sizeof(*set));
}

static __attribute__((unused))
void FD_SET(int fd, fd_set *set)
{
	if (fd < 0 || fd >= FD_SETSIZE)
		return;
	set->fd32[fd / 32] |= 1 << (fd & 31);
}

/* WARNING, it only deals with the 4096 first majors and 256 first minors */
static __attribute__((unused))
dev_t makedev(unsigned int major, unsigned int minor)
{
	return ((major & 0xfff) << 8) | (minor & 0xff);
}

#endif /* _NOLIBC_H */
