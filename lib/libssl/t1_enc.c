/* $OpenBSD: t1_enc.c,v 1.158 2024/07/20 04:04:23 jsing Exp $ */
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
/* ====================================================================
 * Copyright (c) 1998-2007 The OpenSSL Project.  All rights reserved.
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
/* ====================================================================
 * Copyright 2005 Nokia. All rights reserved.
 *
 * The portions of the attached software ("Contribution") is developed by
 * Nokia Corporation and is licensed pursuant to the OpenSSL open source
 * license.
 *
 * The Contribution, originally written by Mika Kousa and Pasi Eronen of
 * Nokia Corporation, consists of the "PSK" (Pre-Shared Key) ciphersuites
 * support (see RFC 4279) to OpenSSL.
 *
 * No patent licenses or other rights except those expressly stated in
 * the OpenSSL open source license shall be deemed granted or received
 * expressly, by implication, estoppel, or otherwise.
 *
 * No assurances are provided by Nokia that the Contribution does not
 * infringe the patent or other intellectual property rights of any third
 * party or that the license provides you with all the necessary rights
 * to make use of the Contribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" WITHOUT WARRANTY OF ANY KIND. IN
 * ADDITION TO THE DISCLAIMERS INCLUDED IN THE LICENSE, NOKIA
 * SPECIFICALLY DISCLAIMS ANY LIABILITY FOR CLAIMS BROUGHT BY YOU OR ANY
 * OTHER ENTITY BASED ON INFRINGEMENT OF INTELLECTUAL PROPERTY RIGHTS OR
 * OTHERWISE.
 */

#include <limits.h>
#include <stdio.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/opensslconf.h>

#include "dtls_local.h"
#include "ssl_local.h"

void
tls1_cleanup_key_block(SSL *s)
{
	tls12_key_block_free(s->s3->hs.tls12.key_block);
	s->s3->hs.tls12.key_block = NULL;
}

/*
 * TLS P_hash() data expansion function - see RFC 5246, section 5.
 */
static int
tls1_P_hash(const EVP_MD *md, const unsigned char *secret, size_t secret_len,
    const void *seed1, size_t seed1_len, const void *seed2, size_t seed2_len,
    const void *seed3, size_t seed3_len, const void *seed4, size_t seed4_len,
    const void *seed5, size_t seed5_len, unsigned char *out, size_t out_len)
{
	unsigned char A1[EVP_MAX_MD_SIZE], hmac[EVP_MAX_MD_SIZE];
	size_t A1_len, hmac_len;
	EVP_MD_CTX *ctx = NULL;
	EVP_PKEY *mac_key = NULL;
	int ret = 0;
	int chunk;
	size_t i;

	chunk = EVP_MD_size(md);
	OPENSSL_assert(chunk >= 0);

	if ((ctx = EVP_MD_CTX_new()) == NULL)
		goto err;

	mac_key = EVP_PKEY_new_mac_key(EVP_PKEY_HMAC, NULL, secret, secret_len);
	if (mac_key == NULL)
		goto err;
	if (!EVP_DigestSignInit(ctx, NULL, md, NULL, mac_key))
		goto err;
	if (seed1 && !EVP_DigestSignUpdate(ctx, seed1, seed1_len))
		goto err;
	if (seed2 && !EVP_DigestSignUpdate(ctx, seed2, seed2_len))
		goto err;
	if (seed3 && !EVP_DigestSignUpdate(ctx, seed3, seed3_len))
		goto err;
	if (seed4 && !EVP_DigestSignUpdate(ctx, seed4, seed4_len))
		goto err;
	if (seed5 && !EVP_DigestSignUpdate(ctx, seed5, seed5_len))
		goto err;
	if (!EVP_DigestSignFinal(ctx, A1, &A1_len))
		goto err;

	for (;;) {
		if (!EVP_DigestSignInit(ctx, NULL, md, NULL, mac_key))
			goto err;
		if (!EVP_DigestSignUpdate(ctx, A1, A1_len))
			goto err;
		if (seed1 && !EVP_DigestSignUpdate(ctx, seed1, seed1_len))
			goto err;
		if (seed2 && !EVP_DigestSignUpdate(ctx, seed2, seed2_len))
			goto err;
		if (seed3 && !EVP_DigestSignUpdate(ctx, seed3, seed3_len))
			goto err;
		if (seed4 && !EVP_DigestSignUpdate(ctx, seed4, seed4_len))
			goto err;
		if (seed5 && !EVP_DigestSignUpdate(ctx, seed5, seed5_len))
			goto err;
		if (!EVP_DigestSignFinal(ctx, hmac, &hmac_len))
			goto err;

		if (hmac_len > out_len)
			hmac_len = out_len;

		for (i = 0; i < hmac_len; i++)
			out[i] ^= hmac[i];

		out += hmac_len;
		out_len -= hmac_len;

		if (out_len == 0)
			break;

		if (!EVP_DigestSignInit(ctx, NULL, md, NULL, mac_key))
			goto err;
		if (!EVP_DigestSignUpdate(ctx, A1, A1_len))
			goto err;
		if (!EVP_DigestSignFinal(ctx, A1, &A1_len))
			goto err;
	}
	ret = 1;

 err:
	EVP_PKEY_free(mac_key);
	EVP_MD_CTX_free(ctx);

	explicit_bzero(A1, sizeof(A1));
	explicit_bzero(hmac, sizeof(hmac));

	return ret;
}

