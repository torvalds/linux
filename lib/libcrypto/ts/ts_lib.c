/* $OpenBSD: ts_lib.c,v 1.15 2025/01/07 14:22:19 tb Exp $ */
/* Written by Zoltan Glozik (zglozik@stones.com) for the OpenSSL
 * project 2002.
 */
/* ====================================================================
 * Copyright (c) 2006 The OpenSSL Project.  All rights reserved.
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
#include <string.h>

#include <openssl/bn.h>
#include <openssl/objects.h>
#include <openssl/ts.h>
#include <openssl/x509v3.h>

#include "bn_local.h"
#include "x509_local.h"

/* Local function declarations. */

/* Function definitions. */

int
TS_ASN1_INTEGER_print_bio(BIO *bio, const ASN1_INTEGER *num)
{
	BIGNUM *bn = NULL;
	char *hex = NULL;
	int ret = 0;

	/* XXX - OpenSSL decided to return -1 here for some stupid reason. */
	if ((bn = ASN1_INTEGER_to_BN(num, NULL)) == NULL)
		goto err;
	if ((hex = BN_bn2hex(bn)) == NULL)
		goto err;
	if (BIO_printf(bio, "0x%s", hex) <= 0)
		goto err;

	ret = 1;

 err:
	BN_free(bn);
	free(hex);

	return ret;
}
LCRYPTO_ALIAS(TS_ASN1_INTEGER_print_bio);

int
TS_OBJ_print_bio(BIO *bio, const ASN1_OBJECT *obj)
{
	char obj_txt[128];

	int len = OBJ_obj2txt(obj_txt, sizeof(obj_txt), obj, 0);
	if (len >= sizeof(obj_txt))
		len = sizeof(obj_txt) - 1;
	BIO_write(bio, obj_txt, len);
	BIO_write(bio, "\n", 1);
	return 1;
}
LCRYPTO_ALIAS(TS_OBJ_print_bio);

int
TS_ext_print_bio(BIO *bio, const STACK_OF(X509_EXTENSION) *extensions)
{
	int i, critical, n;
	X509_EXTENSION *ex;
	ASN1_OBJECT *obj;

	BIO_printf(bio, "Extensions:\n");
	n = X509v3_get_ext_count(extensions);
	for (i = 0; i < n; i++) {
		ex = X509v3_get_ext(extensions, i);
		obj = X509_EXTENSION_get_object(ex);
		i2a_ASN1_OBJECT(bio, obj);
		critical = X509_EXTENSION_get_critical(ex);
		BIO_printf(bio, ": %s\n", critical ? "critical" : "");
		if (!X509V3_EXT_print(bio, ex, 0, 4)) {
			BIO_printf(bio, "%4s", "");
			ASN1_STRING_print(bio, ex->value);
		}
		BIO_write(bio, "\n", 1);
	}

	return 1;
}
LCRYPTO_ALIAS(TS_ext_print_bio);

int
TS_X509_ALGOR_print_bio(BIO *bio, const X509_ALGOR *alg)
{
	int i = OBJ_obj2nid(alg->algorithm);

	return BIO_printf(bio, "Hash Algorithm: %s\n",
	    (i == NID_undef) ? "UNKNOWN" : OBJ_nid2ln(i));
}
LCRYPTO_ALIAS(TS_X509_ALGOR_print_bio);

int
TS_MSG_IMPRINT_print_bio(BIO *bio, TS_MSG_IMPRINT *a)
{
	ASN1_OCTET_STRING *msg;

	TS_X509_ALGOR_print_bio(bio, TS_MSG_IMPRINT_get_algo(a));

	BIO_printf(bio, "Message data:\n");
	msg = TS_MSG_IMPRINT_get_msg(a);
	BIO_dump_indent(bio, (const char *)ASN1_STRING_data(msg),
	    ASN1_STRING_length(msg), 4);

	return 1;
}
LCRYPTO_ALIAS(TS_MSG_IMPRINT_print_bio);
