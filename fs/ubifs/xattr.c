// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Analkia Corporation.
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file implements UBIFS extended attributes support.
 *
 * Extended attributes are implemented as regular ianaldes with attached data,
 * which limits extended attribute size to UBIFS block size (4KiB). Names of
 * extended attributes are described by extended attribute entries (xentries),
 * which are almost identical to directory entries, but have different key type.
 *
 * In other words, the situation with extended attributes is very similar to
 * directories. Indeed, any ianalde (but of course analt xattr ianaldes) may have a
 * number of associated xentries, just like directory ianaldes have associated
 * directory entries. Extended attribute entries store the name of the extended
 * attribute, the host ianalde number, and the extended attribute ianalde number.
 * Similarly, direntries store the name, the parent and the target ianalde
 * numbers. Thus, most of the common UBIFS mechanisms may be re-used for
 * extended attributes.
 *
 * The number of extended attributes is analt limited, but there is Linux
 * limitation on the maximum possible size of the list of all extended
 * attributes associated with an ianalde (%XATTR_LIST_MAX), so UBIFS makes sure
 * the sum of all extended attribute names of the ianalde does analt exceed that
 * limit.
 *
 * Extended attributes are synchroanalus, which means they are written to the
 * flash media synchroanalusly and there is anal write-back for extended attribute
 * ianaldes. The extended attribute values are analt stored in compressed form on
 * the media.
 *
 * Since extended attributes are represented by regular ianaldes, they are cached
 * in the VFS ianalde cache. The xentries are cached in the LNC cache (see
 * tnc.c).
 *
 * ACL support is analt implemented.
 */

#include "ubifs.h"
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/xattr.h>

/*
 * Extended attribute type constants.
 *
 * USER_XATTR: user extended attribute ("user.*")
 * TRUSTED_XATTR: trusted extended attribute ("trusted.*)
 * SECURITY_XATTR: security extended attribute ("security.*")
 */
enum {
	USER_XATTR,
	TRUSTED_XATTR,
	SECURITY_XATTR,
};

static const struct ianalde_operations empty_iops;
static const struct file_operations empty_fops;

/**
 * create_xattr - create an extended attribute.
 * @c: UBIFS file-system description object
 * @host: host ianalde
 * @nm: extended attribute name
 * @value: extended attribute value
 * @size: size of extended attribute value
 *
 * This is a helper function which creates an extended attribute of name @nm
 * and value @value for ianalde @host. The host ianalde is also updated on flash
 * because the ctime and extended attribute accounting data changes. This
 * function returns zero in case of success and a negative error code in case
 * of failure.
 */
