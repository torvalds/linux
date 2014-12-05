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
#include <linux/cleancache.h>
#include <asm/uaccess.h>

#include <linux/kthread.h>
#include <linux/freezer.h>

#include "ext4.h"
#include "ext4_extents.h"	/* Needed for trace points definition */
#include "ext4_jbd2.h"
#include "xattr.h"
#include "acl.h"
#include "mballoc.h"

#define CREATE_TRACE_POINTS
#include <trace/events/ext4.h>

static struct proc_dir_entry *ext4_proc_root;
static struct kset *ext4_kset;
static struct ext4_lazy_init *ext4_li_info;
static struct mutex ext4_li_mtx;
static struct ext4_features *ext4_feat;

static int ext4_load_journal(struct super_block *, struct ext4_super_block *,
			     unsigned long journal_devnum);
static int ext4_show_options(struct seq_file *seq, struct dentry *root);
static int ext4_commit_super(struct super_block *sb, int sync);
static void ext4_mark_recovery_complete(struct super_block *sb,
					struct ext4_super_block *es);
static void ext4_clear_journal_err(struct super_block *sb,
				   struct ext4_super_block *es);
static int ext4_sync_fs(struct super_block *sb, int wait);
static int ext4_remount(struct super_block *sb, int *flags, char *data);
static int ext4_statfs(struct dentry *dentry, struct kstatfs *buf);
static int ext4_unfreeze(struct super_block *sb);
static int ext4_freeze(struct super_block *sb);
static struct dentry *ext4_mount(struct file_system_type *fs_type, int flags,
		       const char *dev_name, void *data);
static inline int ext2_feature_set_ok(struct super_block *sb);
static inline int ext3_feature_set_ok(struct super_block *sb);
static int ext4_feature_set_ok(struct super_block *sb, int readonly);
static void ext4_destroy_lazyinit_thread(void);
static void ext4_unregister_li_request(struct super_block *sb);
static void ext4_clear_request_list(void);
static int ext4_reserve_clusters(struct ext4_sb_info *, ext4_fsblk_t);

#if !defined(CONFIG_EXT2_FS) && !defined(CONFIG_EXT2_FS_MODULE) && defined(CONFIG_EXT4_USE_FOR_EXT23)
static struct file_system_type ext2_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ext2",
	.mount		= ext4_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("ext2");
MODULE_ALIAS("ext2");
#define IS_EXT2_SB(sb) ((sb)->s_bdev->bd_holder == &ext2_fs_type)
#else
#define IS_EXT2_SB(sb) (0)
#endif


#if !defined(CONFIG_EXT3_FS) && !defined(CONFIG_EXT3_FS_MODULE) && defined(CONFIG_EXT4_USE_FOR_EXT23)
static struct file_system_type ext3_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ext3",
	.mount		= ext4_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("ext3");
MODULE_ALIAS("ext3");
#define IS_EXT3_SB(sb) ((sb)->s_bdev->bd_holder == &ext3_fs_type)
#else
#define IS_EXT3_SB(sb) (0)
#endif

static int ext4_verify_csum_type(struct super_block *sb,
				 struct ext4_super_block *es)
{
	if (!EXT4_HAS_RO_COMPAT_FEATURE(sb,
					EXT4_FEATURE_RO_COMPAT_METADATA_CSUM))
		return 1;

	return es->s_checksum_type == EXT4_CRC32C_CHKSUM;
}

static __le32 ext4_superblock_csum(struct super_block *sb,
				   struct ext4_super_block *es)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	int offset = offsetof(struct ext4_super_block, s_checksum);
	__u32 csum;

	csum = ext4_chksum(sbi, ~0, (char *)es, offset);

	return cpu_to_le32(csum);
}

int ext4_superblock_csum_verify(struct super_block *sb,
				struct ext4_super_block *es)
{
	if (!EXT4_HAS_RO_COMPAT_FEATURE(sb,
				       EXT4_FEATURE_RO_COMPAT_METADATA_CSUM))
		return 1;

	return es->s_checksum == ext4_superblock_csum(sb, es);
}

void ext4_superblock_csum_set(struct super_block *sb)
{
	struct ext4_super_block *es = EXT4_SB(sb)->s_es;

	if (!EXT4_HAS_RO_COMPAT_FEATURE(sb,
		EXT4_FEATURE_RO_COMPAT_METADATA_CSUM))
		return;

	es->s_checksum = ext4_superblock_csum(sb, es);
}

void *ext4_kvmalloc(size_t size, gfp_t flags)
{
	void *ret;

	ret = kmalloc(size, flags);
	if (!ret)
		ret = __vmalloc(size, flags, PAGE_KERNEL);
	return ret;
}

void *ext4_kvzalloc(size_t size, gfp_t flags)
{
	void *ret;

	ret = kzalloc(size, flags);
	if (!ret)
		ret = __vmalloc(size, flags | __GFP_ZERO, PAGE_KERNEL);
	return ret;
}

void ext4_kvfree(void *ptr)
{
	if (is_vmalloc_addr(ptr))
		vfree(ptr);
	else
		kfree(ptr);

}

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

__u32 ext4_free_group_clusters(struct super_block *sb,
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

void ext4_free_group_clusters_set(struct super_block *sb,
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
	le32_add_cpu(&es->s_error_count, 1);
}

static void save_error_info(struct super_block *sb, const char *func,
			    unsigned int line)
{
	__save_error_info(sb, func, line);
	ext4_commit_super(sb, 1);
}

/*
 * The del_gendisk() function uninitializes the disk-specific data
 * structures, including the bdi structure, without telling anyone
 * else.  Once this happens, any attempt to call mark_buffer_dirty()
 * (for example, by ext4_commit_super), will cause a kernel OOPS.
 * This is a kludge to prevent these oops until we can put in a proper
 * hook in del_gendisk() to inform the VFS and file system layers.
 */
static int block_device_ejected(struct super_block *sb)
{
	struct inode *bd_inode = sb->s_bdev->bd_inode;
	struct backing_dev_info *bdi = bd_inode->i_mapping->backing_dev_info;

	return bdi->dev == NULL;
}

static void ext4_journal_commit_callback(journal_t *journal, transaction_t *txn)
{
	struct super_block		*sb = journal->j_private;
	struct ext4_sb_info		*sbi = EXT4_SB(sb);
	int				error = is_journal_aborted(journal);
	struct ext4_journal_cb_entry	*jce;

	BUG_ON(txn->t_state == T_FINISHED);
	spin_lock(&sbi->s_md_lock);
	while (!list_empty(&txn->t_private_list)) {
		jce = list_entry(txn->t_private_list.next,
				 struct ext4_journal_cb_entry, jce_list);
		list_del_init(&jce->jce_list);
		spin_unlock(&sbi->s_md_lock);
		jce->jce_func(sb, jce, error);
		spin_lock(&sbi->s_md_lock);
	}
	spin_unlock(&sbi->s_md_lock);
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
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	printk(KERN_CRIT "EXT4-fs error (device %s): %s:%d: comm %s: %pV\n",
	       sb->s_id, function, line, current->comm, &vaf);
	va_end(args);
	save_error_info(sb, function, line);

	ext4_handle_error(sb);
}

void ext4_error_inode(struct inode *inode, const char *function,
		      unsigned int line, ext4_fsblk_t block,
		      const char *fmt, ...)
{
	va_list args;
	struct va_format vaf;
	struct ext4_super_block *es = EXT4_SB(inode->i_sb)->s_es;

	es->s_last_error_ino = cpu_to_le32(inode->i_ino);
	es->s_last_error_block = cpu_to_le64(block);
	save_error_info(inode->i_sb, function, line);
	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	if (block)
		printk(KERN_CRIT "EXT4-fs error (device %s): %s:%d: "
		       "inode #%lu: block %llu: comm %s: %pV\n",
		       inode->i_sb->s_id, function, line, inode->i_ino,
		       block, current->comm, &vaf);
	else
		printk(KERN_CRIT "EXT4-fs error (device %s): %s:%d: "
		       "inode #%lu: comm %s: %pV\n",
		       inode->i_sb->s_id, function, line, inode->i_ino,
		       current->comm, &vaf);
	va_end(args);

	ext4_handle_error(inode->i_sb);
}

void ext4_error_file(struct file *file, const char *function,
		     unsigned int line, ext4_fsblk_t block,
		     const char *fmt, ...)
{
	va_list args;
	struct va_format vaf;
	struct ext4_super_block *es;
	struct inode *inode = file_inode(file);
	char pathname[80], *path;

	es = EXT4_SB(inode->i_sb)->s_es;
	es->s_last_error_ino = cpu_to_le32(inode->i_ino);
	save_error_info(inode->i_sb, function, line);
	path = d_path(&(file->f_path), pathname, sizeof(pathname));
	if (IS_ERR(path))
		path = "(unknown)";
	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	if (block)
		printk(KERN_CRIT
		       "EXT4-fs error (device %s): %s:%d: inode #%lu: "
		       "block %llu: comm %s: path %s: %pV\n",
		       inode->i_sb->s_id, function, line, inode->i_ino,
		       block, current->comm, path, &vaf);
	else
		printk(KERN_CRIT
		       "EXT4-fs error (device %s): %s:%d: inode #%lu: "
		       "comm %s: path %s: %pV\n",
		       inode->i_sb->s_id, function, line, inode->i_ino,
		       current->comm, path, &vaf);
	va_end(args);

	ext4_handle_error(inode->i_sb);
}

const char *ext4_decode_error(struct super_block *sb, int errno,
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

void ext4_msg(struct super_block *sb, const char *prefix, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	printk("%sEXT4-fs (%s): %pV\n", prefix, sb->s_id, &vaf);
	va_end(args);
}

void __ext4_warning(struct super_block *sb, const char *function,
		    unsigned int line, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);
	vaf.fmt = fmt;
	vaf.va = &args;
	printk(KERN_WARNING "EXT4-fs warning (device %s): %s:%d: %pV\n",
	       sb->s_id, function, line, &vaf);
	va_end(args);
}

void __ext4_grp_locked_error(const char *function, unsigned int line,
			     struct super_block *sb, ext4_group_t grp,
			     unsigned long ino, ext4_fsblk_t block,
			     const char *fmt, ...)
__releases(bitlock)
__acquires(bitlock)
{
	struct va_format vaf;
	va_list args;
	struct ext4_super_block *es = EXT4_SB(sb)->s_es;

	es->s_last_error_ino = cpu_to_le32(ino);
	es->s_last_error_block = cpu_to_le64(block);
	__save_error_info(sb, function, line);

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;
	printk(KERN_CRIT "EXT4-fs error (device %s): %s:%d: group %u, ",
	       sb->s_id, function, line, grp);
	if (ino)
		printk(KERN_CONT "inode %lu: ", ino);
	if (block)
		printk(KERN_CONT "block %llu:", (unsigned long long) block);
	printk(KERN_CONT "%pV\n", &vaf);
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
	 * ext4_grp_locked_error() to distinguish between the
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

	bdev = blkdev_get_by_dev(dev, FMODE_READ|FMODE_WRITE|FMODE_EXCL, sb);
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
static void ext4_blkdev_put(struct block_device *bdev)
{
	blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
}

static void ext4_blkdev_remove(struct ext4_sb_info *sbi)
{
	struct block_device *bdev;
	bdev = sbi->journal_bdev;
	if (bdev) {
		ext4_blkdev_put(bdev);
		sbi->journal_bdev = NULL;
	}
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

	ext4_unregister_li_request(sb);
	dquot_disable(sb, -1, DQUOT_USAGE_ENABLED | DQUOT_LIMITS_ENABLED);

	flush_workqueue(sbi->dio_unwritten_wq);
	destroy_workqueue(sbi->dio_unwritten_wq);

	if (sbi->s_journal) {
		err = jbd2_journal_destroy(sbi->s_journal);
		sbi->s_journal = NULL;
		if (err < 0)
			ext4_abort(sb, "Couldn't clean up the journal");
	}

	ext4_es_unregister_shrinker(sb);
	del_timer(&sbi->s_err_report);
	ext4_release_system_zone(sb);
	ext4_mb_release(sb);
	ext4_ext_release(sb);
	ext4_xattr_put_super(sb);

	if (!(sb->s_flags & MS_RDONLY)) {
		EXT4_CLEAR_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER);
		es->s_state = cpu_to_le16(sbi->s_mount_state);
	}
	if (!(sb->s_flags & MS_RDONLY))
		ext4_commit_super(sb, 1);

	if (sbi->s_proc) {
		remove_proc_entry("options", sbi->s_proc);
		remove_proc_entry(sb->s_id, ext4_proc_root);
	}
	kobject_del(&sbi->s_kobj);

	for (i = 0; i < sbi->s_gdb_count; i++)
		brelse(sbi->s_group_desc[i]);
	ext4_kvfree(sbi->s_group_desc);
	ext4_kvfree(sbi->s_flex_groups);
	percpu_counter_destroy(&sbi->s_freeclusters_counter);
	percpu_counter_destroy(&sbi->s_freeinodes_counter);
	percpu_counter_destroy(&sbi->s_dirs_counter);
	percpu_counter_destroy(&sbi->s_dirtyclusters_counter);
	percpu_counter_destroy(&sbi->s_extent_cache_cnt);
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
	if (sbi->s_mmp_tsk)
		kthread_stop(sbi->s_mmp_tsk);
	sb->s_fs_info = NULL;
	/*
	 * Now that we are completely done shutting down the
	 * superblock, we need to actually destroy the kobject.
	 */
	kobject_put(&sbi->s_kobj);
	wait_for_completion(&sbi->s_kobj_unregister);
	if (sbi->s_chksum_driver)
		crypto_free_shash(sbi->s_chksum_driver);
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
	INIT_LIST_HEAD(&ei->i_prealloc_list);
	spin_lock_init(&ei->i_prealloc_lock);
	ext4_es_init_tree(&ei->i_es_tree);
	rwlock_init(&ei->i_es_lock);
	INIT_LIST_HEAD(&ei->i_es_lru);
	ei->i_es_lru_nr = 0;
	ei->i_reserved_data_blocks = 0;
	ei->i_reserved_meta_blocks = 0;
	ei->i_allocated_meta_blocks = 0;
	ei->i_da_metadata_calc_len = 0;
	ei->i_da_metadata_calc_last_lblock = 0;
	spin_lock_init(&(ei->i_block_reservation_lock));
#ifdef CONFIG_QUOTA
	ei->i_reserved_quota = 0;
#endif
	ei->jinode = NULL;
	INIT_LIST_HEAD(&ei->i_completed_io_list);
	spin_lock_init(&ei->i_completed_io_lock);
	ei->i_sync_tid = 0;
	ei->i_datasync_tid = 0;
	atomic_set(&ei->i_ioend_count, 0);
	atomic_set(&ei->i_unwritten, 0);
	INIT_WORK(&ei->i_unwritten_work, ext4_end_io_work);

	return &ei->vfs_inode;
}

static int ext4_drop_inode(struct inode *inode)
{
	int drop = generic_drop_inode(inode);

	trace_ext4_drop_inode(inode, drop);
	return drop;
}

static void ext4_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(ext4_inode_cachep, EXT4_I(inode));
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
	call_rcu(&inode->i_rcu, ext4_i_callback);
}

