/* $OpenBSD: pk7_asn1.c,v 1.19 2025/06/11 18:11:55 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2000.
 */
/* ====================================================================
 * Copyright (c) 2000 The OpenSSL Project.  All rights reserved.
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
#include <openssl/pkcs7.h>
#include <openssl/x509.h>

/* PKCS#7 ASN1 module */

/* This is the ANY DEFINED BY table for the top level PKCS#7 structure */

static const ASN1_TEMPLATE p7default_tt = {
	.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
	.tag = 0,
	.offset = offsetof(PKCS7, d.other),
	.field_name = "d.other",
	.item = &ASN1_ANY_it,
};

static const ASN1_ADB_TABLE PKCS7_adbtbl[] = {
	{
		.value = NID_pkcs7_data,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL | ASN1_TFLG_NDEF,
			.tag = 0,
			.offset = offsetof(PKCS7, d.data),
			.field_name = "d.data",
			.item = &ASN1_OCTET_STRING_NDEF_it,
		},
	},
	{
		.value = NID_pkcs7_signed,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL | ASN1_TFLG_NDEF,
			.tag = 0,
			.offset = offsetof(PKCS7, d.sign),
			.field_name = "d.sign",
			.item = &PKCS7_SIGNED_it,
		},
	},
	{
		.value = NID_pkcs7_enveloped,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL | ASN1_TFLG_NDEF,
			.tag = 0,
			.offset = offsetof(PKCS7, d.enveloped),
			.field_name = "d.enveloped",
			.item = &PKCS7_ENVELOPE_it,
		},
	},
	{
		.value = NID_pkcs7_signedAndEnveloped,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL | ASN1_TFLG_NDEF,
			.tag = 0,
			.offset = offsetof(PKCS7, d.signed_and_enveloped),
			.field_name = "d.signed_and_enveloped",
			.item = &PKCS7_SIGN_ENVELOPE_it,
		},
	},
	{
		.value = NID_pkcs7_digest,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL | ASN1_TFLG_NDEF,
			.tag = 0,
			.offset = offsetof(PKCS7, d.digest),
			.field_name = "d.digest",
			.item = &PKCS7_DIGEST_it,
		},
	},
	{
		.value = NID_pkcs7_encrypted,
		.tt = {
			.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL | ASN1_TFLG_NDEF,
			.tag = 0,
			.offset = offsetof(PKCS7, d.encrypted),
			.field_name = "d.encrypted",
			.item = &PKCS7_ENCRYPT_it,
		},
	},
};

static const ASN1_ADB PKCS7_adb = {
	.flags = 0,
	.offset = offsetof(PKCS7, type),
	.tbl = PKCS7_adbtbl,
	.tblcount = sizeof(PKCS7_adbtbl) / sizeof(ASN1_ADB_TABLE),
	.default_tt = &p7default_tt,
	.null_tt = NULL,
};

/* PKCS#7 streaming support */
static int
pk7_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	ASN1_STREAM_ARG *sarg = exarg;
	PKCS7 **pp7 = (PKCS7 **)pval;

	switch (operation) {
	case ASN1_OP_STREAM_PRE:
		if (PKCS7_stream(&sarg->boundary, *pp7) <= 0)
			return 0;
		/* FALLTHROUGH */

	case ASN1_OP_DETACHED_PRE:
		sarg->ndef_bio = PKCS7_dataInit(*pp7, sarg->out);
		if (!sarg->ndef_bio)
			return 0;
		break;

	case ASN1_OP_STREAM_POST:
	case ASN1_OP_DETACHED_POST:
		if (PKCS7_dataFinal(*pp7, sarg->ndef_bio) <= 0)
			return 0;
		break;
	}
	return 1;
}

static const ASN1_AUX PKCS7_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = pk7_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE PKCS7_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7, type),
		.field_name = "type",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_ADB_OID,
		.tag = -1,
		.offset = 0,
		.field_name = "PKCS7",
		.item = (const ASN1_ITEM *)&PKCS7_adb,
	},
};

