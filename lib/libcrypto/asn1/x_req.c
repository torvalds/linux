/* $OpenBSD: x_req.c,v 1.23 2024/07/08 14:48:49 beck Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */

#include <stdio.h>

#include <openssl/asn1t.h>
#include <openssl/x509.h>

#include "x509_local.h"

/* X509_REQ_INFO is handled in an unusual way to get round
 * invalid encodings. Some broken certificate requests don't
 * encode the attributes field if it is empty. This is in
 * violation of PKCS#10 but we need to tolerate it. We do
 * this by making the attributes field OPTIONAL then using
 * the callback to initialise it to an empty STACK.
 *
 * This means that the field will be correctly encoded unless
 * we NULL out the field.
 *
 * As a result we no longer need the req_kludge field because
 * the information is now contained in the attributes field:
 * 1. If it is NULL then it's the invalid omission.
 * 2. If it is empty it is the correct encoding.
 * 3. If it is not empty then some attributes are present.
 *
 */

static int
rinf_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	X509_REQ_INFO *rinf = (X509_REQ_INFO *)*pval;

	if (operation == ASN1_OP_NEW_POST) {
		rinf->attributes = sk_X509_ATTRIBUTE_new_null();
		if (!rinf->attributes)
			return 0;
	}
	return 1;
}

static const ASN1_AUX X509_REQ_INFO_aux = {
	.flags = ASN1_AFLG_ENCODING,
	.asn1_cb = rinf_cb,
	.enc_offset = offsetof(X509_REQ_INFO, enc),
};
static const ASN1_TEMPLATE X509_REQ_INFO_seq_tt[] = {
	{
		.offset = offsetof(X509_REQ_INFO, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.offset = offsetof(X509_REQ_INFO, subject),
		.field_name = "subject",
		.item = &X509_NAME_it,
	},
	{
		.offset = offsetof(X509_REQ_INFO, pubkey),
		.field_name = "pubkey",
		.item = &X509_PUBKEY_it,
	},
	/* This isn't really OPTIONAL but it gets round invalid
	 * encodings
	 */
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_SET_OF | ASN1_TFLG_OPTIONAL,
		.offset = offsetof(X509_REQ_INFO, attributes),
		.field_name = "attributes",
		.item = &X509_ATTRIBUTE_it,
	},
};

const ASN1_ITEM X509_REQ_INFO_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X509_REQ_INFO_seq_tt,
	.tcount = sizeof(X509_REQ_INFO_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &X509_REQ_INFO_aux,
	.size = sizeof(X509_REQ_INFO),
	.sname = "X509_REQ_INFO",
};
LCRYPTO_ALIAS(X509_REQ_INFO_it);


X509_REQ_INFO *
d2i_X509_REQ_INFO(X509_REQ_INFO **a, const unsigned char **in, long len)
{
	return (X509_REQ_INFO *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &X509_REQ_INFO_it);
}
LCRYPTO_ALIAS(d2i_X509_REQ_INFO);

int
i2d_X509_REQ_INFO(X509_REQ_INFO *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &X509_REQ_INFO_it);
}
LCRYPTO_ALIAS(i2d_X509_REQ_INFO);

X509_REQ_INFO *
X509_REQ_INFO_new(void)
{
	return (X509_REQ_INFO *)ASN1_item_new(&X509_REQ_INFO_it);
}
LCRYPTO_ALIAS(X509_REQ_INFO_new);

void
X509_REQ_INFO_free(X509_REQ_INFO *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &X509_REQ_INFO_it);
}
LCRYPTO_ALIAS(X509_REQ_INFO_free);

static const ASN1_AUX X509_REQ_aux = {
	.app_data = NULL,
	.flags = ASN1_AFLG_REFCOUNT,
	.ref_offset = offsetof(X509_REQ, references),
	.ref_lock = CRYPTO_LOCK_X509_REQ,
};
static const ASN1_TEMPLATE X509_REQ_seq_tt[] = {
	{
		.offset = offsetof(X509_REQ, req_info),
		.field_name = "req_info",
		.item = &X509_REQ_INFO_it,
	},
	{
		.offset = offsetof(X509_REQ, sig_alg),
		.field_name = "sig_alg",
		.item = &X509_ALGOR_it,
	},
	{
		.offset = offsetof(X509_REQ, signature),
		.field_name = "signature",
		.item = &ASN1_BIT_STRING_it,
	},
};

const ASN1_ITEM X509_REQ_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X509_REQ_seq_tt,
	.tcount = sizeof(X509_REQ_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &X509_REQ_aux,
	.size = sizeof(X509_REQ),
	.sname = "X509_REQ",
};
LCRYPTO_ALIAS(X509_REQ_it);


X509_REQ *
d2i_X509_REQ(X509_REQ **a, const unsigned char **in, long len)
{
	return (X509_REQ *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &X509_REQ_it);
}
LCRYPTO_ALIAS(d2i_X509_REQ);

int
i2d_X509_REQ(X509_REQ *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &X509_REQ_it);
}
LCRYPTO_ALIAS(i2d_X509_REQ);

X509_REQ *
X509_REQ_new(void)
{
	return (X509_REQ *)ASN1_item_new(&X509_REQ_it);
}
LCRYPTO_ALIAS(X509_REQ_new);

void
X509_REQ_free(X509_REQ *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &X509_REQ_it);
}
LCRYPTO_ALIAS(X509_REQ_free);

X509_REQ *
X509_REQ_dup(X509_REQ *x)
{
	return ASN1_item_dup(&X509_REQ_it, x);
}
LCRYPTO_ALIAS(X509_REQ_dup);

int
X509_REQ_get_signature_nid(const X509_REQ *req)
{
	return OBJ_obj2nid(req->sig_alg->algorithm);
}
LCRYPTO_ALIAS(X509_REQ_get_signature_nid);

void
X509_REQ_get0_signature(const X509_REQ *req, const ASN1_BIT_STRING **psig,
    const X509_ALGOR **palg)
{
	if (psig != NULL)
		*psig = req->signature;
	if (palg != NULL)
		*palg = req->sig_alg;
}
LCRYPTO_ALIAS(X509_REQ_get0_signature);
