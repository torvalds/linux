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
#include <linux/seq_file.h>
#include <linux/mount.h>
#include "nilfs.h"
#include "mdt.h"
#include "alloc.h"
#include "btree.h"
#include "btnode.h"
#include "page.h"
#include "cpfile.h"
#include "ifile.h"
#include "dat.h"
#include "segment.h"
#include "segbuf.h"

MODULE_AUTHOR("NTT Corp.");
MODULE_DESCRIPTION("A New Implementation of the Log-structured Filesystem "
		   "(NILFS)");
MODULE_LICENSE("GPL");

struct kmem_cache *nilfs_inode_cachep;
struct kmem_cache *nilfs_transaction_cachep;
struct kmem_cache *nilfs_segbuf_cachep;
struct kmem_cache *nilfs_btree_path_cache;

static int nilfs_remount(struct super_block *sb, int *flags, char *data);

static void nilfs_set_error(struct nilfs_sb_info *sbi)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	struct nilfs_super_block **sbp;

	down_write(&nilfs->ns_sem);
	if (!(nilfs->ns_mount_state & NILFS_ERROR_FS)) {
		nilfs->ns_mount_state |= NILFS_ERROR_FS;
		sbp = nilfs_prepare_super(sbi, 0);
		if (likely(sbp)) {
			sbp[0]->s_state |= cpu_to_le16(NILFS_ERROR_FS);
			if (sbp[1])
				sbp[1]->s_state |= cpu_to_le16(NILFS_ERROR_FS);
			nilfs_commit_super(sbi, NILFS_SB_COMMIT_ALL);
		}
	}
	up_write(&nilfs->ns_sem);
}

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
		nilfs_set_error(sbi);

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


struct inode *nilfs_alloc_inode_common(struct the_nilfs *nilfs)
{
	struct nilfs_inode_info *ii;

	ii = kmem_cache_alloc(nilfs_inode_cachep, GFP_NOFS);
	if (!ii)
		return NULL;
	ii->i_bh = NULL;
	ii->i_state = 0;
	ii->vfs_inode.i_version = 1;
	nilfs_btnode_cache_init(&ii->i_btnode_cache, nilfs->ns_bdi);
	return &ii->vfs_inode;
}

struct inode *nilfs_alloc_inode(struct super_block *sb)
{
	return nilfs_alloc_inode_common(NILFS_SB(sb)->s_nilfs);
}

void nilfs_destroy_inode(struct inode *inode)
{
	kmem_cache_free(nilfs_inode_cachep, NILFS_I(inode));
}

static int nilfs_sync_super(struct nilfs_sb_info *sbi, int flag)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	int err;

 retry:
	set_buffer_dirty(nilfs->ns_sbh[0]);

	if (nilfs_test_opt(sbi, BARRIER)) {
		err = __sync_dirty_buffer(nilfs->ns_sbh[0],
					  WRITE_SYNC | WRITE_BARRIER);
		if (err == -EOPNOTSUPP) {
			nilfs_warning(sbi->s_super, __func__,
				      "barrier-based sync failed. "
				      "disabling barriers\n");
			nilfs_clear_opt(sbi, BARRIER);
			goto retry;
		}
	} else {
		err = sync_dirty_buffer(nilfs->ns_sbh[0]);
	}

	if (unlikely(err)) {
		printk(KERN_ERR
		       "NILFS: unable to write superblock (err=%d)\n", err);
		if (err == -EIO && nilfs->ns_sbh[1]) {
			/*
			 * sbp[0] points to newer log than sbp[1],
			 * so copy sbp[0] to sbp[1] to take over sbp[0].
			 */
			memcpy(nilfs->ns_sbp[1], nilfs->ns_sbp[0],
			       nilfs->ns_sbsize);
			nilfs_fall_back_super_block(nilfs);
			goto retry;
		}
	} else {
		struct nilfs_super_block *sbp = nilfs->ns_sbp[0];

		nilfs->ns_sbwcount++;

		/*
		 * The latest segment becomes trailable from the position
		 * written in superblock.
		 */
		clear_nilfs_discontinued(nilfs);

		/* update GC protection for recent segments */
		if (nilfs->ns_sbh[1]) {
			if (flag == NILFS_SB_COMMIT_ALL) {
				set_buffer_dirty(nilfs->ns_sbh[1]);
				if (sync_dirty_buffer(nilfs->ns_sbh[1]) < 0)
					goto out;
			}
			if (le64_to_cpu(nilfs->ns_sbp[1]->s_last_cno) <
			    le64_to_cpu(nilfs->ns_sbp[0]->s_last_cno))
				sbp = nilfs->ns_sbp[1];
		}

		spin_lock(&nilfs->ns_last_segment_lock);
		nilfs->ns_prot_seq = le64_to_cpu(sbp->s_last_seq);
		spin_unlock(&nilfs->ns_last_segment_lock);
	}
 out:
	return err;
}

