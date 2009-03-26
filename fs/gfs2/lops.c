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

#include "gfs2.h"
#include "incore.h"
#include "inode.h"
#include "glock.h"
#include "log.h"
#include "lops.h"
#include "meta_io.h"
#include "recovery.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"

/**
 * gfs2_pin - Pin a buffer in memory
 * @sdp: The superblock
 * @bh: The buffer to be pinned
 *
 * The log lock must be held when calling this function
 */
static void gfs2_pin(struct gfs2_sbd *sdp, struct buffer_head *bh)
{
	struct gfs2_bufdata *bd;

	gfs2_assert_withdraw(sdp, test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags));

	clear_buffer_dirty(bh);
	if (test_set_buffer_pinned(bh))
		gfs2_assert_withdraw(sdp, 0);
	if (!buffer_uptodate(bh))
		gfs2_io_error_bh(sdp, bh);
	bd = bh->b_private;
	/* If this buffer is in the AIL and it has already been written
	 * to in-place disk block, remove it from the AIL.
	 */
	if (bd->bd_ail)
		list_move(&bd->bd_ail_st_list, &bd->bd_ail->ai_ail2_list);
	get_bh(bh);
}

/**
 * gfs2_unpin - Unpin a buffer
 * @sdp: the filesystem the buffer belongs to
 * @bh: The buffer to unpin
 * @ai:
 *
 */

static void gfs2_unpin(struct gfs2_sbd *sdp, struct buffer_head *bh,
		       struct gfs2_ail *ai)
{
	struct gfs2_bufdata *bd = bh->b_private;

	gfs2_assert_withdraw(sdp, buffer_uptodate(bh));

	if (!buffer_pinned(bh))
		gfs2_assert_withdraw(sdp, 0);

	lock_buffer(bh);
	mark_buffer_dirty(bh);
	clear_buffer_pinned(bh);

	gfs2_log_lock(sdp);
	if (bd->bd_ail) {
		list_del(&bd->bd_ail_st_list);
		brelse(bh);
	} else {
		struct gfs2_glock *gl = bd->bd_gl;
		list_add(&bd->bd_ail_gl_list, &gl->gl_ail_list);
		atomic_inc(&gl->gl_ail_count);
	}
	bd->bd_ail = ai;
	list_add(&bd->bd_ail_st_list, &ai->ai_ail1_list);
	clear_bit(GLF_LFLUSH, &bd->bd_gl->gl_flags);
	gfs2_log_unlock(sdp);
	unlock_buffer(bh);
}


static inline struct gfs2_log_descriptor *bh_log_desc(struct buffer_head *bh)
{
	return (struct gfs2_log_descriptor *)bh->b_data;
}

static inline __be64 *bh_log_ptr(struct buffer_head *bh)
{
	struct gfs2_log_descriptor *ld = bh_log_desc(bh);
	return (__force __be64 *)(ld + 1);
}

static inline __be64 *bh_ptr_end(struct buffer_head *bh)
{
	return (__force __be64 *)(bh->b_data + bh->b_size);
}


static struct buffer_head *gfs2_get_log_desc(struct gfs2_sbd *sdp, u32 ld_type)
{
	struct buffer_head *bh = gfs2_log_get_buf(sdp);
	struct gfs2_log_descriptor *ld = bh_log_desc(bh);
	ld->ld_header.mh_magic = cpu_to_be32(GFS2_MAGIC);
	ld->ld_header.mh_type = cpu_to_be32(GFS2_METATYPE_LD);
	ld->ld_header.mh_format = cpu_to_be32(GFS2_FORMAT_LD);
	ld->ld_type = cpu_to_be32(ld_type);
	ld->ld_length = 0;
	ld->ld_data1 = 0;
	ld->ld_data2 = 0;
	memset(ld->ld_reserved, 0, sizeof(ld->ld_reserved));
	return bh;
}

