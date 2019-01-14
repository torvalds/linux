/* SPDX-License-Identifier: LGPL-2.1 */
/*
 * Copyright (C) 2010 Red Hat Inc, Steven Rostedt <srostedt@redhat.com>
 *
 */
#ifndef __UTIL_H
#define __UTIL_H

#include <ctype.h>

/* Can be overridden */
void warning(const char *fmt, ...);
void pr_stat(const char *fmt, ...);
void vpr_stat(const char *fmt, va_list ap);

/* Always available */
void __warning(const char *fmt, ...);
void __pr_stat(const char *fmt, ...);

void __vwarning(const char *fmt, ...);
void __vpr_stat(const char *fmt, ...);

#define min(x, y) ({				\
	typeof(x) _min1 = (x);			\
	typeof(y) _min2 = (y);			\
	(void) (&_min1 == &_min2);		\
	_min1 < _min2 ? _min1 : _min2; })

static inline char *strim(char *string)
{
	char *ret;

	if (!string)
		return NULL;
	while (*string) {
		if (!isspace(*string))
			break;
		string++;
	}
	ret = string;

	string = ret + strlen(ret) - 1;
	while (string > ret) {
		if (!isspace(*string))
			break;
		string--;
	}
	string[1] = 0;

	return ret;
}

static inline int has_text(const char *text)
{
	if (!text)
		return 0;

	while (*text) {
		if (!isspace(*text))
			return 1;
		text++;
	}

	return 0;
}

#endif
