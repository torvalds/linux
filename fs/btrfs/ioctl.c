/*
 * Copyright (C) 2007 Oracle.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License v2 as published by the Free Software Foundation.
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

#include <linux/kernel.h>
#include <linux/bio.h>
#include <linux/buffer_head.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/fsnotify.h>
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/backing-dev.h>
#include <linux/mount.h>
#include <linux/mpage.h>
#include <linux/namei.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/statfs.h>
#include <linux/compat.h>
#include <linux/bit_spinlock.h>
#include <linux/security.h>
#include <linux/xattr.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include "compat.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "ioctl.h"
#include "print-tree.h"
#include "volumes.h"
#include "locking.h"

/* Mask out flags that are inappropriate for the given type of inode. */
static inline __u32 btrfs_mask_flags(umode_t mode, __u32 flags)
{
	if (S_ISDIR(mode))
		return flags;
	else if (S_ISREG(mode))
		return flags & ~FS_DIRSYNC_FL;
	else
		return flags & (FS_NODUMP_FL | FS_NOATIME_FL);
}

/*
 * Export inode flags to the format expected by the FS_IOC_GETFLAGS ioctl.
 */
static unsigned int btrfs_flags_to_ioctl(unsigned int flags)
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

	if ((flags & BTRFS_INODE_COMPRESS) && !(flags & BTRFS_INODE_NOCOMPRESS))
		iflags |= FS_COMPR_FL;
	else if (flags & BTRFS_INODE_NOCOMPRESS)
		iflags |= FS_NOCOMP_FL;

	return iflags;
}

/*
 * Update inode->i_flags based on the btrfs internal flags.
 */
void btrfs_update_iflags(struct inode *inode)
{
	struct btrfs_inode *ip = BTRFS_I(inode);

	inode->i_flags &= ~(S_SYNC|S_APPEND|S_IMMUTABLE|S_NOATIME|S_DIRSYNC);

	if (ip->flags & BTRFS_INODE_SYNC)
		inode->i_flags |= S_SYNC;
	if (ip->flags & BTRFS_INODE_IMMUTABLE)
		inode->i_flags |= S_IMMUTABLE;
	if (ip->flags & BTRFS_INODE_APPEND)
		inode->i_flags |= S_APPEND;
	if (ip->flags & BTRFS_INODE_NOATIME)
		inode->i_flags |= S_NOATIME;
	if (ip->flags & BTRFS_INODE_DIRSYNC)
		inode->i_flags |= S_DIRSYNC;
}

/*
 * Inherit flags from the parent inode.
 *
 * Unlike extN we don't have any flags we don't want to inherit currently.
 */
void btrfs_inherit_iflags(struct inode *inode, struct inode *dir)
{
	unsigned int flags;

	if (!dir)
		return;

	flags = BTRFS_I(dir)->flags;

	if (S_ISREG(inode->i_mode))
		flags &= ~BTRFS_INODE_DIRSYNC;
	else if (!S_ISDIR(inode->i_mode))
		flags &= (BTRFS_INODE_NODUMP | BTRFS_INODE_NOATIME);

	BTRFS_I(inode)->flags = flags;
	btrfs_update_iflags(inode);
}

static int btrfs_ioctl_getflags(struct file *file, void __user *arg)
{
	struct btrfs_inode *ip = BTRFS_I(file->f_path.dentry->d_inode);
	unsigned int flags = btrfs_flags_to_ioctl(ip->flags);

	if (copy_to_user(arg, &flags, sizeof(flags)))
		return -EFAULT;
	return 0;
}

static int check_flags(unsigned int flags)
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
	struct inode *inode = file->f_path.dentry->d_inode;
	struct btrfs_inode *ip = BTRFS_I(inode);
	struct btrfs_root *root = ip->root;
	struct btrfs_trans_handle *trans;
	unsigned int flags, oldflags;
	int ret;

	if (btrfs_root_readonly(root))
		return -EROFS;

	if (copy_from_user(&flags, arg, sizeof(flags)))
		return -EFAULT;

	ret = check_flags(flags);
	if (ret)
		return ret;

	if (!inode_owner_or_capable(inode))
		return -EACCES;

	mutex_lock(&inode->i_mutex);

	flags = btrfs_mask_flags(inode->i_mode, flags);
	oldflags = btrfs_flags_to_ioctl(ip->flags);
	if ((flags ^ oldflags) & (FS_APPEND_FL | FS_IMMUTABLE_FL)) {
		if (!capable(CAP_LINUX_IMMUTABLE)) {
			ret = -EPERM;
			goto out_unlock;
		}
	}

	ret = mnt_want_write(file->f_path.mnt);
	if (ret)
		goto out_unlock;

	if (flags & FS_SYNC_FL)
		ip->flags |= BTRFS_INODE_SYNC;
	else
		ip->flags &= ~BTRFS_INODE_SYNC;
	if (flags & FS_IMMUTABLE_FL)
		ip->flags |= BTRFS_INODE_IMMUTABLE;
	else
		ip->flags &= ~BTRFS_INODE_IMMUTABLE;
	if (flags & FS_APPEND_FL)
		ip->flags |= BTRFS_INODE_APPEND;
	else
		ip->flags &= ~BTRFS_INODE_APPEND;
	if (flags & FS_NODUMP_FL)
		ip->flags |= BTRFS_INODE_NODUMP;
	else
		ip->flags &= ~BTRFS_INODE_NODUMP;
	if (flags & FS_NOATIME_FL)
		ip->flags |= BTRFS_INODE_NOATIME;
	else
		ip->flags &= ~BTRFS_INODE_NOATIME;
	if (flags & FS_DIRSYNC_FL)
		ip->flags |= BTRFS_INODE_DIRSYNC;
	else
		ip->flags &= ~BTRFS_INODE_DIRSYNC;
	if (flags & FS_NOCOW_FL)
		ip->flags |= BTRFS_INODE_NODATACOW;
	else
		ip->flags &= ~BTRFS_INODE_NODATACOW;

	/*
	 * The COMPRESS flag can only be changed by users, while the NOCOMPRESS
	 * flag may be changed automatically if compression code won't make
	 * things smaller.
	 */
	if (flags & FS_NOCOMP_FL) {
		ip->flags &= ~BTRFS_INODE_COMPRESS;
		ip->flags |= BTRFS_INODE_NOCOMPRESS;
	} else if (flags & FS_COMPR_FL) {
		ip->flags |= BTRFS_INODE_COMPRESS;
		ip->flags &= ~BTRFS_INODE_NOCOMPRESS;
	} else {
		ip->flags &= ~(BTRFS_INODE_COMPRESS | BTRFS_INODE_NOCOMPRESS);
	}

	trans = btrfs_join_transaction(root, 1);
	BUG_ON(IS_ERR(trans));

	ret = btrfs_update_inode(trans, root, inode);
	BUG_ON(ret);

	btrfs_update_iflags(inode);
	inode->i_ctime = CURRENT_TIME;
	btrfs_end_transaction(trans, root);

	mnt_drop_write(file->f_path.mnt);

	ret = 0;
 out_unlock:
	mutex_unlock(&inode->i_mutex);
	return ret;
}

static int btrfs_ioctl_getversion(struct file *file, int __user *arg)
{
	struct inode *inode = file->f_path.dentry->d_inode;

	return put_user(inode->i_generation, arg);
}

static noinline int btrfs_ioctl_fitrim(struct file *file, void __user *arg)
{
	struct btrfs_root *root = fdentry(file)->d_sb->s_fs_info;
	struct btrfs_fs_info *fs_info = root->fs_info;
	struct btrfs_device *device;
	struct request_queue *q;
	struct fstrim_range range;
	u64 minlen = ULLONG_MAX;
	u64 num_devices = 0;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	mutex_lock(&fs_info->fs_devices->device_list_mutex);
	list_for_each_entry(device, &fs_info->fs_devices->devices, dev_list) {
		if (!device->bdev)
			continue;
		q = bdev_get_queue(device->bdev);
		if (blk_queue_discard(q)) {
			num_devices++;
			minlen = min((u64)q->limits.discard_granularity,
				     minlen);
		}
	}
	mutex_unlock(&fs_info->fs_devices->device_list_mutex);
	if (!num_devices)
		return -EOPNOTSUPP;

	if (copy_from_user(&range, arg, sizeof(range)))
		return -EFAULT;

	range.minlen = max(range.minlen, minlen);
	ret = btrfs_trim_fs(root, &range);
	if (ret < 0)
		return ret;

	if (copy_to_user(arg, &range, sizeof(range)))
		return -EFAULT;

	return 0;
}

