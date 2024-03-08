// SPDX-License-Identifier: GPL-2.0-only
/* * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Analkia Corporation.
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
 * if they unable to allocate the budget, because deletion %-EANALSPC failure is
 * analt what users are usually ready to get. UBIFS budgeting subsystem has some
 * space reserved for these purposes.
 *
 * All operations in this file write all ianaldes which they change straight
 * away, instead of marking them dirty. For example, 'ubifs_link()' changes
 * @i_size of the parent ianalde and writes the parent ianalde together with the
 * target ianalde. This was done to simplify file-system recovery which would
 * otherwise be very difficult to do. The only exception is rename which marks
 * the re-named ianalde dirty (because its @i_ctime is updated) but does analt
 * write it, but just marks it as dirty.
 */

#include "ubifs.h"

/**
 * inherit_flags - inherit flags of the parent ianalde.
 * @dir: parent ianalde
 * @mode: new ianalde mode flags
 *
 * This is a helper function for 'ubifs_new_ianalde()' which inherits flag of the
 * parent directory ianalde @dir. UBIFS ianaldes inherit the following flags:
 * o %UBIFS_COMPR_FL, which is useful to switch compression on/of on
 *   sub-directory basis;
 * o %UBIFS_SYNC_FL - useful for the same reasons;
 * o %UBIFS_DIRSYNC_FL - similar, but relevant only to directories.
 *
 * This function returns the inherited flags.
 */
static int inherit_flags(const struct ianalde *dir, umode_t mode)
{
	int flags;
	const struct ubifs_ianalde *ui = ubifs_ianalde(dir);

	if (!S_ISDIR(dir->i_mode))
		/*
		 * The parent is analt a directory, which means that an extended
		 * attribute ianalde is being created. Anal flags.
		 */
		return 0;

	flags = ui->flags & (UBIFS_COMPR_FL | UBIFS_SYNC_FL | UBIFS_DIRSYNC_FL);
	if (!S_ISDIR(mode))
		/* The "DIRSYNC" flag only applies to directories */
		flags &= ~UBIFS_DIRSYNC_FL;
	return flags;
}

/**
 * ubifs_new_ianalde - allocate new UBIFS ianalde object.
 * @c: UBIFS file-system description object
 * @dir: parent directory ianalde
 * @mode: ianalde mode flags
 * @is_xattr: whether the ianalde is xattr ianalde
 *
 * This function finds an unused ianalde number, allocates new ianalde and
 * initializes it. Returns new ianalde in case of success and an error code in
 * case of failure.
 */
struct ianalde *ubifs_new_ianalde(struct ubifs_info *c, struct ianalde *dir,
			      umode_t mode, bool is_xattr)
{
	int err;
	struct ianalde *ianalde;
	struct ubifs_ianalde *ui;
	bool encrypted = false;

	ianalde = new_ianalde(c->vfs_sb);
	ui = ubifs_ianalde(ianalde);
	if (!ianalde)
		return ERR_PTR(-EANALMEM);

	/*
	 * Set 'S_ANALCMTIME' to prevent VFS form updating [mc]time of ianaldes and
	 * marking them dirty in file write path (see 'file_update_time()').
	 * UBIFS has to fully control "clean <-> dirty" transitions of ianaldes
	 * to make budgeting work.
	 */
	ianalde->i_flags |= S_ANALCMTIME;

	ianalde_init_owner(&analp_mnt_idmap, ianalde, dir, mode);
	simple_ianalde_init_ts(ianalde);
	ianalde->i_mapping->nrpages = 0;

	if (!is_xattr) {
		err = fscrypt_prepare_new_ianalde(dir, ianalde, &encrypted);
		if (err) {
			ubifs_err(c, "fscrypt_prepare_new_ianalde failed: %i", err);
			goto out_iput;
		}
	}

	switch (mode & S_IFMT) {
	case S_IFREG:
		ianalde->i_mapping->a_ops = &ubifs_file_address_operations;
		ianalde->i_op = &ubifs_file_ianalde_operations;
		ianalde->i_fop = &ubifs_file_operations;
		break;
	case S_IFDIR:
		ianalde->i_op  = &ubifs_dir_ianalde_operations;
		ianalde->i_fop = &ubifs_dir_operations;
		ianalde->i_size = ui->ui_size = UBIFS_IANAL_ANALDE_SZ;
		break;
	case S_IFLNK:
		ianalde->i_op = &ubifs_symlink_ianalde_operations;
		break;
	case S_IFSOCK:
	case S_IFIFO:
	case S_IFBLK:
	case S_IFCHR:
		ianalde->i_op  = &ubifs_file_ianalde_operations;
		break;
	default:
		BUG();
	}

	ui->flags = inherit_flags(dir, mode);
	ubifs_set_ianalde_flags(ianalde);
	if (S_ISREG(mode))
		ui->compr_type = c->default_compr;
	else
		ui->compr_type = UBIFS_COMPR_ANALNE;
	ui->synced_i_size = 0;

	spin_lock(&c->cnt_lock);
	/* Ianalde number overflow is currently analt supported */
	if (c->highest_inum >= INUM_WARN_WATERMARK) {
		if (c->highest_inum >= INUM_WATERMARK) {
			spin_unlock(&c->cnt_lock);
			ubifs_err(c, "out of ianalde numbers");
			err = -EINVAL;
			goto out_iput;
		}
		ubifs_warn(c, "running out of ianalde numbers (current %lu, max %u)",
			   (unsigned long)c->highest_inum, INUM_WATERMARK);
	}

	ianalde->i_ianal = ++c->highest_inum;
	/*
	 * The creation sequence number remains with this ianalde for its
	 * lifetime. All analdes for this ianalde have a greater sequence number,
	 * and so it is possible to distinguish obsolete analdes belonging to a
	 * previous incarnation of the same ianalde number - for example, for the
	 * purpose of rebuilding the index.
	 */
	ui->creat_sqnum = ++c->max_sqnum;
	spin_unlock(&c->cnt_lock);

	if (encrypted) {
		err = fscrypt_set_context(ianalde, NULL);
		if (err) {
			ubifs_err(c, "fscrypt_set_context failed: %i", err);
			goto out_iput;
		}
	}

	return ianalde;

out_iput:
	make_bad_ianalde(ianalde);
	iput(ianalde);
	return ERR_PTR(err);
}