void nilfs_set_log_cursor(struct nilfs_super_block *sbp,
			  struct the_nilfs *nilfs)
{
	sector_t nfreeblocks;

	/* nilfs->ns_sem must be locked by the caller. */
	nilfs_count_free_blocks(nilfs, &nfreeblocks);
	sbp->s_free_blocks_count = cpu_to_le64(nfreeblocks);

	spin_lock(&nilfs->ns_last_segment_lock);
	sbp->s_last_seq = cpu_to_le64(nilfs->ns_last_seq);
	sbp->s_last_pseg = cpu_to_le64(nilfs->ns_last_pseg);
	sbp->s_last_cno = cpu_to_le64(nilfs->ns_last_cno);
	spin_unlock(&nilfs->ns_last_segment_lock);
}

struct nilfs_super_block **nilfs_prepare_super(struct nilfs_sb_info *sbi,
					       int flip)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	struct nilfs_super_block **sbp = nilfs->ns_sbp;

	/* nilfs->ns_sem must be locked by the caller. */
	if (sbp[0]->s_magic != cpu_to_le16(NILFS_SUPER_MAGIC)) {
		if (sbp[1] &&
		    sbp[1]->s_magic == cpu_to_le16(NILFS_SUPER_MAGIC)) {
			memcpy(sbp[0], sbp[1], nilfs->ns_sbsize);
		} else {
			printk(KERN_CRIT "NILFS: superblock broke on dev %s\n",
			       sbi->s_super->s_id);
			return NULL;
		}
	} else if (sbp[1] &&
		   sbp[1]->s_magic != cpu_to_le16(NILFS_SUPER_MAGIC)) {
			memcpy(sbp[1], sbp[0], nilfs->ns_sbsize);
	}

	if (flip && sbp[1])
		nilfs_swap_super_block(nilfs);

	return sbp;
}

int nilfs_commit_super(struct nilfs_sb_info *sbi, int flag)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	struct nilfs_super_block **sbp = nilfs->ns_sbp;
	time_t t;

	/* nilfs->ns_sem must be locked by the caller. */
	t = get_seconds();
	nilfs->ns_sbwtime = t;
	sbp[0]->s_wtime = cpu_to_le64(t);
	sbp[0]->s_sum = 0;
	sbp[0]->s_sum = cpu_to_le32(crc32_le(nilfs->ns_crc_seed,
					     (unsigned char *)sbp[0],
					     nilfs->ns_sbsize));
	if (flag == NILFS_SB_COMMIT_ALL && sbp[1]) {
		sbp[1]->s_wtime = sbp[0]->s_wtime;
		sbp[1]->s_sum = 0;
		sbp[1]->s_sum = cpu_to_le32(crc32_le(nilfs->ns_crc_seed,
					    (unsigned char *)sbp[1],
					    nilfs->ns_sbsize));
	}
	clear_nilfs_sb_dirty(nilfs);
	return nilfs_sync_super(sbi, flag);
}

/**
 * nilfs_cleanup_super() - write filesystem state for cleanup
 * @sbi: nilfs_sb_info to be unmounted or degraded to read-only
 *
 * This function restores state flags in the on-disk super block.
 * This will set "clean" flag (i.e. NILFS_VALID_FS) unless the
 * filesystem was not clean previously.
 */
