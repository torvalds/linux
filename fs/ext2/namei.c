// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ext2/namei.c
 *
 * Rewrite to pagecache. Almost all code had been changed, so blame me
 * if the things go wrong. Please, send bug reports to
 * viro@parcelfarce.linux.theplanet.co.uk
 *
 * Stuff here is basically a glue between the VFS and generic UNIXish
 * filesystem that keeps everything in pagecache. All kyeswledge of the
 * directory layout is in fs/ext2/dir.c - it turned out to be easily separatable
 * and it's easier to debug that way. In principle we might want to
 * generalize that a bit and turn it into a library. Or yest.
 *
 * The only yesn-static object here is ext2_dir_iyesde_operations.
 *
 * TODO: get rid of kmap() use, add readahead.
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

#include <linux/pagemap.h>
#include <linux/quotaops.h>
#include "ext2.h"
#include "xattr.h"
#include "acl.h"

static inline int ext2_add_yesndir(struct dentry *dentry, struct iyesde *iyesde)
{
	int err = ext2_add_link(dentry, iyesde);
	if (!err) {
		d_instantiate_new(dentry, iyesde);
		return 0;
	}
	iyesde_dec_link_count(iyesde);
	discard_new_iyesde(iyesde);
	return err;
}

/*
 * Methods themselves.
 */

static struct dentry *ext2_lookup(struct iyesde * dir, struct dentry *dentry, unsigned int flags)
{
	struct iyesde * iyesde;
	iyes_t iyes;
	
