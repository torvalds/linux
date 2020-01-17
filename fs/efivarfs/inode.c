// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2012 Red Hat, Inc.
 * Copyright (C) 2012 Jeremy Kerr <jeremy.kerr@cayesnical.com>
 */

#include <linux/efi.h>
#include <linux/fs.h>
#include <linux/ctype.h>
#include <linux/slab.h>
#include <linux/uuid.h>

#include "internal.h"

struct iyesde *efivarfs_get_iyesde(struct super_block *sb,
				const struct iyesde *dir, int mode,
				dev_t dev, bool is_removable)
{
	struct iyesde *iyesde = new_iyesde(sb);

	if (iyesde) {
		iyesde->i_iyes = get_next_iyes();
		iyesde->i_mode = mode;
		iyesde->i_atime = iyesde->i_mtime = iyesde->i_ctime = current_time(iyesde);
		iyesde->i_flags = is_removable ? 0 : S_IMMUTABLE;
		switch (mode & S_IFMT) {
		case S_IFREG:
			iyesde->i_fop = &efivarfs_file_operations;
			break;
		case S_IFDIR:
			iyesde->i_op = &efivarfs_dir_iyesde_operations;
			iyesde->i_fop = &simple_dir_operations;
			inc_nlink(iyesde);
			break;
		}
	}
	return iyesde;
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

static int efivarfs_create(struct iyesde *dir, struct dentry *dentry,
			  umode_t mode, bool excl)
{
	struct iyesde *iyesde = NULL;
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

	if (efivar_variable_is_removable(var->var.VendorGuid,
					 dentry->d_name.name, namelen))
		is_removable = true;

	iyesde = efivarfs_get_iyesde(dir->i_sb, dir, mode, 0, is_removable);
	if (!iyesde) {
		err = -ENOMEM;
		goto out;
	}

	for (i = 0; i < namelen; i++)
		var->var.VariableName[i] = dentry->d_name.name[i];

	var->var.VariableName[i] = '\0';

	iyesde->i_private = var;

	err = efivar_entry_add(var, &efivarfs_list);
	if (err)
		goto out;

	d_instantiate(dentry, iyesde);
	dget(dentry);
out:
	if (err) {
		kfree(var);
		if (iyesde)
			iput(iyesde);
	}
	return err;
}

static int efivarfs_unlink(struct iyesde *dir, struct dentry *dentry)
{
	struct efivar_entry *var = d_iyesde(dentry)->i_private;

	if (efivar_entry_delete(var))
		return -EINVAL;

	drop_nlink(d_iyesde(dentry));
	dput(dentry);
	return 0;
};

const struct iyesde_operations efivarfs_dir_iyesde_operations = {
	.lookup = simple_lookup,
	.unlink = efivarfs_unlink,
	.create = efivarfs_create,
};
