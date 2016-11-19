/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
 *
 * Authors: Adrian Hunter
 *          Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * This file implements garbage collection. The procedure for garbage collection
 * is different depending on whether a LEB as an index LEB (contains index
 * nodes) or not. For non-index LEBs, garbage collection finds a LEB which
 * contains a lot of dirty space (obsolete nodes), and copies the non-obsolete
 * nodes to the journal, at which point the garbage-collected LEB is free to be
 * reused. For index LEBs, garbage collection marks the non-obsolete index nodes
 * dirty in the TNC, and after the next commit, the garbage-collected LEB is
 * to be reused. Garbage collection will cause the number of dirty index nodes
 * to grow, however sufficient space is reserved for the index to ensure the
 * commit will never run out of space.
 *
 * Notes about dead watermark. At current UBIFS implementation we assume that
 * LEBs which have less than @c->dead_wm bytes of free + dirty space are full
 * and not worth garbage-collecting. The dead watermark is one min. I/O unit
 * size, or min. UBIFS node size, depending on what is greater. Indeed, UBIFS
 * Garbage Collector has to synchronize the GC head's write buffer before
 * returning, so this is about wasting one min. I/O unit. However, UBIFS GC can
 * actually reclaim even very small pieces of dirty space by garbage collecting
 * enough dirty LEBs, but we do not bother doing this at this implementation.
 *
 * Notes about dark watermark. The results of GC work depends on how big are
 * the UBIFS nodes GC deals with. Large nodes make GC waste more space. Indeed,
 * if GC move data from LEB A to LEB B and nodes in LEB A are large, GC would
 * have to waste large pieces of free space at the end of LEB B, because nodes
 * from LEB A would not fit. And the worst situation is when all nodes are of
 * maximum size. So dark watermark is the amount of free + dirty space in LEB
 * which are guaranteed to be reclaimable. If LEB has less space, the GC might
 * be unable to reclaim it. So, LEBs with free + dirty greater than dark
 * watermark are "good" LEBs from GC's point of few. The other LEBs are not so
 * good, and GC takes extra care when moving them.
 */

#include <linux/slab.h>
#include <linux/pagemap.h>
#include <linux/list_sort.h>
#include "ubifs.h"

/*
 * GC may need to move more than one LEB to make progress. The below constants
 * define "soft" and "hard" limits on the number of LEBs the garbage collector
 * may move.
 */
#define SOFT_LEBS_LIMIT 4
#define HARD_LEBS_LIMIT 32

/**
 * switch_gc_head - switch the garbage collection journal head.
 * @c: UBIFS file-system description object
 * @buf: buffer to write
 * @len: length of the buffer to write
 * @lnum: LEB number written is returned here
 * @offs: offset written is returned here
 *
 * This function switch the GC head to the next LEB which is reserved in
 * @c->gc_lnum. Returns %0 in case of success, %-EAGAIN if commit is required,
 * and other negative error code in case of failures.
 */
static int switch_gc_head(struct ubifs_info *c)
{
	int err, gc_lnum = c->gc_lnum;
	struct ubifs_wbuf *wbuf = &c->jheads[GCHD].wbuf;

	ubifs_assert(gc_lnum != -1);
	dbg_gc("switch GC head from LEB %d:%d to LEB %d (waste %d bytes)",
	       wbuf->lnum, wbuf->offs + wbuf->used, gc_lnum,
	       c->leb_size - wbuf->offs - wbuf->used);

	err = ubifs_wbuf_sync_nolock(wbuf);
	if (err)
		return err;

	/*
	 * The GC write-buffer was synchronized, we may safely unmap
	 * 'c->gc_lnum'.
	 */
	err = ubifs_leb_unmap(c, gc_lnum);
	if (err)
		return err;

	err = ubifs_add_bud_to_log(c, GCHD, gc_lnum, 0);
	if (err)
		return err;

	c->gc_lnum = -1;
	err = ubifs_wbuf_seek_nolock(wbuf, gc_lnum, 0);
	return err;
}

/**
 * data_nodes_cmp - compare 2 data nodes.
 * @priv: UBIFS file-system description object
 * @a: first data node
 * @b: second data node
 *
 * This function compares data nodes @a and @b. Returns %1 if @a has greater
 * inode or block number, and %-1 otherwise.
 */
