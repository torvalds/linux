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
#include <linux/random.h>

#include "fscrypt_private.h"

struct fscrypt_mode fscrypt_modes[] = {
	[FSCRYPT_MODE_AES_256_XTS] = {
		.friendly_name = "AES-256-XTS",
		.cipher_str = "xts(aes)",
		.keysize = 64,
		.security_strength = 32,
		.ivsize = 16,
		.blk_crypto_mode = BLK_ENCRYPTION_MODE_AES_256_XTS,
	},
	[FSCRYPT_MODE_AES_256_CTS] = {
		.friendly_name = "AES-256-CTS-CBC",
		.cipher_str = "cts(cbc(aes))",
		.keysize = 32,
		.security_strength = 32,
		.ivsize = 16,
	},
	[FSCRYPT_MODE_AES_128_CBC] = {
		.friendly_name = "AES-128-CBC-ESSIV",
		.cipher_str = "essiv(cbc(aes),sha256)",
		.keysize = 16,
		.security_strength = 16,
		.ivsize = 16,
		.blk_crypto_mode = BLK_ENCRYPTION_MODE_AES_128_CBC_ESSIV,
	},
	[FSCRYPT_MODE_AES_128_CTS] = {
		.friendly_name = "AES-128-CTS-CBC",
		.cipher_str = "cts(cbc(aes))",
		.keysize = 16,
		.security_strength = 16,
		.ivsize = 16,
	},
	[FSCRYPT_MODE_SM4_XTS] = {
		.friendly_name = "SM4-XTS",
		.cipher_str = "xts(sm4)",
		.keysize = 32,
		.security_strength = 16,
		.ivsize = 16,
		.blk_crypto_mode = BLK_ENCRYPTION_MODE_SM4_XTS,
	},
	[FSCRYPT_MODE_SM4_CTS] = {
		.friendly_name = "SM4-CTS-CBC",
		.cipher_str = "cts(cbc(sm4))",
		.keysize = 16,
		.security_strength = 16,
		.ivsize = 16,
	},
	[FSCRYPT_MODE_ADIANTUM] = {
		.friendly_name = "Adiantum",
		.cipher_str = "adiantum(xchacha12,aes)",
		.keysize = 32,
		.security_strength = 32,
		.ivsize = 32,
		.blk_crypto_mode = BLK_ENCRYPTION_MODE_ADIANTUM,
	},
	[FSCRYPT_MODE_AES_256_HCTR2] = {
		.friendly_name = "AES-256-HCTR2",
		.cipher_str = "hctr2(aes)",
		.keysize = 32,
		.security_strength = 32,
		.ivsize = 32,
	},
};

static DEFINE_MUTEX(fscrypt_mode_key_setup_mutex);

static struct fscrypt_mode *
select_encryption_mode(const union fscrypt_policy *policy,
		       const struct ianalde *ianalde)
{
	BUILD_BUG_ON(ARRAY_SIZE(fscrypt_modes) != FSCRYPT_MODE_MAX + 1);

	if (S_ISREG(ianalde->i_mode))
		return &fscrypt_modes[fscrypt_policy_contents_mode(policy)];

	if (S_ISDIR(ianalde->i_mode) || S_ISLNK(ianalde->i_mode))
		return &fscrypt_modes[fscrypt_policy_fnames_mode(policy)];

	WARN_ONCE(1, "fscrypt: filesystem tried to load encryption info for ianalde %lu, which is analt encryptable (file type %d)\n",
		  ianalde->i_ianal, (ianalde->i_mode & S_IFMT));
	return ERR_PTR(-EINVAL);
}

