/* $OpenBSD: lhash.c,v 1.29 2025/05/01 00:35:23 tb Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
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
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <openssl/opensslconf.h>

#include <openssl/crypto.h>
#include <openssl/lhash.h>

#undef MIN_NODES
#define MIN_NODES	16
#define UP_LOAD		(2*LH_LOAD_MULT) /* load times 256  (default 2) */
#define DOWN_LOAD	(LH_LOAD_MULT)   /* load times 256  (default 1) */

typedef struct lhash_node_st {
	void *data;
	struct lhash_node_st *next;
#ifndef OPENSSL_NO_HASH_COMP
	unsigned long hash;
#endif
} LHASH_NODE;

struct lhash_st {
	LHASH_NODE **b;
	LHASH_COMP_FN_TYPE comp;
	LHASH_HASH_FN_TYPE hash;
	unsigned int num_nodes;
	unsigned int num_alloc_nodes;
	unsigned int p;
	unsigned int pmax;
	unsigned long up_load; /* load times 256 */
	unsigned long down_load; /* load times 256 */
	unsigned long num_items;

	int error;
} /* _LHASH */;

static void
expand(_LHASH *lh)
{
	LHASH_NODE **n, **n1, **n2, *np;
	unsigned int p, i, j;
	unsigned long hash, nni;

	lh->num_nodes++;
	p = (int)lh->p++;
	n1 = &(lh->b[p]);
	n2 = &(lh->b[p + (int)lh->pmax]);
	*n2 = NULL;        /* 27/07/92 - eay - undefined pointer bug */
	nni = lh->num_alloc_nodes;

	for (np = *n1; np != NULL; ) {
#ifndef OPENSSL_NO_HASH_COMP
		hash = np->hash;
#else
		hash = lh->hash(np->data);
#endif
		if ((hash % nni) != p) { /* move it */
			*n1 = (*n1)->next;
			np->next = *n2;
			*n2 = np;
		} else
			n1 = &((*n1)->next);
		np = *n1;
	}

	if ((lh->p) >= lh->pmax) {
		j = (int)lh->num_alloc_nodes * 2;
		n = reallocarray(lh->b, j, sizeof(LHASH_NODE *));
		if (n == NULL) {
/*			fputs("realloc error in lhash", stderr); */
			lh->error++;
			lh->p = 0;
			return;
		}
		/* else */
		for (i = (int)lh->num_alloc_nodes; i < j; i++)/* 26/02/92 eay */
			n[i] = NULL;			  /* 02/03/92 eay */
		lh->pmax = lh->num_alloc_nodes;
		lh->num_alloc_nodes = j;
		lh->p = 0;
		lh->b = n;
	}
}

static void
contract(_LHASH *lh)
{
	LHASH_NODE **n, *n1, *np;

	np = lh->b[lh->p + lh->pmax - 1];
	lh->b[lh->p+lh->pmax - 1] = NULL; /* 24/07-92 - eay - weird but :-( */
	if (lh->p == 0) {
		n = reallocarray(lh->b, lh->pmax, sizeof(LHASH_NODE *));
		if (n == NULL) {
/*			fputs("realloc error in lhash", stderr); */
			lh->error++;
			return;
		}
		lh->num_alloc_nodes /= 2;
		lh->pmax /= 2;
		lh->p = lh->pmax - 1;
		lh->b = n;
	} else
		lh->p--;

	lh->num_nodes--;

	n1 = lh->b[(int)lh->p];
	if (n1 == NULL)
		lh->b[(int)lh->p] = np;
	else {
		while (n1->next != NULL)
			n1 = n1->next;
		n1->next = np;
	}
}

static LHASH_NODE **
getrn(_LHASH *lh, const void *data, unsigned long *rhash)
{
	LHASH_NODE **ret, *n1;
	unsigned long hash, nn;
	LHASH_COMP_FN_TYPE cf;

	hash = (*(lh->hash))(data);
	*rhash = hash;

	nn = hash % lh->pmax;
	if (nn < lh->p)
		nn = hash % lh->num_alloc_nodes;

	cf = lh->comp;
	ret = &(lh->b[(int)nn]);
	for (n1 = *ret; n1 != NULL; n1 = n1->next) {
#ifndef OPENSSL_NO_HASH_COMP
		if (n1->hash != hash) {
			ret = &(n1->next);
			continue;
		}
#endif
		if (cf(n1->data, data) == 0)
			break;
		ret = &(n1->next);
	}
	return (ret);
}

_LHASH *
lh_new(LHASH_HASH_FN_TYPE h, LHASH_COMP_FN_TYPE c)
{
	_LHASH *ret;

	if ((ret = calloc(1, sizeof(_LHASH))) == NULL)
		return NULL;
	if ((ret->b = calloc(MIN_NODES, sizeof(LHASH_NODE *))) == NULL) {
		free(ret);
		return NULL;
	}
	ret->comp = ((c == NULL) ? (LHASH_COMP_FN_TYPE)strcmp : c);
	ret->hash = ((h == NULL) ? (LHASH_HASH_FN_TYPE)lh_strhash : h);
	ret->num_nodes = MIN_NODES / 2;
	ret->num_alloc_nodes = MIN_NODES;
	ret->pmax = MIN_NODES / 2;
	ret->up_load = UP_LOAD;
	ret->down_load = DOWN_LOAD;

	return (ret);
}
LCRYPTO_ALIAS(lh_new);

