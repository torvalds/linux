/*	$OpenBSD: pack.c,v 1.18 2015/01/16 06:40:16 deraadt Exp $	*/
/*	$NetBSD: pack.c,v 1.5 1996/08/31 21:15:11 mycroft Exp $	*/

/*
 * Copyright (c) 1992, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * This software was developed by the Computer Systems Engineering group
 * at Lawrence Berkeley Laboratory under DARPA contract BG 91-66 and
 * contributed to Berkeley.
 *
 * All advertising materials mentioning features or use of this software
 * must display the following acknowledgement:
 *	This product includes software developed by the University of
 *	California, Lawrence Berkeley Laboratories.
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
 *
 *	from: @(#)pack.c	8.1 (Berkeley) 6/6/93
 */

#include <stdlib.h>
#include <string.h>

#include "config.h"

/*
 * Packing.  We have three separate kinds of packing here.
 *
 * First, we pack device instances, to collapse things like
 *
 *	uba0 at sbi0 nexus ?
 *	uba0 at bi0 nexus ?
 *
 * into a single instance that is "at sbi0 or bi0".
 *
 * Second, we pack locators.  Given something like
 *
 *	hp0 at mba0 drive 0
 *	hp* at mba* drive ?
 *	ht0 at mba0 drive 0
 *	tu0 at ht0 slave 0
 *	ht* at mba* drive ?
 *	tu* at ht* slave ?
 *
 * (where the default drive and slave numbers are -1), we have three
 * locators whose value is 0 and three whose value is -1.  Rather than
 * emitting six integers, we emit just two.
 *
 * Finally, we pack parent vectors.  This is very much like packing
 * locators.  Unlike locators, however, parent vectors are always
 * terminated by -1 (rather like the way C strings always end with
 * a NUL).
 *
 * When packing locators, we would like to find sequences such as
 *	{1 2 3} {2 3 4} {3} {4 5}
 * and turn this into the flat sequence {1 2 3 4 5}, with each subsequence
 * given by the appropriate offset (here 0, 1, 2, and 3 respectively).
 * When we pack parent vectors, overlap of this sort is impossible.
 * Non-overlapping packing is much easier, and so we use that here
 * and miss out on the chance to squeeze the locator sequence optimally.
 * (So it goes.)
 */

typedef int (*vec_cmp_func)(const void *, int, int);

#define	TAILHSIZE	128
#define	PVHASH(i)	((i) & (TAILHSIZE - 1))
#define	LOCHASH(l)	(((long)(l) >> 2) & (TAILHSIZE - 1))
struct tails {
	struct	tails *t_next;
	int	t_ends_at;
};

static struct tails *tails[TAILHSIZE];
static int locspace;
static int pvecspace;
static int longest_pvec;

static void packdevi(void);
static void packlocs(void);
static void packpvec(void);

static void addparents(struct devi *src, struct devi *dst);
static int nparents(struct devi **, struct devbase *, int);
static int sameas(struct devi *, struct devi *);
static int findvec(const void *, int, int, vec_cmp_func, int);
static int samelocs(const void *, int, int);
static int addlocs(const char **, int);
static int loclencmp(const void *, const void *);
static int samepv(const void *, int, int);
static int addpv(short *, int);
static int pvlencmp(const void *, const void *);
static void resettails(void);

void
pack(void)
{
	struct devi *i;
	int n;

	/* Pack instances and make parent vectors. */
	packdevi();

	/*
	 * Now that we know what we have, find upper limits on space
	 * needed for the loc[] and pv[] tables, and find the longest
	 * single pvec.  The loc and pv table sizes are bounded by
	 * what we would get if no packing occurred.
	 */
	locspace = pvecspace = 0;
	for (i = alldevi; i != NULL; i = i->i_next) {
		if (i->i_collapsed)
			continue;
		locspace += i->i_atattr->a_loclen;
		n = i->i_pvlen + 1;
		if (n > longest_pvec)
			longest_pvec = n;
		pvecspace += n;
	}

	/* Allocate and pack loc[]. */
	locators.vec = ereallocarray(NULL, locspace, sizeof(*locators.vec));
	locators.used = 0;
	packlocs();

	/* Allocate and pack pv[]. */
	parents.vec = ereallocarray(NULL, pvecspace, sizeof(*parents.vec));
	parents.used = 0;
	packpvec();
}

/*
 * Pack instances together wherever possible.  When everything is
 * packed, go back and set up the parents for each.  We must do this
 * on a second pass because during the first one, we do not know which,
 * if any, of the parents will collapse during packing.
 */
