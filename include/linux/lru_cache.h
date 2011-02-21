/*
   lru_cache.c

   This file is part of DRBD by Philipp Reisner and Lars Ellenberg.

   Copyright (C) 2003-2008, LINBIT Information Technologies GmbH.
   Copyright (C) 2003-2008, Philipp Reisner <philipp.reisner@linbit.com>.
   Copyright (C) 2003-2008, Lars Ellenberg <lars.ellenberg@linbit.com>.

   drbd is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   drbd is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with drbd; see the file COPYING.  If not, write to
   the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.

 */

#ifndef LRU_CACHE_H
#define LRU_CACHE_H

#include <linux/list.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/string.h> /* for memset */
#include <linux/seq_file.h>

/*
This header file (and its .c file; kernel-doc of functions see there)
  define a helper framework to easily keep track of index:label associations,
  and changes to an "active set" of objects, as well as pending transactions,
  to persistently record those changes.

  We use an LRU policy if it is necessary to "cool down" a region currently in
  the active set before we can "heat" a previously unused region.

  Because of this later property, it is called "lru_cache".
  As it actually Tracks Objects in an Active SeT, we could also call it
  toast (incidentally that is what may happen to the data on the
  backend storage uppon next resync, if we don't get it right).

What for?

We replicate IO (more or less synchronously) to local and remote disk.

For crash recovery after replication node failure,
  we need to resync all regions that have been target of in-flight WRITE IO
  (in use, or "hot", regions), as we don't know wether or not those WRITEs have
  made it to stable storage.

  To avoid a "full resync", we need to persistently track these regions.

  This is known as "write intent log", and can be implemented as on-disk
  (coarse or fine grained) bitmap, or other meta data.

  To avoid the overhead of frequent extra writes to this meta data area,
  usually the condition is softened to regions that _may_ have been target of
  in-flight WRITE IO, e.g. by only lazily clearing the on-disk write-intent
  bitmap, trading frequency of meta data transactions against amount of
  (possibly unnecessary) resync traffic.

  If we set a hard limit on the area that may be "hot" at any given time, we
  limit the amount of resync traffic needed for crash recovery.

For recovery after replication link failure,
  we need to resync all blocks that have been changed on the other replica
  in the mean time, or, if both replica have been changed independently [*],
  all blocks that have been changed on either replica in the mean time.
  [*] usually as a result of a cluster split-brain and insufficient protection.
      but there are valid use cases to do this on purpose.

  Tracking those blocks can be implemented as "dirty bitmap".
  Having it fine-grained reduces the amount of resync traffic.
  It should also be persistent, to allow for reboots (or crashes)
  while the replication link is down.

There are various possible implementations for persistently storing
write intent log information, three of which are mentioned here.

"Chunk dirtying"
  The on-disk "dirty bitmap" may be re-used as "write-intent" bitmap as well.
  To reduce the frequency of bitmap updates for write-intent log purposes,
  one could dirty "chunks" (of some size) at a time of the (fine grained)
  on-disk bitmap, while keeping the in-memory "dirty" bitmap as clean as
  possible, flushing it to disk again when a previously "hot" (and on-disk
  dirtied as full chunk) area "cools down" again (no IO in flight anymore,
  and none expected in the near future either).

"Explicit (coarse) write intent bitmap"
  An other implementation could chose a (probably coarse) explicit bitmap,
  for write-intent log purposes, additionally to the fine grained dirty bitmap.

"Activity log"
  Yet an other implementation may keep track of the hot regions, by starting
  with an empty set, and writing down a journal of region numbers that have
  become "hot", or have "cooled down" again.

  To be able to use a ring buffer for this journal of changes to the active
  set, we not only record the actual changes to that set, but also record the
  not changing members of the set in a round robin fashion. To do so, we use a
  fixed (but configurable) number of slots which we can identify by index, and
  associate region numbers (labels) with these indices.
  For each transaction recording a change to the active set, we record the
  change itself (index: -old_label, +new_label), and which index is associated
  with which label (index: current_label) within a certain sliding window that
  is moved further over the available indices with each such transaction.

  Thus, for crash recovery, if the ringbuffer is sufficiently large, we can
  accurately reconstruct the active set.

  Sufficiently large depends only on maximum number of active objects, and the
  size of the sliding window recording "index: current_label" associations within
  each transaction.

  This is what we call the "activity log".

  Currently we need one activity log transaction per single label change, which
  does not give much benefit over the "dirty chunks of bitmap" approach, other
  than potentially less seeks.

  We plan to change the transaction format to support multiple changes per
  transaction, which then would reduce several (disjoint, "random") updates to
  the bitmap into one transaction to the activity log ring buffer.
*/

