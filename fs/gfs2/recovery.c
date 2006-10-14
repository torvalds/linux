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

#include "gfs2.h"
#include "incore.h"
#include "bmap.h"
#include "glock.h"
#include "glops.h"
#include "lm.h"
#include "lops.h"
#include "meta_io.h"
#include "recovery.h"
#include "super.h"
#include "util.h"
#include "dir.h"

int gfs2_replay_read_block(struct gfs2_jdesc *jd, unsigned int blk,
			   struct buffer_head **bh)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_glock *gl = ip->i_gl;
	int new = 0;
	u64 dblock;
	u32 extlen;
	int error;

	error = gfs2_extent_map(&ip->i_inode, blk, &new, &dblock, &extlen);
	if (error)
		return error;
	if (!dblock) {
		gfs2_consist_inode(ip);
		return -EIO;
	}

	*bh = gfs2_meta_ra(gl, dblock, extlen);

	return error;
}

int gfs2_revoke_add(struct gfs2_sbd *sdp, u64 blkno, unsigned int where)
{
	struct list_head *head = &sdp->sd_revoke_list;
	struct gfs2_revoke_replay *rr;
	int found = 0;

	list_for_each_entry(rr, head, rr_list) {
		if (rr->rr_blkno == blkno) {
			found = 1;
			break;
		}
	}

	if (found) {
		rr->rr_where = where;
		return 0;
	}

	rr = kmalloc(sizeof(struct gfs2_revoke_replay), GFP_KERNEL);
	if (!rr)
		return -ENOMEM;

	rr->rr_blkno = blkno;
	rr->rr_where = where;
	list_add(&rr->rr_list, head);

	return 1;
}

int gfs2_revoke_check(struct gfs2_sbd *sdp, u64 blkno, unsigned int where)
{
	struct gfs2_revoke_replay *rr;
	int wrap, a, b, revoke;
	int found = 0;

	list_for_each_entry(rr, &sdp->sd_revoke_list, rr_list) {
		if (rr->rr_blkno == blkno) {
			found = 1;
			break;
		}
	}

	if (!found)
		return 0;

	wrap = (rr->rr_where < sdp->sd_replay_tail);
	a = (sdp->sd_replay_tail < where);
	b = (where < rr->rr_where);
	revoke = (wrap) ? (a || b) : (a && b);

	return revoke;
}

void gfs2_revoke_clean(struct gfs2_sbd *sdp)
{
	struct list_head *head = &sdp->sd_revoke_list;
	struct gfs2_revoke_replay *rr;

	while (!list_empty(head)) {
		rr = list_entry(head->next, struct gfs2_revoke_replay, rr_list);
		list_del(&rr->rr_list);
		kfree(rr);
	}
}

/**
 * get_log_header - read the log header for a given segment
 * @jd: the journal
 * @blk: the block to look at
 * @lh: the log header to return
 *
 * Read the log header for a given segement in a given journal.  Do a few
 * sanity checks on it.
 *
 * Returns: 0 on success,
 *          1 if the header was invalid or incomplete,
 *          errno on error
 */

static int get_log_header(struct gfs2_jdesc *jd, unsigned int blk,
			  struct gfs2_log_header_host *head)
{
	struct buffer_head *bh;
	struct gfs2_log_header_host lh;
	u32 hash;
	int error;

	error = gfs2_replay_read_block(jd, blk, &bh);
	if (error)
		return error;

	memcpy(&lh, bh->b_data, sizeof(struct gfs2_log_header));	/* XXX */
	lh.lh_hash = 0;
	hash = gfs2_disk_hash((char *)&lh, sizeof(struct gfs2_log_header));
	gfs2_log_header_in(&lh, bh->b_data);

	brelse(bh);

	if (lh.lh_header.mh_magic != GFS2_MAGIC ||
	    lh.lh_header.mh_type != GFS2_METATYPE_LH ||
	    lh.lh_blkno != blk || lh.lh_hash != hash)
		return 1;

	*head = lh;

	return 0;
}

/**
 * find_good_lh - find a good log header
 * @jd: the journal
 * @blk: the segment to start searching from
 * @lh: the log header to fill in
 * @forward: if true search forward in the log, else search backward
 *
 * Call get_log_header() to get a log header for a segment, but if the
 * segment is bad, either scan forward or backward until we find a good one.
 *
 * Returns: errno
 */

