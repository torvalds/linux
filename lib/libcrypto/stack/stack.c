/* $OpenBSD: stack.c,v 1.33 2025/01/03 08:04:16 tb Exp $ */
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

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <openssl/objects.h>
#include <openssl/stack.h>

#include "stack_local.h"

#undef MIN_NODES
#define MIN_NODES	4

int
(*sk_set_cmp_func(_STACK *sk, int (*c)(const void *, const void *)))(
    const void *, const void *)
{
	int (*old)(const void *, const void *) = sk->comp;

	if (sk->comp != c)
		sk->sorted = 0;
	sk->comp = c;

	return old;
}
LCRYPTO_ALIAS(sk_set_cmp_func);

_STACK *
sk_dup(_STACK *sk)
{
	_STACK *ret;
	char **s;

	if ((ret = sk_new(sk->comp)) == NULL)
		goto err;
	s = reallocarray(ret->data, sk->num_alloc, sizeof(char *));
	if (s == NULL)
		goto err;
	ret->data = s;

	ret->num = sk->num;
	memcpy(ret->data, sk->data, sizeof(char *) * sk->num);
	ret->sorted = sk->sorted;
	ret->num_alloc = sk->num_alloc;
	ret->comp = sk->comp;
	return (ret);

err:
	if (ret)
		sk_free(ret);
	return (NULL);
}
LCRYPTO_ALIAS(sk_dup);

_STACK *
sk_new_null(void)
{
	return sk_new((int (*)(const void *, const void *))0);
}
LCRYPTO_ALIAS(sk_new_null);

_STACK *
sk_new(int (*c)(const void *, const void *))
{
	_STACK *ret;
	int i;

	if ((ret = malloc(sizeof(_STACK))) == NULL)
		goto err;
	if ((ret->data = reallocarray(NULL, MIN_NODES, sizeof(char *))) == NULL)
		goto err;
	for (i = 0; i < MIN_NODES; i++)
		ret->data[i] = NULL;
	ret->comp = c;
	ret->num_alloc = MIN_NODES;
	ret->num = 0;
	ret->sorted = 0;
	return (ret);

err:
	free(ret);
	return (NULL);
}
LCRYPTO_ALIAS(sk_new);

int
sk_insert(_STACK *st, void *data, int loc)
{
	char **s;

	if (st == NULL)
		return 0;
	if (st->num_alloc <= st->num + 1) {
		s = reallocarray(st->data, st->num_alloc, 2 * sizeof(char *));
		if (s == NULL)
			return (0);
		st->data = s;
		st->num_alloc *= 2;
	}
	if ((loc >= (int)st->num) || (loc < 0))
		st->data[st->num] = data;
	else {
		memmove(&(st->data[loc + 1]), &(st->data[loc]),
		    sizeof(char *)*(st->num - loc));
		st->data[loc] = data;
	}
	st->num++;
	st->sorted = 0;
	return (st->num);
}
LCRYPTO_ALIAS(sk_insert);

void *
sk_delete_ptr(_STACK *st, void *p)
{
	int i;

	for (i = 0; i < st->num; i++)
		if (st->data[i] == p)
			return (sk_delete(st, i));
	return (NULL);
}
LCRYPTO_ALIAS(sk_delete_ptr);

void *
sk_delete(_STACK *st, int loc)
{
	char *ret;

	if (!st || (loc < 0) || (loc >= st->num))
		return NULL;

	ret = st->data[loc];
	if (loc != st->num - 1) {
		memmove(&(st->data[loc]), &(st->data[loc + 1]),
		    sizeof(char *)*(st->num - 1 - loc));
	}
	st->num--;
	return (ret);
}
LCRYPTO_ALIAS(sk_delete);

static const void *
obj_bsearch_ex(const void *key, const void *base_, int num, int size,
    int (*cmp)(const void *, const void *))
{
	const char *base = base_;
	int l, h, i, c;

	l = 0;
	h = num;
	while (l < h) {
		i = (l + h) / 2;
		if ((c = cmp(key,  &base[i * size])) == 0) {
			/* Return first match. */
			while (i > 0 && cmp(key, &base[(i - 1) * size]) == 0)
				i--;
			return &base[i * size];
		}
		if (c < 0)
			h = i;
		else
			l = i + 1;
	}

	return NULL;
}

