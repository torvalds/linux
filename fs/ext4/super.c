/*
 *  linux/fs/ext4/super.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <linux/module.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/time.h>
#include <linux/vmalloc.h>
#include <linux/jbd2.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/parser.h>
#include <linux/smp_lock.h>
#include <linux/buffer_head.h>
#include <linux/exportfs.h>
#include <linux/vfs.h>
#include <linux/random.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/quotaops.h>
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/ctype.h>
#include <linux/log2.h>
#include <linux/crc16.h>
#include <asm/uaccess.h>

#include "ext4.h"
#include "ext4_jbd2.h"
#include "xattr.h"
#include "acl.h"
#include "mballoc.h"

#define CREATE_TRACE_POINTS
#include <trace/events/ext4.h>

struct proc_dir_entry *ext4_proc_root;
static struct kset *ext4_kset;

static int ext4_load_journal(struct super_block *, struct ext4_super_block *,
			     unsigned long journal_devnum);
static int ext4_commit_super(struct super_block *sb, int sync);
static void ext4_mark_recovery_complete(struct super_block *sb,
					struct ext4_super_block *es);
static void ext4_clear_journal_err(struct super_block *sb,
				   struct ext4_super_block *es);
static int ext4_sync_fs(struct super_block *sb, int wait);
static const char *ext4_decode_error(struct super_block *sb, int errno,
				     char nbuf[16]);
static int ext4_remount(struct super_block *sb, int *flags, char *data);
static int ext4_statfs(struct dentry *dentry, struct kstatfs *buf);
static int ext4_unfreeze(struct super_block *sb);
static void ext4_write_super(struct super_block *sb);
static int ext4_freeze(struct super_block *sb);
static int ext4_get_sb(struct file_system_type *fs_type, int flags,
		       const char *dev_name, void *data, struct vfsmount *mnt);

#if !defined(CONFIG_EXT3_FS) && !defined(CONFIG_EXT3_FS_MODULE) && defined(CONFIG_EXT4_USE_FOR_EXT23)
static struct file_system_type ext3_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ext3",
	.get_sb		= ext4_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
#define IS_EXT3_SB(sb) ((sb)->s_bdev->bd_holder == &ext3_fs_type)
#else
#define IS_EXT3_SB(sb) (0)
#endif

ext4_fsblk_t ext4_block_bitmap(struct super_block *sb,
			       struct ext4_group_desc *bg)
{
	return le32_to_cpu(bg->bg_block_bitmap_lo) |
		(EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT ?
		 (ext4_fsblk_t)le32_to_cpu(bg->bg_block_bitmap_hi) << 32 : 0);
}

ext4_fsblk_t ext4_inode_bitmap(struct super_block *sb,
			       struct ext4_group_desc *bg)
{
	return le32_to_cpu(bg->bg_inode_bitmap_lo) |
		(EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT ?
		 (ext4_fsblk_t)le32_to_cpu(bg->bg_inode_bitmap_hi) << 32 : 0);
}

ext4_fsblk_t ext4_inode_table(struct super_block *sb,
			      struct ext4_group_desc *bg)
{
	return le32_to_cpu(bg->bg_inode_table_lo) |
		(EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT ?
		 (ext4_fsblk_t)le32_to_cpu(bg->bg_inode_table_hi) << 32 : 0);
}

__u32 ext4_free_blks_count(struct super_block *sb,
			      struct ext4_group_desc *bg)
{
	return le16_to_cpu(bg->bg_free_blocks_count_lo) |
		(EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT ?
		 (__u32)le16_to_cpu(bg->bg_free_blocks_count_hi) << 16 : 0);
}

__u32 ext4_free_inodes_count(struct super_block *sb,
			      struct ext4_group_desc *bg)
{
	return le16_to_cpu(bg->bg_free_inodes_count_lo) |
		(EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT ?
		 (__u32)le16_to_cpu(bg->bg_free_inodes_count_hi) << 16 : 0);
}

__u32 ext4_used_dirs_count(struct super_block *sb,
			      struct ext4_group_desc *bg)
{
	return le16_to_cpu(bg->bg_used_dirs_count_lo) |
		(EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT ?
		 (__u32)le16_to_cpu(bg->bg_used_dirs_count_hi) << 16 : 0);
}

__u32 ext4_itable_unused_count(struct super_block *sb,
			      struct ext4_group_desc *bg)
{
	return le16_to_cpu(bg->bg_itable_unused_lo) |
		(EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT ?
		 (__u32)le16_to_cpu(bg->bg_itable_unused_hi) << 16 : 0);
}

void ext4_block_bitmap_set(struct super_block *sb,
			   struct ext4_group_desc *bg, ext4_fsblk_t blk)
{
	bg->bg_block_bitmap_lo = cpu_to_le32((u32)blk);
	if (EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT)
		bg->bg_block_bitmap_hi = cpu_to_le32(blk >> 32);
}

void ext4_inode_bitmap_set(struct super_block *sb,
			   struct ext4_group_desc *bg, ext4_fsblk_t blk)
{
	bg->bg_inode_bitmap_lo  = cpu_to_le32((u32)blk);
	if (EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT)
		bg->bg_inode_bitmap_hi = cpu_to_le32(blk >> 32);
}

void ext4_inode_table_set(struct super_block *sb,
			  struct ext4_group_desc *bg, ext4_fsblk_t blk)
{
	bg->bg_inode_table_lo = cpu_to_le32((u32)blk);
	if (EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT)
		bg->bg_inode_table_hi = cpu_to_le32(blk >> 32);
}

void ext4_free_blks_set(struct super_block *sb,
			  struct ext4_group_desc *bg, __u32 count)
{
	bg->bg_free_blocks_count_lo = cpu_to_le16((__u16)count);
	if (EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT)
		bg->bg_free_blocks_count_hi = cpu_to_le16(count >> 16);
}

void ext4_free_inodes_set(struct super_block *sb,
			  struct ext4_group_desc *bg, __u32 count)
{
	bg->bg_free_inodes_count_lo = cpu_to_le16((__u16)count);
	if (EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT)
		bg->bg_free_inodes_count_hi = cpu_to_le16(count >> 16);
}

void ext4_used_dirs_set(struct super_block *sb,
			  struct ext4_group_desc *bg, __u32 count)
{
	bg->bg_used_dirs_count_lo = cpu_to_le16((__u16)count);
	if (EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT)
		bg->bg_used_dirs_count_hi = cpu_to_le16(count >> 16);
}

void ext4_itable_unused_set(struct super_block *sb,
			  struct ext4_group_desc *bg, __u32 count)
{
	bg->bg_itable_unused_lo = cpu_to_le16((__u16)count);
	if (EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT)
		bg->bg_itable_unused_hi = cpu_to_le16(count >> 16);
}


/* Just increment the non-pointer handle value */
static handle_t *ext4_get_nojournal(void)
{
	handle_t *handle = current->journal_info;
	unsigned long ref_cnt = (unsigned long)handle;

	BUG_ON(ref_cnt >= EXT4_NOJOURNAL_MAX_REF_COUNT);

	ref_cnt++;
	handle = (handle_t *)ref_cnt;

	current->journal_info = handle;
	return handle;
}


/* Decrement the non-pointer handle value */
static void ext4_put_nojournal(handle_t *handle)
{
	unsigned long ref_cnt = (unsigned long)handle;

	BUG_ON(ref_cnt == 0);

	ref_cnt--;
	handle = (handle_t *)ref_cnt;

	current->journal_info = handle;
}

/*
 * Wrappers for jbd2_journal_start/end.
 *
 * The only special thing we need to do here is to make sure that all
 * journal_end calls result in the superblock being marked dirty, so
 * that sync() will call the filesystem's write_super callback if
 * appropriate.
 */
handle_t *ext4_journal_start_sb(struct super_block *sb, int nblocks)
{
	journal_t *journal;

	if (sb->s_flags & MS_RDONLY)
		return ERR_PTR(-EROFS);

	vfs_check_frozen(sb, SB_FREEZE_TRANS);
	/* Special case here: if the journal has aborted behind our
	 * backs (eg. EIO in the commit thread), then we still need to
	 * take the FS itself readonly cleanly. */
	journal = EXT4_SB(sb)->s_journal;
	if (journal) {
		if (is_journal_aborted(journal)) {
			ext4_abort(sb, "Detected aborted journal");
			return ERR_PTR(-EROFS);
		}
		return jbd2_journal_start(journal, nblocks);
	}
	return ext4_get_nojournal();
}

/*
 * The only special thing we need to do here is to make sure that all
 * jbd2_journal_stop calls result in the superblock being marked dirty, so
 * that sync() will call the filesystem's write_super callback if
 * appropriate.
 */
int __ext4_journal_stop(const char *where, unsigned int line, handle_t *handle)
{
	struct super_block *sb;
	int err;
	int rc;

	if (!ext4_handle_valid(handle)) {
		ext4_put_nojournal(handle);
		return 0;
	}
	sb = handle->h_transaction->t_journal->j_private;
	err = handle->h_err;
	rc = jbd2_journal_stop(handle);

	if (!err)
		err = rc;
	if (err)
		__ext4_std_error(sb, where, line, err);
	return err;
}

void ext4_journal_abort_handle(const char *caller, unsigned int line,
			       const char *err_fn, struct buffer_head *bh,
			       handle_t *handle, int err)
{
	char nbuf[16];
	const char *errstr = ext4_decode_error(NULL, err, nbuf);

	BUG_ON(!ext4_handle_valid(handle));

	if (bh)
		BUFFER_TRACE(bh, "abort");

	if (!handle->h_err)
		handle->h_err = err;

	if (is_handle_aborted(handle))
		return;

	printk(KERN_ERR "%s:%d: aborting transaction: %s in %s\n",
	       caller, line, errstr, err_fn);

	jbd2_journal_abort_handle(handle);
}

static void __save_error_info(struct super_block *sb, const char *func,
			    unsigned int line)
{
	struct ext4_super_block *es = EXT4_SB(sb)->s_es;

	EXT4_SB(sb)->s_mount_state |= EXT4_ERROR_FS;
	es->s_state |= cpu_to_le16(EXT4_ERROR_FS);
	es->s_last_error_time = cpu_to_le32(get_seconds());
	strncpy(es->s_last_error_func, func, sizeof(es->s_last_error_func));
	es->s_last_error_line = cpu_to_le32(line);
	if (!es->s_first_error_time) {
		es->s_first_error_time = es->s_last_error_time;
		strncpy(es->s_first_error_func, func,
			sizeof(es->s_first_error_func));
		es->s_first_error_line = cpu_to_le32(line);
		es->s_first_error_ino = es->s_last_error_ino;
		es->s_first_error_block = es->s_last_error_block;
	}
	/*
	 * Start the daily error reporting function if it hasn't been
	 * started already
	 */
	if (!es->s_error_count)
		mod_timer(&EXT4_SB(sb)->s_err_report, jiffies + 24*60*60*HZ);
	es->s_error_count = cpu_to_le32(le32_to_cpu(es->s_error_count) + 1);
}

static void save_error_info(struct super_block *sb, const char *func,
			    unsigned int line)
{
	__save_error_info(sb, func, line);
	ext4_commit_super(sb, 1);
}


/* Deal with the reporting of failure conditions on a filesystem such as
 * inconsistencies detected or read IO failures.
 *
 * On ext2, we can store the error state of the filesystem in the
 * superblock.  That is not possible on ext4, because we may have other
 * write ordering constraints on the superblock which prevent us from
 * writing it out straight away; and given that the journal is about to
 * be aborted, we can't rely on the current, or future, transactions to
 * write out the superblock safely.
 *
 * We'll just use the jbd2_journal_abort() error code to record an error in
 * the journal instead.  On recovery, the journal will complain about
 * that error until we've noted it down and cleared it.
 */

static void ext4_handle_error(struct super_block *sb)
{
	if (sb->s_flags & MS_RDONLY)
		return;

	if (!test_opt(sb, ERRORS_CONT)) {
		journal_t *journal = EXT4_SB(sb)->s_journal;

		EXT4_SB(sb)->s_mount_flags |= EXT4_MF_FS_ABORTED;
		if (journal)
			jbd2_journal_abort(journal, -EIO);
	}
	if (test_opt(sb, ERRORS_RO)) {
		ext4_msg(sb, KERN_CRIT, "Remounting filesystem read-only");
		sb->s_flags |= MS_RDONLY;
	}
	if (test_opt(sb, ERRORS_PANIC))
		panic("EXT4-fs (device %s): panic forced after error\n",
			sb->s_id);
}

void __ext4_error(struct super_block *sb, const char *function,
		  unsigned int line, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	printk(KERN_CRIT "EXT4-fs error (device %s): %s:%d: comm %s: ",
	       sb->s_id, function, line, current->comm);
	vprintk(fmt, args);
	printk("\n");
	va_end(args);

	ext4_handle_error(sb);
}

void ext4_error_inode(struct inode *inode, const char *function,
		      unsigned int line, ext4_fsblk_t block,
		      const char *fmt, ...)
{
	va_list args;
	struct ext4_super_block *es = EXT4_SB(inode->i_sb)->s_es;

	es->s_last_error_ino = cpu_to_le32(inode->i_ino);
	es->s_last_error_block = cpu_to_le64(block);
	save_error_info(inode->i_sb, function, line);
	va_start(args, fmt);
	printk(KERN_CRIT "EXT4-fs error (device %s): %s:%d: inode #%lu: ",
	       inode->i_sb->s_id, function, line, inode->i_ino);
	if (block)
		printk("block %llu: ", block);
	printk("comm %s: ", current->comm);
	vprintk(fmt, args);
	printk("\n");
	va_end(args);

	ext4_handle_error(inode->i_sb);
}

void ext4_error_file(struct file *file, const char *function,
		     unsigned int line, const char *fmt, ...)
{
	va_list args;
	struct ext4_super_block *es;
	struct inode *inode = file->f_dentry->d_inode;
	char pathname[80], *path;

	es = EXT4_SB(inode->i_sb)->s_es;
	es->s_last_error_ino = cpu_to_le32(inode->i_ino);
	save_error_info(inode->i_sb, function, line);
	va_start(args, fmt);
	path = d_path(&(file->f_path), pathname, sizeof(pathname));
	if (!path)
		path = "(unknown)";
	printk(KERN_CRIT
	       "EXT4-fs error (device %s): %s:%d: inode #%lu "
	       "(comm %s path %s): ",
	       inode->i_sb->s_id, function, line, inode->i_ino,
	       current->comm, path);
	vprintk(fmt, args);
	printk("\n");
	va_end(args);

	ext4_handle_error(inode->i_sb);
}

static const char *ext4_decode_error(struct super_block *sb, int errno,
				     char nbuf[16])
{
	char *errstr = NULL;

	switch (errno) {
	case -EIO:
		errstr = "IO failure";
		break;
	case -ENOMEM:
		errstr = "Out of memory";
		break;
	case -EROFS:
		if (!sb || (EXT4_SB(sb)->s_journal &&
			    EXT4_SB(sb)->s_journal->j_flags & JBD2_ABORT))
			errstr = "Journal has aborted";
		else
			errstr = "Readonly filesystem";
		break;
	default:
		/* If the caller passed in an extra buffer for unknown
		 * errors, textualise them now.  Else we just return
		 * NULL. */
		if (nbuf) {
			/* Check for truncated error codes... */
			if (snprintf(nbuf, 16, "error %d", -errno) >= 0)
				errstr = nbuf;
		}
		break;
	}

	return errstr;
}

/* __ext4_std_error decodes expected errors from journaling functions
 * automatically and invokes the appropriate error response.  */

void __ext4_std_error(struct super_block *sb, const char *function,
		      unsigned int line, int errno)
{
	char nbuf[16];
	const char *errstr;

	/* Special case: if the error is EROFS, and we're not already
	 * inside a transaction, then there's really no point in logging
	 * an error. */
	if (errno == -EROFS && journal_current_handle() == NULL &&
	    (sb->s_flags & MS_RDONLY))
		return;

	errstr = ext4_decode_error(sb, errno, nbuf);
	printk(KERN_CRIT "EXT4-fs error (device %s) in %s:%d: %s\n",
	       sb->s_id, function, line, errstr);
	save_error_info(sb, function, line);

	ext4_handle_error(sb);
}

/*
 * ext4_abort is a much stronger failure handler than ext4_error.  The
 * abort function may be used to deal with unrecoverable failures such
 * as journal IO errors or ENOMEM at a critical moment in log management.
 *
 * We unconditionally force the filesystem into an ABORT|READONLY state,
 * unless the error response on the fs has been set to panic in which
 * case we take the easy way out and panic immediately.
 */

void __ext4_abort(struct super_block *sb, const char *function,
		unsigned int line, const char *fmt, ...)
{
	va_list args;

	save_error_info(sb, function, line);
	va_start(args, fmt);
	printk(KERN_CRIT "EXT4-fs error (device %s): %s:%d: ", sb->s_id,
	       function, line);
	vprintk(fmt, args);
	printk("\n");
	va_end(args);

	if ((sb->s_flags & MS_RDONLY) == 0) {
		ext4_msg(sb, KERN_CRIT, "Remounting filesystem read-only");
		sb->s_flags |= MS_RDONLY;
		EXT4_SB(sb)->s_mount_flags |= EXT4_MF_FS_ABORTED;
		if (EXT4_SB(sb)->s_journal)
			jbd2_journal_abort(EXT4_SB(sb)->s_journal, -EIO);
		save_error_info(sb, function, line);
	}
	if (test_opt(sb, ERRORS_PANIC))
		panic("EXT4-fs panic from previous error\n");
}

void ext4_msg (struct super_block * sb, const char *prefix,
		   const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	printk("%sEXT4-fs (%s): ", prefix, sb->s_id);
	vprintk(fmt, args);
	printk("\n");
	va_end(args);
}

void __ext4_warning(struct super_block *sb, const char *function,
		    unsigned int line, const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	printk(KERN_WARNING "EXT4-fs warning (device %s): %s:%d: ",
	       sb->s_id, function, line);
	vprintk(fmt, args);
	printk("\n");
	va_end(args);
}

void __ext4_grp_locked_error(const char *function, unsigned int line,
			     struct super_block *sb, ext4_group_t grp,
			     unsigned long ino, ext4_fsblk_t block,
			     const char *fmt, ...)
__releases(bitlock)
__acquires(bitlock)
{
	va_list args;
	struct ext4_super_block *es = EXT4_SB(sb)->s_es;

	es->s_last_error_ino = cpu_to_le32(ino);
	es->s_last_error_block = cpu_to_le64(block);
	__save_error_info(sb, function, line);
	va_start(args, fmt);
	printk(KERN_CRIT "EXT4-fs error (device %s): %s:%d: group %u",
	       sb->s_id, function, line, grp);
	if (ino)
		printk("inode %lu: ", ino);
	if (block)
		printk("block %llu:", (unsigned long long) block);
	vprintk(fmt, args);
	printk("\n");
	va_end(args);

	if (test_opt(sb, ERRORS_CONT)) {
		ext4_commit_super(sb, 0);
		return;
	}

	ext4_unlock_group(sb, grp);
	ext4_handle_error(sb);
	/*
	 * We only get here in the ERRORS_RO case; relocking the group
	 * may be dangerous, but nothing bad will happen since the
	 * filesystem will have already been marked read/only and the
	 * journal has been aborted.  We return 1 as a hint to callers
	 * who might what to use the return value from
	 * ext4_grp_locked_error() to distinguish beween the
	 * ERRORS_CONT and ERRORS_RO case, and perhaps return more
	 * aggressively from the ext4 function in question, with a
	 * more appropriate error code.
	 */
	ext4_lock_group(sb, grp);
	return;
}

