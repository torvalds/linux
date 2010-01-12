/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2007 Red Hat, Inc.  All rights reserved.
 *
 * This copyrighted material is made available to anyone wishing to use,
 * modify, copy, or redistribute it subject to the terms and conditions
 * of the GNU General Public License version 2.
 */

#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/completion.h>
#include <linux/buffer_head.h>
#include <linux/gfs2_ondisk.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/freezer.h>
#include <linux/bio.h>

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

#define PULL 1

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
 * The log lock _must_ be held when calling this function
 *
 */

void gfs2_remove_from_ail(struct gfs2_bufdata *bd)
{
	bd->bd_ail = NULL;
	list_del_init(&bd->bd_ail_st_list);
	list_del_init(&bd->bd_ail_gl_list);
	atomic_dec(&bd->bd_gl->gl_ail_count);
	brelse(bd->bd_bh);
}

/**
 * gfs2_ail1_start_one - Start I/O on a part of the AIL
 * @sdp: the filesystem
 * @tr: the part of the AIL
 *
 */

static void gfs2_ail1_start_one(struct gfs2_sbd *sdp, struct gfs2_ail *ai)
__releases(&sdp->sd_log_lock)
__acquires(&sdp->sd_log_lock)
{
	struct gfs2_bufdata *bd, *s;
	struct buffer_head *bh;
	int retry;

	do {
		retry = 0;

		list_for_each_entry_safe_reverse(bd, s, &ai->ai_ail1_list,
						 bd_ail_st_list) {
			bh = bd->bd_bh;

			gfs2_assert(sdp, bd->bd_ail == ai);

			if (!buffer_busy(bh)) {
				if (!buffer_uptodate(bh))
					gfs2_io_error_bh(sdp, bh);
				list_move(&bd->bd_ail_st_list, &ai->ai_ail2_list);
				continue;
			}

			if (!buffer_dirty(bh))
				continue;

			list_move(&bd->bd_ail_st_list, &ai->ai_ail1_list);

			get_bh(bh);
			gfs2_log_unlock(sdp);
			lock_buffer(bh);
			if (test_clear_buffer_dirty(bh)) {
				bh->b_end_io = end_buffer_write_sync;
				submit_bh(WRITE_SYNC_PLUG, bh);
			} else {
				unlock_buffer(bh);
				brelse(bh);
			}
			gfs2_log_lock(sdp);

			retry = 1;
			break;
		}
	} while (retry);
}

/**
 * gfs2_ail1_empty_one - Check whether or not a trans in the AIL has been synced
 * @sdp: the filesystem
 * @ai: the AIL entry
 *
 */

static int gfs2_ail1_empty_one(struct gfs2_sbd *sdp, struct gfs2_ail *ai, int flags)
{
	struct gfs2_bufdata *bd, *s;
	struct buffer_head *bh;

	list_for_each_entry_safe_reverse(bd, s, &ai->ai_ail1_list,
					 bd_ail_st_list) {
		bh = bd->bd_bh;

		gfs2_assert(sdp, bd->bd_ail == ai);

		if (buffer_busy(bh)) {
			if (flags & DIO_ALL)
				continue;
			else
				break;
		}

		if (!buffer_uptodate(bh))
			gfs2_io_error_bh(sdp, bh);

		list_move(&bd->bd_ail_st_list, &ai->ai_ail2_list);
	}

	return list_empty(&ai->ai_ail1_list);
}