static void init_once(void *foo)
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *) foo;

	INIT_LIST_HEAD(&ei->i_orphan);
	init_rwsem(&ei->xattr_sem);
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
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();
	kmem_cache_destroy(ext4_inode_cachep);
}

void ext4_clear_inode(struct inode *inode)
{
	invalidate_inode_buffers(inode);
	clear_inode(inode);
	dquot_drop(inode);
	ext4_discard_preallocations(inode);
	ext4_es_remove_extent(inode, 0, EXT_MAX_BLOCKS);
	ext4_es_lru_del(inode);
	if (EXT4_I(inode)->jinode) {
		jbd2_journal_release_jbd_inode(EXT4_JOURNAL(inode),
					       EXT4_I(inode)->jinode);
		jbd2_free_inode(EXT4_I(inode)->jinode);
		EXT4_I(inode)->jinode = NULL;
	}
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
	inode = ext4_iget_normal(sb, ino);
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
			 struct path *path);
static int ext4_quota_on_sysfile(struct super_block *sb, int type,
				 int format_id);
static int ext4_quota_off(struct super_block *sb, int type);
static int ext4_quota_off_sysfile(struct super_block *sb, int type);
static int ext4_quota_on_mount(struct super_block *sb, int type);
static ssize_t ext4_quota_read(struct super_block *sb, int type, char *data,
			       size_t len, loff_t off);
static ssize_t ext4_quota_write(struct super_block *sb, int type,
				const char *data, size_t len, loff_t off);
static int ext4_quota_enable(struct super_block *sb, int type, int format_id,
			     unsigned int flags);
static int ext4_enable_quotas(struct super_block *sb);

static const struct dquot_operations ext4_quota_operations = {
	.get_reserved_space = ext4_get_reserved_space,
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

static const struct quotactl_ops ext4_qctl_sysfile_operations = {
	.quota_on_meta	= ext4_quota_on_sysfile,
	.quota_off	= ext4_quota_off_sysfile,
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
	.drop_inode	= ext4_drop_inode,
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
	.drop_inode	= ext4_drop_inode,
	.evict_inode	= ext4_evict_inode,
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
	Opt_nouid32, Opt_debug, Opt_removed,
	Opt_user_xattr, Opt_nouser_xattr, Opt_acl, Opt_noacl,
	Opt_auto_da_alloc, Opt_noauto_da_alloc, Opt_noload,
	Opt_commit, Opt_min_batch_time, Opt_max_batch_time,
	Opt_journal_dev, Opt_journal_checksum, Opt_journal_async_commit,
	Opt_abort, Opt_data_journal, Opt_data_ordered, Opt_data_writeback,
	Opt_data_err_abort, Opt_data_err_ignore,
	Opt_usrjquota, Opt_grpjquota, Opt_offusrjquota, Opt_offgrpjquota,
	Opt_jqfmt_vfsold, Opt_jqfmt_vfsv0, Opt_jqfmt_vfsv1, Opt_quota,
	Opt_noquota, Opt_barrier, Opt_nobarrier, Opt_err,
	Opt_usrquota, Opt_grpquota, Opt_i_version,
	Opt_stripe, Opt_delalloc, Opt_nodelalloc, Opt_mblk_io_submit,
	Opt_nomblk_io_submit, Opt_block_validity, Opt_noblock_validity,
	Opt_inode_readahead_blks, Opt_journal_ioprio,
	Opt_dioread_nolock, Opt_dioread_lock,
	Opt_discard, Opt_nodiscard, Opt_init_itable, Opt_noinit_itable,
	Opt_max_dir_size_kb,
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
	{Opt_removed, "oldalloc"},
	{Opt_removed, "orlov"},
	{Opt_user_xattr, "user_xattr"},
	{Opt_nouser_xattr, "nouser_xattr"},
	{Opt_acl, "acl"},
	{Opt_noacl, "noacl"},
	{Opt_noload, "norecovery"},
	{Opt_noload, "noload"},
	{Opt_removed, "nobh"},
	{Opt_removed, "bh"},
	{Opt_commit, "commit=%u"},
	{Opt_min_batch_time, "min_batch_time=%u"},
	{Opt_max_batch_time, "max_batch_time=%u"},
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
	{Opt_delalloc, "delalloc"},
	{Opt_nodelalloc, "nodelalloc"},
	{Opt_removed, "mblk_io_submit"},
	{Opt_removed, "nomblk_io_submit"},
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
	{Opt_init_itable, "init_itable=%u"},
	{Opt_init_itable, "init_itable"},
	{Opt_noinit_itable, "noinit_itable"},
	{Opt_max_dir_size_kb, "max_dir_size_kb=%u"},
	{Opt_removed, "check=none"},	/* mount option from ext2/3 */
	{Opt_removed, "nocheck"},	/* mount option from ext2/3 */
	{Opt_removed, "reservation"},	/* mount option from ext2/3 */
	{Opt_removed, "noreservation"}, /* mount option from ext2/3 */
	{Opt_removed, "journal=%u"},	/* mount option from ext2/3 */
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
	int ret = -1;

	if (sb_any_quota_loaded(sb) &&
		!sbi->s_qf_names[qtype]) {
		ext4_msg(sb, KERN_ERR,
			"Cannot change journaled "
			"quota options when quota turned on");
		return -1;
	}
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_QUOTA)) {
		ext4_msg(sb, KERN_ERR, "Cannot set journaled quota options "
			 "when QUOTA feature is enabled");
		return -1;
	}
	qname = match_strdup(args);
	if (!qname) {
		ext4_msg(sb, KERN_ERR,
			"Not enough memory for storing quotafile name");
		return -1;
	}
	if (sbi->s_qf_names[qtype]) {
		if (strcmp(sbi->s_qf_names[qtype], qname) == 0)
			ret = 1;
		else
			ext4_msg(sb, KERN_ERR,
				 "%s quota file already specified",
				 QTYPE2NAME(qtype));
		goto errout;
	}
	if (strchr(qname, '/')) {
		ext4_msg(sb, KERN_ERR,
			"quotafile must be on filesystem root");
		goto errout;
	}
	sbi->s_qf_names[qtype] = qname;
	set_opt(sb, QUOTA);
	return 1;
errout:
	kfree(qname);
	return ret;
}

static int clear_qf_name(struct super_block *sb, int qtype)
{

	struct ext4_sb_info *sbi = EXT4_SB(sb);

	if (sb_any_quota_loaded(sb) &&
		sbi->s_qf_names[qtype]) {
		ext4_msg(sb, KERN_ERR, "Cannot change journaled quota options"
			" when quota turned on");
		return -1;
	}
	kfree(sbi->s_qf_names[qtype]);
	sbi->s_qf_names[qtype] = NULL;
	return 1;
}
#endif

#define MOPT_SET	0x0001
#define MOPT_CLEAR	0x0002
#define MOPT_NOSUPPORT	0x0004
#define MOPT_EXPLICIT	0x0008
#define MOPT_CLEAR_ERR	0x0010
#define MOPT_GTE0	0x0020
#ifdef CONFIG_QUOTA
#define MOPT_Q		0
#define MOPT_QFMT	0x0040
#else
#define MOPT_Q		MOPT_NOSUPPORT
#define MOPT_QFMT	MOPT_NOSUPPORT
#endif
#define MOPT_DATAJ	0x0080
#define MOPT_NO_EXT2	0x0100
#define MOPT_NO_EXT3	0x0200
#define MOPT_EXT4_ONLY	(MOPT_NO_EXT2 | MOPT_NO_EXT3)

static const struct mount_opts {
	int	token;
	int	mount_opt;
	int	flags;
} ext4_mount_opts[] = {
	{Opt_minix_df, EXT4_MOUNT_MINIX_DF, MOPT_SET},
	{Opt_bsd_df, EXT4_MOUNT_MINIX_DF, MOPT_CLEAR},
	{Opt_grpid, EXT4_MOUNT_GRPID, MOPT_SET},
	{Opt_nogrpid, EXT4_MOUNT_GRPID, MOPT_CLEAR},
	{Opt_block_validity, EXT4_MOUNT_BLOCK_VALIDITY, MOPT_SET},
	{Opt_noblock_validity, EXT4_MOUNT_BLOCK_VALIDITY, MOPT_CLEAR},
	{Opt_dioread_nolock, EXT4_MOUNT_DIOREAD_NOLOCK,
	 MOPT_EXT4_ONLY | MOPT_SET},
	{Opt_dioread_lock, EXT4_MOUNT_DIOREAD_NOLOCK,
	 MOPT_EXT4_ONLY | MOPT_CLEAR},
	{Opt_discard, EXT4_MOUNT_DISCARD, MOPT_SET},
	{Opt_nodiscard, EXT4_MOUNT_DISCARD, MOPT_CLEAR},
	{Opt_delalloc, EXT4_MOUNT_DELALLOC,
	 MOPT_EXT4_ONLY | MOPT_SET | MOPT_EXPLICIT},
	{Opt_nodelalloc, EXT4_MOUNT_DELALLOC,
	 MOPT_EXT4_ONLY | MOPT_CLEAR},
	{Opt_journal_checksum, EXT4_MOUNT_JOURNAL_CHECKSUM,
	 MOPT_EXT4_ONLY | MOPT_SET},
	{Opt_journal_async_commit, (EXT4_MOUNT_JOURNAL_ASYNC_COMMIT |
				    EXT4_MOUNT_JOURNAL_CHECKSUM),
	 MOPT_EXT4_ONLY | MOPT_SET},
	{Opt_noload, EXT4_MOUNT_NOLOAD, MOPT_NO_EXT2 | MOPT_SET},
	{Opt_err_panic, EXT4_MOUNT_ERRORS_PANIC, MOPT_SET | MOPT_CLEAR_ERR},
	{Opt_err_ro, EXT4_MOUNT_ERRORS_RO, MOPT_SET | MOPT_CLEAR_ERR},
	{Opt_err_cont, EXT4_MOUNT_ERRORS_CONT, MOPT_SET | MOPT_CLEAR_ERR},
	{Opt_data_err_abort, EXT4_MOUNT_DATA_ERR_ABORT,
	 MOPT_NO_EXT2 | MOPT_SET},
	{Opt_data_err_ignore, EXT4_MOUNT_DATA_ERR_ABORT,
	 MOPT_NO_EXT2 | MOPT_CLEAR},
	{Opt_barrier, EXT4_MOUNT_BARRIER, MOPT_SET},
	{Opt_nobarrier, EXT4_MOUNT_BARRIER, MOPT_CLEAR},
	{Opt_noauto_da_alloc, EXT4_MOUNT_NO_AUTO_DA_ALLOC, MOPT_SET},
	{Opt_auto_da_alloc, EXT4_MOUNT_NO_AUTO_DA_ALLOC, MOPT_CLEAR},
	{Opt_noinit_itable, EXT4_MOUNT_INIT_INODE_TABLE, MOPT_CLEAR},
	{Opt_commit, 0, MOPT_GTE0},
	{Opt_max_batch_time, 0, MOPT_GTE0},
	{Opt_min_batch_time, 0, MOPT_GTE0},
	{Opt_inode_readahead_blks, 0, MOPT_GTE0},
	{Opt_init_itable, 0, MOPT_GTE0},
	{Opt_stripe, 0, MOPT_GTE0},
	{Opt_resuid, 0, MOPT_GTE0},
	{Opt_resgid, 0, MOPT_GTE0},
	{Opt_journal_dev, 0, MOPT_GTE0},
	{Opt_journal_ioprio, 0, MOPT_GTE0},
	{Opt_data_journal, EXT4_MOUNT_JOURNAL_DATA, MOPT_NO_EXT2 | MOPT_DATAJ},
	{Opt_data_ordered, EXT4_MOUNT_ORDERED_DATA, MOPT_NO_EXT2 | MOPT_DATAJ},
	{Opt_data_writeback, EXT4_MOUNT_WRITEBACK_DATA,
	 MOPT_NO_EXT2 | MOPT_DATAJ},
	{Opt_user_xattr, EXT4_MOUNT_XATTR_USER, MOPT_SET},
	{Opt_nouser_xattr, EXT4_MOUNT_XATTR_USER, MOPT_CLEAR},
#ifdef CONFIG_EXT4_FS_POSIX_ACL
	{Opt_acl, EXT4_MOUNT_POSIX_ACL, MOPT_SET},
	{Opt_noacl, EXT4_MOUNT_POSIX_ACL, MOPT_CLEAR},
#else
	{Opt_acl, 0, MOPT_NOSUPPORT},
	{Opt_noacl, 0, MOPT_NOSUPPORT},
#endif
	{Opt_nouid32, EXT4_MOUNT_NO_UID32, MOPT_SET},
	{Opt_debug, EXT4_MOUNT_DEBUG, MOPT_SET},
	{Opt_quota, EXT4_MOUNT_QUOTA | EXT4_MOUNT_USRQUOTA, MOPT_SET | MOPT_Q},
	{Opt_usrquota, EXT4_MOUNT_QUOTA | EXT4_MOUNT_USRQUOTA,
							MOPT_SET | MOPT_Q},
	{Opt_grpquota, EXT4_MOUNT_QUOTA | EXT4_MOUNT_GRPQUOTA,
							MOPT_SET | MOPT_Q},
	{Opt_noquota, (EXT4_MOUNT_QUOTA | EXT4_MOUNT_USRQUOTA |
		       EXT4_MOUNT_GRPQUOTA), MOPT_CLEAR | MOPT_Q},
	{Opt_usrjquota, 0, MOPT_Q},
	{Opt_grpjquota, 0, MOPT_Q},
	{Opt_offusrjquota, 0, MOPT_Q},
	{Opt_offgrpjquota, 0, MOPT_Q},
	{Opt_jqfmt_vfsold, QFMT_VFS_OLD, MOPT_QFMT},
	{Opt_jqfmt_vfsv0, QFMT_VFS_V0, MOPT_QFMT},
	{Opt_jqfmt_vfsv1, QFMT_VFS_V1, MOPT_QFMT},
	{Opt_max_dir_size_kb, 0, MOPT_GTE0},
	{Opt_err, 0, 0}
};

