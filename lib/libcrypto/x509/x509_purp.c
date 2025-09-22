/* $OpenBSD: x509_purp.c,v 1.44 2025/05/10 05:54:39 tb Exp $ */
/* Written by Dr Stephen N Henson (steve@openssl.org) for the OpenSSL
 * project 2001.
 */
/* ====================================================================
 * Copyright (c) 1999-2004 The OpenSSL Project.  All rights reserved.
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
#include <string.h>

#include <openssl/opensslconf.h>

#include <openssl/x509v3.h>
#include <openssl/x509_vfy.h>

#include "x509_internal.h"
#include "x509_local.h"

struct x509_purpose_st {
	int purpose;
	int trust;		/* Default trust ID */
	int flags;
	int (*check_purpose)(const struct x509_purpose_st *, const X509 *, int);
	char *name;
	char *sname;
	void *usr_data;
} /* X509_PURPOSE */;

#define V1_ROOT (EXFLAG_V1|EXFLAG_SS)
#define ku_reject(x, usage) \
	(((x)->ex_flags & EXFLAG_KUSAGE) && !((x)->ex_kusage & (usage)))
#define xku_reject(x, usage) \
	(((x)->ex_flags & EXFLAG_XKUSAGE) && !((x)->ex_xkusage & (usage)))
#define ns_reject(x, usage) \
	(((x)->ex_flags & EXFLAG_NSCERT) && !((x)->ex_nscert & (usage)))

static int check_ssl_ca(const X509 *x);
static int check_purpose_ssl_client(const X509_PURPOSE *xp, const X509 *x,
    int ca);
static int check_purpose_ssl_server(const X509_PURPOSE *xp, const X509 *x,
    int ca);
static int check_purpose_ns_ssl_server(const X509_PURPOSE *xp, const X509 *x,
    int ca);
static int purpose_smime(const X509 *x, int ca);
static int check_purpose_smime_sign(const X509_PURPOSE *xp, const X509 *x,
    int ca);
static int check_purpose_smime_encrypt(const X509_PURPOSE *xp, const X509 *x,
    int ca);
static int check_purpose_crl_sign(const X509_PURPOSE *xp, const X509 *x,
    int ca);
static int check_purpose_timestamp_sign(const X509_PURPOSE *xp, const X509 *x,
    int ca);
static int no_check(const X509_PURPOSE *xp, const X509 *x, int ca);
static int ocsp_helper(const X509_PURPOSE *xp, const X509 *x, int ca);

static const X509_PURPOSE xstandard[] = {
	{
		.purpose = X509_PURPOSE_SSL_CLIENT,
		.trust = X509_TRUST_SSL_CLIENT,
		.check_purpose = check_purpose_ssl_client,
		.name = "SSL client",
		.sname = "sslclient",
	},
	{
		.purpose = X509_PURPOSE_SSL_SERVER,
		.trust = X509_TRUST_SSL_SERVER,
		.check_purpose = check_purpose_ssl_server,
		.name = "SSL server",
		.sname = "sslserver",
	},
	{
		.purpose = X509_PURPOSE_NS_SSL_SERVER,
		.trust = X509_TRUST_SSL_SERVER,
		.check_purpose = check_purpose_ns_ssl_server,
		.name = "Netscape SSL server",
		.sname = "nssslserver",
	},
	{
		.purpose = X509_PURPOSE_SMIME_SIGN,
		.trust = X509_TRUST_EMAIL,
		.check_purpose = check_purpose_smime_sign,
		.name = "S/MIME signing",
		.sname = "smimesign",
	},
	{
		.purpose = X509_PURPOSE_SMIME_ENCRYPT,
		.trust = X509_TRUST_EMAIL,
		.check_purpose = check_purpose_smime_encrypt,
		.name = "S/MIME encryption",
		.sname = "smimeencrypt",
	},
	{
		.purpose = X509_PURPOSE_CRL_SIGN,
		.trust = X509_TRUST_COMPAT,
		.check_purpose = check_purpose_crl_sign,
		.name = "CRL signing",
		.sname = "crlsign",
	},
	{
		.purpose = X509_PURPOSE_ANY,
		.trust = X509_TRUST_ACCEPT_ALL,
		.check_purpose = no_check,
		.name = "Any Purpose",
		.sname = "any",
	},
	{
		.purpose = X509_PURPOSE_OCSP_HELPER,
		.trust = X509_TRUST_COMPAT,
		.check_purpose = ocsp_helper,
		.name = "OCSP helper",
		.sname = "ocsphelper",
	},
	{
		.purpose = X509_PURPOSE_TIMESTAMP_SIGN,
		.trust = X509_TRUST_TSA,
		.check_purpose = check_purpose_timestamp_sign,
		.name = "Time Stamp signing",
		.sname = "timestampsign",
	},
};