const ASN1_ITEM PKCS7_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS7_seq_tt,
	.tcount = sizeof(PKCS7_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &PKCS7_aux,
	.size = sizeof(PKCS7),
	.sname = "PKCS7",
};
LCRYPTO_ALIAS(PKCS7_it);


PKCS7 *
d2i_PKCS7(PKCS7 **a, const unsigned char **in, long len)
{
	return (PKCS7 *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS7_it);
}
LCRYPTO_ALIAS(d2i_PKCS7);

int
i2d_PKCS7(PKCS7 *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS7_it);
}
LCRYPTO_ALIAS(i2d_PKCS7);

PKCS7 *
PKCS7_new(void)
{
	return (PKCS7 *)ASN1_item_new(&PKCS7_it);
}
LCRYPTO_ALIAS(PKCS7_new);

void
PKCS7_free(PKCS7 *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS7_it);
}
LCRYPTO_ALIAS(PKCS7_free);

PKCS7 *
PKCS7_dup(PKCS7 *x)
{
	return ASN1_item_dup(&PKCS7_it, x);
}
LCRYPTO_ALIAS(PKCS7_dup);

static const ASN1_TEMPLATE PKCS7_SIGNED_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGNED, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = ASN1_TFLG_SET_OF,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGNED, md_algs),
		.field_name = "md_algs",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGNED, contents),
		.field_name = "contents",
		.item = &PKCS7_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGNED, cert),
		.field_name = "cert",
		.item = &X509_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(PKCS7_SIGNED, crl),
		.field_name = "crl",
		.item = &X509_CRL_it,
	},
	{
		.flags = ASN1_TFLG_SET_OF,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGNED, signer_info),
		.field_name = "signer_info",
		.item = &PKCS7_SIGNER_INFO_it,
	},
};

const ASN1_ITEM PKCS7_SIGNED_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS7_SIGNED_seq_tt,
	.tcount = sizeof(PKCS7_SIGNED_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(PKCS7_SIGNED),
	.sname = "PKCS7_SIGNED",
};
LCRYPTO_ALIAS(PKCS7_SIGNED_it);


PKCS7_SIGNED *
d2i_PKCS7_SIGNED(PKCS7_SIGNED **a, const unsigned char **in, long len)
{
	return (PKCS7_SIGNED *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS7_SIGNED_it);
}
LCRYPTO_ALIAS(d2i_PKCS7_SIGNED);

int
i2d_PKCS7_SIGNED(PKCS7_SIGNED *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS7_SIGNED_it);
}
LCRYPTO_ALIAS(i2d_PKCS7_SIGNED);

PKCS7_SIGNED *
PKCS7_SIGNED_new(void)
{
	return (PKCS7_SIGNED *)ASN1_item_new(&PKCS7_SIGNED_it);
}
LCRYPTO_ALIAS(PKCS7_SIGNED_new);

void
PKCS7_SIGNED_free(PKCS7_SIGNED *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS7_SIGNED_it);
}
LCRYPTO_ALIAS(PKCS7_SIGNED_free);

/* Minor tweak to operation: free up EVP_PKEY */
static int
si_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	if (operation == ASN1_OP_FREE_POST) {
		PKCS7_SIGNER_INFO *si = (PKCS7_SIGNER_INFO *)*pval;
		EVP_PKEY_free(si->pkey);
	}
	return 1;
}

static const ASN1_AUX PKCS7_SIGNER_INFO_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = si_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE PKCS7_SIGNER_INFO_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGNER_INFO, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGNER_INFO, issuer_and_serial),
		.field_name = "issuer_and_serial",
		.item = &PKCS7_ISSUER_AND_SERIAL_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGNER_INFO, digest_alg),
		.field_name = "digest_alg",
		.item = &X509_ALGOR_it,
	},
	/* NB this should be a SET OF but we use a SEQUENCE OF so the
	 * original order * is retained when the structure is reencoded.
	 * Since the attributes are implicitly tagged this will not affect
	 * the encoding.
	 */
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGNER_INFO, auth_attr),
		.field_name = "auth_attr",
		.item = &X509_ATTRIBUTE_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGNER_INFO, digest_enc_alg),
		.field_name = "digest_enc_alg",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGNER_INFO, enc_digest),
		.field_name = "enc_digest",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(PKCS7_SIGNER_INFO, unauth_attr),
		.field_name = "unauth_attr",
		.item = &X509_ATTRIBUTE_it,
	},
};

