/*
 *  linux/fs/sysv/namei.c
 *
 *  minix/namei.c
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  coh/namei.c
 *  Copyright (C) 1993  Pascal Haible, Bruno Haible
 *
 *  sysv/namei.c
 *  Copyright (C) 1993  Bruno Haible
 *  Copyright (C) 1997, 1998  Krzysztof G. Baranowski
 */

#include <linux/pagemap.h>
#include "sysv.h"

static int add_nondir(struct dentry *dentry, struct inode *inode)
{
	int err = sysv_add_link(dentry, inode);
	if (!err) {
		d_instantiate(dentry, inode);
		return 0;
	}
	inode_dec_link_count(inode);
	iput(inode);
	return err;
}

static int sysv_hash(const struct dentry *dentry, struct qstr *qstr)
{
	/* Truncate the name in place, avoids having to define a compare
	   function. */
	if (qstr->len > SYSV_NAMELEN) {
		qstr->len = SYSV_NAMELEN;
		qstr->hash = full_name_hash(qstr->name, qstr->len);
	}
	return 0;
}

const struct dentry_operations sysv_dentry_operations = {
	.d_hash		= sysv_hash,
};

static struct dentry *sysv_lookup(struct inode * dir, struct dentry * dentry, unsigned int flags)
{
	struct inode * inode = NULL;
	ino_t ino;

	if (dentry->d_name.len > SYSV_NAMELEN)
		return ERR_PTR(-ENAMETOOLONG);
	ino = sysv_inode_by_name(dentry);

	if (ino) {
		inode = sysv_iget(dir->i_sb, ino);
		if (IS_ERR(inode))
			return ERR_CAST(inode);
	}
	d_add(dentry, inode);
	return NULL;
}

static int sysv_mknod(struct inode * dir, struct dentry * dentry, umode_t mode, dev_t rdev)
{
	struct inode * inode;
	int err;

	if (!old_valid_dev(rdev))
		return -EINVAL;

	inode = sysv_new_inode(dir, mode);
	err = PTR_ERR(inode);

	if (!IS_ERR(inode)) {
		sysv_set_inode(inode, rdev);
		mark_inode_dirty(inode);
		err = add_nondir(dentry, inode);
	}
	return err;
}

static int sysv_create(struct inode * dir, struct dentry * dentry, umode_t mode, bool excl)
{
	return sysv_mknod(dir, dentry, mode, 0);
}

static int sysv_symlink(struct inode * dir, struct dentry * dentry, 
	const char * symname)
{
	int err = -ENAMETOOLONG;
	int l = strlen(symname)+1;
	struct inode * inode;

	if (l > dir->i_sb->s_blocksize)
		goto out;

	inode = sysv_new_inode(dir, S_IFLNK|0777);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out;
	
	sysv_set_inode(inode, 0);
	err = page_symlink(inode, symname, l);
	if (err)
		goto out_fail;

	mark_inode_dirty(inode);
	err = add_nondir(dentry, inode);
out:
	return err;

out_fail:
	inode_dec_link_count(inode);
	iput(inode);
	goto out;
}

static int sysv_link(struct dentry * old_dentry, struct inode * dir, 
	struct dentry * dentry)
{
	struct inode *inode = d_inode(old_dentry);

	inode->i_ctime = CURRENT_TIME_SEC;
	inode_inc_link_count(inode);
	ihold(inode);

	return add_nondir(dentry, inode);
}

static int sysv_mkdir(struct inode * dir, struct dentry *dentry, umode_t mode)
{
	struct inode * inode;
	int err;

	inode_inc_link_count(dir);

	inode = sysv_new_inode(dir, S_IFDIR|mode);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_dir;

	sysv_set_inode(inode, 0);

	inode_inc_link_count(inode);

	err = sysv_make_empty(inode, dir);
	if (err)
		goto out_fail;

	err = sysv_add_link(dentry, inode);
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

static int sysv_unlink(struct inode * dir, struct dentry * dentry)
{
	struct inode * inode = d_inode(dentry);
	struct page * page;
	struct sysv_dir_entry * de;
	int err = -ENOENT;

	de = sysv_find_entry(dentry, &page);
	if (!de)
		goto out;

	err = sysv_delete_entry (de, page);
	if (err)
		goto out;

	inode->i_ctime = dir->i_ctime;
	inode_dec_link_count(inode);
out:
	return err;
}

static int sysv_rmdir(struct inode * dir, struct dentry * dentry)
{
	struct inode *inode = d_inode(dentry);
	int err = -ENOTEMPTY;

	if (sysv_empty_dir(inode)) {
		err = sysv_unlink(dir, dentry);
		if (!err) {
			inode->i_size = 0;
			inode_dec_link_count(inode);
			inode_dec_link_count(dir);
		}
	}
	return err;
}

/*
 * Anybody can rename anything with this: the permission checks are left to the
 * higher-level routines.
 */
static int sysv_rename(struct inode * old_dir, struct dentry * old_dentry,
		  struct inode * new_dir, struct dentry * new_dentry)
{
	struct inode * old_inode = d_inode(old_dentry);
	struct inode * new_inode = d_inode(new_dentry);
	struct page * dir_page = NULL;
	struct sysv_dir_entry * dir_de = NULL;
	struct page * old_page;
	struct sysv_dir_entry * old_de;
	int err = -ENOENT;

	old_de = sysv_find_entry(old_dentry, &old_page);
	if (!old_de)
		goto out;

	if (S_ISDIR(old_inode->i_mode)) {
		err = -EIO;
		dir_de = sysv_dotdot(old_inode, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_inode) {
		struct page * new_page;
		struct sysv_dir_entry * new_de;

		err = -ENOTEMPTY;
		if (dir_de && !sysv_empty_dir(new_inode))
			goto out_dir;

		err = -ENOENT;
		new_de = sysv_find_entry(new_dentry, &new_page);
		if (!new_de)
			goto out_dir;
		sysv_set_link(new_de, new_page, old_inode);
		new_inode->i_ctime = CURRENT_TIME_SEC;
		if (dir_de)
			drop_nlink(new_inode);
		inode_dec_link_count(new_inode);
	} else {
		err = sysv_add_link(new_dentry, old_inode);
		if (err)
			goto out_dir;
		if (dir_de)
			inode_inc_link_count(new_dir);
	}

	sysv_delete_entry(old_de, old_page);
	mark_inode_dirty(old_inode);

	if (dir_de) {
		sysv_set_link(dir_de, dir_page, new_dir);
		inode_dec_link_count(old_dir);
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
const struct inode_operations sysv_dir_inode_operations = {
	.create		= sysv_create,
	.lookup		= sysv_lookup,
	.link		= sysv_link,
	.unlink		= sysv_unlink,
	.symlink	= sysv_symlink,
	.mkdir		= sysv_mkdir,
	.rmdir		= sysv_rmdir,
	.mknod		= sysv_mknod,
	.rename		= sysv_rename,
	.getattr	= sysv_getattr,
};
