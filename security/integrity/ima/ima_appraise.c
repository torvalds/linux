// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 IBM Corporation
 *
 * Author:
 * Mimi Zohar <zohar@us.ibm.com>
 */
#include <linux/init.h>
#include <linux/file.h>
#include <linux/fs.h>
#include <linux/xattr.h>
#include <linux/magic.h>
#include <linux/ima.h>
#include <linux/evm.h>
#include <keys/system_keyring.h>

#include "ima.h"

static int __init default_appraise_setup(char *str)
{
#ifdef CONFIG_IMA_APPRAISE_BOOTPARAM
	bool sb_state = arch_ima_get_secureboot();
	int appraisal_state = ima_appraise;

	if (strncmp(str, "off", 3) == 0)
		appraisal_state = 0;
	else if (strncmp(str, "log", 3) == 0)
		appraisal_state = IMA_APPRAISE_LOG;
	else if (strncmp(str, "fix", 3) == 0)
		appraisal_state = IMA_APPRAISE_FIX;
	else if (strncmp(str, "enforce", 7) == 0)
		appraisal_state = IMA_APPRAISE_ENFORCE;
	else
		pr_err("invalid \"%s\" appraise option", str);

	/* If appraisal state was changed, but secure boot is enabled,
	 * keep its default */
	if (sb_state) {
		if (!(appraisal_state & IMA_APPRAISE_ENFORCE))
			pr_info("Secure boot enabled: ignoring ima_appraise=%s option",
				str);
	} else {
		ima_appraise = appraisal_state;
	}
#endif
	return 1;
}

__setup("ima_appraise=", default_appraise_setup);

/*
 * is_ima_appraise_enabled - return appraise status
 *
 * Only return enabled, if not in ima_appraise="fix" or "log" modes.
 */
bool is_ima_appraise_enabled(void)
{
	return ima_appraise & IMA_APPRAISE_ENFORCE;
}

/*
 * ima_must_appraise - set appraise flag
 *
 * Return 1 to appraise or hash
 */
int ima_must_appraise(struct inode *inode, int mask, enum ima_hooks func)
{
	u32 secid;

	if (!ima_appraise)
		return 0;

	security_task_getsecid(current, &secid);
	return ima_match_policy(inode, current_cred(), secid, func, mask,
				IMA_APPRAISE | IMA_HASH, NULL, NULL, NULL);
}

static int ima_fix_xattr(struct dentry *dentry,
			 struct integrity_iint_cache *iint)
{
	int rc, offset;
	u8 algo = iint->ima_hash->algo;

	if (algo <= HASH_ALGO_SHA1) {
		offset = 1;
		iint->ima_hash->xattr.sha1.type = IMA_XATTR_DIGEST;
	} else {
		offset = 0;
		iint->ima_hash->xattr.ng.type = IMA_XATTR_DIGEST_NG;
		iint->ima_hash->xattr.ng.algo = algo;
	}
	rc = __vfs_setxattr_noperm(dentry, XATTR_NAME_IMA,
				   &iint->ima_hash->xattr.data[offset],
				   (sizeof(iint->ima_hash->xattr) - offset) +
				   iint->ima_hash->length, 0);
	return rc;
}

/* Return specific func appraised cached result */
enum integrity_status ima_get_cache_status(struct integrity_iint_cache *iint,
					   enum ima_hooks func)
{
	switch (func) {
	case MMAP_CHECK:
		return iint->ima_mmap_status;
	case BPRM_CHECK:
		return iint->ima_bprm_status;
	case CREDS_CHECK:
		return iint->ima_creds_status;
	case FILE_CHECK:
	case POST_SETATTR:
		return iint->ima_file_status;
	case MODULE_CHECK ... MAX_CHECK - 1:
	default:
		return iint->ima_read_status;
	}
}

