// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfsplus/extents.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 *
 * Handling of Extents both in catalog and extents overflow trees
 */

#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/pagemap.h>

#include "hfsplus_fs.h"
#include "hfsplus_raw.h"

/* Compare two extents keys, returns 0 on same, pos/neg for difference */
int hfsplus_ext_cmp_key(const hfsplus_btree_key *k1,
			const hfsplus_btree_key *k2)
{
	__be32 k1id, k2id;
	__be32 k1s, k2s;

	k1id = k1->ext.cnid;
	k2id = k2->ext.cnid;
	if (k1id != k2id)
		return be32_to_cpu(k1id) < be32_to_cpu(k2id) ? -1 : 1;

	if (k1->ext.fork_type != k2->ext.fork_type)
		return k1->ext.fork_type < k2->ext.fork_type ? -1 : 1;

	k1s = k1->ext.start_block;
	k2s = k2->ext.start_block;
	if (k1s == k2s)
		return 0;
	return be32_to_cpu(k1s) < be32_to_cpu(k2s) ? -1 : 1;
}

static void hfsplus_ext_build_key(hfsplus_btree_key *key, u32 cnid,
				  u32 block, u8 type)
{
	key->key_len = cpu_to_be16(HFSPLUS_EXT_KEYLEN - 2);
	key->ext.cnid = cpu_to_be32(cnid);
	key->ext.start_block = cpu_to_be32(block);
	key->ext.fork_type = type;
	key->ext.pad = 0;
}

static u32 hfsplus_ext_find_block(struct hfsplus_extent *ext, u32 off)
{
	int i;
	u32 count;

	for (i = 0; i < 8; ext++, i++) {
		count = be32_to_cpu(ext->block_count);
		if (off < count)
			return be32_to_cpu(ext->start_block) + off;
		off -= count;
	}
	/* panic? */
	return 0;
}

static int hfsplus_ext_block_count(struct hfsplus_extent *ext)
{
	int i;
	u32 count = 0;

	for (i = 0; i < 8; ext++, i++)
		count += be32_to_cpu(ext->block_count);
	return count;
}

static u32 hfsplus_ext_lastblock(struct hfsplus_extent *ext)
{
	int i;

	ext += 7;
	for (i = 0; i < 7; ext--, i++)
		if (ext->block_count)
			break;
	return be32_to_cpu(ext->start_block) + be32_to_cpu(ext->block_count);
}

static int __hfsplus_ext_write_extent(struct inode *inode,
		struct hfs_find_data *fd)
{
	struct hfsplus_inode_info *hip = HFSPLUS_I(inode);
	int res;

	WARN_ON(!mutex_is_locked(&hip->extents_lock));

	hfsplus_ext_build_key(fd->search_key, inode->i_ino, hip->cached_start,
			      HFSPLUS_IS_RSRC(inode) ?
				HFSPLUS_TYPE_RSRC : HFSPLUS_TYPE_DATA);

	res = hfs_brec_find(fd, hfs_find_rec_by_key);
	if (hip->extent_state & HFSPLUS_EXT_NEW) {
		if (res != -ENOENT)
			return res;
		/* Fail early and avoid ENOSPC during the btree operation */
		res = hfs_bmap_reserve(fd->tree, fd->tree->depth + 1);
		if (res)
			return res;
		hfs_brec_insert(fd, hip->cached_extents,
				sizeof(hfsplus_extent_rec));
		hip->extent_state &= ~(HFSPLUS_EXT_DIRTY | HFSPLUS_EXT_NEW);
	} else {
		if (res)
			return res;
		hfs_bnode_write(fd->bnode, hip->cached_extents,
				fd->entryoffset, fd->entrylength);
		hip->extent_state &= ~HFSPLUS_EXT_DIRTY;
	}

	/*
	 * We can't just use hfsplus_mark_inode_dirty here, because we
	 * also get called from hfsplus_write_inode, which should not
	 * redirty the inode.  Instead the callers have to be careful
	 * to explicily mark the inode dirty, too.
	 */
	set_bit(HFSPLUS_I_EXT_DIRTY, &hip->flags);

	return 0;
}

