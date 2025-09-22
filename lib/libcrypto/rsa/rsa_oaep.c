/* $OpenBSD: rsa_oaep.c,v 1.41 2025/08/25 18:47:39 tb Exp $ */
/*
 * Copyright 1999-2018 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * 3. All advertising materials mentioning features or use of this
 *    software must display the following acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit. (http://www.openssl.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    openssl-core@openssl.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.openssl.org/)"
 *
 * THIS SOFTWARE IS PROVIDED BY THE OpenSSL PROJECT ``AS IS'' AND ANY
 * EXPRESSED OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE OpenSSL PROJECT OR
 * ITS CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * ====================================================================
 *
 * This product includes cryptographic software written by Eric Young
 * (eay@cryptsoft.com).  This product includes software written by Tim
 * Hudson (tjh@cryptsoft.com).
 *
 */

/* EME-OAEP as defined in RFC 2437 (PKCS #1 v2.0) */

/* See Victor Shoup, "OAEP reconsidered," Nov. 2000,
 * <URL: http://www.shoup.net/papers/oaep.ps.Z>
 * for problems with the security proof for the
 * original OAEP scheme, which EME-OAEP is based on.
 *
 * A new proof can be found in E. Fujisaki, T. Okamoto,
 * D. Pointcheval, J. Stern, "RSA-OEAP is Still Alive!",
 * Dec. 2000, <URL: http://eprint.iacr.org/2000/061/>.
 * The new proof has stronger requirements for the
 * underlying permutation: "partial-one-wayness" instead
 * of one-wayness.  For the RSA function, this is
 * an equivalent notion.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/rsa.h>
#include <openssl/sha.h>

#include "constant_time.h"
#include "err_local.h"
#include "evp_local.h"
#include "rsa_local.h"

int
RSA_padding_add_PKCS1_OAEP(unsigned char *to, int tlen,
    const unsigned char *from, int flen, const unsigned char *param, int plen)
{
	return RSA_padding_add_PKCS1_OAEP_mgf1(to, tlen, from, flen, param,
	    plen, NULL, NULL);
}
LCRYPTO_ALIAS(RSA_padding_add_PKCS1_OAEP);

int
RSA_padding_add_PKCS1_OAEP_mgf1(unsigned char *to, int tlen,
    const unsigned char *from, int flen, const unsigned char *param, int plen,
    const EVP_MD *md, const EVP_MD *mgf1md)
{
	int i, emlen = tlen - 1;
	unsigned char *db, *seed;
	unsigned char *dbmask = NULL;
	unsigned char seedmask[EVP_MAX_MD_SIZE];
	int mdlen, dbmask_len = 0;
	int rv = 0;

	if (md == NULL)
		md = EVP_sha1();
	if (mgf1md == NULL)
		mgf1md = md;

	if ((mdlen = EVP_MD_size(md)) <= 0)
		goto err;

	if (flen > emlen - 2 * mdlen - 1) {
		RSAerror(RSA_R_DATA_TOO_LARGE_FOR_KEY_SIZE);
		goto err;
	}

	if (emlen < 2 * mdlen + 1) {
		RSAerror(RSA_R_KEY_SIZE_TOO_SMALL);
		goto err;
	}

	to[0] = 0;
	seed = to + 1;
	db = to + mdlen + 1;

	if (!EVP_Digest((void *)param, plen, db, NULL, md, NULL))
		goto err;

	memset(db + mdlen, 0, emlen - flen - 2 * mdlen - 1);
	db[emlen - flen - mdlen - 1] = 0x01;
	memcpy(db + emlen - flen - mdlen, from, flen);
	arc4random_buf(seed, mdlen);

	dbmask_len = emlen - mdlen;
	if ((dbmask = malloc(dbmask_len)) == NULL) {
		RSAerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	if (PKCS1_MGF1(dbmask, dbmask_len, seed, mdlen, mgf1md) < 0)
		goto err;
	for (i = 0; i < dbmask_len; i++)
		db[i] ^= dbmask[i];
	if (PKCS1_MGF1(seedmask, mdlen, db, dbmask_len, mgf1md) < 0)
		goto err;
	for (i = 0; i < mdlen; i++)
		seed[i] ^= seedmask[i];

	rv = 1;

 err:
	explicit_bzero(seedmask, sizeof(seedmask));
	freezero(dbmask, dbmask_len);

	return rv;
}
LCRYPTO_ALIAS(RSA_padding_add_PKCS1_OAEP_mgf1);

int
RSA_padding_check_PKCS1_OAEP(unsigned char *to, int tlen,
    const unsigned char *from, int flen, int num, const unsigned char *param,
    int plen)
{
	return RSA_padding_check_PKCS1_OAEP_mgf1(to, tlen, from, flen, num,
	    param, plen, NULL, NULL);
}
LCRYPTO_ALIAS(RSA_padding_check_PKCS1_OAEP);

int
RSA_padding_check_PKCS1_OAEP_mgf1(unsigned char *to, int tlen,
    const unsigned char *from, int flen, int num, const unsigned char *param,
    int plen, const EVP_MD *md, const EVP_MD *mgf1md)
{
	int i, dblen = 0, mlen = -1, one_index = 0, msg_index;
	unsigned int good = 0, found_one_byte, mask;
	const unsigned char *maskedseed, *maskeddb;
	unsigned char seed[EVP_MAX_MD_SIZE], phash[EVP_MAX_MD_SIZE];
	unsigned char *db = NULL, *em = NULL;
	int mdlen;

	if (md == NULL)
		md = EVP_sha1();
	if (mgf1md == NULL)
		mgf1md = md;

	if ((mdlen = EVP_MD_size(md)) <= 0)
		return -1;

	if (tlen <= 0 || flen <= 0)
		return -1;

	/*
	 * |num| is the length of the modulus; |flen| is the length of the
	 * encoded message. Therefore, for any |from| that was obtained by
	 * decrypting a ciphertext, we must have |flen| <= |num|. Similarly,
	 * |num| >= 2 * |mdlen| + 2 must hold for the modulus irrespective
	 * of the ciphertext, see PKCS #1 v2.2, section 7.1.2.
	 * This does not leak any side-channel information.
	 */
	if (num < flen || num < 2 * mdlen + 2) {
		RSAerror(RSA_R_OAEP_DECODING_ERROR);
		return -1;
	}

	dblen = num - mdlen - 1;
	if ((db = malloc(dblen)) == NULL) {
		RSAerror(ERR_R_MALLOC_FAILURE);
		goto cleanup;
	}
	if ((em = malloc(num)) == NULL) {
		RSAerror(ERR_R_MALLOC_FAILURE);
		goto cleanup;
	}

	/*
	 * Caller is encouraged to pass zero-padded message created with
	 * BN_bn2binpad. Trouble is that since we can't read out of |from|'s
	 * bounds, it's impossible to have an invariant memory access pattern
	 * in case |from| was not zero-padded in advance.
	 */
	for (from += flen, em += num, i = 0; i < num; i++) {
		mask = ~constant_time_is_zero(flen);
		flen -= 1 & mask;
		from -= 1 & mask;
		*--em = *from & mask;
	}

	/*
	 * The first byte must be zero, however we must not leak if this is
	 * true. See James H. Manger, "A Chosen Ciphertext Attack on RSA
	 * Optimal Asymmetric Encryption Padding (OAEP) [...]", CRYPTO 2001).
	 */
	good = constant_time_is_zero(em[0]);

	maskedseed = em + 1;
	maskeddb = em + 1 + mdlen;

	if (PKCS1_MGF1(seed, mdlen, maskeddb, dblen, mgf1md))
		goto cleanup;
	for (i = 0; i < mdlen; i++)
		seed[i] ^= maskedseed[i];

	if (PKCS1_MGF1(db, dblen, seed, mdlen, mgf1md))
		goto cleanup;
	for (i = 0; i < dblen; i++)
		db[i] ^= maskeddb[i];

	if (!EVP_Digest((void *)param, plen, phash, NULL, md, NULL))
		goto cleanup;

	good &= constant_time_is_zero(timingsafe_memcmp(db, phash, mdlen));

	found_one_byte = 0;
	for (i = mdlen; i < dblen; i++) {
		/*
		 * Padding consists of a number of 0-bytes, followed by a 1.
		 */
		unsigned int equals1 = constant_time_eq(db[i], 1);
		unsigned int equals0 = constant_time_is_zero(db[i]);

		one_index = constant_time_select_int(~found_one_byte & equals1,
		    i, one_index);
		found_one_byte |= equals1;
		good &= (found_one_byte | equals0);
	}

	good &= found_one_byte;

	/*
	 * At this point |good| is zero unless the plaintext was valid,
	 * so plaintext-awareness ensures timing side-channels are no longer a
	 * concern.
	 */
	msg_index = one_index + 1;
	mlen = dblen - msg_index;

	/*
	 * For good measure, do this check in constant time as well.
	 */
	good &= constant_time_ge(tlen, mlen);

	/*
	 * Even though we can't fake result's length, we can pretend copying
	 * |tlen| bytes where |mlen| bytes would be real. The last |tlen| of
	 * |dblen| bytes are viewed as a circular buffer starting at |tlen|-|mlen'|,
	 * where |mlen'| is the "saturated" |mlen| value. Deducing information
	 * about failure or |mlen| would require an attacker to observe
	 * memory access patterns with byte granularity *as it occurs*. It
	 * should be noted that failure is indistinguishable from normal
	 * operation if |tlen| is fixed by protocol.
	 */
	tlen = constant_time_select_int(constant_time_lt(dblen - mdlen - 1, tlen),
	    dblen - mdlen - 1, tlen);
	msg_index = constant_time_select_int(good, msg_index, dblen - tlen);
	mlen = dblen - msg_index;
	for (mask = good, i = 0; i < tlen; i++) {
		unsigned int equals = constant_time_eq(msg_index, dblen);

		msg_index -= tlen & equals;	/* rewind at EOF */
		mask &= ~equals;		/* mask = 0 at EOF */
		to[i] = constant_time_select_8(mask, db[msg_index++], to[i]);
	}

	/*
	 * To avoid chosen ciphertext attacks, the error message should not
	 * reveal which kind of decoding error happened.
	 */
	RSAerror(RSA_R_OAEP_DECODING_ERROR);
	err_clear_last_constant_time(1 & good);

 cleanup:
	explicit_bzero(seed, sizeof(seed));
	freezero(db, dblen);
	freezero(em, num);

	return constant_time_select_int(good, mlen, -1);
}
LCRYPTO_ALIAS(RSA_padding_check_PKCS1_OAEP_mgf1);

