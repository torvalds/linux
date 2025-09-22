/*	$OpenBSD: uvm_aobj.c,v 1.116 2025/03/10 14:13:58 mpi Exp $	*/
/*	$NetBSD: uvm_aobj.c,v 1.39 2001/02/18 21:19:08 chs Exp $	*/

/*
 * Copyright (c) 1998 Chuck Silvers, Charles D. Cranor and
 *                    Washington University.
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
 * from: Id: uvm_aobj.c,v 1.1.2.5 1998/02/06 05:14:38 chs Exp
 */
/*
 * uvm_aobj.c: anonymous memory uvm_object pager
 *
 * author: Chuck Silvers <chuq@chuq.com>
 * started: Jan-1998
 *
 * - design mostly from Chuck Cranor
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/pool.h>
#include <sys/stdint.h>
#include <sys/atomic.h>

#include <uvm/uvm.h>

/*
 * An anonymous UVM object (aobj) manages anonymous-memory.  In addition to
 * keeping the list of resident pages, it may also keep a list of allocated
 * swap blocks.  Depending on the size of the object, this list is either
 * stored in an array (small objects) or in a hash table (large objects).
 */

/*
 * Note: for hash tables, we break the address space of the aobj into blocks
 * of UAO_SWHASH_CLUSTER_SIZE pages, which shall be a power of two.
 */
#define	UAO_SWHASH_CLUSTER_SHIFT	4
#define	UAO_SWHASH_CLUSTER_SIZE		(1 << UAO_SWHASH_CLUSTER_SHIFT)

/* Get the "tag" for this page index. */
#define	UAO_SWHASH_ELT_TAG(idx)		((idx) >> UAO_SWHASH_CLUSTER_SHIFT)
#define UAO_SWHASH_ELT_PAGESLOT_IDX(idx) \
    ((idx) & (UAO_SWHASH_CLUSTER_SIZE - 1))

/* Given an ELT and a page index, find the swap slot. */
#define	UAO_SWHASH_ELT_PAGESLOT(elt, idx) \
    ((elt)->slots[UAO_SWHASH_ELT_PAGESLOT_IDX(idx)])

/* Given an ELT, return its pageidx base. */
#define	UAO_SWHASH_ELT_PAGEIDX_BASE(elt) \
    ((elt)->tag << UAO_SWHASH_CLUSTER_SHIFT)

/* The hash function. */
#define	UAO_SWHASH_HASH(aobj, idx) \
    (&(aobj)->u_swhash[(((idx) >> UAO_SWHASH_CLUSTER_SHIFT) \
    & (aobj)->u_swhashmask)])

/*
 * The threshold which determines whether we will use an array or a
 * hash table to store the list of allocated swap blocks.
 */
#define	UAO_SWHASH_THRESHOLD		(UAO_SWHASH_CLUSTER_SIZE * 4)
#define	UAO_USES_SWHASH(aobj) \
    ((aobj)->u_pages > UAO_SWHASH_THRESHOLD)

/* The number of buckets in a hash, with an upper bound. */
#define	UAO_SWHASH_MAXBUCKETS		256
#define	UAO_SWHASH_BUCKETS(pages) \
    (min((pages) >> UAO_SWHASH_CLUSTER_SHIFT, UAO_SWHASH_MAXBUCKETS))


/*
 * uao_swhash_elt: when a hash table is being used, this structure defines
 * the format of an entry in the bucket list.
 */
struct uao_swhash_elt {
	LIST_ENTRY(uao_swhash_elt) list;	/* the hash list */
	voff_t tag;				/* our 'tag' */
	int count;				/* our number of active slots */
	int slots[UAO_SWHASH_CLUSTER_SIZE];	/* the slots */
};

/*
 * uao_swhash: the swap hash table structure
 */
LIST_HEAD(uao_swhash, uao_swhash_elt);

/*
 * uao_swhash_elt_pool: pool of uao_swhash_elt structures
 */
struct pool uao_swhash_elt_pool;

/*
 * uvm_aobj: the actual anon-backed uvm_object
 *
 * => the uvm_object is at the top of the structure, this allows
 *   (struct uvm_aobj *) == (struct uvm_object *)
 * => only one of u_swslots and u_swhash is used in any given aobj
 */
struct uvm_aobj {
	struct uvm_object u_obj; /* has: pgops, memt, #pages, #refs */
	int u_pages;		 /* number of pages in entire object */
	int u_flags;		 /* the flags (see uvm_aobj.h) */
	/*
	 * Either an array or hashtable (array of bucket heads) of
	 * offset -> swapslot mappings for the aobj.
	 */
#define u_swslots	u_swap.slot_array 
#define u_swhash	u_swap.slot_hash
	union swslots {
		int			*slot_array;
		struct uao_swhash	*slot_hash;
	} u_swap;
	u_long u_swhashmask;		/* mask for hashtable */
	LIST_ENTRY(uvm_aobj) u_list;	/* global list of aobjs */
};

struct pool uvm_aobj_pool;

static struct uao_swhash_elt	*uao_find_swhash_elt(struct uvm_aobj *, int,
				     boolean_t);
static boolean_t		 uao_flush(struct uvm_object *, voff_t,
				     voff_t, int);
static void			 uao_free(struct uvm_aobj *);
static int			 uao_get(struct uvm_object *, voff_t,
				     vm_page_t *, int *, int, vm_prot_t,
				     int, int);
static boolean_t		 uao_pagein(struct uvm_aobj *, int, int);
static boolean_t		 uao_pagein_page(struct uvm_aobj *, int);

void	uao_dropswap_range(struct uvm_object *, voff_t, voff_t);
void	uao_shrink_flush(struct uvm_object *, int, int);
int	uao_shrink_hash(struct uvm_object *, int);
int	uao_shrink_array(struct uvm_object *, int);
int	uao_shrink_convert(struct uvm_object *, int);

int	uao_grow_hash(struct uvm_object *, int);
int	uao_grow_array(struct uvm_object *, int);
int	uao_grow_convert(struct uvm_object *, int);

/*
 * aobj_pager
 *
 * note that some functions (e.g. put) are handled elsewhere
 */
