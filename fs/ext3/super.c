/*
 *  linux/fs/ext3/super.c
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
#include <linux/blkdev.h>
#include <linux/parser.h>
#include <linux/exportfs.h>
#include <linux/statfs.h>
#include <linux/random.h>
#include <linux/mount.h>
#include <linux/quotaops.h>
#include <linux/seq_file.h>
#include <linux/log2.h>
#include <linux/cleancache.h>
#include <linux/namei.h>

#include <asm/uaccess.h>

#define CREATE_TRACE_POINTS

#include "ext3.h"
#include "xattr.h"
#include "acl.h"
#include "namei.h"

#ifdef CONFIG_EXT3_DEFAULTS_TO_ORDERED
  #define EXT3_MOUNT_DEFAULT_DATA_MODE EXT3_MOUNT_ORDERED_DATA
#else
  #define EXT3_MOUNT_DEFAULT_DATA_MODE EXT3_MOUNT_WRITEBACK_DATA
#endif

static int ext3_load_journal(struct super_block *, struct ext3_super_block *,
			     unsigned long journal_devnum);
static int ext3_create_journal(struct super_block *, struct ext3_super_block *,
			       unsigned int);
static int ext3_commit_super(struct super_block *sb,
			       struct ext3_super_block *es,
			       int sync);
static void ext3_mark_recovery_complete(struct super_block * sb,
					struct ext3_super_block * es);
static void ext3_clear_journal_err(struct super_block * sb,
				   struct ext3_super_block * es);
static int ext3_sync_fs(struct super_block *sb, int wait);
static const char *ext3_decode_error(struct super_block * sb, int errno,
				     char nbuf[16]);
static int ext3_remount (struct super_block * sb, int * flags, char * data);
static int ext3_statfs (struct dentry * dentry, struct kstatfs * buf);
static int ext3_unfreeze(struct super_block *sb);
static int ext3_freeze(struct super_block *sb);

/*
 * Wrappers for journal_start/end.
 */
handle_t *ext3_journal_start_sb(struct super_block *sb, int nblocks)
{
	journal_t *journal;

	if (sb->s_flags & MS_RDONLY)
		return ERR_PTR(-EROFS);

	/* Special case here: if the journal has aborted behind our
	 * backs (eg. EIO in the commit thread), then we still need to
	 * take the FS itself readonly cleanly. */
	journal = EXT3_SB(sb)->s_journal;
	if (is_journal_aborted(journal)) {
		ext3_abort(sb, __func__,
			   "Detected aborted journal");
		return ERR_PTR(-EROFS);
	}

	return journal_start(journal, nblocks);
}

int __ext3_journal_stop(const char *where, handle_t *handle)
{
	struct super_block *sb;
	int err;
	int rc;

	sb = handle->h_transaction->t_journal->j_private;
	err = handle->h_err;
	rc = journal_stop(handle);

	if (!err)
		err = rc;
	if (err)
		__ext3_std_error(sb, where, err);
	return err;
}

void ext3_journal_abort_handle(const char *caller, const char *err_fn,
		struct buffer_head *bh, handle_t *handle, int err)
{
	char nbuf[16];
	const char *errstr = ext3_decode_error(NULL, err, nbuf);

	if (bh)
		BUFFER_TRACE(bh, "abort");

	if (!handle->h_err)
		handle->h_err = err;

	if (is_handle_aborted(handle))
		return;

	printk(KERN_ERR "EXT3-fs: %s: aborting transaction: %s in %s\n",
		caller, errstr, err_fn);

	journal_abort_handle(handle);
}

void ext3_msg(struct super_block *sb, const char *prefix,
		const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk("%sEXT3-fs (%s): %pV\n", prefix, sb->s_id, &vaf);

	va_end(args);
}

/* Deal with the reporting of failure conditions on a filesystem such as
 * inconsistencies detected or read IO failures.
 *
 * On ext2, we can store the error state of the filesystem in the
 * superblock.  That is not possible on ext3, because we may have other
 * write ordering constraints on the superblock which prevent us from
 * writing it out straight away; and given that the journal is about to
 * be aborted, we can't rely on the current, or future, transactions to
 * write out the superblock safely.
 *
 * We'll just use the journal_abort() error code to record an error in
 * the journal instead.  On recovery, the journal will complain about
 * that error until we've noted it down and cleared it.
 */

static void ext3_handle_error(struct super_block *sb)
{
	struct ext3_super_block *es = EXT3_SB(sb)->s_es;

	EXT3_SB(sb)->s_mount_state |= EXT3_ERROR_FS;
	es->s_state |= cpu_to_le16(EXT3_ERROR_FS);

	if (sb->s_flags & MS_RDONLY)
		return;

	if (!test_opt (sb, ERRORS_CONT)) {
		journal_t *journal = EXT3_SB(sb)->s_journal;

		set_opt(EXT3_SB(sb)->s_mount_opt, ABORT);
		if (journal)
			journal_abort(journal, -EIO);
	}
	if (test_opt (sb, ERRORS_RO)) {
		ext3_msg(sb, KERN_CRIT,
			"error: remounting filesystem read-only");
		/*
		 * Make sure updated value of ->s_mount_state will be visible
		 * before ->s_flags update.
		 */
		smp_wmb();
		sb->s_flags |= MS_RDONLY;
	}
	ext3_commit_super(sb, es, 1);
	if (test_opt(sb, ERRORS_PANIC))
		panic("EXT3-fs (%s): panic forced after error\n",
			sb->s_id);
}

void ext3_error(struct super_block *sb, const char *function,
		const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk(KERN_CRIT "EXT3-fs error (device %s): %s: %pV\n",
	       sb->s_id, function, &vaf);

	va_end(args);

	ext3_handle_error(sb);
}

