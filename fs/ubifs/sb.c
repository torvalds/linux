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
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file implements UBIFS superblock. The superblock is stored at the first
 * LEB of the volume and is never changed by UBIFS. Only user-space tools may
 * change it. The superblock node mostly contains geometry information.
 */

#include "ubifs.h"
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/math64.h>

/*
 * Default journal size in logical eraseblocks as a percent of total
 * flash size.
 */
#define DEFAULT_JNL_PERCENT 5

/* Default maximum journal size in bytes */
#define DEFAULT_MAX_JNL (32*1024*1024)

/* Default indexing tree fanout */
#define DEFAULT_FANOUT 8

/* Default number of data journal heads */
#define DEFAULT_JHEADS_CNT 1

/* Default positions of different LEBs in the main area */
#define DEFAULT_IDX_LEB  0
#define DEFAULT_DATA_LEB 1
#define DEFAULT_GC_LEB   2

/* Default number of LEB numbers in LPT's save table */
#define DEFAULT_LSAVE_CNT 256

/* Default reserved pool size as a percent of maximum free space */
#define DEFAULT_RP_PERCENT 5

/* The default maximum size of reserved pool in bytes */
#define DEFAULT_MAX_RP_SIZE (5*1024*1024)

/* Default time granularity in nanoseconds */
#define DEFAULT_TIME_GRAN 1000000000

/**
 * create_default_filesystem - format empty UBI volume.
 * @c: UBIFS file-system description object
 *
 * This function creates default empty file-system. Returns zero in case of
 * success and a negative error code in case of failure.
 */
