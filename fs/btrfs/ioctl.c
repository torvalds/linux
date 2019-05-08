// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 */

#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/mount.h>
#include <linux/namei.h>
#include <linux/writeback.h>
#include <linux/compat.h>
#include <linux/security.h>
#include <linux/xattr.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include <linux/uuid.h>
#include <linux/btrfs.h>
#include <linux/uaccess.h>
#include <linux/iversion.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "print-tree.h"
#include "volumes.h"
#include "locking.h"
#include "inode-map.h"
#include "backref.h"
#include "rcu-string.h"
#include "send.h"
#include "dev-replace.h"
#include "props.h"
#include "sysfs.h"
#include "qgroup.h"
#include "tree-log.h"
#include "compression.h"

#ifdef CONFIG_64BIT
/* If we have a 32-bit userspace and 64-bit kernel, then the UAPI
 * structures are incorrect, as the timespec structure from userspace
 * is 4 bytes too small. We define these alternatives here to teach
 * the kernel about the 32-bit struct packing.
 */
struct btrfs_ioctl_timespec_32 {
	__u64 sec;
	__u32 nsec;
} __attribute__ ((__packed__));

struct btrfs_ioctl_received_subvol_args_32 {
	char	uuid[BTRFS_UUID_SIZE];	/* in */
	__u64	stransid;		/* in */
	__u64	rtransid;		/* out */
	struct btrfs_ioctl_timespec_32 stime; /* in */
	struct btrfs_ioctl_timespec_32 rtime; /* out */
	__u64	flags;			/* in */
	__u64	reserved[16];		/* in */
} __attribute__ ((__packed__));

#define BTRFS_IOC_SET_RECEIVED_SUBVOL_32 _IOWR(BTRFS_IOCTL_MAGIC, 37, \
				struct btrfs_ioctl_received_subvol_args_32)
#endif

#if defined(CONFIG_64BIT) && defined(CONFIG_COMPAT)
struct btrfs_ioctl_send_args_32 {
	__s64 send_fd;			/* in */
	__u64 clone_sources_count;	/* in */
	compat_uptr_t clone_sources;	/* in */
	__u64 parent_root;		/* in */
	__u64 flags;			/* in */
	__u64 reserved[4];		/* in */
} __attribute__ ((__packed__));

#define BTRFS_IOC_SEND_32 _IOW(BTRFS_IOCTL_MAGIC, 38, \
			       struct btrfs_ioctl_send_args_32)
#endif

static int btrfs_clone(struct inode *src, struct inode *inode,
		       u64 off, u64 olen, u64 olen_aligned, u64 destoff,
		       int no_time_update);

/* Mask out flags that are inappropriate for the given type of inode. */
static unsigned int btrfs_mask_fsflags_for_type(struct inode *inode,
		unsigned int flags)
{
	if (S_ISDIR(inode->i_mode))
		return flags;
	else if (S_ISREG(inode->i_mode))
		return flags & ~FS_DIRSYNC_FL;
	else
		return flags & (FS_NODUMP_FL | FS_NOATIME_FL);
}

/*
 * Export internal inode flags to the format expected by the FS_IOC_GETFLAGS
 * ioctl.
 */
static unsigned int btrfs_inode_flags_to_fsflags(unsigned int flags)
{
	unsigned int iflags = 0;

	if (flags & BTRFS_INODE_SYNC)
		iflags |= FS_SYNC_FL;
	if (flags & BTRFS_INODE_IMMUTABLE)
		iflags |= FS_IMMUTABLE_FL;
	if (flags & BTRFS_INODE_APPEND)
		iflags |= FS_APPEND_FL;
	if (flags & BTRFS_INODE_NODUMP)
		iflags |= FS_NODUMP_FL;
	if (flags & BTRFS_INODE_NOATIME)
		iflags |= FS_NOATIME_FL;
	if (flags & BTRFS_INODE_DIRSYNC)
		iflags |= FS_DIRSYNC_FL;
	if (flags & BTRFS_INODE_NODATACOW)
		iflags |= FS_NOCOW_FL;

	if (flags & BTRFS_INODE_NOCOMPRESS)
		iflags |= FS_NOCOMP_FL;
	else if (flags & BTRFS_INODE_COMPRESS)
		iflags |= FS_COMPR_FL;

	return iflags;
}

/*
 * Update inode->i_flags based on the btrfs internal flags.
 */
void btrfs_sync_inode_flags_to_i_flags(struct inode *inode)
{
	struct btrfs_inode *binode = BTRFS_I(inode);
	unsigned int new_fl = 0;

	if (binode->flags & BTRFS_INODE_SYNC)
		new_fl |= S_SYNC;
	if (binode->flags & BTRFS_INODE_IMMUTABLE)
		new_fl |= S_IMMUTABLE;
	if (binode->flags & BTRFS_INODE_APPEND)
		new_fl |= S_APPEND;
	if (binode->flags & BTRFS_INODE_NOATIME)
		new_fl |= S_NOATIME;
	if (binode->flags & BTRFS_INODE_DIRSYNC)
		new_fl |= S_DIRSYNC;

	set_mask_bits(&inode->i_flags,
		      S_SYNC | S_APPEND | S_IMMUTABLE | S_NOATIME | S_DIRSYNC,
		      new_fl);
}

static int btrfs_ioctl_getflags(struct file *file, void __user *arg)
{
	struct btrfs_inode *binode = BTRFS_I(file_inode(file));
	unsigned int flags = btrfs_inode_flags_to_fsflags(binode->flags);

	if (copy_to_user(arg, &flags, sizeof(flags)))
		return -EFAULT;
	return 0;
}

/* Check if @flags are a supported and valid set of FS_*_FL flags */
static int check_fsflags(unsigned int flags)
{
	if (flags & ~(FS_IMMUTABLE_FL | FS_APPEND_FL | \
		      FS_NOATIME_FL | FS_NODUMP_FL | \
		      FS_SYNC_FL | FS_DIRSYNC_FL | \
		      FS_NOCOMP_FL | FS_COMPR_FL |
		      FS_NOCOW_FL))
		return -EOPNOTSUPP;

	if ((flags & FS_NOCOMP_FL) && (flags & FS_COMPR_FL))
		return -EINVAL;

	return 0;
}

static int btrfs_ioctl_setflags(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_inode *binode = BTRFS_I(inode);
	struct btrfs_root *root = binode->root;
	struct btrfs_trans_handle *trans;
	unsigned int fsflags;
	int ret;
	const char *comp = NULL;
	u32 binode_flags = binode->flags;

	if (!inode_owner_or_capable(inode))
		return -EPERM;

	if (btrfs_root_readonly(root))
		return -EROFS;

	if (copy_from_user(&fsflags, arg, sizeof(fsflags)))
		return -EFAULT;

	ret = check_fsflags(fsflags);
	if (ret)
		return ret;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	inode_lock(inode);

	fsflags = btrfs_mask_fsflags_for_type(inode, fsflags);
	if ((fsflags ^ btrfs_inode_flags_to_fsflags(binode->flags)) &
	    (FS_APPEND_FL | FS_IMMUTABLE_FL)) {
		if (!capable(CAP_LINUX_IMMUTABLE)) {
			ret = -EPERM;
			goto out_unlock;
		}
	}

	if (fsflags & FS_SYNC_FL)
		binode_flags |= BTRFS_INODE_SYNC;
	else
		binode_flags &= ~BTRFS_INODE_SYNC;
	if (fsflags & FS_IMMUTABLE_FL)
		binode_flags |= BTRFS_INODE_IMMUTABLE;
	else
		binode_flags &= ~BTRFS_INODE_IMMUTABLE;
	if (fsflags & FS_APPEND_FL)
		binode_flags |= BTRFS_INODE_APPEND;
	else
		binode_flags &= ~BTRFS_INODE_APPEND;
	if (fsflags & FS_NODUMP_FL)
		binode_flags |= BTRFS_INODE_NODUMP;
	else
		binode_flags &= ~BTRFS_INODE_NODUMP;
	if (fsflags & FS_NOATIME_FL)
		binode_flags |= BTRFS_INODE_NOATIME;
	else
		binode_flags &= ~BTRFS_INODE_NOATIME;
	if (fsflags & FS_DIRSYNC_FL)
		binode_flags |= BTRFS_INODE_DIRSYNC;
	else
		binode_flags &= ~BTRFS_INODE_DIRSYNC;
	if (fsflags & FS_NOCOW_FL) {
		if (S_ISREG(inode->i_mode)) {
			/*
			 * It's safe to turn csums off here, no extents exist.
			 * Otherwise we want the flag to reflect the real COW
			 * status of the file and will not set it.
			 */
			if (inode->i_size == 0)
				binode_flags |= BTRFS_INODE_NODATACOW |
						BTRFS_INODE_NODATASUM;
		} else {
			binode_flags |= BTRFS_INODE_NODATACOW;
		}
	} else {
		/*
		 * Revert back under same assumptions as above
		 */
		if (S_ISREG(inode->i_mode)) {
			if (inode->i_size == 0)
				binode_flags &= ~(BTRFS_INODE_NODATACOW |
						  BTRFS_INODE_NODATASUM);
		} else {
			binode_flags &= ~BTRFS_INODE_NODATACOW;
		}
	}

	/*
	 * The COMPRESS flag can only be changed by users, while the NOCOMPRESS
	 * flag may be changed automatically if compression code won't make
	 * things smaller.
	 */
	if (fsflags & FS_NOCOMP_FL) {
		binode_flags &= ~BTRFS_INODE_COMPRESS;
		binode_flags |= BTRFS_INODE_NOCOMPRESS;
	} else if (fsflags & FS_COMPR_FL) {

		if (IS_SWAPFILE(inode)) {
			ret = -ETXTBSY;
			goto out_unlock;
		}

		binode_flags |= BTRFS_INODE_COMPRESS;
		binode_flags &= ~BTRFS_INODE_NOCOMPRESS;

		comp = btrfs_compress_type2str(fs_info->compress_type);
		if (!comp || comp[0] == 0)
			comp = btrfs_compress_type2str(BTRFS_COMPRESS_ZLIB);
	} else {
		binode_flags &= ~(BTRFS_INODE_COMPRESS | BTRFS_INODE_NOCOMPRESS);
	}

	/*
	 * 1 for inode item
	 * 2 for properties
	 */
	trans = btrfs_start_transaction(root, 3);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_unlock;
	}

	if (comp) {
		ret = btrfs_set_prop(trans, inode, "btrfs.compression", comp,
				     strlen(comp), 0);
		if (ret) {
			btrfs_abort_transaction(trans, ret);
			goto out_end_trans;
		}
	} else {
		ret = btrfs_set_prop(trans, inode, "btrfs.compression", NULL,
				     0, 0);
		if (ret && ret != -ENODATA) {
			btrfs_abort_transaction(trans, ret);
			goto out_end_trans;
		}
	}

	binode->flags = binode_flags;
	btrfs_sync_inode_flags_to_i_flags(inode);
	inode_inc_iversion(inode);
	inode->i_ctime = current_time(inode);
	ret = btrfs_update_inode(trans, root, inode);

 out_end_trans:
	btrfs_end_transaction(trans);
 out_unlock:
	inode_unlock(inode);
	mnt_drop_write_file(file);
	return ret;
}

/*
 * Translate btrfs internal inode flags to xflags as expected by the
 * FS_IOC_FSGETXATT ioctl. Filter only the supported ones, unknown flags are
 * silently dropped.
 */
static unsigned int btrfs_inode_flags_to_xflags(unsigned int flags)
{
	unsigned int xflags = 0;

	if (flags & BTRFS_INODE_APPEND)
		xflags |= FS_XFLAG_APPEND;
	if (flags & BTRFS_INODE_IMMUTABLE)
		xflags |= FS_XFLAG_IMMUTABLE;
	if (flags & BTRFS_INODE_NOATIME)
		xflags |= FS_XFLAG_NOATIME;
	if (flags & BTRFS_INODE_NODUMP)
		xflags |= FS_XFLAG_NODUMP;
	if (flags & BTRFS_INODE_SYNC)
		xflags |= FS_XFLAG_SYNC;

	return xflags;
}

/* Check if @flags are a supported and valid set of FS_XFLAGS_* flags */
static int check_xflags(unsigned int flags)
{
	if (flags & ~(FS_XFLAG_APPEND | FS_XFLAG_IMMUTABLE | FS_XFLAG_NOATIME |
		      FS_XFLAG_NODUMP | FS_XFLAG_SYNC))
		return -EOPNOTSUPP;
	return 0;
}

/*
 * Set the xflags from the internal inode flags. The remaining items of fsxattr
 * are zeroed.
 */
static int btrfs_ioctl_fsgetxattr(struct file *file, void __user *arg)
{
	struct btrfs_inode *binode = BTRFS_I(file_inode(file));
	struct fsxattr fa;

	memset(&fa, 0, sizeof(fa));
	fa.fsx_xflags = btrfs_inode_flags_to_xflags(binode->flags);

	if (copy_to_user(arg, &fa, sizeof(fa)))
		return -EFAULT;

	return 0;
}

static int btrfs_ioctl_fssetxattr(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_inode *binode = BTRFS_I(inode);
	struct btrfs_root *root = binode->root;
	struct btrfs_trans_handle *trans;
	struct fsxattr fa;
	unsigned old_flags;
	unsigned old_i_flags;
	int ret = 0;

	if (!inode_owner_or_capable(inode))
		return -EPERM;

	if (btrfs_root_readonly(root))
		return -EROFS;

	memset(&fa, 0, sizeof(fa));
	if (copy_from_user(&fa, arg, sizeof(fa)))
		return -EFAULT;

	ret = check_xflags(fa.fsx_xflags);
	if (ret)
		return ret;

	if (fa.fsx_extsize != 0 || fa.fsx_projid != 0 || fa.fsx_cowextsize != 0)
		return -EOPNOTSUPP;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	inode_lock(inode);

	old_flags = binode->flags;
	old_i_flags = inode->i_flags;

	/* We need the capabilities to change append-only or immutable inode */
	if (((old_flags & (BTRFS_INODE_APPEND | BTRFS_INODE_IMMUTABLE)) ||
	     (fa.fsx_xflags & (FS_XFLAG_APPEND | FS_XFLAG_IMMUTABLE))) &&
	    !capable(CAP_LINUX_IMMUTABLE)) {
		ret = -EPERM;
		goto out_unlock;
	}

	if (fa.fsx_xflags & FS_XFLAG_SYNC)
		binode->flags |= BTRFS_INODE_SYNC;
	else
		binode->flags &= ~BTRFS_INODE_SYNC;
	if (fa.fsx_xflags & FS_XFLAG_IMMUTABLE)
		binode->flags |= BTRFS_INODE_IMMUTABLE;
	else
		binode->flags &= ~BTRFS_INODE_IMMUTABLE;
	if (fa.fsx_xflags & FS_XFLAG_APPEND)
		binode->flags |= BTRFS_INODE_APPEND;
	else
		binode->flags &= ~BTRFS_INODE_APPEND;
	if (fa.fsx_xflags & FS_XFLAG_NODUMP)
		binode->flags |= BTRFS_INODE_NODUMP;
	else
		binode->flags &= ~BTRFS_INODE_NODUMP;
	if (fa.fsx_xflags & FS_XFLAG_NOATIME)
		binode->flags |= BTRFS_INODE_NOATIME;
	else
		binode->flags &= ~BTRFS_INODE_NOATIME;

	/* 1 item for the inode */
	trans = btrfs_start_transaction(root, 1);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_unlock;
	}

	btrfs_sync_inode_flags_to_i_flags(inode);
	inode_inc_iversion(inode);
	inode->i_ctime = current_time(inode);
	ret = btrfs_update_inode(trans, root, inode);

	btrfs_end_transaction(trans);

out_unlock:
	if (ret) {
		binode->flags = old_flags;
		inode->i_flags = old_i_flags;
	}

	inode_unlock(inode);
	mnt_drop_write_file(file);

	return ret;
}

static int btrfs_ioctl_getversion(struct file *file, int __user *arg)
{
	struct inode *inode = file_inode(file);

	return put_user(inode->i_generation, arg);
}

static noinline int btrfs_ioctl_fitrim(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_device *device;
	struct request_queue *q;
	struct fstrim_range range;
	u64 minlen = ULLONG_MAX;
	u64 num_devices = 0;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/*
	 * If the fs is mounted with nologreplay, which requires it to be
	 * mounted in RO mode as well, we can not allow discard on free space
	 * inside block groups, because log trees refer to extents that are not
	 * pinned in a block group's free space cache (pinning the extents is
	 * precisely the first phase of replaying a log tree).
	 */
	if (btrfs_test_opt(fs_info, NOLOGREPLAY))
		return -EROFS;

	rcu_read_lock();
	list_for_each_entry_rcu(device, &fs_info->fs_devices->devices,
				dev_list) {
		if (!device->bdev)
			continue;
		q = bdev_get_queue(device->bdev);
		if (blk_queue_discard(q)) {
			num_devices++;
			minlen = min_t(u64, q->limits.discard_granularity,
				     minlen);
		}
	}
	rcu_read_unlock();

	if (!num_devices)
		return -EOPNOTSUPP;
	if (copy_from_user(&range, arg, sizeof(range)))
		return -EFAULT;

	/*
	 * NOTE: Don't truncate the range using super->total_bytes.  Bytenr of
	 * block group is in the logical address space, which can be any
	 * sectorsize aligned bytenr in  the range [0, U64_MAX].
	 */
	if (range.len < fs_info->sb->s_blocksize)
		return -EINVAL;

	range.minlen = max(range.minlen, minlen);
	ret = btrfs_trim_fs(fs_info, &range);
	if (ret < 0)
		return ret;

	if (copy_to_user(arg, &range, sizeof(range)))
		return -EFAULT;

	return 0;
}

int btrfs_is_empty_uuid(u8 *uuid)
{
	int i;

	for (i = 0; i < BTRFS_UUID_SIZE; i++) {
		if (uuid[i])
			return 0;
	}
	return 1;
}

static noinline int create_subvol(struct inode *dir,
				  struct dentry *dentry,
				  const char *name, int namelen,
				  u64 *async_transid,
				  struct btrfs_qgroup_inherit *inherit)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct btrfs_trans_handle *trans;
	struct btrfs_key key;
	struct btrfs_root_item *root_item;
	struct btrfs_inode_item *inode_item;
	struct extent_buffer *leaf;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_root *new_root;
	struct btrfs_block_rsv block_rsv;
	struct timespec64 cur_time = current_time(dir);
	struct inode *inode;
	int ret;
	int err;
	u64 objectid;
	u64 new_dirid = BTRFS_FIRST_FREE_OBJECTID;
	u64 index = 0;
	uuid_le new_uuid;

	root_item = kzalloc(sizeof(*root_item), GFP_KERNEL);
	if (!root_item)
		return -ENOMEM;

	ret = btrfs_find_free_objectid(fs_info->tree_root, &objectid);
	if (ret)
		goto fail_free;

	/*
	 * Don't create subvolume whose level is not zero. Or qgroup will be
	 * screwed up since it assumes subvolume qgroup's level to be 0.
	 */
	if (btrfs_qgroup_level(objectid)) {
		ret = -ENOSPC;
		goto fail_free;
	}

	btrfs_init_block_rsv(&block_rsv, BTRFS_BLOCK_RSV_TEMP);
	/*
	 * The same as the snapshot creation, please see the comment
	 * of create_snapshot().
	 */
	ret = btrfs_subvolume_reserve_metadata(root, &block_rsv, 8, false);
	if (ret)
		goto fail_free;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		btrfs_subvolume_release_metadata(fs_info, &block_rsv);
		goto fail_free;
	}
	trans->block_rsv = &block_rsv;
	trans->bytes_reserved = block_rsv.size;

	ret = btrfs_qgroup_inherit(trans, 0, objectid, inherit);
	if (ret)
		goto fail;

	leaf = btrfs_alloc_tree_block(trans, root, 0, objectid, NULL, 0, 0, 0);
	if (IS_ERR(leaf)) {
		ret = PTR_ERR(leaf);
		goto fail;
	}

	btrfs_mark_buffer_dirty(leaf);

	inode_item = &root_item->inode;
	btrfs_set_stack_inode_generation(inode_item, 1);
	btrfs_set_stack_inode_size(inode_item, 3);
	btrfs_set_stack_inode_nlink(inode_item, 1);
	btrfs_set_stack_inode_nbytes(inode_item,
				     fs_info->nodesize);
	btrfs_set_stack_inode_mode(inode_item, S_IFDIR | 0755);

	btrfs_set_root_flags(root_item, 0);
	btrfs_set_root_limit(root_item, 0);
	btrfs_set_stack_inode_flags(inode_item, BTRFS_INODE_ROOT_ITEM_INIT);

	btrfs_set_root_bytenr(root_item, leaf->start);
	btrfs_set_root_generation(root_item, trans->transid);
	btrfs_set_root_level(root_item, 0);
	btrfs_set_root_refs(root_item, 1);
	btrfs_set_root_used(root_item, leaf->len);
	btrfs_set_root_last_snapshot(root_item, 0);

	btrfs_set_root_generation_v2(root_item,
			btrfs_root_generation(root_item));
	uuid_le_gen(&new_uuid);
	memcpy(root_item->uuid, new_uuid.b, BTRFS_UUID_SIZE);
	btrfs_set_stack_timespec_sec(&root_item->otime, cur_time.tv_sec);
	btrfs_set_stack_timespec_nsec(&root_item->otime, cur_time.tv_nsec);
	root_item->ctime = root_item->otime;
	btrfs_set_root_ctransid(root_item, trans->transid);
	btrfs_set_root_otransid(root_item, trans->transid);

	btrfs_tree_unlock(leaf);
	free_extent_buffer(leaf);
	leaf = NULL;

	btrfs_set_root_dirid(root_item, new_dirid);

	key.objectid = objectid;
	key.offset = 0;
	key.type = BTRFS_ROOT_ITEM_KEY;
	ret = btrfs_insert_root(trans, fs_info->tree_root, &key,
				root_item);
	if (ret)
		goto fail;

	key.offset = (u64)-1;
	new_root = btrfs_read_fs_root_no_name(fs_info, &key);
	if (IS_ERR(new_root)) {
		ret = PTR_ERR(new_root);
		btrfs_abort_transaction(trans, ret);
		goto fail;
	}

	btrfs_record_root_in_trans(trans, new_root);

	ret = btrfs_create_subvol_root(trans, new_root, root, new_dirid);
	if (ret) {
		/* We potentially lose an unused inode item here */
		btrfs_abort_transaction(trans, ret);
		goto fail;
	}

	mutex_lock(&new_root->objectid_mutex);
	new_root->highest_objectid = new_dirid;
	mutex_unlock(&new_root->objectid_mutex);

	/*
	 * insert the directory item
	 */
	ret = btrfs_set_inode_index(BTRFS_I(dir), &index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto fail;
	}

	ret = btrfs_insert_dir_item(trans, name, namelen, BTRFS_I(dir), &key,
				    BTRFS_FT_DIR, index);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		goto fail;
	}

	btrfs_i_size_write(BTRFS_I(dir), dir->i_size + namelen * 2);
	ret = btrfs_update_inode(trans, root, dir);
	BUG_ON(ret);

	ret = btrfs_add_root_ref(trans, objectid, root->root_key.objectid,
				 btrfs_ino(BTRFS_I(dir)), index, name, namelen);
	BUG_ON(ret);

	ret = btrfs_uuid_tree_add(trans, root_item->uuid,
				  BTRFS_UUID_KEY_SUBVOL, objectid);
	if (ret)
		btrfs_abort_transaction(trans, ret);

