/* SPDX-License-Identifier: LGPL-2.1 OR MIT */
/*
 * formatted error message for NOLIBC
 * Copyright (C) 2026 Thomas Weißschuh <linux@weissschuh.net>
 */

/* make sure to include all global symbols */
#include "nolibc.h"

#ifndef _NOLIBC_ERR_H
#define _NOLIBC_ERR_H

#include "errno.h"
#include "stdarg.h"
#include "sys.h"

static __attribute__((unused))
void vwarn(const char *fmt, va_list args)
{
	fprintf(stderr, "%s: ", program_invocation_short_name);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, ": %m\n");
}

static __attribute__((unused))
void vwarnx(const char *fmt, va_list args)
{
	fprintf(stderr, "%s: ", program_invocation_short_name);
	vfprintf(stderr, fmt, args);
	fprintf(stderr, "\n");
}

static __attribute__((unused))
void warn(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vwarn(fmt, args);
	va_end(args);
}

static __attribute__((unused))
void warnx(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vwarnx(fmt, args);
	va_end(args);
}

static __attribute__((noreturn, unused))
void verr(int eval, const char *fmt, va_list args)
{
	vwarn(fmt, args);
	exit(eval);
}

static __attribute__((noreturn, unused))
void verrx(int eval, const char *fmt, va_list args)
{
	warnx(fmt, args);
	exit(eval);
}

static __attribute__((noreturn, unused))
void err(int eval, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	verr(eval, fmt, args);
	va_end(args);
}

static __attribute__((noreturn, unused))
void errx(int eval, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	verrx(eval, fmt, args);
	va_end(args);
}

#endif /* _NOLIBC_ERR_H */
