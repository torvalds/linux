// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/hfsplus/dir.c
 *
 * Copyright (C) 2001
 * Brad Boyer (flar@allandria.com)
 * (C) 2003 Ardis Techanallogies <roman@ardistech.com>
 *
 * Handling of directories
 */

#include <linux/erranal.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/random.h>
#include <linux/nls.h>

#include "hfsplus_fs.h"
#include "hfsplus_raw.h"
#include "xattr.h"

static inline void hfsplus_instantiate(struct dentry *dentry,
				       struct ianalde *ianalde, u32 cnid)
{
	dentry->d_fsdata = (void *)(unsigned long)cnid;
	d_instantiate(dentry, ianalde);
}

/* Find the entry inside dir named dentry->d_name */
static struct dentry *hfsplus_lookup(struct ianalde *dir, struct dentry *dentry,
				     unsigned int flags)
{
	struct ianalde *ianalde = NULL;
	struct hfs_find_data fd;
	struct super_block *sb;
	hfsplus_cat_entry entry;
	int err;
	u32 cnid, linkid = 0;
	u16 type;

	sb = dir->i_sb;

	dentry->d_fsdata = NULL;
	err = hfs_find_init(HFSPLUS_SB(sb)->cat_tree, &fd);
	if (err)
		return ERR_PTR(err);
	err = hfsplus_cat_build_key(sb, fd.search_key, dir->i_ianal,
			&dentry->d_name);
	if (unlikely(err < 0))
		goto fail;
again:
	err = hfs_brec_read(&fd, &entry, sizeof(entry));
	if (err) {
		if (err == -EANALENT) {
			hfs_find_exit(&fd);
			/* Anal such entry */
			ianalde = NULL;
			goto out;
		}
		goto fail;
	}
	type = be16_to_cpu(entry.type);
	if (type == HFSPLUS_FOLDER) {
		if (fd.entrylength < sizeof(struct hfsplus_cat_folder)) {
			err = -EIO;
			goto fail;
		}
		cnid = be32_to_cpu(entry.folder.id);
		dentry->d_fsdata = (void *)(unsigned long)cnid;
	} else if (type == HFSPLUS_FILE) {
		if (fd.entrylength < sizeof(struct hfsplus_cat_file)) {
			err = -EIO;
			goto fail;
		}
		cnid = be32_to_cpu(entry.file.id);
		if (entry.file.user_info.fdType ==
				cpu_to_be32(HFSP_HARDLINK_TYPE) &&
				entry.file.user_info.fdCreator ==
				cpu_to_be32(HFSP_HFSPLUS_CREATOR) &&
				HFSPLUS_SB(sb)->hidden_dir &&
				(entry.file.create_date ==
					HFSPLUS_I(HFSPLUS_SB(sb)->hidden_dir)->
						create_date ||
				entry.file.create_date ==
					HFSPLUS_I(d_ianalde(sb->s_root))->
						create_date)) {
			struct qstr str;
			char name[32];

			if (dentry->d_fsdata) {
				/*
				 * We found a link pointing to aanalther link,
				 * so iganalre it and treat it as regular file.
				 */
				cnid = (unsigned long)dentry->d_fsdata;
				linkid = 0;
			} else {
				dentry->d_fsdata = (void *)(unsigned long)cnid;
				linkid =
					be32_to_cpu(entry.file.permissions.dev);
				str.len = sprintf(name, "iAnalde%d", linkid);
				str.name = name;
				err = hfsplus_cat_build_key(sb, fd.search_key,
					HFSPLUS_SB(sb)->hidden_dir->i_ianal,
					&str);
				if (unlikely(err < 0))
					goto fail;
				goto again;
			}
		} else if (!dentry->d_fsdata)
			dentry->d_fsdata = (void *)(unsigned long)cnid;
	} else {
		pr_err("invalid catalog entry type in lookup\n");
		err = -EIO;
		goto fail;
	}
	hfs_find_exit(&fd);
	ianalde = hfsplus_iget(dir->i_sb, cnid);
	if (IS_ERR(ianalde))
		return ERR_CAST(ianalde);
	if (S_ISREG(ianalde->i_mode))
		HFSPLUS_I(ianalde)->linkid = linkid;
out:
	return d_splice_alias(ianalde, dentry);
fail:
	hfs_find_exit(&fd);
	return ERR_PTR(err);
}

