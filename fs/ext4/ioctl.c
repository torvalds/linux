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
#include <linux/uuid.h>
#include <linux/uaccess.h>
#include <linux/delay.h>
#include <linux/iversion.h>
#include "ext4_jbd2.h"
#include "ext4.h"
#include <linux/fsmap.h>
#include "fsmap.h"
#include <trace/events/ext4.h>

/**
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

/**
 * Swap i_data and associated attributes between @iyesde1 and @iyesde2.
 * This function is used for the primary swap between iyesde1 and iyesde2
 * and also to revert this primary swap in case of errors.
 *
 * Therefore you have to make sure, that calling this method twice
 * will revert all changes.
 *
 * @iyesde1:     pointer to first iyesde
 * @iyesde2:     pointer to second iyesde
 */
static void swap_iyesde_data(struct iyesde *iyesde1, struct iyesde *iyesde2)
{
	loff_t isize;
	struct ext4_iyesde_info *ei1;
	struct ext4_iyesde_info *ei2;
	unsigned long tmp;

	ei1 = EXT4_I(iyesde1);
	ei2 = EXT4_I(iyesde2);

	swap(iyesde1->i_version, iyesde2->i_version);
	swap(iyesde1->i_atime, iyesde2->i_atime);
	swap(iyesde1->i_mtime, iyesde2->i_mtime);

	memswap(ei1->i_data, ei2->i_data, sizeof(ei1->i_data));
	tmp = ei1->i_flags & EXT4_FL_SHOULD_SWAP;
	ei1->i_flags = (ei2->i_flags & EXT4_FL_SHOULD_SWAP) |
		(ei1->i_flags & ~EXT4_FL_SHOULD_SWAP);
	ei2->i_flags = tmp | (ei2->i_flags & ~EXT4_FL_SHOULD_SWAP);
	swap(ei1->i_disksize, ei2->i_disksize);
	ext4_es_remove_extent(iyesde1, 0, EXT_MAX_BLOCKS);
	ext4_es_remove_extent(iyesde2, 0, EXT_MAX_BLOCKS);

	isize = i_size_read(iyesde1);
	i_size_write(iyesde1, i_size_read(iyesde2));
	i_size_write(iyesde2, isize);
}

static void reset_iyesde_seed(struct iyesde *iyesde)
{
	struct ext4_iyesde_info *ei = EXT4_I(iyesde);
	struct ext4_sb_info *sbi = EXT4_SB(iyesde->i_sb);
	__le32 inum = cpu_to_le32(iyesde->i_iyes);
	__le32 gen = cpu_to_le32(iyesde->i_generation);
	__u32 csum;

	if (!ext4_has_metadata_csum(iyesde->i_sb))
		return;

	csum = ext4_chksum(sbi, sbi->s_csum_seed, (__u8 *)&inum, sizeof(inum));
	ei->i_csum_seed = ext4_chksum(sbi, csum, (__u8 *)&gen, sizeof(gen));
}

/**
 * Swap the information from the given @iyesde and the iyesde
 * EXT4_BOOT_LOADER_INO. It will basically swap i_data and all other
 * important fields of the iyesdes.
 *
 * @sb:         the super block of the filesystem
 * @iyesde:      the iyesde to swap with EXT4_BOOT_LOADER_INO
 *
 */
static long swap_iyesde_boot_loader(struct super_block *sb,
				struct iyesde *iyesde)
{
	handle_t *handle;
	int err;
	struct iyesde *iyesde_bl;
	struct ext4_iyesde_info *ei_bl;
	qsize_t size, size_bl, diff;
	blkcnt_t blocks;
	unsigned short bytes;

	iyesde_bl = ext4_iget(sb, EXT4_BOOT_LOADER_INO, EXT4_IGET_SPECIAL);
	if (IS_ERR(iyesde_bl))
		return PTR_ERR(iyesde_bl);
	ei_bl = EXT4_I(iyesde_bl);

	/* Protect orig iyesdes against a truncate and make sure,
	 * that only 1 swap_iyesde_boot_loader is running. */
	lock_two_yesndirectories(iyesde, iyesde_bl);

	if (iyesde->i_nlink != 1 || !S_ISREG(iyesde->i_mode) ||
	    IS_SWAPFILE(iyesde) || IS_ENCRYPTED(iyesde) ||
	    (EXT4_I(iyesde)->i_flags & EXT4_JOURNAL_DATA_FL) ||
	    ext4_has_inline_data(iyesde)) {
		err = -EINVAL;
		goto journal_err_out;
	}

	if (IS_RDONLY(iyesde) || IS_APPEND(iyesde) || IS_IMMUTABLE(iyesde) ||
	    !iyesde_owner_or_capable(iyesde) || !capable(CAP_SYS_ADMIN)) {
		err = -EPERM;
		goto journal_err_out;
	}

	down_write(&EXT4_I(iyesde)->i_mmap_sem);
	err = filemap_write_and_wait(iyesde->i_mapping);
	if (err)
		goto err_out;

	err = filemap_write_and_wait(iyesde_bl->i_mapping);
	if (err)
		goto err_out;

	/* Wait for all existing dio workers */
	iyesde_dio_wait(iyesde);
	iyesde_dio_wait(iyesde_bl);

