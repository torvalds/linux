/*
 * super.c - NILFS module and super block management.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Written by Ryusuke Konishi <ryusuke@osrg.net>
 */
/*
 *  linux/fs/ext2/super.c
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
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/blkdev.h>
#include <linux/parser.h>
#include <linux/random.h>
#include <linux/crc32.h>
#include <linux/smp_lock.h>
#include <linux/vfs.h>
#include <linux/writeback.h>
#include <linux/kobject.h>
#include <linux/exportfs.h>
#include "nilfs.h"
#include "mdt.h"
#include "alloc.h"
#include "page.h"
#include "cpfile.h"
#include "ifile.h"
#include "dat.h"
#include "segment.h"
#include "segbuf.h"

MODULE_AUTHOR("NTT Corp.");
MODULE_DESCRIPTION("A New Implementation of the Log-structured Filesystem "
		   "(NILFS)");
MODULE_VERSION(NILFS_VERSION);
MODULE_LICENSE("GPL");

static int nilfs_remount(struct super_block *sb, int *flags, char *data);
static int test_exclusive_mount(struct file_system_type *fs_type,
				struct block_device *bdev, int flags);

/**
 * nilfs_error() - report failure condition on a filesystem
 *
 * nilfs_error() sets an ERROR_FS flag on the superblock as well as
 * reporting an error message.  It should be called when NILFS detects
 * incoherences or defects of meta data on disk.  As for sustainable
 * errors such as a single-shot I/O error, nilfs_warning() or the printk()
 * function should be used instead.
 *
 * The segment constructor must not call this function because it can
 * kill itself.
 */
void nilfs_error(struct super_block *sb, const char *function,
		 const char *fmt, ...)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	va_list args;

	va_start(args, fmt);
	printk(KERN_CRIT "NILFS error (device %s): %s: ", sb->s_id, function);
	vprintk(fmt, args);
	printk("\n");
	va_end(args);

	if (!(sb->s_flags & MS_RDONLY)) {
		struct the_nilfs *nilfs = sbi->s_nilfs;

		if (!nilfs_test_opt(sbi, ERRORS_CONT))
			nilfs_detach_segment_constructor(sbi);

		down_write(&nilfs->ns_sem);
		if (!(nilfs->ns_mount_state & NILFS_ERROR_FS)) {
			nilfs->ns_mount_state |= NILFS_ERROR_FS;
			nilfs->ns_sbp->s_state |= cpu_to_le16(NILFS_ERROR_FS);
			nilfs_commit_super(sbi);
		}
		up_write(&nilfs->ns_sem);

		if (nilfs_test_opt(sbi, ERRORS_RO)) {
			printk(KERN_CRIT "Remounting filesystem read-only\n");
			sb->s_flags |= MS_RDONLY;
		}
	}

	if (nilfs_test_opt(sbi, ERRORS_PANIC))
		panic("NILFS (device %s): panic forced after error\n",
		      sb->s_id);
}

void nilfs_warning(struct super_block *sb, const char *function,
		   const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	printk(KERN_WARNING "NILFS warning (device %s): %s: ",
	       sb->s_id, function);
	vprintk(fmt, args);
	printk("\n");
	va_end(args);
}

static struct kmem_cache *nilfs_inode_cachep;

struct inode *nilfs_alloc_inode(struct super_block *sb)
{
	struct nilfs_inode_info *ii;

	ii = kmem_cache_alloc(nilfs_inode_cachep, GFP_NOFS);
	if (!ii)
		return NULL;
	ii->i_bh = NULL;
	ii->i_state = 0;
	ii->vfs_inode.i_version = 1;
	nilfs_btnode_cache_init(&ii->i_btnode_cache);
	return &ii->vfs_inode;
}

void nilfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(nilfs_inode_cachep, NILFS_I(inode));
}

static void init_once(void *obj)
{
	struct nilfs_inode_info *ii = obj;

	INIT_LIST_HEAD(&ii->i_dirty);
#ifdef CONFIG_NILFS_XATTR
	init_rwsem(&ii->xattr_sem);
#endif
	nilfs_btnode_cache_init_once(&ii->i_btnode_cache);
	ii->i_bmap = (struct nilfs_bmap *)&ii->i_bmap_union;
	inode_init_once(&ii->vfs_inode);
}

static int nilfs_init_inode_cache(void)
{
	nilfs_inode_cachep = kmem_cache_create("nilfs2_inode_cache",
					       sizeof(struct nilfs_inode_info),
					       0, SLAB_RECLAIM_ACCOUNT,
					       init_once);

	return (nilfs_inode_cachep == NULL) ? -ENOMEM : 0;
}

static inline void nilfs_destroy_inode_cache(void)
{
	kmem_cache_destroy(nilfs_inode_cachep);
}

static void nilfs_clear_inode(struct inode *inode)
{
	struct nilfs_inode_info *ii = NILFS_I(inode);

#ifdef CONFIG_NILFS_POSIX_ACL
	if (ii->i_acl && ii->i_acl != NILFS_ACL_NOT_CACHED) {
		posix_acl_release(ii->i_acl);
		ii->i_acl = NILFS_ACL_NOT_CACHED;
	}
	if (ii->i_default_acl && ii->i_default_acl != NILFS_ACL_NOT_CACHED) {
		posix_acl_release(ii->i_default_acl);
		ii->i_default_acl = NILFS_ACL_NOT_CACHED;
	}
#endif
	/*
	 * Free resources allocated in nilfs_read_inode(), here.
	 */
	BUG_ON(!list_empty(&ii->i_dirty));
	brelse(ii->i_bh);
	ii->i_bh = NULL;

	if (test_bit(NILFS_I_BMAP, &ii->i_state))
		nilfs_bmap_clear(ii->i_bmap);

	nilfs_btnode_cache_clear(&ii->i_btnode_cache);
}

