/*
 * This file is part of UBIFS.
 *
 * Copyright (C) 2006-2008 Nokia Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc., 51
 * Franklin St, Fifth Floor, Boston, MA 02110-1301 USA
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
#include <linux/posix_acl_xattr.h>

/*
 * Limit the number of extended attributes per inode so that the total size
 * (@xattr_size) is guaranteeded to fit in an 'unsigned int'.
 */
#define MAX_XATTRS_PER_INODE 65535

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
			const struct qstr *nm, const void *value, int size)
{
	int err, names_len;
	struct inode *inode;
	struct ubifs_inode *ui, *host_ui = ubifs_inode(host);
	struct ubifs_budget_req req = { .new_ino = 1, .new_dent = 1,
				.new_ino_d = ALIGN(size, 8), .dirtied_ino = 1,
				.dirtied_ino_d = ALIGN(host_ui->data_len, 8) };

	if (host_ui->xattr_cnt >= MAX_XATTRS_PER_INODE) {
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
	names_len = host_ui->xattr_names + host_ui->xattr_cnt + nm->len + 1;
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

	inode->i_flags |= S_SYNC | S_NOATIME | S_NOCMTIME | S_NOQUOTA;
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
	host->i_ctime = ubifs_current_time(host);
	host_ui->xattr_cnt += 1;
	host_ui->xattr_size += CALC_DENT_SIZE(nm->len);
	host_ui->xattr_size += CALC_XATTR_BYTES(size);
	host_ui->xattr_names += nm->len;

	err = ubifs_jnl_update(c, host, nm, inode, 0, 1);
	if (err)
		goto out_cancel;
	mutex_unlock(&host_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	insert_inode_hash(inode);
	iput(inode);
	return 0;

out_cancel:
	host_ui->xattr_cnt -= 1;
	host_ui->xattr_size -= CALC_DENT_SIZE(nm->len);
	host_ui->xattr_size -= CALC_XATTR_BYTES(size);
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
	struct ubifs_budget_req req = { .dirtied_ino = 2,
		.dirtied_ino_d = ALIGN(size, 8) + ALIGN(host_ui->data_len, 8) };

	ubifs_assert(ui->data_len == inode->i_size);
	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	kfree(ui->data);
	ui->data = kmemdup(value, size, GFP_NOFS);
	if (!ui->data) {
		err = -ENOMEM;
		goto out_free;
	}
	inode->i_size = ui->ui_size = size;
	ui->data_len = size;

	mutex_lock(&host_ui->ui_mutex);
	host->i_ctime = ubifs_current_time(host);
	host_ui->xattr_size -= CALC_XATTR_BYTES(ui->data_len);
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
	host_ui->xattr_size += CALC_XATTR_BYTES(ui->data_len);
	mutex_unlock(&host_ui->ui_mutex);
	make_bad_inode(inode);
out_free:
	ubifs_release_budget(c, &req);
	return err;
}

/**
 * check_namespace - check extended attribute name-space.
 * @nm: extended attribute name
 *
 * This function makes sure the extended attribute name belongs to one of the
 * supported extended attribute name-spaces. Returns name-space index in case
 * of success and a negative error code in case of failure.
 */
static int check_namespace(const struct qstr *nm)
{
	int type;

	if (nm->len > UBIFS_MAX_NLEN)
		return -ENAMETOOLONG;

	if (!strncmp(nm->name, XATTR_TRUSTED_PREFIX,
		     XATTR_TRUSTED_PREFIX_LEN)) {
		if (nm->name[sizeof(XATTR_TRUSTED_PREFIX) - 1] == '\0')
			return -EINVAL;
		type = TRUSTED_XATTR;
	} else if (!strncmp(nm->name, XATTR_USER_PREFIX,
				      XATTR_USER_PREFIX_LEN)) {
		if (nm->name[XATTR_USER_PREFIX_LEN] == '\0')
			return -EINVAL;
		type = USER_XATTR;
	} else if (!strncmp(nm->name, XATTR_SECURITY_PREFIX,
				     XATTR_SECURITY_PREFIX_LEN)) {
		if (nm->name[sizeof(XATTR_SECURITY_PREFIX) - 1] == '\0')
			return -EINVAL;
		type = SECURITY_XATTR;
	} else
		return -EOPNOTSUPP;

	return type;
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

static int setxattr(struct inode *host, const char *name, const void *value,
		    size_t size, int flags)
{
	struct inode *inode;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct qstr nm = QSTR_INIT(name, strlen(name));
	struct ubifs_dent_node *xent;
	union ubifs_key key;
	int err, type;

	ubifs_assert(mutex_is_locked(&host->i_mutex));

	if (size > UBIFS_MAX_INO_DATA)
		return -ERANGE;

	type = check_namespace(&nm);
	if (type < 0)
		return type;

	xent = kmalloc(UBIFS_MAX_XENT_NODE_SZ, GFP_NOFS);
	if (!xent)
		return -ENOMEM;

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
	kfree(xent);
	return err;
}

int ubifs_setxattr(struct dentry *dentry, const char *name,
		   const void *value, size_t size, int flags)
{
	dbg_gen("xattr '%s', host ino %lu ('%pd'), size %zd",
		name, dentry->d_inode->i_ino, dentry, size);

	return setxattr(dentry->d_inode, name, value, size, flags);
}

ssize_t ubifs_getxattr(struct dentry *dentry, const char *name, void *buf,
		       size_t size)
{
	struct inode *inode, *host = dentry->d_inode;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct qstr nm = QSTR_INIT(name, strlen(name));
	struct ubifs_inode *ui;
	struct ubifs_dent_node *xent;
	union ubifs_key key;
	int err;

	dbg_gen("xattr '%s', ino %lu ('%pd'), buf size %zd", name,
		host->i_ino, dentry, size);

	err = check_namespace(&nm);
	if (err < 0)
		return err;

	xent = kmalloc(UBIFS_MAX_XENT_NODE_SZ, GFP_NOFS);
	if (!xent)
		return -ENOMEM;

	xent_key_init(c, &key, host->i_ino, &nm);
	err = ubifs_tnc_lookup_nm(c, &key, xent, &nm);
	if (err) {
		if (err == -ENOENT)
			err = -ENODATA;
		goto out_unlock;
	}

	inode = iget_xattr(c, le64_to_cpu(xent->inum));
	if (IS_ERR(inode)) {
		err = PTR_ERR(inode);
		goto out_unlock;
	}

	ui = ubifs_inode(inode);
	ubifs_assert(inode->i_size == ui->data_len);
	ubifs_assert(ubifs_inode(host)->xattr_size > ui->data_len);

	if (buf) {
		/* If @buf is %NULL we are supposed to return the length */
		if (ui->data_len > size) {
			ubifs_err(c, "buffer size %zd, xattr len %d",
				  size, ui->data_len);
			err = -ERANGE;
			goto out_iput;
		}

		memcpy(buf, ui->data, ui->data_len);
	}
	err = ui->data_len;

out_iput:
	iput(inode);
out_unlock:
	kfree(xent);
	return err;
}

ssize_t ubifs_listxattr(struct dentry *dentry, char *buffer, size_t size)
{
	union ubifs_key key;
	struct inode *host = dentry->d_inode;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct ubifs_inode *host_ui = ubifs_inode(host);
	struct ubifs_dent_node *xent, *pxent = NULL;
	int err, len, written = 0;
	struct qstr nm = { .name = NULL };

	dbg_gen("ino %lu ('%pd'), buffer size %zd", host->i_ino,
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

	lowest_xent_key(c, &key, host->i_ino);
	while (1) {
		int type;

		xent = ubifs_tnc_next_ent(c, &key, &nm);
		if (IS_ERR(xent)) {
			err = PTR_ERR(xent);
			break;
		}

		nm.name = xent->name;
		nm.len = le16_to_cpu(xent->nlen);

		type = check_namespace(&nm);
		if (unlikely(type < 0)) {
			err = type;
			break;
		}

		/* Show trusted namespace only for "power" users */
		if (type != TRUSTED_XATTR || capable(CAP_SYS_ADMIN)) {
			memcpy(buffer + written, nm.name, nm.len + 1);
			written += nm.len + 1;
		}

		kfree(pxent);
		pxent = xent;
		key_read(c, &xent->key, &key);
	}

	kfree(pxent);
	if (err != -ENOENT) {
		ubifs_err(c, "cannot find next direntry, error %d", err);
		return err;
	}

	ubifs_assert(written <= size);
	return written;
}

static int remove_xattr(struct ubifs_info *c, struct inode *host,
			struct inode *inode, const struct qstr *nm)
{
	int err;
	struct ubifs_inode *host_ui = ubifs_inode(host);
	struct ubifs_inode *ui = ubifs_inode(inode);
	struct ubifs_budget_req req = { .dirtied_ino = 2, .mod_dent = 1,
				.dirtied_ino_d = ALIGN(host_ui->data_len, 8) };

	ubifs_assert(ui->data_len == inode->i_size);

	err = ubifs_budget_space(c, &req);
	if (err)
		return err;

	mutex_lock(&host_ui->ui_mutex);
	host->i_ctime = ubifs_current_time(host);
	host_ui->xattr_cnt -= 1;
	host_ui->xattr_size -= CALC_DENT_SIZE(nm->len);
	host_ui->xattr_size -= CALC_XATTR_BYTES(ui->data_len);
	host_ui->xattr_names -= nm->len;

	err = ubifs_jnl_delete_xattr(c, host, inode, nm);
	if (err)
		goto out_cancel;
	mutex_unlock(&host_ui->ui_mutex);

	ubifs_release_budget(c, &req);
	return 0;

out_cancel:
	host_ui->xattr_cnt += 1;
	host_ui->xattr_size += CALC_DENT_SIZE(nm->len);
	host_ui->xattr_size += CALC_XATTR_BYTES(ui->data_len);
	mutex_unlock(&host_ui->ui_mutex);
	ubifs_release_budget(c, &req);
	make_bad_inode(inode);
	return err;
}

int ubifs_removexattr(struct dentry *dentry, const char *name)
{
	struct inode *inode, *host = dentry->d_inode;
	struct ubifs_info *c = host->i_sb->s_fs_info;
	struct qstr nm = QSTR_INIT(name, strlen(name));
	struct ubifs_dent_node *xent;
	union ubifs_key key;
	int err;

	dbg_gen("xattr '%s', ino %lu ('%pd')", name,
		host->i_ino, dentry);
	ubifs_assert(mutex_is_locked(&host->i_mutex));

	err = check_namespace(&nm);
	if (err < 0)
		return err;

	xent = kmalloc(UBIFS_MAX_XENT_NODE_SZ, GFP_NOFS);
	if (!xent)
		return -ENOMEM;

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

	ubifs_assert(inode->i_nlink == 1);
	clear_nlink(inode);
	err = remove_xattr(c, host, inode, &nm);
	if (err)
		set_nlink(inode, 1);

	/* If @i_nlink is 0, 'iput()' will delete the inode */
	iput(inode);

out_free:
	kfree(xent);
	return err;
}

static size_t security_listxattr(struct dentry *d, char *list, size_t list_size,
				 const char *name, size_t name_len, int flags)
{
	const int prefix_len = XATTR_SECURITY_PREFIX_LEN;
	const size_t total_len = prefix_len + name_len + 1;

	if (list && total_len <= list_size) {
		memcpy(list, XATTR_SECURITY_PREFIX, prefix_len);
		memcpy(list + prefix_len, name, name_len);
		list[prefix_len + name_len] = '\0';
	}

	return total_len;
}

static int security_getxattr(struct dentry *d, const char *name, void *buffer,
		      size_t size, int flags)
{
	return ubifs_getxattr(d, name, buffer, size);
}

static int security_setxattr(struct dentry *d, const char *name,
			     const void *value, size_t size, int flags,
			     int handler_flags)
{
	return ubifs_setxattr(d, name, value, size, flags);
}

static const struct xattr_handler ubifs_xattr_security_handler = {
	.prefix = XATTR_SECURITY_PREFIX,
	.list   = security_listxattr,
	.get    = security_getxattr,
	.set    = security_setxattr,
};

const struct xattr_handler *ubifs_xattr_handlers[] = {
	&ubifs_xattr_security_handler,
	NULL,
};

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
		err = setxattr(inode, name, xattr->value, xattr->value_len, 0);
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

	mutex_lock(&inode->i_mutex);
	err = security_inode_init_security(inode, dentry, qstr,
					   &init_xattrs, 0);
	mutex_unlock(&inode->i_mutex);

	if (err) {
		struct ubifs_info *c = dentry->i_sb->s_fs_info;
		ubifs_err(c, "cannot initialize security for inode %lu, error %d",
			  inode->i_ino, err);
	}
	return err;
}