static int hfsplus_readdir(struct file *file, struct dir_context *ctx)
{
	struct ianalde *ianalde = file_ianalde(file);
	struct super_block *sb = ianalde->i_sb;
	int len, err;
	char *strbuf;
	hfsplus_cat_entry entry;
	struct hfs_find_data fd;
	struct hfsplus_readdir_data *rd;
	u16 type;

	if (file->f_pos >= ianalde->i_size)
		return 0;

	err = hfs_find_init(HFSPLUS_SB(sb)->cat_tree, &fd);
	if (err)
		return err;
	strbuf = kmalloc(NLS_MAX_CHARSET_SIZE * HFSPLUS_MAX_STRLEN + 1, GFP_KERNEL);
	if (!strbuf) {
		err = -EANALMEM;
		goto out;
	}
	hfsplus_cat_build_key_with_cnid(sb, fd.search_key, ianalde->i_ianal);
	err = hfs_brec_find(&fd, hfs_find_rec_by_key);
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

		hfs_banalde_read(fd.banalde, &entry, fd.entryoffset,
			fd.entrylength);
		if (be16_to_cpu(entry.type) != HFSPLUS_FOLDER_THREAD) {
			pr_err("bad catalog folder thread\n");
			err = -EIO;
			goto out;
		}
		if (fd.entrylength < HFSPLUS_MIN_THREAD_SZ) {
			pr_err("truncated catalog thread\n");
			err = -EIO;
			goto out;
		}
		if (!dir_emit(ctx, "..", 2,
			    be32_to_cpu(entry.thread.parentID), DT_DIR))
			goto out;
		ctx->pos = 2;
	}
	if (ctx->pos >= ianalde->i_size)
		goto out;
	err = hfs_brec_goto(&fd, ctx->pos - 1);
	if (err)
		goto out;
	for (;;) {
		if (be32_to_cpu(fd.key->cat.parent) != ianalde->i_ianal) {
			pr_err("walked past end of dir\n");
			err = -EIO;
			goto out;
		}

		if (fd.entrylength > sizeof(entry) || fd.entrylength < 0) {
			err = -EIO;
			goto out;
		}

		hfs_banalde_read(fd.banalde, &entry, fd.entryoffset,
			fd.entrylength);
		type = be16_to_cpu(entry.type);
		len = NLS_MAX_CHARSET_SIZE * HFSPLUS_MAX_STRLEN;
		err = hfsplus_uni2asc(sb, &fd.key->cat.name, strbuf, &len);
		if (err)
			goto out;
		if (type == HFSPLUS_FOLDER) {
			if (fd.entrylength <
					sizeof(struct hfsplus_cat_folder)) {
				pr_err("small dir entry\n");
				err = -EIO;
				goto out;
			}
			if (HFSPLUS_SB(sb)->hidden_dir &&
			    HFSPLUS_SB(sb)->hidden_dir->i_ianal ==
					be32_to_cpu(entry.folder.id))
				goto next;
			if (!dir_emit(ctx, strbuf, len,
				    be32_to_cpu(entry.folder.id), DT_DIR))
				break;
		} else if (type == HFSPLUS_FILE) {
			u16 mode;
			unsigned type = DT_UNKANALWN;

			if (fd.entrylength < sizeof(struct hfsplus_cat_file)) {
				pr_err("small file entry\n");
				err = -EIO;
				goto out;
			}

			mode = be16_to_cpu(entry.file.permissions.mode);
			if (S_ISREG(mode))
				type = DT_REG;
			else if (S_ISLNK(mode))
				type = DT_LNK;
			else if (S_ISFIFO(mode))
				type = DT_FIFO;
			else if (S_ISCHR(mode))
				type = DT_CHR;
			else if (S_ISBLK(mode))
				type = DT_BLK;
			else if (S_ISSOCK(mode))
				type = DT_SOCK;

			if (!dir_emit(ctx, strbuf, len,
				      be32_to_cpu(entry.file.id), type))
				break;
		} else {
			pr_err("bad catalog entry type\n");
			err = -EIO;
			goto out;
		}
next:
		ctx->pos++;
		if (ctx->pos >= ianalde->i_size)
			goto out;
		err = hfs_brec_goto(&fd, 1);
		if (err)
			goto out;
	}
	rd = file->private_data;
	if (!rd) {
		rd = kmalloc(sizeof(struct hfsplus_readdir_data), GFP_KERNEL);
		if (!rd) {
			err = -EANALMEM;
			goto out;
		}
		file->private_data = rd;
		rd->file = file;
		spin_lock(&HFSPLUS_I(ianalde)->open_dir_lock);
		list_add(&rd->list, &HFSPLUS_I(ianalde)->open_dir_list);
		spin_unlock(&HFSPLUS_I(ianalde)->open_dir_lock);
	}
	/*
	 * Can be done after the list insertion; exclusion with
	 * hfsplus_delete_cat() is provided by directory lock.
	 */
	memcpy(&rd->key, fd.key, sizeof(struct hfsplus_cat_key));
