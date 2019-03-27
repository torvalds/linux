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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/random.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#else /* !_KERNEL */
#include <sys/param.h>
#include <sys/types.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>
#include "unit_test.h"
#endif /* _KERNEL */

#define CHACHA_EMBED
#define KEYSTREAM_ONLY
#define CHACHA_NONCE0_CTR128
#include <crypto/chacha20/chacha.c>
#include <crypto/rijndael/rijndael-api-fst.h>
#include <crypto/sha2/sha256.h>

#include <dev/random/hash.h>
#ifdef _KERNEL
#include <dev/random/randomdev.h>
#endif

/* This code presumes that RANDOM_KEYSIZE is twice as large as RANDOM_BLOCKSIZE */
CTASSERT(RANDOM_KEYSIZE == 2*RANDOM_BLOCKSIZE);

/* Validate that full Chacha IV is as large as the 128-bit counter */
_Static_assert(CHACHA_STATELEN == RANDOM_BLOCKSIZE, "");

/*
 * Experimental Chacha20-based PRF for Fortuna keystream primitive.  For now,
 * disabled by default.  But we may enable it in the future.
 *
 * Benefits include somewhat faster keystream generation compared with
 * unaccelerated AES-ICM.
 */
bool random_chachamode = false;
#ifdef _KERNEL
SYSCTL_BOOL(_kern_random, OID_AUTO, use_chacha20_cipher, CTLFLAG_RDTUN,
    &random_chachamode, 0,
    "If non-zero, use the ChaCha20 cipher for randomdev PRF.  "
    "If zero, use AES-ICM cipher for randomdev PRF (default).");
#endif

/* Initialise the hash */
void
randomdev_hash_init(struct randomdev_hash *context)
{

	SHA256_Init(&context->sha);
}

/* Iterate the hash */
void
randomdev_hash_iterate(struct randomdev_hash *context, const void *data, size_t size)
{

	SHA256_Update(&context->sha, data, size);
}

/* Conclude by returning the hash in the supplied <*buf> which must be
 * RANDOM_KEYSIZE bytes long.
 */
void
randomdev_hash_finish(struct randomdev_hash *context, void *buf)
{

	SHA256_Final(buf, &context->sha);
}

/* Initialise the encryption routine by setting up the key schedule
 * from the supplied <*data> which must be RANDOM_KEYSIZE bytes of binary
 * data.
 */
void
randomdev_encrypt_init(union randomdev_key *context, const void *data)
{

	if (random_chachamode) {
		chacha_keysetup(&context->chacha, data, RANDOM_KEYSIZE * 8);
	} else {
		rijndael_cipherInit(&context->cipher, MODE_ECB, NULL);
		rijndael_makeKey(&context->key, DIR_ENCRYPT, RANDOM_KEYSIZE*8, data);
	}
}

/*
 * Create a psuedorandom output stream of 'blockcount' blocks using a CTR-mode
 * cipher or similar.  The 128-bit counter is supplied in the in-out parmeter
 * 'ctr.'  The output stream goes to 'd_out.'  'blockcount' RANDOM_BLOCKSIZE
 * bytes are generated.
 */
void
randomdev_keystream(union randomdev_key *context, uint128_t *ctr,
    void *d_out, u_int blockcount)
{
	u_int i;

	if (random_chachamode) {
		uint128_t lectr;

		/*
		 * Chacha always encodes and increments the counter little
		 * endian.  So on BE machines, we must provide a swapped
		 * counter to chacha, and swap the output too.
		 */
		le128enc(&lectr, *ctr);

		chacha_ivsetup(&context->chacha, NULL, (const void *)&lectr);
		chacha_encrypt_bytes(&context->chacha, NULL, d_out,
		    RANDOM_BLOCKSIZE * blockcount);

		/*
		 * Decode Chacha-updated LE counter to native endian and store
		 * it back in the caller's in-out parameter.
		 */
		chacha_ctrsave(&context->chacha, (void *)&lectr);
		*ctr = le128dec(&lectr);
	} else {
		for (i = 0; i < blockcount; i++) {
			/*-
			 * FS&K - r = r|E(K,C)
			 *      - C = C + 1
			 */
			rijndael_blockEncrypt(&context->cipher, &context->key,
			    (void *)ctr, RANDOM_BLOCKSIZE * 8, d_out);
			d_out = (char *)d_out + RANDOM_BLOCKSIZE;
			uint128_increment(ctr);
		}
	}
}

/*
 * Fetch a pointer to the relevant key material and its size.
 *
 * This API is expected to only be used only for reseeding, where the
 * endianness does not matter; the goal is to simply incorporate the key
 * material into the hash iterator that will produce key'.
 *
 * Do not expect the buffer pointed to by this API to match the exact
 * endianness, etc, as the key material that was supplied to
 * randomdev_encrypt_init().
 */
void
randomdev_getkey(union randomdev_key *context, const void **keyp, size_t *szp)
{

	if (!random_chachamode) {
		*keyp = &context->key.keyMaterial;
		*szp = context->key.keyLen / 8;
		return;
	}

	/* Chacha20 mode */
	*keyp = (const void *)&context->chacha.input[4];

	/* Sanity check keysize */
	if (context->chacha.input[0] == U8TO32_LITTLE(sigma) &&
	    context->chacha.input[1] == U8TO32_LITTLE(&sigma[4]) &&
	    context->chacha.input[2] == U8TO32_LITTLE(&sigma[8]) &&
	    context->chacha.input[3] == U8TO32_LITTLE(&sigma[12])) {
		*szp = 32;
		return;
	}

#if 0
	/*
	 * Included for the sake of completeness; as-implemented, Fortuna
	 * doesn't need or use 128-bit Chacha20.
	 */
	if (context->chacha->input[0] == U8TO32_LITTLE(tau) &&
	    context->chacha->input[1] == U8TO32_LITTLE(&tau[4]) &&
	    context->chacha->input[2] == U8TO32_LITTLE(&tau[8]) &&
	    context->chacha->input[3] == U8TO32_LITTLE(&tau[12])) {
		*szp = 16;
		return;
	}
#endif

#ifdef _KERNEL
	panic("%s: Invalid chacha20 keysize: %16D\n", __func__,
	    (void *)context->chacha.input, " ");
#else
	raise(SIGKILL);
#endif
}
