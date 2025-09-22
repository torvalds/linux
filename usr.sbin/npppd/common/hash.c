/*	$OpenBSD: hash.c,v 1.6 2024/02/26 08:25:51 yasuoka Exp $ */
/*-
 * Copyright (c) 2009 Internet Initiative Japan Inc.
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
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hash.h"

/* hash_create - Create a new hash table.
 * Returns a pointer of new hash_table on success.  Otherwise returns
 * NULL.
 */
hash_table *
hash_create(int (*cmp_func) (const void *, const void *),
    uint32_t (*hash_func) (const void *, int), int hsz)
{
	hash_table *htbl;

	htbl = malloc(sizeof(hash_table));
	if (htbl == NULL)
		return NULL;

	if (hsz < 1)
		htbl->size = HASH_SIZE;
	else
		htbl->size = hsz;

	htbl->bucket = calloc(htbl->size, sizeof(hash_link *));
	htbl->cmp = cmp_func;
	htbl->hash = hash_func;
	htbl->cur = 0;
	htbl->bucket_cur = NULL;

	return htbl;
}

/* hash_first - Get first item from hash table.
 * Returns a pointer of first bucket on success.  Otherwise returns
 * NULL.
 */
hash_link *
hash_first(hash_table *htbl)
{
	htbl->cur = 0;
	htbl->bucket_cur = NULL;
	return hash_next(htbl);
}

/* hash_next -  Get next item from hash table.
 * Returns a pointer of next bucket on success.  Otherwise returns
 * NULL.
 */
hash_link *
hash_next(hash_table *htbl)
{
	hash_link *hlink;

	if (htbl->bucket_cur != NULL) {
		hlink = htbl->bucket_cur;
		htbl->bucket_cur = hlink->next;
		return hlink;
	}
	while (htbl->cur < htbl->size) {
		if (htbl->bucket[htbl->cur] != NULL) {
			hlink = htbl->bucket[htbl->cur++];
			htbl->bucket_cur = hlink->next;
			return hlink;
		}
		htbl->cur++;
	}
	return NULL;
}

/* hash_lookup - Lookup item under the key in hash table.
 * Return a pointer of the bucket on success.  Otherwise returns
 * NULL
 */
hash_link *
hash_lookup(hash_table *htbl, const void *k)
{
	int c;
	hash_link *w;

	if (htbl == NULL || k == NULL)
		return NULL;
	c = (htbl->hash) (k, (int)htbl->size);
	for (w = htbl->bucket[c]; w != NULL; w = w->next)
		if (htbl->cmp(w->key, k) == 0)
			return w;
	return NULL;
}

/* hash_insert - Insert a item into hash table.
 * Return 0 on success.  Return -1 on failure.
 */
int
hash_insert(hash_table *htbl, const void *k, void *i)
{
	int c;
	hash_link *n;

	if (htbl == NULL || k == NULL)
		return -1;

	if ((n = malloc(sizeof(hash_link))) == NULL) {
		return -1;
	}

	c = (htbl->hash) (k, (int)htbl->size);

	n->item = i;
	n->key = k;
	n->next = htbl->bucket[c];
	htbl->bucket[c] = n;

	return 0;
}

/* hash_delete - Remove a item from hash table.
 * If memfree then free the item.  Return 0 on success.  Return -1
 * on failure.
 */
int
hash_delete(hash_table *htbl, const void *k, int memfree)
{
	int i;
	hash_link *b, *w;

	if (htbl == NULL || k == NULL)
		return -1;

	i = (htbl->hash) (k, (int)htbl->size);

	for (w = htbl->bucket[i], b = NULL; w != NULL; w = w->next) {
		if (htbl->cmp(w->key, k) == 0) {
			if (b != NULL)
				b->next = w->next;
			else
				htbl->bucket[i] = w->next;

			if (htbl->bucket_cur == w)
				htbl->bucket_cur = w->next;

			if (w->item != NULL && memfree) {
				free(w->item);
			}
			free(w);
			return 0;
		}
		b = w;
	}
	return -1;
}

/*
 * delete all items from this hash_table.
 * If memfree != 0 then free items.
 */
void
hash_delete_all(hash_table *htbl, int memfree)
{
	int i;
	hash_link *w, *hl;

	for (i = 0; i < htbl->size; i++) {
		hl = htbl->bucket[i];
		htbl->bucket[i] = NULL;
		while (hl != NULL) {
			w = hl;
			hl = hl->next;
			if (memfree && w->item != NULL)
				free(w->item);
			free(w);
		}
	}
}

/* hash_free - Free hash table and all buckets.
 */
void
hash_free(hash_table *htbl)
{
	if (htbl != NULL) {
		free(htbl->bucket);
		free(htbl);
	}
}
