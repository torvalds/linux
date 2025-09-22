/* $OpenBSD: x509_internal.h,v 1.28 2024/05/19 07:12:50 jsg Exp $ */
/*
 * Copyright (c) 2020 Bob Beck <beck@openbsd.org>
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
#ifndef HEADER_X509_INTERNAL_H
#define HEADER_X509_INTERNAL_H

/* Internal use only, not public API */
#include <netinet/in.h>

#include "bytestring.h"
#include "x509_local.h"
#include "x509_verify.h"

/* Hard limits on structure size and number of signature checks. */
#define X509_VERIFY_MAX_CHAINS		8	/* Max validated chains */
#define X509_VERIFY_MAX_CHAIN_CERTS	32	/* Max depth of a chain */
#define X509_VERIFY_MAX_SIGCHECKS	256	/* Max signature checks */

/*
 * Limit the number of names and constraints we will check in a chain
 * to avoid a hostile input DOS
 */
#define X509_VERIFY_MAX_CHAIN_NAMES		512
#define X509_VERIFY_MAX_CHAIN_CONSTRAINTS	512

/*
 * Hold the parsed and validated result of names from a certificate.
 * these typically come from a GENERALNAME, but we store the parsed
 * and validated results, not the ASN1 bytes.
 */
struct x509_constraints_name {
	int type;			/* GEN_* types from GENERAL_NAME */
	char *name;			/* Name to check */
	char *local;			/* holds the local part of GEN_EMAIL */
	uint8_t *der;			/* DER encoded value or NULL*/
	size_t der_len;
	int af;				/* INET and INET6 are supported */
	uint8_t address[32];		/* Must hold ipv6 + mask */
};

struct x509_constraints_names {
	struct x509_constraints_name **names;
	size_t names_count;
	size_t names_len;
	size_t names_max;
};

struct x509_verify_chain {
	STACK_OF(X509) *certs;		/* Kept in chain order, includes leaf */
	int *cert_errors;		/* Verify error for each cert in chain. */
	struct x509_constraints_names *names;	/* All names from all certs */
};

struct x509_verify_ctx {
	X509_STORE_CTX *xsc;
	struct x509_verify_chain **chains;	/* Validated chains */
	STACK_OF(X509) *saved_error_chain;
	int saved_error;
	int saved_error_depth;
	size_t chains_count;
	STACK_OF(X509) *roots;		/* Trusted roots for this validation */
	STACK_OF(X509) *intermediates;	/* Intermediates provided by peer */
	time_t *check_time;		/* Time for validity checks */
	int purpose;			/* Cert purpose we are validating */
	size_t max_chains;		/* Max chains to return */
	size_t max_depth;		/* Max chain depth for validation */
	size_t max_sigs;		/* Max number of signature checks */
	size_t sig_checks;		/* Number of signature checks done */
	size_t error_depth;		/* Depth of last error seen */
	int error;			/* Last error seen */
};

int ASN1_time_tm_clamp_notafter(struct tm *tm);

__BEGIN_HIDDEN_DECLS

int x509_vfy_check_id(X509_STORE_CTX *ctx);
int x509_vfy_check_revocation(X509_STORE_CTX *ctx);
int x509_vfy_check_policy(X509_STORE_CTX *ctx);
int x509_vfy_check_trust(X509_STORE_CTX *ctx);
int x509_vfy_check_chain_extensions(X509_STORE_CTX *ctx);
int x509_vfy_callback_indicate_completion(X509_STORE_CTX *ctx);
int x509v3_cache_extensions(X509 *x);
X509 *x509_vfy_lookup_cert_match(X509_STORE_CTX *ctx, X509 *x);

int x509_verify_asn1_time_to_time_t(const ASN1_TIME *atime, int notafter,
    time_t *out);

struct x509_verify_ctx *x509_verify_ctx_new_from_xsc(X509_STORE_CTX *xsc);

void x509_constraints_name_clear(struct x509_constraints_name *name);
void x509_constraints_name_free(struct x509_constraints_name *name);
int x509_constraints_names_add(struct x509_constraints_names *names,
    struct x509_constraints_name *name);
struct x509_constraints_names *x509_constraints_names_dup(
    struct x509_constraints_names *names);
void x509_constraints_names_clear(struct x509_constraints_names *names);
struct x509_constraints_names *x509_constraints_names_new(size_t names_max);
int x509_constraints_general_to_bytes(GENERAL_NAME *name, uint8_t **bytes,
    size_t *len);
void x509_constraints_names_free(struct x509_constraints_names *names);
int x509_constraints_valid_host(CBS *cbs, int permit_ip);
int x509_constraints_valid_sandns(CBS *cbs);
int x509_constraints_domain(char *domain, size_t dlen, char *constraint,
    size_t len);
int x509_constraints_parse_mailbox(CBS *candidate,
    struct x509_constraints_name *name);
int x509_constraints_valid_domain_constraint(CBS *cbs);
int x509_constraints_uri_host(uint8_t *uri, size_t len, char **hostp);
int x509_constraints_uri(uint8_t *uri, size_t ulen, uint8_t *constraint,
    size_t len, int *error);
int x509_constraints_extract_names(struct x509_constraints_names *names,
    X509 *cert, int include_cn, int *error);
int x509_constraints_extract_constraints(X509 *cert,
    struct x509_constraints_names *permitted,
    struct x509_constraints_names *excluded, int *error);
int x509_constraints_validate(GENERAL_NAME *constraint,
    struct x509_constraints_name **out_name, int *error);
int x509_constraints_check(struct x509_constraints_names *names,
    struct x509_constraints_names *permitted,
    struct x509_constraints_names *excluded, int *error);
int x509_constraints_chain(STACK_OF(X509) *chain, int *error,
    int *depth);
int x509_vfy_check_security_level(X509_STORE_CTX *ctx);

__END_HIDDEN_DECLS

#endif