/**
 * nilfs_update_last_segment - change pointer to the latest segment
 * @sbi: nilfs_sb_info
 * @update_cno: flag whether to update checkpoint number.
 *
 * nilfs_update_last_segment() changes information in the super block
 * after a partial segment is written out successfully. The super
 * block is marked dirty. It will be written out at the next VFS sync
 * operations such as sync_supers() and generic_shutdown_super().
 */
void nilfs_update_last_segment(struct nilfs_sb_info *sbi, int update_cno)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	struct nilfs_super_block *sbp = nilfs->ns_sbp;

	/* nilfs->sem must be locked by the caller. */
	spin_lock(&nilfs->ns_last_segment_lock);
	if (update_cno)
		nilfs->ns_last_cno = nilfs->ns_cno++;
	sbp->s_last_seq = cpu_to_le64(nilfs->ns_last_seq);
	sbp->s_last_pseg = cpu_to_le64(nilfs->ns_last_pseg);
	sbp->s_last_cno = cpu_to_le64(nilfs->ns_last_cno);
	spin_unlock(&nilfs->ns_last_segment_lock);

	sbi->s_super->s_dirt = 1; /* must be set if delaying the call of
				     nilfs_commit_super() */
}

static int nilfs_sync_super(struct nilfs_sb_info *sbi)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	int err;
	int barrier_done = 0;

	if (nilfs_test_opt(sbi, BARRIER)) {
		set_buffer_ordered(nilfs->ns_sbh);
		barrier_done = 1;
	}
 retry:
	set_buffer_dirty(nilfs->ns_sbh);
	err = sync_dirty_buffer(nilfs->ns_sbh);
	if (err == -EOPNOTSUPP && barrier_done) {
		nilfs_warning(sbi->s_super, __func__,
			      "barrier-based sync failed. "
			      "disabling barriers\n");
		nilfs_clear_opt(sbi, BARRIER);
		barrier_done = 0;
		clear_buffer_ordered(nilfs->ns_sbh);
		goto retry;
	}
	if (unlikely(err))
		printk(KERN_ERR
		       "NILFS: unable to write superblock (err=%d)\n", err);
	else {
		clear_nilfs_discontinued(nilfs);
		spin_lock(&nilfs->ns_last_segment_lock);
		nilfs->ns_prot_seq = le64_to_cpu(nilfs->ns_sbp->s_last_seq);
		spin_unlock(&nilfs->ns_last_segment_lock);
	}

	return err;
}

int nilfs_commit_super(struct nilfs_sb_info *sbi)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	struct nilfs_super_block *sbp = nilfs->ns_sbp;
	sector_t nfreeblocks;
	int err;

	/* nilfs->sem must be locked by the caller. */
	err = nilfs_count_free_blocks(nilfs, &nfreeblocks);
	if (unlikely(err)) {
		printk(KERN_ERR "NILFS: failed to count free blocks\n");
		return err;
	}
	sbp->s_free_blocks_count = cpu_to_le64(nfreeblocks);
	sbp->s_wtime = cpu_to_le64(get_seconds());
	sbp->s_sum = 0;
	sbp->s_sum = cpu_to_le32(crc32_le(nilfs->ns_crc_seed,
					  (unsigned char *)sbp,
					  le16_to_cpu(sbp->s_bytes)));
	sbi->s_super->s_dirt = 0;
	return nilfs_sync_super(sbi);
}

static void nilfs_put_super(struct super_block *sb)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	struct the_nilfs *nilfs = sbi->s_nilfs;

	nilfs_detach_segment_constructor(sbi);

	if (!(sb->s_flags & MS_RDONLY)) {
		down_write(&nilfs->ns_sem);
		nilfs->ns_sbp->s_state = cpu_to_le16(nilfs->ns_mount_state);
		nilfs_commit_super(sbi);
		up_write(&nilfs->ns_sem);
	}

	nilfs_detach_checkpoint(sbi);
	put_nilfs(sbi->s_nilfs);
	sbi->s_super = NULL;
	sb->s_fs_info = NULL;
	kfree(sbi);
}

/**
 * nilfs_write_super - write super block(s) of NILFS
 * @sb: super_block
 *
 * nilfs_write_super() gets a fs-dependent lock, writes super block(s), and
 * clears s_dirt.  This function is called in the section protected by
 * lock_super().
 *
 * The s_dirt flag is managed by each filesystem and we protect it by ns_sem
 * of the struct the_nilfs.  Lock order must be as follows:
 *
 *   1. lock_super()
 *   2.    down_write(&nilfs->ns_sem)
 *
 * Inside NILFS, locking ns_sem is enough to protect s_dirt and the buffer
 * of the super block (nilfs->ns_sbp).
 *
 * In most cases, VFS functions call lock_super() before calling these
 * methods.  So we must be careful not to bring on deadlocks when using
 * lock_super();  see generic_shutdown_super(), write_super(), and so on.
 *
 * Note that order of lock_kernel() and lock_super() depends on contexts
 * of VFS.  We should also note that lock_kernel() can be used in its
 * protective section and only the outermost one has an effect.
 */
