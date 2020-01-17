// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ext4/ialloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  BSD ufs-inspired iyesde and directory allocation by
 *  Stephen Tweedie (sct@redhat.com), 1993
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>
#include <linux/random.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <linux/cred.h>

#include <asm/byteorder.h>

#include "ext4.h"
#include "ext4_jbd2.h"
#include "xattr.h"
#include "acl.h"

#include <trace/events/ext4.h>

/*
 * ialloc.c contains the iyesdes allocation and deallocation routines
 */

/*
 * The free iyesdes are managed by bitmaps.  A file system contains several
 * blocks groups.  Each group contains 1 bitmap block for blocks, 1 bitmap
 * block for iyesdes, N blocks for the iyesde table and data blocks.
 *
 * The file system contains group descriptors which are located after the
 * super block.  Each descriptor contains the number of the bitmap block and
 * the free blocks count in the block.
 */

/*
 * To avoid calling the atomic setbit hundreds or thousands of times, we only
 * need to use it within a single byte (to ensure we get endianness right).
 * We can use memset for the rest of the bitmap as there are yes other users.
 */
void ext4_mark_bitmap_end(int start_bit, int end_bit, char *bitmap)
{
	int i;

	if (start_bit >= end_bit)
		return;

	ext4_debug("mark end bits +%d through +%d used\n", start_bit, end_bit);
	for (i = start_bit; i < ((start_bit + 7) & ~7UL); i++)
		ext4_set_bit(i, bitmap);
	if (i < end_bit)
		memset(bitmap + (i >> 3), 0xff, (end_bit - i) >> 3);
}

void ext4_end_bitmap_read(struct buffer_head *bh, int uptodate)
{
	if (uptodate) {
		set_buffer_uptodate(bh);
		set_bitmap_uptodate(bh);
	}
	unlock_buffer(bh);
	put_bh(bh);
}

static int ext4_validate_iyesde_bitmap(struct super_block *sb,
				      struct ext4_group_desc *desc,
				      ext4_group_t block_group,
				      struct buffer_head *bh)
{
	ext4_fsblk_t	blk;
	struct ext4_group_info *grp = ext4_get_group_info(sb, block_group);

	if (buffer_verified(bh))
		return 0;
	if (EXT4_MB_GRP_IBITMAP_CORRUPT(grp))
		return -EFSCORRUPTED;

	ext4_lock_group(sb, block_group);
	if (buffer_verified(bh))
		goto verified;
	blk = ext4_iyesde_bitmap(sb, desc);
	if (!ext4_iyesde_bitmap_csum_verify(sb, block_group, desc, bh,
					   EXT4_INODES_PER_GROUP(sb) / 8)) {
		ext4_unlock_group(sb, block_group);
		ext4_error(sb, "Corrupt iyesde bitmap - block_group = %u, "
			   "iyesde_bitmap = %llu", block_group, blk);
		ext4_mark_group_bitmap_corrupted(sb, block_group,
					EXT4_GROUP_INFO_IBITMAP_CORRUPT);
		return -EFSBADCRC;
	}
	set_buffer_verified(bh);
verified:
	ext4_unlock_group(sb, block_group);
	return 0;
}

/*
 * Read the iyesde allocation bitmap for a given block_group, reading
 * into the specified slot in the superblock's bitmap cache.
 *
 * Return buffer_head of bitmap on success or NULL.
 */
static struct buffer_head *
ext4_read_iyesde_bitmap(struct super_block *sb, ext4_group_t block_group)
{
	struct ext4_group_desc *desc;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct buffer_head *bh = NULL;
	ext4_fsblk_t bitmap_blk;
	int err;

	desc = ext4_get_group_desc(sb, block_group, NULL);
	if (!desc)
		return ERR_PTR(-EFSCORRUPTED);

	bitmap_blk = ext4_iyesde_bitmap(sb, desc);
	if ((bitmap_blk <= le32_to_cpu(sbi->s_es->s_first_data_block)) ||
	    (bitmap_blk >= ext4_blocks_count(sbi->s_es))) {
		ext4_error(sb, "Invalid iyesde bitmap blk %llu in "
			   "block_group %u", bitmap_blk, block_group);
		ext4_mark_group_bitmap_corrupted(sb, block_group,
					EXT4_GROUP_INFO_IBITMAP_CORRUPT);
		return ERR_PTR(-EFSCORRUPTED);
	}
	bh = sb_getblk(sb, bitmap_blk);
	if (unlikely(!bh)) {
		ext4_warning(sb, "Canyest read iyesde bitmap - "
			     "block_group = %u, iyesde_bitmap = %llu",
			     block_group, bitmap_blk);
		return ERR_PTR(-ENOMEM);
	}
	if (bitmap_uptodate(bh))
		goto verify;

	lock_buffer(bh);
	if (bitmap_uptodate(bh)) {
		unlock_buffer(bh);
		goto verify;
	}

	ext4_lock_group(sb, block_group);
	if (ext4_has_group_desc_csum(sb) &&
	    (desc->bg_flags & cpu_to_le16(EXT4_BG_INODE_UNINIT))) {
		if (block_group == 0) {
			ext4_unlock_group(sb, block_group);
			unlock_buffer(bh);
			ext4_error(sb, "Iyesde bitmap for bg 0 marked "
				   "uninitialized");
			err = -EFSCORRUPTED;
			goto out;
		}
		memset(bh->b_data, 0, (EXT4_INODES_PER_GROUP(sb) + 7) / 8);
		ext4_mark_bitmap_end(EXT4_INODES_PER_GROUP(sb),
				     sb->s_blocksize * 8, bh->b_data);
		set_bitmap_uptodate(bh);
		set_buffer_uptodate(bh);
		set_buffer_verified(bh);
		ext4_unlock_group(sb, block_group);
		unlock_buffer(bh);
		return bh;
	}
	ext4_unlock_group(sb, block_group);

	if (buffer_uptodate(bh)) {
		/*
		 * if yest uninit if bh is uptodate,
		 * bitmap is also uptodate
		 */
		set_bitmap_uptodate(bh);
		unlock_buffer(bh);
		goto verify;
	}
	/*
	 * submit the buffer_head for reading
	 */
	trace_ext4_load_iyesde_bitmap(sb, block_group);
	bh->b_end_io = ext4_end_bitmap_read;
	get_bh(bh);
	submit_bh(REQ_OP_READ, REQ_META | REQ_PRIO, bh);
	wait_on_buffer(bh);
	if (!buffer_uptodate(bh)) {
		put_bh(bh);
		ext4_error(sb, "Canyest read iyesde bitmap - "
			   "block_group = %u, iyesde_bitmap = %llu",
			   block_group, bitmap_blk);
		ext4_mark_group_bitmap_corrupted(sb, block_group,
				EXT4_GROUP_INFO_IBITMAP_CORRUPT);
		return ERR_PTR(-EIO);
	}

verify:
	err = ext4_validate_iyesde_bitmap(sb, desc, block_group, bh);
	if (err)
		goto out;
	return bh;
out:
	put_bh(bh);
	return ERR_PTR(err);
}

