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
 * Extended attributes are implemented as regular inodes with attached data,
 * which limits extended attribute size to UBIFS block size (4KiB). Names of
 * extended attributes are described by extended attribute entries (xentries),
 * which are almost identical to directory entries, but have different key type.
 *
 * In other words, the situation with extended attributes is very similar to
 * directories. Indeed, any inode (but of course not xattr inodes) may have a
 * number of associated xentries, just like directory inodes have associated
 * directory entries. Extended attribute entries store the name of the extended
 * attribute, the host inode number, and the extended attribute inode number.
 * Similarly, direntries store the name, the parent and the target inode
 * numbers. Thus, most of the common UBIFS mechanisms may be re-used for
 * extended attributes.
 *
 * The number of extended attributes is not limited, but there is Linux
 * limitation on the maximum possible size of the list of all extended
 * attributes associated with an inode (%XATTR_LIST_MAX), so UBIFS makes sure
 * the sum of all extended attribute names of the inode does not exceed that
 * limit.
 *
 * Extended attributes are synchronous, which means they are written to the
 * flash media synchronously and there is no write-back for extended attribute
 * inodes. The extended attribute values are not stored in compressed form on
 * the media.
 *
 * Since extended attributes are represented by regular inodes, they are cached
 * in the VFS inode cache. The xentries are cached in the LNC cache (see
 * tnc.c).
 *
 * ACL support is not implemented.
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

static const struct inode_operations empty_iops;
static const struct file_operations empty_fops;

/**
 * create_xattr - create an extended attribute.
 * @c: UBIFS file-system description object
 * @host: host inode
 * @nm: extended attribute name
 * @value: extended attribute value
 * @size: size of extended attribute value
 *
 * This is a helper function which creates an extended attribute of name @nm
 * and value @value for inode @host. The host inode is also updated on flash
 * because the ctime and extended attribute accounting data changes. This
 * function returns zero in case of success and a negative error code in case
 * of failure.
 */
static int create_xattr(struct ubifs_info *c, struct inode *host,
			const struct fscrypt_name *nm, const void *value, int size)
{
	int err, names_len;
	struct inode *inode;
	struct ubifs_inode *ui, *host_ui = ubifs_inode(host);
	struct ubifs_budget_req req = { .new_ino = 1, .new_dent = 1,
				.new_ino_d = ALIGN(size, 8), .dirtied_ino = 1,
				.dirtied_ino_d = ALIGN(host_ui->data_len, 8) };

	if (host_ui->xattr_cnt >= ubifs_xattr_max_cnt(c)) {
		ubifs_err(c, "inode %lu already has too many xattrs (%d), cannot create more",
			  host->i_ino, host_ui->xattr_cnt);
		return -ENOSPC;
	}
	/*
	 * Linux limits the maximum size of the extended attribute names list
	 * to %XATTR_LIST_MAX. This means we should not allow creating more
	 * extended attributes if the name list becomes larger. This limitation
	 * is artificial for UBIFS, though.
	 */
	names_len = host_ui->xattr_names + host_ui->xattr_cnt + fname_len(nm) + 1;
	if (names_len > XATTR_LIST_MAX) {
		ubifs_err(c, "cannot add one more xattr name to inode %lu, total names length would become %d, max. is %d",
			  host->i_ino, names_len, XATTR_LIST_MAX);
		return -ENOSPC;
	}

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	inode = ubifs_new_inode(c, host, S_IFREG | S_IRWXUGO);
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_budg;
	}

	/* Re-define all operations to be "nothing" */
	inode->i_mapping->a_ops = &empty_aops;
	inode->i_op = &empty_iops;
	inode->i_fop = &empty_fops;

	inode->i_flags |= S_SYNC | S_NOATIME | S_NOCMTIME;
	ui = ubifs_inode(inode);
	ui->xattr = 1;
	ui->flags |= UBIFS_XATTR_FL;
	ui->data = kmemdup(value, size, GFP_NOFS);
	if (!ui->data) {
		err = -ENOMEM;
		goto out_free;
	}
	inode->i_size = ui->ui_size = size;
	ui->data_len = size;

	mutex_lock(&host_ui->ui_mutex);
	host->i_ctime = current_time(host);
	host_ui->xattr_cnt += 1;
	host_ui->xattr_size += CALC_DENT_SIZE(fname_len(nm));
	host_ui->xattr_size += CALC_XATTR_BYTES(size);
	host_ui->xattr_names += fname_len(nm);

	/*
	 * We handle UBIFS_XATTR_NAME_ENCRYPTION_CONTEXT here because we
	 * have to set the UBIFS_CRYPT_FL flag on the host inode.
	 * To avoid multiple updates of the same inode in the same operation,
	 * let's do it here.
	 */
	if (strcmp(fname_name(nm), UBIFS_XATTR_NAME_ENCRYPTION_CONTEXT) == 0)
		host_ui->flags |= UBIFS_CRYPT_FL;

	err = ubifs_jnl_update(c, host, nm, inode, 0, 1);
	if (err)
		goto out_cancel;
	ubifs_set_inode_flags(host);
	mutex_unlock(&host_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	insert_inode_hash(inode);
	iput(inode);
	return 0;

out_cancel:
	host_ui->xattr_cnt -= 1;
	host_ui->xattr_size -= CALC_DENT_SIZE(fname_len(nm));
	host_ui->xattr_size -= CALC_XATTR_BYTES(size);
	host_ui->xattr_names -= fname_len(nm);
	host_ui->flags &= ~UBIFS_CRYPT_FL;
	mutex_unlock(&host_ui->ui_mutex);
out_free:
	make_bad_inode(inode);
	iput(inode);
out_budg:
	ubifs_release_budget(c, &req);
	return err;
}

