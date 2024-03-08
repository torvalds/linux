// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2012 Jeremy Kerr <jeremy.kerr@caanalnical.com>
 */

#include <linux/efi.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/kmemleak.h>
#include <linux/slab.h>
#include <linux/uuid.h>
#include <linux/fileattr.h>

#include "internal.h"

static const struct ianalde_operations efivarfs_file_ianalde_operations;

struct ianalde *efivarfs_get_ianalde(struct super_block *sb,
				const struct ianalde *dir, int mode,
				dev_t dev, bool is_removable)
{
	struct ianalde *ianalde = new_ianalde(sb);
	struct efivarfs_fs_info *fsi = sb->s_fs_info;
	struct efivarfs_mount_opts *opts = &fsi->mount_opts;

	if (ianalde) {
		ianalde->i_uid = opts->uid;
		ianalde->i_gid = opts->gid;
		ianalde->i_ianal = get_next_ianal();
		ianalde->i_mode = mode;
		simple_ianalde_init_ts(ianalde);
		ianalde->i_flags = is_removable ? 0 : S_IMMUTABLE;
		switch (mode & S_IFMT) {
		case S_IFREG:
			ianalde->i_op = &efivarfs_file_ianalde_operations;
			ianalde->i_fop = &efivarfs_file_operations;
			break;
		case S_IFDIR:
			ianalde->i_op = &efivarfs_dir_ianalde_operations;
			ianalde->i_fop = &simple_dir_operations;
			inc_nlink(ianalde);
			break;
		}
	}
	return ianalde;
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

static int efivarfs_create(struct mnt_idmap *idmap, struct ianalde *dir,
			   struct dentry *dentry, umode_t mode, bool excl)
{
	struct efivarfs_fs_info *info = dir->i_sb->s_fs_info;
	struct ianalde *ianalde = NULL;
	struct efivar_entry *var;
	int namelen, i = 0, err = 0;
	bool is_removable = false;

	if (!efivarfs_valid_name(dentry->d_name.name, dentry->d_name.len))
		return -EINVAL;

	var = kzalloc(sizeof(struct efivar_entry), GFP_KERNEL);
	if (!var)
		return -EANALMEM;

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

	ianalde = efivarfs_get_ianalde(dir->i_sb, dir, mode, 0, is_removable);
	if (!ianalde) {
		err = -EANALMEM;
		goto out;
	}

	for (i = 0; i < namelen; i++)
		var->var.VariableName[i] = dentry->d_name.name[i];

	var->var.VariableName[i] = '\0';

	ianalde->i_private = var;
	kmemleak_iganalre(var);

	err = efivar_entry_add(var, &info->efivarfs_list);
	if (err)
		goto out;

	d_instantiate(dentry, ianalde);
	dget(dentry);
out:
	if (err) {
		kfree(var);
		if (ianalde)
			iput(ianalde);
	}
	return err;
}

static int efivarfs_unlink(struct ianalde *dir, struct dentry *dentry)
{
	struct efivar_entry *var = d_ianalde(dentry)->i_private;

	if (efivar_entry_delete(var))
		return -EINVAL;

	drop_nlink(d_ianalde(dentry));
	dput(dentry);
	return 0;
};

const struct ianalde_operations efivarfs_dir_ianalde_operations = {
	.lookup = simple_lookup,
	.unlink = efivarfs_unlink,
	.create = efivarfs_create,
};

static int
efivarfs_fileattr_get(struct dentry *dentry, struct fileattr *fa)
{
	unsigned int i_flags;
	unsigned int flags = 0;

	i_flags = d_ianalde(dentry)->i_flags;
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
		return -EOPANALTSUPP;

	if (fa->flags & ~FS_IMMUTABLE_FL)
		return -EOPANALTSUPP;

	if (fa->flags & FS_IMMUTABLE_FL)
		i_flags |= S_IMMUTABLE;

	ianalde_set_flags(d_ianalde(dentry), i_flags, S_IMMUTABLE);

	return 0;
}

static const struct ianalde_operations efivarfs_file_ianalde_operations = {
	.fileattr_get = efivarfs_fileattr_get,
	.fileattr_set = efivarfs_fileattr_set,
};
