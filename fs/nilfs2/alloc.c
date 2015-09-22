/*
 * alloc.c - NILFS dat/inode allocator
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
 * Original code was written by Koji Sato <koji@osrg.net>.
 * Two allocators were unified by Ryusuke Konishi <ryusuke@osrg.net>,
 *                                Amagai Yoshiji <amagai@osrg.net>.
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
 * @inode: inode of metadata file using this allocator
 */
static inline unsigned long
nilfs_palloc_groups_per_desc_block(const struct inode *inode)
{
	return (1UL << inode->i_blkbits) /
		sizeof(struct nilfs_palloc_group_desc);
}

/**
 * nilfs_palloc_groups_count - get maximum number of groups
 * @inode: inode of metadata file using this allocator
 */
static inline unsigned long
nilfs_palloc_groups_count(const struct inode *inode)
{
	return 1UL << (BITS_PER_LONG - (inode->i_blkbits + 3 /* log2(8) */));
}

/**
 * nilfs_palloc_init_blockgroup - initialize private variables for allocator
 * @inode: inode of metadata file using this allocator
 * @entry_size: size of the persistent object
 */
int nilfs_palloc_init_blockgroup(struct inode *inode, unsigned entry_size)
{
	struct nilfs_mdt_info *mi = NILFS_MDT(inode);

	mi->mi_bgl = kmalloc(sizeof(*mi->mi_bgl), GFP_NOFS);
	if (!mi->mi_bgl)
		return -ENOMEM;

	bgl_lock_init(mi->mi_bgl);

	nilfs_mdt_set_entry_size(inode, entry_size, 0);

	mi->mi_blocks_per_group =
		DIV_ROUND_UP(nilfs_palloc_entries_per_group(inode),
			     mi->mi_entries_per_block) + 1;
		/* Number of blocks in a group including entry blocks and
		   a bitmap block */
	mi->mi_blocks_per_desc_block =
		nilfs_palloc_groups_per_desc_block(inode) *
		mi->mi_blocks_per_group + 1;
		/* Number of blocks per descriptor including the
		   descriptor block */
	return 0;
}

/**
 * nilfs_palloc_group - get group number and offset from an entry number
 * @inode: inode of metadata file using this allocator
 * @nr: serial number of the entry (e.g. inode number)
 * @offset: pointer to store offset number in the group
 */
static unsigned long nilfs_palloc_group(const struct inode *inode, __u64 nr,
					unsigned long *offset)
{
	__u64 group = nr;

	*offset = do_div(group, nilfs_palloc_entries_per_group(inode));
	return group;
}

/**
 * nilfs_palloc_desc_blkoff - get block offset of a group descriptor block
 * @inode: inode of metadata file using this allocator
 * @group: group number
 *
 * nilfs_palloc_desc_blkoff() returns block offset of the descriptor
 * block which contains a descriptor of the specified group.
 */
static unsigned long
nilfs_palloc_desc_blkoff(const struct inode *inode, unsigned long group)
{
	unsigned long desc_block =
		group / nilfs_palloc_groups_per_desc_block(inode);
	return desc_block * NILFS_MDT(inode)->mi_blocks_per_desc_block;
}

/**
 * nilfs_palloc_bitmap_blkoff - get block offset of a bitmap block
 * @inode: inode of metadata file using this allocator
 * @group: group number
 *
 * nilfs_palloc_bitmap_blkoff() returns block offset of the bitmap
 * block used to allocate/deallocate entries in the specified group.
 */
static unsigned long
nilfs_palloc_bitmap_blkoff(const struct inode *inode, unsigned long group)
{
	unsigned long desc_offset =
		group % nilfs_palloc_groups_per_desc_block(inode);
	return nilfs_palloc_desc_blkoff(inode, group) + 1 +
		desc_offset * NILFS_MDT(inode)->mi_blocks_per_group;
}

/**
 * nilfs_palloc_group_desc_nfrees - get the number of free entries in a group
 * @inode: inode of metadata file using this allocator
 * @group: group number
 * @desc: pointer to descriptor structure for the group
 */
static unsigned long
nilfs_palloc_group_desc_nfrees(struct inode *inode, unsigned long group,
			       const struct nilfs_palloc_group_desc *desc)
{
	unsigned long nfree;

	spin_lock(nilfs_mdt_bgl_lock(inode, group));
	nfree = le32_to_cpu(desc->pg_nfrees);
	spin_unlock(nilfs_mdt_bgl_lock(inode, group));
	return nfree;
}