int nilfs_cleanup_super(struct nilfs_sb_info *sbi)
{
	struct nilfs_super_block **sbp;
	int flag = NILFS_SB_COMMIT;
	int ret = -EIO;

	sbp = nilfs_prepare_super(sbi, 0);
	if (sbp) {
		sbp[0]->s_state = cpu_to_le16(sbi->s_nilfs->ns_mount_state);
		nilfs_set_log_cursor(sbp[0], sbi->s_nilfs);
		if (sbp[1] && sbp[0]->s_last_cno == sbp[1]->s_last_cno) {
			/*
			 * make the "clean" flag also to the opposite
			 * super block if both super blocks point to
			 * the same checkpoint.
			 */
			sbp[1]->s_state = sbp[0]->s_state;
			flag = NILFS_SB_COMMIT_ALL;
		}
		ret = nilfs_commit_super(sbi, flag);
	}
	return ret;
}

static void nilfs_put_super(struct super_block *sb)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	struct the_nilfs *nilfs = sbi->s_nilfs;

	lock_kernel();

	nilfs_detach_segment_constructor(sbi);

	if (!(sb->s_flags & MS_RDONLY)) {
		down_write(&nilfs->ns_sem);
		nilfs_cleanup_super(sbi);
		up_write(&nilfs->ns_sem);
	}
	down_write(&nilfs->ns_super_sem);
	if (nilfs->ns_current == sbi)
		nilfs->ns_current = NULL;
	up_write(&nilfs->ns_super_sem);

	nilfs_detach_checkpoint(sbi);
	put_nilfs(sbi->s_nilfs);
	sbi->s_super = NULL;
	sb->s_fs_info = NULL;
	nilfs_put_sbinfo(sbi);

	unlock_kernel();
}

static int nilfs_sync_fs(struct super_block *sb, int wait)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	struct the_nilfs *nilfs = sbi->s_nilfs;
	struct nilfs_super_block **sbp;
	int err = 0;

	/* This function is called when super block should be written back */
	if (wait)
		err = nilfs_construct_segment(sb);

	down_write(&nilfs->ns_sem);
	if (nilfs_sb_dirty(nilfs)) {
		sbp = nilfs_prepare_super(sbi, nilfs_sb_will_flip(nilfs));
		if (likely(sbp)) {
			nilfs_set_log_cursor(sbp[0], nilfs);
			nilfs_commit_super(sbi, NILFS_SB_COMMIT);
		}
	}
	up_write(&nilfs->ns_sem);

	return err;
}

int nilfs_attach_checkpoint(struct nilfs_sb_info *sbi, __u64 cno)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	struct nilfs_checkpoint *raw_cp;
	struct buffer_head *bh_cp;
	int err;

	down_write(&nilfs->ns_super_sem);
	list_add(&sbi->s_list, &nilfs->ns_supers);
	up_write(&nilfs->ns_super_sem);

	err = -ENOMEM;
	sbi->s_ifile = nilfs_ifile_new(sbi, nilfs->ns_inode_size);
	if (!sbi->s_ifile)
		goto delist;

	down_read(&nilfs->ns_segctor_sem);
	err = nilfs_cpfile_get_checkpoint(nilfs->ns_cpfile, cno, 0, &raw_cp,
					  &bh_cp);
	up_read(&nilfs->ns_segctor_sem);
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

 delist:
	down_write(&nilfs->ns_super_sem);
	list_del_init(&sbi->s_list);
	up_write(&nilfs->ns_super_sem);

	return err;
}

void nilfs_detach_checkpoint(struct nilfs_sb_info *sbi)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;

	nilfs_mdt_destroy(sbi->s_ifile);
	sbi->s_ifile = NULL;
	down_write(&nilfs->ns_super_sem);
	list_del_init(&sbi->s_list);
	up_write(&nilfs->ns_super_sem);
}

static int nilfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	struct the_nilfs *nilfs = sbi->s_nilfs;
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);
	unsigned long long blocks;
	unsigned long overhead;
	unsigned long nrsvblocks;
	sector_t nfreeblocks;
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
	 * When distributing meta data blocks outside segment structure,
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
	buf->f_fsid.val[0] = (u32)id;
	buf->f_fsid.val[1] = (u32)(id >> 32);

	return 0;
}

