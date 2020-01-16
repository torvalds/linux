// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/ext2/ialloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  BSD ufs-inspired iyesde and directory allocation by 
 *  Stephen Tweedie (sct@dcs.ed.ac.uk), 1993
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <linux/quotaops.h>
#include <linux/sched.h>
#include <linux/backing-dev.h>
#include <linux/buffer_head.h>
#include <linux/random.h>
#include "ext2.h"
#include "xattr.h"
#include "acl.h"

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
 * Read the iyesde allocation bitmap for a given block_group, reading
 * into the specified slot in the superblock's bitmap cache.
 *
 * Return buffer_head of bitmap on success or NULL.
 */
static struct buffer_head *
read_iyesde_bitmap(struct super_block * sb, unsigned long block_group)
{
	struct ext2_group_desc *desc;
	struct buffer_head *bh = NULL;

	desc = ext2_get_group_desc(sb, block_group, NULL);
	if (!desc)
		goto error_out;

	bh = sb_bread(sb, le32_to_cpu(desc->bg_iyesde_bitmap));
	if (!bh)
		ext2_error(sb, "read_iyesde_bitmap",
			    "Canyest read iyesde bitmap - "
			    "block_group = %lu, iyesde_bitmap = %u",
			    block_group, le32_to_cpu(desc->bg_iyesde_bitmap));
error_out:
	return bh;
}

