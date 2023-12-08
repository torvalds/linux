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

/* compute_ecdh_secret() - function assumes that the private key was
 *                         already set.
 * @tfm:          KPP tfm handle allocated with crypto_alloc_kpp().
 * @public_key:   pair's ecc public key.
 * secret:        memory where the ecdh computed shared secret will be saved.
 *
 * Return: zero on success; error code in case of error.
 */
int compute_ecdh_secret(struct crypto_kpp *tfm, const u8 public_key[64],
			u8 secret[32])
{
	struct kpp_request *req;
	u8 *tmp;
	struct ecdh_completion result;
	struct scatterlist src, dst;
	int err;

	tmp = kmalloc(64, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	req = kpp_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		err = -ENOMEM;
		goto free_tmp;
	}

	init_completion(&result.completion);

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
	kpp_request_free(req);
free_tmp:
	kfree_sensitive(tmp);
	return err;
}

/* set_ecdh_privkey() - set or generate ecc private key.
 *
 * Function generates an ecc private key in the crypto subsystem when receiving
 * a NULL private key or sets the received key when not NULL.
 *
 * @tfm:           KPP tfm handle allocated with crypto_alloc_kpp().
 * @private_key:   user's ecc private key. When not NULL, the key is expected
 *                 in little endian format.
 *
 * Return: zero on success; error code in case of error.
 */
int set_ecdh_privkey(struct crypto_kpp *tfm, const u8 private_key[32])
{
	u8 *buf, *tmp = NULL;
	unsigned int buf_len;
	int err;
	struct ecdh p = {0};

	if (private_key) {
		tmp = kmalloc(32, GFP_KERNEL);
		if (!tmp)
			return -ENOMEM;
		swap_digits((u64 *)private_key, (u64 *)tmp, 4);
		p.key = tmp;
		p.key_size = 32;
	}

	buf_len = crypto_ecdh_key_len(&p);
	buf = kmalloc(buf_len, GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto free_tmp;
	}

	err = crypto_ecdh_encode_key(buf, buf_len, &p);
	if (err)
		goto free_all;

	err = crypto_kpp_set_secret(tfm, buf, buf_len);
	/* fall through */
free_all:
	kfree_sensitive(buf);
free_tmp:
	kfree_sensitive(tmp);
	return err;
}

/* generate_ecdh_public_key() - function assumes that the private key was
 *                              already set.
 *
 * @tfm:          KPP tfm handle allocated with crypto_alloc_kpp().
 * @public_key:   memory where the computed ecc public key will be saved.
 *
 * Return: zero on success; error code in case of error.
 */
int generate_ecdh_public_key(struct crypto_kpp *tfm, u8 public_key[64])
{
	struct kpp_request *req;
	u8 *tmp;
	struct ecdh_completion result;
	struct scatterlist dst;
	int err;

	tmp = kmalloc(64, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	req = kpp_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		err = -ENOMEM;
		goto free_tmp;
	}

	init_completion(&result.completion);
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
	if (err < 0)
		goto free_all;

	/* The public key is handed back in little endian as expected by
	 * the Security Manager Protocol.
	 */
	swap_digits((u64 *)tmp, (u64 *)public_key, 4); /* x */
	swap_digits((u64 *)&tmp[32], (u64 *)&public_key[32], 4); /* y */

free_all:
	kpp_request_free(req);
free_tmp:
	kfree(tmp);
	return err;
}

/* generate_ecdh_keys() - generate ecc key pair.
 *
 * @tfm:          KPP tfm handle allocated with crypto_alloc_kpp().
 * @public_key:   memory where the computed ecc public key will be saved.
 *
 * Return: zero on success; error code in case of error.
 */
int generate_ecdh_keys(struct crypto_kpp *tfm, u8 public_key[64])
{
	int err;

	err = set_ecdh_privkey(tfm, NULL);
	if (err)
		return err;

	return generate_ecdh_public_key(tfm, public_key);
}
