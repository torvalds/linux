/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

/*
 * This file contains the code to implement file range locking in
 * ZFS, although there isn't much specific to ZFS (all that comes to mind is
 * support for growing the blocksize).
 *
 * Interface
 * ---------
 * Defined in zfs_rlock.h but essentially:
 *	rl = zfs_range_lock(zp, off, len, lock_type);
 *	zfs_range_unlock(rl);
 *	zfs_range_reduce(rl, off, len);
 *
 * AVL tree
 * --------
 * An AVL tree is used to maintain the state of the existing ranges
 * that are locked for exclusive (writer) or shared (reader) use.
 * The starting range offset is used for searching and sorting the tree.
 *
 * Common case
 * -----------
 * The (hopefully) usual case is of no overlaps or contention for
 * locks. On entry to zfs_lock_range() a rl_t is allocated; the tree
 * searched that finds no overlap, and *this* rl_t is placed in the tree.
 *
 * Overlaps/Reference counting/Proxy locks
 * ---------------------------------------
 * The avl code only allows one node at a particular offset. Also it's very
 * inefficient to search through all previous entries looking for overlaps
 * (because the very 1st in the ordered list might be at offset 0 but
 * cover the whole file).
 * So this implementation uses reference counts and proxy range locks.
 * Firstly, only reader locks use reference counts and proxy locks,
 * because writer locks are exclusive.
 * When a reader lock overlaps with another then a proxy lock is created
 * for that range and replaces the original lock. If the overlap
 * is exact then the reference count of the proxy is simply incremented.
 * Otherwise, the proxy lock is split into smaller lock ranges and
 * new proxy locks created for non overlapping ranges.
 * The reference counts are adjusted accordingly.
 * Meanwhile, the orginal lock is kept around (this is the callers handle)
 * and its offset and length are used when releasing the lock.
 *
 * Thread coordination
 * -------------------
 * In order to make wakeups efficient and to ensure multiple continuous
 * readers on a range don't starve a writer for the same range lock,
 * two condition variables are allocated in each rl_t.
 * If a writer (or reader) can't get a range it initialises the writer
 * (or reader) cv; sets a flag saying there's a writer (or reader) waiting;
 * and waits on that cv. When a thread unlocks that range it wakes up all
 * writers then all readers before destroying the lock.
 *
 * Append mode writes
 * ------------------
 * Append mode writes need to lock a range at the end of a file.
 * The offset of the end of the file is determined under the
 * range locking mutex, and the lock type converted from RL_APPEND to
 * RL_WRITER and the range locked.
 *
 * Grow block handling
 * -------------------
 * ZFS supports multiple block sizes currently upto 128K. The smallest
 * block size is used for the file which is grown as needed. During this
 * growth all other writers and readers must be excluded.
 * So if the block size needs to be grown then the whole file is
 * exclusively locked, then later the caller will reduce the lock
 * range to just the range to be written using zfs_reduce_range.
 */

#include <sys/zfs_rlock.h>

/*
 * Check if a write lock can be grabbed, or wait and recheck until available.
 */
