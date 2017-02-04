/*
 * FILS AEAD for (Re)Association Request/Response frames
 * Copyright 2016, Qualcomm Atheros, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <crypto/aes.h>
#include <crypto/algapi.h>
#include <crypto/skcipher.h>

#include "ieee80211_i.h"
#include "aes_cmac.h"
#include "fils_aead.h"

static int aes_s2v(struct crypto_cipher *tfm,
		   size_t num_elem, const u8 *addr[], size_t len[], u8 *v)
{
	u8 d[AES_BLOCK_SIZE], tmp[AES_BLOCK_SIZE];
	size_t i;
	const u8 *data[2];
	size_t data_len[2], data_elems;

	/* D = AES-CMAC(K, <zero>) */
	memset(tmp, 0, AES_BLOCK_SIZE);
	data[0] = tmp;
	data_len[0] = AES_BLOCK_SIZE;
	aes_cmac_vector(tfm, 1, data, data_len, d, AES_BLOCK_SIZE);

	for (i = 0; i < num_elem - 1; i++) {
		/* D = dbl(D) xor AES_CMAC(K, Si) */
		gf_mulx(d); /* dbl */
		aes_cmac_vector(tfm, 1, &addr[i], &len[i], tmp,
				AES_BLOCK_SIZE);
		crypto_xor(d, tmp, AES_BLOCK_SIZE);
	}

	if (len[i] >= AES_BLOCK_SIZE) {
		/* len(Sn) >= 128 */
		size_t j;
		const u8 *pos;

		/* T = Sn xorend D */

		/* Use a temporary buffer to perform xorend on Sn (addr[i]) to
		 * avoid modifying the const input argument.
		 */
		data[0] = addr[i];
		data_len[0] = len[i] - AES_BLOCK_SIZE;
		pos = addr[i] + data_len[0];
		for (j = 0; j < AES_BLOCK_SIZE; j++)
			tmp[j] = pos[j] ^ d[j];
		data[1] = tmp;
		data_len[1] = AES_BLOCK_SIZE;
		data_elems = 2;
	} else {
		/* len(Sn) < 128 */
		/* T = dbl(D) xor pad(Sn) */
		gf_mulx(d); /* dbl */
		memset(tmp, 0, AES_BLOCK_SIZE);
		memcpy(tmp, addr[i], len[i]);
		tmp[len[i]] = 0x80;
		crypto_xor(d, tmp, AES_BLOCK_SIZE);
		data[0] = d;
		data_len[0] = sizeof(d);
		data_elems = 1;
	}
	/* V = AES-CMAC(K, T) */
	aes_cmac_vector(tfm, data_elems, data, data_len, v, AES_BLOCK_SIZE);

	return 0;
}

