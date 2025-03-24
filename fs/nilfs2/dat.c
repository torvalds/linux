// SPDX-License-Identifier: GPL-2.0+
/*
 * NILFS disk address translation.
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Koji Sato.
 */

#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/string.h>
#include <linux/errno.h>
#include "nilfs.h"
#include "mdt.h"
#include "alloc.h"
#include "dat.h"


#define NILFS_CNO_MIN	((__u64)1)
#define NILFS_CNO_MAX	(~(__u64)0)

/**
 * struct nilfs_dat_info - on-memory private data of DAT file
 * @mi: on-memory private data of metadata file
 * @palloc_cache: persistent object allocator cache of DAT file
 * @shadow: shadow map of DAT file
 */
struct nilfs_dat_info {
	struct nilfs_mdt_info mi;
	struct nilfs_palloc_cache palloc_cache;
	struct nilfs_shadow_map shadow;
};

static inline struct nilfs_dat_info *NILFS_DAT_I(struct inode *dat)
{
	return (struct nilfs_dat_info *)NILFS_MDT(dat);
}

static int nilfs_dat_prepare_entry(struct inode *dat,
				   struct nilfs_palloc_req *req, int create)
{
	int ret;

	ret = nilfs_palloc_get_entry_block(dat, req->pr_entry_nr,
					   create, &req->pr_entry_bh);
	if (unlikely(ret == -ENOENT)) {
		nilfs_err(dat->i_sb,
			  "DAT doesn't have a block to manage vblocknr = %llu",
			  (unsigned long long)req->pr_entry_nr);
		/*
		 * Return internal code -EINVAL to notify bmap layer of
		 * metadata corruption.
		 */
		ret = -EINVAL;
	}
	return ret;
}

static void nilfs_dat_commit_entry(struct inode *dat,
				   struct nilfs_palloc_req *req)
{
	mark_buffer_dirty(req->pr_entry_bh);
	nilfs_mdt_mark_dirty(dat);
	brelse(req->pr_entry_bh);
}

static void nilfs_dat_abort_entry(struct inode *dat,
				  struct nilfs_palloc_req *req)
{
	brelse(req->pr_entry_bh);
}

int nilfs_dat_prepare_alloc(struct inode *dat, struct nilfs_palloc_req *req)
{
	int ret;

	ret = nilfs_palloc_prepare_alloc_entry(dat, req, true);
	if (ret < 0)
		return ret;

	ret = nilfs_dat_prepare_entry(dat, req, 1);
	if (ret < 0)
		nilfs_palloc_abort_alloc_entry(dat, req);

	return ret;
}

void nilfs_dat_commit_alloc(struct inode *dat, struct nilfs_palloc_req *req)
{
	struct nilfs_dat_entry *entry;
	size_t offset;

	offset = nilfs_palloc_entry_offset(dat, req->pr_entry_nr,
					   req->pr_entry_bh);
	entry = kmap_local_folio(req->pr_entry_bh->b_folio, offset);
	entry->de_start = cpu_to_le64(NILFS_CNO_MIN);
	entry->de_end = cpu_to_le64(NILFS_CNO_MAX);
	entry->de_blocknr = cpu_to_le64(0);
	kunmap_local(entry);

	nilfs_palloc_commit_alloc_entry(dat, req);
	nilfs_dat_commit_entry(dat, req);
}

void nilfs_dat_abort_alloc(struct inode *dat, struct nilfs_palloc_req *req)
{
	nilfs_dat_abort_entry(dat, req);
	nilfs_palloc_abort_alloc_entry(dat, req);
}

static void nilfs_dat_commit_free(struct inode *dat,
				  struct nilfs_palloc_req *req)
{
	struct nilfs_dat_entry *entry;
	size_t offset;

	offset = nilfs_palloc_entry_offset(dat, req->pr_entry_nr,
					   req->pr_entry_bh);
	entry = kmap_local_folio(req->pr_entry_bh->b_folio, offset);
	entry->de_start = cpu_to_le64(NILFS_CNO_MIN);
	entry->de_end = cpu_to_le64(NILFS_CNO_MIN);
	entry->de_blocknr = cpu_to_le64(0);
	kunmap_local(entry);

	nilfs_dat_commit_entry(dat, req);

	if (unlikely(req->pr_desc_bh == NULL || req->pr_bitmap_bh == NULL)) {
		nilfs_error(dat->i_sb,
			    "state inconsistency probably due to duplicate use of vblocknr = %llu",
			    (unsigned long long)req->pr_entry_nr);
		return;
	}
	nilfs_palloc_commit_free_entry(dat, req);
}

int nilfs_dat_prepare_start(struct inode *dat, struct nilfs_palloc_req *req)
{
	return nilfs_dat_prepare_entry(dat, req, 0);
}

