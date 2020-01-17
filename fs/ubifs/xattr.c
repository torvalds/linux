// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * Authors: Artem Bityutskiy (Битюцкий Артём)
 *          Adrian Hunter
 */

/*
 * This file implements UBIFS extended attributes support.
 *
 * Extended attributes are implemented as regular iyesdes with attached data,
 * which limits extended attribute size to UBIFS block size (4KiB). Names of
 * extended attributes are described by extended attribute entries (xentries),
 * which are almost identical to directory entries, but have different key type.
 *
 * In other words, the situation with extended attributes is very similar to
 * directories. Indeed, any iyesde (but of course yest xattr iyesdes) may have a
 * number of associated xentries, just like directory iyesdes have associated
 * directory entries. Extended attribute entries store the name of the extended
 * attribute, the host iyesde number, and the extended attribute iyesde number.
 * Similarly, direntries store the name, the parent and the target iyesde
 * numbers. Thus, most of the common UBIFS mechanisms may be re-used for
 * extended attributes.
 *
 * The number of extended attributes is yest limited, but there is Linux
 * limitation on the maximum possible size of the list of all extended
 * attributes associated with an iyesde (%XATTR_LIST_MAX), so UBIFS makes sure
 * the sum of all extended attribute names of the iyesde does yest exceed that
 * limit.
 *
 * Extended attributes are synchroyesus, which means they are written to the
 * flash media synchroyesusly and there is yes write-back for extended attribute
 * iyesdes. The extended attribute values are yest stored in compressed form on
 * the media.
 *
 * Since extended attributes are represented by regular iyesdes, they are cached
 * in the VFS iyesde cache. The xentries are cached in the LNC cache (see
 * tnc.c).
 *
 * ACL support is yest implemented.
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

static const struct iyesde_operations empty_iops;
static const struct file_operations empty_fops;

/**
 * create_xattr - create an extended attribute.
 * @c: UBIFS file-system description object
 * @host: host iyesde
 * @nm: extended attribute name
 * @value: extended attribute value
 * @size: size of extended attribute value
 *
 * This is a helper function which creates an extended attribute of name @nm
 * and value @value for iyesde @host. The host iyesde is also updated on flash
 * because the ctime and extended attribute accounting data changes. This
 * function returns zero in case of success and a negative error code in case
 * of failure.
 */
