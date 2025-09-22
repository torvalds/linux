/* $OpenBSD: cms_io.c,v 1.22 2025/05/10 05:54:38 tb Exp $ */
/*
 * Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project.
 */
/* ====================================================================
 * Copyright (c) 2008 The OpenSSL Project.  All rights reserved.
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
 */

#include <openssl/asn1t.h>
#include <openssl/cms.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#include "asn1_local.h"
#include "cms_local.h"
#include "err_local.h"

int
CMS_stream(unsigned char ***boundary, CMS_ContentInfo *cms)
{
	ASN1_OCTET_STRING **pos;

	if ((pos = CMS_get0_content(cms)) == NULL)
		return 0;

	if (*pos == NULL)
		*pos = ASN1_OCTET_STRING_new();
	if (*pos == NULL) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	(*pos)->flags |= ASN1_STRING_FLAG_NDEF;
	(*pos)->flags &= ~ASN1_STRING_FLAG_CONT;
	*boundary = &(*pos)->data;

	return 1;
}
LCRYPTO_ALIAS(CMS_stream);

CMS_ContentInfo *
d2i_CMS_bio(BIO *bp, CMS_ContentInfo **cms)
{
	return ASN1_item_d2i_bio(&CMS_ContentInfo_it, bp, cms);
}
LCRYPTO_ALIAS(d2i_CMS_bio);

int
i2d_CMS_bio(BIO *bp, CMS_ContentInfo *cms)
{
	return ASN1_item_i2d_bio(&CMS_ContentInfo_it, bp, cms);
}
LCRYPTO_ALIAS(i2d_CMS_bio);


CMS_ContentInfo *
PEM_read_bio_CMS(BIO *bp, CMS_ContentInfo **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read_bio((d2i_of_void *)d2i_CMS_ContentInfo,
	    PEM_STRING_CMS, bp, (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_bio_CMS);

CMS_ContentInfo *
PEM_read_CMS(FILE *fp, CMS_ContentInfo **x, pem_password_cb *cb, void *u)
{
	return PEM_ASN1_read((d2i_of_void *)d2i_CMS_ContentInfo,
	    PEM_STRING_CMS, fp, (void **)x, cb, u);
}
LCRYPTO_ALIAS(PEM_read_CMS);

int
PEM_write_bio_CMS(BIO *bp, const CMS_ContentInfo *x)
{
	return PEM_ASN1_write_bio((i2d_of_void *)i2d_CMS_ContentInfo,
	    PEM_STRING_CMS, bp, (void *)x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_bio_CMS);

int
PEM_write_CMS(FILE *fp, const CMS_ContentInfo *x)
{
	return PEM_ASN1_write((i2d_of_void *)i2d_CMS_ContentInfo,
	    PEM_STRING_CMS, fp, (void *)x, NULL, NULL, 0, NULL, NULL);
}
LCRYPTO_ALIAS(PEM_write_CMS);

BIO *
BIO_new_CMS(BIO *out, CMS_ContentInfo *cms)
{
	return BIO_new_NDEF(out, (ASN1_VALUE *)cms, &CMS_ContentInfo_it);
}
LCRYPTO_ALIAS(BIO_new_CMS);

/* CMS wrappers round generalised stream and MIME routines */

int
i2d_CMS_bio_stream(BIO *out, CMS_ContentInfo *cms, BIO *in, int flags)
{
	return i2d_ASN1_bio_stream(out, (ASN1_VALUE *)cms, in, flags,
	    &CMS_ContentInfo_it);
}
LCRYPTO_ALIAS(i2d_CMS_bio_stream);

int
PEM_write_bio_CMS_stream(BIO *out, CMS_ContentInfo *cms, BIO *in, int flags)
{
	return PEM_write_bio_ASN1_stream(out, (ASN1_VALUE *)cms, in, flags,
	    "CMS", &CMS_ContentInfo_it);
}
LCRYPTO_ALIAS(PEM_write_bio_CMS_stream);

int
SMIME_write_CMS(BIO *bio, CMS_ContentInfo *cms, BIO *data, int flags)
{
	STACK_OF(X509_ALGOR) *mdalgs = NULL;
	int ctype_nid = OBJ_obj2nid(cms->contentType);
	int econt_nid = OBJ_obj2nid(CMS_get0_eContentType(cms));

	if (ctype_nid == NID_pkcs7_signed)
		mdalgs = cms->d.signedData->digestAlgorithms;

	return SMIME_write_ASN1(bio, (ASN1_VALUE *)cms, data, flags, ctype_nid,
	    econt_nid, mdalgs, &CMS_ContentInfo_it);
}
LCRYPTO_ALIAS(SMIME_write_CMS);

CMS_ContentInfo *
SMIME_read_CMS(BIO *bio, BIO **bcont)
{
	return (CMS_ContentInfo *)SMIME_read_ASN1(bio, bcont,
	    &CMS_ContentInfo_it);
}
LCRYPTO_ALIAS(SMIME_read_CMS);