static void gfs2_ail1_start(struct gfs2_sbd *sdp, int flags)
{
	struct list_head *head;
	u64 sync_gen;
	struct list_head *first;
	struct gfs2_ail *first_ai, *ai, *tmp;
	int done = 0;

	gfs2_log_lock(sdp);
	head = &sdp->sd_ail1_list;
	if (list_empty(head)) {
		gfs2_log_unlock(sdp);
		return;
	}
	sync_gen = sdp->sd_ail_sync_gen++;

	first = head->prev;
	first_ai = list_entry(first, struct gfs2_ail, ai_list);
	first_ai->ai_sync_gen = sync_gen;
	gfs2_ail1_start_one(sdp, first_ai); /* This may drop log lock */

	if (flags & DIO_ALL)
		first = NULL;

	while(!done) {
		if (first && (head->prev != first ||
			      gfs2_ail1_empty_one(sdp, first_ai, 0)))
			break;

		done = 1;
		list_for_each_entry_safe_reverse(ai, tmp, head, ai_list) {
			if (ai->ai_sync_gen >= sync_gen)
				continue;
			ai->ai_sync_gen = sync_gen;
			gfs2_ail1_start_one(sdp, ai); /* This may drop log lock */
			done = 0;
			break;
		}
	}

	gfs2_log_unlock(sdp);
}

static int gfs2_ail1_empty(struct gfs2_sbd *sdp, int flags)
{
	struct gfs2_ail *ai, *s;
	int ret;

	gfs2_log_lock(sdp);

	list_for_each_entry_safe_reverse(ai, s, &sdp->sd_ail1_list, ai_list) {
		if (gfs2_ail1_empty_one(sdp, ai, flags))
			list_move(&ai->ai_list, &sdp->sd_ail2_list);
		else if (!(flags & DIO_ALL))
			break;
	}

	ret = list_empty(&sdp->sd_ail1_list);

	gfs2_log_unlock(sdp);

	return ret;
}


/**
 * gfs2_ail2_empty_one - Check whether or not a trans in the AIL has been synced
 * @sdp: the filesystem
 * @ai: the AIL entry
 *
 */

static void gfs2_ail2_empty_one(struct gfs2_sbd *sdp, struct gfs2_ail *ai)
{
	struct list_head *head = &ai->ai_ail2_list;
	struct gfs2_bufdata *bd;

	while (!list_empty(head)) {
		bd = list_entry(head->prev, struct gfs2_bufdata,
				bd_ail_st_list);
		gfs2_assert(sdp, bd->bd_ail == ai);
		gfs2_remove_from_ail(bd);
	}
}