void
packdevi(void)
{
	struct devi *i, *l, *p;
	struct deva *d;
	int j, m, n;

	packed = ereallocarray(NULL, ndevi + 1, sizeof *packed);
	n = 0;
	for (d = alldevas; d != NULL; d = d->d_next) {
		/*
		 * For each instance of each attachment, add or collapse
		 * all its aliases.
		 */
		for (i = d->d_ihead; i != NULL; i = i->i_asame) {
			m = n;
			for (l = i; l != NULL; l = l->i_alias) {
				/* Skip if we already handled this one.  */
				if (l->i_cfindex >= 0)
					continue;
				l->i_pvlen = 0;
				l->i_pvoff = -1;
				l->i_locoff = -1;
				/* try to find an equivalent for l */
				for (j = m; j < n; j++) {
					p = packed[j];
					if (sameas(l, p)) {
						l->i_collapsed = 1;
						l->i_cfindex = p->i_cfindex;
						goto nextalias;
					}
				}
				/* could not find a suitable alias */
				l->i_collapsed = 0;
				l->i_cfindex = n;
				l->i_parents = emalloc(sizeof(*l->i_parents));
				l->i_parents[0] = NULL;
				packed[n++] = l;
			nextalias:;
			}
		}
	}
	npacked = n;
	packed[n] = NULL;
	for (i = alldevi; i != NULL; i = i->i_next)
		addparents(i, packed[i->i_cfindex]);
}

/*
 * Return true if two aliases are "the same".  In this case, they need
 * to attach via the same attribute, have the same config flags, and
 * have the same locators.
 */
static int
sameas(struct devi *i1, struct devi *i2)
{
	const char **p1, **p2;

	if (i1->i_atattr != i2->i_atattr)
		return (0);
	if (i1->i_cfflags != i2->i_cfflags)
		return (0);
	for (p1 = i1->i_locs, p2 = i2->i_locs; *p1 == *p2; p2++)
		if (*p1++ == 0)
			return (1);
	return 0;
}

/*
 * Add the parents associated with "src" to the (presumably uncollapsed)
 * instance "dst".
 */
static void
addparents(struct devi *src, struct devi *dst)
{
	struct nvlist *nv;
	struct devi *i, **p, **q;
	int j, n, old, new, ndup;

	if (dst->i_collapsed)
		panic("addparents() i_collapsed");

	/* Collect up list of parents to add. */
	if (src->i_at == NULL)	/* none, because we are "at root" */
		return;
	if (src->i_atdev != NULL) {
		n = nparents(NULL, src->i_atdev, src->i_atunit);
		p = ereallocarray(NULL, n, sizeof *p);
		if (n == 0)
			return;
		(void)nparents(p, src->i_atdev, src->i_atunit);
	} else {
		n = 0;
		for (nv = src->i_atattr->a_refs; nv != NULL; nv = nv->nv_next)
			n += nparents(NULL, nv->nv_ptr, src->i_atunit);
		if (n == 0)
			return;
		p = ereallocarray(NULL, n, sizeof *p);
		n = 0;
		for (nv = src->i_atattr->a_refs; nv != NULL; nv = nv->nv_next)
			n += nparents(p + n, nv->nv_ptr, src->i_atunit);
	}
	/* Now elide duplicates. */
	ndup = 0;
	for (j = 0; j < n; j++) {
		i = p[j];
		for (q = dst->i_parents; *q != NULL; q++) {
			if (*q == i) {
				ndup++;
				p[j] = NULL;
				break;
			}
		}
	}
	/* Finally, add all the non-duplicates. */
	old = dst->i_pvlen;
	new = old + (n - ndup);
	if (old > new)
		panic("addparents() old > new");
	if (old == new) {
		free(p);
		return;
	}
	dst->i_parents = q = ereallocarray(dst->i_parents, new + 1, sizeof(*q));
	dst->i_pvlen = new;
	q[new] = NULL;
	q += old;
	for (j = 0; j < n; j++)
		if (p[j] != NULL)
			*q++ = p[j];
	free(p);
}

/*
 * Count up parents, and optionally store pointers to each.
 */
static int
nparents(struct devi **p, struct devbase *dev, int unit)
{
	struct devi *i, *l;
	int n;

	n = 0;
	/* for each instance ... */
	for (i = dev->d_ihead; i != NULL; i = i->i_bsame) {
		/* ... take each un-collapsed alias */
		for (l = i; l != NULL; l = l->i_alias) {
			if (!l->i_collapsed &&
			    (unit == WILD || unit == l->i_unit)) {
				if (p != NULL)
					*p++ = l;
				n++;
			}
		}
	}
	return (n);
}

