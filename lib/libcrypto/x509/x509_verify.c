/* $OpenBSD: x509_verify.c,v 1.73 2025/02/08 10:12:00 tb Exp $ */
/*
 * Copyright (c) 2020-2021 Bob Beck <beck@openbsd.org>
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

/* x509_verify - inspired by golang's crypto/x509.Verify */

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <openssl/safestack.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "asn1_local.h"
#include "x509_internal.h"
#include "x509_issuer_cache.h"

static int x509_verify_cert_valid(struct x509_verify_ctx *ctx, X509 *cert,
    struct x509_verify_chain *current_chain);
static int x509_verify_cert_hostname(struct x509_verify_ctx *ctx, X509 *cert,
    char *name);
static void x509_verify_build_chains(struct x509_verify_ctx *ctx, X509 *cert,
    struct x509_verify_chain *current_chain, int full_chain, char *name);
static int x509_verify_cert_error(struct x509_verify_ctx *ctx, X509 *cert,
    size_t depth, int error, int ok);
static void x509_verify_chain_free(struct x509_verify_chain *chain);

/*
 * Parse an asn1 to a representable time_t as per RFC 5280 rules.
 * Returns -1 if that can't be done for any reason.
 */
int
x509_verify_asn1_time_to_time_t(const ASN1_TIME *atime, int notAfter,
    time_t *out)
{
	struct tm tm = { 0 };
	int type;

	if (atime == NULL)
		return 0;

	type = ASN1_time_parse(atime->data, atime->length, &tm, atime->type);
	if (type == -1)
		return 0;

	/* RFC 5280 section 4.1.2.5 */
	if (tm.tm_year < 150 && type != V_ASN1_UTCTIME)
		return 0;
	if (tm.tm_year >= 150 && type != V_ASN1_GENERALIZEDTIME)
		return 0;

	if (notAfter) {
		/*
		 * If we are a completely broken operating system with a
		 * 32 bit time_t, and we have been told this is a notAfter
		 * date, limit the date to a 32 bit representable value.
		 */
		if (!ASN1_time_tm_clamp_notafter(&tm))
			return 0;
	}

	/*
	 * Defensively fail if the time string is not representable as
	 * a time_t. A time_t must be sane if you care about times after
	 * Jan 19 2038.
	 */
	return asn1_time_tm_to_time_t(&tm, out);
}

struct x509_verify_chain *
x509_verify_chain_new(void)
{
	struct x509_verify_chain *chain;

	if ((chain = calloc(1, sizeof(*chain))) == NULL)
		goto err;
	if ((chain->certs = sk_X509_new_null()) == NULL)
		goto err;
	if ((chain->cert_errors = calloc(X509_VERIFY_MAX_CHAIN_CERTS,
	    sizeof(int))) == NULL)
		goto err;
	if ((chain->names =
	    x509_constraints_names_new(X509_VERIFY_MAX_CHAIN_NAMES)) == NULL)
		goto err;

	return chain;
 err:
	x509_verify_chain_free(chain);
	return NULL;
}

static void
x509_verify_chain_clear(struct x509_verify_chain *chain)
{
	sk_X509_pop_free(chain->certs, X509_free);
	chain->certs = NULL;
	free(chain->cert_errors);
	chain->cert_errors = NULL;
	x509_constraints_names_free(chain->names);
	chain->names = NULL;
}

static void
x509_verify_chain_free(struct x509_verify_chain *chain)
{
	if (chain == NULL)
		return;
	x509_verify_chain_clear(chain);
	free(chain);
}

static struct x509_verify_chain *
x509_verify_chain_dup(struct x509_verify_chain *chain)
{
	struct x509_verify_chain *new_chain;

	if ((new_chain = calloc(1, sizeof(*chain))) == NULL)
		goto err;
	if ((new_chain->certs = X509_chain_up_ref(chain->certs)) == NULL)
		goto err;
	if ((new_chain->cert_errors = calloc(X509_VERIFY_MAX_CHAIN_CERTS,
	    sizeof(int))) == NULL)
		goto err;
	memcpy(new_chain->cert_errors, chain->cert_errors,
	    X509_VERIFY_MAX_CHAIN_CERTS * sizeof(int));
	if ((new_chain->names =
	    x509_constraints_names_dup(chain->names)) == NULL)
		goto err;
	return(new_chain);
 err:
	x509_verify_chain_free(new_chain);
	return NULL;
}

static int
x509_verify_chain_append(struct x509_verify_chain *chain, X509 *cert,
    int *error)
{
	int verify_err = X509_V_ERR_UNSPECIFIED;
	size_t idx;

	if (!x509_constraints_extract_names(chain->names, cert,
	    sk_X509_num(chain->certs) == 0, &verify_err)) {
		*error = verify_err;
		return 0;
	}

	X509_up_ref(cert);
	if (!sk_X509_push(chain->certs, cert)) {
		X509_free(cert);
		*error = X509_V_ERR_OUT_OF_MEM;
		return 0;
	}

	idx = sk_X509_num(chain->certs) - 1;
	chain->cert_errors[idx] = *error;

	/*
	 * We've just added the issuer for the previous certificate,
	 * clear its error if appropriate.
	 */
	if (idx > 1 && chain->cert_errors[idx - 1] ==
	    X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY)
		chain->cert_errors[idx - 1] = X509_V_OK;

	return 1;
}