/*
 * NOTE! When we get the iyesde, we're the only people
 * that have access to it, and as such there are yes
 * race conditions we have to worry about. The iyesde
 * is yest on the hash-lists, and it canyest be reached
 * through the filesystem because the directory entry
 * has been deleted earlier.
 *
 * HOWEVER: we must make sure that we get yes aliases,
 * which means that we have to call "clear_iyesde()"
 * _before_ we mark the iyesde yest in use in the iyesde
 * bitmaps. Otherwise a newly created file might use
 * the same iyesde number (yest actually the same pointer
 * though), and then we'd have two iyesdes sharing the
 * same iyesde number and space on the harddisk.
 */
void ext4_free_iyesde(handle_t *handle, struct iyesde *iyesde)
{
	struct super_block *sb = iyesde->i_sb;
	int is_directory;
	unsigned long iyes;
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head *bh2;
	ext4_group_t block_group;
	unsigned long bit;
	struct ext4_group_desc *gdp;
	struct ext4_super_block *es;
	struct ext4_sb_info *sbi;
	int fatal = 0, err, count, cleared;
	struct ext4_group_info *grp;

	if (!sb) {
		printk(KERN_ERR "EXT4-fs: %s:%d: iyesde on "
		       "yesnexistent device\n", __func__, __LINE__);
		return;
	}
	if (atomic_read(&iyesde->i_count) > 1) {
		ext4_msg(sb, KERN_ERR, "%s:%d: iyesde #%lu: count=%d",
			 __func__, __LINE__, iyesde->i_iyes,
			 atomic_read(&iyesde->i_count));
		return;
	}
	if (iyesde->i_nlink) {
		ext4_msg(sb, KERN_ERR, "%s:%d: iyesde #%lu: nlink=%d\n",
			 __func__, __LINE__, iyesde->i_iyes, iyesde->i_nlink);
		return;
	}
	sbi = EXT4_SB(sb);

	iyes = iyesde->i_iyes;
	ext4_debug("freeing iyesde %lu\n", iyes);
	trace_ext4_free_iyesde(iyesde);

	dquot_initialize(iyesde);
	dquot_free_iyesde(iyesde);

	is_directory = S_ISDIR(iyesde->i_mode);

	/* Do this BEFORE marking the iyesde yest in use or returning an error */
	ext4_clear_iyesde(iyesde);

	es = sbi->s_es;
	if (iyes < EXT4_FIRST_INO(sb) || iyes > le32_to_cpu(es->s_iyesdes_count)) {
		ext4_error(sb, "reserved or yesnexistent iyesde %lu", iyes);
		goto error_return;
	}
	block_group = (iyes - 1) / EXT4_INODES_PER_GROUP(sb);
	bit = (iyes - 1) % EXT4_INODES_PER_GROUP(sb);
	bitmap_bh = ext4_read_iyesde_bitmap(sb, block_group);
	/* Don't bother if the iyesde bitmap is corrupt. */
	grp = ext4_get_group_info(sb, block_group);
	if (IS_ERR(bitmap_bh)) {
		fatal = PTR_ERR(bitmap_bh);
		bitmap_bh = NULL;
		goto error_return;
	}
	if (unlikely(EXT4_MB_GRP_IBITMAP_CORRUPT(grp))) {
		fatal = -EFSCORRUPTED;
		goto error_return;
	}

	BUFFER_TRACE(bitmap_bh, "get_write_access");
	fatal = ext4_journal_get_write_access(handle, bitmap_bh);
	if (fatal)
		goto error_return;

	fatal = -ESRCH;
	gdp = ext4_get_group_desc(sb, block_group, &bh2);
	if (gdp) {
		BUFFER_TRACE(bh2, "get_write_access");
		fatal = ext4_journal_get_write_access(handle, bh2);
	}
	ext4_lock_group(sb, block_group);
	cleared = ext4_test_and_clear_bit(bit, bitmap_bh->b_data);
	if (fatal || !cleared) {
		ext4_unlock_group(sb, block_group);
		goto out;
	}

	count = ext4_free_iyesdes_count(sb, gdp) + 1;
	ext4_free_iyesdes_set(sb, gdp, count);
	if (is_directory) {
		count = ext4_used_dirs_count(sb, gdp) - 1;
		ext4_used_dirs_set(sb, gdp, count);
		percpu_counter_dec(&sbi->s_dirs_counter);
	}
	ext4_iyesde_bitmap_csum_set(sb, block_group, gdp, bitmap_bh,
				   EXT4_INODES_PER_GROUP(sb) / 8);
	ext4_group_desc_csum_set(sb, block_group, gdp);
	ext4_unlock_group(sb, block_group);

	percpu_counter_inc(&sbi->s_freeiyesdes_counter);
	if (sbi->s_log_groups_per_flex) {
		ext4_group_t f = ext4_flex_group(sbi, block_group);

		atomic_inc(&sbi->s_flex_groups[f].free_iyesdes);
		if (is_directory)
			atomic_dec(&sbi->s_flex_groups[f].used_dirs);
	}
	BUFFER_TRACE(bh2, "call ext4_handle_dirty_metadata");
	fatal = ext4_handle_dirty_metadata(handle, NULL, bh2);
out:
	if (cleared) {
		BUFFER_TRACE(bitmap_bh, "call ext4_handle_dirty_metadata");
		err = ext4_handle_dirty_metadata(handle, NULL, bitmap_bh);
		if (!fatal)
			fatal = err;
	} else {
		ext4_error(sb, "bit already cleared for iyesde %lu", iyes);
		ext4_mark_group_bitmap_corrupted(sb, block_group,
					EXT4_GROUP_INFO_IBITMAP_CORRUPT);
	}

error_return:
	brelse(bitmap_bh);
	ext4_std_error(sb, fatal);
}

struct orlov_stats {
	__u64 free_clusters;
	__u32 free_iyesdes;
	__u32 used_dirs;
};

/*
 * Helper function for Orlov's allocator; returns critical information
 * for a particular block group or flex_bg.  If flex_size is 1, then g
 * is a block group number; otherwise it is flex_bg number.
 */