static void ail2_empty(struct gfs2_sbd *sdp, unsigned int new_tail)
{
	struct gfs2_ail *ai, *safe;
	unsigned int old_tail = sdp->sd_log_tail;
	int wrap = (new_tail < old_tail);
	int a, b, rm;

	gfs2_log_lock(sdp);

	list_for_each_entry_safe(ai, safe, &sdp->sd_ail2_list, ai_list) {
		a = (old_tail <= ai->ai_first);
		b = (ai->ai_first < new_tail);
		rm = (wrap) ? (a || b) : (a && b);
		if (!rm)
			continue;

		gfs2_ail2_empty_one(sdp, ai);
		list_del(&ai->ai_list);
		gfs2_assert_warn(sdp, list_empty(&ai->ai_ail1_list));
		gfs2_assert_warn(sdp, list_empty(&ai->ai_ail2_list));
		kfree(ai);
	}

	gfs2_log_unlock(sdp);
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
 * Returns: errno
 */

int gfs2_log_reserve(struct gfs2_sbd *sdp, unsigned int blks)
{
	unsigned int try = 0;
	unsigned reserved_blks = 6 * (4096 / sdp->sd_vfs->s_blocksize);

	if (gfs2_assert_warn(sdp, blks) ||
	    gfs2_assert_warn(sdp, blks <= sdp->sd_jdesc->jd_blocks))
		return -EINVAL;

	mutex_lock(&sdp->sd_log_reserve_mutex);
	gfs2_log_lock(sdp);
	while(atomic_read(&sdp->sd_log_blks_free) <= (blks + reserved_blks)) {
		gfs2_log_unlock(sdp);
		gfs2_ail1_empty(sdp, 0);
		gfs2_log_flush(sdp, NULL);

		if (try++)
			gfs2_ail1_start(sdp, 0);
		gfs2_log_lock(sdp);
	}
	atomic_sub(blks, &sdp->sd_log_blks_free);
	trace_gfs2_log_blocks(sdp, -blks);
	gfs2_log_unlock(sdp);
	mutex_unlock(&sdp->sd_log_reserve_mutex);

	down_read(&sdp->sd_log_flush_lock);

	return 0;
}

/**
 * gfs2_log_release - Release a given number of log blocks
 * @sdp: The GFS2 superblock
 * @blks: The number of blocks
 *
 */

void gfs2_log_release(struct gfs2_sbd *sdp, unsigned int blks)
{

	gfs2_log_lock(sdp);
	atomic_add(blks, &sdp->sd_log_blks_free);
	trace_gfs2_log_blocks(sdp, blks);
	gfs2_assert_withdraw(sdp,
			     atomic_read(&sdp->sd_log_blks_free) <= sdp->sd_jdesc->jd_blocks);
	gfs2_log_unlock(sdp);
	up_read(&sdp->sd_log_flush_lock);
}

static u64 log_bmap(struct gfs2_sbd *sdp, unsigned int lbn)
{
	struct gfs2_journal_extent *je;

	list_for_each_entry(je, &sdp->sd_jdesc->extent_list, extent_list) {
		if (lbn >= je->lblock && lbn < je->lblock + je->blocks)
			return je->dblock + lbn - je->lblock;
	}

	return -1;
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
	unsigned int mbuf_limit, metabufhdrs_needed;
	unsigned int dbuf_limit, databufhdrs_needed;
	unsigned int revokes = 0;

	mbuf_limit = buf_limit(sdp);
	metabufhdrs_needed = (sdp->sd_log_commited_buf +
			      (mbuf_limit - 1)) / mbuf_limit;
	dbuf_limit = databuf_limit(sdp);
	databufhdrs_needed = (sdp->sd_log_commited_databuf +
			      (dbuf_limit - 1)) / dbuf_limit;

	if (sdp->sd_log_commited_revoke)
		revokes = gfs2_struct2blk(sdp, sdp->sd_log_commited_revoke,
					  sizeof(u64));

	reserved = sdp->sd_log_commited_buf + metabufhdrs_needed +
		sdp->sd_log_commited_databuf + databufhdrs_needed +
		revokes;
	/* One for the overall header */
	if (reserved)
		reserved++;
	return reserved;
}

static unsigned int current_tail(struct gfs2_sbd *sdp)
{
	struct gfs2_ail *ai;
	unsigned int tail;

	gfs2_log_lock(sdp);

	if (list_empty(&sdp->sd_ail1_list)) {
		tail = sdp->sd_log_head;
	} else {
		ai = list_entry(sdp->sd_ail1_list.prev, struct gfs2_ail, ai_list);
		tail = ai->ai_first;
	}

	gfs2_log_unlock(sdp);

	return tail;
}

void gfs2_log_incr_head(struct gfs2_sbd *sdp)
{
	if (sdp->sd_log_flush_head == sdp->sd_log_tail)
		BUG_ON(sdp->sd_log_flush_head != sdp->sd_log_head);

	if (++sdp->sd_log_flush_head == sdp->sd_jdesc->jd_blocks) {
		sdp->sd_log_flush_head = 0;
		sdp->sd_log_flush_wrapped = 1;
	}
}

/**
 * gfs2_log_write_endio - End of I/O for a log buffer
 * @bh: The buffer head
 * @uptodate: I/O Status
 *
 */

static void gfs2_log_write_endio(struct buffer_head *bh, int uptodate)
{
	struct gfs2_sbd *sdp = bh->b_private;
	bh->b_private = NULL;

	end_buffer_write_sync(bh, uptodate);
	if (atomic_dec_and_test(&sdp->sd_log_in_flight))
		wake_up(&sdp->sd_log_flush_wait);
}

/**
 * gfs2_log_get_buf - Get and initialize a buffer to use for log control data
 * @sdp: The GFS2 superblock
 *
 * Returns: the buffer_head
 */

struct buffer_head *gfs2_log_get_buf(struct gfs2_sbd *sdp)
{
	u64 blkno = log_bmap(sdp, sdp->sd_log_flush_head);
	struct buffer_head *bh;

	bh = sb_getblk(sdp->sd_vfs, blkno);
	lock_buffer(bh);
	memset(bh->b_data, 0, bh->b_size);
	set_buffer_uptodate(bh);
	clear_buffer_dirty(bh);
	gfs2_log_incr_head(sdp);
	atomic_inc(&sdp->sd_log_in_flight);
	bh->b_private = sdp;
	bh->b_end_io = gfs2_log_write_endio;

	return bh;
}

/**
 * gfs2_fake_write_endio - 
 * @bh: The buffer head
 * @uptodate: The I/O Status
 *
 */

static void gfs2_fake_write_endio(struct buffer_head *bh, int uptodate)
{
	struct buffer_head *real_bh = bh->b_private;
	struct gfs2_bufdata *bd = real_bh->b_private;
	struct gfs2_sbd *sdp = bd->bd_gl->gl_sbd;

	end_buffer_write_sync(bh, uptodate);
	free_buffer_head(bh);
	unlock_buffer(real_bh);
	brelse(real_bh);
	if (atomic_dec_and_test(&sdp->sd_log_in_flight))
		wake_up(&sdp->sd_log_flush_wait);
}

/**
 * gfs2_log_fake_buf - Build a fake buffer head to write metadata buffer to log
 * @sdp: the filesystem
 * @data: the data the buffer_head should point to
 *
 * Returns: the log buffer descriptor
 */

struct buffer_head *gfs2_log_fake_buf(struct gfs2_sbd *sdp,
				      struct buffer_head *real)
{
	u64 blkno = log_bmap(sdp, sdp->sd_log_flush_head);
	struct buffer_head *bh;

	bh = alloc_buffer_head(GFP_NOFS | __GFP_NOFAIL);
	atomic_set(&bh->b_count, 1);
	bh->b_state = (1 << BH_Mapped) | (1 << BH_Uptodate) | (1 << BH_Lock);
	set_bh_page(bh, real->b_page, bh_offset(real));
	bh->b_blocknr = blkno;
	bh->b_size = sdp->sd_sb.sb_bsize;
	bh->b_bdev = sdp->sd_vfs->s_bdev;
	bh->b_private = real;
	bh->b_end_io = gfs2_fake_write_endio;

	gfs2_log_incr_head(sdp);
	atomic_inc(&sdp->sd_log_in_flight);

	return bh;
}

static void log_pull_tail(struct gfs2_sbd *sdp, unsigned int new_tail)
{
	unsigned int dist = log_distance(sdp, new_tail, sdp->sd_log_tail);

	ail2_empty(sdp, new_tail);

	gfs2_log_lock(sdp);
	atomic_add(dist, &sdp->sd_log_blks_free);
	trace_gfs2_log_blocks(sdp, dist);
	gfs2_assert_withdraw(sdp, atomic_read(&sdp->sd_log_blks_free) <= sdp->sd_jdesc->jd_blocks);
	gfs2_log_unlock(sdp);

	sdp->sd_log_tail = new_tail;
}

/**
 * log_write_header - Get and initialize a journal header buffer
 * @sdp: The GFS2 superblock
 *
 * Returns: the initialized log buffer descriptor
 */

static void log_write_header(struct gfs2_sbd *sdp, u32 flags, int pull)
{
	u64 blkno = log_bmap(sdp, sdp->sd_log_flush_head);
	struct buffer_head *bh;
	struct gfs2_log_header *lh;
	unsigned int tail;
	u32 hash;

	bh = sb_getblk(sdp->sd_vfs, blkno);
	lock_buffer(bh);
	memset(bh->b_data, 0, bh->b_size);
	set_buffer_uptodate(bh);
	clear_buffer_dirty(bh);

	gfs2_ail1_empty(sdp, 0);
	tail = current_tail(sdp);

	lh = (struct gfs2_log_header *)bh->b_data;
	memset(lh, 0, sizeof(struct gfs2_log_header));
	lh->lh_header.mh_magic = cpu_to_be32(GFS2_MAGIC);
	lh->lh_header.mh_type = cpu_to_be32(GFS2_METATYPE_LH);
	lh->lh_header.__pad0 = cpu_to_be64(0);
	lh->lh_header.mh_format = cpu_to_be32(GFS2_FORMAT_LH);
	lh->lh_header.mh_jid = cpu_to_be32(sdp->sd_jdesc->jd_jid);
	lh->lh_sequence = cpu_to_be64(sdp->sd_log_sequence++);
	lh->lh_flags = cpu_to_be32(flags);
	lh->lh_tail = cpu_to_be32(tail);
	lh->lh_blkno = cpu_to_be32(sdp->sd_log_flush_head);
	hash = gfs2_disk_hash(bh->b_data, sizeof(struct gfs2_log_header));
	lh->lh_hash = cpu_to_be32(hash);

	bh->b_end_io = end_buffer_write_sync;
	if (test_bit(SDF_NOBARRIERS, &sdp->sd_flags))
		goto skip_barrier;
	get_bh(bh);
	submit_bh(WRITE_SYNC | (1 << BIO_RW_BARRIER) | (1 << BIO_RW_META), bh);
	wait_on_buffer(bh);
	if (buffer_eopnotsupp(bh)) {
		clear_buffer_eopnotsupp(bh);
		set_buffer_uptodate(bh);
		set_bit(SDF_NOBARRIERS, &sdp->sd_flags);
		lock_buffer(bh);
skip_barrier:
		get_bh(bh);
		submit_bh(WRITE_SYNC | (1 << BIO_RW_META), bh);
		wait_on_buffer(bh);
	}
	if (!buffer_uptodate(bh))
		gfs2_io_error_bh(sdp, bh);
	brelse(bh);

	if (sdp->sd_log_tail != tail)
		log_pull_tail(sdp, tail);
	else
		gfs2_assert_withdraw(sdp, !pull);

	sdp->sd_log_idle = (tail == sdp->sd_log_flush_head);
	gfs2_log_incr_head(sdp);
}

static void log_flush_commit(struct gfs2_sbd *sdp)
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

	log_write_header(sdp, 0, 0);
}