const struct uvm_pagerops aobj_pager = {
	.pgo_reference = uao_reference,
	.pgo_detach = uao_detach,
	.pgo_flush = uao_flush,
	.pgo_get = uao_get,
};

/*
 * uao_list: global list of active aobjs, locked by uao_list_lock
 *
 * Lock ordering: generally the locking order is object lock, then list lock.
 * in the case of swap off we have to iterate over the list, and thus the
 * ordering is reversed. In that case we must use trylocking to prevent
 * deadlock.
 */
static LIST_HEAD(aobjlist, uvm_aobj) uao_list = LIST_HEAD_INITIALIZER(uao_list);
static struct mutex uao_list_lock = MUTEX_INITIALIZER(IPL_MPFLOOR);


/*
 * functions
 */
/*
 * hash table/array related functions
 */
/*
 * uao_find_swhash_elt: find (or create) a hash table entry for a page
 * offset.
 */
static struct uao_swhash_elt *
uao_find_swhash_elt(struct uvm_aobj *aobj, int pageidx, boolean_t create)
{
	struct uao_swhash *swhash;
	struct uao_swhash_elt *elt;
	voff_t page_tag;

	swhash = UAO_SWHASH_HASH(aobj, pageidx); /* first hash to get bucket */
	page_tag = UAO_SWHASH_ELT_TAG(pageidx);	/* tag to search for */

	/*
	 * now search the bucket for the requested tag
	 */
	LIST_FOREACH(elt, swhash, list) {
		if (elt->tag == page_tag)
			return elt;
	}

	if (!create)
		return NULL;

	/*
	 * allocate a new entry for the bucket and init/insert it in
	 */
	elt = pool_get(&uao_swhash_elt_pool, PR_NOWAIT | PR_ZERO);
	/*
	 * XXX We cannot sleep here as the hash table might disappear
	 * from under our feet.  And we run the risk of deadlocking
	 * the pagedeamon.  In fact this code will only be called by
	 * the pagedaemon and allocation will only fail if we
	 * exhausted the pagedeamon reserve.  In that case we're
	 * doomed anyway, so panic.
	 */
	if (elt == NULL)
		panic("%s: can't allocate entry", __func__);
	LIST_INSERT_HEAD(swhash, elt, list);
	elt->tag = page_tag;

	return elt;
}

/*
 * uao_find_swslot: find the swap slot number for an aobj/pageidx
 */
int
uao_find_swslot(struct uvm_object *uobj, int pageidx)
{
	struct uvm_aobj *aobj = (struct uvm_aobj *)uobj;

	KASSERT(UVM_OBJ_IS_AOBJ(uobj));

	/*
	 * if noswap flag is set, then we never return a slot
	 */
	if (aobj->u_flags & UAO_FLAG_NOSWAP)
		return 0;

	/*
	 * if hashing, look in hash table.
	 */
	if (UAO_USES_SWHASH(aobj)) {
		struct uao_swhash_elt *elt =
		    uao_find_swhash_elt(aobj, pageidx, FALSE);

		if (elt)
			return UAO_SWHASH_ELT_PAGESLOT(elt, pageidx);
		else
			return 0;
	}

	/*
	 * otherwise, look in the array
	 */
	return aobj->u_swslots[pageidx];
}

/*
 * uao_set_swslot: set the swap slot for a page in an aobj.
 *
 * => setting a slot to zero frees the slot
 * => object must be locked by caller
 * => we return the old slot number, or -1 if we failed to allocate
 *    memory to record the new slot number
 */
int
uao_set_swslot(struct uvm_object *uobj, int pageidx, int slot)
{
	struct uvm_aobj *aobj = (struct uvm_aobj *)uobj;
	int oldslot;

	KASSERT(rw_write_held(uobj->vmobjlock) || uobj->uo_refs == 0);
	KASSERT(UVM_OBJ_IS_AOBJ(uobj));

	/*
	 * if noswap flag is set, then we can't set a slot
	 */
	if (aobj->u_flags & UAO_FLAG_NOSWAP) {
		if (slot == 0)
			return 0;		/* a clear is ok */

		/* but a set is not */
		printf("uao_set_swslot: uobj = %p\n", uobj);
	    	panic("uao_set_swslot: attempt to set a slot on a NOSWAP object");
	}

	/*
	 * are we using a hash table?  if so, add it in the hash.
	 */
	if (UAO_USES_SWHASH(aobj)) {
		/*
		 * Avoid allocating an entry just to free it again if
		 * the page had not swap slot in the first place, and
		 * we are freeing.
		 */
		struct uao_swhash_elt *elt =
		    uao_find_swhash_elt(aobj, pageidx, slot ? TRUE : FALSE);
		if (elt == NULL) {
			KASSERT(slot == 0);
			return 0;
		}

		oldslot = UAO_SWHASH_ELT_PAGESLOT(elt, pageidx);
		UAO_SWHASH_ELT_PAGESLOT(elt, pageidx) = slot;

		/*
		 * now adjust the elt's reference counter and free it if we've
		 * dropped it to zero.
		 */
		if (slot) {
			if (oldslot == 0)
				elt->count++;
		} else {
			if (oldslot)
				elt->count--;

			if (elt->count == 0) {
				LIST_REMOVE(elt, list);
				pool_put(&uao_swhash_elt_pool, elt);
			}
		}
	} else {
		/* we are using an array */
		oldslot = aobj->u_swslots[pageidx];
		aobj->u_swslots[pageidx] = slot;
	}
	return oldslot;
}
/*
 * end of hash/array functions
 */

/*
 * uao_free: free all resources held by an aobj, and then free the aobj
 *
 * => the aobj should be dead
 */
static void
uao_free(struct uvm_aobj *aobj)
{
	struct uvm_object *uobj = &aobj->u_obj;

	KASSERT(UVM_OBJ_IS_AOBJ(uobj));
	KASSERT(rw_write_held(uobj->vmobjlock));
	uao_dropswap_range(uobj, 0, 0);
	rw_exit(uobj->vmobjlock);

	if (UAO_USES_SWHASH(aobj)) {
		/*
		 * free the hash table itself.
		 */
		hashfree(aobj->u_swhash, UAO_SWHASH_BUCKETS(aobj->u_pages), M_UVMAOBJ);
	} else {
		free(aobj->u_swslots, M_UVMAOBJ, aobj->u_pages * sizeof(int));
	}

	/*
	 * finally free the aobj itself
	 */
	uvm_obj_destroy(uobj);
	pool_put(&uvm_aobj_pool, aobj);
}