fail:
	kfree(root_item);
	trans->block_rsv = NULL;
	trans->bytes_reserved = 0;
	btrfs_subvolume_release_metadata(fs_info, &block_rsv);

	if (async_transid) {
		*async_transid = trans->transid;
		err = btrfs_commit_transaction_async(trans, 1);
		if (err)
			err = btrfs_commit_transaction(trans);
	} else {
		err = btrfs_commit_transaction(trans);
	}
	if (err && !ret)
		ret = err;

	if (!ret) {
		inode = btrfs_lookup_dentry(dir, dentry);
		if (IS_ERR(inode))
			return PTR_ERR(inode);
		d_instantiate(dentry, inode);
	}
	return ret;

fail_free:
	kfree(root_item);
	return ret;
}

static int create_snapshot(struct btrfs_root *root, struct inode *dir,
			   struct dentry *dentry,
			   u64 *async_transid, bool readonly,
			   struct btrfs_qgroup_inherit *inherit)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct inode *inode;
	struct btrfs_pending_snapshot *pending_snapshot;
	struct btrfs_trans_handle *trans;
	int ret;
	bool snapshot_force_cow = false;

	if (!test_bit(BTRFS_ROOT_REF_COWS, &root->state))
		return -EINVAL;

	if (atomic_read(&root->nr_swapfiles)) {
		btrfs_warn(fs_info,
			   "cannot snapshot subvolume with active swapfile");
		return -ETXTBSY;
	}

	pending_snapshot = kzalloc(sizeof(*pending_snapshot), GFP_KERNEL);
	if (!pending_snapshot)
		return -ENOMEM;

	pending_snapshot->root_item = kzalloc(sizeof(struct btrfs_root_item),
			GFP_KERNEL);
	pending_snapshot->path = btrfs_alloc_path();
	if (!pending_snapshot->root_item || !pending_snapshot->path) {
		ret = -ENOMEM;
		goto free_pending;
	}

	/*
	 * Force new buffered writes to reserve space even when NOCOW is
	 * possible. This is to avoid later writeback (running dealloc) to
	 * fallback to COW mode and unexpectedly fail with ENOSPC.
	 */
	atomic_inc(&root->will_be_snapshotted);
	smp_mb__after_atomic();
	/* wait for no snapshot writes */
	wait_event(root->subv_writers->wait,
		   percpu_counter_sum(&root->subv_writers->counter) == 0);

	ret = btrfs_start_delalloc_snapshot(root);
	if (ret)
		goto dec_and_free;

	/*
	 * All previous writes have started writeback in NOCOW mode, so now
	 * we force future writes to fallback to COW mode during snapshot
	 * creation.
	 */
	atomic_inc(&root->snapshot_force_cow);
	snapshot_force_cow = true;

	btrfs_wait_ordered_extents(root, U64_MAX, 0, (u64)-1);

	btrfs_init_block_rsv(&pending_snapshot->block_rsv,
			     BTRFS_BLOCK_RSV_TEMP);
	/*
	 * 1 - parent dir inode
	 * 2 - dir entries
	 * 1 - root item
	 * 2 - root ref/backref
	 * 1 - root of snapshot
	 * 1 - UUID item
	 */
	ret = btrfs_subvolume_reserve_metadata(BTRFS_I(dir)->root,
					&pending_snapshot->block_rsv, 8,
					false);
	if (ret)
		goto dec_and_free;

	pending_snapshot->dentry = dentry;
	pending_snapshot->root = root;
	pending_snapshot->readonly = readonly;
	pending_snapshot->dir = dir;
	pending_snapshot->inherit = inherit;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto fail;
	}

	spin_lock(&fs_info->trans_lock);
	list_add(&pending_snapshot->list,
		 &trans->transaction->pending_snapshots);
	spin_unlock(&fs_info->trans_lock);
	if (async_transid) {
		*async_transid = trans->transid;
		ret = btrfs_commit_transaction_async(trans, 1);
		if (ret)
			ret = btrfs_commit_transaction(trans);
	} else {
		ret = btrfs_commit_transaction(trans);
	}
	if (ret)
		goto fail;

	ret = pending_snapshot->error;
	if (ret)
		goto fail;

	ret = btrfs_orphan_cleanup(pending_snapshot->snap);
	if (ret)
		goto fail;

	inode = btrfs_lookup_dentry(d_inode(dentry->d_parent), dentry);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto fail;
	}

	d_instantiate(dentry, inode);
	ret = 0;
fail:
	btrfs_subvolume_release_metadata(fs_info, &pending_snapshot->block_rsv);
dec_and_free:
	if (snapshot_force_cow)
		atomic_dec(&root->snapshot_force_cow);
	if (atomic_dec_and_test(&root->will_be_snapshotted))
		wake_up_var(&root->will_be_snapshotted);
free_pending:
	kfree(pending_snapshot->root_item);
	btrfs_free_path(pending_snapshot->path);
	kfree(pending_snapshot);

	return ret;
}

/*  copy of may_delete in fs/namei.c()
 *	Check whether we can remove a link victim from directory dir, check
 *  whether the type of victim is right.
 *  1. We can't do it if dir is read-only (done in permission())
 *  2. We should have write and exec permissions on dir
 *  3. We can't remove anything from append-only dir
 *  4. We can't do anything with immutable dir (done in permission())
 *  5. If the sticky bit on dir is set we should either
 *	a. be owner of dir, or
 *	b. be owner of victim, or
 *	c. have CAP_FOWNER capability
 *  6. If the victim is append-only or immutable we can't do anything with
 *     links pointing to it.
 *  7. If we were asked to remove a directory and victim isn't one - ENOTDIR.
 *  8. If we were asked to remove a non-directory and victim isn't one - EISDIR.
 *  9. We can't remove a root or mountpoint.
 * 10. We don't allow removal of NFS sillyrenamed files; it's handled by
 *     nfs_async_unlink().
 */

static int btrfs_may_delete(struct inode *dir, struct dentry *victim, int isdir)
{
	int error;

	if (d_really_is_negative(victim))
		return -ENOENT;

	BUG_ON(d_inode(victim->d_parent) != dir);
	audit_inode_child(dir, victim, AUDIT_TYPE_CHILD_DELETE);

	error = inode_permission(dir, MAY_WRITE | MAY_EXEC);
	if (error)
		return error;
	if (IS_APPEND(dir))
		return -EPERM;
	if (check_sticky(dir, d_inode(victim)) || IS_APPEND(d_inode(victim)) ||
	    IS_IMMUTABLE(d_inode(victim)) || IS_SWAPFILE(d_inode(victim)))
		return -EPERM;
	if (isdir) {
		if (!d_is_dir(victim))
			return -ENOTDIR;
		if (IS_ROOT(victim))
			return -EBUSY;
	} else if (d_is_dir(victim))
		return -EISDIR;
	if (IS_DEADDIR(dir))
		return -ENOENT;
	if (victim->d_flags & DCACHE_NFSFS_RENAMED)
		return -EBUSY;
	return 0;
}

/* copy of may_create in fs/namei.c() */
static inline int btrfs_may_create(struct inode *dir, struct dentry *child)
{
	if (d_really_is_positive(child))
		return -EEXIST;
	if (IS_DEADDIR(dir))
		return -ENOENT;
	return inode_permission(dir, MAY_WRITE | MAY_EXEC);
}

/*
 * Create a new subvolume below @parent.  This is largely modeled after
 * sys_mkdirat and vfs_mkdir, but we only do a single component lookup
 * inside this filesystem so it's quite a bit simpler.
 */
static noinline int btrfs_mksubvol(const struct path *parent,
				   const char *name, int namelen,
				   struct btrfs_root *snap_src,
				   u64 *async_transid, bool readonly,
				   struct btrfs_qgroup_inherit *inherit)
{
	struct inode *dir = d_inode(parent->dentry);
	struct btrfs_fs_info *fs_info = btrfs_sb(dir->i_sb);
	struct dentry *dentry;
	int error;

	error = down_write_killable_nested(&dir->i_rwsem, I_MUTEX_PARENT);
	if (error == -EINTR)
		return error;

	dentry = lookup_one_len(name, parent->dentry, namelen);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out_unlock;

	error = btrfs_may_create(dir, dentry);
	if (error)
		goto out_dput;

	/*
	 * even if this name doesn't exist, we may get hash collisions.
	 * check for them now when we can safely fail
	 */
	error = btrfs_check_dir_item_collision(BTRFS_I(dir)->root,
					       dir->i_ino, name,
					       namelen);
	if (error)
		goto out_dput;

	down_read(&fs_info->subvol_sem);

	if (btrfs_root_refs(&BTRFS_I(dir)->root->root_item) == 0)
		goto out_up_read;

	if (snap_src) {
		error = create_snapshot(snap_src, dir, dentry,
					async_transid, readonly, inherit);
	} else {
		error = create_subvol(dir, dentry, name, namelen,
				      async_transid, inherit);
	}
	if (!error)
		fsnotify_mkdir(dir, dentry);
out_up_read:
	up_read(&fs_info->subvol_sem);
out_dput:
	dput(dentry);
out_unlock:
	inode_unlock(dir);
	return error;
}

/*
 * When we're defragging a range, we don't want to kick it off again
 * if it is really just waiting for delalloc to send it down.
 * If we find a nice big extent or delalloc range for the bytes in the
 * file you want to defrag, we return 0 to let you know to skip this
 * part of the file
 */
static int check_defrag_in_cache(struct inode *inode, u64 offset, u32 thresh)
{
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct extent_map *em = NULL;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	u64 end;

	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, offset, PAGE_SIZE);
	read_unlock(&em_tree->lock);

	if (em) {
		end = extent_map_end(em);
		free_extent_map(em);
		if (end - offset > thresh)
			return 0;
	}
	/* if we already have a nice delalloc here, just stop */
	thresh /= 2;
	end = count_range_bits(io_tree, &offset, offset + thresh,
			       thresh, EXTENT_DELALLOC, 1);
	if (end >= thresh)
		return 0;
	return 1;
}

/*
 * helper function to walk through a file and find extents
 * newer than a specific transid, and smaller than thresh.
 *
 * This is used by the defragging code to find new and small
 * extents
 */
static int find_new_extents(struct btrfs_root *root,
			    struct inode *inode, u64 newer_than,
			    u64 *off, u32 thresh)
{
	struct btrfs_path *path;
	struct btrfs_key min_key;
	struct extent_buffer *leaf;
	struct btrfs_file_extent_item *extent;
	int type;
	int ret;
	u64 ino = btrfs_ino(BTRFS_I(inode));

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	min_key.objectid = ino;
	min_key.type = BTRFS_EXTENT_DATA_KEY;
	min_key.offset = *off;

	while (1) {
		ret = btrfs_search_forward(root, &min_key, path, newer_than);
		if (ret != 0)
			goto none;
process_slot:
		if (min_key.objectid != ino)
			goto none;
		if (min_key.type != BTRFS_EXTENT_DATA_KEY)
			goto none;

		leaf = path->nodes[0];
		extent = btrfs_item_ptr(leaf, path->slots[0],
					struct btrfs_file_extent_item);

		type = btrfs_file_extent_type(leaf, extent);
		if (type == BTRFS_FILE_EXTENT_REG &&
		    btrfs_file_extent_num_bytes(leaf, extent) < thresh &&
		    check_defrag_in_cache(inode, min_key.offset, thresh)) {
			*off = min_key.offset;
			btrfs_free_path(path);
			return 0;
		}

		path->slots[0]++;
		if (path->slots[0] < btrfs_header_nritems(leaf)) {
			btrfs_item_key_to_cpu(leaf, &min_key, path->slots[0]);
			goto process_slot;
		}

		if (min_key.offset == (u64)-1)
			goto none;

		min_key.offset++;
		btrfs_release_path(path);
	}
none:
	btrfs_free_path(path);
	return -ENOENT;
}

static struct extent_map *defrag_lookup_extent(struct inode *inode, u64 start)
{
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct extent_map *em;
	u64 len = PAGE_SIZE;

	/*
	 * hopefully we have this extent in the tree already, try without
	 * the full extent lock
	 */
	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);
	read_unlock(&em_tree->lock);

	if (!em) {
		struct extent_state *cached = NULL;
		u64 end = start + len - 1;

		/* get the big lock and read metadata off disk */
		lock_extent_bits(io_tree, start, end, &cached);
		em = btrfs_get_extent(BTRFS_I(inode), NULL, 0, start, len, 0);
		unlock_extent_cached(io_tree, start, end, &cached);

		if (IS_ERR(em))
			return NULL;
	}

	return em;
}

static bool defrag_check_next_extent(struct inode *inode, struct extent_map *em)
{
	struct extent_map *next;
	bool ret = true;

	/* this is the last extent */
	if (em->start + em->len >= i_size_read(inode))
		return false;

	next = defrag_lookup_extent(inode, em->start + em->len);
	if (!next || next->block_start >= EXTENT_MAP_LAST_BYTE)
		ret = false;
	else if ((em->block_start + em->block_len == next->block_start) &&
		 (em->block_len > SZ_128K && next->block_len > SZ_128K))
		ret = false;

	free_extent_map(next);
	return ret;
}

static int should_defrag_range(struct inode *inode, u64 start, u32 thresh,
			       u64 *last_len, u64 *skip, u64 *defrag_end,
			       int compress)
{
	struct extent_map *em;
	int ret = 1;
	bool next_mergeable = true;
	bool prev_mergeable = true;

	/*
	 * make sure that once we start defragging an extent, we keep on
	 * defragging it
	 */
	if (start < *defrag_end)
		return 1;

	*skip = 0;

	em = defrag_lookup_extent(inode, start);
	if (!em)
		return 0;

	/* this will cover holes, and inline extents */
	if (em->block_start >= EXTENT_MAP_LAST_BYTE) {
		ret = 0;
		goto out;
	}

	if (!*defrag_end)
		prev_mergeable = false;

	next_mergeable = defrag_check_next_extent(inode, em);
	/*
	 * we hit a real extent, if it is big or the next extent is not a
	 * real extent, don't bother defragging it
	 */
	if (!compress && (*last_len == 0 || *last_len >= thresh) &&
	    (em->len >= thresh || (!next_mergeable && !prev_mergeable)))
		ret = 0;
out:
	/*
	 * last_len ends up being a counter of how many bytes we've defragged.
	 * every time we choose not to defrag an extent, we reset *last_len
	 * so that the next tiny extent will force a defrag.
	 *
	 * The end result of this is that tiny extents before a single big
	 * extent will force at least part of that big extent to be defragged.
	 */
	if (ret) {
		*defrag_end = extent_map_end(em);
	} else {
		*last_len = 0;
		*skip = extent_map_end(em);
		*defrag_end = 0;
	}

	free_extent_map(em);
	return ret;
}

/*
 * it doesn't do much good to defrag one or two pages
 * at a time.  This pulls in a nice chunk of pages
 * to COW and defrag.
 *
 * It also makes sure the delalloc code has enough
 * dirty data to avoid making new small extents as part
 * of the defrag
 *
 * It's a good idea to start RA on this range
 * before calling this.
 */
static int cluster_pages_for_defrag(struct inode *inode,
				    struct page **pages,
				    unsigned long start_index,
				    unsigned long num_pages)
{
	unsigned long file_end;
	u64 isize = i_size_read(inode);
	u64 page_start;
	u64 page_end;
	u64 page_cnt;
	int ret;
	int i;
	int i_done;
	struct btrfs_ordered_extent *ordered;
	struct extent_state *cached_state = NULL;
	struct extent_io_tree *tree;
	struct extent_changeset *data_reserved = NULL;
	gfp_t mask = btrfs_alloc_write_mask(inode->i_mapping);

	file_end = (isize - 1) >> PAGE_SHIFT;
	if (!isize || start_index > file_end)
		return 0;

	page_cnt = min_t(u64, (u64)num_pages, (u64)file_end - start_index + 1);

	ret = btrfs_delalloc_reserve_space(inode, &data_reserved,
			start_index << PAGE_SHIFT,
			page_cnt << PAGE_SHIFT);
	if (ret)
		return ret;
	i_done = 0;
	tree = &BTRFS_I(inode)->io_tree;

	/* step one, lock all the pages */
	for (i = 0; i < page_cnt; i++) {
		struct page *page;
again:
		page = find_or_create_page(inode->i_mapping,
					   start_index + i, mask);
		if (!page)
			break;

		page_start = page_offset(page);
		page_end = page_start + PAGE_SIZE - 1;
		while (1) {
			lock_extent_bits(tree, page_start, page_end,
					 &cached_state);
			ordered = btrfs_lookup_ordered_extent(inode,
							      page_start);
			unlock_extent_cached(tree, page_start, page_end,
					     &cached_state);
			if (!ordered)
				break;

			unlock_page(page);
			btrfs_start_ordered_extent(inode, ordered, 1);
			btrfs_put_ordered_extent(ordered);
			lock_page(page);
			/*
			 * we unlocked the page above, so we need check if
			 * it was released or not.
			 */
			if (page->mapping != inode->i_mapping) {
				unlock_page(page);
				put_page(page);
				goto again;
			}
		}

		if (!PageUptodate(page)) {
			btrfs_readpage(NULL, page);
			lock_page(page);
			if (!PageUptodate(page)) {
				unlock_page(page);
				put_page(page);
				ret = -EIO;
				break;
			}
		}

		if (page->mapping != inode->i_mapping) {
			unlock_page(page);
			put_page(page);
			goto again;
		}

		pages[i] = page;
		i_done++;
	}
	if (!i_done || ret)
		goto out;

	if (!(inode->i_sb->s_flags & SB_ACTIVE))
		goto out;

	/*
	 * so now we have a nice long stream of locked
	 * and up to date pages, lets wait on them
	 */
	for (i = 0; i < i_done; i++)
		wait_on_page_writeback(pages[i]);

	page_start = page_offset(pages[0]);
	page_end = page_offset(pages[i_done - 1]) + PAGE_SIZE;

	lock_extent_bits(&BTRFS_I(inode)->io_tree,
			 page_start, page_end - 1, &cached_state);
	clear_extent_bit(&BTRFS_I(inode)->io_tree, page_start,
			  page_end - 1, EXTENT_DIRTY | EXTENT_DELALLOC |
			  EXTENT_DO_ACCOUNTING | EXTENT_DEFRAG, 0, 0,
			  &cached_state);

	if (i_done != page_cnt) {
		spin_lock(&BTRFS_I(inode)->lock);
		btrfs_mod_outstanding_extents(BTRFS_I(inode), 1);
		spin_unlock(&BTRFS_I(inode)->lock);
		btrfs_delalloc_release_space(inode, data_reserved,
				start_index << PAGE_SHIFT,
				(page_cnt - i_done) << PAGE_SHIFT, true);
	}


	set_extent_defrag(&BTRFS_I(inode)->io_tree, page_start, page_end - 1,
			  &cached_state);

	unlock_extent_cached(&BTRFS_I(inode)->io_tree,
			     page_start, page_end - 1, &cached_state);

	for (i = 0; i < i_done; i++) {
		clear_page_dirty_for_io(pages[i]);
		ClearPageChecked(pages[i]);
		set_page_extent_mapped(pages[i]);
		set_page_dirty(pages[i]);
		unlock_page(pages[i]);
		put_page(pages[i]);
	}
	btrfs_delalloc_release_extents(BTRFS_I(inode), page_cnt << PAGE_SHIFT,
				       false);
	extent_changeset_free(data_reserved);
	return i_done;
out:
	for (i = 0; i < i_done; i++) {
		unlock_page(pages[i]);
		put_page(pages[i]);
	}
	btrfs_delalloc_release_space(inode, data_reserved,
			start_index << PAGE_SHIFT,
			page_cnt << PAGE_SHIFT, true);
	btrfs_delalloc_release_extents(BTRFS_I(inode), page_cnt << PAGE_SHIFT,
				       true);
	extent_changeset_free(data_reserved);
	return ret;

}

