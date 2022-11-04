// SPDX-License-Identifier: GPL-2.0+
/*
 * super.c - NILFS module and super block management.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * Written by Ryusuke Konishi.
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
#include <linux/crc32.h>
#include <linux/vfs.h>
#include <linux/writeback.h>
#include <linux/seq_file.h>
#include <linux/mount.h>
#include "nilfs.h"
#include "export.h"
#include "mdt.h"
#include "alloc.h"
#include "btree.h"
#include "btnode.h"
#include "page.h"
#include "cpfile.h"
#include "sufile.h" /* nilfs_sufile_resize(), nilfs_sufile_set_alloc_range() */
#include "ifile.h"
#include "dat.h"
#include "segment.h"
#include "segbuf.h"

MODULE_AUTHOR("NTT Corp.");
MODULE_DESCRIPTION("A New Implementation of the Log-structured Filesystem "
		   "(NILFS)");
MODULE_LICENSE("GPL");

static struct kmem_cache *nilfs_inode_cachep;
struct kmem_cache *nilfs_transaction_cachep;
struct kmem_cache *nilfs_segbuf_cachep;
struct kmem_cache *nilfs_btree_path_cache;

static int nilfs_setup_super(struct super_block *sb, int is_mount);
static int nilfs_remount(struct super_block *sb, int *flags, char *data);

void __nilfs_msg(struct super_block *sb, const char *fmt, ...)
{
	struct va_format vaf;
	va_list args;
	int level;

	va_start(args, fmt);

	level = printk_get_level(fmt);
	vaf.fmt = printk_skip_level(fmt);
	vaf.va = &args;

	if (sb)
		printk("%c%cNILFS (%s): %pV\n",
		       KERN_SOH_ASCII, level, sb->s_id, &vaf);
	else
		printk("%c%cNILFS: %pV\n",
		       KERN_SOH_ASCII, level, &vaf);

	va_end(args);
}

static void nilfs_set_error(struct super_block *sb)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_super_block **sbp;

	down_write(&nilfs->ns_sem);
	if (!(nilfs->ns_mount_state & NILFS_ERROR_FS)) {
		nilfs->ns_mount_state |= NILFS_ERROR_FS;
		sbp = nilfs_prepare_super(sb, 0);
		if (likely(sbp)) {
			sbp[0]->s_state |= cpu_to_le16(NILFS_ERROR_FS);
			if (sbp[1])
				sbp[1]->s_state |= cpu_to_le16(NILFS_ERROR_FS);
			nilfs_commit_super(sb, NILFS_SB_COMMIT_ALL);
		}
	}
	up_write(&nilfs->ns_sem);
}

/**
 * __nilfs_error() - report failure condition on a filesystem
 *
 * __nilfs_error() sets an ERROR_FS flag on the superblock as well as
 * reporting an error message.  This function should be called when
 * NILFS detects incoherences or defects of meta data on disk.
 *
 * This implements the body of nilfs_error() macro.  Normally,
 * nilfs_error() should be used.  As for sustainable errors such as a
 * single-shot I/O error, nilfs_err() should be used instead.
 *
 * Callers should not add a trailing newline since this will do it.
 */
void __nilfs_error(struct super_block *sb, const char *function,
		   const char *fmt, ...)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct va_format vaf;
	va_list args;

	va_start(args, fmt);

	vaf.fmt = fmt;
	vaf.va = &args;

	printk(KERN_CRIT "NILFS error (device %s): %s: %pV\n",
	       sb->s_id, function, &vaf);

	va_end(args);

	if (!sb_rdonly(sb)) {
		nilfs_set_error(sb);

		if (nilfs_test_opt(nilfs, ERRORS_RO)) {
			printk(KERN_CRIT "Remounting filesystem read-only\n");
			sb->s_flags |= SB_RDONLY;
		}
	}

	if (nilfs_test_opt(nilfs, ERRORS_PANIC))
		panic("NILFS (device %s): panic forced after error\n",
		      sb->s_id);
}

struct inode *nilfs_alloc_inode(struct super_block *sb)
{
	struct nilfs_inode_info *ii;

	ii = kmem_cache_alloc(nilfs_inode_cachep, GFP_NOFS);
	if (!ii)
		return NULL;
	ii->i_bh = NULL;
	ii->i_state = 0;
	ii->i_cno = 0;
	ii->i_assoc_inode = NULL;
	ii->i_bmap = &ii->i_bmap_data;
	return &ii->vfs_inode;
}

static void nilfs_free_inode(struct inode *inode)
{
	if (nilfs_is_metadata_file_inode(inode))
		nilfs_mdt_destroy(inode);

	kmem_cache_free(nilfs_inode_cachep, NILFS_I(inode));
}

