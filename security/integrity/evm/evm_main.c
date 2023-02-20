// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2005-2010 IBM Corporation
 *
 * Author:
 * Mimi Zohar <zohar@us.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * File: evm_main.c
 *	implements evm_inode_setxattr, evm_inode_post_setxattr,
 *	evm_inode_removexattr, evm_verifyxattr, and evm_inode_set_acl.
 */

#define pr_fmt(fmt) "EVM: "fmt

#include <linux/init.h>
#include <linux/crypto.h>
#include <linux/audit.h>
#include <linux/xattr.h>
#include <linux/integrity.h>
#include <linux/evm.h>
#include <linux/magic.h>
#include <linux/posix_acl_xattr.h>

#include <crypto/hash.h>
#include <crypto/hash_info.h>
#include <crypto/algapi.h>
#include "evm.h"

int evm_initialized;

static const char * const integrity_status_msg[] = {
	"pass", "pass_immutable", "fail", "fail_immutable", "no_label",
	"no_xattrs", "unknown"
};
int evm_hmac_attrs;

static struct xattr_list evm_config_default_xattrnames[] = {
	{
	 .name = XATTR_NAME_SELINUX,
	 .enabled = IS_ENABLED(CONFIG_SECURITY_SELINUX)
	},
	{
	 .name = XATTR_NAME_SMACK,
	 .enabled = IS_ENABLED(CONFIG_SECURITY_SMACK)
	},
	{
	 .name = XATTR_NAME_SMACKEXEC,
	 .enabled = IS_ENABLED(CONFIG_EVM_EXTRA_SMACK_XATTRS)
	},
	{
	 .name = XATTR_NAME_SMACKTRANSMUTE,
	 .enabled = IS_ENABLED(CONFIG_EVM_EXTRA_SMACK_XATTRS)
	},
	{
	 .name = XATTR_NAME_SMACKMMAP,
	 .enabled = IS_ENABLED(CONFIG_EVM_EXTRA_SMACK_XATTRS)
	},
	{
	 .name = XATTR_NAME_APPARMOR,
	 .enabled = IS_ENABLED(CONFIG_SECURITY_APPARMOR)
	},
	{
	 .name = XATTR_NAME_IMA,
	 .enabled = IS_ENABLED(CONFIG_IMA_APPRAISE)
	},
	{
	 .name = XATTR_NAME_CAPS,
	 .enabled = true
	},
};

LIST_HEAD(evm_config_xattrnames);

static int evm_fixmode __ro_after_init;
static int __init evm_set_fixmode(char *str)
{
	if (strncmp(str, "fix", 3) == 0)
		evm_fixmode = 1;
	else
		pr_err("invalid \"%s\" mode", str);

	return 1;
}
__setup("evm=", evm_set_fixmode);

static void __init evm_init_config(void)
{
	int i, xattrs;

	xattrs = ARRAY_SIZE(evm_config_default_xattrnames);

	pr_info("Initialising EVM extended attributes:\n");
	for (i = 0; i < xattrs; i++) {
		pr_info("%s%s\n", evm_config_default_xattrnames[i].name,
			!evm_config_default_xattrnames[i].enabled ?
			" (disabled)" : "");
		list_add_tail(&evm_config_default_xattrnames[i].list,
			      &evm_config_xattrnames);
	}

#ifdef CONFIG_EVM_ATTR_FSUUID
	evm_hmac_attrs |= EVM_ATTR_FSUUID;
#endif
	pr_info("HMAC attrs: 0x%x\n", evm_hmac_attrs);
}

static bool evm_key_loaded(void)
{
	return (bool)(evm_initialized & EVM_KEY_MASK);
}

/*
 * This function determines whether or not it is safe to ignore verification
 * errors, based on the ability of EVM to calculate HMACs. If the HMAC key
 * is not loaded, and it cannot be loaded in the future due to the
 * EVM_SETUP_COMPLETE initialization flag, allowing an operation despite the
 * attrs/xattrs being found invalid will not make them valid.
 */
