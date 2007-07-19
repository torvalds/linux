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
#include <linux/jbd2.h>
#include <linux/ext4_fs.h>
#include <linux/ext4_jbd2.h>
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
#include <linux/log2.h>

#include <asm/uaccess.h>

#include "xattr.h"
#include "acl.h"
#include "namei.h"

static int ext4_load_journal(struct super_block *, struct ext4_super_block *,
			     unsigned long journal_devnum);
static int ext4_create_journal(struct super_block *, struct ext4_super_block *,
			       unsigned int);
static void ext4_commit_super (struct super_block * sb,
			       struct ext4_super_block * es,
			       int sync);
static void ext4_mark_recovery_complete(struct super_block * sb,
					struct ext4_super_block * es);
static void ext4_clear_journal_err(struct super_block * sb,
				   struct ext4_super_block * es);
static int ext4_sync_fs(struct super_block *sb, int wait);
static const char *ext4_decode_error(struct super_block * sb, int errno,
				     char nbuf[16]);
static int ext4_remount (struct super_block * sb, int * flags, char * data);
static int ext4_statfs (struct dentry * dentry, struct kstatfs * buf);
static void ext4_unlockfs(struct super_block *sb);
static void ext4_write_super (struct super_block * sb);
static void ext4_write_super_lockfs(struct super_block *sb);


ext4_fsblk_t ext4_block_bitmap(struct super_block *sb,
			       struct ext4_group_desc *bg)
{
	return le32_to_cpu(bg->bg_block_bitmap) |
		(EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT ?
		 (ext4_fsblk_t)le32_to_cpu(bg->bg_block_bitmap_hi) << 32 : 0);
}

ext4_fsblk_t ext4_inode_bitmap(struct super_block *sb,
			       struct ext4_group_desc *bg)
{
	return le32_to_cpu(bg->bg_inode_bitmap) |
		(EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT ?
		 (ext4_fsblk_t)le32_to_cpu(bg->bg_inode_bitmap_hi) << 32 : 0);
}

ext4_fsblk_t ext4_inode_table(struct super_block *sb,
			      struct ext4_group_desc *bg)
{
	return le32_to_cpu(bg->bg_inode_table) |
		(EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT ?
		 (ext4_fsblk_t)le32_to_cpu(bg->bg_inode_table_hi) << 32 : 0);
}

void ext4_block_bitmap_set(struct super_block *sb,
			   struct ext4_group_desc *bg, ext4_fsblk_t blk)
{
	bg->bg_block_bitmap = cpu_to_le32((u32)blk);
	if (EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT)
		bg->bg_block_bitmap_hi = cpu_to_le32(blk >> 32);
}

void ext4_inode_bitmap_set(struct super_block *sb,
			   struct ext4_group_desc *bg, ext4_fsblk_t blk)
{
	bg->bg_inode_bitmap  = cpu_to_le32((u32)blk);
	if (EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT)
		bg->bg_inode_bitmap_hi = cpu_to_le32(blk >> 32);
}

void ext4_inode_table_set(struct super_block *sb,
			  struct ext4_group_desc *bg, ext4_fsblk_t blk)
{
	bg->bg_inode_table = cpu_to_le32((u32)blk);
	if (EXT4_DESC_SIZE(sb) >= EXT4_MIN_DESC_SIZE_64BIT)
		bg->bg_inode_table_hi = cpu_to_le32(blk >> 32);
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

	/* Special case here: if the journal has aborted behind our
	 * backs (eg. EIO in the commit thread), then we still need to
	 * take the FS itself readonly cleanly. */
	journal = EXT4_SB(sb)->s_journal;
	if (is_journal_aborted(journal)) {
		ext4_abort(sb, __FUNCTION__,
			   "Detected aborted journal");
		return ERR_PTR(-EROFS);
	}

	return jbd2_journal_start(journal, nblocks);
}

/*
 * The only special thing we need to do here is to make sure that all
 * jbd2_journal_stop calls result in the superblock being marked dirty, so
 * that sync() will call the filesystem's write_super callback if
 * appropriate.
 */
int __ext4_journal_stop(const char *where, handle_t *handle)
{
	struct super_block *sb;
	int err;
	int rc;

	sb = handle->h_transaction->t_journal->j_private;
	err = handle->h_err;
	rc = jbd2_journal_stop(handle);

	if (!err)
		err = rc;
	if (err)
		__ext4_std_error(sb, where, err);
	return err;
}

void ext4_journal_abort_handle(const char *caller, const char *err_fn,
		struct buffer_head *bh, handle_t *handle, int err)
{
	char nbuf[16];
	const char *errstr = ext4_decode_error(NULL, err, nbuf);

	if (bh)
		BUFFER_TRACE(bh, "abort");

	if (!handle->h_err)
		handle->h_err = err;

	if (is_handle_aborted(handle))
		return;

	printk(KERN_ERR "%s: aborting transaction: %s in %s\n",
	       caller, errstr, err_fn);