static int create_default_filesystem(struct ubifs_info *c)
{
	struct ubifs_sb_node *sup;
	struct ubifs_mst_node *mst;
	struct ubifs_idx_node *idx;
	struct ubifs_branch *br;
	struct ubifs_ino_node *ino;
	struct ubifs_cs_node *cs;
	union ubifs_key key;
	int err, tmp, jnl_lebs, log_lebs, max_buds, main_lebs, main_first;
	int lpt_lebs, lpt_first, orph_lebs, big_lpt, ino_waste, sup_flags = 0;
	int min_leb_cnt = UBIFS_MIN_LEB_CNT;
	long long tmp64, main_bytes;
	__le64 tmp_le64;

	/* Some functions called from here depend on the @c->key_len filed */
	c->key_len = UBIFS_SK_LEN;

	/*
	 * First of all, we have to calculate default file-system geometry -
	 * log size, journal size, etc.
	 */
	if (c->leb_cnt < 0x7FFFFFFF / DEFAULT_JNL_PERCENT)
		/* We can first multiply then divide and have no overflow */
		jnl_lebs = c->leb_cnt * DEFAULT_JNL_PERCENT / 100;
	else
		jnl_lebs = (c->leb_cnt / 100) * DEFAULT_JNL_PERCENT;

	if (jnl_lebs < UBIFS_MIN_JNL_LEBS)
		jnl_lebs = UBIFS_MIN_JNL_LEBS;
	if (jnl_lebs * c->leb_size > DEFAULT_MAX_JNL)
		jnl_lebs = DEFAULT_MAX_JNL / c->leb_size;

	/*
	 * The log should be large enough to fit reference nodes for all bud
	 * LEBs. Because buds do not have to start from the beginning of LEBs
	 * (half of the LEB may contain committed data), the log should
	 * generally be larger, make it twice as large.
	 */
	tmp = 2 * (c->ref_node_alsz * jnl_lebs) + c->leb_size - 1;
	log_lebs = tmp / c->leb_size;
	/* Plus one LEB reserved for commit */
	log_lebs += 1;
	if (c->leb_cnt - min_leb_cnt > 8) {
		/* And some extra space to allow writes while committing */
		log_lebs += 1;
		min_leb_cnt += 1;
	}

	max_buds = jnl_lebs - log_lebs;
	if (max_buds < UBIFS_MIN_BUD_LEBS)
		max_buds = UBIFS_MIN_BUD_LEBS;

	/*
	 * Orphan nodes are stored in a separate area. One node can store a lot
	 * of orphan inode numbers, but when new orphan comes we just add a new
	 * orphan node. At some point the nodes are consolidated into one
	 * orphan node.
	 */
	orph_lebs = UBIFS_MIN_ORPH_LEBS;
#ifdef CONFIG_UBIFS_FS_DEBUG
	if (c->leb_cnt - min_leb_cnt > 1)
		/*
		 * For debugging purposes it is better to have at least 2
		 * orphan LEBs, because the orphan subsystem would need to do
		 * consolidations and would be stressed more.
		 */
		orph_lebs += 1;
#endif

	main_lebs = c->leb_cnt - UBIFS_SB_LEBS - UBIFS_MST_LEBS - log_lebs;
	main_lebs -= orph_lebs;

	lpt_first = UBIFS_LOG_LNUM + log_lebs;
	c->lsave_cnt = DEFAULT_LSAVE_CNT;
	c->max_leb_cnt = c->leb_cnt;
	err = ubifs_create_dflt_lpt(c, &main_lebs, lpt_first, &lpt_lebs,
				    &big_lpt);
	if (err)
		return err;

	dbg_gen("LEB Properties Tree created (LEBs %d-%d)", lpt_first,
		lpt_first + lpt_lebs - 1);

	main_first = c->leb_cnt - main_lebs;

	/* Create default superblock */
	tmp = ALIGN(UBIFS_SB_NODE_SZ, c->min_io_size);
	sup = kzalloc(tmp, GFP_KERNEL);
	if (!sup)
		return -ENOMEM;

	tmp64 = (long long)max_buds * c->leb_size;
	if (big_lpt)
		sup_flags |= UBIFS_FLG_BIGLPT;

	sup->ch.node_type  = UBIFS_SB_NODE;
	sup->key_hash      = UBIFS_KEY_HASH_R5;
	sup->flags         = cpu_to_le32(sup_flags);
	sup->min_io_size   = cpu_to_le32(c->min_io_size);
	sup->leb_size      = cpu_to_le32(c->leb_size);
	sup->leb_cnt       = cpu_to_le32(c->leb_cnt);
	sup->max_leb_cnt   = cpu_to_le32(c->max_leb_cnt);
	sup->max_bud_bytes = cpu_to_le64(tmp64);
	sup->log_lebs      = cpu_to_le32(log_lebs);
	sup->lpt_lebs      = cpu_to_le32(lpt_lebs);
	sup->orph_lebs     = cpu_to_le32(orph_lebs);
	sup->jhead_cnt     = cpu_to_le32(DEFAULT_JHEADS_CNT);
	sup->fanout        = cpu_to_le32(DEFAULT_FANOUT);
	sup->lsave_cnt     = cpu_to_le32(c->lsave_cnt);
	sup->fmt_version   = cpu_to_le32(UBIFS_FORMAT_VERSION);
	sup->time_gran     = cpu_to_le32(DEFAULT_TIME_GRAN);
	if (c->mount_opts.override_compr)
		sup->default_compr = cpu_to_le16(c->mount_opts.compr_type);
	else
		sup->default_compr = cpu_to_le16(UBIFS_COMPR_LZO);

	generate_random_uuid(sup->uuid);

	main_bytes = (long long)main_lebs * c->leb_size;
	tmp64 = div_u64(main_bytes * DEFAULT_RP_PERCENT, 100);
	if (tmp64 > DEFAULT_MAX_RP_SIZE)
		tmp64 = DEFAULT_MAX_RP_SIZE;
	sup->rp_size = cpu_to_le64(tmp64);
	sup->ro_compat_version = cpu_to_le32(UBIFS_RO_COMPAT_VERSION);

	err = ubifs_write_node(c, sup, UBIFS_SB_NODE_SZ, 0, 0, UBI_LONGTERM);
	kfree(sup);
	if (err)
		return err;

	dbg_gen("default superblock created at LEB 0:0");

	/* Create default master node */
	mst = kzalloc(c->mst_node_alsz, GFP_KERNEL);
	if (!mst)
		return -ENOMEM;

	mst->ch.node_type = UBIFS_MST_NODE;
	mst->log_lnum     = cpu_to_le32(UBIFS_LOG_LNUM);
	mst->highest_inum = cpu_to_le64(UBIFS_FIRST_INO);
	mst->cmt_no       = 0;
	mst->root_lnum    = cpu_to_le32(main_first + DEFAULT_IDX_LEB);
	mst->root_offs    = 0;
	tmp = ubifs_idx_node_sz(c, 1);
	mst->root_len     = cpu_to_le32(tmp);
	mst->gc_lnum      = cpu_to_le32(main_first + DEFAULT_GC_LEB);
	mst->ihead_lnum   = cpu_to_le32(main_first + DEFAULT_IDX_LEB);
	mst->ihead_offs   = cpu_to_le32(ALIGN(tmp, c->min_io_size));
	mst->index_size   = cpu_to_le64(ALIGN(tmp, 8));
	mst->lpt_lnum     = cpu_to_le32(c->lpt_lnum);
	mst->lpt_offs     = cpu_to_le32(c->lpt_offs);
	mst->nhead_lnum   = cpu_to_le32(c->nhead_lnum);
	mst->nhead_offs   = cpu_to_le32(c->nhead_offs);
	mst->ltab_lnum    = cpu_to_le32(c->ltab_lnum);
	mst->ltab_offs    = cpu_to_le32(c->ltab_offs);
	mst->lsave_lnum   = cpu_to_le32(c->lsave_lnum);
	mst->lsave_offs   = cpu_to_le32(c->lsave_offs);
	mst->lscan_lnum   = cpu_to_le32(main_first);
	mst->empty_lebs   = cpu_to_le32(main_lebs - 2);
	mst->idx_lebs     = cpu_to_le32(1);
	mst->leb_cnt      = cpu_to_le32(c->leb_cnt);

	/* Calculate lprops statistics */
	tmp64 = main_bytes;
	tmp64 -= ALIGN(ubifs_idx_node_sz(c, 1), c->min_io_size);
	tmp64 -= ALIGN(UBIFS_INO_NODE_SZ, c->min_io_size);
	mst->total_free = cpu_to_le64(tmp64);

	tmp64 = ALIGN(ubifs_idx_node_sz(c, 1), c->min_io_size);
	ino_waste = ALIGN(UBIFS_INO_NODE_SZ, c->min_io_size) -
			  UBIFS_INO_NODE_SZ;
	tmp64 += ino_waste;
	tmp64 -= ALIGN(ubifs_idx_node_sz(c, 1), 8);
	mst->total_dirty = cpu_to_le64(tmp64);

	/*  The indexing LEB does not contribute to dark space */
	tmp64 = ((long long)(c->main_lebs - 1) * c->dark_wm);
	mst->total_dark = cpu_to_le64(tmp64);

	mst->total_used = cpu_to_le64(UBIFS_INO_NODE_SZ);

	err = ubifs_write_node(c, mst, UBIFS_MST_NODE_SZ, UBIFS_MST_LNUM, 0,
			       UBI_UNKNOWN);
	if (err) {
		kfree(mst);
		return err;
	}
	err = ubifs_write_node(c, mst, UBIFS_MST_NODE_SZ, UBIFS_MST_LNUM + 1, 0,
			       UBI_UNKNOWN);
	kfree(mst);
	if (err)
		return err;

	dbg_gen("default master node created at LEB %d:0", UBIFS_MST_LNUM);

	/* Create the root indexing node */
	tmp = ubifs_idx_node_sz(c, 1);
	idx = kzalloc(ALIGN(tmp, c->min_io_size), GFP_KERNEL);
	if (!idx)
		return -ENOMEM;

	c->key_fmt = UBIFS_SIMPLE_KEY_FMT;
	c->key_hash = key_r5_hash;

	idx->ch.node_type = UBIFS_IDX_NODE;
	idx->child_cnt = cpu_to_le16(1);
	ino_key_init(c, &key, UBIFS_ROOT_INO);
	br = ubifs_idx_branch(c, idx, 0);
	key_write_idx(c, &key, &br->key);
	br->lnum = cpu_to_le32(main_first + DEFAULT_DATA_LEB);
	br->len  = cpu_to_le32(UBIFS_INO_NODE_SZ);
	err = ubifs_write_node(c, idx, tmp, main_first + DEFAULT_IDX_LEB, 0,
			       UBI_UNKNOWN);
	kfree(idx);
	if (err)
		return err;

	dbg_gen("default root indexing node created LEB %d:0",
		main_first + DEFAULT_IDX_LEB);

	/* Create default root inode */
	tmp = ALIGN(UBIFS_INO_NODE_SZ, c->min_io_size);
	ino = kzalloc(tmp, GFP_KERNEL);
	if (!ino)
		return -ENOMEM;

	ino_key_init_flash(c, &ino->key, UBIFS_ROOT_INO);
	ino->ch.node_type = UBIFS_INO_NODE;
	ino->creat_sqnum = cpu_to_le64(++c->max_sqnum);
	ino->nlink = cpu_to_le32(2);
	tmp_le64 = cpu_to_le64(CURRENT_TIME_SEC.tv_sec);
	ino->atime_sec   = tmp_le64;
	ino->ctime_sec   = tmp_le64;
	ino->mtime_sec   = tmp_le64;
	ino->atime_nsec  = 0;
	ino->ctime_nsec  = 0;
	ino->mtime_nsec  = 0;
	ino->mode = cpu_to_le32(S_IFDIR | S_IRUGO | S_IWUSR | S_IXUGO);
	ino->size = cpu_to_le64(UBIFS_INO_NODE_SZ);

	/* Set compression enabled by default */
	ino->flags = cpu_to_le32(UBIFS_COMPR_FL);

	err = ubifs_write_node(c, ino, UBIFS_INO_NODE_SZ,
			       main_first + DEFAULT_DATA_LEB, 0,
			       UBI_UNKNOWN);
	kfree(ino);
	if (err)
		return err;

	dbg_gen("root inode created at LEB %d:0",
		main_first + DEFAULT_DATA_LEB);

	/*
	 * The first node in the log has to be the commit start node. This is
	 * always the case during normal file-system operation. Write a fake
	 * commit start node to the log.
	 */
	tmp = ALIGN(UBIFS_CS_NODE_SZ, c->min_io_size);
	cs = kzalloc(tmp, GFP_KERNEL);
	if (!cs)
		return -ENOMEM;

	cs->ch.node_type = UBIFS_CS_NODE;
	err = ubifs_write_node(c, cs, UBIFS_CS_NODE_SZ, UBIFS_LOG_LNUM,
			       0, UBI_UNKNOWN);
	kfree(cs);

	ubifs_msg("default file-system created");
	return 0;
}

