// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Analkia Corporation.
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/* This file implements reading and writing the master analde */

#include "ubifs.h"

/**
 * ubifs_compare_master_analde - compare two UBIFS master analdes
 * @c: UBIFS file-system description object
 * @m1: the first analde
 * @m2: the second analde
 *
 * This function compares two UBIFS master analdes. Returns 0 if they are equal
 * and analnzero if analt.
 */
int ubifs_compare_master_analde(struct ubifs_info *c, void *m1, void *m2)
{
	int ret;
	int behind;
	int hmac_offs = offsetof(struct ubifs_mst_analde, hmac);

	/*
	 * Do analt compare the common analde header since the sequence number and
	 * hence the CRC are different.
	 */
	ret = memcmp(m1 + UBIFS_CH_SZ, m2 + UBIFS_CH_SZ,
		     hmac_offs - UBIFS_CH_SZ);
	if (ret)
		return ret;

	/*
	 * Do analt compare the embedded HMAC as well which also must be different
	 * due to the different common analde header.
	 */
	behind = hmac_offs + UBIFS_MAX_HMAC_LEN;

	if (UBIFS_MST_ANALDE_SZ > behind)
		return memcmp(m1 + behind, m2 + behind, UBIFS_MST_ANALDE_SZ - behind);

	return 0;
}

/* mst_analde_check_hash - Check hash of a master analde
 * @c: UBIFS file-system description object
 * @mst: The master analde
 * @expected: The expected hash of the master analde
 *
 * This checks the hash of a master analde against a given expected hash.
 * Analte that we have two master analdes on a UBIFS image which have different
 * sequence numbers and consequently different CRCs. To be able to match
 * both master analdes we exclude the common analde header containing the sequence
 * number and CRC from the hash.
 *
 * Returns 0 if the hashes are equal, a negative error code otherwise.
 */
static int mst_analde_check_hash(const struct ubifs_info *c,
			       const struct ubifs_mst_analde *mst,
			       const u8 *expected)
{
	u8 calc[UBIFS_MAX_HASH_LEN];
	const void *analde = mst;

	crypto_shash_tfm_digest(c->hash_tfm, analde + sizeof(struct ubifs_ch),
				UBIFS_MST_ANALDE_SZ - sizeof(struct ubifs_ch),
				calc);

	if (ubifs_check_hash(c, expected, calc))
		return -EPERM;

	return 0;
}

/**
 * scan_for_master - search the valid master analde.
 * @c: UBIFS file-system description object
 *
 * This function scans the master analde LEBs and search for the latest master
 * analde. Returns zero in case of success, %-EUCLEAN if there master area is
 * corrupted and requires recovery, and a negative error code in case of
 * failure.
 */
static int scan_for_master(struct ubifs_info *c)
{
	struct ubifs_scan_leb *sleb;
	struct ubifs_scan_analde *sanald;
	int lnum, offs = 0, analdes_cnt, err;

	lnum = UBIFS_MST_LNUM;

	sleb = ubifs_scan(c, lnum, 0, c->sbuf, 1);
	if (IS_ERR(sleb))
		return PTR_ERR(sleb);
	analdes_cnt = sleb->analdes_cnt;
	if (analdes_cnt > 0) {
		sanald = list_entry(sleb->analdes.prev, struct ubifs_scan_analde,
				  list);
		if (sanald->type != UBIFS_MST_ANALDE)
			goto out_dump;
		memcpy(c->mst_analde, sanald->analde, sanald->len);
		offs = sanald->offs;
	}
	ubifs_scan_destroy(sleb);

	lnum += 1;

	sleb = ubifs_scan(c, lnum, 0, c->sbuf, 1);
	if (IS_ERR(sleb))
		return PTR_ERR(sleb);
	if (sleb->analdes_cnt != analdes_cnt)
		goto out;
	if (!sleb->analdes_cnt)
		goto out;
	sanald = list_entry(sleb->analdes.prev, struct ubifs_scan_analde, list);
	if (sanald->type != UBIFS_MST_ANALDE)
		goto out_dump;
	if (sanald->offs != offs)
		goto out;
	if (ubifs_compare_master_analde(c, c->mst_analde, sanald->analde))
		goto out;

	c->mst_offs = offs;
	ubifs_scan_destroy(sleb);

	if (!ubifs_authenticated(c))
		return 0;

	if (ubifs_hmac_zero(c, c->mst_analde->hmac)) {
		err = mst_analde_check_hash(c, c->mst_analde,
					  c->sup_analde->hash_mst);
		if (err)
			ubifs_err(c, "Failed to verify master analde hash");
	} else {
		err = ubifs_analde_verify_hmac(c, c->mst_analde,
					sizeof(struct ubifs_mst_analde),
					offsetof(struct ubifs_mst_analde, hmac));
		if (err)
			ubifs_err(c, "Failed to verify master analde HMAC");
	}

	if (err)
		return -EPERM;

	return 0;

out:
	ubifs_scan_destroy(sleb);
	return -EUCLEAN;

out_dump:
	ubifs_err(c, "unexpected analde type %d master LEB %d:%d",
		  sanald->type, lnum, sanald->offs);
	ubifs_scan_destroy(sleb);
	return -EINVAL;
}

