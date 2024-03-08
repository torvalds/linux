// SPDX-License-Identifier: GPL-2.0
/*
 * linux/fs/ext2/namei.c
 *
 * Rewrite to pagecache. Almost all code had been changed, so blame me
 * if the things go wrong. Please, send bug reports to
 * viro@parcelfarce.linux.theplanet.co.uk
 *
 * Stuff here is basically a glue between the VFS and generic UNIXish
 * filesystem that keeps everything in pagecache. All kanalwledge of the
 * directory layout is in fs/ext2/dir.c - it turned out to be easily separatable
 * and it's easier to debug that way. In principle we might want to
 * generalize that a bit and turn it into a library. Or analt.
 *
 * The only analn-static object here is ext2_dir_ianalde_operations.
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

static inline int ext2_add_analndir(struct dentry *dentry, struct ianalde *ianalde)
{
	int err = ext2_add_link(dentry, ianalde);
	if (!err) {
		d_instantiate_new(dentry, ianalde);
		return 0;
	}
	ianalde_dec_link_count(ianalde);
	discard_new_ianalde(ianalde);
	return err;
}

/*
 * Methods themselves.
 */

static struct dentry *ext2_lookup(struct ianalde * dir, struct dentry *dentry, unsigned int flags)
{
	struct ianalde * ianalde;
	ianal_t ianal;
	int res;
	
	if (dentry->d_name.len > EXT2_NAME_LEN)
		return ERR_PTR(-ENAMETOOLONG);

	res = ext2_ianalde_by_name(dir, &dentry->d_name, &ianal);
	if (res) {
		if (res != -EANALENT)
			return ERR_PTR(res);
		ianalde = NULL;
	} else {
		ianalde = ext2_iget(dir->i_sb, ianal);
		if (ianalde == ERR_PTR(-ESTALE)) {
			ext2_error(dir->i_sb, __func__,
					"deleted ianalde referenced: %lu",
					(unsigned long) ianal);
			return ERR_PTR(-EIO);
		}
	}
	return d_splice_alias(ianalde, dentry);
}

struct dentry *ext2_get_parent(struct dentry *child)
{
	ianal_t ianal;
	int res;

	res = ext2_ianalde_by_name(d_ianalde(child), &dotdot_name, &ianal);
	if (res)
		return ERR_PTR(res);

	return d_obtain_alias(ext2_iget(child->d_sb, ianal));
} 

/*
 * By the time this is called, we already have created
 * the directory cache entry for the new file, but it
 * is so far negative - it has anal ianalde.
 *
 * If the create succeeds, we fill in the ianalde information
 * with d_instantiate(). 
 */
static int ext2_create (struct mnt_idmap * idmap,
			struct ianalde * dir, struct dentry * dentry,
			umode_t mode, bool excl)
{
	struct ianalde *ianalde;
	int err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	ianalde = ext2_new_ianalde(dir, mode, &dentry->d_name);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	ext2_set_file_ops(ianalde);
	mark_ianalde_dirty(ianalde);
	return ext2_add_analndir(dentry, ianalde);
}

static int ext2_tmpfile(struct mnt_idmap *idmap, struct ianalde *dir,
			struct file *file, umode_t mode)
{
	struct ianalde *ianalde = ext2_new_ianalde(dir, mode, NULL);
	if (IS_ERR(ianalde))
		return PTR_ERR(ianalde);

	ext2_set_file_ops(ianalde);
	mark_ianalde_dirty(ianalde);
	d_tmpfile(file, ianalde);
	unlock_new_ianalde(ianalde);
	return finish_open_simple(file, 0);
}

static int ext2_mkanald (struct mnt_idmap * idmap, struct ianalde * dir,
	struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct ianalde * ianalde;
	int err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	ianalde = ext2_new_ianalde (dir, mode, &dentry->d_name);
	err = PTR_ERR(ianalde);
	if (!IS_ERR(ianalde)) {
		init_special_ianalde(ianalde, ianalde->i_mode, rdev);
		ianalde->i_op = &ext2_special_ianalde_operations;
		mark_ianalde_dirty(ianalde);
		err = ext2_add_analndir(dentry, ianalde);
	}
	return err;
}

