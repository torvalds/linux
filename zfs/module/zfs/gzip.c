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



#include <sys/debug.h>
#include <sys/types.h>
#include "qat_compress.h"

#ifdef _KERNEL

#include <sys/systm.h>
#include <sys/zmod.h>

typedef size_t zlen_t;
#define	compress_func	z_compress_level
#define	uncompress_func	z_uncompress

#else /* _KERNEL */

#include <strings.h>
#include <zlib.h>

typedef uLongf zlen_t;
#define	compress_func	compress2
#define	uncompress_func	uncompress

#endif

size_t
gzip_compress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
	zlen_t dstlen = d_len;

	ASSERT(d_len <= s_len);

	/* check if hardware accelerator can be used */
	if (qat_use_accel(s_len)) {
		if (qat_compress(QAT_COMPRESS, s_start,
		    s_len, d_start, d_len, &dstlen) == CPA_STATUS_SUCCESS)
			return ((size_t)dstlen);
		/* if hardware compress fail, do it again with software */
	}

	if (compress_func(d_start, &dstlen, s_start, s_len, n) != Z_OK) {
		if (d_len != s_len)
			return (s_len);

		bcopy(s_start, d_start, s_len);
		return (s_len);
	}

	return ((size_t)dstlen);
}

/*ARGSUSED*/
int
gzip_decompress(void *s_start, void *d_start, size_t s_len, size_t d_len, int n)
{
	zlen_t dstlen = d_len;

	ASSERT(d_len >= s_len);

	/* check if hardware accelerator can be used */
	if (qat_use_accel(d_len)) {
		if (qat_compress(QAT_DECOMPRESS, s_start, s_len,
		    d_start, d_len, &dstlen) == CPA_STATUS_SUCCESS)
			return (0);
		/* if hardware de-compress fail, do it again with software */
	}

	if (uncompress_func(d_start, &dstlen, s_start, s_len) != Z_OK)
		return (-1);

	return (0);
}
