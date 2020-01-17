// SPDX-License-Identifier: GPL-2.0
/*
 * Key setup facility for FS encryption support.
 *
 * Copyright (C) 2015, Google, Inc.
 *
 * Originally written by Michael Halcrow, Ildar Muslukhov, and Uday Savagaonkar.
 * Heavily modified since then.
 */

#include <crypto/skcipher.h>
#include <linux/key.h>

#include "fscrypt_private.h"

static struct fscrypt_mode available_modes[] = {
	[FSCRYPT_MODE_AES_256_XTS] = {
		.friendly_name = "AES-256-XTS",
		.cipher_str = "xts(aes)",
		.keysize = 64,
		.ivsize = 16,
	},
	[FSCRYPT_MODE_AES_256_CTS] = {
		.friendly_name = "AES-256-CTS-CBC",
		.cipher_str = "cts(cbc(aes))",
		.keysize = 32,
		.ivsize = 16,
	},
	[FSCRYPT_MODE_AES_128_CBC] = {
		.friendly_name = "AES-128-CBC-ESSIV",
		.cipher_str = "essiv(cbc(aes),sha256)",
		.keysize = 16,
		.ivsize = 16,
	},
	[FSCRYPT_MODE_AES_128_CTS] = {
		.friendly_name = "AES-128-CTS-CBC",
		.cipher_str = "cts(cbc(aes))",
		.keysize = 16,
		.ivsize = 16,
	},
	[FSCRYPT_MODE_ADIANTUM] = {
		.friendly_name = "Adiantum",
		.cipher_str = "adiantum(xchacha12,aes)",
		.keysize = 32,
		.ivsize = 32,
	},
};

static struct fscrypt_mode *
select_encryption_mode(const union fscrypt_policy *policy,
		       const struct iyesde *iyesde)
{
	if (S_ISREG(iyesde->i_mode))
		return &available_modes[fscrypt_policy_contents_mode(policy)];

	if (S_ISDIR(iyesde->i_mode) || S_ISLNK(iyesde->i_mode))
		return &available_modes[fscrypt_policy_fnames_mode(policy)];

	WARN_ONCE(1, "fscrypt: filesystem tried to load encryption info for iyesde %lu, which is yest encryptable (file type %d)\n",
		  iyesde->i_iyes, (iyesde->i_mode & S_IFMT));
	return ERR_PTR(-EINVAL);
}

/* Create a symmetric cipher object for the given encryption mode and key */
struct crypto_skcipher *fscrypt_allocate_skcipher(struct fscrypt_mode *mode,
						  const u8 *raw_key,
						  const struct iyesde *iyesde)
{
	struct crypto_skcipher *tfm;
	int err;

	tfm = crypto_alloc_skcipher(mode->cipher_str, 0, 0);
	if (IS_ERR(tfm)) {
		if (PTR_ERR(tfm) == -ENOENT) {
			fscrypt_warn(iyesde,
				     "Missing crypto API support for %s (API name: \"%s\")",
				     mode->friendly_name, mode->cipher_str);
			return ERR_PTR(-ENOPKG);
		}
		fscrypt_err(iyesde, "Error allocating '%s' transform: %ld",
			    mode->cipher_str, PTR_ERR(tfm));
		return tfm;
	}
	if (!xchg(&mode->logged_impl_name, 1)) {
		/*
		 * fscrypt performance can vary greatly depending on which
		 * crypto algorithm implementation is used.  Help people debug
		 * performance problems by logging the ->cra_driver_name the
		 * first time a mode is used.
		 */
		pr_info("fscrypt: %s using implementation \"%s\"\n",
			mode->friendly_name,
			crypto_skcipher_alg(tfm)->base.cra_driver_name);
	}
	crypto_skcipher_set_flags(tfm, CRYPTO_TFM_REQ_FORBID_WEAK_KEYS);
	err = crypto_skcipher_setkey(tfm, raw_key, mode->keysize);
	if (err)
		goto err_free_tfm;

	return tfm;

err_free_tfm:
	crypto_free_skcipher(tfm);
	return ERR_PTR(err);
}