static bool evm_hmac_disabled(void)
{
	if (evm_initialized & EVM_INIT_HMAC)
		return false;

	if (!(evm_initialized & EVM_SETUP_COMPLETE))
		return false;

	return true;
}

static int evm_find_protected_xattrs(struct dentry *dentry)
{
	struct inode *inode = d_backing_inode(dentry);
	struct xattr_list *xattr;
	int error;
	int count = 0;

	if (!(inode->i_opflags & IOP_XATTR))
		return -EOPNOTSUPP;

	list_for_each_entry_lockless(xattr, &evm_config_xattrnames, list) {
		error = __vfs_getxattr(dentry, inode, xattr->name, NULL, 0);
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
	struct signature_v2_hdr *hdr;
	enum integrity_status evm_status = INTEGRITY_PASS;
	struct evm_digest digest;
	struct inode *inode;
	int rc, xattr_len, evm_immutable = 0;

	if (iint && (iint->evm_status == INTEGRITY_PASS ||
		     iint->evm_status == INTEGRITY_PASS_IMMUTABLE))
		return iint->evm_status;

	/* if status is not PASS, try to check again - against -ENOMEM */

	/* first need to know the sig type */
	rc = vfs_getxattr_alloc(&nop_mnt_idmap, dentry, XATTR_NAME_EVM,
				(char **)&xattr_data, 0, GFP_NOFS);
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
		if (xattr_len != sizeof(struct evm_xattr)) {
			evm_status = INTEGRITY_FAIL;
			goto out;
		}

		digest.hdr.algo = HASH_ALGO_SHA1;
		rc = evm_calc_hmac(dentry, xattr_name, xattr_value,
				   xattr_value_len, &digest);
		if (rc)
			break;
		rc = crypto_memneq(xattr_data->data, digest.digest,
				   SHA1_DIGEST_SIZE);
		if (rc)
			rc = -EINVAL;
		break;
	case EVM_XATTR_PORTABLE_DIGSIG:
		evm_immutable = 1;
		fallthrough;
	case EVM_IMA_XATTR_DIGSIG:
		/* accept xattr with non-empty signature field */
		if (xattr_len <= sizeof(struct signature_v2_hdr)) {
			evm_status = INTEGRITY_FAIL;
			goto out;
		}

		hdr = (struct signature_v2_hdr *)xattr_data;
		digest.hdr.algo = hdr->hash_algo;
		rc = evm_calc_hash(dentry, xattr_name, xattr_value,
				   xattr_value_len, xattr_data->type, &digest);
		if (rc)
			break;
		rc = integrity_digsig_verify(INTEGRITY_KEYRING_EVM,
					(const char *)xattr_data, xattr_len,
					digest.digest, digest.hdr.length);
		if (!rc) {
			inode = d_backing_inode(dentry);

			if (xattr_data->type == EVM_XATTR_PORTABLE_DIGSIG) {
				if (iint)
					iint->flags |= EVM_IMMUTABLE_DIGSIG;
				evm_status = INTEGRITY_PASS_IMMUTABLE;
			} else if (!IS_RDONLY(inode) &&
				   !(inode->i_sb->s_readonly_remount) &&
				   !IS_IMMUTABLE(inode)) {
				evm_update_evmxattr(dentry, xattr_name,
						    xattr_value,
						    xattr_value_len);
			}
		}
		break;
	default:
		rc = -EINVAL;
		break;
	}

	if (rc) {
		if (rc == -ENODATA)
			evm_status = INTEGRITY_NOXATTRS;
		else if (evm_immutable)
			evm_status = INTEGRITY_FAIL_IMMUTABLE;
		else
			evm_status = INTEGRITY_FAIL;
	}
	pr_debug("digest: (%d) [%*phN]\n", digest.hdr.length, digest.hdr.length,
		  digest.digest);
out:
	if (iint)
		iint->evm_status = evm_status;
	kfree(xattr_data);
	return evm_status;
}

