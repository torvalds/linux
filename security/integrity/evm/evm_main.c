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

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/audit.h>
#include <linux/xattr.h>
#include <linux/integrity.h>
#include <linux/evm.h>
#include <crypto/hash.h>
#include <crypto/algapi.h>
#include "evm.h"

int evm_initialized;

static char *integrity_status_msg[] = {
	"pass", "fail", "no_label", "no_xattrs", "unknown"
};
char *evm_hmac = "hmac(sha1)";
char *evm_hash = "sha1";
int evm_hmac_attrs;

char *evm_config_xattrnames[] = {
#ifdef CONFIG_SECURITY_SELINUX
	XATTR_NAME_SELINUX,
#endif
#ifdef CONFIG_SECURITY_SMACK
	XATTR_NAME_SMACK,
#ifdef CONFIG_EVM_EXTRA_SMACK_XATTRS
	XATTR_NAME_SMACKEXEC,
	XATTR_NAME_SMACKTRANSMUTE,
	XATTR_NAME_SMACKMMAP,
#endif
#endif
#ifdef CONFIG_IMA_APPRAISE
	XATTR_NAME_IMA,
#endif
	XATTR_NAME_CAPS,
	NULL
};

static int evm_fixmode;
static int __init evm_set_fixmode(char *str)
{
	if (strncmp(str, "fix", 3) == 0)
		evm_fixmode = 1;
	return 0;
}
__setup("evm=", evm_set_fixmode);

static void __init evm_init_config(void)
{
#ifdef CONFIG_EVM_ATTR_FSUUID
	evm_hmac_attrs |= EVM_ATTR_FSUUID;
#endif
	pr_info("HMAC attrs: 0x%x\n", evm_hmac_attrs);
}

static int evm_find_protected_xattrs(struct dentry *dentry)
{
	struct inode *inode = d_backing_inode(dentry);
	char **xattr;
	int error;
	int count = 0;

	if (!(inode->i_opflags & IOP_XATTR))
		return -EOPNOTSUPP;

	for (xattr = evm_config_xattrnames; *xattr != NULL; xattr++) {
		error = __vfs_getxattr(dentry, inode, *xattr, NULL, 0);
		if (error < 0) {
			if (error == -ENODATA)
				continue;
			return error;
		}
		count++;
	}

	return count;
}

/*
 * evm_verify_hmac - calculate and compare the HMAC with the EVM xattr
 *
 * Compute the HMAC on the dentry's protected set of extended attributes
 * and compare it against the stored security.evm xattr.
 *
 * For performance:
 * - use the previoulsy retrieved xattr value and length to calculate the
 *   HMAC.)
 * - cache the verification result in the iint, when available.
 *
 * Returns integrity status
 */
static enum integrity_status evm_verify_hmac(struct dentry *dentry,
					     const char *xattr_name,
					     char *xattr_value,
					     size_t xattr_value_len,
					     struct integrity_iint_cache *iint)
{
	struct evm_ima_xattr_data *xattr_data = NULL;
	struct evm_ima_xattr_data calc;
	enum integrity_status evm_status = INTEGRITY_PASS;
	int rc, xattr_len;

	if (iint && iint->evm_status == INTEGRITY_PASS)
		return iint->evm_status;

	/* if status is not PASS, try to check again - against -ENOMEM */

	/* first need to know the sig type */
	rc = vfs_getxattr_alloc(dentry, XATTR_NAME_EVM, (char **)&xattr_data, 0,
				GFP_NOFS);
	if (rc <= 0) {
		evm_status = INTEGRITY_FAIL;
		if (rc == -ENODATA) {
			rc = evm_find_protected_xattrs(dentry);
			if (rc > 0)
				evm_status = INTEGRITY_NOLABEL;
			else if (rc == 0)
				evm_status = INTEGRITY_NOXATTRS; /* new file */
		} else if (rc == -EOPNOTSUPP) {
			evm_status = INTEGRITY_UNKNOWN;
		}
		goto out;
	}

	xattr_len = rc;

	/* check value type */
	switch (xattr_data->type) {
	case EVM_XATTR_HMAC:
		rc = evm_calc_hmac(dentry, xattr_name, xattr_value,
				   xattr_value_len, calc.digest);
		if (rc)
			break;
		rc = crypto_memneq(xattr_data->digest, calc.digest,
			    sizeof(calc.digest));
		if (rc)
			rc = -EINVAL;
		break;
	case EVM_IMA_XATTR_DIGSIG:
		rc = evm_calc_hash(dentry, xattr_name, xattr_value,
				xattr_value_len, calc.digest);
		if (rc)
			break;
		rc = integrity_digsig_verify(INTEGRITY_KEYRING_EVM,
					(const char *)xattr_data, xattr_len,
					calc.digest, sizeof(calc.digest));
		if (!rc) {
			/* Replace RSA with HMAC if not mounted readonly and
			 * not immutable
			 */
			if (!IS_RDONLY(d_backing_inode(dentry)) &&
			    !IS_IMMUTABLE(d_backing_inode(dentry)))
				evm_update_evmxattr(dentry, xattr_name,
						    xattr_value,
						    xattr_value_len);
		}
		break;
	default:
		rc = -EINVAL;
		break;
	}

