// SPDX-License-Identifier: GPL-2.0
/*
 *
 * Copyright (C) 2019-2021 Paragon Software GmbH, All rights reserved.
 *
 */

#include <linux/fs.h>
#include <linux/nls.h>
#include <linux/ctype.h>

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
	struct inode *inode;

	inode = ntfs_create_inode(mnt_userns, dir, dentry, NULL, S_IFREG | mode,
				  0, NULL, 0, NULL);

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
	struct inode *inode;

	inode = ntfs_create_inode(mnt_userns, dir, dentry, NULL, mode, rdev,
				  NULL, 0, NULL);

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

	inc_nlink(inode);
	ihold(inode);

	err = ntfs_link_inode(inode, de);

	if (!err) {
		dir->i_ctime = dir->i_mtime = inode->i_ctime =
			current_time(dir);
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

	inode = ntfs_create_inode(mnt_userns, dir, dentry, NULL, S_IFLNK | 0777,
				  0, symname, size, NULL);

	return IS_ERR(inode) ? PTR_ERR(inode) : 0;
}

/*
 * ntfs_mkdir- inode_operations::mkdir
 */
static int ntfs_mkdir(struct user_namespace *mnt_userns, struct inode *dir,
		      struct dentry *dentry, umode_t mode)
{
	struct inode *inode;

	inode = ntfs_create_inode(mnt_userns, dir, dentry, NULL, S_IFDIR | mode,
				  0, NULL, 0, NULL);

	return IS_ERR(inode) ? PTR_ERR(inode) : 0;
}

/*
 * ntfs_rmdir - inode_operations::rmdir
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
static int ntfs_rename(struct user_namespace *mnt_userns, struct inode *dir,
		       struct dentry *dentry, struct inode *new_dir,
		       struct dentry *new_dentry, u32 flags)
{
	int err;
	struct super_block *sb = dir->i_sb;
	struct ntfs_sb_info *sbi = sb->s_fs_info;
	struct ntfs_inode *dir_ni = ntfs_i(dir);
	struct ntfs_inode *new_dir_ni = ntfs_i(new_dir);
	struct inode *inode = d_inode(dentry);
	struct ntfs_inode *ni = ntfs_i(inode);
	struct inode *new_inode = d_inode(new_dentry);
	struct NTFS_DE *de, *new_de;
	bool is_same, is_bad;
	/*
	 * de		- memory of PATH_MAX bytes:
	 * [0-1024)	- original name (dentry->d_name)
	 * [1024-2048)	- paired to original name, usually DOS variant of dentry->d_name
	 * [2048-3072)	- new name (new_dentry->d_name)
	 */
	static_assert(SIZEOF_ATTRIBUTE_FILENAME_MAX + SIZEOF_RESIDENT < 1024);
	static_assert(SIZEOF_ATTRIBUTE_FILENAME_MAX + sizeof(struct NTFS_DE) <
		      1024);
	static_assert(PATH_MAX >= 4 * 1024);

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	is_same = dentry->d_name.len == new_dentry->d_name.len &&
		  !memcmp(dentry->d_name.name, new_dentry->d_name.name,
			  dentry->d_name.len);

	if (is_same && dir == new_dir) {
		/* Nothing to do. */
		return 0;
	}

	if (ntfs_is_meta_file(sbi, inode->i_ino)) {
		/* Should we print an error? */
		return -EINVAL;
	}

	if (new_inode) {
		/* Target name exists. Unlink it. */
		dget(new_dentry);
		ni_lock_dir(new_dir_ni);
		err = ntfs_unlink_inode(new_dir, new_dentry);
		ni_unlock(new_dir_ni);
		dput(new_dentry);
		if (err)
			return err;
	}

	/* Allocate PATH_MAX bytes. */
	de = __getname();
	if (!de)
		return -ENOMEM;

	/* Translate dentry->d_name into unicode form. */
	err = fill_name_de(sbi, de, &dentry->d_name, NULL);
	if (err < 0)
		goto out;

	if (is_same) {
		/* Reuse 'de'. */
		new_de = de;
	} else {
		/* Translate new_dentry->d_name into unicode form. */
		new_de = Add2Ptr(de, 2048);
		err = fill_name_de(sbi, new_de, &new_dentry->d_name, NULL);
		if (err < 0)
			goto out;
	}

	ni_lock_dir(dir_ni);
	ni_lock(ni);

	is_bad = false;
	err = ni_rename(dir_ni, new_dir_ni, ni, de, new_de, &is_bad);
	if (is_bad) {
		/* Restore after failed rename failed too. */
		_ntfs_bad_inode(inode);
	} else if (!err) {
		inode->i_ctime = dir->i_ctime = dir->i_mtime =
			current_time(dir);
		mark_inode_dirty(inode);
		mark_inode_dirty(dir);
		if (dir != new_dir) {
			new_dir->i_mtime = new_dir->i_ctime = dir->i_ctime;
			mark_inode_dirty(new_dir);
		}

		if (IS_DIRSYNC(dir))
			ntfs_sync_inode(dir);

		if (IS_DIRSYNC(new_dir))
			ntfs_sync_inode(inode);
	}

	ni_unlock(ni);
	ni_unlock(dir_ni);