/**
 * nilfs_palloc_group_desc_add_entries - adjust count of free entries
 * @inode: inode of metadata file using this allocator
 * @group: group number
 * @desc: pointer to descriptor structure for the group
 * @n: delta to be added
 */
static void
nilfs_palloc_group_desc_add_entries(struct inode *inode,
				    unsigned long group,
				    struct nilfs_palloc_group_desc *desc,
				    u32 n)
{
	spin_lock(nilfs_mdt_bgl_lock(inode, group));
	le32_add_cpu(&desc->pg_nfrees, n);
	spin_unlock(nilfs_mdt_bgl_lock(inode, group));
}

/**
 * nilfs_palloc_entry_blkoff - get block offset of an entry block
 * @inode: inode of metadata file using this allocator
 * @nr: serial number of the entry (e.g. inode number)
 */
static unsigned long
nilfs_palloc_entry_blkoff(const struct inode *inode, __u64 nr)
{
	unsigned long group, group_offset;

	group = nilfs_palloc_group(inode, nr, &group_offset);

	return nilfs_palloc_bitmap_blkoff(inode, group) + 1 +
		group_offset / NILFS_MDT(inode)->mi_entries_per_block;
}

/**
 * nilfs_palloc_desc_block_init - initialize buffer of a group descriptor block
 * @inode: inode of metadata file
 * @bh: buffer head of the buffer to be initialized
 * @kaddr: kernel address mapped for the page including the buffer
 */
static void nilfs_palloc_desc_block_init(struct inode *inode,
					 struct buffer_head *bh, void *kaddr)
{
	struct nilfs_palloc_group_desc *desc = kaddr + bh_offset(bh);
	unsigned long n = nilfs_palloc_groups_per_desc_block(inode);
	__le32 nfrees;

	nfrees = cpu_to_le32(nilfs_palloc_entries_per_group(inode));
	while (n-- > 0) {
		desc->pg_nfrees = nfrees;
		desc++;
	}
}