/**
 * change_xattr - change an extended attribute.
 * @c: UBIFS file-system description object
 * @host: host inode
 * @inode: extended attribute inode
 * @value: extended attribute value
 * @size: size of extended attribute value
 *
 * This helper function changes the value of extended attribute @inode with new
 * data from @value. Returns zero in case of success and a negative error code
 * in case of failure.
 */
static int change_xattr(struct ubifs_info *c, struct inode *host,
			struct inode *inode, const void *value, int size)
{
	int err;
	struct ubifs_inode *host_ui = ubifs_inode(host);
	struct ubifs_inode *ui = ubifs_inode(inode);
	void *buf = NULL;
	int old_size;
	struct ubifs_budget_req req = { .dirtied_ino = 2,
		.dirtied_ino_d = ALIGN(size, 8) + ALIGN(host_ui->data_len, 8) };

	ubifs_assert(c, ui->data_len == inode->i_size);
	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	buf = kmemdup(value, size, GFP_NOFS);
	if (!buf) {
		err = -ENOMEM;
		goto out_free;
	}
	kfree(ui->data);
	ui->data = buf;
	inode->i_size = ui->ui_size = size;
	old_size = ui->data_len;
	ui->data_len = size;

	mutex_lock(&host_ui->ui_mutex);
	host->i_ctime = current_time(host);
	host_ui->xattr_size -= CALC_XATTR_BYTES(old_size);
	host_ui->xattr_size += CALC_XATTR_BYTES(size);

	/*
	 * It is important to write the host inode after the xattr inode
	 * because if the host inode gets synchronized (via 'fsync()'), then
	 * the extended attribute inode gets synchronized, because it goes
	 * before the host inode in the write-buffer.
	 */
	err = ubifs_jnl_change_xattr(c, inode, host);
	if (err)
		goto out_cancel;
	mutex_unlock(&host_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	return 0;

out_cancel:
	host_ui->xattr_size -= CALC_XATTR_BYTES(size);
	host_ui->xattr_size += CALC_XATTR_BYTES(old_size);
	mutex_unlock(&host_ui->ui_mutex);
	make_bad_inode(inode);
out_free:
	ubifs_release_budget(c, &req);
	return err;
}

static struct inode *iget_xattr(struct ubifs_info *c, ino_t inum)
{
	struct inode *inode;