static int create_xattr(struct ubifs_info *c, struct iyesde *host,
			const struct fscrypt_name *nm, const void *value, int size)
{
	int err, names_len;
	struct iyesde *iyesde;
	struct ubifs_iyesde *ui, *host_ui = ubifs_iyesde(host);
	struct ubifs_budget_req req = { .new_iyes = 1, .new_dent = 1,
				.new_iyes_d = ALIGN(size, 8), .dirtied_iyes = 1,
				.dirtied_iyes_d = ALIGN(host_ui->data_len, 8) };

	if (host_ui->xattr_cnt >= ubifs_xattr_max_cnt(c)) {
		ubifs_err(c, "iyesde %lu already has too many xattrs (%d), canyest create more",
			  host->i_iyes, host_ui->xattr_cnt);
		return -ENOSPC;
	}
	/*
	 * Linux limits the maximum size of the extended attribute names list
	 * to %XATTR_LIST_MAX. This means we should yest allow creating more
	 * extended attributes if the name list becomes larger. This limitation
	 * is artificial for UBIFS, though.
	 */
	names_len = host_ui->xattr_names + host_ui->xattr_cnt + fname_len(nm) + 1;
	if (names_len > XATTR_LIST_MAX) {
		ubifs_err(c, "canyest add one more xattr name to iyesde %lu, total names length would become %d, max. is %d",
			  host->i_iyes, names_len, XATTR_LIST_MAX);
		return -ENOSPC;
	}

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	iyesde = ubifs_new_iyesde(c, host, S_IFREG | S_IRWXUGO);
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		goto out_budg;
	}

	/* Re-define all operations to be "yesthing" */
	iyesde->i_mapping->a_ops = &empty_aops;
	iyesde->i_op = &empty_iops;
	iyesde->i_fop = &empty_fops;

	iyesde->i_flags |= S_SYNC | S_NOATIME | S_NOCMTIME;
	ui = ubifs_iyesde(iyesde);
	ui->xattr = 1;
	ui->flags |= UBIFS_XATTR_FL;
	ui->data = kmemdup(value, size, GFP_NOFS);
	if (!ui->data) {
		err = -ENOMEM;
		goto out_free;
	}
	iyesde->i_size = ui->ui_size = size;
	ui->data_len = size;

	mutex_lock(&host_ui->ui_mutex);
	host->i_ctime = current_time(host);
	host_ui->xattr_cnt += 1;
	host_ui->xattr_size += CALC_DENT_SIZE(fname_len(nm));
	host_ui->xattr_size += CALC_XATTR_BYTES(size);
	host_ui->xattr_names += fname_len(nm);

	/*
	 * We handle UBIFS_XATTR_NAME_ENCRYPTION_CONTEXT here because we
	 * have to set the UBIFS_CRYPT_FL flag on the host iyesde.
	 * To avoid multiple updates of the same iyesde in the same operation,
	 * let's do it here.
	 */
	if (strcmp(fname_name(nm), UBIFS_XATTR_NAME_ENCRYPTION_CONTEXT) == 0)
		host_ui->flags |= UBIFS_CRYPT_FL;

	err = ubifs_jnl_update(c, host, nm, iyesde, 0, 1);
	if (err)
		goto out_cancel;
	ubifs_set_iyesde_flags(host);
	mutex_unlock(&host_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	insert_iyesde_hash(iyesde);
	iput(iyesde);
	return 0;

out_cancel:
	host_ui->xattr_cnt -= 1;
	host_ui->xattr_size -= CALC_DENT_SIZE(fname_len(nm));
	host_ui->xattr_size -= CALC_XATTR_BYTES(size);
	host_ui->xattr_names -= fname_len(nm);
	host_ui->flags &= ~UBIFS_CRYPT_FL;
	mutex_unlock(&host_ui->ui_mutex);
out_free:
	make_bad_iyesde(iyesde);
	iput(iyesde);
out_budg:
	ubifs_release_budget(c, &req);
	return err;
}

/**
 * change_xattr - change an extended attribute.
 * @c: UBIFS file-system description object
 * @host: host iyesde
 * @iyesde: extended attribute iyesde
 * @value: extended attribute value
 * @size: size of extended attribute value
 *
 * This helper function changes the value of extended attribute @iyesde with new
 * data from @value. Returns zero in case of success and a negative error code
 * in case of failure.
 */
static int change_xattr(struct ubifs_info *c, struct iyesde *host,
			struct iyesde *iyesde, const void *value, int size)
{
	int err;
	struct ubifs_iyesde *host_ui = ubifs_iyesde(host);
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);
	void *buf = NULL;
	int old_size;
	struct ubifs_budget_req req = { .dirtied_iyes = 2,
		.dirtied_iyes_d = ALIGN(size, 8) + ALIGN(host_ui->data_len, 8) };

	ubifs_assert(c, ui->data_len == iyesde->i_size);
	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	buf = kmemdup(value, size, GFP_NOFS);
	if (!buf) {
		err = -ENOMEM;
		goto out_free;
	}
	mutex_lock(&ui->ui_mutex);
	kfree(ui->data);
	ui->data = buf;
	iyesde->i_size = ui->ui_size = size;
	old_size = ui->data_len;
	ui->data_len = size;
	mutex_unlock(&ui->ui_mutex);

	mutex_lock(&host_ui->ui_mutex);
	host->i_ctime = current_time(host);
	host_ui->xattr_size -= CALC_XATTR_BYTES(old_size);
	host_ui->xattr_size += CALC_XATTR_BYTES(size);

	/*
	 * It is important to write the host iyesde after the xattr iyesde
	 * because if the host iyesde gets synchronized (via 'fsync()'), then
	 * the extended attribute iyesde gets synchronized, because it goes
	 * before the host iyesde in the write-buffer.
	 */
	err = ubifs_jnl_change_xattr(c, iyesde, host);
	if (err)
		goto out_cancel;
	mutex_unlock(&host_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	return 0;

out_cancel:
	host_ui->xattr_size -= CALC_XATTR_BYTES(size);
	host_ui->xattr_size += CALC_XATTR_BYTES(old_size);
	mutex_unlock(&host_ui->ui_mutex);
	make_bad_iyesde(iyesde);
out_free:
	ubifs_release_budget(c, &req);
	return err;
}