	if (rc)
		evm_status = (rc == -ENODATA) ?
				INTEGRITY_NOXATTRS : INTEGRITY_FAIL;
out:
	if (iint)
		iint->evm_status = evm_status;
	kfree(xattr_data);
	return evm_status;
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
		if (strncmp(req_xattr_name,
			    *xattrname + XATTR_SECURITY_PREFIX_LEN,
			    strlen(req_xattr_name)) == 0) {
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
				      void *xattr_value, size_t xattr_value_len,
				      struct integrity_iint_cache *iint)
{
	if (!evm_initialized || !evm_protected_xattr(xattr_name))
		return INTEGRITY_UNKNOWN;

	if (!iint) {
		iint = integrity_iint_find(d_backing_inode(dentry));
		if (!iint)
			return INTEGRITY_UNKNOWN;
	}
	return evm_verify_hmac(dentry, xattr_name, xattr_value,
				 xattr_value_len, iint);
}
EXPORT_SYMBOL_GPL(evm_verifyxattr);

/*
 * evm_verify_current_integrity - verify the dentry's metadata integrity
 * @dentry: pointer to the affected dentry
 *
 * Verify and return the dentry's metadata integrity. The exceptions are
 * before EVM is initialized or in 'fix' mode.
 */
static enum integrity_status evm_verify_current_integrity(struct dentry *dentry)
{
	struct inode *inode = d_backing_inode(dentry);

	if (!evm_initialized || !S_ISREG(inode->i_mode) || evm_fixmode)
		return 0;
	return evm_verify_hmac(dentry, NULL, NULL, 0, NULL);
}

/*
 * evm_protect_xattr - protect the EVM extended attribute
 *
 * Prevent security.evm from being modified or removed without the
 * necessary permissions or when the existing value is invalid.
 *
 * The posix xattr acls are 'system' prefixed, which normally would not
 * affect security.evm.  An interesting side affect of writing posix xattr
 * acls is their modifying of the i_mode, which is included in security.evm.
 * For posix xattr acls only, permit security.evm, even if it currently
 * doesn't exist, to be updated.
 */
static int evm_protect_xattr(struct dentry *dentry, const char *xattr_name,
			     const void *xattr_value, size_t xattr_value_len)
{
	enum integrity_status evm_status;

	if (strcmp(xattr_name, XATTR_NAME_EVM) == 0) {
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
	} else if (!evm_protected_xattr(xattr_name)) {
		if (!posix_xattr_acl(xattr_name))
			return 0;
		evm_status = evm_verify_current_integrity(dentry);
		if ((evm_status == INTEGRITY_PASS) ||
		    (evm_status == INTEGRITY_NOXATTRS))
			return 0;
		goto out;
	}
	evm_status = evm_verify_current_integrity(dentry);
	if (evm_status == INTEGRITY_NOXATTRS) {
		struct integrity_iint_cache *iint;

		iint = integrity_iint_find(d_backing_inode(dentry));
		if (iint && (iint->flags & IMA_NEW_FILE))
			return 0;

		/* exception for pseudo filesystems */
		if (dentry->d_sb->s_magic == TMPFS_MAGIC
		    || dentry->d_sb->s_magic == SYSFS_MAGIC)
			return 0;

		integrity_audit_msg(AUDIT_INTEGRITY_METADATA,
				    dentry->d_inode, dentry->d_name.name,
				    "update_metadata",
				    integrity_status_msg[evm_status],
				    -EPERM, 0);
	}
out:
	if (evm_status != INTEGRITY_PASS)
		integrity_audit_msg(AUDIT_INTEGRITY_METADATA, d_backing_inode(dentry),
				    dentry->d_name.name, "appraise_metadata",
				    integrity_status_msg[evm_status],
				    -EPERM, 0);
	return evm_status == INTEGRITY_PASS ? 0 : -EPERM;
}

/**
 * evm_inode_setxattr - protect the EVM extended attribute
 * @dentry: pointer to the affected dentry
 * @xattr_name: pointer to the affected extended attribute name
 * @xattr_value: pointer to the new extended attribute value
 * @xattr_value_len: pointer to the new extended attribute value length
 *
 * Before allowing the 'security.evm' protected xattr to be updated,
 * verify the existing value is valid.  As only the kernel should have
 * access to the EVM encrypted key needed to calculate the HMAC, prevent
 * userspace from writing HMAC value.  Writing 'security.evm' requires
 * requires CAP_SYS_ADMIN privileges.
 */
int evm_inode_setxattr(struct dentry *dentry, const char *xattr_name,
		       const void *xattr_value, size_t xattr_value_len)
{
	const struct evm_ima_xattr_data *xattr_data = xattr_value;

	if (strcmp(xattr_name, XATTR_NAME_EVM) == 0) {
		if (!xattr_value_len)
			return -EINVAL;
		if (xattr_data->type != EVM_IMA_XATTR_DIGSIG)
			return -EPERM;
	}
	return evm_protect_xattr(dentry, xattr_name, xattr_value,
				 xattr_value_len);
}

/**
 * evm_inode_removexattr - protect the EVM extended attribute
 * @dentry: pointer to the affected dentry
 * @xattr_name: pointer to the affected extended attribute name
 *
 * Removing 'security.evm' requires CAP_SYS_ADMIN privileges and that
 * the current value is valid.
 */
int evm_inode_removexattr(struct dentry *dentry, const char *xattr_name)
{
	return evm_protect_xattr(dentry, xattr_name, NULL, 0);
}

static void evm_reset_status(struct inode *inode)
{
	struct integrity_iint_cache *iint;

	iint = integrity_iint_find(inode);
	if (iint)
		iint->evm_status = INTEGRITY_UNKNOWN;
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
	if (!evm_initialized || (!evm_protected_xattr(xattr_name)
				 && !posix_xattr_acl(xattr_name)))
		return;

	evm_reset_status(dentry->d_inode);

	evm_update_evmxattr(dentry, xattr_name, xattr_value, xattr_value_len);
}

/**
 * evm_inode_post_removexattr - update 'security.evm' after removing the xattr
 * @dentry: pointer to the affected dentry
 * @xattr_name: pointer to the affected extended attribute name
 *
 * Update the HMAC stored in 'security.evm' to reflect removal of the xattr.
 *
 * No need to take the i_mutex lock here, as this function is called from
 * vfs_removexattr() which takes the i_mutex.
 */
void evm_inode_post_removexattr(struct dentry *dentry, const char *xattr_name)
{
	if (!evm_initialized || !evm_protected_xattr(xattr_name))
		return;

	evm_reset_status(dentry->d_inode);

	evm_update_evmxattr(dentry, xattr_name, NULL, 0);
}

/**
 * evm_inode_setattr - prevent updating an invalid EVM extended attribute
 * @dentry: pointer to the affected dentry
 */
int evm_inode_setattr(struct dentry *dentry, struct iattr *attr)
{
	unsigned int ia_valid = attr->ia_valid;
	enum integrity_status evm_status;

	if (!(ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID)))
		return 0;
	evm_status = evm_verify_current_integrity(dentry);
	if ((evm_status == INTEGRITY_PASS) ||
	    (evm_status == INTEGRITY_NOXATTRS))
		return 0;
	integrity_audit_msg(AUDIT_INTEGRITY_METADATA, d_backing_inode(dentry),
			    dentry->d_name.name, "appraise_metadata",
			    integrity_status_msg[evm_status], -EPERM, 0);
	return -EPERM;
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
}