static X509 *
x509_verify_chain_last(struct x509_verify_chain *chain)
{
	int last;

	if (chain->certs == NULL)
		return NULL;
	if ((last = sk_X509_num(chain->certs) - 1) < 0)
		return NULL;
	return sk_X509_value(chain->certs, last);
}

X509 *
x509_verify_chain_leaf(struct x509_verify_chain *chain)
{
	if (chain->certs == NULL)
		return NULL;
	return sk_X509_value(chain->certs, 0);
}

static void
x509_verify_ctx_reset(struct x509_verify_ctx *ctx)
{
	size_t i;

	for (i = 0; i < ctx->chains_count; i++)
		x509_verify_chain_free(ctx->chains[i]);
	sk_X509_pop_free(ctx->saved_error_chain, X509_free);
	ctx->saved_error = 0;
	ctx->saved_error_depth = 0;
	ctx->error = 0;
	ctx->error_depth = 0;
	ctx->chains_count = 0;
	ctx->sig_checks = 0;
	ctx->check_time = NULL;
}

static void
x509_verify_ctx_clear(struct x509_verify_ctx *ctx)
{
	x509_verify_ctx_reset(ctx);
	sk_X509_pop_free(ctx->intermediates, X509_free);
	free(ctx->chains);

}

static int
x509_verify_cert_cache_extensions(X509 *cert)
{
	return x509v3_cache_extensions(cert);
}

static int
x509_verify_cert_self_signed(X509 *cert)
{
	return (cert->ex_flags & EXFLAG_SS) ? 1 : 0;
}

/* XXX beck - clean up this mess of is_root */
static int
x509_verify_check_chain_end(X509 *cert, int full_chain)
{
	if (full_chain)
		return x509_verify_cert_self_signed(cert);
	return 1;
}

static int
x509_verify_ctx_cert_is_root(struct x509_verify_ctx *ctx, X509 *cert,
    int full_chain)
{
	X509 *match = NULL;
	int i;

	if (!x509_verify_cert_cache_extensions(cert))
		return 0;

	/* Check by lookup if we have a legacy xsc */
	if (ctx->xsc != NULL) {
		/*
		 * "alternative" lookup method, using the "trusted" stack in the
		 * xsc as the source for roots.
		 */
		if (ctx->xsc->trusted != NULL) {
			for (i = 0; i < sk_X509_num(ctx->xsc->trusted); i++) {
				if (X509_cmp(sk_X509_value(ctx->xsc->trusted,
				    i), cert) == 0)
					return x509_verify_check_chain_end(cert,
					    full_chain);
			}
		}
		if ((match = x509_vfy_lookup_cert_match(ctx->xsc,
		    cert)) != NULL) {
			X509_free(match);
			return x509_verify_check_chain_end(cert, full_chain);
		}
	} else {
		/* Check the provided roots */
		for (i = 0; i < sk_X509_num(ctx->roots); i++) {
			if (X509_cmp(sk_X509_value(ctx->roots, i), cert) == 0)
				return x509_verify_check_chain_end(cert,
				    full_chain);
		}
	}

	return 0;
}

static int
x509_verify_ctx_set_xsc_chain(struct x509_verify_ctx *ctx,
    struct x509_verify_chain *chain, int set_error, int is_trusted)
{
	size_t num_untrusted;
	int i;

	if (ctx->xsc == NULL)
		return 1;

	/*
	 * XXX num_untrusted is the number of untrusted certs at the
	 * bottom of the chain. This works now since we stop at the first
	 * trusted cert. This will need fixing once we allow more than one
	 * trusted certificate.
	 */
	num_untrusted = sk_X509_num(chain->certs);
	if (is_trusted && num_untrusted > 0)
		num_untrusted--;
	ctx->xsc->num_untrusted = num_untrusted;

	sk_X509_pop_free(ctx->xsc->chain, X509_free);
	ctx->xsc->chain = X509_chain_up_ref(chain->certs);
	if (ctx->xsc->chain == NULL)
		return x509_verify_cert_error(ctx, NULL, 0,
		    X509_V_ERR_OUT_OF_MEM, 0);

	if (set_error) {
		ctx->xsc->error = X509_V_OK;
		ctx->xsc->error_depth = 0;
		for (i = 0; i < sk_X509_num(chain->certs); i++) {
			if (chain->cert_errors[i] != X509_V_OK) {
				ctx->xsc->error = chain->cert_errors[i];
				ctx->xsc->error_depth = i;
				break;
			}
		}
	}

	return 1;
}


/*
 * Save the error state and unvalidated chain off of the xsc for
 * later.
 */
