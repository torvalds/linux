/* $FreeBSD$ */
/* $NetBSD: citrus_prop.h,v 1.5 2011/05/23 14:52:32 joerg Exp $ */

/*-
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c)2006 Citrus Project,
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
 *
 */

#ifndef _CITRUS_PROP_H_
#define _CITRUS_PROP_H_

typedef enum {
	_CITRUS_PROP_BOOL = 0,
	_CITRUS_PROP_STR  = 1,
	_CITRUS_PROP_CHR  = 2,
	_CITRUS_PROP_NUM  = 3,
} _citrus_prop_type_t;

typedef struct _citrus_prop_hint_t _citrus_prop_hint_t;

#define _CITRUS_PROP_CB0_T(_func_, _type_) \
typedef int (*_citrus_prop_##_func_##_cb_func_t) \
    (void * __restrict, const char *, _type_); \
typedef struct { \
	_citrus_prop_##_func_##_cb_func_t func; \
} _citrus_prop_##_func_##_cb_t;
_CITRUS_PROP_CB0_T(boolean, int)
_CITRUS_PROP_CB0_T(str, const char *)
#undef _CITRUS_PROP_CB0_T

#define _CITRUS_PROP_CB1_T(_func_, _type_) \
typedef int (*_citrus_prop_##_func_##_cb_func_t) \
    (void * __restrict, const char *, _type_, _type_); \
typedef struct { \
	_citrus_prop_##_func_##_cb_func_t func; \
} _citrus_prop_##_func_##_cb_t;
_CITRUS_PROP_CB1_T(chr, int)
_CITRUS_PROP_CB1_T(num, uint64_t)
#undef _CITRUS_PROP_CB1_T

struct _citrus_prop_hint_t {
	const char *name;
	_citrus_prop_type_t type;
#define _CITRUS_PROP_CB_T_OPS(_name_) \
	_citrus_prop_##_name_##_cb_t _name_
	union {
		_CITRUS_PROP_CB_T_OPS(boolean);
		_CITRUS_PROP_CB_T_OPS(str);
		_CITRUS_PROP_CB_T_OPS(chr);
		_CITRUS_PROP_CB_T_OPS(num);
	} cb;
};

#define _CITRUS_PROP_HINT_BOOL(name, cb) \
    { name, _CITRUS_PROP_BOOL, { .boolean = { cb } } }
#define _CITRUS_PROP_HINT_STR(name, cb) \
    { name, _CITRUS_PROP_STR, { .str = { cb } } }
#define _CITRUS_PROP_HINT_CHR(name, cb) \
    { name, _CITRUS_PROP_CHR, { .chr = { cb } } }
#define _CITRUS_PROP_HINT_NUM(name, cb) \
    { name, _CITRUS_PROP_NUM, { .num = { cb } } }
#define _CITRUS_PROP_HINT_END \
    { .name = NULL }

__BEGIN_DECLS
int	 _citrus_prop_parse_variable(const _citrus_prop_hint_t * __restrict,
	    void * __restrict, const void *, size_t);
__END_DECLS

#endif /* !_CITRUS_PROP_H_ */
