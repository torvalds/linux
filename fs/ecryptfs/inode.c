/**
 * eCryptfs: Linux filesystem encryption layer
 *
 * Copyright (C) 1997-2004 Erez Zadok
 * Copyright (C) 2001-2004 Stony Brook University
 * Copyright (C) 2004-2007 International Business Machines Corp.
 *   Author(s): Michael A. Halcrow <mahalcro@us.ibm.com>
 *              Michael C. Thompsion <mcthomps@us.ibm.com>
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

#include <linux/file.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/dcache.h>
#include <linux/namei.h>
#include <linux/mount.h>
#include <linux/crypto.h>
#include <linux/fs_stack.h>
#include "ecryptfs_kernel.h"

static struct dentry *lock_parent(struct dentry *dentry)
{
	struct dentry *dir;

	dir = dget(dentry->d_parent);
	mutex_lock_nested(&(dir->d_inode->i_mutex), I_MUTEX_PARENT);
	return dir;
}

static void unlock_parent(struct dentry *dentry)
{
	mutex_unlock(&(dentry->d_parent->d_inode->i_mutex));
	dput(dentry->d_parent);
}

static void unlock_dir(struct dentry *dir)
{
	mutex_unlock(&dir->d_inode->i_mutex);
	dput(dir);
}

/**
 * ecryptfs_create_underlying_file
 * @lower_dir_inode: inode of the parent in the lower fs of the new file
 * @lower_dentry: New file's dentry in the lower fs
 * @ecryptfs_dentry: New file's dentry in ecryptfs
 * @mode: The mode of the new file
 * @nd: nameidata of ecryptfs' parent's dentry & vfsmount
 *
 * Creates the file in the lower file system.
 *
 * Returns zero on success; non-zero on error condition
 */
static int
ecryptfs_create_underlying_file(struct inode *lower_dir_inode,
				struct dentry *dentry, int mode,
				struct nameidata *nd)
{
	struct dentry *lower_dentry = ecryptfs_dentry_to_lower(dentry);
	struct vfsmount *lower_mnt = ecryptfs_dentry_to_lower_mnt(dentry);
	struct dentry *dentry_save;
	struct vfsmount *vfsmount_save;
	int rc;

	dentry_save = nd->path.dentry;
	vfsmount_save = nd->path.mnt;
	nd->path.dentry = lower_dentry;
	nd->path.mnt = lower_mnt;
	rc = vfs_create(lower_dir_inode, lower_dentry, mode, nd);
	nd->path.dentry = dentry_save;
	nd->path.mnt = vfsmount_save;
	return rc;
}

/**
 * ecryptfs_do_create
 * @directory_inode: inode of the new file's dentry's parent in ecryptfs
 * @ecryptfs_dentry: New file's dentry in ecryptfs
 * @mode: The mode of the new file
 * @nd: nameidata of ecryptfs' parent's dentry & vfsmount
 *
 * Creates the underlying file and the eCryptfs inode which will link to
 * it. It will also update the eCryptfs directory inode to mimic the
 * stat of the lower directory inode.
 *
 * Returns zero on success; non-zero on error condition
 */
static int
ecryptfs_do_create(struct inode *directory_inode,
		   struct dentry *ecryptfs_dentry, int mode,
		   struct nameidata *nd)
{
	int rc;
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;

	lower_dentry = ecryptfs_dentry_to_lower(ecryptfs_dentry);
	lower_dir_dentry = lock_parent(lower_dentry);
	if (unlikely(IS_ERR(lower_dir_dentry))) {
		ecryptfs_printk(KERN_ERR, "Error locking directory of "
				"dentry\n");
		rc = PTR_ERR(lower_dir_dentry);
		goto out;
	}
	rc = ecryptfs_create_underlying_file(lower_dir_dentry->d_inode,
					     ecryptfs_dentry, mode, nd);
	if (rc) {
		printk(KERN_ERR "%s: Failure to create dentry in lower fs; "
		       "rc = [%d]\n", __func__, rc);
		goto out_lock;
	}
	rc = ecryptfs_interpose(lower_dentry, ecryptfs_dentry,
				directory_inode->i_sb, 0);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Failure in ecryptfs_interpose\n");
		goto out_lock;
	}
	fsstack_copy_attr_times(directory_inode, lower_dir_dentry->d_inode);
	fsstack_copy_inode_size(directory_inode, lower_dir_dentry->d_inode);
out_lock:
	unlock_dir(lower_dir_dentry);
out:
	return rc;
}

/**
 * grow_file
 * @ecryptfs_dentry: the eCryptfs dentry
 *
 * This is the code which will grow the file to its correct size.
 */
