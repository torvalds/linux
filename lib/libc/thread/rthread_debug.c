/* $OpenBSD: rthread_debug.c,v 1.3 2017/09/05 02:40:54 guenther Exp $ */

/* PUBLIC DOMAIN: No Rights Reserved. Marco S Hyman <marc@snafu.org> */

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>

#include "rthread.h"

/*
 * format and send output to stderr if the given "level" is less than or
 * equal to the current debug level.   Messages with a level <= 0 will
 * always be printed.
 */
void
_rthread_debug(int level, const char *fmt, ...)
{
	if (_rthread_debug_level >= level) {
		va_list ap;
		va_start(ap, fmt);
		vdprintf(STDERR_FILENO, fmt, ap);
		va_end(ap);
	}
}
DEF_STRONG(_rthread_debug);

