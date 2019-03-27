/* $FreeBSD$ */
/*	$NetBSD: citrus_csmapper.h,v 1.3 2013/06/24 17:28:35 joerg Exp $	*/

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

#ifndef _CITRUS_CSMAPPER_H_
#define _CITRUS_CSMAPPER_H_

#define _citrus_csmapper		_citrus_mapper
#define _citrus_csmapper_close		_citrus_mapper_close
#define _citrus_csmapper_convert	_citrus_mapper_convert
#define _citrus_csmapper_init_state	_citrus_mapper_init_state
#define _citrus_csmapper_get_state_size	_citrus_mapper_get_state_size
#define _citrus_csmapper_get_src_max	_citrus_mapper_get_src_max
#define _citrus_csmapper_get_dst_max	_citrus_mapper_get_dst_max

#define _CITRUS_CSMAPPER_F_PREVENT_PIVOT	0x00000001
__BEGIN_DECLS
int	 _citrus_csmapper_open(struct _citrus_csmapper *__restrict *__restrict,
	    const char *__restrict, const char *__restrict, uint32_t,
	    unsigned long *);
__END_DECLS

#endif
