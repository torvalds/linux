/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * CTR: Counter mode
 *
 * Copyright (c) 2007 Herbert Xu <herbert@gondor.apana.org.au>
 */

#ifndef _CRYPTO_CTR_H
#define _CRYPTO_CTR_H

#include <crypto/algapi.h>
#include <crypto/internal/skcipher.h>
#include <linux/string.h>
#include <linux/types.h>

#define CTR_RFC3686_NONCE_SIZE 4
#define CTR_RFC3686_IV_SIZE 8
#define CTR_RFC3686_BLOCK_SIZE 16

static inline int crypto_ctr_encrypt_walk(struct skcipher_request *req,
					  void (*fn)(struct crypto_skcipher *,
						     const u8 *, u8 *))
{
	struct crypto_skcipher *tfm = crypto_skcipher_reqtfm(req);
	int blocksize = crypto_skcipher_chunksize(tfm);
	u8 buf[MAX_CIPHER_BLOCKSIZE];
	struct skcipher_walk walk;
	int err;

	/* avoid integer division due to variable blocksize parameter */
	if (WARN_ON_ONCE(!is_power_of_2(blocksize)))
		return -EINVAL;

	err = skcipher_walk_virt(&walk, req, false);

	while (walk.nbytes > 0) {
		const u8 *src = walk.src.virt.addr;
		u8 *dst = walk.dst.virt.addr;
		int nbytes = walk.nbytes;
		int tail = 0;

		if (nbytes < walk.total) {
			tail = walk.nbytes & (blocksize - 1);
			nbytes -= tail;
		}

		do {
			int bsize = min(nbytes, blocksize);

			fn(tfm, walk.iv, buf);

			crypto_xor_cpy(dst, src, buf, bsize);
			crypto_inc(walk.iv, blocksize);

			dst += bsize;
			src += bsize;
			nbytes -= bsize;
		} while (nbytes > 0);

		err = skcipher_walk_done(&walk, tail);
	}
	return err;
}

#endif  /* _CRYPTO_CTR_H */
