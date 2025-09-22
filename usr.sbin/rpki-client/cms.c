/*	$OpenBSD: cms.c,v 1.54 2025/08/01 14:57:15 tb Exp $ */
/*
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
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/cms.h>

#include "extern.h"

static int
cms_extract_econtent(const char *fn, CMS_ContentInfo *cms, unsigned char **res,
    size_t *rsz)
{
	ASN1_OCTET_STRING		**os = NULL;

	/* Detached signature case: no eContent to extract, so do nothing. */
	if (res == NULL || rsz == NULL)
		return 1;

	if ((os = CMS_get0_content(cms)) == NULL || *os == NULL) {
		warnx("%s: RFC 6488 section 2.1.4: "
		    "eContent: zero-length content", fn);
		return 0;
	}

	/*
	 * The eContent in os is owned by the cms object and it has to outlive
	 * it for further processing by the signedObject handlers. Since there
	 * is no convenient API for this purpose, duplicate it by hand.
	 */
	if ((*res = malloc((*os)->length)) == NULL)
		err(1, NULL);
	memcpy(*res, (*os)->data, (*os)->length);
	*rsz = (*os)->length;

	return 1;
}

static int
cms_get_signtime(const char *fn, X509_ATTRIBUTE *attr, time_t *signtime)
{
	const ASN1_TIME		*at;
	const char		*time_str = "UTCtime";
	int			 time_type = V_ASN1_UTCTIME;

	*signtime = 0;
	at = X509_ATTRIBUTE_get0_data(attr, 0, time_type, NULL);
	if (at == NULL) {
		time_str = "GeneralizedTime";
		time_type = V_ASN1_GENERALIZEDTIME;
		at = X509_ATTRIBUTE_get0_data(attr, 0, time_type, NULL);
		if (at == NULL) {
			warnx("%s: CMS signing-time issue", fn);
			return 0;
		}
		warnx("%s: GeneralizedTime instead of UTCtime", fn);
	}

	if (!x509_get_time(at, signtime)) {
		warnx("%s: failed to convert %s", fn, time_str);
		return 0;
	}

	return 1;
}

