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

#include <linux/uaccess.h>
#include <linux/module.h>
#include "evm.h"

static struct dentry *evm_init_tpm;

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

int __init evm_init_secfs(void)
{
	int error = 0;

	evm_init_tpm = securityfs_create_file("evm", S_IRUSR | S_IRGRP,
					      NULL, NULL, &evm_key_ops);
	if (!evm_init_tpm || IS_ERR(evm_init_tpm))
		error = -EFAULT;
	return error;
}
