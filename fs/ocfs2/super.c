/* -*- mode: c; c-basic-offset: 8; -*-
 * vim: noexpandtab sw=8 ts=8 sts=0:
 *
 * super.c
 *
 * load/unload driver, mount/dismount volumes
 *
 * Copyright (C) 2002, 2004 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 021110-1307, USA.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/utsname.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/statfs.h>
#include <linux/moduleparam.h>
#include <linux/blkdev.h>
#include <linux/socket.h>
#include <linux/inet.h>
#include <linux/parser.h>
#include <linux/crc32.h>
#include <linux/debugfs.h>

#include <cluster/nodemanager.h>

#define MLOG_MASK_PREFIX ML_SUPER
#include <cluster/masklog.h>

#include "ocfs2.h"

/* this should be the only file to include a version 1 header */
#include "ocfs1_fs_compat.h"

#include "alloc.h"
#include "dlmglue.h"
#include "export.h"
#include "extent_map.h"
#include "heartbeat.h"
#include "inode.h"
#include "journal.h"
#include "localalloc.h"
#include "namei.h"
#include "slot_map.h"
#include "super.h"
#include "sysfile.h"
#include "uptodate.h"
#include "ver.h"
#include "vote.h"

#include "buffer_head_io.h"

static struct kmem_cache *ocfs2_inode_cachep = NULL;

/* OCFS2 needs to schedule several differnt types of work which
 * require cluster locking, disk I/O, recovery waits, etc. Since these
 * types of work tend to be heavy we avoid using the kernel events
 * workqueue and schedule on our own. */
struct workqueue_struct *ocfs2_wq = NULL;

static struct dentry *ocfs2_debugfs_root = NULL;

MODULE_AUTHOR("Oracle");
MODULE_LICENSE("GPL");

static int ocfs2_parse_options(struct super_block *sb, char *options,
			       unsigned long *mount_opt, int is_remount);
static void ocfs2_put_super(struct super_block *sb);
static int ocfs2_mount_volume(struct super_block *sb);
static int ocfs2_remount(struct super_block *sb, int *flags, char *data);
static void ocfs2_dismount_volume(struct super_block *sb, int mnt_err);
static int ocfs2_initialize_mem_caches(void);
static void ocfs2_free_mem_caches(void);
static void ocfs2_delete_osb(struct ocfs2_super *osb);

static int ocfs2_statfs(struct dentry *dentry, struct kstatfs *buf);

static int ocfs2_sync_fs(struct super_block *sb, int wait);

static int ocfs2_init_global_system_inodes(struct ocfs2_super *osb);
static int ocfs2_init_local_system_inodes(struct ocfs2_super *osb);
static int ocfs2_release_system_inodes(struct ocfs2_super *osb);
static int ocfs2_fill_local_node_info(struct ocfs2_super *osb);
static int ocfs2_check_volume(struct ocfs2_super *osb);
static int ocfs2_verify_volume(struct ocfs2_dinode *di,
			       struct buffer_head *bh,
			       u32 sectsize);
static int ocfs2_initialize_super(struct super_block *sb,
				  struct buffer_head *bh,
				  int sector_size);
static int ocfs2_get_sector(struct super_block *sb,
			    struct buffer_head **bh,
			    int block,
			    int sect_size);
static void ocfs2_write_super(struct super_block *sb);
static struct inode *ocfs2_alloc_inode(struct super_block *sb);
static void ocfs2_destroy_inode(struct inode *inode);

static unsigned long long ocfs2_max_file_offset(unsigned int blockshift);

static struct super_operations ocfs2_sops = {
	.statfs		= ocfs2_statfs,
	.alloc_inode	= ocfs2_alloc_inode,
	.destroy_inode	= ocfs2_destroy_inode,
	.drop_inode	= ocfs2_drop_inode,
	.clear_inode	= ocfs2_clear_inode,
	.delete_inode	= ocfs2_delete_inode,
	.sync_fs	= ocfs2_sync_fs,
	.write_super	= ocfs2_write_super,
	.put_super	= ocfs2_put_super,
	.remount_fs	= ocfs2_remount,
};

enum {
	Opt_barrier,
	Opt_err_panic,
	Opt_err_ro,
	Opt_intr,
	Opt_nointr,
	Opt_hb_none,
	Opt_hb_local,
	Opt_data_ordered,
	Opt_data_writeback,
	Opt_atime_quantum,
	Opt_err,
};

static match_table_t tokens = {
	{Opt_barrier, "barrier=%u"},
	{Opt_err_panic, "errors=panic"},
	{Opt_err_ro, "errors=remount-ro"},
	{Opt_intr, "intr"},
	{Opt_nointr, "nointr"},
	{Opt_hb_none, OCFS2_HB_NONE},
	{Opt_hb_local, OCFS2_HB_LOCAL},
	{Opt_data_ordered, "data=ordered"},
	{Opt_data_writeback, "data=writeback"},
	{Opt_atime_quantum, "atime_quantum=%u"},
	{Opt_err, NULL}
};

/*
 * write_super and sync_fs ripped right out of ext3.
 */
static void ocfs2_write_super(struct super_block *sb)
{
	if (mutex_trylock(&sb->s_lock) != 0)
		BUG();
	sb->s_dirt = 0;
}

static int ocfs2_sync_fs(struct super_block *sb, int wait)
{
	int status = 0;
	tid_t target;
	struct ocfs2_super *osb = OCFS2_SB(sb);

	sb->s_dirt = 0;

	if (ocfs2_is_hard_readonly(osb))
		return -EROFS;

	if (wait) {
		status = ocfs2_flush_truncate_log(osb);
		if (status < 0)
			mlog_errno(status);
	} else {
		ocfs2_schedule_truncate_log_flush(osb, 0);
	}

	if (journal_start_commit(OCFS2_SB(sb)->journal->j_journal, &target)) {
		if (wait)
			log_wait_commit(OCFS2_SB(sb)->journal->j_journal,
					target);
	}
	return 0;
}

static int ocfs2_init_global_system_inodes(struct ocfs2_super *osb)
{
	struct inode *new = NULL;
	int status = 0;
	int i;

	mlog_entry_void();

	new = ocfs2_iget(osb, osb->root_blkno, OCFS2_FI_FLAG_SYSFILE);
	if (IS_ERR(new)) {
		status = PTR_ERR(new);
		mlog_errno(status);
		goto bail;
	}
	osb->root_inode = new;

	new = ocfs2_iget(osb, osb->system_dir_blkno, OCFS2_FI_FLAG_SYSFILE);
	if (IS_ERR(new)) {
		status = PTR_ERR(new);
		mlog_errno(status);
		goto bail;
	}
	osb->sys_root_inode = new;

	for (i = OCFS2_FIRST_ONLINE_SYSTEM_INODE;
	     i <= OCFS2_LAST_GLOBAL_SYSTEM_INODE; i++) {
		new = ocfs2_get_system_file_inode(osb, i, osb->slot_num);
		if (!new) {
			ocfs2_release_system_inodes(osb);
			status = -EINVAL;
			mlog_errno(status);
			/* FIXME: Should ERROR_RO_FS */
			mlog(ML_ERROR, "Unable to load system inode %d, "
			     "possibly corrupt fs?", i);
			goto bail;
		}
		// the array now has one ref, so drop this one
		iput(new);
	}

bail:
	mlog_exit(status);
	return status;
}

