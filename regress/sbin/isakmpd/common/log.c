/*	$OpenBSD: log.c,v 1.1 2016/09/02 16:54:29 mikeb Exp $	 */

/*
 * Public Domain 2016 Mike Belopuhov
 */

#include <sys/types.h>
#include <stdlib.h>

void
log_debug(int cls, int level, const char *fmt, ...)
{
}

void
log_debug_buf(int cls, int level, const char *header, const u_int8_t *buf,
    size_t sz)
{
}

void
log_print(const char *fmt, ...)
{
}

void
log_error(const char *fmt, ...)
{
}

void
log_errorx(const char *fmt, ...)
{
}

void
log_fatal(const char *fmt, ...)
{
	exit(1);
}

void
log_fatalx(const char *fmt, ...)
{
	exit(1);
}