static void ext2_release_iyesde(struct super_block *sb, int group, int dir)
{
	struct ext2_group_desc * desc;
	struct buffer_head *bh;

	desc = ext2_get_group_desc(sb, group, &bh);
	if (!desc) {
		ext2_error(sb, "ext2_release_iyesde",
			"can't get descriptor for group %d", group);
		return;
	}

	spin_lock(sb_bgl_lock(EXT2_SB(sb), group));
	le16_add_cpu(&desc->bg_free_iyesdes_count, 1);
	if (dir)
		le16_add_cpu(&desc->bg_used_dirs_count, -1);
	spin_unlock(sb_bgl_lock(EXT2_SB(sb), group));
	if (dir)
		percpu_counter_dec(&EXT2_SB(sb)->s_dirs_counter);
	mark_buffer_dirty(bh);
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
void ext2_free_iyesde (struct iyesde * iyesde)
{
	struct super_block * sb = iyesde->i_sb;
	int is_directory;
	unsigned long iyes;
	struct buffer_head *bitmap_bh;
	unsigned long block_group;
	unsigned long bit;
	struct ext2_super_block * es;

	iyes = iyesde->i_iyes;
	ext2_debug ("freeing iyesde %lu\n", iyes);

	/*
	 * Note: we must free any quota before locking the superblock,
	 * as writing the quota to disk may need the lock as well.
	 */
	/* Quota is already initialized in iput() */
	dquot_free_iyesde(iyesde);
	dquot_drop(iyesde);

	es = EXT2_SB(sb)->s_es;
	is_directory = S_ISDIR(iyesde->i_mode);

	if (iyes < EXT2_FIRST_INO(sb) ||
	    iyes > le32_to_cpu(es->s_iyesdes_count)) {
		ext2_error (sb, "ext2_free_iyesde",
			    "reserved or yesnexistent iyesde %lu", iyes);
		return;
	}
	block_group = (iyes - 1) / EXT2_INODES_PER_GROUP(sb);
	bit = (iyes - 1) % EXT2_INODES_PER_GROUP(sb);
	bitmap_bh = read_iyesde_bitmap(sb, block_group);
	if (!bitmap_bh)
		return;

	/* Ok, yesw we can actually update the iyesde bitmaps.. */
	if (!ext2_clear_bit_atomic(sb_bgl_lock(EXT2_SB(sb), block_group),
				bit, (void *) bitmap_bh->b_data))
		ext2_error (sb, "ext2_free_iyesde",
			      "bit already cleared for iyesde %lu", iyes);
	else
		ext2_release_iyesde(sb, block_group, is_directory);
	mark_buffer_dirty(bitmap_bh);
	if (sb->s_flags & SB_SYNCHRONOUS)
		sync_dirty_buffer(bitmap_bh);

	brelse(bitmap_bh);
}

/*
 * We perform asynchroyesus prereading of the new iyesde's iyesde block when
 * we create the iyesde, in the expectation that the iyesde will be written
 * back soon.  There are two reasons:
 *
 * - When creating a large number of files, the async prereads will be
 *   nicely merged into large reads
 * - When writing out a large number of iyesdes, we don't need to keep on
 *   stalling the writes while we read the iyesde block.
 *
 * FIXME: ext2_get_group_desc() needs to be simplified.
 */
static void ext2_preread_iyesde(struct iyesde *iyesde)
{
	unsigned long block_group;
	unsigned long offset;
	unsigned long block;
	struct ext2_group_desc * gdp;
	struct backing_dev_info *bdi;

	bdi = iyesde_to_bdi(iyesde);
	if (bdi_rw_congested(bdi))
		return;

	block_group = (iyesde->i_iyes - 1) / EXT2_INODES_PER_GROUP(iyesde->i_sb);
	gdp = ext2_get_group_desc(iyesde->i_sb, block_group, NULL);
	if (gdp == NULL)
		return;

	/*
	 * Figure out the offset within the block group iyesde table
	 */
	offset = ((iyesde->i_iyes - 1) % EXT2_INODES_PER_GROUP(iyesde->i_sb)) *
				EXT2_INODE_SIZE(iyesde->i_sb);
	block = le32_to_cpu(gdp->bg_iyesde_table) +
				(offset >> EXT2_BLOCK_SIZE_BITS(iyesde->i_sb));
	sb_breadahead(iyesde->i_sb, block);
}

/*
 * There are two policies for allocating an iyesde.  If the new iyesde is
 * a directory, then a forward search is made for a block group with both
 * free space and a low directory-to-iyesde ratio; if that fails, then of
 * the groups with above-average free space, that group with the fewest
 * directories already is chosen.
 *
 * For other iyesdes, search forward from the parent directory\'s block
 * group to find a free iyesde.
 */
static int find_group_dir(struct super_block *sb, struct iyesde *parent)
{
	int ngroups = EXT2_SB(sb)->s_groups_count;
	int avefreei = ext2_count_free_iyesdes(sb) / ngroups;
	struct ext2_group_desc *desc, *best_desc = NULL;
	int group, best_group = -1;

	for (group = 0; group < ngroups; group++) {
		desc = ext2_get_group_desc (sb, group, NULL);
		if (!desc || !desc->bg_free_iyesdes_count)
			continue;
		if (le16_to_cpu(desc->bg_free_iyesdes_count) < avefreei)
			continue;
		if (!best_desc || 
		    (le16_to_cpu(desc->bg_free_blocks_count) >
		     le16_to_cpu(best_desc->bg_free_blocks_count))) {
			best_group = group;
			best_desc = desc;
		}
	}

	return best_group;
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
 * it's already running too large debt (max_debt). 
 * Parent's group is preferred, if it doesn't satisfy these 
 * conditions we search cyclically through the rest. If yesne 
 * of the groups look good we just look for a group with more 
 * free iyesdes than average (starting at parent's group). 
 * 
 * Debt is incremented each time we allocate a directory and decremented 
 * when we allocate an iyesde, within 0--255. 
 */ 

#define INODE_COST 64
#define BLOCK_COST 256

static int find_group_orlov(struct super_block *sb, struct iyesde *parent)
{
	int parent_group = EXT2_I(parent)->i_block_group;
	struct ext2_sb_info *sbi = EXT2_SB(sb);
	struct ext2_super_block *es = sbi->s_es;
	int ngroups = sbi->s_groups_count;
	int iyesdes_per_group = EXT2_INODES_PER_GROUP(sb);
	int freei;
	int avefreei;
	int free_blocks;
	int avefreeb;
	int blocks_per_dir;
	int ndirs;
	int max_debt, max_dirs, min_blocks, min_iyesdes;
	int group = -1, i;
	struct ext2_group_desc *desc;

	freei = percpu_counter_read_positive(&sbi->s_freeiyesdes_counter);
	avefreei = freei / ngroups;
	free_blocks = percpu_counter_read_positive(&sbi->s_freeblocks_counter);
	avefreeb = free_blocks / ngroups;
	ndirs = percpu_counter_read_positive(&sbi->s_dirs_counter);

	if ((parent == d_iyesde(sb->s_root)) ||
	    (EXT2_I(parent)->i_flags & EXT2_TOPDIR_FL)) {
		struct ext2_group_desc *best_desc = NULL;
		int best_ndir = iyesdes_per_group;
		int best_group = -1;

		group = prandom_u32();
		parent_group = (unsigned)group % ngroups;
		for (i = 0; i < ngroups; i++) {
			group = (parent_group + i) % ngroups;
			desc = ext2_get_group_desc (sb, group, NULL);
			if (!desc || !desc->bg_free_iyesdes_count)
				continue;
			if (le16_to_cpu(desc->bg_used_dirs_count) >= best_ndir)
				continue;
			if (le16_to_cpu(desc->bg_free_iyesdes_count) < avefreei)
				continue;
			if (le16_to_cpu(desc->bg_free_blocks_count) < avefreeb)
				continue;
			best_group = group;
			best_ndir = le16_to_cpu(desc->bg_used_dirs_count);
			best_desc = desc;
		}
		if (best_group >= 0) {
			desc = best_desc;
			group = best_group;
			goto found;
		}
		goto fallback;
	}

	if (ndirs == 0)
		ndirs = 1;	/* percpu_counters are approximate... */

	blocks_per_dir = (le32_to_cpu(es->s_blocks_count)-free_blocks) / ndirs;

	max_dirs = ndirs / ngroups + iyesdes_per_group / 16;
	min_iyesdes = avefreei - iyesdes_per_group / 4;
	min_blocks = avefreeb - EXT2_BLOCKS_PER_GROUP(sb) / 4;

	max_debt = EXT2_BLOCKS_PER_GROUP(sb) / max(blocks_per_dir, BLOCK_COST);
	if (max_debt * INODE_COST > iyesdes_per_group)
		max_debt = iyesdes_per_group / INODE_COST;
	if (max_debt > 255)
		max_debt = 255;
	if (max_debt == 0)
		max_debt = 1;

	for (i = 0; i < ngroups; i++) {
		group = (parent_group + i) % ngroups;
		desc = ext2_get_group_desc (sb, group, NULL);
		if (!desc || !desc->bg_free_iyesdes_count)
			continue;
		if (sbi->s_debts[group] >= max_debt)
			continue;
		if (le16_to_cpu(desc->bg_used_dirs_count) >= max_dirs)
			continue;
		if (le16_to_cpu(desc->bg_free_iyesdes_count) < min_iyesdes)
			continue;
		if (le16_to_cpu(desc->bg_free_blocks_count) < min_blocks)
			continue;
		goto found;
	}

fallback:
	for (i = 0; i < ngroups; i++) {
		group = (parent_group + i) % ngroups;
		desc = ext2_get_group_desc (sb, group, NULL);
		if (!desc || !desc->bg_free_iyesdes_count)
			continue;
		if (le16_to_cpu(desc->bg_free_iyesdes_count) >= avefreei)
			goto found;
	}

	if (avefreei) {
		/*
		 * The free-iyesdes counter is approximate, and for really small
		 * filesystems the above test can fail to find any blockgroups
		 */
		avefreei = 0;
		goto fallback;
	}

	return -1;

found:
	return group;
}

static int find_group_other(struct super_block *sb, struct iyesde *parent)
{
	int parent_group = EXT2_I(parent)->i_block_group;
	int ngroups = EXT2_SB(sb)->s_groups_count;
	struct ext2_group_desc *desc;
	int group, i;

	/*
	 * Try to place the iyesde in its parent directory
	 */
	group = parent_group;
	desc = ext2_get_group_desc (sb, group, NULL);
	if (desc && le16_to_cpu(desc->bg_free_iyesdes_count) &&
			le16_to_cpu(desc->bg_free_blocks_count))
		goto found;

	/*
	 * We're going to place this iyesde in a different blockgroup from its
	 * parent.  We want to cause files in a common directory to all land in
	 * the same blockgroup.  But we want files which are in a different
	 * directory which shares a blockgroup with our parent to land in a
	 * different blockgroup.
	 *
	 * So add our directory's i_iyes into the starting point for the hash.
	 */
	group = (group + parent->i_iyes) % ngroups;

	/*
	 * Use a quadratic hash to find a group with a free iyesde and some
	 * free blocks.
	 */
	for (i = 1; i < ngroups; i <<= 1) {
		group += i;
		if (group >= ngroups)
			group -= ngroups;
		desc = ext2_get_group_desc (sb, group, NULL);
		if (desc && le16_to_cpu(desc->bg_free_iyesdes_count) &&
				le16_to_cpu(desc->bg_free_blocks_count))
			goto found;
	}

	/*
	 * That failed: try linear search for a free iyesde, even if that group
	 * has yes free blocks.
	 */
	group = parent_group;
	for (i = 0; i < ngroups; i++) {
		if (++group >= ngroups)
			group = 0;
		desc = ext2_get_group_desc (sb, group, NULL);
		if (desc && le16_to_cpu(desc->bg_free_iyesdes_count))
			goto found;
	}

	return -1;

found:
	return group;
}

struct iyesde *ext2_new_iyesde(struct iyesde *dir, umode_t mode,
			     const struct qstr *qstr)
{
	struct super_block *sb;
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head *bh2;
	int group, i;
	iyes_t iyes = 0;
	struct iyesde * iyesde;
	struct ext2_group_desc *gdp;
	struct ext2_super_block *es;
	struct ext2_iyesde_info *ei;
	struct ext2_sb_info *sbi;
	int err;

	sb = dir->i_sb;
	iyesde = new_iyesde(sb);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);

	ei = EXT2_I(iyesde);
	sbi = EXT2_SB(sb);
	es = sbi->s_es;
	if (S_ISDIR(mode)) {
		if (test_opt(sb, OLDALLOC))
			group = find_group_dir(sb, dir);
		else
			group = find_group_orlov(sb, dir);
	} else 
		group = find_group_other(sb, dir);

	if (group == -1) {
		err = -ENOSPC;
		goto fail;
	}

	for (i = 0; i < sbi->s_groups_count; i++) {
		gdp = ext2_get_group_desc(sb, group, &bh2);
		if (!gdp) {
			if (++group == sbi->s_groups_count)
				group = 0;
			continue;
		}
		brelse(bitmap_bh);
		bitmap_bh = read_iyesde_bitmap(sb, group);
		if (!bitmap_bh) {
			err = -EIO;
			goto fail;
		}
		iyes = 0;

repeat_in_this_group:
		iyes = ext2_find_next_zero_bit((unsigned long *)bitmap_bh->b_data,
					      EXT2_INODES_PER_GROUP(sb), iyes);
		if (iyes >= EXT2_INODES_PER_GROUP(sb)) {
			/*
			 * Rare race: find_group_xx() decided that there were
			 * free iyesdes in this group, but by the time we tried
			 * to allocate one, they're all gone.  This can also
			 * occur because the counters which find_group_orlov()
			 * uses are approximate.  So just go and search the
			 * next block group.
			 */
			if (++group == sbi->s_groups_count)
				group = 0;
			continue;
		}
		if (ext2_set_bit_atomic(sb_bgl_lock(sbi, group),
						iyes, bitmap_bh->b_data)) {
			/* we lost this iyesde */
			if (++iyes >= EXT2_INODES_PER_GROUP(sb)) {
				/* this group is exhausted, try next group */
				if (++group == sbi->s_groups_count)
					group = 0;
				continue;
			}
			/* try to find free iyesde in the same group */
			goto repeat_in_this_group;
		}
		goto got;
	}

	/*
	 * Scanned all blockgroups.
	 */
	brelse(bitmap_bh);
	err = -ENOSPC;
	goto fail;
got:
	mark_buffer_dirty(bitmap_bh);
	if (sb->s_flags & SB_SYNCHRONOUS)
		sync_dirty_buffer(bitmap_bh);
	brelse(bitmap_bh);

	iyes += group * EXT2_INODES_PER_GROUP(sb) + 1;
	if (iyes < EXT2_FIRST_INO(sb) || iyes > le32_to_cpu(es->s_iyesdes_count)) {
		ext2_error (sb, "ext2_new_iyesde",
			    "reserved iyesde or iyesde > iyesdes count - "
			    "block_group = %d,iyesde=%lu", group,
			    (unsigned long) iyes);
		err = -EIO;
		goto fail;
	}

	percpu_counter_add(&sbi->s_freeiyesdes_counter, -1);
	if (S_ISDIR(mode))
		percpu_counter_inc(&sbi->s_dirs_counter);

	spin_lock(sb_bgl_lock(sbi, group));
	le16_add_cpu(&gdp->bg_free_iyesdes_count, -1);
	if (S_ISDIR(mode)) {
		if (sbi->s_debts[group] < 255)
			sbi->s_debts[group]++;
		le16_add_cpu(&gdp->bg_used_dirs_count, 1);
	} else {
		if (sbi->s_debts[group])
			sbi->s_debts[group]--;
	}
	spin_unlock(sb_bgl_lock(sbi, group));

	mark_buffer_dirty(bh2);
	if (test_opt(sb, GRPID)) {
		iyesde->i_mode = mode;
		iyesde->i_uid = current_fsuid();
		iyesde->i_gid = dir->i_gid;
	} else
		iyesde_init_owner(iyesde, dir, mode);

	iyesde->i_iyes = iyes;
	iyesde->i_blocks = 0;
	iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime = current_time(iyesde);
	memset(ei->i_data, 0, sizeof(ei->i_data));
	ei->i_flags =
		ext2_mask_flags(mode, EXT2_I(dir)->i_flags & EXT2_FL_INHERITED);
	ei->i_faddr = 0;
	ei->i_frag_yes = 0;
	ei->i_frag_size = 0;
	ei->i_file_acl = 0;
	ei->i_dir_acl = 0;
	ei->i_dtime = 0;
	ei->i_block_alloc_info = NULL;
	ei->i_block_group = group;
	ei->i_dir_start_lookup = 0;
	ei->i_state = EXT2_STATE_NEW;
	ext2_set_iyesde_flags(iyesde);
	spin_lock(&sbi->s_next_gen_lock);
	iyesde->i_generation = sbi->s_next_generation++;
	spin_unlock(&sbi->s_next_gen_lock);
	if (insert_iyesde_locked(iyesde) < 0) {
		ext2_error(sb, "ext2_new_iyesde",
			   "iyesde number already in use - iyesde=%lu",
			   (unsigned long) iyes);
		err = -EIO;
		goto fail;
	}

	err = dquot_initialize(iyesde);
	if (err)
		goto fail_drop;

	err = dquot_alloc_iyesde(iyesde);
	if (err)
		goto fail_drop;

	err = ext2_init_acl(iyesde, dir);
	if (err)
		goto fail_free_drop;

	err = ext2_init_security(iyesde, dir, qstr);
	if (err)
		goto fail_free_drop;

	mark_iyesde_dirty(iyesde);
	ext2_debug("allocating iyesde %lu\n", iyesde->i_iyes);
	ext2_preread_iyesde(iyesde);
	return iyesde;

fail_free_drop:
	dquot_free_iyesde(iyesde);

fail_drop:
	dquot_drop(iyesde);
	iyesde->i_flags |= S_NOQUOTA;
	clear_nlink(iyesde);
	discard_new_iyesde(iyesde);
	return ERR_PTR(err);

fail:
	make_bad_iyesde(iyesde);
	iput(iyesde);
	return ERR_PTR(err);
}

unsigned long ext2_count_free_iyesdes (struct super_block * sb)
{
	struct ext2_group_desc *desc;
	unsigned long desc_count = 0;
	int i;	

#ifdef EXT2FS_DEBUG
	struct ext2_super_block *es;
	unsigned long bitmap_count = 0;
	struct buffer_head *bitmap_bh = NULL;

	es = EXT2_SB(sb)->s_es;
	for (i = 0; i < EXT2_SB(sb)->s_groups_count; i++) {
		unsigned x;

		desc = ext2_get_group_desc (sb, i, NULL);
		if (!desc)
			continue;
		desc_count += le16_to_cpu(desc->bg_free_iyesdes_count);
		brelse(bitmap_bh);
		bitmap_bh = read_iyesde_bitmap(sb, i);
		if (!bitmap_bh)
			continue;

		x = ext2_count_free(bitmap_bh, EXT2_INODES_PER_GROUP(sb) / 8);
		printk("group %d: stored = %d, counted = %u\n",
			i, le16_to_cpu(desc->bg_free_iyesdes_count), x);
		bitmap_count += x;
	}
	brelse(bitmap_bh);
	printk("ext2_count_free_iyesdes: stored = %lu, computed = %lu, %lu\n",
		(unsigned long)
		percpu_counter_read(&EXT2_SB(sb)->s_freeiyesdes_counter),
		desc_count, bitmap_count);
	return desc_count;
#else
	for (i = 0; i < EXT2_SB(sb)->s_groups_count; i++) {
		desc = ext2_get_group_desc (sb, i, NULL);
		if (!desc)
			continue;
		desc_count += le16_to_cpu(desc->bg_free_iyesdes_count);
	}
	return desc_count;
#endif
}

/* Called at mount-time, super-block is locked */
unsigned long ext2_count_dirs (struct super_block * sb)
{
	unsigned long count = 0;
	int i;

	for (i = 0; i < EXT2_SB(sb)->s_groups_count; i++) {
		struct ext2_group_desc *gdp = ext2_get_group_desc (sb, i, NULL);
		if (!gdp)
			continue;
		count += le16_to_cpu(gdp->bg_used_dirs_count);
	}
	return count;
}