static int hfsplus_ext_write_extent_locked(struct inode *inode)
{
	int res = 0;

	if (HFSPLUS_I(inode)->extent_state & HFSPLUS_EXT_DIRTY) {
		struct hfs_find_data fd;

		res = hfs_find_init(HFSPLUS_SB(inode->i_sb)->ext_tree, &fd);
		if (res)
			return res;
		res = __hfsplus_ext_write_extent(inode, &fd);
		hfs_find_exit(&fd);
	}
	return res;
}

int hfsplus_ext_write_extent(struct inode *inode)
{
	int res;

	mutex_lock(&HFSPLUS_I(inode)->extents_lock);
	res = hfsplus_ext_write_extent_locked(inode);
	mutex_unlock(&HFSPLUS_I(inode)->extents_lock);

	return res;
}

static inline int __hfsplus_ext_read_extent(struct hfs_find_data *fd,
					    struct hfsplus_extent *extent,
					    u32 cnid, u32 block, u8 type)
{
	int res;

	hfsplus_ext_build_key(fd->search_key, cnid, block, type);
	fd->key->ext.cnid = 0;
	res = hfs_brec_find(fd, hfs_find_rec_by_key);
	if (res && res != -ENOENT)
		return res;
	if (fd->key->ext.cnid != fd->search_key->ext.cnid ||
	    fd->key->ext.fork_type != fd->search_key->ext.fork_type)
		return -ENOENT;
	if (fd->entrylength != sizeof(hfsplus_extent_rec))
		return -EIO;
	hfs_bnode_read(fd->bnode, extent, fd->entryoffset,
		sizeof(hfsplus_extent_rec));
	return 0;
}

static inline int __hfsplus_ext_cache_extent(struct hfs_find_data *fd,
		struct inode *inode, u32 block)
{
	struct hfsplus_inode_info *hip = HFSPLUS_I(inode);
	int res;

	WARN_ON(!mutex_is_locked(&hip->extents_lock));

	if (hip->extent_state & HFSPLUS_EXT_DIRTY) {
		res = __hfsplus_ext_write_extent(inode, fd);
		if (res)
			return res;
	}

	res = __hfsplus_ext_read_extent(fd, hip->cached_extents, inode->i_ino,
					block, HFSPLUS_IS_RSRC(inode) ?
						HFSPLUS_TYPE_RSRC :
						HFSPLUS_TYPE_DATA);
	if (!res) {
		hip->cached_start = be32_to_cpu(fd->key->ext.start_block);
		hip->cached_blocks =
			hfsplus_ext_block_count(hip->cached_extents);
	} else {
		hip->cached_start = hip->cached_blocks = 0;
		hip->extent_state &= ~(HFSPLUS_EXT_DIRTY | HFSPLUS_EXT_NEW);
	}
	return res;
}

static int hfsplus_ext_read_extent(struct inode *inode, u32 block)
{
	struct hfsplus_inode_info *hip = HFSPLUS_I(inode);
	struct hfs_find_data fd;
	int res;

	if (block >= hip->cached_start &&
	    block < hip->cached_start + hip->cached_blocks)
		return 0;

	res = hfs_find_init(HFSPLUS_SB(inode->i_sb)->ext_tree, &fd);
	if (!res) {
		res = __hfsplus_ext_cache_extent(&fd, inode, block);
		hfs_find_exit(&fd);
	}
	return res;
}

/* Get a block at iblock for inode, possibly allocating if create */
int hfsplus_get_block(struct inode *inode, sector_t iblock,
		      struct buffer_head *bh_result, int create)
{
	struct super_block *sb = inode->i_sb;
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	struct hfsplus_inode_info *hip = HFSPLUS_I(inode);
	int res = -EIO;
	u32 ablock, dblock, mask;
	sector_t sector;
	int was_dirty = 0;

	/* Convert inode block to disk allocation block */
	ablock = iblock >> sbi->fs_shift;

	if (iblock >= hip->fs_blocks) {
		if (!create)
			return 0;
		if (iblock > hip->fs_blocks)
			return -EIO;
		if (ablock >= hip->alloc_blocks) {
			res = hfsplus_file_extend(inode, false);
			if (res)
				return res;
		}
	} else
		create = 0;

	if (ablock < hip->first_blocks) {
		dblock = hfsplus_ext_find_block(hip->first_extents, ablock);
		goto done;
	}