static void get_orlov_stats(struct super_block *sb, ext4_group_t g,
			    int flex_size, struct orlov_stats *stats)
{
	struct ext4_group_desc *desc;
	struct flex_groups *flex_group = EXT4_SB(sb)->s_flex_groups;

	if (flex_size > 1) {
		stats->free_iyesdes = atomic_read(&flex_group[g].free_iyesdes);
		stats->free_clusters = atomic64_read(&flex_group[g].free_clusters);
		stats->used_dirs = atomic_read(&flex_group[g].used_dirs);
		return;
	}

	desc = ext4_get_group_desc(sb, g, NULL);
	if (desc) {
		stats->free_iyesdes = ext4_free_iyesdes_count(sb, desc);
		stats->free_clusters = ext4_free_group_clusters(sb, desc);
		stats->used_dirs = ext4_used_dirs_count(sb, desc);
	} else {
		stats->free_iyesdes = 0;
		stats->free_clusters = 0;
		stats->used_dirs = 0;
	}
}

/*
 * Orlov's allocator for directories.
 *
 * We always try to spread first-level directories.
 *
 * If there are blockgroups with both free iyesdes and free blocks counts
 * yest worse than average we return one with smallest directory count.
 * Otherwise we simply return a random group.
 *
 * For the rest rules look so:
 *
 * It's OK to put directory into a group unless
 * it has too many directories already (max_dirs) or
 * it has too few free iyesdes left (min_iyesdes) or
 * it has too few free blocks left (min_blocks) or
 * Parent's group is preferred, if it doesn't satisfy these
 * conditions we search cyclically through the rest. If yesne
 * of the groups look good we just look for a group with more
 * free iyesdes than average (starting at parent's group).
 */

static int find_group_orlov(struct super_block *sb, struct iyesde *parent,
			    ext4_group_t *group, umode_t mode,
			    const struct qstr *qstr)
{
	ext4_group_t parent_group = EXT4_I(parent)->i_block_group;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	ext4_group_t real_ngroups = ext4_get_groups_count(sb);
	int iyesdes_per_group = EXT4_INODES_PER_GROUP(sb);
	unsigned int freei, avefreei, grp_free;
	ext4_fsblk_t freeb, avefreec;
	unsigned int ndirs;
	int max_dirs, min_iyesdes;
	ext4_grpblk_t min_clusters;
	ext4_group_t i, grp, g, ngroups;
	struct ext4_group_desc *desc;
	struct orlov_stats stats;
	int flex_size = ext4_flex_bg_size(sbi);
	struct dx_hash_info hinfo;

	ngroups = real_ngroups;
	if (flex_size > 1) {
		ngroups = (real_ngroups + flex_size - 1) >>
			sbi->s_log_groups_per_flex;
		parent_group >>= sbi->s_log_groups_per_flex;
	}

	freei = percpu_counter_read_positive(&sbi->s_freeiyesdes_counter);
	avefreei = freei / ngroups;
	freeb = EXT4_C2B(sbi,
		percpu_counter_read_positive(&sbi->s_freeclusters_counter));
	avefreec = freeb;
	do_div(avefreec, ngroups);
	ndirs = percpu_counter_read_positive(&sbi->s_dirs_counter);

	if (S_ISDIR(mode) &&
	    ((parent == d_iyesde(sb->s_root)) ||
	     (ext4_test_iyesde_flag(parent, EXT4_INODE_TOPDIR)))) {
		int best_ndir = iyesdes_per_group;
		int ret = -1;

		if (qstr) {
			hinfo.hash_version = DX_HASH_HALF_MD4;
			hinfo.seed = sbi->s_hash_seed;
			ext4fs_dirhash(parent, qstr->name, qstr->len, &hinfo);
			grp = hinfo.hash;
		} else
			grp = prandom_u32();
		parent_group = (unsigned)grp % ngroups;
		for (i = 0; i < ngroups; i++) {
			g = (parent_group + i) % ngroups;
			get_orlov_stats(sb, g, flex_size, &stats);
			if (!stats.free_iyesdes)
				continue;
			if (stats.used_dirs >= best_ndir)
				continue;
			if (stats.free_iyesdes < avefreei)
				continue;
			if (stats.free_clusters < avefreec)
				continue;
			grp = g;
			ret = 0;
			best_ndir = stats.used_dirs;
		}
		if (ret)
			goto fallback;
	found_flex_bg:
		if (flex_size == 1) {
			*group = grp;
			return 0;
		}

		/*
		 * We pack iyesdes at the beginning of the flexgroup's
		 * iyesde tables.  Block allocation decisions will do
		 * something similar, although regular files will
		 * start at 2nd block group of the flexgroup.  See
		 * ext4_ext_find_goal() and ext4_find_near().
		 */
		grp *= flex_size;
		for (i = 0; i < flex_size; i++) {
			if (grp+i >= real_ngroups)
				break;
			desc = ext4_get_group_desc(sb, grp+i, NULL);
			if (desc && ext4_free_iyesdes_count(sb, desc)) {
				*group = grp+i;
				return 0;
			}
		}
		goto fallback;
	}

	max_dirs = ndirs / ngroups + iyesdes_per_group / 16;
	min_iyesdes = avefreei - iyesdes_per_group*flex_size / 4;
	if (min_iyesdes < 1)
		min_iyesdes = 1;
	min_clusters = avefreec - EXT4_CLUSTERS_PER_GROUP(sb)*flex_size / 4;

	/*
	 * Start looking in the flex group where we last allocated an
	 * iyesde for this parent directory
	 */
	if (EXT4_I(parent)->i_last_alloc_group != ~0) {
		parent_group = EXT4_I(parent)->i_last_alloc_group;
		if (flex_size > 1)
			parent_group >>= sbi->s_log_groups_per_flex;
	}

	for (i = 0; i < ngroups; i++) {
		grp = (parent_group + i) % ngroups;
		get_orlov_stats(sb, grp, flex_size, &stats);
		if (stats.used_dirs >= max_dirs)
			continue;
		if (stats.free_iyesdes < min_iyesdes)
			continue;
		if (stats.free_clusters < min_clusters)
			continue;
		goto found_flex_bg;
	}

fallback:
	ngroups = real_ngroups;
	avefreei = freei / ngroups;
fallback_retry:
	parent_group = EXT4_I(parent)->i_block_group;
	for (i = 0; i < ngroups; i++) {
		grp = (parent_group + i) % ngroups;
		desc = ext4_get_group_desc(sb, grp, NULL);
		if (desc) {
			grp_free = ext4_free_iyesdes_count(sb, desc);
			if (grp_free && grp_free >= avefreei) {
				*group = grp;
				return 0;
			}
		}
	}

	if (avefreei) {
		/*
		 * The free-iyesdes counter is approximate, and for really small
		 * filesystems the above test can fail to find any blockgroups
		 */
		avefreei = 0;
		goto fallback_retry;
	}

	return -1;
}

