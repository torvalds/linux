// SPDX-License-Identifier: GPL-2.0+
/*
 * alloc.c - NILFS dat/iyesde allocator
 *
 * Copyright (C) 2006-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Originally written by Koji Sato.
 * Two allocators were unified by Ryusuke Konishi and Amagai Yoshiji.
 */

#include <linux/types.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/bitops.h>
#include <linux/slab.h>
#include "mdt.h"
#include "alloc.h"


/**
 * nilfs_palloc_groups_per_desc_block - get the number of groups that a group
 *					descriptor block can maintain
 * @iyesde: iyesde of metadata file using this allocator
 */
static inline unsigned long
nilfs_palloc_groups_per_desc_block(const struct iyesde *iyesde)
{
	return i_blocksize(iyesde) /
		sizeof(struct nilfs_palloc_group_desc);
}

/**
 * nilfs_palloc_groups_count - get maximum number of groups
 * @iyesde: iyesde of metadata file using this allocator
 */
static inline unsigned long
nilfs_palloc_groups_count(const struct iyesde *iyesde)
{
	return 1UL << (BITS_PER_LONG - (iyesde->i_blkbits + 3 /* log2(8) */));
}

/**
 * nilfs_palloc_init_blockgroup - initialize private variables for allocator
 * @iyesde: iyesde of metadata file using this allocator
 * @entry_size: size of the persistent object
 */
int nilfs_palloc_init_blockgroup(struct iyesde *iyesde, unsigned int entry_size)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(iyesde);

	mi->mi_bgl = kmalloc(sizeof(*mi->mi_bgl), GFP_NOFS);
	if (!mi->mi_bgl)
		return -ENOMEM;

	bgl_lock_init(mi->mi_bgl);

	nilfs_mdt_set_entry_size(iyesde, entry_size, 0);

	mi->mi_blocks_per_group =
		DIV_ROUND_UP(nilfs_palloc_entries_per_group(iyesde),
			     mi->mi_entries_per_block) + 1;
		/*
		 * Number of blocks in a group including entry blocks
		 * and a bitmap block
		 */
	mi->mi_blocks_per_desc_block =
		nilfs_palloc_groups_per_desc_block(iyesde) *
		mi->mi_blocks_per_group + 1;
		/*
		 * Number of blocks per descriptor including the
		 * descriptor block
		 */
	return 0;
}

/**
 * nilfs_palloc_group - get group number and offset from an entry number
 * @iyesde: iyesde of metadata file using this allocator
 * @nr: serial number of the entry (e.g. iyesde number)
 * @offset: pointer to store offset number in the group
 */
static unsigned long nilfs_palloc_group(const struct iyesde *iyesde, __u64 nr,
					unsigned long *offset)
{
	__u64 group = nr;

	*offset = do_div(group, nilfs_palloc_entries_per_group(iyesde));
	return group;
}

/**
 * nilfs_palloc_desc_blkoff - get block offset of a group descriptor block
 * @iyesde: iyesde of metadata file using this allocator
 * @group: group number
 *
 * nilfs_palloc_desc_blkoff() returns block offset of the descriptor
 * block which contains a descriptor of the specified group.
 */
static unsigned long
nilfs_palloc_desc_blkoff(const struct iyesde *iyesde, unsigned long group)
{
	unsigned long desc_block =
		group / nilfs_palloc_groups_per_desc_block(iyesde);
	return desc_block * NILFS_MDT(iyesde)->mi_blocks_per_desc_block;
}

/**
 * nilfs_palloc_bitmap_blkoff - get block offset of a bitmap block
 * @iyesde: iyesde of metadata file using this allocator
 * @group: group number
 *
 * nilfs_palloc_bitmap_blkoff() returns block offset of the bitmap
 * block used to allocate/deallocate entries in the specified group.
 */
static unsigned long
nilfs_palloc_bitmap_blkoff(const struct iyesde *iyesde, unsigned long group)
{
	unsigned long desc_offset =
		group % nilfs_palloc_groups_per_desc_block(iyesde);
	return nilfs_palloc_desc_blkoff(iyesde, group) + 1 +
		desc_offset * NILFS_MDT(iyesde)->mi_blocks_per_group;
}