static void nilfs_write_super(struct super_block *sb)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	struct the_nilfs *nilfs = sbi->s_nilfs;

	down_write(&nilfs->ns_sem);
	if (!(sb->s_flags & MS_RDONLY))
		nilfs_commit_super(sbi);
	sb->s_dirt = 0;
	up_write(&nilfs->ns_sem);
}

static int nilfs_sync_fs(struct super_block *sb, int wait)
{
	int err = 0;

	/* This function is called when super block should be written back */
	if (wait)
		err = nilfs_construct_segment(sb);
	return err;
}

int nilfs_attach_checkpoint(struct nilfs_sb_info *sbi, __u64 cno)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	struct nilfs_checkpoint *raw_cp;
	struct buffer_head *bh_cp;
	int err;

	down_write(&nilfs->ns_sem);
	list_add(&sbi->s_list, &nilfs->ns_supers);
	up_write(&nilfs->ns_sem);

	sbi->s_ifile = nilfs_mdt_new(
		nilfs, sbi->s_super, NILFS_IFILE_INO, NILFS_IFILE_GFP);
	if (!sbi->s_ifile)
		return -ENOMEM;

	err = nilfs_palloc_init_blockgroup(sbi->s_ifile, nilfs->ns_inode_size);
	if (unlikely(err))
		goto failed;

	err = nilfs_cpfile_get_checkpoint(nilfs->ns_cpfile, cno, 0, &raw_cp,
					  &bh_cp);
	if (unlikely(err)) {
		if (err == -ENOENT || err == -EINVAL) {
			printk(KERN_ERR
			       "NILFS: Invalid checkpoint "
			       "(checkpoint number=%llu)\n",
			       (unsigned long long)cno);
			err = -EINVAL;
		}
		goto failed;
	}
	err = nilfs_read_inode_common(sbi->s_ifile, &raw_cp->cp_ifile_inode);
	if (unlikely(err))
		goto failed_bh;
	atomic_set(&sbi->s_inodes_count, le64_to_cpu(raw_cp->cp_inodes_count));
	atomic_set(&sbi->s_blocks_count, le64_to_cpu(raw_cp->cp_blocks_count));

	nilfs_cpfile_put_checkpoint(nilfs->ns_cpfile, cno, bh_cp);
	return 0;

 failed_bh:
	nilfs_cpfile_put_checkpoint(nilfs->ns_cpfile, cno, bh_cp);
 failed:
	nilfs_mdt_destroy(sbi->s_ifile);
	sbi->s_ifile = NULL;

	down_write(&nilfs->ns_sem);
	list_del_init(&sbi->s_list);
	up_write(&nilfs->ns_sem);

	return err;
}

void nilfs_detach_checkpoint(struct nilfs_sb_info *sbi)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;

	nilfs_mdt_clear(sbi->s_ifile);
	nilfs_mdt_destroy(sbi->s_ifile);
	sbi->s_ifile = NULL;
	down_write(&nilfs->ns_sem);
	list_del_init(&sbi->s_list);
	up_write(&nilfs->ns_sem);
}

static int nilfs_mark_recovery_complete(struct nilfs_sb_info *sbi)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	int err = 0;

	down_write(&nilfs->ns_sem);
	if (!(nilfs->ns_mount_state & NILFS_VALID_FS)) {
		nilfs->ns_mount_state |= NILFS_VALID_FS;
		err = nilfs_commit_super(sbi);
		if (likely(!err))
			printk(KERN_INFO "NILFS: recovery complete.\n");
	}
	up_write(&nilfs->ns_sem);
	return err;
}

static int nilfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	unsigned long long blocks;
	unsigned long overhead;
	unsigned long nrsvblocks;
	sector_t nfreeblocks;
	struct the_nilfs *nilfs = sbi->s_nilfs;
	int err;

	/*
	 * Compute all of the segment blocks
	 *
	 * The blocks before first segment and after last segment
	 * are excluded.
	 */
	blocks = nilfs->ns_blocks_per_segment * nilfs->ns_nsegments
		- nilfs->ns_first_data_block;
	nrsvblocks = nilfs->ns_nrsvsegs * nilfs->ns_blocks_per_segment;

	/*
	 * Compute the overhead
	 *
	 * When distributing meta data blocks outside semgent structure,
	 * We must count them as the overhead.
	 */
	overhead = 0;

	err = nilfs_count_free_blocks(nilfs, &nfreeblocks);
	if (unlikely(err))
		return err;

	buf->f_type = NILFS_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = blocks - overhead;
	buf->f_bfree = nfreeblocks;
	buf->f_bavail = (buf->f_bfree >= nrsvblocks) ?
		(buf->f_bfree - nrsvblocks) : 0;
	buf->f_files = atomic_read(&sbi->s_inodes_count);
	buf->f_ffree = 0; /* nilfs_count_free_inodes(sb); */
	buf->f_namelen = NILFS_NAME_LEN;
	return 0;
}

