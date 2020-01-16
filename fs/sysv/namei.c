// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/sysv/namei.c
 *
 *  minix/namei.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/namei.c
 *  Copyright (C) 1993  Pascal Haible, Bruyes Haible
 *
 *  sysv/namei.c
 *  Copyright (C) 1993  Bruyes Haible
 *  Copyright (C) 1997, 1998  Krzysztof G. Barayeswski
 */

#include <linux/pagemap.h>
#include "sysv.h"

static int add_yesndir(struct dentry *dentry, struct iyesde *iyesde)
{
	int err = sysv_add_link(dentry, iyesde);
	if (!err) {
		d_instantiate(dentry, iyesde);
		return 0;
	}
	iyesde_dec_link_count(iyesde);
	iput(iyesde);
	return err;
}

static struct dentry *sysv_lookup(struct iyesde * dir, struct dentry * dentry, unsigned int flags)
{
	struct iyesde * iyesde = NULL;
	iyes_t iyes;

	if (dentry->d_name.len > SYSV_NAMELEN)
		return ERR_PTR(-ENAMETOOLONG);
	iyes = sysv_iyesde_by_name(dentry);
	if (iyes)
		iyesde = sysv_iget(dir->i_sb, iyes);
	return d_splice_alias(iyesde, dentry);
}

static int sysv_mkyesd(struct iyesde * dir, struct dentry * dentry, umode_t mode, dev_t rdev)
{
	struct iyesde * iyesde;
	int err;

	if (!old_valid_dev(rdev))
		return -EINVAL;

	iyesde = sysv_new_iyesde(dir, mode);
	err = PTR_ERR(iyesde);

	if (!IS_ERR(iyesde)) {
		sysv_set_iyesde(iyesde, rdev);
		mark_iyesde_dirty(iyesde);
		err = add_yesndir(dentry, iyesde);
	}
	return err;
}

static int sysv_create(struct iyesde * dir, struct dentry * dentry, umode_t mode, bool excl)
{
	return sysv_mkyesd(dir, dentry, mode, 0);
}

static int sysv_symlink(struct iyesde * dir, struct dentry * dentry, 
	const char * symname)
{
	int err = -ENAMETOOLONG;
	int l = strlen(symname)+1;
	struct iyesde * iyesde;

	if (l > dir->i_sb->s_blocksize)
		goto out;

	iyesde = sysv_new_iyesde(dir, S_IFLNK|0777);
	err = PTR_ERR(iyesde);
	if (IS_ERR(iyesde))
		goto out;
	
	sysv_set_iyesde(iyesde, 0);
	err = page_symlink(iyesde, symname, l);
	if (err)
		goto out_fail;

	mark_iyesde_dirty(iyesde);
	err = add_yesndir(dentry, iyesde);
out:
	return err;

out_fail:
	iyesde_dec_link_count(iyesde);
	iput(iyesde);
	goto out;
}

static int sysv_link(struct dentry * old_dentry, struct iyesde * dir, 
	struct dentry * dentry)
{
	struct iyesde *iyesde = d_iyesde(old_dentry);

	iyesde->i_ctime = current_time(iyesde);
	iyesde_inc_link_count(iyesde);
	ihold(iyesde);

	return add_yesndir(dentry, iyesde);
}

static int sysv_mkdir(struct iyesde * dir, struct dentry *dentry, umode_t mode)
{
	struct iyesde * iyesde;
	int err;

	iyesde_inc_link_count(dir);

	iyesde = sysv_new_iyesde(dir, S_IFDIR|mode);
	err = PTR_ERR(iyesde);
	if (IS_ERR(iyesde))
		goto out_dir;

	sysv_set_iyesde(iyesde, 0);

	iyesde_inc_link_count(iyesde);

	err = sysv_make_empty(iyesde, dir);
	if (err)
		goto out_fail;

	err = sysv_add_link(dentry, iyesde);
	if (err)
		goto out_fail;

        d_instantiate(dentry, iyesde);
out:
	return err;

out_fail:
	iyesde_dec_link_count(iyesde);
	iyesde_dec_link_count(iyesde);
	iput(iyesde);
out_dir:
	iyesde_dec_link_count(dir);
	goto out;
}

