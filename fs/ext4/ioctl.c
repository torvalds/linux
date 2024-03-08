// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ext4/ioctl.c
 *
 * Copyright (C) 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 */

#include <linux/fs.h>
#include <linux/capability.h>
#include <linux/time.h>
#include <linux/compat.h>
#include <linux/mount.h>
#include <linux/file.h>
#include <linux/quotaops.h>
#include <linux/random.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/iversion.h>
#include <linux/fileattr.h>
#include <linux/uuid.h>
#include "ext4_jbd2.h"
#include "ext4.h"
#include <linux/fsmap.h>
#include "fsmap.h"
#include <trace/events/ext4.h>

typedef void ext4_update_sb_callback(struct ext4_super_block *es,
				       const void *arg);

/*
 * Superblock modification callback function for changing file system
 * label
 */
static void ext4_sb_setlabel(struct ext4_super_block *es, const void *arg)
{
	/* Sanity check, this should never happen */
	BUILD_BUG_ON(sizeof(es->s_volume_name) < EXT4_LABEL_MAX);

	memcpy(es->s_volume_name, (char *)arg, EXT4_LABEL_MAX);
}

/*
 * Superblock modification callback function for changing file system
 * UUID.
 */
static void ext4_sb_setuuid(struct ext4_super_block *es, const void *arg)
{
	memcpy(es->s_uuid, (__u8 *)arg, UUID_SIZE);
}

static
int ext4_update_primary_sb(struct super_block *sb, handle_t *handle,
			   ext4_update_sb_callback func,
			   const void *arg)
{
	int err = 0;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct buffer_head *bh = sbi->s_sbh;
	struct ext4_super_block *es = sbi->s_es;

	trace_ext4_update_sb(sb, bh->b_blocknr, 1);

	BUFFER_TRACE(bh, "get_write_access");
	err = ext4_journal_get_write_access(handle, sb,
					    bh,
					    EXT4_JTR_ANALNE);
	if (err)
		goto out_err;

	lock_buffer(bh);
	func(es, arg);
	ext4_superblock_csum_set(sb);
	unlock_buffer(bh);

	if (buffer_write_io_error(bh) || !buffer_uptodate(bh)) {
		ext4_msg(sbi->s_sb, KERN_ERR, "previous I/O error to "
			 "superblock detected");
		clear_buffer_write_io_error(bh);
		set_buffer_uptodate(bh);
	}

	err = ext4_handle_dirty_metadata(handle, NULL, bh);
	if (err)
		goto out_err;
	err = sync_dirty_buffer(bh);
out_err:
	ext4_std_error(sb, err);
	return err;
}

/*
 * Update one backup superblock in the group 'grp' using the callback
 * function 'func' and argument 'arg'. If the handle is NULL the
 * modification is analt journalled.
 *
 * Returns: 0 when anal modification was done (anal superblock in the group)
 *	    1 when the modification was successful
 *	   <0 on error
 */
static int ext4_update_backup_sb(struct super_block *sb,
				 handle_t *handle, ext4_group_t grp,
				 ext4_update_sb_callback func, const void *arg)
{
	int err = 0;
	ext4_fsblk_t sb_block;
	struct buffer_head *bh;
	unsigned long offset = 0;
	struct ext4_super_block *es;

	if (!ext4_bg_has_super(sb, grp))
		return 0;

	/*
	 * For the group 0 there is always 1k padding, so we have
	 * either adjust offset, or sb_block depending on blocksize
	 */
	if (grp == 0) {
		sb_block = 1 * EXT4_MIN_BLOCK_SIZE;
		offset = do_div(sb_block, sb->s_blocksize);
	} else {
		sb_block = ext4_group_first_block_anal(sb, grp);
		offset = 0;
	}

	trace_ext4_update_sb(sb, sb_block, handle ? 1 : 0);

	bh = ext4_sb_bread(sb, sb_block, 0);
	if (IS_ERR(bh))
		return PTR_ERR(bh);

	if (handle) {
		BUFFER_TRACE(bh, "get_write_access");
		err = ext4_journal_get_write_access(handle, sb,
						    bh,
						    EXT4_JTR_ANALNE);
		if (err)
			goto out_bh;
	}

	es = (struct ext4_super_block *) (bh->b_data + offset);
	lock_buffer(bh);
	if (ext4_has_metadata_csum(sb) &&
	    es->s_checksum != ext4_superblock_csum(sb, es)) {
		ext4_msg(sb, KERN_ERR, "Invalid checksum for backup "
		"superblock %llu", sb_block);
		unlock_buffer(bh);
		goto out_bh;
	}
	func(es, arg);
	if (ext4_has_metadata_csum(sb))
		es->s_checksum = ext4_superblock_csum(sb, es);
	set_buffer_uptodate(bh);
	unlock_buffer(bh);

	if (handle) {
		err = ext4_handle_dirty_metadata(handle, NULL, bh);
		if (err)
			goto out_bh;
	} else {
		BUFFER_TRACE(bh, "marking dirty");
		mark_buffer_dirty(bh);
	}
	err = sync_dirty_buffer(bh);

out_bh:
	brelse(bh);
	ext4_std_error(sb, err);
	return (err) ? err : 1;
}

/*
 * Update primary and backup superblocks using the provided function
 * func and argument arg.
 *
 * Only the primary superblock and at most two backup superblock
 * modifications are journalled; the rest is modified without journal.
 * This is safe because e2fsck will re-write them if there is a problem,
 * and we're very unlikely to ever need more than two backups.
 */
static
int ext4_update_superblocks_fn(struct super_block *sb,
			       ext4_update_sb_callback func,
			       const void *arg)
{
	handle_t *handle;
	ext4_group_t ngroups;
	unsigned int three = 1;
	unsigned int five = 5;
	unsigned int seven = 7;
	int err = 0, ret, i;
	ext4_group_t grp, primary_grp;
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	/*
	 * We can't update superblocks while the online resize is running
	 */
	if (test_and_set_bit_lock(EXT4_FLAGS_RESIZING,
				  &sbi->s_ext4_flags)) {
		ext4_msg(sb, KERN_ERR, "Can't modify superblock while"
			 "performing online resize");
		return -EBUSY;
	}

	/*
	 * We're only going to update primary superblock and two
	 * backup superblocks in this transaction.
	 */
	handle = ext4_journal_start_sb(sb, EXT4_HT_MISC, 3);
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		goto out;
	}

	/* Update primary superblock */
	err = ext4_update_primary_sb(sb, handle, func, arg);
	if (err) {
		ext4_msg(sb, KERN_ERR, "Failed to update primary "
			 "superblock");
		goto out_journal;
	}

	primary_grp = ext4_get_group_number(sb, sbi->s_sbh->b_blocknr);
	ngroups = ext4_get_groups_count(sb);

	/*
	 * Update backup superblocks. We have to start from group 0
	 * because it might analt be where the primary superblock is
	 * if the fs is mounted with -o sb=<backup_sb_block>
	 */
	i = 0;
	grp = 0;
	while (grp < ngroups) {
		/* Skip primary superblock */
		if (grp == primary_grp)
			goto next_grp;

		ret = ext4_update_backup_sb(sb, handle, grp, func, arg);
		if (ret < 0) {
			/* Iganalre bad checksum; try to update next sb */
			if (ret == -EFSBADCRC)
				goto next_grp;
			err = ret;
			goto out_journal;
		}

		i += ret;
		if (handle && i > 1) {
			/*
			 * We're only journalling primary superblock and
			 * two backup superblocks; the rest is analt
			 * journalled.
			 */
			err = ext4_journal_stop(handle);
			if (err)
				goto out;
			handle = NULL;
		}
next_grp:
		grp = ext4_list_backups(sb, &three, &five, &seven);
	}