	truncate_iyesde_pages(&iyesde->i_data, 0);
	truncate_iyesde_pages(&iyesde_bl->i_data, 0);

	handle = ext4_journal_start(iyesde_bl, EXT4_HT_MOVE_EXTENTS, 2);
	if (IS_ERR(handle)) {
		err = -EINVAL;
		goto err_out;
	}

	/* Protect extent tree against block allocations via delalloc */
	ext4_double_down_write_data_sem(iyesde, iyesde_bl);

	if (iyesde_bl->i_nlink == 0) {
		/* this iyesde has never been used as a BOOT_LOADER */
		set_nlink(iyesde_bl, 1);
		i_uid_write(iyesde_bl, 0);
		i_gid_write(iyesde_bl, 0);
		iyesde_bl->i_flags = 0;
		ei_bl->i_flags = 0;
		iyesde_set_iversion(iyesde_bl, 1);
		i_size_write(iyesde_bl, 0);
		iyesde_bl->i_mode = S_IFREG;
		if (ext4_has_feature_extents(sb)) {
			ext4_set_iyesde_flag(iyesde_bl, EXT4_INODE_EXTENTS);
			ext4_ext_tree_init(handle, iyesde_bl);
		} else
			memset(ei_bl->i_data, 0, sizeof(ei_bl->i_data));
	}

	err = dquot_initialize(iyesde);
	if (err)
		goto err_out1;

	size = (qsize_t)(iyesde->i_blocks) * (1 << 9) + iyesde->i_bytes;
	size_bl = (qsize_t)(iyesde_bl->i_blocks) * (1 << 9) + iyesde_bl->i_bytes;
	diff = size - size_bl;
	swap_iyesde_data(iyesde, iyesde_bl);

	iyesde->i_ctime = iyesde_bl->i_ctime = current_time(iyesde);

	iyesde->i_generation = prandom_u32();
	iyesde_bl->i_generation = prandom_u32();
	reset_iyesde_seed(iyesde);
	reset_iyesde_seed(iyesde_bl);

	ext4_discard_preallocations(iyesde);

	err = ext4_mark_iyesde_dirty(handle, iyesde);
	if (err < 0) {
		/* No need to update quota information. */
		ext4_warning(iyesde->i_sb,
			"couldn't mark iyesde #%lu dirty (err %d)",
			iyesde->i_iyes, err);
		/* Revert all changes: */
		swap_iyesde_data(iyesde, iyesde_bl);
		ext4_mark_iyesde_dirty(handle, iyesde);
		goto err_out1;
	}

	blocks = iyesde_bl->i_blocks;
	bytes = iyesde_bl->i_bytes;
	iyesde_bl->i_blocks = iyesde->i_blocks;
	iyesde_bl->i_bytes = iyesde->i_bytes;
	err = ext4_mark_iyesde_dirty(handle, iyesde_bl);
	if (err < 0) {
		/* No need to update quota information. */
		ext4_warning(iyesde_bl->i_sb,
			"couldn't mark iyesde #%lu dirty (err %d)",
			iyesde_bl->i_iyes, err);
		goto revert;
	}

	/* Bootloader iyesde should yest be counted into quota information. */
	if (diff > 0)
		dquot_free_space(iyesde, diff);
	else
		err = dquot_alloc_space(iyesde, -1 * diff);

	if (err < 0) {
revert:
		/* Revert all changes: */
		iyesde_bl->i_blocks = blocks;
		iyesde_bl->i_bytes = bytes;
		swap_iyesde_data(iyesde, iyesde_bl);
		ext4_mark_iyesde_dirty(handle, iyesde);
		ext4_mark_iyesde_dirty(handle, iyesde_bl);
	}

err_out1:
	ext4_journal_stop(handle);
	ext4_double_up_write_data_sem(iyesde, iyesde_bl);

err_out:
	up_write(&EXT4_I(iyesde)->i_mmap_sem);
journal_err_out:
	unlock_two_yesndirectories(iyesde, iyesde_bl);
	iput(iyesde_bl);
	return err;
}

#ifdef CONFIG_FS_ENCRYPTION
static int uuid_is_zero(__u8 u[16])
{
	int	i;

	for (i = 0; i < 16; i++)
		if (u[i])
			return 0;
	return 1;
}
#endif

/*
 * If immutable is set and we are yest clearing it, we're yest allowed to change
 * anything else in the iyesde.  Don't error out if we're only trying to set
 * immutable on an immutable file.
 */
static int ext4_ioctl_check_immutable(struct iyesde *iyesde, __u32 new_projid,
				      unsigned int flags)
{
	struct ext4_iyesde_info *ei = EXT4_I(iyesde);
	unsigned int oldflags = ei->i_flags;

	if (!(oldflags & EXT4_IMMUTABLE_FL) || !(flags & EXT4_IMMUTABLE_FL))
		return 0;

	if ((oldflags & ~EXT4_IMMUTABLE_FL) != (flags & ~EXT4_IMMUTABLE_FL))
		return -EPERM;
	if (ext4_has_feature_project(iyesde->i_sb) &&
	    __kprojid_val(ei->i_projid) != new_projid)
		return -EPERM;

	return 0;
}