static const char *ext3_decode_error(struct super_block * sb, int errno,
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
		if (!sb || EXT3_SB(sb)->s_journal->j_flags & JFS_ABORT)
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

/* __ext3_std_error decodes expected errors from journaling functions
 * automatically and invokes the appropriate error response.  */

void __ext3_std_error (struct super_block * sb, const char * function,
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

	errstr = ext3_decode_error(sb, errno, nbuf);
	ext3_msg(sb, KERN_CRIT, "error in %s: %s", function, errstr);

	ext3_handle_error(sb);
}

/*
 * ext3_abort is a much stronger failure handler than ext3_error.  The
 * abort function may be used to deal with unrecoverable failures such
 * as journal IO errors or ENOMEM at a critical moment in log management.
 *
 * We unconditionally force the filesystem into an ABORT|READONLY state,
 * unless the error response on the fs has been set to panic in which
 * case we take the easy way out and panic immediately.
 */

void ext3_abort(struct super_block *sb, const char *function,
		 const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk(KERN_CRIT "EXT3-fs (%s): error: %s: %pV\n",
	       sb->s_id, function, &vaf);

	va_end(args);

	if (test_opt(sb, ERRORS_PANIC))
		panic("EXT3-fs: panic from previous error\n");

	if (sb->s_flags & MS_RDONLY)
		return;

	ext3_msg(sb, KERN_CRIT,
		"error: remounting filesystem read-only");
	EXT3_SB(sb)->s_mount_state |= EXT3_ERROR_FS;
	set_opt(EXT3_SB(sb)->s_mount_opt, ABORT);
	/*
	 * Make sure updated value of ->s_mount_state will be visible
	 * before ->s_flags update.
	 */
	smp_wmb();
	sb->s_flags |= MS_RDONLY;

	if (EXT3_SB(sb)->s_journal)
		journal_abort(EXT3_SB(sb)->s_journal, -EIO);
}

void ext3_warning(struct super_block *sb, const char *function,
		  const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk(KERN_WARNING "EXT3-fs (%s): warning: %s: %pV\n",
	       sb->s_id, function, &vaf);

	va_end(args);
}

void ext3_update_dynamic_rev(struct super_block *sb)
{
	struct ext3_super_block *es = EXT3_SB(sb)->s_es;

	if (le32_to_cpu(es->s_rev_level) > EXT3_GOOD_OLD_REV)
		return;

	ext3_msg(sb, KERN_WARNING,
		"warning: updating to rev %d because of "
		"new feature flag, running e2fsck is recommended",
		EXT3_DYNAMIC_REV);

	es->s_first_ino = cpu_to_le32(EXT3_GOOD_OLD_FIRST_INO);
	es->s_inode_size = cpu_to_le16(EXT3_GOOD_OLD_INODE_SIZE);
	es->s_rev_level = cpu_to_le32(EXT3_DYNAMIC_REV);
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
static struct block_device *ext3_blkdev_get(dev_t dev, struct super_block *sb)
{
	struct block_device *bdev;
	char b[BDEVNAME_SIZE];

	bdev = blkdev_get_by_dev(dev, FMODE_READ|FMODE_WRITE|FMODE_EXCL, sb);
	if (IS_ERR(bdev))
		goto fail;
	return bdev;

fail:
	ext3_msg(sb, KERN_ERR, "error: failed to open journal device %s: %ld",
		__bdevname(dev, b), PTR_ERR(bdev));

	return NULL;
}

/*
 * Release the journal device
 */
static void ext3_blkdev_put(struct block_device *bdev)
{
	blkdev_put(bdev, FMODE_READ|FMODE_WRITE|FMODE_EXCL);
}

static void ext3_blkdev_remove(struct ext3_sb_info *sbi)
{
	struct block_device *bdev;
	bdev = sbi->journal_bdev;
	if (bdev) {
		ext3_blkdev_put(bdev);
		sbi->journal_bdev = NULL;
	}
}

static inline struct inode *orphan_list_entry(struct list_head *l)
{
	return &list_entry(l, struct ext3_inode_info, i_orphan)->vfs_inode;
}

static void dump_orphan_list(struct super_block *sb, struct ext3_sb_info *sbi)
{
	struct list_head *l;

	ext3_msg(sb, KERN_ERR, "error: sb orphan head is %d",
	       le32_to_cpu(sbi->s_es->s_last_orphan));

	ext3_msg(sb, KERN_ERR, "sb_info orphan list:");
	list_for_each(l, &sbi->s_orphan) {
		struct inode *inode = orphan_list_entry(l);
		ext3_msg(sb, KERN_ERR, "  "
		       "inode %s:%lu at %p: mode %o, nlink %d, next %d\n",
		       inode->i_sb->s_id, inode->i_ino, inode,
		       inode->i_mode, inode->i_nlink,
		       NEXT_ORPHAN(inode));
	}
}

static void ext3_put_super (struct super_block * sb)
{
	struct ext3_sb_info *sbi = EXT3_SB(sb);
	struct ext3_super_block *es = sbi->s_es;
	int i, err;

	dquot_disable(sb, -1, DQUOT_USAGE_ENABLED | DQUOT_LIMITS_ENABLED);
	ext3_xattr_put_super(sb);
	err = journal_destroy(sbi->s_journal);
	sbi->s_journal = NULL;
	if (err < 0)
		ext3_abort(sb, __func__, "Couldn't clean up the journal");

	if (!(sb->s_flags & MS_RDONLY)) {
		EXT3_CLEAR_INCOMPAT_FEATURE(sb, EXT3_FEATURE_INCOMPAT_RECOVER);
		es->s_state = cpu_to_le16(sbi->s_mount_state);
		BUFFER_TRACE(sbi->s_sbh, "marking dirty");
		mark_buffer_dirty(sbi->s_sbh);
		ext3_commit_super(sb, es, 1);
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
		ext3_blkdev_remove(sbi);
	}
	sb->s_fs_info = NULL;
	kfree(sbi->s_blockgroup_lock);
	kfree(sbi);
}

static struct kmem_cache *ext3_inode_cachep;

/*
 * Called inside transaction, so use GFP_NOFS
 */
static struct inode *ext3_alloc_inode(struct super_block *sb)
{
	struct ext3_inode_info *ei;

	ei = kmem_cache_alloc(ext3_inode_cachep, GFP_NOFS);
	if (!ei)
		return NULL;
	ei->i_block_alloc_info = NULL;
	ei->vfs_inode.i_version = 1;
	atomic_set(&ei->i_datasync_tid, 0);
	atomic_set(&ei->i_sync_tid, 0);
	return &ei->vfs_inode;
}

static int ext3_drop_inode(struct inode *inode)
{
	int drop = generic_drop_inode(inode);

	trace_ext3_drop_inode(inode, drop);
	return drop;
}

static void ext3_i_callback(struct rcu_head *head)
{
	struct inode *inode = container_of(head, struct inode, i_rcu);
	kmem_cache_free(ext3_inode_cachep, EXT3_I(inode));
}

static void ext3_destroy_inode(struct inode *inode)
{
	if (!list_empty(&(EXT3_I(inode)->i_orphan))) {
		printk("EXT3 Inode %p: orphan list check failed!\n",
			EXT3_I(inode));
		print_hex_dump(KERN_INFO, "", DUMP_PREFIX_ADDRESS, 16, 4,
				EXT3_I(inode), sizeof(struct ext3_inode_info),
				false);
		dump_stack();
	}
	call_rcu(&inode->i_rcu, ext3_i_callback);
}

static void init_once(void *foo)
{
	struct ext3_inode_info *ei = (struct ext3_inode_info *) foo;

	INIT_LIST_HEAD(&ei->i_orphan);
#ifdef CONFIG_EXT3_FS_XATTR
	init_rwsem(&ei->xattr_sem);
#endif
	mutex_init(&ei->truncate_mutex);
	inode_init_once(&ei->vfs_inode);
}

static int __init init_inodecache(void)
{
	ext3_inode_cachep = kmem_cache_create("ext3_inode_cache",
					     sizeof(struct ext3_inode_info),
					     0, (SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
					     init_once);
	if (ext3_inode_cachep == NULL)
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
	kmem_cache_destroy(ext3_inode_cachep);
}

static inline void ext3_show_quota_options(struct seq_file *seq, struct super_block *sb)
{
#if defined(CONFIG_QUOTA)
	struct ext3_sb_info *sbi = EXT3_SB(sb);

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

static char *data_mode_string(unsigned long mode)
{
	switch (mode) {
	case EXT3_MOUNT_JOURNAL_DATA:
		return "journal";
	case EXT3_MOUNT_ORDERED_DATA:
		return "ordered";
	case EXT3_MOUNT_WRITEBACK_DATA:
		return "writeback";
	}
	return "unknown";
}

/*
 * Show an option if
 *  - it's set to a non-default value OR
 *  - if the per-sb default is different from the global default
 */
static int ext3_show_options(struct seq_file *seq, struct dentry *root)
{
	struct super_block *sb = root->d_sb;
	struct ext3_sb_info *sbi = EXT3_SB(sb);
	struct ext3_super_block *es = sbi->s_es;
	unsigned long def_mount_opts;

	def_mount_opts = le32_to_cpu(es->s_default_mount_opts);

	if (sbi->s_sb_block != 1)
		seq_printf(seq, ",sb=%lu", sbi->s_sb_block);
	if (test_opt(sb, MINIX_DF))
		seq_puts(seq, ",minixdf");
	if (test_opt(sb, GRPID))
		seq_puts(seq, ",grpid");
	if (!test_opt(sb, GRPID) && (def_mount_opts & EXT3_DEFM_BSDGROUPS))
		seq_puts(seq, ",nogrpid");
	if (!uid_eq(sbi->s_resuid, make_kuid(&init_user_ns, EXT3_DEF_RESUID)) ||
	    le16_to_cpu(es->s_def_resuid) != EXT3_DEF_RESUID) {
		seq_printf(seq, ",resuid=%u",
				from_kuid_munged(&init_user_ns, sbi->s_resuid));
	}
	if (!gid_eq(sbi->s_resgid, make_kgid(&init_user_ns, EXT3_DEF_RESGID)) ||
	    le16_to_cpu(es->s_def_resgid) != EXT3_DEF_RESGID) {
		seq_printf(seq, ",resgid=%u",
				from_kgid_munged(&init_user_ns, sbi->s_resgid));
	}
	if (test_opt(sb, ERRORS_RO)) {
		int def_errors = le16_to_cpu(es->s_errors);

		if (def_errors == EXT3_ERRORS_PANIC ||
		    def_errors == EXT3_ERRORS_CONTINUE) {
			seq_puts(seq, ",errors=remount-ro");
		}
	}
	if (test_opt(sb, ERRORS_CONT))
		seq_puts(seq, ",errors=continue");
	if (test_opt(sb, ERRORS_PANIC))
		seq_puts(seq, ",errors=panic");
	if (test_opt(sb, NO_UID32))
		seq_puts(seq, ",nouid32");
	if (test_opt(sb, DEBUG))
		seq_puts(seq, ",debug");
#ifdef CONFIG_EXT3_FS_XATTR
	if (test_opt(sb, XATTR_USER))
		seq_puts(seq, ",user_xattr");
	if (!test_opt(sb, XATTR_USER) &&
	    (def_mount_opts & EXT3_DEFM_XATTR_USER)) {
		seq_puts(seq, ",nouser_xattr");
	}
#endif
#ifdef CONFIG_EXT3_FS_POSIX_ACL
	if (test_opt(sb, POSIX_ACL))
		seq_puts(seq, ",acl");
	if (!test_opt(sb, POSIX_ACL) && (def_mount_opts & EXT3_DEFM_ACL))
		seq_puts(seq, ",noacl");
#endif
	if (!test_opt(sb, RESERVATION))
		seq_puts(seq, ",noreservation");
	if (sbi->s_commit_interval) {
		seq_printf(seq, ",commit=%u",
			   (unsigned) (sbi->s_commit_interval / HZ));
	}

	/*
	 * Always display barrier state so it's clear what the status is.
	 */
	seq_puts(seq, ",barrier=");
	seq_puts(seq, test_opt(sb, BARRIER) ? "1" : "0");
	seq_printf(seq, ",data=%s", data_mode_string(test_opt(sb, DATA_FLAGS)));
	if (test_opt(sb, DATA_ERR_ABORT))
		seq_puts(seq, ",data_err=abort");

	if (test_opt(sb, NOLOAD))
		seq_puts(seq, ",norecovery");

	ext3_show_quota_options(seq, sb);

	return 0;
}


static struct inode *ext3_nfs_get_inode(struct super_block *sb,
		u64 ino, u32 generation)
{
	struct inode *inode;

	if (ino < EXT3_FIRST_INO(sb) && ino != EXT3_ROOT_INO)
		return ERR_PTR(-ESTALE);
	if (ino > le32_to_cpu(EXT3_SB(sb)->s_es->s_inodes_count))
		return ERR_PTR(-ESTALE);

	/* iget isn't really right if the inode is currently unallocated!!
	 *
	 * ext3_read_inode will return a bad_inode if the inode had been
	 * deleted, so we should be safe.
	 *
	 * Currently we don't know the generation for parent directory, so
	 * a generation of 0 means "accept any"
	 */
	inode = ext3_iget(sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	if (generation && inode->i_generation != generation) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}

	return inode;
}

static struct dentry *ext3_fh_to_dentry(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    ext3_nfs_get_inode);
}

static struct dentry *ext3_fh_to_parent(struct super_block *sb, struct fid *fid,
		int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    ext3_nfs_get_inode);
}

/*
 * Try to release metadata pages (indirect blocks, directories) which are
 * mapped via the block device.  Since these pages could have journal heads
 * which would prevent try_to_free_buffers() from freeing them, we must use
 * jbd layer's try_to_free_buffers() function to release them.
 */
static int bdev_try_to_free_page(struct super_block *sb, struct page *page,
				 gfp_t wait)
{
	journal_t *journal = EXT3_SB(sb)->s_journal;

	WARN_ON(PageChecked(page));
	if (!page_has_buffers(page))
		return 0;
	if (journal)
		return journal_try_to_free_buffers(journal, page, 
						   wait & ~__GFP_WAIT);
	return try_to_free_buffers(page);
}

#ifdef CONFIG_QUOTA
#define QTYPE2NAME(t) ((t)==USRQUOTA?"user":"group")
#define QTYPE2MOPT(on, t) ((t)==USRQUOTA?((on)##USRJQUOTA):((on)##GRPJQUOTA))

static int ext3_write_dquot(struct dquot *dquot);
static int ext3_acquire_dquot(struct dquot *dquot);
static int ext3_release_dquot(struct dquot *dquot);
static int ext3_mark_dquot_dirty(struct dquot *dquot);
static int ext3_write_info(struct super_block *sb, int type);
static int ext3_quota_on(struct super_block *sb, int type, int format_id,
			 struct path *path);
static int ext3_quota_on_mount(struct super_block *sb, int type);
static ssize_t ext3_quota_read(struct super_block *sb, int type, char *data,
			       size_t len, loff_t off);
static ssize_t ext3_quota_write(struct super_block *sb, int type,
				const char *data, size_t len, loff_t off);

static const struct dquot_operations ext3_quota_operations = {
	.write_dquot	= ext3_write_dquot,
	.acquire_dquot	= ext3_acquire_dquot,
	.release_dquot	= ext3_release_dquot,
	.mark_dirty	= ext3_mark_dquot_dirty,
	.write_info	= ext3_write_info,
	.alloc_dquot	= dquot_alloc,
	.destroy_dquot	= dquot_destroy,
};

static const struct quotactl_ops ext3_qctl_operations = {
	.quota_on	= ext3_quota_on,
	.quota_off	= dquot_quota_off,
	.quota_sync	= dquot_quota_sync,
	.get_info	= dquot_get_dqinfo,
	.set_info	= dquot_set_dqinfo,
	.get_dqblk	= dquot_get_dqblk,
	.set_dqblk	= dquot_set_dqblk
};
#endif

static const struct super_operations ext3_sops = {
	.alloc_inode	= ext3_alloc_inode,
	.destroy_inode	= ext3_destroy_inode,
	.write_inode	= ext3_write_inode,
	.dirty_inode	= ext3_dirty_inode,
	.drop_inode	= ext3_drop_inode,
	.evict_inode	= ext3_evict_inode,
	.put_super	= ext3_put_super,
	.sync_fs	= ext3_sync_fs,
	.freeze_fs	= ext3_freeze,
	.unfreeze_fs	= ext3_unfreeze,
	.statfs		= ext3_statfs,
	.remount_fs	= ext3_remount,
	.show_options	= ext3_show_options,
#ifdef CONFIG_QUOTA
	.quota_read	= ext3_quota_read,
	.quota_write	= ext3_quota_write,
#endif
	.bdev_try_to_free_page = bdev_try_to_free_page,
};

static const struct export_operations ext3_export_ops = {
	.fh_to_dentry = ext3_fh_to_dentry,
	.fh_to_parent = ext3_fh_to_parent,
	.get_parent = ext3_get_parent,
};

enum {
	Opt_bsd_df, Opt_minix_df, Opt_grpid, Opt_nogrpid,
	Opt_resgid, Opt_resuid, Opt_sb, Opt_err_cont, Opt_err_panic, Opt_err_ro,
	Opt_nouid32, Opt_nocheck, Opt_debug, Opt_oldalloc, Opt_orlov,
	Opt_user_xattr, Opt_nouser_xattr, Opt_acl, Opt_noacl,
	Opt_reservation, Opt_noreservation, Opt_noload, Opt_nobh, Opt_bh,
	Opt_commit, Opt_journal_update, Opt_journal_inum, Opt_journal_dev,
	Opt_journal_path,
	Opt_abort, Opt_data_journal, Opt_data_ordered, Opt_data_writeback,
	Opt_data_err_abort, Opt_data_err_ignore,
	Opt_usrjquota, Opt_grpjquota, Opt_offusrjquota, Opt_offgrpjquota,
	Opt_jqfmt_vfsold, Opt_jqfmt_vfsv0, Opt_jqfmt_vfsv1, Opt_quota,
	Opt_noquota, Opt_ignore, Opt_barrier, Opt_nobarrier, Opt_err,
	Opt_resize, Opt_usrquota, Opt_grpquota
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
	{Opt_noload, "norecovery"},
	{Opt_nobh, "nobh"},
	{Opt_bh, "bh"},
	{Opt_commit, "commit=%u"},
	{Opt_journal_update, "journal=update"},
	{Opt_journal_inum, "journal=%u"},
	{Opt_journal_dev, "journal_dev=%u"},
	{Opt_journal_path, "journal_path=%s"},
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
	{Opt_resize, "resize"},
	{Opt_err, NULL},
};

static ext3_fsblk_t get_sb_block(void **data, struct super_block *sb)
{
	ext3_fsblk_t	sb_block;
	char		*options = (char *) *data;

	if (!options || strncmp(options, "sb=", 3) != 0)
		return 1;	/* Default location */
	options += 3;
	/*todo: use simple_strtoll with >32bit ext3 */
	sb_block = simple_strtoul(options, &options, 0);
	if (*options && *options != ',') {
		ext3_msg(sb, KERN_ERR, "error: invalid sb specification: %s",
		       (char *) *data);
		return 1;
	}
	if (*options == ',')
		options++;
	*data = (void *) options;
	return sb_block;
}

#ifdef CONFIG_QUOTA
static int set_qf_name(struct super_block *sb, int qtype, substring_t *args)
{
	struct ext3_sb_info *sbi = EXT3_SB(sb);
	char *qname;

	if (sb_any_quota_loaded(sb) &&
		!sbi->s_qf_names[qtype]) {
		ext3_msg(sb, KERN_ERR,
			"Cannot change journaled "
			"quota options when quota turned on");
		return 0;
	}
	qname = match_strdup(args);
	if (!qname) {
		ext3_msg(sb, KERN_ERR,
			"Not enough memory for storing quotafile name");
		return 0;
	}
	if (sbi->s_qf_names[qtype]) {
		int same = !strcmp(sbi->s_qf_names[qtype], qname);

		kfree(qname);
		if (!same) {
			ext3_msg(sb, KERN_ERR,
				 "%s quota file already specified",
				 QTYPE2NAME(qtype));
		}
		return same;
	}
	if (strchr(qname, '/')) {
		ext3_msg(sb, KERN_ERR,
			"quotafile must be on filesystem root");
		kfree(qname);
		return 0;
	}
	sbi->s_qf_names[qtype] = qname;
	set_opt(sbi->s_mount_opt, QUOTA);
	return 1;
}

static int clear_qf_name(struct super_block *sb, int qtype) {

	struct ext3_sb_info *sbi = EXT3_SB(sb);

	if (sb_any_quota_loaded(sb) &&
		sbi->s_qf_names[qtype]) {
		ext3_msg(sb, KERN_ERR, "Cannot change journaled quota options"
			" when quota turned on");
		return 0;
	}
	if (sbi->s_qf_names[qtype]) {
		kfree(sbi->s_qf_names[qtype]);
		sbi->s_qf_names[qtype] = NULL;
	}
	return 1;
}
#endif

static int parse_options (char *options, struct super_block *sb,
			  unsigned int *inum, unsigned long *journal_devnum,
			  ext3_fsblk_t *n_blocks_count, int is_remount)
{
	struct ext3_sb_info *sbi = EXT3_SB(sb);
	char * p;
	substring_t args[MAX_OPT_ARGS];
	int data_opt = 0;
	int option;
	kuid_t uid;
	kgid_t gid;
	char *journal_path;
	struct inode *journal_inode;
	struct path path;
	int error;

#ifdef CONFIG_QUOTA
	int qfmt;
#endif

	if (!options)
		return 1;

	while ((p = strsep (&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;
		/*
		 * Initialize args struct so we know whether arg was
		 * found; some options take optional arguments.
		 */
		args[0].to = args[0].from = NULL;
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
			uid = make_kuid(current_user_ns(), option);
			if (!uid_valid(uid)) {
				ext3_msg(sb, KERN_ERR, "Invalid uid value %d", option);
				return 0;

			}
			sbi->s_resuid = uid;
			break;
		case Opt_resgid:
			if (match_int(&args[0], &option))
				return 0;
			gid = make_kgid(current_user_ns(), option);
			if (!gid_valid(gid)) {
				ext3_msg(sb, KERN_ERR, "Invalid gid value %d", option);
				return 0;
			}
			sbi->s_resgid = gid;
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
			ext3_msg(sb, KERN_WARNING,
				"Ignoring deprecated oldalloc option");
			break;
		case Opt_orlov:
			ext3_msg(sb, KERN_WARNING,
				"Ignoring deprecated orlov option");
			break;
#ifdef CONFIG_EXT3_FS_XATTR
		case Opt_user_xattr:
			set_opt (sbi->s_mount_opt, XATTR_USER);
			break;
		case Opt_nouser_xattr:
			clear_opt (sbi->s_mount_opt, XATTR_USER);
			break;
#else
		case Opt_user_xattr:
		case Opt_nouser_xattr:
			ext3_msg(sb, KERN_INFO,
				"(no)user_xattr options not supported");
			break;
#endif
#ifdef CONFIG_EXT3_FS_POSIX_ACL
		case Opt_acl:
			set_opt(sbi->s_mount_opt, POSIX_ACL);
			break;
		case Opt_noacl:
			clear_opt(sbi->s_mount_opt, POSIX_ACL);
			break;
#else
		case Opt_acl:
		case Opt_noacl:
			ext3_msg(sb, KERN_INFO,
				"(no)acl options not supported");
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
				ext3_msg(sb, KERN_ERR, "error: cannot specify "
					"journal on remount");
				return 0;
			}
			set_opt (sbi->s_mount_opt, UPDATE_JOURNAL);
			break;
		case Opt_journal_inum:
			if (is_remount) {
				ext3_msg(sb, KERN_ERR, "error: cannot specify "
				       "journal on remount");
				return 0;
			}
			if (match_int(&args[0], &option))
				return 0;
			*inum = option;
			break;
		case Opt_journal_dev:
			if (is_remount) {
				ext3_msg(sb, KERN_ERR, "error: cannot specify "
				       "journal on remount");
				return 0;
			}
			if (match_int(&args[0], &option))
				return 0;
			*journal_devnum = option;
			break;
		case Opt_journal_path:
			if (is_remount) {
				ext3_msg(sb, KERN_ERR, "error: cannot specify "
				       "journal on remount");
				return 0;
			}

			journal_path = match_strdup(&args[0]);
			if (!journal_path) {
				ext3_msg(sb, KERN_ERR, "error: could not dup "
					"journal device string");
				return 0;
			}

			error = kern_path(journal_path, LOOKUP_FOLLOW, &path);
			if (error) {
				ext3_msg(sb, KERN_ERR, "error: could not find "
					"journal device path: error %d", error);
				kfree(journal_path);
				return 0;
			}

			journal_inode = path.dentry->d_inode;
			if (!S_ISBLK(journal_inode->i_mode)) {
				ext3_msg(sb, KERN_ERR, "error: journal path %s "
					"is not a block device", journal_path);
				path_put(&path);
				kfree(journal_path);
				return 0;
			}

			*journal_devnum = new_encode_dev(journal_inode->i_rdev);
			path_put(&path);
			kfree(journal_path);
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
			data_opt = EXT3_MOUNT_JOURNAL_DATA;
			goto datacheck;
		case Opt_data_ordered:
			data_opt = EXT3_MOUNT_ORDERED_DATA;
			goto datacheck;
		case Opt_data_writeback:
			data_opt = EXT3_MOUNT_WRITEBACK_DATA;
		datacheck:
			if (is_remount) {
				if (test_opt(sb, DATA_FLAGS) == data_opt)
					break;
				ext3_msg(sb, KERN_ERR,
					"error: cannot change "
					"data mode on remount. The filesystem "
					"is mounted in data=%s mode and you "
					"try to remount it in data=%s mode.",
					data_mode_string(test_opt(sb,
							DATA_FLAGS)),
					data_mode_string(data_opt));
				return 0;
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
				ext3_msg(sb, KERN_ERR, "error: cannot change "
					"journaled quota options when "
					"quota turned on.");
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
				ext3_msg(sb, KERN_ERR, "error: cannot change "
					"quota options when quota turned on.");
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
			ext3_msg(sb, KERN_ERR,
				"error: quota options not supported.");
			break;
		case Opt_usrjquota:
		case Opt_grpjquota:
		case Opt_offusrjquota:
		case Opt_offgrpjquota:
		case Opt_jqfmt_vfsold:
		case Opt_jqfmt_vfsv0:
		case Opt_jqfmt_vfsv1:
			ext3_msg(sb, KERN_ERR,
				"error: journaled quota options not "
				"supported.");
			break;
		case Opt_noquota:
			break;
#endif
		case Opt_abort:
			set_opt(sbi->s_mount_opt, ABORT);
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
				ext3_msg(sb, KERN_ERR,
					"error: resize option only available "
					"for remount");
				return 0;
			}
			if (match_int(&args[0], &option) != 0)
				return 0;
			*n_blocks_count = option;
			break;
		case Opt_nobh:
			ext3_msg(sb, KERN_WARNING,
				"warning: ignoring deprecated nobh option");
			break;
		case Opt_bh:
			ext3_msg(sb, KERN_WARNING,
				"warning: ignoring deprecated bh option");
			break;
		default:
			ext3_msg(sb, KERN_ERR,
				"error: unrecognized mount option \"%s\" "
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
			ext3_msg(sb, KERN_ERR, "error: old and new quota "
					"format mixing.");
			return 0;
		}

		if (!sbi->s_jquota_fmt) {
			ext3_msg(sb, KERN_ERR, "error: journaled quota format "
					"not specified.");
			return 0;
		}
	} else {
		if (sbi->s_jquota_fmt) {
			ext3_msg(sb, KERN_ERR, "error: journaled quota format "
					"specified with no journaling "
					"enabled.");
			return 0;
		}
	}
#endif
	return 1;
}

static int ext3_setup_super(struct super_block *sb, struct ext3_super_block *es,
			    int read_only)
{
	struct ext3_sb_info *sbi = EXT3_SB(sb);
	int res = 0;

	if (le32_to_cpu(es->s_rev_level) > EXT3_MAX_SUPP_REV) {
		ext3_msg(sb, KERN_ERR,
			"error: revision level too high, "
			"forcing read-only mode");
		res = MS_RDONLY;
	}
	if (read_only)
		return res;
	if (!(sbi->s_mount_state & EXT3_VALID_FS))
		ext3_msg(sb, KERN_WARNING,
			"warning: mounting unchecked fs, "
			"running e2fsck is recommended");
	else if ((sbi->s_mount_state & EXT3_ERROR_FS))
		ext3_msg(sb, KERN_WARNING,
			"warning: mounting fs with errors, "
			"running e2fsck is recommended");
	else if ((__s16) le16_to_cpu(es->s_max_mnt_count) > 0 &&
		 le16_to_cpu(es->s_mnt_count) >=
			le16_to_cpu(es->s_max_mnt_count))
		ext3_msg(sb, KERN_WARNING,
			"warning: maximal mount count reached, "
			"running e2fsck is recommended");
	else if (le32_to_cpu(es->s_checkinterval) &&
		(le32_to_cpu(es->s_lastcheck) +
			le32_to_cpu(es->s_checkinterval) <= get_seconds()))
		ext3_msg(sb, KERN_WARNING,
			"warning: checktime reached, "
			"running e2fsck is recommended");
#if 0
		/* @@@ We _will_ want to clear the valid bit if we find
                   inconsistencies, to force a fsck at reboot.  But for
                   a plain journaled filesystem we can keep it set as
                   valid forever! :) */
	es->s_state &= cpu_to_le16(~EXT3_VALID_FS);
#endif
	if (!le16_to_cpu(es->s_max_mnt_count))
		es->s_max_mnt_count = cpu_to_le16(EXT3_DFL_MAX_MNT_COUNT);
	le16_add_cpu(&es->s_mnt_count, 1);
	es->s_mtime = cpu_to_le32(get_seconds());
	ext3_update_dynamic_rev(sb);
	EXT3_SET_INCOMPAT_FEATURE(sb, EXT3_FEATURE_INCOMPAT_RECOVER);

	ext3_commit_super(sb, es, 1);
	if (test_opt(sb, DEBUG))
		ext3_msg(sb, KERN_INFO, "[bs=%lu, gc=%lu, "
				"bpg=%lu, ipg=%lu, mo=%04lx]",
			sb->s_blocksize,
			sbi->s_groups_count,
			EXT3_BLOCKS_PER_GROUP(sb),
			EXT3_INODES_PER_GROUP(sb),
			sbi->s_mount_opt);

	if (EXT3_SB(sb)->s_journal->j_inode == NULL) {
		char b[BDEVNAME_SIZE];
		ext3_msg(sb, KERN_INFO, "using external journal on %s",
			bdevname(EXT3_SB(sb)->s_journal->j_dev, b));
	} else {
		ext3_msg(sb, KERN_INFO, "using internal journal");
	}
	cleancache_init_fs(sb);
	return res;
}

/* Called at mount-time, super-block is locked */
static int ext3_check_descriptors(struct super_block *sb)
{
	struct ext3_sb_info *sbi = EXT3_SB(sb);
	int i;

	ext3_debug ("Checking group descriptors");

	for (i = 0; i < sbi->s_groups_count; i++) {
		struct ext3_group_desc *gdp = ext3_get_group_desc(sb, i, NULL);
		ext3_fsblk_t first_block = ext3_group_first_block_no(sb, i);
		ext3_fsblk_t last_block;

		if (i == sbi->s_groups_count - 1)
			last_block = le32_to_cpu(sbi->s_es->s_blocks_count) - 1;
		else
			last_block = first_block +
				(EXT3_BLOCKS_PER_GROUP(sb) - 1);

		if (le32_to_cpu(gdp->bg_block_bitmap) < first_block ||
		    le32_to_cpu(gdp->bg_block_bitmap) > last_block)
		{
			ext3_error (sb, "ext3_check_descriptors",
				    "Block bitmap for group %d"
				    " not in group (block %lu)!",
				    i, (unsigned long)
					le32_to_cpu(gdp->bg_block_bitmap));
			return 0;
		}
		if (le32_to_cpu(gdp->bg_inode_bitmap) < first_block ||
		    le32_to_cpu(gdp->bg_inode_bitmap) > last_block)
		{
			ext3_error (sb, "ext3_check_descriptors",
				    "Inode bitmap for group %d"
				    " not in group (block %lu)!",
				    i, (unsigned long)
					le32_to_cpu(gdp->bg_inode_bitmap));
			return 0;
		}
		if (le32_to_cpu(gdp->bg_inode_table) < first_block ||
		    le32_to_cpu(gdp->bg_inode_table) + sbi->s_itb_per_group - 1 >
		    last_block)
		{
			ext3_error (sb, "ext3_check_descriptors",
				    "Inode table for group %d"
				    " not in group (block %lu)!",
				    i, (unsigned long)
					le32_to_cpu(gdp->bg_inode_table));
			return 0;
		}
	}

	sbi->s_es->s_free_blocks_count=cpu_to_le32(ext3_count_free_blocks(sb));
	sbi->s_es->s_free_inodes_count=cpu_to_le32(ext3_count_free_inodes(sb));
	return 1;
}


/* ext3_orphan_cleanup() walks a singly-linked list of inodes (starting at
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
 * ext3_free_inode().  The only reason we would point at a wrong inode is if
 * e2fsck was run on this filesystem, and it must have already done the orphan
 * inode cleanup for us, so we can safely abort without any further action.
 */
static void ext3_orphan_cleanup (struct super_block * sb,
				 struct ext3_super_block * es)
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
		ext3_msg(sb, KERN_ERR, "error: write access "
			"unavailable, skipping orphan cleanup.");
		return;
	}

	/* Check if feature set allows readwrite operations */
	if (EXT3_HAS_RO_COMPAT_FEATURE(sb, ~EXT3_FEATURE_RO_COMPAT_SUPP)) {
		ext3_msg(sb, KERN_INFO, "Skipping orphan cleanup due to "
			 "unknown ROCOMPAT features");
		return;
	}

	if (EXT3_SB(sb)->s_mount_state & EXT3_ERROR_FS) {
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
		ext3_msg(sb, KERN_INFO, "orphan cleanup on readonly fs");
		sb->s_flags &= ~MS_RDONLY;
	}
#ifdef CONFIG_QUOTA
	/* Needed for iput() to work correctly and not trash data */
	sb->s_flags |= MS_ACTIVE;
	/* Turn on quotas so that they are updated correctly */
	for (i = 0; i < MAXQUOTAS; i++) {
		if (EXT3_SB(sb)->s_qf_names[i]) {
			int ret = ext3_quota_on_mount(sb, i);
			if (ret < 0)
				ext3_msg(sb, KERN_ERR,
					"error: cannot turn on journaled "
					"quota: %d", ret);
		}
	}
#endif

	while (es->s_last_orphan) {
		struct inode *inode;

		inode = ext3_orphan_get(sb, le32_to_cpu(es->s_last_orphan));
		if (IS_ERR(inode)) {
			es->s_last_orphan = 0;
			break;
		}

		list_add(&EXT3_I(inode)->i_orphan, &EXT3_SB(sb)->s_orphan);
		dquot_initialize(inode);
		if (inode->i_nlink) {
			printk(KERN_DEBUG
				"%s: truncating inode %lu to %Ld bytes\n",
				__func__, inode->i_ino, inode->i_size);
			jbd_debug(2, "truncating inode %lu to %Ld bytes\n",
				  inode->i_ino, inode->i_size);
			ext3_truncate(inode);
			nr_truncates++;
		} else {
			printk(KERN_DEBUG
				"%s: deleting unreferenced inode %lu\n",
				__func__, inode->i_ino);
			jbd_debug(2, "deleting unreferenced inode %lu\n",
				  inode->i_ino);
			nr_orphans++;
		}
		iput(inode);  /* The delete magic happens here! */
	}

#define PLURAL(x) (x), ((x)==1) ? "" : "s"

	if (nr_orphans)
		ext3_msg(sb, KERN_INFO, "%d orphan inode%s deleted",
		       PLURAL(nr_orphans));
	if (nr_truncates)
		ext3_msg(sb, KERN_INFO, "%d truncate%s cleaned up",
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
 * Maximal file size.  There is a direct, and {,double-,triple-}indirect
 * block limit, and also a limit of (2^32 - 1) 512-byte sectors in i_blocks.
 * We need to be 1 filesystem block less than the 2^32 sector limit.
 */
static loff_t ext3_max_size(int bits)
{
	loff_t res = EXT3_NDIR_BLOCKS;
	int meta_blocks;
	loff_t upper_limit;

	/* This is calculated to be the largest file size for a
	 * dense, file such that the total number of
	 * sectors in the file, including data and all indirect blocks,
	 * does not exceed 2^32 -1
	 * __u32 i_blocks representing the total number of
	 * 512 bytes blocks of the file
	 */
	upper_limit = (1LL << 32) - 1;

	/* total blocks in file system block size */
	upper_limit >>= (bits - 9);


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

static ext3_fsblk_t descriptor_loc(struct super_block *sb,
				    ext3_fsblk_t logic_sb_block,
				    int nr)
{
	struct ext3_sb_info *sbi = EXT3_SB(sb);
	unsigned long bg, first_meta_bg;
	int has_super = 0;

	first_meta_bg = le32_to_cpu(sbi->s_es->s_first_meta_bg);

	if (!EXT3_HAS_INCOMPAT_FEATURE(sb, EXT3_FEATURE_INCOMPAT_META_BG) ||
	    nr < first_meta_bg)
		return (logic_sb_block + nr + 1);
	bg = sbi->s_desc_per_block * nr;
	if (ext3_bg_has_super(sb, bg))
		has_super = 1;
	return (has_super + ext3_group_first_block_no(sb, bg));
}


static int ext3_fill_super (struct super_block *sb, void *data, int silent)
{
	struct buffer_head * bh;
	struct ext3_super_block *es = NULL;
	struct ext3_sb_info *sbi;
	ext3_fsblk_t block;
	ext3_fsblk_t sb_block = get_sb_block(&data, sb);
	ext3_fsblk_t logic_sb_block;
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
	int ret = -EINVAL;
	__le32 features;
	int err;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	sbi->s_blockgroup_lock =
		kzalloc(sizeof(struct blockgroup_lock), GFP_KERNEL);
	if (!sbi->s_blockgroup_lock) {
		kfree(sbi);
		return -ENOMEM;
	}
	sb->s_fs_info = sbi;
	sbi->s_sb_block = sb_block;

	blocksize = sb_min_blocksize(sb, EXT3_MIN_BLOCK_SIZE);
	if (!blocksize) {
		ext3_msg(sb, KERN_ERR, "error: unable to set blocksize");
		goto out_fail;
	}

	/*
	 * The ext3 superblock will not be buffer aligned for other than 1kB
	 * block sizes.  We need to calculate the offset from buffer start.
	 */
	if (blocksize != EXT3_MIN_BLOCK_SIZE) {
		logic_sb_block = (sb_block * EXT3_MIN_BLOCK_SIZE) / blocksize;
		offset = (sb_block * EXT3_MIN_BLOCK_SIZE) % blocksize;
	} else {
		logic_sb_block = sb_block;
	}

	if (!(bh = sb_bread(sb, logic_sb_block))) {
		ext3_msg(sb, KERN_ERR, "error: unable to read superblock");
		goto out_fail;
	}
	/*
	 * Note: s_es must be initialized as soon as possible because
	 *       some ext3 macro-instructions depend on its value
	 */
	es = (struct ext3_super_block *) (bh->b_data + offset);
	sbi->s_es = es;
	sb->s_magic = le16_to_cpu(es->s_magic);
	if (sb->s_magic != EXT3_SUPER_MAGIC)
		goto cantfind_ext3;

	/* Set defaults before we parse the mount options */
	def_mount_opts = le32_to_cpu(es->s_default_mount_opts);
	if (def_mount_opts & EXT3_DEFM_DEBUG)
		set_opt(sbi->s_mount_opt, DEBUG);
	if (def_mount_opts & EXT3_DEFM_BSDGROUPS)
		set_opt(sbi->s_mount_opt, GRPID);
	if (def_mount_opts & EXT3_DEFM_UID16)
		set_opt(sbi->s_mount_opt, NO_UID32);
#ifdef CONFIG_EXT3_FS_XATTR
	if (def_mount_opts & EXT3_DEFM_XATTR_USER)
		set_opt(sbi->s_mount_opt, XATTR_USER);
#endif
#ifdef CONFIG_EXT3_FS_POSIX_ACL
	if (def_mount_opts & EXT3_DEFM_ACL)
		set_opt(sbi->s_mount_opt, POSIX_ACL);
#endif
	if ((def_mount_opts & EXT3_DEFM_JMODE) == EXT3_DEFM_JMODE_DATA)
		set_opt(sbi->s_mount_opt, JOURNAL_DATA);
	else if ((def_mount_opts & EXT3_DEFM_JMODE) == EXT3_DEFM_JMODE_ORDERED)
		set_opt(sbi->s_mount_opt, ORDERED_DATA);
	else if ((def_mount_opts & EXT3_DEFM_JMODE) == EXT3_DEFM_JMODE_WBACK)
		set_opt(sbi->s_mount_opt, WRITEBACK_DATA);

	if (le16_to_cpu(sbi->s_es->s_errors) == EXT3_ERRORS_PANIC)
		set_opt(sbi->s_mount_opt, ERRORS_PANIC);
	else if (le16_to_cpu(sbi->s_es->s_errors) == EXT3_ERRORS_CONTINUE)
		set_opt(sbi->s_mount_opt, ERRORS_CONT);
	else
		set_opt(sbi->s_mount_opt, ERRORS_RO);

	sbi->s_resuid = make_kuid(&init_user_ns, le16_to_cpu(es->s_def_resuid));
	sbi->s_resgid = make_kgid(&init_user_ns, le16_to_cpu(es->s_def_resgid));

	/* enable barriers by default */
	set_opt(sbi->s_mount_opt, BARRIER);
	set_opt(sbi->s_mount_opt, RESERVATION);

	if (!parse_options ((char *) data, sb, &journal_inum, &journal_devnum,
			    NULL, 0))
		goto failed_mount;

	sb->s_flags = (sb->s_flags & ~MS_POSIXACL) |
		(test_opt(sb, POSIX_ACL) ? MS_POSIXACL : 0);

	if (le32_to_cpu(es->s_rev_level) == EXT3_GOOD_OLD_REV &&
	    (EXT3_HAS_COMPAT_FEATURE(sb, ~0U) ||
	     EXT3_HAS_RO_COMPAT_FEATURE(sb, ~0U) ||
	     EXT3_HAS_INCOMPAT_FEATURE(sb, ~0U)))
		ext3_msg(sb, KERN_WARNING,
			"warning: feature flags set on rev 0 fs, "
			"running e2fsck is recommended");
	/*
	 * Check feature flags regardless of the revision level, since we
	 * previously didn't change the revision level when setting the flags,
	 * so there is a chance incompat flags are set on a rev 0 filesystem.
	 */
	features = EXT3_HAS_INCOMPAT_FEATURE(sb, ~EXT3_FEATURE_INCOMPAT_SUPP);
	if (features) {
		ext3_msg(sb, KERN_ERR,
			"error: couldn't mount because of unsupported "
			"optional features (%x)", le32_to_cpu(features));
		goto failed_mount;
	}
	features = EXT3_HAS_RO_COMPAT_FEATURE(sb, ~EXT3_FEATURE_RO_COMPAT_SUPP);
	if (!(sb->s_flags & MS_RDONLY) && features) {
		ext3_msg(sb, KERN_ERR,
			"error: couldn't mount RDWR because of unsupported "
			"optional features (%x)", le32_to_cpu(features));
		goto failed_mount;
	}
	blocksize = BLOCK_SIZE << le32_to_cpu(es->s_log_block_size);

	if (blocksize < EXT3_MIN_BLOCK_SIZE ||
	    blocksize > EXT3_MAX_BLOCK_SIZE) {
		ext3_msg(sb, KERN_ERR,
			"error: couldn't mount because of unsupported "
			"filesystem blocksize %d", blocksize);
		goto failed_mount;
	}

	hblock = bdev_logical_block_size(sb->s_bdev);
	if (sb->s_blocksize != blocksize) {
		/*
		 * Make sure the blocksize for the filesystem is larger
		 * than the hardware sectorsize for the machine.
		 */
		if (blocksize < hblock) {
			ext3_msg(sb, KERN_ERR,
				"error: fsblocksize %d too small for "
				"hardware sectorsize %d", blocksize, hblock);
			goto failed_mount;
		}

		brelse (bh);
		if (!sb_set_blocksize(sb, blocksize)) {
			ext3_msg(sb, KERN_ERR,
				"error: bad blocksize %d", blocksize);
			goto out_fail;
		}
		logic_sb_block = (sb_block * EXT3_MIN_BLOCK_SIZE) / blocksize;
		offset = (sb_block * EXT3_MIN_BLOCK_SIZE) % blocksize;
		bh = sb_bread(sb, logic_sb_block);
		if (!bh) {
			ext3_msg(sb, KERN_ERR,
			       "error: can't read superblock on 2nd try");
			goto failed_mount;
		}
		es = (struct ext3_super_block *)(bh->b_data + offset);
		sbi->s_es = es;
		if (es->s_magic != cpu_to_le16(EXT3_SUPER_MAGIC)) {
			ext3_msg(sb, KERN_ERR,
				"error: magic mismatch");
			goto failed_mount;
		}
	}

	sb->s_maxbytes = ext3_max_size(sb->s_blocksize_bits);

	if (le32_to_cpu(es->s_rev_level) == EXT3_GOOD_OLD_REV) {
		sbi->s_inode_size = EXT3_GOOD_OLD_INODE_SIZE;
		sbi->s_first_ino = EXT3_GOOD_OLD_FIRST_INO;
	} else {
		sbi->s_inode_size = le16_to_cpu(es->s_inode_size);
		sbi->s_first_ino = le32_to_cpu(es->s_first_ino);
		if ((sbi->s_inode_size < EXT3_GOOD_OLD_INODE_SIZE) ||
		    (!is_power_of_2(sbi->s_inode_size)) ||
		    (sbi->s_inode_size > blocksize)) {
			ext3_msg(sb, KERN_ERR,
				"error: unsupported inode size: %d",
				sbi->s_inode_size);
			goto failed_mount;
		}
	}
	sbi->s_frag_size = EXT3_MIN_FRAG_SIZE <<
				   le32_to_cpu(es->s_log_frag_size);
	if (blocksize != sbi->s_frag_size) {
		ext3_msg(sb, KERN_ERR,
		       "error: fragsize %lu != blocksize %u (unsupported)",
		       sbi->s_frag_size, blocksize);
		goto failed_mount;
	}
	sbi->s_frags_per_block = 1;
	sbi->s_blocks_per_group = le32_to_cpu(es->s_blocks_per_group);
	sbi->s_frags_per_group = le32_to_cpu(es->s_frags_per_group);
	sbi->s_inodes_per_group = le32_to_cpu(es->s_inodes_per_group);
	if (EXT3_INODE_SIZE(sb) == 0 || EXT3_INODES_PER_GROUP(sb) == 0)
		goto cantfind_ext3;
	sbi->s_inodes_per_block = blocksize / EXT3_INODE_SIZE(sb);
	if (sbi->s_inodes_per_block == 0)
		goto cantfind_ext3;
	sbi->s_itb_per_group = sbi->s_inodes_per_group /
					sbi->s_inodes_per_block;
	sbi->s_desc_per_block = blocksize / sizeof(struct ext3_group_desc);
	sbi->s_sbh = bh;
	sbi->s_mount_state = le16_to_cpu(es->s_state);
	sbi->s_addr_per_block_bits = ilog2(EXT3_ADDR_PER_BLOCK(sb));
	sbi->s_desc_per_block_bits = ilog2(EXT3_DESC_PER_BLOCK(sb));
	for (i=0; i < 4; i++)
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
	}

	if (sbi->s_blocks_per_group > blocksize * 8) {
		ext3_msg(sb, KERN_ERR,
			"#blocks per group too big: %lu",
			sbi->s_blocks_per_group);
		goto failed_mount;
	}
	if (sbi->s_frags_per_group > blocksize * 8) {
		ext3_msg(sb, KERN_ERR,
			"error: #fragments per group too big: %lu",
			sbi->s_frags_per_group);
		goto failed_mount;
	}
	if (sbi->s_inodes_per_group > blocksize * 8) {
		ext3_msg(sb, KERN_ERR,
			"error: #inodes per group too big: %lu",
			sbi->s_inodes_per_group);
		goto failed_mount;
	}

	err = generic_check_addressable(sb->s_blocksize_bits,
					le32_to_cpu(es->s_blocks_count));
	if (err) {
		ext3_msg(sb, KERN_ERR,
			"error: filesystem is too large to mount safely");
		if (sizeof(sector_t) < 8)
			ext3_msg(sb, KERN_ERR,
				"error: CONFIG_LBDAF not enabled");
		ret = err;
		goto failed_mount;
	}

	if (EXT3_BLOCKS_PER_GROUP(sb) == 0)
		goto cantfind_ext3;
	sbi->s_groups_count = ((le32_to_cpu(es->s_blocks_count) -
			       le32_to_cpu(es->s_first_data_block) - 1)
				       / EXT3_BLOCKS_PER_GROUP(sb)) + 1;
	db_count = DIV_ROUND_UP(sbi->s_groups_count, EXT3_DESC_PER_BLOCK(sb));
	sbi->s_group_desc = kmalloc(db_count * sizeof (struct buffer_head *),
				    GFP_KERNEL);
	if (sbi->s_group_desc == NULL) {
		ext3_msg(sb, KERN_ERR,
			"error: not enough memory");
		ret = -ENOMEM;
		goto failed_mount;
	}

	bgl_lock_init(sbi->s_blockgroup_lock);

	for (i = 0; i < db_count; i++) {
		block = descriptor_loc(sb, logic_sb_block, i);
		sbi->s_group_desc[i] = sb_bread(sb, block);
		if (!sbi->s_group_desc[i]) {
			ext3_msg(sb, KERN_ERR,
				"error: can't read group descriptor %d", i);
			db_count = i;
			goto failed_mount2;
		}
	}
	if (!ext3_check_descriptors (sb)) {
		ext3_msg(sb, KERN_ERR,
			"error: group descriptors corrupted");
		goto failed_mount2;
	}
	sbi->s_gdb_count = db_count;
	get_random_bytes(&sbi->s_next_generation, sizeof(u32));
	spin_lock_init(&sbi->s_next_gen_lock);

	/* per fileystem reservation list head & lock */
	spin_lock_init(&sbi->s_rsv_window_lock);
	sbi->s_rsv_window_root = RB_ROOT;
	/* Add a single, static dummy reservation to the start of the
	 * reservation window list --- it gives us a placeholder for
	 * append-at-start-of-list which makes the allocation logic
	 * _much_ simpler. */
	sbi->s_rsv_window_head.rsv_start = EXT3_RESERVE_WINDOW_NOT_ALLOCATED;
	sbi->s_rsv_window_head.rsv_end = EXT3_RESERVE_WINDOW_NOT_ALLOCATED;
	sbi->s_rsv_window_head.rsv_alloc_hit = 0;
	sbi->s_rsv_window_head.rsv_goal_size = 0;
	ext3_rsv_window_add(sb, &sbi->s_rsv_window_head);

	/*
	 * set up enough so that it can read an inode
	 */
	sb->s_op = &ext3_sops;
	sb->s_export_op = &ext3_export_ops;
	sb->s_xattr = ext3_xattr_handlers;
#ifdef CONFIG_QUOTA
	sb->s_qcop = &ext3_qctl_operations;
	sb->dq_op = &ext3_quota_operations;
#endif
	memcpy(sb->s_uuid, es->s_uuid, sizeof(es->s_uuid));
	INIT_LIST_HEAD(&sbi->s_orphan); /* unlinked but open files */
	mutex_init(&sbi->s_orphan_lock);
	mutex_init(&sbi->s_resize_lock);

	sb->s_root = NULL;

	needs_recovery = (es->s_last_orphan != 0 ||
			  EXT3_HAS_INCOMPAT_FEATURE(sb,
				    EXT3_FEATURE_INCOMPAT_RECOVER));

	/*
	 * The first inode we look at is the journal inode.  Don't try
	 * root first: it may be modified in the journal!
	 */
	if (!test_opt(sb, NOLOAD) &&
	    EXT3_HAS_COMPAT_FEATURE(sb, EXT3_FEATURE_COMPAT_HAS_JOURNAL)) {
		if (ext3_load_journal(sb, es, journal_devnum))
			goto failed_mount2;
	} else if (journal_inum) {
		if (ext3_create_journal(sb, es, journal_inum))
			goto failed_mount2;
	} else {
		if (!silent)
			ext3_msg(sb, KERN_ERR,
				"error: no journal found. "
				"mounting ext3 over ext2?");
		goto failed_mount2;
	}
	err = percpu_counter_init(&sbi->s_freeblocks_counter,
			ext3_count_free_blocks(sb));
	if (!err) {
		err = percpu_counter_init(&sbi->s_freeinodes_counter,
				ext3_count_free_inodes(sb));
	}
	if (!err) {
		err = percpu_counter_init(&sbi->s_dirs_counter,
				ext3_count_dirs(sb));
	}
	if (err) {
		ext3_msg(sb, KERN_ERR, "error: insufficient memory");
		ret = err;
		goto failed_mount3;
	}

	/* We have now updated the journal if required, so we can
	 * validate the data journaling mode. */
	switch (test_opt(sb, DATA_FLAGS)) {
	case 0:
		/* No mode set, assume a default based on the journal
                   capabilities: ORDERED_DATA if the journal can
                   cope, else JOURNAL_DATA */
		if (journal_check_available_features
		    (sbi->s_journal, 0, 0, JFS_FEATURE_INCOMPAT_REVOKE))
			set_opt(sbi->s_mount_opt, DEFAULT_DATA_MODE);
		else
			set_opt(sbi->s_mount_opt, JOURNAL_DATA);
		break;

	case EXT3_MOUNT_ORDERED_DATA:
	case EXT3_MOUNT_WRITEBACK_DATA:
		if (!journal_check_available_features
		    (sbi->s_journal, 0, 0, JFS_FEATURE_INCOMPAT_REVOKE)) {
			ext3_msg(sb, KERN_ERR,
				"error: journal does not support "
				"requested data journaling mode");
			goto failed_mount3;
		}
	default:
		break;
	}

	/*
	 * The journal_load will have done any necessary log recovery,
	 * so we can safely mount the rest of the filesystem now.
	 */

	root = ext3_iget(sb, EXT3_ROOT_INO);
	if (IS_ERR(root)) {
		ext3_msg(sb, KERN_ERR, "error: get root inode failed");
		ret = PTR_ERR(root);
		goto failed_mount3;
	}
	if (!S_ISDIR(root->i_mode) || !root->i_blocks || !root->i_size) {
		iput(root);
		ext3_msg(sb, KERN_ERR, "error: corrupt root inode, run e2fsck");
		goto failed_mount3;
	}
	sb->s_root = d_make_root(root);
	if (!sb->s_root) {
		ext3_msg(sb, KERN_ERR, "error: get root dentry failed");
		ret = -ENOMEM;
		goto failed_mount3;
	}

	if (ext3_setup_super(sb, es, sb->s_flags & MS_RDONLY))
		sb->s_flags |= MS_RDONLY;

	EXT3_SB(sb)->s_mount_state |= EXT3_ORPHAN_FS;
	ext3_orphan_cleanup(sb, es);
	EXT3_SB(sb)->s_mount_state &= ~EXT3_ORPHAN_FS;
	if (needs_recovery) {
		ext3_mark_recovery_complete(sb, es);
		ext3_msg(sb, KERN_INFO, "recovery complete");
	}
	ext3_msg(sb, KERN_INFO, "mounted filesystem with %s data mode",
		test_opt(sb,DATA_FLAGS) == EXT3_MOUNT_JOURNAL_DATA ? "journal":
		test_opt(sb,DATA_FLAGS) == EXT3_MOUNT_ORDERED_DATA ? "ordered":
		"writeback");

	return 0;

cantfind_ext3:
	if (!silent)
		ext3_msg(sb, KERN_INFO,
			"error: can't find ext3 filesystem on dev %s.",
		       sb->s_id);
	goto failed_mount;

failed_mount3:
	percpu_counter_destroy(&sbi->s_freeblocks_counter);
	percpu_counter_destroy(&sbi->s_freeinodes_counter);
	percpu_counter_destroy(&sbi->s_dirs_counter);
	journal_destroy(sbi->s_journal);
failed_mount2:
	for (i = 0; i < db_count; i++)
		brelse(sbi->s_group_desc[i]);
	kfree(sbi->s_group_desc);
failed_mount:
#ifdef CONFIG_QUOTA
	for (i = 0; i < MAXQUOTAS; i++)
		kfree(sbi->s_qf_names[i]);
#endif
	ext3_blkdev_remove(sbi);
	brelse(bh);
out_fail:
	sb->s_fs_info = NULL;
	kfree(sbi->s_blockgroup_lock);
	kfree(sbi);
	return ret;
}

/*
 * Setup any per-fs journal parameters now.  We'll do this both on
 * initial mount, once the journal has been initialised but before we've
 * done any recovery; and again on any subsequent remount.
 */
static void ext3_init_journal_params(struct super_block *sb, journal_t *journal)
{
	struct ext3_sb_info *sbi = EXT3_SB(sb);

	if (sbi->s_commit_interval)
		journal->j_commit_interval = sbi->s_commit_interval;
	/* We could also set up an ext3-specific default for the commit
	 * interval here, but for now we'll just fall back to the jbd
	 * default. */

	spin_lock(&journal->j_state_lock);
	if (test_opt(sb, BARRIER))
		journal->j_flags |= JFS_BARRIER;
	else
		journal->j_flags &= ~JFS_BARRIER;
	if (test_opt(sb, DATA_ERR_ABORT))
		journal->j_flags |= JFS_ABORT_ON_SYNCDATA_ERR;
	else
		journal->j_flags &= ~JFS_ABORT_ON_SYNCDATA_ERR;
	spin_unlock(&journal->j_state_lock);
}

static journal_t *ext3_get_journal(struct super_block *sb,
				   unsigned int journal_inum)
{
	struct inode *journal_inode;
	journal_t *journal;

	/* First, test for the existence of a valid inode on disk.  Bad
	 * things happen if we iget() an unused inode, as the subsequent
	 * iput() will try to delete it. */

	journal_inode = ext3_iget(sb, journal_inum);
	if (IS_ERR(journal_inode)) {
		ext3_msg(sb, KERN_ERR, "error: no journal found");
		return NULL;
	}
	if (!journal_inode->i_nlink) {
		make_bad_inode(journal_inode);
		iput(journal_inode);
		ext3_msg(sb, KERN_ERR, "error: journal inode is deleted");
		return NULL;
	}

	jbd_debug(2, "Journal inode found at %p: %Ld bytes\n",
		  journal_inode, journal_inode->i_size);
	if (!S_ISREG(journal_inode->i_mode)) {
		ext3_msg(sb, KERN_ERR, "error: invalid journal inode");
		iput(journal_inode);
		return NULL;
	}

	journal = journal_init_inode(journal_inode);
	if (!journal) {
		ext3_msg(sb, KERN_ERR, "error: could not load journal inode");
		iput(journal_inode);
		return NULL;
	}
	journal->j_private = sb;
	ext3_init_journal_params(sb, journal);
	return journal;
}

static journal_t *ext3_get_dev_journal(struct super_block *sb,
				       dev_t j_dev)
{
	struct buffer_head * bh;
	journal_t *journal;
	ext3_fsblk_t start;
	ext3_fsblk_t len;
	int hblock, blocksize;
	ext3_fsblk_t sb_block;
	unsigned long offset;
	struct ext3_super_block * es;
	struct block_device *bdev;

	bdev = ext3_blkdev_get(j_dev, sb);
	if (bdev == NULL)
		return NULL;

	blocksize = sb->s_blocksize;
	hblock = bdev_logical_block_size(bdev);
	if (blocksize < hblock) {
		ext3_msg(sb, KERN_ERR,
			"error: blocksize too small for journal device");
		goto out_bdev;
	}

	sb_block = EXT3_MIN_BLOCK_SIZE / blocksize;
	offset = EXT3_MIN_BLOCK_SIZE % blocksize;
	set_blocksize(bdev, blocksize);
	if (!(bh = __bread(bdev, sb_block, blocksize))) {
		ext3_msg(sb, KERN_ERR, "error: couldn't read superblock of "
			"external journal");
		goto out_bdev;
	}

	es = (struct ext3_super_block *) (bh->b_data + offset);
	if ((le16_to_cpu(es->s_magic) != EXT3_SUPER_MAGIC) ||
	    !(le32_to_cpu(es->s_feature_incompat) &
	      EXT3_FEATURE_INCOMPAT_JOURNAL_DEV)) {
		ext3_msg(sb, KERN_ERR, "error: external journal has "
			"bad superblock");
		brelse(bh);
		goto out_bdev;
	}

	if (memcmp(EXT3_SB(sb)->s_es->s_journal_uuid, es->s_uuid, 16)) {
		ext3_msg(sb, KERN_ERR, "error: journal UUID does not match");
		brelse(bh);
		goto out_bdev;
	}

	len = le32_to_cpu(es->s_blocks_count);
	start = sb_block + 1;
	brelse(bh);	/* we're done with the superblock */

	journal = journal_init_dev(bdev, sb->s_bdev,
					start, len, blocksize);
	if (!journal) {
		ext3_msg(sb, KERN_ERR,
			"error: failed to create device journal");
		goto out_bdev;
	}
	journal->j_private = sb;
	if (!bh_uptodate_or_lock(journal->j_sb_buffer)) {
		if (bh_submit_read(journal->j_sb_buffer)) {
			ext3_msg(sb, KERN_ERR, "I/O error on journal device");
			goto out_journal;
		}
	}
	if (be32_to_cpu(journal->j_superblock->s_nr_users) != 1) {
		ext3_msg(sb, KERN_ERR,
			"error: external journal has more than one "
			"user (unsupported) - %d",
			be32_to_cpu(journal->j_superblock->s_nr_users));
		goto out_journal;
	}
	EXT3_SB(sb)->journal_bdev = bdev;
	ext3_init_journal_params(sb, journal);
	return journal;
out_journal:
	journal_destroy(journal);
out_bdev:
	ext3_blkdev_put(bdev);
	return NULL;
}

static int ext3_load_journal(struct super_block *sb,
			     struct ext3_super_block *es,
			     unsigned long journal_devnum)
{
	journal_t *journal;
	unsigned int journal_inum = le32_to_cpu(es->s_journal_inum);
	dev_t journal_dev;
	int err = 0;
	int really_read_only;

	if (journal_devnum &&
	    journal_devnum != le32_to_cpu(es->s_journal_dev)) {
		ext3_msg(sb, KERN_INFO, "external journal device major/minor "
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

	if (EXT3_HAS_INCOMPAT_FEATURE(sb, EXT3_FEATURE_INCOMPAT_RECOVER)) {
		if (sb->s_flags & MS_RDONLY) {
			ext3_msg(sb, KERN_INFO,
				"recovery required on readonly filesystem");
			if (really_read_only) {
				ext3_msg(sb, KERN_ERR, "error: write access "
					"unavailable, cannot proceed");
				return -EROFS;
			}
			ext3_msg(sb, KERN_INFO,
				"write access will be enabled during recovery");
		}
	}

	if (journal_inum && journal_dev) {
		ext3_msg(sb, KERN_ERR, "error: filesystem has both journal "
		       "and inode journals");
		return -EINVAL;
	}

	if (journal_inum) {
		if (!(journal = ext3_get_journal(sb, journal_inum)))
			return -EINVAL;
	} else {
		if (!(journal = ext3_get_dev_journal(sb, journal_dev)))
			return -EINVAL;
	}

	if (!(journal->j_flags & JFS_BARRIER))
		printk(KERN_INFO "EXT3-fs: barriers not enabled\n");

	if (!really_read_only && test_opt(sb, UPDATE_JOURNAL)) {
		err = journal_update_format(journal);
		if (err)  {
			ext3_msg(sb, KERN_ERR, "error updating journal");
			journal_destroy(journal);
			return err;
		}
	}

	if (!EXT3_HAS_INCOMPAT_FEATURE(sb, EXT3_FEATURE_INCOMPAT_RECOVER))
		err = journal_wipe(journal, !really_read_only);
	if (!err)
		err = journal_load(journal);

	if (err) {
		ext3_msg(sb, KERN_ERR, "error loading journal");
		journal_destroy(journal);
		return err;
	}

	EXT3_SB(sb)->s_journal = journal;
	ext3_clear_journal_err(sb, es);

	if (!really_read_only && journal_devnum &&
	    journal_devnum != le32_to_cpu(es->s_journal_dev)) {
		es->s_journal_dev = cpu_to_le32(journal_devnum);

		/* Make sure we flush the recovery flag to disk. */
		ext3_commit_super(sb, es, 1);
	}

	return 0;
}

static int ext3_create_journal(struct super_block *sb,
			       struct ext3_super_block *es,
			       unsigned int journal_inum)
{
	journal_t *journal;
	int err;

	if (sb->s_flags & MS_RDONLY) {
		ext3_msg(sb, KERN_ERR,
			"error: readonly filesystem when trying to "
			"create journal");
		return -EROFS;
	}

	journal = ext3_get_journal(sb, journal_inum);
	if (!journal)
		return -EINVAL;

	ext3_msg(sb, KERN_INFO, "creating new journal on inode %u",
	       journal_inum);

	err = journal_create(journal);
	if (err) {
		ext3_msg(sb, KERN_ERR, "error creating journal");
		journal_destroy(journal);
		return -EIO;
	}

	EXT3_SB(sb)->s_journal = journal;

	ext3_update_dynamic_rev(sb);
	EXT3_SET_INCOMPAT_FEATURE(sb, EXT3_FEATURE_INCOMPAT_RECOVER);
	EXT3_SET_COMPAT_FEATURE(sb, EXT3_FEATURE_COMPAT_HAS_JOURNAL);

	es->s_journal_inum = cpu_to_le32(journal_inum);

	/* Make sure we flush the recovery flag to disk. */
	ext3_commit_super(sb, es, 1);

	return 0;
}

static int ext3_commit_super(struct super_block *sb,
			       struct ext3_super_block *es,
			       int sync)
{
	struct buffer_head *sbh = EXT3_SB(sb)->s_sbh;
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
		ext3_msg(sb, KERN_ERR, "previous I/O error to "
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
	es->s_free_blocks_count = cpu_to_le32(ext3_count_free_blocks(sb));
	es->s_free_inodes_count = cpu_to_le32(ext3_count_free_inodes(sb));
	BUFFER_TRACE(sbh, "marking dirty");
	mark_buffer_dirty(sbh);
	if (sync) {
		error = sync_dirty_buffer(sbh);
		if (buffer_write_io_error(sbh)) {
			ext3_msg(sb, KERN_ERR, "I/O error while writing "
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
static void ext3_mark_recovery_complete(struct super_block * sb,
					struct ext3_super_block * es)
{
	journal_t *journal = EXT3_SB(sb)->s_journal;

	journal_lock_updates(journal);
	if (journal_flush(journal) < 0)
		goto out;

	if (EXT3_HAS_INCOMPAT_FEATURE(sb, EXT3_FEATURE_INCOMPAT_RECOVER) &&
	    sb->s_flags & MS_RDONLY) {
		EXT3_CLEAR_INCOMPAT_FEATURE(sb, EXT3_FEATURE_INCOMPAT_RECOVER);
		ext3_commit_super(sb, es, 1);
	}

out:
	journal_unlock_updates(journal);
}

/*
 * If we are mounting (or read-write remounting) a filesystem whose journal
 * has recorded an error from a previous lifetime, move that error to the
 * main filesystem now.
 */
static void ext3_clear_journal_err(struct super_block *sb,
				   struct ext3_super_block *es)
{
	journal_t *journal;
	int j_errno;
	const char *errstr;

	journal = EXT3_SB(sb)->s_journal;

	/*
	 * Now check for any error status which may have been recorded in the
	 * journal by a prior ext3_error() or ext3_abort()
	 */

	j_errno = journal_errno(journal);
	if (j_errno) {
		char nbuf[16];

		errstr = ext3_decode_error(sb, j_errno, nbuf);
		ext3_warning(sb, __func__, "Filesystem error recorded "
			     "from previous mount: %s", errstr);
		ext3_warning(sb, __func__, "Marking fs in need of "
			     "filesystem check.");

		EXT3_SB(sb)->s_mount_state |= EXT3_ERROR_FS;
		es->s_state |= cpu_to_le16(EXT3_ERROR_FS);
		ext3_commit_super (sb, es, 1);

		journal_clear_err(journal);
	}
}

/*
 * Force the running and committing transactions to commit,
 * and wait on the commit.
 */
int ext3_force_commit(struct super_block *sb)
{
	journal_t *journal;
	int ret;

	if (sb->s_flags & MS_RDONLY)
		return 0;

	journal = EXT3_SB(sb)->s_journal;
	ret = ext3_journal_force_commit(journal);
	return ret;
}

static int ext3_sync_fs(struct super_block *sb, int wait)
{
	tid_t target;

	trace_ext3_sync_fs(sb, wait);
	/*
	 * Writeback quota in non-journalled quota case - journalled quota has
	 * no dirty dquots
	 */
	dquot_writeback_dquots(sb, -1);
	if (journal_start_commit(EXT3_SB(sb)->s_journal, &target)) {
		if (wait)
			log_wait_commit(EXT3_SB(sb)->s_journal, target);
	}
	return 0;
}

/*
 * LVM calls this function before a (read-only) snapshot is created.  This
 * gives us a chance to flush the journal completely and mark the fs clean.
 */
static int ext3_freeze(struct super_block *sb)
{
	int error = 0;
	journal_t *journal;

	if (!(sb->s_flags & MS_RDONLY)) {
		journal = EXT3_SB(sb)->s_journal;

		/* Now we set up the journal barrier. */
		journal_lock_updates(journal);

		/*
		 * We don't want to clear needs_recovery flag when we failed
		 * to flush the journal.
		 */
		error = journal_flush(journal);
		if (error < 0)
			goto out;

		/* Journal blocked and flushed, clear needs_recovery flag. */
		EXT3_CLEAR_INCOMPAT_FEATURE(sb, EXT3_FEATURE_INCOMPAT_RECOVER);
		error = ext3_commit_super(sb, EXT3_SB(sb)->s_es, 1);
		if (error)
			goto out;
	}
	return 0;

out:
	journal_unlock_updates(journal);
	return error;
}

/*
 * Called by LVM after the snapshot is done.  We need to reset the RECOVER
 * flag here, even though the filesystem is not technically dirty yet.
 */
static int ext3_unfreeze(struct super_block *sb)
{
	if (!(sb->s_flags & MS_RDONLY)) {
		/* Reser the needs_recovery flag before the fs is unlocked. */
		EXT3_SET_INCOMPAT_FEATURE(sb, EXT3_FEATURE_INCOMPAT_RECOVER);
		ext3_commit_super(sb, EXT3_SB(sb)->s_es, 1);
		journal_unlock_updates(EXT3_SB(sb)->s_journal);
	}
	return 0;
}

static int ext3_remount (struct super_block * sb, int * flags, char * data)
{
	struct ext3_super_block * es;
	struct ext3_sb_info *sbi = EXT3_SB(sb);
	ext3_fsblk_t n_blocks_count = 0;
	unsigned long old_sb_flags;
	struct ext3_mount_options old_opts;
	int enable_quota = 0;
	int err;
#ifdef CONFIG_QUOTA
	int i;
#endif

	sync_filesystem(sb);

	/* Store the original options */
	old_sb_flags = sb->s_flags;
	old_opts.s_mount_opt = sbi->s_mount_opt;
	old_opts.s_resuid = sbi->s_resuid;
	old_opts.s_resgid = sbi->s_resgid;
	old_opts.s_commit_interval = sbi->s_commit_interval;
#ifdef CONFIG_QUOTA
	old_opts.s_jquota_fmt = sbi->s_jquota_fmt;
	for (i = 0; i < MAXQUOTAS; i++)
		if (sbi->s_qf_names[i]) {
			old_opts.s_qf_names[i] = kstrdup(sbi->s_qf_names[i],
							 GFP_KERNEL);
			if (!old_opts.s_qf_names[i]) {
				int j;

				for (j = 0; j < i; j++)
					kfree(old_opts.s_qf_names[j]);
				return -ENOMEM;
			}
		} else
			old_opts.s_qf_names[i] = NULL;
#endif

	/*
	 * Allow the "check" option to be passed as a remount option.
	 */
	if (!parse_options(data, sb, NULL, NULL, &n_blocks_count, 1)) {
		err = -EINVAL;
		goto restore_opts;
	}

	if (test_opt(sb, ABORT))
		ext3_abort(sb, __func__, "Abort forced by user");

	sb->s_flags = (sb->s_flags & ~MS_POSIXACL) |
		(test_opt(sb, POSIX_ACL) ? MS_POSIXACL : 0);

	es = sbi->s_es;

	ext3_init_journal_params(sb, sbi->s_journal);

	if ((*flags & MS_RDONLY) != (sb->s_flags & MS_RDONLY) ||
		n_blocks_count > le32_to_cpu(es->s_blocks_count)) {
		if (test_opt(sb, ABORT)) {
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
			if (!(es->s_state & cpu_to_le16(EXT3_VALID_FS)) &&
			    (sbi->s_mount_state & EXT3_VALID_FS))
				es->s_state = cpu_to_le16(sbi->s_mount_state);

			ext3_mark_recovery_complete(sb, es);
		} else {
			__le32 ret;
			if ((ret = EXT3_HAS_RO_COMPAT_FEATURE(sb,
					~EXT3_FEATURE_RO_COMPAT_SUPP))) {
				ext3_msg(sb, KERN_WARNING,
					"warning: couldn't remount RDWR "
					"because of unsupported optional "
					"features (%x)", le32_to_cpu(ret));
				err = -EROFS;
				goto restore_opts;
			}

			/*
			 * If we have an unprocessed orphan list hanging
			 * around from a previously readonly bdev mount,
			 * require a full umount & mount for now.
			 */
			if (es->s_last_orphan) {
				ext3_msg(sb, KERN_WARNING, "warning: couldn't "
				       "remount RDWR because of unprocessed "
				       "orphan inode list.  Please "
				       "umount & mount instead.");
				err = -EINVAL;
				goto restore_opts;
			}

			/*
			 * Mounting a RDONLY partition read-write, so reread
			 * and store the current valid flag.  (It may have
			 * been changed by e2fsck since we originally mounted
			 * the partition.)
			 */
			ext3_clear_journal_err(sb, es);
			sbi->s_mount_state = le16_to_cpu(es->s_state);
			if ((err = ext3_group_extend(sb, es, n_blocks_count)))
				goto restore_opts;
			if (!ext3_setup_super (sb, es, 0))
				sb->s_flags &= ~MS_RDONLY;
			enable_quota = 1;
		}
	}
#ifdef CONFIG_QUOTA
	/* Release old quota file names */
	for (i = 0; i < MAXQUOTAS; i++)
		kfree(old_opts.s_qf_names[i]);
#endif
	if (enable_quota)
		dquot_resume(sb, -1);
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
		kfree(sbi->s_qf_names[i]);
		sbi->s_qf_names[i] = old_opts.s_qf_names[i];
	}
#endif
	return err;
}

static int ext3_statfs (struct dentry * dentry, struct kstatfs * buf)
{
	struct super_block *sb = dentry->d_sb;
	struct ext3_sb_info *sbi = EXT3_SB(sb);
	struct ext3_super_block *es = sbi->s_es;
	u64 fsid;

	if (test_opt(sb, MINIX_DF)) {
		sbi->s_overhead_last = 0;
	} else if (sbi->s_blocks_last != le32_to_cpu(es->s_blocks_count)) {
		unsigned long ngroups = sbi->s_groups_count, i;
		ext3_fsblk_t overhead = 0;
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
			overhead += ext3_bg_has_super(sb, i) +
				ext3_bg_num_gdb(sb, i);
			cond_resched();
		}

		/*
		 * Every block group has an inode bitmap, a block
		 * bitmap, and an inode table.
		 */
		overhead += ngroups * (2 + sbi->s_itb_per_group);

		/* Add the internal journal blocks as well */
		if (sbi->s_journal && !sbi->journal_bdev)
			overhead += sbi->s_journal->j_maxlen;

		sbi->s_overhead_last = overhead;
		smp_wmb();
		sbi->s_blocks_last = le32_to_cpu(es->s_blocks_count);
	}

	buf->f_type = EXT3_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = le32_to_cpu(es->s_blocks_count) - sbi->s_overhead_last;
	buf->f_bfree = percpu_counter_sum_positive(&sbi->s_freeblocks_counter);
	buf->f_bavail = buf->f_bfree - le32_to_cpu(es->s_r_blocks_count);
	if (buf->f_bfree < le32_to_cpu(es->s_r_blocks_count))
		buf->f_bavail = 0;
	buf->f_files = le32_to_cpu(es->s_inodes_count);
	buf->f_ffree = percpu_counter_sum_positive(&sbi->s_freeinodes_counter);
	buf->f_namelen = EXT3_NAME_LEN;
	fsid = le64_to_cpup((void *)es->s_uuid) ^
	       le64_to_cpup((void *)es->s_uuid + sizeof(u64));
	buf->f_fsid.val[0] = fsid & 0xFFFFFFFFUL;
	buf->f_fsid.val[1] = (fsid >> 32) & 0xFFFFFFFFUL;
	return 0;
}

/* Helper function for writing quotas on sync - we need to start transaction before quota file
 * is locked for write. Otherwise the are possible deadlocks:
 * Process 1                         Process 2
 * ext3_create()                     quota_sync()
 *   journal_start()                   write_dquot()
 *   dquot_initialize()                       down(dqio_mutex)
 *     down(dqio_mutex)                    journal_start()
 *
 */

#ifdef CONFIG_QUOTA

static inline struct inode *dquot_to_inode(struct dquot *dquot)
{
	return sb_dqopt(dquot->dq_sb)->files[dquot->dq_id.type];
}

static int ext3_write_dquot(struct dquot *dquot)
{
	int ret, err;
	handle_t *handle;
	struct inode *inode;

	inode = dquot_to_inode(dquot);
	handle = ext3_journal_start(inode,
					EXT3_QUOTA_TRANS_BLOCKS(dquot->dq_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);
	ret = dquot_commit(dquot);
	err = ext3_journal_stop(handle);
	if (!ret)
		ret = err;
	return ret;
}

static int ext3_acquire_dquot(struct dquot *dquot)
{
	int ret, err;
	handle_t *handle;

	handle = ext3_journal_start(dquot_to_inode(dquot),
					EXT3_QUOTA_INIT_BLOCKS(dquot->dq_sb));
	if (IS_ERR(handle))
		return PTR_ERR(handle);
	ret = dquot_acquire(dquot);
	err = ext3_journal_stop(handle);
	if (!ret)
		ret = err;
	return ret;
}

static int ext3_release_dquot(struct dquot *dquot)
{
	int ret, err;
	handle_t *handle;

	handle = ext3_journal_start(dquot_to_inode(dquot),
					EXT3_QUOTA_DEL_BLOCKS(dquot->dq_sb));
	if (IS_ERR(handle)) {
		/* Release dquot anyway to avoid endless cycle in dqput() */
		dquot_release(dquot);
		return PTR_ERR(handle);
	}
	ret = dquot_release(dquot);
	err = ext3_journal_stop(handle);
	if (!ret)
		ret = err;
	return ret;
}

static int ext3_mark_dquot_dirty(struct dquot *dquot)
{
	/* Are we journaling quotas? */
	if (EXT3_SB(dquot->dq_sb)->s_qf_names[USRQUOTA] ||
	    EXT3_SB(dquot->dq_sb)->s_qf_names[GRPQUOTA]) {
		dquot_mark_dquot_dirty(dquot);
		return ext3_write_dquot(dquot);
	} else {
		return dquot_mark_dquot_dirty(dquot);
	}
}

static int ext3_write_info(struct super_block *sb, int type)
{
	int ret, err;
	handle_t *handle;

	/* Data block + inode block */
	handle = ext3_journal_start(sb->s_root->d_inode, 2);
	if (IS_ERR(handle))
		return PTR_ERR(handle);
	ret = dquot_commit_info(sb, type);
	err = ext3_journal_stop(handle);
	if (!ret)
		ret = err;
	return ret;
}

/*
 * Turn on quotas during mount time - we need to find
 * the quota file and such...
 */
static int ext3_quota_on_mount(struct super_block *sb, int type)
{
	return dquot_quota_on_mount(sb, EXT3_SB(sb)->s_qf_names[type],
					EXT3_SB(sb)->s_jquota_fmt, type);
}

/*
 * Standard function to be called on quota_on
 */
static int ext3_quota_on(struct super_block *sb, int type, int format_id,
			 struct path *path)
{
	int err;

	if (!test_opt(sb, QUOTA))
		return -EINVAL;

	/* Quotafile not on the same filesystem? */
	if (path->dentry->d_sb != sb)
		return -EXDEV;
	/* Journaling quota? */
	if (EXT3_SB(sb)->s_qf_names[type]) {
		/* Quotafile not of fs root? */
		if (path->dentry->d_parent != sb->s_root)
			ext3_msg(sb, KERN_WARNING,
				"warning: Quota file not on filesystem root. "
				"Journaled quota will not work.");
	}

	/*
	 * When we journal data on quota file, we have to flush journal to see
	 * all updates to the file when we bypass pagecache...
	 */
	if (ext3_should_journal_data(path->dentry->d_inode)) {
		/*
		 * We don't need to lock updates but journal_flush() could
		 * otherwise be livelocked...
		 */
		journal_lock_updates(EXT3_SB(sb)->s_journal);
		err = journal_flush(EXT3_SB(sb)->s_journal);
		journal_unlock_updates(EXT3_SB(sb)->s_journal);
		if (err)
			return err;
	}

	return dquot_quota_on(sb, type, format_id, path);
}

/* Read data from quotafile - avoid pagecache and such because we cannot afford
 * acquiring the locks... As quota files are never truncated and quota code
 * itself serializes the operations (and no one else should touch the files)
 * we don't have to be afraid of races */
static ssize_t ext3_quota_read(struct super_block *sb, int type, char *data,
			       size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	sector_t blk = off >> EXT3_BLOCK_SIZE_BITS(sb);
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
		bh = ext3_bread(NULL, inode, blk, 0, &err);
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
static ssize_t ext3_quota_write(struct super_block *sb, int type,
				const char *data, size_t len, loff_t off)
{
	struct inode *inode = sb_dqopt(sb)->files[type];
	sector_t blk = off >> EXT3_BLOCK_SIZE_BITS(sb);
	int err = 0;
	int offset = off & (sb->s_blocksize - 1);
	int journal_quota = EXT3_SB(sb)->s_qf_names[type] != NULL;
	struct buffer_head *bh;
	handle_t *handle = journal_current_handle();

	if (!handle) {
		ext3_msg(sb, KERN_WARNING,
			"warning: quota write (off=%llu, len=%llu)"
			" cancelled because transaction is not started.",
			(unsigned long long)off, (unsigned long long)len);
		return -EIO;
	}

	/*
	 * Since we account only one data block in transaction credits,
	 * then it is impossible to cross a block boundary.
	 */
	if (sb->s_blocksize - offset < len) {
		ext3_msg(sb, KERN_WARNING, "Quota write (off=%llu, len=%llu)"
			" cancelled because not block aligned",
			(unsigned long long)off, (unsigned long long)len);
		return -EIO;
	}
	bh = ext3_bread(handle, inode, blk, 1, &err);
	if (!bh)
		goto out;
	if (journal_quota) {
		err = ext3_journal_get_write_access(handle, bh);
		if (err) {
			brelse(bh);
			goto out;
		}
	}
	lock_buffer(bh);
	memcpy(bh->b_data+offset, data, len);
	flush_dcache_page(bh->b_page);
	unlock_buffer(bh);
	if (journal_quota)
		err = ext3_journal_dirty_metadata(handle, bh);
	else {
		/* Always do at least ordered writes for quotas */
		err = ext3_journal_dirty_data(handle, bh);
		mark_buffer_dirty(bh);
	}
	brelse(bh);
out:
	if (err)
		return err;
	if (inode->i_size < off + len) {
		i_size_write(inode, off + len);
		EXT3_I(inode)->i_disksize = inode->i_size;
	}
	inode->i_version++;
	inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	ext3_mark_inode_dirty(handle, inode);
	return len;
}

#endif

static struct dentry *ext3_mount(struct file_system_type *fs_type,
	int flags, const char *dev_name, void *data)
{
	return mount_bdev(fs_type, flags, dev_name, data, ext3_fill_super);
}

static struct file_system_type ext3_fs_type = {
	.owner		= THIS_MODULE,
	.name		= "ext3",
	.mount		= ext3_mount,
	.kill_sb	= kill_block_super,
	.fs_flags	= FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("ext3");

static int __init init_ext3_fs(void)
{
	int err = init_ext3_xattr();
	if (err)
		return err;
	err = init_inodecache();
	if (err)
		goto out1;
        err = register_filesystem(&ext3_fs_type);
	if (err)
		goto out;
	return 0;
out:
	destroy_inodecache();
out1:
	exit_ext3_xattr();
	return err;
}

static void __exit exit_ext3_fs(void)
{
	unregister_filesystem(&ext3_fs_type);
	destroy_inodecache();
	exit_ext3_xattr();
}

MODULE_AUTHOR("Remy Card, Stephen Tweedie, Andrew Morton, Andreas Dilger, Theodore Ts'o and others");
MODULE_DESCRIPTION("Second Extended Filesystem with journaling extensions");
MODULE_LICENSE("GPL");
module_init(init_ext3_fs)
module_exit(exit_ext3_fs)
