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
#include <linux/lm_interface.h>

#include "gfs2.h"
#include "incore.h"
#include "glock.h"
#include "log.h"
#include "lops.h"
#include "meta_io.h"
#include "recovery.h"
#include "rgrp.h"
#include "trans.h"
#include "util.h"

static void glock_lo_add(struct gfs2_sbd *sdp, struct gfs2_log_element *le)
{
	struct gfs2_glock *gl;
	struct gfs2_trans *tr = current->journal_info;

	tr->tr_touched = 1;

	if (!list_empty(&le->le_list))
		return;

	gl = container_of(le, struct gfs2_glock, gl_le);
	if (gfs2_assert_withdraw(sdp, gfs2_glock_is_held_excl(gl)))
		return;
	gfs2_glock_hold(gl);
	set_bit(GLF_DIRTY, &gl->gl_flags);

	gfs2_log_lock(sdp);
	sdp->sd_log_num_gl++;
	list_add(&le->le_list, &sdp->sd_log_le_gl);
	gfs2_log_unlock(sdp);
}

static void glock_lo_after_commit(struct gfs2_sbd *sdp, struct gfs2_ail *ai)
{
	struct list_head *head = &sdp->sd_log_le_gl;
	struct gfs2_glock *gl;

	while (!list_empty(head)) {
		gl = list_entry(head->next, struct gfs2_glock, gl_le.le_list);
		list_del_init(&gl->gl_le.le_list);
		sdp->sd_log_num_gl--;

		gfs2_assert_withdraw(sdp, gfs2_glock_is_held_excl(gl));
		gfs2_glock_put(gl);
	}
	gfs2_assert_warn(sdp, !sdp->sd_log_num_gl);
}

static void buf_lo_add(struct gfs2_sbd *sdp, struct gfs2_log_element *le)
{
	struct gfs2_bufdata *bd = container_of(le, struct gfs2_bufdata, bd_le);
	struct gfs2_trans *tr;

	if (!list_empty(&bd->bd_list_tr))
		return;

	tr = current->journal_info;
	tr->tr_touched = 1;
	tr->tr_num_buf++;
	list_add(&bd->bd_list_tr, &tr->tr_list_buf);

	if (!list_empty(&le->le_list))
		return;

	gfs2_trans_add_gl(bd->bd_gl);

	gfs2_meta_check(sdp, bd->bd_bh);
	gfs2_pin(sdp, bd->bd_bh);

	gfs2_log_lock(sdp);
	sdp->sd_log_num_buf++;
	list_add(&le->le_list, &sdp->sd_log_le_buf);
	gfs2_log_unlock(sdp);

	tr->tr_num_buf_new++;
}

static void buf_lo_incore_commit(struct gfs2_sbd *sdp, struct gfs2_trans *tr)
{
	struct list_head *head = &tr->tr_list_buf;
	struct gfs2_bufdata *bd;

	while (!list_empty(head)) {
		bd = list_entry(head->next, struct gfs2_bufdata, bd_list_tr);
		list_del_init(&bd->bd_list_tr);
		tr->tr_num_buf--;
	}
	gfs2_assert_warn(sdp, !tr->tr_num_buf);
}