static int find_good_lh(struct gfs2_jdesc *jd, unsigned int *blk,
			struct gfs2_log_header_host *head)
{
	unsigned int orig_blk = *blk;
	int error;

	for (;;) {
		error = get_log_header(jd, *blk, head);
		if (error <= 0)
			return error;

		if (++*blk == jd->jd_blocks)
			*blk = 0;

		if (*blk == orig_blk) {
			gfs2_consist_inode(GFS2_I(jd->jd_inode));
			return -EIO;
		}
	}
}

/**
 * jhead_scan - make sure we've found the head of the log
 * @jd: the journal
 * @head: this is filled in with the log descriptor of the head
 *
 * At this point, seg and lh should be either the head of the log or just
 * before.  Scan forward until we find the head.
 *
 * Returns: errno
 */

static int jhead_scan(struct gfs2_jdesc *jd, struct gfs2_log_header_host *head)
{
	unsigned int blk = head->lh_blkno;
	struct gfs2_log_header_host lh;
	int error;

	for (;;) {
		if (++blk == jd->jd_blocks)
			blk = 0;

		error = get_log_header(jd, blk, &lh);
		if (error < 0)
			return error;
		if (error == 1)
			continue;

		if (lh.lh_sequence == head->lh_sequence) {
			gfs2_consist_inode(GFS2_I(jd->jd_inode));
			return -EIO;
		}
		if (lh.lh_sequence < head->lh_sequence)
			break;

		*head = lh;
	}

	return 0;
}

/**
 * gfs2_find_jhead - find the head of a log
 * @jd: the journal
 * @head: the log descriptor for the head of the log is returned here
 *
 * Do a binary search of a journal and find the valid log entry with the
 * highest sequence number.  (i.e. the log head)
 *
 * Returns: errno
 */

int gfs2_find_jhead(struct gfs2_jdesc *jd, struct gfs2_log_header_host *head)
{
	struct gfs2_log_header_host lh_1, lh_m;
	u32 blk_1, blk_2, blk_m;
	int error;

	blk_1 = 0;
	blk_2 = jd->jd_blocks - 1;

	for (;;) {
		blk_m = (blk_1 + blk_2) / 2;

		error = find_good_lh(jd, &blk_1, &lh_1);
		if (error)
			return error;

		error = find_good_lh(jd, &blk_m, &lh_m);
		if (error)
			return error;

		if (blk_1 == blk_m || blk_m == blk_2)
			break;

		if (lh_1.lh_sequence <= lh_m.lh_sequence)
			blk_1 = blk_m;
		else
			blk_2 = blk_m;
	}

	error = jhead_scan(jd, &lh_1);
	if (error)
		return error;

	*head = lh_1;

	return error;
}

/**
 * foreach_descriptor - go through the active part of the log
 * @jd: the journal
 * @start: the first log header in the active region
 * @end: the last log header (don't process the contents of this entry))
 *
 * Call a given function once for every log descriptor in the active
 * portion of the log.
 *
 * Returns: errno
 */

static int foreach_descriptor(struct gfs2_jdesc *jd, unsigned int start,
			      unsigned int end, int pass)
{
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);
	struct buffer_head *bh;
	struct gfs2_log_descriptor *ld;
	int error = 0;
	u32 length;
	__be64 *ptr;
	unsigned int offset = sizeof(struct gfs2_log_descriptor);
	offset += sizeof(__be64) - 1;
	offset &= ~(sizeof(__be64) - 1);

	while (start != end) {
		error = gfs2_replay_read_block(jd, start, &bh);
		if (error)
			return error;
		if (gfs2_meta_check(sdp, bh)) {
			brelse(bh);
			return -EIO;
		}
		ld = (struct gfs2_log_descriptor *)bh->b_data;
		length = be32_to_cpu(ld->ld_length);

		if (be32_to_cpu(ld->ld_header.mh_type) == GFS2_METATYPE_LH) {
			struct gfs2_log_header_host lh;
			error = get_log_header(jd, start, &lh);
			if (!error) {
				gfs2_replay_incr_blk(sdp, &start);
				brelse(bh);
				continue;
			}
			if (error == 1) {
				gfs2_consist_inode(GFS2_I(jd->jd_inode));
				error = -EIO;
			}
			brelse(bh);
			return error;
		} else if (gfs2_metatype_check(sdp, bh, GFS2_METATYPE_LD)) {
			brelse(bh);
			return -EIO;
		}
		ptr = (__be64 *)(bh->b_data + offset);
		error = lops_scan_elements(jd, start, ld, ptr, pass);
		if (error) {
			brelse(bh);
			return error;
		}

		while (length--)
			gfs2_replay_incr_blk(sdp, &start);

		brelse(bh);
	}

	return 0;
}

