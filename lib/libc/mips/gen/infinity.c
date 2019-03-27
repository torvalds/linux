/*
 * infinity.c
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <math.h>

/* bytes for +Infinity on a 387 */
const union __infinity_un __infinity = { 
#if BYTE_ORDER == BIG_ENDIAN
	{ 0x7f, 0xf0, 0, 0, 0, 0, 0, 0 }
#else
	{ 0, 0, 0, 0, 0, 0, 0xf0, 0x7f }
#endif
};

/* bytes for NaN */
const union __nan_un __nan = {
#if BYTE_ORDER == BIG_ENDIAN
	{0x7f, 0xa0, 0, 0}
#else
	{ 0, 0, 0xa0, 0x7f }
#endif
};