static int
x509_verify_ctx_save_xsc_error(struct x509_verify_ctx *ctx)
{
	if (ctx->xsc != NULL && ctx->xsc->chain != NULL) {
		sk_X509_pop_free(ctx->saved_error_chain, X509_free);
		ctx->saved_error_chain = X509_chain_up_ref(ctx->xsc->chain);
		if (ctx->saved_error_chain == NULL)
			return x509_verify_cert_error(ctx, NULL, 0,
			    X509_V_ERR_OUT_OF_MEM, 0);
		ctx->saved_error = ctx->xsc->error;
		ctx->saved_error_depth = ctx->xsc->error_depth;
	}
	return 1;
}

/*
 * Restore the saved error state and unvalidated chain to the xsc
 * if we do not have a validated chain.
 */
static int
x509_verify_ctx_restore_xsc_error(struct x509_verify_ctx *ctx)
{
	if (ctx->xsc != NULL && ctx->chains_count == 0 &&
	    ctx->saved_error_chain != NULL) {
		sk_X509_pop_free(ctx->xsc->chain, X509_free);
		ctx->xsc->chain = X509_chain_up_ref(ctx->saved_error_chain);
		if (ctx->xsc->chain == NULL)
			return x509_verify_cert_error(ctx, NULL, 0,
			    X509_V_ERR_OUT_OF_MEM, 0);
		ctx->xsc->error = ctx->saved_error;
		ctx->xsc->error_depth = ctx->saved_error_depth;
	}
	return 1;
}

/* Perform legacy style validation of a chain */
static int
x509_verify_ctx_validate_legacy_chain(struct x509_verify_ctx *ctx,
    struct x509_verify_chain *chain, size_t depth)
{
	int ret = 0, trust;

	if (ctx->xsc == NULL)
		return 1;

	/*
	 * If we have a legacy xsc, choose a validated chain, and
	 * apply the extensions, revocation, and policy checks just
	 * like the legacy code did. We do this here instead of as
	 * building the chains to more easily support the callback and
	 * the bewildering array of VERIFY_PARAM knobs that are there
	 * for the fiddling.
	 */

	/* These may be set in one of the following calls. */
	ctx->xsc->error = X509_V_OK;
	ctx->xsc->error_depth = 0;

	if (!x509_verify_ctx_set_xsc_chain(ctx, chain, 0, 1))
		goto err;

	/*
	 * Call the legacy code to walk the chain and check trust
	 * in the legacy way to handle partial chains and get the
	 * callback fired correctly.
	 */
	trust = x509_vfy_check_trust(ctx->xsc);
	if (trust == X509_TRUST_REJECTED)
		goto err; /* callback was called in x509_vfy_check_trust */
	if (trust != X509_TRUST_TRUSTED) {
		/* NOTREACHED */
		goto err;  /* should not happen if we get in here - abort? */
	}

	/*
	 * XXX currently this duplicates some work done in chain
	 * build, but we keep it here until we have feature parity
	 */
	if (!x509_vfy_check_chain_extensions(ctx->xsc))
		goto err;

#ifndef OPENSSL_NO_RFC3779
	if (!X509v3_asid_validate_path(ctx->xsc))
		goto err;

	if (!X509v3_addr_validate_path(ctx->xsc))
		goto err;
#endif

	if (!x509_vfy_check_security_level(ctx->xsc))
		goto err;

	if (!x509_constraints_chain(ctx->xsc->chain,
		&ctx->xsc->error, &ctx->xsc->error_depth)) {
		X509 *cert = sk_X509_value(ctx->xsc->chain, depth);
		if (!x509_verify_cert_error(ctx, cert,
			ctx->xsc->error_depth, ctx->xsc->error, 0))
			goto err;
	}

	if (!x509_vfy_check_revocation(ctx->xsc))
		goto err;

	if (!x509_vfy_check_policy(ctx->xsc))
		goto err;

	ret = 1;

 err:
	/*
	 * The above checks may have set ctx->xsc->error and
	 * ctx->xsc->error_depth - save these for later on.
	 */
	if (ctx->xsc->error != X509_V_OK) {
		if (ctx->xsc->error_depth < 0 ||
		    ctx->xsc->error_depth >= X509_VERIFY_MAX_CHAIN_CERTS)
			return 0;
		chain->cert_errors[ctx->xsc->error_depth] =
		    ctx->xsc->error;
		ctx->error_depth = ctx->xsc->error_depth;
	}

	return ret;
}

/* Add a validated chain to our list of valid chains */
static int
x509_verify_ctx_add_chain(struct x509_verify_ctx *ctx,
    struct x509_verify_chain *chain, char *name)
{
	size_t depth;
	X509 *last = x509_verify_chain_last(chain);
	X509 *leaf = x509_verify_chain_leaf(chain);

	depth = sk_X509_num(chain->certs);
	if (depth > 0)
		depth--;

	if (ctx->chains_count >= ctx->max_chains)
		return x509_verify_cert_error(ctx, last, depth,
		    X509_V_ERR_CERT_CHAIN_TOO_LONG, 0);

	/* Clear a get issuer failure for a root certificate. */
	if (chain->cert_errors[depth] ==
	    X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY)
		chain->cert_errors[depth] = X509_V_OK;

	if (!x509_verify_ctx_validate_legacy_chain(ctx, chain, depth))
		return 0;