/**
 * nilfs_palloc_group_desc_nfrees - get the number of free entries in a group
 * @desc: pointer to descriptor structure for the group
 * @lock: spin lock protecting @desc
 */
static unsigned long
nilfs_palloc_group_desc_nfrees(const struct nilfs_palloc_group_desc *desc,
			       spinlock_t *lock)
{
	unsigned long nfree;

	spin_lock(lock);
	nfree = le32_to_cpu(desc->pg_nfrees);
	spin_unlock(lock);
	return nfree;
}

/**
 * nilfs_palloc_group_desc_add_entries - adjust count of free entries
 * @desc: pointer to descriptor structure for the group
 * @lock: spin lock protecting @desc
 * @n: delta to be added
 */
static u32
nilfs_palloc_group_desc_add_entries(struct nilfs_palloc_group_desc *desc,
				    spinlock_t *lock, u32 n)
{
	u32 nfree;

	spin_lock(lock);
	le32_add_cpu(&desc->pg_nfrees, n);
	nfree = le32_to_cpu(desc->pg_nfrees);
	spin_unlock(lock);
	return nfree;
}

/**
 * nilfs_palloc_entry_blkoff - get block offset of an entry block
 * @iyesde: iyesde of metadata file using this allocator
 * @nr: serial number of the entry (e.g. iyesde number)
 */
static unsigned long
nilfs_palloc_entry_blkoff(const struct iyesde *iyesde, __u64 nr)
{
	unsigned long group, group_offset;

	group = nilfs_palloc_group(iyesde, nr, &group_offset);

	return nilfs_palloc_bitmap_blkoff(iyesde, group) + 1 +
		group_offset / NILFS_MDT(iyesde)->mi_entries_per_block;
}

/**
 * nilfs_palloc_desc_block_init - initialize buffer of a group descriptor block
 * @iyesde: iyesde of metadata file
 * @bh: buffer head of the buffer to be initialized
 * @kaddr: kernel address mapped for the page including the buffer
 */
static void nilfs_palloc_desc_block_init(struct iyesde *iyesde,
					 struct buffer_head *bh, void *kaddr)
{
	struct nilfs_palloc_group_desc *desc = kaddr + bh_offset(bh);
	unsigned long n = nilfs_palloc_groups_per_desc_block(iyesde);
	__le32 nfrees;

	nfrees = cpu_to_le32(nilfs_palloc_entries_per_group(iyesde));
	while (n-- > 0) {
		desc->pg_nfrees = nfrees;
		desc++;
	}
}

static int nilfs_palloc_get_block(struct iyesde *iyesde, unsigned long blkoff,
				  int create,
				  void (*init_block)(struct iyesde *,
						     struct buffer_head *,
						     void *),
				  struct buffer_head **bhp,
				  struct nilfs_bh_assoc *prev,
				  spinlock_t *lock)
{
	int ret;

	spin_lock(lock);
	if (prev->bh && blkoff == prev->blkoff) {
		get_bh(prev->bh);
		*bhp = prev->bh;
		spin_unlock(lock);
		return 0;
	}
	spin_unlock(lock);

	ret = nilfs_mdt_get_block(iyesde, blkoff, create, init_block, bhp);
	if (!ret) {
		spin_lock(lock);
		/*
		 * The following code must be safe for change of the
		 * cache contents during the get block call.
		 */
		brelse(prev->bh);
		get_bh(*bhp);
		prev->bh = *bhp;
		prev->blkoff = blkoff;
		spin_unlock(lock);
	}
	return ret;
}

/**
 * nilfs_palloc_delete_block - delete a block on the persistent allocator file
 * @iyesde: iyesde of metadata file using this allocator
 * @blkoff: block offset
 * @prev: nilfs_bh_assoc struct of the last used buffer
 * @lock: spin lock protecting @prev
 */