static int data_nodes_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	ino_t inuma, inumb;
	struct ubifs_info *c = priv;
	struct ubifs_scan_node *sa, *sb;

	cond_resched();
	if (a == b)
		return 0;

	sa = list_entry(a, struct ubifs_scan_node, list);
	sb = list_entry(b, struct ubifs_scan_node, list);

	ubifs_assert(key_type(c, &sa->key) == UBIFS_DATA_KEY);
	ubifs_assert(key_type(c, &sb->key) == UBIFS_DATA_KEY);
	ubifs_assert(sa->type == UBIFS_DATA_NODE);
	ubifs_assert(sb->type == UBIFS_DATA_NODE);

	inuma = key_inum(c, &sa->key);
	inumb = key_inum(c, &sb->key);

	if (inuma == inumb) {
		unsigned int blka = key_block(c, &sa->key);
		unsigned int blkb = key_block(c, &sb->key);

		if (blka <= blkb)
			return -1;
	} else if (inuma <= inumb)
		return -1;

	return 1;
}

/*
 * nondata_nodes_cmp - compare 2 non-data nodes.
 * @priv: UBIFS file-system description object
 * @a: first node
 * @a: second node
 *
 * This function compares nodes @a and @b. It makes sure that inode nodes go
 * first and sorted by length in descending order. Directory entry nodes go
 * after inode nodes and are sorted in ascending hash valuer order.
 */
static int nondata_nodes_cmp(void *priv, struct list_head *a,
			     struct list_head *b)
{
	ino_t inuma, inumb;
	struct ubifs_info *c = priv;
	struct ubifs_scan_node *sa, *sb;

	cond_resched();
	if (a == b)
		return 0;

	sa = list_entry(a, struct ubifs_scan_node, list);
	sb = list_entry(b, struct ubifs_scan_node, list);

	ubifs_assert(key_type(c, &sa->key) != UBIFS_DATA_KEY &&
		     key_type(c, &sb->key) != UBIFS_DATA_KEY);
	ubifs_assert(sa->type != UBIFS_DATA_NODE &&
		     sb->type != UBIFS_DATA_NODE);

	/* Inodes go before directory entries */
	if (sa->type == UBIFS_INO_NODE) {
		if (sb->type == UBIFS_INO_NODE)
			return sb->len - sa->len;
		return -1;
	}
	if (sb->type == UBIFS_INO_NODE)
		return 1;

	ubifs_assert(key_type(c, &sa->key) == UBIFS_DENT_KEY ||
		     key_type(c, &sa->key) == UBIFS_XENT_KEY);
	ubifs_assert(key_type(c, &sb->key) == UBIFS_DENT_KEY ||
		     key_type(c, &sb->key) == UBIFS_XENT_KEY);
	ubifs_assert(sa->type == UBIFS_DENT_NODE ||
		     sa->type == UBIFS_XENT_NODE);
	ubifs_assert(sb->type == UBIFS_DENT_NODE ||
		     sb->type == UBIFS_XENT_NODE);

	inuma = key_inum(c, &sa->key);
	inumb = key_inum(c, &sb->key);

	if (inuma == inumb) {
		uint32_t hasha = key_hash(c, &sa->key);
		uint32_t hashb = key_hash(c, &sb->key);

		if (hasha <= hashb)
			return -1;
	} else if (inuma <= inumb)
		return -1;

	return 1;
}

