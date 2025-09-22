/*	$OpenBSD: hcreate.c,v 1.7 2016/05/29 20:47:49 guenther Exp $	*/
/*	$NetBSD: hcreate.c,v 1.5 2004/04/23 02:48:12 simonb Exp $	*/

/*
 * Copyright (c) 2001 Christopher G. Demetriou
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *          This product includes software developed for the
 *          NetBSD Project.  See http://www.NetBSD.org/ for
 *          information about NetBSD.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * <<Id: LICENSE,v 1.2 2000/06/14 15:57:33 cgd Exp>>
 */

/*
 * hcreate() / hsearch() / hdestroy()
 *
 * SysV/XPG4 hash table functions.
 *
 * Implementation done based on NetBSD manual page and Solaris manual page,
 * plus my own personal experience about how they're supposed to work.
 *
 * I tried to look at Knuth (as cited by the Solaris manual page), but
 * nobody had a copy in the office, so...
 */

#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <search.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>

#include <db.h>		/* for __default_hash */

#ifndef _DIAGASSERT
#define _DIAGASSERT(x)
#endif

/*
 * DO NOT MAKE THIS STRUCTURE LARGER THAN 32 BYTES (4 ptrs on 64-bit
 * ptr machine) without adjusting MAX_BUCKETS_LG2 below.
 */
struct internal_entry {
	SLIST_ENTRY(internal_entry) link;
	ENTRY ent;
};
SLIST_HEAD(internal_head, internal_entry);

#define	MIN_BUCKETS_LG2	4
#define	MIN_BUCKETS	(1 << MIN_BUCKETS_LG2)

/*
 * max * sizeof internal_entry must fit into size_t.
 * assumes internal_entry is <= 32 (2^5) bytes.
 */
#define	MAX_BUCKETS_LG2	(sizeof (size_t) * 8 - 1 - 5)
#define	MAX_BUCKETS	((size_t)1 << MAX_BUCKETS_LG2)

static struct internal_head *htable;
static size_t htablesize;

int
hcreate(size_t nel)
{
	size_t idx;
	unsigned int p2;

	/* Make sure this isn't called when a table already exists. */
	_DIAGASSERT(htable == NULL);
	if (htable != NULL) {
		errno = EINVAL;
		return 0;
	}

	/* If nel is too small, make it min sized. */
	if (nel < MIN_BUCKETS)
		nel = MIN_BUCKETS;

	/* If it's too large, cap it. */
	if (nel > MAX_BUCKETS)
		nel = MAX_BUCKETS;

	/* If it's is not a power of two in size, round up. */
	if ((nel & (nel - 1)) != 0) {
		for (p2 = 0; nel != 0; p2++)
			nel >>= 1;
		_DIAGASSERT(p2 <= MAX_BUCKETS_LG2);
		nel = 1 << p2;
	}
	
	/* Allocate the table. */
	htablesize = nel;
	htable = calloc(htablesize, sizeof htable[0]);
	if (htable == NULL) {
		errno = ENOMEM;
		return 0;
	}

	/* Initialize it. */
	for (idx = 0; idx < htablesize; idx++)
		SLIST_INIT(&htable[idx]);

	return 1;
}

void
hdestroy(void)
{
	struct internal_entry *ie;
	size_t idx;

	_DIAGASSERT(htable != NULL);
	if (htable == NULL)
		return;

	for (idx = 0; idx < htablesize; idx++) {
		while (!SLIST_EMPTY(&htable[idx])) {
			ie = SLIST_FIRST(&htable[idx]);
			SLIST_REMOVE_HEAD(&htable[idx], link);
			free(ie->ent.key);
			free(ie);
		}
	}
	free(htable);
	htable = NULL;
}

ENTRY *
hsearch(ENTRY item, ACTION action)
{
	struct internal_head *head;
	struct internal_entry *ie;
	uint32_t hashval;
	size_t len;

	_DIAGASSERT(htable != NULL);
	_DIAGASSERT(item.key != NULL);
	_DIAGASSERT(action == ENTER || action == FIND);

	len = strlen(item.key);
	hashval = __default_hash(item.key, len);

	head = &htable[hashval & (htablesize - 1)];
	ie = SLIST_FIRST(head);
	while (ie != NULL) {
		if (strcmp(ie->ent.key, item.key) == 0)
			break;
		ie = SLIST_NEXT(ie, link);
	}

	if (ie != NULL)
		return &ie->ent;
	else if (action == FIND)
		return NULL;

	ie = malloc(sizeof *ie);
	if (ie == NULL)
		return NULL;
	ie->ent.key = item.key;
	ie->ent.data = item.data;

	SLIST_INSERT_HEAD(head, ie, link);
	return &ie->ent;
}