static void ima_set_cache_status(struct integrity_iint_cache *iint,
				 enum ima_hooks func,
				 enum integrity_status status)
{
	switch (func) {
	case MMAP_CHECK:
		iint->ima_mmap_status = status;
		break;
	case BPRM_CHECK:
		iint->ima_bprm_status = status;
		break;
	case CREDS_CHECK:
		iint->ima_creds_status = status;
		break;
	case FILE_CHECK:
	case POST_SETATTR:
		iint->ima_file_status = status;
		break;
	case MODULE_CHECK ... MAX_CHECK - 1:
	default:
		iint->ima_read_status = status;
		break;
	}
}

static void ima_cache_flags(struct integrity_iint_cache *iint,
			     enum ima_hooks func)
{
	switch (func) {
	case MMAP_CHECK:
		iint->flags |= (IMA_MMAP_APPRAISED | IMA_APPRAISED);
		break;
	case BPRM_CHECK:
		iint->flags |= (IMA_BPRM_APPRAISED | IMA_APPRAISED);
		break;
	case CREDS_CHECK:
		iint->flags |= (IMA_CREDS_APPRAISED | IMA_APPRAISED);
		break;
	case FILE_CHECK:
	case POST_SETATTR:
		iint->flags |= (IMA_FILE_APPRAISED | IMA_APPRAISED);
		break;
	case MODULE_CHECK ... MAX_CHECK - 1:
	default:
		iint->flags |= (IMA_READ_APPRAISED | IMA_APPRAISED);
		break;
	}
}

enum hash_algo ima_get_hash_algo(struct evm_ima_xattr_data *xattr_value,
				 int xattr_len)
{
	struct signature_v2_hdr *sig;
	enum hash_algo ret;

	if (!xattr_value || xattr_len < 2)
		/* return default hash algo */
		return ima_hash_algo;

	switch (xattr_value->type) {
	case EVM_IMA_XATTR_DIGSIG:
		sig = (typeof(sig))xattr_value;
		if (sig->version != 2 || xattr_len <= sizeof(*sig))
			return ima_hash_algo;
		return sig->hash_algo;
		break;
	case IMA_XATTR_DIGEST_NG:
		/* first byte contains algorithm id */
		ret = xattr_value->data[0];
		if (ret < HASH_ALGO__LAST)
			return ret;
		break;
	case IMA_XATTR_DIGEST:
		/* this is for backward compatibility */
		if (xattr_len == 21) {
			unsigned int zero = 0;
			if (!memcmp(&xattr_value->data[16], &zero, 4))
				return HASH_ALGO_MD5;
			else
				return HASH_ALGO_SHA1;
		} else if (xattr_len == 17)
			return HASH_ALGO_MD5;
		break;
	}

	/* return default hash algo */
	return ima_hash_algo;
}

int ima_read_xattr(struct dentry *dentry,
		   struct evm_ima_xattr_data **xattr_value)
{
	ssize_t ret;

	ret = vfs_getxattr_alloc(dentry, XATTR_NAME_IMA, (char **)xattr_value,
				 0, GFP_NOFS);
	if (ret == -EOPNOTSUPP)
		ret = 0;
	return ret;
}

/*
 * xattr_verify - verify xattr digest or signature
 *
 * Verify whether the hash or signature matches the file contents.
 *
 * Return 0 on success, error code otherwise.
 */
static int xattr_verify(enum ima_hooks func, struct integrity_iint_cache *iint,
			struct evm_ima_xattr_data *xattr_value, int xattr_len,
			enum integrity_status *status, const char **cause)
{
	int rc = -EINVAL, hash_start = 0;