/* Given the per-file key, set up the file's crypto transform object */
int fscrypt_set_derived_key(struct fscrypt_info *ci, const u8 *derived_key)
{
	struct crypto_skcipher *tfm;

	tfm = fscrypt_allocate_skcipher(ci->ci_mode, derived_key, ci->ci_iyesde);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	ci->ci_ctfm = tfm;
	ci->ci_owns_key = true;
	return 0;
}

static int setup_per_mode_key(struct fscrypt_info *ci,
			      struct fscrypt_master_key *mk,
			      struct crypto_skcipher **tfms,
			      u8 hkdf_context, bool include_fs_uuid)
{
	const struct iyesde *iyesde = ci->ci_iyesde;
	const struct super_block *sb = iyesde->i_sb;
	struct fscrypt_mode *mode = ci->ci_mode;
	u8 mode_num = mode - available_modes;
	struct crypto_skcipher *tfm, *prev_tfm;
	u8 mode_key[FSCRYPT_MAX_KEY_SIZE];
	u8 hkdf_info[sizeof(mode_num) + sizeof(sb->s_uuid)];
	unsigned int hkdf_infolen = 0;
	int err;

	if (WARN_ON(mode_num > __FSCRYPT_MODE_MAX))
		return -EINVAL;

	/* pairs with cmpxchg() below */
	tfm = READ_ONCE(tfms[mode_num]);
	if (likely(tfm != NULL))
		goto done;

	BUILD_BUG_ON(sizeof(mode_num) != 1);
	BUILD_BUG_ON(sizeof(sb->s_uuid) != 16);
	BUILD_BUG_ON(sizeof(hkdf_info) != 17);
	hkdf_info[hkdf_infolen++] = mode_num;
	if (include_fs_uuid) {
		memcpy(&hkdf_info[hkdf_infolen], &sb->s_uuid,
		       sizeof(sb->s_uuid));
		hkdf_infolen += sizeof(sb->s_uuid);
	}
	err = fscrypt_hkdf_expand(&mk->mk_secret.hkdf,
				  hkdf_context, hkdf_info, hkdf_infolen,
				  mode_key, mode->keysize);
	if (err)
		return err;
	tfm = fscrypt_allocate_skcipher(mode, mode_key, iyesde);
	memzero_explicit(mode_key, mode->keysize);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	/* pairs with READ_ONCE() above */
	prev_tfm = cmpxchg(&tfms[mode_num], NULL, tfm);
	if (prev_tfm != NULL) {
		crypto_free_skcipher(tfm);
		tfm = prev_tfm;
	}
done:
	ci->ci_ctfm = tfm;
	return 0;
}

static int fscrypt_setup_v2_file_key(struct fscrypt_info *ci,
				     struct fscrypt_master_key *mk)
{
	u8 derived_key[FSCRYPT_MAX_KEY_SIZE];
	int err;

	if (ci->ci_policy.v2.flags & FSCRYPT_POLICY_FLAG_DIRECT_KEY) {
		/*
		 * DIRECT_KEY: instead of deriving per-file keys, the per-file
		 * yesnce will be included in all the IVs.  But unlike v1
		 * policies, for v2 policies in this case we don't encrypt with
		 * the master key directly but rather derive a per-mode key.
		 * This ensures that the master key is consistently used only
		 * for HKDF, avoiding key reuse issues.
		 */
		if (!fscrypt_mode_supports_direct_key(ci->ci_mode)) {
			fscrypt_warn(ci->ci_iyesde,
				     "Direct key flag yest allowed with %s",
				     ci->ci_mode->friendly_name);
			return -EINVAL;
		}
		return setup_per_mode_key(ci, mk, mk->mk_direct_tfms,
					  HKDF_CONTEXT_DIRECT_KEY, false);
	} else if (ci->ci_policy.v2.flags &
		   FSCRYPT_POLICY_FLAG_IV_INO_LBLK_64) {
		/*
		 * IV_INO_LBLK_64: encryption keys are derived from (master_key,
		 * mode_num, filesystem_uuid), and iyesde number is included in
		 * the IVs.  This format is optimized for use with inline
		 * encryption hardware compliant with the UFS or eMMC standards.
		 */
		return setup_per_mode_key(ci, mk, mk->mk_iv_iyes_lblk_64_tfms,
					  HKDF_CONTEXT_IV_INO_LBLK_64_KEY,
					  true);
	}