	jbd2_journal_abort_handle(handle);
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
 * the journal instead.  On recovery, the journal will compain about
 * that error until we've noted it down and cleared it.
 */

static void ext4_handle_error(struct super_block *sb)
{
	struct ext4_super_block *es = EXT4_SB(sb)->s_es;

	EXT4_SB(sb)->s_mount_state |= EXT4_ERROR_FS;
	es->s_state |= cpu_to_le16(EXT4_ERROR_FS);

	if (sb->s_flags & MS_RDONLY)
		return;

	if (!test_opt (sb, ERRORS_CONT)) {
		journal_t *journal = EXT4_SB(sb)->s_journal;

		EXT4_SB(sb)->s_mount_opt |= EXT4_MOUNT_ABORT;
		if (journal)
			jbd2_journal_abort(journal, -EIO);
	}
	if (test_opt (sb, ERRORS_RO)) {
		printk (KERN_CRIT "Remounting filesystem read-only\n");
		sb->s_flags |= MS_RDONLY;
	}
	ext4_commit_super(sb, es, 1);
	if (test_opt(sb, ERRORS_PANIC))
		panic("EXT4-fs (device %s): panic forced after error\n",
			sb->s_id);
}

void ext4_error (struct super_block * sb, const char * function,
		 const char * fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	printk(KERN_CRIT "EXT4-fs error (device %s): %s: ",sb->s_id, function);
	vprintk(fmt, args);
	printk("\n");
	va_end(args);

	ext4_handle_error(sb);
}

static const char *ext4_decode_error(struct super_block * sb, int errno,
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
		if (!sb || EXT4_SB(sb)->s_journal->j_flags & JBD2_ABORT)
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

void __ext4_std_error (struct super_block * sb, const char * function,
		       int errno)
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
	printk (KERN_CRIT "EXT4-fs error (device %s) in %s: %s\n",
		sb->s_id, function, errstr);

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

void ext4_abort (struct super_block * sb, const char * function,
		 const char * fmt, ...)
{
	va_list args;

	printk (KERN_CRIT "ext4_abort called.\n");

	va_start(args, fmt);
	printk(KERN_CRIT "EXT4-fs error (device %s): %s: ",sb->s_id, function);
	vprintk(fmt, args);
	printk("\n");
	va_end(args);

	if (test_opt(sb, ERRORS_PANIC))
		panic("EXT4-fs panic from previous error\n");

	if (sb->s_flags & MS_RDONLY)
		return;

	printk(KERN_CRIT "Remounting filesystem read-only\n");
	EXT4_SB(sb)->s_mount_state |= EXT4_ERROR_FS;
	sb->s_flags |= MS_RDONLY;
	EXT4_SB(sb)->s_mount_opt |= EXT4_MOUNT_ABORT;
	jbd2_journal_abort(EXT4_SB(sb)->s_journal, -EIO);
}

void ext4_warning (struct super_block * sb, const char * function,
		   const char * fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	printk(KERN_WARNING "EXT4-fs warning (device %s): %s: ",
	       sb->s_id, function);
	vprintk(fmt, args);
	printk("\n");
	va_end(args);
}

void ext4_update_dynamic_rev(struct super_block *sb)
{
	struct ext4_super_block *es = EXT4_SB(sb)->s_es;

	if (le32_to_cpu(es->s_rev_level) > EXT4_GOOD_OLD_REV)
		return;

	ext4_warning(sb, __FUNCTION__,
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
static struct block_device *ext4_blkdev_get(dev_t dev)
{
	struct block_device *bdev;
	char b[BDEVNAME_SIZE];

	bdev = open_by_devnum(dev, FMODE_READ|FMODE_WRITE);
	if (IS_ERR(bdev))
		goto fail;
	return bdev;

fail:
	printk(KERN_ERR "EXT4: failed to open journal device %s: %ld\n",
			__bdevname(dev, b), PTR_ERR(bdev));
	return NULL;
}

/*
 * Release the journal device
 */
static int ext4_blkdev_put(struct block_device *bdev)
{
	bd_release(bdev);
	return blkdev_put(bdev);
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

	printk(KERN_ERR "sb orphan head is %d\n",
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

static void ext4_put_super (struct super_block * sb)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_super_block *es = sbi->s_es;
	int i;

	ext4_ext_release(sb);
	ext4_xattr_put_super(sb);
	jbd2_journal_destroy(sbi->s_journal);
	if (!(sb->s_flags & MS_RDONLY)) {
		EXT4_CLEAR_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER);
		es->s_state = cpu_to_le16(sbi->s_mount_state);
		BUFFER_TRACE(sbi->s_sbh, "marking dirty");
		mark_buffer_dirty(sbi->s_sbh);
		ext4_commit_super(sb, es, 1);
	}

	for (i = 0; i < sbi->s_gdb_count; i++)
		brelse(sbi->s_group_desc[i]);
	kfree(sbi->s_group_desc);
	percpu_counter_destroy(&sbi->s_freeblocks_counter);
	percpu_counter_destroy(&sbi->s_freeinodes_counter);
	percpu_counter_destroy(&sbi->s_dirs_counter);
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
	kfree(sbi);
	return;
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
#ifdef CONFIG_EXT4DEV_FS_POSIX_ACL
	ei->i_acl = EXT4_ACL_NOT_CACHED;
	ei->i_default_acl = EXT4_ACL_NOT_CACHED;
#endif
	ei->i_block_alloc_info = NULL;
	ei->vfs_inode.i_version = 1;
	memset(&ei->i_cached_extent, 0, sizeof(struct ext4_ext_cache));
	return &ei->vfs_inode;
}

static void ext4_destroy_inode(struct inode *inode)
{
	if (!list_empty(&(EXT4_I(inode)->i_orphan))) {
		printk("EXT4 Inode %p: orphan list check failed!\n",
			EXT4_I(inode));
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 16, 4,
				EXT4_I(inode), sizeof(struct ext4_inode_info),
				true);
		dump_stack();
	}
	kmem_cache_free(ext4_inode_cachep, EXT4_I(inode));
}

static void init_once(void * foo, struct kmem_cache * cachep, unsigned long flags)
{
	struct ext4_inode_info *ei = (struct ext4_inode_info *) foo;

	INIT_LIST_HEAD(&ei->i_orphan);
#ifdef CONFIG_EXT4DEV_FS_XATTR
	init_rwsem(&ei->xattr_sem);
#endif
	mutex_init(&ei->truncate_mutex);
	inode_init_once(&ei->vfs_inode);
}

static int init_inodecache(void)
{
	ext4_inode_cachep = kmem_cache_create("ext4_inode_cache",
					     sizeof(struct ext4_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once, NULL);
	if (ext4_inode_cachep == NULL)
		return -ENOMEM;
	return 0;
}

static void destroy_inodecache(void)
{
	kmem_cache_destroy(ext4_inode_cachep);
}

static void ext4_clear_inode(struct inode *inode)
{
	struct ext4_block_alloc_info *rsv = EXT4_I(inode)->i_block_alloc_info;
#ifdef CONFIG_EXT4DEV_FS_POSIX_ACL
	if (EXT4_I(inode)->i_acl &&
			EXT4_I(inode)->i_acl != EXT4_ACL_NOT_CACHED) {
		posix_acl_release(EXT4_I(inode)->i_acl);
		EXT4_I(inode)->i_acl = EXT4_ACL_NOT_CACHED;
	}
	if (EXT4_I(inode)->i_default_acl &&
			EXT4_I(inode)->i_default_acl != EXT4_ACL_NOT_CACHED) {
		posix_acl_release(EXT4_I(inode)->i_default_acl);
		EXT4_I(inode)->i_default_acl = EXT4_ACL_NOT_CACHED;
	}
#endif
	ext4_discard_reservation(inode);
	EXT4_I(inode)->i_block_alloc_info = NULL;
	if (unlikely(rsv))
		kfree(rsv);
}

static inline void ext4_show_quota_options(struct seq_file *seq, struct super_block *sb)
{
#if defined(CONFIG_QUOTA)
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	if (sbi->s_jquota_fmt)
		seq_printf(seq, ",jqfmt=%s",
		(sbi->s_jquota_fmt == QFMT_VFS_OLD) ? "vfsold": "vfsv0");

	if (sbi->s_qf_names[USRQUOTA])
		seq_printf(seq, ",usrjquota=%s", sbi->s_qf_names[USRQUOTA]);

	if (sbi->s_qf_names[GRPQUOTA])
		seq_printf(seq, ",grpjquota=%s", sbi->s_qf_names[GRPQUOTA]);

	if (sbi->s_mount_opt & EXT4_MOUNT_USRQUOTA)
		seq_puts(seq, ",usrquota");

	if (sbi->s_mount_opt & EXT4_MOUNT_GRPQUOTA)
		seq_puts(seq, ",grpquota");
#endif
}

static int ext4_show_options(struct seq_file *seq, struct vfsmount *vfs)
{
	struct super_block *sb = vfs->mnt_sb;

	if (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_JOURNAL_DATA)
		seq_puts(seq, ",data=journal");
	else if (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_ORDERED_DATA)
		seq_puts(seq, ",data=ordered");
	else if (test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_WRITEBACK_DATA)
		seq_puts(seq, ",data=writeback");

	ext4_show_quota_options(seq, sb);

	return 0;
}


static struct dentry *ext4_get_dentry(struct super_block *sb, void *vobjp)
{
	__u32 *objp = vobjp;
	unsigned long ino = objp[0];
	__u32 generation = objp[1];
	struct inode *inode;
	struct dentry *result;

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
	inode = iget(sb, ino);
	if (inode == NULL)
		return ERR_PTR(-ENOMEM);
	if (is_bad_inode(inode) ||
	    (generation && inode->i_generation != generation)) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	/* now to find a dentry.
	 * If possible, get a well-connected one
	 */
	result = d_alloc_anon(inode);
	if (!result) {
		iput(inode);
		return ERR_PTR(-ENOMEM);
	}
	return result;
}

#ifdef CONFIG_QUOTA
#define QTYPE2NAME(t) ((t)==USRQUOTA?"user":"group")
#define QTYPE2MOPT(on, t) ((t)==USRQUOTA?((on)##USRJQUOTA):((on)##GRPJQUOTA))

static int ext4_dquot_initialize(struct inode *inode, int type);
static int ext4_dquot_drop(struct inode *inode);
static int ext4_write_dquot(struct dquot *dquot);
static int ext4_acquire_dquot(struct dquot *dquot);
static int ext4_release_dquot(struct dquot *dquot);
static int ext4_mark_dquot_dirty(struct dquot *dquot);
static int ext4_write_info(struct super_block *sb, int type);
static int ext4_quota_on(struct super_block *sb, int type, int format_id, char *path);
static int ext4_quota_on_mount(struct super_block *sb, int type);
static ssize_t ext4_quota_read(struct super_block *sb, int type, char *data,
			       size_t len, loff_t off);
static ssize_t ext4_quota_write(struct super_block *sb, int type,
				const char *data, size_t len, loff_t off);

static struct dquot_operations ext4_quota_operations = {
	.initialize	= ext4_dquot_initialize,
	.drop		= ext4_dquot_drop,
	.alloc_space	= dquot_alloc_space,
	.alloc_inode	= dquot_alloc_inode,
	.free_space	= dquot_free_space,
	.free_inode	= dquot_free_inode,
	.transfer	= dquot_transfer,
	.write_dquot	= ext4_write_dquot,
	.acquire_dquot	= ext4_acquire_dquot,
	.release_dquot	= ext4_release_dquot,
	.mark_dirty	= ext4_mark_dquot_dirty,
	.write_info	= ext4_write_info
};

static struct quotactl_ops ext4_qctl_operations = {
	.quota_on	= ext4_quota_on,
	.quota_off	= vfs_quota_off,
	.quota_sync	= vfs_quota_sync,
	.get_info	= vfs_get_dqinfo,
	.set_info	= vfs_set_dqinfo,
	.get_dqblk	= vfs_get_dqblk,
	.set_dqblk	= vfs_set_dqblk
};
#endif

static const struct super_operations ext4_sops = {
	.alloc_inode	= ext4_alloc_inode,
	.destroy_inode	= ext4_destroy_inode,
	.read_inode	= ext4_read_inode,
	.write_inode	= ext4_write_inode,
	.dirty_inode	= ext4_dirty_inode,
	.delete_inode	= ext4_delete_inode,
	.put_super	= ext4_put_super,
	.write_super	= ext4_write_super,
	.sync_fs	= ext4_sync_fs,
	.write_super_lockfs = ext4_write_super_lockfs,
	.unlockfs	= ext4_unlockfs,
	.statfs		= ext4_statfs,
	.remount_fs	= ext4_remount,
	.clear_inode	= ext4_clear_inode,
	.show_options	= ext4_show_options,
#ifdef CONFIG_QUOTA
	.quota_read	= ext4_quota_read,
	.quota_write	= ext4_quota_write,
#endif
};

static struct export_operations ext4_export_ops = {
	.get_parent = ext4_get_parent,
	.get_dentry = ext4_get_dentry,
};

enum {
	Opt_bsd_df, Opt_minix_df, Opt_grpid, Opt_nogrpid,
	Opt_resgid, Opt_resuid, Opt_sb, Opt_err_cont, Opt_err_panic, Opt_err_ro,
	Opt_nouid32, Opt_nocheck, Opt_debug, Opt_oldalloc, Opt_orlov,
	Opt_user_xattr, Opt_nouser_xattr, Opt_acl, Opt_noacl,
	Opt_reservation, Opt_noreservation, Opt_noload, Opt_nobh, Opt_bh,
	Opt_commit, Opt_journal_update, Opt_journal_inum, Opt_journal_dev,
	Opt_abort, Opt_data_journal, Opt_data_ordered, Opt_data_writeback,
	Opt_usrjquota, Opt_grpjquota, Opt_offusrjquota, Opt_offgrpjquota,
	Opt_jqfmt_vfsold, Opt_jqfmt_vfsv0, Opt_quota, Opt_noquota,
	Opt_ignore, Opt_barrier, Opt_err, Opt_resize, Opt_usrquota,
	Opt_grpquota, Opt_extents, Opt_noextents,
};

static match_table_t tokens = {
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
	{Opt_nocheck, "nocheck"},
	{Opt_nocheck, "check=none"},
	{Opt_debug, "debug"},
	{Opt_oldalloc, "oldalloc"},
	{Opt_orlov, "orlov"},
	{Opt_user_xattr, "user_xattr"},
	{Opt_nouser_xattr, "nouser_xattr"},
	{Opt_acl, "acl"},
	{Opt_noacl, "noacl"},
	{Opt_reservation, "reservation"},
	{Opt_noreservation, "noreservation"},
	{Opt_noload, "noload"},
	{Opt_nobh, "nobh"},
	{Opt_bh, "bh"},
	{Opt_commit, "commit=%u"},
	{Opt_journal_update, "journal=update"},
	{Opt_journal_inum, "journal=%u"},
	{Opt_journal_dev, "journal_dev=%u"},
	{Opt_abort, "abort"},
	{Opt_data_journal, "data=journal"},
	{Opt_data_ordered, "data=ordered"},
	{Opt_data_writeback, "data=writeback"},
	{Opt_offusrjquota, "usrjquota="},
	{Opt_usrjquota, "usrjquota=%s"},
	{Opt_offgrpjquota, "grpjquota="},
	{Opt_grpjquota, "grpjquota=%s"},
	{Opt_jqfmt_vfsold, "jqfmt=vfsold"},
	{Opt_jqfmt_vfsv0, "jqfmt=vfsv0"},
	{Opt_grpquota, "grpquota"},
	{Opt_noquota, "noquota"},
	{Opt_quota, "quota"},
	{Opt_usrquota, "usrquota"},
	{Opt_barrier, "barrier=%u"},
	{Opt_extents, "extents"},
	{Opt_noextents, "noextents"},
	{Opt_err, NULL},
	{Opt_resize, "resize"},
};

static ext4_fsblk_t get_sb_block(void **data)
{
	ext4_fsblk_t	sb_block;
	char		*options = (char *) *data;

	if (!options || strncmp(options, "sb=", 3) != 0)
		return 1;	/* Default location */
	options += 3;
	/*todo: use simple_strtoll with >32bit ext4 */
	sb_block = simple_strtoul(options, &options, 0);
	if (*options && *options != ',') {
		printk("EXT4-fs: Invalid sb specification: %s\n",
		       (char *) *data);
		return 1;
	}
	if (*options == ',')
		options++;
	*data = (void *) options;
	return sb_block;
}

static int parse_options (char *options, struct super_block *sb,
			  unsigned int *inum, unsigned long *journal_devnum,
			  ext4_fsblk_t *n_blocks_count, int is_remount)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	char * p;
	substring_t args[MAX_OPT_ARGS];
	int data_opt = 0;
	int option;
#ifdef CONFIG_QUOTA
	int qtype;
	char *qname;
#endif

	if (!options)
		return 1;

	while ((p = strsep (&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_bsd_df:
			clear_opt (sbi->s_mount_opt, MINIX_DF);
			break;
		case Opt_minix_df:
			set_opt (sbi->s_mount_opt, MINIX_DF);
			break;
		case Opt_grpid:
			set_opt (sbi->s_mount_opt, GRPID);
			break;
		case Opt_nogrpid:
			clear_opt (sbi->s_mount_opt, GRPID);
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
			clear_opt (sbi->s_mount_opt, ERRORS_CONT);
			clear_opt (sbi->s_mount_opt, ERRORS_RO);
			set_opt (sbi->s_mount_opt, ERRORS_PANIC);
			break;
		case Opt_err_ro:
			clear_opt (sbi->s_mount_opt, ERRORS_CONT);
			clear_opt (sbi->s_mount_opt, ERRORS_PANIC);
			set_opt (sbi->s_mount_opt, ERRORS_RO);
			break;
		case Opt_err_cont:
			clear_opt (sbi->s_mount_opt, ERRORS_RO);
			clear_opt (sbi->s_mount_opt, ERRORS_PANIC);
			set_opt (sbi->s_mount_opt, ERRORS_CONT);
			break;
		case Opt_nouid32:
			set_opt (sbi->s_mount_opt, NO_UID32);
			break;
		case Opt_nocheck:
			clear_opt (sbi->s_mount_opt, CHECK);
			break;
		case Opt_debug:
			set_opt (sbi->s_mount_opt, DEBUG);
			break;
		case Opt_oldalloc:
			set_opt (sbi->s_mount_opt, OLDALLOC);
			break;
		case Opt_orlov:
			clear_opt (sbi->s_mount_opt, OLDALLOC);
			break;
#ifdef CONFIG_EXT4DEV_FS_XATTR
		case Opt_user_xattr:
			set_opt (sbi->s_mount_opt, XATTR_USER);
			break;
		case Opt_nouser_xattr:
			clear_opt (sbi->s_mount_opt, XATTR_USER);
			break;
#else
		case Opt_user_xattr:
		case Opt_nouser_xattr:
			printk("EXT4 (no)user_xattr options not supported\n");
			break;
#endif
#ifdef CONFIG_EXT4DEV_FS_POSIX_ACL
		case Opt_acl:
			set_opt(sbi->s_mount_opt, POSIX_ACL);
			break;
		case Opt_noacl:
			clear_opt(sbi->s_mount_opt, POSIX_ACL);
			break;
#else
		case Opt_acl:
		case Opt_noacl:
			printk("EXT4 (no)acl options not supported\n");
			break;
#endif
		case Opt_reservation:
			set_opt(sbi->s_mount_opt, RESERVATION);
			break;
		case Opt_noreservation:
			clear_opt(sbi->s_mount_opt, RESERVATION);
			break;
		case Opt_journal_update:
			/* @@@ FIXME */
			/* Eventually we will want to be able to create
			   a journal file here.  For now, only allow the
			   user to specify an existing inode to be the
			   journal file. */
			if (is_remount) {
				printk(KERN_ERR "EXT4-fs: cannot specify "
				       "journal on remount\n");
				return 0;
			}
			set_opt (sbi->s_mount_opt, UPDATE_JOURNAL);
			break;
		case Opt_journal_inum:
			if (is_remount) {
				printk(KERN_ERR "EXT4-fs: cannot specify "
				       "journal on remount\n");
				return 0;
			}
			if (match_int(&args[0], &option))
				return 0;
			*inum = option;
			break;
		case Opt_journal_dev:
			if (is_remount) {
				printk(KERN_ERR "EXT4-fs: cannot specify "
				       "journal on remount\n");
				return 0;
			}
			if (match_int(&args[0], &option))
				return 0;
			*journal_devnum = option;
			break;
		case Opt_noload:
			set_opt (sbi->s_mount_opt, NOLOAD);
			break;
		case Opt_commit:
			if (match_int(&args[0], &option))
				return 0;
			if (option < 0)
				return 0;
			if (option == 0)
				option = JBD_DEFAULT_MAX_COMMIT_AGE;
			sbi->s_commit_interval = HZ * option;
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
				if ((sbi->s_mount_opt & EXT4_MOUNT_DATA_FLAGS)
						!= data_opt) {
					printk(KERN_ERR
						"EXT4-fs: cannot change data "
						"mode on remount\n");
					return 0;
				}
			} else {
				sbi->s_mount_opt &= ~EXT4_MOUNT_DATA_FLAGS;
				sbi->s_mount_opt |= data_opt;
			}
			break;
#ifdef CONFIG_QUOTA
		case Opt_usrjquota:
			qtype = USRQUOTA;
			goto set_qf_name;
		case Opt_grpjquota:
			qtype = GRPQUOTA;
set_qf_name:
			if (sb_any_quota_enabled(sb)) {
				printk(KERN_ERR
					"EXT4-fs: Cannot change journalled "
					"quota options when quota turned on.\n");
				return 0;
			}
			qname = match_strdup(&args[0]);
			if (!qname) {
				printk(KERN_ERR
					"EXT4-fs: not enough memory for "
					"storing quotafile name.\n");
				return 0;
			}
			if (sbi->s_qf_names[qtype] &&
			    strcmp(sbi->s_qf_names[qtype], qname)) {
				printk(KERN_ERR
					"EXT4-fs: %s quota file already "
					"specified.\n", QTYPE2NAME(qtype));
				kfree(qname);
				return 0;
			}
			sbi->s_qf_names[qtype] = qname;
			if (strchr(sbi->s_qf_names[qtype], '/')) {
				printk(KERN_ERR
					"EXT4-fs: quotafile must be on "
					"filesystem root.\n");
				kfree(sbi->s_qf_names[qtype]);
				sbi->s_qf_names[qtype] = NULL;
				return 0;
			}
			set_opt(sbi->s_mount_opt, QUOTA);
			break;
		case Opt_offusrjquota:
			qtype = USRQUOTA;
			goto clear_qf_name;
		case Opt_offgrpjquota:
			qtype = GRPQUOTA;
clear_qf_name:
			if (sb_any_quota_enabled(sb)) {
				printk(KERN_ERR "EXT4-fs: Cannot change "
					"journalled quota options when "
					"quota turned on.\n");
				return 0;
			}
			/*
			 * The space will be released later when all options
			 * are confirmed to be correct
			 */
			sbi->s_qf_names[qtype] = NULL;
			break;
		case Opt_jqfmt_vfsold:
			sbi->s_jquota_fmt = QFMT_VFS_OLD;
			break;
		case Opt_jqfmt_vfsv0:
			sbi->s_jquota_fmt = QFMT_VFS_V0;
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
			if (sb_any_quota_enabled(sb)) {
				printk(KERN_ERR "EXT4-fs: Cannot change quota "
					"options when quota turned on.\n");
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
		case Opt_usrjquota:
		case Opt_grpjquota:
		case Opt_offusrjquota:
		case Opt_offgrpjquota:
		case Opt_jqfmt_vfsold:
		case Opt_jqfmt_vfsv0:
			printk(KERN_ERR
				"EXT4-fs: journalled quota options not "
				"supported.\n");
			break;
		case Opt_noquota:
			break;
#endif
		case Opt_abort:
			set_opt(sbi->s_mount_opt, ABORT);
			break;
		case Opt_barrier:
			if (match_int(&args[0], &option))
				return 0;
			if (option)
				set_opt(sbi->s_mount_opt, BARRIER);
			else
				clear_opt(sbi->s_mount_opt, BARRIER);
			break;
		case Opt_ignore:
			break;
		case Opt_resize:
			if (!is_remount) {
				printk("EXT4-fs: resize option only available "
					"for remount\n");
				return 0;
			}
			if (match_int(&args[0], &option) != 0)
				return 0;
			*n_blocks_count = option;
			break;
		case Opt_nobh:
			set_opt(sbi->s_mount_opt, NOBH);
			break;
		case Opt_bh:
			clear_opt(sbi->s_mount_opt, NOBH);
			break;
		case Opt_extents:
			set_opt (sbi->s_mount_opt, EXTENTS);
			break;
		case Opt_noextents:
			clear_opt (sbi->s_mount_opt, EXTENTS);
			break;
		default:
			printk (KERN_ERR
				"EXT4-fs: Unrecognized mount option \"%s\" "
				"or missing value\n", p);
			return 0;
		}
	}
#ifdef CONFIG_QUOTA
	if (sbi->s_qf_names[USRQUOTA] || sbi->s_qf_names[GRPQUOTA]) {
		if ((sbi->s_mount_opt & EXT4_MOUNT_USRQUOTA) &&
		     sbi->s_qf_names[USRQUOTA])
			clear_opt(sbi->s_mount_opt, USRQUOTA);

		if ((sbi->s_mount_opt & EXT4_MOUNT_GRPQUOTA) &&
		     sbi->s_qf_names[GRPQUOTA])
			clear_opt(sbi->s_mount_opt, GRPQUOTA);

		if ((sbi->s_qf_names[USRQUOTA] &&
				(sbi->s_mount_opt & EXT4_MOUNT_GRPQUOTA)) ||
		    (sbi->s_qf_names[GRPQUOTA] &&
				(sbi->s_mount_opt & EXT4_MOUNT_USRQUOTA))) {
			printk(KERN_ERR "EXT4-fs: old and new quota "
					"format mixing.\n");
			return 0;
		}

		if (!sbi->s_jquota_fmt) {
			printk(KERN_ERR "EXT4-fs: journalled quota format "
					"not specified.\n");
			return 0;
		}
	} else {
		if (sbi->s_jquota_fmt) {
			printk(KERN_ERR "EXT4-fs: journalled quota format "
					"specified with no journalling "
					"enabled.\n");
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
		printk (KERN_ERR "EXT4-fs warning: revision level too high, "
			"forcing read-only mode\n");
		res = MS_RDONLY;
	}
	if (read_only)
		return res;
	if (!(sbi->s_mount_state & EXT4_VALID_FS))
		printk (KERN_WARNING "EXT4-fs warning: mounting unchecked fs, "
			"running e2fsck is recommended\n");
	else if ((sbi->s_mount_state & EXT4_ERROR_FS))
		printk (KERN_WARNING
			"EXT4-fs warning: mounting fs with errors, "
			"running e2fsck is recommended\n");
	else if ((__s16) le16_to_cpu(es->s_max_mnt_count) >= 0 &&
		 le16_to_cpu(es->s_mnt_count) >=
		 (unsigned short) (__s16) le16_to_cpu(es->s_max_mnt_count))
		printk (KERN_WARNING
			"EXT4-fs warning: maximal mount count reached, "
			"running e2fsck is recommended\n");
	else if (le32_to_cpu(es->s_checkinterval) &&
		(le32_to_cpu(es->s_lastcheck) +
			le32_to_cpu(es->s_checkinterval) <= get_seconds()))
		printk (KERN_WARNING
			"EXT4-fs warning: checktime reached, "
			"running e2fsck is recommended\n");
#if 0
		/* @@@ We _will_ want to clear the valid bit if we find
		 * inconsistencies, to force a fsck at reboot.  But for
		 * a plain journaled filesystem we can keep it set as
		 * valid forever! :)
		 */
	es->s_state = cpu_to_le16(le16_to_cpu(es->s_state) & ~EXT4_VALID_FS);
#endif
	if (!(__s16) le16_to_cpu(es->s_max_mnt_count))
		es->s_max_mnt_count = cpu_to_le16(EXT4_DFL_MAX_MNT_COUNT);
	es->s_mnt_count=cpu_to_le16(le16_to_cpu(es->s_mnt_count) + 1);
	es->s_mtime = cpu_to_le32(get_seconds());
	ext4_update_dynamic_rev(sb);
	EXT4_SET_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER);

	ext4_commit_super(sb, es, 1);
	if (test_opt(sb, DEBUG))
		printk(KERN_INFO "[EXT4 FS bs=%lu, gc=%lu, "
				"bpg=%lu, ipg=%lu, mo=%04lx]\n",
			sb->s_blocksize,
			sbi->s_groups_count,
			EXT4_BLOCKS_PER_GROUP(sb),
			EXT4_INODES_PER_GROUP(sb),
			sbi->s_mount_opt);

	printk(KERN_INFO "EXT4 FS on %s, ", sb->s_id);
	if (EXT4_SB(sb)->s_journal->j_inode == NULL) {
		char b[BDEVNAME_SIZE];

		printk("external journal on %s\n",
			bdevname(EXT4_SB(sb)->s_journal->j_dev, b));
	} else {
		printk("internal journal\n");
	}
	return res;
}

/* Called at mount-time, super-block is locked */
static int ext4_check_descriptors (struct super_block * sb)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	ext4_fsblk_t first_block = le32_to_cpu(sbi->s_es->s_first_data_block);
	ext4_fsblk_t last_block;
	ext4_fsblk_t block_bitmap;
	ext4_fsblk_t inode_bitmap;
	ext4_fsblk_t inode_table;
	struct ext4_group_desc * gdp = NULL;
	int desc_block = 0;
	int i;

	ext4_debug ("Checking group descriptors");

	for (i = 0; i < sbi->s_groups_count; i++)
	{
		if (i == sbi->s_groups_count - 1)
			last_block = ext4_blocks_count(sbi->s_es) - 1;
		else
			last_block = first_block +
				(EXT4_BLOCKS_PER_GROUP(sb) - 1);

		if ((i % EXT4_DESC_PER_BLOCK(sb)) == 0)
			gdp = (struct ext4_group_desc *)
					sbi->s_group_desc[desc_block++]->b_data;
		block_bitmap = ext4_block_bitmap(sb, gdp);
		if (block_bitmap < first_block || block_bitmap > last_block)
		{
			ext4_error (sb, "ext4_check_descriptors",
				    "Block bitmap for group %d"
				    " not in group (block %llu)!",
				    i, block_bitmap);
			return 0;
		}
		inode_bitmap = ext4_inode_bitmap(sb, gdp);
		if (inode_bitmap < first_block || inode_bitmap > last_block)
		{
			ext4_error (sb, "ext4_check_descriptors",
				    "Inode bitmap for group %d"
				    " not in group (block %llu)!",
				    i, inode_bitmap);
			return 0;
		}
		inode_table = ext4_inode_table(sb, gdp);
		if (inode_table < first_block ||
		    inode_table + sbi->s_itb_per_group > last_block)
		{
			ext4_error (sb, "ext4_check_descriptors",
				    "Inode table for group %d"
				    " not in group (block %llu)!",
				    i, inode_table);
			return 0;
		}
		first_block += EXT4_BLOCKS_PER_GROUP(sb);
		gdp = (struct ext4_group_desc *)
			((__u8 *)gdp + EXT4_DESC_SIZE(sb));
	}

	ext4_free_blocks_count_set(sbi->s_es, ext4_count_free_blocks(sb));
	sbi->s_es->s_free_inodes_count=cpu_to_le32(ext4_count_free_inodes(sb));
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
static void ext4_orphan_cleanup (struct super_block * sb,
				 struct ext4_super_block * es)
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
		printk(KERN_ERR "EXT4-fs: write access "
			"unavailable, skipping orphan cleanup.\n");
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
		printk(KERN_INFO "EXT4-fs: %s: orphan cleanup on readonly fs\n",
		       sb->s_id);
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
				printk(KERN_ERR
					"EXT4-fs: Cannot turn on journalled "
					"quota: error %d\n", ret);
		}
	}
#endif

	while (es->s_last_orphan) {
		struct inode *inode;

		if (!(inode =
		      ext4_orphan_get(sb, le32_to_cpu(es->s_last_orphan)))) {
			es->s_last_orphan = 0;
			break;
		}

		list_add(&EXT4_I(inode)->i_orphan, &EXT4_SB(sb)->s_orphan);
		DQUOT_INIT(inode);
		if (inode->i_nlink) {
			printk(KERN_DEBUG
				"%s: truncating inode %lu to %Ld bytes\n",
				__FUNCTION__, inode->i_ino, inode->i_size);
			jbd_debug(2, "truncating inode %lu to %Ld bytes\n",
				  inode->i_ino, inode->i_size);
			ext4_truncate(inode);
			nr_truncates++;
		} else {
			printk(KERN_DEBUG
				"%s: deleting unreferenced inode %lu\n",
				__FUNCTION__, inode->i_ino);
			jbd_debug(2, "deleting unreferenced inode %lu\n",
				  inode->i_ino);
			nr_orphans++;
		}
		iput(inode);  /* The delete magic happens here! */
	}

#define PLURAL(x) (x), ((x)==1) ? "" : "s"

	if (nr_orphans)
		printk(KERN_INFO "EXT4-fs: %s: %d orphan inode%s deleted\n",
		       sb->s_id, PLURAL(nr_orphans));
	if (nr_truncates)
		printk(KERN_INFO "EXT4-fs: %s: %d truncate%s cleaned up\n",
		       sb->s_id, PLURAL(nr_truncates));
#ifdef CONFIG_QUOTA
	/* Turn quotas off */
	for (i = 0; i < MAXQUOTAS; i++) {
		if (sb_dqopt(sb)->files[i])
			vfs_quota_off(sb, i);
	}
#endif
	sb->s_flags = s_flags; /* Restore MS_RDONLY status */
}

#define log2(n) ffz(~(n))

/*
 * Maximal file size.  There is a direct, and {,double-,triple-}indirect
 * block limit, and also a limit of (2^32 - 1) 512-byte sectors in i_blocks.
 * We need to be 1 filesystem block less than the 2^32 sector limit.
 */
static loff_t ext4_max_size(int bits)
{
	loff_t res = EXT4_NDIR_BLOCKS;
	/* This constant is calculated to be the largest file size for a
	 * dense, 4k-blocksize file such that the total number of
	 * sectors in the file, including data and all indirect blocks,
	 * does not exceed 2^32. */
	const loff_t upper_limit = 0x1ff7fffd000LL;

	res += 1LL << (bits-2);
	res += 1LL << (2*(bits-2));
	res += 1LL << (3*(bits-2));
	res <<= bits;
	if (res > upper_limit)
		res = upper_limit;
	return res;
}

static ext4_fsblk_t descriptor_loc(struct super_block *sb,
				ext4_fsblk_t logical_sb_block, int nr)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	unsigned long bg, first_meta_bg;
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


static int ext4_fill_super (struct super_block *sb, void *data, int silent)
{
	struct buffer_head * bh;
	struct ext4_super_block *es = NULL;
	struct ext4_sb_info *sbi;
	ext4_fsblk_t block;
	ext4_fsblk_t sb_block = get_sb_block(&data);
	ext4_fsblk_t logical_sb_block;
	unsigned long offset = 0;
	unsigned int journal_inum = 0;
	unsigned long journal_devnum = 0;
	unsigned long def_mount_opts;
	struct inode *root;
	int blocksize;
	int hblock;
	int db_count;
	int i;
	int needs_recovery;
	__le32 features;
	__u64 blocks_count;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;
	sb->s_fs_info = sbi;
	sbi->s_mount_opt = 0;
	sbi->s_resuid = EXT4_DEF_RESUID;
	sbi->s_resgid = EXT4_DEF_RESGID;

	unlock_kernel();

	blocksize = sb_min_blocksize(sb, EXT4_MIN_BLOCK_SIZE);
	if (!blocksize) {
		printk(KERN_ERR "EXT4-fs: unable to set blocksize\n");
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
		printk (KERN_ERR "EXT4-fs: unable to read superblock\n");
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

	/* Set defaults before we parse the mount options */
	def_mount_opts = le32_to_cpu(es->s_default_mount_opts);
	if (def_mount_opts & EXT4_DEFM_DEBUG)
		set_opt(sbi->s_mount_opt, DEBUG);
	if (def_mount_opts & EXT4_DEFM_BSDGROUPS)
		set_opt(sbi->s_mount_opt, GRPID);
	if (def_mount_opts & EXT4_DEFM_UID16)
		set_opt(sbi->s_mount_opt, NO_UID32);
#ifdef CONFIG_EXT4DEV_FS_XATTR
	if (def_mount_opts & EXT4_DEFM_XATTR_USER)
		set_opt(sbi->s_mount_opt, XATTR_USER);
#endif
#ifdef CONFIG_EXT4DEV_FS_POSIX_ACL
	if (def_mount_opts & EXT4_DEFM_ACL)
		set_opt(sbi->s_mount_opt, POSIX_ACL);
#endif
	if ((def_mount_opts & EXT4_DEFM_JMODE) == EXT4_DEFM_JMODE_DATA)
		sbi->s_mount_opt |= EXT4_MOUNT_JOURNAL_DATA;
	else if ((def_mount_opts & EXT4_DEFM_JMODE) == EXT4_DEFM_JMODE_ORDERED)
		sbi->s_mount_opt |= EXT4_MOUNT_ORDERED_DATA;
	else if ((def_mount_opts & EXT4_DEFM_JMODE) == EXT4_DEFM_JMODE_WBACK)
		sbi->s_mount_opt |= EXT4_MOUNT_WRITEBACK_DATA;

	if (le16_to_cpu(sbi->s_es->s_errors) == EXT4_ERRORS_PANIC)
		set_opt(sbi->s_mount_opt, ERRORS_PANIC);
	else if (le16_to_cpu(sbi->s_es->s_errors) == EXT4_ERRORS_RO)
		set_opt(sbi->s_mount_opt, ERRORS_RO);
	else
		set_opt(sbi->s_mount_opt, ERRORS_CONT);

	sbi->s_resuid = le16_to_cpu(es->s_def_resuid);
	sbi->s_resgid = le16_to_cpu(es->s_def_resgid);

	set_opt(sbi->s_mount_opt, RESERVATION);

	/*
	 * turn on extents feature by default in ext4 filesystem
	 * User -o noextents to turn it off
	 */
	set_opt(sbi->s_mount_opt, EXTENTS);

	if (!parse_options ((char *) data, sb, &journal_inum, &journal_devnum,
			    NULL, 0))
		goto failed_mount;

	sb->s_flags = (sb->s_flags & ~MS_POSIXACL) |
		((sbi->s_mount_opt & EXT4_MOUNT_POSIX_ACL) ? MS_POSIXACL : 0);

	if (le32_to_cpu(es->s_rev_level) == EXT4_GOOD_OLD_REV &&
	    (EXT4_HAS_COMPAT_FEATURE(sb, ~0U) ||
	     EXT4_HAS_RO_COMPAT_FEATURE(sb, ~0U) ||
	     EXT4_HAS_INCOMPAT_FEATURE(sb, ~0U)))
		printk(KERN_WARNING
		       "EXT4-fs warning: feature flags set on rev 0 fs, "
		       "running e2fsck is recommended\n");
	/*
	 * Check feature flags regardless of the revision level, since we
	 * previously didn't change the revision level when setting the flags,
	 * so there is a chance incompat flags are set on a rev 0 filesystem.
	 */
	features = EXT4_HAS_INCOMPAT_FEATURE(sb, ~EXT4_FEATURE_INCOMPAT_SUPP);
	if (features) {
		printk(KERN_ERR "EXT4-fs: %s: couldn't mount because of "
		       "unsupported optional features (%x).\n",
		       sb->s_id, le32_to_cpu(features));
		goto failed_mount;
	}
	features = EXT4_HAS_RO_COMPAT_FEATURE(sb, ~EXT4_FEATURE_RO_COMPAT_SUPP);
	if (!(sb->s_flags & MS_RDONLY) && features) {
		printk(KERN_ERR "EXT4-fs: %s: couldn't mount RDWR because of "
		       "unsupported optional features (%x).\n",
		       sb->s_id, le32_to_cpu(features));
		goto failed_mount;
	}
	blocksize = BLOCK_SIZE << le32_to_cpu(es->s_log_block_size);

	if (blocksize < EXT4_MIN_BLOCK_SIZE ||
	    blocksize > EXT4_MAX_BLOCK_SIZE) {
		printk(KERN_ERR
		       "EXT4-fs: Unsupported filesystem blocksize %d on %s.\n",
		       blocksize, sb->s_id);
		goto failed_mount;
	}

	hblock = bdev_hardsect_size(sb->s_bdev);
	if (sb->s_blocksize != blocksize) {
		/*
		 * Make sure the blocksize for the filesystem is larger
		 * than the hardware sectorsize for the machine.
		 */
		if (blocksize < hblock) {
			printk(KERN_ERR "EXT4-fs: blocksize %d too small for "
			       "device blocksize %d.\n", blocksize, hblock);
			goto failed_mount;
		}

		brelse (bh);
		sb_set_blocksize(sb, blocksize);
		logical_sb_block = sb_block * EXT4_MIN_BLOCK_SIZE;
		offset = do_div(logical_sb_block, blocksize);
		bh = sb_bread(sb, logical_sb_block);
		if (!bh) {
			printk(KERN_ERR
			       "EXT4-fs: Can't read superblock on 2nd try.\n");
			goto failed_mount;
		}
		es = (struct ext4_super_block *)(((char *)bh->b_data) + offset);
		sbi->s_es = es;
		if (es->s_magic != cpu_to_le16(EXT4_SUPER_MAGIC)) {
			printk (KERN_ERR
				"EXT4-fs: Magic mismatch, very weird !\n");
			goto failed_mount;
		}
	}

	sb->s_maxbytes = ext4_max_size(sb->s_blocksize_bits);

	if (le32_to_cpu(es->s_rev_level) == EXT4_GOOD_OLD_REV) {
		sbi->s_inode_size = EXT4_GOOD_OLD_INODE_SIZE;
		sbi->s_first_ino = EXT4_GOOD_OLD_FIRST_INO;
	} else {
		sbi->s_inode_size = le16_to_cpu(es->s_inode_size);
		sbi->s_first_ino = le32_to_cpu(es->s_first_ino);
		if ((sbi->s_inode_size < EXT4_GOOD_OLD_INODE_SIZE) ||
		    (!is_power_of_2(sbi->s_inode_size)) ||
		    (sbi->s_inode_size > blocksize)) {
			printk (KERN_ERR
				"EXT4-fs: unsupported inode size: %d\n",
				sbi->s_inode_size);
			goto failed_mount;
		}
		if (sbi->s_inode_size > EXT4_GOOD_OLD_INODE_SIZE)
			sb->s_time_gran = 1 << (EXT4_EPOCH_BITS - 2);
	}
	sbi->s_frag_size = EXT4_MIN_FRAG_SIZE <<
				   le32_to_cpu(es->s_log_frag_size);
	if (blocksize != sbi->s_frag_size) {
		printk(KERN_ERR
		       "EXT4-fs: fragsize %lu != blocksize %u (unsupported)\n",
		       sbi->s_frag_size, blocksize);
		goto failed_mount;
	}
	sbi->s_desc_size = le16_to_cpu(es->s_desc_size);
	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_64BIT)) {
		if (sbi->s_desc_size < EXT4_MIN_DESC_SIZE_64BIT ||
		    sbi->s_desc_size > EXT4_MAX_DESC_SIZE ||
		    sbi->s_desc_size & (sbi->s_desc_size - 1)) {
			printk(KERN_ERR
			       "EXT4-fs: unsupported descriptor size %lu\n",
			       sbi->s_desc_size);
			goto failed_mount;
		}
	} else
		sbi->s_desc_size = EXT4_MIN_DESC_SIZE;
	sbi->s_blocks_per_group = le32_to_cpu(es->s_blocks_per_group);
	sbi->s_frags_per_group = le32_to_cpu(es->s_frags_per_group);
	sbi->s_inodes_per_group = le32_to_cpu(es->s_inodes_per_group);
	if (EXT4_INODE_SIZE(sb) == 0)
		goto cantfind_ext4;
	sbi->s_inodes_per_block = blocksize / EXT4_INODE_SIZE(sb);
	if (sbi->s_inodes_per_block == 0)
		goto cantfind_ext4;
	sbi->s_itb_per_group = sbi->s_inodes_per_group /
					sbi->s_inodes_per_block;
	sbi->s_desc_per_block = blocksize / EXT4_DESC_SIZE(sb);
	sbi->s_sbh = bh;
	sbi->s_mount_state = le16_to_cpu(es->s_state);
	sbi->s_addr_per_block_bits = log2(EXT4_ADDR_PER_BLOCK(sb));
	sbi->s_desc_per_block_bits = log2(EXT4_DESC_PER_BLOCK(sb));
	for (i=0; i < 4; i++)
		sbi->s_hash_seed[i] = le32_to_cpu(es->s_hash_seed[i]);
	sbi->s_def_hash_version = es->s_def_hash_version;

