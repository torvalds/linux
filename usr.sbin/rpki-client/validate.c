/*	$OpenBSD: validate.c,v 1.81 2025/08/24 11:52:20 tb Exp $ */
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

#include <arpa/inet.h>
#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "extern.h"

/*
 * Walk up the chain of certificates trying to match our AS number to
 * one of the allocations in that chain.
 * Returns 1 if covered or 0 if not.
 */
static int
valid_as(struct auth *a, uint32_t min, uint32_t max)
{
	int	 c;

	if (a == NULL)
		return 0;

	/* Does this certificate cover our AS number? */
	c = as_check_covered(min, max, a->cert->ases, a->cert->num_ases);
	if (c > 0)
		return 1;
	else if (c < 0)
		return 0;

	/* If it inherits, walk up the chain. */
	return valid_as(a->issuer, min, max);
}

/*
 * Walk up the chain of certificates (really just the last one, but in
 * the case of inheritance, the ones before) making sure that our IP
 * prefix is covered in the first non-inheriting specification.
 * Returns 1 if covered or 0 if not.
 */
static int
valid_ip(struct auth *a, enum afi afi,
    const unsigned char *min, const unsigned char *max)
{
	int	 c;

	if (a == NULL)
		return 0;

	/* Does this certificate cover our IP prefix? */
	c = ip_addr_check_covered(afi, min, max, a->cert->ips,
	    a->cert->num_ips);
	if (c > 0)
		return 1;
	else if (c < 0)
		return 0;

	/* If it inherits, walk up the chain. */
	return valid_ip(a->issuer, afi, min, max);
}

/*
 * Validate a non-TA certificate: make sure its IP and AS resources are
 * fully covered by those in the authority key (which must exist).
 * Returns 1 if valid, 0 otherwise.
 */
int
valid_cert(const char *fn, struct auth *a, const struct cert *cert)
{
	size_t		 i;
	uint32_t	 min, max;

	for (i = 0; i < cert->num_ases; i++) {
		if (cert->ases[i].type == CERT_AS_INHERIT)
			continue;

		if (cert->ases[i].type == CERT_AS_ID) {
			min = cert->ases[i].id;
			max = cert->ases[i].id;
		} else {
			min = cert->ases[i].range.min;
			max = cert->ases[i].range.max;
		}

		if (valid_as(a, min, max))
			continue;

		as_warn(fn, "RFC 6487: uncovered resource", &cert->ases[i]);
		return 0;
	}

	for (i = 0; i < cert->num_ips; i++) {
		if (cert->ips[i].type == CERT_IP_INHERIT)
			continue;

		if (valid_ip(a, cert->ips[i].afi, cert->ips[i].min,
		    cert->ips[i].max))
			continue;

		ip_warn(fn, "RFC 6487: uncovered resource", &cert->ips[i]);
		return 0;
	}

	return 1;
}

/*
 * Validate our ROA: check that the prefixes (ipAddrBlocks) are contained.
 * Returns 1 if valid, 0 otherwise.
 */
int
valid_roa(const char *fn, struct cert *cert, struct roa *roa)
{
	size_t	 i;
	char	 buf[64];

	for (i = 0; i < roa->num_ips; i++) {
		if (ip_addr_check_covered(roa->ips[i].afi, roa->ips[i].min,
		    roa->ips[i].max, cert->ips, cert->num_ips) > 0)
			continue;

		ip_addr_print(&roa->ips[i].addr, roa->ips[i].afi, buf,
		    sizeof(buf));
		warnx("%s: RFC 9582: uncovered IP: %s", fn, buf);
		return 0;
	}

	return 1;
}

/*
 * Validate our SPL: check that the asID is contained in the end-entity
 * certificate's resources.
 * Returns 1 if valid, 0 otherwise.
 */
int
valid_spl(const char *fn, struct cert *cert, struct spl *spl)
{
	if (as_check_covered(spl->asid, spl->asid, cert->ases,
	    cert->num_ases) > 0)
		return 1;

	warnx("%s: SPL: uncovered ASID: %u", fn, spl->asid);

	return 0;
}

/*
 * Validate a file by verifying the SHA256 hash of that file.
 * The file to check is passed as a file descriptor.
 * Returns 1 if hash matched, 0 otherwise. Closes fd when done.
 */
