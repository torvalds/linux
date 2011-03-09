/*
 * Copyright (C) 2005-2010 IBM Corporation
 *
 * Author:
 * Mimi Zohar <zohar@us.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * File: evm_main.c
 *	implements evm_inode_setxattr, evm_inode_post_setxattr,
 *	evm_inode_removexattr, and evm_verifyxattr
 */

#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/xattr.h>
#include <linux/integrity.h>
#include "evm.h"

int evm_initialized;

char *evm_hmac = "hmac(sha1)";

char *evm_config_xattrnames[] = {
#ifdef CONFIG_SECURITY_SELINUX
	XATTR_NAME_SELINUX,
#endif
#ifdef CONFIG_SECURITY_SMACK
	XATTR_NAME_SMACK,
#endif
	XATTR_NAME_CAPS,
	NULL
};

/*
 * evm_verify_hmac - calculate and compare the HMAC with the EVM xattr
 *
 * Compute the HMAC on the dentry's protected set of extended attributes
 * and compare it against the stored security.evm xattr. (For performance,
 * use the previoulsy retrieved xattr value and length to calculate the
 * HMAC.)
 *
 * Returns integrity status
 */
static enum integrity_status evm_verify_hmac(struct dentry *dentry,
					     const char *xattr_name,
					     char *xattr_value,
					     size_t xattr_value_len,
					     struct integrity_iint_cache *iint)
{
	struct evm_ima_xattr_data xattr_data;
	int rc;

	if (iint->hmac_status != INTEGRITY_UNKNOWN)
		return iint->hmac_status;

	rc = evm_calc_hmac(dentry, xattr_name, xattr_value,
			   xattr_value_len, xattr_data.digest);
	if (rc < 0)
		return INTEGRITY_UNKNOWN;

	xattr_data.type = EVM_XATTR_HMAC;
	rc = vfs_xattr_cmp(dentry, XATTR_NAME_EVM, (u8 *)&xattr_data,
			   sizeof xattr_data, GFP_NOFS);
	if (rc < 0)
		goto err_out;
	iint->hmac_status = INTEGRITY_PASS;
	return iint->hmac_status;

err_out:
	switch (rc) {
	case -ENODATA:		/* file not labelled */
		iint->hmac_status = INTEGRITY_NOLABEL;
		break;
	case -EINVAL:
		iint->hmac_status = INTEGRITY_FAIL;
		break;
	default:
		iint->hmac_status = INTEGRITY_UNKNOWN;
	}
	return iint->hmac_status;
}

static int evm_protected_xattr(const char *req_xattr_name)
{
	char **xattrname;
	int namelen;
	int found = 0;

	namelen = strlen(req_xattr_name);
	for (xattrname = evm_config_xattrnames; *xattrname != NULL; xattrname++) {
		if ((strlen(*xattrname) == namelen)
		    && (strncmp(req_xattr_name, *xattrname, namelen) == 0)) {
			found = 1;
			break;
		}
	}
	return found;
}

/**
 * evm_verifyxattr - verify the integrity of the requested xattr
 * @dentry: object of the verify xattr
 * @xattr_name: requested xattr
 * @xattr_value: requested xattr value
 * @xattr_value_len: requested xattr value length
 *
 * Calculate the HMAC for the given dentry and verify it against the stored
 * security.evm xattr. For performance, use the xattr value and length
 * previously retrieved to calculate the HMAC.
 *
 * Returns the xattr integrity status.
 *
 * This function requires the caller to lock the inode's i_mutex before it
 * is executed.
 */
enum integrity_status evm_verifyxattr(struct dentry *dentry,
				      const char *xattr_name,
				      void *xattr_value, size_t xattr_value_len)
{
	struct inode *inode = dentry->d_inode;
	struct integrity_iint_cache *iint;
	enum integrity_status status;

	if (!evm_initialized || !evm_protected_xattr(xattr_name))
		return INTEGRITY_UNKNOWN;

	iint = integrity_iint_find(inode);
	if (!iint)
		return INTEGRITY_UNKNOWN;
	status = evm_verify_hmac(dentry, xattr_name, xattr_value,
				 xattr_value_len, iint);
	return status;
}
EXPORT_SYMBOL_GPL(evm_verifyxattr);

/*
 * evm_protect_xattr - protect the EVM extended attribute
 *
 * Prevent security.evm from being modified or removed.
 */
