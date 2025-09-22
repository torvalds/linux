/*	$OpenBSD: a64l.c,v 1.5 2005/08/08 08:05:36 espie Exp $ */
/*
 * Written by J.T. Conklin <jtc@netbsd.org>.
 * Public domain.
 */

#include <errno.h>
#include <stdlib.h>

long
a64l(const char *s)
{
	long value, digit, shift;
	int i;

	if (s == NULL) {
		errno = EINVAL;
		return(-1L);
	}

	value = 0;
	shift = 0;
	for (i = 0; *s && i < 6; i++, s++) {
		if (*s >= '.' && *s <= '/')
			digit = *s - '.';
		else if (*s >= '0' && *s <= '9')
			digit = *s - '0' + 2;
		else if (*s >= 'A' && *s <= 'Z')
			digit = *s - 'A' + 12;
		else if (*s >= 'a' && *s <= 'z')
			digit = *s - 'a' + 38;
		else {
			errno = EINVAL;
			return(-1L);
		}

		value |= digit << shift;
		shift += 6;
	}

	return(value);
}
