/*
 * ECDH helper functions - KPP wrappings
 *
 * Copyright (C) 2017 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation;
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF THIRD PARTY RIGHTS.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) AND AUTHOR(S) BE LIABLE FOR ANY
 * CLAIM, OR ANY SPECIAL INDIRECT OR CONSEQUENTIAL DAMAGES, OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * ALL LIABILITY, INCLUDING LIABILITY FOR INFRINGEMENT OF ANY PATENTS,
 * COPYRIGHTS, TRADEMARKS OR OTHER RIGHTS, RELATING TO USE OF THIS
 * SOFTWARE IS DISCLAIMED.
 */
#include "ecdh_helper.h"

#include <linux/scatterlist.h>
#include <crypto/kpp.h>
#include <crypto/ecdh.h>

struct ecdh_completion {
	struct completion completion;
	int err;
};

static void ecdh_complete(struct crypto_async_request *req, int err)
{
	struct ecdh_completion *res = req->data;

	if (err == -EINPROGRESS)
		return;

	res->err = err;
	complete(&res->completion);
}

static inline void swap_digits(u64 *in, u64 *out, unsigned int ndigits)
{
	int i;

	for (i = 0; i < ndigits; i++)
		out[i] = __swab64(in[ndigits - 1 - i]);
}

bool compute_ecdh_secret(const u8 public_key[64], const u8 private_key[32],
			 u8 secret[32])
{
	struct crypto_kpp *tfm;
	struct kpp_request *req;
	struct ecdh p;
	struct ecdh_completion result;
	struct scatterlist src, dst;
	u8 *tmp, *buf;
	unsigned int buf_len;
	int err = -ENOMEM;

	tmp = kmalloc(64, GFP_KERNEL);
	if (!tmp)
		return false;

	tfm = crypto_alloc_kpp("ecdh", CRYPTO_ALG_INTERNAL, 0);
	if (IS_ERR(tfm)) {
		pr_err("alg: kpp: Failed to load tfm for kpp: %ld\n",
		       PTR_ERR(tfm));
		goto free_tmp;
	}

	req = kpp_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		goto free_kpp;

	init_completion(&result.completion);

	/* Security Manager Protocol holds digits in litte-endian order
	 * while ECC API expect big-endian data
	 */
	swap_digits((u64 *)private_key, (u64 *)tmp, 4);
	p.key = (char *)tmp;
	p.key_size = 32;
	/* Set curve_id */
	p.curve_id = ECC_CURVE_NIST_P256;
	buf_len = crypto_ecdh_key_len(&p);
	buf = kmalloc(buf_len, GFP_KERNEL);
	if (!buf)
		goto free_req;

	crypto_ecdh_encode_key(buf, buf_len, &p);

	/* Set A private Key */
	err = crypto_kpp_set_secret(tfm, (void *)buf, buf_len);
	if (err)
		goto free_all;

	swap_digits((u64 *)public_key, (u64 *)tmp, 4); /* x */
	swap_digits((u64 *)&public_key[32], (u64 *)&tmp[32], 4); /* y */

	sg_init_one(&src, tmp, 64);
	sg_init_one(&dst, secret, 32);
	kpp_request_set_input(req, &src, 64);
	kpp_request_set_output(req, &dst, 32);
	kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
				 ecdh_complete, &result);
	err = crypto_kpp_compute_shared_secret(req);
	if (err == -EINPROGRESS) {
		wait_for_completion(&result.completion);
		err = result.err;
	}
	if (err < 0) {
		pr_err("alg: ecdh: compute shared secret failed. err %d\n",
		       err);
		goto free_all;
	}

	swap_digits((u64 *)secret, (u64 *)tmp, 4);
	memcpy(secret, tmp, 32);

free_all:
	kzfree(buf);
free_req:
	kpp_request_free(req);
free_kpp:
	crypto_free_kpp(tfm);
free_tmp:
	kfree(tmp);
	return (err == 0);
}

bool generate_ecdh_keys(u8 public_key[64], u8 private_key[32])
{
	struct crypto_kpp *tfm;
	struct kpp_request *req;
	struct ecdh p;
	struct ecdh_completion result;
	struct scatterlist dst;
	u8 *tmp, *buf;
	unsigned int buf_len;
	int err = -ENOMEM;
	const unsigned short max_tries = 16;
	unsigned short tries = 0;

	tmp = kmalloc(64, GFP_KERNEL);
	if (!tmp)
		return false;

	tfm = crypto_alloc_kpp("ecdh", CRYPTO_ALG_INTERNAL, 0);
	if (IS_ERR(tfm)) {
		pr_err("alg: kpp: Failed to load tfm for kpp: %ld\n",
		       PTR_ERR(tfm));
		goto free_tmp;
	}

	req = kpp_request_alloc(tfm, GFP_KERNEL);
	if (!req)
		goto free_kpp;

	init_completion(&result.completion);

	/* Set curve_id */
	p.curve_id = ECC_CURVE_NIST_P256;
	p.key_size = 32;
	buf_len = crypto_ecdh_key_len(&p);
	buf = kmalloc(buf_len, GFP_KERNEL);
	if (!buf)
		goto free_req;

	do {
		if (tries++ >= max_tries)
			goto free_all;

		/* Set private Key */
		p.key = (char *)private_key;
		crypto_ecdh_encode_key(buf, buf_len, &p);
		err = crypto_kpp_set_secret(tfm, buf, buf_len);
		if (err)
			goto free_all;

		sg_init_one(&dst, tmp, 64);
		kpp_request_set_input(req, NULL, 0);
		kpp_request_set_output(req, &dst, 64);
		kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG,
					 ecdh_complete, &result);

		err = crypto_kpp_generate_public_key(req);

		if (err == -EINPROGRESS) {
			wait_for_completion(&result.completion);
			err = result.err;
		}

		/* Private key is not valid. Regenerate */
		if (err == -EINVAL)
			continue;

		if (err < 0)
			goto free_all;
		else
			break;

	} while (true);

	/* Keys are handed back in little endian as expected by Security
	 * Manager Protocol
	 */
	swap_digits((u64 *)tmp, (u64 *)public_key, 4); /* x */
	swap_digits((u64 *)&tmp[32], (u64 *)&public_key[32], 4); /* y */
	swap_digits((u64 *)private_key, (u64 *)tmp, 4);
	memcpy(private_key, tmp, 32);

free_all:
	kzfree(buf);
free_req:
	kpp_request_free(req);
free_kpp:
	crypto_free_kpp(tfm);
free_tmp:
	kfree(tmp);
	return (err == 0);
}
