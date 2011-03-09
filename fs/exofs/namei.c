/*
 * Copyright (C) 2005, 2006
 * Avishay Traeger (avishay@gmail.com)
 * Copyright (C) 2008, 2009
 * Boaz Harrosh <bharrosh@panasas.com>
 *
 * Copyrights for code taken from ext2:
 *     Copyright (C) 1992, 1993, 1994, 1995
 *     Remy Card (card@masi.ibp.fr)
 *     Laboratoire MASI - Institut Blaise Pascal
 *     Universite Pierre et Marie Curie (Paris VI)
 *     from
 *     linux/fs/minix/inode.c
 *     Copyright (C) 1991, 1992  Linus Torvalds
 *
 * This file is part of exofs.
 *
 * exofs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation.  Since it is based on ext2, and the only
 * valid version of GPL for the Linux kernel is version 2, the only valid
 * version of GPL for exofs is version 2.
 *
 * exofs is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with exofs; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include "exofs.h"

static inline int exofs_add_nondir(struct dentry *dentry, struct inode *inode)
{
	int err = exofs_add_link(dentry, inode);
	if (!err) {
		d_instantiate(dentry, inode);
		return 0;
	}
	inode_dec_link_count(inode);
	iput(inode);
	return err;
}

static struct dentry *exofs_lookup(struct inode *dir, struct dentry *dentry,
				   struct nameidata *nd)
{
	struct inode *inode;
	ino_t ino;

	if (dentry->d_name.len > EXOFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ino = exofs_inode_by_name(dir, dentry);
	inode = NULL;
	if (ino) {
		inode = exofs_iget(dir->i_sb, ino);
		if (IS_ERR(inode))
			return ERR_CAST(inode);
	}
	return d_splice_alias(inode, dentry);
}

static int exofs_create(struct inode *dir, struct dentry *dentry, int mode,
			 struct nameidata *nd)
{
	struct inode *inode = exofs_new_inode(dir, mode);
	int err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {
		inode->i_op = &exofs_file_inode_operations;
		inode->i_fop = &exofs_file_operations;
		inode->i_mapping->a_ops = &exofs_aops;
		mark_inode_dirty(inode);
		err = exofs_add_nondir(dentry, inode);
	}
	return err;
}

static int exofs_mknod(struct inode *dir, struct dentry *dentry, int mode,
		       dev_t rdev)
{
	struct inode *inode;
	int err;

	if (!new_valid_dev(rdev))
		return -EINVAL;

	inode = exofs_new_inode(dir, mode);
	err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {
		init_special_inode(inode, inode->i_mode, rdev);
		mark_inode_dirty(inode);
		err = exofs_add_nondir(dentry, inode);
	}
	return err;
}

static int exofs_symlink(struct inode *dir, struct dentry *dentry,
			  const char *symname)
{
	struct super_block *sb = dir->i_sb;
	int err = -ENAMETOOLONG;
	unsigned l = strlen(symname)+1;
	struct inode *inode;
	struct exofs_i_info *oi;

	if (l > sb->s_blocksize)
		goto out;

	inode = exofs_new_inode(dir, S_IFLNK | S_IRWXUGO);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out;

	oi = exofs_i(inode);
	if (l > sizeof(oi->i_data)) {
		/* slow symlink */
		inode->i_op = &exofs_symlink_inode_operations;
		inode->i_mapping->a_ops = &exofs_aops;
		memset(oi->i_data, 0, sizeof(oi->i_data));

		err = page_symlink(inode, symname, l);
		if (err)
			goto out_fail;
	} else {
		/* fast symlink */
		inode->i_op = &exofs_fast_symlink_inode_operations;
		memcpy(oi->i_data, symname, l);
		inode->i_size = l-1;
	}
	mark_inode_dirty(inode);

	err = exofs_add_nondir(dentry, inode);
out:
	return err;

out_fail:
	inode_dec_link_count(inode);
	iput(inode);
	goto out;
}

static int exofs_link(struct dentry *old_dentry, struct inode *dir,
		struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;

	if (inode->i_nlink >= EXOFS_LINK_MAX)
		return -EMLINK;

	inode->i_ctime = CURRENT_TIME;
	inode_inc_link_count(inode);
	ihold(inode);

	return exofs_add_nondir(dentry, inode);
}