/**
 * sort_nodes - sort nodes for GC.
 * @c: UBIFS file-system description object
 * @sleb: describes nodes to sort and contains the result on exit
 * @nondata: contains non-data nodes on exit
 * @min: minimum node size is returned here
 *
 * This function sorts the list of inodes to garbage collect. First of all, it
 * kills obsolete nodes and separates data and non-data nodes to the
 * @sleb->nodes and @nondata lists correspondingly.
 *
 * Data nodes are then sorted in block number order - this is important for
 * bulk-read; data nodes with lower inode number go before data nodes with
 * higher inode number, and data nodes with lower block number go before data
 * nodes with higher block number;
 *
 * Non-data nodes are sorted as follows.
 *   o First go inode nodes - they are sorted in descending length order.
 *   o Then go directory entry nodes - they are sorted in hash order, which
 *     should supposedly optimize 'readdir()'. Direntry nodes with lower parent
 *     inode number go before direntry nodes with higher parent inode number,
 *     and direntry nodes with lower name hash values go before direntry nodes
 *     with higher name hash values.
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
static int sort_nodes(struct ubifs_info *c, struct ubifs_scan_leb *sleb,
		      struct list_head *nondata, int *min)
{
	int err;
	struct ubifs_scan_node *snod, *tmp;

	*min = INT_MAX;

	/* Separate data nodes and non-data nodes */
	list_for_each_entry_safe(snod, tmp, &sleb->nodes, list) {
		ubifs_assert(snod->type == UBIFS_INO_NODE  ||
			     snod->type == UBIFS_DATA_NODE ||
			     snod->type == UBIFS_DENT_NODE ||
			     snod->type == UBIFS_XENT_NODE ||
			     snod->type == UBIFS_TRUN_NODE);

		if (snod->type != UBIFS_INO_NODE  &&
		    snod->type != UBIFS_DATA_NODE &&
		    snod->type != UBIFS_DENT_NODE &&
		    snod->type != UBIFS_XENT_NODE) {
			/* Probably truncation node, zap it */
			list_del(&snod->list);
			kfree(snod);
			continue;
		}

		ubifs_assert(key_type(c, &snod->key) == UBIFS_DATA_KEY ||
			     key_type(c, &snod->key) == UBIFS_INO_KEY  ||
			     key_type(c, &snod->key) == UBIFS_DENT_KEY ||
			     key_type(c, &snod->key) == UBIFS_XENT_KEY);

		err = ubifs_tnc_has_node(c, &snod->key, 0, sleb->lnum,
					 snod->offs, 0);
		if (err < 0)
			return err;

		if (!err) {
			/* The node is obsolete, remove it from the list */
			list_del(&snod->list);
			kfree(snod);
			continue;
		}

		if (snod->len < *min)
			*min = snod->len;

		if (key_type(c, &snod->key) != UBIFS_DATA_KEY)
			list_move_tail(&snod->list, nondata);
	}

	/* Sort data and non-data nodes */
	list_sort(c, &sleb->nodes, &data_nodes_cmp);
	list_sort(c, nondata, &nondata_nodes_cmp);

	err = dbg_check_data_nodes_order(c, &sleb->nodes);
	if (err)
		return err;
	err = dbg_check_nondata_nodes_order(c, nondata);
	if (err)
		return err;
	return 0;
}

/**
 * move_node - move a node.
 * @c: UBIFS file-system description object
 * @sleb: describes the LEB to move nodes from
 * @snod: the mode to move
 * @wbuf: write-buffer to move node to
 *
 * This function moves node @snod to @wbuf, changes TNC correspondingly, and
 * destroys @snod. Returns zero in case of success and a negative error code in
 * case of failure.
 */
static int move_node(struct ubifs_info *c, struct ubifs_scan_leb *sleb,
		     struct ubifs_scan_node *snod, struct ubifs_wbuf *wbuf)
{
	int err, new_lnum = wbuf->lnum, new_offs = wbuf->offs + wbuf->used;

	cond_resched();
	err = ubifs_wbuf_write_nolock(wbuf, snod->node, snod->len);
	if (err)
		return err;

	err = ubifs_tnc_replace(c, &snod->key, sleb->lnum,
				snod->offs, new_lnum, new_offs,
				snod->len);
	list_del(&snod->list);
	kfree(snod);
	return err;
}

/**
 * move_nodes - move nodes.
 * @c: UBIFS file-system description object
 * @sleb: describes the LEB to move nodes from
 *
 * This function moves valid nodes from data LEB described by @sleb to the GC
 * journal head. This function returns zero in case of success, %-EAGAIN if
 * commit is required, and other negative error codes in case of other
 * failures.
 */