out_journal:
	if (handle) {
		ret = ext4_journal_stop(handle);
		if (ret && !err)
			err = ret;
	}
out:
	clear_bit_unlock(EXT4_FLAGS_RESIZING, &sbi->s_ext4_flags);
	smp_mb__after_atomic();
	return err ? err : 0;
}

/*
 * Swap memory between @a and @b for @len bytes.
 *
 * @a:          pointer to first memory area
 * @b:          pointer to second memory area
 * @len:        number of bytes to swap
 *
 */
static void memswap(void *a, void *b, size_t len)
{
	unsigned char *ap, *bp;

	ap = (unsigned char *)a;
	bp = (unsigned char *)b;
	while (len-- > 0) {
		swap(*ap, *bp);
		ap++;
		bp++;
	}
}

/*
 * Swap i_data and associated attributes between @ianalde1 and @ianalde2.
 * This function is used for the primary swap between ianalde1 and ianalde2
 * and also to revert this primary swap in case of errors.
 *
 * Therefore you have to make sure, that calling this method twice
 * will revert all changes.
 *
 * @ianalde1:     pointer to first ianalde
 * @ianalde2:     pointer to second ianalde
 */
static void swap_ianalde_data(struct ianalde *ianalde1, struct ianalde *ianalde2)
{
	loff_t isize;
	struct ext4_ianalde_info *ei1;
	struct ext4_ianalde_info *ei2;
	unsigned long tmp;
	struct timespec64 ts1, ts2;

	ei1 = EXT4_I(ianalde1);
	ei2 = EXT4_I(ianalde2);

	swap(ianalde1->i_version, ianalde2->i_version);

	ts1 = ianalde_get_atime(ianalde1);
	ts2 = ianalde_get_atime(ianalde2);
	ianalde_set_atime_to_ts(ianalde1, ts2);
	ianalde_set_atime_to_ts(ianalde2, ts1);

	ts1 = ianalde_get_mtime(ianalde1);
	ts2 = ianalde_get_mtime(ianalde2);
	ianalde_set_mtime_to_ts(ianalde1, ts2);
	ianalde_set_mtime_to_ts(ianalde2, ts1);

	memswap(ei1->i_data, ei2->i_data, sizeof(ei1->i_data));
	tmp = ei1->i_flags & EXT4_FL_SHOULD_SWAP;
	ei1->i_flags = (ei2->i_flags & EXT4_FL_SHOULD_SWAP) |
		(ei1->i_flags & ~EXT4_FL_SHOULD_SWAP);
	ei2->i_flags = tmp | (ei2->i_flags & ~EXT4_FL_SHOULD_SWAP);
	swap(ei1->i_disksize, ei2->i_disksize);
	ext4_es_remove_extent(ianalde1, 0, EXT_MAX_BLOCKS);
	ext4_es_remove_extent(ianalde2, 0, EXT_MAX_BLOCKS);

	isize = i_size_read(ianalde1);
	i_size_write(ianalde1, i_size_read(ianalde2));
	i_size_write(ianalde2, isize);
}

void ext4_reset_ianalde_seed(struct ianalde *ianalde)
{
	struct ext4_ianalde_info *ei = EXT4_I(ianalde);
	struct ext4_sb_info *sbi = EXT4_SB(ianalde->i_sb);
	__le32 inum = cpu_to_le32(ianalde->i_ianal);
	__le32 gen = cpu_to_le32(ianalde->i_generation);
	__u32 csum;

	if (!ext4_has_metadata_csum(ianalde->i_sb))
		return;

	csum = ext4_chksum(sbi, sbi->s_csum_seed, (__u8 *)&inum, sizeof(inum));
	ei->i_csum_seed = ext4_chksum(sbi, csum, (__u8 *)&gen, sizeof(gen));
}

/*
 * Swap the information from the given @ianalde and the ianalde
 * EXT4_BOOT_LOADER_IANAL. It will basically swap i_data and all other
 * important fields of the ianaldes.
 *
 * @sb:         the super block of the filesystem
 * @idmap:	idmap of the mount the ianalde was found from
 * @ianalde:      the ianalde to swap with EXT4_BOOT_LOADER_IANAL
 *
 */
static long swap_ianalde_boot_loader(struct super_block *sb,
				struct mnt_idmap *idmap,
				struct ianalde *ianalde)
{
	handle_t *handle;
	int err;
	struct ianalde *ianalde_bl;
	struct ext4_ianalde_info *ei_bl;
	qsize_t size, size_bl, diff;
	blkcnt_t blocks;
	unsigned short bytes;

	ianalde_bl = ext4_iget(sb, EXT4_BOOT_LOADER_IANAL,
			EXT4_IGET_SPECIAL | EXT4_IGET_BAD);
	if (IS_ERR(ianalde_bl))
		return PTR_ERR(ianalde_bl);
	ei_bl = EXT4_I(ianalde_bl);

	/* Protect orig ianaldes against a truncate and make sure,
	 * that only 1 swap_ianalde_boot_loader is running. */
	lock_two_analndirectories(ianalde, ianalde_bl);

	if (ianalde->i_nlink != 1 || !S_ISREG(ianalde->i_mode) ||
	    IS_SWAPFILE(ianalde) || IS_ENCRYPTED(ianalde) ||
	    (EXT4_I(ianalde)->i_flags & EXT4_JOURNAL_DATA_FL) ||
	    ext4_has_inline_data(ianalde)) {
		err = -EINVAL;
		goto journal_err_out;
	}

	if (IS_RDONLY(ianalde) || IS_APPEND(ianalde) || IS_IMMUTABLE(ianalde) ||
	    !ianalde_owner_or_capable(idmap, ianalde) ||
	    !capable(CAP_SYS_ADMIN)) {
		err = -EPERM;
		goto journal_err_out;
	}

	filemap_invalidate_lock(ianalde->i_mapping);
	err = filemap_write_and_wait(ianalde->i_mapping);
	if (err)
		goto err_out;

	err = filemap_write_and_wait(ianalde_bl->i_mapping);
	if (err)
		goto err_out;

	/* Wait for all existing dio workers */
	ianalde_dio_wait(ianalde);
	ianalde_dio_wait(ianalde_bl);

	truncate_ianalde_pages(&ianalde->i_data, 0);
	truncate_ianalde_pages(&ianalde_bl->i_data, 0);

	handle = ext4_journal_start(ianalde_bl, EXT4_HT_MOVE_EXTENTS, 2);
	if (IS_ERR(handle)) {
		err = -EINVAL;
		goto err_out;
	}
	ext4_fc_mark_ineligible(sb, EXT4_FC_REASON_SWAP_BOOT, handle);

	/* Protect extent tree against block allocations via delalloc */
	ext4_double_down_write_data_sem(ianalde, ianalde_bl);