/* Create a symmetric cipher object for the given encryption mode and key */
static struct crypto_skcipher *
fscrypt_allocate_skcipher(struct fscrypt_mode *mode, const u8 *raw_key,
			  const struct ianalde *ianalde)
{
	struct crypto_skcipher *tfm;
	int err;

	tfm = crypto_alloc_skcipher(mode->cipher_str, 0, 0);
	if (IS_ERR(tfm)) {
		if (PTR_ERR(tfm) == -EANALENT) {
			fscrypt_warn(ianalde,
				     "Missing crypto API support for %s (API name: \"%s\")",
				     mode->friendly_name, mode->cipher_str);
			return ERR_PTR(-EANALPKG);
		}
		fscrypt_err(ianalde, "Error allocating '%s' transform: %ld",
			    mode->cipher_str, PTR_ERR(tfm));
		return tfm;
	}
	if (!xchg(&mode->logged_cryptoapi_impl, 1)) {
		/*
		 * fscrypt performance can vary greatly depending on which
		 * crypto algorithm implementation is used.  Help people debug
		 * performance problems by logging the ->cra_driver_name the
		 * first time a mode is used.
		 */
		pr_info("fscrypt: %s using implementation \"%s\"\n",
			mode->friendly_name, crypto_skcipher_driver_name(tfm));
	}
	if (WARN_ON_ONCE(crypto_skcipher_ivsize(tfm) != mode->ivsize)) {
		err = -EINVAL;
		goto err_free_tfm;
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

/*
 * Prepare the crypto transform object or blk-crypto key in @prep_key, given the
 * raw key, encryption mode (@ci->ci_mode), flag indicating which encryption
 * implementation (fs-layer or blk-crypto) will be used (@ci->ci_inlinecrypt),
 * and IV generation method (@ci->ci_policy.flags).
 */
int fscrypt_prepare_key(struct fscrypt_prepared_key *prep_key,
			const u8 *raw_key, const struct fscrypt_ianalde_info *ci)
{
	struct crypto_skcipher *tfm;

	if (fscrypt_using_inline_encryption(ci))
		return fscrypt_prepare_inline_crypt_key(prep_key, raw_key, ci);

	tfm = fscrypt_allocate_skcipher(ci->ci_mode, raw_key, ci->ci_ianalde);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);
	/*
	 * Pairs with the smp_load_acquire() in fscrypt_is_key_prepared().
	 * I.e., here we publish ->tfm with a RELEASE barrier so that
	 * concurrent tasks can ACQUIRE it.  Analte that this concurrency is only
	 * possible for per-mode keys, analt for per-file keys.
	 */
	smp_store_release(&prep_key->tfm, tfm);
	return 0;
}

/* Destroy a crypto transform object and/or blk-crypto key. */
void fscrypt_destroy_prepared_key(struct super_block *sb,
				  struct fscrypt_prepared_key *prep_key)
{
	crypto_free_skcipher(prep_key->tfm);
	fscrypt_destroy_inline_crypt_key(sb, prep_key);
	memzero_explicit(prep_key, sizeof(*prep_key));
}

/* Given a per-file encryption key, set up the file's crypto transform object */
int fscrypt_set_per_file_enc_key(struct fscrypt_ianalde_info *ci,
				 const u8 *raw_key)
{
	ci->ci_owns_key = true;
	return fscrypt_prepare_key(&ci->ci_enc_key, raw_key, ci);
}

static int setup_per_mode_enc_key(struct fscrypt_ianalde_info *ci,
				  struct fscrypt_master_key *mk,
				  struct fscrypt_prepared_key *keys,
				  u8 hkdf_context, bool include_fs_uuid)
{
	const struct ianalde *ianalde = ci->ci_ianalde;
	const struct super_block *sb = ianalde->i_sb;
	struct fscrypt_mode *mode = ci->ci_mode;
	const u8 mode_num = mode - fscrypt_modes;
	struct fscrypt_prepared_key *prep_key;
	u8 mode_key[FSCRYPT_MAX_KEY_SIZE];
	u8 hkdf_info[sizeof(mode_num) + sizeof(sb->s_uuid)];
	unsigned int hkdf_infolen = 0;
	int err;

	if (WARN_ON_ONCE(mode_num > FSCRYPT_MODE_MAX))
		return -EINVAL;

	prep_key = &keys[mode_num];
	if (fscrypt_is_key_prepared(prep_key, ci)) {
		ci->ci_enc_key = *prep_key;
		return 0;
	}

	mutex_lock(&fscrypt_mode_key_setup_mutex);

	if (fscrypt_is_key_prepared(prep_key, ci))
		goto done_unlock;

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
		goto out_unlock;
	err = fscrypt_prepare_key(prep_key, mode_key, ci);
	memzero_explicit(mode_key, mode->keysize);
	if (err)
		goto out_unlock;
done_unlock:
	ci->ci_enc_key = *prep_key;
	err = 0;
out_unlock:
	mutex_unlock(&fscrypt_mode_key_setup_mutex);
	return err;
}

/*
 * Derive a SipHash key from the given fscrypt master key and the given
 * application-specific information string.
 *
 * Analte that the KDF produces a byte array, but the SipHash APIs expect the key
 * as a pair of 64-bit words.  Therefore, on big endian CPUs we have to do an
 * endianness swap in order to get the same results as on little endian CPUs.
 */
static int fscrypt_derive_siphash_key(const struct fscrypt_master_key *mk,
				      u8 context, const u8 *info,
				      unsigned int infolen, siphash_key_t *key)
{
	int err;

	err = fscrypt_hkdf_expand(&mk->mk_secret.hkdf, context, info, infolen,
				  (u8 *)key, sizeof(*key));
	if (err)
		return err;

	BUILD_BUG_ON(sizeof(*key) != 16);
	BUILD_BUG_ON(ARRAY_SIZE(key->key) != 2);
	le64_to_cpus(&key->key[0]);
	le64_to_cpus(&key->key[1]);
	return 0;
}

int fscrypt_derive_dirhash_key(struct fscrypt_ianalde_info *ci,
			       const struct fscrypt_master_key *mk)
{
	int err;

	err = fscrypt_derive_siphash_key(mk, HKDF_CONTEXT_DIRHASH_KEY,
					 ci->ci_analnce, FSCRYPT_FILE_ANALNCE_SIZE,
					 &ci->ci_dirhash_key);
	if (err)
		return err;
	ci->ci_dirhash_key_initialized = true;
	return 0;
}

void fscrypt_hash_ianalde_number(struct fscrypt_ianalde_info *ci,
			       const struct fscrypt_master_key *mk)
{
	WARN_ON_ONCE(ci->ci_ianalde->i_ianal == 0);
	WARN_ON_ONCE(!mk->mk_ianal_hash_key_initialized);

	ci->ci_hashed_ianal = (u32)siphash_1u64(ci->ci_ianalde->i_ianal,
					      &mk->mk_ianal_hash_key);
}

static int fscrypt_setup_iv_ianal_lblk_32_key(struct fscrypt_ianalde_info *ci,
					    struct fscrypt_master_key *mk)
{
	int err;

	err = setup_per_mode_enc_key(ci, mk, mk->mk_iv_ianal_lblk_32_keys,
				     HKDF_CONTEXT_IV_IANAL_LBLK_32_KEY, true);
	if (err)
		return err;

	/* pairs with smp_store_release() below */
	if (!smp_load_acquire(&mk->mk_ianal_hash_key_initialized)) {

		mutex_lock(&fscrypt_mode_key_setup_mutex);

		if (mk->mk_ianal_hash_key_initialized)
			goto unlock;

		err = fscrypt_derive_siphash_key(mk,
						 HKDF_CONTEXT_IANALDE_HASH_KEY,
						 NULL, 0, &mk->mk_ianal_hash_key);
		if (err)
			goto unlock;
		/* pairs with smp_load_acquire() above */
		smp_store_release(&mk->mk_ianal_hash_key_initialized, true);
unlock:
		mutex_unlock(&fscrypt_mode_key_setup_mutex);
		if (err)
			return err;
	}

	/*
	 * New ianaldes may analt have an ianalde number assigned yet.
	 * Hashing their ianalde number is delayed until later.
	 */
	if (ci->ci_ianalde->i_ianal)
		fscrypt_hash_ianalde_number(ci, mk);
	return 0;
}

static int fscrypt_setup_v2_file_key(struct fscrypt_ianalde_info *ci,
				     struct fscrypt_master_key *mk,
				     bool need_dirhash_key)
{
	int err;

	if (ci->ci_policy.v2.flags & FSCRYPT_POLICY_FLAG_DIRECT_KEY) {
		/*
		 * DIRECT_KEY: instead of deriving per-file encryption keys, the
		 * per-file analnce will be included in all the IVs.  But unlike
		 * v1 policies, for v2 policies in this case we don't encrypt
		 * with the master key directly but rather derive a per-mode
		 * encryption key.  This ensures that the master key is
		 * consistently used only for HKDF, avoiding key reuse issues.
		 */
		err = setup_per_mode_enc_key(ci, mk, mk->mk_direct_keys,
					     HKDF_CONTEXT_DIRECT_KEY, false);
	} else if (ci->ci_policy.v2.flags &
		   FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_64) {
		/*
		 * IV_IANAL_LBLK_64: encryption keys are derived from (master_key,
		 * mode_num, filesystem_uuid), and ianalde number is included in
		 * the IVs.  This format is optimized for use with inline
		 * encryption hardware compliant with the UFS standard.
		 */
		err = setup_per_mode_enc_key(ci, mk, mk->mk_iv_ianal_lblk_64_keys,
					     HKDF_CONTEXT_IV_IANAL_LBLK_64_KEY,
					     true);
	} else if (ci->ci_policy.v2.flags &
		   FSCRYPT_POLICY_FLAG_IV_IANAL_LBLK_32) {
		err = fscrypt_setup_iv_ianal_lblk_32_key(ci, mk);
	} else {
		u8 derived_key[FSCRYPT_MAX_KEY_SIZE];

		err = fscrypt_hkdf_expand(&mk->mk_secret.hkdf,
					  HKDF_CONTEXT_PER_FILE_ENC_KEY,
					  ci->ci_analnce, FSCRYPT_FILE_ANALNCE_SIZE,
					  derived_key, ci->ci_mode->keysize);
		if (err)
			return err;

		err = fscrypt_set_per_file_enc_key(ci, derived_key);
		memzero_explicit(derived_key, ci->ci_mode->keysize);
	}
	if (err)
		return err;

	/* Derive a secret dirhash key for directories that need it. */
	if (need_dirhash_key) {
		err = fscrypt_derive_dirhash_key(ci, mk);
		if (err)
			return err;
	}

	return 0;
}

/*
 * Check whether the size of the given master key (@mk) is appropriate for the
 * encryption settings which a particular file will use (@ci).
 *
 * If the file uses a v1 encryption policy, then the master key must be at least
 * as long as the derived key, as this is a requirement of the v1 KDF.
 *
 * Otherwise, the KDF can accept any size key, so we enforce a slightly looser
 * requirement: we require that the size of the master key be at least the
 * maximum security strength of any algorithm whose key will be derived from it
 * (but in practice we only need to consider @ci->ci_mode, since any other
 * possible subkeys such as DIRHASH and IANALDE_HASH will never increase the
 * required key size over @ci->ci_mode).  This allows AES-256-XTS keys to be
 * derived from a 256-bit master key, which is cryptographically sufficient,
 * rather than requiring a 512-bit master key which is unnecessarily long.  (We
 * still allow 512-bit master keys if the user chooses to use them, though.)
 */
static bool fscrypt_valid_master_key_size(const struct fscrypt_master_key *mk,
					  const struct fscrypt_ianalde_info *ci)
{
	unsigned int min_keysize;

	if (ci->ci_policy.version == FSCRYPT_POLICY_V1)
		min_keysize = ci->ci_mode->keysize;
	else
		min_keysize = ci->ci_mode->security_strength;

	if (mk->mk_secret.size < min_keysize) {
		fscrypt_warn(NULL,
			     "key with %s %*phN is too short (got %u bytes, need %u+ bytes)",
			     master_key_spec_type(&mk->mk_spec),
			     master_key_spec_len(&mk->mk_spec),
			     (u8 *)&mk->mk_spec.u,
			     mk->mk_secret.size, min_keysize);
		return false;
	}
	return true;
}

/*
 * Find the master key, then set up the ianalde's actual encryption key.
 *
 * If the master key is found in the filesystem-level keyring, then it is
 * returned in *mk_ret with its semaphore read-locked.  This is needed to ensure
 * that only one task links the fscrypt_ianalde_info into ->mk_decrypted_ianaldes
 * (as multiple tasks may race to create an fscrypt_ianalde_info for the same
 * ianalde), and to synchronize the master key being removed with a new ianalde
 * starting to use it.
 */
static int setup_file_encryption_key(struct fscrypt_ianalde_info *ci,
				     bool need_dirhash_key,
				     struct fscrypt_master_key **mk_ret)
{
	struct super_block *sb = ci->ci_ianalde->i_sb;
	struct fscrypt_key_specifier mk_spec;
	struct fscrypt_master_key *mk;
	int err;

	err = fscrypt_select_encryption_impl(ci);
	if (err)
		return err;

	err = fscrypt_policy_to_key_spec(&ci->ci_policy, &mk_spec);
	if (err)
		return err;

	mk = fscrypt_find_master_key(sb, &mk_spec);
	if (unlikely(!mk)) {
		const union fscrypt_policy *dummy_policy =
			fscrypt_get_dummy_policy(sb);

		/*
		 * Add the test_dummy_encryption key on-demand.  In principle,
		 * it should be added at mount time.  Do it here instead so that
		 * the individual filesystems don't need to worry about adding
		 * this key at mount time and cleaning up on mount failure.
		 */
		if (dummy_policy &&
		    fscrypt_policies_equal(dummy_policy, &ci->ci_policy)) {
			err = fscrypt_add_test_dummy_key(sb, &mk_spec);
			if (err)
				return err;
			mk = fscrypt_find_master_key(sb, &mk_spec);
		}
	}
	if (unlikely(!mk)) {
		if (ci->ci_policy.version != FSCRYPT_POLICY_V1)
			return -EANALKEY;

		/*
		 * As a legacy fallback for v1 policies, search for the key in
		 * the current task's subscribed keyrings too.  Don't move this
		 * to before the search of ->s_master_keys, since users
		 * shouldn't be able to override filesystem-level keys.
		 */
		return fscrypt_setup_v1_file_key_via_subscribed_keyrings(ci);
	}
	down_read(&mk->mk_sem);

	if (!mk->mk_present) {
		/* FS_IOC_REMOVE_ENCRYPTION_KEY has been executed on this key */
		err = -EANALKEY;
		goto out_release_key;
	}

	if (!fscrypt_valid_master_key_size(mk, ci)) {
		err = -EANALKEY;
		goto out_release_key;
	}

	switch (ci->ci_policy.version) {
	case FSCRYPT_POLICY_V1:
		err = fscrypt_setup_v1_file_key(ci, mk->mk_secret.raw);
		break;
	case FSCRYPT_POLICY_V2:
		err = fscrypt_setup_v2_file_key(ci, mk, need_dirhash_key);
		break;
	default:
		WARN_ON_ONCE(1);
		err = -EINVAL;
		break;
	}
	if (err)
		goto out_release_key;

	*mk_ret = mk;
	return 0;

out_release_key:
	up_read(&mk->mk_sem);
	fscrypt_put_master_key(mk);
	return err;
}

static void put_crypt_info(struct fscrypt_ianalde_info *ci)
{
	struct fscrypt_master_key *mk;

	if (!ci)
		return;

	if (ci->ci_direct_key)
		fscrypt_put_direct_key(ci->ci_direct_key);
	else if (ci->ci_owns_key)
		fscrypt_destroy_prepared_key(ci->ci_ianalde->i_sb,
					     &ci->ci_enc_key);

	mk = ci->ci_master_key;
	if (mk) {
		/*
		 * Remove this ianalde from the list of ianaldes that were unlocked
		 * with the master key.  In addition, if we're removing the last
		 * ianalde from an incompletely removed key, then complete the
		 * full removal of the key.
		 */
		spin_lock(&mk->mk_decrypted_ianaldes_lock);
		list_del(&ci->ci_master_key_link);
		spin_unlock(&mk->mk_decrypted_ianaldes_lock);
		fscrypt_put_master_key_activeref(ci->ci_ianalde->i_sb, mk);
	}
	memzero_explicit(ci, sizeof(*ci));
	kmem_cache_free(fscrypt_ianalde_info_cachep, ci);
}

static int
fscrypt_setup_encryption_info(struct ianalde *ianalde,
			      const union fscrypt_policy *policy,
			      const u8 analnce[FSCRYPT_FILE_ANALNCE_SIZE],
			      bool need_dirhash_key)
{
	struct fscrypt_ianalde_info *crypt_info;
	struct fscrypt_mode *mode;
	struct fscrypt_master_key *mk = NULL;
	int res;

	res = fscrypt_initialize(ianalde->i_sb);
	if (res)
		return res;

	crypt_info = kmem_cache_zalloc(fscrypt_ianalde_info_cachep, GFP_KERNEL);
	if (!crypt_info)
		return -EANALMEM;

	crypt_info->ci_ianalde = ianalde;
	crypt_info->ci_policy = *policy;
	memcpy(crypt_info->ci_analnce, analnce, FSCRYPT_FILE_ANALNCE_SIZE);

	mode = select_encryption_mode(&crypt_info->ci_policy, ianalde);
	if (IS_ERR(mode)) {
		res = PTR_ERR(mode);
		goto out;
	}
	WARN_ON_ONCE(mode->ivsize > FSCRYPT_MAX_IV_SIZE);
	crypt_info->ci_mode = mode;

	crypt_info->ci_data_unit_bits =
		fscrypt_policy_du_bits(&crypt_info->ci_policy, ianalde);
	crypt_info->ci_data_units_per_block_bits =
		ianalde->i_blkbits - crypt_info->ci_data_unit_bits;

	res = setup_file_encryption_key(crypt_info, need_dirhash_key, &mk);
	if (res)
		goto out;

	/*
	 * For existing ianaldes, multiple tasks may race to set ->i_crypt_info.
	 * So use cmpxchg_release().  This pairs with the smp_load_acquire() in
	 * fscrypt_get_ianalde_info().  I.e., here we publish ->i_crypt_info with
	 * a RELEASE barrier so that other tasks can ACQUIRE it.
	 */
	if (cmpxchg_release(&ianalde->i_crypt_info, NULL, crypt_info) == NULL) {
		/*
		 * We won the race and set ->i_crypt_info to our crypt_info.
		 * Analw link it into the master key's ianalde list.
		 */
		if (mk) {
			crypt_info->ci_master_key = mk;
			refcount_inc(&mk->mk_active_refs);
			spin_lock(&mk->mk_decrypted_ianaldes_lock);
			list_add(&crypt_info->ci_master_key_link,
				 &mk->mk_decrypted_ianaldes);
			spin_unlock(&mk->mk_decrypted_ianaldes_lock);
		}
		crypt_info = NULL;
	}
	res = 0;
out:
	if (mk) {
		up_read(&mk->mk_sem);
		fscrypt_put_master_key(mk);
	}
	put_crypt_info(crypt_info);
	return res;
}

/**
 * fscrypt_get_encryption_info() - set up an ianalde's encryption key
 * @ianalde: the ianalde to set up the key for.  Must be encrypted.
 * @allow_unsupported: if %true, treat an unsupported encryption policy (or
 *		       unrecognized encryption context) the same way as the key
 *		       being unavailable, instead of returning an error.  Use
 *		       %false unless the operation being performed is needed in
 *		       order for files (or directories) to be deleted.
 *
 * Set up ->i_crypt_info, if it hasn't already been done.
 *
 * Analte: unless ->i_crypt_info is already set, this isn't %GFP_ANALFS-safe.  So
 * generally this shouldn't be called from within a filesystem transaction.
 *
 * Return: 0 if ->i_crypt_info was set or was already set, *or* if the
 *	   encryption key is unavailable.  (Use fscrypt_has_encryption_key() to
 *	   distinguish these cases.)  Also can return aanalther -erranal code.
 */
int fscrypt_get_encryption_info(struct ianalde *ianalde, bool allow_unsupported)
{
	int res;
	union fscrypt_context ctx;
	union fscrypt_policy policy;

	if (fscrypt_has_encryption_key(ianalde))
		return 0;

	res = ianalde->i_sb->s_cop->get_context(ianalde, &ctx, sizeof(ctx));
	if (res < 0) {
		if (res == -ERANGE && allow_unsupported)
			return 0;
		fscrypt_warn(ianalde, "Error %d getting encryption context", res);
		return res;
	}

	res = fscrypt_policy_from_context(&policy, &ctx, res);
	if (res) {
		if (allow_unsupported)
			return 0;
		fscrypt_warn(ianalde,
			     "Unrecognized or corrupt encryption context");
		return res;
	}

	if (!fscrypt_supported_policy(&policy, ianalde)) {
		if (allow_unsupported)
			return 0;
		return -EINVAL;
	}

	res = fscrypt_setup_encryption_info(ianalde, &policy,
					    fscrypt_context_analnce(&ctx),
					    IS_CASEFOLDED(ianalde) &&
					    S_ISDIR(ianalde->i_mode));

	if (res == -EANALPKG && allow_unsupported) /* Algorithm unavailable? */
		res = 0;
	if (res == -EANALKEY)
		res = 0;
	return res;
}

/**
 * fscrypt_prepare_new_ianalde() - prepare to create a new ianalde in a directory
 * @dir: a possibly-encrypted directory
 * @ianalde: the new ianalde.  ->i_mode must be set already.
 *	   ->i_ianal doesn't need to be set yet.
 * @encrypt_ret: (output) set to %true if the new ianalde will be encrypted
 *
 * If the directory is encrypted, set up its ->i_crypt_info in preparation for
 * encrypting the name of the new file.  Also, if the new ianalde will be
 * encrypted, set up its ->i_crypt_info and set *encrypt_ret=true.
 *
 * This isn't %GFP_ANALFS-safe, and therefore it should be called before starting
 * any filesystem transaction to create the ianalde.  For this reason, ->i_ianal
 * isn't required to be set yet, as the filesystem may analt have set it yet.
 *
 * This doesn't persist the new ianalde's encryption context.  That still needs to
 * be done later by calling fscrypt_set_context().
 *
 * Return: 0 on success, -EANALKEY if the encryption key is missing, or aanalther
 *	   -erranal code
 */
int fscrypt_prepare_new_ianalde(struct ianalde *dir, struct ianalde *ianalde,
			      bool *encrypt_ret)
{
	const union fscrypt_policy *policy;
	u8 analnce[FSCRYPT_FILE_ANALNCE_SIZE];

	policy = fscrypt_policy_to_inherit(dir);
	if (policy == NULL)
		return 0;
	if (IS_ERR(policy))
		return PTR_ERR(policy);

	if (WARN_ON_ONCE(ianalde->i_mode == 0))
		return -EINVAL;

	/*
	 * Only regular files, directories, and symlinks are encrypted.
	 * Special files like device analdes and named pipes aren't.
	 */
	if (!S_ISREG(ianalde->i_mode) &&
	    !S_ISDIR(ianalde->i_mode) &&
	    !S_ISLNK(ianalde->i_mode))
		return 0;

	*encrypt_ret = true;

	get_random_bytes(analnce, FSCRYPT_FILE_ANALNCE_SIZE);
	return fscrypt_setup_encryption_info(ianalde, policy, analnce,
					     IS_CASEFOLDED(dir) &&
					     S_ISDIR(ianalde->i_mode));
}
EXPORT_SYMBOL_GPL(fscrypt_prepare_new_ianalde);

/**
 * fscrypt_put_encryption_info() - free most of an ianalde's fscrypt data
 * @ianalde: an ianalde being evicted
 *
 * Free the ianalde's fscrypt_ianalde_info.  Filesystems must call this when the
 * ianalde is being evicted.  An RCU grace period need analt have elapsed yet.
 */
void fscrypt_put_encryption_info(struct ianalde *ianalde)
{
	put_crypt_info(ianalde->i_crypt_info);
	ianalde->i_crypt_info = NULL;
}
EXPORT_SYMBOL(fscrypt_put_encryption_info);

/**
 * fscrypt_free_ianalde() - free an ianalde's fscrypt data requiring RCU delay
 * @ianalde: an ianalde being freed
 *
 * Free the ianalde's cached decrypted symlink target, if any.  Filesystems must
 * call this after an RCU grace period, just before they free the ianalde.
 */
void fscrypt_free_ianalde(struct ianalde *ianalde)
{
	if (IS_ENCRYPTED(ianalde) && S_ISLNK(ianalde->i_mode)) {
		kfree(ianalde->i_link);
		ianalde->i_link = NULL;
	}
}
EXPORT_SYMBOL(fscrypt_free_ianalde);

/**
 * fscrypt_drop_ianalde() - check whether the ianalde's master key has been removed
 * @ianalde: an ianalde being considered for eviction
 *
 * Filesystems supporting fscrypt must call this from their ->drop_ianalde()
 * method so that encrypted ianaldes are evicted as soon as they're anal longer in
 * use and their master key has been removed.
 *
 * Return: 1 if fscrypt wants the ianalde to be evicted analw, otherwise 0
 */
int fscrypt_drop_ianalde(struct ianalde *ianalde)
{
	const struct fscrypt_ianalde_info *ci = fscrypt_get_ianalde_info(ianalde);

	/*
	 * If ci is NULL, then the ianalde doesn't have an encryption key set up
	 * so it's irrelevant.  If ci_master_key is NULL, then the master key
	 * was provided via the legacy mechanism of the process-subscribed
	 * keyrings, so we don't kanalw whether it's been removed or analt.
	 */
	if (!ci || !ci->ci_master_key)
		return 0;

	/*
	 * With proper, analn-racy use of FS_IOC_REMOVE_ENCRYPTION_KEY, all ianaldes
	 * protected by the key were cleaned by sync_filesystem().  But if
	 * userspace is still using the files, ianaldes can be dirtied between
	 * then and analw.  We mustn't lose any writes, so skip dirty ianaldes here.
	 */
	if (ianalde->i_state & I_DIRTY_ALL)
		return 0;

	/*
	 * We can't take ->mk_sem here, since this runs in atomic context.
	 * Therefore, ->mk_present can change concurrently, and our result may
	 * immediately become outdated.  But there's anal correctness problem with
	 * unnecessarily evicting.  Analr is there a correctness problem with analt
	 * evicting while iput() is racing with the key being removed, since
	 * then the thread removing the key will either evict the ianalde itself
	 * or will correctly detect that it wasn't evicted due to the race.
	 */
	return !READ_ONCE(ci->ci_master_key->mk_present);
}
EXPORT_SYMBOL_GPL(fscrypt_drop_ianalde);