void ext4_update_dynamic_rev(struct super_block *sb)
{
	struct ext4_super_block *es = EXT4_SB(sb)->s_es;

	if (le32_to_cpu(es->s_rev_level) > EXT4_GOOD_OLD_REV)
		return;

	ext4_warning(sb,
		     "updating to rev %d because of new feature flag, "
		     "running e2fsck is recommended",
		     EXT4_DYNAMIC_REV);

	es->s_first_ino = cpu_to_le32(EXT4_GOOD_OLD_FIRST_INO);
	es->s_inode_size = cpu_to_le16(EXT4_GOOD_OLD_INODE_SIZE);
	es->s_rev_level = cpu_to_le32(EXT4_DYNAMIC_REV);
	/* leave es->s_feature_*compat flags alone */
	/* es->s_uuid will be set by e2fsck if empty */

	/*
	 * The rest of the superblock fields should be zero, and if not it
	 * means they are likely already in use, so leave them alone.  We
	 * can leave it up to e2fsck to clean up any inconsistencies there.
	 */
}

/*
 * Open the external journal device
 */
static struct block_device *ext4_blkdev_get(dev_t dev, struct super_block *sb)
{
	struct block_device *bdev;
	char b[BDEVNAME_SIZE];

	bdev = open_by_devnum(dev, FMODE_READ|FMODE_WRITE);
	if (IS_ERR(bdev))
		goto fail;
	return bdev;

fail:
	ext4_msg(sb, KERN_ERR, "failed to open journal device %s: %ld",
			__bdevname(dev, b), PTR_ERR(bdev));
	return NULL;
}

/*
 * Release the journal device
 */
static int ext4_blkdev_put(struct block_device *bdev)
{
	bd_release(bdev);
	return blkdev_put(bdev, FMODE_READ|FMODE_WRITE);
}

static int ext4_blkdev_remove(struct ext4_sb_info *sbi)
{
	struct block_device *bdev;
	int ret = -ENODEV;

	bdev = sbi->journal_bdev;
	if (bdev) {
		ret = ext4_blkdev_put(bdev);
		sbi->journal_bdev = NULL;
	}
	return ret;
}

static inline struct inode *orphan_list_entry(struct list_head *l)
{
	return &list_entry(l, struct ext4_inode_info, i_orphan)->vfs_inode;
}

static void dump_orphan_list(struct super_block *sb, struct ext4_sb_info *sbi)
{
	struct list_head *l;

	ext4_msg(sb, KERN_ERR, "sb orphan head is %d",
		 le32_to_cpu(sbi->s_es->s_last_orphan));

	printk(KERN_ERR "sb_info orphan list:\n");
	list_for_each(l, &sbi->s_orphan) {
		struct inode *inode = orphan_list_entry(l);
		printk(KERN_ERR "  "
		       "inode %s:%lu at %p: mode %o, nlink %d, next %d\n",
		       inode->i_sb->s_id, inode->i_ino, inode,
		       inode->i_mode, inode->i_nlink,
		       NEXT_ORPHAN(inode));
	}
}

static void ext4_put_super(struct super_block *sb)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_super_block *es = sbi->s_es;
	int i, err;

	dquot_disable(sb, -1, DQUOT_USAGE_ENABLED | DQUOT_LIMITS_ENABLED);

	flush_workqueue(sbi->dio_unwritten_wq);
	destroy_workqueue(sbi->dio_unwritten_wq);

	lock_super(sb);
	lock_kernel();
	if (sb->s_dirt)
		ext4_commit_super(sb, 1);

	if (sbi->s_journal) {
		err = jbd2_journal_destroy(sbi->s_journal);
		sbi->s_journal = NULL;
		if (err < 0)
			ext4_abort(sb, "Couldn't clean up the journal");
	}

	ext4_release_system_zone(sb);
	ext4_mb_release(sb);
	ext4_ext_release(sb);
	ext4_xattr_put_super(sb);

	if (!(sb->s_flags & MS_RDONLY)) {
		EXT4_CLEAR_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER);
		es->s_state = cpu_to_le16(sbi->s_mount_state);
		ext4_commit_super(sb, 1);
	}
	if (sbi->s_proc) {
		remove_proc_entry(sb->s_id, ext4_proc_root);
	}
	kobject_del(&sbi->s_kobj);

	for (i = 0; i < sbi->s_gdb_count; i++)
		brelse(sbi->s_group_desc[i]);
	kfree(sbi->s_group_desc);
	if (is_vmalloc_addr(sbi->s_flex_groups))
		vfree(sbi->s_flex_groups);
	else
		kfree(sbi->s_flex_groups);
	percpu_counter_destroy(&sbi->s_freeblocks_counter);
	percpu_counter_destroy(&sbi->s_freeinodes_counter);
	percpu_counter_destroy(&sbi->s_dirs_counter);
	percpu_counter_destroy(&sbi->s_dirtyblocks_counter);
	brelse(sbi->s_sbh);
#ifdef CONFIG_QUOTA
	for (i = 0; i < MAXQUOTAS; i++)
		kfree(sbi->s_qf_names[i]);
#endif

	/* Debugging code just in case the in-memory inode orphan list
	 * isn't empty.  The on-disk one can be non-empty if we've
	 * detected an error and taken the fs readonly, but the
	 * in-memory list had better be clean by this point. */
	if (!list_empty(&sbi->s_orphan))
		dump_orphan_list(sb, sbi);
	J_ASSERT(list_empty(&sbi->s_orphan));

	invalidate_bdev(sb->s_bdev);
	if (sbi->journal_bdev && sbi->journal_bdev != sb->s_bdev) {
		/*
		 * Invalidate the journal device's buffers.  We don't want them
		 * floating about in memory - the physical journal device may
		 * hotswapped, and it breaks the `ro-after' testing code.
		 */
		sync_blockdev(sbi->journal_bdev);
		invalidate_bdev(sbi->journal_bdev);
		ext4_blkdev_remove(sbi);
	}
	sb->s_fs_info = NULL;
	/*
	 * Now that we are completely done shutting down the
	 * superblock, we need to actually destroy the kobject.
	 */
	unlock_kernel();
	unlock_super(sb);
	kobject_put(&sbi->s_kobj);
	wait_for_completion(&sbi->s_kobj_unregister);
	kfree(sbi->s_blockgroup_lock);
	kfree(sbi);
}

static struct kmem_cache *ext4_inode_cachep;

/*
 * Called inside transaction, so use GFP_NOFS
 */
static struct inode *ext4_alloc_inode(struct super_block *sb)
{
	struct ext4_inode_info *ei;

	ei = kmem_cache_alloc(ext4_inode_cachep, GFP_NOFS);
	if (!ei)
		return NULL;

	ei->vfs_inode.i_version = 1;
	ei->vfs_inode.i_data.writeback_index = 0;
	memset(&ei->i_cached_extent, 0, sizeof(struct ext4_ext_cache));
	INIT_LIST_HEAD(&ei->i_prealloc_list);
	spin_lock_init(&ei->i_prealloc_lock);
	/*
	 * Note:  We can be called before EXT4_SB(sb)->s_journal is set,
	 * therefore it can be null here.  Don't check it, just initialize
	 * jinode.
	 */
	jbd2_journal_init_jbd_inode(&ei->jinode, &ei->vfs_inode);
	ei->i_reserved_data_blocks = 0;
	ei->i_reserved_meta_blocks = 0;
	ei->i_allocated_meta_blocks = 0;
	ei->i_da_metadata_calc_len = 0;
	ei->i_delalloc_reserved_flag = 0;
	spin_lock_init(&(ei->i_block_reservation_lock));
#ifdef CONFIG_QUOTA
	ei->i_reserved_quota = 0;
#endif
	INIT_LIST_HEAD(&ei->i_completed_io_list);
	spin_lock_init(&ei->i_completed_io_lock);
	ei->cur_aio_dio = NULL;
	ei->i_sync_tid = 0;
	ei->i_datasync_tid = 0;

	return &ei->vfs_inode;
}

static void ext4_destroy_inode(struct inode *inode)
{
	if (!list_empty(&(EXT4_I(inode)->i_orphan))) {
		ext4_msg(inode->i_sb, KERN_ERR,
			 "Inode %lu (%p): orphan list check failed!",
			 inode->i_ino, EXT4_I(inode));
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 16, 4,
				EXT4_I(inode), sizeof(struct ext4_inode_info),
				true);
		dump_stack();
	}
	kmem_cache_free(ext4_inode_cachep, EXT4_I(inode));
}

static void init_once(void *foo)
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *) foo;

	INIT_LIST_HEAD(&ei->i_orphan);
#ifdef CONFIG_EXT4_FS_XATTR
	init_rwsem(&ei->xattr_sem);
#endif
	init_rwsem(&ei->i_data_sem);
	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	ext4_inode_cachep = kmem_cache_create("ext4_inode_cache",
					     sizeof(struct ext4_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once);
	if (ext4_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	kmem_cache_destroy(ext4_inode_cachep);
}

void ext4_clear_inode(struct inode *inode)
{
	invalidate_inode_buffers(inode);
	end_writeback(inode);
	dquot_drop(inode);
	ext4_discard_preallocations(inode);
	if (EXT4_JOURNAL(inode))
		jbd2_journal_release_jbd_inode(EXT4_SB(inode->i_sb)->s_journal,
				       &EXT4_I(inode)->jinode);
}

static inline void ext4_show_quota_options(struct seq_file *seq,
					   struct super_block *sb)
{
#if defined(CONFIG_QUOTA)
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	if (sbi->s_jquota_fmt) {
		char *fmtname = "";

		switch (sbi->s_jquota_fmt) {
		case QFMT_VFS_OLD:
			fmtname = "vfsold";
			break;
		case QFMT_VFS_V0:
			fmtname = "vfsv0";
			break;
		case QFMT_VFS_V1:
			fmtname = "vfsv1";
			break;
		}
		seq_printf(seq, ",jqfmt=%s", fmtname);
	}

	if (sbi->s_qf_names[USRQUOTA])
		seq_printf(seq, ",usrjquota=%s", sbi->s_qf_names[USRQUOTA]);

	if (sbi->s_qf_names[GRPQUOTA])
		seq_printf(seq, ",grpjquota=%s", sbi->s_qf_names[GRPQUOTA]);

	if (test_opt(sb, USRQUOTA))
		seq_puts(seq, ",usrquota");

	if (test_opt(sb, GRPQUOTA))
		seq_puts(seq, ",grpquota");
#endif
}

/*
 * Show an option if
 *  - it's set to a non-default value OR
 *  - if the per-sb default is different from the global default
 */
static int ext4_show_options(struct seq_file *seq, struct vfsmount *vfs)
{
	int def_errors;
	unsigned long def_mount_opts;
	struct super_block *sb = vfs->mnt_sb;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_super_block *es = sbi->s_es;

	def_mount_opts = le32_to_cpu(es->s_default_mount_opts);
	def_errors     = le16_to_cpu(es->s_errors);

	if (sbi->s_sb_block != 1)
		seq_printf(seq, ",sb=%llu", sbi->s_sb_block);
	if (test_opt(sb, MINIX_DF))
		seq_puts(seq, ",minixdf");
	if (test_opt(sb, GRPID) && !(def_mount_opts & EXT4_DEFM_BSDGROUPS))
		seq_puts(seq, ",grpid");
	if (!test_opt(sb, GRPID) && (def_mount_opts & EXT4_DEFM_BSDGROUPS))
		seq_puts(seq, ",nogrpid");
	if (sbi->s_resuid != EXT4_DEF_RESUID ||
	    le16_to_cpu(es->s_def_resuid) != EXT4_DEF_RESUID) {
		seq_printf(seq, ",resuid=%u", sbi->s_resuid);
	}
	if (sbi->s_resgid != EXT4_DEF_RESGID ||
	    le16_to_cpu(es->s_def_resgid) != EXT4_DEF_RESGID) {
		seq_printf(seq, ",resgid=%u", sbi->s_resgid);
	}
	if (test_opt(sb, ERRORS_RO)) {
		if (def_errors == EXT4_ERRORS_PANIC ||
		    def_errors == EXT4_ERRORS_CONTINUE) {
			seq_puts(seq, ",errors=remount-ro");
		}
	}
	if (test_opt(sb, ERRORS_CONT) && def_errors != EXT4_ERRORS_CONTINUE)
		seq_puts(seq, ",errors=continue");
	if (test_opt(sb, ERRORS_PANIC) && def_errors != EXT4_ERRORS_PANIC)
		seq_puts(seq, ",errors=panic");
	if (test_opt(sb, NO_UID32) && !(def_mount_opts & EXT4_DEFM_UID16))
		seq_puts(seq, ",nouid32");
	if (test_opt(sb, DEBUG) && !(def_mount_opts & EXT4_DEFM_DEBUG))
		seq_puts(seq, ",debug");
	if (test_opt(sb, OLDALLOC))
		seq_puts(seq, ",oldalloc");
#ifdef CONFIG_EXT4_FS_XATTR
	if (test_opt(sb, XATTR_USER) &&
		!(def_mount_opts & EXT4_DEFM_XATTR_USER))
		seq_puts(seq, ",user_xattr");
	if (!test_opt(sb, XATTR_USER) &&
	    (def_mount_opts & EXT4_DEFM_XATTR_USER)) {
		seq_puts(seq, ",nouser_xattr");
	}
#endif
#ifdef CONFIG_EXT4_FS_POSIX_ACL
	if (test_opt(sb, POSIX_ACL) && !(def_mount_opts & EXT4_DEFM_ACL))
		seq_puts(seq, ",acl");
	if (!test_opt(sb, POSIX_ACL) && (def_mount_opts & EXT4_DEFM_ACL))
		seq_puts(seq, ",noacl");
#endif
	if (sbi->s_commit_interval != JBD2_DEFAULT_MAX_COMMIT_AGE*HZ) {
		seq_printf(seq, ",commit=%u",
			   (unsigned) (sbi->s_commit_interval / HZ));
	}
	if (sbi->s_min_batch_time != EXT4_DEF_MIN_BATCH_TIME) {
		seq_printf(seq, ",min_batch_time=%u",
			   (unsigned) sbi->s_min_batch_time);
	}
	if (sbi->s_max_batch_time != EXT4_DEF_MAX_BATCH_TIME) {
		seq_printf(seq, ",max_batch_time=%u",
			   (unsigned) sbi->s_min_batch_time);
	}

	/*
	 * We're changing the default of barrier mount option, so
	 * let's always display its mount state so it's clear what its
	 * status is.
	 */
	seq_puts(seq, ",barrier=");
	seq_puts(seq, test_opt(sb, BARRIER) ? "1" : "0");
	if (test_opt(sb, JOURNAL_ASYNC_COMMIT))
		seq_puts(seq, ",journal_async_commit");
	else if (test_opt(sb, JOURNAL_CHECKSUM))
		seq_puts(seq, ",journal_checksum");
	if (test_opt(sb, I_VERSION))
		seq_puts(seq, ",i_version");
	if (!test_opt(sb, DELALLOC) &&
	    !(def_mount_opts & EXT4_DEFM_NODELALLOC))
		seq_puts(seq, ",nodelalloc");

	if (sbi->s_stripe)
		seq_printf(seq, ",stripe=%lu", sbi->s_stripe);
	/*
	 * journal mode get enabled in different ways
	 * So just print the value even if we didn't specify it
	 */
	if (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_JOURNAL_DATA)
		seq_puts(seq, ",data=journal");
	else if (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_ORDERED_DATA)
		seq_puts(seq, ",data=ordered");
	else if (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_WRITEBACK_DATA)
		seq_puts(seq, ",data=writeback");

	if (sbi->s_inode_readahead_blks != EXT4_DEF_INODE_READAHEAD_BLKS)
		seq_printf(seq, ",inode_readahead_blks=%u",
			   sbi->s_inode_readahead_blks);

	if (test_opt(sb, DATA_ERR_ABORT))
		seq_puts(seq, ",data_err=abort");

	if (test_opt(sb, NO_AUTO_DA_ALLOC))
		seq_puts(seq, ",noauto_da_alloc");

	if (test_opt(sb, DISCARD) && !(def_mount_opts & EXT4_DEFM_DISCARD))
		seq_puts(seq, ",discard");

	if (test_opt(sb, NOLOAD))
		seq_puts(seq, ",norecovery");

	if (test_opt(sb, DIOREAD_NOLOCK))
		seq_puts(seq, ",dioread_nolock");

	if (test_opt(sb, BLOCK_VALIDITY) &&
	    !(def_mount_opts & EXT4_DEFM_BLOCK_VALIDITY))
		seq_puts(seq, ",block_validity");

	ext4_show_quota_options(seq, sb);

	return 0;
}

static struct inode *ext4_nfs_get_inode(struct super_block *sb,
					u64 ino, u32 generation)
{
	struct inode *inode;

	if (ino < EXT4_FIRST_INO(sb) && ino != EXT4_ROOT_INO)
		return ERR_PTR(-ESTALE);
	if (ino > le32_to_cpu(EXT4_SB(sb)->s_es->s_inodes_count))
		return ERR_PTR(-ESTALE);

	/* iget isn't really right if the inode is currently unallocated!!
	 *
	 * ext4_read_inode will return a bad_inode if the inode had been
	 * deleted, so we should be safe.
	 *
	 * Currently we don't know the generation for parent directory, so
	 * a generation of 0 means "accept any"
	 */
	inode = ext4_iget(sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	if (generation && inode->i_generation != generation) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}

	return inode;
}

static struct dentry *ext4_fh_to_dentry(struct super_block *sb, struct fid *fid,
					int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    ext4_nfs_get_inode);
}

static struct dentry *ext4_fh_to_parent(struct super_block *sb, struct fid *fid,
					int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    ext4_nfs_get_inode);
}

/*
 * Try to release metadata pages (indirect blocks, directories) which are
 * mapped via the block device.  Since these pages could have journal heads
 * which would prevent try_to_free_buffers() from freeing them, we must use
 * jbd2 layer's try_to_free_buffers() function to release them.
 */
static int bdev_try_to_free_page(struct super_block *sb, struct page *page,
				 gfp_t wait)
{
	journal_t *journal = EXT4_SB(sb)->s_journal;

	WARN_ON(PageChecked(page));
	if (!page_has_buffers(page))
		return 0;
	if (journal)
		return jbd2_journal_try_to_free_buffers(journal, page,
							wait & ~__GFP_WAIT);
	return try_to_free_buffers(page);
}

#ifdef CONFIG_QUOTA
#define QTYPE2NAME(t) ((t) == USRQUOTA ? "user" : "group")
#define QTYPE2MOPT(on, t) ((t) == USRQUOTA?((on)##USRJQUOTA):((on)##GRPJQUOTA))

static int ext4_write_dquot(struct dquot *dquot);
static int ext4_acquire_dquot(struct dquot *dquot);
static int ext4_release_dquot(struct dquot *dquot);
static int ext4_mark_dquot_dirty(struct dquot *dquot);
static int ext4_write_info(struct super_block *sb, int type);
static int ext4_quota_on(struct super_block *sb, int type, int format_id,
				char *path);
static int ext4_quota_off(struct super_block *sb, int type);
static int ext4_quota_on_mount(struct super_block *sb, int type);
static ssize_t ext4_quota_read(struct super_block *sb, int type, char *data,
			       size_t len, loff_t off);
static ssize_t ext4_quota_write(struct super_block *sb, int type,
				const char *data, size_t len, loff_t off);

static const struct dquot_operations ext4_quota_operations = {
#ifdef CONFIG_QUOTA
	.get_reserved_space = ext4_get_reserved_space,
#endif
	.write_dquot	= ext4_write_dquot,
	.acquire_dquot	= ext4_acquire_dquot,
	.release_dquot	= ext4_release_dquot,
	.mark_dirty	= ext4_mark_dquot_dirty,
	.write_info	= ext4_write_info,
	.alloc_dquot	= dquot_alloc,
	.destroy_dquot	= dquot_destroy,
};

