// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2012 Jeremy Kerr <jeremy.kerr@canonical.com>
 */

#include <linux/efi.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/kmemleak.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include <linux/fileattr.h>

#include "internal.h"

static const struct inode_operations efivarfs_file_inode_operations;

struct inode *efivarfs_get_inode(struct super_block *sb,
				const struct inode *dir, int mode,
				dev_t dev, bool is_removable)
{
	struct inode *inode = new_inode(sb);

	if (inode) {
		inode->i_ino = get_next_ino();
		inode->i_mode = mode;
		inode->i_atime = inode->i_mtime = inode_set_ctime_current(inode);
		inode->i_flags = is_removable ? 0 : S_IMMUTABLE;
		switch (mode & S_IFMT) {
		case S_IFREG:
			inode->i_op = &efivarfs_file_inode_operations;
			inode->i_fop = &efivarfs_file_operations;
			break;
		case S_IFDIR:
			inode->i_op = &efivarfs_dir_inode_operations;
			inode->i_fop = &simple_dir_operations;
			inc_nlink(inode);
			break;
		}
	}
	return inode;
}

/*
 * Return true if 'str' is a valid efivarfs filename of the form,
 *
 *	VariableName-12345678-1234-1234-1234-1234567891bc
 */
bool efivarfs_valid_name(const char *str, int len)
{
	const char *s = str + len - EFI_VARIABLE_GUID_LEN;

	/*
	 * We need a GUID, plus at least one letter for the variable name,
	 * plus the '-' separator
	 */
	if (len < EFI_VARIABLE_GUID_LEN + 2)
		return false;

	/* GUID must be preceded by a '-' */
	if (*(s - 1) != '-')
		return false;

	/*
	 * Validate that 's' is of the correct format, e.g.
	 *
	 *	12345678-1234-1234-1234-123456789abc
	 */
	return uuid_is_valid(s);
}

static int efivarfs_create(struct mnt_idmap *idmap, struct inode *dir,
			   struct dentry *dentry, umode_t mode, bool excl)
{
	struct inode *inode = NULL;
	struct efivar_entry *var;
	int namelen, i = 0, err = 0;
	bool is_removable = false;

	if (!efivarfs_valid_name(dentry->d_name.name, dentry->d_name.len))
		return -EINVAL;

	var = kzalloc(sizeof(struct efivar_entry), GFP_KERNEL);
	if (!var)
		return -ENOMEM;

	/* length of the variable name itself: remove GUID and separator */
	namelen = dentry->d_name.len - EFI_VARIABLE_GUID_LEN - 1;

	err = guid_parse(dentry->d_name.name + namelen + 1, &var->var.VendorGuid);
	if (err)
		goto out;
	if (guid_equal(&var->var.VendorGuid, &LINUX_EFI_RANDOM_SEED_TABLE_GUID)) {
		err = -EPERM;
		goto out;
	}

	if (efivar_variable_is_removable(var->var.VendorGuid,
					 dentry->d_name.name, namelen))
		is_removable = true;

	inode = efivarfs_get_inode(dir->i_sb, dir, mode, 0, is_removable);
	if (!inode) {
		err = -ENOMEM;
		goto out;
	}

	for (i = 0; i < namelen; i++)
		var->var.VariableName[i] = dentry->d_name.name[i];

	var->var.VariableName[i] = '\0';

	inode->i_private = var;
	kmemleak_ignore(var);

	err = efivar_entry_add(var, &efivarfs_list);
	if (err)
		goto out;

	d_instantiate(dentry, inode);
	dget(dentry);
out:
	if (err) {
		kfree(var);
		if (inode)
			iput(inode);
	}
	return err;
}

static int efivarfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct efivar_entry *var = d_inode(dentry)->i_private;

	if (efivar_entry_delete(var))
		return -EINVAL;

	drop_nlink(d_inode(dentry));
	dput(dentry);
	return 0;
};

const struct inode_operations efivarfs_dir_inode_operations = {
	.lookup = simple_lookup,
	.unlink = efivarfs_unlink,
	.create = efivarfs_create,
};

static int
efivarfs_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	unsigned int i_flags;
	unsigned int flags = 0;

	i_flags = d_inode(dentry)->i_flags;
	if (i_flags & S_IMMUTABLE)
		flags |= FS_IMMUTABLE_FL;

	fileattr_fill_flags(fa, flags);

	return 0;
}

static int
efivarfs_fileattr_set(struct mnt_idmap *idmap,
		      struct dentry *dentry, struct fileattr *fa)
{
	unsigned int i_flags = 0;

	if (fileattr_has_fsx(fa))
		return -EOPNOTSUPP;

	if (fa->flags & ~FS_IMMUTABLE_FL)
		return -EOPNOTSUPP;

	if (fa->flags & FS_IMMUTABLE_FL)
		i_flags |= S_IMMUTABLE;

	inode_set_flags(d_inode(dentry), i_flags, S_IMMUTABLE);

	return 0;
}

static const struct inode_operations efivarfs_file_inode_operations = {
	.fileattr_get = efivarfs_fileattr_get,
	.fileattr_set = efivarfs_fileattr_set,
};
