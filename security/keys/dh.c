/* Crypto operations using stored keys
 *
 * Copyright (c) 2016, Intel Corporation
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/mpi.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/crypto.h>
#include <crypto/hash.h>
#include <keys/user-type.h>
#include "internal.h"

/*
 * Public key or shared secret generation function [RFC2631 sec 2.1.1]
 *
 * ya = g^xa mod p;
 * or
 * ZZ = yb^xa mod p;
 *
 * where xa is the local private key, ya is the local public key, g is
 * the generator, p is the prime, yb is the remote public key, and ZZ
 * is the shared secret.
 *
 * Both are the same calculation, so g or yb are the "base" and ya or
 * ZZ are the "result".
 */
static int do_dh(MPI result, MPI base, MPI xa, MPI p)
{
	return mpi_powm(result, base, xa, p);
}

static ssize_t mpi_from_key(key_serial_t keyid, size_t maxlen, MPI *mpi)
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

			payload = user_key_payload_locked(key);

			if (maxlen == 0) {
				*mpi = NULL;
				ret = payload->datalen;
			} else if (payload->datalen <= maxlen) {
				*mpi = mpi_read_raw_data(payload->data,
							 payload->datalen);
				if (*mpi)
					ret = payload->datalen;
			} else {
				ret = -EINVAL;
			}
		}
		up_read(&key->sem);
	}

	key_put(key);
error:
	return ret;
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

	/* allocate synchronous hash */
	tfm = crypto_alloc_shash(hashname, 0, 0);
	if (IS_ERR(tfm)) {
		pr_info("could not allocate digest TFM handle %s\n", hashname);
		return PTR_ERR(tfm);
	}

	size = sizeof(struct shash_desc) + crypto_shash_descsize(tfm);
	sdesc = kmalloc(size, GFP_KERNEL);
	if (!sdesc)
		return -ENOMEM;
	sdesc->shash.tfm = tfm;
	sdesc->shash.flags = 0x0;

	*sdesc_ret = sdesc;

	return 0;
}

static void kdf_dealloc(struct kdf_sdesc *sdesc)
{
	if (!sdesc)
		return;

	if (sdesc->shash.tfm)
		crypto_free_shash(sdesc->shash.tfm);

	kzfree(sdesc);
}

/* convert 32 bit integer into its string representation */
static inline void crypto_kw_cpu_to_be32(u32 val, u8 *buf)
{
	__be32 *a = (__be32 *)buf;

	*a = cpu_to_be32(val);
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
		   u8 *dst, unsigned int dlen)
{
	struct shash_desc *desc = &sdesc->shash;
	unsigned int h = crypto_shash_digestsize(desc->tfm);
	int err = 0;
	u8 *dst_orig = dst;
	u32 i = 1;
	u8 iteration[sizeof(u32)];

	while (dlen) {
		err = crypto_shash_init(desc);
		if (err)
			goto err;

		crypto_kw_cpu_to_be32(i, iteration);
		err = crypto_shash_update(desc, iteration, sizeof(u32));
		if (err)
			goto err;

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
			i++;
		}
	}

	return 0;

err:
	memzero_explicit(dst_orig, dlen);
	return err;
}

static int keyctl_dh_compute_kdf(struct kdf_sdesc *sdesc,
				 char __user *buffer, size_t buflen,
				 uint8_t *kbuf, size_t kbuflen)
{
	uint8_t *outbuf = NULL;
	int ret;

	outbuf = kmalloc(buflen, GFP_KERNEL);
	if (!outbuf) {
		ret = -ENOMEM;
		goto err;
	}

	ret = kdf_ctr(sdesc, kbuf, kbuflen, outbuf, buflen);
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
	MPI base, private, prime, result;
	unsigned nbytes;
	struct keyctl_dh_params pcopy;
	uint8_t *kbuf;
	ssize_t keylen;
	size_t resultlen;
	struct kdf_sdesc *sdesc = NULL;

	if (!params || (!buffer && buflen)) {
		ret = -EINVAL;
		goto out;
	}
	if (copy_from_user(&pcopy, params, sizeof(pcopy)) != 0) {
		ret = -EFAULT;
		goto out;
	}

	if (kdfcopy) {
		char *hashname;

		if (buflen > KEYCTL_KDF_MAX_OUTPUT_LEN ||
		    kdfcopy->otherinfolen > KEYCTL_KDF_MAX_OI_LEN) {
			ret = -EMSGSIZE;
			goto out;
		}

		/* get KDF name string */
		hashname = strndup_user(kdfcopy->hashname, CRYPTO_MAX_ALG_NAME);
		if (IS_ERR(hashname)) {
			ret = PTR_ERR(hashname);
			goto out;
		}

		/* allocate KDF from the kernel crypto API */
		ret = kdf_alloc(&sdesc, hashname);
		kfree(hashname);
		if (ret)
			goto out;
	}

	/*
	 * If the caller requests postprocessing with a KDF, allow an
	 * arbitrary output buffer size since the KDF ensures proper truncation.
	 */
	keylen = mpi_from_key(pcopy.prime, kdfcopy ? SIZE_MAX : buflen, &prime);
	if (keylen < 0 || !prime) {
		/* buflen == 0 may be used to query the required buffer size,
		 * which is the prime key length.
		 */
		ret = keylen;
		goto out;
	}

	/* The result is never longer than the prime */
	resultlen = keylen;

	keylen = mpi_from_key(pcopy.base, SIZE_MAX, &base);
	if (keylen < 0 || !base) {
		ret = keylen;
		goto error1;
	}

	keylen = mpi_from_key(pcopy.private, SIZE_MAX, &private);
	if (keylen < 0 || !private) {
		ret = keylen;
		goto error2;
	}

	result = mpi_alloc(0);
	if (!result) {
		ret = -ENOMEM;
		goto error3;
	}

	/* allocate space for DH shared secret and SP800-56A otherinfo */
	kbuf = kmalloc(kdfcopy ? (resultlen + kdfcopy->otherinfolen) : resultlen,
		       GFP_KERNEL);
	if (!kbuf) {
		ret = -ENOMEM;
		goto error4;
	}

	/*
	 * Concatenate SP800-56A otherinfo past DH shared secret -- the
	 * input to the KDF is (DH shared secret || otherinfo)
	 */
	if (kdfcopy && kdfcopy->otherinfo &&
	    copy_from_user(kbuf + resultlen, kdfcopy->otherinfo,
			   kdfcopy->otherinfolen) != 0) {
		ret = -EFAULT;
		goto error5;
	}

	ret = do_dh(result, base, private, prime);
	if (ret)
		goto error5;

	ret = mpi_read_buffer(result, kbuf, resultlen, &nbytes, NULL);
	if (ret != 0)
		goto error5;

	if (kdfcopy) {
		ret = keyctl_dh_compute_kdf(sdesc, buffer, buflen, kbuf,
					    resultlen + kdfcopy->otherinfolen);
	} else {
		ret = nbytes;
		if (copy_to_user(buffer, kbuf, nbytes) != 0)
			ret = -EFAULT;
	}

error5:
	kzfree(kbuf);
error4:
	mpi_free(result);
error3:
	mpi_free(private);
error2:
	mpi_free(base);
error1:
	mpi_free(prime);
out:
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