	/* Verify the leaf certificate and store any resulting error. */
	if (!x509_verify_cert_valid(ctx, leaf, NULL))
		return 0;
	if (!x509_verify_cert_hostname(ctx, leaf, name))
		return 0;
	if (ctx->error_depth == 0 &&
	    ctx->error != X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY)
		chain->cert_errors[0] = ctx->error;

	/*
	 * In the non-legacy code, extensions and purpose are dealt
	 * with as the chain is built.
	 *
	 * The non-legacy api returns multiple chains but does not do
	 * any revocation checking (it must be done by the caller on
	 * any chain they wish to use)
	 */

	if ((ctx->chains[ctx->chains_count] = x509_verify_chain_dup(chain)) ==
	    NULL) {
		return x509_verify_cert_error(ctx, last, depth,
		    X509_V_ERR_OUT_OF_MEM, 0);
	}
	ctx->chains_count++;

	ctx->error = X509_V_OK;
	ctx->error_depth = depth;

	return 1;
}

static int
x509_verify_potential_parent(struct x509_verify_ctx *ctx, X509 *parent,
    X509 *child)
{
	if (!x509_verify_cert_cache_extensions(parent))
		return 0;
	if (ctx->xsc != NULL)
		return (ctx->xsc->check_issued(ctx->xsc, child, parent));

	/* XXX key usage */
	return X509_check_issued(parent, child) == X509_V_OK;
}

/* Matches x509_crl_verify_parent_signature() */
static int
x509_verify_parent_signature(X509 *parent, X509 *child, int *error)
{
	EVP_PKEY *pkey;
	int cached;
	int ret = 0;

	/* Use cached value if we have it */
	if ((cached = x509_issuer_cache_find(parent->hash, child->hash)) >= 0) {
		if (cached == 0)
			*error = X509_V_ERR_CERT_SIGNATURE_FAILURE;
		return cached;
	}

	/* Check signature. Did parent sign child? */
	if ((pkey = X509_get0_pubkey(parent)) == NULL) {
		*error = X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY;
		return 0;
	}
	if (X509_verify(child, pkey) <= 0)
		*error = X509_V_ERR_CERT_SIGNATURE_FAILURE;
	else
		ret = 1;

	/* Add result to cache */
	x509_issuer_cache_add(parent->hash, child->hash, ret);

	return ret;
}

static int
x509_verify_consider_candidate(struct x509_verify_ctx *ctx, X509 *cert,
    int is_root_cert, X509 *candidate, struct x509_verify_chain *current_chain,
    int full_chain, char *name)
{
	int depth = sk_X509_num(current_chain->certs);
	struct x509_verify_chain *new_chain;
	int i;

	/* Fail if the certificate is already in the chain */
	for (i = 0; i < sk_X509_num(current_chain->certs); i++) {
		if (X509_cmp(sk_X509_value(current_chain->certs, i),
		    candidate) == 0)
			return 0;
	}

	if (ctx->sig_checks++ > X509_VERIFY_MAX_SIGCHECKS) {
		/* don't allow callback to override safety check */
		(void) x509_verify_cert_error(ctx, candidate, depth,
		    X509_V_ERR_CERT_CHAIN_TOO_LONG, 0);
		return 0;
	}

	if (!x509_verify_parent_signature(candidate, cert, &ctx->error)) {
		if (!x509_verify_cert_error(ctx, candidate, depth,
		    ctx->error, 0))
			return 0;
	}

	if (!x509_verify_cert_valid(ctx, candidate, current_chain))
		return 0;

	/* candidate is good, add it to a copy of the current chain */
	if ((new_chain = x509_verify_chain_dup(current_chain)) == NULL) {
		x509_verify_cert_error(ctx, candidate, depth,
		    X509_V_ERR_OUT_OF_MEM, 0);
		return 0;
	}
	if (!x509_verify_chain_append(new_chain, candidate, &ctx->error)) {
		x509_verify_cert_error(ctx, candidate, depth, ctx->error, 0);
		x509_verify_chain_free(new_chain);
		return 0;
	}

	/*
	 * If candidate is a trusted root, we have a validated chain,
	 * so we save it.  Otherwise, recurse until we find a root or
	 * give up.
	 */
	if (is_root_cert) {
		if (!x509_verify_ctx_set_xsc_chain(ctx, new_chain, 0, 1)) {
			x509_verify_chain_free(new_chain);
			return 0;
		}
		if (!x509_verify_ctx_add_chain(ctx, new_chain, name)) {
			x509_verify_chain_free(new_chain);
			return 0;
		}
		goto done;
	}

	x509_verify_build_chains(ctx, candidate, new_chain, full_chain, name);

 done:
	x509_verify_chain_free(new_chain);
	return 1;
}

static int
x509_verify_cert_error(struct x509_verify_ctx *ctx, X509 *cert, size_t depth,
    int error, int ok)
{
	ctx->error = error;
	ctx->error_depth = depth;
	if (ctx->xsc != NULL) {
		ctx->xsc->error = error;
		ctx->xsc->error_depth = depth;
		ctx->xsc->current_cert = cert;
		return ctx->xsc->verify_cb(ok, ctx->xsc);
	}
	return ok;
}