static void
zfs_range_lock_writer(znode_t *zp, rl_t *new)
{
	avl_tree_t *tree = &zp->z_range_avl;
	rl_t *rl;
	avl_index_t where;
	uint64_t end_size;
	uint64_t off = new->r_off;
	uint64_t len = new->r_len;

	for (;;) {
		/*
		 * Range locking is also used by zvol and uses a
		 * dummied up znode. However, for zvol, we don't need to
		 * append or grow blocksize, and besides we don't have
		 * a "sa" data or z_zfsvfs - so skip that processing.
		 *
		 * Yes, this is ugly, and would be solved by not handling
		 * grow or append in range lock code. If that was done then
		 * we could make the range locking code generically available
		 * to other non-zfs consumers.
		 */
		if (zp->z_vnode) { /* caller is ZPL */
			/*
			 * If in append mode pick up the current end of file.
			 * This is done under z_range_lock to avoid races.
			 */
			if (new->r_type == RL_APPEND)
				new->r_off = zp->z_size;

			/*
			 * If we need to grow the block size then grab the whole
			 * file range. This is also done under z_range_lock to
			 * avoid races.
			 */
			end_size = MAX(zp->z_size, new->r_off + len);
			if (end_size > zp->z_blksz && (!ISP2(zp->z_blksz) ||
			    zp->z_blksz < zp->z_zfsvfs->z_max_blksz)) {
				new->r_off = 0;
				new->r_len = UINT64_MAX;
			}
		}

		/*
		 * First check for the usual case of no locks
		 */
		if (avl_numnodes(tree) == 0) {
			new->r_type = RL_WRITER; /* convert to writer */
			avl_add(tree, new);
			return;
		}

		/*
		 * Look for any locks in the range.
		 */
		rl = avl_find(tree, new, &where);
		if (rl)
			goto wait; /* already locked at same offset */

		rl = (rl_t *)avl_nearest(tree, where, AVL_AFTER);
		if (rl && (rl->r_off < new->r_off + new->r_len))
			goto wait;

		rl = (rl_t *)avl_nearest(tree, where, AVL_BEFORE);
		if (rl && rl->r_off + rl->r_len > new->r_off)
			goto wait;

		new->r_type = RL_WRITER; /* convert possible RL_APPEND */
		avl_insert(tree, new, where);
		return;
wait:
		if (!rl->r_write_wanted) {
			cv_init(&rl->r_wr_cv, NULL, CV_DEFAULT, NULL);
			rl->r_write_wanted = B_TRUE;
		}
		cv_wait(&rl->r_wr_cv, &zp->z_range_lock);

		/* reset to original */
		new->r_off = off;
		new->r_len = len;
	}
}

/*
 * If this is an original (non-proxy) lock then replace it by
 * a proxy and return the proxy.
 */
static rl_t *
zfs_range_proxify(avl_tree_t *tree, rl_t *rl)
{
	rl_t *proxy;

	if (rl->r_proxy)
		return (rl); /* already a proxy */

	ASSERT3U(rl->r_cnt, ==, 1);
	ASSERT(rl->r_write_wanted == B_FALSE);
	ASSERT(rl->r_read_wanted == B_FALSE);
	avl_remove(tree, rl);
	rl->r_cnt = 0;

	/* create a proxy range lock */
	proxy = kmem_alloc(sizeof (rl_t), KM_SLEEP);
	proxy->r_off = rl->r_off;
	proxy->r_len = rl->r_len;
	proxy->r_cnt = 1;
	proxy->r_type = RL_READER;
	proxy->r_proxy = B_TRUE;
	proxy->r_write_wanted = B_FALSE;
	proxy->r_read_wanted = B_FALSE;
	avl_add(tree, proxy);

	return (proxy);
}

/*
 * Split the range lock at the supplied offset
 * returning the *front* proxy.
 */
static rl_t *
zfs_range_split(avl_tree_t *tree, rl_t *rl, uint64_t off)
{
	rl_t *front, *rear;

	ASSERT3U(rl->r_len, >, 1);
	ASSERT3U(off, >, rl->r_off);
	ASSERT3U(off, <, rl->r_off + rl->r_len);
	ASSERT(rl->r_write_wanted == B_FALSE);
	ASSERT(rl->r_read_wanted == B_FALSE);

	/* create the rear proxy range lock */
	rear = kmem_alloc(sizeof (rl_t), KM_SLEEP);
	rear->r_off = off;
	rear->r_len = rl->r_off + rl->r_len - off;
	rear->r_cnt = rl->r_cnt;
	rear->r_type = RL_READER;
	rear->r_proxy = B_TRUE;
	rear->r_write_wanted = B_FALSE;
	rear->r_read_wanted = B_FALSE;

	front = zfs_range_proxify(tree, rl);
	front->r_len = off - rl->r_off;

	avl_insert_here(tree, rear, front, AVL_AFTER);
	return (front);
}

/*
 * Create and add a new proxy range lock for the supplied range.
 */
