/*
 * Copyright (c) 1993 Martin Birgmeier
 * All rights reserved.
 *
 * You may redistribute unmodified or modified versions of this source
 * code provided that the above copyright notice and this and the
 * following conditions are retained.
 *
 * This software is provided ``as is'', and comes with no warranties
 * of any kind. I shall in no event be liable for anything that happens
 * to anyone/anything when using this software.
 *
 *	$OpenBSD: rand48.h,v 1.6 2015/09/14 13:30:17 guenther Exp $
 */

#ifndef _RAND48_H_
#define _RAND48_H_

#include <stdlib.h>

__BEGIN_HIDDEN_DECLS
extern unsigned short __rand48_seed[3];
extern unsigned short __rand48_mult[3];
extern unsigned short __rand48_add;

void		__dorand48(unsigned short[3]);
extern int	__rand48_deterministic;
__END_HIDDEN_DECLS

#define	RAND48_SEED_0	(0x330e)
#define	RAND48_SEED_1	(0xabcd)
#define	RAND48_SEED_2	(0x1234)
#define	RAND48_MULT_0	(0xe66d)
#define	RAND48_MULT_1	(0xdeec)
#define	RAND48_MULT_2	(0x0005)
#define	RAND48_ADD	(0x000b)

#endif /* _RAND48_H_ */
