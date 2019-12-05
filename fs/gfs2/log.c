// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/gfs2_ondisk.h>
#include <linux/crc32.h>
#include <linux/crc32c.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/writeback.h>
#include <linux/list_sort.h>

#include "gfs2.h"
#include "incore.h"
#include "bmap.h"
#include "glock.h"
#include "log.h"
#include "lops.h"
#include "meta_io.h"
#include "util.h"
#include "dir.h"
#include "trace_gfs2.h"

static void gfs2_log_shutdown(struct gfs2_sbd *sdp);

/**
 * gfs2_struct2blk - compute stuff
 * @sdp: the filesystem
 * @nstruct: the number of structures
 * @ssize: the size of the structures
 *
 * Compute the number of log descriptor blocks needed to hold a certain number
 * of structures of a certain size.
 *
 * Returns: the number of blocks needed (minimum is always 1)
 */

unsigned int gfs2_struct2blk(struct gfs2_sbd *sdp, unsigned int nstruct,
			     unsigned int ssize)
{
	unsigned int blks;
	unsigned int first, second;

	blks = 1;
	first = (sdp->sd_sb.sb_bsize - sizeof(struct gfs2_log_descriptor)) / ssize;

	if (nstruct > first) {
		second = (sdp->sd_sb.sb_bsize -
			  sizeof(struct gfs2_meta_header)) / ssize;
		blks += DIV_ROUND_UP(nstruct - first, second);
	}

	return blks;
}

/**
 * gfs2_remove_from_ail - Remove an entry from the ail lists, updating counters
 * @mapping: The associated mapping (maybe NULL)
 * @bd: The gfs2_bufdata to remove
 *
 * The ail lock _must_ be held when calling this function
 *
 */

static void gfs2_remove_from_ail(struct gfs2_bufdata *bd)
{
	bd->bd_tr = NULL;
	list_del_init(&bd->bd_ail_st_list);
	list_del_init(&bd->bd_ail_gl_list);
	atomic_dec(&bd->bd_gl->gl_ail_count);
	brelse(bd->bd_bh);
}

/**
 * gfs2_ail1_start_one - Start I/O on a part of the AIL
 * @sdp: the filesystem
 * @wbc: The writeback control structure
 * @ai: The ail structure
 *
 */

static int gfs2_ail1_start_one(struct gfs2_sbd *sdp,
			       struct writeback_control *wbc,
			       struct gfs2_trans *tr,
			       bool *withdraw)
__releases(&sdp->sd_ail_lock)
__acquires(&sdp->sd_ail_lock)
{
	struct gfs2_glock *gl = NULL;
	struct address_space *mapping;
	struct gfs2_bufdata *bd, *s;
	struct buffer_head *bh;

	list_for_each_entry_safe_reverse(bd, s, &tr->tr_ail1_list, bd_ail_st_list) {
		bh = bd->bd_bh;

		gfs2_assert(sdp, bd->bd_tr == tr);

		if (!buffer_busy(bh)) {
			if (!buffer_uptodate(bh) &&
			    !test_and_set_bit(SDF_AIL1_IO_ERROR,
					      &sdp->sd_flags)) {
				gfs2_io_error_bh(sdp, bh);
				*withdraw = true;
			}
			list_move(&bd->bd_ail_st_list, &tr->tr_ail2_list);
			continue;
		}

		if (!buffer_dirty(bh))
			continue;
		if (gl == bd->bd_gl)
			continue;
		gl = bd->bd_gl;
		list_move(&bd->bd_ail_st_list, &tr->tr_ail1_list);
		mapping = bh->b_page->mapping;
		if (!mapping)
			continue;
		spin_unlock(&sdp->sd_ail_lock);
		generic_writepages(mapping, wbc);
		spin_lock(&sdp->sd_ail_lock);
		if (wbc->nr_to_write <= 0)
			break;
		return 1;
	}

	return 0;
}


/**
 * gfs2_ail1_flush - start writeback of some ail1 entries 
 * @sdp: The super block
 * @wbc: The writeback control structure
 *
 * Writes back some ail1 entries, according to the limits in the
 * writeback control structure
 */

void gfs2_ail1_flush(struct gfs2_sbd *sdp, struct writeback_control *wbc)
{
	struct list_head *head = &sdp->sd_ail1_list;
	struct gfs2_trans *tr;
	struct blk_plug plug;
	bool withdraw = false;

	trace_gfs2_ail_flush(sdp, wbc, 1);
	blk_start_plug(&plug);
	spin_lock(&sdp->sd_ail_lock);
restart:
	list_for_each_entry_reverse(tr, head, tr_list) {
		if (wbc->nr_to_write <= 0)
			break;
		if (gfs2_ail1_start_one(sdp, wbc, tr, &withdraw) &&
		    !gfs2_withdrawn(sdp))
			goto restart;
	}
	spin_unlock(&sdp->sd_ail_lock);
	blk_finish_plug(&plug);
	if (withdraw)
		gfs2_lm_withdraw(sdp, NULL);
	trace_gfs2_ail_flush(sdp, wbc, 0);
}