const ASN1_ITEM PKCS7_SIGNER_INFO_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS7_SIGNER_INFO_seq_tt,
	.tcount = sizeof(PKCS7_SIGNER_INFO_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &PKCS7_SIGNER_INFO_aux,
	.size = sizeof(PKCS7_SIGNER_INFO),
	.sname = "PKCS7_SIGNER_INFO",
};
LCRYPTO_ALIAS(PKCS7_SIGNER_INFO_it);


PKCS7_SIGNER_INFO *
d2i_PKCS7_SIGNER_INFO(PKCS7_SIGNER_INFO **a, const unsigned char **in, long len)
{
	return (PKCS7_SIGNER_INFO *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS7_SIGNER_INFO_it);
}
LCRYPTO_ALIAS(d2i_PKCS7_SIGNER_INFO);

int
i2d_PKCS7_SIGNER_INFO(PKCS7_SIGNER_INFO *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS7_SIGNER_INFO_it);
}
LCRYPTO_ALIAS(i2d_PKCS7_SIGNER_INFO);

PKCS7_SIGNER_INFO *
PKCS7_SIGNER_INFO_new(void)
{
	return (PKCS7_SIGNER_INFO *)ASN1_item_new(&PKCS7_SIGNER_INFO_it);
}
LCRYPTO_ALIAS(PKCS7_SIGNER_INFO_new);

void
PKCS7_SIGNER_INFO_free(PKCS7_SIGNER_INFO *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS7_SIGNER_INFO_it);
}
LCRYPTO_ALIAS(PKCS7_SIGNER_INFO_free);

static const ASN1_TEMPLATE PKCS7_ISSUER_AND_SERIAL_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_ISSUER_AND_SERIAL, issuer),
		.field_name = "issuer",
		.item = &X509_NAME_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_ISSUER_AND_SERIAL, serial),
		.field_name = "serial",
		.item = &ASN1_INTEGER_it,
	},
};

const ASN1_ITEM PKCS7_ISSUER_AND_SERIAL_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS7_ISSUER_AND_SERIAL_seq_tt,
	.tcount = sizeof(PKCS7_ISSUER_AND_SERIAL_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(PKCS7_ISSUER_AND_SERIAL),
	.sname = "PKCS7_ISSUER_AND_SERIAL",
};
LCRYPTO_ALIAS(PKCS7_ISSUER_AND_SERIAL_it);


PKCS7_ISSUER_AND_SERIAL *
d2i_PKCS7_ISSUER_AND_SERIAL(PKCS7_ISSUER_AND_SERIAL **a, const unsigned char **in, long len)
{
	return (PKCS7_ISSUER_AND_SERIAL *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS7_ISSUER_AND_SERIAL_it);
}
LCRYPTO_ALIAS(d2i_PKCS7_ISSUER_AND_SERIAL);

int
i2d_PKCS7_ISSUER_AND_SERIAL(PKCS7_ISSUER_AND_SERIAL *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS7_ISSUER_AND_SERIAL_it);
}
LCRYPTO_ALIAS(i2d_PKCS7_ISSUER_AND_SERIAL);

PKCS7_ISSUER_AND_SERIAL *
PKCS7_ISSUER_AND_SERIAL_new(void)
{
	return (PKCS7_ISSUER_AND_SERIAL *)ASN1_item_new(&PKCS7_ISSUER_AND_SERIAL_it);
}
LCRYPTO_ALIAS(PKCS7_ISSUER_AND_SERIAL_new);