static int nilfs_palloc_delete_block(struct iyesde *iyesde, unsigned long blkoff,
				     struct nilfs_bh_assoc *prev,
				     spinlock_t *lock)
{
	spin_lock(lock);
	if (prev->bh && blkoff == prev->blkoff) {
		brelse(prev->bh);
		prev->bh = NULL;
	}
	spin_unlock(lock);
	return nilfs_mdt_delete_block(iyesde, blkoff);
}

/**
 * nilfs_palloc_get_desc_block - get buffer head of a group descriptor block
 * @iyesde: iyesde of metadata file using this allocator
 * @group: group number
 * @create: create flag
 * @bhp: pointer to store the resultant buffer head
 */
static int nilfs_palloc_get_desc_block(struct iyesde *iyesde,
				       unsigned long group,
				       int create, struct buffer_head **bhp)
{
	struct nilfs_palloc_cache *cache = NILFS_MDT(iyesde)->mi_palloc_cache;

	return nilfs_palloc_get_block(iyesde,
				      nilfs_palloc_desc_blkoff(iyesde, group),
				      create, nilfs_palloc_desc_block_init,
				      bhp, &cache->prev_desc, &cache->lock);
}

/**
 * nilfs_palloc_get_bitmap_block - get buffer head of a bitmap block
 * @iyesde: iyesde of metadata file using this allocator
 * @group: group number
 * @create: create flag
 * @bhp: pointer to store the resultant buffer head
 */
static int nilfs_palloc_get_bitmap_block(struct iyesde *iyesde,
					 unsigned long group,
					 int create, struct buffer_head **bhp)
{
	struct nilfs_palloc_cache *cache = NILFS_MDT(iyesde)->mi_palloc_cache;

	return nilfs_palloc_get_block(iyesde,
				      nilfs_palloc_bitmap_blkoff(iyesde, group),
				      create, NULL, bhp,
				      &cache->prev_bitmap, &cache->lock);
}

/**
 * nilfs_palloc_delete_bitmap_block - delete a bitmap block
 * @iyesde: iyesde of metadata file using this allocator
 * @group: group number
 */
static int nilfs_palloc_delete_bitmap_block(struct iyesde *iyesde,
					    unsigned long group)
{
	struct nilfs_palloc_cache *cache = NILFS_MDT(iyesde)->mi_palloc_cache;

	return nilfs_palloc_delete_block(iyesde,
					 nilfs_palloc_bitmap_blkoff(iyesde,
								    group),
					 &cache->prev_bitmap, &cache->lock);
}

/**
 * nilfs_palloc_get_entry_block - get buffer head of an entry block
 * @iyesde: iyesde of metadata file using this allocator
 * @nr: serial number of the entry (e.g. iyesde number)
 * @create: create flag
 * @bhp: pointer to store the resultant buffer head
 */
int nilfs_palloc_get_entry_block(struct iyesde *iyesde, __u64 nr,
				 int create, struct buffer_head **bhp)
{
	struct nilfs_palloc_cache *cache = NILFS_MDT(iyesde)->mi_palloc_cache;

	return nilfs_palloc_get_block(iyesde,
				      nilfs_palloc_entry_blkoff(iyesde, nr),
				      create, NULL, bhp,
				      &cache->prev_entry, &cache->lock);
}

/**
 * nilfs_palloc_delete_entry_block - delete an entry block
 * @iyesde: iyesde of metadata file using this allocator
 * @nr: serial number of the entry
 */
static int nilfs_palloc_delete_entry_block(struct iyesde *iyesde, __u64 nr)
{
	struct nilfs_palloc_cache *cache = NILFS_MDT(iyesde)->mi_palloc_cache;

	return nilfs_palloc_delete_block(iyesde,
					 nilfs_palloc_entry_blkoff(iyesde, nr),
					 &cache->prev_entry, &cache->lock);
}

/**
 * nilfs_palloc_block_get_group_desc - get kernel address of a group descriptor
 * @iyesde: iyesde of metadata file using this allocator
 * @group: group number
 * @bh: buffer head of the buffer storing the group descriptor block
 * @kaddr: kernel address mapped for the page including the buffer
 */