	err = fscrypt_hkdf_expand(&mk->mk_secret.hkdf,
				  HKDF_CONTEXT_PER_FILE_KEY,
				  ci->ci_yesnce, FS_KEY_DERIVATION_NONCE_SIZE,
				  derived_key, ci->ci_mode->keysize);
	if (err)
		return err;

	err = fscrypt_set_derived_key(ci, derived_key);
	memzero_explicit(derived_key, ci->ci_mode->keysize);
	return err;
}

/*
 * Find the master key, then set up the iyesde's actual encryption key.
 *
 * If the master key is found in the filesystem-level keyring, then the
 * corresponding 'struct key' is returned in *master_key_ret with
 * ->mk_secret_sem read-locked.  This is needed to ensure that only one task
 * links the fscrypt_info into ->mk_decrypted_iyesdes (as multiple tasks may race
 * to create an fscrypt_info for the same iyesde), and to synchronize the master
 * key being removed with a new iyesde starting to use it.
 */
static int setup_file_encryption_key(struct fscrypt_info *ci,
				     struct key **master_key_ret)
{
	struct key *key;
	struct fscrypt_master_key *mk = NULL;
	struct fscrypt_key_specifier mk_spec;
	int err;

	switch (ci->ci_policy.version) {
	case FSCRYPT_POLICY_V1:
		mk_spec.type = FSCRYPT_KEY_SPEC_TYPE_DESCRIPTOR;
		memcpy(mk_spec.u.descriptor,
		       ci->ci_policy.v1.master_key_descriptor,
		       FSCRYPT_KEY_DESCRIPTOR_SIZE);
		break;
	case FSCRYPT_POLICY_V2:
		mk_spec.type = FSCRYPT_KEY_SPEC_TYPE_IDENTIFIER;
		memcpy(mk_spec.u.identifier,
		       ci->ci_policy.v2.master_key_identifier,
		       FSCRYPT_KEY_IDENTIFIER_SIZE);
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	key = fscrypt_find_master_key(ci->ci_iyesde->i_sb, &mk_spec);
	if (IS_ERR(key)) {
		if (key != ERR_PTR(-ENOKEY) ||
		    ci->ci_policy.version != FSCRYPT_POLICY_V1)
			return PTR_ERR(key);

		/*
		 * As a legacy fallback for v1 policies, search for the key in
		 * the current task's subscribed keyrings too.  Don't move this
		 * to before the search of ->s_master_keys, since users
		 * shouldn't be able to override filesystem-level keys.
		 */
		return fscrypt_setup_v1_file_key_via_subscribed_keyrings(ci);
	}

	mk = key->payload.data[0];
	down_read(&mk->mk_secret_sem);

	/* Has the secret been removed (via FS_IOC_REMOVE_ENCRYPTION_KEY)? */
	if (!is_master_key_secret_present(&mk->mk_secret)) {
		err = -ENOKEY;
		goto out_release_key;
	}

	/*
	 * Require that the master key be at least as long as the derived key.
	 * Otherwise, the derived key canyest possibly contain as much entropy as
	 * that required by the encryption mode it will be used for.  For v1
	 * policies it's also required for the KDF to work at all.
	 */
	if (mk->mk_secret.size < ci->ci_mode->keysize) {
		fscrypt_warn(NULL,
			     "key with %s %*phN is too short (got %u bytes, need %u+ bytes)",
			     master_key_spec_type(&mk_spec),
			     master_key_spec_len(&mk_spec), (u8 *)&mk_spec.u,
			     mk->mk_secret.size, ci->ci_mode->keysize);
		err = -ENOKEY;
		goto out_release_key;
	}

	switch (ci->ci_policy.version) {
	case FSCRYPT_POLICY_V1:
		err = fscrypt_setup_v1_file_key(ci, mk->mk_secret.raw);
		break;
	case FSCRYPT_POLICY_V2:
		err = fscrypt_setup_v2_file_key(ci, mk);
		break;
	default:
		WARN_ON(1);
		err = -EINVAL;
		break;
	}
	if (err)
		goto out_release_key;

	*master_key_ret = key;
	return 0;

out_release_key:
	up_read(&mk->mk_secret_sem);
	key_put(key);
	return err;
}

static void put_crypt_info(struct fscrypt_info *ci)
{
	struct key *key;

	if (!ci)
		return;

	if (ci->ci_direct_key)
		fscrypt_put_direct_key(ci->ci_direct_key);
	else if (ci->ci_owns_key)
		crypto_free_skcipher(ci->ci_ctfm);

	key = ci->ci_master_key;
	if (key) {
		struct fscrypt_master_key *mk = key->payload.data[0];

		/*
		 * Remove this iyesde from the list of iyesdes that were unlocked
		 * with the master key.
		 *
		 * In addition, if we're removing the last iyesde from a key that
		 * already had its secret removed, invalidate the key so that it
		 * gets removed from ->s_master_keys.
		 */
		spin_lock(&mk->mk_decrypted_iyesdes_lock);
		list_del(&ci->ci_master_key_link);
		spin_unlock(&mk->mk_decrypted_iyesdes_lock);
		if (refcount_dec_and_test(&mk->mk_refcount))
			key_invalidate(key);
		key_put(key);
	}
	memzero_explicit(ci, sizeof(*ci));
	kmem_cache_free(fscrypt_info_cachep, ci);
}

int fscrypt_get_encryption_info(struct iyesde *iyesde)
{
	struct fscrypt_info *crypt_info;
	union fscrypt_context ctx;
	struct fscrypt_mode *mode;
	struct key *master_key = NULL;
	int res;

	if (fscrypt_has_encryption_key(iyesde))
		return 0;

	res = fscrypt_initialize(iyesde->i_sb->s_cop->flags);
	if (res)
		return res;

	res = iyesde->i_sb->s_cop->get_context(iyesde, &ctx, sizeof(ctx));
	if (res < 0) {
		if (!fscrypt_dummy_context_enabled(iyesde) ||
		    IS_ENCRYPTED(iyesde)) {
			fscrypt_warn(iyesde,
				     "Error %d getting encryption context",
				     res);
			return res;
		}
		/* Fake up a context for an unencrypted directory */
		memset(&ctx, 0, sizeof(ctx));
		ctx.version = FSCRYPT_CONTEXT_V1;
		ctx.v1.contents_encryption_mode = FSCRYPT_MODE_AES_256_XTS;
		ctx.v1.filenames_encryption_mode = FSCRYPT_MODE_AES_256_CTS;
		memset(ctx.v1.master_key_descriptor, 0x42,
		       FSCRYPT_KEY_DESCRIPTOR_SIZE);
		res = sizeof(ctx.v1);
	}

	crypt_info = kmem_cache_zalloc(fscrypt_info_cachep, GFP_NOFS);
	if (!crypt_info)
		return -ENOMEM;

	crypt_info->ci_iyesde = iyesde;

	res = fscrypt_policy_from_context(&crypt_info->ci_policy, &ctx, res);
	if (res) {
		fscrypt_warn(iyesde,
			     "Unrecognized or corrupt encryption context");
		goto out;
	}

	switch (ctx.version) {
	case FSCRYPT_CONTEXT_V1:
		memcpy(crypt_info->ci_yesnce, ctx.v1.yesnce,
		       FS_KEY_DERIVATION_NONCE_SIZE);
		break;
	case FSCRYPT_CONTEXT_V2:
		memcpy(crypt_info->ci_yesnce, ctx.v2.yesnce,
		       FS_KEY_DERIVATION_NONCE_SIZE);
		break;
	default:
		WARN_ON(1);
		res = -EINVAL;
		goto out;
	}

	if (!fscrypt_supported_policy(&crypt_info->ci_policy, iyesde)) {
		res = -EINVAL;
		goto out;
	}

	mode = select_encryption_mode(&crypt_info->ci_policy, iyesde);
	if (IS_ERR(mode)) {
		res = PTR_ERR(mode);
		goto out;
	}
	WARN_ON(mode->ivsize > FSCRYPT_MAX_IV_SIZE);
	crypt_info->ci_mode = mode;

	res = setup_file_encryption_key(crypt_info, &master_key);
	if (res)
		goto out;

	if (cmpxchg_release(&iyesde->i_crypt_info, NULL, crypt_info) == NULL) {
		if (master_key) {
			struct fscrypt_master_key *mk =
				master_key->payload.data[0];

			refcount_inc(&mk->mk_refcount);
			crypt_info->ci_master_key = key_get(master_key);
			spin_lock(&mk->mk_decrypted_iyesdes_lock);
			list_add(&crypt_info->ci_master_key_link,
				 &mk->mk_decrypted_iyesdes);
			spin_unlock(&mk->mk_decrypted_iyesdes_lock);
		}
		crypt_info = NULL;
	}
	res = 0;
out:
	if (master_key) {
		struct fscrypt_master_key *mk = master_key->payload.data[0];

		up_read(&mk->mk_secret_sem);
		key_put(master_key);
	}
	if (res == -ENOKEY)
		res = 0;
	put_crypt_info(crypt_info);
	return res;
}
EXPORT_SYMBOL(fscrypt_get_encryption_info);

/**
 * fscrypt_put_encryption_info - free most of an iyesde's fscrypt data
 *
 * Free the iyesde's fscrypt_info.  Filesystems must call this when the iyesde is
 * being evicted.  An RCU grace period need yest have elapsed yet.
 */
void fscrypt_put_encryption_info(struct iyesde *iyesde)
{
	put_crypt_info(iyesde->i_crypt_info);
	iyesde->i_crypt_info = NULL;
}
EXPORT_SYMBOL(fscrypt_put_encryption_info);

/**
 * fscrypt_free_iyesde - free an iyesde's fscrypt data requiring RCU delay
 *
 * Free the iyesde's cached decrypted symlink target, if any.  Filesystems must
 * call this after an RCU grace period, just before they free the iyesde.
 */
void fscrypt_free_iyesde(struct iyesde *iyesde)
{
	if (IS_ENCRYPTED(iyesde) && S_ISLNK(iyesde->i_mode)) {
		kfree(iyesde->i_link);
		iyesde->i_link = NULL;
	}
}
EXPORT_SYMBOL(fscrypt_free_iyesde);

/**
 * fscrypt_drop_iyesde - check whether the iyesde's master key has been removed
 *
 * Filesystems supporting fscrypt must call this from their ->drop_iyesde()
 * method so that encrypted iyesdes are evicted as soon as they're yes longer in
 * use and their master key has been removed.
 *
 * Return: 1 if fscrypt wants the iyesde to be evicted yesw, otherwise 0
 */
int fscrypt_drop_iyesde(struct iyesde *iyesde)
{
	const struct fscrypt_info *ci = READ_ONCE(iyesde->i_crypt_info);
	const struct fscrypt_master_key *mk;

	/*
	 * If ci is NULL, then the iyesde doesn't have an encryption key set up
	 * so it's irrelevant.  If ci_master_key is NULL, then the master key
	 * was provided via the legacy mechanism of the process-subscribed
	 * keyrings, so we don't kyesw whether it's been removed or yest.
	 */
	if (!ci || !ci->ci_master_key)
		return 0;
	mk = ci->ci_master_key->payload.data[0];

	/*
	 * Note: since we aren't holding ->mk_secret_sem, the result here can
	 * immediately become outdated.  But there's yes correctness problem with
	 * unnecessarily evicting.  Nor is there a correctness problem with yest
	 * evicting while iput() is racing with the key being removed, since
	 * then the thread removing the key will either evict the iyesde itself
	 * or will correctly detect that it wasn't evicted due to the race.
	 */
	return !is_master_key_secret_present(&mk->mk_secret);
}
EXPORT_SYMBOL_GPL(fscrypt_drop_iyesde);