void
PKCS7_ISSUER_AND_SERIAL_free(PKCS7_ISSUER_AND_SERIAL *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS7_ISSUER_AND_SERIAL_it);
}
LCRYPTO_ALIAS(PKCS7_ISSUER_AND_SERIAL_free);

static const ASN1_TEMPLATE PKCS7_ENVELOPE_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_ENVELOPE, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = ASN1_TFLG_SET_OF,
		.tag = 0,
		.offset = offsetof(PKCS7_ENVELOPE, recipientinfo),
		.field_name = "recipientinfo",
		.item = &PKCS7_RECIP_INFO_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_ENVELOPE, enc_data),
		.field_name = "enc_data",
		.item = &PKCS7_ENC_CONTENT_it,
	},
};

const ASN1_ITEM PKCS7_ENVELOPE_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS7_ENVELOPE_seq_tt,
	.tcount = sizeof(PKCS7_ENVELOPE_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(PKCS7_ENVELOPE),
	.sname = "PKCS7_ENVELOPE",
};
LCRYPTO_ALIAS(PKCS7_ENVELOPE_it);


PKCS7_ENVELOPE *
d2i_PKCS7_ENVELOPE(PKCS7_ENVELOPE **a, const unsigned char **in, long len)
{
	return (PKCS7_ENVELOPE *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS7_ENVELOPE_it);
}
LCRYPTO_ALIAS(d2i_PKCS7_ENVELOPE);

int
i2d_PKCS7_ENVELOPE(PKCS7_ENVELOPE *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS7_ENVELOPE_it);
}
LCRYPTO_ALIAS(i2d_PKCS7_ENVELOPE);

PKCS7_ENVELOPE *
PKCS7_ENVELOPE_new(void)
{
	return (PKCS7_ENVELOPE *)ASN1_item_new(&PKCS7_ENVELOPE_it);
}
LCRYPTO_ALIAS(PKCS7_ENVELOPE_new);

void
PKCS7_ENVELOPE_free(PKCS7_ENVELOPE *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS7_ENVELOPE_it);
}
LCRYPTO_ALIAS(PKCS7_ENVELOPE_free);

/* Minor tweak to operation: free up X509 */
static int
ri_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	if (operation == ASN1_OP_FREE_POST) {
		PKCS7_RECIP_INFO *ri = (PKCS7_RECIP_INFO *)*pval;
		X509_free(ri->cert);
	}
	return 1;
}

static const ASN1_AUX PKCS7_RECIP_INFO_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = ri_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE PKCS7_RECIP_INFO_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_RECIP_INFO, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_RECIP_INFO, issuer_and_serial),
		.field_name = "issuer_and_serial",
		.item = &PKCS7_ISSUER_AND_SERIAL_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_RECIP_INFO, key_enc_algor),
		.field_name = "key_enc_algor",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_RECIP_INFO, enc_key),
		.field_name = "enc_key",
		.item = &ASN1_OCTET_STRING_it,
	},
};

const ASN1_ITEM PKCS7_RECIP_INFO_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS7_RECIP_INFO_seq_tt,
	.tcount = sizeof(PKCS7_RECIP_INFO_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &PKCS7_RECIP_INFO_aux,
	.size = sizeof(PKCS7_RECIP_INFO),
	.sname = "PKCS7_RECIP_INFO",
};
LCRYPTO_ALIAS(PKCS7_RECIP_INFO_it);


PKCS7_RECIP_INFO *
d2i_PKCS7_RECIP_INFO(PKCS7_RECIP_INFO **a, const unsigned char **in, long len)
{
	return (PKCS7_RECIP_INFO *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS7_RECIP_INFO_it);
}
LCRYPTO_ALIAS(d2i_PKCS7_RECIP_INFO);

int
i2d_PKCS7_RECIP_INFO(PKCS7_RECIP_INFO *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS7_RECIP_INFO_it);
}
LCRYPTO_ALIAS(i2d_PKCS7_RECIP_INFO);