static void
x509_verify_build_chains(struct x509_verify_ctx *ctx, X509 *cert,
    struct x509_verify_chain *current_chain, int full_chain, char *name)
{
	X509 *candidate;
	int i, depth, count, ret, is_root;

	/*
	 * If we are finding chains with an xsc, just stop after we have
	 * one chain, there's no point in finding more, it just exercises
	 * the potentially buggy callback processing in the calling software.
	 */
	if (ctx->xsc != NULL && ctx->chains_count > 0)
		return;

	depth = sk_X509_num(current_chain->certs);
	if (depth > 0)
		depth--;

	if (depth >= ctx->max_depth &&
	    !x509_verify_cert_error(ctx, cert, depth,
		X509_V_ERR_CERT_CHAIN_TOO_LONG, 0))
		return;

	count = ctx->chains_count;

	ctx->error = X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY;
	ctx->error_depth = depth;

	if (ctx->saved_error != 0)
		ctx->error = ctx->saved_error;
	if (ctx->saved_error_depth != 0)
		ctx->error_depth = ctx->saved_error_depth;

	if (ctx->xsc != NULL) {
		/*
		 * Long ago experiments at Muppet labs resulted in a
		 * situation where software not only sees these errors
		 * but forced developers to expect them in certain cases.
		 * so we must mimic this awfulness for the legacy case.
		 */
		if (cert->ex_flags & EXFLAG_SS)
			ctx->error = (depth == 0) ?
			    X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
			    X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN;
	}

	/* Check for legacy mode roots */
	if (ctx->xsc != NULL) {
		if ((ret = ctx->xsc->get_issuer(&candidate, ctx->xsc, cert)) < 0) {
			x509_verify_cert_error(ctx, cert, depth,
			    X509_V_ERR_STORE_LOOKUP, 0);
			return;
		}
		if (ret > 0) {
			if (x509_verify_potential_parent(ctx, candidate, cert)) {
				is_root = x509_verify_check_chain_end(candidate,
				    full_chain);
				x509_verify_consider_candidate(ctx, cert,
				    is_root, candidate, current_chain,
				    full_chain, name);
			}
			X509_free(candidate);
		}
	} else {
		/* Check to see if we have a trusted root issuer. */
		for (i = 0; i < sk_X509_num(ctx->roots); i++) {
			candidate = sk_X509_value(ctx->roots, i);
			if (x509_verify_potential_parent(ctx, candidate, cert)) {
				is_root = x509_verify_check_chain_end(candidate,
				    full_chain);
				x509_verify_consider_candidate(ctx, cert,
				    is_root, candidate, current_chain,
				    full_chain, name);
			}
		}
	}

	/* Check intermediates after checking roots */
	if (ctx->intermediates != NULL) {
		for (i = 0; i < sk_X509_num(ctx->intermediates); i++) {
			candidate = sk_X509_value(ctx->intermediates, i);
			if (x509_verify_potential_parent(ctx, candidate, cert)) {
				x509_verify_consider_candidate(ctx, cert,
				    0, candidate, current_chain,
				    full_chain, name);
			}
		}
	}

	if (ctx->chains_count > count) {
		if (ctx->xsc != NULL) {
			ctx->xsc->error = X509_V_OK;
			ctx->xsc->error_depth = depth;
			ctx->xsc->current_cert = cert;
		}
	} else if (ctx->error_depth == depth) {
		if (!x509_verify_ctx_set_xsc_chain(ctx, current_chain, 0, 0))
			return;
	}
}

static int
x509_verify_cert_hostname(struct x509_verify_ctx *ctx, X509 *cert, char *name)
{
	char *candidate;
	size_t len;

	if (name == NULL) {
		if (ctx->xsc != NULL) {
			int ret;

			if ((ret = x509_vfy_check_id(ctx->xsc)) == 0)
				ctx->error = ctx->xsc->error;
			return ret;
		}
		return 1;
	}
	if ((candidate = strdup(name)) == NULL) {
		ctx->error = X509_V_ERR_OUT_OF_MEM;
		goto err;
	}
	if ((len = strlen(candidate)) < 1) {
		ctx->error = X509_V_ERR_UNSPECIFIED; /* XXX */
		goto err;
	}

	/* IP addresses may be written in [ ]. */
	if (candidate[0] == '[' && candidate[len - 1] == ']') {
		candidate[len - 1] = '\0';
		if (X509_check_ip_asc(cert, candidate + 1, 0) <= 0) {
			ctx->error = X509_V_ERR_IP_ADDRESS_MISMATCH;
			goto err;
		}
	} else {
		int flags = 0;

		if (ctx->xsc == NULL)
			flags = X509_CHECK_FLAG_NEVER_CHECK_SUBJECT;

		if (X509_check_host(cert, candidate, len, flags, NULL) <= 0) {
			ctx->error = X509_V_ERR_HOSTNAME_MISMATCH;
			goto err;
		}
	}
	free(candidate);
	return 1;
 err:
	free(candidate);
	return x509_verify_cert_error(ctx, cert, 0, ctx->error, 0);
}