/**
 * clean_journal - mark a dirty journal as being clean
 * @sdp: the filesystem
 * @jd: the journal
 * @gl: the journal's glock
 * @head: the head journal to start from
 *
 * Returns: errno
 */

static int clean_journal(struct gfs2_jdesc *jd, struct gfs2_log_header_host *head)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);
	unsigned int lblock;
	struct gfs2_log_header *lh;
	u32 hash;
	struct buffer_head *bh;
	int error;
	struct buffer_head bh_map = { .b_state = 0, .b_blocknr = 0 };

	lblock = head->lh_blkno;
	gfs2_replay_incr_blk(sdp, &lblock);
	bh_map.b_size = 1 << ip->i_inode.i_blkbits;
	error = gfs2_block_map(&ip->i_inode, lblock, 0, &bh_map);
	if (error)
		return error;
	if (!bh_map.b_blocknr) {
		gfs2_consist_inode(ip);
		return -EIO;
	}

	bh = sb_getblk(sdp->sd_vfs, bh_map.b_blocknr);
	lock_buffer(bh);
	memset(bh->b_data, 0, bh->b_size);
	set_buffer_uptodate(bh);
	clear_buffer_dirty(bh);
	unlock_buffer(bh);

	lh = (struct gfs2_log_header *)bh->b_data;
	memset(lh, 0, sizeof(struct gfs2_log_header));
	lh->lh_header.mh_magic = cpu_to_be32(GFS2_MAGIC);
	lh->lh_header.mh_type = cpu_to_be32(GFS2_METATYPE_LH);
	lh->lh_header.mh_format = cpu_to_be32(GFS2_FORMAT_LH);
	lh->lh_sequence = cpu_to_be64(head->lh_sequence + 1);
	lh->lh_flags = cpu_to_be32(GFS2_LOG_HEAD_UNMOUNT);
	lh->lh_blkno = cpu_to_be32(lblock);
	hash = gfs2_disk_hash((const char *)lh, sizeof(struct gfs2_log_header));
	lh->lh_hash = cpu_to_be32(hash);

	set_buffer_dirty(bh);
	if (sync_dirty_buffer(bh))
		gfs2_io_error_bh(sdp, bh);
	brelse(bh);

	return error;
}

/**
 * gfs2_recover_journal - recovery a given journal
 * @jd: the struct gfs2_jdesc describing the journal
 *
 * Acquire the journal's lock, check to see if the journal is clean, and
 * do recovery if necessary.
 *
 * Returns: errno
 */