static void gfs2_ordered_write(struct gfs2_sbd *sdp)
{
	struct gfs2_bufdata *bd;
	struct buffer_head *bh;
	LIST_HEAD(written);

	gfs2_log_lock(sdp);
	while (!list_empty(&sdp->sd_log_le_ordered)) {
		bd = list_entry(sdp->sd_log_le_ordered.next, struct gfs2_bufdata, bd_le.le_list);
		list_move(&bd->bd_le.le_list, &written);
		bh = bd->bd_bh;
		if (!buffer_dirty(bh))
			continue;
		get_bh(bh);
		gfs2_log_unlock(sdp);
		lock_buffer(bh);
		if (buffer_mapped(bh) && test_clear_buffer_dirty(bh)) {
			bh->b_end_io = end_buffer_write_sync;
			submit_bh(WRITE_SYNC_PLUG, bh);
		} else {
			unlock_buffer(bh);
			brelse(bh);
		}
		gfs2_log_lock(sdp);
	}
	list_splice(&written, &sdp->sd_log_le_ordered);
	gfs2_log_unlock(sdp);
}

static void gfs2_ordered_wait(struct gfs2_sbd *sdp)
{
	struct gfs2_bufdata *bd;
	struct buffer_head *bh;

	gfs2_log_lock(sdp);
	while (!list_empty(&sdp->sd_log_le_ordered)) {
		bd = list_entry(sdp->sd_log_le_ordered.prev, struct gfs2_bufdata, bd_le.le_list);
		bh = bd->bd_bh;
		if (buffer_locked(bh)) {
			get_bh(bh);
			gfs2_log_unlock(sdp);
			wait_on_buffer(bh);
			brelse(bh);
			gfs2_log_lock(sdp);
			continue;
		}
		list_del_init(&bd->bd_le.le_list);
	}
	gfs2_log_unlock(sdp);
}

