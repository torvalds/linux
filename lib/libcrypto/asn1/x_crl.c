/* $OpenBSD: x_crl.c,v 1.51 2025/08/19 21:54:11 tb Exp $ */
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
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "asn1_local.h"
#include "err_local.h"
#include "x509_local.h"

static void setup_idp(X509_CRL *crl, ISSUING_DIST_POINT *idp);

static const ASN1_TEMPLATE X509_REVOKED_seq_tt[] = {
	{
		.offset = offsetof(X509_REVOKED, serialNumber),
		.field_name = "serialNumber",
		.item = &ASN1_INTEGER_it,
	},
	{
		.offset = offsetof(X509_REVOKED, revocationDate),
		.field_name = "revocationDate",
		.item = &ASN1_TIME_it,
	},
	{
		.flags = ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.offset = offsetof(X509_REVOKED, extensions),
		.field_name = "extensions",
		.item = &X509_EXTENSION_it,
	},
};

const ASN1_ITEM X509_REVOKED_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X509_REVOKED_seq_tt,
	.tcount = sizeof(X509_REVOKED_seq_tt) / sizeof(ASN1_TEMPLATE),
	.size = sizeof(X509_REVOKED),
	.sname = "X509_REVOKED",
};
LCRYPTO_ALIAS(X509_REVOKED_it);

static int
X509_REVOKED_cmp(const X509_REVOKED * const *a, const X509_REVOKED * const *b)
{
	return ASN1_INTEGER_cmp((*a)->serialNumber, (*b)->serialNumber);
}

/*
 * The X509_CRL_INFO structure needs a bit of customisation.
 * Since we cache the original encoding, the signature won't be affected by
 * reordering of the revoked field.
 */
static int
crl_info_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	X509_CRL_INFO *a = (X509_CRL_INFO *)*pval;

	if (!a || !a->revoked)
		return 1;
	switch (operation) {
		/* Just set cmp function here. We don't sort because that
		 * would affect the output of X509_CRL_print().
		 */
	case ASN1_OP_D2I_POST:
		(void)sk_X509_REVOKED_set_cmp_func(a->revoked, X509_REVOKED_cmp);
		break;
	}
	return 1;
}


static const ASN1_AUX X509_CRL_INFO_aux = {
	.flags = ASN1_AFLG_ENCODING,
	.asn1_cb = crl_info_cb,
	.enc_offset = offsetof(X509_CRL_INFO, enc),
};
static const ASN1_TEMPLATE X509_CRL_INFO_seq_tt[] = {
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.offset = offsetof(X509_CRL_INFO, version),
		.field_name = "version",
		.item = &ASN1_INTEGER_it,
	},
	{
		.offset = offsetof(X509_CRL_INFO, sig_alg),
		.field_name = "sig_alg",
		.item = &X509_ALGOR_it,
	},
	{
		.offset = offsetof(X509_CRL_INFO, issuer),
		.field_name = "issuer",
		.item = &X509_NAME_it,
	},
	{
		.offset = offsetof(X509_CRL_INFO, lastUpdate),
		.field_name = "lastUpdate",
		.item = &ASN1_TIME_it,
	},
	{
		.flags = ASN1_TFLG_OPTIONAL,
		.offset = offsetof(X509_CRL_INFO, nextUpdate),
		.field_name = "nextUpdate",
		.item = &ASN1_TIME_it,
	},
	{
		.flags = ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.offset = offsetof(X509_CRL_INFO, revoked),
		.field_name = "revoked",
		.item = &X509_REVOKED_it,
	},
	{
		.flags = ASN1_TFLG_EXPLICIT | ASN1_TFLG_SEQUENCE_OF | ASN1_TFLG_OPTIONAL,
		.offset = offsetof(X509_CRL_INFO, extensions),
		.field_name = "extensions",
		.item = &X509_EXTENSION_it,
	},
};

const ASN1_ITEM X509_CRL_INFO_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X509_CRL_INFO_seq_tt,
	.tcount = sizeof(X509_CRL_INFO_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &X509_CRL_INFO_aux,
	.size = sizeof(X509_CRL_INFO),
	.sname = "X509_CRL_INFO",
};
LCRYPTO_ALIAS(X509_CRL_INFO_it);

/* Set CRL entry issuer according to CRL certificate issuer extension.
 * Check for unhandled critical CRL entry extensions.
 */

