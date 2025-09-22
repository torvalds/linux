/* $OpenBSD: aes_local.h,v 1.11 2025/07/22 09:29:31 jsing Exp $ */
/* ====================================================================
 * Copyright (c) 1998-2002 The OpenSSL Project.  All rights reserved.
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

#ifndef HEADER_AES_LOCAL_H
#define HEADER_AES_LOCAL_H

#include <openssl/opensslconf.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__BEGIN_HIDDEN_DECLS

/* This controls loop-unrolling in aes_core.c */
#undef FULL_UNROLL

void aes_encrypt_block128(const unsigned char *in, unsigned char *out,
   const void *key);

void aes_ctr32_encrypt_ctr128f(const unsigned char *in, unsigned char *out,
    size_t blocks, const void *key, const unsigned char ivec[AES_BLOCK_SIZE]);

void aes_ccm64_encrypt_ccm128f(const unsigned char *in, unsigned char *out,
    size_t blocks, const void *key, const unsigned char ivec[16],
    unsigned char cmac[16]);

void aes_ccm64_decrypt_ccm128f(const unsigned char *in, unsigned char *out,
    size_t blocks, const void *key, const unsigned char ivec[16],
    unsigned char cmac[16]);

void aes_ecb_encrypt_internal(const unsigned char *in, unsigned char *out,
    size_t len, const AES_KEY *key, int encrypt);

void aes_xts_encrypt_internal(const char unsigned *in, char unsigned *out,
    size_t len, const AES_KEY *key1, const AES_KEY *key2,
    const unsigned char iv[16], int encrypt);

__END_HIDDEN_DECLS

#endif /* !HEADER_AES_LOCAL_H */
