/*
 * dat.c - NILFS disk address translation.
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Koji Sato <koji@osrg.net>.
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

static int nilfs_dat_prepare_entry(struct inode *dat,
				   struct nilfs_palloc_req *req, int create)
{
	return nilfs_palloc_get_entry_block(dat, req->pr_entry_nr,
					    create, &req->pr_entry_bh);
}

static void nilfs_dat_commit_entry(struct inode *dat,
				   struct nilfs_palloc_req *req)
{
	nilfs_mdt_mark_buffer_dirty(req->pr_entry_bh);
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

	ret = nilfs_palloc_prepare_alloc_entry(dat, req);
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
	void *kaddr;

	kaddr = kmap_atomic(req->pr_entry_bh->b_page, KM_USER0);
	entry = nilfs_palloc_block_get_entry(dat, req->pr_entry_nr,
					     req->pr_entry_bh, kaddr);
	entry->de_start = cpu_to_le64(NILFS_CNO_MIN);
	entry->de_end = cpu_to_le64(NILFS_CNO_MAX);
	entry->de_blocknr = cpu_to_le64(0);
	kunmap_atomic(kaddr, KM_USER0);

	nilfs_palloc_commit_alloc_entry(dat, req);
	nilfs_dat_commit_entry(dat, req);
}

void nilfs_dat_abort_alloc(struct inode *dat, struct nilfs_palloc_req *req)
{
	nilfs_dat_abort_entry(dat, req);
	nilfs_palloc_abort_alloc_entry(dat, req);
}

int nilfs_dat_prepare_free(struct inode *dat, struct nilfs_palloc_req *req)
{
	int ret;

	ret = nilfs_palloc_prepare_free_entry(dat, req);
	if (ret < 0)
		return ret;
	ret = nilfs_dat_prepare_entry(dat, req, 0);
	if (ret < 0) {
		nilfs_palloc_abort_free_entry(dat, req);
		return ret;
	}
	return 0;
}

void nilfs_dat_commit_free(struct inode *dat, struct nilfs_palloc_req *req)
{
	struct nilfs_dat_entry *entry;
	void *kaddr;

	kaddr = kmap_atomic(req->pr_entry_bh->b_page, KM_USER0);
	entry = nilfs_palloc_block_get_entry(dat, req->pr_entry_nr,
					     req->pr_entry_bh, kaddr);
	entry->de_start = cpu_to_le64(NILFS_CNO_MIN);
	entry->de_end = cpu_to_le64(NILFS_CNO_MIN);
	entry->de_blocknr = cpu_to_le64(0);
	kunmap_atomic(kaddr, KM_USER0);

	nilfs_dat_commit_entry(dat, req);
	nilfs_palloc_commit_free_entry(dat, req);
}

void nilfs_dat_abort_free(struct inode *dat, struct nilfs_palloc_req *req)
{
	nilfs_dat_abort_entry(dat, req);
	nilfs_palloc_abort_free_entry(dat, req);
}

int nilfs_dat_prepare_start(struct inode *dat, struct nilfs_palloc_req *req)
{
	int ret;

	ret = nilfs_dat_prepare_entry(dat, req, 0);
	BUG_ON(ret == -ENOENT);
	return ret;
}

void nilfs_dat_commit_start(struct inode *dat, struct nilfs_palloc_req *req,
			    sector_t blocknr)
{
	struct nilfs_dat_entry *entry;
	void *kaddr;

	kaddr = kmap_atomic(req->pr_entry_bh->b_page, KM_USER0);
	entry = nilfs_palloc_block_get_entry(dat, req->pr_entry_nr,
					     req->pr_entry_bh, kaddr);
	entry->de_start = cpu_to_le64(nilfs_mdt_cno(dat));
	if (entry->de_blocknr != cpu_to_le64(0) ||
	    entry->de_end != cpu_to_le64(NILFS_CNO_MAX)) {
		printk(KERN_CRIT
		       "%s: vbn = %llu, start = %llu, end = %llu, pbn = %llu\n",
		       __func__, (unsigned long long)req->pr_entry_nr,
		       (unsigned long long)le64_to_cpu(entry->de_start),
		       (unsigned long long)le64_to_cpu(entry->de_end),
		       (unsigned long long)le64_to_cpu(entry->de_blocknr));
		BUG();
	}
	entry->de_blocknr = cpu_to_le64(blocknr);
	kunmap_atomic(kaddr, KM_USER0);

	nilfs_dat_commit_entry(dat, req);
}

void nilfs_dat_abort_start(struct inode *dat, struct nilfs_palloc_req *req)
{
	nilfs_dat_abort_entry(dat, req);
}

int nilfs_dat_prepare_end(struct inode *dat, struct nilfs_palloc_req *req)
{
	struct nilfs_dat_entry *entry;
	__u64 start;
	sector_t blocknr;
	void *kaddr;
	int ret;

	ret = nilfs_dat_prepare_entry(dat, req, 0);
	if (ret < 0) {
		BUG_ON(ret == -ENOENT);
		return ret;
	}

	kaddr = kmap_atomic(req->pr_entry_bh->b_page, KM_USER0);
	entry = nilfs_palloc_block_get_entry(dat, req->pr_entry_nr,
					     req->pr_entry_bh, kaddr);
	start = le64_to_cpu(entry->de_start);
	blocknr = le64_to_cpu(entry->de_blocknr);
	kunmap_atomic(kaddr, KM_USER0);

	if (blocknr == 0) {
		ret = nilfs_palloc_prepare_free_entry(dat, req);
		if (ret < 0) {
			nilfs_dat_abort_entry(dat, req);
			return ret;
		}
	}

	return 0;
}

void nilfs_dat_commit_end(struct inode *dat, struct nilfs_palloc_req *req,
			  int dead)
{
	struct nilfs_dat_entry *entry;
	__u64 start, end;
	sector_t blocknr;
	void *kaddr;

	kaddr = kmap_atomic(req->pr_entry_bh->b_page, KM_USER0);
	entry = nilfs_palloc_block_get_entry(dat, req->pr_entry_nr,
					     req->pr_entry_bh, kaddr);
	end = start = le64_to_cpu(entry->de_start);
	if (!dead) {
		end = nilfs_mdt_cno(dat);
		BUG_ON(start > end);
	}
	entry->de_end = cpu_to_le64(end);
	blocknr = le64_to_cpu(entry->de_blocknr);
	kunmap_atomic(kaddr, KM_USER0);

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
	void *kaddr;

	kaddr = kmap_atomic(req->pr_entry_bh->b_page, KM_USER0);
	entry = nilfs_palloc_block_get_entry(dat, req->pr_entry_nr,
					     req->pr_entry_bh, kaddr);
	start = le64_to_cpu(entry->de_start);
	blocknr = le64_to_cpu(entry->de_blocknr);
	kunmap_atomic(kaddr, KM_USER0);

	if (start == nilfs_mdt_cno(dat) && blocknr == 0)
		nilfs_palloc_abort_free_entry(dat, req);
	nilfs_dat_abort_entry(dat, req);
}

/**
 * nilfs_dat_mark_dirty -
 * @dat: DAT file inode
 * @vblocknr: virtual block number
 *
 * Description:
 *
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
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
 * Return Value: On success, 0 is returned. On error, one of the following
 * nagative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - The virtual block number have not been allocated.
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
 * Return Value: On success, 0 is returned. On error, one of the following
 * negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 */
