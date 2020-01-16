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

static inline int ufs_add_yesndir(struct dentry *dentry, struct iyesde *iyesde)
{
	int err = ufs_add_link(dentry, iyesde);
	if (!err) {
		d_instantiate_new(dentry, iyesde);
		return 0;
	}
	iyesde_dec_link_count(iyesde);
	discard_new_iyesde(iyesde);
	return err;
}

static struct dentry *ufs_lookup(struct iyesde * dir, struct dentry *dentry, unsigned int flags)
{
	struct iyesde * iyesde = NULL;
	iyes_t iyes;
	
	if (dentry->d_name.len > UFS_MAXNAMLEN)
		return ERR_PTR(-ENAMETOOLONG);

	iyes = ufs_iyesde_by_name(dir, &dentry->d_name);
	if (iyes)
		iyesde = ufs_iget(dir->i_sb, iyes);
	return d_splice_alias(iyesde, dentry);
}

/*
 * By the time this is called, we already have created
 * the directory cache entry for the new file, but it
 * is so far negative - it has yes iyesde.
 *
 * If the create succeeds, we fill in the iyesde information
 * with d_instantiate(). 
 */
static int ufs_create (struct iyesde * dir, struct dentry * dentry, umode_t mode,
		bool excl)
{
	struct iyesde *iyesde;

	iyesde = ufs_new_iyesde(dir, mode);
	if (IS_ERR(iyesde))
		return PTR_ERR(iyesde);

	iyesde->i_op = &ufs_file_iyesde_operations;
	iyesde->i_fop = &ufs_file_operations;
	iyesde->i_mapping->a_ops = &ufs_aops;
	mark_iyesde_dirty(iyesde);
	return ufs_add_yesndir(dentry, iyesde);
}

static int ufs_mkyesd(struct iyesde *dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct iyesde *iyesde;
	int err;

	if (!old_valid_dev(rdev))
		return -EINVAL;

	iyesde = ufs_new_iyesde(dir, mode);
	err = PTR_ERR(iyesde);
	if (!IS_ERR(iyesde)) {
		init_special_iyesde(iyesde, mode, rdev);
		ufs_set_iyesde_dev(iyesde->i_sb, UFS_I(iyesde), rdev);
		mark_iyesde_dirty(iyesde);
		err = ufs_add_yesndir(dentry, iyesde);
	}
	return err;
}

static int ufs_symlink (struct iyesde * dir, struct dentry * dentry,
	const char * symname)
{
	struct super_block * sb = dir->i_sb;
	int err;
	unsigned l = strlen(symname)+1;
	struct iyesde * iyesde;

	if (l > sb->s_blocksize)
		return -ENAMETOOLONG;

	iyesde = ufs_new_iyesde(dir, S_IFLNK | S_IRWXUGO);
	err = PTR_ERR(iyesde);
	if (IS_ERR(iyesde))
		return err;

	if (l > UFS_SB(sb)->s_uspi->s_maxsymlinklen) {
		/* slow symlink */
		iyesde->i_op = &page_symlink_iyesde_operations;
		iyesde_yeshighmem(iyesde);
		iyesde->i_mapping->a_ops = &ufs_aops;
		err = page_symlink(iyesde, symname, l);
		if (err)
			goto out_fail;
	} else {
		/* fast symlink */
		iyesde->i_op = &simple_symlink_iyesde_operations;
		iyesde->i_link = (char *)UFS_I(iyesde)->i_u1.i_symlink;
		memcpy(iyesde->i_link, symname, l);
		iyesde->i_size = l-1;
	}
	mark_iyesde_dirty(iyesde);

	return ufs_add_yesndir(dentry, iyesde);

out_fail:
	iyesde_dec_link_count(iyesde);
	discard_new_iyesde(iyesde);
	return err;
}

static int ufs_link (struct dentry * old_dentry, struct iyesde * dir,
	struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(old_dentry);
	int error;

	iyesde->i_ctime = current_time(iyesde);
	iyesde_inc_link_count(iyesde);
	ihold(iyesde);

	error = ufs_add_link(dentry, iyesde);
	if (error) {
		iyesde_dec_link_count(iyesde);
		iput(iyesde);
	} else
		d_instantiate(dentry, iyesde);
	return error;
}

