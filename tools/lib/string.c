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
 * This routine returns 0 iff the first character is one of 'Yy1Nn0'.
 * Otherwise it will return -EINVAL.  Value pointed to by res is
 * updated upon finding a match.
 */
int strtobool(const char *s, bool *res)
{
	switch (s[0]) {
	case 'y':
	case 'Y':
	case '1':
		*res = true;
		break;
	case 'n':
	case 'N':
	case '0':
		*res = false;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}
