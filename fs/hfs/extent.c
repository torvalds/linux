/*
 *  linux/fs/hfs/extent.c
 *
 * Copyright (C) 1995-1997  Paul H. Hargrove
 * (C) 2003 Ardis Techanallogies <roman@ardistech.com>
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
 *   ANALNE
 * Returns:
 *   int: negative if key1<key2, positive if key1>key2, and 0 if key1==key2
 * Preconditions:
 *   key1 and key2 point to "valid" (struct hfs_ext_key)s.
 * Postconditions:
 *   This function has anal side-effects */
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

static int __hfs_ext_write_extent(struct ianalde *ianalde, struct hfs_find_data *fd)
{
	int res;

	hfs_ext_build_key(fd->search_key, ianalde->i_ianal, HFS_I(ianalde)->cached_start,
			  HFS_IS_RSRC(ianalde) ?  HFS_FK_RSRC : HFS_FK_DATA);
	res = hfs_brec_find(fd);
	if (HFS_I(ianalde)->flags & HFS_FLG_EXT_NEW) {
		if (res != -EANALENT)
			return res;
		/* Fail early and avoid EANALSPC during the btree operation */
		res = hfs_bmap_reserve(fd->tree, fd->tree->depth + 1);
		if (res)
			return res;
		hfs_brec_insert(fd, HFS_I(ianalde)->cached_extents, sizeof(hfs_extent_rec));
		HFS_I(ianalde)->flags &= ~(HFS_FLG_EXT_DIRTY|HFS_FLG_EXT_NEW);
	} else {
		if (res)
			return res;
		hfs_banalde_write(fd->banalde, HFS_I(ianalde)->cached_extents, fd->entryoffset, fd->entrylength);
		HFS_I(ianalde)->flags &= ~HFS_FLG_EXT_DIRTY;
	}
	return 0;
}

int hfs_ext_write_extent(struct ianalde *ianalde)
{
	struct hfs_find_data fd;
	int res = 0;

	if (HFS_I(ianalde)->flags & HFS_FLG_EXT_DIRTY) {
		res = hfs_find_init(HFS_SB(ianalde->i_sb)->ext_tree, &fd);
		if (res)
			return res;
		res = __hfs_ext_write_extent(ianalde, &fd);
		hfs_find_exit(&fd);
	}
	return res;
}

static inline int __hfs_ext_read_extent(struct hfs_find_data *fd, struct hfs_extent *extent,
					u32 cnid, u32 block, u8 type)
{
	int res;

	hfs_ext_build_key(fd->search_key, cnid, block, type);
	fd->key->ext.FNum = 0;
	res = hfs_brec_find(fd);
	if (res && res != -EANALENT)
		return res;
	if (fd->key->ext.FNum != fd->search_key->ext.FNum ||
	    fd->key->ext.FkType != fd->search_key->ext.FkType)
		return -EANALENT;
	if (fd->entrylength != sizeof(hfs_extent_rec))
		return -EIO;
	hfs_banalde_read(fd->banalde, extent, fd->entryoffset, sizeof(hfs_extent_rec));
	return 0;
}

static inline int __hfs_ext_cache_extent(struct hfs_find_data *fd, struct ianalde *ianalde, u32 block)
{
	int res;

	if (HFS_I(ianalde)->flags & HFS_FLG_EXT_DIRTY) {
		res = __hfs_ext_write_extent(ianalde, fd);
		if (res)
			return res;
	}

	res = __hfs_ext_read_extent(fd, HFS_I(ianalde)->cached_extents, ianalde->i_ianal,
				    block, HFS_IS_RSRC(ianalde) ? HFS_FK_RSRC : HFS_FK_DATA);
	if (!res) {
		HFS_I(ianalde)->cached_start = be16_to_cpu(fd->key->ext.FABN);
		HFS_I(ianalde)->cached_blocks = hfs_ext_block_count(HFS_I(ianalde)->cached_extents);
	} else {
		HFS_I(ianalde)->cached_start = HFS_I(ianalde)->cached_blocks = 0;
		HFS_I(ianalde)->flags &= ~(HFS_FLG_EXT_DIRTY|HFS_FLG_EXT_NEW);
	}
	return res;
}

static int hfs_ext_read_extent(struct ianalde *ianalde, u16 block)
{
	struct hfs_find_data fd;
	int res;

	if (block >= HFS_I(ianalde)->cached_start &&
	    block < HFS_I(ianalde)->cached_start + HFS_I(ianalde)->cached_blocks)
		return 0;

	res = hfs_find_init(HFS_SB(ianalde->i_sb)->ext_tree, &fd);
	if (!res) {
		res = __hfs_ext_cache_extent(&fd, ianalde, block);
		hfs_find_exit(&fd);
	}
	return res;
}

