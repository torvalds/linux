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
#include <linux/smp_lock.h>
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
#include <linux/version.h>
#include <linux/xattr.h>
#include <linux/vmalloc.h>
#include "compat.h"
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "ioctl.h"
#include "print-tree.h"
#include "volumes.h"
#include "locking.h"



static noinline int create_subvol(struct btrfs_root *root,
				  struct dentry *dentry,
				  char *name, int namelen)
{
	struct btrfs_trans_handle *trans;
	struct btrfs_key key;
	struct btrfs_root_item root_item;
	struct btrfs_inode_item *inode_item;
	struct extent_buffer *leaf;
	struct btrfs_root *new_root = root;
	struct inode *dir;
	int ret;
	int err;
	u64 objectid;
	u64 new_dirid = BTRFS_FIRST_FREE_OBJECTID;
	u64 index = 0;
	unsigned long nr = 1;

	ret = btrfs_check_free_space(root, 1, 0);
	if (ret)
		goto fail_commit;

	trans = btrfs_start_transaction(root, 1);
	BUG_ON(!trans);

	ret = btrfs_find_free_objectid(trans, root->fs_info->tree_root,
				       0, &objectid);
	if (ret)
		goto fail;

	leaf = btrfs_alloc_free_block(trans, root, root->leafsize, 0,
				      objectid, trans->transid, 0, 0, 0);
	if (IS_ERR(leaf)) {
		ret = PTR_ERR(leaf);
		goto fail;
	}

	btrfs_set_header_nritems(leaf, 0);
	btrfs_set_header_level(leaf, 0);
	btrfs_set_header_bytenr(leaf, leaf->start);
	btrfs_set_header_generation(leaf, trans->transid);
	btrfs_set_header_owner(leaf, objectid);

	write_extent_buffer(leaf, root->fs_info->fsid,
			    (unsigned long)btrfs_header_fsid(leaf),
			    BTRFS_FSID_SIZE);
	btrfs_mark_buffer_dirty(leaf);

	inode_item = &root_item.inode;
	memset(inode_item, 0, sizeof(*inode_item));
	inode_item->generation = cpu_to_le64(1);
	inode_item->size = cpu_to_le64(3);
	inode_item->nlink = cpu_to_le32(1);
	inode_item->nbytes = cpu_to_le64(root->leafsize);
	inode_item->mode = cpu_to_le32(S_IFDIR | 0755);

	btrfs_set_root_bytenr(&root_item, leaf->start);
	btrfs_set_root_generation(&root_item, trans->transid);
	btrfs_set_root_level(&root_item, 0);
	btrfs_set_root_refs(&root_item, 1);
	btrfs_set_root_used(&root_item, 0);
	btrfs_set_root_last_snapshot(&root_item, 0);

	memset(&root_item.drop_progress, 0, sizeof(root_item.drop_progress));
	root_item.drop_level = 0;

	btrfs_tree_unlock(leaf);
	free_extent_buffer(leaf);
	leaf = NULL;

	btrfs_set_root_dirid(&root_item, new_dirid);

	key.objectid = objectid;
	key.offset = 1;
	btrfs_set_key_type(&key, BTRFS_ROOT_ITEM_KEY);
	ret = btrfs_insert_root(trans, root->fs_info->tree_root, &key,
				&root_item);
	if (ret)
		goto fail;

	/*
	 * insert the directory item
	 */
	key.offset = (u64)-1;
	dir = dentry->d_parent->d_inode;
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

	/* add the backref first */
	ret = btrfs_add_root_ref(trans, root->fs_info->tree_root,
				 objectid, BTRFS_ROOT_BACKREF_KEY,
				 root->root_key.objectid,
				 dir->i_ino, index, name, namelen);

	BUG_ON(ret);

	/* now add the forward ref */
	ret = btrfs_add_root_ref(trans, root->fs_info->tree_root,
				 root->root_key.objectid, BTRFS_ROOT_REF_KEY,
				 objectid,
				 dir->i_ino, index, name, namelen);

	BUG_ON(ret);

	ret = btrfs_commit_transaction(trans, root);
	if (ret)
		goto fail_commit;

	new_root = btrfs_read_fs_root_no_name(root->fs_info, &key);
	BUG_ON(!new_root);

	trans = btrfs_start_transaction(new_root, 1);
	BUG_ON(!trans);

	ret = btrfs_create_subvol_root(trans, new_root, dentry, new_dirid,
				       BTRFS_I(dir)->block_group);
	if (ret)
		goto fail;

fail:
	nr = trans->blocks_used;
	err = btrfs_commit_transaction(trans, new_root);
	if (err && !ret)
		ret = err;
fail_commit:
	btrfs_btree_balance_dirty(root, nr);
	return ret;
}