static void buf_lo_add(struct gfs2_sbd *sdp, struct gfs2_log_element *le)
{
	struct gfs2_bufdata *bd = container_of(le, struct gfs2_bufdata, bd_le);
	struct gfs2_trans *tr;

	lock_buffer(bd->bd_bh);
	gfs2_log_lock(sdp);
	if (!list_empty(&bd->bd_list_tr))
		goto out;
	tr = current->journal_info;
	tr->tr_touched = 1;
	tr->tr_num_buf++;
	list_add(&bd->bd_list_tr, &tr->tr_list_buf);
	if (!list_empty(&le->le_list))
		goto out;
	set_bit(GLF_LFLUSH, &bd->bd_gl->gl_flags);
	set_bit(GLF_DIRTY, &bd->bd_gl->gl_flags);
	gfs2_meta_check(sdp, bd->bd_bh);
	gfs2_pin(sdp, bd->bd_bh);
	sdp->sd_log_num_buf++;
	list_add(&le->le_list, &sdp->sd_log_le_buf);
	tr->tr_num_buf_new++;
out:
	gfs2_log_unlock(sdp);
	unlock_buffer(bd->bd_bh);
}

static void buf_lo_before_commit(struct gfs2_sbd *sdp)
{
	struct buffer_head *bh;
	struct gfs2_log_descriptor *ld;
	struct gfs2_bufdata *bd1 = NULL, *bd2;
	unsigned int total;
	unsigned int limit;
	unsigned int num;
	unsigned n;
	__be64 *ptr;

	limit = buf_limit(sdp);
	/* for 4k blocks, limit = 503 */

	gfs2_log_lock(sdp);
	total = sdp->sd_log_num_buf;
	bd1 = bd2 = list_prepare_entry(bd1, &sdp->sd_log_le_buf, bd_le.le_list);
	while(total) {
		num = total;
		if (total > limit)
			num = limit;
		gfs2_log_unlock(sdp);
		bh = gfs2_get_log_desc(sdp, GFS2_LOG_DESC_METADATA);
		gfs2_log_lock(sdp);
		ld = bh_log_desc(bh);
		ptr = bh_log_ptr(bh);
		ld->ld_length = cpu_to_be32(num + 1);
		ld->ld_data1 = cpu_to_be32(num);

		n = 0;
		list_for_each_entry_continue(bd1, &sdp->sd_log_le_buf,
					     bd_le.le_list) {
			*ptr++ = cpu_to_be64(bd1->bd_bh->b_blocknr);
			if (++n >= num)
				break;
		}

		gfs2_log_unlock(sdp);
		submit_bh(WRITE, bh);
		gfs2_log_lock(sdp);

		n = 0;
		list_for_each_entry_continue(bd2, &sdp->sd_log_le_buf,
					     bd_le.le_list) {
			get_bh(bd2->bd_bh);
			gfs2_log_unlock(sdp);
			lock_buffer(bd2->bd_bh);
			bh = gfs2_log_fake_buf(sdp, bd2->bd_bh);
			submit_bh(WRITE, bh);
			gfs2_log_lock(sdp);
			if (++n >= num)
				break;
		}

		BUG_ON(total < num);
		total -= num;
	}
	gfs2_log_unlock(sdp);
}

static void buf_lo_after_commit(struct gfs2_sbd *sdp, struct gfs2_ail *ai)
{
	struct list_head *head = &sdp->sd_log_le_buf;
	struct gfs2_bufdata *bd;

	while (!list_empty(head)) {
		bd = list_entry(head->next, struct gfs2_bufdata, bd_le.le_list);
		list_del_init(&bd->bd_le.le_list);
		sdp->sd_log_num_buf--;

		gfs2_unpin(sdp, bd->bd_bh, ai);
	}
	gfs2_assert_warn(sdp, !sdp->sd_log_num_buf);
}

static void buf_lo_before_scan(struct gfs2_jdesc *jd,
			       struct gfs2_log_header_host *head, int pass)
{
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);

	if (pass != 0)
		return;

	sdp->sd_found_blocks = 0;
	sdp->sd_replayed_blocks = 0;
}

static int buf_lo_scan_elements(struct gfs2_jdesc *jd, unsigned int start,
				struct gfs2_log_descriptor *ld, __be64 *ptr,
				int pass)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);
	struct gfs2_glock *gl = ip->i_gl;
	unsigned int blks = be32_to_cpu(ld->ld_data1);
	struct buffer_head *bh_log, *bh_ip;
	u64 blkno;
	int error = 0;

	if (pass != 1 || be32_to_cpu(ld->ld_type) != GFS2_LOG_DESC_METADATA)
		return 0;

	gfs2_replay_incr_blk(sdp, &start);

	for (; blks; gfs2_replay_incr_blk(sdp, &start), blks--) {
		blkno = be64_to_cpu(*ptr++);

		sdp->sd_found_blocks++;

		if (gfs2_revoke_check(sdp, blkno, start))
			continue;

		error = gfs2_replay_read_block(jd, start, &bh_log);
		if (error)
			return error;

		bh_ip = gfs2_meta_new(gl, blkno);
		memcpy(bh_ip->b_data, bh_log->b_data, bh_log->b_size);

		if (gfs2_meta_check(sdp, bh_ip))
			error = -EIO;
		else
			mark_buffer_dirty(bh_ip);

		brelse(bh_log);
		brelse(bh_ip);

		if (error)
			break;

		sdp->sd_replayed_blocks++;
	}

	return error;
}