static int nilfs_show_options(struct seq_file *seq, struct vfsmount *vfs)
{
	struct super_block *sb = vfs->mnt_sb;
	struct nilfs_sb_info *sbi = NILFS_SB(sb);

	if (!nilfs_test_opt(sbi, BARRIER))
		seq_puts(seq, ",nobarrier");
	if (nilfs_test_opt(sbi, SNAPSHOT))
		seq_printf(seq, ",cp=%llu",
			   (unsigned long long int)sbi->s_snapshot_cno);
	if (nilfs_test_opt(sbi, ERRORS_PANIC))
		seq_puts(seq, ",errors=panic");
	if (nilfs_test_opt(sbi, ERRORS_CONT))
		seq_puts(seq, ",errors=continue");
	if (nilfs_test_opt(sbi, STRICT_ORDER))
		seq_puts(seq, ",order=strict");
	if (nilfs_test_opt(sbi, NORECOVERY))
		seq_puts(seq, ",norecovery");
	if (nilfs_test_opt(sbi, DISCARD))
		seq_puts(seq, ",discard");

	return 0;
}

static const struct super_operations nilfs_sops = {
	.alloc_inode    = nilfs_alloc_inode,
	.destroy_inode  = nilfs_destroy_inode,
	.dirty_inode    = nilfs_dirty_inode,
	/* .write_inode    = nilfs_write_inode, */
	/* .put_inode      = nilfs_put_inode, */
	/* .drop_inode	  = nilfs_drop_inode, */
	.evict_inode    = nilfs_evict_inode,
	.put_super      = nilfs_put_super,
	/* .write_super    = nilfs_write_super, */
	.sync_fs        = nilfs_sync_fs,
	/* .write_super_lockfs */
	/* .unlockfs */
	.statfs         = nilfs_statfs,
	.remount_fs     = nilfs_remount,
	/* .umount_begin */
	.show_options = nilfs_show_options
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

static const struct export_operations nilfs_export_ops = {
	.fh_to_dentry = nilfs_fh_to_dentry,
	.fh_to_parent = nilfs_fh_to_parent,
	.get_parent = nilfs_get_parent,
};

enum {
	Opt_err_cont, Opt_err_panic, Opt_err_ro,
	Opt_barrier, Opt_nobarrier, Opt_snapshot, Opt_order, Opt_norecovery,
	Opt_discard, Opt_nodiscard, Opt_err,
};

static match_table_t tokens = {
	{Opt_err_cont, "errors=continue"},
	{Opt_err_panic, "errors=panic"},
	{Opt_err_ro, "errors=remount-ro"},
	{Opt_barrier, "barrier"},
	{Opt_nobarrier, "nobarrier"},
	{Opt_snapshot, "cp=%u"},
	{Opt_order, "order=%s"},
	{Opt_norecovery, "norecovery"},
	{Opt_discard, "discard"},
	{Opt_nodiscard, "nodiscard"},
	{Opt_err, NULL}
};

static int parse_options(char *options, struct super_block *sb, int is_remount)
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
			nilfs_set_opt(sbi, BARRIER);
			break;
		case Opt_nobarrier:
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
			if (is_remount) {
				if (!nilfs_test_opt(sbi, SNAPSHOT)) {
					printk(KERN_ERR
					       "NILFS: cannot change regular "
					       "mount to snapshot.\n");
					return 0;
				} else if (option != sbi->s_snapshot_cno) {
					printk(KERN_ERR
					       "NILFS: cannot remount to a "
					       "different snapshot.\n");
					return 0;
				}
				break;
			}
			if (!(sb->s_flags & MS_RDONLY)) {
				printk(KERN_ERR "NILFS: cannot mount snapshot "
				       "read/write.  A read-only option is "
				       "required.\n");
				return 0;
			}
			sbi->s_snapshot_cno = option;
			nilfs_set_opt(sbi, SNAPSHOT);
			break;
		case Opt_norecovery:
			nilfs_set_opt(sbi, NORECOVERY);
			break;
		case Opt_discard:
			nilfs_set_opt(sbi, DISCARD);
			break;
		case Opt_nodiscard:
			nilfs_clear_opt(sbi, DISCARD);
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
		NILFS_MOUNT_ERRORS_RO | NILFS_MOUNT_BARRIER;
}