int nilfs_dat_move(struct inode *dat, __u64 vblocknr, sector_t blocknr)
{
	struct buffer_head *entry_bh;
	struct nilfs_dat_entry *entry;
	void *kaddr;
	int ret;

	ret = nilfs_palloc_get_entry_block(dat, vblocknr, 0, &entry_bh);
	if (ret < 0)
		return ret;
	kaddr = kmap_atomic(entry_bh->b_page, KM_USER0);
	entry = nilfs_palloc_block_get_entry(dat, vblocknr, entry_bh, kaddr);
	if (entry->de_blocknr == cpu_to_le64(0)) {
		printk(KERN_CRIT "%s: vbn = %llu, [%llu, %llu)\n", __func__,
		       (unsigned long long)vblocknr,
		       (unsigned long long)le64_to_cpu(entry->de_start),
		       (unsigned long long)le64_to_cpu(entry->de_end));
		BUG();
	}
	BUG_ON(blocknr == 0);
	entry->de_blocknr = cpu_to_le64(blocknr);
	kunmap_atomic(kaddr, KM_USER0);

	nilfs_mdt_mark_buffer_dirty(entry_bh);
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
 * to the corresponding block number.
 *
 * Return Value: On success, 0 is returned and the block number associated
 * with @vblocknr is stored in the place pointed by @blocknrp. On error, one
 * of the following negative error codes is returned.
 *
 * %-EIO - I/O error.
 *
 * %-ENOMEM - Insufficient amount of memory available.
 *
 * %-ENOENT - A block number associated with @vblocknr does not exist.
 */
int nilfs_dat_translate(struct inode *dat, __u64 vblocknr, sector_t *blocknrp)
{
	struct buffer_head *entry_bh;
	struct nilfs_dat_entry *entry;
	sector_t blocknr;
	void *kaddr;
	int ret;

	ret = nilfs_palloc_get_entry_block(dat, vblocknr, 0, &entry_bh);
	if (ret < 0)
		return ret;

	kaddr = kmap_atomic(entry_bh->b_page, KM_USER0);
	entry = nilfs_palloc_block_get_entry(dat, vblocknr, entry_bh, kaddr);
	blocknr = le64_to_cpu(entry->de_blocknr);
	if (blocknr == 0) {
		ret = -ENOENT;
		goto out;
	}
	if (blocknrp != NULL)
		*blocknrp = blocknr;

 out:
	kunmap_atomic(kaddr, KM_USER0);
	brelse(entry_bh);
	return ret;
}

ssize_t nilfs_dat_get_vinfo(struct inode *dat, struct nilfs_vinfo *vinfo,
			    size_t nvi)
{
	struct buffer_head *entry_bh;
	struct nilfs_dat_entry *entry;
	__u64 first, last;
	void *kaddr;
	unsigned long entries_per_block = NILFS_MDT(dat)->mi_entries_per_block;
	int i, j, n, ret;

	for (i = 0; i < nvi; i += n) {
		ret = nilfs_palloc_get_entry_block(dat, vinfo[i].vi_vblocknr,
						   0, &entry_bh);
		if (ret < 0)
			return ret;
		kaddr = kmap_atomic(entry_bh->b_page, KM_USER0);
		/* last virtual block number in this block */
		first = vinfo[i].vi_vblocknr;
		do_div(first, entries_per_block);
		first *= entries_per_block;
		last = first + entries_per_block - 1;
		for (j = i, n = 0;
		     j < nvi && vinfo[j].vi_vblocknr >= first &&
			     vinfo[j].vi_vblocknr <= last;
		     j++, n++) {
			entry = nilfs_palloc_block_get_entry(
				dat, vinfo[j].vi_vblocknr, entry_bh, kaddr);
			vinfo[j].vi_start = le64_to_cpu(entry->de_start);
			vinfo[j].vi_end = le64_to_cpu(entry->de_end);
			vinfo[j].vi_blocknr = le64_to_cpu(entry->de_blocknr);
		}
		kunmap_atomic(kaddr, KM_USER0);
		brelse(entry_bh);
	}

	return nvi;
}
