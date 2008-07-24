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
#include <linux/pagemap.h>
#include <linux/highmem.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/string.h>
#include <linux/smp_lock.h>
#include <linux/backing-dev.h>
#include <linux/mpage.h>
#include <linux/swap.h>
#include <linux/writeback.h>
#include <linux/statfs.h>
#include <linux/compat.h>
#include <linux/bit_spinlock.h>
#include <linux/version.h>
#include <linux/xattr.h>
#include "ctree.h"
#include "disk-io.h"
#include "transaction.h"
#include "btrfs_inode.h"
#include "ioctl.h"
#include "print-tree.h"
#include "volumes.h"
#include "locking.h"



static noinline int create_subvol(struct btrfs_root *root, char *name,
				  int namelen)
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

	leaf = btrfs_alloc_free_block(trans, root, root->leafsize,
				      objectid, trans->transid, 0, 0,
				      0, 0);
	if (IS_ERR(leaf))
		return PTR_ERR(leaf);

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
	inode_item->nblocks = cpu_to_le64(1);
	inode_item->mode = cpu_to_le32(S_IFDIR | 0755);

	btrfs_set_root_bytenr(&root_item, leaf->start);
	btrfs_set_root_level(&root_item, 0);
	btrfs_set_root_refs(&root_item, 1);
	btrfs_set_root_used(&root_item, 0);

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
	dir = root->fs_info->sb->s_root->d_inode;
	ret = btrfs_insert_dir_item(trans, root->fs_info->tree_root,
				    name, namelen, dir->i_ino, &key,
				    BTRFS_FT_DIR, 0);
	if (ret)
		goto fail;

	ret = btrfs_insert_inode_ref(trans, root->fs_info->tree_root,
			     name, namelen, objectid,
			     root->fs_info->sb->s_root->d_inode->i_ino, 0);
	if (ret)
		goto fail;

	ret = btrfs_commit_transaction(trans, root);
	if (ret)
		goto fail_commit;

	new_root = btrfs_read_fs_root(root->fs_info, &key, name, namelen);
	BUG_ON(!new_root);

	trans = btrfs_start_transaction(new_root, 1);
	BUG_ON(!trans);

	ret = btrfs_create_subvol_root(new_root, trans, new_dirid,
				       BTRFS_I(dir)->block_group);
	if (ret)
		goto fail;

	/* Invalidate existing dcache entry for new subvolume. */
	btrfs_invalidate_dcache_root(root, name, namelen);

fail:
	nr = trans->blocks_used;
	err = btrfs_commit_transaction(trans, new_root);
	if (err && !ret)
		ret = err;
fail_commit:
	btrfs_btree_balance_dirty(root, nr);
	return ret;
}

static int create_snapshot(struct btrfs_root *root, char *name, int namelen)
{
	struct btrfs_pending_snapshot *pending_snapshot;
	struct btrfs_trans_handle *trans;
	int ret;
	int err;
	unsigned long nr = 0;

	if (!root->ref_cows)
		return -EINVAL;

	ret = btrfs_check_free_space(root, 1, 0);
	if (ret)
		goto fail_unlock;

	pending_snapshot = kmalloc(sizeof(*pending_snapshot), GFP_NOFS);
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
	trans = btrfs_start_transaction(root, 1);
	BUG_ON(!trans);
	pending_snapshot->root = root;
	list_add(&pending_snapshot->list,
		 &trans->transaction->pending_snapshots);
	ret = btrfs_update_inode(trans, root, root->inode);
	err = btrfs_commit_transaction(trans, root);

fail_unlock:
	btrfs_btree_balance_dirty(root, nr);
	return ret;
}

int btrfs_defrag_file(struct file *file)
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

		set_extent_delalloc(io_tree, page_start,
				    page_end, GFP_NOFS);

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

	vol_args = kmalloc(sizeof(*vol_args), GFP_NOFS);

	if (!vol_args)
		return -ENOMEM;

	if (copy_from_user(vol_args, arg, sizeof(*vol_args))) {
		ret = -EFAULT;
		goto out;
	}
	namelen = strlen(vol_args->name);
	if (namelen > BTRFS_VOL_NAME_MAX) {
		ret = -EINVAL;
		goto out;
	}

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
	device = btrfs_find_device(root, devid, NULL);
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