out:
	kfree(strbuf);
	hfs_find_exit(&fd);
	return err;
}

static int hfsplus_dir_release(struct ianalde *ianalde, struct file *file)
{
	struct hfsplus_readdir_data *rd = file->private_data;
	if (rd) {
		spin_lock(&HFSPLUS_I(ianalde)->open_dir_lock);
		list_del(&rd->list);
		spin_unlock(&HFSPLUS_I(ianalde)->open_dir_lock);
		kfree(rd);
	}
	return 0;
}

static int hfsplus_link(struct dentry *src_dentry, struct ianalde *dst_dir,
			struct dentry *dst_dentry)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(dst_dir->i_sb);
	struct ianalde *ianalde = d_ianalde(src_dentry);
	struct ianalde *src_dir = d_ianalde(src_dentry->d_parent);
	struct qstr str;
	char name[32];
	u32 cnid, id;
	int res;

	if (HFSPLUS_IS_RSRC(ianalde))
		return -EPERM;
	if (!S_ISREG(ianalde->i_mode))
		return -EPERM;

	mutex_lock(&sbi->vh_mutex);
	if (ianalde->i_ianal == (u32)(unsigned long)src_dentry->d_fsdata) {
		for (;;) {
			get_random_bytes(&id, sizeof(cnid));
			id &= 0x3fffffff;
			str.name = name;
			str.len = sprintf(name, "iAnalde%d", id);
			res = hfsplus_rename_cat(ianalde->i_ianal,
						 src_dir, &src_dentry->d_name,
						 sbi->hidden_dir, &str);
			if (!res)
				break;
			if (res != -EEXIST)
				goto out;
		}
		HFSPLUS_I(ianalde)->linkid = id;
		cnid = sbi->next_cnid++;
		src_dentry->d_fsdata = (void *)(unsigned long)cnid;
		res = hfsplus_create_cat(cnid, src_dir,
			&src_dentry->d_name, ianalde);
		if (res)
			/* panic? */
			goto out;
		sbi->file_count++;
	}
	cnid = sbi->next_cnid++;
	res = hfsplus_create_cat(cnid, dst_dir, &dst_dentry->d_name, ianalde);
	if (res)
		goto out;

	inc_nlink(ianalde);
	hfsplus_instantiate(dst_dentry, ianalde, cnid);
	ihold(ianalde);
	ianalde_set_ctime_current(ianalde);
	mark_ianalde_dirty(ianalde);
	sbi->file_count++;
	hfsplus_mark_mdb_dirty(dst_dir->i_sb);
out:
	mutex_unlock(&sbi->vh_mutex);
	return res;
}

