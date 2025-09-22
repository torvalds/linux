/* $OpenBSD: ssl_txt.c,v 1.39 2024/07/22 14:47:15 jsing Exp $ */
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

#include <stdio.h>

#include <openssl/buffer.h>

#include "ssl_local.h"

int
SSL_SESSION_print_fp(FILE *fp, const SSL_SESSION *x)
{
	BIO *b;
	int ret;

	if ((b = BIO_new(BIO_s_file())) == NULL) {
		SSLerrorx(ERR_R_BUF_LIB);
		return 0;
	}
	BIO_set_fp(b, fp, BIO_NOCLOSE);
	ret = SSL_SESSION_print(b, x);
	BIO_free(b);
	return ret;
}
LSSL_ALIAS(SSL_SESSION_print_fp);

int
SSL_SESSION_print(BIO *bp, const SSL_SESSION *x)
{
	const SSL_CIPHER *cipher;
	size_t i;
	int ret = 0;

	if (x == NULL)
		goto err;

	if (BIO_puts(bp, "SSL-Session:\n") <= 0)
		goto err;

	if (BIO_printf(bp, "    Protocol  : %s\n",
	    ssl_version_string(x->ssl_version)) <= 0)
		goto err;

	if ((cipher = ssl3_get_cipher_by_value(x->cipher_value)) == NULL) {
		if (BIO_printf(bp, "    Cipher    : %04X\n",
		    x->cipher_value) <= 0)
			goto err;
	} else {
		const char *cipher_name = "unknown";

		if (cipher->name != NULL)
			cipher_name = cipher->name;

		if (BIO_printf(bp, "    Cipher    : %s\n", cipher_name) <= 0)
			goto err;
	}

	if (BIO_puts(bp, "    Session-ID: ") <= 0)
		goto err;

	for (i = 0; i < x->session_id_length; i++) {
		if (BIO_printf(bp, "%02X", x->session_id[i]) <= 0)
			goto err;
	}

	if (BIO_puts(bp, "\n    Session-ID-ctx: ") <= 0)
		goto err;

	for (i = 0; i < x->sid_ctx_length; i++) {
		if (BIO_printf(bp, "%02X", x->sid_ctx[i]) <= 0)
			goto err;
	}

	if (BIO_puts(bp, "\n    Master-Key: ") <= 0)
		goto err;

	for (i = 0; i < x->master_key_length; i++) {
		if (BIO_printf(bp, "%02X", x->master_key[i]) <= 0)
			goto err;
	}

	if (x->tlsext_tick_lifetime_hint > 0) {
		if (BIO_printf(bp,
		    "\n    TLS session ticket lifetime hint: %u (seconds)",
		    x->tlsext_tick_lifetime_hint) <= 0)
			goto err;
	}

	if (x->tlsext_tick != NULL) {
		if (BIO_puts(bp, "\n    TLS session ticket:\n") <= 0)
			goto err;
		if (BIO_dump_indent(bp, x->tlsext_tick, x->tlsext_ticklen,
		    4) <= 0)
			goto err;
	}

	if (x->time != 0) {
		if (BIO_printf(bp, "\n    Start Time: %lld",
		    (long long)x->time) <= 0)
			goto err;
	}

	if (x->timeout != 0) {
		if (BIO_printf(bp, "\n    Timeout   : %ld (sec)",
		    x->timeout) <= 0)
			goto err;
	}

	if (BIO_puts(bp, "\n") <= 0)
		goto err;

	if (BIO_printf(bp, "    Verify return code: %ld (%s)\n",
	    x->verify_result,
	    X509_verify_cert_error_string(x->verify_result)) <= 0)
		goto err;

	ret = 1;
 err:
	return ret;
}
LSSL_ALIAS(SSL_SESSION_print);