/**
 * gfs2_ail1_start - start writeback of all ail1 entries
 * @sdp: The superblock
 */

static void gfs2_ail1_start(struct gfs2_sbd *sdp)
{
	struct writeback_control wbc = {
		.sync_mode = WB_SYNC_NONE,
		.nr_to_write = LONG_MAX,
		.range_start = 0,
		.range_end = LLONG_MAX,
	};

	return gfs2_ail1_flush(sdp, &wbc);
}

/**
 * gfs2_ail1_empty_one - Check whether or not a trans in the AIL has been synced
 * @sdp: the filesystem
 * @ai: the AIL entry
 *
 */

static void gfs2_ail1_empty_one(struct gfs2_sbd *sdp, struct gfs2_trans *tr,
				bool *withdraw)
{
	struct gfs2_bufdata *bd, *s;
	struct buffer_head *bh;

	list_for_each_entry_safe_reverse(bd, s, &tr->tr_ail1_list,
					 bd_ail_st_list) {
		bh = bd->bd_bh;
		gfs2_assert(sdp, bd->bd_tr == tr);
		if (buffer_busy(bh))
			continue;
		if (!buffer_uptodate(bh) &&
		    !test_and_set_bit(SDF_AIL1_IO_ERROR, &sdp->sd_flags)) {
			gfs2_io_error_bh(sdp, bh);
			*withdraw = true;
		}
		list_move(&bd->bd_ail_st_list, &tr->tr_ail2_list);
	}
}

/**
 * gfs2_ail1_empty - Try to empty the ail1 lists
 * @sdp: The superblock
 *
 * Tries to empty the ail1 lists, starting with the oldest first
 */

static int gfs2_ail1_empty(struct gfs2_sbd *sdp)
{
	struct gfs2_trans *tr, *s;
	int oldest_tr = 1;
	int ret;
	bool withdraw = false;

	spin_lock(&sdp->sd_ail_lock);
	list_for_each_entry_safe_reverse(tr, s, &sdp->sd_ail1_list, tr_list) {
		gfs2_ail1_empty_one(sdp, tr, &withdraw);
		if (list_empty(&tr->tr_ail1_list) && oldest_tr)
			list_move(&tr->tr_list, &sdp->sd_ail2_list);
		else
			oldest_tr = 0;
	}
	ret = list_empty(&sdp->sd_ail1_list);
	spin_unlock(&sdp->sd_ail_lock);

	if (withdraw)
		gfs2_lm_withdraw(sdp, "fatal: I/O error(s)\n");

	return ret;
}

static void gfs2_ail1_wait(struct gfs2_sbd *sdp)
{
	struct gfs2_trans *tr;
	struct gfs2_bufdata *bd;
	struct buffer_head *bh;

	spin_lock(&sdp->sd_ail_lock);
	list_for_each_entry_reverse(tr, &sdp->sd_ail1_list, tr_list) {
		list_for_each_entry(bd, &tr->tr_ail1_list, bd_ail_st_list) {
			bh = bd->bd_bh;
			if (!buffer_locked(bh))
				continue;
			get_bh(bh);
			spin_unlock(&sdp->sd_ail_lock);
			wait_on_buffer(bh);
			brelse(bh);
			return;
		}
	}
	spin_unlock(&sdp->sd_ail_lock);
}

/**
 * gfs2_ail2_empty_one - Check whether or not a trans in the AIL has been synced
 * @sdp: the filesystem
 * @ai: the AIL entry
 *
 */

static void gfs2_ail2_empty_one(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	struct list_head *head = &tr->tr_ail2_list;
	struct gfs2_bufdata *bd;

	while (!list_empty(head)) {
		bd = list_entry(head->prev, struct gfs2_bufdata,
				bd_ail_st_list);
		gfs2_assert(sdp, bd->bd_tr == tr);
		gfs2_remove_from_ail(bd);
	}
}

static void ail2_empty(struct gfs2_sbd *sdp, unsigned int new_tail)
{
	struct gfs2_trans *tr, *safe;
	unsigned int old_tail = sdp->sd_log_tail;
	int wrap = (new_tail < old_tail);
	int a, b, rm;

	spin_lock(&sdp->sd_ail_lock);

	list_for_each_entry_safe(tr, safe, &sdp->sd_ail2_list, tr_list) {
		a = (old_tail <= tr->tr_first);
		b = (tr->tr_first < new_tail);
		rm = (wrap) ? (a || b) : (a && b);
		if (!rm)
			continue;

		gfs2_ail2_empty_one(sdp, tr);
		list_del(&tr->tr_list);
		gfs2_assert_warn(sdp, list_empty(&tr->tr_ail1_list));
		gfs2_assert_warn(sdp, list_empty(&tr->tr_ail2_list));
		kfree(tr);
	}

	spin_unlock(&sdp->sd_ail_lock);
}

/**
 * gfs2_log_release - Release a given number of log blocks
 * @sdp: The GFS2 superblock
 * @blks: The number of blocks
 *
 */

