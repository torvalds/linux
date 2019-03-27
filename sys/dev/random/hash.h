/*-
 * Copyright (c) 2000-2015 Mark R V Murray
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer
 *    in this position and unchanged.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef SYS_DEV_RANDOM_HASH_H_INCLUDED
#define	SYS_DEV_RANDOM_HASH_H_INCLUDED

#include <crypto/chacha20/_chacha.h>
#include <dev/random/uint128.h>

/* Keys are formed from cipher blocks */
#define	RANDOM_KEYSIZE		32	/* (in bytes) == 256 bits */
#define	RANDOM_KEYSIZE_WORDS	(RANDOM_KEYSIZE/sizeof(uint32_t))
#define	RANDOM_BLOCKSIZE	16	/* (in bytes) == 128 bits */
#define	RANDOM_BLOCKSIZE_WORDS	(RANDOM_BLOCKSIZE/sizeof(uint32_t))
#define	RANDOM_KEYS_PER_BLOCK	(RANDOM_KEYSIZE/RANDOM_BLOCKSIZE)

/* The size of the zero block portion used to form H_d(m) */
#define	RANDOM_ZERO_BLOCKSIZE	64	/* (in bytes) == 512 zero bits */

struct randomdev_hash {
	SHA256_CTX	sha;
};

union randomdev_key {
	struct {
		keyInstance key;	/* Key schedule */
		cipherInstance cipher;	/* Rijndael internal */
	};
	struct chacha_ctx chacha;
};

extern bool fortuna_chachamode;

void randomdev_hash_init(struct randomdev_hash *);
void randomdev_hash_iterate(struct randomdev_hash *, const void *, size_t);
void randomdev_hash_finish(struct randomdev_hash *, void *);

void randomdev_encrypt_init(union randomdev_key *, const void *);
void randomdev_keystream(union randomdev_key *context, uint128_t *, void *, u_int);
void randomdev_getkey(union randomdev_key *, const void **, size_t *);

#endif /* SYS_DEV_RANDOM_HASH_H_INCLUDED */