static int move_nodes(struct ubifs_info *c, struct ubifs_scan_leb *sleb)
{
	int err, min;
	LIST_HEAD(nondata);
	struct ubifs_wbuf *wbuf = &c->jheads[GCHD].wbuf;

	if (wbuf->lnum == -1) {
		/*
		 * The GC journal head is not set, because it is the first GC
		 * invocation since mount.
		 */
		err = switch_gc_head(c);
		if (err)
			return err;
	}

	err = sort_nodes(c, sleb, &nondata, &min);
	if (err)
		goto out;

	/* Write nodes to their new location. Use the first-fit strategy */
	while (1) {
		int avail;
		struct ubifs_scan_node *snod, *tmp;

		/* Move data nodes */
		list_for_each_entry_safe(snod, tmp, &sleb->nodes, list) {
			avail = c->leb_size - wbuf->offs - wbuf->used;
			if  (snod->len > avail)
				/*
				 * Do not skip data nodes in order to optimize
				 * bulk-read.
				 */
				break;

			err = move_node(c, sleb, snod, wbuf);
			if (err)
				goto out;
		}

		/* Move non-data nodes */
		list_for_each_entry_safe(snod, tmp, &nondata, list) {
			avail = c->leb_size - wbuf->offs - wbuf->used;
			if (avail < min)
				break;

			if  (snod->len > avail) {
				/*
				 * Keep going only if this is an inode with
				 * some data. Otherwise stop and switch the GC
				 * head. IOW, we assume that data-less inode
				 * nodes and direntry nodes are roughly of the
				 * same size.
				 */
				if (key_type(c, &snod->key) == UBIFS_DENT_KEY ||
				    snod->len == UBIFS_INO_NODE_SZ)
					break;
				continue;
			}

			err = move_node(c, sleb, snod, wbuf);
			if (err)
				goto out;
		}

		if (list_empty(&sleb->nodes) && list_empty(&nondata))
			break;

		/*
		 * Waste the rest of the space in the LEB and switch to the
		 * next LEB.
		 */
		err = switch_gc_head(c);
		if (err)
			goto out;
	}

	return 0;

out:
	list_splice_tail(&nondata, &sleb->nodes);
	return err;
}

/**
 * gc_sync_wbufs - sync write-buffers for GC.
 * @c: UBIFS file-system description object
 *
 * We must guarantee that obsoleting nodes are on flash. Unfortunately they may
 * be in a write-buffer instead. That is, a node could be written to a
 * write-buffer, obsoleting another node in a LEB that is GC'd. If that LEB is
 * erased before the write-buffer is sync'd and then there is an unclean
 * unmount, then an existing node is lost. To avoid this, we sync all
 * write-buffers.
 *
 * This function returns %0 on success or a negative error code on failure.
 */
static int gc_sync_wbufs(struct ubifs_info *c)
{
	int err, i;

	for (i = 0; i < c->jhead_cnt; i++) {
		if (i == GCHD)
			continue;
		err = ubifs_wbuf_sync(&c->jheads[i].wbuf);
		if (err)
			return err;
	}
	return 0;
}

/**
 * ubifs_garbage_collect_leb - garbage-collect a logical eraseblock.
 * @c: UBIFS file-system description object
 * @lp: describes the LEB to garbage collect
 *
 * This function garbage-collects an LEB and returns one of the @LEB_FREED,
 * @LEB_RETAINED, etc positive codes in case of success, %-EAGAIN if commit is
 * required, and other negative error codes in case of failures.
 */