	if (sbi->s_blocks_per_group > blocksize * 8) {
		printk (KERN_ERR
			"EXT4-fs: #blocks per group too big: %lu\n",
			sbi->s_blocks_per_group);
		goto failed_mount;
	}
	if (sbi->s_frags_per_group > blocksize * 8) {
		printk (KERN_ERR
			"EXT4-fs: #fragments per group too big: %lu\n",
			sbi->s_frags_per_group);
		goto failed_mount;
	}
	if (sbi->s_inodes_per_group > blocksize * 8) {
		printk (KERN_ERR
			"EXT4-fs: #inodes per group too big: %lu\n",
			sbi->s_inodes_per_group);
		goto failed_mount;
	}

	if (ext4_blocks_count(es) >
		    (sector_t)(~0ULL) >> (sb->s_blocksize_bits - 9)) {
		printk(KERN_ERR "EXT4-fs: filesystem on %s:"
			" too large to mount safely\n", sb->s_id);
		if (sizeof(sector_t) < 8)
			printk(KERN_WARNING "EXT4-fs: CONFIG_LBD not "
					"enabled\n");
		goto failed_mount;
	}

	if (EXT4_BLOCKS_PER_GROUP(sb) == 0)
		goto cantfind_ext4;
	blocks_count = (ext4_blocks_count(es) -
			le32_to_cpu(es->s_first_data_block) +
			EXT4_BLOCKS_PER_GROUP(sb) - 1);
	do_div(blocks_count, EXT4_BLOCKS_PER_GROUP(sb));
	sbi->s_groups_count = blocks_count;
	db_count = (sbi->s_groups_count + EXT4_DESC_PER_BLOCK(sb) - 1) /
		   EXT4_DESC_PER_BLOCK(sb);
	sbi->s_group_desc = kmalloc(db_count * sizeof (struct buffer_head *),
				    GFP_KERNEL);
	if (sbi->s_group_desc == NULL) {
		printk (KERN_ERR "EXT4-fs: not enough memory\n");
		goto failed_mount;
	}

