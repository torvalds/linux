// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 */

#include <linux/blkdev.h>
#include <linux/buffer_head.h>
#include <linux/fs.h>
#include <linux/iversion.h>
#include <linux/namei.h>
#include <linux/nls.h>

#include "debug.h"
#include "ntfs.h"
#include "ntfs_fs.h"

/*
 * fill_name_de - Format NTFS_DE in @buf.
 */
int fill_name_de(struct ntfs_sb_info *sbi, void *buf, const struct qstr *name,
		 const struct cpu_str *uni)
{
	int err;
	struct NTFS_DE *e = buf;
	u16 data_size;
	struct ATTR_FILE_NAME *fname = (struct ATTR_FILE_NAME *)(e + 1);

#ifndef CONFIG_NTFS3_64BIT_CLUSTER
	e->ref.high = fname->home.high = 0;
#endif
	if (uni) {
#ifdef __BIG_ENDIAN
		int ulen = uni->len;
		__le16 *uname = fname->name;
		const u16 *name_cpu = uni->name;

		while (ulen--)
			*uname++ = cpu_to_le16(*name_cpu++);
#else
		memcpy(fname->name, uni->name, uni->len * sizeof(u16));
#endif
		fname->name_len = uni->len;

	} else {
		/* Convert input string to unicode. */
		err = ntfs_nls_to_utf16(sbi, name->name, name->len,
					(struct cpu_str *)&fname->name_len,
					NTFS_NAME_LEN, UTF16_LITTLE_ENDIAN);
		if (err < 0)
			return err;
	}

	fname->type = FILE_NAME_POSIX;
	data_size = fname_full_size(fname);

	e->size = cpu_to_le16(ALIGN(data_size, 8) + sizeof(struct NTFS_DE));
	e->key_size = cpu_to_le16(data_size);
	e->flags = 0;
	e->res = 0;

	return 0;
}

/*
 * ntfs_lookup - inode_operations::lookup
 */
static struct dentry *ntfs_lookup(struct inode *dir, struct dentry *dentry,
				  u32 flags)
{
	struct ntfs_inode *ni = ntfs_i(dir);
	struct cpu_str *uni = __getname();
	struct inode *inode;
	int err;

	if (!uni)
		inode = ERR_PTR(-ENOMEM);
	else {
		err = ntfs_nls_to_utf16(ni->mi.sbi, dentry->d_name.name,
					dentry->d_name.len, uni, NTFS_NAME_LEN,
					UTF16_HOST_ENDIAN);
		if (err < 0)
			inode = ERR_PTR(err);
		else {
			ni_lock(ni);
			inode = dir_search_u(dir, uni, NULL);
			ni_unlock(ni);
		}
		__putname(uni);
	}

	return d_splice_alias(inode, dentry);
}

/*
 * ntfs_create - inode_operations::create
 */
static int ntfs_create(struct user_namespace *mnt_userns, struct inode *dir,
		       struct dentry *dentry, umode_t mode, bool excl)
{
	struct ntfs_inode *ni = ntfs_i(dir);
	struct inode *inode;

	ni_lock_dir(ni);

	inode = ntfs_create_inode(mnt_userns, dir, dentry, NULL, S_IFREG | mode,
				  0, NULL, 0, NULL);

	ni_unlock(ni);

	return IS_ERR(inode) ? PTR_ERR(inode) : 0;
}

/*
 * ntfs_mknod
 *
 * inode_operations::mknod
 */
static int ntfs_mknod(struct user_namespace *mnt_userns, struct inode *dir,
		      struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct ntfs_inode *ni = ntfs_i(dir);
	struct inode *inode;

	ni_lock_dir(ni);

	inode = ntfs_create_inode(mnt_userns, dir, dentry, NULL, mode, rdev,
				  NULL, 0, NULL);

	ni_unlock(ni);

	return IS_ERR(inode) ? PTR_ERR(inode) : 0;
}

/*
 * ntfs_link - inode_operations::link
 */