static int exofs_mkdir(struct inode *dir, struct dentry *dentry, int mode)
{
	struct inode *inode;
	int err = -EMLINK;

	if (dir->i_nlink >= EXOFS_LINK_MAX)
		goto out;

	inode_inc_link_count(dir);

	inode = exofs_new_inode(dir, S_IFDIR | mode);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_dir;

	inode->i_op = &exofs_dir_inode_operations;
	inode->i_fop = &exofs_dir_operations;
	inode->i_mapping->a_ops = &exofs_aops;

	inode_inc_link_count(inode);

	err = exofs_make_empty(inode, dir);
	if (err)
		goto out_fail;

	err = exofs_add_link(dentry, inode);
	if (err)
		goto out_fail;

	d_instantiate(dentry, inode);
out:
	return err;

out_fail:
	inode_dec_link_count(inode);
	inode_dec_link_count(inode);
	iput(inode);
out_dir:
	inode_dec_link_count(dir);
	goto out;
}

static int exofs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct exofs_dir_entry *de;
	struct page *page;
	int err = -ENOENT;

	de = exofs_find_entry(dir, dentry, &page);
	if (!de)
		goto out;

	err = exofs_delete_entry(de, page);
	if (err)
		goto out;

	inode->i_ctime = dir->i_ctime;
	inode_dec_link_count(inode);
	err = 0;
out:
	return err;
}

static int exofs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	int err = -ENOTEMPTY;

	if (exofs_empty_dir(inode)) {
		err = exofs_unlink(dir, dentry);
		if (!err) {
			inode->i_size = 0;
			inode_dec_link_count(inode);
			inode_dec_link_count(dir);
		}
	}
	return err;
}

static int exofs_rename(struct inode *old_dir, struct dentry *old_dentry,
		struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode *old_inode = old_dentry->d_inode;
	struct inode *new_inode = new_dentry->d_inode;
	struct page *dir_page = NULL;
	struct exofs_dir_entry *dir_de = NULL;
	struct page *old_page;
	struct exofs_dir_entry *old_de;
	int err = -ENOENT;

	old_de = exofs_find_entry(old_dir, old_dentry, &old_page);
	if (!old_de)
		goto out;

	if (S_ISDIR(old_inode->i_mode)) {
		err = -EIO;
		dir_de = exofs_dotdot(old_inode, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_inode) {
		struct page *new_page;
		struct exofs_dir_entry *new_de;

		err = -ENOTEMPTY;
		if (dir_de && !exofs_empty_dir(new_inode))
			goto out_dir;

		err = -ENOENT;
		new_de = exofs_find_entry(new_dir, new_dentry, &new_page);
		if (!new_de)
			goto out_dir;
		inode_inc_link_count(old_inode);
		err = exofs_set_link(new_dir, new_de, new_page, old_inode);
		new_inode->i_ctime = CURRENT_TIME;
		if (dir_de)
			drop_nlink(new_inode);
		inode_dec_link_count(new_inode);
		if (err)
			goto out_dir;
	} else {
		if (dir_de) {
			err = -EMLINK;
			if (new_dir->i_nlink >= EXOFS_LINK_MAX)
				goto out_dir;
		}
		inode_inc_link_count(old_inode);
		err = exofs_add_link(new_dentry, old_inode);
		if (err) {
			inode_dec_link_count(old_inode);
			goto out_dir;
		}
		if (dir_de)
			inode_inc_link_count(new_dir);
	}

	old_inode->i_ctime = CURRENT_TIME;

	exofs_delete_entry(old_de, old_page);
	inode_dec_link_count(old_inode);

	if (dir_de) {
		err = exofs_set_link(old_inode, dir_de, dir_page, new_dir);
		inode_dec_link_count(old_dir);
		if (err)
			goto out_dir;
	}
	return 0;


out_dir:
	if (dir_de) {
		kunmap(dir_page);
		page_cache_release(dir_page);
	}
out_old:
	kunmap(old_page);
	page_cache_release(old_page);
out:
	return err;
}

const struct inode_operations exofs_dir_inode_operations = {
	.create 	= exofs_create,
	.lookup 	= exofs_lookup,
	.link   	= exofs_link,
	.unlink 	= exofs_unlink,
	.symlink	= exofs_symlink,
	.mkdir  	= exofs_mkdir,
	.rmdir  	= exofs_rmdir,
	.mknod  	= exofs_mknod,
	.rename 	= exofs_rename,
	.setattr	= exofs_setattr,
};

const struct inode_operations exofs_special_inode_operations = {
	.setattr	= exofs_setattr,
};