int gfs2_recover_journal(struct gfs2_jdesc *jd)
{
	struct gfs2_inode *ip = GFS2_I(jd->jd_inode);
	struct gfs2_sbd *sdp = GFS2_SB(jd->jd_inode);
	struct gfs2_log_header_host head;
	struct gfs2_holder j_gh, ji_gh, t_gh;
	unsigned long t;
	int ro = 0;
	unsigned int pass;
	int error;

	if (jd->jd_jid != sdp->sd_lockstruct.ls_jid) {
		fs_info(sdp, "jid=%u: Trying to acquire journal lock...\n",
			jd->jd_jid);

		/* Aquire the journal lock so we can do recovery */

		error = gfs2_glock_nq_num(sdp, jd->jd_jid, &gfs2_journal_glops,
					  LM_ST_EXCLUSIVE,
					  LM_FLAG_NOEXP | LM_FLAG_TRY | GL_NOCACHE,
					  &j_gh);
		switch (error) {
		case 0:
			break;

		case GLR_TRYFAILED:
			fs_info(sdp, "jid=%u: Busy\n", jd->jd_jid);
			error = 0;

		default:
			goto fail;
		};

		error = gfs2_glock_nq_init(ip->i_gl, LM_ST_SHARED,
					   LM_FLAG_NOEXP, &ji_gh);
		if (error)
			goto fail_gunlock_j;
	} else {
		fs_info(sdp, "jid=%u, already locked for use\n", jd->jd_jid);
	}

	fs_info(sdp, "jid=%u: Looking at journal...\n", jd->jd_jid);

	error = gfs2_jdesc_check(jd);
	if (error)
		goto fail_gunlock_ji;

	error = gfs2_find_jhead(jd, &head);
	if (error)
		goto fail_gunlock_ji;

	if (!(head.lh_flags & GFS2_LOG_HEAD_UNMOUNT)) {
		fs_info(sdp, "jid=%u: Acquiring the transaction lock...\n",
			jd->jd_jid);

		t = jiffies;

		/* Acquire a shared hold on the transaction lock */

		error = gfs2_glock_nq_init(sdp->sd_trans_gl, LM_ST_SHARED,
					   LM_FLAG_NOEXP | LM_FLAG_PRIORITY |
					   GL_NOCANCEL | GL_NOCACHE, &t_gh);
		if (error)
			goto fail_gunlock_ji;

		if (test_bit(SDF_JOURNAL_CHECKED, &sdp->sd_flags)) {
			if (!test_bit(SDF_JOURNAL_LIVE, &sdp->sd_flags))
				ro = 1;
		} else {
			if (sdp->sd_vfs->s_flags & MS_RDONLY)
				ro = 1;
		}

		if (ro) {
			fs_warn(sdp, "jid=%u: Can't replay: read-only FS\n",
				jd->jd_jid);
			error = -EROFS;
			goto fail_gunlock_tr;
		}

		fs_info(sdp, "jid=%u: Replaying journal...\n", jd->jd_jid);

		for (pass = 0; pass < 2; pass++) {
			lops_before_scan(jd, &head, pass);
			error = foreach_descriptor(jd, head.lh_tail,
						   head.lh_blkno, pass);
			lops_after_scan(jd, error, pass);
			if (error)
				goto fail_gunlock_tr;
		}

		error = clean_journal(jd, &head);
		if (error)
			goto fail_gunlock_tr;

		gfs2_glock_dq_uninit(&t_gh);
		t = DIV_ROUND_UP(jiffies - t, HZ);
		fs_info(sdp, "jid=%u: Journal replayed in %lus\n",
			jd->jd_jid, t);
	}

	if (jd->jd_jid != sdp->sd_lockstruct.ls_jid)
		gfs2_glock_dq_uninit(&ji_gh);

	gfs2_lm_recovery_done(sdp, jd->jd_jid, LM_RD_SUCCESS);

	if (jd->jd_jid != sdp->sd_lockstruct.ls_jid)
		gfs2_glock_dq_uninit(&j_gh);

	fs_info(sdp, "jid=%u: Done\n", jd->jd_jid);
	return 0;

fail_gunlock_tr:
	gfs2_glock_dq_uninit(&t_gh);
fail_gunlock_ji:
	if (jd->jd_jid != sdp->sd_lockstruct.ls_jid) {
		gfs2_glock_dq_uninit(&ji_gh);
fail_gunlock_j:
		gfs2_glock_dq_uninit(&j_gh);
	}

	fs_info(sdp, "jid=%u: %s\n", jd->jd_jid, (error) ? "Failed" : "Done");

fail:
	gfs2_lm_recovery_done(sdp, jd->jd_jid, LM_RD_GAVEUP);
	return error;
}

/**
 * gfs2_check_journals - Recover any dirty journals
 * @sdp: the filesystem
 *
 */

void gfs2_check_journals(struct gfs2_sbd *sdp)
{
	struct gfs2_jdesc *jd;

	for (;;) {
		jd = gfs2_jdesc_find_dirty(sdp);
		if (!jd)
			break;

		if (jd != sdp->sd_jdesc)
			gfs2_recover_journal(jd);
	}
}