static int nilfs_setup_super(struct nilfs_sb_info *sbi)
{
	struct the_nilfs *nilfs = sbi->s_nilfs;
	struct nilfs_super_block **sbp;
	int max_mnt_count;
	int mnt_count;

	/* nilfs->ns_sem must be locked by the caller. */
	sbp = nilfs_prepare_super(sbi, 0);
	if (!sbp)
		return -EIO;

	max_mnt_count = le16_to_cpu(sbp[0]->s_max_mnt_count);
	mnt_count = le16_to_cpu(sbp[0]->s_mnt_count);

	if (nilfs->ns_mount_state & NILFS_ERROR_FS) {
		printk(KERN_WARNING
		       "NILFS warning: mounting fs with errors\n");
#if 0
	} else if (max_mnt_count >= 0 && mnt_count >= max_mnt_count) {
		printk(KERN_WARNING
		       "NILFS warning: maximal mount count reached\n");
#endif
	}
	if (!max_mnt_count)
		sbp[0]->s_max_mnt_count = cpu_to_le16(NILFS_DFL_MAX_MNT_COUNT);

	sbp[0]->s_mnt_count = cpu_to_le16(mnt_count + 1);
	sbp[0]->s_state =
		cpu_to_le16(le16_to_cpu(sbp[0]->s_state) & ~NILFS_VALID_FS);
	sbp[0]->s_mtime = cpu_to_le64(get_seconds());
	/* synchronize sbp[1] with sbp[0] */
	memcpy(sbp[1], sbp[0], nilfs->ns_sbsize);
	return nilfs_commit_super(sbi, NILFS_SB_COMMIT_ALL);
}

struct nilfs_super_block *nilfs_read_super_block(struct super_block *sb,
						 u64 pos, int blocksize,
						 struct buffer_head **pbh)
{
	unsigned long long sb_index = pos;
	unsigned long offset;

	offset = do_div(sb_index, blocksize);
	*pbh = sb_bread(sb, sb_index);
	if (!*pbh)
		return NULL;
	return (struct nilfs_super_block *)((char *)(*pbh)->b_data + offset);
}

int nilfs_store_magic_and_option(struct super_block *sb,
				 struct nilfs_super_block *sbp,
				 char *data)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);

	sb->s_magic = le16_to_cpu(sbp->s_magic);

	/* FS independent flags */
#ifdef NILFS_ATIME_DISABLE
	sb->s_flags |= MS_NOATIME;
#endif

	nilfs_set_default_options(sbi, sbp);

	sbi->s_resuid = le16_to_cpu(sbp->s_def_resuid);
	sbi->s_resgid = le16_to_cpu(sbp->s_def_resgid);
	sbi->s_interval = le32_to_cpu(sbp->s_c_interval);
	sbi->s_watermark = le32_to_cpu(sbp->s_c_block_max);

	return !parse_options(data, sb, 0) ? -EINVAL : 0 ;
}

int nilfs_check_feature_compatibility(struct super_block *sb,
				      struct nilfs_super_block *sbp)
{
	__u64 features;

	features = le64_to_cpu(sbp->s_feature_incompat) &
		~NILFS_FEATURE_INCOMPAT_SUPP;
	if (features) {
		printk(KERN_ERR "NILFS: couldn't mount because of unsupported "
		       "optional features (%llx)\n",
		       (unsigned long long)features);
		return -EINVAL;
	}
	features = le64_to_cpu(sbp->s_feature_compat_ro) &
		~NILFS_FEATURE_COMPAT_RO_SUPP;
	if (!(sb->s_flags & MS_RDONLY) && features) {
		printk(KERN_ERR "NILFS: couldn't mount RDWR because of "
		       "unsupported optional features (%llx)\n",
		       (unsigned long long)features);
		return -EINVAL;
	}
	return 0;
}