int
valid_filehash(int fd, const char *hash, size_t hlen)
{
	SHA256_CTX	ctx;
	char		filehash[SHA256_DIGEST_LENGTH];
	char		buffer[8192];
	ssize_t		nr;

	if (hlen != sizeof(filehash))
		errx(1, "bad hash size");

	if (fd == -1)
		return 0;

	SHA256_Init(&ctx);
	while ((nr = read(fd, buffer, sizeof(buffer))) > 0)
		SHA256_Update(&ctx, buffer, nr);
	close(fd);
	SHA256_Final(filehash, &ctx);

	if (memcmp(hash, filehash, sizeof(filehash)) != 0)
		return 0;
	return 1;
}

/*
 * Same as above but with a buffer instead of a fd.
 */
int
valid_hash(unsigned char *buf, size_t len, const char *hash, size_t hlen)
{
	char	filehash[SHA256_DIGEST_LENGTH];

	if (hlen != sizeof(filehash))
		errx(1, "bad hash size");

	if (buf == NULL || len == 0)
		return 0;

	if (!EVP_Digest(buf, len, filehash, NULL, EVP_sha256(), NULL))
		errx(1, "EVP_Digest failed");

	if (memcmp(hash, filehash, sizeof(filehash)) != 0)
		return 0;
	return 1;
}

/*
 * Validate that a filename only contains characters from the POSIX portable
 * filename character set [A-Za-z0-9._-], see IEEE Std 1003.1-2013, 3.278.
 */
int
valid_filename(const char *fn, size_t len)
{
	const unsigned char *c;
	size_t i;

	for (c = fn, i = 0; i < len; i++, c++)
		if (!isalnum(*c) && *c != '-' && *c != '_' && *c != '.')
			return 0;
	return 1;
}

/*
 * Validate a URI to make sure it is pure ASCII and does not point backwards
 * or doing some other silly tricks. To enforce the protocol pass either
 * https:// or rsync:// as proto, if NULL is passed no protocol is enforced.
 * Returns 1 if valid, 0 otherwise.
 */
int
valid_uri(const char *uri, size_t usz, const char *proto)
{
	size_t s;

	if (usz > MAX_URI_LENGTH)
		return 0;

	for (s = 0; s < usz; s++)
		if (!isalnum((unsigned char)uri[s]) &&
		    !ispunct((unsigned char)uri[s]))
			return 0;

	if (proto != NULL) {
		s = strlen(proto);
		if (s >= usz)
			return 0;
		if (strncasecmp(uri, proto, s) != 0)
			return 0;
	}

	/* do not allow files or directories to start with a '.' */
	if (strstr(uri, "/.") != NULL)
		return 0;

	return 1;
}

/*
 * Validate that a URI has the same host as the URI passed in proto.
 * Returns 1 if valid, 0 otherwise.
 */
int
valid_origin(const char *uri, const char *proto)
{
	const char *to;

	/* extract end of host from proto URI */
	to = strstr(proto, "://");
	if (to == NULL)
		return 0;
	to += strlen("://");
	if ((to = strchr(to, '/')) == NULL)
		return 0;

	/* compare hosts including the / for the start of the path section */
	if (strncasecmp(uri, proto, to - proto + 1) != 0)
		return 0;

	return 1;
}

/*
 * Walk the tree of known valid CA certificates until we find a certificate that
 * doesn't inherit. Build a chain of intermediates and use the non-inheriting
 * certificate as a trusted root by virtue of X509_V_FLAG_PARTIAL_CHAIN. The
 * RFC 3779 path validation needs a non-inheriting trust root to ensure that
 * all delegated resources are covered.
 */
static void
build_chain(const struct auth *a, STACK_OF(X509) **intermediates,
    STACK_OF(X509) **root)
{
	*intermediates = NULL;
	*root = NULL;

	/* XXX - this should be removed, but filemode relies on it. */
	if (a == NULL)
		return;

	if ((*intermediates = sk_X509_new_null()) == NULL)
		err(1, "sk_X509_new_null");
	if ((*root = sk_X509_new_null()) == NULL)
		err(1, "sk_X509_new_null");
	for (; a != NULL; a = a->issuer) {
		assert(a->cert->x509 != NULL);
		if (!a->any_inherits) {
			if (!sk_X509_push(*root, a->cert->x509))
				errx(1, "sk_X509_push");
			break;
		}
		if (!sk_X509_push(*intermediates, a->cert->x509))
			errx(1, "sk_X509_push");
	}
	assert(sk_X509_num(*root) == 1);
}

