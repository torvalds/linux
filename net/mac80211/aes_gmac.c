/*
 * AES-GMAC for IEEE 802.11 BIP-GMAC-128 and BIP-GMAC-256
 * Copyright 2015, Qualcomm Atheros, Inc.
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
#include "aes_gmac.h"

#define GMAC_MIC_LEN 16
#define GMAC_NONCE_LEN 12
#define AAD_LEN 20

int ieee80211_aes_gmac(struct crypto_aead *tfm, const u8 *aad, u8 *nonce,
		       const u8 *data, size_t data_len, u8 *mic)
{
	struct scatterlist sg[3], ct[1];
	char aead_req_data[sizeof(struct aead_request) +
			   crypto_aead_reqsize(tfm)]
		__aligned(__alignof__(struct aead_request));
	struct aead_request *aead_req = (void *)aead_req_data;
	u8 zero[GMAC_MIC_LEN], iv[AES_BLOCK_SIZE];

	if (data_len < GMAC_MIC_LEN)
		return -EINVAL;

	memset(aead_req, 0, sizeof(aead_req_data));

	memset(zero, 0, GMAC_MIC_LEN);
	sg_init_table(sg, 3);
	sg_set_buf(&sg[0], aad, AAD_LEN);
	sg_set_buf(&sg[1], data, data_len - GMAC_MIC_LEN);
	sg_set_buf(&sg[2], zero, GMAC_MIC_LEN);

	memcpy(iv, nonce, GMAC_NONCE_LEN);
	memset(iv + GMAC_NONCE_LEN, 0, sizeof(iv) - GMAC_NONCE_LEN);
	iv[AES_BLOCK_SIZE - 1] = 0x01;

	sg_init_table(ct, 1);
	sg_set_buf(&ct[0], mic, GMAC_MIC_LEN);

	aead_request_set_tfm(aead_req, tfm);
	aead_request_set_assoc(aead_req, sg, AAD_LEN + data_len);
	aead_request_set_crypt(aead_req, NULL, ct, 0, iv);

	crypto_aead_encrypt(aead_req);

	return 0;
}

struct crypto_aead *ieee80211_aes_gmac_key_setup(const u8 key[],
						 size_t key_len)
{
	struct crypto_aead *tfm;
	int err;

	tfm = crypto_alloc_aead("gcm(aes)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm))
		return tfm;

	err = crypto_aead_setkey(tfm, key, key_len);
	if (!err)
		err = crypto_aead_setauthsize(tfm, GMAC_MIC_LEN);
	if (!err)
		return tfm;

	crypto_free_aead(tfm);
	return ERR_PTR(err);
}

void ieee80211_aes_gmac_key_free(struct crypto_aead *tfm)
{
	crypto_free_aead(tfm);
}
