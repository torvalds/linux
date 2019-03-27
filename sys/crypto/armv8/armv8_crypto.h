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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _ARMV8_CRYPTO_H_
#define _ARMV8_CRYPTO_H_

#define	AES128_ROUNDS	10
#define	AES192_ROUNDS	12
#define	AES256_ROUNDS	14
#define	AES_SCHED_LEN	((AES256_ROUNDS + 1) * AES_BLOCK_LEN)

struct armv8_crypto_session {
	uint32_t enc_schedule[AES_SCHED_LEN/4];
	uint32_t dec_schedule[AES_SCHED_LEN/4];
	int algo;
	int rounds;
};

void armv8_aes_encrypt_cbc(int, const void *, size_t, const uint8_t *,
    uint8_t *, const uint8_t[static AES_BLOCK_LEN]);
void armv8_aes_decrypt_cbc(int, const void *, size_t, uint8_t *,
    const uint8_t[static AES_BLOCK_LEN]);

#endif /* _ARMV8_CRYPTO_H_ */
