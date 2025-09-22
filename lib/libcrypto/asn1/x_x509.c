/* $OpenBSD: x_x509.c,v 1.41 2025/02/21 05:44:28 tb Exp $ */
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

#include <openssl/opensslconf.h>

#include <openssl/asn1t.h>
#include <openssl/evp.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "x509_local.h"

static const ASN1_AUX X509_CINF_aux = {
	.flags = ASN1_AFLG_ENCODING,
	.enc_offset = offsetof(X509_CINF, enc),
};
static const ASN1_TEMPLATE X509_CINF_seq_tt[] = {
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_OPTIONAL,
		.offset = offsetof(X509_CINF, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.offset = offsetof(X509_CINF, serialNumber),
		.field_name = "serialNumber",
		.item = &ASN1_INTEGER_it,
	},
	{
		.offset = offsetof(X509_CINF, signature),
		.field_name = "signature",
		.item = &X509_ALGOR_it,
	},
	{
		.offset = offsetof(X509_CINF, issuer),
		.field_name = "issuer",
		.item = &X509_NAME_it,
	},
	{
		.offset = offsetof(X509_CINF, validity),
		.field_name = "validity",
		.item = &X509_VAL_it,
	},
	{
		.offset = offsetof(X509_CINF, subject),
		.field_name = "subject",
		.item = &X509_NAME_it,
	},
	{
		.offset = offsetof(X509_CINF, key),
		.field_name = "key",
		.item = &X509_PUBKEY_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 1,
		.offset = offsetof(X509_CINF, issuerUID),
		.field_name = "issuerUID",
		.item = &ASN1_BIT_STRING_it,
	},
	{
		.flags = ASN1_TFLG_IMPLICIT | ASN1_TFLG_OPTIONAL,
		.tag = 2,
		.offset = offsetof(X509_CINF, subjectUID),
		.field_name = "subjectUID",
		.item = &ASN1_BIT_STRING_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_SEQUENCE_OF |
		    ASN1_TFLG_OPTIONAL,
		.tag = 3,
		.offset = offsetof(X509_CINF, extensions),
		.field_name = "extensions",
		.item = &X509_EXTENSION_it,
	},
};

const ASN1_ITEM X509_CINF_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X509_CINF_seq_tt,
	.tcount = sizeof(X509_CINF_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &X509_CINF_aux,
	.size = sizeof(X509_CINF),
	.sname = "X509_CINF",
};
LCRYPTO_ALIAS(X509_CINF_it);


X509_CINF *
d2i_X509_CINF(X509_CINF **a, const unsigned char **in, long len)
{
	return (X509_CINF *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &X509_CINF_it);
}
LCRYPTO_ALIAS(d2i_X509_CINF);

int
i2d_X509_CINF(X509_CINF *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &X509_CINF_it);
}
LCRYPTO_ALIAS(i2d_X509_CINF);

X509_CINF *
X509_CINF_new(void)
{
	return (X509_CINF *)ASN1_item_new(&X509_CINF_it);
}
LCRYPTO_ALIAS(X509_CINF_new);

void
X509_CINF_free(X509_CINF *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &X509_CINF_it);
}
LCRYPTO_ALIAS(X509_CINF_free);
/* X509 top level structure needs a bit of customisation */

static int
x509_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	X509 *ret = (X509 *)*pval;

	switch (operation) {

	case ASN1_OP_NEW_POST:
		ret->ex_flags = 0;
		ret->ex_pathlen = -1;
		ret->skid = NULL;
		ret->akid = NULL;
		ret->aux = NULL;
		ret->crldp = NULL;
#ifndef OPENSSL_NO_RFC3779
		ret->rfc3779_addr = NULL;
		ret->rfc3779_asid = NULL;
#endif
		CRYPTO_new_ex_data(CRYPTO_EX_INDEX_X509, ret, &ret->ex_data);
		break;

	case ASN1_OP_FREE_POST:
		CRYPTO_free_ex_data(CRYPTO_EX_INDEX_X509, ret, &ret->ex_data);
		X509_CERT_AUX_free(ret->aux);
		ASN1_OCTET_STRING_free(ret->skid);
		AUTHORITY_KEYID_free(ret->akid);
		CRL_DIST_POINTS_free(ret->crldp);
		GENERAL_NAMES_free(ret->altname);
		NAME_CONSTRAINTS_free(ret->nc);
#ifndef OPENSSL_NO_RFC3779
		sk_IPAddressFamily_pop_free(ret->rfc3779_addr, IPAddressFamily_free);
		ASIdentifiers_free(ret->rfc3779_asid);
#endif
		break;
	}

	return 1;
}
LCRYPTO_ALIAS(d2i_X509_CINF);