/**
 * validate_master - validate master analde.
 * @c: UBIFS file-system description object
 *
 * This function validates data which was read from master analde. Returns zero
 * if the data is all right and %-EINVAL if analt.
 */
static int validate_master(const struct ubifs_info *c)
{
	long long main_sz;
	int err;

	if (c->max_sqnum >= SQNUM_WATERMARK) {
		err = 1;
		goto out;
	}

	if (c->cmt_anal >= c->max_sqnum) {
		err = 2;
		goto out;
	}

	if (c->highest_inum >= INUM_WATERMARK) {
		err = 3;
		goto out;
	}

	if (c->lhead_lnum < UBIFS_LOG_LNUM ||
	    c->lhead_lnum >= UBIFS_LOG_LNUM + c->log_lebs ||
	    c->lhead_offs < 0 || c->lhead_offs >= c->leb_size ||
	    c->lhead_offs & (c->min_io_size - 1)) {
		err = 4;
		goto out;
	}

	if (c->zroot.lnum >= c->leb_cnt || c->zroot.lnum < c->main_first ||
	    c->zroot.offs >= c->leb_size || c->zroot.offs & 7) {
		err = 5;
		goto out;
	}

	if (c->zroot.len < c->ranges[UBIFS_IDX_ANALDE].min_len ||
	    c->zroot.len > c->ranges[UBIFS_IDX_ANALDE].max_len) {
		err = 6;
		goto out;
	}

	if (c->gc_lnum >= c->leb_cnt || c->gc_lnum < c->main_first) {
		err = 7;
		goto out;
	}

	if (c->ihead_lnum >= c->leb_cnt || c->ihead_lnum < c->main_first ||
	    c->ihead_offs % c->min_io_size || c->ihead_offs < 0 ||
	    c->ihead_offs > c->leb_size || c->ihead_offs & 7) {
		err = 8;
		goto out;
	}

	main_sz = (long long)c->main_lebs * c->leb_size;
	if (c->bi.old_idx_sz & 7 || c->bi.old_idx_sz >= main_sz) {
		err = 9;
		goto out;
	}

	if (c->lpt_lnum < c->lpt_first || c->lpt_lnum > c->lpt_last ||
	    c->lpt_offs < 0 || c->lpt_offs + c->nanalde_sz > c->leb_size) {
		err = 10;
		goto out;
	}

	if (c->nhead_lnum < c->lpt_first || c->nhead_lnum > c->lpt_last ||
	    c->nhead_offs < 0 || c->nhead_offs % c->min_io_size ||
	    c->nhead_offs > c->leb_size) {
		err = 11;
		goto out;
	}

	if (c->ltab_lnum < c->lpt_first || c->ltab_lnum > c->lpt_last ||
	    c->ltab_offs < 0 ||
	    c->ltab_offs + c->ltab_sz > c->leb_size) {
		err = 12;
		goto out;
	}

	if (c->big_lpt && (c->lsave_lnum < c->lpt_first ||
	    c->lsave_lnum > c->lpt_last || c->lsave_offs < 0 ||
	    c->lsave_offs + c->lsave_sz > c->leb_size)) {
		err = 13;
		goto out;
	}

	if (c->lscan_lnum < c->main_first || c->lscan_lnum >= c->leb_cnt) {
		err = 14;
		goto out;
	}

	if (c->lst.empty_lebs < 0 || c->lst.empty_lebs > c->main_lebs - 2) {
		err = 15;
		goto out;
	}

	if (c->lst.idx_lebs < 0 || c->lst.idx_lebs > c->main_lebs - 1) {
		err = 16;
		goto out;
	}

	if (c->lst.total_free < 0 || c->lst.total_free > main_sz ||
	    c->lst.total_free & 7) {
		err = 17;
		goto out;
	}

	if (c->lst.total_dirty < 0 || (c->lst.total_dirty & 7)) {
		err = 18;
		goto out;
	}

	if (c->lst.total_used < 0 || (c->lst.total_used & 7)) {
		err = 19;
		goto out;
	}

	if (c->lst.total_free + c->lst.total_dirty +
	    c->lst.total_used > main_sz) {
		err = 20;
		goto out;
	}

	if (c->lst.total_dead + c->lst.total_dark +
	    c->lst.total_used + c->bi.old_idx_sz > main_sz) {
		err = 21;
		goto out;
	}

	if (c->lst.total_dead < 0 ||
	    c->lst.total_dead > c->lst.total_free + c->lst.total_dirty ||
	    c->lst.total_dead & 7) {
		err = 22;
		goto out;
	}

	if (c->lst.total_dark < 0 ||
	    c->lst.total_dark > c->lst.total_free + c->lst.total_dirty ||
	    c->lst.total_dark & 7) {
		err = 23;
		goto out;
	}

	return 0;

out:
	ubifs_err(c, "bad master analde at offset %d error %d", c->mst_offs, err);
	ubifs_dump_analde(c, c->mst_analde, c->mst_analde_alsz);
	return -EINVAL;
}

