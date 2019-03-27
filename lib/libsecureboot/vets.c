/*-
 * Copyright (c) 2017-2018, Juniper Networks, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/**
 * @file vets.c - trust store
 * @brief verify signatures
 *
 * We leverage code from BearSSL www.bearssl.org
 */

#include <sys/time.h>
#include <stdarg.h>
#define NEED_BRSSL_H
#include "libsecureboot-priv.h"
#include <brssl.h>
#include <ta.h>

#ifndef TRUST_ANCHOR_STR
# define TRUST_ANCHOR_STR ta_PEM
#endif

#define SECONDS_PER_DAY		86400
#define X509_DAYS_TO_UTC0	719528

int DebugVe = 0;

typedef VECTOR(br_x509_certificate) cert_list;
typedef VECTOR(hash_data) digest_list;

static anchor_list trust_anchors = VEC_INIT;
static anchor_list forbidden_anchors = VEC_INIT;
static digest_list forbidden_digests = VEC_INIT;

void
ve_debug_set(int n)
{
	DebugVe = n;
}

static char ebuf[512];

char *
ve_error_get(void)
{
	return (ebuf);
}

int
ve_error_set(const char *fmt, ...)
{
	int rc;
	va_list ap;

	va_start(ap, fmt);
	ebuf[0] = '\0';
	rc = 0;
	if (fmt) {
#ifdef STAND_H
		vsprintf(ebuf, fmt, ap); /* no vsnprintf in libstand */
		ebuf[sizeof(ebuf) - 1] = '\0';
		rc = strlen(ebuf);
#else
		rc = vsnprintf(ebuf, sizeof(ebuf), fmt, ap);
#endif
	}
	va_end(ap);
	return (rc);
}

/* this is the time we use for verifying certs */
static time_t ve_utc = 0;

/**
 * @brief
 * set ve_utc used for certificate verification
 *
 * @param[in] utc
 *	time - ignored unless greater than current value.
 */
void
ve_utc_set(time_t utc)
{
	if (utc > ve_utc) {
		DEBUG_PRINTF(2, ("Set ve_utc=%jd\n", (intmax_t)utc));
		ve_utc = utc;
	}
}

static void
free_cert_contents(br_x509_certificate *xc)
{
	xfree(xc->data);
}

/* ASN parsing related defines */
#define ASN1_PRIMITIVE_TAG 0x1F
#define ASN1_INF_LENGTH    0x80
#define ASN1_LENGTH_MASK   0x7F

/*
 * Get TBS part of certificate.
 * Since BearSSL doesn't provide any API to do this,
 * it has to be implemented here.
 */
static void*
X509_to_tbs(unsigned char* cert, size_t* output_size)
{
	unsigned char *result;
	size_t tbs_size;
	int size, i;

	if (cert == NULL)
		return (NULL);

	/* Strip two sequences to get to the TBS section */
	for (i = 0; i < 2; i++) {
		/*
		 * XXX: We don't need to support extended tags since
		 * they should not be present in certificates.
		 */
		if ((*cert & ASN1_PRIMITIVE_TAG) == ASN1_PRIMITIVE_TAG)
			return (NULL);

		cert++;

		if (*cert == ASN1_INF_LENGTH)
			return (NULL);

		size = *cert & ASN1_LENGTH_MASK;
		tbs_size = 0;

		/* Size can either be stored on a single or multiple bytes */
		if (*cert & (ASN1_LENGTH_MASK + 1)) {
			cert++;
			while (*cert == 0 && size > 0) {
				cert++;
				size--;
			}
			while (size-- > 0) {
				tbs_size <<= 8;
				tbs_size |= *(cert++);
			}
		}
		if (i == 0)
			result = cert;
	}
	tbs_size += (cert - result);

	if (output_size != NULL)
		*output_size = tbs_size;

	return (result);
}

void
ve_forbidden_digest_add(hash_data *digest, size_t num)
{
	while (num--)
		VEC_ADD(forbidden_digests, digest[num]);
}

static size_t
ve_anchors_add(br_x509_certificate *xcs, size_t num, anchor_list *anchors)
{
	br_x509_trust_anchor ta;
	size_t u;

	for (u = 0; u < num; u++) {
		if (certificate_to_trust_anchor_inner(&ta, &xcs[u]) < 0) {
			break;
		}
		VEC_ADD(*anchors, ta);
	}
	return (u);
}

/**
 * @brief
 * add certs to our trust store
 */