static int nilfs_palloc_get_block(struct inode *inode, unsigned long blkoff,
				  int create,
				  void (*init_block)(struct inode *,
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

	ret = nilfs_mdt_get_block(inode, blkoff, create, init_block, bhp);
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
 * nilfs_palloc_get_desc_block - get buffer head of a group descriptor block
 * @inode: inode of metadata file using this allocator
 * @group: group number
 * @create: create flag
 * @bhp: pointer to store the resultant buffer head
 */
static int nilfs_palloc_get_desc_block(struct inode *inode,
				       unsigned long group,
				       int create, struct buffer_head **bhp)
{
	struct nilfs_palloc_cache *cache = NILFS_MDT(inode)->mi_palloc_cache;

	return nilfs_palloc_get_block(inode,
				      nilfs_palloc_desc_blkoff(inode, group),
				      create, nilfs_palloc_desc_block_init,
				      bhp, &cache->prev_desc, &cache->lock);
}

/**
 * nilfs_palloc_get_bitmap_block - get buffer head of a bitmap block
 * @inode: inode of metadata file using this allocator
 * @group: group number
 * @create: create flag
 * @bhp: pointer to store the resultant buffer head
 */
static int nilfs_palloc_get_bitmap_block(struct inode *inode,
					 unsigned long group,
					 int create, struct buffer_head **bhp)
{
	struct nilfs_palloc_cache *cache = NILFS_MDT(inode)->mi_palloc_cache;

	return nilfs_palloc_get_block(inode,
				      nilfs_palloc_bitmap_blkoff(inode, group),
				      create, NULL, bhp,
				      &cache->prev_bitmap, &cache->lock);
}

/**
 * nilfs_palloc_get_entry_block - get buffer head of an entry block
 * @inode: inode of metadata file using this allocator
 * @nr: serial number of the entry (e.g. inode number)
 * @create: create flag
 * @bhp: pointer to store the resultant buffer head
 */
int nilfs_palloc_get_entry_block(struct inode *inode, __u64 nr,
				 int create, struct buffer_head **bhp)
{
	struct nilfs_palloc_cache *cache = NILFS_MDT(inode)->mi_palloc_cache;

	return nilfs_palloc_get_block(inode,
				      nilfs_palloc_entry_blkoff(inode, nr),
				      create, NULL, bhp,
				      &cache->prev_entry, &cache->lock);
}

/**
 * nilfs_palloc_block_get_group_desc - get kernel address of a group descriptor
 * @inode: inode of metadata file using this allocator
 * @group: group number
 * @bh: buffer head of the buffer storing the group descriptor block
 * @kaddr: kernel address mapped for the page including the buffer
 */
static struct nilfs_palloc_group_desc *
nilfs_palloc_block_get_group_desc(const struct inode *inode,
				  unsigned long group,
				  const struct buffer_head *bh, void *kaddr)
{
	return (struct nilfs_palloc_group_desc *)(kaddr + bh_offset(bh)) +
		group % nilfs_palloc_groups_per_desc_block(inode);
}

/**
 * nilfs_palloc_block_get_entry - get kernel address of an entry
 * @inode: inode of metadata file using this allocator
 * @nr: serial number of the entry (e.g. inode number)
 * @bh: buffer head of the buffer storing the entry block
 * @kaddr: kernel address mapped for the page including the buffer
 */
void *nilfs_palloc_block_get_entry(const struct inode *inode, __u64 nr,
				   const struct buffer_head *bh, void *kaddr)
{
	unsigned long entry_offset, group_offset;

	nilfs_palloc_group(inode, nr, &group_offset);
	entry_offset = group_offset % NILFS_MDT(inode)->mi_entries_per_block;

	return kaddr + bh_offset(bh) +
		entry_offset * NILFS_MDT(inode)->mi_entry_size;
}

/**
 * nilfs_palloc_find_available_slot - find available slot in a group
 * @inode: inode of metadata file using this allocator
 * @group: group number
 * @target: offset number of an entry in the group (start point)
 * @bitmap: bitmap of the group
 * @bsize: size in bits
 */
static int nilfs_palloc_find_available_slot(struct inode *inode,
					    unsigned long group,
					    unsigned long target,
					    unsigned char *bitmap,
					    int bsize)
{
	int curr, pos, end, i;

	if (target > 0) {
		end = (target + BITS_PER_LONG - 1) & ~(BITS_PER_LONG - 1);
		if (end > bsize)
			end = bsize;
		pos = nilfs_find_next_zero_bit(bitmap, end, target);
		if (pos < end &&
		    !nilfs_set_bit_atomic(
			    nilfs_mdt_bgl_lock(inode, group), pos, bitmap))
			return pos;
	} else
		end = 0;

	for (i = 0, curr = end;
	     i < bsize;
	     i += BITS_PER_LONG, curr += BITS_PER_LONG) {
		/* wrap around */
		if (curr >= bsize)
			curr = 0;
		while (*((unsigned long *)bitmap + curr / BITS_PER_LONG)
		       != ~0UL) {
			end = curr + BITS_PER_LONG;
			if (end > bsize)
				end = bsize;
			pos = nilfs_find_next_zero_bit(bitmap, end, curr);
			if ((pos < end) &&
			    !nilfs_set_bit_atomic(
				    nilfs_mdt_bgl_lock(inode, group), pos,
				    bitmap))
				return pos;
		}
	}
	return -ENOSPC;
}

/**
 * nilfs_palloc_rest_groups_in_desc_block - get the remaining number of groups
 *					    in a group descriptor block
 * @inode: inode of metadata file using this allocator
 * @curr: current group number
 * @max: maximum number of groups
 */
static unsigned long
nilfs_palloc_rest_groups_in_desc_block(const struct inode *inode,
				       unsigned long curr, unsigned long max)
{
	return min_t(unsigned long,
		     nilfs_palloc_groups_per_desc_block(inode) -
		     curr % nilfs_palloc_groups_per_desc_block(inode),
		     max - curr + 1);
}

/**
 * nilfs_palloc_count_desc_blocks - count descriptor blocks number
 * @inode: inode of metadata file using this allocator
 * @desc_blocks: descriptor blocks number [out]
 */
static int nilfs_palloc_count_desc_blocks(struct inode *inode,
					    unsigned long *desc_blocks)
{
	__u64 blknum;
	int ret;

	ret = nilfs_bmap_last_key(NILFS_I(inode)->i_bmap, &blknum);
	if (likely(!ret))
		*desc_blocks = DIV_ROUND_UP(
			(unsigned long)blknum,
			NILFS_MDT(inode)->mi_blocks_per_desc_block);
	return ret;
}

/**
 * nilfs_palloc_mdt_file_can_grow - check potential opportunity for
 *					MDT file growing
 * @inode: inode of metadata file using this allocator
 * @desc_blocks: known current descriptor blocks count
 */
static inline bool nilfs_palloc_mdt_file_can_grow(struct inode *inode,
						    unsigned long desc_blocks)
{
	return (nilfs_palloc_groups_per_desc_block(inode) * desc_blocks) <
			nilfs_palloc_groups_count(inode);
}

/**
 * nilfs_palloc_count_max_entries - count max number of entries that can be
 *					described by descriptor blocks count
 * @inode: inode of metadata file using this allocator
 * @nused: current number of used entries
 * @nmaxp: max number of entries [out]
 */
int nilfs_palloc_count_max_entries(struct inode *inode, u64 nused, u64 *nmaxp)
{
	unsigned long desc_blocks = 0;
	u64 entries_per_desc_block, nmax;
	int err;

	err = nilfs_palloc_count_desc_blocks(inode, &desc_blocks);
	if (unlikely(err))
		return err;

	entries_per_desc_block = (u64)nilfs_palloc_entries_per_group(inode) *
				nilfs_palloc_groups_per_desc_block(inode);
	nmax = entries_per_desc_block * desc_blocks;

	if (nused == nmax &&
			nilfs_palloc_mdt_file_can_grow(inode, desc_blocks))
		nmax += entries_per_desc_block;

	if (nused > nmax)
		return -ERANGE;

	*nmaxp = nmax;
	return 0;
}

/**
 * nilfs_palloc_prepare_alloc_entry - prepare to allocate a persistent object
 * @inode: inode of metadata file using this allocator
 * @req: nilfs_palloc_req structure exchanged for the allocation
 */
int nilfs_palloc_prepare_alloc_entry(struct inode *inode,
				     struct nilfs_palloc_req *req)
{
	struct buffer_head *desc_bh, *bitmap_bh;
	struct nilfs_palloc_group_desc *desc;
	unsigned char *bitmap;
	void *desc_kaddr, *bitmap_kaddr;
	unsigned long group, maxgroup, ngroups;
	unsigned long group_offset, maxgroup_offset;
	unsigned long n, entries_per_group, groups_per_desc_block;
	unsigned long i, j;
	int pos, ret;

	ngroups = nilfs_palloc_groups_count(inode);
	maxgroup = ngroups - 1;
	group = nilfs_palloc_group(inode, req->pr_entry_nr, &group_offset);
	entries_per_group = nilfs_palloc_entries_per_group(inode);
	groups_per_desc_block = nilfs_palloc_groups_per_desc_block(inode);

	for (i = 0; i < ngroups; i += n) {
		if (group >= ngroups) {
			/* wrap around */
			group = 0;
			maxgroup = nilfs_palloc_group(inode, req->pr_entry_nr,
						      &maxgroup_offset) - 1;
		}
		ret = nilfs_palloc_get_desc_block(inode, group, 1, &desc_bh);
		if (ret < 0)
			return ret;
		desc_kaddr = kmap(desc_bh->b_page);
		desc = nilfs_palloc_block_get_group_desc(
			inode, group, desc_bh, desc_kaddr);
		n = nilfs_palloc_rest_groups_in_desc_block(inode, group,
							   maxgroup);
		for (j = 0; j < n; j++, desc++, group++) {
			if (nilfs_palloc_group_desc_nfrees(inode, group, desc)
			    > 0) {
				ret = nilfs_palloc_get_bitmap_block(
					inode, group, 1, &bitmap_bh);
				if (ret < 0)
					goto out_desc;
				bitmap_kaddr = kmap(bitmap_bh->b_page);
				bitmap = bitmap_kaddr + bh_offset(bitmap_bh);
				pos = nilfs_palloc_find_available_slot(
					inode, group, group_offset, bitmap,
					entries_per_group);
				if (pos >= 0) {
					/* found a free entry */
					nilfs_palloc_group_desc_add_entries(
						inode, group, desc, -1);
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

	/* no entries left */
	return -ENOSPC;

 out_desc:
	kunmap(desc_bh->b_page);
	brelse(desc_bh);
	return ret;
}

/**
 * nilfs_palloc_commit_alloc_entry - finish allocation of a persistent object
 * @inode: inode of metadata file using this allocator
 * @req: nilfs_palloc_req structure exchanged for the allocation
 */
void nilfs_palloc_commit_alloc_entry(struct inode *inode,
				     struct nilfs_palloc_req *req)
{
	mark_buffer_dirty(req->pr_bitmap_bh);
	mark_buffer_dirty(req->pr_desc_bh);
	nilfs_mdt_mark_dirty(inode);

	brelse(req->pr_bitmap_bh);
	brelse(req->pr_desc_bh);
}

/**
 * nilfs_palloc_commit_free_entry - finish deallocating a persistent object
 * @inode: inode of metadata file using this allocator
 * @req: nilfs_palloc_req structure exchanged for the removal
 */
void nilfs_palloc_commit_free_entry(struct inode *inode,
				    struct nilfs_palloc_req *req)
{
	struct nilfs_palloc_group_desc *desc;
	unsigned long group, group_offset;
	unsigned char *bitmap;
	void *desc_kaddr, *bitmap_kaddr;

	group = nilfs_palloc_group(inode, req->pr_entry_nr, &group_offset);
	desc_kaddr = kmap(req->pr_desc_bh->b_page);
	desc = nilfs_palloc_block_get_group_desc(inode, group,
						 req->pr_desc_bh, desc_kaddr);
	bitmap_kaddr = kmap(req->pr_bitmap_bh->b_page);
	bitmap = bitmap_kaddr + bh_offset(req->pr_bitmap_bh);

	if (!nilfs_clear_bit_atomic(nilfs_mdt_bgl_lock(inode, group),
				    group_offset, bitmap))
		printk(KERN_WARNING "%s: entry number %llu already freed\n",
		       __func__, (unsigned long long)req->pr_entry_nr);
	else
		nilfs_palloc_group_desc_add_entries(inode, group, desc, 1);

	kunmap(req->pr_bitmap_bh->b_page);
	kunmap(req->pr_desc_bh->b_page);

	mark_buffer_dirty(req->pr_desc_bh);
	mark_buffer_dirty(req->pr_bitmap_bh);
	nilfs_mdt_mark_dirty(inode);

	brelse(req->pr_bitmap_bh);
	brelse(req->pr_desc_bh);
}

/**
 * nilfs_palloc_abort_alloc_entry - cancel allocation of a persistent object
 * @inode: inode of metadata file using this allocator
 * @req: nilfs_palloc_req structure exchanged for the allocation
 */
void nilfs_palloc_abort_alloc_entry(struct inode *inode,
				    struct nilfs_palloc_req *req)
{
	struct nilfs_palloc_group_desc *desc;
	void *desc_kaddr, *bitmap_kaddr;
	unsigned char *bitmap;
	unsigned long group, group_offset;

	group = nilfs_palloc_group(inode, req->pr_entry_nr, &group_offset);
	desc_kaddr = kmap(req->pr_desc_bh->b_page);
	desc = nilfs_palloc_block_get_group_desc(inode, group,
						 req->pr_desc_bh, desc_kaddr);
	bitmap_kaddr = kmap(req->pr_bitmap_bh->b_page);
	bitmap = bitmap_kaddr + bh_offset(req->pr_bitmap_bh);
	if (!nilfs_clear_bit_atomic(nilfs_mdt_bgl_lock(inode, group),
				    group_offset, bitmap))
		printk(KERN_WARNING "%s: entry number %llu already freed\n",
		       __func__, (unsigned long long)req->pr_entry_nr);
	else
		nilfs_palloc_group_desc_add_entries(inode, group, desc, 1);

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
 * @inode: inode of metadata file using this allocator
 * @req: nilfs_palloc_req structure exchanged for the removal
 */
int nilfs_palloc_prepare_free_entry(struct inode *inode,
				    struct nilfs_palloc_req *req)
{
	struct buffer_head *desc_bh, *bitmap_bh;
	unsigned long group, group_offset;
	int ret;

	group = nilfs_palloc_group(inode, req->pr_entry_nr, &group_offset);
	ret = nilfs_palloc_get_desc_block(inode, group, 1, &desc_bh);
	if (ret < 0)
		return ret;
	ret = nilfs_palloc_get_bitmap_block(inode, group, 1, &bitmap_bh);
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
 * @inode: inode of metadata file using this allocator
 * @req: nilfs_palloc_req structure exchanged for the removal
 */
void nilfs_palloc_abort_free_entry(struct inode *inode,
				   struct nilfs_palloc_req *req)
{
	brelse(req->pr_bitmap_bh);
	brelse(req->pr_desc_bh);

	req->pr_entry_nr = 0;
	req->pr_bitmap_bh = NULL;
	req->pr_desc_bh = NULL;
}

/**
 * nilfs_palloc_group_is_in - judge if an entry is in a group
 * @inode: inode of metadata file using this allocator
 * @group: group number
 * @nr: serial number of the entry (e.g. inode number)
 */
static int
nilfs_palloc_group_is_in(struct inode *inode, unsigned long group, __u64 nr)
{
	__u64 first, last;

	first = group * nilfs_palloc_entries_per_group(inode);
	last = first + nilfs_palloc_entries_per_group(inode) - 1;
	return (nr >= first) && (nr <= last);
}

/**
 * nilfs_palloc_freev - deallocate a set of persistent objects
 * @inode: inode of metadata file using this allocator
 * @entry_nrs: array of entry numbers to be deallocated
 * @nitems: number of entries stored in @entry_nrs
 */
int nilfs_palloc_freev(struct inode *inode, __u64 *entry_nrs, size_t nitems)
{
	struct buffer_head *desc_bh, *bitmap_bh;
	struct nilfs_palloc_group_desc *desc;
	unsigned char *bitmap;
	void *desc_kaddr, *bitmap_kaddr;
	unsigned long group, group_offset;
	int i, j, n, ret;

	for (i = 0; i < nitems; i = j) {
		group = nilfs_palloc_group(inode, entry_nrs[i], &group_offset);
		ret = nilfs_palloc_get_desc_block(inode, group, 0, &desc_bh);
		if (ret < 0)
			return ret;
		ret = nilfs_palloc_get_bitmap_block(inode, group, 0,
						    &bitmap_bh);
		if (ret < 0) {
			brelse(desc_bh);
			return ret;
		}
		desc_kaddr = kmap(desc_bh->b_page);
		desc = nilfs_palloc_block_get_group_desc(
			inode, group, desc_bh, desc_kaddr);
		bitmap_kaddr = kmap(bitmap_bh->b_page);
		bitmap = bitmap_kaddr + bh_offset(bitmap_bh);
		for (j = i, n = 0;
		     (j < nitems) && nilfs_palloc_group_is_in(inode, group,
							      entry_nrs[j]);
		     j++) {
			nilfs_palloc_group(inode, entry_nrs[j], &group_offset);
			if (!nilfs_clear_bit_atomic(
				    nilfs_mdt_bgl_lock(inode, group),
				    group_offset, bitmap)) {
				printk(KERN_WARNING
				       "%s: entry number %llu already freed\n",
				       __func__,
				       (unsigned long long)entry_nrs[j]);
			} else {
				n++;
			}
		}
		nilfs_palloc_group_desc_add_entries(inode, group, desc, n);

		kunmap(bitmap_bh->b_page);
		kunmap(desc_bh->b_page);

		mark_buffer_dirty(desc_bh);
		mark_buffer_dirty(bitmap_bh);
		nilfs_mdt_mark_dirty(inode);

		brelse(bitmap_bh);
		brelse(desc_bh);
	}
	return 0;
}

void nilfs_palloc_setup_cache(struct inode *inode,
			      struct nilfs_palloc_cache *cache)
{
	NILFS_MDT(inode)->mi_palloc_cache = cache;
	spin_lock_init(&cache->lock);
}

void nilfs_palloc_clear_cache(struct inode *inode)
{
	struct nilfs_palloc_cache *cache = NILFS_MDT(inode)->mi_palloc_cache;

	spin_lock(&cache->lock);
	brelse(cache->prev_desc.bh);
	brelse(cache->prev_bitmap.bh);
	brelse(cache->prev_entry.bh);
	cache->prev_desc.bh = NULL;
	cache->prev_bitmap.bh = NULL;
	cache->prev_entry.bh = NULL;
	spin_unlock(&cache->lock);
}

void nilfs_palloc_destroy_cache(struct inode *inode)
{
	nilfs_palloc_clear_cache(inode);
	NILFS_MDT(inode)->mi_palloc_cache = NULL;
}