static int
crl_set_issuers(X509_CRL *crl)
{
	int i, j;
	GENERAL_NAMES *gens, *gtmp;
	STACK_OF(X509_REVOKED) *revoked;

	revoked = X509_CRL_get_REVOKED(crl);

	gens = NULL;
	for (i = 0; i < sk_X509_REVOKED_num(revoked); i++) {
		X509_REVOKED *rev = sk_X509_REVOKED_value(revoked, i);
		STACK_OF(X509_EXTENSION) *exts;
		ASN1_ENUMERATED *reason;
		X509_EXTENSION *ext;
		gtmp = X509_REVOKED_get_ext_d2i(rev, NID_certificate_issuer,
		    &j, NULL);
		if (!gtmp && (j != -1)) {
			crl->flags |= EXFLAG_INVALID;
			return 1;
		}

		if (gtmp) {
			gens = gtmp;
			if (!crl->issuers) {
				crl->issuers = sk_GENERAL_NAMES_new_null();
				if (!crl->issuers)
					return 0;
			}
			if (!sk_GENERAL_NAMES_push(crl->issuers, gtmp))
				return 0;
		}
		rev->issuer = gens;

		reason = X509_REVOKED_get_ext_d2i(rev, NID_crl_reason,
		    &j, NULL);
		if (!reason && (j != -1)) {
			crl->flags |= EXFLAG_INVALID;
			return 1;
		}

		if (reason) {
			rev->reason = ASN1_ENUMERATED_get(reason);
			ASN1_ENUMERATED_free(reason);
		} else
			rev->reason = CRL_REASON_NONE;

		/* Check for critical CRL entry extensions */

		exts = rev->extensions;

		for (j = 0; j < sk_X509_EXTENSION_num(exts); j++) {
			ext = sk_X509_EXTENSION_value(exts, j);
			if (ext->critical > 0) {
				if (OBJ_obj2nid(ext->object) ==
				    NID_certificate_issuer)
					continue;
				crl->flags |= EXFLAG_CRITICAL;
				break;
			}
		}
	}

	return 1;
}

/* The X509_CRL structure needs a bit of customisation. Cache some extensions
 * and hash of the whole CRL.
 */
static int
crl_cb(int operation, ASN1_VALUE **pval, const ASN1_ITEM *it, void *exarg)
{
	X509_CRL *crl = (X509_CRL *)*pval;
	STACK_OF(X509_EXTENSION) *exts;
	X509_EXTENSION *ext;
	int idx;
	int rc = 1;

	switch (operation) {
	case ASN1_OP_NEW_POST:
		crl->idp = NULL;
		crl->akid = NULL;
		crl->flags = 0;
		crl->idp_flags = 0;
		crl->idp_reasons = CRLDP_ALL_REASONS;
		crl->issuers = NULL;
		crl->crl_number = NULL;
		crl->base_crl_number = NULL;
		break;

	case ASN1_OP_D2I_POST:
		X509_CRL_digest(crl, X509_CRL_HASH_EVP, crl->hash, NULL);
		crl->idp = X509_CRL_get_ext_d2i(crl,
		    NID_issuing_distribution_point, NULL, NULL);
		if (crl->idp)
			setup_idp(crl, crl->idp);

		crl->akid = X509_CRL_get_ext_d2i(crl,
		    NID_authority_key_identifier, NULL, NULL);

		crl->crl_number = X509_CRL_get_ext_d2i(crl,
		    NID_crl_number, NULL, NULL);

		crl->base_crl_number = X509_CRL_get_ext_d2i(crl,
		    NID_delta_crl, NULL, NULL);
		/* Delta CRLs must have CRL number */
		if (crl->base_crl_number && !crl->crl_number)
			crl->flags |= EXFLAG_INVALID;

		/* See if we have any unhandled critical CRL extensions and
		 * indicate this in a flag. We only currently handle IDP,
		 * AKID and deltas, so anything else critical sets the flag.
		 *
		 * This code accesses the X509_CRL structure directly:
		 * applications shouldn't do this.
		 */

		exts = crl->crl->extensions;

		for (idx = 0; idx < sk_X509_EXTENSION_num(exts); idx++) {
			int nid;
			ext = sk_X509_EXTENSION_value(exts, idx);
			nid = OBJ_obj2nid(ext->object);
			if (nid == NID_freshest_crl)
				crl->flags |= EXFLAG_FRESHEST;
			if (ext->critical > 0) {
				/* We handle IDP, AKID and deltas */
				if (nid == NID_issuing_distribution_point ||
				    nid == NID_authority_key_identifier ||
				    nid == NID_delta_crl)
					break;
				crl->flags |= EXFLAG_CRITICAL;
				break;
			}
		}

		if (!crl_set_issuers(crl))
			return 0;
		break;

	case ASN1_OP_FREE_POST:
		AUTHORITY_KEYID_free(crl->akid);
		ISSUING_DIST_POINT_free(crl->idp);
		ASN1_INTEGER_free(crl->crl_number);
		ASN1_INTEGER_free(crl->base_crl_number);
		sk_GENERAL_NAMES_pop_free(crl->issuers, GENERAL_NAMES_free);
		break;
	}
	return rc;
}