	bgl_lock_init(&sbi->s_blockgroup_lock);

	for (i = 0; i < db_count; i++) {
		block = descriptor_loc(sb, logical_sb_block, i);
		sbi->s_group_desc[i] = sb_bread(sb, block);
		if (!sbi->s_group_desc[i]) {
			printk (KERN_ERR "EXT4-fs: "
				"can't read group descriptor %d\n", i);
			db_count = i;
			goto failed_mount2;
		}
	}
	if (!ext4_check_descriptors (sb)) {
		printk(KERN_ERR "EXT4-fs: group descriptors corrupted!\n");
		goto failed_mount2;
	}
	sbi->s_gdb_count = db_count;
	get_random_bytes(&sbi->s_next_generation, sizeof(u32));
	spin_lock_init(&sbi->s_next_gen_lock);

	percpu_counter_init(&sbi->s_freeblocks_counter,
		ext4_count_free_blocks(sb));
	percpu_counter_init(&sbi->s_freeinodes_counter,
		ext4_count_free_inodes(sb));
	percpu_counter_init(&sbi->s_dirs_counter,
		ext4_count_dirs(sb));

	/* per fileystem reservation list head & lock */
	spin_lock_init(&sbi->s_rsv_window_lock);
	sbi->s_rsv_window_root = RB_ROOT;
	/* Add a single, static dummy reservation to the start of the
	 * reservation window list --- it gives us a placeholder for
	 * append-at-start-of-list which makes the allocation logic
	 * _much_ simpler. */
	sbi->s_rsv_window_head.rsv_start = EXT4_RESERVE_WINDOW_NOT_ALLOCATED;
	sbi->s_rsv_window_head.rsv_end = EXT4_RESERVE_WINDOW_NOT_ALLOCATED;
	sbi->s_rsv_window_head.rsv_alloc_hit = 0;
	sbi->s_rsv_window_head.rsv_goal_size = 0;
	ext4_rsv_window_add(sb, &sbi->s_rsv_window_head);