	if (inode->i_ino == HFSPLUS_EXT_CNID)
		return -EIO;

	mutex_lock(&hip->extents_lock);

	/*
	 * hfsplus_ext_read_extent will write out a cached extent into
	 * the extents btree.  In that case we may have to mark the inode
	 * dirty even for a pure read of an extent here.
	 */
	was_dirty = (hip->extent_state & HFSPLUS_EXT_DIRTY);
	res = hfsplus_ext_read_extent(inode, ablock);
	if (res) {
		mutex_unlock(&hip->extents_lock);
		return -EIO;
	}
	dblock = hfsplus_ext_find_block(hip->cached_extents,
					ablock - hip->cached_start);
	mutex_unlock(&hip->extents_lock);

done:
	hfs_dbg(EXTENT, "get_block(%lu): %llu - %u\n",
		inode->i_ino, (long long)iblock, dblock);

	mask = (1 << sbi->fs_shift) - 1;
	sector = ((sector_t)dblock << sbi->fs_shift) +
		  sbi->blockoffset + (iblock & mask);
	map_bh(bh_result, sb, sector);

	if (create) {
		set_buffer_new(bh_result);
		hip->phys_size += sb->s_blocksize;
		hip->fs_blocks++;
		inode_add_bytes(inode, sb->s_blocksize);
	}
	if (create || was_dirty)
		mark_inode_dirty(inode);
	return 0;
}

static void hfsplus_dump_extent(struct hfsplus_extent *extent)
{
	int i;

	hfs_dbg(EXTENT, "   ");
	for (i = 0; i < 8; i++)
		hfs_dbg_cont(EXTENT, " %u:%u",
			     be32_to_cpu(extent[i].start_block),
			     be32_to_cpu(extent[i].block_count));
	hfs_dbg_cont(EXTENT, "\n");
}

static int hfsplus_add_extent(struct hfsplus_extent *extent, u32 offset,
			      u32 alloc_block, u32 block_count)
{
	u32 count, start;
	int i;

	hfsplus_dump_extent(extent);
	for (i = 0; i < 8; extent++, i++) {
		count = be32_to_cpu(extent->block_count);
		if (offset == count) {
			start = be32_to_cpu(extent->start_block);
			if (alloc_block != start + count) {
				if (++i >= 8)
					return -ENOSPC;
				extent++;
				extent->start_block = cpu_to_be32(alloc_block);
			} else
				block_count += count;
			extent->block_count = cpu_to_be32(block_count);
			return 0;
		} else if (offset < count)
			break;
		offset -= count;
	}
	/* panic? */
	return -EIO;
}

static int hfsplus_free_extents(struct super_block *sb,
				struct hfsplus_extent *extent,
				u32 offset, u32 block_nr)
{
	u32 count, start;
	int i;
	int err = 0;

	/* Mapping the allocation file may lock the extent tree */
	WARN_ON(mutex_is_locked(&HFSPLUS_SB(sb)->ext_tree->tree_lock));

	hfsplus_dump_extent(extent);
	for (i = 0; i < 8; extent++, i++) {
		count = be32_to_cpu(extent->block_count);
		if (offset == count)
			goto found;
		else if (offset < count)
			break;
		offset -= count;
	}
	/* panic? */
	return -EIO;
found:
	for (;;) {
		start = be32_to_cpu(extent->start_block);
		if (count <= block_nr) {
			err = hfsplus_block_free(sb, start, count);
			if (err) {
				pr_err("can't free extent\n");
				hfs_dbg(EXTENT, " start: %u count: %u\n",
					start, count);
			}
			extent->block_count = 0;
			extent->start_block = 0;
			block_nr -= count;
		} else {
			count -= block_nr;
			err = hfsplus_block_free(sb, start + count, block_nr);
			if (err) {
				pr_err("can't free extent\n");
				hfs_dbg(EXTENT, " start: %u count: %u\n",
					start, count);
			}
			extent->block_count = cpu_to_be32(count);
			block_nr = 0;
		}
		if (!block_nr || !i) {
			/*
			 * Try to free all extents and
			 * return only last error
			 */
			return err;
		}
		i--;
		extent--;
		count = be32_to_cpu(extent->block_count);
	}
}

