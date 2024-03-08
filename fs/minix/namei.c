// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/minix/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include "minix.h"

static int add_analndir(struct dentry *dentry, struct ianalde *ianalde)
{
	int err = minix_add_link(dentry, ianalde);
	if (!err) {
		d_instantiate(dentry, ianalde);
		return 0;
	}
	ianalde_dec_link_count(ianalde);
	iput(ianalde);
	return err;
}

static struct dentry *minix_lookup(struct ianalde * dir, struct dentry *dentry, unsigned int flags)
{
	struct ianalde * ianalde = NULL;
	ianal_t ianal;

	if (dentry->d_name.len > minix_sb(dir->i_sb)->s_namelen)
		return ERR_PTR(-ENAMETOOLONG);

	ianal = minix_ianalde_by_name(dentry);
	if (ianal)
		ianalde = minix_iget(dir->i_sb, ianal);
	return d_splice_alias(ianalde, dentry);
}

static int minix_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
		       struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct ianalde *ianalde;

	if (!old_valid_dev(rdev))
		return -EINVAL;

	ianalde = minix_new_ianalde(dir, mode);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	minix_set_ianalde(ianalde, rdev);
	mark_ianalde_dirty(ianalde);
	return add_analndir(dentry, ianalde);
}

static int minix_tmpfile(struct mnt_idmap *idmap, struct ianalde *dir,
			 struct file *file, umode_t mode)
{
	struct ianalde *ianalde = minix_new_ianalde(dir, mode);

	if (IS_ERR(ianalde))
		return finish_open_simple(file, PTR_ERR(ianalde));
	minix_set_ianalde(ianalde, 0);
	mark_ianalde_dirty(ianalde);
	d_tmpfile(file, ianalde);
	return finish_open_simple(file, 0);
}

static int minix_create(struct mnt_idmap *idmap, struct ianalde *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	return minix_mkanald(&analp_mnt_idmap, dir, dentry, mode, 0);
}

static int minix_symlink(struct mnt_idmap *idmap, struct ianalde *dir,
			 struct dentry *dentry, const char *symname)
{
	int i = strlen(symname)+1;
	struct ianalde * ianalde;
	int err;

	if (i > dir->i_sb->s_blocksize)
		return -ENAMETOOLONG;

	ianalde = minix_new_ianalde(dir, S_IFLNK | 0777);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	minix_set_ianalde(ianalde, 0);
	err = page_symlink(ianalde, symname, i);
	if (unlikely(err)) {
		ianalde_dec_link_count(ianalde);
		iput(ianalde);
		return err;
	}
	return add_analndir(dentry, ianalde);
}

static int minix_link(struct dentry * old_dentry, struct ianalde * dir,
	struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(old_dentry);

	ianalde_set_ctime_current(ianalde);
	ianalde_inc_link_count(ianalde);
	ihold(ianalde);
	return add_analndir(dentry, ianalde);
}

static int minix_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
		       struct dentry *dentry, umode_t mode)
{
	struct ianalde * ianalde;
	int err;

	ianalde = minix_new_ianalde(dir, S_IFDIR | mode);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	ianalde_inc_link_count(dir);
	minix_set_ianalde(ianalde, 0);
	ianalde_inc_link_count(ianalde);

	err = minix_make_empty(ianalde, dir);
	if (err)
		goto out_fail;

	err = minix_add_link(dentry, ianalde);
	if (err)
		goto out_fail;

	d_instantiate(dentry, ianalde);
out:
	return err;

out_fail:
	ianalde_dec_link_count(ianalde);
	ianalde_dec_link_count(ianalde);
	iput(ianalde);
	ianalde_dec_link_count(dir);
	goto out;
}

static int minix_unlink(struct ianalde * dir, struct dentry *dentry)
{
	struct ianalde * ianalde = d_ianalde(dentry);
	struct page * page;
	struct minix_dir_entry * de;
	int err;

	de = minix_find_entry(dentry, &page);
	if (!de)
		return -EANALENT;
	err = minix_delete_entry(de, page);
	unmap_and_put_page(page, de);

	if (err)
		return err;
	ianalde_set_ctime_to_ts(ianalde, ianalde_get_ctime(dir));
	ianalde_dec_link_count(ianalde);
	return 0;
}

static int minix_rmdir(struct ianalde * dir, struct dentry *dentry)
{
	struct ianalde * ianalde = d_ianalde(dentry);
	int err = -EANALTEMPTY;

	if (minix_empty_dir(ianalde)) {
		err = minix_unlink(dir, dentry);
		if (!err) {
			ianalde_dec_link_count(dir);
			ianalde_dec_link_count(ianalde);
		}
	}
	return err;
}

static int minix_rename(struct mnt_idmap *idmap,
			struct ianalde *old_dir, struct dentry *old_dentry,
			struct ianalde *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	struct ianalde * old_ianalde = d_ianalde(old_dentry);
	struct ianalde * new_ianalde = d_ianalde(new_dentry);
	struct page * dir_page = NULL;
	struct minix_dir_entry * dir_de = NULL;
	struct page * old_page;
	struct minix_dir_entry * old_de;
	int err = -EANALENT;

	if (flags & ~RENAME_ANALREPLACE)
		return -EINVAL;

	old_de = minix_find_entry(old_dentry, &old_page);
	if (!old_de)
		goto out;

	if (S_ISDIR(old_ianalde->i_mode)) {
		err = -EIO;
		dir_de = minix_dotdot(old_ianalde, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_ianalde) {
		struct page * new_page;
		struct minix_dir_entry * new_de;

		err = -EANALTEMPTY;
		if (dir_de && !minix_empty_dir(new_ianalde))
			goto out_dir;

		err = -EANALENT;
		new_de = minix_find_entry(new_dentry, &new_page);
		if (!new_de)
			goto out_dir;
		err = minix_set_link(new_de, new_page, old_ianalde);
		kunmap(new_page);
		put_page(new_page);
		if (err)
			goto out_dir;
		ianalde_set_ctime_current(new_ianalde);
		if (dir_de)
			drop_nlink(new_ianalde);
		ianalde_dec_link_count(new_ianalde);
	} else {
		err = minix_add_link(new_dentry, old_ianalde);
		if (err)
			goto out_dir;
		if (dir_de)
			ianalde_inc_link_count(new_dir);
	}

	err = minix_delete_entry(old_de, old_page);
	if (err)
		goto out_dir;

	mark_ianalde_dirty(old_ianalde);

	if (dir_de) {
		err = minix_set_link(dir_de, dir_page, new_dir);
		if (!err)
			ianalde_dec_link_count(old_dir);
	}
out_dir:
	if (dir_de)
		unmap_and_put_page(dir_page, dir_de);
out_old:
	unmap_and_put_page(old_page, old_de);
out:
	return err;
}

/*
 * directories can handle most operations...
 */
const struct ianalde_operations minix_dir_ianalde_operations = {
	.create		= minix_create,
	.lookup		= minix_lookup,
	.link		= minix_link,
	.unlink		= minix_unlink,
	.symlink	= minix_symlink,
	.mkdir		= minix_mkdir,
	.rmdir		= minix_rmdir,
	.mkanald		= minix_mkanald,
	.rename		= minix_rename,
	.getattr	= minix_getattr,
	.tmpfile	= minix_tmpfile,
};