static int evm_protected_xattr_common(const char *req_xattr_name,
				      bool all_xattrs)
{
	int namelen;
	int found = 0;
	struct xattr_list *xattr;

	namelen = strlen(req_xattr_name);
	list_for_each_entry_lockless(xattr, &evm_config_xattrnames, list) {
		if (!all_xattrs && !xattr->enabled)
			continue;

		if ((strlen(xattr->name) == namelen)
		    && (strncmp(req_xattr_name, xattr->name, namelen) == 0)) {
			found = 1;
			break;
		}
		if (strncmp(req_xattr_name,
			    xattr->name + XATTR_SECURITY_PREFIX_LEN,
			    strlen(req_xattr_name)) == 0) {
			found = 1;
			break;
		}
	}

	return found;
}

static int evm_protected_xattr(const char *req_xattr_name)
{
	return evm_protected_xattr_common(req_xattr_name, false);
}

int evm_protected_xattr_if_enabled(const char *req_xattr_name)
{
	return evm_protected_xattr_common(req_xattr_name, true);
}

/**
 * evm_read_protected_xattrs - read EVM protected xattr names, lengths, values
 * @dentry: dentry of the read xattrs
 * @inode: inode of the read xattrs
 * @buffer: buffer xattr names, lengths or values are copied to
 * @buffer_size: size of buffer
 * @type: n: names, l: lengths, v: values
 * @canonical_fmt: data format (true: little endian, false: native format)
 *
 * Read protected xattr names (separated by |), lengths (u32) or values for a
 * given dentry and return the total size of copied data. If buffer is NULL,
 * just return the total size.
 *
 * Returns the total size on success, a negative value on error.
 */
int evm_read_protected_xattrs(struct dentry *dentry, u8 *buffer,
			      int buffer_size, char type, bool canonical_fmt)
{
	struct xattr_list *xattr;
	int rc, size, total_size = 0;

	list_for_each_entry_lockless(xattr, &evm_config_xattrnames, list) {
		rc = __vfs_getxattr(dentry, d_backing_inode(dentry),
				    xattr->name, NULL, 0);
		if (rc < 0 && rc == -ENODATA)
			continue;
		else if (rc < 0)
			return rc;

		switch (type) {
		case 'n':
			size = strlen(xattr->name) + 1;
			if (buffer) {
				if (total_size)
					*(buffer + total_size - 1) = '|';

				memcpy(buffer + total_size, xattr->name, size);
			}
			break;
		case 'l':
			size = sizeof(u32);
			if (buffer) {
				if (canonical_fmt)
					rc = (__force int)cpu_to_le32(rc);

				*(u32 *)(buffer + total_size) = rc;
			}
			break;
		case 'v':
			size = rc;
			if (buffer) {
				rc = __vfs_getxattr(dentry,
					d_backing_inode(dentry), xattr->name,
					buffer + total_size,
					buffer_size - total_size);
				if (rc < 0)
					return rc;
			}
			break;
		default:
			return -EINVAL;
		}

		total_size += size;
	}

	return total_size;
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
	if (!evm_key_loaded() || !evm_protected_xattr(xattr_name))
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

	if (!evm_key_loaded() || !S_ISREG(inode->i_mode) || evm_fixmode)
		return INTEGRITY_PASS;
	return evm_verify_hmac(dentry, NULL, NULL, 0, NULL);
}

/*
 * evm_xattr_change - check if passed xattr value differs from current value
 * @idmap: idmap of the mount
 * @dentry: pointer to the affected dentry
 * @xattr_name: requested xattr
 * @xattr_value: requested xattr value
 * @xattr_value_len: requested xattr value length
 *
 * Check if passed xattr value differs from current value.
 *
 * Returns 1 if passed xattr value differs from current value, 0 otherwise.
 */