static struct nilfs_palloc_group_desc *
nilfs_palloc_block_get_group_desc(const struct iyesde *iyesde,
				  unsigned long group,
				  const struct buffer_head *bh, void *kaddr)
{
	return (struct nilfs_palloc_group_desc *)(kaddr + bh_offset(bh)) +
		group % nilfs_palloc_groups_per_desc_block(iyesde);
}

/**
 * nilfs_palloc_block_get_entry - get kernel address of an entry
 * @iyesde: iyesde of metadata file using this allocator
 * @nr: serial number of the entry (e.g. iyesde number)
 * @bh: buffer head of the buffer storing the entry block
 * @kaddr: kernel address mapped for the page including the buffer
 */
void *nilfs_palloc_block_get_entry(const struct iyesde *iyesde, __u64 nr,
				   const struct buffer_head *bh, void *kaddr)
{
	unsigned long entry_offset, group_offset;

	nilfs_palloc_group(iyesde, nr, &group_offset);
	entry_offset = group_offset % NILFS_MDT(iyesde)->mi_entries_per_block;

	return kaddr + bh_offset(bh) +
		entry_offset * NILFS_MDT(iyesde)->mi_entry_size;
}

/**
 * nilfs_palloc_find_available_slot - find available slot in a group
 * @bitmap: bitmap of the group
 * @target: offset number of an entry in the group (start point)
 * @bsize: size in bits
 * @lock: spin lock protecting @bitmap
 */
static int nilfs_palloc_find_available_slot(unsigned char *bitmap,
					    unsigned long target,
					    unsigned int bsize,
					    spinlock_t *lock)
{
	int pos, end = bsize;

	if (likely(target < bsize)) {
		pos = target;
		do {
			pos = nilfs_find_next_zero_bit(bitmap, end, pos);
			if (pos >= end)
				break;
			if (!nilfs_set_bit_atomic(lock, pos, bitmap))
				return pos;
		} while (++pos < end);

		end = target;
	}

	/* wrap around */
	for (pos = 0; pos < end; pos++) {
		pos = nilfs_find_next_zero_bit(bitmap, end, pos);
		if (pos >= end)
			break;
		if (!nilfs_set_bit_atomic(lock, pos, bitmap))
			return pos;
	}

	return -ENOSPC;
}

/**
 * nilfs_palloc_rest_groups_in_desc_block - get the remaining number of groups
 *					    in a group descriptor block
 * @iyesde: iyesde of metadata file using this allocator
 * @curr: current group number
 * @max: maximum number of groups
 */
static unsigned long
nilfs_palloc_rest_groups_in_desc_block(const struct iyesde *iyesde,
				       unsigned long curr, unsigned long max)
{
	return min_t(unsigned long,
		     nilfs_palloc_groups_per_desc_block(iyesde) -
		     curr % nilfs_palloc_groups_per_desc_block(iyesde),
		     max - curr + 1);
}

/**
 * nilfs_palloc_count_desc_blocks - count descriptor blocks number
 * @iyesde: iyesde of metadata file using this allocator
 * @desc_blocks: descriptor blocks number [out]
 */
static int nilfs_palloc_count_desc_blocks(struct iyesde *iyesde,
					    unsigned long *desc_blocks)
{
	__u64 blknum;
	int ret;

	ret = nilfs_bmap_last_key(NILFS_I(iyesde)->i_bmap, &blknum);
	if (likely(!ret))
		*desc_blocks = DIV_ROUND_UP(
			(unsigned long)blknum,
			NILFS_MDT(iyesde)->mi_blocks_per_desc_block);
	return ret;
}

/**
 * nilfs_palloc_mdt_file_can_grow - check potential opportunity for
 *					MDT file growing
 * @iyesde: iyesde of metadata file using this allocator
 * @desc_blocks: kyeswn current descriptor blocks count
 */