int ubifs_garbage_collect_leb(struct ubifs_info *c, struct ubifs_lprops *lp)
{
	struct ubifs_scan_leb *sleb;
	struct ubifs_scan_node *snod;
	struct ubifs_wbuf *wbuf = &c->jheads[GCHD].wbuf;
	int err = 0, lnum = lp->lnum;

	ubifs_assert(c->gc_lnum != -1 || wbuf->offs + wbuf->used == 0 ||
		     c->need_recovery);
	ubifs_assert(c->gc_lnum != lnum);
	ubifs_assert(wbuf->lnum != lnum);

	if (lp->free + lp->dirty == c->leb_size) {
		/* Special case - a free LEB  */
		dbg_gc("LEB %d is free, return it", lp->lnum);
		ubifs_assert(!(lp->flags & LPROPS_INDEX));

		if (lp->free != c->leb_size) {
			/*
			 * Write buffers must be sync'd before unmapping
			 * freeable LEBs, because one of them may contain data
			 * which obsoletes something in 'lp->pnum'.
			 */
			err = gc_sync_wbufs(c);
			if (err)
				return err;
			err = ubifs_change_one_lp(c, lp->lnum, c->leb_size,
						  0, 0, 0, 0);
			if (err)
				return err;
		}
		err = ubifs_leb_unmap(c, lp->lnum);
		if (err)
			return err;

		if (c->gc_lnum == -1) {
			c->gc_lnum = lnum;
			return LEB_RETAINED;
		}

		return LEB_FREED;
	}

	/*
	 * We scan the entire LEB even though we only really need to scan up to
	 * (c->leb_size - lp->free).
	 */
	sleb = ubifs_scan(c, lnum, 0, c->sbuf, 0);
	if (IS_ERR(sleb))
		return PTR_ERR(sleb);

	ubifs_assert(!list_empty(&sleb->nodes));
	snod = list_entry(sleb->nodes.next, struct ubifs_scan_node, list);

	if (snod->type == UBIFS_IDX_NODE) {
		struct ubifs_gced_idx_leb *idx_gc;

		dbg_gc("indexing LEB %d (free %d, dirty %d)",
		       lnum, lp->free, lp->dirty);
		list_for_each_entry(snod, &sleb->nodes, list) {
			struct ubifs_idx_node *idx = snod->node;
			int level = le16_to_cpu(idx->level);

			ubifs_assert(snod->type == UBIFS_IDX_NODE);
			key_read(c, ubifs_idx_key(c, idx), &snod->key);
			err = ubifs_dirty_idx_node(c, &snod->key, level, lnum,
						   snod->offs);
			if (err)
				goto out;
		}

		idx_gc = kmalloc(sizeof(struct ubifs_gced_idx_leb), GFP_NOFS);
		if (!idx_gc) {
			err = -ENOMEM;
			goto out;
		}

		idx_gc->lnum = lnum;
		idx_gc->unmap = 0;
		list_add(&idx_gc->list, &c->idx_gc);

		/*
		 * Don't release the LEB until after the next commit, because
		 * it may contain data which is needed for recovery. So
		 * although we freed this LEB, it will become usable only after
		 * the commit.
		 */
		err = ubifs_change_one_lp(c, lnum, c->leb_size, 0, 0,
					  LPROPS_INDEX, 1);
		if (err)
			goto out;
		err = LEB_FREED_IDX;
	} else {
		dbg_gc("data LEB %d (free %d, dirty %d)",
		       lnum, lp->free, lp->dirty);

		err = move_nodes(c, sleb);
		if (err)
			goto out_inc_seq;

		err = gc_sync_wbufs(c);
		if (err)
			goto out_inc_seq;

		err = ubifs_change_one_lp(c, lnum, c->leb_size, 0, 0, 0, 0);
		if (err)
			goto out_inc_seq;

		/* Allow for races with TNC */
		c->gced_lnum = lnum;
		smp_wmb();
		c->gc_seq += 1;
		smp_wmb();

		if (c->gc_lnum == -1) {
			c->gc_lnum = lnum;
			err = LEB_RETAINED;
		} else {
			err = ubifs_wbuf_sync_nolock(wbuf);
			if (err)
				goto out;

			err = ubifs_leb_unmap(c, lnum);
			if (err)
				goto out;

			err = LEB_FREED;
		}
	}

out:
	ubifs_scan_destroy(sleb);
	return err;

out_inc_seq:
	/* We may have moved at least some nodes so allow for races with TNC */
	c->gced_lnum = lnum;
	smp_wmb();
	c->gc_seq += 1;
	smp_wmb();
	goto out;
}