static struct iyesde *iget_xattr(struct ubifs_info *c, iyes_t inum)
{
	struct iyesde *iyesde;

	iyesde = ubifs_iget(c->vfs_sb, inum);
	if (IS_ERR(iyesde)) {
		ubifs_err(c, "dead extended attribute entry, error %d",
			  (int)PTR_ERR(iyesde));
		return iyesde;
	}
	if (ubifs_iyesde(iyesde)->xattr)
		return iyesde;
	ubifs_err(c, "corrupt extended attribute entry");
	iput(iyesde);
	return ERR_PTR(-EINVAL);
}

int ubifs_xattr_set(struct iyesde *host, const char *name, const void *value,
		    size_t size, int flags, bool check_lock)
{
	struct iyesde *iyesde;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct fscrypt_name nm = { .disk_name = FSTR_INIT((char *)name, strlen(name))};
	struct ubifs_dent_yesde *xent;
	union ubifs_key key;
	int err;

	if (check_lock)
		ubifs_assert(c, iyesde_is_locked(host));

	if (size > UBIFS_MAX_INO_DATA)
		return -ERANGE;

	if (fname_len(&nm) > UBIFS_MAX_NLEN)
		return -ENAMETOOLONG;

	xent = kmalloc(UBIFS_MAX_XENT_NODE_SZ, GFP_NOFS);
	if (!xent)
		return -ENOMEM;

	/*
	 * The extended attribute entries are stored in LNC, so multiple
	 * look-ups do yest involve reading the flash.
	 */
	xent_key_init(c, &key, host->i_iyes, &nm);
	err = ubifs_tnc_lookup_nm(c, &key, xent, &nm);
	if (err) {
		if (err != -ENOENT)
			goto out_free;

		if (flags & XATTR_REPLACE)
			/* We are asked yest to create the xattr */
			err = -ENODATA;
		else
			err = create_xattr(c, host, &nm, value, size);
		goto out_free;
	}

	if (flags & XATTR_CREATE) {
		/* We are asked yest to replace the xattr */
		err = -EEXIST;
		goto out_free;
	}

	iyesde = iget_xattr(c, le64_to_cpu(xent->inum));
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		goto out_free;
	}

	err = change_xattr(c, host, iyesde, value, size);
	iput(iyesde);

out_free:
	kfree(xent);
	return err;
}

ssize_t ubifs_xattr_get(struct iyesde *host, const char *name, void *buf,
			size_t size)
{
	struct iyesde *iyesde;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct fscrypt_name nm = { .disk_name = FSTR_INIT((char *)name, strlen(name))};
	struct ubifs_iyesde *ui;
	struct ubifs_dent_yesde *xent;
	union ubifs_key key;
	int err;

	if (fname_len(&nm) > UBIFS_MAX_NLEN)
		return -ENAMETOOLONG;

	xent = kmalloc(UBIFS_MAX_XENT_NODE_SZ, GFP_NOFS);
	if (!xent)
		return -ENOMEM;

	xent_key_init(c, &key, host->i_iyes, &nm);
	err = ubifs_tnc_lookup_nm(c, &key, xent, &nm);
	if (err) {
		if (err == -ENOENT)
			err = -ENODATA;
		goto out_unlock;
	}

	iyesde = iget_xattr(c, le64_to_cpu(xent->inum));
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		goto out_unlock;
	}

	ui = ubifs_iyesde(iyesde);
	ubifs_assert(c, iyesde->i_size == ui->data_len);
	ubifs_assert(c, ubifs_iyesde(host)->xattr_size > ui->data_len);

	mutex_lock(&ui->ui_mutex);
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
	mutex_unlock(&ui->ui_mutex);
	iput(iyesde);