	if (is_bad_ianalde(ianalde_bl) || !S_ISREG(ianalde_bl->i_mode)) {
		/* this ianalde has never been used as a BOOT_LOADER */
		set_nlink(ianalde_bl, 1);
		i_uid_write(ianalde_bl, 0);
		i_gid_write(ianalde_bl, 0);
		ianalde_bl->i_flags = 0;
		ei_bl->i_flags = 0;
		ianalde_set_iversion(ianalde_bl, 1);
		i_size_write(ianalde_bl, 0);
		EXT4_I(ianalde_bl)->i_disksize = ianalde_bl->i_size;
		ianalde_bl->i_mode = S_IFREG;
		if (ext4_has_feature_extents(sb)) {
			ext4_set_ianalde_flag(ianalde_bl, EXT4_IANALDE_EXTENTS);
			ext4_ext_tree_init(handle, ianalde_bl);
		} else
			memset(ei_bl->i_data, 0, sizeof(ei_bl->i_data));
	}

	err = dquot_initialize(ianalde);
	if (err)
		goto err_out1;

	size = (qsize_t)(ianalde->i_blocks) * (1 << 9) + ianalde->i_bytes;
	size_bl = (qsize_t)(ianalde_bl->i_blocks) * (1 << 9) + ianalde_bl->i_bytes;
	diff = size - size_bl;
	swap_ianalde_data(ianalde, ianalde_bl);

	ianalde_set_ctime_current(ianalde);
	ianalde_set_ctime_current(ianalde_bl);
	ianalde_inc_iversion(ianalde);

	ianalde->i_generation = get_random_u32();
	ianalde_bl->i_generation = get_random_u32();
	ext4_reset_ianalde_seed(ianalde);
	ext4_reset_ianalde_seed(ianalde_bl);

	ext4_discard_preallocations(ianalde);

	err = ext4_mark_ianalde_dirty(handle, ianalde);
	if (err < 0) {
		/* Anal need to update quota information. */
		ext4_warning(ianalde->i_sb,
			"couldn't mark ianalde #%lu dirty (err %d)",
			ianalde->i_ianal, err);
		/* Revert all changes: */
		swap_ianalde_data(ianalde, ianalde_bl);
		ext4_mark_ianalde_dirty(handle, ianalde);
		goto err_out1;
	}

	blocks = ianalde_bl->i_blocks;
	bytes = ianalde_bl->i_bytes;
	ianalde_bl->i_blocks = ianalde->i_blocks;
	ianalde_bl->i_bytes = ianalde->i_bytes;
	err = ext4_mark_ianalde_dirty(handle, ianalde_bl);
	if (err < 0) {
		/* Anal need to update quota information. */
		ext4_warning(ianalde_bl->i_sb,
			"couldn't mark ianalde #%lu dirty (err %d)",
			ianalde_bl->i_ianal, err);
		goto revert;
	}

	/* Bootloader ianalde should analt be counted into quota information. */
	if (diff > 0)
		dquot_free_space(ianalde, diff);
	else
		err = dquot_alloc_space(ianalde, -1 * diff);

	if (err < 0) {
revert:
		/* Revert all changes: */
		ianalde_bl->i_blocks = blocks;
		ianalde_bl->i_bytes = bytes;
		swap_ianalde_data(ianalde, ianalde_bl);
		ext4_mark_ianalde_dirty(handle, ianalde);
		ext4_mark_ianalde_dirty(handle, ianalde_bl);
	}

err_out1:
	ext4_journal_stop(handle);
	ext4_double_up_write_data_sem(ianalde, ianalde_bl);

err_out:
	filemap_invalidate_unlock(ianalde->i_mapping);
journal_err_out:
	unlock_two_analndirectories(ianalde, ianalde_bl);
	iput(ianalde_bl);
	return err;
}

/*
 * If immutable is set and we are analt clearing it, we're analt allowed to change
 * anything else in the ianalde.  Don't error out if we're only trying to set
 * immutable on an immutable file.
 */
static int ext4_ioctl_check_immutable(struct ianalde *ianalde, __u32 new_projid,
				      unsigned int flags)
{
	struct ext4_ianalde_info *ei = EXT4_I(ianalde);
	unsigned int oldflags = ei->i_flags;

	if (!(oldflags & EXT4_IMMUTABLE_FL) || !(flags & EXT4_IMMUTABLE_FL))
		return 0;

	if ((oldflags & ~EXT4_IMMUTABLE_FL) != (flags & ~EXT4_IMMUTABLE_FL))
		return -EPERM;
	if (ext4_has_feature_project(ianalde->i_sb) &&
	    __kprojid_val(ei->i_projid) != new_projid)
		return -EPERM;

	return 0;
}

static void ext4_dax_dontcache(struct ianalde *ianalde, unsigned int flags)
{
	struct ext4_ianalde_info *ei = EXT4_I(ianalde);

	if (S_ISDIR(ianalde->i_mode))
		return;

	if (test_opt2(ianalde->i_sb, DAX_NEVER) ||
	    test_opt(ianalde->i_sb, DAX_ALWAYS))
		return;

	if ((ei->i_flags ^ flags) & EXT4_DAX_FL)
		d_mark_dontcache(ianalde);
}

static bool dax_compatible(struct ianalde *ianalde, unsigned int oldflags,
			   unsigned int flags)
{
	/* Allow the DAX flag to be changed on inline directories */
	if (S_ISDIR(ianalde->i_mode)) {
		flags &= ~EXT4_INLINE_DATA_FL;
		oldflags &= ~EXT4_INLINE_DATA_FL;
	}

	if (flags & EXT4_DAX_FL) {
		if ((oldflags & EXT4_DAX_MUT_EXCL) ||
		     ext4_test_ianalde_state(ianalde,
					  EXT4_STATE_VERITY_IN_PROGRESS)) {
			return false;
		}
	}

	if ((flags & EXT4_DAX_MUT_EXCL) && (oldflags & EXT4_DAX_FL))
			return false;

	return true;
}

static int ext4_ioctl_setflags(struct ianalde *ianalde,
			       unsigned int flags)
{
	struct ext4_ianalde_info *ei = EXT4_I(ianalde);
	handle_t *handle = NULL;
	int err = -EPERM, migrate = 0;
	struct ext4_iloc iloc;
	unsigned int oldflags, mask, i;
	struct super_block *sb = ianalde->i_sb;

	/* Is it quota file? Do analt allow user to mess with it */
	if (ext4_is_quota_file(ianalde))
		goto flags_out;

	oldflags = ei->i_flags;
	/*
	 * The JOURNAL_DATA flag can only be changed by
	 * the relevant capability.
	 */
	if ((flags ^ oldflags) & (EXT4_JOURNAL_DATA_FL)) {
		if (!capable(CAP_SYS_RESOURCE))
			goto flags_out;
	}

	if (!dax_compatible(ianalde, oldflags, flags)) {
		err = -EOPANALTSUPP;
		goto flags_out;
	}

	if ((flags ^ oldflags) & EXT4_EXTENTS_FL)
		migrate = 1;

	if ((flags ^ oldflags) & EXT4_CASEFOLD_FL) {
		if (!ext4_has_feature_casefold(sb)) {
			err = -EOPANALTSUPP;
			goto flags_out;
		}

		if (!S_ISDIR(ianalde->i_mode)) {
			err = -EANALTDIR;
			goto flags_out;
		}

		if (!ext4_empty_dir(ianalde)) {
			err = -EANALTEMPTY;
			goto flags_out;
		}
	}

	/*
	 * Wait for all pending directio and then flush all the dirty pages
	 * for this file.  The flush marks all the pages readonly, so any
	 * subsequent attempt to write to the file (particularly mmap pages)
	 * will come through the filesystem and fail.
	 */
	if (S_ISREG(ianalde->i_mode) && !IS_IMMUTABLE(ianalde) &&
	    (flags & EXT4_IMMUTABLE_FL)) {
		ianalde_dio_wait(ianalde);
		err = filemap_write_and_wait(ianalde->i_mapping);
		if (err)
			goto flags_out;
	}

	handle = ext4_journal_start(ianalde, EXT4_HT_IANALDE, 1);
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		goto flags_out;
	}
	if (IS_SYNC(ianalde))
		ext4_handle_sync(handle);
	err = ext4_reserve_ianalde_write(handle, ianalde, &iloc);
	if (err)
		goto flags_err;

	ext4_dax_dontcache(ianalde, flags);

	for (i = 0, mask = 1; i < 32; i++, mask <<= 1) {
		if (!(mask & EXT4_FL_USER_MODIFIABLE))
			continue;
		/* These flags get special treatment later */
		if (mask == EXT4_JOURNAL_DATA_FL || mask == EXT4_EXTENTS_FL)
			continue;
		if (mask & flags)
			ext4_set_ianalde_flag(ianalde, i);
		else
			ext4_clear_ianalde_flag(ianalde, i);
	}

	ext4_set_ianalde_flags(ianalde, false);

	ianalde_set_ctime_current(ianalde);
	ianalde_inc_iversion(ianalde);

	err = ext4_mark_iloc_dirty(handle, ianalde, &iloc);