#define X509_PURPOSE_COUNT (sizeof(xstandard) / sizeof(xstandard[0]))

/* As much as I'd like to make X509_check_purpose use a "const" X509*
 * I really can't because it does recalculate hashes and do other non-const
 * things. */
int
X509_check_purpose(X509 *x, int id, int ca)
{
	int idx;
	const X509_PURPOSE *pt;

	if (!x509v3_cache_extensions(x))
		return -1;

	if (id == -1)
		return 1;

	if ((idx = X509_PURPOSE_get_by_id(id)) == -1)
		return -1;
	if ((pt = X509_PURPOSE_get0(idx)) == NULL)
		return -1;

	return pt->check_purpose(pt, x, ca);
}
LCRYPTO_ALIAS(X509_check_purpose);

int
X509_PURPOSE_get_count(void)
{
	return X509_PURPOSE_COUNT;
}
LCRYPTO_ALIAS(X509_PURPOSE_get_count);

const X509_PURPOSE *
X509_PURPOSE_get0(int idx)
{
	if (idx < 0 || (size_t)idx >= X509_PURPOSE_COUNT)
		return NULL;

	return &xstandard[idx];
}
LCRYPTO_ALIAS(X509_PURPOSE_get0);

int
X509_PURPOSE_get_by_sname(const char *sname)
{
	int i;
	const X509_PURPOSE *xptmp;

	for (i = 0; i < X509_PURPOSE_get_count(); i++) {
		xptmp = X509_PURPOSE_get0(i);
		if (!strcmp(xptmp->sname, sname))
			return i;
	}
	return -1;
}
LCRYPTO_ALIAS(X509_PURPOSE_get_by_sname);

int
X509_PURPOSE_get_by_id(int purpose)
{
	/*
	 * Ensure the purpose identifier is between MIN and MAX inclusive.
	 * If so, translate it to an index into the xstandard[] table.
	 */
	if (purpose < X509_PURPOSE_MIN || purpose > X509_PURPOSE_MAX)
		return -1;

	return purpose - X509_PURPOSE_MIN;
}

int
X509_PURPOSE_get_id(const X509_PURPOSE *xp)
{
	return xp->purpose;
}
LCRYPTO_ALIAS(X509_PURPOSE_get_id);

const char *
X509_PURPOSE_get0_name(const X509_PURPOSE *xp)
{
	return xp->name;
}
LCRYPTO_ALIAS(X509_PURPOSE_get0_name);

const char *
X509_PURPOSE_get0_sname(const X509_PURPOSE *xp)
{
	return xp->sname;
}
LCRYPTO_ALIAS(X509_PURPOSE_get0_sname);

int
X509_PURPOSE_get_trust(const X509_PURPOSE *xp)
{
	return xp->trust;
}

/*
 * List of NIDs of extensions supported by the verifier. If an extension
 * is critical and doesn't appear in this list, then the certificate will
 * normally be rejected.
 */
int
X509_supported_extension(X509_EXTENSION *ext)
{
	switch (OBJ_obj2nid(X509_EXTENSION_get_object(ext))) {
	case NID_basic_constraints:
	case NID_certificate_policies:
	case NID_ext_key_usage:
	case NID_inhibit_any_policy:
	case NID_key_usage:
	case NID_name_constraints:
	case NID_netscape_cert_type:
	case NID_policy_constraints:
	case NID_policy_mappings:
#ifndef OPENSSL_NO_RFC3779
	case NID_sbgp_ipAddrBlock:
	case NID_sbgp_autonomousSysNum:
#endif
	case NID_subject_alt_name:
		return 1;
	default:
		return 0;
	}
}
LCRYPTO_ALIAS(X509_supported_extension);