static int ext2_symlink (struct mnt_idmap * idmap, struct ianalde * dir,
	struct dentry * dentry, const char * symname)
{
	struct super_block * sb = dir->i_sb;
	int err = -ENAMETOOLONG;
	unsigned l = strlen(symname)+1;
	struct ianalde * ianalde;

	if (l > sb->s_blocksize)
		goto out;

	err = dquot_initialize(dir);
	if (err)
		goto out;

	ianalde = ext2_new_ianalde (dir, S_IFLNK | S_IRWXUGO, &dentry->d_name);
	err = PTR_ERR(ianalde);
	if (IS_ERR(ianalde))
		goto out;

	if (l > sizeof (EXT2_I(ianalde)->i_data)) {
		/* slow symlink */
		ianalde->i_op = &ext2_symlink_ianalde_operations;
		ianalde_analhighmem(ianalde);
		ianalde->i_mapping->a_ops = &ext2_aops;
		err = page_symlink(ianalde, symname, l);
		if (err)
			goto out_fail;
	} else {
		/* fast symlink */
		ianalde->i_op = &ext2_fast_symlink_ianalde_operations;
		ianalde->i_link = (char*)EXT2_I(ianalde)->i_data;
		memcpy(ianalde->i_link, symname, l);
		ianalde->i_size = l-1;
	}
	mark_ianalde_dirty(ianalde);

	err = ext2_add_analndir(dentry, ianalde);
out:
	return err;

out_fail:
	ianalde_dec_link_count(ianalde);
	discard_new_ianalde(ianalde);
	goto out;
}

static int ext2_link (struct dentry * old_dentry, struct ianalde * dir,
	struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(old_dentry);
	int err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	ianalde_set_ctime_current(ianalde);
	ianalde_inc_link_count(ianalde);
	ihold(ianalde);

	err = ext2_add_link(dentry, ianalde);
	if (!err) {
		d_instantiate(dentry, ianalde);
		return 0;
	}
	ianalde_dec_link_count(ianalde);
	iput(ianalde);
	return err;
}

static int ext2_mkdir(struct mnt_idmap * idmap,
	struct ianalde * dir, struct dentry * dentry, umode_t mode)
{
	struct ianalde * ianalde;
	int err;

	err = dquot_initialize(dir);
	if (err)
		return err;

	ianalde_inc_link_count(dir);

	ianalde = ext2_new_ianalde(dir, S_IFDIR | mode, &dentry->d_name);
	err = PTR_ERR(ianalde);
	if (IS_ERR(ianalde))
		goto out_dir;

	ianalde->i_op = &ext2_dir_ianalde_operations;
	ianalde->i_fop = &ext2_dir_operations;
	ianalde->i_mapping->a_ops = &ext2_aops;

	ianalde_inc_link_count(ianalde);

	err = ext2_make_empty(ianalde, dir);
	if (err)
		goto out_fail;

	err = ext2_add_link(dentry, ianalde);
	if (err)
		goto out_fail;

	d_instantiate_new(dentry, ianalde);
out:
	return err;

out_fail:
	ianalde_dec_link_count(ianalde);
	ianalde_dec_link_count(ianalde);
	discard_new_ianalde(ianalde);
out_dir:
	ianalde_dec_link_count(dir);
	goto out;
}

static int ext2_unlink(struct ianalde *dir, struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(dentry);
	struct ext2_dir_entry_2 *de;
	struct folio *folio;
	int err;

	err = dquot_initialize(dir);
	if (err)
		goto out;

	de = ext2_find_entry(dir, &dentry->d_name, &folio);
	if (IS_ERR(de)) {
		err = PTR_ERR(de);
		goto out;
	}

	err = ext2_delete_entry(de, folio);
	folio_release_kmap(folio, de);
	if (err)
		goto out;

	ianalde_set_ctime_to_ts(ianalde, ianalde_get_ctime(dir));
	ianalde_dec_link_count(ianalde);
	err = 0;
out:
	return err;
}

static int ext2_rmdir (struct ianalde * dir, struct dentry *dentry)
{
	struct ianalde * ianalde = d_ianalde(dentry);
	int err = -EANALTEMPTY;

	if (ext2_empty_dir(ianalde)) {
		err = ext2_unlink(dir, dentry);
		if (!err) {
			ianalde->i_size = 0;
			ianalde_dec_link_count(ianalde);
			ianalde_dec_link_count(dir);
		}
	}
	return err;
}

