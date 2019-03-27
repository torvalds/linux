/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1998-2002,2010 Luigi Rizzo, Universita` di Pisa
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
 * Binary heap and hash tables, used in dummynet
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#ifdef _KERNEL
__FBSDID("$FreeBSD$");
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <netpfil/ipfw/dn_heap.h>
#ifndef log
#define log(x, arg...)
#endif

#else /* !_KERNEL */

#include <stdio.h>
#include <dn_test.h>
#include <strings.h>
#include <stdlib.h>

#include  "dn_heap.h"
#define log(x, arg...)	fprintf(stderr, ## arg)
#define panic(x...)	fprintf(stderr, ## x), exit(1)
#define MALLOC_DEFINE(a, b, c)	volatile int __dummy__ ## a __attribute__((__unused__))
static void *my_malloc(int s) {	return malloc(s); }
static void my_free(void *p) {	free(p); }
#define malloc(s, t, w)	my_malloc(s)
#define free(p, t)	my_free(p)
#endif /* !_KERNEL */

static MALLOC_DEFINE(M_DN_HEAP, "dummynet", "dummynet heap");

/*
 * Heap management functions.
 *
 * In the heap, first node is element 0. Children of i are 2i+1 and 2i+2.
 * Some macros help finding parent/children so we can optimize them.
 *
 * heap_init() is called to expand the heap when needed.
 * Increment size in blocks of 16 entries.
 * Returns 1 on error, 0 on success
 */
#define HEAP_FATHER(x) ( ( (x) - 1 ) / 2 )
#define HEAP_LEFT(x) ( (x)+(x) + 1 )
#define	HEAP_SWAP(a, b, buffer) { buffer = a ; a = b ; b = buffer ; }
#define HEAP_INCREMENT	15

static int
heap_resize(struct dn_heap *h, unsigned int new_size)
{
	struct dn_heap_entry *p;

	if ((unsigned int)h->size >= new_size )	/* have enough room */
		return 0;
#if 1  /* round to the next power of 2 */
	new_size |= new_size >> 1;
	new_size |= new_size >> 2;
	new_size |= new_size >> 4;
	new_size |= new_size >> 8;
	new_size |= new_size >> 16;
#else
	new_size = (new_size + HEAP_INCREMENT ) & ~HEAP_INCREMENT;
#endif
	p = mallocarray(new_size, sizeof(*p), M_DN_HEAP, M_NOWAIT);
	if (p == NULL) {
		printf("--- %s, resize %d failed\n", __func__, new_size );
		return 1; /* error */
	}
	if (h->size > 0) {
		bcopy(h->p, p, h->size * sizeof(*p) );
		free(h->p, M_DN_HEAP);
	}
	h->p = p;
	h->size = new_size;
	return 0;
}

int
heap_init(struct dn_heap *h, int size, int ofs)
{
	if (heap_resize(h, size))
		return 1;
	h->elements = 0;
	h->ofs = ofs;
	return 0;
}

/*
 * Insert element in heap. Normally, p != NULL, we insert p in
 * a new position and bubble up. If p == NULL, then the element is
 * already in place, and key is the position where to start the
 * bubble-up.
 * Returns 1 on failure (cannot allocate new heap entry)
 *
 * If ofs > 0 the position (index, int) of the element in the heap is
 * also stored in the element itself at the given offset in bytes.
 */
#define SET_OFFSET(h, i) do {					\
	if (h->ofs > 0)						\
	    *((int32_t *)((char *)(h->p[i].object) + h->ofs)) = i;	\
	} while (0)
/*
 * RESET_OFFSET is used for sanity checks. It sets ofs
 * to an invalid value.
 */
#define RESET_OFFSET(h, i) do {					\
	if (h->ofs > 0)						\
	    *((int32_t *)((char *)(h->p[i].object) + h->ofs)) = -16;	\
	} while (0)

int
heap_insert(struct dn_heap *h, uint64_t key1, void *p)
{
	int son = h->elements;

	//log("%s key %llu p %p\n", __FUNCTION__, key1, p);
	if (p == NULL) { /* data already there, set starting point */
		son = key1;
	} else { /* insert new element at the end, possibly resize */
		son = h->elements;
		if (son == h->size) /* need resize... */
			// XXX expand by 16 or so
			if (heap_resize(h, h->elements+16) )
				return 1; /* failure... */
		h->p[son].object = p;
		h->p[son].key = key1;
		h->elements++;
	}
	/* make sure that son >= father along the path */
	while (son > 0) {
		int father = HEAP_FATHER(son);
		struct dn_heap_entry tmp;

		if (DN_KEY_LT( h->p[father].key, h->p[son].key ) )
			break; /* found right position */
		/* son smaller than father, swap and repeat */
		HEAP_SWAP(h->p[son], h->p[father], tmp);
		SET_OFFSET(h, son);
		son = father;
	}
	SET_OFFSET(h, son);
	return 0;
}

/*
 * remove top element from heap, or obj if obj != NULL
 */
void
heap_extract(struct dn_heap *h, void *obj)
{
	int child, father, max = h->elements - 1;

	if (max < 0) {
		printf("--- %s: empty heap 0x%p\n", __FUNCTION__, h);
		return;
	}
	if (obj == NULL)
		father = 0; /* default: move up smallest child */
	else { /* extract specific element, index is at offset */
		if (h->ofs <= 0)
			panic("%s: extract from middle not set on %p\n",
				__FUNCTION__, h);
		father = *((int *)((char *)obj + h->ofs));
		if (father < 0 || father >= h->elements) {
			panic("%s: father %d out of bound 0..%d\n",
				__FUNCTION__, father, h->elements);
		}
	}
	/*
	 * below, father is the index of the empty element, which
	 * we replace at each step with the smallest child until we
	 * reach the bottom level.
	 */
	// XXX why removing RESET_OFFSET increases runtime by 10% ?
	RESET_OFFSET(h, father);
	while ( (child = HEAP_LEFT(father)) <= max ) {
		if (child != max &&
		    DN_KEY_LT(h->p[child+1].key, h->p[child].key) )
			child++; /* take right child, otherwise left */
		h->p[father] = h->p[child];
		SET_OFFSET(h, father);
		father = child;
	}
	h->elements--;
	if (father != max) {
		/*
		 * Fill hole with last entry and bubble up,
		 * reusing the insert code
		 */
		h->p[father] = h->p[max];
		heap_insert(h, father, NULL);
	}
}

#if 0
/*
 * change object position and update references
 * XXX this one is never used!
 */
static void
heap_move(struct dn_heap *h, uint64_t new_key, void *object)
{
	int temp, i, max = h->elements-1;
	struct dn_heap_entry *p, buf;

	if (h->ofs <= 0)
		panic("cannot move items on this heap");
	p = h->p;	/* shortcut */

	i = *((int *)((char *)object + h->ofs));
	if (DN_KEY_LT(new_key, p[i].key) ) { /* must move up */
		p[i].key = new_key;
		for (; i>0 &&
		    DN_KEY_LT(new_key, p[(temp = HEAP_FATHER(i))].key);
		    i = temp ) { /* bubble up */
			HEAP_SWAP(p[i], p[temp], buf);
			SET_OFFSET(h, i);
		}
	} else {		/* must move down */
		p[i].key = new_key;
		while ( (temp = HEAP_LEFT(i)) <= max ) {
			/* found left child */
			if (temp != max &&
			    DN_KEY_LT(p[temp+1].key, p[temp].key))
				temp++; /* select child with min key */
			if (DN_KEY_LT(>p[temp].key, new_key)) {
				/* go down */
				HEAP_SWAP(p[i], p[temp], buf);
				SET_OFFSET(h, i);
			} else
				break;
			i = temp;
		}
	}
	SET_OFFSET(h, i);
}
#endif /* heap_move, unused */

/*
 * heapify() will reorganize data inside an array to maintain the
 * heap property. It is needed when we delete a bunch of entries.
 */
static void
heapify(struct dn_heap *h)
{
	int i;

	for (i = 0; i < h->elements; i++ )
		heap_insert(h, i , NULL);
}

int
heap_scan(struct dn_heap *h, int (*fn)(void *, uintptr_t),
	uintptr_t arg)
{
	int i, ret, found;

	for (i = found = 0 ; i < h->elements ;) {
		ret = fn(h->p[i].object, arg);
		if (ret & HEAP_SCAN_DEL) {
			h->elements-- ;
			h->p[i] = h->p[h->elements] ;
			found++ ;
		} else
			i++ ;
		if (ret & HEAP_SCAN_END)
			break;
	}
	if (found)
		heapify(h);
	return found;
}

/*
 * cleanup the heap and free data structure
 */
void
heap_free(struct dn_heap *h)
{
	if (h->size >0 )
		free(h->p, M_DN_HEAP);
	bzero(h, sizeof(*h) );
}

/*
 * hash table support.
 */

struct dn_ht {
        int buckets;            /* how many buckets, really buckets - 1*/
        int entries;            /* how many entries */
        int ofs;	        /* offset of link field */
        uint32_t (*hash)(uintptr_t, int, void *arg);
        int (*match)(void *_el, uintptr_t key, int, void *);
        void *(*newh)(uintptr_t, int, void *);
        void **ht;              /* bucket heads */
};
/*
 * Initialize, allocating bucket pointers inline.
 * Recycle previous record if possible.
 * If the 'newh' function is not supplied, we assume that the
 * key passed to ht_find is the same object to be stored in.
 */
struct dn_ht *
dn_ht_init(struct dn_ht *ht, int buckets, int ofs,
        uint32_t (*h)(uintptr_t, int, void *),
        int (*match)(void *, uintptr_t, int, void *),
	void *(*newh)(uintptr_t, int, void *))
{
	int l;

	/*
	 * Notes about rounding bucket size to a power of two.
	 * Given the original bucket size, we compute the nearest lower and
	 * higher power of two, minus 1  (respectively b_min and b_max) because
	 * this value will be used to do an AND with the index returned
	 * by hash function.
	 * To choice between these two values, the original bucket size is
	 * compared with b_min. If the original size is greater than 4/3 b_min,
	 * we round the bucket size to b_max, else to b_min.
	 * This ratio try to round to the nearest power of two, advantaging
	 * the greater size if the different between two power is relatively
	 * big.
	 * Rounding the bucket size to a power of two avoid the use of
	 * module when calculating the correct bucket.
	 * The ht->buckets variable store the bucket size - 1 to simply
	 * do an AND between the index returned by hash function and ht->bucket
	 * instead of a module.
	 */
	int b_min; /* min buckets */
	int b_max; /* max buckets */
	int b_ori; /* original buckets */

	if (h == NULL || match == NULL) {
		printf("--- missing hash or match function");
		return NULL;
	}
	if (buckets < 1 || buckets > 65536)
		return NULL;

	b_ori = buckets;
	/* calculate next power of 2, - 1*/
	buckets |= buckets >> 1;
	buckets |= buckets >> 2;
	buckets |= buckets >> 4;
	buckets |= buckets >> 8;
	buckets |= buckets >> 16;

	b_max = buckets; /* Next power */
	b_min = buckets >> 1; /* Previous power */

	/* Calculate the 'nearest' bucket size */
	if (b_min * 4000 / 3000 < b_ori)
		buckets = b_max;
	else
		buckets = b_min;

	if (ht) {	/* see if we can reuse */
		if (buckets <= ht->buckets) {
			ht->buckets = buckets;
		} else {
			/* free pointers if not allocated inline */
			if (ht->ht != (void *)(ht + 1))
				free(ht->ht, M_DN_HEAP);
			free(ht, M_DN_HEAP);
			ht = NULL;
		}
	}
	if (ht == NULL) {
		/* Allocate buckets + 1 entries because buckets is use to
		 * do the AND with the index returned by hash function
		 */
		l = sizeof(*ht) + (buckets + 1) * sizeof(void **);
		ht = malloc(l, M_DN_HEAP, M_NOWAIT | M_ZERO);
	}
	if (ht) {
		ht->ht = (void **)(ht + 1);
		ht->buckets = buckets;
		ht->ofs = ofs;
		ht->hash = h;
		ht->match = match;
		ht->newh = newh;
	}
	return ht;
}

/* dummy callback for dn_ht_free to unlink all */
static int
do_del(void *obj, void *arg)
{
	(void)obj;
	(void)arg;
	return DNHT_SCAN_DEL;
}

void
dn_ht_free(struct dn_ht *ht, int flags)
{
	if (ht == NULL)
		return;
	if (flags & DNHT_REMOVE) {
		(void)dn_ht_scan(ht, do_del, NULL);
	} else {
		if (ht->ht && ht->ht != (void *)(ht + 1))
			free(ht->ht, M_DN_HEAP);
		free(ht, M_DN_HEAP);
	}
}

int
dn_ht_entries(struct dn_ht *ht)
{
	return ht ? ht->entries : 0;
}

/* lookup and optionally create or delete element */
void *
dn_ht_find(struct dn_ht *ht, uintptr_t key, int flags, void *arg)
{
	int i;
	void **pp, *p;

	if (ht == NULL)	/* easy on an empty hash */
		return NULL;
	i = (ht->buckets == 1) ? 0 :
		(ht->hash(key, flags, arg) & ht->buckets);

	for (pp = &ht->ht[i]; (p = *pp); pp = (void **)((char *)p + ht->ofs)) {
		if (flags & DNHT_MATCH_PTR) {
			if (key == (uintptr_t)p)
				break;
		} else if (ht->match(p, key, flags, arg)) /* found match */
			break;
	}
	if (p) {
		if (flags & DNHT_REMOVE) {
			/* link in the next element */
			*pp = *(void **)((char *)p + ht->ofs);
			*(void **)((char *)p + ht->ofs) = NULL;
			ht->entries--;
		}
	} else if (flags & DNHT_INSERT) {
		// printf("%s before calling new, bucket %d ofs %d\n",
		//	__FUNCTION__, i, ht->ofs);
		p = ht->newh ? ht->newh(key, flags, arg) : (void *)key;
		// printf("%s newh returns %p\n", __FUNCTION__, p);
		if (p) {
			ht->entries++;
			*(void **)((char *)p + ht->ofs) = ht->ht[i];
			ht->ht[i] = p;
		}
	}
	return p;
}

/*
 * do a scan with the option to delete the object. Extract next before
 * running the callback because the element may be destroyed there.
 */
int
dn_ht_scan(struct dn_ht *ht, int (*fn)(void *, void *), void *arg)
{
	int i, ret, found = 0;
	void **curp, *cur, *next;

	if (ht == NULL || fn == NULL)
		return 0;
	for (i = 0; i <= ht->buckets; i++) {
		curp = &ht->ht[i];
		while ( (cur = *curp) != NULL) {
			next = *(void **)((char *)cur + ht->ofs);
			ret = fn(cur, arg);
			if (ret & DNHT_SCAN_DEL) {
				found++;
				ht->entries--;
				*curp = next;
			} else {
				curp = (void **)((char *)cur + ht->ofs);
			}
			if (ret & DNHT_SCAN_END)
				return found;
		}
	}
	return found;
}

/*
 * Similar to dn_ht_scan(), except that the scan is performed only
 * in the bucket 'bucket'. The function returns a correct bucket number if
 * the original is invalid.
 * If the callback returns DNHT_SCAN_END, the function move the ht->ht[i]
 * pointer to the last entry processed. Moreover, the bucket number passed
 * by caller is decremented, because usually the caller increment it.
 */
int
dn_ht_scan_bucket(struct dn_ht *ht, int *bucket, int (*fn)(void *, void *),
		 void *arg)
{
	int i, ret, found = 0;
	void **curp, *cur, *next;

	if (ht == NULL || fn == NULL)
		return 0;
	if (*bucket > ht->buckets)
		*bucket = 0;
	i = *bucket;

	curp = &ht->ht[i];
	while ( (cur = *curp) != NULL) {
		next = *(void **)((char *)cur + ht->ofs);
		ret = fn(cur, arg);
		if (ret & DNHT_SCAN_DEL) {
			found++;
			ht->entries--;
			*curp = next;
		} else {
			curp = (void **)((char *)cur + ht->ofs);
		}
		if (ret & DNHT_SCAN_END)
			return found;
	}
	return found;
}