/*
 * Add the CRL based on the certs SKI value.
 * No need to insert any other CRL since those were already checked.
 */
static void
build_crls(const struct crl *crl, STACK_OF(X509_CRL) **crls)
{
	*crls = NULL;

	if (crl == NULL)
		return;
	if ((*crls = sk_X509_CRL_new_null()) == NULL)
		errx(1, "sk_X509_CRL_new_null");
	if (!sk_X509_CRL_push(*crls, crl->x509_crl))
		err(1, "sk_X509_CRL_push");
}

/*
 * Attempt to upgrade the generic 'certificate revoked' message to include
 * a timestamp.
 */
static void
pretty_revocation_time(X509 *x509, X509_CRL *crl, const char **errstr)
{
	static char		 buf[64];
	X509_REVOKED		*revoked;
	const ASN1_TIME		*atime;
	time_t			 t;

	if (X509_CRL_get0_by_cert(crl, &revoked, x509) != 1)
		return;
	if ((atime = X509_REVOKED_get0_revocationDate(revoked)) == NULL)
		return;
	if (!x509_get_time(atime, &t))
		return;

	snprintf(buf, sizeof(buf), "certificate revoked on %s", time2str(t));
	*errstr = buf;
}

/*
 * Validate the X509 certificate. Returns 1 for valid certificates,
 * returns 0 if there is a verify error and sets *errstr to the error
 * returned by X509_verify_cert_error_string().
 */
int
valid_x509(char *file, X509_STORE_CTX *store_ctx, X509 *x509, struct auth *a,
    struct crl *crl, const char **errstr)
{
	X509_VERIFY_PARAM	*params;
	ASN1_OBJECT		*cp_oid;
	STACK_OF(X509)		*intermediates, *root;
	STACK_OF(X509_CRL)	*crls = NULL;
	unsigned long		 flags;
	int			 error;

	*errstr = NULL;
	build_chain(a, &intermediates, &root);
	build_crls(crl, &crls);

	assert(store_ctx != NULL);
	assert(x509 != NULL);
	if (!X509_STORE_CTX_init(store_ctx, NULL, x509, NULL))
		err(1, "X509_STORE_CTX_init");

	if ((params = X509_STORE_CTX_get0_param(store_ctx)) == NULL)
		errx(1, "X509_STORE_CTX_get0_param");
	if ((cp_oid = OBJ_dup(certpol_oid)) == NULL)
		err(1, "OBJ_dup");
	if (!X509_VERIFY_PARAM_add0_policy(params, cp_oid))
		err(1, "X509_VERIFY_PARAM_add0_policy");
	X509_VERIFY_PARAM_set_time(params, get_current_time());

	flags = X509_V_FLAG_CRL_CHECK;
	flags |= X509_V_FLAG_PARTIAL_CHAIN;
	flags |= X509_V_FLAG_POLICY_CHECK;
	flags |= X509_V_FLAG_EXPLICIT_POLICY;
	flags |= X509_V_FLAG_INHIBIT_MAP;
	X509_STORE_CTX_set_flags(store_ctx, flags);
	X509_STORE_CTX_set_depth(store_ctx, MAX_CERT_DEPTH);
	/*
	 * See the comment above build_chain() for details on what's happening
	 * here. The nomenclature in this API is dubious and poorly documented.
	 */
	X509_STORE_CTX_set0_untrusted(store_ctx, intermediates);
	X509_STORE_CTX_set0_trusted_stack(store_ctx, root);
	X509_STORE_CTX_set0_crls(store_ctx, crls);

	if (X509_verify_cert(store_ctx) <= 0) {
		error = X509_STORE_CTX_get_error(store_ctx);
		*errstr = X509_verify_cert_error_string(error);
		if (filemode && error == X509_V_ERR_CERT_REVOKED)
			pretty_revocation_time(x509, crl->x509_crl, errstr);
		X509_STORE_CTX_cleanup(store_ctx);
		sk_X509_free(intermediates);
		sk_X509_free(root);
		sk_X509_CRL_free(crls);
		return 0;
	}

