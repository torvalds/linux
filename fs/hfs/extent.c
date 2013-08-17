/*
 *  linux/fs/hfs/extent.c
 *
 * Copyright (C) 1995-1997  Paul H. Hargrove
 * (C) 2003 Ardis Technologies <roman@ardistech.com>
 * This file may be distributed under the terms of the GNU General Public License.
 *
 * This file contains the functions related to the extents B-tree.
 */

#include <linux/pagemap.h>

#include "hfs_fs.h"
#include "btree.h"

/*================ File-local functions ================*/

/*
 * build_key
 */
static void hfs_ext_build_key(hfs_btree_key *key, u32 cnid, u16 block, u8 type)
{
	key->key_len = 7;
	key->ext.FkType = type;
	key->ext.FNum = cpu_to_be32(cnid);
	key->ext.FABN = cpu_to_be16(block);
}

/*
 * hfs_ext_compare()
 *
 * Description:
 *   This is the comparison function used for the extents B-tree.  In
 *   comparing extent B-tree entries, the file id is the most
 *   significant field (compared as unsigned ints); the fork type is
 *   the second most significant field (compared as unsigned chars);
 *   and the allocation block number field is the least significant
 *   (compared as unsigned ints).
 * Input Variable(s):
 *   struct hfs_ext_key *key1: pointer to the first key to compare
 *   struct hfs_ext_key *key2: pointer to the second key to compare
 * Output Variable(s):
 *   NONE
 * Returns:
 *   int: negative if key1<key2, positive if key1>key2, and 0 if key1==key2
 * Preconditions:
 *   key1 and key2 point to "valid" (struct hfs_ext_key)s.
 * Postconditions:
 *   This function has no side-effects */
int hfs_ext_keycmp(const btree_key *key1, const btree_key *key2)
{
	__be32 fnum1, fnum2;
	__be16 block1, block2;

	fnum1 = key1->ext.FNum;
	fnum2 = key2->ext.FNum;
	if (fnum1 != fnum2)
		return be32_to_cpu(fnum1) < be32_to_cpu(fnum2) ? -1 : 1;
	if (key1->ext.FkType != key2->ext.FkType)
		return key1->ext.FkType < key2->ext.FkType ? -1 : 1;

	block1 = key1->ext.FABN;
	block2 = key2->ext.FABN;
	if (block1 == block2)
		return 0;
	return be16_to_cpu(block1) < be16_to_cpu(block2) ? -1 : 1;
}

/*
 * hfs_ext_find_block
 *
 * Find a block within an extent record
 */
static u16 hfs_ext_find_block(struct hfs_extent *ext, u16 off)
{
	int i;
	u16 count;

	for (i = 0; i < 3; ext++, i++) {
		count = be16_to_cpu(ext->count);
		if (off < count)
			return be16_to_cpu(ext->block) + off;
		off -= count;
	}
	/* panic? */
	return 0;
}

static int hfs_ext_block_count(struct hfs_extent *ext)
{
	int i;
	u16 count = 0;

	for (i = 0; i < 3; ext++, i++)
		count += be16_to_cpu(ext->count);
	return count;
}

static u16 hfs_ext_lastblock(struct hfs_extent *ext)
{
	int i;

	ext += 2;
	for (i = 0; i < 2; ext--, i++)
		if (ext->count)
			break;
	return be16_to_cpu(ext->block) + be16_to_cpu(ext->count);
}

static void __hfs_ext_write_extent(struct inode *inode, struct hfs_find_data *fd)
{
	int res;

	hfs_ext_build_key(fd->search_key, inode->i_ino, HFS_I(inode)->cached_start,
			  HFS_IS_RSRC(inode) ?  HFS_FK_RSRC : HFS_FK_DATA);
	res = hfs_brec_find(fd);
	if (HFS_I(inode)->flags & HFS_FLG_EXT_NEW) {
		if (res != -ENOENT)
			return;
		hfs_brec_insert(fd, HFS_I(inode)->cached_extents, sizeof(hfs_extent_rec));
		HFS_I(inode)->flags &= ~(HFS_FLG_EXT_DIRTY|HFS_FLG_EXT_NEW);
	} else {
		if (res)
			return;
		hfs_bnode_write(fd->bnode, HFS_I(inode)->cached_extents, fd->entryoffset, fd->entrylength);
		HFS_I(inode)->flags &= ~HFS_FLG_EXT_DIRTY;
	}
}

void hfs_ext_write_extent(struct inode *inode)
{
	struct hfs_find_data fd;

	if (HFS_I(inode)->flags & HFS_FLG_EXT_DIRTY) {
		hfs_find_init(HFS_SB(inode->i_sb)->ext_tree, &fd);
		__hfs_ext_write_extent(inode, &fd);
		hfs_find_exit(&fd);
	}
}