	inode = ubifs_iget(c->vfs_sb, inum);
	if (IS_ERR(inode)) {
		ubifs_err(c, "dead extended attribute entry, error %d",
			  (int)PTR_ERR(inode));
		return inode;
	}
	if (ubifs_inode(inode)->xattr)
		return inode;
	ubifs_err(c, "corrupt extended attribute entry");
	iput(inode);
	return ERR_PTR(-EINVAL);
}

int ubifs_xattr_set(struct inode *host, const char *name, const void *value,
		    size_t size, int flags, bool check_lock)
{
	struct inode *inode;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct fscrypt_name nm = { .disk_name = FSTR_INIT((char *)name, strlen(name))};
	struct ubifs_dent_node *xent;
	union ubifs_key key;
	int err;

	if (check_lock)
		ubifs_assert(c, inode_is_locked(host));

	if (size > UBIFS_MAX_INO_DATA)
		return -ERANGE;

	if (fname_len(&nm) > UBIFS_MAX_NLEN)
		return -ENAMETOOLONG;

	xent = kmalloc(UBIFS_MAX_XENT_NODE_SZ, GFP_NOFS);
	if (!xent)
		return -ENOMEM;

	down_write(&ubifs_inode(host)->xattr_sem);
	/*
	 * The extended attribute entries are stored in LNC, so multiple
	 * look-ups do not involve reading the flash.
	 */
	xent_key_init(c, &key, host->i_ino, &nm);
	err = ubifs_tnc_lookup_nm(c, &key, xent, &nm);
	if (err) {
		if (err != -ENOENT)
			goto out_free;

		if (flags & XATTR_REPLACE)
			/* We are asked not to create the xattr */
			err = -ENODATA;
		else
			err = create_xattr(c, host, &nm, value, size);
		goto out_free;
	}

	if (flags & XATTR_CREATE) {
		/* We are asked not to replace the xattr */
		err = -EEXIST;
		goto out_free;
	}

	inode = iget_xattr(c, le64_to_cpu(xent->inum));
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_free;
	}

	err = change_xattr(c, host, inode, value, size);
	iput(inode);

out_free:
	up_write(&ubifs_inode(host)->xattr_sem);
	kfree(xent);
	return err;
}

ssize_t ubifs_xattr_get(struct inode *host, const char *name, void *buf,
			size_t size)
{
	struct inode *inode;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct fscrypt_name nm = { .disk_name = FSTR_INIT((char *)name, strlen(name))};
	struct ubifs_inode *ui;
	struct ubifs_dent_node *xent;
	union ubifs_key key;
	int err;

	if (fname_len(&nm) > UBIFS_MAX_NLEN)
		return -ENAMETOOLONG;

	xent = kmalloc(UBIFS_MAX_XENT_NODE_SZ, GFP_NOFS);
	if (!xent)
		return -ENOMEM;

	down_read(&ubifs_inode(host)->xattr_sem);
	xent_key_init(c, &key, host->i_ino, &nm);
	err = ubifs_tnc_lookup_nm(c, &key, xent, &nm);
	if (err) {
		if (err == -ENOENT)
			err = -ENODATA;
		goto out_cleanup;
	}

	inode = iget_xattr(c, le64_to_cpu(xent->inum));
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_cleanup;
	}

	ui = ubifs_inode(inode);
	ubifs_assert(c, inode->i_size == ui->data_len);
	ubifs_assert(c, ubifs_inode(host)->xattr_size > ui->data_len);

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
	iput(inode);