	switch (xattr_value->type) {
	case IMA_XATTR_DIGEST_NG:
		/* first byte contains algorithm id */
		hash_start = 1;
		fallthrough;
	case IMA_XATTR_DIGEST:
		if (iint->flags & IMA_DIGSIG_REQUIRED) {
			*cause = "IMA-signature-required";
			*status = INTEGRITY_FAIL;
			break;
		}
		clear_bit(IMA_DIGSIG, &iint->atomic_flags);
		if (xattr_len - sizeof(xattr_value->type) - hash_start >=
				iint->ima_hash->length)
			/*
			 * xattr length may be longer. md5 hash in previous
			 * version occupied 20 bytes in xattr, instead of 16
			 */
			rc = memcmp(&xattr_value->data[hash_start],
				    iint->ima_hash->digest,
				    iint->ima_hash->length);
		else
			rc = -EINVAL;
		if (rc) {
			*cause = "invalid-hash";
			*status = INTEGRITY_FAIL;
			break;
		}
		*status = INTEGRITY_PASS;
		break;
	case EVM_IMA_XATTR_DIGSIG:
		set_bit(IMA_DIGSIG, &iint->atomic_flags);
		rc = integrity_digsig_verify(INTEGRITY_KEYRING_IMA,
					     (const char *)xattr_value,
					     xattr_len,
					     iint->ima_hash->digest,
					     iint->ima_hash->length);
		if (rc == -EOPNOTSUPP) {
			*status = INTEGRITY_UNKNOWN;
			break;
		}
		if (IS_ENABLED(CONFIG_INTEGRITY_PLATFORM_KEYRING) && rc &&
		    func == KEXEC_KERNEL_CHECK)
			rc = integrity_digsig_verify(INTEGRITY_KEYRING_PLATFORM,
						     (const char *)xattr_value,
						     xattr_len,
						     iint->ima_hash->digest,
						     iint->ima_hash->length);
		if (rc) {
			*cause = "invalid-signature";
			*status = INTEGRITY_FAIL;
		} else {
			*status = INTEGRITY_PASS;
		}
		break;
	default:
		*status = INTEGRITY_UNKNOWN;
		*cause = "unknown-ima-data";
		break;
	}

	return rc;
}

/*
 * modsig_verify - verify modsig signature
 *
 * Verify whether the signature matches the file contents.
 *
 * Return 0 on success, error code otherwise.
 */
static int modsig_verify(enum ima_hooks func, const struct modsig *modsig,
			 enum integrity_status *status, const char **cause)
{
	int rc;

	rc = integrity_modsig_verify(INTEGRITY_KEYRING_IMA, modsig);
	if (IS_ENABLED(CONFIG_INTEGRITY_PLATFORM_KEYRING) && rc &&
	    func == KEXEC_KERNEL_CHECK)
		rc = integrity_modsig_verify(INTEGRITY_KEYRING_PLATFORM,
					     modsig);
	if (rc) {
		*cause = "invalid-signature";
		*status = INTEGRITY_FAIL;
	} else {
		*status = INTEGRITY_PASS;
	}

	return rc;
}

/*
 * ima_check_blacklist - determine if the binary is blacklisted.
 *
 * Add the hash of the blacklisted binary to the measurement list, based
 * on policy.
 *
 * Returns -EPERM if the hash is blacklisted.
 */
int ima_check_blacklist(struct integrity_iint_cache *iint,
			const struct modsig *modsig, int pcr)
{
	enum hash_algo hash_algo;
	const u8 *digest = NULL;
	u32 digestsize = 0;
	int rc = 0;

	if (!(iint->flags & IMA_CHECK_BLACKLIST))
		return 0;

	if (iint->flags & IMA_MODSIG_ALLOWED && modsig) {
		ima_get_modsig_digest(modsig, &hash_algo, &digest, &digestsize);

		rc = is_binary_blacklisted(digest, digestsize);
		if ((rc == -EPERM) && (iint->flags & IMA_MEASURE))
			process_buffer_measurement(NULL, digest, digestsize,
						   "blacklisted-hash", NONE,
						   pcr, NULL);
	}

	return rc;
}