static int
x509_verify_set_check_time(struct x509_verify_ctx *ctx)
{
	if (ctx->xsc != NULL)  {
		if (ctx->xsc->param->flags & X509_V_FLAG_USE_CHECK_TIME) {
			ctx->check_time = &ctx->xsc->param->check_time;
			return 1;
		}
		if (ctx->xsc->param->flags & X509_V_FLAG_NO_CHECK_TIME)
			return 0;
	}

	ctx->check_time = NULL;
	return 1;
}

static int
x509_verify_cert_times(X509 *cert, time_t *cmp_time, int *error)
{
	time_t when, not_before, not_after;

	if (cmp_time == NULL)
		when = time(NULL);
	else
		when = *cmp_time;

	if (!x509_verify_asn1_time_to_time_t(X509_get_notBefore(cert), 0,
	    &not_before)) {
		*error = X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD;
		return 0;
	}
	if (when < not_before) {
		*error = X509_V_ERR_CERT_NOT_YET_VALID;
		return 0;
	}
	if (!x509_verify_asn1_time_to_time_t(X509_get_notAfter(cert), 1,
	    &not_after)) {
		*error = X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD;
		return 0;
	}
	if (when > not_after) {
		*error = X509_V_ERR_CERT_HAS_EXPIRED;
		return 0;
	}

	return 1;
}

static int
x509_verify_validate_constraints(X509 *cert,
    struct x509_verify_chain *current_chain, int *error)
{
	struct x509_constraints_names *excluded = NULL;
	struct x509_constraints_names *permitted = NULL;
	int err = X509_V_ERR_UNSPECIFIED;

	if (current_chain == NULL)
		return 1;

	if (cert->nc != NULL) {
		if ((permitted = x509_constraints_names_new(
		    X509_VERIFY_MAX_CHAIN_CONSTRAINTS)) == NULL) {
			err = X509_V_ERR_OUT_OF_MEM;
			goto err;
		}
		if ((excluded = x509_constraints_names_new(
		    X509_VERIFY_MAX_CHAIN_CONSTRAINTS)) == NULL) {
			err = X509_V_ERR_OUT_OF_MEM;
			goto err;
		}
		if (!x509_constraints_extract_constraints(cert,
		    permitted, excluded, &err))
			goto err;
		if (!x509_constraints_check(current_chain->names,
		    permitted, excluded, &err))
			goto err;
		x509_constraints_names_free(excluded);
		x509_constraints_names_free(permitted);
	}

	return 1;
 err:
	*error = err;
	x509_constraints_names_free(excluded);
	x509_constraints_names_free(permitted);
	return 0;
}

static int
x509_verify_cert_extensions(struct x509_verify_ctx *ctx, X509 *cert, int need_ca)
{
	if (!x509_verify_cert_cache_extensions(cert)) {
		ctx->error = X509_V_ERR_UNSPECIFIED;
		return 0;
	}

	if (ctx->xsc != NULL)
		return 1;	/* legacy is checked after chain is built */

	if (cert->ex_flags & EXFLAG_CRITICAL) {
		ctx->error = X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION;
		return 0;
	}
	/* No we don't care about v1, netscape, and other ancient silliness */
	if (need_ca && (!(cert->ex_flags & EXFLAG_BCONS) &&
	    (cert->ex_flags & EXFLAG_CA))) {
		ctx->error = X509_V_ERR_INVALID_CA;
		return 0;
	}
	if (ctx->purpose > 0 && X509_check_purpose(cert, ctx->purpose, need_ca)) {
		ctx->error = X509_V_ERR_INVALID_PURPOSE;
		return 0;
	}

	return 1;
}

/* Validate that cert is a possible candidate to append to current_chain */
static int
x509_verify_cert_valid(struct x509_verify_ctx *ctx, X509 *cert,
    struct x509_verify_chain *current_chain)
{
	X509 *issuer_candidate;
	int should_be_ca = current_chain != NULL;
	size_t depth = 0;

	if (current_chain != NULL)
		depth = sk_X509_num(current_chain->certs);

	if (!x509_verify_cert_extensions(ctx, cert, should_be_ca))
		return 0;

	if (should_be_ca) {
		issuer_candidate = x509_verify_chain_last(current_chain);
		if (issuer_candidate != NULL &&
		    !X509_check_issued(issuer_candidate, cert))
			if (!x509_verify_cert_error(ctx, cert, depth,
			    X509_V_ERR_SUBJECT_ISSUER_MISMATCH, 0))
				return 0;
	}

	if (x509_verify_set_check_time(ctx)) {
		if (!x509_verify_cert_times(cert, ctx->check_time,
		    &ctx->error)) {
			if (!x509_verify_cert_error(ctx, cert, depth,
			    ctx->error, 0))
				return 0;
		}
	}

	if (!x509_verify_validate_constraints(cert, current_chain,
	    &ctx->error) && !x509_verify_cert_error(ctx, cert, depth,
	    ctx->error, 0))
		return 0;

	return 1;
}

