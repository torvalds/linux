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

int asymmetric_verify(struct key *keyring, const char *sig,
		      int siglen, const char *data, int datalen)
{
	struct public_key_signature pks;
	struct signature_v2_hdr *hdr = (struct signature_v2_hdr *)sig;
	const struct public_key *pk;
	struct key *key;
	int ret;

	if (siglen <= sizeof(*hdr))
		return -EBADMSG;

	siglen -= sizeof(*hdr);

	if (siglen != be16_to_cpu(hdr->sig_size))
		return -EBADMSG;

	if (hdr->hash_algo >= HASH_ALGO__LAST)
		return -ENOPKG;

	key = request_asymmetric_key(keyring, be32_to_cpu(hdr->keyid));
	if (IS_ERR(key))
		return PTR_ERR(key);

	memset(&pks, 0, sizeof(pks));

	pks.hash_algo = hash_algo_name[hdr->hash_algo];

	pk = asymmetric_key_public_key(key);
	pks.pkey_algo = pk->pkey_algo;
	if (!strcmp(pk->pkey_algo, "rsa")) {
		pks.encoding = "pkcs1";
	} else if (!strncmp(pk->pkey_algo, "ecdsa-", 6)) {
		/* edcsa-nist-p192 etc. */
		pks.encoding = "x962";
	} else if (!strcmp(pk->pkey_algo, "ecrdsa") ||
		   !strcmp(pk->pkey_algo, "sm2")) {
		pks.encoding = "raw";
	} else {
		ret = -ENOPKG;
		goto out;
	}

	pks.digest = (u8 *)data;
	pks.digest_size = datalen;
	pks.s = hdr->sig;
	pks.s_size = siglen;
	ret = verify_signature(key, &pks);
out:
	key_put(key);
	pr_debug("%s() = %d\n", __func__, ret);
	return ret;
}
