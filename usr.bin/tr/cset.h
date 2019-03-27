/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2004 Tim J. Robbins.
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
 * $FreeBSD$
 */

#ifndef CSET_H
#define	CSET_H

#include <stdbool.h>
#include <wchar.h>
#include <wctype.h>

struct csnode {
	wchar_t		csn_min;
	wchar_t		csn_max;
	struct csnode	*csn_left;
	struct csnode	*csn_right;
};

struct csclass {
	wctype_t	csc_type;
	bool		csc_invert;
	struct csclass	*csc_next;
};

struct cset {
#define	CS_CACHE_SIZE	256
	bool		cs_cache[CS_CACHE_SIZE];
	bool		cs_havecache;
	struct csclass	*cs_classes;
	struct csnode	*cs_root;
	bool		cs_invert;
};

bool			cset_addclass(struct cset *, wctype_t, bool);
struct cset *		cset_alloc(void);
bool 			cset_add(struct cset *, wchar_t);
void			cset_invert(struct cset *);
bool			cset_in_hard(struct cset *, wchar_t);
void			cset_cache(struct cset *);

static __inline bool
cset_in(struct cset *cs, wchar_t ch)
{

	if (ch < CS_CACHE_SIZE && cs->cs_havecache)
		return (cs->cs_cache[ch]);
	return (cset_in_hard(cs, ch));
}

#endif	/* CSET_H */