void gfs2_log_release(struct gfs2_sbd *sdp, unsigned int blks)
{

	atomic_add(blks, &sdp->sd_log_blks_free);
	trace_gfs2_log_blocks(sdp, blks);
	gfs2_assert_withdraw(sdp, atomic_read(&sdp->sd_log_blks_free) <=
				  sdp->sd_jdesc->jd_blocks);
	up_read(&sdp->sd_log_flush_lock);
}

/**
 * gfs2_log_reserve - Make a log reservation
 * @sdp: The GFS2 superblock
 * @blks: The number of blocks to reserve
 *
 * Note that we never give out the last few blocks of the journal. Thats
 * due to the fact that there is a small number of header blocks
 * associated with each log flush. The exact number can't be known until
 * flush time, so we ensure that we have just enough free blocks at all
 * times to avoid running out during a log flush.
 *
 * We no longer flush the log here, instead we wake up logd to do that
 * for us. To avoid the thundering herd and to ensure that we deal fairly
 * with queued waiters, we use an exclusive wait. This means that when we
 * get woken with enough journal space to get our reservation, we need to
 * wake the next waiter on the list.
 *
 * Returns: errno
 */

int gfs2_log_reserve(struct gfs2_sbd *sdp, unsigned int blks)
{
	int ret = 0;
	unsigned reserved_blks = 7 * (4096 / sdp->sd_vfs->s_blocksize);
	unsigned wanted = blks + reserved_blks;
	DEFINE_WAIT(wait);
	int did_wait = 0;
	unsigned int free_blocks;

	if (gfs2_assert_warn(sdp, blks) ||
	    gfs2_assert_warn(sdp, blks <= sdp->sd_jdesc->jd_blocks))
		return -EINVAL;
	atomic_add(blks, &sdp->sd_log_blks_needed);
retry:
	free_blocks = atomic_read(&sdp->sd_log_blks_free);
	if (unlikely(free_blocks <= wanted)) {
		do {
			prepare_to_wait_exclusive(&sdp->sd_log_waitq, &wait,
					TASK_UNINTERRUPTIBLE);
			wake_up(&sdp->sd_logd_waitq);
			did_wait = 1;
			if (atomic_read(&sdp->sd_log_blks_free) <= wanted)
				io_schedule();
			free_blocks = atomic_read(&sdp->sd_log_blks_free);
		} while(free_blocks <= wanted);
		finish_wait(&sdp->sd_log_waitq, &wait);
	}
	atomic_inc(&sdp->sd_reserving_log);
	if (atomic_cmpxchg(&sdp->sd_log_blks_free, free_blocks,
				free_blocks - blks) != free_blocks) {
		if (atomic_dec_and_test(&sdp->sd_reserving_log))
			wake_up(&sdp->sd_reserving_log_wait);
		goto retry;
	}
	atomic_sub(blks, &sdp->sd_log_blks_needed);
	trace_gfs2_log_blocks(sdp, -blks);

	/*
	 * If we waited, then so might others, wake them up _after_ we get
	 * our share of the log.
	 */
	if (unlikely(did_wait))
		wake_up(&sdp->sd_log_waitq);

	down_read(&sdp->sd_log_flush_lock);
	if (unlikely(!test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags))) {
		gfs2_log_release(sdp, blks);
		ret = -EROFS;
	}
	if (atomic_dec_and_test(&sdp->sd_reserving_log))
		wake_up(&sdp->sd_reserving_log_wait);
	return ret;
}

/**
 * log_distance - Compute distance between two journal blocks
 * @sdp: The GFS2 superblock
 * @newer: The most recent journal block of the pair
 * @older: The older journal block of the pair
 *
 *   Compute the distance (in the journal direction) between two
 *   blocks in the journal
 *
 * Returns: the distance in blocks
 */

static inline unsigned int log_distance(struct gfs2_sbd *sdp, unsigned int newer,
					unsigned int older)
{
	int dist;

	dist = newer - older;
	if (dist < 0)
		dist += sdp->sd_jdesc->jd_blocks;

	return dist;
}

/**
 * calc_reserved - Calculate the number of blocks to reserve when
 *                 refunding a transaction's unused buffers.
 * @sdp: The GFS2 superblock
 *
 * This is complex.  We need to reserve room for all our currently used
 * metadata buffers (e.g. normal file I/O rewriting file time stamps) and 
 * all our journaled data buffers for journaled files (e.g. files in the 
 * meta_fs like rindex, or files for which chattr +j was done.)
 * If we don't reserve enough space, gfs2_log_refund and gfs2_log_flush
 * will count it as free space (sd_log_blks_free) and corruption will follow.
 *
 * We can have metadata bufs and jdata bufs in the same journal.  So each
 * type gets its own log header, for which we need to reserve a block.
 * In fact, each type has the potential for needing more than one header 
 * in cases where we have more buffers than will fit on a journal page.
 * Metadata journal entries take up half the space of journaled buffer entries.
 * Thus, metadata entries have buf_limit (502) and journaled buffers have
 * databuf_limit (251) before they cause a wrap around.
 *
 * Also, we need to reserve blocks for revoke journal entries and one for an
 * overall header for the lot.
 *
 * Returns: the number of blocks reserved
 */
