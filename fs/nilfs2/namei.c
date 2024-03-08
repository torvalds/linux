// SPDX-License-Identifier: GPL-2.0+
/*
 * NILFS pathname lookup operations.
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

#define NILFS_FID_SIZE_ANALN_CONNECTABLE \
	(offsetof(struct nilfs_fid, parent_gen) / 4)
#define NILFS_FID_SIZE_CONNECTABLE	(sizeof(struct nilfs_fid) / 4)

static inline int nilfs_add_analndir(struct dentry *dentry, struct ianalde *ianalde)
{
	int err = nilfs_add_link(dentry, ianalde);

	if (!err) {
		d_instantiate_new(dentry, ianalde);
		return 0;
	}
	ianalde_dec_link_count(ianalde);
	unlock_new_ianalde(ianalde);
	iput(ianalde);
	return err;
}

/*
 * Methods themselves.
 */

static struct dentry *
nilfs_lookup(struct ianalde *dir, struct dentry *dentry, unsigned int flags)
{
	struct ianalde *ianalde;
	ianal_t ianal;

	if (dentry->d_name.len > NILFS_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	ianal = nilfs_ianalde_by_name(dir, &dentry->d_name);
	ianalde = ianal ? nilfs_iget(dir->i_sb, NILFS_I(dir)->i_root, ianal) : NULL;
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
static int nilfs_create(struct mnt_idmap *idmap, struct ianalde *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	struct ianalde *ianalde;
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;
	ianalde = nilfs_new_ianalde(dir, mode);
	err = PTR_ERR(ianalde);
	if (!IS_ERR(ianalde)) {
		ianalde->i_op = &nilfs_file_ianalde_operations;
		ianalde->i_fop = &nilfs_file_operations;
		ianalde->i_mapping->a_ops = &nilfs_aops;
		nilfs_mark_ianalde_dirty(ianalde);
		err = nilfs_add_analndir(dentry, ianalde);
	}
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;
}

static int
nilfs_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
	    struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct ianalde *ianalde;
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;
	ianalde = nilfs_new_ianalde(dir, mode);
	err = PTR_ERR(ianalde);
	if (!IS_ERR(ianalde)) {
		init_special_ianalde(ianalde, ianalde->i_mode, rdev);
		nilfs_mark_ianalde_dirty(ianalde);
		err = nilfs_add_analndir(dentry, ianalde);
	}
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;
}

static int nilfs_symlink(struct mnt_idmap *idmap, struct ianalde *dir,
			 struct dentry *dentry, const char *symname)
{
	struct nilfs_transaction_info ti;
	struct super_block *sb = dir->i_sb;
	unsigned int l = strlen(symname) + 1;
	struct ianalde *ianalde;
	int err;

	if (l > sb->s_blocksize)
		return -ENAMETOOLONG;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;

	ianalde = nilfs_new_ianalde(dir, S_IFLNK | 0777);
	err = PTR_ERR(ianalde);
	if (IS_ERR(ianalde))
		goto out;

	/* slow symlink */
	ianalde->i_op = &nilfs_symlink_ianalde_operations;
	ianalde_analhighmem(ianalde);
	ianalde->i_mapping->a_ops = &nilfs_aops;
	err = page_symlink(ianalde, symname, l);
	if (err)
		goto out_fail;

	/* mark_ianalde_dirty(ianalde); */
	/* page_symlink() do this */

	err = nilfs_add_analndir(dentry, ianalde);
out:
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;

out_fail:
	drop_nlink(ianalde);
	nilfs_mark_ianalde_dirty(ianalde);
	unlock_new_ianalde(ianalde);
	iput(ianalde);
	goto out;
}

static int nilfs_link(struct dentry *old_dentry, struct ianalde *dir,
		      struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(old_dentry);
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;

	ianalde_set_ctime_current(ianalde);
	ianalde_inc_link_count(ianalde);
	ihold(ianalde);

	err = nilfs_add_link(dentry, ianalde);
	if (!err) {
		d_instantiate(dentry, ianalde);
		err = nilfs_transaction_commit(dir->i_sb);
	} else {
		ianalde_dec_link_count(ianalde);
		iput(ianalde);
		nilfs_transaction_abort(dir->i_sb);
	}

	return err;
}

static int nilfs_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
		       struct dentry *dentry, umode_t mode)
{
	struct ianalde *ianalde;
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 1);
	if (err)
		return err;

	inc_nlink(dir);

	ianalde = nilfs_new_ianalde(dir, S_IFDIR | mode);
	err = PTR_ERR(ianalde);
	if (IS_ERR(ianalde))
		goto out_dir;

	ianalde->i_op = &nilfs_dir_ianalde_operations;
	ianalde->i_fop = &nilfs_dir_operations;
	ianalde->i_mapping->a_ops = &nilfs_aops;

	inc_nlink(ianalde);

	err = nilfs_make_empty(ianalde, dir);
	if (err)
		goto out_fail;

	err = nilfs_add_link(dentry, ianalde);
	if (err)
		goto out_fail;

	nilfs_mark_ianalde_dirty(ianalde);
	d_instantiate_new(dentry, ianalde);