/**
 * validate_sb - validate superblock node.
 * @c: UBIFS file-system description object
 * @sup: superblock node
 *
 * This function validates superblock node @sup. Since most of data was read
 * from the superblock and stored in @c, the function validates fields in @c
 * instead. Returns zero in case of success and %-EINVAL in case of validation
 * failure.
 */
static int validate_sb(struct ubifs_info *c, struct ubifs_sb_node *sup)
{
	long long max_bytes;
	int err = 1, min_leb_cnt;

	if (!c->key_hash) {
		err = 2;
		goto failed;
	}

	if (sup->key_fmt != UBIFS_SIMPLE_KEY_FMT) {
		err = 3;
		goto failed;
	}

	if (le32_to_cpu(sup->min_io_size) != c->min_io_size) {
		ubifs_err("min. I/O unit mismatch: %d in superblock, %d real",
			  le32_to_cpu(sup->min_io_size), c->min_io_size);
		goto failed;
	}

	if (le32_to_cpu(sup->leb_size) != c->leb_size) {
		ubifs_err("LEB size mismatch: %d in superblock, %d real",
			  le32_to_cpu(sup->leb_size), c->leb_size);
		goto failed;
	}

	if (c->log_lebs < UBIFS_MIN_LOG_LEBS ||
	    c->lpt_lebs < UBIFS_MIN_LPT_LEBS ||
	    c->orph_lebs < UBIFS_MIN_ORPH_LEBS ||
	    c->main_lebs < UBIFS_MIN_MAIN_LEBS) {
		err = 4;
		goto failed;
	}

	/*
	 * Calculate minimum allowed amount of main area LEBs. This is very
	 * similar to %UBIFS_MIN_LEB_CNT, but we take into account real what we
	 * have just read from the superblock.
	 */
	min_leb_cnt = UBIFS_SB_LEBS + UBIFS_MST_LEBS + c->log_lebs;
	min_leb_cnt += c->lpt_lebs + c->orph_lebs + c->jhead_cnt + 6;

	if (c->leb_cnt < min_leb_cnt || c->leb_cnt > c->vi.size) {
		ubifs_err("bad LEB count: %d in superblock, %d on UBI volume, "
			  "%d minimum required", c->leb_cnt, c->vi.size,
			  min_leb_cnt);
		goto failed;
	}

	if (c->max_leb_cnt < c->leb_cnt) {
		ubifs_err("max. LEB count %d less than LEB count %d",
			  c->max_leb_cnt, c->leb_cnt);
		goto failed;
	}

	if (c->main_lebs < UBIFS_MIN_MAIN_LEBS) {
		ubifs_err("too few main LEBs count %d, must be at least %d",
			  c->main_lebs, UBIFS_MIN_MAIN_LEBS);
		goto failed;
	}

	max_bytes = (long long)c->leb_size * UBIFS_MIN_BUD_LEBS;
	if (c->max_bud_bytes < max_bytes) {
		ubifs_err("too small journal (%lld bytes), must be at least "
			  "%lld bytes",  c->max_bud_bytes, max_bytes);
		goto failed;
	}

	max_bytes = (long long)c->leb_size * c->main_lebs;
	if (c->max_bud_bytes > max_bytes) {
		ubifs_err("too large journal size (%lld bytes), only %lld bytes"
			  "available in the main area",
			  c->max_bud_bytes, max_bytes);
		goto failed;
	}

	if (c->jhead_cnt < NONDATA_JHEADS_CNT + 1 ||
	    c->jhead_cnt > NONDATA_JHEADS_CNT + UBIFS_MAX_JHEADS) {
		err = 9;
		goto failed;
	}

	if (c->fanout < UBIFS_MIN_FANOUT ||
	    ubifs_idx_node_sz(c, c->fanout) > c->leb_size) {
		err = 10;
		goto failed;
	}

	if (c->lsave_cnt < 0 || (c->lsave_cnt > DEFAULT_LSAVE_CNT &&
	    c->lsave_cnt > c->max_leb_cnt - UBIFS_SB_LEBS - UBIFS_MST_LEBS -
	    c->log_lebs - c->lpt_lebs - c->orph_lebs)) {
		err = 11;
		goto failed;
	}

	if (UBIFS_SB_LEBS + UBIFS_MST_LEBS + c->log_lebs + c->lpt_lebs +
	    c->orph_lebs + c->main_lebs != c->leb_cnt) {
		err = 12;
		goto failed;
	}

	if (c->default_compr < 0 || c->default_compr >= UBIFS_COMPR_TYPES_CNT) {
		err = 13;
		goto failed;
	}

	if (c->rp_size < 0 || max_bytes < c->rp_size) {
		err = 14;
		goto failed;
	}

	if (le32_to_cpu(sup->time_gran) > 1000000000 ||
	    le32_to_cpu(sup->time_gran) < 1) {
		err = 15;
		goto failed;
	}

	return 0;

failed:
	ubifs_err("bad superblock, error %d", err);
	dbg_dump_node(c, sup);
	return -EINVAL;
}