static const struct quotactl_ops ext4_qctl_operations = {
	.quota_on	= ext4_quota_on,
	.quota_off	= ext4_quota_off,
	.quota_sync	= dquot_quota_sync,
	.get_info	= dquot_get_dqinfo,
	.set_info	= dquot_set_dqinfo,
	.get_dqblk	= dquot_get_dqblk,
	.set_dqblk	= dquot_set_dqblk
};
#endif

static const struct super_operations ext4_sops = {
	.alloc_inode	= ext4_alloc_inode,
	.destroy_inode	= ext4_destroy_inode,
	.write_inode	= ext4_write_inode,
	.dirty_inode	= ext4_dirty_inode,
	.evict_inode	= ext4_evict_inode,
	.put_super	= ext4_put_super,
	.sync_fs	= ext4_sync_fs,
	.freeze_fs	= ext4_freeze,
	.unfreeze_fs	= ext4_unfreeze,
	.statfs		= ext4_statfs,
	.remount_fs	= ext4_remount,
	.show_options	= ext4_show_options,
#ifdef CONFIG_QUOTA
	.quota_read	= ext4_quota_read,
	.quota_write	= ext4_quota_write,
#endif
	.bdev_try_to_free_page = bdev_try_to_free_page,
};

static const struct super_operations ext4_nojournal_sops = {
	.alloc_inode	= ext4_alloc_inode,
	.destroy_inode	= ext4_destroy_inode,
	.write_inode	= ext4_write_inode,
	.dirty_inode	= ext4_dirty_inode,
	.evict_inode	= ext4_evict_inode,
	.write_super	= ext4_write_super,
	.put_super	= ext4_put_super,
	.statfs		= ext4_statfs,
	.remount_fs	= ext4_remount,
	.show_options	= ext4_show_options,
#ifdef CONFIG_QUOTA
	.quota_read	= ext4_quota_read,
	.quota_write	= ext4_quota_write,
#endif
	.bdev_try_to_free_page = bdev_try_to_free_page,
};

static const struct export_operations ext4_export_ops = {
	.fh_to_dentry = ext4_fh_to_dentry,
	.fh_to_parent = ext4_fh_to_parent,
	.get_parent = ext4_get_parent,
};

enum {
	Opt_bsd_df, Opt_minix_df, Opt_grpid, Opt_nogrpid,
	Opt_resgid, Opt_resuid, Opt_sb, Opt_err_cont, Opt_err_panic, Opt_err_ro,
	Opt_nouid32, Opt_debug, Opt_oldalloc, Opt_orlov,
	Opt_user_xattr, Opt_nouser_xattr, Opt_acl, Opt_noacl,
	Opt_auto_da_alloc, Opt_noauto_da_alloc, Opt_noload, Opt_nobh, Opt_bh,
	Opt_commit, Opt_min_batch_time, Opt_max_batch_time,
	Opt_journal_update, Opt_journal_dev,
	Opt_journal_checksum, Opt_journal_async_commit,
	Opt_abort, Opt_data_journal, Opt_data_ordered, Opt_data_writeback,
	Opt_data_err_abort, Opt_data_err_ignore,
	Opt_usrjquota, Opt_grpjquota, Opt_offusrjquota, Opt_offgrpjquota,
	Opt_jqfmt_vfsold, Opt_jqfmt_vfsv0, Opt_jqfmt_vfsv1, Opt_quota,
	Opt_noquota, Opt_ignore, Opt_barrier, Opt_nobarrier, Opt_err,
	Opt_resize, Opt_usrquota, Opt_grpquota, Opt_i_version,
	Opt_stripe, Opt_delalloc, Opt_nodelalloc,
	Opt_block_validity, Opt_noblock_validity,
	Opt_inode_readahead_blks, Opt_journal_ioprio,
	Opt_dioread_nolock, Opt_dioread_lock,
	Opt_discard, Opt_nodiscard,
};

static const match_table_t tokens = {
	{Opt_bsd_df, "bsddf"},
	{Opt_minix_df, "minixdf"},
	{Opt_grpid, "grpid"},
	{Opt_grpid, "bsdgroups"},
	{Opt_nogrpid, "nogrpid"},
	{Opt_nogrpid, "sysvgroups"},
	{Opt_resgid, "resgid=%u"},
	{Opt_resuid, "resuid=%u"},
	{Opt_sb, "sb=%u"},
	{Opt_err_cont, "errors=continue"},
	{Opt_err_panic, "errors=panic"},
	{Opt_err_ro, "errors=remount-ro"},
	{Opt_nouid32, "nouid32"},
	{Opt_debug, "debug"},
	{Opt_oldalloc, "oldalloc"},
	{Opt_orlov, "orlov"},
	{Opt_user_xattr, "user_xattr"},
	{Opt_nouser_xattr, "nouser_xattr"},
	{Opt_acl, "acl"},
	{Opt_noacl, "noacl"},
	{Opt_noload, "noload"},
	{Opt_noload, "norecovery"},
	{Opt_nobh, "nobh"},
	{Opt_bh, "bh"},
	{Opt_commit, "commit=%u"},
	{Opt_min_batch_time, "min_batch_time=%u"},
	{Opt_max_batch_time, "max_batch_time=%u"},
	{Opt_journal_update, "journal=update"},
	{Opt_journal_dev, "journal_dev=%u"},
	{Opt_journal_checksum, "journal_checksum"},
	{Opt_journal_async_commit, "journal_async_commit"},
	{Opt_abort, "abort"},
	{Opt_data_journal, "data=journal"},
	{Opt_data_ordered, "data=ordered"},
	{Opt_data_writeback, "data=writeback"},
	{Opt_data_err_abort, "data_err=abort"},
	{Opt_data_err_ignore, "data_err=ignore"},
	{Opt_offusrjquota, "usrjquota="},
	{Opt_usrjquota, "usrjquota=%s"},
	{Opt_offgrpjquota, "grpjquota="},
	{Opt_grpjquota, "grpjquota=%s"},
	{Opt_jqfmt_vfsold, "jqfmt=vfsold"},
	{Opt_jqfmt_vfsv0, "jqfmt=vfsv0"},
	{Opt_jqfmt_vfsv1, "jqfmt=vfsv1"},
	{Opt_grpquota, "grpquota"},
	{Opt_noquota, "noquota"},
	{Opt_quota, "quota"},
	{Opt_usrquota, "usrquota"},
	{Opt_barrier, "barrier=%u"},
	{Opt_barrier, "barrier"},
	{Opt_nobarrier, "nobarrier"},
	{Opt_i_version, "i_version"},
	{Opt_stripe, "stripe=%u"},
	{Opt_resize, "resize"},
	{Opt_delalloc, "delalloc"},
	{Opt_nodelalloc, "nodelalloc"},
	{Opt_block_validity, "block_validity"},
	{Opt_noblock_validity, "noblock_validity"},
	{Opt_inode_readahead_blks, "inode_readahead_blks=%u"},
	{Opt_journal_ioprio, "journal_ioprio=%u"},
	{Opt_auto_da_alloc, "auto_da_alloc=%u"},
	{Opt_auto_da_alloc, "auto_da_alloc"},
	{Opt_noauto_da_alloc, "noauto_da_alloc"},
	{Opt_dioread_nolock, "dioread_nolock"},
	{Opt_dioread_lock, "dioread_lock"},
	{Opt_discard, "discard"},
	{Opt_nodiscard, "nodiscard"},
	{Opt_err, NULL},
};

static ext4_fsblk_t get_sb_block(void **data)
{
	ext4_fsblk_t	sb_block;
	char		*options = (char *) *data;

	if (!options || strncmp(options, "sb=", 3) != 0)
		return 1;	/* Default location */

	options += 3;
	/* TODO: use simple_strtoll with >32bit ext4 */
	sb_block = simple_strtoul(options, &options, 0);
	if (*options && *options != ',') {
		printk(KERN_ERR "EXT4-fs: Invalid sb specification: %s\n",
		       (char *) *data);
		return 1;
	}
	if (*options == ',')
		options++;
	*data = (void *) options;

	return sb_block;
}

#define DEFAULT_JOURNAL_IOPRIO (IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, 3))
static char deprecated_msg[] = "Mount option \"%s\" will be removed by %s\n"
	"Contact linux-ext4@vger.kernel.org if you think we should keep it.\n";

#ifdef CONFIG_QUOTA
static int set_qf_name(struct super_block *sb, int qtype, substring_t *args)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	char *qname;

	if (sb_any_quota_loaded(sb) &&
		!sbi->s_qf_names[qtype]) {
		ext4_msg(sb, KERN_ERR,
			"Cannot change journaled "
			"quota options when quota turned on");
		return 0;
	}
	qname = match_strdup(args);
	if (!qname) {
		ext4_msg(sb, KERN_ERR,
			"Not enough memory for storing quotafile name");
		return 0;
	}
	if (sbi->s_qf_names[qtype] &&
		strcmp(sbi->s_qf_names[qtype], qname)) {
		ext4_msg(sb, KERN_ERR,
			"%s quota file already specified", QTYPE2NAME(qtype));
		kfree(qname);
		return 0;
	}
	sbi->s_qf_names[qtype] = qname;
	if (strchr(sbi->s_qf_names[qtype], '/')) {
		ext4_msg(sb, KERN_ERR,
			"quotafile must be on filesystem root");
		kfree(sbi->s_qf_names[qtype]);
		sbi->s_qf_names[qtype] = NULL;
		return 0;
	}
	set_opt(sbi->s_mount_opt, QUOTA);
	return 1;
}

static int clear_qf_name(struct super_block *sb, int qtype)
{

	struct ext4_sb_info *sbi = EXT4_SB(sb);

	if (sb_any_quota_loaded(sb) &&
		sbi->s_qf_names[qtype]) {
		ext4_msg(sb, KERN_ERR, "Cannot change journaled quota options"
			" when quota turned on");
		return 0;
	}
	/*
	 * The space will be released later when all options are confirmed
	 * to be correct
	 */
	sbi->s_qf_names[qtype] = NULL;
	return 1;
}
#endif

static int parse_options(char *options, struct super_block *sb,
			 unsigned long *journal_devnum,
			 unsigned int *journal_ioprio,
			 ext4_fsblk_t *n_blocks_count, int is_remount)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int data_opt = 0;
	int option;
#ifdef CONFIG_QUOTA
	int qfmt;
#endif

	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		/*
		 * Initialize args struct so we know whether arg was
		 * found; some options take optional arguments.
		 */
		args[0].to = args[0].from = 0;
		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_bsd_df:
			ext4_msg(sb, KERN_WARNING, deprecated_msg, p, "2.6.38");
			clear_opt(sbi->s_mount_opt, MINIX_DF);
			break;
		case Opt_minix_df:
			ext4_msg(sb, KERN_WARNING, deprecated_msg, p, "2.6.38");
			set_opt(sbi->s_mount_opt, MINIX_DF);

			break;
		case Opt_grpid:
			ext4_msg(sb, KERN_WARNING, deprecated_msg, p, "2.6.38");
			set_opt(sbi->s_mount_opt, GRPID);

			break;
		case Opt_nogrpid:
			ext4_msg(sb, KERN_WARNING, deprecated_msg, p, "2.6.38");
			clear_opt(sbi->s_mount_opt, GRPID);

			break;
		case Opt_resuid:
			if (match_int(&args[0], &option))
				return 0;
			sbi->s_resuid = option;
			break;
		case Opt_resgid:
			if (match_int(&args[0], &option))
				return 0;
			sbi->s_resgid = option;
			break;
		case Opt_sb:
			/* handled by get_sb_block() instead of here */
			/* *sb_block = match_int(&args[0]); */
			break;
		case Opt_err_panic:
			clear_opt(sbi->s_mount_opt, ERRORS_CONT);
			clear_opt(sbi->s_mount_opt, ERRORS_RO);
			set_opt(sbi->s_mount_opt, ERRORS_PANIC);
			break;
		case Opt_err_ro:
			clear_opt(sbi->s_mount_opt, ERRORS_CONT);
			clear_opt(sbi->s_mount_opt, ERRORS_PANIC);
			set_opt(sbi->s_mount_opt, ERRORS_RO);
			break;
		case Opt_err_cont:
			clear_opt(sbi->s_mount_opt, ERRORS_RO);
			clear_opt(sbi->s_mount_opt, ERRORS_PANIC);
			set_opt(sbi->s_mount_opt, ERRORS_CONT);
			break;
		case Opt_nouid32:
			set_opt(sbi->s_mount_opt, NO_UID32);
			break;
		case Opt_debug:
			set_opt(sbi->s_mount_opt, DEBUG);
			break;
		case Opt_oldalloc:
			set_opt(sbi->s_mount_opt, OLDALLOC);
			break;
		case Opt_orlov:
			clear_opt(sbi->s_mount_opt, OLDALLOC);
			break;
#ifdef CONFIG_EXT4_FS_XATTR
		case Opt_user_xattr:
			set_opt(sbi->s_mount_opt, XATTR_USER);
			break;
		case Opt_nouser_xattr:
			clear_opt(sbi->s_mount_opt, XATTR_USER);
			break;
#else
		case Opt_user_xattr:
		case Opt_nouser_xattr:
			ext4_msg(sb, KERN_ERR, "(no)user_xattr options not supported");
			break;
#endif
#ifdef CONFIG_EXT4_FS_POSIX_ACL
		case Opt_acl:
			set_opt(sbi->s_mount_opt, POSIX_ACL);
			break;
		case Opt_noacl:
			clear_opt(sbi->s_mount_opt, POSIX_ACL);
			break;
#else
		case Opt_acl:
		case Opt_noacl:
			ext4_msg(sb, KERN_ERR, "(no)acl options not supported");
			break;
#endif
		case Opt_journal_update:
			/* @@@ FIXME */
			/* Eventually we will want to be able to create
			   a journal file here.  For now, only allow the
			   user to specify an existing inode to be the
			   journal file. */
			if (is_remount) {
				ext4_msg(sb, KERN_ERR,
					 "Cannot specify journal on remount");
				return 0;
			}
			set_opt(sbi->s_mount_opt, UPDATE_JOURNAL);
			break;
		case Opt_journal_dev:
			if (is_remount) {
				ext4_msg(sb, KERN_ERR,
					"Cannot specify journal on remount");
				return 0;
			}
			if (match_int(&args[0], &option))
				return 0;
			*journal_devnum = option;
			break;
		case Opt_journal_checksum:
			set_opt(sbi->s_mount_opt, JOURNAL_CHECKSUM);
			break;
		case Opt_journal_async_commit:
			set_opt(sbi->s_mount_opt, JOURNAL_ASYNC_COMMIT);
			set_opt(sbi->s_mount_opt, JOURNAL_CHECKSUM);
			break;
		case Opt_noload:
			set_opt(sbi->s_mount_opt, NOLOAD);
			break;
		case Opt_commit:
			if (match_int(&args[0], &option))
				return 0;
			if (option < 0)
				return 0;
			if (option == 0)
				option = JBD2_DEFAULT_MAX_COMMIT_AGE;
			sbi->s_commit_interval = HZ * option;
			break;
		case Opt_max_batch_time:
			if (match_int(&args[0], &option))
				return 0;
			if (option < 0)
				return 0;
			if (option == 0)
				option = EXT4_DEF_MAX_BATCH_TIME;
			sbi->s_max_batch_time = option;
			break;
		case Opt_min_batch_time:
			if (match_int(&args[0], &option))
				return 0;
			if (option < 0)
				return 0;
			sbi->s_min_batch_time = option;
			break;
		case Opt_data_journal:
			data_opt = EXT4_MOUNT_JOURNAL_DATA;
			goto datacheck;
		case Opt_data_ordered:
			data_opt = EXT4_MOUNT_ORDERED_DATA;
			goto datacheck;
		case Opt_data_writeback:
			data_opt = EXT4_MOUNT_WRITEBACK_DATA;
		datacheck:
			if (is_remount) {
				if (test_opt(sb, DATA_FLAGS) != data_opt) {
					ext4_msg(sb, KERN_ERR,
						"Cannot change data mode on remount");
					return 0;
				}
			} else {
				clear_opt(sbi->s_mount_opt, DATA_FLAGS);
				sbi->s_mount_opt |= data_opt;
			}
			break;
		case Opt_data_err_abort:
			set_opt(sbi->s_mount_opt, DATA_ERR_ABORT);
			break;
		case Opt_data_err_ignore:
			clear_opt(sbi->s_mount_opt, DATA_ERR_ABORT);
			break;
#ifdef CONFIG_QUOTA
		case Opt_usrjquota:
			if (!set_qf_name(sb, USRQUOTA, &args[0]))
				return 0;
			break;
		case Opt_grpjquota:
			if (!set_qf_name(sb, GRPQUOTA, &args[0]))
				return 0;
			break;
		case Opt_offusrjquota:
			if (!clear_qf_name(sb, USRQUOTA))
				return 0;
			break;
		case Opt_offgrpjquota:
			if (!clear_qf_name(sb, GRPQUOTA))
				return 0;
			break;

		case Opt_jqfmt_vfsold:
			qfmt = QFMT_VFS_OLD;
			goto set_qf_format;
		case Opt_jqfmt_vfsv0:
			qfmt = QFMT_VFS_V0;
			goto set_qf_format;
		case Opt_jqfmt_vfsv1:
			qfmt = QFMT_VFS_V1;
set_qf_format:
			if (sb_any_quota_loaded(sb) &&
			    sbi->s_jquota_fmt != qfmt) {
				ext4_msg(sb, KERN_ERR, "Cannot change "
					"journaled quota options when "
					"quota turned on");
				return 0;
			}
			sbi->s_jquota_fmt = qfmt;
			break;
		case Opt_quota:
		case Opt_usrquota:
			set_opt(sbi->s_mount_opt, QUOTA);
			set_opt(sbi->s_mount_opt, USRQUOTA);
			break;
		case Opt_grpquota:
			set_opt(sbi->s_mount_opt, QUOTA);
			set_opt(sbi->s_mount_opt, GRPQUOTA);
			break;
		case Opt_noquota:
			if (sb_any_quota_loaded(sb)) {
				ext4_msg(sb, KERN_ERR, "Cannot change quota "
					"options when quota turned on");
				return 0;
			}
			clear_opt(sbi->s_mount_opt, QUOTA);
			clear_opt(sbi->s_mount_opt, USRQUOTA);
			clear_opt(sbi->s_mount_opt, GRPQUOTA);
			break;
#else
		case Opt_quota:
		case Opt_usrquota:
		case Opt_grpquota:
			ext4_msg(sb, KERN_ERR,
				"quota options not supported");
			break;
		case Opt_usrjquota:
		case Opt_grpjquota:
		case Opt_offusrjquota:
		case Opt_offgrpjquota:
		case Opt_jqfmt_vfsold:
		case Opt_jqfmt_vfsv0:
		case Opt_jqfmt_vfsv1:
			ext4_msg(sb, KERN_ERR,
				"journaled quota options not supported");
			break;
		case Opt_noquota:
			break;
