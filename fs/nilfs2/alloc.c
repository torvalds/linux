// SPDX-License-Identifier: GPL-2.0+
/*
 * NILFS dat/inode allocator
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
 * @inode: inode of metadata file using this allocator
 *
 * Return: Number of groups that a group descriptor block can maintain.
 */
static inline unsigned long
nilfs_palloc_groups_per_desc_block(const struct inode *inode)
{
	return i_blocksize(inode) /
		sizeof(struct nilfs_palloc_group_desc);
}

/**
 * nilfs_palloc_groups_count - get maximum number of groups
 * @inode: inode of metadata file using this allocator
 *
 * Return: Maximum number of groups.
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
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int nilfs_palloc_init_blockgroup(struct inode *inode, unsigned int entry_size)
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
		/*
		 * Number of blocks in a group including entry blocks
		 * and a bitmap block
		 */
	mi->mi_blocks_per_desc_block =
		nilfs_palloc_groups_per_desc_block(inode) *
		mi->mi_blocks_per_group + 1;
		/*
		 * Number of blocks per descriptor including the
		 * descriptor block
		 */
	return 0;
}

/**
 * nilfs_palloc_group - get group number and offset from an entry number
 * @inode: inode of metadata file using this allocator
 * @nr: serial number of the entry (e.g. inode number)
 * @offset: pointer to store offset number in the group
 *
 * Return: Number of the group that contains the entry with the index
 * specified by @nr.
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
 * Return: Index number in the metadata file of the descriptor block of
 * the group specified by @group.
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
 *
 * Return: Index number in the metadata file of the bitmap block of
 * the group specified by @group.
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
 * @desc: pointer to descriptor structure for the group
 * @lock: spin lock protecting @desc
 *
 * Return: Number of free entries written in the group descriptor @desc.
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
 *
 * Return: Number of free entries after adjusting the group descriptor
 * @desc.
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
 * @inode: inode of metadata file using this allocator
 * @nr: serial number of the entry (e.g. inode number)
 *
 * Return: Index number in the metadata file of the block containing
 * the entry specified by @nr.
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
 * @from: kernel address mapped for a chunk of the block
 *
 * This function does not yet support the case where block size > PAGE_SIZE.
 */