static int grow_file(struct dentry *ecryptfs_dentry)
{
	struct inode *ecryptfs_inode = ecryptfs_dentry->d_inode;
	struct file fake_file;
	struct ecryptfs_file_info tmp_file_info;
	char zero_virt[] = { 0x00 };
	int rc = 0;

	memset(&fake_file, 0, sizeof(fake_file));
	fake_file.f_path.dentry = ecryptfs_dentry;
	memset(&tmp_file_info, 0, sizeof(tmp_file_info));
	ecryptfs_set_file_private(&fake_file, &tmp_file_info);
	ecryptfs_set_file_lower(
		&fake_file,
		ecryptfs_inode_to_private(ecryptfs_inode)->lower_file);
	rc = ecryptfs_write(&fake_file, zero_virt, 0, 1);
	i_size_write(ecryptfs_inode, 0);
	rc = ecryptfs_write_inode_size_to_metadata(ecryptfs_inode);
	ecryptfs_inode_to_private(ecryptfs_inode)->crypt_stat.flags |=
		ECRYPTFS_NEW_FILE;
	return rc;
}

/**
 * ecryptfs_initialize_file
 *
 * Cause the file to be changed from a basic empty file to an ecryptfs
 * file with a header and first data page.
 *
 * Returns zero on success
 */
static int ecryptfs_initialize_file(struct dentry *ecryptfs_dentry)
{
	struct ecryptfs_crypt_stat *crypt_stat =
		&ecryptfs_inode_to_private(ecryptfs_dentry->d_inode)->crypt_stat;
	int rc = 0;

	if (S_ISDIR(ecryptfs_dentry->d_inode->i_mode)) {
		ecryptfs_printk(KERN_DEBUG, "This is a directory\n");
		crypt_stat->flags &= ~(ECRYPTFS_ENCRYPTED);
		goto out;
	}
	crypt_stat->flags |= ECRYPTFS_NEW_FILE;
	ecryptfs_printk(KERN_DEBUG, "Initializing crypto context\n");
	rc = ecryptfs_new_file_context(ecryptfs_dentry);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error creating new file "
				"context; rc = [%d]\n", rc);
		goto out;
	}
	rc = ecryptfs_write_metadata(ecryptfs_dentry);
	if (rc) {
		printk(KERN_ERR "Error writing headers; rc = [%d]\n", rc);
		goto out;
	}
	rc = grow_file(ecryptfs_dentry);
	if (rc)
		printk(KERN_ERR "Error growing file; rc = [%d]\n", rc);
out:
	return rc;
}

/**
 * ecryptfs_create
 * @dir: The inode of the directory in which to create the file.
 * @dentry: The eCryptfs dentry
 * @mode: The mode of the new file.
 * @nd: nameidata
 *
 * Creates a new file.
 *
 * Returns zero on success; non-zero on error condition
 */
static int
ecryptfs_create(struct inode *directory_inode, struct dentry *ecryptfs_dentry,
		int mode, struct nameidata *nd)
{
	int rc;

	/* ecryptfs_do_create() calls ecryptfs_interpose(), which opens
	 * the crypt_stat->lower_file (persistent file) */
	rc = ecryptfs_do_create(directory_inode, ecryptfs_dentry, mode, nd);
	if (unlikely(rc)) {
		ecryptfs_printk(KERN_WARNING, "Failed to create file in"
				"lower filesystem\n");
		goto out;
	}
	/* At this point, a file exists on "disk"; we need to make sure
	 * that this on disk file is prepared to be an ecryptfs file */
	rc = ecryptfs_initialize_file(ecryptfs_dentry);
out:
	return rc;
}

/**
 * ecryptfs_lookup
 * @dir: inode
 * @dentry: The dentry
 * @nd: nameidata, may be NULL
 *
 * Find a file on disk. If the file does not exist, then we'll add it to the
 * dentry cache and continue on to read it from the disk.
 */
static struct dentry *ecryptfs_lookup(struct inode *dir, struct dentry *dentry,
				      struct nameidata *nd)
{
	int rc = 0;
	struct dentry *lower_dir_dentry;
	struct dentry *lower_dentry;
	struct vfsmount *lower_mnt;
	char *encoded_name;
	int encoded_namelen;
	struct ecryptfs_crypt_stat *crypt_stat = NULL;
	struct ecryptfs_mount_crypt_stat *mount_crypt_stat;
	char *page_virt = NULL;
	struct inode *lower_inode;
	u64 file_size;

