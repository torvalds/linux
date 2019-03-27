/*-
 * Copyright (c) 2016 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Andrew Turner under
 * sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * This code is built with floating-point enabled. Make sure to have entered
 * into floating-point context before calling any of these functions.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>

#include <opencrypto/cryptodev.h>
#include <crypto/armv8/armv8_crypto.h>

#include <arm_neon.h>

static uint8x16_t
armv8_aes_enc(int rounds, const uint8x16_t *keysched, const uint8x16_t from)
{
	uint8x16_t tmp;
	int i;

	tmp = from;
	for (i = 0; i < rounds - 1; i += 2) {
		tmp = vaeseq_u8(tmp, keysched[i]);
		tmp = vaesmcq_u8(tmp);
		tmp = vaeseq_u8(tmp, keysched[i + 1]);
		tmp = vaesmcq_u8(tmp);
	}

	tmp = vaeseq_u8(tmp, keysched[rounds - 1]);
	tmp = vaesmcq_u8(tmp);
	tmp = vaeseq_u8(tmp, keysched[rounds]);
	tmp = veorq_u8(tmp, keysched[rounds + 1]);

	return (tmp);
}

static uint8x16_t
armv8_aes_dec(int rounds, const uint8x16_t *keysched, const uint8x16_t from)
{
	uint8x16_t tmp;
	int i;

	tmp = from;
	for (i = 0; i < rounds - 1; i += 2) {
		tmp = vaesdq_u8(tmp, keysched[i]);
		tmp = vaesimcq_u8(tmp);
		tmp = vaesdq_u8(tmp, keysched[i+1]);
		tmp = vaesimcq_u8(tmp);
	}

	tmp = vaesdq_u8(tmp, keysched[rounds - 1]);
	tmp = vaesimcq_u8(tmp);
	tmp = vaesdq_u8(tmp, keysched[rounds]);
	tmp = veorq_u8(tmp, keysched[rounds + 1]);

	return (tmp);
}

void
armv8_aes_encrypt_cbc(int rounds, const void *key_schedule, size_t len,
    const uint8_t *from, uint8_t *to, const uint8_t iv[static AES_BLOCK_LEN])
{
	uint8x16_t tot, ivreg, tmp;
	size_t i;

	len /= AES_BLOCK_LEN;
	ivreg = vld1q_u8(iv);
	for (i = 0; i < len; i++) {
		tmp = vld1q_u8(from);
		tot = armv8_aes_enc(rounds - 1, key_schedule,
		    veorq_u8(tmp, ivreg));
		ivreg = tot;
		vst1q_u8(to, tot);
		from += AES_BLOCK_LEN;
		to += AES_BLOCK_LEN;
	}
}

void
armv8_aes_decrypt_cbc(int rounds, const void *key_schedule, size_t len,
    uint8_t *buf, const uint8_t iv[static AES_BLOCK_LEN])
{
	uint8x16_t ivreg, nextiv, tmp;
	size_t i;

	len /= AES_BLOCK_LEN;
	ivreg = vld1q_u8(iv);
	for (i = 0; i < len; i++) {
		nextiv = vld1q_u8(buf);
		tmp = armv8_aes_dec(rounds - 1, key_schedule, nextiv);
		vst1q_u8(buf, veorq_u8(tmp, ivreg));
		ivreg = nextiv;
		buf += AES_BLOCK_LEN;
	}
}