out_unlock:
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
	struct iyesde *host = d_iyesde(dentry);
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct ubifs_iyesde *host_ui = ubifs_iyesde(host);
	struct ubifs_dent_yesde *xent, *pxent = NULL;
	int err, len, written = 0;
	struct fscrypt_name nm = {0};

	dbg_gen("iyes %lu ('%pd'), buffer size %zd", host->i_iyes,
		dentry, size);

	len = host_ui->xattr_names + host_ui->xattr_cnt;
	if (!buffer)
		/*
		 * We should return the minimum buffer size which will fit a
		 * null-terminated list of all the extended attribute names.
		 */
		return len;

	if (len > size)
		return -ERANGE;

	lowest_xent_key(c, &key, host->i_iyes);
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
	if (err != -ENOENT) {
		ubifs_err(c, "canyest find next direntry, error %d", err);
		return err;
	}

	ubifs_assert(c, written <= size);
	return written;
}

static int remove_xattr(struct ubifs_info *c, struct iyesde *host,
			struct iyesde *iyesde, const struct fscrypt_name *nm)
{
	int err;
	struct ubifs_iyesde *host_ui = ubifs_iyesde(host);
	struct ubifs_iyesde *ui = ubifs_iyesde(iyesde);
	struct ubifs_budget_req req = { .dirtied_iyes = 2, .mod_dent = 1,
				.dirtied_iyes_d = ALIGN(host_ui->data_len, 8) };

	ubifs_assert(c, ui->data_len == iyesde->i_size);

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	mutex_lock(&host_ui->ui_mutex);
	host->i_ctime = current_time(host);
	host_ui->xattr_cnt -= 1;
	host_ui->xattr_size -= CALC_DENT_SIZE(fname_len(nm));
	host_ui->xattr_size -= CALC_XATTR_BYTES(ui->data_len);
	host_ui->xattr_names -= fname_len(nm);

	err = ubifs_jnl_delete_xattr(c, host, iyesde, nm);
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
	make_bad_iyesde(iyesde);
	return err;
}

int ubifs_purge_xattrs(struct iyesde *host)
{
	union ubifs_key key;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct ubifs_dent_yesde *xent, *pxent = NULL;
	struct iyesde *xiyes;
	struct fscrypt_name nm = {0};
	int err;

	if (ubifs_iyesde(host)->xattr_cnt < ubifs_xattr_max_cnt(c))
		return 0;

	ubifs_warn(c, "iyesde %lu has too many xattrs, doing a yesn-atomic deletion",
		   host->i_iyes);

	lowest_xent_key(c, &key, host->i_iyes);
	while (1) {
		xent = ubifs_tnc_next_ent(c, &key, &nm);
		if (IS_ERR(xent)) {
			err = PTR_ERR(xent);
			break;
		}

		fname_name(&nm) = xent->name;
		fname_len(&nm) = le16_to_cpu(xent->nlen);

		xiyes = ubifs_iget(c->vfs_sb, le64_to_cpu(xent->inum));
		if (IS_ERR(xiyes)) {
			err = PTR_ERR(xiyes);
			ubifs_err(c, "dead directory entry '%s', error %d",
				  xent->name, err);
			ubifs_ro_mode(c, err);
			kfree(pxent);
			return err;
		}

		ubifs_assert(c, ubifs_iyesde(xiyes)->xattr);

		clear_nlink(xiyes);
		err = remove_xattr(c, host, xiyes, &nm);
		if (err) {
			kfree(pxent);
			iput(xiyes);
			ubifs_err(c, "canyest remove xattr, error %d", err);
			return err;
		}

		iput(xiyes);

		kfree(pxent);
		pxent = xent;
		key_read(c, &xent->key, &key);
	}

	kfree(pxent);
	if (err != -ENOENT) {
		ubifs_err(c, "canyest find next direntry, error %d", err);
		return err;
	}

	return 0;
}

/**
 * ubifs_evict_xattr_iyesde - Evict an xattr iyesde.
 * @c: UBIFS file-system description object
 * @xattr_inum: xattr iyesde number
 *
 * When an iyesde that hosts xattrs is being removed we have to make sure
 * that cached iyesdes of the xattrs also get removed from the iyesde cache
 * otherwise we'd waste memory. This function looks up an iyesde from the
 * iyesde cache and clears the link counter such that iput() will evict
 * the iyesde.
 */
void ubifs_evict_xattr_iyesde(struct ubifs_info *c, iyes_t xattr_inum)
{
	struct iyesde *iyesde;

	iyesde = ilookup(c->vfs_sb, xattr_inum);
	if (iyesde) {
		clear_nlink(iyesde);
		iput(iyesde);
	}
}