static noinline int create_subvol(struct btrfs_root *root,
				  struct dentry *dentry,
				  char *name, int namelen,
				  u64 *async_transid)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_key key;
	struct btrfs_root_item root_item;
	struct btrfs_inode_item *inode_item;
	struct extent_buffer *leaf;
	struct btrfs_root *new_root;
	struct dentry *parent = dget_parent(dentry);
	struct inode *dir;
	int ret;
	int err;
	u64 objectid;
	u64 new_dirid = BTRFS_FIRST_FREE_OBJECTID;
	u64 index = 0;

	ret = btrfs_find_free_objectid(NULL, root->fs_info->tree_root,
				       0, &objectid);
	if (ret) {
		dput(parent);
		return ret;
	}

	dir = parent->d_inode;

	/*
	 * 1 - inode item
	 * 2 - refs
	 * 1 - root item
	 * 2 - dir items
	 */
	trans = btrfs_start_transaction(root, 6);
	if (IS_ERR(trans)) {
		dput(parent);
		return PTR_ERR(trans);
	}

	leaf = btrfs_alloc_free_block(trans, root, root->leafsize,
				      0, objectid, NULL, 0, 0, 0);
	if (IS_ERR(leaf)) {
		ret = PTR_ERR(leaf);
		goto fail;
	}

	memset_extent_buffer(leaf, 0, 0, sizeof(struct btrfs_header));
	btrfs_set_header_bytenr(leaf, leaf->start);
	btrfs_set_header_generation(leaf, trans->transid);
	btrfs_set_header_backref_rev(leaf, BTRFS_MIXED_BACKREF_REV);
	btrfs_set_header_owner(leaf, objectid);

	write_extent_buffer(leaf, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(leaf),
			    BTRFS_FSID_SIZE);
	write_extent_buffer(leaf, root->fs_info->chunk_tree_uuid,
			    (unsigned long)btrfs_header_chunk_tree_uuid(leaf),
			    BTRFS_UUID_SIZE);
	btrfs_mark_buffer_dirty(leaf);

	inode_item = &root_item.inode;
	memset(inode_item, 0, sizeof(*inode_item));
	inode_item->generation = cpu_to_le64(1);
	inode_item->size = cpu_to_le64(3);
	inode_item->nlink = cpu_to_le32(1);
	inode_item->nbytes = cpu_to_le64(root->leafsize);
	inode_item->mode = cpu_to_le32(S_IFDIR | 0755);

	root_item.flags = 0;
	root_item.byte_limit = 0;
	inode_item->flags = cpu_to_le64(BTRFS_INODE_ROOT_ITEM_INIT);

	btrfs_set_root_bytenr(&root_item, leaf->start);
	btrfs_set_root_generation(&root_item, trans->transid);
	btrfs_set_root_level(&root_item, 0);
	btrfs_set_root_refs(&root_item, 1);
	btrfs_set_root_used(&root_item, leaf->len);
	btrfs_set_root_last_snapshot(&root_item, 0);

	memset(&root_item.drop_progress, 0, sizeof(root_item.drop_progress));
	root_item.drop_level = 0;

	btrfs_tree_unlock(leaf);
	free_extent_buffer(leaf);
	leaf = NULL;

	btrfs_set_root_dirid(&root_item, new_dirid);

	key.objectid = objectid;
	key.offset = 0;
	btrfs_set_key_type(&key, BTRFS_ROOT_ITEM_KEY);
	ret = btrfs_insert_root(trans, root->fs_info->tree_root, &key,
				&root_item);
	if (ret)
		goto fail;

	key.offset = (u64)-1;
	new_root = btrfs_read_fs_root_no_name(root->fs_info, &key);
	BUG_ON(IS_ERR(new_root));

	btrfs_record_root_in_trans(trans, new_root);

	ret = btrfs_create_subvol_root(trans, new_root, new_dirid,
				       BTRFS_I(dir)->block_group);
	/*
	 * insert the directory item
	 */
	ret = btrfs_set_inode_index(dir, &index);
	BUG_ON(ret);

	ret = btrfs_insert_dir_item(trans, root,
				    name, namelen, dir->i_ino, &key,
				    BTRFS_FT_DIR, index);
	if (ret)
		goto fail;

	btrfs_i_size_write(dir, dir->i_size + namelen * 2);
	ret = btrfs_update_inode(trans, root, dir);
	BUG_ON(ret);

	ret = btrfs_add_root_ref(trans, root->fs_info->tree_root,
				 objectid, root->root_key.objectid,
				 dir->i_ino, index, name, namelen);

	BUG_ON(ret);

	d_instantiate(dentry, btrfs_lookup_dentry(dir, dentry));
fail:
	dput(parent);
	if (async_transid) {
		*async_transid = trans->transid;
		err = btrfs_commit_transaction_async(trans, root, 1);
	} else {
		err = btrfs_commit_transaction(trans, root);
	}
	if (err && !ret)
		ret = err;
	return ret;
}

static int create_snapshot(struct btrfs_root *root, struct dentry *dentry,
			   char *name, int namelen, u64 *async_transid,
			   bool readonly)
{
	struct inode *inode;
	struct dentry *parent;
	struct btrfs_pending_snapshot *pending_snapshot;
	struct btrfs_trans_handle *trans;
	int ret;

	if (!root->ref_cows)
		return -EINVAL;

	pending_snapshot = kzalloc(sizeof(*pending_snapshot), GFP_NOFS);
	if (!pending_snapshot)
		return -ENOMEM;

	btrfs_init_block_rsv(&pending_snapshot->block_rsv);
	pending_snapshot->dentry = dentry;
	pending_snapshot->root = root;
	pending_snapshot->readonly = readonly;

	trans = btrfs_start_transaction(root->fs_info->extent_root, 5);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto fail;
	}

	ret = btrfs_snap_reserve_metadata(trans, pending_snapshot);
	BUG_ON(ret);

	list_add(&pending_snapshot->list,
		 &trans->transaction->pending_snapshots);
	if (async_transid) {
		*async_transid = trans->transid;
		ret = btrfs_commit_transaction_async(trans,
				     root->fs_info->extent_root, 1);
	} else {
		ret = btrfs_commit_transaction(trans,
					       root->fs_info->extent_root);
	}
	BUG_ON(ret);

	ret = pending_snapshot->error;
	if (ret)
		goto fail;

	ret = btrfs_orphan_cleanup(pending_snapshot->snap);
	if (ret)
		goto fail;

	parent = dget_parent(dentry);
	inode = btrfs_lookup_dentry(parent->d_inode, dentry);
	dput(parent);
	if (IS_ERR(inode)) {
		ret = PTR_ERR(inode);
		goto fail;
	}
	BUG_ON(!inode);
	d_instantiate(dentry, inode);
	ret = 0;
fail:
	kfree(pending_snapshot);
	return ret;
}