	/*
	 * set up enough so that it can read an inode
	 */
	sb->s_op = &ext4_sops;
	sb->s_export_op = &ext4_export_ops;
	sb->s_xattr = ext4_xattr_handlers;
#ifdef CONFIG_QUOTA
	sb->s_qcop = &ext4_qctl_operations;
	sb->dq_op = &ext4_quota_operations;
#endif
	INIT_LIST_HEAD(&sbi->s_orphan); /* unlinked but open files */

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
	} else if (journal_inum) {
		if (ext4_create_journal(sb, es, journal_inum))
			goto failed_mount3;
	} else {
		if (!silent)
			printk (KERN_ERR
				"ext4: No journal on filesystem on %s\n",
				sb->s_id);
		goto failed_mount3;
	}

	if (ext4_blocks_count(es) > 0xffffffffULL &&
	    !jbd2_journal_set_features(EXT4_SB(sb)->s_journal, 0, 0,
				       JBD2_FEATURE_INCOMPAT_64BIT)) {
		printk(KERN_ERR "ext4: Failed to set 64-bit journal feature\n");
		goto failed_mount4;
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
			printk(KERN_ERR "EXT4-fs: Journal does not support "
			       "requested data journaling mode\n");
			goto failed_mount4;
		}
	default:
		break;
	}

	if (test_opt(sb, NOBH)) {
		if (!(test_opt(sb, DATA_FLAGS) == EXT4_MOUNT_WRITEBACK_DATA)) {
			printk(KERN_WARNING "EXT4-fs: Ignoring nobh option - "
				"its supported only with writeback mode\n");
			clear_opt(sbi->s_mount_opt, NOBH);
		}
	}
	/*
	 * The jbd2_journal_load will have done any necessary log recovery,
	 * so we can safely mount the rest of the filesystem now.
	 */

	root = iget(sb, EXT4_ROOT_INO);
	sb->s_root = d_alloc_root(root);
	if (!sb->s_root) {
		printk(KERN_ERR "EXT4-fs: get root inode failed\n");
		iput(root);
		goto failed_mount4;
	}
	if (!S_ISDIR(root->i_mode) || !root->i_blocks || !root->i_size) {
		dput(sb->s_root);
		sb->s_root = NULL;
		printk(KERN_ERR "EXT4-fs: corrupt root inode, run e2fsck\n");
		goto failed_mount4;
	}

	ext4_setup_super (sb, es, sb->s_flags & MS_RDONLY);

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
		printk(KERN_INFO "EXT4-fs: required extra inode space not"
			"available.\n");
	}

	/*
	 * akpm: core read_super() calls in here with the superblock locked.
	 * That deadlocks, because orphan cleanup needs to lock the superblock
	 * in numerous places.  Here we just pop the lock - it's relatively
	 * harmless, because we are now ready to accept write_super() requests,
	 * and aviro says that's the only reason for hanging onto the
	 * superblock lock.
	 */
	EXT4_SB(sb)->s_mount_state |= EXT4_ORPHAN_FS;
	ext4_orphan_cleanup(sb, es);
	EXT4_SB(sb)->s_mount_state &= ~EXT4_ORPHAN_FS;
	if (needs_recovery)
		printk (KERN_INFO "EXT4-fs: recovery complete.\n");
	ext4_mark_recovery_complete(sb, es);
	printk (KERN_INFO "EXT4-fs: mounted filesystem with %s data mode.\n",
		test_opt(sb,DATA_FLAGS) == EXT4_MOUNT_JOURNAL_DATA ? "journal":
		test_opt(sb,DATA_FLAGS) == EXT4_MOUNT_ORDERED_DATA ? "ordered":
		"writeback");

	ext4_ext_init(sb);

	lock_kernel();
	return 0;