/*
 * pager functions
 */

#ifdef TMPFS
/*
 * Shrink an aobj to a given number of pages. The procedure is always the same:
 * assess the necessity of data structure conversion (hash to array), secure
 * resources, flush pages and drop swap slots.
 *
 */

void
uao_shrink_flush(struct uvm_object *uobj, int startpg, int endpg)
{
	KASSERT(startpg < endpg);
	KASSERT(uobj->uo_refs == 1);
	uao_flush(uobj, (voff_t)startpg << PAGE_SHIFT,
	    (voff_t)endpg << PAGE_SHIFT, PGO_FREE);
	uao_dropswap_range(uobj, startpg, endpg);
}

int
uao_shrink_hash(struct uvm_object *uobj, int pages)
{
	struct uvm_aobj *aobj = (struct uvm_aobj *)uobj;
	struct uao_swhash *new_swhash;
	struct uao_swhash_elt *elt;
	unsigned long new_hashmask;
	int i;

	KASSERT(UAO_USES_SWHASH(aobj));

	/*
	 * If the size of the hash table doesn't change, all we need to do is
	 * to adjust the page count.
	 */
	if (UAO_SWHASH_BUCKETS(aobj->u_pages) == UAO_SWHASH_BUCKETS(pages)) {
		uao_shrink_flush(uobj, pages, aobj->u_pages);
		aobj->u_pages = pages;
		return 0;
	}

	new_swhash = hashinit(UAO_SWHASH_BUCKETS(pages), M_UVMAOBJ,
	    M_WAITOK | M_CANFAIL, &new_hashmask);
	if (new_swhash == NULL)
		return ENOMEM;

	uao_shrink_flush(uobj, pages, aobj->u_pages);

	/*
	 * Even though the hash table size is changing, the hash of the buckets
	 * we are interested in copying should not change.
	 */
	for (i = 0; i < UAO_SWHASH_BUCKETS(aobj->u_pages); i++) {
		while (LIST_EMPTY(&aobj->u_swhash[i]) == 0) {
			elt = LIST_FIRST(&aobj->u_swhash[i]);
			LIST_REMOVE(elt, list);
			LIST_INSERT_HEAD(&new_swhash[i], elt, list);
		}
	}

	hashfree(aobj->u_swhash, UAO_SWHASH_BUCKETS(aobj->u_pages), M_UVMAOBJ);

	aobj->u_swhash = new_swhash;
	aobj->u_pages = pages;
	aobj->u_swhashmask = new_hashmask;

	return 0;
}

int
uao_shrink_convert(struct uvm_object *uobj, int pages)
{
	struct uvm_aobj *aobj = (struct uvm_aobj *)uobj;
	struct uao_swhash_elt *elt;
	int i, *new_swslots;

	new_swslots = mallocarray(pages, sizeof(int), M_UVMAOBJ,
	    M_WAITOK | M_CANFAIL | M_ZERO);
	if (new_swslots == NULL)
		return ENOMEM;

	uao_shrink_flush(uobj, pages, aobj->u_pages);

	/* Convert swap slots from hash to array.  */
	for (i = 0; i < pages; i++) {
		elt = uao_find_swhash_elt(aobj, i, FALSE);
		if (elt != NULL) {
			new_swslots[i] = UAO_SWHASH_ELT_PAGESLOT(elt, i);
			if (new_swslots[i] != 0)
				elt->count--;
			if (elt->count == 0) {
				LIST_REMOVE(elt, list);
				pool_put(&uao_swhash_elt_pool, elt);
			}
		}
	}

	hashfree(aobj->u_swhash, UAO_SWHASH_BUCKETS(aobj->u_pages), M_UVMAOBJ);

	aobj->u_swslots = new_swslots;
	aobj->u_pages = pages;

	return 0;
}

int
uao_shrink_array(struct uvm_object *uobj, int pages)
{
	struct uvm_aobj *aobj = (struct uvm_aobj *)uobj;
	int i, *new_swslots;

	new_swslots = mallocarray(pages, sizeof(int), M_UVMAOBJ,
	    M_WAITOK | M_CANFAIL | M_ZERO);
	if (new_swslots == NULL)
		return ENOMEM;

	uao_shrink_flush(uobj, pages, aobj->u_pages);

	for (i = 0; i < pages; i++)
		new_swslots[i] = aobj->u_swslots[i];

	free(aobj->u_swslots, M_UVMAOBJ, aobj->u_pages * sizeof(int));

	aobj->u_swslots = new_swslots;
	aobj->u_pages = pages;

	return 0;
}

int
uao_shrink(struct uvm_object *uobj, int pages)
{
	struct uvm_aobj *aobj = (struct uvm_aobj *)uobj;

	KASSERT(pages < aobj->u_pages);

	/*
	 * Distinguish between three possible cases:
	 * 1. aobj uses hash and must be converted to array.
	 * 2. aobj uses array and array size needs to be adjusted.
	 * 3. aobj uses hash and hash size needs to be adjusted.
	 */
	if (pages > UAO_SWHASH_THRESHOLD)
		return uao_shrink_hash(uobj, pages);	/* case 3 */
	else if (aobj->u_pages > UAO_SWHASH_THRESHOLD)
		return uao_shrink_convert(uobj, pages);	/* case 1 */
	else
		return uao_shrink_array(uobj, pages);	/* case 2 */
}

/*
 * Grow an aobj to a given number of pages. Right now we only adjust the swap
 * slots. We could additionally handle page allocation directly, so that they
 * don't happen through uvm_fault(). That would allow us to use another
 * mechanism for the swap slots other than malloc(). It is thus mandatory that
 * the caller of these functions does not allow faults to happen in case of
 * growth error.
 */
