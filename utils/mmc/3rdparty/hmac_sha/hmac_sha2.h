/*
 * HMAC-SHA-224/256/384/512 implementation
 * Last update: 06/15/2005
 * Issue date:  06/15/2005
 *
 * Since this code has been incorporated into a GPLv2 project, it is
 * distributed under GPLv2 inside mmc-utils.  The original BSD license
 * that the code was released under is included below for clarity.
 *
 * Copyright (C) 2005 Olivier Gay <olivier.gay@a3.epfl.ch>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef HMAC_SHA2_H
#define HMAC_SHA2_H

#include "sha2.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    sha224_ctx ctx_inside;
    sha224_ctx ctx_outside;

    /* for hmac_reinit */
    sha224_ctx ctx_inside_reinit;
    sha224_ctx ctx_outside_reinit;

    unsigned char block_ipad[SHA224_BLOCK_SIZE];
    unsigned char block_opad[SHA224_BLOCK_SIZE];
} hmac_sha224_ctx;

typedef struct {
    sha256_ctx ctx_inside;
    sha256_ctx ctx_outside;

    /* for hmac_reinit */
    sha256_ctx ctx_inside_reinit;
    sha256_ctx ctx_outside_reinit;

    unsigned char block_ipad[SHA256_BLOCK_SIZE];
    unsigned char block_opad[SHA256_BLOCK_SIZE];
} hmac_sha256_ctx;

typedef struct {
    sha384_ctx ctx_inside;
    sha384_ctx ctx_outside;

    /* for hmac_reinit */
    sha384_ctx ctx_inside_reinit;
    sha384_ctx ctx_outside_reinit;

    unsigned char block_ipad[SHA384_BLOCK_SIZE];
    unsigned char block_opad[SHA384_BLOCK_SIZE];
} hmac_sha384_ctx;

typedef struct {
    sha512_ctx ctx_inside;
    sha512_ctx ctx_outside;

    /* for hmac_reinit */
    sha512_ctx ctx_inside_reinit;
    sha512_ctx ctx_outside_reinit;

    unsigned char block_ipad[SHA512_BLOCK_SIZE];
    unsigned char block_opad[SHA512_BLOCK_SIZE];
} hmac_sha512_ctx;

void hmac_sha224_init(hmac_sha224_ctx *ctx, const unsigned char *key,
                      unsigned int key_size);
void hmac_sha224_reinit(hmac_sha224_ctx *ctx);
void hmac_sha224_update(hmac_sha224_ctx *ctx, const unsigned char *message,
                        unsigned int message_len);
void hmac_sha224_final(hmac_sha224_ctx *ctx, unsigned char *mac,
                       unsigned int mac_size);
void hmac_sha224(const unsigned char *key, unsigned int key_size,
                 const unsigned char *message, unsigned int message_len,
                 unsigned char *mac, unsigned mac_size);

void hmac_sha256_init(hmac_sha256_ctx *ctx, const unsigned char *key,
                      unsigned int key_size);
void hmac_sha256_reinit(hmac_sha256_ctx *ctx);
void hmac_sha256_update(hmac_sha256_ctx *ctx, const unsigned char *message,
                        unsigned int message_len);
void hmac_sha256_final(hmac_sha256_ctx *ctx, unsigned char *mac,
                       unsigned int mac_size);
void hmac_sha256(const unsigned char *key, unsigned int key_size,
                 const unsigned char *message, unsigned int message_len,
                 unsigned char *mac, unsigned mac_size);

void hmac_sha384_init(hmac_sha384_ctx *ctx, const unsigned char *key,
                      unsigned int key_size);
void hmac_sha384_reinit(hmac_sha384_ctx *ctx);
void hmac_sha384_update(hmac_sha384_ctx *ctx, const unsigned char *message,
                        unsigned int message_len);
void hmac_sha384_final(hmac_sha384_ctx *ctx, unsigned char *mac,
                       unsigned int mac_size);
void hmac_sha384(const unsigned char *key, unsigned int key_size,
                 const unsigned char *message, unsigned int message_len,
                 unsigned char *mac, unsigned mac_size);

void hmac_sha512_init(hmac_sha512_ctx *ctx, const unsigned char *key,
                      unsigned int key_size);
void hmac_sha512_reinit(hmac_sha512_ctx *ctx);
void hmac_sha512_update(hmac_sha512_ctx *ctx, const unsigned char *message,
                        unsigned int message_len);
void hmac_sha512_final(hmac_sha512_ctx *ctx, unsigned char *mac,
                       unsigned int mac_size);
void hmac_sha512(const unsigned char *key, unsigned int key_size,
                 const unsigned char *message, unsigned int message_len,
                 unsigned char *mac, unsigned mac_size);

#ifdef __cplusplus
}
#endif

#endif /* !HMAC_SHA2_H */