PKCS7_RECIP_INFO *
PKCS7_RECIP_INFO_new(void)
{
	return (PKCS7_RECIP_INFO *)ASN1_item_new(&PKCS7_RECIP_INFO_it);
}
LCRYPTO_ALIAS(PKCS7_RECIP_INFO_new);

void
PKCS7_RECIP_INFO_free(PKCS7_RECIP_INFO *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS7_RECIP_INFO_it);
}
LCRYPTO_ALIAS(PKCS7_RECIP_INFO_free);

static const ASN1_TEMPLATE PKCS7_ENC_CONTENT_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_ENC_CONTENT, content_type),
		.field_name = "content_type",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_ENC_CONTENT, algorithm),
		.field_name = "algorithm",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(PKCS7_ENC_CONTENT, enc_data),
		.field_name = "enc_data",
		.item = &ASN1_OCTET_STRING_NDEF_it,
	},
};

const ASN1_ITEM PKCS7_ENC_CONTENT_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS7_ENC_CONTENT_seq_tt,
	.tcount = sizeof(PKCS7_ENC_CONTENT_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(PKCS7_ENC_CONTENT),
	.sname = "PKCS7_ENC_CONTENT",
};
LCRYPTO_ALIAS(PKCS7_ENC_CONTENT_it);


PKCS7_ENC_CONTENT *
d2i_PKCS7_ENC_CONTENT(PKCS7_ENC_CONTENT **a, const unsigned char **in, long len)
{
	return (PKCS7_ENC_CONTENT *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS7_ENC_CONTENT_it);
}
LCRYPTO_ALIAS(d2i_PKCS7_ENC_CONTENT);

int
i2d_PKCS7_ENC_CONTENT(PKCS7_ENC_CONTENT *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS7_ENC_CONTENT_it);
}
LCRYPTO_ALIAS(i2d_PKCS7_ENC_CONTENT);

PKCS7_ENC_CONTENT *
PKCS7_ENC_CONTENT_new(void)
{
	return (PKCS7_ENC_CONTENT *)ASN1_item_new(&PKCS7_ENC_CONTENT_it);
}
LCRYPTO_ALIAS(PKCS7_ENC_CONTENT_new);

void
PKCS7_ENC_CONTENT_free(PKCS7_ENC_CONTENT *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS7_ENC_CONTENT_it);
}
LCRYPTO_ALIAS(PKCS7_ENC_CONTENT_free);

static const ASN1_TEMPLATE PKCS7_SIGN_ENVELOPE_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGN_ENVELOPE, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = ASN1_TFLG_SET_OF,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGN_ENVELOPE, recipientinfo),
		.field_name = "recipientinfo",
		.item = &PKCS7_RECIP_INFO_it,
	},
	{
		.flags = ASN1_TFLG_SET_OF,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGN_ENVELOPE, md_algs),
		.field_name = "md_algs",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGN_ENVELOPE, enc_data),
		.field_name = "enc_data",
		.item = &PKCS7_ENC_CONTENT_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGN_ENVELOPE, cert),
		.field_name = "cert",
		.item = &X509_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(PKCS7_SIGN_ENVELOPE, crl),
		.field_name = "crl",
		.item = &X509_CRL_it,
	},
	{
		.flags = ASN1_TFLG_SET_OF,
		.tag = 0,
		.offset = offsetof(PKCS7_SIGN_ENVELOPE, signer_info),
		.field_name = "signer_info",
		.item = &PKCS7_SIGNER_INFO_it,
	},
};

const ASN1_ITEM PKCS7_SIGN_ENVELOPE_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS7_SIGN_ENVELOPE_seq_tt,
	.tcount = sizeof(PKCS7_SIGN_ENVELOPE_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(PKCS7_SIGN_ENVELOPE),
	.sname = "PKCS7_SIGN_ENVELOPE",
};
LCRYPTO_ALIAS(PKCS7_SIGN_ENVELOPE_it);