int btrfs_defrag_file(struct inode *inode, struct file *file,
		      struct btrfs_ioctl_defrag_range_args *range,
		      u64 newer_than, unsigned long max_to_defrag)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct file_ra_state *ra = NULL;
	unsigned long last_index;
	u64 isize = i_size_read(inode);
	u64 last_len = 0;
	u64 skip = 0;
	u64 defrag_end = 0;
	u64 newer_off = range->start;
	unsigned long i;
	unsigned long ra_index = 0;
	int ret;
	int defrag_count = 0;
	int compress_type = BTRFS_COMPRESS_ZLIB;
	u32 extent_thresh = range->extent_thresh;
	unsigned long max_cluster = SZ_256K >> PAGE_SHIFT;
	unsigned long cluster = max_cluster;
	u64 new_align = ~((u64)SZ_128K - 1);
	struct page **pages = NULL;
	bool do_compress = range->flags & BTRFS_DEFRAG_RANGE_COMPRESS;

	if (isize == 0)
		return 0;

	if (range->start >= isize)
		return -EINVAL;

	if (do_compress) {
		if (range->compress_type > BTRFS_COMPRESS_TYPES)
			return -EINVAL;
		if (range->compress_type)
			compress_type = range->compress_type;
	}

	if (extent_thresh == 0)
		extent_thresh = SZ_256K;

	/*
	 * If we were not given a file, allocate a readahead context. As
	 * readahead is just an optimization, defrag will work without it so
	 * we don't error out.
	 */
	if (!file) {
		ra = kzalloc(sizeof(*ra), GFP_KERNEL);
		if (ra)
			file_ra_state_init(ra, inode->i_mapping);
	} else {
		ra = &file->f_ra;
	}

	pages = kmalloc_array(max_cluster, sizeof(struct page *), GFP_KERNEL);
	if (!pages) {
		ret = -ENOMEM;
		goto out_ra;
	}

	/* find the last page to defrag */
	if (range->start + range->len > range->start) {
		last_index = min_t(u64, isize - 1,
			 range->start + range->len - 1) >> PAGE_SHIFT;
	} else {
		last_index = (isize - 1) >> PAGE_SHIFT;
	}

	if (newer_than) {
		ret = find_new_extents(root, inode, newer_than,
				       &newer_off, SZ_64K);
		if (!ret) {
			range->start = newer_off;
			/*
			 * we always align our defrag to help keep
			 * the extents in the file evenly spaced
			 */
			i = (newer_off & new_align) >> PAGE_SHIFT;
		} else
			goto out_ra;
	} else {
		i = range->start >> PAGE_SHIFT;
	}
	if (!max_to_defrag)
		max_to_defrag = last_index - i + 1;

	/*
	 * make writeback starts from i, so the defrag range can be
	 * written sequentially.
	 */
	if (i < inode->i_mapping->writeback_index)
		inode->i_mapping->writeback_index = i;

	while (i <= last_index && defrag_count < max_to_defrag &&
	       (i < DIV_ROUND_UP(i_size_read(inode), PAGE_SIZE))) {
		/*
		 * make sure we stop running if someone unmounts
		 * the FS
		 */
		if (!(inode->i_sb->s_flags & SB_ACTIVE))
			break;

		if (btrfs_defrag_cancelled(fs_info)) {
			btrfs_debug(fs_info, "defrag_file cancelled");
			ret = -EAGAIN;
			break;
		}

		if (!should_defrag_range(inode, (u64)i << PAGE_SHIFT,
					 extent_thresh, &last_len, &skip,
					 &defrag_end, do_compress)){
			unsigned long next;
			/*
			 * the should_defrag function tells us how much to skip
			 * bump our counter by the suggested amount
			 */
			next = DIV_ROUND_UP(skip, PAGE_SIZE);
			i = max(i + 1, next);
			continue;
		}

		if (!newer_than) {
			cluster = (PAGE_ALIGN(defrag_end) >>
				   PAGE_SHIFT) - i;
			cluster = min(cluster, max_cluster);
		} else {
			cluster = max_cluster;
		}

		if (i + cluster > ra_index) {
			ra_index = max(i, ra_index);
			if (ra)
				page_cache_sync_readahead(inode->i_mapping, ra,
						file, ra_index, cluster);
			ra_index += cluster;
		}

		inode_lock(inode);
		if (IS_SWAPFILE(inode)) {
			ret = -ETXTBSY;
		} else {
			if (do_compress)
				BTRFS_I(inode)->defrag_compress = compress_type;
			ret = cluster_pages_for_defrag(inode, pages, i, cluster);
		}
		if (ret < 0) {
			inode_unlock(inode);
			goto out_ra;
		}

		defrag_count += ret;
		balance_dirty_pages_ratelimited(inode->i_mapping);
		inode_unlock(inode);

		if (newer_than) {
			if (newer_off == (u64)-1)
				break;

			if (ret > 0)
				i += ret;

			newer_off = max(newer_off + 1,
					(u64)i << PAGE_SHIFT);

			ret = find_new_extents(root, inode, newer_than,
					       &newer_off, SZ_64K);
			if (!ret) {
				range->start = newer_off;
				i = (newer_off & new_align) >> PAGE_SHIFT;
			} else {
				break;
			}
		} else {
			if (ret > 0) {
				i += ret;
				last_len += ret << PAGE_SHIFT;
			} else {
				i++;
				last_len = 0;
			}
		}
	}

	if ((range->flags & BTRFS_DEFRAG_RANGE_START_IO)) {
		filemap_flush(inode->i_mapping);
		if (test_bit(BTRFS_INODE_HAS_ASYNC_EXTENT,
			     &BTRFS_I(inode)->runtime_flags))
			filemap_flush(inode->i_mapping);
	}

	if (range->compress_type == BTRFS_COMPRESS_LZO) {
		btrfs_set_fs_incompat(fs_info, COMPRESS_LZO);
	} else if (range->compress_type == BTRFS_COMPRESS_ZSTD) {
		btrfs_set_fs_incompat(fs_info, COMPRESS_ZSTD);
	}

	ret = defrag_count;

out_ra:
	if (do_compress) {
		inode_lock(inode);
		BTRFS_I(inode)->defrag_compress = BTRFS_COMPRESS_NONE;
		inode_unlock(inode);
	}
	if (!file)
		kfree(ra);
	kfree(pages);
	return ret;
}

static noinline int btrfs_ioctl_resize(struct file *file,
					void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	u64 new_size;
	u64 old_size;
	u64 devid = 1;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_ioctl_vol_args *vol_args;
	struct btrfs_trans_handle *trans;
	struct btrfs_device *device = NULL;
	char *sizestr;
	char *retptr;
	char *devstr = NULL;
	int ret = 0;
	int mod = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	if (test_and_set_bit(BTRFS_FS_EXCL_OP, &fs_info->flags)) {
		mnt_drop_write_file(file);
		return BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS;
	}

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args)) {
		ret = PTR_ERR(vol_args);
		goto out;
	}

	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';

	sizestr = vol_args->name;
	devstr = strchr(sizestr, ':');
	if (devstr) {
		sizestr = devstr + 1;
		*devstr = '\0';
		devstr = vol_args->name;
		ret = kstrtoull(devstr, 10, &devid);
		if (ret)
			goto out_free;
		if (!devid) {
			ret = -EINVAL;
			goto out_free;
		}
		btrfs_info(fs_info, "resizing devid %llu", devid);
	}

	device = btrfs_find_device(fs_info->fs_devices, devid, NULL, NULL, true);
	if (!device) {
		btrfs_info(fs_info, "resizer unable to find device %llu",
			   devid);
		ret = -ENODEV;
		goto out_free;
	}

	if (!test_bit(BTRFS_DEV_STATE_WRITEABLE, &device->dev_state)) {
		btrfs_info(fs_info,
			   "resizer unable to apply on readonly device %llu",
		       devid);
		ret = -EPERM;
		goto out_free;
	}

	if (!strcmp(sizestr, "max"))
		new_size = device->bdev->bd_inode->i_size;
	else {
		if (sizestr[0] == '-') {
			mod = -1;
			sizestr++;
		} else if (sizestr[0] == '+') {
			mod = 1;
			sizestr++;
		}
		new_size = memparse(sizestr, &retptr);
		if (*retptr != '\0' || new_size == 0) {
			ret = -EINVAL;
			goto out_free;
		}
	}

	if (test_bit(BTRFS_DEV_STATE_REPLACE_TGT, &device->dev_state)) {
		ret = -EPERM;
		goto out_free;
	}

	old_size = btrfs_device_get_total_bytes(device);

	if (mod < 0) {
		if (new_size > old_size) {
			ret = -EINVAL;
			goto out_free;
		}
		new_size = old_size - new_size;
	} else if (mod > 0) {
		if (new_size > ULLONG_MAX - old_size) {
			ret = -ERANGE;
			goto out_free;
		}
		new_size = old_size + new_size;
	}

	if (new_size < SZ_256M) {
		ret = -EINVAL;
		goto out_free;
	}
	if (new_size > device->bdev->bd_inode->i_size) {
		ret = -EFBIG;
		goto out_free;
	}

	new_size = round_down(new_size, fs_info->sectorsize);

	btrfs_info_in_rcu(fs_info, "new size for %s is %llu",
			  rcu_str_deref(device->name), new_size);

	if (new_size > old_size) {
		trans = btrfs_start_transaction(root, 0);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out_free;
		}
		ret = btrfs_grow_device(trans, device, new_size);
		btrfs_commit_transaction(trans);
	} else if (new_size < old_size) {
		ret = btrfs_shrink_device(device, new_size);
	} /* equal, nothing need to do */

out_free:
	kfree(vol_args);
out:
	clear_bit(BTRFS_FS_EXCL_OP, &fs_info->flags);
	mnt_drop_write_file(file);
	return ret;
}

static noinline int btrfs_ioctl_snap_create_transid(struct file *file,
				const char *name, unsigned long fd, int subvol,
				u64 *transid, bool readonly,
				struct btrfs_qgroup_inherit *inherit)
{
	int namelen;
	int ret = 0;

	if (!S_ISDIR(file_inode(file)->i_mode))
		return -ENOTDIR;

	ret = mnt_want_write_file(file);
	if (ret)
		goto out;

	namelen = strlen(name);
	if (strchr(name, '/')) {
		ret = -EINVAL;
		goto out_drop_write;
	}

	if (name[0] == '.' &&
	   (namelen == 1 || (name[1] == '.' && namelen == 2))) {
		ret = -EEXIST;
		goto out_drop_write;
	}

	if (subvol) {
		ret = btrfs_mksubvol(&file->f_path, name, namelen,
				     NULL, transid, readonly, inherit);
	} else {
		struct fd src = fdget(fd);
		struct inode *src_inode;
		if (!src.file) {
			ret = -EINVAL;
			goto out_drop_write;
		}

		src_inode = file_inode(src.file);
		if (src_inode->i_sb != file_inode(file)->i_sb) {
			btrfs_info(BTRFS_I(file_inode(file))->root->fs_info,
				   "Snapshot src from another FS");
			ret = -EXDEV;
		} else if (!inode_owner_or_capable(src_inode)) {
			/*
			 * Subvolume creation is not restricted, but snapshots
			 * are limited to own subvolumes only
			 */
			ret = -EPERM;
		} else {
			ret = btrfs_mksubvol(&file->f_path, name, namelen,
					     BTRFS_I(src_inode)->root,
					     transid, readonly, inherit);
		}
		fdput(src);
	}
out_drop_write:
	mnt_drop_write_file(file);
out:
	return ret;
}

static noinline int btrfs_ioctl_snap_create(struct file *file,
					    void __user *arg, int subvol)
{
	struct btrfs_ioctl_vol_args *vol_args;
	int ret;

	if (!S_ISDIR(file_inode(file)->i_mode))
		return -ENOTDIR;

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args))
		return PTR_ERR(vol_args);
	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';

	ret = btrfs_ioctl_snap_create_transid(file, vol_args->name,
					      vol_args->fd, subvol,
					      NULL, false, NULL);

	kfree(vol_args);
	return ret;
}

static noinline int btrfs_ioctl_snap_create_v2(struct file *file,
					       void __user *arg, int subvol)
{
	struct btrfs_ioctl_vol_args_v2 *vol_args;
	int ret;
	u64 transid = 0;
	u64 *ptr = NULL;
	bool readonly = false;
	struct btrfs_qgroup_inherit *inherit = NULL;

	if (!S_ISDIR(file_inode(file)->i_mode))
		return -ENOTDIR;

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args))
		return PTR_ERR(vol_args);
	vol_args->name[BTRFS_SUBVOL_NAME_MAX] = '\0';

	if (vol_args->flags &
	    ~(BTRFS_SUBVOL_CREATE_ASYNC | BTRFS_SUBVOL_RDONLY |
	      BTRFS_SUBVOL_QGROUP_INHERIT)) {
		ret = -EOPNOTSUPP;
		goto free_args;
	}

	if (vol_args->flags & BTRFS_SUBVOL_CREATE_ASYNC)
		ptr = &transid;
	if (vol_args->flags & BTRFS_SUBVOL_RDONLY)
		readonly = true;
	if (vol_args->flags & BTRFS_SUBVOL_QGROUP_INHERIT) {
		if (vol_args->size > PAGE_SIZE) {
			ret = -EINVAL;
			goto free_args;
		}
		inherit = memdup_user(vol_args->qgroup_inherit, vol_args->size);
		if (IS_ERR(inherit)) {
			ret = PTR_ERR(inherit);
			goto free_args;
		}
	}

	ret = btrfs_ioctl_snap_create_transid(file, vol_args->name,
					      vol_args->fd, subvol, ptr,
					      readonly, inherit);
	if (ret)
		goto free_inherit;

	if (ptr && copy_to_user(arg +
				offsetof(struct btrfs_ioctl_vol_args_v2,
					transid),
				ptr, sizeof(*ptr)))
		ret = -EFAULT;

free_inherit:
	kfree(inherit);
free_args:
	kfree(vol_args);
	return ret;
}

static noinline int btrfs_ioctl_subvol_getflags(struct file *file,
						void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret = 0;
	u64 flags = 0;

	if (btrfs_ino(BTRFS_I(inode)) != BTRFS_FIRST_FREE_OBJECTID)
		return -EINVAL;

	down_read(&fs_info->subvol_sem);
	if (btrfs_root_readonly(root))
		flags |= BTRFS_SUBVOL_RDONLY;
	up_read(&fs_info->subvol_sem);

	if (copy_to_user(arg, &flags, sizeof(flags)))
		ret = -EFAULT;

	return ret;
}

static noinline int btrfs_ioctl_subvol_setflags(struct file *file,
					      void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	u64 root_flags;
	u64 flags;
	int ret = 0;

	if (!inode_owner_or_capable(inode))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		goto out;

	if (btrfs_ino(BTRFS_I(inode)) != BTRFS_FIRST_FREE_OBJECTID) {
		ret = -EINVAL;
		goto out_drop_write;
	}

	if (copy_from_user(&flags, arg, sizeof(flags))) {
		ret = -EFAULT;
		goto out_drop_write;
	}

	if (flags & BTRFS_SUBVOL_CREATE_ASYNC) {
		ret = -EINVAL;
		goto out_drop_write;
	}

	if (flags & ~BTRFS_SUBVOL_RDONLY) {
		ret = -EOPNOTSUPP;
		goto out_drop_write;
	}

	down_write(&fs_info->subvol_sem);

	/* nothing to do */
	if (!!(flags & BTRFS_SUBVOL_RDONLY) == btrfs_root_readonly(root))
		goto out_drop_sem;

	root_flags = btrfs_root_flags(&root->root_item);
	if (flags & BTRFS_SUBVOL_RDONLY) {
		btrfs_set_root_flags(&root->root_item,
				     root_flags | BTRFS_ROOT_SUBVOL_RDONLY);
	} else {
		/*
		 * Block RO -> RW transition if this subvolume is involved in
		 * send
		 */
		spin_lock(&root->root_item_lock);
		if (root->send_in_progress == 0) {
			btrfs_set_root_flags(&root->root_item,
				     root_flags & ~BTRFS_ROOT_SUBVOL_RDONLY);
			spin_unlock(&root->root_item_lock);
		} else {
			spin_unlock(&root->root_item_lock);
			btrfs_warn(fs_info,
				   "Attempt to set subvolume %llu read-write during send",
				   root->root_key.objectid);
			ret = -EPERM;
			goto out_drop_sem;
		}
	}

	trans = btrfs_start_transaction(root, 1);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_reset;
	}

	ret = btrfs_update_root(trans, fs_info->tree_root,
				&root->root_key, &root->root_item);
	if (ret < 0) {
		btrfs_end_transaction(trans);
		goto out_reset;
	}

	ret = btrfs_commit_transaction(trans);

out_reset:
	if (ret)
		btrfs_set_root_flags(&root->root_item, root_flags);
out_drop_sem:
	up_write(&fs_info->subvol_sem);
out_drop_write:
	mnt_drop_write_file(file);
out:
	return ret;
}

static noinline int key_in_sk(struct btrfs_key *key,
			      struct btrfs_ioctl_search_key *sk)
{
	struct btrfs_key test;
	int ret;

	test.objectid = sk->min_objectid;
	test.type = sk->min_type;
	test.offset = sk->min_offset;

	ret = btrfs_comp_cpu_keys(key, &test);
	if (ret < 0)
		return 0;

	test.objectid = sk->max_objectid;
	test.type = sk->max_type;
	test.offset = sk->max_offset;

	ret = btrfs_comp_cpu_keys(key, &test);
	if (ret > 0)
		return 0;
	return 1;
}

static noinline int copy_to_sk(struct btrfs_path *path,
			       struct btrfs_key *key,
			       struct btrfs_ioctl_search_key *sk,
			       size_t *buf_size,
			       char __user *ubuf,
			       unsigned long *sk_offset,
			       int *num_found)
{
	u64 found_transid;
	struct extent_buffer *leaf;
	struct btrfs_ioctl_search_header sh;
	struct btrfs_key test;
	unsigned long item_off;
	unsigned long item_len;
	int nritems;
	int i;
	int slot;
	int ret = 0;

	leaf = path->nodes[0];
	slot = path->slots[0];
	nritems = btrfs_header_nritems(leaf);

	if (btrfs_header_generation(leaf) > sk->max_transid) {
		i = nritems;
		goto advance_key;
	}
	found_transid = btrfs_header_generation(leaf);

	for (i = slot; i < nritems; i++) {
		item_off = btrfs_item_ptr_offset(leaf, i);
		item_len = btrfs_item_size_nr(leaf, i);

		btrfs_item_key_to_cpu(leaf, key, i);
		if (!key_in_sk(key, sk))
			continue;

		if (sizeof(sh) + item_len > *buf_size) {
			if (*num_found) {
				ret = 1;
				goto out;
			}

			/*
			 * return one empty item back for v1, which does not
			 * handle -EOVERFLOW
			 */

			*buf_size = sizeof(sh) + item_len;
			item_len = 0;
			ret = -EOVERFLOW;
		}

		if (sizeof(sh) + item_len + *sk_offset > *buf_size) {
			ret = 1;
			goto out;
		}

		sh.objectid = key->objectid;
		sh.offset = key->offset;
		sh.type = key->type;
		sh.len = item_len;
		sh.transid = found_transid;

		/* copy search result header */
		if (copy_to_user(ubuf + *sk_offset, &sh, sizeof(sh))) {
			ret = -EFAULT;
			goto out;
		}

		*sk_offset += sizeof(sh);

		if (item_len) {
			char __user *up = ubuf + *sk_offset;
			/* copy the item */
			if (read_extent_buffer_to_user(leaf, up,
						       item_off, item_len)) {
				ret = -EFAULT;
				goto out;
			}

			*sk_offset += item_len;
		}
		(*num_found)++;

		if (ret) /* -EOVERFLOW from above */
			goto out;

		if (*num_found >= sk->nr_items) {
			ret = 1;
			goto out;
		}
	}
advance_key:
	ret = 0;
	test.objectid = sk->max_objectid;
	test.type = sk->max_type;
	test.offset = sk->max_offset;
	if (btrfs_comp_cpu_keys(key, &test) >= 0)
		ret = 1;
	else if (key->offset < (u64)-1)
		key->offset++;
	else if (key->type < (u8)-1) {
		key->offset = 0;
		key->type++;
	} else if (key->objectid < (u64)-1) {
		key->offset = 0;
		key->type = 0;
		key->objectid++;
	} else
		ret = 1;
out:
	/*
	 *  0: all items from this leaf copied, continue with next
	 *  1: * more items can be copied, but unused buffer is too small
	 *     * all items were found
	 *     Either way, it will stops the loop which iterates to the next
	 *     leaf
	 *  -EOVERFLOW: item was to large for buffer
	 *  -EFAULT: could not copy extent buffer back to userspace
	 */
	return ret;
}

