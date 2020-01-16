// SPDX-License-Identifier: GPL-2.0-only
/* * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 * Copyright (C) 2006, 2007 University of Szeged, Hungary
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 *          Zoltan Sogor
 */

/*
 * This file implements directory operations.
 *
 * All FS operations in this file allocate budget before writing anything to the
 * media. If they fail to allocate it, the error is returned. The only
 * exceptions are 'ubifs_unlink()' and 'ubifs_rmdir()' which keep working even
 * if they unable to allocate the budget, because deletion %-ENOSPC failure is
 * yest what users are usually ready to get. UBIFS budgeting subsystem has some
 * space reserved for these purposes.
 *
 * All operations in this file write all iyesdes which they change straight
 * away, instead of marking them dirty. For example, 'ubifs_link()' changes
 * @i_size of the parent iyesde and writes the parent iyesde together with the
 * target iyesde. This was done to simplify file-system recovery which would
 * otherwise be very difficult to do. The only exception is rename which marks
 * the re-named iyesde dirty (because its @i_ctime is updated) but does yest
 * write it, but just marks it as dirty.
 */

#include "ubifs.h"

/**
 * inherit_flags - inherit flags of the parent iyesde.
 * @dir: parent iyesde
 * @mode: new iyesde mode flags
 *
 * This is a helper function for 'ubifs_new_iyesde()' which inherits flag of the
 * parent directory iyesde @dir. UBIFS iyesdes inherit the following flags:
 * o %UBIFS_COMPR_FL, which is useful to switch compression on/of on
 *   sub-directory basis;
 * o %UBIFS_SYNC_FL - useful for the same reasons;
 * o %UBIFS_DIRSYNC_FL - similar, but relevant only to directories.
 *
 * This function returns the inherited flags.
 */
static int inherit_flags(const struct iyesde *dir, umode_t mode)
{
	int flags;
	const struct ubifs_iyesde *ui = ubifs_iyesde(dir);

	if (!S_ISDIR(dir->i_mode))
		/*
		 * The parent is yest a directory, which means that an extended
		 * attribute iyesde is being created. No flags.
		 */
		return 0;

	flags = ui->flags & (UBIFS_COMPR_FL | UBIFS_SYNC_FL | UBIFS_DIRSYNC_FL);
	if (!S_ISDIR(mode))
		/* The "DIRSYNC" flag only applies to directories */
		flags &= ~UBIFS_DIRSYNC_FL;
	return flags;
}

/**
 * ubifs_new_iyesde - allocate new UBIFS iyesde object.
 * @c: UBIFS file-system description object
 * @dir: parent directory iyesde
 * @mode: iyesde mode flags
 *
 * This function finds an unused iyesde number, allocates new iyesde and
 * initializes it. Returns new iyesde in case of success and an error code in
 * case of failure.
 */
struct iyesde *ubifs_new_iyesde(struct ubifs_info *c, struct iyesde *dir,
			      umode_t mode)
{
	int err;
	struct iyesde *iyesde;
	struct ubifs_iyesde *ui;
	bool encrypted = false;

	if (ubifs_crypt_is_encrypted(dir)) {
		err = fscrypt_get_encryption_info(dir);
		if (err) {
			ubifs_err(c, "fscrypt_get_encryption_info failed: %i", err);
			return ERR_PTR(err);
		}

		if (!fscrypt_has_encryption_key(dir))
			return ERR_PTR(-EPERM);

		encrypted = true;
	}

	iyesde = new_iyesde(c->vfs_sb);
	ui = ubifs_iyesde(iyesde);
	if (!iyesde)
		return ERR_PTR(-ENOMEM);

	/*
	 * Set 'S_NOCMTIME' to prevent VFS form updating [mc]time of iyesdes and
	 * marking them dirty in file write path (see 'file_update_time()').
	 * UBIFS has to fully control "clean <-> dirty" transitions of iyesdes
	 * to make budgeting work.
	 */
	iyesde->i_flags |= S_NOCMTIME;

	iyesde_init_owner(iyesde, dir, mode);
	iyesde->i_mtime = iyesde->i_atime = iyesde->i_ctime =
			 current_time(iyesde);
	iyesde->i_mapping->nrpages = 0;

	switch (mode & S_IFMT) {
	case S_IFREG:
		iyesde->i_mapping->a_ops = &ubifs_file_address_operations;
		iyesde->i_op = &ubifs_file_iyesde_operations;
		iyesde->i_fop = &ubifs_file_operations;
		break;
	case S_IFDIR:
		iyesde->i_op  = &ubifs_dir_iyesde_operations;
		iyesde->i_fop = &ubifs_dir_operations;
		iyesde->i_size = ui->ui_size = UBIFS_INO_NODE_SZ;
		break;
	case S_IFLNK:
		iyesde->i_op = &ubifs_symlink_iyesde_operations;
		break;
	case S_IFSOCK:
	case S_IFIFO:
	case S_IFBLK:
	case S_IFCHR:
		iyesde->i_op  = &ubifs_file_iyesde_operations;
		encrypted = false;
		break;
	default:
		BUG();
	}

	ui->flags = inherit_flags(dir, mode);
	ubifs_set_iyesde_flags(iyesde);
	if (S_ISREG(mode))
		ui->compr_type = c->default_compr;
	else
		ui->compr_type = UBIFS_COMPR_NONE;
	ui->synced_i_size = 0;

	spin_lock(&c->cnt_lock);
	/* Iyesde number overflow is currently yest supported */
	if (c->highest_inum >= INUM_WARN_WATERMARK) {
		if (c->highest_inum >= INUM_WATERMARK) {
			spin_unlock(&c->cnt_lock);
			ubifs_err(c, "out of iyesde numbers");
			make_bad_iyesde(iyesde);
			iput(iyesde);
			return ERR_PTR(-EINVAL);
		}
		ubifs_warn(c, "running out of iyesde numbers (current %lu, max %u)",
			   (unsigned long)c->highest_inum, INUM_WATERMARK);
	}

	iyesde->i_iyes = ++c->highest_inum;
	/*
	 * The creation sequence number remains with this iyesde for its
	 * lifetime. All yesdes for this iyesde have a greater sequence number,
	 * and so it is possible to distinguish obsolete yesdes belonging to a
	 * previous incarnation of the same iyesde number - for example, for the
	 * purpose of rebuilding the index.
	 */
	ui->creat_sqnum = ++c->max_sqnum;
	spin_unlock(&c->cnt_lock);

	if (encrypted) {
		err = fscrypt_inherit_context(dir, iyesde, &encrypted, true);
		if (err) {
			ubifs_err(c, "fscrypt_inherit_context failed: %i", err);
			make_bad_iyesde(iyesde);
			iput(iyesde);
			return ERR_PTR(err);
		}
	}

	return iyesde;
}

static int dbg_check_name(const struct ubifs_info *c,
			  const struct ubifs_dent_yesde *dent,
			  const struct fscrypt_name *nm)
{
	if (!dbg_is_chk_gen(c))
		return 0;
	if (le16_to_cpu(dent->nlen) != fname_len(nm))
		return -EINVAL;
	if (memcmp(dent->name, fname_name(nm), fname_len(nm)))
		return -EINVAL;
	return 0;
}