static int find_group_other(struct super_block *sb, struct iyesde *parent,
			    ext4_group_t *group, umode_t mode)
{
	ext4_group_t parent_group = EXT4_I(parent)->i_block_group;
	ext4_group_t i, last, ngroups = ext4_get_groups_count(sb);
	struct ext4_group_desc *desc;
	int flex_size = ext4_flex_bg_size(EXT4_SB(sb));

	/*
	 * Try to place the iyesde is the same flex group as its
	 * parent.  If we can't find space, use the Orlov algorithm to
	 * find ayesther flex group, and store that information in the
	 * parent directory's iyesde information so that use that flex
	 * group for future allocations.
	 */
	if (flex_size > 1) {
		int retry = 0;

	try_again:
		parent_group &= ~(flex_size-1);
		last = parent_group + flex_size;
		if (last > ngroups)
			last = ngroups;
		for  (i = parent_group; i < last; i++) {
			desc = ext4_get_group_desc(sb, i, NULL);
			if (desc && ext4_free_iyesdes_count(sb, desc)) {
				*group = i;
				return 0;
			}
		}
		if (!retry && EXT4_I(parent)->i_last_alloc_group != ~0) {
			retry = 1;
			parent_group = EXT4_I(parent)->i_last_alloc_group;
			goto try_again;
		}
		/*
		 * If this didn't work, use the Orlov search algorithm
		 * to find a new flex group; we pass in the mode to
		 * avoid the topdir algorithms.
		 */
		*group = parent_group + flex_size;
		if (*group > ngroups)
			*group = 0;
		return find_group_orlov(sb, parent, group, mode, NULL);
	}

	/*
	 * Try to place the iyesde in its parent directory
	 */
	*group = parent_group;
	desc = ext4_get_group_desc(sb, *group, NULL);
	if (desc && ext4_free_iyesdes_count(sb, desc) &&
	    ext4_free_group_clusters(sb, desc))
		return 0;

	/*
	 * We're going to place this iyesde in a different blockgroup from its
	 * parent.  We want to cause files in a common directory to all land in
	 * the same blockgroup.  But we want files which are in a different
	 * directory which shares a blockgroup with our parent to land in a
	 * different blockgroup.
	 *
	 * So add our directory's i_iyes into the starting point for the hash.
	 */
	*group = (*group + parent->i_iyes) % ngroups;

	/*
	 * Use a quadratic hash to find a group with a free iyesde and some free
	 * blocks.
	 */
	for (i = 1; i < ngroups; i <<= 1) {
		*group += i;
		if (*group >= ngroups)
			*group -= ngroups;
		desc = ext4_get_group_desc(sb, *group, NULL);
		if (desc && ext4_free_iyesdes_count(sb, desc) &&
		    ext4_free_group_clusters(sb, desc))
			return 0;
	}

	/*
	 * That failed: try linear search for a free iyesde, even if that group
	 * has yes free blocks.
	 */
	*group = parent_group;
	for (i = 0; i < ngroups; i++) {
		if (++*group >= ngroups)
			*group = 0;
		desc = ext4_get_group_desc(sb, *group, NULL);
		if (desc && ext4_free_iyesdes_count(sb, desc))
			return 0;
	}

	return -1;
}

/*
 * In yes journal mode, if an iyesde has recently been deleted, we want
 * to avoid reusing it until we're reasonably sure the iyesde table
 * block has been written back to disk.  (Yes, these values are
 * somewhat arbitrary...)
 */
#define RECENTCY_MIN	5
#define RECENTCY_DIRTY	300

static int recently_deleted(struct super_block *sb, ext4_group_t group, int iyes)
{
	struct ext4_group_desc	*gdp;
	struct ext4_iyesde	*raw_iyesde;
	struct buffer_head	*bh;
	int iyesdes_per_block = EXT4_SB(sb)->s_iyesdes_per_block;
	int offset, ret = 0;
	int recentcy = RECENTCY_MIN;
	u32 dtime, yesw;

	gdp = ext4_get_group_desc(sb, group, NULL);
	if (unlikely(!gdp))
		return 0;

	bh = sb_find_get_block(sb, ext4_iyesde_table(sb, gdp) +
		       (iyes / iyesdes_per_block));
	if (!bh || !buffer_uptodate(bh))
		/*
		 * If the block is yest in the buffer cache, then it
		 * must have been written out.
		 */
		goto out;

	offset = (iyes % iyesdes_per_block) * EXT4_INODE_SIZE(sb);
	raw_iyesde = (struct ext4_iyesde *) (bh->b_data + offset);

	/* i_dtime is only 32 bits on disk, but we only care about relative
	 * times in the range of a few minutes (i.e. long eyesugh to sync a
	 * recently-deleted iyesde to disk), so using the low 32 bits of the
	 * clock (a 68 year range) is eyesugh, see time_before32() */
	dtime = le32_to_cpu(raw_iyesde->i_dtime);
	yesw = ktime_get_real_seconds();
	if (buffer_dirty(bh))
		recentcy += RECENTCY_DIRTY;

	if (dtime && time_before32(dtime, yesw) &&
	    time_before32(yesw, dtime + recentcy))
		ret = 1;
out:
	brelse(bh);
	return ret;
}

static int find_iyesde_bit(struct super_block *sb, ext4_group_t group,
			  struct buffer_head *bitmap, unsigned long *iyes)
{
next:
	*iyes = ext4_find_next_zero_bit((unsigned long *)
				       bitmap->b_data,
				       EXT4_INODES_PER_GROUP(sb), *iyes);
	if (*iyes >= EXT4_INODES_PER_GROUP(sb))
		return 0;

	if ((EXT4_SB(sb)->s_journal == NULL) &&
	    recently_deleted(sb, group, *iyes)) {
		*iyes = *iyes + 1;
		if (*iyes < EXT4_INODES_PER_GROUP(sb))
			goto next;
		return 0;
	}

	return 1;
}

/*
 * There are two policies for allocating an iyesde.  If the new iyesde is
 * a directory, then a forward search is made for a block group with both
 * free space and a low directory-to-iyesde ratio; if that fails, then of
 * the groups with above-average free space, that group with the fewest
 * directories already is chosen.
 *
 * For other iyesdes, search forward from the parent directory's block
 * group to find a free iyesde.
 */
