/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SKC_LINUX_STRING_H
#define _SKC_LINUX_STRING_H

#include <string.h>

/* Copied from lib/string.c */
static inline char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}

static inline char *strim(char *s)
{
	size_t size;
	char *end;

	size = strlen(s);
	if (!size)
		return s;

	end = s + size - 1;
	while (end >= s && isspace(*end))
		end--;
	*(end + 1) = '\0';

	return skip_spaces(s);
}

#endif
