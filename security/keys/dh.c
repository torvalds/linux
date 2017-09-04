/* Crypto operations using stored keys
 *
 * Copyright (c) 2016, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <crypto/kpp.h>
#include <crypto/dh.h>
#include <keys/user-type.h>
#include "internal.h"

static ssize_t dh_data_from_key(key_serial_t keyid, void **data)
{
	struct key *key;
	key_ref_t key_ref;
	long status;
	ssize_t ret;

	key_ref = lookup_user_key(keyid, 0, KEY_NEED_READ);
	if (IS_ERR(key_ref)) {
		ret = -ENOKEY;
		goto error;
	}

	key = key_ref_to_ptr(key_ref);

	ret = -EOPNOTSUPP;
	if (key->type == &key_type_user) {
		down_read(&key->sem);
		status = key_validate(key);
		if (status == 0) {
			const struct user_key_payload *payload;
			uint8_t *duplicate;

			payload = user_key_payload_locked(key);

			duplicate = kmemdup(payload->data, payload->datalen,
					    GFP_KERNEL);
			if (duplicate) {
				*data = duplicate;
				ret = payload->datalen;
			} else {
				ret = -ENOMEM;
			}
		}
		up_read(&key->sem);
	}

	key_put(key);
error:
	return ret;
}

static void dh_free_data(struct dh *dh)
{
	kzfree(dh->key);
	kzfree(dh->p);
	kzfree(dh->g);
}

struct dh_completion {
	struct completion completion;
	int err;
};

static void dh_crypto_done(struct crypto_async_request *req, int err)
{
	struct dh_completion *compl = req->data;

	if (err == -EINPROGRESS)
		return;

	compl->err = err;
	complete(&compl->completion);
}

struct kdf_sdesc {
	struct shash_desc shash;
	char ctx[];
};

static int kdf_alloc(struct kdf_sdesc **sdesc_ret, char *hashname)
{
	struct crypto_shash *tfm;
	struct kdf_sdesc *sdesc;
	int size;
	int err;

	/* allocate synchronous hash */
	tfm = crypto_alloc_shash(hashname, 0, 0);
	if (IS_ERR(tfm)) {
		pr_info("could not allocate digest TFM handle %s\n", hashname);
		return PTR_ERR(tfm);
	}

	err = -EINVAL;
	if (crypto_shash_digestsize(tfm) == 0)
		goto out_free_tfm;

	err = -ENOMEM;
	size = sizeof(struct shash_desc) + crypto_shash_descsize(tfm);
	sdesc = kmalloc(size, GFP_KERNEL);
	if (!sdesc)
		goto out_free_tfm;
	sdesc->shash.tfm = tfm;
	sdesc->shash.flags = 0x0;

	*sdesc_ret = sdesc;

	return 0;

out_free_tfm:
	crypto_free_shash(tfm);
	return err;
}

static void kdf_dealloc(struct kdf_sdesc *sdesc)
{
	if (!sdesc)
		return;

	if (sdesc->shash.tfm)
		crypto_free_shash(sdesc->shash.tfm);

	kzfree(sdesc);
}

/*
 * Implementation of the KDF in counter mode according to SP800-108 section 5.1
 * as well as SP800-56A section 5.8.1 (Single-step KDF).
 *
 * SP800-56A:
 * The src pointer is defined as Z || other info where Z is the shared secret
 * from DH and other info is an arbitrary string (see SP800-56A section
 * 5.8.1.2).
 */