/* this defines an element in a tracked set
 * .colision is for hash table lookup.
 * When we process a new IO request, we know its sector, thus can deduce the
 * region number (label) easily.  To do the label -> object lookup without a
 * full list walk, we use a simple hash table.
 *
 * .list is on one of three lists:
 *  in_use: currently in use (refcnt > 0, lc_number != LC_FREE)
 *     lru: unused but ready to be reused or recycled
 *          (lc_refcnt == 0, lc_number != LC_FREE),
 *    free: unused but ready to be recycled
 *          (lc_refcnt == 0, lc_number == LC_FREE),
 *
 * an element is said to be "in the active set",
 * if either on "in_use" or "lru", i.e. lc_number != LC_FREE.
 *
 * DRBD currently (May 2009) only uses 61 elements on the resync lru_cache
 * (total memory usage 2 pages), and up to 3833 elements on the act_log
 * lru_cache, totalling ~215 kB for 64bit architecture, ~53 pages.
 *
 * We usually do not actually free these objects again, but only "recycle"
 * them, as the change "index: -old_label, +LC_FREE" would need a transaction
 * as well.  Which also means that using a kmem_cache to allocate the objects
 * from wastes some resources.
 * But it avoids high order page allocations in kmalloc.
 */
struct lc_element {
	struct hlist_node colision;
	struct list_head list;		 /* LRU list or free list */
	unsigned refcnt;
	/* back "pointer" into lc_cache->element[index],
	 * for paranoia, and for "lc_element_to_index" */
	unsigned lc_index;
	/* if we want to track a larger set of objects,
	 * it needs to become arch independend u64 */
	unsigned lc_number;
	/* special label when on free list */
#define LC_FREE (~0U)

	/* for pending changes */
	unsigned lc_new_number;
};

struct lru_cache {
	/* the least recently used item is kept at lru->prev */
	struct list_head lru;
	struct list_head free;
	struct list_head in_use;
	struct list_head to_be_changed;

	/* the pre-created kmem cache to allocate the objects from */
	struct kmem_cache *lc_cache;

	/* size of tracked objects, used to memset(,0,) them in lc_reset */
	size_t element_size;
	/* offset of struct lc_element member in the tracked object */
	size_t element_off;

	/* number of elements (indices) */
	unsigned int nr_elements;
	/* Arbitrary limit on maximum tracked objects. Practical limit is much
	 * lower due to allocation failures, probably. For typical use cases,
	 * nr_elements should be a few thousand at most.
	 * This also limits the maximum value of lc_element.lc_index, allowing the
	 * 8 high bits of .lc_index to be overloaded with flags in the future. */
#define LC_MAX_ACTIVE	(1<<24)

	/* allow to accumulate a few (index:label) changes,
	 * but no more than max_pending_changes */
	unsigned int max_pending_changes;
	/* number of elements currently on to_be_changed list */
	unsigned int pending_changes;

	/* statistics */
	unsigned used; /* number of elements currently on in_use list */
	unsigned long hits, misses, starving, locked, changed;

	/* see below: flag-bits for lru_cache */
	unsigned long flags;


	void  *lc_private;
	const char *name;