static struct super_operations nilfs_sops = {
	.alloc_inode    = nilfs_alloc_inode,
	.destroy_inode  = nilfs_destroy_inode,
	.dirty_inode    = nilfs_dirty_inode,
	/* .write_inode    = nilfs_write_inode, */
	/* .put_inode      = nilfs_put_inode, */
	/* .drop_inode	  = nilfs_drop_inode, */
	.delete_inode   = nilfs_delete_inode,
	.put_super      = nilfs_put_super,
	.write_super    = nilfs_write_super,
	.sync_fs        = nilfs_sync_fs,
	/* .write_super_lockfs */
	/* .unlockfs */
	.statfs         = nilfs_statfs,
	.remount_fs     = nilfs_remount,
	.clear_inode    = nilfs_clear_inode,
	/* .umount_begin */
	/* .show_options */
};

static struct inode *
nilfs_nfs_get_inode(struct super_block *sb, u64 ino, u32 generation)
{
	struct inode *inode;

	if (ino < NILFS_FIRST_INO(sb) && ino != NILFS_ROOT_INO &&
	    ino != NILFS_SKETCH_INO)
		return ERR_PTR(-ESTALE);

	inode = nilfs_iget(sb, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);
	if (generation && inode->i_generation != generation) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}

	return inode;
}

static struct dentry *
nilfs_fh_to_dentry(struct super_block *sb, struct fid *fid, int fh_len,
		   int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    nilfs_nfs_get_inode);
}

static struct dentry *
nilfs_fh_to_parent(struct super_block *sb, struct fid *fid, int fh_len,
		   int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    nilfs_nfs_get_inode);
}

static struct export_operations nilfs_export_ops = {
	.fh_to_dentry = nilfs_fh_to_dentry,
	.fh_to_parent = nilfs_fh_to_parent,
	.get_parent = nilfs_get_parent,
};

enum {
	Opt_err_cont, Opt_err_panic, Opt_err_ro,
	Opt_barrier, Opt_snapshot, Opt_order,
	Opt_err,
};

static match_table_t tokens = {
	{Opt_err_cont, "errors=continue"},
	{Opt_err_panic, "errors=panic"},
	{Opt_err_ro, "errors=remount-ro"},
	{Opt_barrier, "barrier=%s"},
	{Opt_snapshot, "cp=%u"},
	{Opt_order, "order=%s"},
	{Opt_err, NULL}
};

static int match_bool(substring_t *s, int *result)
{
	int len = s->to - s->from;

	if (strncmp(s->from, "on", len) == 0)
		*result = 1;
	else if (strncmp(s->from, "off", len) == 0)
		*result = 0;
	else
		return 1;
	return 0;
}

static int parse_options(char *options, struct super_block *sb)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	char *p;
	substring_t args[MAX_OPT_ARGS];
	int option;

	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;
		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_barrier:
			if (match_bool(&args[0], &option))
				return 0;
			if (option)
				nilfs_set_opt(sbi, BARRIER);
			else
				nilfs_clear_opt(sbi, BARRIER);
			break;
		case Opt_order:
			if (strcmp(args[0].from, "relaxed") == 0)
				/* Ordered data semantics */
				nilfs_clear_opt(sbi, STRICT_ORDER);
			else if (strcmp(args[0].from, "strict") == 0)
				/* Strict in-order semantics */
				nilfs_set_opt(sbi, STRICT_ORDER);
			else
				return 0;
			break;
		case Opt_err_panic:
			nilfs_write_opt(sbi, ERROR_MODE, ERRORS_PANIC);
			break;
		case Opt_err_ro:
			nilfs_write_opt(sbi, ERROR_MODE, ERRORS_RO);
			break;
		case Opt_err_cont:
			nilfs_write_opt(sbi, ERROR_MODE, ERRORS_CONT);
			break;
		case Opt_snapshot:
			if (match_int(&args[0], &option) || option <= 0)
				return 0;
			if (!(sb->s_flags & MS_RDONLY))
				return 0;
			sbi->s_snapshot_cno = option;
			nilfs_set_opt(sbi, SNAPSHOT);
			break;
		default:
			printk(KERN_ERR
			       "NILFS: Unrecognized mount option \"%s\"\n", p);
			return 0;
		}
	}
	return 1;
}

static inline void
nilfs_set_default_options(struct nilfs_sb_info *sbi,
			  struct nilfs_super_block *sbp)
{
	sbi->s_mount_opt =
		NILFS_MOUNT_ERRORS_CONT | NILFS_MOUNT_BARRIER;
}

static int nilfs_setup_super(struct nilfs_sb_info *sbi)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	struct nilfs_super_block *sbp = nilfs->ns_sbp;
	int max_mnt_count = le16_to_cpu(sbp->s_max_mnt_count);
	int mnt_count = le16_to_cpu(sbp->s_mnt_count);

	/* nilfs->sem must be locked by the caller. */
	if (!(nilfs->ns_mount_state & NILFS_VALID_FS)) {
		printk(KERN_WARNING "NILFS warning: mounting unchecked fs\n");
	} else if (nilfs->ns_mount_state & NILFS_ERROR_FS) {
		printk(KERN_WARNING
		       "NILFS warning: mounting fs with errors\n");
#if 0
	} else if (max_mnt_count >= 0 && mnt_count >= max_mnt_count) {
		printk(KERN_WARNING
		       "NILFS warning: maximal mount count reached\n");
#endif
	}
	if (!max_mnt_count)
		sbp->s_max_mnt_count = cpu_to_le16(NILFS_DFL_MAX_MNT_COUNT);

	sbp->s_mnt_count = cpu_to_le16(mnt_count + 1);
	sbp->s_state = cpu_to_le16(le16_to_cpu(sbp->s_state) & ~NILFS_VALID_FS);
	sbp->s_mtime = cpu_to_le64(get_seconds());
	return nilfs_commit_super(sbi);
}

