/* $OpenBSD: e_rc4.c,v 1.20 2024/04/09 13:52:41 beck Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
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
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <stdio.h>

#include <openssl/opensslconf.h>

#ifndef OPENSSL_NO_RC4

#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/rc4.h>

#include "evp_local.h"

/* FIXME: surely this is available elsewhere? */
#define EVP_RC4_KEY_SIZE		16

typedef struct {
    RC4_KEY ks;	/* working key */
} EVP_RC4_KEY;

#define data(ctx) ((EVP_RC4_KEY *)(ctx)->cipher_data)

static int rc4_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc);
static int rc4_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t inl);

static const EVP_CIPHER r4_cipher = {
	.nid = NID_rc4,
	.block_size = 1,
	.key_len = EVP_RC4_KEY_SIZE,
	.iv_len = 0,
	.flags = EVP_CIPH_VARIABLE_LENGTH,
	.init = rc4_init_key,
	.do_cipher = rc4_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_RC4_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

static const EVP_CIPHER r4_40_cipher = {
	.nid = NID_rc4_40,
	.block_size = 1,
	.key_len = 5,
	.iv_len = 0,
	.flags = EVP_CIPH_VARIABLE_LENGTH,
	.init = rc4_init_key,
	.do_cipher = rc4_cipher,
	.cleanup = NULL,
	.ctx_size = sizeof(EVP_RC4_KEY),
	.set_asn1_parameters = NULL,
	.get_asn1_parameters = NULL,
	.ctrl = NULL,
};

const EVP_CIPHER *
EVP_rc4(void)
{
	return (&r4_cipher);
}
LCRYPTO_ALIAS(EVP_rc4);

const EVP_CIPHER *
EVP_rc4_40(void)
{
	return (&r4_40_cipher);
}
LCRYPTO_ALIAS(EVP_rc4_40);

static int
rc4_init_key(EVP_CIPHER_CTX *ctx, const unsigned char *key,
    const unsigned char *iv, int enc)
{
	RC4_set_key(&data(ctx)->ks, EVP_CIPHER_CTX_key_length(ctx), key);
	return 1;
}

static int
rc4_cipher(EVP_CIPHER_CTX *ctx, unsigned char *out,
    const unsigned char *in, size_t inl)
{
	RC4(&data(ctx)->ks, inl, in, out);
	return 1;
}
#endif