static int ntfs_link(struct dentry *ode, struct inode *dir, struct dentry *de)
{
	int err;
	struct inode *inode = d_inode(ode);
	struct ntfs_inode *ni = ntfs_i(inode);

	if (S_ISDIR(inode->i_mode))
		return -EPERM;

	if (inode->i_nlink >= NTFS_LINK_MAX)
		return -EMLINK;

	ni_lock_dir(ntfs_i(dir));
	if (inode != dir)
		ni_lock(ni);

	dir->i_ctime = dir->i_mtime = inode->i_ctime = current_time(inode);
	inc_nlink(inode);
	ihold(inode);

	err = ntfs_link_inode(inode, de);
	if (!err) {
		mark_inode_dirty(inode);
		mark_inode_dirty(dir);
		d_instantiate(de, inode);
	} else {
		drop_nlink(inode);
		iput(inode);
	}

	if (inode != dir)
		ni_unlock(ni);
	ni_unlock(ntfs_i(dir));

	return err;
}

/*
 * ntfs_unlink - inode_operations::unlink
 */
static int ntfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct ntfs_inode *ni = ntfs_i(dir);
	int err;

	ni_lock_dir(ni);

	err = ntfs_unlink_inode(dir, dentry);

	ni_unlock(ni);

	return err;
}

/*
 * ntfs_symlink - inode_operations::symlink
 */
static int ntfs_symlink(struct user_namespace *mnt_userns, struct inode *dir,
			struct dentry *dentry, const char *symname)
{
	u32 size = strlen(symname);
	struct inode *inode;
	struct ntfs_inode *ni = ntfs_i(dir);

	ni_lock_dir(ni);

	inode = ntfs_create_inode(mnt_userns, dir, dentry, NULL, S_IFLNK | 0777,
				  0, symname, size, NULL);

	ni_unlock(ni);

	return IS_ERR(inode) ? PTR_ERR(inode) : 0;
}

/*
 * ntfs_mkdir- inode_operations::mkdir
 */
static int ntfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,
		      struct dentry *dentry, umode_t mode)
{
	struct inode *inode;
	struct ntfs_inode *ni = ntfs_i(dir);

	ni_lock_dir(ni);

	inode = ntfs_create_inode(mnt_userns, dir, dentry, NULL, S_IFDIR | mode,
				  0, NULL, 0, NULL);

	ni_unlock(ni);

	return IS_ERR(inode) ? PTR_ERR(inode) : 0;
}

/*
 * ntfs_rmdir - inode_operations::rm_dir
 */
static int ntfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct ntfs_inode *ni = ntfs_i(dir);
	int err;

	ni_lock_dir(ni);

	err = ntfs_unlink_inode(dir, dentry);

	ni_unlock(ni);

	return err;
}

/*
 * ntfs_rename - inode_operations::rename
 */
static int ntfs_rename(struct user_namespace *mnt_userns, struct inode *old_dir,
		       struct dentry *old_dentry, struct inode *new_dir,
		       struct dentry *new_dentry, u32 flags)
{
	int err;
	struct super_block *sb = old_dir->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct ntfs_inode *old_dir_ni = ntfs_i(old_dir);
	struct ntfs_inode *new_dir_ni = ntfs_i(new_dir);
	struct ntfs_inode *old_ni;
	struct ATTR_FILE_NAME *old_name, *new_name, *fname;
	u8 name_type;
	bool is_same;
	struct inode *old_inode, *new_inode;
	struct NTFS_DE *old_de, *new_de;
	struct ATTRIB *attr;
	struct ATTR_LIST_ENTRY *le;
	u16 new_de_key_size;

	static_assert(SIZEOF_ATTRIBUTE_FILENAME_MAX + SIZEOF_RESIDENT < 1024);
	static_assert(SIZEOF_ATTRIBUTE_FILENAME_MAX + sizeof(struct NTFS_DE) <
		      1024);
	static_assert(PATH_MAX >= 4 * 1024);

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	old_inode = d_inode(old_dentry);
	new_inode = d_inode(new_dentry);

