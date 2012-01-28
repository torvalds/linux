/*
 * usage.c
 *
 * Various reporting routines.
 * Originally copied from GIT source.
 *
 * Copyright (C) Linus Torvalds, 2005
 */
#include "util.h"

static void report(const char *prefix, const char *err, va_list params)
{
	char msg[1024];
	vsnprintf(msg, sizeof(msg), err, params);
	fprintf(stderr, " %s%s\n", prefix, msg);
}

static NORETURN void usage_builtin(const char *err)
{
	fprintf(stderr, "\n Usage: %s\n", err);
	exit(129);
}

static NORETURN void die_builtin(const char *err, va_list params)
{
	report(" Fatal: ", err, params);
	exit(128);
}

static void error_builtin(const char *err, va_list params)
{
	report(" Error: ", err, params);
}

static void warn_builtin(const char *warn, va_list params)
{
	report(" Warning: ", warn, params);
}

/* If we are in a dlopen()ed .so write to a global variable would segfault
 * (ugh), so keep things static. */
static void (*usage_routine)(const char *err) NORETURN = usage_builtin;
static void (*die_routine)(const char *err, va_list params) NORETURN = die_builtin;
static void (*error_routine)(const char *err, va_list params) = error_builtin;
static void (*warn_routine)(const char *err, va_list params) = warn_builtin;

void set_die_routine(void (*routine)(const char *err, va_list params) NORETURN)
{
	die_routine = routine;
}

void usage(const char *err)
{
	usage_routine(err);
}

void die(const char *err, ...)
{
	va_list params;

	va_start(params, err);
	die_routine(err, params);
	va_end(params);
}

int error(const char *err, ...)
{
	va_list params;

	va_start(params, err);
	error_routine(err, params);
	va_end(params);
	return -1;
}

void warning(const char *warn, ...)
{
	va_list params;

	va_start(params, warn);
	warn_routine(warn, params);
	va_end(params);
}