static const ASN1_AUX X509_aux = {
	.app_data = NULL,
	.flags = ASN1_AFLG_REFCOUNT,
	.ref_offset = offsetof(X509, references),
	.ref_lock = CRYPTO_LOCK_X509,
	.asn1_cb = x509_cb,
};
static const ASN1_TEMPLATE X509_seq_tt[] = {
	{
		.offset = offsetof(X509, cert_info),
		.field_name = "cert_info",
		.item = &X509_CINF_it,
	},
	{
		.offset = offsetof(X509, sig_alg),
		.field_name = "sig_alg",
		.item = &X509_ALGOR_it,
	},
	{
		.offset = offsetof(X509, signature),
		.field_name = "signature",
		.item = &ASN1_BIT_STRING_it,
	},
};

const ASN1_ITEM X509_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X509_seq_tt,
	.tcount = sizeof(X509_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &X509_aux,
	.size = sizeof(X509),
	.sname = "X509",
};
LCRYPTO_ALIAS(X509_it);


X509 *
d2i_X509(X509 **a, const unsigned char **in, long len)
{
	return (X509 *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &X509_it);
}
LCRYPTO_ALIAS(d2i_X509);

int
i2d_X509(X509 *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &X509_it);
}
LCRYPTO_ALIAS(i2d_X509);

X509 *
X509_new(void)
{
	return (X509 *)ASN1_item_new(&X509_it);
}
LCRYPTO_ALIAS(X509_new);

void
X509_free(X509 *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &X509_it);
}
LCRYPTO_ALIAS(X509_free);

X509 *
X509_dup(X509 *x)
{
	return ASN1_item_dup(&X509_it, x);
}
LCRYPTO_ALIAS(X509_dup);

int
X509_get_ex_new_index(long argl, void *argp, CRYPTO_EX_new *new_func,
    CRYPTO_EX_dup *dup_func, CRYPTO_EX_free *free_func)
{
	return CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_X509, argl, argp,
	    new_func, dup_func, free_func);
}
LCRYPTO_ALIAS(X509_get_ex_new_index);

int
X509_set_ex_data(X509 *r, int idx, void *arg)
{
	return (CRYPTO_set_ex_data(&r->ex_data, idx, arg));
}
LCRYPTO_ALIAS(X509_set_ex_data);

void *
X509_get_ex_data(X509 *r, int idx)
{
	return (CRYPTO_get_ex_data(&r->ex_data, idx));
}
LCRYPTO_ALIAS(X509_get_ex_data);

/* X509_AUX ASN1 routines. X509_AUX is the name given to
 * a certificate with extra info tagged on the end. Since these
 * functions set how a certificate is trusted they should only
 * be used when the certificate comes from a reliable source
 * such as local storage.
 *
 */

X509 *
d2i_X509_AUX(X509 **a, const unsigned char **pp, long length)
{
	const unsigned char *q;
	X509 *ret;

	/* Save start position */
	q = *pp;
	ret = d2i_X509(NULL, pp, length);
	/* If certificate unreadable then forget it */
	if (!ret)
		return NULL;
	/* update length */
	length -= *pp - q;
	if (length > 0) {
		if (!d2i_X509_CERT_AUX(&ret->aux, pp, length))
			goto err;
	}
	if (a != NULL) {
		X509_free(*a);
		*a = ret;
	}
	return ret;

 err:
	X509_free(ret);
	return NULL;
}
LCRYPTO_ALIAS(d2i_X509_AUX);

int
i2d_X509_AUX(X509 *a, unsigned char **pp)
{
	int length;

	length = i2d_X509(a, pp);
	if (a)
		length += i2d_X509_CERT_AUX(a->aux, pp);
	return length;
}
LCRYPTO_ALIAS(i2d_X509_AUX);

int
i2d_re_X509_tbs(X509 *x, unsigned char **pp)
{
	x->cert_info->enc.modified = 1;
	return i2d_X509_CINF(x->cert_info, pp);
}
LCRYPTO_ALIAS(i2d_re_X509_tbs);

void
X509_get0_signature(const ASN1_BIT_STRING **psig, const X509_ALGOR **palg,
    const X509 *x)
{
	if (psig != NULL)
		*psig = x->signature;
	if (palg != NULL)
		*palg = x->sig_alg;
}
LCRYPTO_ALIAS(X509_get0_signature);

int
X509_get_signature_nid(const X509 *x)
{
	return OBJ_obj2nid(x->sig_alg->algorithm);
}
LCRYPTO_ALIAS(X509_get_signature_nid);