static int handle_mount_opt(struct super_block *sb, char *opt, int token,
			    substring_t *args, unsigned long *journal_devnum,
			    unsigned int *journal_ioprio, int is_remount)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	const struct mount_opts *m;
	kuid_t uid;
	kgid_t gid;
	int arg = 0;

#ifdef CONFIG_QUOTA
	if (token == Opt_usrjquota)
		return set_qf_name(sb, USRQUOTA, &args[0]);
	else if (token == Opt_grpjquota)
		return set_qf_name(sb, GRPQUOTA, &args[0]);
	else if (token == Opt_offusrjquota)
		return clear_qf_name(sb, USRQUOTA);
	else if (token == Opt_offgrpjquota)
		return clear_qf_name(sb, GRPQUOTA);
#endif
	switch (token) {
	case Opt_noacl:
	case Opt_nouser_xattr:
		ext4_msg(sb, KERN_WARNING, deprecated_msg, opt, "3.5");
		break;
	case Opt_sb:
		return 1;	/* handled by get_sb_block() */
	case Opt_removed:
		ext4_msg(sb, KERN_WARNING, "Ignoring removed %s option", opt);
		return 1;
	case Opt_abort:
		sbi->s_mount_flags |= EXT4_MF_FS_ABORTED;
		return 1;
	case Opt_i_version:
		sb->s_flags |= MS_I_VERSION;
		return 1;
	}

	for (m = ext4_mount_opts; m->token != Opt_err; m++)
		if (token == m->token)
			break;

	if (m->token == Opt_err) {
		ext4_msg(sb, KERN_ERR, "Unrecognized mount option \"%s\" "
			 "or missing value", opt);
		return -1;
	}

	if ((m->flags & MOPT_NO_EXT2) && IS_EXT2_SB(sb)) {
		ext4_msg(sb, KERN_ERR,
			 "Mount option \"%s\" incompatible with ext2", opt);
		return -1;
	}
	if ((m->flags & MOPT_NO_EXT3) && IS_EXT3_SB(sb)) {
		ext4_msg(sb, KERN_ERR,
			 "Mount option \"%s\" incompatible with ext3", opt);
		return -1;
	}

	if (args->from && match_int(args, &arg))
		return -1;
	if (args->from && (m->flags & MOPT_GTE0) && (arg < 0))
		return -1;
	if (m->flags & MOPT_EXPLICIT)
		set_opt2(sb, EXPLICIT_DELALLOC);
	if (m->flags & MOPT_CLEAR_ERR)
		clear_opt(sb, ERRORS_MASK);
	if (token == Opt_noquota && sb_any_quota_loaded(sb)) {
		ext4_msg(sb, KERN_ERR, "Cannot change quota "
			 "options when quota turned on");
		return -1;
	}

	if (m->flags & MOPT_NOSUPPORT) {
		ext4_msg(sb, KERN_ERR, "%s option not supported", opt);
	} else if (token == Opt_commit) {
		if (arg == 0)
			arg = JBD2_DEFAULT_MAX_COMMIT_AGE;
		sbi->s_commit_interval = HZ * arg;
	} else if (token == Opt_max_batch_time) {
		sbi->s_max_batch_time = arg;
	} else if (token == Opt_min_batch_time) {
		sbi->s_min_batch_time = arg;
	} else if (token == Opt_inode_readahead_blks) {
		if (arg && (arg > (1 << 30) || !is_power_of_2(arg))) {
			ext4_msg(sb, KERN_ERR,
				 "EXT4-fs: inode_readahead_blks must be "
				 "0 or a power of 2 smaller than 2^31");
			return -1;
		}
		sbi->s_inode_readahead_blks = arg;
	} else if (token == Opt_init_itable) {
		set_opt(sb, INIT_INODE_TABLE);
		if (!args->from)
			arg = EXT4_DEF_LI_WAIT_MULT;
		sbi->s_li_wait_mult = arg;
	} else if (token == Opt_max_dir_size_kb) {
		sbi->s_max_dir_size_kb = arg;
	} else if (token == Opt_stripe) {
		sbi->s_stripe = arg;
	} else if (token == Opt_resuid) {
		uid = make_kuid(current_user_ns(), arg);
		if (!uid_valid(uid)) {
			ext4_msg(sb, KERN_ERR, "Invalid uid value %d", arg);
			return -1;
		}
		sbi->s_resuid = uid;
	} else if (token == Opt_resgid) {
		gid = make_kgid(current_user_ns(), arg);
		if (!gid_valid(gid)) {
			ext4_msg(sb, KERN_ERR, "Invalid gid value %d", arg);
			return -1;
		}
		sbi->s_resgid = gid;
	} else if (token == Opt_journal_dev) {
		if (is_remount) {
			ext4_msg(sb, KERN_ERR,
				 "Cannot specify journal on remount");
			return -1;
		}
		*journal_devnum = arg;
	} else if (token == Opt_journal_ioprio) {
		if (arg > 7) {
			ext4_msg(sb, KERN_ERR, "Invalid journal IO priority"
				 " (must be 0-7)");
			return -1;
		}
		*journal_ioprio =
			IOPRIO_PRIO_VALUE(IOPRIO_CLASS_BE, arg);
	} else if (m->flags & MOPT_DATAJ) {
		if (is_remount) {
			if (!sbi->s_journal)
				ext4_msg(sb, KERN_WARNING, "Remounting file system with no journal so ignoring journalled data option");
			else if (test_opt(sb, DATA_FLAGS) != m->mount_opt) {
				ext4_msg(sb, KERN_ERR,
					 "Cannot change data mode on remount");
				return -1;
			}
		} else {
			clear_opt(sb, DATA_FLAGS);
			sbi->s_mount_opt |= m->mount_opt;
		}
#ifdef CONFIG_QUOTA
	} else if (m->flags & MOPT_QFMT) {
		if (sb_any_quota_loaded(sb) &&
		    sbi->s_jquota_fmt != m->mount_opt) {
			ext4_msg(sb, KERN_ERR, "Cannot change journaled "
				 "quota options when quota turned on");
			return -1;
		}
		if (EXT4_HAS_RO_COMPAT_FEATURE(sb,
					       EXT4_FEATURE_RO_COMPAT_QUOTA)) {
			ext4_msg(sb, KERN_ERR,
				 "Cannot set journaled quota options "
				 "when QUOTA feature is enabled");
			return -1;
		}
		sbi->s_jquota_fmt = m->mount_opt;
#endif
	} else {
		if (!args->from)
			arg = 1;
		if (m->flags & MOPT_CLEAR)
			arg = !arg;
		else if (unlikely(!(m->flags & MOPT_SET))) {
			ext4_msg(sb, KERN_WARNING,
				 "buggy handling of option %s", opt);
			WARN_ON(1);
			return -1;
		}
		if (arg != 0)
			sbi->s_mount_opt |= m->mount_opt;
		else
			sbi->s_mount_opt &= ~m->mount_opt;
	}
	return 1;
}

static int parse_options(char *options, struct super_block *sb,
			 unsigned long *journal_devnum,
			 unsigned int *journal_ioprio,
			 int is_remount)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int token;

	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		if (!*p)
			continue;
		/*
		 * Initialize args struct so we know whether arg was
		 * found; some options take optional arguments.
		 */
		args[0].to = args[0].from = NULL;
		token = match_token(p, tokens, args);
		if (handle_mount_opt(sb, p, token, args, journal_devnum,
				     journal_ioprio, is_remount) < 0)
			return 0;
	}
#ifdef CONFIG_QUOTA
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_QUOTA) &&
	    (test_opt(sb, USRQUOTA) || test_opt(sb, GRPQUOTA))) {
		ext4_msg(sb, KERN_ERR, "Cannot set quota options when QUOTA "
			 "feature is enabled");
		return 0;
	}
	if (sbi->s_qf_names[USRQUOTA] || sbi->s_qf_names[GRPQUOTA]) {
		if (test_opt(sb, USRQUOTA) && sbi->s_qf_names[USRQUOTA])
			clear_opt(sb, USRQUOTA);

		if (test_opt(sb, GRPQUOTA) && sbi->s_qf_names[GRPQUOTA])
			clear_opt(sb, GRPQUOTA);

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
	}
#endif
	if (test_opt(sb, DIOREAD_NOLOCK)) {
		int blocksize =
			BLOCK_SIZE << le32_to_cpu(sbi->s_es->s_log_block_size);

		if (blocksize < PAGE_CACHE_SIZE) {
			ext4_msg(sb, KERN_ERR, "can't mount with "
				 "dioread_nolock if block size != PAGE_SIZE");
			return 0;
		}
	}
	return 1;
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
#endif
}

static const char *token2str(int token)
{
	const struct match_token *t;

	for (t = tokens; t->token != Opt_err; t++)
		if (t->token == token && !strchr(t->pattern, '='))
			break;
	return t->pattern;
}

/*
 * Show an option if
 *  - it's set to a non-default value OR
 *  - if the per-sb default is different from the global default
 */
static int _ext4_show_options(struct seq_file *seq, struct super_block *sb,
			      int nodefs)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_super_block *es = sbi->s_es;
	int def_errors, def_mount_opt = nodefs ? 0 : sbi->s_def_mount_opt;
	const struct mount_opts *m;
	char sep = nodefs ? '\n' : ',';

#define SEQ_OPTS_PUTS(str) seq_printf(seq, "%c" str, sep)
#define SEQ_OPTS_PRINT(str, arg) seq_printf(seq, "%c" str, sep, arg)

	if (sbi->s_sb_block != 1)
		SEQ_OPTS_PRINT("sb=%llu", sbi->s_sb_block);

	for (m = ext4_mount_opts; m->token != Opt_err; m++) {
		int want_set = m->flags & MOPT_SET;
		if (((m->flags & (MOPT_SET|MOPT_CLEAR)) == 0) ||
		    (m->flags & MOPT_CLEAR_ERR))
			continue;
		if (!(m->mount_opt & (sbi->s_mount_opt ^ def_mount_opt)))
			continue; /* skip if same as the default */
		if ((want_set &&
		     (sbi->s_mount_opt & m->mount_opt) != m->mount_opt) ||
		    (!want_set && (sbi->s_mount_opt & m->mount_opt)))
			continue; /* select Opt_noFoo vs Opt_Foo */
		SEQ_OPTS_PRINT("%s", token2str(m->token));
	}

	if (nodefs || !uid_eq(sbi->s_resuid, make_kuid(&init_user_ns, EXT4_DEF_RESUID)) ||
	    le16_to_cpu(es->s_def_resuid) != EXT4_DEF_RESUID)
		SEQ_OPTS_PRINT("resuid=%u",
				from_kuid_munged(&init_user_ns, sbi->s_resuid));
	if (nodefs || !gid_eq(sbi->s_resgid, make_kgid(&init_user_ns, EXT4_DEF_RESGID)) ||
	    le16_to_cpu(es->s_def_resgid) != EXT4_DEF_RESGID)
		SEQ_OPTS_PRINT("resgid=%u",
				from_kgid_munged(&init_user_ns, sbi->s_resgid));
	def_errors = nodefs ? -1 : le16_to_cpu(es->s_errors);
	if (test_opt(sb, ERRORS_RO) && def_errors != EXT4_ERRORS_RO)
		SEQ_OPTS_PUTS("errors=remount-ro");
	if (test_opt(sb, ERRORS_CONT) && def_errors != EXT4_ERRORS_CONTINUE)
		SEQ_OPTS_PUTS("errors=continue");
	if (test_opt(sb, ERRORS_PANIC) && def_errors != EXT4_ERRORS_PANIC)
		SEQ_OPTS_PUTS("errors=panic");
	if (nodefs || sbi->s_commit_interval != JBD2_DEFAULT_MAX_COMMIT_AGE*HZ)
		SEQ_OPTS_PRINT("commit=%lu", sbi->s_commit_interval / HZ);
	if (nodefs || sbi->s_min_batch_time != EXT4_DEF_MIN_BATCH_TIME)
		SEQ_OPTS_PRINT("min_batch_time=%u", sbi->s_min_batch_time);
	if (nodefs || sbi->s_max_batch_time != EXT4_DEF_MAX_BATCH_TIME)
		SEQ_OPTS_PRINT("max_batch_time=%u", sbi->s_max_batch_time);
	if (sb->s_flags & MS_I_VERSION)
		SEQ_OPTS_PUTS("i_version");
	if (nodefs || sbi->s_stripe)
		SEQ_OPTS_PRINT("stripe=%lu", sbi->s_stripe);
	if (EXT4_MOUNT_DATA_FLAGS & (sbi->s_mount_opt ^ def_mount_opt)) {
		if (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_JOURNAL_DATA)
			SEQ_OPTS_PUTS("data=journal");
		else if (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_ORDERED_DATA)
			SEQ_OPTS_PUTS("data=ordered");
		else if (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_WRITEBACK_DATA)
			SEQ_OPTS_PUTS("data=writeback");
	}
	if (nodefs ||
	    sbi->s_inode_readahead_blks != EXT4_DEF_INODE_READAHEAD_BLKS)
		SEQ_OPTS_PRINT("inode_readahead_blks=%u",
			       sbi->s_inode_readahead_blks);

	if (nodefs || (test_opt(sb, INIT_INODE_TABLE) &&
		       (sbi->s_li_wait_mult != EXT4_DEF_LI_WAIT_MULT)))
		SEQ_OPTS_PRINT("init_itable=%u", sbi->s_li_wait_mult);
	if (nodefs || sbi->s_max_dir_size_kb)
		SEQ_OPTS_PRINT("max_dir_size_kb=%u", sbi->s_max_dir_size_kb);

	ext4_show_quota_options(seq, sb);
	return 0;
}

static int ext4_show_options(struct seq_file *seq, struct dentry *root)
{
	return _ext4_show_options(seq, root->d_sb, 0);
}

static int options_seq_show(struct seq_file *seq, void *offset)
{
	struct super_block *sb = seq->private;
	int rc;

	seq_puts(seq, (sb->s_flags & MS_RDONLY) ? "ro" : "rw");
	rc = _ext4_show_options(seq, sb, 1);
	seq_puts(seq, "\n");
	return rc;
}

static int options_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, options_seq_show, PDE_DATA(inode));
}