void nilfs_dat_commit_start(struct inode *dat, struct nilfs_palloc_req *req,
			    sector_t blocknr)
{
	struct nilfs_dat_entry *entry;
	size_t offset;

	offset = nilfs_palloc_entry_offset(dat, req->pr_entry_nr,
					   req->pr_entry_bh);
	entry = kmap_local_folio(req->pr_entry_bh->b_folio, offset);
	entry->de_start = cpu_to_le64(nilfs_mdt_cno(dat));
	entry->de_blocknr = cpu_to_le64(blocknr);
	kunmap_local(entry);

	nilfs_dat_commit_entry(dat, req);
}

int nilfs_dat_prepare_end(struct inode *dat, struct nilfs_palloc_req *req)
{
	struct nilfs_dat_entry *entry;
	__u64 start;
	sector_t blocknr;
	size_t offset;
	int ret;

	ret = nilfs_dat_prepare_entry(dat, req, 0);
	if (ret < 0)
		return ret;

	offset = nilfs_palloc_entry_offset(dat, req->pr_entry_nr,
					   req->pr_entry_bh);
	entry = kmap_local_folio(req->pr_entry_bh->b_folio, offset);
	start = le64_to_cpu(entry->de_start);
	blocknr = le64_to_cpu(entry->de_blocknr);
	kunmap_local(entry);

	if (blocknr == 0) {
		ret = nilfs_palloc_prepare_free_entry(dat, req);
		if (ret < 0) {
			nilfs_dat_abort_entry(dat, req);
			return ret;
		}
	}
	if (unlikely(start > nilfs_mdt_cno(dat))) {
		nilfs_err(dat->i_sb,
			  "vblocknr = %llu has abnormal lifetime: start cno (= %llu) > current cno (= %llu)",
			  (unsigned long long)req->pr_entry_nr,
			  (unsigned long long)start,
			  (unsigned long long)nilfs_mdt_cno(dat));
		nilfs_dat_abort_entry(dat, req);
		return -EINVAL;
	}

	return 0;
}

void nilfs_dat_commit_end(struct inode *dat, struct nilfs_palloc_req *req,
			  int dead)
{
	struct nilfs_dat_entry *entry;
	__u64 start, end;
	sector_t blocknr;
	size_t offset;

	offset = nilfs_palloc_entry_offset(dat, req->pr_entry_nr,
					   req->pr_entry_bh);
	entry = kmap_local_folio(req->pr_entry_bh->b_folio, offset);
	end = start = le64_to_cpu(entry->de_start);
	if (!dead) {
		end = nilfs_mdt_cno(dat);
		WARN_ON(start > end);
	}
	entry->de_end = cpu_to_le64(end);
	blocknr = le64_to_cpu(entry->de_blocknr);
	kunmap_local(entry);

	if (blocknr == 0)
		nilfs_dat_commit_free(dat, req);
	else
		nilfs_dat_commit_entry(dat, req);
}

void nilfs_dat_abort_end(struct inode *dat, struct nilfs_palloc_req *req)
{
	struct nilfs_dat_entry *entry;
	__u64 start;
	sector_t blocknr;
	size_t offset;

	offset = nilfs_palloc_entry_offset(dat, req->pr_entry_nr,
					   req->pr_entry_bh);
	entry = kmap_local_folio(req->pr_entry_bh->b_folio, offset);
	start = le64_to_cpu(entry->de_start);
	blocknr = le64_to_cpu(entry->de_blocknr);
	kunmap_local(entry);

	if (start == nilfs_mdt_cno(dat) && blocknr == 0)
		nilfs_palloc_abort_free_entry(dat, req);
	nilfs_dat_abort_entry(dat, req);
}

int nilfs_dat_prepare_update(struct inode *dat,
			     struct nilfs_palloc_req *oldreq,
			     struct nilfs_palloc_req *newreq)
{
	int ret;

	ret = nilfs_dat_prepare_end(dat, oldreq);
	if (!ret) {
		ret = nilfs_dat_prepare_alloc(dat, newreq);
		if (ret < 0)
			nilfs_dat_abort_end(dat, oldreq);
	}
	return ret;
}

void nilfs_dat_commit_update(struct inode *dat,
			     struct nilfs_palloc_req *oldreq,
			     struct nilfs_palloc_req *newreq, int dead)
{
	nilfs_dat_commit_end(dat, oldreq, dead);
	nilfs_dat_commit_alloc(dat, newreq);
}

void nilfs_dat_abort_update(struct inode *dat,
			    struct nilfs_palloc_req *oldreq,
			    struct nilfs_palloc_req *newreq)
{
	nilfs_dat_abort_end(dat, oldreq);
	nilfs_dat_abort_alloc(dat, newreq);
}