void
lh_free(_LHASH *lh)
{
	unsigned int i;
	LHASH_NODE *n, *nn;

	if (lh == NULL)
		return;

	for (i = 0; i < lh->num_nodes; i++) {
		n = lh->b[i];
		while (n != NULL) {
			nn = n->next;
			free(n);
			n = nn;
		}
	}
	free(lh->b);
	free(lh);
}
LCRYPTO_ALIAS(lh_free);

int
lh_error(_LHASH *lh)
{
	return lh->error;
}
LCRYPTO_ALIAS(lh_error);

void *
lh_insert(_LHASH *lh, void *data)
{
	unsigned long hash;
	LHASH_NODE *nn, **rn;
	void *ret;

	lh->error = 0;
	if (lh->up_load <= (lh->num_items * LH_LOAD_MULT / lh->num_nodes))
		expand(lh);

	rn = getrn(lh, data, &hash);

	if (*rn == NULL) {
		if ((nn = malloc(sizeof(LHASH_NODE))) == NULL) {
			lh->error++;
			return (NULL);
		}
		nn->data = data;
		nn->next = NULL;
#ifndef OPENSSL_NO_HASH_COMP
		nn->hash = hash;
#endif
		*rn = nn;
		ret = NULL;
		lh->num_items++;
	}
	else /* replace same key */
	{
		ret = (*rn)->data;
		(*rn)->data = data;
	}
	return (ret);
}
LCRYPTO_ALIAS(lh_insert);

void *
lh_delete(_LHASH *lh, const void *data)
{
	unsigned long hash;
	LHASH_NODE *nn, **rn;
	void *ret;

	lh->error = 0;
	rn = getrn(lh, data, &hash);

	if (*rn == NULL) {
		return (NULL);
	} else {
		nn = *rn;
		*rn = nn->next;
		ret = nn->data;
		free(nn);
	}

	lh->num_items--;
	if ((lh->num_nodes > MIN_NODES) &&
	    (lh->down_load >= (lh->num_items * LH_LOAD_MULT / lh->num_nodes)))
		contract(lh);

	return (ret);
}
LCRYPTO_ALIAS(lh_delete);

void *
lh_retrieve(_LHASH *lh, const void *data)
{
	unsigned long hash;
	LHASH_NODE **rn;
	void *ret;

	lh->error = 0;
	rn = getrn(lh, data, &hash);

	if (*rn == NULL) {
		return (NULL);
	} else {
		ret = (*rn)->data;
	}
	return (ret);
}
LCRYPTO_ALIAS(lh_retrieve);

static void
doall_util_fn(_LHASH *lh, int use_arg, LHASH_DOALL_FN_TYPE func,
    LHASH_DOALL_ARG_FN_TYPE func_arg, void *arg)
{
	LHASH_NODE *a, *n;
	int down_load;
	int i;

	if (lh == NULL)
		return;

	/*
	 * Disable contraction of the hash while walking, as some consumers use
	 * it to delete hash entries. A better option would be to snapshot the
	 * hash, making it insert safe as well.
	 */
	down_load = lh->down_load;
	lh->down_load = 0;

	/* reverse the order so we search from 'top to bottom'
	 * We were having memory leaks otherwise */
	for (i = lh->num_nodes - 1; i >= 0; i--) {
		a = lh->b[i];
		while (a != NULL) {
			/* 28/05/91 - eay - n added so items can be deleted
			 * via lh_doall */
			/* 22/05/08 - ben - eh? since a is not passed,
			 * this should not be needed */
			n = a->next;
			if (use_arg)
				func_arg(a->data, arg);
			else
				func(a->data);
			a = n;
		}
	}

	/* Restore down load factor and trigger contraction. */
	lh->down_load = down_load;
	if ((lh->num_nodes > MIN_NODES) &&
	    (lh->down_load >= (lh->num_items * LH_LOAD_MULT / lh->num_nodes)))
		contract(lh);
}

void
lh_doall(_LHASH *lh, LHASH_DOALL_FN_TYPE func)
{
	doall_util_fn(lh, 0, func, (LHASH_DOALL_ARG_FN_TYPE)0, NULL);
}
LCRYPTO_ALIAS(lh_doall);

void
lh_doall_arg(_LHASH *lh, LHASH_DOALL_ARG_FN_TYPE func, void *arg)
{
	doall_util_fn(lh, 1, (LHASH_DOALL_FN_TYPE)0, func, arg);
}
LCRYPTO_ALIAS(lh_doall_arg);

/* The following hash seems to work very well on normal text strings
 * no collisions on /usr/dict/words and it distributes on %2^n quite
 * well, not as good as MD5, but still good.
 */
unsigned long
lh_strhash(const char *c)
{
	unsigned long ret = 0;
	unsigned long n, v;
	unsigned int r;

	if (c == NULL || *c == '\0')
		return ret;

	n = 0x100;
	while (*c) {
		v = n | *c;
		n += 0x100;
		if ((r = ((v >> 2) ^ v) & 0x0f) != 0)
			ret = (ret << r) | (ret >> (32 - r));
		ret &= 0xFFFFFFFFUL;
		ret ^= v * v;
		c++;
	}
	return (ret >> 16) ^ ret;
}
LCRYPTO_ALIAS(lh_strhash);

unsigned long
lh_num_items(const _LHASH *lh)
{
	return lh ? lh->num_items : 0;
}
LCRYPTO_ALIAS(lh_num_items);