static const struct file_operations ext4_seq_options_fops = {
	.owner = THIS_MODULE,
	.open = options_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

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
		goto done;
	if (!(sbi->s_mount_state & EXT4_VALID_FS))
		ext4_msg(sb, KERN_WARNING, "warning: mounting unchecked fs, "
			 "running e2fsck is recommended");
	else if ((sbi->s_mount_state & EXT4_ERROR_FS))
		ext4_msg(sb, KERN_WARNING,
			 "warning: mounting fs with errors, "
			 "running e2fsck is recommended");
	else if ((__s16) le16_to_cpu(es->s_max_mnt_count) > 0 &&
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
done:
	if (test_opt(sb, DEBUG))
		printk(KERN_INFO "[EXT4 FS bs=%lu, gc=%u, "
				"bpg=%lu, ipg=%lu, mo=%04x, mo2=%04x]\n",
			sb->s_blocksize,
			sbi->s_groups_count,
			EXT4_BLOCKS_PER_GROUP(sb),
			EXT4_INODES_PER_GROUP(sb),
			sbi->s_mount_opt, sbi->s_mount_opt2);

	cleancache_init_fs(sb);
	return res;
}

int ext4_alloc_flex_bg_array(struct super_block *sb, ext4_group_t ngroup)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct flex_groups *new_groups;
	int size;

	if (!sbi->s_log_groups_per_flex)
		return 0;

	size = ext4_flex_group(sbi, ngroup - 1) + 1;
	if (size <= sbi->s_flex_groups_allocated)
		return 0;

	size = roundup_pow_of_two(size * sizeof(struct flex_groups));
	new_groups = ext4_kvzalloc(size, GFP_KERNEL);
	if (!new_groups) {
		ext4_msg(sb, KERN_ERR, "not enough memory for %d flex groups",
			 size / (int) sizeof(struct flex_groups));
		return -ENOMEM;
	}

	if (sbi->s_flex_groups) {
		memcpy(new_groups, sbi->s_flex_groups,
		       (sbi->s_flex_groups_allocated *
			sizeof(struct flex_groups)));
		ext4_kvfree(sbi->s_flex_groups);
	}
	sbi->s_flex_groups = new_groups;
	sbi->s_flex_groups_allocated = size / sizeof(struct flex_groups);
	return 0;
}

static int ext4_fill_flex_info(struct super_block *sb)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_group_desc *gdp = NULL;
	ext4_group_t flex_group;
	unsigned int groups_per_flex = 0;
	int i, err;

	sbi->s_log_groups_per_flex = sbi->s_es->s_log_groups_per_flex;
	if (sbi->s_log_groups_per_flex < 1 || sbi->s_log_groups_per_flex > 31) {
		sbi->s_log_groups_per_flex = 0;
		return 1;
	}
	groups_per_flex = 1U << sbi->s_log_groups_per_flex;

	err = ext4_alloc_flex_bg_array(sb, sbi->s_groups_count);
	if (err)
		goto failed;

	for (i = 0; i < sbi->s_groups_count; i++) {
		gdp = ext4_get_group_desc(sb, i, NULL);

		flex_group = ext4_flex_group(sbi, i);
		atomic_add(ext4_free_inodes_count(sb, gdp),
			   &sbi->s_flex_groups[flex_group].free_inodes);
		atomic64_add(ext4_free_group_clusters(sb, gdp),
			     &sbi->s_flex_groups[flex_group].free_clusters);
		atomic_add(ext4_used_dirs_count(sb, gdp),
			   &sbi->s_flex_groups[flex_group].used_dirs);
	}

	return 1;
failed:
	return 0;
}

static __le16 ext4_group_desc_csum(struct ext4_sb_info *sbi, __u32 block_group,
				   struct ext4_group_desc *gdp)
{
	int offset;
	__u16 crc = 0;
	__le32 le_group = cpu_to_le32(block_group);

	if ((sbi->s_es->s_feature_ro_compat &
	     cpu_to_le32(EXT4_FEATURE_RO_COMPAT_METADATA_CSUM))) {
		/* Use new metadata_csum algorithm */
		__le16 save_csum;
		__u32 csum32;

		save_csum = gdp->bg_checksum;
		gdp->bg_checksum = 0;
		csum32 = ext4_chksum(sbi, sbi->s_csum_seed, (__u8 *)&le_group,
				     sizeof(le_group));
		csum32 = ext4_chksum(sbi, csum32, (__u8 *)gdp,
				     sbi->s_desc_size);
		gdp->bg_checksum = save_csum;

		crc = csum32 & 0xFFFF;
		goto out;
	}

	/* old crc16 code */
	if (!(sbi->s_es->s_feature_ro_compat &
	      cpu_to_le32(EXT4_FEATURE_RO_COMPAT_GDT_CSUM)))
		return 0;

	offset = offsetof(struct ext4_group_desc, bg_checksum);

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

out:
	return cpu_to_le16(crc);
}

int ext4_group_desc_csum_verify(struct super_block *sb, __u32 block_group,
				struct ext4_group_desc *gdp)
{
	if (ext4_has_group_desc_csum(sb) &&
	    (gdp->bg_checksum != ext4_group_desc_csum(EXT4_SB(sb),
						      block_group, gdp)))
		return 0;

	return 1;
}

void ext4_group_desc_csum_set(struct super_block *sb, __u32 block_group,
			      struct ext4_group_desc *gdp)
{
	if (!ext4_has_group_desc_csum(sb))
		return;
	gdp->bg_checksum = ext4_group_desc_csum(EXT4_SB(sb), block_group, gdp);
}

/* Called at mount-time, super-block is locked */
static int ext4_check_descriptors(struct super_block *sb,
				  ext4_group_t *first_not_zeroed)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	ext4_fsblk_t first_block = le32_to_cpu(sbi->s_es->s_first_data_block);
	ext4_fsblk_t last_block;
	ext4_fsblk_t block_bitmap;
	ext4_fsblk_t inode_bitmap;
	ext4_fsblk_t inode_table;
	int flexbg_flag = 0;
	ext4_group_t i, grp = sbi->s_groups_count;

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

		if ((grp == sbi->s_groups_count) &&
		   !(gdp->bg_flags & cpu_to_le16(EXT4_BG_INODE_ZEROED)))
			grp = i;

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
		if (!ext4_group_desc_csum_verify(sb, i, gdp)) {
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
	if (NULL != first_not_zeroed)
		*first_not_zeroed = grp;

	ext4_free_blocks_count_set(sbi->s_es,
				   EXT4_C2B(sbi, ext4_count_free_clusters(sb)));
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

	/* Check if feature set would not allow a r/w mount */
	if (!ext4_feature_set_ok(sb, 0)) {
		ext4_msg(sb, KERN_INFO, "Skipping orphan cleanup due to "
			 "unknown ROCOMPAT features");
		return;
	}

	if (EXT4_SB(sb)->s_mount_state & EXT4_ERROR_FS) {
		/* don't clear list on RO mount w/ errors */
		if (es->s_last_orphan && !(s_flags & MS_RDONLY)) {
			jbd_debug(1, "Errors on filesystem, "
				  "clearing orphan list.\n");
			es->s_last_orphan = 0;
		}
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
			mutex_lock(&inode->i_mutex);
			ext4_truncate(inode);
			mutex_unlock(&inode->i_mutex);
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
 * However there is other limiting factor. We do store extents in the form
 * of starting block and length, hence the resulting length of the extent
 * covering maximum file size must fit into on-disk format containers as
 * well. Given that length is always by 1 unit bigger than max unit (because
 * we count 0 as well) we have to lower the s_maxbytes by one fs block.
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

	/*
	 * 32-bit extent-start container, ee_block. We lower the maxbytes
	 * by one fs block, so ee_len can cover the extent of maximum file
	 * size
	 */
	res = (1LL << 32) - 1;
	res <<= blkbits;

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
	int ret;

	if (sbi->s_stripe && sbi->s_stripe <= sbi->s_blocks_per_group)
		ret = sbi->s_stripe;
	else if (stripe_width <= sbi->s_blocks_per_group)
		ret = stripe_width;
	else if (stride <= sbi->s_blocks_per_group)
		ret = stride;
	else
		ret = 0;

	/*
	 * If the stripe width is 1, this makes no sense and
	 * we set it to 0 to turn off stripe handling code.
	 */
	if (ret <= 1)
		ret = 0;

	return ret;
}

/* sysfs supprt */

struct ext4_attr {
	struct attribute attr;
	ssize_t (*show)(struct ext4_attr *, struct ext4_sb_info *, char *);
	ssize_t (*store)(struct ext4_attr *, struct ext4_sb_info *,
			 const char *, size_t);
	int offset;
};

static int parse_strtoull(const char *buf,
		unsigned long long max, unsigned long long *value)
{
	int ret;

	ret = kstrtoull(skip_spaces(buf), 0, value);
	if (!ret && *value > max)
		ret = -EINVAL;
	return ret;
}

static ssize_t delayed_allocation_blocks_show(struct ext4_attr *a,
					      struct ext4_sb_info *sbi,
					      char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n",
		(s64) EXT4_C2B(sbi,
			percpu_counter_sum(&sbi->s_dirtyclusters_counter)));
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
	int ret;

	ret = kstrtoul(skip_spaces(buf), 0, &t);
	if (ret)
		return ret;

	if (t && (!is_power_of_2(t) || t > 0x40000000))
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
	int ret;

	ret = kstrtoul(skip_spaces(buf), 0, &t);
	if (ret)
		return ret;
	*ui = t;
	return count;
}

static ssize_t reserved_clusters_show(struct ext4_attr *a,
				  struct ext4_sb_info *sbi, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%llu\n",
		(unsigned long long) atomic64_read(&sbi->s_resv_clusters));
}

static ssize_t reserved_clusters_store(struct ext4_attr *a,
				   struct ext4_sb_info *sbi,
				   const char *buf, size_t count)
{
	unsigned long long val;
	int ret;

	if (parse_strtoull(buf, -1ULL, &val))
		return -EINVAL;
	ret = ext4_reserve_clusters(sbi, val);

	return ret ? ret : count;
}

static ssize_t trigger_test_error(struct ext4_attr *a,
				  struct ext4_sb_info *sbi,
				  const char *buf, size_t count)
{
	int len = count;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (len && buf[len-1] == '\n')
		len--;

	if (len)
		ext4_error(sbi->s_sb, "%.*s", len, buf);
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

#define EXT4_INFO_ATTR(name) EXT4_ATTR(name, 0444, NULL, NULL)
#define EXT4_RO_ATTR(name) EXT4_ATTR(name, 0444, name##_show, NULL)
#define EXT4_RW_ATTR(name) EXT4_ATTR(name, 0644, name##_show, name##_store)
#define EXT4_RW_ATTR_SBI_UI(name, elname)	\
	EXT4_ATTR_OFFSET(name, 0644, sbi_ui_show, sbi_ui_store, elname)
#define ATTR_LIST(name) &ext4_attr_##name.attr

EXT4_RO_ATTR(delayed_allocation_blocks);
EXT4_RO_ATTR(session_write_kbytes);
EXT4_RO_ATTR(lifetime_write_kbytes);
EXT4_RW_ATTR(reserved_clusters);
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
EXT4_RW_ATTR_SBI_UI(extent_max_zeroout_kb, s_extent_max_zeroout_kb);
EXT4_ATTR(trigger_fs_error, 0200, NULL, trigger_test_error);

static struct attribute *ext4_attrs[] = {
	ATTR_LIST(delayed_allocation_blocks),
	ATTR_LIST(session_write_kbytes),
	ATTR_LIST(lifetime_write_kbytes),
	ATTR_LIST(reserved_clusters),
	ATTR_LIST(inode_readahead_blks),
	ATTR_LIST(inode_goal),
	ATTR_LIST(mb_stats),
	ATTR_LIST(mb_max_to_scan),
	ATTR_LIST(mb_min_to_scan),
	ATTR_LIST(mb_order2_req),
	ATTR_LIST(mb_stream_req),
	ATTR_LIST(mb_group_prealloc),
	ATTR_LIST(max_writeback_mb_bump),
	ATTR_LIST(extent_max_zeroout_kb),
	ATTR_LIST(trigger_fs_error),
	NULL,
};

/* Features this copy of ext4 supports */
EXT4_INFO_ATTR(lazy_itable_init);
EXT4_INFO_ATTR(batched_discard);
EXT4_INFO_ATTR(meta_bg_resize);

static struct attribute *ext4_feat_attrs[] = {
	ATTR_LIST(lazy_itable_init),
	ATTR_LIST(batched_discard),
	ATTR_LIST(meta_bg_resize),
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

static void ext4_feat_release(struct kobject *kobj)
{
	complete(&ext4_feat->f_kobj_unregister);
}

static struct kobj_type ext4_feat_ktype = {
	.default_attrs	= ext4_feat_attrs,
	.sysfs_ops	= &ext4_attr_ops,
	.release	= ext4_feat_release,
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
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_BIGALLOC) &&
	    !EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_EXTENTS)) {
		ext4_msg(sb, KERN_ERR,
			 "Can't support bigalloc feature without "
			 "extents feature\n");
		return 0;
	}

#ifndef CONFIG_QUOTA
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_QUOTA) &&
	    !readonly) {
		ext4_msg(sb, KERN_ERR,
			 "Filesystem with quota feature cannot be mounted RDWR "
			 "without CONFIG_QUOTA");
		return 0;
	}