int
sk_find(_STACK *st, void *data)
{
	const void * const *r;
	int i;

	if (st == NULL)
		return -1;

	if (st->comp == NULL) {
		for (i = 0; i < st->num; i++)
			if (st->data[i] == data)
				return (i);
		return (-1);
	}
	sk_sort(st);
	if (data == NULL)
		return (-1);
	r = obj_bsearch_ex(&data, st->data, st->num, sizeof(void *), st->comp);
	if (r == NULL)
		return (-1);
	return (int)((char **)r - st->data);
}
LCRYPTO_ALIAS(sk_find);

int
sk_push(_STACK *st, void *data)
{
	return (sk_insert(st, data, st->num));
}
LCRYPTO_ALIAS(sk_push);

int
sk_unshift(_STACK *st, void *data)
{
	return (sk_insert(st, data, 0));
}
LCRYPTO_ALIAS(sk_unshift);

void *
sk_shift(_STACK *st)
{
	if (st == NULL)
		return (NULL);
	if (st->num <= 0)
		return (NULL);
	return (sk_delete(st, 0));
}
LCRYPTO_ALIAS(sk_shift);

void *
sk_pop(_STACK *st)
{
	if (st == NULL)
		return (NULL);
	if (st->num <= 0)
		return (NULL);
	return (sk_delete(st, st->num - 1));
}
LCRYPTO_ALIAS(sk_pop);

void
sk_zero(_STACK *st)
{
	if (st == NULL)
		return;
	if (st->num <= 0)
		return;
	memset(st->data, 0, sizeof(st->data)*st->num);
	st->num = 0;
}
LCRYPTO_ALIAS(sk_zero);

void
sk_pop_free(_STACK *st, void (*func)(void *))
{
	int i;

	if (st == NULL)
		return;
	for (i = 0; i < st->num; i++)
		if (st->data[i] != NULL)
			func(st->data[i]);
	sk_free(st);
}
LCRYPTO_ALIAS(sk_pop_free);

void
sk_free(_STACK *st)
{
	if (st == NULL)
		return;
	free(st->data);
	free(st);
}
LCRYPTO_ALIAS(sk_free);

int
sk_num(const _STACK *st)
{
	if (st == NULL)
		return -1;
	return st->num;
}
LCRYPTO_ALIAS(sk_num);

void *
sk_value(const _STACK *st, int i)
{
	if (!st || (i < 0) || (i >= st->num))
		return NULL;
	return st->data[i];
}
LCRYPTO_ALIAS(sk_value);

void *
sk_set(_STACK *st, int i, void *value)
{
	if (!st || (i < 0) || (i >= st->num))
		return NULL;
	st->sorted = 0;
	return (st->data[i] = value);
}
LCRYPTO_ALIAS(sk_set);

void
sk_sort(_STACK *st)
{
	if (st && !st->sorted) {
		int (*comp_func)(const void *, const void *);

		/* same comment as in sk_find ... previously st->comp was declared
		 * as a (void*,void*) callback type, but this made the population
		 * of the callback pointer illogical - our callbacks compare
		 * type** with type**, so we leave the casting until absolutely
		 * necessary (ie. "now"). */
		comp_func = (int (*)(const void *, const void *))(st->comp);
		qsort(st->data, st->num, sizeof(char *), comp_func);
		st->sorted = 1;
	}
}
LCRYPTO_ALIAS(sk_sort);

int
sk_is_sorted(const _STACK *st)
{
	if (st == NULL)
		return 1;

	if (st->sorted)
		return 1;

	/* If there is no comparison function we cannot sort. */
	if (st->comp == NULL)
		return 0;

	/* Lists with zero or one elements are always sorted. */
	return st->num <= 1;
}
LCRYPTO_ALIAS(sk_is_sorted);
