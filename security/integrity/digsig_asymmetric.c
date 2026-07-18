// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2013 Intel Corporation
 *
 * Author:
 * Dmitry Kasatkin <dmitry.kasatkin@intel.com>
 */

#include <linux/err.h>
#include <linux/ratelimit.h>
#include <linux/key-type.h>
#include <crypto/public_key.h>
#include <crypto/hash_info.h>
#include <keys/asymmetric-type.h>
#include <keys/system_keyring.h>

#include "integrity.h"

/*
 * Request an asymmetric key.
 */
static struct key *request_asymmetric_key(struct key *keyring, uint32_t keyid)
{
	struct key *key;
	char name[12];

	sprintf(name, "id:%08x", keyid);

	pr_debug("key search: \"%s\"\n", name);

	key = get_ima_blacklist_keyring();
	if (key) {
		key_ref_t kref;

		kref = keyring_search(make_key_ref(key, 1),
				      &key_type_asymmetric, name, true);
		if (!IS_ERR(kref)) {
			pr_err("Key '%s' is in ima_blacklist_keyring\n", name);
			return ERR_PTR(-EKEYREJECTED);
		}
	}

	if (keyring) {
		/* search in specific keyring */
		key_ref_t kref;

		kref = keyring_search(make_key_ref(keyring, 1),
				      &key_type_asymmetric, name, true);
		if (IS_ERR(kref))
			key = ERR_CAST(kref);
		else
			key = key_ref_to_ptr(kref);
	} else {
		key = request_key(&key_type_asymmetric, name, NULL);
	}

	if (IS_ERR(key)) {
		if (keyring)
			pr_err_ratelimited("Request for unknown key '%s' in '%s' keyring. err %ld\n",
					   name, keyring->description,
					   PTR_ERR(key));
		else
			pr_err_ratelimited("Request for unknown key '%s' err %ld\n",
					   name, PTR_ERR(key));

		switch (PTR_ERR(key)) {
			/* Hide some search errors */
		case -EACCES:
		case -ENOTDIR:
		case -EAGAIN:
			return ERR_PTR(-ENOKEY);
		default:
			return key;
		}
	}

	pr_debug("%s() = 0 [%x]\n", __func__, key_serial(key));

	return key;
}

/**
 * asymmetric_verify_common -- sigv2 and sigv3 common verify function
 * @key: The key to use for signature verification; caller must free it
 * @pk: The associated public key; must not be NULL
 * @sig: The xattr signature
 * @siglen: The length of the xattr signature; must be at least
 *          sizeof(struct signature_v2_hdr)
 * @data: The data to verify the signature on
 * @datalen: Length of @data
 */
static int asymmetric_verify_common(const struct key *key,
				    const struct public_key *pk,
				    const char *sig, int siglen,
				    const char *data, int datalen)
{
	struct signature_v2_hdr *hdr = (struct signature_v2_hdr *)sig;
	struct public_key_signature pks;
	int ret;

	siglen -= sizeof(*hdr);

	if (siglen != be16_to_cpu(hdr->sig_size))
		return -EBADMSG;

	if (hdr->hash_algo >= HASH_ALGO__LAST)
		return -ENOPKG;

	memset(&pks, 0, sizeof(pks));

	pks.hash_algo = hash_algo_name[hdr->hash_algo];
	pks.pkey_algo = pk->pkey_algo;
	if (!strcmp(pk->pkey_algo, "rsa")) {
		pks.encoding = "pkcs1";
	} else if (!strncmp(pk->pkey_algo, "ecdsa-", 6)) {
		/* edcsa-nist-p192 etc. */
		pks.encoding = "x962";
	} else if (!strcmp(pk->pkey_algo, "ecrdsa")) {
		pks.encoding = "raw";
	} else {
		ret = -ENOPKG;
		goto out;
	}

	pks.m = (u8 *)data;
	pks.m_size = datalen;
	pks.s = hdr->sig;
	pks.s_size = siglen;
	ret = verify_signature(key, &pks);
out:
	pr_debug("%s() = %d\n", __func__, ret);
	return ret;
}

int asymmetric_verify(struct key *keyring, const char *sig,
		      int siglen, const char *data, int datalen)
{
	struct signature_v2_hdr *hdr = (struct signature_v2_hdr *)sig;
	const struct public_key *pk;
	struct key *key;
	int ret;

	if (siglen <= sizeof(*hdr))
		return -EBADMSG;

	key = request_asymmetric_key(keyring, be32_to_cpu(hdr->keyid));
	if (IS_ERR(key))
		return PTR_ERR(key);
	pk = asymmetric_key_public_key(key);
	if (!pk) {
		ret = -ENOKEY;
		goto out;
	}

	ret = asymmetric_verify_common(key, pk, sig, siglen, data, datalen);

out:
	key_put(key);

	return ret;
}