#endif  /* CONFIG_QUOTA */
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
		/* fsck newer than v1.41.13 is needed to clean this condition. */
		ext4_msg(sb, KERN_NOTICE, "error count since last fsck: %u",
			 le32_to_cpu(es->s_error_count));
	if (es->s_first_error_time) {
		printk(KERN_NOTICE "EXT4-fs (%s): initial error at time %u: %.*s:%d",
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
		printk(KERN_NOTICE "EXT4-fs (%s): last error at time %u: %.*s:%d",
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

/* Find next suitable group and run ext4_init_inode_table */
static int ext4_run_li_request(struct ext4_li_request *elr)
{
	struct ext4_group_desc *gdp = NULL;
	ext4_group_t group, ngroups;
	struct super_block *sb;
	unsigned long timeout = 0;
	int ret = 0;

	sb = elr->lr_super;
	ngroups = EXT4_SB(sb)->s_groups_count;

	sb_start_write(sb);
	for (group = elr->lr_next_group; group < ngroups; group++) {
		gdp = ext4_get_group_desc(sb, group, NULL);
		if (!gdp) {
			ret = 1;
			break;
		}

		if (!(gdp->bg_flags & cpu_to_le16(EXT4_BG_INODE_ZEROED)))
			break;
	}

	if (group >= ngroups)
		ret = 1;

	if (!ret) {
		timeout = jiffies;
		ret = ext4_init_inode_table(sb, group,
					    elr->lr_timeout ? 0 : 1);
		if (elr->lr_timeout == 0) {
			timeout = (jiffies - timeout) *
				  elr->lr_sbi->s_li_wait_mult;
			elr->lr_timeout = timeout;
		}
		elr->lr_next_sched = jiffies + elr->lr_timeout;
		elr->lr_next_group = group + 1;
	}
	sb_end_write(sb);

	return ret;
}

/*
 * Remove lr_request from the list_request and free the
 * request structure. Should be called with li_list_mtx held
 */
static void ext4_remove_li_request(struct ext4_li_request *elr)
{
	struct ext4_sb_info *sbi;

	if (!elr)
		return;

	sbi = elr->lr_sbi;

	list_del(&elr->lr_request);
	sbi->s_li_request = NULL;
	kfree(elr);
}

static void ext4_unregister_li_request(struct super_block *sb)
{
	mutex_lock(&ext4_li_mtx);
	if (!ext4_li_info) {
		mutex_unlock(&ext4_li_mtx);
		return;
	}

	mutex_lock(&ext4_li_info->li_list_mtx);
	ext4_remove_li_request(EXT4_SB(sb)->s_li_request);
	mutex_unlock(&ext4_li_info->li_list_mtx);
	mutex_unlock(&ext4_li_mtx);
}

static struct task_struct *ext4_lazyinit_task;

/*
 * This is the function where ext4lazyinit thread lives. It walks
 * through the request list searching for next scheduled filesystem.
 * When such a fs is found, run the lazy initialization request
 * (ext4_rn_li_request) and keep track of the time spend in this
 * function. Based on that time we compute next schedule time of
 * the request. When walking through the list is complete, compute
 * next waking time and put itself into sleep.
 */
static int ext4_lazyinit_thread(void *arg)
{
	struct ext4_lazy_init *eli = (struct ext4_lazy_init *)arg;
	struct list_head *pos, *n;
	struct ext4_li_request *elr;
	unsigned long next_wakeup, cur;

	BUG_ON(NULL == eli);

cont_thread:
	while (true) {
		next_wakeup = MAX_JIFFY_OFFSET;

		mutex_lock(&eli->li_list_mtx);
		if (list_empty(&eli->li_request_list)) {
			mutex_unlock(&eli->li_list_mtx);
			goto exit_thread;
		}

		list_for_each_safe(pos, n, &eli->li_request_list) {
			elr = list_entry(pos, struct ext4_li_request,
					 lr_request);

			if (time_after_eq(jiffies, elr->lr_next_sched)) {
				if (ext4_run_li_request(elr) != 0) {
					/* error, remove the lazy_init job */
					ext4_remove_li_request(elr);
					continue;
				}
			}

			if (time_before(elr->lr_next_sched, next_wakeup))
				next_wakeup = elr->lr_next_sched;
		}
		mutex_unlock(&eli->li_list_mtx);

		try_to_freeze();

		cur = jiffies;
		if ((time_after_eq(cur, next_wakeup)) ||
		    (MAX_JIFFY_OFFSET == next_wakeup)) {
			cond_resched();
			continue;
		}

		schedule_timeout_interruptible(next_wakeup - cur);

		if (kthread_should_stop()) {
			ext4_clear_request_list();
			goto exit_thread;
		}
	}

exit_thread:
	/*
	 * It looks like the request list is empty, but we need
	 * to check it under the li_list_mtx lock, to prevent any
	 * additions into it, and of course we should lock ext4_li_mtx
	 * to atomically free the list and ext4_li_info, because at
	 * this point another ext4 filesystem could be registering
	 * new one.
	 */
	mutex_lock(&ext4_li_mtx);
	mutex_lock(&eli->li_list_mtx);
	if (!list_empty(&eli->li_request_list)) {
		mutex_unlock(&eli->li_list_mtx);
		mutex_unlock(&ext4_li_mtx);
		goto cont_thread;
	}
	mutex_unlock(&eli->li_list_mtx);
	kfree(ext4_li_info);
	ext4_li_info = NULL;
	mutex_unlock(&ext4_li_mtx);

	return 0;
}

static void ext4_clear_request_list(void)
{
	struct list_head *pos, *n;
	struct ext4_li_request *elr;

	mutex_lock(&ext4_li_info->li_list_mtx);
	list_for_each_safe(pos, n, &ext4_li_info->li_request_list) {
		elr = list_entry(pos, struct ext4_li_request,
				 lr_request);
		ext4_remove_li_request(elr);
	}
	mutex_unlock(&ext4_li_info->li_list_mtx);
}

static int ext4_run_lazyinit_thread(void)
{
	ext4_lazyinit_task = kthread_run(ext4_lazyinit_thread,
					 ext4_li_info, "ext4lazyinit");
	if (IS_ERR(ext4_lazyinit_task)) {
		int err = PTR_ERR(ext4_lazyinit_task);
		ext4_clear_request_list();
		kfree(ext4_li_info);
		ext4_li_info = NULL;
		printk(KERN_CRIT "EXT4-fs: error %d creating inode table "
				 "initialization thread\n",
				 err);
		return err;
	}
	ext4_li_info->li_state |= EXT4_LAZYINIT_RUNNING;
	return 0;
}

/*
 * Check whether it make sense to run itable init. thread or not.
 * If there is at least one uninitialized inode table, return
 * corresponding group number, else the loop goes through all
 * groups and return total number of groups.
 */
static ext4_group_t ext4_has_uninit_itable(struct super_block *sb)
{
	ext4_group_t group, ngroups = EXT4_SB(sb)->s_groups_count;
	struct ext4_group_desc *gdp = NULL;

	for (group = 0; group < ngroups; group++) {
		gdp = ext4_get_group_desc(sb, group, NULL);
		if (!gdp)
			continue;

		if (!(gdp->bg_flags & cpu_to_le16(EXT4_BG_INODE_ZEROED)))
			break;
	}

	return group;
}

static int ext4_li_info_new(void)
{
	struct ext4_lazy_init *eli = NULL;

	eli = kzalloc(sizeof(*eli), GFP_KERNEL);
	if (!eli)
		return -ENOMEM;

	INIT_LIST_HEAD(&eli->li_request_list);
	mutex_init(&eli->li_list_mtx);

	eli->li_state |= EXT4_LAZYINIT_QUIT;

	ext4_li_info = eli;

	return 0;
}

static struct ext4_li_request *ext4_li_request_new(struct super_block *sb,
					    ext4_group_t start)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_li_request *elr;
	unsigned long rnd;

	elr = kzalloc(sizeof(*elr), GFP_KERNEL);
	if (!elr)
		return NULL;

	elr->lr_super = sb;
	elr->lr_sbi = sbi;
	elr->lr_next_group = start;

	/*
	 * Randomize first schedule time of the request to
	 * spread the inode table initialization requests
	 * better.
	 */
	get_random_bytes(&rnd, sizeof(rnd));
	elr->lr_next_sched = jiffies + (unsigned long)rnd %
			     (EXT4_DEF_LI_MAX_START_DELAY * HZ);

	return elr;
}

int ext4_register_li_request(struct super_block *sb,
			     ext4_group_t first_not_zeroed)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_li_request *elr = NULL;
	ext4_group_t ngroups = EXT4_SB(sb)->s_groups_count;
	int ret = 0;

	mutex_lock(&ext4_li_mtx);
	if (sbi->s_li_request != NULL) {
		/*
		 * Reset timeout so it can be computed again, because
		 * s_li_wait_mult might have changed.
		 */
		sbi->s_li_request->lr_timeout = 0;
		goto out;
	}

	if (first_not_zeroed == ngroups ||
	    (sb->s_flags & MS_RDONLY) ||
	    !test_opt(sb, INIT_INODE_TABLE))
		goto out;

	elr = ext4_li_request_new(sb, first_not_zeroed);
	if (!elr) {
		ret = -ENOMEM;
		goto out;
	}

	if (NULL == ext4_li_info) {
		ret = ext4_li_info_new();
		if (ret)
			goto out;
	}

	mutex_lock(&ext4_li_info->li_list_mtx);
	list_add(&elr->lr_request, &ext4_li_info->li_request_list);
	mutex_unlock(&ext4_li_info->li_list_mtx);

	sbi->s_li_request = elr;
	/*
	 * set elr to NULL here since it has been inserted to
	 * the request_list and the removal and free of it is
	 * handled by ext4_clear_request_list from now on.
	 */
	elr = NULL;

	if (!(ext4_li_info->li_state & EXT4_LAZYINIT_RUNNING)) {
		ret = ext4_run_lazyinit_thread();
		if (ret)
			goto out;
	}
out:
	mutex_unlock(&ext4_li_mtx);
	if (ret)
		kfree(elr);
	return ret;
}

/*
 * We do not need to lock anything since this is called on
 * module unload.
 */
static void ext4_destroy_lazyinit_thread(void)
{
	/*
	 * If thread exited earlier
	 * there's nothing to be done.
	 */
	if (!ext4_li_info || !ext4_lazyinit_task)
		return;

	kthread_stop(ext4_lazyinit_task);
}

static int set_journal_csum_feature_set(struct super_block *sb)
{
	int ret = 1;
	int compat, incompat;
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	if (EXT4_HAS_RO_COMPAT_FEATURE(sb,
				       EXT4_FEATURE_RO_COMPAT_METADATA_CSUM)) {
		/* journal checksum v2 */
		compat = 0;
		incompat = JBD2_FEATURE_INCOMPAT_CSUM_V2;
	} else {
		/* journal checksum v1 */
		compat = JBD2_FEATURE_COMPAT_CHECKSUM;
		incompat = 0;
	}

	if (test_opt(sb, JOURNAL_ASYNC_COMMIT)) {
		ret = jbd2_journal_set_features(sbi->s_journal,
				compat, 0,
				JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT |
				incompat);
	} else if (test_opt(sb, JOURNAL_CHECKSUM)) {
		ret = jbd2_journal_set_features(sbi->s_journal,
				compat, 0,
				incompat);
		jbd2_journal_clear_features(sbi->s_journal, 0, 0,
				JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT);
	} else {
		jbd2_journal_clear_features(sbi->s_journal,
				JBD2_FEATURE_COMPAT_CHECKSUM, 0,
				JBD2_FEATURE_INCOMPAT_ASYNC_COMMIT |
				JBD2_FEATURE_INCOMPAT_CSUM_V2);
	}

	return ret;
}

/*
 * Note: calculating the overhead so we can be compatible with
 * historical BSD practice is quite difficult in the face of
 * clusters/bigalloc.  This is because multiple metadata blocks from
 * different block group can end up in the same allocation cluster.
 * Calculating the exact overhead in the face of clustered allocation
 * requires either O(all block bitmaps) in memory or O(number of block
 * groups**2) in time.  We will still calculate the superblock for
 * older file systems --- and if we come across with a bigalloc file
 * system with zero in s_overhead_clusters the estimate will be close to
 * correct especially for very large cluster sizes --- but for newer
 * file systems, it's better to calculate this figure once at mkfs
 * time, and store it in the superblock.  If the superblock value is
 * present (even for non-bigalloc file systems), we will use it.
 */
static int count_overhead(struct super_block *sb, ext4_group_t grp,
			  char *buf)
{
	struct ext4_sb_info	*sbi = EXT4_SB(sb);
	struct ext4_group_desc	*gdp;
	ext4_fsblk_t		first_block, last_block, b;
	ext4_group_t		i, ngroups = ext4_get_groups_count(sb);
	int			s, j, count = 0;

	if (!EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_BIGALLOC))
		return (ext4_bg_has_super(sb, grp) + ext4_bg_num_gdb(sb, grp) +
			sbi->s_itb_per_group + 2);

	first_block = le32_to_cpu(sbi->s_es->s_first_data_block) +
		(grp * EXT4_BLOCKS_PER_GROUP(sb));
	last_block = first_block + EXT4_BLOCKS_PER_GROUP(sb) - 1;
	for (i = 0; i < ngroups; i++) {
		gdp = ext4_get_group_desc(sb, i, NULL);
		b = ext4_block_bitmap(sb, gdp);
		if (b >= first_block && b <= last_block) {
			ext4_set_bit(EXT4_B2C(sbi, b - first_block), buf);
			count++;
		}
		b = ext4_inode_bitmap(sb, gdp);
		if (b >= first_block && b <= last_block) {
			ext4_set_bit(EXT4_B2C(sbi, b - first_block), buf);
			count++;
		}
		b = ext4_inode_table(sb, gdp);
		if (b >= first_block && b + sbi->s_itb_per_group <= last_block)
			for (j = 0; j < sbi->s_itb_per_group; j++, b++) {
				int c = EXT4_B2C(sbi, b - first_block);
				ext4_set_bit(c, buf);
				count++;
			}
		if (i != grp)
			continue;
		s = 0;
		if (ext4_bg_has_super(sb, grp)) {
			ext4_set_bit(s++, buf);
			count++;
		}
		for (j = ext4_bg_num_gdb(sb, grp); j > 0; j--) {
			ext4_set_bit(EXT4_B2C(sbi, s++), buf);
			count++;
		}
	}
	if (!count)
		return 0;
	return EXT4_CLUSTERS_PER_GROUP(sb) -
		ext4_count_free(buf, EXT4_CLUSTERS_PER_GROUP(sb) / 8);
}

/*
 * Compute the overhead and stash it in sbi->s_overhead
 */
int ext4_calculate_overhead(struct super_block *sb)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_super_block *es = sbi->s_es;
	ext4_group_t i, ngroups = ext4_get_groups_count(sb);
	ext4_fsblk_t overhead = 0;
	char *buf = (char *) get_zeroed_page(GFP_KERNEL);

	if (!buf)
		return -ENOMEM;

	/*
	 * Compute the overhead (FS structures).  This is constant
	 * for a given filesystem unless the number of block groups
	 * changes so we cache the previous value until it does.
	 */

	/*
	 * All of the blocks before first_data_block are overhead
	 */
	overhead = EXT4_B2C(sbi, le32_to_cpu(es->s_first_data_block));

	/*
	 * Add the overhead found in each block group
	 */
	for (i = 0; i < ngroups; i++) {
		int blks;

		blks = count_overhead(sb, i, buf);
		overhead += blks;
		if (blks)
			memset(buf, 0, PAGE_SIZE);
		cond_resched();
	}
	/* Add the journal blocks as well */
	if (sbi->s_journal)
		overhead += EXT4_NUM_B2C(sbi, sbi->s_journal->j_maxlen);

	sbi->s_overhead = overhead;
	smp_wmb();
	free_page((unsigned long) buf);
	return 0;
}


