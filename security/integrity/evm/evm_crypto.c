/*
 * Copyright (C) 2005-2010 IBM Corporation
 *
 * Authors:
 * Mimi Zohar <zohar@us.ibm.com>
 * Kylene Hall <kjhall@us.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 * File: evm_crypto.c
 *	 Using root's kernel master key (kmk), calculate the HMAC
 */

#include <linux/module.h>
#include <linux/crypto.h>
#include <linux/xattr.h>
#include <linux/scatterlist.h>
#include <keys/encrypted-type.h>
#include "evm.h"

#define EVMKEY "evm-key"
#define MAX_KEY_SIZE 128
static unsigned char evmkey[MAX_KEY_SIZE];
static int evmkey_len = MAX_KEY_SIZE;

static int init_desc(struct hash_desc *desc)
{
	int rc;

	desc->tfm = crypto_alloc_hash(evm_hmac, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(desc->tfm)) {
		pr_info("Can not allocate %s (reason: %ld)\n",
			evm_hmac, PTR_ERR(desc->tfm));
		rc = PTR_ERR(desc->tfm);
		return rc;
	}
	desc->flags = 0;
	rc = crypto_hash_setkey(desc->tfm, evmkey, evmkey_len);
	if (rc)
		goto out;
	rc = crypto_hash_init(desc);
out:
	if (rc)
		crypto_free_hash(desc->tfm);
	return rc;
}

/* Protect against 'cutting & pasting' security.evm xattr, include inode
 * specific info.
 *
 * (Additional directory/file metadata needs to be added for more complete
 * protection.)
 */
static void hmac_add_misc(struct hash_desc *desc, struct inode *inode,
			  char *digest)
{
	struct h_misc {
		unsigned long ino;
		__u32 generation;
		uid_t uid;
		gid_t gid;
		umode_t mode;
	} hmac_misc;
	struct scatterlist sg[1];

	memset(&hmac_misc, 0, sizeof hmac_misc);
	hmac_misc.ino = inode->i_ino;
	hmac_misc.generation = inode->i_generation;
	hmac_misc.uid = inode->i_uid;
	hmac_misc.gid = inode->i_gid;
	hmac_misc.mode = inode->i_mode;
	sg_init_one(sg, &hmac_misc, sizeof hmac_misc);
	crypto_hash_update(desc, sg, sizeof hmac_misc);
	crypto_hash_final(desc, digest);
}

/*
 * Calculate the HMAC value across the set of protected security xattrs.
 *
 * Instead of retrieving the requested xattr, for performance, calculate
 * the hmac using the requested xattr value. Don't alloc/free memory for
 * each xattr, but attempt to re-use the previously allocated memory.
 */
int evm_calc_hmac(struct dentry *dentry, const char *req_xattr_name,
		  const char *req_xattr_value, size_t req_xattr_value_len,
		  char *digest)
{
	struct inode *inode = dentry->d_inode;
	struct hash_desc desc;
	struct scatterlist sg[1];
	char **xattrname;
	size_t xattr_size = 0;
	char *xattr_value = NULL;
	int error;
	int size;

	if (!inode->i_op || !inode->i_op->getxattr)
		return -EOPNOTSUPP;
	error = init_desc(&desc);
	if (error)
		return error;

	error = -ENODATA;
	for (xattrname = evm_config_xattrnames; *xattrname != NULL; xattrname++) {
		if ((req_xattr_name && req_xattr_value)
		    && !strcmp(*xattrname, req_xattr_name)) {
			error = 0;
			sg_init_one(sg, req_xattr_value, req_xattr_value_len);
			crypto_hash_update(&desc, sg, req_xattr_value_len);
			continue;
		}
		size = vfs_getxattr_alloc(dentry, *xattrname,
					  &xattr_value, xattr_size, GFP_NOFS);
		if (size == -ENOMEM) {
			error = -ENOMEM;
			goto out;
		}
		if (size < 0)
			continue;

		error = 0;
		xattr_size = size;
		sg_init_one(sg, xattr_value, xattr_size);
		crypto_hash_update(&desc, sg, xattr_size);
	}
	hmac_add_misc(&desc, inode, digest);
	kfree(xattr_value);
out:
	crypto_free_hash(desc.tfm);
	return error;
}

/*
 * Calculate the hmac and update security.evm xattr
 *
 * Expects to be called with i_mutex locked.
 */
int evm_update_evmxattr(struct dentry *dentry, const char *xattr_name,
			const char *xattr_value, size_t xattr_value_len)
{
	struct inode *inode = dentry->d_inode;
	struct evm_ima_xattr_data xattr_data;
	int rc = 0;

	rc = evm_calc_hmac(dentry, xattr_name, xattr_value,
			   xattr_value_len, xattr_data.digest);
	if (rc == 0) {
		xattr_data.type = EVM_XATTR_HMAC;
		rc = __vfs_setxattr_noperm(dentry, XATTR_NAME_EVM,
					   &xattr_data,
					   sizeof(xattr_data), 0);
	}
	else if (rc == -ENODATA)
		rc = inode->i_op->removexattr(dentry, XATTR_NAME_EVM);
	return rc;
}

int evm_init_hmac(struct inode *inode, const struct xattr *lsm_xattr,
		  char *hmac_val)
{
	struct hash_desc desc;
	struct scatterlist sg[1];
	int error;

	error = init_desc(&desc);
	if (error != 0) {
		printk(KERN_INFO "init_desc failed\n");
		return error;
	}

	sg_init_one(sg, lsm_xattr->value, lsm_xattr->value_len);
	crypto_hash_update(&desc, sg, lsm_xattr->value_len);
	hmac_add_misc(&desc, inode, hmac_val);
	crypto_free_hash(desc.tfm);
	return 0;
}

/*
 * Get the key from the TPM for the SHA1-HMAC
 */
int evm_init_key(void)
{
	struct key *evm_key;
	struct encrypted_key_payload *ekp;
	int rc = 0;

	evm_key = request_key(&key_type_encrypted, EVMKEY, NULL);
	if (IS_ERR(evm_key))
		return -ENOENT;

	down_read(&evm_key->sem);
	ekp = evm_key->payload.data;
	if (ekp->decrypted_datalen > MAX_KEY_SIZE) {
		rc = -EINVAL;
		goto out;
	}
	memcpy(evmkey, ekp->decrypted_data, ekp->decrypted_datalen);
out:
	/* burn the original key contents */
	memset(ekp->decrypted_data, 0, ekp->decrypted_datalen);
	up_read(&evm_key->sem);
	key_put(evm_key);
	return rc;
}