/*  copy of check_sticky in fs/namei.c()
* It's inline, so penalty for filesystems that don't use sticky bit is
* minimal.
*/
static inline int btrfs_check_sticky(struct inode *dir, struct inode *inode)
{
	uid_t fsuid = current_fsuid();

	if (!(dir->i_mode & S_ISVTX))
		return 0;
	if (inode->i_uid == fsuid)
		return 0;
	if (dir->i_uid == fsuid)
		return 0;
	return !capable(CAP_FOWNER);
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
 *  6. If the victim is append-only or immutable we can't do antyhing with
 *     links pointing to it.
 *  7. If we were asked to remove a directory and victim isn't one - ENOTDIR.
 *  8. If we were asked to remove a non-directory and victim isn't one - EISDIR.
 *  9. We can't remove a root or mountpoint.
 * 10. We don't allow removal of NFS sillyrenamed files; it's handled by
 *     nfs_async_unlink().
 */

static int btrfs_may_delete(struct inode *dir,struct dentry *victim,int isdir)
{
	int error;

	if (!victim->d_inode)
		return -ENOENT;

	BUG_ON(victim->d_parent->d_inode != dir);
	audit_inode_child(victim, dir);

	error = inode_permission(dir, MAY_WRITE | MAY_EXEC);
	if (error)
		return error;
	if (IS_APPEND(dir))
		return -EPERM;
	if (btrfs_check_sticky(dir, victim->d_inode)||
		IS_APPEND(victim->d_inode)||
	    IS_IMMUTABLE(victim->d_inode) || IS_SWAPFILE(victim->d_inode))
		return -EPERM;
	if (isdir) {
		if (!S_ISDIR(victim->d_inode->i_mode))
			return -ENOTDIR;
		if (IS_ROOT(victim))
			return -EBUSY;
	} else if (S_ISDIR(victim->d_inode->i_mode))
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
	if (child->d_inode)
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
static noinline int btrfs_mksubvol(struct path *parent,
				   char *name, int namelen,
				   struct btrfs_root *snap_src,
				   u64 *async_transid, bool readonly)
{
	struct inode *dir  = parent->dentry->d_inode;
	struct dentry *dentry;
	int error;

	mutex_lock_nested(&dir->i_mutex, I_MUTEX_PARENT);

	dentry = lookup_one_len(name, parent->dentry, namelen);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out_unlock;

	error = -EEXIST;
	if (dentry->d_inode)
		goto out_dput;

	error = mnt_want_write(parent->mnt);
	if (error)
		goto out_dput;

	error = btrfs_may_create(dir, dentry);
	if (error)
		goto out_drop_write;

	down_read(&BTRFS_I(dir)->root->fs_info->subvol_sem);

	if (btrfs_root_refs(&BTRFS_I(dir)->root->root_item) == 0)
		goto out_up_read;

	if (snap_src) {
		error = create_snapshot(snap_src, dentry,
					name, namelen, async_transid, readonly);
	} else {
		error = create_subvol(BTRFS_I(dir)->root, dentry,
				      name, namelen, async_transid);
	}
	if (!error)
		fsnotify_mkdir(dir, dentry);
out_up_read:
	up_read(&BTRFS_I(dir)->root->fs_info->subvol_sem);
out_drop_write:
	mnt_drop_write(parent->mnt);
out_dput:
	dput(dentry);
out_unlock:
	mutex_unlock(&dir->i_mutex);
	return error;
}

static int should_defrag_range(struct inode *inode, u64 start, u64 len,
			       int thresh, u64 *last_len, u64 *skip,
			       u64 *defrag_end)
{
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct extent_map *em = NULL;
	struct extent_map_tree *em_tree = &BTRFS_I(inode)->extent_tree;
	int ret = 1;


	if (thresh == 0)
		thresh = 256 * 1024;

	/*
	 * make sure that once we start defragging and extent, we keep on
	 * defragging it
	 */
	if (start < *defrag_end)
		return 1;

	*skip = 0;

	/*
	 * hopefully we have this extent in the tree already, try without
	 * the full extent lock
	 */
	read_lock(&em_tree->lock);
	em = lookup_extent_mapping(em_tree, start, len);
	read_unlock(&em_tree->lock);

	if (!em) {
		/* get the big lock and read metadata off disk */
		lock_extent(io_tree, start, start + len - 1, GFP_NOFS);
		em = btrfs_get_extent(inode, NULL, 0, start, len, 0);
		unlock_extent(io_tree, start, start + len - 1, GFP_NOFS);

		if (IS_ERR(em))
			return 0;
	}

	/* this will cover holes, and inline extents */
	if (em->block_start >= EXTENT_MAP_LAST_BYTE)
		ret = 0;

	/*
	 * we hit a real extent, if it is big don't bother defragging it again
	 */
	if ((*last_len == 0 || *last_len >= thresh) && em->len >= thresh)
		ret = 0;

	/*
	 * last_len ends up being a counter of how many bytes we've defragged.
	 * every time we choose not to defrag an extent, we reset *last_len
	 * so that the next tiny extent will force a defrag.
	 *
	 * The end result of this is that tiny extents before a single big
	 * extent will force at least part of that big extent to be defragged.
	 */
	if (ret) {
		*last_len += len;
		*defrag_end = extent_map_end(em);
	} else {
		*last_len = 0;
		*skip = extent_map_end(em);
		*defrag_end = 0;
	}

	free_extent_map(em);
	return ret;
}

static int btrfs_defrag_file(struct file *file,
			     struct btrfs_ioctl_defrag_range_args *range)
{
	struct inode *inode = fdentry(file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_ordered_extent *ordered;
	struct page *page;
	struct btrfs_super_block *disk_super;
	unsigned long last_index;
	unsigned long ra_pages = root->fs_info->bdi.ra_pages;
	unsigned long total_read = 0;
	u64 features;
	u64 page_start;
	u64 page_end;
	u64 last_len = 0;
	u64 skip = 0;
	u64 defrag_end = 0;
	unsigned long i;
	int ret;
	int compress_type = BTRFS_COMPRESS_ZLIB;

	if (range->flags & BTRFS_DEFRAG_RANGE_COMPRESS) {
		if (range->compress_type > BTRFS_COMPRESS_TYPES)
			return -EINVAL;
		if (range->compress_type)
			compress_type = range->compress_type;
	}

	if (inode->i_size == 0)
		return 0;

	if (range->start + range->len > range->start) {
		last_index = min_t(u64, inode->i_size - 1,
			 range->start + range->len - 1) >> PAGE_CACHE_SHIFT;
	} else {
		last_index = (inode->i_size - 1) >> PAGE_CACHE_SHIFT;
	}

	i = range->start >> PAGE_CACHE_SHIFT;
	while (i <= last_index) {
		if (!should_defrag_range(inode, (u64)i << PAGE_CACHE_SHIFT,
					PAGE_CACHE_SIZE,
					range->extent_thresh,
					&last_len, &skip,
					&defrag_end)) {
			unsigned long next;
			/*
			 * the should_defrag function tells us how much to skip
			 * bump our counter by the suggested amount
			 */
			next = (skip + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
			i = max(i + 1, next);
			continue;
		}

		if (total_read % ra_pages == 0) {
			btrfs_force_ra(inode->i_mapping, &file->f_ra, file, i,
				       min(last_index, i + ra_pages - 1));
		}
		total_read++;
		mutex_lock(&inode->i_mutex);
		if (range->flags & BTRFS_DEFRAG_RANGE_COMPRESS)
			BTRFS_I(inode)->force_compress = compress_type;

		ret  = btrfs_delalloc_reserve_space(inode, PAGE_CACHE_SIZE);
		if (ret)
			goto err_unlock;
again:
		if (inode->i_size == 0 ||
		    i > ((inode->i_size - 1) >> PAGE_CACHE_SHIFT)) {
			ret = 0;
			goto err_reservations;
		}

		page = grab_cache_page(inode->i_mapping, i);
		if (!page) {
			ret = -ENOMEM;
			goto err_reservations;
		}

		if (!PageUptodate(page)) {
			btrfs_readpage(NULL, page);
			lock_page(page);
			if (!PageUptodate(page)) {
				unlock_page(page);
				page_cache_release(page);
				ret = -EIO;
				goto err_reservations;
			}
		}

		if (page->mapping != inode->i_mapping) {
			unlock_page(page);
			page_cache_release(page);
			goto again;
		}

		wait_on_page_writeback(page);

		if (PageDirty(page)) {
			btrfs_delalloc_release_space(inode, PAGE_CACHE_SIZE);
			goto loop_unlock;
		}

		page_start = (u64)page->index << PAGE_CACHE_SHIFT;
		page_end = page_start + PAGE_CACHE_SIZE - 1;
		lock_extent(io_tree, page_start, page_end, GFP_NOFS);

		ordered = btrfs_lookup_ordered_extent(inode, page_start);
		if (ordered) {
			unlock_extent(io_tree, page_start, page_end, GFP_NOFS);
			unlock_page(page);
			page_cache_release(page);
			btrfs_start_ordered_extent(inode, ordered, 1);
			btrfs_put_ordered_extent(ordered);
			goto again;
		}
		set_page_extent_mapped(page);

		/*
		 * this makes sure page_mkwrite is called on the
		 * page if it is dirtied again later
		 */
		clear_page_dirty_for_io(page);
		clear_extent_bits(&BTRFS_I(inode)->io_tree, page_start,
				  page_end, EXTENT_DIRTY | EXTENT_DELALLOC |
				  EXTENT_DO_ACCOUNTING, GFP_NOFS);

		btrfs_set_extent_delalloc(inode, page_start, page_end, NULL);
		ClearPageChecked(page);
		set_page_dirty(page);
		unlock_extent(io_tree, page_start, page_end, GFP_NOFS);

loop_unlock:
		unlock_page(page);
		page_cache_release(page);
		mutex_unlock(&inode->i_mutex);

		balance_dirty_pages_ratelimited_nr(inode->i_mapping, 1);
		i++;
	}

	if ((range->flags & BTRFS_DEFRAG_RANGE_START_IO))
		filemap_flush(inode->i_mapping);

	if ((range->flags & BTRFS_DEFRAG_RANGE_COMPRESS)) {
		/* the filemap_flush will queue IO into the worker threads, but
		 * we have to make sure the IO is actually started and that
		 * ordered extents get created before we return
		 */
		atomic_inc(&root->fs_info->async_submit_draining);
		while (atomic_read(&root->fs_info->nr_async_submits) ||
		      atomic_read(&root->fs_info->async_delalloc_pages)) {
			wait_event(root->fs_info->async_submit_wait,
			   (atomic_read(&root->fs_info->nr_async_submits) == 0 &&
			    atomic_read(&root->fs_info->async_delalloc_pages) == 0));
		}
		atomic_dec(&root->fs_info->async_submit_draining);

		mutex_lock(&inode->i_mutex);
		BTRFS_I(inode)->force_compress = BTRFS_COMPRESS_NONE;
		mutex_unlock(&inode->i_mutex);
	}

	disk_super = &root->fs_info->super_copy;
	features = btrfs_super_incompat_flags(disk_super);
	if (range->compress_type == BTRFS_COMPRESS_LZO) {
		features |= BTRFS_FEATURE_INCOMPAT_COMPRESS_LZO;
		btrfs_set_super_incompat_flags(disk_super, features);
	}

	return 0;

err_reservations:
	btrfs_delalloc_release_space(inode, PAGE_CACHE_SIZE);
err_unlock:
	mutex_unlock(&inode->i_mutex);
	return ret;
}

static noinline int btrfs_ioctl_resize(struct btrfs_root *root,
					void __user *arg)
{
	u64 new_size;
	u64 old_size;
	u64 devid = 1;
	struct btrfs_ioctl_vol_args *vol_args;
	struct btrfs_trans_handle *trans;
	struct btrfs_device *device = NULL;
	char *sizestr;
	char *devstr = NULL;
	int ret = 0;
	int mod = 0;

	if (root->fs_info->sb->s_flags & MS_RDONLY)
		return -EROFS;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args))
		return PTR_ERR(vol_args);

	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';

	mutex_lock(&root->fs_info->volume_mutex);
	sizestr = vol_args->name;
	devstr = strchr(sizestr, ':');
	if (devstr) {
		char *end;
		sizestr = devstr + 1;
		*devstr = '\0';
		devstr = vol_args->name;
		devid = simple_strtoull(devstr, &end, 10);
		printk(KERN_INFO "resizing devid %llu\n",
		       (unsigned long long)devid);
	}
	device = btrfs_find_device(root, devid, NULL, NULL);
	if (!device) {
		printk(KERN_INFO "resizer unable to find device %llu\n",
		       (unsigned long long)devid);
		ret = -EINVAL;
		goto out_unlock;
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
		new_size = memparse(sizestr, NULL);
		if (new_size == 0) {
			ret = -EINVAL;
			goto out_unlock;
		}
	}

	old_size = device->total_bytes;

	if (mod < 0) {
		if (new_size > old_size) {
			ret = -EINVAL;
			goto out_unlock;
		}
		new_size = old_size - new_size;
	} else if (mod > 0) {
		new_size = old_size + new_size;
	}

	if (new_size < 256 * 1024 * 1024) {
		ret = -EINVAL;
		goto out_unlock;
	}
	if (new_size > device->bdev->bd_inode->i_size) {
		ret = -EFBIG;
		goto out_unlock;
	}

	do_div(new_size, root->sectorsize);
	new_size *= root->sectorsize;

	printk(KERN_INFO "new size for %s is %llu\n",
		device->name, (unsigned long long)new_size);

	if (new_size > old_size) {
		trans = btrfs_start_transaction(root, 0);
		if (IS_ERR(trans)) {
			ret = PTR_ERR(trans);
			goto out_unlock;
		}
		ret = btrfs_grow_device(trans, device, new_size);
		btrfs_commit_transaction(trans, root);
	} else {
		ret = btrfs_shrink_device(device, new_size);
	}

out_unlock:
	mutex_unlock(&root->fs_info->volume_mutex);
	kfree(vol_args);
	return ret;
}

static noinline int btrfs_ioctl_snap_create_transid(struct file *file,
						    char *name,
						    unsigned long fd,
						    int subvol,
						    u64 *transid,
						    bool readonly)
{
	struct btrfs_root *root = BTRFS_I(fdentry(file)->d_inode)->root;
	struct file *src_file;
	int namelen;
	int ret = 0;

	if (root->fs_info->sb->s_flags & MS_RDONLY)
		return -EROFS;

	namelen = strlen(name);
	if (strchr(name, '/')) {
		ret = -EINVAL;
		goto out;
	}

	if (subvol) {
		ret = btrfs_mksubvol(&file->f_path, name, namelen,
				     NULL, transid, readonly);
	} else {
		struct inode *src_inode;
		src_file = fget(fd);
		if (!src_file) {
			ret = -EINVAL;
			goto out;
		}

		src_inode = src_file->f_path.dentry->d_inode;
		if (src_inode->i_sb != file->f_path.dentry->d_inode->i_sb) {
			printk(KERN_INFO "btrfs: Snapshot src from "
			       "another FS\n");
			ret = -EINVAL;
			fput(src_file);
			goto out;
		}
		ret = btrfs_mksubvol(&file->f_path, name, namelen,
				     BTRFS_I(src_inode)->root,
				     transid, readonly);
		fput(src_file);
	}
out:
	return ret;
}

static noinline int btrfs_ioctl_snap_create(struct file *file,
					    void __user *arg, int subvol)
{
	struct btrfs_ioctl_vol_args *vol_args;
	int ret;

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args))
		return PTR_ERR(vol_args);
	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';

	ret = btrfs_ioctl_snap_create_transid(file, vol_args->name,
					      vol_args->fd, subvol,
					      NULL, false);

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

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args))
		return PTR_ERR(vol_args);
	vol_args->name[BTRFS_SUBVOL_NAME_MAX] = '\0';

	if (vol_args->flags &
	    ~(BTRFS_SUBVOL_CREATE_ASYNC | BTRFS_SUBVOL_RDONLY)) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (vol_args->flags & BTRFS_SUBVOL_CREATE_ASYNC)
		ptr = &transid;
	if (vol_args->flags & BTRFS_SUBVOL_RDONLY)
		readonly = true;

	ret = btrfs_ioctl_snap_create_transid(file, vol_args->name,
					      vol_args->fd, subvol,
					      ptr, readonly);

	if (ret == 0 && ptr &&
	    copy_to_user(arg +
			 offsetof(struct btrfs_ioctl_vol_args_v2,
				  transid), ptr, sizeof(*ptr)))
		ret = -EFAULT;