/**
 * ubifs_read_sb_node - read superblock node.
 * @c: UBIFS file-system description object
 *
 * This function returns a pointer to the superblock node or a negative error
 * code. Note, the user of this function is responsible of kfree()'ing the
 * returned superblock buffer.
 */
struct ubifs_sb_node *ubifs_read_sb_node(struct ubifs_info *c)
{
	struct ubifs_sb_node *sup;
	int err;

	sup = kmalloc(ALIGN(UBIFS_SB_NODE_SZ, c->min_io_size), GFP_NOFS);
	if (!sup)
		return ERR_PTR(-ENOMEM);

	err = ubifs_read_node(c, sup, UBIFS_SB_NODE, UBIFS_SB_NODE_SZ,
			      UBIFS_SB_LNUM, 0);
	if (err) {
		kfree(sup);
		return ERR_PTR(err);
	}

	return sup;
}

/**
 * ubifs_write_sb_node - write superblock node.
 * @c: UBIFS file-system description object
 * @sup: superblock node read with 'ubifs_read_sb_node()'
 *
 * This function returns %0 on success and a negative error code on failure.
 */
int ubifs_write_sb_node(struct ubifs_info *c, struct ubifs_sb_node *sup)
{
	int len = ALIGN(UBIFS_SB_NODE_SZ, c->min_io_size);

	ubifs_prepare_node(c, sup, UBIFS_SB_NODE_SZ, 1);
	return ubifs_leb_change(c, UBIFS_SB_LNUM, sup, len, UBI_LONGTERM);
}