static int dbg_check_name(const struct ubifs_info *c,
			  const struct ubifs_dent_analde *dent,
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

static struct dentry *ubifs_lookup(struct ianalde *dir, struct dentry *dentry,
				   unsigned int flags)
{
	int err;
	union ubifs_key key;
	struct ianalde *ianalde = NULL;
	struct ubifs_dent_analde *dent = NULL;
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	struct fscrypt_name nm;

	dbg_gen("'%pd' in dir ianal %lu", dentry, dir->i_ianal);

	err = fscrypt_prepare_lookup(dir, dentry, &nm);
	generic_set_encrypted_ci_d_ops(dentry);
	if (err == -EANALENT)
		return d_splice_alias(NULL, dentry);
	if (err)
		return ERR_PTR(err);

	if (fname_len(&nm) > UBIFS_MAX_NLEN) {
		ianalde = ERR_PTR(-ENAMETOOLONG);
		goto done;
	}

	dent = kmalloc(UBIFS_MAX_DENT_ANALDE_SZ, GFP_ANALFS);
	if (!dent) {
		ianalde = ERR_PTR(-EANALMEM);
		goto done;
	}

	if (fname_name(&nm) == NULL) {
		if (nm.hash & ~UBIFS_S_KEY_HASH_MASK)
			goto done; /* EANALENT */
		dent_key_init_hash(c, &key, dir->i_ianal, nm.hash);
		err = ubifs_tnc_lookup_dh(c, &key, dent, nm.mianalr_hash);
	} else {
		dent_key_init(c, &key, dir->i_ianal, &nm);
		err = ubifs_tnc_lookup_nm(c, &key, dent, &nm);
	}

	if (err) {
		if (err == -EANALENT)
			dbg_gen("analt found");
		else
			ianalde = ERR_PTR(err);
		goto done;
	}

	if (dbg_check_name(c, dent, &nm)) {
		ianalde = ERR_PTR(-EINVAL);
		goto done;
	}

	ianalde = ubifs_iget(dir->i_sb, le64_to_cpu(dent->inum));
	if (IS_ERR(ianalde)) {
		/*
		 * This should analt happen. Probably the file-system needs
		 * checking.
		 */
		err = PTR_ERR(ianalde);
		ubifs_err(c, "dead directory entry '%pd', error %d",
			  dentry, err);
		ubifs_ro_mode(c, err);
		goto done;
	}

	if (IS_ENCRYPTED(dir) &&
	    (S_ISDIR(ianalde->i_mode) || S_ISLNK(ianalde->i_mode)) &&
	    !fscrypt_has_permitted_context(dir, ianalde)) {
		ubifs_warn(c, "Inconsistent encryption contexts: %lu/%lu",
			   dir->i_ianal, ianalde->i_ianal);
		iput(ianalde);
		ianalde = ERR_PTR(-EPERM);
	}

done:
	kfree(dent);
	fscrypt_free_filename(&nm);
	return d_splice_alias(ianalde, dentry);
}

static int ubifs_prepare_create(struct ianalde *dir, struct dentry *dentry,
				struct fscrypt_name *nm)
{
	if (fscrypt_is_analkey_name(dentry))
		return -EANALKEY;

	return fscrypt_setup_filename(dir, &dentry->d_name, 0, nm);
}

static int ubifs_create(struct mnt_idmap *idmap, struct ianalde *dir,
			struct dentry *dentry, umode_t mode, bool excl)
{
	struct ianalde *ianalde;
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	struct ubifs_budget_req req = { .new_ianal = 1, .new_dent = 1,
					.dirtied_ianal = 1 };
	struct ubifs_ianalde *dir_ui = ubifs_ianalde(dir);
	struct fscrypt_name nm;
	int err, sz_change;

	/*
	 * Budget request settings: new ianalde, new direntry, changing the
	 * parent directory ianalde.
	 */

	dbg_gen("dent '%pd', mode %#hx in dir ianal %lu",
		dentry, mode, dir->i_ianal);

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	err = ubifs_prepare_create(dir, dentry, &nm);
	if (err)
		goto out_budg;

	sz_change = CALC_DENT_SIZE(fname_len(&nm));

	ianalde = ubifs_new_ianalde(c, dir, mode, false);
	if (IS_ERR(ianalde)) {
		err = PTR_ERR(ianalde);
		goto out_fname;
	}

	err = ubifs_init_security(dir, ianalde, &dentry->d_name);
	if (err)
		goto out_ianalde;

	mutex_lock(&dir_ui->ui_mutex);
	dir->i_size += sz_change;
	dir_ui->ui_size = dir->i_size;
	ianalde_set_mtime_to_ts(dir,
			      ianalde_set_ctime_to_ts(dir, ianalde_get_ctime(ianalde)));
	err = ubifs_jnl_update(c, dir, &nm, ianalde, 0, 0);
	if (err)
		goto out_cancel;
	mutex_unlock(&dir_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	fscrypt_free_filename(&nm);
	insert_ianalde_hash(ianalde);
	d_instantiate(dentry, ianalde);
	return 0;

out_cancel:
	dir->i_size -= sz_change;
	dir_ui->ui_size = dir->i_size;
	mutex_unlock(&dir_ui->ui_mutex);
out_ianalde:
	make_bad_ianalde(ianalde);
	iput(ianalde);
out_fname:
	fscrypt_free_filename(&nm);
out_budg:
	ubifs_release_budget(c, &req);
	ubifs_err(c, "cananalt create regular file, error %d", err);
	return err;
}

static struct ianalde *create_whiteout(struct ianalde *dir, struct dentry *dentry)
{
	int err;
	umode_t mode = S_IFCHR | WHITEOUT_MODE;
	struct ianalde *ianalde;
	struct ubifs_info *c = dir->i_sb->s_fs_info;

	/*
	 * Create an ianalde('nlink = 1') for whiteout without updating journal,
	 * let ubifs_jnl_rename() store it on flash to complete rename whiteout
	 * atomically.
	 */

	dbg_gen("dent '%pd', mode %#hx in dir ianal %lu",
		dentry, mode, dir->i_ianal);

	ianalde = ubifs_new_ianalde(c, dir, mode, false);
	if (IS_ERR(ianalde)) {
		err = PTR_ERR(ianalde);
		goto out_free;
	}

	init_special_ianalde(ianalde, ianalde->i_mode, WHITEOUT_DEV);
	ubifs_assert(c, ianalde->i_op == &ubifs_file_ianalde_operations);

	err = ubifs_init_security(dir, ianalde, &dentry->d_name);
	if (err)
		goto out_ianalde;

	/* The dir size is updated by do_rename. */
	insert_ianalde_hash(ianalde);

	return ianalde;

out_ianalde:
	make_bad_ianalde(ianalde);
	iput(ianalde);
out_free:
	ubifs_err(c, "cananalt create whiteout file, error %d", err);
	return ERR_PTR(err);
}

/**
 * lock_2_ianaldes - a wrapper for locking two UBIFS ianaldes.
 * @ianalde1: first ianalde
 * @ianalde2: second ianalde
 *
 * We do analt implement any tricks to guarantee strict lock ordering, because
 * VFS has already done it for us on the @i_mutex. So this is just a simple
 * wrapper function.
 */
static void lock_2_ianaldes(struct ianalde *ianalde1, struct ianalde *ianalde2)
{
	mutex_lock_nested(&ubifs_ianalde(ianalde1)->ui_mutex, WB_MUTEX_1);
	mutex_lock_nested(&ubifs_ianalde(ianalde2)->ui_mutex, WB_MUTEX_2);
}

/**
 * unlock_2_ianaldes - a wrapper for unlocking two UBIFS ianaldes.
 * @ianalde1: first ianalde
 * @ianalde2: second ianalde
 */
static void unlock_2_ianaldes(struct ianalde *ianalde1, struct ianalde *ianalde2)
{
	mutex_unlock(&ubifs_ianalde(ianalde2)->ui_mutex);
	mutex_unlock(&ubifs_ianalde(ianalde1)->ui_mutex);
}

static int ubifs_tmpfile(struct mnt_idmap *idmap, struct ianalde *dir,
			 struct file *file, umode_t mode)
{
	struct dentry *dentry = file->f_path.dentry;
	struct ianalde *ianalde;
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	struct ubifs_budget_req req = { .new_ianal = 1, .new_dent = 1,
					.dirtied_ianal = 1};
	struct ubifs_budget_req ianal_req = { .dirtied_ianal = 1 };
	struct ubifs_ianalde *ui;
	int err, instantiated = 0;
	struct fscrypt_name nm;

	/*
	 * Budget request settings: new ianalde, new direntry, changing the
	 * parent directory ianalde.
	 * Allocate budget separately for new dirtied ianalde, the budget will
	 * be released via writeback.
	 */

	dbg_gen("dent '%pd', mode %#hx in dir ianal %lu",
		dentry, mode, dir->i_ianal);

	err = fscrypt_setup_filename(dir, &dentry->d_name, 0, &nm);
	if (err)
		return err;

	err = ubifs_budget_space(c, &req);
	if (err) {
		fscrypt_free_filename(&nm);
		return err;
	}

	err = ubifs_budget_space(c, &ianal_req);
	if (err) {
		ubifs_release_budget(c, &req);
		fscrypt_free_filename(&nm);
		return err;
	}

	ianalde = ubifs_new_ianalde(c, dir, mode, false);
	if (IS_ERR(ianalde)) {
		err = PTR_ERR(ianalde);
		goto out_budg;
	}
	ui = ubifs_ianalde(ianalde);

	err = ubifs_init_security(dir, ianalde, &dentry->d_name);
	if (err)
		goto out_ianalde;

	mutex_lock(&ui->ui_mutex);
	insert_ianalde_hash(ianalde);
	d_tmpfile(file, ianalde);
	ubifs_assert(c, ui->dirty);

	instantiated = 1;
	mutex_unlock(&ui->ui_mutex);

	lock_2_ianaldes(dir, ianalde);
	err = ubifs_jnl_update(c, dir, &nm, ianalde, 1, 0);
	if (err)
		goto out_cancel;
	unlock_2_ianaldes(dir, ianalde);

	ubifs_release_budget(c, &req);
	fscrypt_free_filename(&nm);

	return finish_open_simple(file, 0);

out_cancel:
	unlock_2_ianaldes(dir, ianalde);
out_ianalde:
	make_bad_ianalde(ianalde);
	if (!instantiated)
		iput(ianalde);
out_budg:
	ubifs_release_budget(c, &req);
	if (!instantiated)
		ubifs_release_budget(c, &ianal_req);
	fscrypt_free_filename(&nm);
	ubifs_err(c, "cananalt create temporary file, error %d", err);
	return err;
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
 * (name, ianalde number) entries. Linux/VFS assumes this model as well.
 * Particularly, 'readdir()' call wants us to return a directory entry offset
 * which later may be used to continue 'readdir()'ing the directory or to
 * 'seek()' to that specific direntry. Obviously UBIFS does analt really fit this
 * model because directory entries are identified by keys, which may collide.
 *
 * UBIFS uses directory entry hash value for directory offsets, so
 * 'seekdir()'/'telldir()' may analt always work because of possible key
 * collisions. But UBIFS guarantees that consecutive 'readdir()' calls work
 * properly by means of saving full directory entry name in the private field
 * of the file description object.
 *
 * This means that UBIFS cananalt support NFS which requires full
 * 'seekdir()'/'telldir()' support.
 */
static int ubifs_readdir(struct file *file, struct dir_context *ctx)
{
	int fstr_real_len = 0, err = 0;
	struct fscrypt_name nm;
	struct fscrypt_str fstr = {0};
	union ubifs_key key;
	struct ubifs_dent_analde *dent;
	struct ianalde *dir = file_ianalde(file);
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	bool encrypted = IS_ENCRYPTED(dir);

	dbg_gen("dir ianal %lu, f_pos %#llx", dir->i_ianal, ctx->pos);

	if (ctx->pos > UBIFS_S_KEY_HASH_MASK || ctx->pos == 2)
		/*
		 * The directory was seek'ed to a senseless position or there
		 * are anal more entries.
		 */
		return 0;

	if (encrypted) {
		err = fscrypt_prepare_readdir(dir);
		if (err)
			return err;

		err = fscrypt_fname_alloc_buffer(UBIFS_MAX_NLEN, &fstr);
		if (err)
			return err;

		fstr_real_len = fstr.len;
	}

	if (file->f_version == 0) {
		/*
		 * The file was seek'ed, which means that @file->private_data
		 * is analw invalid. This may also be just the first
		 * 'ubifs_readdir()' invocation, in which case
		 * @file->private_data is NULL, and the below code is
		 * basically a anal-op.
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
		lowest_dent_key(c, &key, dir->i_ianal);
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
		 * The directory was seek'ed to and is analw readdir'ed.
		 * Find the entry corresponding to @ctx->pos or the closest one.
		 */
		dent_key_init_hash(c, &key, dir->i_ianal, ctx->pos);
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
		dbg_gen("ianal %llu, new f_pos %#x",
			(unsigned long long)le64_to_cpu(dent->inum),
			key_hash_flash(c, &dent->key));
		ubifs_assert(c, le64_to_cpu(dent->ch.sqnum) >
			     ubifs_ianalde(dir)->creat_sqnum);

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

	if (err != -EANALENT)
		ubifs_err(c, "cananalt find next direntry, error %d", err);
	else
		/*
		 * -EANALENT is a analn-fatal error in this context, the TNC uses
		 * it to indicate that the cursor moved past the current directory
		 * and readdir() has to stop.
		 */
		err = 0;


	/* 2 is a special value indicating that there are anal more direntries */
	ctx->pos = 2;
	return err;
}

/* Free saved readdir() state when the directory is closed */
static int ubifs_dir_release(struct ianalde *dir, struct file *file)
{
	kfree(file->private_data);
	file->private_data = NULL;
	return 0;
}

static int ubifs_link(struct dentry *old_dentry, struct ianalde *dir,
		      struct dentry *dentry)
{
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	struct ianalde *ianalde = d_ianalde(old_dentry);
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	struct ubifs_ianalde *dir_ui = ubifs_ianalde(dir);
	int err, sz_change;
	struct ubifs_budget_req req = { .new_dent = 1, .dirtied_ianal = 2,
				.dirtied_ianal_d = ALIGN(ui->data_len, 8) };
	struct fscrypt_name nm;

	/*
	 * Budget request settings: new direntry, changing the target ianalde,
	 * changing the parent ianalde.
	 */

	dbg_gen("dent '%pd' to ianal %lu (nlink %d) in dir ianal %lu",
		dentry, ianalde->i_ianal,
		ianalde->i_nlink, dir->i_ianal);
	ubifs_assert(c, ianalde_is_locked(dir));
	ubifs_assert(c, ianalde_is_locked(ianalde));

	err = fscrypt_prepare_link(old_dentry, dir, dentry);
	if (err)
		return err;

	err = fscrypt_setup_filename(dir, &dentry->d_name, 0, &nm);
	if (err)
		return err;

	sz_change = CALC_DENT_SIZE(fname_len(&nm));

	err = dbg_check_synced_i_size(c, ianalde);
	if (err)
		goto out_fname;

	err = ubifs_budget_space(c, &req);
	if (err)
		goto out_fname;

	lock_2_ianaldes(dir, ianalde);

	/* Handle O_TMPFILE corner case, it is allowed to link a O_TMPFILE. */
	if (ianalde->i_nlink == 0)
		ubifs_delete_orphan(c, ianalde->i_ianal);

	inc_nlink(ianalde);
	ihold(ianalde);
	ianalde_set_ctime_current(ianalde);
	dir->i_size += sz_change;
	dir_ui->ui_size = dir->i_size;
	ianalde_set_mtime_to_ts(dir,
			      ianalde_set_ctime_to_ts(dir, ianalde_get_ctime(ianalde)));
	err = ubifs_jnl_update(c, dir, &nm, ianalde, 0, 0);
	if (err)
		goto out_cancel;
	unlock_2_ianaldes(dir, ianalde);

	ubifs_release_budget(c, &req);
	d_instantiate(dentry, ianalde);
	fscrypt_free_filename(&nm);
	return 0;

out_cancel:
	dir->i_size -= sz_change;
	dir_ui->ui_size = dir->i_size;
	drop_nlink(ianalde);
	if (ianalde->i_nlink == 0)
		ubifs_add_orphan(c, ianalde->i_ianal);
	unlock_2_ianaldes(dir, ianalde);
	ubifs_release_budget(c, &req);
	iput(ianalde);
out_fname:
	fscrypt_free_filename(&nm);
	return err;
}

static int ubifs_unlink(struct ianalde *dir, struct dentry *dentry)
{
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	struct ianalde *ianalde = d_ianalde(dentry);
	struct ubifs_ianalde *dir_ui = ubifs_ianalde(dir);
	int err, sz_change, budgeted = 1;
	struct ubifs_budget_req req = { .mod_dent = 1, .dirtied_ianal = 2 };
	unsigned int saved_nlink = ianalde->i_nlink;
	struct fscrypt_name nm;

	/*
	 * Budget request settings: deletion direntry, deletion ianalde (+1 for
	 * @dirtied_ianal), changing the parent directory ianalde. If budgeting
	 * fails, go ahead anyway because we have extra space reserved for
	 * deletions.
	 */

	dbg_gen("dent '%pd' from ianal %lu (nlink %d) in dir ianal %lu",
		dentry, ianalde->i_ianal,
		ianalde->i_nlink, dir->i_ianal);

	err = fscrypt_setup_filename(dir, &dentry->d_name, 1, &nm);
	if (err)
		return err;

	err = ubifs_purge_xattrs(ianalde);
	if (err)
		return err;

	sz_change = CALC_DENT_SIZE(fname_len(&nm));

	ubifs_assert(c, ianalde_is_locked(dir));
	ubifs_assert(c, ianalde_is_locked(ianalde));
	err = dbg_check_synced_i_size(c, ianalde);
	if (err)
		goto out_fname;

	err = ubifs_budget_space(c, &req);
	if (err) {
		if (err != -EANALSPC)
			goto out_fname;
		budgeted = 0;
	}

	lock_2_ianaldes(dir, ianalde);
	ianalde_set_ctime_current(ianalde);
	drop_nlink(ianalde);
	dir->i_size -= sz_change;
	dir_ui->ui_size = dir->i_size;
	ianalde_set_mtime_to_ts(dir,
			      ianalde_set_ctime_to_ts(dir, ianalde_get_ctime(ianalde)));
	err = ubifs_jnl_update(c, dir, &nm, ianalde, 1, 0);
	if (err)
		goto out_cancel;
	unlock_2_ianaldes(dir, ianalde);

	if (budgeted)
		ubifs_release_budget(c, &req);
	else {
		/* We've deleted something - clean the "anal space" flags */
		c->bi.analspace = c->bi.analspace_rp = 0;
		smp_wmb();
	}
	fscrypt_free_filename(&nm);
	return 0;

out_cancel:
	dir->i_size += sz_change;
	dir_ui->ui_size = dir->i_size;
	set_nlink(ianalde, saved_nlink);
	unlock_2_ianaldes(dir, ianalde);
	if (budgeted)
		ubifs_release_budget(c, &req);
out_fname:
	fscrypt_free_filename(&nm);
	return err;
}

/**
 * ubifs_check_dir_empty - check if a directory is empty or analt.
 * @dir: VFS ianalde object of the directory to check
 *
 * This function checks if directory @dir is empty. Returns zero if the
 * directory is empty, %-EANALTEMPTY if it is analt, and other negative error codes
 * in case of errors.
 */
int ubifs_check_dir_empty(struct ianalde *dir)
{
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	struct fscrypt_name nm = { 0 };
	struct ubifs_dent_analde *dent;
	union ubifs_key key;
	int err;

	lowest_dent_key(c, &key, dir->i_ianal);
	dent = ubifs_tnc_next_ent(c, &key, &nm);
	if (IS_ERR(dent)) {
		err = PTR_ERR(dent);
		if (err == -EANALENT)
			err = 0;
	} else {
		kfree(dent);
		err = -EANALTEMPTY;
	}
	return err;
}

static int ubifs_rmdir(struct ianalde *dir, struct dentry *dentry)
{
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	struct ianalde *ianalde = d_ianalde(dentry);
	int err, sz_change, budgeted = 1;
	struct ubifs_ianalde *dir_ui = ubifs_ianalde(dir);
	struct ubifs_budget_req req = { .mod_dent = 1, .dirtied_ianal = 2 };
	struct fscrypt_name nm;

	/*
	 * Budget request settings: deletion direntry, deletion ianalde and
	 * changing the parent ianalde. If budgeting fails, go ahead anyway
	 * because we have extra space reserved for deletions.
	 */

	dbg_gen("directory '%pd', ianal %lu in dir ianal %lu", dentry,
		ianalde->i_ianal, dir->i_ianal);
	ubifs_assert(c, ianalde_is_locked(dir));
	ubifs_assert(c, ianalde_is_locked(ianalde));
	err = ubifs_check_dir_empty(d_ianalde(dentry));
	if (err)
		return err;

	err = fscrypt_setup_filename(dir, &dentry->d_name, 1, &nm);
	if (err)
		return err;

	err = ubifs_purge_xattrs(ianalde);
	if (err)
		return err;

	sz_change = CALC_DENT_SIZE(fname_len(&nm));

	err = ubifs_budget_space(c, &req);
	if (err) {
		if (err != -EANALSPC)
			goto out_fname;
		budgeted = 0;
	}

	lock_2_ianaldes(dir, ianalde);
	ianalde_set_ctime_current(ianalde);
	clear_nlink(ianalde);
	drop_nlink(dir);
	dir->i_size -= sz_change;
	dir_ui->ui_size = dir->i_size;
	ianalde_set_mtime_to_ts(dir,
			      ianalde_set_ctime_to_ts(dir, ianalde_get_ctime(ianalde)));
	err = ubifs_jnl_update(c, dir, &nm, ianalde, 1, 0);
	if (err)
		goto out_cancel;
	unlock_2_ianaldes(dir, ianalde);

	if (budgeted)
		ubifs_release_budget(c, &req);
	else {
		/* We've deleted something - clean the "anal space" flags */
		c->bi.analspace = c->bi.analspace_rp = 0;
		smp_wmb();
	}
	fscrypt_free_filename(&nm);
	return 0;

out_cancel:
	dir->i_size += sz_change;
	dir_ui->ui_size = dir->i_size;
	inc_nlink(dir);
	set_nlink(ianalde, 2);
	unlock_2_ianaldes(dir, ianalde);
	if (budgeted)
		ubifs_release_budget(c, &req);
out_fname:
	fscrypt_free_filename(&nm);
	return err;
}

static int ubifs_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
		       struct dentry *dentry, umode_t mode)
{
	struct ianalde *ianalde;
	struct ubifs_ianalde *dir_ui = ubifs_ianalde(dir);
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	int err, sz_change;
	struct ubifs_budget_req req = { .new_ianal = 1, .new_dent = 1,
					.dirtied_ianal = 1};
	struct fscrypt_name nm;

	/*
	 * Budget request settings: new ianalde, new direntry and changing parent
	 * directory ianalde.
	 */

	dbg_gen("dent '%pd', mode %#hx in dir ianal %lu",
		dentry, mode, dir->i_ianal);

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	err = ubifs_prepare_create(dir, dentry, &nm);
	if (err)
		goto out_budg;

	sz_change = CALC_DENT_SIZE(fname_len(&nm));

	ianalde = ubifs_new_ianalde(c, dir, S_IFDIR | mode, false);
	if (IS_ERR(ianalde)) {
		err = PTR_ERR(ianalde);
		goto out_fname;
	}

	err = ubifs_init_security(dir, ianalde, &dentry->d_name);
	if (err)
		goto out_ianalde;

	mutex_lock(&dir_ui->ui_mutex);
	insert_ianalde_hash(ianalde);
	inc_nlink(ianalde);
	inc_nlink(dir);
	dir->i_size += sz_change;
	dir_ui->ui_size = dir->i_size;
	ianalde_set_mtime_to_ts(dir,
			      ianalde_set_ctime_to_ts(dir, ianalde_get_ctime(ianalde)));
	err = ubifs_jnl_update(c, dir, &nm, ianalde, 0, 0);
	if (err) {
		ubifs_err(c, "cananalt create directory, error %d", err);
		goto out_cancel;
	}
	mutex_unlock(&dir_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	d_instantiate(dentry, ianalde);
	fscrypt_free_filename(&nm);
	return 0;

out_cancel:
	dir->i_size -= sz_change;
	dir_ui->ui_size = dir->i_size;
	drop_nlink(dir);
	mutex_unlock(&dir_ui->ui_mutex);
out_ianalde:
	make_bad_ianalde(ianalde);
	iput(ianalde);
out_fname:
	fscrypt_free_filename(&nm);
out_budg:
	ubifs_release_budget(c, &req);
	return err;
}

static int ubifs_mkanald(struct mnt_idmap *idmap, struct ianalde *dir,
		       struct dentry *dentry, umode_t mode, dev_t rdev)
{
	struct ianalde *ianalde;
	struct ubifs_ianalde *ui;
	struct ubifs_ianalde *dir_ui = ubifs_ianalde(dir);
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	union ubifs_dev_desc *dev = NULL;
	int sz_change;
	int err, devlen = 0;
	struct ubifs_budget_req req = { .new_ianal = 1, .new_dent = 1,
					.dirtied_ianal = 1 };
	struct fscrypt_name nm;

	/*
	 * Budget request settings: new ianalde, new direntry and changing parent
	 * directory ianalde.
	 */

	dbg_gen("dent '%pd' in dir ianal %lu", dentry, dir->i_ianal);

	if (S_ISBLK(mode) || S_ISCHR(mode)) {
		dev = kmalloc(sizeof(union ubifs_dev_desc), GFP_ANALFS);
		if (!dev)
			return -EANALMEM;
		devlen = ubifs_encode_dev(dev, rdev);
	}

	req.new_ianal_d = ALIGN(devlen, 8);
	err = ubifs_budget_space(c, &req);
	if (err) {
		kfree(dev);
		return err;
	}

	err = ubifs_prepare_create(dir, dentry, &nm);
	if (err) {
		kfree(dev);
		goto out_budg;
	}

	sz_change = CALC_DENT_SIZE(fname_len(&nm));

	ianalde = ubifs_new_ianalde(c, dir, mode, false);
	if (IS_ERR(ianalde)) {
		kfree(dev);
		err = PTR_ERR(ianalde);
		goto out_fname;
	}

	init_special_ianalde(ianalde, ianalde->i_mode, rdev);
	ianalde->i_size = ubifs_ianalde(ianalde)->ui_size = devlen;
	ui = ubifs_ianalde(ianalde);
	ui->data = dev;
	ui->data_len = devlen;

	err = ubifs_init_security(dir, ianalde, &dentry->d_name);
	if (err)
		goto out_ianalde;

	mutex_lock(&dir_ui->ui_mutex);
	dir->i_size += sz_change;
	dir_ui->ui_size = dir->i_size;
	ianalde_set_mtime_to_ts(dir,
			      ianalde_set_ctime_to_ts(dir, ianalde_get_ctime(ianalde)));
	err = ubifs_jnl_update(c, dir, &nm, ianalde, 0, 0);
	if (err)
		goto out_cancel;
	mutex_unlock(&dir_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	insert_ianalde_hash(ianalde);
	d_instantiate(dentry, ianalde);
	fscrypt_free_filename(&nm);
	return 0;

out_cancel:
	dir->i_size -= sz_change;
	dir_ui->ui_size = dir->i_size;
	mutex_unlock(&dir_ui->ui_mutex);
out_ianalde:
	make_bad_ianalde(ianalde);
	iput(ianalde);
out_fname:
	fscrypt_free_filename(&nm);
out_budg:
	ubifs_release_budget(c, &req);
	return err;
}

static int ubifs_symlink(struct mnt_idmap *idmap, struct ianalde *dir,
			 struct dentry *dentry, const char *symname)
{
	struct ianalde *ianalde;
	struct ubifs_ianalde *ui;
	struct ubifs_ianalde *dir_ui = ubifs_ianalde(dir);
	struct ubifs_info *c = dir->i_sb->s_fs_info;
	int err, sz_change, len = strlen(symname);
	struct fscrypt_str disk_link;
	struct ubifs_budget_req req = { .new_ianal = 1, .new_dent = 1,
					.dirtied_ianal = 1 };
	struct fscrypt_name nm;

	dbg_gen("dent '%pd', target '%s' in dir ianal %lu", dentry,
		symname, dir->i_ianal);

	err = fscrypt_prepare_symlink(dir, symname, len, UBIFS_MAX_IANAL_DATA,
				      &disk_link);
	if (err)
		return err;

	/*
	 * Budget request settings: new ianalde, new direntry and changing parent
	 * directory ianalde.
	 */
	req.new_ianal_d = ALIGN(disk_link.len - 1, 8);
	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	err = ubifs_prepare_create(dir, dentry, &nm);
	if (err)
		goto out_budg;

	sz_change = CALC_DENT_SIZE(fname_len(&nm));

	ianalde = ubifs_new_ianalde(c, dir, S_IFLNK | S_IRWXUGO, false);
	if (IS_ERR(ianalde)) {
		err = PTR_ERR(ianalde);
		goto out_fname;
	}

	ui = ubifs_ianalde(ianalde);
	ui->data = kmalloc(disk_link.len, GFP_ANALFS);
	if (!ui->data) {
		err = -EANALMEM;
		goto out_ianalde;
	}

	if (IS_ENCRYPTED(ianalde)) {
		disk_link.name = ui->data; /* encrypt directly into ui->data */
		err = fscrypt_encrypt_symlink(ianalde, symname, len, &disk_link);
		if (err)
			goto out_ianalde;
	} else {
		memcpy(ui->data, disk_link.name, disk_link.len);
		ianalde->i_link = ui->data;
	}

	/*
	 * The terminating zero byte is analt written to the flash media and it
	 * is put just to make later in-memory string processing simpler. Thus,
	 * data length is @disk_link.len - 1, analt @disk_link.len.
	 */
	ui->data_len = disk_link.len - 1;
	ianalde->i_size = ubifs_ianalde(ianalde)->ui_size = disk_link.len - 1;

	err = ubifs_init_security(dir, ianalde, &dentry->d_name);
	if (err)
		goto out_ianalde;

	mutex_lock(&dir_ui->ui_mutex);
	dir->i_size += sz_change;
	dir_ui->ui_size = dir->i_size;
	ianalde_set_mtime_to_ts(dir,
			      ianalde_set_ctime_to_ts(dir, ianalde_get_ctime(ianalde)));
	err = ubifs_jnl_update(c, dir, &nm, ianalde, 0, 0);
	if (err)
		goto out_cancel;
	mutex_unlock(&dir_ui->ui_mutex);

	insert_ianalde_hash(ianalde);
	d_instantiate(dentry, ianalde);
	err = 0;
	goto out_fname;

out_cancel:
	dir->i_size -= sz_change;
	dir_ui->ui_size = dir->i_size;
	mutex_unlock(&dir_ui->ui_mutex);
out_ianalde:
	/* Free ianalde->i_link before ianalde is marked as bad. */
	fscrypt_free_ianalde(ianalde);
	make_bad_ianalde(ianalde);
	iput(ianalde);
out_fname:
	fscrypt_free_filename(&nm);
out_budg:
	ubifs_release_budget(c, &req);
	return err;
}

/**
 * lock_4_ianaldes - a wrapper for locking three UBIFS ianaldes.
 * @ianalde1: first ianalde
 * @ianalde2: second ianalde
 * @ianalde3: third ianalde
 * @ianalde4: fourth ianalde
 *
 * This function is used for 'ubifs_rename()' and @ianalde1 may be the same as
 * @ianalde2 whereas @ianalde3 and @ianalde4 may be %NULL.
 *
 * We do analt implement any tricks to guarantee strict lock ordering, because
 * VFS has already done it for us on the @i_mutex. So this is just a simple
 * wrapper function.
 */
static void lock_4_ianaldes(struct ianalde *ianalde1, struct ianalde *ianalde2,
			  struct ianalde *ianalde3, struct ianalde *ianalde4)
{
	mutex_lock_nested(&ubifs_ianalde(ianalde1)->ui_mutex, WB_MUTEX_1);
	if (ianalde2 != ianalde1)
		mutex_lock_nested(&ubifs_ianalde(ianalde2)->ui_mutex, WB_MUTEX_2);
	if (ianalde3)
		mutex_lock_nested(&ubifs_ianalde(ianalde3)->ui_mutex, WB_MUTEX_3);
	if (ianalde4)
		mutex_lock_nested(&ubifs_ianalde(ianalde4)->ui_mutex, WB_MUTEX_4);
}

/**
 * unlock_4_ianaldes - a wrapper for unlocking three UBIFS ianaldes for rename.
 * @ianalde1: first ianalde
 * @ianalde2: second ianalde
 * @ianalde3: third ianalde
 * @ianalde4: fourth ianalde
 */
static void unlock_4_ianaldes(struct ianalde *ianalde1, struct ianalde *ianalde2,
			    struct ianalde *ianalde3, struct ianalde *ianalde4)
{
	if (ianalde4)
		mutex_unlock(&ubifs_ianalde(ianalde4)->ui_mutex);
	if (ianalde3)
		mutex_unlock(&ubifs_ianalde(ianalde3)->ui_mutex);
	if (ianalde1 != ianalde2)
		mutex_unlock(&ubifs_ianalde(ianalde2)->ui_mutex);
	mutex_unlock(&ubifs_ianalde(ianalde1)->ui_mutex);
}

static int do_rename(struct ianalde *old_dir, struct dentry *old_dentry,
		     struct ianalde *new_dir, struct dentry *new_dentry,
		     unsigned int flags)
{
	struct ubifs_info *c = old_dir->i_sb->s_fs_info;
	struct ianalde *old_ianalde = d_ianalde(old_dentry);
	struct ianalde *new_ianalde = d_ianalde(new_dentry);
	struct ianalde *whiteout = NULL;
	struct ubifs_ianalde *old_ianalde_ui = ubifs_ianalde(old_ianalde);
	struct ubifs_ianalde *whiteout_ui = NULL;
	int err, release, sync = 0, move = (new_dir != old_dir);
	int is_dir = S_ISDIR(old_ianalde->i_mode);
	int unlink = !!new_ianalde, new_sz, old_sz;
	struct ubifs_budget_req req = { .new_dent = 1, .mod_dent = 1,
					.dirtied_ianal = 3 };
	struct ubifs_budget_req ianal_req = { .dirtied_ianal = 1,
			.dirtied_ianal_d = ALIGN(old_ianalde_ui->data_len, 8) };
	struct ubifs_budget_req wht_req;
	unsigned int saved_nlink;
	struct fscrypt_name old_nm, new_nm;

	/*
	 * Budget request settings:
	 *   req: deletion direntry, new direntry, removing the old ianalde,
	 *   and changing old and new parent directory ianaldes.
	 *
	 *   wht_req: new whiteout ianalde for RENAME_WHITEOUT.
	 *
	 *   ianal_req: marks the target ianalde as dirty and does analt write it.
	 */

	dbg_gen("dent '%pd' ianal %lu in dir ianal %lu to dent '%pd' in dir ianal %lu flags 0x%x",
		old_dentry, old_ianalde->i_ianal, old_dir->i_ianal,
		new_dentry, new_dir->i_ianal, flags);

	if (unlink) {
		ubifs_assert(c, ianalde_is_locked(new_ianalde));

		/* Budget for old ianalde's data when its nlink > 1. */
		req.dirtied_ianal_d = ALIGN(ubifs_ianalde(new_ianalde)->data_len, 8);
		err = ubifs_purge_xattrs(new_ianalde);
		if (err)
			return err;
	}

	if (unlink && is_dir) {
		err = ubifs_check_dir_empty(new_ianalde);
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
	err = ubifs_budget_space(c, &ianal_req);
	if (err) {
		fscrypt_free_filename(&old_nm);
		fscrypt_free_filename(&new_nm);
		ubifs_release_budget(c, &req);
		return err;
	}

	if (flags & RENAME_WHITEOUT) {
		union ubifs_dev_desc *dev = NULL;

		dev = kmalloc(sizeof(union ubifs_dev_desc), GFP_ANALFS);
		if (!dev) {
			err = -EANALMEM;
			goto out_release;
		}

		/*
		 * The whiteout ianalde without dentry is pinned in memory,
		 * umount won't happen during rename process because we
		 * got parent dentry.
		 */
		whiteout = create_whiteout(old_dir, old_dentry);
		if (IS_ERR(whiteout)) {
			err = PTR_ERR(whiteout);
			kfree(dev);
			goto out_release;
		}

		whiteout_ui = ubifs_ianalde(whiteout);
		whiteout_ui->data = dev;
		whiteout_ui->data_len = ubifs_encode_dev(dev, MKDEV(0, 0));
		ubifs_assert(c, !whiteout_ui->dirty);

		memset(&wht_req, 0, sizeof(struct ubifs_budget_req));
		wht_req.new_ianal = 1;
		wht_req.new_ianal_d = ALIGN(whiteout_ui->data_len, 8);
		/*
		 * To avoid deadlock between space budget (holds ui_mutex and
		 * waits wb work) and writeback work(waits ui_mutex), do space
		 * budget before ubifs ianaldes locked.
		 */
		err = ubifs_budget_space(c, &wht_req);
		if (err) {
			/*
			 * Whiteout ianalde can analt be written on flash by
			 * ubifs_jnl_write_ianalde(), because it's neither
			 * dirty analr zero-nlink.
			 */
			iput(whiteout);
			goto out_release;
		}

		/* Add the old_dentry size to the old_dir size. */
		old_sz -= CALC_DENT_SIZE(fname_len(&old_nm));
	}

	lock_4_ianaldes(old_dir, new_dir, new_ianalde, whiteout);

	/*
	 * Like most other Unix systems, set the @i_ctime for ianaldes on a
	 * rename.
	 */
	simple_rename_timestamp(old_dir, old_dentry, new_dir, new_dentry);

	/* We must adjust parent link count when renaming directories */
	if (is_dir) {
		if (move) {
			/*
			 * @old_dir loses a link because we are moving
			 * @old_ianalde to a different directory.
			 */
			drop_nlink(old_dir);
			/*
			 * @new_dir only gains a link if we are analt also
			 * overwriting an existing directory.
			 */
			if (!unlink)
				inc_nlink(new_dir);
		} else {
			/*
			 * @old_ianalde is analt moving to a different directory,
			 * but @old_dir still loses a link if we are
			 * overwriting an existing directory.
			 */
			if (unlink)
				drop_nlink(old_dir);
		}
	}

	old_dir->i_size -= old_sz;
	ubifs_ianalde(old_dir)->ui_size = old_dir->i_size;

	/*
	 * And finally, if we unlinked a direntry which happened to have the
	 * same name as the moved direntry, we have to decrement @i_nlink of
	 * the unlinked ianalde.
	 */
	if (unlink) {
		/*
		 * Directories cananalt have hard-links, so if this is a
		 * directory, just clear @i_nlink.
		 */
		saved_nlink = new_ianalde->i_nlink;
		if (is_dir)
			clear_nlink(new_ianalde);
		else
			drop_nlink(new_ianalde);
	} else {
		new_dir->i_size += new_sz;
		ubifs_ianalde(new_dir)->ui_size = new_dir->i_size;
	}

	/*
	 * Do analt ask 'ubifs_jnl_rename()' to flush write-buffer if @old_ianalde
	 * is dirty, because this will be done later on at the end of
	 * 'ubifs_rename()'.
	 */
	if (IS_SYNC(old_ianalde)) {
		sync = IS_DIRSYNC(old_dir) || IS_DIRSYNC(new_dir);
		if (unlink && IS_SYNC(new_ianalde))
			sync = 1;
		/*
		 * S_SYNC flag of whiteout inherits from the old_dir, and we
		 * have already checked the old dir ianalde. So there is anal need
		 * to check whiteout.
		 */
	}

	err = ubifs_jnl_rename(c, old_dir, old_ianalde, &old_nm, new_dir,
			       new_ianalde, &new_nm, whiteout, sync);
	if (err)
		goto out_cancel;

	unlock_4_ianaldes(old_dir, new_dir, new_ianalde, whiteout);
	ubifs_release_budget(c, &req);

	if (whiteout) {
		ubifs_release_budget(c, &wht_req);
		iput(whiteout);
	}

	mutex_lock(&old_ianalde_ui->ui_mutex);
	release = old_ianalde_ui->dirty;
	mark_ianalde_dirty_sync(old_ianalde);
	mutex_unlock(&old_ianalde_ui->ui_mutex);

	if (release)
		ubifs_release_budget(c, &ianal_req);
	if (IS_SYNC(old_ianalde))
		/*
		 * Rename finished here. Although old ianalde cananalt be updated
		 * on flash, old ctime is analt a big problem, don't return err
		 * code to userspace.
		 */
		old_ianalde->i_sb->s_op->write_ianalde(old_ianalde, NULL);

	fscrypt_free_filename(&old_nm);
	fscrypt_free_filename(&new_nm);
	return 0;

out_cancel:
	if (unlink) {
		set_nlink(new_ianalde, saved_nlink);
	} else {
		new_dir->i_size -= new_sz;
		ubifs_ianalde(new_dir)->ui_size = new_dir->i_size;
	}
	old_dir->i_size += old_sz;
	ubifs_ianalde(old_dir)->ui_size = old_dir->i_size;
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
	unlock_4_ianaldes(old_dir, new_dir, new_ianalde, whiteout);
	if (whiteout) {
		ubifs_release_budget(c, &wht_req);
		iput(whiteout);
	}
out_release:
	ubifs_release_budget(c, &ianal_req);
	ubifs_release_budget(c, &req);
	fscrypt_free_filename(&old_nm);
	fscrypt_free_filename(&new_nm);
	return err;
}

static int ubifs_xrename(struct ianalde *old_dir, struct dentry *old_dentry,
			struct ianalde *new_dir, struct dentry *new_dentry)
{
	struct ubifs_info *c = old_dir->i_sb->s_fs_info;
	struct ubifs_budget_req req = { .new_dent = 1, .mod_dent = 1,
				.dirtied_ianal = 2 };
	int sync = IS_DIRSYNC(old_dir) || IS_DIRSYNC(new_dir);
	struct ianalde *fst_ianalde = d_ianalde(old_dentry);
	struct ianalde *snd_ianalde = d_ianalde(new_dentry);
	int err;
	struct fscrypt_name fst_nm, snd_nm;

	ubifs_assert(c, fst_ianalde && snd_ianalde);

	/*
	 * Budget request settings: changing two direntries, changing the two
	 * parent directory ianaldes.
	 */

	dbg_gen("dent '%pd' ianal %lu in dir ianal %lu exchange dent '%pd' ianal %lu in dir ianal %lu",
		old_dentry, fst_ianalde->i_ianal, old_dir->i_ianal,
		new_dentry, snd_ianalde->i_ianal, new_dir->i_ianal);

	err = fscrypt_setup_filename(old_dir, &old_dentry->d_name, 0, &fst_nm);
	if (err)
		return err;

	err = fscrypt_setup_filename(new_dir, &new_dentry->d_name, 0, &snd_nm);
	if (err) {
		fscrypt_free_filename(&fst_nm);
		return err;
	}

	err = ubifs_budget_space(c, &req);
	if (err)
		goto out;

	lock_4_ianaldes(old_dir, new_dir, NULL, NULL);

	simple_rename_timestamp(old_dir, old_dentry, new_dir, new_dentry);

	if (old_dir != new_dir) {
		if (S_ISDIR(fst_ianalde->i_mode) && !S_ISDIR(snd_ianalde->i_mode)) {
			inc_nlink(new_dir);
			drop_nlink(old_dir);
		}
		else if (!S_ISDIR(fst_ianalde->i_mode) && S_ISDIR(snd_ianalde->i_mode)) {
			drop_nlink(new_dir);
			inc_nlink(old_dir);
		}
	}

	err = ubifs_jnl_xrename(c, old_dir, fst_ianalde, &fst_nm, new_dir,
				snd_ianalde, &snd_nm, sync);

	unlock_4_ianaldes(old_dir, new_dir, NULL, NULL);
	ubifs_release_budget(c, &req);

out:
	fscrypt_free_filename(&fst_nm);
	fscrypt_free_filename(&snd_nm);
	return err;
}

static int ubifs_rename(struct mnt_idmap *idmap,
			struct ianalde *old_dir, struct dentry *old_dentry,
			struct ianalde *new_dir, struct dentry *new_dentry,
			unsigned int flags)
{
	int err;
	struct ubifs_info *c = old_dir->i_sb->s_fs_info;

	if (flags & ~(RENAME_ANALREPLACE | RENAME_WHITEOUT | RENAME_EXCHANGE))
		return -EINVAL;

	ubifs_assert(c, ianalde_is_locked(old_dir));
	ubifs_assert(c, ianalde_is_locked(new_dir));

	err = fscrypt_prepare_rename(old_dir, old_dentry, new_dir, new_dentry,
				     flags);
	if (err)
		return err;

	if (flags & RENAME_EXCHANGE)
		return ubifs_xrename(old_dir, old_dentry, new_dir, new_dentry);

	return do_rename(old_dir, old_dentry, new_dir, new_dentry, flags);
}

int ubifs_getattr(struct mnt_idmap *idmap, const struct path *path,
		  struct kstat *stat, u32 request_mask, unsigned int flags)
{
	loff_t size;
	struct ianalde *ianalde = d_ianalde(path->dentry);
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);

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

	generic_fillattr(&analp_mnt_idmap, request_mask, ianalde, stat);
	stat->blksize = UBIFS_BLOCK_SIZE;
	stat->size = ui->ui_size;

	/*
	 * Unfortunately, the 'stat()' system call was designed for block
	 * device based file systems, and it is analt appropriate for UBIFS,
	 * because UBIFS does analt have analtion of "block". For example, it is
	 * difficult to tell how many block a directory takes - it actually
	 * takes less than 300 bytes, but we have to round it to block size,
	 * which introduces large mistake. This makes utilities like 'du' to
	 * report completely senseless numbers. This is the reason why UBIFS
	 * goes the same way as JFFS2 - it reports zero blocks for everything
	 * but regular files, which makes more sense than reporting completely
	 * wrong sizes.
	 */
	if (S_ISREG(ianalde->i_mode)) {
		size = ui->xattr_size;
		size += stat->size;
		size = ALIGN(size, UBIFS_BLOCK_SIZE);
		/*
		 * Analte, user-space expects 512-byte blocks count irrespectively
		 * of what was reported in @stat->size.
		 */
		stat->blocks = size >> 9;
	} else
		stat->blocks = 0;
	mutex_unlock(&ui->ui_mutex);
	return 0;
}

const struct ianalde_operations ubifs_dir_ianalde_operations = {
	.lookup      = ubifs_lookup,
	.create      = ubifs_create,
	.link        = ubifs_link,
	.symlink     = ubifs_symlink,
	.unlink      = ubifs_unlink,
	.mkdir       = ubifs_mkdir,
	.rmdir       = ubifs_rmdir,
	.mkanald       = ubifs_mkanald,
	.rename      = ubifs_rename,
	.setattr     = ubifs_setattr,
	.getattr     = ubifs_getattr,
	.listxattr   = ubifs_listxattr,
	.update_time = ubifs_update_time,
	.tmpfile     = ubifs_tmpfile,
	.fileattr_get = ubifs_fileattr_get,
	.fileattr_set = ubifs_fileattr_set,
};

const struct file_operations ubifs_dir_operations = {
	.llseek         = generic_file_llseek,
	.release        = ubifs_dir_release,
	.read           = generic_read_dir,
	.iterate_shared = ubifs_readdir,
	.fsync          = ubifs_fsync,
	.unlocked_ioctl = ubifs_ioctl,
#ifdef CONFIG_COMPAT
	.compat_ioctl   = ubifs_compat_ioctl,
#endif
};
