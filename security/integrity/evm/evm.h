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
 * File: evm.h
 *
 */

#ifndef __INTEGRITY_EVM_H
#define __INTEGRITY_EVM_H

#include <linux/xattr.h>
#include <linux/security.h>

#include "../integrity.h"

extern int evm_initialized;
extern char *evm_hmac;
extern char *evm_hash;
extern int evm_hmac_version;

extern struct crypto_shash *hmac_tfm;
extern struct crypto_shash *hash_tfm;

/* List of EVM protected security xattrs */
extern char *evm_config_xattrnames[];

extern int evm_init_key(void);
extern int evm_update_evmxattr(struct dentry *dentry,
			       const char *req_xattr_name,
			       const char *req_xattr_value,
			       size_t req_xattr_value_len);
extern int evm_calc_hmac(struct dentry *dentry, const char *req_xattr_name,
			 const char *req_xattr_value,
			 size_t req_xattr_value_len, char *digest);
extern int evm_calc_hash(struct dentry *dentry, const char *req_xattr_name,
			 const char *req_xattr_value,
			 size_t req_xattr_value_len, char *digest);
extern int evm_init_hmac(struct inode *inode, const struct xattr *xattr,
			 char *hmac_val);
extern int evm_init_secfs(void);

#endif
