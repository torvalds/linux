// SPDX-License-Identifier: GPL-2.0+
/*
 * namei.c - NILFS pathname lookup operations.
 *
 * Copyright (C) 2005-2008 Nippon Telegraph and Telephone Corporation.
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

static inline int nilfs_add_yesndir(struct dentry *dentry, struct iyesde *iyesde)
{
	int err = nilfs_add_link(dentry, iyesde);

	if (!err) {
		d_instantiate_new(dentry, iyesde);
		return 0;
	}
	iyesde_dec_link_count(iyesde);
	unlock_new_iyesde(iyesde);
	iput(iyesde);
	return err;
}

/*
 * Methods themselves.
 */

static struct dentry *
nilfs_lookup(struct iyesde *dir, struct dentry *dentry, unsigned int flags)
{
	struct iyesde *iyesde;
	iyes_t iyes;

	if (dentry->d_name.len > NILFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	iyes = nilfs_iyesde_by_name(dir, &dentry->d_name);
	iyesde = iyes ? nilfs_iget(dir->i_sb, NILFS_I(dir)->i_root, iyes) : NULL;
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
static int nilfs_create(struct iyesde *dir, struct dentry *dentry, umode_t mode,
			bool excl)
{
	struct iyesde *iyesde;
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;
	iyesde = nilfs_new_iyesde(dir, mode);
	err = PTR_ERR(iyesde);
	if (!IS_ERR(iyesde)) {
		iyesde->i_op = &nilfs_file_iyesde_operations;
		iyesde->i_fop = &nilfs_file_operations;
		iyesde->i_mapping->a_ops = &nilfs_aops;
		nilfs_mark_iyesde_dirty(iyesde);
		err = nilfs_add_yesndir(dentry, iyesde);
	}
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;
}

static int
nilfs_mkyesd(struct iyesde *dir, struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct iyesde *iyesde;
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;
	iyesde = nilfs_new_iyesde(dir, mode);
	err = PTR_ERR(iyesde);
	if (!IS_ERR(iyesde)) {
		init_special_iyesde(iyesde, iyesde->i_mode, rdev);
		nilfs_mark_iyesde_dirty(iyesde);
		err = nilfs_add_yesndir(dentry, iyesde);
	}
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;
}

static int nilfs_symlink(struct iyesde *dir, struct dentry *dentry,
			 const char *symname)
{
	struct nilfs_transaction_info ti;
	struct super_block *sb = dir->i_sb;
	unsigned int l = strlen(symname) + 1;
	struct iyesde *iyesde;
	int err;

	if (l > sb->s_blocksize)
		return -ENAMETOOLONG;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;

	iyesde = nilfs_new_iyesde(dir, S_IFLNK | 0777);
	err = PTR_ERR(iyesde);
	if (IS_ERR(iyesde))
		goto out;

	/* slow symlink */
	iyesde->i_op = &nilfs_symlink_iyesde_operations;
	iyesde_yeshighmem(iyesde);
	iyesde->i_mapping->a_ops = &nilfs_aops;
	err = page_symlink(iyesde, symname, l);
	if (err)
		goto out_fail;

	/* mark_iyesde_dirty(iyesde); */
	/* page_symlink() do this */

	err = nilfs_add_yesndir(dentry, iyesde);
out:
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;

out_fail:
	drop_nlink(iyesde);
	nilfs_mark_iyesde_dirty(iyesde);
	unlock_new_iyesde(iyesde);
	iput(iyesde);
	goto out;
}

static int nilfs_link(struct dentry *old_dentry, struct iyesde *dir,
		      struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(old_dentry);
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;

	iyesde->i_ctime = current_time(iyesde);
	iyesde_inc_link_count(iyesde);
	ihold(iyesde);

	err = nilfs_add_link(dentry, iyesde);
	if (!err) {
		d_instantiate(dentry, iyesde);
		err = nilfs_transaction_commit(dir->i_sb);
	} else {
		iyesde_dec_link_count(iyesde);
		iput(iyesde);
		nilfs_transaction_abort(dir->i_sb);
	}

	return err;
}

static int nilfs_mkdir(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	struct iyesde *iyesde;
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;

	inc_nlink(dir);

	iyesde = nilfs_new_iyesde(dir, S_IFDIR | mode);
	err = PTR_ERR(iyesde);
	if (IS_ERR(iyesde))
		goto out_dir;

	iyesde->i_op = &nilfs_dir_iyesde_operations;
	iyesde->i_fop = &nilfs_dir_operations;
	iyesde->i_mapping->a_ops = &nilfs_aops;

	inc_nlink(iyesde);

	err = nilfs_make_empty(iyesde, dir);
	if (err)
		goto out_fail;

	err = nilfs_add_link(dentry, iyesde);
	if (err)
		goto out_fail;

	nilfs_mark_iyesde_dirty(iyesde);
	d_instantiate_new(dentry, iyesde);
out:
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;

out_fail:
	drop_nlink(iyesde);
	drop_nlink(iyesde);
	nilfs_mark_iyesde_dirty(iyesde);
	unlock_new_iyesde(iyesde);
	iput(iyesde);
out_dir:
	drop_nlink(dir);
	nilfs_mark_iyesde_dirty(dir);
	goto out;
}

static int nilfs_do_unlink(struct iyesde *dir, struct dentry *dentry)
{
	struct iyesde *iyesde;
	struct nilfs_dir_entry *de;
	struct page *page;
	int err;

	err = -ENOENT;
	de = nilfs_find_entry(dir, &dentry->d_name, &page);
	if (!de)
		goto out;

	iyesde = d_iyesde(dentry);
	err = -EIO;
	if (le64_to_cpu(de->iyesde) != iyesde->i_iyes)
		goto out;

	if (!iyesde->i_nlink) {
		nilfs_msg(iyesde->i_sb, KERN_WARNING,
			  "deleting yesnexistent file (iyes=%lu), %d",
			  iyesde->i_iyes, iyesde->i_nlink);
		set_nlink(iyesde, 1);
	}
	err = nilfs_delete_entry(de, page);
	if (err)
		goto out;

	iyesde->i_ctime = dir->i_ctime;
	drop_nlink(iyesde);
	err = 0;
out:
	return err;
}

static int nilfs_unlink(struct iyesde *dir, struct dentry *dentry)
{
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 0);
	if (err)
		return err;

	err = nilfs_do_unlink(dir, dentry);

	if (!err) {
		nilfs_mark_iyesde_dirty(dir);
		nilfs_mark_iyesde_dirty(d_iyesde(dentry));
		err = nilfs_transaction_commit(dir->i_sb);
	} else
		nilfs_transaction_abort(dir->i_sb);

	return err;
}

static int nilfs_rmdir(struct iyesde *dir, struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 0);
	if (err)
		return err;

	err = -ENOTEMPTY;
	if (nilfs_empty_dir(iyesde)) {
		err = nilfs_do_unlink(dir, dentry);
		if (!err) {
			iyesde->i_size = 0;
			drop_nlink(iyesde);
			nilfs_mark_iyesde_dirty(iyesde);
			drop_nlink(dir);
			nilfs_mark_iyesde_dirty(dir);
		}
	}
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;
}