out:
	kfree(vol_args);
	return ret;
}

static noinline int btrfs_ioctl_subvol_getflags(struct file *file,
						void __user *arg)
{
	struct inode *inode = fdentry(file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret = 0;
	u64 flags = 0;

	if (inode->i_ino != BTRFS_FIRST_FREE_OBJECTID)
		return -EINVAL;

	down_read(&root->fs_info->subvol_sem);
	if (btrfs_root_readonly(root))
		flags |= BTRFS_SUBVOL_RDONLY;
	up_read(&root->fs_info->subvol_sem);

	if (copy_to_user(arg, &flags, sizeof(flags)))
		ret = -EFAULT;

	return ret;
}

static noinline int btrfs_ioctl_subvol_setflags(struct file *file,
					      void __user *arg)
{
	struct inode *inode = fdentry(file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	u64 root_flags;
	u64 flags;
	int ret = 0;

	if (root->fs_info->sb->s_flags & MS_RDONLY)
		return -EROFS;

	if (inode->i_ino != BTRFS_FIRST_FREE_OBJECTID)
		return -EINVAL;

	if (copy_from_user(&flags, arg, sizeof(flags)))
		return -EFAULT;

	if (flags & BTRFS_SUBVOL_CREATE_ASYNC)
		return -EINVAL;

	if (flags & ~BTRFS_SUBVOL_RDONLY)
		return -EOPNOTSUPP;

	if (!inode_owner_or_capable(inode))
		return -EACCES;

	down_write(&root->fs_info->subvol_sem);

	/* nothing to do */
	if (!!(flags & BTRFS_SUBVOL_RDONLY) == btrfs_root_readonly(root))
		goto out;

	root_flags = btrfs_root_flags(&root->root_item);
	if (flags & BTRFS_SUBVOL_RDONLY)
		btrfs_set_root_flags(&root->root_item,
				     root_flags | BTRFS_ROOT_SUBVOL_RDONLY);
	else
		btrfs_set_root_flags(&root->root_item,
				     root_flags & ~BTRFS_ROOT_SUBVOL_RDONLY);

	trans = btrfs_start_transaction(root, 1);
	if (IS_ERR(trans)) {
		ret = PTR_ERR(trans);
		goto out_reset;
	}

	ret = btrfs_update_root(trans, root->fs_info->tree_root,
				&root->root_key, &root->root_item);

	btrfs_commit_transaction(trans, root);
out_reset:
	if (ret)
		btrfs_set_root_flags(&root->root_item, root_flags);
out:
	up_write(&root->fs_info->subvol_sem);
	return ret;
}

/*
 * helper to check if the subvolume references other subvolumes
 */
static noinline int may_destroy_subvol(struct btrfs_root *root)
{
	struct btrfs_path *path;
	struct btrfs_key key;
	int ret;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;

	key.objectid = root->root_key.objectid;
	key.type = BTRFS_ROOT_REF_KEY;
	key.offset = (u64)-1;

	ret = btrfs_search_slot(NULL, root->fs_info->tree_root,
				&key, path, 0, 0);
	if (ret < 0)
		goto out;
	BUG_ON(ret == 0);

	ret = 0;
	if (path->slots[0] > 0) {
		path->slots[0]--;
		btrfs_item_key_to_cpu(path->nodes[0], &key, path->slots[0]);
		if (key.objectid == root->root_key.objectid &&
		    key.type == BTRFS_ROOT_REF_KEY)
			ret = -ENOTEMPTY;
	}
out:
	btrfs_free_path(path);
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

static noinline int copy_to_sk(struct btrfs_root *root,
			       struct btrfs_path *path,
			       struct btrfs_key *key,
			       struct btrfs_ioctl_search_key *sk,
			       char *buf,
			       unsigned long *sk_offset,
			       int *num_found)
{
	u64 found_transid;
	struct extent_buffer *leaf;
	struct btrfs_ioctl_search_header sh;
	unsigned long item_off;
	unsigned long item_len;
	int nritems;
	int i;
	int slot;
	int found = 0;
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

		if (item_len > BTRFS_SEARCH_ARGS_BUFSIZE)
			item_len = 0;

		if (sizeof(sh) + item_len + *sk_offset >
		    BTRFS_SEARCH_ARGS_BUFSIZE) {
			ret = 1;
			goto overflow;
		}

		btrfs_item_key_to_cpu(leaf, key, i);
		if (!key_in_sk(key, sk))
			continue;

		sh.objectid = key->objectid;
		sh.offset = key->offset;
		sh.type = key->type;
		sh.len = item_len;
		sh.transid = found_transid;

		/* copy search result header */
		memcpy(buf + *sk_offset, &sh, sizeof(sh));
		*sk_offset += sizeof(sh);

		if (item_len) {
			char *p = buf + *sk_offset;
			/* copy the item */
			read_extent_buffer(leaf, p,
					   item_off, item_len);
			*sk_offset += item_len;
		}
		found++;

		if (*num_found >= sk->nr_items)
			break;
	}
advance_key:
	ret = 0;
	if (key->offset < (u64)-1 && key->offset < sk->max_offset)
		key->offset++;
	else if (key->type < (u8)-1 && key->type < sk->max_type) {
		key->offset = 0;
		key->type++;
	} else if (key->objectid < (u64)-1 && key->objectid < sk->max_objectid) {
		key->offset = 0;
		key->type = 0;
		key->objectid++;
	} else
		ret = 1;
overflow:
	*num_found += found;
	return ret;
}

static noinline int search_ioctl(struct inode *inode,
				 struct btrfs_ioctl_search_args *args)
{
	struct btrfs_root *root;
	struct btrfs_key key;
	struct btrfs_key max_key;
	struct btrfs_path *path;
	struct btrfs_ioctl_search_key *sk = &args->key;
	struct btrfs_fs_info *info = BTRFS_I(inode)->root->fs_info;
	int ret;
	int num_found = 0;
	unsigned long sk_offset = 0;

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
			printk(KERN_ERR "could not find root %llu\n",
			       sk->tree_id);
			btrfs_free_path(path);
			return -ENOENT;
		}
	}

	key.objectid = sk->min_objectid;
	key.type = sk->min_type;
	key.offset = sk->min_offset;

	max_key.objectid = sk->max_objectid;
	max_key.type = sk->max_type;
	max_key.offset = sk->max_offset;

	path->keep_locks = 1;

	while(1) {
		ret = btrfs_search_forward(root, &key, &max_key, path, 0,
					   sk->min_transid);
		if (ret != 0) {
			if (ret > 0)
				ret = 0;
			goto err;
		}
		ret = copy_to_sk(root, path, &key, sk, args->buf,
				 &sk_offset, &num_found);
		btrfs_release_path(root, path);
		if (ret || num_found >= sk->nr_items)
			break;

	}
	ret = 0;
err:
	sk->nr_items = num_found;
	btrfs_free_path(path);
	return ret;
}