static int evm_xattr_change(struct mnt_idmap *idmap,
			    struct dentry *dentry, const char *xattr_name,
			    const void *xattr_value, size_t xattr_value_len)
{
	char *xattr_data = NULL;
	int rc = 0;

	rc = vfs_getxattr_alloc(&nop_mnt_idmap, dentry, xattr_name, &xattr_data,
				0, GFP_NOFS);
	if (rc < 0) {
		rc = 1;
		goto out;
	}

	if (rc == xattr_value_len)
		rc = !!memcmp(xattr_value, xattr_data, rc);
	else
		rc = 1;

out:
	kfree(xattr_data);
	return rc;
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
 * doesn't exist, to be updated unless the EVM signature is immutable.
 */
static int evm_protect_xattr(struct mnt_idmap *idmap,
			     struct dentry *dentry, const char *xattr_name,
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

		/* Exception if the HMAC is not going to be calculated. */
		if (evm_hmac_disabled())
			return 0;

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
	/* Exception if the HMAC is not going to be calculated. */
	if (evm_hmac_disabled() && (evm_status == INTEGRITY_NOLABEL ||
	    evm_status == INTEGRITY_UNKNOWN))
		return 0;

	/*
	 * Writing other xattrs is safe for portable signatures, as portable
	 * signatures are immutable and can never be updated.
	 */
	if (evm_status == INTEGRITY_FAIL_IMMUTABLE)
		return 0;

	if (evm_status == INTEGRITY_PASS_IMMUTABLE &&
	    !evm_xattr_change(idmap, dentry, xattr_name, xattr_value,
			      xattr_value_len))
		return 0;

	if (evm_status != INTEGRITY_PASS &&
	    evm_status != INTEGRITY_PASS_IMMUTABLE)
		integrity_audit_msg(AUDIT_INTEGRITY_METADATA, d_backing_inode(dentry),
				    dentry->d_name.name, "appraise_metadata",
				    integrity_status_msg[evm_status],
				    -EPERM, 0);
	return evm_status == INTEGRITY_PASS ? 0 : -EPERM;
}

/**
 * evm_inode_setxattr - protect the EVM extended attribute
 * @idmap: idmap of the mount
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
int evm_inode_setxattr(struct mnt_idmap *idmap, struct dentry *dentry,
		       const char *xattr_name, const void *xattr_value,
		       size_t xattr_value_len)
{
	const struct evm_ima_xattr_data *xattr_data = xattr_value;

	/* Policy permits modification of the protected xattrs even though
	 * there's no HMAC key loaded
	 */
	if (evm_initialized & EVM_ALLOW_METADATA_WRITES)
		return 0;

	if (strcmp(xattr_name, XATTR_NAME_EVM) == 0) {
		if (!xattr_value_len)
			return -EINVAL;
		if (xattr_data->type != EVM_IMA_XATTR_DIGSIG &&
		    xattr_data->type != EVM_XATTR_PORTABLE_DIGSIG)
			return -EPERM;
	}
	return evm_protect_xattr(idmap, dentry, xattr_name, xattr_value,
				 xattr_value_len);
}

/**
 * evm_inode_removexattr - protect the EVM extended attribute
 * @idmap: idmap of the mount
 * @dentry: pointer to the affected dentry
 * @xattr_name: pointer to the affected extended attribute name
 *
 * Removing 'security.evm' requires CAP_SYS_ADMIN privileges and that
 * the current value is valid.
 */
int evm_inode_removexattr(struct mnt_idmap *idmap,
			  struct dentry *dentry, const char *xattr_name)
{
	/* Policy permits modification of the protected xattrs even though
	 * there's no HMAC key loaded
	 */
	if (evm_initialized & EVM_ALLOW_METADATA_WRITES)
		return 0;

	return evm_protect_xattr(idmap, dentry, xattr_name, NULL, 0);
}

