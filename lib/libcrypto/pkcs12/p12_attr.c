/* $OpenBSD: p12_attr.c,v 1.21 2024/03/24 06:48:03 tb Exp $ */
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

#include <openssl/pkcs12.h>

#include "pkcs12_local.h"
#include "x509_local.h"

/* Add a local keyid to a safebag */

int
PKCS12_add_localkeyid(PKCS12_SAFEBAG *bag, unsigned char *name, int namelen)
{
	if (X509at_add1_attr_by_NID(&bag->attrib, NID_localKeyID,
	    V_ASN1_OCTET_STRING, name, namelen))
		return 1;
	else
		return 0;
}

/* Add key usage to PKCS#8 structure */

int
PKCS8_add_keyusage(PKCS8_PRIV_KEY_INFO *p8, int usage)
{
	unsigned char us_val = (unsigned char)usage;

	return PKCS8_pkey_add1_attr_by_NID(p8, NID_key_usage, V_ASN1_BIT_STRING,
	    &us_val, 1);
}
LCRYPTO_ALIAS(PKCS8_add_keyusage);

/* Add a friendlyname to a safebag */

int
PKCS12_add_friendlyname_asc(PKCS12_SAFEBAG *bag, const char *name, int namelen)
{
	if (X509at_add1_attr_by_NID(&bag->attrib, NID_friendlyName,
	    MBSTRING_ASC, (unsigned char *)name, namelen))
		return 1;
	else
		return 0;
}

int
PKCS12_add_friendlyname_uni(PKCS12_SAFEBAG *bag, const unsigned char *name,
    int namelen)
{
	if (X509at_add1_attr_by_NID(&bag->attrib, NID_friendlyName,
	    MBSTRING_BMP, name, namelen))
		return 1;
	else
		return 0;
}

int
PKCS12_add_CSPName_asc(PKCS12_SAFEBAG *bag, const char *name, int namelen)
{
	if (X509at_add1_attr_by_NID(&bag->attrib, NID_ms_csp_name,
	    MBSTRING_ASC, (unsigned char *)name, namelen))
		return 1;
	else
		return 0;
}

ASN1_TYPE *
PKCS12_get_attr_gen(const STACK_OF(X509_ATTRIBUTE) *attrs, int attr_nid)
{
	X509_ATTRIBUTE *attrib;
	int i;

	if (!attrs)
		return NULL;
	for (i = 0; i < sk_X509_ATTRIBUTE_num(attrs); i++) {
		attrib = sk_X509_ATTRIBUTE_value(attrs, i);
		if (OBJ_obj2nid(attrib->object) == attr_nid)
			return sk_ASN1_TYPE_value(attrib->set, 0);
	}
	return NULL;
}

char *
PKCS12_get_friendlyname(PKCS12_SAFEBAG *bag)
{
	const ASN1_TYPE *atype;

	if (!(atype = PKCS12_SAFEBAG_get0_attr(bag, NID_friendlyName)))
		return NULL;
	if (atype->type != V_ASN1_BMPSTRING)
		return NULL;
	return OPENSSL_uni2asc(atype->value.bmpstring->data,
	    atype->value.bmpstring->length);
}
LCRYPTO_ALIAS(PKCS12_get_friendlyname);

const STACK_OF(X509_ATTRIBUTE) *
PKCS12_SAFEBAG_get0_attrs(const PKCS12_SAFEBAG *bag)
{
	return bag->attrib;
}
LCRYPTO_ALIAS(PKCS12_SAFEBAG_get0_attrs);
