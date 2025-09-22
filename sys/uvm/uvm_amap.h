/*	$OpenBSD: uvm_amap.h,v 1.36 2025/05/25 01:52:00 gnezdo Exp $	*/
/*	$NetBSD: uvm_amap.h,v 1.14 2001/02/18 21:19:08 chs Exp $	*/

/*
 * Copyright (c) 1997 Charles D. Cranor and Washington University.
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
 */

#ifndef _UVM_UVM_AMAP_H_
#define _UVM_UVM_AMAP_H_

/*
 * uvm_amap.h: general amap interface and amap implementation-specific info
 */

/*
 * an amap structure contains pointers to a set of anons that are
 * mapped together in virtual memory (an anon is a single page of
 * anonymous virtual memory -- see uvm_anon.h).  in uvm we hide the
 * details of the implementation of amaps behind a general amap
 * interface.  this allows us to change the amap implementation
 * without having to touch the rest of the code.  this file is divided
 * into two parts: the definition of the uvm amap interface and the
 * amap implementation-specific definitions.
 */

#ifdef _KERNEL

/*
 * part 1: amap interface
 */

/*
 * forward definition of vm_amap structure.  only amap
 * implementation-specific code should directly access the fields of
 * this structure.  
 */

struct vm_amap;

/*
 * prototypes for the amap interface 
 */

					/* ensure amap can store anon */
void		amap_populate(struct vm_aref *, vaddr_t);
					/* add an anon to an amap */
int		amap_add(struct vm_aref *, vaddr_t, struct vm_anon *,
		    boolean_t);
					/* allocate a new amap */
struct vm_amap	*amap_alloc(vaddr_t, int, int);
					/* clear amap needs-copy flag */
void		amap_copy(vm_map_t, vm_map_entry_t, int, boolean_t, vaddr_t,
		    vaddr_t);
					/* resolve all COW faults now */
void		amap_cow_now(vm_map_t, vm_map_entry_t);
					/* free amap */
void		amap_free(struct vm_amap *);
					/* init amap module (at boot time) */
void		amap_init(void);
					/* lookup an anon @ offset in amap */
struct vm_anon	*amap_lookup(struct vm_aref *, vaddr_t);
					/* lookup multiple anons */
void		amap_lookups(struct vm_aref *, vaddr_t, struct vm_anon **, int);
					/* add a reference to an amap */
void		amap_ref(struct vm_amap *, vaddr_t, vsize_t, int);
					/* split reference to amap into two */
void		amap_splitref(struct vm_aref *, struct vm_aref *, vaddr_t);
					/* remove an anon from an amap */
void		amap_unadd(struct vm_aref *, vaddr_t);
					/* drop reference to an amap */
void		amap_unref(struct vm_amap *, vaddr_t, vsize_t, int);
					/* remove all anons from amap */
void		amap_wipeout(struct vm_amap *);
boolean_t	amap_swap_off(int, int);

/*
 * amap flag values
 */

#define AMAP_SHARED	0x1	/* amap is shared */
#define AMAP_REFALL	0x2	/* amap_ref: reference entire amap */
#define AMAP_SWAPOFF	0x4	/* amap_swap_off() is in progress */

#endif /* _KERNEL */

/**********************************************************************/

/*
 * part 2: amap implementation-specific info
 */

/*
 * we currently provide an array-based amap implementation.  in this
 * implementation we track split references so that we don't lose track of
 * references during partial unmaps.
 */

/*
 * here is the definition of the vm_amap structure and helper structures for
 * this implementation.
 */

struct vm_amap_chunk {
	TAILQ_ENTRY(vm_amap_chunk) ac_list;
	int ac_baseslot;
	uint16_t ac_usedmap;
	uint16_t ac_nslot;
	struct vm_anon *ac_anon[];
};

struct vm_amap {
	struct rwlock *am_lock;	/* lock for all vm_amap flags */
	int am_ref;		/* reference count */
	int am_flags;		/* flags */
	int am_nslot;		/* # of slots currently in map */
	int am_nused;		/* # of slots currently in use */
	int *am_ppref;		/* per page reference count (if !NULL) */
	LIST_ENTRY(vm_amap) am_list;

	union {
		struct {
			struct vm_amap_chunk **amn_buckets;
			TAILQ_HEAD(, vm_amap_chunk) amn_chunks;
			int amn_nbuckets; /* # of buckets */
			int amn_ncused;	/* # of chunkers currently in use */
			int amn_hashshift; /* shift count to hash slot to bucket */
		} ami_normal;

		/*
		 * MUST be last element in vm_amap because it contains a
		 * variably sized array element.
		 */
		struct vm_amap_chunk ami_small;
	} am_impl;

#define am_buckets	am_impl.ami_normal.amn_buckets
#define am_chunks	am_impl.ami_normal.amn_chunks
#define am_nbuckets	am_impl.ami_normal.amn_nbuckets
#define am_ncused	am_impl.ami_normal.amn_ncused
#define am_hashshift	am_impl.ami_normal.amn_hashshift

#define am_small	am_impl.ami_small
};