static void buf_lo_before_commit(struct gfs2_sbd *sdp)
{
	struct buffer_head *bh;
	struct gfs2_log_descriptor *ld;
	struct gfs2_bufdata *bd1 = NULL, *bd2;
	unsigned int total = sdp->sd_log_num_buf;
	unsigned int offset = sizeof(struct gfs2_log_descriptor);
	unsigned int limit;
	unsigned int num;
	unsigned n;
	__be64 *ptr;

	offset += sizeof(__be64) - 1;
	offset &= ~(sizeof(__be64) - 1);
	limit = (sdp->sd_sb.sb_bsize - offset)/sizeof(__be64);
	/* for 4k blocks, limit = 503 */

	bd1 = bd2 = list_prepare_entry(bd1, &sdp->sd_log_le_buf, bd_le.le_list);
	while(total) {
		num = total;
		if (total > limit)
			num = limit;
		bh = gfs2_log_get_buf(sdp);
		sdp->sd_log_num_hdrs++;
		ld = (struct gfs2_log_descriptor *)bh->b_data;
		ptr = (__be64 *)(bh->b_data + offset);
		ld->ld_header.mh_magic = cpu_to_be32(GFS2_MAGIC);
		ld->ld_header.mh_type = cpu_to_be32(GFS2_METATYPE_LD);
		ld->ld_header.mh_format = cpu_to_be32(GFS2_FORMAT_LD);
		ld->ld_type = cpu_to_be32(GFS2_LOG_DESC_METADATA);
		ld->ld_length = cpu_to_be32(num + 1);
		ld->ld_data1 = cpu_to_be32(num);
		ld->ld_data2 = cpu_to_be32(0);
		memset(ld->ld_reserved, 0, sizeof(ld->ld_reserved));

		n = 0;
		list_for_each_entry_continue(bd1, &sdp->sd_log_le_buf,
					     bd_le.le_list) {
			*ptr++ = cpu_to_be64(bd1->bd_bh->b_blocknr);
			if (++n >= num)
				break;
		}

		set_buffer_dirty(bh);
		ll_rw_block(WRITE, 1, &bh);

		n = 0;
		list_for_each_entry_continue(bd2, &sdp->sd_log_le_buf,
					     bd_le.le_list) {
			bh = gfs2_log_fake_buf(sdp, bd2->bd_bh);
			set_buffer_dirty(bh);
			ll_rw_block(WRITE, 1, &bh);
			if (++n >= num)
				break;
		}

		total -= num;
	}
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
			       struct gfs2_log_header *head, int pass)
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

	gfs2_log_lock(sdp);
	sdp->sd_log_num_revoke++;
	list_add(&le->le_list, &sdp->sd_log_le_revoke);
	gfs2_log_unlock(sdp);
}

static void revoke_lo_before_commit(struct gfs2_sbd *sdp)
{
	struct gfs2_log_descriptor *ld;
	struct gfs2_meta_header *mh;
	struct buffer_head *bh;
	unsigned int offset;
	struct list_head *head = &sdp->sd_log_le_revoke;
	struct gfs2_revoke *rv;

	if (!sdp->sd_log_num_revoke)
		return;

	bh = gfs2_log_get_buf(sdp);
	ld = (struct gfs2_log_descriptor *)bh->b_data;
	ld->ld_header.mh_magic = cpu_to_be32(GFS2_MAGIC);
	ld->ld_header.mh_type = cpu_to_be32(GFS2_METATYPE_LD);
	ld->ld_header.mh_format = cpu_to_be32(GFS2_FORMAT_LD);
	ld->ld_type = cpu_to_be32(GFS2_LOG_DESC_REVOKE);
	ld->ld_length = cpu_to_be32(gfs2_struct2blk(sdp, sdp->sd_log_num_revoke,
						    sizeof(u64)));
	ld->ld_data1 = cpu_to_be32(sdp->sd_log_num_revoke);
	ld->ld_data2 = cpu_to_be32(0);
	memset(ld->ld_reserved, 0, sizeof(ld->ld_reserved));
	offset = sizeof(struct gfs2_log_descriptor);

	while (!list_empty(head)) {
		rv = list_entry(head->next, struct gfs2_revoke, rv_le.le_list);
		list_del_init(&rv->rv_le.le_list);
		sdp->sd_log_num_revoke--;

		if (offset + sizeof(u64) > sdp->sd_sb.sb_bsize) {
			set_buffer_dirty(bh);
			ll_rw_block(WRITE, 1, &bh);

			bh = gfs2_log_get_buf(sdp);
			mh = (struct gfs2_meta_header *)bh->b_data;
			mh->mh_magic = cpu_to_be32(GFS2_MAGIC);
			mh->mh_type = cpu_to_be32(GFS2_METATYPE_LB);
			mh->mh_format = cpu_to_be32(GFS2_FORMAT_LB);
			offset = sizeof(struct gfs2_meta_header);
		}

		*(__be64 *)(bh->b_data + offset) = cpu_to_be64(rv->rv_blkno);
		kfree(rv);

		offset += sizeof(u64);
	}
	gfs2_assert_withdraw(sdp, !sdp->sd_log_num_revoke);

	set_buffer_dirty(bh);
	ll_rw_block(WRITE, 1, &bh);
}