static void
setup_dp(X509 *x, DIST_POINT *dp)
{
	X509_NAME *iname = NULL;
	int i;

	if (dp->reasons) {
		if (dp->reasons->length > 0)
			dp->dp_reasons = dp->reasons->data[0];
		if (dp->reasons->length > 1)
			dp->dp_reasons |= (dp->reasons->data[1] << 8);
		dp->dp_reasons &= CRLDP_ALL_REASONS;
	} else
		dp->dp_reasons = CRLDP_ALL_REASONS;
	if (!dp->distpoint || (dp->distpoint->type != 1))
		return;
	for (i = 0; i < sk_GENERAL_NAME_num(dp->CRLissuer); i++) {
		GENERAL_NAME *gen = sk_GENERAL_NAME_value(dp->CRLissuer, i);
		if (gen->type == GEN_DIRNAME) {
			iname = gen->d.directoryName;
			break;
		}
	}
	if (!iname)
		iname = X509_get_issuer_name(x);

	DIST_POINT_set_dpname(dp->distpoint, iname);
}

static void
setup_crldp(X509 *x)
{
	int i;

	x->crldp = X509_get_ext_d2i(x, NID_crl_distribution_points, &i, NULL);
	if (x->crldp == NULL && i != -1) {
		x->ex_flags |= EXFLAG_INVALID;
		return;
	}

	for (i = 0; i < sk_DIST_POINT_num(x->crldp); i++)
		setup_dp(x, sk_DIST_POINT_value(x->crldp, i));
}

static int
x509_extension_oid_cmp(const X509_EXTENSION *const *a,
    const X509_EXTENSION *const *b)
{
	return OBJ_cmp((*a)->object, (*b)->object);
}

static int
x509_extension_oids_are_unique(X509 *x509)
{
	STACK_OF(X509_EXTENSION) *exts = NULL;
	const X509_EXTENSION *prev_ext, *curr_ext;
	int i;
	int ret = 0;

	if (X509_get_ext_count(x509) <= 1)
		goto done;

	if ((exts = sk_X509_EXTENSION_dup(x509->cert_info->extensions)) == NULL)
		goto err;

	(void)sk_X509_EXTENSION_set_cmp_func(exts, x509_extension_oid_cmp);
	sk_X509_EXTENSION_sort(exts);

	prev_ext = sk_X509_EXTENSION_value(exts, 0);
	for (i = 1; i < sk_X509_EXTENSION_num(exts); i++) {
		curr_ext = sk_X509_EXTENSION_value(exts, i);
		if (x509_extension_oid_cmp(&prev_ext, &curr_ext) == 0)
			goto err;
		prev_ext = curr_ext;
	}

 done:
	ret = 1;

 err:
	sk_X509_EXTENSION_free(exts);

	return ret;
}