int
uao_grow_array(struct uvm_object *uobj, int pages)
{
	struct uvm_aobj *aobj = (struct uvm_aobj *)uobj;
	int i, *new_swslots;

	KASSERT(aobj->u_pages <= UAO_SWHASH_THRESHOLD);

	new_swslots = mallocarray(pages, sizeof(int), M_UVMAOBJ,
	    M_WAITOK | M_CANFAIL | M_ZERO);
	if (new_swslots == NULL)
		return ENOMEM;

	for (i = 0; i < aobj->u_pages; i++)
		new_swslots[i] = aobj->u_swslots[i];

	free(aobj->u_swslots, M_UVMAOBJ, aobj->u_pages * sizeof(int));

	aobj->u_swslots = new_swslots;
	aobj->u_pages = pages;

	return 0;
}

int
uao_grow_hash(struct uvm_object *uobj, int pages)
{
	struct uvm_aobj *aobj = (struct uvm_aobj *)uobj;
	struct uao_swhash *new_swhash;
	struct uao_swhash_elt *elt;
	unsigned long new_hashmask;
	int i;

	KASSERT(pages > UAO_SWHASH_THRESHOLD);

	/*
	 * If the size of the hash table doesn't change, all we need to do is
	 * to adjust the page count.
	 */
	if (UAO_SWHASH_BUCKETS(aobj->u_pages) == UAO_SWHASH_BUCKETS(pages)) {
		aobj->u_pages = pages;
		return 0;
	}

	KASSERT(UAO_SWHASH_BUCKETS(aobj->u_pages) < UAO_SWHASH_BUCKETS(pages));

	new_swhash = hashinit(UAO_SWHASH_BUCKETS(pages), M_UVMAOBJ,
	    M_WAITOK | M_CANFAIL, &new_hashmask);
	if (new_swhash == NULL)
		return ENOMEM;

	for (i = 0; i < UAO_SWHASH_BUCKETS(aobj->u_pages); i++) {
		while (LIST_EMPTY(&aobj->u_swhash[i]) == 0) {
			elt = LIST_FIRST(&aobj->u_swhash[i]);
			LIST_REMOVE(elt, list);
			LIST_INSERT_HEAD(&new_swhash[i], elt, list);
		}
	}

	hashfree(aobj->u_swhash, UAO_SWHASH_BUCKETS(aobj->u_pages), M_UVMAOBJ);

	aobj->u_swhash = new_swhash;
	aobj->u_pages = pages;
	aobj->u_swhashmask = new_hashmask;

	return 0;
}

int
uao_grow_convert(struct uvm_object *uobj, int pages)
{
	struct uvm_aobj *aobj = (struct uvm_aobj *)uobj;
	struct uao_swhash *new_swhash;
	struct uao_swhash_elt *elt;
	unsigned long new_hashmask;
	int i, *old_swslots;

	new_swhash = hashinit(UAO_SWHASH_BUCKETS(pages), M_UVMAOBJ,
	    M_WAITOK | M_CANFAIL, &new_hashmask);
	if (new_swhash == NULL)
		return ENOMEM;

	/* Set these now, so we can use uao_find_swhash_elt(). */
	old_swslots = aobj->u_swslots;
	aobj->u_swhash = new_swhash;		
	aobj->u_swhashmask = new_hashmask;

	for (i = 0; i < aobj->u_pages; i++) {
		if (old_swslots[i] != 0) {
			elt = uao_find_swhash_elt(aobj, i, TRUE);
			elt->count++;
			UAO_SWHASH_ELT_PAGESLOT(elt, i) = old_swslots[i];
		}
	}

	free(old_swslots, M_UVMAOBJ, aobj->u_pages * sizeof(int));
	aobj->u_pages = pages;

	return 0;
}

int
uao_grow(struct uvm_object *uobj, int pages)
{
	struct uvm_aobj *aobj = (struct uvm_aobj *)uobj;

	KASSERT(pages > aobj->u_pages);

	/*
	 * Distinguish between three possible cases:
	 * 1. aobj uses hash and hash size needs to be adjusted.
	 * 2. aobj uses array and array size needs to be adjusted.
	 * 3. aobj uses array and must be converted to hash.
	 */
	if (pages <= UAO_SWHASH_THRESHOLD)
		return uao_grow_array(uobj, pages);	/* case 2 */
	else if (aobj->u_pages > UAO_SWHASH_THRESHOLD)
		return uao_grow_hash(uobj, pages);	/* case 1 */
	else
		return uao_grow_convert(uobj, pages);
}
#endif /* TMPFS */

/*
 * uao_create: create an aobj of the given size and return its uvm_object.
 *
 * => for normal use, flags are zero or UAO_FLAG_CANFAIL.
 * => for the kernel object, the flags are:
 *	UAO_FLAG_KERNOBJ - allocate the kernel object (can only happen once)
 *	UAO_FLAG_KERNSWAP - enable swapping of kernel object ("           ")
 */