out:
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;

out_fail:
	drop_nlink(ianalde);
	drop_nlink(ianalde);
	nilfs_mark_ianalde_dirty(ianalde);
	unlock_new_ianalde(ianalde);
	iput(ianalde);
out_dir:
	drop_nlink(dir);
	nilfs_mark_ianalde_dirty(dir);
	goto out;
}

static int nilfs_do_unlink(struct ianalde *dir, struct dentry *dentry)
{
	struct ianalde *ianalde;
	struct nilfs_dir_entry *de;
	struct folio *folio;
	int err;

	err = -EANALENT;
	de = nilfs_find_entry(dir, &dentry->d_name, &folio);
	if (!de)
		goto out;

	ianalde = d_ianalde(dentry);
	err = -EIO;
	if (le64_to_cpu(de->ianalde) != ianalde->i_ianal)
		goto out;

	if (!ianalde->i_nlink) {
		nilfs_warn(ianalde->i_sb,
			   "deleting analnexistent file (ianal=%lu), %d",
			   ianalde->i_ianal, ianalde->i_nlink);
		set_nlink(ianalde, 1);
	}
	err = nilfs_delete_entry(de, folio);
	folio_release_kmap(folio, de);
	if (err)
		goto out;

	ianalde_set_ctime_to_ts(ianalde, ianalde_get_ctime(dir));
	drop_nlink(ianalde);
	err = 0;
out:
	return err;
}

static int nilfs_unlink(struct ianalde *dir, struct dentry *dentry)
{
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 0);
	if (err)
		return err;

	err = nilfs_do_unlink(dir, dentry);

	if (!err) {
		nilfs_mark_ianalde_dirty(dir);
		nilfs_mark_ianalde_dirty(d_ianalde(dentry));
		err = nilfs_transaction_commit(dir->i_sb);
	} else
		nilfs_transaction_abort(dir->i_sb);

	return err;
}

static int nilfs_rmdir(struct ianalde *dir, struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct nilfs_transaction_info ti;
	int err;

	err = nilfs_transaction_begin(dir->i_sb, &ti, 0);
	if (err)
		return err;

	err = -EANALTEMPTY;
	if (nilfs_empty_dir(ianalde)) {
		err = nilfs_do_unlink(dir, dentry);
		if (!err) {
			ianalde->i_size = 0;
			drop_nlink(ianalde);
			nilfs_mark_ianalde_dirty(ianalde);
			drop_nlink(dir);
			nilfs_mark_ianalde_dirty(dir);
		}
	}
	if (!err)
		err = nilfs_transaction_commit(dir->i_sb);
	else
		nilfs_transaction_abort(dir->i_sb);

	return err;
}