static int create_snapshot(struct btrfs_root *root, struct dentry *dentry,
			   char *name, int namelen)
{
	struct btrfs_pending_snapshot *pending_snapshot;
	struct btrfs_trans_handle *trans;
	int ret = 0;
	int err;
	unsigned long nr = 0;

	if (!root->ref_cows)
		return -EINVAL;

	ret = btrfs_check_free_space(root, 1, 0);
	if (ret)
		goto fail_unlock;

	pending_snapshot = kzalloc(sizeof(*pending_snapshot), GFP_NOFS);
	if (!pending_snapshot) {
		ret = -ENOMEM;
		goto fail_unlock;
	}
	pending_snapshot->name = kmalloc(namelen + 1, GFP_NOFS);
	if (!pending_snapshot->name) {
		ret = -ENOMEM;
		kfree(pending_snapshot);
		goto fail_unlock;
	}
	memcpy(pending_snapshot->name, name, namelen);
	pending_snapshot->name[namelen] = '\0';
	pending_snapshot->dentry = dentry;
	trans = btrfs_start_transaction(root, 1);
	BUG_ON(!trans);
	pending_snapshot->root = root;
	list_add(&pending_snapshot->list,
		 &trans->transaction->pending_snapshots);
	err = btrfs_commit_transaction(trans, root);

fail_unlock:
	btrfs_btree_balance_dirty(root, nr);
	return ret;
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
static noinline int btrfs_mksubvol(struct path *parent, char *name,
				   int mode, int namelen,
				   struct btrfs_root *snap_src)
{
	struct dentry *dentry;
	int error;

	mutex_lock_nested(&parent->dentry->d_inode->i_mutex, I_MUTEX_PARENT);

	dentry = lookup_one_len(name, parent->dentry, namelen);
	error = PTR_ERR(dentry);
	if (IS_ERR(dentry))
		goto out_unlock;

	error = -EEXIST;
	if (dentry->d_inode)
		goto out_dput;

	if (!IS_POSIXACL(parent->dentry->d_inode))
		mode &= ~current->fs->umask;

	error = mnt_want_write(parent->mnt);
	if (error)
		goto out_dput;

	error = btrfs_may_create(parent->dentry->d_inode, dentry);
	if (error)
		goto out_drop_write;

	/*
	 * Actually perform the low-level subvolume creation after all
	 * this VFS fuzz.
	 *
	 * Eventually we want to pass in an inode under which we create this
	 * subvolume, but for now all are under the filesystem root.
	 *
	 * Also we should pass on the mode eventually to allow creating new
	 * subvolume with specific mode bits.
	 */
	if (snap_src) {
		struct dentry *dir = dentry->d_parent;
		struct dentry *test = dir->d_parent;
		struct btrfs_path *path = btrfs_alloc_path();
		int ret;
		u64 test_oid;
		u64 parent_oid = BTRFS_I(dir->d_inode)->root->root_key.objectid;

		test_oid = snap_src->root_key.objectid;

		ret = btrfs_find_root_ref(snap_src->fs_info->tree_root,
					  path, parent_oid, test_oid);
		if (ret == 0)
			goto create;
		btrfs_release_path(snap_src->fs_info->tree_root, path);

		/* we need to make sure we aren't creating a directory loop
		 * by taking a snapshot of something that has our current
		 * subvol in its directory tree.  So, this loops through
		 * the dentries and checks the forward refs for each subvolume
		 * to see if is references the subvolume where we are
		 * placing this new snapshot.
		 */
		while (1) {
			if (!test ||
			    dir == snap_src->fs_info->sb->s_root ||
			    test == snap_src->fs_info->sb->s_root ||
			    test->d_inode->i_sb != snap_src->fs_info->sb) {
				break;
			}
			if (S_ISLNK(test->d_inode->i_mode)) {
				printk(KERN_INFO "Btrfs symlink in snapshot "
				       "path, failed\n");
				error = -EMLINK;
				btrfs_free_path(path);
				goto out_drop_write;
			}
			test_oid =
				BTRFS_I(test->d_inode)->root->root_key.objectid;
			ret = btrfs_find_root_ref(snap_src->fs_info->tree_root,
				  path, test_oid, parent_oid);
			if (ret == 0) {
				printk(KERN_INFO "Btrfs snapshot creation "
				       "failed, looping\n");
				error = -EMLINK;
				btrfs_free_path(path);
				goto out_drop_write;
			}
			btrfs_release_path(snap_src->fs_info->tree_root, path);
			test = test->d_parent;
		}
create:
		btrfs_free_path(path);
		error = create_snapshot(snap_src, dentry, name, namelen);
	} else {
		error = create_subvol(BTRFS_I(parent->dentry->d_inode)->root,
				      dentry, name, namelen);
	}
	if (error)
		goto out_drop_write;

	fsnotify_mkdir(parent->dentry->d_inode, dentry);
out_drop_write:
	mnt_drop_write(parent->mnt);
out_dput:
	dput(dentry);
out_unlock:
	mutex_unlock(&parent->dentry->d_inode->i_mutex);
	return error;
}


static int btrfs_defrag_file(struct file *file)
{
	struct inode *inode = fdentry(file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct extent_io_tree *io_tree = &BTRFS_I(inode)->io_tree;
	struct btrfs_ordered_extent *ordered;
	struct page *page;
	unsigned long last_index;
	unsigned long ra_pages = root->fs_info->bdi.ra_pages;
	unsigned long total_read = 0;
	u64 page_start;
	u64 page_end;
	unsigned long i;
	int ret;

	ret = btrfs_check_free_space(root, inode->i_size, 0);
	if (ret)
		return -ENOSPC;

	mutex_lock(&inode->i_mutex);
	last_index = inode->i_size >> PAGE_CACHE_SHIFT;
	for (i = 0; i <= last_index; i++) {
		if (total_read % ra_pages == 0) {
			btrfs_force_ra(inode->i_mapping, &file->f_ra, file, i,
				       min(last_index, i + ra_pages - 1));
		}
		total_read++;
again:
		page = grab_cache_page(inode->i_mapping, i);
		if (!page)
			goto out_unlock;
		if (!PageUptodate(page)) {
			btrfs_readpage(NULL, page);
			lock_page(page);
			if (!PageUptodate(page)) {
				unlock_page(page);
				page_cache_release(page);
				goto out_unlock;
			}
		}

		wait_on_page_writeback(page);

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

		btrfs_set_extent_delalloc(inode, page_start, page_end);

		unlock_extent(io_tree, page_start, page_end, GFP_NOFS);
		set_page_dirty(page);
		unlock_page(page);
		page_cache_release(page);
		balance_dirty_pages_ratelimited_nr(inode->i_mapping, 1);
	}

out_unlock:
	mutex_unlock(&inode->i_mutex);
	return 0;
}

/*
 * Called inside transaction, so use GFP_NOFS
 */

static int btrfs_ioctl_resize(struct btrfs_root *root, void __user *arg)
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
	int namelen;
	int mod = 0;

	if (root->fs_info->sb->s_flags & MS_RDONLY)
		return -EROFS;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	vol_args = kmalloc(sizeof(*vol_args), GFP_NOFS);

	if (!vol_args)
		return -ENOMEM;

	if (copy_from_user(vol_args, arg, sizeof(*vol_args))) {
		ret = -EFAULT;
		goto out;
	}

	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';
	namelen = strlen(vol_args->name);

	mutex_lock(&root->fs_info->volume_mutex);
	sizestr = vol_args->name;
	devstr = strchr(sizestr, ':');
	if (devstr) {
		char *end;
		sizestr = devstr + 1;
		*devstr = '\0';
		devstr = vol_args->name;
		devid = simple_strtoull(devstr, &end, 10);
		printk(KERN_INFO "resizing devid %llu\n", devid);
	}
	device = btrfs_find_device(root, devid, NULL, NULL);
	if (!device) {
		printk(KERN_INFO "resizer unable to find device %llu\n", devid);
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
		new_size = btrfs_parse_size(sizestr);
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
		trans = btrfs_start_transaction(root, 1);
		ret = btrfs_grow_device(trans, device, new_size);
		btrfs_commit_transaction(trans, root);
	} else {
		ret = btrfs_shrink_device(device, new_size);
	}

out_unlock:
	mutex_unlock(&root->fs_info->volume_mutex);
out:
	kfree(vol_args);
	return ret;
}

static noinline int btrfs_ioctl_snap_create(struct file *file,
					    void __user *arg, int subvol)
{
	struct btrfs_root *root = BTRFS_I(fdentry(file)->d_inode)->root;
	struct btrfs_ioctl_vol_args *vol_args;
	struct btrfs_dir_item *di;
	struct btrfs_path *path;
	struct file *src_file;
	u64 root_dirid;
	int namelen;
	int ret = 0;

	if (root->fs_info->sb->s_flags & MS_RDONLY)
		return -EROFS;

	vol_args = kmalloc(sizeof(*vol_args), GFP_NOFS);

	if (!vol_args)
		return -ENOMEM;

	if (copy_from_user(vol_args, arg, sizeof(*vol_args))) {
		ret = -EFAULT;
		goto out;
	}

	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';
	namelen = strlen(vol_args->name);
	if (strchr(vol_args->name, '/')) {
		ret = -EINVAL;
		goto out;
	}

	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}

	root_dirid = root->fs_info->sb->s_root->d_inode->i_ino,
	di = btrfs_lookup_dir_item(NULL, root->fs_info->tree_root,
			    path, root_dirid,
			    vol_args->name, namelen, 0);
	btrfs_free_path(path);

	if (di && !IS_ERR(di)) {
		ret = -EEXIST;
		goto out;
	}

	if (IS_ERR(di)) {
		ret = PTR_ERR(di);
		goto out;
	}

	if (subvol) {
		ret = btrfs_mksubvol(&file->f_path, vol_args->name,
				     file->f_path.dentry->d_inode->i_mode,
				     namelen, NULL);
	} else {
		struct inode *src_inode;
		src_file = fget(vol_args->fd);
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
		ret = btrfs_mksubvol(&file->f_path, vol_args->name,
			     file->f_path.dentry->d_inode->i_mode,
			     namelen, BTRFS_I(src_inode)->root);
		fput(src_file);
	}

out:
	kfree(vol_args);
	return ret;
}