static int ocfs2_init_local_system_inodes(struct ocfs2_super *osb)
{
	struct inode *new = NULL;
	int status = 0;
	int i;

	mlog_entry_void();

	for (i = OCFS2_LAST_GLOBAL_SYSTEM_INODE + 1;
	     i < NUM_SYSTEM_INODES;
	     i++) {
		new = ocfs2_get_system_file_inode(osb, i, osb->slot_num);
		if (!new) {
			ocfs2_release_system_inodes(osb);
			status = -EINVAL;
			mlog(ML_ERROR, "status=%d, sysfile=%d, slot=%d\n",
			     status, i, osb->slot_num);
			goto bail;
		}
		/* the array now has one ref, so drop this one */
		iput(new);
	}

bail:
	mlog_exit(status);
	return status;
}

static int ocfs2_release_system_inodes(struct ocfs2_super *osb)
{
	int status = 0, i;
	struct inode *inode;

	mlog_entry_void();

	for (i = 0; i < NUM_SYSTEM_INODES; i++) {
		inode = osb->system_inodes[i];
		if (inode) {
			iput(inode);
			osb->system_inodes[i] = NULL;
		}
	}

	inode = osb->sys_root_inode;
	if (inode) {
		iput(inode);
		osb->sys_root_inode = NULL;
	}

	inode = osb->root_inode;
	if (inode) {
		iput(inode);
		osb->root_inode = NULL;
	}

	mlog_exit(status);
	return status;
}

/* We're allocating fs objects, use GFP_NOFS */
static struct inode *ocfs2_alloc_inode(struct super_block *sb)
{
	struct ocfs2_inode_info *oi;

	oi = kmem_cache_alloc(ocfs2_inode_cachep, GFP_NOFS);
	if (!oi)
		return NULL;

	return &oi->vfs_inode;
}

static void ocfs2_destroy_inode(struct inode *inode)
{
	kmem_cache_free(ocfs2_inode_cachep, OCFS2_I(inode));
}

/* From xfs_super.c:xfs_max_file_offset
 * Copyright (c) 2000-2004 Silicon Graphics, Inc.
 */
static unsigned long long ocfs2_max_file_offset(unsigned int blockshift)
{
	unsigned int pagefactor = 1;
	unsigned int bitshift = BITS_PER_LONG - 1;

	/* Figure out maximum filesize, on Linux this can depend on
	 * the filesystem blocksize (on 32 bit platforms).
	 * __block_prepare_write does this in an [unsigned] long...
	 *      page->index << (PAGE_CACHE_SHIFT - bbits)
	 * So, for page sized blocks (4K on 32 bit platforms),
	 * this wraps at around 8Tb (hence MAX_LFS_FILESIZE which is
	 *      (((u64)PAGE_CACHE_SIZE << (BITS_PER_LONG-1))-1)
	 * but for smaller blocksizes it is less (bbits = log2 bsize).
	 * Note1: get_block_t takes a long (implicit cast from above)
	 * Note2: The Large Block Device (LBD and HAVE_SECTOR_T) patch
	 * can optionally convert the [unsigned] long from above into
	 * an [unsigned] long long.
	 */

#if BITS_PER_LONG == 32
# if defined(CONFIG_LBD)
	BUILD_BUG_ON(sizeof(sector_t) != 8);
	pagefactor = PAGE_CACHE_SIZE;
	bitshift = BITS_PER_LONG;
# else
	pagefactor = PAGE_CACHE_SIZE >> (PAGE_CACHE_SHIFT - blockshift);
# endif
#endif

	return (((unsigned long long)pagefactor) << bitshift) - 1;
}

static int ocfs2_remount(struct super_block *sb, int *flags, char *data)
{
	int incompat_features;
	int ret = 0;
	unsigned long parsed_options;
	struct ocfs2_super *osb = OCFS2_SB(sb);

	if (!ocfs2_parse_options(sb, data, &parsed_options, 1)) {
		ret = -EINVAL;
		goto out;
	}

	if ((osb->s_mount_opt & OCFS2_MOUNT_HB_LOCAL) !=
	    (parsed_options & OCFS2_MOUNT_HB_LOCAL)) {
		ret = -EINVAL;
		mlog(ML_ERROR, "Cannot change heartbeat mode on remount\n");
		goto out;
	}

	if ((osb->s_mount_opt & OCFS2_MOUNT_DATA_WRITEBACK) !=
	    (parsed_options & OCFS2_MOUNT_DATA_WRITEBACK)) {
		ret = -EINVAL;
		mlog(ML_ERROR, "Cannot change data mode on remount\n");
		goto out;
	}

	/* We're going to/from readonly mode. */
	if ((*flags & MS_RDONLY) != (sb->s_flags & MS_RDONLY)) {
		/* Lock here so the check of HARD_RO and the potential
		 * setting of SOFT_RO is atomic. */
		spin_lock(&osb->osb_lock);
		if (osb->osb_flags & OCFS2_OSB_HARD_RO) {
			mlog(ML_ERROR, "Remount on readonly device is forbidden.\n");
			ret = -EROFS;
			goto unlock_osb;
		}

		if (*flags & MS_RDONLY) {
			mlog(0, "Going to ro mode.\n");
			sb->s_flags |= MS_RDONLY;
			osb->osb_flags |= OCFS2_OSB_SOFT_RO;
		} else {
			mlog(0, "Making ro filesystem writeable.\n");

			if (osb->osb_flags & OCFS2_OSB_ERROR_FS) {
				mlog(ML_ERROR, "Cannot remount RDWR "
				     "filesystem due to previous errors.\n");
				ret = -EROFS;
				goto unlock_osb;
			}
			incompat_features = OCFS2_HAS_RO_COMPAT_FEATURE(sb, ~OCFS2_FEATURE_RO_COMPAT_SUPP);
			if (incompat_features) {
				mlog(ML_ERROR, "Cannot remount RDWR because "
				     "of unsupported optional features "
				     "(%x).\n", incompat_features);
				ret = -EINVAL;
				goto unlock_osb;
			}
			sb->s_flags &= ~MS_RDONLY;
			osb->osb_flags &= ~OCFS2_OSB_SOFT_RO;
		}
unlock_osb:
		spin_unlock(&osb->osb_lock);
	}

	if (!ret) {
		if (!ocfs2_is_hard_readonly(osb))
			ocfs2_set_journal_params(osb);

		/* Only save off the new mount options in case of a successful
		 * remount. */
		osb->s_mount_opt = parsed_options;
	}
out:
	return ret;
}