size_t
ve_trust_anchors_add(br_x509_certificate *xcs, size_t num)
{
	return (ve_anchors_add(xcs, num, &trust_anchors));
}

size_t
ve_forbidden_anchors_add(br_x509_certificate *xcs, size_t num)
{
	return (ve_anchors_add(xcs, num, &forbidden_anchors));
}

/**
 * @brief
 * initialize our trust_anchors from ta_PEM
 */
int
ve_trust_init(void)
{
#ifdef TRUST_ANCHOR_STR
	br_x509_certificate *xcs;
#endif
	static int once = -1;
	size_t num;

	if (once >= 0)
		return (once);

	ve_utc_set(time(NULL));
#ifdef BUILD_UTC
	ve_utc_set(BUILD_UTC);		/* just in case */
#endif
	ve_error_set(NULL);		/* make sure it is empty */
#ifdef VE_PCR_SUPPORT
	ve_pcr_init();
#endif

#ifdef TRUST_ANCHOR_STR
	xcs = parse_certificates(__DECONST(unsigned char *, TRUST_ANCHOR_STR),
	    sizeof(TRUST_ANCHOR_STR), &num);
	if (xcs != NULL)
		num = ve_trust_anchors_add(xcs, num);
#endif
	once = (int) VEC_LEN(trust_anchors);

	return (once);
}

/**
 * if we can verify the certificate chain in "certs",
 * return the public key and if "xcp" is !NULL the associated
 * certificate
 */
static br_x509_pkey *
verify_signer_xcs(br_x509_certificate *xcs,
    size_t num,
    br_name_element *elts, size_t num_elts,
    anchor_list *anchors)
{
	br_x509_minimal_context mc;
	br_x509_certificate *xc;
	size_t u;
	cert_list chain = VEC_INIT;
	const br_x509_pkey *tpk;
	br_x509_pkey *pk;
	unsigned int usages;
	int err;

	DEBUG_PRINTF(5, ("verify_signer: %zu certs in chain\n", num));
	VEC_ADDMANY(chain, xcs, num);
	if (VEC_LEN(chain) == 0) {
		ve_error_set("ERROR: no/invalid certificate chain\n");
		return (NULL);
	}

	DEBUG_PRINTF(5, ("verify_signer: %zu trust anchors\n",
		VEC_LEN(*anchors)));

	br_x509_minimal_init(&mc, &br_sha256_vtable,
	    &VEC_ELT(*anchors, 0),
	    VEC_LEN(*anchors));
#ifdef VE_ECDSA_SUPPORT
	br_x509_minimal_set_ecdsa(&mc,
	    &br_ec_prime_i31, &br_ecdsa_i31_vrfy_asn1);
#endif
#ifdef VE_RSA_SUPPORT
	br_x509_minimal_set_rsa(&mc, &br_rsa_i31_pkcs1_vrfy);
#endif
#if defined(UNIT_TEST) && defined(VE_DEPRECATED_RSA_SHA1_SUPPORT)
	/* This is deprecated! do not enable unless you absoultely have to */
	br_x509_minimal_set_hash(&mc, br_sha1_ID, &br_sha1_vtable);
#endif
	br_x509_minimal_set_hash(&mc, br_sha256_ID, &br_sha256_vtable);
#ifdef VE_SHA384_SUPPORT
	br_x509_minimal_set_hash(&mc, br_sha384_ID, &br_sha384_vtable);
#endif
#ifdef VE_SHA512_SUPPORT
	br_x509_minimal_set_hash(&mc, br_sha512_ID, &br_sha512_vtable);
#endif
	br_x509_minimal_set_name_elements(&mc, elts, num_elts);

#ifdef _STANDALONE
	/*
	 * Clock is probably bogus so we use ve_utc.
	 */
	mc.days = (ve_utc / SECONDS_PER_DAY) + X509_DAYS_TO_UTC0;
	mc.seconds = (ve_utc % SECONDS_PER_DAY);
#endif

	mc.vtable->start_chain(&mc.vtable, NULL);
	for (u = 0; u < VEC_LEN(chain); u ++) {
		xc = &VEC_ELT(chain, u);
		mc.vtable->start_cert(&mc.vtable, xc->data_len);
		mc.vtable->append(&mc.vtable, xc->data, xc->data_len);
		mc.vtable->end_cert(&mc.vtable);
		switch (mc.err) {
		case 0:
		case BR_ERR_X509_OK:
		case BR_ERR_X509_EXPIRED:
			break;
		default:
			printf("u=%zu mc.err=%d\n", u, mc.err);
			break;
		}
	}
	err = mc.vtable->end_chain(&mc.vtable);
	pk = NULL;
	if (err) {
		ve_error_set("Validation failed, err = %d", err);
	} else {
		tpk = mc.vtable->get_pkey(&mc.vtable, &usages);
		if (tpk != NULL) {
			pk = xpkeydup(tpk);
		}
	}
	VEC_CLEAR(chain);
	return (pk);
}

