/* $OpenBSD: bcrypt_pbkdf.c,v 1.3 2021/03/12 10:22:46 jsg Exp $ */
/*
 * Copyright (c) 2013 Ted Unangst <tedu@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <lib/libsa/stand.h>
#include <sys/param.h>
 
#include "bcrypt_pbkdf.h"
#include "blowfish.h"
#include "sha2.h"

#define	MINIMUM(a,b) (((a) < (b)) ? (a) : (b))

/*
 * pkcs #5 pbkdf2 implementation using the "bcrypt" hash
 *
 * The bcrypt hash function is derived from the bcrypt password hashing
 * function with the following modifications:
 * 1. The input password and salt are preprocessed with SHA512.
 * 2. The output length is expanded to 256 bits.
 * 3. Subsequently the magic string to be encrypted is lengthened and modified
 *    to "OxychromaticBlowfishSwatDynamite"
 * 4. The hash function is defined to perform 64 rounds of initial state
 *    expansion. (More rounds are performed by iterating the hash.)
 *
 * Note that this implementation pulls the SHA512 operations into the caller
 * as a performance optimization.
 *
 * One modification from official pbkdf2. Instead of outputting key material
 * linearly, we mix it. pbkdf2 has a known weakness where if one uses it to
 * generate (e.g.) 512 bits of key material for use as two 256 bit keys, an
 * attacker can merely run once through the outer loop, but the user
 * always runs it twice. Shuffling output bytes requires computing the
 * entirety of the key material to assemble any subkey. This is something a
 * wise caller could do; we just do it for you.
 */

#define BCRYPT_WORDS 8
#define BCRYPT_HASHSIZE (BCRYPT_WORDS * 4)

static void
bcrypt_hash(uint8_t *sha2pass, uint8_t *sha2salt, uint8_t *out)
{
	blf_ctx state;
	uint8_t ciphertext[BCRYPT_HASHSIZE] =
	    "OxychromaticBlowfishSwatDynamite";
	uint32_t cdata[BCRYPT_WORDS];
	int i;
	uint16_t j;
	size_t shalen = SHA512_DIGEST_LENGTH;

	/* key expansion */
	Blowfish_initstate(&state);
	Blowfish_expandstate(&state, sha2salt, shalen, sha2pass, shalen);
	for (i = 0; i < 64; i++) {
		Blowfish_expand0state(&state, sha2salt, shalen);
		Blowfish_expand0state(&state, sha2pass, shalen);
	}

	/* encryption */
	j = 0;
	for (i = 0; i < BCRYPT_WORDS; i++)
		cdata[i] = Blowfish_stream2word(ciphertext, sizeof(ciphertext),
		    &j);
	for (i = 0; i < 64; i++)
		blf_enc(&state, cdata, BCRYPT_WORDS / 2);

	/* copy out */
	for (i = 0; i < BCRYPT_WORDS; i++) {
		out[4 * i + 3] = (cdata[i] >> 24) & 0xff;
		out[4 * i + 2] = (cdata[i] >> 16) & 0xff;
		out[4 * i + 1] = (cdata[i] >> 8) & 0xff;
		out[4 * i + 0] = cdata[i] & 0xff;
	}

	/* zap */
	explicit_bzero(ciphertext, sizeof(ciphertext));
	explicit_bzero(cdata, sizeof(cdata));
	explicit_bzero(&state, sizeof(state));
}

int
bcrypt_pbkdf(const char *pass, size_t passlen, const uint8_t *salt, size_t saltlen,
    uint8_t *key, size_t keylen, unsigned int rounds)
{
	SHA2_CTX ctx;
	uint8_t sha2pass[SHA512_DIGEST_LENGTH];
	uint8_t sha2salt[SHA512_DIGEST_LENGTH];
	uint8_t out[BCRYPT_HASHSIZE];
	uint8_t tmpout[BCRYPT_HASHSIZE];
	uint8_t countsalt[4];
	size_t i, j, amt, stride;
	uint32_t count;
	size_t origkeylen = keylen;

	/* nothing crazy */
	if (rounds < 1)
		return -1;
	if (passlen == 0 || saltlen == 0 || keylen == 0 ||
	    keylen > sizeof(out) * sizeof(out))
		return -1;
	stride = (keylen + sizeof(out) - 1) / sizeof(out);
	amt = (keylen + stride - 1) / stride;

	/* collapse password */
	SHA512Init(&ctx);
	SHA512Update(&ctx, pass, passlen);
	SHA512Final(sha2pass, &ctx);


	/* generate key, sizeof(out) at a time */
	for (count = 1; keylen > 0; count++) {
		countsalt[0] = (count >> 24) & 0xff;
		countsalt[1] = (count >> 16) & 0xff;
		countsalt[2] = (count >> 8) & 0xff;
		countsalt[3] = count & 0xff;

		/* first round, salt is salt */
		SHA512Init(&ctx);
		SHA512Update(&ctx, salt, saltlen);
		SHA512Update(&ctx, countsalt, sizeof(countsalt));
		SHA512Final(sha2salt, &ctx);
		bcrypt_hash(sha2pass, sha2salt, tmpout);
		memcpy(out, tmpout, sizeof(out));

		for (i = 1; i < rounds; i++) {
			/* subsequent rounds, salt is previous output */
			SHA512Init(&ctx);
			SHA512Update(&ctx, tmpout, sizeof(tmpout));
			SHA512Final(sha2salt, &ctx);
			bcrypt_hash(sha2pass, sha2salt, tmpout);
			for (j = 0; j < sizeof(out); j++)
				out[j] ^= tmpout[j];
		}

		/*
		 * pbkdf2 deviation: output the key material non-linearly.
		 */
		amt = MINIMUM(amt, keylen);
		for (i = 0; i < amt; i++) {
			size_t dest = i * stride + (count - 1);
			if (dest >= origkeylen)
				break;
			key[dest] = out[i];
		}
		keylen -= i;
	}

	/* zap */
	explicit_bzero(&ctx, sizeof(ctx));
	explicit_bzero(out, sizeof(out));

	return 0;
}
