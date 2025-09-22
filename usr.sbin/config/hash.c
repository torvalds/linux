/*	$OpenBSD: hash.c,v 1.19 2021/11/28 19:26:03 deraadt Exp $	*/
/*	$NetBSD: hash.c,v 1.4 1996/11/07 22:59:43 gwr Exp $	*/

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
 *	from: @(#)hash.c	8.1 (Berkeley) 6/6/93
 */

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "config.h"

/*
 * Interned strings are kept in a hash table.  By making each string
 * unique, the program can compare strings by comparing pointers.
 */
struct hashent {
	struct	hashent *h_next;	/* hash buckets are chained */
	const char *h_name;		/* the string */
	u_int	h_hash;			/* its hash value */
	void	*h_value;		/* other values (for name=value) */
};
struct hashtab {
	size_t	ht_size;		/* size (power of 2) */
	u_int	ht_mask;		/* == ht_size - 1 */
	u_int	ht_used;		/* number of entries used */
	u_int	ht_lim;			/* when to expand */
	struct	hashent **ht_tab;	/* base of table */
};
static struct hashtab strings;

/*
 * HASHFRACTION controls ht_lim, which in turn controls the average chain
 * length.  We allow a few entries, on average, as comparing them is usually
 * cheap (the h_hash values prevent a strcmp).
 */
#define	HASHFRACTION(sz) ((sz) * 3 / 2)

/* round up to next multiple of y, where y is a power of 2 */
#define	ROUND(x, y) (((x) + (y) - 1) & ~((y) - 1))

static void ht_init(struct hashtab *, size_t);
static void ht_expand(struct hashtab *);

/*
 * Initialize a new hash table.  The size must be a power of 2.
 */
static void
ht_init(struct hashtab *ht, size_t sz)
{
	struct hashent **h;
	u_int n;

	h = ereallocarray(NULL, sz, sizeof *h);
	ht->ht_tab = h;
	ht->ht_size = sz;
	ht->ht_mask = sz - 1;
	for (n = 0; n < sz; n++)
		*h++ = NULL;
	ht->ht_used = 0;
	ht->ht_lim = HASHFRACTION(sz);
}

/*
 * Expand an existing hash table.
 */
static void
ht_expand(struct hashtab *ht)
{
	struct hashent *p, **h, **oldh, *q;
	u_int n, i;

	n = ht->ht_size * 2;
	h = ecalloc(n, sizeof *h);
	oldh = ht->ht_tab;
	n--;
	for (i = ht->ht_size; i != 0; i--) {
		for (p = *oldh++; p != NULL; p = q) {
			q = p->h_next;
			p->h_next = h[p->h_hash & n];
			h[p->h_hash & n] = p;
		}
	}
	free(ht->ht_tab);
	ht->ht_tab = h;
	ht->ht_mask = n;
	ht->ht_size = ++n;
	ht->ht_lim = HASHFRACTION(n);
}

/*
 * Make a new hash entry, setting its h_next to NULL.
 */
static __inline struct hashent *
newhashent(const char *name, u_int h)
{
	struct	hashent *hp;

	hp = emalloc(sizeof(*hp));
	hp->h_name = name;
	hp->h_hash = h;
	hp->h_next = NULL;
	return (hp);
}

/*
 * Hash a string.
 */
static __inline u_int
hash(const char *str)
{
	u_int h;

	for (h = 0; *str;)
		h = (h << 5) + h + *str++;
	return (h);
}

void
initintern(void)
{

	ht_init(&strings, 128);
}

/*
 * Generate a single unique copy of the given string.  We expect this
 * function to be used frequently, so it should be fast.
 */
const char *
intern(const char *s)
{
	struct hashtab *ht;
	struct hashent *hp, **hpp;
	u_int h;
	char *p;
	size_t l;

	ht = &strings;
	h = hash(s);
	hpp = &ht->ht_tab[h & ht->ht_mask];
	for (; (hp = *hpp) != NULL; hpp = &hp->h_next)
		if (hp->h_hash == h && strcmp(hp->h_name, s) == 0)
			return (hp->h_name);
	l = strlen(s) + 1;
	p = malloc(l);
	bcopy(s, p, l);
	*hpp = newhashent(p, h);
	if (++ht->ht_used > ht->ht_lim)
		ht_expand(ht);
	return (p);
}

struct hashtab *
ht_new(void)
{
	struct hashtab *ht;

	ht = emalloc(sizeof *ht);
	ht_init(ht, 8);
	return (ht);
}

/*
 * Remove.
 */
int
ht_remove(struct hashtab *ht, const char *nam)
{
	struct hashent *hp, *thp;
	u_int h;

	h = hash(nam);
	hp = ht->ht_tab[h & ht->ht_mask];
	while (hp && hp->h_name == nam)	{
		ht->ht_tab[h & ht->ht_mask] = hp->h_next;
		/* XXX free hp ? */
		hp = ht->ht_tab[h & ht->ht_mask];
	}

	if ((hp = ht->ht_tab[h & ht->ht_mask]) == NULL)
		return (0);

	for (thp = hp->h_next; thp != NULL; thp = hp->h_next) {
		if (thp->h_name == nam) {
			hp->h_next = thp->h_next;
			/* XXX free thp ? */
		} else
			hp = thp;
	}

	return (0);
}

/*
 * Insert and/or replace.
 */
int
ht_insrep(struct hashtab *ht, const char *nam, void *val, int replace)
{
	struct hashent *hp, **hpp;
	u_int h;

	h = hash(nam);
	hpp = &ht->ht_tab[h & ht->ht_mask];
	for (; (hp = *hpp) != NULL; hpp = &hp->h_next) {
		if (hp->h_name == nam) {
			if (replace)
				hp->h_value = val;
			return (1);
		}
	}
	*hpp = hp = newhashent(nam, h);
	hp->h_value = val;
	if (++ht->ht_used > ht->ht_lim)
		ht_expand(ht);
	return (0);
}

void *
ht_lookup(struct hashtab *ht, const char *nam)
{
	struct hashent *hp, **hpp;
	u_int h;

	h = hash(nam);
	hpp = &ht->ht_tab[h & ht->ht_mask];
	for (; (hp = *hpp) != NULL; hpp = &hp->h_next)
		if (hp->h_name == nam)
			return (hp->h_value);
	return (NULL);
}
