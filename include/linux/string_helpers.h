#ifndef _LINUX_STRING_HELPERS_H_
#define _LINUX_STRING_HELPERS_H_

#include <linux/types.h>

/* Descriptions of the types of units to
 * print in */
enum string_size_units {
	STRING_UNITS_10,	/* use powers of 10^3 (standard SI) */
	STRING_UNITS_2,		/* use binary powers of 2^10 */
};

int string_get_size(u64 size, enum string_size_units units,
		    char *buf, int len);

#endif
