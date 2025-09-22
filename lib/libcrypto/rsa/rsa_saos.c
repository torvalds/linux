/* $OpenBSD: rsa_saos.c,v 1.26 2025/05/10 05:54:38 tb Exp $ */
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
#include <string.h>

#include <openssl/bn.h>
#include <openssl/objects.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>

#include "err_local.h"

int
RSA_sign_ASN1_OCTET_STRING(int type, const unsigned char *m, unsigned int m_len,
    unsigned char *sigret, unsigned int *siglen, RSA *rsa)
{
	ASN1_OCTET_STRING sig;
	int i, j, ret = 1;
	unsigned char *p, *s;

	sig.type = V_ASN1_OCTET_STRING;
	sig.length = m_len;
	sig.data = (unsigned char *)m;

	i = i2d_ASN1_OCTET_STRING(&sig, NULL);
	j = RSA_size(rsa);
	if (i > (j - RSA_PKCS1_PADDING_SIZE)) {
		RSAerror(RSA_R_DIGEST_TOO_BIG_FOR_RSA_KEY);
		return 0;
	}
	s = malloc(j + 1);
	if (s == NULL) {
		RSAerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	p = s;
	i2d_ASN1_OCTET_STRING(&sig, &p);
	i = RSA_private_encrypt(i, s, sigret, rsa, RSA_PKCS1_PADDING);
	if (i <= 0)
		ret = 0;
	else
		*siglen = i;

	freezero(s, (unsigned int)j + 1);
	return ret;
}
LCRYPTO_ALIAS(RSA_sign_ASN1_OCTET_STRING);

int
RSA_verify_ASN1_OCTET_STRING(int dtype, const unsigned char *m,
    unsigned int m_len, unsigned char *sigbuf, unsigned int siglen, RSA *rsa)
{
	int i, ret = 0;
	unsigned char *s;
	const unsigned char *p;
	ASN1_OCTET_STRING *sig = NULL;

	if (siglen != (unsigned int)RSA_size(rsa)) {
		RSAerror(RSA_R_WRONG_SIGNATURE_LENGTH);
		return 0;
	}

	s = malloc(siglen);
	if (s == NULL) {
		RSAerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}
	i = RSA_public_decrypt((int)siglen, sigbuf, s, rsa, RSA_PKCS1_PADDING);

	if (i <= 0)
		goto err;

	p = s;
	sig = d2i_ASN1_OCTET_STRING(NULL, &p, (long)i);
	if (sig == NULL)
		goto err;

	if ((unsigned int)sig->length != m_len ||
	    timingsafe_bcmp(m, sig->data, m_len) != 0) {
		RSAerror(RSA_R_BAD_SIGNATURE);
	} else
		ret = 1;
err:
	ASN1_OCTET_STRING_free(sig);
	freezero(s, (unsigned int)siglen);
	return ret;
}
LCRYPTO_ALIAS(RSA_verify_ASN1_OCTET_STRING);
