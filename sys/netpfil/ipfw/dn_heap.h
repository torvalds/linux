/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998-2010 Luigi Rizzo, Universita` di Pisa
 * All rights reserved
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

/*
 * Binary heap and hash tables, header file
 *
 * $FreeBSD$
 */

#ifndef _IP_DN_HEAP_H
#define _IP_DN_HEAP_H

#define DN_KEY_LT(a,b)     ((int64_t)((a)-(b)) < 0)
#define DN_KEY_LEQ(a,b)    ((int64_t)((a)-(b)) <= 0)

/*
 * This module implements a binary heap supporting random extraction.
 *
 * A heap entry contains an uint64_t key and a pointer to object.
 * DN_KEY_LT(a,b) returns true if key 'a' is smaller than 'b'
 *
 * The heap is a struct dn_heap plus a dynamically allocated
 * array of dn_heap_entry entries. 'size' represents the size of
 * the array, 'elements' count entries in use. The topmost
 * element has the smallest key.
 * The heap supports ordered insert, and extract from the top.
 * To extract an object from the middle of the heap, we the object
 * must reserve an 'int32_t' to store the position of the object
 * in the heap itself, and the location of this field must be
 * passed as an argument to heap_init() -- use -1 if the feature
 * is not used.
 */
struct dn_heap_entry {
	uint64_t key;	/* sorting key, smallest comes first */
	void *object;	/* object pointer */
};

struct dn_heap {
	int size;	/* the size of the array */
	int elements;	/* elements in use */
	int ofs;	/* offset in the object of heap index */
	struct dn_heap_entry *p;	/* array of "size" entries */
};

enum {
	HEAP_SCAN_DEL = 1,
	HEAP_SCAN_END = 2,
};

/*
 * heap_init() reinitializes the heap setting the size and the offset
 *	of the index for random extraction (use -1 if not used).
 *	The 'elements' counter is set to 0.
 *
 * SET_HEAP_OFS() indicates where, in the object, is stored the index
 *	for random extractions from the heap.
 *
 * heap_free() frees the memory associated to a heap.
 *
 * heap_insert() adds a key-pointer pair to the heap
 *
 * HEAP_TOP() returns a pointer to the top element of the heap,
 *	but makes no checks on its existence (XXX should we change ?)
 *
 * heap_extract() removes the entry at the top, returning the pointer.
 *	(the key should have been read before).
 *
 * heap_scan() invokes a callback on each entry of the heap.
 *	The callback can return a combination of HEAP_SCAN_DEL and
 *	HEAP_SCAN_END. HEAP_SCAN_DEL means the current element must
 *	be removed, and HEAP_SCAN_END means to terminate the scan.
 *	heap_scan() returns the number of elements removed.
 *	Because the order is not guaranteed, we should use heap_scan()
 *	only as a last resort mechanism.
 */
#define HEAP_TOP(h)	((h)->p)
#define SET_HEAP_OFS(h, n)	do { (h)->ofs = n; } while (0)
int     heap_init(struct dn_heap *h, int size, int ofs);
int     heap_insert(struct dn_heap *h, uint64_t key1, void *p);
void    heap_extract(struct dn_heap *h, void *obj);
void heap_free(struct dn_heap *h);
int heap_scan(struct dn_heap *, int (*)(void *, uintptr_t), uintptr_t);

/*------------------------------------------------------
 * This module implements a generic hash table with support for
 * running callbacks on the entire table. To avoid allocating
 * memory during hash table operations, objects must reserve
 * space for a link field. XXX if the heap is moderately full,
 * an SLIST suffices, and we can tolerate the cost of a hash
 * computation on each removal.
 *
 * dn_ht_init() initializes the table, setting the number of
 *	buckets, the offset of the link field, the main callbacks.
 *	Callbacks are:
 * 
 *	hash(key, flags, arg) called to return a bucket index.
 *	match(obj, key, flags, arg) called to determine if key
 *		matches the current 'obj' in the heap
 *	newh(key, flags, arg) optional, used to allocate a new
 *		object during insertions.
 *
 * dn_ht_free() frees the heap or unlink elements.
 *	DNHT_REMOVE unlink elements, 0 frees the heap.
 *	You need two calls to do both.
 *
 * dn_ht_find() is the main lookup function, which can also be
 *	used to insert or delete elements in the hash table.
 *	The final 'arg' is passed to all callbacks.
 *
 * dn_ht_scan() is used to invoke a callback on all entries of
 *	the heap, or possibly on just one bucket. The callback
 *	is invoked with a pointer to the object, and must return
 *	one of DNHT_SCAN_DEL or DNHT_SCAN_END to request the
 *	removal of the object from the heap and the end of the
 *	scan, respectively.
 *
 * dn_ht_scan_bucket() is similar to dn_ht_scan(), except that it scans
 *	only the specific bucket of the table. The bucket is a in-out
 *	parameter and return a valid bucket number if the original
 *	is invalid.
 *
 * A combination of flags can be used to modify the operation
 * of the dn_ht_find(), and of the callbacks:
 *
 * DNHT_KEY_IS_OBJ	means the key is the object pointer.
 *	It is usually of interest for the hash and match functions.
 *
 * DNHT_MATCH_PTR	during a lookup, match pointers instead
 *	of calling match(). Normally used when removing specific
 *	entries. Does not imply KEY_IS_OBJ as the latter _is_ used
 *	by the match function.
 *
 * DNHT_INSERT		insert the element if not found.
 *	Calls new() to allocates a new object unless
 *	DNHT_KEY_IS_OBJ is set.
 *
 * DNHT_UNIQUE		only insert if object not found.
 *	XXX should it imply DNHT_INSERT ?
 *
 * DNHT_REMOVE		remove objects if we find them.
 */
struct dn_ht;	/* should be opaque */

struct dn_ht *dn_ht_init(struct dn_ht *, int buckets, int ofs, 
        uint32_t (*hash)(uintptr_t, int, void *),
        int (*match)(void *, uintptr_t, int, void *),
        void *(*newh)(uintptr_t, int, void *));
void dn_ht_free(struct dn_ht *, int flags);

void *dn_ht_find(struct dn_ht *, uintptr_t, int, void *);
int dn_ht_scan(struct dn_ht *, int (*)(void *, void *), void *);
int dn_ht_scan_bucket(struct dn_ht *, int * , int (*)(void *, void *), void *);
int dn_ht_entries(struct dn_ht *);

enum {  /* flags values.
	 * first two are returned by the scan callback to indicate
	 * to delete the matching element or to end the scan
	 */
        DNHT_SCAN_DEL	= 0x0001,
        DNHT_SCAN_END	= 0x0002,
        DNHT_KEY_IS_OBJ	= 0x0004,	/* key is the obj pointer */
        DNHT_MATCH_PTR	= 0x0008,	/* match by pointer, not match() */
        DNHT_INSERT	= 0x0010,	/* insert if not found */
        DNHT_UNIQUE	= 0x0020,	/* report error if already there */
        DNHT_REMOVE	= 0x0040,	/* remove on find or dn_ht_free */
}; 

#endif /* _IP_DN_HEAP_H */
