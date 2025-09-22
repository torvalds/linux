/*	$OpenBSD: ctzdi2.c,v 1.1 2022/12/14 23:50:31 jsg Exp $	*/

/*
 * Public domain.
 * Written by Dale Rahn.
 */

#include <lib/libkern/libkern.h>

/*
 * ffsl -- vax ffs instruction with long arg
 */

#ifdef __LP64__
static int
ffsl(long mask)
{
	int bit;
	unsigned long r = mask;
	static const signed char t[16] = {
		-60, 1, 2, 1,
		  3, 1, 2, 1,
		  4, 1, 2, 1,
		  3, 1, 2, 1
	};

	bit = 0;
	if (!(r & 0xffffffff)) {
		bit += 32;
		r >>= 32;
	}
	if (!(r & 0xffff)) {
		bit += 16;
		r >>= 16;
	}
	if (!(r & 0xff)) {
		bit += 8;
		r >>= 8;
	}
	if (!(r & 0xf)) {
		bit += 4;
		r >>= 4;
	}

	return (bit + t[ r & 0xf ]);
}
#else
static int
ffsl(long mask)
{
	return ffs(mask);
}
#endif

int
__ctzdi2(long mask)
{
	if (mask == 0)
		return 0;
	return ffsl(mask) - 1;
}
