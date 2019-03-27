/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1990, 1993, 1994
 *	The Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Mike Olson.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#if defined(LIBC_SCCS) && !defined(lint)
static char sccsid[] = "@(#)bt_debug.c	8.5 (Berkeley) 8/17/94";
#endif /* LIBC_SCCS and not lint */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <db.h>
#include "btree.h"

#ifdef DEBUG
/*
 * BT_DUMP -- Dump the tree
 *
 * Parameters:
 *	dbp:	pointer to the DB
 */
void
__bt_dump(DB *dbp)
{
	BTREE *t;
	PAGE *h;
	pgno_t i;
	char *sep;

	t = dbp->internal;
	(void)fprintf(stderr, "%s: pgsz %u",
	    F_ISSET(t, B_INMEM) ? "memory" : "disk", t->bt_psize);
	if (F_ISSET(t, R_RECNO))
		(void)fprintf(stderr, " keys %u", t->bt_nrecs);
#undef X
#define	X(flag, name) \
	if (F_ISSET(t, flag)) { \
		(void)fprintf(stderr, "%s%s", sep, name); \
		sep = ", "; \
	}
	if (t->flags != 0) {
		sep = " flags (";
		X(R_FIXLEN,	"FIXLEN");
		X(B_INMEM,	"INMEM");
		X(B_NODUPS,	"NODUPS");
		X(B_RDONLY,	"RDONLY");
		X(R_RECNO,	"RECNO");
		X(B_METADIRTY,"METADIRTY");
		(void)fprintf(stderr, ")\n");
	}
#undef X

	for (i = P_ROOT;
	    (h = mpool_get(t->bt_mp, i, MPOOL_IGNOREPIN)) != NULL; ++i)
		__bt_dpage(h);
}

/*
 * BT_DMPAGE -- Dump the meta page
 *
 * Parameters:
 *	h:	pointer to the PAGE
 */
void
__bt_dmpage(PAGE *h)
{
	BTMETA *m;
	char *sep;

	m = (BTMETA *)h;
	(void)fprintf(stderr, "magic %x\n", m->magic);
	(void)fprintf(stderr, "version %u\n", m->version);
	(void)fprintf(stderr, "psize %u\n", m->psize);
	(void)fprintf(stderr, "free %u\n", m->free);
	(void)fprintf(stderr, "nrecs %u\n", m->nrecs);
	(void)fprintf(stderr, "flags %u", m->flags);
#undef X
#define	X(flag, name) \
	if (m->flags & flag) { \
		(void)fprintf(stderr, "%s%s", sep, name); \
		sep = ", "; \
	}
	if (m->flags) {
		sep = " (";
		X(B_NODUPS,	"NODUPS");
		X(R_RECNO,	"RECNO");
		(void)fprintf(stderr, ")");
	}
}

/*
 * BT_DNPAGE -- Dump the page
 *
 * Parameters:
 *	n:	page number to dump.
 */
void
__bt_dnpage(DB *dbp, pgno_t pgno)
{
	BTREE *t;
	PAGE *h;

	t = dbp->internal;
	if ((h = mpool_get(t->bt_mp, pgno, MPOOL_IGNOREPIN)) != NULL)
		__bt_dpage(h);
}

/*
 * BT_DPAGE -- Dump the page
 *
 * Parameters:
 *	h:	pointer to the PAGE
 */