/* Convert IDP into a more convenient form */

static void
setup_idp(X509_CRL *crl, ISSUING_DIST_POINT *idp)
{
	int idp_only = 0;

	/* Set various flags according to IDP */
	crl->idp_flags |= IDP_PRESENT;
	if (idp->onlyuser > 0) {
		idp_only++;
		crl->idp_flags |= IDP_ONLYUSER;
	}
	if (idp->onlyCA > 0) {
		idp_only++;
		crl->idp_flags |= IDP_ONLYCA;
	}
	if (idp->onlyattr > 0) {
		idp_only++;
		crl->idp_flags |= IDP_ONLYATTR;
	}

	if (idp_only > 1)
		crl->idp_flags |= IDP_INVALID;

	if (idp->indirectCRL > 0)
		crl->idp_flags |= IDP_INDIRECT;

	if (idp->onlysomereasons) {
		crl->idp_flags |= IDP_REASONS;
		if (idp->onlysomereasons->length > 0)
			crl->idp_reasons = idp->onlysomereasons->data[0];
		if (idp->onlysomereasons->length > 1)
			crl->idp_reasons |=
			    (idp->onlysomereasons->data[1] << 8);
		crl->idp_reasons &= CRLDP_ALL_REASONS;
	}

	DIST_POINT_set_dpname(idp->distpoint, X509_CRL_get_issuer(crl));
}

static const ASN1_AUX X509_CRL_aux = {
	.app_data = NULL,
	.flags = ASN1_AFLG_REFCOUNT,
	.ref_offset = offsetof(X509_CRL, references),
	.ref_lock = CRYPTO_LOCK_X509_CRL,
	.asn1_cb = crl_cb,
};
static const ASN1_TEMPLATE X509_CRL_seq_tt[] = {
	{
		.offset = offsetof(X509_CRL, crl),
		.field_name = "crl",
		.item = &X509_CRL_INFO_it,
	},
	{
		.offset = offsetof(X509_CRL, sig_alg),
		.field_name = "sig_alg",
		.item = &X509_ALGOR_it,
	},
	{
		.offset = offsetof(X509_CRL, signature),
		.field_name = "signature",
		.item = &ASN1_BIT_STRING_it,
	},
};

const ASN1_ITEM X509_CRL_it = {
	.itype = ASN1_ITYPE_SEQUENCE,
	.utype = V_ASN1_SEQUENCE,
	.templates = X509_CRL_seq_tt,
	.tcount = sizeof(X509_CRL_seq_tt) / sizeof(ASN1_TEMPLATE),
	.funcs = &X509_CRL_aux,
	.size = sizeof(X509_CRL),
	.sname = "X509_CRL",
};
LCRYPTO_ALIAS(X509_CRL_it);


X509_REVOKED *
d2i_X509_REVOKED(X509_REVOKED **a, const unsigned char **in, long len)
{
	return (X509_REVOKED *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &X509_REVOKED_it);
}
LCRYPTO_ALIAS(d2i_X509_REVOKED);

int
i2d_X509_REVOKED(X509_REVOKED *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &X509_REVOKED_it);
}
LCRYPTO_ALIAS(i2d_X509_REVOKED);

X509_REVOKED *
X509_REVOKED_new(void)
{
	return (X509_REVOKED *)ASN1_item_new(&X509_REVOKED_it);
}
LCRYPTO_ALIAS(X509_REVOKED_new);

void
X509_REVOKED_free(X509_REVOKED *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &X509_REVOKED_it);
}
LCRYPTO_ALIAS(X509_REVOKED_free);