	lower_dir_dentry = ecryptfs_dentry_to_lower(dentry->d_parent);
	dentry->d_op = &ecryptfs_dops;
	if ((dentry->d_name.len == 1 && !strcmp(dentry->d_name.name, "."))
	    || (dentry->d_name.len == 2
		&& !strcmp(dentry->d_name.name, ".."))) {
		d_drop(dentry);
		goto out;
	}
	encoded_namelen = ecryptfs_encode_filename(crypt_stat,
						   dentry->d_name.name,
						   dentry->d_name.len,
						   &encoded_name);
	if (encoded_namelen < 0) {
		rc = encoded_namelen;
		d_drop(dentry);
		goto out;
	}
	ecryptfs_printk(KERN_DEBUG, "encoded_name = [%s]; encoded_namelen "
			"= [%d]\n", encoded_name, encoded_namelen);
	lower_dentry = lookup_one_len(encoded_name, lower_dir_dentry,
				      encoded_namelen - 1);
	kfree(encoded_name);
	if (IS_ERR(lower_dentry)) {
		ecryptfs_printk(KERN_ERR, "ERR from lower_dentry\n");
		rc = PTR_ERR(lower_dentry);
		d_drop(dentry);
		goto out;
	}
	lower_mnt = mntget(ecryptfs_dentry_to_lower_mnt(dentry->d_parent));
	ecryptfs_printk(KERN_DEBUG, "lower_dentry = [%p]; lower_dentry->"
       		"d_name.name = [%s]\n", lower_dentry,
		lower_dentry->d_name.name);
	lower_inode = lower_dentry->d_inode;
	fsstack_copy_attr_atime(dir, lower_dir_dentry->d_inode);
	BUG_ON(!atomic_read(&lower_dentry->d_count));
	ecryptfs_set_dentry_private(dentry,
				    kmem_cache_alloc(ecryptfs_dentry_info_cache,
						     GFP_KERNEL));
	if (!ecryptfs_dentry_to_private(dentry)) {
		rc = -ENOMEM;
		ecryptfs_printk(KERN_ERR, "Out of memory whilst attempting "
				"to allocate ecryptfs_dentry_info struct\n");
		goto out_dput;
	}
	ecryptfs_set_dentry_lower(dentry, lower_dentry);
	ecryptfs_set_dentry_lower_mnt(dentry, lower_mnt);
	if (!lower_dentry->d_inode) {
		/* We want to add because we couldn't find in lower */
		d_add(dentry, NULL);
		goto out;
	}
	rc = ecryptfs_interpose(lower_dentry, dentry, dir->i_sb, 1);
	if (rc) {
		ecryptfs_printk(KERN_ERR, "Error interposing\n");
		goto out_dput;
	}
	if (S_ISDIR(lower_inode->i_mode)) {
		ecryptfs_printk(KERN_DEBUG, "Is a directory; returning\n");
		goto out;
	}
	if (S_ISLNK(lower_inode->i_mode)) {
		ecryptfs_printk(KERN_DEBUG, "Is a symlink; returning\n");
		goto out;
	}
	if (special_file(lower_inode->i_mode)) {
		ecryptfs_printk(KERN_DEBUG, "Is a special file; returning\n");
		goto out;
	}
	if (!nd) {
		ecryptfs_printk(KERN_DEBUG, "We have a NULL nd, just leave"
				"as we *think* we are about to unlink\n");
		goto out;
	}
	/* Released in this function */
	page_virt = kmem_cache_zalloc(ecryptfs_header_cache_2,
				      GFP_USER);
	if (!page_virt) {
		rc = -ENOMEM;
		ecryptfs_printk(KERN_ERR,
				"Cannot ecryptfs_kmalloc a page\n");
		goto out_dput;
	}
	crypt_stat = &ecryptfs_inode_to_private(dentry->d_inode)->crypt_stat;
	if (!(crypt_stat->flags & ECRYPTFS_POLICY_APPLIED))
		ecryptfs_set_default_sizes(crypt_stat);
	rc = ecryptfs_read_and_validate_header_region(page_virt,
						      dentry->d_inode);
	if (rc) {
		rc = ecryptfs_read_and_validate_xattr_region(page_virt, dentry);
		if (rc) {
			printk(KERN_DEBUG "Valid metadata not found in header "
			       "region or xattr region; treating file as "
			       "unencrypted\n");
			rc = 0;
			kmem_cache_free(ecryptfs_header_cache_2, page_virt);
			goto out;
		}
		crypt_stat->flags |= ECRYPTFS_METADATA_IN_XATTR;
	}
	mount_crypt_stat = &ecryptfs_superblock_to_private(
		dentry->d_sb)->mount_crypt_stat;
	if (mount_crypt_stat->flags & ECRYPTFS_ENCRYPTED_VIEW_ENABLED) {
		if (crypt_stat->flags & ECRYPTFS_METADATA_IN_XATTR)
			file_size = (crypt_stat->num_header_bytes_at_front
				     + i_size_read(lower_dentry->d_inode));
		else
			file_size = i_size_read(lower_dentry->d_inode);
	} else {
		memcpy(&file_size, page_virt, sizeof(file_size));
		file_size = be64_to_cpu(file_size);
	}
	i_size_write(dentry->d_inode, (loff_t)file_size);
	kmem_cache_free(ecryptfs_header_cache_2, page_virt);
	goto out;

out_dput:
	dput(lower_dentry);
	d_drop(dentry);
out:
	return ERR_PTR(rc);
}

static int ecryptfs_link(struct dentry *old_dentry, struct inode *dir,
			 struct dentry *new_dentry)
{
	struct dentry *lower_old_dentry;
	struct dentry *lower_new_dentry;
	struct dentry *lower_dir_dentry;
	u64 file_size_save;
	int rc;