static noinline int btrfs_ioctl_tree_search(struct file *file,
					   void __user *argp)
{
	 struct btrfs_ioctl_search_args *args;
	 struct inode *inode;
	 int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	args = memdup_user(argp, sizeof(*args));
	if (IS_ERR(args))
		return PTR_ERR(args);

	inode = fdentry(file)->d_inode;
	ret = search_ioctl(inode, args);
	if (ret == 0 && copy_to_user(argp, args, sizeof(*args)))
		ret = -EFAULT;
	kfree(args);
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

	ptr = &name[BTRFS_INO_LOOKUP_PATH_MAX];

	key.objectid = tree_id;
	key.type = BTRFS_ROOT_ITEM_KEY;
	key.offset = (u64)-1;
	root = btrfs_read_fs_root_no_name(info, &key);
	if (IS_ERR(root)) {
		printk(KERN_ERR "could not find root %llu\n", tree_id);
		ret = -ENOENT;
		goto out;
	}

	key.objectid = dirid;
	key.type = BTRFS_INODE_REF_KEY;
	key.offset = (u64)-1;

	while(1) {
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto out;

		l = path->nodes[0];
		slot = path->slots[0];
		if (ret > 0 && slot > 0)
			slot--;
		btrfs_item_key_to_cpu(l, &key, slot);

		if (ret > 0 && (key.objectid != dirid ||
				key.type != BTRFS_INODE_REF_KEY)) {
			ret = -ENOENT;
			goto out;
		}

		iref = btrfs_item_ptr(l, slot, struct btrfs_inode_ref);
		len = btrfs_inode_ref_name_len(l, iref);
		ptr -= len + 1;
		total_len += len + 1;
		if (ptr < name)
			goto out;

		*(ptr + len) = '/';
		read_extent_buffer(l, ptr,(unsigned long)(iref + 1), len);

		if (key.offset == BTRFS_FIRST_FREE_OBJECTID)
			break;

		btrfs_release_path(root, path);
		key.objectid = key.offset;
		key.offset = (u64)-1;
		dirid = key.objectid;

	}
	if (ptr < name)
		goto out;
	memcpy(name, ptr, total_len);
	name[total_len]='\0';
	ret = 0;
out:
	btrfs_free_path(path);
	return ret;
}