static void buf_lo_after_scan(struct gfs2_jdesc *jd, int error, int pass)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);

	if (error) {
		gfs2_meta_sync(ip->i_gl);
		return;
	}
	if (pass != 1)
		return;

	gfs2_meta_sync(ip->i_gl);

	fs_info(sdp, "jid=%u: Replayed %u of %u blocks\n",
	        jd->jd_jid, sdp->sd_replayed_blocks, sdp->sd_found_blocks);
}

static void revoke_lo_add(struct gfs2_sbd *sdp, struct gfs2_log_element *le)
{
	struct gfs2_trans *tr;

	tr = current->journal_info;
	tr->tr_touched = 1;
	tr->tr_num_revoke++;
	sdp->sd_log_num_revoke++;
	list_add(&le->le_list, &sdp->sd_log_le_revoke);
}

static void revoke_lo_before_commit(struct gfs2_sbd *sdp)
{
	struct gfs2_log_descriptor *ld;
	struct gfs2_meta_header *mh;
	struct buffer_head *bh;
	unsigned int offset;
	struct list_head *head = &sdp->sd_log_le_revoke;
	struct gfs2_bufdata *bd;

	if (!sdp->sd_log_num_revoke)
		return;

	bh = gfs2_get_log_desc(sdp, GFS2_LOG_DESC_REVOKE);
	ld = bh_log_desc(bh);
	ld->ld_length = cpu_to_be32(gfs2_struct2blk(sdp, sdp->sd_log_num_revoke,
						    sizeof(u64)));
	ld->ld_data1 = cpu_to_be32(sdp->sd_log_num_revoke);
	offset = sizeof(struct gfs2_log_descriptor);

	while (!list_empty(head)) {
		bd = list_entry(head->next, struct gfs2_bufdata, bd_le.le_list);
		list_del_init(&bd->bd_le.le_list);
		sdp->sd_log_num_revoke--;

		if (offset + sizeof(u64) > sdp->sd_sb.sb_bsize) {
			submit_bh(WRITE, bh);

			bh = gfs2_log_get_buf(sdp);
			mh = (struct gfs2_meta_header *)bh->b_data;
			mh->mh_magic = cpu_to_be32(GFS2_MAGIC);
			mh->mh_type = cpu_to_be32(GFS2_METATYPE_LB);
			mh->mh_format = cpu_to_be32(GFS2_FORMAT_LB);
			offset = sizeof(struct gfs2_meta_header);
		}

		*(__be64 *)(bh->b_data + offset) = cpu_to_be64(bd->bd_blkno);
		kmem_cache_free(gfs2_bufdata_cachep, bd);

		offset += sizeof(u64);
	}
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_revoke);

	submit_bh(WRITE, bh);
}

static void revoke_lo_before_scan(struct gfs2_jdesc *jd,
				  struct gfs2_log_header_host *head, int pass)
{
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);

	if (pass != 0)
		return;

	sdp->sd_found_revokes = 0;
	sdp->sd_replay_tail = head->lh_tail;
}

static int revoke_lo_scan_elements(struct gfs2_jdesc *jd, unsigned int start,
				   struct gfs2_log_descriptor *ld, __be64 *ptr,
				   int pass)
{
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);
	unsigned int blks = be32_to_cpu(ld->ld_length);
	unsigned int revokes = be32_to_cpu(ld->ld_data1);
	struct buffer_head *bh;
	unsigned int offset;
	u64 blkno;
	int first = 1;
	int error;

	if (pass != 0 || be32_to_cpu(ld->ld_type) != GFS2_LOG_DESC_REVOKE)
		return 0;

	offset = sizeof(struct gfs2_log_descriptor);

	for (; blks; gfs2_replay_incr_blk(sdp, &start), blks--) {
		error = gfs2_replay_read_block(jd, start, &bh);
		if (error)
			return error;

		if (!first)
			gfs2_metatype_check(sdp, bh, GFS2_METATYPE_LB);

		while (offset + sizeof(u64) <= sdp->sd_sb.sb_bsize) {
			blkno = be64_to_cpu(*(__be64 *)(bh->b_data + offset));

			error = gfs2_revoke_add(sdp, blkno, start);
			if (error < 0) {
				brelse(bh);
				return error;
			}
			else if (error)
				sdp->sd_found_revokes++;

			if (!--revokes)
				break;
			offset += sizeof(u64);
		}

		brelse(bh);
		offset = sizeof(struct gfs2_meta_header);
		first = 0;
	}

	return 0;
}