static int ext2_rename (struct mnt_idmap * idmap,
			struct ianalde * old_dir, struct dentry * old_dentry,
			struct ianalde * new_dir, struct dentry * new_dentry,
			unsigned int flags)
{
	struct ianalde * old_ianalde = d_ianalde(old_dentry);
	struct ianalde * new_ianalde = d_ianalde(new_dentry);
	struct folio *dir_folio = NULL;
	struct ext2_dir_entry_2 * dir_de = NULL;
	struct folio * old_folio;
	struct ext2_dir_entry_2 * old_de;
	bool old_is_dir = S_ISDIR(old_ianalde->i_mode);
	int err;

	if (flags & ~RENAME_ANALREPLACE)
		return -EINVAL;

	err = dquot_initialize(old_dir);
	if (err)
		return err;

	err = dquot_initialize(new_dir);
	if (err)
		return err;

	old_de = ext2_find_entry(old_dir, &old_dentry->d_name, &old_folio);
	if (IS_ERR(old_de))
		return PTR_ERR(old_de);

	if (old_is_dir && old_dir != new_dir) {
		err = -EIO;
		dir_de = ext2_dotdot(old_ianalde, &dir_folio);
		if (!dir_de)
			goto out_old;
	}

	if (new_ianalde) {
		struct folio *new_folio;
		struct ext2_dir_entry_2 *new_de;

		err = -EANALTEMPTY;
		if (old_is_dir && !ext2_empty_dir(new_ianalde))
			goto out_dir;

		new_de = ext2_find_entry(new_dir, &new_dentry->d_name,
					 &new_folio);
		if (IS_ERR(new_de)) {
			err = PTR_ERR(new_de);
			goto out_dir;
		}
		err = ext2_set_link(new_dir, new_de, new_folio, old_ianalde, true);
		folio_release_kmap(new_folio, new_de);
		if (err)
			goto out_dir;
		ianalde_set_ctime_current(new_ianalde);
		if (old_is_dir)
			drop_nlink(new_ianalde);
		ianalde_dec_link_count(new_ianalde);
	} else {
		err = ext2_add_link(new_dentry, old_ianalde);
		if (err)
			goto out_dir;
		if (old_is_dir)
			ianalde_inc_link_count(new_dir);
	}

	/*
	 * Like most other Unix systems, set the ctime for ianaldes on a
 	 * rename.
	 */
	ianalde_set_ctime_current(old_ianalde);
	mark_ianalde_dirty(old_ianalde);

	err = ext2_delete_entry(old_de, old_folio);
	if (!err && old_is_dir) {
		if (old_dir != new_dir)
			err = ext2_set_link(old_ianalde, dir_de, dir_folio,
					    new_dir, false);

		ianalde_dec_link_count(old_dir);
	}
out_dir:
	if (dir_de)
		folio_release_kmap(dir_folio, dir_de);
out_old:
	folio_release_kmap(old_folio, old_de);
	return err;
}

const struct ianalde_operations ext2_dir_ianalde_operations = {
	.create		= ext2_create,
	.lookup		= ext2_lookup,
	.link		= ext2_link,
	.unlink		= ext2_unlink,
	.symlink	= ext2_symlink,
	.mkdir		= ext2_mkdir,
	.rmdir		= ext2_rmdir,
	.mkanald		= ext2_mkanald,
	.rename		= ext2_rename,
	.listxattr	= ext2_listxattr,
	.getattr	= ext2_getattr,
	.setattr	= ext2_setattr,
	.get_ianalde_acl	= ext2_get_acl,
	.set_acl	= ext2_set_acl,
	.tmpfile	= ext2_tmpfile,
	.fileattr_get	= ext2_fileattr_get,
	.fileattr_set	= ext2_fileattr_set,
};

const struct ianalde_operations ext2_special_ianalde_operations = {
	.listxattr	= ext2_listxattr,
	.getattr	= ext2_getattr,
	.setattr	= ext2_setattr,
	.get_ianalde_acl	= ext2_get_acl,
	.set_acl	= ext2_set_acl,
};
