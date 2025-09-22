/* $OpenBSD: ts_asn1.c,v 1.16 2025/05/10 05:54:39 tb Exp $ */
/* Written by Nils Larsch for the OpenSSL project 2004.
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

#include <openssl/opensslconf.h>

#include <openssl/ts.h>
#include <openssl/asn1t.h>

#include "err_local.h"
#include "ts_local.h"

static const ASN1_TEMPLATE TS_MSG_IMPRINT_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(TS_MSG_IMPRINT, hash_algo),
		.field_name = "hash_algo",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(TS_MSG_IMPRINT, hashed_msg),
		.field_name = "hashed_msg",
		.item = &ASN1_OCTET_STRING_it,
	},
};

static const ASN1_ITEM TS_MSG_IMPRINT_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = TS_MSG_IMPRINT_seq_tt,
	.tcount = sizeof(TS_MSG_IMPRINT_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(TS_MSG_IMPRINT),
	.sname = "TS_MSG_IMPRINT",
};


TS_MSG_IMPRINT *
d2i_TS_MSG_IMPRINT(TS_MSG_IMPRINT **a, const unsigned char **in, long len)
{
	return (TS_MSG_IMPRINT *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &TS_MSG_IMPRINT_it);
}
LCRYPTO_ALIAS(d2i_TS_MSG_IMPRINT);

int
i2d_TS_MSG_IMPRINT(const TS_MSG_IMPRINT *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &TS_MSG_IMPRINT_it);
}
LCRYPTO_ALIAS(i2d_TS_MSG_IMPRINT);

TS_MSG_IMPRINT *
TS_MSG_IMPRINT_new(void)
{
	return (TS_MSG_IMPRINT *)ASN1_item_new(&TS_MSG_IMPRINT_it);
}
LCRYPTO_ALIAS(TS_MSG_IMPRINT_new);

void
TS_MSG_IMPRINT_free(TS_MSG_IMPRINT *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &TS_MSG_IMPRINT_it);
}
LCRYPTO_ALIAS(TS_MSG_IMPRINT_free);

TS_MSG_IMPRINT *
TS_MSG_IMPRINT_dup(TS_MSG_IMPRINT *x)
{
	return ASN1_item_dup(&TS_MSG_IMPRINT_it, x);
}
LCRYPTO_ALIAS(TS_MSG_IMPRINT_dup);

#ifndef OPENSSL_NO_BIO
TS_MSG_IMPRINT *
d2i_TS_MSG_IMPRINT_bio(BIO *bp, TS_MSG_IMPRINT **a)
{
	return ASN1_item_d2i_bio(&TS_MSG_IMPRINT_it, bp, a);
}
LCRYPTO_ALIAS(d2i_TS_MSG_IMPRINT_bio);

int
i2d_TS_MSG_IMPRINT_bio(BIO *bp, TS_MSG_IMPRINT *a)
{
	return ASN1_item_i2d_bio(&TS_MSG_IMPRINT_it, bp, a);
}
LCRYPTO_ALIAS(i2d_TS_MSG_IMPRINT_bio);
#endif

TS_MSG_IMPRINT *
d2i_TS_MSG_IMPRINT_fp(FILE *fp, TS_MSG_IMPRINT **a)
{
	return ASN1_item_d2i_fp(&TS_MSG_IMPRINT_it, fp, a);
}
LCRYPTO_ALIAS(d2i_TS_MSG_IMPRINT_fp);

int
i2d_TS_MSG_IMPRINT_fp(FILE *fp, TS_MSG_IMPRINT *a)
{
	return ASN1_item_i2d_fp(&TS_MSG_IMPRINT_it, fp, a);
}
LCRYPTO_ALIAS(i2d_TS_MSG_IMPRINT_fp);

static const ASN1_TEMPLATE TS_REQ_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(TS_REQ, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(TS_REQ, msg_imprint),
		.field_name = "msg_imprint",
		.item = &TS_MSG_IMPRINT_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(TS_REQ, policy_id),
		.field_name = "policy_id",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(TS_REQ, nonce),
		.field_name = "nonce",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(TS_REQ, cert_req),
		.field_name = "cert_req",
		.item = &ASN1_FBOOLEAN_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(TS_REQ, extensions),
		.field_name = "extensions",
		.item = &X509_EXTENSION_it,
	},
};

static const ASN1_ITEM TS_REQ_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = TS_REQ_seq_tt,
	.tcount = sizeof(TS_REQ_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(TS_REQ),
	.sname = "TS_REQ",
};


TS_REQ *
d2i_TS_REQ(TS_REQ **a, const unsigned char **in, long len)
{
	return (TS_REQ *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &TS_REQ_it);
}
LCRYPTO_ALIAS(d2i_TS_REQ);

int
i2d_TS_REQ(const TS_REQ *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &TS_REQ_it);
}
LCRYPTO_ALIAS(i2d_TS_REQ);

TS_REQ *
TS_REQ_new(void)
{
	return (TS_REQ *)ASN1_item_new(&TS_REQ_it);
}
LCRYPTO_ALIAS(TS_REQ_new);

void
TS_REQ_free(TS_REQ *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &TS_REQ_it);
}
LCRYPTO_ALIAS(TS_REQ_free);

TS_REQ *
TS_REQ_dup(TS_REQ *x)
{
	return ASN1_item_dup(&TS_REQ_it, x);
}
LCRYPTO_ALIAS(TS_REQ_dup);

#ifndef OPENSSL_NO_BIO
TS_REQ *
d2i_TS_REQ_bio(BIO *bp, TS_REQ **a)
{
	return ASN1_item_d2i_bio(&TS_REQ_it, bp, a);
}
LCRYPTO_ALIAS(d2i_TS_REQ_bio);

int
i2d_TS_REQ_bio(BIO *bp, TS_REQ *a)
{
	return ASN1_item_i2d_bio(&TS_REQ_it, bp, a);
}
LCRYPTO_ALIAS(i2d_TS_REQ_bio);
#endif

TS_REQ *
d2i_TS_REQ_fp(FILE *fp, TS_REQ **a)
{
	return ASN1_item_d2i_fp(&TS_REQ_it, fp, a);
}
LCRYPTO_ALIAS(d2i_TS_REQ_fp);

int
i2d_TS_REQ_fp(FILE *fp, TS_REQ *a)
{
	return ASN1_item_i2d_fp(&TS_REQ_it, fp, a);
}
LCRYPTO_ALIAS(i2d_TS_REQ_fp);

static const ASN1_TEMPLATE TS_ACCURACY_seq_tt[] = {
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(TS_ACCURACY, seconds),
		.field_name = "seconds",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(TS_ACCURACY, millis),
		.field_name = "millis",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(TS_ACCURACY, micros),
		.field_name = "micros",
		.item = &ASN1_INTEGER_it,
	},
};

static const ASN1_ITEM TS_ACCURACY_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = TS_ACCURACY_seq_tt,
	.tcount = sizeof(TS_ACCURACY_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(TS_ACCURACY),
	.sname = "TS_ACCURACY",
};


TS_ACCURACY *
d2i_TS_ACCURACY(TS_ACCURACY **a, const unsigned char **in, long len)
{
	return (TS_ACCURACY *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &TS_ACCURACY_it);
}
LCRYPTO_ALIAS(d2i_TS_ACCURACY);

int
i2d_TS_ACCURACY(const TS_ACCURACY *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &TS_ACCURACY_it);
}
LCRYPTO_ALIAS(i2d_TS_ACCURACY);

TS_ACCURACY *
TS_ACCURACY_new(void)
{
	return (TS_ACCURACY *)ASN1_item_new(&TS_ACCURACY_it);
}
LCRYPTO_ALIAS(TS_ACCURACY_new);

void
TS_ACCURACY_free(TS_ACCURACY *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &TS_ACCURACY_it);
}
LCRYPTO_ALIAS(TS_ACCURACY_free);

TS_ACCURACY *
TS_ACCURACY_dup(TS_ACCURACY *x)
{
	return ASN1_item_dup(&TS_ACCURACY_it, x);
}
LCRYPTO_ALIAS(TS_ACCURACY_dup);

static const ASN1_TEMPLATE TS_TST_INFO_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(TS_TST_INFO, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(TS_TST_INFO, policy_id),
		.field_name = "policy_id",
		.item = &ASN1_OBJECT_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(TS_TST_INFO, msg_imprint),
		.field_name = "msg_imprint",
		.item = &TS_MSG_IMPRINT_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(TS_TST_INFO, serial),
		.field_name = "serial",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(TS_TST_INFO, time),
		.field_name = "time",
		.item = &ASN1_GENERALIZEDTIME_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(TS_TST_INFO, accuracy),
		.field_name = "accuracy",
		.item = &TS_ACCURACY_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(TS_TST_INFO, ordering),
		.field_name = "ordering",
		.item = &ASN1_FBOOLEAN_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(TS_TST_INFO, nonce),
		.field_name = "nonce",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(TS_TST_INFO, tsa),
		.field_name = "tsa",
		.item = &GENERAL_NAME_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(TS_TST_INFO, extensions),
		.field_name = "extensions",
		.item = &X509_EXTENSION_it,
	},
};

static const ASN1_ITEM TS_TST_INFO_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = TS_TST_INFO_seq_tt,
	.tcount = sizeof(TS_TST_INFO_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(TS_TST_INFO),
	.sname = "TS_TST_INFO",
};


TS_TST_INFO *
d2i_TS_TST_INFO(TS_TST_INFO **a, const unsigned char **in, long len)
{
	return (TS_TST_INFO *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &TS_TST_INFO_it);
}
LCRYPTO_ALIAS(d2i_TS_TST_INFO);

int
i2d_TS_TST_INFO(const TS_TST_INFO *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &TS_TST_INFO_it);
}
LCRYPTO_ALIAS(i2d_TS_TST_INFO);

TS_TST_INFO *
TS_TST_INFO_new(void)
{
	return (TS_TST_INFO *)ASN1_item_new(&TS_TST_INFO_it);
}
LCRYPTO_ALIAS(TS_TST_INFO_new);

void
TS_TST_INFO_free(TS_TST_INFO *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &TS_TST_INFO_it);
}
LCRYPTO_ALIAS(TS_TST_INFO_free);

TS_TST_INFO *
TS_TST_INFO_dup(TS_TST_INFO *x)
{
	return ASN1_item_dup(&TS_TST_INFO_it, x);
}
LCRYPTO_ALIAS(TS_TST_INFO_dup);

#ifndef OPENSSL_NO_BIO
TS_TST_INFO *
d2i_TS_TST_INFO_bio(BIO *bp, TS_TST_INFO **a)
{
	return ASN1_item_d2i_bio(&TS_TST_INFO_it, bp, a);
}
LCRYPTO_ALIAS(d2i_TS_TST_INFO_bio);

int
i2d_TS_TST_INFO_bio(BIO *bp, TS_TST_INFO *a)
{
	return ASN1_item_i2d_bio(&TS_TST_INFO_it, bp, a);
}
LCRYPTO_ALIAS(i2d_TS_TST_INFO_bio);
#endif

TS_TST_INFO *
d2i_TS_TST_INFO_fp(FILE *fp, TS_TST_INFO **a)
{
	return ASN1_item_d2i_fp(&TS_TST_INFO_it, fp, a);
}
LCRYPTO_ALIAS(d2i_TS_TST_INFO_fp);

int
i2d_TS_TST_INFO_fp(FILE *fp, TS_TST_INFO *a)
{
	return ASN1_item_i2d_fp(&TS_TST_INFO_it, fp, a);
}
LCRYPTO_ALIAS(i2d_TS_TST_INFO_fp);

static const ASN1_TEMPLATE TS_STATUS_INFO_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(TS_STATUS_INFO, status),
		.field_name = "status",
		.item = &ASN1_INTEGER_it,
	},
	{
		.flags = ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(TS_STATUS_INFO, text),
		.field_name = "text",
		.item = &ASN1_UTF8STRING_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(TS_STATUS_INFO, failure_info),
		.field_name = "failure_info",
		.item = &ASN1_BIT_STRING_it,
	},
};

static const ASN1_ITEM TS_STATUS_INFO_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = TS_STATUS_INFO_seq_tt,
	.tcount = sizeof(TS_STATUS_INFO_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(TS_STATUS_INFO),
	.sname = "TS_STATUS_INFO",
};


TS_STATUS_INFO *
d2i_TS_STATUS_INFO(TS_STATUS_INFO **a, const unsigned char **in, long len)
{
	return (TS_STATUS_INFO *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &TS_STATUS_INFO_it);
}
LCRYPTO_ALIAS(d2i_TS_STATUS_INFO);

int
i2d_TS_STATUS_INFO(const TS_STATUS_INFO *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &TS_STATUS_INFO_it);
}
LCRYPTO_ALIAS(i2d_TS_STATUS_INFO);

TS_STATUS_INFO *
TS_STATUS_INFO_new(void)
{
	return (TS_STATUS_INFO *)ASN1_item_new(&TS_STATUS_INFO_it);
}
LCRYPTO_ALIAS(TS_STATUS_INFO_new);

void
TS_STATUS_INFO_free(TS_STATUS_INFO *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &TS_STATUS_INFO_it);
}
LCRYPTO_ALIAS(TS_STATUS_INFO_free);

TS_STATUS_INFO *
TS_STATUS_INFO_dup(TS_STATUS_INFO *x)
{
	return ASN1_item_dup(&TS_STATUS_INFO_it, x);
}
LCRYPTO_ALIAS(TS_STATUS_INFO_dup);

static int
ts_resp_set_tst_info(TS_RESP *a)
{
	long    status;

	status = ASN1_INTEGER_get(a->status_info->status);

	if (a->token) {
		if (status != 0 && status != 1) {
			TSerror(TS_R_TOKEN_PRESENT);
			return 0;
		}
		if (a->tst_info != NULL)
			TS_TST_INFO_free(a->tst_info);
		a->tst_info = PKCS7_to_TS_TST_INFO(a->token);
		if (!a->tst_info) {
			TSerror(TS_R_PKCS7_TO_TS_TST_INFO_FAILED);
			return 0;
		}
	} else if (status == 0 || status == 1) {
		TSerror(TS_R_TOKEN_NOT_PRESENT);
		return 0;
	}

	return 1;
}

static int
ts_resp_cb(int op, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	TS_RESP *ts_resp = (TS_RESP *)*pval;

	if (op == ASN1_OP_NEW_POST) {
		ts_resp->tst_info = NULL;
	} else if (op == ASN1_OP_FREE_POST) {
		if (ts_resp->tst_info != NULL)
			TS_TST_INFO_free(ts_resp->tst_info);
	} else if (op == ASN1_OP_D2I_POST) {
		if (ts_resp_set_tst_info(ts_resp) == 0)
			return 0;
	}
	return 1;
}

static const ASN1_AUX TS_RESP_aux = {
	.app_data = NULL,
	.flags = 0,
	.ref_offset = 0,
	.ref_lock = 0,
	.asn1_cb = ts_resp_cb,
	.enc_offset = 0,
};
static const ASN1_TEMPLATE TS_RESP_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(TS_RESP, status_info),
		.field_name = "status_info",
		.item = &TS_STATUS_INFO_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(TS_RESP, token),
		.field_name = "token",
		.item = &PKCS7_it,
	},
};

static const ASN1_ITEM TS_RESP_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = TS_RESP_seq_tt,
	.tcount = sizeof(TS_RESP_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &TS_RESP_aux,
	.size = sizeof(TS_RESP),
	.sname = "TS_RESP",
};


TS_RESP *
d2i_TS_RESP(TS_RESP **a, const unsigned char **in, long len)
{
	return (TS_RESP *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &TS_RESP_it);
}
LCRYPTO_ALIAS(d2i_TS_RESP);

int
i2d_TS_RESP(const TS_RESP *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &TS_RESP_it);
}
LCRYPTO_ALIAS(i2d_TS_RESP);

TS_RESP *
TS_RESP_new(void)
{
	return (TS_RESP *)ASN1_item_new(&TS_RESP_it);
}
LCRYPTO_ALIAS(TS_RESP_new);

void
TS_RESP_free(TS_RESP *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &TS_RESP_it);
}
LCRYPTO_ALIAS(TS_RESP_free);

TS_RESP *
TS_RESP_dup(TS_RESP *x)
{
	return ASN1_item_dup(&TS_RESP_it, x);
}
LCRYPTO_ALIAS(TS_RESP_dup);

#ifndef OPENSSL_NO_BIO
TS_RESP *
d2i_TS_RESP_bio(BIO *bp, TS_RESP **a)
{
	return ASN1_item_d2i_bio(&TS_RESP_it, bp, a);
}
LCRYPTO_ALIAS(d2i_TS_RESP_bio);

int
i2d_TS_RESP_bio(BIO *bp, TS_RESP *a)
{
	return ASN1_item_i2d_bio(&TS_RESP_it, bp, a);
}
LCRYPTO_ALIAS(i2d_TS_RESP_bio);
#endif

TS_RESP *
d2i_TS_RESP_fp(FILE *fp, TS_RESP **a)
{
	return ASN1_item_d2i_fp(&TS_RESP_it, fp, a);
}
LCRYPTO_ALIAS(d2i_TS_RESP_fp);

int
i2d_TS_RESP_fp(FILE *fp, TS_RESP *a)
{
	return ASN1_item_i2d_fp(&TS_RESP_it, fp, a);
}
LCRYPTO_ALIAS(i2d_TS_RESP_fp);

static const ASN1_TEMPLATE ESS_ISSUER_SERIAL_seq_tt[] = {
	{
		.flags = ASN1_TFLG_SEQUENCE_OF,
		.tag = 0,
		.offset = offsetof(ESS_ISSUER_SERIAL, issuer),
		.field_name = "issuer",
		.item = &GENERAL_NAME_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ESS_ISSUER_SERIAL, serial),
		.field_name = "serial",
		.item = &ASN1_INTEGER_it,
	},
};

static const ASN1_ITEM ESS_ISSUER_SERIAL_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = ESS_ISSUER_SERIAL_seq_tt,
	.tcount = sizeof(ESS_ISSUER_SERIAL_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(ESS_ISSUER_SERIAL),
	.sname = "ESS_ISSUER_SERIAL",
};


ESS_ISSUER_SERIAL *
d2i_ESS_ISSUER_SERIAL(ESS_ISSUER_SERIAL **a, const unsigned char **in, long len)
{
	return (ESS_ISSUER_SERIAL *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ESS_ISSUER_SERIAL_it);
}
LCRYPTO_ALIAS(d2i_ESS_ISSUER_SERIAL);

int
i2d_ESS_ISSUER_SERIAL(const ESS_ISSUER_SERIAL *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ESS_ISSUER_SERIAL_it);
}
LCRYPTO_ALIAS(i2d_ESS_ISSUER_SERIAL);

ESS_ISSUER_SERIAL *
ESS_ISSUER_SERIAL_new(void)
{
	return (ESS_ISSUER_SERIAL *)ASN1_item_new(&ESS_ISSUER_SERIAL_it);
}
LCRYPTO_ALIAS(ESS_ISSUER_SERIAL_new);

void
ESS_ISSUER_SERIAL_free(ESS_ISSUER_SERIAL *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ESS_ISSUER_SERIAL_it);
}
LCRYPTO_ALIAS(ESS_ISSUER_SERIAL_free);

ESS_ISSUER_SERIAL *
ESS_ISSUER_SERIAL_dup(ESS_ISSUER_SERIAL *x)
{
	return ASN1_item_dup(&ESS_ISSUER_SERIAL_it, x);
}
LCRYPTO_ALIAS(ESS_ISSUER_SERIAL_dup);

static const ASN1_TEMPLATE ESS_CERT_ID_seq_tt[] = {
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ESS_CERT_ID, hash),
		.field_name = "hash",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(ESS_CERT_ID, issuer_serial),
		.field_name = "issuer_serial",
		.item = &ESS_ISSUER_SERIAL_it,
	},
};

static const ASN1_ITEM ESS_CERT_ID_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = ESS_CERT_ID_seq_tt,
	.tcount = sizeof(ESS_CERT_ID_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(ESS_CERT_ID),
	.sname = "ESS_CERT_ID",
};


ESS_CERT_ID *
d2i_ESS_CERT_ID(ESS_CERT_ID **a, const unsigned char **in, long len)
{
	return (ESS_CERT_ID *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ESS_CERT_ID_it);
}
LCRYPTO_ALIAS(d2i_ESS_CERT_ID);

int
i2d_ESS_CERT_ID(const ESS_CERT_ID *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ESS_CERT_ID_it);
}
LCRYPTO_ALIAS(i2d_ESS_CERT_ID);

ESS_CERT_ID *
ESS_CERT_ID_new(void)
{
	return (ESS_CERT_ID *)ASN1_item_new(&ESS_CERT_ID_it);
}
LCRYPTO_ALIAS(ESS_CERT_ID_new);

void
ESS_CERT_ID_free(ESS_CERT_ID *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ESS_CERT_ID_it);
}
LCRYPTO_ALIAS(ESS_CERT_ID_free);

ESS_CERT_ID *
ESS_CERT_ID_dup(ESS_CERT_ID *x)
{
	return ASN1_item_dup(&ESS_CERT_ID_it, x);
}
LCRYPTO_ALIAS(ESS_CERT_ID_dup);

static const ASN1_TEMPLATE ESS_SIGNING_CERT_seq_tt[] = {
	{
		.flags = ASN1_TFLG_SEQUENCE_OF,
		.tag = 0,
		.offset = offsetof(ESS_SIGNING_CERT, cert_ids),
		.field_name = "cert_ids",
		.item = &ESS_CERT_ID_it,
	},
	{
		.flags = ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(ESS_SIGNING_CERT, policy_info),
		.field_name = "policy_info",
		.item = &POLICYINFO_it,
	},
};

static const ASN1_ITEM ESS_SIGNING_CERT_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = ESS_SIGNING_CERT_seq_tt,
	.tcount = sizeof(ESS_SIGNING_CERT_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(ESS_SIGNING_CERT),
	.sname = "ESS_SIGNING_CERT",
};


ESS_SIGNING_CERT *
d2i_ESS_SIGNING_CERT(ESS_SIGNING_CERT **a, const unsigned char **in, long len)
{
	return (ESS_SIGNING_CERT *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ESS_SIGNING_CERT_it);
}
LCRYPTO_ALIAS(d2i_ESS_SIGNING_CERT);

int
i2d_ESS_SIGNING_CERT(const ESS_SIGNING_CERT *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ESS_SIGNING_CERT_it);
}
LCRYPTO_ALIAS(i2d_ESS_SIGNING_CERT);

ESS_SIGNING_CERT *
ESS_SIGNING_CERT_new(void)
{
	return (ESS_SIGNING_CERT *)ASN1_item_new(&ESS_SIGNING_CERT_it);
}
LCRYPTO_ALIAS(ESS_SIGNING_CERT_new);

void
ESS_SIGNING_CERT_free(ESS_SIGNING_CERT *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ESS_SIGNING_CERT_it);
}
LCRYPTO_ALIAS(ESS_SIGNING_CERT_free);

ESS_SIGNING_CERT *
ESS_SIGNING_CERT_dup(ESS_SIGNING_CERT *x)
{
	return ASN1_item_dup(&ESS_SIGNING_CERT_it, x);
}
LCRYPTO_ALIAS(ESS_SIGNING_CERT_dup);

static const ASN1_TEMPLATE ESS_CERT_ID_V2_seq_tt[] = {
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(ESS_CERT_ID_V2, hash_alg),
		.field_name = "hash_alg",
		.item = &X509_ALGOR_it,
	},
	{
		.flags = 0,
		.tag = 0,
		.offset = offsetof(ESS_CERT_ID_V2, hash),
		.field_name = "hash",
		.item = &ASN1_OCTET_STRING_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(ESS_CERT_ID_V2, issuer_serial),
		.field_name = "issuer_serial",
		.item = &ESS_ISSUER_SERIAL_it,
	},
};

static const ASN1_ITEM ESS_CERT_ID_V2_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = ESS_CERT_ID_V2_seq_tt,
	.tcount = sizeof(ESS_CERT_ID_V2_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(ESS_CERT_ID_V2),
	.sname = "ESS_CERT_ID_V2",
};

ESS_CERT_ID_V2 *
d2i_ESS_CERT_ID_V2(ESS_CERT_ID_V2 **a, const unsigned char **in, long len)
{
	return (ESS_CERT_ID_V2 *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ESS_CERT_ID_V2_it);
}

int
i2d_ESS_CERT_ID_V2(const ESS_CERT_ID_V2 *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ESS_CERT_ID_V2_it);
}

ESS_CERT_ID_V2 *
ESS_CERT_ID_V2_new(void)
{
	return (ESS_CERT_ID_V2 *)ASN1_item_new(&ESS_CERT_ID_V2_it);
}

void
ESS_CERT_ID_V2_free(ESS_CERT_ID_V2 *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ESS_CERT_ID_V2_it);
}

ESS_CERT_ID_V2 *
ESS_CERT_ID_V2_dup(ESS_CERT_ID_V2 *x)
{
	return ASN1_item_dup(&ESS_CERT_ID_V2_it, x);
}

static const ASN1_TEMPLATE ESS_SIGNING_CERT_V2_seq_tt[] = {
	{
		.flags = ASN1_TFLG_SEQUENCE_OF,
		.tag = 0,
		.offset = offsetof(ESS_SIGNING_CERT_V2, cert_ids),
		.field_name = "cert_ids",
		.item = &ESS_CERT_ID_V2_it,
	},
	{
		.flags = ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.tag = 0,
		.offset = offsetof(ESS_SIGNING_CERT_V2, policy_info),
		.field_name = "policy_info",
		.item = &POLICYINFO_it,
	},
};

static const ASN1_ITEM ESS_SIGNING_CERT_V2_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = ESS_SIGNING_CERT_V2_seq_tt,
	.tcount = sizeof(ESS_SIGNING_CERT_V2_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = NULL,
	.size = sizeof(ESS_SIGNING_CERT_V2),
	.sname = "ESS_SIGNING_CERT_V2",
};

ESS_SIGNING_CERT_V2 *
d2i_ESS_SIGNING_CERT_V2(ESS_SIGNING_CERT_V2 **a, const unsigned char **in, long len)
{
	return (ESS_SIGNING_CERT_V2 *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &ESS_SIGNING_CERT_V2_it);
}

int
i2d_ESS_SIGNING_CERT_V2(const ESS_SIGNING_CERT_V2 *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &ESS_SIGNING_CERT_V2_it);
}

ESS_SIGNING_CERT_V2 *
ESS_SIGNING_CERT_V2_new(void)
{
	return (ESS_SIGNING_CERT_V2 *)ASN1_item_new(&ESS_SIGNING_CERT_V2_it);
}

void
ESS_SIGNING_CERT_V2_free(ESS_SIGNING_CERT_V2 *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &ESS_SIGNING_CERT_V2_it);
}

ESS_SIGNING_CERT_V2 *
ESS_SIGNING_CERT_V2_dup(ESS_SIGNING_CERT_V2 *x)
{
	return ASN1_item_dup(&ESS_SIGNING_CERT_V2_it, x);
}

/* Getting encapsulated TS_TST_INFO object from PKCS7. */
TS_TST_INFO *
PKCS7_to_TS_TST_INFO(PKCS7 *token)
{
	PKCS7_SIGNED *pkcs7_signed;
	PKCS7 *enveloped;
	ASN1_TYPE *tst_info_wrapper;
	ASN1_OCTET_STRING *tst_info_der;
	const unsigned char *p;

	if (!PKCS7_type_is_signed(token)) {
		TSerror(TS_R_BAD_PKCS7_TYPE);
		return NULL;
	}

	/* Content must be present. */
	if (PKCS7_get_detached(token)) {
		TSerror(TS_R_DETACHED_CONTENT);
		return NULL;
	}

	/* We have a signed data with content. */
	pkcs7_signed = token->d.sign;
	enveloped = pkcs7_signed->contents;
	if (OBJ_obj2nid(enveloped->type) != NID_id_smime_ct_TSTInfo) {
		TSerror(TS_R_BAD_PKCS7_TYPE);
		return NULL;
	}

	/* We have a DER encoded TST_INFO as the signed data. */
	tst_info_wrapper = enveloped->d.other;
	if (tst_info_wrapper->type != V_ASN1_OCTET_STRING) {
		TSerror(TS_R_BAD_TYPE);
		return NULL;
	}

	/* We have the correct ASN1_OCTET_STRING type. */
	tst_info_der = tst_info_wrapper->value.octet_string;
	/* At last, decode the TST_INFO. */
	p = tst_info_der->data;
	return d2i_TS_TST_INFO(NULL, &p, tst_info_der->length);
}
LCRYPTO_ALIAS(PKCS7_to_TS_TST_INFO);