static inline int __hfs_ext_read_extent(struct hfs_find_data *fd, struct hfs_extent *extent,
					u32 cnid, u32 block, u8 type)
{
	int res;

	hfs_ext_build_key(fd->search_key, cnid, block, type);
	fd->key->ext.FNum = 0;
	res = hfs_brec_find(fd);
	if (res && res != -ENOENT)
		return res;
	if (fd->key->ext.FNum != fd->search_key->ext.FNum ||
	    fd->key->ext.FkType != fd->search_key->ext.FkType)
		return -ENOENT;
	if (fd->entrylength != sizeof(hfs_extent_rec))
		return -EIO;
	hfs_bnode_read(fd->bnode, extent, fd->entryoffset, sizeof(hfs_extent_rec));
	return 0;
}

static inline int __hfs_ext_cache_extent(struct hfs_find_data *fd, struct inode *inode, u32 block)
{
	int res;

	if (HFS_I(inode)->flags & HFS_FLG_EXT_DIRTY)
		__hfs_ext_write_extent(inode, fd);

	res = __hfs_ext_read_extent(fd, HFS_I(inode)->cached_extents, inode->i_ino,
				    block, HFS_IS_RSRC(inode) ? HFS_FK_RSRC : HFS_FK_DATA);
	if (!res) {
		HFS_I(inode)->cached_start = be16_to_cpu(fd->key->ext.FABN);
		HFS_I(inode)->cached_blocks = hfs_ext_block_count(HFS_I(inode)->cached_extents);
	} else {
		HFS_I(inode)->cached_start = HFS_I(inode)->cached_blocks = 0;
		HFS_I(inode)->flags &= ~(HFS_FLG_EXT_DIRTY|HFS_FLG_EXT_NEW);
	}
	return res;
}

static int hfs_ext_read_extent(struct inode *inode, u16 block)
{
	struct hfs_find_data fd;
	int res;

	if (block >= HFS_I(inode)->cached_start &&
	    block < HFS_I(inode)->cached_start + HFS_I(inode)->cached_blocks)
		return 0;

	hfs_find_init(HFS_SB(inode->i_sb)->ext_tree, &fd);
	res = __hfs_ext_cache_extent(&fd, inode, block);
	hfs_find_exit(&fd);
	return res;
}

static void hfs_dump_extent(struct hfs_extent *extent)
{
	int i;

	dprint(DBG_EXTENT, "   ");
	for (i = 0; i < 3; i++)
		dprint(DBG_EXTENT, " %u:%u", be16_to_cpu(extent[i].block),
				 be16_to_cpu(extent[i].count));
	dprint(DBG_EXTENT, "\n");
}

static int hfs_add_extent(struct hfs_extent *extent, u16 offset,
			  u16 alloc_block, u16 block_count)
{
	u16 count, start;
	int i;

	hfs_dump_extent(extent);
	for (i = 0; i < 3; extent++, i++) {
		count = be16_to_cpu(extent->count);
		if (offset == count) {
			start = be16_to_cpu(extent->block);
			if (alloc_block != start + count) {
				if (++i >= 3)
					return -ENOSPC;
				extent++;
				extent->block = cpu_to_be16(alloc_block);
			} else
				block_count += count;
			extent->count = cpu_to_be16(block_count);
			return 0;
		} else if (offset < count)
			break;
		offset -= count;
	}
	/* panic? */
	return -EIO;
}

static int hfs_free_extents(struct super_block *sb, struct hfs_extent *extent,
			    u16 offset, u16 block_nr)
{
	u16 count, start;
	int i;

	hfs_dump_extent(extent);
	for (i = 0; i < 3; extent++, i++) {
		count = be16_to_cpu(extent->count);
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
		start = be16_to_cpu(extent->block);
		if (count <= block_nr) {
			hfs_clear_vbm_bits(sb, start, count);
			extent->block = 0;
			extent->count = 0;
			block_nr -= count;
		} else {
			count -= block_nr;
			hfs_clear_vbm_bits(sb, start + count, block_nr);
			extent->count = cpu_to_be16(count);
			block_nr = 0;
		}
		if (!block_nr || !i)
			return 0;
		i--;
		extent--;
		count = be16_to_cpu(extent->count);
	}
}