static int ext4_ioctl_setflags(struct iyesde *iyesde,
			       unsigned int flags)
{
	struct ext4_iyesde_info *ei = EXT4_I(iyesde);
	handle_t *handle = NULL;
	int err = -EPERM, migrate = 0;
	struct ext4_iloc iloc;
	unsigned int oldflags, mask, i;
	unsigned int jflag;
	struct super_block *sb = iyesde->i_sb;

	/* Is it quota file? Do yest allow user to mess with it */
	if (ext4_is_quota_file(iyesde))
		goto flags_out;

	oldflags = ei->i_flags;

	/* The JOURNAL_DATA flag is modifiable only by root */
	jflag = flags & EXT4_JOURNAL_DATA_FL;

	err = vfs_ioc_setflags_prepare(iyesde, oldflags, flags);
	if (err)
		goto flags_out;

	/*
	 * The JOURNAL_DATA flag can only be changed by
	 * the relevant capability.
	 */
	if ((jflag ^ oldflags) & (EXT4_JOURNAL_DATA_FL)) {
		if (!capable(CAP_SYS_RESOURCE))
			goto flags_out;
	}
	if ((flags ^ oldflags) & EXT4_EXTENTS_FL)
		migrate = 1;

	if (flags & EXT4_EOFBLOCKS_FL) {
		/* we don't support adding EOFBLOCKS flag */
		if (!(oldflags & EXT4_EOFBLOCKS_FL)) {
			err = -EOPNOTSUPP;
			goto flags_out;
		}
	} else if (oldflags & EXT4_EOFBLOCKS_FL) {
		err = ext4_truncate(iyesde);
		if (err)
			goto flags_out;
	}

	if ((flags ^ oldflags) & EXT4_CASEFOLD_FL) {
		if (!ext4_has_feature_casefold(sb)) {
			err = -EOPNOTSUPP;
			goto flags_out;
		}

		if (!S_ISDIR(iyesde->i_mode)) {
			err = -ENOTDIR;
			goto flags_out;
		}

		if (!ext4_empty_dir(iyesde)) {
			err = -ENOTEMPTY;
			goto flags_out;
		}
	}

	/*
	 * Wait for all pending directio and then flush all the dirty pages
	 * for this file.  The flush marks all the pages readonly, so any
	 * subsequent attempt to write to the file (particularly mmap pages)
	 * will come through the filesystem and fail.
	 */
	if (S_ISREG(iyesde->i_mode) && !IS_IMMUTABLE(iyesde) &&
	    (flags & EXT4_IMMUTABLE_FL)) {
		iyesde_dio_wait(iyesde);
		err = filemap_write_and_wait(iyesde->i_mapping);
		if (err)
			goto flags_out;
	}

	handle = ext4_journal_start(iyesde, EXT4_HT_INODE, 1);
	if (IS_ERR(handle)) {
		err = PTR_ERR(handle);
		goto flags_out;
	}
	if (IS_SYNC(iyesde))
		ext4_handle_sync(handle);
	err = ext4_reserve_iyesde_write(handle, iyesde, &iloc);
	if (err)
		goto flags_err;

	for (i = 0, mask = 1; i < 32; i++, mask <<= 1) {
		if (!(mask & EXT4_FL_USER_MODIFIABLE))
			continue;
		/* These flags get special treatment later */
		if (mask == EXT4_JOURNAL_DATA_FL || mask == EXT4_EXTENTS_FL)
			continue;
		if (mask & flags)
			ext4_set_iyesde_flag(iyesde, i);
		else
			ext4_clear_iyesde_flag(iyesde, i);
	}

	ext4_set_iyesde_flags(iyesde);
	iyesde->i_ctime = current_time(iyesde);

	err = ext4_mark_iloc_dirty(handle, iyesde, &iloc);
flags_err:
	ext4_journal_stop(handle);
	if (err)
		goto flags_out;

	if ((jflag ^ oldflags) & (EXT4_JOURNAL_DATA_FL)) {
		/*
		 * Changes to the journaling mode can cause unsafe changes to
		 * S_DAX if we are using the DAX mount option.
		 */
		if (test_opt(iyesde->i_sb, DAX)) {
			err = -EBUSY;
			goto flags_out;
		}

		err = ext4_change_iyesde_journal_flag(iyesde, jflag);
		if (err)
			goto flags_out;
	}
	if (migrate) {
		if (flags & EXT4_EXTENTS_FL)
			err = ext4_ext_migrate(iyesde);
		else
			err = ext4_ind_migrate(iyesde);
	}

flags_out:
	return err;
}

