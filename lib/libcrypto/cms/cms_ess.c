/* $OpenBSD: cms_ess.c,v 1.27 2025/05/10 05:54:38 tb Exp $ */
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

#include <stdlib.h>
#include <string.h>

#include <openssl/asn1.h>
#include <openssl/cms.h>
#include <openssl/evp.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "cms_local.h"
#include "err_local.h"

CMS_ReceiptRequest *
d2i_CMS_ReceiptRequest(CMS_ReceiptRequest **a, const unsigned char **in, long len)
{
	return (CMS_ReceiptRequest *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &CMS_ReceiptRequest_it);
}
LCRYPTO_ALIAS(d2i_CMS_ReceiptRequest);

int
i2d_CMS_ReceiptRequest(CMS_ReceiptRequest *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &CMS_ReceiptRequest_it);
}
LCRYPTO_ALIAS(i2d_CMS_ReceiptRequest);

CMS_ReceiptRequest *
CMS_ReceiptRequest_new(void)
{
	return (CMS_ReceiptRequest *)ASN1_item_new(&CMS_ReceiptRequest_it);
}
LCRYPTO_ALIAS(CMS_ReceiptRequest_new);

void
CMS_ReceiptRequest_free(CMS_ReceiptRequest *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &CMS_ReceiptRequest_it);
}
LCRYPTO_ALIAS(CMS_ReceiptRequest_free);

/* ESS services: for now just Signed Receipt related */

int
CMS_get1_ReceiptRequest(CMS_SignerInfo *si, CMS_ReceiptRequest **prr)
{
	ASN1_STRING *str;
	CMS_ReceiptRequest *rr = NULL;

	if (prr)
		*prr = NULL;
	str = CMS_signed_get0_data_by_OBJ(si,
	    OBJ_nid2obj(NID_id_smime_aa_receiptRequest), -3, V_ASN1_SEQUENCE);
	if (!str)
		return 0;

	rr = ASN1_item_unpack(str, &CMS_ReceiptRequest_it);
	if (!rr)
		return -1;
	if (prr)
		*prr = rr;
	else
		CMS_ReceiptRequest_free(rr);

	return 1;
}
LCRYPTO_ALIAS(CMS_get1_ReceiptRequest);

CMS_ReceiptRequest *
CMS_ReceiptRequest_create0(unsigned char *id, int idlen, int allorfirst,
    STACK_OF(GENERAL_NAMES) *receiptList, STACK_OF(GENERAL_NAMES) *receiptsTo)
{
	CMS_ReceiptRequest *rr = NULL;

	rr = CMS_ReceiptRequest_new();
	if (rr == NULL)
		goto merr;
	if (id)
		ASN1_STRING_set0(rr->signedContentIdentifier, id, idlen);
	else {
		if (!ASN1_STRING_set(rr->signedContentIdentifier, NULL, 32))
			goto merr;
		arc4random_buf(rr->signedContentIdentifier->data, 32);
	}

	sk_GENERAL_NAMES_pop_free(rr->receiptsTo, GENERAL_NAMES_free);
	rr->receiptsTo = receiptsTo;

	if (receiptList) {
		rr->receiptsFrom->type = 1;
		rr->receiptsFrom->d.receiptList = receiptList;
	} else {
		rr->receiptsFrom->type = 0;
		rr->receiptsFrom->d.allOrFirstTier = allorfirst;
	}

	return rr;

 merr:
	CMSerror(ERR_R_MALLOC_FAILURE);
	CMS_ReceiptRequest_free(rr);

	return NULL;
}
LCRYPTO_ALIAS(CMS_ReceiptRequest_create0);

