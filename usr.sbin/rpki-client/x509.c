/*	$OpenBSD: x509.c,v 1.119 2025/09/11 08:21:00 tb Exp $ */
/*
 * Copyright (c) 2022 Theo Buehler <tb@openbsd.org>
 * Copyright (c) 2021 Claudio Jeker <claudio@openbsd.org>
 * Copyright (c) 2019 Kristaps Dzonsons <kristaps@bsd.lv>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/evp.h>
#include <openssl/x509v3.h>

#include "extern.h"

#define GENTIME_LENGTH 15

ASN1_OBJECT	*certpol_oid;	/* id-cp-ipAddr-asNumber cert policy */
ASN1_OBJECT	*caissuers_oid;	/* 1.3.6.1.5.5.7.48.2 (caIssuers) */
ASN1_OBJECT	*carepo_oid;	/* 1.3.6.1.5.5.7.48.5 (caRepository) */
ASN1_OBJECT	*manifest_oid;	/* 1.3.6.1.5.5.7.48.10 (rpkiManifest) */
ASN1_OBJECT	*signedobj_oid;	/* 1.3.6.1.5.5.7.48.11 (signedObject) */
ASN1_OBJECT	*notify_oid;	/* 1.3.6.1.5.5.7.48.13 (rpkiNotify) */
ASN1_OBJECT	*roa_oid;	/* id-ct-routeOriginAuthz CMS content type */
ASN1_OBJECT	*mft_oid;	/* id-ct-rpkiManifest CMS content type */
ASN1_OBJECT	*gbr_oid;	/* id-ct-rpkiGhostbusters CMS content type */
ASN1_OBJECT	*bgpsec_oid;	/* id-kp-bgpsec-router Key Purpose */
ASN1_OBJECT	*cnt_type_oid;	/* pkcs-9 id-contentType */
ASN1_OBJECT	*msg_dgst_oid;	/* pkcs-9 id-messageDigest */
ASN1_OBJECT	*sign_time_oid;	/* pkcs-9 id-signingTime */
ASN1_OBJECT	*rsc_oid;	/* id-ct-signedChecklist */
ASN1_OBJECT	*aspa_oid;	/* id-ct-ASPA */
ASN1_OBJECT	*tak_oid;	/* id-ct-SignedTAL */
ASN1_OBJECT	*geofeed_oid;	/* id-ct-geofeedCSVwithCRLF */
ASN1_OBJECT	*spl_oid;	/* id-ct-signedPrefixList */
ASN1_OBJECT	*ccr_oid;	/* CanonicalCacheRepresentation PEN OID */

static const struct {
	const char	 *oid;
	ASN1_OBJECT	**ptr;
} oid_table[] = {
	{
		.oid = "1.3.6.1.5.5.7.14.2",
		.ptr = &certpol_oid,
	},
	{
		.oid = "1.3.6.1.5.5.7.48.2",
		.ptr = &caissuers_oid,
	},
	{
		.oid = "1.3.6.1.5.5.7.48.5",
		.ptr = &carepo_oid,
	},
	{
		.oid = "1.3.6.1.5.5.7.48.10",
		.ptr = &manifest_oid,
	},
	{
		.oid = "1.3.6.1.5.5.7.48.11",
		.ptr = &signedobj_oid,
	},
	{
		.oid = "1.3.6.1.5.5.7.48.13",
		.ptr = &notify_oid,
	},
	{
		.oid = "1.2.840.113549.1.9.16.1.24",
		.ptr = &roa_oid,
	},
	{
		.oid = "1.2.840.113549.1.9.16.1.26",
		.ptr = &mft_oid,
	},
	{
		.oid = "1.2.840.113549.1.9.16.1.35",
		.ptr = &gbr_oid,
	},
	{
		.oid = "1.3.6.1.5.5.7.3.30",
		.ptr = &bgpsec_oid,
	},
	{
		.oid = "1.2.840.113549.1.9.3",
		.ptr = &cnt_type_oid,
	},
	{
		.oid = "1.2.840.113549.1.9.4",
		.ptr = &msg_dgst_oid,
	},
	{
		.oid = "1.2.840.113549.1.9.5",
		.ptr = &sign_time_oid,
	},
	{
		.oid = "1.2.840.113549.1.9.16.1.47",
		.ptr = &geofeed_oid,
	},
	{
		.oid = "1.2.840.113549.1.9.16.1.48",
		.ptr = &rsc_oid,
	},
	{
		.oid = "1.2.840.113549.1.9.16.1.49",
		.ptr = &aspa_oid,
	},
	{
		.oid = "1.2.840.113549.1.9.16.1.50",
		.ptr = &tak_oid,
	},
	{
		.oid = "1.2.840.113549.1.9.16.1.51",
		.ptr = &spl_oid,
	},
	{
		.oid = "1.3.6.1.4.1.41948.825",
		.ptr = &ccr_oid,
	},
};