static noinline int search_ioctl(struct inode *inode,
				 struct btrfs_ioctl_search_key *sk,
				 size_t *buf_size,
				 char __user *ubuf)
{
	struct btrfs_fs_info *info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root;
	struct btrfs_key key;
	struct btrfs_path *path;
	int ret;
	int num_found = 0;
	unsigned long sk_offset = 0;

	if (*buf_size < sizeof(struct btrfs_ioctl_search_header)) {
		*buf_size = sizeof(struct btrfs_ioctl_search_header);
		return -EOVERFLOW;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	if (sk->tree_id == 0) {
		/* search the root of the inode that was passed */
		root = BTRFS_I(inode)->root;
	} else {
		key.objectid = sk->tree_id;
		key.type = BTRFS_ROOT_ITEM_KEY;
		key.offset = (u64)-1;
		root = btrfs_read_fs_root_no_name(info, &key);
		if (IS_ERR(root)) {
			btrfs_free_path(path);
			return PTR_ERR(root);
		}
	}

	key.objectid = sk->min_objectid;
	key.type = sk->min_type;
	key.offset = sk->min_offset;

	while (1) {
		ret = btrfs_search_forward(root, &key, path, sk->min_transid);
		if (ret != 0) {
			if (ret > 0)
				ret = 0;
			goto err;
		}
		ret = copy_to_sk(path, &key, sk, buf_size, ubuf,
				 &sk_offset, &num_found);
		btrfs_release_path(path);
		if (ret)
			break;

	}
	if (ret > 0)
		ret = 0;
err:
	sk->nr_items = num_found;
	btrfs_free_path(path);
	return ret;
}

static noinline int btrfs_ioctl_tree_search(struct file *file,
					   void __user *argp)
{
	struct btrfs_ioctl_search_args __user *uargs;
	struct btrfs_ioctl_search_key sk;
	struct inode *inode;
	int ret;
	size_t buf_size;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	uargs = (struct btrfs_ioctl_search_args __user *)argp;

	if (copy_from_user(&sk, &uargs->key, sizeof(sk)))
		return -EFAULT;

	buf_size = sizeof(uargs->buf);

	inode = file_inode(file);
	ret = search_ioctl(inode, &sk, &buf_size, uargs->buf);

	/*
	 * In the origin implementation an overflow is handled by returning a
	 * search header with a len of zero, so reset ret.
	 */
	if (ret == -EOVERFLOW)
		ret = 0;

	if (ret == 0 && copy_to_user(&uargs->key, &sk, sizeof(sk)))
		ret = -EFAULT;
	return ret;
}

static noinline int btrfs_ioctl_tree_search_v2(struct file *file,
					       void __user *argp)
{
	struct btrfs_ioctl_search_args_v2 __user *uarg;
	struct btrfs_ioctl_search_args_v2 args;
	struct inode *inode;
	int ret;
	size_t buf_size;
	const size_t buf_limit = SZ_16M;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	/* copy search header and buffer size */
	uarg = (struct btrfs_ioctl_search_args_v2 __user *)argp;
	if (copy_from_user(&args, uarg, sizeof(args)))
		return -EFAULT;

	buf_size = args.buf_size;

	/* limit result size to 16MB */
	if (buf_size > buf_limit)
		buf_size = buf_limit;

	inode = file_inode(file);
	ret = search_ioctl(inode, &args.key, &buf_size,
			   (char __user *)(&uarg->buf[0]));
	if (ret == 0 && copy_to_user(&uarg->key, &args.key, sizeof(args.key)))
		ret = -EFAULT;
	else if (ret == -EOVERFLOW &&
		copy_to_user(&uarg->buf_size, &buf_size, sizeof(buf_size)))
		ret = -EFAULT;

	return ret;
}

/*
 * Search INODE_REFs to identify path name of 'dirid' directory
 * in a 'tree_id' tree. and sets path name to 'name'.
 */
static noinline int btrfs_search_path_in_tree(struct btrfs_fs_info *info,
				u64 tree_id, u64 dirid, char *name)
{
	struct btrfs_root *root;
	struct btrfs_key key;
	char *ptr;
	int ret = -1;
	int slot;
	int len;
	int total_len = 0;
	struct btrfs_inode_ref *iref;
	struct extent_buffer *l;
	struct btrfs_path *path;

	if (dirid == BTRFS_FIRST_FREE_OBJECTID) {
		name[0]='\0';
		return 0;
	}

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	ptr = &name[BTRFS_INO_LOOKUP_PATH_MAX - 1];

	key.objectid = tree_id;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = (u64)-1;
	root = btrfs_read_fs_root_no_name(info, &key);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto out;
	}

	key.objectid = dirid;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = (u64)-1;

	while (1) {
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto out;
		else if (ret > 0) {
			ret = btrfs_previous_item(root, path, dirid,
						  BTRFS_INODE_REF_KEY);
			if (ret < 0)
				goto out;
			else if (ret > 0) {
				ret = -ENOENT;
				goto out;
			}
		}

		l = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(l, &key, slot);

		iref = btrfs_item_ptr(l, slot, struct btrfs_inode_ref);
		len = btrfs_inode_ref_name_len(l, iref);
		ptr -= len + 1;
		total_len += len + 1;
		if (ptr < name) {
			ret = -ENAMETOOLONG;
			goto out;
		}

		*(ptr + len) = '/';
		read_extent_buffer(l, ptr, (unsigned long)(iref + 1), len);

		if (key.offset == BTRFS_FIRST_FREE_OBJECTID)
			break;

		btrfs_release_path(path);
		key.objectid = key.offset;
		key.offset = (u64)-1;
		dirid = key.objectid;
	}
	memmove(name, ptr, total_len);
	name[total_len] = '\0';
	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

static int btrfs_search_path_in_tree_user(struct inode *inode,
				struct btrfs_ioctl_ino_lookup_user_args *args)
{
	struct btrfs_fs_info *fs_info = BTRFS_I(inode)->root->fs_info;
	struct super_block *sb = inode->i_sb;
	struct btrfs_key upper_limit = BTRFS_I(inode)->location;
	u64 treeid = BTRFS_I(inode)->root->root_key.objectid;
	u64 dirid = args->dirid;
	unsigned long item_off;
	unsigned long item_len;
	struct btrfs_inode_ref *iref;
	struct btrfs_root_ref *rref;
	struct btrfs_root *root;
	struct btrfs_path *path;
	struct btrfs_key key, key2;
	struct extent_buffer *leaf;
	struct inode *temp_inode;
	char *ptr;
	int slot;
	int len;
	int total_len = 0;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	/*
	 * If the bottom subvolume does not exist directly under upper_limit,
	 * construct the path in from the bottom up.
	 */
	if (dirid != upper_limit.objectid) {
		ptr = &args->path[BTRFS_INO_LOOKUP_USER_PATH_MAX - 1];

		key.objectid = treeid;
		key.type = BTRFS_ROOT_ITEM_KEY;
		key.offset = (u64)-1;
		root = btrfs_read_fs_root_no_name(fs_info, &key);
		if (IS_ERR(root)) {
			ret = PTR_ERR(root);
			goto out;
		}

		key.objectid = dirid;
		key.type = BTRFS_INODE_REF_KEY;
		key.offset = (u64)-1;
		while (1) {
			ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
			if (ret < 0) {
				goto out;
			} else if (ret > 0) {
				ret = btrfs_previous_item(root, path, dirid,
							  BTRFS_INODE_REF_KEY);
				if (ret < 0) {
					goto out;
				} else if (ret > 0) {
					ret = -ENOENT;
					goto out;
				}
			}

			leaf = path->nodes[0];
			slot = path->slots[0];
			btrfs_item_key_to_cpu(leaf, &key, slot);

			iref = btrfs_item_ptr(leaf, slot, struct btrfs_inode_ref);
			len = btrfs_inode_ref_name_len(leaf, iref);
			ptr -= len + 1;
			total_len += len + 1;
			if (ptr < args->path) {
				ret = -ENAMETOOLONG;
				goto out;
			}

			*(ptr + len) = '/';
			read_extent_buffer(leaf, ptr,
					(unsigned long)(iref + 1), len);

			/* Check the read+exec permission of this directory */
			ret = btrfs_previous_item(root, path, dirid,
						  BTRFS_INODE_ITEM_KEY);
			if (ret < 0) {
				goto out;
			} else if (ret > 0) {
				ret = -ENOENT;
				goto out;
			}

			leaf = path->nodes[0];
			slot = path->slots[0];
			btrfs_item_key_to_cpu(leaf, &key2, slot);
			if (key2.objectid != dirid) {
				ret = -ENOENT;
				goto out;
			}

			temp_inode = btrfs_iget(sb, &key2, root, NULL);
			if (IS_ERR(temp_inode)) {
				ret = PTR_ERR(temp_inode);
				goto out;
			}
			ret = inode_permission(temp_inode, MAY_READ | MAY_EXEC);
			iput(temp_inode);
			if (ret) {
				ret = -EACCES;
				goto out;
			}

			if (key.offset == upper_limit.objectid)
				break;
			if (key.objectid == BTRFS_FIRST_FREE_OBJECTID) {
				ret = -EACCES;
				goto out;
			}

			btrfs_release_path(path);
			key.objectid = key.offset;
			key.offset = (u64)-1;
			dirid = key.objectid;
		}

		memmove(args->path, ptr, total_len);
		args->path[total_len] = '\0';
		btrfs_release_path(path);
	}

	/* Get the bottom subvolume's name from ROOT_REF */
	root = fs_info->tree_root;
	key.objectid = treeid;
	key.type = BTRFS_ROOT_REF_KEY;
	key.offset = args->treeid;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		goto out;
	} else if (ret > 0) {
		ret = -ENOENT;
		goto out;
	}

	leaf = path->nodes[0];
	slot = path->slots[0];
	btrfs_item_key_to_cpu(leaf, &key, slot);

	item_off = btrfs_item_ptr_offset(leaf, slot);
	item_len = btrfs_item_size_nr(leaf, slot);
	/* Check if dirid in ROOT_REF corresponds to passed dirid */
	rref = btrfs_item_ptr(leaf, slot, struct btrfs_root_ref);
	if (args->dirid != btrfs_root_ref_dirid(leaf, rref)) {
		ret = -EINVAL;
		goto out;
	}

	/* Copy subvolume's name */
	item_off += sizeof(struct btrfs_root_ref);
	item_len -= sizeof(struct btrfs_root_ref);
	read_extent_buffer(leaf, args->name, item_off, item_len);
	args->name[item_len] = 0;

out:
	btrfs_free_path(path);
	return ret;
}

static noinline int btrfs_ioctl_ino_lookup(struct file *file,
					   void __user *argp)
{
	struct btrfs_ioctl_ino_lookup_args *args;
	struct inode *inode;
	int ret = 0;

	args = memdup_user(argp, sizeof(*args));
	if (IS_ERR(args))
		return PTR_ERR(args);

	inode = file_inode(file);

	/*
	 * Unprivileged query to obtain the containing subvolume root id. The
	 * path is reset so it's consistent with btrfs_search_path_in_tree.
	 */
	if (args->treeid == 0)
		args->treeid = BTRFS_I(inode)->root->root_key.objectid;

	if (args->objectid == BTRFS_FIRST_FREE_OBJECTID) {
		args->name[0] = 0;
		goto out;
	}

	if (!capable(CAP_SYS_ADMIN)) {
		ret = -EPERM;
		goto out;
	}

	ret = btrfs_search_path_in_tree(BTRFS_I(inode)->root->fs_info,
					args->treeid, args->objectid,
					args->name);

out:
	if (ret == 0 && copy_to_user(argp, args, sizeof(*args)))
		ret = -EFAULT;

	kfree(args);
	return ret;
}

/*
 * Version of ino_lookup ioctl (unprivileged)
 *
 * The main differences from ino_lookup ioctl are:
 *
 *   1. Read + Exec permission will be checked using inode_permission() during
 *      path construction. -EACCES will be returned in case of failure.
 *   2. Path construction will be stopped at the inode number which corresponds
 *      to the fd with which this ioctl is called. If constructed path does not
 *      exist under fd's inode, -EACCES will be returned.
 *   3. The name of bottom subvolume is also searched and filled.
 */
static int btrfs_ioctl_ino_lookup_user(struct file *file, void __user *argp)
{
	struct btrfs_ioctl_ino_lookup_user_args *args;
	struct inode *inode;
	int ret;

	args = memdup_user(argp, sizeof(*args));
	if (IS_ERR(args))
		return PTR_ERR(args);

	inode = file_inode(file);

	if (args->dirid == BTRFS_FIRST_FREE_OBJECTID &&
	    BTRFS_I(inode)->location.objectid != BTRFS_FIRST_FREE_OBJECTID) {
		/*
		 * The subvolume does not exist under fd with which this is
		 * called
		 */
		kfree(args);
		return -EACCES;
	}

	ret = btrfs_search_path_in_tree_user(inode, args);

	if (ret == 0 && copy_to_user(argp, args, sizeof(*args)))
		ret = -EFAULT;

	kfree(args);
	return ret;
}

/* Get the subvolume information in BTRFS_ROOT_ITEM and BTRFS_ROOT_BACKREF */
static int btrfs_ioctl_get_subvol_info(struct file *file, void __user *argp)
{
	struct btrfs_ioctl_get_subvol_info_args *subvol_info;
	struct btrfs_fs_info *fs_info;
	struct btrfs_root *root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct btrfs_root_item *root_item;
	struct btrfs_root_ref *rref;
	struct extent_buffer *leaf;
	unsigned long item_off;
	unsigned long item_len;
	struct inode *inode;
	int slot;
	int ret = 0;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	subvol_info = kzalloc(sizeof(*subvol_info), GFP_KERNEL);
	if (!subvol_info) {
		btrfs_free_path(path);
		return -ENOMEM;
	}

	inode = file_inode(file);
	fs_info = BTRFS_I(inode)->root->fs_info;

	/* Get root_item of inode's subvolume */
	key.objectid = BTRFS_I(inode)->root->root_key.objectid;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = (u64)-1;
	root = btrfs_read_fs_root_no_name(fs_info, &key);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto out;
	}
	root_item = &root->root_item;

	subvol_info->treeid = key.objectid;

	subvol_info->generation = btrfs_root_generation(root_item);
	subvol_info->flags = btrfs_root_flags(root_item);

	memcpy(subvol_info->uuid, root_item->uuid, BTRFS_UUID_SIZE);
	memcpy(subvol_info->parent_uuid, root_item->parent_uuid,
						    BTRFS_UUID_SIZE);
	memcpy(subvol_info->received_uuid, root_item->received_uuid,
						    BTRFS_UUID_SIZE);

	subvol_info->ctransid = btrfs_root_ctransid(root_item);
	subvol_info->ctime.sec = btrfs_stack_timespec_sec(&root_item->ctime);
	subvol_info->ctime.nsec = btrfs_stack_timespec_nsec(&root_item->ctime);

	subvol_info->otransid = btrfs_root_otransid(root_item);
	subvol_info->otime.sec = btrfs_stack_timespec_sec(&root_item->otime);
	subvol_info->otime.nsec = btrfs_stack_timespec_nsec(&root_item->otime);

	subvol_info->stransid = btrfs_root_stransid(root_item);
	subvol_info->stime.sec = btrfs_stack_timespec_sec(&root_item->stime);
	subvol_info->stime.nsec = btrfs_stack_timespec_nsec(&root_item->stime);

	subvol_info->rtransid = btrfs_root_rtransid(root_item);
	subvol_info->rtime.sec = btrfs_stack_timespec_sec(&root_item->rtime);
	subvol_info->rtime.nsec = btrfs_stack_timespec_nsec(&root_item->rtime);

	if (key.objectid != BTRFS_FS_TREE_OBJECTID) {
		/* Search root tree for ROOT_BACKREF of this subvolume */
		root = fs_info->tree_root;

		key.type = BTRFS_ROOT_BACKREF_KEY;
		key.offset = 0;
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0) {
			goto out;
		} else if (path->slots[0] >=
			   btrfs_header_nritems(path->nodes[0])) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0) {
				goto out;
			} else if (ret > 0) {
				ret = -EUCLEAN;
				goto out;
			}
		}

		leaf = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid == subvol_info->treeid &&
		    key.type == BTRFS_ROOT_BACKREF_KEY) {
			subvol_info->parent_id = key.offset;

			rref = btrfs_item_ptr(leaf, slot, struct btrfs_root_ref);
			subvol_info->dirid = btrfs_root_ref_dirid(leaf, rref);

			item_off = btrfs_item_ptr_offset(leaf, slot)
					+ sizeof(struct btrfs_root_ref);
			item_len = btrfs_item_size_nr(leaf, slot)
					- sizeof(struct btrfs_root_ref);
			read_extent_buffer(leaf, subvol_info->name,
					   item_off, item_len);
		} else {
			ret = -ENOENT;
			goto out;
		}
	}

	if (copy_to_user(argp, subvol_info, sizeof(*subvol_info)))
		ret = -EFAULT;

out:
	btrfs_free_path(path);
	kzfree(subvol_info);
	return ret;
}

/*
 * Return ROOT_REF information of the subvolume containing this inode
 * except the subvolume name.
 */
static int btrfs_ioctl_get_subvol_rootref(struct file *file, void __user *argp)
{
	struct btrfs_ioctl_get_subvol_rootref_args *rootrefs;
	struct btrfs_root_ref *rref;
	struct btrfs_root *root;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	struct inode *inode;
	u64 objectid;
	int slot;
	int ret;
	u8 found;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	rootrefs = memdup_user(argp, sizeof(*rootrefs));
	if (IS_ERR(rootrefs)) {
		btrfs_free_path(path);
		return PTR_ERR(rootrefs);
	}

	inode = file_inode(file);
	root = BTRFS_I(inode)->root->fs_info->tree_root;
	objectid = BTRFS_I(inode)->root->root_key.objectid;

	key.objectid = objectid;
	key.type = BTRFS_ROOT_REF_KEY;
	key.offset = rootrefs->min_treeid;
	found = 0;

	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		goto out;
	} else if (path->slots[0] >=
		   btrfs_header_nritems(path->nodes[0])) {
		ret = btrfs_next_leaf(root, path);
		if (ret < 0) {
			goto out;
		} else if (ret > 0) {
			ret = -EUCLEAN;
			goto out;
		}
	}
	while (1) {
		leaf = path->nodes[0];
		slot = path->slots[0];

		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.objectid != objectid || key.type != BTRFS_ROOT_REF_KEY) {
			ret = 0;
			goto out;
		}

		if (found == BTRFS_MAX_ROOTREF_BUFFER_NUM) {
			ret = -EOVERFLOW;
			goto out;
		}

		rref = btrfs_item_ptr(leaf, slot, struct btrfs_root_ref);
		rootrefs->rootref[found].treeid = key.offset;
		rootrefs->rootref[found].dirid =
				  btrfs_root_ref_dirid(leaf, rref);
		found++;

		ret = btrfs_next_item(root, path);
		if (ret < 0) {
			goto out;
		} else if (ret > 0) {
			ret = -EUCLEAN;
			goto out;
		}
	}

out:
	if (!ret || ret == -EOVERFLOW) {
		rootrefs->num_items = found;
		/* update min_treeid for next search */
		if (found)
			rootrefs->min_treeid =
				rootrefs->rootref[found - 1].treeid + 1;
		if (copy_to_user(argp, rootrefs, sizeof(*rootrefs)))
			ret = -EFAULT;
	}

	kfree(rootrefs);
	btrfs_free_path(path);

	return ret;
}

