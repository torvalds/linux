/*
 * Written by J.T. Conklin <jtc@NetBSD.org>.
 * Public domain.
 */

#if 0
#if defined(LIBC_SCCS) && !defined(lint)
__RCSID("$NetBSD: l64a.c,v 1.13 2003/07/26 19:24:54 salo Exp $");
#endif /* not lint */
#endif

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <stdint.h>
#include <stdlib.h>

char *
l64a(long value)
{
	static char buf[7];

	(void)l64a_r(value, buf, sizeof(buf));
	return (buf);
}

int
l64a_r(long value, char *buffer, int buflen)
{
	static const char chars[] =
	    "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
	uint32_t v;

	v = value;
	while (buflen-- > 0) {
		if (v == 0) {
			*buffer = '\0';
			return (0);
		}
		*buffer++ = chars[v & 0x3f];
		v >>= 6;
	}
	return (-1);
}
