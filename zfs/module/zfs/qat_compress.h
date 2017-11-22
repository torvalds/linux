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

#ifndef	_SYS_QAT_COMPRESS_H
#define	_SYS_QAT_COMPRESS_H

#if defined(_KERNEL) && defined(HAVE_QAT)
#include <sys/zio.h>
#include "cpa.h"
#include "dc/cpa_dc.h"

typedef enum qat_compress_dir {
	QAT_COMPRESS = 0,
	QAT_DECOMPRESS = 1,
} qat_compress_dir_t;

extern int qat_init(void);
extern void qat_fini(void);
extern boolean_t qat_use_accel(size_t s_len);
extern int qat_compress(qat_compress_dir_t dir, char *src, int src_len,
    char *dst, int dst_len, size_t *c_len);
#else
#define	CPA_STATUS_SUCCESS	0
#define	qat_init()
#define	qat_fini()
#define	qat_use_accel(s_len)	0
#define	qat_compress(dir, s, sl, d, dl, cl)	0
#endif

#endif /* _SYS_QAT_COMPRESS_H */