static noinline int btrfs_ioctl_snap_destroy(struct file *file,
					     void __user *arg)
{
	struct dentry *parent = file->f_path.dentry;
	struct btrfs_fs_info *fs_info = btrfs_sb(parent->d_sb);
	struct dentry *dentry;
	struct inode *dir = d_inode(parent);
	struct inode *inode;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_root *dest = NULL;
	struct btrfs_ioctl_vol_args *vol_args;
	int namelen;
	int err = 0;

	if (!S_ISDIR(dir->i_mode))
		return -ENOTDIR;

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args))
		return PTR_ERR(vol_args);

	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';
	namelen = strlen(vol_args->name);
	if (strchr(vol_args->name, '/') ||
	    strncmp(vol_args->name, "..", namelen) == 0) {
		err = -EINVAL;
		goto out;
	}

	err = mnt_want_write_file(file);
	if (err)
		goto out;


	err = down_write_killable_nested(&dir->i_rwsem, I_MUTEX_PARENT);
	if (err == -EINTR)
		goto out_drop_write;
	dentry = lookup_one_len(vol_args->name, parent, namelen);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto out_unlock_dir;
	}

	if (d_really_is_negative(dentry)) {
		err = -ENOENT;
		goto out_dput;
	}

	inode = d_inode(dentry);
	dest = BTRFS_I(inode)->root;
	if (!capable(CAP_SYS_ADMIN)) {
		/*
		 * Regular user.  Only allow this with a special mount
		 * option, when the user has write+exec access to the
		 * subvol root, and when rmdir(2) would have been
		 * allowed.
		 *
		 * Note that this is _not_ check that the subvol is
		 * empty or doesn't contain data that we wouldn't
		 * otherwise be able to delete.
		 *
		 * Users who want to delete empty subvols should try
		 * rmdir(2).
		 */
		err = -EPERM;
		if (!btrfs_test_opt(fs_info, USER_SUBVOL_RM_ALLOWED))
			goto out_dput;

		/*
		 * Do not allow deletion if the parent dir is the same
		 * as the dir to be deleted.  That means the ioctl
		 * must be called on the dentry referencing the root
		 * of the subvol, not a random directory contained
		 * within it.
		 */
		err = -EINVAL;
		if (root == dest)
			goto out_dput;

		err = inode_permission(inode, MAY_WRITE | MAY_EXEC);
		if (err)
			goto out_dput;
	}

	/* check if subvolume may be deleted by a user */
	err = btrfs_may_delete(dir, dentry, 1);
	if (err)
		goto out_dput;

	if (btrfs_ino(BTRFS_I(inode)) != BTRFS_FIRST_FREE_OBJECTID) {
		err = -EINVAL;
		goto out_dput;
	}

	inode_lock(inode);
	err = btrfs_delete_subvolume(dir, dentry);
	inode_unlock(inode);
	if (!err)
		d_delete(dentry);

out_dput:
	dput(dentry);
out_unlock_dir:
	inode_unlock(dir);
out_drop_write:
	mnt_drop_write_file(file);
out:
	kfree(vol_args);
	return err;
}

static int btrfs_ioctl_defrag(struct file *file, void __user *argp)
{
	struct inode *inode = file_inode(file);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_ioctl_defrag_range_args *range;
	int ret;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	if (btrfs_root_readonly(root)) {
		ret = -EROFS;
		goto out;
	}

	switch (inode->i_mode & S_IFMT) {
	case S_IFDIR:
		if (!capable(CAP_SYS_ADMIN)) {
			ret = -EPERM;
			goto out;
		}
		ret = btrfs_defrag_root(root);
		break;
	case S_IFREG:
		/*
		 * Note that this does not check the file descriptor for write
		 * access. This prevents defragmenting executables that are
		 * running and allows defrag on files open in read-only mode.
		 */
		if (!capable(CAP_SYS_ADMIN) &&
		    inode_permission(inode, MAY_WRITE)) {
			ret = -EPERM;
			goto out;
		}

		range = kzalloc(sizeof(*range), GFP_KERNEL);
		if (!range) {
			ret = -ENOMEM;
			goto out;
		}

		if (argp) {
			if (copy_from_user(range, argp,
					   sizeof(*range))) {
				ret = -EFAULT;
				kfree(range);
				goto out;
			}
			/* compression requires us to start the IO */
			if ((range->flags & BTRFS_DEFRAG_RANGE_COMPRESS)) {
				range->flags |= BTRFS_DEFRAG_RANGE_START_IO;
				range->extent_thresh = (u32)-1;
			}
		} else {
			/* the rest are all set to zero by kzalloc */
			range->len = (u64)-1;
		}
		ret = btrfs_defrag_file(file_inode(file), file,
					range, BTRFS_OLDEST_GENERATION, 0);
		if (ret > 0)
			ret = 0;
		kfree(range);
		break;
	default:
		ret = -EINVAL;
	}
out:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_add_dev(struct btrfs_fs_info *fs_info, void __user *arg)
{
	struct btrfs_ioctl_vol_args *vol_args;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (test_and_set_bit(BTRFS_FS_EXCL_OP, &fs_info->flags))
		return BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS;

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args)) {
		ret = PTR_ERR(vol_args);
		goto out;
	}

	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';
	ret = btrfs_init_new_device(fs_info, vol_args->name);

	if (!ret)
		btrfs_info(fs_info, "disk added %s", vol_args->name);

	kfree(vol_args);
out:
	clear_bit(BTRFS_FS_EXCL_OP, &fs_info->flags);
	return ret;
}

static long btrfs_ioctl_rm_dev_v2(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_ioctl_vol_args_v2 *vol_args;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args)) {
		ret = PTR_ERR(vol_args);
		goto err_drop;
	}

	/* Check for compatibility reject unknown flags */
	if (vol_args->flags & ~BTRFS_VOL_ARG_V2_FLAGS_SUPPORTED) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (test_and_set_bit(BTRFS_FS_EXCL_OP, &fs_info->flags)) {
		ret = BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS;
		goto out;
	}

	if (vol_args->flags & BTRFS_DEVICE_SPEC_BY_ID) {
		ret = btrfs_rm_device(fs_info, NULL, vol_args->devid);
	} else {
		vol_args->name[BTRFS_SUBVOL_NAME_MAX] = '\0';
		ret = btrfs_rm_device(fs_info, vol_args->name, 0);
	}
	clear_bit(BTRFS_FS_EXCL_OP, &fs_info->flags);

	if (!ret) {
		if (vol_args->flags & BTRFS_DEVICE_SPEC_BY_ID)
			btrfs_info(fs_info, "device deleted: id %llu",
					vol_args->devid);
		else
			btrfs_info(fs_info, "device deleted: %s",
					vol_args->name);
	}
out:
	kfree(vol_args);
err_drop:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_rm_dev(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_ioctl_vol_args *vol_args;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	if (test_and_set_bit(BTRFS_FS_EXCL_OP, &fs_info->flags)) {
		ret = BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS;
		goto out_drop_write;
	}

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args)) {
		ret = PTR_ERR(vol_args);
		goto out;
	}

	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';
	ret = btrfs_rm_device(fs_info, vol_args->name, 0);

	if (!ret)
		btrfs_info(fs_info, "disk deleted %s", vol_args->name);
	kfree(vol_args);
out:
	clear_bit(BTRFS_FS_EXCL_OP, &fs_info->flags);
out_drop_write:
	mnt_drop_write_file(file);

	return ret;
}

static long btrfs_ioctl_fs_info(struct btrfs_fs_info *fs_info,
				void __user *arg)
{
	struct btrfs_ioctl_fs_info_args *fi_args;
	struct btrfs_device *device;
	struct btrfs_fs_devices *fs_devices = fs_info->fs_devices;
	int ret = 0;

	fi_args = kzalloc(sizeof(*fi_args), GFP_KERNEL);
	if (!fi_args)
		return -ENOMEM;

	rcu_read_lock();
	fi_args->num_devices = fs_devices->num_devices;

	list_for_each_entry_rcu(device, &fs_devices->devices, dev_list) {
		if (device->devid > fi_args->max_id)
			fi_args->max_id = device->devid;
	}
	rcu_read_unlock();

	memcpy(&fi_args->fsid, fs_devices->fsid, sizeof(fi_args->fsid));
	fi_args->nodesize = fs_info->nodesize;
	fi_args->sectorsize = fs_info->sectorsize;
	fi_args->clone_alignment = fs_info->sectorsize;

	if (copy_to_user(arg, fi_args, sizeof(*fi_args)))
		ret = -EFAULT;

	kfree(fi_args);
	return ret;
}

static long btrfs_ioctl_dev_info(struct btrfs_fs_info *fs_info,
				 void __user *arg)
{
	struct btrfs_ioctl_dev_info_args *di_args;
	struct btrfs_device *dev;
	int ret = 0;
	char *s_uuid = NULL;

	di_args = memdup_user(arg, sizeof(*di_args));
	if (IS_ERR(di_args))
		return PTR_ERR(di_args);

	if (!btrfs_is_empty_uuid(di_args->uuid))
		s_uuid = di_args->uuid;

	rcu_read_lock();
	dev = btrfs_find_device(fs_info->fs_devices, di_args->devid, s_uuid,
				NULL, true);

	if (!dev) {
		ret = -ENODEV;
		goto out;
	}

	di_args->devid = dev->devid;
	di_args->bytes_used = btrfs_device_get_bytes_used(dev);
	di_args->total_bytes = btrfs_device_get_total_bytes(dev);
	memcpy(di_args->uuid, dev->uuid, sizeof(di_args->uuid));
	if (dev->name) {
		strncpy(di_args->path, rcu_str_deref(dev->name),
				sizeof(di_args->path) - 1);
		di_args->path[sizeof(di_args->path) - 1] = 0;
	} else {
		di_args->path[0] = '\0';
	}

out:
	rcu_read_unlock();
	if (ret == 0 && copy_to_user(arg, di_args, sizeof(*di_args)))
		ret = -EFAULT;

	kfree(di_args);
	return ret;
}

static void btrfs_double_extent_unlock(struct inode *inode1, u64 loff1,
				       struct inode *inode2, u64 loff2, u64 len)
{
	unlock_extent(&BTRFS_I(inode1)->io_tree, loff1, loff1 + len - 1);
	unlock_extent(&BTRFS_I(inode2)->io_tree, loff2, loff2 + len - 1);
}

static void btrfs_double_extent_lock(struct inode *inode1, u64 loff1,
				     struct inode *inode2, u64 loff2, u64 len)
{
	if (inode1 < inode2) {
		swap(inode1, inode2);
		swap(loff1, loff2);
	} else if (inode1 == inode2 && loff2 < loff1) {
		swap(loff1, loff2);
	}
	lock_extent(&BTRFS_I(inode1)->io_tree, loff1, loff1 + len - 1);
	lock_extent(&BTRFS_I(inode2)->io_tree, loff2, loff2 + len - 1);
}

static int btrfs_extent_same_range(struct inode *src, u64 loff, u64 len,
				   struct inode *dst, u64 dst_loff)
{
	int ret;

	/*
	 * Lock destination range to serialize with concurrent readpages() and
	 * source range to serialize with relocation.
	 */
	btrfs_double_extent_lock(src, loff, dst, dst_loff, len);
	ret = btrfs_clone(src, dst, loff, len, len, dst_loff, 1);
	btrfs_double_extent_unlock(src, loff, dst, dst_loff, len);

	return ret;
}

#define BTRFS_MAX_DEDUPE_LEN	SZ_16M

static int btrfs_extent_same(struct inode *src, u64 loff, u64 olen,
			     struct inode *dst, u64 dst_loff)
{
	int ret;
	u64 i, tail_len, chunk_count;
	struct btrfs_root *root_dst = BTRFS_I(dst)->root;

	spin_lock(&root_dst->root_item_lock);
	if (root_dst->send_in_progress) {
		btrfs_warn_rl(root_dst->fs_info,
"cannot deduplicate to root %llu while send operations are using it (%d in progress)",
			      root_dst->root_key.objectid,
			      root_dst->send_in_progress);
		spin_unlock(&root_dst->root_item_lock);
		return -EAGAIN;
	}
	root_dst->dedupe_in_progress++;
	spin_unlock(&root_dst->root_item_lock);

	tail_len = olen % BTRFS_MAX_DEDUPE_LEN;
	chunk_count = div_u64(olen, BTRFS_MAX_DEDUPE_LEN);

	for (i = 0; i < chunk_count; i++) {
		ret = btrfs_extent_same_range(src, loff, BTRFS_MAX_DEDUPE_LEN,
					      dst, dst_loff);
		if (ret)
			goto out;

		loff += BTRFS_MAX_DEDUPE_LEN;
		dst_loff += BTRFS_MAX_DEDUPE_LEN;
	}

	if (tail_len > 0)
		ret = btrfs_extent_same_range(src, loff, tail_len, dst,
					      dst_loff);
out:
	spin_lock(&root_dst->root_item_lock);
	root_dst->dedupe_in_progress--;
	spin_unlock(&root_dst->root_item_lock);

	return ret;
}

static int clone_finish_inode_update(struct btrfs_trans_handle *trans,
				     struct inode *inode,
				     u64 endoff,
				     const u64 destoff,
				     const u64 olen,
				     int no_time_update)
{
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;

	inode_inc_iversion(inode);
	if (!no_time_update)
		inode->i_mtime = inode->i_ctime = current_time(inode);
	/*
	 * We round up to the block size at eof when determining which
	 * extents to clone above, but shouldn't round up the file size.
	 */
	if (endoff > destoff + olen)
		endoff = destoff + olen;
	if (endoff > inode->i_size)
		btrfs_i_size_write(BTRFS_I(inode), endoff);

	ret = btrfs_update_inode(trans, root, inode);
	if (ret) {
		btrfs_abort_transaction(trans, ret);
		btrfs_end_transaction(trans);
		goto out;
	}
	ret = btrfs_end_transaction(trans);
out:
	return ret;
}

static void clone_update_extent_map(struct btrfs_inode *inode,
				    const struct btrfs_trans_handle *trans,
				    const struct btrfs_path *path,
				    const u64 hole_offset,
				    const u64 hole_len)
{
	struct extent_map_tree *em_tree = &inode->extent_tree;
	struct extent_map *em;
	int ret;

	em = alloc_extent_map();
	if (!em) {
		set_bit(BTRFS_INODE_NEEDS_FULL_SYNC, &inode->runtime_flags);
		return;
	}

	if (path) {
		struct btrfs_file_extent_item *fi;

		fi = btrfs_item_ptr(path->nodes[0], path->slots[0],
				    struct btrfs_file_extent_item);
		btrfs_extent_item_to_extent_map(inode, path, fi, false, em);
		em->generation = -1;
		if (btrfs_file_extent_type(path->nodes[0], fi) ==
		    BTRFS_FILE_EXTENT_INLINE)
			set_bit(BTRFS_INODE_NEEDS_FULL_SYNC,
					&inode->runtime_flags);
	} else {
		em->start = hole_offset;
		em->len = hole_len;
		em->ram_bytes = em->len;
		em->orig_start = hole_offset;
		em->block_start = EXTENT_MAP_HOLE;
		em->block_len = 0;
		em->orig_block_len = 0;
		em->compress_type = BTRFS_COMPRESS_NONE;
		em->generation = trans->transid;
	}

	while (1) {
		write_lock(&em_tree->lock);
		ret = add_extent_mapping(em_tree, em, 1);
		write_unlock(&em_tree->lock);
		if (ret != -EEXIST) {
			free_extent_map(em);
			break;
		}
		btrfs_drop_extent_cache(inode, em->start,
					em->start + em->len - 1, 0);
	}

	if (ret)
		set_bit(BTRFS_INODE_NEEDS_FULL_SYNC, &inode->runtime_flags);
}

/*
 * Make sure we do not end up inserting an inline extent into a file that has
 * already other (non-inline) extents. If a file has an inline extent it can
 * not have any other extents and the (single) inline extent must start at the
 * file offset 0. Failing to respect these rules will lead to file corruption,
 * resulting in EIO errors on read/write operations, hitting BUG_ON's in mm, etc
 *
 * We can have extents that have been already written to disk or we can have
 * dirty ranges still in delalloc, in which case the extent maps and items are
 * created only when we run delalloc, and the delalloc ranges might fall outside
 * the range we are currently locking in the inode's io tree. So we check the
 * inode's i_size because of that (i_size updates are done while holding the
 * i_mutex, which we are holding here).
 * We also check to see if the inode has a size not greater than "datal" but has
 * extents beyond it, due to an fallocate with FALLOC_FL_KEEP_SIZE (and we are
 * protected against such concurrent fallocate calls by the i_mutex).
 *
 * If the file has no extents but a size greater than datal, do not allow the
 * copy because we would need turn the inline extent into a non-inline one (even
 * with NO_HOLES enabled). If we find our destination inode only has one inline
 * extent, just overwrite it with the source inline extent if its size is less
 * than the source extent's size, or we could copy the source inline extent's
 * data into the destination inode's inline extent if the later is greater then
 * the former.
 */
static int clone_copy_inline_extent(struct inode *dst,
				    struct btrfs_trans_handle *trans,
				    struct btrfs_path *path,
				    struct btrfs_key *new_key,
				    const u64 drop_start,
				    const u64 datal,
				    const u64 skip,
				    const u64 size,
				    char *inline_data)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(dst->i_sb);
	struct btrfs_root *root = BTRFS_I(dst)->root;
	const u64 aligned_end = ALIGN(new_key->offset + datal,
				      fs_info->sectorsize);
	int ret;
	struct btrfs_key key;

	if (new_key->offset > 0)
		return -EOPNOTSUPP;

	key.objectid = btrfs_ino(BTRFS_I(dst));
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = 0;
	ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
	if (ret < 0) {
		return ret;
	} else if (ret > 0) {
		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				return ret;
			else if (ret > 0)
				goto copy_inline_extent;
		}
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		if (key.objectid == btrfs_ino(BTRFS_I(dst)) &&
		    key.type == BTRFS_EXTENT_DATA_KEY) {
			ASSERT(key.offset > 0);
			return -EOPNOTSUPP;
		}
	} else if (i_size_read(dst) <= datal) {
		struct btrfs_file_extent_item *ei;
		u64 ext_len;

		/*
		 * If the file size is <= datal, make sure there are no other
		 * extents following (can happen do to an fallocate call with
		 * the flag FALLOC_FL_KEEP_SIZE).
		 */
		ei = btrfs_item_ptr(path->nodes[0], path->slots[0],
				    struct btrfs_file_extent_item);
		/*
		 * If it's an inline extent, it can not have other extents
		 * following it.
		 */
		if (btrfs_file_extent_type(path->nodes[0], ei) ==
		    BTRFS_FILE_EXTENT_INLINE)
			goto copy_inline_extent;

		ext_len = btrfs_file_extent_num_bytes(path->nodes[0], ei);
		if (ext_len > aligned_end)
			return -EOPNOTSUPP;

		ret = btrfs_next_item(root, path);
		if (ret < 0) {
			return ret;
		} else if (ret == 0) {
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      path->slots[0]);
			if (key.objectid == btrfs_ino(BTRFS_I(dst)) &&
			    key.type == BTRFS_EXTENT_DATA_KEY)
				return -EOPNOTSUPP;
		}
	}

copy_inline_extent:
	/*
	 * We have no extent items, or we have an extent at offset 0 which may
	 * or may not be inlined. All these cases are dealt the same way.
	 */
	if (i_size_read(dst) > datal) {
		/*
		 * If the destination inode has an inline extent...
		 * This would require copying the data from the source inline
		 * extent into the beginning of the destination's inline extent.
		 * But this is really complex, both extents can be compressed
		 * or just one of them, which would require decompressing and
		 * re-compressing data (which could increase the new compressed
		 * size, not allowing the compressed data to fit anymore in an
		 * inline extent).
		 * So just don't support this case for now (it should be rare,
		 * we are not really saving space when cloning inline extents).
		 */
		return -EOPNOTSUPP;
	}

	btrfs_release_path(path);
	ret = btrfs_drop_extents(trans, root, dst, drop_start, aligned_end, 1);
	if (ret)
		return ret;
	ret = btrfs_insert_empty_item(trans, root, path, new_key, size);
	if (ret)
		return ret;

	if (skip) {
		const u32 start = btrfs_file_extent_calc_inline_size(0);

		memmove(inline_data + start, inline_data + start + skip, datal);
	}

	write_extent_buffer(path->nodes[0], inline_data,
			    btrfs_item_ptr_offset(path->nodes[0],
						  path->slots[0]),
			    size);
	inode_add_bytes(dst, datal);

	return 0;
}

/**
 * btrfs_clone() - clone a range from inode file to another
 *
 * @src: Inode to clone from
 * @inode: Inode to clone to
 * @off: Offset within source to start clone from
 * @olen: Original length, passed by user, of range to clone
 * @olen_aligned: Block-aligned value of olen
 * @destoff: Offset within @inode to start clone
 * @no_time_update: Whether to update mtime/ctime on the target inode
 */