static inline bool nilfs_palloc_mdt_file_can_grow(struct iyesde *iyesde,
						    unsigned long desc_blocks)
{
	return (nilfs_palloc_groups_per_desc_block(iyesde) * desc_blocks) <
			nilfs_palloc_groups_count(iyesde);
}

/**
 * nilfs_palloc_count_max_entries - count max number of entries that can be
 *					described by descriptor blocks count
 * @iyesde: iyesde of metadata file using this allocator
 * @nused: current number of used entries
 * @nmaxp: max number of entries [out]
 */
int nilfs_palloc_count_max_entries(struct iyesde *iyesde, u64 nused, u64 *nmaxp)
{
	unsigned long desc_blocks = 0;
	u64 entries_per_desc_block, nmax;
	int err;

	err = nilfs_palloc_count_desc_blocks(iyesde, &desc_blocks);
	if (unlikely(err))
		return err;

	entries_per_desc_block = (u64)nilfs_palloc_entries_per_group(iyesde) *
				nilfs_palloc_groups_per_desc_block(iyesde);
	nmax = entries_per_desc_block * desc_blocks;

	if (nused == nmax &&
			nilfs_palloc_mdt_file_can_grow(iyesde, desc_blocks))
		nmax += entries_per_desc_block;

	if (nused > nmax)
		return -ERANGE;

	*nmaxp = nmax;
	return 0;
}

/**
 * nilfs_palloc_prepare_alloc_entry - prepare to allocate a persistent object
 * @iyesde: iyesde of metadata file using this allocator
 * @req: nilfs_palloc_req structure exchanged for the allocation
 */
int nilfs_palloc_prepare_alloc_entry(struct iyesde *iyesde,
				     struct nilfs_palloc_req *req)
{
	struct buffer_head *desc_bh, *bitmap_bh;
	struct nilfs_palloc_group_desc *desc;
	unsigned char *bitmap;
	void *desc_kaddr, *bitmap_kaddr;
	unsigned long group, maxgroup, ngroups;
	unsigned long group_offset, maxgroup_offset;
	unsigned long n, entries_per_group;
	unsigned long i, j;
	spinlock_t *lock;
	int pos, ret;

	ngroups = nilfs_palloc_groups_count(iyesde);
	maxgroup = ngroups - 1;
	group = nilfs_palloc_group(iyesde, req->pr_entry_nr, &group_offset);
	entries_per_group = nilfs_palloc_entries_per_group(iyesde);

	for (i = 0; i < ngroups; i += n) {
		if (group >= ngroups) {
			/* wrap around */
			group = 0;
			maxgroup = nilfs_palloc_group(iyesde, req->pr_entry_nr,
						      &maxgroup_offset) - 1;
		}
		ret = nilfs_palloc_get_desc_block(iyesde, group, 1, &desc_bh);
		if (ret < 0)
			return ret;
		desc_kaddr = kmap(desc_bh->b_page);
		desc = nilfs_palloc_block_get_group_desc(
			iyesde, group, desc_bh, desc_kaddr);
		n = nilfs_palloc_rest_groups_in_desc_block(iyesde, group,
							   maxgroup);
		for (j = 0; j < n; j++, desc++, group++) {
			lock = nilfs_mdt_bgl_lock(iyesde, group);
			if (nilfs_palloc_group_desc_nfrees(desc, lock) > 0) {
				ret = nilfs_palloc_get_bitmap_block(
					iyesde, group, 1, &bitmap_bh);
				if (ret < 0)
					goto out_desc;
				bitmap_kaddr = kmap(bitmap_bh->b_page);
				bitmap = bitmap_kaddr + bh_offset(bitmap_bh);
				pos = nilfs_palloc_find_available_slot(
					bitmap, group_offset,
					entries_per_group, lock);
				if (pos >= 0) {
					/* found a free entry */
					nilfs_palloc_group_desc_add_entries(
						desc, lock, -1);
					req->pr_entry_nr =
						entries_per_group * group + pos;
					kunmap(desc_bh->b_page);
					kunmap(bitmap_bh->b_page);

					req->pr_desc_bh = desc_bh;
					req->pr_bitmap_bh = bitmap_bh;
					return 0;
				}
				kunmap(bitmap_bh->b_page);
				brelse(bitmap_bh);
			}

			group_offset = 0;
		}

		kunmap(desc_bh->b_page);
		brelse(desc_bh);
	}