/*
 * Check if digest of one of the certificates from verified chain
 * is present in the forbidden database.
 * Since UEFI allows to store three types of digests
 * all of them have to be checked separately.
 */
static int
check_forbidden_digests(br_x509_certificate *xcs, size_t num)
{
	unsigned char sha256_digest[br_sha256_SIZE];
	unsigned char sha384_digest[br_sha384_SIZE];
	unsigned char sha512_digest[br_sha512_SIZE];
	void *tbs;
	hash_data *digest;
	br_hash_compat_context ctx;
	const br_hash_class *md;
	size_t tbs_len, i;
	int have_sha256, have_sha384, have_sha512;

	if (VEC_LEN(forbidden_digests) == 0)
		return (0);

	/*
	 * Iterate through certificates, extract their To-Be-Signed section,
	 * and compare its digest against the ones in the forbidden database.
	 */
	while (num--) {
		tbs = X509_to_tbs(xcs[num].data, &tbs_len);
		if (tbs == NULL) {
			printf("Failed to obtain TBS part of certificate\n");
			return (1);
		}
		have_sha256 = have_sha384 = have_sha512 = 0;

		for (i = 0; i < VEC_LEN(forbidden_digests); i++) {
			digest = &VEC_ELT(forbidden_digests, i);
			switch (digest->hash_size) {
			case br_sha256_SIZE:
				if (!have_sha256) {
					have_sha256 = 1;
					md = &br_sha256_vtable;
					md->init(&ctx.vtable);
					md->update(&ctx.vtable, tbs, tbs_len);
					md->out(&ctx.vtable, sha256_digest);
				}
				if (!memcmp(sha256_digest,
					digest->data,
					br_sha256_SIZE))
					return (1);

				break;
			case br_sha384_SIZE:
				if (!have_sha384) {
					have_sha384 = 1;
					md = &br_sha384_vtable;
					md->init(&ctx.vtable);
					md->update(&ctx.vtable, tbs, tbs_len);
					md->out(&ctx.vtable, sha384_digest);
				}
				if (!memcmp(sha384_digest,
					digest->data,
					br_sha384_SIZE))
					return (1);

				break;
			case br_sha512_SIZE:
				if (!have_sha512) {
					have_sha512 = 1;
					md = &br_sha512_vtable;
					md->init(&ctx.vtable);
					md->update(&ctx.vtable, tbs, tbs_len);
					md->out(&ctx.vtable, sha512_digest);
				}
				if (!memcmp(sha512_digest,
					digest->data,
					br_sha512_SIZE))
					return (1);

				break;
			}
		}
	}

	return (0);
}

static br_x509_pkey *
verify_signer(const char *certs,
    br_name_element *elts, size_t num_elts)
{
	br_x509_certificate *xcs;
	br_x509_pkey *pk;
	size_t num;

	pk = NULL;

	ve_trust_init();
	xcs = read_certificates(certs, &num);
	if (xcs == NULL) {
		ve_error_set("cannot read certificates\n");
		return (NULL);
	}

	/*
	 * Check if either
	 * 1. There is a direct match between cert from forbidden_anchors
	 * and a cert from chain.
	 * 2. CA that signed the chain is found in forbidden_anchors.
	 */
	if (VEC_LEN(forbidden_anchors) > 0)
		pk = verify_signer_xcs(xcs, num, elts, num_elts, &forbidden_anchors);
	if (pk != NULL) {
		ve_error_set("Certificate is on forbidden list\n");
		xfreepkey(pk);
		pk = NULL;
		goto out;
	}

	pk = verify_signer_xcs(xcs, num, elts, num_elts, &trust_anchors);
	if (pk == NULL)
		goto out;

	/*
	 * Check if hash of tbs part of any certificate in chain
	 * is on the forbidden list.
	 */
	if (check_forbidden_digests(xcs, num)) {
		ve_error_set("Certificate hash is on forbidden list\n");
		xfreepkey(pk);
		pk = NULL;
	}
out:
	free_certificates(xcs, num);
	return (pk);
}

