/*
 * Copyright (C) 2010 IBM Corporation
 *
 * Authors:
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * File: evm_secfs.c
 *	- Used to signal when key is on keyring
 *	- Get the key and enable EVM
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/audit.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include "evm.h"

static struct dentry *evm_dir;
static struct dentry *evm_init_tpm;
static struct dentry *evm_symlink;

#ifdef CONFIG_EVM_ADD_XATTRS
static struct dentry *evm_xattrs;
static DEFINE_MUTEX(xattr_list_mutex);
static int evm_xattrs_locked;
#endif

/**
 * evm_read_key - read() for <securityfs>/evm
 *
 * @filp: file pointer, not actually used
 * @buf: where to put the result
 * @count: maximum to send along
 * @ppos: where to start
 *
 * Returns number of bytes read or error code, as appropriate
 */
static ssize_t evm_read_key(struct file *filp, char __user *buf,
			    size_t count, loff_t *ppos)
{
	char temp[80];
	ssize_t rc;

	if (*ppos != 0)
		return 0;

	sprintf(temp, "%d", (evm_initialized & ~EVM_SETUP_COMPLETE));
	rc = simple_read_from_buffer(buf, count, ppos, temp, strlen(temp));

	return rc;
}

/**
 * evm_write_key - write() for <securityfs>/evm
 * @file: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start
 *
 * Used to signal that key is on the kernel key ring.
 * - get the integrity hmac key from the kernel key ring
 * - create list of hmac protected extended attributes
 * Returns number of bytes written or error code, as appropriate
 */
static ssize_t evm_write_key(struct file *file, const char __user *buf,
			     size_t count, loff_t *ppos)
{
	int i, ret;

	if (!capable(CAP_SYS_ADMIN) || (evm_initialized & EVM_SETUP_COMPLETE))
		return -EPERM;

	ret = kstrtoint_from_user(buf, count, 0, &i);

	if (ret)
		return ret;

	/* Reject invalid values */
	if (!i || (i & ~EVM_INIT_MASK) != 0)
		return -EINVAL;

	/* Don't allow a request to freshly enable metadata writes if
	 * keys are loaded.
	 */
	if ((i & EVM_ALLOW_METADATA_WRITES) &&
	    ((evm_initialized & EVM_KEY_MASK) != 0) &&
	    !(evm_initialized & EVM_ALLOW_METADATA_WRITES))
		return -EPERM;

	if (i & EVM_INIT_HMAC) {
		ret = evm_init_key();
		if (ret != 0)
			return ret;
		/* Forbid further writes after the symmetric key is loaded */
		i |= EVM_SETUP_COMPLETE;
	}

	evm_initialized |= i;

	/* Don't allow protected metadata modification if a symmetric key
	 * is loaded
	 */
	if (evm_initialized & EVM_INIT_HMAC)
		evm_initialized &= ~(EVM_ALLOW_METADATA_WRITES);

	return count;
}

static const struct file_operations evm_key_ops = {
	.read		= evm_read_key,
	.write		= evm_write_key,
};

#ifdef CONFIG_EVM_ADD_XATTRS
/**
 * evm_read_xattrs - read() for <securityfs>/evm_xattrs
 *
 * @filp: file pointer, not actually used
 * @buf: where to put the result
 * @count: maximum to send along
 * @ppos: where to start
 *
 * Returns number of bytes read or error code, as appropriate
 */
static ssize_t evm_read_xattrs(struct file *filp, char __user *buf,
			       size_t count, loff_t *ppos)
{
	char *temp;
	int offset = 0;
	ssize_t rc, size = 0;
	struct xattr_list *xattr;

	if (*ppos != 0)
		return 0;

	rc = mutex_lock_interruptible(&xattr_list_mutex);
	if (rc)
		return -ERESTARTSYS;

	list_for_each_entry(xattr, &evm_config_xattrnames, list)
		size += strlen(xattr->name) + 1;

	temp = kmalloc(size + 1, GFP_KERNEL);
	if (!temp) {
		mutex_unlock(&xattr_list_mutex);
		return -ENOMEM;
	}

	list_for_each_entry(xattr, &evm_config_xattrnames, list) {
		sprintf(temp + offset, "%s\n", xattr->name);
		offset += strlen(xattr->name) + 1;
	}

	mutex_unlock(&xattr_list_mutex);
	rc = simple_read_from_buffer(buf, count, ppos, temp, strlen(temp));

	kfree(temp);

	return rc;
}