	file_size_save = i_size_read(old_dentry->d_inode);
	lower_old_dentry = ecryptfs_dentry_to_lower(old_dentry);
	lower_new_dentry = ecryptfs_dentry_to_lower(new_dentry);
	dget(lower_old_dentry);
	dget(lower_new_dentry);
	lower_dir_dentry = lock_parent(lower_new_dentry);
	rc = vfs_link(lower_old_dentry, lower_dir_dentry->d_inode,
		      lower_new_dentry);
	if (rc || !lower_new_dentry->d_inode)
		goto out_lock;
	rc = ecryptfs_interpose(lower_new_dentry, new_dentry, dir->i_sb, 0);
	if (rc)
		goto out_lock;
	fsstack_copy_attr_times(dir, lower_new_dentry->d_inode);
	fsstack_copy_inode_size(dir, lower_new_dentry->d_inode);
	old_dentry->d_inode->i_nlink =
		ecryptfs_inode_to_lower(old_dentry->d_inode)->i_nlink;
	i_size_write(new_dentry->d_inode, file_size_save);
out_lock:
	unlock_dir(lower_dir_dentry);
	dput(lower_new_dentry);
	dput(lower_old_dentry);
	d_drop(lower_old_dentry);
	d_drop(new_dentry);
	d_drop(old_dentry);
	return rc;
}

static int ecryptfs_unlink(struct inode *dir, struct dentry *dentry)
{
	int rc = 0;
	struct dentry *lower_dentry = ecryptfs_dentry_to_lower(dentry);
	struct inode *lower_dir_inode = ecryptfs_inode_to_lower(dir);

	lock_parent(lower_dentry);
	rc = vfs_unlink(lower_dir_inode, lower_dentry);
	if (rc) {
		printk(KERN_ERR "Error in vfs_unlink; rc = [%d]\n", rc);
		goto out_unlock;
	}
	fsstack_copy_attr_times(dir, lower_dir_inode);
	dentry->d_inode->i_nlink =
		ecryptfs_inode_to_lower(dentry->d_inode)->i_nlink;
	dentry->d_inode->i_ctime = dir->i_ctime;
	d_drop(dentry);
out_unlock:
	unlock_parent(lower_dentry);
	return rc;
}

static int ecryptfs_symlink(struct inode *dir, struct dentry *dentry,
			    const char *symname)
{
	int rc;
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;
	umode_t mode;
	char *encoded_symname;
	int encoded_symlen;
	struct ecryptfs_crypt_stat *crypt_stat = NULL;

	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	dget(lower_dentry);
	lower_dir_dentry = lock_parent(lower_dentry);
	mode = S_IALLUGO;
	encoded_symlen = ecryptfs_encode_filename(crypt_stat, symname,
						  strlen(symname),
						  &encoded_symname);
	if (encoded_symlen < 0) {
		rc = encoded_symlen;
		goto out_lock;
	}
	rc = vfs_symlink(lower_dir_dentry->d_inode, lower_dentry,
			 encoded_symname, mode);
	kfree(encoded_symname);
	if (rc || !lower_dentry->d_inode)
		goto out_lock;
	rc = ecryptfs_interpose(lower_dentry, dentry, dir->i_sb, 0);
	if (rc)
		goto out_lock;
	fsstack_copy_attr_times(dir, lower_dir_dentry->d_inode);
	fsstack_copy_inode_size(dir, lower_dir_dentry->d_inode);
out_lock:
	unlock_dir(lower_dir_dentry);
	dput(lower_dentry);
	if (!dentry->d_inode)
		d_drop(dentry);
	return rc;
}

static int ecryptfs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	int rc;
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;

	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	lower_dir_dentry = lock_parent(lower_dentry);
	rc = vfs_mkdir(lower_dir_dentry->d_inode, lower_dentry, mode);
	if (rc || !lower_dentry->d_inode)
		goto out;
	rc = ecryptfs_interpose(lower_dentry, dentry, dir->i_sb, 0);
	if (rc)
		goto out;
	fsstack_copy_attr_times(dir, lower_dir_dentry->d_inode);
	fsstack_copy_inode_size(dir, lower_dir_dentry->d_inode);
	dir->i_nlink = lower_dir_dentry->d_inode->i_nlink;
out:
	unlock_dir(lower_dir_dentry);
	if (!dentry->d_inode)
		d_drop(dentry);
	return rc;
}

static int ecryptfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;
	int rc;

	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	dget(dentry);
	lower_dir_dentry = lock_parent(lower_dentry);
	dget(lower_dentry);
	rc = vfs_rmdir(lower_dir_dentry->d_inode, lower_dentry);
	dput(lower_dentry);
	if (!rc)
		d_delete(lower_dentry);
	fsstack_copy_attr_times(dir, lower_dir_dentry->d_inode);
	dir->i_nlink = lower_dir_dentry->d_inode->i_nlink;
	unlock_dir(lower_dir_dentry);
	if (!rc)
		d_drop(dentry);
	dput(dentry);
	return rc;
}

