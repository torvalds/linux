#include <sys/cdefs.h>
#if 0
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: infinity.c,v 1.2 1998/11/14 19:31:02 christos Exp $");
#endif /* LIBC_SCCS and not lint */
#endif
__FBSDID("$FreeBSD$");

/* infinity.c */

#include <math.h>

/* bytes for +Infinity on powerpc */
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
	{0xff, 0xc0, 0, 0}
#else
	{ 0, 0, 0xc0, 0xff }
#endif
};