	old_ni = ntfs_i(old_inode);

	is_same = old_dentry->d_name.len == new_dentry->d_name.len &&
		  !memcmp(old_dentry->d_name.name, new_dentry->d_name.name,
			  old_dentry->d_name.len);

	if (is_same && old_dir == new_dir) {
		/* Nothing to do. */
		err = 0;
		goto out;
	}

	if (ntfs_is_meta_file(sbi, old_inode->i_ino)) {
		err = -EINVAL;
		goto out;
	}

	if (new_inode) {
		/* Target name exists. Unlink it. */
		dget(new_dentry);
		ni_lock_dir(new_dir_ni);
		err = ntfs_unlink_inode(new_dir, new_dentry);
		ni_unlock(new_dir_ni);
		dput(new_dentry);
		if (err)
			goto out;
	}

	/* Allocate PATH_MAX bytes. */
	old_de = __getname();
	if (!old_de) {
		err = -ENOMEM;
		goto out;
	}

	err = fill_name_de(sbi, old_de, &old_dentry->d_name, NULL);
	if (err < 0)
		goto out1;

	old_name = (struct ATTR_FILE_NAME *)(old_de + 1);

	if (is_same) {
		new_de = old_de;
	} else {
		new_de = Add2Ptr(old_de, 1024);
		err = fill_name_de(sbi, new_de, &new_dentry->d_name, NULL);
		if (err < 0)
			goto out1;
	}

	ni_lock_dir(old_dir_ni);
	ni_lock(old_ni);

	mi_get_ref(&old_dir_ni->mi, &old_name->home);

	/* Get pointer to file_name in MFT. */
	fname = ni_fname_name(old_ni, (struct cpu_str *)&old_name->name_len,
			      &old_name->home, &le);
	if (!fname) {
		err = -EINVAL;
		goto out2;
	}

	/* Copy fname info from record into new fname. */
	new_name = (struct ATTR_FILE_NAME *)(new_de + 1);
	memcpy(&new_name->dup, &fname->dup, sizeof(fname->dup));

	name_type = paired_name(fname->type);

	/* Remove first name from directory. */
	err = indx_delete_entry(&old_dir_ni->dir, old_dir_ni, old_de + 1,
				le16_to_cpu(old_de->key_size), sbi);
	if (err)
		goto out3;

	/* Remove first name from MFT. */
	err = ni_remove_attr_le(old_ni, attr_from_name(fname), le);
	if (err)
		goto out4;

	le16_add_cpu(&old_ni->mi.mrec->hard_links, -1);
	old_ni->mi.dirty = true;

	if (name_type != FILE_NAME_POSIX) {
		/* Get paired name. */
		fname = ni_fname_type(old_ni, name_type, &le);
		if (fname) {
			/* Remove second name from directory. */
			err = indx_delete_entry(&old_dir_ni->dir, old_dir_ni,
						fname, fname_full_size(fname),
						sbi);
			if (err)
				goto out5;

			/* Remove second name from MFT. */
			err = ni_remove_attr_le(old_ni, attr_from_name(fname),
						le);
			if (err)
				goto out6;

			le16_add_cpu(&old_ni->mi.mrec->hard_links, -1);
			old_ni->mi.dirty = true;
		}
	}

	/* Add new name. */
	mi_get_ref(&old_ni->mi, &new_de->ref);
	mi_get_ref(&ntfs_i(new_dir)->mi, &new_name->home);

	new_de_key_size = le16_to_cpu(new_de->key_size);

	/* Insert new name in MFT. */
	err = ni_insert_resident(old_ni, new_de_key_size, ATTR_NAME, NULL, 0,
				 &attr, NULL);
	if (err)
		goto out7;

	attr->res.flags = RESIDENT_FLAG_INDEXED;

	memcpy(Add2Ptr(attr, SIZEOF_RESIDENT), new_name, new_de_key_size);