struct x509_verify_ctx *
x509_verify_ctx_new_from_xsc(X509_STORE_CTX *xsc)
{
	struct x509_verify_ctx *ctx;
	size_t max_depth;

	if (xsc == NULL)
		return NULL;

	if ((ctx = x509_verify_ctx_new(NULL)) == NULL)
		return NULL;

	ctx->xsc = xsc;

	if (xsc->untrusted &&
	    (ctx->intermediates = X509_chain_up_ref(xsc->untrusted)) == NULL)
		goto err;

	max_depth = X509_VERIFY_MAX_CHAIN_CERTS;
	if (xsc->param->depth > 0 && xsc->param->depth < X509_VERIFY_MAX_CHAIN_CERTS)
		max_depth = xsc->param->depth;
	if (!x509_verify_ctx_set_max_depth(ctx, max_depth))
		goto err;

	return ctx;
 err:
	x509_verify_ctx_free(ctx);
	return NULL;
}

/* Public API */

struct x509_verify_ctx *
x509_verify_ctx_new(STACK_OF(X509) *roots)
{
	struct x509_verify_ctx *ctx;

	if ((ctx = calloc(1, sizeof(struct x509_verify_ctx))) == NULL)
		return NULL;

	if (roots != NULL) {
		if  ((ctx->roots = X509_chain_up_ref(roots)) == NULL)
			goto err;
	} else {
		if ((ctx->roots = sk_X509_new_null()) == NULL)
			goto err;
	}

	ctx->max_depth = X509_VERIFY_MAX_CHAIN_CERTS;
	ctx->max_chains = X509_VERIFY_MAX_CHAINS;
	ctx->max_sigs = X509_VERIFY_MAX_SIGCHECKS;

	if ((ctx->chains = calloc(X509_VERIFY_MAX_CHAINS,
	    sizeof(*ctx->chains))) == NULL)
		goto err;

	return ctx;
 err:
	x509_verify_ctx_free(ctx);
	return NULL;
}

void
x509_verify_ctx_free(struct x509_verify_ctx *ctx)
{
	if (ctx == NULL)
		return;
	sk_X509_pop_free(ctx->roots, X509_free);
	x509_verify_ctx_clear(ctx);
	free(ctx);
}

int
x509_verify_ctx_set_max_depth(struct x509_verify_ctx *ctx, size_t max)
{
	if (max < 1 || max > X509_VERIFY_MAX_CHAIN_CERTS)
		return 0;
	ctx->max_depth = max;
	return 1;
}

int
x509_verify_ctx_set_max_chains(struct x509_verify_ctx *ctx, size_t max)
{
	if (max < 1 || max > X509_VERIFY_MAX_CHAINS)
		return 0;
	ctx->max_chains = max;
	return 1;
}

int
x509_verify_ctx_set_max_signatures(struct x509_verify_ctx *ctx, size_t max)
{
	if (max < 1 || max > 100000)
		return 0;
	ctx->max_sigs = max;
	return 1;
}

int
x509_verify_ctx_set_purpose(struct x509_verify_ctx *ctx, int purpose)
{
	if (purpose < X509_PURPOSE_MIN || purpose > X509_PURPOSE_MAX)
		return 0;
	ctx->purpose = purpose;
	return 1;
}

int
x509_verify_ctx_set_intermediates(struct x509_verify_ctx *ctx,
    STACK_OF(X509) *intermediates)
{
	if ((ctx->intermediates = X509_chain_up_ref(intermediates)) == NULL)
		return 0;
	return 1;
}

const char *
x509_verify_ctx_error_string(struct x509_verify_ctx *ctx)
{
	return X509_verify_cert_error_string(ctx->error);
}

size_t
x509_verify_ctx_error_depth(struct x509_verify_ctx *ctx)
{
	return ctx->error_depth;
}

STACK_OF(X509) *
x509_verify_ctx_chain(struct x509_verify_ctx *ctx, size_t i)
{
	if (i >= ctx->chains_count)
		return NULL;
	return ctx->chains[i]->certs;
}

