/* $FreeBSD$ */
/* $NetBSD: citrus_db.h,v 1.2 2008/02/09 14:56:20 junyoung Exp $ */

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

#ifndef _CITRUS_DB_H_
#define _CITRUS_DB_H_

#include "citrus_db_factory.h"

struct _citrus_db;
struct _citrus_db_locator {
	uint32_t	dl_hashval;
	size_t		dl_offset;
};

__BEGIN_DECLS
int	 _citrus_db_open(struct _citrus_db **, struct _citrus_region *,
	    const char *, _citrus_db_hash_func_t, void *);
void	 _citrus_db_close(struct _citrus_db *);
int	 _citrus_db_lookup(struct _citrus_db *, struct _citrus_region *,
	    struct _citrus_region *, struct _citrus_db_locator *);
int	 _citrus_db_lookup_by_string(struct _citrus_db *, const char *,
	    struct _citrus_region *, struct _citrus_db_locator *);
int	 _citrus_db_lookup8_by_string(struct _citrus_db *, const char *,
	    uint8_t *, struct _citrus_db_locator *);
int	 _citrus_db_lookup16_by_string(struct _citrus_db *, const char *,
	    uint16_t *, struct _citrus_db_locator *);
int	 _citrus_db_lookup32_by_string(struct _citrus_db *, const char *,
	    uint32_t *, struct _citrus_db_locator *);
int	 _citrus_db_lookup_string_by_string(struct _citrus_db *, const char *,
	    const char **, struct _citrus_db_locator *);
int	 _citrus_db_get_number_of_entries(struct _citrus_db *);
int	 _citrus_db_get_entry(struct _citrus_db *, int,
	    struct _citrus_region *, struct _citrus_region *);
__END_DECLS

static __inline void
_citrus_db_locator_init(struct _citrus_db_locator *dl)
{

	dl->dl_hashval = 0;
	dl->dl_offset = 0;
}

#endif