/**
 * gfs2_log_flush - flush incore transaction(s)
 * @sdp: the filesystem
 * @gl: The glock structure to flush.  If NULL, flush the whole incore log
 *
 */

void __gfs2_log_flush(struct gfs2_sbd *sdp, struct gfs2_glock *gl)
{
	struct gfs2_ail *ai;

	down_write(&sdp->sd_log_flush_lock);

	/* Log might have been flushed while we waited for the flush lock */
	if (gl && !test_bit(GLF_LFLUSH, &gl->gl_flags)) {
		up_write(&sdp->sd_log_flush_lock);
		return;
	}
	trace_gfs2_log_flush(sdp, 1);

	ai = kzalloc(sizeof(struct gfs2_ail), GFP_NOFS | __GFP_NOFAIL);
	INIT_LIST_HEAD(&ai->ai_ail1_list);
	INIT_LIST_HEAD(&ai->ai_ail2_list);

	if (sdp->sd_log_num_buf != sdp->sd_log_commited_buf) {
		printk(KERN_INFO "GFS2: log buf %u %u\n", sdp->sd_log_num_buf,
		       sdp->sd_log_commited_buf);
		gfs2_assert_withdraw(sdp, 0);
	}
	if (sdp->sd_log_num_databuf != sdp->sd_log_commited_databuf) {
		printk(KERN_INFO "GFS2: log databuf %u %u\n",
		       sdp->sd_log_num_databuf, sdp->sd_log_commited_databuf);
		gfs2_assert_withdraw(sdp, 0);
	}
	gfs2_assert_withdraw(sdp,
			sdp->sd_log_num_revoke == sdp->sd_log_commited_revoke);

	sdp->sd_log_flush_head = sdp->sd_log_head;
	sdp->sd_log_flush_wrapped = 0;
	ai->ai_first = sdp->sd_log_flush_head;

	gfs2_ordered_write(sdp);
	lops_before_commit(sdp);
	gfs2_ordered_wait(sdp);

	if (sdp->sd_log_head != sdp->sd_log_flush_head)
		log_flush_commit(sdp);
	else if (sdp->sd_log_tail != current_tail(sdp) && !sdp->sd_log_idle){
		gfs2_log_lock(sdp);
		atomic_dec(&sdp->sd_log_blks_free); /* Adjust for unreserved buffer */
		trace_gfs2_log_blocks(sdp, -1);
		gfs2_log_unlock(sdp);
		log_write_header(sdp, 0, PULL);
	}
	lops_after_commit(sdp, ai);

	gfs2_log_lock(sdp);
	sdp->sd_log_head = sdp->sd_log_flush_head;
	sdp->sd_log_blks_reserved = 0;
	sdp->sd_log_commited_buf = 0;
	sdp->sd_log_commited_databuf = 0;
	sdp->sd_log_commited_revoke = 0;

	if (!list_empty(&ai->ai_ail1_list)) {
		list_add(&ai->ai_list, &sdp->sd_ail1_list);
		ai = NULL;
	}
	gfs2_log_unlock(sdp);
	trace_gfs2_log_flush(sdp, 0);
	up_write(&sdp->sd_log_flush_lock);

	kfree(ai);
}