static int
ecryptfs_mknod(struct inode *dir, struct dentry *dentry, int mode, dev_t dev)
{
	int rc;
	struct dentry *lower_dentry;
	struct dentry *lower_dir_dentry;

	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	lower_dir_dentry = lock_parent(lower_dentry);
	rc = vfs_mknod(lower_dir_dentry->d_inode, lower_dentry, mode, dev);
	if (rc || !lower_dentry->d_inode)
		goto out;
	rc = ecryptfs_interpose(lower_dentry, dentry, dir->i_sb, 0);
	if (rc)
		goto out;
	fsstack_copy_attr_times(dir, lower_dir_dentry->d_inode);
	fsstack_copy_inode_size(dir, lower_dir_dentry->d_inode);
out:
	unlock_dir(lower_dir_dentry);
	if (!dentry->d_inode)
		d_drop(dentry);
	return rc;
}

static int
ecryptfs_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry)
{
	int rc;
	struct dentry *lower_old_dentry;
	struct dentry *lower_new_dentry;
	struct dentry *lower_old_dir_dentry;
	struct dentry *lower_new_dir_dentry;

	lower_old_dentry = ecryptfs_dentry_to_lower(old_dentry);
	lower_new_dentry = ecryptfs_dentry_to_lower(new_dentry);
	dget(lower_old_dentry);
	dget(lower_new_dentry);
	lower_old_dir_dentry = dget_parent(lower_old_dentry);
	lower_new_dir_dentry = dget_parent(lower_new_dentry);
	lock_rename(lower_old_dir_dentry, lower_new_dir_dentry);
	rc = vfs_rename(lower_old_dir_dentry->d_inode, lower_old_dentry,
			lower_new_dir_dentry->d_inode, lower_new_dentry);
	if (rc)
		goto out_lock;
	fsstack_copy_attr_all(new_dir, lower_new_dir_dentry->d_inode, NULL);
	if (new_dir != old_dir)
		fsstack_copy_attr_all(old_dir, lower_old_dir_dentry->d_inode, NULL);
out_lock:
	unlock_rename(lower_old_dir_dentry, lower_new_dir_dentry);
	dput(lower_new_dentry->d_parent);
	dput(lower_old_dentry->d_parent);
	dput(lower_new_dentry);
	dput(lower_old_dentry);
	return rc;
}

static int
ecryptfs_readlink(struct dentry *dentry, char __user * buf, int bufsiz)
{
	int rc;
	struct dentry *lower_dentry;
	char *decoded_name;
	char *lower_buf;
	mm_segment_t old_fs;
	struct ecryptfs_crypt_stat *crypt_stat;

	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	if (!lower_dentry->d_inode->i_op ||
	    !lower_dentry->d_inode->i_op->readlink) {
		rc = -EINVAL;
		goto out;
	}
	/* Released in this function */
	lower_buf = kmalloc(bufsiz, GFP_KERNEL);
	if (lower_buf == NULL) {
		ecryptfs_printk(KERN_ERR, "Out of memory\n");
		rc = -ENOMEM;
		goto out;
	}
	old_fs = get_fs();
	set_fs(get_ds());
	ecryptfs_printk(KERN_DEBUG, "Calling readlink w/ "
			"lower_dentry->d_name.name = [%s]\n",
			lower_dentry->d_name.name);
	rc = lower_dentry->d_inode->i_op->readlink(lower_dentry,
						   (char __user *)lower_buf,
						   bufsiz);
	set_fs(old_fs);
	if (rc >= 0) {
		crypt_stat = NULL;
		rc = ecryptfs_decode_filename(crypt_stat, lower_buf, rc,
					      &decoded_name);
		if (rc == -ENOMEM)
			goto out_free_lower_buf;
		if (rc > 0) {
			ecryptfs_printk(KERN_DEBUG, "Copying [%d] bytes "
					"to userspace: [%*s]\n", rc,
					decoded_name);
			if (copy_to_user(buf, decoded_name, rc))
				rc = -EFAULT;
		}
		kfree(decoded_name);
		fsstack_copy_attr_atime(dentry->d_inode,
					lower_dentry->d_inode);
	}
out_free_lower_buf:
	kfree(lower_buf);
out:
	return rc;
}