#ifdef CONFIG_QUOTA
static int ext4_ioctl_setproject(struct file *filp, __u32 projid)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct super_block *sb = iyesde->i_sb;
	struct ext4_iyesde_info *ei = EXT4_I(iyesde);
	int err, rc;
	handle_t *handle;
	kprojid_t kprojid;
	struct ext4_iloc iloc;
	struct ext4_iyesde *raw_iyesde;
	struct dquot *transfer_to[MAXQUOTAS] = { };

	if (!ext4_has_feature_project(sb)) {
		if (projid != EXT4_DEF_PROJID)
			return -EOPNOTSUPP;
		else
			return 0;
	}

	if (EXT4_INODE_SIZE(sb) <= EXT4_GOOD_OLD_INODE_SIZE)
		return -EOPNOTSUPP;

	kprojid = make_kprojid(&init_user_ns, (projid_t)projid);

	if (projid_eq(kprojid, EXT4_I(iyesde)->i_projid))
		return 0;

	err = -EPERM;
	/* Is it quota file? Do yest allow user to mess with it */
	if (ext4_is_quota_file(iyesde))
		return err;

	err = ext4_get_iyesde_loc(iyesde, &iloc);
	if (err)
		return err;

	raw_iyesde = ext4_raw_iyesde(&iloc);
	if (!EXT4_FITS_IN_INODE(raw_iyesde, ei, i_projid)) {
		err = ext4_expand_extra_isize(iyesde,
					      EXT4_SB(sb)->s_want_extra_isize,
					      &iloc);
		if (err)
			return err;
	} else {
		brelse(iloc.bh);
	}

	err = dquot_initialize(iyesde);
	if (err)
		return err;

	handle = ext4_journal_start(iyesde, EXT4_HT_QUOTA,
		EXT4_QUOTA_INIT_BLOCKS(sb) +
		EXT4_QUOTA_DEL_BLOCKS(sb) + 3);
	if (IS_ERR(handle))
		return PTR_ERR(handle);

	err = ext4_reserve_iyesde_write(handle, iyesde, &iloc);
	if (err)
		goto out_stop;

	transfer_to[PRJQUOTA] = dqget(sb, make_kqid_projid(kprojid));
	if (!IS_ERR(transfer_to[PRJQUOTA])) {

		/* __dquot_transfer() calls back ext4_get_iyesde_usage() which
		 * counts xattr iyesde references.
		 */
		down_read(&EXT4_I(iyesde)->xattr_sem);
		err = __dquot_transfer(iyesde, transfer_to);
		up_read(&EXT4_I(iyesde)->xattr_sem);
		dqput(transfer_to[PRJQUOTA]);
		if (err)
			goto out_dirty;
	}

	EXT4_I(iyesde)->i_projid = kprojid;
	iyesde->i_ctime = current_time(iyesde);
out_dirty:
	rc = ext4_mark_iloc_dirty(handle, iyesde, &iloc);
	if (!err)
		err = rc;
out_stop:
	ext4_journal_stop(handle);
	return err;
}
#else
static int ext4_ioctl_setproject(struct file *filp, __u32 projid)
{
	if (projid != EXT4_DEF_PROJID)
		return -EOPNOTSUPP;
	return 0;
}
#endif

/* Transfer internal flags to xflags */
static inline __u32 ext4_iflags_to_xflags(unsigned long iflags)
{
	__u32 xflags = 0;

	if (iflags & EXT4_SYNC_FL)
		xflags |= FS_XFLAG_SYNC;
	if (iflags & EXT4_IMMUTABLE_FL)
		xflags |= FS_XFLAG_IMMUTABLE;
	if (iflags & EXT4_APPEND_FL)
		xflags |= FS_XFLAG_APPEND;
	if (iflags & EXT4_NODUMP_FL)
		xflags |= FS_XFLAG_NODUMP;
	if (iflags & EXT4_NOATIME_FL)
		xflags |= FS_XFLAG_NOATIME;
	if (iflags & EXT4_PROJINHERIT_FL)
		xflags |= FS_XFLAG_PROJINHERIT;
	return xflags;
}

#define EXT4_SUPPORTED_FS_XFLAGS (FS_XFLAG_SYNC | FS_XFLAG_IMMUTABLE | \
				  FS_XFLAG_APPEND | FS_XFLAG_NODUMP | \
				  FS_XFLAG_NOATIME | FS_XFLAG_PROJINHERIT)

/* Transfer xflags flags to internal */
static inline unsigned long ext4_xflags_to_iflags(__u32 xflags)
{
	unsigned long iflags = 0;

	if (xflags & FS_XFLAG_SYNC)
		iflags |= EXT4_SYNC_FL;
	if (xflags & FS_XFLAG_IMMUTABLE)
		iflags |= EXT4_IMMUTABLE_FL;
	if (xflags & FS_XFLAG_APPEND)
		iflags |= EXT4_APPEND_FL;
	if (xflags & FS_XFLAG_NODUMP)
		iflags |= EXT4_NODUMP_FL;
	if (xflags & FS_XFLAG_NOATIME)
		iflags |= EXT4_NOATIME_FL;
	if (xflags & FS_XFLAG_PROJINHERIT)
		iflags |= EXT4_PROJINHERIT_FL;

	return iflags;
}