X509_REVOKED *
X509_REVOKED_dup(X509_REVOKED *a)
{
	return ASN1_item_dup(&X509_REVOKED_it, a);
}
LCRYPTO_ALIAS(X509_REVOKED_dup);

X509_CRL_INFO *
d2i_X509_CRL_INFO(X509_CRL_INFO **a, const unsigned char **in, long len)
{
	return (X509_CRL_INFO *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &X509_CRL_INFO_it);
}
LCRYPTO_ALIAS(d2i_X509_CRL_INFO);

int
i2d_X509_CRL_INFO(X509_CRL_INFO *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &X509_CRL_INFO_it);
}
LCRYPTO_ALIAS(i2d_X509_CRL_INFO);

X509_CRL_INFO *
X509_CRL_INFO_new(void)
{
	return (X509_CRL_INFO *)ASN1_item_new(&X509_CRL_INFO_it);
}
LCRYPTO_ALIAS(X509_CRL_INFO_new);

void
X509_CRL_INFO_free(X509_CRL_INFO *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &X509_CRL_INFO_it);
}
LCRYPTO_ALIAS(X509_CRL_INFO_free);

X509_CRL *
d2i_X509_CRL(X509_CRL **a, const unsigned char **in, long len)
{
	return (X509_CRL *)ASN1_item_d2i((ASN1_VALUE **)a, in, len,
	    &X509_CRL_it);
}
LCRYPTO_ALIAS(d2i_X509_CRL);

int
i2d_X509_CRL(X509_CRL *a, unsigned char **out)
{
	return ASN1_item_i2d((ASN1_VALUE *)a, out, &X509_CRL_it);
}
LCRYPTO_ALIAS(i2d_X509_CRL);

X509_CRL *
X509_CRL_new(void)
{
	return (X509_CRL *)ASN1_item_new(&X509_CRL_it);
}
LCRYPTO_ALIAS(X509_CRL_new);

void
X509_CRL_free(X509_CRL *a)
{
	ASN1_item_free((ASN1_VALUE *)a, &X509_CRL_it);
}
LCRYPTO_ALIAS(X509_CRL_free);

X509_CRL *
X509_CRL_dup(X509_CRL *x)
{
	return ASN1_item_dup(&X509_CRL_it, x);
}
LCRYPTO_ALIAS(X509_CRL_dup);

