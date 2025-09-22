/* $OpenBSD: cms_dd.c,v 1.18 2025/05/10 05:54:38 tb Exp $ */
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

#include <string.h>

#include <openssl/asn1.h>
#include <openssl/cms.h>
#include <openssl/evp.h>
#include <openssl/objects.h>

#include "cms_local.h"
#include "err_local.h"
#include "x509_local.h"

/* CMS DigestedData Utilities */

CMS_ContentInfo *
cms_DigestedData_create(const EVP_MD *md)
{
	CMS_ContentInfo *cms;
	CMS_DigestedData *dd;

	cms = CMS_ContentInfo_new();
	if (cms == NULL)
		return NULL;

	dd = (CMS_DigestedData *)ASN1_item_new(&CMS_DigestedData_it);

	if (dd == NULL)
		goto err;

	cms->contentType = OBJ_nid2obj(NID_pkcs7_digest);
	cms->d.digestedData = dd;

	dd->version = 0;
	dd->encapContentInfo->eContentType = OBJ_nid2obj(NID_pkcs7_data);

	if (!X509_ALGOR_set_evp_md(dd->digestAlgorithm, md))
		goto err;

	return cms;

 err:
	CMS_ContentInfo_free(cms);

	return NULL;
}

BIO *
cms_DigestedData_init_bio(CMS_ContentInfo *cms)
{
	CMS_DigestedData *dd;

	dd = cms->d.digestedData;

	return cms_DigestAlgorithm_init_bio(dd->digestAlgorithm);
}

int
cms_DigestedData_do_final(CMS_ContentInfo *cms, BIO *chain, int verify)
{
	EVP_MD_CTX *mctx = EVP_MD_CTX_new();
	unsigned char md[EVP_MAX_MD_SIZE];
	unsigned int mdlen;
	int r = 0;
	CMS_DigestedData *dd;

	if (mctx == NULL) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		goto err;
	}

	dd = cms->d.digestedData;

	if (!cms_DigestAlgorithm_find_ctx(mctx, chain, dd->digestAlgorithm))
		goto err;

	if (EVP_DigestFinal_ex(mctx, md, &mdlen) <= 0)
		goto err;

	if (verify) {
		if (mdlen != (unsigned int)dd->digest->length) {
			CMSerror(CMS_R_MESSAGEDIGEST_WRONG_LENGTH);
			goto err;
		}

		if (memcmp(md, dd->digest->data, mdlen))
			CMSerror(CMS_R_VERIFICATION_FAILURE);
		else
			r = 1;
	} else {
		if (!ASN1_STRING_set(dd->digest, md, mdlen))
			goto err;
		r = 1;
	}

 err:
	EVP_MD_CTX_free(mctx);

	return r;
}