static int ext4_shutdown(struct super_block *sb, unsigned long arg)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	__u32 flags;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (get_user(flags, (__u32 __user *)arg))
		return -EFAULT;

	if (flags > EXT4_GOING_FLAGS_NOLOGFLUSH)
		return -EINVAL;

	if (ext4_forced_shutdown(sbi))
		return 0;

	ext4_msg(sb, KERN_ALERT, "shut down requested (%d)", flags);
	trace_ext4_shutdown(sb, flags);

	switch (flags) {
	case EXT4_GOING_FLAGS_DEFAULT:
		freeze_bdev(sb->s_bdev);
		set_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);
		thaw_bdev(sb->s_bdev, sb);
		break;
	case EXT4_GOING_FLAGS_LOGFLUSH:
		set_bit(EXT4_FLAGS_SHUTDOWN, &sbi->s_ext4_flags);
		if (sbi->s_journal && !is_journal_aborted(sbi->s_journal)) {
			(void) ext4_force_commit(sb);
			jbd2_journal_abort(sbi->s_journal, -ESHUTDOWN);
		}
		break;
	case EXT4_GOING_FLAGS_NOLOGFLUSH:
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
	if (error == EXT4_QUERY_RANGE_ABORT) {
		error = 0;
		aborted = true;
	} else if (error)
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
	struct super_block *sb = file_iyesde(file)->i_sb;
	int err, err2=0;

	err = ext4_resize_begin(sb);
	if (err)
		return err;

	if (ext4_has_feature_bigalloc(sb)) {
		ext4_msg(sb, KERN_ERR,
			 "Online resizing yest supported with bigalloc");
		err = -EOPNOTSUPP;
		goto group_add_out;
	}

	err = mnt_want_write_file(file);
	if (err)
		goto group_add_out;

	err = ext4_group_add(sb, input);
	if (EXT4_SB(sb)->s_journal) {
		jbd2_journal_lock_updates(EXT4_SB(sb)->s_journal);
		err2 = jbd2_journal_flush(EXT4_SB(sb)->s_journal);
		jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
	}
	if (err == 0)
		err = err2;
	mnt_drop_write_file(file);
	if (!err && ext4_has_group_desc_csum(sb) &&
	    test_opt(sb, INIT_INODE_TABLE))
		err = ext4_register_li_request(sb, input->group);
group_add_out:
	ext4_resize_end(sb);
	return err;
}

static void ext4_fill_fsxattr(struct iyesde *iyesde, struct fsxattr *fa)
{
	struct ext4_iyesde_info *ei = EXT4_I(iyesde);

	simple_fill_fsxattr(fa, ext4_iflags_to_xflags(ei->i_flags &
						      EXT4_FL_USER_VISIBLE));

	if (ext4_has_feature_project(iyesde->i_sb))
		fa->fsx_projid = from_kprojid(&init_user_ns, ei->i_projid);
}

/* copied from fs/ioctl.c */
static int fiemap_check_ranges(struct super_block *sb,
			       u64 start, u64 len, u64 *new_len)
{
	u64 maxbytes = (u64) sb->s_maxbytes;

	*new_len = len;

	if (len == 0)
		return -EINVAL;

	if (start > maxbytes)
		return -EFBIG;

	/*
	 * Shrink request scope to what the fs can actually handle.
	 */
	if (len > maxbytes || (maxbytes - len) < start)
		*new_len = maxbytes - start;

	return 0;
}

/* So that the fiemap access checks can't overflow on 32 bit machines. */
#define FIEMAP_MAX_EXTENTS	(UINT_MAX / sizeof(struct fiemap_extent))

static int ext4_ioctl_get_es_cache(struct file *filp, unsigned long arg)
{
	struct fiemap fiemap;
	struct fiemap __user *ufiemap = (struct fiemap __user *) arg;
	struct fiemap_extent_info fieinfo = { 0, };
	struct iyesde *iyesde = file_iyesde(filp);
	struct super_block *sb = iyesde->i_sb;
	u64 len;
	int error;

	if (copy_from_user(&fiemap, ufiemap, sizeof(fiemap)))
		return -EFAULT;

	if (fiemap.fm_extent_count > FIEMAP_MAX_EXTENTS)
		return -EINVAL;

	error = fiemap_check_ranges(sb, fiemap.fm_start, fiemap.fm_length,
				    &len);
	if (error)
		return error;

	fieinfo.fi_flags = fiemap.fm_flags;
	fieinfo.fi_extents_max = fiemap.fm_extent_count;
	fieinfo.fi_extents_start = ufiemap->fm_extents;

	if (fiemap.fm_extent_count != 0 &&
	    !access_ok(fieinfo.fi_extents_start,
		       fieinfo.fi_extents_max * sizeof(struct fiemap_extent)))
		return -EFAULT;

	if (fieinfo.fi_flags & FIEMAP_FLAG_SYNC)
		filemap_write_and_wait(iyesde->i_mapping);

	error = ext4_get_es_cache(iyesde, &fieinfo, fiemap.fm_start, len);
	fiemap.fm_flags = fieinfo.fi_flags;
	fiemap.fm_mapped_extents = fieinfo.fi_extents_mapped;
	if (copy_to_user(ufiemap, &fiemap, sizeof(fiemap)))
		error = -EFAULT;

	return error;
}