#endif
		case Opt_abort:
			sbi->s_mount_flags |= EXT4_MF_FS_ABORTED;
			break;
		case Opt_nobarrier:
			clear_opt(sbi->s_mount_opt, BARRIER);
			break;
		case Opt_barrier:
			if (args[0].from) {
				if (match_int(&args[0], &option))
					return 0;
			} else
				option = 1;	/* No argument, default to 1 */
			if (option)
				set_opt(sbi->s_mount_opt, BARRIER);
			else
				clear_opt(sbi->s_mount_opt, BARRIER);
			break;
		case Opt_ignore:
			break;
		case Opt_resize:
			if (!is_remount) {
				ext4_msg(sb, KERN_ERR,
					"resize option only available "
					"for remount");
				return 0;
			}
			if (match_int(&args[0], &option) != 0)
				return 0;
			*n_blocks_count = option;
			break;
		case Opt_nobh:
			ext4_msg(sb, KERN_WARNING,
				 "Ignoring deprecated nobh option");
			break;
		case Opt_bh:
			ext4_msg(sb, KERN_WARNING,
				 "Ignoring deprecated bh option");
			break;
		case Opt_i_version:
			set_opt(sbi->s_mount_opt, I_VERSION);
			sb->s_flags |= MS_I_VERSION;
			break;
		case Opt_nodelalloc:
			clear_opt(sbi->s_mount_opt, DELALLOC);
			break;
		case Opt_stripe:
			if (match_int(&args[0], &option))
				return 0;
			if (option < 0)
				return 0;
			sbi->s_stripe = option;
			break;
		case Opt_delalloc:
			set_opt(sbi->s_mount_opt, DELALLOC);
			break;
		case Opt_block_validity:
			set_opt(sbi->s_mount_opt, BLOCK_VALIDITY);
			break;
		case Opt_noblock_validity:
			clear_opt(sbi->s_mount_opt, BLOCK_VALIDITY);
			break;
		case Opt_inode_readahead_blks:
			if (match_int(&args[0], &option))
				return 0;
			if (option < 0 || option > (1 << 30))
				return 0;
			if (!is_power_of_2(option)) {
				ext4_msg(sb, KERN_ERR,
					 "EXT4-fs: inode_readahead_blks"
					 " must be a power of 2");
				return 0;
			}
			sbi->s_inode_readahead_blks = option;
			break;
		case Opt_journal_ioprio:
			if (match_int(&args[0], &option))
				return 0;
			if (option < 0 || option > 7)
				break;
			*journal_ioprio = IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE,
							    option);
			break;
		case Opt_noauto_da_alloc:
			set_opt(sbi->s_mount_opt,NO_AUTO_DA_ALLOC);
			break;
		case Opt_auto_da_alloc:
			if (args[0].from) {
				if (match_int(&args[0], &option))
					return 0;
			} else
				option = 1;	/* No argument, default to 1 */
			if (option)
				clear_opt(sbi->s_mount_opt, NO_AUTO_DA_ALLOC);
			else
				set_opt(sbi->s_mount_opt,NO_AUTO_DA_ALLOC);
			break;
		case Opt_discard:
			set_opt(sbi->s_mount_opt, DISCARD);
			break;
		case Opt_nodiscard:
			clear_opt(sbi->s_mount_opt, DISCARD);
			break;
		case Opt_dioread_nolock:
			set_opt(sbi->s_mount_opt, DIOREAD_NOLOCK);
			break;
		case Opt_dioread_lock:
			clear_opt(sbi->s_mount_opt, DIOREAD_NOLOCK);
			break;
		default:
			ext4_msg(sb, KERN_ERR,
			       "Unrecognized mount option \"%s\" "
			       "or missing value", p);
			return 0;
		}
	}
#ifdef CONFIG_QUOTA
	if (sbi->s_qf_names[USRQUOTA] || sbi->s_qf_names[GRPQUOTA]) {
		if (test_opt(sb, USRQUOTA) && sbi->s_qf_names[USRQUOTA])
			clear_opt(sbi->s_mount_opt, USRQUOTA);

		if (test_opt(sb, GRPQUOTA) && sbi->s_qf_names[GRPQUOTA])
			clear_opt(sbi->s_mount_opt, GRPQUOTA);

		if (test_opt(sb, GRPQUOTA) || test_opt(sb, USRQUOTA)) {
			ext4_msg(sb, KERN_ERR, "old and new quota "
					"format mixing");
			return 0;
		}

		if (!sbi->s_jquota_fmt) {
			ext4_msg(sb, KERN_ERR, "journaled quota format "
					"not specified");
			return 0;
		}
	} else {
		if (sbi->s_jquota_fmt) {
			ext4_msg(sb, KERN_ERR, "journaled quota format "
					"specified with no journaling "
					"enabled");
			return 0;
		}
	}
#endif
	return 1;
}

static int ext4_setup_super(struct super_block *sb, struct ext4_super_block *es,
			    int read_only)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	int res = 0;

	if (le32_to_cpu(es->s_rev_level) > EXT4_MAX_SUPP_REV) {
		ext4_msg(sb, KERN_ERR, "revision level too high, "
			 "forcing read-only mode");
		res = MS_RDONLY;
	}
	if (read_only)
		return res;
	if (!(sbi->s_mount_state & EXT4_VALID_FS))
		ext4_msg(sb, KERN_WARNING, "warning: mounting unchecked fs, "
			 "running e2fsck is recommended");
	else if ((sbi->s_mount_state & EXT4_ERROR_FS))
		ext4_msg(sb, KERN_WARNING,
			 "warning: mounting fs with errors, "
			 "running e2fsck is recommended");
	else if ((__s16) le16_to_cpu(es->s_max_mnt_count) >= 0 &&
		 le16_to_cpu(es->s_mnt_count) >=
		 (unsigned short) (__s16) le16_to_cpu(es->s_max_mnt_count))
		ext4_msg(sb, KERN_WARNING,
			 "warning: maximal mount count reached, "
			 "running e2fsck is recommended");
	else if (le32_to_cpu(es->s_checkinterval) &&
		(le32_to_cpu(es->s_lastcheck) +
			le32_to_cpu(es->s_checkinterval) <= get_seconds()))
		ext4_msg(sb, KERN_WARNING,
			 "warning: checktime reached, "
			 "running e2fsck is recommended");
	if (!sbi->s_journal)
		es->s_state &= cpu_to_le16(~EXT4_VALID_FS);
	if (!(__s16) le16_to_cpu(es->s_max_mnt_count))
		es->s_max_mnt_count = cpu_to_le16(EXT4_DFL_MAX_MNT_COUNT);
	le16_add_cpu(&es->s_mnt_count, 1);
	es->s_mtime = cpu_to_le32(get_seconds());
	ext4_update_dynamic_rev(sb);
	if (sbi->s_journal)
		EXT4_SET_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER);

	ext4_commit_super(sb, 1);
	if (test_opt(sb, DEBUG))
		printk(KERN_INFO "[EXT4 FS bs=%lu, gc=%u, "
				"bpg=%lu, ipg=%lu, mo=%04x]\n",
			sb->s_blocksize,
			sbi->s_groups_count,
			EXT4_BLOCKS_PER_GROUP(sb),
			EXT4_INODES_PER_GROUP(sb),
			sbi->s_mount_opt);

	return res;
}

static int ext4_fill_flex_info(struct super_block *sb)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_group_desc *gdp = NULL;
	ext4_group_t flex_group_count;
	ext4_group_t flex_group;
	int groups_per_flex = 0;
	size_t size;
	int i;

	sbi->s_log_groups_per_flex = sbi->s_es->s_log_groups_per_flex;
	groups_per_flex = 1 << sbi->s_log_groups_per_flex;

	if (groups_per_flex < 2) {
		sbi->s_log_groups_per_flex = 0;
		return 1;
	}

	/* We allocate both existing and potentially added groups */
	flex_group_count = ((sbi->s_groups_count + groups_per_flex - 1) +
			((le16_to_cpu(sbi->s_es->s_reserved_gdt_blocks) + 1) <<
			      EXT4_DESC_PER_BLOCK_BITS(sb))) / groups_per_flex;
	size = flex_group_count * sizeof(struct flex_groups);
	sbi->s_flex_groups = kzalloc(size, GFP_KERNEL);
	if (sbi->s_flex_groups == NULL) {
		sbi->s_flex_groups = vmalloc(size);
		if (sbi->s_flex_groups)
			memset(sbi->s_flex_groups, 0, size);
	}
	if (sbi->s_flex_groups == NULL) {
		ext4_msg(sb, KERN_ERR, "not enough memory for "
				"%u flex groups", flex_group_count);
		goto failed;
	}

	for (i = 0; i < sbi->s_groups_count; i++) {
		gdp = ext4_get_group_desc(sb, i, NULL);

		flex_group = ext4_flex_group(sbi, i);
		atomic_add(ext4_free_inodes_count(sb, gdp),
			   &sbi->s_flex_groups[flex_group].free_inodes);
		atomic_add(ext4_free_blks_count(sb, gdp),
			   &sbi->s_flex_groups[flex_group].free_blocks);
		atomic_add(ext4_used_dirs_count(sb, gdp),
			   &sbi->s_flex_groups[flex_group].used_dirs);
	}

	return 1;
failed:
	return 0;
}

__le16 ext4_group_desc_csum(struct ext4_sb_info *sbi, __u32 block_group,
			    struct ext4_group_desc *gdp)
{
	__u16 crc = 0;

	if (sbi->s_es->s_feature_ro_compat &
	    cpu_to_le32(EXT4_FEATURE_RO_COMPAT_GDT_CSUM)) {
		int offset = offsetof(struct ext4_group_desc, bg_checksum);
		__le32 le_group = cpu_to_le32(block_group);

		crc = crc16(~0, sbi->s_es->s_uuid, sizeof(sbi->s_es->s_uuid));
		crc = crc16(crc, (__u8 *)&le_group, sizeof(le_group));
		crc = crc16(crc, (__u8 *)gdp, offset);
		offset += sizeof(gdp->bg_checksum); /* skip checksum */
		/* for checksum of struct ext4_group_desc do the rest...*/
		if ((sbi->s_es->s_feature_incompat &
		     cpu_to_le32(EXT4_FEATURE_INCOMPAT_64BIT)) &&
		    offset < le16_to_cpu(sbi->s_es->s_desc_size))
			crc = crc16(crc, (__u8 *)gdp + offset,
				    le16_to_cpu(sbi->s_es->s_desc_size) -
					offset);
	}

	return cpu_to_le16(crc);
}

int ext4_group_desc_csum_verify(struct ext4_sb_info *sbi, __u32 block_group,
				struct ext4_group_desc *gdp)
{
	if ((sbi->s_es->s_feature_ro_compat &
	     cpu_to_le32(EXT4_FEATURE_RO_COMPAT_GDT_CSUM)) &&
	    (gdp->bg_checksum != ext4_group_desc_csum(sbi, block_group, gdp)))
		return 0;

	return 1;
}

/* Called at mount-time, super-block is locked */
static int ext4_check_descriptors(struct super_block *sb)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	ext4_fsblk_t first_block = le32_to_cpu(sbi->s_es->s_first_data_block);
	ext4_fsblk_t last_block;
	ext4_fsblk_t block_bitmap;
	ext4_fsblk_t inode_bitmap;
	ext4_fsblk_t inode_table;
	int flexbg_flag = 0;
	ext4_group_t i;

	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_FLEX_BG))
		flexbg_flag = 1;

	ext4_debug("Checking group descriptors");

	for (i = 0; i < sbi->s_groups_count; i++) {
		struct ext4_group_desc *gdp = ext4_get_group_desc(sb, i, NULL);

		if (i == sbi->s_groups_count - 1 || flexbg_flag)
			last_block = ext4_blocks_count(sbi->s_es) - 1;
		else
			last_block = first_block +
				(EXT4_BLOCKS_PER_GROUP(sb) - 1);

		block_bitmap = ext4_block_bitmap(sb, gdp);
		if (block_bitmap < first_block || block_bitmap > last_block) {
			ext4_msg(sb, KERN_ERR, "ext4_check_descriptors: "
			       "Block bitmap for group %u not in group "
			       "(block %llu)!", i, block_bitmap);
			return 0;
		}
		inode_bitmap = ext4_inode_bitmap(sb, gdp);
		if (inode_bitmap < first_block || inode_bitmap > last_block) {
			ext4_msg(sb, KERN_ERR, "ext4_check_descriptors: "
			       "Inode bitmap for group %u not in group "
			       "(block %llu)!", i, inode_bitmap);
			return 0;
		}
		inode_table = ext4_inode_table(sb, gdp);
		if (inode_table < first_block ||
		    inode_table + sbi->s_itb_per_group - 1 > last_block) {
			ext4_msg(sb, KERN_ERR, "ext4_check_descriptors: "
			       "Inode table for group %u not in group "
			       "(block %llu)!", i, inode_table);
			return 0;
		}
		ext4_lock_group(sb, i);
		if (!ext4_group_desc_csum_verify(sbi, i, gdp)) {
			ext4_msg(sb, KERN_ERR, "ext4_check_descriptors: "
				 "Checksum for group %u failed (%u!=%u)",
				 i, le16_to_cpu(ext4_group_desc_csum(sbi, i,
				     gdp)), le16_to_cpu(gdp->bg_checksum));
			if (!(sb->s_flags & MS_RDONLY)) {
				ext4_unlock_group(sb, i);
				return 0;
			}
		}
		ext4_unlock_group(sb, i);
		if (!flexbg_flag)
			first_block += EXT4_BLOCKS_PER_GROUP(sb);
	}

	ext4_free_blocks_count_set(sbi->s_es, ext4_count_free_blocks(sb));
	sbi->s_es->s_free_inodes_count =cpu_to_le32(ext4_count_free_inodes(sb));
	return 1;
}

/* ext4_orphan_cleanup() walks a singly-linked list of inodes (starting at
 * the superblock) which were deleted from all directories, but held open by
 * a process at the time of a crash.  We walk the list and try to delete these
 * inodes at recovery time (only with a read-write filesystem).
 *
 * In order to keep the orphan inode chain consistent during traversal (in
 * case of crash during recovery), we link each inode into the superblock
 * orphan list_head and handle it the same way as an inode deletion during
 * normal operation (which journals the operations for us).
 *
 * We only do an iget() and an iput() on each inode, which is very safe if we
 * accidentally point at an in-use or already deleted inode.  The worst that
 * can happen in this case is that we get a "bit already cleared" message from
 * ext4_free_inode().  The only reason we would point at a wrong inode is if
 * e2fsck was run on this filesystem, and it must have already done the orphan
 * inode cleanup for us, so we can safely abort without any further action.
 */
static void ext4_orphan_cleanup(struct super_block *sb,
				struct ext4_super_block *es)
{
	unsigned int s_flags = sb->s_flags;
	int nr_orphans = 0, nr_truncates = 0;
#ifdef CONFIG_QUOTA
	int i;
#endif
	if (!es->s_last_orphan) {
		jbd_debug(4, "no orphan inodes to clean up\n");
		return;
	}

	if (bdev_read_only(sb->s_bdev)) {
		ext4_msg(sb, KERN_ERR, "write access "
			"unavailable, skipping orphan cleanup");
		return;
	}

	if (EXT4_SB(sb)->s_mount_state & EXT4_ERROR_FS) {
		if (es->s_last_orphan)
			jbd_debug(1, "Errors on filesystem, "
				  "clearing orphan list.\n");
		es->s_last_orphan = 0;
		jbd_debug(1, "Skipping orphan recovery on fs with errors.\n");
		return;
	}

	if (s_flags & MS_RDONLY) {
		ext4_msg(sb, KERN_INFO, "orphan cleanup on readonly fs");
		sb->s_flags &= ~MS_RDONLY;
	}
#ifdef CONFIG_QUOTA
	/* Needed for iput() to work correctly and not trash data */
	sb->s_flags |= MS_ACTIVE;
	/* Turn on quotas so that they are updated correctly */
	for (i = 0; i < MAXQUOTAS; i++) {
		if (EXT4_SB(sb)->s_qf_names[i]) {
			int ret = ext4_quota_on_mount(sb, i);
			if (ret < 0)
				ext4_msg(sb, KERN_ERR,
					"Cannot turn on journaled "
					"quota: error %d", ret);
		}
	}
#endif

	while (es->s_last_orphan) {
		struct inode *inode;

		inode = ext4_orphan_get(sb, le32_to_cpu(es->s_last_orphan));
		if (IS_ERR(inode)) {
			es->s_last_orphan = 0;
			break;
		}

		list_add(&EXT4_I(inode)->i_orphan, &EXT4_SB(sb)->s_orphan);
		dquot_initialize(inode);
		if (inode->i_nlink) {
			ext4_msg(sb, KERN_DEBUG,
				"%s: truncating inode %lu to %lld bytes",
				__func__, inode->i_ino, inode->i_size);
			jbd_debug(2, "truncating inode %lu to %lld bytes\n",
				  inode->i_ino, inode->i_size);
			ext4_truncate(inode);
			nr_truncates++;
		} else {
			ext4_msg(sb, KERN_DEBUG,
				"%s: deleting unreferenced inode %lu",
				__func__, inode->i_ino);
			jbd_debug(2, "deleting unreferenced inode %lu\n",
				  inode->i_ino);
			nr_orphans++;
		}
		iput(inode);  /* The delete magic happens here! */
	}

#define PLURAL(x) (x), ((x) == 1) ? "" : "s"

	if (nr_orphans)
		ext4_msg(sb, KERN_INFO, "%d orphan inode%s deleted",
		       PLURAL(nr_orphans));
	if (nr_truncates)
		ext4_msg(sb, KERN_INFO, "%d truncate%s cleaned up",
		       PLURAL(nr_truncates));
#ifdef CONFIG_QUOTA
	/* Turn quotas off */
	for (i = 0; i < MAXQUOTAS; i++) {
		if (sb_dqopt(sb)->files[i])
			dquot_quota_off(sb, i);
	}
#endif
	sb->s_flags = s_flags; /* Restore MS_RDONLY status */
}

/*
 * Maximal extent format file size.
 * Resulting logical blkno at s_maxbytes must fit in our on-disk
 * extent format containers, within a sector_t, and within i_blocks
 * in the vfs.  ext4 inode has 48 bits of i_block in fsblock units,
 * so that won't be a limiting factor.
 *
 * Note, this does *not* consider any metadata overhead for vfs i_blocks.
 */
static loff_t ext4_max_size(int blkbits, int has_huge_files)
{
	loff_t res;
	loff_t upper_limit = MAX_LFS_FILESIZE;

	/* small i_blocks in vfs inode? */
	if (!has_huge_files || sizeof(blkcnt_t) < sizeof(u64)) {
		/*
		 * CONFIG_LBDAF is not enabled implies the inode
		 * i_block represent total blocks in 512 bytes
		 * 32 == size of vfs inode i_blocks * 8
		 */
		upper_limit = (1LL << 32) - 1;

		/* total blocks in file system block size */
		upper_limit >>= (blkbits - 9);
		upper_limit <<= blkbits;
	}

	/* 32-bit extent-start container, ee_block */
	res = 1LL << 32;
	res <<= blkbits;
	res -= 1;

	/* Sanity check against vm- & vfs- imposed limits */
	if (res > upper_limit)
		res = upper_limit;

	return res;
}

/*
 * Maximal bitmap file size.  There is a direct, and {,double-,triple-}indirect
 * block limit, and also a limit of (2^48 - 1) 512-byte sectors in i_blocks.
 * We need to be 1 filesystem block less than the 2^48 sector limit.
 */
static loff_t ext4_max_bitmap_size(int bits, int has_huge_files)
{
	loff_t res = EXT4_NDIR_BLOCKS;
	int meta_blocks;
	loff_t upper_limit;
	/* This is calculated to be the largest file size for a dense, block
	 * mapped file such that the file's total number of 512-byte sectors,
	 * including data and all indirect blocks, does not exceed (2^48 - 1).
	 *
	 * __u32 i_blocks_lo and _u16 i_blocks_high represent the total
	 * number of 512-byte sectors of the file.
	 */

	if (!has_huge_files || sizeof(blkcnt_t) < sizeof(u64)) {
		/*
		 * !has_huge_files or CONFIG_LBDAF not enabled implies that
		 * the inode i_block field represents total file blocks in
		 * 2^32 512-byte sectors == size of vfs inode i_blocks * 8
		 */
		upper_limit = (1LL << 32) - 1;

		/* total blocks in file system block size */
		upper_limit >>= (bits - 9);

	} else {
		/*
		 * We use 48 bit ext4_inode i_blocks
		 * With EXT4_HUGE_FILE_FL set the i_blocks
		 * represent total number of blocks in
		 * file system block size
		 */
		upper_limit = (1LL << 48) - 1;

	}

	/* indirect blocks */
	meta_blocks = 1;
	/* double indirect blocks */
	meta_blocks += 1 + (1LL << (bits-2));
	/* tripple indirect blocks */
	meta_blocks += 1 + (1LL << (bits-2)) + (1LL << (2*(bits-2)));

	upper_limit -= meta_blocks;
	upper_limit <<= bits;

	res += 1LL << (bits-2);
	res += 1LL << (2*(bits-2));
	res += 1LL << (3*(bits-2));
	res <<= bits;
	if (res > upper_limit)
		res = upper_limit;

	if (res > MAX_LFS_FILESIZE)
		res = MAX_LFS_FILESIZE;

	return res;
}

