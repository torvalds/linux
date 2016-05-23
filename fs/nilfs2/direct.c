/*
 * direct.c - NILFS direct block pointer.
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
 * Written by Koji Sato <koji@osrg.net>.
 */

#include <linux/errno.h>
#include "nilfs.h"
#include "page.h"
#include "direct.h"
#include "alloc.h"
#include "dat.h"

static inline __le64 *nilfs_direct_dptrs(const struct nilfs_bmap *direct)
{
	return (__le64 *)
		((struct nilfs_direct_node *)direct->b_u.u_data + 1);
}

static inline __u64
nilfs_direct_get_ptr(const struct nilfs_bmap *direct, __u64 key)
{
	return le64_to_cpu(*(nilfs_direct_dptrs(direct) + key));
}

static inline void nilfs_direct_set_ptr(struct nilfs_bmap *direct,
					__u64 key, __u64 ptr)
{
	*(nilfs_direct_dptrs(direct) + key) = cpu_to_le64(ptr);
}

static int nilfs_direct_lookup(const struct nilfs_bmap *direct,
			       __u64 key, int level, __u64 *ptrp)
{
	__u64 ptr;

	if (key > NILFS_DIRECT_KEY_MAX || level != 1)
		return -ENOENT;
	ptr = nilfs_direct_get_ptr(direct, key);
	if (ptr == NILFS_BMAP_INVALID_PTR)
		return -ENOENT;

	*ptrp = ptr;
	return 0;
}

static int nilfs_direct_lookup_contig(const struct nilfs_bmap *direct,
				      __u64 key, __u64 *ptrp,
				      unsigned maxblocks)
{
	struct inode *dat = NULL;
	__u64 ptr, ptr2;
	sector_t blocknr;
	int ret, cnt;

	if (key > NILFS_DIRECT_KEY_MAX)
		return -ENOENT;
	ptr = nilfs_direct_get_ptr(direct, key);
	if (ptr == NILFS_BMAP_INVALID_PTR)
		return -ENOENT;

	if (NILFS_BMAP_USE_VBN(direct)) {
		dat = nilfs_bmap_get_dat(direct);
		ret = nilfs_dat_translate(dat, ptr, &blocknr);
		if (ret < 0)
			return ret;
		ptr = blocknr;
	}

	maxblocks = min_t(unsigned, maxblocks, NILFS_DIRECT_KEY_MAX - key + 1);
	for (cnt = 1; cnt < maxblocks &&
		     (ptr2 = nilfs_direct_get_ptr(direct, key + cnt)) !=
		     NILFS_BMAP_INVALID_PTR;
	     cnt++) {
		if (dat) {
			ret = nilfs_dat_translate(dat, ptr2, &blocknr);
			if (ret < 0)
				return ret;
			ptr2 = blocknr;
		}
		if (ptr2 != ptr + cnt)
			break;
	}
	*ptrp = ptr;
	return cnt;
}

static __u64
nilfs_direct_find_target_v(const struct nilfs_bmap *direct, __u64 key)
{
	__u64 ptr;

	ptr = nilfs_bmap_find_target_seq(direct, key);
	if (ptr != NILFS_BMAP_INVALID_PTR)
		/* sequential access */
		return ptr;
	else
		/* block group */
		return nilfs_bmap_find_target_in_group(direct);
}

static int nilfs_direct_insert(struct nilfs_bmap *bmap, __u64 key, __u64 ptr)
{
	union nilfs_bmap_ptr_req req;
	struct inode *dat = NULL;
	struct buffer_head *bh;
	int ret;

	if (key > NILFS_DIRECT_KEY_MAX)
		return -ENOENT;
	if (nilfs_direct_get_ptr(bmap, key) != NILFS_BMAP_INVALID_PTR)
		return -EEXIST;

	if (NILFS_BMAP_USE_VBN(bmap)) {
		req.bpr_ptr = nilfs_direct_find_target_v(bmap, key);
		dat = nilfs_bmap_get_dat(bmap);
	}
	ret = nilfs_bmap_prepare_alloc_ptr(bmap, &req, dat);
	if (!ret) {
		/* ptr must be a pointer to a buffer head. */
		bh = (struct buffer_head *)((unsigned long)ptr);
		set_buffer_nilfs_volatile(bh);

		nilfs_bmap_commit_alloc_ptr(bmap, &req, dat);
		nilfs_direct_set_ptr(bmap, key, req.bpr_ptr);

		if (!nilfs_bmap_dirty(bmap))
			nilfs_bmap_set_dirty(bmap);

		if (NILFS_BMAP_USE_VBN(bmap))
			nilfs_bmap_set_target_v(bmap, key, req.bpr_ptr);

		nilfs_inode_add_blocks(bmap->b_inode, 1);
	}
	return ret;
}