static int ocfs2_sb_probe(struct super_block *sb,
			  struct buffer_head **bh,
			  int *sector_size)
{
	int status = 0, tmpstat;
	struct ocfs1_vol_disk_hdr *hdr;
	struct ocfs2_dinode *di;
	int blksize;

	*bh = NULL;

	/* may be > 512 */
	*sector_size = bdev_hardsect_size(sb->s_bdev);
	if (*sector_size > OCFS2_MAX_BLOCKSIZE) {
		mlog(ML_ERROR, "Hardware sector size too large: %d (max=%d)\n",
		     *sector_size, OCFS2_MAX_BLOCKSIZE);
		status = -EINVAL;
		goto bail;
	}

	/* Can this really happen? */
	if (*sector_size < OCFS2_MIN_BLOCKSIZE)
		*sector_size = OCFS2_MIN_BLOCKSIZE;

	/* check block zero for old format */
	status = ocfs2_get_sector(sb, bh, 0, *sector_size);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}
	hdr = (struct ocfs1_vol_disk_hdr *) (*bh)->b_data;
	if (hdr->major_version == OCFS1_MAJOR_VERSION) {
		mlog(ML_ERROR, "incompatible version: %u.%u\n",
		     hdr->major_version, hdr->minor_version);
		status = -EINVAL;
	}
	if (memcmp(hdr->signature, OCFS1_VOLUME_SIGNATURE,
		   strlen(OCFS1_VOLUME_SIGNATURE)) == 0) {
		mlog(ML_ERROR, "incompatible volume signature: %8s\n",
		     hdr->signature);
		status = -EINVAL;
	}
	brelse(*bh);
	*bh = NULL;
	if (status < 0) {
		mlog(ML_ERROR, "This is an ocfs v1 filesystem which must be "
		     "upgraded before mounting with ocfs v2\n");
		goto bail;
	}

	/*
	 * Now check at magic offset for 512, 1024, 2048, 4096
	 * blocksizes.  4096 is the maximum blocksize because it is
	 * the minimum clustersize.
	 */
	status = -EINVAL;
	for (blksize = *sector_size;
	     blksize <= OCFS2_MAX_BLOCKSIZE;
	     blksize <<= 1) {
		tmpstat = ocfs2_get_sector(sb, bh,
					   OCFS2_SUPER_BLOCK_BLKNO,
					   blksize);
		if (tmpstat < 0) {
			status = tmpstat;
			mlog_errno(status);
			goto bail;
		}
		di = (struct ocfs2_dinode *) (*bh)->b_data;
		status = ocfs2_verify_volume(di, *bh, blksize);
		if (status >= 0)
			goto bail;
		brelse(*bh);
		*bh = NULL;
		if (status != -EAGAIN)
			break;
	}

bail:
	return status;
}

static int ocfs2_fill_super(struct super_block *sb, void *data, int silent)
{
	struct dentry *root;
	int status, sector_size;
	unsigned long parsed_opt;
	struct inode *inode = NULL;
	struct ocfs2_super *osb = NULL;
	struct buffer_head *bh = NULL;

	mlog_entry("%p, %p, %i", sb, data, silent);

	/* for now we only have one cluster/node, make sure we see it
	 * in the heartbeat universe */
	if (!o2hb_check_local_node_heartbeating()) {
		status = -EINVAL;
		goto read_super_error;
	}

	/* probe for superblock */
	status = ocfs2_sb_probe(sb, &bh, &sector_size);
	if (status < 0) {
		mlog(ML_ERROR, "superblock probe failed!\n");
		goto read_super_error;
	}

	status = ocfs2_initialize_super(sb, bh, sector_size);
	osb = OCFS2_SB(sb);
	if (status < 0) {
		mlog_errno(status);
		goto read_super_error;
	}
	brelse(bh);
	bh = NULL;

	if (!ocfs2_parse_options(sb, data, &parsed_opt, 0)) {
		status = -EINVAL;
		goto read_super_error;
	}
	osb->s_mount_opt = parsed_opt;

	sb->s_magic = OCFS2_SUPER_MAGIC;

	/* Hard readonly mode only if: bdev_read_only, MS_RDONLY,
	 * heartbeat=none */
	if (bdev_read_only(sb->s_bdev)) {
		if (!(sb->s_flags & MS_RDONLY)) {
			status = -EACCES;
			mlog(ML_ERROR, "Readonly device detected but readonly "
			     "mount was not specified.\n");
			goto read_super_error;
		}

		/* You should not be able to start a local heartbeat
		 * on a readonly device. */
		if (osb->s_mount_opt & OCFS2_MOUNT_HB_LOCAL) {
			status = -EROFS;
			mlog(ML_ERROR, "Local heartbeat specified on readonly "
			     "device.\n");
			goto read_super_error;
		}

		status = ocfs2_check_journals_nolocks(osb);
		if (status < 0) {
			if (status == -EROFS)
				mlog(ML_ERROR, "Recovery required on readonly "
				     "file system, but write access is "
				     "unavailable.\n");
			else
				mlog_errno(status);			
			goto read_super_error;
		}

		ocfs2_set_ro_flag(osb, 1);

		printk(KERN_NOTICE "Readonly device detected. No cluster "
		       "services will be utilized for this mount. Recovery "
		       "will be skipped.\n");
	}

	if (!ocfs2_is_hard_readonly(osb)) {
		/* If this isn't a hard readonly mount, then we need
		 * to make sure that heartbeat is in a valid state,
		 * and that we mark ourselves soft readonly is -oro
		 * was specified. */
		if (!(osb->s_mount_opt & OCFS2_MOUNT_HB_LOCAL)) {
			mlog(ML_ERROR, "No heartbeat for device (%s)\n",
			     sb->s_id);
			status = -EINVAL;
			goto read_super_error;
		}

		if (sb->s_flags & MS_RDONLY)
			ocfs2_set_ro_flag(osb, 0);
	}

	osb->osb_debug_root = debugfs_create_dir(osb->uuid_str,
						 ocfs2_debugfs_root);
	if (!osb->osb_debug_root) {
		status = -EINVAL;
		mlog(ML_ERROR, "Unable to create per-mount debugfs root.\n");
		goto read_super_error;
	}

	status = ocfs2_mount_volume(sb);
	if (osb->root_inode)
		inode = igrab(osb->root_inode);

	if (status < 0)
		goto read_super_error;

	if (!inode) {
		status = -EIO;
		mlog_errno(status);
		goto read_super_error;
	}

	root = d_alloc_root(inode);
	if (!root) {
		status = -ENOMEM;
		mlog_errno(status);
		goto read_super_error;
	}

	sb->s_root = root;

	ocfs2_complete_mount_recovery(osb);

	printk(KERN_INFO "ocfs2: Mounting device (%s) on (node %d, slot %d) "
	       "with %s data mode.\n",
	       osb->dev_str, osb->node_num, osb->slot_num,
	       osb->s_mount_opt & OCFS2_MOUNT_DATA_WRITEBACK ? "writeback" :
	       "ordered");

	atomic_set(&osb->vol_state, VOLUME_MOUNTED);
	wake_up(&osb->osb_mount_event);

	mlog_exit(status);
	return status;

read_super_error:
	if (bh != NULL)
		brelse(bh);

	if (inode)
		iput(inode);

	if (osb) {
		atomic_set(&osb->vol_state, VOLUME_DISABLED);
		wake_up(&osb->osb_mount_event);
		ocfs2_dismount_volume(sb, 1);
	}

	mlog_exit(status);
	return status;
}