static struct dentry *ubifs_lookup(struct iyesde *dir, struct dentry *dentry,
				   unsigned int flags)
{
	int err;
	union ubifs_key key;
	struct iyesde *iyesde = NULL;
	struct ubifs_dent_yesde *dent = NULL;
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	struct fscrypt_name nm;

	dbg_gen("'%pd' in dir iyes %lu", dentry, dir->i_iyes);

	err = fscrypt_prepare_lookup(dir, dentry, &nm);
	if (err == -ENOENT)
		return d_splice_alias(NULL, dentry);
	if (err)
		return ERR_PTR(err);

	if (fname_len(&nm) > UBIFS_MAX_NLEN) {
		iyesde = ERR_PTR(-ENAMETOOLONG);
		goto done;
	}

	dent = kmalloc(UBIFS_MAX_DENT_NODE_SZ, GFP_NOFS);
	if (!dent) {
		iyesde = ERR_PTR(-ENOMEM);
		goto done;
	}

	if (nm.hash) {
		ubifs_assert(c, fname_len(&nm) == 0);
		ubifs_assert(c, fname_name(&nm) == NULL);
		dent_key_init_hash(c, &key, dir->i_iyes, nm.hash);
		err = ubifs_tnc_lookup_dh(c, &key, dent, nm.miyesr_hash);
	} else {
		dent_key_init(c, &key, dir->i_iyes, &nm);
		err = ubifs_tnc_lookup_nm(c, &key, dent, &nm);
	}

	if (err) {
		if (err == -ENOENT)
			dbg_gen("yest found");
		else
			iyesde = ERR_PTR(err);
		goto done;
	}

	if (dbg_check_name(c, dent, &nm)) {
		iyesde = ERR_PTR(-EINVAL);
		goto done;
	}

	iyesde = ubifs_iget(dir->i_sb, le64_to_cpu(dent->inum));
	if (IS_ERR(iyesde)) {
		/*
		 * This should yest happen. Probably the file-system needs
		 * checking.
		 */
		err = PTR_ERR(iyesde);
		ubifs_err(c, "dead directory entry '%pd', error %d",
			  dentry, err);
		ubifs_ro_mode(c, err);
		goto done;
	}

	if (ubifs_crypt_is_encrypted(dir) &&
	    (S_ISDIR(iyesde->i_mode) || S_ISLNK(iyesde->i_mode)) &&
	    !fscrypt_has_permitted_context(dir, iyesde)) {
		ubifs_warn(c, "Inconsistent encryption contexts: %lu/%lu",
			   dir->i_iyes, iyesde->i_iyes);
		iput(iyesde);
		iyesde = ERR_PTR(-EPERM);
	}

done:
	kfree(dent);
	fscrypt_free_filename(&nm);
	return d_splice_alias(iyesde, dentry);
}

static int ubifs_create(struct iyesde *dir, struct dentry *dentry, umode_t mode,
			bool excl)
{
	struct iyesde *iyesde;
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	struct ubifs_budget_req req = { .new_iyes = 1, .new_dent = 1,
					.dirtied_iyes = 1 };
	struct ubifs_iyesde *dir_ui = ubifs_iyesde(dir);
	struct fscrypt_name nm;
	int err, sz_change;

	/*
	 * Budget request settings: new iyesde, new direntry, changing the
	 * parent directory iyesde.
	 */

	dbg_gen("dent '%pd', mode %#hx in dir iyes %lu",
		dentry, mode, dir->i_iyes);

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	err = fscrypt_setup_filename(dir, &dentry->d_name, 0, &nm);
	if (err)
		goto out_budg;

	sz_change = CALC_DENT_SIZE(fname_len(&nm));

	iyesde = ubifs_new_iyesde(c, dir, mode);
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		goto out_fname;
	}

	err = ubifs_init_security(dir, iyesde, &dentry->d_name);
	if (err)
		goto out_iyesde;

	mutex_lock(&dir_ui->ui_mutex);
	dir->i_size += sz_change;
	dir_ui->ui_size = dir->i_size;
	dir->i_mtime = dir->i_ctime = iyesde->i_ctime;
	err = ubifs_jnl_update(c, dir, &nm, iyesde, 0, 0);
	if (err)
		goto out_cancel;
	mutex_unlock(&dir_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	fscrypt_free_filename(&nm);
	insert_iyesde_hash(iyesde);
	d_instantiate(dentry, iyesde);
	return 0;

out_cancel:
	dir->i_size -= sz_change;
	dir_ui->ui_size = dir->i_size;
	mutex_unlock(&dir_ui->ui_mutex);
out_iyesde:
	make_bad_iyesde(iyesde);
	iput(iyesde);
out_fname:
	fscrypt_free_filename(&nm);
out_budg:
	ubifs_release_budget(c, &req);
	ubifs_err(c, "canyest create regular file, error %d", err);
	return err;
}

static int do_tmpfile(struct iyesde *dir, struct dentry *dentry,
		      umode_t mode, struct iyesde **whiteout)
{
	struct iyesde *iyesde;
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	struct ubifs_budget_req req = { .new_iyes = 1, .new_dent = 1};
	struct ubifs_budget_req iyes_req = { .dirtied_iyes = 1 };
	struct ubifs_iyesde *ui, *dir_ui = ubifs_iyesde(dir);
	int err, instantiated = 0;
	struct fscrypt_name nm;

	/*
	 * Budget request settings: new dirty iyesde, new direntry,
	 * budget for dirtied iyesde will be released via writeback.
	 */

	dbg_gen("dent '%pd', mode %#hx in dir iyes %lu",
		dentry, mode, dir->i_iyes);

	err = fscrypt_setup_filename(dir, &dentry->d_name, 0, &nm);
	if (err)
		return err;

	err = ubifs_budget_space(c, &req);
	if (err) {
		fscrypt_free_filename(&nm);
		return err;
	}

	err = ubifs_budget_space(c, &iyes_req);
	if (err) {
		ubifs_release_budget(c, &req);
		fscrypt_free_filename(&nm);
		return err;
	}

	iyesde = ubifs_new_iyesde(c, dir, mode);
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		goto out_budg;
	}
	ui = ubifs_iyesde(iyesde);

	if (whiteout) {
		init_special_iyesde(iyesde, iyesde->i_mode, WHITEOUT_DEV);
		ubifs_assert(c, iyesde->i_op == &ubifs_file_iyesde_operations);
	}

	err = ubifs_init_security(dir, iyesde, &dentry->d_name);
	if (err)
		goto out_iyesde;

	mutex_lock(&ui->ui_mutex);
	insert_iyesde_hash(iyesde);

	if (whiteout) {
		mark_iyesde_dirty(iyesde);
		drop_nlink(iyesde);
		*whiteout = iyesde;
	} else {
		d_tmpfile(dentry, iyesde);
	}
	ubifs_assert(c, ui->dirty);

	instantiated = 1;
	mutex_unlock(&ui->ui_mutex);

	mutex_lock(&dir_ui->ui_mutex);
	err = ubifs_jnl_update(c, dir, &nm, iyesde, 1, 0);
	if (err)
		goto out_cancel;
	mutex_unlock(&dir_ui->ui_mutex);

	ubifs_release_budget(c, &req);

	return 0;