static ext4_fsblk_t ext4_calculate_resv_clusters(struct super_block *sb)
{
	ext4_fsblk_t resv_clusters;

	/*
	 * There's no need to reserve anything when we aren't using extents.
	 * The space estimates are exact, there are no unwritten extents,
	 * hole punching doesn't need new metadata... This is needed especially
	 * to keep ext2/3 backward compatibility.
	 */
	if (!EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_EXTENTS))
		return 0;
	/*
	 * By default we reserve 2% or 4096 clusters, whichever is smaller.
	 * This should cover the situations where we can not afford to run
	 * out of space like for example punch hole, or converting
	 * uninitialized extents in delalloc path. In most cases such
	 * allocation would require 1, or 2 blocks, higher numbers are
	 * very rare.
	 */
	resv_clusters = ext4_blocks_count(EXT4_SB(sb)->s_es) >>
			EXT4_SB(sb)->s_cluster_bits;

	do_div(resv_clusters, 50);
	resv_clusters = min_t(ext4_fsblk_t, resv_clusters, 4096);

	return resv_clusters;
}


static int ext4_reserve_clusters(struct ext4_sb_info *sbi, ext4_fsblk_t count)
{
	ext4_fsblk_t clusters = ext4_blocks_count(sbi->s_es) >>
				sbi->s_cluster_bits;

	if (count >= clusters)
		return -EINVAL;

	atomic64_set(&sbi->s_resv_clusters, count);
	return 0;
}

static int ext4_fill_super(struct super_block *sb, void *data, int silent)
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
	int blocksize, clustersize;
	unsigned int db_count;
	unsigned int i;
	int needs_recovery, has_huge_files, has_bigalloc;
	__u64 blocks_count;
	int err = 0;
	unsigned int journal_ioprio = DEFAULT_JOURNAL_IOPRIO;
	ext4_group_t first_not_zeroed;

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
	sbi->s_sb = sb;
	sbi->s_inode_readahead_blks = EXT4_DEF_INODE_READAHEAD_BLKS;
	sbi->s_sb_block = sb_block;
	if (sb->s_bdev->bd_part)
		sbi->s_sectors_written_start =
			part_stat_read(sb->s_bdev->bd_part, sectors[1]);

	/* Cleanup superblock name */
	for (cp = sb->s_id; (cp = strchr(cp, '/'));)
		*cp = '!';

	/* -EINVAL is default */
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
	es = (struct ext4_super_block *) (bh->b_data + offset);
	sbi->s_es = es;
	sb->s_magic = le16_to_cpu(es->s_magic);
	if (sb->s_magic != EXT4_SUPER_MAGIC)
		goto cantfind_ext4;
	sbi->s_kbytes_written = le64_to_cpu(es->s_kbytes_written);

	/* Warn if metadata_csum and gdt_csum are both set. */
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb,
				       EXT4_FEATURE_RO_COMPAT_METADATA_CSUM) &&
	    EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_GDT_CSUM))
		ext4_warning(sb, KERN_INFO "metadata_csum and uninit_bg are "
			     "redundant flags; please run fsck.");

	/* Check for a known checksum algorithm */
	if (!ext4_verify_csum_type(sb, es)) {
		ext4_msg(sb, KERN_ERR, "VFS: Found ext4 filesystem with "
			 "unknown checksum algorithm.");
		silent = 1;
		goto cantfind_ext4;
	}

	/* Load the checksum driver */
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb,
				       EXT4_FEATURE_RO_COMPAT_METADATA_CSUM)) {
		sbi->s_chksum_driver = crypto_alloc_shash("crc32c", 0, 0);
		if (IS_ERR(sbi->s_chksum_driver)) {
			ext4_msg(sb, KERN_ERR, "Cannot load crc32c driver.");
			ret = PTR_ERR(sbi->s_chksum_driver);
			sbi->s_chksum_driver = NULL;
			goto failed_mount;
		}
	}

	/* Check superblock checksum */
	if (!ext4_superblock_csum_verify(sb, es)) {
		ext4_msg(sb, KERN_ERR, "VFS: Found ext4 filesystem with "
			 "invalid superblock checksum.  Run e2fsck?");
		silent = 1;
		goto cantfind_ext4;
	}

	/* Precompute checksum seed for all metadata */
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb,
			EXT4_FEATURE_RO_COMPAT_METADATA_CSUM))
		sbi->s_csum_seed = ext4_chksum(sbi, ~0, es->s_uuid,
					       sizeof(es->s_uuid));

	/* Set defaults before we parse the mount options */
	def_mount_opts = le32_to_cpu(es->s_default_mount_opts);
	set_opt(sb, INIT_INODE_TABLE);
	if (def_mount_opts & EXT4_DEFM_DEBUG)
		set_opt(sb, DEBUG);
	if (def_mount_opts & EXT4_DEFM_BSDGROUPS)
		set_opt(sb, GRPID);
	if (def_mount_opts & EXT4_DEFM_UID16)
		set_opt(sb, NO_UID32);
	/* xattr user namespace & acls are now defaulted on */
	set_opt(sb, XATTR_USER);
#ifdef CONFIG_EXT4_FS_POSIX_ACL
	set_opt(sb, POSIX_ACL);
#endif
	if ((def_mount_opts & EXT4_DEFM_JMODE) == EXT4_DEFM_JMODE_DATA)
		set_opt(sb, JOURNAL_DATA);
	else if ((def_mount_opts & EXT4_DEFM_JMODE) == EXT4_DEFM_JMODE_ORDERED)
		set_opt(sb, ORDERED_DATA);
	else if ((def_mount_opts & EXT4_DEFM_JMODE) == EXT4_DEFM_JMODE_WBACK)
		set_opt(sb, WRITEBACK_DATA);

	if (le16_to_cpu(sbi->s_es->s_errors) == EXT4_ERRORS_PANIC)
		set_opt(sb, ERRORS_PANIC);
	else if (le16_to_cpu(sbi->s_es->s_errors) == EXT4_ERRORS_CONTINUE)
		set_opt(sb, ERRORS_CONT);
	else
		set_opt(sb, ERRORS_RO);
	if (def_mount_opts & EXT4_DEFM_BLOCK_VALIDITY)
		set_opt(sb, BLOCK_VALIDITY);
	if (def_mount_opts & EXT4_DEFM_DISCARD)
		set_opt(sb, DISCARD);

	sbi->s_resuid = make_kuid(&init_user_ns, le16_to_cpu(es->s_def_resuid));
	sbi->s_resgid = make_kgid(&init_user_ns, le16_to_cpu(es->s_def_resgid));
	sbi->s_commit_interval = JBD2_DEFAULT_MAX_COMMIT_AGE * HZ;
	sbi->s_min_batch_time = EXT4_DEF_MIN_BATCH_TIME;
	sbi->s_max_batch_time = EXT4_DEF_MAX_BATCH_TIME;

	if ((def_mount_opts & EXT4_DEFM_NOBARRIER) == 0)
		set_opt(sb, BARRIER);

	/*
	 * enable delayed allocation by default
	 * Use -o nodelalloc to turn it off
	 */
	if (!IS_EXT3_SB(sb) && !IS_EXT2_SB(sb) &&
	    ((def_mount_opts & EXT4_DEFM_NODELALLOC) == 0))
		set_opt(sb, DELALLOC);

	/*
	 * set default s_li_wait_mult for lazyinit, for the case there is
	 * no mount option specified.
	 */
	sbi->s_li_wait_mult = EXT4_DEF_LI_WAIT_MULT;

	if (!parse_options((char *) sbi->s_es->s_mount_opts, sb,
			   &journal_devnum, &journal_ioprio, 0)) {
		ext4_msg(sb, KERN_WARNING,
			 "failed to parse options in superblock: %s",
			 sbi->s_es->s_mount_opts);
	}
	sbi->s_def_mount_opt = sbi->s_mount_opt;
	if (!parse_options((char *) data, sb, &journal_devnum,
			   &journal_ioprio, 0))
		goto failed_mount;

	if (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_JOURNAL_DATA) {
		printk_once(KERN_WARNING "EXT4-fs: Warning: mounting "
			    "with data=journal disables delayed "
			    "allocation and O_DIRECT support!\n");
		if (test_opt2(sb, EXPLICIT_DELALLOC)) {
			ext4_msg(sb, KERN_ERR, "can't mount with "
				 "both data=journal and delalloc");
			goto failed_mount;
		}
		if (test_opt(sb, DIOREAD_NOLOCK)) {
			ext4_msg(sb, KERN_ERR, "can't mount with "
				 "both data=journal and dioread_nolock");
			goto failed_mount;
		}
		if (test_opt(sb, DELALLOC))
			clear_opt(sb, DELALLOC);
	}

	sb->s_flags = (sb->s_flags & ~MS_POSIXACL) |
		(test_opt(sb, POSIX_ACL) ? MS_POSIXACL : 0);

	if (le32_to_cpu(es->s_rev_level) == EXT4_GOOD_OLD_REV &&
	    (EXT4_HAS_COMPAT_FEATURE(sb, ~0U) ||
	     EXT4_HAS_RO_COMPAT_FEATURE(sb, ~0U) ||
	     EXT4_HAS_INCOMPAT_FEATURE(sb, ~0U)))
		ext4_msg(sb, KERN_WARNING,
		       "feature flags set on rev 0 fs, "
		       "running e2fsck is recommended");

	if (IS_EXT2_SB(sb)) {
		if (ext2_feature_set_ok(sb))
			ext4_msg(sb, KERN_INFO, "mounting ext2 file system "
				 "using the ext4 subsystem");
		else {
			ext4_msg(sb, KERN_ERR, "couldn't mount as ext2 due "
				 "to feature incompatibilities");
			goto failed_mount;
		}
	}

	if (IS_EXT3_SB(sb)) {
		if (ext3_feature_set_ok(sb))
			ext4_msg(sb, KERN_INFO, "mounting ext3 file system "
				 "using the ext4 subsystem");
		else {
			ext4_msg(sb, KERN_ERR, "couldn't mount as ext3 due "
				 "to feature incompatibilities");
			goto failed_mount;
		}
	}

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
		es = (struct ext4_super_block *)(bh->b_data + offset);
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
	if (EXT4_HAS_COMPAT_FEATURE(sb, EXT4_FEATURE_COMPAT_DIR_INDEX)) {
		i = le32_to_cpu(es->s_flags);
		if (i & EXT2_FLAGS_UNSIGNED_HASH)
			sbi->s_hash_unsigned = 3;
		else if ((i & EXT2_FLAGS_SIGNED_HASH) == 0) {
#ifdef __CHAR_UNSIGNED__
			if (!(sb->s_flags & MS_RDONLY))
				es->s_flags |=
					cpu_to_le32(EXT2_FLAGS_UNSIGNED_HASH);
			sbi->s_hash_unsigned = 3;
#else
			if (!(sb->s_flags & MS_RDONLY))
				es->s_flags |=
					cpu_to_le32(EXT2_FLAGS_SIGNED_HASH);
#endif
		}
	}

	/* Handle clustersize */
	clustersize = BLOCK_SIZE << le32_to_cpu(es->s_log_cluster_size);
	has_bigalloc = EXT4_HAS_RO_COMPAT_FEATURE(sb,
				EXT4_FEATURE_RO_COMPAT_BIGALLOC);
	if (has_bigalloc) {
		if (clustersize < blocksize) {
			ext4_msg(sb, KERN_ERR,
				 "cluster size (%d) smaller than "
				 "block size (%d)", clustersize, blocksize);
			goto failed_mount;
		}
		sbi->s_cluster_bits = le32_to_cpu(es->s_log_cluster_size) -
			le32_to_cpu(es->s_log_block_size);
		sbi->s_clusters_per_group =
			le32_to_cpu(es->s_clusters_per_group);
		if (sbi->s_clusters_per_group > blocksize * 8) {
			ext4_msg(sb, KERN_ERR,
				 "#clusters per group too big: %lu",
				 sbi->s_clusters_per_group);
			goto failed_mount;
		}
		if (sbi->s_blocks_per_group !=
		    (sbi->s_clusters_per_group * (clustersize / blocksize))) {
			ext4_msg(sb, KERN_ERR, "blocks per group (%lu) and "
				 "clusters per group (%lu) inconsistent",
				 sbi->s_blocks_per_group,
				 sbi->s_clusters_per_group);
			goto failed_mount;
		}
	} else {
		if (clustersize != blocksize) {
			ext4_warning(sb, "fragment/cluster size (%d) != "
				     "block size (%d)", clustersize,
				     blocksize);
			clustersize = blocksize;
		}
		if (sbi->s_blocks_per_group > blocksize * 8) {
			ext4_msg(sb, KERN_ERR,
				 "#blocks per group too big: %lu",
				 sbi->s_blocks_per_group);
			goto failed_mount;
		}
		sbi->s_clusters_per_group = sbi->s_blocks_per_group;
		sbi->s_cluster_bits = 0;
	}
	sbi->s_cluster_ratio = clustersize / blocksize;

	if (sbi->s_inodes_per_group > blocksize * 8) {
		ext4_msg(sb, KERN_ERR,
		       "#inodes per group too big: %lu",
		       sbi->s_inodes_per_group);
		goto failed_mount;
	}

	/* Do we have standard group size of clustersize * 8 blocks ? */
	if (sbi->s_blocks_per_group == clustersize << 3)
		set_opt2(sb, STD_GROUP_SIZE);

	/*
	 * Test whether we have more sectors than will fit in sector_t,
	 * and whether the max offset is addressable by the page cache.
	 */
	err = generic_check_addressable(sb->s_blocksize_bits,
					ext4_blocks_count(es));
	if (err) {
		ext4_msg(sb, KERN_ERR, "filesystem"
			 " too large to mount safely on this system");
		if (sizeof(sector_t) < 8)
			ext4_msg(sb, KERN_WARNING, "CONFIG_LBDAF not enabled");
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
		ext4_msg(sb, KERN_WARNING, "bad geometry: first data "
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
	sbi->s_group_desc = ext4_kvmalloc(db_count *
					  sizeof(struct buffer_head *),
					  GFP_KERNEL);
	if (sbi->s_group_desc == NULL) {
		ext4_msg(sb, KERN_ERR, "not enough memory");
		ret = -ENOMEM;
		goto failed_mount;
	}

	if (ext4_proc_root)
		sbi->s_proc = proc_mkdir(sb->s_id, ext4_proc_root);

	if (sbi->s_proc)
		proc_create_data("options", S_IRUGO, sbi->s_proc,
				 &ext4_seq_options_fops, sb);

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
	if (!ext4_check_descriptors(sb, &first_not_zeroed)) {
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

	init_timer(&sbi->s_err_report);
	sbi->s_err_report.function = print_daily_error_info;
	sbi->s_err_report.data = (unsigned long) sb;

	/* Register extent status tree shrinker */
	ext4_es_register_shrinker(sb);

	err = percpu_counter_init(&sbi->s_freeclusters_counter,
			ext4_count_free_clusters(sb));
	if (!err) {
		err = percpu_counter_init(&sbi->s_freeinodes_counter,
				ext4_count_free_inodes(sb));
	}
	if (!err) {
		err = percpu_counter_init(&sbi->s_dirs_counter,
				ext4_count_dirs(sb));
	}
	if (!err) {
		err = percpu_counter_init(&sbi->s_dirtyclusters_counter, 0);
	}
	if (!err) {
		err = percpu_counter_init(&sbi->s_extent_cache_cnt, 0);
	}
	if (err) {
		ext4_msg(sb, KERN_ERR, "insufficient memory");
		goto failed_mount3;
	}

	sbi->s_stripe = ext4_get_stripe_size(sbi);
	sbi->s_max_writeback_mb_bump = 128;
	sbi->s_extent_max_zeroout_kb = 32;

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
	sb->dq_op = &ext4_quota_operations;
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_QUOTA))
		sb->s_qcop = &ext4_qctl_sysfile_operations;
	else
		sb->s_qcop = &ext4_qctl_operations;
#endif
	memcpy(sb->s_uuid, es->s_uuid, sizeof(es->s_uuid));

	INIT_LIST_HEAD(&sbi->s_orphan); /* unlinked but open files */
	mutex_init(&sbi->s_orphan_lock);

	sb->s_root = NULL;

	needs_recovery = (es->s_last_orphan != 0 ||
			  EXT4_HAS_INCOMPAT_FEATURE(sb,
				    EXT4_FEATURE_INCOMPAT_RECOVER));

	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_MMP) &&
	    !(sb->s_flags & MS_RDONLY))
		if (ext4_multi_mount_protect(sb, le64_to_cpu(es->s_mmp_block)))
			goto failed_mount3;

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
		clear_opt(sb, DATA_FLAGS);
		sbi->s_journal = NULL;
		needs_recovery = 0;
		goto no_journal;
	}

	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_64BIT) &&
	    !jbd2_journal_set_features(EXT4_SB(sb)->s_journal, 0, 0,
				       JBD2_FEATURE_INCOMPAT_64BIT)) {
		ext4_msg(sb, KERN_ERR, "Failed to set 64-bit journal feature");
		goto failed_mount_wq;
	}

	if (!set_journal_csum_feature_set(sb)) {
		ext4_msg(sb, KERN_ERR, "Failed to set journal checksum "
			 "feature set");
		goto failed_mount_wq;
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
			set_opt(sb, ORDERED_DATA);
		else
			set_opt(sb, JOURNAL_DATA);
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

	sbi->s_journal->j_commit_callback = ext4_journal_commit_callback;

	/*
	 * The journal may have updated the bg summary counts, so we
	 * need to update the global counters.
	 */
	percpu_counter_set(&sbi->s_freeclusters_counter,
			   ext4_count_free_clusters(sb));
	percpu_counter_set(&sbi->s_freeinodes_counter,
			   ext4_count_free_inodes(sb));
	percpu_counter_set(&sbi->s_dirs_counter,
			   ext4_count_dirs(sb));
	percpu_counter_set(&sbi->s_dirtyclusters_counter, 0);

no_journal:
	/*
	 * Get the # of file system overhead blocks from the
	 * superblock if present.
	 */
	if (es->s_overhead_clusters)
		sbi->s_overhead = le32_to_cpu(es->s_overhead_clusters);
	else {
		err = ext4_calculate_overhead(sb);
		if (err)
			goto failed_mount_wq;
	}

	/*
	 * The maximum number of concurrent works can be high and
	 * concurrency isn't really necessary.  Limit it to 1.
	 */
	EXT4_SB(sb)->dio_unwritten_wq =
		alloc_workqueue("ext4-dio-unwritten", WQ_MEM_RECLAIM | WQ_UNBOUND, 1);
	if (!EXT4_SB(sb)->dio_unwritten_wq) {
		printk(KERN_ERR "EXT4-fs: failed to create DIO workqueue\n");
		ret = -ENOMEM;
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
		root = NULL;
		goto failed_mount4;
	}
	if (!S_ISDIR(root->i_mode) || !root->i_blocks || !root->i_size) {
		ext4_msg(sb, KERN_ERR, "corrupt root inode, run e2fsck");
		iput(root);
		goto failed_mount4;
	}
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		ext4_msg(sb, KERN_ERR, "get root dentry failed");
		ret = -ENOMEM;
		goto failed_mount4;
	}

	if (ext4_setup_super(sb, es, sb->s_flags & MS_RDONLY))
		sb->s_flags |= MS_RDONLY;

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

	err = ext4_reserve_clusters(sbi, ext4_calculate_resv_clusters(sb));
	if (err) {
		ext4_msg(sb, KERN_ERR, "failed to reserve %llu clusters for "
			 "reserved pool", ext4_calculate_resv_clusters(sb));
		goto failed_mount4a;
	}

	err = ext4_setup_system_zone(sb);
	if (err) {
		ext4_msg(sb, KERN_ERR, "failed to initialize system "
			 "zone (%d)", err);
		goto failed_mount4a;
	}

	ext4_ext_init(sb);
	err = ext4_mb_init(sb);
	if (err) {
		ext4_msg(sb, KERN_ERR, "failed to initialize mballoc (%d)",
			 err);
		goto failed_mount5;
	}

	err = ext4_register_li_request(sb, first_not_zeroed);
	if (err)
		goto failed_mount6;

	sbi->s_kobj.kset = ext4_kset;
	init_completion(&sbi->s_kobj_unregister);
	err = kobject_init_and_add(&sbi->s_kobj, &ext4_ktype, NULL,
				   "%s", sb->s_id);
	if (err)
		goto failed_mount7;