static int ocfs2_get_sb(struct file_system_type *fs_type,
			int flags,
			const char *dev_name,
			void *data,
			struct vfsmount *mnt)
{
	return get_sb_bdev(fs_type, flags, dev_name, data, ocfs2_fill_super,
			   mnt);
}

static struct file_system_type ocfs2_fs_type = {
	.owner          = THIS_MODULE,
	.name           = "ocfs2",
	.get_sb         = ocfs2_get_sb, /* is this called when we mount
					* the fs? */
	.kill_sb        = kill_block_super, /* set to the generic one
					     * right now, but do we
					     * need to change that? */
	.fs_flags       = FS_REQUIRES_DEV|FS_RENAME_DOES_D_MOVE,
	.next           = NULL
};

static int ocfs2_parse_options(struct super_block *sb,
			       char *options,
			       unsigned long *mount_opt,
			       int is_remount)
{
	int status;
	char *p;

	mlog_entry("remount: %d, options: \"%s\"\n", is_remount,
		   options ? options : "(none)");

	*mount_opt = 0;

	if (!options) {
		status = 1;
		goto bail;
	}

	while ((p = strsep(&options, ",")) != NULL) {
		int token, option;
		substring_t args[MAX_OPT_ARGS];
		struct ocfs2_super * osb = OCFS2_SB(sb);

		if (!*p)
			continue;

		token = match_token(p, tokens, args);
		switch (token) {
		case Opt_hb_local:
			*mount_opt |= OCFS2_MOUNT_HB_LOCAL;
			break;
		case Opt_hb_none:
			*mount_opt &= ~OCFS2_MOUNT_HB_LOCAL;
			break;
		case Opt_barrier:
			if (match_int(&args[0], &option)) {
				status = 0;
				goto bail;
			}
			if (option)
				*mount_opt |= OCFS2_MOUNT_BARRIER;
			else
				*mount_opt &= ~OCFS2_MOUNT_BARRIER;
			break;
		case Opt_intr:
			*mount_opt &= ~OCFS2_MOUNT_NOINTR;
			break;
		case Opt_nointr:
			*mount_opt |= OCFS2_MOUNT_NOINTR;
			break;
		case Opt_err_panic:
			*mount_opt |= OCFS2_MOUNT_ERRORS_PANIC;
			break;
		case Opt_err_ro:
			*mount_opt &= ~OCFS2_MOUNT_ERRORS_PANIC;
			break;
		case Opt_data_ordered:
			*mount_opt &= ~OCFS2_MOUNT_DATA_WRITEBACK;
			break;
		case Opt_data_writeback:
			*mount_opt |= OCFS2_MOUNT_DATA_WRITEBACK;
			break;
		case Opt_atime_quantum:
			if (match_int(&args[0], &option)) {
				status = 0;
				goto bail;
			}
			if (option >= 0)
				osb->s_atime_quantum = option;
			else
				osb->s_atime_quantum = OCFS2_DEFAULT_ATIME_QUANTUM;
			break;
		default:
			mlog(ML_ERROR,
			     "Unrecognized mount option \"%s\" "
			     "or missing value\n", p);
			status = 0;
			goto bail;
		}
	}

	status = 1;

bail:
	mlog_exit(status);
	return status;
}

static int __init ocfs2_init(void)
{
	int status;

	mlog_entry_void();

	ocfs2_print_version();

	if (init_ocfs2_extent_maps())
		return -ENOMEM;

	status = init_ocfs2_uptodate_cache();
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	status = ocfs2_initialize_mem_caches();
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	ocfs2_wq = create_singlethread_workqueue("ocfs2_wq");
	if (!ocfs2_wq) {
		status = -ENOMEM;
		goto leave;
	}

	ocfs2_debugfs_root = debugfs_create_dir("ocfs2", NULL);
	if (!ocfs2_debugfs_root) {
		status = -EFAULT;
		mlog(ML_ERROR, "Unable to create ocfs2 debugfs root.\n");
	}

leave:
	if (status < 0) {
		ocfs2_free_mem_caches();
		exit_ocfs2_uptodate_cache();
		exit_ocfs2_extent_maps();
	}

	mlog_exit(status);

	if (status >= 0) {
		return register_filesystem(&ocfs2_fs_type);
	} else
		return -1;
}

static void __exit ocfs2_exit(void)
{
	mlog_entry_void();

	if (ocfs2_wq) {
		flush_workqueue(ocfs2_wq);
		destroy_workqueue(ocfs2_wq);
	}

	debugfs_remove(ocfs2_debugfs_root);

	ocfs2_free_mem_caches();

	unregister_filesystem(&ocfs2_fs_type);

	exit_ocfs2_extent_maps();

	exit_ocfs2_uptodate_cache();

	mlog_exit_void();
}

static void ocfs2_put_super(struct super_block *sb)
{
	mlog_entry("(0x%p)\n", sb);

	ocfs2_sync_blockdev(sb);
	ocfs2_dismount_volume(sb, 0);

	mlog_exit_void();
}

static int ocfs2_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	struct ocfs2_super *osb;
	u32 numbits, freebits;
	int status;
	struct ocfs2_dinode *bm_lock;
	struct buffer_head *bh = NULL;
	struct inode *inode = NULL;

	mlog_entry("(%p, %p)\n", dentry->d_sb, buf);

	osb = OCFS2_SB(dentry->d_sb);

	inode = ocfs2_get_system_file_inode(osb,
					    GLOBAL_BITMAP_SYSTEM_INODE,
					    OCFS2_INVALID_SLOT);
	if (!inode) {
		mlog(ML_ERROR, "failed to get bitmap inode\n");
		status = -EIO;
		goto bail;
	}

	status = ocfs2_meta_lock(inode, &bh, 0);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	bm_lock = (struct ocfs2_dinode *) bh->b_data;

	numbits = le32_to_cpu(bm_lock->id1.bitmap1.i_total);
	freebits = numbits - le32_to_cpu(bm_lock->id1.bitmap1.i_used);

	buf->f_type = OCFS2_SUPER_MAGIC;
	buf->f_bsize = dentry->d_sb->s_blocksize;
	buf->f_namelen = OCFS2_MAX_FILENAME_LEN;
	buf->f_blocks = ((sector_t) numbits) *
			(osb->s_clustersize >> osb->sb->s_blocksize_bits);
	buf->f_bfree = ((sector_t) freebits) *
		       (osb->s_clustersize >> osb->sb->s_blocksize_bits);
	buf->f_bavail = buf->f_bfree;
	buf->f_files = numbits;
	buf->f_ffree = freebits;

	brelse(bh);

	ocfs2_meta_unlock(inode, 0);
	status = 0;
