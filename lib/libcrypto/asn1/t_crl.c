/* $OpenBSD: t_crl.c,v 1.27 2025/05/10 05:54:38 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 1999.
 */
/* ====================================================================
 * Copyright (c) 1999 The OpenSSL Project.  All rights reserved.
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
 *    for use in the OpenSSL Toolkit. (http://www.OpenSSL.org/)"
 *
 * 4. The names "OpenSSL Toolkit" and "OpenSSL Project" must not be used to
 *    endorse or promote products derived from this software without
 *    prior written permission. For written permission, please contact
 *    licensing@OpenSSL.org.
 *
 * 5. Products derived from this software may not be called "OpenSSL"
 *    nor may "OpenSSL" appear in their names without prior written
 *    permission of the OpenSSL Project.
 *
 * 6. Redistributions of any form whatsoever must retain the following
 *    acknowledgment:
 *    "This product includes software developed by the OpenSSL Project
 *    for use in the OpenSSL Toolkit (http://www.OpenSSL.org/)"
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

#include <stdio.h>
#include <limits.h>

#include <openssl/bn.h>
#include <openssl/buffer.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "err_local.h"
#include "x509_local.h"

int
X509_CRL_print_fp(FILE *fp, X509_CRL *x)
{
	BIO *b;
	int ret;

	if ((b = BIO_new(BIO_s_file())) == NULL) {
		X509error(ERR_R_BUF_LIB);
		return (0);
	}
	BIO_set_fp(b, fp, BIO_NOCLOSE);
	ret = X509_CRL_print(b, x);
	BIO_free(b);
	return (ret);
}
LCRYPTO_ALIAS(X509_CRL_print_fp);

int
X509_CRL_print(BIO *out, X509_CRL *x)
{
	STACK_OF(X509_REVOKED) *rev;
	X509_REVOKED *r;
	long l;
	int i;
	char *p;

	BIO_printf(out, "Certificate Revocation List (CRL):\n");
	l = X509_CRL_get_version(x);
	if (l >= 0 && l <= 1) {
		if (BIO_printf(out, "%8sVersion: %lu (0x%lx)\n",
		    "", l + 1, l) <= 0)
			goto err;
	} else {
		if (BIO_printf(out, "%8sVersion: unknown (%ld)\n",
		    "", l) <= 0)
			goto err;
	}
	if (X509_signature_print(out, x->sig_alg, NULL) == 0)
		goto err;
	p = X509_NAME_oneline(X509_CRL_get_issuer(x), NULL, 0);
	if (p == NULL)
		goto err;
	BIO_printf(out, "%8sIssuer: %s\n", "", p);
	free(p);
	BIO_printf(out, "%8sLast Update: ", "");
	ASN1_TIME_print(out, X509_CRL_get_lastUpdate(x));
	BIO_printf(out, "\n%8sNext Update: ", "");
	if (X509_CRL_get_nextUpdate(x))
		ASN1_TIME_print(out, X509_CRL_get_nextUpdate(x));
	else
		BIO_printf(out, "NONE");
	BIO_printf(out, "\n");

	X509V3_extensions_print(out, "CRL extensions",
	    x->crl->extensions, 0, 8);

	rev = X509_CRL_get_REVOKED(x);

	if (sk_X509_REVOKED_num(rev) > 0)
		BIO_printf(out, "Revoked Certificates:\n");
	else
		BIO_printf(out, "No Revoked Certificates.\n");

	for (i = 0; i < sk_X509_REVOKED_num(rev); i++) {
		r = sk_X509_REVOKED_value(rev, i);
		BIO_printf(out, "    Serial Number: ");
		i2a_ASN1_INTEGER(out, r->serialNumber);
		BIO_printf(out, "\n        Revocation Date: ");
		ASN1_TIME_print(out, r->revocationDate);
		BIO_printf(out, "\n");
		X509V3_extensions_print(out, "CRL entry extensions",
		    r->extensions, 0, 8);
	}
	if (X509_signature_print(out, x->sig_alg, x->signature) == 0)
		goto err;

	return 1;

 err:
	return 0;
}
LCRYPTO_ALIAS(X509_CRL_print);