#ifdef CONFIG_FS_POSIX_ACL
static int evm_inode_set_acl_change(struct mnt_idmap *idmap,
				    struct dentry *dentry, const char *name,
				    struct posix_acl *kacl)
{
	int rc;

	umode_t mode;
	struct inode *inode = d_backing_inode(dentry);

	if (!kacl)
		return 1;

	rc = posix_acl_update_mode(idmap, inode, &mode, &kacl);
	if (rc || (inode->i_mode != mode))
		return 1;

	return 0;
}
#else
static inline int evm_inode_set_acl_change(struct mnt_idmap *idmap,
					   struct dentry *dentry,
					   const char *name,
					   struct posix_acl *kacl)
{
	return 0;
}
#endif

/**
 * evm_inode_set_acl - protect the EVM extended attribute from posix acls
 * @idmap: idmap of the idmapped mount
 * @dentry: pointer to the affected dentry
 * @acl_name: name of the posix acl
 * @kacl: pointer to the posix acls
 *
 * Prevent modifying posix acls causing the EVM HMAC to be re-calculated
 * and 'security.evm' xattr updated, unless the existing 'security.evm' is
 * valid.
 */
int evm_inode_set_acl(struct mnt_idmap *idmap, struct dentry *dentry,
		      const char *acl_name, struct posix_acl *kacl)
{
	enum integrity_status evm_status;

	/* Policy permits modification of the protected xattrs even though
	 * there's no HMAC key loaded
	 */
	if (evm_initialized & EVM_ALLOW_METADATA_WRITES)
		return 0;

	evm_status = evm_verify_current_integrity(dentry);
	if ((evm_status == INTEGRITY_PASS) ||
	    (evm_status == INTEGRITY_NOXATTRS))
		return 0;

	/* Exception if the HMAC is not going to be calculated. */
	if (evm_hmac_disabled() && (evm_status == INTEGRITY_NOLABEL ||
	    evm_status == INTEGRITY_UNKNOWN))
		return 0;

	/*
	 * Writing other xattrs is safe for portable signatures, as portable
	 * signatures are immutable and can never be updated.
	 */
	if (evm_status == INTEGRITY_FAIL_IMMUTABLE)
		return 0;

	if (evm_status == INTEGRITY_PASS_IMMUTABLE &&
	    !evm_inode_set_acl_change(idmap, dentry, acl_name, kacl))
		return 0;

	if (evm_status != INTEGRITY_PASS_IMMUTABLE)
		integrity_audit_msg(AUDIT_INTEGRITY_METADATA, d_backing_inode(dentry),
				    dentry->d_name.name, "appraise_metadata",
				    integrity_status_msg[evm_status],
				    -EPERM, 0);
	return -EPERM;
}

static void evm_reset_status(struct inode *inode)
{
	struct integrity_iint_cache *iint;

	iint = integrity_iint_find(inode);
	if (iint)
		iint->evm_status = INTEGRITY_UNKNOWN;
}

/**
 * evm_revalidate_status - report whether EVM status re-validation is necessary
 * @xattr_name: pointer to the affected extended attribute name
 *
 * Report whether callers of evm_verifyxattr() should re-validate the
 * EVM status.
 *
 * Return true if re-validation is necessary, false otherwise.
 */
bool evm_revalidate_status(const char *xattr_name)
{
	if (!evm_key_loaded())
		return false;

	/* evm_inode_post_setattr() passes NULL */
	if (!xattr_name)
		return true;

	if (!evm_protected_xattr(xattr_name) && !posix_xattr_acl(xattr_name) &&
	    strcmp(xattr_name, XATTR_NAME_EVM))
		return false;

	return true;
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
	if (!evm_revalidate_status(xattr_name))
		return;

	evm_reset_status(dentry->d_inode);

	if (!strcmp(xattr_name, XATTR_NAME_EVM))
		return;

	if (!(evm_initialized & EVM_INIT_HMAC))
		return;

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
	if (!evm_revalidate_status(xattr_name))
		return;

	evm_reset_status(dentry->d_inode);

	if (!strcmp(xattr_name, XATTR_NAME_EVM))
		return;

	if (!(evm_initialized & EVM_INIT_HMAC))
		return;

	evm_update_evmxattr(dentry, xattr_name, NULL, 0);
}