/**
 * ubifs_garbage_collect - UBIFS garbage collector.
 * @c: UBIFS file-system description object
 * @anyway: do GC even if there are free LEBs
 *
 * This function does out-of-place garbage collection. The return codes are:
 *   o positive LEB number if the LEB has been freed and may be used;
 *   o %-EAGAIN if the caller has to run commit;
 *   o %-ENOSPC if GC failed to make any progress;
 *   o other negative error codes in case of other errors.
 *
 * Garbage collector writes data to the journal when GC'ing data LEBs, and just
 * marking indexing nodes dirty when GC'ing indexing LEBs. Thus, at some point
 * commit may be required. But commit cannot be run from inside GC, because the
 * caller might be holding the commit lock, so %-EAGAIN is returned instead;
 * And this error code means that the caller has to run commit, and re-run GC
 * if there is still no free space.
 *
 * There are many reasons why this function may return %-EAGAIN:
 * o the log is full and there is no space to write an LEB reference for
 *   @c->gc_lnum;
 * o the journal is too large and exceeds size limitations;
 * o GC moved indexing LEBs, but they can be used only after the commit;
 * o the shrinker fails to find clean znodes to free and requests the commit;
 * o etc.
 *
 * Note, if the file-system is close to be full, this function may return
 * %-EAGAIN infinitely, so the caller has to limit amount of re-invocations of
 * the function. E.g., this happens if the limits on the journal size are too
 * tough and GC writes too much to the journal before an LEB is freed. This
 * might also mean that the journal is too large, and the TNC becomes to big,
 * so that the shrinker is constantly called, finds not clean znodes to free,
 * and requests commit. Well, this may also happen if the journal is all right,
 * but another kernel process consumes too much memory. Anyway, infinite
 * %-EAGAIN may happen, but in some extreme/misconfiguration cases.
 */
int ubifs_garbage_collect(struct ubifs_info *c, int anyway)
{
	int i, err, ret, min_space = c->dead_wm;
	struct ubifs_lprops lp;
	struct ubifs_wbuf *wbuf = &c->jheads[GCHD].wbuf;

	ubifs_assert_cmt_locked(c);
	ubifs_assert(!c->ro_media && !c->ro_mount);

	if (ubifs_gc_should_commit(c))
		return -EAGAIN;

	mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);

	if (c->ro_error) {
		ret = -EROFS;
		goto out_unlock;
	}

	/* We expect the write-buffer to be empty on entry */
	ubifs_assert(!wbuf->used);

	for (i = 0; ; i++) {
		int space_before, space_after;

		cond_resched();

		/* Give the commit an opportunity to run */
		if (ubifs_gc_should_commit(c)) {
			ret = -EAGAIN;
			break;
		}

		if (i > SOFT_LEBS_LIMIT && !list_empty(&c->idx_gc)) {
			/*
			 * We've done enough iterations. Indexing LEBs were
			 * moved and will be available after the commit.
			 */
			dbg_gc("soft limit, some index LEBs GC'ed, -EAGAIN");
			ubifs_commit_required(c);
			ret = -EAGAIN;
			break;
		}

		if (i > HARD_LEBS_LIMIT) {
			/*
			 * We've moved too many LEBs and have not made
			 * progress, give up.
			 */
			dbg_gc("hard limit, -ENOSPC");
			ret = -ENOSPC;
			break;
		}

		/*
		 * Empty and freeable LEBs can turn up while we waited for
		 * the wbuf lock, or while we have been running GC. In that
		 * case, we should just return one of those instead of
		 * continuing to GC dirty LEBs. Hence we request
		 * 'ubifs_find_dirty_leb()' to return an empty LEB if it can.
		 */
		ret = ubifs_find_dirty_leb(c, &lp, min_space, anyway ? 0 : 1);
		if (ret) {
			if (ret == -ENOSPC)
				dbg_gc("no more dirty LEBs");
			break;
		}

		dbg_gc("found LEB %d: free %d, dirty %d, sum %d (min. space %d)",
		       lp.lnum, lp.free, lp.dirty, lp.free + lp.dirty,
		       min_space);

		space_before = c->leb_size - wbuf->offs - wbuf->used;
		if (wbuf->lnum == -1)
			space_before = 0;

		ret = ubifs_garbage_collect_leb(c, &lp);
		if (ret < 0) {
			if (ret == -EAGAIN) {
				/*
				 * This is not error, so we have to return the
				 * LEB to lprops. But if 'ubifs_return_leb()'
				 * fails, its failure code is propagated to the
				 * caller instead of the original '-EAGAIN'.
				 */
				err = ubifs_return_leb(c, lp.lnum);
				if (err)
					ret = err;
				break;
			}
			goto out;
		}

		if (ret == LEB_FREED) {
			/* An LEB has been freed and is ready for use */
			dbg_gc("LEB %d freed, return", lp.lnum);
			ret = lp.lnum;
			break;
		}

		if (ret == LEB_FREED_IDX) {
			/*
			 * This was an indexing LEB and it cannot be
			 * immediately used. And instead of requesting the
			 * commit straight away, we try to garbage collect some
			 * more.
			 */
			dbg_gc("indexing LEB %d freed, continue", lp.lnum);
			continue;
		}

		ubifs_assert(ret == LEB_RETAINED);
		space_after = c->leb_size - wbuf->offs - wbuf->used;
		dbg_gc("LEB %d retained, freed %d bytes", lp.lnum,
		       space_after - space_before);

		if (space_after > space_before) {
			/* GC makes progress, keep working */
			min_space >>= 1;
			if (min_space < c->dead_wm)
				min_space = c->dead_wm;
			continue;
		}

		dbg_gc("did not make progress");

		/*
		 * GC moved an LEB bud have not done any progress. This means
		 * that the previous GC head LEB contained too few free space
		 * and the LEB which was GC'ed contained only large nodes which
		 * did not fit that space.
		 *
		 * We can do 2 things:
		 * 1. pick another LEB in a hope it'll contain a small node
		 *    which will fit the space we have at the end of current GC
		 *    head LEB, but there is no guarantee, so we try this out
		 *    unless we have already been working for too long;
		 * 2. request an LEB with more dirty space, which will force
		 *    'ubifs_find_dirty_leb()' to start scanning the lprops
		 *    table, instead of just picking one from the heap
		 *    (previously it already picked the dirtiest LEB).
		 */
		if (i < SOFT_LEBS_LIMIT) {
			dbg_gc("try again");
			continue;
		}

		min_space <<= 1;
		if (min_space > c->dark_wm)
			min_space = c->dark_wm;
		dbg_gc("set min. space to %d", min_space);
	}

	if (ret == -ENOSPC && !list_empty(&c->idx_gc)) {
		dbg_gc("no space, some index LEBs GC'ed, -EAGAIN");
		ubifs_commit_required(c);
		ret = -EAGAIN;
	}

	err = ubifs_wbuf_sync_nolock(wbuf);
	if (!err)
		err = ubifs_leb_unmap(c, c->gc_lnum);
	if (err) {
		ret = err;
		goto out;
	}
