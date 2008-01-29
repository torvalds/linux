/*
 *  linux/fs/ext4/ialloc.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  BSD ufs-inspired inode and directory allocation by
 *  Stephen Tweedie (sct@redhat.com), 1993
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <linux/time.h>
#include <linux/fs.h>
#include <linux/jbd2.h>
#include <linux/ext4_fs.h>
#include <linux/ext4_jbd2.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/quotaops.h>
#include <linux/buffer_head.h>
#include <linux/random.h>
#include <linux/bitops.h>
#include <linux/blkdev.h>
#include <asm/byteorder.h>

#include "xattr.h"
#include "acl.h"
#include "group.h"

/*
 * ialloc.c contains the inodes allocation and deallocation routines
 */

/*
 * The free inodes are managed by bitmaps.  A file system contains several
 * blocks groups.  Each group contains 1 bitmap block for blocks, 1 bitmap
 * block for inodes, N blocks for the inode table and data blocks.
 *
 * The file system contains group descriptors which are located after the
 * super block.  Each descriptor contains the number of the bitmap block and
 * the free blocks count in the block.
 */

/*
 * To avoid calling the atomic setbit hundreds or thousands of times, we only
 * need to use it within a single byte (to ensure we get endianness right).
 * We can use memset for the rest of the bitmap as there are no other users.
 */
void mark_bitmap_end(int start_bit, int end_bit, char *bitmap)
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

/* Initializes an uninitialized inode bitmap */
unsigned ext4_init_inode_bitmap(struct super_block *sb, struct buffer_head *bh,
				ext4_group_t block_group,
				struct ext4_group_desc *gdp)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	J_ASSERT_BH(bh, buffer_locked(bh));

	/* If checksum is bad mark all blocks and inodes use to prevent
	 * allocation, essentially implementing a per-group read-only flag. */
	if (!ext4_group_desc_csum_verify(sbi, block_group, gdp)) {
		ext4_error(sb, __FUNCTION__, "Checksum bad for group %lu\n",
			   block_group);
		gdp->bg_free_blocks_count = 0;
		gdp->bg_free_inodes_count = 0;
		gdp->bg_itable_unused = 0;
		memset(bh->b_data, 0xff, sb->s_blocksize);
		return 0;
	}

	memset(bh->b_data, 0, (EXT4_INODES_PER_GROUP(sb) + 7) / 8);
	mark_bitmap_end(EXT4_INODES_PER_GROUP(sb), EXT4_BLOCKS_PER_GROUP(sb),
			bh->b_data);

	return EXT4_INODES_PER_GROUP(sb);
}

/*
 * Read the inode allocation bitmap for a given block_group, reading
 * into the specified slot in the superblock's bitmap cache.
 *
 * Return buffer_head of bitmap on success or NULL.
 */
static struct buffer_head *
read_inode_bitmap(struct super_block *sb, ext4_group_t block_group)
{
	struct ext4_group_desc *desc;
	struct buffer_head *bh = NULL;

	desc = ext4_get_group_desc(sb, block_group, NULL);
	if (!desc)
		goto error_out;
	if (desc->bg_flags & cpu_to_le16(EXT4_BG_INODE_UNINIT)) {
		bh = sb_getblk(sb, ext4_inode_bitmap(sb, desc));
		if (!buffer_uptodate(bh)) {
			lock_buffer(bh);
			if (!buffer_uptodate(bh)) {
				ext4_init_inode_bitmap(sb, bh, block_group,
						       desc);
				set_buffer_uptodate(bh);
			}
			unlock_buffer(bh);
		}
	} else {
		bh = sb_bread(sb, ext4_inode_bitmap(sb, desc));
	}
	if (!bh)
		ext4_error(sb, "read_inode_bitmap",
			    "Cannot read inode bitmap - "
			    "block_group = %lu, inode_bitmap = %llu",
			    block_group, ext4_inode_bitmap(sb, desc));
error_out:
	return bh;
}