/**
 * evm_write_xattrs - write() for <securityfs>/evm_xattrs
 * @file: file pointer, not actually used
 * @buf: where to get the data from
 * @count: bytes sent
 * @ppos: where to start
 *
 * Returns number of bytes written or error code, as appropriate
 */
static ssize_t evm_write_xattrs(struct file *file, const char __user *buf,
				size_t count, loff_t *ppos)
{
	int len, err;
	struct xattr_list *xattr, *tmp;
	struct audit_buffer *ab;
	struct iattr newattrs;
	struct inode *inode;

	if (!capable(CAP_SYS_ADMIN) || evm_xattrs_locked)
		return -EPERM;

	if (*ppos != 0)
		return -EINVAL;

	if (count > XATTR_NAME_MAX)
		return -E2BIG;

	ab = audit_log_start(NULL, GFP_KERNEL, AUDIT_INTEGRITY_EVM_XATTR);
	if (IS_ERR(ab))
		return PTR_ERR(ab);

	xattr = kmalloc(sizeof(struct xattr_list), GFP_KERNEL);
	if (!xattr) {
		err = -ENOMEM;
		goto out;
	}

	xattr->name = memdup_user_nul(buf, count);
	if (IS_ERR(xattr->name)) {
		err = PTR_ERR(xattr->name);
		xattr->name = NULL;
		goto out;
	}

	/* Remove any trailing newline */
	len = strlen(xattr->name);
	if (len && xattr->name[len-1] == '\n')
		xattr->name[len-1] = '\0';

	if (strcmp(xattr->name, ".") == 0) {
		evm_xattrs_locked = 1;
		newattrs.ia_mode = S_IFREG | 0440;
		newattrs.ia_valid = ATTR_MODE;
		inode = evm_xattrs->d_inode;
		inode_lock(inode);
		err = simple_setattr(evm_xattrs, &newattrs);
		inode_unlock(inode);
		audit_log_format(ab, "locked");
		if (!err)
			err = count;
		goto out;
	}

	audit_log_format(ab, "xattr=");
	audit_log_untrustedstring(ab, xattr->name);

	if (strncmp(xattr->name, XATTR_SECURITY_PREFIX,
		    XATTR_SECURITY_PREFIX_LEN) != 0) {
		err = -EINVAL;
		goto out;
	}

	/* Guard against races in evm_read_xattrs */
	mutex_lock(&xattr_list_mutex);
	list_for_each_entry(tmp, &evm_config_xattrnames, list) {
		if (strcmp(xattr->name, tmp->name) == 0) {
			err = -EEXIST;
			mutex_unlock(&xattr_list_mutex);
			goto out;
		}
	}
	list_add_tail_rcu(&xattr->list, &evm_config_xattrnames);
	mutex_unlock(&xattr_list_mutex);

	audit_log_format(ab, " res=0");
	audit_log_end(ab);
	return count;
out:
	audit_log_format(ab, " res=%d", err);
	audit_log_end(ab);
	if (xattr) {
		kfree(xattr->name);
		kfree(xattr);
	}
	return err;
}

static const struct file_operations evm_xattr_ops = {
	.read		= evm_read_xattrs,
	.write		= evm_write_xattrs,
};

static int evm_init_xattrs(void)
{
	evm_xattrs = securityfs_create_file("evm_xattrs", 0660, evm_dir, NULL,
					    &evm_xattr_ops);
	if (!evm_xattrs || IS_ERR(evm_xattrs))
		return -EFAULT;

	return 0;
}
#else
static int evm_init_xattrs(void)
{
	return 0;
}
#endif

int __init evm_init_secfs(void)
{
	int error = 0;

	evm_dir = securityfs_create_dir("evm", integrity_dir);
	if (!evm_dir || IS_ERR(evm_dir))
		return -EFAULT;

	evm_init_tpm = securityfs_create_file("evm", 0660,
					      evm_dir, NULL, &evm_key_ops);
	if (!evm_init_tpm || IS_ERR(evm_init_tpm)) {
		error = -EFAULT;
		goto out;
	}

	evm_symlink = securityfs_create_symlink("evm", NULL,
						"integrity/evm/evm", NULL);
	if (!evm_symlink || IS_ERR(evm_symlink)) {
		error = -EFAULT;
		goto out;
	}

	if (evm_init_xattrs() != 0) {
		error = -EFAULT;
		goto out;
	}

	return 0;
out:
	securityfs_remove(evm_symlink);
	securityfs_remove(evm_init_tpm);
	securityfs_remove(evm_dir);
	return error;
}
