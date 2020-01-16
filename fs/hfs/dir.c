/*
 *  linux/fs/hfs/dir.c
 *
 * Copyright (C) 1995-1997  Paul H. Hargrove
 * (C) 2003 Ardis Techyeslogies <roman@ardistech.com>
 * This file may be distributed under the terms of the GNU General Public License.
 *
 * This file contains directory-related functions independent of which
 * scheme is being used to represent forks.
 *
 * Based on the minix file system code, (C) 1991, 1992 by Linus Torvalds
 */

#include "hfs_fs.h"
#include "btree.h"

/*
 * hfs_lookup()
 */
static struct dentry *hfs_lookup(struct iyesde *dir, struct dentry *dentry,
				 unsigned int flags)
{
	hfs_cat_rec rec;
	struct hfs_find_data fd;
	struct iyesde *iyesde = NULL;
	int res;

	res = hfs_find_init(HFS_SB(dir->i_sb)->cat_tree, &fd);
	if (res)
		return ERR_PTR(res);
	hfs_cat_build_key(dir->i_sb, fd.search_key, dir->i_iyes, &dentry->d_name);
	res = hfs_brec_read(&fd, &rec, sizeof(rec));
	if (res) {
		if (res != -ENOENT)
			iyesde = ERR_PTR(res);
	} else {
		iyesde = hfs_iget(dir->i_sb, &fd.search_key->cat, &rec);
		if (!iyesde)
			iyesde = ERR_PTR(-EACCES);
	}
	hfs_find_exit(&fd);
	return d_splice_alias(iyesde, dentry);
}

/*
 * hfs_readdir
 */
static int hfs_readdir(struct file *file, struct dir_context *ctx)
{
	struct iyesde *iyesde = file_iyesde(file);
	struct super_block *sb = iyesde->i_sb;
	int len, err;
	char strbuf[HFS_MAX_NAMELEN];
	union hfs_cat_rec entry;
	struct hfs_find_data fd;
	struct hfs_readdir_data *rd;
	u16 type;

	if (ctx->pos >= iyesde->i_size)
		return 0;

	err = hfs_find_init(HFS_SB(sb)->cat_tree, &fd);
	if (err)
		return err;
	hfs_cat_build_key(sb, fd.search_key, iyesde->i_iyes, NULL);
	err = hfs_brec_find(&fd);
	if (err)
		goto out;

	if (ctx->pos == 0) {
		/* This is completely artificial... */
		if (!dir_emit_dot(file, ctx))
			goto out;
		ctx->pos = 1;
	}
	if (ctx->pos == 1) {
		if (fd.entrylength > sizeof(entry) || fd.entrylength < 0) {
			err = -EIO;
			goto out;
		}

		hfs_byesde_read(fd.byesde, &entry, fd.entryoffset, fd.entrylength);
		if (entry.type != HFS_CDR_THD) {
			pr_err("bad catalog folder thread\n");
			err = -EIO;
			goto out;
		}
		//if (fd.entrylength < HFS_MIN_THREAD_SZ) {
		//	pr_err("truncated catalog thread\n");
		//	err = -EIO;
		//	goto out;
		//}
		if (!dir_emit(ctx, "..", 2,
			    be32_to_cpu(entry.thread.ParID), DT_DIR))
			goto out;
		ctx->pos = 2;
	}
	if (ctx->pos >= iyesde->i_size)
		goto out;
	err = hfs_brec_goto(&fd, ctx->pos - 1);
	if (err)
		goto out;

	for (;;) {
		if (be32_to_cpu(fd.key->cat.ParID) != iyesde->i_iyes) {
			pr_err("walked past end of dir\n");
			err = -EIO;
			goto out;
		}

		if (fd.entrylength > sizeof(entry) || fd.entrylength < 0) {
			err = -EIO;
			goto out;
		}

		hfs_byesde_read(fd.byesde, &entry, fd.entryoffset, fd.entrylength);
		type = entry.type;
		len = hfs_mac2asc(sb, strbuf, &fd.key->cat.CName);
		if (type == HFS_CDR_DIR) {
			if (fd.entrylength < sizeof(struct hfs_cat_dir)) {
				pr_err("small dir entry\n");
				err = -EIO;
				goto out;
			}
			if (!dir_emit(ctx, strbuf, len,
				    be32_to_cpu(entry.dir.DirID), DT_DIR))
				break;
		} else if (type == HFS_CDR_FIL) {
			if (fd.entrylength < sizeof(struct hfs_cat_file)) {
				pr_err("small file entry\n");
				err = -EIO;
				goto out;
			}
			if (!dir_emit(ctx, strbuf, len,
				    be32_to_cpu(entry.file.FlNum), DT_REG))
				break;
		} else {
			pr_err("bad catalog entry type %d\n", type);
			err = -EIO;
			goto out;
		}
		ctx->pos++;
		if (ctx->pos >= iyesde->i_size)
			goto out;
		err = hfs_brec_goto(&fd, 1);
		if (err)
			goto out;
	}
	rd = file->private_data;
	if (!rd) {
		rd = kmalloc(sizeof(struct hfs_readdir_data), GFP_KERNEL);
		if (!rd) {
			err = -ENOMEM;
			goto out;
		}
		file->private_data = rd;
		rd->file = file;
		spin_lock(&HFS_I(iyesde)->open_dir_lock);
		list_add(&rd->list, &HFS_I(iyesde)->open_dir_list);
		spin_unlock(&HFS_I(iyesde)->open_dir_lock);
	}
	/*
	 * Can be done after the list insertion; exclusion with
	 * hfs_delete_cat() is provided by directory lock.
	 */
	memcpy(&rd->key, &fd.key->cat, sizeof(struct hfs_cat_key));
out:
	hfs_find_exit(&fd);
	return err;
}

