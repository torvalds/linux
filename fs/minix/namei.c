// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/minix/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include "minix.h"

static int add_yesndir(struct dentry *dentry, struct iyesde *iyesde)
{
	int err = minix_add_link(dentry, iyesde);
	if (!err) {
		d_instantiate(dentry, iyesde);
		return 0;
	}
	iyesde_dec_link_count(iyesde);
	iput(iyesde);
	return err;
}

static struct dentry *minix_lookup(struct iyesde * dir, struct dentry *dentry, unsigned int flags)
{
	struct iyesde * iyesde = NULL;
	iyes_t iyes;

	if (dentry->d_name.len > minix_sb(dir->i_sb)->s_namelen)
		return ERR_PTR(-ENAMETOOLONG);

	iyes = minix_iyesde_by_name(dentry);
	if (iyes)
		iyesde = minix_iget(dir->i_sb, iyes);
	return d_splice_alias(iyesde, dentry);
}

static int minix_mkyesd(struct iyesde * dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	int error;
	struct iyesde *iyesde;

	if (!old_valid_dev(rdev))
		return -EINVAL;

	iyesde = minix_new_iyesde(dir, mode, &error);

	if (iyesde) {
		minix_set_iyesde(iyesde, rdev);
		mark_iyesde_dirty(iyesde);
		error = add_yesndir(dentry, iyesde);
	}
	return error;
}

static int minix_tmpfile(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	int error;
	struct iyesde *iyesde = minix_new_iyesde(dir, mode, &error);
	if (iyesde) {
		minix_set_iyesde(iyesde, 0);
		mark_iyesde_dirty(iyesde);
		d_tmpfile(dentry, iyesde);
	}
	return error;
}

static int minix_create(struct iyesde *dir, struct dentry *dentry, umode_t mode,
		bool excl)
{
	return minix_mkyesd(dir, dentry, mode, 0);
}

static int minix_symlink(struct iyesde * dir, struct dentry *dentry,
	  const char * symname)
{
	int err = -ENAMETOOLONG;
	int i = strlen(symname)+1;
	struct iyesde * iyesde;

	if (i > dir->i_sb->s_blocksize)
		goto out;

	iyesde = minix_new_iyesde(dir, S_IFLNK | 0777, &err);
	if (!iyesde)
		goto out;

	minix_set_iyesde(iyesde, 0);
	err = page_symlink(iyesde, symname, i);
	if (err)
		goto out_fail;

	err = add_yesndir(dentry, iyesde);
out:
	return err;

out_fail:
	iyesde_dec_link_count(iyesde);
	iput(iyesde);
	goto out;
}

static int minix_link(struct dentry * old_dentry, struct iyesde * dir,
	struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(old_dentry);

	iyesde->i_ctime = current_time(iyesde);
	iyesde_inc_link_count(iyesde);
	ihold(iyesde);
	return add_yesndir(dentry, iyesde);
}

static int minix_mkdir(struct iyesde * dir, struct dentry *dentry, umode_t mode)
{
	struct iyesde * iyesde;
	int err;

	iyesde_inc_link_count(dir);

	iyesde = minix_new_iyesde(dir, S_IFDIR | mode, &err);
	if (!iyesde)
		goto out_dir;

	minix_set_iyesde(iyesde, 0);

	iyesde_inc_link_count(iyesde);

	err = minix_make_empty(iyesde, dir);
	if (err)
		goto out_fail;

	err = minix_add_link(dentry, iyesde);
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

static int minix_unlink(struct iyesde * dir, struct dentry *dentry)
{
	int err = -ENOENT;
	struct iyesde * iyesde = d_iyesde(dentry);
	struct page * page;
	struct minix_dir_entry * de;

	de = minix_find_entry(dentry, &page);
	if (!de)
		goto end_unlink;

	err = minix_delete_entry(de, page);
	if (err)
		goto end_unlink;

	iyesde->i_ctime = dir->i_ctime;
	iyesde_dec_link_count(iyesde);
end_unlink:
	return err;
}

static int minix_rmdir(struct iyesde * dir, struct dentry *dentry)
{
	struct iyesde * iyesde = d_iyesde(dentry);
	int err = -ENOTEMPTY;

	if (minix_empty_dir(iyesde)) {
		err = minix_unlink(dir, dentry);
		if (!err) {
			iyesde_dec_link_count(dir);
			iyesde_dec_link_count(iyesde);
		}
	}
	return err;
}

static int minix_rename(struct iyesde * old_dir, struct dentry *old_dentry,
			struct iyesde * new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	struct iyesde * old_iyesde = d_iyesde(old_dentry);
	struct iyesde * new_iyesde = d_iyesde(new_dentry);
	struct page * dir_page = NULL;
	struct minix_dir_entry * dir_de = NULL;
	struct page * old_page;
	struct minix_dir_entry * old_de;
	int err = -ENOENT;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	old_de = minix_find_entry(old_dentry, &old_page);
	if (!old_de)
		goto out;

	if (S_ISDIR(old_iyesde->i_mode)) {
		err = -EIO;
		dir_de = minix_dotdot(old_iyesde, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_iyesde) {
		struct page * new_page;
		struct minix_dir_entry * new_de;

		err = -ENOTEMPTY;
		if (dir_de && !minix_empty_dir(new_iyesde))
			goto out_dir;

		err = -ENOENT;
		new_de = minix_find_entry(new_dentry, &new_page);
		if (!new_de)
			goto out_dir;
		minix_set_link(new_de, new_page, old_iyesde);
		new_iyesde->i_ctime = current_time(new_iyesde);
		if (dir_de)
			drop_nlink(new_iyesde);
		iyesde_dec_link_count(new_iyesde);
	} else {
		err = minix_add_link(new_dentry, old_iyesde);
		if (err)
			goto out_dir;
		if (dir_de)
			iyesde_inc_link_count(new_dir);
	}

	minix_delete_entry(old_de, old_page);
	mark_iyesde_dirty(old_iyesde);

	if (dir_de) {
		minix_set_link(dir_de, dir_page, new_dir);
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
const struct iyesde_operations minix_dir_iyesde_operations = {
	.create		= minix_create,
	.lookup		= minix_lookup,
	.link		= minix_link,
	.unlink		= minix_unlink,
	.symlink	= minix_symlink,
	.mkdir		= minix_mkdir,
	.rmdir		= minix_rmdir,
	.mkyesd		= minix_mkyesd,
	.rename		= minix_rename,
	.getattr	= minix_getattr,
	.tmpfile	= minix_tmpfile,
};