static int nilfs_sync_super(struct super_block *sb, int flag)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	int err;

 retry:
	set_buffer_dirty(nilfs->ns_sbh[0]);
	if (nilfs_test_opt(nilfs, BARRIER)) {
		err = __sync_dirty_buffer(nilfs->ns_sbh[0],
					  REQ_SYNC | REQ_PREFLUSH | REQ_FUA);
	} else {
		err = sync_dirty_buffer(nilfs->ns_sbh[0]);
	}

	if (unlikely(err)) {
		nilfs_err(sb, "unable to write superblock: err=%d", err);
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

struct nilfs_super_block **nilfs_prepare_super(struct super_block *sb,
					       int flip)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_super_block **sbp = nilfs->ns_sbp;

	/* nilfs->ns_sem must be locked by the caller. */
	if (sbp[0]->s_magic != cpu_to_le16(NILFS_SUPER_MAGIC)) {
		if (sbp[1] &&
		    sbp[1]->s_magic == cpu_to_le16(NILFS_SUPER_MAGIC)) {
			memcpy(sbp[0], sbp[1], nilfs->ns_sbsize);
		} else {
			nilfs_crit(sb, "superblock broke");
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

int nilfs_commit_super(struct super_block *sb, int flag)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_super_block **sbp = nilfs->ns_sbp;
	time64_t t;

	/* nilfs->ns_sem must be locked by the caller. */
	t = ktime_get_real_seconds();
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
	nilfs->ns_flushed_device = 1;
	/* make sure store to ns_flushed_device cannot be reordered */
	smp_wmb();
	return nilfs_sync_super(sb, flag);
}

/**
 * nilfs_cleanup_super() - write filesystem state for cleanup
 * @sb: super block instance to be unmounted or degraded to read-only
 *
 * This function restores state flags in the on-disk super block.
 * This will set "clean" flag (i.e. NILFS_VALID_FS) unless the
 * filesystem was not clean previously.
 */
int nilfs_cleanup_super(struct super_block *sb)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_super_block **sbp;
	int flag = NILFS_SB_COMMIT;
	int ret = -EIO;

	sbp = nilfs_prepare_super(sb, 0);
	if (sbp) {
		sbp[0]->s_state = cpu_to_le16(nilfs->ns_mount_state);
		nilfs_set_log_cursor(sbp[0], nilfs);
		if (sbp[1] && sbp[0]->s_last_cno == sbp[1]->s_last_cno) {
			/*
			 * make the "clean" flag also to the opposite
			 * super block if both super blocks point to
			 * the same checkpoint.
			 */
			sbp[1]->s_state = sbp[0]->s_state;
			flag = NILFS_SB_COMMIT_ALL;
		}
		ret = nilfs_commit_super(sb, flag);
	}
	return ret;
}

/**
 * nilfs_move_2nd_super - relocate secondary super block
 * @sb: super block instance
 * @sb2off: new offset of the secondary super block (in bytes)
 */
static int nilfs_move_2nd_super(struct super_block *sb, loff_t sb2off)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct buffer_head *nsbh;
	struct nilfs_super_block *nsbp;
	sector_t blocknr, newblocknr;
	unsigned long offset;
	int sb2i;  /* array index of the secondary superblock */
	int ret = 0;

	/* nilfs->ns_sem must be locked by the caller. */
	if (nilfs->ns_sbh[1] &&
	    nilfs->ns_sbh[1]->b_blocknr > nilfs->ns_first_data_block) {
		sb2i = 1;
		blocknr = nilfs->ns_sbh[1]->b_blocknr;
	} else if (nilfs->ns_sbh[0]->b_blocknr > nilfs->ns_first_data_block) {
		sb2i = 0;
		blocknr = nilfs->ns_sbh[0]->b_blocknr;
	} else {
		sb2i = -1;
		blocknr = 0;
	}
	if (sb2i >= 0 && (u64)blocknr << nilfs->ns_blocksize_bits == sb2off)
		goto out;  /* super block location is unchanged */

	/* Get new super block buffer */
	newblocknr = sb2off >> nilfs->ns_blocksize_bits;
	offset = sb2off & (nilfs->ns_blocksize - 1);
	nsbh = sb_getblk(sb, newblocknr);
	if (!nsbh) {
		nilfs_warn(sb,
			   "unable to move secondary superblock to block %llu",
			   (unsigned long long)newblocknr);
		ret = -EIO;
		goto out;
	}
	nsbp = (void *)nsbh->b_data + offset;
	memset(nsbp, 0, nilfs->ns_blocksize);

	if (sb2i >= 0) {
		memcpy(nsbp, nilfs->ns_sbp[sb2i], nilfs->ns_sbsize);
		brelse(nilfs->ns_sbh[sb2i]);
		nilfs->ns_sbh[sb2i] = nsbh;
		nilfs->ns_sbp[sb2i] = nsbp;
	} else if (nilfs->ns_sbh[0]->b_blocknr < nilfs->ns_first_data_block) {
		/* secondary super block will be restored to index 1 */
		nilfs->ns_sbh[1] = nsbh;
		nilfs->ns_sbp[1] = nsbp;
	} else {
		brelse(nsbh);
	}
out:
	return ret;
}