void
x509_init_oid(void)
{
	size_t	i;

	for (i = 0; i < sizeof(oid_table) / sizeof(oid_table[0]); i++) {
		*oid_table[i].ptr = OBJ_txt2obj(oid_table[i].oid, 1);
		if (*oid_table[i].ptr == NULL)
			errx(1, "OBJ_txt2obj for %s failed", oid_table[i].oid);
	}
}

/*
 * Compute the SKI of an RSA public key in an X509_PUBKEY using SHA-1.
 * Returns allocated hex-encoded SKI on success, NULL on failure.
 */
char *
x509_pubkey_get_ski(X509_PUBKEY *pubkey, const char *fn)
{
	X509_ALGOR		*alg = NULL;
	const ASN1_OBJECT	*aobj = NULL;
	int			 ptype = 0;
	const void		*pval = NULL;
	const unsigned char	*der;
	int			 der_len;
	unsigned char		 md[EVP_MAX_MD_SIZE];
	unsigned int		 md_len = EVP_MAX_MD_SIZE;
	unsigned char		 buf[80];

	/* XXX - dedup with cert_check_spki(), add more validity checks? */

	if (!X509_PUBKEY_get0_param(NULL, &der, &der_len, &alg, pubkey)) {
		warnx("%s: X509_PUBKEY_get0_param failed", fn);
		return NULL;
	}
	X509_ALGOR_get0(&aobj, &ptype, &pval, alg);

	if (OBJ_obj2nid(aobj) == NID_rsaEncryption) {
		if (ptype != V_ASN1_NULL || pval != NULL) {
			warnx("%s: RFC 4055, 1.2, rsaEncryption "
			    "parameters not NULL", fn);
			return NULL;
		}

		goto done;
	}

	if (!experimental) {
		warnx("%s: RFC 7935, 3.1 SPKI not RSAPublicKey", fn);
		return NULL;
	}

	if (OBJ_obj2nid(aobj) == NID_X9_62_id_ecPublicKey) {
		if (ptype != V_ASN1_OBJECT) {
			warnx("%s: RFC 5480, 2.1.1, ecPublicKey "
			    "parameters not namedCurve", fn);
			return NULL;
		}
		if (OBJ_obj2nid(pval) != NID_X9_62_prime256v1) {
			warnx("%s: RFC 8608, 3.1, named curve not P-256", fn);
			return NULL;
		}

		goto done;
	}

	OBJ_obj2txt(buf, sizeof(buf), aobj, 0);
	warnx("%s: unsupported public key type %s", fn, buf);
	return NULL;

 done:
	if (!EVP_Digest(der, der_len, md, &md_len, EVP_sha1(), NULL)) {
		warnx("%s: EVP_Digest failed", fn);
		return NULL;
	}

	return hex_encode(md, md_len);
}

/*
 * Check whether all RFC 3779 extensions are set to inherit.
 * Return 1 if both AS & IP are set to inherit.
 * Return 0 on failure (such as missing extensions or no inheritance).
 */
