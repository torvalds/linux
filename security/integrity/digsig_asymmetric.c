/*
 * Copyright (C) 2013 Intel Corporation
 *
 * Author:
 * Dmitry Kasatkin <dmitry.kasatkin@intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2 of the License.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/err.h>
#include <linux/key-type.h>
#include <crypto/public_key.h>
#include <keys/asymmetric-type.h>

#include "integrity.h"

/*
 * signature format v2 - for using with asymmetric keys
 */
struct signature_v2_hdr {
	uint8_t version;	/* signature format version */
	uint8_t	hash_algo;	/* Digest algorithm [enum pkey_hash_algo] */
	uint32_t keyid;		/* IMA key identifier - not X509/PGP specific*/
	uint16_t sig_size;	/* signature size */
	uint8_t sig[0];		/* signature payload */
} __packed;

/*
 * Request an asymmetric key.
 */
static struct key *request_asymmetric_key(struct key *keyring, uint32_t keyid)
{
	struct key *key;
	char name[12];

	sprintf(name, "id:%x", keyid);

	pr_debug("key search: \"%s\"\n", name);

	if (keyring) {
		/* search in specific keyring */
		key_ref_t kref;
		kref = keyring_search(make_key_ref(keyring, 1),
				      &key_type_asymmetric, name);
		if (IS_ERR(kref))
			key = ERR_CAST(kref);
		else
			key = key_ref_to_ptr(kref);
	} else {
		key = request_key(&key_type_asymmetric, name, NULL);
	}

	if (IS_ERR(key)) {
		pr_warn("Request for unknown key '%s' err %ld\n",
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
	struct key *key;
	int ret = -ENOMEM;

	if (siglen <= sizeof(*hdr))
		return -EBADMSG;

	siglen -= sizeof(*hdr);

	if (siglen != __be16_to_cpu(hdr->sig_size))
		return -EBADMSG;

	if (hdr->hash_algo >= PKEY_HASH__LAST)
		return -ENOPKG;

	key = request_asymmetric_key(keyring, __be32_to_cpu(hdr->keyid));
	if (IS_ERR(key))
		return PTR_ERR(key);

	memset(&pks, 0, sizeof(pks));

	pks.pkey_hash_algo = hdr->hash_algo;
	pks.digest = (u8 *)data;
	pks.digest_size = datalen;
	pks.nr_mpi = 1;
	pks.rsa.s = mpi_read_raw_data(hdr->sig, siglen);

	if (pks.rsa.s)
		ret = verify_signature(key, &pks);

	mpi_free(pks.rsa.s);
	key_put(key);
	pr_debug("%s() = %d\n", __func__, ret);
	return ret;
}
