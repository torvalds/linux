/* $FreeBSD$ */
/* $NetBSD: citrus_db_factory.h,v 1.3 2008/02/09 14:56:20 junyoung Exp $ */

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

#ifndef _CITRUS_DB_FACTORY_H_
#define _CITRUS_DB_FACTORY_H_

struct _citrus_db_factory;
typedef uint32_t (*_citrus_db_hash_func_t)(struct _citrus_region *);

__BEGIN_DECLS
int	 _citrus_db_factory_create(struct _citrus_db_factory **,
	    _citrus_db_hash_func_t, void *);
void	 _citrus_db_factory_free(struct _citrus_db_factory *);
int	 _citrus_db_factory_add(struct _citrus_db_factory *,
	    struct _citrus_region *, int, struct _citrus_region *, int);
int	 _citrus_db_factory_add_by_string(struct _citrus_db_factory *,
	    const char *, struct _citrus_region *, int);
int	 _citrus_db_factory_add8_by_string(struct _citrus_db_factory *,
	    const char *, uint8_t);
int	 _citrus_db_factory_add16_by_string(struct _citrus_db_factory *,
	    const char *, uint16_t);
int	 _citrus_db_factory_add32_by_string(struct _citrus_db_factory *,
	    const char *, uint32_t);
int	 _citrus_db_factory_add_string_by_string(struct _citrus_db_factory *,
	    const char *, const char *);
size_t	 _citrus_db_factory_calc_size(struct _citrus_db_factory *);
int	 _citrus_db_factory_serialize(struct _citrus_db_factory *,
	    const char *, struct _citrus_region *);
__END_DECLS

#endif