out_unlock:
	mutex_unlock(&wbuf->io_mutex);
	return ret;

out:
	ubifs_assert(ret < 0);
	ubifs_assert(ret != -ENOSPC && ret != -EAGAIN);
	ubifs_wbuf_sync_nolock(wbuf);
	ubifs_ro_mode(c, ret);
	mutex_unlock(&wbuf->io_mutex);
	ubifs_return_leb(c, lp.lnum);
	return ret;
}

/**
 * ubifs_gc_start_commit - garbage collection at start of commit.
 * @c: UBIFS file-system description object
 *
 * If a LEB has only dirty and free space, then we may safely unmap it and make
 * it free.  Note, we cannot do this with indexing LEBs because dirty space may
 * correspond index nodes that are required for recovery.  In that case, the
 * LEB cannot be unmapped until after the next commit.
 *
 * This function returns %0 upon success and a negative error code upon failure.
 */
int ubifs_gc_start_commit(struct ubifs_info *c)
{
	struct ubifs_gced_idx_leb *idx_gc;
	const struct ubifs_lprops *lp;
	int err = 0, flags;

	ubifs_get_lprops(c);

	/*
	 * Unmap (non-index) freeable LEBs. Note that recovery requires that all
	 * wbufs are sync'd before this, which is done in 'do_commit()'.
	 */
	while (1) {
		lp = ubifs_fast_find_freeable(c);
		if (IS_ERR(lp)) {
			err = PTR_ERR(lp);
			goto out;
		}
		if (!lp)
			break;
		ubifs_assert(!(lp->flags & LPROPS_TAKEN));
		ubifs_assert(!(lp->flags & LPROPS_INDEX));
		err = ubifs_leb_unmap(c, lp->lnum);
		if (err)
			goto out;
		lp = ubifs_change_lp(c, lp, c->leb_size, 0, lp->flags, 0);
		if (IS_ERR(lp)) {
			err = PTR_ERR(lp);
			goto out;
		}
		ubifs_assert(!(lp->flags & LPROPS_TAKEN));
		ubifs_assert(!(lp->flags & LPROPS_INDEX));
	}

	/* Mark GC'd index LEBs OK to unmap after this commit finishes */
	list_for_each_entry(idx_gc, &c->idx_gc, list)
		idx_gc->unmap = 1;

	/* Record index freeable LEBs for unmapping after commit */
	while (1) {
		lp = ubifs_fast_find_frdi_idx(c);
		if (IS_ERR(lp)) {
			err = PTR_ERR(lp);
			goto out;
		}
		if (!lp)
			break;
		idx_gc = kmalloc(sizeof(struct ubifs_gced_idx_leb), GFP_NOFS);
		if (!idx_gc) {
			err = -ENOMEM;
			goto out;
		}
		ubifs_assert(!(lp->flags & LPROPS_TAKEN));
		ubifs_assert(lp->flags & LPROPS_INDEX);
		/* Don't release the LEB until after the next commit */
		flags = (lp->flags | LPROPS_TAKEN) ^ LPROPS_INDEX;
		lp = ubifs_change_lp(c, lp, c->leb_size, 0, flags, 1);
		if (IS_ERR(lp)) {
			err = PTR_ERR(lp);
			kfree(idx_gc);
			goto out;
		}
		ubifs_assert(lp->flags & LPROPS_TAKEN);
		ubifs_assert(!(lp->flags & LPROPS_INDEX));
		idx_gc->lnum = lp->lnum;
		idx_gc->unmap = 1;
		list_add(&idx_gc->list, &c->idx_gc);
	}
out:
	ubifs_release_lprops(c);
	return err;
}