struct uvm_object *
uao_create(vsize_t size, int flags)
{
	static struct uvm_aobj kernel_object_store;
	static struct rwlock bootstrap_kernel_object_lock;
	static int kobj_alloced = 0;
	int pages = round_page(size) >> PAGE_SHIFT;
	struct uvm_aobj *aobj;
	int refs;

	/*
	 * Allocate a new aobj, unless kernel object is requested.
	 */
	if (flags & UAO_FLAG_KERNOBJ) {
		KASSERT(!kobj_alloced);
		aobj = &kernel_object_store;
		aobj->u_pages = pages;
		aobj->u_flags = UAO_FLAG_NOSWAP;
		refs = UVM_OBJ_KERN;
		kobj_alloced = UAO_FLAG_KERNOBJ;
	} else if (flags & UAO_FLAG_KERNSWAP) {
		KASSERT(kobj_alloced == UAO_FLAG_KERNOBJ);
		aobj = &kernel_object_store;
		kobj_alloced = UAO_FLAG_KERNSWAP;
	} else {
		aobj = pool_get(&uvm_aobj_pool, PR_WAITOK);
		aobj->u_pages = pages;
		aobj->u_flags = 0;
		refs = 1;
	}

	/*
	 * allocate hash/array if necessary
	 */
 	if (flags == 0 || (flags & (UAO_FLAG_KERNSWAP | UAO_FLAG_CANFAIL))) {
		int mflags;

		if (flags)
			mflags = M_NOWAIT;
		else
			mflags = M_WAITOK;

		/* allocate hash table or array depending on object size */
		if (UAO_USES_SWHASH(aobj)) {
			aobj->u_swhash = hashinit(UAO_SWHASH_BUCKETS(pages),
			    M_UVMAOBJ, mflags, &aobj->u_swhashmask);
			if (aobj->u_swhash == NULL) {
				if (flags & UAO_FLAG_CANFAIL) {
					pool_put(&uvm_aobj_pool, aobj);
					return NULL;
				}
				panic("uao_create: hashinit swhash failed");
			}
		} else {
			aobj->u_swslots = mallocarray(pages, sizeof(int),
			    M_UVMAOBJ, mflags|M_ZERO);
			if (aobj->u_swslots == NULL) {
				if (flags & UAO_FLAG_CANFAIL) {
					pool_put(&uvm_aobj_pool, aobj);
					return NULL;
				}
				panic("uao_create: malloc swslots failed");
			}
		}

		if (flags & UAO_FLAG_KERNSWAP) {
			aobj->u_flags &= ~UAO_FLAG_NOSWAP; /* clear noswap */
			return &aobj->u_obj;
			/* done! */
		}
	}

	/*
	 * Initialise UVM object.
	 */
	uvm_obj_init(&aobj->u_obj, &aobj_pager, refs);
	if (flags & UAO_FLAG_KERNOBJ) {
		/* Use a temporary static lock for kernel_object. */
		rw_init(&bootstrap_kernel_object_lock, "kobjlk");
		uvm_obj_setlock(&aobj->u_obj, &bootstrap_kernel_object_lock);
	}

	/*
 	 * now that aobj is ready, add it to the global list
 	 */
	mtx_enter(&uao_list_lock);
	LIST_INSERT_HEAD(&uao_list, aobj, u_list);
	mtx_leave(&uao_list_lock);

	return &aobj->u_obj;
}



/*
 * uao_init: set up aobj pager subsystem
 *
 * => called at boot time from uvm_pager_init()
 */
void
uao_init(void)
{
	/*
	 * NOTE: Pages for this pool must not come from a pageable
	 * kernel map!
	 */
	pool_init(&uao_swhash_elt_pool, sizeof(struct uao_swhash_elt), 0,
	    IPL_NONE, PR_WAITOK, "uaoeltpl", NULL);
	pool_init(&uvm_aobj_pool, sizeof(struct uvm_aobj), 0,
	    IPL_NONE, PR_WAITOK, "aobjpl", NULL);
}

/*
 * uao_reference: hold a reference to an anonymous UVM object.
 */
void
uao_reference(struct uvm_object *uobj)
{
	/* Kernel object is persistent. */
	if (UVM_OBJ_IS_KERN_OBJECT(uobj))
		return;

	atomic_inc_int(&uobj->uo_refs);
}


/*
 * uao_detach: drop a reference to an anonymous UVM object.
 */
void
uao_detach(struct uvm_object *uobj)
{
	struct uvm_aobj *aobj = (struct uvm_aobj *)uobj;
	struct vm_page *pg;

	/*
	 * Detaching from kernel_object is a NOP.
	 */
	if (UVM_OBJ_IS_KERN_OBJECT(uobj))
		return;

	/*
	 * Drop the reference.  If it was the last one, destroy the object.
	 */
	if (atomic_dec_int_nv(&uobj->uo_refs) > 0) {
		return;
	}

	/*
	 * Remove the aobj from the global list.
	 */
	mtx_enter(&uao_list_lock);
	LIST_REMOVE(aobj, u_list);
	mtx_leave(&uao_list_lock);

	/*
	 * Free all the pages left in the aobj.  For each page, when the
	 * page is no longer busy (and thus after any disk I/O that it is
	 * involved in is complete), release any swap resources and free
	 * the page itself.
	 */
	rw_enter(uobj->vmobjlock, RW_WRITE);
	while ((pg = RBT_ROOT(uvm_objtree, &uobj->memt)) != NULL) {
		pmap_page_protect(pg, PROT_NONE);
		if (pg->pg_flags & PG_BUSY) {
			uvm_pagewait(pg, uobj->vmobjlock, "uao_det");
			rw_enter(uobj->vmobjlock, RW_WRITE);
			continue;
		}
		uao_dropswap(&aobj->u_obj, pg->offset >> PAGE_SHIFT);
		uvm_pagefree(pg);
	}

	/*
	 * Finally, free the anonymous UVM object itself.
	 */
	uao_free(aobj);
}

/*
 * uao_flush: flush pages out of a uvm object
 *
 * => if PGO_CLEANIT is not set, then we will not block.
 * => if PGO_ALLPAGE is set, then all pages in the object are valid targets
 *	for flushing.
 * => NOTE: we are allowed to lock the page queues, so the caller
 *	must not be holding the lock on them [e.g. pagedaemon had
 *	better not call us with the queues locked]
 * => we return TRUE unless we encountered some sort of I/O error
 *	XXXJRT currently never happens, as we never directly initiate
 *	XXXJRT I/O
 */
