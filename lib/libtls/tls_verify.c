/* $OpenBSD: tls_verify.c,v 1.32 2024/12/10 08:40:30 tb Exp $ */
/*
 * Copyright (c) 2014 Jeremie Courreges-Anglas <jca@openbsd.org>
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

#include <sys/socket.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <string.h>

#include <openssl/x509v3.h>

#include <tls.h>
#include "tls_internal.h"

static int
tls_match_name(const char *cert_name, const char *name)
{
	const char *cert_domain, *domain, *next_dot;

	if (strcasecmp(cert_name, name) == 0)
		return 0;

	/* Wildcard match? */
	if (cert_name[0] == '*') {
		/*
		 * Valid wildcards:
		 * - "*.domain.tld"
		 * - "*.sub.domain.tld"
		 * - etc.
		 * Reject "*.tld".
		 * No attempt to prevent the use of eg. "*.co.uk".
		 */
		cert_domain = &cert_name[1];
		/* Disallow "*"  */
		if (cert_domain[0] == '\0')
			return -1;
		/* Disallow "*foo" */
		if (cert_domain[0] != '.')
			return -1;
		/* Disallow "*.." */
		if (cert_domain[1] == '.')
			return -1;
		next_dot = strchr(&cert_domain[1], '.');
		/* Disallow "*.bar" */
		if (next_dot == NULL)
			return -1;
		/* Disallow "*.bar.." */
		if (next_dot[1] == '.')
			return -1;

		domain = strchr(name, '.');

		/* No wildcard match against a name with no host part. */
		if (name[0] == '.')
			return -1;
		/* No wildcard match against a name with no domain part. */
		if (domain == NULL || strlen(domain) == 1)
			return -1;

		if (strcasecmp(cert_domain, domain) == 0)
			return 0;
	}

	return -1;
}

/*
 * See RFC 5280 section 4.2.1.6 for SubjectAltName details.
 * alt_match is set to 1 if a matching alternate name is found.
 * alt_exists is set to 1 if any known alternate name exists in the certificate.
 */
static int
tls_check_subject_altname(struct tls *ctx, X509 *cert, const char *name,
    int *alt_match, int *alt_exists)
{
	STACK_OF(GENERAL_NAME) *altname_stack = NULL;
	union tls_addr addrbuf;
	int addrlen, type;
	int count, i;
	int critical = 0;
	int rv = -1;

	*alt_match = 0;
	*alt_exists = 0;

	altname_stack = X509_get_ext_d2i(cert, NID_subject_alt_name, &critical,
	    NULL);
	if (altname_stack == NULL) {
		if (critical != -1) {
			tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
			    "error decoding subjectAltName");
			goto err;
		}
		goto done;
	}

	if (inet_pton(AF_INET, name, &addrbuf) == 1) {
		type = GEN_IPADD;
		addrlen = 4;
	} else if (inet_pton(AF_INET6, name, &addrbuf) == 1) {
		type = GEN_IPADD;
		addrlen = 16;
	} else {
		type = GEN_DNS;
		addrlen = 0;
	}

	count = sk_GENERAL_NAME_num(altname_stack);
	for (i = 0; i < count; i++) {
		GENERAL_NAME *altname;

		altname = sk_GENERAL_NAME_value(altname_stack, i);

		if (altname->type == GEN_DNS || altname->type == GEN_IPADD)
			*alt_exists = 1;

		if (altname->type != type)
			continue;

		if (type == GEN_DNS) {
			const unsigned char *data;
			int format, len;

			format = ASN1_STRING_type(altname->d.dNSName);
			if (format == V_ASN1_IA5STRING) {
				data = ASN1_STRING_get0_data(altname->d.dNSName);
				len = ASN1_STRING_length(altname->d.dNSName);

				if (len < 0 || (size_t)len != strlen(data)) {
					tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
					    "error verifying name '%s': "
					    "NUL byte in subjectAltName, "
					    "probably a malicious certificate",
					    name);
					goto err;
				}

				/*
				 * Per RFC 5280 section 4.2.1.6:
				 * " " is a legal domain name, but that
				 * dNSName must be rejected.
				 */
				if (strcmp(data, " ") == 0) {
					tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
					    "error verifying name '%s': "
					    "a dNSName of \" \" must not be "
					    "used", name);
					goto err;
				}

				if (tls_match_name(data, name) == 0) {
					*alt_match = 1;
					goto done;
				}
			} else {
#ifdef DEBUG
				fprintf(stdout, "%s: unhandled subjectAltName "
				    "dNSName encoding (%d)\n", getprogname(),
				    format);
#endif
			}

		} else if (type == GEN_IPADD) {
			const unsigned char *data;
			int datalen;

			datalen = ASN1_STRING_length(altname->d.iPAddress);
			data = ASN1_STRING_get0_data(altname->d.iPAddress);

			if (datalen < 0) {
				tls_set_errorx(ctx, TLS_ERROR_UNKNOWN,
				    "Unexpected negative length for an "
				    "IP address: %d", datalen);
				goto err;
			}

			/*
			 * Per RFC 5280 section 4.2.1.6:
			 * IPv4 must use 4 octets and IPv6 must use 16 octets.
			 */
			if (datalen == addrlen &&
			    memcmp(data, &addrbuf, addrlen) == 0) {
				*alt_match = 1;
				goto done;
			}
		}
	}

 done:
	rv = 0;

 err:
	sk_GENERAL_NAME_pop_free(altname_stack, GENERAL_NAME_free);
	return rv;
}

