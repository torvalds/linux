/* $FreeBSD$ */
/* $NetBSD: citrus_mapper_local.h,v 1.2 2008/02/09 14:56:20 junyoung Exp $ */

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

#ifndef _CITRUS_MAPPER_LOCAL_H_
#define _CITRUS_MAPPER_LOCAL_H_

#define _CITRUS_MAPPER_GETOPS_FUNC_BASE(_n_)				\
int _n_(struct _citrus_mapper_ops *)
#define _CITRUS_MAPPER_GETOPS_FUNC(_n_)					\
_CITRUS_MAPPER_GETOPS_FUNC_BASE(_citrus_##_n_##_mapper_getops)

#define _CITRUS_MAPPER_DECLS(_m_)					\
static int	 _citrus_##_m_##_mapper_init				\
		    (struct _citrus_mapper_area *__restrict,		\
		    struct _citrus_mapper * __restrict,			\
		    const char * __restrict, const void * __restrict,	\
		    size_t, struct _citrus_mapper_traits * __restrict,	\
		    size_t);						\
static void	 _citrus_##_m_##_mapper_uninit(				\
		    struct _citrus_mapper *);				\
static int	 _citrus_##_m_##_mapper_convert				\
		    (struct _citrus_mapper * __restrict,		\
		    _citrus_index_t * __restrict, _citrus_index_t,	\
		    void * __restrict);					\
static void	 _citrus_##_m_##_mapper_init_state			\
		    (void);

#define _CITRUS_MAPPER_DEF_OPS(_m_)					\
extern struct _citrus_mapper_ops _citrus_##_m_##_mapper_ops;		\
struct _citrus_mapper_ops _citrus_##_m_##_mapper_ops = {		\
	/* mo_init */		&_citrus_##_m_##_mapper_init,		\
	/* mo_uninit */		&_citrus_##_m_##_mapper_uninit,		\
	/* mo_convert */	&_citrus_##_m_##_mapper_convert,	\
	/* mo_init_state */	&_citrus_##_m_##_mapper_init_state	\
}

typedef _CITRUS_MAPPER_GETOPS_FUNC_BASE((*_citrus_mapper_getops_t));
typedef	int (*_citrus_mapper_init_t)(
    struct _citrus_mapper_area *__restrict,
    struct _citrus_mapper *__restrict, const char *__restrict,
    const void *__restrict, size_t,
    struct _citrus_mapper_traits * __restrict, size_t);
typedef void (*_citrus_mapper_uninit_t)(struct _citrus_mapper *);
typedef int (*_citrus_mapper_convert_t)(struct _citrus_mapper * __restrict,
    _citrus_index_t * __restrict, _citrus_index_t, void * __restrict);
typedef void (*_citrus_mapper_init_state_t)(void);

struct _citrus_mapper_ops {
	_citrus_mapper_init_t			 mo_init;
	_citrus_mapper_uninit_t			 mo_uninit;
	_citrus_mapper_convert_t		 mo_convert;
	_citrus_mapper_init_state_t		 mo_init_state;
};

struct _citrus_mapper_traits {
	/* version 0x00000001 */
	size_t					 mt_state_size;
	size_t					 mt_src_max;
	size_t					 mt_dst_max;
};

struct _citrus_mapper {
	struct _citrus_mapper_ops		*cm_ops;
	void					*cm_closure;
	_citrus_module_t			 cm_module;
	struct _citrus_mapper_traits		*cm_traits;
	_CITRUS_HASH_ENTRY(_citrus_mapper)	 cm_entry;
	int					 cm_refcount;
	char					*cm_key;
};
#endif
