// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2011 Nokia Corporation
 * Copyright (C) 2011 Intel Corporation
 *
 * Author:
 * Dmitry Kasatkin <dmitry.kasatkin@nokia.com>
 *                 <dmitry.kasatkin@intel.com>
 *
 * File: sign.c
 *	implements signature (RSA) verification
 *	pkcs decoding is based on LibTomCrypt code
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/err.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/key.h>
#include <crypto/sha1.h>
#include <keys/user-type.h>
#include <linux/mpi.h>
#include <linux/digsig.h>

static const char *pkcs_1_v1_5_decode_emsa(const unsigned char *msg,
						unsigned long  msglen,
						unsigned long  modulus_bitlen,
						unsigned long *outlen)
{
	unsigned long modulus_len, ps_len, i;

	modulus_len = (modulus_bitlen >> 3) + (modulus_bitlen & 7 ? 1 : 0);

	/* test message size */
	if ((msglen > modulus_len) || (modulus_len < 11))
		return NULL;

	/* separate encoded message */
	if (msg[0] != 0x00 || msg[1] != 0x01)
		return NULL;

	for (i = 2; i < modulus_len - 1; i++)
		if (msg[i] != 0xFF)
			break;

	/* separator check */
	if (msg[i] != 0)
		/* There was no octet with hexadecimal value 0x00
		to separate ps from m. */
		return NULL;

	ps_len = i - 2;

	*outlen = (msglen - (2 + ps_len + 1));

	return msg + 2 + ps_len + 1;
}

/*
 * RSA Signature verification with public key
 */
static int digsig_verify_rsa(struct key *key,
		    const char *sig, int siglen,
		       const char *h, int hlen)
{
	int err = -EINVAL;
	unsigned long len;
	unsigned long mlen, mblen;
	unsigned nret, l;
	int head, i;
	unsigned char *out1 = NULL;
	const char *m;
	MPI in = NULL, res = NULL, pkey[2];
	uint8_t *p, *datap;
	const uint8_t *endp;
	const struct user_key_payload *ukp;
	struct pubkey_hdr *pkh;

	down_read(&key->sem);
	ukp = user_key_payload_locked(key);

	if (!ukp) {
		/* key was revoked before we acquired its semaphore */
		err = -EKEYREVOKED;
		goto err1;
	}

	if (ukp->datalen < sizeof(*pkh))
		goto err1;

	pkh = (struct pubkey_hdr *)ukp->data;

	if (pkh->version != 1)
		goto err1;

	if (pkh->algo != PUBKEY_ALGO_RSA)
		goto err1;

	if (pkh->nmpi != 2)
		goto err1;

	datap = pkh->mpi;
	endp = ukp->data + ukp->datalen;

	for (i = 0; i < pkh->nmpi; i++) {
		unsigned int remaining = endp - datap;
		pkey[i] = mpi_read_from_buffer(datap, &remaining);
		if (IS_ERR(pkey[i])) {
			err = PTR_ERR(pkey[i]);
			goto err;
		}
		datap += remaining;
	}

	mblen = mpi_get_nbits(pkey[0]);
	mlen = DIV_ROUND_UP(mblen, 8);

	if (mlen == 0) {
		err = -EINVAL;
		goto err;
	}

	err = -ENOMEM;

	out1 = kzalloc(mlen, GFP_KERNEL);
	if (!out1)
		goto err;

	nret = siglen;
	in = mpi_read_from_buffer(sig, &nret);
	if (IS_ERR(in)) {
		err = PTR_ERR(in);
		goto err;
	}

	res = mpi_alloc(mpi_get_nlimbs(in) * 2);
	if (!res)
		goto err;

	err = mpi_powm(res, in, pkey[1], pkey[0]);
	if (err)
		goto err;

	if (mpi_get_nlimbs(res) * BYTES_PER_MPI_LIMB > mlen) {
		err = -EINVAL;
		goto err;
	}

	p = mpi_get_buffer(res, &l, NULL);
	if (!p) {
		err = -EINVAL;
		goto err;
	}

	len = mlen;
	head = len - l;
	memcpy(out1 + head, p, l);

	kfree(p);

	m = pkcs_1_v1_5_decode_emsa(out1, len, mblen, &len);

	if (!m || len != hlen || memcmp(m, h, hlen))
		err = -EINVAL;

err:
	mpi_free(in);
	mpi_free(res);
	kfree(out1);
	while (--i >= 0)
		mpi_free(pkey[i]);
err1:
	up_read(&key->sem);

	return err;
}

/**
 * digsig_verify() - digital signature verification with public key
 * @keyring:	keyring to search key in
 * @sig:	digital signature
 * @siglen:	length of the signature
 * @data:	data
 * @datalen:	length of the data
 *
 * Returns 0 on success, -EINVAL otherwise
 *
 * Verifies data integrity against digital signature.
 * Currently only RSA is supported.
 * Normally hash of the content is used as a data for this function.
 *
 */
int digsig_verify(struct key *keyring, const char *sig, int siglen,
						const char *data, int datalen)
{
	struct signature_hdr *sh = (struct signature_hdr *)sig;
	struct sha1_ctx ctx;
	unsigned char hash[SHA1_DIGEST_SIZE];
	struct key *key;
	char name[20];
	int err;

	if (siglen < sizeof(*sh) + 2)
		return -EINVAL;

	if (sh->algo != PUBKEY_ALGO_RSA)
		return -ENOTSUPP;

	sprintf(name, "%llX", __be64_to_cpup((uint64_t *)sh->keyid));

	if (keyring) {
		/* search in specific keyring */
		key_ref_t kref;
		kref = keyring_search(make_key_ref(keyring, 1UL),
				      &key_type_user, name, true);
		if (IS_ERR(kref))
			key = ERR_CAST(kref);
		else
			key = key_ref_to_ptr(kref);
	} else {
		key = request_key(&key_type_user, name, NULL);
	}
	if (IS_ERR(key)) {
		pr_err("key not found, id: %s\n", name);
		return PTR_ERR(key);
	}

	sha1_init(&ctx);
	sha1_update(&ctx, data, datalen);
	sha1_update(&ctx, sig, sizeof(*sh));
	sha1_final(&ctx, hash);

	/* pass signature mpis address */
	err = digsig_verify_rsa(key, sig + sizeof(*sh), siglen - sizeof(*sh),
			     hash, sizeof(hash));

	key_put(key);

	return err ? -EINVAL : 0;
}
EXPORT_SYMBOL_GPL(digsig_verify);

MODULE_LICENSE("GPL");