/**
 * ubifs_gc_end_commit - garbage collection at end of commit.
 * @c: UBIFS file-system description object
 *
 * This function completes out-of-place garbage collection of index LEBs.
 */
int ubifs_gc_end_commit(struct ubifs_info *c)
{
	struct ubifs_gced_idx_leb *idx_gc, *tmp;
	struct ubifs_wbuf *wbuf;
	int err = 0;

	wbuf = &c->jheads[GCHD].wbuf;
	mutex_lock_nested(&wbuf->io_mutex, wbuf->jhead);
	list_for_each_entry_safe(idx_gc, tmp, &c->idx_gc, list)
		if (idx_gc->unmap) {
			dbg_gc("LEB %d", idx_gc->lnum);
			err = ubifs_leb_unmap(c, idx_gc->lnum);
			if (err)
				goto out;
			err = ubifs_change_one_lp(c, idx_gc->lnum, LPROPS_NC,
					  LPROPS_NC, 0, LPROPS_TAKEN, -1);
			if (err)
				goto out;
			list_del(&idx_gc->list);
			kfree(idx_gc);
		}
out:
	mutex_unlock(&wbuf->io_mutex);
	return err;
}

/**
 * ubifs_destroy_idx_gc - destroy idx_gc list.
 * @c: UBIFS file-system description object
 *
 * This function destroys the @c->idx_gc list. It is called when unmounting
 * so locks are not needed. Returns zero in case of success and a negative
 * error code in case of failure.
 */
void ubifs_destroy_idx_gc(struct ubifs_info *c)
{
	while (!list_empty(&c->idx_gc)) {
		struct ubifs_gced_idx_leb *idx_gc;

		idx_gc = list_entry(c->idx_gc.next, struct ubifs_gced_idx_leb,
				    list);
		c->idx_gc_cnt -= 1;
		list_del(&idx_gc->list);
		kfree(idx_gc);
	}
}

/**
 * ubifs_get_idx_gc_leb - get a LEB from GC'd index LEB list.
 * @c: UBIFS file-system description object
 *
 * Called during start commit so locks are not needed.
 */
int ubifs_get_idx_gc_leb(struct ubifs_info *c)
{
	struct ubifs_gced_idx_leb *idx_gc;
	int lnum;

	if (list_empty(&c->idx_gc))
		return -ENOSPC;
	idx_gc = list_entry(c->idx_gc.next, struct ubifs_gced_idx_leb, list);
	lnum = idx_gc->lnum;
	/* c->idx_gc_cnt is updated by the caller when lprops are updated */
	list_del(&idx_gc->list);
	kfree(idx_gc);
	return lnum;
}