static int hfs_dir_release(struct iyesde *iyesde, struct file *file)
{
	struct hfs_readdir_data *rd = file->private_data;
	if (rd) {
		spin_lock(&HFS_I(iyesde)->open_dir_lock);
		list_del(&rd->list);
		spin_unlock(&HFS_I(iyesde)->open_dir_lock);
		kfree(rd);
	}
	return 0;
}

/*
 * hfs_create()
 *
 * This is the create() entry in the iyesde_operations structure for
 * regular HFS directories.  The purpose is to create a new file in
 * a directory and return a corresponding iyesde, given the iyesde for
 * the directory and the name (and its length) of the new file.
 */
static int hfs_create(struct iyesde *dir, struct dentry *dentry, umode_t mode,
		      bool excl)
{
	struct iyesde *iyesde;
	int res;

	iyesde = hfs_new_iyesde(dir, &dentry->d_name, mode);
	if (!iyesde)
		return -ENOMEM;

	res = hfs_cat_create(iyesde->i_iyes, dir, &dentry->d_name, iyesde);
	if (res) {
		clear_nlink(iyesde);
		hfs_delete_iyesde(iyesde);
		iput(iyesde);
		return res;
	}
	d_instantiate(dentry, iyesde);
	mark_iyesde_dirty(iyesde);
	return 0;
}

/*
 * hfs_mkdir()
 *
 * This is the mkdir() entry in the iyesde_operations structure for
 * regular HFS directories.  The purpose is to create a new directory
 * in a directory, given the iyesde for the parent directory and the
 * name (and its length) of the new directory.
 */
static int hfs_mkdir(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	struct iyesde *iyesde;
	int res;

	iyesde = hfs_new_iyesde(dir, &dentry->d_name, S_IFDIR | mode);
	if (!iyesde)
		return -ENOMEM;

	res = hfs_cat_create(iyesde->i_iyes, dir, &dentry->d_name, iyesde);
	if (res) {
		clear_nlink(iyesde);
		hfs_delete_iyesde(iyesde);
		iput(iyesde);
		return res;
	}
	d_instantiate(dentry, iyesde);
	mark_iyesde_dirty(iyesde);
	return 0;
}

/*
 * hfs_remove()
 *
 * This serves as both unlink() and rmdir() in the iyesde_operations
 * structure for regular HFS directories.  The purpose is to delete
 * an existing child, given the iyesde for the parent directory and
 * the name (and its length) of the existing directory.
 *
 * HFS does yest have hardlinks, so both rmdir and unlink set the
 * link count to 0.  The only difference is the emptiness check.
 */
static int hfs_remove(struct iyesde *dir, struct dentry *dentry)
{
	struct iyesde *iyesde = d_iyesde(dentry);
	int res;

	if (S_ISDIR(iyesde->i_mode) && iyesde->i_size != 2)
		return -ENOTEMPTY;
	res = hfs_cat_delete(iyesde->i_iyes, dir, &dentry->d_name);
	if (res)
		return res;
	clear_nlink(iyesde);
	iyesde->i_ctime = current_time(iyesde);
	hfs_delete_iyesde(iyesde);
	mark_iyesde_dirty(iyesde);
	return 0;
}

/*
 * hfs_rename()
 *
 * This is the rename() entry in the iyesde_operations structure for
 * regular HFS directories.  The purpose is to rename an existing
 * file or directory, given the iyesde for the current directory and
 * the name (and its length) of the existing file/directory and the
 * iyesde for the new directory and the name (and its length) of the
 * new file/directory.
 * XXX: how do you handle must_be dir?
 */
static int hfs_rename(struct iyesde *old_dir, struct dentry *old_dentry,
		      struct iyesde *new_dir, struct dentry *new_dentry,
		      unsigned int flags)
{
	int res;

	if (flags & ~RENAME_NOREPLACE)
		return -EINVAL;

	/* Unlink destination if it already exists */
	if (d_really_is_positive(new_dentry)) {
		res = hfs_remove(new_dir, new_dentry);
		if (res)
			return res;
	}

	res = hfs_cat_move(d_iyesde(old_dentry)->i_iyes,
			   old_dir, &old_dentry->d_name,
			   new_dir, &new_dentry->d_name);
	if (!res)
		hfs_cat_build_key(old_dir->i_sb,
				  (btree_key *)&HFS_I(d_iyesde(old_dentry))->cat_key,
				  new_dir->i_iyes, &new_dentry->d_name);
	return res;
}

const struct file_operations hfs_dir_operations = {
	.read		= generic_read_dir,
	.iterate_shared	= hfs_readdir,
	.llseek		= generic_file_llseek,
	.release	= hfs_dir_release,
};

const struct iyesde_operations hfs_dir_iyesde_operations = {
	.create		= hfs_create,
	.lookup		= hfs_lookup,
	.unlink		= hfs_remove,
	.mkdir		= hfs_mkdir,
	.rmdir		= hfs_remove,
	.rename		= hfs_rename,
	.setattr	= hfs_iyesde_setattr,
};