/*
 * The entries in an amap are called slots. For example an amap that
 * covers four pages is said to have four slots.
 *
 * The slots of an amap are clustered into chunks of UVM_AMAP_CHUNK
 * slots each. The data structure of a chunk is vm_amap_chunk.
 * Every chunk contains an array of pointers to vm_anon, and a bitmap
 * is used to represent which of the slots are in use.
 *
 * Small amaps of up to UVM_AMAP_CHUNK slots have the chunk directly
 * embedded in the amap structure.
 *
 * amaps with more slots are normal amaps and organize chunks in a hash
 * table. The hash table is organized as an array of buckets.
 * All chunks of the amap are additionally stored in a linked list.
 * Chunks that belong to the same hash bucket are stored in the list
 * consecutively. When all slots in a chunk are unused, the chunk is freed.
 *
 * For large amaps, the bucket array can grow large. See the description
 * below how large bucket arrays are avoided.
 */

/*
 * defines for handling of large sparse amaps:
 * 
 * one of the problems of array-based amaps is that if you allocate a
 * large sparsely-used area of virtual memory you end up allocating
 * large arrays that, for the most part, don't get used.  this is a
 * problem for BSD in that the kernel likes to make these types of
 * allocations to "reserve" memory for possible future use.
 *
 * for example, the kernel allocates (reserves) a large chunk of user
 * VM for possible stack growth.  most of the time only a page or two
 * of this VM is actually used.  since the stack is anonymous memory
 * it makes sense for it to live in an amap, but if we allocated an
 * amap for the entire stack range we could end up wasting a large
 * amount of malloc'd KVM.
 * 
 * for example, on the i386 at boot time we allocate two amaps for the stack 
 * of /sbin/init: 
 *  1. a 7680 slot amap at protection PROT_NONE (reserve space for stack)
 *  2. a 512 slot amap at protection PROT_READ|PROT_WRITE (top of stack)
 *
 * most of the array allocated for the amaps for this is never used.  
 * the amap interface provides a way for us to avoid this problem by
 * allowing amap_copy() to break larger amaps up into smaller sized 
 * chunks (controlled by the "canchunk" option).   we use this feature
 * to reduce our memory usage with the BSD stack management.  if we
 * are asked to create an amap with more than UVM_AMAP_LARGE slots in it,
 * we attempt to break it up into a UVM_AMAP_CHUNK sized amap if the
 * "canchunk" flag is set.
 *
 * so, in the i386 example, the 7680 slot area is never referenced so
 * nothing gets allocated (amap_copy is never called because the protection
 * is zero).   the 512 slot area for the top of the stack is referenced.
 * the chunking code breaks it up into 16 slot chunks (hopefully a single
 * 16 slot chunk is enough to handle the whole stack).
 */

#define UVM_AMAP_LARGE	256	/* # of slots in "large" amap */
#define UVM_AMAP_CHUNK	16	/* # of slots to chunk large amaps in */

#define UVM_AMAP_SMALL(amap)		((amap)->am_nslot <= UVM_AMAP_CHUNK)
#define UVM_AMAP_SLOTIDX(slot)		((slot) % UVM_AMAP_CHUNK)
#define UVM_AMAP_BUCKET(amap, slot)				\
	(((slot) / UVM_AMAP_CHUNK) >> (amap)->am_hashshift)

#ifdef _KERNEL

/*
 * macros
 */

/* AMAP_B2SLOT: convert byte offset to slot */
#define AMAP_B2SLOT(S,B) {						\
	KASSERT(((B) & (PAGE_SIZE - 1)) == 0);				\
	(S) = (B) >> PAGE_SHIFT;					\
}

#define AMAP_CHUNK_FOREACH(chunk, amap)					\
	for (chunk = (UVM_AMAP_SMALL(amap) ?				\
	    &(amap)->am_small : TAILQ_FIRST(&(amap)->am_chunks));	\
	    (chunk) != NULL; (chunk) = TAILQ_NEXT(chunk, ac_list))

#define AMAP_BASE_SLOT(slot)						\
	(((slot) / UVM_AMAP_CHUNK) * UVM_AMAP_CHUNK)

/*
 * flags macros
 */

#define amap_flags(AMAP)	((AMAP)->am_flags)
#define amap_refs(AMAP)		((AMAP)->am_ref)

#define amap_lock(AMAP, RWLT)	rw_enter((AMAP)->am_lock, (RWLT))
#define amap_unlock(AMAP)	rw_exit((AMAP)->am_lock)

#endif /* _KERNEL */

#endif /* _UVM_UVM_AMAP_H_ */