static void *ecryptfs_follow_link(struct dentry *dentry, struct nameidata *nd)
{
	char *buf;
	int len = PAGE_SIZE, rc;
	mm_segment_t old_fs;

	/* Released in ecryptfs_put_link(); only release here on error */
	buf = kmalloc(len, GFP_KERNEL);
	if (!buf) {
		rc = -ENOMEM;
		goto out;
	}
	old_fs = get_fs();
	set_fs(get_ds());
	ecryptfs_printk(KERN_DEBUG, "Calling readlink w/ "
			"dentry->d_name.name = [%s]\n", dentry->d_name.name);
	rc = dentry->d_inode->i_op->readlink(dentry, (char __user *)buf, len);
	buf[rc] = '\0';
	set_fs(old_fs);
	if (rc < 0)
		goto out_free;
	rc = 0;
	nd_set_link(nd, buf);
	goto out;
out_free:
	kfree(buf);
out:
	return ERR_PTR(rc);
}

static void
ecryptfs_put_link(struct dentry *dentry, struct nameidata *nd, void *ptr)
{
	/* Free the char* */
	kfree(nd_get_link(nd));
}

/**
 * upper_size_to_lower_size
 * @crypt_stat: Crypt_stat associated with file
 * @upper_size: Size of the upper file
 *
 * Calculate the required size of the lower file based on the
 * specified size of the upper file. This calculation is based on the
 * number of headers in the underlying file and the extent size.
 *
 * Returns Calculated size of the lower file.
 */
static loff_t
upper_size_to_lower_size(struct ecryptfs_crypt_stat *crypt_stat,
			 loff_t upper_size)
{
	loff_t lower_size;

	lower_size = crypt_stat->num_header_bytes_at_front;
	if (upper_size != 0) {
		loff_t num_extents;

		num_extents = upper_size >> crypt_stat->extent_shift;
		if (upper_size & ~crypt_stat->extent_mask)
			num_extents++;
		lower_size += (num_extents * crypt_stat->extent_size);
	}
	return lower_size;
}

/**
 * ecryptfs_truncate
 * @dentry: The ecryptfs layer dentry
 * @new_length: The length to expand the file to
 *
 * Function to handle truncations modifying the size of the file. Note
 * that the file sizes are interpolated. When expanding, we are simply
 * writing strings of 0's out. When truncating, we need to modify the
 * underlying file size according to the page index interpolations.
 *
 * Returns zero on success; non-zero otherwise
 */
int ecryptfs_truncate(struct dentry *dentry, loff_t new_length)
{
	int rc = 0;
	struct inode *inode = dentry->d_inode;
	struct dentry *lower_dentry;
	struct file fake_ecryptfs_file;
	struct ecryptfs_crypt_stat *crypt_stat;
	loff_t i_size = i_size_read(inode);
	loff_t lower_size_before_truncate;
	loff_t lower_size_after_truncate;

	if (unlikely((new_length == i_size)))
		goto out;
	crypt_stat = &ecryptfs_inode_to_private(dentry->d_inode)->crypt_stat;
	/* Set up a fake ecryptfs file, this is used to interface with
	 * the file in the underlying filesystem so that the
	 * truncation has an effect there as well. */
	memset(&fake_ecryptfs_file, 0, sizeof(fake_ecryptfs_file));
	fake_ecryptfs_file.f_path.dentry = dentry;
	/* Released at out_free: label */
	ecryptfs_set_file_private(&fake_ecryptfs_file,
				  kmem_cache_alloc(ecryptfs_file_info_cache,
						   GFP_KERNEL));
	if (unlikely(!ecryptfs_file_to_private(&fake_ecryptfs_file))) {
		rc = -ENOMEM;
		goto out;
	}
	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	ecryptfs_set_file_lower(
		&fake_ecryptfs_file,
		ecryptfs_inode_to_private(dentry->d_inode)->lower_file);
	/* Switch on growing or shrinking file */
	if (new_length > i_size) {
		char zero[] = { 0x00 };

		/* Write a single 0 at the last position of the file;
		 * this triggers code that will fill in 0's throughout
		 * the intermediate portion of the previous end of the
		 * file and the new and of the file */
		rc = ecryptfs_write(&fake_ecryptfs_file, zero,
				    (new_length - 1), 1);
	} else { /* new_length < i_size_read(inode) */
		/* We're chopping off all the pages down do the page
		 * in which new_length is located. Fill in the end of
		 * that page from (new_length & ~PAGE_CACHE_MASK) to
		 * PAGE_CACHE_SIZE with zeros. */
		size_t num_zeros = (PAGE_CACHE_SIZE
				    - (new_length & ~PAGE_CACHE_MASK));

		if (num_zeros) {
			char *zeros_virt;

			zeros_virt = kzalloc(num_zeros, GFP_KERNEL);
			if (!zeros_virt) {
				rc = -ENOMEM;
				goto out_free;
			}
			rc = ecryptfs_write(&fake_ecryptfs_file, zeros_virt,
					    new_length, num_zeros);
			kfree(zeros_virt);
			if (rc) {
				printk(KERN_ERR "Error attempting to zero out "
				       "the remainder of the end page on "
				       "reducing truncate; rc = [%d]\n", rc);
				goto out_free;
			}
		}
		vmtruncate(inode, new_length);
		rc = ecryptfs_write_inode_size_to_metadata(inode);
		if (rc) {
			printk(KERN_ERR	"Problem with "
			       "ecryptfs_write_inode_size_to_metadata; "
			       "rc = [%d]\n", rc);
			goto out_free;
		}
		/* We are reducing the size of the ecryptfs file, and need to
		 * know if we need to reduce the size of the lower file. */
		lower_size_before_truncate =
		    upper_size_to_lower_size(crypt_stat, i_size);
		lower_size_after_truncate =
		    upper_size_to_lower_size(crypt_stat, new_length);
		if (lower_size_after_truncate < lower_size_before_truncate)
			vmtruncate(lower_dentry->d_inode,
				   lower_size_after_truncate);
	}
out_free:
	if (ecryptfs_file_to_private(&fake_ecryptfs_file))
		kmem_cache_free(ecryptfs_file_info_cache,
				ecryptfs_file_to_private(&fake_ecryptfs_file));
out:
	return rc;
}