out_cleanup:
	up_read(&ubifs_inode(host)->xattr_sem);
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
	struct inode *host = d_inode(dentry);
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct ubifs_inode *host_ui = ubifs_inode(host);
	struct ubifs_dent_node *xent, *pxent = NULL;
	int err, len, written = 0;
	struct fscrypt_name nm = {0};

	dbg_gen("ino %lu ('%pd'), buffer size %zd", host->i_ino,
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

	lowest_xent_key(c, &key, host->i_ino);
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

	if (err != -ENOENT) {
		ubifs_err(c, "cannot find next direntry, error %d", err);
		return err;
	}

	ubifs_assert(c, written <= size);
	return written;

out_err:
	up_read(&host_ui->xattr_sem);
	return err;
}

static int remove_xattr(struct ubifs_info *c, struct inode *host,
			struct inode *inode, const struct fscrypt_name *nm)
{
	int err;
	struct ubifs_inode *host_ui = ubifs_inode(host);
	struct ubifs_inode *ui = ubifs_inode(inode);
	struct ubifs_budget_req req = { .dirtied_ino = 2, .mod_dent = 1,
				.dirtied_ino_d = ALIGN(host_ui->data_len, 8) };

	ubifs_assert(c, ui->data_len == inode->i_size);

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	mutex_lock(&host_ui->ui_mutex);
	host->i_ctime = current_time(host);
	host_ui->xattr_cnt -= 1;
	host_ui->xattr_size -= CALC_DENT_SIZE(fname_len(nm));
	host_ui->xattr_size -= CALC_XATTR_BYTES(ui->data_len);
	host_ui->xattr_names -= fname_len(nm);

	err = ubifs_jnl_delete_xattr(c, host, inode, nm);
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
	make_bad_inode(inode);
	return err;
}

int ubifs_purge_xattrs(struct inode *host)
{
	union ubifs_key key;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct ubifs_dent_node *xent, *pxent = NULL;
	struct inode *xino;
	struct fscrypt_name nm = {0};
	int err;

	if (ubifs_inode(host)->xattr_cnt <= ubifs_xattr_max_cnt(c))
		return 0;

	ubifs_warn(c, "inode %lu has too many xattrs, doing a non-atomic deletion",
		   host->i_ino);

	down_write(&ubifs_inode(host)->xattr_sem);
	lowest_xent_key(c, &key, host->i_ino);
	while (1) {
		xent = ubifs_tnc_next_ent(c, &key, &nm);
		if (IS_ERR(xent)) {
			err = PTR_ERR(xent);
			break;
		}

		fname_name(&nm) = xent->name;
		fname_len(&nm) = le16_to_cpu(xent->nlen);

		xino = ubifs_iget(c->vfs_sb, le64_to_cpu(xent->inum));
		if (IS_ERR(xino)) {
			err = PTR_ERR(xino);
			ubifs_err(c, "dead directory entry '%s', error %d",
				  xent->name, err);
			ubifs_ro_mode(c, err);
			kfree(pxent);
			kfree(xent);
			goto out_err;
		}

		ubifs_assert(c, ubifs_inode(xino)->xattr);

		clear_nlink(xino);
		err = remove_xattr(c, host, xino, &nm);
		if (err) {
			kfree(pxent);
			kfree(xent);
			iput(xino);
			ubifs_err(c, "cannot remove xattr, error %d", err);
			goto out_err;
		}

		iput(xino);

		kfree(pxent);
		pxent = xent;
		key_read(c, &xent->key, &key);
	}
	kfree(pxent);
	up_write(&ubifs_inode(host)->xattr_sem);

	if (err != -ENOENT) {
		ubifs_err(c, "cannot find next direntry, error %d", err);
		return err;
	}

	return 0;

out_err:
	up_write(&ubifs_inode(host)->xattr_sem);
	return err;
}