int hfsplus_free_fork(struct super_block *sb, u32 cnid,
		struct hfsplus_fork_raw *fork, int type)
{
	struct hfs_find_data fd;
	hfsplus_extent_rec ext_entry;
	u32 total_blocks, blocks, start;
	int res, i;

	total_blocks = be32_to_cpu(fork->total_blocks);
	if (!total_blocks)
		return 0;

	blocks = 0;
	for (i = 0; i < 8; i++)
		blocks += be32_to_cpu(fork->extents[i].block_count);

	res = hfsplus_free_extents(sb, fork->extents, blocks, blocks);
	if (res)
		return res;
	if (total_blocks == blocks)
		return 0;

	res = hfs_find_init(HFSPLUS_SB(sb)->ext_tree, &fd);
	if (res)
		return res;
	do {
		res = __hfsplus_ext_read_extent(&fd, ext_entry, cnid,
						total_blocks, type);
		if (res)
			break;
		start = be32_to_cpu(fd.key->ext.start_block);
		hfs_brec_remove(&fd);

		mutex_unlock(&fd.tree->tree_lock);
		hfsplus_free_extents(sb, ext_entry, total_blocks - start,
				     total_blocks);
		total_blocks = start;
		mutex_lock_nested(&fd.tree->tree_lock,
			hfsplus_btree_lock_class(fd.tree));
	} while (total_blocks > blocks);
	hfs_find_exit(&fd);

	return res;
}

int hfsplus_file_extend(struct inode *inode, bool zeroout)
{
	struct super_block *sb = inode->i_sb;
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(sb);
	struct hfsplus_inode_info *hip = HFSPLUS_I(inode);
	u32 start, len, goal;
	int res;

	if (sbi->alloc_file->i_size * 8 <
	    sbi->total_blocks - sbi->free_blocks + 8) {
		/* extend alloc file */
		pr_err_ratelimited("extend alloc file! (%llu,%u,%u)\n",
				   sbi->alloc_file->i_size * 8,
				   sbi->total_blocks, sbi->free_blocks);
		return -ENOSPC;
	}

	mutex_lock(&hip->extents_lock);
	if (hip->alloc_blocks == hip->first_blocks)
		goal = hfsplus_ext_lastblock(hip->first_extents);
	else {
		res = hfsplus_ext_read_extent(inode, hip->alloc_blocks);
		if (res)
			goto out;
		goal = hfsplus_ext_lastblock(hip->cached_extents);
	}

	len = hip->clump_blocks;
	start = hfsplus_block_allocate(sb, sbi->total_blocks, goal, &len);
	if (start >= sbi->total_blocks) {
		start = hfsplus_block_allocate(sb, goal, 0, &len);
		if (start >= goal) {
			res = -ENOSPC;
			goto out;
		}
	}

	if (zeroout) {
		res = sb_issue_zeroout(sb, start, len, GFP_NOFS);
		if (res)
			goto out;
	}

	hfs_dbg(EXTENT, "extend %lu: %u,%u\n", inode->i_ino, start, len);

	if (hip->alloc_blocks <= hip->first_blocks) {
		if (!hip->first_blocks) {
			hfs_dbg(EXTENT, "first extents\n");
			/* no extents yet */
			hip->first_extents[0].start_block = cpu_to_be32(start);
			hip->first_extents[0].block_count = cpu_to_be32(len);
			res = 0;
		} else {
			/* try to append to extents in inode */
			res = hfsplus_add_extent(hip->first_extents,
						 hip->alloc_blocks,
						 start, len);
			if (res == -ENOSPC)
				goto insert_extent;
		}
		if (!res) {
			hfsplus_dump_extent(hip->first_extents);
			hip->first_blocks += len;
		}
	} else {
		res = hfsplus_add_extent(hip->cached_extents,
					 hip->alloc_blocks - hip->cached_start,
					 start, len);
		if (!res) {
			hfsplus_dump_extent(hip->cached_extents);
			hip->extent_state |= HFSPLUS_EXT_DIRTY;
			hip->cached_blocks += len;
		} else if (res == -ENOSPC)
			goto insert_extent;
	}
out:
	if (!res) {
		hip->alloc_blocks += len;
		mutex_unlock(&hip->extents_lock);
		hfsplus_mark_inode_dirty(inode, HFSPLUS_I_ALLOC_DIRTY);
		return 0;
	}
	mutex_unlock(&hip->extents_lock);
	return res;

insert_extent:
	hfs_dbg(EXTENT, "insert new extent\n");
	res = hfsplus_ext_write_extent_locked(inode);
	if (res)
		goto out;

	memset(hip->cached_extents, 0, sizeof(hfsplus_extent_rec));
	hip->cached_extents[0].start_block = cpu_to_be32(start);
	hip->cached_extents[0].block_count = cpu_to_be32(len);
	hfsplus_dump_extent(hip->cached_extents);
	hip->extent_state |= HFSPLUS_EXT_DIRTY | HFSPLUS_EXT_NEW;
	hip->cached_start = hip->alloc_blocks;
	hip->cached_blocks = len;

	res = 0;
	goto out;
}