/*
 * NOTE! When we get the inode, we're the only people
 * that have access to it, and as such there are no
 * race conditions we have to worry about. The inode
 * is not on the hash-lists, and it cannot be reached
 * through the filesystem because the directory entry
 * has been deleted earlier.
 *
 * HOWEVER: we must make sure that we get no aliases,
 * which means that we have to call "clear_inode()"
 * _before_ we mark the inode not in use in the inode
 * bitmaps. Otherwise a newly created file might use
 * the same inode number (not actually the same pointer
 * though), and then we'd have two inodes sharing the
 * same inode number and space on the harddisk.
 */
void ext4_free_inode (handle_t *handle, struct inode * inode)
{
	struct super_block * sb = inode->i_sb;
	int is_directory;
	unsigned long ino;
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head *bh2;
	ext4_group_t block_group;
	unsigned long bit;
	struct ext4_group_desc * gdp;
	struct ext4_super_block * es;
	struct ext4_sb_info *sbi;
	int fatal = 0, err;

	if (atomic_read(&inode->i_count) > 1) {
		printk ("ext4_free_inode: inode has count=%d\n",
					atomic_read(&inode->i_count));
		return;
	}
	if (inode->i_nlink) {
		printk ("ext4_free_inode: inode has nlink=%d\n",
			inode->i_nlink);
		return;
	}
	if (!sb) {
		printk("ext4_free_inode: inode on nonexistent device\n");
		return;
	}
	sbi = EXT4_SB(sb);

	ino = inode->i_ino;
	ext4_debug ("freeing inode %lu\n", ino);

	/*
	 * Note: we must free any quota before locking the superblock,
	 * as writing the quota to disk may need the lock as well.
	 */
	DQUOT_INIT(inode);
	ext4_xattr_delete_inode(handle, inode);
	DQUOT_FREE_INODE(inode);
	DQUOT_DROP(inode);

	is_directory = S_ISDIR(inode->i_mode);

	/* Do this BEFORE marking the inode not in use or returning an error */
	clear_inode (inode);

	es = EXT4_SB(sb)->s_es;
	if (ino < EXT4_FIRST_INO(sb) || ino > le32_to_cpu(es->s_inodes_count)) {
		ext4_error (sb, "ext4_free_inode",
			    "reserved or nonexistent inode %lu", ino);
		goto error_return;
	}
	block_group = (ino - 1) / EXT4_INODES_PER_GROUP(sb);
	bit = (ino - 1) % EXT4_INODES_PER_GROUP(sb);
	bitmap_bh = read_inode_bitmap(sb, block_group);
	if (!bitmap_bh)
		goto error_return;

	BUFFER_TRACE(bitmap_bh, "get_write_access");
	fatal = ext4_journal_get_write_access(handle, bitmap_bh);
	if (fatal)
		goto error_return;

	/* Ok, now we can actually update the inode bitmaps.. */
	if (!ext4_clear_bit_atomic(sb_bgl_lock(sbi, block_group),
					bit, bitmap_bh->b_data))
		ext4_error (sb, "ext4_free_inode",
			      "bit already cleared for inode %lu", ino);
	else {
		gdp = ext4_get_group_desc (sb, block_group, &bh2);

		BUFFER_TRACE(bh2, "get_write_access");
		fatal = ext4_journal_get_write_access(handle, bh2);
		if (fatal) goto error_return;

		if (gdp) {
			spin_lock(sb_bgl_lock(sbi, block_group));
			gdp->bg_free_inodes_count = cpu_to_le16(
				le16_to_cpu(gdp->bg_free_inodes_count) + 1);
			if (is_directory)
				gdp->bg_used_dirs_count = cpu_to_le16(
				  le16_to_cpu(gdp->bg_used_dirs_count) - 1);
			gdp->bg_checksum = ext4_group_desc_csum(sbi,
							block_group, gdp);
			spin_unlock(sb_bgl_lock(sbi, block_group));
			percpu_counter_inc(&sbi->s_freeinodes_counter);
			if (is_directory)
				percpu_counter_dec(&sbi->s_dirs_counter);

		}
		BUFFER_TRACE(bh2, "call ext4_journal_dirty_metadata");
		err = ext4_journal_dirty_metadata(handle, bh2);
		if (!fatal) fatal = err;
	}
	BUFFER_TRACE(bitmap_bh, "call ext4_journal_dirty_metadata");
	err = ext4_journal_dirty_metadata(handle, bitmap_bh);
	if (!fatal)
		fatal = err;
	sb->s_dirt = 1;
error_return:
	brelse(bitmap_bh);
	ext4_std_error(sb, fatal);
}

