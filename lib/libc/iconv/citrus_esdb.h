/* $FreeBSD$ */
/* $NetBSD: citrus_esdb.h,v 1.1 2003/06/25 09:51:32 tshiozak Exp $ */

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

#ifndef _CITRUS_ESDB_H_
#define _CITRUS_ESDB_H_

#include "citrus_types.h"

struct _citrus_esdb_charset {
	_citrus_csid_t			 ec_csid;
	char				*ec_csname;
};

struct _citrus_esdb {
	char				*db_encname;
	void				*db_variable;
	size_t				 db_len_variable;
	int				 db_num_charsets;
	struct	_citrus_esdb_charset	*db_charsets;
	int				 db_use_invalid;
	_citrus_wc_t			 db_invalid;
};

__BEGIN_DECLS
const char	*_citrus_esdb_alias(const char *, char *, size_t);
int		 _citrus_esdb_open(struct _citrus_esdb *, const char *);
void		 _citrus_esdb_close(struct _citrus_esdb *);
void		 _citrus_esdb_free_list(char **, size_t);
int		 _citrus_esdb_get_list(char ***, size_t *, bool);
__END_DECLS

#endif