static int kdf_ctr(struct kdf_sdesc *sdesc, const u8 *src, unsigned int slen,
		   u8 *dst, unsigned int dlen, unsigned int zlen)
{
	struct shash_desc *desc = &sdesc->shash;
	unsigned int h = crypto_shash_digestsize(desc->tfm);
	int err = 0;
	u8 *dst_orig = dst;
	__be32 counter = cpu_to_be32(1);

	while (dlen) {
		err = crypto_shash_init(desc);
		if (err)
			goto err;

		err = crypto_shash_update(desc, (u8 *)&counter, sizeof(__be32));
		if (err)
			goto err;

		if (zlen && h) {
			u8 tmpbuffer[h];
			size_t chunk = min_t(size_t, zlen, h);
			memset(tmpbuffer, 0, chunk);

			do {
				err = crypto_shash_update(desc, tmpbuffer,
							  chunk);
				if (err)
					goto err;

				zlen -= chunk;
				chunk = min_t(size_t, zlen, h);
			} while (zlen);
		}

		if (src && slen) {
			err = crypto_shash_update(desc, src, slen);
			if (err)
				goto err;
		}

		if (dlen < h) {
			u8 tmpbuffer[h];

			err = crypto_shash_final(desc, tmpbuffer);
			if (err)
				goto err;
			memcpy(dst, tmpbuffer, dlen);
			memzero_explicit(tmpbuffer, h);
			return 0;
		} else {
			err = crypto_shash_final(desc, dst);
			if (err)
				goto err;

			dlen -= h;
			dst += h;
			counter = cpu_to_be32(be32_to_cpu(counter) + 1);
		}
	}

	return 0;

err:
	memzero_explicit(dst_orig, dlen);
	return err;
}

static int keyctl_dh_compute_kdf(struct kdf_sdesc *sdesc,
				 char __user *buffer, size_t buflen,
				 uint8_t *kbuf, size_t kbuflen, size_t lzero)
{
	uint8_t *outbuf = NULL;
	int ret;

	outbuf = kmalloc(buflen, GFP_KERNEL);
	if (!outbuf) {
		ret = -ENOMEM;
		goto err;
	}

	ret = kdf_ctr(sdesc, kbuf, kbuflen, outbuf, buflen, lzero);
	if (ret)
		goto err;

	ret = buflen;
	if (copy_to_user(buffer, outbuf, buflen) != 0)
		ret = -EFAULT;

err:
	kzfree(outbuf);
	return ret;
}

long __keyctl_dh_compute(struct keyctl_dh_params __user *params,
			 char __user *buffer, size_t buflen,
			 struct keyctl_kdf_params *kdfcopy)
{
	long ret;
	ssize_t dlen;
	int secretlen;
	int outlen;
	struct keyctl_dh_params pcopy;
	struct dh dh_inputs;
	struct scatterlist outsg;
	struct dh_completion compl;
	struct crypto_kpp *tfm;
	struct kpp_request *req;
	uint8_t *secret;
	uint8_t *outbuf;
	struct kdf_sdesc *sdesc = NULL;

	if (!params || (!buffer && buflen)) {
		ret = -EINVAL;
		goto out1;
	}
	if (copy_from_user(&pcopy, params, sizeof(pcopy)) != 0) {
		ret = -EFAULT;
		goto out1;
	}

	if (kdfcopy) {
		char *hashname;

		if (buflen > KEYCTL_KDF_MAX_OUTPUT_LEN ||
		    kdfcopy->otherinfolen > KEYCTL_KDF_MAX_OI_LEN) {
			ret = -EMSGSIZE;
			goto out1;
		}

		/* get KDF name string */
		hashname = strndup_user(kdfcopy->hashname, CRYPTO_MAX_ALG_NAME);
		if (IS_ERR(hashname)) {
			ret = PTR_ERR(hashname);
			goto out1;
		}

		/* allocate KDF from the kernel crypto API */
		ret = kdf_alloc(&sdesc, hashname);
		kfree(hashname);
		if (ret)
			goto out1;
	}

	memset(&dh_inputs, 0, sizeof(dh_inputs));

	dlen = dh_data_from_key(pcopy.prime, &dh_inputs.p);
	if (dlen < 0) {
		ret = dlen;
		goto out1;
	}
	dh_inputs.p_size = dlen;

	dlen = dh_data_from_key(pcopy.base, &dh_inputs.g);
	if (dlen < 0) {
		ret = dlen;
		goto out2;
	}
	dh_inputs.g_size = dlen;