/**
 * nilfs_resize_fs - resize the filesystem
 * @sb: super block instance
 * @newsize: new size of the filesystem (in bytes)
 */
int nilfs_resize_fs(struct super_block *sb, __u64 newsize)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_super_block **sbp;
	__u64 devsize, newnsegs;
	loff_t sb2off;
	int ret;

	ret = -ERANGE;
	devsize = i_size_read(sb->s_bdev->bd_inode);
	if (newsize > devsize)
		goto out;

	/*
	 * Write lock is required to protect some functions depending
	 * on the number of segments, the number of reserved segments,
	 * and so forth.
	 */
	down_write(&nilfs->ns_segctor_sem);

	sb2off = NILFS_SB2_OFFSET_BYTES(newsize);
	newnsegs = sb2off >> nilfs->ns_blocksize_bits;
	do_div(newnsegs, nilfs->ns_blocks_per_segment);

	ret = nilfs_sufile_resize(nilfs->ns_sufile, newnsegs);
	up_write(&nilfs->ns_segctor_sem);
	if (ret < 0)
		goto out;

	ret = nilfs_construct_segment(sb);
	if (ret < 0)
		goto out;

	down_write(&nilfs->ns_sem);
	nilfs_move_2nd_super(sb, sb2off);
	ret = -EIO;
	sbp = nilfs_prepare_super(sb, 0);
	if (likely(sbp)) {
		nilfs_set_log_cursor(sbp[0], nilfs);
		/*
		 * Drop NILFS_RESIZE_FS flag for compatibility with
		 * mount-time resize which may be implemented in a
		 * future release.
		 */
		sbp[0]->s_state = cpu_to_le16(le16_to_cpu(sbp[0]->s_state) &
					      ~NILFS_RESIZE_FS);
		sbp[0]->s_dev_size = cpu_to_le64(newsize);
		sbp[0]->s_nsegments = cpu_to_le64(nilfs->ns_nsegments);
		if (sbp[1])
			memcpy(sbp[1], sbp[0], nilfs->ns_sbsize);
		ret = nilfs_commit_super(sb, NILFS_SB_COMMIT_ALL);
	}
	up_write(&nilfs->ns_sem);

	/*
	 * Reset the range of allocatable segments last.  This order
	 * is important in the case of expansion because the secondary
	 * superblock must be protected from log write until migration
	 * completes.
	 */
	if (!ret)
		nilfs_sufile_set_alloc_range(nilfs->ns_sufile, 0, newnsegs - 1);
out:
	return ret;
}

static void nilfs_put_super(struct super_block *sb)
{
	struct the_nilfs *nilfs = sb->s_fs_info;

	nilfs_detach_log_writer(sb);

	if (!sb_rdonly(sb)) {
		down_write(&nilfs->ns_sem);
		nilfs_cleanup_super(sb);
		up_write(&nilfs->ns_sem);
	}

	iput(nilfs->ns_sufile);
	iput(nilfs->ns_cpfile);
	iput(nilfs->ns_dat);

	destroy_nilfs(nilfs);
	sb->s_fs_info = NULL;
}

static int nilfs_sync_fs(struct super_block *sb, int wait)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_super_block **sbp;
	int err = 0;

	/* This function is called when super block should be written back */
	if (wait)
		err = nilfs_construct_segment(sb);

	down_write(&nilfs->ns_sem);
	if (nilfs_sb_dirty(nilfs)) {
		sbp = nilfs_prepare_super(sb, nilfs_sb_will_flip(nilfs));
		if (likely(sbp)) {
			nilfs_set_log_cursor(sbp[0], nilfs);
			nilfs_commit_super(sb, NILFS_SB_COMMIT);
		}
	}
	up_write(&nilfs->ns_sem);

	if (!err)
		err = nilfs_flush_device(nilfs);

	return err;
}

int nilfs_attach_checkpoint(struct super_block *sb, __u64 cno, int curr_mnt,
			    struct nilfs_root **rootp)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_root *root;
	struct nilfs_checkpoint *raw_cp;
	struct buffer_head *bh_cp;
	int err = -ENOMEM;

	root = nilfs_find_or_create_root(
		nilfs, curr_mnt ? NILFS_CPTREE_CURRENT_CNO : cno);
	if (!root)
		return err;

	if (root->ifile)
		goto reuse; /* already attached checkpoint */

	down_read(&nilfs->ns_segctor_sem);
	err = nilfs_cpfile_get_checkpoint(nilfs->ns_cpfile, cno, 0, &raw_cp,
					  &bh_cp);
	up_read(&nilfs->ns_segctor_sem);
	if (unlikely(err)) {
		if (err == -ENOENT || err == -EINVAL) {
			nilfs_err(sb,
				  "Invalid checkpoint (checkpoint number=%llu)",
				  (unsigned long long)cno);
			err = -EINVAL;
		}
		goto failed;
	}

	err = nilfs_ifile_read(sb, root, nilfs->ns_inode_size,
			       &raw_cp->cp_ifile_inode, &root->ifile);
	if (err)
		goto failed_bh;

	atomic64_set(&root->inodes_count,
			le64_to_cpu(raw_cp->cp_inodes_count));
	atomic64_set(&root->blocks_count,
			le64_to_cpu(raw_cp->cp_blocks_count));

	nilfs_cpfile_put_checkpoint(nilfs->ns_cpfile, cno, bh_cp);

 reuse:
	*rootp = root;
	return 0;

 failed_bh:
	nilfs_cpfile_put_checkpoint(nilfs->ns_cpfile, cno, bh_cp);
 failed:
	nilfs_put_root(root);

	return err;
}