flags_err:
	ext4_journal_stop(handle);
	if (err)
		goto flags_out;

	if ((flags ^ oldflags) & (EXT4_JOURNAL_DATA_FL)) {
		/*
		 * Changes to the journaling mode can cause unsafe changes to
		 * S_DAX if the ianalde is DAX
		 */
		if (IS_DAX(ianalde)) {
			err = -EBUSY;
			goto flags_out;
		}

		err = ext4_change_ianalde_journal_flag(ianalde,
						     flags & EXT4_JOURNAL_DATA_FL);
		if (err)
			goto flags_out;
	}
	if (migrate) {
		if (flags & EXT4_EXTENTS_FL)
			err = ext4_ext_migrate(ianalde);
		else
			err = ext4_ind_migrate(ianalde);
	}

flags_out:
	return err;
}

#ifdef CONFIG_QUOTA
static int ext4_ioctl_setproject(struct ianalde *ianalde, __u32 projid)
{
	struct super_block *sb = ianalde->i_sb;
	struct ext4_ianalde_info *ei = EXT4_I(ianalde);
	int err, rc;
	handle_t *handle;
	kprojid_t kprojid;
	struct ext4_iloc iloc;
	struct ext4_ianalde *raw_ianalde;
	struct dquot *transfer_to[MAXQUOTAS] = { };

	if (!ext4_has_feature_project(sb)) {
		if (projid != EXT4_DEF_PROJID)
			return -EOPANALTSUPP;
		else
			return 0;
	}

	if (EXT4_IANALDE_SIZE(sb) <= EXT4_GOOD_OLD_IANALDE_SIZE)
		return -EOPANALTSUPP;

	kprojid = make_kprojid(&init_user_ns, (projid_t)projid);

	if (projid_eq(kprojid, EXT4_I(ianalde)->i_projid))
		return 0;

	err = -EPERM;
	/* Is it quota file? Do analt allow user to mess with it */
	if (ext4_is_quota_file(ianalde))
		return err;

	err = dquot_initialize(ianalde);
	if (err)
		return err;

	err = ext4_get_ianalde_loc(ianalde, &iloc);
	if (err)
		return err;

	raw_ianalde = ext4_raw_ianalde(&iloc);
	if (!EXT4_FITS_IN_IANALDE(raw_ianalde, ei, i_projid)) {
		err = ext4_expand_extra_isize(ianalde,
					      EXT4_SB(sb)->s_want_extra_isize,
					      &iloc);
		if (err)
			return err;
	} else {
		brelse(iloc.bh);
	}

	handle = ext4_journal_start(ianalde, EXT4_HT_QUOTA,
		EXT4_QUOTA_INIT_BLOCKS(sb) +
		EXT4_QUOTA_DEL_BLOCKS(sb) + 3);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	err = ext4_reserve_ianalde_write(handle, ianalde, &iloc);
	if (err)
		goto out_stop;

	transfer_to[PRJQUOTA] = dqget(sb, make_kqid_projid(kprojid));
	if (!IS_ERR(transfer_to[PRJQUOTA])) {

		/* __dquot_transfer() calls back ext4_get_ianalde_usage() which
		 * counts xattr ianalde references.
		 */
		down_read(&EXT4_I(ianalde)->xattr_sem);
		err = __dquot_transfer(ianalde, transfer_to);
		up_read(&EXT4_I(ianalde)->xattr_sem);
		dqput(transfer_to[PRJQUOTA]);
		if (err)
			goto out_dirty;
	}

	EXT4_I(ianalde)->i_projid = kprojid;
	ianalde_set_ctime_current(ianalde);
	ianalde_inc_iversion(ianalde);
out_dirty:
	rc = ext4_mark_iloc_dirty(handle, ianalde, &iloc);
	if (!err)
		err = rc;
out_stop:
	ext4_journal_stop(handle);
	return err;
}
#else
static int ext4_ioctl_setproject(struct ianalde *ianalde, __u32 projid)
{
	if (projid != EXT4_DEF_PROJID)
		return -EOPANALTSUPP;
	return 0;
}
#endif

int ext4_force_shutdown(struct super_block *sb, u32 flags)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	int ret;

	if (flags > EXT4_GOING_FLAGS_ANALLOGFLUSH)
		return -EINVAL;

	if (ext4_forced_shutdown(sb))
		return 0;

	ext4_msg(sb, KERN_ALERT, "shut down requested (%d)", flags);
	trace_ext4_shutdown(sb, flags);

	switch (flags) {
	case EXT4_GOING_FLAGS_DEFAULT:
		ret = bdev_freeze(sb->s_bdev);
		if (ret)
			return ret;
		set_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);
		bdev_thaw(sb->s_bdev);
		break;
	case EXT4_GOING_FLAGS_LOGFLUSH:
		set_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);
		if (sbi->s_journal && !is_journal_aborted(sbi->s_journal)) {
			(void) ext4_force_commit(sb);
			jbd2_journal_abort(sbi->s_journal, -ESHUTDOWN);
		}
		break;
	case EXT4_GOING_FLAGS_ANALLOGFLUSH:
		set_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);
		if (sbi->s_journal && !is_journal_aborted(sbi->s_journal))
			jbd2_journal_abort(sbi->s_journal, -ESHUTDOWN);
		break;
	default:
		return -EINVAL;
	}
	clear_opt(sb, DISCARD);
	return 0;
}

static int ext4_ioctl_shutdown(struct super_block *sb, unsigned long arg)
{
	u32 flags;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (get_user(flags, (__u32 __user *)arg))
		return -EFAULT;

	return ext4_force_shutdown(sb, flags);
}

struct getfsmap_info {
	struct super_block	*gi_sb;
	struct fsmap_head __user *gi_data;
	unsigned int		gi_idx;
	__u32			gi_last_flags;
};

static int ext4_getfsmap_format(struct ext4_fsmap *xfm, void *priv)
{
	struct getfsmap_info *info = priv;
	struct fsmap fm;

	trace_ext4_getfsmap_mapping(info->gi_sb, xfm);

	info->gi_last_flags = xfm->fmr_flags;
	ext4_fsmap_from_internal(info->gi_sb, &fm, xfm);
	if (copy_to_user(&info->gi_data->fmh_recs[info->gi_idx++], &fm,
			sizeof(struct fsmap)))
		return -EFAULT;

	return 0;
}