static void
x509v3_cache_extensions_internal(X509 *x)
{
	BASIC_CONSTRAINTS *bs;
	ASN1_BIT_STRING *usage;
	ASN1_BIT_STRING *ns;
	EXTENDED_KEY_USAGE *extusage;
	X509_EXTENSION *ex;
	long version;
	int i;

	if (x->ex_flags & EXFLAG_SET)
		return;

	/*
	 * XXX - this should really only set EXFLAG_INVALID if extensions are
	 * invalid. However, the X509_digest() failure matches OpenSSL/BoringSSL
	 * behavior and the version checks are at least vaguely related to
	 * extensions.
	 */

	if (!X509_digest(x, X509_CERT_HASH_EVP, x->hash, NULL))
		x->ex_flags |= EXFLAG_INVALID;

	version = X509_get_version(x);
	if (version < 0 || version > 2)
		x->ex_flags |= EXFLAG_INVALID;
	if (version == 0) {
		x->ex_flags |= EXFLAG_V1;
		/* UIDs may only appear in v2 or v3 certs */
		if (x->cert_info->issuerUID != NULL ||
		    x->cert_info->subjectUID != NULL)
			x->ex_flags |= EXFLAG_INVALID;
	}
	if (version != 2 && X509_get_ext_count(x) != 0)
		x->ex_flags |= EXFLAG_INVALID;

	/* Handle basic constraints */
	if ((bs = X509_get_ext_d2i(x, NID_basic_constraints, &i, NULL))) {
		if (bs->ca)
			x->ex_flags |= EXFLAG_CA;
		if (bs->pathlen) {
			if ((bs->pathlen->type == V_ASN1_NEG_INTEGER) ||
			    !bs->ca) {
				x->ex_flags |= EXFLAG_INVALID;
				x->ex_pathlen = 0;
			} else
				x->ex_pathlen = ASN1_INTEGER_get(bs->pathlen);
		} else
			x->ex_pathlen = -1;
		BASIC_CONSTRAINTS_free(bs);
		x->ex_flags |= EXFLAG_BCONS;
	} else if (i != -1) {
		x->ex_flags |= EXFLAG_INVALID;
	}

	/* Handle key usage */
	if ((usage = X509_get_ext_d2i(x, NID_key_usage, &i, NULL))) {
		if (usage->length > 0) {
			x->ex_kusage = usage->data[0];
			if (usage->length > 1)
				x->ex_kusage |= usage->data[1] << 8;
		} else
			x->ex_kusage = 0;
		x->ex_flags |= EXFLAG_KUSAGE;
		ASN1_BIT_STRING_free(usage);
	} else if (i != -1) {
		x->ex_flags |= EXFLAG_INVALID;
	}

	x->ex_xkusage = 0;
	if ((extusage = X509_get_ext_d2i(x, NID_ext_key_usage, &i, NULL))) {
		x->ex_flags |= EXFLAG_XKUSAGE;
		for (i = 0; i < sk_ASN1_OBJECT_num(extusage); i++) {
			switch (OBJ_obj2nid(sk_ASN1_OBJECT_value(extusage, i))) {
			case NID_server_auth:
				x->ex_xkusage |= XKU_SSL_SERVER;
				break;

			case NID_client_auth:
				x->ex_xkusage |= XKU_SSL_CLIENT;
				break;

			case NID_email_protect:
				x->ex_xkusage |= XKU_SMIME;
				break;

			case NID_code_sign:
				x->ex_xkusage |= XKU_CODE_SIGN;
				break;

			case NID_ms_sgc:
			case NID_ns_sgc:
				x->ex_xkusage |= XKU_SGC;
				break;

			case NID_OCSP_sign:
				x->ex_xkusage |= XKU_OCSP_SIGN;
				break;

			case NID_time_stamp:
				x->ex_xkusage |= XKU_TIMESTAMP;
				break;

			case NID_dvcs:
				x->ex_xkusage |= XKU_DVCS;
				break;

			case NID_anyExtendedKeyUsage:
				x->ex_xkusage |= XKU_ANYEKU;
				break;
			}
		}
		sk_ASN1_OBJECT_pop_free(extusage, ASN1_OBJECT_free);
	} else if (i != -1) {
		x->ex_flags |= EXFLAG_INVALID;
	}

	if ((ns = X509_get_ext_d2i(x, NID_netscape_cert_type, &i, NULL))) {
		if (ns->length > 0)
			x->ex_nscert = ns->data[0];
		else
			x->ex_nscert = 0;
		x->ex_flags |= EXFLAG_NSCERT;
		ASN1_BIT_STRING_free(ns);
	} else if (i != -1) {
		x->ex_flags |= EXFLAG_INVALID;
	}

	x->skid = X509_get_ext_d2i(x, NID_subject_key_identifier, &i, NULL);
	if (x->skid == NULL && i != -1)
		x->ex_flags |= EXFLAG_INVALID;
	x->akid = X509_get_ext_d2i(x, NID_authority_key_identifier, &i, NULL);
	if (x->akid == NULL && i != -1)
		x->ex_flags |= EXFLAG_INVALID;

	/* Does subject name match issuer? */
	if (!X509_NAME_cmp(X509_get_subject_name(x), X509_get_issuer_name(x))) {
		x->ex_flags |= EXFLAG_SI;
		/* If SKID matches AKID also indicate self signed. */
		if (X509_check_akid(x, x->akid) == X509_V_OK &&
		    !ku_reject(x, KU_KEY_CERT_SIGN))
			x->ex_flags |= EXFLAG_SS;
	}

	x->altname = X509_get_ext_d2i(x, NID_subject_alt_name, &i, NULL);
	if (x->altname == NULL && i != -1)
		x->ex_flags |= EXFLAG_INVALID;
	x->nc = X509_get_ext_d2i(x, NID_name_constraints, &i, NULL);
	if (!x->nc && (i != -1))
		x->ex_flags |= EXFLAG_INVALID;
	setup_crldp(x);

#ifndef OPENSSL_NO_RFC3779
	x->rfc3779_addr = X509_get_ext_d2i(x, NID_sbgp_ipAddrBlock, &i, NULL);
	if (x->rfc3779_addr == NULL && i != -1)
		x->ex_flags |= EXFLAG_INVALID;
	if (!X509v3_addr_is_canonical(x->rfc3779_addr))
		x->ex_flags |= EXFLAG_INVALID;
	x->rfc3779_asid = X509_get_ext_d2i(x, NID_sbgp_autonomousSysNum, &i, NULL);
	if (x->rfc3779_asid == NULL && i != -1)
		x->ex_flags |= EXFLAG_INVALID;
	if (!X509v3_asid_is_canonical(x->rfc3779_asid))
		x->ex_flags |= EXFLAG_INVALID;
#endif

	for (i = 0; i < X509_get_ext_count(x); i++) {
		ex = X509_get_ext(x, i);
		if (OBJ_obj2nid(X509_EXTENSION_get_object(ex)) ==
		    NID_freshest_crl)
			x->ex_flags |= EXFLAG_FRESHEST;
		if (!X509_EXTENSION_get_critical(ex))
			continue;
		if (!X509_supported_extension(ex)) {
			x->ex_flags |= EXFLAG_CRITICAL;
			break;
		}
	}

	if (!x509_extension_oids_are_unique(x))
		x->ex_flags |= EXFLAG_INVALID;

	x->ex_flags |= EXFLAG_SET;
}