/*
 * There are two policies for allocating an inode.  If the new inode is
 * a directory, then a forward search is made for a block group with both
 * free space and a low directory-to-inode ratio; if that fails, then of
 * the groups with above-average free space, that group with the fewest
 * directories already is chosen.
 *
 * For other inodes, search forward from the parent directory\'s block
 * group to find a free inode.
 */
static int find_group_dir(struct super_block *sb, struct inode *parent,
				ext4_group_t *best_group)
{
	ext4_group_t ngroups = EXT4_SB(sb)->s_groups_count;
	unsigned int freei, avefreei;
	struct ext4_group_desc *desc, *best_desc = NULL;
	ext4_group_t group;
	int ret = -1;

	freei = percpu_counter_read_positive(&EXT4_SB(sb)->s_freeinodes_counter);
	avefreei = freei / ngroups;

	for (group = 0; group < ngroups; group++) {
		desc = ext4_get_group_desc (sb, group, NULL);
		if (!desc || !desc->bg_free_inodes_count)
			continue;
		if (le16_to_cpu(desc->bg_free_inodes_count) < avefreei)
			continue;
		if (!best_desc ||
		    (le16_to_cpu(desc->bg_free_blocks_count) >
		     le16_to_cpu(best_desc->bg_free_blocks_count))) {
			*best_group = group;
			best_desc = desc;
			ret = 0;
		}
	}
	return ret;
}

/*
 * Orlov's allocator for directories.
 *
 * We always try to spread first-level directories.
 *
 * If there are blockgroups with both free inodes and free blocks counts
 * not worse than average we return one with smallest directory count.
 * Otherwise we simply return a random group.
 *
 * For the rest rules look so:
 *
 * It's OK to put directory into a group unless
 * it has too many directories already (max_dirs) or
 * it has too few free inodes left (min_inodes) or
 * it has too few free blocks left (min_blocks) or
 * it's already running too large debt (max_debt).
 * Parent's group is prefered, if it doesn't satisfy these
 * conditions we search cyclically through the rest. If none
 * of the groups look good we just look for a group with more
 * free inodes than average (starting at parent's group).
 *
 * Debt is incremented each time we allocate a directory and decremented
 * when we allocate an inode, within 0--255.
 */

#define INODE_COST 64
#define BLOCK_COST 256