bail:
	if (inode)
		iput(inode);

	mlog_exit(status);

	return status;
}

static void ocfs2_inode_init_once(void *data,
				  struct kmem_cache *cachep,
				  unsigned long flags)
{
	struct ocfs2_inode_info *oi = data;

	if ((flags & (SLAB_CTOR_VERIFY|SLAB_CTOR_CONSTRUCTOR)) ==
	    SLAB_CTOR_CONSTRUCTOR) {
		oi->ip_flags = 0;
		oi->ip_open_count = 0;
		spin_lock_init(&oi->ip_lock);
		ocfs2_extent_map_init(&oi->vfs_inode);
		INIT_LIST_HEAD(&oi->ip_io_markers);
		oi->ip_created_trans = 0;
		oi->ip_last_trans = 0;
		oi->ip_dir_start_lookup = 0;

		init_rwsem(&oi->ip_alloc_sem);
		mutex_init(&oi->ip_io_mutex);

		oi->ip_blkno = 0ULL;
		oi->ip_clusters = 0;

		ocfs2_lock_res_init_once(&oi->ip_rw_lockres);
		ocfs2_lock_res_init_once(&oi->ip_meta_lockres);
		ocfs2_lock_res_init_once(&oi->ip_data_lockres);

		ocfs2_metadata_cache_init(&oi->vfs_inode);

		inode_init_once(&oi->vfs_inode);
	}
}

static int ocfs2_initialize_mem_caches(void)
{
	ocfs2_inode_cachep = kmem_cache_create("ocfs2_inode_cache",
				       sizeof(struct ocfs2_inode_info),
				       0,
				       (SLAB_HWCACHE_ALIGN|SLAB_RECLAIM_ACCOUNT|
						SLAB_MEM_SPREAD),
				       ocfs2_inode_init_once, NULL);
	if (!ocfs2_inode_cachep)
		return -ENOMEM;

	return 0;
}

static void ocfs2_free_mem_caches(void)
{
	if (ocfs2_inode_cachep)
		kmem_cache_destroy(ocfs2_inode_cachep);

	ocfs2_inode_cachep = NULL;
}

static int ocfs2_get_sector(struct super_block *sb,
			    struct buffer_head **bh,
			    int block,
			    int sect_size)
{
	if (!sb_set_blocksize(sb, sect_size)) {
		mlog(ML_ERROR, "unable to set blocksize\n");
		return -EIO;
	}

	*bh = sb_getblk(sb, block);
	if (!*bh) {
		mlog_errno(-EIO);
		return -EIO;
	}
	lock_buffer(*bh);
	if (!buffer_dirty(*bh))
		clear_buffer_uptodate(*bh);
	unlock_buffer(*bh);
	ll_rw_block(READ, 1, bh);
	wait_on_buffer(*bh);
	return 0;
}

/* ocfs2 1.0 only allows one cluster and node identity per kernel image. */
static int ocfs2_fill_local_node_info(struct ocfs2_super *osb)
{
	int status;

	/* XXX hold a ref on the node while mounte?  easy enough, if
	 * desirable. */
	osb->node_num = o2nm_this_node();
	if (osb->node_num == O2NM_MAX_NODES) {
		mlog(ML_ERROR, "could not find this host's node number\n");
		status = -ENOENT;
		goto bail;
	}

	mlog(0, "I am node %d\n", osb->node_num);

	status = 0;
bail:
	return status;
}

static int ocfs2_mount_volume(struct super_block *sb)
{
	int status = 0;
	int unlock_super = 0;
	struct ocfs2_super *osb = OCFS2_SB(sb);

	mlog_entry_void();

	if (ocfs2_is_hard_readonly(osb))
		goto leave;

	status = ocfs2_fill_local_node_info(osb);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	status = ocfs2_register_hb_callbacks(osb);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	status = ocfs2_dlm_init(osb);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	/* requires vote_thread to be running. */
	status = ocfs2_register_net_handlers(osb);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	status = ocfs2_super_lock(osb, 1);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}
	unlock_super = 1;

	/* This will load up the node map and add ourselves to it. */
	status = ocfs2_find_slot(osb);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	ocfs2_populate_mounted_map(osb);

	/* load all node-local system inodes */
	status = ocfs2_init_local_system_inodes(osb);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	status = ocfs2_check_volume(osb);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	status = ocfs2_truncate_log_init(osb);
	if (status < 0) {
		mlog_errno(status);
		goto leave;
	}

	/* This should be sent *after* we recovered our journal as it
	 * will cause other nodes to unmark us as needing
	 * recovery. However, we need to send it *before* dropping the
	 * super block lock as otherwise their recovery threads might
	 * try to clean us up while we're live! */
	status = ocfs2_request_mount_vote(osb);
	if (status < 0)
		mlog_errno(status);

leave:
	if (unlock_super)
		ocfs2_super_unlock(osb, 1);

	mlog_exit(status);
	return status;
}

/* we can't grab the goofy sem lock from inside wait_event, so we use
 * memory barriers to make sure that we'll see the null task before
 * being woken up */
static int ocfs2_recovery_thread_running(struct ocfs2_super *osb)
{
	mb();
	return osb->recovery_thread_task != NULL;
}

static void ocfs2_dismount_volume(struct super_block *sb, int mnt_err)
{
	int tmp;
	struct ocfs2_super *osb = NULL;

	mlog_entry("(0x%p)\n", sb);

	BUG_ON(!sb);
	osb = OCFS2_SB(sb);
	BUG_ON(!osb);

	ocfs2_shutdown_local_alloc(osb);

	ocfs2_truncate_log_shutdown(osb);

	/* disable any new recovery threads and wait for any currently
	 * running ones to exit. Do this before setting the vol_state. */
	mutex_lock(&osb->recovery_lock);
	osb->disable_recovery = 1;
	mutex_unlock(&osb->recovery_lock);
	wait_event(osb->recovery_event, !ocfs2_recovery_thread_running(osb));

	/* At this point, we know that no more recovery threads can be
	 * launched, so wait for any recovery completion work to
	 * complete. */
	flush_workqueue(ocfs2_wq);

	ocfs2_journal_shutdown(osb);

	ocfs2_sync_blockdev(sb);

	/* No dlm means we've failed during mount, so skip all the
	 * steps which depended on that to complete. */
	if (osb->dlm) {
		tmp = ocfs2_super_lock(osb, 1);
		if (tmp < 0) {
			mlog_errno(tmp);
			return;
		}

		tmp = ocfs2_request_umount_vote(osb);
		if (tmp < 0)
			mlog_errno(tmp);

		if (osb->slot_num != OCFS2_INVALID_SLOT)
			ocfs2_put_slot(osb);

		ocfs2_super_unlock(osb, 1);
	}

	ocfs2_release_system_inodes(osb);

	if (osb->dlm) {
		ocfs2_unregister_net_handlers(osb);

		ocfs2_dlm_shutdown(osb);
	}

	ocfs2_clear_hb_callbacks(osb);

	debugfs_remove(osb->osb_debug_root);

	if (!mnt_err)
		ocfs2_stop_heartbeat(osb);

	atomic_set(&osb->vol_state, VOLUME_DISMOUNTED);

	printk(KERN_INFO "ocfs2: Unmounting device (%s) on (node %d)\n",
	       osb->dev_str, osb->node_num);

	ocfs2_delete_osb(osb);
	kfree(osb);
	sb->s_dev = 0;
	sb->s_fs_info = NULL;
}