static ext4_fsblk_t descriptor_loc(struct super_block *sb,
				   ext4_fsblk_t logical_sb_block, int nr)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	ext4_group_t bg, first_meta_bg;
	int has_super = 0;

	first_meta_bg = le32_to_cpu(sbi->s_es->s_first_meta_bg);

	if (!EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_META_BG) ||
	    nr < first_meta_bg)
		return logical_sb_block + nr + 1;
	bg = sbi->s_desc_per_block * nr;
	if (ext4_bg_has_super(sb, bg))
		has_super = 1;

	return (has_super + ext4_group_first_block_no(sb, bg));
}

/**
 * ext4_get_stripe_size: Get the stripe size.
 * @sbi: In memory super block info
 *
 * If we have specified it via mount option, then
 * use the mount option value. If the value specified at mount time is
 * greater than the blocks per group use the super block value.
 * If the super block value is greater than blocks per group return 0.
 * Allocator needs it be less than blocks per group.
 *
 */
static unsigned long ext4_get_stripe_size(struct ext4_sb_info *sbi)
{
	unsigned long stride = le16_to_cpu(sbi->s_es->s_raid_stride);
	unsigned long stripe_width =
			le32_to_cpu(sbi->s_es->s_raid_stripe_width);

	if (sbi->s_stripe && sbi->s_stripe <= sbi->s_blocks_per_group)
		return sbi->s_stripe;

	if (stripe_width <= sbi->s_blocks_per_group)
		return stripe_width;

	if (stride <= sbi->s_blocks_per_group)
		return stride;

	return 0;
}

/* sysfs supprt */

struct ext4_attr {
	struct attribute attr;
	ssize_t (*show)(struct ext4_attr *, struct ext4_sb_info *, char *);
	ssize_t (*store)(struct ext4_attr *, struct ext4_sb_info *,
			 const char *, size_t);
	int offset;
};

static int parse_strtoul(const char *buf,
		unsigned long max, unsigned long *value)
{
	char *endp;

	*value = simple_strtoul(skip_spaces(buf), &endp, 0);
	endp = skip_spaces(endp);
	if (*endp || *value > max)
		return -EINVAL;

	return 0;
}

static ssize_t delayed_allocation_blocks_show(struct ext4_attr *a,
					      struct ext4_sb_info *sbi,
					      char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n",
			(s64) percpu_counter_sum(&sbi->s_dirtyblocks_counter));
}

static ssize_t session_write_kbytes_show(struct ext4_attr *a,
					 struct ext4_sb_info *sbi, char *buf)
{
	struct super_block *sb = sbi->s_buddy_cache->i_sb;

	if (!sb->s_bdev->bd_part)
		return snprintf(buf, PAGE_SIZE, "0\n");
	return snprintf(buf, PAGE_SIZE, "%lu\n",
			(part_stat_read(sb->s_bdev->bd_part, sectors[1]) -
			 sbi->s_sectors_written_start) >> 1);
}

static ssize_t lifetime_write_kbytes_show(struct ext4_attr *a,
					  struct ext4_sb_info *sbi, char *buf)
{
	struct super_block *sb = sbi->s_buddy_cache->i_sb;

	if (!sb->s_bdev->bd_part)
		return snprintf(buf, PAGE_SIZE, "0\n");
	return snprintf(buf, PAGE_SIZE, "%llu\n",
			(unsigned long long)(sbi->s_kbytes_written +
			((part_stat_read(sb->s_bdev->bd_part, sectors[1]) -
			  EXT4_SB(sb)->s_sectors_written_start) >> 1)));
}

static ssize_t inode_readahead_blks_store(struct ext4_attr *a,
					  struct ext4_sb_info *sbi,
					  const char *buf, size_t count)
{
	unsigned long t;

	if (parse_strtoul(buf, 0x40000000, &t))
		return -EINVAL;

	if (!is_power_of_2(t))
		return -EINVAL;

	sbi->s_inode_readahead_blks = t;
	return count;
}

static ssize_t sbi_ui_show(struct ext4_attr *a,
			   struct ext4_sb_info *sbi, char *buf)
{
	unsigned int *ui = (unsigned int *) (((char *) sbi) + a->offset);

	return snprintf(buf, PAGE_SIZE, "%u\n", *ui);
}

static ssize_t sbi_ui_store(struct ext4_attr *a,
			    struct ext4_sb_info *sbi,
			    const char *buf, size_t count)
{
	unsigned int *ui = (unsigned int *) (((char *) sbi) + a->offset);
	unsigned long t;

	if (parse_strtoul(buf, 0xffffffff, &t))
		return -EINVAL;
	*ui = t;
	return count;
}

#define EXT4_ATTR_OFFSET(_name,_mode,_show,_store,_elname) \
static struct ext4_attr ext4_attr_##_name = {			\
	.attr = {.name = __stringify(_name), .mode = _mode },	\
	.show	= _show,					\
	.store	= _store,					\
	.offset = offsetof(struct ext4_sb_info, _elname),	\
}
#define EXT4_ATTR(name, mode, show, store) \
static struct ext4_attr ext4_attr_##name = __ATTR(name, mode, show, store)

#define EXT4_RO_ATTR(name) EXT4_ATTR(name, 0444, name##_show, NULL)
#define EXT4_RW_ATTR(name) EXT4_ATTR(name, 0644, name##_show, name##_store)
#define EXT4_RW_ATTR_SBI_UI(name, elname)	\
	EXT4_ATTR_OFFSET(name, 0644, sbi_ui_show, sbi_ui_store, elname)
#define ATTR_LIST(name) &ext4_attr_##name.attr

EXT4_RO_ATTR(delayed_allocation_blocks);
EXT4_RO_ATTR(session_write_kbytes);
EXT4_RO_ATTR(lifetime_write_kbytes);
EXT4_ATTR_OFFSET(inode_readahead_blks, 0644, sbi_ui_show,
		 inode_readahead_blks_store, s_inode_readahead_blks);
EXT4_RW_ATTR_SBI_UI(inode_goal, s_inode_goal);
EXT4_RW_ATTR_SBI_UI(mb_stats, s_mb_stats);
EXT4_RW_ATTR_SBI_UI(mb_max_to_scan, s_mb_max_to_scan);
EXT4_RW_ATTR_SBI_UI(mb_min_to_scan, s_mb_min_to_scan);
EXT4_RW_ATTR_SBI_UI(mb_order2_req, s_mb_order2_reqs);
EXT4_RW_ATTR_SBI_UI(mb_stream_req, s_mb_stream_request);
EXT4_RW_ATTR_SBI_UI(mb_group_prealloc, s_mb_group_prealloc);
EXT4_RW_ATTR_SBI_UI(max_writeback_mb_bump, s_max_writeback_mb_bump);

static struct attribute *ext4_attrs[] = {
	ATTR_LIST(delayed_allocation_blocks),
	ATTR_LIST(session_write_kbytes),
	ATTR_LIST(lifetime_write_kbytes),
	ATTR_LIST(inode_readahead_blks),
	ATTR_LIST(inode_goal),
	ATTR_LIST(mb_stats),
	ATTR_LIST(mb_max_to_scan),
	ATTR_LIST(mb_min_to_scan),
	ATTR_LIST(mb_order2_req),
	ATTR_LIST(mb_stream_req),
	ATTR_LIST(mb_group_prealloc),
	ATTR_LIST(max_writeback_mb_bump),
	NULL,
};

static ssize_t ext4_attr_show(struct kobject *kobj,
			      struct attribute *attr, char *buf)
{
	struct ext4_sb_info *sbi = container_of(kobj, struct ext4_sb_info,
						s_kobj);
	struct ext4_attr *a = container_of(attr, struct ext4_attr, attr);

	return a->show ? a->show(a, sbi, buf) : 0;
}

static ssize_t ext4_attr_store(struct kobject *kobj,
			       struct attribute *attr,
			       const char *buf, size_t len)
{
	struct ext4_sb_info *sbi = container_of(kobj, struct ext4_sb_info,
						s_kobj);
	struct ext4_attr *a = container_of(attr, struct ext4_attr, attr);

	return a->store ? a->store(a, sbi, buf, len) : 0;
}

static void ext4_sb_release(struct kobject *kobj)
{
	struct ext4_sb_info *sbi = container_of(kobj, struct ext4_sb_info,
						s_kobj);
	complete(&sbi->s_kobj_unregister);
}


static const struct sysfs_ops ext4_attr_ops = {
	.show	= ext4_attr_show,
	.store	= ext4_attr_store,
};

static struct kobj_type ext4_ktype = {
	.default_attrs	= ext4_attrs,
	.sysfs_ops	= &ext4_attr_ops,
	.release	= ext4_sb_release,
};

/*
 * Check whether this filesystem can be mounted based on
 * the features present and the RDONLY/RDWR mount requested.
 * Returns 1 if this filesystem can be mounted as requested,
 * 0 if it cannot be.
 */
static int ext4_feature_set_ok(struct super_block *sb, int readonly)
{
	if (EXT4_HAS_INCOMPAT_FEATURE(sb, ~EXT4_FEATURE_INCOMPAT_SUPP)) {
		ext4_msg(sb, KERN_ERR,
			"Couldn't mount because of "
			"unsupported optional features (%x)",
			(le32_to_cpu(EXT4_SB(sb)->s_es->s_feature_incompat) &
			~EXT4_FEATURE_INCOMPAT_SUPP));
		return 0;
	}

	if (readonly)
		return 1;

	/* Check that feature set is OK for a read-write mount */
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb, ~EXT4_FEATURE_RO_COMPAT_SUPP)) {
		ext4_msg(sb, KERN_ERR, "couldn't mount RDWR because of "
			 "unsupported optional features (%x)",
			 (le32_to_cpu(EXT4_SB(sb)->s_es->s_feature_ro_compat) &
				~EXT4_FEATURE_RO_COMPAT_SUPP));
		return 0;
	}
	/*
	 * Large file size enabled file system can only be mounted
	 * read-write on 32-bit systems if kernel is built with CONFIG_LBDAF
	 */
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_HUGE_FILE)) {
		if (sizeof(blkcnt_t) < sizeof(u64)) {
			ext4_msg(sb, KERN_ERR, "Filesystem with huge files "
				 "cannot be mounted RDWR without "
				 "CONFIG_LBDAF");
			return 0;
		}
	}
	return 1;
}

/*
 * This function is called once a day if we have errors logged
 * on the file system
 */
static void print_daily_error_info(unsigned long arg)
{
	struct super_block *sb = (struct super_block *) arg;
	struct ext4_sb_info *sbi;
	struct ext4_super_block *es;

	sbi = EXT4_SB(sb);
	es = sbi->s_es;

	if (es->s_error_count)
		ext4_msg(sb, KERN_NOTICE, "error count: %u",
			 le32_to_cpu(es->s_error_count));
	if (es->s_first_error_time) {
		printk(KERN_NOTICE "EXT4-fs (%s): initial error at %u: %.*s:%d",
		       sb->s_id, le32_to_cpu(es->s_first_error_time),
		       (int) sizeof(es->s_first_error_func),
		       es->s_first_error_func,
		       le32_to_cpu(es->s_first_error_line));
		if (es->s_first_error_ino)
			printk(": inode %u",
			       le32_to_cpu(es->s_first_error_ino));
		if (es->s_first_error_block)
			printk(": block %llu", (unsigned long long)
			       le64_to_cpu(es->s_first_error_block));
		printk("\n");
	}
	if (es->s_last_error_time) {
		printk(KERN_NOTICE "EXT4-fs (%s): last error at %u: %.*s:%d",
		       sb->s_id, le32_to_cpu(es->s_last_error_time),
		       (int) sizeof(es->s_last_error_func),
		       es->s_last_error_func,
		       le32_to_cpu(es->s_last_error_line));
		if (es->s_last_error_ino)
			printk(": inode %u",
			       le32_to_cpu(es->s_last_error_ino));
		if (es->s_last_error_block)
			printk(": block %llu", (unsigned long long)
			       le64_to_cpu(es->s_last_error_block));
		printk("\n");
	}
	mod_timer(&sbi->s_err_report, jiffies + 24*60*60*HZ);  /* Once a day */
}