static int nilfs_direct_delete(struct nilfs_bmap *bmap, __u64 key)
{
	union nilfs_bmap_ptr_req req;
	struct inode *dat;
	int ret;

	if (key > NILFS_DIRECT_KEY_MAX ||
	    nilfs_direct_get_ptr(bmap, key) == NILFS_BMAP_INVALID_PTR)
		return -ENOENT;

	dat = NILFS_BMAP_USE_VBN(bmap) ? nilfs_bmap_get_dat(bmap) : NULL;
	req.bpr_ptr = nilfs_direct_get_ptr(bmap, key);

	ret = nilfs_bmap_prepare_end_ptr(bmap, &req, dat);
	if (!ret) {
		nilfs_bmap_commit_end_ptr(bmap, &req, dat);
		nilfs_direct_set_ptr(bmap, key, NILFS_BMAP_INVALID_PTR);
		nilfs_inode_sub_blocks(bmap->b_inode, 1);
	}
	return ret;
}

static int nilfs_direct_seek_key(const struct nilfs_bmap *direct, __u64 start,
				 __u64 *keyp)
{
	__u64 key;

	for (key = start; key <= NILFS_DIRECT_KEY_MAX; key++) {
		if (nilfs_direct_get_ptr(direct, key) !=
		    NILFS_BMAP_INVALID_PTR) {
			*keyp = key;
			return 0;
		}
	}
	return -ENOENT;
}

static int nilfs_direct_last_key(const struct nilfs_bmap *direct, __u64 *keyp)
{
	__u64 key, lastkey;

	lastkey = NILFS_DIRECT_KEY_MAX + 1;
	for (key = NILFS_DIRECT_KEY_MIN; key <= NILFS_DIRECT_KEY_MAX; key++)
		if (nilfs_direct_get_ptr(direct, key) !=
		    NILFS_BMAP_INVALID_PTR)
			lastkey = key;

	if (lastkey == NILFS_DIRECT_KEY_MAX + 1)
		return -ENOENT;

	*keyp = lastkey;

	return 0;
}

static int nilfs_direct_check_insert(const struct nilfs_bmap *bmap, __u64 key)
{
	return key > NILFS_DIRECT_KEY_MAX;
}

static int nilfs_direct_gather_data(struct nilfs_bmap *direct,
				    __u64 *keys, __u64 *ptrs, int nitems)
{
	__u64 key;
	__u64 ptr;
	int n;

	if (nitems > NILFS_DIRECT_NBLOCKS)
		nitems = NILFS_DIRECT_NBLOCKS;
	n = 0;
	for (key = 0; key < nitems; key++) {
		ptr = nilfs_direct_get_ptr(direct, key);
		if (ptr != NILFS_BMAP_INVALID_PTR) {
			keys[n] = key;
			ptrs[n] = ptr;
			n++;
		}
	}
	return n;
}

int nilfs_direct_delete_and_convert(struct nilfs_bmap *bmap,
				    __u64 key, __u64 *keys, __u64 *ptrs, int n)
{
	__le64 *dptrs;
	int ret, i, j;

	/* no need to allocate any resource for conversion */

	/* delete */
	ret = bmap->b_ops->bop_delete(bmap, key);
	if (ret < 0)
		return ret;

	/* free resources */
	if (bmap->b_ops->bop_clear != NULL)
		bmap->b_ops->bop_clear(bmap);

	/* convert */
	dptrs = nilfs_direct_dptrs(bmap);
	for (i = 0, j = 0; i < NILFS_DIRECT_NBLOCKS; i++) {
		if ((j < n) && (i == keys[j])) {
			dptrs[i] = (i != key) ?
				cpu_to_le64(ptrs[j]) :
				NILFS_BMAP_INVALID_PTR;
			j++;
		} else
			dptrs[i] = NILFS_BMAP_INVALID_PTR;
	}

	nilfs_direct_init(bmap);
	return 0;
}