static int ocfs2_setup_osb_uuid(struct ocfs2_super *osb, const unsigned char *uuid,
				unsigned uuid_bytes)
{
	int i, ret;
	char *ptr;

	BUG_ON(uuid_bytes != OCFS2_VOL_UUID_LEN);

	osb->uuid_str = kcalloc(1, OCFS2_VOL_UUID_LEN * 2 + 1, GFP_KERNEL);
	if (osb->uuid_str == NULL)
		return -ENOMEM;

	for (i = 0, ptr = osb->uuid_str; i < OCFS2_VOL_UUID_LEN; i++) {
		/* print with null */
		ret = snprintf(ptr, 3, "%02X", uuid[i]);
		if (ret != 2) /* drop super cleans up */
			return -EINVAL;
		/* then only advance past the last char */
		ptr += 2;
	}

	return 0;
}

static int ocfs2_initialize_super(struct super_block *sb,
				  struct buffer_head *bh,
				  int sector_size)
{
	int status = 0;
	int i;
	struct ocfs2_dinode *di = NULL;
	struct inode *inode = NULL;
	struct buffer_head *bitmap_bh = NULL;
	struct ocfs2_journal *journal;
	__le32 uuid_net_key;
	struct ocfs2_super *osb;

	mlog_entry_void();

	osb = kcalloc(1, sizeof(struct ocfs2_super), GFP_KERNEL);
	if (!osb) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	sb->s_fs_info = osb;
	sb->s_op = &ocfs2_sops;
	sb->s_export_op = &ocfs2_export_ops;
	sb->s_flags |= MS_NOATIME;
	/* this is needed to support O_LARGEFILE */
	sb->s_maxbytes = ocfs2_max_file_offset(sb->s_blocksize_bits);

	osb->sb = sb;
	/* Save off for ocfs2_rw_direct */
	osb->s_sectsize_bits = blksize_bits(sector_size);
	BUG_ON(!osb->s_sectsize_bits);

	osb->net_response_ids = 0;
	spin_lock_init(&osb->net_response_lock);
	INIT_LIST_HEAD(&osb->net_response_list);

	INIT_LIST_HEAD(&osb->osb_net_handlers);
	init_waitqueue_head(&osb->recovery_event);
	spin_lock_init(&osb->vote_task_lock);
	init_waitqueue_head(&osb->vote_event);
	osb->vote_work_sequence = 0;
	osb->vote_wake_sequence = 0;
	INIT_LIST_HEAD(&osb->blocked_lock_list);
	osb->blocked_lock_count = 0;
	INIT_LIST_HEAD(&osb->vote_list);
	spin_lock_init(&osb->osb_lock);

	atomic_set(&osb->alloc_stats.moves, 0);
	atomic_set(&osb->alloc_stats.local_data, 0);
	atomic_set(&osb->alloc_stats.bitmap_data, 0);
	atomic_set(&osb->alloc_stats.bg_allocs, 0);
	atomic_set(&osb->alloc_stats.bg_extends, 0);

	ocfs2_init_node_maps(osb);

	snprintf(osb->dev_str, sizeof(osb->dev_str), "%u,%u",
		 MAJOR(osb->sb->s_dev), MINOR(osb->sb->s_dev));

	mutex_init(&osb->recovery_lock);

	osb->disable_recovery = 0;
	osb->recovery_thread_task = NULL;

	init_waitqueue_head(&osb->checkpoint_event);
	atomic_set(&osb->needs_checkpoint, 0);

	osb->s_atime_quantum = OCFS2_DEFAULT_ATIME_QUANTUM;

	osb->node_num = O2NM_INVALID_NODE_NUM;
	osb->slot_num = OCFS2_INVALID_SLOT;

	osb->local_alloc_state = OCFS2_LA_UNUSED;
	osb->local_alloc_bh = NULL;

	ocfs2_setup_hb_callbacks(osb);

	init_waitqueue_head(&osb->osb_mount_event);

	osb->vol_label = kmalloc(OCFS2_MAX_VOL_LABEL_LEN, GFP_KERNEL);
	if (!osb->vol_label) {
		mlog(ML_ERROR, "unable to alloc vol label\n");
		status = -ENOMEM;
		goto bail;
	}

	di = (struct ocfs2_dinode *)bh->b_data;

	osb->max_slots = le16_to_cpu(di->id2.i_super.s_max_slots);
	if (osb->max_slots > OCFS2_MAX_SLOTS || osb->max_slots == 0) {
		mlog(ML_ERROR, "Invalid number of node slots (%u)\n",
		     osb->max_slots);
		status = -EINVAL;
		goto bail;
	}
	mlog(0, "max_slots for this device: %u\n", osb->max_slots);

	init_waitqueue_head(&osb->osb_wipe_event);
	osb->osb_orphan_wipes = kcalloc(osb->max_slots,
					sizeof(*osb->osb_orphan_wipes),
					GFP_KERNEL);
	if (!osb->osb_orphan_wipes) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	osb->s_feature_compat =
		le32_to_cpu(OCFS2_RAW_SB(di)->s_feature_compat);
	osb->s_feature_ro_compat =
		le32_to_cpu(OCFS2_RAW_SB(di)->s_feature_ro_compat);
	osb->s_feature_incompat =
		le32_to_cpu(OCFS2_RAW_SB(di)->s_feature_incompat);

	if ((i = OCFS2_HAS_INCOMPAT_FEATURE(osb->sb, ~OCFS2_FEATURE_INCOMPAT_SUPP))) {
		mlog(ML_ERROR, "couldn't mount because of unsupported "
		     "optional features (%x).\n", i);
		status = -EINVAL;
		goto bail;
	}
	if (!(osb->sb->s_flags & MS_RDONLY) &&
	    (i = OCFS2_HAS_RO_COMPAT_FEATURE(osb->sb, ~OCFS2_FEATURE_RO_COMPAT_SUPP))) {
		mlog(ML_ERROR, "couldn't mount RDWR because of "
		     "unsupported optional features (%x).\n", i);
		status = -EINVAL;
		goto bail;
	}

	get_random_bytes(&osb->s_next_generation, sizeof(u32));

	/* FIXME
	 * This should be done in ocfs2_journal_init(), but unknown
	 * ordering issues will cause the filesystem to crash.
	 * If anyone wants to figure out what part of the code
	 * refers to osb->journal before ocfs2_journal_init() is run,
	 * be my guest.
	 */
	/* initialize our journal structure */

	journal = kcalloc(1, sizeof(struct ocfs2_journal), GFP_KERNEL);
	if (!journal) {
		mlog(ML_ERROR, "unable to alloc journal\n");
		status = -ENOMEM;
		goto bail;
	}
	osb->journal = journal;
	journal->j_osb = osb;

	atomic_set(&journal->j_num_trans, 0);
	init_rwsem(&journal->j_trans_barrier);
	init_waitqueue_head(&journal->j_checkpointed);
	spin_lock_init(&journal->j_lock);
	journal->j_trans_id = (unsigned long) 1;
	INIT_LIST_HEAD(&journal->j_la_cleanups);
	INIT_WORK(&journal->j_recovery_work, ocfs2_complete_recovery);
	journal->j_state = OCFS2_JOURNAL_FREE;

	/* get some pseudo constants for clustersize bits */
	osb->s_clustersize_bits =
		le32_to_cpu(di->id2.i_super.s_clustersize_bits);
	osb->s_clustersize = 1 << osb->s_clustersize_bits;
	mlog(0, "clusterbits=%d\n", osb->s_clustersize_bits);

	if (osb->s_clustersize < OCFS2_MIN_CLUSTERSIZE ||
	    osb->s_clustersize > OCFS2_MAX_CLUSTERSIZE) {
		mlog(ML_ERROR, "Volume has invalid cluster size (%d)\n",
		     osb->s_clustersize);
		status = -EINVAL;
		goto bail;
	}

	if (ocfs2_clusters_to_blocks(osb->sb, le32_to_cpu(di->i_clusters) - 1)
	    > (u32)~0UL) {
		mlog(ML_ERROR, "Volume might try to write to blocks beyond "
		     "what jbd can address in 32 bits.\n");
		status = -EINVAL;
		goto bail;
	}

	if (ocfs2_setup_osb_uuid(osb, di->id2.i_super.s_uuid,
				 sizeof(di->id2.i_super.s_uuid))) {
		mlog(ML_ERROR, "Out of memory trying to setup our uuid.\n");
		status = -ENOMEM;
		goto bail;
	}

	memcpy(&uuid_net_key, di->id2.i_super.s_uuid, sizeof(uuid_net_key));
	osb->net_key = le32_to_cpu(uuid_net_key);

	strncpy(osb->vol_label, di->id2.i_super.s_label, 63);
	osb->vol_label[63] = '\0';
	osb->root_blkno = le64_to_cpu(di->id2.i_super.s_root_blkno);
	osb->system_dir_blkno = le64_to_cpu(di->id2.i_super.s_system_dir_blkno);
	osb->first_cluster_group_blkno =
		le64_to_cpu(di->id2.i_super.s_first_cluster_group);
	osb->fs_generation = le32_to_cpu(di->i_fs_generation);
	mlog(0, "vol_label: %s\n", osb->vol_label);
	mlog(0, "uuid: %s\n", osb->uuid_str);
	mlog(0, "root_blkno=%llu, system_dir_blkno=%llu\n",
	     (unsigned long long)osb->root_blkno,
	     (unsigned long long)osb->system_dir_blkno);

	osb->osb_dlm_debug = ocfs2_new_dlm_debug();
	if (!osb->osb_dlm_debug) {
		status = -ENOMEM;
		mlog_errno(status);
		goto bail;
	}

	atomic_set(&osb->vol_state, VOLUME_INIT);

	/* load root, system_dir, and all global system inodes */
	status = ocfs2_init_global_system_inodes(osb);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	/*
	 * global bitmap
	 */
	inode = ocfs2_get_system_file_inode(osb, GLOBAL_BITMAP_SYSTEM_INODE,
					    OCFS2_INVALID_SLOT);
	if (!inode) {
		status = -EINVAL;
		mlog_errno(status);
		goto bail;
	}

	osb->bitmap_blkno = OCFS2_I(inode)->ip_blkno;

	/* We don't have a cluster lock on the bitmap here because
	 * we're only interested in static information and the extra
	 * complexity at mount time isn't worht it. Don't pass the
	 * inode in to the read function though as we don't want it to
	 * be put in the cache. */
	status = ocfs2_read_block(osb, osb->bitmap_blkno, &bitmap_bh, 0,
				  NULL);
	iput(inode);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

	di = (struct ocfs2_dinode *) bitmap_bh->b_data;
	osb->bitmap_cpg = le16_to_cpu(di->id2.i_chain.cl_cpg);
	brelse(bitmap_bh);
	mlog(0, "cluster bitmap inode: %llu, clusters per group: %u\n",
	     (unsigned long long)osb->bitmap_blkno, osb->bitmap_cpg);

	status = ocfs2_init_slot_info(osb);
	if (status < 0) {
		mlog_errno(status);
		goto bail;
	}

bail:
	mlog_exit(status);
	return status;
}