/**
 * nilfs_dat_mark_dirty - mark the DAT block buffer containing the specified
 *                        virtual block address entry as dirty
 * @dat:      DAT file inode
 * @vblocknr: virtual block number
 *
 * Return: 0 on success, or one of the following negative error codes on
 * failure:
 * * %-EINVAL	- Invalid DAT entry (internal code).
 * * %-EIO	- I/O error (including metadata corruption).
 * * %-ENOMEM	- Insufficient memory available.
 */
int nilfs_dat_mark_dirty(struct inode *dat, __u64 vblocknr)
{
	struct nilfs_palloc_req req;
	int ret;

	req.pr_entry_nr = vblocknr;
	ret = nilfs_dat_prepare_entry(dat, &req, 0);
	if (ret == 0)
		nilfs_dat_commit_entry(dat, &req);
	return ret;
}

/**
 * nilfs_dat_freev - free virtual block numbers
 * @dat: DAT file inode
 * @vblocknrs: array of virtual block numbers
 * @nitems: number of virtual block numbers
 *
 * Description: nilfs_dat_freev() frees the virtual block numbers specified by
 * @vblocknrs and @nitems.
 *
 * Return: 0 on success, or one of the following negative error codes on
 * failure:
 * * %-EIO	- I/O error (including metadata corruption).
 * * %-ENOENT	- The virtual block number have not been allocated.
 * * %-ENOMEM	- Insufficient memory available.
 */
int nilfs_dat_freev(struct inode *dat, __u64 *vblocknrs, size_t nitems)
{
	return nilfs_palloc_freev(dat, vblocknrs, nitems);
}

/**
 * nilfs_dat_move - change a block number
 * @dat: DAT file inode
 * @vblocknr: virtual block number
 * @blocknr: block number
 *
 * Description: nilfs_dat_move() changes the block number associated with
 * @vblocknr to @blocknr.
 *
 * Return: 0 on success, or one of the following negative error codes on
 * failure:
 * * %-EIO	- I/O error (including metadata corruption).
 * * %-ENOMEM	- Insufficient memory available.
 */
int nilfs_dat_move(struct inode *dat, __u64 vblocknr, sector_t blocknr)
{
	struct buffer_head *entry_bh;
	struct nilfs_dat_entry *entry;
	size_t offset;
	int ret;

	ret = nilfs_palloc_get_entry_block(dat, vblocknr, 0, &entry_bh);
	if (ret < 0)
		return ret;

	/*
	 * The given disk block number (blocknr) is not yet written to
	 * the device at this point.
	 *
	 * To prevent nilfs_dat_translate() from returning the
	 * uncommitted block number, this makes a copy of the entry
	 * buffer and redirects nilfs_dat_translate() to the copy.
	 */
	if (!buffer_nilfs_redirected(entry_bh)) {
		ret = nilfs_mdt_freeze_buffer(dat, entry_bh);
		if (ret) {
			brelse(entry_bh);
			return ret;
		}
	}

	offset = nilfs_palloc_entry_offset(dat, vblocknr, entry_bh);
	entry = kmap_local_folio(entry_bh->b_folio, offset);
	if (unlikely(entry->de_blocknr == cpu_to_le64(0))) {
		nilfs_crit(dat->i_sb,
			   "%s: invalid vblocknr = %llu, [%llu, %llu)",
			   __func__, (unsigned long long)vblocknr,
			   (unsigned long long)le64_to_cpu(entry->de_start),
			   (unsigned long long)le64_to_cpu(entry->de_end));
		kunmap_local(entry);
		brelse(entry_bh);
		return -EINVAL;
	}
	WARN_ON(blocknr == 0);
	entry->de_blocknr = cpu_to_le64(blocknr);
	kunmap_local(entry);

	mark_buffer_dirty(entry_bh);
	nilfs_mdt_mark_dirty(dat);

	brelse(entry_bh);

	return 0;
}

/**
 * nilfs_dat_translate - translate a virtual block number to a block number
 * @dat: DAT file inode
 * @vblocknr: virtual block number
 * @blocknrp: pointer to a block number
 *
 * Description: nilfs_dat_translate() maps the virtual block number @vblocknr
 * to the corresponding block number.  The block number associated with
 * @vblocknr is stored in the place pointed to by @blocknrp.
 *
 * Return: 0 on success, or one of the following negative error codes on
 * failure:
 * * %-EIO	- I/O error (including metadata corruption).
 * * %-ENOENT	- A block number associated with @vblocknr does not exist.
 * * %-ENOMEM	- Insufficient memory available.
 */
