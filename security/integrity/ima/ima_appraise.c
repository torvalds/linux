/*
 * Copyright (C) 2011 IBM Corporation
 *
 * Author:
 * Mimi Zohar <zohar@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 */
#include <linux/module.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/xattr.h>
#include <linux/magic.h>
#include <linux/ima.h>
#include <linux/evm.h>

#include "ima.h"

static int __init default_appraise_setup(char *str)
{
	if (strncmp(str, "off", 3) == 0)
		ima_appraise = 0;
	else if (strncmp(str, "fix", 3) == 0)
		ima_appraise = IMA_APPRAISE_FIX;
	return 1;
}

__setup("ima_appraise=", default_appraise_setup);

/*
 * ima_must_appraise - set appraise flag
 *
 * Return 1 to appraise
 */
int ima_must_appraise(struct inode *inode, int mask, enum ima_hooks func)
{
	if (!ima_appraise)
		return 0;

	return ima_match_policy(inode, func, mask, IMA_APPRAISE);
}

static void ima_fix_xattr(struct dentry *dentry,
			  struct integrity_iint_cache *iint)
{
	iint->ima_xattr.type = IMA_XATTR_DIGEST;
	__vfs_setxattr_noperm(dentry, XATTR_NAME_IMA, (u8 *)&iint->ima_xattr,
			      sizeof iint->ima_xattr, 0);
}

/*
 * ima_appraise_measurement - appraise file measurement
 *
 * Call evm_verifyxattr() to verify the integrity of 'security.ima'.
 * Assuming success, compare the xattr hash with the collected measurement.
 *
 * Return 0 on success, error code otherwise
 */
int ima_appraise_measurement(struct integrity_iint_cache *iint,
			     struct file *file, const unsigned char *filename)
{
	struct dentry *dentry = file->f_dentry;
	struct inode *inode = dentry->d_inode;
	struct evm_ima_xattr_data *xattr_value = NULL;
	enum integrity_status status = INTEGRITY_UNKNOWN;
	const char *op = "appraise_data";
	char *cause = "unknown";
	int rc;

	if (!ima_appraise)
		return 0;
	if (!inode->i_op->getxattr)
		return INTEGRITY_UNKNOWN;

	if (iint->flags & IMA_APPRAISED)
		return iint->ima_status;

	rc = vfs_getxattr_alloc(dentry, XATTR_NAME_IMA, (char **)&xattr_value,
				0, GFP_NOFS);
	if (rc <= 0) {
		if (rc && rc != -ENODATA)
			goto out;

		cause = "missing-hash";
		status =
		    (inode->i_size == 0) ? INTEGRITY_PASS : INTEGRITY_NOLABEL;
		goto out;
	}

	status = evm_verifyxattr(dentry, XATTR_NAME_IMA, xattr_value, rc, iint);
	if ((status != INTEGRITY_PASS) && (status != INTEGRITY_UNKNOWN)) {
		if ((status == INTEGRITY_NOLABEL)
		    || (status == INTEGRITY_NOXATTRS))
			cause = "missing-HMAC";
		else if (status == INTEGRITY_FAIL)
			cause = "invalid-HMAC";
		goto out;
	}

