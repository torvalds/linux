/* $OpenBSD: p12_asn.c,v 1.16 2024/07/09 06:13:22 beck Exp $ */
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

#include <openssl/asn1t.h>
#include <openssl/pkcs12.h>

#include "pkcs12_local.h"

/* PKCS#12 ASN1 module */

static const ASN1_TEMPLATE PKCS12_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS12, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS12, authsafes),
		.field_name = "authsafes",
		.item = &PKCS7_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(PKCS12, mac),
		.field_name = "mac",
		.item = &PKCS12_MAC_DATA_it,
	},
};

const ASN1_ITEM PKCS12_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS12_seq_tt,
	.tcount = sizeof(PKCS12_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(PKCS12),
	.sname = "PKCS12",
};
LCRYPTO_ALIAS(PKCS12_it);


PKCS12 *
d2i_PKCS12(PKCS12 **a, const unsigned char **in, long len)
{
	return (PKCS12 *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS12_it);
}
LCRYPTO_ALIAS(d2i_PKCS12);

int
i2d_PKCS12(PKCS12 *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS12_it);
}
LCRYPTO_ALIAS(i2d_PKCS12);

PKCS12 *
PKCS12_new(void)
{
	return (PKCS12 *)ASN1_item_new(&PKCS12_it);
}
LCRYPTO_ALIAS(PKCS12_new);

void
PKCS12_free(PKCS12 *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS12_it);
}
LCRYPTO_ALIAS(PKCS12_free);

static const ASN1_TEMPLATE PKCS12_MAC_DATA_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS12_MAC_DATA, dinfo),
		.field_name = "dinfo",
		.item = &X509_SIG_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS12_MAC_DATA, salt),
		.field_name = "salt",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(PKCS12_MAC_DATA, iter),
		.field_name = "iter",
		.item = &ASN1_INTEGER_it,
	},
};

const ASN1_ITEM PKCS12_MAC_DATA_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS12_MAC_DATA_seq_tt,
	.tcount = sizeof(PKCS12_MAC_DATA_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(PKCS12_MAC_DATA),
	.sname = "PKCS12_MAC_DATA",
};


PKCS12_MAC_DATA *
d2i_PKCS12_MAC_DATA(PKCS12_MAC_DATA **a, const unsigned char **in, long len)
{
	return (PKCS12_MAC_DATA *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS12_MAC_DATA_it);
}

int
i2d_PKCS12_MAC_DATA(PKCS12_MAC_DATA *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS12_MAC_DATA_it);
}

PKCS12_MAC_DATA *
PKCS12_MAC_DATA_new(void)
{
	return (PKCS12_MAC_DATA *)ASN1_item_new(&PKCS12_MAC_DATA_it);
}

void
PKCS12_MAC_DATA_free(PKCS12_MAC_DATA *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS12_MAC_DATA_it);
}

static const ASN1_TEMPLATE bag_default_tt = {
	.flags = ASN1_TFLG_EXPLICIT,
	.tag = 0,
	.offset = offsetof(PKCS12_BAGS, value.other),
	.field_name = "value.other",
	.item = &ASN1_ANY_it,
};

static const ASN1_ADB_TABLE PKCS12_BAGS_adbtbl[] = {
	{
		.value = NID_x509Certificate,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT,
			.tag = 0,
			.offset = offsetof(PKCS12_BAGS, value.x509cert),
			.field_name = "value.x509cert",
			.item = &ASN1_OCTET_STRING_it,
		},
	
	},
	{
		.value = NID_x509Crl,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT,
			.tag = 0,
			.offset = offsetof(PKCS12_BAGS, value.x509crl),
			.field_name = "value.x509crl",
			.item = &ASN1_OCTET_STRING_it,
		},
	
	},
	{
		.value = NID_sdsiCertificate,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT,
			.tag = 0,
			.offset = offsetof(PKCS12_BAGS, value.sdsicert),
			.field_name = "value.sdsicert",
			.item = &ASN1_IA5STRING_it,
		},
	
	},
};

static const ASN1_ADB PKCS12_BAGS_adb = {
	.flags = 0,
	.offset = offsetof(PKCS12_BAGS, type),
	.tbl = PKCS12_BAGS_adbtbl,
	.tblcount = sizeof(PKCS12_BAGS_adbtbl) / sizeof(ASN1_ADB_TABLE),
	.default_tt = &bag_default_tt,
	.null_tt = NULL,
};

static const ASN1_TEMPLATE PKCS12_BAGS_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS12_BAGS, type),
		.field_name = "type",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_ADB_OID,
		.tag = -1,
		.offset = 0,
		.field_name = "PKCS12_BAGS",
		.item = (const ASN1_ITEM *)&PKCS12_BAGS_adb,
	},
};

const ASN1_ITEM PKCS12_BAGS_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS12_BAGS_seq_tt,
	.tcount = sizeof(PKCS12_BAGS_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(PKCS12_BAGS),
	.sname = "PKCS12_BAGS",
};


PKCS12_BAGS *
d2i_PKCS12_BAGS(PKCS12_BAGS **a, const unsigned char **in, long len)
{
	return (PKCS12_BAGS *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS12_BAGS_it);
}

int
i2d_PKCS12_BAGS(PKCS12_BAGS *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS12_BAGS_it);
}

PKCS12_BAGS *
PKCS12_BAGS_new(void)
{
	return (PKCS12_BAGS *)ASN1_item_new(&PKCS12_BAGS_it);
}

