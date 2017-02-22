/*
 * namei.c - NILFS pathname lookup operations.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Modified for NILFS by Amagai Yoshiji and Ryusuke Konishi.
 */
/*
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

#include <linux/pagemap.h>
#include "nilfs.h"
#include "export.h"

#define NILFS_FID_SIZE_NON_CONNECTABLE \
	(offsetof(struct nilfs_fid, parent_gen) / 4)
#define NILFS_FID_SIZE_CONNECTABLE	(sizeof(struct nilfs_fid) / 4)

static inline int nilfs_add_nondir(struct dentry *dentry, struct inode *inode)
{
	int err = nilfs_add_link(dentry, inode);

	if (!err) {
		d_instantiate(dentry, inode);
		unlock_new_inode(inode);
		return 0;
	}
	inode_dec_link_count(inode);
	unlock_new_inode(inode);
	iput(inode);
	return err;
}

/*
 * Methods themselves.
 */

static struct dentry *
nilfs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	struct inode *inode;
	ino_t ino;

	if (dentry->d_name.len > NILFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ino = nilfs_inode_by_name(dir, &dentry->d_name);
	inode = ino ? nilfs_iget(dir->i_sb, NILFS_I(dir)->i_root, ino) : NULL;
	return d_splice_alias(inode, dentry);
}

/*
 * By the time this is called, we already have created
 * the directory cache entry for the new file, but it
 * is so far negative - it has no inode.
 *
 * If the create succeeds, we fill in the inode information
 * with d_instantiate().
 */
static int nilfs_create(struct inode *dir, struct dentry *dentry, umode_t mode,
			bool excl)
{
	struct inode *inode;
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;
	inode = nilfs_new_inode(dir, mode);
	err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {
		inode->i_op = &nilfs_file_inode_operations;
		inode->i_fop = &nilfs_file_operations;
		inode->i_mapping->a_ops = &nilfs_aops;
		nilfs_mark_inode_dirty(inode);
		err = nilfs_add_nondir(dentry, inode);
	}
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;
}

static int
nilfs_mknod(struct inode *dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct inode *inode;
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;
	inode = nilfs_new_inode(dir, mode);
	err = PTR_ERR(inode);
	if (!IS_ERR(inode)) {
		init_special_inode(inode, inode->i_mode, rdev);
		nilfs_mark_inode_dirty(inode);
		err = nilfs_add_nondir(dentry, inode);
	}
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;
}

static int nilfs_symlink(struct inode *dir, struct dentry *dentry,
			 const char *symname)
{
	struct nilfs_transaction_info ti;
	struct super_block *sb = dir->i_sb;
	unsigned int l = strlen(symname) + 1;
	struct inode *inode;
	int err;

	if (l > sb->s_blocksize)
		return -ENAMETOOLONG;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;

	inode = nilfs_new_inode(dir, S_IFLNK | S_IRWXUGO);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out;

	/* slow symlink */
	inode->i_op = &nilfs_symlink_inode_operations;
	inode_nohighmem(inode);
	inode->i_mapping->a_ops = &nilfs_aops;
	err = page_symlink(inode, symname, l);
	if (err)
		goto out_fail;

	/* mark_inode_dirty(inode); */
	/* page_symlink() do this */

	err = nilfs_add_nondir(dentry, inode);
out:
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;

out_fail:
	drop_nlink(inode);
	nilfs_mark_inode_dirty(inode);
	unlock_new_inode(inode);
	iput(inode);
	goto out;
}

static int nilfs_link(struct dentry *old_dentry, struct inode *dir,
		      struct dentry *dentry)
{
	struct inode *inode = d_inode(old_dentry);
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;

	inode->i_ctime = current_time(inode);
	inode_inc_link_count(inode);
	ihold(inode);

	err = nilfs_add_link(dentry, inode);
	if (!err) {
		d_instantiate(dentry, inode);
		err = nilfs_transaction_commit(dir->i_sb);
	} else {
		inode_dec_link_count(inode);
		iput(inode);
		nilfs_transaction_abort(dir->i_sb);
	}

	return err;
}

static int nilfs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode *inode;
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;

	inc_nlink(dir);

	inode = nilfs_new_inode(dir, S_IFDIR | mode);
	err = PTR_ERR(inode);
	if (IS_ERR(inode))
		goto out_dir;

	inode->i_op = &nilfs_dir_inode_operations;
	inode->i_fop = &nilfs_dir_operations;
	inode->i_mapping->a_ops = &nilfs_aops;

	inc_nlink(inode);

	err = nilfs_make_empty(inode, dir);
	if (err)
		goto out_fail;

	err = nilfs_add_link(dentry, inode);
	if (err)
		goto out_fail;

	nilfs_mark_inode_dirty(inode);
	d_instantiate(dentry, inode);
	unlock_new_inode(inode);