struct iyesde *__ext4_new_iyesde(handle_t *handle, struct iyesde *dir,
			       umode_t mode, const struct qstr *qstr,
			       __u32 goal, uid_t *owner, __u32 i_flags,
			       int handle_type, unsigned int line_yes,
			       int nblocks)
{
	struct super_block *sb;
	struct buffer_head *iyesde_bitmap_bh = NULL;
	struct buffer_head *group_desc_bh;
	ext4_group_t ngroups, group = 0;
	unsigned long iyes = 0;
	struct iyesde *iyesde;
	struct ext4_group_desc *gdp = NULL;
	struct ext4_iyesde_info *ei;
	struct ext4_sb_info *sbi;
	int ret2, err;
	struct iyesde *ret;
	ext4_group_t i;
	ext4_group_t flex_group;
	struct ext4_group_info *grp;
	int encrypt = 0;

	/* Canyest create files in a deleted directory */
	if (!dir || !dir->i_nlink)
		return ERR_PTR(-EPERM);

	sb = dir->i_sb;
	sbi = EXT4_SB(sb);

	if (unlikely(ext4_forced_shutdown(sbi)))
		return ERR_PTR(-EIO);

	if ((IS_ENCRYPTED(dir) || DUMMY_ENCRYPTION_ENABLED(sbi)) &&
	    (S_ISREG(mode) || S_ISDIR(mode) || S_ISLNK(mode)) &&
	    !(i_flags & EXT4_EA_INODE_FL)) {
		err = fscrypt_get_encryption_info(dir);
		if (err)
			return ERR_PTR(err);
		if (!fscrypt_has_encryption_key(dir))
			return ERR_PTR(-ENOKEY);
		encrypt = 1;
	}

	if (!handle && sbi->s_journal && !(i_flags & EXT4_EA_INODE_FL)) {
#ifdef CONFIG_EXT4_FS_POSIX_ACL
		struct posix_acl *p = get_acl(dir, ACL_TYPE_DEFAULT);

		if (IS_ERR(p))
			return ERR_CAST(p);
		if (p) {
			int acl_size = p->a_count * sizeof(ext4_acl_entry);

			nblocks += (S_ISDIR(mode) ? 2 : 1) *
				__ext4_xattr_set_credits(sb, NULL /* iyesde */,
					NULL /* block_bh */, acl_size,
					true /* is_create */);
			posix_acl_release(p);
		}
#endif

#ifdef CONFIG_SECURITY
		{
			int num_security_xattrs = 1;

#ifdef CONFIG_INTEGRITY
			num_security_xattrs++;
#endif
			/*
			 * We assume that security xattrs are never
			 * more than 1k.  In practice they are under
			 * 128 bytes.
			 */
			nblocks += num_security_xattrs *
				__ext4_xattr_set_credits(sb, NULL /* iyesde */,
					NULL /* block_bh */, 1024,
					true /* is_create */);
		}
#endif
		if (encrypt)
			nblocks += __ext4_xattr_set_credits(sb,
					NULL /* iyesde */, NULL /* block_bh */,
					FSCRYPT_SET_CONTEXT_MAX_SIZE,
					true /* is_create */);
	}

	ngroups = ext4_get_groups_count(sb);
	trace_ext4_request_iyesde(dir, mode);
	iyesde = new_iyesde(sb);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);
	ei = EXT4_I(iyesde);

	/*
	 * Initialize owners and quota early so that we don't have to account
	 * for quota initialization worst case in standard iyesde creating
	 * transaction
	 */
	if (owner) {
		iyesde->i_mode = mode;
		i_uid_write(iyesde, owner[0]);
		i_gid_write(iyesde, owner[1]);
	} else if (test_opt(sb, GRPID)) {
		iyesde->i_mode = mode;
		iyesde->i_uid = current_fsuid();
		iyesde->i_gid = dir->i_gid;
	} else
		iyesde_init_owner(iyesde, dir, mode);

	if (ext4_has_feature_project(sb) &&
	    ext4_test_iyesde_flag(dir, EXT4_INODE_PROJINHERIT))
		ei->i_projid = EXT4_I(dir)->i_projid;
	else
		ei->i_projid = make_kprojid(&init_user_ns, EXT4_DEF_PROJID);

	err = dquot_initialize(iyesde);
	if (err)
		goto out;

	if (!goal)
		goal = sbi->s_iyesde_goal;

	if (goal && goal <= le32_to_cpu(sbi->s_es->s_iyesdes_count)) {
		group = (goal - 1) / EXT4_INODES_PER_GROUP(sb);
		iyes = (goal - 1) % EXT4_INODES_PER_GROUP(sb);
		ret2 = 0;
		goto got_group;
	}

	if (S_ISDIR(mode))
		ret2 = find_group_orlov(sb, dir, &group, mode, qstr);
	else
		ret2 = find_group_other(sb, dir, &group, mode);

got_group:
	EXT4_I(dir)->i_last_alloc_group = group;
	err = -ENOSPC;
	if (ret2 == -1)
		goto out;

	/*
	 * Normally we will only go through one pass of this loop,
	 * unless we get unlucky and it turns out the group we selected
	 * had its last iyesde grabbed by someone else.
	 */
	for (i = 0; i < ngroups; i++, iyes = 0) {
		err = -EIO;

		gdp = ext4_get_group_desc(sb, group, &group_desc_bh);
		if (!gdp)
			goto out;

		/*
		 * Check free iyesdes count before loading bitmap.
		 */
		if (ext4_free_iyesdes_count(sb, gdp) == 0)
			goto next_group;

		grp = ext4_get_group_info(sb, group);
		/* Skip groups with already-kyeswn suspicious iyesde tables */
		if (EXT4_MB_GRP_IBITMAP_CORRUPT(grp))
			goto next_group;

		brelse(iyesde_bitmap_bh);
		iyesde_bitmap_bh = ext4_read_iyesde_bitmap(sb, group);
		/* Skip groups with suspicious iyesde tables */
		if (EXT4_MB_GRP_IBITMAP_CORRUPT(grp) ||
		    IS_ERR(iyesde_bitmap_bh)) {
			iyesde_bitmap_bh = NULL;
			goto next_group;
		}

repeat_in_this_group:
		ret2 = find_iyesde_bit(sb, group, iyesde_bitmap_bh, &iyes);
		if (!ret2)
			goto next_group;

		if (group == 0 && (iyes + 1) < EXT4_FIRST_INO(sb)) {
			ext4_error(sb, "reserved iyesde found cleared - "
				   "iyesde=%lu", iyes + 1);
			ext4_mark_group_bitmap_corrupted(sb, group,
					EXT4_GROUP_INFO_IBITMAP_CORRUPT);
			goto next_group;
		}

		if (!handle) {
			BUG_ON(nblocks <= 0);
			handle = __ext4_journal_start_sb(dir->i_sb, line_yes,
				 handle_type, nblocks, 0,
				 ext4_trans_default_revoke_credits(sb));
			if (IS_ERR(handle)) {
				err = PTR_ERR(handle);
				ext4_std_error(sb, err);
				goto out;
			}
		}
		BUFFER_TRACE(iyesde_bitmap_bh, "get_write_access");
		err = ext4_journal_get_write_access(handle, iyesde_bitmap_bh);
		if (err) {
			ext4_std_error(sb, err);
			goto out;
		}
		ext4_lock_group(sb, group);
		ret2 = ext4_test_and_set_bit(iyes, iyesde_bitmap_bh->b_data);
		if (ret2) {
			/* Someone already took the bit. Repeat the search
			 * with lock held.
			 */
			ret2 = find_iyesde_bit(sb, group, iyesde_bitmap_bh, &iyes);
			if (ret2) {
				ext4_set_bit(iyes, iyesde_bitmap_bh->b_data);
				ret2 = 0;
			} else {
				ret2 = 1; /* we didn't grab the iyesde */
			}
		}
		ext4_unlock_group(sb, group);
		iyes++;		/* the iyesde bitmap is zero-based */
		if (!ret2)
			goto got; /* we grabbed the iyesde! */

		if (iyes < EXT4_INODES_PER_GROUP(sb))
			goto repeat_in_this_group;
next_group:
		if (++group == ngroups)
			group = 0;
	}
	err = -ENOSPC;
	goto out;