int
x509v3_cache_extensions(X509 *x)
{
	if ((x->ex_flags & EXFLAG_SET) == 0) {
		CRYPTO_w_lock(CRYPTO_LOCK_X509);
		x509v3_cache_extensions_internal(x);
		CRYPTO_w_unlock(CRYPTO_LOCK_X509);
	}

	return (x->ex_flags & EXFLAG_INVALID) == 0;
}

/* CA checks common to all purposes
 * return codes:
 * 0 not a CA
 * 1 is a CA
 * 2 basicConstraints absent so "maybe" a CA
 * 3 basicConstraints absent but self signed V1.
 * 4 basicConstraints absent but keyUsage present and keyCertSign asserted.
 */

static int
check_ca(const X509 *x)
{
	/* keyUsage if present should allow cert signing */
	if (ku_reject(x, KU_KEY_CERT_SIGN))
		return 0;
	if (x->ex_flags & EXFLAG_BCONS) {
		if (x->ex_flags & EXFLAG_CA)
			return 1;
		/* If basicConstraints says not a CA then say so */
		else
			return 0;
	} else {
		/* we support V1 roots for...  uh, I don't really know why. */
		if ((x->ex_flags & V1_ROOT) == V1_ROOT)
			return 3;
		/* If key usage present it must have certSign so tolerate it */
		else if (x->ex_flags & EXFLAG_KUSAGE)
			return 4;
		/* Older certificates could have Netscape-specific CA types */
		else if (x->ex_flags & EXFLAG_NSCERT &&
		    x->ex_nscert & NS_ANY_CA)
			return 5;
		/* can this still be regarded a CA certificate?  I doubt it */
		return 0;
	}
}

int
X509_check_ca(X509 *x)
{
	x509v3_cache_extensions(x);

	return check_ca(x);
}
LCRYPTO_ALIAS(X509_check_ca);

/* Check SSL CA: common checks for SSL client and server */
static int
check_ssl_ca(const X509 *x)
{
	int ca_ret;

	ca_ret = check_ca(x);
	if (!ca_ret)
		return 0;
	/* check nsCertType if present */
	if (ca_ret != 5 || x->ex_nscert & NS_SSL_CA)
		return ca_ret;
	else
		return 0;
}

static int
check_purpose_ssl_client(const X509_PURPOSE *xp, const X509 *x, int ca)
{
	if (xku_reject(x, XKU_SSL_CLIENT))
		return 0;
	if (ca)
		return check_ssl_ca(x);
	/* We need to do digital signatures with it */
	if (ku_reject(x, KU_DIGITAL_SIGNATURE))
		return 0;
	/* nsCertType if present should allow SSL client use */
	if (ns_reject(x, NS_SSL_CLIENT))
		return 0;
	return 1;
}