static int
ecryptfs_permission(struct inode *inode, int mask, struct nameidata *nd)
{
	int rc;

        if (nd) {
		struct vfsmount *vfsmnt_save = nd->path.mnt;
		struct dentry *dentry_save = nd->path.dentry;

		nd->path.mnt = ecryptfs_dentry_to_lower_mnt(nd->path.dentry);
		nd->path.dentry = ecryptfs_dentry_to_lower(nd->path.dentry);
		rc = permission(ecryptfs_inode_to_lower(inode), mask, nd);
		nd->path.mnt = vfsmnt_save;
		nd->path.dentry = dentry_save;
        } else
		rc = permission(ecryptfs_inode_to_lower(inode), mask, NULL);
        return rc;
}

/**
 * ecryptfs_setattr
 * @dentry: dentry handle to the inode to modify
 * @ia: Structure with flags of what to change and values
 *
 * Updates the metadata of an inode. If the update is to the size
 * i.e. truncation, then ecryptfs_truncate will handle the size modification
 * of both the ecryptfs inode and the lower inode.
 *
 * All other metadata changes will be passed right to the lower filesystem,
 * and we will just update our inode to look like the lower.
 */
static int ecryptfs_setattr(struct dentry *dentry, struct iattr *ia)
{
	int rc = 0;
	struct dentry *lower_dentry;
	struct inode *inode;
	struct inode *lower_inode;
	struct ecryptfs_crypt_stat *crypt_stat;

	crypt_stat = &ecryptfs_inode_to_private(dentry->d_inode)->crypt_stat;
	if (!(crypt_stat->flags & ECRYPTFS_STRUCT_INITIALIZED))
		ecryptfs_init_crypt_stat(crypt_stat);
	inode = dentry->d_inode;
	lower_inode = ecryptfs_inode_to_lower(inode);
	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	mutex_lock(&crypt_stat->cs_mutex);
	if (S_ISDIR(dentry->d_inode->i_mode))
		crypt_stat->flags &= ~(ECRYPTFS_ENCRYPTED);
	else if (S_ISREG(dentry->d_inode->i_mode)
		 && (!(crypt_stat->flags & ECRYPTFS_POLICY_APPLIED)
		     || !(crypt_stat->flags & ECRYPTFS_KEY_VALID))) {
		struct ecryptfs_mount_crypt_stat *mount_crypt_stat;

		mount_crypt_stat = &ecryptfs_superblock_to_private(
			dentry->d_sb)->mount_crypt_stat;
		rc = ecryptfs_read_metadata(dentry);
		if (rc) {
			if (!(mount_crypt_stat->flags
			      & ECRYPTFS_PLAINTEXT_PASSTHROUGH_ENABLED)) {
				rc = -EIO;
				printk(KERN_WARNING "Either the lower file "
				       "is not in a valid eCryptfs format, "
				       "or the key could not be retrieved. "
				       "Plaintext passthrough mode is not "
				       "enabled; returning -EIO\n");
				mutex_unlock(&crypt_stat->cs_mutex);
				goto out;
			}
			rc = 0;
			crypt_stat->flags &= ~(ECRYPTFS_ENCRYPTED);
			mutex_unlock(&crypt_stat->cs_mutex);
			goto out;
		}
	}
	mutex_unlock(&crypt_stat->cs_mutex);
	if (ia->ia_valid & ATTR_SIZE) {
		ecryptfs_printk(KERN_DEBUG,
				"ia->ia_valid = [0x%x] ATTR_SIZE" " = [0x%x]\n",
				ia->ia_valid, ATTR_SIZE);
		rc = ecryptfs_truncate(dentry, ia->ia_size);
		/* ecryptfs_truncate handles resizing of the lower file */
		ia->ia_valid &= ~ATTR_SIZE;
		ecryptfs_printk(KERN_DEBUG, "ia->ia_valid = [%x]\n",
				ia->ia_valid);
		if (rc < 0)
			goto out;
	}

	/*
	 * mode change is for clearing setuid/setgid bits. Allow lower fs
	 * to interpret this in its own way.
	 */
	if (ia->ia_valid & (ATTR_KILL_SUID | ATTR_KILL_SGID))
		ia->ia_valid &= ~ATTR_MODE;

	rc = notify_change(lower_dentry, ia);
out:
	fsstack_copy_attr_all(inode, lower_inode, NULL);
	return rc;
}