/**
 * ubifs_read_superblock - read superblock.
 * @c: UBIFS file-system description object
 *
 * This function finds, reads and checks the superblock. If an empty UBI volume
 * is being mounted, this function creates default superblock. Returns zero in
 * case of success, and a negative error code in case of failure.
 */
int ubifs_read_superblock(struct ubifs_info *c)
{
	int err, sup_flags;
	struct ubifs_sb_node *sup;

	if (c->empty) {
		err = create_default_filesystem(c);
		if (err)
			return err;
	}

	sup = ubifs_read_sb_node(c);
	if (IS_ERR(sup))
		return PTR_ERR(sup);

	c->fmt_version = le32_to_cpu(sup->fmt_version);
	c->ro_compat_version = le32_to_cpu(sup->ro_compat_version);

	/*
	 * The software supports all previous versions but not future versions,
	 * due to the unavailability of time-travelling equipment.
	 */
	if (c->fmt_version > UBIFS_FORMAT_VERSION) {
		ubifs_assert(!c->ro_media || c->ro_mount);
		if (!c->ro_mount ||
		    c->ro_compat_version > UBIFS_RO_COMPAT_VERSION) {
			ubifs_err("on-flash format version is w%d/r%d, but "
				  "software only supports up to version "
				  "w%d/r%d", c->fmt_version,
				  c->ro_compat_version, UBIFS_FORMAT_VERSION,
				  UBIFS_RO_COMPAT_VERSION);
			if (c->ro_compat_version <= UBIFS_RO_COMPAT_VERSION) {
				ubifs_msg("only R/O mounting is possible");
				err = -EROFS;
			} else
				err = -EINVAL;
			goto out;
		}

		/*
		 * The FS is mounted R/O, and the media format is
		 * R/O-compatible with the UBIFS implementation, so we can
		 * mount.
		 */
		c->rw_incompat = 1;
	}

	if (c->fmt_version < 3) {
		ubifs_err("on-flash format version %d is not supported",
			  c->fmt_version);
		err = -EINVAL;
		goto out;
	}

	switch (sup->key_hash) {
	case UBIFS_KEY_HASH_R5:
		c->key_hash = key_r5_hash;
		c->key_hash_type = UBIFS_KEY_HASH_R5;
		break;

	case UBIFS_KEY_HASH_TEST:
		c->key_hash = key_test_hash;
		c->key_hash_type = UBIFS_KEY_HASH_TEST;
		break;
	};

	c->key_fmt = sup->key_fmt;

	switch (c->key_fmt) {
	case UBIFS_SIMPLE_KEY_FMT:
		c->key_len = UBIFS_SK_LEN;
		break;
	default:
		ubifs_err("unsupported key format");
		err = -EINVAL;
		goto out;
	}

	c->leb_cnt       = le32_to_cpu(sup->leb_cnt);
	c->max_leb_cnt   = le32_to_cpu(sup->max_leb_cnt);
	c->max_bud_bytes = le64_to_cpu(sup->max_bud_bytes);
	c->log_lebs      = le32_to_cpu(sup->log_lebs);
	c->lpt_lebs      = le32_to_cpu(sup->lpt_lebs);
	c->orph_lebs     = le32_to_cpu(sup->orph_lebs);
	c->jhead_cnt     = le32_to_cpu(sup->jhead_cnt) + NONDATA_JHEADS_CNT;
	c->fanout        = le32_to_cpu(sup->fanout);
	c->lsave_cnt     = le32_to_cpu(sup->lsave_cnt);
	c->rp_size       = le64_to_cpu(sup->rp_size);
	c->rp_uid        = le32_to_cpu(sup->rp_uid);
	c->rp_gid        = le32_to_cpu(sup->rp_gid);
	sup_flags        = le32_to_cpu(sup->flags);
	if (!c->mount_opts.override_compr)
		c->default_compr = le16_to_cpu(sup->default_compr);

	c->vfs_sb->s_time_gran = le32_to_cpu(sup->time_gran);
	memcpy(&c->uuid, &sup->uuid, 16);
	c->big_lpt = !!(sup_flags & UBIFS_FLG_BIGLPT);
	c->space_fixup = !!(sup_flags & UBIFS_FLG_SPACE_FIXUP);

	/* Automatically increase file system size to the maximum size */
	c->old_leb_cnt = c->leb_cnt;
	if (c->leb_cnt < c->vi.size && c->leb_cnt < c->max_leb_cnt) {
		c->leb_cnt = min_t(int, c->max_leb_cnt, c->vi.size);
		if (c->ro_mount)
			dbg_mnt("Auto resizing (ro) from %d LEBs to %d LEBs",
				c->old_leb_cnt,	c->leb_cnt);
		else {
			dbg_mnt("Auto resizing (sb) from %d LEBs to %d LEBs",
				c->old_leb_cnt, c->leb_cnt);
			sup->leb_cnt = cpu_to_le32(c->leb_cnt);
			err = ubifs_write_sb_node(c, sup);
			if (err)
				goto out;
			c->old_leb_cnt = c->leb_cnt;
		}
	}

	c->log_bytes = (long long)c->log_lebs * c->leb_size;
	c->log_last = UBIFS_LOG_LNUM + c->log_lebs - 1;
	c->lpt_first = UBIFS_LOG_LNUM + c->log_lebs;
	c->lpt_last = c->lpt_first + c->lpt_lebs - 1;
	c->orph_first = c->lpt_last + 1;
	c->orph_last = c->orph_first + c->orph_lebs - 1;
	c->main_lebs = c->leb_cnt - UBIFS_SB_LEBS - UBIFS_MST_LEBS;
	c->main_lebs -= c->log_lebs + c->lpt_lebs + c->orph_lebs;
	c->main_first = c->leb_cnt - c->main_lebs;

	err = validate_sb(c, sup);
out:
	kfree(sup);
	return err;
}