boolean_t
uao_flush(struct uvm_object *uobj, voff_t start, voff_t stop, int flags)
{
	struct uvm_aobj *aobj = (struct uvm_aobj *) uobj;
	struct vm_page *pg;
	voff_t curoff;

	KASSERT(UVM_OBJ_IS_AOBJ(uobj));
	KASSERT(rw_write_held(uobj->vmobjlock));

	if (flags & PGO_ALLPAGES) {
		start = 0;
		stop = (voff_t)aobj->u_pages << PAGE_SHIFT;
	} else {
		start = trunc_page(start);
		stop = round_page(stop);
		if (stop > ((voff_t)aobj->u_pages << PAGE_SHIFT)) {
			printf("uao_flush: strange, got an out of range "
			    "flush (fixed)\n");
			stop = (voff_t)aobj->u_pages << PAGE_SHIFT;
		}
	}

	/*
	 * Don't need to do any work here if we're not freeing
	 * or deactivating pages.
	 */
	if ((flags & (PGO_DEACTIVATE|PGO_FREE)) == 0) {
		return TRUE;
	}

	curoff = start;
	for (;;) {
		if (curoff < stop) {
			pg = uvm_pagelookup(uobj, curoff);
			curoff += PAGE_SIZE;
			if (pg == NULL)
				continue;
		} else {
			break;
		}

		/* Make sure page is unbusy, else wait for it. */
		if (pg->pg_flags & PG_BUSY) {
			uvm_pagewait(pg, uobj->vmobjlock, "uaoflsh");
			rw_enter(uobj->vmobjlock, RW_WRITE);
			curoff -= PAGE_SIZE;
			continue;
		}

		switch (flags & (PGO_CLEANIT|PGO_FREE|PGO_DEACTIVATE)) {
		/*
		 * XXX In these first 3 cases, we always just
		 * XXX deactivate the page.  We may want to
		 * XXX handle the different cases more specifically
		 * XXX in the future.
		 */
		case PGO_CLEANIT|PGO_FREE:
			/* FALLTHROUGH */
		case PGO_CLEANIT|PGO_DEACTIVATE:
			/* FALLTHROUGH */
		case PGO_DEACTIVATE:
 deactivate_it:
			if (pg->wire_count != 0)
				continue;

			uvm_lock_pageq();
			uvm_pagedeactivate(pg);
			uvm_unlock_pageq();

			continue;
		case PGO_FREE:
			/*
			 * If there are multiple references to
			 * the object, just deactivate the page.
			 */
			if (uobj->uo_refs > 1)
				goto deactivate_it;

			/* XXX skip the page if it's wired */
			if (pg->wire_count != 0)
				continue;

			/*
			 * free the swap slot and the page.
			 */
			pmap_page_protect(pg, PROT_NONE);

			/*
			 * freeing swapslot here is not strictly necessary.
			 * however, leaving it here doesn't save much
			 * because we need to update swap accounting anyway.
			 */
			uao_dropswap(uobj, pg->offset >> PAGE_SHIFT);
			uvm_pagefree(pg);
			continue;
		default:
			panic("uao_flush: weird flags");
		}
	}

	return TRUE;
}

/*
 * uao_get: fetch me a page
 *
 * we have three cases:
 * 1: page is resident     -> just return the page.
 * 2: page is zero-fill    -> allocate a new page and zero it.
 * 3: page is swapped out  -> fetch the page from swap.
 *
 * cases 1 can be handled with PGO_LOCKED, cases 2 and 3 cannot.
 * so, if the "center" page hits case 3 (or any page, with PGO_ALLPAGES),
 * then we will need to return VM_PAGER_UNLOCK.
 *
 * => flags: PGO_ALLPAGES: get all of the pages
 *           PGO_LOCKED: fault data structures are locked
 * => NOTE: offset is the offset of pps[0], _NOT_ pps[centeridx]
 * => NOTE: caller must check for released pages!!
 */
