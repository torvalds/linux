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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SPA_CHECKSUM_H
#define	_SPA_CHECKSUM_H

#include <sys/types.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Each block has a 256-bit checksum -- strong enough for cryptographic hashes.
 */
typedef struct zio_cksum {
	uint64_t	zc_word[4];
} zio_cksum_t;

#define	ZIO_SET_CHECKSUM(zcp, w0, w1, w2, w3)	\
{						\
	(zcp)->zc_word[0] = w0;			\
	(zcp)->zc_word[1] = w1;			\
	(zcp)->zc_word[2] = w2;			\
	(zcp)->zc_word[3] = w3;			\
}

#define	ZIO_CHECKSUM_EQUAL(zc1, zc2) \
	(0 == (((zc1).zc_word[0] - (zc2).zc_word[0]) | \
	((zc1).zc_word[1] - (zc2).zc_word[1]) | \
	((zc1).zc_word[2] - (zc2).zc_word[2]) | \
	((zc1).zc_word[3] - (zc2).zc_word[3])))

#define	ZIO_CHECKSUM_IS_ZERO(zc) \
	(0 == ((zc)->zc_word[0] | (zc)->zc_word[1] | \
	(zc)->zc_word[2] | (zc)->zc_word[3]))

#define	ZIO_CHECKSUM_BSWAP(zcp)					\
{								\
	(zcp)->zc_word[0] = BSWAP_64((zcp)->zc_word[0]);	\
	(zcp)->zc_word[1] = BSWAP_64((zcp)->zc_word[1]);	\
	(zcp)->zc_word[2] = BSWAP_64((zcp)->zc_word[2]);	\
	(zcp)->zc_word[3] = BSWAP_64((zcp)->zc_word[3]);	\
}

#ifdef	__cplusplus
}
#endif

#endif