got:
	BUFFER_TRACE(iyesde_bitmap_bh, "call ext4_handle_dirty_metadata");
	err = ext4_handle_dirty_metadata(handle, NULL, iyesde_bitmap_bh);
	if (err) {
		ext4_std_error(sb, err);
		goto out;
	}

	BUFFER_TRACE(group_desc_bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, group_desc_bh);
	if (err) {
		ext4_std_error(sb, err);
		goto out;
	}

	/* We may have to initialize the block bitmap if it isn't already */
	if (ext4_has_group_desc_csum(sb) &&
	    gdp->bg_flags & cpu_to_le16(EXT4_BG_BLOCK_UNINIT)) {
		struct buffer_head *block_bitmap_bh;

		block_bitmap_bh = ext4_read_block_bitmap(sb, group);
		if (IS_ERR(block_bitmap_bh)) {
			err = PTR_ERR(block_bitmap_bh);
			goto out;
		}
		BUFFER_TRACE(block_bitmap_bh, "get block bitmap access");
		err = ext4_journal_get_write_access(handle, block_bitmap_bh);
		if (err) {
			brelse(block_bitmap_bh);
			ext4_std_error(sb, err);
			goto out;
		}

		BUFFER_TRACE(block_bitmap_bh, "dirty block bitmap");
		err = ext4_handle_dirty_metadata(handle, NULL, block_bitmap_bh);

		/* recheck and clear flag under lock if we still need to */
		ext4_lock_group(sb, group);
		if (ext4_has_group_desc_csum(sb) &&
		    (gdp->bg_flags & cpu_to_le16(EXT4_BG_BLOCK_UNINIT))) {
			gdp->bg_flags &= cpu_to_le16(~EXT4_BG_BLOCK_UNINIT);
			ext4_free_group_clusters_set(sb, gdp,
				ext4_free_clusters_after_init(sb, group, gdp));
			ext4_block_bitmap_csum_set(sb, group, gdp,
						   block_bitmap_bh);
			ext4_group_desc_csum_set(sb, group, gdp);
		}
		ext4_unlock_group(sb, group);
		brelse(block_bitmap_bh);

		if (err) {
			ext4_std_error(sb, err);
			goto out;
		}
	}

	/* Update the relevant bg descriptor fields */
	if (ext4_has_group_desc_csum(sb)) {
		int free;
		struct ext4_group_info *grp = ext4_get_group_info(sb, group);

		down_read(&grp->alloc_sem); /* protect vs itable lazyinit */
		ext4_lock_group(sb, group); /* while we modify the bg desc */
		free = EXT4_INODES_PER_GROUP(sb) -
			ext4_itable_unused_count(sb, gdp);
		if (gdp->bg_flags & cpu_to_le16(EXT4_BG_INODE_UNINIT)) {
			gdp->bg_flags &= cpu_to_le16(~EXT4_BG_INODE_UNINIT);
			free = 0;
		}
		/*
		 * Check the relative iyesde number against the last used
		 * relative iyesde number in this group. if it is greater
		 * we need to update the bg_itable_unused count
		 */
		if (iyes > free)
			ext4_itable_unused_set(sb, gdp,
					(EXT4_INODES_PER_GROUP(sb) - iyes));
		up_read(&grp->alloc_sem);
	} else {
		ext4_lock_group(sb, group);
	}

	ext4_free_iyesdes_set(sb, gdp, ext4_free_iyesdes_count(sb, gdp) - 1);
	if (S_ISDIR(mode)) {
		ext4_used_dirs_set(sb, gdp, ext4_used_dirs_count(sb, gdp) + 1);
		if (sbi->s_log_groups_per_flex) {
			ext4_group_t f = ext4_flex_group(sbi, group);

			atomic_inc(&sbi->s_flex_groups[f].used_dirs);
		}
	}
	if (ext4_has_group_desc_csum(sb)) {
		ext4_iyesde_bitmap_csum_set(sb, group, gdp, iyesde_bitmap_bh,
					   EXT4_INODES_PER_GROUP(sb) / 8);
		ext4_group_desc_csum_set(sb, group, gdp);
	}
	ext4_unlock_group(sb, group);

	BUFFER_TRACE(group_desc_bh, "call ext4_handle_dirty_metadata");
	err = ext4_handle_dirty_metadata(handle, NULL, group_desc_bh);
	if (err) {
		ext4_std_error(sb, err);
		goto out;
	}

	percpu_counter_dec(&sbi->s_freeiyesdes_counter);
	if (S_ISDIR(mode))
		percpu_counter_inc(&sbi->s_dirs_counter);

	if (sbi->s_log_groups_per_flex) {
		flex_group = ext4_flex_group(sbi, group);
		atomic_dec(&sbi->s_flex_groups[flex_group].free_iyesdes);
	}

	iyesde->i_iyes = iyes + group * EXT4_INODES_PER_GROUP(sb);
	/* This is the optimal IO size (for stat), yest the fs block size */
	iyesde->i_blocks = 0;
	iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
	ei->i_crtime = iyesde->i_mtime;

	memset(ei->i_data, 0, sizeof(ei->i_data));
	ei->i_dir_start_lookup = 0;
	ei->i_disksize = 0;

	/* Don't inherit extent flag from directory, amongst others. */
	ei->i_flags =
		ext4_mask_flags(mode, EXT4_I(dir)->i_flags & EXT4_FL_INHERITED);
	ei->i_flags |= i_flags;
	ei->i_file_acl = 0;
	ei->i_dtime = 0;
	ei->i_block_group = group;
	ei->i_last_alloc_group = ~0;

	ext4_set_iyesde_flags(iyesde);
	if (IS_DIRSYNC(iyesde))
		ext4_handle_sync(handle);
	if (insert_iyesde_locked(iyesde) < 0) {
		/*
		 * Likely a bitmap corruption causing iyesde to be allocated
		 * twice.
		 */
		err = -EIO;
		ext4_error(sb, "failed to insert iyesde %lu: doubly allocated?",
			   iyesde->i_iyes);
		ext4_mark_group_bitmap_corrupted(sb, group,
					EXT4_GROUP_INFO_IBITMAP_CORRUPT);
		goto out;
	}
	iyesde->i_generation = prandom_u32();

	/* Precompute checksum seed for iyesde metadata */
	if (ext4_has_metadata_csum(sb)) {
		__u32 csum;
		__le32 inum = cpu_to_le32(iyesde->i_iyes);
		__le32 gen = cpu_to_le32(iyesde->i_generation);
		csum = ext4_chksum(sbi, sbi->s_csum_seed, (__u8 *)&inum,
				   sizeof(inum));
		ei->i_csum_seed = ext4_chksum(sbi, csum, (__u8 *)&gen,
					      sizeof(gen));
	}

	ext4_clear_state_flags(ei); /* Only relevant on 32-bit archs */
	ext4_set_iyesde_state(iyesde, EXT4_STATE_NEW);

	ei->i_extra_isize = sbi->s_want_extra_isize;
	ei->i_inline_off = 0;
	if (ext4_has_feature_inline_data(sb))
		ext4_set_iyesde_state(iyesde, EXT4_STATE_MAY_INLINE_DATA);
	ret = iyesde;
	err = dquot_alloc_iyesde(iyesde);
	if (err)
		goto fail_drop;

	/*
	 * Since the encryption xattr will always be unique, create it first so
	 * that it's less likely to end up in an external xattr block and
	 * prevent its deduplication.
	 */
	if (encrypt) {
		err = fscrypt_inherit_context(dir, iyesde, handle, true);
		if (err)
			goto fail_free_drop;
	}

	if (!(ei->i_flags & EXT4_EA_INODE_FL)) {
		err = ext4_init_acl(handle, iyesde, dir);
		if (err)
			goto fail_free_drop;

		err = ext4_init_security(handle, iyesde, dir, qstr);
		if (err)
			goto fail_free_drop;
	}

	if (ext4_has_feature_extents(sb)) {
		/* set extent flag only for directory, file and yesrmal symlink*/
		if (S_ISDIR(mode) || S_ISREG(mode) || S_ISLNK(mode)) {
			ext4_set_iyesde_flag(iyesde, EXT4_INODE_EXTENTS);
			ext4_ext_tree_init(handle, iyesde);
		}
	}

	if (ext4_handle_valid(handle)) {
		ei->i_sync_tid = handle->h_transaction->t_tid;
		ei->i_datasync_tid = handle->h_transaction->t_tid;
	}

	err = ext4_mark_iyesde_dirty(handle, iyesde);
	if (err) {
		ext4_std_error(sb, err);
		goto fail_free_drop;
	}

	ext4_debug("allocating iyesde %lu\n", iyesde->i_iyes);
	trace_ext4_allocate_iyesde(iyesde, dir, mode);
	brelse(iyesde_bitmap_bh);
	return ret;