static void log_refund(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	unsigned int reserved;
	unsigned int unused;

	gfs2_log_lock(sdp);

	sdp->sd_log_commited_buf += tr->tr_num_buf_new - tr->tr_num_buf_rm;
	sdp->sd_log_commited_databuf += tr->tr_num_databuf_new -
		tr->tr_num_databuf_rm;
	gfs2_assert_withdraw(sdp, (((int)sdp->sd_log_commited_buf) >= 0) ||
			     (((int)sdp->sd_log_commited_databuf) >= 0));
	sdp->sd_log_commited_revoke += tr->tr_num_revoke - tr->tr_num_revoke_rm;
	gfs2_assert_withdraw(sdp, ((int)sdp->sd_log_commited_revoke) >= 0);
	reserved = calc_reserved(sdp);
	gfs2_assert_withdraw(sdp, sdp->sd_log_blks_reserved + tr->tr_reserved >= reserved);
	unused = sdp->sd_log_blks_reserved - reserved + tr->tr_reserved;
	atomic_add(unused, &sdp->sd_log_blks_free);
	trace_gfs2_log_blocks(sdp, unused);
	gfs2_assert_withdraw(sdp, atomic_read(&sdp->sd_log_blks_free) <=
			     sdp->sd_jdesc->jd_blocks);
	sdp->sd_log_blks_reserved = reserved;

	gfs2_log_unlock(sdp);
}

