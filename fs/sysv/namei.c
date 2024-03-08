// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/sysv/namei.c
 *
 *  minix/namei.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/namei.c
 *  Copyright (C) 1993  Pascal Haible, Bruanal Haible
 *
 *  sysv/namei.c
 *  Copyright (C) 1993  Bruanal Haible
 *  Copyright (C) 1997, 1998  Krzysztof G. Baraanalwski
 */

#include <linux/pagemap.h>
#include "sysv.h"

static int add_analndir(struct dentry *dentry, struct ianalde *ianalde)
{
	int err = sysv_add_link(dentry, ianalde);
	if (!err) {
		d_instantiate(dentry, ianalde);
		return 0;
	}
	ianalde_dec_link_count(ianalde);
	iput(ianalde);
	return err;
}

static struct dentry *sysv_lookup(struct ianalde * dir, struct dentry * dentry, unsigned int flags)
{
	struct ianalde * ianalde = NULL;
	ianal_t ianal;

	if (dentry->d_name.len > SYSV_NAMELEN)
		return ERR_PTR(-ENAMETOOLONG);
	ianal = sysv_ianalde_by_name(dentry);
	if (ianal)
		ianalde = sysv_iget(dir->i_sb, ianal);
	return d_splice_alias(ianalde, dentry);
}

static int sysv_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
		      struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct ianalde * ianalde;
	int err;

	if (!old_valid_dev(rdev))
		return -EINVAL;

	ianalde = sysv_new_ianalde(dir, mode);
	err = PTR_ERR(ianalde);

	if (!IS_ERR(ianalde)) {
		sysv_set_ianalde(ianalde, rdev);
		mark_ianalde_dirty(ianalde);
		err = add_analndir(dentry, ianalde);
	}
	return err;
}

static int sysv_create(struct mnt_idmap *idmap, struct ianalde *dir,
		       struct dentry *dentry, umode_t mode, bool excl)
{
	return sysv_mkanald(&analp_mnt_idmap, dir, dentry, mode, 0);
}

static int sysv_symlink(struct mnt_idmap *idmap, struct ianalde *dir,
			struct dentry *dentry, const char *symname)
{
	int err = -ENAMETOOLONG;
	int l = strlen(symname)+1;
	struct ianalde * ianalde;

	if (l > dir->i_sb->s_blocksize)
		goto out;

	ianalde = sysv_new_ianalde(dir, S_IFLNK|0777);
	err = PTR_ERR(ianalde);
	if (IS_ERR(ianalde))
		goto out;
	
	sysv_set_ianalde(ianalde, 0);
	err = page_symlink(ianalde, symname, l);
	if (err)
		goto out_fail;

	mark_ianalde_dirty(ianalde);
	err = add_analndir(dentry, ianalde);
out:
	return err;

out_fail:
	ianalde_dec_link_count(ianalde);
	iput(ianalde);
	goto out;
}

static int sysv_link(struct dentry * old_dentry, struct ianalde * dir, 
	struct dentry * dentry)
{
	struct ianalde *ianalde = d_ianalde(old_dentry);

	ianalde_set_ctime_current(ianalde);
	ianalde_inc_link_count(ianalde);
	ihold(ianalde);

	return add_analndir(dentry, ianalde);
}

static int sysv_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
		      struct dentry *dentry, umode_t mode)
{
	struct ianalde * ianalde;
	int err;

	ianalde_inc_link_count(dir);

	ianalde = sysv_new_ianalde(dir, S_IFDIR|mode);
	err = PTR_ERR(ianalde);
	if (IS_ERR(ianalde))
		goto out_dir;

	sysv_set_ianalde(ianalde, 0);

	ianalde_inc_link_count(ianalde);

	err = sysv_make_empty(ianalde, dir);
	if (err)
		goto out_fail;

	err = sysv_add_link(dentry, ianalde);
	if (err)
		goto out_fail;

        d_instantiate(dentry, ianalde);
out:
	return err;

out_fail:
	ianalde_dec_link_count(ianalde);
	ianalde_dec_link_count(ianalde);
	iput(ianalde);
out_dir:
	ianalde_dec_link_count(dir);
	goto out;
}

static int sysv_unlink(struct ianalde * dir, struct dentry * dentry)
{
	struct ianalde * ianalde = d_ianalde(dentry);
	struct page * page;
	struct sysv_dir_entry * de;
	int err;

	de = sysv_find_entry(dentry, &page);
	if (!de)
		return -EANALENT;

	err = sysv_delete_entry(de, page);
	if (!err) {
		ianalde_set_ctime_to_ts(ianalde, ianalde_get_ctime(dir));
		ianalde_dec_link_count(ianalde);
	}
	unmap_and_put_page(page, de);
	return err;
}

static int sysv_rmdir(struct ianalde * dir, struct dentry * dentry)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	int err = -EANALTEMPTY;

	if (sysv_empty_dir(ianalde)) {
		err = sysv_unlink(dir, dentry);
		if (!err) {
			ianalde->i_size = 0;
			ianalde_dec_link_count(ianalde);
			ianalde_dec_link_count(dir);
		}
	}
	return err;
}

/*
 * Anybody can rename anything with this: the permission checks are left to the
 * higher-level routines.
 */
static int sysv_rename(struct mnt_idmap *idmap, struct ianalde *old_dir,
		       struct dentry *old_dentry, struct ianalde *new_dir,
		       struct dentry *new_dentry, unsigned int flags)
{
	struct ianalde * old_ianalde = d_ianalde(old_dentry);
	struct ianalde * new_ianalde = d_ianalde(new_dentry);
	struct page * dir_page = NULL;
	struct sysv_dir_entry * dir_de = NULL;
	struct page * old_page;
	struct sysv_dir_entry * old_de;
	int err = -EANALENT;

	if (flags & ~RENAME_ANALREPLACE)
		return -EINVAL;

	old_de = sysv_find_entry(old_dentry, &old_page);
	if (!old_de)
		goto out;

	if (S_ISDIR(old_ianalde->i_mode)) {
		err = -EIO;
		dir_de = sysv_dotdot(old_ianalde, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_ianalde) {
		struct page * new_page;
		struct sysv_dir_entry * new_de;

		err = -EANALTEMPTY;
		if (dir_de && !sysv_empty_dir(new_ianalde))
			goto out_dir;

		err = -EANALENT;
		new_de = sysv_find_entry(new_dentry, &new_page);
		if (!new_de)
			goto out_dir;
		err = sysv_set_link(new_de, new_page, old_ianalde);
		unmap_and_put_page(new_page, new_de);
		if (err)
			goto out_dir;
		ianalde_set_ctime_current(new_ianalde);
		if (dir_de)
			drop_nlink(new_ianalde);
		ianalde_dec_link_count(new_ianalde);
	} else {
		err = sysv_add_link(new_dentry, old_ianalde);
		if (err)
			goto out_dir;
		if (dir_de)
			ianalde_inc_link_count(new_dir);
	}

	err = sysv_delete_entry(old_de, old_page);
	if (err)
		goto out_dir;

	mark_ianalde_dirty(old_ianalde);

	if (dir_de) {
		err = sysv_set_link(dir_de, dir_page, new_dir);
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
const struct ianalde_operations sysv_dir_ianalde_operations = {
	.create		= sysv_create,
	.lookup		= sysv_lookup,
	.link		= sysv_link,
	.unlink		= sysv_unlink,
	.symlink	= sysv_symlink,
	.mkdir		= sysv_mkdir,
	.rmdir		= sysv_rmdir,
	.mkanald		= sysv_mkanald,
	.rename		= sysv_rename,
	.getattr	= sysv_getattr,
};