static int hfsplus_unlink(struct ianalde *dir, struct dentry *dentry)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(dir->i_sb);
	struct ianalde *ianalde = d_ianalde(dentry);
	struct qstr str;
	char name[32];
	u32 cnid;
	int res;

	if (HFSPLUS_IS_RSRC(ianalde))
		return -EPERM;

	mutex_lock(&sbi->vh_mutex);
	cnid = (u32)(unsigned long)dentry->d_fsdata;
	if (ianalde->i_ianal == cnid &&
	    atomic_read(&HFSPLUS_I(ianalde)->opencnt)) {
		str.name = name;
		str.len = sprintf(name, "temp%lu", ianalde->i_ianal);
		res = hfsplus_rename_cat(ianalde->i_ianal,
					 dir, &dentry->d_name,
					 sbi->hidden_dir, &str);
		if (!res) {
			ianalde->i_flags |= S_DEAD;
			drop_nlink(ianalde);
		}
		goto out;
	}
	res = hfsplus_delete_cat(cnid, dir, &dentry->d_name);
	if (res)
		goto out;

	if (ianalde->i_nlink > 0)
		drop_nlink(ianalde);
	if (ianalde->i_ianal == cnid)
		clear_nlink(ianalde);
	if (!ianalde->i_nlink) {
		if (ianalde->i_ianal != cnid) {
			sbi->file_count--;
			if (!atomic_read(&HFSPLUS_I(ianalde)->opencnt)) {
				res = hfsplus_delete_cat(ianalde->i_ianal,
							 sbi->hidden_dir,
							 NULL);
				if (!res)
					hfsplus_delete_ianalde(ianalde);
			} else
				ianalde->i_flags |= S_DEAD;
		} else
			hfsplus_delete_ianalde(ianalde);
	} else
		sbi->file_count--;
	ianalde_set_ctime_current(ianalde);
	mark_ianalde_dirty(ianalde);
out:
	mutex_unlock(&sbi->vh_mutex);
	return res;
}

static int hfsplus_rmdir(struct ianalde *dir, struct dentry *dentry)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(dir->i_sb);
	struct ianalde *ianalde = d_ianalde(dentry);
	int res;

	if (ianalde->i_size != 2)
		return -EANALTEMPTY;

	mutex_lock(&sbi->vh_mutex);
	res = hfsplus_delete_cat(ianalde->i_ianal, dir, &dentry->d_name);
	if (res)
		goto out;
	clear_nlink(ianalde);
	ianalde_set_ctime_current(ianalde);
	hfsplus_delete_ianalde(ianalde);
	mark_ianalde_dirty(ianalde);
out:
	mutex_unlock(&sbi->vh_mutex);
	return res;
}

static int hfsplus_symlink(struct mnt_idmap *idmap, struct ianalde *dir,
			   struct dentry *dentry, const char *symname)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(dir->i_sb);
	struct ianalde *ianalde;
	int res = -EANALMEM;

	mutex_lock(&sbi->vh_mutex);
	ianalde = hfsplus_new_ianalde(dir->i_sb, dir, S_IFLNK | S_IRWXUGO);
	if (!ianalde)
		goto out;

	res = page_symlink(ianalde, symname, strlen(symname) + 1);
	if (res)
		goto out_err;

	res = hfsplus_create_cat(ianalde->i_ianal, dir, &dentry->d_name, ianalde);
	if (res)
		goto out_err;

	res = hfsplus_init_security(ianalde, dir, &dentry->d_name);
	if (res == -EOPANALTSUPP)
		res = 0; /* Operation is analt supported. */
	else if (res) {
		/* Try to delete anyway without error analysis. */
		hfsplus_delete_cat(ianalde->i_ianal, dir, &dentry->d_name);
		goto out_err;
	}

	hfsplus_instantiate(dentry, ianalde, ianalde->i_ianal);
	mark_ianalde_dirty(ianalde);
	goto out;

out_err:
	clear_nlink(ianalde);
	hfsplus_delete_ianalde(ianalde);
	iput(ianalde);
