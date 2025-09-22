/* $OpenBSD: pk7_mime.c,v 1.20 2024/01/25 13:44:08 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project.
 */
/* ====================================================================
 * Copyright (c) 1999-2005 The OpenSSL Project.  All rights reserved.
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
 */

#include <ctype.h>
#include <stdio.h>

#include <openssl/asn1.h>
#include <openssl/x509.h>

#include "asn1_local.h"

/* PKCS#7 wrappers round generalised stream and MIME routines */
BIO *
BIO_new_PKCS7(BIO *out, PKCS7 *p7)
{
	return BIO_new_NDEF(out, (ASN1_VALUE *)p7, &PKCS7_it);
}
LCRYPTO_ALIAS(BIO_new_PKCS7);

int
i2d_PKCS7_bio_stream(BIO *out, PKCS7 *p7, BIO *in, int flags)
{
	return i2d_ASN1_bio_stream(out, (ASN1_VALUE *)p7, in, flags, &PKCS7_it);
}
LCRYPTO_ALIAS(i2d_PKCS7_bio_stream);

int
PEM_write_bio_PKCS7_stream(BIO *out, PKCS7 *p7, BIO *in, int flags)
{
	return PEM_write_bio_ASN1_stream(out, (ASN1_VALUE *) p7, in, flags,
	    "PKCS7", &PKCS7_it);
}
LCRYPTO_ALIAS(PEM_write_bio_PKCS7_stream);

int
SMIME_write_PKCS7(BIO *bio, PKCS7 *p7, BIO *data, int flags)
{
	STACK_OF(X509_ALGOR) *mdalgs = NULL;
	int ctype_nid;

	if ((ctype_nid = OBJ_obj2nid(p7->type)) == NID_pkcs7_signed) {
		if (p7->d.sign == NULL)
			return 0;
		mdalgs = p7->d.sign->md_algs;
	}

	flags ^= SMIME_OLDMIME;

	return SMIME_write_ASN1(bio, (ASN1_VALUE *)p7, data, flags,
	    ctype_nid, NID_undef, mdalgs, &PKCS7_it);
}
LCRYPTO_ALIAS(SMIME_write_PKCS7);

PKCS7 *
SMIME_read_PKCS7(BIO *bio, BIO **bcont)
{
	return (PKCS7 *)SMIME_read_ASN1(bio, bcont, &PKCS7_it);
}
LCRYPTO_ALIAS(SMIME_read_PKCS7);
