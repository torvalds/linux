/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BOOTCONFIG_LINUX_BOOTCONFIG_H
#define _BOOTCONFIG_LINUX_BOOTCONFIG_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>


#ifndef fallthrough
# define fallthrough
#endif

#define WARN_ON(cond)	\
	((cond) ? printf("Internal warning(%s:%d, %s): %s\n",	\
			__FILE__, __LINE__, __func__, #cond) : 0)

#define unlikely(cond)	(cond)

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

#define __init
#define __initdata

#include "../../../../include/linux/bootconfig.h"

#endif
