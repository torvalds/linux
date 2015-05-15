/*
 * linux/fs/f2fs/crypto_key.c
 *
 * Copied from linux/fs/f2fs/crypto_key.c
 *
 * Copyright (C) 2015, Google, Inc.
 *
 * This contains encryption key functions for f2fs
 *
 * Written by Michael Halcrow, Ildar Muslukhov, and Uday Savagaonkar, 2015.
 */
#include <keys/encrypted-type.h>
#include <keys/user-type.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <uapi/linux/keyctl.h>
#include <crypto/hash.h>
#include <linux/f2fs_fs.h>

#include "f2fs.h"
#include "xattr.h"

static void derive_crypt_complete(struct crypto_async_request *req, int rc)
{
	struct f2fs_completion_result *ecr = req->data;

	if (rc == -EINPROGRESS)
		return;

	ecr->res = rc;
	complete(&ecr->completion);
}

/**
 * f2fs_derive_key_aes() - Derive a key using AES-128-ECB
 * @deriving_key: Encryption key used for derivatio.
 * @source_key:   Source key to which to apply derivation.
 * @derived_key:  Derived key.
 *
 * Return: Zero on success; non-zero otherwise.
 */
static int f2fs_derive_key_aes(char deriving_key[F2FS_AES_128_ECB_KEY_SIZE],
				char source_key[F2FS_AES_256_XTS_KEY_SIZE],
				char derived_key[F2FS_AES_256_XTS_KEY_SIZE])
{
	int res = 0;
	struct ablkcipher_request *req = NULL;
	DECLARE_F2FS_COMPLETION_RESULT(ecr);
	struct scatterlist src_sg, dst_sg;
	struct crypto_ablkcipher *tfm = crypto_alloc_ablkcipher("ecb(aes)", 0,
								0);

	if (IS_ERR(tfm)) {
		res = PTR_ERR(tfm);
		tfm = NULL;
		goto out;
	}
	crypto_ablkcipher_set_flags(tfm, CRYPTO_TFM_REQ_WEAK_KEY);
	req = ablkcipher_request_alloc(tfm, GFP_NOFS);
	if (!req) {
		res = -ENOMEM;
		goto out;
	}
	ablkcipher_request_set_callback(req,
			CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
			derive_crypt_complete, &ecr);
	res = crypto_ablkcipher_setkey(tfm, deriving_key,
				F2FS_AES_128_ECB_KEY_SIZE);
	if (res < 0)
		goto out;

	sg_init_one(&src_sg, source_key, F2FS_AES_256_XTS_KEY_SIZE);
	sg_init_one(&dst_sg, derived_key, F2FS_AES_256_XTS_KEY_SIZE);
	ablkcipher_request_set_crypt(req, &src_sg, &dst_sg,
					F2FS_AES_256_XTS_KEY_SIZE, NULL);
	res = crypto_ablkcipher_encrypt(req);
	if (res == -EINPROGRESS || res == -EBUSY) {
		BUG_ON(req->base.data != &ecr);
		wait_for_completion(&ecr.completion);
		res = ecr.res;
	}
out:
	if (req)
		ablkcipher_request_free(req);
	if (tfm)
		crypto_free_ablkcipher(tfm);
	return res;
}

void f2fs_free_encryption_info(struct inode *inode)
{
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct f2fs_crypt_info *ci = fi->i_crypt_info;

	if (!ci)
		return;

	if (ci->ci_keyring_key)
		key_put(ci->ci_keyring_key);
	crypto_free_ablkcipher(ci->ci_ctfm);
	memzero_explicit(&ci->ci_raw, sizeof(ci->ci_raw));
	kmem_cache_free(f2fs_crypt_info_cachep, ci);
	fi->i_crypt_info = NULL;
}