static void buf_lo_incore_commit(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	struct list_head *head = &tr->tr_list_buf;
	struct gfs2_bufdata *bd;

	gfs2_log_lock(sdp);
	while (!list_empty(head)) {
		bd = list_entry(head->next, struct gfs2_bufdata, bd_list_tr);
		list_del_init(&bd->bd_list_tr);
		tr->tr_num_buf--;
	}
	gfs2_log_unlock(sdp);
	gfs2_assert_warn(sdp, !tr->tr_num_buf);
}

/**
 * gfs2_log_commit - Commit a transaction to the log
 * @sdp: the filesystem
 * @tr: the transaction
 *
 * Returns: errno
 */

void gfs2_log_commit(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	log_refund(sdp, tr);
	buf_lo_incore_commit(sdp, tr);

	up_read(&sdp->sd_log_flush_lock);

	gfs2_log_lock(sdp);
	if (sdp->sd_log_num_buf > gfs2_tune_get(sdp, gt_incore_log_blocks))
		wake_up_process(sdp->sd_logd_process);
	gfs2_log_unlock(sdp);
}

/**
 * gfs2_log_shutdown - write a shutdown header into a journal
 * @sdp: the filesystem
 *
 */

void gfs2_log_shutdown(struct gfs2_sbd *sdp)
{
	down_write(&sdp->sd_log_flush_lock);

	gfs2_assert_withdraw(sdp, !sdp->sd_log_blks_reserved);
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_buf);
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_revoke);
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_rg);
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_databuf);
	gfs2_assert_withdraw(sdp, list_empty(&sdp->sd_ail1_list));

	sdp->sd_log_flush_head = sdp->sd_log_head;
	sdp->sd_log_flush_wrapped = 0;

	log_write_header(sdp, GFS2_LOG_HEAD_UNMOUNT,
			 (sdp->sd_log_tail == current_tail(sdp)) ? 0 : PULL);

	gfs2_assert_warn(sdp, atomic_read(&sdp->sd_log_blks_free) == sdp->sd_jdesc->jd_blocks);
	gfs2_assert_warn(sdp, sdp->sd_log_head == sdp->sd_log_tail);
	gfs2_assert_warn(sdp, list_empty(&sdp->sd_ail2_list));

	sdp->sd_log_head = sdp->sd_log_flush_head;
	sdp->sd_log_tail = sdp->sd_log_head;

	up_write(&sdp->sd_log_flush_lock);
}


/**
 * gfs2_meta_syncfs - sync all the buffers in a filesystem
 * @sdp: the filesystem
 *
 */

void gfs2_meta_syncfs(struct gfs2_sbd *sdp)
{
	gfs2_log_flush(sdp, NULL);
	for (;;) {
		gfs2_ail1_start(sdp, DIO_ALL);
		if (gfs2_ail1_empty(sdp, DIO_ALL))
			break;
		msleep(10);
	}
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
	unsigned long t;
	int need_flush;

	while (!kthread_should_stop()) {
		/* Advance the log tail */

		t = sdp->sd_log_flush_time +
		    gfs2_tune_get(sdp, gt_log_flush_secs) * HZ;

		gfs2_ail1_empty(sdp, DIO_ALL);
		gfs2_log_lock(sdp);
		need_flush = sdp->sd_log_num_buf > gfs2_tune_get(sdp, gt_incore_log_blocks);
		gfs2_log_unlock(sdp);
		if (need_flush || time_after_eq(jiffies, t)) {
			gfs2_log_flush(sdp, NULL);
			sdp->sd_log_flush_time = jiffies;
		}

		t = gfs2_tune_get(sdp, gt_logd_secs) * HZ;
		if (freezing(current))
			refrigerator();
		schedule_timeout_interruptible(t);
	}

	return 0;
}

