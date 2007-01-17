/**
 * eCryptfs: Linux filesystem encryption layer
 *
 * Copyright (C) 1997-2003 Erez Zadok
 * Copyright (C) 2001-2003 Stony Brook University
 * Copyright (C) 2004-2006 International Business Machines Corp.
 *   Author(s): Michael A. Halcrow <mahalcro@us.ibm.com>
 *              Michael C. Thompson <mcthomps@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <linux/fs.h>
#include <linux/mount.h>
#include <linux/key.h>
#include <linux/seq_file.h>
#include <linux/crypto.h>
#include "ecryptfs_kernel.h"

struct kmem_cache *ecryptfs_inode_info_cache;

/**
 * ecryptfs_alloc_inode - allocate an ecryptfs inode
 * @sb: Pointer to the ecryptfs super block
 *
 * Called to bring an inode into existence.
 *
 * Only handle allocation, setting up structures should be done in
 * ecryptfs_read_inode. This is because the kernel, between now and
 * then, will 0 out the private data pointer.
 *
 * Returns a pointer to a newly allocated inode, NULL otherwise
 */
static struct inode *ecryptfs_alloc_inode(struct super_block *sb)
{
	struct ecryptfs_inode_info *ecryptfs_inode;
	struct inode *inode = NULL;

	ecryptfs_inode = kmem_cache_alloc(ecryptfs_inode_info_cache,
					  GFP_KERNEL);
	if (unlikely(!ecryptfs_inode))
		goto out;
	ecryptfs_init_crypt_stat(&ecryptfs_inode->crypt_stat);
	inode = &ecryptfs_inode->vfs_inode;
out:
	return inode;
}

/**
 * ecryptfs_destroy_inode
 * @inode: The ecryptfs inode
 *
 * This is used during the final destruction of the inode.
 * All allocation of memory related to the inode, including allocated
 * memory in the crypt_stat struct, will be released here.
 * There should be no chance that this deallocation will be missed.
 */
static void ecryptfs_destroy_inode(struct inode *inode)
{
	struct ecryptfs_inode_info *inode_info;

	inode_info = ecryptfs_inode_to_private(inode);
	ecryptfs_destruct_crypt_stat(&inode_info->crypt_stat);
	kmem_cache_free(ecryptfs_inode_info_cache, inode_info);
}

/**
 * ecryptfs_init_inode
 * @inode: The ecryptfs inode
 *
 * Set up the ecryptfs inode.
 */
void ecryptfs_init_inode(struct inode *inode, struct inode *lower_inode)
{
	ecryptfs_set_inode_lower(inode, lower_inode);
	inode->i_ino = lower_inode->i_ino;
	inode->i_version++;
	inode->i_op = &ecryptfs_main_iops;
	inode->i_fop = &ecryptfs_main_fops;
	inode->i_mapping->a_ops = &ecryptfs_aops;
}

/**
 * ecryptfs_put_super
 * @sb: Pointer to the ecryptfs super block
 *
 * Final actions when unmounting a file system.
 * This will handle deallocation and release of our private data.
 */
static void ecryptfs_put_super(struct super_block *sb)
{
	struct ecryptfs_sb_info *sb_info = ecryptfs_superblock_to_private(sb);

	ecryptfs_destruct_mount_crypt_stat(&sb_info->mount_crypt_stat);
	kmem_cache_free(ecryptfs_sb_info_cache, sb_info);
	ecryptfs_set_superblock_private(sb, NULL);
}

/**
 * ecryptfs_statfs
 * @sb: The ecryptfs super block
 * @buf: The struct kstatfs to fill in with stats
 *
 * Get the filesystem statistics. Currently, we let this pass right through
 * to the lower filesystem and take no action ourselves.
 */
static int ecryptfs_statfs(struct dentry *dentry, struct kstatfs *buf)
{
	return vfs_statfs(ecryptfs_dentry_to_lower(dentry), buf);
}

/**
 * ecryptfs_clear_inode
 * @inode - The ecryptfs inode
 *
 * Called by iput() when the inode reference count reached zero
 * and the inode is not hashed anywhere.  Used to clear anything
 * that needs to be, before the inode is completely destroyed and put
 * on the inode free list. We use this to drop out reference to the
 * lower inode.
 */
static void ecryptfs_clear_inode(struct inode *inode)
{
	iput(ecryptfs_inode_to_lower(inode));
}

/**
 * ecryptfs_show_options
 *
 * Prints the directory we are currently mounted over.
 * Returns zero on success; non-zero otherwise
 */
static int ecryptfs_show_options(struct seq_file *m, struct vfsmount *mnt)
{
	struct super_block *sb = mnt->mnt_sb;
	struct dentry *lower_root_dentry = ecryptfs_dentry_to_lower(sb->s_root);
	struct vfsmount *lower_mnt = ecryptfs_dentry_to_lower_mnt(sb->s_root);
	char *tmp_page;
	char *path;
	int rc = 0;

	tmp_page = (char *)__get_free_page(GFP_KERNEL);
	if (!tmp_page) {
		rc = -ENOMEM;
		goto out;
	}
	path = d_path(lower_root_dentry, lower_mnt, tmp_page, PAGE_SIZE);
	if (IS_ERR(path)) {
		rc = PTR_ERR(path);
		goto out;
	}
	seq_printf(m, ",dir=%s", path);
	free_page((unsigned long)tmp_page);
out:
	return rc;
}

struct super_operations ecryptfs_sops = {
	.alloc_inode = ecryptfs_alloc_inode,
	.destroy_inode = ecryptfs_destroy_inode,
	.drop_inode = generic_delete_inode,
	.put_super = ecryptfs_put_super,
	.statfs = ecryptfs_statfs,
	.remount_fs = NULL,
	.clear_inode = ecryptfs_clear_inode,
	.show_options = ecryptfs_show_options
};