void
PKCS12_BAGS_free(PKCS12_BAGS *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS12_BAGS_it);
}

static const ASN1_TEMPLATE safebag_default_tt = {
	.flags = ASN1_TFLG_EXPLICIT,
	.tag = 0,
	.offset = offsetof(PKCS12_SAFEBAG, value.other),
	.field_name = "value.other",
	.item = &ASN1_ANY_it,
};

static const ASN1_ADB_TABLE PKCS12_SAFEBAG_adbtbl[] = {
	{
		.value = NID_keyBag,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT,
			.tag = 0,
			.offset = offsetof(PKCS12_SAFEBAG, value.keybag),
			.field_name = "value.keybag",
			.item = &PKCS8_PRIV_KEY_INFO_it,
		},
	
	},
	{
		.value = NID_pkcs8ShroudedKeyBag,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT,
			.tag = 0,
			.offset = offsetof(PKCS12_SAFEBAG, value.shkeybag),
			.field_name = "value.shkeybag",
			.item = &X509_SIG_it,
		},
	
	},
	{
		.value = NID_safeContentsBag,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_SEQUENCE_OF,
			.tag = 0,
			.offset = offsetof(PKCS12_SAFEBAG, value.safes),
			.field_name = "value.safes",
			.item = &PKCS12_SAFEBAG_it,
		},
	},
	{
		.value = NID_certBag,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT,
			.tag = 0,
			.offset = offsetof(PKCS12_SAFEBAG, value.bag),
			.field_name = "value.bag",
			.item = &PKCS12_BAGS_it,
		},
	
	},
	{
		.value = NID_crlBag,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT,
			.tag = 0,
			.offset = offsetof(PKCS12_SAFEBAG, value.bag),
			.field_name = "value.bag",
			.item = &PKCS12_BAGS_it,
		},
	
	},
	{
		.value = NID_secretBag,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT,
			.tag = 0,
			.offset = offsetof(PKCS12_SAFEBAG, value.bag),
			.field_name = "value.bag",
			.item = &PKCS12_BAGS_it,
		},
	
	},
};

static const ASN1_ADB PKCS12_SAFEBAG_adb = {
	.flags = 0,
	.offset = offsetof(PKCS12_SAFEBAG, type),
	.tbl = PKCS12_SAFEBAG_adbtbl,
	.tblcount = sizeof(PKCS12_SAFEBAG_adbtbl) / sizeof(ASN1_ADB_TABLE),
	.default_tt = &safebag_default_tt,
	.null_tt = NULL,
};

static const ASN1_TEMPLATE PKCS12_SAFEBAG_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS12_SAFEBAG, type),
		.field_name = "type",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_ADB_OID,
		.tag = -1,
		.offset = 0,
		.field_name = "PKCS12_SAFEBAG",
		.item = (const ASN1_ITEM *)&PKCS12_SAFEBAG_adb,
	},
	{
		.flags = ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(PKCS12_SAFEBAG, attrib),
		.field_name = "attrib",
		.item = &X509_ATTRIBUTE_it,
	},
};

const ASN1_ITEM PKCS12_SAFEBAG_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS12_SAFEBAG_seq_tt,
	.tcount = sizeof(PKCS12_SAFEBAG_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(PKCS12_SAFEBAG),
	.sname = "PKCS12_SAFEBAG",
};
LCRYPTO_ALIAS(PKCS12_SAFEBAG_it);


PKCS12_SAFEBAG *
d2i_PKCS12_SAFEBAG(PKCS12_SAFEBAG **a, const unsigned char **in, long len)
{
	return (PKCS12_SAFEBAG *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS12_SAFEBAG_it);
}
LCRYPTO_ALIAS(d2i_PKCS12_SAFEBAG);

int
i2d_PKCS12_SAFEBAG(PKCS12_SAFEBAG *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS12_SAFEBAG_it);
}
LCRYPTO_ALIAS(i2d_PKCS12_SAFEBAG);

PKCS12_SAFEBAG *
PKCS12_SAFEBAG_new(void)
{
	return (PKCS12_SAFEBAG *)ASN1_item_new(&PKCS12_SAFEBAG_it);
}
LCRYPTO_ALIAS(PKCS12_SAFEBAG_new);

void
PKCS12_SAFEBAG_free(PKCS12_SAFEBAG *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS12_SAFEBAG_it);
}
LCRYPTO_ALIAS(PKCS12_SAFEBAG_free);

/* SEQUENCE OF SafeBag */
static const ASN1_TEMPLATE PKCS12_SAFEBAGS_item_tt = {
	.flags = ASN1_TFLG_SEQUENCE_OF,
	.tag = 0,
	.offset = 0,
	.field_name = "PKCS12_SAFEBAGS",
	.item = &PKCS12_SAFEBAG_it,
};

const ASN1_ITEM PKCS12_SAFEBAGS_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &PKCS12_SAFEBAGS_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "PKCS12_SAFEBAGS",
};

/* Authsafes: SEQUENCE OF PKCS7 */
static const ASN1_TEMPLATE PKCS12_AUTHSAFES_item_tt = {
	.flags = ASN1_TFLG_SEQUENCE_OF,
	.tag = 0,
	.offset = 0,
	.field_name = "PKCS12_AUTHSAFES",
	.item = &PKCS7_it,
};

const ASN1_ITEM PKCS12_AUTHSAFES_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &PKCS12_AUTHSAFES_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "PKCS12_AUTHSAFES",
};
