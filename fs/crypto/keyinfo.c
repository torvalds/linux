/*
 * key management facility for FS encryption support.
 *
 * Copyright (C) 2015, Google, Inc.
 *
 * This contains encryption key functions.
 *
 * Written by Michael Halcrow, Ildar Muslukhov, and Uday Savagaonkar, 2015.
 */

#include <keys/encrypted-type.h>
#include <keys/user-type.h>
#include <linux/random.h>
#include <linux/scatterlist.h>
#include <uapi/linux/keyctl.h>
#include <linux/fscrypto.h>

static void derive_crypt_complete(struct crypto_async_request *req, int rc)
{
	struct fscrypt_completion_result *ecr = req->data;

	if (rc == -EINPROGRESS)
		return;

	ecr->res = rc;
	complete(&ecr->completion);
}

/**
 * derive_key_aes() - Derive a key using AES-128-ECB
 * @deriving_key: Encryption key used for derivation.
 * @source_key:   Source key to which to apply derivation.
 * @derived_key:  Derived key.
 *
 * Return: Zero on success; non-zero otherwise.
 */
static int derive_key_aes(u8 deriving_key[FS_AES_128_ECB_KEY_SIZE],
				u8 source_key[FS_AES_256_XTS_KEY_SIZE],
				u8 derived_key[FS_AES_256_XTS_KEY_SIZE])
{
	int res = 0;
	struct skcipher_request *req = NULL;
	DECLARE_FS_COMPLETION_RESULT(ecr);
	struct scatterlist src_sg, dst_sg;
	struct crypto_skcipher *tfm = crypto_alloc_skcipher("ecb(aes)", 0, 0);

	if (IS_ERR(tfm)) {
		res = PTR_ERR(tfm);
		tfm = NULL;
		goto out;
	}
	crypto_skcipher_set_flags(tfm, CRYPTO_TFM_REQ_WEAK_KEY);
	req = skcipher_request_alloc(tfm, GFP_NOFS);
	if (!req) {
		res = -ENOMEM;
		goto out;
	}
	skcipher_request_set_callback(req,
			CRYPTO_TFM_REQ_MAY_BACKLOG | CRYPTO_TFM_REQ_MAY_SLEEP,
			derive_crypt_complete, &ecr);
	res = crypto_skcipher_setkey(tfm, deriving_key,
					FS_AES_128_ECB_KEY_SIZE);
	if (res < 0)
		goto out;

	sg_init_one(&src_sg, source_key, FS_AES_256_XTS_KEY_SIZE);
	sg_init_one(&dst_sg, derived_key, FS_AES_256_XTS_KEY_SIZE);
	skcipher_request_set_crypt(req, &src_sg, &dst_sg,
					FS_AES_256_XTS_KEY_SIZE, NULL);
	res = crypto_skcipher_encrypt(req);
	if (res == -EINPROGRESS || res == -EBUSY) {
		wait_for_completion(&ecr.completion);
		res = ecr.res;
	}
out:
	skcipher_request_free(req);
	crypto_free_skcipher(tfm);
	return res;
}

static int validate_user_key(struct fscrypt_info *crypt_info,
			struct fscrypt_context *ctx, u8 *raw_key,
			u8 *prefix, int prefix_size)
{
	u8 *full_key_descriptor;
	struct key *keyring_key;
	struct fscrypt_key *master_key;
	const struct user_key_payload *ukp;
	int full_key_len = prefix_size + (FS_KEY_DESCRIPTOR_SIZE * 2) + 1;
	int res;

	full_key_descriptor = kmalloc(full_key_len, GFP_NOFS);
	if (!full_key_descriptor)
		return -ENOMEM;

	memcpy(full_key_descriptor, prefix, prefix_size);
	sprintf(full_key_descriptor + prefix_size,
			"%*phN", FS_KEY_DESCRIPTOR_SIZE,
			ctx->master_key_descriptor);
	full_key_descriptor[full_key_len - 1] = '\0';
	keyring_key = request_key(&key_type_logon, full_key_descriptor, NULL);
	kfree(full_key_descriptor);
	if (IS_ERR(keyring_key))
		return PTR_ERR(keyring_key);

	if (keyring_key->type != &key_type_logon) {
		printk_once(KERN_WARNING
				"%s: key type must be logon\n", __func__);
		res = -ENOKEY;
		goto out;
	}
	down_read(&keyring_key->sem);
	ukp = user_key_payload(keyring_key);
	if (ukp->datalen != sizeof(struct fscrypt_key)) {
		res = -EINVAL;
		up_read(&keyring_key->sem);
		goto out;
	}
	master_key = (struct fscrypt_key *)ukp->data;
	BUILD_BUG_ON(FS_AES_128_ECB_KEY_SIZE != FS_KEY_DERIVATION_NONCE_SIZE);

	if (master_key->size != FS_AES_256_XTS_KEY_SIZE) {
		printk_once(KERN_WARNING
				"%s: key size incorrect: %d\n",
				__func__, master_key->size);
		res = -ENOKEY;
		up_read(&keyring_key->sem);
		goto out;
	}
	res = derive_key_aes(ctx->nonce, master_key->raw, raw_key);
	up_read(&keyring_key->sem);
	if (res)
		goto out;

	crypt_info->ci_keyring_key = keyring_key;
	return 0;
out:
	key_put(keyring_key);
	return res;
}

static void put_crypt_info(struct fscrypt_info *ci)
{
	if (!ci)
		return;

	key_put(ci->ci_keyring_key);
	crypto_free_skcipher(ci->ci_ctfm);
	kmem_cache_free(fscrypt_info_cachep, ci);
}