/**
 * nilfs_fill_super() - initialize a super block instance
 * @sb: super_block
 * @data: mount options
 * @silent: silent mode flag
 * @nilfs: the_nilfs struct
 *
 * This function is called exclusively by nilfs->ns_mount_mutex.
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
	atomic_set(&sbi->s_count, 1);

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
	sb->s_time_gran = 1;
	sb->s_bdi = nilfs->ns_bdi;

	err = load_nilfs(nilfs, sbi);
	if (err)
		goto failed_sbi;

	cno = nilfs_last_cno(nilfs);

	if (sb->s_flags & MS_RDONLY) {
		if (nilfs_test_opt(sbi, SNAPSHOT)) {
			down_read(&nilfs->ns_segctor_sem);
			err = nilfs_cpfile_is_snapshot(nilfs->ns_cpfile,
						       sbi->s_snapshot_cno);
			up_read(&nilfs->ns_segctor_sem);
			if (err < 0) {
				if (err == -ENOENT)
					err = -EINVAL;
				goto failed_sbi;
			}
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
		}
	}

	err = nilfs_attach_checkpoint(sbi, cno);
	if (err) {
		printk(KERN_ERR "NILFS: error loading a checkpoint"
		       " (checkpoint number=%llu).\n", (unsigned long long)cno);
		goto failed_sbi;
	}

	if (!(sb->s_flags & MS_RDONLY)) {
		err = nilfs_attach_segment_constructor(sbi);
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

	down_write(&nilfs->ns_super_sem);
	if (!nilfs_test_opt(sbi, SNAPSHOT))
		nilfs->ns_current = sbi;
	up_write(&nilfs->ns_super_sem);

	return 0;

 failed_segctor:
	nilfs_detach_segment_constructor(sbi);

 failed_checkpoint:
	nilfs_detach_checkpoint(sbi);

 failed_sbi:
	put_nilfs(nilfs);
	sb->s_fs_info = NULL;
	nilfs_put_sbinfo(sbi);
	return err;
}

static int nilfs_remount(struct super_block *sb, int *flags, char *data)
{
	struct nilfs_sb_info *sbi = NILFS_SB(sb);
	struct the_nilfs *nilfs = sbi->s_nilfs;
	unsigned long old_sb_flags;
	struct nilfs_mount_options old_opts;
	int was_snapshot, err;

	lock_kernel();

	down_write(&nilfs->ns_super_sem);
	old_sb_flags = sb->s_flags;
	old_opts.mount_opt = sbi->s_mount_opt;
	old_opts.snapshot_cno = sbi->s_snapshot_cno;
	was_snapshot = nilfs_test_opt(sbi, SNAPSHOT);

	if (!parse_options(data, sb, 1)) {
		err = -EINVAL;
		goto restore_opts;
	}
	sb->s_flags = (sb->s_flags & ~MS_POSIXACL);

	err = -EINVAL;
	if (was_snapshot && !(*flags & MS_RDONLY)) {
		printk(KERN_ERR "NILFS (device %s): cannot remount snapshot "
		       "read/write.\n", sb->s_id);
		goto restore_opts;
	}

	if (!nilfs_valid_fs(nilfs)) {
		printk(KERN_WARNING "NILFS (device %s): couldn't "
		       "remount because the filesystem is in an "
		       "incomplete recovery state.\n", sb->s_id);
		goto restore_opts;
	}

	if ((*flags & MS_RDONLY) == (sb->s_flags & MS_RDONLY))
		goto out;
	if (*flags & MS_RDONLY) {
		/* Shutting down the segment constructor */
		nilfs_detach_segment_constructor(sbi);
		sb->s_flags |= MS_RDONLY;

		/*
		 * Remounting a valid RW partition RDONLY, so set
		 * the RDONLY flag and then mark the partition as valid again.
		 */
		down_write(&nilfs->ns_sem);
		nilfs_cleanup_super(sbi);
		up_write(&nilfs->ns_sem);
	} else {
		__u64 features;

		/*
		 * Mounting a RDONLY partition read-write, so reread and
		 * store the current valid flag.  (It may have been changed
		 * by fsck since we originally mounted the partition.)
		 */
		down_read(&nilfs->ns_sem);
		features = le64_to_cpu(nilfs->ns_sbp[0]->s_feature_compat_ro) &
			~NILFS_FEATURE_COMPAT_RO_SUPP;
		up_read(&nilfs->ns_sem);
		if (features) {
			printk(KERN_WARNING "NILFS (device %s): couldn't "
			       "remount RDWR because of unsupported optional "
			       "features (%llx)\n",
			       sb->s_id, (unsigned long long)features);
			err = -EROFS;
			goto restore_opts;
		}

		sb->s_flags &= ~MS_RDONLY;

		err = nilfs_attach_segment_constructor(sbi);
		if (err)
			goto restore_opts;

		down_write(&nilfs->ns_sem);
		nilfs_setup_super(sbi);
		up_write(&nilfs->ns_sem);
	}
 out:
	up_write(&nilfs->ns_super_sem);
	unlock_kernel();
	return 0;

 restore_opts:
	sb->s_flags = old_sb_flags;
	sbi->s_mount_opt = old_opts.mount_opt;
	sbi->s_snapshot_cno = old_opts.snapshot_cno;
	up_write(&nilfs->ns_super_sem);
	unlock_kernel();
	return err;
}

