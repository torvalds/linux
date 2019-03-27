/*	$FreeBSD$	*/
/*	$NetBSD: hash.c,v 1.1.1.1 1999/11/19 04:30:56 mrg Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1988, 1989, 1990 The Regents of the University of California.
 * Copyright (c) 1988, 1989 by Adam de Boor
 * Copyright (c) 1989 by Berkeley Softworks
 * All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Adam de Boor.
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
 *	This product includes software developed by the University of
 *	California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
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

#ifdef MAKE_BOOTSTRAP
static char rcsid[] = "$NetBSD: hash.c,v 1.1.1.1 1999/11/19 04:30:56 mrg Exp $";
#else
#include <sys/cdefs.h>
#ifndef lint
#if 0
static char sccsid[] = "@(#)hash.c	8.1 (Berkeley) 6/6/93";
#else
__RCSID("$NetBSD: hash.c,v 1.1.1.1 1999/11/19 04:30:56 mrg Exp $");
#endif
#endif /* not lint */
#endif

#include <sys/types.h>

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* hash.c --
 *
 * 	This module contains routines to manipulate a hash table.
 * 	See hash.h for a definition of the structure of the hash
 * 	table.  Hash tables grow automatically as the amount of
 * 	information increases.
 */
#include "sprite.h"
#ifndef ORDER
#include "make.h"
#endif /* ORDER */
#include "hash.h"
#include "ealloc.h"

/*
 * Forward references to local procedures that are used before they're
 * defined:
 */

static void RebuildTable(Hash_Table *);

/*
 * The following defines the ratio of # entries to # buckets
 * at which we rebuild the table to make it larger.
 */

#define rebuildLimit 8

/*
 *---------------------------------------------------------
 *
 * Hash_InitTable --
 *
 *	This routine just sets up the hash table.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Memory is allocated for the initial bucket area.
 *
 *---------------------------------------------------------
 */

void
Hash_InitTable(
	register Hash_Table *t,	/* Structure to use to hold table. */
	int numBuckets)		/* How many buckets to create for starters.
				 * This number is rounded up to a power of
				 * two.   If <= 0, a reasonable default is
				 * chosen. The table will grow in size later
				 * as needed. */
{
	register int i;
	register struct Hash_Entry **hp;

	/*
	 * Round up the size to a power of two.
	 */
	if (numBuckets <= 0)
		i = 16;
	else {
		for (i = 2; i < numBuckets; i <<= 1)
			 continue;
	}
	t->numEntries = 0;
	t->size = i;
	t->mask = i - 1;
	t->bucketPtr = hp = (struct Hash_Entry **)emalloc(sizeof(*hp) * i);
	while (--i >= 0)
		*hp++ = NULL;
}

/*
 *---------------------------------------------------------
 *
 * Hash_DeleteTable --
 *
 *	This routine removes everything from a hash table
 *	and frees up the memory space it occupied (except for
 *	the space in the Hash_Table structure).
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Lots of memory is freed up.
 *
 *---------------------------------------------------------
 */

void
Hash_DeleteTable(Hash_Table *t)
{
	register struct Hash_Entry **hp, *h, *nexth = NULL;
	register int i;

	for (hp = t->bucketPtr, i = t->size; --i >= 0;) {
		for (h = *hp++; h != NULL; h = nexth) {
			nexth = h->next;
			free((char *)h);
		}
	}
	free((char *)t->bucketPtr);

	/*
	 * Set up the hash table to cause memory faults on any future access
	 * attempts until re-initialization.
	 */
	t->bucketPtr = NULL;
}

/*
 *---------------------------------------------------------
 *
 * Hash_FindEntry --
 *
 * 	Searches a hash table for an entry corresponding to key.
 *
 * Results:
 *	The return value is a pointer to the entry for key,
 *	if key was present in the table.  If key was not
 *	present, NULL is returned.
 *
 * Side Effects:
 *	None.
 *
 *---------------------------------------------------------
 */

Hash_Entry *
Hash_FindEntry(
	Hash_Table *t,		/* Hash table to search. */
	char *key)		/* A hash key. */
{
	register Hash_Entry *e;
	register unsigned h;
	register char *p;

	for (h = 0, p = key; *p;)
		h = (h << 5) - h + *p++;
	p = key;
	for (e = t->bucketPtr[h & t->mask]; e != NULL; e = e->next)
		if (e->namehash == h && strcmp(e->name, p) == 0)
			return (e);
	return (NULL);
}

/*
 *---------------------------------------------------------
 *
 * Hash_CreateEntry --
 *
 *	Searches a hash table for an entry corresponding to
 *	key.  If no entry is found, then one is created.
 *
 * Results:
 *	The return value is a pointer to the entry.  If *newPtr
 *	isn't NULL, then *newPtr is filled in with TRUE if a
 *	new entry was created, and FALSE if an entry already existed
 *	with the given key.
 *
 * Side Effects:
 *	Memory may be allocated, and the hash buckets may be modified.
 *---------------------------------------------------------
 */

