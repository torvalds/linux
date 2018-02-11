/*
 * Copyright 2003-2004, Instant802 Networks, Inc.
 * Copyright 2005-2006, Devicescape Software, Inc.
 * Copyright 2014-2015, Qualcomm Atheros, Inc.
 *
 * Rewrite: Copyright (C) 2013 Linaro Ltd <ard.biesheuvel@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/scatterlist.h>
#include <crypto/aead.h>

#include "aead_api.h"

int aead_encrypt(struct crypto_aead *tfm, u8 *b_0, u8 *aad, size_t aad_len,
		 u8 *data, size_t data_len, u8 *mic)
{
	size_t mic_len = crypto_aead_authsize(tfm);
	struct scatterlist sg[3];
	struct aead_request *aead_req;
	int reqsize = sizeof(*aead_req) + crypto_aead_reqsize(tfm);
	u8 *__aad;

	aead_req = kzalloc(reqsize + aad_len, GFP_ATOMIC);
	if (!aead_req)
		return -ENOMEM;

	__aad = (u8 *)aead_req + reqsize;
	memcpy(__aad, aad, aad_len);

	sg_init_table(sg, 3);
	sg_set_buf(&sg[0], __aad, aad_len);
	sg_set_buf(&sg[1], data, data_len);
	sg_set_buf(&sg[2], mic, mic_len);

	aead_request_set_tfm(aead_req, tfm);
	aead_request_set_crypt(aead_req, sg, sg, data_len, b_0);
	aead_request_set_ad(aead_req, sg[0].length);

	crypto_aead_encrypt(aead_req);
	kzfree(aead_req);

	return 0;
}

int aead_decrypt(struct crypto_aead *tfm, u8 *b_0, u8 *aad, size_t aad_len,
		 u8 *data, size_t data_len, u8 *mic)
{
	size_t mic_len = crypto_aead_authsize(tfm);
	struct scatterlist sg[3];
	struct aead_request *aead_req;
	int reqsize = sizeof(*aead_req) + crypto_aead_reqsize(tfm);
	u8 *__aad;
	int err;

	if (data_len == 0)
		return -EINVAL;

	aead_req = kzalloc(reqsize + aad_len, GFP_ATOMIC);
	if (!aead_req)
		return -ENOMEM;

	__aad = (u8 *)aead_req + reqsize;
	memcpy(__aad, aad, aad_len);

	sg_init_table(sg, 3);
	sg_set_buf(&sg[0], __aad, aad_len);
	sg_set_buf(&sg[1], data, data_len);
	sg_set_buf(&sg[2], mic, mic_len);

	aead_request_set_tfm(aead_req, tfm);
	aead_request_set_crypt(aead_req, sg, sg, data_len + mic_len, b_0);
	aead_request_set_ad(aead_req, sg[0].length);

	err = crypto_aead_decrypt(aead_req);
	kzfree(aead_req);

	return err;
}

struct crypto_aead *
aead_key_setup_encrypt(const char *alg, const u8 key[],
		       size_t key_len, size_t mic_len)
{
	struct crypto_aead *tfm;
	int err;

	tfm = crypto_alloc_aead(alg, 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm))
		return tfm;

	err = crypto_aead_setkey(tfm, key, key_len);
	if (err)
		goto free_aead;
	err = crypto_aead_setauthsize(tfm, mic_len);
	if (err)
		goto free_aead;

	return tfm;

free_aead:
	crypto_free_aead(tfm);
	return ERR_PTR(err);
}

void aead_key_free(struct crypto_aead *tfm)
{
	crypto_free_aead(tfm);
}
