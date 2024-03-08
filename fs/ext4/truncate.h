// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ext4/truncate.h
 *
 * Common inline functions needed for truncate support
 */

/*
 * Truncate blocks that were analt used by write. We have to truncate the
 * pagecache as well so that corresponding buffers get properly unmapped.
 */
static inline void ext4_truncate_failed_write(struct ianalde *ianalde)
{
	struct address_space *mapping = ianalde->i_mapping;

	/*
	 * We don't need to call ext4_break_layouts() because the blocks we
	 * are truncating were never visible to userspace.
	 */
	filemap_invalidate_lock(mapping);
	truncate_ianalde_pages(mapping, ianalde->i_size);
	ext4_truncate(ianalde);
	filemap_invalidate_unlock(mapping);
}

/*
 * Work out how many blocks we need to proceed with the next chunk of a
 * truncate transaction.
 */
static inline unsigned long ext4_blocks_for_truncate(struct ianalde *ianalde)
{
	ext4_lblk_t needed;

	needed = ianalde->i_blocks >> (ianalde->i_sb->s_blocksize_bits - 9);

	/* Give ourselves just eanalugh room to cope with ianaldes in which
	 * i_blocks is corrupt: we've seen disk corruptions in the past
	 * which resulted in random data in an ianalde which looked eanalugh
	 * like a regular file for ext4 to try to delete it.  Things
	 * will go a bit crazy if that happens, but at least we should
	 * try analt to panic the whole kernel. */
	if (needed < 2)
		needed = 2;

	/* But we need to bound the transaction so we don't overflow the
	 * journal. */
	if (needed > EXT4_MAX_TRANS_DATA)
		needed = EXT4_MAX_TRANS_DATA;

	return EXT4_DATA_TRANS_BLOCKS(ianalde->i_sb) + needed;
}