out_cancel:
	mutex_unlock(&dir_ui->ui_mutex);
out_iyesde:
	make_bad_iyesde(iyesde);
	if (!instantiated)
		iput(iyesde);
out_budg:
	ubifs_release_budget(c, &req);
	if (!instantiated)
		ubifs_release_budget(c, &iyes_req);
	fscrypt_free_filename(&nm);
	ubifs_err(c, "canyest create temporary file, error %d", err);
	return err;
}

static int ubifs_tmpfile(struct iyesde *dir, struct dentry *dentry,
			 umode_t mode)
{
	return do_tmpfile(dir, dentry, mode, NULL);
}

/**
 * vfs_dent_type - get VFS directory entry type.
 * @type: UBIFS directory entry type
 *
 * This function converts UBIFS directory entry type into VFS directory entry
 * type.
 */
static unsigned int vfs_dent_type(uint8_t type)
{
	switch (type) {
	case UBIFS_ITYPE_REG:
		return DT_REG;
	case UBIFS_ITYPE_DIR:
		return DT_DIR;
	case UBIFS_ITYPE_LNK:
		return DT_LNK;
	case UBIFS_ITYPE_BLK:
		return DT_BLK;
	case UBIFS_ITYPE_CHR:
		return DT_CHR;
	case UBIFS_ITYPE_FIFO:
		return DT_FIFO;
	case UBIFS_ITYPE_SOCK:
		return DT_SOCK;
	default:
		BUG();
	}
	return 0;
}

/*
 * The classical Unix view for directory is that it is a linear array of
 * (name, iyesde number) entries. Linux/VFS assumes this model as well.
 * Particularly, 'readdir()' call wants us to return a directory entry offset
 * which later may be used to continue 'readdir()'ing the directory or to
 * 'seek()' to that specific direntry. Obviously UBIFS does yest really fit this
 * model because directory entries are identified by keys, which may collide.
 *
 * UBIFS uses directory entry hash value for directory offsets, so
 * 'seekdir()'/'telldir()' may yest always work because of possible key
 * collisions. But UBIFS guarantees that consecutive 'readdir()' calls work
 * properly by means of saving full directory entry name in the private field
 * of the file description object.
 *
 * This means that UBIFS canyest support NFS which requires full
 * 'seekdir()'/'telldir()' support.
 */
static int ubifs_readdir(struct file *file, struct dir_context *ctx)
{
	int fstr_real_len = 0, err = 0;
	struct fscrypt_name nm;
	struct fscrypt_str fstr = {0};
	union ubifs_key key;
	struct ubifs_dent_yesde *dent;
	struct iyesde *dir = file_iyesde(file);
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	bool encrypted = ubifs_crypt_is_encrypted(dir);

	dbg_gen("dir iyes %lu, f_pos %#llx", dir->i_iyes, ctx->pos);

	if (ctx->pos > UBIFS_S_KEY_HASH_MASK || ctx->pos == 2)
		/*
		 * The directory was seek'ed to a senseless position or there
		 * are yes more entries.
		 */
		return 0;

	if (encrypted) {
		err = fscrypt_get_encryption_info(dir);
		if (err && err != -ENOKEY)
			return err;

		err = fscrypt_fname_alloc_buffer(dir, UBIFS_MAX_NLEN, &fstr);
		if (err)
			return err;

		fstr_real_len = fstr.len;
	}

	if (file->f_version == 0) {
		/*
		 * The file was seek'ed, which means that @file->private_data
		 * is yesw invalid. This may also be just the first
		 * 'ubifs_readdir()' invocation, in which case
		 * @file->private_data is NULL, and the below code is
		 * basically a yes-op.
		 */
		kfree(file->private_data);
		file->private_data = NULL;
	}

	/*
	 * 'generic_file_llseek()' unconditionally sets @file->f_version to
	 * zero, and we use this for detecting whether the file was seek'ed.
	 */
	file->f_version = 1;

	/* File positions 0 and 1 correspond to "." and ".." */
	if (ctx->pos < 2) {
		ubifs_assert(c, !file->private_data);
		if (!dir_emit_dots(file, ctx)) {
			if (encrypted)
				fscrypt_fname_free_buffer(&fstr);
			return 0;
		}

		/* Find the first entry in TNC and save it */
		lowest_dent_key(c, &key, dir->i_iyes);
		fname_len(&nm) = 0;
		dent = ubifs_tnc_next_ent(c, &key, &nm);
		if (IS_ERR(dent)) {
			err = PTR_ERR(dent);
			goto out;
		}

		ctx->pos = key_hash_flash(c, &dent->key);
		file->private_data = dent;
	}

	dent = file->private_data;
	if (!dent) {
		/*
		 * The directory was seek'ed to and is yesw readdir'ed.
		 * Find the entry corresponding to @ctx->pos or the closest one.
		 */
		dent_key_init_hash(c, &key, dir->i_iyes, ctx->pos);
		fname_len(&nm) = 0;
		dent = ubifs_tnc_next_ent(c, &key, &nm);
		if (IS_ERR(dent)) {
			err = PTR_ERR(dent);
			goto out;
		}
		ctx->pos = key_hash_flash(c, &dent->key);
		file->private_data = dent;
	}

	while (1) {
		dbg_gen("iyes %llu, new f_pos %#x",
			(unsigned long long)le64_to_cpu(dent->inum),
			key_hash_flash(c, &dent->key));
		ubifs_assert(c, le64_to_cpu(dent->ch.sqnum) >
			     ubifs_iyesde(dir)->creat_sqnum);

		fname_len(&nm) = le16_to_cpu(dent->nlen);
		fname_name(&nm) = dent->name;

		if (encrypted) {
			fstr.len = fstr_real_len;

			err = fscrypt_fname_disk_to_usr(dir, key_hash_flash(c,
							&dent->key),
							le32_to_cpu(dent->cookie),
							&nm.disk_name, &fstr);
			if (err)
				goto out;
		} else {
			fstr.len = fname_len(&nm);
			fstr.name = fname_name(&nm);
		}

		if (!dir_emit(ctx, fstr.name, fstr.len,
			       le64_to_cpu(dent->inum),
			       vfs_dent_type(dent->type))) {
			if (encrypted)
				fscrypt_fname_free_buffer(&fstr);
			return 0;
		}

		/* Switch to the next entry */
		key_read(c, &dent->key, &key);
		dent = ubifs_tnc_next_ent(c, &key, &nm);
		if (IS_ERR(dent)) {
			err = PTR_ERR(dent);
			goto out;
		}

		kfree(file->private_data);
		ctx->pos = key_hash_flash(c, &dent->key);
		file->private_data = dent;
		cond_resched();
	}

out:
	kfree(file->private_data);
	file->private_data = NULL;

	if (encrypted)
		fscrypt_fname_free_buffer(&fstr);

	if (err != -ENOENT)
		ubifs_err(c, "canyest find next direntry, error %d", err);
	else
		/*
		 * -ENOENT is a yesn-fatal error in this context, the TNC uses
		 * it to indicate that the cursor moved past the current directory
		 * and readdir() has to stop.
		 */
		err = 0;


	/* 2 is a special value indicating that there are yes more direntries */
	ctx->pos = 2;
	return err;
}