static int nilfs_freeze(struct super_block *sb)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	int err;

	if (sb_rdonly(sb))
		return 0;

	/* Mark super block clean */
	down_write(&nilfs->ns_sem);
	err = nilfs_cleanup_super(sb);
	up_write(&nilfs->ns_sem);
	return err;
}

static int nilfs_unfreeze(struct super_block *sb)
{
	struct the_nilfs *nilfs = sb->s_fs_info;

	if (sb_rdonly(sb))
		return 0;

	down_write(&nilfs->ns_sem);
	nilfs_setup_super(sb, false);
	up_write(&nilfs->ns_sem);
	return 0;
}

static int nilfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct super_block *sb = dentry->d_sb;
	struct nilfs_root *root = NILFS_I(d_inode(dentry))->i_root;
	struct the_nilfs *nilfs = root->nilfs;
	u64 id = huge_encode_dev(sb->s_bdev->bd_dev);
	unsigned long long blocks;
	unsigned long overhead;
	unsigned long nrsvblocks;
	sector_t nfreeblocks;
	u64 nmaxinodes, nfreeinodes;
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

	err = nilfs_ifile_count_free_inodes(root->ifile,
					    &nmaxinodes, &nfreeinodes);
	if (unlikely(err)) {
		nilfs_warn(sb, "failed to count free inodes: err=%d", err);
		if (err == -ERANGE) {
			/*
			 * If nilfs_palloc_count_max_entries() returns
			 * -ERANGE error code then we simply treat
			 * curent inodes count as maximum possible and
			 * zero as free inodes value.
			 */
			nmaxinodes = atomic64_read(&root->inodes_count);
			nfreeinodes = 0;
			err = 0;
		} else
			return err;
	}

	buf->f_type = NILFS_SUPER_MAGIC;
	buf->f_bsize = sb->s_blocksize;
	buf->f_blocks = blocks - overhead;
	buf->f_bfree = nfreeblocks;
	buf->f_bavail = (buf->f_bfree >= nrsvblocks) ?
		(buf->f_bfree - nrsvblocks) : 0;
	buf->f_files = nmaxinodes;
	buf->f_ffree = nfreeinodes;
	buf->f_namelen = NILFS_NAME_LEN;
	buf->f_fsid = u64_to_fsid(id);

	return 0;
}

static int nilfs_show_options(struct seq_file *seq, struct dentry *dentry)
{
	struct super_block *sb = dentry->d_sb;
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_root *root = NILFS_I(d_inode(dentry))->i_root;

	if (!nilfs_test_opt(nilfs, BARRIER))
		seq_puts(seq, ",nobarrier");
	if (root->cno != NILFS_CPTREE_CURRENT_CNO)
		seq_printf(seq, ",cp=%llu", (unsigned long long)root->cno);
	if (nilfs_test_opt(nilfs, ERRORS_PANIC))
		seq_puts(seq, ",errors=panic");
	if (nilfs_test_opt(nilfs, ERRORS_CONT))
		seq_puts(seq, ",errors=continue");
	if (nilfs_test_opt(nilfs, STRICT_ORDER))
		seq_puts(seq, ",order=strict");
	if (nilfs_test_opt(nilfs, NORECOVERY))
		seq_puts(seq, ",norecovery");
	if (nilfs_test_opt(nilfs, DISCARD))
		seq_puts(seq, ",discard");

	return 0;
}