static void nilfs_palloc_desc_block_init(struct inode *inode,
					 struct buffer_head *bh, void *from)
{
	struct nilfs_palloc_group_desc *desc = from;
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
	if (prev->bh && blkoff == prev->blkoff &&
	    likely(buffer_uptodate(prev->bh))) {
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
 * nilfs_palloc_delete_block - delete a block on the persistent allocator file
 * @inode: inode of metadata file using this allocator
 * @blkoff: block offset
 * @prev: nilfs_bh_assoc struct of the last used buffer
 * @lock: spin lock protecting @prev
 *
 * Return: 0 on success, or one of the following negative error codes on
 * failure:
 * * %-EIO	- I/O error (including metadata corruption).
 * * %-ENOENT	- Non-existent block.
 * * %-ENOMEM	- Insufficient memory available.
 */
static int nilfs_palloc_delete_block(struct inode *inode, unsigned long blkoff,
				     struct nilfs_bh_assoc *prev,
				     spinlock_t *lock)
{
	spin_lock(lock);
	if (prev->bh && blkoff == prev->blkoff) {
		brelse(prev->bh);
		prev->bh = NULL;
	}
	spin_unlock(lock);
	return nilfs_mdt_delete_block(inode, blkoff);
}

/**
 * nilfs_palloc_get_desc_block - get buffer head of a group descriptor block
 * @inode: inode of metadata file using this allocator
 * @group: group number
 * @create: create flag
 * @bhp: pointer to store the resultant buffer head
 *
 * Return: 0 on success, or a negative error code on failure.
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
 *
 * Return: 0 on success, or a negative error code on failure.
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
 * nilfs_palloc_delete_bitmap_block - delete a bitmap block
 * @inode: inode of metadata file using this allocator
 * @group: group number
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int nilfs_palloc_delete_bitmap_block(struct inode *inode,
					    unsigned long group)
{
	struct nilfs_palloc_cache *cache = NILFS_MDT(inode)->mi_palloc_cache;

	return nilfs_palloc_delete_block(inode,
					 nilfs_palloc_bitmap_blkoff(inode,
								    group),
					 &cache->prev_bitmap, &cache->lock);
}

/**
 * nilfs_palloc_get_entry_block - get buffer head of an entry block
 * @inode: inode of metadata file using this allocator
 * @nr: serial number of the entry (e.g. inode number)
 * @create: create flag
 * @bhp: pointer to store the resultant buffer head
 *
 * Return: 0 on success, or a negative error code on failure.
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
 * nilfs_palloc_delete_entry_block - delete an entry block
 * @inode: inode of metadata file using this allocator
 * @nr: serial number of the entry
 *
 * Return: 0 on success, or a negative error code on failure.
 */
static int nilfs_palloc_delete_entry_block(struct inode *inode, __u64 nr)
{
	struct nilfs_palloc_cache *cache = NILFS_MDT(inode)->mi_palloc_cache;

	return nilfs_palloc_delete_block(inode,
					 nilfs_palloc_entry_blkoff(inode, nr),
					 &cache->prev_entry, &cache->lock);
}

/**
 * nilfs_palloc_group_desc_offset - calculate the byte offset of a group
 *                                  descriptor in the folio containing it
 * @inode: inode of metadata file using this allocator
 * @group: group number
 * @bh:    buffer head of the group descriptor block
 *
 * Return: Byte offset in the folio of the group descriptor for @group.
 */
static size_t nilfs_palloc_group_desc_offset(const struct inode *inode,
					     unsigned long group,
					     const struct buffer_head *bh)
{
	return offset_in_folio(bh->b_folio, bh->b_data) +
		sizeof(struct nilfs_palloc_group_desc) *
		(group % nilfs_palloc_groups_per_desc_block(inode));
}

/**
 * nilfs_palloc_bitmap_offset - calculate the byte offset of a bitmap block
 *                              in the folio containing it
 * @bh: buffer head of the bitmap block
 *
 * Return: Byte offset in the folio of the bitmap block for @bh.
 */
static size_t nilfs_palloc_bitmap_offset(const struct buffer_head *bh)
{
	return offset_in_folio(bh->b_folio, bh->b_data);
}

/**
 * nilfs_palloc_entry_offset - calculate the byte offset of an entry in the
 *                             folio containing it
 * @inode: inode of metadata file using this allocator
 * @nr:    serial number of the entry (e.g. inode number)
 * @bh:    buffer head of the entry block
 *
 * Return: Byte offset in the folio of the entry @nr.
 */
size_t nilfs_palloc_entry_offset(const struct inode *inode, __u64 nr,
				 const struct buffer_head *bh)
{
	unsigned long entry_index_in_group, entry_index_in_block;

	nilfs_palloc_group(inode, nr, &entry_index_in_group);
	entry_index_in_block = entry_index_in_group %
		NILFS_MDT(inode)->mi_entries_per_block;

	return offset_in_folio(bh->b_folio, bh->b_data) +
		entry_index_in_block * NILFS_MDT(inode)->mi_entry_size;
}

/**
 * nilfs_palloc_find_available_slot - find available slot in a group
 * @bitmap: bitmap of the group
 * @target: offset number of an entry in the group (start point)
 * @bsize: size in bits
 * @lock: spin lock protecting @bitmap
 * @wrap: whether to wrap around
 *
 * Return: Offset number within the group of the found free entry, or
 * %-ENOSPC if not found.
 */
static int nilfs_palloc_find_available_slot(unsigned char *bitmap,
					    unsigned long target,
					    unsigned int bsize,
					    spinlock_t *lock, bool wrap)
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
	if (!wrap)
		return -ENOSPC;

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
 * @inode: inode of metadata file using this allocator
 * @curr: current group number
 * @max: maximum number of groups
 *
 * Return: Number of remaining descriptors (= groups) managed by the descriptor
 * block.
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
 *
 * Return: 0 on success, or a negative error code on failure.
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
 *
 * Return: true if a group can be added in the metadata file, false if not.
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
 *
 * Return: 0 on success, or one of the following negative error codes on
 * failure:
 * * %-EIO	- I/O error (including metadata corruption).
 * * %-ENOMEM	- Insufficient memory available.
 * * %-ERANGE	- Number of entries in use is out of range.
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
 * @wrap: whether to wrap around
 *
 * Return: 0 on success, or one of the following negative error codes on
 * failure:
 * * %-EIO	- I/O error (including metadata corruption).
 * * %-ENOMEM	- Insufficient memory available.
 * * %-ENOSPC	- Entries exhausted (No entries available for allocation).
 * * %-EROFS	- Read only filesystem
 */
int nilfs_palloc_prepare_alloc_entry(struct inode *inode,
				     struct nilfs_palloc_req *req, bool wrap)
{
	struct buffer_head *desc_bh, *bitmap_bh;
	struct nilfs_palloc_group_desc *desc;
	unsigned char *bitmap;
	size_t doff, boff;
	unsigned long group, maxgroup, ngroups;
	unsigned long group_offset, maxgroup_offset;
	unsigned long n, entries_per_group;
	unsigned long i, j;
	spinlock_t *lock;
	int pos, ret;

	ngroups = nilfs_palloc_groups_count(inode);
	maxgroup = ngroups - 1;
	group = nilfs_palloc_group(inode, req->pr_entry_nr, &group_offset);
	entries_per_group = nilfs_palloc_entries_per_group(inode);

	for (i = 0; i < ngroups; i += n) {
		if (group >= ngroups && wrap) {
			/* wrap around */
			group = 0;
			maxgroup = nilfs_palloc_group(inode, req->pr_entry_nr,
						      &maxgroup_offset) - 1;
		}
		ret = nilfs_palloc_get_desc_block(inode, group, 1, &desc_bh);
		if (ret < 0)
			return ret;

		doff = nilfs_palloc_group_desc_offset(inode, group, desc_bh);
		desc = kmap_local_folio(desc_bh->b_folio, doff);
		n = nilfs_palloc_rest_groups_in_desc_block(inode, group,
							   maxgroup);
		for (j = 0; j < n; j++, group++, group_offset = 0) {
			lock = nilfs_mdt_bgl_lock(inode, group);
			if (nilfs_palloc_group_desc_nfrees(&desc[j], lock) == 0)
				continue;

			kunmap_local(desc);
			ret = nilfs_palloc_get_bitmap_block(inode, group, 1,
							    &bitmap_bh);
			if (unlikely(ret < 0)) {
				brelse(desc_bh);
				return ret;
			}

			/*
			 * Re-kmap the folio containing the first (and
			 * subsequent) group descriptors.
			 */
			desc = kmap_local_folio(desc_bh->b_folio, doff);

			boff = nilfs_palloc_bitmap_offset(bitmap_bh);
			bitmap = kmap_local_folio(bitmap_bh->b_folio, boff);
			pos = nilfs_palloc_find_available_slot(
				bitmap, group_offset, entries_per_group, lock,
				wrap);
			/*
			 * Since the search for a free slot in the second and
			 * subsequent bitmap blocks always starts from the
			 * beginning, the wrap flag only has an effect on the
			 * first search.
			 */
			kunmap_local(bitmap);
			if (pos >= 0)
				goto found;

			brelse(bitmap_bh);
		}

		kunmap_local(desc);
		brelse(desc_bh);
	}

	/* no entries left */
	return -ENOSPC;

found:
	/* found a free entry */
	nilfs_palloc_group_desc_add_entries(&desc[j], lock, -1);
	req->pr_entry_nr = entries_per_group * group + pos;
	kunmap_local(desc);

	req->pr_desc_bh = desc_bh;
	req->pr_bitmap_bh = bitmap_bh;
	return 0;
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
	unsigned long group, group_offset;
	size_t doff, boff;
	struct nilfs_palloc_group_desc *desc;
	unsigned char *bitmap;
	spinlock_t *lock;

	group = nilfs_palloc_group(inode, req->pr_entry_nr, &group_offset);
	doff = nilfs_palloc_group_desc_offset(inode, group, req->pr_desc_bh);
	desc = kmap_local_folio(req->pr_desc_bh->b_folio, doff);

	boff = nilfs_palloc_bitmap_offset(req->pr_bitmap_bh);
	bitmap = kmap_local_folio(req->pr_bitmap_bh->b_folio, boff);
	lock = nilfs_mdt_bgl_lock(inode, group);

	if (!nilfs_clear_bit_atomic(lock, group_offset, bitmap))
		nilfs_warn(inode->i_sb,
			   "%s (ino=%lu): entry number %llu already freed",
			   __func__, inode->i_ino,
			   (unsigned long long)req->pr_entry_nr);
	else
		nilfs_palloc_group_desc_add_entries(desc, lock, 1);

	kunmap_local(bitmap);
	kunmap_local(desc);

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
	size_t doff, boff;
	unsigned char *bitmap;
	unsigned long group, group_offset;
	spinlock_t *lock;

	group = nilfs_palloc_group(inode, req->pr_entry_nr, &group_offset);
	doff = nilfs_palloc_group_desc_offset(inode, group, req->pr_desc_bh);
	desc = kmap_local_folio(req->pr_desc_bh->b_folio, doff);

	boff = nilfs_palloc_bitmap_offset(req->pr_bitmap_bh);
	bitmap = kmap_local_folio(req->pr_bitmap_bh->b_folio, boff);
	lock = nilfs_mdt_bgl_lock(inode, group);

	if (!nilfs_clear_bit_atomic(lock, group_offset, bitmap))
		nilfs_warn(inode->i_sb,
			   "%s (ino=%lu): entry number %llu already freed",
			   __func__, inode->i_ino,
			   (unsigned long long)req->pr_entry_nr);
	else
		nilfs_palloc_group_desc_add_entries(desc, lock, 1);

	kunmap_local(bitmap);
	kunmap_local(desc);

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
 *
 * Return: 0 on success, or a negative error code on failure.
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
 * nilfs_palloc_freev - deallocate a set of persistent objects
 * @inode: inode of metadata file using this allocator
 * @entry_nrs: array of entry numbers to be deallocated
 * @nitems: number of entries stored in @entry_nrs
 *
 * Return: 0 on success, or a negative error code on failure.
 */
int nilfs_palloc_freev(struct inode *inode, __u64 *entry_nrs, size_t nitems)
{
	struct buffer_head *desc_bh, *bitmap_bh;
	struct nilfs_palloc_group_desc *desc;
	unsigned char *bitmap;
	size_t doff, boff;
	unsigned long group, group_offset;
	__u64 group_min_nr, last_nrs[8];
	const unsigned long epg = nilfs_palloc_entries_per_group(inode);
	const unsigned int epb = NILFS_MDT(inode)->mi_entries_per_block;
	unsigned int entry_start, end, pos;
	spinlock_t *lock;
	int i, j, k, ret;
	u32 nfree;

	for (i = 0; i < nitems; i = j) {
		int change_group = false;
		int nempties = 0, n = 0;

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

		/* Get the first entry number of the group */
		group_min_nr = (__u64)group * epg;

		boff = nilfs_palloc_bitmap_offset(bitmap_bh);
		bitmap = kmap_local_folio(bitmap_bh->b_folio, boff);
		lock = nilfs_mdt_bgl_lock(inode, group);

		j = i;
		entry_start = rounddown(group_offset, epb);
		do {
			if (!nilfs_clear_bit_atomic(lock, group_offset,
						    bitmap)) {
				nilfs_warn(inode->i_sb,
					   "%s (ino=%lu): entry number %llu already freed",
					   __func__, inode->i_ino,
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

			/* Test if the entry block is empty or not */
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

		kunmap_local(bitmap);
		mark_buffer_dirty(bitmap_bh);
		brelse(bitmap_bh);

		for (k = 0; k < nempties; k++) {
			ret = nilfs_palloc_delete_entry_block(inode,
							      last_nrs[k]);
			if (ret && ret != -ENOENT)
				nilfs_warn(inode->i_sb,
					   "error %d deleting block that object (entry=%llu, ino=%lu) belongs to",
					   ret, (unsigned long long)last_nrs[k],
					   inode->i_ino);
		}

		doff = nilfs_palloc_group_desc_offset(inode, group, desc_bh);
		desc = kmap_local_folio(desc_bh->b_folio, doff);
		nfree = nilfs_palloc_group_desc_add_entries(desc, lock, n);
		kunmap_local(desc);
		mark_buffer_dirty(desc_bh);
		nilfs_mdt_mark_dirty(inode);
		brelse(desc_bh);

		if (nfree == nilfs_palloc_entries_per_group(inode)) {
			ret = nilfs_palloc_delete_bitmap_block(inode, group);
			if (ret && ret != -ENOENT)
				nilfs_warn(inode->i_sb,
					   "error %d deleting bitmap block of group=%lu, ino=%lu",
					   ret, group, inode->i_ino);
		}
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