int hfs_free_fork(struct super_block *sb, struct hfs_cat_file *file, int type)
{
	struct hfs_find_data fd;
	u32 total_blocks, blocks, start;
	u32 cnid = be32_to_cpu(file->FlNum);
	struct hfs_extent *extent;
	int res, i;

	if (type == HFS_FK_DATA) {
		total_blocks = be32_to_cpu(file->PyLen);
		extent = file->ExtRec;
	} else {
		total_blocks = be32_to_cpu(file->RPyLen);
		extent = file->RExtRec;
	}
	total_blocks /= HFS_SB(sb)->alloc_blksz;
	if (!total_blocks)
		return 0;

	blocks = 0;
	for (i = 0; i < 3; extent++, i++)
		blocks += be16_to_cpu(extent[i].count);

	res = hfs_free_extents(sb, extent, blocks, blocks);
	if (res)
		return res;
	if (total_blocks == blocks)
		return 0;

	hfs_find_init(HFS_SB(sb)->ext_tree, &fd);
	do {
		res = __hfs_ext_read_extent(&fd, extent, cnid, total_blocks, type);
		if (res)
			break;
		start = be16_to_cpu(fd.key->ext.FABN);
		hfs_free_extents(sb, extent, total_blocks - start, total_blocks);
		hfs_brec_remove(&fd);
		total_blocks = start;
	} while (total_blocks > blocks);
	hfs_find_exit(&fd);

	return res;
}

/*
 * hfs_get_block
 */
int hfs_get_block(struct inode *inode, sector_t block,
		  struct buffer_head *bh_result, int create)
{
	struct super_block *sb;
	u16 dblock, ablock;
	int res;

	sb = inode->i_sb;
	/* Convert inode block to disk allocation block */
	ablock = (u32)block / HFS_SB(sb)->fs_div;

	if (block >= HFS_I(inode)->fs_blocks) {
		if (block > HFS_I(inode)->fs_blocks || !create)
			return -EIO;
		if (ablock >= HFS_I(inode)->alloc_blocks) {
			res = hfs_extend_file(inode);
			if (res)
				return res;
		}
	} else
		create = 0;

	if (ablock < HFS_I(inode)->first_blocks) {
		dblock = hfs_ext_find_block(HFS_I(inode)->first_extents, ablock);
		goto done;
	}

	mutex_lock(&HFS_I(inode)->extents_lock);
	res = hfs_ext_read_extent(inode, ablock);
	if (!res)
		dblock = hfs_ext_find_block(HFS_I(inode)->cached_extents,
					    ablock - HFS_I(inode)->cached_start);
	else {
		mutex_unlock(&HFS_I(inode)->extents_lock);
		return -EIO;
	}
	mutex_unlock(&HFS_I(inode)->extents_lock);

done:
	map_bh(bh_result, sb, HFS_SB(sb)->fs_start +
	       dblock * HFS_SB(sb)->fs_div +
	       (u32)block % HFS_SB(sb)->fs_div);

	if (create) {
		set_buffer_new(bh_result);
		HFS_I(inode)->phys_size += sb->s_blocksize;
		HFS_I(inode)->fs_blocks++;
		inode_add_bytes(inode, sb->s_blocksize);
		mark_inode_dirty(inode);
	}
	return 0;
}

int hfs_extend_file(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	u32 start, len, goal;
	int res;

	mutex_lock(&HFS_I(inode)->extents_lock);
	if (HFS_I(inode)->alloc_blocks == HFS_I(inode)->first_blocks)
		goal = hfs_ext_lastblock(HFS_I(inode)->first_extents);
	else {
		res = hfs_ext_read_extent(inode, HFS_I(inode)->alloc_blocks);
		if (res)
			goto out;
		goal = hfs_ext_lastblock(HFS_I(inode)->cached_extents);
	}

	len = HFS_I(inode)->clump_blocks;
	start = hfs_vbm_search_free(sb, goal, &len);
	if (!len) {
		res = -ENOSPC;
		goto out;
	}

	dprint(DBG_EXTENT, "extend %lu: %u,%u\n", inode->i_ino, start, len);
	if (HFS_I(inode)->alloc_blocks == HFS_I(inode)->first_blocks) {
		if (!HFS_I(inode)->first_blocks) {
			dprint(DBG_EXTENT, "first extents\n");
			/* no extents yet */
			HFS_I(inode)->first_extents[0].block = cpu_to_be16(start);
			HFS_I(inode)->first_extents[0].count = cpu_to_be16(len);
			res = 0;
		} else {
			/* try to append to extents in inode */
			res = hfs_add_extent(HFS_I(inode)->first_extents,
					     HFS_I(inode)->alloc_blocks,
					     start, len);
			if (res == -ENOSPC)
				goto insert_extent;
		}
		if (!res) {
			hfs_dump_extent(HFS_I(inode)->first_extents);
			HFS_I(inode)->first_blocks += len;
		}
	} else {
		res = hfs_add_extent(HFS_I(inode)->cached_extents,
				     HFS_I(inode)->alloc_blocks -
				     HFS_I(inode)->cached_start,
				     start, len);
		if (!res) {
			hfs_dump_extent(HFS_I(inode)->cached_extents);
			HFS_I(inode)->flags |= HFS_FLG_EXT_DIRTY;
			HFS_I(inode)->cached_blocks += len;
		} else if (res == -ENOSPC)
			goto insert_extent;
	}
out:
	mutex_unlock(&HFS_I(inode)->extents_lock);
	if (!res) {
		HFS_I(inode)->alloc_blocks += len;
		mark_inode_dirty(inode);
		if (inode->i_ino < HFS_FIRSTUSER_CNID)
			set_bit(HFS_FLG_ALT_MDB_DIRTY, &HFS_SB(sb)->flags);
		set_bit(HFS_FLG_MDB_DIRTY, &HFS_SB(sb)->flags);
		sb->s_dirt = 1;
	}
	return res;

insert_extent:
	dprint(DBG_EXTENT, "insert new extent\n");
	hfs_ext_write_extent(inode);

	memset(HFS_I(inode)->cached_extents, 0, sizeof(hfs_extent_rec));
	HFS_I(inode)->cached_extents[0].block = cpu_to_be16(start);
	HFS_I(inode)->cached_extents[0].count = cpu_to_be16(len);
	hfs_dump_extent(HFS_I(inode)->cached_extents);
	HFS_I(inode)->flags |= HFS_FLG_EXT_DIRTY|HFS_FLG_EXT_NEW;
	HFS_I(inode)->cached_start = HFS_I(inode)->alloc_blocks;
	HFS_I(inode)->cached_blocks = len;

	res = 0;
	goto out;
}

