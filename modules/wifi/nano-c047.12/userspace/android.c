/* Copyright (C) 2009 Nanoradio AB */

/* Implements glibc stuff which is missing from bionic library */
 
#ifdef ANDROID

#include "android.h"
#include <stdlib.h>     
#include <stdarg.h>
#include <errno.h>

void warn(const char *fmt, ...)
{
	va_list args;
    va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	printf(" (errno = %d)\n", errno);
}

void err(int eval, const char *fmt, ...)
{
	va_list args;
    va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	printf(" (errno = %d)\n", errno);
	exit(eval);
}

void errx(int eval, const char *fmt, ...)
{
	va_list args;
    va_start(args, fmt);
	vfprintf(stderr, fmt, args);
	va_end(args);
	exit(eval);
}

#endif /* ANDROID */