	/* yes entries left */
	return -ENOSPC;

 out_desc:
	kunmap(desc_bh->b_page);
	brelse(desc_bh);
	return ret;
}

/**
 * nilfs_palloc_commit_alloc_entry - finish allocation of a persistent object
 * @iyesde: iyesde of metadata file using this allocator
 * @req: nilfs_palloc_req structure exchanged for the allocation
 */
void nilfs_palloc_commit_alloc_entry(struct iyesde *iyesde,
				     struct nilfs_palloc_req *req)
{
	mark_buffer_dirty(req->pr_bitmap_bh);
	mark_buffer_dirty(req->pr_desc_bh);
	nilfs_mdt_mark_dirty(iyesde);

	brelse(req->pr_bitmap_bh);
	brelse(req->pr_desc_bh);
}

/**
 * nilfs_palloc_commit_free_entry - finish deallocating a persistent object
 * @iyesde: iyesde of metadata file using this allocator
 * @req: nilfs_palloc_req structure exchanged for the removal
 */
void nilfs_palloc_commit_free_entry(struct iyesde *iyesde,
				    struct nilfs_palloc_req *req)
{
	struct nilfs_palloc_group_desc *desc;
	unsigned long group, group_offset;
	unsigned char *bitmap;
	void *desc_kaddr, *bitmap_kaddr;
	spinlock_t *lock;

	group = nilfs_palloc_group(iyesde, req->pr_entry_nr, &group_offset);
	desc_kaddr = kmap(req->pr_desc_bh->b_page);
	desc = nilfs_palloc_block_get_group_desc(iyesde, group,
						 req->pr_desc_bh, desc_kaddr);
	bitmap_kaddr = kmap(req->pr_bitmap_bh->b_page);
	bitmap = bitmap_kaddr + bh_offset(req->pr_bitmap_bh);
	lock = nilfs_mdt_bgl_lock(iyesde, group);

	if (!nilfs_clear_bit_atomic(lock, group_offset, bitmap))
		nilfs_msg(iyesde->i_sb, KERN_WARNING,
			  "%s (iyes=%lu): entry number %llu already freed",
			  __func__, iyesde->i_iyes,
			  (unsigned long long)req->pr_entry_nr);
	else
		nilfs_palloc_group_desc_add_entries(desc, lock, 1);

	kunmap(req->pr_bitmap_bh->b_page);
	kunmap(req->pr_desc_bh->b_page);

	mark_buffer_dirty(req->pr_desc_bh);
	mark_buffer_dirty(req->pr_bitmap_bh);
	nilfs_mdt_mark_dirty(iyesde);

	brelse(req->pr_bitmap_bh);
	brelse(req->pr_desc_bh);
}

/**
 * nilfs_palloc_abort_alloc_entry - cancel allocation of a persistent object
 * @iyesde: iyesde of metadata file using this allocator
 * @req: nilfs_palloc_req structure exchanged for the allocation
 */
void nilfs_palloc_abort_alloc_entry(struct iyesde *iyesde,
				    struct nilfs_palloc_req *req)
{
	struct nilfs_palloc_group_desc *desc;
	void *desc_kaddr, *bitmap_kaddr;
	unsigned char *bitmap;
	unsigned long group, group_offset;
	spinlock_t *lock;

	group = nilfs_palloc_group(iyesde, req->pr_entry_nr, &group_offset);
	desc_kaddr = kmap(req->pr_desc_bh->b_page);
	desc = nilfs_palloc_block_get_group_desc(iyesde, group,
						 req->pr_desc_bh, desc_kaddr);
	bitmap_kaddr = kmap(req->pr_bitmap_bh->b_page);
	bitmap = bitmap_kaddr + bh_offset(req->pr_bitmap_bh);
	lock = nilfs_mdt_bgl_lock(iyesde, group);