static int find_group_orlov(struct super_block *sb, struct inode *parent,
				ext4_group_t *group)
{
	ext4_group_t parent_group = EXT4_I(parent)->i_block_group;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_super_block *es = sbi->s_es;
	ext4_group_t ngroups = sbi->s_groups_count;
	int inodes_per_group = EXT4_INODES_PER_GROUP(sb);
	unsigned int freei, avefreei;
	ext4_fsblk_t freeb, avefreeb;
	ext4_fsblk_t blocks_per_dir;
	unsigned int ndirs;
	int max_debt, max_dirs, min_inodes;
	ext4_grpblk_t min_blocks;
	ext4_group_t i;
	struct ext4_group_desc *desc;

	freei = percpu_counter_read_positive(&sbi->s_freeinodes_counter);
	avefreei = freei / ngroups;
	freeb = percpu_counter_read_positive(&sbi->s_freeblocks_counter);
	avefreeb = freeb;
	do_div(avefreeb, ngroups);
	ndirs = percpu_counter_read_positive(&sbi->s_dirs_counter);

	if ((parent == sb->s_root->d_inode) ||
	    (EXT4_I(parent)->i_flags & EXT4_TOPDIR_FL)) {
		int best_ndir = inodes_per_group;
		ext4_group_t grp;
		int ret = -1;

		get_random_bytes(&grp, sizeof(grp));
		parent_group = (unsigned)grp % ngroups;
		for (i = 0; i < ngroups; i++) {
			grp = (parent_group + i) % ngroups;
			desc = ext4_get_group_desc(sb, grp, NULL);
			if (!desc || !desc->bg_free_inodes_count)
				continue;
			if (le16_to_cpu(desc->bg_used_dirs_count) >= best_ndir)
				continue;
			if (le16_to_cpu(desc->bg_free_inodes_count) < avefreei)
				continue;
			if (le16_to_cpu(desc->bg_free_blocks_count) < avefreeb)
				continue;
			*group = grp;
			ret = 0;
			best_ndir = le16_to_cpu(desc->bg_used_dirs_count);
		}
		if (ret == 0)
			return ret;
		goto fallback;
	}

	blocks_per_dir = ext4_blocks_count(es) - freeb;
	do_div(blocks_per_dir, ndirs);

	max_dirs = ndirs / ngroups + inodes_per_group / 16;
	min_inodes = avefreei - inodes_per_group / 4;
	min_blocks = avefreeb - EXT4_BLOCKS_PER_GROUP(sb) / 4;

	max_debt = EXT4_BLOCKS_PER_GROUP(sb);
	max_debt /= max_t(int, blocks_per_dir, BLOCK_COST);
	if (max_debt * INODE_COST > inodes_per_group)
		max_debt = inodes_per_group / INODE_COST;
	if (max_debt > 255)
		max_debt = 255;
	if (max_debt == 0)
		max_debt = 1;

	for (i = 0; i < ngroups; i++) {
		*group = (parent_group + i) % ngroups;
		desc = ext4_get_group_desc(sb, *group, NULL);
		if (!desc || !desc->bg_free_inodes_count)
			continue;
		if (le16_to_cpu(desc->bg_used_dirs_count) >= max_dirs)
			continue;
		if (le16_to_cpu(desc->bg_free_inodes_count) < min_inodes)
			continue;
		if (le16_to_cpu(desc->bg_free_blocks_count) < min_blocks)
			continue;
		return 0;
	}

fallback:
	for (i = 0; i < ngroups; i++) {
		*group = (parent_group + i) % ngroups;
		desc = ext4_get_group_desc(sb, *group, NULL);
		if (desc && desc->bg_free_inodes_count &&
			le16_to_cpu(desc->bg_free_inodes_count) >= avefreei)
			return 0;
	}

	if (avefreei) {
		/*
		 * The free-inodes counter is approximate, and for really small
		 * filesystems the above test can fail to find any blockgroups
		 */
		avefreei = 0;
		goto fallback;
	}

	return -1;
}

static int find_group_other(struct super_block *sb, struct inode *parent,
				ext4_group_t *group)
{
	ext4_group_t parent_group = EXT4_I(parent)->i_block_group;
	ext4_group_t ngroups = EXT4_SB(sb)->s_groups_count;
	struct ext4_group_desc *desc;
	ext4_group_t i;

	/*
	 * Try to place the inode in its parent directory
	 */
	*group = parent_group;
	desc = ext4_get_group_desc(sb, *group, NULL);
	if (desc && le16_to_cpu(desc->bg_free_inodes_count) &&
			le16_to_cpu(desc->bg_free_blocks_count))
		return 0;

	/*
	 * We're going to place this inode in a different blockgroup from its
	 * parent.  We want to cause files in a common directory to all land in
	 * the same blockgroup.  But we want files which are in a different
	 * directory which shares a blockgroup with our parent to land in a
	 * different blockgroup.
	 *
	 * So add our directory's i_ino into the starting point for the hash.
	 */
	*group = (*group + parent->i_ino) % ngroups;

	/*
	 * Use a quadratic hash to find a group with a free inode and some free
	 * blocks.
	 */
	for (i = 1; i < ngroups; i <<= 1) {
		*group += i;
		if (*group >= ngroups)
			*group -= ngroups;
		desc = ext4_get_group_desc(sb, *group, NULL);
		if (desc && le16_to_cpu(desc->bg_free_inodes_count) &&
				le16_to_cpu(desc->bg_free_blocks_count))
			return 0;
	}

	/*
	 * That failed: try linear search for a free inode, even if that group
	 * has no free blocks.
	 */
	*group = parent_group;
	for (i = 0; i < ngroups; i++) {
		if (++*group >= ngroups)
			*group = 0;
		desc = ext4_get_group_desc(sb, *group, NULL);
		if (desc && le16_to_cpu(desc->bg_free_inodes_count))
			return 0;
	}

	return -1;
}