static unsigned int calc_reserved(struct gfs2_sbd *sdp)
{
	unsigned int reserved = 0;
	unsigned int mbuf;
	unsigned int dbuf;
	struct gfs2_trans *tr = sdp->sd_log_tr;

	if (tr) {
		mbuf = tr->tr_num_buf_new - tr->tr_num_buf_rm;
		dbuf = tr->tr_num_databuf_new - tr->tr_num_databuf_rm;
		reserved = mbuf + dbuf;
		/* Account for header blocks */
		reserved += DIV_ROUND_UP(mbuf, buf_limit(sdp));
		reserved += DIV_ROUND_UP(dbuf, databuf_limit(sdp));
	}

	if (sdp->sd_log_commited_revoke > 0)
		reserved += gfs2_struct2blk(sdp, sdp->sd_log_commited_revoke,
					  sizeof(u64));
	/* One for the overall header */
	if (reserved)
		reserved++;
	return reserved;
}

static unsigned int current_tail(struct gfs2_sbd *sdp)
{
	struct gfs2_trans *tr;
	unsigned int tail;

	spin_lock(&sdp->sd_ail_lock);

	if (list_empty(&sdp->sd_ail1_list)) {
		tail = sdp->sd_log_head;
	} else {
		tr = list_entry(sdp->sd_ail1_list.prev, struct gfs2_trans,
				tr_list);
		tail = tr->tr_first;
	}

	spin_unlock(&sdp->sd_ail_lock);

	return tail;
}

static void log_pull_tail(struct gfs2_sbd *sdp, unsigned int new_tail)
{
	unsigned int dist = log_distance(sdp, new_tail, sdp->sd_log_tail);

	ail2_empty(sdp, new_tail);

	atomic_add(dist, &sdp->sd_log_blks_free);
	trace_gfs2_log_blocks(sdp, dist);
	gfs2_assert_withdraw(sdp, atomic_read(&sdp->sd_log_blks_free) <=
			     sdp->sd_jdesc->jd_blocks);

	sdp->sd_log_tail = new_tail;
}


static void log_flush_wait(struct gfs2_sbd *sdp)
{
	DEFINE_WAIT(wait);

	if (atomic_read(&sdp->sd_log_in_flight)) {
		do {
			prepare_to_wait(&sdp->sd_log_flush_wait, &wait,
					TASK_UNINTERRUPTIBLE);
			if (atomic_read(&sdp->sd_log_in_flight))
				io_schedule();
		} while(atomic_read(&sdp->sd_log_in_flight));
		finish_wait(&sdp->sd_log_flush_wait, &wait);
	}
}

static int ip_cmp(void *priv, struct list_head *a, struct list_head *b)
{
	struct gfs2_inode *ipa, *ipb;

	ipa = list_entry(a, struct gfs2_inode, i_ordered);
	ipb = list_entry(b, struct gfs2_inode, i_ordered);

	if (ipa->i_no_addr < ipb->i_no_addr)
		return -1;
	if (ipa->i_no_addr > ipb->i_no_addr)
		return 1;
	return 0;
}

static void gfs2_ordered_write(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip;
	LIST_HEAD(written);

	spin_lock(&sdp->sd_ordered_lock);
	list_sort(NULL, &sdp->sd_log_ordered, &ip_cmp);
	while (!list_empty(&sdp->sd_log_ordered)) {
		ip = list_entry(sdp->sd_log_ordered.next, struct gfs2_inode, i_ordered);
		if (ip->i_inode.i_mapping->nrpages == 0) {
			test_and_clear_bit(GIF_ORDERED, &ip->i_flags);
			list_del(&ip->i_ordered);
			continue;
		}
		list_move(&ip->i_ordered, &written);
		spin_unlock(&sdp->sd_ordered_lock);
		filemap_fdatawrite(ip->i_inode.i_mapping);
		spin_lock(&sdp->sd_ordered_lock);
	}
	list_splice(&written, &sdp->sd_log_ordered);
	spin_unlock(&sdp->sd_ordered_lock);
}

static void gfs2_ordered_wait(struct gfs2_sbd *sdp)
{
	struct gfs2_inode *ip;

	spin_lock(&sdp->sd_ordered_lock);
	while (!list_empty(&sdp->sd_log_ordered)) {
		ip = list_entry(sdp->sd_log_ordered.next, struct gfs2_inode, i_ordered);
		list_del(&ip->i_ordered);
		WARN_ON(!test_and_clear_bit(GIF_ORDERED, &ip->i_flags));
		if (ip->i_inode.i_mapping->nrpages == 0)
			continue;
		spin_unlock(&sdp->sd_ordered_lock);
		filemap_fdatawait(ip->i_inode.i_mapping);
		spin_lock(&sdp->sd_ordered_lock);
	}
	spin_unlock(&sdp->sd_ordered_lock);
}