int nilfs_dat_translate(struct inode *dat, __u64 vblocknr, sector_t *blocknrp)
{
	struct buffer_head *entry_bh, *bh;
	struct nilfs_dat_entry *entry;
	sector_t blocknr;
	size_t offset;
	int ret;

	ret = nilfs_palloc_get_entry_block(dat, vblocknr, 0, &entry_bh);
	if (ret < 0)
		return ret;

	if (!nilfs_doing_gc() && buffer_nilfs_redirected(entry_bh)) {
		bh = nilfs_mdt_get_frozen_buffer(dat, entry_bh);
		if (bh) {
			WARN_ON(!buffer_uptodate(bh));
			brelse(entry_bh);
			entry_bh = bh;
		}
	}

	offset = nilfs_palloc_entry_offset(dat, vblocknr, entry_bh);
	entry = kmap_local_folio(entry_bh->b_folio, offset);
	blocknr = le64_to_cpu(entry->de_blocknr);
	if (blocknr == 0) {
		ret = -ENOENT;
		goto out;
	}
	*blocknrp = blocknr;

 out:
	kunmap_local(entry);
	brelse(entry_bh);
	return ret;
}

ssize_t nilfs_dat_get_vinfo(struct inode *dat, void *buf, unsigned int visz,
			    size_t nvi)
{
	struct buffer_head *entry_bh;
	struct nilfs_dat_entry *entry, *first_entry;
	struct nilfs_vinfo *vinfo = buf;
	__u64 first, last;
	size_t offset;
	unsigned long entries_per_block = NILFS_MDT(dat)->mi_entries_per_block;
	unsigned int entry_size = NILFS_MDT(dat)->mi_entry_size;
	int i, j, n, ret;

	for (i = 0; i < nvi; i += n) {
		ret = nilfs_palloc_get_entry_block(dat, vinfo->vi_vblocknr,
						   0, &entry_bh);
		if (ret < 0)
			return ret;

		first = vinfo->vi_vblocknr;
		first = div64_ul(first, entries_per_block);
		first *= entries_per_block;
		/* first virtual block number in this block */

		last = first + entries_per_block - 1;
		/* last virtual block number in this block */

		offset = nilfs_palloc_entry_offset(dat, first, entry_bh);
		first_entry = kmap_local_folio(entry_bh->b_folio, offset);
		for (j = i, n = 0;
		     j < nvi && vinfo->vi_vblocknr >= first &&
			     vinfo->vi_vblocknr <= last;
		     j++, n++, vinfo = (void *)vinfo + visz) {
			entry = (void *)first_entry +
				(vinfo->vi_vblocknr - first) * entry_size;
			vinfo->vi_start = le64_to_cpu(entry->de_start);
			vinfo->vi_end = le64_to_cpu(entry->de_end);
			vinfo->vi_blocknr = le64_to_cpu(entry->de_blocknr);
		}
		kunmap_local(first_entry);
		brelse(entry_bh);
	}

	return nvi;
}

/**
 * nilfs_dat_read - read or get dat inode
 * @sb: super block instance
 * @entry_size: size of a dat entry
 * @raw_inode: on-disk dat inode
 * @inodep: buffer to store the inode
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int nilfs_dat_read(struct super_block *sb, size_t entry_size,
		   struct nilfs_inode *raw_inode, struct inode **inodep)
{
	static struct lock_class_key dat_lock_key;
	struct inode *dat;
	struct nilfs_dat_info *di;
	int err;

	if (entry_size > sb->s_blocksize) {
		nilfs_err(sb, "too large DAT entry size: %zu bytes",
			  entry_size);
		return -EINVAL;
	} else if (entry_size < NILFS_MIN_DAT_ENTRY_SIZE) {
		nilfs_err(sb, "too small DAT entry size: %zu bytes",
			  entry_size);
		return -EINVAL;
	}

	dat = nilfs_iget_locked(sb, NULL, NILFS_DAT_INO);
	if (unlikely(!dat))
		return -ENOMEM;
	if (!(dat->i_state & I_NEW))
		goto out;

	err = nilfs_mdt_init(dat, NILFS_MDT_GFP, sizeof(*di));
	if (err)
		goto failed;

	err = nilfs_palloc_init_blockgroup(dat, entry_size);
	if (err)
		goto failed;

	di = NILFS_DAT_I(dat);
	lockdep_set_class(&di->mi.mi_sem, &dat_lock_key);
	nilfs_palloc_setup_cache(dat, &di->palloc_cache);
	err = nilfs_mdt_setup_shadow_map(dat, &di->shadow);
	if (err)
		goto failed;

	err = nilfs_read_inode_common(dat, raw_inode);
	if (err)
		goto failed;

	unlock_new_inode(dat);
 out:
	*inodep = dat;
	return 0;
 failed:
	iget_failed(dat);
	return err;
}