static int
check_purpose_ssl_server(const X509_PURPOSE *xp, const X509 *x, int ca)
{
	if (xku_reject(x, XKU_SSL_SERVER|XKU_SGC))
		return 0;
	if (ca)
		return check_ssl_ca(x);

	if (ns_reject(x, NS_SSL_SERVER))
		return 0;
	/* Now as for keyUsage: we'll at least need to sign OR encipher */
	if (ku_reject(x, KU_DIGITAL_SIGNATURE|KU_KEY_ENCIPHERMENT))
		return 0;

	return 1;
}

static int
check_purpose_ns_ssl_server(const X509_PURPOSE *xp, const X509 *x, int ca)
{
	int ret;

	ret = check_purpose_ssl_server(xp, x, ca);
	if (!ret || ca)
		return ret;
	/* We need to encipher or Netscape complains */
	if (ku_reject(x, KU_KEY_ENCIPHERMENT))
		return 0;
	return ret;
}

/* common S/MIME checks */
static int
purpose_smime(const X509 *x, int ca)
{
	if (xku_reject(x, XKU_SMIME))
		return 0;
	if (ca) {
		int ca_ret;
		ca_ret = check_ca(x);
		if (!ca_ret)
			return 0;
		/* check nsCertType if present */
		if (ca_ret != 5 || x->ex_nscert & NS_SMIME_CA)
			return ca_ret;
		else
			return 0;
	}
	if (x->ex_flags & EXFLAG_NSCERT) {
		if (x->ex_nscert & NS_SMIME)
			return 1;
		/* Workaround for some buggy certificates */
		if (x->ex_nscert & NS_SSL_CLIENT)
			return 2;
		return 0;
	}
	return 1;
}

static int
check_purpose_smime_sign(const X509_PURPOSE *xp, const X509 *x, int ca)
{
	int ret;

	ret = purpose_smime(x, ca);
	if (!ret || ca)
		return ret;
	if (ku_reject(x, KU_DIGITAL_SIGNATURE|KU_NON_REPUDIATION))
		return 0;
	return ret;
}

static int
check_purpose_smime_encrypt(const X509_PURPOSE *xp, const X509 *x, int ca)
{
	int ret;

	ret = purpose_smime(x, ca);
	if (!ret || ca)
		return ret;
	if (ku_reject(x, KU_KEY_ENCIPHERMENT))
		return 0;
	return ret;
}

static int
check_purpose_crl_sign(const X509_PURPOSE *xp, const X509 *x, int ca)
{
	if (ca) {
		int ca_ret;
		if ((ca_ret = check_ca(x)) != 2)
			return ca_ret;
		else
			return 0;
	}
	if (ku_reject(x, KU_CRL_SIGN))
		return 0;
	return 1;
}

/* OCSP helper: this is *not* a full OCSP check. It just checks that
 * each CA is valid. Additional checks must be made on the chain.
 */
static int
ocsp_helper(const X509_PURPOSE *xp, const X509 *x, int ca)
{
	/* Must be a valid CA.  Should we really support the "I don't know"
	   value (2)? */
	if (ca)
		return check_ca(x);
	/* leaf certificate is checked in OCSP_verify() */
	return 1;
}

static int
check_purpose_timestamp_sign(const X509_PURPOSE *xp, const X509 *x, int ca)
{
	int i_ext;

	/* If ca is true we must return if this is a valid CA certificate. */
	if (ca)
		return check_ca(x);

	/*
	 * Check the optional key usage field:
	 * if Key Usage is present, it must be one of digitalSignature
	 * and/or nonRepudiation (other values are not consistent and shall
	 * be rejected).
	 */
	if ((x->ex_flags & EXFLAG_KUSAGE) &&
	    ((x->ex_kusage & ~(KU_NON_REPUDIATION | KU_DIGITAL_SIGNATURE)) ||
	    !(x->ex_kusage & (KU_NON_REPUDIATION | KU_DIGITAL_SIGNATURE))))
		return 0;

	/* Only time stamp key usage is permitted and it's required. */
	if (!(x->ex_flags & EXFLAG_XKUSAGE) || x->ex_xkusage != XKU_TIMESTAMP)
		return 0;

	/* Extended Key Usage MUST be critical */
	i_ext = X509_get_ext_by_NID((X509 *) x, NID_ext_key_usage, -1);
	if (i_ext >= 0) {
		X509_EXTENSION *ext = X509_get_ext((X509 *) x, i_ext);
		if (!X509_EXTENSION_get_critical(ext))
			return 0;
	}

	return 1;
}