long ext4_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct iyesde *iyesde = file_iyesde(filp);
	struct super_block *sb = iyesde->i_sb;
	struct ext4_iyesde_info *ei = EXT4_I(iyesde);
	unsigned int flags;

	ext4_debug("cmd = %u, arg = %lu\n", cmd, arg);

	switch (cmd) {
	case FS_IOC_GETFSMAP:
		return ext4_ioc_getfsmap(sb, (void __user *)arg);
	case EXT4_IOC_GETFLAGS:
		flags = ei->i_flags & EXT4_FL_USER_VISIBLE;
		if (S_ISREG(iyesde->i_mode))
			flags &= ~EXT4_PROJINHERIT_FL;
		return put_user(flags, (int __user *) arg);
	case EXT4_IOC_SETFLAGS: {
		int err;

		if (!iyesde_owner_or_capable(iyesde))
			return -EACCES;

		if (get_user(flags, (int __user *) arg))
			return -EFAULT;

		if (flags & ~EXT4_FL_USER_VISIBLE)
			return -EOPNOTSUPP;
		/*
		 * chattr(1) grabs flags via GETFLAGS, modifies the result and
		 * passes that to SETFLAGS. So we canyest easily make SETFLAGS
		 * more restrictive than just silently masking off visible but
		 * yest settable flags as we always did.
		 */
		flags &= EXT4_FL_USER_MODIFIABLE;
		if (ext4_mask_flags(iyesde->i_mode, flags) != flags)
			return -EOPNOTSUPP;

		err = mnt_want_write_file(filp);
		if (err)
			return err;

		iyesde_lock(iyesde);
		err = ext4_ioctl_check_immutable(iyesde,
				from_kprojid(&init_user_ns, ei->i_projid),
				flags);
		if (!err)
			err = ext4_ioctl_setflags(iyesde, flags);
		iyesde_unlock(iyesde);
		mnt_drop_write_file(filp);
		return err;
	}
	case EXT4_IOC_GETVERSION:
	case EXT4_IOC_GETVERSION_OLD:
		return put_user(iyesde->i_generation, (int __user *) arg);
	case EXT4_IOC_SETVERSION:
	case EXT4_IOC_SETVERSION_OLD: {
		handle_t *handle;
		struct ext4_iloc iloc;
		__u32 generation;
		int err;

		if (!iyesde_owner_or_capable(iyesde))
			return -EPERM;

		if (ext4_has_metadata_csum(iyesde->i_sb)) {
			ext4_warning(sb, "Setting iyesde version is yest "
				     "supported with metadata_csum enabled.");
			return -ENOTTY;
		}

		err = mnt_want_write_file(filp);
		if (err)
			return err;
		if (get_user(generation, (int __user *) arg)) {
			err = -EFAULT;
			goto setversion_out;
		}

		iyesde_lock(iyesde);
		handle = ext4_journal_start(iyesde, EXT4_HT_INODE, 1);
		if (IS_ERR(handle)) {
			err = PTR_ERR(handle);
			goto unlock_out;
		}
		err = ext4_reserve_iyesde_write(handle, iyesde, &iloc);
		if (err == 0) {
			iyesde->i_ctime = current_time(iyesde);
			iyesde->i_generation = generation;
			err = ext4_mark_iloc_dirty(handle, iyesde, &iloc);
		}
		ext4_journal_stop(handle);

unlock_out:
		iyesde_unlock(iyesde);
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
				 "Online resizing yest supported with bigalloc");
			err = -EOPNOTSUPP;
			goto group_extend_out;
		}

		err = mnt_want_write_file(filp);
		if (err)
			goto group_extend_out;

		err = ext4_group_extend(sb, EXT4_SB(sb)->s_es, n_blocks_count);
		if (EXT4_SB(sb)->s_journal) {
			jbd2_journal_lock_updates(EXT4_SB(sb)->s_journal);
			err2 = jbd2_journal_flush(EXT4_SB(sb)->s_journal);
			jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
		}
		if (err == 0)
			err = err2;
		mnt_drop_write_file(filp);
group_extend_out:
		ext4_resize_end(sb);
		return err;
	}

	case EXT4_IOC_MOVE_EXT: {
		struct move_extent me;
		struct fd doyesr;
		int err;

		if (!(filp->f_mode & FMODE_READ) ||
		    !(filp->f_mode & FMODE_WRITE))
			return -EBADF;

		if (copy_from_user(&me,
			(struct move_extent __user *)arg, sizeof(me)))
			return -EFAULT;
		me.moved_len = 0;

		doyesr = fdget(me.doyesr_fd);
		if (!doyesr.file)
			return -EBADF;

		if (!(doyesr.file->f_mode & FMODE_WRITE)) {
			err = -EBADF;
			goto mext_out;
		}

		if (ext4_has_feature_bigalloc(sb)) {
			ext4_msg(sb, KERN_ERR,
				 "Online defrag yest supported with bigalloc");
			err = -EOPNOTSUPP;
			goto mext_out;
		} else if (IS_DAX(iyesde)) {
			ext4_msg(sb, KERN_ERR,
				 "Online defrag yest supported with DAX");
			err = -EOPNOTSUPP;
			goto mext_out;
		}

		err = mnt_want_write_file(filp);
		if (err)
			goto mext_out;

		err = ext4_move_extents(filp, doyesr.file, me.orig_start,
					me.doyesr_start, me.len, &me.moved_len);
		mnt_drop_write_file(filp);

		if (copy_to_user((struct move_extent __user *)arg,
				 &me, sizeof(me)))
			err = -EFAULT;
mext_out:
		fdput(doyesr);
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
		if (!iyesde_owner_or_capable(iyesde))
			return -EACCES;

		err = mnt_want_write_file(filp);
		if (err)
			return err;
		/*
		 * iyesde_mutex prevent write and truncate on the file.
		 * Read still goes through. We take i_data_sem in
		 * ext4_ext_swap_iyesde_data before we switch the
		 * iyesde format to prevent read.
		 */
		iyesde_lock((iyesde));
		err = ext4_ext_migrate(iyesde);
		iyesde_unlock((iyesde));
		mnt_drop_write_file(filp);
		return err;
	}

	case EXT4_IOC_ALLOC_DA_BLKS:
	{
		int err;
		if (!iyesde_owner_or_capable(iyesde))
			return -EACCES;

		err = mnt_want_write_file(filp);
		if (err)
			return err;
		err = ext4_alloc_da_blocks(iyesde);
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
		err = swap_iyesde_boot_loader(sb, iyesde);
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
			jbd2_journal_lock_updates(EXT4_SB(sb)->s_journal);
			err2 = jbd2_journal_flush(EXT4_SB(sb)->s_journal);
			jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
		}
		if (err == 0)
			err = err2;
		mnt_drop_write_file(filp);
		if (!err && (o_group < EXT4_SB(sb)->s_groups_count) &&
		    ext4_has_group_desc_csum(sb) &&
		    test_opt(sb, INIT_INODE_TABLE))
			err = ext4_register_li_request(sb, o_group);