size_t
x509_verify(struct x509_verify_ctx *ctx, X509 *leaf, char *name)
{
	struct x509_verify_chain *current_chain;
	int retry_chain_build, full_chain = 0;

	if (ctx->roots == NULL || ctx->max_depth == 0) {
		ctx->error = X509_V_ERR_INVALID_CALL;
		goto err;
	}

	if (ctx->xsc != NULL) {
		if (leaf != NULL || name != NULL) {
			ctx->error = X509_V_ERR_INVALID_CALL;
			goto err;
		}
		leaf = ctx->xsc->cert;

		/* XXX */
		full_chain = 1;
		if (ctx->xsc->param->flags & X509_V_FLAG_PARTIAL_CHAIN)
			full_chain = 0;
		/*
		 * XXX
		 * The legacy code expects the top level cert to be
		 * there, even if we didn't find a chain. So put it
		 * there, we will clobber it later if we find a valid
		 * chain.
		 */
		if ((ctx->xsc->chain = sk_X509_new_null()) == NULL) {
			ctx->error = X509_V_ERR_OUT_OF_MEM;
			goto err;
		}
		if (!X509_up_ref(leaf)) {
			ctx->error = X509_V_ERR_OUT_OF_MEM;
			goto err;
		}
		if (!sk_X509_push(ctx->xsc->chain, leaf)) {
			X509_free(leaf);
			ctx->error = X509_V_ERR_OUT_OF_MEM;
			goto err;
		}
		ctx->xsc->error_depth = 0;
		ctx->xsc->current_cert = leaf;
	}

	if ((current_chain = x509_verify_chain_new()) == NULL) {
		ctx->error = X509_V_ERR_OUT_OF_MEM;
		goto err;
	}

	/*
	 * Add the leaf to the chain and try to build chains from it.
	 * Note that unlike Go's verifier, we have not yet checked
	 * anything about the leaf, This is intentional, so that we
	 * report failures in chain building before we report problems
	 * with the leaf.
	 */
	if (!x509_verify_chain_append(current_chain, leaf, &ctx->error)) {
		x509_verify_chain_free(current_chain);
		goto err;
	}
	do {
		retry_chain_build = 0;
		if (x509_verify_ctx_cert_is_root(ctx, leaf, full_chain)) {
			if (!x509_verify_ctx_add_chain(ctx, current_chain,
			    name)) {
				x509_verify_chain_free(current_chain);
				goto err;
			}
		} else {
			x509_verify_build_chains(ctx, leaf, current_chain,
			    full_chain, name);
			if (full_chain && ctx->chains_count == 0) {
				/*
				 * Save the error state from the xsc
				 * at this point to put back on the
				 * xsc in case we do not find a chain
				 * that is trusted but not a full
				 * chain to a self signed root. This
				 * is because the unvalidated chain is
				 * used by the autochain batshittery
				 * on failure and will be needed for
				 * that.
				 */
				ctx->xsc->error_depth = ctx->error_depth;
				if (!x509_verify_ctx_save_xsc_error(ctx)) {
					x509_verify_chain_free(current_chain);
					goto err;
				}
				full_chain = 0;
				retry_chain_build = 1;
			}
		}
	} while (retry_chain_build);

	x509_verify_chain_free(current_chain);

	/*
	 * Do the new verifier style return, where we don't have an xsc
	 * that allows a crazy callback to turn invalid things into valid.
	 */
	if (ctx->xsc == NULL) {
		/*
		 * Safety net:
		 * We could not find a validated chain, and for some reason do not
		 * have an error set.
		 */
		if (ctx->chains_count == 0 && ctx->error == X509_V_OK)
			ctx->error = X509_V_ERR_UNSPECIFIED;

		/*
		 * If we are not using an xsc, and have no possibility for the
		 * crazy OpenSSL callback API changing the results of
		 * validation steps (because the callback can make validation
		 * proceed in the presence of invalid certs), any chains we
		 * have here are correctly built and verified.
		 */
		if (ctx->chains_count > 0)
			ctx->error = X509_V_OK;

		return ctx->chains_count;
	}

	/*
	 * Otherwise we are doing compatibility with an xsc, which means that we
	 * will have one chain, which might actually be a bogus chain because
	 * the callback told us to ignore errors and proceed to build an invalid
	 * chain. Possible return values from this include returning 1 with an
	 * invalid chain and a value of xsc->error != X509_V_OK (It's tradition
	 * that makes it ok).
	 */

	if (ctx->chains_count > 0) {
		/*
		 * The chain we have using an xsc might not be a verified chain
		 * if the callback perverted things while we built it to ignore
		 * failures and proceed with chain building. We put this chain
		 * and the error associated with it on the xsc.
		 */
		if (!x509_verify_ctx_set_xsc_chain(ctx, ctx->chains[0], 1, 1))
			goto err;

		/*
		 * Call the callback for completion up our built
		 * chain. The callback could still tell us to
		 * fail. Since this chain might exist as the result of
		 * callback doing perversions, we could still return
		 * "success" with something other than X509_V_OK set
		 * as the error.
		 */
		if (!x509_vfy_callback_indicate_completion(ctx->xsc))
			goto err;
	} else {
		/*
		 * We did not find a chain. Bring back the failure
		 * case we wanted to the xsc if we saved one. If we
		 * did not we should have just the leaf on the xsc.
		 */
		if (!x509_verify_ctx_restore_xsc_error(ctx))
			goto err;

		/*
		 * Safety net, ensure we have an error set in the
		 * failing case.
		 */
		if (ctx->xsc->error == X509_V_OK) {
			if (ctx->error == X509_V_OK)
				ctx->error = X509_V_ERR_UNSPECIFIED;
			ctx->xsc->error = ctx->error;
		}

		/*
		 * Let the callback override the return value
		 * at depth 0 if it chooses to
		 */
		return ctx->xsc->verify_cb(0, ctx->xsc);
	}

	/* We only ever find one chain in compat mode with an xsc. */
	return 1;

 err:
	if (ctx->error == X509_V_OK)
		ctx->error = X509_V_ERR_UNSPECIFIED;

	if (ctx->xsc != NULL) {
		if (ctx->xsc->error == X509_V_OK)
			ctx->xsc->error = X509_V_ERR_UNSPECIFIED;
		ctx->error = ctx->xsc->error;
	}

	return 0;
}