static void
zfs_range_new_proxy(avl_tree_t *tree, uint64_t off, uint64_t len)
{
	rl_t *rl;

	ASSERT(len);
	rl = kmem_alloc(sizeof (rl_t), KM_SLEEP);
	rl->r_off = off;
	rl->r_len = len;
	rl->r_cnt = 1;
	rl->r_type = RL_READER;
	rl->r_proxy = B_TRUE;
	rl->r_write_wanted = B_FALSE;
	rl->r_read_wanted = B_FALSE;
	avl_add(tree, rl);
}

static void
zfs_range_add_reader(avl_tree_t *tree, rl_t *new, rl_t *prev, avl_index_t where)
{
	rl_t *next;
	uint64_t off = new->r_off;
	uint64_t len = new->r_len;

	/*
	 * prev arrives either:
	 * - pointing to an entry at the same offset
	 * - pointing to the entry with the closest previous offset whose
	 *   range may overlap with the new range
	 * - null, if there were no ranges starting before the new one
	 */
	if (prev) {
		if (prev->r_off + prev->r_len <= off) {
			prev = NULL;
		} else if (prev->r_off != off) {
			/*
			 * convert to proxy if needed then
			 * split this entry and bump ref count
			 */
			prev = zfs_range_split(tree, prev, off);
			prev = AVL_NEXT(tree, prev); /* move to rear range */
		}
	}
	ASSERT((prev == NULL) || (prev->r_off == off));

	if (prev)
		next = prev;
	else
		next = (rl_t *)avl_nearest(tree, where, AVL_AFTER);

	if (next == NULL || off + len <= next->r_off) {
		/* no overlaps, use the original new rl_t in the tree */
		avl_insert(tree, new, where);
		return;
	}

	if (off < next->r_off) {
		/* Add a proxy for initial range before the overlap */
		zfs_range_new_proxy(tree, off, next->r_off - off);
	}

	new->r_cnt = 0; /* will use proxies in tree */
	/*
	 * We now search forward through the ranges, until we go past the end
	 * of the new range. For each entry we make it a proxy if it
	 * isn't already, then bump its reference count. If there's any
	 * gaps between the ranges then we create a new proxy range.
	 */
	for (prev = NULL; next; prev = next, next = AVL_NEXT(tree, next)) {
		if (off + len <= next->r_off)
			break;
		if (prev && prev->r_off + prev->r_len < next->r_off) {
			/* there's a gap */
			ASSERT3U(next->r_off, >, prev->r_off + prev->r_len);
			zfs_range_new_proxy(tree, prev->r_off + prev->r_len,
			    next->r_off - (prev->r_off + prev->r_len));
		}
		if (off + len == next->r_off + next->r_len) {
			/* exact overlap with end */
			next = zfs_range_proxify(tree, next);
			next->r_cnt++;
			return;
		}
		if (off + len < next->r_off + next->r_len) {
			/* new range ends in the middle of this block */
			next = zfs_range_split(tree, next, off + len);
			next->r_cnt++;
			return;
		}
		ASSERT3U(off + len, >, next->r_off + next->r_len);
		next = zfs_range_proxify(tree, next);
		next->r_cnt++;
	}

	/* Add the remaining end range. */
	zfs_range_new_proxy(tree, prev->r_off + prev->r_len,
	    (off + len) - (prev->r_off + prev->r_len));
}

/*
 * Check if a reader lock can be grabbed, or wait and recheck until available.
 */