struct nilfs_super_block *
nilfs_load_super_block(struct super_block *sb, struct buffer_head **pbh)
{
	int blocksize;
	unsigned long offset, sb_index;

	/*
	 * Adjusting block size
	 * Blocksize will be enlarged when it is smaller than hardware
	 * sector size.
	 * Disk format of superblock does not change.
	 */
	blocksize = sb_min_blocksize(sb, BLOCK_SIZE);
	if (!blocksize) {
		printk(KERN_ERR
		       "NILFS: unable to set blocksize of superblock\n");
		return NULL;
	}
	sb_index = NILFS_SB_OFFSET_BYTES / blocksize;
	offset = NILFS_SB_OFFSET_BYTES % blocksize;

	*pbh = sb_bread(sb, sb_index);
	if (!*pbh) {
		printk(KERN_ERR "NILFS: unable to read superblock\n");
		return NULL;
	}
	return (struct nilfs_super_block *)((char *)(*pbh)->b_data + offset);
}

struct nilfs_super_block *
nilfs_reload_super_block(struct super_block *sb, struct buffer_head **pbh,
			 int blocksize)
{
	struct nilfs_super_block *sbp;
	unsigned long offset, sb_index;
	int hw_blocksize = bdev_hardsect_size(sb->s_bdev);

	if (blocksize < hw_blocksize) {
		printk(KERN_ERR
		       "NILFS: blocksize %d too small for device "
		       "(sector-size = %d).\n",
		       blocksize, hw_blocksize);
		goto failed_sbh;
	}
	brelse(*pbh);
	sb_set_blocksize(sb, blocksize);

	sb_index = NILFS_SB_OFFSET_BYTES / blocksize;
	offset = NILFS_SB_OFFSET_BYTES % blocksize;

	*pbh = sb_bread(sb, sb_index);
	if (!*pbh) {
		printk(KERN_ERR
		       "NILFS: cannot read superblock on 2nd try.\n");
		goto failed;
	}

	sbp = (struct nilfs_super_block *)((char *)(*pbh)->b_data + offset);
	if (sbp->s_magic != cpu_to_le16(NILFS_SUPER_MAGIC)) {
		printk(KERN_ERR
		       "NILFS: !? Magic mismatch on 2nd try.\n");
		goto failed_sbh;
	}
	return sbp;

 failed_sbh:
	brelse(*pbh);

 failed:
	return NULL;
}

int nilfs_store_magic_and_option(struct super_block *sb,
				 struct nilfs_super_block *sbp,
				 char *data)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);

	/* trying to fill super (1st stage) */
	sb->s_magic = le16_to_cpu(sbp->s_magic);

	/* FS independent flags */
#ifdef NILFS_ATIME_DISABLE
	sb->s_flags |= MS_NOATIME;
#endif

	if (sb->s_magic != NILFS_SUPER_MAGIC) {
		printk("NILFS: Can't find nilfs on dev %s.\n", sb->s_id);
		return -EINVAL;
	}

	nilfs_set_default_options(sbi, sbp);

	sbi->s_resuid = le16_to_cpu(sbp->s_def_resuid);
	sbi->s_resgid = le16_to_cpu(sbp->s_def_resgid);
	sbi->s_interval = le32_to_cpu(sbp->s_c_interval);
	sbi->s_watermark = le32_to_cpu(sbp->s_c_block_max);

	if (!parse_options(data, sb))
		return -EINVAL;

	return 0;
}

/**
 * nilfs_fill_super() - initialize a super block instance
 * @sb: super_block
 * @data: mount options
 * @silent: silent mode flag
 * @nilfs: the_nilfs struct
 *
 * This function is called exclusively by bd_mount_mutex.
 * So, the recovery process is protected from other simultaneous mounts.
 */
static int
nilfs_fill_super(struct super_block *sb, void *data, int silent,
		 struct the_nilfs *nilfs)
{
	struct nilfs_sb_info *sbi;
	struct inode *root;
	__u64 cno;
	int err;

	sbi = kzalloc(sizeof(*sbi), GFP_KERNEL);
	if (!sbi)
		return -ENOMEM;

	sb->s_fs_info = sbi;

	get_nilfs(nilfs);
	sbi->s_nilfs = nilfs;
	sbi->s_super = sb;

	err = init_nilfs(nilfs, sbi, (char *)data);
	if (err)
		goto failed_sbi;

	spin_lock_init(&sbi->s_inode_lock);
	INIT_LIST_HEAD(&sbi->s_dirty_files);
	INIT_LIST_HEAD(&sbi->s_list);

	/*
	 * Following initialization is overlapped because
	 * nilfs_sb_info structure has been cleared at the beginning.
	 * But we reserve them to keep our interest and make ready
	 * for the future change.
	 */
	get_random_bytes(&sbi->s_next_generation,
			 sizeof(sbi->s_next_generation));
	spin_lock_init(&sbi->s_next_gen_lock);

	sb->s_op = &nilfs_sops;
	sb->s_export_op = &nilfs_export_ops;
	sb->s_root = NULL;

	if (!nilfs_loaded(nilfs)) {
		err = load_nilfs(nilfs, sbi);
		if (err)
			goto failed_sbi;
	}
	cno = nilfs_last_cno(nilfs);