void gfs2_ordered_del_inode(struct gfs2_inode *ip)
{
	struct gfs2_sbd *sdp = GFS2_SB(&ip->i_inode);

	spin_lock(&sdp->sd_ordered_lock);
	if (test_and_clear_bit(GIF_ORDERED, &ip->i_flags))
		list_del(&ip->i_ordered);
	spin_unlock(&sdp->sd_ordered_lock);
}

void gfs2_add_revoke(struct gfs2_sbd *sdp, struct gfs2_bufdata *bd)
{
	struct buffer_head *bh = bd->bd_bh;
	struct gfs2_glock *gl = bd->bd_gl;

	bh->b_private = NULL;
	bd->bd_blkno = bh->b_blocknr;
	gfs2_remove_from_ail(bd); /* drops ref on bh */
	bd->bd_bh = NULL;
	sdp->sd_log_num_revoke++;
	if (atomic_inc_return(&gl->gl_revokes) == 1)
		gfs2_glock_hold(gl);
	set_bit(GLF_LFLUSH, &gl->gl_flags);
	list_add(&bd->bd_list, &sdp->sd_log_revokes);
}

void gfs2_glock_remove_revoke(struct gfs2_glock *gl)
{
	if (atomic_dec_return(&gl->gl_revokes) == 0) {
		clear_bit(GLF_LFLUSH, &gl->gl_flags);
		gfs2_glock_queue_put(gl);
	}
}

void gfs2_write_revokes(struct gfs2_sbd *sdp)
{
	struct gfs2_trans *tr;
	struct gfs2_bufdata *bd, *tmp;
	int have_revokes = 0;
	int max_revokes = (sdp->sd_sb.sb_bsize - sizeof(struct gfs2_log_descriptor)) / sizeof(u64);

	gfs2_ail1_empty(sdp);
	spin_lock(&sdp->sd_ail_lock);
	list_for_each_entry_reverse(tr, &sdp->sd_ail1_list, tr_list) {
		list_for_each_entry(bd, &tr->tr_ail2_list, bd_ail_st_list) {
			if (list_empty(&bd->bd_list)) {
				have_revokes = 1;
				goto done;
			}
		}
	}
done:
	spin_unlock(&sdp->sd_ail_lock);
	if (have_revokes == 0)
		return;
	while (sdp->sd_log_num_revoke > max_revokes)
		max_revokes += (sdp->sd_sb.sb_bsize - sizeof(struct gfs2_meta_header)) / sizeof(u64);
	max_revokes -= sdp->sd_log_num_revoke;
	if (!sdp->sd_log_num_revoke) {
		atomic_dec(&sdp->sd_log_blks_free);
		/* If no blocks have been reserved, we need to also
		 * reserve a block for the header */
		if (!sdp->sd_log_blks_reserved)
			atomic_dec(&sdp->sd_log_blks_free);
	}
	gfs2_log_lock(sdp);
	spin_lock(&sdp->sd_ail_lock);
	list_for_each_entry_reverse(tr, &sdp->sd_ail1_list, tr_list) {
		list_for_each_entry_safe(bd, tmp, &tr->tr_ail2_list, bd_ail_st_list) {
			if (max_revokes == 0)
				goto out_of_blocks;
			if (!list_empty(&bd->bd_list))
				continue;
			gfs2_add_revoke(sdp, bd);
			max_revokes--;
		}
	}
out_of_blocks:
	spin_unlock(&sdp->sd_ail_lock);
	gfs2_log_unlock(sdp);

	if (!sdp->sd_log_num_revoke) {
		atomic_inc(&sdp->sd_log_blks_free);
		if (!sdp->sd_log_blks_reserved)
			atomic_inc(&sdp->sd_log_blks_free);
	}
}

/**
 * gfs2_write_log_header - Write a journal log header buffer at lblock
 * @sdp: The GFS2 superblock
 * @jd: journal descriptor of the journal to which we are writing
 * @seq: sequence number
 * @tail: tail of the log
 * @lblock: value for lh_blkno (block number relative to start of journal)
 * @flags: log header flags GFS2_LOG_HEAD_*
 * @op_flags: flags to pass to the bio
 *
 * Returns: the initialized log buffer descriptor
 */