/*
 * There are two policies for allocating an inode.  If the new inode is
 * a directory, then a forward search is made for a block group with both
 * free space and a low directory-to-inode ratio; if that fails, then of
 * the groups with above-average free space, that group with the fewest
 * directories already is chosen.
 *
 * For other inodes, search forward from the parent directory's block
 * group to find a free inode.
 */
struct inode *ext4_new_inode(handle_t *handle, struct inode * dir, int mode)
{
	struct super_block *sb;
	struct buffer_head *bitmap_bh = NULL;
	struct buffer_head *bh2;
	ext4_group_t group = 0;
	unsigned long ino = 0;
	struct inode * inode;
	struct ext4_group_desc * gdp = NULL;
	struct ext4_super_block * es;
	struct ext4_inode_info *ei;
	struct ext4_sb_info *sbi;
	int ret2, err = 0;
	struct inode *ret;
	ext4_group_t i;
	int free = 0;

	/* Cannot create files in a deleted directory */
	if (!dir || !dir->i_nlink)
		return ERR_PTR(-EPERM);

	sb = dir->i_sb;
	inode = new_inode(sb);
	if (!inode)
		return ERR_PTR(-ENOMEM);
	ei = EXT4_I(inode);

	sbi = EXT4_SB(sb);
	es = sbi->s_es;
	if (S_ISDIR(mode)) {
		if (test_opt (sb, OLDALLOC))
			ret2 = find_group_dir(sb, dir, &group);
		else
			ret2 = find_group_orlov(sb, dir, &group);
	} else
		ret2 = find_group_other(sb, dir, &group);

	err = -ENOSPC;
	if (ret2 == -1)
		goto out;

	for (i = 0; i < sbi->s_groups_count; i++) {
		err = -EIO;

		gdp = ext4_get_group_desc(sb, group, &bh2);
		if (!gdp)
			goto fail;

		brelse(bitmap_bh);
		bitmap_bh = read_inode_bitmap(sb, group);
		if (!bitmap_bh)
			goto fail;

		ino = 0;

repeat_in_this_group:
		ino = ext4_find_next_zero_bit((unsigned long *)
				bitmap_bh->b_data, EXT4_INODES_PER_GROUP(sb), ino);
		if (ino < EXT4_INODES_PER_GROUP(sb)) {

			BUFFER_TRACE(bitmap_bh, "get_write_access");
			err = ext4_journal_get_write_access(handle, bitmap_bh);
			if (err)
				goto fail;

			if (!ext4_set_bit_atomic(sb_bgl_lock(sbi, group),
						ino, bitmap_bh->b_data)) {
				/* we won it */
				BUFFER_TRACE(bitmap_bh,
					"call ext4_journal_dirty_metadata");
				err = ext4_journal_dirty_metadata(handle,
								bitmap_bh);
				if (err)
					goto fail;
				goto got;
			}
			/* we lost it */
			jbd2_journal_release_buffer(handle, bitmap_bh);

			if (++ino < EXT4_INODES_PER_GROUP(sb))
				goto repeat_in_this_group;
		}

		/*
		 * This case is possible in concurrent environment.  It is very
		 * rare.  We cannot repeat the find_group_xxx() call because
		 * that will simply return the same blockgroup, because the
		 * group descriptor metadata has not yet been updated.
		 * So we just go onto the next blockgroup.
		 */
		if (++group == sbi->s_groups_count)
			group = 0;
	}
	err = -ENOSPC;
	goto out;

got:
	ino++;
	if ((group == 0 && ino < EXT4_FIRST_INO(sb)) ||
	    ino > EXT4_INODES_PER_GROUP(sb)) {
		ext4_error(sb, __FUNCTION__,
			   "reserved inode or inode > inodes count - "
			   "block_group = %lu, inode=%lu", group,
			   ino + group * EXT4_INODES_PER_GROUP(sb));
		err = -EIO;
		goto fail;
	}