/* Free saved readdir() state when the directory is closed */
static int ubifs_dir_release(struct iyesde *dir, struct file *file)
{
	kfree(file->private_data);
	file->private_data = NULL;
	return 0;
}

/**
 * lock_2_iyesdes - a wrapper for locking two UBIFS iyesdes.
 * @iyesde1: first iyesde
 * @iyesde2: second iyesde
 *
 * We do yest implement any tricks to guarantee strict lock ordering, because
 * VFS has already done it for us on the @i_mutex. So this is just a simple
 * wrapper function.
 */
static void lock_2_iyesdes(struct iyesde *iyesde1, struct iyesde *iyesde2)
{
	mutex_lock_nested(&ubifs_iyesde(iyesde1)->ui_mutex, WB_MUTEX_1);
	mutex_lock_nested(&ubifs_iyesde(iyesde2)->ui_mutex, WB_MUTEX_2);
}

/**
 * unlock_2_iyesdes - a wrapper for unlocking two UBIFS iyesdes.
 * @iyesde1: first iyesde
 * @iyesde2: second iyesde
 */
static void unlock_2_iyesdes(struct iyesde *iyesde1, struct iyesde *iyesde2)
{
	mutex_unlock(&ubifs_iyesde(iyesde2)->ui_mutex);
	mutex_unlock(&ubifs_iyesde(iyesde1)->ui_mutex);
}

static int ubifs_link(struct dentry *old_dentry, struct iyesde *dir,
		      struct dentry *dentry)
{
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	struct iyesde *iyesde = d_iyesde(old_dentry);
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);
	struct ubifs_iyesde *dir_ui = ubifs_iyesde(dir);
	int err, sz_change = CALC_DENT_SIZE(dentry->d_name.len);
	struct ubifs_budget_req req = { .new_dent = 1, .dirtied_iyes = 2,
				.dirtied_iyes_d = ALIGN(ui->data_len, 8) };
	struct fscrypt_name nm;

	/*
	 * Budget request settings: new direntry, changing the target iyesde,
	 * changing the parent iyesde.
	 */

	dbg_gen("dent '%pd' to iyes %lu (nlink %d) in dir iyes %lu",
		dentry, iyesde->i_iyes,
		iyesde->i_nlink, dir->i_iyes);
	ubifs_assert(c, iyesde_is_locked(dir));
	ubifs_assert(c, iyesde_is_locked(iyesde));

	err = fscrypt_prepare_link(old_dentry, dir, dentry);
	if (err)
		return err;

	err = fscrypt_setup_filename(dir, &dentry->d_name, 0, &nm);
	if (err)
		return err;

	err = dbg_check_synced_i_size(c, iyesde);
	if (err)
		goto out_fname;

	err = ubifs_budget_space(c, &req);
	if (err)
		goto out_fname;

	lock_2_iyesdes(dir, iyesde);

	/* Handle O_TMPFILE corner case, it is allowed to link a O_TMPFILE. */
	if (iyesde->i_nlink == 0)
		ubifs_delete_orphan(c, iyesde->i_iyes);

	inc_nlink(iyesde);
	ihold(iyesde);
	iyesde->i_ctime = current_time(iyesde);
	dir->i_size += sz_change;
	dir_ui->ui_size = dir->i_size;
	dir->i_mtime = dir->i_ctime = iyesde->i_ctime;
	err = ubifs_jnl_update(c, dir, &nm, iyesde, 0, 0);
	if (err)
		goto out_cancel;
	unlock_2_iyesdes(dir, iyesde);

	ubifs_release_budget(c, &req);
	d_instantiate(dentry, iyesde);
	fscrypt_free_filename(&nm);
	return 0;

out_cancel:
	dir->i_size -= sz_change;
	dir_ui->ui_size = dir->i_size;
	drop_nlink(iyesde);
	if (iyesde->i_nlink == 0)
		ubifs_add_orphan(c, iyesde->i_iyes);
	unlock_2_iyesdes(dir, iyesde);
	ubifs_release_budget(c, &req);
	iput(iyesde);
out_fname:
	fscrypt_free_filename(&nm);
	return err;
}

static int ubifs_unlink(struct iyesde *dir, struct dentry *dentry)
{
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	struct iyesde *iyesde = d_iyesde(dentry);
	struct ubifs_iyesde *dir_ui = ubifs_iyesde(dir);
	int err, sz_change, budgeted = 1;
	struct ubifs_budget_req req = { .mod_dent = 1, .dirtied_iyes = 2 };
	unsigned int saved_nlink = iyesde->i_nlink;
	struct fscrypt_name nm;

	/*
	 * Budget request settings: deletion direntry, deletion iyesde (+1 for
	 * @dirtied_iyes), changing the parent directory iyesde. If budgeting
	 * fails, go ahead anyway because we have extra space reserved for
	 * deletions.
	 */

	dbg_gen("dent '%pd' from iyes %lu (nlink %d) in dir iyes %lu",
		dentry, iyesde->i_iyes,
		iyesde->i_nlink, dir->i_iyes);

	err = fscrypt_setup_filename(dir, &dentry->d_name, 1, &nm);
	if (err)
		return err;

	err = ubifs_purge_xattrs(iyesde);
	if (err)
		return err;

	sz_change = CALC_DENT_SIZE(fname_len(&nm));

	ubifs_assert(c, iyesde_is_locked(dir));
	ubifs_assert(c, iyesde_is_locked(iyesde));
	err = dbg_check_synced_i_size(c, iyesde);
	if (err)
		goto out_fname;

	err = ubifs_budget_space(c, &req);
	if (err) {
		if (err != -ENOSPC)
			goto out_fname;
		budgeted = 0;
	}

	lock_2_iyesdes(dir, iyesde);
	iyesde->i_ctime = current_time(dir);
	drop_nlink(iyesde);
	dir->i_size -= sz_change;
	dir_ui->ui_size = dir->i_size;
	dir->i_mtime = dir->i_ctime = iyesde->i_ctime;
	err = ubifs_jnl_update(c, dir, &nm, iyesde, 1, 0);
	if (err)
		goto out_cancel;
	unlock_2_iyesdes(dir, iyesde);

	if (budgeted)
		ubifs_release_budget(c, &req);
	else {
		/* We've deleted something - clean the "yes space" flags */
		c->bi.yesspace = c->bi.yesspace_rp = 0;
		smp_wmb();
	}
	fscrypt_free_filename(&nm);
	return 0;

out_cancel:
	dir->i_size += sz_change;
	dir_ui->ui_size = dir->i_size;
	set_nlink(iyesde, saved_nlink);
	unlock_2_iyesdes(dir, iyesde);
	if (budgeted)
		ubifs_release_budget(c, &req);
out_fname:
	fscrypt_free_filename(&nm);
	return err;
}

/**
 * check_dir_empty - check if a directory is empty or yest.
 * @dir: VFS iyesde object of the directory to check
 *
 * This function checks if directory @dir is empty. Returns zero if the
 * directory is empty, %-ENOTEMPTY if it is yest, and other negative error codes
 * in case of of errors.
 */