static void revoke_lo_before_scan(struct gfs2_jdesc *jd,
				  struct gfs2_log_header *head, int pass)
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
			if (error < 0)
				return error;
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

	if (!list_empty(&le->le_list))
		return;

	rgd = container_of(le, struct gfs2_rgrpd, rd_le);
	gfs2_rgrp_bh_hold(rgd);

	gfs2_log_lock(sdp);
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

	tr->tr_touched = 1;
	if (list_empty(&bd->bd_list_tr) &&
	    (ip->i_di.di_flags & GFS2_DIF_JDATA)) {
		tr->tr_num_buf++;
		list_add(&bd->bd_list_tr, &tr->tr_list_buf);
		gfs2_pin(sdp, bd->bd_bh);
		tr->tr_num_buf_new++;
	}
	gfs2_trans_add_gl(bd->bd_gl);
	gfs2_log_lock(sdp);
	if (list_empty(&le->le_list)) {
		if (ip->i_di.di_flags & GFS2_DIF_JDATA)
			sdp->sd_log_num_jdata++;
		sdp->sd_log_num_databuf++;
		list_add(&le->le_list, &sdp->sd_log_le_databuf);
	}
	gfs2_log_unlock(sdp);
}

static int gfs2_check_magic(struct buffer_head *bh)
{
	struct page *page = bh->b_page;
	void *kaddr;
	__be32 *ptr;
	int rv = 0;

	kaddr = kmap_atomic(page, KM_USER0);
	ptr = kaddr + bh_offset(bh);
	if (*ptr == cpu_to_be32(GFS2_MAGIC))
		rv = 1;
	kunmap_atomic(kaddr, KM_USER0);

	return rv;
}

/**
 * databuf_lo_before_commit - Scan the data buffers, writing as we go
 *
 * Here we scan through the lists of buffers and make the assumption
 * that any buffer thats been pinned is being journaled, and that
 * any unpinned buffer is an ordered write data buffer and therefore
 * will be written back rather than journaled.
 */