/**
 * we need a hex digest including trailing newline below
 */
char *
hexdigest(char *buf, size_t bufsz, unsigned char *foo, size_t foo_len)
{
	char const hex2ascii[] = "0123456789abcdef";
	size_t i;

	/* every binary byte is 2 chars in hex + newline + null  */
	if (bufsz < (2 * foo_len) + 2)
		return (NULL);

	for (i = 0; i < foo_len; i++) {
		buf[i * 2] = hex2ascii[foo[i] >> 4];
		buf[i * 2 + 1] = hex2ascii[foo[i] & 0x0f];
	}

	buf[i * 2] = 0x0A; /* we also want a newline */
	buf[i * 2 + 1] = '\0';

	return (buf);
}

/**
 * @brief
 * verify file against sigfile using pk
 *
 * When we generated the signature in sigfile,
 * we hashed (sha256) file, and sent that to signing server
 * which hashed (sha256) that hash.
 *
 * To verify we need to replicate that result.
 *
 * @param[in] pk
 *	br_x509_pkey
 *
 * @paramp[in] file
 *	file to be verified
 *
 * @param[in] sigfile
 * 	signature (PEM encoded)
 *
 * @return NULL on error, otherwise content of file.
 */
#ifdef VE_ECDSA_SUPPORT
static unsigned char *
verify_ec(br_x509_pkey *pk, const char *file, const char *sigfile)
{
	char hexbuf[br_sha512_SIZE * 2 + 2];
	unsigned char rhbuf[br_sha512_SIZE];
	char *hex;
	br_sha256_context ctx;
	unsigned char *fcp, *scp;
	size_t flen, slen, plen;
	pem_object *po;
	const br_ec_impl *ec;
	br_ecdsa_vrfy vrfy;

	if ((fcp = read_file(file, &flen)) == NULL)
		return (NULL);
	if ((scp = read_file(sigfile, &slen)) == NULL) {
		free(fcp);
		return (NULL);
	}
	if ((po = decode_pem(scp, slen, &plen)) == NULL) {
		free(fcp);
		free(scp);
		return (NULL);
	}
	br_sha256_init(&ctx);
	br_sha256_update(&ctx, fcp, flen);
	br_sha256_out(&ctx, rhbuf);
	hex = hexdigest(hexbuf, sizeof(hexbuf), rhbuf, br_sha256_SIZE);
	/* now hash that */
	if (hex) {
		br_sha256_init(&ctx);
		br_sha256_update(&ctx, hex, strlen(hex));
		br_sha256_out(&ctx, rhbuf);
	}
	ec = br_ec_get_default();
	vrfy = br_ecdsa_vrfy_asn1_get_default();
	if (!vrfy(ec, rhbuf, br_sha256_SIZE, &pk->key.ec, po->data,
		po->data_len)) {
		free(fcp);
		fcp = NULL;
	}
	free(scp);
	return (fcp);
}
#endif

#if defined(VE_RSA_SUPPORT) || defined(VE_OPENPGP_SUPPORT)
/**
 * @brief verify an rsa digest
 *
 * @return 0 on failure
 */
int
verify_rsa_digest (br_rsa_public_key *pkey,
    const unsigned char *hash_oid,
    unsigned char *mdata, size_t mlen,
    unsigned char *sdata, size_t slen)
{
	br_rsa_pkcs1_vrfy vrfy;
	unsigned char vhbuf[br_sha512_SIZE];

	vrfy = br_rsa_pkcs1_vrfy_get_default();

	if (!vrfy(sdata, slen, hash_oid, mlen, pkey, vhbuf) ||
	    memcmp(vhbuf, mdata, mlen) != 0) {
		return (0);		/* fail */
	}
	return (1);			/* ok */
}
#endif

/**
 * @brief
 * verify file against sigfile using pk
 *
 * When we generated the signature in sigfile,
 * we hashed (sha256) file, and sent that to signing server
 * which hashed (sha256) that hash.
 *
 * Or (deprecated) we simply used sha1 hash directly.
 *
 * To verify we need to replicate that result.
 *
 * @param[in] pk
 *	br_x509_pkey
 *
 * @paramp[in] file
 *	file to be verified
 *
 * @param[in] sigfile
 * 	signature (PEM encoded)
 *
 * @return NULL on error, otherwise content of file.
 */
