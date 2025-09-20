// SPDX-License-Identifier: GPL-2.0-only
/*
 * From lib/cmdline.c
 */
#include <stdlib.h>

#if __has_attribute(__fallthrough__)
# define fallthrough                    __attribute__((__fallthrough__))
#else
# define fallthrough                    do {} while (0)  /* fallthrough */
#endif

unsigned long long memparse(const char *ptr, char **retptr)
{
	char *endptr;	/* local pointer to end of parsed string */

	unsigned long long ret = strtoll(ptr, &endptr, 0);

	switch (*endptr) {
	case 'E':
	case 'e':
		ret <<= 10;
		fallthrough;
	case 'P':
	case 'p':
		ret <<= 10;
		fallthrough;
	case 'T':
	case 't':
		ret <<= 10;
		fallthrough;
	case 'G':
	case 'g':
		ret <<= 10;
		fallthrough;
	case 'M':
	case 'm':
		ret <<= 10;
		fallthrough;
	case 'K':
	case 'k':
		ret <<= 10;
		endptr++;
		fallthrough;
	default:
		break;
	}

	if (retptr)
		*retptr = endptr;

	return ret;
}