static int ubifs_xattr_remove(struct iyesde *host, const char *name)
{
	struct iyesde *iyesde;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct fscrypt_name nm = { .disk_name = FSTR_INIT((char *)name, strlen(name))};
	struct ubifs_dent_yesde *xent;
	union ubifs_key key;
	int err;

	ubifs_assert(c, iyesde_is_locked(host));

	if (fname_len(&nm) > UBIFS_MAX_NLEN)
		return -ENAMETOOLONG;

	xent = kmalloc(UBIFS_MAX_XENT_NODE_SZ, GFP_NOFS);
	if (!xent)
		return -ENOMEM;

	xent_key_init(c, &key, host->i_iyes, &nm);
	err = ubifs_tnc_lookup_nm(c, &key, xent, &nm);
	if (err) {
		if (err == -ENOENT)
			err = -ENODATA;
		goto out_free;
	}

	iyesde = iget_xattr(c, le64_to_cpu(xent->inum));
	if (IS_ERR(iyesde)) {
		err = PTR_ERR(iyesde);
		goto out_free;
	}

	ubifs_assert(c, iyesde->i_nlink == 1);
	clear_nlink(iyesde);
	err = remove_xattr(c, host, iyesde, &nm);
	if (err)
		set_nlink(iyesde, 1);

	/* If @i_nlink is 0, 'iput()' will delete the iyesde */
	iput(iyesde);

out_free:
	kfree(xent);
	return err;
}

#ifdef CONFIG_UBIFS_FS_SECURITY
static int init_xattrs(struct iyesde *iyesde, const struct xattr *xattr_array,
		      void *fs_info)
{
	const struct xattr *xattr;
	char *name;
	int err = 0;

	for (xattr = xattr_array; xattr->name != NULL; xattr++) {
		name = kmalloc(XATTR_SECURITY_PREFIX_LEN +
			       strlen(xattr->name) + 1, GFP_NOFS);
		if (!name) {
			err = -ENOMEM;
			break;
		}
		strcpy(name, XATTR_SECURITY_PREFIX);
		strcpy(name + XATTR_SECURITY_PREFIX_LEN, xattr->name);
		/*
		 * creating a new iyesde without holding the iyesde rwsem,
		 * yes need to check whether iyesde is locked.
		 */
		err = ubifs_xattr_set(iyesde, name, xattr->value,
				      xattr->value_len, 0, false);
		kfree(name);
		if (err < 0)
			break;
	}

	return err;
}

int ubifs_init_security(struct iyesde *dentry, struct iyesde *iyesde,
			const struct qstr *qstr)
{
	int err;

	err = security_iyesde_init_security(iyesde, dentry, qstr,
					   &init_xattrs, 0);
	if (err) {
		struct ubifs_info *c = dentry->i_sb->s_fs_info;
		ubifs_err(c, "canyest initialize security for iyesde %lu, error %d",
			  iyesde->i_iyes, err);
	}
	return err;
}
#endif

static int xattr_get(const struct xattr_handler *handler,
			   struct dentry *dentry, struct iyesde *iyesde,
			   const char *name, void *buffer, size_t size)
{
	dbg_gen("xattr '%s', iyes %lu ('%pd'), buf size %zd", name,
		iyesde->i_iyes, dentry, size);

	name = xattr_full_name(handler, name);
	return ubifs_xattr_get(iyesde, name, buffer, size);
}

static int xattr_set(const struct xattr_handler *handler,
			   struct dentry *dentry, struct iyesde *iyesde,
			   const char *name, const void *value,
			   size_t size, int flags)
{
	dbg_gen("xattr '%s', host iyes %lu ('%pd'), size %zd",
		name, iyesde->i_iyes, dentry, size);

	name = xattr_full_name(handler, name);

	if (value)
		return ubifs_xattr_set(iyesde, name, value, size, flags, true);
	else
		return ubifs_xattr_remove(iyesde, name);
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

const struct xattr_handler *ubifs_xattr_handlers[] = {
	&ubifs_user_xattr_handler,
	&ubifs_trusted_xattr_handler,
#ifdef CONFIG_UBIFS_FS_SECURITY
	&ubifs_security_xattr_handler,
#endif
	NULL
};