	if (sb->s_flags & MS_RDONLY) {
		if (nilfs_test_opt(sbi, SNAPSHOT)) {
			err = nilfs_cpfile_is_snapshot(nilfs->ns_cpfile,
						       sbi->s_snapshot_cno);
			if (err < 0)
				goto failed_sbi;
			if (!err) {
				printk(KERN_ERR
				       "NILFS: The specified checkpoint is "
				       "not a snapshot "
				       "(checkpoint number=%llu).\n",
				       (unsigned long long)sbi->s_snapshot_cno);
				err = -EINVAL;
				goto failed_sbi;
			}
			cno = sbi->s_snapshot_cno;
		} else
			/* Read-only mount */
			sbi->s_snapshot_cno = cno;
	}

	err = nilfs_attach_checkpoint(sbi, cno);
	if (err) {
		printk(KERN_ERR "NILFS: error loading a checkpoint"
		       " (checkpoint number=%llu).\n", (unsigned long long)cno);
		goto failed_sbi;
	}

	if (!(sb->s_flags & MS_RDONLY)) {
		err = nilfs_attach_segment_constructor(sbi, NULL);
		if (err)
			goto failed_checkpoint;
	}

	root = nilfs_iget(sb, NILFS_ROOT_INO);
	if (IS_ERR(root)) {
		printk(KERN_ERR "NILFS: get root inode failed\n");
		err = PTR_ERR(root);
		goto failed_segctor;
	}
	if (!S_ISDIR(root->i_mode) || !root->i_blocks || !root->i_size) {
		iput(root);
		printk(KERN_ERR "NILFS: corrupt root inode.\n");
		err = -EINVAL;
		goto failed_segctor;
	}
	sb->s_root = d_alloc_root(root);
	if (!sb->s_root) {
		iput(root);
		printk(KERN_ERR "NILFS: get root dentry failed\n");
		err = -ENOMEM;
		goto failed_segctor;
	}

	if (!(sb->s_flags & MS_RDONLY)) {
		down_write(&nilfs->ns_sem);
		nilfs_setup_super(sbi);
		up_write(&nilfs->ns_sem);
	}

	err = nilfs_mark_recovery_complete(sbi);
	if (unlikely(err)) {
		printk(KERN_ERR "NILFS: recovery failed.\n");
		goto failed_root;
	}

	return 0;

 failed_root:
	dput(sb->s_root);
	sb->s_root = NULL;

 failed_segctor:
	nilfs_detach_segment_constructor(sbi);

 failed_checkpoint:
	nilfs_detach_checkpoint(sbi);

 failed_sbi:
	put_nilfs(nilfs);
	sb->s_fs_info = NULL;
	kfree(sbi);
	return err;
}

static int nilfs_remount(struct super_block *sb, int *flags, char *data)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	struct nilfs_super_block *sbp;
	struct the_nilfs *nilfs = sbi->s_nilfs;
	unsigned long old_sb_flags;
	struct nilfs_mount_options old_opts;
	int err;

	old_sb_flags = sb->s_flags;
	old_opts.mount_opt = sbi->s_mount_opt;
	old_opts.snapshot_cno = sbi->s_snapshot_cno;

	if (!parse_options(data, sb)) {
		err = -EINVAL;
		goto restore_opts;
	}
	sb->s_flags = (sb->s_flags & ~MS_POSIXACL);

	if ((*flags & MS_RDONLY) &&
	    sbi->s_snapshot_cno != old_opts.snapshot_cno) {
		printk(KERN_WARNING "NILFS (device %s): couldn't "
		       "remount to a different snapshot. \n",
		       sb->s_id);
		err = -EINVAL;
		goto restore_opts;
	}

	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		goto out;
	if (*flags & MS_RDONLY) {
		/* Shutting down the segment constructor */
		nilfs_detach_segment_constructor(sbi);
		sb->s_flags |= MS_RDONLY;

		sbi->s_snapshot_cno = nilfs_last_cno(nilfs);
		/* nilfs_set_opt(sbi, SNAPSHOT); */

		/*
		 * Remounting a valid RW partition RDONLY, so set
		 * the RDONLY flag and then mark the partition as valid again.
		 */
		down_write(&nilfs->ns_sem);
		sbp = nilfs->ns_sbp;
		if (!(sbp->s_state & le16_to_cpu(NILFS_VALID_FS)) &&
		    (nilfs->ns_mount_state & NILFS_VALID_FS))
			sbp->s_state = cpu_to_le16(nilfs->ns_mount_state);
		sbp->s_mtime = cpu_to_le64(get_seconds());
		nilfs_commit_super(sbi);
		up_write(&nilfs->ns_sem);
	} else {
		/*
		 * Mounting a RDONLY partition read-write, so reread and
		 * store the current valid flag.  (It may have been changed
		 * by fsck since we originally mounted the partition.)
		 */
		down(&sb->s_bdev->bd_mount_sem);
		/* Check existing RW-mount */
		if (test_exclusive_mount(sb->s_type, sb->s_bdev, 0)) {
			printk(KERN_WARNING "NILFS (device %s): couldn't "
			       "remount because a RW-mount exists.\n",
			       sb->s_id);
			err = -EBUSY;
			goto rw_remount_failed;
		}
		if (sbi->s_snapshot_cno != nilfs_last_cno(nilfs)) {
			printk(KERN_WARNING "NILFS (device %s): couldn't "
			       "remount because the current RO-mount is not "
			       "the latest one.\n",
			       sb->s_id);
			err = -EINVAL;
			goto rw_remount_failed;
		}
		sb->s_flags &= ~MS_RDONLY;
		nilfs_clear_opt(sbi, SNAPSHOT);
		sbi->s_snapshot_cno = 0;

		err = nilfs_attach_segment_constructor(sbi, NULL);
		if (err)
			goto rw_remount_failed;

		down_write(&nilfs->ns_sem);
		nilfs_setup_super(sbi);
		up_write(&nilfs->ns_sem);

		up(&sb->s_bdev->bd_mount_sem);
	}
 out:
	return 0;

 rw_remount_failed:
	up(&sb->s_bdev->bd_mount_sem);
 restore_opts:
	sb->s_flags = old_sb_flags;
	sbi->s_mount_opt = old_opts.mount_opt;
	sbi->s_snapshot_cno = old_opts.snapshot_cno;
	return err;
}