/**
 * fixup_leb - fixup/unmap an LEB containing free space.
 * @c: UBIFS file-system description object
 * @lnum: the LEB number to fix up
 * @len: number of used bytes in LEB (starting at offset 0)
 *
 * This function reads the contents of the given LEB number @lnum, then fixes
 * it up, so that empty min. I/O units in the end of LEB are actually erased on
 * flash (rather than being just all-0xff real data). If the LEB is completely
 * empty, it is simply unmapped.
 */
static int fixup_leb(struct ubifs_info *c, int lnum, int len)
{
	int err;

	ubifs_assert(len >= 0);
	ubifs_assert(len % c->min_io_size == 0);
	ubifs_assert(len < c->leb_size);

	if (len == 0) {
		dbg_mnt("unmap empty LEB %d", lnum);
		return ubifs_leb_unmap(c, lnum);
	}

	dbg_mnt("fixup LEB %d, data len %d", lnum, len);
	err = ubifs_leb_read(c, lnum, c->sbuf, 0, len, 1);
	if (err)
		return err;

	return ubifs_leb_change(c, lnum, c->sbuf, len, UBI_UNKNOWN);
}

/**
 * fixup_free_space - find & remap all LEBs containing free space.
 * @c: UBIFS file-system description object
 *
 * This function walks through all LEBs in the filesystem and fiexes up those
 * containing free/empty space.
 */