out:
	__putname(de);
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

/*
 * dentry_operations::d_hash
 */
static int ntfs_d_hash(const struct dentry *dentry, struct qstr *name)
{
	struct ntfs_sb_info *sbi;
	const char *n = name->name;
	unsigned int len = name->len;
	unsigned long hash;
	struct cpu_str *uni;
	unsigned int c;
	int err;

	/* First try fast implementation. */
	hash = init_name_hash(dentry);

	for (;;) {
		if (!len--) {
			name->hash = end_name_hash(hash);
			return 0;
		}

		c = *n++;
		if (c >= 0x80)
			break;

		hash = partial_name_hash(toupper(c), hash);
	}

	/*
	 * Try slow way with current upcase table
	 */
	uni = __getname();
	if (!uni)
		return -ENOMEM;

	sbi = dentry->d_sb->s_fs_info;

	err = ntfs_nls_to_utf16(sbi, name->name, name->len, uni, NTFS_NAME_LEN,
				UTF16_HOST_ENDIAN);
	if (err < 0)
		goto out;

	if (!err) {
		err = -EINVAL;
		goto out;
	}

	hash = ntfs_names_hash(uni->name, uni->len, sbi->upcase,
			       init_name_hash(dentry));
	name->hash = end_name_hash(hash);
	err = 0;

out:
	__putname(uni);
	return err;
}

/*
 * dentry_operations::d_compare
 */
static int ntfs_d_compare(const struct dentry *dentry, unsigned int len1,
			  const char *str, const struct qstr *name)
{
	struct ntfs_sb_info *sbi;
	int ret;
	const char *n1 = str;
	const char *n2 = name->name;
	unsigned int len2 = name->len;
	unsigned int lm = min(len1, len2);
	unsigned char c1, c2;
	struct cpu_str *uni1, *uni2;

	/* First try fast implementation. */
	for (;;) {
		if (!lm--) {
			ret = len1 == len2 ? 0 : 1;
			goto out;
		}

		if ((c1 = *n1++) == (c2 = *n2++))
			continue;

		if (c1 >= 0x80 || c2 >= 0x80)
			break;

		if (toupper(c1) != toupper(c2)) {
			ret = 1;
			goto out;
		}
	}

	/*
	 * Try slow way with current upcase table
	 */
	sbi = dentry->d_sb->s_fs_info;
	uni1 = __getname();
	if (!uni1)
		return -ENOMEM;

	ret = ntfs_nls_to_utf16(sbi, str, len1, uni1, NTFS_NAME_LEN,
				UTF16_HOST_ENDIAN);
	if (ret < 0)
		goto out;

	if (!ret) {
		ret = -EINVAL;
		goto out;
	}

	uni2 = Add2Ptr(uni1, 2048);

	ret = ntfs_nls_to_utf16(sbi, name->name, name->len, uni2, NTFS_NAME_LEN,
				UTF16_HOST_ENDIAN);
	if (ret < 0)
		goto out;

	if (!ret) {
		ret = -EINVAL;
		goto out;
	}

	ret = !ntfs_cmp_names(uni1->name, uni1->len, uni2->name, uni2->len,
			      sbi->upcase, false)
		      ? 0
		      : 1;

out:
	__putname(uni1);
	return ret;
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

const struct dentry_operations ntfs_dentry_ops = {
	.d_hash		= ntfs_d_hash,
	.d_compare	= ntfs_d_compare,
};

// clang-format on