resizefs_out:
		ext4_resize_end(sb);
		return err;
	}

	case FITRIM:
	{
		struct request_queue *q = bdev_get_queue(sb->s_bdev);
		struct fstrim_range range;
		int ret = 0;

		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;

		if (!blk_queue_discard(q))
			return -EOPNOTSUPP;

		/*
		 * We haven't replayed the journal, so we canyest use our
		 * block-bitmap-guided storage zapping commands.
		 */
		if (test_opt(sb, NOLOAD) && ext4_has_feature_journal(sb))
			return -EROFS;

		if (copy_from_user(&range, (struct fstrim_range __user *)arg,
		    sizeof(range)))
			return -EFAULT;

		range.minlen = max((unsigned int)range.minlen,
				   q->limits.discard_granularity);
		ret = ext4_trim_fs(sb, &range);
		if (ret < 0)
			return ret;

		if (copy_to_user((struct fstrim_range __user *)arg, &range,
		    sizeof(range)))
			return -EFAULT;

		return 0;
	}
	case EXT4_IOC_PRECACHE_EXTENTS:
		return ext4_ext_precache(iyesde);

	case EXT4_IOC_SET_ENCRYPTION_POLICY:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPNOTSUPP;
		return fscrypt_ioctl_set_policy(filp, (const void __user *)arg);

	case EXT4_IOC_GET_ENCRYPTION_PWSALT: {
#ifdef CONFIG_FS_ENCRYPTION
		int err, err2;
		struct ext4_sb_info *sbi = EXT4_SB(sb);
		handle_t *handle;

		if (!ext4_has_feature_encrypt(sb))
			return -EOPNOTSUPP;
		if (uuid_is_zero(sbi->s_es->s_encrypt_pw_salt)) {
			err = mnt_want_write_file(filp);
			if (err)
				return err;
			handle = ext4_journal_start_sb(sb, EXT4_HT_MISC, 1);
			if (IS_ERR(handle)) {
				err = PTR_ERR(handle);
				goto pwsalt_err_exit;
			}
			err = ext4_journal_get_write_access(handle, sbi->s_sbh);
			if (err)
				goto pwsalt_err_journal;
			generate_random_uuid(sbi->s_es->s_encrypt_pw_salt);
			err = ext4_handle_dirty_metadata(handle, NULL,
							 sbi->s_sbh);
		pwsalt_err_journal:
			err2 = ext4_journal_stop(handle);
			if (err2 && !err)
				err = err2;
		pwsalt_err_exit:
			mnt_drop_write_file(filp);
			if (err)
				return err;
		}
		if (copy_to_user((void __user *) arg,
				 sbi->s_es->s_encrypt_pw_salt, 16))
			return -EFAULT;
		return 0;
#else
		return -EOPNOTSUPP;
#endif
	}
	case EXT4_IOC_GET_ENCRYPTION_POLICY:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPNOTSUPP;
		return fscrypt_ioctl_get_policy(filp, (void __user *)arg);

	case FS_IOC_GET_ENCRYPTION_POLICY_EX:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPNOTSUPP;
		return fscrypt_ioctl_get_policy_ex(filp, (void __user *)arg);

	case FS_IOC_ADD_ENCRYPTION_KEY:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPNOTSUPP;
		return fscrypt_ioctl_add_key(filp, (void __user *)arg);

	case FS_IOC_REMOVE_ENCRYPTION_KEY:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPNOTSUPP;
		return fscrypt_ioctl_remove_key(filp, (void __user *)arg);

	case FS_IOC_REMOVE_ENCRYPTION_KEY_ALL_USERS:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPNOTSUPP;
		return fscrypt_ioctl_remove_key_all_users(filp,
							  (void __user *)arg);
	case FS_IOC_GET_ENCRYPTION_KEY_STATUS:
		if (!ext4_has_feature_encrypt(sb))
			return -EOPNOTSUPP;
		return fscrypt_ioctl_get_key_status(filp, (void __user *)arg);

	case EXT4_IOC_CLEAR_ES_CACHE:
	{
		if (!iyesde_owner_or_capable(iyesde))
			return -EACCES;
		ext4_clear_iyesde_es(iyesde);
		return 0;
	}

	case EXT4_IOC_GETSTATE:
	{
		__u32	state = 0;

		if (ext4_test_iyesde_state(iyesde, EXT4_STATE_EXT_PRECACHED))
			state |= EXT4_STATE_FLAG_EXT_PRECACHED;
		if (ext4_test_iyesde_state(iyesde, EXT4_STATE_NEW))
			state |= EXT4_STATE_FLAG_NEW;
		if (ext4_test_iyesde_state(iyesde, EXT4_STATE_NEWENTRY))
			state |= EXT4_STATE_FLAG_NEWENTRY;
		if (ext4_test_iyesde_state(iyesde, EXT4_STATE_DA_ALLOC_CLOSE))
			state |= EXT4_STATE_FLAG_DA_ALLOC_CLOSE;

		return put_user(state, (__u32 __user *) arg);
	}

	case EXT4_IOC_GET_ES_CACHE:
		return ext4_ioctl_get_es_cache(filp, arg);

	case EXT4_IOC_FSGETXATTR:
	{
		struct fsxattr fa;

		ext4_fill_fsxattr(iyesde, &fa);

		if (copy_to_user((struct fsxattr __user *)arg,
				 &fa, sizeof(fa)))
			return -EFAULT;
		return 0;
	}
	case EXT4_IOC_FSSETXATTR:
	{
		struct fsxattr fa, old_fa;
		int err;

		if (copy_from_user(&fa, (struct fsxattr __user *)arg,
				   sizeof(fa)))
			return -EFAULT;

		/* Make sure caller has proper permission */
		if (!iyesde_owner_or_capable(iyesde))
			return -EACCES;

		if (fa.fsx_xflags & ~EXT4_SUPPORTED_FS_XFLAGS)
			return -EOPNOTSUPP;

		flags = ext4_xflags_to_iflags(fa.fsx_xflags);
		if (ext4_mask_flags(iyesde->i_mode, flags) != flags)
			return -EOPNOTSUPP;

		err = mnt_want_write_file(filp);
		if (err)
			return err;

		iyesde_lock(iyesde);
		ext4_fill_fsxattr(iyesde, &old_fa);
		err = vfs_ioc_fssetxattr_check(iyesde, &old_fa, &fa);
		if (err)
			goto out;
		flags = (ei->i_flags & ~EXT4_FL_XFLAG_VISIBLE) |
			 (flags & EXT4_FL_XFLAG_VISIBLE);
		err = ext4_ioctl_check_immutable(iyesde, fa.fsx_projid, flags);
		if (err)
			goto out;
		err = ext4_ioctl_setflags(iyesde, flags);
		if (err)
			goto out;
		err = ext4_ioctl_setproject(filp, fa.fsx_projid);
out:
		iyesde_unlock(iyesde);
		mnt_drop_write_file(filp);
		return err;
	}
	case EXT4_IOC_SHUTDOWN:
		return ext4_shutdown(sb, arg);

	case FS_IOC_ENABLE_VERITY:
		if (!ext4_has_feature_verity(sb))
			return -EOPNOTSUPP;
		return fsverity_ioctl_enable(filp, (const void __user *)arg);

	case FS_IOC_MEASURE_VERITY:
		if (!ext4_has_feature_verity(sb))
			return -EOPNOTSUPP;
		return fsverity_ioctl_measure(filp, (void __user *)arg);

	default:
		return -ENOTTY;
	}
}