void gfs2_write_log_header(struct gfs2_sbd *sdp, struct gfs2_jdesc *jd,
			   u64 seq, u32 tail, u32 lblock, u32 flags,
			   int op_flags)
{
	struct gfs2_log_header *lh;
	u32 hash, crc;
	struct page *page;
	struct gfs2_statfs_change_host *l_sc = &sdp->sd_statfs_local;
	struct timespec64 tv;
	struct super_block *sb = sdp->sd_vfs;
	u64 dblock;

	if (gfs2_withdrawn(sdp))
		goto out;

	page = mempool_alloc(gfs2_page_pool, GFP_NOIO);
	lh = page_address(page);
	clear_page(lh);

	lh->lh_header.mh_magic = cpu_to_be32(GFS2_MAGIC);
	lh->lh_header.mh_type = cpu_to_be32(GFS2_METATYPE_LH);
	lh->lh_header.__pad0 = cpu_to_be64(0);
	lh->lh_header.mh_format = cpu_to_be32(GFS2_FORMAT_LH);
	lh->lh_header.mh_jid = cpu_to_be32(sdp->sd_jdesc->jd_jid);
	lh->lh_sequence = cpu_to_be64(seq);
	lh->lh_flags = cpu_to_be32(flags);
	lh->lh_tail = cpu_to_be32(tail);
	lh->lh_blkno = cpu_to_be32(lblock);
	hash = ~crc32(~0, lh, LH_V1_SIZE);
	lh->lh_hash = cpu_to_be32(hash);

	ktime_get_coarse_real_ts64(&tv);
	lh->lh_nsec = cpu_to_be32(tv.tv_nsec);
	lh->lh_sec = cpu_to_be64(tv.tv_sec);
	if (!list_empty(&jd->extent_list))
		dblock = gfs2_log_bmap(jd, lblock);
	else {
		int ret = gfs2_lblk_to_dblk(jd->jd_inode, lblock, &dblock);
		if (gfs2_assert_withdraw(sdp, ret == 0))
			return;
	}
	lh->lh_addr = cpu_to_be64(dblock);
	lh->lh_jinode = cpu_to_be64(GFS2_I(jd->jd_inode)->i_no_addr);

	/* We may only write local statfs, quota, etc., when writing to our
	   own journal. The values are left 0 when recovering a journal
	   different from our own. */
	if (!(flags & GFS2_LOG_HEAD_RECOVERY)) {
		lh->lh_statfs_addr =
			cpu_to_be64(GFS2_I(sdp->sd_sc_inode)->i_no_addr);
		lh->lh_quota_addr =
			cpu_to_be64(GFS2_I(sdp->sd_qc_inode)->i_no_addr);

		spin_lock(&sdp->sd_statfs_spin);
		lh->lh_local_total = cpu_to_be64(l_sc->sc_total);
		lh->lh_local_free = cpu_to_be64(l_sc->sc_free);
		lh->lh_local_dinodes = cpu_to_be64(l_sc->sc_dinodes);
		spin_unlock(&sdp->sd_statfs_spin);
	}

	BUILD_BUG_ON(offsetof(struct gfs2_log_header, lh_crc) != LH_V1_SIZE);

	crc = crc32c(~0, (void *)lh + LH_V1_SIZE + 4,
		     sb->s_blocksize - LH_V1_SIZE - 4);
	lh->lh_crc = cpu_to_be32(crc);

	gfs2_log_write(sdp, page, sb->s_blocksize, 0, dblock);
	gfs2_log_submit_bio(&sdp->sd_log_bio, REQ_OP_WRITE | op_flags);
out:
	log_flush_wait(sdp);
}

/**
 * log_write_header - Get and initialize a journal header buffer
 * @sdp: The GFS2 superblock
 * @flags: The log header flags, including log header origin
 *
 * Returns: the initialized log buffer descriptor
 */

static void log_write_header(struct gfs2_sbd *sdp, u32 flags)
{
	unsigned int tail;
	int op_flags = REQ_PREFLUSH | REQ_FUA | REQ_META | REQ_SYNC;
	enum gfs2_freeze_state state = atomic_read(&sdp->sd_freeze_state);

	gfs2_assert_withdraw(sdp, (state != SFS_FROZEN));
	tail = current_tail(sdp);

	if (test_bit(SDF_NOBARRIERS, &sdp->sd_flags)) {
		gfs2_ordered_wait(sdp);
		log_flush_wait(sdp);
		op_flags = REQ_SYNC | REQ_META | REQ_PRIO;
	}
	sdp->sd_log_idle = (tail == sdp->sd_log_flush_head);
	gfs2_write_log_header(sdp, sdp->sd_jdesc, sdp->sd_log_sequence++, tail,
			      sdp->sd_log_flush_head, flags, op_flags);
	gfs2_log_incr_head(sdp);

	if (sdp->sd_log_tail != tail)
		log_pull_tail(sdp, tail);
}

/**
 * gfs2_log_flush - flush incore transaction(s)
 * @sdp: the filesystem
 * @gl: The glock structure to flush.  If NULL, flush the whole incore log
 * @flags: The log header flags: GFS2_LOG_HEAD_FLUSH_* and debug flags
 *
 */