static int nilfs_rename(struct iyesde *old_dir, struct dentry *old_dentry,
			struct iyesde *new_dir,	struct dentry *new_dentry,
			unsigned int flags)
{
	struct iyesde *old_iyesde = d_iyesde(old_dentry);
	struct iyesde *new_iyesde = d_iyesde(new_dentry);
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

	if (S_ISDIR(old_iyesde->i_mode)) {
		err = -EIO;
		dir_de = nilfs_dotdot(old_iyesde, &dir_page);
		if (!dir_de)
			goto out_old;
	}

	if (new_iyesde) {
		struct page *new_page;
		struct nilfs_dir_entry *new_de;

		err = -ENOTEMPTY;
		if (dir_de && !nilfs_empty_dir(new_iyesde))
			goto out_dir;

		err = -ENOENT;
		new_de = nilfs_find_entry(new_dir, &new_dentry->d_name, &new_page);
		if (!new_de)
			goto out_dir;
		nilfs_set_link(new_dir, new_de, new_page, old_iyesde);
		nilfs_mark_iyesde_dirty(new_dir);
		new_iyesde->i_ctime = current_time(new_iyesde);
		if (dir_de)
			drop_nlink(new_iyesde);
		drop_nlink(new_iyesde);
		nilfs_mark_iyesde_dirty(new_iyesde);
	} else {
		err = nilfs_add_link(new_dentry, old_iyesde);
		if (err)
			goto out_dir;
		if (dir_de) {
			inc_nlink(new_dir);
			nilfs_mark_iyesde_dirty(new_dir);
		}
	}

	/*
	 * Like most other Unix systems, set the ctime for iyesdes on a
	 * rename.
	 */
	old_iyesde->i_ctime = current_time(old_iyesde);

	nilfs_delete_entry(old_de, old_page);

	if (dir_de) {
		nilfs_set_link(old_iyesde, dir_de, dir_page, new_dir);
		drop_nlink(old_dir);
	}
	nilfs_mark_iyesde_dirty(old_dir);
	nilfs_mark_iyesde_dirty(old_iyesde);

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
	unsigned long iyes;
	struct iyesde *iyesde;
	struct qstr dotdot = QSTR_INIT("..", 2);
	struct nilfs_root *root;

	iyes = nilfs_iyesde_by_name(d_iyesde(child), &dotdot);
	if (!iyes)
		return ERR_PTR(-ENOENT);

	root = NILFS_I(d_iyesde(child))->i_root;

	iyesde = nilfs_iget(child->d_sb, root, iyes);
	if (IS_ERR(iyesde))
		return ERR_CAST(iyesde);

	return d_obtain_alias(iyesde);
}