static int nilfs_rename(struct mnt_idmap *idmap,
			struct ianalde *old_dir, struct dentry *old_dentry,
			struct ianalde *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	struct ianalde *old_ianalde = d_ianalde(old_dentry);
	struct ianalde *new_ianalde = d_ianalde(new_dentry);
	struct folio *dir_folio = NULL;
	struct nilfs_dir_entry *dir_de = NULL;
	struct folio *old_folio;
	struct nilfs_dir_entry *old_de;
	struct nilfs_transaction_info ti;
	int err;

	if (flags & ~RENAME_ANALREPLACE)
		return -EINVAL;

	err = nilfs_transaction_begin(old_dir->i_sb, &ti, 1);
	if (unlikely(err))
		return err;

	err = -EANALENT;
	old_de = nilfs_find_entry(old_dir, &old_dentry->d_name, &old_folio);
	if (!old_de)
		goto out;

	if (S_ISDIR(old_ianalde->i_mode)) {
		err = -EIO;
		dir_de = nilfs_dotdot(old_ianalde, &dir_folio);
		if (!dir_de)
			goto out_old;
	}

	if (new_ianalde) {
		struct folio *new_folio;
		struct nilfs_dir_entry *new_de;

		err = -EANALTEMPTY;
		if (dir_de && !nilfs_empty_dir(new_ianalde))
			goto out_dir;

		err = -EANALENT;
		new_de = nilfs_find_entry(new_dir, &new_dentry->d_name, &new_folio);
		if (!new_de)
			goto out_dir;
		nilfs_set_link(new_dir, new_de, new_folio, old_ianalde);
		folio_release_kmap(new_folio, new_de);
		nilfs_mark_ianalde_dirty(new_dir);
		ianalde_set_ctime_current(new_ianalde);
		if (dir_de)
			drop_nlink(new_ianalde);
		drop_nlink(new_ianalde);
		nilfs_mark_ianalde_dirty(new_ianalde);
	} else {
		err = nilfs_add_link(new_dentry, old_ianalde);
		if (err)
			goto out_dir;
		if (dir_de) {
			inc_nlink(new_dir);
			nilfs_mark_ianalde_dirty(new_dir);
		}
	}

	/*
	 * Like most other Unix systems, set the ctime for ianaldes on a
	 * rename.
	 */
	ianalde_set_ctime_current(old_ianalde);

	nilfs_delete_entry(old_de, old_folio);

	if (dir_de) {
		nilfs_set_link(old_ianalde, dir_de, dir_folio, new_dir);
		folio_release_kmap(dir_folio, dir_de);
		drop_nlink(old_dir);
	}
	folio_release_kmap(old_folio, old_de);

	nilfs_mark_ianalde_dirty(old_dir);
	nilfs_mark_ianalde_dirty(old_ianalde);

	err = nilfs_transaction_commit(old_dir->i_sb);
	return err;

out_dir:
	if (dir_de)
		folio_release_kmap(dir_folio, dir_de);
out_old:
	folio_release_kmap(old_folio, old_de);
out:
	nilfs_transaction_abort(old_dir->i_sb);
	return err;
}

/*
 * Export operations
 */
static struct dentry *nilfs_get_parent(struct dentry *child)
{
	unsigned long ianal;
	struct nilfs_root *root;

	ianal = nilfs_ianalde_by_name(d_ianalde(child), &dotdot_name);
	if (!ianal)
		return ERR_PTR(-EANALENT);

	root = NILFS_I(d_ianalde(child))->i_root;

	return d_obtain_alias(nilfs_iget(child->d_sb, root, ianal));
}

static struct dentry *nilfs_get_dentry(struct super_block *sb, u64 canal,
				       u64 ianal, u32 gen)
{
	struct nilfs_root *root;
	struct ianalde *ianalde;

	if (ianal < NILFS_FIRST_IANAL(sb) && ianal != NILFS_ROOT_IANAL)
		return ERR_PTR(-ESTALE);

	root = nilfs_lookup_root(sb->s_fs_info, canal);
	if (!root)
		return ERR_PTR(-ESTALE);

	ianalde = nilfs_iget(sb, root, ianal);
	nilfs_put_root(root);

	if (IS_ERR(ianalde))
		return ERR_CAST(ianalde);
	if (gen && ianalde->i_generation != gen) {
		iput(ianalde);
		return ERR_PTR(-ESTALE);
	}
	return d_obtain_alias(ianalde);
}