static int
uao_get(struct uvm_object *uobj, voff_t offset, struct vm_page **pps,
    int *npagesp, int centeridx, vm_prot_t access_type, int advice, int flags)
{
	struct uvm_aobj *aobj = (struct uvm_aobj *)uobj;
	voff_t current_offset;
	vm_page_t ptmp;
	int lcv, gotpages, maxpages, swslot, rv, pageidx;
	boolean_t done;

	KASSERT(UVM_OBJ_IS_AOBJ(uobj));
	KASSERT(rw_lock_held(uobj->vmobjlock));
	KASSERT(rw_write_held(uobj->vmobjlock) ||
	    ((flags & PGO_LOCKED) != 0 && (access_type & PROT_WRITE) == 0));

	/*
 	 * get number of pages
 	 */
	maxpages = *npagesp;

	if (flags & PGO_LOCKED) {
		/*
 		 * step 1a: get pages that are already resident.   only do
		 * this if the data structures are locked (i.e. the first
		 * time through).
 		 */
		done = TRUE;	/* be optimistic */
		gotpages = 0;	/* # of pages we got so far */

		for (lcv = 0, current_offset = offset ; lcv < maxpages ;
		    lcv++, current_offset += PAGE_SIZE) {
			/* do we care about this page?  if not, skip it */
			if (pps[lcv] == PGO_DONTCARE)
				continue;

			/* lookup page */
			ptmp = uvm_pagelookup(uobj, current_offset);

			/*
			 * to be useful must get a non-busy page
			 */
			if (ptmp == NULL || (ptmp->pg_flags & PG_BUSY) != 0) {
				if (lcv == centeridx ||
				    (flags & PGO_ALLPAGES) != 0)
					/* need to do a wait or I/O! */
					done = FALSE;
				continue;
			}

			/*
			 * useful page: plug it in our result array
			 */
			pps[lcv] = ptmp;
			gotpages++;
		}

		/*
 		 * step 1b: now we've either done everything needed or we
		 * to unlock and do some waiting or I/O.
 		 */
		*npagesp = gotpages;
		return done ? VM_PAGER_OK : VM_PAGER_UNLOCK;
	}

	/*
 	 * step 2: get non-resident or busy pages.
 	 * data structures are unlocked.
 	 */
	for (lcv = 0, current_offset = offset ; lcv < maxpages ;
	    lcv++, current_offset += PAGE_SIZE) {
		/*
		 * - skip over pages we've already gotten or don't want
		 * - skip over pages we don't _have_ to get
		 */
		if (pps[lcv] != NULL ||
		    (lcv != centeridx && (flags & PGO_ALLPAGES) == 0))
			continue;

		pageidx = current_offset >> PAGE_SHIFT;

		/*
 		 * we have yet to locate the current page (pps[lcv]).   we
		 * first look for a page that is already at the current offset.
		 * if we find a page, we check to see if it is busy or
		 * released.  if that is the case, then we sleep on the page
		 * until it is no longer busy or released and repeat the lookup.
		 * if the page we found is neither busy nor released, then we
		 * busy it (so we own it) and plug it into pps[lcv].   this
		 * 'break's the following while loop and indicates we are
		 * ready to move on to the next page in the "lcv" loop above.
 		 *
 		 * if we exit the while loop with pps[lcv] still set to NULL,
		 * then it means that we allocated a new busy/fake/clean page
		 * ptmp in the object and we need to do I/O to fill in the data.
 		 */

		/* top of "pps" while loop */
		while (pps[lcv] == NULL) {
			/* look for a resident page */
			ptmp = uvm_pagelookup(uobj, current_offset);

			/* not resident?   allocate one now (if we can) */
			if (ptmp == NULL) {

				ptmp = uvm_pagealloc(uobj, current_offset,
				    NULL, 0);

				/* out of RAM? */
				if (ptmp == NULL) {
					rw_exit(uobj->vmobjlock);
					uvm_wait("uao_getpage");
					rw_enter(uobj->vmobjlock, RW_WRITE);
					/* goto top of pps while loop */
					continue;
				}

				/*
				 * safe with PQ's unlocked: because we just
				 * alloc'd the page
				 */
				atomic_setbits_int(&ptmp->pg_flags, PQ_AOBJ);

				/* 
				 * got new page ready for I/O.  break pps while
				 * loop.  pps[lcv] is still NULL.
				 */
				break;
			}

			/* page is there, see if we need to wait on it */
			if ((ptmp->pg_flags & PG_BUSY) != 0) {
				uvm_pagewait(ptmp, uobj->vmobjlock, "uao_get");
				rw_enter(uobj->vmobjlock, RW_WRITE);
				continue;	/* goto top of pps while loop */
			}

			/*
 			 * if we get here then the page is resident and
			 * unbusy.  we busy it now (so we own it).
 			 */
			/* we own it, caller must un-busy */
			atomic_setbits_int(&ptmp->pg_flags, PG_BUSY);
			UVM_PAGE_OWN(ptmp, "uao_get2");
			pps[lcv] = ptmp;
		}

		/*
 		 * if we own the valid page at the correct offset, pps[lcv] will
 		 * point to it.   nothing more to do except go to the next page.
 		 */
		if (pps[lcv])
			continue;			/* next lcv */

		/*
 		 * we have a "fake/busy/clean" page that we just allocated.  
 		 * do the needed "i/o", either reading from swap or zeroing.
 		 */
		swslot = uao_find_swslot(uobj, pageidx);

		/* just zero the page if there's nothing in swap.  */
		if (swslot == 0) {
			/* page hasn't existed before, just zero it. */
			uvm_pagezero(ptmp);
		} else {
			/*
			 * page in the swapped-out page.
			 * unlock object for i/o, relock when done.
			 */

			rw_exit(uobj->vmobjlock);
			rv = uvm_swap_get(ptmp, swslot, PGO_SYNCIO);
			rw_enter(uobj->vmobjlock, RW_WRITE);

			/*
			 * I/O done.  check for errors.
			 */
			if (rv != VM_PAGER_OK) {
				/*
				 * remove the swap slot from the aobj
				 * and mark the aobj as having no real slot.
				 * don't free the swap slot, thus preventing
				 * it from being used again.
				 */
				swslot = uao_set_swslot(&aobj->u_obj, pageidx,
							SWSLOT_BAD);
				uvm_swap_markbad(swslot, 1);

				if (ptmp->pg_flags & PG_WANTED)
					wakeup(ptmp);
				atomic_clearbits_int(&ptmp->pg_flags,
				    PG_WANTED|PG_BUSY);
				UVM_PAGE_OWN(ptmp, NULL);
				uvm_pagefree(ptmp);
				rw_exit(uobj->vmobjlock);

				return rv;
			}
		}

		/*
 		 * we got the page!   clear the fake flag (indicates valid
		 * data now in page) and plug into our result array.   note
		 * that page is still busy.
 		 *
 		 * it is the callers job to:
 		 * => check if the page is released
 		 * => unbusy the page
 		 * => activate the page
 		 */
		atomic_clearbits_int(&ptmp->pg_flags, PG_FAKE);
		pmap_clear_modify(ptmp);		/* ... and clean */
		pps[lcv] = ptmp;

	}	/* lcv loop */

	rw_exit(uobj->vmobjlock);
	return VM_PAGER_OK;
}

/*
 * uao_dropswap:  release any swap resources from this aobj page.
 *
 * => aobj must be locked or have a reference count of 0.
 */
int
uao_dropswap(struct uvm_object *uobj, int pageidx)
{
	int slot;

	KASSERT(UVM_OBJ_IS_AOBJ(uobj));

	slot = uao_set_swslot(uobj, pageidx, 0);
	if (slot) {
		uvm_swap_free(slot, 1);
	}
	return slot;
}

/*
 * page in every page in every aobj that is paged-out to a range of swslots.
 * 
 * => aobj must be locked and is returned locked.
 * => returns TRUE if pagein was aborted due to lack of memory.
 */
boolean_t
uao_swap_off(int startslot, int endslot)
{
	struct uvm_aobj *aobj;

	/*
	 * Walk the list of all anonymous UVM objects.  Grab the first.
	 */
	mtx_enter(&uao_list_lock);
	if ((aobj = LIST_FIRST(&uao_list)) == NULL) {
		mtx_leave(&uao_list_lock);
		return FALSE;
	}
	uao_reference(&aobj->u_obj);

	do {
		struct uvm_aobj *nextaobj;
		boolean_t rv;

		/*
		 * Prefetch the next object and immediately hold a reference
		 * on it, so neither the current nor the next entry could
		 * disappear while we are iterating.
		 */
		if ((nextaobj = LIST_NEXT(aobj, u_list)) != NULL) {
			uao_reference(&nextaobj->u_obj);
		}
		mtx_leave(&uao_list_lock);

		/*
		 * Page in all pages in the swap slot range.
		 */
		rw_enter(aobj->u_obj.vmobjlock, RW_WRITE);
		rv = uao_pagein(aobj, startslot, endslot);
		rw_exit(aobj->u_obj.vmobjlock);

		/* Drop the reference of the current object. */
		uao_detach(&aobj->u_obj);
		if (rv) {
			if (nextaobj) {
				uao_detach(&nextaobj->u_obj);
			}
			return rv;
		}

		aobj = nextaobj;
		mtx_enter(&uao_list_lock);
	} while (aobj);

	/*
	 * done with traversal, unlock the list
	 */
	mtx_leave(&uao_list_lock);
	return FALSE;
}

/*
 * page in any pages from aobj in the given range.
 *
 * => returns TRUE if pagein was aborted due to lack of memory.
 */