void gfs2_log_flush(struct gfs2_sbd *sdp, struct gfs2_glock *gl, u32 flags)
{
	struct gfs2_trans *tr;
	enum gfs2_freeze_state state = atomic_read(&sdp->sd_freeze_state);

	down_write(&sdp->sd_log_flush_lock);

	/* Log might have been flushed while we waited for the flush lock */
	if (gl && !test_bit(GLF_LFLUSH, &gl->gl_flags)) {
		up_write(&sdp->sd_log_flush_lock);
		return;
	}
	trace_gfs2_log_flush(sdp, 1, flags);

	if (flags & GFS2_LOG_HEAD_FLUSH_SHUTDOWN)
		clear_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags);

	sdp->sd_log_flush_head = sdp->sd_log_head;
	tr = sdp->sd_log_tr;
	if (tr) {
		sdp->sd_log_tr = NULL;
		INIT_LIST_HEAD(&tr->tr_ail1_list);
		INIT_LIST_HEAD(&tr->tr_ail2_list);
		tr->tr_first = sdp->sd_log_flush_head;
		if (unlikely (state == SFS_FROZEN))
			gfs2_assert_withdraw(sdp, !tr->tr_num_buf_new && !tr->tr_num_databuf_new);
	}

	if (unlikely(state == SFS_FROZEN))
		gfs2_assert_withdraw(sdp, !sdp->sd_log_num_revoke);
	gfs2_assert_withdraw(sdp,
			sdp->sd_log_num_revoke == sdp->sd_log_commited_revoke);

	gfs2_ordered_write(sdp);
	lops_before_commit(sdp, tr);
	gfs2_log_submit_bio(&sdp->sd_log_bio, REQ_OP_WRITE);

	if (sdp->sd_log_head != sdp->sd_log_flush_head) {
		log_flush_wait(sdp);
		log_write_header(sdp, flags);
	} else if (sdp->sd_log_tail != current_tail(sdp) && !sdp->sd_log_idle){
		atomic_dec(&sdp->sd_log_blks_free); /* Adjust for unreserved buffer */
		trace_gfs2_log_blocks(sdp, -1);
		log_write_header(sdp, flags);
	}
	lops_after_commit(sdp, tr);

	gfs2_log_lock(sdp);
	sdp->sd_log_head = sdp->sd_log_flush_head;
	sdp->sd_log_blks_reserved = 0;
	sdp->sd_log_commited_revoke = 0;

	spin_lock(&sdp->sd_ail_lock);
	if (tr && !list_empty(&tr->tr_ail1_list)) {
		list_add(&tr->tr_list, &sdp->sd_ail1_list);
		tr = NULL;
	}
	spin_unlock(&sdp->sd_ail_lock);
	gfs2_log_unlock(sdp);

	if (!(flags & GFS2_LOG_HEAD_FLUSH_NORMAL)) {
		if (!sdp->sd_log_idle) {
			for (;;) {
				gfs2_ail1_start(sdp);
				gfs2_ail1_wait(sdp);
				if (gfs2_ail1_empty(sdp))
					break;
			}
			atomic_dec(&sdp->sd_log_blks_free); /* Adjust for unreserved buffer */
			trace_gfs2_log_blocks(sdp, -1);
			log_write_header(sdp, flags);
			sdp->sd_log_head = sdp->sd_log_flush_head;
		}
		if (flags & (GFS2_LOG_HEAD_FLUSH_SHUTDOWN |
			     GFS2_LOG_HEAD_FLUSH_FREEZE))
			gfs2_log_shutdown(sdp);
		if (flags & GFS2_LOG_HEAD_FLUSH_FREEZE)
			atomic_set(&sdp->sd_freeze_state, SFS_FROZEN);
	}

	trace_gfs2_log_flush(sdp, 0, flags);
	up_write(&sdp->sd_log_flush_lock);

	kfree(tr);
}

/**
 * gfs2_merge_trans - Merge a new transaction into a cached transaction
 * @old: Original transaction to be expanded
 * @new: New transaction to be merged
 */

static void gfs2_merge_trans(struct gfs2_trans *old, struct gfs2_trans *new)
{
	WARN_ON_ONCE(!test_bit(TR_ATTACHED, &old->tr_flags));

	old->tr_num_buf_new	+= new->tr_num_buf_new;
	old->tr_num_databuf_new	+= new->tr_num_databuf_new;
	old->tr_num_buf_rm	+= new->tr_num_buf_rm;
	old->tr_num_databuf_rm	+= new->tr_num_databuf_rm;
	old->tr_num_revoke	+= new->tr_num_revoke;

	list_splice_tail_init(&new->tr_databuf, &old->tr_databuf);
	list_splice_tail_init(&new->tr_buf, &old->tr_buf);
}

static void log_refund(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	unsigned int reserved;
	unsigned int unused;
	unsigned int maxres;

	gfs2_log_lock(sdp);

	if (sdp->sd_log_tr) {
		gfs2_merge_trans(sdp->sd_log_tr, tr);
	} else if (tr->tr_num_buf_new || tr->tr_num_databuf_new) {
		gfs2_assert_withdraw(sdp, test_bit(TR_ALLOCED, &tr->tr_flags));
		sdp->sd_log_tr = tr;
		set_bit(TR_ATTACHED, &tr->tr_flags);
	}

	sdp->sd_log_commited_revoke += tr->tr_num_revoke;
	reserved = calc_reserved(sdp);
	maxres = sdp->sd_log_blks_reserved + tr->tr_reserved;
	gfs2_assert_withdraw(sdp, maxres >= reserved);
	unused = maxres - reserved;
	atomic_add(unused, &sdp->sd_log_blks_free);
	trace_gfs2_log_blocks(sdp, unused);
	gfs2_assert_withdraw(sdp, atomic_read(&sdp->sd_log_blks_free) <=
			     sdp->sd_jdesc->jd_blocks);
	sdp->sd_log_blks_reserved = reserved;

	gfs2_log_unlock(sdp);
}