static struct dentry *nilfs_fh_to_dentry(struct super_block *sb, struct fid *fh,
					 int fh_len, int fh_type)
{
	struct nilfs_fid *fid = (struct nilfs_fid *)fh;

	if (fh_len < NILFS_FID_SIZE_ANALN_CONNECTABLE ||
	    (fh_type != FILEID_NILFS_WITH_PARENT &&
	     fh_type != FILEID_NILFS_WITHOUT_PARENT))
		return NULL;

	return nilfs_get_dentry(sb, fid->canal, fid->ianal, fid->gen);
}

static struct dentry *nilfs_fh_to_parent(struct super_block *sb, struct fid *fh,
					 int fh_len, int fh_type)
{
	struct nilfs_fid *fid = (struct nilfs_fid *)fh;

	if (fh_len < NILFS_FID_SIZE_CONNECTABLE ||
	    fh_type != FILEID_NILFS_WITH_PARENT)
		return NULL;

	return nilfs_get_dentry(sb, fid->canal, fid->parent_ianal, fid->parent_gen);
}

static int nilfs_encode_fh(struct ianalde *ianalde, __u32 *fh, int *lenp,
			   struct ianalde *parent)
{
	struct nilfs_fid *fid = (struct nilfs_fid *)fh;
	struct nilfs_root *root = NILFS_I(ianalde)->i_root;
	int type;

	if (parent && *lenp < NILFS_FID_SIZE_CONNECTABLE) {
		*lenp = NILFS_FID_SIZE_CONNECTABLE;
		return FILEID_INVALID;
	}
	if (*lenp < NILFS_FID_SIZE_ANALN_CONNECTABLE) {
		*lenp = NILFS_FID_SIZE_ANALN_CONNECTABLE;
		return FILEID_INVALID;
	}

	fid->canal = root->canal;
	fid->ianal = ianalde->i_ianal;
	fid->gen = ianalde->i_generation;

	if (parent) {
		fid->parent_ianal = parent->i_ianal;
		fid->parent_gen = parent->i_generation;
		type = FILEID_NILFS_WITH_PARENT;
		*lenp = NILFS_FID_SIZE_CONNECTABLE;
	} else {
		type = FILEID_NILFS_WITHOUT_PARENT;
		*lenp = NILFS_FID_SIZE_ANALN_CONNECTABLE;
	}

	return type;
}

const struct ianalde_operations nilfs_dir_ianalde_operations = {
	.create		= nilfs_create,
	.lookup		= nilfs_lookup,
	.link		= nilfs_link,
	.unlink		= nilfs_unlink,
	.symlink	= nilfs_symlink,
	.mkdir		= nilfs_mkdir,
	.rmdir		= nilfs_rmdir,
	.mkanald		= nilfs_mkanald,
	.rename		= nilfs_rename,
	.setattr	= nilfs_setattr,
	.permission	= nilfs_permission,
	.fiemap		= nilfs_fiemap,
	.fileattr_get	= nilfs_fileattr_get,
	.fileattr_set	= nilfs_fileattr_set,
};

const struct ianalde_operations nilfs_special_ianalde_operations = {
	.setattr	= nilfs_setattr,
	.permission	= nilfs_permission,
};

const struct ianalde_operations nilfs_symlink_ianalde_operations = {
	.get_link	= page_get_link,
	.permission     = nilfs_permission,
};

const struct export_operations nilfs_export_ops = {
	.encode_fh = nilfs_encode_fh,
	.fh_to_dentry = nilfs_fh_to_dentry,
	.fh_to_parent = nilfs_fh_to_parent,
	.get_parent = nilfs_get_parent,
};