/**
 * ubifs_evict_xattr_inode - Evict an xattr inode.
 * @c: UBIFS file-system description object
 * @xattr_inum: xattr inode number
 *
 * When an inode that hosts xattrs is being removed we have to make sure
 * that cached inodes of the xattrs also get removed from the inode cache
 * otherwise we'd waste memory. This function looks up an inode from the
 * inode cache and clears the link counter such that iput() will evict
 * the inode.
 */
void ubifs_evict_xattr_inode(struct ubifs_info *c, ino_t xattr_inum)
{
	struct inode *inode;

	inode = ilookup(c->vfs_sb, xattr_inum);
	if (inode) {
		clear_nlink(inode);
		iput(inode);
	}
}

static int ubifs_xattr_remove(struct inode *host, const char *name)
{
	struct inode *inode;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct fscrypt_name nm = { .disk_name = FSTR_INIT((char *)name, strlen(name))};
	struct ubifs_dent_node *xent;
	union ubifs_key key;
	int err;

	ubifs_assert(c, inode_is_locked(host));

	if (fname_len(&nm) > UBIFS_MAX_NLEN)
		return -ENAMETOOLONG;

	xent = kmalloc(UBIFS_MAX_XENT_NODE_SZ, GFP_NOFS);
	if (!xent)
		return -ENOMEM;

	down_write(&ubifs_inode(host)->xattr_sem);
	xent_key_init(c, &key, host->i_ino, &nm);
	err = ubifs_tnc_lookup_nm(c, &key, xent, &nm);
	if (err) {
		if (err == -ENOENT)
			err = -ENODATA;
		goto out_free;
	}

	inode = iget_xattr(c, le64_to_cpu(xent->inum));
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_free;
	}

	ubifs_assert(c, inode->i_nlink == 1);
	clear_nlink(inode);
	err = remove_xattr(c, host, inode, &nm);
	if (err)
		set_nlink(inode, 1);

	/* If @i_nlink is 0, 'iput()' will delete the inode */
	iput(inode);

out_free:
	up_write(&ubifs_inode(host)->xattr_sem);
	kfree(xent);
	return err;
}

#ifdef CONFIG_UBIFS_FS_SECURITY
static int init_xattrs(struct inode *inode, const struct xattr *xattr_array,
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
		 * creating a new inode without holding the inode rwsem,
		 * no need to check whether inode is locked.
		 */
		err = ubifs_xattr_set(inode, name, xattr->value,
				      xattr->value_len, 0, false);
		kfree(name);
		if (err < 0)
			break;
	}

	return err;
}

int ubifs_init_security(struct inode *dentry, struct inode *inode,
			const struct qstr *qstr)
{
	int err;

	err = security_inode_init_security(inode, dentry, qstr,
					   &init_xattrs, 0);
	if (err) {
		struct ubifs_info *c = dentry->i_sb->s_fs_info;
		ubifs_err(c, "cannot initialize security for inode %lu, error %d",
			  inode->i_ino, err);
	}
	return err;
}
#endif

static int xattr_get(const struct xattr_handler *handler,
			   struct dentry *dentry, struct inode *inode,
			   const char *name, void *buffer, size_t size)
{
	dbg_gen("xattr '%s', ino %lu ('%pd'), buf size %zd", name,
		inode->i_ino, dentry, size);

	name = xattr_full_name(handler, name);
	return ubifs_xattr_get(inode, name, buffer, size);
}

static int xattr_set(const struct xattr_handler *handler,
			   struct user_namespace *mnt_userns,
			   struct dentry *dentry, struct inode *inode,
			   const char *name, const void *value,
			   size_t size, int flags)
{
	dbg_gen("xattr '%s', host ino %lu ('%pd'), size %zd",
		name, inode->i_ino, dentry, size);

	name = xattr_full_name(handler, name);

	if (value)
		return ubifs_xattr_set(inode, name, value, size, flags, true);
	else
		return ubifs_xattr_remove(inode, name);
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
