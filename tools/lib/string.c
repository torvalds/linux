// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/tools/lib/string.c
 *
 *  Copied from linux/lib/string.c, where it is:
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  More specifically, the first copied function was strtobool, which
 *  was introduced by:
 *
 *  d0f1fed29e6e ("Add a strtobool function matching semantics of existing in kernel equivalents")
 *  Author: Jonathan Cameron <jic23@cam.ac.uk>
 */

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/compiler.h>

/**
 * memdup - duplicate region of memory
 *
 * @src: memory region to duplicate
 * @len: memory region length
 */
void *memdup(const void *src, size_t len)
{
	void *p = malloc(len);

	if (p)
		memcpy(p, src, len);

	return p;
}

/**
 * strtobool - convert common user inputs into boolean values
 * @s: input string
 * @res: result
 *
 * This routine returns 0 iff the first character is one of 'Yy1Nn0', or
 * [oO][NnFf] for "on" and "off". Otherwise it will return -EINVAL.  Value
 * pointed to by res is updated upon finding a match.
 */
int strtobool(const char *s, bool *res)
{
	if (!s)
		return -EINVAL;

	switch (s[0]) {
	case 'y':
	case 'Y':
	case '1':
		*res = true;
		return 0;
	case 'n':
	case 'N':
	case '0':
		*res = false;
		return 0;
	case 'o':
	case 'O':
		switch (s[1]) {
		case 'n':
		case 'N':
			*res = true;
			return 0;
		case 'f':
		case 'F':
			*res = false;
			return 0;
		default:
			break;
		}
	default:
		break;
	}

	return -EINVAL;
}

/**
 * strlcpy - Copy a C-string into a sized buffer
 * @dest: Where to copy the string to
 * @src: Where to copy the string from
 * @size: size of destination buffer
 *
 * Compatible with *BSD: the result is always a valid
 * NUL-terminated string that fits in the buffer (unless,
 * of course, the buffer size is zero). It does not pad
 * out the result like strncpy() does.
 *
 * If libc has strlcpy() then that version will override this
 * implementation:
 */
size_t __weak strlcpy(char *dest, const char *src, size_t size)
{
	size_t ret = strlen(src);

	if (size) {
		size_t len = (ret >= size) ? size - 1 : ret;
		memcpy(dest, src, len);
		dest[len] = '\0';
	}
	return ret;
}

/**
 * skip_spaces - Removes leading whitespace from @str.
 * @str: The string to be stripped.
 *
 * Returns a pointer to the first non-whitespace character in @str.
 */
char *skip_spaces(const char *str)
{
	while (isspace(*str))
		++str;
	return (char *)str;
}