static void
packlocs(void)
{
	struct devi **p, *i;
	int l, o;

	qsort(packed, npacked, sizeof *packed, loclencmp);
	for (p = packed; (i = *p) != NULL; p++) {
		if ((l = i->i_atattr->a_loclen) > 0) {
			o = findvec(i->i_locs, LOCHASH(i->i_locs[l - 1]), l,
				    samelocs, locators.used);
			i->i_locoff = o < 0 ? addlocs(i->i_locs, l) : o;
		} else
			i->i_locoff = -1;
	}
	resettails();
}

static void
packpvec(void)
{
	struct devi **p, *i, **par;
	int l, v, o;
	short *vec;

	vec = ereallocarray(NULL, longest_pvec, sizeof(*vec));
	qsort(packed, npacked, sizeof *packed, pvlencmp);
	for (p = packed; (i = *p) != NULL; p++) {
		l = i->i_pvlen;
		if (l > longest_pvec)
			panic("packpvec");
		par = i->i_parents;
		for (v = 0; v < l; v++)
			vec[v] = par[v]->i_cfindex;
		if (l == 0 || (o = findvec(vec, PVHASH(vec[l - 1]), l,
		    samepv, parents.used)) < 0)
			o = addpv(vec, l);
		i->i_pvoff = o;
	}
	free(vec);
	resettails();
}

/*
 * Return the index at which the given vector already exists, or -1
 * if it is not anywhere in the current set.  If we return -1, we assume
 * our caller will add it at the end of the current set, and we make
 * sure that next time, we will find it there.
 */
static int
findvec(const void *ptr, int hash, int len, vec_cmp_func cmp, int nextplace)
{
	struct tails *t, **hp;
	int off;

	hp = &tails[hash];
	for (t = *hp; t != NULL; t = t->t_next) {
		off = t->t_ends_at - len;
		if (off >= 0 && (*cmp)(ptr, off, len))
			return (off);
	}
	t = emalloc(sizeof(*t));
	t->t_next = *hp;
	*hp = t;
	t->t_ends_at = nextplace + len;
	return (-1);
}

/*
 * Comparison function for locators.
 */
static int
samelocs(const void *ptr, int off, int len)
{
	const char **p, **q;

	for (p = &locators.vec[off], q = (const char **)ptr; --len >= 0;)
		if (*p++ != *q++)
			return (0);	/* different */
	return (1);			/* same */
}

/*
 * Add the given locators at the end of the global loc[] table.
 */
static int
addlocs(const char **locs, int len)
{
	const char **p;
	int ret;

	ret = locators.used;
	if ((locators.used = ret + len) > locspace)
		panic("addlocs: overrun");
	for (p = &locators.vec[ret]; --len >= 0;)
		*p++ = *locs++;
	return (ret);
}

/*
 * Comparison function for qsort-by-locator-length, longest first.
 * We rashly assume that subtraction of these lengths does not overflow.
 */
static int
loclencmp(const void *a, const void *b)
{
	int l1, l2;

	l1 = (*(struct devi **)a)->i_atattr->a_loclen;
	l2 = (*(struct devi **)b)->i_atattr->a_loclen;
	return (l2 - l1);
}

/*
 * Comparison function for parent vectors.
 */
static int
samepv(const void *ptr, int off, int len)
{
	short *p, *q;

	for (p = &parents.vec[off], q = (short *)ptr; --len >= 0;)
		if (*p++ != *q++)
			return (0);	/* different */
	return (1);			/* same */
}

/*
 * Add the given parent vectors at the end of the global pv[] table.
 */
static int
addpv(short *pv, int len)
{
	short *p;
	int ret;
	static int firstend = -1;

	/*
	 * If the vector is empty, reuse the first -1.  It will be
	 * there if there are any nonempty vectors at all, since we
	 * do the longest first.  If there are no nonempty vectors,
	 * something is probably wrong, but we will ignore that here.
	 */
	if (len == 0 && firstend >= 0)
		return (firstend);
	len++;			/* account for trailing -1 */
	ret = parents.used;
	if ((parents.used = ret + len) > pvecspace)
		panic("addpv: overrun");
	for (p = &parents.vec[ret]; --len > 0;)
		*p++ = *pv++;
	*p = -1;
	if (firstend < 0)
		firstend = parents.used - 1;
	return (ret);
}

/*
 * Comparison function for qsort-by-parent-vector-length, longest first.
 * We rashly assume that subtraction of these lengths does not overflow.
 */
static int
pvlencmp(const void *a, const void *b)
{
	int l1, l2;

	l1 = (*(struct devi **)a)->i_pvlen;
	l2 = (*(struct devi **)b)->i_pvlen;
	return (l2 - l1);
}

static void
resettails(void)
{
	struct tails **p, *t, *next;
	int i;

	for (p = tails, i = TAILHSIZE; --i >= 0; p++) {
		for (t = *p; t != NULL; t = next) {
			next = t->t_next;
			free(t);
		}
		*p = NULL;
	}
}
