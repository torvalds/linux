/* $OpenBSD: cfb128.c,v 1.10 2025/04/23 10:09:08 jsing Exp $ */
/* ====================================================================
 * Copyright (c) 2008 The OpenSSL Project.  All rights reserved.
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
 */

#include <string.h>

#include <openssl/crypto.h>

#include "modes_local.h"

/* The input and output encrypted as though 128bit cfb mode is being
 * used.  The extra state information to record how much of the
 * 128bit block we have used is contained in *num;
 */
void
CRYPTO_cfb128_encrypt(const unsigned char *in, unsigned char *out,
    size_t len, const void *key,
    unsigned char ivec[16], int *num,
    int enc, block128_f block)
{
	unsigned int n;
	size_t l = 0;

	n = *num;

	if (enc) {
		if (16 % sizeof(size_t) == 0)
			do {	/* always true actually */
				while (n && len) {
					*(out++) = ivec[n] ^= *(in++);
					--len;
					n = (n + 1) % 16;
				}
#ifdef __STRICT_ALIGNMENT
				if (((size_t)in|(size_t)out|(size_t)ivec) %
				    sizeof(size_t) != 0)
					break;
#endif
				while (len >= 16) {
					(*block)(ivec, ivec, key);
					for (; n < 16; n += sizeof(size_t)) {
						*(size_t *)(out + n) =
						    *(size_t *)(ivec + n) ^= *(size_t *)(in +
						    n);
					}
					len -= 16;
					out += 16;
					in += 16;
					n = 0;
				}
				if (len) {
					(*block)(ivec, ivec, key);
					while (len--) {
						out[n] = ivec[n] ^= in[n];
						++n;
					}
				}
				*num = n;
				return;
			} while (0);
	/* the rest would be commonly eliminated by x86* compiler */
		while (l < len) {
			if (n == 0) {
				(*block)(ivec, ivec, key);
			}
			out[l] = ivec[n] ^= in[l];
			++l;
			n = (n + 1) % 16;
		}
		*num = n;
	} else {
		if (16 % sizeof(size_t) == 0)
			do {	/* always true actually */
				while (n && len) {
					unsigned char c;
					*(out++) = ivec[n] ^ (c = *(in++));
					ivec[n] = c;
					--len;
					n = (n + 1) % 16;
				}
#ifdef __STRICT_ALIGNMENT
				if (((size_t)in|(size_t)out|(size_t)ivec) %
				    sizeof(size_t) != 0)
					break;
#endif
				while (len >= 16) {
					(*block)(ivec, ivec, key);
					for (; n < 16; n += sizeof(size_t)) {
						size_t t = *(size_t *)(in + n);
						*(size_t *)(out + n) = *(size_t *)(ivec +
						    n) ^ t;
						*(size_t *)(ivec + n) = t;
					}
					len -= 16;
					out += 16;
					in += 16;
					n = 0;
				}
				if (len) {
					(*block)(ivec, ivec, key);
					while (len--) {
						unsigned char c;
						out[n] = ivec[n] ^ (c = in[n]);
						ivec[n] = c;
						++n;
					}
				}
				*num = n;
				return;
			} while (0);
	/* the rest would be commonly eliminated by x86* compiler */
		while (l < len) {
			unsigned char c;
			if (n == 0) {
				(*block)(ivec, ivec, key);
			}
			out[l] = ivec[n] ^ (c = in[l]);
			ivec[n] = c;
			++l;
			n = (n + 1) % 16;
		}
		*num = n;
	}
}
LCRYPTO_ALIAS(CRYPTO_cfb128_encrypt);

/* This expects a single block of size nbits for both in and out. Note that
   it corrupts any extra bits in the last byte of out */
static void
cfbr_encrypt_block(const unsigned char *in, unsigned char *out,
    int nbits, const void *key,
    unsigned char ivec[16], int enc,
    block128_f block)
{
	int n, rem, num;
	unsigned char ovec[16*2 + 1];  /* +1 because we dererefence (but don't use) one byte off the end */

	if (nbits <= 0 || nbits > 128)
		return;

	/* fill in the first half of the new IV with the current IV */
	memcpy(ovec, ivec, 16);
	/* construct the new IV */
	(*block)(ivec, ivec, key);
	num = (nbits + 7)/8;
	if (enc)	/* encrypt the input */
		for (n = 0; n < num; ++n)
			out[n] = (ovec[16 + n] = in[n] ^ ivec[n]);
	else		/* decrypt the input */
		for (n = 0; n < num; ++n)
			out[n] = (ovec[16 + n] = in[n]) ^ ivec[n];
	/* shift ovec left... */
	rem = nbits % 8;
	num = nbits/8;
	if (rem == 0)
		memcpy(ivec, ovec + num, 16);
	else
		for (n = 0; n < 16; ++n)
			ivec[n] = ovec[n + num] << rem |
			    ovec[n + num + 1] >> (8 - rem);

    /* it is not necessary to cleanse ovec, since the IV is not secret */
}

/* N.B. This expects the input to be packed, MS bit first */
void
CRYPTO_cfb128_1_encrypt(const unsigned char *in, unsigned char *out,
    size_t bits, const void *key,
    unsigned char ivec[16], int *num,
    int enc, block128_f block)
{
	size_t n;
	unsigned char c[1], d[1];

	for (n = 0; n < bits; ++n)
	{
		c[0] = (in[n/8] & (1 << (7 - n % 8))) ? 0x80 : 0;
		cfbr_encrypt_block(c, d, 1, key, ivec, enc, block);
		out[n/8] = (out[n/8] & ~(1 << (unsigned int)(7 - n % 8))) |
		    ((d[0] & 0x80) >> (unsigned int)(n % 8));
	}
}
LCRYPTO_ALIAS(CRYPTO_cfb128_1_encrypt);

void
CRYPTO_cfb128_8_encrypt(const unsigned char *in, unsigned char *out,
    size_t length, const void *key,
    unsigned char ivec[16], int *num,
    int enc, block128_f block)
{
	size_t n;

	for (n = 0; n < length; ++n)
		cfbr_encrypt_block(&in[n], &out[n], 8, key, ivec, enc, block);
}
LCRYPTO_ALIAS(CRYPTO_cfb128_8_encrypt);