static void revoke_lo_after_scan(struct gfs2_jdesc *jd, int error, int pass)
{
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);

	if (error) {
		gfs2_revoke_clean(sdp);
		return;
	}
	if (pass != 1)
		return;

	fs_info(sdp, "jid=%u: Found %u revoke tags\n",
	        jd->jd_jid, sdp->sd_found_revokes);

	gfs2_revoke_clean(sdp);
}

static void rg_lo_add(struct gfs2_sbd *sdp, struct gfs2_log_element *le)
{
	struct gfs2_rgrpd *rgd;
	struct gfs2_trans *tr = current->journal_info;

	tr->tr_touched = 1;

	rgd = container_of(le, struct gfs2_rgrpd, rd_le);

	gfs2_log_lock(sdp);
	if (!list_empty(&le->le_list)){
		gfs2_log_unlock(sdp);
		return;
	}
	gfs2_rgrp_bh_hold(rgd);
	sdp->sd_log_num_rg++;
	list_add(&le->le_list, &sdp->sd_log_le_rg);
	gfs2_log_unlock(sdp);
}

static void rg_lo_after_commit(struct gfs2_sbd *sdp, struct gfs2_ail *ai)
{
	struct list_head *head = &sdp->sd_log_le_rg;
	struct gfs2_rgrpd *rgd;

	while (!list_empty(head)) {
		rgd = list_entry(head->next, struct gfs2_rgrpd, rd_le.le_list);
		list_del_init(&rgd->rd_le.le_list);
		sdp->sd_log_num_rg--;

		gfs2_rgrp_repolish_clones(rgd);
		gfs2_rgrp_bh_put(rgd);
	}
	gfs2_assert_warn(sdp, !sdp->sd_log_num_rg);
}

/**
 * databuf_lo_add - Add a databuf to the transaction.
 *
 * This is used in two distinct cases:
 * i) In ordered write mode
 *    We put the data buffer on a list so that we can ensure that its
 *    synced to disk at the right time
 * ii) In journaled data mode
 *    We need to journal the data block in the same way as metadata in
 *    the functions above. The difference is that here we have a tag
 *    which is two __be64's being the block number (as per meta data)
 *    and a flag which says whether the data block needs escaping or
 *    not. This means we need a new log entry for each 251 or so data
 *    blocks, which isn't an enormous overhead but twice as much as
 *    for normal metadata blocks.
 */
static void databuf_lo_add(struct gfs2_sbd *sdp, struct gfs2_log_element *le)
{
	struct gfs2_bufdata *bd = container_of(le, struct gfs2_bufdata, bd_le);
	struct gfs2_trans *tr = current->journal_info;
	struct address_space *mapping = bd->bd_bh->b_page->mapping;
	struct gfs2_inode *ip = GFS2_I(mapping->host);

	lock_buffer(bd->bd_bh);
	gfs2_log_lock(sdp);
	if (tr) {
		if (!list_empty(&bd->bd_list_tr))
			goto out;
		tr->tr_touched = 1;
		if (gfs2_is_jdata(ip)) {
			tr->tr_num_buf++;
			list_add(&bd->bd_list_tr, &tr->tr_list_buf);
		}
	}
	if (!list_empty(&le->le_list))
		goto out;

	set_bit(GLF_LFLUSH, &bd->bd_gl->gl_flags);
	set_bit(GLF_DIRTY, &bd->bd_gl->gl_flags);
	if (gfs2_is_jdata(ip)) {
		gfs2_pin(sdp, bd->bd_bh);
		tr->tr_num_databuf_new++;
		sdp->sd_log_num_databuf++;
		list_add(&le->le_list, &sdp->sd_log_le_databuf);
	} else {
		list_add(&le->le_list, &sdp->sd_log_le_ordered);
	}
out:
	gfs2_log_unlock(sdp);
	unlock_buffer(bd->bd_bh);
}

