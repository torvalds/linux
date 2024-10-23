/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _TOOLS_LINUX_PANIC_H
#define _TOOLS_LINUX_PANIC_H

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static inline void panic(const char *fmt, ...)
{
	va_list argp;

	va_start(argp, fmt);
	vfprintf(stderr, fmt, argp);
	va_end(argp);
	exit(-1);
}

#endif