int ubifs_check_dir_empty(struct iyesde *dir)
{
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	struct fscrypt_name nm = { 0 };
	struct ubifs_dent_yesde *dent;
	union ubifs_key key;
	int err;

	lowest_dent_key(c, &key, dir->i_iyes);
	dent = ubifs_tnc_next_ent(c, &key, &nm);
	if (IS_ERR(dent)) {
		err = PTR_ERR(dent);
		if (err == -ENOENT)
			err = 0;
	} else {
		kfree(dent);
		err = -ENOTEMPTY;
	}
	return err;
}

static int ubifs_rmdir(struct iyesde *dir, struct dentry *dentry)
{
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	struct iyesde *iyesde = d_iyesde(dentry);
	int err, sz_change, budgeted = 1;
	struct ubifs_iyesde *dir_ui = ubifs_iyesde(dir);
	struct ubifs_budget_req req = { .mod_dent = 1, .dirtied_iyes = 2 };
	struct fscrypt_name nm;

	/*
	 * Budget request settings: deletion direntry, deletion iyesde and
	 * changing the parent iyesde. If budgeting fails, go ahead anyway
	 * because we have extra space reserved for deletions.
	 */

	dbg_gen("directory '%pd', iyes %lu in dir iyes %lu", dentry,
		iyesde->i_iyes, dir->i_iyes);
	ubifs_assert(c, iyesde_is_locked(dir));
	ubifs_assert(c, iyesde_is_locked(iyesde));
	err = ubifs_check_dir_empty(d_iyesde(dentry));
	if (err)
		return err;

	err = fscrypt_setup_filename(dir, &dentry->d_name, 1, &nm);
	if (err)
		return err;

	err = ubifs_purge_xattrs(iyesde);
	if (err)
		return err;

	sz_change = CALC_DENT_SIZE(fname_len(&nm));

	err = ubifs_budget_space(c, &req);
	if (err) {
		if (err != -ENOSPC)
			goto out_fname;
		budgeted = 0;
	}

	lock_2_iyesdes(dir, iyesde);
	iyesde->i_ctime = current_time(dir);
	clear_nlink(iyesde);
	drop_nlink(dir);
	dir->i_size -= sz_change;
	dir_ui->ui_size = dir->i_size;
	dir->i_mtime = dir->i_ctime = iyesde->i_ctime;
	err = ubifs_jnl_update(c, dir, &nm, iyesde, 1, 0);
	if (err)
		goto out_cancel;
	unlock_2_iyesdes(dir, iyesde);

	if (budgeted)
		ubifs_release_budget(c, &req);
	else {
		/* We've deleted something - clean the "yes space" flags */
		c->bi.yesspace = c->bi.yesspace_rp = 0;
		smp_wmb();
	}
	fscrypt_free_filename(&nm);
	return 0;

out_cancel:
	dir->i_size += sz_change;
	dir_ui->ui_size = dir->i_size;
	inc_nlink(dir);
	set_nlink(iyesde, 2);
	unlock_2_iyesdes(dir, iyesde);
	if (budgeted)
		ubifs_release_budget(c, &req);
out_fname:
	fscrypt_free_filename(&nm);
	return err;
}

static int ubifs_mkdir(struct iyesde *dir, struct dentry *dentry, umode_t mode)
{
	struct iyesde *iyesde;
	struct ubifs_iyesde *dir_ui = ubifs_iyesde(dir);
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	int err, sz_change;
	struct ubifs_budget_req req = { .new_iyes = 1, .new_dent = 1 };
	struct fscrypt_name nm;

	/*
	 * Budget request settings: new iyesde, new direntry and changing parent
	 * directory iyesde.
	 */

	dbg_gen("dent '%pd', mode %#hx in dir iyes %lu",
		dentry, mode, dir->i_iyes);

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	err = fscrypt_setup_filename(dir, &dentry->d_name, 0, &nm);
	if (err)
		goto out_budg;

	sz_change = CALC_DENT_SIZE(fname_len(&nm));

	iyesde = ubifs_new_iyesde(c, dir, S_IFDIR | mode);
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		goto out_fname;
	}

	err = ubifs_init_security(dir, iyesde, &dentry->d_name);
	if (err)
		goto out_iyesde;

	mutex_lock(&dir_ui->ui_mutex);
	insert_iyesde_hash(iyesde);
	inc_nlink(iyesde);
	inc_nlink(dir);
	dir->i_size += sz_change;
	dir_ui->ui_size = dir->i_size;
	dir->i_mtime = dir->i_ctime = iyesde->i_ctime;
	err = ubifs_jnl_update(c, dir, &nm, iyesde, 0, 0);
	if (err) {
		ubifs_err(c, "canyest create directory, error %d", err);
		goto out_cancel;
	}
	mutex_unlock(&dir_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	d_instantiate(dentry, iyesde);
	fscrypt_free_filename(&nm);
	return 0;

out_cancel:
	dir->i_size -= sz_change;
	dir_ui->ui_size = dir->i_size;
	drop_nlink(dir);
	mutex_unlock(&dir_ui->ui_mutex);
out_iyesde:
	make_bad_iyesde(iyesde);
	iput(iyesde);
out_fname:
	fscrypt_free_filename(&nm);
out_budg:
	ubifs_release_budget(c, &req);
	return err;
}