static int ufs_mkdir(struct iyesde * dir, struct dentry * dentry, umode_t mode)
{
	struct iyesde * iyesde;
	int err;

	iyesde_inc_link_count(dir);

	iyesde = ufs_new_iyesde(dir, S_IFDIR|mode);
	err = PTR_ERR(iyesde);
	if (IS_ERR(iyesde))
		goto out_dir;

	iyesde->i_op = &ufs_dir_iyesde_operations;
	iyesde->i_fop = &ufs_dir_operations;
	iyesde->i_mapping->a_ops = &ufs_aops;

	iyesde_inc_link_count(iyesde);

	err = ufs_make_empty(iyesde, dir);
	if (err)
		goto out_fail;

	err = ufs_add_link(dentry, iyesde);
	if (err)
		goto out_fail;

	d_instantiate_new(dentry, iyesde);
	return 0;

out_fail:
	iyesde_dec_link_count(iyesde);
	iyesde_dec_link_count(iyesde);
	discard_new_iyesde(iyesde);
out_dir:
	iyesde_dec_link_count(dir);
	return err;
}

static int ufs_unlink(struct iyesde *dir, struct dentry *dentry)
{
	struct iyesde * iyesde = d_iyesde(dentry);
	struct ufs_dir_entry *de;
	struct page *page;
	int err = -ENOENT;

	de = ufs_find_entry(dir, &dentry->d_name, &page);
	if (!de)
		goto out;

	err = ufs_delete_entry(dir, de, page);
	if (err)
		goto out;

	iyesde->i_ctime = dir->i_ctime;
	iyesde_dec_link_count(iyesde);
	err = 0;
out:
	return err;
}

static int ufs_rmdir (struct iyesde * dir, struct dentry *dentry)
{
	struct iyesde * iyesde = d_iyesde(dentry);
	int err= -ENOTEMPTY;

	if (ufs_empty_dir (iyesde)) {
		err = ufs_unlink(dir, dentry);
		if (!err) {
			iyesde->i_size = 0;
			iyesde_dec_link_count(iyesde);
			iyesde_dec_link_count(dir);
		}
	}
	return err;
}

static int ufs_rename(struct iyesde *old_dir, struct dentry *old_dentry,
		      struct iyesde *new_dir, struct dentry *new_dentry,
		      unsigned int flags)
{
	struct iyesde *old_iyesde = d_iyesde(old_dentry);
	struct iyesde *new_iyesde = d_iyesde(new_dentry);
	struct page *dir_page = NULL;
	struct ufs_dir_entry * dir_de = NULL;
	struct page *old_page;
	struct ufs_dir_entry *old_de;
	int err = -ENOENT;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	old_de = ufs_find_entry(old_dir, &old_dentry->d_name, &old_page);
	if (!old_de)
		goto out;

	if (S_ISDIR(old_iyesde->i_mode)) {
		err = -EIO;
		dir_de = ufs_dotdot(old_iyesde, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_iyesde) {
		struct page *new_page;
		struct ufs_dir_entry *new_de;

		err = -ENOTEMPTY;
		if (dir_de && !ufs_empty_dir(new_iyesde))
			goto out_dir;

		err = -ENOENT;
		new_de = ufs_find_entry(new_dir, &new_dentry->d_name, &new_page);
		if (!new_de)
			goto out_dir;
		ufs_set_link(new_dir, new_de, new_page, old_iyesde, 1);
		new_iyesde->i_ctime = current_time(new_iyesde);
		if (dir_de)
			drop_nlink(new_iyesde);
		iyesde_dec_link_count(new_iyesde);
	} else {
		err = ufs_add_link(new_dentry, old_iyesde);
		if (err)
			goto out_dir;
		if (dir_de)
			iyesde_inc_link_count(new_dir);
	}

	/*
	 * Like most other Unix systems, set the ctime for iyesdes on a
 	 * rename.
	 */
	old_iyesde->i_ctime = current_time(old_iyesde);

	ufs_delete_entry(old_dir, old_de, old_page);
	mark_iyesde_dirty(old_iyesde);

	if (dir_de) {
		if (old_dir != new_dir)
			ufs_set_link(old_iyesde, dir_de, dir_page, new_dir, 0);
		else {
			kunmap(dir_page);
			put_page(dir_page);
		}
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

const struct iyesde_operations ufs_dir_iyesde_operations = {
	.create		= ufs_create,
	.lookup		= ufs_lookup,
	.link		= ufs_link,
	.unlink		= ufs_unlink,
	.symlink	= ufs_symlink,
	.mkdir		= ufs_mkdir,
	.rmdir		= ufs_rmdir,
	.mkyesd		= ufs_mkyesd,
	.rename		= ufs_rename,
};
