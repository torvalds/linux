/*	$OpenBSD: sm3.h,v 1.2 2025/01/25 17:59:44 tb Exp $	*/
/*
 * Copyright (c) 2018, Ribose Inc
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

#ifndef HEADER_SM3_H
#define HEADER_SM3_H

#include <stddef.h>
#include <openssl/opensslconf.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define SM3_DIGEST_LENGTH 32
#define SM3_WORD unsigned int

#define SM3_CBLOCK 64
#define SM3_LBLOCK (SM3_CBLOCK / 4)

typedef struct SM3state_st {
	SM3_WORD A, B, C, D, E, F, G, H;
	SM3_WORD Nl, Nh;
	SM3_WORD data[SM3_LBLOCK];
	unsigned int num;
} SM3_CTX;

int SM3_Init(SM3_CTX *c);
int SM3_Update(SM3_CTX *c, const void *data, size_t len);
int SM3_Final(unsigned char *md, SM3_CTX *c);

#ifdef  __cplusplus
}
#endif

#endif /* HEADER_SM3_H */