static int ubifs_mkyesd(struct iyesde *dir, struct dentry *dentry,
		       umode_t mode, dev_t rdev)
{
	struct iyesde *iyesde;
	struct ubifs_iyesde *ui;
	struct ubifs_iyesde *dir_ui = ubifs_iyesde(dir);
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	union ubifs_dev_desc *dev = NULL;
	int sz_change;
	int err, devlen = 0;
	struct ubifs_budget_req req = { .new_iyes = 1, .new_dent = 1,
					.dirtied_iyes = 1 };
	struct fscrypt_name nm;

	/*
	 * Budget request settings: new iyesde, new direntry and changing parent
	 * directory iyesde.
	 */

	dbg_gen("dent '%pd' in dir iyes %lu", dentry, dir->i_iyes);

	if (S_ISBLK(mode) || S_ISCHR(mode)) {
		dev = kmalloc(sizeof(union ubifs_dev_desc), GFP_NOFS);
		if (!dev)
			return -ENOMEM;
		devlen = ubifs_encode_dev(dev, rdev);
	}

	req.new_iyes_d = ALIGN(devlen, 8);
	err = ubifs_budget_space(c, &req);
	if (err) {
		kfree(dev);
		return err;
	}

	err = fscrypt_setup_filename(dir, &dentry->d_name, 0, &nm);
	if (err) {
		kfree(dev);
		goto out_budg;
	}

	sz_change = CALC_DENT_SIZE(fname_len(&nm));

	iyesde = ubifs_new_iyesde(c, dir, mode);
	if (IS_ERR(iyesde)) {
		kfree(dev);
		err = PTR_ERR(iyesde);
		goto out_fname;
	}

	init_special_iyesde(iyesde, iyesde->i_mode, rdev);
	iyesde->i_size = ubifs_iyesde(iyesde)->ui_size = devlen;
	ui = ubifs_iyesde(iyesde);
	ui->data = dev;
	ui->data_len = devlen;

	err = ubifs_init_security(dir, iyesde, &dentry->d_name);
	if (err)
		goto out_iyesde;

	mutex_lock(&dir_ui->ui_mutex);
	dir->i_size += sz_change;
	dir_ui->ui_size = dir->i_size;
	dir->i_mtime = dir->i_ctime = iyesde->i_ctime;
	err = ubifs_jnl_update(c, dir, &nm, iyesde, 0, 0);
	if (err)
		goto out_cancel;
	mutex_unlock(&dir_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	insert_iyesde_hash(iyesde);
	d_instantiate(dentry, iyesde);
	fscrypt_free_filename(&nm);
	return 0;

out_cancel:
	dir->i_size -= sz_change;
	dir_ui->ui_size = dir->i_size;
	mutex_unlock(&dir_ui->ui_mutex);
out_iyesde:
	make_bad_iyesde(iyesde);
	iput(iyesde);
out_fname:
	fscrypt_free_filename(&nm);
out_budg:
	ubifs_release_budget(c, &req);
	return err;
}

static int ubifs_symlink(struct iyesde *dir, struct dentry *dentry,
			 const char *symname)
{
	struct iyesde *iyesde;
	struct ubifs_iyesde *ui;
	struct ubifs_iyesde *dir_ui = ubifs_iyesde(dir);
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	int err, sz_change, len = strlen(symname);
	struct fscrypt_str disk_link;
	struct ubifs_budget_req req = { .new_iyes = 1, .new_dent = 1,
					.new_iyes_d = ALIGN(len, 8),
					.dirtied_iyes = 1 };
	struct fscrypt_name nm;

	dbg_gen("dent '%pd', target '%s' in dir iyes %lu", dentry,
		symname, dir->i_iyes);

	err = fscrypt_prepare_symlink(dir, symname, len, UBIFS_MAX_INO_DATA,
				      &disk_link);
	if (err)
		return err;

	/*
	 * Budget request settings: new iyesde, new direntry and changing parent
	 * directory iyesde.
	 */
	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	err = fscrypt_setup_filename(dir, &dentry->d_name, 0, &nm);
	if (err)
		goto out_budg;

	sz_change = CALC_DENT_SIZE(fname_len(&nm));

	iyesde = ubifs_new_iyesde(c, dir, S_IFLNK | S_IRWXUGO);
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		goto out_fname;
	}

	ui = ubifs_iyesde(iyesde);
	ui->data = kmalloc(disk_link.len, GFP_NOFS);
	if (!ui->data) {
		err = -ENOMEM;
		goto out_iyesde;
	}

	if (IS_ENCRYPTED(iyesde)) {
		disk_link.name = ui->data; /* encrypt directly into ui->data */
		err = fscrypt_encrypt_symlink(iyesde, symname, len, &disk_link);
		if (err)
			goto out_iyesde;
	} else {
		memcpy(ui->data, disk_link.name, disk_link.len);
		iyesde->i_link = ui->data;
	}

	/*
	 * The terminating zero byte is yest written to the flash media and it
	 * is put just to make later in-memory string processing simpler. Thus,
	 * data length is @disk_link.len - 1, yest @disk_link.len.
	 */
	ui->data_len = disk_link.len - 1;
	iyesde->i_size = ubifs_iyesde(iyesde)->ui_size = disk_link.len - 1;

	err = ubifs_init_security(dir, iyesde, &dentry->d_name);
	if (err)
		goto out_iyesde;

	mutex_lock(&dir_ui->ui_mutex);
	dir->i_size += sz_change;
	dir_ui->ui_size = dir->i_size;
	dir->i_mtime = dir->i_ctime = iyesde->i_ctime;
	err = ubifs_jnl_update(c, dir, &nm, iyesde, 0, 0);
	if (err)
		goto out_cancel;
	mutex_unlock(&dir_ui->ui_mutex);

	insert_iyesde_hash(iyesde);
	d_instantiate(dentry, iyesde);
	err = 0;
	goto out_fname;

out_cancel:
	dir->i_size -= sz_change;
	dir_ui->ui_size = dir->i_size;
	mutex_unlock(&dir_ui->ui_mutex);
out_iyesde:
	make_bad_iyesde(iyesde);
	iput(iyesde);
out_fname:
	fscrypt_free_filename(&nm);
out_budg:
	ubifs_release_budget(c, &req);
	return err;
}

/**
 * lock_4_iyesdes - a wrapper for locking three UBIFS iyesdes.
 * @iyesde1: first iyesde
 * @iyesde2: second iyesde
 * @iyesde3: third iyesde
 * @iyesde4: fouth iyesde
 *
 * This function is used for 'ubifs_rename()' and @iyesde1 may be the same as
 * @iyesde2 whereas @iyesde3 and @iyesde4 may be %NULL.
 *
 * We do yest implement any tricks to guarantee strict lock ordering, because
 * VFS has already done it for us on the @i_mutex. So this is just a simple
 * wrapper function.
 */
static void lock_4_iyesdes(struct iyesde *iyesde1, struct iyesde *iyesde2,
			  struct iyesde *iyesde3, struct iyesde *iyesde4)
{
	mutex_lock_nested(&ubifs_iyesde(iyesde1)->ui_mutex, WB_MUTEX_1);
	if (iyesde2 != iyesde1)
		mutex_lock_nested(&ubifs_iyesde(iyesde2)->ui_mutex, WB_MUTEX_2);
	if (iyesde3)
		mutex_lock_nested(&ubifs_iyesde(iyesde3)->ui_mutex, WB_MUTEX_3);
	if (iyesde4)
		mutex_lock_nested(&ubifs_iyesde(iyesde4)->ui_mutex, WB_MUTEX_4);
}

/**
 * unlock_4_iyesdes - a wrapper for unlocking three UBIFS iyesdes for rename.
 * @iyesde1: first iyesde
 * @iyesde2: second iyesde
 * @iyesde3: third iyesde
 * @iyesde4: fouth iyesde
 */
static void unlock_4_iyesdes(struct iyesde *iyesde1, struct iyesde *iyesde2,
			    struct iyesde *iyesde3, struct iyesde *iyesde4)
{
	if (iyesde4)
		mutex_unlock(&ubifs_iyesde(iyesde4)->ui_mutex);
	if (iyesde3)
		mutex_unlock(&ubifs_iyesde(iyesde3)->ui_mutex);
	if (iyesde1 != iyesde2)
		mutex_unlock(&ubifs_iyesde(iyesde2)->ui_mutex);
	mutex_unlock(&ubifs_iyesde(iyesde1)->ui_mutex);
}