int
x509_inherits(X509 *x)
{
	STACK_OF(IPAddressFamily)	*addrblk = NULL;
	ASIdentifiers			*asidentifiers = NULL;
	const IPAddressFamily		*af;
	int				 crit, i, rc = 0;

	addrblk = X509_get_ext_d2i(x, NID_sbgp_ipAddrBlock, &crit, NULL);
	if (addrblk == NULL) {
		if (crit != -1)
			warnx("error parsing ipAddrBlock");
		goto out;
	}

	/*
	 * Check by hand, since X509v3_addr_inherits() success only means that
	 * at least one address family inherits, not all of them.
	 */
	for (i = 0; i < sk_IPAddressFamily_num(addrblk); i++) {
		af = sk_IPAddressFamily_value(addrblk, i);
		if (af->ipAddressChoice->type != IPAddressChoice_inherit)
			goto out;
	}

	asidentifiers = X509_get_ext_d2i(x, NID_sbgp_autonomousSysNum, NULL,
	    NULL);
	if (asidentifiers == NULL) {
		if (crit != -1)
			warnx("error parsing asIdentifiers");
		goto out;
	}

	/* We need to have AS numbers and don't want RDIs. */
	if (asidentifiers->asnum == NULL || asidentifiers->rdi != NULL)
		goto out;
	if (!X509v3_asid_inherits(asidentifiers))
		goto out;

	rc = 1;
 out:
	ASIdentifiers_free(asidentifiers);
	sk_IPAddressFamily_pop_free(addrblk, IPAddressFamily_free);
	return rc;
}

/*
 * Check whether at least one RFC 3779 extension is set to inherit.
 * Return 1 if an inherit element is encountered in AS or IP.
 * Return 0 otherwise.
 */
int
x509_any_inherits(X509 *x)
{
	STACK_OF(IPAddressFamily)	*addrblk = NULL;
	ASIdentifiers			*asidentifiers = NULL;
	int				 crit, rc = 0;

	addrblk = X509_get_ext_d2i(x, NID_sbgp_ipAddrBlock, &crit, NULL);
	if (addrblk == NULL && crit != -1)
		warnx("error parsing ipAddrBlock");
	if (X509v3_addr_inherits(addrblk))
		rc = 1;

	asidentifiers = X509_get_ext_d2i(x, NID_sbgp_autonomousSysNum, &crit,
	    NULL);
	if (asidentifiers == NULL && crit != -1)
		warnx("error parsing asIdentifiers");
	if (X509v3_asid_inherits(asidentifiers))
		rc = 1;

	ASIdentifiers_free(asidentifiers);
	sk_IPAddressFamily_pop_free(addrblk, IPAddressFamily_free);
	return rc;
}

/*
 * Convert passed ASN1_TIME to time_t *t.
 * Returns 1 on success and 0 on failure.
 */
int
x509_get_time(const ASN1_TIME *at, time_t *t)
{
	struct tm	 tm;

	*t = 0;
	memset(&tm, 0, sizeof(tm));
	/* Fail instead of silently falling back to the current time. */
	if (at == NULL)
		return 0;
	if (!ASN1_TIME_to_tm(at, &tm))
		return 0;
	if ((*t = timegm(&tm)) == -1)
		errx(1, "timegm failed");
	return 1;
}

int
x509_get_generalized_time(const char *fn, const char *descr,
    const ASN1_TIME *at, time_t *t)
{
	if (at->length != GENTIME_LENGTH) {
		warnx("%s: %s time format invalid", fn, descr);
		return 0;
	}
	if (!x509_get_time(at, t)) {
		warnx("%s: parsing %s failed", fn, descr);
		return 0;
	}
	return 1;
}

/*
 * Extract and validate an accessLocation, RFC 6487, 4.8 and RFC 8182, 3.2.
 * Returns 0 on failure and 1 on success.
 */