static const struct super_operations nilfs_sops = {
	.alloc_inode    = nilfs_alloc_inode,
	.free_inode     = nilfs_free_inode,
	.dirty_inode    = nilfs_dirty_inode,
	.evict_inode    = nilfs_evict_inode,
	.put_super      = nilfs_put_super,
	.sync_fs        = nilfs_sync_fs,
	.freeze_fs	= nilfs_freeze,
	.unfreeze_fs	= nilfs_unfreeze,
	.statfs         = nilfs_statfs,
	.remount_fs     = nilfs_remount,
	.show_options = nilfs_show_options
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
	struct the_nilfs *nilfs = sb->s_fs_info;
	char *p;
	substring_t args[MAX_OPT_ARGS];

	if (!options)
		return 1;

	while ((p = strsep(&options, ",")) != NULL) {
		int token;

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_barrier:
			nilfs_set_opt(nilfs, BARRIER);
			break;
		case Opt_nobarrier:
			nilfs_clear_opt(nilfs, BARRIER);
			break;
		case Opt_order:
			if (strcmp(args[0].from, "relaxed") == 0)
				/* Ordered data semantics */
				nilfs_clear_opt(nilfs, STRICT_ORDER);
			else if (strcmp(args[0].from, "strict") == 0)
				/* Strict in-order semantics */
				nilfs_set_opt(nilfs, STRICT_ORDER);
			else
				return 0;
			break;
		case Opt_err_panic:
			nilfs_write_opt(nilfs, ERROR_MODE, ERRORS_PANIC);
			break;
		case Opt_err_ro:
			nilfs_write_opt(nilfs, ERROR_MODE, ERRORS_RO);
			break;
		case Opt_err_cont:
			nilfs_write_opt(nilfs, ERROR_MODE, ERRORS_CONT);
			break;
		case Opt_snapshot:
			if (is_remount) {
				nilfs_err(sb,
					  "\"%s\" option is invalid for remount",
					  p);
				return 0;
			}
			break;
		case Opt_norecovery:
			nilfs_set_opt(nilfs, NORECOVERY);
			break;
		case Opt_discard:
			nilfs_set_opt(nilfs, DISCARD);
			break;
		case Opt_nodiscard:
			nilfs_clear_opt(nilfs, DISCARD);
			break;
		default:
			nilfs_err(sb, "unrecognized mount option \"%s\"", p);
			return 0;
		}
	}
	return 1;
}

static inline void
nilfs_set_default_options(struct super_block *sb,
			  struct nilfs_super_block *sbp)
{
	struct the_nilfs *nilfs = sb->s_fs_info;

	nilfs->ns_mount_opt =
		NILFS_MOUNT_ERRORS_RO | NILFS_MOUNT_BARRIER;
}

static int nilfs_setup_super(struct super_block *sb, int is_mount)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_super_block **sbp;
	int max_mnt_count;
	int mnt_count;

	/* nilfs->ns_sem must be locked by the caller. */
	sbp = nilfs_prepare_super(sb, 0);
	if (!sbp)
		return -EIO;

	if (!is_mount)
		goto skip_mount_setup;

	max_mnt_count = le16_to_cpu(sbp[0]->s_max_mnt_count);
	mnt_count = le16_to_cpu(sbp[0]->s_mnt_count);

	if (nilfs->ns_mount_state & NILFS_ERROR_FS) {
		nilfs_warn(sb, "mounting fs with errors");
#if 0
	} else if (max_mnt_count >= 0 && mnt_count >= max_mnt_count) {
		nilfs_warn(sb, "maximal mount count reached");
#endif
	}
	if (!max_mnt_count)
		sbp[0]->s_max_mnt_count = cpu_to_le16(NILFS_DFL_MAX_MNT_COUNT);

	sbp[0]->s_mnt_count = cpu_to_le16(mnt_count + 1);
	sbp[0]->s_mtime = cpu_to_le64(ktime_get_real_seconds());

skip_mount_setup:
	sbp[0]->s_state =
		cpu_to_le16(le16_to_cpu(sbp[0]->s_state) & ~NILFS_VALID_FS);
	/* synchronize sbp[1] with sbp[0] */
	if (sbp[1])
		memcpy(sbp[1], sbp[0], nilfs->ns_sbsize);
	return nilfs_commit_super(sb, NILFS_SB_COMMIT_ALL);
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
	struct the_nilfs *nilfs = sb->s_fs_info;

	sb->s_magic = le16_to_cpu(sbp->s_magic);

	/* FS independent flags */
#ifdef NILFS_ATIME_DISABLE
	sb->s_flags |= SB_NOATIME;
#endif

	nilfs_set_default_options(sb, sbp);

	nilfs->ns_resuid = le16_to_cpu(sbp->s_def_resuid);
	nilfs->ns_resgid = le16_to_cpu(sbp->s_def_resgid);
	nilfs->ns_interval = le32_to_cpu(sbp->s_c_interval);
	nilfs->ns_watermark = le32_to_cpu(sbp->s_c_block_max);

	return !parse_options(data, sb, 0) ? -EINVAL : 0;
}

int nilfs_check_feature_compatibility(struct super_block *sb,
				      struct nilfs_super_block *sbp)
{
	__u64 features;

	features = le64_to_cpu(sbp->s_feature_incompat) &
		~NILFS_FEATURE_INCOMPAT_SUPP;
	if (features) {
		nilfs_err(sb,
			  "couldn't mount because of unsupported optional features (%llx)",
			  (unsigned long long)features);
		return -EINVAL;
	}
	features = le64_to_cpu(sbp->s_feature_compat_ro) &
		~NILFS_FEATURE_COMPAT_RO_SUPP;
	if (!sb_rdonly(sb) && features) {
		nilfs_err(sb,
			  "couldn't mount RDWR because of unsupported optional features (%llx)",
			  (unsigned long long)features);
		return -EINVAL;
	}
	return 0;
}