cantfind_ext4:
	if (!silent)
		printk(KERN_ERR "VFS: Can't find ext4 filesystem on dev %s.\n",
		       sb->s_id);
	goto failed_mount;

failed_mount4:
	jbd2_journal_destroy(sbi->s_journal);
failed_mount3:
	percpu_counter_destroy(&sbi->s_freeblocks_counter);
	percpu_counter_destroy(&sbi->s_freeinodes_counter);
	percpu_counter_destroy(&sbi->s_dirs_counter);
failed_mount2:
	for (i = 0; i < db_count; i++)
		brelse(sbi->s_group_desc[i]);
	kfree(sbi->s_group_desc);
failed_mount:
#ifdef CONFIG_QUOTA
	for (i = 0; i < MAXQUOTAS; i++)
		kfree(sbi->s_qf_names[i]);
#endif
	ext4_blkdev_remove(sbi);
	brelse(bh);
out_fail:
	sb->s_fs_info = NULL;
	kfree(sbi);
	lock_kernel();
	return -EINVAL;
}

/*
 * Setup any per-fs journal parameters now.  We'll do this both on
 * initial mount, once the journal has been initialised but before we've
 * done any recovery; and again on any subsequent remount.
 */
static void ext4_init_journal_params(struct super_block *sb, journal_t *journal)
{
	struct ext4_sb_info *sbi = EXT4_SB(sb);

	if (sbi->s_commit_interval)
		journal->j_commit_interval = sbi->s_commit_interval;
	/* We could also set up an ext4-specific default for the commit
	 * interval here, but for now we'll just fall back to the jbd
	 * default. */

	spin_lock(&journal->j_state_lock);
	if (test_opt(sb, BARRIER))
		journal->j_flags |= JBD2_BARRIER;
	else
		journal->j_flags &= ~JBD2_BARRIER;
	spin_unlock(&journal->j_state_lock);
}