static void databuf_lo_before_commit(struct gfs2_sbd *sdp)
{
	LIST_HEAD(started);
	struct gfs2_bufdata *bd1 = NULL, *bd2, *bdt;
	struct buffer_head *bh = NULL;
	unsigned int offset = sizeof(struct gfs2_log_descriptor);
	struct gfs2_log_descriptor *ld;
	unsigned int limit;
	unsigned int total_dbuf = sdp->sd_log_num_databuf;
	unsigned int total_jdata = sdp->sd_log_num_jdata;
	unsigned int num, n;
	__be64 *ptr = NULL;

	offset += 2*sizeof(__be64) - 1;
	offset &= ~(2*sizeof(__be64) - 1);
	limit = (sdp->sd_sb.sb_bsize - offset)/sizeof(__be64);

	/*
	 * Start writing ordered buffers, write journaled buffers
	 * into the log along with a header
	 */
	gfs2_log_lock(sdp);
	bd2 = bd1 = list_prepare_entry(bd1, &sdp->sd_log_le_databuf,
				       bd_le.le_list);
	while(total_dbuf) {
		num = total_jdata;
		if (num > limit)
			num = limit;
		n = 0;
		list_for_each_entry_safe_continue(bd1, bdt,
						  &sdp->sd_log_le_databuf,
						  bd_le.le_list) {
			/* An ordered write buffer */
			if (bd1->bd_bh && !buffer_pinned(bd1->bd_bh)) {
				list_move(&bd1->bd_le.le_list, &started);
				if (bd1 == bd2) {
					bd2 = NULL;
					bd2 = list_prepare_entry(bd2,
							&sdp->sd_log_le_databuf,
							bd_le.le_list);
				}
				total_dbuf--;
				if (bd1->bd_bh) {
					get_bh(bd1->bd_bh);
					if (buffer_dirty(bd1->bd_bh)) {
						gfs2_log_unlock(sdp);
						wait_on_buffer(bd1->bd_bh);
						ll_rw_block(WRITE, 1,
							    &bd1->bd_bh);
						gfs2_log_lock(sdp);
					}
					brelse(bd1->bd_bh);
					continue;
				}
				continue;
			} else if (bd1->bd_bh) { /* A journaled buffer */
				int magic;
				gfs2_log_unlock(sdp);
				if (!bh) {
					bh = gfs2_log_get_buf(sdp);
					sdp->sd_log_num_hdrs++;
					ld = (struct gfs2_log_descriptor *)
					     bh->b_data;
					ptr = (__be64 *)(bh->b_data + offset);
					ld->ld_header.mh_magic =
						cpu_to_be32(GFS2_MAGIC);
					ld->ld_header.mh_type =
						cpu_to_be32(GFS2_METATYPE_LD);
					ld->ld_header.mh_format =
						cpu_to_be32(GFS2_FORMAT_LD);
					ld->ld_type =
						cpu_to_be32(GFS2_LOG_DESC_JDATA);
					ld->ld_length = cpu_to_be32(num + 1);
					ld->ld_data1 = cpu_to_be32(num);
					ld->ld_data2 = cpu_to_be32(0);
					memset(ld->ld_reserved, 0, sizeof(ld->ld_reserved));
				}
				magic = gfs2_check_magic(bd1->bd_bh);
				*ptr++ = cpu_to_be64(bd1->bd_bh->b_blocknr);
				*ptr++ = cpu_to_be64((__u64)magic);
				clear_buffer_escaped(bd1->bd_bh);
				if (unlikely(magic != 0))
					set_buffer_escaped(bd1->bd_bh);
				gfs2_log_lock(sdp);
				if (n++ > num)
					break;
			} else if (!bd1->bd_bh) {
				total_dbuf--;
				sdp->sd_log_num_databuf--;
				list_del_init(&bd1->bd_le.le_list);
				if (bd1 == bd2) {
					bd2 = NULL;
					bd2 = list_prepare_entry(bd2,
						&sdp->sd_log_le_databuf,
						bd_le.le_list);
                                }
				kmem_cache_free(gfs2_bufdata_cachep, bd1);
			}
		}
		gfs2_log_unlock(sdp);
		if (bh) {
			set_buffer_dirty(bh);
			ll_rw_block(WRITE, 1, &bh);
			bh = NULL;
		}
		n = 0;
		gfs2_log_lock(sdp);
		list_for_each_entry_continue(bd2, &sdp->sd_log_le_databuf,
					     bd_le.le_list) {
			if (!bd2->bd_bh)
				continue;
			/* copy buffer if it needs escaping */
			gfs2_log_unlock(sdp);
			if (unlikely(buffer_escaped(bd2->bd_bh))) {
				void *kaddr;
				struct page *page = bd2->bd_bh->b_page;
				bh = gfs2_log_get_buf(sdp);
				kaddr = kmap_atomic(page, KM_USER0);
				memcpy(bh->b_data,
				       kaddr + bh_offset(bd2->bd_bh),
				       sdp->sd_sb.sb_bsize);
				kunmap_atomic(kaddr, KM_USER0);
				*(__be32 *)bh->b_data = 0;
			} else {
				bh = gfs2_log_fake_buf(sdp, bd2->bd_bh);
			}
			set_buffer_dirty(bh);
			ll_rw_block(WRITE, 1, &bh);
			gfs2_log_lock(sdp);
			if (++n >= num)
				break;
		}
		bh = NULL;
		total_dbuf -= num;
		total_jdata -= num;
	}
	gfs2_log_unlock(sdp);

	/* Wait on all ordered buffers */
	while (!list_empty(&started)) {
		gfs2_log_lock(sdp);
		bd1 = list_entry(started.next, struct gfs2_bufdata,
				 bd_le.le_list);
		list_del_init(&bd1->bd_le.le_list);
		sdp->sd_log_num_databuf--;
		bh = bd1->bd_bh;
		if (bh) {
			bh->b_private = NULL;
			get_bh(bh);
			gfs2_log_unlock(sdp);
			wait_on_buffer(bh);
			brelse(bh);
		} else
			gfs2_log_unlock(sdp);

		kmem_cache_free(gfs2_bufdata_cachep, bd1);
	}

	/* We've removed all the ordered write bufs here, so only jdata left */
	gfs2_assert_warn(sdp, sdp->sd_log_num_databuf == sdp->sd_log_num_jdata);
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
		sdp->sd_log_num_jdata--;
		gfs2_unpin(sdp, bd->bd_bh, ai);
	}
	gfs2_assert_warn(sdp, !sdp->sd_log_num_databuf);
	gfs2_assert_warn(sdp, !sdp->sd_log_num_jdata);
}


const struct gfs2_log_operations gfs2_glock_lops = {
	.lo_add = glock_lo_add,
	.lo_after_commit = glock_lo_after_commit,
	.lo_name = "glock",
};

const struct gfs2_log_operations gfs2_buf_lops = {
	.lo_add = buf_lo_add,
	.lo_incore_commit = buf_lo_incore_commit,
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
	.lo_incore_commit = buf_lo_incore_commit,
	.lo_before_commit = databuf_lo_before_commit,
	.lo_after_commit = databuf_lo_after_commit,
	.lo_scan_elements = databuf_lo_scan_elements,
	.lo_after_scan = databuf_lo_after_scan,
	.lo_name = "databuf",
};

const struct gfs2_log_operations *gfs2_log_ops[] = {
	&gfs2_glock_lops,
	&gfs2_buf_lops,
	&gfs2_revoke_lops,
	&gfs2_rg_lops,
	&gfs2_databuf_lops,
	NULL,
};