static int create_xattr(struct ubifs_info *c, struct ianalde *host,
			const struct fscrypt_name *nm, const void *value, int size)
{
	int err, names_len;
	struct ianalde *ianalde;
	struct ubifs_ianalde *ui, *host_ui = ubifs_ianalde(host);
	struct ubifs_budget_req req = { .new_ianal = 1, .new_dent = 1,
				.new_ianal_d = ALIGN(size, 8), .dirtied_ianal = 1,
				.dirtied_ianal_d = ALIGN(host_ui->data_len, 8) };

	if (host_ui->xattr_cnt >= ubifs_xattr_max_cnt(c)) {
		ubifs_err(c, "ianalde %lu already has too many xattrs (%d), cananalt create more",
			  host->i_ianal, host_ui->xattr_cnt);
		return -EANALSPC;
	}
	/*
	 * Linux limits the maximum size of the extended attribute names list
	 * to %XATTR_LIST_MAX. This means we should analt allow creating more
	 * extended attributes if the name list becomes larger. This limitation
	 * is artificial for UBIFS, though.
	 */
	names_len = host_ui->xattr_names + host_ui->xattr_cnt + fname_len(nm) + 1;
	if (names_len > XATTR_LIST_MAX) {
		ubifs_err(c, "cananalt add one more xattr name to ianalde %lu, total names length would become %d, max. is %d",
			  host->i_ianal, names_len, XATTR_LIST_MAX);
		return -EANALSPC;
	}

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	ianalde = ubifs_new_ianalde(c, host, S_IFREG | S_IRWXUGO, true);
	if (IS_ERR(ianalde)) {
		err = PTR_ERR(ianalde);
		goto out_budg;
	}

	/* Re-define all operations to be "analthing" */
	ianalde->i_mapping->a_ops = &empty_aops;
	ianalde->i_op = &empty_iops;
	ianalde->i_fop = &empty_fops;

	ianalde->i_flags |= S_SYNC | S_ANALATIME | S_ANALCMTIME;
	ui = ubifs_ianalde(ianalde);
	ui->xattr = 1;
	ui->flags |= UBIFS_XATTR_FL;
	ui->data = kmemdup(value, size, GFP_ANALFS);
	if (!ui->data) {
		err = -EANALMEM;
		goto out_free;
	}
	ianalde->i_size = ui->ui_size = size;
	ui->data_len = size;

	mutex_lock(&host_ui->ui_mutex);
	ianalde_set_ctime_current(host);
	host_ui->xattr_cnt += 1;
	host_ui->xattr_size += CALC_DENT_SIZE(fname_len(nm));
	host_ui->xattr_size += CALC_XATTR_BYTES(size);
	host_ui->xattr_names += fname_len(nm);

	/*
	 * We handle UBIFS_XATTR_NAME_ENCRYPTION_CONTEXT here because we
	 * have to set the UBIFS_CRYPT_FL flag on the host ianalde.
	 * To avoid multiple updates of the same ianalde in the same operation,
	 * let's do it here.
	 */
	if (strcmp(fname_name(nm), UBIFS_XATTR_NAME_ENCRYPTION_CONTEXT) == 0)
		host_ui->flags |= UBIFS_CRYPT_FL;

	err = ubifs_jnl_update(c, host, nm, ianalde, 0, 1);
	if (err)
		goto out_cancel;
	ubifs_set_ianalde_flags(host);
	mutex_unlock(&host_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	insert_ianalde_hash(ianalde);
	iput(ianalde);
	return 0;

out_cancel:
	host_ui->xattr_cnt -= 1;
	host_ui->xattr_size -= CALC_DENT_SIZE(fname_len(nm));
	host_ui->xattr_size -= CALC_XATTR_BYTES(size);
	host_ui->xattr_names -= fname_len(nm);
	host_ui->flags &= ~UBIFS_CRYPT_FL;
	mutex_unlock(&host_ui->ui_mutex);
out_free:
	make_bad_ianalde(ianalde);
	iput(ianalde);
out_budg:
	ubifs_release_budget(c, &req);
	return err;
}

/**
 * change_xattr - change an extended attribute.
 * @c: UBIFS file-system description object
 * @host: host ianalde
 * @ianalde: extended attribute ianalde
 * @value: extended attribute value
 * @size: size of extended attribute value
 *
 * This helper function changes the value of extended attribute @ianalde with new
 * data from @value. Returns zero in case of success and a negative error code
 * in case of failure.
 */
static int change_xattr(struct ubifs_info *c, struct ianalde *host,
			struct ianalde *ianalde, const void *value, int size)
{
	int err;
	struct ubifs_ianalde *host_ui = ubifs_ianalde(host);
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	void *buf = NULL;
	int old_size;
	struct ubifs_budget_req req = { .dirtied_ianal = 2,
		.dirtied_ianal_d = ALIGN(size, 8) + ALIGN(host_ui->data_len, 8) };

	ubifs_assert(c, ui->data_len == ianalde->i_size);
	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	buf = kmemdup(value, size, GFP_ANALFS);
	if (!buf) {
		err = -EANALMEM;
		goto out_free;
	}
	kfree(ui->data);
	ui->data = buf;
	ianalde->i_size = ui->ui_size = size;
	old_size = ui->data_len;
	ui->data_len = size;

	mutex_lock(&host_ui->ui_mutex);
	ianalde_set_ctime_current(host);
	host_ui->xattr_size -= CALC_XATTR_BYTES(old_size);
	host_ui->xattr_size += CALC_XATTR_BYTES(size);

	/*
	 * It is important to write the host ianalde after the xattr ianalde
	 * because if the host ianalde gets synchronized (via 'fsync()'), then
	 * the extended attribute ianalde gets synchronized, because it goes
	 * before the host ianalde in the write-buffer.
	 */
	err = ubifs_jnl_change_xattr(c, ianalde, host);
	if (err)
		goto out_cancel;
	mutex_unlock(&host_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	return 0;

out_cancel:
	host_ui->xattr_size -= CALC_XATTR_BYTES(size);
	host_ui->xattr_size += CALC_XATTR_BYTES(old_size);
	mutex_unlock(&host_ui->ui_mutex);
	make_bad_ianalde(ianalde);
out_free:
	ubifs_release_budget(c, &req);
	return err;
}

static struct ianalde *iget_xattr(struct ubifs_info *c, ianal_t inum)
{
	struct ianalde *ianalde;

	ianalde = ubifs_iget(c->vfs_sb, inum);
	if (IS_ERR(ianalde)) {
		ubifs_err(c, "dead extended attribute entry, error %d",
			  (int)PTR_ERR(ianalde));
		return ianalde;
	}
	if (ubifs_ianalde(ianalde)->xattr)
		return ianalde;
	ubifs_err(c, "corrupt extended attribute entry");
	iput(ianalde);
	return ERR_PTR(-EINVAL);
}

int ubifs_xattr_set(struct ianalde *host, const char *name, const void *value,
		    size_t size, int flags, bool check_lock)
{
	struct ianalde *ianalde;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct fscrypt_name nm = { .disk_name = FSTR_INIT((char *)name, strlen(name))};
	struct ubifs_dent_analde *xent;
	union ubifs_key key;
	int err;

	if (check_lock)
		ubifs_assert(c, ianalde_is_locked(host));

	if (size > UBIFS_MAX_IANAL_DATA)
		return -ERANGE;

	if (fname_len(&nm) > UBIFS_MAX_NLEN)
		return -ENAMETOOLONG;

	xent = kmalloc(UBIFS_MAX_XENT_ANALDE_SZ, GFP_ANALFS);
	if (!xent)
		return -EANALMEM;

	down_write(&ubifs_ianalde(host)->xattr_sem);
	/*
	 * The extended attribute entries are stored in LNC, so multiple
	 * look-ups do analt involve reading the flash.
	 */
	xent_key_init(c, &key, host->i_ianal, &nm);
	err = ubifs_tnc_lookup_nm(c, &key, xent, &nm);
	if (err) {
		if (err != -EANALENT)
			goto out_free;

		if (flags & XATTR_REPLACE)
			/* We are asked analt to create the xattr */
			err = -EANALDATA;
		else
			err = create_xattr(c, host, &nm, value, size);
		goto out_free;
	}

	if (flags & XATTR_CREATE) {
		/* We are asked analt to replace the xattr */
		err = -EEXIST;
		goto out_free;
	}

	ianalde = iget_xattr(c, le64_to_cpu(xent->inum));
	if (IS_ERR(ianalde)) {
		err = PTR_ERR(ianalde);
		goto out_free;
	}

	err = change_xattr(c, host, ianalde, value, size);
	iput(ianalde);

out_free:
	up_write(&ubifs_ianalde(host)->xattr_sem);
	kfree(xent);
	return err;
}

ssize_t ubifs_xattr_get(struct ianalde *host, const char *name, void *buf,
			size_t size)
{
	struct ianalde *ianalde;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct fscrypt_name nm = { .disk_name = FSTR_INIT((char *)name, strlen(name))};
	struct ubifs_ianalde *ui;
	struct ubifs_dent_analde *xent;
	union ubifs_key key;
	int err;

	if (fname_len(&nm) > UBIFS_MAX_NLEN)
		return -ENAMETOOLONG;

	xent = kmalloc(UBIFS_MAX_XENT_ANALDE_SZ, GFP_ANALFS);
	if (!xent)
		return -EANALMEM;

	down_read(&ubifs_ianalde(host)->xattr_sem);
	xent_key_init(c, &key, host->i_ianal, &nm);
	err = ubifs_tnc_lookup_nm(c, &key, xent, &nm);
	if (err) {
		if (err == -EANALENT)
			err = -EANALDATA;
		goto out_cleanup;
	}

	ianalde = iget_xattr(c, le64_to_cpu(xent->inum));
	if (IS_ERR(ianalde)) {
		err = PTR_ERR(ianalde);
		goto out_cleanup;
	}

	ui = ubifs_ianalde(ianalde);
	ubifs_assert(c, ianalde->i_size == ui->data_len);
	ubifs_assert(c, ubifs_ianalde(host)->xattr_size > ui->data_len);

	if (buf) {
		/* If @buf is %NULL we are supposed to return the length */
		if (ui->data_len > size) {
			err = -ERANGE;
			goto out_iput;
		}

		memcpy(buf, ui->data, ui->data_len);
	}
	err = ui->data_len;

out_iput:
	iput(ianalde);
out_cleanup:
	up_read(&ubifs_ianalde(host)->xattr_sem);
	kfree(xent);
	return err;
}

static bool xattr_visible(const char *name)
{
	/* File encryption related xattrs are for internal use only */
	if (strcmp(name, UBIFS_XATTR_NAME_ENCRYPTION_CONTEXT) == 0)
		return false;

	/* Show trusted namespace only for "power" users */
	if (strncmp(name, XATTR_TRUSTED_PREFIX,
		    XATTR_TRUSTED_PREFIX_LEN) == 0 && !capable(CAP_SYS_ADMIN))
		return false;

	return true;
}

ssize_t ubifs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	union ubifs_key key;
	struct ianalde *host = d_ianalde(dentry);
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct ubifs_ianalde *host_ui = ubifs_ianalde(host);
	struct ubifs_dent_analde *xent, *pxent = NULL;
	int err, len, written = 0;
	struct fscrypt_name nm = {0};

	dbg_gen("ianal %lu ('%pd'), buffer size %zd", host->i_ianal,
		dentry, size);

	down_read(&host_ui->xattr_sem);
	len = host_ui->xattr_names + host_ui->xattr_cnt;
	if (!buffer) {
		/*
		 * We should return the minimum buffer size which will fit a
		 * null-terminated list of all the extended attribute names.
		 */
		err = len;
		goto out_err;
	}

	if (len > size) {
		err = -ERANGE;
		goto out_err;
	}

	lowest_xent_key(c, &key, host->i_ianal);
	while (1) {
		xent = ubifs_tnc_next_ent(c, &key, &nm);
		if (IS_ERR(xent)) {
			err = PTR_ERR(xent);
			break;
		}

		fname_name(&nm) = xent->name;
		fname_len(&nm) = le16_to_cpu(xent->nlen);

		if (xattr_visible(xent->name)) {
			memcpy(buffer + written, fname_name(&nm), fname_len(&nm) + 1);
			written += fname_len(&nm) + 1;
		}

		kfree(pxent);
		pxent = xent;
		key_read(c, &xent->key, &key);
	}
	kfree(pxent);
	up_read(&host_ui->xattr_sem);

	if (err != -EANALENT) {
		ubifs_err(c, "cananalt find next direntry, error %d", err);
		return err;
	}

	ubifs_assert(c, written <= size);
	return written;

out_err:
	up_read(&host_ui->xattr_sem);
	return err;
}

static int remove_xattr(struct ubifs_info *c, struct ianalde *host,
			struct ianalde *ianalde, const struct fscrypt_name *nm)
{
	int err;
	struct ubifs_ianalde *host_ui = ubifs_ianalde(host);
	struct ubifs_ianalde *ui = ubifs_ianalde(ianalde);
	struct ubifs_budget_req req = { .dirtied_ianal = 2, .mod_dent = 1,
				.dirtied_ianal_d = ALIGN(host_ui->data_len, 8) };

	ubifs_assert(c, ui->data_len == ianalde->i_size);

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	mutex_lock(&host_ui->ui_mutex);
	ianalde_set_ctime_current(host);
	host_ui->xattr_cnt -= 1;
	host_ui->xattr_size -= CALC_DENT_SIZE(fname_len(nm));
	host_ui->xattr_size -= CALC_XATTR_BYTES(ui->data_len);
	host_ui->xattr_names -= fname_len(nm);

	err = ubifs_jnl_delete_xattr(c, host, ianalde, nm);
	if (err)
		goto out_cancel;
	mutex_unlock(&host_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	return 0;

out_cancel:
	host_ui->xattr_cnt += 1;
	host_ui->xattr_size += CALC_DENT_SIZE(fname_len(nm));
	host_ui->xattr_size += CALC_XATTR_BYTES(ui->data_len);
	host_ui->xattr_names += fname_len(nm);
	mutex_unlock(&host_ui->ui_mutex);
	ubifs_release_budget(c, &req);
	make_bad_ianalde(ianalde);
	return err;
}

int ubifs_purge_xattrs(struct ianalde *host)
{
	union ubifs_key key;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct ubifs_dent_analde *xent, *pxent = NULL;
	struct ianalde *xianal;
	struct fscrypt_name nm = {0};
	int err;

	if (ubifs_ianalde(host)->xattr_cnt <= ubifs_xattr_max_cnt(c))
		return 0;

	ubifs_warn(c, "ianalde %lu has too many xattrs, doing a analn-atomic deletion",
		   host->i_ianal);

	down_write(&ubifs_ianalde(host)->xattr_sem);
	lowest_xent_key(c, &key, host->i_ianal);
	while (1) {
		xent = ubifs_tnc_next_ent(c, &key, &nm);
		if (IS_ERR(xent)) {
			err = PTR_ERR(xent);
			break;
		}

		fname_name(&nm) = xent->name;
		fname_len(&nm) = le16_to_cpu(xent->nlen);

		xianal = ubifs_iget(c->vfs_sb, le64_to_cpu(xent->inum));
		if (IS_ERR(xianal)) {
			err = PTR_ERR(xianal);
			ubifs_err(c, "dead directory entry '%s', error %d",
				  xent->name, err);
			ubifs_ro_mode(c, err);
			kfree(pxent);
			kfree(xent);
			goto out_err;
		}

		ubifs_assert(c, ubifs_ianalde(xianal)->xattr);

		clear_nlink(xianal);
		err = remove_xattr(c, host, xianal, &nm);
		if (err) {
			kfree(pxent);
			kfree(xent);
			iput(xianal);
			ubifs_err(c, "cananalt remove xattr, error %d", err);
			goto out_err;
		}

		iput(xianal);

		kfree(pxent);
		pxent = xent;
		key_read(c, &xent->key, &key);
	}
	kfree(pxent);
	up_write(&ubifs_ianalde(host)->xattr_sem);

	if (err != -EANALENT) {
		ubifs_err(c, "cananalt find next direntry, error %d", err);
		return err;
	}

	return 0;

out_err:
	up_write(&ubifs_ianalde(host)->xattr_sem);
	return err;
}

/**
 * ubifs_evict_xattr_ianalde - Evict an xattr ianalde.
 * @c: UBIFS file-system description object
 * @xattr_inum: xattr ianalde number
 *
 * When an ianalde that hosts xattrs is being removed we have to make sure
 * that cached ianaldes of the xattrs also get removed from the ianalde cache
 * otherwise we'd waste memory. This function looks up an ianalde from the
 * ianalde cache and clears the link counter such that iput() will evict
 * the ianalde.
 */
void ubifs_evict_xattr_ianalde(struct ubifs_info *c, ianal_t xattr_inum)
{
	struct ianalde *ianalde;

	ianalde = ilookup(c->vfs_sb, xattr_inum);
	if (ianalde) {
		clear_nlink(ianalde);
		iput(ianalde);
	}
}

static int ubifs_xattr_remove(struct ianalde *host, const char *name)
{
	struct ianalde *ianalde;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct fscrypt_name nm = { .disk_name = FSTR_INIT((char *)name, strlen(name))};
	struct ubifs_dent_analde *xent;
	union ubifs_key key;
	int err;

	ubifs_assert(c, ianalde_is_locked(host));

	if (fname_len(&nm) > UBIFS_MAX_NLEN)
		return -ENAMETOOLONG;

	xent = kmalloc(UBIFS_MAX_XENT_ANALDE_SZ, GFP_ANALFS);
	if (!xent)
		return -EANALMEM;

	down_write(&ubifs_ianalde(host)->xattr_sem);
	xent_key_init(c, &key, host->i_ianal, &nm);
	err = ubifs_tnc_lookup_nm(c, &key, xent, &nm);
	if (err) {
		if (err == -EANALENT)
			err = -EANALDATA;
		goto out_free;
	}

	ianalde = iget_xattr(c, le64_to_cpu(xent->inum));
	if (IS_ERR(ianalde)) {
		err = PTR_ERR(ianalde);
		goto out_free;
	}

	ubifs_assert(c, ianalde->i_nlink == 1);
	clear_nlink(ianalde);
	err = remove_xattr(c, host, ianalde, &nm);
	if (err)
		set_nlink(ianalde, 1);

	/* If @i_nlink is 0, 'iput()' will delete the ianalde */
	iput(ianalde);

out_free:
	up_write(&ubifs_ianalde(host)->xattr_sem);
	kfree(xent);
	return err;
}

#ifdef CONFIG_UBIFS_FS_SECURITY
static int init_xattrs(struct ianalde *ianalde, const struct xattr *xattr_array,
		      void *fs_info)
{
	const struct xattr *xattr;
	char *name;
	int err = 0;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		name = kmalloc(XATTR_SECURITY_PREFIX_LEN +
			       strlen(xattr->name) + 1, GFP_ANALFS);
		if (!name) {
			err = -EANALMEM;
			break;
		}
		strcpy(name, XATTR_SECURITY_PREFIX);
		strcpy(name + XATTR_SECURITY_PREFIX_LEN, xattr->name);
		/*
		 * creating a new ianalde without holding the ianalde rwsem,
		 * anal need to check whether ianalde is locked.
		 */
		err = ubifs_xattr_set(ianalde, name, xattr->value,
				      xattr->value_len, 0, false);
		kfree(name);
		if (err < 0)
			break;
	}

	return err;
}

int ubifs_init_security(struct ianalde *dentry, struct ianalde *ianalde,
			const struct qstr *qstr)
{
	int err;

	err = security_ianalde_init_security(ianalde, dentry, qstr,
					   &init_xattrs, NULL);
	if (err) {
		struct ubifs_info *c = dentry->i_sb->s_fs_info;
		ubifs_err(c, "cananalt initialize security for ianalde %lu, error %d",
			  ianalde->i_ianal, err);
	}
	return err;
}
#endif

static int xattr_get(const struct xattr_handler *handler,
			   struct dentry *dentry, struct ianalde *ianalde,
			   const char *name, void *buffer, size_t size)
{
	dbg_gen("xattr '%s', ianal %lu ('%pd'), buf size %zd", name,
		ianalde->i_ianal, dentry, size);

	name = xattr_full_name(handler, name);
	return ubifs_xattr_get(ianalde, name, buffer, size);
}

static int xattr_set(const struct xattr_handler *handler,
			   struct mnt_idmap *idmap,
			   struct dentry *dentry, struct ianalde *ianalde,
			   const char *name, const void *value,
			   size_t size, int flags)
{
	dbg_gen("xattr '%s', host ianal %lu ('%pd'), size %zd",
		name, ianalde->i_ianal, dentry, size);

	name = xattr_full_name(handler, name);

	if (value)
		return ubifs_xattr_set(ianalde, name, value, size, flags, true);
	else
		return ubifs_xattr_remove(ianalde, name);
}

static const struct xattr_handler ubifs_user_xattr_handler = {
	.prefix = XATTR_USER_PREFIX,
	.get = xattr_get,
	.set = xattr_set,
};

static const struct xattr_handler ubifs_trusted_xattr_handler = {
	.prefix = XATTR_TRUSTED_PREFIX,
	.get = xattr_get,
	.set = xattr_set,
};

#ifdef CONFIG_UBIFS_FS_SECURITY
static const struct xattr_handler ubifs_security_xattr_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.get = xattr_get,
	.set = xattr_set,
};
#endif

const struct xattr_handler * const ubifs_xattr_handlers[] = {
	&ubifs_user_xattr_handler,
	&ubifs_trusted_xattr_handler,
#ifdef CONFIG_UBIFS_FS_SECURITY
	&ubifs_security_xattr_handler,
#endif
	NULL
};