/**
 * gfs2_log_commit - Commit a transaction to the log
 * @sdp: the filesystem
 * @tr: the transaction
 *
 * We wake up gfs2_logd if the number of pinned blocks exceed thresh1
 * or the total number of used blocks (pinned blocks plus AIL blocks)
 * is greater than thresh2.
 *
 * At mount time thresh1 is 1/3rd of journal size, thresh2 is 2/3rd of
 * journal size.
 *
 * Returns: errno
 */

void gfs2_log_commit(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	log_refund(sdp, tr);

	if (atomic_read(&sdp->sd_log_pinned) > atomic_read(&sdp->sd_log_thresh1) ||
	    ((sdp->sd_jdesc->jd_blocks - atomic_read(&sdp->sd_log_blks_free)) >
	    atomic_read(&sdp->sd_log_thresh2)))
		wake_up(&sdp->sd_logd_waitq);
}

/**
 * gfs2_log_shutdown - write a shutdown header into a journal
 * @sdp: the filesystem
 *
 */

static void gfs2_log_shutdown(struct gfs2_sbd *sdp)
{
	gfs2_assert_withdraw(sdp, !sdp->sd_log_blks_reserved);
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_revoke);
	gfs2_assert_withdraw(sdp, list_empty(&sdp->sd_ail1_list));

	sdp->sd_log_flush_head = sdp->sd_log_head;

	log_write_header(sdp, GFS2_LOG_HEAD_UNMOUNT | GFS2_LFC_SHUTDOWN);

	gfs2_assert_warn(sdp, sdp->sd_log_head == sdp->sd_log_tail);
	gfs2_assert_warn(sdp, list_empty(&sdp->sd_ail2_list));

	sdp->sd_log_head = sdp->sd_log_flush_head;
	sdp->sd_log_tail = sdp->sd_log_head;
}

static inline int gfs2_jrnl_flush_reqd(struct gfs2_sbd *sdp)
{
	return (atomic_read(&sdp->sd_log_pinned) +
		atomic_read(&sdp->sd_log_blks_needed) >=
		atomic_read(&sdp->sd_log_thresh1));
}

static inline int gfs2_ail_flush_reqd(struct gfs2_sbd *sdp)
{
	unsigned int used_blocks = sdp->sd_jdesc->jd_blocks - atomic_read(&sdp->sd_log_blks_free);

	if (test_and_clear_bit(SDF_FORCE_AIL_FLUSH, &sdp->sd_flags))
		return 1;

	return used_blocks + atomic_read(&sdp->sd_log_blks_needed) >=
		atomic_read(&sdp->sd_log_thresh2);
}

/**
 * gfs2_logd - Update log tail as Active Items get flushed to in-place blocks
 * @sdp: Pointer to GFS2 superblock
 *
 * Also, periodically check to make sure that we're using the most recent
 * journal index.
 */

int gfs2_logd(void *data)
{
	struct gfs2_sbd *sdp = data;
	unsigned long t = 1;
	DEFINE_WAIT(wait);
	bool did_flush;

	while (!kthread_should_stop()) {

		/* Check for errors writing to the journal */
		if (sdp->sd_log_error) {
			gfs2_lm_withdraw(sdp,
					 "GFS2: fsid=%s: error %d: "
					 "withdrawing the file system to "
					 "prevent further damage.\n",
					 sdp->sd_fsname, sdp->sd_log_error);
		}

		did_flush = false;
		if (gfs2_jrnl_flush_reqd(sdp) || t == 0) {
			gfs2_ail1_empty(sdp);
			gfs2_log_flush(sdp, NULL, GFS2_LOG_HEAD_FLUSH_NORMAL |
				       GFS2_LFC_LOGD_JFLUSH_REQD);
			did_flush = true;
		}

		if (gfs2_ail_flush_reqd(sdp)) {
			gfs2_ail1_start(sdp);
			gfs2_ail1_wait(sdp);
			gfs2_ail1_empty(sdp);
			gfs2_log_flush(sdp, NULL, GFS2_LOG_HEAD_FLUSH_NORMAL |
				       GFS2_LFC_LOGD_AIL_FLUSH_REQD);
			did_flush = true;
		}

		if (!gfs2_ail_flush_reqd(sdp) || did_flush)
			wake_up(&sdp->sd_log_waitq);

		t = gfs2_tune_get(sdp, gt_logd_secs) * HZ;

		try_to_freeze();

		do {
			prepare_to_wait(&sdp->sd_logd_waitq, &wait,
					TASK_INTERRUPTIBLE);
			if (!gfs2_ail_flush_reqd(sdp) &&
			    !gfs2_jrnl_flush_reqd(sdp) &&
			    !kthread_should_stop())
				t = schedule_timeout(t);
		} while(t && !gfs2_ail_flush_reqd(sdp) &&
			!gfs2_jrnl_flush_reqd(sdp) &&
			!kthread_should_stop());
		finish_wait(&sdp->sd_logd_waitq, &wait);
	}

	return 0;
}