static noinline int btrfs_ioctl_ino_lookup(struct file *file,
					   void __user *argp)
{
	 struct btrfs_ioctl_ino_lookup_args *args;
	 struct inode *inode;
	 int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	args = memdup_user(argp, sizeof(*args));
	if (IS_ERR(args))
		return PTR_ERR(args);

	inode = fdentry(file)->d_inode;

	if (args->treeid == 0)
		args->treeid = BTRFS_I(inode)->root->root_key.objectid;

	ret = btrfs_search_path_in_tree(BTRFS_I(inode)->root->fs_info,
					args->treeid, args->objectid,
					args->name);

	if (ret == 0 && copy_to_user(argp, args, sizeof(*args)))
		ret = -EFAULT;

	kfree(args);
	return ret;
}

static noinline int btrfs_ioctl_snap_destroy(struct file *file,
					     void __user *arg)
{
	struct dentry *parent = fdentry(file);
	struct dentry *dentry;
	struct inode *dir = parent->d_inode;
	struct inode *inode;
	struct btrfs_root *root = BTRFS_I(dir)->root;
	struct btrfs_root *dest = NULL;
	struct btrfs_ioctl_vol_args *vol_args;
	struct btrfs_trans_handle *trans;
	int namelen;
	int ret;
	int err = 0;

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

	err = mnt_want_write(file->f_path.mnt);
	if (err)
		goto out;

	mutex_lock_nested(&dir->i_mutex, I_MUTEX_PARENT);
	dentry = lookup_one_len(vol_args->name, parent, namelen);
	if (IS_ERR(dentry)) {
		err = PTR_ERR(dentry);
		goto out_unlock_dir;
	}

	if (!dentry->d_inode) {
		err = -ENOENT;
		goto out_dput;
	}

	inode = dentry->d_inode;
	dest = BTRFS_I(inode)->root;
	if (!capable(CAP_SYS_ADMIN)){
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
		if (!btrfs_test_opt(root, USER_SUBVOL_RM_ALLOWED))
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

		/* check if subvolume may be deleted by a non-root user */
		err = btrfs_may_delete(dir, dentry, 1);
		if (err)
			goto out_dput;
	}

	if (inode->i_ino != BTRFS_FIRST_FREE_OBJECTID) {
		err = -EINVAL;
		goto out_dput;
	}

	mutex_lock(&inode->i_mutex);
	err = d_invalidate(dentry);
	if (err)
		goto out_unlock;

	down_write(&root->fs_info->subvol_sem);

	err = may_destroy_subvol(dest);
	if (err)
		goto out_up_write;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans)) {
		err = PTR_ERR(trans);
		goto out_up_write;
	}
	trans->block_rsv = &root->fs_info->global_block_rsv;

	ret = btrfs_unlink_subvol(trans, root, dir,
				dest->root_key.objectid,
				dentry->d_name.name,
				dentry->d_name.len);
	BUG_ON(ret);

	btrfs_record_root_in_trans(trans, dest);

	memset(&dest->root_item.drop_progress, 0,
		sizeof(dest->root_item.drop_progress));
	dest->root_item.drop_level = 0;
	btrfs_set_root_refs(&dest->root_item, 0);

	if (!xchg(&dest->orphan_item_inserted, 1)) {
		ret = btrfs_insert_orphan_item(trans,
					root->fs_info->tree_root,
					dest->root_key.objectid);
		BUG_ON(ret);
	}

	ret = btrfs_end_transaction(trans, root);
	BUG_ON(ret);
	inode->i_flags |= S_DEAD;
out_up_write:
	up_write(&root->fs_info->subvol_sem);
out_unlock:
	mutex_unlock(&inode->i_mutex);
	if (!err) {
		shrink_dcache_sb(root->fs_info->sb);
		btrfs_invalidate_inodes(dest);
		d_delete(dentry);
	}
out_dput:
	dput(dentry);
out_unlock_dir:
	mutex_unlock(&dir->i_mutex);
	mnt_drop_write(file->f_path.mnt);
out:
	kfree(vol_args);
	return err;
}

static int btrfs_ioctl_defrag(struct file *file, void __user *argp)
{
	struct inode *inode = fdentry(file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_ioctl_defrag_range_args *range;
	int ret;

	if (btrfs_root_readonly(root))
		return -EROFS;

	ret = mnt_want_write(file->f_path.mnt);
	if (ret)
		return ret;

	switch (inode->i_mode & S_IFMT) {
	case S_IFDIR:
		if (!capable(CAP_SYS_ADMIN)) {
			ret = -EPERM;
			goto out;
		}
		ret = btrfs_defrag_root(root, 0);
		if (ret)
			goto out;
		ret = btrfs_defrag_root(root->fs_info->extent_root, 0);
		break;
	case S_IFREG:
		if (!(file->f_mode & FMODE_WRITE)) {
			ret = -EINVAL;
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
		ret = btrfs_defrag_file(file, range);
		kfree(range);
		break;
	default:
		ret = -EINVAL;
	}
out:
	mnt_drop_write(file->f_path.mnt);
	return ret;
}

static long btrfs_ioctl_add_dev(struct btrfs_root *root, void __user *arg)
{
	struct btrfs_ioctl_vol_args *vol_args;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args))
		return PTR_ERR(vol_args);

	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';
	ret = btrfs_init_new_device(root, vol_args->name);

	kfree(vol_args);
	return ret;
}

static long btrfs_ioctl_rm_dev(struct btrfs_root *root, void __user *arg)
{
	struct btrfs_ioctl_vol_args *vol_args;
	int ret;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (root->fs_info->sb->s_flags & MS_RDONLY)
		return -EROFS;

	vol_args = memdup_user(arg, sizeof(*vol_args));
	if (IS_ERR(vol_args))
		return PTR_ERR(vol_args);

	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';
	ret = btrfs_rm_device(root, vol_args->name);

	kfree(vol_args);
	return ret;
}