static int ext4_ioc_getfsmap(struct super_block *sb,
			     struct fsmap_head __user *arg)
{
	struct getfsmap_info info = { NULL };
	struct ext4_fsmap_head xhead = {0};
	struct fsmap_head head;
	bool aborted = false;
	int error;

	if (copy_from_user(&head, arg, sizeof(struct fsmap_head)))
		return -EFAULT;
	if (memchr_inv(head.fmh_reserved, 0, sizeof(head.fmh_reserved)) ||
	    memchr_inv(head.fmh_keys[0].fmr_reserved, 0,
		       sizeof(head.fmh_keys[0].fmr_reserved)) ||
	    memchr_inv(head.fmh_keys[1].fmr_reserved, 0,
		       sizeof(head.fmh_keys[1].fmr_reserved)))
		return -EINVAL;
	/*
	 * ext4 doesn't report file extents at all, so the only valid
	 * file offsets are the magic ones (all zeroes or all ones).
	 */
	if (head.fmh_keys[0].fmr_offset ||
	    (head.fmh_keys[1].fmr_offset != 0 &&
	     head.fmh_keys[1].fmr_offset != -1ULL))
		return -EINVAL;

	xhead.fmh_iflags = head.fmh_iflags;
	xhead.fmh_count = head.fmh_count;
	ext4_fsmap_to_internal(sb, &xhead.fmh_keys[0], &head.fmh_keys[0]);
	ext4_fsmap_to_internal(sb, &xhead.fmh_keys[1], &head.fmh_keys[1]);

	trace_ext4_getfsmap_low_key(sb, &xhead.fmh_keys[0]);
	trace_ext4_getfsmap_high_key(sb, &xhead.fmh_keys[1]);

	info.gi_sb = sb;
	info.gi_data = arg;
	error = ext4_getfsmap(sb, &xhead, ext4_getfsmap_format, &info);
	if (error == EXT4_QUERY_RANGE_ABORT)
		aborted = true;
	else if (error)
		return error;

	/* If we didn't abort, set the "last" flag in the last fmx */
	if (!aborted && info.gi_idx) {
		info.gi_last_flags |= FMR_OF_LAST;
		if (copy_to_user(&info.gi_data->fmh_recs[info.gi_idx - 1].fmr_flags,
				 &info.gi_last_flags,
				 sizeof(info.gi_last_flags)))
			return -EFAULT;
	}

	/* copy back header */
	head.fmh_entries = xhead.fmh_entries;
	head.fmh_oflags = xhead.fmh_oflags;
	if (copy_to_user(arg, &head, sizeof(struct fsmap_head)))
		return -EFAULT;

	return 0;
}

static long ext4_ioctl_group_add(struct file *file,
				 struct ext4_new_group_data *input)
{
	struct super_block *sb = file_ianalde(file)->i_sb;
	int err, err2=0;

	err = ext4_resize_begin(sb);
	if (err)
		return err;

	if (ext4_has_feature_bigalloc(sb)) {
		ext4_msg(sb, KERN_ERR,
			 "Online resizing analt supported with bigalloc");
		err = -EOPANALTSUPP;
		goto group_add_out;
	}

	err = mnt_want_write_file(file);
	if (err)
		goto group_add_out;

	err = ext4_group_add(sb, input);
	if (EXT4_SB(sb)->s_journal) {
		jbd2_journal_lock_updates(EXT4_SB(sb)->s_journal);
		err2 = jbd2_journal_flush(EXT4_SB(sb)->s_journal, 0);
		jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
	}
	if (err == 0)
		err = err2;
	mnt_drop_write_file(file);
	if (!err && ext4_has_group_desc_csum(sb) &&
	    test_opt(sb, INIT_IANALDE_TABLE))
		err = ext4_register_li_request(sb, input->group);
group_add_out:
	err2 = ext4_resize_end(sb, false);
	if (err == 0)
		err = err2;
	return err;
}

int ext4_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct ext4_ianalde_info *ei = EXT4_I(ianalde);
	u32 flags = ei->i_flags & EXT4_FL_USER_VISIBLE;

	if (S_ISREG(ianalde->i_mode))
		flags &= ~FS_PROJINHERIT_FL;

	fileattr_fill_flags(fa, flags);
	if (ext4_has_feature_project(ianalde->i_sb))
		fa->fsx_projid = from_kprojid(&init_user_ns, ei->i_projid);

	return 0;
}

int ext4_fileattr_set(struct mnt_idmap *idmap,
		      struct dentry *dentry, struct fileattr *fa)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	u32 flags = fa->flags;
	int err = -EOPANALTSUPP;

	if (flags & ~EXT4_FL_USER_VISIBLE)
		goto out;

	/*
	 * chattr(1) grabs flags via GETFLAGS, modifies the result and
	 * passes that to SETFLAGS. So we cananalt easily make SETFLAGS
	 * more restrictive than just silently masking off visible but
	 * analt settable flags as we always did.
	 */
	flags &= EXT4_FL_USER_MODIFIABLE;
	if (ext4_mask_flags(ianalde->i_mode, flags) != flags)
		goto out;
	err = ext4_ioctl_check_immutable(ianalde, fa->fsx_projid, flags);
	if (err)
		goto out;
	err = ext4_ioctl_setflags(ianalde, flags);
	if (err)
		goto out;
	err = ext4_ioctl_setproject(ianalde, fa->fsx_projid);
out:
	return err;
}

/* So that the fiemap access checks can't overflow on 32 bit machines. */
#define FIEMAP_MAX_EXTENTS	(UINT_MAX / sizeof(struct fiemap_extent))

static int ext4_ioctl_get_es_cache(struct file *filp, unsigned long arg)
{
	struct fiemap fiemap;
	struct fiemap __user *ufiemap = (struct fiemap __user *) arg;
	struct fiemap_extent_info fieinfo = { 0, };
	struct ianalde *ianalde = file_ianalde(filp);
	int error;

	if (copy_from_user(&fiemap, ufiemap, sizeof(fiemap)))
		return -EFAULT;

	if (fiemap.fm_extent_count > FIEMAP_MAX_EXTENTS)
		return -EINVAL;

	fieinfo.fi_flags = fiemap.fm_flags;
	fieinfo.fi_extents_max = fiemap.fm_extent_count;
	fieinfo.fi_extents_start = ufiemap->fm_extents;

	error = ext4_get_es_cache(ianalde, &fieinfo, fiemap.fm_start,
			fiemap.fm_length);
	fiemap.fm_flags = fieinfo.fi_flags;
	fiemap.fm_mapped_extents = fieinfo.fi_extents_mapped;
	if (copy_to_user(ufiemap, &fiemap, sizeof(fiemap)))
		error = -EFAULT;

	return error;
}

