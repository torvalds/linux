/* $OpenBSD: cms_att.c,v 1.13 2024/08/27 01:19:27 tb Exp $ */
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

#include <openssl/asn1.h>
#include <openssl/cms.h>
#include <openssl/x509.h>

#include "cms_local.h"
#include "x509_local.h"

/* CMS SignedData Attribute utilities */

int
CMS_signed_get_attr_count(const CMS_SignerInfo *si)
{
	return sk_X509_ATTRIBUTE_num(si->signedAttrs);
}
LCRYPTO_ALIAS(CMS_signed_get_attr_count);

int
CMS_signed_get_attr_by_NID(const CMS_SignerInfo *si, int nid, int lastpos)
{
	return X509at_get_attr_by_NID(si->signedAttrs, nid, lastpos);
}
LCRYPTO_ALIAS(CMS_signed_get_attr_by_NID);

int
CMS_signed_get_attr_by_OBJ(const CMS_SignerInfo *si, const ASN1_OBJECT *obj,
    int lastpos)
{
	return X509at_get_attr_by_OBJ(si->signedAttrs, obj, lastpos);
}
LCRYPTO_ALIAS(CMS_signed_get_attr_by_OBJ);

X509_ATTRIBUTE *
CMS_signed_get_attr(const CMS_SignerInfo *si, int loc)
{
	return sk_X509_ATTRIBUTE_value(si->signedAttrs, loc);
}
LCRYPTO_ALIAS(CMS_signed_get_attr);

X509_ATTRIBUTE *
CMS_signed_delete_attr(CMS_SignerInfo *si, int loc)
{
	return sk_X509_ATTRIBUTE_delete(si->signedAttrs, loc);
}
LCRYPTO_ALIAS(CMS_signed_delete_attr);

int
CMS_signed_add1_attr(CMS_SignerInfo *si, X509_ATTRIBUTE *attr)
{
	if (X509at_add1_attr(&si->signedAttrs, attr))
		return 1;
	return 0;
}
LCRYPTO_ALIAS(CMS_signed_add1_attr);

int
CMS_signed_add1_attr_by_OBJ(CMS_SignerInfo *si, const ASN1_OBJECT *obj, int type,
    const void *bytes, int len)
{
	if (X509at_add1_attr_by_OBJ(&si->signedAttrs, obj, type, bytes, len))
		return 1;
	return 0;
}
LCRYPTO_ALIAS(CMS_signed_add1_attr_by_OBJ);

int
CMS_signed_add1_attr_by_NID(CMS_SignerInfo *si, int nid, int type,
    const void *bytes, int len)
{
	if (X509at_add1_attr_by_NID(&si->signedAttrs, nid, type, bytes, len))
		return 1;
	return 0;
}
LCRYPTO_ALIAS(CMS_signed_add1_attr_by_NID);

int
CMS_signed_add1_attr_by_txt(CMS_SignerInfo *si, const char *attrname, int type,
    const void *bytes, int len)
{
	if (X509at_add1_attr_by_txt(&si->signedAttrs, attrname, type, bytes, len))
		return 1;
	return 0;
}
LCRYPTO_ALIAS(CMS_signed_add1_attr_by_txt);

void *
CMS_signed_get0_data_by_OBJ(CMS_SignerInfo *si, const ASN1_OBJECT *oid,
    int lastpos, int type)
{
	return X509at_get0_data_by_OBJ(si->signedAttrs, oid, lastpos, type);
}
LCRYPTO_ALIAS(CMS_signed_get0_data_by_OBJ);

int
CMS_unsigned_get_attr_count(const CMS_SignerInfo *si)
{
	return sk_X509_ATTRIBUTE_num(si->unsignedAttrs);
}
LCRYPTO_ALIAS(CMS_unsigned_get_attr_count);

int
CMS_unsigned_get_attr_by_NID(const CMS_SignerInfo *si, int nid, int lastpos)
{
	return X509at_get_attr_by_NID(si->unsignedAttrs, nid, lastpos);
}
LCRYPTO_ALIAS(CMS_unsigned_get_attr_by_NID);

int
CMS_unsigned_get_attr_by_OBJ(const CMS_SignerInfo *si, const ASN1_OBJECT *obj,
    int lastpos)
{
	return X509at_get_attr_by_OBJ(si->unsignedAttrs, obj, lastpos);
}
LCRYPTO_ALIAS(CMS_unsigned_get_attr_by_OBJ);

X509_ATTRIBUTE *
CMS_unsigned_get_attr(const CMS_SignerInfo *si, int loc)
{
	return sk_X509_ATTRIBUTE_value(si->unsignedAttrs, loc);
}
LCRYPTO_ALIAS(CMS_unsigned_get_attr);

X509_ATTRIBUTE *
CMS_unsigned_delete_attr(CMS_SignerInfo *si, int loc)
{
	return sk_X509_ATTRIBUTE_delete(si->unsignedAttrs, loc);
}
LCRYPTO_ALIAS(CMS_unsigned_delete_attr);

int
CMS_unsigned_add1_attr(CMS_SignerInfo *si, X509_ATTRIBUTE *attr)
{
	if (X509at_add1_attr(&si->unsignedAttrs, attr))
		return 1;
	return 0;
}
LCRYPTO_ALIAS(CMS_unsigned_add1_attr);

int
CMS_unsigned_add1_attr_by_OBJ(CMS_SignerInfo *si, const ASN1_OBJECT *obj,
    int type, const void *bytes, int len)
{
	if (X509at_add1_attr_by_OBJ(&si->unsignedAttrs, obj, type, bytes, len))
		return 1;
	return 0;
}
LCRYPTO_ALIAS(CMS_unsigned_add1_attr_by_OBJ);

int
CMS_unsigned_add1_attr_by_NID(CMS_SignerInfo *si, int nid, int type,
    const void *bytes, int len)
{
	if (X509at_add1_attr_by_NID(&si->unsignedAttrs, nid, type, bytes, len))
		return 1;
	return 0;
}
LCRYPTO_ALIAS(CMS_unsigned_add1_attr_by_NID);

int
CMS_unsigned_add1_attr_by_txt(CMS_SignerInfo *si, const char *attrname,
    int type, const void *bytes, int len)
{
	if (X509at_add1_attr_by_txt(&si->unsignedAttrs, attrname, type,
	    bytes, len))
		return 1;
	return 0;
}
LCRYPTO_ALIAS(CMS_unsigned_add1_attr_by_txt);

void *
CMS_unsigned_get0_data_by_OBJ(CMS_SignerInfo *si, ASN1_OBJECT *oid, int lastpos,
    int type)
{
	return X509at_get0_data_by_OBJ(si->unsignedAttrs, oid, lastpos, type);
}
LCRYPTO_ALIAS(CMS_unsigned_get0_data_by_OBJ);

/* Specific attribute cases */
