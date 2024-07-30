// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/minix/namei.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include "minix.h"

static int add_nondir(struct dentry *dentry, struct inode *inode)
{
	int err = minix_add_link(dentry, inode);
	if (!err) {
		d_instantiate(dentry, inode);
		return 0;
	}
	inode_dec_link_count(inode);
	iput(inode);
	return err;
}

static struct dentry *minix_lookup(struct inode * dir, struct dentry *dentry, unsigned int flags)
{
	struct inode * inode = NULL;
	ino_t ino;

	if (dentry->d_name.len > minix_sb(dir->i_sb)->s_namelen)
		return ERR_PTR(-ENAMETOOLONG);

	ino = minix_inode_by_name(dentry);
	if (ino)
		inode = minix_iget(dir->i_sb, ino);
	return d_splice_alias(inode, dentry);
}

static int minix_mknod(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct inode *inode;

	if (!old_valid_dev(rdev))
		return -EINVAL;

	inode = minix_new_inode(dir, mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	minix_set_inode(inode, rdev);
	mark_inode_dirty(inode);
	return add_nondir(dentry, inode);
}

static int minix_tmpfile(struct mnt_idmap *idmap, struct inode *dir,
			 struct file *file, umode_t mode)
{
	struct inode *inode = minix_new_inode(dir, mode);

	if (IS_ERR(inode))
		return finish_open_simple(file, PTR_ERR(inode));
	minix_set_inode(inode, 0);
	mark_inode_dirty(inode);
	d_tmpfile(file, inode);
	return finish_open_simple(file, 0);
}

static int minix_create(struct mnt_idmap *idmap, struct inode *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	return minix_mknod(&nop_mnt_idmap, dir, dentry, mode, 0);
}

static int minix_symlink(struct mnt_idmap *idmap, struct inode *dir,
			 struct dentry *dentry, const char *symname)
{
	int i = strlen(symname)+1;
	struct inode * inode;
	int err;

	if (i > dir->i_sb->s_blocksize)
		return -ENAMETOOLONG;

	inode = minix_new_inode(dir, S_IFLNK | 0777);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	minix_set_inode(inode, 0);
	err = page_symlink(inode, symname, i);
	if (unlikely(err)) {
		inode_dec_link_count(inode);
		iput(inode);
		return err;
	}
	return add_nondir(dentry, inode);
}

static int minix_link(struct dentry * old_dentry, struct inode * dir,
	struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);

	inode_set_ctime_current(inode);
	inode_inc_link_count(inode);
	ihold(inode);
	return add_nondir(dentry, inode);
}

static int minix_mkdir(struct mnt_idmap *idmap, struct inode *dir,
		       struct dentry *dentry, umode_t mode)
{
	struct inode * inode;
	int err;

	inode = minix_new_inode(dir, S_IFDIR | mode);
	if (IS_ERR(inode))
		return PTR_ERR(inode);

	inode_inc_link_count(dir);
	minix_set_inode(inode, 0);
	inode_inc_link_count(inode);

	err = minix_make_empty(inode, dir);
	if (err)
		goto out_fail;

	err = minix_add_link(dentry, inode);
	if (err)
		goto out_fail;

	d_instantiate(dentry, inode);
out:
	return err;

out_fail:
	inode_dec_link_count(inode);
	inode_dec_link_count(inode);
	iput(inode);
	inode_dec_link_count(dir);
	goto out;
}

static int minix_unlink(struct inode * dir, struct dentry *dentry)
{
	struct inode * inode = d_inode(dentry);
	struct page * page;
	struct minix_dir_entry * de;
	int err;

	de = minix_find_entry(dentry, &page);
	if (!de)
		return -ENOENT;
	err = minix_delete_entry(de, page);
	unmap_and_put_page(page, de);

	if (err)
		return err;
	inode_set_ctime_to_ts(inode, inode_get_ctime(dir));
	inode_dec_link_count(inode);
	return 0;
}

static int minix_rmdir(struct inode * dir, struct dentry *dentry)
{
	struct inode * inode = d_inode(dentry);
	int err = -ENOTEMPTY;

	if (minix_empty_dir(inode)) {
		err = minix_unlink(dir, dentry);
		if (!err) {
			inode_dec_link_count(dir);
			inode_dec_link_count(inode);
		}
	}
	return err;
}

static int minix_rename(struct mnt_idmap *idmap,
			struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	struct inode * old_inode = d_inode(old_dentry);
	struct inode * new_inode = d_inode(new_dentry);
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

	if (S_ISDIR(old_inode->i_mode)) {
		err = -EIO;
		dir_de = minix_dotdot(old_inode, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_inode) {
		struct page * new_page;
		struct minix_dir_entry * new_de;

		err = -ENOTEMPTY;
		if (dir_de && !minix_empty_dir(new_inode))
			goto out_dir;

		err = -ENOENT;
		new_de = minix_find_entry(new_dentry, &new_page);
		if (!new_de)
			goto out_dir;
		err = minix_set_link(new_de, new_page, old_inode);
		unmap_and_put_page(new_page, new_de);
		if (err)
			goto out_dir;
		inode_set_ctime_current(new_inode);
		if (dir_de)
			drop_nlink(new_inode);
		inode_dec_link_count(new_inode);
	} else {
		err = minix_add_link(new_dentry, old_inode);
		if (err)
			goto out_dir;
		if (dir_de)
			inode_inc_link_count(new_dir);
	}

	err = minix_delete_entry(old_de, old_page);
	if (err)
		goto out_dir;

	mark_inode_dirty(old_inode);

	if (dir_de) {
		err = minix_set_link(dir_de, dir_page, new_dir);
		if (!err)
			inode_dec_link_count(old_dir);
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
const struct inode_operations minix_dir_inode_operations = {
	.create		= minix_create,
	.lookup		= minix_lookup,
	.link		= minix_link,
	.unlink		= minix_unlink,
	.symlink	= minix_symlink,
	.mkdir		= minix_mkdir,
	.rmdir		= minix_rmdir,
	.mknod		= minix_mknod,
	.rename		= minix_rename,
	.getattr	= minix_getattr,
	.tmpfile	= minix_tmpfile,
};
