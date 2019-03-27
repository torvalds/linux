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

#ifndef CMAP_H
#define	CMAP_H

#include <limits.h>
#include <stdbool.h>
#include <wchar.h>

struct cmapnode {
	wint_t		cmn_from;
	wint_t		cmn_to;
	struct cmapnode	*cmn_left;
	struct cmapnode	*cmn_right;
};

struct cmap {
#define	CM_CACHE_SIZE	128
	wint_t		cm_cache[CM_CACHE_SIZE];
	bool		cm_havecache;
	struct cmapnode	*cm_root;
#define	CM_DEF_SELF	-2
	wint_t		cm_def;
	wint_t		cm_min;
	wint_t		cm_max;
};

struct cmap *	cmap_alloc(void);
bool		cmap_add(struct cmap *, wint_t, wint_t);
wint_t		cmap_lookup_hard(struct cmap *, wint_t);
void		cmap_cache(struct cmap *);
wint_t		cmap_default(struct cmap *, wint_t);

static __inline wint_t
cmap_lookup(struct cmap *cm, wint_t from)
{

	if (from < CM_CACHE_SIZE && cm->cm_havecache)
		return (cm->cm_cache[from]);
	return (cmap_lookup_hard(cm, from));
}

static __inline wint_t
cmap_min(struct cmap *cm)
{

	return (cm->cm_min);
}

static __inline wint_t
cmap_max(struct cmap *cm)
{

	return (cm->cm_max);
}

#endif