static void gfs2_check_magic(struct buffer_head *bh)
{
	void *kaddr;
	__be32 *ptr;

	clear_buffer_escaped(bh);
	kaddr = kmap_atomic(bh->b_page, KM_USER0);
	ptr = kaddr + bh_offset(bh);
	if (*ptr == cpu_to_be32(GFS2_MAGIC))
		set_buffer_escaped(bh);
	kunmap_atomic(kaddr, KM_USER0);
}

static void gfs2_write_blocks(struct gfs2_sbd *sdp, struct buffer_head *bh,
			      struct list_head *list, struct list_head *done,
			      unsigned int n)
{
	struct buffer_head *bh1;
	struct gfs2_log_descriptor *ld;
	struct gfs2_bufdata *bd;
	__be64 *ptr;

	if (!bh)
		return;

	ld = bh_log_desc(bh);
	ld->ld_length = cpu_to_be32(n + 1);
	ld->ld_data1 = cpu_to_be32(n);

	ptr = bh_log_ptr(bh);
	
	get_bh(bh);
	submit_bh(WRITE, bh);
	gfs2_log_lock(sdp);
	while(!list_empty(list)) {
		bd = list_entry(list->next, struct gfs2_bufdata, bd_le.le_list);
		list_move_tail(&bd->bd_le.le_list, done);
		get_bh(bd->bd_bh);
		while (be64_to_cpu(*ptr) != bd->bd_bh->b_blocknr) {
			gfs2_log_incr_head(sdp);
			ptr += 2;
		}
		gfs2_log_unlock(sdp);
		lock_buffer(bd->bd_bh);
		if (buffer_escaped(bd->bd_bh)) {
			void *kaddr;
			bh1 = gfs2_log_get_buf(sdp);
			kaddr = kmap_atomic(bd->bd_bh->b_page, KM_USER0);
			memcpy(bh1->b_data, kaddr + bh_offset(bd->bd_bh),
			       bh1->b_size);
			kunmap_atomic(kaddr, KM_USER0);
			*(__be32 *)bh1->b_data = 0;
			clear_buffer_escaped(bd->bd_bh);
			unlock_buffer(bd->bd_bh);
			brelse(bd->bd_bh);
		} else {
			bh1 = gfs2_log_fake_buf(sdp, bd->bd_bh);
		}
		submit_bh(WRITE, bh1);
		gfs2_log_lock(sdp);
		ptr += 2;
	}
	gfs2_log_unlock(sdp);
	brelse(bh);
}

/**
 * databuf_lo_before_commit - Scan the data buffers, writing as we go
 *
 */

static void databuf_lo_before_commit(struct gfs2_sbd *sdp)
{
	struct gfs2_bufdata *bd = NULL;
	struct buffer_head *bh = NULL;
	unsigned int n = 0;
	__be64 *ptr = NULL, *end = NULL;
	LIST_HEAD(processed);
	LIST_HEAD(in_progress);

	gfs2_log_lock(sdp);
	while (!list_empty(&sdp->sd_log_le_databuf)) {
		if (ptr == end) {
			gfs2_log_unlock(sdp);
			gfs2_write_blocks(sdp, bh, &in_progress, &processed, n);
			n = 0;
			bh = gfs2_get_log_desc(sdp, GFS2_LOG_DESC_JDATA);
			ptr = bh_log_ptr(bh);
			end = bh_ptr_end(bh) - 1;
			gfs2_log_lock(sdp);
			continue;
		}
		bd = list_entry(sdp->sd_log_le_databuf.next, struct gfs2_bufdata, bd_le.le_list);
		list_move_tail(&bd->bd_le.le_list, &in_progress);
		gfs2_check_magic(bd->bd_bh);
		*ptr++ = cpu_to_be64(bd->bd_bh->b_blocknr);
		*ptr++ = cpu_to_be64(buffer_escaped(bh) ? 1 : 0);
		n++;
	}
	gfs2_log_unlock(sdp);
	gfs2_write_blocks(sdp, bh, &in_progress, &processed, n);
	gfs2_log_lock(sdp);
	list_splice(&processed, &sdp->sd_log_le_databuf);
	gfs2_log_unlock(sdp);
}