static int nilfs_get_root_dentry(struct super_block *sb,
				 struct nilfs_root *root,
				 struct dentry **root_dentry)
{
	struct inode *inode;
	struct dentry *dentry;
	int ret = 0;

	inode = nilfs_iget(sb, root, NILFS_ROOT_INO);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		nilfs_err(sb, "error %d getting root inode", ret);
		goto out;
	}
	if (!S_ISDIR(inode->i_mode) || !inode->i_blocks || !inode->i_size) {
		iput(inode);
		nilfs_err(sb, "corrupt root inode");
		ret = -EINVAL;
		goto out;
	}

	if (root->cno == NILFS_CPTREE_CURRENT_CNO) {
		dentry = d_find_alias(inode);
		if (!dentry) {
			dentry = d_make_root(inode);
			if (!dentry) {
				ret = -ENOMEM;
				goto failed_dentry;
			}
		} else {
			iput(inode);
		}
	} else {
		dentry = d_obtain_root(inode);
		if (IS_ERR(dentry)) {
			ret = PTR_ERR(dentry);
			goto failed_dentry;
		}
	}
	*root_dentry = dentry;
 out:
	return ret;

 failed_dentry:
	nilfs_err(sb, "error %d getting root dentry", ret);
	goto out;
}

static int nilfs_attach_snapshot(struct super_block *s, __u64 cno,
				 struct dentry **root_dentry)
{
	struct the_nilfs *nilfs = s->s_fs_info;
	struct nilfs_root *root;
	int ret;

	mutex_lock(&nilfs->ns_snapshot_mount_mutex);

	down_read(&nilfs->ns_segctor_sem);
	ret = nilfs_cpfile_is_snapshot(nilfs->ns_cpfile, cno);
	up_read(&nilfs->ns_segctor_sem);
	if (ret < 0) {
		ret = (ret == -ENOENT) ? -EINVAL : ret;
		goto out;
	} else if (!ret) {
		nilfs_err(s,
			  "The specified checkpoint is not a snapshot (checkpoint number=%llu)",
			  (unsigned long long)cno);
		ret = -EINVAL;
		goto out;
	}

	ret = nilfs_attach_checkpoint(s, cno, false, &root);
	if (ret) {
		nilfs_err(s,
			  "error %d while loading snapshot (checkpoint number=%llu)",
			  ret, (unsigned long long)cno);
		goto out;
	}
	ret = nilfs_get_root_dentry(s, root, root_dentry);
	nilfs_put_root(root);
 out:
	mutex_unlock(&nilfs->ns_snapshot_mount_mutex);
	return ret;
}

/**
 * nilfs_tree_is_busy() - try to shrink dentries of a checkpoint
 * @root_dentry: root dentry of the tree to be shrunk
 *
 * This function returns true if the tree was in-use.
 */
static bool nilfs_tree_is_busy(struct dentry *root_dentry)
{
	shrink_dcache_parent(root_dentry);
	return d_count(root_dentry) > 1;
}

int nilfs_checkpoint_is_mounted(struct super_block *sb, __u64 cno)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	struct nilfs_root *root;
	struct inode *inode;
	struct dentry *dentry;
	int ret;

	if (cno > nilfs->ns_cno)
		return false;

	if (cno >= nilfs_last_cno(nilfs))
		return true;	/* protect recent checkpoints */

	ret = false;
	root = nilfs_lookup_root(nilfs, cno);
	if (root) {
		inode = nilfs_ilookup(sb, root, NILFS_ROOT_INO);
		if (inode) {
			dentry = d_find_alias(inode);
			if (dentry) {
				ret = nilfs_tree_is_busy(dentry);
				dput(dentry);
			}
			iput(inode);
		}
		nilfs_put_root(root);
	}
	return ret;
}

/**
 * nilfs_fill_super() - initialize a super block instance
 * @sb: super_block
 * @data: mount options
 * @silent: silent mode flag
 *
 * This function is called exclusively by nilfs->ns_mount_mutex.
 * So, the recovery process is protected from other simultaneous mounts.
 */