static int
no_check(const X509_PURPOSE *xp, const X509 *x, int ca)
{
	return 1;
}

/* Various checks to see if one certificate issued the second.
 * This can be used to prune a set of possible issuer certificates
 * which have been looked up using some simple method such as by
 * subject name.
 * These are:
 * 1. Check issuer_name(subject) == subject_name(issuer)
 * 2. If akid(subject) exists check it matches issuer
 * 3. If key_usage(issuer) exists check it supports certificate signing
 * returns 0 for OK, positive for reason for mismatch, reasons match
 * codes for X509_verify_cert()
 */

int
X509_check_issued(X509 *issuer, X509 *subject)
{
	if (X509_NAME_cmp(X509_get_subject_name(issuer),
	    X509_get_issuer_name(subject)))
		return X509_V_ERR_SUBJECT_ISSUER_MISMATCH;

	if (!x509v3_cache_extensions(issuer))
		return X509_V_ERR_UNSPECIFIED;
	if (!x509v3_cache_extensions(subject))
		return X509_V_ERR_UNSPECIFIED;

	if (subject->akid) {
		int ret = X509_check_akid(issuer, subject->akid);
		if (ret != X509_V_OK)
			return ret;
	}

	if (ku_reject(issuer, KU_KEY_CERT_SIGN))
		return X509_V_ERR_KEYUSAGE_NO_CERTSIGN;
	return X509_V_OK;
}
LCRYPTO_ALIAS(X509_check_issued);

int
X509_check_akid(X509 *issuer, AUTHORITY_KEYID *akid)
{
	if (!akid)
		return X509_V_OK;

	/* Check key ids (if present) */
	if (akid->keyid && issuer->skid &&
	    ASN1_OCTET_STRING_cmp(akid->keyid, issuer->skid))
		return X509_V_ERR_AKID_SKID_MISMATCH;
	/* Check serial number */
	if (akid->serial &&
	    ASN1_INTEGER_cmp(X509_get_serialNumber(issuer), akid->serial))
		return X509_V_ERR_AKID_ISSUER_SERIAL_MISMATCH;
	/* Check issuer name */
	if (akid->issuer) {
		/* Ugh, for some peculiar reason AKID includes
		 * SEQUENCE OF GeneralName. So look for a DirName.
		 * There may be more than one but we only take any
		 * notice of the first.
		 */
		GENERAL_NAMES *gens;
		GENERAL_NAME *gen;
		X509_NAME *nm = NULL;
		int i;
		gens = akid->issuer;
		for (i = 0; i < sk_GENERAL_NAME_num(gens); i++) {
			gen = sk_GENERAL_NAME_value(gens, i);
			if (gen->type == GEN_DIRNAME) {
				nm = gen->d.dirn;
				break;
			}
		}
		if (nm && X509_NAME_cmp(nm, X509_get_issuer_name(issuer)))
			return X509_V_ERR_AKID_ISSUER_SERIAL_MISMATCH;
	}
	return X509_V_OK;
}
LCRYPTO_ALIAS(X509_check_akid);

uint32_t
X509_get_extension_flags(X509 *x)
{
	/* Call for side-effect of computing hash and caching extensions */
	if (X509_check_purpose(x, -1, -1) != 1)
		return EXFLAG_INVALID;

	return x->ex_flags;
}
LCRYPTO_ALIAS(X509_get_extension_flags);

uint32_t
X509_get_key_usage(X509 *x)
{
	/* Call for side-effect of computing hash and caching extensions */
	if (X509_check_purpose(x, -1, -1) != 1)
		return 0;

	if (x->ex_flags & EXFLAG_KUSAGE)
		return x->ex_kusage;

	return UINT32_MAX;
}
LCRYPTO_ALIAS(X509_get_key_usage);

uint32_t
X509_get_extended_key_usage(X509 *x)
{
	/* Call for side-effect of computing hash and caching extensions */
	if (X509_check_purpose(x, -1, -1) != 1)
		return 0;

	if (x->ex_flags & EXFLAG_XKUSAGE)
		return x->ex_xkusage;

	return UINT32_MAX;
}
LCRYPTO_ALIAS(X509_get_extended_key_usage);