#ifdef CONFIG_QUOTA
	/* Enable quota usage during mount. */
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_QUOTA) &&
	    !(sb->s_flags & MS_RDONLY)) {
		err = ext4_enable_quotas(sb);
		if (err)
			goto failed_mount8;
	}
#endif  /* CONFIG_QUOTA */

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

	if (test_opt(sb, DISCARD)) {
		struct request_queue *q = bdev_get_queue(sb->s_bdev);
		if (!blk_queue_discard(q))
			ext4_msg(sb, KERN_WARNING,
				 "mounting with \"discard\" option, but "
				 "the device does not support discard");
	}

	ext4_msg(sb, KERN_INFO, "mounted filesystem with%s. "
		 "Opts: %s%s%s", descr, sbi->s_es->s_mount_opts,
		 *sbi->s_es->s_mount_opts ? "; " : "", orig_data);

	if (es->s_error_count)
		mod_timer(&sbi->s_err_report, jiffies + 300*HZ); /* 5 minutes */

	kfree(orig_data);
	return 0;

cantfind_ext4:
	if (!silent)
		ext4_msg(sb, KERN_ERR, "VFS: Can't find ext4 filesystem");
	goto failed_mount;

#ifdef CONFIG_QUOTA
failed_mount8:
	kobject_del(&sbi->s_kobj);
#endif
failed_mount7:
	ext4_unregister_li_request(sb);
failed_mount6:
	ext4_mb_release(sb);
failed_mount5:
	ext4_ext_release(sb);
	ext4_release_system_zone(sb);
failed_mount4a:
	dput(sb->s_root);
	sb->s_root = NULL;
failed_mount4:
	ext4_msg(sb, KERN_ERR, "mount failed");
	destroy_workqueue(EXT4_SB(sb)->dio_unwritten_wq);
failed_mount_wq:
	if (sbi->s_journal) {
		jbd2_journal_destroy(sbi->s_journal);
		sbi->s_journal = NULL;
	}
failed_mount3:
	ext4_es_unregister_shrinker(sb);
	del_timer(&sbi->s_err_report);
	if (sbi->s_flex_groups)
		ext4_kvfree(sbi->s_flex_groups);
	percpu_counter_destroy(&sbi->s_freeclusters_counter);
	percpu_counter_destroy(&sbi->s_freeinodes_counter);
	percpu_counter_destroy(&sbi->s_dirs_counter);
	percpu_counter_destroy(&sbi->s_dirtyclusters_counter);
	percpu_counter_destroy(&sbi->s_extent_cache_cnt);
	if (sbi->s_mmp_tsk)
		kthread_stop(sbi->s_mmp_tsk);
failed_mount2:
	for (i = 0; i < db_count; i++)
		brelse(sbi->s_group_desc[i]);
	ext4_kvfree(sbi->s_group_desc);
failed_mount:
	if (sbi->s_chksum_driver)
		crypto_free_shash(sbi->s_chksum_driver);
	if (sbi->s_proc) {
		remove_proc_entry("options", sbi->s_proc);
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
out_free_orig:
	kfree(orig_data);
	return err ? err : ret;
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

	es = (struct ext4_super_block *) (bh->b_data + offset);
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
	ll_rw_block(READ | REQ_META | REQ_PRIO, 1, &journal->j_sb_buffer);
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

	if (!really_read_only && journal_devnum &&
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

	if (!sbh || block_device_ejected(sb))
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
	ext4_free_blocks_count_set(es,
			EXT4_C2B(EXT4_SB(sb), percpu_counter_sum_positive(
				&EXT4_SB(sb)->s_freeclusters_counter)));
	es->s_free_inodes_count =
		cpu_to_le32(percpu_counter_sum_positive(
				&EXT4_SB(sb)->s_freeinodes_counter));
	BUFFER_TRACE(sbh, "marking dirty");
	ext4_superblock_csum_set(sb);
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
		jbd2_journal_update_sb_errno(journal);
	}
}

/*
 * Force the running and committing transactions to commit,
 * and wait on the commit.
 */
int ext4_force_commit(struct super_block *sb)
{
	journal_t *journal;

	if (sb->s_flags & MS_RDONLY)
		return 0;

	journal = EXT4_SB(sb)->s_journal;
	return ext4_journal_force_commit(journal);
}

static int ext4_sync_fs(struct super_block *sb, int wait)
{
	int ret = 0;
	tid_t target;
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	trace_ext4_sync_fs(sb, wait);
	flush_workqueue(sbi->dio_unwritten_wq);
	/*
	 * Writeback quota in non-journalled quota case - journalled quota has
	 * no dirty dquots
	 */
	dquot_writeback_dquots(sb, -1);
	if (jbd2_journal_start_commit(sbi->s_journal, &target)) {
		if (wait)
			jbd2_log_wait_commit(sbi->s_journal, target);
	}
	return ret;
}

/*
 * LVM calls this function before a (read-only) snapshot is created.  This
 * gives us a chance to flush the journal completely and mark the fs clean.
 *
 * Note that only this function cannot bring a filesystem to be in a clean
 * state independently. It relies on upper layer to stop all data & metadata
 * modifications.
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
	/* we rely on upper layer to stop further updates */
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

	/* Reset the needs_recovery flag before the fs is unlocked. */
	EXT4_SET_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER);
	ext4_commit_super(sb, 1);
	return 0;
}

/*
 * Structure to save mount options for ext4_remount's benefit
 */
struct ext4_mount_options {
	unsigned long s_mount_opt;
	unsigned long s_mount_opt2;
	kuid_t s_resuid;
	kgid_t s_resgid;
	unsigned long s_commit_interval;
	u32 s_min_batch_time, s_max_batch_time;
#ifdef CONFIG_QUOTA
	int s_jquota_fmt;
	char *s_qf_names[MAXQUOTAS];
#endif
};