struct nilfs_super_data {
	struct block_device *bdev;
	struct nilfs_sb_info *sbi;
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

	return sd->sbi && s->s_fs_info == (void *)sd->sbi;
}

static int
nilfs_get_sb(struct file_system_type *fs_type, int flags,
	     const char *dev_name, void *data, struct vfsmount *mnt)
{
	struct nilfs_super_data sd;
	struct super_block *s;
	fmode_t mode = FMODE_READ;
	struct the_nilfs *nilfs;
	int err, need_to_close = 1;

	if (!(flags & MS_RDONLY))
		mode |= FMODE_WRITE;

	sd.bdev = open_bdev_exclusive(dev_name, mode, fs_type);
	if (IS_ERR(sd.bdev))
		return PTR_ERR(sd.bdev);

	/*
	 * To get mount instance using sget() vfs-routine, NILFS needs
	 * much more information than normal filesystems to identify mount
	 * instance.  For snapshot mounts, not only a mount type (ro-mount
	 * or rw-mount) but also a checkpoint number is required.
	 */
	sd.cno = 0;
	sd.flags = flags;
	if (nilfs_identify((char *)data, &sd)) {
		err = -EINVAL;
		goto failed;
	}

	nilfs = find_or_create_nilfs(sd.bdev);
	if (!nilfs) {
		err = -ENOMEM;
		goto failed;
	}

	mutex_lock(&nilfs->ns_mount_mutex);

	if (!sd.cno) {
		/*
		 * Check if an exclusive mount exists or not.
		 * Snapshot mounts coexist with a current mount
		 * (i.e. rw-mount or ro-mount), whereas rw-mount and
		 * ro-mount are mutually exclusive.
		 */
		down_read(&nilfs->ns_super_sem);
		if (nilfs->ns_current &&
		    ((nilfs->ns_current->s_super->s_flags ^ flags)
		     & MS_RDONLY)) {
			up_read(&nilfs->ns_super_sem);
			err = -EBUSY;
			goto failed_unlock;
		}
		up_read(&nilfs->ns_super_sem);
	}

	/*
	 * Find existing nilfs_sb_info struct
	 */
	sd.sbi = nilfs_find_sbinfo(nilfs, !(flags & MS_RDONLY), sd.cno);

	/*
	 * Get super block instance holding the nilfs_sb_info struct.
	 * A new instance is allocated if no existing mount is present or
	 * existing instance has been unmounted.
	 */
	s = sget(fs_type, nilfs_test_bdev_super, nilfs_set_bdev_super, &sd);
	if (sd.sbi)
		nilfs_put_sbinfo(sd.sbi);

	if (IS_ERR(s)) {
		err = PTR_ERR(s);
		goto failed_unlock;
	}

	if (!s->s_root) {
		char b[BDEVNAME_SIZE];

		/* New superblock instance created */
		s->s_flags = flags;
		s->s_mode = mode;
		strlcpy(s->s_id, bdevname(sd.bdev, b), sizeof(s->s_id));
		sb_set_blocksize(s, block_size(sd.bdev));

		err = nilfs_fill_super(s, data, flags & MS_SILENT ? 1 : 0,
				       nilfs);
		if (err)
			goto cancel_new;

		s->s_flags |= MS_ACTIVE;
		need_to_close = 0;
	}

	mutex_unlock(&nilfs->ns_mount_mutex);
	put_nilfs(nilfs);
	if (need_to_close)
		close_bdev_exclusive(sd.bdev, mode);
	simple_set_mnt(mnt, s);
	return 0;

 failed_unlock:
	mutex_unlock(&nilfs->ns_mount_mutex);
	put_nilfs(nilfs);
 failed:
	close_bdev_exclusive(sd.bdev, mode);

	return err;

 cancel_new:
	/* Abandoning the newly allocated superblock */
	mutex_unlock(&nilfs->ns_mount_mutex);
	put_nilfs(nilfs);
	deactivate_locked_super(s);
	/*
	 * deactivate_locked_super() invokes close_bdev_exclusive().
	 * We must finish all post-cleaning before this call;
	 * put_nilfs() needs the block device.
	 */
	return err;
}