/*
 * calc_file_id_hash - calculate the hash of the ima_file_id struct data
 * @type: xattr type [enum evm_ima_xattr_type]
 * @algo: hash algorithm [enum hash_algo]; caller must ensure valid value
 * @digest: pointer to the digest to be hashed
 * @hash: (out) pointer to the hash
 *
 * IMA signature version 3 disambiguates the data that is signed by
 * indirectly signing the hash of the ima_file_id structure data.
 *
 * Return 0 on success, error code otherwise.
 */
static int calc_file_id_hash(enum evm_ima_xattr_type type,
			     enum hash_algo algo, const u8 *digest,
			     struct ima_max_digest_data *hash)
{
	struct ima_file_id file_id = {.hash_type = type, .hash_algorithm = algo};
	size_t digest_size = hash_digest_size[algo];
	struct crypto_shash *tfm;
	size_t file_id_size;
	int rc;

	if (type != IMA_VERITY_DIGSIG && type != EVM_IMA_XATTR_DIGSIG &&
	    type != EVM_XATTR_PORTABLE_DIGSIG)
		return -EINVAL;

	tfm = crypto_alloc_shash(hash_algo_name[algo], 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);

	memcpy(file_id.hash, digest, digest_size);

	/* Calculate the ima_file_id struct hash on the portion used. */
	file_id_size = sizeof(file_id) - (HASH_MAX_DIGESTSIZE - digest_size);

	hash->hdr.algo = algo;
	hash->hdr.length = digest_size;
	rc = crypto_shash_tfm_digest(tfm, (const u8 *)&file_id, file_id_size,
				     hash->digest);

	crypto_free_shash(tfm);
	return rc;
}

/**
 * asymmetric_verify_v3_hashless - Use hashless signature verification on sigv3
 * @key: The key to use for signature verification; caller must free it
 * @pk: The associated public key; must not be NULL
 * @encoding: The encoding the key type uses
 * @sig: The xattr signature
 * @siglen: The length of the xattr signature; must be at least
 *          sizeof(struct signature_v2_hdr)
 * @algo: hash algorithm [enum hash_algo]; caller must ensure valid value
 * @digest: The file digest
 *
 * Create an ima_file_id structure and use it for signature verification
 * directly. This can be used for ML-DSA in pure mode for example.
 */
static int asymmetric_verify_v3_hashless(struct key *key,
					 const struct public_key *pk,
					 const char *encoding,
					 const char *sig, int siglen,
					 u8 algo,
					 const u8 *digest)
{
	struct signature_v2_hdr *hdr = (struct signature_v2_hdr *)sig;
	struct ima_file_id file_id = {
		.hash_type = hdr->type,
		.hash_algorithm = algo,
	};
	size_t digest_size = hash_digest_size[algo];
	struct public_key_signature pks = {
		.m = (u8 *)&file_id,
		.m_size = sizeof(file_id) - (HASH_MAX_DIGESTSIZE - digest_size),
		.s = hdr->sig,
		.s_size = siglen - sizeof(*hdr),
		.pkey_algo = pk->pkey_algo,
		.hash_algo = "none",
		.encoding = encoding,
	};
	int ret;

	if (hdr->type != IMA_VERITY_DIGSIG &&
	    hdr->type != EVM_IMA_XATTR_DIGSIG &&
	    hdr->type != EVM_XATTR_PORTABLE_DIGSIG)
		return -EINVAL;

	if (pks.s_size != be16_to_cpu(hdr->sig_size))
		return -EBADMSG;

	memcpy(file_id.hash, digest, digest_size);

	ret = verify_signature(key, &pks);
	pr_debug("%s() = %d\n", __func__, ret);
	return ret;
}

int asymmetric_verify_v3(struct key *keyring, const char *sig, int siglen,
			 const char *data, int datalen, u8 algo)
{
	struct signature_v2_hdr *hdr = (struct signature_v2_hdr *)sig;
	struct ima_max_digest_data hash;
	const struct public_key *pk;
	struct key *key;
	int rc;

	if (algo >= HASH_ALGO__LAST)
		return -ENOPKG;

	if (siglen <= sizeof(*hdr))
		return -EBADMSG;

	key = request_asymmetric_key(keyring, be32_to_cpu(hdr->keyid));
	if (IS_ERR(key))
		return PTR_ERR(key);

	pk = asymmetric_key_public_key(key);
	if (!pk) {
		rc = -ENOKEY;
		goto out;
	}
	if (!strncmp(pk->pkey_algo, "mldsa", 5)) {
		rc = asymmetric_verify_v3_hashless(key, pk, "raw",
						   sig, siglen, algo, data);
	} else {
		rc = calc_file_id_hash(hdr->type, algo, data, &hash);
		if (rc) {
			rc = -EINVAL;
			goto out;
		}

		rc = asymmetric_verify_common(key, pk, sig, siglen, hash.digest,
					      hash.hdr.length);
	}

out:
	key_put(key);

	return rc;
}