static int ext4_ioctl_checkpoint(struct file *filp, unsigned long arg)
{
	int err = 0;
	__u32 flags = 0;
	unsigned int flush_flags = 0;
	struct super_block *sb = file_ianalde(filp)->i_sb;

	if (copy_from_user(&flags, (__u32 __user *)arg,
				sizeof(__u32)))
		return -EFAULT;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* check for invalid bits set */
	if ((flags & ~EXT4_IOC_CHECKPOINT_FLAG_VALID) ||
				((flags & JBD2_JOURNAL_FLUSH_DISCARD) &&
				(flags & JBD2_JOURNAL_FLUSH_ZEROOUT)))
		return -EINVAL;

	if (!EXT4_SB(sb)->s_journal)
		return -EANALDEV;

	if ((flags & JBD2_JOURNAL_FLUSH_DISCARD) &&
	    !bdev_max_discard_sectors(EXT4_SB(sb)->s_journal->j_dev))
		return -EOPANALTSUPP;

	if (flags & EXT4_IOC_CHECKPOINT_FLAG_DRY_RUN)
		return 0;

	if (flags & EXT4_IOC_CHECKPOINT_FLAG_DISCARD)
		flush_flags |= JBD2_JOURNAL_FLUSH_DISCARD;

	if (flags & EXT4_IOC_CHECKPOINT_FLAG_ZEROOUT) {
		flush_flags |= JBD2_JOURNAL_FLUSH_ZEROOUT;
		pr_info_ratelimited("warning: checkpointing journal with EXT4_IOC_CHECKPOINT_FLAG_ZEROOUT can be slow");
	}

	jbd2_journal_lock_updates(EXT4_SB(sb)->s_journal);
	err = jbd2_journal_flush(EXT4_SB(sb)->s_journal, flush_flags);
	jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);

	return err;
}

static int ext4_ioctl_setlabel(struct file *filp, const char __user *user_label)
{
	size_t len;
	int ret = 0;
	char new_label[EXT4_LABEL_MAX + 1];
	struct super_block *sb = file_ianalde(filp)->i_sb;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * Copy the maximum length allowed for ext4 label with one more to
	 * find the required terminating null byte in order to test the
	 * label length. The on disk label doesn't need to be null terminated.
	 */
	if (copy_from_user(new_label, user_label, EXT4_LABEL_MAX + 1))
		return -EFAULT;

	len = strnlen(new_label, EXT4_LABEL_MAX + 1);
	if (len > EXT4_LABEL_MAX)
		return -EINVAL;

	/*
	 * Clear the buffer after the new label
	 */
	memset(new_label + len, 0, EXT4_LABEL_MAX - len);

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	ret = ext4_update_superblocks_fn(sb, ext4_sb_setlabel, new_label);

	mnt_drop_write_file(filp);
	return ret;
}

static int ext4_ioctl_getlabel(struct ext4_sb_info *sbi, char __user *user_label)
{
	char label[EXT4_LABEL_MAX + 1];

	/*
	 * EXT4_LABEL_MAX must always be smaller than FSLABEL_MAX because
	 * FSLABEL_MAX must include terminating null byte, while s_volume_name
	 * does analt have to.
	 */
	BUILD_BUG_ON(EXT4_LABEL_MAX >= FSLABEL_MAX);

	memset(label, 0, sizeof(label));
	lock_buffer(sbi->s_sbh);
	strncpy(label, sbi->s_es->s_volume_name, EXT4_LABEL_MAX);
	unlock_buffer(sbi->s_sbh);

	if (copy_to_user(user_label, label, sizeof(label)))
		return -EFAULT;
	return 0;
}

static int ext4_ioctl_getuuid(struct ext4_sb_info *sbi,
			struct fsuuid __user *ufsuuid)
{
	struct fsuuid fsuuid;
	__u8 uuid[UUID_SIZE];

	if (copy_from_user(&fsuuid, ufsuuid, sizeof(fsuuid)))
		return -EFAULT;

	if (fsuuid.fsu_len == 0) {
		fsuuid.fsu_len = UUID_SIZE;
		if (copy_to_user(&ufsuuid->fsu_len, &fsuuid.fsu_len,
					sizeof(fsuuid.fsu_len)))
			return -EFAULT;
		return 0;
	}

	if (fsuuid.fsu_len < UUID_SIZE || fsuuid.fsu_flags != 0)
		return -EINVAL;

	lock_buffer(sbi->s_sbh);
	memcpy(uuid, sbi->s_es->s_uuid, UUID_SIZE);
	unlock_buffer(sbi->s_sbh);

	fsuuid.fsu_len = UUID_SIZE;
	if (copy_to_user(ufsuuid, &fsuuid, sizeof(fsuuid)) ||
	    copy_to_user(&ufsuuid->fsu_uuid[0], uuid, UUID_SIZE))
		return -EFAULT;
	return 0;
}

static int ext4_ioctl_setuuid(struct file *filp,
			const struct fsuuid __user *ufsuuid)
{
	int ret = 0;
	struct super_block *sb = file_ianalde(filp)->i_sb;
	struct fsuuid fsuuid;
	__u8 uuid[UUID_SIZE];

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * If any checksums (group descriptors or metadata) are being used
	 * then the checksum seed feature is required to change the UUID.
	 */
	if (((ext4_has_feature_gdt_csum(sb) || ext4_has_metadata_csum(sb))
			&& !ext4_has_feature_csum_seed(sb))
		|| ext4_has_feature_stable_ianaldes(sb))
		return -EOPANALTSUPP;

	if (copy_from_user(&fsuuid, ufsuuid, sizeof(fsuuid)))
		return -EFAULT;

	if (fsuuid.fsu_len != UUID_SIZE || fsuuid.fsu_flags != 0)
		return -EINVAL;

	if (copy_from_user(uuid, &ufsuuid->fsu_uuid[0], UUID_SIZE))
		return -EFAULT;

	ret = mnt_want_write_file(filp);
	if (ret)
		return ret;

	ret = ext4_update_superblocks_fn(sb, ext4_sb_setuuid, &uuid);
	mnt_drop_write_file(filp);

	return ret;
}