	BUFFER_TRACE(bh2, "get_write_access");
	err = ext4_journal_get_write_access(handle, bh2);
	if (err) goto fail;

	/* We may have to initialize the block bitmap if it isn't already */
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_GDT_CSUM) &&
	    gdp->bg_flags & cpu_to_le16(EXT4_BG_BLOCK_UNINIT)) {
		struct buffer_head *block_bh = read_block_bitmap(sb, group);

		BUFFER_TRACE(block_bh, "get block bitmap access");
		err = ext4_journal_get_write_access(handle, block_bh);
		if (err) {
			brelse(block_bh);
			goto fail;
		}

		free = 0;
		spin_lock(sb_bgl_lock(sbi, group));
		/* recheck and clear flag under lock if we still need to */
		if (gdp->bg_flags & cpu_to_le16(EXT4_BG_BLOCK_UNINIT)) {
			gdp->bg_flags &= cpu_to_le16(~EXT4_BG_BLOCK_UNINIT);
			free = ext4_free_blocks_after_init(sb, group, gdp);
			gdp->bg_free_blocks_count = cpu_to_le16(free);
		}
		spin_unlock(sb_bgl_lock(sbi, group));

		/* Don't need to dirty bitmap block if we didn't change it */
		if (free) {
			BUFFER_TRACE(block_bh, "dirty block bitmap");
			err = ext4_journal_dirty_metadata(handle, block_bh);
		}

		brelse(block_bh);
		if (err)
			goto fail;
	}

	spin_lock(sb_bgl_lock(sbi, group));
	/* If we didn't allocate from within the initialized part of the inode
	 * table then we need to initialize up to this inode. */
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_GDT_CSUM)) {
		if (gdp->bg_flags & cpu_to_le16(EXT4_BG_INODE_UNINIT)) {
			gdp->bg_flags &= cpu_to_le16(~EXT4_BG_INODE_UNINIT);

			/* When marking the block group with
			 * ~EXT4_BG_INODE_UNINIT we don't want to depend
			 * on the value of bg_itable_unsed even though
			 * mke2fs could have initialized the same for us.
			 * Instead we calculated the value below
			 */

			free = 0;
		} else {
			free = EXT4_INODES_PER_GROUP(sb) -
				le16_to_cpu(gdp->bg_itable_unused);
		}

		/*
		 * Check the relative inode number against the last used
		 * relative inode number in this group. if it is greater
		 * we need to  update the bg_itable_unused count
		 *
		 */
		if (ino > free)
			gdp->bg_itable_unused =
				cpu_to_le16(EXT4_INODES_PER_GROUP(sb) - ino);
	}

	gdp->bg_free_inodes_count =
		cpu_to_le16(le16_to_cpu(gdp->bg_free_inodes_count) - 1);
	if (S_ISDIR(mode)) {
		gdp->bg_used_dirs_count =
			cpu_to_le16(le16_to_cpu(gdp->bg_used_dirs_count) + 1);
	}
	gdp->bg_checksum = ext4_group_desc_csum(sbi, group, gdp);
	spin_unlock(sb_bgl_lock(sbi, group));
	BUFFER_TRACE(bh2, "call ext4_journal_dirty_metadata");
	err = ext4_journal_dirty_metadata(handle, bh2);
	if (err) goto fail;

	percpu_counter_dec(&sbi->s_freeinodes_counter);
	if (S_ISDIR(mode))
		percpu_counter_inc(&sbi->s_dirs_counter);
	sb->s_dirt = 1;

	inode->i_uid = current->fsuid;
	if (test_opt (sb, GRPID))
		inode->i_gid = dir->i_gid;
	else if (dir->i_mode & S_ISGID) {
		inode->i_gid = dir->i_gid;
		if (S_ISDIR(mode))
			mode |= S_ISGID;
	} else
		inode->i_gid = current->fsgid;
	inode->i_mode = mode;

	inode->i_ino = ino + group * EXT4_INODES_PER_GROUP(sb);
	/* This is the optimal IO size (for stat), not the fs block size */
	inode->i_blocks = 0;
	inode->i_mtime = inode->i_atime = inode->i_ctime = ei->i_crtime =
						       ext4_current_time(inode);

	memset(ei->i_data, 0, sizeof(ei->i_data));
	ei->i_dir_start_lookup = 0;
	ei->i_disksize = 0;

	ei->i_flags = EXT4_I(dir)->i_flags & ~EXT4_INDEX_FL;
	if (S_ISLNK(mode))
		ei->i_flags &= ~(EXT4_IMMUTABLE_FL|EXT4_APPEND_FL);
	/* dirsync only applies to directories */
	if (!S_ISDIR(mode))
		ei->i_flags &= ~EXT4_DIRSYNC_FL;
	ei->i_file_acl = 0;
	ei->i_dtime = 0;
	ei->i_block_alloc_info = NULL;
	ei->i_block_group = group;

	ext4_set_inode_flags(inode);
	if (IS_DIRSYNC(inode))
		handle->h_sync = 1;
	insert_inode_hash(inode);
	spin_lock(&sbi->s_next_gen_lock);
	inode->i_generation = sbi->s_next_generation++;
	spin_unlock(&sbi->s_next_gen_lock);

	ei->i_state = EXT4_STATE_NEW;

	ei->i_extra_isize = EXT4_SB(sb)->s_want_extra_isize;

	ret = inode;
	if(DQUOT_ALLOC_INODE(inode)) {
		err = -EDQUOT;
		goto fail_drop;
	}

	err = ext4_init_acl(handle, inode, dir);
	if (err)
		goto fail_free_drop;

	err = ext4_init_security(handle,inode, dir);
	if (err)
		goto fail_free_drop;

	err = ext4_mark_inode_dirty(handle, inode);
	if (err) {
		ext4_std_error(sb, err);
		goto fail_free_drop;
	}
	if (test_opt(sb, EXTENTS)) {
		EXT4_I(inode)->i_flags |= EXT4_EXTENTS_FL;
		ext4_ext_tree_init(handle, inode);
		err = ext4_update_incompat_feature(handle, sb,
						EXT4_FEATURE_INCOMPAT_EXTENTS);
		if (err)
			goto fail;
	}

	ext4_debug("allocating inode %lu\n", inode->i_ino);
	goto really_out;
