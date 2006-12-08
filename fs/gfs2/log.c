/*
 * Copyright (C) Sistina Software, Inc.  1997-2003 All rights reserved.
 * Copyright (C) 2004-2006 Red Hat, Inc.  All rights reserved.
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
#include <linux/lm_interface.h>
#include <linux/delay.h>

#include "gfs2.h"
#include "incore.h"
#include "bmap.h"
#include "glock.h"
#include "log.h"
#include "lops.h"
#include "meta_io.h"
#include "util.h"
#include "dir.h"

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
 * gfs2_ail1_start_one - Start I/O on a part of the AIL
 * @sdp: the filesystem
 * @tr: the part of the AIL
 *
 */

static void gfs2_ail1_start_one(struct gfs2_sbd *sdp, struct gfs2_ail *ai)
{
	struct gfs2_bufdata *bd, *s;
	struct buffer_head *bh;
	int retry;

	BUG_ON(!spin_is_locked(&sdp->sd_log_lock));

	do {
		retry = 0;

		list_for_each_entry_safe_reverse(bd, s, &ai->ai_ail1_list,
						 bd_ail_st_list) {
			bh = bd->bd_bh;

			gfs2_assert(sdp, bd->bd_ail == ai);

			if (!buffer_busy(bh)) {
				if (!buffer_uptodate(bh)) {
					gfs2_log_unlock(sdp);
					gfs2_io_error_bh(sdp, bh);
					gfs2_log_lock(sdp);
				}
				list_move(&bd->bd_ail_st_list, &ai->ai_ail2_list);
				continue;
			}

			if (!buffer_dirty(bh))
				continue;

			list_move(&bd->bd_ail_st_list, &ai->ai_ail1_list);

			gfs2_log_unlock(sdp);
			wait_on_buffer(bh);
			ll_rw_block(WRITE, 1, &bh);
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
	struct list_head *head = &sdp->sd_ail1_list;
	u64 sync_gen;
	struct list_head *first;
	struct gfs2_ail *first_ai, *ai, *tmp;
	int done = 0;

	gfs2_log_lock(sdp);
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

int gfs2_ail1_empty(struct gfs2_sbd *sdp, int flags)
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
		bd->bd_ail = NULL;
		list_del(&bd->bd_ail_st_list);
		list_del(&bd->bd_ail_gl_list);
		atomic_dec(&bd->bd_gl->gl_ail_count);
		brelse(bd->bd_bh);
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
 * Note that we never give out the last 6 blocks of the journal. Thats
 * due to the fact that there is are a small number of header blocks
 * associated with each log flush. The exact number can't be known until
 * flush time, so we ensure that we have just enough free blocks at all
 * times to avoid running out during a log flush.
 *
 * Returns: errno
 */

int gfs2_log_reserve(struct gfs2_sbd *sdp, unsigned int blks)
{
	unsigned int try = 0;

	if (gfs2_assert_warn(sdp, blks) ||
	    gfs2_assert_warn(sdp, blks <= sdp->sd_jdesc->jd_blocks))
		return -EINVAL;

	mutex_lock(&sdp->sd_log_reserve_mutex);
	gfs2_log_lock(sdp);
	while(sdp->sd_log_blks_free <= (blks + 6)) {
		gfs2_log_unlock(sdp);
		gfs2_ail1_empty(sdp, 0);
		gfs2_log_flush(sdp, NULL);

		if (try++)
			gfs2_ail1_start(sdp, 0);
		gfs2_log_lock(sdp);
	}
	sdp->sd_log_blks_free -= blks;
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
	sdp->sd_log_blks_free += blks;
	gfs2_assert_withdraw(sdp,
			     sdp->sd_log_blks_free <= sdp->sd_jdesc->jd_blocks);
	gfs2_log_unlock(sdp);
	up_read(&sdp->sd_log_flush_lock);
}

static u64 log_bmap(struct gfs2_sbd *sdp, unsigned int lbn)
{
	struct inode *inode = sdp->sd_jdesc->jd_inode;
	int error;
	struct buffer_head bh_map = { .b_state = 0, .b_blocknr = 0 };

	bh_map.b_size = 1 << inode->i_blkbits;
	error = gfs2_block_map(inode, lbn, 0, &bh_map);
	if (error || !bh_map.b_blocknr)
		printk(KERN_INFO "error=%d, dbn=%llu lbn=%u", error,
		       (unsigned long long)bh_map.b_blocknr, lbn);
	gfs2_assert_withdraw(sdp, !error && bh_map.b_blocknr);

	return bh_map.b_blocknr;
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

static inline void log_incr_head(struct gfs2_sbd *sdp)
{
	if (sdp->sd_log_flush_head == sdp->sd_log_tail)
		gfs2_assert_withdraw(sdp, sdp->sd_log_flush_head == sdp->sd_log_head);

	if (++sdp->sd_log_flush_head == sdp->sd_jdesc->jd_blocks) {
		sdp->sd_log_flush_head = 0;
		sdp->sd_log_flush_wrapped = 1;
	}
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
	struct gfs2_log_buf *lb;
	struct buffer_head *bh;

	lb = kzalloc(sizeof(struct gfs2_log_buf), GFP_NOFS | __GFP_NOFAIL);
	list_add(&lb->lb_list, &sdp->sd_log_flush_list);

	bh = lb->lb_bh = sb_getblk(sdp->sd_vfs, blkno);
	lock_buffer(bh);
	memset(bh->b_data, 0, bh->b_size);
	set_buffer_uptodate(bh);
	clear_buffer_dirty(bh);
	unlock_buffer(bh);

	log_incr_head(sdp);

	return bh;
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
	struct gfs2_log_buf *lb;
	struct buffer_head *bh;

	lb = kzalloc(sizeof(struct gfs2_log_buf), GFP_NOFS | __GFP_NOFAIL);
	list_add(&lb->lb_list, &sdp->sd_log_flush_list);
	lb->lb_real = real;

	bh = lb->lb_bh = alloc_buffer_head(GFP_NOFS | __GFP_NOFAIL);
	atomic_set(&bh->b_count, 1);
	bh->b_state = (1 << BH_Mapped) | (1 << BH_Uptodate);
	set_bh_page(bh, real->b_page, bh_offset(real));
	bh->b_blocknr = blkno;
	bh->b_size = sdp->sd_sb.sb_bsize;
	bh->b_bdev = sdp->sd_vfs->s_bdev;

	log_incr_head(sdp);

	return bh;
}

static void log_pull_tail(struct gfs2_sbd *sdp, unsigned int new_tail, int pull)
{
	unsigned int dist = log_distance(sdp, new_tail, sdp->sd_log_tail);

	ail2_empty(sdp, new_tail);

	gfs2_log_lock(sdp);
	sdp->sd_log_blks_free += dist - (pull ? 1 : 0);
	gfs2_assert_withdraw(sdp, sdp->sd_log_blks_free <= sdp->sd_jdesc->jd_blocks);
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
	unlock_buffer(bh);

	gfs2_ail1_empty(sdp, 0);
	tail = current_tail(sdp);

	lh = (struct gfs2_log_header *)bh->b_data;
	memset(lh, 0, sizeof(struct gfs2_log_header));
	lh->lh_header.mh_magic = cpu_to_be32(GFS2_MAGIC);
	lh->lh_header.mh_type = cpu_to_be32(GFS2_METATYPE_LH);
	lh->lh_header.mh_format = cpu_to_be32(GFS2_FORMAT_LH);
	lh->lh_sequence = cpu_to_be64(sdp->sd_log_sequence++);
	lh->lh_flags = cpu_to_be32(flags);
	lh->lh_tail = cpu_to_be32(tail);
	lh->lh_blkno = cpu_to_be32(sdp->sd_log_flush_head);
	hash = gfs2_disk_hash(bh->b_data, sizeof(struct gfs2_log_header));
	lh->lh_hash = cpu_to_be32(hash);

	set_buffer_dirty(bh);
	if (sync_dirty_buffer(bh))
		gfs2_io_error_bh(sdp, bh);
	brelse(bh);

	if (sdp->sd_log_tail != tail)
		log_pull_tail(sdp, tail, pull);
	else
		gfs2_assert_withdraw(sdp, !pull);

	sdp->sd_log_idle = (tail == sdp->sd_log_flush_head);
	log_incr_head(sdp);
}

static void log_flush_commit(struct gfs2_sbd *sdp)
{
	struct list_head *head = &sdp->sd_log_flush_list;
	struct gfs2_log_buf *lb;
	struct buffer_head *bh;

	while (!list_empty(head)) {
		lb = list_entry(head->next, struct gfs2_log_buf, lb_list);
		list_del(&lb->lb_list);
		bh = lb->lb_bh;

		wait_on_buffer(bh);
		if (!buffer_uptodate(bh))
			gfs2_io_error_bh(sdp, bh);
		if (lb->lb_real) {
			while (atomic_read(&bh->b_count) != 1)  /* Grrrr... */
				schedule();
			free_buffer_head(bh);
		} else
			brelse(bh);
		kfree(lb);
	}

	log_write_header(sdp, 0, 0);
}

/**
 * gfs2_log_flush - flush incore transaction(s)
 * @sdp: the filesystem
 * @gl: The glock structure to flush.  If NULL, flush the whole incore log
 *
 */

void gfs2_log_flush(struct gfs2_sbd *sdp, struct gfs2_glock *gl)
{
	struct gfs2_ail *ai;

	down_write(&sdp->sd_log_flush_lock);

	if (gl) {
		gfs2_log_lock(sdp);
		if (list_empty(&gl->gl_le.le_list)) {
			gfs2_log_unlock(sdp);
			up_write(&sdp->sd_log_flush_lock);
			return;
		}
		gfs2_log_unlock(sdp);
	}

	ai = kzalloc(sizeof(struct gfs2_ail), GFP_NOFS | __GFP_NOFAIL);
	INIT_LIST_HEAD(&ai->ai_ail1_list);
	INIT_LIST_HEAD(&ai->ai_ail2_list);

	gfs2_assert_withdraw(sdp, sdp->sd_log_num_buf == sdp->sd_log_commited_buf);
	gfs2_assert_withdraw(sdp,
			sdp->sd_log_num_revoke == sdp->sd_log_commited_revoke);

	sdp->sd_log_flush_head = sdp->sd_log_head;
	sdp->sd_log_flush_wrapped = 0;
	ai->ai_first = sdp->sd_log_flush_head;

	lops_before_commit(sdp);
	if (!list_empty(&sdp->sd_log_flush_list))
		log_flush_commit(sdp);
	else if (sdp->sd_log_tail != current_tail(sdp) && !sdp->sd_log_idle)
		log_write_header(sdp, 0, PULL);
	lops_after_commit(sdp, ai);

	gfs2_log_lock(sdp);
	sdp->sd_log_head = sdp->sd_log_flush_head;
	sdp->sd_log_blks_free -= sdp->sd_log_num_hdrs;
	sdp->sd_log_blks_reserved = 0;
	sdp->sd_log_commited_buf = 0;
	sdp->sd_log_num_hdrs = 0;
	sdp->sd_log_commited_revoke = 0;

	if (!list_empty(&ai->ai_ail1_list)) {
		list_add(&ai->ai_list, &sdp->sd_ail1_list);
		ai = NULL;
	}
	gfs2_log_unlock(sdp);

	sdp->sd_vfs->s_dirt = 0;
	up_write(&sdp->sd_log_flush_lock);

	kfree(ai);
}

static void log_refund(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	unsigned int reserved = 0;
	unsigned int old;

	gfs2_log_lock(sdp);

	sdp->sd_log_commited_buf += tr->tr_num_buf_new - tr->tr_num_buf_rm;
	gfs2_assert_withdraw(sdp, ((int)sdp->sd_log_commited_buf) >= 0);
	sdp->sd_log_commited_revoke += tr->tr_num_revoke - tr->tr_num_revoke_rm;
	gfs2_assert_withdraw(sdp, ((int)sdp->sd_log_commited_revoke) >= 0);

	if (sdp->sd_log_commited_buf)
		reserved += sdp->sd_log_commited_buf;
	if (sdp->sd_log_commited_revoke)
		reserved += gfs2_struct2blk(sdp, sdp->sd_log_commited_revoke,
					    sizeof(u64));
	if (reserved)
		reserved++;

	old = sdp->sd_log_blks_free;
	sdp->sd_log_blks_free += tr->tr_reserved -
				 (reserved - sdp->sd_log_blks_reserved);

	gfs2_assert_withdraw(sdp, sdp->sd_log_blks_free >= old);
	gfs2_assert_withdraw(sdp,
			     sdp->sd_log_blks_free <= sdp->sd_jdesc->jd_blocks +
			     sdp->sd_log_num_hdrs);

	sdp->sd_log_blks_reserved = reserved;

	gfs2_log_unlock(sdp);
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
	lops_incore_commit(sdp, tr);

	sdp->sd_vfs->s_dirt = 1;
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
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_gl);
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_buf);
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_jdata);
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_revoke);
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_rg);
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_databuf);
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_hdrs);
	gfs2_assert_withdraw(sdp, list_empty(&sdp->sd_ail1_list));

	sdp->sd_log_flush_head = sdp->sd_log_head;
	sdp->sd_log_flush_wrapped = 0;

	log_write_header(sdp, GFS2_LOG_HEAD_UNMOUNT, 0);

	gfs2_assert_warn(sdp, sdp->sd_log_blks_free == sdp->sd_jdesc->jd_blocks);
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