static int btrfs_ioctl_defrag(struct file *file)
{
	struct inode *inode = fdentry(file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	int ret;

	ret = mnt_want_write(file->f_path.mnt);
	if (ret)
		return ret;

	switch (inode->i_mode & S_IFMT) {
	case S_IFDIR:
		if (!capable(CAP_SYS_ADMIN)) {
			ret = -EPERM;
			goto out;
		}
		btrfs_defrag_root(root, 0);
		btrfs_defrag_root(root->fs_info->extent_root, 0);
		break;
	case S_IFREG:
		if (!(file->f_mode & FMODE_WRITE)) {
			ret = -EINVAL;
			goto out;
		}
		btrfs_defrag_file(file);
		break;
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

	vol_args = kmalloc(sizeof(*vol_args), GFP_NOFS);

	if (!vol_args)
		return -ENOMEM;

	if (copy_from_user(vol_args, arg, sizeof(*vol_args))) {
		ret = -EFAULT;
		goto out;
	}
	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';
	ret = btrfs_init_new_device(root, vol_args->name);

out:
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

	vol_args = kmalloc(sizeof(*vol_args), GFP_NOFS);

	if (!vol_args)
		return -ENOMEM;

	if (copy_from_user(vol_args, arg, sizeof(*vol_args))) {
		ret = -EFAULT;
		goto out;
	}
	vol_args->name[BTRFS_PATH_NAME_MAX] = '\0';
	ret = btrfs_rm_device(root, vol_args->name);

out:
	kfree(vol_args);
	return ret;
}

static long btrfs_ioctl_clone(struct file *file, unsigned long srcfd,
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
	if (!(file->f_mode & FMODE_WRITE))
		return -EINVAL;

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
		mutex_lock(&inode->i_mutex);
		mutex_lock(&src->i_mutex);
	} else {
		mutex_lock(&src->i_mutex);
		mutex_lock(&inode->i_mutex);
	}

	/* determine range to clone */
	ret = -EINVAL;
	if (off >= src->i_size || off + len > src->i_size)
		goto out_unlock;
	if (len == 0)
		olen = len = src->i_size - off;
	/* if we extend to eof, continue to block boundary */
	if (off + len == src->i_size)
		len = ((src->i_size + bs-1) & ~(bs-1))
			- off;

	/* verify the end result is block aligned */
	if ((off & (bs-1)) ||
	    ((off + len) & (bs-1)))
		goto out_unlock;

	/* do any pending delalloc/csum calc on src, one way or
	   another, and lock file content */
	while (1) {
		struct btrfs_ordered_extent *ordered;
		lock_extent(&BTRFS_I(src)->io_tree, off, off+len, GFP_NOFS);
		ordered = btrfs_lookup_first_ordered_extent(inode, off+len);
		if (BTRFS_I(src)->delalloc_bytes == 0 && !ordered)
			break;
		unlock_extent(&BTRFS_I(src)->io_tree, off, off+len, GFP_NOFS);
		if (ordered)
			btrfs_put_ordered_extent(ordered);
		btrfs_wait_ordered_range(src, off, off+len);
	}

	trans = btrfs_start_transaction(root, 1);
	BUG_ON(!trans);

	/* punch hole in destination first */
	btrfs_drop_extents(trans, root, inode, off, off+len, 0, &hint_byte);

	/* clone data */
	key.objectid = src->i_ino;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.offset = 0;

	while (1) {
		/*
		 * note the key will change type as we walk through the
		 * tree.
		 */
		ret = btrfs_search_slot(trans, root, &key, path, 0, 0);
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

			size = btrfs_item_size_nr(leaf, slot);
			read_extent_buffer(leaf, buf,
					   btrfs_item_ptr_offset(leaf, slot),
					   size);

			extent = btrfs_item_ptr(leaf, slot,
						struct btrfs_file_extent_item);
			comp = btrfs_file_extent_compression(leaf, extent);
			type = btrfs_file_extent_type(leaf, extent);
			if (type == BTRFS_FILE_EXTENT_REG) {
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

			if (key.offset + datal < off ||
			    key.offset >= off+len)
				goto next;

			memcpy(&new_key, &key, sizeof(new_key));
			new_key.objectid = inode->i_ino;
			new_key.offset = key.offset + destoff - off;

			if (type == BTRFS_FILE_EXTENT_REG) {
				ret = btrfs_insert_empty_item(trans, root, path,
							      &new_key, size);
				if (ret)
					goto out;

				leaf = path->nodes[0];
				slot = path->slots[0];
				write_extent_buffer(leaf, buf,
					    btrfs_item_ptr_offset(leaf, slot),
					    size);

				extent = btrfs_item_ptr(leaf, slot,
						struct btrfs_file_extent_item);

				if (off > key.offset) {
					datao += off - key.offset;
					datal -= off - key.offset;
				}
				if (key.offset + datao + datal + key.offset >
				    off + len)
					datal = off + len - key.offset - datao;
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
						   disko, diskl, leaf->start,
						   root->root_key.objectid,
						   trans->transid,
						   inode->i_ino);
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
					goto out;
				}
				size -= skip + trim;
				datal -= skip + trim;
				ret = btrfs_insert_empty_item(trans, root, path,
							      &new_key, size);
				if (ret)
					goto out;

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
		}

next:
		btrfs_release_path(root, path);
		key.offset++;
	}
	ret = 0;
out:
	btrfs_release_path(root, path);
	if (ret == 0) {
		inode->i_mtime = inode->i_ctime = CURRENT_TIME;
		if (destoff + olen > inode->i_size)
			btrfs_i_size_write(inode, destoff + olen);
		BTRFS_I(inode)->flags = BTRFS_I(src)->flags;
		ret = btrfs_update_inode(trans, root, inode);
	}
	btrfs_end_transaction(trans, root);
	unlock_extent(&BTRFS_I(src)->io_tree, off, off+len, GFP_NOFS);
	if (ret)
		vmtruncate(inode, 0);
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
	int ret = 0;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	if (file->private_data) {
		ret = -EINPROGRESS;
		goto out;
	}

	ret = mnt_want_write(file->f_path.mnt);
	if (ret)
		goto out;

	mutex_lock(&root->fs_info->trans_mutex);
	root->fs_info->open_ioctl_trans++;
	mutex_unlock(&root->fs_info->trans_mutex);

	trans = btrfs_start_ioctl_transaction(root, 0);
	if (trans)
		file->private_data = trans;
	else
		ret = -ENOMEM;
	/*printk(KERN_INFO "btrfs_ioctl_trans_start on %p\n", file);*/
out:
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
	int ret = 0;

	trans = file->private_data;
	if (!trans) {
		ret = -EINVAL;
		goto out;
	}
	btrfs_end_transaction(trans, root);
	file->private_data = NULL;

	mutex_lock(&root->fs_info->trans_mutex);
	root->fs_info->open_ioctl_trans--;
	mutex_unlock(&root->fs_info->trans_mutex);

	mnt_drop_write(file->f_path.mnt);

out:
	return ret;
}

long btrfs_ioctl(struct file *file, unsigned int
		cmd, unsigned long arg)
{
	struct btrfs_root *root = BTRFS_I(fdentry(file)->d_inode)->root;
	void __user *argp = (void __user *)arg;

	switch (cmd) {
	case BTRFS_IOC_SNAP_CREATE:
		return btrfs_ioctl_snap_create(file, argp, 0);
	case BTRFS_IOC_SUBVOL_CREATE:
		return btrfs_ioctl_snap_create(file, argp, 1);
	case BTRFS_IOC_DEFRAG:
		return btrfs_ioctl_defrag(file);
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
	case BTRFS_IOC_SYNC:
		btrfs_sync_fs(file->f_dentry->d_sb, 1);
		return 0;
	}

	return -ENOTTY;
}