fail:
	ext4_std_error(sb, err);
out:
	iput(inode);
	ret = ERR_PTR(err);
really_out:
	brelse(bitmap_bh);
	return ret;

fail_free_drop:
	DQUOT_FREE_INODE(inode);

fail_drop:
	DQUOT_DROP(inode);
	inode->i_flags |= S_NOQUOTA;
	inode->i_nlink = 0;
	iput(inode);
	brelse(bitmap_bh);
	return ERR_PTR(err);
}

/* Verify that we are loading a valid orphan from disk */
struct inode *ext4_orphan_get(struct super_block *sb, unsigned long ino)
{
	unsigned long max_ino = le32_to_cpu(EXT4_SB(sb)->s_es->s_inodes_count);
	ext4_group_t block_group;
	int bit;
	struct buffer_head *bitmap_bh = NULL;
	struct inode *inode = NULL;

	/* Error cases - e2fsck has already cleaned up for us */
	if (ino > max_ino) {
		ext4_warning(sb, __FUNCTION__,
			     "bad orphan ino %lu!  e2fsck was run?", ino);
		goto out;
	}

	block_group = (ino - 1) / EXT4_INODES_PER_GROUP(sb);
	bit = (ino - 1) % EXT4_INODES_PER_GROUP(sb);
	bitmap_bh = read_inode_bitmap(sb, block_group);
	if (!bitmap_bh) {
		ext4_warning(sb, __FUNCTION__,
			     "inode bitmap error for orphan %lu", ino);
		goto out;
	}