static int ext4_fill_super(struct super_block *sb, void *data, int silent)
				__releases(kernel_lock)
				__acquires(kernel_lock)
{
	char *orig_data = kstrdup(data, GFP_KERNEL);
	struct buffer_head *bh;
	struct ext4_super_block *es = NULL;
	struct ext4_sb_info *sbi;
	ext4_fsblk_t block;
	ext4_fsblk_t sb_block = get_sb_block(&data);
	ext4_fsblk_t logical_sb_block;
	unsigned long offset = 0;
	unsigned long journal_devnum = 0;
	unsigned long def_mount_opts;
	struct inode *root;
	char *cp;
	const char *descr;
	int ret = -ENOMEM;
	int blocksize;
	unsigned int db_count;
	unsigned int i;
	int needs_recovery, has_huge_files;
	__u64 blocks_count;
	int err;
	unsigned int journal_ioprio = DEFAULT_JOURNAL_IOPRIO;

	lock_kernel();

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		goto out_free_orig;

	sbi->s_blockgroup_lock =
		kzalloc(sizeof(struct blockgroup_lock), GFP_KERNEL);
	if (!sbi->s_blockgroup_lock) {
		kfree(sbi);
		goto out_free_orig;
	}
	sb->s_fs_info = sbi;
	sbi->s_mount_opt = 0;
	sbi->s_resuid = EXT4_DEF_RESUID;
	sbi->s_resgid = EXT4_DEF_RESGID;
	sbi->s_inode_readahead_blks = EXT4_DEF_INODE_READAHEAD_BLKS;
	sbi->s_sb_block = sb_block;
	if (sb->s_bdev->bd_part)
		sbi->s_sectors_written_start =
			part_stat_read(sb->s_bdev->bd_part, sectors[1]);

	unlock_kernel();

	/* Cleanup superblock name */
	for (cp = sb->s_id; (cp = strchr(cp, '/'));)
		*cp = '!';

	ret = -EINVAL;
	blocksize = sb_min_blocksize(sb, EXT4_MIN_BLOCK_SIZE);
	if (!blocksize) {
		ext4_msg(sb, KERN_ERR, "unable to set blocksize");
		goto out_fail;
	}

	/*
	 * The ext4 superblock will not be buffer aligned for other than 1kB
	 * block sizes.  We need to calculate the offset from buffer start.
	 */
	if (blocksize != EXT4_MIN_BLOCK_SIZE) {
		logical_sb_block = sb_block * EXT4_MIN_BLOCK_SIZE;
		offset = do_div(logical_sb_block, blocksize);
	} else {
		logical_sb_block = sb_block;
	}

	if (!(bh = sb_bread(sb, logical_sb_block))) {
		ext4_msg(sb, KERN_ERR, "unable to read superblock");
		goto out_fail;
	}
	/*
	 * Note: s_es must be initialized as soon as possible because
	 *       some ext4 macro-instructions depend on its value
	 */
	es = (struct ext4_super_block *) (((char *)bh->b_data) + offset);
	sbi->s_es = es;
	sb->s_magic = le16_to_cpu(es->s_magic);
	if (sb->s_magic != EXT4_SUPER_MAGIC)
		goto cantfind_ext4;
	sbi->s_kbytes_written = le64_to_cpu(es->s_kbytes_written);

	/* Set defaults before we parse the mount options */
	def_mount_opts = le32_to_cpu(es->s_default_mount_opts);
	if (def_mount_opts & EXT4_DEFM_DEBUG)
		set_opt(sbi->s_mount_opt, DEBUG);
	if (def_mount_opts & EXT4_DEFM_BSDGROUPS) {
		ext4_msg(sb, KERN_WARNING, deprecated_msg, "bsdgroups",
			"2.6.38");
		set_opt(sbi->s_mount_opt, GRPID);
	}
	if (def_mount_opts & EXT4_DEFM_UID16)
		set_opt(sbi->s_mount_opt, NO_UID32);
#ifdef CONFIG_EXT4_FS_XATTR
	if (def_mount_opts & EXT4_DEFM_XATTR_USER)
		set_opt(sbi->s_mount_opt, XATTR_USER);
#endif
#ifdef CONFIG_EXT4_FS_POSIX_ACL
	if (def_mount_opts & EXT4_DEFM_ACL)
		set_opt(sbi->s_mount_opt, POSIX_ACL);
#endif
	if ((def_mount_opts & EXT4_DEFM_JMODE) == EXT4_DEFM_JMODE_DATA)
		set_opt(sbi->s_mount_opt, JOURNAL_DATA);
	else if ((def_mount_opts & EXT4_DEFM_JMODE) == EXT4_DEFM_JMODE_ORDERED)
		set_opt(sbi->s_mount_opt, ORDERED_DATA);
	else if ((def_mount_opts & EXT4_DEFM_JMODE) == EXT4_DEFM_JMODE_WBACK)
		set_opt(sbi->s_mount_opt, WRITEBACK_DATA);

	if (le16_to_cpu(sbi->s_es->s_errors) == EXT4_ERRORS_PANIC)
		set_opt(sbi->s_mount_opt, ERRORS_PANIC);
	else if (le16_to_cpu(sbi->s_es->s_errors) == EXT4_ERRORS_CONTINUE)
		set_opt(sbi->s_mount_opt, ERRORS_CONT);
	else
		set_opt(sbi->s_mount_opt, ERRORS_RO);
	if (def_mount_opts & EXT4_DEFM_BLOCK_VALIDITY)
		set_opt(sbi->s_mount_opt, BLOCK_VALIDITY);
	if (def_mount_opts & EXT4_DEFM_DISCARD)
		set_opt(sbi->s_mount_opt, DISCARD);

	sbi->s_resuid = le16_to_cpu(es->s_def_resuid);
	sbi->s_resgid = le16_to_cpu(es->s_def_resgid);
	sbi->s_commit_interval = JBD2_DEFAULT_MAX_COMMIT_AGE * HZ;
	sbi->s_min_batch_time = EXT4_DEF_MIN_BATCH_TIME;
	sbi->s_max_batch_time = EXT4_DEF_MAX_BATCH_TIME;

	if ((def_mount_opts & EXT4_DEFM_NOBARRIER) == 0)
		set_opt(sbi->s_mount_opt, BARRIER);

	/*
	 * enable delayed allocation by default
	 * Use -o nodelalloc to turn it off
	 */
	if (!IS_EXT3_SB(sb) &&
	    ((def_mount_opts & EXT4_DEFM_NODELALLOC) == 0))
		set_opt(sbi->s_mount_opt, DELALLOC);

	if (!parse_options((char *) sbi->s_es->s_mount_opts, sb,
			   &journal_devnum, &journal_ioprio, NULL, 0)) {
		ext4_msg(sb, KERN_WARNING,
			 "failed to parse options in superblock: %s",
			 sbi->s_es->s_mount_opts);
	}
	if (!parse_options((char *) data, sb, &journal_devnum,
			   &journal_ioprio, NULL, 0))
		goto failed_mount;

	sb->s_flags = (sb->s_flags & ~MS_POSIXACL) |
		(test_opt(sb, POSIX_ACL) ? MS_POSIXACL : 0);

	if (le32_to_cpu(es->s_rev_level) == EXT4_GOOD_OLD_REV &&
	    (EXT4_HAS_COMPAT_FEATURE(sb, ~0U) ||
	     EXT4_HAS_RO_COMPAT_FEATURE(sb, ~0U) ||
	     EXT4_HAS_INCOMPAT_FEATURE(sb, ~0U)))
		ext4_msg(sb, KERN_WARNING,
		       "feature flags set on rev 0 fs, "
		       "running e2fsck is recommended");

	/*
	 * Check feature flags regardless of the revision level, since we
	 * previously didn't change the revision level when setting the flags,
	 * so there is a chance incompat flags are set on a rev 0 filesystem.
	 */
	if (!ext4_feature_set_ok(sb, (sb->s_flags & MS_RDONLY)))
		goto failed_mount;

	blocksize = BLOCK_SIZE << le32_to_cpu(es->s_log_block_size);

	if (blocksize < EXT4_MIN_BLOCK_SIZE ||
	    blocksize > EXT4_MAX_BLOCK_SIZE) {
		ext4_msg(sb, KERN_ERR,
		       "Unsupported filesystem blocksize %d", blocksize);
		goto failed_mount;
	}

	if (sb->s_blocksize != blocksize) {
		/* Validate the filesystem blocksize */
		if (!sb_set_blocksize(sb, blocksize)) {
			ext4_msg(sb, KERN_ERR, "bad block size %d",
					blocksize);
			goto failed_mount;
		}

		brelse(bh);
		logical_sb_block = sb_block * EXT4_MIN_BLOCK_SIZE;
		offset = do_div(logical_sb_block, blocksize);
		bh = sb_bread(sb, logical_sb_block);
		if (!bh) {
			ext4_msg(sb, KERN_ERR,
			       "Can't read superblock on 2nd try");
			goto failed_mount;
		}
		es = (struct ext4_super_block *)(((char *)bh->b_data) + offset);
		sbi->s_es = es;
		if (es->s_magic != cpu_to_le16(EXT4_SUPER_MAGIC)) {
			ext4_msg(sb, KERN_ERR,
			       "Magic mismatch, very weird!");
			goto failed_mount;
		}
	}

	has_huge_files = EXT4_HAS_RO_COMPAT_FEATURE(sb,
				EXT4_FEATURE_RO_COMPAT_HUGE_FILE);
	sbi->s_bitmap_maxbytes = ext4_max_bitmap_size(sb->s_blocksize_bits,
						      has_huge_files);
	sb->s_maxbytes = ext4_max_size(sb->s_blocksize_bits, has_huge_files);

	if (le32_to_cpu(es->s_rev_level) == EXT4_GOOD_OLD_REV) {
		sbi->s_inode_size = EXT4_GOOD_OLD_INODE_SIZE;
		sbi->s_first_ino = EXT4_GOOD_OLD_FIRST_INO;
	} else {
		sbi->s_inode_size = le16_to_cpu(es->s_inode_size);
		sbi->s_first_ino = le32_to_cpu(es->s_first_ino);
		if ((sbi->s_inode_size < EXT4_GOOD_OLD_INODE_SIZE) ||
		    (!is_power_of_2(sbi->s_inode_size)) ||
		    (sbi->s_inode_size > blocksize)) {
			ext4_msg(sb, KERN_ERR,
			       "unsupported inode size: %d",
			       sbi->s_inode_size);
			goto failed_mount;
		}
		if (sbi->s_inode_size > EXT4_GOOD_OLD_INODE_SIZE)
			sb->s_time_gran = 1 << (EXT4_EPOCH_BITS - 2);
	}

	sbi->s_desc_size = le16_to_cpu(es->s_desc_size);
	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_64BIT)) {
		if (sbi->s_desc_size < EXT4_MIN_DESC_SIZE_64BIT ||
		    sbi->s_desc_size > EXT4_MAX_DESC_SIZE ||
		    !is_power_of_2(sbi->s_desc_size)) {
			ext4_msg(sb, KERN_ERR,
			       "unsupported descriptor size %lu",
			       sbi->s_desc_size);
			goto failed_mount;
		}
	} else
		sbi->s_desc_size = EXT4_MIN_DESC_SIZE;

	sbi->s_blocks_per_group = le32_to_cpu(es->s_blocks_per_group);
	sbi->s_inodes_per_group = le32_to_cpu(es->s_inodes_per_group);
	if (EXT4_INODE_SIZE(sb) == 0 || EXT4_INODES_PER_GROUP(sb) == 0)
		goto cantfind_ext4;

	sbi->s_inodes_per_block = blocksize / EXT4_INODE_SIZE(sb);
	if (sbi->s_inodes_per_block == 0)
		goto cantfind_ext4;
	sbi->s_itb_per_group = sbi->s_inodes_per_group /
					sbi->s_inodes_per_block;
	sbi->s_desc_per_block = blocksize / EXT4_DESC_SIZE(sb);
	sbi->s_sbh = bh;
	sbi->s_mount_state = le16_to_cpu(es->s_state);
	sbi->s_addr_per_block_bits = ilog2(EXT4_ADDR_PER_BLOCK(sb));
	sbi->s_desc_per_block_bits = ilog2(EXT4_DESC_PER_BLOCK(sb));

	for (i = 0; i < 4; i++)
		sbi->s_hash_seed[i] = le32_to_cpu(es->s_hash_seed[i]);
	sbi->s_def_hash_version = es->s_def_hash_version;
	i = le32_to_cpu(es->s_flags);
	if (i & EXT2_FLAGS_UNSIGNED_HASH)
		sbi->s_hash_unsigned = 3;
	else if ((i & EXT2_FLAGS_SIGNED_HASH) == 0) {
#ifdef __CHAR_UNSIGNED__
		es->s_flags |= cpu_to_le32(EXT2_FLAGS_UNSIGNED_HASH);
		sbi->s_hash_unsigned = 3;
#else
		es->s_flags |= cpu_to_le32(EXT2_FLAGS_SIGNED_HASH);
#endif
		sb->s_dirt = 1;
	}

	if (sbi->s_blocks_per_group > blocksize * 8) {
		ext4_msg(sb, KERN_ERR,
		       "#blocks per group too big: %lu",
		       sbi->s_blocks_per_group);
		goto failed_mount;
	}
	if (sbi->s_inodes_per_group > blocksize * 8) {
		ext4_msg(sb, KERN_ERR,
		       "#inodes per group too big: %lu",
		       sbi->s_inodes_per_group);
		goto failed_mount;
	}

	/*
	 * Test whether we have more sectors than will fit in sector_t,
	 * and whether the max offset is addressable by the page cache.
	 */
	if ((ext4_blocks_count(es) >
	     (sector_t)(~0ULL) >> (sb->s_blocksize_bits - 9)) ||
	    (ext4_blocks_count(es) >
	     (pgoff_t)(~0ULL) >> (PAGE_CACHE_SHIFT - sb->s_blocksize_bits))) {
		ext4_msg(sb, KERN_ERR, "filesystem"
			 " too large to mount safely on this system");
		if (sizeof(sector_t) < 8)
			ext4_msg(sb, KERN_WARNING, "CONFIG_LBDAF not enabled");
		ret = -EFBIG;
		goto failed_mount;
	}

	if (EXT4_BLOCKS_PER_GROUP(sb) == 0)
		goto cantfind_ext4;

	/* check blocks count against device size */
	blocks_count = sb->s_bdev->bd_inode->i_size >> sb->s_blocksize_bits;
	if (blocks_count && ext4_blocks_count(es) > blocks_count) {
		ext4_msg(sb, KERN_WARNING, "bad geometry: block count %llu "
		       "exceeds size of device (%llu blocks)",
		       ext4_blocks_count(es), blocks_count);
		goto failed_mount;
	}

	/*
	 * It makes no sense for the first data block to be beyond the end
	 * of the filesystem.
	 */
	if (le32_to_cpu(es->s_first_data_block) >= ext4_blocks_count(es)) {
                ext4_msg(sb, KERN_WARNING, "bad geometry: first data"
			 "block %u is beyond end of filesystem (%llu)",
			 le32_to_cpu(es->s_first_data_block),
			 ext4_blocks_count(es));
		goto failed_mount;
	}
	blocks_count = (ext4_blocks_count(es) -
			le32_to_cpu(es->s_first_data_block) +
			EXT4_BLOCKS_PER_GROUP(sb) - 1);
	do_div(blocks_count, EXT4_BLOCKS_PER_GROUP(sb));
	if (blocks_count > ((uint64_t)1<<32) - EXT4_DESC_PER_BLOCK(sb)) {
		ext4_msg(sb, KERN_WARNING, "groups count too large: %u "
		       "(block count %llu, first data block %u, "
		       "blocks per group %lu)", sbi->s_groups_count,
		       ext4_blocks_count(es),
		       le32_to_cpu(es->s_first_data_block),
		       EXT4_BLOCKS_PER_GROUP(sb));
		goto failed_mount;
	}
	sbi->s_groups_count = blocks_count;
	sbi->s_blockfile_groups = min_t(ext4_group_t, sbi->s_groups_count,
			(EXT4_MAX_BLOCK_FILE_PHYS / EXT4_BLOCKS_PER_GROUP(sb)));
	db_count = (sbi->s_groups_count + EXT4_DESC_PER_BLOCK(sb) - 1) /
		   EXT4_DESC_PER_BLOCK(sb);
	sbi->s_group_desc = kmalloc(db_count * sizeof(struct buffer_head *),
				    GFP_KERNEL);
	if (sbi->s_group_desc == NULL) {
		ext4_msg(sb, KERN_ERR, "not enough memory");
		goto failed_mount;
	}

#ifdef CONFIG_PROC_FS
	if (ext4_proc_root)
		sbi->s_proc = proc_mkdir(sb->s_id, ext4_proc_root);
#endif

	bgl_lock_init(sbi->s_blockgroup_lock);

	for (i = 0; i < db_count; i++) {
		block = descriptor_loc(sb, logical_sb_block, i);
		sbi->s_group_desc[i] = sb_bread(sb, block);
		if (!sbi->s_group_desc[i]) {
			ext4_msg(sb, KERN_ERR,
			       "can't read group descriptor %d", i);
			db_count = i;
			goto failed_mount2;
		}
	}
	if (!ext4_check_descriptors(sb)) {
		ext4_msg(sb, KERN_ERR, "group descriptors corrupted!");
		goto failed_mount2;
	}
	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_FLEX_BG))
		if (!ext4_fill_flex_info(sb)) {
			ext4_msg(sb, KERN_ERR,
			       "unable to initialize "
			       "flex_bg meta info!");
			goto failed_mount2;
		}

	sbi->s_gdb_count = db_count;
	get_random_bytes(&sbi->s_next_generation, sizeof(u32));
	spin_lock_init(&sbi->s_next_gen_lock);

	sbi->s_stripe = ext4_get_stripe_size(sbi);
	sbi->s_max_writeback_mb_bump = 128;

	/*
	 * set up enough so that it can read an inode
	 */
	if (!test_opt(sb, NOLOAD) &&
	    EXT4_HAS_COMPAT_FEATURE(sb, EXT4_FEATURE_COMPAT_HAS_JOURNAL))
		sb->s_op = &ext4_sops;
	else
		sb->s_op = &ext4_nojournal_sops;
	sb->s_export_op = &ext4_export_ops;
	sb->s_xattr = ext4_xattr_handlers;
#ifdef CONFIG_QUOTA
	sb->s_qcop = &ext4_qctl_operations;
	sb->dq_op = &ext4_quota_operations;
#endif
	INIT_LIST_HEAD(&sbi->s_orphan); /* unlinked but open files */
	mutex_init(&sbi->s_orphan_lock);
	mutex_init(&sbi->s_resize_lock);

	sb->s_root = NULL;

	needs_recovery = (es->s_last_orphan != 0 ||
			  EXT4_HAS_INCOMPAT_FEATURE(sb,
				    EXT4_FEATURE_INCOMPAT_RECOVER));

	/*
	 * The first inode we look at is the journal inode.  Don't try
	 * root first: it may be modified in the journal!
	 */
	if (!test_opt(sb, NOLOAD) &&
	    EXT4_HAS_COMPAT_FEATURE(sb, EXT4_FEATURE_COMPAT_HAS_JOURNAL)) {
		if (ext4_load_journal(sb, es, journal_devnum))
			goto failed_mount3;
	} else if (test_opt(sb, NOLOAD) && !(sb->s_flags & MS_RDONLY) &&
	      EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER)) {
		ext4_msg(sb, KERN_ERR, "required journal recovery "
		       "suppressed and not mounted read-only");
		goto failed_mount_wq;
	} else {
		clear_opt(sbi->s_mount_opt, DATA_FLAGS);
		set_opt(sbi->s_mount_opt, WRITEBACK_DATA);
		sbi->s_journal = NULL;
		needs_recovery = 0;
		goto no_journal;
	}

	if (ext4_blocks_count(es) > 0xffffffffULL &&
	    !jbd2_journal_set_features(EXT4_SB(sb)->s_journal, 0, 0,
				       JBD2_FEATURE_INCOMPAT_64BIT)) {
		ext4_msg(sb, KERN_ERR, "Failed to set 64-bit journal feature");
		goto failed_mount_wq;
	}

	if (test_opt(sb, JOURNAL_ASYNC_COMMIT)) {
		jbd2_journal_set_features(sbi->s_journal,
				JBD2_FEATURE_COMPAT_CHECKSUM, 0,
				JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT);
	} else if (test_opt(sb, JOURNAL_CHECKSUM)) {
		jbd2_journal_set_features(sbi->s_journal,
				JBD2_FEATURE_COMPAT_CHECKSUM, 0, 0);
		jbd2_journal_clear_features(sbi->s_journal, 0, 0,
				JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT);
	} else {
		jbd2_journal_clear_features(sbi->s_journal,
				JBD2_FEATURE_COMPAT_CHECKSUM, 0,
				JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT);
	}

	/* We have now updated the journal if required, so we can
	 * validate the data journaling mode. */
	switch (test_opt(sb, DATA_FLAGS)) {
	case 0:
		/* No mode set, assume a default based on the journal
		 * capabilities: ORDERED_DATA if the journal can
		 * cope, else JOURNAL_DATA
		 */
		if (jbd2_journal_check_available_features
		    (sbi->s_journal, 0, 0, JBD2_FEATURE_INCOMPAT_REVOKE))
			set_opt(sbi->s_mount_opt, ORDERED_DATA);
		else
			set_opt(sbi->s_mount_opt, JOURNAL_DATA);
		break;

	case EXT4_MOUNT_ORDERED_DATA:
	case EXT4_MOUNT_WRITEBACK_DATA:
		if (!jbd2_journal_check_available_features
		    (sbi->s_journal, 0, 0, JBD2_FEATURE_INCOMPAT_REVOKE)) {
			ext4_msg(sb, KERN_ERR, "Journal does not support "
			       "requested data journaling mode");
			goto failed_mount_wq;
		}
	default:
		break;
	}
	set_task_ioprio(sbi->s_journal->j_task, journal_ioprio);

no_journal:
	err = percpu_counter_init(&sbi->s_freeblocks_counter,
				  ext4_count_free_blocks(sb));
	if (!err)
		err = percpu_counter_init(&sbi->s_freeinodes_counter,
					  ext4_count_free_inodes(sb));
	if (!err)
		err = percpu_counter_init(&sbi->s_dirs_counter,
					  ext4_count_dirs(sb));
	if (!err)
		err = percpu_counter_init(&sbi->s_dirtyblocks_counter, 0);
	if (err) {
		ext4_msg(sb, KERN_ERR, "insufficient memory");
		goto failed_mount_wq;
	}

	EXT4_SB(sb)->dio_unwritten_wq = create_workqueue("ext4-dio-unwritten");
	if (!EXT4_SB(sb)->dio_unwritten_wq) {
		printk(KERN_ERR "EXT4-fs: failed to create DIO workqueue\n");
		goto failed_mount_wq;
	}

	/*
	 * The jbd2_journal_load will have done any necessary log recovery,
	 * so we can safely mount the rest of the filesystem now.
	 */

	root = ext4_iget(sb, EXT4_ROOT_INO);
	if (IS_ERR(root)) {
		ext4_msg(sb, KERN_ERR, "get root inode failed");
		ret = PTR_ERR(root);
		goto failed_mount4;
	}
	if (!S_ISDIR(root->i_mode) || !root->i_blocks || !root->i_size) {
		iput(root);
		ext4_msg(sb, KERN_ERR, "corrupt root inode, run e2fsck");
		goto failed_mount4;
	}
	sb->s_root = d_alloc_root(root);
	if (!sb->s_root) {
		ext4_msg(sb, KERN_ERR, "get root dentry failed");
		iput(root);
		ret = -ENOMEM;
		goto failed_mount4;
	}

	ext4_setup_super(sb, es, sb->s_flags & MS_RDONLY);

	/* determine the minimum size of new large inodes, if present */
	if (sbi->s_inode_size > EXT4_GOOD_OLD_INODE_SIZE) {
		sbi->s_want_extra_isize = sizeof(struct ext4_inode) -
						     EXT4_GOOD_OLD_INODE_SIZE;
		if (EXT4_HAS_RO_COMPAT_FEATURE(sb,
				       EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE)) {
			if (sbi->s_want_extra_isize <
			    le16_to_cpu(es->s_want_extra_isize))
				sbi->s_want_extra_isize =
					le16_to_cpu(es->s_want_extra_isize);
			if (sbi->s_want_extra_isize <
			    le16_to_cpu(es->s_min_extra_isize))
				sbi->s_want_extra_isize =
					le16_to_cpu(es->s_min_extra_isize);
		}
	}
	/* Check if enough inode space is available */
	if (EXT4_GOOD_OLD_INODE_SIZE + sbi->s_want_extra_isize >
							sbi->s_inode_size) {
		sbi->s_want_extra_isize = sizeof(struct ext4_inode) -
						       EXT4_GOOD_OLD_INODE_SIZE;
		ext4_msg(sb, KERN_INFO, "required extra inode space not"
			 "available");
	}

	if (test_opt(sb, DELALLOC) &&
	    (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_JOURNAL_DATA)) {
		ext4_msg(sb, KERN_WARNING, "Ignoring delalloc option - "
			 "requested data journaling mode");
		clear_opt(sbi->s_mount_opt, DELALLOC);
	}
	if (test_opt(sb, DIOREAD_NOLOCK)) {
		if (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_JOURNAL_DATA) {
			ext4_msg(sb, KERN_WARNING, "Ignoring dioread_nolock "
				"option - requested data journaling mode");
			clear_opt(sbi->s_mount_opt, DIOREAD_NOLOCK);
		}
		if (sb->s_blocksize < PAGE_SIZE) {
			ext4_msg(sb, KERN_WARNING, "Ignoring dioread_nolock "
				"option - block size is too small");
			clear_opt(sbi->s_mount_opt, DIOREAD_NOLOCK);
		}
	}

	err = ext4_setup_system_zone(sb);
	if (err) {
		ext4_msg(sb, KERN_ERR, "failed to initialize system "
			 "zone (%d)", err);
		goto failed_mount4;
	}

	ext4_ext_init(sb);
	err = ext4_mb_init(sb, needs_recovery);
	if (err) {
		ext4_msg(sb, KERN_ERR, "failed to initialize mballoc (%d)",
			 err);
		goto failed_mount4;
	}

	sbi->s_kobj.kset = ext4_kset;
	init_completion(&sbi->s_kobj_unregister);
	err = kobject_init_and_add(&sbi->s_kobj, &ext4_ktype, NULL,
				   "%s", sb->s_id);
	if (err) {
		ext4_mb_release(sb);
		ext4_ext_release(sb);
		goto failed_mount4;
	};

	EXT4_SB(sb)->s_mount_state |= EXT4_ORPHAN_FS;
	ext4_orphan_cleanup(sb, es);
	EXT4_SB(sb)->s_mount_state &= ~EXT4_ORPHAN_FS;
	if (needs_recovery) {
		ext4_msg(sb, KERN_INFO, "recovery complete");
		ext4_mark_recovery_complete(sb, es);
	}
	if (EXT4_SB(sb)->s_journal) {
		if (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_JOURNAL_DATA)
			descr = " journalled data mode";
		else if (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_ORDERED_DATA)
			descr = " ordered data mode";
		else
			descr = " writeback data mode";
	} else
		descr = "out journal";

	ext4_msg(sb, KERN_INFO, "mounted filesystem with%s. "
		 "Opts: %s%s%s", descr, sbi->s_es->s_mount_opts,
		 *sbi->s_es->s_mount_opts ? "; " : "", orig_data);

	init_timer(&sbi->s_err_report);
	sbi->s_err_report.function = print_daily_error_info;
	sbi->s_err_report.data = (unsigned long) sb;
	if (es->s_error_count)
		mod_timer(&sbi->s_err_report, jiffies + 300*HZ); /* 5 minutes */

	kfree(orig_data);
	return 0;