struct file_system_type nilfs_fs_type = {
	.owner    = THIS_MODULE,
	.name     = "nilfs2",
	.get_sb   = nilfs_get_sb,
	.kill_sb  = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV,
};

static void nilfs_inode_init_once(void *obj)
{
	struct nilfs_inode_info *ii = obj;

	INIT_LIST_HEAD(&ii->i_dirty);
#ifdef CONFIG_NILFS_XATTR
	init_rwsem(&ii->xattr_sem);
#endif
	nilfs_btnode_cache_init_once(&ii->i_btnode_cache);
	ii->i_bmap = &ii->i_bmap_data;
	inode_init_once(&ii->vfs_inode);
}

static void nilfs_segbuf_init_once(void *obj)
{
	memset(obj, 0, sizeof(struct nilfs_segment_buffer));
}

static void nilfs_destroy_cachep(void)
{
	if (nilfs_inode_cachep)
		kmem_cache_destroy(nilfs_inode_cachep);
	if (nilfs_transaction_cachep)
		kmem_cache_destroy(nilfs_transaction_cachep);
	if (nilfs_segbuf_cachep)
		kmem_cache_destroy(nilfs_segbuf_cachep);
	if (nilfs_btree_path_cache)
		kmem_cache_destroy(nilfs_btree_path_cache);
}

static int __init nilfs_init_cachep(void)
{
	nilfs_inode_cachep = kmem_cache_create("nilfs2_inode_cache",
			sizeof(struct nilfs_inode_info), 0,
			SLAB_RECLAIM_ACCOUNT, nilfs_inode_init_once);
	if (!nilfs_inode_cachep)
		goto fail;

	nilfs_transaction_cachep = kmem_cache_create("nilfs2_transaction_cache",
			sizeof(struct nilfs_transaction_info), 0,
			SLAB_RECLAIM_ACCOUNT, NULL);
	if (!nilfs_transaction_cachep)
		goto fail;

	nilfs_segbuf_cachep = kmem_cache_create("nilfs2_segbuf_cache",
			sizeof(struct nilfs_segment_buffer), 0,
			SLAB_RECLAIM_ACCOUNT, nilfs_segbuf_init_once);
	if (!nilfs_segbuf_cachep)
		goto fail;

	nilfs_btree_path_cache = kmem_cache_create("nilfs2_btree_path_cache",
			sizeof(struct nilfs_btree_path) * NILFS_BTREE_LEVEL_MAX,
			0, 0, NULL);
	if (!nilfs_btree_path_cache)
		goto fail;

	return 0;

fail:
	nilfs_destroy_cachep();
	return -ENOMEM;
}

static int __init init_nilfs_fs(void)
{
	int err;

	err = nilfs_init_cachep();
	if (err)
		goto fail;

	err = register_filesystem(&nilfs_fs_type);
	if (err)
		goto free_cachep;

	printk(KERN_INFO "NILFS version 2 loaded\n");
	return 0;

free_cachep:
	nilfs_destroy_cachep();
fail:
	return err;
}

static void __exit exit_nilfs_fs(void)
{
	nilfs_destroy_cachep();
	unregister_filesystem(&nilfs_fs_type);
}

module_init(init_nilfs_fs)
module_exit(exit_nilfs_fs)