fail_free_drop:
	dquot_free_iyesde(iyesde);
fail_drop:
	clear_nlink(iyesde);
	unlock_new_iyesde(iyesde);
out:
	dquot_drop(iyesde);
	iyesde->i_flags |= S_NOQUOTA;
	iput(iyesde);
	brelse(iyesde_bitmap_bh);
	return ERR_PTR(err);
}

/* Verify that we are loading a valid orphan from disk */
struct iyesde *ext4_orphan_get(struct super_block *sb, unsigned long iyes)
{
	unsigned long max_iyes = le32_to_cpu(EXT4_SB(sb)->s_es->s_iyesdes_count);
	ext4_group_t block_group;
	int bit;
	struct buffer_head *bitmap_bh = NULL;
	struct iyesde *iyesde = NULL;
	int err = -EFSCORRUPTED;

	if (iyes < EXT4_FIRST_INO(sb) || iyes > max_iyes)
		goto bad_orphan;

	block_group = (iyes - 1) / EXT4_INODES_PER_GROUP(sb);
	bit = (iyes - 1) % EXT4_INODES_PER_GROUP(sb);
	bitmap_bh = ext4_read_iyesde_bitmap(sb, block_group);
	if (IS_ERR(bitmap_bh))
		return ERR_CAST(bitmap_bh);

	/* Having the iyesde bit set should be a 100% indicator that this
	 * is a valid orphan (yes e2fsck run on fs).  Orphans also include
	 * iyesdes that were being truncated, so we can't check i_nlink==0.
	 */
	if (!ext4_test_bit(bit, bitmap_bh->b_data))
		goto bad_orphan;

	iyesde = ext4_iget(sb, iyes, EXT4_IGET_NORMAL);
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		ext4_error(sb, "couldn't read orphan iyesde %lu (err %d)",
			   iyes, err);
		return iyesde;
	}

	/*
	 * If the orphans has i_nlinks > 0 then it should be able to
	 * be truncated, otherwise it won't be removed from the orphan
	 * list during processing and an infinite loop will result.
	 * Similarly, it must yest be a bad iyesde.
	 */
	if ((iyesde->i_nlink && !ext4_can_truncate(iyesde)) ||
	    is_bad_iyesde(iyesde))
		goto bad_orphan;

	if (NEXT_ORPHAN(iyesde) > max_iyes)
		goto bad_orphan;
	brelse(bitmap_bh);
	return iyesde;

bad_orphan:
	ext4_error(sb, "bad orphan iyesde %lu", iyes);
	if (bitmap_bh)
		printk(KERN_ERR "ext4_test_bit(bit=%d, block=%llu) = %d\n",
		       bit, (unsigned long long)bitmap_bh->b_blocknr,
		       ext4_test_bit(bit, bitmap_bh->b_data));
	if (iyesde) {
		printk(KERN_ERR "is_bad_iyesde(iyesde)=%d\n",
		       is_bad_iyesde(iyesde));
		printk(KERN_ERR "NEXT_ORPHAN(iyesde)=%u\n",
		       NEXT_ORPHAN(iyesde));
		printk(KERN_ERR "max_iyes=%lu\n", max_iyes);
		printk(KERN_ERR "i_nlink=%u\n", iyesde->i_nlink);
		/* Avoid freeing blocks if we got a bad deleted iyesde */
		if (iyesde->i_nlink == 0)
			iyesde->i_blocks = 0;
		iput(iyesde);
	}
	brelse(bitmap_bh);
	return ERR_PTR(err);
}

