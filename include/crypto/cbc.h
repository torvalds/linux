/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * CBC: Cipher Block Chaining mode
 *
 * Copyright (c) 2016 Herbert Xu <herbert@gondor.apana.org.au>
 */

#ifndef _CRYPTO_CBC_H
#define _CRYPTO_CBC_H

#include <crypto/internal/skcipher.h>
#include <linux/string.h>
#include <linux/types.h>

static inline int crypto_cbc_encrypt_segment(
	struct skcipher_walk *walk, struct crypto_skcipher *tfm,
	void (*fn)(struct crypto_skcipher *, const u8 *, u8 *))
{
	unsigned int bsize = crypto_skcipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	u8 *iv = walk->iv;

	do {
		crypto_xor(iv, src, bsize);
		fn(tfm, iv, dst);
		memcpy(iv, dst, bsize);

		src += bsize;
		dst += bsize;
	} while ((nbytes -= bsize) >= bsize);

	return nbytes;
}

static inline int crypto_cbc_encrypt_inplace(
	struct skcipher_walk *walk, struct crypto_skcipher *tfm,
	void (*fn)(struct crypto_skcipher *, const u8 *, u8 *))
{
	unsigned int bsize = crypto_skcipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *iv = walk->iv;

	do {
		crypto_xor(src, iv, bsize);
		fn(tfm, src, src);
		iv = src;

		src += bsize;
	} while ((nbytes -= bsize) >= bsize);

	memcpy(walk->iv, iv, bsize);

	return nbytes;
}

static inline int crypto_cbc_encrypt_walk(struct skcipher_request *req,
					  void (*fn)(struct crypto_skcipher *,
						     const u8 *, u8 *))
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	struct skcipher_walk walk;
	int err;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes) {
		if (walk.src.virt.addr == walk.dst.virt.addr)
			err = crypto_cbc_encrypt_inplace(&walk, tfm, fn);
		else
			err = crypto_cbc_encrypt_segment(&walk, tfm, fn);
		err = skcipher_walk_done(&walk, err);
	}

	return err;
}

static inline int crypto_cbc_decrypt_segment(
	struct skcipher_walk *walk, struct crypto_skcipher *tfm,
	void (*fn)(struct crypto_skcipher *, const u8 *, u8 *))
{
	unsigned int bsize = crypto_skcipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 *dst = walk->dst.virt.addr;
	u8 *iv = walk->iv;

	do {
		fn(tfm, src, dst);
		crypto_xor(dst, iv, bsize);
		iv = src;

		src += bsize;
		dst += bsize;
	} while ((nbytes -= bsize) >= bsize);

	memcpy(walk->iv, iv, bsize);

	return nbytes;
}

static inline int crypto_cbc_decrypt_inplace(
	struct skcipher_walk *walk, struct crypto_skcipher *tfm,
	void (*fn)(struct crypto_skcipher *, const u8 *, u8 *))
{
	unsigned int bsize = crypto_skcipher_blocksize(tfm);
	unsigned int nbytes = walk->nbytes;
	u8 *src = walk->src.virt.addr;
	u8 last_iv[MAX_CIPHER_BLOCKSIZE];

	/* Start of the last block. */
	src += nbytes - (nbytes & (bsize - 1)) - bsize;
	memcpy(last_iv, src, bsize);

	for (;;) {
		fn(tfm, src, src);
		if ((nbytes -= bsize) < bsize)
			break;
		crypto_xor(src, src - bsize, bsize);
		src -= bsize;
	}

	crypto_xor(src, walk->iv, bsize);
	memcpy(walk->iv, last_iv, bsize);

	return nbytes;
}

static inline int crypto_cbc_decrypt_blocks(
	struct skcipher_walk *walk, struct crypto_skcipher *tfm,
	void (*fn)(struct crypto_skcipher *, const u8 *, u8 *))
{
	if (walk->src.virt.addr == walk->dst.virt.addr)
		return crypto_cbc_decrypt_inplace(walk, tfm, fn);
	else
		return crypto_cbc_decrypt_segment(walk, tfm, fn);
}

#endif	/* _CRYPTO_CBC_H */