static int databuf_lo_scan_elements(struct gfs2_jdesc *jd, unsigned int start,
				    struct gfs2_log_descriptor *ld,
				    __be64 *ptr, int pass)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);
	struct gfs2_glock *gl = ip->i_gl;
	unsigned int blks = be32_to_cpu(ld->ld_data1);
	struct buffer_head *bh_log, *bh_ip;
	u64 blkno;
	u64 esc;
	int error = 0;

	if (pass != 1 || be32_to_cpu(ld->ld_type) != GFS2_LOG_DESC_JDATA)
		return 0;

	gfs2_replay_incr_blk(sdp, &start);
	for (; blks; gfs2_replay_incr_blk(sdp, &start), blks--) {
		blkno = be64_to_cpu(*ptr++);
		esc = be64_to_cpu(*ptr++);

		sdp->sd_found_blocks++;

		if (gfs2_revoke_check(sdp, blkno, start))
			continue;

		error = gfs2_replay_read_block(jd, start, &bh_log);
		if (error)
			return error;

		bh_ip = gfs2_meta_new(gl, blkno);
		memcpy(bh_ip->b_data, bh_log->b_data, bh_log->b_size);

		/* Unescape */
		if (esc) {
			__be32 *eptr = (__be32 *)bh_ip->b_data;
			*eptr = cpu_to_be32(GFS2_MAGIC);
		}
		mark_buffer_dirty(bh_ip);

		brelse(bh_log);
		brelse(bh_ip);
		if (error)
			break;

		sdp->sd_replayed_blocks++;
	}

	return error;
}

/* FIXME: sort out accounting for log blocks etc. */

static void databuf_lo_after_scan(struct gfs2_jdesc *jd, int error, int pass)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);

	if (error) {
		gfs2_meta_sync(ip->i_gl);
		return;
	}
	if (pass != 1)
		return;

	/* data sync? */
	gfs2_meta_sync(ip->i_gl);

	fs_info(sdp, "jid=%u: Replayed %u of %u data blocks\n",
		jd->jd_jid, sdp->sd_replayed_blocks, sdp->sd_found_blocks);
}

static void databuf_lo_after_commit(struct gfs2_sbd *sdp, struct gfs2_ail *ai)
{
	struct list_head *head = &sdp->sd_log_le_databuf;
	struct gfs2_bufdata *bd;

	while (!list_empty(head)) {
		bd = list_entry(head->next, struct gfs2_bufdata, bd_le.le_list);
		list_del_init(&bd->bd_le.le_list);
		sdp->sd_log_num_databuf--;
		gfs2_unpin(sdp, bd->bd_bh, ai);
	}
	gfs2_assert_warn(sdp, !sdp->sd_log_num_databuf);
}


const struct gfs2_log_operations gfs2_buf_lops = {
	.lo_add = buf_lo_add,
	.lo_before_commit = buf_lo_before_commit,
	.lo_after_commit = buf_lo_after_commit,
	.lo_before_scan = buf_lo_before_scan,
	.lo_scan_elements = buf_lo_scan_elements,
	.lo_after_scan = buf_lo_after_scan,
	.lo_name = "buf",
};

const struct gfs2_log_operations gfs2_revoke_lops = {
	.lo_add = revoke_lo_add,
	.lo_before_commit = revoke_lo_before_commit,
	.lo_before_scan = revoke_lo_before_scan,
	.lo_scan_elements = revoke_lo_scan_elements,
	.lo_after_scan = revoke_lo_after_scan,
	.lo_name = "revoke",
};

const struct gfs2_log_operations gfs2_rg_lops = {
	.lo_add = rg_lo_add,
	.lo_after_commit = rg_lo_after_commit,
	.lo_name = "rg",
};

const struct gfs2_log_operations gfs2_databuf_lops = {
	.lo_add = databuf_lo_add,
	.lo_before_commit = databuf_lo_before_commit,
	.lo_after_commit = databuf_lo_after_commit,
	.lo_scan_elements = databuf_lo_scan_elements,
	.lo_after_scan = databuf_lo_after_scan,
	.lo_name = "databuf",
};

const struct gfs2_log_operations *gfs2_log_ops[] = {
	&gfs2_databuf_lops,
	&gfs2_buf_lops,
	&gfs2_rg_lops,
	&gfs2_revoke_lops,
	NULL,
};