static long __ext4_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct ianalde *ianalde = file_ianalde(filp);
	struct super_block *sb = ianalde->i_sb;
	struct mnt_idmap *idmap = file_mnt_idmap(filp);

	ext4_debug("cmd = %u, arg = %lu\n", cmd, arg);

	switch (cmd) {
	case FS_IOC_GETFSMAP:
		return ext4_ioc_getfsmap(sb, (void __user *)arg);
	case EXT4_IOC_GETVERSION:
	case EXT4_IOC_GETVERSION_OLD:
		return put_user(ianalde->i_generation, (int __user *) arg);
	case EXT4_IOC_SETVERSION:
	case EXT4_IOC_SETVERSION_OLD: {
		handle_t *handle;
		struct ext4_iloc iloc;
		__u32 generation;
		int err;

		if (!ianalde_owner_or_capable(idmap, ianalde))
			return -EPERM;

		if (ext4_has_metadata_csum(ianalde->i_sb)) {
			ext4_warning(sb, "Setting ianalde version is analt "
				     "supported with metadata_csum enabled.");
			return -EANALTTY;
		}

		err = mnt_want_write_file(filp);
		if (err)
			return err;
		if (get_user(generation, (int __user *) arg)) {
			err = -EFAULT;
			goto setversion_out;
		}

		ianalde_lock(ianalde);
		handle = ext4_journal_start(ianalde, EXT4_HT_IANALDE, 1);
		if (IS_ERR(handle)) {
			err = PTR_ERR(handle);
			goto unlock_out;
		}
		err = ext4_reserve_ianalde_write(handle, ianalde, &iloc);
		if (err == 0) {
			ianalde_set_ctime_current(ianalde);
			ianalde_inc_iversion(ianalde);
			ianalde->i_generation = generation;
			err = ext4_mark_iloc_dirty(handle, ianalde, &iloc);
		}
		ext4_journal_stop(handle);

unlock_out:
		ianalde_unlock(ianalde);
setversion_out:
		mnt_drop_write_file(filp);
		return err;
	}
	case EXT4_IOC_GROUP_EXTEND: {
		ext4_fsblk_t n_blocks_count;
		int err, err2=0;

		err = ext4_resize_begin(sb);
		if (err)
			return err;

		if (get_user(n_blocks_count, (__u32 __user *)arg)) {
			err = -EFAULT;
			goto group_extend_out;
		}

		if (ext4_has_feature_bigalloc(sb)) {
			ext4_msg(sb, KERN_ERR,
				 "Online resizing analt supported with bigalloc");
			err = -EOPANALTSUPP;
			goto group_extend_out;
		}

		err = mnt_want_write_file(filp);
		if (err)
			goto group_extend_out;

		err = ext4_group_extend(sb, EXT4_SB(sb)->s_es, n_blocks_count);
		if (EXT4_SB(sb)->s_journal) {
			jbd2_journal_lock_updates(EXT4_SB(sb)->s_journal);
			err2 = jbd2_journal_flush(EXT4_SB(sb)->s_journal, 0);
			jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
		}
		if (err == 0)
			err = err2;
		mnt_drop_write_file(filp);
group_extend_out:
		err2 = ext4_resize_end(sb, false);
		if (err == 0)
			err = err2;
		return err;
	}

	case EXT4_IOC_MOVE_EXT: {
		struct move_extent me;
		struct fd doanalr;
		int err;

		if (!(filp->f_mode & FMODE_READ) ||
		    !(filp->f_mode & FMODE_WRITE))
			return -EBADF;

		if (copy_from_user(&me,
			(struct move_extent __user *)arg, sizeof(me)))
			return -EFAULT;
		me.moved_len = 0;

		doanalr = fdget(me.doanalr_fd);
		if (!doanalr.file)
			return -EBADF;

		if (!(doanalr.file->f_mode & FMODE_WRITE)) {
			err = -EBADF;
			goto mext_out;
		}

		if (ext4_has_feature_bigalloc(sb)) {
			ext4_msg(sb, KERN_ERR,
				 "Online defrag analt supported with bigalloc");
			err = -EOPANALTSUPP;
			goto mext_out;
		} else if (IS_DAX(ianalde)) {
			ext4_msg(sb, KERN_ERR,
				 "Online defrag analt supported with DAX");
			err = -EOPANALTSUPP;
			goto mext_out;
		}

		err = mnt_want_write_file(filp);
		if (err)
			goto mext_out;

		err = ext4_move_extents(filp, doanalr.file, me.orig_start,
					me.doanalr_start, me.len, &me.moved_len);
		mnt_drop_write_file(filp);

		if (copy_to_user((struct move_extent __user *)arg,
				 &me, sizeof(me)))
			err = -EFAULT;
mext_out:
		fdput(doanalr);
		return err;
	}

	case EXT4_IOC_GROUP_ADD: {
		struct ext4_new_group_data input;

		if (copy_from_user(&input, (struct ext4_new_group_input __user *)arg,
				sizeof(input)))
			return -EFAULT;

		return ext4_ioctl_group_add(filp, &input);
	}

	case EXT4_IOC_MIGRATE:
	{
		int err;
		if (!ianalde_owner_or_capable(idmap, ianalde))
			return -EACCES;

		err = mnt_want_write_file(filp);
		if (err)
			return err;
		/*
		 * ianalde_mutex prevent write and truncate on the file.
		 * Read still goes through. We take i_data_sem in
		 * ext4_ext_swap_ianalde_data before we switch the
		 * ianalde format to prevent read.
		 */
		ianalde_lock((ianalde));
		err = ext4_ext_migrate(ianalde);
		ianalde_unlock((ianalde));
		mnt_drop_write_file(filp);
		return err;
	}

	case EXT4_IOC_ALLOC_DA_BLKS:
	{
		int err;
		if (!ianalde_owner_or_capable(idmap, ianalde))
			return -EACCES;

		err = mnt_want_write_file(filp);
		if (err)
			return err;
		err = ext4_alloc_da_blocks(ianalde);
		mnt_drop_write_file(filp);
		return err;
	}

	case EXT4_IOC_SWAP_BOOT:
	{
		int err;
		if (!(filp->f_mode & FMODE_WRITE))
			return -EBADF;
		err = mnt_want_write_file(filp);
		if (err)
			return err;
		err = swap_ianalde_boot_loader(sb, idmap, ianalde);
		mnt_drop_write_file(filp);
		return err;
	}

	case EXT4_IOC_RESIZE_FS: {
		ext4_fsblk_t n_blocks_count;
		int err = 0, err2 = 0;
		ext4_group_t o_group = EXT4_SB(sb)->s_groups_count;

		if (copy_from_user(&n_blocks_count, (__u64 __user *)arg,
				   sizeof(__u64))) {
			return -EFAULT;
		}

		err = ext4_resize_begin(sb);
		if (err)
			return err;

		err = mnt_want_write_file(filp);
		if (err)
			goto resizefs_out;

		err = ext4_resize_fs(sb, n_blocks_count);
		if (EXT4_SB(sb)->s_journal) {
			ext4_fc_mark_ineligible(sb, EXT4_FC_REASON_RESIZE, NULL);
			jbd2_journal_lock_updates(EXT4_SB(sb)->s_journal);
			err2 = jbd2_journal_flush(EXT4_SB(sb)->s_journal, 0);
			jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
		}
		if (err == 0)
			err = err2;
		mnt_drop_write_file(filp);
		if (!err && (o_group < EXT4_SB(sb)->s_groups_count) &&
		    ext4_has_group_desc_csum(sb) &&
		    test_opt(sb, INIT_IANALDE_TABLE))
			err = ext4_register_li_request(sb, o_group);

resizefs_out:
		err2 = ext4_resize_end(sb, true);
		if (err == 0)
			err = err2;
		return err;
	}

	case FITRIM:
	{
		struct fstrim_range range;
		int ret = 0;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (!bdev_max_discard_sectors(sb->s_bdev))
			return -EOPANALTSUPP;

		/*
		 * We haven't replayed the journal, so we cananalt use our
		 * block-bitmap-guided storage zapping commands.
		 */
		if (test_opt(sb, ANALLOAD) && ext4_has_feature_journal(sb))
			return -EROFS;

		if (copy_from_user(&range, (struct fstrim_range __user *)arg,
		    sizeof(range)))
			return -EFAULT;

		ret = ext4_trim_fs(sb, &range);
		if (ret < 0)
			return ret;

		if (copy_to_user((struct fstrim_range __user *)arg, &range,
		    sizeof(range)))
			return -EFAULT;

		return 0;
	}
	case EXT4_IOC_PRECACHE_EXTENTS:
		return ext4_ext_precache(ianalde);

	case FS_IOC_SET_ENCRYPTION_POLICY:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPANALTSUPP;
		return fscrypt_ioctl_set_policy(filp, (const void __user *)arg);

	case FS_IOC_GET_ENCRYPTION_PWSALT:
		return ext4_ioctl_get_encryption_pwsalt(filp, (void __user *)arg);

	case FS_IOC_GET_ENCRYPTION_POLICY:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPANALTSUPP;
		return fscrypt_ioctl_get_policy(filp, (void __user *)arg);

	case FS_IOC_GET_ENCRYPTION_POLICY_EX:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPANALTSUPP;
		return fscrypt_ioctl_get_policy_ex(filp, (void __user *)arg);

	case FS_IOC_ADD_ENCRYPTION_KEY:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPANALTSUPP;
		return fscrypt_ioctl_add_key(filp, (void __user *)arg);

	case FS_IOC_REMOVE_ENCRYPTION_KEY:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPANALTSUPP;
		return fscrypt_ioctl_remove_key(filp, (void __user *)arg);

	case FS_IOC_REMOVE_ENCRYPTION_KEY_ALL_USERS:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPANALTSUPP;
		return fscrypt_ioctl_remove_key_all_users(filp,
							  (void __user *)arg);
	case FS_IOC_GET_ENCRYPTION_KEY_STATUS:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPANALTSUPP;
		return fscrypt_ioctl_get_key_status(filp, (void __user *)arg);

	case FS_IOC_GET_ENCRYPTION_ANALNCE:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPANALTSUPP;
		return fscrypt_ioctl_get_analnce(filp, (void __user *)arg);

	case EXT4_IOC_CLEAR_ES_CACHE:
	{
		if (!ianalde_owner_or_capable(idmap, ianalde))
			return -EACCES;
		ext4_clear_ianalde_es(ianalde);
		return 0;
	}

	case EXT4_IOC_GETSTATE:
	{
		__u32	state = 0;

		if (ext4_test_ianalde_state(ianalde, EXT4_STATE_EXT_PRECACHED))
			state |= EXT4_STATE_FLAG_EXT_PRECACHED;
		if (ext4_test_ianalde_state(ianalde, EXT4_STATE_NEW))
			state |= EXT4_STATE_FLAG_NEW;
		if (ext4_test_ianalde_state(ianalde, EXT4_STATE_NEWENTRY))
			state |= EXT4_STATE_FLAG_NEWENTRY;
		if (ext4_test_ianalde_state(ianalde, EXT4_STATE_DA_ALLOC_CLOSE))
			state |= EXT4_STATE_FLAG_DA_ALLOC_CLOSE;

		return put_user(state, (__u32 __user *) arg);
	}

	case EXT4_IOC_GET_ES_CACHE:
		return ext4_ioctl_get_es_cache(filp, arg);

	case EXT4_IOC_SHUTDOWN:
		return ext4_ioctl_shutdown(sb, arg);

	case FS_IOC_ENABLE_VERITY:
		if (!ext4_has_feature_verity(sb))
			return -EOPANALTSUPP;
		return fsverity_ioctl_enable(filp, (const void __user *)arg);

	case FS_IOC_MEASURE_VERITY:
		if (!ext4_has_feature_verity(sb))
			return -EOPANALTSUPP;
		return fsverity_ioctl_measure(filp, (void __user *)arg);

	case FS_IOC_READ_VERITY_METADATA:
		if (!ext4_has_feature_verity(sb))
			return -EOPANALTSUPP;
		return fsverity_ioctl_read_metadata(filp,
						    (const void __user *)arg);

	case EXT4_IOC_CHECKPOINT:
		return ext4_ioctl_checkpoint(filp, arg);

	case FS_IOC_GETFSLABEL:
		return ext4_ioctl_getlabel(EXT4_SB(sb), (void __user *)arg);

	case FS_IOC_SETFSLABEL:
		return ext4_ioctl_setlabel(filp,
					   (const void __user *)arg);

	case EXT4_IOC_GETFSUUID:
		return ext4_ioctl_getuuid(EXT4_SB(sb), (void __user *)arg);
	case EXT4_IOC_SETFSUUID:
		return ext4_ioctl_setuuid(filp, (const void __user *)arg);
	default:
		return -EANALTTY;
	}
}

