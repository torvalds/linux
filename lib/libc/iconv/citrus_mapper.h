/* $FreeBSD$ */
/* $NetBSD: citrus_mapper.h,v 1.3 2003/07/12 15:39:19 tshiozak Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2003 Citrus Project,
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _CITRUS_MAPPER_H_
#define _CITRUS_MAPPER_H_

struct _citrus_mapper_area;
struct _citrus_mapper;
struct _citrus_mapper_ops;
struct _citrus_mapper_traits;

__BEGIN_DECLS
int	 _citrus_mapper_create_area(
	    struct _citrus_mapper_area *__restrict *__restrict,
	    const char *__restrict);
int	 _citrus_mapper_open(struct _citrus_mapper_area *__restrict,
	    struct _citrus_mapper *__restrict *__restrict,
	    const char *__restrict);
int	 _citrus_mapper_open_direct(
	    struct _citrus_mapper_area *__restrict,
	    struct _citrus_mapper *__restrict *__restrict,
	    const char *__restrict, const char *__restrict);
void	 _citrus_mapper_close(struct _citrus_mapper *);
void	 _citrus_mapper_set_persistent(struct _citrus_mapper * __restrict);
__END_DECLS

#include "citrus_mapper_local.h"

/* return values of _citrus_mapper_convert */
#define _CITRUS_MAPPER_CONVERT_SUCCESS		(0)
#define _CITRUS_MAPPER_CONVERT_NONIDENTICAL	(1)
#define _CITRUS_MAPPER_CONVERT_SRC_MORE		(2)
#define _CITRUS_MAPPER_CONVERT_DST_MORE		(3)
#define _CITRUS_MAPPER_CONVERT_ILSEQ		(4)
#define _CITRUS_MAPPER_CONVERT_FATAL		(5)

/*
 * _citrus_mapper_convert:
 *	convert an index.
 *	- if the converter supports M:1 converter, the function may return
 *	  _CITRUS_MAPPER_CONVERT_SRC_MORE and the storage pointed by dst
 *	  may be unchanged in this case, although the internal status of
 *	  the mapper is affected.
 *	- if the converter supports 1:N converter, the function may return
 *	  _CITRUS_MAPPER_CONVERT_DST_MORE. In this case, the contiguous
 *	  call of this function ignores src and changes the storage pointed
 *	  by dst.
 *	- if the converter supports M:N converter, the function may behave
 *	  the combination of the above.
 *
 */
static __inline int
_citrus_mapper_convert(struct _citrus_mapper * __restrict cm,
    _citrus_index_t * __restrict dst, _citrus_index_t src,
    void * __restrict ps)
{

	return ((*cm->cm_ops->mo_convert)(cm, dst, src, ps));
}

/*
 * _citrus_mapper_init_state:
 *	initialize the state.
 */
static __inline void
_citrus_mapper_init_state(struct _citrus_mapper * __restrict cm)
{

	(*cm->cm_ops->mo_init_state)();
}

/*
 * _citrus_mapper_get_state_size:
 *	get the size of state storage.
 */
static __inline size_t
_citrus_mapper_get_state_size(struct _citrus_mapper * __restrict cm)
{

	return (cm->cm_traits->mt_state_size);
}

/*
 * _citrus_mapper_get_src_max:
 *	get the maximum number of suspended sources.
 */
static __inline size_t
_citrus_mapper_get_src_max(struct _citrus_mapper * __restrict cm)
{

	return (cm->cm_traits->mt_src_max);
}

/*
 * _citrus_mapper_get_dst_max:
 *	get the maximum number of suspended destinations.
 */
static __inline size_t
_citrus_mapper_get_dst_max(struct _citrus_mapper * __restrict cm)
{

	return (cm->cm_traits->mt_dst_max);
}

#endif