int
X509_CRL_add0_revoked(X509_CRL *crl, X509_REVOKED *rev)
{
	X509_CRL_INFO *inf;

	inf = crl->crl;
	if (!inf->revoked)
		inf->revoked = sk_X509_REVOKED_new(X509_REVOKED_cmp);
	if (!inf->revoked || !sk_X509_REVOKED_push(inf->revoked, rev)) {
		ASN1error(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	inf->enc.modified = 1;
	return 1;
}
LCRYPTO_ALIAS(X509_CRL_add0_revoked);

int
X509_CRL_verify(X509_CRL *crl, EVP_PKEY *pkey)
{
	/*
	 * The CertificateList's signature AlgorithmIdentifier must match
	 * the one inside the TBSCertList, see RFC 5280, 5.1.1.2, 5.1.2.2.
	 */
	if (X509_ALGOR_cmp(crl->sig_alg, crl->crl->sig_alg) != 0)
		return 0;
	return ASN1_item_verify(&X509_CRL_INFO_it, crl->sig_alg, crl->signature,
	    crl->crl, pkey);
}
LCRYPTO_ALIAS(X509_CRL_verify);

static int
crl_revoked_issuer_match(X509_CRL *crl, X509_NAME *nm, X509_REVOKED *rev)
{
	int i;

	if (!rev->issuer) {
		if (!nm)
			return 1;
		if (!X509_NAME_cmp(nm, X509_CRL_get_issuer(crl)))
			return 1;
		return 0;
	}

	if (!nm)
		nm = X509_CRL_get_issuer(crl);

	for (i = 0; i < sk_GENERAL_NAME_num(rev->issuer); i++) {
		GENERAL_NAME *gen = sk_GENERAL_NAME_value(rev->issuer, i);
		if (gen->type != GEN_DIRNAME)
			continue;
		if (!X509_NAME_cmp(nm, gen->d.directoryName))
			return 1;
	}
	return 0;

}

static int
crl_lookup(X509_CRL *crl, X509_REVOKED **ret, ASN1_INTEGER *serial,
    X509_NAME *issuer)
{
	X509_REVOKED rtmp, *rev;
	int idx;

	rtmp.serialNumber = serial;
	if (!sk_X509_REVOKED_is_sorted(crl->crl->revoked)) {
		CRYPTO_w_lock(CRYPTO_LOCK_X509_CRL);
		sk_X509_REVOKED_sort(crl->crl->revoked);
		CRYPTO_w_unlock(CRYPTO_LOCK_X509_CRL);
	}
	idx = sk_X509_REVOKED_find(crl->crl->revoked, &rtmp);
	if (idx < 0)
		return 0;
	/* Need to look for matching name */
	for (; idx < sk_X509_REVOKED_num(crl->crl->revoked); idx++) {
		rev = sk_X509_REVOKED_value(crl->crl->revoked, idx);
		if (ASN1_INTEGER_cmp(rev->serialNumber, serial))
			return 0;
		if (crl_revoked_issuer_match(crl, issuer, rev)) {
			if (ret)
				*ret = rev;
			if (rev->reason == CRL_REASON_REMOVE_FROM_CRL)
				return 2;
			return 1;
		}
	}
	return 0;
}

int
X509_CRL_get0_by_serial(X509_CRL *crl, X509_REVOKED **ret,
    ASN1_INTEGER *serial)
{
	return crl_lookup(crl, ret, serial, NULL);
}
LCRYPTO_ALIAS(X509_CRL_get0_by_serial);

int
X509_CRL_get0_by_cert(X509_CRL *crl, X509_REVOKED **ret, X509 *x)
{
	return crl_lookup(crl, ret, X509_get_serialNumber(x),
	    X509_get_issuer_name(x));
}
LCRYPTO_ALIAS(X509_CRL_get0_by_cert);

int
X509_CRL_get_signature_nid(const X509_CRL *crl)
{
	return OBJ_obj2nid(crl->sig_alg->algorithm);
}
LCRYPTO_ALIAS(X509_CRL_get_signature_nid);

const STACK_OF(X509_EXTENSION) *
X509_CRL_get0_extensions(const X509_CRL *crl)
{
	return crl->crl->extensions;
}
LCRYPTO_ALIAS(X509_CRL_get0_extensions);

long
X509_CRL_get_version(const X509_CRL *crl)
{
	return ASN1_INTEGER_get(crl->crl->version);
}
LCRYPTO_ALIAS(X509_CRL_get_version);

const ASN1_TIME *
X509_CRL_get0_lastUpdate(const X509_CRL *crl)
{
	return crl->crl->lastUpdate;
}
LCRYPTO_ALIAS(X509_CRL_get0_lastUpdate);

ASN1_TIME *
X509_CRL_get_lastUpdate(X509_CRL *crl)
{
	return crl->crl->lastUpdate;
}
LCRYPTO_ALIAS(X509_CRL_get_lastUpdate);

const ASN1_TIME *
X509_CRL_get0_nextUpdate(const X509_CRL *crl)
{
	return crl->crl->nextUpdate;
}
LCRYPTO_ALIAS(X509_CRL_get0_nextUpdate);

ASN1_TIME *
X509_CRL_get_nextUpdate(X509_CRL *crl)
{
	return crl->crl->nextUpdate;
}
LCRYPTO_ALIAS(X509_CRL_get_nextUpdate);

X509_NAME *
X509_CRL_get_issuer(const X509_CRL *crl)
{
	return crl->crl->issuer;
}
LCRYPTO_ALIAS(X509_CRL_get_issuer);

STACK_OF(X509_REVOKED) *
X509_CRL_get_REVOKED(X509_CRL *crl)
{
	return crl->crl->revoked;
}
LCRYPTO_ALIAS(X509_CRL_get_REVOKED);

void
X509_CRL_get0_signature(const X509_CRL *crl, const ASN1_BIT_STRING **psig,
    const X509_ALGOR **palg)
{
	if (psig != NULL)
		*psig = crl->signature;
	if (palg != NULL)
		*palg = crl->sig_alg;
}
LCRYPTO_ALIAS(X509_CRL_get0_signature);

const X509_ALGOR *
X509_CRL_get0_tbs_sigalg(const X509_CRL *crl)
{
	return crl->crl->sig_alg;
}
LCRYPTO_ALIAS(X509_CRL_get0_tbs_sigalg);