static int
nilfs_fill_super(struct super_block *sb, void *data, int silent)
{
	struct the_nilfs *nilfs;
	struct nilfs_root *fsroot;
	__u64 cno;
	int err;

	nilfs = alloc_nilfs(sb);
	if (!nilfs)
		return -ENOMEM;

	sb->s_fs_info = nilfs;

	err = init_nilfs(nilfs, sb, (char *)data);
	if (err)
		goto failed_nilfs;

	sb->s_op = &nilfs_sops;
	sb->s_export_op = &nilfs_export_ops;
	sb->s_root = NULL;
	sb->s_time_gran = 1;
	sb->s_max_links = NILFS_LINK_MAX;

	sb->s_bdi = bdi_get(sb->s_bdev->bd_bdi);

	err = load_nilfs(nilfs, sb);
	if (err)
		goto failed_nilfs;

	cno = nilfs_last_cno(nilfs);
	err = nilfs_attach_checkpoint(sb, cno, true, &fsroot);
	if (err) {
		nilfs_err(sb,
			  "error %d while loading last checkpoint (checkpoint number=%llu)",
			  err, (unsigned long long)cno);
		goto failed_unload;
	}

	if (!sb_rdonly(sb)) {
		err = nilfs_attach_log_writer(sb, fsroot);
		if (err)
			goto failed_checkpoint;
	}

	err = nilfs_get_root_dentry(sb, fsroot, &sb->s_root);
	if (err)
		goto failed_segctor;

	nilfs_put_root(fsroot);

	if (!sb_rdonly(sb)) {
		down_write(&nilfs->ns_sem);
		nilfs_setup_super(sb, true);
		up_write(&nilfs->ns_sem);
	}

	return 0;

 failed_segctor:
	nilfs_detach_log_writer(sb);

 failed_checkpoint:
	nilfs_put_root(fsroot);

 failed_unload:
	iput(nilfs->ns_sufile);
	iput(nilfs->ns_cpfile);
	iput(nilfs->ns_dat);

 failed_nilfs:
	destroy_nilfs(nilfs);
	return err;
}

static int nilfs_remount(struct super_block *sb, int *flags, char *data)
{
	struct the_nilfs *nilfs = sb->s_fs_info;
	unsigned long old_sb_flags;
	unsigned long old_mount_opt;
	int err;

	sync_filesystem(sb);
	old_sb_flags = sb->s_flags;
	old_mount_opt = nilfs->ns_mount_opt;

	if (!parse_options(data, sb, 1)) {
		err = -EINVAL;
		goto restore_opts;
	}
	sb->s_flags = (sb->s_flags & ~SB_POSIXACL);

	err = -EINVAL;

	if (!nilfs_valid_fs(nilfs)) {
		nilfs_warn(sb,
			   "couldn't remount because the filesystem is in an incomplete recovery state");
		goto restore_opts;
	}

	if ((bool)(*flags & SB_RDONLY) == sb_rdonly(sb))
		goto out;
	if (*flags & SB_RDONLY) {
		sb->s_flags |= SB_RDONLY;

		/*
		 * Remounting a valid RW partition RDONLY, so set
		 * the RDONLY flag and then mark the partition as valid again.
		 */
		down_write(&nilfs->ns_sem);
		nilfs_cleanup_super(sb);
		up_write(&nilfs->ns_sem);
	} else {
		__u64 features;
		struct nilfs_root *root;

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
			nilfs_warn(sb,
				   "couldn't remount RDWR because of unsupported optional features (%llx)",
				   (unsigned long long)features);
			err = -EROFS;
			goto restore_opts;
		}

		sb->s_flags &= ~SB_RDONLY;

		root = NILFS_I(d_inode(sb->s_root))->i_root;
		err = nilfs_attach_log_writer(sb, root);
		if (err)
			goto restore_opts;

		down_write(&nilfs->ns_sem);
		nilfs_setup_super(sb, true);
		up_write(&nilfs->ns_sem);
	}
 out:
	return 0;

 restore_opts:
	sb->s_flags = old_sb_flags;
	nilfs->ns_mount_opt = old_mount_opt;
	return err;
}

struct nilfs_super_data {
	struct block_device *bdev;
	__u64 cno;
	int flags;
};

static int nilfs_parse_snapshot_option(const char *option,
				       const substring_t *arg,
				       struct nilfs_super_data *sd)
{
	unsigned long long val;
	const char *msg = NULL;
	int err;

	if (!(sd->flags & SB_RDONLY)) {
		msg = "read-only option is not specified";
		goto parse_error;
	}

	err = kstrtoull(arg->from, 0, &val);
	if (err) {
		if (err == -ERANGE)
			msg = "too large checkpoint number";
		else
			msg = "malformed argument";
		goto parse_error;
	} else if (val == 0) {
		msg = "invalid checkpoint number 0";
		goto parse_error;
	}
	sd->cno = val;
	return 0;

parse_error:
	nilfs_err(NULL, "invalid option \"%s\": %s", option, msg);
	return 1;
}

/**
 * nilfs_identify - pre-read mount options needed to identify mount instance
 * @data: mount options
 * @sd: nilfs_super_data
 */
