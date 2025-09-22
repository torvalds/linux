/* $OpenBSD: ctr128.c,v 1.18 2025/05/18 09:05:59 jsing Exp $ */
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

#include "crypto_internal.h"
#include "modes_local.h"

/* NOTE: the IV/counter CTR mode is big-endian.  The code itself
 * is endian-neutral. */

/* increment counter (128-bit int) by 1 */
static void
ctr128_inc(unsigned char *counter)
{
	uint32_t n = 16;
	uint8_t  c;

	do {
		--n;
		c = counter[n];
		++c;
		counter[n] = c;
		if (c)
			return;
	} while (n);
}

static void
ctr128_inc_aligned(unsigned char *counter)
{
#if BYTE_ORDER == LITTLE_ENDIAN
	ctr128_inc(counter);
#else
	size_t *data, c, n;
	data = (size_t *)counter;
	n = 16 / sizeof(size_t);
	do {
		--n;
		c = data[n];
		++c;
		data[n] = c;
		if (c)
			return;
	} while (n);
#endif
}

/* The input encrypted as though 128bit counter mode is being
 * used.  The extra state information to record how much of the
 * 128bit block we have used is contained in *num, and the
 * encrypted counter is kept in ecount_buf.  Both *num and
 * ecount_buf must be initialised with zeros before the first
 * call to CRYPTO_ctr128_encrypt().
 *
 * This algorithm assumes that the counter is in the x lower bits
 * of the IV (ivec), and that the application has full control over
 * overflow and the rest of the IV.  This implementation takes NO
 * responsibility for checking that the counter doesn't overflow
 * into the rest of the IV when incremented.
 */
void
CRYPTO_ctr128_encrypt(const unsigned char *in, unsigned char *out,
    size_t len, const void *key,
    unsigned char ivec[16], unsigned char ecount_buf[16],
    unsigned int *num, block128_f block)
{
	unsigned int n = *num;
	size_t l = 0;

	OPENSSL_assert(n < 16);

	if (16 % sizeof(size_t) == 0)
		do { /* always true actually */
			while (n && len) {
				*(out++) = *(in++) ^ ecount_buf[n];
				--len;
				n = (n + 1) % 16;
			}

#ifdef __STRICT_ALIGNMENT
			if (((size_t)in|(size_t)out|(size_t)ivec) %
			    sizeof(size_t) != 0)
				break;
#endif
			while (len >= 16) {
				(*block)(ivec, ecount_buf, key);
				ctr128_inc_aligned(ivec);
				for (; n < 16; n += sizeof(size_t))
					*(size_t *)(out + n) =
					    *(size_t *)(in + n) ^ *(size_t *)(ecount_buf +
					    n);
				len -= 16;
				out += 16;
				in += 16;
				n = 0;
			}
			if (len) {
				(*block)(ivec, ecount_buf, key);
				ctr128_inc_aligned(ivec);
				while (len--) {
					out[n] = in[n] ^ ecount_buf[n];
					++n;
				}
			}
			*num = n;
			return;
		} while (0);
	/* the rest would be commonly eliminated by x86* compiler */
	while (l < len) {
		if (n == 0) {
			(*block)(ivec, ecount_buf, key);
			ctr128_inc(ivec);
		}
		out[l] = in[l] ^ ecount_buf[n];
		++l;
		n = (n + 1) % 16;
	}

	*num = n;
}
LCRYPTO_ALIAS(CRYPTO_ctr128_encrypt);

/* increment upper 96 bits of 128-bit counter by 1 */
static void
ctr96_inc(unsigned char *counter)
{
	uint32_t n = 12;
	uint8_t  c;

	do {
		--n;
		c = counter[n];
		++c;
		counter[n] = c;
		if (c)
			return;
	} while (n);
}

void
CRYPTO_ctr128_encrypt_ctr32(const unsigned char *in, unsigned char *out,
    size_t len, const void *key,
    unsigned char ivec[16], unsigned char ecount_buf[16],
    unsigned int *num, ctr128_f func)
{
	unsigned int n = *num;
	unsigned int ctr32;

	OPENSSL_assert(n < 16);

	while (n && len) {
		*(out++) = *(in++) ^ ecount_buf[n];
		--len;
		n = (n + 1) % 16;
	}

	ctr32 = crypto_load_be32toh(&ivec[12]);

	while (len >= 16) {
		size_t blocks = len/16;
		/*
		 * 1<<28 is just a not-so-small yet not-so-large number...
		 * Below condition is practically never met, but it has to
		 * be checked for code correctness.
		 */
		if (sizeof(size_t) > sizeof(unsigned int) &&
		    blocks > (1U << 28))
			blocks = (1U << 28);
		/*
		 * As (*func) operates on 32-bit counter, caller
		 * has to handle overflow. 'if' below detects the
		 * overflow, which is then handled by limiting the
		 * amount of blocks to the exact overflow point...
		 */
		ctr32 += (uint32_t)blocks;
		if (ctr32 < blocks) {
			blocks -= ctr32;
			ctr32 = 0;
		}
		(*func)(in, out, blocks, key, ivec);
		/* (*ctr) does not update ivec, caller does: */
		crypto_store_htobe32(&ivec[12], ctr32);
		/* ... overflow was detected, propagate carry. */
		if (ctr32 == 0)
			ctr96_inc(ivec);
		blocks *= 16;
		len -= blocks;
		out += blocks;
		in += blocks;
	}
	if (len) {
		memset(ecount_buf, 0, 16);
		(*func)(ecount_buf, ecount_buf, 1, key, ivec);
		++ctr32;
		crypto_store_htobe32(&ivec[12], ctr32);
		if (ctr32 == 0)
			ctr96_inc(ivec);
		while (len--) {
			out[n] = in[n] ^ ecount_buf[n];
			++n;
		}
	}

	*num = n;
}
LCRYPTO_ALIAS(CRYPTO_ctr128_encrypt_ctr32);