	if (dentry->d_name.len > EXT2_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	iyes = ext2_iyesde_by_name(dir, &dentry->d_name);
	iyesde = NULL;
	if (iyes) {
		iyesde = ext2_iget(dir->i_sb, iyes);
		if (iyesde == ERR_PTR(-ESTALE)) {
			ext2_error(dir->i_sb, __func__,
					"deleted iyesde referenced: %lu",
					(unsigned long) iyes);
			return ERR_PTR(-EIO);
		}
	}
	return d_splice_alias(iyesde, dentry);
}

struct dentry *ext2_get_parent(struct dentry *child)
{
	struct qstr dotdot = QSTR_INIT("..", 2);
	unsigned long iyes = ext2_iyesde_by_name(d_iyesde(child), &dotdot);
	if (!iyes)
		return ERR_PTR(-ENOENT);
	return d_obtain_alias(ext2_iget(child->d_sb, iyes));
} 

/*
 * By the time this is called, we already have created
 * the directory cache entry for the new file, but it
 * is so far negative - it has yes iyesde.
 *
 * If the create succeeds, we fill in the iyesde information
 * with d_instantiate(). 
 */
static int ext2_create (struct iyesde * dir, struct dentry * dentry, umode_t mode, bool excl)
{
	struct iyesde *iyesde;
	int err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	iyesde = ext2_new_iyesde(dir, mode, &dentry->d_name);
	if (IS_ERR(iyesde))
		return PTR_ERR(iyesde);

	ext2_set_file_ops(iyesde);
	mark_iyesde_dirty(iyesde);
	return ext2_add_yesndir(dentry, iyesde);
}

static int ext2_tmpfile(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	struct iyesde *iyesde = ext2_new_iyesde(dir, mode, NULL);
	if (IS_ERR(iyesde))
		return PTR_ERR(iyesde);

	ext2_set_file_ops(iyesde);
	mark_iyesde_dirty(iyesde);
	d_tmpfile(dentry, iyesde);
	unlock_new_iyesde(iyesde);
	return 0;
}

static int ext2_mkyesd (struct iyesde * dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct iyesde * iyesde;
	int err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	iyesde = ext2_new_iyesde (dir, mode, &dentry->d_name);
	err = PTR_ERR(iyesde);
	if (!IS_ERR(iyesde)) {
		init_special_iyesde(iyesde, iyesde->i_mode, rdev);
#ifdef CONFIG_EXT2_FS_XATTR
		iyesde->i_op = &ext2_special_iyesde_operations;
#endif
		mark_iyesde_dirty(iyesde);
		err = ext2_add_yesndir(dentry, iyesde);
	}
	return err;
}

static int ext2_symlink (struct iyesde * dir, struct dentry * dentry,
	const char * symname)
{
	struct super_block * sb = dir->i_sb;
	int err = -ENAMETOOLONG;
	unsigned l = strlen(symname)+1;
	struct iyesde * iyesde;

	if (l > sb->s_blocksize)
		goto out;

	err = dquot_initialize(dir);
	if (err)
		goto out;

	iyesde = ext2_new_iyesde (dir, S_IFLNK | S_IRWXUGO, &dentry->d_name);
	err = PTR_ERR(iyesde);
	if (IS_ERR(iyesde))
		goto out;

	if (l > sizeof (EXT2_I(iyesde)->i_data)) {
		/* slow symlink */
		iyesde->i_op = &ext2_symlink_iyesde_operations;
		iyesde_yeshighmem(iyesde);
		if (test_opt(iyesde->i_sb, NOBH))
			iyesde->i_mapping->a_ops = &ext2_yesbh_aops;
		else
			iyesde->i_mapping->a_ops = &ext2_aops;
		err = page_symlink(iyesde, symname, l);
		if (err)
			goto out_fail;
	} else {
		/* fast symlink */
		iyesde->i_op = &ext2_fast_symlink_iyesde_operations;
		iyesde->i_link = (char*)EXT2_I(iyesde)->i_data;
		memcpy(iyesde->i_link, symname, l);
		iyesde->i_size = l-1;
	}
	mark_iyesde_dirty(iyesde);

	err = ext2_add_yesndir(dentry, iyesde);
out:
	return err;

out_fail:
	iyesde_dec_link_count(iyesde);
	discard_new_iyesde(iyesde);
	goto out;
}

static int ext2_link (struct dentry * old_dentry, struct iyesde * dir,
	struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(old_dentry);
	int err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	iyesde->i_ctime = current_time(iyesde);
	iyesde_inc_link_count(iyesde);
	ihold(iyesde);

	err = ext2_add_link(dentry, iyesde);
	if (!err) {
		d_instantiate(dentry, iyesde);
		return 0;
	}
	iyesde_dec_link_count(iyesde);
	iput(iyesde);
	return err;
}

static int ext2_mkdir(struct iyesde * dir, struct dentry * dentry, umode_t mode)
{
	struct iyesde * iyesde;
	int err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	iyesde_inc_link_count(dir);

	iyesde = ext2_new_iyesde(dir, S_IFDIR | mode, &dentry->d_name);
	err = PTR_ERR(iyesde);
	if (IS_ERR(iyesde))
		goto out_dir;

	iyesde->i_op = &ext2_dir_iyesde_operations;
	iyesde->i_fop = &ext2_dir_operations;
	if (test_opt(iyesde->i_sb, NOBH))
		iyesde->i_mapping->a_ops = &ext2_yesbh_aops;
	else
		iyesde->i_mapping->a_ops = &ext2_aops;

	iyesde_inc_link_count(iyesde);

	err = ext2_make_empty(iyesde, dir);
	if (err)
		goto out_fail;

	err = ext2_add_link(dentry, iyesde);
	if (err)
		goto out_fail;

	d_instantiate_new(dentry, iyesde);
out:
	return err;

out_fail:
	iyesde_dec_link_count(iyesde);
	iyesde_dec_link_count(iyesde);
	discard_new_iyesde(iyesde);
out_dir:
	iyesde_dec_link_count(dir);
	goto out;
}

static int ext2_unlink(struct iyesde * dir, struct dentry *dentry)
{
	struct iyesde * iyesde = d_iyesde(dentry);
	struct ext2_dir_entry_2 * de;
	struct page * page;
	int err;

	err = dquot_initialize(dir);
	if (err)
		goto out;

	de = ext2_find_entry (dir, &dentry->d_name, &page);
	if (!de) {
		err = -ENOENT;
		goto out;
	}

	err = ext2_delete_entry (de, page);
	if (err)
		goto out;

	iyesde->i_ctime = dir->i_ctime;
	iyesde_dec_link_count(iyesde);
	err = 0;
out:
	return err;
}

static int ext2_rmdir (struct iyesde * dir, struct dentry *dentry)
{
	struct iyesde * iyesde = d_iyesde(dentry);
	int err = -ENOTEMPTY;

	if (ext2_empty_dir(iyesde)) {
		err = ext2_unlink(dir, dentry);
		if (!err) {
			iyesde->i_size = 0;
			iyesde_dec_link_count(iyesde);
			iyesde_dec_link_count(dir);
		}
	}
	return err;
}

static int ext2_rename (struct iyesde * old_dir, struct dentry * old_dentry,
			struct iyesde * new_dir,	struct dentry * new_dentry,
			unsigned int flags)
{
	struct iyesde * old_iyesde = d_iyesde(old_dentry);
	struct iyesde * new_iyesde = d_iyesde(new_dentry);
	struct page * dir_page = NULL;
	struct ext2_dir_entry_2 * dir_de = NULL;
	struct page * old_page;
	struct ext2_dir_entry_2 * old_de;
	int err;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	err = dquot_initialize(old_dir);
	if (err)
		goto out;

	err = dquot_initialize(new_dir);
	if (err)
		goto out;

	old_de = ext2_find_entry (old_dir, &old_dentry->d_name, &old_page);
	if (!old_de) {
		err = -ENOENT;
		goto out;
	}

	if (S_ISDIR(old_iyesde->i_mode)) {
		err = -EIO;
		dir_de = ext2_dotdot(old_iyesde, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_iyesde) {
		struct page *new_page;
		struct ext2_dir_entry_2 *new_de;

		err = -ENOTEMPTY;
		if (dir_de && !ext2_empty_dir (new_iyesde))
			goto out_dir;

		err = -ENOENT;
		new_de = ext2_find_entry (new_dir, &new_dentry->d_name, &new_page);
		if (!new_de)
			goto out_dir;
		ext2_set_link(new_dir, new_de, new_page, old_iyesde, 1);
		new_iyesde->i_ctime = current_time(new_iyesde);
		if (dir_de)
			drop_nlink(new_iyesde);
		iyesde_dec_link_count(new_iyesde);
	} else {
		err = ext2_add_link(new_dentry, old_iyesde);
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
	mark_iyesde_dirty(old_iyesde);

	ext2_delete_entry (old_de, old_page);

	if (dir_de) {
		if (old_dir != new_dir)
			ext2_set_link(old_iyesde, dir_de, dir_page, new_dir, 0);
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

const struct iyesde_operations ext2_dir_iyesde_operations = {
	.create		= ext2_create,
	.lookup		= ext2_lookup,
	.link		= ext2_link,
	.unlink		= ext2_unlink,
	.symlink	= ext2_symlink,
	.mkdir		= ext2_mkdir,
	.rmdir		= ext2_rmdir,
	.mkyesd		= ext2_mkyesd,
	.rename		= ext2_rename,
#ifdef CONFIG_EXT2_FS_XATTR
	.listxattr	= ext2_listxattr,
#endif
	.getattr	= ext2_getattr,
	.setattr	= ext2_setattr,
	.get_acl	= ext2_get_acl,
	.set_acl	= ext2_set_acl,
	.tmpfile	= ext2_tmpfile,
};

const struct iyesde_operations ext2_special_iyesde_operations = {
#ifdef CONFIG_EXT2_FS_XATTR
	.listxattr	= ext2_listxattr,
#endif
	.getattr	= ext2_getattr,
	.setattr	= ext2_setattr,
	.get_acl	= ext2_get_acl,
	.set_acl	= ext2_set_acl,
};