static journal_t *ext4_get_journal(struct super_block *sb,
				   unsigned int journal_inum)
{
	struct inode *journal_inode;
	journal_t *journal;

	/* First, test for the existence of a valid inode on disk.  Bad
	 * things happen if we iget() an unused inode, as the subsequent
	 * iput() will try to delete it. */

	journal_inode = iget(sb, journal_inum);
	if (!journal_inode) {
		printk(KERN_ERR "EXT4-fs: no journal found.\n");
		return NULL;
	}
	if (!journal_inode->i_nlink) {
		make_bad_inode(journal_inode);
		iput(journal_inode);
		printk(KERN_ERR "EXT4-fs: journal inode is deleted.\n");
		return NULL;
	}

	jbd_debug(2, "Journal inode found at %p: %Ld bytes\n",
		  journal_inode, journal_inode->i_size);
	if (is_bad_inode(journal_inode) || !S_ISREG(journal_inode->i_mode)) {
		printk(KERN_ERR "EXT4-fs: invalid journal inode.\n");
		iput(journal_inode);
		return NULL;
	}

	journal = jbd2_journal_init_inode(journal_inode);
	if (!journal) {
		printk(KERN_ERR "EXT4-fs: Could not load journal inode\n");
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
	struct buffer_head * bh;
	journal_t *journal;
	ext4_fsblk_t start;
	ext4_fsblk_t len;
	int hblock, blocksize;
	ext4_fsblk_t sb_block;
	unsigned long offset;
	struct ext4_super_block * es;
	struct block_device *bdev;

	bdev = ext4_blkdev_get(j_dev);
	if (bdev == NULL)
		return NULL;

	if (bd_claim(bdev, sb)) {
		printk(KERN_ERR
			"EXT4: failed to claim external journal device.\n");
		blkdev_put(bdev);
		return NULL;
	}

	blocksize = sb->s_blocksize;
	hblock = bdev_hardsect_size(bdev);
	if (blocksize < hblock) {
		printk(KERN_ERR
			"EXT4-fs: blocksize too small for journal device.\n");
		goto out_bdev;
	}

	sb_block = EXT4_MIN_BLOCK_SIZE / blocksize;
	offset = EXT4_MIN_BLOCK_SIZE % blocksize;
	set_blocksize(bdev, blocksize);
	if (!(bh = __bread(bdev, sb_block, blocksize))) {
		printk(KERN_ERR "EXT4-fs: couldn't read superblock of "
		       "external journal\n");
		goto out_bdev;
	}

	es = (struct ext4_super_block *) (((char *)bh->b_data) + offset);
	if ((le16_to_cpu(es->s_magic) != EXT4_SUPER_MAGIC) ||
	    !(le32_to_cpu(es->s_feature_incompat) &
	      EXT4_FEATURE_INCOMPAT_JOURNAL_DEV)) {
		printk(KERN_ERR "EXT4-fs: external journal has "
					"bad superblock\n");
		brelse(bh);
		goto out_bdev;
	}

	if (memcmp(EXT4_SB(sb)->s_es->s_journal_uuid, es->s_uuid, 16)) {
		printk(KERN_ERR "EXT4-fs: journal UUID does not match\n");
		brelse(bh);
		goto out_bdev;
	}

	len = ext4_blocks_count(es);
	start = sb_block + 1;
	brelse(bh);	/* we're done with the superblock */

	journal = jbd2_journal_init_dev(bdev, sb->s_bdev,
					start, len, blocksize);
	if (!journal) {
		printk(KERN_ERR "EXT4-fs: failed to create device journal\n");
		goto out_bdev;
	}
	journal->j_private = sb;
	ll_rw_block(READ, 1, &journal->j_sb_buffer);
	wait_on_buffer(journal->j_sb_buffer);
	if (!buffer_uptodate(journal->j_sb_buffer)) {
		printk(KERN_ERR "EXT4-fs: I/O error on journal device\n");
		goto out_journal;
	}
	if (be32_to_cpu(journal->j_superblock->s_nr_users) != 1) {
		printk(KERN_ERR "EXT4-fs: External journal has more than one "
					"user (unsupported) - %d\n",
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

	if (journal_devnum &&
	    journal_devnum != le32_to_cpu(es->s_journal_dev)) {
		printk(KERN_INFO "EXT4-fs: external journal device major/minor "
			"numbers have changed\n");
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
			printk(KERN_INFO "EXT4-fs: INFO: recovery "
					"required on readonly filesystem.\n");
			if (really_read_only) {
				printk(KERN_ERR "EXT4-fs: write access "
					"unavailable, cannot proceed.\n");
				return -EROFS;
			}
			printk (KERN_INFO "EXT4-fs: write access will "
					"be enabled during recovery.\n");
		}
	}

	if (journal_inum && journal_dev) {
		printk(KERN_ERR "EXT4-fs: filesystem has both journal "
		       "and inode journals!\n");
		return -EINVAL;
	}

	if (journal_inum) {
		if (!(journal = ext4_get_journal(sb, journal_inum)))
			return -EINVAL;
	} else {
		if (!(journal = ext4_get_dev_journal(sb, journal_dev)))
			return -EINVAL;
	}

	if (!really_read_only && test_opt(sb, UPDATE_JOURNAL)) {
		err = jbd2_journal_update_format(journal);
		if (err)  {
			printk(KERN_ERR "EXT4-fs: error updating journal.\n");
			jbd2_journal_destroy(journal);
			return err;
		}
	}

	if (!EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER))
		err = jbd2_journal_wipe(journal, !really_read_only);
	if (!err)
		err = jbd2_journal_load(journal);

	if (err) {
		printk(KERN_ERR "EXT4-fs: error loading journal.\n");
		jbd2_journal_destroy(journal);
		return err;
	}

	EXT4_SB(sb)->s_journal = journal;
	ext4_clear_journal_err(sb, es);

	if (journal_devnum &&
	    journal_devnum != le32_to_cpu(es->s_journal_dev)) {
		es->s_journal_dev = cpu_to_le32(journal_devnum);
		sb->s_dirt = 1;

		/* Make sure we flush the recovery flag to disk. */
		ext4_commit_super(sb, es, 1);
	}

	return 0;
}

static int ext4_create_journal(struct super_block * sb,
			       struct ext4_super_block * es,
			       unsigned int journal_inum)
{
	journal_t *journal;
	int err;

	if (sb->s_flags & MS_RDONLY) {
		printk(KERN_ERR "EXT4-fs: readonly filesystem when trying to "
				"create journal.\n");
		return -EROFS;
	}

	journal = ext4_get_journal(sb, journal_inum);
	if (!journal)
		return -EINVAL;

	printk(KERN_INFO "EXT4-fs: creating new journal on inode %u\n",
	       journal_inum);

	err = jbd2_journal_create(journal);
	if (err) {
		printk(KERN_ERR "EXT4-fs: error creating journal.\n");
		jbd2_journal_destroy(journal);
		return -EIO;
	}

	EXT4_SB(sb)->s_journal = journal;

	ext4_update_dynamic_rev(sb);
	EXT4_SET_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER);
	EXT4_SET_COMPAT_FEATURE(sb, EXT4_FEATURE_COMPAT_HAS_JOURNAL);

	es->s_journal_inum = cpu_to_le32(journal_inum);
	sb->s_dirt = 1;

	/* Make sure we flush the recovery flag to disk. */
	ext4_commit_super(sb, es, 1);

	return 0;
}

static void ext4_commit_super (struct super_block * sb,
			       struct ext4_super_block * es,
			       int sync)
{
	struct buffer_head *sbh = EXT4_SB(sb)->s_sbh;

	if (!sbh)
		return;
	es->s_wtime = cpu_to_le32(get_seconds());
	ext4_free_blocks_count_set(es, ext4_count_free_blocks(sb));
	es->s_free_inodes_count = cpu_to_le32(ext4_count_free_inodes(sb));
	BUFFER_TRACE(sbh, "marking dirty");
	mark_buffer_dirty(sbh);
	if (sync)
		sync_dirty_buffer(sbh);
}


/*
 * Have we just finished recovery?  If so, and if we are mounting (or
 * remounting) the filesystem readonly, then we will end up with a
 * consistent fs on disk.  Record that fact.
 */
static void ext4_mark_recovery_complete(struct super_block * sb,
					struct ext4_super_block * es)
{
	journal_t *journal = EXT4_SB(sb)->s_journal;

	jbd2_journal_lock_updates(journal);
	jbd2_journal_flush(journal);
	lock_super(sb);
	if (EXT4_HAS_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER) &&
	    sb->s_flags & MS_RDONLY) {
		EXT4_CLEAR_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER);
		sb->s_dirt = 0;
		ext4_commit_super(sb, es, 1);
	}
	unlock_super(sb);
	jbd2_journal_unlock_updates(journal);
}

/*
 * If we are mounting (or read-write remounting) a filesystem whose journal
 * has recorded an error from a previous lifetime, move that error to the
 * main filesystem now.
 */
static void ext4_clear_journal_err(struct super_block * sb,
				   struct ext4_super_block * es)
{
	journal_t *journal;
	int j_errno;
	const char *errstr;

	journal = EXT4_SB(sb)->s_journal;

	/*
	 * Now check for any error status which may have been recorded in the
	 * journal by a prior ext4_error() or ext4_abort()
	 */

	j_errno = jbd2_journal_errno(journal);
	if (j_errno) {
		char nbuf[16];

		errstr = ext4_decode_error(sb, j_errno, nbuf);
		ext4_warning(sb, __FUNCTION__, "Filesystem error recorded "
			     "from previous mount: %s", errstr);
		ext4_warning(sb, __FUNCTION__, "Marking fs in need of "
			     "filesystem check.");

		EXT4_SB(sb)->s_mount_state |= EXT4_ERROR_FS;
		es->s_state |= cpu_to_le16(EXT4_ERROR_FS);
		ext4_commit_super (sb, es, 1);

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
	int ret;

	if (sb->s_flags & MS_RDONLY)
		return 0;

	journal = EXT4_SB(sb)->s_journal;
	sb->s_dirt = 0;
	ret = ext4_journal_force_commit(journal);
	return ret;
}

/*
 * Ext4 always journals updates to the superblock itself, so we don't
 * have to propagate any other updates to the superblock on disk at this
 * point.  Just start an async writeback to get the buffers on their way
 * to the disk.
 *
 * This implicitly triggers the writebehind on sync().
 */

static void ext4_write_super (struct super_block * sb)
{
	if (mutex_trylock(&sb->s_lock) != 0)
		BUG();
	sb->s_dirt = 0;
}

static int ext4_sync_fs(struct super_block *sb, int wait)
{
	tid_t target;

	sb->s_dirt = 0;
	if (jbd2_journal_start_commit(EXT4_SB(sb)->s_journal, &target)) {
		if (wait)
			jbd2_log_wait_commit(EXT4_SB(sb)->s_journal, target);
	}
	return 0;
}

/*
 * LVM calls this function before a (read-only) snapshot is created.  This
 * gives us a chance to flush the journal completely and mark the fs clean.
 */
static void ext4_write_super_lockfs(struct super_block *sb)
{
	sb->s_dirt = 0;

	if (!(sb->s_flags & MS_RDONLY)) {
		journal_t *journal = EXT4_SB(sb)->s_journal;

		/* Now we set up the journal barrier. */
		jbd2_journal_lock_updates(journal);
		jbd2_journal_flush(journal);

		/* Journal blocked and flushed, clear needs_recovery flag. */
		EXT4_CLEAR_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER);
		ext4_commit_super(sb, EXT4_SB(sb)->s_es, 1);
	}
}

/*
 * Called by LVM after the snapshot is done.  We need to reset the RECOVER
 * flag here, even though the filesystem is not technically dirty yet.
 */
static void ext4_unlockfs(struct super_block *sb)
{
	if (!(sb->s_flags & MS_RDONLY)) {
		lock_super(sb);
		/* Reser the needs_recovery flag before the fs is unlocked. */
		EXT4_SET_INCOMPAT_FEATURE(sb, EXT4_FEATURE_INCOMPAT_RECOVER);
		ext4_commit_super(sb, EXT4_SB(sb)->s_es, 1);
		unlock_super(sb);
		jbd2_journal_unlock_updates(EXT4_SB(sb)->s_journal);
	}
}