#ifdef VE_RSA_SUPPORT
static unsigned char *
verify_rsa(br_x509_pkey *pk,  const char *file, const char *sigfile)
{
	unsigned char rhbuf[br_sha512_SIZE];
	const unsigned char *hash_oid;
	const br_hash_class *md;
	br_hash_compat_context mctx;
	unsigned char *fcp, *scp;
	size_t flen, slen, plen, hlen;
	pem_object *po;

	if ((fcp = read_file(file, &flen)) == NULL)
		return (NULL);
	if ((scp = read_file(sigfile, &slen)) == NULL) {
		free(fcp);
		return (NULL);
	}
	if ((po = decode_pem(scp, slen, &plen)) == NULL) {
		free(fcp);
		free(scp);
		return (NULL);
	}

	switch (po->data_len) {
#if defined(UNIT_TEST) && defined(VE_DEPRECATED_RSA_SHA1_SUPPORT)
	case 256:
		// this is our old deprecated sig method
		md = &br_sha1_vtable;
		hlen = br_sha1_SIZE;
		hash_oid = BR_HASH_OID_SHA1;
		break;
#endif
	default:
		md = &br_sha256_vtable;
		hlen = br_sha256_SIZE;
		hash_oid = BR_HASH_OID_SHA256;
		break;
	}
	md->init(&mctx.vtable);
	md->update(&mctx.vtable, fcp, flen);
	md->out(&mctx.vtable, rhbuf);
	if (!verify_rsa_digest(&pk->key.rsa, hash_oid,
		rhbuf, hlen, po->data, po->data_len)) {
		free(fcp);
		fcp = NULL;
	}
	free(scp);
	return (fcp);
}
#endif

/**
 * @brief
 * verify a signature and return content of signed file
 *
 * @param[in] sigfile
 * 	file containing signature
 * 	we derrive path of signed file and certificate change from
 * 	this.
 *
 * @param[in] flags
 * 	only bit 1 significant so far
 *
 * @return NULL on error otherwise content of signed file
 */
unsigned char *
verify_sig(const char *sigfile, int flags)
{
	br_x509_pkey *pk;
	br_name_element cn;
	char cn_buf[80];
	unsigned char cn_oid[4];
	char pbuf[MAXPATHLEN];
	char *cp;
	unsigned char *ucp;
	size_t n;

	DEBUG_PRINTF(5, ("verify_sig: %s\n", sigfile));
	n = strlcpy(pbuf, sigfile, sizeof(pbuf));
	if (n > (sizeof(pbuf) - 5) || strcmp(&sigfile[n - 3], "sig") != 0)
		return (NULL);
	cp = strcpy(&pbuf[n - 3], "certs");
	/*
	 * We want the commonName field
	 * the OID we want is 2,5,4,3 - but DER encoded
	 */
	cn_oid[0] = 3;
	cn_oid[1] = 0x55;
	cn_oid[2] = 4;
	cn_oid[3] = 3;
	cn.oid = cn_oid;
	cn.buf = cn_buf;
	cn.len = sizeof(cn_buf);

	pk = verify_signer(pbuf, &cn, 1);
	if (!pk) {
		printf("cannot verify: %s: %s\n", pbuf, ve_error_get());
		return (NULL);
	}
	for (; cp > pbuf; cp--) {
		if (*cp == '.') {
			*cp = '\0';
			break;
		}
	}
	switch (pk->key_type) {
#ifdef VE_ECDSA_SUPPORT
	case BR_KEYTYPE_EC:
		ucp = verify_ec(pk, pbuf, sigfile);
		break;
#endif
#ifdef VE_RSA_SUPPORT
	case BR_KEYTYPE_RSA:
		ucp = verify_rsa(pk, pbuf, sigfile);
		break;
#endif
	default:
		ucp = NULL;		/* not supported */
	}
	xfreepkey(pk);
	if (!ucp) {
		printf("Unverified %s (%s)\n", pbuf,
		    cn.status ? cn_buf : "unknown");
	} else if ((flags & 1) != 0) {
		printf("Verified %s signed by %s\n", pbuf,
		    cn.status ? cn_buf : "someone we trust");
	}
	return (ucp);
}


/**
 * @brief verify hash matches
 *
 * We have finished hashing a file,
 * see if we got the desired result.
 *
 * @param[in] ctx
 *	pointer to hash context
 *
 * @param[in] md
 *	pointer to hash class
 *
 * @param[in] path
 *	name of the file we are checking
 *
 * @param[in] want
 *	the expected result
 *
 * @param[in] hlen
 *	size of hash output
 *
 * @return 0 on success
 */