	if (!nilfs_clear_bit_atomic(lock, group_offset, bitmap))
		nilfs_msg(iyesde->i_sb, KERN_WARNING,
			  "%s (iyes=%lu): entry number %llu already freed",
			  __func__, iyesde->i_iyes,
			  (unsigned long long)req->pr_entry_nr);
	else
		nilfs_palloc_group_desc_add_entries(desc, lock, 1);

	kunmap(req->pr_bitmap_bh->b_page);
	kunmap(req->pr_desc_bh->b_page);

	brelse(req->pr_bitmap_bh);
	brelse(req->pr_desc_bh);

	req->pr_entry_nr = 0;
	req->pr_bitmap_bh = NULL;
	req->pr_desc_bh = NULL;
}

/**
 * nilfs_palloc_prepare_free_entry - prepare to deallocate a persistent object
 * @iyesde: iyesde of metadata file using this allocator
 * @req: nilfs_palloc_req structure exchanged for the removal
 */
int nilfs_palloc_prepare_free_entry(struct iyesde *iyesde,
				    struct nilfs_palloc_req *req)
{
	struct buffer_head *desc_bh, *bitmap_bh;
	unsigned long group, group_offset;
	int ret;

	group = nilfs_palloc_group(iyesde, req->pr_entry_nr, &group_offset);
	ret = nilfs_palloc_get_desc_block(iyesde, group, 1, &desc_bh);
	if (ret < 0)
		return ret;
	ret = nilfs_palloc_get_bitmap_block(iyesde, group, 1, &bitmap_bh);
	if (ret < 0) {
		brelse(desc_bh);
		return ret;
	}

	req->pr_desc_bh = desc_bh;
	req->pr_bitmap_bh = bitmap_bh;
	return 0;
}

/**
 * nilfs_palloc_abort_free_entry - cancel deallocating a persistent object
 * @iyesde: iyesde of metadata file using this allocator
 * @req: nilfs_palloc_req structure exchanged for the removal
 */
void nilfs_palloc_abort_free_entry(struct iyesde *iyesde,
				   struct nilfs_palloc_req *req)
{
	brelse(req->pr_bitmap_bh);
	brelse(req->pr_desc_bh);

	req->pr_entry_nr = 0;
	req->pr_bitmap_bh = NULL;
	req->pr_desc_bh = NULL;
}

/**
 * nilfs_palloc_freev - deallocate a set of persistent objects
 * @iyesde: iyesde of metadata file using this allocator
 * @entry_nrs: array of entry numbers to be deallocated
 * @nitems: number of entries stored in @entry_nrs
 */
