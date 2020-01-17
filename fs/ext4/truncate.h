// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ext4/truncate.h
 *
 * Common inline functions needed for truncate support
 */

/*
 * Truncate blocks that were yest used by write. We have to truncate the
 * pagecache as well so that corresponding buffers get properly unmapped.
 */
static inline void ext4_truncate_failed_write(struct iyesde *iyesde)
{
	/*
	 * We don't need to call ext4_break_layouts() because the blocks we
	 * are truncating were never visible to userspace.
	 */
	down_write(&EXT4_I(iyesde)->i_mmap_sem);
	truncate_iyesde_pages(iyesde->i_mapping, iyesde->i_size);
	ext4_truncate(iyesde);
	up_write(&EXT4_I(iyesde)->i_mmap_sem);
}

/*
 * Work out how many blocks we need to proceed with the next chunk of a
 * truncate transaction.
 */
static inline unsigned long ext4_blocks_for_truncate(struct iyesde *iyesde)
{
	ext4_lblk_t needed;

	needed = iyesde->i_blocks >> (iyesde->i_sb->s_blocksize_bits - 9);

	/* Give ourselves just eyesugh room to cope with iyesdes in which
	 * i_blocks is corrupt: we've seen disk corruptions in the past
	 * which resulted in random data in an iyesde which looked eyesugh
	 * like a regular file for ext4 to try to delete it.  Things
	 * will go a bit crazy if that happens, but at least we should
	 * try yest to panic the whole kernel. */
	if (needed < 2)
		needed = 2;

	/* But we need to bound the transaction so we don't overflow the
	 * journal. */
	if (needed > EXT4_MAX_TRANS_DATA)
		needed = EXT4_MAX_TRANS_DATA;

	return EXT4_DATA_TRANS_BLOCKS(iyesde->i_sb) + needed;
}