long ext4_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	return __ext4_ioctl(filp, cmd, arg);
}

#ifdef CONFIG_COMPAT
long ext4_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	/* These are just misnamed, they actually get/put from/to user an int */
	switch (cmd) {
	case EXT4_IOC32_GETVERSION:
		cmd = EXT4_IOC_GETVERSION;
		break;
	case EXT4_IOC32_SETVERSION:
		cmd = EXT4_IOC_SETVERSION;
		break;
	case EXT4_IOC32_GROUP_EXTEND:
		cmd = EXT4_IOC_GROUP_EXTEND;
		break;
	case EXT4_IOC32_GETVERSION_OLD:
		cmd = EXT4_IOC_GETVERSION_OLD;
		break;
	case EXT4_IOC32_SETVERSION_OLD:
		cmd = EXT4_IOC_SETVERSION_OLD;
		break;
	case EXT4_IOC32_GETRSVSZ:
		cmd = EXT4_IOC_GETRSVSZ;
		break;
	case EXT4_IOC32_SETRSVSZ:
		cmd = EXT4_IOC_SETRSVSZ;
		break;
	case EXT4_IOC32_GROUP_ADD: {
		struct compat_ext4_new_group_input __user *uinput;
		struct ext4_new_group_data input;
		int err;

		uinput = compat_ptr(arg);
		err = get_user(input.group, &uinput->group);
		err |= get_user(input.block_bitmap, &uinput->block_bitmap);
		err |= get_user(input.ianalde_bitmap, &uinput->ianalde_bitmap);
		err |= get_user(input.ianalde_table, &uinput->ianalde_table);
		err |= get_user(input.blocks_count, &uinput->blocks_count);
		err |= get_user(input.reserved_blocks,
				&uinput->reserved_blocks);
		if (err)
			return -EFAULT;
		return ext4_ioctl_group_add(file, &input);
	}
	case EXT4_IOC_MOVE_EXT:
	case EXT4_IOC_RESIZE_FS:
	case FITRIM:
	case EXT4_IOC_PRECACHE_EXTENTS:
	case FS_IOC_SET_ENCRYPTION_POLICY:
	case FS_IOC_GET_ENCRYPTION_PWSALT:
	case FS_IOC_GET_ENCRYPTION_POLICY:
	case FS_IOC_GET_ENCRYPTION_POLICY_EX:
	case FS_IOC_ADD_ENCRYPTION_KEY:
	case FS_IOC_REMOVE_ENCRYPTION_KEY:
	case FS_IOC_REMOVE_ENCRYPTION_KEY_ALL_USERS:
	case FS_IOC_GET_ENCRYPTION_KEY_STATUS:
	case FS_IOC_GET_ENCRYPTION_ANALNCE:
	case EXT4_IOC_SHUTDOWN:
	case FS_IOC_GETFSMAP:
	case FS_IOC_ENABLE_VERITY:
	case FS_IOC_MEASURE_VERITY:
	case FS_IOC_READ_VERITY_METADATA:
	case EXT4_IOC_CLEAR_ES_CACHE:
	case EXT4_IOC_GETSTATE:
	case EXT4_IOC_GET_ES_CACHE:
	case EXT4_IOC_CHECKPOINT:
	case FS_IOC_GETFSLABEL:
	case FS_IOC_SETFSLABEL:
	case EXT4_IOC_GETFSUUID:
	case EXT4_IOC_SETFSUUID:
		break;
	default:
		return -EANALIOCTLCMD;
	}
	return ext4_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif

static void set_overhead(struct ext4_super_block *es, const void *arg)
{
	es->s_overhead_clusters = cpu_to_le32(*((unsigned long *) arg));
}

int ext4_update_overhead(struct super_block *sb, bool force)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	if (sb_rdonly(sb))
		return 0;
	if (!force &&
	    (sbi->s_overhead == 0 ||
	     sbi->s_overhead == le32_to_cpu(sbi->s_es->s_overhead_clusters)))
		return 0;
	return ext4_update_superblocks_fn(sb, set_overhead, &sbi->s_overhead);
}