	switch (xattr_value->type) {
	case IMA_XATTR_DIGEST:
		rc = memcmp(xattr_value->digest, iint->ima_xattr.digest,
			    IMA_DIGEST_SIZE);
		if (rc) {
			cause = "invalid-hash";
			status = INTEGRITY_FAIL;
			print_hex_dump_bytes("security.ima: ", DUMP_PREFIX_NONE,
					     xattr_value, sizeof(*xattr_value));
			print_hex_dump_bytes("collected: ", DUMP_PREFIX_NONE,
					     (u8 *)&iint->ima_xattr,
					     sizeof iint->ima_xattr);
			break;
		}
		status = INTEGRITY_PASS;
		break;
	case EVM_IMA_XATTR_DIGSIG:
		iint->flags |= IMA_DIGSIG;
		rc = integrity_digsig_verify(INTEGRITY_KEYRING_IMA,
					     xattr_value->digest, rc - 1,
					     iint->ima_xattr.digest,
					     IMA_DIGEST_SIZE);
		if (rc == -EOPNOTSUPP) {
			status = INTEGRITY_UNKNOWN;
		} else if (rc) {
			cause = "invalid-signature";
			status = INTEGRITY_FAIL;
		} else {
			status = INTEGRITY_PASS;
		}
		break;
	default:
		status = INTEGRITY_UNKNOWN;
		cause = "unknown-ima-data";
		break;
	}

out:
	if (status != INTEGRITY_PASS) {
		if ((ima_appraise & IMA_APPRAISE_FIX) &&
		    (!xattr_value ||
		     xattr_value->type != EVM_IMA_XATTR_DIGSIG)) {
			ima_fix_xattr(dentry, iint);
			status = INTEGRITY_PASS;
		}
		integrity_audit_msg(AUDIT_INTEGRITY_DATA, inode, filename,
				    op, cause, rc, 0);
	} else {
		iint->flags |= IMA_APPRAISED;
	}
	iint->ima_status = status;
	kfree(xattr_value);
	return status;
}

/*
 * ima_update_xattr - update 'security.ima' hash value
 */
void ima_update_xattr(struct integrity_iint_cache *iint, struct file *file)
{
	struct dentry *dentry = file->f_dentry;
	int rc = 0;

	/* do not collect and update hash for digital signatures */
	if (iint->flags & IMA_DIGSIG)
		return;

	rc = ima_collect_measurement(iint, file);
	if (rc < 0)
		return;

	ima_fix_xattr(dentry, iint);
}

/**
 * ima_inode_post_setattr - reflect file metadata changes
 * @dentry: pointer to the affected dentry
 *
 * Changes to a dentry's metadata might result in needing to appraise.
 *
 * This function is called from notify_change(), which expects the caller
 * to lock the inode's i_mutex.
 */
void ima_inode_post_setattr(struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;
	struct integrity_iint_cache *iint;
	int must_appraise, rc;

	if (!ima_initialized || !ima_appraise || !S_ISREG(inode->i_mode)
	    || !inode->i_op->removexattr)
		return;

	must_appraise = ima_must_appraise(inode, MAY_ACCESS, POST_SETATTR);
	iint = integrity_iint_find(inode);
	if (iint) {
		if (must_appraise)
			iint->flags |= IMA_APPRAISE;
		else
			iint->flags &= ~(IMA_APPRAISE | IMA_APPRAISED);
	}
	if (!must_appraise)
		rc = inode->i_op->removexattr(dentry, XATTR_NAME_IMA);
	return;
}

/*
 * ima_protect_xattr - protect 'security.ima'
 *
 * Ensure that not just anyone can modify or remove 'security.ima'.
 */
static int ima_protect_xattr(struct dentry *dentry, const char *xattr_name,
			     const void *xattr_value, size_t xattr_value_len)
{
	if (strcmp(xattr_name, XATTR_NAME_IMA) == 0) {
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		return 1;
	}
	return 0;
}

static void ima_reset_appraise_flags(struct inode *inode)
{
	struct integrity_iint_cache *iint;

	if (!ima_initialized || !ima_appraise || !S_ISREG(inode->i_mode))
		return;

	iint = integrity_iint_find(inode);
	if (!iint)
		return;

	iint->flags &= ~IMA_DONE_MASK;
	return;
}

int ima_inode_setxattr(struct dentry *dentry, const char *xattr_name,
		       const void *xattr_value, size_t xattr_value_len)
{
	int result;

	result = ima_protect_xattr(dentry, xattr_name, xattr_value,
				   xattr_value_len);
	if (result == 1) {
		ima_reset_appraise_flags(dentry->d_inode);
		result = 0;
	}
	return result;
}

int ima_inode_removexattr(struct dentry *dentry, const char *xattr_name)
{
	int result;

	result = ima_protect_xattr(dentry, xattr_name, NULL, 0);
	if (result == 1) {
		ima_reset_appraise_flags(dentry->d_inode);
		result = 0;
	}
	return result;
}