int
CMS_add1_ReceiptRequest(CMS_SignerInfo *si, CMS_ReceiptRequest *rr)
{
	unsigned char *rrder = NULL;
	int rrderlen, r = 0;

	rrderlen = i2d_CMS_ReceiptRequest(rr, &rrder);
	if (rrderlen < 0)
		goto merr;

	if (!CMS_signed_add1_attr_by_NID(si, NID_id_smime_aa_receiptRequest,
	    V_ASN1_SEQUENCE, rrder, rrderlen))
		goto merr;

	r = 1;

 merr:
	if (!r)
		CMSerror(ERR_R_MALLOC_FAILURE);

	free(rrder);

	return r;
}
LCRYPTO_ALIAS(CMS_add1_ReceiptRequest);

void
CMS_ReceiptRequest_get0_values(CMS_ReceiptRequest *rr, ASN1_STRING **pcid,
    int *pallorfirst, STACK_OF(GENERAL_NAMES) **plist,
    STACK_OF(GENERAL_NAMES) **prto)
{
	if (pcid)
		*pcid = rr->signedContentIdentifier;
	if (rr->receiptsFrom->type == 0) {
		if (pallorfirst)
			*pallorfirst = (int)rr->receiptsFrom->d.allOrFirstTier;
		if (plist)
			*plist = NULL;
	} else {
		if (pallorfirst)
			*pallorfirst = -1;
		if (plist)
			*plist = rr->receiptsFrom->d.receiptList;
	}
	if (prto)
		*prto = rr->receiptsTo;
}
LCRYPTO_ALIAS(CMS_ReceiptRequest_get0_values);

/* Digest a SignerInfo structure for msgSigDigest attribute processing */

static int
cms_msgSigDigest(CMS_SignerInfo *si, unsigned char *dig, unsigned int *diglen)
{
	const EVP_MD *md;

	md = EVP_get_digestbyobj(si->digestAlgorithm->algorithm);
	if (md == NULL)
		return 0;
	if (!ASN1_item_digest(&CMS_Attributes_Verify_it, md,
	    si->signedAttrs, dig, diglen))
		return 0;

	return 1;
}

/* Add a msgSigDigest attribute to a SignerInfo */