unsigned long ext4_count_free_iyesdes(struct super_block *sb)
{
	unsigned long desc_count;
	struct ext4_group_desc *gdp;
	ext4_group_t i, ngroups = ext4_get_groups_count(sb);
#ifdef EXT4FS_DEBUG
	struct ext4_super_block *es;
	unsigned long bitmap_count, x;
	struct buffer_head *bitmap_bh = NULL;

	es = EXT4_SB(sb)->s_es;
	desc_count = 0;
	bitmap_count = 0;
	gdp = NULL;
	for (i = 0; i < ngroups; i++) {
		gdp = ext4_get_group_desc(sb, i, NULL);
		if (!gdp)
			continue;
		desc_count += ext4_free_iyesdes_count(sb, gdp);
		brelse(bitmap_bh);
		bitmap_bh = ext4_read_iyesde_bitmap(sb, i);
		if (IS_ERR(bitmap_bh)) {
			bitmap_bh = NULL;
			continue;
		}

		x = ext4_count_free(bitmap_bh->b_data,
				    EXT4_INODES_PER_GROUP(sb) / 8);
		printk(KERN_DEBUG "group %lu: stored = %d, counted = %lu\n",
			(unsigned long) i, ext4_free_iyesdes_count(sb, gdp), x);
		bitmap_count += x;
	}
	brelse(bitmap_bh);
	printk(KERN_DEBUG "ext4_count_free_iyesdes: "
	       "stored = %u, computed = %lu, %lu\n",
	       le32_to_cpu(es->s_free_iyesdes_count), desc_count, bitmap_count);
	return desc_count;
#else
	desc_count = 0;
	for (i = 0; i < ngroups; i++) {
		gdp = ext4_get_group_desc(sb, i, NULL);
		if (!gdp)
			continue;
		desc_count += ext4_free_iyesdes_count(sb, gdp);
		cond_resched();
	}
	return desc_count;
#endif
}

/* Called at mount-time, super-block is locked */
unsigned long ext4_count_dirs(struct super_block * sb)
{
	unsigned long count = 0;
	ext4_group_t i, ngroups = ext4_get_groups_count(sb);

	for (i = 0; i < ngroups; i++) {
		struct ext4_group_desc *gdp = ext4_get_group_desc(sb, i, NULL);
		if (!gdp)
			continue;
		count += ext4_used_dirs_count(sb, gdp);
	}
	return count;
}

/*
 * Zeroes yest yet zeroed iyesde table - just write zeroes through the whole
 * iyesde table. Must be called without any spinlock held. The only place
 * where it is called from on active part of filesystem is ext4lazyinit
 * thread, so we do yest need any special locks, however we have to prevent
 * iyesde allocation from the current group, so we take alloc_sem lock, to
 * block ext4_new_iyesde() until we are finished.
 */
int ext4_init_iyesde_table(struct super_block *sb, ext4_group_t group,
				 int barrier)
{
	struct ext4_group_info *grp = ext4_get_group_info(sb, group);
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_group_desc *gdp = NULL;
	struct buffer_head *group_desc_bh;
	handle_t *handle;
	ext4_fsblk_t blk;
	int num, ret = 0, used_blks = 0;

	/* This should yest happen, but just to be sure check this */
	if (sb_rdonly(sb)) {
		ret = 1;
		goto out;
	}

	gdp = ext4_get_group_desc(sb, group, &group_desc_bh);
	if (!gdp)
		goto out;

	/*
	 * We do yest need to lock this, because we are the only one
	 * handling this flag.
	 */
	if (gdp->bg_flags & cpu_to_le16(EXT4_BG_INODE_ZEROED))
		goto out;

	handle = ext4_journal_start_sb(sb, EXT4_HT_MISC, 1);
	if (IS_ERR(handle)) {
		ret = PTR_ERR(handle);
		goto out;
	}

	down_write(&grp->alloc_sem);
	/*
	 * If iyesde bitmap was already initialized there may be some
	 * used iyesdes so we need to skip blocks with used iyesdes in
	 * iyesde table.
	 */
	if (!(gdp->bg_flags & cpu_to_le16(EXT4_BG_INODE_UNINIT)))
		used_blks = DIV_ROUND_UP((EXT4_INODES_PER_GROUP(sb) -
			    ext4_itable_unused_count(sb, gdp)),
			    sbi->s_iyesdes_per_block);

	if ((used_blks < 0) || (used_blks > sbi->s_itb_per_group) ||
	    ((group == 0) && ((EXT4_INODES_PER_GROUP(sb) -
			       ext4_itable_unused_count(sb, gdp)) <
			      EXT4_FIRST_INO(sb)))) {
		ext4_error(sb, "Something is wrong with group %u: "
			   "used itable blocks: %d; "
			   "itable unused count: %u",
			   group, used_blks,
			   ext4_itable_unused_count(sb, gdp));
		ret = 1;
		goto err_out;
	}

	blk = ext4_iyesde_table(sb, gdp) + used_blks;
	num = sbi->s_itb_per_group - used_blks;

	BUFFER_TRACE(group_desc_bh, "get_write_access");
	ret = ext4_journal_get_write_access(handle,
					    group_desc_bh);
	if (ret)
		goto err_out;

	/*
	 * Skip zeroout if the iyesde table is full. But we set the ZEROED
	 * flag anyway, because obviously, when it is full it does yest need
	 * further zeroing.
	 */
	if (unlikely(num == 0))
		goto skip_zeroout;

	ext4_debug("going to zero out iyesde table in group %d\n",
		   group);
	ret = sb_issue_zeroout(sb, blk, num, GFP_NOFS);
	if (ret < 0)
		goto err_out;
	if (barrier)
		blkdev_issue_flush(sb->s_bdev, GFP_NOFS, NULL);

skip_zeroout:
	ext4_lock_group(sb, group);
	gdp->bg_flags |= cpu_to_le16(EXT4_BG_INODE_ZEROED);
	ext4_group_desc_csum_set(sb, group, gdp);
	ext4_unlock_group(sb, group);

	BUFFER_TRACE(group_desc_bh,
		     "call ext4_handle_dirty_metadata");
	ret = ext4_handle_dirty_metadata(handle, NULL,
					 group_desc_bh);

err_out:
	up_write(&grp->alloc_sem);
	ext4_journal_stop(handle);
out:
	return ret;
}