int
tls1_PRF(SSL *s, const unsigned char *secret, size_t secret_len,
    const void *seed1, size_t seed1_len, const void *seed2, size_t seed2_len,
    const void *seed3, size_t seed3_len, const void *seed4, size_t seed4_len,
    const void *seed5, size_t seed5_len, unsigned char *out, size_t out_len)
{
	const EVP_MD *md;
	size_t half_len;

	memset(out, 0, out_len);

	if (!ssl_get_handshake_evp_md(s, &md))
		return (0);

	if (EVP_MD_type(md) == NID_md5_sha1) {
		/*
		 * Partition secret between MD5 and SHA1, then XOR result.
		 * If the secret length is odd, a one byte overlap is used.
		 */
		half_len = secret_len - (secret_len / 2);
		if (!tls1_P_hash(EVP_md5(), secret, half_len, seed1, seed1_len,
		    seed2, seed2_len, seed3, seed3_len, seed4, seed4_len,
		    seed5, seed5_len, out, out_len))
			return (0);

		secret += secret_len - half_len;
		if (!tls1_P_hash(EVP_sha1(), secret, half_len, seed1, seed1_len,
		    seed2, seed2_len, seed3, seed3_len, seed4, seed4_len,
		    seed5, seed5_len, out, out_len))
			return (0);

		return (1);
	}

	if (!tls1_P_hash(md, secret, secret_len, seed1, seed1_len,
	    seed2, seed2_len, seed3, seed3_len, seed4, seed4_len,
	    seed5, seed5_len, out, out_len))
		return (0);

	return (1);
}

int
tls1_generate_key_block(SSL *s, uint8_t *key_block, size_t key_block_len)
{
	return tls1_PRF(s,
	    s->session->master_key, s->session->master_key_length,
	    TLS_MD_KEY_EXPANSION_CONST, TLS_MD_KEY_EXPANSION_CONST_SIZE,
	    s->s3->server_random, SSL3_RANDOM_SIZE,
	    s->s3->client_random, SSL3_RANDOM_SIZE,
	    NULL, 0, NULL, 0, key_block, key_block_len);
}

static int
tls1_change_cipher_state(SSL *s, int is_write)
{
	CBS mac_key, key, iv;

	/* Use client write keys on client write and server read. */
	if ((!s->server && is_write) || (s->server && !is_write)) {
		tls12_key_block_client_write(s->s3->hs.tls12.key_block,
		    &mac_key, &key, &iv);
	} else {
		tls12_key_block_server_write(s->s3->hs.tls12.key_block,
		    &mac_key, &key, &iv);
	}

	if (!is_write) {
		if (!tls12_record_layer_change_read_cipher_state(s->rl,
		    &mac_key, &key, &iv))
			goto err;
		if (SSL_is_dtls(s))
			dtls1_reset_read_seq_numbers(s);
	} else {
		if (!tls12_record_layer_change_write_cipher_state(s->rl,
		    &mac_key, &key, &iv))
			goto err;
	}
	return (1);

 err:
	return (0);
}

int
tls1_change_read_cipher_state(SSL *s)
{
	return tls1_change_cipher_state(s, 0);
}

int
tls1_change_write_cipher_state(SSL *s)
{
	return tls1_change_cipher_state(s, 1);
}

int
tls1_setup_key_block(SSL *s)
{
	struct tls12_key_block *key_block;
	int mac_type = NID_undef, mac_secret_size = 0;
	const EVP_CIPHER *cipher = NULL;
	const EVP_AEAD *aead = NULL;
	const EVP_MD *handshake_hash = NULL;
	const EVP_MD *mac_hash = NULL;
	int ret = 0;

	/*
	 * XXX - callers should be changed so that they only call this
	 * function once.
	 */
	if (s->s3->hs.tls12.key_block != NULL)
		return (1);

	if (s->s3->hs.cipher == NULL)
		return (0);

	if ((s->s3->hs.cipher->algorithm_mac & SSL_AEAD) != 0) {
		if (!ssl_cipher_get_evp_aead(s, &aead)) {
			SSLerror(s, SSL_R_CIPHER_OR_HASH_UNAVAILABLE);
			return (0);
		}
	} else {
		/* XXX - mac_type and mac_secret_size are now unused. */
		if (!ssl_cipher_get_evp(s, &cipher, &mac_hash,
		    &mac_type, &mac_secret_size)) {
			SSLerror(s, SSL_R_CIPHER_OR_HASH_UNAVAILABLE);
			return (0);
		}
	}

	if (!ssl_get_handshake_evp_md(s, &handshake_hash))
		return (0);

	tls12_record_layer_set_aead(s->rl, aead);
	tls12_record_layer_set_cipher_hash(s->rl, cipher,
	    handshake_hash, mac_hash);

	if ((key_block = tls12_key_block_new()) == NULL)
		goto err;
	if (!tls12_key_block_generate(key_block, s, aead, cipher, mac_hash))
		goto err;

	s->s3->hs.tls12.key_block = key_block;
	key_block = NULL;

	if (!(s->options & SSL_OP_DONT_INSERT_EMPTY_FRAGMENTS) &&
	    s->method->version <= TLS1_VERSION) {
		/*
		 * Enable vulnerability countermeasure for CBC ciphers with
		 * known-IV problem (http://www.openssl.org/~bodo/tls-cbc.txt)
		 */
		s->s3->need_empty_fragments = 1;

		if (s->s3->hs.cipher != NULL) {
			if (s->s3->hs.cipher->algorithm_enc == SSL_eNULL)
				s->s3->need_empty_fragments = 0;

#ifndef OPENSSL_NO_RC4
			if (s->s3->hs.cipher->algorithm_enc == SSL_RC4)
				s->s3->need_empty_fragments = 0;
#endif
		}
	}

	ret = 1;

 err:
	tls12_key_block_free(key_block);

	return (ret);
}