int _f2fs_get_encryption_info(struct inode *inode)
{
	struct f2fs_inode_info *fi = F2FS_I(inode);
	struct f2fs_crypt_info *crypt_info;
	char full_key_descriptor[F2FS_KEY_DESC_PREFIX_SIZE +
				(F2FS_KEY_DESCRIPTOR_SIZE * 2) + 1];
	struct key *keyring_key = NULL;
	struct f2fs_encryption_key *master_key;
	struct f2fs_encryption_context ctx;
	struct user_key_payload *ukp;
	int res;

	res = f2fs_crypto_initialize();
	if (res)
		return res;

	if (fi->i_crypt_info) {
		if (!fi->i_crypt_info->ci_keyring_key ||
			key_validate(fi->i_crypt_info->ci_keyring_key) == 0)
			return 0;
		f2fs_free_encryption_info(inode);
	}

	res = f2fs_getxattr(inode, F2FS_XATTR_INDEX_ENCRYPTION,
				F2FS_XATTR_NAME_ENCRYPTION_CONTEXT,
				&ctx, sizeof(ctx), NULL);
	if (res < 0)
		return res;
	else if (res != sizeof(ctx))
		return -EINVAL;
	res = 0;

	crypt_info = kmem_cache_alloc(f2fs_crypt_info_cachep, GFP_NOFS);
	if (!crypt_info)
		return -ENOMEM;

	crypt_info->ci_flags = ctx.flags;
	crypt_info->ci_data_mode = ctx.contents_encryption_mode;
	crypt_info->ci_filename_mode = ctx.filenames_encryption_mode;
	crypt_info->ci_ctfm = NULL;
	memcpy(crypt_info->ci_master_key, ctx.master_key_descriptor,
				sizeof(crypt_info->ci_master_key));
	if (S_ISREG(inode->i_mode))
		crypt_info->ci_size =
			f2fs_encryption_key_size(crypt_info->ci_data_mode);
	else if (S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode))
		crypt_info->ci_size =
			f2fs_encryption_key_size(crypt_info->ci_filename_mode);
	else
		BUG();

	BUG_ON(!crypt_info->ci_size);

	memcpy(full_key_descriptor, F2FS_KEY_DESC_PREFIX,
					F2FS_KEY_DESC_PREFIX_SIZE);
	sprintf(full_key_descriptor + F2FS_KEY_DESC_PREFIX_SIZE,
					"%*phN", F2FS_KEY_DESCRIPTOR_SIZE,
					ctx.master_key_descriptor);
	full_key_descriptor[F2FS_KEY_DESC_PREFIX_SIZE +
					(2 * F2FS_KEY_DESCRIPTOR_SIZE)] = '\0';
	keyring_key = request_key(&key_type_logon, full_key_descriptor, NULL);
	if (IS_ERR(keyring_key)) {
		res = PTR_ERR(keyring_key);
		keyring_key = NULL;
		goto out;
	}
	BUG_ON(keyring_key->type != &key_type_logon);
	ukp = ((struct user_key_payload *)keyring_key->payload.data);
	if (ukp->datalen != sizeof(struct f2fs_encryption_key)) {
		res = -EINVAL;
		goto out;
	}
	master_key = (struct f2fs_encryption_key *)ukp->data;
	BUILD_BUG_ON(F2FS_AES_128_ECB_KEY_SIZE !=
				F2FS_KEY_DERIVATION_NONCE_SIZE);
	BUG_ON(master_key->size != F2FS_AES_256_XTS_KEY_SIZE);
	res = f2fs_derive_key_aes(ctx.nonce, master_key->raw,
					crypt_info->ci_raw);
out:
	if (res < 0) {
		if (res == -ENOKEY)
			res = 0;
		kmem_cache_free(f2fs_crypt_info_cachep, crypt_info);
	} else {
		fi->i_crypt_info = crypt_info;
		crypt_info->ci_keyring_key = keyring_key;
		keyring_key = NULL;
	}
	if (keyring_key)
		key_put(keyring_key);
	return res;
}

int f2fs_has_encryption_key(struct inode *inode)
{
	struct f2fs_inode_info *fi = F2FS_I(inode);

	return (fi->i_crypt_info != NULL);
}