cantfind_ext4:
	if (!silent)
		ext4_msg(sb, KERN_ERR, "VFS: Can't find ext4 filesystem");
	goto failed_mount;

failed_mount4:
	ext4_msg(sb, KERN_ERR, "mount failed");
	destroy_workqueue(EXT4_SB(sb)->dio_unwritten_wq);
failed_mount_wq:
	ext4_release_system_zone(sb);
	if (sbi->s_journal) {
		jbd2_journal_destroy(sbi->s_journal);
		sbi->s_journal = NULL;
	}
	percpu_counter_destroy(&sbi->s_freeblocks_counter);
	percpu_counter_destroy(&sbi->s_freeinodes_counter);
	percpu_counter_destroy(&sbi->s_dirs_counter);
	percpu_counter_destroy(&sbi->s_dirtyblocks_counter);
failed_mount3:
	if (sbi->s_flex_groups) {
		if (is_vmalloc_addr(sbi->s_flex_groups))
			vfree(sbi->s_flex_groups);
		else
			kfree(sbi->s_flex_groups);
	}
failed_mount2:
	for (i = 0; i < db_count; i++)
		brelse(sbi->s_group_desc[i]);
	kfree(sbi->s_group_desc);
failed_mount:
	if (sbi->s_proc) {
		remove_proc_entry(sb->s_id, ext4_proc_root);
	}
#ifdef CONFIG_QUOTA
	for (i = 0; i < MAXQUOTAS; i++)
		kfree(sbi->s_qf_names[i]);
#endif
	ext4_blkdev_remove(sbi);
	brelse(bh);
out_fail:
	sb->s_fs_info = NULL;
	kfree(sbi->s_blockgroup_lock);
	kfree(sbi);
	kfree(orig_data);
	return ret;

out_free_orig:
	unlock_kernel();
	kfree(orig_data);
	return ret;
}

/*
 * Setup any per-fs journal parameters now.  We'll do this both on
 * initial mount, once the journal has been initialised but before we've
 * done any recovery; and again on any subsequent remount.
 */
static void ext4_init_journal_params(struct super_block *sb, journal_t *journal)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	journal->j_commit_interval = sbi->s_commit_interval;
	journal->j_min_batch_time = sbi->s_min_batch_time;
	journal->j_max_batch_time = sbi->s_max_batch_time;

	write_lock(&journal->j_state_lock);
	if (test_opt(sb, BARRIER))
		journal->j_flags |= JBD2_BARRIER;
	else
		journal->j_flags &= ~JBD2_BARRIER;
	if (test_opt(sb, DATA_ERR_ABORT))
		journal->j_flags |= JBD2_ABORT_ON_SYNCDATA_ERR;
	else
		journal->j_flags &= ~JBD2_ABORT_ON_SYNCDATA_ERR;
	write_unlock(&journal->j_state_lock);
}

static journal_t *ext4_get_journal(struct super_block *sb,
				   unsigned int journal_inum)
{
	struct inode *journal_inode;
	journal_t *journal;

	BUG_ON(!EXT4_HAS_COMPAT_FEATURE(sb, EXT4_FEATURE_COMPAT_HAS_JOURNAL));

	/* First, test for the existence of a valid inode on disk.  Bad
	 * things happen if we iget() an unused inode, as the subsequent
	 * iput() will try to delete it. */

	journal_inode = ext4_iget(sb, journal_inum);
	if (IS_ERR(journal_inode)) {
		ext4_msg(sb, KERN_ERR, "no journal found");
		return NULL;
	}
	if (!journal_inode->i_nlink) {
		make_bad_inode(journal_inode);
		iput(journal_inode);
		ext4_msg(sb, KERN_ERR, "journal inode is deleted");
		return NULL;
	}

	jbd_debug(2, "Journal inode found at %p: %lld bytes\n",
		  journal_inode, journal_inode->i_size);
	if (!S_ISREG(journal_inode->i_mode)) {
		ext4_msg(sb, KERN_ERR, "invalid journal inode");
		iput(journal_inode);
		return NULL;
	}

	journal = jbd2_journal_init_inode(journal_inode);
	if (!journal) {
		ext4_msg(sb, KERN_ERR, "Could not load journal inode");
		iput(journal_inode);
		return NULL;
	}
	journal->j_private = sb;
	ext4_init_journal_params(sb, journal);
	return journal;
}

static journal_t *ext4_get_dev_journal(struct super_block *sb,
				       dev_t j_dev)
{
	struct buffer_head *bh;
	journal_t *journal;
	ext4_fsblk_t start;
	ext4_fsblk_t len;
	int hblock, blocksize;
	ext4_fsblk_t sb_block;
	unsigned long offset;
	struct ext4_super_block *es;
	struct block_device *bdev;

	BUG_ON(!EXT4_HAS_COMPAT_FEATURE(sb, EXT4_FEATURE_COMPAT_HAS_JOURNAL));

	bdev = ext4_blkdev_get(j_dev, sb);
	if (bdev == NULL)
		return NULL;

	if (bd_claim(bdev, sb)) {
		ext4_msg(sb, KERN_ERR,
			"failed to claim external journal device");
		blkdev_put(bdev, FMODE_READ|FMODE_WRITE);
		return NULL;
	}

	blocksize = sb->s_blocksize;
	hblock = bdev_logical_block_size(bdev);
	if (blocksize < hblock) {
		ext4_msg(sb, KERN_ERR,
			"blocksize too small for journal device");
		goto out_bdev;
	}

	sb_block = EXT4_MIN_BLOCK_SIZE / blocksize;
	offset = EXT4_MIN_BLOCK_SIZE % blocksize;
	set_blocksize(bdev, blocksize);
	if (!(bh = __bread(bdev, sb_block, blocksize))) {
		ext4_msg(sb, KERN_ERR, "couldn't read superblock of "
		       "external journal");
		goto out_bdev;
	}

	es = (struct ext4_super_block *) (((char *)bh->b_data) + offset);
	if ((le16_to_cpu(es->s_magic) != EXT4_SUPER_MAGIC) ||
	    !(le32_to_cpu(es->s_feature_incompat) &
	      EXT4_FEATURE_INCOMPAT_JOURNAL_DEV)) {
		ext4_msg(sb, KERN_ERR, "external journal has "
					"bad superblock");
		brelse(bh);
		goto out_bdev;
	}

	if (memcmp(EXT4_SB(sb)->s_es->s_journal_uuid, es->s_uuid, 16)) {
		ext4_msg(sb, KERN_ERR, "journal UUID does not match");
		brelse(bh);
		goto out_bdev;
	}

	len = ext4_blocks_count(es);
	start = sb_block + 1;
	brelse(bh);	/* we're done with the superblock */

	journal = jbd2_journal_init_dev(bdev, sb->s_bdev,
					start, len, blocksize);
	if (!journal) {
		ext4_msg(sb, KERN_ERR, "failed to create device journal");
		goto out_bdev;
	}
	journal->j_private = sb;
	ll_rw_block(READ, 1, &journal->j_sb_buffer);
	wait_on_buffer(journal->j_sb_buffer);
	if (!buffer_uptodate(journal->j_sb_buffer)) {
		ext4_msg(sb, KERN_ERR, "I/O error on journal device");
		goto out_journal;
	}
	if (be32_to_cpu(journal->j_superblock->s_nr_users) != 1) {
		ext4_msg(sb, KERN_ERR, "External journal has more than one "
					"user (unsupported) - %d",
			be32_to_cpu(journal->j_superblock->s_nr_users));
		goto out_journal;
	}
	EXT4_SB(sb)->journal_bdev = bdev;
	ext4_init_journal_params(sb, journal);
	return journal;

out_journal:
	jbd2_journal_destroy(journal);
out_bdev:
	ext4_blkdev_put(bdev);
	return NULL;
}

static int ext4_load_journal(struct super_block *sb,
			     struct ext4_super_block *es,
			     unsigned long journal_devnum)
{
	journal_t *journal;
	unsigned int journal_inum = le32_to_cpu(es->s_journal_inum);
	dev_t journal_dev;
	int err = 0;
	int really_read_only;

	BUG_ON(!EXT4_HAS_COMPAT_FEATURE(sb, EXT4_FEATURE_COMPAT_HAS_JOURNAL));

	if (journal_devnum &&
	    journal_devnum != le32_to_cpu(es->s_journal_dev)) {
		ext4_msg(sb, KERN_INFO, "external journal device major/minor "
			"numbers have changed");
		journal_dev = new_decode_dev(journal_devnum);
	} else
		journal_dev = new_decode_dev(le32_to_cpu(es->s_journal_dev));

	really_read_only = bdev_read_only(sb->s_bdev);

	/*
	 * Are we loading a blank journal or performing recovery after a
	 * crash?  For recovery, we need to check in advance whether we
	 * can get read-write access to the device.
	 */
	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER)) {
		if (sb->s_flags & MS_RDONLY) {
			ext4_msg(sb, KERN_INFO, "INFO: recovery "
					"required on readonly filesystem");
			if (really_read_only) {
				ext4_msg(sb, KERN_ERR, "write access "
					"unavailable, cannot proceed");
				return -EROFS;
			}
			ext4_msg(sb, KERN_INFO, "write access will "
			       "be enabled during recovery");
		}
	}

	if (journal_inum && journal_dev) {
		ext4_msg(sb, KERN_ERR, "filesystem has both journal "
		       "and inode journals!");
		return -EINVAL;
	}

	if (journal_inum) {
		if (!(journal = ext4_get_journal(sb, journal_inum)))
			return -EINVAL;
	} else {
		if (!(journal = ext4_get_dev_journal(sb, journal_dev)))
			return -EINVAL;
	}

	if (!(journal->j_flags & JBD2_BARRIER))
		ext4_msg(sb, KERN_INFO, "barriers disabled");

	if (!really_read_only && test_opt(sb, UPDATE_JOURNAL)) {
		err = jbd2_journal_update_format(journal);
		if (err)  {
			ext4_msg(sb, KERN_ERR, "error updating journal");
			jbd2_journal_destroy(journal);
			return err;
		}
	}

	if (!EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER))
		err = jbd2_journal_wipe(journal, !really_read_only);
	if (!err) {
		char *save = kmalloc(EXT4_S_ERR_LEN, GFP_KERNEL);
		if (save)
			memcpy(save, ((char *) es) +
			       EXT4_S_ERR_START, EXT4_S_ERR_LEN);
		err = jbd2_journal_load(journal);
		if (save)
			memcpy(((char *) es) + EXT4_S_ERR_START,
			       save, EXT4_S_ERR_LEN);
		kfree(save);
	}

	if (err) {
		ext4_msg(sb, KERN_ERR, "error loading journal");
		jbd2_journal_destroy(journal);
		return err;
	}

	EXT4_SB(sb)->s_journal = journal;
	ext4_clear_journal_err(sb, es);

	if (journal_devnum &&
	    journal_devnum != le32_to_cpu(es->s_journal_dev)) {
		es->s_journal_dev = cpu_to_le32(journal_devnum);

		/* Make sure we flush the recovery flag to disk. */
		ext4_commit_super(sb, 1);
	}

	return 0;
}

static int ext4_commit_super(struct super_block *sb, int sync)
{
	struct ext4_super_block *es = EXT4_SB(sb)->s_es;
	struct buffer_head *sbh = EXT4_SB(sb)->s_sbh;
	int error = 0;

	if (!sbh)
		return error;
	if (buffer_write_io_error(sbh)) {
		/*
		 * Oh, dear.  A previous attempt to write the
		 * superblock failed.  This could happen because the
		 * USB device was yanked out.  Or it could happen to
		 * be a transient write error and maybe the block will
		 * be remapped.  Nothing we can do but to retry the
		 * write and hope for the best.
		 */
		ext4_msg(sb, KERN_ERR, "previous I/O error to "
		       "superblock detected");
		clear_buffer_write_io_error(sbh);
		set_buffer_uptodate(sbh);
	}
	/*
	 * If the file system is mounted read-only, don't update the
	 * superblock write time.  This avoids updating the superblock
	 * write time when we are mounting the root file system
	 * read/only but we need to replay the journal; at that point,
	 * for people who are east of GMT and who make their clock
	 * tick in localtime for Windows bug-for-bug compatibility,
	 * the clock is set in the future, and this will cause e2fsck
	 * to complain and force a full file system check.
	 */
	if (!(sb->s_flags & MS_RDONLY))
		es->s_wtime = cpu_to_le32(get_seconds());
	if (sb->s_bdev->bd_part)
		es->s_kbytes_written =
			cpu_to_le64(EXT4_SB(sb)->s_kbytes_written +
			    ((part_stat_read(sb->s_bdev->bd_part, sectors[1]) -
			      EXT4_SB(sb)->s_sectors_written_start) >> 1));
	else
		es->s_kbytes_written =
			cpu_to_le64(EXT4_SB(sb)->s_kbytes_written);
	ext4_free_blocks_count_set(es, percpu_counter_sum_positive(
					&EXT4_SB(sb)->s_freeblocks_counter));
	es->s_free_inodes_count = cpu_to_le32(percpu_counter_sum_positive(
					&EXT4_SB(sb)->s_freeinodes_counter));
	sb->s_dirt = 0;
	BUFFER_TRACE(sbh, "marking dirty");
	mark_buffer_dirty(sbh);
	if (sync) {
		error = sync_dirty_buffer(sbh);
		if (error)
			return error;

		error = buffer_write_io_error(sbh);
		if (error) {
			ext4_msg(sb, KERN_ERR, "I/O error while writing "
			       "superblock");
			clear_buffer_write_io_error(sbh);
			set_buffer_uptodate(sbh);
		}
	}
	return error;
}

/*
 * Have we just finished recovery?  If so, and if we are mounting (or
 * remounting) the filesystem readonly, then we will end up with a
 * consistent fs on disk.  Record that fact.
 */
static void ext4_mark_recovery_complete(struct super_block *sb,
					struct ext4_super_block *es)
{
	journal_t *journal = EXT4_SB(sb)->s_journal;

	if (!EXT4_HAS_COMPAT_FEATURE(sb, EXT4_FEATURE_COMPAT_HAS_JOURNAL)) {
		BUG_ON(journal != NULL);
		return;
	}
	jbd2_journal_lock_updates(journal);
	if (jbd2_journal_flush(journal) < 0)
		goto out;

	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER) &&
	    sb->s_flags & MS_RDONLY) {
		EXT4_CLEAR_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER);
		ext4_commit_super(sb, 1);
	}

out:
	jbd2_journal_unlock_updates(journal);
}

/*
 * If we are mounting (or read-write remounting) a filesystem whose journal
 * has recorded an error from a previous lifetime, move that error to the
 * main filesystem now.
 */
static void ext4_clear_journal_err(struct super_block *sb,
				   struct ext4_super_block *es)
{
	journal_t *journal;
	int j_errno;
	const char *errstr;

	BUG_ON(!EXT4_HAS_COMPAT_FEATURE(sb, EXT4_FEATURE_COMPAT_HAS_JOURNAL));

	journal = EXT4_SB(sb)->s_journal;

	/*
	 * Now check for any error status which may have been recorded in the
	 * journal by a prior ext4_error() or ext4_abort()
	 */

	j_errno = jbd2_journal_errno(journal);
	if (j_errno) {
		char nbuf[16];

		errstr = ext4_decode_error(sb, j_errno, nbuf);
		ext4_warning(sb, "Filesystem error recorded "
			     "from previous mount: %s", errstr);
		ext4_warning(sb, "Marking fs in need of filesystem check.");

		EXT4_SB(sb)->s_mount_state |= EXT4_ERROR_FS;
		es->s_state |= cpu_to_le16(EXT4_ERROR_FS);
		ext4_commit_super(sb, 1);

		jbd2_journal_clear_err(journal);
	}
}

/*
 * Force the running and committing transactions to commit,
 * and wait on the commit.
 */
int ext4_force_commit(struct super_block *sb)
{
	journal_t *journal;
	int ret = 0;

	if (sb->s_flags & MS_RDONLY)
		return 0;

	journal = EXT4_SB(sb)->s_journal;
	if (journal) {
		vfs_check_frozen(sb, SB_FREEZE_TRANS);
		ret = ext4_journal_force_commit(journal);
	}

	return ret;
}

static void ext4_write_super(struct super_block *sb)
{
	lock_super(sb);
	ext4_commit_super(sb, 1);
	unlock_super(sb);
}

static int ext4_sync_fs(struct super_block *sb, int wait)
{
	int ret = 0;
	tid_t target;
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	trace_ext4_sync_fs(sb, wait);
	flush_workqueue(sbi->dio_unwritten_wq);
	if (jbd2_journal_start_commit(sbi->s_journal, &target)) {
		if (wait)
			jbd2_log_wait_commit(sbi->s_journal, target);
	}
	return ret;
}

/*
 * LVM calls this function before a (read-only) snapshot is created.  This
 * gives us a chance to flush the journal completely and mark the fs clean.
 */
static int ext4_freeze(struct super_block *sb)
{
	int error = 0;
	journal_t *journal;

	if (sb->s_flags & MS_RDONLY)
		return 0;

	journal = EXT4_SB(sb)->s_journal;

	/* Now we set up the journal barrier. */
	jbd2_journal_lock_updates(journal);

	/*
	 * Don't clear the needs_recovery flag if we failed to flush
	 * the journal.
	 */
	error = jbd2_journal_flush(journal);
	if (error < 0)
		goto out;

	/* Journal blocked and flushed, clear needs_recovery flag. */
	EXT4_CLEAR_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER);
	error = ext4_commit_super(sb, 1);
out:
	/* we rely on s_frozen to stop further updates */
	jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
	return error;
}

/*
 * Called by LVM after the snapshot is done.  We need to reset the RECOVER
 * flag here, even though the filesystem is not technically dirty yet.
 */
static int ext4_unfreeze(struct super_block *sb)
{
	if (sb->s_flags & MS_RDONLY)
		return 0;

	lock_super(sb);
	/* Reset the needs_recovery flag before the fs is unlocked. */
	EXT4_SET_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER);
	ext4_commit_super(sb, 1);
	unlock_super(sb);
	return 0;
}