	le16_add_cpu(&old_ni->mi.mrec->hard_links, 1);
	old_ni->mi.dirty = true;

	/* Insert new name in directory. */
	err = indx_insert_entry(&new_dir_ni->dir, new_dir_ni, new_de, sbi,
				NULL);
	if (err)
		goto out8;

	if (IS_DIRSYNC(new_dir))
		err = ntfs_sync_inode(old_inode);
	else
		mark_inode_dirty(old_inode);

	old_dir->i_ctime = old_dir->i_mtime = current_time(old_dir);
	if (IS_DIRSYNC(old_dir))
		(void)ntfs_sync_inode(old_dir);
	else
		mark_inode_dirty(old_dir);

	if (old_dir != new_dir) {
		new_dir->i_mtime = new_dir->i_ctime = old_dir->i_ctime;
		mark_inode_dirty(new_dir);
	}

	if (old_inode) {
		old_inode->i_ctime = old_dir->i_ctime;
		mark_inode_dirty(old_inode);
	}

	err = 0;
	/* Normal way* */
	goto out2;

out8:
	/* undo
	 * ni_insert_resident(old_ni, new_de_key_size, ATTR_NAME, NULL, 0,
	 *			 &attr, NULL);
	 */
	mi_remove_attr(&old_ni->mi, attr);
out7:
	/* undo
	 * ni_remove_attr_le(old_ni, attr_from_name(fname), le);
	 */
out6:
	/* undo
	 * indx_delete_entry(&old_dir_ni->dir, old_dir_ni,
	 *					fname, fname_full_size(fname),
	 *					sbi);
	 */
out5:
	/* undo
	 * ni_remove_attr_le(old_ni, attr_from_name(fname), le);
	 */
out4:
	/* undo:
	 * indx_delete_entry(&old_dir_ni->dir, old_dir_ni, old_de + 1,
	 *			old_de->key_size, NULL);
	 */
out3:
out2:
	ni_unlock(old_ni);
	ni_unlock(old_dir_ni);
out1:
	__putname(old_de);
out:
	return err;
}

struct dentry *ntfs3_get_parent(struct dentry *child)
{
	struct inode *inode = d_inode(child);
	struct ntfs_inode *ni = ntfs_i(inode);

	struct ATTR_LIST_ENTRY *le = NULL;
	struct ATTRIB *attr = NULL;
	struct ATTR_FILE_NAME *fname;

	while ((attr = ni_find_attr(ni, attr, &le, ATTR_NAME, NULL, 0, NULL,
				    NULL))) {
		fname = resident_data_ex(attr, SIZEOF_ATTRIBUTE_FILENAME);
		if (!fname)
			continue;

		return d_obtain_alias(
			ntfs_iget5(inode->i_sb, &fname->home, NULL));
	}

	return ERR_PTR(-ENOENT);
}

// clang-format off
const struct inode_operations ntfs_dir_inode_operations = {
	.lookup		= ntfs_lookup,
	.create		= ntfs_create,
	.link		= ntfs_link,
	.unlink		= ntfs_unlink,
	.symlink	= ntfs_symlink,
	.mkdir		= ntfs_mkdir,
	.rmdir		= ntfs_rmdir,
	.mknod		= ntfs_mknod,
	.rename		= ntfs_rename,
	.permission	= ntfs_permission,
	.get_acl	= ntfs_get_acl,
	.set_acl	= ntfs_set_acl,
	.setattr	= ntfs3_setattr,
	.getattr	= ntfs_getattr,
	.listxattr	= ntfs_listxattr,
	.fiemap		= ntfs_fiemap,
};

const struct inode_operations ntfs_special_inode_operations = {
	.setattr	= ntfs3_setattr,
	.getattr	= ntfs_getattr,
	.listxattr	= ntfs_listxattr,
	.get_acl	= ntfs_get_acl,
	.set_acl	= ntfs_set_acl,
};
// clang-format on