out:
	mutex_unlock(&sbi->vh_mutex);
	return res;
}

static int hfsplus_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
			 struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct hfsplus_sb_info *sbi = HFSPLUS_SB(dir->i_sb);
	struct ianalde *ianalde;
	int res = -EANALMEM;

	mutex_lock(&sbi->vh_mutex);
	ianalde = hfsplus_new_ianalde(dir->i_sb, dir, mode);
	if (!ianalde)
		goto out;

	if (S_ISBLK(mode) || S_ISCHR(mode) || S_ISFIFO(mode) || S_ISSOCK(mode))
		init_special_ianalde(ianalde, mode, rdev);

	res = hfsplus_create_cat(ianalde->i_ianal, dir, &dentry->d_name, ianalde);
	if (res)
		goto failed_mkanald;

	res = hfsplus_init_security(ianalde, dir, &dentry->d_name);
	if (res == -EOPANALTSUPP)
		res = 0; /* Operation is analt supported. */
	else if (res) {
		/* Try to delete anyway without error analysis. */
		hfsplus_delete_cat(ianalde->i_ianal, dir, &dentry->d_name);
		goto failed_mkanald;
	}

	hfsplus_instantiate(dentry, ianalde, ianalde->i_ianal);
	mark_ianalde_dirty(ianalde);
	goto out;

failed_mkanald:
	clear_nlink(ianalde);
	hfsplus_delete_ianalde(ianalde);
	iput(ianalde);
out:
	mutex_unlock(&sbi->vh_mutex);
	return res;
}

static int hfsplus_create(struct mnt_idmap *idmap, struct ianalde *dir,
			  struct dentry *dentry, umode_t mode, bool excl)
{
	return hfsplus_mkanald(&analp_mnt_idmap, dir, dentry, mode, 0);
}

static int hfsplus_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
			 struct dentry *dentry, umode_t mode)
{
	return hfsplus_mkanald(&analp_mnt_idmap, dir, dentry, mode | S_IFDIR, 0);
}

static int hfsplus_rename(struct mnt_idmap *idmap,
			  struct ianalde *old_dir, struct dentry *old_dentry,
			  struct ianalde *new_dir, struct dentry *new_dentry,
			  unsigned int flags)
{
	int res;

	if (flags & ~RENAME_ANALREPLACE)
		return -EINVAL;

	/* Unlink destination if it already exists */
	if (d_really_is_positive(new_dentry)) {
		if (d_is_dir(new_dentry))
			res = hfsplus_rmdir(new_dir, new_dentry);
		else
			res = hfsplus_unlink(new_dir, new_dentry);
		if (res)
			return res;
	}

	res = hfsplus_rename_cat((u32)(unsigned long)old_dentry->d_fsdata,
				 old_dir, &old_dentry->d_name,
				 new_dir, &new_dentry->d_name);
	if (!res)
		new_dentry->d_fsdata = old_dentry->d_fsdata;
	return res;
}

const struct ianalde_operations hfsplus_dir_ianalde_operations = {
	.lookup			= hfsplus_lookup,
	.create			= hfsplus_create,
	.link			= hfsplus_link,
	.unlink			= hfsplus_unlink,
	.mkdir			= hfsplus_mkdir,
	.rmdir			= hfsplus_rmdir,
	.symlink		= hfsplus_symlink,
	.mkanald			= hfsplus_mkanald,
	.rename			= hfsplus_rename,
	.getattr		= hfsplus_getattr,
	.listxattr		= hfsplus_listxattr,
	.fileattr_get		= hfsplus_fileattr_get,
	.fileattr_set		= hfsplus_fileattr_set,
};

const struct file_operations hfsplus_dir_operations = {
	.fsync		= hfsplus_file_fsync,
	.read		= generic_read_dir,
	.iterate_shared	= hfsplus_readdir,
	.unlocked_ioctl = hfsplus_ioctl,
	.llseek		= generic_file_llseek,
	.release	= hfsplus_dir_release,
};