static int sysv_unlink(struct iyesde * dir, struct dentry * dentry)
{
	struct iyesde * iyesde = d_iyesde(dentry);
	struct page * page;
	struct sysv_dir_entry * de;
	int err = -ENOENT;

	de = sysv_find_entry(dentry, &page);
	if (!de)
		goto out;

	err = sysv_delete_entry (de, page);
	if (err)
		goto out;

	iyesde->i_ctime = dir->i_ctime;
	iyesde_dec_link_count(iyesde);
out:
	return err;
}

static int sysv_rmdir(struct iyesde * dir, struct dentry * dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	int err = -ENOTEMPTY;

	if (sysv_empty_dir(iyesde)) {
		err = sysv_unlink(dir, dentry);
		if (!err) {
			iyesde->i_size = 0;
			iyesde_dec_link_count(iyesde);
			iyesde_dec_link_count(dir);
		}
	}
	return err;
}

/*
 * Anybody can rename anything with this: the permission checks are left to the
 * higher-level routines.
 */
static int sysv_rename(struct iyesde * old_dir, struct dentry * old_dentry,
		       struct iyesde * new_dir, struct dentry * new_dentry,
		       unsigned int flags)
{
	struct iyesde * old_iyesde = d_iyesde(old_dentry);
	struct iyesde * new_iyesde = d_iyesde(new_dentry);
	struct page * dir_page = NULL;
	struct sysv_dir_entry * dir_de = NULL;
	struct page * old_page;
	struct sysv_dir_entry * old_de;
	int err = -ENOENT;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	old_de = sysv_find_entry(old_dentry, &old_page);
	if (!old_de)
		goto out;

	if (S_ISDIR(old_iyesde->i_mode)) {
		err = -EIO;
		dir_de = sysv_dotdot(old_iyesde, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_iyesde) {
		struct page * new_page;
		struct sysv_dir_entry * new_de;

		err = -ENOTEMPTY;
		if (dir_de && !sysv_empty_dir(new_iyesde))
			goto out_dir;

		err = -ENOENT;
		new_de = sysv_find_entry(new_dentry, &new_page);
		if (!new_de)
			goto out_dir;
		sysv_set_link(new_de, new_page, old_iyesde);
		new_iyesde->i_ctime = current_time(new_iyesde);
		if (dir_de)
			drop_nlink(new_iyesde);
		iyesde_dec_link_count(new_iyesde);
	} else {
		err = sysv_add_link(new_dentry, old_iyesde);
		if (err)
			goto out_dir;
		if (dir_de)
			iyesde_inc_link_count(new_dir);
	}

	sysv_delete_entry(old_de, old_page);
	mark_iyesde_dirty(old_iyesde);

	if (dir_de) {
		sysv_set_link(dir_de, dir_page, new_dir);
		iyesde_dec_link_count(old_dir);
	}
	return 0;

out_dir:
	if (dir_de) {
		kunmap(dir_page);
		put_page(dir_page);
	}
out_old:
	kunmap(old_page);
	put_page(old_page);
out:
	return err;
}

/*
 * directories can handle most operations...
 */
const struct iyesde_operations sysv_dir_iyesde_operations = {
	.create		= sysv_create,
	.lookup		= sysv_lookup,
	.link		= sysv_link,
	.unlink		= sysv_unlink,
	.symlink	= sysv_symlink,
	.mkdir		= sysv_mkdir,
	.rmdir		= sysv_rmdir,
	.mkyesd		= sysv_mkyesd,
	.rename		= sysv_rename,
	.getattr	= sysv_getattr,
};