/*
 * evm_inode_init_security - initializes security.evm
 */
int evm_inode_init_security(struct inode *inode,
				 const struct xattr *lsm_xattr,
				 struct xattr *evm_xattr)
{
	struct evm_ima_xattr_data *xattr_data;
	int rc;

	if (!evm_initialized || !evm_protected_xattr(lsm_xattr->name))
		return 0;

	xattr_data = kzalloc(sizeof(*xattr_data), GFP_NOFS);
	if (!xattr_data)
		return -ENOMEM;

	xattr_data->type = EVM_XATTR_HMAC;
	rc = evm_init_hmac(inode, lsm_xattr, xattr_data->digest);
	if (rc < 0)
		goto out;

	evm_xattr->value = xattr_data;
	evm_xattr->value_len = sizeof(*xattr_data);
	evm_xattr->name = XATTR_EVM_SUFFIX;
	return 0;
out:
	kfree(xattr_data);
	return rc;
}
EXPORT_SYMBOL_GPL(evm_inode_init_security);

#ifdef CONFIG_EVM_LOAD_X509
void __init evm_load_x509(void)
{
	int rc;

	rc = integrity_load_x509(INTEGRITY_KEYRING_EVM, CONFIG_EVM_X509_PATH);
	if (!rc)
		evm_initialized |= EVM_INIT_X509;
}
#endif

static int __init init_evm(void)
{
	int error;

	evm_init_config();

	error = integrity_init_keyring(INTEGRITY_KEYRING_EVM);
	if (error)
		return error;

	error = evm_init_secfs();
	if (error < 0) {
		pr_info("Error registering secfs\n");
		return error;
	}

	return 0;
}

/*
 * evm_display_config - list the EVM protected security extended attributes
 */
static int __init evm_display_config(void)
{
	char **xattrname;

	for (xattrname = evm_config_xattrnames; *xattrname != NULL; xattrname++)
		pr_info("%s\n", *xattrname);
	return 0;
}

pure_initcall(evm_display_config);
late_initcall(init_evm);

MODULE_DESCRIPTION("Extended Verification Module");
MODULE_LICENSE("GPL");