static int btrfs_clone(struct inode *src, struct inode *inode,
		       const u64 off, const u64 olen, const u64 olen_aligned,
		       const u64 destoff, int no_time_update)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_path *path = NULL;
	struct extent_buffer *leaf;
	struct btrfs_trans_handle *trans;
	char *buf = NULL;
	struct btrfs_key key;
	u32 nritems;
	int slot;
	int ret;
	const u64 len = olen_aligned;
	u64 last_dest_end = destoff;

	ret = -ENOMEM;
	buf = kvmalloc(fs_info->nodesize, GFP_KERNEL);
	if (!buf)
		return ret;

	path = btrfs_alloc_path();
	if (!path) {
		kvfree(buf);
		return ret;
	}

	path->reada = READA_FORWARD;
	/* clone data */
	key.objectid = btrfs_ino(BTRFS_I(src));
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = off;

	while (1) {
		u64 next_key_min_offset = key.offset + 1;

		/*
		 * note the key will change type as we walk through the
		 * tree.
		 */
		path->leave_spinning = 1;
		ret = btrfs_search_slot(NULL, BTRFS_I(src)->root, &key, path,
				0, 0);
		if (ret < 0)
			goto out;
		/*
		 * First search, if no extent item that starts at offset off was
		 * found but the previous item is an extent item, it's possible
		 * it might overlap our target range, therefore process it.
		 */
		if (key.offset == off && ret > 0 && path->slots[0] > 0) {
			btrfs_item_key_to_cpu(path->nodes[0], &key,
					      path->slots[0] - 1);
			if (key.type == BTRFS_EXTENT_DATA_KEY)
				path->slots[0]--;
		}

		nritems = btrfs_header_nritems(path->nodes[0]);
process_slot:
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(BTRFS_I(src)->root, path);
			if (ret < 0)
				goto out;
			if (ret > 0)
				break;
			nritems = btrfs_header_nritems(path->nodes[0]);
		}
		leaf = path->nodes[0];
		slot = path->slots[0];

		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (key.type > BTRFS_EXTENT_DATA_KEY ||
		    key.objectid != btrfs_ino(BTRFS_I(src)))
			break;

		if (key.type == BTRFS_EXTENT_DATA_KEY) {
			struct btrfs_file_extent_item *extent;
			int type;
			u32 size;
			struct btrfs_key new_key;
			u64 disko = 0, diskl = 0;
			u64 datao = 0, datal = 0;
			u8 comp;
			u64 drop_start;

			extent = btrfs_item_ptr(leaf, slot,
						struct btrfs_file_extent_item);
			comp = btrfs_file_extent_compression(leaf, extent);
			type = btrfs_file_extent_type(leaf, extent);
			if (type == BTRFS_FILE_EXTENT_REG ||
			    type == BTRFS_FILE_EXTENT_PREALLOC) {
				disko = btrfs_file_extent_disk_bytenr(leaf,
								      extent);
				diskl = btrfs_file_extent_disk_num_bytes(leaf,
								 extent);
				datao = btrfs_file_extent_offset(leaf, extent);
				datal = btrfs_file_extent_num_bytes(leaf,
								    extent);
			} else if (type == BTRFS_FILE_EXTENT_INLINE) {
				/* take upper bound, may be compressed */
				datal = btrfs_file_extent_ram_bytes(leaf,
								    extent);
			}

			/*
			 * The first search might have left us at an extent
			 * item that ends before our target range's start, can
			 * happen if we have holes and NO_HOLES feature enabled.
			 */
			if (key.offset + datal <= off) {
				path->slots[0]++;
				goto process_slot;
			} else if (key.offset >= off + len) {
				break;
			}
			next_key_min_offset = key.offset + datal;
			size = btrfs_item_size_nr(leaf, slot);
			read_extent_buffer(leaf, buf,
					   btrfs_item_ptr_offset(leaf, slot),
					   size);

			btrfs_release_path(path);
			path->leave_spinning = 0;

			memcpy(&new_key, &key, sizeof(new_key));
			new_key.objectid = btrfs_ino(BTRFS_I(inode));
			if (off <= key.offset)
				new_key.offset = key.offset + destoff - off;
			else
				new_key.offset = destoff;

			/*
			 * Deal with a hole that doesn't have an extent item
			 * that represents it (NO_HOLES feature enabled).
			 * This hole is either in the middle of the cloning
			 * range or at the beginning (fully overlaps it or
			 * partially overlaps it).
			 */
			if (new_key.offset != last_dest_end)
				drop_start = last_dest_end;
			else
				drop_start = new_key.offset;

			/*
			 * 1 - adjusting old extent (we may have to split it)
			 * 1 - add new extent
			 * 1 - inode update
			 */
			trans = btrfs_start_transaction(root, 3);
			if (IS_ERR(trans)) {
				ret = PTR_ERR(trans);
				goto out;
			}

			if (type == BTRFS_FILE_EXTENT_REG ||
			    type == BTRFS_FILE_EXTENT_PREALLOC) {
				/*
				 *    a  | --- range to clone ---|  b
				 * | ------------- extent ------------- |
				 */

				/* subtract range b */
				if (key.offset + datal > off + len)
					datal = off + len - key.offset;

				/* subtract range a */
				if (off > key.offset) {
					datao += off - key.offset;
					datal -= off - key.offset;
				}

				ret = btrfs_drop_extents(trans, root, inode,
							 drop_start,
							 new_key.offset + datal,
							 1);
				if (ret) {
					if (ret != -EOPNOTSUPP)
						btrfs_abort_transaction(trans,
									ret);
					btrfs_end_transaction(trans);
					goto out;
				}

				ret = btrfs_insert_empty_item(trans, root, path,
							      &new_key, size);
				if (ret) {
					btrfs_abort_transaction(trans, ret);
					btrfs_end_transaction(trans);
					goto out;
				}

				leaf = path->nodes[0];
				slot = path->slots[0];
				write_extent_buffer(leaf, buf,
					    btrfs_item_ptr_offset(leaf, slot),
					    size);

				extent = btrfs_item_ptr(leaf, slot,
						struct btrfs_file_extent_item);

				/* disko == 0 means it's a hole */
				if (!disko)
					datao = 0;

				btrfs_set_file_extent_offset(leaf, extent,
							     datao);
				btrfs_set_file_extent_num_bytes(leaf, extent,
								datal);

				if (disko) {
					struct btrfs_ref ref = { 0 };
					inode_add_bytes(inode, datal);
					btrfs_init_generic_ref(&ref,
						BTRFS_ADD_DELAYED_REF, disko,
						diskl, 0);
					btrfs_init_data_ref(&ref,
						root->root_key.objectid,
						btrfs_ino(BTRFS_I(inode)),
						new_key.offset - datao);
					ret = btrfs_inc_extent_ref(trans, &ref);
					if (ret) {
						btrfs_abort_transaction(trans,
									ret);
						btrfs_end_transaction(trans);
						goto out;

					}
				}
			} else if (type == BTRFS_FILE_EXTENT_INLINE) {
				u64 skip = 0;
				u64 trim = 0;

				if (off > key.offset) {
					skip = off - key.offset;
					new_key.offset += skip;
				}

				if (key.offset + datal > off + len)
					trim = key.offset + datal - (off + len);

				if (comp && (skip || trim)) {
					ret = -EINVAL;
					btrfs_end_transaction(trans);
					goto out;
				}
				size -= skip + trim;
				datal -= skip + trim;

				ret = clone_copy_inline_extent(inode,
							       trans, path,
							       &new_key,
							       drop_start,
							       datal,
							       skip, size, buf);
				if (ret) {
					if (ret != -EOPNOTSUPP)
						btrfs_abort_transaction(trans,
									ret);
					btrfs_end_transaction(trans);
					goto out;
				}
				leaf = path->nodes[0];
				slot = path->slots[0];
			}

			/* If we have an implicit hole (NO_HOLES feature). */
			if (drop_start < new_key.offset)
				clone_update_extent_map(BTRFS_I(inode), trans,
						NULL, drop_start,
						new_key.offset - drop_start);

			clone_update_extent_map(BTRFS_I(inode), trans,
					path, 0, 0);

			btrfs_mark_buffer_dirty(leaf);
			btrfs_release_path(path);

			last_dest_end = ALIGN(new_key.offset + datal,
					      fs_info->sectorsize);
			ret = clone_finish_inode_update(trans, inode,
							last_dest_end,
							destoff, olen,
							no_time_update);
			if (ret)
				goto out;
			if (new_key.offset + datal >= destoff + len)
				break;
		}
		btrfs_release_path(path);
		key.offset = next_key_min_offset;

		if (fatal_signal_pending(current)) {
			ret = -EINTR;
			goto out;
		}
	}
	ret = 0;

	if (last_dest_end < destoff + len) {
		/*
		 * We have an implicit hole (NO_HOLES feature is enabled) that
		 * fully or partially overlaps our cloning range at its end.
		 */
		btrfs_release_path(path);

		/*
		 * 1 - remove extent(s)
		 * 1 - inode update
		 */
		trans = btrfs_start_transaction(root, 2);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out;
		}
		ret = btrfs_drop_extents(trans, root, inode,
					 last_dest_end, destoff + len, 1);
		if (ret) {
			if (ret != -EOPNOTSUPP)
				btrfs_abort_transaction(trans, ret);
			btrfs_end_transaction(trans);
			goto out;
		}
		clone_update_extent_map(BTRFS_I(inode), trans, NULL,
				last_dest_end,
				destoff + len - last_dest_end);
		ret = clone_finish_inode_update(trans, inode, destoff + len,
						destoff, olen, no_time_update);
	}

out:
	btrfs_free_path(path);
	kvfree(buf);
	return ret;
}

static noinline int btrfs_clone_files(struct file *file, struct file *file_src,
					u64 off, u64 olen, u64 destoff)
{
	struct inode *inode = file_inode(file);
	struct inode *src = file_inode(file_src);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	int ret;
	u64 len = olen;
	u64 bs = fs_info->sb->s_blocksize;

	/*
	 * TODO:
	 * - split compressed inline extents.  annoying: we need to
	 *   decompress into destination's address_space (the file offset
	 *   may change, so source mapping won't do), then recompress (or
	 *   otherwise reinsert) a subrange.
	 *
	 * - split destination inode's inline extents.  The inline extents can
	 *   be either compressed or non-compressed.
	 */

	/*
	 * VFS's generic_remap_file_range_prep() protects us from cloning the
	 * eof block into the middle of a file, which would result in corruption
	 * if the file size is not blocksize aligned. So we don't need to check
	 * for that case here.
	 */
	if (off + len == src->i_size)
		len = ALIGN(src->i_size, bs) - off;

	if (destoff > inode->i_size) {
		const u64 wb_start = ALIGN_DOWN(inode->i_size, bs);

		ret = btrfs_cont_expand(inode, inode->i_size, destoff);
		if (ret)
			return ret;
		/*
		 * We may have truncated the last block if the inode's size is
		 * not sector size aligned, so we need to wait for writeback to
		 * complete before proceeding further, otherwise we can race
		 * with cloning and attempt to increment a reference to an
		 * extent that no longer exists (writeback completed right after
		 * we found the previous extent covering eof and before we
		 * attempted to increment its reference count).
		 */
		ret = btrfs_wait_ordered_range(inode, wb_start,
					       destoff - wb_start);
		if (ret)
			return ret;
	}

	/*
	 * Lock destination range to serialize with concurrent readpages() and
	 * source range to serialize with relocation.
	 */
	btrfs_double_extent_lock(src, off, inode, destoff, len);
	ret = btrfs_clone(src, inode, off, olen, len, destoff, 0);
	btrfs_double_extent_unlock(src, off, inode, destoff, len);
	/*
	 * Truncate page cache pages so that future reads will see the cloned
	 * data immediately and not the previous data.
	 */
	truncate_inode_pages_range(&inode->i_data,
				round_down(destoff, PAGE_SIZE),
				round_up(destoff + len, PAGE_SIZE) - 1);

	return ret;
}

static int btrfs_remap_file_range_prep(struct file *file_in, loff_t pos_in,
				       struct file *file_out, loff_t pos_out,
				       loff_t *len, unsigned int remap_flags)
{
	struct inode *inode_in = file_inode(file_in);
	struct inode *inode_out = file_inode(file_out);
	u64 bs = BTRFS_I(inode_out)->root->fs_info->sb->s_blocksize;
	bool same_inode = inode_out == inode_in;
	u64 wb_len;
	int ret;

	if (!(remap_flags & REMAP_FILE_DEDUP)) {
		struct btrfs_root *root_out = BTRFS_I(inode_out)->root;

		if (btrfs_root_readonly(root_out))
			return -EROFS;

		if (file_in->f_path.mnt != file_out->f_path.mnt ||
		    inode_in->i_sb != inode_out->i_sb)
			return -EXDEV;
	}

	/* don't make the dst file partly checksummed */
	if ((BTRFS_I(inode_in)->flags & BTRFS_INODE_NODATASUM) !=
	    (BTRFS_I(inode_out)->flags & BTRFS_INODE_NODATASUM)) {
		return -EINVAL;
	}

	/*
	 * Now that the inodes are locked, we need to start writeback ourselves
	 * and can not rely on the writeback from the VFS's generic helper
	 * generic_remap_file_range_prep() because:
	 *
	 * 1) For compression we must call filemap_fdatawrite_range() range
	 *    twice (btrfs_fdatawrite_range() does it for us), and the generic
	 *    helper only calls it once;
	 *
	 * 2) filemap_fdatawrite_range(), called by the generic helper only
	 *    waits for the writeback to complete, i.e. for IO to be done, and
	 *    not for the ordered extents to complete. We need to wait for them
	 *    to complete so that new file extent items are in the fs tree.
	 */
	if (*len == 0 && !(remap_flags & REMAP_FILE_DEDUP))
		wb_len = ALIGN(inode_in->i_size, bs) - ALIGN_DOWN(pos_in, bs);
	else
		wb_len = ALIGN(*len, bs);

	/*
	 * Since we don't lock ranges, wait for ongoing lockless dio writes (as
	 * any in progress could create its ordered extents after we wait for
	 * existing ordered extents below).
	 */
	inode_dio_wait(inode_in);
	if (!same_inode)
		inode_dio_wait(inode_out);

	/*
	 * Workaround to make sure NOCOW buffered write reach disk as NOCOW.
	 *
	 * Btrfs' back references do not have a block level granularity, they
	 * work at the whole extent level.
	 * NOCOW buffered write without data space reserved may not be able
	 * to fall back to CoW due to lack of data space, thus could cause
	 * data loss.
	 *
	 * Here we take a shortcut by flushing the whole inode, so that all
	 * nocow write should reach disk as nocow before we increase the
	 * reference of the extent. We could do better by only flushing NOCOW
	 * data, but that needs extra accounting.
	 *
	 * Also we don't need to check ASYNC_EXTENT, as async extent will be
	 * CoWed anyway, not affecting nocow part.
	 */
	ret = filemap_flush(inode_in->i_mapping);
	if (ret < 0)
		return ret;

	ret = btrfs_wait_ordered_range(inode_in, ALIGN_DOWN(pos_in, bs),
				       wb_len);
	if (ret < 0)
		return ret;
	ret = btrfs_wait_ordered_range(inode_out, ALIGN_DOWN(pos_out, bs),
				       wb_len);
	if (ret < 0)
		return ret;

	return generic_remap_file_range_prep(file_in, pos_in, file_out, pos_out,
					    len, remap_flags);
}

loff_t btrfs_remap_file_range(struct file *src_file, loff_t off,
		struct file *dst_file, loff_t destoff, loff_t len,
		unsigned int remap_flags)
{
	struct inode *src_inode = file_inode(src_file);
	struct inode *dst_inode = file_inode(dst_file);
	bool same_inode = dst_inode == src_inode;
	int ret;

	if (remap_flags & ~(REMAP_FILE_DEDUP | REMAP_FILE_ADVISORY))
		return -EINVAL;

	if (same_inode)
		inode_lock(src_inode);
	else
		lock_two_nondirectories(src_inode, dst_inode);

	ret = btrfs_remap_file_range_prep(src_file, off, dst_file, destoff,
					  &len, remap_flags);
	if (ret < 0 || len == 0)
		goto out_unlock;

	if (remap_flags & REMAP_FILE_DEDUP)
		ret = btrfs_extent_same(src_inode, off, len, dst_inode, destoff);
	else
		ret = btrfs_clone_files(dst_file, src_file, off, len, destoff);

out_unlock:
	if (same_inode)
		inode_unlock(src_inode);
	else
		unlock_two_nondirectories(src_inode, dst_inode);

	return ret < 0 ? ret : len;
}

static long btrfs_ioctl_default_subvol(struct file *file, void __user *argp)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_root *new_root;
	struct btrfs_dir_item *di;
	struct btrfs_trans_handle *trans;
	struct btrfs_path *path;
	struct btrfs_key location;
	struct btrfs_disk_key disk_key;
	u64 objectid = 0;
	u64 dir_id;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	if (copy_from_user(&objectid, argp, sizeof(objectid))) {
		ret = -EFAULT;
		goto out;
	}

	if (!objectid)
		objectid = BTRFS_FS_TREE_OBJECTID;

	location.objectid = objectid;
	location.type = BTRFS_ROOT_ITEM_KEY;
	location.offset = (u64)-1;

	new_root = btrfs_read_fs_root_no_name(fs_info, &location);
	if (IS_ERR(new_root)) {
		ret = PTR_ERR(new_root);
		goto out;
	}
	if (!is_fstree(new_root->root_key.objectid)) {
		ret = -ENOENT;
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}
	path->leave_spinning = 1;

	trans = btrfs_start_transaction(root, 1);
	if (IS_ERR(trans)) {
		btrfs_free_path(path);
		ret = PTR_ERR(trans);
		goto out;
	}

	dir_id = btrfs_super_root_dir(fs_info->super_copy);
	di = btrfs_lookup_dir_item(trans, fs_info->tree_root, path,
				   dir_id, "default", 7, 1);
	if (IS_ERR_OR_NULL(di)) {
		btrfs_free_path(path);
		btrfs_end_transaction(trans);
		btrfs_err(fs_info,
			  "Umm, you don't have the default diritem, this isn't going to work");
		ret = -ENOENT;
		goto out;
	}

	btrfs_cpu_key_to_disk(&disk_key, &new_root->root_key);
	btrfs_set_dir_item_key(path->nodes[0], di, &disk_key);
	btrfs_mark_buffer_dirty(path->nodes[0]);
	btrfs_free_path(path);

	btrfs_set_fs_incompat(fs_info, DEFAULT_SUBVOL);
	btrfs_end_transaction(trans);
out:
	mnt_drop_write_file(file);
	return ret;
}

static void get_block_group_info(struct list_head *groups_list,
				 struct btrfs_ioctl_space_info *space)
{
	struct btrfs_block_group_cache *block_group;

	space->total_bytes = 0;
	space->used_bytes = 0;
	space->flags = 0;
	list_for_each_entry(block_group, groups_list, list) {
		space->flags = block_group->flags;
		space->total_bytes += block_group->key.offset;
		space->used_bytes +=
			btrfs_block_group_used(&block_group->item);
	}
}

static long btrfs_ioctl_space_info(struct btrfs_fs_info *fs_info,
				   void __user *arg)
{
	struct btrfs_ioctl_space_args space_args;
	struct btrfs_ioctl_space_info space;
	struct btrfs_ioctl_space_info *dest;
	struct btrfs_ioctl_space_info *dest_orig;
	struct btrfs_ioctl_space_info __user *user_dest;
	struct btrfs_space_info *info;
	static const u64 types[] = {
		BTRFS_BLOCK_GROUP_DATA,
		BTRFS_BLOCK_GROUP_SYSTEM,
		BTRFS_BLOCK_GROUP_METADATA,
		BTRFS_BLOCK_GROUP_DATA | BTRFS_BLOCK_GROUP_METADATA
	};
	int num_types = 4;
	int alloc_size;
	int ret = 0;
	u64 slot_count = 0;
	int i, c;

	if (copy_from_user(&space_args,
			   (struct btrfs_ioctl_space_args __user *)arg,
			   sizeof(space_args)))
		return -EFAULT;

	for (i = 0; i < num_types; i++) {
		struct btrfs_space_info *tmp;

		info = NULL;
		rcu_read_lock();
		list_for_each_entry_rcu(tmp, &fs_info->space_info,
					list) {
			if (tmp->flags == types[i]) {
				info = tmp;
				break;
			}
		}
		rcu_read_unlock();

		if (!info)
			continue;

		down_read(&info->groups_sem);
		for (c = 0; c < BTRFS_NR_RAID_TYPES; c++) {
			if (!list_empty(&info->block_groups[c]))
				slot_count++;
		}
		up_read(&info->groups_sem);
	}

	/*
	 * Global block reserve, exported as a space_info
	 */
	slot_count++;

	/* space_slots == 0 means they are asking for a count */
	if (space_args.space_slots == 0) {
		space_args.total_spaces = slot_count;
		goto out;
	}

	slot_count = min_t(u64, space_args.space_slots, slot_count);

	alloc_size = sizeof(*dest) * slot_count;

	/* we generally have at most 6 or so space infos, one for each raid
	 * level.  So, a whole page should be more than enough for everyone
	 */
	if (alloc_size > PAGE_SIZE)
		return -ENOMEM;

	space_args.total_spaces = 0;
	dest = kmalloc(alloc_size, GFP_KERNEL);
	if (!dest)
		return -ENOMEM;
	dest_orig = dest;

	/* now we have a buffer to copy into */
	for (i = 0; i < num_types; i++) {
		struct btrfs_space_info *tmp;

		if (!slot_count)
			break;

		info = NULL;
		rcu_read_lock();
		list_for_each_entry_rcu(tmp, &fs_info->space_info,
					list) {
			if (tmp->flags == types[i]) {
				info = tmp;
				break;
			}
		}
		rcu_read_unlock();

		if (!info)
			continue;
		down_read(&info->groups_sem);
		for (c = 0; c < BTRFS_NR_RAID_TYPES; c++) {
			if (!list_empty(&info->block_groups[c])) {
				get_block_group_info(&info->block_groups[c],
						     &space);
				memcpy(dest, &space, sizeof(space));
				dest++;
				space_args.total_spaces++;
				slot_count--;
			}
			if (!slot_count)
				break;
		}
		up_read(&info->groups_sem);
	}

	/*
	 * Add global block reserve
	 */
	if (slot_count) {
		struct btrfs_block_rsv *block_rsv = &fs_info->global_block_rsv;

		spin_lock(&block_rsv->lock);
		space.total_bytes = block_rsv->size;
		space.used_bytes = block_rsv->size - block_rsv->reserved;
		spin_unlock(&block_rsv->lock);
		space.flags = BTRFS_SPACE_INFO_GLOBAL_RSV;
		memcpy(dest, &space, sizeof(space));
		space_args.total_spaces++;
	}

	user_dest = (struct btrfs_ioctl_space_info __user *)
		(arg + sizeof(struct btrfs_ioctl_space_args));

	if (copy_to_user(user_dest, dest_orig, alloc_size))
		ret = -EFAULT;

	kfree(dest_orig);
out:
	if (ret == 0 && copy_to_user(arg, &space_args, sizeof(space_args)))
		ret = -EFAULT;

	return ret;
}