int get_crypt_info(struct inode *inode)
{
	struct fscrypt_info *crypt_info;
	struct fscrypt_context ctx;
	struct crypto_skcipher *ctfm;
	const char *cipher_str;
	u8 raw_key[FS_MAX_KEY_SIZE];
	u8 mode;
	int res;

	res = fscrypt_initialize();
	if (res)
		return res;

	if (!inode->i_sb->s_cop->get_context)
		return -EOPNOTSUPP;
retry:
	crypt_info = ACCESS_ONCE(inode->i_crypt_info);
	if (crypt_info) {
		if (!crypt_info->ci_keyring_key ||
				key_validate(crypt_info->ci_keyring_key) == 0)
			return 0;
		fscrypt_put_encryption_info(inode, crypt_info);
		goto retry;
	}

	res = inode->i_sb->s_cop->get_context(inode, &ctx, sizeof(ctx));
	if (res < 0) {
		if (!fscrypt_dummy_context_enabled(inode))
			return res;
		ctx.contents_encryption_mode = FS_ENCRYPTION_MODE_AES_256_XTS;
		ctx.filenames_encryption_mode = FS_ENCRYPTION_MODE_AES_256_CTS;
		ctx.flags = 0;
	} else if (res != sizeof(ctx)) {
		return -EINVAL;
	}
	res = 0;

	crypt_info = kmem_cache_alloc(fscrypt_info_cachep, GFP_NOFS);
	if (!crypt_info)
		return -ENOMEM;

	crypt_info->ci_flags = ctx.flags;
	crypt_info->ci_data_mode = ctx.contents_encryption_mode;
	crypt_info->ci_filename_mode = ctx.filenames_encryption_mode;
	crypt_info->ci_ctfm = NULL;
	crypt_info->ci_keyring_key = NULL;
	memcpy(crypt_info->ci_master_key, ctx.master_key_descriptor,
				sizeof(crypt_info->ci_master_key));
	if (S_ISREG(inode->i_mode))
		mode = crypt_info->ci_data_mode;
	else if (S_ISDIR(inode->i_mode) || S_ISLNK(inode->i_mode))
		mode = crypt_info->ci_filename_mode;
	else
		BUG();

	switch (mode) {
	case FS_ENCRYPTION_MODE_AES_256_XTS:
		cipher_str = "xts(aes)";
		break;
	case FS_ENCRYPTION_MODE_AES_256_CTS:
		cipher_str = "cts(cbc(aes))";
		break;
	default:
		printk_once(KERN_WARNING
			    "%s: unsupported key mode %d (ino %u)\n",
			    __func__, mode, (unsigned) inode->i_ino);
		res = -ENOKEY;
		goto out;
	}
	if (fscrypt_dummy_context_enabled(inode)) {
		memset(raw_key, 0x42, FS_AES_256_XTS_KEY_SIZE);
		goto got_key;
	}

	res = validate_user_key(crypt_info, &ctx, raw_key,
			FS_KEY_DESC_PREFIX, FS_KEY_DESC_PREFIX_SIZE);
	if (res && inode->i_sb->s_cop->key_prefix) {
		u8 *prefix = NULL;
		int prefix_size, res2;

		prefix_size = inode->i_sb->s_cop->key_prefix(inode, &prefix);
		res2 = validate_user_key(crypt_info, &ctx, raw_key,
							prefix, prefix_size);
		if (res2) {
			if (res2 == -ENOKEY)
				res = -ENOKEY;
			goto out;
		}
	} else if (res) {
		goto out;
	}
got_key:
	ctfm = crypto_alloc_skcipher(cipher_str, 0, 0);
	if (!ctfm || IS_ERR(ctfm)) {
		res = ctfm ? PTR_ERR(ctfm) : -ENOMEM;
		printk(KERN_DEBUG
		       "%s: error %d (inode %u) allocating crypto tfm\n",
		       __func__, res, (unsigned) inode->i_ino);
		goto out;
	}
	crypt_info->ci_ctfm = ctfm;
	crypto_skcipher_clear_flags(ctfm, ~0);
	crypto_skcipher_set_flags(ctfm, CRYPTO_TFM_REQ_WEAK_KEY);
	res = crypto_skcipher_setkey(ctfm, raw_key, fscrypt_key_size(mode));
	if (res)
		goto out;

	memzero_explicit(raw_key, sizeof(raw_key));
	if (cmpxchg(&inode->i_crypt_info, NULL, crypt_info) != NULL) {
		put_crypt_info(crypt_info);
		goto retry;
	}
	return 0;

out:
	if (res == -ENOKEY)
		res = 0;
	put_crypt_info(crypt_info);
	memzero_explicit(raw_key, sizeof(raw_key));
	return res;
}

void fscrypt_put_encryption_info(struct inode *inode, struct fscrypt_info *ci)
{
	struct fscrypt_info *prev;

	if (ci == NULL)
		ci = ACCESS_ONCE(inode->i_crypt_info);
	if (ci == NULL)
		return;

	prev = cmpxchg(&inode->i_crypt_info, ci, NULL);
	if (prev != ci)
		return;

	put_crypt_info(ci);
}
EXPORT_SYMBOL(fscrypt_put_encryption_info);

int fscrypt_get_encryption_info(struct inode *inode)
{
	struct fscrypt_info *ci = inode->i_crypt_info;

	if (!ci ||
		(ci->ci_keyring_key &&
		 (ci->ci_keyring_key->flags & ((1 << KEY_FLAG_INVALIDATED) |
					       (1 << KEY_FLAG_REVOKED) |
					       (1 << KEY_FLAG_DEAD)))))
		return get_crypt_info(inode);
	return 0;
}
EXPORT_SYMBOL(fscrypt_get_encryption_info);