PKCS7_SIGN_ENVELOPE *
d2i_PKCS7_SIGN_ENVELOPE(PKCS7_SIGN_ENVELOPE **a, const unsigned char **in, long len)
{
	return (PKCS7_SIGN_ENVELOPE *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS7_SIGN_ENVELOPE_it);
}
LCRYPTO_ALIAS(d2i_PKCS7_SIGN_ENVELOPE);

int
i2d_PKCS7_SIGN_ENVELOPE(PKCS7_SIGN_ENVELOPE *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS7_SIGN_ENVELOPE_it);
}
LCRYPTO_ALIAS(i2d_PKCS7_SIGN_ENVELOPE);

PKCS7_SIGN_ENVELOPE *
PKCS7_SIGN_ENVELOPE_new(void)
{
	return (PKCS7_SIGN_ENVELOPE *)ASN1_item_new(&PKCS7_SIGN_ENVELOPE_it);
}
LCRYPTO_ALIAS(PKCS7_SIGN_ENVELOPE_new);

void
PKCS7_SIGN_ENVELOPE_free(PKCS7_SIGN_ENVELOPE *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS7_SIGN_ENVELOPE_it);
}
LCRYPTO_ALIAS(PKCS7_SIGN_ENVELOPE_free);

static const ASN1_TEMPLATE PKCS7_ENCRYPT_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_ENCRYPT, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_ENCRYPT, enc_data),
		.field_name = "enc_data",
		.item = &PKCS7_ENC_CONTENT_it,
	},
};

const ASN1_ITEM PKCS7_ENCRYPT_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS7_ENCRYPT_seq_tt,
	.tcount = sizeof(PKCS7_ENCRYPT_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(PKCS7_ENCRYPT),
	.sname = "PKCS7_ENCRYPT",
};
LCRYPTO_ALIAS(PKCS7_ENCRYPT_it);


PKCS7_ENCRYPT *
d2i_PKCS7_ENCRYPT(PKCS7_ENCRYPT **a, const unsigned char **in, long len)
{
	return (PKCS7_ENCRYPT *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS7_ENCRYPT_it);
}
LCRYPTO_ALIAS(d2i_PKCS7_ENCRYPT);

int
i2d_PKCS7_ENCRYPT(PKCS7_ENCRYPT *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS7_ENCRYPT_it);
}
LCRYPTO_ALIAS(i2d_PKCS7_ENCRYPT);

PKCS7_ENCRYPT *
PKCS7_ENCRYPT_new(void)
{
	return (PKCS7_ENCRYPT *)ASN1_item_new(&PKCS7_ENCRYPT_it);
}
LCRYPTO_ALIAS(PKCS7_ENCRYPT_new);

void
PKCS7_ENCRYPT_free(PKCS7_ENCRYPT *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS7_ENCRYPT_it);
}
LCRYPTO_ALIAS(PKCS7_ENCRYPT_free);

static const ASN1_TEMPLATE PKCS7_DIGEST_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_DIGEST, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_DIGEST, md),
		.field_name = "md",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_DIGEST, contents),
		.field_name = "contents",
		.item = &PKCS7_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(PKCS7_DIGEST, digest),
		.field_name = "digest",
		.item = &ASN1_OCTET_STRING_it,
	},
};

const ASN1_ITEM PKCS7_DIGEST_it = {
	.itype = ASN1_ITYPE_NDEF_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = PKCS7_DIGEST_seq_tt,
	.tcount = sizeof(PKCS7_DIGEST_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(PKCS7_DIGEST),
	.sname = "PKCS7_DIGEST",
};
LCRYPTO_ALIAS(PKCS7_DIGEST_it);


PKCS7_DIGEST *
d2i_PKCS7_DIGEST(PKCS7_DIGEST **a, const unsigned char **in, long len)
{
	return (PKCS7_DIGEST *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &PKCS7_DIGEST_it);
}
LCRYPTO_ALIAS(d2i_PKCS7_DIGEST);

int
i2d_PKCS7_DIGEST(PKCS7_DIGEST *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &PKCS7_DIGEST_it);
}
LCRYPTO_ALIAS(i2d_PKCS7_DIGEST);