int
ve_check_hash(br_hash_compat_context *ctx, const br_hash_class *md,
    const char *path, const char *want, size_t hlen)
{
	char hexbuf[br_sha512_SIZE * 2 + 2];
	unsigned char hbuf[br_sha512_SIZE];
	char *hex;
	int rc;
	int n;

	md->out(&ctx->vtable, hbuf);
#ifdef VE_PCR_SUPPORT
	ve_pcr_update(hbuf, hlen);
#endif
	hex = hexdigest(hexbuf, sizeof(hexbuf), hbuf, hlen);
	if (!hex)
		return (VE_FINGERPRINT_WRONG);
	n = 2*hlen;
	if ((rc = strncmp(hex, want, n))) {
		ve_error_set("%s: %.*s != %.*s", path, n, hex, n, want);
		rc = VE_FINGERPRINT_WRONG;
	}
	return (rc ? rc : VE_FINGERPRINT_OK);
}

#ifdef VE_HASH_KAT_STR
static int
test_hash(const br_hash_class *md, size_t hlen,
    const char *hname, const char *s, size_t slen, const char *want)
{
	br_hash_compat_context mctx;

	md->init(&mctx.vtable);
	md->update(&mctx.vtable, s, slen);
	return (ve_check_hash(&mctx, md, hname, want, hlen) != VE_FINGERPRINT_OK);
}

#endif

#define ve_test_hash(n, N) \
	printf("Testing hash: " #n "\t\t\t\t%s\n", \
	    test_hash(&br_ ## n ## _vtable, br_ ## n ## _SIZE, #n, \
	    VE_HASH_KAT_STR, sizeof(VE_HASH_KAT_STR), \
	    vh_ ## N) ? "Failed" : "Passed")

/**
 * @brief
 * run self tests on hash and signature verification
 *
 * Test that the hash methods (SHA1 and SHA256) work.
 * Test that we can verify a certificate for each supported
 * Root CA.
 *
 * @return cached result.
 */
int
ve_self_tests(void)
{
	static int once = -1;
#ifdef VERIFY_CERTS_STR
	br_x509_certificate *xcs;
	br_x509_pkey *pk;
	br_name_element cn;
	char cn_buf[80];
	unsigned char cn_oid[4];
	size_t num;
	size_t u;
#endif

	if (once >= 0)
		return (once);
	once = 0;

	DEBUG_PRINTF(5, ("Self tests...\n"));
#ifdef VE_HASH_KAT_STR
#ifdef VE_SHA1_SUPPORT
	ve_test_hash(sha1, SHA1);
#endif
#ifdef VE_SHA256_SUPPORT
	ve_test_hash(sha256, SHA256);
#endif
#ifdef VE_SHA384_SUPPORT
	ve_test_hash(sha384, SHA384);
#endif
#ifdef VE_SHA512_SUPPORT
	ve_test_hash(sha512, SHA512);
#endif
#endif
#ifdef VERIFY_CERTS_STR
	xcs = parse_certificates(__DECONST(unsigned char *, VERIFY_CERTS_STR),
	    sizeof(VERIFY_CERTS_STR), &num);
	if (xcs == NULL)
		return (0);
	/*
	 * We want the commonName field
	 * the OID we want is 2,5,4,3 - but DER encoded
	 */
	cn_oid[0] = 3;
	cn_oid[1] = 0x55;
	cn_oid[2] = 4;
	cn_oid[3] = 3;
	cn.oid = cn_oid;
	cn.buf = cn_buf;

	for (u = 0; u < num; u ++) {
		cn.len = sizeof(cn_buf);
		if ((pk = verify_signer_xcs(&xcs[u], 1, &cn, 1, &trust_anchors)) != NULL) {
			free_cert_contents(&xcs[u]);
			once++;
			printf("Testing verify certificate: %s\tPassed\n",
			    cn.status ? cn_buf : "");
			xfreepkey(pk);
		}
	}
	if (!once)
		printf("Testing verify certificate:\t\t\tFailed\n");
	xfree(xcs);
#else
	printf("No X.509 self tests\n");
#endif	/* VERIFY_CERTS_STR */
#ifdef VE_OPENPGP_SUPPORT
	if (!openpgp_self_tests())
		once++;
#endif
	return (once);
}