	/* Having the inode bit set should be a 100% indicator that this
	 * is a valid orphan (no e2fsck run on fs).  Orphans also include
	 * inodes that were being truncated, so we can't check i_nlink==0.
	 */
	if (!ext4_test_bit(bit, bitmap_bh->b_data) ||
			!(inode = iget(sb, ino)) || is_bad_inode(inode) ||
			NEXT_ORPHAN(inode) > max_ino) {
		ext4_warning(sb, __FUNCTION__,
			     "bad orphan inode %lu!  e2fsck was run?", ino);
		printk(KERN_NOTICE "ext4_test_bit(bit=%d, block=%llu) = %d\n",
		       bit, (unsigned long long)bitmap_bh->b_blocknr,
		       ext4_test_bit(bit, bitmap_bh->b_data));
		printk(KERN_NOTICE "inode=%p\n", inode);
		if (inode) {
			printk(KERN_NOTICE "is_bad_inode(inode)=%d\n",
			       is_bad_inode(inode));
			printk(KERN_NOTICE "NEXT_ORPHAN(inode)=%u\n",
			       NEXT_ORPHAN(inode));
			printk(KERN_NOTICE "max_ino=%lu\n", max_ino);
		}
		/* Avoid freeing blocks if we got a bad deleted inode */
		if (inode && inode->i_nlink == 0)
			inode->i_blocks = 0;
		iput(inode);
		inode = NULL;
	}
out:
	brelse(bitmap_bh);
	return inode;
}

unsigned long ext4_count_free_inodes (struct super_block * sb)
{
	unsigned long desc_count;
	struct ext4_group_desc *gdp;
	ext4_group_t i;
#ifdef EXT4FS_DEBUG
	struct ext4_super_block *es;
	unsigned long bitmap_count, x;
	struct buffer_head *bitmap_bh = NULL;

	es = EXT4_SB(sb)->s_es;
	desc_count = 0;
	bitmap_count = 0;
	gdp = NULL;
	for (i = 0; i < EXT4_SB(sb)->s_groups_count; i++) {
		gdp = ext4_get_group_desc (sb, i, NULL);
		if (!gdp)
			continue;
		desc_count += le16_to_cpu(gdp->bg_free_inodes_count);
		brelse(bitmap_bh);
		bitmap_bh = read_inode_bitmap(sb, i);
		if (!bitmap_bh)
			continue;

		x = ext4_count_free(bitmap_bh, EXT4_INODES_PER_GROUP(sb) / 8);
		printk(KERN_DEBUG "group %lu: stored = %d, counted = %lu\n",
			i, le16_to_cpu(gdp->bg_free_inodes_count), x);
		bitmap_count += x;
	}
	brelse(bitmap_bh);
	printk("ext4_count_free_inodes: stored = %u, computed = %lu, %lu\n",
		le32_to_cpu(es->s_free_inodes_count), desc_count, bitmap_count);
	return desc_count;
#else
	desc_count = 0;
	for (i = 0; i < EXT4_SB(sb)->s_groups_count; i++) {
		gdp = ext4_get_group_desc (sb, i, NULL);
		if (!gdp)
			continue;
		desc_count += le16_to_cpu(gdp->bg_free_inodes_count);
		cond_resched();
	}
	return desc_count;
#endif
}

/* Called at mount-time, super-block is locked */
unsigned long ext4_count_dirs (struct super_block * sb)
{
	unsigned long count = 0;
	ext4_group_t i;

	for (i = 0; i < EXT4_SB(sb)->s_groups_count; i++) {
		struct ext4_group_desc *gdp = ext4_get_group_desc (sb, i, NULL);
		if (!gdp)
			continue;
		count += le16_to_cpu(gdp->bg_used_dirs_count);
	}
	return count;
}