PKCS7_DIGEST *
PKCS7_DIGEST_new(void)
{
	return (PKCS7_DIGEST *)ASN1_item_new(&PKCS7_DIGEST_it);
}
LCRYPTO_ALIAS(PKCS7_DIGEST_new);

void
PKCS7_DIGEST_free(PKCS7_DIGEST *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &PKCS7_DIGEST_it);
}
LCRYPTO_ALIAS(PKCS7_DIGEST_free);

/* Specials for authenticated attributes */

/* When signing attributes we want to reorder them to match the sorted
 * encoding.
 */

static const ASN1_TEMPLATE PKCS7_ATTR_SIGN_item_tt = {
	.flags = ASN1_TFLG_SET_ORDER,
	.tag = 0,
	.offset = 0,
	.field_name = "PKCS7_ATTRIBUTES",
	.item = &X509_ATTRIBUTE_it,
};

const ASN1_ITEM PKCS7_ATTR_SIGN_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &PKCS7_ATTR_SIGN_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "PKCS7_ATTR_SIGN",
};
LCRYPTO_ALIAS(PKCS7_ATTR_SIGN_it);

/* When verifying attributes we need to use the received order. So
 * we use SEQUENCE OF and tag it to SET OF
 */

static const ASN1_TEMPLATE PKCS7_ATTR_VERIFY_item_tt = {
	.flags = ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_IMPTAG | ASN1_TFLG_UNIVERSAL,
	.tag = V_ASN1_SET,
	.offset = 0,
	.field_name = "PKCS7_ATTRIBUTES",
	.item = &X509_ATTRIBUTE_it,
};

const ASN1_ITEM PKCS7_ATTR_VERIFY_it = {
	.itype = ASN1_ITYPE_PRIMITIVE,
	.utype = -1,
	.templates = &PKCS7_ATTR_VERIFY_item_tt,
	.tcount = 0,
	.funcs = NULL,
	.size = 0,
	.sname = "PKCS7_ATTR_VERIFY",
};
LCRYPTO_ALIAS(PKCS7_ATTR_VERIFY_it);


int
PKCS7_print_ctx(BIO *out, PKCS7 *x, int indent, const ASN1_PCTX *pctx)
{
	return ASN1_item_print(out, (ASN1_VALUE *)x, indent,
	    &PKCS7_it, pctx);
}
LCRYPTO_ALIAS(PKCS7_print_ctx);

PKCS7 *
d2i_PKCS7_bio(BIO *bp, PKCS7 **p7)
{
	return ASN1_item_d2i_bio(&PKCS7_it, bp, p7);
}
LCRYPTO_ALIAS(d2i_PKCS7_bio);

int
i2d_PKCS7_bio(BIO *bp, PKCS7 *p7)
{
	return ASN1_item_i2d_bio(&PKCS7_it, bp, p7);
}
LCRYPTO_ALIAS(i2d_PKCS7_bio);

PKCS7 *
d2i_PKCS7_fp(FILE *fp, PKCS7 **p7)
{
	return ASN1_item_d2i_fp(&PKCS7_it, fp, p7);
}
LCRYPTO_ALIAS(d2i_PKCS7_fp);

int
i2d_PKCS7_fp(FILE *fp, PKCS7 *p7)
{
	return ASN1_item_i2d_fp(&PKCS7_it, fp, p7);
}
LCRYPTO_ALIAS(i2d_PKCS7_fp);

int
PKCS7_ISSUER_AND_SERIAL_digest(PKCS7_ISSUER_AND_SERIAL *data,
    const EVP_MD *type, unsigned char *md, unsigned int *len)
{
	return(ASN1_item_digest(&PKCS7_ISSUER_AND_SERIAL_it, type,
	    (char *)data, md, len));
}
LCRYPTO_ALIAS(PKCS7_ISSUER_AND_SERIAL_digest);