/* Note: addr[] and len[] needs to have one extra slot at the end. */
static int aes_siv_encrypt(const u8 *key, size_t key_len,
			   const u8 *plain, size_t plain_len,
			   size_t num_elem, const u8 *addr[],
			   size_t len[], u8 *out)
{
	u8 v[AES_BLOCK_SIZE];
	struct crypto_cipher *tfm;
	struct crypto_skcipher *tfm2;
	struct skcipher_request *req;
	int res;
	struct scatterlist src[1], dst[1];
	u8 *tmp;

	key_len /= 2; /* S2V key || CTR key */

	addr[num_elem] = plain;
	len[num_elem] = plain_len;
	num_elem++;

	/* S2V */

	tfm = crypto_alloc_cipher("aes", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);
	/* K1 for S2V */
	res = crypto_cipher_setkey(tfm, key, key_len);
	if (!res)
		res = aes_s2v(tfm, num_elem, addr, len, v);
	crypto_free_cipher(tfm);
	if (res)
		return res;

	/* Use a temporary buffer of the plaintext to handle need for
	 * overwriting this during AES-CTR.
	 */
	tmp = kmemdup(plain, plain_len, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	/* IV for CTR before encrypted data */
	memcpy(out, v, AES_BLOCK_SIZE);

	/* Synthetic IV to be used as the initial counter in CTR:
	 * Q = V bitand (1^64 || 0^1 || 1^31 || 0^1 || 1^31)
	 */
	v[8] &= 0x7f;
	v[12] &= 0x7f;

	/* CTR */

	tfm2 = crypto_alloc_skcipher("ctr(aes)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm2)) {
		kfree(tmp);
		return PTR_ERR(tfm2);
	}
	/* K2 for CTR */
	res = crypto_skcipher_setkey(tfm2, key + key_len, key_len);
	if (res)
		goto fail;

	req = skcipher_request_alloc(tfm2, GFP_KERNEL);
	if (!req) {
		res = -ENOMEM;
		goto fail;
	}

	sg_init_one(src, tmp, plain_len);
	sg_init_one(dst, out + AES_BLOCK_SIZE, plain_len);
	skcipher_request_set_crypt(req, src, dst, plain_len, v);
	res = crypto_skcipher_encrypt(req);
	skcipher_request_free(req);
fail:
	kfree(tmp);
	crypto_free_skcipher(tfm2);
	return res;
}

/* Note: addr[] and len[] needs to have one extra slot at the end. */
static int aes_siv_decrypt(const u8 *key, size_t key_len,
			   const u8 *iv_crypt, size_t iv_c_len,
			   size_t num_elem, const u8 *addr[], size_t len[],
			   u8 *out)
{
	struct crypto_cipher *tfm;
	struct crypto_skcipher *tfm2;
	struct skcipher_request *req;
	struct scatterlist src[1], dst[1];
	size_t crypt_len;
	int res;
	u8 frame_iv[AES_BLOCK_SIZE], iv[AES_BLOCK_SIZE];
	u8 check[AES_BLOCK_SIZE];

	crypt_len = iv_c_len - AES_BLOCK_SIZE;
	key_len /= 2; /* S2V key || CTR key */
	addr[num_elem] = out;
	len[num_elem] = crypt_len;
	num_elem++;

	memcpy(iv, iv_crypt, AES_BLOCK_SIZE);
	memcpy(frame_iv, iv_crypt, AES_BLOCK_SIZE);

	/* Synthetic IV to be used as the initial counter in CTR:
	 * Q = V bitand (1^64 || 0^1 || 1^31 || 0^1 || 1^31)
	 */
	iv[8] &= 0x7f;
	iv[12] &= 0x7f;

	/* CTR */

	tfm2 = crypto_alloc_skcipher("ctr(aes)", 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm2))
		return PTR_ERR(tfm2);
	/* K2 for CTR */
	res = crypto_skcipher_setkey(tfm2, key + key_len, key_len);
	if (res) {
		crypto_free_skcipher(tfm2);
		return res;
	}

	req = skcipher_request_alloc(tfm2, GFP_KERNEL);
	if (!req) {
		crypto_free_skcipher(tfm2);
		return -ENOMEM;
	}

	sg_init_one(src, iv_crypt + AES_BLOCK_SIZE, crypt_len);
	sg_init_one(dst, out, crypt_len);
	skcipher_request_set_crypt(req, src, dst, crypt_len, iv);
	res = crypto_skcipher_decrypt(req);
	skcipher_request_free(req);
	crypto_free_skcipher(tfm2);
	if (res)
		return res;

	/* S2V */

	tfm = crypto_alloc_cipher("aes", 0, 0);
	if (IS_ERR(tfm))
		return PTR_ERR(tfm);
	/* K1 for S2V */
	res = crypto_cipher_setkey(tfm, key, key_len);
	if (!res)
		res = aes_s2v(tfm, num_elem, addr, len, check);
	crypto_free_cipher(tfm);
	if (res)
		return res;
	if (memcmp(check, frame_iv, AES_BLOCK_SIZE) != 0)
		return -EINVAL;
	return 0;
}

int fils_encrypt_assoc_req(struct sk_buff *skb,
			   struct ieee80211_mgd_assoc_data *assoc_data)
{
	struct ieee80211_mgmt *mgmt = (void *)skb->data;
	u8 *capab, *ies, *encr;
	const u8 *addr[5 + 1], *session;
	size_t len[5 + 1];
	size_t crypt_len;