#ifdef CONFIG_COMPAT
long ext4_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	/* These are just misnamed, they actually get/put from/to user an int */
	switch (cmd) {
	case EXT4_IOC32_GETFLAGS:
		cmd = EXT4_IOC_GETFLAGS;
		break;
	case EXT4_IOC32_SETFLAGS:
		cmd = EXT4_IOC_SETFLAGS;
		break;
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
		err |= get_user(input.iyesde_bitmap, &uinput->iyesde_bitmap);
		err |= get_user(input.iyesde_table, &uinput->iyesde_table);
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
	case EXT4_IOC_SET_ENCRYPTION_POLICY:
	case EXT4_IOC_GET_ENCRYPTION_PWSALT:
	case EXT4_IOC_GET_ENCRYPTION_POLICY:
	case FS_IOC_GET_ENCRYPTION_POLICY_EX:
	case FS_IOC_ADD_ENCRYPTION_KEY:
	case FS_IOC_REMOVE_ENCRYPTION_KEY:
	case FS_IOC_REMOVE_ENCRYPTION_KEY_ALL_USERS:
	case FS_IOC_GET_ENCRYPTION_KEY_STATUS:
	case EXT4_IOC_SHUTDOWN:
	case FS_IOC_GETFSMAP:
	case FS_IOC_ENABLE_VERITY:
	case FS_IOC_MEASURE_VERITY:
	case EXT4_IOC_CLEAR_ES_CACHE:
	case EXT4_IOC_GETSTATE:
	case EXT4_IOC_GET_ES_CACHE:
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return ext4_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif
