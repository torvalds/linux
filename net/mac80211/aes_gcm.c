/*
 * Copyright 2014-2015, Qualcomm Atheros, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/crypto.h>
#include <linux/err.h>
#include <crypto/aes.h>

#include <net/mac80211.h>
#include "key.h"
#include "aes_gcm.h"

void ieee80211_aes_gcm_encrypt(struct crypto_aead *tfm, u8 *j_0, u8 *aad,
			       u8 *data, size_t data_len, u8 *mic)
{
	struct scatterlist assoc, pt, ct[2];

	char aead_req_data[sizeof(struct aead_request) +
			   crypto_aead_reqsize(tfm)]
		__aligned(__alignof__(struct aead_request));
	struct aead_request *aead_req = (void *)aead_req_data;

	memset(aead_req, 0, sizeof(aead_req_data));

	sg_init_one(&pt, data, data_len);
	sg_init_one(&assoc, &aad[2], be16_to_cpup((__be16 *)aad));
	sg_init_table(ct, 2);
	sg_set_buf(&ct[0], data, data_len);
	sg_set_buf(&ct[1], mic, IEEE80211_GCMP_MIC_LEN);

	aead_request_set_tfm(aead_req, tfm);
	aead_request_set_assoc(aead_req, &assoc, assoc.length);
	aead_request_set_crypt(aead_req, &pt, ct, data_len, j_0);

	crypto_aead_encrypt(aead_req);
}

int ieee80211_aes_gcm_decrypt(struct crypto_aead *tfm, u8 *j_0, u8 *aad,
			      u8 *data, size_t data_len, u8 *mic)
{
	struct scatterlist assoc, pt, ct[2];
	char aead_req_data[sizeof(struct aead_request) +
			   crypto_aead_reqsize(tfm)]
		__aligned(__alignof__(struct aead_request));
	struct aead_request *aead_req = (void *)aead_req_data;

	if (data_len == 0)
		return -EINVAL;

	memset(aead_req, 0, sizeof(aead_req_data));

	sg_init_one(&pt, data, data_len);
	sg_init_one(&assoc, &aad[2], be16_to_cpup((__be16 *)aad));
	sg_init_table(ct, 2);
	sg_set_buf(&ct[0], data, data_len);
	sg_set_buf(&ct[1], mic, IEEE80211_GCMP_MIC_LEN);

	aead_request_set_tfm(aead_req, tfm);
	aead_request_set_assoc(aead_req, &assoc, assoc.length);
	aead_request_set_crypt(aead_req, ct, &pt,
			       data_len + IEEE80211_GCMP_MIC_LEN, j_0);

	return crypto_aead_decrypt(aead_req);
}

struct crypto_aead *ieee80211_aes_gcm_key_setup_encrypt(const u8 key[],
							size_t key_len)
{
	struct crypto_aead *tfm;
	int err;

	tfm = crypto_alloc_aead("gcm(aes)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm))
		return tfm;

	err = crypto_aead_setkey(tfm, key, key_len);
	if (err)
		goto free_aead;
	err = crypto_aead_setauthsize(tfm, IEEE80211_GCMP_MIC_LEN);
	if (err)
		goto free_aead;

	return tfm;

free_aead:
	crypto_free_aead(tfm);
	return ERR_PTR(err);
}

void ieee80211_aes_gcm_key_free(struct crypto_aead *tfm)
{
	crypto_free_aead(tfm);
}