/*
 * ima_appraise_measurement - appraise file measurement
 *
 * Call evm_verifyxattr() to verify the integrity of 'security.ima'.
 * Assuming success, compare the xattr hash with the collected measurement.
 *
 * Return 0 on success, error code otherwise
 */
int ima_appraise_measurement(enum ima_hooks func,
			     struct integrity_iint_cache *iint,
			     struct file *file, const unsigned char *filename,
			     struct evm_ima_xattr_data *xattr_value,
			     int xattr_len, const struct modsig *modsig)
{
	static const char op[] = "appraise_data";
	const char *cause = "unknown";
	struct dentry *dentry = file_dentry(file);
	struct inode *inode = d_backing_inode(dentry);
	enum integrity_status status = INTEGRITY_UNKNOWN;
	int rc = xattr_len;
	bool try_modsig = iint->flags & IMA_MODSIG_ALLOWED && modsig;

	/* If not appraising a modsig, we need an xattr. */
	if (!(inode->i_opflags & IOP_XATTR) && !try_modsig)
		return INTEGRITY_UNKNOWN;

	/* If reading the xattr failed and there's no modsig, error out. */
	if (rc <= 0 && !try_modsig) {
		if (rc && rc != -ENODATA)
			goto out;

		cause = iint->flags & IMA_DIGSIG_REQUIRED ?
				"IMA-signature-required" : "missing-hash";
		status = INTEGRITY_NOLABEL;
		if (file->f_mode & FMODE_CREATED)
			iint->flags |= IMA_NEW_FILE;
		if ((iint->flags & IMA_NEW_FILE) &&
		    (!(iint->flags & IMA_DIGSIG_REQUIRED) ||
		     (inode->i_size == 0)))
			status = INTEGRITY_PASS;
		goto out;
	}

	status = evm_verifyxattr(dentry, XATTR_NAME_IMA, xattr_value, rc, iint);
	switch (status) {
	case INTEGRITY_PASS:
	case INTEGRITY_PASS_IMMUTABLE:
	case INTEGRITY_UNKNOWN:
		break;
	case INTEGRITY_NOXATTRS:	/* No EVM protected xattrs. */
		/* It's fine not to have xattrs when using a modsig. */
		if (try_modsig)
			break;
		fallthrough;
	case INTEGRITY_NOLABEL:		/* No security.evm xattr. */
		cause = "missing-HMAC";
		goto out;
	case INTEGRITY_FAIL:		/* Invalid HMAC/signature. */
		cause = "invalid-HMAC";
		goto out;
	default:
		WARN_ONCE(true, "Unexpected integrity status %d\n", status);
	}

	if (xattr_value)
		rc = xattr_verify(func, iint, xattr_value, xattr_len, &status,
				  &cause);

	/*
	 * If we have a modsig and either no imasig or the imasig's key isn't
	 * known, then try verifying the modsig.
	 */
	if (try_modsig &&
	    (!xattr_value || xattr_value->type == IMA_XATTR_DIGEST_NG ||
	     rc == -ENOKEY))
		rc = modsig_verify(func, modsig, &status, &cause);

out:
	/*
	 * File signatures on some filesystems can not be properly verified.
	 * When such filesystems are mounted by an untrusted mounter or on a
	 * system not willing to accept such a risk, fail the file signature
	 * verification.
	 */
	if ((inode->i_sb->s_iflags & SB_I_IMA_UNVERIFIABLE_SIGNATURE) &&
	    ((inode->i_sb->s_iflags & SB_I_UNTRUSTED_MOUNTER) ||
	     (iint->flags & IMA_FAIL_UNVERIFIABLE_SIGS))) {
		status = INTEGRITY_FAIL;
		cause = "unverifiable-signature";
		integrity_audit_msg(AUDIT_INTEGRITY_DATA, inode, filename,
				    op, cause, rc, 0);
	} else if (status != INTEGRITY_PASS) {
		/* Fix mode, but don't replace file signatures. */
		if ((ima_appraise & IMA_APPRAISE_FIX) && !try_modsig &&
		    (!xattr_value ||
		     xattr_value->type != EVM_IMA_XATTR_DIGSIG)) {
			if (!ima_fix_xattr(dentry, iint))
				status = INTEGRITY_PASS;
		}

		/* Permit new files with file signatures, but without data. */
		if (inode->i_size == 0 && iint->flags & IMA_NEW_FILE &&
		    xattr_value && xattr_value->type == EVM_IMA_XATTR_DIGSIG) {
			status = INTEGRITY_PASS;
		}

		integrity_audit_msg(AUDIT_INTEGRITY_DATA, inode, filename,
				    op, cause, rc, 0);
	} else {
		ima_cache_flags(iint, func);
	}

	ima_set_cache_status(iint, func, status);
	return status;
}