	dlen = dh_data_from_key(pcopy.private, &dh_inputs.key);
	if (dlen < 0) {
		ret = dlen;
		goto out2;
	}
	dh_inputs.key_size = dlen;

	secretlen = crypto_dh_key_len(&dh_inputs);
	secret = kmalloc(secretlen, GFP_KERNEL);
	if (!secret) {
		ret = -ENOMEM;
		goto out2;
	}
	ret = crypto_dh_encode_key(secret, secretlen, &dh_inputs);
	if (ret)
		goto out3;

	tfm = crypto_alloc_kpp("dh", CRYPTO_ALG_TYPE_KPP, 0);
	if (IS_ERR(tfm)) {
		ret = PTR_ERR(tfm);
		goto out3;
	}

	ret = crypto_kpp_set_secret(tfm, secret, secretlen);
	if (ret)
		goto out4;

	outlen = crypto_kpp_maxsize(tfm);

	if (!kdfcopy) {
		/*
		 * When not using a KDF, buflen 0 is used to read the
		 * required buffer length
		 */
		if (buflen == 0) {
			ret = outlen;
			goto out4;
		} else if (outlen > buflen) {
			ret = -EOVERFLOW;
			goto out4;
		}
	}

	outbuf = kzalloc(kdfcopy ? (outlen + kdfcopy->otherinfolen) : outlen,
			 GFP_KERNEL);
	if (!outbuf) {
		ret = -ENOMEM;
		goto out4;
	}

	sg_init_one(&outsg, outbuf, outlen);

	req = kpp_request_alloc(tfm, GFP_KERNEL);
	if (!req) {
		ret = -ENOMEM;
		goto out5;
	}

	kpp_request_set_input(req, NULL, 0);
	kpp_request_set_output(req, &outsg, outlen);
	init_completion(&compl.completion);
	kpp_request_set_callback(req, CRYPTO_TFM_REQ_MAY_BACKLOG |
				 CRYPTO_TFM_REQ_MAY_SLEEP,
				 dh_crypto_done, &compl);

	/*
	 * For DH, generate_public_key and generate_shared_secret are
	 * the same calculation
	 */
	ret = crypto_kpp_generate_public_key(req);
	if (ret == -EINPROGRESS) {
		wait_for_completion(&compl.completion);
		ret = compl.err;
		if (ret)
			goto out6;
	}

	if (kdfcopy) {
		/*
		 * Concatenate SP800-56A otherinfo past DH shared secret -- the
		 * input to the KDF is (DH shared secret || otherinfo)
		 */
		if (copy_from_user(outbuf + req->dst_len, kdfcopy->otherinfo,
				   kdfcopy->otherinfolen) != 0) {
			ret = -EFAULT;
			goto out6;
		}

		ret = keyctl_dh_compute_kdf(sdesc, buffer, buflen, outbuf,
					    req->dst_len + kdfcopy->otherinfolen,
					    outlen - req->dst_len);
	} else if (copy_to_user(buffer, outbuf, req->dst_len) == 0) {
		ret = req->dst_len;
	} else {
		ret = -EFAULT;
	}

out6:
	kpp_request_free(req);
out5:
	kzfree(outbuf);
out4:
	crypto_free_kpp(tfm);
out3:
	kzfree(secret);
out2:
	dh_free_data(&dh_inputs);
out1:
	kdf_dealloc(sdesc);
	return ret;
}

long keyctl_dh_compute(struct keyctl_dh_params __user *params,
		       char __user *buffer, size_t buflen,
		       struct keyctl_kdf_params __user *kdf)
{
	struct keyctl_kdf_params kdfcopy;

	if (!kdf)
		return __keyctl_dh_compute(params, buffer, buflen, NULL);

	if (copy_from_user(&kdfcopy, kdf, sizeof(kdfcopy)) != 0)
		return -EFAULT;

	return __keyctl_dh_compute(params, buffer, buflen, &kdfcopy);
}