static int
tls_get_common_name_internal(X509 *cert, char **out_common_name,
    unsigned int *out_tlserr, const char **out_errstr)
{
	unsigned char *utf8_bytes = NULL;
	X509_NAME *subject_name;
	char *common_name = NULL;
	int common_name_len;
	ASN1_STRING *data;
	int lastpos = -1;
	int rv = -1;

	*out_tlserr = TLS_ERROR_UNKNOWN;
	*out_errstr = "unknown";

	free(*out_common_name);
	*out_common_name = NULL;

	subject_name = X509_get_subject_name(cert);
	if (subject_name == NULL)
		goto err;

	lastpos = X509_NAME_get_index_by_NID(subject_name,
	    NID_commonName, lastpos);
	if (lastpos == -1)
		goto done;
	if (lastpos < 0)
		goto err;
	if (X509_NAME_get_index_by_NID(subject_name, NID_commonName, lastpos)
	    != -1) {
		/*
		 * Having multiple CN's is possible, and even happened back in
		 * the glory days of mullets and Hammer pants. In anything like
		 * a modern TLS cert, CN is as close to deprecated as it gets,
		 * and having more than one is bad. We therefore fail if we have
		 * more than one CN fed to us in the subject, treating the
		 * certificate as hostile.
		 */
		*out_tlserr = TLS_ERROR_UNKNOWN;
		*out_errstr = "error getting common name: "
		    "Certificate subject contains multiple Common Name fields, "
		    "probably a malicious or malformed certificate";
		goto err;
	}

	data = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(subject_name,
	    lastpos));
	/*
	 * Fail if we cannot encode the CN bytes as UTF-8.
	 */
	if ((common_name_len = ASN1_STRING_to_UTF8(&utf8_bytes, data)) < 0) {
		*out_tlserr = TLS_ERROR_UNKNOWN;
		*out_errstr = "error getting common name: "
		    "Common Name field cannot be encoded as a UTF-8 string, "
		    "probably a malicious certificate";
		goto err;
	}
	/*
	 * Fail if the CN is of invalid length. RFC 5280 specifies that a CN
	 * must be between 1 and 64 bytes long.
	 */
	if (common_name_len < 1 || common_name_len > 64) {
		*out_tlserr = TLS_ERROR_UNKNOWN;
		*out_errstr = "error getting common name: "
		    "Common Name field has invalid length, "
		    "probably a malicious certificate";
		goto err;
	}
	/*
	 * Fail if the resulting text contains a NUL byte.
	 */
	if (memchr(utf8_bytes, 0, common_name_len) != NULL) {
		*out_tlserr = TLS_ERROR_UNKNOWN;
		*out_errstr = "error getting common name: "
		    "NUL byte in Common Name field, "
		    "probably a malicious certificate";
		goto err;
	}

	common_name = strndup(utf8_bytes, common_name_len);
	if (common_name == NULL) {
		*out_tlserr = TLS_ERROR_OUT_OF_MEMORY;
		*out_errstr = "out of memory";
		goto err;
	}

	*out_common_name = common_name;
	common_name = NULL;

 done:
	if (*out_common_name == NULL)
		*out_common_name = strdup("");
	if (*out_common_name == NULL) {
		*out_tlserr = TLS_ERROR_OUT_OF_MEMORY;
		*out_errstr = "out of memory";
		goto err;
	}

	rv = 0;

 err:
	free(utf8_bytes);
	free(common_name);
	return rv;
}

int
tls_get_common_name(struct tls *ctx, X509 *cert, const char *in_name,
    char **out_common_name)
{
	unsigned int errcode = TLS_ERROR_UNKNOWN;
	const char *errstr = "unknown";

	if (tls_get_common_name_internal(cert, out_common_name, &errcode,
	    &errstr) == -1) {
		const char *name = in_name;
		const char *space = " ";

		if (name == NULL)
			name = space = "";

		tls_set_errorx(ctx, errcode, "%s%s%s", name, space, errstr);
		return -1;
	}

	return 0;
}

static int
tls_check_common_name(struct tls *ctx, X509 *cert, const char *name,
    int *cn_match)
{
	char *common_name = NULL;
	union tls_addr addrbuf;
	int rv = -1;

	if (tls_get_common_name(ctx, cert, name, &common_name) == -1)
		goto err;
	if (strlen(common_name) == 0)
		goto done;

	/*
	 * We don't want to attempt wildcard matching against IP addresses,
	 * so perform a simple comparison here.
	 */
	if (inet_pton(AF_INET,  name, &addrbuf) == 1 ||
	    inet_pton(AF_INET6, name, &addrbuf) == 1) {
		if (strcmp(common_name, name) == 0)
			*cn_match = 1;
		goto done;
	}

	if (tls_match_name(common_name, name) == 0)
		*cn_match = 1;

 done:
	rv = 0;

 err:
	free(common_name);
	return rv;
}

int
tls_check_name(struct tls *ctx, X509 *cert, const char *name, int *match)
{
	int alt_exists;

	*match = 0;

	if (tls_check_subject_altname(ctx, cert, name, match,
	    &alt_exists) == -1)
		return -1;

	/*
	 * As per RFC 6125 section 6.4.4, if any known alternate name existed
	 * in the certificate, we do not attempt to match on the CN.
	 */
	if (*match || alt_exists)
		return 0;

	return tls_check_common_name(ctx, cert, name, match);
}
