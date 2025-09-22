/*	$OpenBSD: sm4.h,v 1.2 2025/01/25 17:59:44 tb Exp $	*/
/*
 * Copyright (c) 2017, 2019 Ribose Inc
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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

#ifndef HEADER_SM4_H
#define HEADER_SM4_H

#include <stdint.h>

#include <openssl/opensslconf.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define SM4_DECRYPT     0
#define SM4_ENCRYPT     1

#define SM4_BLOCK_SIZE    16
#define SM4_KEY_SCHEDULE  32

typedef struct sm4_key_st {
	unsigned char opaque[128];
} SM4_KEY;

int SM4_set_key(const uint8_t *key, SM4_KEY *ks);
void SM4_decrypt(const uint8_t *in, uint8_t *out, const SM4_KEY *ks);
void SM4_encrypt(const uint8_t *in, uint8_t *out, const SM4_KEY *ks);

#ifdef  __cplusplus
}
#endif

#endif /* HEADER_SM4_H */