static int nilfs_direct_propagate(struct nilfs_bmap *bmap,
				  struct buffer_head *bh)
{
	struct nilfs_palloc_req oldreq, newreq;
	struct inode *dat;
	__u64 key;
	__u64 ptr;
	int ret;

	if (!NILFS_BMAP_USE_VBN(bmap))
		return 0;

	dat = nilfs_bmap_get_dat(bmap);
	key = nilfs_bmap_data_get_key(bmap, bh);
	ptr = nilfs_direct_get_ptr(bmap, key);
	if (!buffer_nilfs_volatile(bh)) {
		oldreq.pr_entry_nr = ptr;
		newreq.pr_entry_nr = ptr;
		ret = nilfs_dat_prepare_update(dat, &oldreq, &newreq);
		if (ret < 0)
			return ret;
		nilfs_dat_commit_update(dat, &oldreq, &newreq,
					bmap->b_ptr_type == NILFS_BMAP_PTR_VS);
		set_buffer_nilfs_volatile(bh);
		nilfs_direct_set_ptr(bmap, key, newreq.pr_entry_nr);
	} else
		ret = nilfs_dat_mark_dirty(dat, ptr);

	return ret;
}

static int nilfs_direct_assign_v(struct nilfs_bmap *direct,
				 __u64 key, __u64 ptr,
				 struct buffer_head **bh,
				 sector_t blocknr,
				 union nilfs_binfo *binfo)
{
	struct inode *dat = nilfs_bmap_get_dat(direct);
	union nilfs_bmap_ptr_req req;
	int ret;

	req.bpr_ptr = ptr;
	ret = nilfs_dat_prepare_start(dat, &req.bpr_req);
	if (!ret) {
		nilfs_dat_commit_start(dat, &req.bpr_req, blocknr);
		binfo->bi_v.bi_vblocknr = cpu_to_le64(ptr);
		binfo->bi_v.bi_blkoff = cpu_to_le64(key);
	}
	return ret;
}

static int nilfs_direct_assign_p(struct nilfs_bmap *direct,
				 __u64 key, __u64 ptr,
				 struct buffer_head **bh,
				 sector_t blocknr,
				 union nilfs_binfo *binfo)
{
	nilfs_direct_set_ptr(direct, key, blocknr);

	binfo->bi_dat.bi_blkoff = cpu_to_le64(key);
	binfo->bi_dat.bi_level = 0;

	return 0;
}

static int nilfs_direct_assign(struct nilfs_bmap *bmap,
			       struct buffer_head **bh,
			       sector_t blocknr,
			       union nilfs_binfo *binfo)
{
	__u64 key;
	__u64 ptr;

	key = nilfs_bmap_data_get_key(bmap, *bh);
	if (unlikely(key > NILFS_DIRECT_KEY_MAX)) {
		printk(KERN_CRIT "%s: invalid key: %llu\n", __func__,
		       (unsigned long long)key);
		return -EINVAL;
	}
	ptr = nilfs_direct_get_ptr(bmap, key);
	if (unlikely(ptr == NILFS_BMAP_INVALID_PTR)) {
		printk(KERN_CRIT "%s: invalid pointer: %llu\n", __func__,
		       (unsigned long long)ptr);
		return -EINVAL;
	}

	return NILFS_BMAP_USE_VBN(bmap) ?
		nilfs_direct_assign_v(bmap, key, ptr, bh, blocknr, binfo) :
		nilfs_direct_assign_p(bmap, key, ptr, bh, blocknr, binfo);
}

static const struct nilfs_bmap_operations nilfs_direct_ops = {
	.bop_lookup		=	nilfs_direct_lookup,
	.bop_lookup_contig	=	nilfs_direct_lookup_contig,
	.bop_insert		=	nilfs_direct_insert,
	.bop_delete		=	nilfs_direct_delete,
	.bop_clear		=	NULL,

	.bop_propagate		=	nilfs_direct_propagate,

	.bop_lookup_dirty_buffers	=	NULL,

	.bop_assign		=	nilfs_direct_assign,
	.bop_mark		=	NULL,

	.bop_seek_key		=	nilfs_direct_seek_key,
	.bop_last_key		=	nilfs_direct_last_key,

	.bop_check_insert	=	nilfs_direct_check_insert,
	.bop_check_delete	=	NULL,
	.bop_gather_data	=	nilfs_direct_gather_data,
};


int nilfs_direct_init(struct nilfs_bmap *bmap)
{
	bmap->b_ops = &nilfs_direct_ops;
	return 0;
}