static noinline long btrfs_ioctl_clone(struct file *file, unsigned long srcfd,
				       u64 off, u64 olen, u64 destoff)
{
	struct inode *inode = fdentry(file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct file *src_file;
	struct inode *src;
	struct btrfs_trans_handle *trans;
	struct btrfs_path *path;
	struct extent_buffer *leaf;
	char *buf;
	struct btrfs_key key;
	u32 nritems;
	int slot;
	int ret;
	u64 len = olen;
	u64 bs = root->fs_info->sb->s_blocksize;
	u64 hint_byte;

	/*
	 * TODO:
	 * - split compressed inline extents.  annoying: we need to
	 *   decompress into destination's address_space (the file offset
	 *   may change, so source mapping won't do), then recompress (or
	 *   otherwise reinsert) a subrange.
	 * - allow ranges within the same file to be cloned (provided
	 *   they don't overlap)?
	 */

	/* the destination must be opened for writing */
	if (!(file->f_mode & FMODE_WRITE) || (file->f_flags & O_APPEND))
		return -EINVAL;

	if (btrfs_root_readonly(root))
		return -EROFS;

	ret = mnt_want_write(file->f_path.mnt);
	if (ret)
		return ret;

	src_file = fget(srcfd);
	if (!src_file) {
		ret = -EBADF;
		goto out_drop_write;
	}

	src = src_file->f_dentry->d_inode;

	ret = -EINVAL;
	if (src == inode)
		goto out_fput;

	/* the src must be open for reading */
	if (!(src_file->f_mode & FMODE_READ))
		goto out_fput;

	ret = -EISDIR;
	if (S_ISDIR(src->i_mode) || S_ISDIR(inode->i_mode))
		goto out_fput;

	ret = -EXDEV;
	if (src->i_sb != inode->i_sb || BTRFS_I(src)->root != root)
		goto out_fput;

	ret = -ENOMEM;
	buf = vmalloc(btrfs_level_size(root, 0));
	if (!buf)
		goto out_fput;

	path = btrfs_alloc_path();
	if (!path) {
		vfree(buf);
		goto out_fput;
	}
	path->reada = 2;

	if (inode < src) {
		mutex_lock_nested(&inode->i_mutex, I_MUTEX_PARENT);
		mutex_lock_nested(&src->i_mutex, I_MUTEX_CHILD);
	} else {
		mutex_lock_nested(&src->i_mutex, I_MUTEX_PARENT);
		mutex_lock_nested(&inode->i_mutex, I_MUTEX_CHILD);
	}

	/* determine range to clone */
	ret = -EINVAL;
	if (off + len > src->i_size || off + len < off)
		goto out_unlock;
	if (len == 0)
		olen = len = src->i_size - off;
	/* if we extend to eof, continue to block boundary */
	if (off + len == src->i_size)
		len = ALIGN(src->i_size, bs) - off;

	/* verify the end result is block aligned */
	if (!IS_ALIGNED(off, bs) || !IS_ALIGNED(off + len, bs) ||
	    !IS_ALIGNED(destoff, bs))
		goto out_unlock;

	/* do any pending delalloc/csum calc on src, one way or
	   another, and lock file content */
	while (1) {
		struct btrfs_ordered_extent *ordered;
		lock_extent(&BTRFS_I(src)->io_tree, off, off+len, GFP_NOFS);
		ordered = btrfs_lookup_first_ordered_extent(src, off+len);
		if (!ordered &&
		    !test_range_bit(&BTRFS_I(src)->io_tree, off, off+len,
				   EXTENT_DELALLOC, 0, NULL))
			break;
		unlock_extent(&BTRFS_I(src)->io_tree, off, off+len, GFP_NOFS);
		if (ordered)
			btrfs_put_ordered_extent(ordered);
		btrfs_wait_ordered_range(src, off, len);
	}

	/* clone data */
	key.objectid = src->i_ino;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = 0;

	while (1) {
		/*
		 * note the key will change type as we walk through the
		 * tree.
		 */
		ret = btrfs_search_slot(NULL, root, &key, path, 0, 0);
		if (ret < 0)
			goto out;

		nritems = btrfs_header_nritems(path->nodes[0]);
		if (path->slots[0] >= nritems) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			if (ret > 0)
				break;
			nritems = btrfs_header_nritems(path->nodes[0]);
		}
		leaf = path->nodes[0];
		slot = path->slots[0];

		btrfs_item_key_to_cpu(leaf, &key, slot);
		if (btrfs_key_type(&key) > BTRFS_EXTENT_DATA_KEY ||
		    key.objectid != src->i_ino)
			break;

		if (btrfs_key_type(&key) == BTRFS_EXTENT_DATA_KEY) {
			struct btrfs_file_extent_item *extent;
			int type;
			u32 size;
			struct btrfs_key new_key;
			u64 disko = 0, diskl = 0;
			u64 datao = 0, datal = 0;
			u8 comp;
			u64 endoff;

			size = btrfs_item_size_nr(leaf, slot);
			read_extent_buffer(leaf, buf,
					   btrfs_item_ptr_offset(leaf, slot),
					   size);

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
			btrfs_release_path(root, path);

			if (key.offset + datal <= off ||
			    key.offset >= off+len)
				goto next;

			memcpy(&new_key, &key, sizeof(new_key));
			new_key.objectid = inode->i_ino;
			if (off <= key.offset)
				new_key.offset = key.offset + destoff - off;
			else
				new_key.offset = destoff;

			trans = btrfs_start_transaction(root, 1);
			if (IS_ERR(trans)) {
				ret = PTR_ERR(trans);
				goto out;
			}

			if (type == BTRFS_FILE_EXTENT_REG ||
			    type == BTRFS_FILE_EXTENT_PREALLOC) {
				if (off > key.offset) {
					datao += off - key.offset;
					datal -= off - key.offset;
				}

				if (key.offset + datal > off + len)
					datal = off + len - key.offset;

				ret = btrfs_drop_extents(trans, inode,
							 new_key.offset,
							 new_key.offset + datal,
							 &hint_byte, 1);
				BUG_ON(ret);

				ret = btrfs_insert_empty_item(trans, root, path,
							      &new_key, size);
				BUG_ON(ret);

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
					inode_add_bytes(inode, datal);
					ret = btrfs_inc_extent_ref(trans, root,
							disko, diskl, 0,
							root->root_key.objectid,
							inode->i_ino,
							new_key.offset - datao);
					BUG_ON(ret);
				}
			} else if (type == BTRFS_FILE_EXTENT_INLINE) {
				u64 skip = 0;
				u64 trim = 0;
				if (off > key.offset) {
					skip = off - key.offset;
					new_key.offset += skip;
				}

				if (key.offset + datal > off+len)
					trim = key.offset + datal - (off+len);

				if (comp && (skip || trim)) {
					ret = -EINVAL;
					btrfs_end_transaction(trans, root);
					goto out;
				}
				size -= skip + trim;
				datal -= skip + trim;

				ret = btrfs_drop_extents(trans, inode,
							 new_key.offset,
							 new_key.offset + datal,
							 &hint_byte, 1);
				BUG_ON(ret);

				ret = btrfs_insert_empty_item(trans, root, path,
							      &new_key, size);
				BUG_ON(ret);

				if (skip) {
					u32 start =
					  btrfs_file_extent_calc_inline_size(0);
					memmove(buf+start, buf+start+skip,
						datal);
				}

				leaf = path->nodes[0];
				slot = path->slots[0];
				write_extent_buffer(leaf, buf,
					    btrfs_item_ptr_offset(leaf, slot),
					    size);
				inode_add_bytes(inode, datal);
			}

			btrfs_mark_buffer_dirty(leaf);
			btrfs_release_path(root, path);

			inode->i_mtime = inode->i_ctime = CURRENT_TIME;

			/*
			 * we round up to the block size at eof when
			 * determining which extents to clone above,
			 * but shouldn't round up the file size
			 */
			endoff = new_key.offset + datal;
			if (endoff > destoff+olen)
				endoff = destoff+olen;
			if (endoff > inode->i_size)
				btrfs_i_size_write(inode, endoff);

			BTRFS_I(inode)->flags = BTRFS_I(src)->flags;
			ret = btrfs_update_inode(trans, root, inode);
			BUG_ON(ret);
			btrfs_end_transaction(trans, root);
		}
next:
		btrfs_release_path(root, path);
		key.offset++;
	}
	ret = 0;
out:
	btrfs_release_path(root, path);
	unlock_extent(&BTRFS_I(src)->io_tree, off, off+len, GFP_NOFS);
out_unlock:
	mutex_unlock(&src->i_mutex);
	mutex_unlock(&inode->i_mutex);
	vfree(buf);
	btrfs_free_path(path);
out_fput:
	fput(src_file);
out_drop_write:
	mnt_drop_write(file->f_path.mnt);
	return ret;
}

static long btrfs_ioctl_clone_range(struct file *file, void __user *argp)
{
	struct btrfs_ioctl_clone_range_args args;

	if (copy_from_user(&args, argp, sizeof(args)))
		return -EFAULT;
	return btrfs_ioctl_clone(file, args.src_fd, args.src_offset,
				 args.src_length, args.dest_offset);
}

/*
 * there are many ways the trans_start and trans_end ioctls can lead
 * to deadlocks.  They should only be used by applications that
 * basically own the machine, and have a very in depth understanding
 * of all the possible deadlocks and enospc problems.
 */