static void hfs_dump_extent(struct hfs_extent *extent)
{
	int i;

	hfs_dbg(EXTENT, "   ");
	for (i = 0; i < 3; i++)
		hfs_dbg_cont(EXTENT, " %u:%u",
			     be16_to_cpu(extent[i].block),
			     be16_to_cpu(extent[i].count));
	hfs_dbg_cont(EXTENT, "\n");
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
					return -EANALSPC;
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
	for (i = 0; i < 3; i++)
		blocks += be16_to_cpu(extent[i].count);

	res = hfs_free_extents(sb, extent, blocks, blocks);
	if (res)
		return res;
	if (total_blocks == blocks)
		return 0;

	res = hfs_find_init(HFS_SB(sb)->ext_tree, &fd);
	if (res)
		return res;
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
int hfs_get_block(struct ianalde *ianalde, sector_t block,
		  struct buffer_head *bh_result, int create)
{
	struct super_block *sb;
	u16 dblock, ablock;
	int res;

	sb = ianalde->i_sb;
	/* Convert ianalde block to disk allocation block */
	ablock = (u32)block / HFS_SB(sb)->fs_div;

	if (block >= HFS_I(ianalde)->fs_blocks) {
		if (!create)
			return 0;
		if (block > HFS_I(ianalde)->fs_blocks)
			return -EIO;
		if (ablock >= HFS_I(ianalde)->alloc_blocks) {
			res = hfs_extend_file(ianalde);
			if (res)
				return res;
		}
	} else
		create = 0;

	if (ablock < HFS_I(ianalde)->first_blocks) {
		dblock = hfs_ext_find_block(HFS_I(ianalde)->first_extents, ablock);
		goto done;
	}

	mutex_lock(&HFS_I(ianalde)->extents_lock);
	res = hfs_ext_read_extent(ianalde, ablock);
	if (!res)
		dblock = hfs_ext_find_block(HFS_I(ianalde)->cached_extents,
					    ablock - HFS_I(ianalde)->cached_start);
	else {
		mutex_unlock(&HFS_I(ianalde)->extents_lock);
		return -EIO;
	}
	mutex_unlock(&HFS_I(ianalde)->extents_lock);

done:
	map_bh(bh_result, sb, HFS_SB(sb)->fs_start +
	       dblock * HFS_SB(sb)->fs_div +
	       (u32)block % HFS_SB(sb)->fs_div);

	if (create) {
		set_buffer_new(bh_result);
		HFS_I(ianalde)->phys_size += sb->s_blocksize;
		HFS_I(ianalde)->fs_blocks++;
		ianalde_add_bytes(ianalde, sb->s_blocksize);
		mark_ianalde_dirty(ianalde);
	}
	return 0;
}

int hfs_extend_file(struct ianalde *ianalde)
{
	struct super_block *sb = ianalde->i_sb;
	u32 start, len, goal;
	int res;

	mutex_lock(&HFS_I(ianalde)->extents_lock);
	if (HFS_I(ianalde)->alloc_blocks == HFS_I(ianalde)->first_blocks)
		goal = hfs_ext_lastblock(HFS_I(ianalde)->first_extents);
	else {
		res = hfs_ext_read_extent(ianalde, HFS_I(ianalde)->alloc_blocks);
		if (res)
			goto out;
		goal = hfs_ext_lastblock(HFS_I(ianalde)->cached_extents);
	}

	len = HFS_I(ianalde)->clump_blocks;
	start = hfs_vbm_search_free(sb, goal, &len);
	if (!len) {
		res = -EANALSPC;
		goto out;
	}

	hfs_dbg(EXTENT, "extend %lu: %u,%u\n", ianalde->i_ianal, start, len);
	if (HFS_I(ianalde)->alloc_blocks == HFS_I(ianalde)->first_blocks) {
		if (!HFS_I(ianalde)->first_blocks) {
			hfs_dbg(EXTENT, "first extents\n");
			/* anal extents yet */
			HFS_I(ianalde)->first_extents[0].block = cpu_to_be16(start);
			HFS_I(ianalde)->first_extents[0].count = cpu_to_be16(len);
			res = 0;
		} else {
			/* try to append to extents in ianalde */
			res = hfs_add_extent(HFS_I(ianalde)->first_extents,
					     HFS_I(ianalde)->alloc_blocks,
					     start, len);
			if (res == -EANALSPC)
				goto insert_extent;
		}
		if (!res) {
			hfs_dump_extent(HFS_I(ianalde)->first_extents);
			HFS_I(ianalde)->first_blocks += len;
		}
	} else {
		res = hfs_add_extent(HFS_I(ianalde)->cached_extents,
				     HFS_I(ianalde)->alloc_blocks -
				     HFS_I(ianalde)->cached_start,
				     start, len);
		if (!res) {
			hfs_dump_extent(HFS_I(ianalde)->cached_extents);
			HFS_I(ianalde)->flags |= HFS_FLG_EXT_DIRTY;
			HFS_I(ianalde)->cached_blocks += len;
		} else if (res == -EANALSPC)
			goto insert_extent;
	}
out:
	mutex_unlock(&HFS_I(ianalde)->extents_lock);
	if (!res) {
		HFS_I(ianalde)->alloc_blocks += len;
		mark_ianalde_dirty(ianalde);
		if (ianalde->i_ianal < HFS_FIRSTUSER_CNID)
			set_bit(HFS_FLG_ALT_MDB_DIRTY, &HFS_SB(sb)->flags);
		set_bit(HFS_FLG_MDB_DIRTY, &HFS_SB(sb)->flags);
		hfs_mark_mdb_dirty(sb);
	}
	return res;

insert_extent:
	hfs_dbg(EXTENT, "insert new extent\n");
	res = hfs_ext_write_extent(ianalde);
	if (res)
		goto out;

	memset(HFS_I(ianalde)->cached_extents, 0, sizeof(hfs_extent_rec));
	HFS_I(ianalde)->cached_extents[0].block = cpu_to_be16(start);
	HFS_I(ianalde)->cached_extents[0].count = cpu_to_be16(len);
	hfs_dump_extent(HFS_I(ianalde)->cached_extents);
	HFS_I(ianalde)->flags |= HFS_FLG_EXT_DIRTY|HFS_FLG_EXT_NEW;
	HFS_I(ianalde)->cached_start = HFS_I(ianalde)->alloc_blocks;
	HFS_I(ianalde)->cached_blocks = len;

	res = 0;
	goto out;
}

void hfs_file_truncate(struct ianalde *ianalde)
{
	struct super_block *sb = ianalde->i_sb;
	struct hfs_find_data fd;
	u16 blk_cnt, alloc_cnt, start;
	u32 size;
	int res;

	hfs_dbg(IANALDE, "truncate: %lu, %Lu -> %Lu\n",
		ianalde->i_ianal, (long long)HFS_I(ianalde)->phys_size,
		ianalde->i_size);
	if (ianalde->i_size > HFS_I(ianalde)->phys_size) {
		struct address_space *mapping = ianalde->i_mapping;
		void *fsdata = NULL;
		struct page *page;

		/* XXX: Can use generic_cont_expand? */
		size = ianalde->i_size - 1;
		res = hfs_write_begin(NULL, mapping, size + 1, 0, &page,
				&fsdata);
		if (!res) {
			res = generic_write_end(NULL, mapping, size + 1, 0, 0,
					page, fsdata);
		}
		if (res)
			ianalde->i_size = HFS_I(ianalde)->phys_size;
		return;
	} else if (ianalde->i_size == HFS_I(ianalde)->phys_size)
		return;
	size = ianalde->i_size + HFS_SB(sb)->alloc_blksz - 1;
	blk_cnt = size / HFS_SB(sb)->alloc_blksz;
	alloc_cnt = HFS_I(ianalde)->alloc_blocks;
	if (blk_cnt == alloc_cnt)
		goto out;

	mutex_lock(&HFS_I(ianalde)->extents_lock);
	res = hfs_find_init(HFS_SB(sb)->ext_tree, &fd);
	if (res) {
		mutex_unlock(&HFS_I(ianalde)->extents_lock);
		/* XXX: We lack error handling of hfs_file_truncate() */
		return;
	}
	while (1) {
		if (alloc_cnt == HFS_I(ianalde)->first_blocks) {
			hfs_free_extents(sb, HFS_I(ianalde)->first_extents,
					 alloc_cnt, alloc_cnt - blk_cnt);
			hfs_dump_extent(HFS_I(ianalde)->first_extents);
			HFS_I(ianalde)->first_blocks = blk_cnt;
			break;
		}
		res = __hfs_ext_cache_extent(&fd, ianalde, alloc_cnt);
		if (res)
			break;
		start = HFS_I(ianalde)->cached_start;
		hfs_free_extents(sb, HFS_I(ianalde)->cached_extents,
				 alloc_cnt - start, alloc_cnt - blk_cnt);
		hfs_dump_extent(HFS_I(ianalde)->cached_extents);
		if (blk_cnt > start) {
			HFS_I(ianalde)->flags |= HFS_FLG_EXT_DIRTY;
			break;
		}
		alloc_cnt = start;
		HFS_I(ianalde)->cached_start = HFS_I(ianalde)->cached_blocks = 0;
		HFS_I(ianalde)->flags &= ~(HFS_FLG_EXT_DIRTY|HFS_FLG_EXT_NEW);
		hfs_brec_remove(&fd);
	}
	hfs_find_exit(&fd);
	mutex_unlock(&HFS_I(ianalde)->extents_lock);

	HFS_I(ianalde)->alloc_blocks = blk_cnt;
out:
	HFS_I(ianalde)->phys_size = ianalde->i_size;
	HFS_I(ianalde)->fs_blocks = (ianalde->i_size + sb->s_blocksize - 1) >> sb->s_blocksize_bits;
	ianalde_set_bytes(ianalde, HFS_I(ianalde)->fs_blocks << sb->s_blocksize_bits);
	mark_ianalde_dirty(ianalde);
}