void hfsplus_file_truncate(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct hfsplus_inode_info *hip = HFSPLUS_I(inode);
	struct hfs_find_data fd;
	u32 alloc_cnt, blk_cnt, start;
	int res;

	hfs_dbg(INODE, "truncate: %lu, %llu -> %llu\n",
		inode->i_ino, (long long)hip->phys_size, inode->i_size);

	if (inode->i_size > hip->phys_size) {
		struct address_space *mapping = inode->i_mapping;
		struct page *page;
		void *fsdata = NULL;
		loff_t size = inode->i_size;

		res = hfsplus_write_begin(NULL, mapping, size, 0,
					  &page, &fsdata);
		if (res)
			return;
		res = generic_write_end(NULL, mapping, size, 0, 0,
					page, fsdata);
		if (res < 0)
			return;
		mark_inode_dirty(inode);
		return;
	} else if (inode->i_size == hip->phys_size)
		return;

	blk_cnt = (inode->i_size + HFSPLUS_SB(sb)->alloc_blksz - 1) >>
			HFSPLUS_SB(sb)->alloc_blksz_shift;

	mutex_lock(&hip->extents_lock);

	alloc_cnt = hip->alloc_blocks;
	if (blk_cnt == alloc_cnt)
		goto out_unlock;

	res = hfs_find_init(HFSPLUS_SB(sb)->ext_tree, &fd);
	if (res) {
		mutex_unlock(&hip->extents_lock);
		/* XXX: We lack error handling of hfsplus_file_truncate() */
		return;
	}
	while (1) {
		if (alloc_cnt == hip->first_blocks) {
			mutex_unlock(&fd.tree->tree_lock);
			hfsplus_free_extents(sb, hip->first_extents,
					     alloc_cnt, alloc_cnt - blk_cnt);
			hfsplus_dump_extent(hip->first_extents);
			hip->first_blocks = blk_cnt;
			mutex_lock_nested(&fd.tree->tree_lock,
				hfsplus_btree_lock_class(fd.tree));
			break;
		}
		res = __hfsplus_ext_cache_extent(&fd, inode, alloc_cnt);
		if (res)
			break;

		start = hip->cached_start;
		if (blk_cnt <= start)
			hfs_brec_remove(&fd);
		mutex_unlock(&fd.tree->tree_lock);
		hfsplus_free_extents(sb, hip->cached_extents,
				     alloc_cnt - start, alloc_cnt - blk_cnt);
		hfsplus_dump_extent(hip->cached_extents);
		mutex_lock_nested(&fd.tree->tree_lock,
				hfsplus_btree_lock_class(fd.tree));
		if (blk_cnt > start) {
			hip->extent_state |= HFSPLUS_EXT_DIRTY;
			break;
		}
		alloc_cnt = start;
		hip->cached_start = hip->cached_blocks = 0;
		hip->extent_state &= ~(HFSPLUS_EXT_DIRTY | HFSPLUS_EXT_NEW);
	}
	hfs_find_exit(&fd);

	hip->alloc_blocks = blk_cnt;
out_unlock:
	mutex_unlock(&hip->extents_lock);
	hip->phys_size = inode->i_size;
	hip->fs_blocks = (inode->i_size + sb->s_blocksize - 1) >>
		sb->s_blocksize_bits;
	inode_set_bytes(inode, hip->fs_blocks << sb->s_blocksize_bits);
	hfsplus_mark_inode_dirty(inode, HFSPLUS_I_ALLOC_DIRTY);
}
