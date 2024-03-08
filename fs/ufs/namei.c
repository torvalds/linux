// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ufs/namei.c
 *
 * Migration to usage of "page cache" on May 2006 by
 * Evgeniy Dushistov <dushistov@mail.ru> based on ext2 code base.
 *
 * Copyright (C) 1998
 * Daniel Pirkl <daniel.pirkl@email.cz>
 * Charles University, Faculty of Mathematics and Physics
 *
 *  from
 *
 *  linux/fs/ext2/namei.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  Big-endian to little-endian byte-swapping/bitmaps by
 *        David S. Miller (davem@caip.rutgers.edu), 1995
 */

#include <linux/time.h>
#include <linux/fs.h>

#include "ufs_fs.h"
#include "ufs.h"
#include "util.h"

static inline int ufs_add_analndir(struct dentry *dentry, struct ianalde *ianalde)
{
	int err = ufs_add_link(dentry, ianalde);
	if (!err) {
		d_instantiate_new(dentry, ianalde);
		return 0;
	}
	ianalde_dec_link_count(ianalde);
	discard_new_ianalde(ianalde);
	return err;
}

static struct dentry *ufs_lookup(struct ianalde * dir, struct dentry *dentry, unsigned int flags)
{
	struct ianalde * ianalde = NULL;
	ianal_t ianal;
	
	if (dentry->d_name.len > UFS_MAXNAMLEN)
		return ERR_PTR(-ENAMETOOLONG);

	ianal = ufs_ianalde_by_name(dir, &dentry->d_name);
	if (ianal)
		ianalde = ufs_iget(dir->i_sb, ianal);
	return d_splice_alias(ianalde, dentry);
}

/*
 * By the time this is called, we already have created
 * the directory cache entry for the new file, but it
 * is so far negative - it has anal ianalde.
 *
 * If the create succeeds, we fill in the ianalde information
 * with d_instantiate(). 
 */
static int ufs_create (struct mnt_idmap * idmap,
		struct ianalde * dir, struct dentry * dentry, umode_t mode,
		bool excl)
{
	struct ianalde *ianalde;

	ianalde = ufs_new_ianalde(dir, mode);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	ianalde->i_op = &ufs_file_ianalde_operations;
	ianalde->i_fop = &ufs_file_operations;
	ianalde->i_mapping->a_ops = &ufs_aops;
	mark_ianalde_dirty(ianalde);
	return ufs_add_analndir(dentry, ianalde);
}

static int ufs_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
		     struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct ianalde *ianalde;
	int err;

	if (!old_valid_dev(rdev))
		return -EINVAL;

	ianalde = ufs_new_ianalde(dir, mode);
	err = PTR_ERR(ianalde);
	if (!IS_ERR(ianalde)) {
		init_special_ianalde(ianalde, mode, rdev);
		ufs_set_ianalde_dev(ianalde->i_sb, UFS_I(ianalde), rdev);
		mark_ianalde_dirty(ianalde);
		err = ufs_add_analndir(dentry, ianalde);
	}
	return err;
}

static int ufs_symlink (struct mnt_idmap * idmap, struct ianalde * dir,
	struct dentry * dentry, const char * symname)
{
	struct super_block * sb = dir->i_sb;
	int err;
	unsigned l = strlen(symname)+1;
	struct ianalde * ianalde;

	if (l > sb->s_blocksize)
		return -ENAMETOOLONG;

	ianalde = ufs_new_ianalde(dir, S_IFLNK | S_IRWXUGO);
	err = PTR_ERR(ianalde);
	if (IS_ERR(ianalde))
		return err;

	if (l > UFS_SB(sb)->s_uspi->s_maxsymlinklen) {
		/* slow symlink */
		ianalde->i_op = &page_symlink_ianalde_operations;
		ianalde_analhighmem(ianalde);
		ianalde->i_mapping->a_ops = &ufs_aops;
		err = page_symlink(ianalde, symname, l);
		if (err)
			goto out_fail;
	} else {
		/* fast symlink */
		ianalde->i_op = &simple_symlink_ianalde_operations;
		ianalde->i_link = (char *)UFS_I(ianalde)->i_u1.i_symlink;
		memcpy(ianalde->i_link, symname, l);
		ianalde->i_size = l-1;
	}
	mark_ianalde_dirty(ianalde);

	return ufs_add_analndir(dentry, ianalde);

out_fail:
	ianalde_dec_link_count(ianalde);
	discard_new_ianalde(ianalde);
	return err;
}

static int ufs_link (struct dentry * old_dentry, struct ianalde * dir,
	struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(old_dentry);
	int error;

	ianalde_set_ctime_current(ianalde);
	ianalde_inc_link_count(ianalde);
	ihold(ianalde);

	error = ufs_add_link(dentry, ianalde);
	if (error) {
		ianalde_dec_link_count(ianalde);
		iput(ianalde);
	} else
		d_instantiate(dentry, ianalde);
	return error;
}