static noinline int btrfs_ioctl_snap_create(struct btrfs_root *root,
					    void __user *arg)
{
	struct btrfs_ioctl_vol_args *vol_args;
	struct btrfs_dir_item *di;
	struct btrfs_path *path;
	u64 root_dirid;
	int namelen;
	int ret;

	vol_args = kmalloc(sizeof(*vol_args), GFP_NOFS);

	if (!vol_args)
		return -ENOMEM;

	if (copy_from_user(vol_args, arg, sizeof(*vol_args))) {
		ret = -EFAULT;
		goto out;
	}

	namelen = strlen(vol_args->name);
	if (namelen > BTRFS_VOL_NAME_MAX) {
		ret = -EINVAL;
		goto out;
	}
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

	mutex_lock(&root->fs_info->drop_mutex);
	if (root == root->fs_info->tree_root)
		ret = create_subvol(root, vol_args->name, namelen);
	else
		ret = create_snapshot(root, vol_args->name, namelen);
	mutex_unlock(&root->fs_info->drop_mutex);
out:
	kfree(vol_args);
	return ret;
}

static int btrfs_ioctl_defrag(struct file *file)
{
	struct inode *inode = fdentry(file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;

	switch (inode->i_mode & S_IFMT) {
	case S_IFDIR:
		btrfs_defrag_root(root, 0);
		btrfs_defrag_root(root->fs_info->extent_root, 0);
		break;
	case S_IFREG:
		btrfs_defrag_file(file);
		break;
	}

	return 0;
}

long btrfs_ioctl_add_dev(struct btrfs_root *root, void __user *arg)
{
	struct btrfs_ioctl_vol_args *vol_args;
	int ret;

	vol_args = kmalloc(sizeof(*vol_args), GFP_NOFS);

	if (!vol_args)
		return -ENOMEM;

	if (copy_from_user(vol_args, arg, sizeof(*vol_args))) {
		ret = -EFAULT;
		goto out;
	}
	ret = btrfs_init_new_device(root, vol_args->name);

out:
	kfree(vol_args);
	return ret;
}

long btrfs_ioctl_rm_dev(struct btrfs_root *root, void __user *arg)
{
	struct btrfs_ioctl_vol_args *vol_args;
	int ret;

	vol_args = kmalloc(sizeof(*vol_args), GFP_NOFS);

	if (!vol_args)
		return -ENOMEM;

	if (copy_from_user(vol_args, arg, sizeof(*vol_args))) {
		ret = -EFAULT;
		goto out;
	}
	ret = btrfs_rm_device(root, vol_args->name);

out:
	kfree(vol_args);
	return ret;
}

int dup_item_to_inode(struct btrfs_trans_handle *trans,
		       struct btrfs_root *root,
		       struct btrfs_path *path,
		       struct extent_buffer *leaf,
		       int slot,
		       struct btrfs_key *key,
		       u64 destino)
{
	char *dup;
	int len = btrfs_item_size_nr(leaf, slot);
	struct btrfs_key ckey = *key;
	int ret = 0;

	dup = kmalloc(len, GFP_NOFS);
	if (!dup)
		return -ENOMEM;

	read_extent_buffer(leaf, dup, btrfs_item_ptr_offset(leaf, slot), len);
	btrfs_release_path(root, path);

	ckey.objectid = destino;
	ret = btrfs_insert_item(trans, root, &ckey, dup, len);
	kfree(dup);
	return ret;
}

long btrfs_ioctl_clone(struct file *file, unsigned long src_fd)
{
	struct inode *inode = fdentry(file)->d_inode;
	struct btrfs_root *root = BTRFS_I(inode)->root;
	struct file *src_file;
	struct inode *src;
	struct btrfs_trans_handle *trans;
	int ret;
	u64 pos;
	struct btrfs_path *path;
	struct btrfs_key key;
	struct extent_buffer *leaf;
	u32 nritems;
	int slot;

	src_file = fget(src_fd);
	if (!src_file)
		return -EBADF;
	src = src_file->f_dentry->d_inode;

	ret = -EXDEV;
	if (src->i_sb != inode->i_sb)
		goto out_fput;

	if (inode < src) {
		mutex_lock(&inode->i_mutex);
		mutex_lock(&src->i_mutex);
	} else {
		mutex_lock(&src->i_mutex);
		mutex_lock(&inode->i_mutex);
	}

	ret = -ENOTEMPTY;
	if (inode->i_size)
		goto out_unlock;

	/* do any pending delalloc/csum calc on src, one way or
	   another, and lock file content */
	while (1) {
		filemap_write_and_wait(src->i_mapping);
		lock_extent(&BTRFS_I(src)->io_tree, 0, (u64)-1, GFP_NOFS);
		if (BTRFS_I(src)->delalloc_bytes == 0)
			break;
		unlock_extent(&BTRFS_I(src)->io_tree, 0, (u64)-1, GFP_NOFS);
	}

	trans = btrfs_start_transaction(root, 0);
	path = btrfs_alloc_path();
	if (!path) {
		ret = -ENOMEM;
		goto out;
	}
	key.offset = 0;
	key.type = BTRFS_EXTENT_DATA_KEY;
	key.objectid = src->i_ino;
	pos = 0;
	path->reada = 2;

	while (1) {
		/*
		 * note the key will change type as we walk through the
		 * tree.
		 */
		ret = btrfs_search_slot(trans, root, &key, path, 0, 0);
		if (ret < 0)
			goto out;

		if (path->slots[0] >= btrfs_header_nritems(path->nodes[0])) {
			ret = btrfs_next_leaf(root, path);
			if (ret < 0)
				goto out;
			if (ret > 0)
				break;
		}
		leaf = path->nodes[0];
		slot = path->slots[0];
		btrfs_item_key_to_cpu(leaf, &key, slot);
		nritems = btrfs_header_nritems(leaf);

		if (btrfs_key_type(&key) > BTRFS_CSUM_ITEM_KEY ||
		    key.objectid != src->i_ino)
			break;

		if (btrfs_key_type(&key) == BTRFS_EXTENT_DATA_KEY) {
			struct btrfs_file_extent_item *extent;
			int found_type;
			pos = key.offset;
			extent = btrfs_item_ptr(leaf, slot,
						struct btrfs_file_extent_item);
			found_type = btrfs_file_extent_type(leaf, extent);
			if (found_type == BTRFS_FILE_EXTENT_REG) {
				u64 len = btrfs_file_extent_num_bytes(leaf,
								      extent);
				u64 ds = btrfs_file_extent_disk_bytenr(leaf,
								       extent);
				u64 dl = btrfs_file_extent_disk_num_bytes(leaf,
								 extent);
				u64 off = btrfs_file_extent_offset(leaf,
								   extent);
				btrfs_insert_file_extent(trans, root,
							 inode->i_ino, pos,
							 ds, dl, len, off);
				/* ds == 0 means there's a hole */
				if (ds != 0) {
					btrfs_inc_extent_ref(trans, root,
						     ds, dl,
						     root->root_key.objectid,
						     trans->transid,
						     inode->i_ino, pos);
				}
				pos = key.offset + len;
			} else if (found_type == BTRFS_FILE_EXTENT_INLINE) {
				ret = dup_item_to_inode(trans, root, path,
							leaf, slot, &key,
							inode->i_ino);
				if (ret)
					goto out;
				pos = key.offset + btrfs_item_size_nr(leaf,
								      slot);
			}
		} else if (btrfs_key_type(&key) == BTRFS_CSUM_ITEM_KEY) {
			ret = dup_item_to_inode(trans, root, path, leaf,
						slot, &key, inode->i_ino);

			if (ret)
				goto out;
		}
		key.offset++;
		btrfs_release_path(root, path);
	}

	ret = 0;
out:
	btrfs_free_path(path);

	inode->i_blocks = src->i_blocks;
	i_size_write(inode, src->i_size);
	btrfs_update_inode(trans, root, inode);

	unlock_extent(&BTRFS_I(src)->io_tree, 0, (u64)-1, GFP_NOFS);

	btrfs_end_transaction(trans, root);

out_unlock:
	mutex_unlock(&src->i_mutex);
	mutex_unlock(&inode->i_mutex);
out_fput:
	fput(src_file);
	return ret;
}

/*
 * there are many ways the trans_start and trans_end ioctls can lead
 * to deadlocks.  They should only be used by applications that
 * basically own the machine, and have a very in depth understanding
 * of all the possible deadlocks and enospc problems.
 */
long btrfs_ioctl_trans_start(struct file *file)
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
	trans = btrfs_start_transaction(root, 0);
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
	file->private_data = 0;
out:
	return ret;
}

long btrfs_ioctl(struct file *file, unsigned int
		cmd, unsigned long arg)
{
	struct btrfs_root *root = BTRFS_I(fdentry(file)->d_inode)->root;

	switch (cmd) {
	case BTRFS_IOC_SNAP_CREATE:
		return btrfs_ioctl_snap_create(root, (void __user *)arg);
	case BTRFS_IOC_DEFRAG:
		return btrfs_ioctl_defrag(file);
	case BTRFS_IOC_RESIZE:
		return btrfs_ioctl_resize(root, (void __user *)arg);
	case BTRFS_IOC_ADD_DEV:
		return btrfs_ioctl_add_dev(root, (void __user *)arg);
	case BTRFS_IOC_RM_DEV:
		return btrfs_ioctl_rm_dev(root, (void __user *)arg);
	case BTRFS_IOC_BALANCE:
		return btrfs_balance(root->fs_info->dev_root);
	case BTRFS_IOC_CLONE:
		return btrfs_ioctl_clone(file, arg);
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