void hfs_file_truncate(struct inode *inode)
{
	struct super_block *sb = inode->i_sb;
	struct hfs_find_data fd;
	u16 blk_cnt, alloc_cnt, start;
	u32 size;
	int res;

	dprint(DBG_INODE, "truncate: %lu, %Lu -> %Lu\n", inode->i_ino,
	       (long long)HFS_I(inode)->phys_size, inode->i_size);
	if (inode->i_size > HFS_I(inode)->phys_size) {
		struct address_space *mapping = inode->i_mapping;
		void *fsdata;
		struct page *page;
		int res;

		/* XXX: Can use generic_cont_expand? */
		size = inode->i_size - 1;
		res = pagecache_write_begin(NULL, mapping, size+1, 0,
				AOP_FLAG_UNINTERRUPTIBLE, &page, &fsdata);
		if (!res) {
			res = pagecache_write_end(NULL, mapping, size+1, 0, 0,
					page, fsdata);
		}
		if (res)
			inode->i_size = HFS_I(inode)->phys_size;
		return;
	} else if (inode->i_size == HFS_I(inode)->phys_size)
		return;
	size = inode->i_size + HFS_SB(sb)->alloc_blksz - 1;
	blk_cnt = size / HFS_SB(sb)->alloc_blksz;
	alloc_cnt = HFS_I(inode)->alloc_blocks;
	if (blk_cnt == alloc_cnt)
		goto out;

	mutex_lock(&HFS_I(inode)->extents_lock);
	hfs_find_init(HFS_SB(sb)->ext_tree, &fd);
	while (1) {
		if (alloc_cnt == HFS_I(inode)->first_blocks) {
			hfs_free_extents(sb, HFS_I(inode)->first_extents,
					 alloc_cnt, alloc_cnt - blk_cnt);
			hfs_dump_extent(HFS_I(inode)->first_extents);
			HFS_I(inode)->first_blocks = blk_cnt;
			break;
		}
		res = __hfs_ext_cache_extent(&fd, inode, alloc_cnt);
		if (res)
			break;
		start = HFS_I(inode)->cached_start;
		hfs_free_extents(sb, HFS_I(inode)->cached_extents,
				 alloc_cnt - start, alloc_cnt - blk_cnt);
		hfs_dump_extent(HFS_I(inode)->cached_extents);
		if (blk_cnt > start) {
			HFS_I(inode)->flags |= HFS_FLG_EXT_DIRTY;
			break;
		}
		alloc_cnt = start;
		HFS_I(inode)->cached_start = HFS_I(inode)->cached_blocks = 0;
		HFS_I(inode)->flags &= ~(HFS_FLG_EXT_DIRTY|HFS_FLG_EXT_NEW);
		hfs_brec_remove(&fd);
	}
	hfs_find_exit(&fd);
	mutex_unlock(&HFS_I(inode)->extents_lock);

	HFS_I(inode)->alloc_blocks = blk_cnt;
out:
	HFS_I(inode)->phys_size = inode->i_size;
	HFS_I(inode)->fs_blocks = (inode->i_size + sb->s_blocksize - 1) >> sb->s_blocksize_bits;
	inode_set_bytes(inode, HFS_I(inode)->fs_blocks << sb->s_blocksize_bits);
	mark_inode_dirty(inode);
}