	/* nr_elements there */
	struct hlist_head *lc_slot;
	struct lc_element **lc_element;
};


/* flag-bits for lru_cache */
enum {
	/* debugging aid, to catch concurrent access early.
	 * user needs to guarantee exclusive access by proper locking! */
	__LC_PARANOIA,

	/* annotate that the set is "dirty", possibly accumulating further
	 * changes, until a transaction is finally triggered */
	__LC_DIRTY,

	/* Locked, no further changes allowed.
	 * Also used to serialize changing transactions. */
	__LC_LOCKED,

	/* if we need to change the set, but currently there is no free nor
	 * unused element available, we are "starving", and must not give out
	 * further references, to guarantee that eventually some refcnt will
	 * drop to zero and we will be able to make progress again, changing
	 * the set, writing the transaction.
	 * if the statistics say we are frequently starving,
	 * nr_elements is too small. */
	__LC_STARVING,
};
#define LC_PARANOIA (1<<__LC_PARANOIA)
#define LC_DIRTY    (1<<__LC_DIRTY)
#define LC_LOCKED   (1<<__LC_LOCKED)
#define LC_STARVING (1<<__LC_STARVING)

extern struct lru_cache *lc_create(const char *name, struct kmem_cache *cache,
		unsigned max_pending_changes,
		unsigned e_count, size_t e_size, size_t e_off);
extern void lc_reset(struct lru_cache *lc);
extern void lc_destroy(struct lru_cache *lc);
extern void lc_set(struct lru_cache *lc, unsigned int enr, int index);
extern void lc_del(struct lru_cache *lc, struct lc_element *element);

extern struct lc_element *lc_try_get(struct lru_cache *lc, unsigned int enr);
extern struct lc_element *lc_find(struct lru_cache *lc, unsigned int enr);
extern struct lc_element *lc_get(struct lru_cache *lc, unsigned int enr);
extern unsigned int lc_put(struct lru_cache *lc, struct lc_element *e);
extern void lc_committed(struct lru_cache *lc);

struct seq_file;
extern size_t lc_seq_printf_stats(struct seq_file *seq, struct lru_cache *lc);

extern void lc_seq_dump_details(struct seq_file *seq, struct lru_cache *lc, char *utext,
				void (*detail) (struct seq_file *, struct lc_element *));

/**
 * lc_try_lock_for_transaction - can be used to stop lc_get() from changing the tracked set
 * @lc: the lru cache to operate on
 *
 * Allows (expects) the set to be "dirty".  Note that the reference counts and
 * order on the active and lru lists may still change.  Used to serialize
 * changing transactions.  Returns true if we aquired the lock.
 */
static inline int lc_try_lock_for_transaction(struct lru_cache *lc)
{
	return !test_and_set_bit(__LC_LOCKED, &lc->flags);
}

/**
 * lc_try_lock - variant to stop lc_get() from changing the tracked set
 * @lc: the lru cache to operate on
 *
 * Note that the reference counts and order on the active and lru lists may
 * still change.  Only works on a "clean" set.  Returns true if we aquired the
 * lock, which means there are no pending changes, and any further attempt to
 * change the set will not succeed until the next lc_unlock().
 */
extern int lc_try_lock(struct lru_cache *lc);

/**
 * lc_unlock - unlock @lc, allow lc_get() to change the set again
 * @lc: the lru cache to operate on
 */
static inline void lc_unlock(struct lru_cache *lc)
{
	clear_bit(__LC_DIRTY, &lc->flags);
	clear_bit_unlock(__LC_LOCKED, &lc->flags);
}

extern bool lc_is_used(struct lru_cache *lc, unsigned int enr);

#define lc_entry(ptr, type, member) \
	container_of(ptr, type, member)

extern struct lc_element *lc_element_by_index(struct lru_cache *lc, unsigned i);
extern unsigned int lc_index_of(struct lru_cache *lc, struct lc_element *e);

#endif