static int
cms_parse_validate_internal(struct cert **out_cert, const char *fn, int talid,
    const unsigned char *der, size_t len, const ASN1_OBJECT *oid, BIO *bio,
    unsigned char **res, size_t *rsz, time_t *signtime)
{
	struct cert			*cert = NULL;
	const unsigned char		*oder;
	char				 buf[128], obuf[128];
	const ASN1_OBJECT		*obj, *octype;
	ASN1_OCTET_STRING		*kid = NULL;
	CMS_ContentInfo			*cms;
	long				 version;
	STACK_OF(X509)			*certs = NULL;
	STACK_OF(X509_CRL)		*crls = NULL;
	STACK_OF(CMS_SignerInfo)	*sinfos;
	CMS_SignerInfo			*si;
	X509_ALGOR			*pdig, *psig;
	int				 i, nattrs, nid;
	int				 has_ct = 0, has_md = 0, has_st = 0;
	int				 rc = 0;

	assert(*out_cert == NULL);

	if (rsz != NULL)
		*rsz = 0;
	*signtime = 0;

	/* just fail for empty buffers, the warning was printed elsewhere */
	if (der == NULL)
		return 0;

	oder = der;
	if ((cms = d2i_CMS_ContentInfo(NULL, &der, len)) == NULL) {
		warnx("%s: RFC 6488: failed CMS parse", fn);
		goto out;
	}
	if (der != oder + len) {
		warnx("%s: %td bytes trailing garbage", fn, oder + len - der);
		goto out;
	}

	/*
	 * The CMS is self-signed with a signing certificate.
	 * Verify that the self-signage is correct.
	 */
	if (!CMS_verify(cms, NULL, NULL, bio, NULL,
	    CMS_NO_SIGNER_CERT_VERIFY)) {
		warnx("%s: CMS verification error", fn);
		goto out;
	}

	/* RFC 6488 section 3 verify the CMS */

	/* Should only return NULL if cms is not of type SignedData. */
	if ((sinfos = CMS_get0_SignerInfos(cms)) == NULL) {
		if ((obj = CMS_get0_type(cms)) == NULL) {
			warnx("%s: RFC 6488: missing content-type", fn);
			goto out;
		}
		OBJ_obj2txt(buf, sizeof(buf), obj, 1);
		warnx("%s: RFC 6488: no signerInfo in CMS object of type %s",
		    fn, buf);
		goto out;
	}
	if (sk_CMS_SignerInfo_num(sinfos) != 1) {
		warnx("%s: RFC 6488: CMS has multiple signerInfos", fn);
		goto out;
	}
	si = sk_CMS_SignerInfo_value(sinfos, 0);

	if (!CMS_get_version(cms, &version)) {
		warnx("%s: Failed to retrieve SignedData version", fn);
		goto out;
	}
	if (version != 3) {
		warnx("%s: SignedData version %ld != 3", fn, version);
		goto out;
	}
	if (!CMS_SignerInfo_get_version(si, &version)) {
		warnx("%s: Failed to retrieve SignerInfo version", fn);
		goto out;
	}
	if (version != 3) {
		warnx("%s: SignerInfo version %ld != 3", fn, version);
		goto out;
	}

	nattrs = CMS_signed_get_attr_count(si);
	if (nattrs <= 0) {
		warnx("%s: RFC 6488: error extracting signedAttrs", fn);
		goto out;
	}
	for (i = 0; i < nattrs; i++) {
		X509_ATTRIBUTE *attr;

		attr = CMS_signed_get_attr(si, i);
		if (attr == NULL || X509_ATTRIBUTE_count(attr) != 1) {
			warnx("%s: RFC 6488: bad signed attribute encoding",
			    fn);
			goto out;
		}

		obj = X509_ATTRIBUTE_get0_object(attr);
		if (obj == NULL) {
			warnx("%s: RFC 6488: bad signed attribute", fn);
			goto out;
		}
		if (OBJ_cmp(obj, cnt_type_oid) == 0) {
			if (has_ct++ != 0) {
				warnx("%s: RFC 6488: duplicate "
				    "signed attribute", fn);
				goto out;
			}
		} else if (OBJ_cmp(obj, msg_dgst_oid) == 0) {
			if (has_md++ != 0) {
				warnx("%s: RFC 6488: duplicate "
				    "signed attribute", fn);
				goto out;
			}
		} else if (OBJ_cmp(obj, sign_time_oid) == 0) {
			if (has_st++ != 0) {
				warnx("%s: RFC 6488: duplicate "
				    "signed attribute", fn);
				goto out;
			}
			if (!cms_get_signtime(fn, attr, signtime))
				goto out;
		} else {
			OBJ_obj2txt(buf, sizeof(buf), obj, 1);
			warnx("%s: RFC 6488: "
			    "CMS has unexpected signed attribute %s",
			    fn, buf);
			goto out;
		}
	}

	if (!has_ct || !has_md) {
		/* RFC 9589, section 4 */
		warnx("%s: RFC 6488: CMS missing required "
		    "signed attribute", fn);
		goto out;
	}

	if (!has_st) {
		/* RFC 9589, section 4 */
		warnx("%s: missing CMS signing-time attribute", fn);
		goto out;
	}

	if (CMS_unsigned_get_attr_count(si) != -1) {
		warnx("%s: RFC 6488: CMS has unsignedAttrs", fn);
		goto out;
	}

	/* Check digest and signature algorithms (RFC 7935) */
	CMS_SignerInfo_get0_algs(si, NULL, NULL, &pdig, &psig);

	X509_ALGOR_get0(&obj, NULL, NULL, pdig);
	nid = OBJ_obj2nid(obj);
	if (nid != NID_sha256) {
		warnx("%s: RFC 6488: wrong digest %s, want %s", fn,
		    nid2str(nid), LN_sha256);
		goto out;
	}
	X509_ALGOR_get0(&obj, NULL, NULL, psig);
	nid = OBJ_obj2nid(obj);
	/* RFC7935 last paragraph of section 2 specifies the allowed psig */
	if (experimental && nid == NID_ecdsa_with_SHA256) {
		if (verbose)
			warnx("%s: P-256 support is experimental", fn);
	} else if (nid != NID_rsaEncryption &&
	    nid != NID_sha256WithRSAEncryption) {
		warnx("%s: RFC 6488: wrong signature algorithm %s, want %s",
		    fn, nid2str(nid), LN_rsaEncryption);
		goto out;
	}

	/* RFC 6488 section 2.1.3.1: check the object's eContentType. */

	obj = CMS_get0_eContentType(cms);
	if (obj == NULL) {
		warnx("%s: RFC 6488 section 2.1.3.1: eContentType: "
		    "OID object is NULL", fn);
		goto out;
	}
	if (OBJ_cmp(obj, oid) != 0) {
		OBJ_obj2txt(buf, sizeof(buf), obj, 1);
		OBJ_obj2txt(obuf, sizeof(obuf), oid, 1);
		warnx("%s: RFC 6488 section 2.1.3.1: eContentType: "
		    "unknown OID: %s, want %s", fn, buf, obuf);
		goto out;
	}

	/* Compare content-type with eContentType */
	octype = CMS_signed_get0_data_by_OBJ(si, cnt_type_oid,
	    -3, V_ASN1_OBJECT);
	/*
	 * Since lastpos == -3, octype can be NULL for 4 reasons:
	 * 1. requested attribute OID is missing
	 * 2. signedAttrs contains multiple attributes with requested OID
	 * 3. attribute with requested OID has multiple values (malformed)
	 * 4. X509_ATTRIBUTE_get0_data() returned NULL. This is also malformed,
	 *    but libcrypto will create, sign, and verify such objects.
	 * Reasons 1 and 2 are excluded because has_ct == 1. We don't know which
	 * one of 3 or 4 we hit. Doesn't matter, drop the garbage on the floor.
	 */
	if (octype == NULL) {
		warnx("%s: RFC 6488, section 2.1.6.4.1: malformed value "
		    "for content-type attribute", fn);
		goto out;
	}
	if (OBJ_cmp(obj, octype) != 0) {
		OBJ_obj2txt(buf, sizeof(buf), obj, 1);
		OBJ_obj2txt(obuf, sizeof(obuf), octype, 1);
		warnx("%s: RFC 6488: eContentType does not match Content-Type "
		    "OID: %s, want %s", fn, buf, obuf);
		goto out;
	}

	/*
	 * Check that there are no CRLS in this CMS message.
	 * XXX - can only error check for OpenSSL >= 3.4.
	 */
	crls = CMS_get1_crls(cms);
	if (crls != NULL && sk_X509_CRL_num(crls) != 0) {
		warnx("%s: RFC 6488: CMS has CRLs", fn);
		goto out;
	}

	/*
	 * The self-signing certificate is further signed by the input
	 * signing authority according to RFC 6488, 2.1.4.
	 * We extract that certificate now for later verification.
	 */

	certs = CMS_get0_signers(cms);
	if (certs == NULL || sk_X509_num(certs) != 1) {
		warnx("%s: RFC 6488 section 2.1.4: eContent: "
		    "want 1 signer, have %d", fn, sk_X509_num(certs));
		goto out;
	}

	cert = cert_parse_ee_cert(fn, talid, sk_X509_value(certs, 0));
	if (cert == NULL)
		goto out;

	if (*signtime > cert->notafter)
		warnx("%s: dating issue: CMS signing-time after X.509 notAfter",
		    fn);

	if (CMS_SignerInfo_get0_signer_id(si, &kid, NULL, NULL) != 1 ||
	    kid == NULL) {
		warnx("%s: RFC 6488: could not extract SKI from SID", fn);
		goto out;
	}
	if (CMS_SignerInfo_cert_cmp(si, cert->x509) != 0) {
		warnx("%s: RFC 6488: wrong cert referenced by SignerInfo", fn);
		goto out;
	}

	if (!cms_extract_econtent(fn, cms, res, rsz))
		goto out;

	*out_cert = cert;
	cert = NULL;

	rc = 1;
 out:
	cert_free(cert);
	sk_X509_CRL_pop_free(crls, X509_CRL_free);
	sk_X509_free(certs);
	CMS_ContentInfo_free(cms);
	return rc;
}

/*
 * Parse and validate a self-signed CMS message.
 * Conforms to RFC 6488.
 * The eContentType of the message must be an oid object.
 * Return the eContent as a string and set "rsz" to be its length.
 */
unsigned char *
cms_parse_validate(struct cert **out_cert, const char *fn, int talid,
    const unsigned char *der, size_t derlen, const ASN1_OBJECT *oid,
    size_t *rsz, time_t *st)
{
	unsigned char *res = NULL;

	if (!cms_parse_validate_internal(out_cert, fn, talid, der, derlen, oid,
	    NULL, &res, rsz, st))
		return NULL;

	return res;
}

/*
 * Parse and validate a detached CMS signature.
 * bio must contain the original message, der must contain the CMS.
 * Return the 1 on success, 0 on failure.
 */
int
cms_parse_validate_detached(struct cert **out_cert, const char *fn, int talid,
    const unsigned char *der, size_t derlen, const ASN1_OBJECT *oid, BIO *bio,
    time_t *st)
{
	return cms_parse_validate_internal(out_cert, fn, talid, der, derlen,
	    oid, bio, NULL, NULL, st);
}