static void
zfs_range_lock_reader(znode_t *zp, rl_t *new)
{
	avl_tree_t *tree = &zp->z_range_avl;
	rl_t *prev, *next;
	avl_index_t where;
	uint64_t off = new->r_off;
	uint64_t len = new->r_len;

	/*
	 * Look for any writer locks in the range.
	 */
retry:
	prev = avl_find(tree, new, &where);
	if (prev == NULL)
		prev = (rl_t *)avl_nearest(tree, where, AVL_BEFORE);

	/*
	 * Check the previous range for a writer lock overlap.
	 */
	if (prev && (off < prev->r_off + prev->r_len)) {
		if ((prev->r_type == RL_WRITER) || (prev->r_write_wanted)) {
			if (!prev->r_read_wanted) {
				cv_init(&prev->r_rd_cv, NULL, CV_DEFAULT, NULL);
				prev->r_read_wanted = B_TRUE;
			}
			cv_wait(&prev->r_rd_cv, &zp->z_range_lock);
			goto retry;
		}
		if (off + len < prev->r_off + prev->r_len)
			goto got_lock;
	}

	/*
	 * Search through the following ranges to see if there's
	 * write lock any overlap.
	 */
	if (prev)
		next = AVL_NEXT(tree, prev);
	else
		next = (rl_t *)avl_nearest(tree, where, AVL_AFTER);
	for (; next; next = AVL_NEXT(tree, next)) {
		if (off + len <= next->r_off)
			goto got_lock;
		if ((next->r_type == RL_WRITER) || (next->r_write_wanted)) {
			if (!next->r_read_wanted) {
				cv_init(&next->r_rd_cv, NULL, CV_DEFAULT, NULL);
				next->r_read_wanted = B_TRUE;
			}
			cv_wait(&next->r_rd_cv, &zp->z_range_lock);
			goto retry;
		}
		if (off + len <= next->r_off + next->r_len)
			goto got_lock;
	}

got_lock:
	/*
	 * Add the read lock, which may involve splitting existing
	 * locks and bumping ref counts (r_cnt).
	 */
	zfs_range_add_reader(tree, new, prev, where);
}

/*
 * Lock a range (offset, length) as either shared (RL_READER)
 * or exclusive (RL_WRITER). Returns the range lock structure
 * for later unlocking or reduce range (if entire file
 * previously locked as RL_WRITER).
 */
rl_t *
zfs_range_lock(znode_t *zp, uint64_t off, uint64_t len, rl_type_t type)
{
	rl_t *new;

	ASSERT(type == RL_READER || type == RL_WRITER || type == RL_APPEND);

	new = kmem_alloc(sizeof (rl_t), KM_SLEEP);
	new->r_zp = zp;
	new->r_off = off;
	if (len + off < off)	/* overflow */
		len = UINT64_MAX - off;
	new->r_len = len;
	new->r_cnt = 1; /* assume it's going to be in the tree */
	new->r_type = type;
	new->r_proxy = B_FALSE;
	new->r_write_wanted = B_FALSE;
	new->r_read_wanted = B_FALSE;

	mutex_enter(&zp->z_range_lock);
	if (type == RL_READER) {
		/*
		 * First check for the usual case of no locks
		 */
		if (avl_numnodes(&zp->z_range_avl) == 0)
			avl_add(&zp->z_range_avl, new);
		else
			zfs_range_lock_reader(zp, new);
	} else
		zfs_range_lock_writer(zp, new); /* RL_WRITER or RL_APPEND */
	mutex_exit(&zp->z_range_lock);
	return (new);
}

/*
 * Unlock a reader lock
 */
