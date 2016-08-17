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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_NVPAIR_IMPL_H
#define	_NVPAIR_IMPL_H



#ifdef __cplusplus
extern "C" {
#endif

#include <sys/nvpair.h>

/*
 * The structures here provided for information and debugging purposes only
 * may be changed in the future.
 */

/*
 * implementation linked list for pre-packed data
 */
typedef struct i_nvp i_nvp_t;

struct i_nvp {
	union {
		uint64_t	_nvi_align;	/* ensure alignment */
		struct {
			i_nvp_t	*_nvi_next;	/* pointer to next nvpair */
			i_nvp_t	*_nvi_prev;	/* pointer to prev nvpair */
		} _nvi;
	} _nvi_un;
	nvpair_t nvi_nvp;			/* nvpair */
};
#define	nvi_next	_nvi_un._nvi._nvi_next
#define	nvi_prev	_nvi_un._nvi._nvi_prev

typedef struct {
	i_nvp_t		*nvp_list;	/* linked list of nvpairs */
	i_nvp_t		*nvp_last;	/* last nvpair */
	i_nvp_t		*nvp_curr;	/* current walker nvpair */
	nv_alloc_t	*nvp_nva;	/* pluggable allocator */
	uint32_t	nvp_stat;	/* internal state */
} nvpriv_t;

#ifdef	__cplusplus
}
#endif

#endif	/* _NVPAIR_IMPL_H */