void
__bt_dpage(PAGE *h)
{
	BINTERNAL *bi;
	BLEAF *bl;
	RINTERNAL *ri;
	RLEAF *rl;
	indx_t cur, top;
	char *sep;

	(void)fprintf(stderr, "    page %u: (", h->pgno);
#undef X
#define	X(flag, name) \
	if (h->flags & flag) { \
		(void)fprintf(stderr, "%s%s", sep, name); \
		sep = ", "; \
	}
	sep = "";
	X(P_BINTERNAL,	"BINTERNAL")		/* types */
	X(P_BLEAF,	"BLEAF")
	X(P_RINTERNAL,	"RINTERNAL")		/* types */
	X(P_RLEAF,	"RLEAF")
	X(P_OVERFLOW,	"OVERFLOW")
	X(P_PRESERVE,	"PRESERVE");
	(void)fprintf(stderr, ")\n");
#undef X

	(void)fprintf(stderr, "\tprev %2u next %2u", h->prevpg, h->nextpg);
	if (h->flags & P_OVERFLOW)
		return;

	top = NEXTINDEX(h);
	(void)fprintf(stderr, " lower %3d upper %3d nextind %d\n",
	    h->lower, h->upper, top);
	for (cur = 0; cur < top; cur++) {
		(void)fprintf(stderr, "\t[%03d] %4d ", cur, h->linp[cur]);
		switch (h->flags & P_TYPE) {
		case P_BINTERNAL:
			bi = GETBINTERNAL(h, cur);
			(void)fprintf(stderr,
			    "size %03d pgno %03d", bi->ksize, bi->pgno);
			if (bi->flags & P_BIGKEY)
				(void)fprintf(stderr, " (indirect)");
			else if (bi->ksize)
				(void)fprintf(stderr,
				    " {%.*s}", (int)bi->ksize, bi->bytes);
			break;
		case P_RINTERNAL:
			ri = GETRINTERNAL(h, cur);
			(void)fprintf(stderr, "entries %03d pgno %03d",
				ri->nrecs, ri->pgno);
			break;
		case P_BLEAF:
			bl = GETBLEAF(h, cur);
			if (bl->flags & P_BIGKEY)
				(void)fprintf(stderr,
				    "big key page %u size %u/",
				    *(pgno_t *)bl->bytes,
				    *(u_int32_t *)(bl->bytes + sizeof(pgno_t)));
			else if (bl->ksize)
				(void)fprintf(stderr, "%.*s/",
				    bl->ksize, bl->bytes);
			if (bl->flags & P_BIGDATA)
				(void)fprintf(stderr,
				    "big data page %u size %u",
				    *(pgno_t *)(bl->bytes + bl->ksize),
				    *(u_int32_t *)(bl->bytes + bl->ksize +
				    sizeof(pgno_t)));
			else if (bl->dsize)
				(void)fprintf(stderr, "%.*s",
				    (int)bl->dsize, bl->bytes + bl->ksize);
			break;
		case P_RLEAF:
			rl = GETRLEAF(h, cur);
			if (rl->flags & P_BIGDATA)
				(void)fprintf(stderr,
				    "big data page %u size %u",
				    *(pgno_t *)rl->bytes,
				    *(u_int32_t *)(rl->bytes + sizeof(pgno_t)));
			else if (rl->dsize)
				(void)fprintf(stderr,
				    "%.*s", (int)rl->dsize, rl->bytes);
			break;
		}
		(void)fprintf(stderr, "\n");
	}
}
#endif

#ifdef STATISTICS
/*
 * BT_STAT -- Gather/print the tree statistics
 *
 * Parameters:
 *	dbp:	pointer to the DB
 */
void
__bt_stat(DB *dbp)
{
	extern u_long bt_cache_hit, bt_cache_miss, bt_pfxsaved, bt_rootsplit;
	extern u_long bt_sortsplit, bt_split;
	BTREE *t;
	PAGE *h;
	pgno_t i, pcont, pinternal, pleaf;
	u_long ifree, lfree, nkeys;
	int levels;

	t = dbp->internal;
	pcont = pinternal = pleaf = 0;
	nkeys = ifree = lfree = 0;
	for (i = P_ROOT;
	    (h = mpool_get(t->bt_mp, i, MPOOL_IGNOREPIN)) != NULL; ++i)
		switch (h->flags & P_TYPE) {
		case P_BINTERNAL:
		case P_RINTERNAL:
			++pinternal;
			ifree += h->upper - h->lower;
			break;
		case P_BLEAF:
		case P_RLEAF:
			++pleaf;
			lfree += h->upper - h->lower;
			nkeys += NEXTINDEX(h);
			break;
		case P_OVERFLOW:
			++pcont;
			break;
		}

	/* Count the levels of the tree. */
	for (i = P_ROOT, levels = 0 ;; ++levels) {
		h = mpool_get(t->bt_mp, i, MPOOL_IGNOREPIN);
		if (h->flags & (P_BLEAF|P_RLEAF)) {
			if (levels == 0)
				levels = 1;
			break;
		}
		i = F_ISSET(t, R_RECNO) ?
		    GETRINTERNAL(h, 0)->pgno :
		    GETBINTERNAL(h, 0)->pgno;
	}

	(void)fprintf(stderr, "%d level%s with %lu keys",
	    levels, levels == 1 ? "" : "s", nkeys);
	if (F_ISSET(t, R_RECNO))
		(void)fprintf(stderr, " (%u header count)", t->bt_nrecs);
	(void)fprintf(stderr,
	    "\n%u pages (leaf %u, internal %u, overflow %u)\n",
	    pinternal + pleaf + pcont, pleaf, pinternal, pcont);
	(void)fprintf(stderr, "%lu cache hits, %lu cache misses\n",
	    bt_cache_hit, bt_cache_miss);
	(void)fprintf(stderr, "%lu splits (%lu root splits, %lu sort splits)\n",
	    bt_split, bt_rootsplit, bt_sortsplit);
	pleaf *= t->bt_psize - BTDATAOFF;
	if (pleaf)
		(void)fprintf(stderr,
		    "%.0f%% leaf fill (%lu bytes used, %lu bytes free)\n",
		    ((double)(pleaf - lfree) / pleaf) * 100,
		    pleaf - lfree, lfree);
	pinternal *= t->bt_psize - BTDATAOFF;
	if (pinternal)
		(void)fprintf(stderr,
		    "%.0f%% internal fill (%lu bytes used, %lu bytes free\n",
		    ((double)(pinternal - ifree) / pinternal) * 100,
		    pinternal - ifree, ifree);
	if (bt_pfxsaved)
		(void)fprintf(stderr, "prefix checking removed %lu bytes.\n",
		    bt_pfxsaved);
}
#endif