static int fixup_free_space(struct ubifs_info *c)
{
	int lnum, err = 0;
	struct ubifs_lprops *lprops;

	ubifs_get_lprops(c);

	/* Fixup LEBs in the master area */
	for (lnum = UBIFS_MST_LNUM; lnum < UBIFS_LOG_LNUM; lnum++) {
		err = fixup_leb(c, lnum, c->mst_offs + c->mst_node_alsz);
		if (err)
			goto out;
	}

	/* Unmap unused log LEBs */
	lnum = ubifs_next_log_lnum(c, c->lhead_lnum);
	while (lnum != c->ltail_lnum) {
		err = fixup_leb(c, lnum, 0);
		if (err)
			goto out;
		lnum = ubifs_next_log_lnum(c, lnum);
	}

	/*
	 * Fixup the log head which contains the only a CS node at the
	 * beginning.
	 */
	err = fixup_leb(c, c->lhead_lnum,
			ALIGN(UBIFS_CS_NODE_SZ, c->min_io_size));
	if (err)
		goto out;

	/* Fixup LEBs in the LPT area */
	for (lnum = c->lpt_first; lnum <= c->lpt_last; lnum++) {
		int free = c->ltab[lnum - c->lpt_first].free;

		if (free > 0) {
			err = fixup_leb(c, lnum, c->leb_size - free);
			if (err)
				goto out;
		}
	}

	/* Unmap LEBs in the orphans area */
	for (lnum = c->orph_first; lnum <= c->orph_last; lnum++) {
		err = fixup_leb(c, lnum, 0);
		if (err)
			goto out;
	}

	/* Fixup LEBs in the main area */
	for (lnum = c->main_first; lnum < c->leb_cnt; lnum++) {
		lprops = ubifs_lpt_lookup(c, lnum);
		if (IS_ERR(lprops)) {
			err = PTR_ERR(lprops);
			goto out;
		}

		if (lprops->free > 0) {
			err = fixup_leb(c, lnum, c->leb_size - lprops->free);
			if (err)
				goto out;
		}
	}

out:
	ubifs_release_lprops(c);
	return err;
}