out:
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;

out_fail:
	drop_nlink(inode);
	drop_nlink(inode);
	nilfs_mark_inode_dirty(inode);
	unlock_new_inode(inode);
	iput(inode);
out_dir:
	drop_nlink(dir);
	nilfs_mark_inode_dirty(dir);
	goto out;
}

static int nilfs_do_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode;
	struct nilfs_dir_entry *de;
	struct page *page;
	int err;

	err = -ENOENT;
	de = nilfs_find_entry(dir, &dentry->d_name, &page);
	if (!de)
		goto out;

	inode = d_inode(dentry);
	err = -EIO;
	if (le64_to_cpu(de->inode) != inode->i_ino)
		goto out;

	if (!inode->i_nlink) {
		nilfs_msg(inode->i_sb, KERN_WARNING,
			  "deleting nonexistent file (ino=%lu), %d",
			  inode->i_ino, inode->i_nlink);
		set_nlink(inode, 1);
	}
	err = nilfs_delete_entry(de, page);
	if (err)
		goto out;

	inode->i_ctime = dir->i_ctime;
	drop_nlink(inode);
	err = 0;
out:
	return err;
}

static int nilfs_unlink(struct inode *dir, struct dentry *dentry)
{
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 0);
	if (err)
		return err;

	err = nilfs_do_unlink(dir, dentry);

	if (!err) {
		nilfs_mark_inode_dirty(dir);
		nilfs_mark_inode_dirty(d_inode(dentry));
		err = nilfs_transaction_commit(dir->i_sb);
	} else
		nilfs_transaction_abort(dir->i_sb);

	return err;
}

static int nilfs_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = d_inode(dentry);
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 0);
	if (err)
		return err;

	err = -ENOTEMPTY;
	if (nilfs_empty_dir(inode)) {
		err = nilfs_do_unlink(dir, dentry);
		if (!err) {
			inode->i_size = 0;
			drop_nlink(inode);
			nilfs_mark_inode_dirty(inode);
			drop_nlink(dir);
			nilfs_mark_inode_dirty(dir);
		}
	}
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;
}