	if (ieee80211_is_reassoc_req(mgmt->frame_control)) {
		capab = (u8 *)&mgmt->u.reassoc_req.capab_info;
		ies = mgmt->u.reassoc_req.variable;
	} else {
		capab = (u8 *)&mgmt->u.assoc_req.capab_info;
		ies = mgmt->u.assoc_req.variable;
	}

	session = cfg80211_find_ext_ie(WLAN_EID_EXT_FILS_SESSION,
				       ies, skb->data + skb->len - ies);
	if (!session || session[1] != 1 + 8)
		return -EINVAL;
	/* encrypt after FILS Session element */
	encr = (u8 *)session + 2 + 1 + 8;

	/* AES-SIV AAD vectors */

	/* The STA's MAC address */
	addr[0] = mgmt->sa;
	len[0] = ETH_ALEN;
	/* The AP's BSSID */
	addr[1] = mgmt->da;
	len[1] = ETH_ALEN;
	/* The STA's nonce */
	addr[2] = assoc_data->fils_nonces;
	len[2] = FILS_NONCE_LEN;
	/* The AP's nonce */
	addr[3] = &assoc_data->fils_nonces[FILS_NONCE_LEN];
	len[3] = FILS_NONCE_LEN;
	/* The (Re)Association Request frame from the Capability Information
	 * field to the FILS Session element (both inclusive).
	 */
	addr[4] = capab;
	len[4] = encr - capab;

	crypt_len = skb->data + skb->len - encr;
	skb_put(skb, AES_BLOCK_SIZE);
	return aes_siv_encrypt(assoc_data->fils_kek, assoc_data->fils_kek_len,
			       encr, crypt_len, 5, addr, len, encr);
}

int fils_decrypt_assoc_resp(struct ieee80211_sub_if_data *sdata,
			    u8 *frame, size_t *frame_len,
			    struct ieee80211_mgd_assoc_data *assoc_data)
{
	struct ieee80211_mgmt *mgmt = (void *)frame;
	u8 *capab, *ies, *encr;
	const u8 *addr[5 + 1], *session;
	size_t len[5 + 1];
	int res;
	size_t crypt_len;

	if (*frame_len < 24 + 6)
		return -EINVAL;

	capab = (u8 *)&mgmt->u.assoc_resp.capab_info;
	ies = mgmt->u.assoc_resp.variable;
	session = cfg80211_find_ext_ie(WLAN_EID_EXT_FILS_SESSION,
				       ies, frame + *frame_len - ies);
	if (!session || session[1] != 1 + 8) {
		mlme_dbg(sdata,
			 "No (valid) FILS Session element in (Re)Association Response frame from %pM",
			 mgmt->sa);
		return -EINVAL;
	}
	/* decrypt after FILS Session element */
	encr = (u8 *)session + 2 + 1 + 8;

	/* AES-SIV AAD vectors */

	/* The AP's BSSID */
	addr[0] = mgmt->sa;
	len[0] = ETH_ALEN;
	/* The STA's MAC address */
	addr[1] = mgmt->da;
	len[1] = ETH_ALEN;
	/* The AP's nonce */
	addr[2] = &assoc_data->fils_nonces[FILS_NONCE_LEN];
	len[2] = FILS_NONCE_LEN;
	/* The STA's nonce */
	addr[3] = assoc_data->fils_nonces;
	len[3] = FILS_NONCE_LEN;
	/* The (Re)Association Response frame from the Capability Information
	 * field to the FILS Session element (both inclusive).
	 */
	addr[4] = capab;
	len[4] = encr - capab;

	crypt_len = frame + *frame_len - encr;
	if (crypt_len < AES_BLOCK_SIZE) {
		mlme_dbg(sdata,
			 "Not enough room for AES-SIV data after FILS Session element in (Re)Association Response frame from %pM",
			 mgmt->sa);
		return -EINVAL;
	}
	res = aes_siv_decrypt(assoc_data->fils_kek, assoc_data->fils_kek_len,
			      encr, crypt_len, 5, addr, len, encr);
	if (res != 0) {
		mlme_dbg(sdata,
			 "AES-SIV decryption of (Re)Association Response frame from %pM failed",
			 mgmt->sa);
		return res;
	}
	*frame_len -= AES_BLOCK_SIZE;
	return 0;
}