int
PKCS1_MGF1(unsigned char *mask, long len, const unsigned char *seed,
    long seedlen, const EVP_MD *dgst)
{
	long i, outlen = 0;
	unsigned char cnt[4];
	EVP_MD_CTX *md_ctx;
	unsigned char md[EVP_MAX_MD_SIZE];
	int mdlen;
	int rv = -1;

	if ((md_ctx = EVP_MD_CTX_new()) == NULL)
		goto err;

	mdlen = EVP_MD_size(dgst);
	if (mdlen < 0)
		goto err;
	for (i = 0; outlen < len; i++) {
		cnt[0] = (unsigned char)((i >> 24) & 255);
		cnt[1] = (unsigned char)((i >> 16) & 255);
		cnt[2] = (unsigned char)((i >> 8)) & 255;
		cnt[3] = (unsigned char)(i & 255);
		if (!EVP_DigestInit_ex(md_ctx, dgst, NULL) ||
		    !EVP_DigestUpdate(md_ctx, seed, seedlen) ||
		    !EVP_DigestUpdate(md_ctx, cnt, 4))
			goto err;
		if (outlen + mdlen <= len) {
			if (!EVP_DigestFinal_ex(md_ctx, mask + outlen, NULL))
				goto err;
			outlen += mdlen;
		} else {
			if (!EVP_DigestFinal_ex(md_ctx, md, NULL))
				goto err;
			memcpy(mask + outlen, md, len - outlen);
			outlen = len;
		}
	}

	rv = 0;

 err:
	EVP_MD_CTX_free(md_ctx);

	return rv;
}
LCRYPTO_ALIAS(PKCS1_MGF1);