static long btrfs_ioctl_trans_start(struct file *file)
{
	struct inode *inode = fdentry(file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;
	int ret;

	ret = -EPERM;
	if (!capable(CAP_SYS_ADMIN))
		goto out;

	ret = -EINPROGRESS;
	if (file->private_data)
		goto out;

	ret = -EROFS;
	if (btrfs_root_readonly(root))
		goto out;

	ret = mnt_want_write(file->f_path.mnt);
	if (ret)
		goto out;

	mutex_lock(&root->fs_info->trans_mutex);
	root->fs_info->open_ioctl_trans++;
	mutex_unlock(&root->fs_info->trans_mutex);

	ret = -ENOMEM;
	trans = btrfs_start_ioctl_transaction(root, 0);
	if (IS_ERR(trans))
		goto out_drop;

	file->private_data = trans;
	return 0;

out_drop:
	mutex_lock(&root->fs_info->trans_mutex);
	root->fs_info->open_ioctl_trans--;
	mutex_unlock(&root->fs_info->trans_mutex);
	mnt_drop_write(file->f_path.mnt);
out:
	return ret;
}

static long btrfs_ioctl_default_subvol(struct file *file, void __user *argp)
{
	struct inode *inode = fdentry(file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_root *new_root;
	struct btrfs_dir_item *di;
	struct btrfs_trans_handle *trans;
	struct btrfs_path *path;
	struct btrfs_key location;
	struct btrfs_disk_key disk_key;
	struct btrfs_super_block *disk_super;
	u64 features;
	u64 objectid = 0;
	u64 dir_id;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (copy_from_user(&objectid, argp, sizeof(objectid)))
		return -EFAULT;

	if (!objectid)
		objectid = root->root_key.objectid;

	location.objectid = objectid;
	location.type = BTRFS_ROOT_ITEM_KEY;
	location.offset = (u64)-1;

	new_root = btrfs_read_fs_root_no_name(root->fs_info, &location);
	if (IS_ERR(new_root))
		return PTR_ERR(new_root);

	if (btrfs_root_refs(&new_root->root_item) == 0)
		return -ENOENT;

	path = btrfs_alloc_path();
	if (!path)
		return -ENOMEM;
	path->leave_spinning = 1;

	trans = btrfs_start_transaction(root, 1);
	if (IS_ERR(trans)) {
		btrfs_free_path(path);
		return PTR_ERR(trans);
	}

	dir_id = btrfs_super_root_dir(&root->fs_info->super_copy);
	di = btrfs_lookup_dir_item(trans, root->fs_info->tree_root, path,
				   dir_id, "default", 7, 1);
	if (IS_ERR_OR_NULL(di)) {
		btrfs_free_path(path);
		btrfs_end_transaction(trans, root);
		printk(KERN_ERR "Umm, you don't have the default dir item, "
		       "this isn't going to work\n");
		return -ENOENT;
	}

	btrfs_cpu_key_to_disk(&disk_key, &new_root->root_key);
	btrfs_set_dir_item_key(path->nodes[0], di, &disk_key);
	btrfs_mark_buffer_dirty(path->nodes[0]);
	btrfs_free_path(path);

	disk_super = &root->fs_info->super_copy;
	features = btrfs_super_incompat_flags(disk_super);
	if (!(features & BTRFS_FEATURE_INCOMPAT_DEFAULT_SUBVOL)) {
		features |= BTRFS_FEATURE_INCOMPAT_DEFAULT_SUBVOL;
		btrfs_set_super_incompat_flags(disk_super, features);
	}
	btrfs_end_transaction(trans, root);

	return 0;
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

long btrfs_ioctl_space_info(struct btrfs_root *root, void __user *arg)
{
	struct btrfs_ioctl_space_args space_args;
	struct btrfs_ioctl_space_info space;
	struct btrfs_ioctl_space_info *dest;
	struct btrfs_ioctl_space_info *dest_orig;
	struct btrfs_ioctl_space_info __user *user_dest;
	struct btrfs_space_info *info;
	u64 types[] = {BTRFS_BLOCK_GROUP_DATA,
		       BTRFS_BLOCK_GROUP_SYSTEM,
		       BTRFS_BLOCK_GROUP_METADATA,
		       BTRFS_BLOCK_GROUP_DATA | BTRFS_BLOCK_GROUP_METADATA};
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
		list_for_each_entry_rcu(tmp, &root->fs_info->space_info,
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
	if (alloc_size > PAGE_CACHE_SIZE)
		return -ENOMEM;

	space_args.total_spaces = 0;
	dest = kmalloc(alloc_size, GFP_NOFS);
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
		list_for_each_entry_rcu(tmp, &root->fs_info->space_info,
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

	user_dest = (struct btrfs_ioctl_space_info *)
		(arg + sizeof(struct btrfs_ioctl_space_args));

	if (copy_to_user(user_dest, dest_orig, alloc_size))
		ret = -EFAULT;

	kfree(dest_orig);
out:
	if (ret == 0 && copy_to_user(arg, &space_args, sizeof(space_args)))
		ret = -EFAULT;

	return ret;
}

/*
 * there are many ways the trans_start and trans_end ioctls can lead
 * to deadlocks.  They should only be used by applications that
 * basically own the machine, and have a very in depth understanding
 * of all the possible deadlocks and enospc problems.
 */
long btrfs_ioctl_trans_end(struct file *file)
{
	struct inode *inode = fdentry(file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct btrfs_trans_handle *trans;

	trans = file->private_data;
	if (!trans)
		return -EINVAL;
	file->private_data = NULL;

	btrfs_end_transaction(trans, root);

	mutex_lock(&root->fs_info->trans_mutex);
	root->fs_info->open_ioctl_trans--;
	mutex_unlock(&root->fs_info->trans_mutex);

	mnt_drop_write(file->f_path.mnt);
	return 0;
}

static noinline long btrfs_ioctl_start_sync(struct file *file, void __user *argp)
{
	struct btrfs_root *root = BTRFS_I(file->f_dentry->d_inode)->root;
	struct btrfs_trans_handle *trans;
	u64 transid;
	int ret;

	trans = btrfs_start_transaction(root, 0);
	if (IS_ERR(trans))
		return PTR_ERR(trans);
	transid = trans->transid;
	ret = btrfs_commit_transaction_async(trans, root, 0);
	if (ret) {
		btrfs_end_transaction(trans, root);
		return ret;
	}

	if (argp)
		if (copy_to_user(argp, &transid, sizeof(transid)))
			return -EFAULT;
	return 0;
}

static noinline long btrfs_ioctl_wait_sync(struct file *file, void __user *argp)
{
	struct btrfs_root *root = BTRFS_I(file->f_dentry->d_inode)->root;
	u64 transid;

	if (argp) {
		if (copy_from_user(&transid, argp, sizeof(transid)))
			return -EFAULT;
	} else {
		transid = 0;  /* current trans */
	}
	return btrfs_wait_for_commit(root, transid);
}

long btrfs_ioctl(struct file *file, unsigned int
		cmd, unsigned long arg)
{
	struct btrfs_root *root = BTRFS_I(fdentry(file)->d_inode)->root;
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
		return btrfs_ioctl_resize(root, argp);
	case BTRFS_IOC_ADD_DEV:
		return btrfs_ioctl_add_dev(root, argp);
	case BTRFS_IOC_RM_DEV:
		return btrfs_ioctl_rm_dev(root, argp);
	case BTRFS_IOC_BALANCE:
		return btrfs_balance(root->fs_info->dev_root);
	case BTRFS_IOC_CLONE:
		return btrfs_ioctl_clone(file, arg, 0, 0, 0);
	case BTRFS_IOC_CLONE_RANGE:
		return btrfs_ioctl_clone_range(file, argp);
	case BTRFS_IOC_TRANS_START:
		return btrfs_ioctl_trans_start(file);
	case BTRFS_IOC_TRANS_END:
		return btrfs_ioctl_trans_end(file);
	case BTRFS_IOC_TREE_SEARCH:
		return btrfs_ioctl_tree_search(file, argp);
	case BTRFS_IOC_INO_LOOKUP:
		return btrfs_ioctl_ino_lookup(file, argp);
	case BTRFS_IOC_SPACE_INFO:
		return btrfs_ioctl_space_info(root, argp);
	case BTRFS_IOC_SYNC:
		btrfs_sync_fs(file->f_dentry->d_sb, 1);
		return 0;
	case BTRFS_IOC_START_SYNC:
		return btrfs_ioctl_start_sync(file, argp);
	case BTRFS_IOC_WAIT_SYNC:
		return btrfs_ioctl_wait_sync(file, argp);
	}

	return -ENOTTY;
}