int
x509_location(const char *fn, const char *descr, GENERAL_NAME *location,
    char **out)
{
	ASN1_IA5STRING	*uri;

	assert(*out == NULL);

	if (location->type != GEN_URI) {
		warnx("%s: RFC 6487 section 4.8: %s not URI", fn, descr);
		return 0;
	}

	uri = location->d.uniformResourceIdentifier;

	if (!valid_uri(uri->data, uri->length, NULL)) {
		warnx("%s: RFC 6487 section 4.8: %s bad location", fn, descr);
		return 0;
	}

	if ((*out = strndup(uri->data, uri->length)) == NULL)
		err(1, NULL);

	return 1;
}

/*
 * Check that subject or issuer only contain commonName and serialNumber.
 * Return 0 on failure.
 */
int
x509_valid_name(const char *fn, const char *descr, const X509_NAME *xn)
{
	const X509_NAME_ENTRY *ne;
	const ASN1_OBJECT *ao;
	const ASN1_STRING *as;
	int cn = 0, sn = 0;
	int i, nid;

	for (i = 0; i < X509_NAME_entry_count(xn); i++) {
		if ((ne = X509_NAME_get_entry(xn, i)) == NULL) {
			warnx("%s: X509_NAME_get_entry", fn);
			return 0;
		}
		if ((ao = X509_NAME_ENTRY_get_object(ne)) == NULL) {
			warnx("%s: X509_NAME_ENTRY_get_object", fn);
			return 0;
		}

		nid = OBJ_obj2nid(ao);
		switch (nid) {
		case NID_commonName:
			if (cn++ > 0) {
				warnx("%s: duplicate commonName in %s",
				    fn, descr);
				return 0;
			}
			if ((as = X509_NAME_ENTRY_get_data(ne)) == NULL) {
				warnx("%s: X509_NAME_ENTRY_get_data failed",
				    fn);
				return 0;
			}
/*
 * The following check can be enabled after AFRINIC re-issues CA certs.
 * https://lists.afrinic.net/pipermail/dbwg/2023-March/000436.html
 */
#if 0
			/*
			 * XXX - For some reason RFC 8209, section 3.1.1 decided
			 * to allow UTF8String for BGPsec Router Certificates.
			 */
			if (ASN1_STRING_type(as) != V_ASN1_PRINTABLESTRING) {
				warnx("%s: RFC 6487 section 4.5: commonName is"
				    " not PrintableString", fn);
				return 0;
			}
#endif
			break;
		case NID_serialNumber:
			if (sn++ > 0) {
				warnx("%s: duplicate serialNumber in %s",
				    fn, descr);
				return 0;
			}
			break;
		case NID_undef:
			warnx("%s: OBJ_obj2nid failed", fn);
			return 0;
		default:
			warnx("%s: RFC 6487 section 4.5: unexpected attribute"
			    " %s in %s", fn, nid2str(nid), descr);
			return 0;
		}
	}

	if (cn == 0) {
		warnx("%s: RFC 6487 section 4.5: %s missing commonName",
		    fn, descr);
		return 0;
	}

	return 1;
}

/*
 * Check ASN1_INTEGER is non-negative and fits in 20 octets.
 * Returns allocated BIGNUM if true, NULL otherwise.
 */
static BIGNUM *
x509_seqnum_to_bn(const char *fn, const char *descr, const ASN1_INTEGER *i)
{
	BIGNUM *bn = NULL;

	if ((bn = ASN1_INTEGER_to_BN(i, NULL)) == NULL) {
		warnx("%s: %s: ASN1_INTEGER_to_BN error", fn, descr);
		goto out;
	}

	if (BN_is_negative(bn)) {
		warnx("%s: %s should be non-negative", fn, descr);
		goto out;
	}

	/* Reject values larger than or equal to 2^159. */
	if (BN_num_bytes(bn) > 20 || BN_is_bit_set(bn, 159)) {
		warnx("%s: %s should fit in 20 octets", fn, descr);
		goto out;
	}

	return bn;

 out:
	BN_free(bn);
	return NULL;
}