	X509_STORE_CTX_cleanup(store_ctx);
	sk_X509_free(intermediates);
	sk_X509_free(root);
	sk_X509_CRL_free(crls);
	return 1;
}

/*
 * Validate our RSC: check that all items in the ResourceBlock are contained.
 * Returns 1 if valid, 0 otherwise.
 */
int
valid_rsc(const char *fn, struct cert *cert, struct rsc *rsc)
{
	size_t		i;
	uint32_t	min, max;

	for (i = 0; i < rsc->num_ases; i++) {
		if (rsc->ases[i].type == CERT_AS_ID) {
			min = rsc->ases[i].id;
			max = rsc->ases[i].id;
		} else {
			min = rsc->ases[i].range.min;
			max = rsc->ases[i].range.max;
		}

		if (as_check_covered(min, max, cert->ases, cert->num_ases) > 0)
			continue;

		as_warn(fn, "RSC ResourceBlock uncovered", &rsc->ases[i]);
		return 0;
	}

	for (i = 0; i < rsc->num_ips; i++) {
		if (ip_addr_check_covered(rsc->ips[i].afi, rsc->ips[i].min,
		    rsc->ips[i].max, cert->ips, cert->num_ips) > 0)
			continue;

		ip_warn(fn, "RSC ResourceBlock uncovered", &rsc->ips[i]);
		return 0;
	}

	return 1;
}

int
valid_econtent_version(const char *fn, const ASN1_INTEGER *aint,
    uint64_t expected)
{
	uint64_t version;

	if (aint == NULL) {
		if (expected == 0)
			return 1;
		warnx("%s: unexpected version 0", fn);
		return 0;
	}

	if (!ASN1_INTEGER_get_uint64(&version, aint)) {
		warnx("%s: ASN1_INTEGER_get_uint64 failed", fn);
		return 0;
	}

	if (version == 0) {
		warnx("%s: incorrect encoding for version 0", fn);
		return 0;
	}

	if (version != expected) {
		warnx("%s: unexpected version (expected %llu, got %llu)", fn,
		    (unsigned long long)expected, (unsigned long long)version);
		return 0;
	}

	return 1;
}

/*
 * Validate the ASPA: check that the customerASID is contained.
 * Returns 1 if valid, 0 otherwise.
 */
int
valid_aspa(const char *fn, struct cert *cert, struct aspa *aspa)
{

	if (as_check_covered(aspa->custasid, aspa->custasid,
	    cert->ases, cert->num_ases) > 0)
		return 1;

	warnx("%s: ASPA: uncovered Customer ASID: %u", fn, aspa->custasid);

	return 0;
}

/*
 * Validate Geofeed prefixes: check that the prefixes are contained.
 * Returns 1 if valid, 0 otherwise.
 */
int
valid_geofeed(const char *fn, struct cert *cert, struct geofeed *g)
{
	size_t	 i;
	char	 buf[64];

	for (i = 0; i < g->num_geoips; i++) {
		if (ip_addr_check_covered(g->geoips[i].ip->afi,
		    g->geoips[i].ip->min, g->geoips[i].ip->max, cert->ips,
		    cert->num_ips) > 0)
			continue;

		ip_addr_print(&g->geoips[i].ip->ip, g->geoips[i].ip->afi, buf,
		    sizeof(buf));
		warnx("%s: Geofeed: uncovered IP: %s", fn, buf);
		return 0;
	}

	return 1;
}

/*
 * Validate whether a given string is a valid UUID.
 * Returns 1 if valid, 0 otherwise.
 */
int
valid_uuid(const char *s)
{
	int n = 0;

	while (1) {
		switch (n) {
		case 8:
		case 13:
		case 18:
		case 23:
			if (s[n] != '-')
				return 0;
			break;
		/* Check UUID is version 4 */
		case 14:
			if (s[n] != '4')
				return 0;
			break;
		/* Check UUID variant is 1 */
		case 19:
			if (s[n] != '8' && s[n] != '9' && s[n] != 'a' &&
			    s[n] != 'A' && s[n] != 'b' && s[n] != 'B')
				return 0;
			break;
		case 36:
			return s[n] == '\0';
		default:
			if (!isxdigit((unsigned char)s[n]))
				return 0;
			break;
		}
		n++;
	}
}