static int ext4_remount(struct super_block *sb, int *flags, char *data)
{
	struct ext4_super_block *es;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	unsigned long old_sb_flags;
	struct ext4_mount_options old_opts;
	int enable_quota = 0;
	ext4_group_t g;
	unsigned int journal_ioprio = DEFAULT_JOURNAL_IOPRIO;
	int err = 0;
#ifdef CONFIG_QUOTA
	int i, j;
#endif
	char *orig_data = kstrdup(data, GFP_KERNEL);

	/* Store the original options */
	old_sb_flags = sb->s_flags;
	old_opts.s_mount_opt = sbi->s_mount_opt;
	old_opts.s_mount_opt2 = sbi->s_mount_opt2;
	old_opts.s_resuid = sbi->s_resuid;
	old_opts.s_resgid = sbi->s_resgid;
	old_opts.s_commit_interval = sbi->s_commit_interval;
	old_opts.s_min_batch_time = sbi->s_min_batch_time;
	old_opts.s_max_batch_time = sbi->s_max_batch_time;
#ifdef CONFIG_QUOTA
	old_opts.s_jquota_fmt = sbi->s_jquota_fmt;
	for (i = 0; i < MAXQUOTAS; i++)
		if (sbi->s_qf_names[i]) {
			old_opts.s_qf_names[i] = kstrdup(sbi->s_qf_names[i],
							 GFP_KERNEL);
			if (!old_opts.s_qf_names[i]) {
				for (j = 0; j < i; j++)
					kfree(old_opts.s_qf_names[j]);
				kfree(orig_data);
				return -ENOMEM;
			}
		} else
			old_opts.s_qf_names[i] = NULL;
#endif
	if (sbi->s_journal && sbi->s_journal->j_task->io_context)
		journal_ioprio = sbi->s_journal->j_task->io_context->ioprio;

	/*
	 * Allow the "check" option to be passed as a remount option.
	 */
	if (!parse_options(data, sb, NULL, &journal_ioprio, 1)) {
		err = -EINVAL;
		goto restore_opts;
	}

	if (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_JOURNAL_DATA) {
		if (test_opt2(sb, EXPLICIT_DELALLOC)) {
			ext4_msg(sb, KERN_ERR, "can't mount with "
				 "both data=journal and delalloc");
			err = -EINVAL;
			goto restore_opts;
		}
		if (test_opt(sb, DIOREAD_NOLOCK)) {
			ext4_msg(sb, KERN_ERR, "can't mount with "
				 "both data=journal and dioread_nolock");
			err = -EINVAL;
			goto restore_opts;
		}
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

	if ((*flags & MS_RDONLY) != (sb->s_flags & MS_RDONLY)) {
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

				if (!ext4_group_desc_csum_verify(sb, g, gdp)) {
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
			if (!ext4_setup_super(sb, es, 0))
				sb->s_flags &= ~MS_RDONLY;
			if (EXT4_HAS_INCOMPAT_FEATURE(sb,
						     EXT4_FEATURE_INCOMPAT_MMP))
				if (ext4_multi_mount_protect(sb,
						le64_to_cpu(es->s_mmp_block))) {
					err = -EROFS;
					goto restore_opts;
				}
			enable_quota = 1;
		}
	}

	/*
	 * Reinitialize lazy itable initialization thread based on
	 * current settings
	 */
	if ((sb->s_flags & MS_RDONLY) || !test_opt(sb, INIT_INODE_TABLE))
		ext4_unregister_li_request(sb);
	else {
		ext4_group_t first_not_zeroed;
		first_not_zeroed = ext4_has_uninit_itable(sb);
		ext4_register_li_request(sb, first_not_zeroed);
	}

	ext4_setup_system_zone(sb);
	if (sbi->s_journal == NULL && !(old_sb_flags & MS_RDONLY))
		ext4_commit_super(sb, 1);

#ifdef CONFIG_QUOTA
	/* Release old quota file names */
	for (i = 0; i < MAXQUOTAS; i++)
		kfree(old_opts.s_qf_names[i]);
	if (enable_quota) {
		if (sb_any_quota_suspended(sb))
			dquot_resume(sb, -1);
		else if (EXT4_HAS_RO_COMPAT_FEATURE(sb,
					EXT4_FEATURE_RO_COMPAT_QUOTA)) {
			err = ext4_enable_quotas(sb);
			if (err)
				goto restore_opts;
		}
	}
#endif

	ext4_msg(sb, KERN_INFO, "re-mounted. Opts: %s", orig_data);
	kfree(orig_data);
	return 0;

restore_opts:
	sb->s_flags = old_sb_flags;
	sbi->s_mount_opt = old_opts.s_mount_opt;
	sbi->s_mount_opt2 = old_opts.s_mount_opt2;
	sbi->s_resuid = old_opts.s_resuid;
	sbi->s_resgid = old_opts.s_resgid;
	sbi->s_commit_interval = old_opts.s_commit_interval;
	sbi->s_min_batch_time = old_opts.s_min_batch_time;
	sbi->s_max_batch_time = old_opts.s_max_batch_time;
#ifdef CONFIG_QUOTA
	sbi->s_jquota_fmt = old_opts.s_jquota_fmt;
	for (i = 0; i < MAXQUOTAS; i++) {
		kfree(sbi->s_qf_names[i]);
		sbi->s_qf_names[i] = old_opts.s_qf_names[i];
	}
#endif
	kfree(orig_data);
	return err;
}

static int ext4_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_super_block *es = sbi->s_es;
	ext4_fsblk_t overhead = 0, resv_blocks;
	u64 fsid;
	s64 bfree;
	resv_blocks = EXT4_C2B(sbi, atomic64_read(&sbi->s_resv_clusters));

	if (!test_opt(sb, MINIX_DF))
		overhead = sbi->s_overhead;

	buf->f_type = EXT4_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = ext4_blocks_count(es) - EXT4_C2B(sbi, overhead);
	bfree = percpu_counter_sum_positive(&sbi->s_freeclusters_counter) -
		percpu_counter_sum_positive(&sbi->s_dirtyclusters_counter);
	/* prevent underflow in case that few free space is available */
	buf->f_bfree = EXT4_C2B(sbi, max_t(s64, bfree, 0));
	buf->f_bavail = buf->f_bfree -
			(ext4_r_blocks_count(es) + resv_blocks);
	if (buf->f_bfree < (ext4_r_blocks_count(es) + resv_blocks))
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
	return sb_dqopt(dquot->dq_sb)->files[dquot->dq_id.type];
}

static int ext4_write_dquot(struct dquot *dquot)
{
	int ret, err;
	handle_t *handle;
	struct inode *inode;

	inode = dquot_to_inode(dquot);
	handle = ext4_journal_start(inode, EXT4_HT_QUOTA,
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

	handle = ext4_journal_start(dquot_to_inode(dquot), EXT4_HT_QUOTA,
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

	handle = ext4_journal_start(dquot_to_inode(dquot), EXT4_HT_QUOTA,
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
	struct super_block *sb = dquot->dq_sb;
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	/* Are we journaling quotas? */
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_QUOTA) ||
	    sbi->s_qf_names[USRQUOTA] || sbi->s_qf_names[GRPQUOTA]) {
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
	handle = ext4_journal_start(sb->s_root->d_inode, EXT4_HT_QUOTA, 2);
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
			 struct path *path)
{
	int err;

	if (!test_opt(sb, QUOTA))
		return -EINVAL;

	/* Quotafile not on the same filesystem? */
	if (path->dentry->d_sb != sb)
		return -EXDEV;
	/* Journaling quota? */
	if (EXT4_SB(sb)->s_qf_names[type]) {
		/* Quotafile not in fs root? */
		if (path->dentry->d_parent != sb->s_root)
			ext4_msg(sb, KERN_WARNING,
				"Quota file not on filesystem root. "
				"Journaled quota will not work");
	}

	/*
	 * When we journal data on quota file, we have to flush journal to see
	 * all updates to the file when we bypass pagecache...
	 */
	if (EXT4_SB(sb)->s_journal &&
	    ext4_should_journal_data(path->dentry->d_inode)) {
		/*
		 * We don't need to lock updates but journal_flush() could
		 * otherwise be livelocked...
		 */
		jbd2_journal_lock_updates(EXT4_SB(sb)->s_journal);
		err = jbd2_journal_flush(EXT4_SB(sb)->s_journal);
		jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
		if (err)
			return err;
	}

	return dquot_quota_on(sb, type, format_id, path);
}

static int ext4_quota_enable(struct super_block *sb, int type, int format_id,
			     unsigned int flags)
{
	int err;
	struct inode *qf_inode;
	unsigned long qf_inums[MAXQUOTAS] = {
		le32_to_cpu(EXT4_SB(sb)->s_es->s_usr_quota_inum),
		le32_to_cpu(EXT4_SB(sb)->s_es->s_grp_quota_inum)
	};

	BUG_ON(!EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_QUOTA));

	if (!qf_inums[type])
		return -EPERM;

	qf_inode = ext4_iget(sb, qf_inums[type]);
	if (IS_ERR(qf_inode)) {
		ext4_error(sb, "Bad quota inode # %lu", qf_inums[type]);
		return PTR_ERR(qf_inode);
	}

	/* Don't account quota for quota files to avoid recursion */
	qf_inode->i_flags |= S_NOQUOTA;
	err = dquot_enable(qf_inode, type, format_id, flags);
	iput(qf_inode);

	return err;
}

/* Enable usage tracking for all quota types. */
static int ext4_enable_quotas(struct super_block *sb)
{
	int type, err = 0;
	unsigned long qf_inums[MAXQUOTAS] = {
		le32_to_cpu(EXT4_SB(sb)->s_es->s_usr_quota_inum),
		le32_to_cpu(EXT4_SB(sb)->s_es->s_grp_quota_inum)
	};

	sb_dqopt(sb)->flags |= DQUOT_QUOTA_SYS_FILE;
	for (type = 0; type < MAXQUOTAS; type++) {
		if (qf_inums[type]) {
			err = ext4_quota_enable(sb, type, QFMT_VFS_V1,
						DQUOT_USAGE_ENABLED);
			if (err) {
				ext4_warning(sb,
					"Failed to enable quota tracking "
					"(type=%d, err=%d). Please run "
					"e2fsck to fix.", type, err);
				return err;
			}
		}
	}
	return 0;
}

/*
 * quota_on function that is used when QUOTA feature is set.
 */
static int ext4_quota_on_sysfile(struct super_block *sb, int type,
				 int format_id)
{
	if (!EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_QUOTA))
		return -EINVAL;

	/*
	 * USAGE was enabled at mount time. Only need to enable LIMITS now.
	 */
	return ext4_quota_enable(sb, type, format_id, DQUOT_LIMITS_ENABLED);
}

static int ext4_quota_off(struct super_block *sb, int type)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	handle_t *handle;

	/* Force all delayed allocation blocks to be allocated.
	 * Caller already holds s_umount sem */
	if (test_opt(sb, DELALLOC))
		sync_filesystem(sb);

	if (!inode)
		goto out;

	/* Update modification times of quota files when userspace can
	 * start looking at them */
	handle = ext4_journal_start(inode, EXT4_HT_QUOTA, 1);
	if (IS_ERR(handle))
		goto out;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	ext4_mark_inode_dirty(handle, inode);
	ext4_journal_stop(handle);

out:
	return dquot_quota_off(sb, type);
}

/*
 * quota_off function that is used when QUOTA feature is set.
 */
static int ext4_quota_off_sysfile(struct super_block *sb, int type)
{
	if (!EXT4_HAS_RO_COMPAT_FEATURE(sb, EXT4_FEATURE_RO_COMPAT_QUOTA))
		return -EINVAL;

	/* Disable only the limits. */
	return dquot_disable(sb, type, DQUOT_LIMITS_ENABLED);
}

/* Read data from quotafile - avoid pagecache and such because we cannot afford
 * acquiring the locks... As quota files are never truncated and quota code
 * itself serializes the operations (and no one else should touch the files)
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
	if (err)
		return err;
	if (inode->i_size < off + len) {
		i_size_write(inode, off + len);
		EXT4_I(inode)->i_disksize = inode->i_size;
		ext4_mark_inode_dirty(handle, inode);
	}
	return len;
}

#endif

static struct dentry *ext4_mount(struct file_system_type *fs_type, int flags,
		       const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, ext4_fill_super);
}

#if !defined(CONFIG_EXT2_FS) && !defined(CONFIG_EXT2_FS_MODULE) && defined(CONFIG_EXT4_USE_FOR_EXT23)
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

static inline int ext2_feature_set_ok(struct super_block *sb)
{
	if (EXT4_HAS_INCOMPAT_FEATURE(sb, ~EXT2_FEATURE_INCOMPAT_SUPP))
		return 0;
	if (sb->s_flags & MS_RDONLY)
		return 1;
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb, ~EXT2_FEATURE_RO_COMPAT_SUPP))
		return 0;
	return 1;
}
#else
static inline void register_as_ext2(void) { }
static inline void unregister_as_ext2(void) { }
static inline int ext2_feature_set_ok(struct super_block *sb) { return 0; }
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

static inline int ext3_feature_set_ok(struct super_block *sb)
{
	if (EXT4_HAS_INCOMPAT_FEATURE(sb, ~EXT3_FEATURE_INCOMPAT_SUPP))
		return 0;
	if (!EXT4_HAS_COMPAT_FEATURE(sb, EXT4_FEATURE_COMPAT_HAS_JOURNAL))
		return 0;
	if (sb->s_flags & MS_RDONLY)
		return 1;
	if (EXT4_HAS_RO_COMPAT_FEATURE(sb, ~EXT3_FEATURE_RO_COMPAT_SUPP))
		return 0;
	return 1;
}
#else
static inline void register_as_ext3(void) { }
static inline void unregister_as_ext3(void) { }
static inline int ext3_feature_set_ok(struct super_block *sb) { return 0; }
#endif

static struct file_system_type ext4_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ext4",
	.mount		= ext4_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("ext4");

static int __init ext4_init_feat_adverts(void)
{
	struct ext4_features *ef;
	int ret = -ENOMEM;

	ef = kzalloc(sizeof(struct ext4_features), GFP_KERNEL);
	if (!ef)
		goto out;

	ef->f_kobj.kset = ext4_kset;
	init_completion(&ef->f_kobj_unregister);
	ret = kobject_init_and_add(&ef->f_kobj, &ext4_feat_ktype, NULL,
				   "features");
	if (ret) {
		kfree(ef);
		goto out;
	}

	ext4_feat = ef;
	ret = 0;
out:
	return ret;
}

static void ext4_exit_feat_adverts(void)
{
	kobject_put(&ext4_feat->f_kobj);
	wait_for_completion(&ext4_feat->f_kobj_unregister);
	kfree(ext4_feat);
}

/* Shared across all ext4 file systems */
wait_queue_head_t ext4__ioend_wq[EXT4_WQ_HASH_SZ];
struct mutex ext4__aio_mutex[EXT4_WQ_HASH_SZ];

static int __init ext4_init_fs(void)
{
	int i, err;

	ext4_li_info = NULL;
	mutex_init(&ext4_li_mtx);

	/* Build-time check for flags consistency */
	ext4_check_flag_values();

	for (i = 0; i < EXT4_WQ_HASH_SZ; i++) {
		mutex_init(&ext4__aio_mutex[i]);
		init_waitqueue_head(&ext4__ioend_wq[i]);
	}

	err = ext4_init_es();
	if (err)
		return err;

	err = ext4_init_pageio();
	if (err)
		goto out7;

	err = ext4_init_system_zone();
	if (err)
		goto out6;
	ext4_kset = kset_create_and_add("ext4", NULL, fs_kobj);
	if (!ext4_kset) {
		err = -ENOMEM;
		goto out5;
	}
	ext4_proc_root = proc_mkdir("fs/ext4", NULL);

	err = ext4_init_feat_adverts();
	if (err)
		goto out4;

	err = ext4_init_mballoc();
	if (err)
		goto out3;

	err = ext4_init_xattr();
	if (err)
		goto out2;
	err = init_inodecache();
	if (err)
		goto out1;
	register_as_ext3();
	register_as_ext2();
	err = register_filesystem(&ext4_fs_type);
	if (err)
		goto out;

	return 0;
out:
	unregister_as_ext2();
	unregister_as_ext3();
	destroy_inodecache();
out1:
	ext4_exit_xattr();
out2:
	ext4_exit_mballoc();
out3:
	ext4_exit_feat_adverts();
out4:
	if (ext4_proc_root)
		remove_proc_entry("fs/ext4", NULL);
	kset_unregister(ext4_kset);
out5:
	ext4_exit_system_zone();
out6:
	ext4_exit_pageio();
out7:
	ext4_exit_es();

	return err;
}

static void __exit ext4_exit_fs(void)
{
	ext4_destroy_lazyinit_thread();
	unregister_as_ext2();
	unregister_as_ext3();
	unregister_filesystem(&ext4_fs_type);
	destroy_inodecache();
	ext4_exit_xattr();
	ext4_exit_mballoc();
	ext4_exit_feat_adverts();
	remove_proc_entry("fs/ext4", NULL);
	kset_unregister(ext4_kset);
	ext4_exit_system_zone();
	ext4_exit_pageio();
	ext4_exit_es();
}

MODULE_AUTHOR("Remy Card, Stephen Tweedie, Andrew Morton, Andreas Dilger, Theodore Ts'o and others");
MODULE_DESCRIPTION("Fourth Extended Filesystem");
MODULE_LICENSE("GPL");
module_init(ext4_init_fs)
module_exit(ext4_exit_fs)