static boolean_t
uao_pagein(struct uvm_aobj *aobj, int startslot, int endslot)
{
	boolean_t rv;

	if (UAO_USES_SWHASH(aobj)) {
		struct uao_swhash_elt *elt;
		int bucket;

restart:
		for (bucket = aobj->u_swhashmask; bucket >= 0; bucket--) {
			for (elt = LIST_FIRST(&aobj->u_swhash[bucket]);
			     elt != NULL;
			     elt = LIST_NEXT(elt, list)) {
				int i;

				for (i = 0; i < UAO_SWHASH_CLUSTER_SIZE; i++) {
					int slot = elt->slots[i];

					/*
					 * if the slot isn't in range, skip it.
					 */
					if (slot < startslot ||
					    slot >= endslot) {
						continue;
					}

					/*
					 * process the page,
					 * the start over on this object
					 * since the swhash elt
					 * may have been freed.
					 */
					rv = uao_pagein_page(aobj,
					  UAO_SWHASH_ELT_PAGEIDX_BASE(elt) + i);
					if (rv) {
						return rv;
					}
					goto restart;
				}
			}
		}
	} else {
		int i;

		for (i = 0; i < aobj->u_pages; i++) {
			int slot = aobj->u_swslots[i];

			/*
			 * if the slot isn't in range, skip it
			 */
			if (slot < startslot || slot >= endslot) {
				continue;
			}

			/*
			 * process the page.
			 */
			rv = uao_pagein_page(aobj, i);
			if (rv) {
				return rv;
			}
		}
	}

	return FALSE;
}

/*
 * uao_pagein_page: page in a single page from an anonymous UVM object.
 *
 * => Returns TRUE if pagein was aborted due to lack of memory.
 */
static boolean_t
uao_pagein_page(struct uvm_aobj *aobj, int pageidx)
{
	struct uvm_object *uobj = &aobj->u_obj;
	struct vm_page *pg;
	int rv, npages;

	pg = NULL;
	npages = 1;

	KASSERT(rw_write_held(uobj->vmobjlock));
	rv = uao_get(&aobj->u_obj, (voff_t)pageidx << PAGE_SHIFT,
	    &pg, &npages, 0, PROT_READ | PROT_WRITE, 0, 0);

	/*
	 * relock and finish up.
	 */
	rw_enter(uobj->vmobjlock, RW_WRITE);
	switch (rv) {
	case VM_PAGER_OK:
		break;

	case VM_PAGER_ERROR:
	case VM_PAGER_REFAULT:
		/*
		 * nothing more to do on errors.
		 * VM_PAGER_REFAULT can only mean that the anon was freed,
		 * so again there's nothing to do.
		 */
		return FALSE;
	}

	/*
	 * ok, we've got the page now.
	 * mark it as dirty, clear its swslot and un-busy it.
	 */
	uao_dropswap(&aobj->u_obj, pageidx);
	atomic_clearbits_int(&pg->pg_flags, PG_BUSY|PG_CLEAN|PG_FAKE);
	UVM_PAGE_OWN(pg, NULL);

	/*
	 * deactivate the page (to put it on a page queue).
	 */
	uvm_lock_pageq();
	uvm_pagedeactivate(pg);
	uvm_unlock_pageq();

	return FALSE;
}

/*
 * uao_dropswap_range: drop swapslots in the range.
 *
 * => aobj must be locked and is returned locked.
 * => start is inclusive.  end is exclusive.
 */
void
uao_dropswap_range(struct uvm_object *uobj, voff_t start, voff_t end)
{
	struct uvm_aobj *aobj = (struct uvm_aobj *)uobj;
	int swpgonlydelta = 0;

	KASSERT(UVM_OBJ_IS_AOBJ(uobj));
	KASSERT(rw_write_held(uobj->vmobjlock));

	if (end == 0) {
		end = INT64_MAX;
	}

	if (UAO_USES_SWHASH(aobj)) {
		int i, hashbuckets = aobj->u_swhashmask + 1;
		voff_t taghi;
		voff_t taglo;

		taglo = UAO_SWHASH_ELT_TAG(start);
		taghi = UAO_SWHASH_ELT_TAG(end);

		for (i = 0; i < hashbuckets; i++) {
			struct uao_swhash_elt *elt, *next;

			for (elt = LIST_FIRST(&aobj->u_swhash[i]);
			     elt != NULL;
			     elt = next) {
				int startidx, endidx;
				int j;

				next = LIST_NEXT(elt, list);

				if (elt->tag < taglo || taghi < elt->tag) {
					continue;
				}

				if (elt->tag == taglo) {
					startidx =
					    UAO_SWHASH_ELT_PAGESLOT_IDX(start);
				} else {
					startidx = 0;
				}

				if (elt->tag == taghi) {
					endidx =
					    UAO_SWHASH_ELT_PAGESLOT_IDX(end);
				} else {
					endidx = UAO_SWHASH_CLUSTER_SIZE;
				}

				for (j = startidx; j < endidx; j++) {
					int slot = elt->slots[j];

					KASSERT(uvm_pagelookup(&aobj->u_obj,
					    (voff_t)(UAO_SWHASH_ELT_PAGEIDX_BASE(elt)
					    + j) << PAGE_SHIFT) == NULL);

					if (slot > 0) {
						uvm_swap_free(slot, 1);
						swpgonlydelta++;
						KASSERT(elt->count > 0);
						elt->slots[j] = 0;
						elt->count--;
					}
				}

				if (elt->count == 0) {
					LIST_REMOVE(elt, list);
					pool_put(&uao_swhash_elt_pool, elt);
				}
			}
		}
	} else {
		int i;

		if (aobj->u_pages < end) {
			end = aobj->u_pages;
		}
		for (i = start; i < end; i++) {
			int slot = aobj->u_swslots[i];

			if (slot > 0) {
				uvm_swap_free(slot, 1);
				swpgonlydelta++;
			}
		}
	}

	/*
	 * adjust the counter of pages only in swap for all
	 * the swap slots we've freed.
	 */
	if (swpgonlydelta > 0) {
		KASSERT(uvmexp.swpgonly >= swpgonlydelta);
		atomic_add_int(&uvmexp.swpgonly, -swpgonlydelta);
	}
}