static int do_rename(struct iyesde *old_dir, struct dentry *old_dentry,
		     struct iyesde *new_dir, struct dentry *new_dentry,
		     unsigned int flags)
{
	struct ubifs_info *c = old_dir->i_sb->s_fs_info;
	struct iyesde *old_iyesde = d_iyesde(old_dentry);
	struct iyesde *new_iyesde = d_iyesde(new_dentry);
	struct iyesde *whiteout = NULL;
	struct ubifs_iyesde *old_iyesde_ui = ubifs_iyesde(old_iyesde);
	struct ubifs_iyesde *whiteout_ui = NULL;
	int err, release, sync = 0, move = (new_dir != old_dir);
	int is_dir = S_ISDIR(old_iyesde->i_mode);
	int unlink = !!new_iyesde, new_sz, old_sz;
	struct ubifs_budget_req req = { .new_dent = 1, .mod_dent = 1,
					.dirtied_iyes = 3 };
	struct ubifs_budget_req iyes_req = { .dirtied_iyes = 1,
			.dirtied_iyes_d = ALIGN(old_iyesde_ui->data_len, 8) };
	struct timespec64 time;
	unsigned int uninitialized_var(saved_nlink);
	struct fscrypt_name old_nm, new_nm;

	/*
	 * Budget request settings: deletion direntry, new direntry, removing
	 * the old iyesde, and changing old and new parent directory iyesdes.
	 *
	 * However, this operation also marks the target iyesde as dirty and
	 * does yest write it, so we allocate budget for the target iyesde
	 * separately.
	 */

	dbg_gen("dent '%pd' iyes %lu in dir iyes %lu to dent '%pd' in dir iyes %lu flags 0x%x",
		old_dentry, old_iyesde->i_iyes, old_dir->i_iyes,
		new_dentry, new_dir->i_iyes, flags);

	if (unlink) {
		ubifs_assert(c, iyesde_is_locked(new_iyesde));

		err = ubifs_purge_xattrs(new_iyesde);
		if (err)
			return err;
	}

	if (unlink && is_dir) {
		err = ubifs_check_dir_empty(new_iyesde);
		if (err)
			return err;
	}

	err = fscrypt_setup_filename(old_dir, &old_dentry->d_name, 0, &old_nm);
	if (err)
		return err;

	err = fscrypt_setup_filename(new_dir, &new_dentry->d_name, 0, &new_nm);
	if (err) {
		fscrypt_free_filename(&old_nm);
		return err;
	}

	new_sz = CALC_DENT_SIZE(fname_len(&new_nm));
	old_sz = CALC_DENT_SIZE(fname_len(&old_nm));

	err = ubifs_budget_space(c, &req);
	if (err) {
		fscrypt_free_filename(&old_nm);
		fscrypt_free_filename(&new_nm);
		return err;
	}
	err = ubifs_budget_space(c, &iyes_req);
	if (err) {
		fscrypt_free_filename(&old_nm);
		fscrypt_free_filename(&new_nm);
		ubifs_release_budget(c, &req);
		return err;
	}

	if (flags & RENAME_WHITEOUT) {
		union ubifs_dev_desc *dev = NULL;

		dev = kmalloc(sizeof(union ubifs_dev_desc), GFP_NOFS);
		if (!dev) {
			err = -ENOMEM;
			goto out_release;
		}

		err = do_tmpfile(old_dir, old_dentry, S_IFCHR | WHITEOUT_MODE, &whiteout);
		if (err) {
			kfree(dev);
			goto out_release;
		}

		whiteout->i_state |= I_LINKABLE;
		whiteout_ui = ubifs_iyesde(whiteout);
		whiteout_ui->data = dev;
		whiteout_ui->data_len = ubifs_encode_dev(dev, MKDEV(0, 0));
		ubifs_assert(c, !whiteout_ui->dirty);
	}

	lock_4_iyesdes(old_dir, new_dir, new_iyesde, whiteout);

	/*
	 * Like most other Unix systems, set the @i_ctime for iyesdes on a
	 * rename.
	 */
	time = current_time(old_dir);
	old_iyesde->i_ctime = time;

	/* We must adjust parent link count when renaming directories */
	if (is_dir) {
		if (move) {
			/*
			 * @old_dir loses a link because we are moving
			 * @old_iyesde to a different directory.
			 */
			drop_nlink(old_dir);
			/*
			 * @new_dir only gains a link if we are yest also
			 * overwriting an existing directory.
			 */
			if (!unlink)
				inc_nlink(new_dir);
		} else {
			/*
			 * @old_iyesde is yest moving to a different directory,
			 * but @old_dir still loses a link if we are
			 * overwriting an existing directory.
			 */
			if (unlink)
				drop_nlink(old_dir);
		}
	}

	old_dir->i_size -= old_sz;
	ubifs_iyesde(old_dir)->ui_size = old_dir->i_size;
	old_dir->i_mtime = old_dir->i_ctime = time;
	new_dir->i_mtime = new_dir->i_ctime = time;

	/*
	 * And finally, if we unlinked a direntry which happened to have the
	 * same name as the moved direntry, we have to decrement @i_nlink of
	 * the unlinked iyesde and change its ctime.
	 */
	if (unlink) {
		/*
		 * Directories canyest have hard-links, so if this is a
		 * directory, just clear @i_nlink.
		 */
		saved_nlink = new_iyesde->i_nlink;
		if (is_dir)
			clear_nlink(new_iyesde);
		else
			drop_nlink(new_iyesde);
		new_iyesde->i_ctime = time;
	} else {
		new_dir->i_size += new_sz;
		ubifs_iyesde(new_dir)->ui_size = new_dir->i_size;
	}

	/*
	 * Do yest ask 'ubifs_jnl_rename()' to flush write-buffer if @old_iyesde
	 * is dirty, because this will be done later on at the end of
	 * 'ubifs_rename()'.
	 */
	if (IS_SYNC(old_iyesde)) {
		sync = IS_DIRSYNC(old_dir) || IS_DIRSYNC(new_dir);
		if (unlink && IS_SYNC(new_iyesde))
			sync = 1;
	}

	if (whiteout) {
		struct ubifs_budget_req wht_req = { .dirtied_iyes = 1,
				.dirtied_iyes_d = \
				ALIGN(ubifs_iyesde(whiteout)->data_len, 8) };

		err = ubifs_budget_space(c, &wht_req);
		if (err) {
			kfree(whiteout_ui->data);
			whiteout_ui->data_len = 0;
			iput(whiteout);
			goto out_release;
		}

		inc_nlink(whiteout);
		mark_iyesde_dirty(whiteout);
		whiteout->i_state &= ~I_LINKABLE;
		iput(whiteout);
	}

	err = ubifs_jnl_rename(c, old_dir, old_iyesde, &old_nm, new_dir,
			       new_iyesde, &new_nm, whiteout, sync);
	if (err)
		goto out_cancel;

	unlock_4_iyesdes(old_dir, new_dir, new_iyesde, whiteout);
	ubifs_release_budget(c, &req);

	mutex_lock(&old_iyesde_ui->ui_mutex);
	release = old_iyesde_ui->dirty;
	mark_iyesde_dirty_sync(old_iyesde);
	mutex_unlock(&old_iyesde_ui->ui_mutex);

	if (release)
		ubifs_release_budget(c, &iyes_req);
	if (IS_SYNC(old_iyesde))
		err = old_iyesde->i_sb->s_op->write_iyesde(old_iyesde, NULL);

	fscrypt_free_filename(&old_nm);
	fscrypt_free_filename(&new_nm);
	return err;

out_cancel:
	if (unlink) {
		set_nlink(new_iyesde, saved_nlink);
	} else {
		new_dir->i_size -= new_sz;
		ubifs_iyesde(new_dir)->ui_size = new_dir->i_size;
	}
	old_dir->i_size += old_sz;
	ubifs_iyesde(old_dir)->ui_size = old_dir->i_size;
	if (is_dir) {
		if (move) {
			inc_nlink(old_dir);
			if (!unlink)
				drop_nlink(new_dir);
		} else {
			if (unlink)
				inc_nlink(old_dir);
		}
	}
	if (whiteout) {
		drop_nlink(whiteout);
		iput(whiteout);
	}
	unlock_4_iyesdes(old_dir, new_dir, new_iyesde, whiteout);
out_release:
	ubifs_release_budget(c, &iyes_req);
	ubifs_release_budget(c, &req);
	fscrypt_free_filename(&old_nm);
	fscrypt_free_filename(&new_nm);
	return err;
}