static int nilfs_rename(struct inode *old_dir, struct dentry *old_dentry,
			struct inode *new_dir,	struct dentry *new_dentry,
			unsigned int flags)
{
	struct inode *old_inode = d_inode(old_dentry);
	struct inode *new_inode = d_inode(new_dentry);
	struct page *dir_page = NULL;
	struct nilfs_dir_entry *dir_de = NULL;
	struct page *old_page;
	struct nilfs_dir_entry *old_de;
	struct nilfs_transaction_info ti;
	int err;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	err = nilfs_transaction_begin(old_dir->i_sb, &ti, 1);
	if (unlikely(err))
		return err;

	err = -ENOENT;
	old_de = nilfs_find_entry(old_dir, &old_dentry->d_name, &old_page);
	if (!old_de)
		goto out;

	if (S_ISDIR(old_inode->i_mode)) {
		err = -EIO;
		dir_de = nilfs_dotdot(old_inode, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_inode) {
		struct page *new_page;
		struct nilfs_dir_entry *new_de;

		err = -ENOTEMPTY;
		if (dir_de && !nilfs_empty_dir(new_inode))
			goto out_dir;

		err = -ENOENT;
		new_de = nilfs_find_entry(new_dir, &new_dentry->d_name, &new_page);
		if (!new_de)
			goto out_dir;
		nilfs_set_link(new_dir, new_de, new_page, old_inode);
		nilfs_mark_inode_dirty(new_dir);
		new_inode->i_ctime = current_time(new_inode);
		if (dir_de)
			drop_nlink(new_inode);
		drop_nlink(new_inode);
		nilfs_mark_inode_dirty(new_inode);
	} else {
		err = nilfs_add_link(new_dentry, old_inode);
		if (err)
			goto out_dir;
		if (dir_de) {
			inc_nlink(new_dir);
			nilfs_mark_inode_dirty(new_dir);
		}
	}

	/*
	 * Like most other Unix systems, set the ctime for inodes on a
	 * rename.
	 */
	old_inode->i_ctime = current_time(old_inode);

	nilfs_delete_entry(old_de, old_page);

	if (dir_de) {
		nilfs_set_link(old_inode, dir_de, dir_page, new_dir);
		drop_nlink(old_dir);
	}
	nilfs_mark_inode_dirty(old_dir);
	nilfs_mark_inode_dirty(old_inode);

	err = nilfs_transaction_commit(old_dir->i_sb);
	return err;

out_dir:
	if (dir_de) {
		kunmap(dir_page);
		put_page(dir_page);
	}
out_old:
	kunmap(old_page);
	put_page(old_page);
out:
	nilfs_transaction_abort(old_dir->i_sb);
	return err;
}

/*
 * Export operations
 */
static struct dentry *nilfs_get_parent(struct dentry *child)
{
	unsigned long ino;
	struct inode *inode;
	struct qstr dotdot = QSTR_INIT("..", 2);
	struct nilfs_root *root;

	ino = nilfs_inode_by_name(d_inode(child), &dotdot);
	if (!ino)
		return ERR_PTR(-ENOENT);

	root = NILFS_I(d_inode(child))->i_root;

	inode = nilfs_iget(child->d_sb, root, ino);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

	return d_obtain_alias(inode);
}

static struct dentry *nilfs_get_dentry(struct super_block *sb, u64 cno,
				       u64 ino, u32 gen)
{
	struct nilfs_root *root;
	struct inode *inode;

	if (ino < NILFS_FIRST_INO(sb) && ino != NILFS_ROOT_INO)
		return ERR_PTR(-ESTALE);

	root = nilfs_lookup_root(sb->s_fs_info, cno);
	if (!root)
		return ERR_PTR(-ESTALE);

	inode = nilfs_iget(sb, root, ino);
	nilfs_put_root(root);

	if (IS_ERR(inode))
		return ERR_CAST(inode);
	if (gen && inode->i_generation != gen) {
		iput(inode);
		return ERR_PTR(-ESTALE);
	}
	return d_obtain_alias(inode);
}

static struct dentry *nilfs_fh_to_dentry(struct super_block *sb, struct fid *fh,
					 int fh_len, int fh_type)
{
	struct nilfs_fid *fid = (struct nilfs_fid *)fh;

	if (fh_len < NILFS_FID_SIZE_NON_CONNECTABLE ||
	    (fh_type != FILEID_NILFS_WITH_PARENT &&
	     fh_type != FILEID_NILFS_WITHOUT_PARENT))
		return NULL;

	return nilfs_get_dentry(sb, fid->cno, fid->ino, fid->gen);
}

static struct dentry *nilfs_fh_to_parent(struct super_block *sb, struct fid *fh,
					 int fh_len, int fh_type)
{
	struct nilfs_fid *fid = (struct nilfs_fid *)fh;

	if (fh_len < NILFS_FID_SIZE_CONNECTABLE ||
	    fh_type != FILEID_NILFS_WITH_PARENT)
		return NULL;

	return nilfs_get_dentry(sb, fid->cno, fid->parent_ino, fid->parent_gen);
}

static int nilfs_encode_fh(struct inode *inode, __u32 *fh, int *lenp,
			   struct inode *parent)
{
	struct nilfs_fid *fid = (struct nilfs_fid *)fh;
	struct nilfs_root *root = NILFS_I(inode)->i_root;
	int type;

	if (parent && *lenp < NILFS_FID_SIZE_CONNECTABLE) {
		*lenp = NILFS_FID_SIZE_CONNECTABLE;
		return FILEID_INVALID;
	}
	if (*lenp < NILFS_FID_SIZE_NON_CONNECTABLE) {
		*lenp = NILFS_FID_SIZE_NON_CONNECTABLE;
		return FILEID_INVALID;
	}

	fid->cno = root->cno;
	fid->ino = inode->i_ino;
	fid->gen = inode->i_generation;

	if (parent) {
		fid->parent_ino = parent->i_ino;
		fid->parent_gen = parent->i_generation;
		type = FILEID_NILFS_WITH_PARENT;
		*lenp = NILFS_FID_SIZE_CONNECTABLE;
	} else {
		type = FILEID_NILFS_WITHOUT_PARENT;
		*lenp = NILFS_FID_SIZE_NON_CONNECTABLE;
	}

	return type;
}

const struct inode_operations nilfs_dir_inode_operations = {
	.create		= nilfs_create,
	.lookup		= nilfs_lookup,
	.link		= nilfs_link,
	.unlink		= nilfs_unlink,
	.symlink	= nilfs_symlink,
	.mkdir		= nilfs_mkdir,
	.rmdir		= nilfs_rmdir,
	.mknod		= nilfs_mknod,
	.rename		= nilfs_rename,
	.setattr	= nilfs_setattr,
	.permission	= nilfs_permission,
	.fiemap		= nilfs_fiemap,
};

const struct inode_operations nilfs_special_inode_operations = {
	.setattr	= nilfs_setattr,
	.permission	= nilfs_permission,
};

const struct inode_operations nilfs_symlink_inode_operations = {
	.get_link	= page_get_link,
	.permission     = nilfs_permission,
};

const struct export_operations nilfs_export_ops = {
	.encode_fh = nilfs_encode_fh,
	.fh_to_dentry = nilfs_fh_to_dentry,
	.fh_to_parent = nilfs_fh_to_parent,
	.get_parent = nilfs_get_parent,
};
