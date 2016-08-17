/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_SHA1_H
#define	_SYS_SHA1_H

#include <sys/types.h>		/* for uint_* */

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * NOTE: n2rng (Niagara2 RNG driver) accesses the state field of
 * SHA1_CTX directly.  NEVER change this structure without verifying
 * compatibility with n2rng.  The important thing is that the state
 * must be in a field declared as uint32_t state[5].
 */
/* SHA-1 context. */
typedef struct 	{
	uint32_t state[5];	/* state (ABCDE) */
	uint32_t count[2];	/* number of bits, modulo 2^64 (msb first) */
	union 	{
		uint8_t		buf8[64];	/* undigested input */
		uint32_t	buf32[16];	/* realigned input */
	} buf_un;
} SHA1_CTX;

#define	SHA1_DIGEST_LENGTH 20

void SHA1Init(SHA1_CTX *);
void SHA1Update(SHA1_CTX *, const void *, size_t);
void SHA1Final(void *, SHA1_CTX *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_SHA1_H */