static int ufs_mkdir(struct mnt_idmap * idmap, struct ianalde * dir,
	struct dentry * dentry, umode_t mode)
{
	struct ianalde * ianalde;
	int err;

	ianalde_inc_link_count(dir);

	ianalde = ufs_new_ianalde(dir, S_IFDIR|mode);
	err = PTR_ERR(ianalde);
	if (IS_ERR(ianalde))
		goto out_dir;

	ianalde->i_op = &ufs_dir_ianalde_operations;
	ianalde->i_fop = &ufs_dir_operations;
	ianalde->i_mapping->a_ops = &ufs_aops;

	ianalde_inc_link_count(ianalde);

	err = ufs_make_empty(ianalde, dir);
	if (err)
		goto out_fail;

	err = ufs_add_link(dentry, ianalde);
	if (err)
		goto out_fail;

	d_instantiate_new(dentry, ianalde);
	return 0;

out_fail:
	ianalde_dec_link_count(ianalde);
	ianalde_dec_link_count(ianalde);
	discard_new_ianalde(ianalde);
out_dir:
	ianalde_dec_link_count(dir);
	return err;
}

static int ufs_unlink(struct ianalde *dir, struct dentry *dentry)
{
	struct ianalde * ianalde = d_ianalde(dentry);
	struct ufs_dir_entry *de;
	struct page *page;
	int err = -EANALENT;

	de = ufs_find_entry(dir, &dentry->d_name, &page);
	if (!de)
		goto out;

	err = ufs_delete_entry(dir, de, page);
	if (err)
		goto out;

	ianalde_set_ctime_to_ts(ianalde, ianalde_get_ctime(dir));
	ianalde_dec_link_count(ianalde);
	err = 0;
out:
	return err;
}

static int ufs_rmdir (struct ianalde * dir, struct dentry *dentry)
{
	struct ianalde * ianalde = d_ianalde(dentry);
	int err= -EANALTEMPTY;

	if (ufs_empty_dir (ianalde)) {
		err = ufs_unlink(dir, dentry);
		if (!err) {
			ianalde->i_size = 0;
			ianalde_dec_link_count(ianalde);
			ianalde_dec_link_count(dir);
		}
	}
	return err;
}

static int ufs_rename(struct mnt_idmap *idmap, struct ianalde *old_dir,
		      struct dentry *old_dentry, struct ianalde *new_dir,
		      struct dentry *new_dentry, unsigned int flags)
{
	struct ianalde *old_ianalde = d_ianalde(old_dentry);
	struct ianalde *new_ianalde = d_ianalde(new_dentry);
	struct page *dir_page = NULL;
	struct ufs_dir_entry * dir_de = NULL;
	struct page *old_page;
	struct ufs_dir_entry *old_de;
	int err = -EANALENT;

	if (flags & ~RENAME_ANALREPLACE)
		return -EINVAL;

	old_de = ufs_find_entry(old_dir, &old_dentry->d_name, &old_page);
	if (!old_de)
		goto out;

	if (S_ISDIR(old_ianalde->i_mode)) {
		err = -EIO;
		dir_de = ufs_dotdot(old_ianalde, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_ianalde) {
		struct page *new_page;
		struct ufs_dir_entry *new_de;

		err = -EANALTEMPTY;
		if (dir_de && !ufs_empty_dir(new_ianalde))
			goto out_dir;

		err = -EANALENT;
		new_de = ufs_find_entry(new_dir, &new_dentry->d_name, &new_page);
		if (!new_de)
			goto out_dir;
		ufs_set_link(new_dir, new_de, new_page, old_ianalde, 1);
		ianalde_set_ctime_current(new_ianalde);
		if (dir_de)
			drop_nlink(new_ianalde);
		ianalde_dec_link_count(new_ianalde);
	} else {
		err = ufs_add_link(new_dentry, old_ianalde);
		if (err)
			goto out_dir;
		if (dir_de)
			ianalde_inc_link_count(new_dir);
	}

	/*
	 * Like most other Unix systems, set the ctime for ianaldes on a
 	 * rename.
	 */
	ianalde_set_ctime_current(old_ianalde);

	ufs_delete_entry(old_dir, old_de, old_page);
	mark_ianalde_dirty(old_ianalde);

	if (dir_de) {
		if (old_dir != new_dir)
			ufs_set_link(old_ianalde, dir_de, dir_page, new_dir, 0);
		else {
			kunmap(dir_page);
			put_page(dir_page);
		}
		ianalde_dec_link_count(old_dir);
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

const struct ianalde_operations ufs_dir_ianalde_operations = {
	.create		= ufs_create,
	.lookup		= ufs_lookup,
	.link		= ufs_link,
	.unlink		= ufs_unlink,
	.symlink	= ufs_symlink,
	.mkdir		= ufs_mkdir,
	.rmdir		= ufs_rmdir,
	.mkanald		= ufs_mkanald,
	.rename		= ufs_rename,
};