int
cms_msgSigDigest_add1(CMS_SignerInfo *dest, CMS_SignerInfo *src)
{
	unsigned char dig[EVP_MAX_MD_SIZE];
	unsigned int diglen;

	if (!cms_msgSigDigest(src, dig, &diglen)) {
		CMSerror(CMS_R_MSGSIGDIGEST_ERROR);
		return 0;
	}
	if (!CMS_signed_add1_attr_by_NID(dest, NID_id_smime_aa_msgSigDigest,
	    V_ASN1_OCTET_STRING, dig, diglen)) {
		CMSerror(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	return 1;
}

/* Verify signed receipt after it has already passed normal CMS verify */

int
cms_Receipt_verify(CMS_ContentInfo *cms, CMS_ContentInfo *req_cms)
{
	int r = 0, i;
	CMS_ReceiptRequest *rr = NULL;
	CMS_Receipt *rct = NULL;
	STACK_OF(CMS_SignerInfo) *sis, *osis;
	CMS_SignerInfo *si, *osi = NULL;
	ASN1_OCTET_STRING *msig, **pcont;
	ASN1_OBJECT *octype;
	unsigned char dig[EVP_MAX_MD_SIZE];
	unsigned int diglen;

	/* Get SignerInfos, also checks SignedData content type */
	osis = CMS_get0_SignerInfos(req_cms);
	sis = CMS_get0_SignerInfos(cms);
	if (!osis || !sis)
		goto err;

	if (sk_CMS_SignerInfo_num(sis) != 1) {
		CMSerror(CMS_R_NEED_ONE_SIGNER);
		goto err;
	}

	/* Check receipt content type */
	if (OBJ_obj2nid(CMS_get0_eContentType(cms)) != NID_id_smime_ct_receipt) {
		CMSerror(CMS_R_NOT_A_SIGNED_RECEIPT);
		goto err;
	}

	/* Extract and decode receipt content */
	pcont = CMS_get0_content(cms);
	if (!pcont || !*pcont) {
		CMSerror(CMS_R_NO_CONTENT);
		goto err;
	}

	rct = ASN1_item_unpack(*pcont, &CMS_Receipt_it);

	if (!rct) {
		CMSerror(CMS_R_RECEIPT_DECODE_ERROR);
		goto err;
	}

	/* Locate original request */

	for (i = 0; i < sk_CMS_SignerInfo_num(osis); i++) {
		osi = sk_CMS_SignerInfo_value(osis, i);
		if (!ASN1_STRING_cmp(osi->signature, rct->originatorSignatureValue))
			break;
	}

	if (i == sk_CMS_SignerInfo_num(osis)) {
		CMSerror(CMS_R_NO_MATCHING_SIGNATURE);
		goto err;
	}

	si = sk_CMS_SignerInfo_value(sis, 0);

	/* Get msgSigDigest value and compare */

	msig = CMS_signed_get0_data_by_OBJ(si,
	    OBJ_nid2obj(NID_id_smime_aa_msgSigDigest), -3, V_ASN1_OCTET_STRING);

	if (!msig) {
		CMSerror(CMS_R_NO_MSGSIGDIGEST);
		goto err;
	}

	if (!cms_msgSigDigest(osi, dig, &diglen)) {
		CMSerror(CMS_R_MSGSIGDIGEST_ERROR);
		goto err;
	}

	if (diglen != (unsigned int)msig->length) {
		CMSerror(CMS_R_MSGSIGDIGEST_WRONG_LENGTH);
		goto err;
	}

	if (memcmp(dig, msig->data, diglen)) {
		CMSerror(CMS_R_MSGSIGDIGEST_VERIFICATION_FAILURE);
		goto err;
	}

	/* Compare content types */

	octype = CMS_signed_get0_data_by_OBJ(osi,
	    OBJ_nid2obj(NID_pkcs9_contentType), -3, V_ASN1_OBJECT);
	if (!octype) {
		CMSerror(CMS_R_NO_CONTENT_TYPE);
		goto err;
	}

	/* Compare details in receipt request */

	if (OBJ_cmp(octype, rct->contentType)) {
		CMSerror(CMS_R_CONTENT_TYPE_MISMATCH);
		goto err;
	}

	/* Get original receipt request details */

	if (CMS_get1_ReceiptRequest(osi, &rr) <= 0) {
		CMSerror(CMS_R_NO_RECEIPT_REQUEST);
		goto err;
	}

	if (ASN1_STRING_cmp(rr->signedContentIdentifier,
	    rct->signedContentIdentifier)) {
		CMSerror(CMS_R_CONTENTIDENTIFIER_MISMATCH);
		goto err;
	}

	r = 1;

 err:
	CMS_ReceiptRequest_free(rr);
	ASN1_item_free((ASN1_VALUE *)rct, &CMS_Receipt_it);
	return r;
}

/*
 * Encode a Receipt into an OCTET STRING read for including into content of a
 * SignedData ContentInfo.
 */

ASN1_OCTET_STRING *
cms_encode_Receipt(CMS_SignerInfo *si)
{
	CMS_Receipt rct;
	CMS_ReceiptRequest *rr = NULL;
	ASN1_OBJECT *ctype;
	ASN1_OCTET_STRING *os = NULL;

	/* Get original receipt request */

	/* Get original receipt request details */

	if (CMS_get1_ReceiptRequest(si, &rr) <= 0) {
		CMSerror(CMS_R_NO_RECEIPT_REQUEST);
		goto err;
	}

	/* Get original content type */

	ctype = CMS_signed_get0_data_by_OBJ(si,
	    OBJ_nid2obj(NID_pkcs9_contentType), -3, V_ASN1_OBJECT);
	if (!ctype) {
		CMSerror(CMS_R_NO_CONTENT_TYPE);
		goto err;
	}

	rct.version = 1;
	rct.contentType = ctype;
	rct.signedContentIdentifier = rr->signedContentIdentifier;
	rct.originatorSignatureValue = si->signature;

	os = ASN1_item_pack(&rct, &CMS_Receipt_it, NULL);

 err:
	CMS_ReceiptRequest_free(rr);
	return os;
}