static int ext4_remount(struct super_block *sb, int *flags, char *data)
{
	struct ext4_super_block *es;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	ext4_fsblk_t n_blocks_count = 0;
	unsigned long old_sb_flags;
	struct ext4_mount_options old_opts;
	int enable_quota = 0;
	ext4_group_t g;
	unsigned int journal_ioprio = DEFAULT_JOURNAL_IOPRIO;
	int err;
#ifdef CONFIG_QUOTA
	int i;
#endif
	char *orig_data = kstrdup(data, GFP_KERNEL);

	lock_kernel();

	/* Store the original options */
	lock_super(sb);
	old_sb_flags = sb->s_flags;
	old_opts.s_mount_opt = sbi->s_mount_opt;
	old_opts.s_resuid = sbi->s_resuid;
	old_opts.s_resgid = sbi->s_resgid;
	old_opts.s_commit_interval = sbi->s_commit_interval;
	old_opts.s_min_batch_time = sbi->s_min_batch_time;
	old_opts.s_max_batch_time = sbi->s_max_batch_time;
#ifdef CONFIG_QUOTA
	old_opts.s_jquota_fmt = sbi->s_jquota_fmt;
	for (i = 0; i < MAXQUOTAS; i++)
		old_opts.s_qf_names[i] = sbi->s_qf_names[i];
#endif
	if (sbi->s_journal && sbi->s_journal->j_task->io_context)
		journal_ioprio = sbi->s_journal->j_task->io_context->ioprio;

	/*
	 * Allow the "check" option to be passed as a remount option.
	 */
	if (!parse_options(data, sb, NULL, &journal_ioprio,
			   &n_blocks_count, 1)) {
		err = -EINVAL;
		goto restore_opts;
	}

	if (sbi->s_mount_flags & EXT4_MF_FS_ABORTED)
		ext4_abort(sb, "Abort forced by user");

	sb->s_flags = (sb->s_flags & ~MS_POSIXACL) |
		(test_opt(sb, POSIX_ACL) ? MS_POSIXACL : 0);

	es = sbi->s_es;

	if (sbi->s_journal) {
		ext4_init_journal_params(sb, sbi->s_journal);
		set_task_ioprio(sbi->s_journal->j_task, journal_ioprio);
	}

	if ((*flags & MS_RDONLY) != (sb->s_flags & MS_RDONLY) ||
		n_blocks_count > ext4_blocks_count(es)) {
		if (sbi->s_mount_flags & EXT4_MF_FS_ABORTED) {
			err = -EROFS;
			goto restore_opts;
		}

		if (*flags & MS_RDONLY) {
			err = dquot_suspend(sb, -1);
			if (err < 0)
				goto restore_opts;

			/*
			 * First of all, the unconditional stuff we have to do
			 * to disable replay of the journal when we next remount
			 */
			sb->s_flags |= MS_RDONLY;

			/*
			 * OK, test if we are remounting a valid rw partition
			 * readonly, and if so set the rdonly flag and then
			 * mark the partition as valid again.
			 */
			if (!(es->s_state & cpu_to_le16(EXT4_VALID_FS)) &&
			    (sbi->s_mount_state & EXT4_VALID_FS))
				es->s_state = cpu_to_le16(sbi->s_mount_state);

			if (sbi->s_journal)
				ext4_mark_recovery_complete(sb, es);
		} else {
			/* Make sure we can mount this feature set readwrite */
			if (!ext4_feature_set_ok(sb, 0)) {
				err = -EROFS;
				goto restore_opts;
			}
			/*
			 * Make sure the group descriptor checksums
			 * are sane.  If they aren't, refuse to remount r/w.
			 */
			for (g = 0; g < sbi->s_groups_count; g++) {
				struct ext4_group_desc *gdp =
					ext4_get_group_desc(sb, g, NULL);

				if (!ext4_group_desc_csum_verify(sbi, g, gdp)) {
					ext4_msg(sb, KERN_ERR,
	       "ext4_remount: Checksum for group %u failed (%u!=%u)",
		g, le16_to_cpu(ext4_group_desc_csum(sbi, g, gdp)),
					       le16_to_cpu(gdp->bg_checksum));
					err = -EINVAL;
					goto restore_opts;
				}
			}

			/*
			 * If we have an unprocessed orphan list hanging
			 * around from a previously readonly bdev mount,
			 * require a full umount/remount for now.
			 */
			if (es->s_last_orphan) {
				ext4_msg(sb, KERN_WARNING, "Couldn't "
				       "remount RDWR because of unprocessed "
				       "orphan inode list.  Please "
				       "umount/remount instead");
				err = -EINVAL;
				goto restore_opts;
			}

			/*
			 * Mounting a RDONLY partition read-write, so reread
			 * and store the current valid flag.  (It may have
			 * been changed by e2fsck since we originally mounted
			 * the partition.)
			 */
			if (sbi->s_journal)
				ext4_clear_journal_err(sb, es);
			sbi->s_mount_state = le16_to_cpu(es->s_state);
			if ((err = ext4_group_extend(sb, es, n_blocks_count)))
				goto restore_opts;
			if (!ext4_setup_super(sb, es, 0))
				sb->s_flags &= ~MS_RDONLY;
			enable_quota = 1;
		}
	}
	ext4_setup_system_zone(sb);
	if (sbi->s_journal == NULL)
		ext4_commit_super(sb, 1);

#ifdef CONFIG_QUOTA
	/* Release old quota file names */
	for (i = 0; i < MAXQUOTAS; i++)
		if (old_opts.s_qf_names[i] &&
		    old_opts.s_qf_names[i] != sbi->s_qf_names[i])
			kfree(old_opts.s_qf_names[i]);
#endif
	unlock_super(sb);
	unlock_kernel();
	if (enable_quota)
		dquot_resume(sb, -1);

	ext4_msg(sb, KERN_INFO, "re-mounted. Opts: %s", orig_data);
	kfree(orig_data);
	return 0;

restore_opts:
	sb->s_flags = old_sb_flags;
	sbi->s_mount_opt = old_opts.s_mount_opt;
	sbi->s_resuid = old_opts.s_resuid;
	sbi->s_resgid = old_opts.s_resgid;
	sbi->s_commit_interval = old_opts.s_commit_interval;
	sbi->s_min_batch_time = old_opts.s_min_batch_time;
	sbi->s_max_batch_time = old_opts.s_max_batch_time;
#ifdef CONFIG_QUOTA
	sbi->s_jquota_fmt = old_opts.s_jquota_fmt;
	for (i = 0; i < MAXQUOTAS; i++) {
		if (sbi->s_qf_names[i] &&
		    old_opts.s_qf_names[i] != sbi->s_qf_names[i])
			kfree(sbi->s_qf_names[i]);
		sbi->s_qf_names[i] = old_opts.s_qf_names[i];
	}
#endif
	unlock_super(sb);
	unlock_kernel();
	kfree(orig_data);
	return err;
}

static int ext4_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_super_block *es = sbi->s_es;
	u64 fsid;

	if (test_opt(sb, MINIX_DF)) {
		sbi->s_overhead_last = 0;
	} else if (sbi->s_blocks_last != ext4_blocks_count(es)) {
		ext4_group_t i, ngroups = ext4_get_groups_count(sb);
		ext4_fsblk_t overhead = 0;

		/*
		 * Compute the overhead (FS structures).  This is constant
		 * for a given filesystem unless the number of block groups
		 * changes so we cache the previous value until it does.
		 */

		/*
		 * All of the blocks before first_data_block are
		 * overhead
		 */
		overhead = le32_to_cpu(es->s_first_data_block);

		/*
		 * Add the overhead attributed to the superblock and
		 * block group descriptors.  If the sparse superblocks
		 * feature is turned on, then not all groups have this.
		 */
		for (i = 0; i < ngroups; i++) {
			overhead += ext4_bg_has_super(sb, i) +
				ext4_bg_num_gdb(sb, i);
			cond_resched();
		}

		/*
		 * Every block group has an inode bitmap, a block
		 * bitmap, and an inode table.
		 */
		overhead += ngroups * (2 + sbi->s_itb_per_group);
		sbi->s_overhead_last = overhead;
		smp_wmb();
		sbi->s_blocks_last = ext4_blocks_count(es);
	}

	buf->f_type = EXT4_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = ext4_blocks_count(es) - sbi->s_overhead_last;
	buf->f_bfree = percpu_counter_sum_positive(&sbi->s_freeblocks_counter) -
		       percpu_counter_sum_positive(&sbi->s_dirtyblocks_counter);
	buf->f_bavail = buf->f_bfree - ext4_r_blocks_count(es);
	if (buf->f_bfree < ext4_r_blocks_count(es))
		buf->f_bavail = 0;
	buf->f_files = le32_to_cpu(es->s_inodes_count);
	buf->f_ffree = percpu_counter_sum_positive(&sbi->s_freeinodes_counter);
	buf->f_namelen = EXT4_NAME_LEN;
	fsid = le64_to_cpup((void *)es->s_uuid) ^
	       le64_to_cpup((void *)es->s_uuid + sizeof(u64));
	buf->f_fsid.val[0] = fsid & 0xFFFFFFFFUL;
	buf->f_fsid.val[1] = (fsid >> 32) & 0xFFFFFFFFUL;

	return 0;
}

/* Helper function for writing quotas on sync - we need to start transaction
 * before quota file is locked for write. Otherwise the are possible deadlocks:
 * Process 1                         Process 2
 * ext4_create()                     quota_sync()
 *   jbd2_journal_start()                  write_dquot()
 *   dquot_initialize()                         down(dqio_mutex)
 *     down(dqio_mutex)                    jbd2_journal_start()
 *
 */

#ifdef CONFIG_QUOTA

static inline struct inode *dquot_to_inode(struct dquot *dquot)
{
	return sb_dqopt(dquot->dq_sb)->files[dquot->dq_type];
}

static int ext4_write_dquot(struct dquot *dquot)
{
	int ret, err;
	handle_t *handle;
	struct inode *inode;

	inode = dquot_to_inode(dquot);
	handle = ext4_journal_start(inode,
				    EXT4_QUOTA_TRANS_BLOCKS(dquot->dq_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);
	ret = dquot_commit(dquot);
	err = ext4_journal_stop(handle);
	if (!ret)
		ret = err;
	return ret;
}

static int ext4_acquire_dquot(struct dquot *dquot)
{
	int ret, err;
	handle_t *handle;

	handle = ext4_journal_start(dquot_to_inode(dquot),
				    EXT4_QUOTA_INIT_BLOCKS(dquot->dq_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);
	ret = dquot_acquire(dquot);
	err = ext4_journal_stop(handle);
	if (!ret)
		ret = err;
	return ret;
}

static int ext4_release_dquot(struct dquot *dquot)
{
	int ret, err;
	handle_t *handle;

	handle = ext4_journal_start(dquot_to_inode(dquot),
				    EXT4_QUOTA_DEL_BLOCKS(dquot->dq_sb));
	if (IS_ERR(handle)) {
		/* Release dquot anyway to avoid endless cycle in dqput() */
		dquot_release(dquot);
		return PTR_ERR(handle);
	}
	ret = dquot_release(dquot);
	err = ext4_journal_stop(handle);
	if (!ret)
		ret = err;
	return ret;
}

static int ext4_mark_dquot_dirty(struct dquot *dquot)
{
	/* Are we journaling quotas? */
	if (EXT4_SB(dquot->dq_sb)->s_qf_names[USRQUOTA] ||
	    EXT4_SB(dquot->dq_sb)->s_qf_names[GRPQUOTA]) {
		dquot_mark_dquot_dirty(dquot);
		return ext4_write_dquot(dquot);
	} else {
		return dquot_mark_dquot_dirty(dquot);
	}
}

static int ext4_write_info(struct super_block *sb, int type)
{
	int ret, err;
	handle_t *handle;

	/* Data block + inode block */
	handle = ext4_journal_start(sb->s_root->d_inode, 2);
	if (IS_ERR(handle))
		return PTR_ERR(handle);
	ret = dquot_commit_info(sb, type);
	err = ext4_journal_stop(handle);
	if (!ret)
		ret = err;
	return ret;
}

/*
 * Turn on quotas during mount time - we need to find
 * the quota file and such...
 */
static int ext4_quota_on_mount(struct super_block *sb, int type)
{
	return dquot_quota_on_mount(sb, EXT4_SB(sb)->s_qf_names[type],
					EXT4_SB(sb)->s_jquota_fmt, type);
}

/*
 * Standard function to be called on quota_on
 */
static int ext4_quota_on(struct super_block *sb, int type, int format_id,
			 char *name)
{
	int err;
	struct path path;

	if (!test_opt(sb, QUOTA))
		return -EINVAL;

	err = kern_path(name, LOOKUP_FOLLOW, &path);
	if (err)
		return err;

	/* Quotafile not on the same filesystem? */
	if (path.mnt->mnt_sb != sb) {
		path_put(&path);
		return -EXDEV;
	}
	/* Journaling quota? */
	if (EXT4_SB(sb)->s_qf_names[type]) {
		/* Quotafile not in fs root? */
		if (path.dentry->d_parent != sb->s_root)
			ext4_msg(sb, KERN_WARNING,
				"Quota file not on filesystem root. "
				"Journaled quota will not work");
	}

	/*
	 * When we journal data on quota file, we have to flush journal to see
	 * all updates to the file when we bypass pagecache...
	 */
	if (EXT4_SB(sb)->s_journal &&
	    ext4_should_journal_data(path.dentry->d_inode)) {
		/*
		 * We don't need to lock updates but journal_flush() could
		 * otherwise be livelocked...
		 */
		jbd2_journal_lock_updates(EXT4_SB(sb)->s_journal);
		err = jbd2_journal_flush(EXT4_SB(sb)->s_journal);
		jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
		if (err) {
			path_put(&path);
			return err;
		}
	}

	err = dquot_quota_on_path(sb, type, format_id, &path);
	path_put(&path);
	return err;
}

static int ext4_quota_off(struct super_block *sb, int type)
{
	/* Force all delayed allocation blocks to be allocated */
	if (test_opt(sb, DELALLOC)) {
		down_read(&sb->s_umount);
		sync_filesystem(sb);
		up_read(&sb->s_umount);
	}

	return dquot_quota_off(sb, type);
}

/* Read data from quotafile - avoid pagecache and such because we cannot afford
 * acquiring the locks... As quota files are never truncated and quota code
 * itself serializes the operations (and noone else should touch the files)
 * we don't have to be afraid of races */
static ssize_t ext4_quota_read(struct super_block *sb, int type, char *data,
			       size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	ext4_lblk_t blk = off >> EXT4_BLOCK_SIZE_BITS(sb);
	int err = 0;
	int offset = off & (sb->s_blocksize - 1);
	int tocopy;
	size_t toread;
	struct buffer_head *bh;
	loff_t i_size = i_size_read(inode);

	if (off > i_size)
		return 0;
	if (off+len > i_size)
		len = i_size-off;
	toread = len;
	while (toread > 0) {
		tocopy = sb->s_blocksize - offset < toread ?
				sb->s_blocksize - offset : toread;
		bh = ext4_bread(NULL, inode, blk, 0, &err);
		if (err)
			return err;
		if (!bh)	/* A hole? */
			memset(data, 0, tocopy);
		else
			memcpy(data, bh->b_data+offset, tocopy);
		brelse(bh);
		offset = 0;
		toread -= tocopy;
		data += tocopy;
		blk++;
	}
	return len;
}

/* Write to quotafile (we know the transaction is already started and has
 * enough credits) */
static ssize_t ext4_quota_write(struct super_block *sb, int type,
				const char *data, size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	ext4_lblk_t blk = off >> EXT4_BLOCK_SIZE_BITS(sb);
	int err = 0;
	int offset = off & (sb->s_blocksize - 1);
	struct buffer_head *bh;
	handle_t *handle = journal_current_handle();

	if (EXT4_SB(sb)->s_journal && !handle) {
		ext4_msg(sb, KERN_WARNING, "Quota write (off=%llu, len=%llu)"
			" cancelled because transaction is not started",
			(unsigned long long)off, (unsigned long long)len);
		return -EIO;
	}
	/*
	 * Since we account only one data block in transaction credits,
	 * then it is impossible to cross a block boundary.
	 */
	if (sb->s_blocksize - offset < len) {
		ext4_msg(sb, KERN_WARNING, "Quota write (off=%llu, len=%llu)"
			" cancelled because not block aligned",
			(unsigned long long)off, (unsigned long long)len);
		return -EIO;
	}

	mutex_lock_nested(&inode->i_mutex, I_MUTEX_QUOTA);
	bh = ext4_bread(handle, inode, blk, 1, &err);
	if (!bh)
		goto out;
	err = ext4_journal_get_write_access(handle, bh);
	if (err) {
		brelse(bh);
		goto out;
	}
	lock_buffer(bh);
	memcpy(bh->b_data+offset, data, len);
	flush_dcache_page(bh->b_page);
	unlock_buffer(bh);
	err = ext4_handle_dirty_metadata(handle, NULL, bh);
	brelse(bh);
out:
	if (err) {
		mutex_unlock(&inode->i_mutex);
		return err;
	}
	if (inode->i_size < off + len) {
		i_size_write(inode, off + len);
		EXT4_I(inode)->i_disksize = inode->i_size;
	}
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	ext4_mark_inode_dirty(handle, inode);
	mutex_unlock(&inode->i_mutex);
	return len;
}

#endif

static int ext4_get_sb(struct file_system_type *fs_type, int flags,
		       const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, ext4_fill_super,mnt);
}

#if !defined(CONFIG_EXT2_FS) && !defined(CONFIG_EXT2_FS_MODULE) && defined(CONFIG_EXT4_USE_FOR_EXT23)
static struct file_system_type ext2_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ext2",
	.get_sb		= ext4_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static inline void register_as_ext2(void)
{
	int err = register_filesystem(&ext2_fs_type);
	if (err)
		printk(KERN_WARNING
		       "EXT4-fs: Unable to register as ext2 (%d)\n", err);
}

static inline void unregister_as_ext2(void)
{
	unregister_filesystem(&ext2_fs_type);
}
MODULE_ALIAS("ext2");
#else
static inline void register_as_ext2(void) { }
static inline void unregister_as_ext2(void) { }
#endif

#if !defined(CONFIG_EXT3_FS) && !defined(CONFIG_EXT3_FS_MODULE) && defined(CONFIG_EXT4_USE_FOR_EXT23)
static inline void register_as_ext3(void)
{
	int err = register_filesystem(&ext3_fs_type);
	if (err)
		printk(KERN_WARNING
		       "EXT4-fs: Unable to register as ext3 (%d)\n", err);
}

static inline void unregister_as_ext3(void)
{
	unregister_filesystem(&ext3_fs_type);
}
MODULE_ALIAS("ext3");
#else
static inline void register_as_ext3(void) { }
static inline void unregister_as_ext3(void) { }
#endif

static struct file_system_type ext4_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ext4",
	.get_sb		= ext4_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static int __init init_ext4_fs(void)
{
	int err;

	ext4_check_flag_values();
	err = init_ext4_system_zone();
	if (err)
		return err;
	ext4_kset = kset_create_and_add("ext4", NULL, fs_kobj);
	if (!ext4_kset)
		goto out4;
	ext4_proc_root = proc_mkdir("fs/ext4", NULL);
	err = init_ext4_mballoc();
	if (err)
		goto out3;

	err = init_ext4_xattr();
	if (err)
		goto out2;
	err = init_inodecache();
	if (err)
		goto out1;
	register_as_ext2();
	register_as_ext3();
	err = register_filesystem(&ext4_fs_type);
	if (err)
		goto out;
	return 0;
out:
	unregister_as_ext2();
	unregister_as_ext3();
	destroy_inodecache();
out1:
	exit_ext4_xattr();
out2:
	exit_ext4_mballoc();
out3:
	remove_proc_entry("fs/ext4", NULL);
	kset_unregister(ext4_kset);
out4:
	exit_ext4_system_zone();
	return err;
}

static void __exit exit_ext4_fs(void)
{
	unregister_as_ext2();
	unregister_as_ext3();
	unregister_filesystem(&ext4_fs_type);
	destroy_inodecache();
	exit_ext4_xattr();
	exit_ext4_mballoc();
	remove_proc_entry("fs/ext4", NULL);
	kset_unregister(ext4_kset);
	exit_ext4_system_zone();
}

MODULE_AUTHOR("Remy Card, Stephen Tweedie, Andrew Morton, Andreas Dilger, Theodore Ts'o and others");
MODULE_DESCRIPTION("Fourth Extended Filesystem");
MODULE_LICENSE("GPL");
module_init(init_ext4_fs)
module_exit(exit_ext4_fs)