/**
 * ubifs_fixup_free_space - find & fix all LEBs with free space.
 * @c: UBIFS file-system description object
 *
 * This function fixes up LEBs containing free space on first mount, if the
 * appropriate flag was set when the FS was created. Each LEB with one or more
 * empty min. I/O unit (i.e. free-space-count > 0) is re-written, to make sure
 * the free space is actually erased. E.g., this is necessary for some NAND
 * chips, since the free space may have been programmed like real "0xff" data
 * (generating a non-0xff ECC), causing future writes to the not-really-erased
 * NAND pages to behave badly. After the space is fixed up, the superblock flag
 * is cleared, so that this is skipped for all future mounts.
 */
int ubifs_fixup_free_space(struct ubifs_info *c)
{
	int err;
	struct ubifs_sb_node *sup;

	ubifs_assert(c->space_fixup);
	ubifs_assert(!c->ro_mount);

	ubifs_msg("start fixing up free space");

	err = fixup_free_space(c);
	if (err)
		return err;

	sup = ubifs_read_sb_node(c);
	if (IS_ERR(sup))
		return PTR_ERR(sup);

	/* Free-space fixup is no longer required */
	c->space_fixup = 0;
	sup->flags &= cpu_to_le32(~UBIFS_FLG_SPACE_FIXUP);

	err = ubifs_write_sb_node(c, sup);
	kfree(sup);
	if (err)
		return err;

	ubifs_msg("free space fixup complete");
	return err;
}