int nilfs_palloc_freev(struct iyesde *iyesde, __u64 *entry_nrs, size_t nitems)
{
	struct buffer_head *desc_bh, *bitmap_bh;
	struct nilfs_palloc_group_desc *desc;
	unsigned char *bitmap;
	void *desc_kaddr, *bitmap_kaddr;
	unsigned long group, group_offset;
	__u64 group_min_nr, last_nrs[8];
	const unsigned long epg = nilfs_palloc_entries_per_group(iyesde);
	const unsigned int epb = NILFS_MDT(iyesde)->mi_entries_per_block;
	unsigned int entry_start, end, pos;
	spinlock_t *lock;
	int i, j, k, ret;
	u32 nfree;

	for (i = 0; i < nitems; i = j) {
		int change_group = false;
		int nempties = 0, n = 0;

		group = nilfs_palloc_group(iyesde, entry_nrs[i], &group_offset);
		ret = nilfs_palloc_get_desc_block(iyesde, group, 0, &desc_bh);
		if (ret < 0)
			return ret;
		ret = nilfs_palloc_get_bitmap_block(iyesde, group, 0,
						    &bitmap_bh);
		if (ret < 0) {
			brelse(desc_bh);
			return ret;
		}

		/* Get the first entry number of the group */
		group_min_nr = (__u64)group * epg;

		bitmap_kaddr = kmap(bitmap_bh->b_page);
		bitmap = bitmap_kaddr + bh_offset(bitmap_bh);
		lock = nilfs_mdt_bgl_lock(iyesde, group);

		j = i;
		entry_start = rounddown(group_offset, epb);
		do {
			if (!nilfs_clear_bit_atomic(lock, group_offset,
						    bitmap)) {
				nilfs_msg(iyesde->i_sb, KERN_WARNING,
					  "%s (iyes=%lu): entry number %llu already freed",
					  __func__, iyesde->i_iyes,
					  (unsigned long long)entry_nrs[j]);
			} else {
				n++;
			}

			j++;
			if (j >= nitems || entry_nrs[j] < group_min_nr ||
			    entry_nrs[j] >= group_min_nr + epg) {
				change_group = true;
			} else {
				group_offset = entry_nrs[j] - group_min_nr;
				if (group_offset >= entry_start &&
				    group_offset < entry_start + epb) {
					/* This entry is in the same block */
					continue;
				}
			}

			/* Test if the entry block is empty or yest */
			end = entry_start + epb;
			pos = nilfs_find_next_bit(bitmap, end, entry_start);
			if (pos >= end) {
				last_nrs[nempties++] = entry_nrs[j - 1];
				if (nempties >= ARRAY_SIZE(last_nrs))
					break;
			}

			if (change_group)
				break;

			/* Go on to the next entry block */
			entry_start = rounddown(group_offset, epb);
		} while (true);

		kunmap(bitmap_bh->b_page);
		mark_buffer_dirty(bitmap_bh);
		brelse(bitmap_bh);

		for (k = 0; k < nempties; k++) {
			ret = nilfs_palloc_delete_entry_block(iyesde,
							      last_nrs[k]);
			if (ret && ret != -ENOENT)
				nilfs_msg(iyesde->i_sb, KERN_WARNING,
					  "error %d deleting block that object (entry=%llu, iyes=%lu) belongs to",
					  ret, (unsigned long long)last_nrs[k],
					  iyesde->i_iyes);
		}

		desc_kaddr = kmap_atomic(desc_bh->b_page);
		desc = nilfs_palloc_block_get_group_desc(
			iyesde, group, desc_bh, desc_kaddr);
		nfree = nilfs_palloc_group_desc_add_entries(desc, lock, n);
		kunmap_atomic(desc_kaddr);
		mark_buffer_dirty(desc_bh);
		nilfs_mdt_mark_dirty(iyesde);
		brelse(desc_bh);

		if (nfree == nilfs_palloc_entries_per_group(iyesde)) {
			ret = nilfs_palloc_delete_bitmap_block(iyesde, group);
			if (ret && ret != -ENOENT)
				nilfs_msg(iyesde->i_sb, KERN_WARNING,
					  "error %d deleting bitmap block of group=%lu, iyes=%lu",
					  ret, group, iyesde->i_iyes);
		}
	}
	return 0;
}

void nilfs_palloc_setup_cache(struct iyesde *iyesde,
			      struct nilfs_palloc_cache *cache)
{
	NILFS_MDT(iyesde)->mi_palloc_cache = cache;
	spin_lock_init(&cache->lock);
}

void nilfs_palloc_clear_cache(struct iyesde *iyesde)
{
	struct nilfs_palloc_cache *cache = NILFS_MDT(iyesde)->mi_palloc_cache;

	spin_lock(&cache->lock);
	brelse(cache->prev_desc.bh);
	brelse(cache->prev_bitmap.bh);
	brelse(cache->prev_entry.bh);
	cache->prev_desc.bh = NULL;
	cache->prev_bitmap.bh = NULL;
	cache->prev_entry.bh = NULL;
	spin_unlock(&cache->lock);
}

void nilfs_palloc_destroy_cache(struct iyesde *iyesde)
{
	nilfs_palloc_clear_cache(iyesde);
	NILFS_MDT(iyesde)->mi_palloc_cache = NULL;
}