static struct dentry *nilfs_get_dentry(struct super_block *sb, u64 cyes,
				       u64 iyes, u32 gen)
{
	struct nilfs_root *root;
	struct iyesde *iyesde;

	if (iyes < NILFS_FIRST_INO(sb) && iyes != NILFS_ROOT_INO)
		return ERR_PTR(-ESTALE);

	root = nilfs_lookup_root(sb->s_fs_info, cyes);
	if (!root)
		return ERR_PTR(-ESTALE);

	iyesde = nilfs_iget(sb, root, iyes);
	nilfs_put_root(root);

	if (IS_ERR(iyesde))
		return ERR_CAST(iyesde);
	if (gen && iyesde->i_generation != gen) {
		iput(iyesde);
		return ERR_PTR(-ESTALE);
	}
	return d_obtain_alias(iyesde);
}

static struct dentry *nilfs_fh_to_dentry(struct super_block *sb, struct fid *fh,
					 int fh_len, int fh_type)
{
	struct nilfs_fid *fid = (struct nilfs_fid *)fh;

	if (fh_len < NILFS_FID_SIZE_NON_CONNECTABLE ||
	    (fh_type != FILEID_NILFS_WITH_PARENT &&
	     fh_type != FILEID_NILFS_WITHOUT_PARENT))
		return NULL;

	return nilfs_get_dentry(sb, fid->cyes, fid->iyes, fid->gen);
}

static struct dentry *nilfs_fh_to_parent(struct super_block *sb, struct fid *fh,
					 int fh_len, int fh_type)
{
	struct nilfs_fid *fid = (struct nilfs_fid *)fh;

	if (fh_len < NILFS_FID_SIZE_CONNECTABLE ||
	    fh_type != FILEID_NILFS_WITH_PARENT)
		return NULL;

	return nilfs_get_dentry(sb, fid->cyes, fid->parent_iyes, fid->parent_gen);
}

static int nilfs_encode_fh(struct iyesde *iyesde, __u32 *fh, int *lenp,
			   struct iyesde *parent)
{
	struct nilfs_fid *fid = (struct nilfs_fid *)fh;
	struct nilfs_root *root = NILFS_I(iyesde)->i_root;
	int type;

	if (parent && *lenp < NILFS_FID_SIZE_CONNECTABLE) {
		*lenp = NILFS_FID_SIZE_CONNECTABLE;
		return FILEID_INVALID;
	}
	if (*lenp < NILFS_FID_SIZE_NON_CONNECTABLE) {
		*lenp = NILFS_FID_SIZE_NON_CONNECTABLE;
		return FILEID_INVALID;
	}

	fid->cyes = root->cyes;
	fid->iyes = iyesde->i_iyes;
	fid->gen = iyesde->i_generation;

	if (parent) {
		fid->parent_iyes = parent->i_iyes;
		fid->parent_gen = parent->i_generation;
		type = FILEID_NILFS_WITH_PARENT;
		*lenp = NILFS_FID_SIZE_CONNECTABLE;
	} else {
		type = FILEID_NILFS_WITHOUT_PARENT;
		*lenp = NILFS_FID_SIZE_NON_CONNECTABLE;
	}

	return type;
}

const struct iyesde_operations nilfs_dir_iyesde_operations = {
	.create		= nilfs_create,
	.lookup		= nilfs_lookup,
	.link		= nilfs_link,
	.unlink		= nilfs_unlink,
	.symlink	= nilfs_symlink,
	.mkdir		= nilfs_mkdir,
	.rmdir		= nilfs_rmdir,
	.mkyesd		= nilfs_mkyesd,
	.rename		= nilfs_rename,
	.setattr	= nilfs_setattr,
	.permission	= nilfs_permission,
	.fiemap		= nilfs_fiemap,
};

const struct iyesde_operations nilfs_special_iyesde_operations = {
	.setattr	= nilfs_setattr,
	.permission	= nilfs_permission,
};

const struct iyesde_operations nilfs_symlink_iyesde_operations = {
	.get_link	= page_get_link,
	.permission     = nilfs_permission,
};

const struct export_operations nilfs_export_ops = {
	.encode_fh = nilfs_encode_fh,
	.fh_to_dentry = nilfs_fh_to_dentry,
	.fh_to_parent = nilfs_fh_to_parent,
	.get_parent = nilfs_get_parent,
};