static noinline long btrfs_ioctl_start_sync(struct btrfs_root *root,
					    void __user *argp)
{
	struct btrfs_trans_handle *trans;
	u64 transid;
	int ret;

	trans = btrfs_attach_transaction_barrier(root);
	if (IS_ERR(trans)) {
		if (PTR_ERR(trans) != -ENOENT)
			return PTR_ERR(trans);

		/* No running transaction, don't bother */
		transid = root->fs_info->last_trans_committed;
		goto out;
	}
	transid = trans->transid;
	ret = btrfs_commit_transaction_async(trans, 0);
	if (ret) {
		btrfs_end_transaction(trans);
		return ret;
	}
out:
	if (argp)
		if (copy_to_user(argp, &transid, sizeof(transid)))
			return -EFAULT;
	return 0;
}

static noinline long btrfs_ioctl_wait_sync(struct btrfs_fs_info *fs_info,
					   void __user *argp)
{
	u64 transid;

	if (argp) {
		if (copy_from_user(&transid, argp, sizeof(transid)))
			return -EFAULT;
	} else {
		transid = 0;  /* current trans */
	}
	return btrfs_wait_for_commit(fs_info, transid);
}

static long btrfs_ioctl_scrub(struct file *file, void __user *arg)
{
	struct btrfs_fs_info *fs_info = btrfs_sb(file_inode(file)->i_sb);
	struct btrfs_ioctl_scrub_args *sa;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	if (!(sa->flags & BTRFS_SCRUB_READONLY)) {
		ret = mnt_want_write_file(file);
		if (ret)
			goto out;
	}

	ret = btrfs_scrub_dev(fs_info, sa->devid, sa->start, sa->end,
			      &sa->progress, sa->flags & BTRFS_SCRUB_READONLY,
			      0);

	if (ret == 0 && copy_to_user(arg, sa, sizeof(*sa)))
		ret = -EFAULT;

	if (!(sa->flags & BTRFS_SCRUB_READONLY))
		mnt_drop_write_file(file);
out:
	kfree(sa);
	return ret;
}

static long btrfs_ioctl_scrub_cancel(struct btrfs_fs_info *fs_info)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return btrfs_scrub_cancel(fs_info);
}

static long btrfs_ioctl_scrub_progress(struct btrfs_fs_info *fs_info,
				       void __user *arg)
{
	struct btrfs_ioctl_scrub_args *sa;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	ret = btrfs_scrub_progress(fs_info, sa->devid, &sa->progress);

	if (ret == 0 && copy_to_user(arg, sa, sizeof(*sa)))
		ret = -EFAULT;

	kfree(sa);
	return ret;
}

static long btrfs_ioctl_get_dev_stats(struct btrfs_fs_info *fs_info,
				      void __user *arg)
{
	struct btrfs_ioctl_get_dev_stats *sa;
	int ret;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	if ((sa->flags & BTRFS_DEV_STATS_RESET) && !capable(CAP_SYS_ADMIN)) {
		kfree(sa);
		return -EPERM;
	}

	ret = btrfs_get_dev_stats(fs_info, sa);

	if (ret == 0 && copy_to_user(arg, sa, sizeof(*sa)))
		ret = -EFAULT;

	kfree(sa);
	return ret;
}

static long btrfs_ioctl_dev_replace(struct btrfs_fs_info *fs_info,
				    void __user *arg)
{
	struct btrfs_ioctl_dev_replace_args *p;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	p = memdup_user(arg, sizeof(*p));
	if (IS_ERR(p))
		return PTR_ERR(p);

	switch (p->cmd) {
	case BTRFS_IOCTL_DEV_REPLACE_CMD_START:
		if (sb_rdonly(fs_info->sb)) {
			ret = -EROFS;
			goto out;
		}
		if (test_and_set_bit(BTRFS_FS_EXCL_OP, &fs_info->flags)) {
			ret = BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS;
		} else {
			ret = btrfs_dev_replace_by_ioctl(fs_info, p);
			clear_bit(BTRFS_FS_EXCL_OP, &fs_info->flags);
		}
		break;
	case BTRFS_IOCTL_DEV_REPLACE_CMD_STATUS:
		btrfs_dev_replace_status(fs_info, p);
		ret = 0;
		break;
	case BTRFS_IOCTL_DEV_REPLACE_CMD_CANCEL:
		p->result = btrfs_dev_replace_cancel(fs_info);
		ret = 0;
		break;
	default:
		ret = -EINVAL;
		break;
	}

	if ((ret == 0 || ret == -ECANCELED) && copy_to_user(arg, p, sizeof(*p)))
		ret = -EFAULT;
out:
	kfree(p);
	return ret;
}

static long btrfs_ioctl_ino_to_path(struct btrfs_root *root, void __user *arg)
{
	int ret = 0;
	int i;
	u64 rel_ptr;
	int size;
	struct btrfs_ioctl_ino_path_args *ipa = NULL;
	struct inode_fs_paths *ipath = NULL;
	struct btrfs_path *path;

	if (!capable(CAP_DAC_READ_SEARCH))
		return -EPERM;

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	ipa = memdup_user(arg, sizeof(*ipa));
	if (IS_ERR(ipa)) {
		ret = PTR_ERR(ipa);
		ipa = NULL;
		goto out;
	}

	size = min_t(u32, ipa->size, 4096);
	ipath = init_ipath(size, root, path);
	if (IS_ERR(ipath)) {
		ret = PTR_ERR(ipath);
		ipath = NULL;
		goto out;
	}

	ret = paths_from_inode(ipa->inum, ipath);
	if (ret < 0)
		goto out;

	for (i = 0; i < ipath->fspath->elem_cnt; ++i) {
		rel_ptr = ipath->fspath->val[i] -
			  (u64)(unsigned long)ipath->fspath->val;
		ipath->fspath->val[i] = rel_ptr;
	}

	ret = copy_to_user((void __user *)(unsigned long)ipa->fspath,
			   ipath->fspath, size);
	if (ret) {
		ret = -EFAULT;
		goto out;
	}

out:
	btrfs_free_path(path);
	free_ipath(ipath);
	kfree(ipa);

	return ret;
}

static int build_ino_list(u64 inum, u64 offset, u64 root, void *ctx)
{
	struct btrfs_data_container *inodes = ctx;
	const size_t c = 3 * sizeof(u64);

	if (inodes->bytes_left >= c) {
		inodes->bytes_left -= c;
		inodes->val[inodes->elem_cnt] = inum;
		inodes->val[inodes->elem_cnt + 1] = offset;
		inodes->val[inodes->elem_cnt + 2] = root;
		inodes->elem_cnt += 3;
	} else {
		inodes->bytes_missing += c - inodes->bytes_left;
		inodes->bytes_left = 0;
		inodes->elem_missed += 3;
	}

	return 0;
}

static long btrfs_ioctl_logical_to_ino(struct btrfs_fs_info *fs_info,
					void __user *arg, int version)
{
	int ret = 0;
	int size;
	struct btrfs_ioctl_logical_ino_args *loi;
	struct btrfs_data_container *inodes = NULL;
	struct btrfs_path *path = NULL;
	bool ignore_offset;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	loi = memdup_user(arg, sizeof(*loi));
	if (IS_ERR(loi))
		return PTR_ERR(loi);

	if (version == 1) {
		ignore_offset = false;
		size = min_t(u32, loi->size, SZ_64K);
	} else {
		/* All reserved bits must be 0 for now */
		if (memchr_inv(loi->reserved, 0, sizeof(loi->reserved))) {
			ret = -EINVAL;
			goto out_loi;
		}
		/* Only accept flags we have defined so far */
		if (loi->flags & ~(BTRFS_LOGICAL_INO_ARGS_IGNORE_OFFSET)) {
			ret = -EINVAL;
			goto out_loi;
		}
		ignore_offset = loi->flags & BTRFS_LOGICAL_INO_ARGS_IGNORE_OFFSET;
		size = min_t(u32, loi->size, SZ_16M);
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	inodes = init_data_container(size);
	if (IS_ERR(inodes)) {
		ret = PTR_ERR(inodes);
		inodes = NULL;
		goto out;
	}

	ret = iterate_inodes_from_logical(loi->logical, fs_info, path,
					  build_ino_list, inodes, ignore_offset);
	if (ret == -EINVAL)
		ret = -ENOENT;
	if (ret < 0)
		goto out;

	ret = copy_to_user((void __user *)(unsigned long)loi->inodes, inodes,
			   size);
	if (ret)
		ret = -EFAULT;

out:
	btrfs_free_path(path);
	kvfree(inodes);
out_loi:
	kfree(loi);

	return ret;
}

void btrfs_update_ioctl_balance_args(struct btrfs_fs_info *fs_info,
			       struct btrfs_ioctl_balance_args *bargs)
{
	struct btrfs_balance_control *bctl = fs_info->balance_ctl;

	bargs->flags = bctl->flags;

	if (test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags))
		bargs->state |= BTRFS_BALANCE_STATE_RUNNING;
	if (atomic_read(&fs_info->balance_pause_req))
		bargs->state |= BTRFS_BALANCE_STATE_PAUSE_REQ;
	if (atomic_read(&fs_info->balance_cancel_req))
		bargs->state |= BTRFS_BALANCE_STATE_CANCEL_REQ;

	memcpy(&bargs->data, &bctl->data, sizeof(bargs->data));
	memcpy(&bargs->meta, &bctl->meta, sizeof(bargs->meta));
	memcpy(&bargs->sys, &bctl->sys, sizeof(bargs->sys));

	spin_lock(&fs_info->balance_lock);
	memcpy(&bargs->stat, &bctl->stat, sizeof(bargs->stat));
	spin_unlock(&fs_info->balance_lock);
}

static long btrfs_ioctl_balance(struct file *file, void __user *arg)
{
	struct btrfs_root *root = BTRFS_I(file_inode(file))->root;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_ioctl_balance_args *bargs;
	struct btrfs_balance_control *bctl;
	bool need_unlock; /* for mut. excl. ops lock */
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

again:
	if (!test_and_set_bit(BTRFS_FS_EXCL_OP, &fs_info->flags)) {
		mutex_lock(&fs_info->balance_mutex);
		need_unlock = true;
		goto locked;
	}

	/*
	 * mut. excl. ops lock is locked.  Three possibilities:
	 *   (1) some other op is running
	 *   (2) balance is running
	 *   (3) balance is paused -- special case (think resume)
	 */
	mutex_lock(&fs_info->balance_mutex);
	if (fs_info->balance_ctl) {
		/* this is either (2) or (3) */
		if (!test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags)) {
			mutex_unlock(&fs_info->balance_mutex);
			/*
			 * Lock released to allow other waiters to continue,
			 * we'll reexamine the status again.
			 */
			mutex_lock(&fs_info->balance_mutex);

			if (fs_info->balance_ctl &&
			    !test_bit(BTRFS_FS_BALANCE_RUNNING, &fs_info->flags)) {
				/* this is (3) */
				need_unlock = false;
				goto locked;
			}

			mutex_unlock(&fs_info->balance_mutex);
			goto again;
		} else {
			/* this is (2) */
			mutex_unlock(&fs_info->balance_mutex);
			ret = -EINPROGRESS;
			goto out;
		}
	} else {
		/* this is (1) */
		mutex_unlock(&fs_info->balance_mutex);
		ret = BTRFS_ERROR_DEV_EXCL_RUN_IN_PROGRESS;
		goto out;
	}

locked:
	BUG_ON(!test_bit(BTRFS_FS_EXCL_OP, &fs_info->flags));

	if (arg) {
		bargs = memdup_user(arg, sizeof(*bargs));
		if (IS_ERR(bargs)) {
			ret = PTR_ERR(bargs);
			goto out_unlock;
		}

		if (bargs->flags & BTRFS_BALANCE_RESUME) {
			if (!fs_info->balance_ctl) {
				ret = -ENOTCONN;
				goto out_bargs;
			}

			bctl = fs_info->balance_ctl;
			spin_lock(&fs_info->balance_lock);
			bctl->flags |= BTRFS_BALANCE_RESUME;
			spin_unlock(&fs_info->balance_lock);

			goto do_balance;
		}
	} else {
		bargs = NULL;
	}

	if (fs_info->balance_ctl) {
		ret = -EINPROGRESS;
		goto out_bargs;
	}

	bctl = kzalloc(sizeof(*bctl), GFP_KERNEL);
	if (!bctl) {
		ret = -ENOMEM;
		goto out_bargs;
	}

	if (arg) {
		memcpy(&bctl->data, &bargs->data, sizeof(bctl->data));
		memcpy(&bctl->meta, &bargs->meta, sizeof(bctl->meta));
		memcpy(&bctl->sys, &bargs->sys, sizeof(bctl->sys));

		bctl->flags = bargs->flags;
	} else {
		/* balance everything - no filters */
		bctl->flags |= BTRFS_BALANCE_TYPE_MASK;
	}

	if (bctl->flags & ~(BTRFS_BALANCE_ARGS_MASK | BTRFS_BALANCE_TYPE_MASK)) {
		ret = -EINVAL;
		goto out_bctl;
	}

do_balance:
	/*
	 * Ownership of bctl and filesystem flag BTRFS_FS_EXCL_OP goes to
	 * btrfs_balance.  bctl is freed in reset_balance_state, or, if
	 * restriper was paused all the way until unmount, in free_fs_info.
	 * The flag should be cleared after reset_balance_state.
	 */
	need_unlock = false;

	ret = btrfs_balance(fs_info, bctl, bargs);
	bctl = NULL;

	if ((ret == 0 || ret == -ECANCELED) && arg) {
		if (copy_to_user(arg, bargs, sizeof(*bargs)))
			ret = -EFAULT;
	}

out_bctl:
	kfree(bctl);
out_bargs:
	kfree(bargs);
out_unlock:
	mutex_unlock(&fs_info->balance_mutex);
	if (need_unlock)
		clear_bit(BTRFS_FS_EXCL_OP, &fs_info->flags);
out:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_balance_ctl(struct btrfs_fs_info *fs_info, int cmd)
{
	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	switch (cmd) {
	case BTRFS_BALANCE_CTL_PAUSE:
		return btrfs_pause_balance(fs_info);
	case BTRFS_BALANCE_CTL_CANCEL:
		return btrfs_cancel_balance(fs_info);
	}

	return -EINVAL;
}

static long btrfs_ioctl_balance_progress(struct btrfs_fs_info *fs_info,
					 void __user *arg)
{
	struct btrfs_ioctl_balance_args *bargs;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	mutex_lock(&fs_info->balance_mutex);
	if (!fs_info->balance_ctl) {
		ret = -ENOTCONN;
		goto out;
	}

	bargs = kzalloc(sizeof(*bargs), GFP_KERNEL);
	if (!bargs) {
		ret = -ENOMEM;
		goto out;
	}

	btrfs_update_ioctl_balance_args(fs_info, bargs);

	if (copy_to_user(arg, bargs, sizeof(*bargs)))
		ret = -EFAULT;

	kfree(bargs);
out:
	mutex_unlock(&fs_info->balance_mutex);
	return ret;
}

static long btrfs_ioctl_quota_ctl(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_ioctl_quota_ctl_args *sa;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa)) {
		ret = PTR_ERR(sa);
		goto drop_write;
	}

	down_write(&fs_info->subvol_sem);

	switch (sa->cmd) {
	case BTRFS_QUOTA_CTL_ENABLE:
		ret = btrfs_quota_enable(fs_info);
		break;
	case BTRFS_QUOTA_CTL_DISABLE:
		ret = btrfs_quota_disable(fs_info);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	kfree(sa);
	up_write(&fs_info->subvol_sem);
drop_write:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_qgroup_assign(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_ioctl_qgroup_assign_args *sa;
	struct btrfs_trans_handle *trans;
	int ret;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa)) {
		ret = PTR_ERR(sa);
		goto drop_write;
	}

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	if (sa->assign) {
		ret = btrfs_add_qgroup_relation(trans, sa->src, sa->dst);
	} else {
		ret = btrfs_del_qgroup_relation(trans, sa->src, sa->dst);
	}

	/* update qgroup status and info */
	err = btrfs_run_qgroups(trans);
	if (err < 0)
		btrfs_handle_fs_error(fs_info, err,
				      "failed to update qgroup status and info");
	err = btrfs_end_transaction(trans);
	if (err && !ret)
		ret = err;

out:
	kfree(sa);
drop_write:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_qgroup_create(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_ioctl_qgroup_create_args *sa;
	struct btrfs_trans_handle *trans;
	int ret;
	int err;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa)) {
		ret = PTR_ERR(sa);
		goto drop_write;
	}

	if (!sa->qgroupid) {
		ret = -EINVAL;
		goto out;
	}

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	if (sa->create) {
		ret = btrfs_create_qgroup(trans, sa->qgroupid);
	} else {
		ret = btrfs_remove_qgroup(trans, sa->qgroupid);
	}

	err = btrfs_end_transaction(trans);
	if (err && !ret)
		ret = err;

out:
	kfree(sa);
drop_write:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_qgroup_limit(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_ioctl_qgroup_limit_args *sa;
	struct btrfs_trans_handle *trans;
	int ret;
	int err;
	u64 qgroupid;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa)) {
		ret = PTR_ERR(sa);
		goto drop_write;
	}

	trans = btrfs_join_transaction(root);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out;
	}

	qgroupid = sa->qgroupid;
	if (!qgroupid) {
		/* take the current subvol as qgroup */
		qgroupid = root->root_key.objectid;
	}

	ret = btrfs_limit_qgroup(trans, qgroupid, &sa->lim);

	err = btrfs_end_transaction(trans);
	if (err && !ret)
		ret = err;

out:
	kfree(sa);
drop_write:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_quota_rescan(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_ioctl_quota_rescan_args *qsa;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	qsa = memdup_user(arg, sizeof(*qsa));
	if (IS_ERR(qsa)) {
		ret = PTR_ERR(qsa);
		goto drop_write;
	}

	if (qsa->flags) {
		ret = -EINVAL;
		goto out;
	}

	ret = btrfs_qgroup_rescan(fs_info);

out:
	kfree(qsa);
drop_write:
	mnt_drop_write_file(file);
	return ret;
}

static long btrfs_ioctl_quota_rescan_status(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_ioctl_quota_rescan_args *qsa;
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	qsa = kzalloc(sizeof(*qsa), GFP_KERNEL);
	if (!qsa)
		return -ENOMEM;

	if (fs_info->qgroup_flags & BTRFS_QGROUP_STATUS_FLAG_RESCAN) {
		qsa->flags = 1;
		qsa->progress = fs_info->qgroup_rescan_progress.objectid;
	}

	if (copy_to_user(arg, qsa, sizeof(*qsa)))
		ret = -EFAULT;

	kfree(qsa);
	return ret;
}

static long btrfs_ioctl_quota_rescan_wait(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	return btrfs_qgroup_wait_for_completion(fs_info, true);
}