struct nilfs_super_data {
	struct block_device *bdev;
	__u64 cno;
	int flags;
};

/**
 * nilfs_identify - pre-read mount options needed to identify mount instance
 * @data: mount options
 * @sd: nilfs_super_data
 */
static int nilfs_identify(char *data, struct nilfs_super_data *sd)
{
	char *p, *options = data;
	substring_t args[MAX_OPT_ARGS];
	int option, token;
	int ret = 0;

	do {
		p = strsep(&options, ",");
		if (p != NULL && *p) {
			token = match_token(p, tokens, args);
			if (token == Opt_snapshot) {
				if (!(sd->flags & MS_RDONLY))
					ret++;
				else {
					ret = match_int(&args[0], &option);
					if (!ret) {
						if (option > 0)
							sd->cno = option;
						else
							ret++;
					}
				}
			}
			if (ret)
				printk(KERN_ERR
				       "NILFS: invalid mount option: %s\n", p);
		}
		if (!options)
			break;
		BUG_ON(options == data);
		*(options - 1) = ',';
	} while (!ret);
	return ret;
}

static int nilfs_set_bdev_super(struct super_block *s, void *data)
{
	struct nilfs_super_data *sd = data;

	s->s_bdev = sd->bdev;
	s->s_dev = s->s_bdev->bd_dev;
	return 0;
}

static int nilfs_test_bdev_super(struct super_block *s, void *data)
{
	struct nilfs_super_data *sd = data;

	return s->s_bdev == sd->bdev;
}

static int nilfs_test_bdev_super2(struct super_block *s, void *data)
{
	struct nilfs_super_data *sd = data;
	int ret;

	if (s->s_bdev != sd->bdev)
		return 0;

	if (!((s->s_flags | sd->flags) & MS_RDONLY))
		return 1; /* Reuse an old R/W-mode super_block */

	if (s->s_flags & sd->flags & MS_RDONLY) {
		if (down_read_trylock(&s->s_umount)) {
			ret = s->s_root &&
				(sd->cno == NILFS_SB(s)->s_snapshot_cno);
			up_read(&s->s_umount);
			/*
			 * This path is locked with sb_lock by sget().
			 * So, drop_super() causes deadlock.
			 */
			return ret;
		}
	}
	return 0;
}

static int
nilfs_get_sb(struct file_system_type *fs_type, int flags,
	     const char *dev_name, void *data, struct vfsmount *mnt)
{
	struct nilfs_super_data sd;
	struct super_block *s, *s2;
	struct the_nilfs *nilfs = NULL;
	int err, need_to_close = 1;

	sd.bdev = open_bdev_exclusive(dev_name, flags, fs_type);
	if (IS_ERR(sd.bdev))
		return PTR_ERR(sd.bdev);

	/*
	 * To get mount instance using sget() vfs-routine, NILFS needs
	 * much more information than normal filesystems to identify mount
	 * instance.  For snapshot mounts, not only a mount type (ro-mount
	 * or rw-mount) but also a checkpoint number is required.
	 * The results are passed in sget() using nilfs_super_data.
	 */
	sd.cno = 0;
	sd.flags = flags;
	if (nilfs_identify((char *)data, &sd)) {
		err = -EINVAL;
		goto failed;
	}

	/*
	 * once the super is inserted into the list by sget, s_umount
	 * will protect the lockfs code from trying to start a snapshot
	 * while we are mounting
	 */
	down(&sd.bdev->bd_mount_sem);
	if (!sd.cno &&
	    (err = test_exclusive_mount(fs_type, sd.bdev, flags ^ MS_RDONLY))) {
		err = (err < 0) ? : -EBUSY;
		goto failed_unlock;
	}

	/*
	 * Phase-1: search any existent instance and get the_nilfs
	 */
	s = sget(fs_type, nilfs_test_bdev_super, nilfs_set_bdev_super, &sd);
	if (IS_ERR(s))
		goto error_s;

	if (!s->s_root) {
		err = -ENOMEM;
		nilfs = alloc_nilfs(sd.bdev);
		if (!nilfs)
			goto cancel_new;
	} else {
		struct nilfs_sb_info *sbi = NILFS_SB(s);

		/*
		 * s_umount protects super_block from unmount process;
		 * It covers pointers of nilfs_sb_info and the_nilfs.
		 */
		nilfs = sbi->s_nilfs;
		get_nilfs(nilfs);
		up_write(&s->s_umount);

		/*
		 * Phase-2: search specified snapshot or R/W mode super_block
		 */
		if (!sd.cno)
			/* trying to get the latest checkpoint.  */
			sd.cno = nilfs_last_cno(nilfs);

		s2 = sget(fs_type, nilfs_test_bdev_super2,
			  nilfs_set_bdev_super, &sd);
		deactivate_super(s);
		/*
		 * Although deactivate_super() invokes close_bdev_exclusive() at
		 * kill_block_super().  Here, s is an existent mount; we need
		 * one more close_bdev_exclusive() call.
		 */
		s = s2;
		if (IS_ERR(s))
			goto error_s;
	}

	if (!s->s_root) {
		char b[BDEVNAME_SIZE];

		s->s_flags = flags;
		strlcpy(s->s_id, bdevname(sd.bdev, b), sizeof(s->s_id));
		sb_set_blocksize(s, block_size(sd.bdev));

		err = nilfs_fill_super(s, data, flags & MS_VERBOSE, nilfs);
		if (err)
			goto cancel_new;

		s->s_flags |= MS_ACTIVE;
		need_to_close = 0;
	} else if (!(s->s_flags & MS_RDONLY)) {
		err = -EBUSY;
	}

	up(&sd.bdev->bd_mount_sem);
	put_nilfs(nilfs);
	if (need_to_close)
		close_bdev_exclusive(sd.bdev, flags);
	simple_set_mnt(mnt, s);
	return 0;

 error_s:
	up(&sd.bdev->bd_mount_sem);
	if (nilfs)
		put_nilfs(nilfs);
	close_bdev_exclusive(sd.bdev, flags);
	return PTR_ERR(s);

 failed_unlock:
	up(&sd.bdev->bd_mount_sem);
 failed:
	close_bdev_exclusive(sd.bdev, flags);

	return err;

 cancel_new:
	/* Abandoning the newly allocated superblock */
	up(&sd.bdev->bd_mount_sem);
	if (nilfs)
		put_nilfs(nilfs);
	up_write(&s->s_umount);
	deactivate_super(s);
	/*
	 * deactivate_super() invokes close_bdev_exclusive().
	 * We must finish all post-cleaning before this call;
	 * put_nilfs() and unlocking bd_mount_sem need the block device.
	 */
	return err;
}