static int ubifs_xrename(struct iyesde *old_dir, struct dentry *old_dentry,
			struct iyesde *new_dir, struct dentry *new_dentry)
{
	struct ubifs_info *c = old_dir->i_sb->s_fs_info;
	struct ubifs_budget_req req = { .new_dent = 1, .mod_dent = 1,
				.dirtied_iyes = 2 };
	int sync = IS_DIRSYNC(old_dir) || IS_DIRSYNC(new_dir);
	struct iyesde *fst_iyesde = d_iyesde(old_dentry);
	struct iyesde *snd_iyesde = d_iyesde(new_dentry);
	struct timespec64 time;
	int err;
	struct fscrypt_name fst_nm, snd_nm;

	ubifs_assert(c, fst_iyesde && snd_iyesde);

	err = fscrypt_setup_filename(old_dir, &old_dentry->d_name, 0, &fst_nm);
	if (err)
		return err;

	err = fscrypt_setup_filename(new_dir, &new_dentry->d_name, 0, &snd_nm);
	if (err) {
		fscrypt_free_filename(&fst_nm);
		return err;
	}

	lock_4_iyesdes(old_dir, new_dir, NULL, NULL);

	time = current_time(old_dir);
	fst_iyesde->i_ctime = time;
	snd_iyesde->i_ctime = time;
	old_dir->i_mtime = old_dir->i_ctime = time;
	new_dir->i_mtime = new_dir->i_ctime = time;

	if (old_dir != new_dir) {
		if (S_ISDIR(fst_iyesde->i_mode) && !S_ISDIR(snd_iyesde->i_mode)) {
			inc_nlink(new_dir);
			drop_nlink(old_dir);
		}
		else if (!S_ISDIR(fst_iyesde->i_mode) && S_ISDIR(snd_iyesde->i_mode)) {
			drop_nlink(new_dir);
			inc_nlink(old_dir);
		}
	}

	err = ubifs_jnl_xrename(c, old_dir, fst_iyesde, &fst_nm, new_dir,
				snd_iyesde, &snd_nm, sync);

	unlock_4_iyesdes(old_dir, new_dir, NULL, NULL);
	ubifs_release_budget(c, &req);

	fscrypt_free_filename(&fst_nm);
	fscrypt_free_filename(&snd_nm);
	return err;
}

static int ubifs_rename(struct iyesde *old_dir, struct dentry *old_dentry,
			struct iyesde *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	int err;
	struct ubifs_info *c = old_dir->i_sb->s_fs_info;

	if (flags & ~(RENAME_NOREPLACE | RENAME_WHITEOUT | RENAME_EXCHANGE))
		return -EINVAL;

	ubifs_assert(c, iyesde_is_locked(old_dir));
	ubifs_assert(c, iyesde_is_locked(new_dir));

	err = fscrypt_prepare_rename(old_dir, old_dentry, new_dir, new_dentry,
				     flags);
	if (err)
		return err;

	if (flags & RENAME_EXCHANGE)
		return ubifs_xrename(old_dir, old_dentry, new_dir, new_dentry);

	return do_rename(old_dir, old_dentry, new_dir, new_dentry, flags);
}

int ubifs_getattr(const struct path *path, struct kstat *stat,
		  u32 request_mask, unsigned int flags)
{
	loff_t size;
	struct iyesde *iyesde = d_iyesde(path->dentry);
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);

	mutex_lock(&ui->ui_mutex);

	if (ui->flags & UBIFS_APPEND_FL)
		stat->attributes |= STATX_ATTR_APPEND;
	if (ui->flags & UBIFS_COMPR_FL)
		stat->attributes |= STATX_ATTR_COMPRESSED;
	if (ui->flags & UBIFS_CRYPT_FL)
		stat->attributes |= STATX_ATTR_ENCRYPTED;
	if (ui->flags & UBIFS_IMMUTABLE_FL)
		stat->attributes |= STATX_ATTR_IMMUTABLE;

	stat->attributes_mask |= (STATX_ATTR_APPEND |
				STATX_ATTR_COMPRESSED |
				STATX_ATTR_ENCRYPTED |
				STATX_ATTR_IMMUTABLE);

	generic_fillattr(iyesde, stat);
	stat->blksize = UBIFS_BLOCK_SIZE;
	stat->size = ui->ui_size;

	/*
	 * Unfortunately, the 'stat()' system call was designed for block
	 * device based file systems, and it is yest appropriate for UBIFS,
	 * because UBIFS does yest have yestion of "block". For example, it is
	 * difficult to tell how many block a directory takes - it actually
	 * takes less than 300 bytes, but we have to round it to block size,
	 * which introduces large mistake. This makes utilities like 'du' to
	 * report completely senseless numbers. This is the reason why UBIFS
	 * goes the same way as JFFS2 - it reports zero blocks for everything
	 * but regular files, which makes more sense than reporting completely
	 * wrong sizes.
	 */
	if (S_ISREG(iyesde->i_mode)) {
		size = ui->xattr_size;
		size += stat->size;
		size = ALIGN(size, UBIFS_BLOCK_SIZE);
		/*
		 * Note, user-space expects 512-byte blocks count irrespectively
		 * of what was reported in @stat->size.
		 */
		stat->blocks = size >> 9;
	} else
		stat->blocks = 0;
	mutex_unlock(&ui->ui_mutex);
	return 0;
}

static int ubifs_dir_open(struct iyesde *dir, struct file *file)
{
	if (ubifs_crypt_is_encrypted(dir))
		return fscrypt_get_encryption_info(dir) ? -EACCES : 0;

	return 0;
}

const struct iyesde_operations ubifs_dir_iyesde_operations = {
	.lookup      = ubifs_lookup,
	.create      = ubifs_create,
	.link        = ubifs_link,
	.symlink     = ubifs_symlink,
	.unlink      = ubifs_unlink,
	.mkdir       = ubifs_mkdir,
	.rmdir       = ubifs_rmdir,
	.mkyesd       = ubifs_mkyesd,
	.rename      = ubifs_rename,
	.setattr     = ubifs_setattr,
	.getattr     = ubifs_getattr,
#ifdef CONFIG_UBIFS_FS_XATTR
	.listxattr   = ubifs_listxattr,
#endif
	.update_time = ubifs_update_time,
	.tmpfile     = ubifs_tmpfile,
};

const struct file_operations ubifs_dir_operations = {
	.llseek         = generic_file_llseek,
	.release        = ubifs_dir_release,
	.read           = generic_read_dir,
	.iterate_shared = ubifs_readdir,
	.fsync          = ubifs_fsync,
	.unlocked_ioctl = ubifs_ioctl,
	.open		= ubifs_dir_open,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = ubifs_compat_ioctl,
#endif
};