/*
 * Convert an ASN1_INTEGER into a hexstring, enforcing that it is non-negative
 * and representable by at most 20 octets (RFC 5280, section 4.1.2.2).
 * Returned string needs to be freed by the caller.
 */
char *
x509_convert_seqnum(const char *fn, const char *descr, const ASN1_INTEGER *i)
{
	BIGNUM	*bn = NULL;
	char	*s = NULL;

	if (i == NULL)
		goto out;

	if ((bn = x509_seqnum_to_bn(fn, descr, i)) == NULL)
		goto out;

	if ((s = BN_bn2hex(bn)) == NULL)
		warnx("%s: %s: BN_bn2hex error", fn, descr);

 out:
	BN_free(bn);
	return s;
}

int
x509_valid_seqnum(const char *fn, const char *descr, const ASN1_INTEGER *i)
{
	BIGNUM *bn;

	if ((bn = x509_seqnum_to_bn(fn, descr, i)) == NULL)
		return 0;

	BN_free(bn);
	return 1;
}

/*
 * Helper to check the signature algorithm in the signed part of a cert
 * or CRL matches expectations. Only accept sha256WithRSAEncryption by
 * default and in experimental mode also accept ecdsa-with-SHA256.
 * Check compliance of the parameter encoding as well.
 */
int
x509_check_tbs_sigalg(const char *fn, const X509_ALGOR *tbsalg)
{
	const ASN1_OBJECT *aobj = NULL;
	int ptype = 0;
	int nid;

	X509_ALGOR_get0(&aobj, &ptype, NULL, tbsalg);
	if ((nid = OBJ_obj2nid(aobj)) == NID_undef) {
		warnx("%s: unknown signature type", fn);
		return 0;
	}

	if (nid == NID_sha256WithRSAEncryption) {
		/*
		 * Correct encoding of parameters is explicit ASN.1 NULL
		 * (V_ASN1_NULL), but implementations MUST accept absent
		 * parameters due to an ASN.1 syntax translation mishap,
		 * see, e.g., RFC 4055, 2.1.
		 */
		if (ptype != V_ASN1_NULL && ptype != V_ASN1_UNDEF) {
			warnx("%s: RFC 4055, 5: wrong ASN.1 parameters for %s",
			    fn, LN_sha256WithRSAEncryption);
			return 0;
		}
		/*
		 * As of July 2025, there still are ~1600 ROA EE certs with this
		 * faulty encoding, all issued by ARIN before September 2020.
		 */
		if (verbose > 1 && ptype == V_ASN1_UNDEF)
			warnx("%s: RFC 4055, 5: %s without ASN.1 parameters",
			    fn, LN_sha256WithRSAEncryption);
		return 1;
	}

	if (experimental && nid == NID_ecdsa_with_SHA256) {
		if (ptype != V_ASN1_UNDEF) {
			warnx("%s: RFC 5758, 3.2: %s encoding MUST omit "
			    "the parameters", fn, SN_ecdsa_with_SHA256);
			return 0;
		}
		if (verbose)
			warnx("%s: P-256 support is experimental", fn);
		return 1;
	}

	warnx("%s: RFC 7935: wrong signature algorithm %s, want %s",
	    fn, nid2str(nid), LN_sha256WithRSAEncryption);
	return 0;
}

/*
 * Find the closest expiry moment by walking the chain of authorities.
 */
time_t
x509_find_expires(time_t notafter, struct auth *a, struct crl_tree *crls)
{
	struct crl	*crl;
	time_t		 expires;

	expires = notafter;

	for (; a != NULL; a = a->issuer) {
		if (expires > a->cert->notafter)
			expires = a->cert->notafter;
		crl = crl_get(crls, a);
		if (crl != NULL && expires > crl->nextupdate)
			expires = crl->nextupdate;
	}

	return expires;
}