static int nilfs_test_bdev_super3(struct super_block *s, void *data)
{
	struct nilfs_super_data *sd = data;
	int ret;

	if (s->s_bdev != sd->bdev)
		return 0;
	if (down_read_trylock(&s->s_umount)) {
		ret = (s->s_flags & MS_RDONLY) && s->s_root &&
			nilfs_test_opt(NILFS_SB(s), SNAPSHOT);
		up_read(&s->s_umount);
		if (ret)
			return 0; /* ignore snapshot mounts */
	}
	return !((sd->flags ^ s->s_flags) & MS_RDONLY);
}

static int __false_bdev_super(struct super_block *s, void *data)
{
#if 0 /* XXX: workaround for lock debug. This is not good idea */
	up_write(&s->s_umount);
#endif
	return -EFAULT;
}

/**
 * test_exclusive_mount - check whether an exclusive RW/RO mount exists or not.
 * fs_type: filesystem type
 * bdev: block device
 * flag: 0 (check rw-mount) or MS_RDONLY (check ro-mount)
 * res: pointer to an integer to store result
 *
 * This function must be called within a section protected by bd_mount_mutex.
 */
static int test_exclusive_mount(struct file_system_type *fs_type,
				struct block_device *bdev, int flags)
{
	struct super_block *s;
	struct nilfs_super_data sd = { .flags = flags, .bdev = bdev };

	s = sget(fs_type, nilfs_test_bdev_super3, __false_bdev_super, &sd);
	if (IS_ERR(s)) {
		if (PTR_ERR(s) != -EFAULT)
			return PTR_ERR(s);
		return 0;  /* Not found */
	}
	up_write(&s->s_umount);
	deactivate_super(s);
	return 1;  /* Found */
}

struct file_system_type nilfs_fs_type = {
	.owner    = THIS_MODULE,
	.name     = "nilfs2",
	.get_sb   = nilfs_get_sb,
	.kill_sb  = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV,
};

static int __init init_nilfs_fs(void)
{
	int err;

	err = nilfs_init_inode_cache();
	if (err)
		goto failed;

	err = nilfs_init_transaction_cache();
	if (err)
		goto failed_inode_cache;

	err = nilfs_init_segbuf_cache();
	if (err)
		goto failed_transaction_cache;

	err = nilfs_btree_path_cache_init();
	if (err)
		goto failed_segbuf_cache;

	err = register_filesystem(&nilfs_fs_type);
	if (err)
		goto failed_btree_path_cache;

	return 0;

 failed_btree_path_cache:
	nilfs_btree_path_cache_destroy();

 failed_segbuf_cache:
	nilfs_destroy_segbuf_cache();

 failed_transaction_cache:
	nilfs_destroy_transaction_cache();

 failed_inode_cache:
	nilfs_destroy_inode_cache();

 failed:
	return err;
}

static void __exit exit_nilfs_fs(void)
{
	nilfs_destroy_segbuf_cache();
	nilfs_destroy_transaction_cache();
	nilfs_destroy_inode_cache();
	nilfs_btree_path_cache_destroy();
	unregister_filesystem(&nilfs_fs_type);
}

module_init(init_nilfs_fs)
module_exit(exit_nilfs_fs)