Hash_Entry *
Hash_CreateEntry(
	register Hash_Table *t,	/* Hash table to search. */
	char *key,		/* A hash key. */
	Boolean *newPtr)	/* Filled in with TRUE if new entry created,
				 * FALSE otherwise. */
{
	register Hash_Entry *e;
	register unsigned h;
	register char *p;
	int keylen;
	struct Hash_Entry **hp;

	/*
	 * Hash the key.  As a side effect, save the length (strlen) of the
	 * key in case we need to create the entry.
	 */
	for (h = 0, p = key; *p;)
		h = (h << 5) - h + *p++;
	keylen = p - key;
	p = key;
	for (e = t->bucketPtr[h & t->mask]; e != NULL; e = e->next) {
		if (e->namehash == h && strcmp(e->name, p) == 0) {
			if (newPtr != NULL)
				*newPtr = FALSE;
			return (e);
		}
	}

	/*
	 * The desired entry isn't there.  Before allocating a new entry,
	 * expand the table if necessary (and this changes the resulting
	 * bucket chain).
	 */
	if (t->numEntries >= rebuildLimit * t->size)
		RebuildTable(t);
	e = (Hash_Entry *) emalloc(sizeof(*e) + keylen);
	hp = &t->bucketPtr[h & t->mask];
	e->next = *hp;
	*hp = e;
	e->clientData = NULL;
	e->namehash = h;
	(void) strcpy(e->name, p);
	t->numEntries++;

	if (newPtr != NULL)
		*newPtr = TRUE;
	return (e);
}

/*
 *---------------------------------------------------------
 *
 * Hash_DeleteEntry --
 *
 * 	Delete the given hash table entry and free memory associated with
 *	it.
 *
 * Results:
 *	None.
 *
 * Side Effects:
 *	Hash chain that entry lives in is modified and memory is freed.
 *
 *---------------------------------------------------------
 */

void
Hash_DeleteEntry(Hash_Table *t, Hash_Entry *e)
{
	register Hash_Entry **hp, *p;

	if (e == NULL)
		return;
	for (hp = &t->bucketPtr[e->namehash & t->mask];
	     (p = *hp) != NULL; hp = &p->next) {
		if (p == e) {
			*hp = p->next;
			free((char *)p);
			t->numEntries--;
			return;
		}
	}
	(void)write(2, "bad call to Hash_DeleteEntry\n", 29);
	abort();
}

/*
 *---------------------------------------------------------
 *
 * Hash_EnumFirst --
 *	This procedure sets things up for a complete search
 *	of all entries recorded in the hash table.
 *
 * Results:
 *	The return value is the address of the first entry in
 *	the hash table, or NULL if the table is empty.
 *
 * Side Effects:
 *	The information in searchPtr is initialized so that successive
 *	calls to Hash_Next will return successive HashEntry's
 *	from the table.
 *
 *---------------------------------------------------------
 */

Hash_Entry *
Hash_EnumFirst(
	Hash_Table *t,			/* Table to be searched. */
	register Hash_Search *searchPtr)/* Area in which to keep state
					 * about search.*/
{
	searchPtr->tablePtr = t;
	searchPtr->nextIndex = 0;
	searchPtr->hashEntryPtr = NULL;
	return Hash_EnumNext(searchPtr);
}

/*
 *---------------------------------------------------------
 *
 * Hash_EnumNext --
 *    This procedure returns successive entries in the hash table.
 *
 * Results:
 *    The return value is a pointer to the next HashEntry
 *    in the table, or NULL when the end of the table is
 *    reached.
 *
 * Side Effects:
 *    The information in searchPtr is modified to advance to the
 *    next entry.
 *
 *---------------------------------------------------------
 */

Hash_Entry *
Hash_EnumNext(
	register Hash_Search *searchPtr) /* Area used to keep state about
					    search. */
{
	register Hash_Entry *e;
	Hash_Table *t = searchPtr->tablePtr;

	/*
	 * The hashEntryPtr field points to the most recently returned
	 * entry, or is nil if we are starting up.  If not nil, we have
	 * to start at the next one in the chain.
	 */
	e = searchPtr->hashEntryPtr;
	if (e != NULL)
		e = e->next;
	/*
	 * If the chain ran out, or if we are starting up, we need to
	 * find the next nonempty chain.
	 */
	while (e == NULL) {
		if (searchPtr->nextIndex >= t->size)
			return (NULL);
		e = t->bucketPtr[searchPtr->nextIndex++];
	}
	searchPtr->hashEntryPtr = e;
	return (e);
}

/*
 *---------------------------------------------------------
 *
 * RebuildTable --
 *	This local routine makes a new hash table that
 *	is larger than the old one.
 *
 * Results:
 * 	None.
 *
 * Side Effects:
 *	The entire hash table is moved, so any bucket numbers
 *	from the old table are invalid.
 *
 *---------------------------------------------------------
 */

static void
RebuildTable(register Hash_Table *t)
{
	register Hash_Entry *e, *next = NULL, **hp, **xp;
	register int i, mask;
        register Hash_Entry **oldhp;
	int oldsize;

	oldhp = t->bucketPtr;
	oldsize = i = t->size;
	i <<= 1;
	t->size = i;
	t->mask = mask = i - 1;
	t->bucketPtr = hp = (struct Hash_Entry **) emalloc(sizeof(*hp) * i);
	while (--i >= 0)
		*hp++ = NULL;
	for (hp = oldhp, i = oldsize; --i >= 0;) {
		for (e = *hp++; e != NULL; e = next) {
			next = e->next;
			xp = &t->bucketPtr[e->namehash & mask];
			e->next = *xp;
			*xp = e;
		}
	}
	free((char *)oldhp);
}