/*
 * ima_update_xattr - update 'security.ima' hash value
 */
void ima_update_xattr(struct integrity_iint_cache *iint, struct file *file)
{
	struct dentry *dentry = file_dentry(file);
	int rc = 0;

	/* do not collect and update hash for digital signatures */
	if (test_bit(IMA_DIGSIG, &iint->atomic_flags))
		return;

	if ((iint->ima_file_status != INTEGRITY_PASS) &&
	    !(iint->flags & IMA_HASH))
		return;

	rc = ima_collect_measurement(iint, file, NULL, 0, ima_hash_algo, NULL);
	if (rc < 0)
		return;

	inode_lock(file_inode(file));
	ima_fix_xattr(dentry, iint);
	inode_unlock(file_inode(file));
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
	struct inode *inode = d_backing_inode(dentry);
	struct integrity_iint_cache *iint;
	int action;

	if (!(ima_policy_flag & IMA_APPRAISE) || !S_ISREG(inode->i_mode)
	    || !(inode->i_opflags & IOP_XATTR))
		return;

	action = ima_must_appraise(inode, MAY_ACCESS, POST_SETATTR);
	if (!action)
		__vfs_removexattr(dentry, XATTR_NAME_IMA);
	iint = integrity_iint_find(inode);
	if (iint) {
		set_bit(IMA_CHANGE_ATTR, &iint->atomic_flags);
		if (!action)
			clear_bit(IMA_UPDATE_XATTR, &iint->atomic_flags);
	}
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

static void ima_reset_appraise_flags(struct inode *inode, int digsig)
{
	struct integrity_iint_cache *iint;

	if (!(ima_policy_flag & IMA_APPRAISE) || !S_ISREG(inode->i_mode))
		return;

	iint = integrity_iint_find(inode);
	if (!iint)
		return;
	iint->measured_pcrs = 0;
	set_bit(IMA_CHANGE_XATTR, &iint->atomic_flags);
	if (digsig)
		set_bit(IMA_DIGSIG, &iint->atomic_flags);
	else
		clear_bit(IMA_DIGSIG, &iint->atomic_flags);
}

int ima_inode_setxattr(struct dentry *dentry, const char *xattr_name,
		       const void *xattr_value, size_t xattr_value_len)
{
	const struct evm_ima_xattr_data *xvalue = xattr_value;
	int result;

	result = ima_protect_xattr(dentry, xattr_name, xattr_value,
				   xattr_value_len);
	if (result == 1) {
		if (!xattr_value_len || (xvalue->type >= IMA_XATTR_LAST))
			return -EINVAL;
		ima_reset_appraise_flags(d_backing_inode(dentry),
			xvalue->type == EVM_IMA_XATTR_DIGSIG);
		result = 0;
	}
	return result;
}

int ima_inode_removexattr(struct dentry *dentry, const char *xattr_name)
{
	int result;

	result = ima_protect_xattr(dentry, xattr_name, NULL, 0);
	if (result == 1) {
		ima_reset_appraise_flags(d_backing_inode(dentry), 0);
		result = 0;
	}
	return result;
}