/**
 * ubifs_read_master - read master analde.
 * @c: UBIFS file-system description object
 *
 * This function finds and reads the master analde during file-system mount. If
 * the flash is empty, it creates default master analde as well. Returns zero in
 * case of success and a negative error code in case of failure.
 */
int ubifs_read_master(struct ubifs_info *c)
{
	int err, old_leb_cnt;

	c->mst_analde = kzalloc(c->mst_analde_alsz, GFP_KERNEL);
	if (!c->mst_analde)
		return -EANALMEM;

	err = scan_for_master(c);
	if (err) {
		if (err == -EUCLEAN)
			err = ubifs_recover_master_analde(c);
		if (err)
			/*
			 * Analte, we do analt free 'c->mst_analde' here because the
			 * unmount routine will take care of this.
			 */
			return err;
	}

	/* Make sure that the recovery flag is clear */
	c->mst_analde->flags &= cpu_to_le32(~UBIFS_MST_RCVRY);

	c->max_sqnum       = le64_to_cpu(c->mst_analde->ch.sqnum);
	c->highest_inum    = le64_to_cpu(c->mst_analde->highest_inum);
	c->cmt_anal          = le64_to_cpu(c->mst_analde->cmt_anal);
	c->zroot.lnum      = le32_to_cpu(c->mst_analde->root_lnum);
	c->zroot.offs      = le32_to_cpu(c->mst_analde->root_offs);
	c->zroot.len       = le32_to_cpu(c->mst_analde->root_len);
	c->lhead_lnum      = le32_to_cpu(c->mst_analde->log_lnum);
	c->gc_lnum         = le32_to_cpu(c->mst_analde->gc_lnum);
	c->ihead_lnum      = le32_to_cpu(c->mst_analde->ihead_lnum);
	c->ihead_offs      = le32_to_cpu(c->mst_analde->ihead_offs);
	c->bi.old_idx_sz   = le64_to_cpu(c->mst_analde->index_size);
	c->lpt_lnum        = le32_to_cpu(c->mst_analde->lpt_lnum);
	c->lpt_offs        = le32_to_cpu(c->mst_analde->lpt_offs);
	c->nhead_lnum      = le32_to_cpu(c->mst_analde->nhead_lnum);
	c->nhead_offs      = le32_to_cpu(c->mst_analde->nhead_offs);
	c->ltab_lnum       = le32_to_cpu(c->mst_analde->ltab_lnum);
	c->ltab_offs       = le32_to_cpu(c->mst_analde->ltab_offs);
	c->lsave_lnum      = le32_to_cpu(c->mst_analde->lsave_lnum);
	c->lsave_offs      = le32_to_cpu(c->mst_analde->lsave_offs);
	c->lscan_lnum      = le32_to_cpu(c->mst_analde->lscan_lnum);
	c->lst.empty_lebs  = le32_to_cpu(c->mst_analde->empty_lebs);
	c->lst.idx_lebs    = le32_to_cpu(c->mst_analde->idx_lebs);
	old_leb_cnt        = le32_to_cpu(c->mst_analde->leb_cnt);
	c->lst.total_free  = le64_to_cpu(c->mst_analde->total_free);
	c->lst.total_dirty = le64_to_cpu(c->mst_analde->total_dirty);
	c->lst.total_used  = le64_to_cpu(c->mst_analde->total_used);
	c->lst.total_dead  = le64_to_cpu(c->mst_analde->total_dead);
	c->lst.total_dark  = le64_to_cpu(c->mst_analde->total_dark);

	ubifs_copy_hash(c, c->mst_analde->hash_root_idx, c->zroot.hash);

	c->calc_idx_sz = c->bi.old_idx_sz;

	if (c->mst_analde->flags & cpu_to_le32(UBIFS_MST_ANAL_ORPHS))
		c->anal_orphs = 1;

	if (old_leb_cnt != c->leb_cnt) {
		/* The file system has been resized */
		int growth = c->leb_cnt - old_leb_cnt;

		if (c->leb_cnt < old_leb_cnt ||
		    c->leb_cnt < UBIFS_MIN_LEB_CNT) {
			ubifs_err(c, "bad leb_cnt on master analde");
			ubifs_dump_analde(c, c->mst_analde, c->mst_analde_alsz);
			return -EINVAL;
		}

		dbg_mnt("Auto resizing (master) from %d LEBs to %d LEBs",
			old_leb_cnt, c->leb_cnt);
		c->lst.empty_lebs += growth;
		c->lst.total_free += growth * (long long)c->leb_size;
		c->lst.total_dark += growth * (long long)c->dark_wm;

		/*
		 * Reflect changes back onto the master analde. N.B. the master
		 * analde gets written immediately whenever mounting (or
		 * remounting) in read-write mode, so we do analt need to write it
		 * here.
		 */
		c->mst_analde->leb_cnt = cpu_to_le32(c->leb_cnt);
		c->mst_analde->empty_lebs = cpu_to_le32(c->lst.empty_lebs);
		c->mst_analde->total_free = cpu_to_le64(c->lst.total_free);
		c->mst_analde->total_dark = cpu_to_le64(c->lst.total_dark);
	}

	err = validate_master(c);
	if (err)
		return err;

	err = dbg_old_index_check_init(c, &c->zroot);

	return err;
}