/*
 * will return: -EAGAIN if it is ok to keep searching for superblocks
 *              -EINVAL if there is a bad superblock
 *              0 on success
 */
static int ocfs2_verify_volume(struct ocfs2_dinode *di,
			       struct buffer_head *bh,
			       u32 blksz)
{
	int status = -EAGAIN;

	mlog_entry_void();

	if (memcmp(di->i_signature, OCFS2_SUPER_BLOCK_SIGNATURE,
		   strlen(OCFS2_SUPER_BLOCK_SIGNATURE)) == 0) {
		status = -EINVAL;
		if ((1 << le32_to_cpu(di->id2.i_super.s_blocksize_bits)) != blksz) {
			mlog(ML_ERROR, "found superblock with incorrect block "
			     "size: found %u, should be %u\n",
			     1 << le32_to_cpu(di->id2.i_super.s_blocksize_bits),
			       blksz);
		} else if (le16_to_cpu(di->id2.i_super.s_major_rev_level) !=
			   OCFS2_MAJOR_REV_LEVEL ||
			   le16_to_cpu(di->id2.i_super.s_minor_rev_level) !=
			   OCFS2_MINOR_REV_LEVEL) {
			mlog(ML_ERROR, "found superblock with bad version: "
			     "found %u.%u, should be %u.%u\n",
			     le16_to_cpu(di->id2.i_super.s_major_rev_level),
			     le16_to_cpu(di->id2.i_super.s_minor_rev_level),
			     OCFS2_MAJOR_REV_LEVEL,
			     OCFS2_MINOR_REV_LEVEL);
		} else if (bh->b_blocknr != le64_to_cpu(di->i_blkno)) {
			mlog(ML_ERROR, "bad block number on superblock: "
			     "found %llu, should be %llu\n",
			     (unsigned long long)di->i_blkno,
			     (unsigned long long)bh->b_blocknr);
		} else if (le32_to_cpu(di->id2.i_super.s_clustersize_bits) < 12 ||
			    le32_to_cpu(di->id2.i_super.s_clustersize_bits) > 20) {
			mlog(ML_ERROR, "bad cluster size found: %u\n",
			     1 << le32_to_cpu(di->id2.i_super.s_clustersize_bits));
		} else if (!le64_to_cpu(di->id2.i_super.s_root_blkno)) {
			mlog(ML_ERROR, "bad root_blkno: 0\n");
		} else if (!le64_to_cpu(di->id2.i_super.s_system_dir_blkno)) {
			mlog(ML_ERROR, "bad system_dir_blkno: 0\n");
		} else if (le16_to_cpu(di->id2.i_super.s_max_slots) > OCFS2_MAX_SLOTS) {
			mlog(ML_ERROR,
			     "Superblock slots found greater than file system "
			     "maximum: found %u, max %u\n",
			     le16_to_cpu(di->id2.i_super.s_max_slots),
			     OCFS2_MAX_SLOTS);
		} else {
			/* found it! */
			status = 0;
		}
	}

	mlog_exit(status);
	return status;
}