static long _btrfs_ioctl_set_received_subvol(struct file *file,
					    struct btrfs_ioctl_received_subvol_args *sa)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_root_item *root_item = &root->root_item;
	struct btrfs_trans_handle *trans;
	struct timespec64 ct = current_time(inode);
	int ret = 0;
	int received_uuid_changed;

	if (!inode_owner_or_capable(inode))
		return -EPERM;

	ret = mnt_want_write_file(file);
	if (ret < 0)
		return ret;

	down_write(&fs_info->subvol_sem);

	if (btrfs_ino(BTRFS_I(inode)) != BTRFS_FIRST_FREE_OBJECTID) {
		ret = -EINVAL;
		goto out;
	}

	if (btrfs_root_readonly(root)) {
		ret = -EROFS;
		goto out;
	}

	/*
	 * 1 - root item
	 * 2 - uuid items (received uuid + subvol uuid)
	 */
	trans = btrfs_start_transaction(root, 3);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		trans = NULL;
		goto out;
	}

	sa->rtransid = trans->transid;
	sa->rtime.sec = ct.tv_sec;
	sa->rtime.nsec = ct.tv_nsec;

	received_uuid_changed = memcmp(root_item->received_uuid, sa->uuid,
				       BTRFS_UUID_SIZE);
	if (received_uuid_changed &&
	    !btrfs_is_empty_uuid(root_item->received_uuid)) {
		ret = btrfs_uuid_tree_remove(trans, root_item->received_uuid,
					  BTRFS_UUID_KEY_RECEIVED_SUBVOL,
					  root->root_key.objectid);
		if (ret && ret != -ENOENT) {
		        btrfs_abort_transaction(trans, ret);
		        btrfs_end_transaction(trans);
		        goto out;
		}
	}
	memcpy(root_item->received_uuid, sa->uuid, BTRFS_UUID_SIZE);
	btrfs_set_root_stransid(root_item, sa->stransid);
	btrfs_set_root_rtransid(root_item, sa->rtransid);
	btrfs_set_stack_timespec_sec(&root_item->stime, sa->stime.sec);
	btrfs_set_stack_timespec_nsec(&root_item->stime, sa->stime.nsec);
	btrfs_set_stack_timespec_sec(&root_item->rtime, sa->rtime.sec);
	btrfs_set_stack_timespec_nsec(&root_item->rtime, sa->rtime.nsec);

	ret = btrfs_update_root(trans, fs_info->tree_root,
				&root->root_key, &root->root_item);
	if (ret < 0) {
		btrfs_end_transaction(trans);
		goto out;
	}
	if (received_uuid_changed && !btrfs_is_empty_uuid(sa->uuid)) {
		ret = btrfs_uuid_tree_add(trans, sa->uuid,
					  BTRFS_UUID_KEY_RECEIVED_SUBVOL,
					  root->root_key.objectid);
		if (ret < 0 && ret != -EEXIST) {
			btrfs_abort_transaction(trans, ret);
			btrfs_end_transaction(trans);
			goto out;
		}
	}
	ret = btrfs_commit_transaction(trans);
out:
	up_write(&fs_info->subvol_sem);
	mnt_drop_write_file(file);
	return ret;
}

#ifdef CONFIG_64BIT
static long btrfs_ioctl_set_received_subvol_32(struct file *file,
						void __user *arg)
{
	struct btrfs_ioctl_received_subvol_args_32 *args32 = NULL;
	struct btrfs_ioctl_received_subvol_args *args64 = NULL;
	int ret = 0;

	args32 = memdup_user(arg, sizeof(*args32));
	if (IS_ERR(args32))
		return PTR_ERR(args32);

	args64 = kmalloc(sizeof(*args64), GFP_KERNEL);
	if (!args64) {
		ret = -ENOMEM;
		goto out;
	}

	memcpy(args64->uuid, args32->uuid, BTRFS_UUID_SIZE);
	args64->stransid = args32->stransid;
	args64->rtransid = args32->rtransid;
	args64->stime.sec = args32->stime.sec;
	args64->stime.nsec = args32->stime.nsec;
	args64->rtime.sec = args32->rtime.sec;
	args64->rtime.nsec = args32->rtime.nsec;
	args64->flags = args32->flags;

	ret = _btrfs_ioctl_set_received_subvol(file, args64);
	if (ret)
		goto out;

	memcpy(args32->uuid, args64->uuid, BTRFS_UUID_SIZE);
	args32->stransid = args64->stransid;
	args32->rtransid = args64->rtransid;
	args32->stime.sec = args64->stime.sec;
	args32->stime.nsec = args64->stime.nsec;
	args32->rtime.sec = args64->rtime.sec;
	args32->rtime.nsec = args64->rtime.nsec;
	args32->flags = args64->flags;

	ret = copy_to_user(arg, args32, sizeof(*args32));
	if (ret)
		ret = -EFAULT;

out:
	kfree(args32);
	kfree(args64);
	return ret;
}
#endif

static long btrfs_ioctl_set_received_subvol(struct file *file,
					    void __user *arg)
{
	struct btrfs_ioctl_received_subvol_args *sa = NULL;
	int ret = 0;

	sa = memdup_user(arg, sizeof(*sa));
	if (IS_ERR(sa))
		return PTR_ERR(sa);

	ret = _btrfs_ioctl_set_received_subvol(file, sa);

	if (ret)
		goto out;

	ret = copy_to_user(arg, sa, sizeof(*sa));
	if (ret)
		ret = -EFAULT;

out:
	kfree(sa);
	return ret;
}

static int btrfs_ioctl_get_fslabel(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	size_t len;
	int ret;
	char label[BTRFS_LABEL_SIZE];

	spin_lock(&fs_info->super_lock);
	memcpy(label, fs_info->super_copy->label, BTRFS_LABEL_SIZE);
	spin_unlock(&fs_info->super_lock);

	len = strnlen(label, BTRFS_LABEL_SIZE);

	if (len == BTRFS_LABEL_SIZE) {
		btrfs_warn(fs_info,
			   "label is too long, return the first %zu bytes",
			   --len);
	}

	ret = copy_to_user(arg, label, len);

	return ret ? -EFAULT : 0;
}

static int btrfs_ioctl_set_fslabel(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_super_block *super_block = fs_info->super_copy;
	struct btrfs_trans_handle *trans;
	char label[BTRFS_LABEL_SIZE];
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(label, arg, sizeof(label)))
		return -EFAULT;

	if (strnlen(label, BTRFS_LABEL_SIZE) == BTRFS_LABEL_SIZE) {
		btrfs_err(fs_info,
			  "unable to set label with more than %d bytes",
			  BTRFS_LABEL_SIZE - 1);
		return -EINVAL;
	}

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_unlock;
	}

	spin_lock(&fs_info->super_lock);
	strcpy(super_block->label, label);
	spin_unlock(&fs_info->super_lock);
	ret = btrfs_commit_transaction(trans);

out_unlock:
	mnt_drop_write_file(file);
	return ret;
}

#define INIT_FEATURE_FLAGS(suffix) \
	{ .compat_flags = BTRFS_FEATURE_COMPAT_##suffix, \
	  .compat_ro_flags = BTRFS_FEATURE_COMPAT_RO_##suffix, \
	  .incompat_flags = BTRFS_FEATURE_INCOMPAT_##suffix }

int btrfs_ioctl_get_supported_features(void __user *arg)
{
	static const struct btrfs_ioctl_feature_flags features[3] = {
		INIT_FEATURE_FLAGS(SUPP),
		INIT_FEATURE_FLAGS(SAFE_SET),
		INIT_FEATURE_FLAGS(SAFE_CLEAR)
	};

	if (copy_to_user(arg, &features, sizeof(features)))
		return -EFAULT;

	return 0;
}

static int btrfs_ioctl_get_features(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_super_block *super_block = fs_info->super_copy;
	struct btrfs_ioctl_feature_flags features;

	features.compat_flags = btrfs_super_compat_flags(super_block);
	features.compat_ro_flags = btrfs_super_compat_ro_flags(super_block);
	features.incompat_flags = btrfs_super_incompat_flags(super_block);

	if (copy_to_user(arg, &features, sizeof(features)))
		return -EFAULT;

	return 0;
}

static int check_feature_bits(struct btrfs_fs_info *fs_info,
			      enum btrfs_feature_set set,
			      u64 change_mask, u64 flags, u64 supported_flags,
			      u64 safe_set, u64 safe_clear)
{
	const char *type = btrfs_feature_set_names[set];
	char *names;
	u64 disallowed, unsupported;
	u64 set_mask = flags & change_mask;
	u64 clear_mask = ~flags & change_mask;

	unsupported = set_mask & ~supported_flags;
	if (unsupported) {
		names = btrfs_printable_features(set, unsupported);
		if (names) {
			btrfs_warn(fs_info,
				   "this kernel does not support the %s feature bit%s",
				   names, strchr(names, ',') ? "s" : "");
			kfree(names);
		} else
			btrfs_warn(fs_info,
				   "this kernel does not support %s bits 0x%llx",
				   type, unsupported);
		return -EOPNOTSUPP;
	}

	disallowed = set_mask & ~safe_set;
	if (disallowed) {
		names = btrfs_printable_features(set, disallowed);
		if (names) {
			btrfs_warn(fs_info,
				   "can't set the %s feature bit%s while mounted",
				   names, strchr(names, ',') ? "s" : "");
			kfree(names);
		} else
			btrfs_warn(fs_info,
				   "can't set %s bits 0x%llx while mounted",
				   type, disallowed);
		return -EPERM;
	}

	disallowed = clear_mask & ~safe_clear;
	if (disallowed) {
		names = btrfs_printable_features(set, disallowed);
		if (names) {
			btrfs_warn(fs_info,
				   "can't clear the %s feature bit%s while mounted",
				   names, strchr(names, ',') ? "s" : "");
			kfree(names);
		} else
			btrfs_warn(fs_info,
				   "can't clear %s bits 0x%llx while mounted",
				   type, disallowed);
		return -EPERM;
	}

	return 0;
}

#define check_feature(fs_info, change_mask, flags, mask_base)	\
check_feature_bits(fs_info, FEAT_##mask_base, change_mask, flags,	\
		   BTRFS_FEATURE_ ## mask_base ## _SUPP,	\
		   BTRFS_FEATURE_ ## mask_base ## _SAFE_SET,	\
		   BTRFS_FEATURE_ ## mask_base ## _SAFE_CLEAR)

static int btrfs_ioctl_set_features(struct file *file, void __user *arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_super_block *super_block = fs_info->super_copy;
	struct btrfs_ioctl_feature_flags flags[2];
	struct btrfs_trans_handle *trans;
	u64 newflags;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(flags, arg, sizeof(flags)))
		return -EFAULT;

	/* Nothing to do */
	if (!flags[0].compat_flags && !flags[0].compat_ro_flags &&
	    !flags[0].incompat_flags)
		return 0;

	ret = check_feature(fs_info, flags[0].compat_flags,
			    flags[1].compat_flags, COMPAT);
	if (ret)
		return ret;

	ret = check_feature(fs_info, flags[0].compat_ro_flags,
			    flags[1].compat_ro_flags, COMPAT_RO);
	if (ret)
		return ret;

	ret = check_feature(fs_info, flags[0].incompat_flags,
			    flags[1].incompat_flags, INCOMPAT);
	if (ret)
		return ret;

	ret = mnt_want_write_file(file);
	if (ret)
		return ret;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_drop_write;
	}

	spin_lock(&fs_info->super_lock);
	newflags = btrfs_super_compat_flags(super_block);
	newflags |= flags[0].compat_flags & flags[1].compat_flags;
	newflags &= ~(flags[0].compat_flags & ~flags[1].compat_flags);
	btrfs_set_super_compat_flags(super_block, newflags);

	newflags = btrfs_super_compat_ro_flags(super_block);
	newflags |= flags[0].compat_ro_flags & flags[1].compat_ro_flags;
	newflags &= ~(flags[0].compat_ro_flags & ~flags[1].compat_ro_flags);
	btrfs_set_super_compat_ro_flags(super_block, newflags);

	newflags = btrfs_super_incompat_flags(super_block);
	newflags |= flags[0].incompat_flags & flags[1].incompat_flags;
	newflags &= ~(flags[0].incompat_flags & ~flags[1].incompat_flags);
	btrfs_set_super_incompat_flags(super_block, newflags);
	spin_unlock(&fs_info->super_lock);

	ret = btrfs_commit_transaction(trans);
out_drop_write:
	mnt_drop_write_file(file);

	return ret;
}

static int _btrfs_ioctl_send(struct file *file, void __user *argp, bool compat)
{
	struct btrfs_ioctl_send_args *arg;
	int ret;

	if (compat) {
#if defined(CONFIG_64BIT) && defined(CONFIG_COMPAT)
		struct btrfs_ioctl_send_args_32 args32;

		ret = copy_from_user(&args32, argp, sizeof(args32));
		if (ret)
			return -EFAULT;
		arg = kzalloc(sizeof(*arg), GFP_KERNEL);
		if (!arg)
			return -ENOMEM;
		arg->send_fd = args32.send_fd;
		arg->clone_sources_count = args32.clone_sources_count;
		arg->clone_sources = compat_ptr(args32.clone_sources);
		arg->parent_root = args32.parent_root;
		arg->flags = args32.flags;
		memcpy(arg->reserved, args32.reserved,
		       sizeof(args32.reserved));
#else
		return -ENOTTY;
#endif
	} else {
		arg = memdup_user(argp, sizeof(*arg));
		if (IS_ERR(arg))
			return PTR_ERR(arg);
	}
	ret = btrfs_ioctl_send(file, arg);
	kfree(arg);
	return ret;
}

long btrfs_ioctl(struct file *file, unsigned int
		cmd, unsigned long arg)
{
	struct inode *inode = file_inode(file);
	struct btrfs_fs_info *fs_info = btrfs_sb(inode->i_sb);
	struct btrfs_root *root = BTRFS_I(inode)->root;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case FS_IOC_GETFLAGS:
		return btrfs_ioctl_getflags(file, argp);
	case FS_IOC_SETFLAGS:
		return btrfs_ioctl_setflags(file, argp);
	case FS_IOC_GETVERSION:
		return btrfs_ioctl_getversion(file, argp);
	case FITRIM:
		return btrfs_ioctl_fitrim(file, argp);
	case BTRFS_IOC_SNAP_CREATE:
		return btrfs_ioctl_snap_create(file, argp, 0);
	case BTRFS_IOC_SNAP_CREATE_V2:
		return btrfs_ioctl_snap_create_v2(file, argp, 0);
	case BTRFS_IOC_SUBVOL_CREATE:
		return btrfs_ioctl_snap_create(file, argp, 1);
	case BTRFS_IOC_SUBVOL_CREATE_V2:
		return btrfs_ioctl_snap_create_v2(file, argp, 1);
	case BTRFS_IOC_SNAP_DESTROY:
		return btrfs_ioctl_snap_destroy(file, argp);
	case BTRFS_IOC_SUBVOL_GETFLAGS:
		return btrfs_ioctl_subvol_getflags(file, argp);
	case BTRFS_IOC_SUBVOL_SETFLAGS:
		return btrfs_ioctl_subvol_setflags(file, argp);
	case BTRFS_IOC_DEFAULT_SUBVOL:
		return btrfs_ioctl_default_subvol(file, argp);
	case BTRFS_IOC_DEFRAG:
		return btrfs_ioctl_defrag(file, NULL);
	case BTRFS_IOC_DEFRAG_RANGE:
		return btrfs_ioctl_defrag(file, argp);
	case BTRFS_IOC_RESIZE:
		return btrfs_ioctl_resize(file, argp);
	case BTRFS_IOC_ADD_DEV:
		return btrfs_ioctl_add_dev(fs_info, argp);
	case BTRFS_IOC_RM_DEV:
		return btrfs_ioctl_rm_dev(file, argp);
	case BTRFS_IOC_RM_DEV_V2:
		return btrfs_ioctl_rm_dev_v2(file, argp);
	case BTRFS_IOC_FS_INFO:
		return btrfs_ioctl_fs_info(fs_info, argp);
	case BTRFS_IOC_DEV_INFO:
		return btrfs_ioctl_dev_info(fs_info, argp);
	case BTRFS_IOC_BALANCE:
		return btrfs_ioctl_balance(file, NULL);
	case BTRFS_IOC_TREE_SEARCH:
		return btrfs_ioctl_tree_search(file, argp);
	case BTRFS_IOC_TREE_SEARCH_V2:
		return btrfs_ioctl_tree_search_v2(file, argp);
	case BTRFS_IOC_INO_LOOKUP:
		return btrfs_ioctl_ino_lookup(file, argp);
	case BTRFS_IOC_INO_PATHS:
		return btrfs_ioctl_ino_to_path(root, argp);
	case BTRFS_IOC_LOGICAL_INO:
		return btrfs_ioctl_logical_to_ino(fs_info, argp, 1);
	case BTRFS_IOC_LOGICAL_INO_V2:
		return btrfs_ioctl_logical_to_ino(fs_info, argp, 2);
	case BTRFS_IOC_SPACE_INFO:
		return btrfs_ioctl_space_info(fs_info, argp);
	case BTRFS_IOC_SYNC: {
		int ret;

		ret = btrfs_start_delalloc_roots(fs_info, -1);
		if (ret)
			return ret;
		ret = btrfs_sync_fs(inode->i_sb, 1);
		/*
		 * The transaction thread may want to do more work,
		 * namely it pokes the cleaner kthread that will start
		 * processing uncleaned subvols.
		 */
		wake_up_process(fs_info->transaction_kthread);
		return ret;
	}
	case BTRFS_IOC_START_SYNC:
		return btrfs_ioctl_start_sync(root, argp);
	case BTRFS_IOC_WAIT_SYNC:
		return btrfs_ioctl_wait_sync(fs_info, argp);
	case BTRFS_IOC_SCRUB:
		return btrfs_ioctl_scrub(file, argp);
	case BTRFS_IOC_SCRUB_CANCEL:
		return btrfs_ioctl_scrub_cancel(fs_info);
	case BTRFS_IOC_SCRUB_PROGRESS:
		return btrfs_ioctl_scrub_progress(fs_info, argp);
	case BTRFS_IOC_BALANCE_V2:
		return btrfs_ioctl_balance(file, argp);
	case BTRFS_IOC_BALANCE_CTL:
		return btrfs_ioctl_balance_ctl(fs_info, arg);
	case BTRFS_IOC_BALANCE_PROGRESS:
		return btrfs_ioctl_balance_progress(fs_info, argp);
	case BTRFS_IOC_SET_RECEIVED_SUBVOL:
		return btrfs_ioctl_set_received_subvol(file, argp);
#ifdef CONFIG_64BIT
	case BTRFS_IOC_SET_RECEIVED_SUBVOL_32:
		return btrfs_ioctl_set_received_subvol_32(file, argp);
#endif
	case BTRFS_IOC_SEND:
		return _btrfs_ioctl_send(file, argp, false);
#if defined(CONFIG_64BIT) && defined(CONFIG_COMPAT)
	case BTRFS_IOC_SEND_32:
		return _btrfs_ioctl_send(file, argp, true);
#endif
	case BTRFS_IOC_GET_DEV_STATS:
		return btrfs_ioctl_get_dev_stats(fs_info, argp);
	case BTRFS_IOC_QUOTA_CTL:
		return btrfs_ioctl_quota_ctl(file, argp);
	case BTRFS_IOC_QGROUP_ASSIGN:
		return btrfs_ioctl_qgroup_assign(file, argp);
	case BTRFS_IOC_QGROUP_CREATE:
		return btrfs_ioctl_qgroup_create(file, argp);
	case BTRFS_IOC_QGROUP_LIMIT:
		return btrfs_ioctl_qgroup_limit(file, argp);
	case BTRFS_IOC_QUOTA_RESCAN:
		return btrfs_ioctl_quota_rescan(file, argp);
	case BTRFS_IOC_QUOTA_RESCAN_STATUS:
		return btrfs_ioctl_quota_rescan_status(file, argp);
	case BTRFS_IOC_QUOTA_RESCAN_WAIT:
		return btrfs_ioctl_quota_rescan_wait(file, argp);
	case BTRFS_IOC_DEV_REPLACE:
		return btrfs_ioctl_dev_replace(fs_info, argp);
	case BTRFS_IOC_GET_FSLABEL:
		return btrfs_ioctl_get_fslabel(file, argp);
	case BTRFS_IOC_SET_FSLABEL:
		return btrfs_ioctl_set_fslabel(file, argp);
	case BTRFS_IOC_GET_SUPPORTED_FEATURES:
		return btrfs_ioctl_get_supported_features(argp);
	case BTRFS_IOC_GET_FEATURES:
		return btrfs_ioctl_get_features(file, argp);
	case BTRFS_IOC_SET_FEATURES:
		return btrfs_ioctl_set_features(file, argp);
	case FS_IOC_FSGETXATTR:
		return btrfs_ioctl_fsgetxattr(file, argp);
	case FS_IOC_FSSETXATTR:
		return btrfs_ioctl_fssetxattr(file, argp);
	case BTRFS_IOC_GET_SUBVOL_INFO:
		return btrfs_ioctl_get_subvol_info(file, argp);
	case BTRFS_IOC_GET_SUBVOL_ROOTREF:
		return btrfs_ioctl_get_subvol_rootref(file, argp);
	case BTRFS_IOC_INO_LOOKUP_USER:
		return btrfs_ioctl_ino_lookup_user(file, argp);
	}

	return -ENOTTY;
}

#ifdef CONFIG_COMPAT
long btrfs_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	/*
	 * These all access 32-bit values anyway so no further
	 * handling is necessary.
	 */
	switch (cmd) {
	case FS_IOC32_GETFLAGS:
		cmd = FS_IOC_GETFLAGS;
		break;
	case FS_IOC32_SETFLAGS:
		cmd = FS_IOC_SETFLAGS;
		break;
	case FS_IOC32_GETVERSION:
		cmd = FS_IOC_GETVERSION;
		break;
	}

	return btrfs_ioctl(file, cmd, (unsigned long) compat_ptr(arg));
}
#endif