static int evm_protect_xattr(struct dentry *dentry, const char *xattr_name,
			     const void *xattr_value, size_t xattr_value_len)
{
	if (strcmp(xattr_name, XATTR_NAME_EVM) == 0) {
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
	}
	return 0;
}

/**
 * evm_inode_setxattr - protect the EVM extended attribute
 * @dentry: pointer to the affected dentry
 * @xattr_name: pointer to the affected extended attribute name
 * @xattr_value: pointer to the new extended attribute value
 * @xattr_value_len: pointer to the new extended attribute value length
 *
 * Prevent 'security.evm' from being modified
 */
int evm_inode_setxattr(struct dentry *dentry, const char *xattr_name,
		       const void *xattr_value, size_t xattr_value_len)
{
	return evm_protect_xattr(dentry, xattr_name, xattr_value,
				 xattr_value_len);
}

/**
 * evm_inode_removexattr - protect the EVM extended attribute
 * @dentry: pointer to the affected dentry
 * @xattr_name: pointer to the affected extended attribute name
 *
 * Prevent 'security.evm' from being removed.
 */
int evm_inode_removexattr(struct dentry *dentry, const char *xattr_name)
{
	return evm_protect_xattr(dentry, xattr_name, NULL, 0);
}

/**
 * evm_inode_post_setxattr - update 'security.evm' to reflect the changes
 * @dentry: pointer to the affected dentry
 * @xattr_name: pointer to the affected extended attribute name
 * @xattr_value: pointer to the new extended attribute value
 * @xattr_value_len: pointer to the new extended attribute value length
 *
 * Update the HMAC stored in 'security.evm' to reflect the change.
 *
 * No need to take the i_mutex lock here, as this function is called from
 * __vfs_setxattr_noperm().  The caller of which has taken the inode's
 * i_mutex lock.
 */
void evm_inode_post_setxattr(struct dentry *dentry, const char *xattr_name,
			     const void *xattr_value, size_t xattr_value_len)
{
	if (!evm_initialized || !evm_protected_xattr(xattr_name))
		return;

	evm_update_evmxattr(dentry, xattr_name, xattr_value, xattr_value_len);
	return;
}

/**
 * evm_inode_post_removexattr - update 'security.evm' after removing the xattr
 * @dentry: pointer to the affected dentry
 * @xattr_name: pointer to the affected extended attribute name
 *
 * Update the HMAC stored in 'security.evm' to reflect removal of the xattr.
 */
void evm_inode_post_removexattr(struct dentry *dentry, const char *xattr_name)
{
	struct inode *inode = dentry->d_inode;

	if (!evm_initialized || !evm_protected_xattr(xattr_name))
		return;

	mutex_lock(&inode->i_mutex);
	evm_update_evmxattr(dentry, xattr_name, NULL, 0);
	mutex_unlock(&inode->i_mutex);
	return;
}

/**
 * evm_inode_post_setattr - update 'security.evm' after modifying metadata
 * @dentry: pointer to the affected dentry
 * @ia_valid: for the UID and GID status
 *
 * For now, update the HMAC stored in 'security.evm' to reflect UID/GID
 * changes.
 *
 * This function is called from notify_change(), which expects the caller
 * to lock the inode's i_mutex.
 */
void evm_inode_post_setattr(struct dentry *dentry, int ia_valid)
{
	if (!evm_initialized)
		return;

	if (ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID))
		evm_update_evmxattr(dentry, NULL, NULL, 0);
	return;
}

static struct crypto_hash *tfm_hmac;	/* preload crypto alg */
static int __init init_evm(void)
{
	int error;

	tfm_hmac = crypto_alloc_hash(evm_hmac, 0, CRYPTO_ALG_ASYNC);
	error = evm_init_secfs();
	if (error < 0) {
		printk(KERN_INFO "EVM: Error registering secfs\n");
		goto err;
	}
err:
	return error;
}

static void __exit cleanup_evm(void)
{
	evm_cleanup_secfs();
	crypto_free_hash(tfm_hmac);
}

/*
 * evm_display_config - list the EVM protected security extended attributes
 */
static int __init evm_display_config(void)
{
	char **xattrname;

	for (xattrname = evm_config_xattrnames; *xattrname != NULL; xattrname++)
		printk(KERN_INFO "EVM: %s\n", *xattrname);
	return 0;
}

pure_initcall(evm_display_config);
late_initcall(init_evm);

MODULE_DESCRIPTION("Extended Verification Module");
MODULE_LICENSE("GPL");