static int evm_attr_change(struct mnt_idmap *idmap,
			   struct dentry *dentry, struct iattr *attr)
{
	struct inode *inode = d_backing_inode(dentry);
	unsigned int ia_valid = attr->ia_valid;

	if (!i_uid_needs_update(idmap, attr, inode) &&
	    !i_gid_needs_update(idmap, attr, inode) &&
	    (!(ia_valid & ATTR_MODE) || attr->ia_mode == inode->i_mode))
		return 0;

	return 1;
}

/**
 * evm_inode_setattr - prevent updating an invalid EVM extended attribute
 * @dentry: pointer to the affected dentry
 *
 * Permit update of file attributes when files have a valid EVM signature,
 * except in the case of them having an immutable portable signature.
 */
int evm_inode_setattr(struct mnt_idmap *idmap, struct dentry *dentry,
		      struct iattr *attr)
{
	unsigned int ia_valid = attr->ia_valid;
	enum integrity_status evm_status;

	/* Policy permits modification of the protected attrs even though
	 * there's no HMAC key loaded
	 */
	if (evm_initialized & EVM_ALLOW_METADATA_WRITES)
		return 0;

	if (!(ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID)))
		return 0;
	evm_status = evm_verify_current_integrity(dentry);
	/*
	 * Writing attrs is safe for portable signatures, as portable signatures
	 * are immutable and can never be updated.
	 */
	if ((evm_status == INTEGRITY_PASS) ||
	    (evm_status == INTEGRITY_NOXATTRS) ||
	    (evm_status == INTEGRITY_FAIL_IMMUTABLE) ||
	    (evm_hmac_disabled() && (evm_status == INTEGRITY_NOLABEL ||
	     evm_status == INTEGRITY_UNKNOWN)))
		return 0;

	if (evm_status == INTEGRITY_PASS_IMMUTABLE &&
	    !evm_attr_change(idmap, dentry, attr))
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
	if (!evm_revalidate_status(NULL))
		return;

	evm_reset_status(dentry->d_inode);

	if (!(evm_initialized & EVM_INIT_HMAC))
		return;

	if (ia_valid & (ATTR_MODE | ATTR_UID | ATTR_GID))
		evm_update_evmxattr(dentry, NULL, NULL, 0);
}

/*
 * evm_inode_init_security - initializes security.evm HMAC value
 */
int evm_inode_init_security(struct inode *inode,
				 const struct xattr *lsm_xattr,
				 struct xattr *evm_xattr)
{
	struct evm_xattr *xattr_data;
	int rc;

	if (!(evm_initialized & EVM_INIT_HMAC) ||
	    !evm_protected_xattr(lsm_xattr->name))
		return 0;

	xattr_data = kzalloc(sizeof(*xattr_data), GFP_NOFS);
	if (!xattr_data)
		return -ENOMEM;

	xattr_data->data.type = EVM_XATTR_HMAC;
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
	struct list_head *pos, *q;

	evm_init_config();

	error = integrity_init_keyring(INTEGRITY_KEYRING_EVM);
	if (error)
		goto error;

	error = evm_init_secfs();
	if (error < 0) {
		pr_info("Error registering secfs\n");
		goto error;
	}

error:
	if (error != 0) {
		if (!list_empty(&evm_config_xattrnames)) {
			list_for_each_safe(pos, q, &evm_config_xattrnames)
				list_del(pos);
		}
	}

	return error;
}

late_initcall(init_evm);
