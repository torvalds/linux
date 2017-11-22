/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 1998, by Sun Microsystems, Inc.
 * All rights reserved.
 */

#ifndef	_SYS_SHA1_CONSTS_H
#define	_SYS_SHA1_CONSTS_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * as explained in sha1.c, loading 32-bit constants on a sparc is expensive
 * since it involves both a `sethi' and an `or'.  thus, we instead use `ld'
 * to load the constants from an array called `sha1_consts'.  however, on
 * intel (and perhaps other processors), it is cheaper to load the constant
 * directly.  thus, the c code in SHA1Transform() uses the macro SHA1_CONST()
 * which either expands to a constant or an array reference, depending on
 * the architecture the code is being compiled for.
 */

#include <sys/types.h>		/* uint32_t */

extern	const uint32_t	sha1_consts[];

#if	defined(__sparc)
#define	SHA1_CONST(x)		(sha1_consts[x])
#else
#define	SHA1_CONST(x)		(SHA1_CONST_ ## x)
#endif

/* constants, as provided in FIPS 180-1 */

#define	SHA1_CONST_0		0x5a827999U
#define	SHA1_CONST_1		0x6ed9eba1U
#define	SHA1_CONST_2		0x8f1bbcdcU
#define	SHA1_CONST_3		0xca62c1d6U

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SHA1_CONSTS_H */