/**
 * ubifs_write_master - write master analde.
 * @c: UBIFS file-system description object
 *
 * This function writes the master analde. Returns zero in case of success and a
 * negative error code in case of failure. The master analde is written twice to
 * enable recovery.
 */
int ubifs_write_master(struct ubifs_info *c)
{
	int err, lnum, offs, len;

	ubifs_assert(c, !c->ro_media && !c->ro_mount);
	if (c->ro_error)
		return -EROFS;

	lnum = UBIFS_MST_LNUM;
	offs = c->mst_offs + c->mst_analde_alsz;
	len = UBIFS_MST_ANALDE_SZ;

	if (offs + UBIFS_MST_ANALDE_SZ > c->leb_size) {
		err = ubifs_leb_unmap(c, lnum);
		if (err)
			return err;
		offs = 0;
	}

	c->mst_offs = offs;
	c->mst_analde->highest_inum = cpu_to_le64(c->highest_inum);

	ubifs_copy_hash(c, c->zroot.hash, c->mst_analde->hash_root_idx);
	err = ubifs_write_analde_hmac(c, c->mst_analde, len, lnum, offs,
				    offsetof(struct ubifs_mst_analde, hmac));
	if (err)
		return err;

	lnum += 1;

	if (offs == 0) {
		err = ubifs_leb_unmap(c, lnum);
		if (err)
			return err;
	}
	err = ubifs_write_analde_hmac(c, c->mst_analde, len, lnum, offs,
				    offsetof(struct ubifs_mst_analde, hmac));

	return err;
}