static int nilfs_identify(char *data, struct nilfs_super_data *sd)
{
	char *p, *options = data;
	substring_t args[MAX_OPT_ARGS];
	int token;
	int ret = 0;

	do {
		p = strsep(&options, ",");
		if (p != NULL && *p) {
			token = match_token(p, tokens, args);
			if (token == Opt_snapshot)
				ret = nilfs_parse_snapshot_option(p, &args[0],
								  sd);
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
	s->s_bdev = data;
	s->s_dev = s->s_bdev->bd_dev;
	return 0;
}

static int nilfs_test_bdev_super(struct super_block *s, void *data)
{
	return (void *)s->s_bdev == data;
}

static struct dentry *
nilfs_mount(struct file_system_type *fs_type, int flags,
	     const char *dev_name, void *data)
{
	struct nilfs_super_data sd;
	struct super_block *s;
	fmode_t mode = FMODE_READ | FMODE_EXCL;
	struct dentry *root_dentry;
	int err, s_new = false;

	if (!(flags & SB_RDONLY))
		mode |= FMODE_WRITE;

	sd.bdev = blkdev_get_by_path(dev_name, mode, fs_type);
	if (IS_ERR(sd.bdev))
		return ERR_CAST(sd.bdev);

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
	mutex_lock(&sd.bdev->bd_fsfreeze_mutex);
	if (sd.bdev->bd_fsfreeze_count > 0) {
		mutex_unlock(&sd.bdev->bd_fsfreeze_mutex);
		err = -EBUSY;
		goto failed;
	}
	s = sget(fs_type, nilfs_test_bdev_super, nilfs_set_bdev_super, flags,
		 sd.bdev);
	mutex_unlock(&sd.bdev->bd_fsfreeze_mutex);
	if (IS_ERR(s)) {
		err = PTR_ERR(s);
		goto failed;
	}

	if (!s->s_root) {
		s_new = true;

		/* New superblock instance created */
		s->s_mode = mode;
		snprintf(s->s_id, sizeof(s->s_id), "%pg", sd.bdev);
		sb_set_blocksize(s, block_size(sd.bdev));

		err = nilfs_fill_super(s, data, flags & SB_SILENT ? 1 : 0);
		if (err)
			goto failed_super;

		s->s_flags |= SB_ACTIVE;
	} else if (!sd.cno) {
		if (nilfs_tree_is_busy(s->s_root)) {
			if ((flags ^ s->s_flags) & SB_RDONLY) {
				nilfs_err(s,
					  "the device already has a %s mount.",
					  sb_rdonly(s) ? "read-only" : "read/write");
				err = -EBUSY;
				goto failed_super;
			}
		} else {
			/*
			 * Try remount to setup mount states if the current
			 * tree is not mounted and only snapshots use this sb.
			 */
			err = nilfs_remount(s, &flags, data);
			if (err)
				goto failed_super;
		}
	}

	if (sd.cno) {
		err = nilfs_attach_snapshot(s, sd.cno, &root_dentry);
		if (err)
			goto failed_super;
	} else {
		root_dentry = dget(s->s_root);
	}

	if (!s_new)
		blkdev_put(sd.bdev, mode);

	return root_dentry;

 failed_super:
	deactivate_locked_super(s);

 failed:
	if (!s_new)
		blkdev_put(sd.bdev, mode);
	return ERR_PTR(err);
}

struct file_system_type nilfs_fs_type = {
	.owner    = THIS_MODULE,
	.name     = "nilfs2",
	.mount    = nilfs_mount,
	.kill_sb  = kill_block_super,
	.fs_flags = FS_REQUIRES_DEV,
};
MODULE_ALIAS_FS("nilfs2");

static void nilfs_inode_init_once(void *obj)
{
	struct nilfs_inode_info *ii = obj;

	INIT_LIST_HEAD(&ii->i_dirty);
#ifdef CONFIG_NILFS_XATTR
	init_rwsem(&ii->xattr_sem);
#endif
	inode_init_once(&ii->vfs_inode);
}

static void nilfs_segbuf_init_once(void *obj)
{
	memset(obj, 0, sizeof(struct nilfs_segment_buffer));
}

static void nilfs_destroy_cachep(void)
{
	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();

	kmem_cache_destroy(nilfs_inode_cachep);
	kmem_cache_destroy(nilfs_transaction_cachep);
	kmem_cache_destroy(nilfs_segbuf_cachep);
	kmem_cache_destroy(nilfs_btree_path_cache);
}

static int __init nilfs_init_cachep(void)
{
	nilfs_inode_cachep = kmem_cache_create("nilfs2_inode_cache",
			sizeof(struct nilfs_inode_info), 0,
			SLAB_RECLAIM_ACCOUNT|SLAB_ACCOUNT,
			nilfs_inode_init_once);
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

	err = nilfs_sysfs_init();
	if (err)
		goto free_cachep;

	err = register_filesystem(&nilfs_fs_type);
	if (err)
		goto deinit_sysfs_entry;

	printk(KERN_INFO "NILFS version 2 loaded\n");
	return 0;

deinit_sysfs_entry:
	nilfs_sysfs_exit();
free_cachep:
	nilfs_destroy_cachep();
fail:
	return err;
}

static void __exit exit_nilfs_fs(void)
{
	nilfs_destroy_cachep();
	nilfs_sysfs_exit();
	unregister_filesystem(&nilfs_fs_type);
}

module_init(init_nilfs_fs)
module_exit(exit_nilfs_fs)