int
ecryptfs_setxattr(struct dentry *dentry, const char *name, const void *value,
		  size_t size, int flags)
{
	int rc = 0;
	struct dentry *lower_dentry;

	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	if (!lower_dentry->d_inode->i_op->setxattr) {
		rc = -ENOSYS;
		goto out;
	}
	mutex_lock(&lower_dentry->d_inode->i_mutex);
	rc = lower_dentry->d_inode->i_op->setxattr(lower_dentry, name, value,
						   size, flags);
	mutex_unlock(&lower_dentry->d_inode->i_mutex);
out:
	return rc;
}

ssize_t
ecryptfs_getxattr_lower(struct dentry *lower_dentry, const char *name,
			void *value, size_t size)
{
	int rc = 0;

	if (!lower_dentry->d_inode->i_op->getxattr) {
		rc = -ENOSYS;
		goto out;
	}
	mutex_lock(&lower_dentry->d_inode->i_mutex);
	rc = lower_dentry->d_inode->i_op->getxattr(lower_dentry, name, value,
						   size);
	mutex_unlock(&lower_dentry->d_inode->i_mutex);
out:
	return rc;
}

static ssize_t
ecryptfs_getxattr(struct dentry *dentry, const char *name, void *value,
		  size_t size)
{
	return ecryptfs_getxattr_lower(ecryptfs_dentry_to_lower(dentry), name,
				       value, size);
}

static ssize_t
ecryptfs_listxattr(struct dentry *dentry, char *list, size_t size)
{
	int rc = 0;
	struct dentry *lower_dentry;

	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	if (!lower_dentry->d_inode->i_op->listxattr) {
		rc = -ENOSYS;
		goto out;
	}
	mutex_lock(&lower_dentry->d_inode->i_mutex);
	rc = lower_dentry->d_inode->i_op->listxattr(lower_dentry, list, size);
	mutex_unlock(&lower_dentry->d_inode->i_mutex);
out:
	return rc;
}

static int ecryptfs_removexattr(struct dentry *dentry, const char *name)
{
	int rc = 0;
	struct dentry *lower_dentry;

	lower_dentry = ecryptfs_dentry_to_lower(dentry);
	if (!lower_dentry->d_inode->i_op->removexattr) {
		rc = -ENOSYS;
		goto out;
	}
	mutex_lock(&lower_dentry->d_inode->i_mutex);
	rc = lower_dentry->d_inode->i_op->removexattr(lower_dentry, name);
	mutex_unlock(&lower_dentry->d_inode->i_mutex);
out:
	return rc;
}

int ecryptfs_inode_test(struct inode *inode, void *candidate_lower_inode)
{
	if ((ecryptfs_inode_to_lower(inode)
	     == (struct inode *)candidate_lower_inode))
		return 1;
	else
		return 0;
}

int ecryptfs_inode_set(struct inode *inode, void *lower_inode)
{
	ecryptfs_init_inode(inode, (struct inode *)lower_inode);
	return 0;
}

const struct inode_operations ecryptfs_symlink_iops = {
	.readlink = ecryptfs_readlink,
	.follow_link = ecryptfs_follow_link,
	.put_link = ecryptfs_put_link,
	.permission = ecryptfs_permission,
	.setattr = ecryptfs_setattr,
	.setxattr = ecryptfs_setxattr,
	.getxattr = ecryptfs_getxattr,
	.listxattr = ecryptfs_listxattr,
	.removexattr = ecryptfs_removexattr
};

const struct inode_operations ecryptfs_dir_iops = {
	.create = ecryptfs_create,
	.lookup = ecryptfs_lookup,
	.link = ecryptfs_link,
	.unlink = ecryptfs_unlink,
	.symlink = ecryptfs_symlink,
	.mkdir = ecryptfs_mkdir,
	.rmdir = ecryptfs_rmdir,
	.mknod = ecryptfs_mknod,
	.rename = ecryptfs_rename,
	.permission = ecryptfs_permission,
	.setattr = ecryptfs_setattr,
	.setxattr = ecryptfs_setxattr,
	.getxattr = ecryptfs_getxattr,
	.listxattr = ecryptfs_listxattr,
	.removexattr = ecryptfs_removexattr
};

const struct inode_operations ecryptfs_main_iops = {
	.permission = ecryptfs_permission,
	.setattr = ecryptfs_setattr,
	.setxattr = ecryptfs_setxattr,
	.getxattr = ecryptfs_getxattr,
	.listxattr = ecryptfs_listxattr,
	.removexattr = ecryptfs_removexattr
};