static int ext4_remount (struct super_block * sb, int * flags, char * data)
{
	struct ext4_super_block * es;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	ext4_fsblk_t n_blocks_count = 0;
	unsigned long old_sb_flags;
	struct ext4_mount_options old_opts;
	int err;
#ifdef CONFIG_QUOTA
	int i;
#endif

	/* Store the original options */
	old_sb_flags = sb->s_flags;
	old_opts.s_mount_opt = sbi->s_mount_opt;
	old_opts.s_resuid = sbi->s_resuid;
	old_opts.s_resgid = sbi->s_resgid;
	old_opts.s_commit_interval = sbi->s_commit_interval;
#ifdef CONFIG_QUOTA
	old_opts.s_jquota_fmt = sbi->s_jquota_fmt;
	for (i = 0; i < MAXQUOTAS; i++)
		old_opts.s_qf_names[i] = sbi->s_qf_names[i];
#endif

	/*
	 * Allow the "check" option to be passed as a remount option.
	 */
	if (!parse_options(data, sb, NULL, NULL, &n_blocks_count, 1)) {
		err = -EINVAL;
		goto restore_opts;
	}

	if (sbi->s_mount_opt & EXT4_MOUNT_ABORT)
		ext4_abort(sb, __FUNCTION__, "Abort forced by user");

	sb->s_flags = (sb->s_flags & ~MS_POSIXACL) |
		((sbi->s_mount_opt & EXT4_MOUNT_POSIX_ACL) ? MS_POSIXACL : 0);

	es = sbi->s_es;

	ext4_init_journal_params(sb, sbi->s_journal);

	if ((*flags & MS_RDONLY) != (sb->s_flags & MS_RDONLY) ||
		n_blocks_count > ext4_blocks_count(es)) {
		if (sbi->s_mount_opt & EXT4_MOUNT_ABORT) {
			err = -EROFS;
			goto restore_opts;
		}

		if (*flags & MS_RDONLY) {
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

			/*
			 * We have to unlock super so that we can wait for
			 * transactions.
			 */
			unlock_super(sb);
			ext4_mark_recovery_complete(sb, es);
			lock_super(sb);
		} else {
			__le32 ret;
			if ((ret = EXT4_HAS_RO_COMPAT_FEATURE(sb,
					~EXT4_FEATURE_RO_COMPAT_SUPP))) {
				printk(KERN_WARNING "EXT4-fs: %s: couldn't "
				       "remount RDWR because of unsupported "
				       "optional features (%x).\n",
				       sb->s_id, le32_to_cpu(ret));
				err = -EROFS;
				goto restore_opts;
			}

			/*
			 * If we have an unprocessed orphan list hanging
			 * around from a previously readonly bdev mount,
			 * require a full umount/remount for now.
			 */
			if (es->s_last_orphan) {
				printk(KERN_WARNING "EXT4-fs: %s: couldn't "
				       "remount RDWR because of unprocessed "
				       "orphan inode list.  Please "
				       "umount/remount instead.\n",
				       sb->s_id);
				err = -EINVAL;
				goto restore_opts;
			}

			/*
			 * Mounting a RDONLY partition read-write, so reread
			 * and store the current valid flag.  (It may have
			 * been changed by e2fsck since we originally mounted
			 * the partition.)
			 */
			ext4_clear_journal_err(sb, es);
			sbi->s_mount_state = le16_to_cpu(es->s_state);
			if ((err = ext4_group_extend(sb, es, n_blocks_count)))
				goto restore_opts;
			if (!ext4_setup_super (sb, es, 0))
				sb->s_flags &= ~MS_RDONLY;
		}
	}
#ifdef CONFIG_QUOTA
	/* Release old quota file names */
	for (i = 0; i < MAXQUOTAS; i++)
		if (old_opts.s_qf_names[i] &&
		    old_opts.s_qf_names[i] != sbi->s_qf_names[i])
			kfree(old_opts.s_qf_names[i]);
#endif
	return 0;
restore_opts:
	sb->s_flags = old_sb_flags;
	sbi->s_mount_opt = old_opts.s_mount_opt;
	sbi->s_resuid = old_opts.s_resuid;
	sbi->s_resgid = old_opts.s_resgid;
	sbi->s_commit_interval = old_opts.s_commit_interval;
#ifdef CONFIG_QUOTA
	sbi->s_jquota_fmt = old_opts.s_jquota_fmt;
	for (i = 0; i < MAXQUOTAS; i++) {
		if (sbi->s_qf_names[i] &&
		    old_opts.s_qf_names[i] != sbi->s_qf_names[i])
			kfree(sbi->s_qf_names[i]);
		sbi->s_qf_names[i] = old_opts.s_qf_names[i];
	}
#endif
	return err;
}

static int ext4_statfs (struct dentry * dentry, struct kstatfs * buf)
{
	struct super_block *sb = dentry->d_sb;
	struct ext4_sb_info *sbi = EXT4_SB(sb);
	struct ext4_super_block *es = sbi->s_es;
	u64 fsid;

	if (test_opt(sb, MINIX_DF)) {
		sbi->s_overhead_last = 0;
	} else if (sbi->s_blocks_last != le32_to_cpu(es->s_blocks_count)) {
		unsigned long ngroups = sbi->s_groups_count, i;
		ext4_fsblk_t overhead = 0;
		smp_rmb();

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
		sbi->s_blocks_last = le32_to_cpu(es->s_blocks_count);
	}

	buf->f_type = EXT4_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = ext4_blocks_count(es) - sbi->s_overhead_last;
	buf->f_bfree = percpu_counter_sum(&sbi->s_freeblocks_counter);
	es->s_free_blocks_count = cpu_to_le32(buf->f_bfree);
	buf->f_bavail = buf->f_bfree - ext4_r_blocks_count(es);
	if (buf->f_bfree < ext4_r_blocks_count(es))
		buf->f_bavail = 0;
	buf->f_files = le32_to_cpu(es->s_inodes_count);
	buf->f_ffree = percpu_counter_sum(&sbi->s_freeinodes_counter);
	es->s_free_inodes_count = cpu_to_le32(buf->f_ffree);
	buf->f_namelen = EXT4_NAME_LEN;
	fsid = le64_to_cpup((void *)es->s_uuid) ^
	       le64_to_cpup((void *)es->s_uuid + sizeof(u64));
	buf->f_fsid.val[0] = fsid & 0xFFFFFFFFUL;
	buf->f_fsid.val[1] = (fsid >> 32) & 0xFFFFFFFFUL;
	return 0;
}

/* Helper function for writing quotas on sync - we need to start transaction before quota file
 * is locked for write. Otherwise the are possible deadlocks:
 * Process 1                         Process 2
 * ext4_create()                     quota_sync()
 *   jbd2_journal_start()                   write_dquot()
 *   DQUOT_INIT()                        down(dqio_mutex)
 *     down(dqio_mutex)                    jbd2_journal_start()
 *
 */

#ifdef CONFIG_QUOTA

static inline struct inode *dquot_to_inode(struct dquot *dquot)
{
	return sb_dqopt(dquot->dq_sb)->files[dquot->dq_type];
}

static int ext4_dquot_initialize(struct inode *inode, int type)
{
	handle_t *handle;
	int ret, err;

	/* We may create quota structure so we need to reserve enough blocks */
	handle = ext4_journal_start(inode, 2*EXT4_QUOTA_INIT_BLOCKS(inode->i_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);
	ret = dquot_initialize(inode, type);
	err = ext4_journal_stop(handle);
	if (!ret)
		ret = err;
	return ret;
}

static int ext4_dquot_drop(struct inode *inode)
{
	handle_t *handle;
	int ret, err;

	/* We may delete quota structure so we need to reserve enough blocks */
	handle = ext4_journal_start(inode, 2*EXT4_QUOTA_DEL_BLOCKS(inode->i_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);
	ret = dquot_drop(inode);
	err = ext4_journal_stop(handle);
	if (!ret)
		ret = err;
	return ret;
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
	if (IS_ERR(handle))
		return PTR_ERR(handle);
	ret = dquot_release(dquot);
	err = ext4_journal_stop(handle);
	if (!ret)
		ret = err;
	return ret;
}

static int ext4_mark_dquot_dirty(struct dquot *dquot)
{
	/* Are we journalling quotas? */
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
	return vfs_quota_on_mount(sb, EXT4_SB(sb)->s_qf_names[type],
			EXT4_SB(sb)->s_jquota_fmt, type);
}

/*
 * Standard function to be called on quota_on
 */
static int ext4_quota_on(struct super_block *sb, int type, int format_id,
			 char *path)
{
	int err;
	struct nameidata nd;

	if (!test_opt(sb, QUOTA))
		return -EINVAL;
	/* Not journalling quota? */
	if (!EXT4_SB(sb)->s_qf_names[USRQUOTA] &&
	    !EXT4_SB(sb)->s_qf_names[GRPQUOTA])
		return vfs_quota_on(sb, type, format_id, path);
	err = path_lookup(path, LOOKUP_FOLLOW, &nd);
	if (err)
		return err;
	/* Quotafile not on the same filesystem? */
	if (nd.mnt->mnt_sb != sb) {
		path_release(&nd);
		return -EXDEV;
	}
	/* Quotafile not of fs root? */
	if (nd.dentry->d_parent->d_inode != sb->s_root->d_inode)
		printk(KERN_WARNING
			"EXT4-fs: Quota file not on filesystem root. "
			"Journalled quota will not work.\n");
	path_release(&nd);
	return vfs_quota_on(sb, type, format_id, path);
}

/* Read data from quotafile - avoid pagecache and such because we cannot afford
 * acquiring the locks... As quota files are never truncated and quota code
 * itself serializes the operations (and noone else should touch the files)
 * we don't have to be afraid of races */
static ssize_t ext4_quota_read(struct super_block *sb, int type, char *data,
			       size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	sector_t blk = off >> EXT4_BLOCK_SIZE_BITS(sb);
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
	sector_t blk = off >> EXT4_BLOCK_SIZE_BITS(sb);
	int err = 0;
	int offset = off & (sb->s_blocksize - 1);
	int tocopy;
	int journal_quota = EXT4_SB(sb)->s_qf_names[type] != NULL;
	size_t towrite = len;
	struct buffer_head *bh;
	handle_t *handle = journal_current_handle();

	mutex_lock_nested(&inode->i_mutex, I_MUTEX_QUOTA);
	while (towrite > 0) {
		tocopy = sb->s_blocksize - offset < towrite ?
				sb->s_blocksize - offset : towrite;
		bh = ext4_bread(handle, inode, blk, 1, &err);
		if (!bh)
			goto out;
		if (journal_quota) {
			err = ext4_journal_get_write_access(handle, bh);
			if (err) {
				brelse(bh);
				goto out;
			}
		}
		lock_buffer(bh);
		memcpy(bh->b_data+offset, data, tocopy);
		flush_dcache_page(bh->b_page);
		unlock_buffer(bh);
		if (journal_quota)
			err = ext4_journal_dirty_metadata(handle, bh);
		else {
			/* Always do at least ordered writes for quotas */
			err = ext4_journal_dirty_data(handle, bh);
			mark_buffer_dirty(bh);
		}
		brelse(bh);
		if (err)
			goto out;
		offset = 0;
		towrite -= tocopy;
		data += tocopy;
		blk++;
	}
out:
	if (len == towrite)
		return err;
	if (inode->i_size < off+len-towrite) {
		i_size_write(inode, off+len-towrite);
		EXT4_I(inode)->i_disksize = inode->i_size;
	}
	inode->i_version++;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	ext4_mark_inode_dirty(handle, inode);
	mutex_unlock(&inode->i_mutex);
	return len - towrite;
}

#endif

static int ext4_get_sb(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data, struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, ext4_fill_super, mnt);
}

static struct file_system_type ext4dev_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ext4dev",
	.get_sb		= ext4_get_sb,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};

static int __init init_ext4_fs(void)
{
	int err = init_ext4_xattr();
	if (err)
		return err;
	err = init_inodecache();
	if (err)
		goto out1;
	err = register_filesystem(&ext4dev_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
out1:
	exit_ext4_xattr();
	return err;
}

static void __exit exit_ext4_fs(void)
{
	unregister_filesystem(&ext4dev_fs_type);
	destroy_inodecache();
	exit_ext4_xattr();
}

MODULE_AUTHOR("Remy Card, Stephen Tweedie, Andrew Morton, Andreas Dilger, Theodore Ts'o and others");
MODULE_DESCRIPTION("Fourth Extended Filesystem with extents");
MODULE_LICENSE("GPL");
module_init(init_ext4_fs)
module_exit(exit_ext4_fs)