static void
zfs_range_unlock_reader(znode_t *zp, rl_t *remove)
{
	avl_tree_t *tree = &zp->z_range_avl;
	rl_t *rl, *next = NULL;
	uint64_t len;

	/*
	 * The common case is when the remove entry is in the tree
	 * (cnt == 1) meaning there's been no other reader locks overlapping
	 * with this one. Otherwise the remove entry will have been
	 * removed from the tree and replaced by proxies (one or
	 * more ranges mapping to the entire range).
	 */
	if (remove->r_cnt == 1) {
		avl_remove(tree, remove);
		if (remove->r_write_wanted) {
			cv_broadcast(&remove->r_wr_cv);
			cv_destroy(&remove->r_wr_cv);
		}
		if (remove->r_read_wanted) {
			cv_broadcast(&remove->r_rd_cv);
			cv_destroy(&remove->r_rd_cv);
		}
	} else {
		ASSERT0(remove->r_cnt);
		ASSERT0(remove->r_write_wanted);
		ASSERT0(remove->r_read_wanted);
		/*
		 * Find start proxy representing this reader lock,
		 * then decrement ref count on all proxies
		 * that make up this range, freeing them as needed.
		 */
		rl = avl_find(tree, remove, NULL);
		ASSERT(rl);
		ASSERT(rl->r_cnt);
		ASSERT(rl->r_type == RL_READER);
		for (len = remove->r_len; len != 0; rl = next) {
			len -= rl->r_len;
			if (len) {
				next = AVL_NEXT(tree, rl);
				ASSERT(next);
				ASSERT(rl->r_off + rl->r_len == next->r_off);
				ASSERT(next->r_cnt);
				ASSERT(next->r_type == RL_READER);
			}
			rl->r_cnt--;
			if (rl->r_cnt == 0) {
				avl_remove(tree, rl);
				if (rl->r_write_wanted) {
					cv_broadcast(&rl->r_wr_cv);
					cv_destroy(&rl->r_wr_cv);
				}
				if (rl->r_read_wanted) {
					cv_broadcast(&rl->r_rd_cv);
					cv_destroy(&rl->r_rd_cv);
				}
				kmem_free(rl, sizeof (rl_t));
			}
		}
	}
	kmem_free(remove, sizeof (rl_t));
}

/*
 * Unlock range and destroy range lock structure.
 */
void
zfs_range_unlock(rl_t *rl)
{
	znode_t *zp = rl->r_zp;

	ASSERT(rl->r_type == RL_WRITER || rl->r_type == RL_READER);
	ASSERT(rl->r_cnt == 1 || rl->r_cnt == 0);
	ASSERT(!rl->r_proxy);

	mutex_enter(&zp->z_range_lock);
	if (rl->r_type == RL_WRITER) {
		/* writer locks can't be shared or split */
		avl_remove(&zp->z_range_avl, rl);
		mutex_exit(&zp->z_range_lock);
		if (rl->r_write_wanted) {
			cv_broadcast(&rl->r_wr_cv);
			cv_destroy(&rl->r_wr_cv);
		}
		if (rl->r_read_wanted) {
			cv_broadcast(&rl->r_rd_cv);
			cv_destroy(&rl->r_rd_cv);
		}
		kmem_free(rl, sizeof (rl_t));
	} else {
		/*
		 * lock may be shared, let zfs_range_unlock_reader()
		 * release the lock and free the rl_t
		 */
		zfs_range_unlock_reader(zp, rl);
		mutex_exit(&zp->z_range_lock);
	}
}

/*
 * Reduce range locked as RL_WRITER from whole file to specified range.
 * Asserts the whole file is exclusivly locked and so there's only one
 * entry in the tree.
 */
void
zfs_range_reduce(rl_t *rl, uint64_t off, uint64_t len)
{
	znode_t *zp = rl->r_zp;

	/* Ensure there are no other locks */
	ASSERT(avl_numnodes(&zp->z_range_avl) == 1);
	ASSERT(rl->r_off == 0);
	ASSERT(rl->r_type == RL_WRITER);
	ASSERT(!rl->r_proxy);
	ASSERT3U(rl->r_len, ==, UINT64_MAX);
	ASSERT3U(rl->r_cnt, ==, 1);

	mutex_enter(&zp->z_range_lock);
	rl->r_off = off;
	rl->r_len = len;
	mutex_exit(&zp->z_range_lock);
	if (rl->r_write_wanted)
		cv_broadcast(&rl->r_wr_cv);
	if (rl->r_read_wanted)
		cv_broadcast(&rl->r_rd_cv);
}

/*
 * AVL comparison function used to order range locks
 * Locks are ordered on the start offset of the range.
 */
int
zfs_range_compare(const void *arg1, const void *arg2)
{
	const rl_t *rl1 = (const rl_t *)arg1;
	const rl_t *rl2 = (const rl_t *)arg2;

	return (AVL_CMP(rl1->r_off, rl2->r_off));
}