static int ocfs2_check_volume(struct ocfs2_super *osb)
{
	int status = 0;
	int dirty;
	struct ocfs2_dinode *local_alloc = NULL; /* only used if we
						  * recover
						  * ourselves. */

	mlog_entry_void();

	/* Init our journal object. */
	status = ocfs2_journal_init(osb->journal, &dirty);
	if (status < 0) {
		mlog(ML_ERROR, "Could not initialize journal!\n");
		goto finally;
	}

	/* If the journal was unmounted cleanly then we don't want to
	 * recover anything. Otherwise, journal_load will do that
	 * dirty work for us :) */
	if (!dirty) {
		status = ocfs2_journal_wipe(osb->journal, 0);
		if (status < 0) {
			mlog_errno(status);
			goto finally;
		}
	} else {
		mlog(ML_NOTICE, "File system was not unmounted cleanly, "
		     "recovering volume.\n");
	}

	/* will play back anything left in the journal. */
	ocfs2_journal_load(osb->journal);

	if (dirty) {
		/* recover my local alloc if we didn't unmount cleanly. */
		status = ocfs2_begin_local_alloc_recovery(osb,
							  osb->slot_num,
							  &local_alloc);
		if (status < 0) {
			mlog_errno(status);
			goto finally;
		}
		/* we complete the recovery process after we've marked
		 * ourselves as mounted. */
	}

	mlog(0, "Journal loaded.\n");

	status = ocfs2_load_local_alloc(osb);
	if (status < 0) {
		mlog_errno(status);
		goto finally;
	}

	if (dirty) {
		/* Recovery will be completed after we've mounted the
		 * rest of the volume. */
		osb->dirty = 1;
		osb->local_alloc_copy = local_alloc;
		local_alloc = NULL;
	}

	/* go through each journal, trylock it and if you get the
	 * lock, and it's marked as dirty, set the bit in the recover
	 * map and launch a recovery thread for it. */
	status = ocfs2_mark_dead_nodes(osb);
	if (status < 0)
		mlog_errno(status);

finally:
	if (local_alloc)
		kfree(local_alloc);

	mlog_exit(status);
	return status;
}

/*
 * The routine gets called from dismount or close whenever a dismount on
 * volume is requested and the osb open count becomes 1.
 * It will remove the osb from the global list and also free up all the
 * initialized resources and fileobject.
 */
static void ocfs2_delete_osb(struct ocfs2_super *osb)
{
	mlog_entry_void();

	/* This function assumes that the caller has the main osb resource */

	if (osb->slot_info)
		ocfs2_free_slot_info(osb->slot_info);

	kfree(osb->osb_orphan_wipes);
	/* FIXME
	 * This belongs in journal shutdown, but because we have to
	 * allocate osb->journal at the start of ocfs2_initalize_osb(),
	 * we free it here.
	 */
	kfree(osb->journal);
	if (osb->local_alloc_copy)
		kfree(osb->local_alloc_copy);
	kfree(osb->uuid_str);
	ocfs2_put_dlm_debug(osb->osb_dlm_debug);
	memset(osb, 0, sizeof(struct ocfs2_super));

	mlog_exit_void();
}

/* Put OCFS2 into a readonly state, or (if the user specifies it),
 * panic(). We do not support continue-on-error operation. */
static void ocfs2_handle_error(struct super_block *sb)
{
	struct ocfs2_super *osb = OCFS2_SB(sb);

	if (osb->s_mount_opt & OCFS2_MOUNT_ERRORS_PANIC)
		panic("OCFS2: (device %s): panic forced after error\n",
		      sb->s_id);

	ocfs2_set_osb_flag(osb, OCFS2_OSB_ERROR_FS);

	if (sb->s_flags & MS_RDONLY &&
	    (ocfs2_is_soft_readonly(osb) ||
	     ocfs2_is_hard_readonly(osb)))
		return;

	printk(KERN_CRIT "File system is now read-only due to the potential "
	       "of on-disk corruption. Please run fsck.ocfs2 once the file "
	       "system is unmounted.\n");
	sb->s_flags |= MS_RDONLY;
	ocfs2_set_ro_flag(osb, 0);
}

static char error_buf[1024];

void __ocfs2_error(struct super_block *sb,
		   const char *function,
		   const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(error_buf, sizeof(error_buf), fmt, args);
	va_end(args);

	/* Not using mlog here because we want to show the actual
	 * function the error came from. */
	printk(KERN_CRIT "OCFS2: ERROR (device %s): %s: %s\n",
	       sb->s_id, function, error_buf);

	ocfs2_handle_error(sb);
}

/* Handle critical errors. This is intentionally more drastic than
 * ocfs2_handle_error, so we only use for things like journal errors,
 * etc. */
void __ocfs2_abort(struct super_block* sb,
		   const char *function,
		   const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	vsnprintf(error_buf, sizeof(error_buf), fmt, args);
	va_end(args);

	printk(KERN_CRIT "OCFS2: abort (device %s): %s: %s\n",
	       sb->s_id, function, error_buf);

	/* We don't have the cluster support yet to go straight to
	 * hard readonly in here. Until then, we want to keep
	 * ocfs2_abort() so that we can at least mark critical
	 * errors.
	 *
	 * TODO: This should abort the journal and alert other nodes
	 * that our slot needs recovery. */

	/* Force a panic(). This stinks, but it's better than letting
	 * things continue without having a proper hard readonly
	 * here. */
	OCFS2_SB(sb)->s_mount_opt |= OCFS2_MOUNT_ERRORS_PANIC;
	ocfs2_handle_error(sb);
}

module_init(ocfs2_init);
module_exit(ocfs2_exit);
