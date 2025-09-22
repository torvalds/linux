/* $OpenBSD: x509_vfy.c,v 1.148 2025/05/10 05:54:39 tb Exp $ */
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

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <openssl/opensslconf.h>

#include <openssl/asn1.h>
#include <openssl/buffer.h>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/lhash.h>
#include <openssl/objects.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "asn1_local.h"
#include "err_local.h"
#include "x509_internal.h"
#include "x509_issuer_cache.h"
#include "x509_local.h"

/* CRL score values */

/* No unhandled critical extensions */

#define CRL_SCORE_NOCRITICAL	0x100

/* certificate is within CRL scope */

#define CRL_SCORE_SCOPE		0x080

/* CRL times valid */

#define CRL_SCORE_TIME		0x040

/* Issuer name matches certificate */

#define CRL_SCORE_ISSUER_NAME	0x020

/* If this score or above CRL is probably valid */

#define CRL_SCORE_VALID (CRL_SCORE_NOCRITICAL|CRL_SCORE_TIME|CRL_SCORE_SCOPE)

/* CRL issuer is certificate issuer */

#define CRL_SCORE_ISSUER_CERT	0x018

/* CRL issuer is on certificate path */

#define CRL_SCORE_SAME_PATH	0x008

/* CRL issuer matches CRL AKID */

#define CRL_SCORE_AKID		0x004

/* Have a delta CRL with valid times */

#define CRL_SCORE_TIME_DELTA	0x002

static int x509_vfy_check_crl(X509_STORE_CTX *ctx, X509_CRL *crl);
static int x509_vfy_cert_crl(X509_STORE_CTX *ctx, X509_CRL *crl, X509 *x);

static int null_callback(int ok, X509_STORE_CTX *e);
static int check_issued(X509_STORE_CTX *ctx, X509 *subject, X509 *issuer);
static X509 *find_issuer(X509_STORE_CTX *ctx, STACK_OF(X509) *sk, X509 *x,
    int allow_expired);
static int check_name_constraints(X509_STORE_CTX *ctx);
static int check_cert(X509_STORE_CTX *ctx, STACK_OF(X509) *chain, int depth);

static int get_crl_score(X509_STORE_CTX *ctx, X509 **pissuer,
    unsigned int *preasons, X509_CRL *crl, X509 *x);
static int get_crl_delta(X509_STORE_CTX *ctx,
    X509_CRL **pcrl, X509_CRL **pdcrl, X509 *x);
static void get_delta_sk(X509_STORE_CTX *ctx, X509_CRL **dcrl, int *pcrl_score,
    X509_CRL *base, STACK_OF(X509_CRL) *crls);
static void crl_akid_check(X509_STORE_CTX *ctx, X509_CRL *crl, X509 **pissuer,
    int *pcrl_score);
static int crl_crldp_check(X509 *x, X509_CRL *crl, int crl_score,
    unsigned int *preasons);
static int check_crl_path(X509_STORE_CTX *ctx, X509 *x);
static int check_crl_chain(X509_STORE_CTX *ctx, STACK_OF(X509) *cert_path,
    STACK_OF(X509) *crl_path);
static int X509_cmp_time_internal(const ASN1_TIME *ctm, time_t *cmp_time,
    int clamp_notafter);

static int internal_verify(X509_STORE_CTX *ctx);
static int check_key_level(X509_STORE_CTX *ctx, X509 *cert);
static int verify_cb_cert(X509_STORE_CTX *ctx, X509 *x, int depth, int err);

static int
null_callback(int ok, X509_STORE_CTX *e)
{
	return ok;
}

/* Return 1 if a certificate is self signed */
static int
cert_self_signed(X509 *x)
{
	X509_check_purpose(x, -1, 0);
	if (x->ex_flags & EXFLAG_SS)
		return 1;
	else
		return 0;
}

static int
check_id_error(X509_STORE_CTX *ctx, int errcode)
{
	ctx->error = errcode;
	ctx->current_cert = ctx->cert;
	ctx->error_depth = 0;
	return ctx->verify_cb(0, ctx);
}

static int
x509_vfy_check_hosts(X509 *x, X509_VERIFY_PARAM *vpm)
{
	int i, n;
	char *name;

	n = sk_OPENSSL_STRING_num(vpm->hosts);
	free(vpm->peername);
	vpm->peername = NULL;

	for (i = 0; i < n; ++i) {
		name = sk_OPENSSL_STRING_value(vpm->hosts, i);
		if (X509_check_host(x, name, strlen(name), vpm->hostflags,
		    &vpm->peername) > 0)
			return 1;
	}
	return n == 0;
}

int
x509_vfy_check_id(X509_STORE_CTX *ctx)
{
	X509_VERIFY_PARAM *vpm = ctx->param;
	X509 *x = ctx->cert;

	if (vpm->hosts && x509_vfy_check_hosts(x, vpm) <= 0) {
		if (!check_id_error(ctx, X509_V_ERR_HOSTNAME_MISMATCH))
			return 0;
	}
	if (vpm->email != NULL && X509_check_email(x, vpm->email, vpm->emaillen, 0)
	    <= 0) {
		if (!check_id_error(ctx, X509_V_ERR_EMAIL_MISMATCH))
			return 0;
	}
	if (vpm->ip != NULL && X509_check_ip(x, vpm->ip, vpm->iplen, 0) <= 0) {
		if (!check_id_error(ctx, X509_V_ERR_IP_ADDRESS_MISMATCH))
			return 0;
	}
	return 1;
}

/*
 * This is the effectively broken legacy OpenSSL chain builder. It
 * might find an unvalidated chain and leave it sitting in
 * ctx->chain. It does not correctly handle many cases where multiple
 * chains could exist.
 *
 * Oh no.. I know a dirty word...
 * Oooooooh..
 */
static int
X509_verify_cert_legacy_build_chain(X509_STORE_CTX *ctx, int *bad, int *out_ok)
{
	X509 *x, *xtmp, *xtmp2, *chain_ss = NULL;
	int bad_chain = 0;
	X509_VERIFY_PARAM *param = ctx->param;
	int ok = 0, ret = 0;
	int depth, i;
	int num, j, retry, trust;
	int (*cb) (int xok, X509_STORE_CTX *xctx);
	STACK_OF(X509) *sktmp = NULL;

	cb = ctx->verify_cb;

	/*
	 * First we make sure the chain we are going to build is
	 * present and that the first entry is in place.
	 */
	ctx->chain = sk_X509_new_null();
	if (ctx->chain == NULL || !sk_X509_push(ctx->chain, ctx->cert)) {
		X509error(ERR_R_MALLOC_FAILURE);
		ctx->error = X509_V_ERR_OUT_OF_MEM;
		goto end;
	}
	X509_up_ref(ctx->cert);
	ctx->num_untrusted = 1;

	/* We use a temporary STACK so we can chop and hack at it */
	if (ctx->untrusted != NULL &&
	    (sktmp = sk_X509_dup(ctx->untrusted)) == NULL) {
		X509error(ERR_R_MALLOC_FAILURE);
		ctx->error = X509_V_ERR_OUT_OF_MEM;
		goto end;
	}

	num = sk_X509_num(ctx->chain);
	x = sk_X509_value(ctx->chain, num - 1);
	depth = param->depth;

	for (;;) {
		/* If we have enough, we break */
		/* FIXME: If this happens, we should take
		 * note of it and, if appropriate, use the
		 * X509_V_ERR_CERT_CHAIN_TOO_LONG error code
		 * later.
		 */
		if (depth < num)
			break;
		/* If we are self signed, we break */
		if (cert_self_signed(x))
			break;
		/*
		 * If asked see if we can find issuer in trusted store first
		 */
		if (ctx->param->flags & X509_V_FLAG_TRUSTED_FIRST) {
			ok = ctx->get_issuer(&xtmp, ctx, x);
			if (ok < 0) {
				ctx->error = X509_V_ERR_STORE_LOOKUP;
				goto end;
			}
			/*
			 * If successful for now free up cert so it
			 * will be picked up again later.
			 */
			if (ok > 0) {
				X509_free(xtmp);
				break;
			}
		}
		/* If we were passed a cert chain, use it first */
		if (ctx->untrusted != NULL) {
			/*
			 * If we do not find a non-expired untrusted cert, peek
			 * ahead and see if we can satisfy this from the trusted
			 * store. If not, see if we have an expired untrusted cert.
			 */
			xtmp = find_issuer(ctx, sktmp, x, 0);
			if (xtmp == NULL &&
			    !(ctx->param->flags & X509_V_FLAG_TRUSTED_FIRST)) {
				ok = ctx->get_issuer(&xtmp, ctx, x);
				if (ok < 0) {
					ctx->error = X509_V_ERR_STORE_LOOKUP;
					goto end;
				}
				if (ok > 0) {
					X509_free(xtmp);
					break;
				}
				xtmp = find_issuer(ctx, sktmp, x, 1);
			}
			if (xtmp != NULL) {
				if (!sk_X509_push(ctx->chain, xtmp)) {
					X509error(ERR_R_MALLOC_FAILURE);
					ctx->error = X509_V_ERR_OUT_OF_MEM;
					ok = 0;
					goto end;
				}
				X509_up_ref(xtmp);
				(void)sk_X509_delete_ptr(sktmp, xtmp);
				ctx->num_untrusted++;
				x = xtmp;
				num++;
				/*
				 * reparse the full chain for the next one
				 */
				continue;
			}
		}
		break;
	}
	/* Remember how many untrusted certs we have */
	j = num;

	/*
	 * At this point, chain should contain a list of untrusted
	 * certificates.  We now need to add at least one trusted one,
	 * if possible, otherwise we complain.
	 */

	do {
		/*
		 * Examine last certificate in chain and see if it is
		 * self signed.
		 */
		i = sk_X509_num(ctx->chain);
		x = sk_X509_value(ctx->chain, i - 1);
		if (cert_self_signed(x)) {
			/* we have a self signed certificate */
			if (i == 1) {
				/*
				 * We have a single self signed
				 * certificate: see if we can find it
				 * in the store. We must have an exact
				 * match to avoid possible
				 * impersonation.
				 */
				ok = ctx->get_issuer(&xtmp, ctx, x);
				if ((ok <= 0) || X509_cmp(x, xtmp)) {
					ctx->error = X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT;
					ctx->current_cert = x;
					ctx->error_depth = i - 1;
					if (ok == 1)
						X509_free(xtmp);
					bad_chain = 1;
					ok = cb(0, ctx);
					if (!ok)
						goto end;
				} else {
					/*
					 * We have a match: replace
					 * certificate with store
					 * version so we get any trust
					 * settings.
					 */
					X509_free(x);
					x = xtmp;
					(void)sk_X509_set(ctx->chain, i - 1, x);
					ctx->num_untrusted = 0;
				}
			} else {
				/*
				 * extract and save self signed
				 * certificate for later use
				 */
				chain_ss = sk_X509_pop(ctx->chain);
				ctx->num_untrusted--;
				num--;
				j--;
				x = sk_X509_value(ctx->chain, num - 1);
			}
		}
		/* We now lookup certs from the certificate store */
		for (;;) {
			/* If we have enough, we break */
			if (depth < num)
				break;
			/* If we are self signed, we break */
			if (cert_self_signed(x))
				break;
			ok = ctx->get_issuer(&xtmp, ctx, x);

			if (ok < 0) {
				ctx->error = X509_V_ERR_STORE_LOOKUP;
				goto end;
			}
			if (ok == 0)
				break;
			x = xtmp;
			if (!sk_X509_push(ctx->chain, x)) {
				X509_free(xtmp);
				X509error(ERR_R_MALLOC_FAILURE);
				ctx->error = X509_V_ERR_OUT_OF_MEM;
				ok = 0;
				goto end;
			}
			num++;
		}

		/* we now have our chain, lets check it... */
		trust = x509_vfy_check_trust(ctx);

		/* If explicitly rejected error */
		if (trust == X509_TRUST_REJECTED) {
			ok = 0;
			goto end;
		}
		/*
		 * If it's not explicitly trusted then check if there
		 * is an alternative chain that could be used. We only
		 * do this if we haven't already checked via
		 * TRUSTED_FIRST and the user hasn't switched off
		 * alternate chain checking
		 */
		retry = 0;
		if (trust != X509_TRUST_TRUSTED &&
		    !(ctx->param->flags & X509_V_FLAG_TRUSTED_FIRST) &&
		    !(ctx->param->flags & X509_V_FLAG_NO_ALT_CHAINS)) {
			while (j-- > 1) {
				xtmp2 = sk_X509_value(ctx->chain, j - 1);
				ok = ctx->get_issuer(&xtmp, ctx, xtmp2);
				if (ok < 0)
					goto end;
				/* Check if we found an alternate chain */
				if (ok > 0) {
					/*
					 * Free up the found cert
					 * we'll add it again later
					 */
					X509_free(xtmp);
					/*
					 * Dump all the certs above
					 * this point - we've found an
					 * alternate chain
					 */
					while (num > j) {
						xtmp = sk_X509_pop(ctx->chain);
						X509_free(xtmp);
						num--;
					}
					ctx->num_untrusted = sk_X509_num(ctx->chain);
					retry = 1;
					break;
				}
			}
		}
	} while (retry);

	/*
	 * If not explicitly trusted then indicate error unless it's a single
	 * self signed certificate in which case we've indicated an error already
	 * and set bad_chain == 1
	 */
	if (trust != X509_TRUST_TRUSTED && !bad_chain) {
		if ((chain_ss == NULL) || !ctx->check_issued(ctx, x, chain_ss)) {
			if (ctx->num_untrusted >= num)
				ctx->error = X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT_LOCALLY;
			else
				ctx->error = X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT;
			ctx->current_cert = x;
		} else {
			if (!sk_X509_push(ctx->chain, chain_ss)) {
				X509error(ERR_R_MALLOC_FAILURE);
				ctx->error = X509_V_ERR_OUT_OF_MEM;
				ok = 0;
				goto end;
			}
			num++;
			ctx->num_untrusted = num;
			ctx->current_cert = chain_ss;
			ctx->error = X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN;
			chain_ss = NULL;
		}

		ctx->error_depth = num - 1;
		bad_chain = 1;
		ok = cb(0, ctx);
		if (!ok)
			goto end;
	}

	ret = 1;
 end:
	sk_X509_free(sktmp);
	X509_free(chain_ss);
	*bad = bad_chain;
	*out_ok = ok;

	return ret;
}

static int
X509_verify_cert_legacy(X509_STORE_CTX *ctx)
{
	int ok = 0, bad_chain;

	ctx->error = X509_V_OK; /* Initialize to OK */

	if (!X509_verify_cert_legacy_build_chain(ctx, &bad_chain, &ok))
		goto end;

	/* We have the chain complete: now we need to check its purpose */
	ok = x509_vfy_check_chain_extensions(ctx);
	if (!ok)
		goto end;

	/* Check that the chain satisfies the security level. */
	ok = x509_vfy_check_security_level(ctx);
	if (!ok)
		goto end;

	/* Check name constraints */
	ok = check_name_constraints(ctx);
	if (!ok)
		goto end;

#ifndef OPENSSL_NO_RFC3779
	ok = X509v3_asid_validate_path(ctx);
	if (!ok)
		goto end;

	ok = X509v3_addr_validate_path(ctx);
	if (!ok)
		goto end;
#endif

	ok = x509_vfy_check_id(ctx);
	if (!ok)
		goto end;

	/*
	 * Check revocation status: we do this after copying parameters because
	 * they may be needed for CRL signature verification.
	 */
	ok = x509_vfy_check_revocation(ctx);
	if (!ok)
		goto end;

	/* At this point, we have a chain and need to verify it */
	if (ctx->verify != NULL)
		ok = ctx->verify(ctx);
	else
		ok = internal_verify(ctx);
	if (!ok)
		goto end;

	/* If we get this far evaluate policies */
	if (!bad_chain)
		ok = x509_vfy_check_policy(ctx);

 end:
	/* Safety net, error returns must set ctx->error */
	if (ok <= 0 && ctx->error == X509_V_OK)
		ctx->error = X509_V_ERR_UNSPECIFIED;

	return ok;
}

int
X509_verify_cert(X509_STORE_CTX *ctx)
{
	struct x509_verify_ctx *vctx = NULL;
	int chain_count = 0;

	if (ctx->cert == NULL) {
		X509error(X509_R_NO_CERT_SET_FOR_US_TO_VERIFY);
		ctx->error = X509_V_ERR_INVALID_CALL;
		return -1;
	}
	if (ctx->chain != NULL) {
		/*
		 * This X509_STORE_CTX has already been used to verify
		 * a cert. We cannot do another one.
		 */
		X509error(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		ctx->error = X509_V_ERR_INVALID_CALL;
		return -1;
	}
	if (ctx->param->poisoned) {
		/*
		 * This X509_STORE_CTX had failures setting
		 * up verify parameters. We can not use it.
		 */
		X509error(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		ctx->error = X509_V_ERR_INVALID_CALL;
		return -1;
	}
	if (ctx->error != X509_V_ERR_INVALID_CALL) {
		/*
		 * This X509_STORE_CTX has not been properly initialized.
		 */
		X509error(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		ctx->error = X509_V_ERR_INVALID_CALL;
		return -1;
	}

	/*
	 * If the certificate's public key is too weak, don't bother
	 * continuing.
	 */
	if (!check_key_level(ctx, ctx->cert) &&
	    !verify_cb_cert(ctx, ctx->cert, 0, X509_V_ERR_EE_KEY_TOO_SMALL))
		return 0;

	/*
	 * If flags request legacy, use the legacy verifier. If we
	 * requested "no alt chains" from the age of hammer pants, use
	 * the legacy verifier because the multi chain verifier really
	 * does find all the "alt chains".
	 *
	 * XXX deprecate the NO_ALT_CHAINS flag?
	 */
	if ((ctx->param->flags & X509_V_FLAG_LEGACY_VERIFY) ||
	    (ctx->param->flags & X509_V_FLAG_NO_ALT_CHAINS))
		return X509_verify_cert_legacy(ctx);

	/* Use the modern multi-chain verifier from x509_verify_cert */

	if ((vctx = x509_verify_ctx_new_from_xsc(ctx)) != NULL) {
		ctx->error = X509_V_OK; /* Initialize to OK */
		chain_count = x509_verify(vctx, NULL, NULL);
	}
	x509_verify_ctx_free(vctx);

	/* if we succeed we have a chain in ctx->chain */
	return chain_count > 0 && ctx->chain != NULL;
}
LCRYPTO_ALIAS(X509_verify_cert);

/* Given a STACK_OF(X509) find the issuer of cert (if any)
 */

static X509 *
find_issuer(X509_STORE_CTX *ctx, STACK_OF(X509) *sk, X509 *x,
    int allow_expired)
{
	int i;
	X509 *issuer, *rv = NULL;

	for (i = 0; i < sk_X509_num(sk); i++) {
		issuer = sk_X509_value(sk, i);
		if (ctx->check_issued(ctx, x, issuer)) {
			if (x509_check_cert_time(ctx, issuer, -1))
				return issuer;
			if (allow_expired)
				rv = issuer;
		}
	}
	return rv;
}

/* Given a possible certificate and issuer check them */

static int
check_issued(X509_STORE_CTX *ctx, X509 *subject, X509 *issuer)
{
	/*
	 * Yes, the arguments of X509_STORE_CTX_check_issued_fn were exposed in
	 * reverse order compared to the already public X509_check_issued()...
	 */
	return X509_check_issued(issuer, subject) == X509_V_OK;
}

/* Alternative lookup method: look from a STACK stored in ctx->trusted */

static int
x509_vfy_get_trusted_issuer(X509 **issuer, X509_STORE_CTX *ctx, X509 *x)
{
	*issuer = find_issuer(ctx, ctx->trusted, x, 1);
	if (*issuer) {
		CRYPTO_add(&(*issuer)->references, 1, CRYPTO_LOCK_X509);
		return 1;
	} else
		return 0;
}

/* Check a certificate chains extensions for consistency
 * with the supplied purpose
 */

int
x509_vfy_check_chain_extensions(X509_STORE_CTX *ctx)
{
	int i, ok = 0, must_be_ca, plen = 0;
	X509 *x;
	int (*cb)(int xok, X509_STORE_CTX *xctx);
	int proxy_path_length = 0;
	int purpose;

	cb = ctx->verify_cb;

	/* must_be_ca can have 1 of 3 values:
	   -1: we accept both CA and non-CA certificates, to allow direct
	       use of self-signed certificates (which are marked as CA).
	   0:  we only accept non-CA certificates.  This is currently not
	       used, but the possibility is present for future extensions.
	   1:  we only accept CA certificates.  This is currently used for
	       all certificates in the chain except the leaf certificate.
	*/
	must_be_ca = -1;

	/* CRL path validation */
	if (ctx->parent)
		purpose = X509_PURPOSE_CRL_SIGN;
	else
		purpose = ctx->param->purpose;

	/* Check all untrusted certificates */
	for (i = 0; i < ctx->num_untrusted; i++) {
		int ret;
		x = sk_X509_value(ctx->chain, i);
		if (!(ctx->param->flags & X509_V_FLAG_IGNORE_CRITICAL) &&
		    (x->ex_flags & EXFLAG_CRITICAL)) {
			ctx->error = X509_V_ERR_UNHANDLED_CRITICAL_EXTENSION;
			ctx->error_depth = i;
			ctx->current_cert = x;
			ok = cb(0, ctx);
			if (!ok)
				goto end;
		}
		ret = X509_check_ca(x);
		if (must_be_ca == -1) {
			if ((ctx->param->flags & X509_V_FLAG_X509_STRICT) &&
			    (ret != 1) && (ret != 0)) {
				ret = 0;
				ctx->error = X509_V_ERR_INVALID_CA;
			} else
				ret = 1;
		} else {
			if ((ret == 0) ||
			    ((ctx->param->flags & X509_V_FLAG_X509_STRICT) &&
			    (ret != 1))) {
				ret = 0;
				ctx->error = X509_V_ERR_INVALID_CA;
			} else
				ret = 1;
		}
		if (ret == 0) {
			ctx->error_depth = i;
			ctx->current_cert = x;
			ok = cb(0, ctx);
			if (!ok)
				goto end;
		}
		if (ctx->param->purpose > 0) {
			ret = X509_check_purpose(x, purpose, must_be_ca > 0);
			if ((ret == 0) ||
			    ((ctx->param->flags & X509_V_FLAG_X509_STRICT) &&
			    (ret != 1))) {
				ctx->error = X509_V_ERR_INVALID_PURPOSE;
				ctx->error_depth = i;
				ctx->current_cert = x;
				ok = cb(0, ctx);
				if (!ok)
					goto end;
			}
		}
		/* Check pathlen if not self issued */
		if ((i > 1) && !(x->ex_flags & EXFLAG_SI) &&
		    (x->ex_pathlen != -1) &&
		    (plen > (x->ex_pathlen + proxy_path_length + 1))) {
			ctx->error = X509_V_ERR_PATH_LENGTH_EXCEEDED;
			ctx->error_depth = i;
			ctx->current_cert = x;
			ok = cb(0, ctx);
			if (!ok)
				goto end;
		}
		/* Increment path length if not self issued */
		if (!(x->ex_flags & EXFLAG_SI))
			plen++;
		must_be_ca = 1;
	}

	ok = 1;

 end:
	return ok;
}

static int
check_name_constraints(X509_STORE_CTX *ctx)
{
	if (!x509_constraints_chain(ctx->chain, &ctx->error,
	    &ctx->error_depth)) {
		ctx->current_cert = sk_X509_value(ctx->chain, ctx->error_depth);
		if (!ctx->verify_cb(0, ctx))
			return 0;
	}
	return 1;
}

/* Given a certificate try and find an exact match in the store */

static X509 *
lookup_cert_match(X509_STORE_CTX *ctx, X509 *x)
{
	STACK_OF(X509) *certs;
	X509 *xtmp = NULL;
	size_t i;

	/* Lookup all certs with matching subject name */
	certs = X509_STORE_CTX_get1_certs(ctx, X509_get_subject_name(x));
	if (certs == NULL)
		return NULL;

	/* Look for exact match */
	for (i = 0; i < sk_X509_num(certs); i++) {
		xtmp = sk_X509_value(certs, i);
		if (!X509_cmp(xtmp, x))
			break;
	}

	if (i < sk_X509_num(certs))
		X509_up_ref(xtmp);
	else
		xtmp = NULL;

	sk_X509_pop_free(certs, X509_free);
	return xtmp;
}

X509 *
x509_vfy_lookup_cert_match(X509_STORE_CTX *ctx, X509 *x)
{
	if (ctx->store == NULL || ctx->store->objs == NULL)
		return NULL;
	return lookup_cert_match(ctx, x);
}

int
x509_vfy_check_trust(X509_STORE_CTX *ctx)
{
	size_t i;
	int ok;
	X509 *x = NULL;
	int (*cb) (int xok, X509_STORE_CTX *xctx);

	cb = ctx->verify_cb;
	/* Check all trusted certificates in chain */
	for (i = ctx->num_untrusted; i < sk_X509_num(ctx->chain); i++) {
		x = sk_X509_value(ctx->chain, i);
		ok = X509_check_trust(x, ctx->param->trust, 0);

		/* If explicitly trusted return trusted */
		if (ok == X509_TRUST_TRUSTED)
			return X509_TRUST_TRUSTED;
		/*
		 * If explicitly rejected notify callback and reject if not
		 * overridden.
		 */
		if (ok == X509_TRUST_REJECTED) {
			ctx->error_depth = i;
			ctx->current_cert = x;
			ctx->error = X509_V_ERR_CERT_REJECTED;
			ok = cb(0, ctx);
			if (!ok)
				return X509_TRUST_REJECTED;
		}
	}
	/*
	 * If we accept partial chains and have at least one trusted certificate
	 * return success.
	 */
	if (ctx->param->flags & X509_V_FLAG_PARTIAL_CHAIN) {
		X509 *mx;
		if (ctx->num_untrusted < (int)sk_X509_num(ctx->chain))
			return X509_TRUST_TRUSTED;
		x = sk_X509_value(ctx->chain, 0);
		mx = lookup_cert_match(ctx, x);
		if (mx) {
			(void)sk_X509_set(ctx->chain, 0, mx);
			X509_free(x);
			ctx->num_untrusted = 0;
			return X509_TRUST_TRUSTED;
		}
	}

	/*
	 * If no trusted certs in chain at all return untrusted and allow
	 * standard (no issuer cert) etc errors to be indicated.
	 */
	return X509_TRUST_UNTRUSTED;
}

int
x509_vfy_check_revocation(X509_STORE_CTX *ctx)
{
	int i, last, ok;

	if (!(ctx->param->flags & X509_V_FLAG_CRL_CHECK))
		return 1;
	if (ctx->param->flags & X509_V_FLAG_CRL_CHECK_ALL)
		last = sk_X509_num(ctx->chain) - 1;
	else {
		/* If checking CRL paths this isn't the EE certificate */
		if (ctx->parent)
			return 1;
		last = 0;
	}
	for (i = 0; i <= last; i++) {
		ok = check_cert(ctx, ctx->chain, i);
		if (!ok)
			return ok;
	}
	return 1;
}

static int
check_cert(X509_STORE_CTX *ctx, STACK_OF(X509) *chain, int depth)
{
	X509_CRL *crl = NULL, *dcrl = NULL;
	X509 *x;
	int ok = 0, cnum;
	unsigned int last_reasons;

	cnum = ctx->error_depth = depth;
	x = sk_X509_value(chain, cnum);
	ctx->current_cert = x;
	ctx->current_issuer = NULL;
	ctx->current_crl_score = 0;
	ctx->current_reasons = 0;
	while (ctx->current_reasons != CRLDP_ALL_REASONS) {
		last_reasons = ctx->current_reasons;
		/* Try to retrieve relevant CRL */
		ok = get_crl_delta(ctx, &crl, &dcrl, x);
		if (!ok) {
			ctx->error = X509_V_ERR_UNABLE_TO_GET_CRL;
			ok = ctx->verify_cb(0, ctx);
			goto err;
		}
		ctx->current_crl = crl;
		ok = x509_vfy_check_crl(ctx, crl);
		if (!ok)
			goto err;

		if (dcrl) {
			ok = x509_vfy_check_crl(ctx, dcrl);
			if (!ok)
				goto err;
			ok = x509_vfy_cert_crl(ctx, dcrl, x);
			if (!ok)
				goto err;
		} else
			ok = 1;

		/* Don't look in full CRL if delta reason is removefromCRL */
		if (ok != 2) {
			ok = x509_vfy_cert_crl(ctx, crl, x);
			if (!ok)
				goto err;
		}

		ctx->current_crl = NULL;
		X509_CRL_free(crl);
		X509_CRL_free(dcrl);
		crl = NULL;
		dcrl = NULL;
		/* If reasons not updated we wont get anywhere by
		 * another iteration, so exit loop.
		 */
		if (last_reasons == ctx->current_reasons) {
			ctx->error = X509_V_ERR_UNABLE_TO_GET_CRL;
			ok = ctx->verify_cb(0, ctx);
			goto err;
		}
	}

err:
	ctx->current_crl = NULL;
	X509_CRL_free(crl);
	X509_CRL_free(dcrl);
	return ok;
}

/* Check CRL times against values in X509_STORE_CTX */

static int
check_crl_time(X509_STORE_CTX *ctx, X509_CRL *crl, int notify)
{
	time_t *ptime;
	int i;

	if (notify)
		ctx->current_crl = crl;
	if (ctx->param->flags & X509_V_FLAG_USE_CHECK_TIME)
		ptime = &ctx->param->check_time;
	else if (ctx->param->flags & X509_V_FLAG_NO_CHECK_TIME)
		return 1;
	else
		ptime = NULL;

	i = X509_cmp_time(X509_CRL_get_lastUpdate(crl), ptime);
	if (i == 0) {
		if (!notify)
			return 0;
		ctx->error = X509_V_ERR_ERROR_IN_CRL_LAST_UPDATE_FIELD;
		if (!ctx->verify_cb(0, ctx))
			return 0;
	}

	if (i > 0) {
		if (!notify)
			return 0;
		ctx->error = X509_V_ERR_CRL_NOT_YET_VALID;
		if (!ctx->verify_cb(0, ctx))
			return 0;
	}

	if (X509_CRL_get_nextUpdate(crl)) {
		i = X509_cmp_time(X509_CRL_get_nextUpdate(crl), ptime);

		if (i == 0) {
			if (!notify)
				return 0;
			ctx->error = X509_V_ERR_ERROR_IN_CRL_NEXT_UPDATE_FIELD;
			if (!ctx->verify_cb(0, ctx))
				return 0;
		}
		/* Ignore expiry of base CRL is delta is valid */
		if ((i < 0) &&
		    !(ctx->current_crl_score & CRL_SCORE_TIME_DELTA)) {
			if (!notify)
				return 0;
			ctx->error = X509_V_ERR_CRL_HAS_EXPIRED;
			if (!ctx->verify_cb(0, ctx))
				return 0;
		}
	}

	if (notify)
		ctx->current_crl = NULL;

	return 1;
}

static int
get_crl_sk(X509_STORE_CTX *ctx, X509_CRL **pcrl, X509_CRL **pdcrl,
    X509 **pissuer, int *pscore, unsigned int *preasons,
    STACK_OF(X509_CRL) *crls)
{
	int i, crl_score, best_score = *pscore;
	unsigned int reasons, best_reasons = 0;
	X509 *x = ctx->current_cert;
	X509_CRL *crl, *best_crl = NULL;
	X509 *crl_issuer = NULL, *best_crl_issuer = NULL;

	for (i = 0; i < sk_X509_CRL_num(crls); i++) {
		crl = sk_X509_CRL_value(crls, i);
		reasons = *preasons;
		crl_score = get_crl_score(ctx, &crl_issuer, &reasons, crl, x);

		if (crl_score > best_score) {
			best_crl = crl;
			best_crl_issuer = crl_issuer;
			best_score = crl_score;
			best_reasons = reasons;
		}
	}

	if (best_crl) {
		if (*pcrl)
			X509_CRL_free(*pcrl);
		*pcrl = best_crl;
		*pissuer = best_crl_issuer;
		*pscore = best_score;
		*preasons = best_reasons;
		CRYPTO_add(&best_crl->references, 1, CRYPTO_LOCK_X509_CRL);
		if (*pdcrl) {
			X509_CRL_free(*pdcrl);
			*pdcrl = NULL;
		}
		get_delta_sk(ctx, pdcrl, pscore, best_crl, crls);
	}

	if (best_score >= CRL_SCORE_VALID)
		return 1;

	return 0;
}

/* Compare two CRL extensions for delta checking purposes. They should be
 * both present or both absent. If both present all fields must be identical.
 */

static int
crl_extension_match(X509_CRL *a, X509_CRL *b, int nid)
{
	ASN1_OCTET_STRING *exta, *extb;
	int i;

	i = X509_CRL_get_ext_by_NID(a, nid, -1);
	if (i >= 0) {
		/* Can't have multiple occurrences */
		if (X509_CRL_get_ext_by_NID(a, nid, i) != -1)
			return 0;
		exta = X509_EXTENSION_get_data(X509_CRL_get_ext(a, i));
	} else
		exta = NULL;

	i = X509_CRL_get_ext_by_NID(b, nid, -1);

	if (i >= 0) {
		if (X509_CRL_get_ext_by_NID(b, nid, i) != -1)
			return 0;
		extb = X509_EXTENSION_get_data(X509_CRL_get_ext(b, i));
	} else
		extb = NULL;

	if (!exta && !extb)
		return 1;

	if (!exta || !extb)
		return 0;

	if (ASN1_OCTET_STRING_cmp(exta, extb))
		return 0;

	return 1;
}

/* See if a base and delta are compatible */

static int
check_delta_base(X509_CRL *delta, X509_CRL *base)
{
	/* Delta CRL must be a delta */
	if (!delta->base_crl_number)
		return 0;
	/* Base must have a CRL number */
	if (!base->crl_number)
		return 0;
	/* Issuer names must match */
	if (X509_NAME_cmp(X509_CRL_get_issuer(base),
	    X509_CRL_get_issuer(delta)))
		return 0;
	/* AKID and IDP must match */
	if (!crl_extension_match(delta, base, NID_authority_key_identifier))
		return 0;
	if (!crl_extension_match(delta, base, NID_issuing_distribution_point))
		return 0;
	/* Delta CRL base number must not exceed Full CRL number. */
	if (ASN1_INTEGER_cmp(delta->base_crl_number, base->crl_number) > 0)
		return 0;
	/* Delta CRL number must exceed full CRL number */
	if (ASN1_INTEGER_cmp(delta->crl_number, base->crl_number) > 0)
		return 1;
	return 0;
}

/* For a given base CRL find a delta... maybe extend to delta scoring
 * or retrieve a chain of deltas...
 */

static void
get_delta_sk(X509_STORE_CTX *ctx, X509_CRL **dcrl, int *pscore, X509_CRL *base,
    STACK_OF(X509_CRL) *crls)
{
	X509_CRL *delta;
	int i;

	if (!(ctx->param->flags & X509_V_FLAG_USE_DELTAS))
		return;
	if (!((ctx->current_cert->ex_flags | base->flags) & EXFLAG_FRESHEST))
		return;
	for (i = 0; i < sk_X509_CRL_num(crls); i++) {
		delta = sk_X509_CRL_value(crls, i);
		if (check_delta_base(delta, base)) {
			if (check_crl_time(ctx, delta, 0))
				*pscore |= CRL_SCORE_TIME_DELTA;
			CRYPTO_add(&delta->references, 1, CRYPTO_LOCK_X509_CRL);
			*dcrl = delta;
			return;
		}
	}
	*dcrl = NULL;
}

/* For a given CRL return how suitable it is for the supplied certificate 'x'.
 * The return value is a mask of several criteria.
 * If the issuer is not the certificate issuer this is returned in *pissuer.
 * The reasons mask is also used to determine if the CRL is suitable: if
 * no new reasons the CRL is rejected, otherwise reasons is updated.
 */

static int
get_crl_score(X509_STORE_CTX *ctx, X509 **pissuer, unsigned int *preasons,
    X509_CRL *crl, X509 *x)
{
	int crl_score = 0;
	unsigned int tmp_reasons = *preasons, crl_reasons;

	/* First see if we can reject CRL straight away */

	/* Invalid IDP cannot be processed */
	if (crl->idp_flags & IDP_INVALID)
		return 0;
	/* Reason codes or indirect CRLs need extended CRL support */
	if (!(ctx->param->flags & X509_V_FLAG_EXTENDED_CRL_SUPPORT)) {
		if (crl->idp_flags & (IDP_INDIRECT | IDP_REASONS))
			return 0;
	} else if (crl->idp_flags & IDP_REASONS) {
		/* If no new reasons reject */
		if (!(crl->idp_reasons & ~tmp_reasons))
			return 0;
	}
	/* Don't process deltas at this stage */
	else if (crl->base_crl_number)
		return 0;
	/* If issuer name doesn't match certificate need indirect CRL */
	if (X509_NAME_cmp(X509_get_issuer_name(x), X509_CRL_get_issuer(crl))) {
		if (!(crl->idp_flags & IDP_INDIRECT))
			return 0;
	} else
		crl_score |= CRL_SCORE_ISSUER_NAME;

	if (!(crl->flags & EXFLAG_CRITICAL))
		crl_score |= CRL_SCORE_NOCRITICAL;

	/* Check expiry */
	if (check_crl_time(ctx, crl, 0))
		crl_score |= CRL_SCORE_TIME;

	/* Check authority key ID and locate certificate issuer */
	crl_akid_check(ctx, crl, pissuer, &crl_score);

	/* If we can't locate certificate issuer at this point forget it */

	if (!(crl_score & CRL_SCORE_AKID))
		return 0;

	/* Check cert for matching CRL distribution points */

	if (crl_crldp_check(x, crl, crl_score, &crl_reasons)) {
		/* If no new reasons reject */
		if (!(crl_reasons & ~tmp_reasons))
			return 0;
		tmp_reasons |= crl_reasons;
		crl_score |= CRL_SCORE_SCOPE;
	}

	*preasons = tmp_reasons;

	return crl_score;
}

static void
crl_akid_check(X509_STORE_CTX *ctx, X509_CRL *crl, X509 **pissuer,
    int *pcrl_score)
{
	X509 *crl_issuer = NULL;
	X509_NAME *cnm = X509_CRL_get_issuer(crl);
	int cidx = ctx->error_depth;
	int i;

	if (cidx != sk_X509_num(ctx->chain) - 1)
		cidx++;

	crl_issuer = sk_X509_value(ctx->chain, cidx);

	if (X509_check_akid(crl_issuer, crl->akid) == X509_V_OK) {
		if (*pcrl_score & CRL_SCORE_ISSUER_NAME) {
			*pcrl_score |= CRL_SCORE_AKID|CRL_SCORE_ISSUER_CERT;
			*pissuer = crl_issuer;
			return;
		}
	}

	for (cidx++; cidx < sk_X509_num(ctx->chain); cidx++) {
		crl_issuer = sk_X509_value(ctx->chain, cidx);
		if (X509_NAME_cmp(X509_get_subject_name(crl_issuer), cnm))
			continue;
		if (X509_check_akid(crl_issuer, crl->akid) == X509_V_OK) {
			*pcrl_score |= CRL_SCORE_AKID|CRL_SCORE_SAME_PATH;
			*pissuer = crl_issuer;
			return;
		}
	}

	/* Anything else needs extended CRL support */

	if (!(ctx->param->flags & X509_V_FLAG_EXTENDED_CRL_SUPPORT))
		return;

	/* Otherwise the CRL issuer is not on the path. Look for it in the
	 * set of untrusted certificates.
	 */
	for (i = 0; i < sk_X509_num(ctx->untrusted); i++) {
		crl_issuer = sk_X509_value(ctx->untrusted, i);
		if (X509_NAME_cmp(X509_get_subject_name(crl_issuer), cnm))
			continue;
		if (X509_check_akid(crl_issuer, crl->akid) == X509_V_OK) {
			*pissuer = crl_issuer;
			*pcrl_score |= CRL_SCORE_AKID;
			return;
		}
	}
}

/* Check the path of a CRL issuer certificate. This creates a new
 * X509_STORE_CTX and populates it with most of the parameters from the
 * parent. This could be optimised somewhat since a lot of path checking
 * will be duplicated by the parent, but this will rarely be used in
 * practice.
 */

static int
check_crl_path(X509_STORE_CTX *ctx, X509 *x)
{
	X509_STORE_CTX crl_ctx;
	int ret;

	/* Don't allow recursive CRL path validation */
	if (ctx->parent)
		return 0;
	if (!X509_STORE_CTX_init(&crl_ctx, ctx->store, x, ctx->untrusted)) {
		ret = -1;
		goto err;
	}

	crl_ctx.crls = ctx->crls;
	/* Copy verify params across */
	X509_STORE_CTX_set0_param(&crl_ctx, ctx->param);

	crl_ctx.parent = ctx;
	crl_ctx.verify_cb = ctx->verify_cb;

	/* Verify CRL issuer */
	ret = X509_verify_cert(&crl_ctx);

	if (ret <= 0)
		goto err;

	/* Check chain is acceptable */
	ret = check_crl_chain(ctx, ctx->chain, crl_ctx.chain);

err:
	X509_STORE_CTX_cleanup(&crl_ctx);
	return ret;
}

/* RFC3280 says nothing about the relationship between CRL path
 * and certificate path, which could lead to situations where a
 * certificate could be revoked or validated by a CA not authorised
 * to do so. RFC5280 is more strict and states that the two paths must
 * end in the same trust anchor, though some discussions remain...
 * until this is resolved we use the RFC5280 version
 */

static int
check_crl_chain(X509_STORE_CTX *ctx, STACK_OF(X509) *cert_path,
    STACK_OF(X509) *crl_path)
{
	X509 *cert_ta, *crl_ta;

	cert_ta = sk_X509_value(cert_path, sk_X509_num(cert_path) - 1);
	crl_ta = sk_X509_value(crl_path, sk_X509_num(crl_path) - 1);
	if (!X509_cmp(cert_ta, crl_ta))
		return 1;
	return 0;
}

/* Check for match between two dist point names: three separate cases.
 * 1. Both are relative names and compare X509_NAME types.
 * 2. One full, one relative. Compare X509_NAME to GENERAL_NAMES.
 * 3. Both are full names and compare two GENERAL_NAMES.
 * 4. One is NULL: automatic match.
 */

static int
idp_check_dp(DIST_POINT_NAME *a, DIST_POINT_NAME *b)
{
	X509_NAME *nm = NULL;
	GENERAL_NAMES *gens = NULL;
	GENERAL_NAME *gena, *genb;
	int i, j;

	if (!a || !b)
		return 1;
	if (a->type == 1) {
		if (!a->dpname)
			return 0;
		/* Case 1: two X509_NAME */
		if (b->type == 1) {
			if (!b->dpname)
				return 0;
			if (!X509_NAME_cmp(a->dpname, b->dpname))
				return 1;
			else
				return 0;
		}
		/* Case 2: set name and GENERAL_NAMES appropriately */
		nm = a->dpname;
		gens = b->name.fullname;
	} else if (b->type == 1) {
		if (!b->dpname)
			return 0;
		/* Case 2: set name and GENERAL_NAMES appropriately */
		gens = a->name.fullname;
		nm = b->dpname;
	}

	/* Handle case 2 with one GENERAL_NAMES and one X509_NAME */
	if (nm) {
		for (i = 0; i < sk_GENERAL_NAME_num(gens); i++) {
			gena = sk_GENERAL_NAME_value(gens, i);
			if (gena->type != GEN_DIRNAME)
				continue;
			if (!X509_NAME_cmp(nm, gena->d.directoryName))
				return 1;
		}
		return 0;
	}

	/* Else case 3: two GENERAL_NAMES */

	for (i = 0; i < sk_GENERAL_NAME_num(a->name.fullname); i++) {
		gena = sk_GENERAL_NAME_value(a->name.fullname, i);
		for (j = 0; j < sk_GENERAL_NAME_num(b->name.fullname); j++) {
			genb = sk_GENERAL_NAME_value(b->name.fullname, j);
			if (!GENERAL_NAME_cmp(gena, genb))
				return 1;
		}
	}

	return 0;
}

static int
crldp_check_crlissuer(DIST_POINT *dp, X509_CRL *crl, int crl_score)
{
	int i;
	X509_NAME *nm = X509_CRL_get_issuer(crl);

	/* If no CRLissuer return is successful iff don't need a match */
	if (!dp->CRLissuer)
		return !!(crl_score & CRL_SCORE_ISSUER_NAME);
	for (i = 0; i < sk_GENERAL_NAME_num(dp->CRLissuer); i++) {
		GENERAL_NAME *gen = sk_GENERAL_NAME_value(dp->CRLissuer, i);
		if (gen->type != GEN_DIRNAME)
			continue;
		if (!X509_NAME_cmp(gen->d.directoryName, nm))
			return 1;
	}
	return 0;
}

/* Check CRLDP and IDP */

static int
crl_crldp_check(X509 *x, X509_CRL *crl, int crl_score, unsigned int *preasons)
{
	int i;

	if (crl->idp_flags & IDP_ONLYATTR)
		return 0;
	if (x->ex_flags & EXFLAG_CA) {
		if (crl->idp_flags & IDP_ONLYUSER)
			return 0;
	} else {
		if (crl->idp_flags & IDP_ONLYCA)
			return 0;
	}
	*preasons = crl->idp_reasons;
	for (i = 0; i < sk_DIST_POINT_num(x->crldp); i++) {
		DIST_POINT *dp = sk_DIST_POINT_value(x->crldp, i);
		if (crldp_check_crlissuer(dp, crl, crl_score)) {
			if (!crl->idp ||
			    idp_check_dp(dp->distpoint, crl->idp->distpoint)) {
				*preasons &= dp->dp_reasons;
				return 1;
			}
		}
	}
	if ((!crl->idp || !crl->idp->distpoint) &&
	    (crl_score & CRL_SCORE_ISSUER_NAME))
		return 1;
	return 0;
}

/* Retrieve CRL corresponding to current certificate.
 * If deltas enabled try to find a delta CRL too
 */

static int
get_crl_delta(X509_STORE_CTX *ctx, X509_CRL **pcrl, X509_CRL **pdcrl, X509 *x)
{
	int ok;
	X509 *issuer = NULL;
	int crl_score = 0;
	unsigned int reasons;
	X509_CRL *crl = NULL, *dcrl = NULL;
	STACK_OF(X509_CRL) *skcrl;
	X509_NAME *nm = X509_get_issuer_name(x);

	reasons = ctx->current_reasons;
	ok = get_crl_sk(ctx, &crl, &dcrl, &issuer, &crl_score, &reasons,
	    ctx->crls);
	if (ok)
		goto done;

	/* Lookup CRLs from store */
	skcrl = X509_STORE_CTX_get1_crls(ctx, nm);

	/* If no CRLs found and a near match from get_crl_sk use that */
	if (!skcrl && crl)
		goto done;

	get_crl_sk(ctx, &crl, &dcrl, &issuer, &crl_score, &reasons, skcrl);

	sk_X509_CRL_pop_free(skcrl, X509_CRL_free);

done:

	/* If we got any kind of CRL use it and return success */
	if (crl) {
		ctx->current_issuer = issuer;
		ctx->current_crl_score = crl_score;
		ctx->current_reasons = reasons;
		*pcrl = crl;
		*pdcrl = dcrl;
		return 1;
	}

	return 0;
}

/* Matches x509_verify_parent_signature() */
static int
x509_crl_verify_parent_signature(X509 *parent, X509_CRL *crl, int *error)
{
	EVP_PKEY *pkey;
	int cached;
	int ret = 0;

	/* Use cached value if we have it */
	if ((cached = x509_issuer_cache_find(parent->hash, crl->hash)) >= 0) {
		if (cached == 0)
			*error = X509_V_ERR_CRL_SIGNATURE_FAILURE;
		return cached;
	}

	/* Check signature. Did parent sign crl? */
	if ((pkey = X509_get0_pubkey(parent)) == NULL) {
		*error = X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY;
		return 0;
	}
	if (X509_CRL_verify(crl, pkey) <= 0)
		*error = X509_V_ERR_CRL_SIGNATURE_FAILURE;
	else
		ret = 1;

	/* Add result to cache */
	x509_issuer_cache_add(parent->hash, crl->hash, ret);

	return ret;
}

/* Check CRL validity */
static int
x509_vfy_check_crl(X509_STORE_CTX *ctx, X509_CRL *crl)
{
	X509 *issuer = NULL;
	int ok = 0, chnum, cnum;

	cnum = ctx->error_depth;
	chnum = sk_X509_num(ctx->chain) - 1;
	/* if we have an alternative CRL issuer cert use that */
	if (ctx->current_issuer) {
		issuer = ctx->current_issuer;
	} else if (cnum < chnum) {
		/*
		 * Else find CRL issuer: if not last certificate then issuer
		 * is next certificate in chain.
		 */
		issuer = sk_X509_value(ctx->chain, cnum + 1);
	} else {
		issuer = sk_X509_value(ctx->chain, chnum);
		/* If not self signed, can't check signature */
		if (!ctx->check_issued(ctx, issuer, issuer)) {
			ctx->error = X509_V_ERR_UNABLE_TO_GET_CRL_ISSUER;
			ok = ctx->verify_cb(0, ctx);
			if (!ok)
				goto err;
		}
	}

	if (issuer) {
		/* Skip most tests for deltas because they have already
		 * been done
		 */
		if (!crl->base_crl_number) {
			/* Check for cRLSign bit if keyUsage present */
			if ((issuer->ex_flags & EXFLAG_KUSAGE) &&
			    !(issuer->ex_kusage & KU_CRL_SIGN)) {
				ctx->error = X509_V_ERR_KEYUSAGE_NO_CRL_SIGN;
				ok = ctx->verify_cb(0, ctx);
				if (!ok)
					goto err;
			}

			if (!(ctx->current_crl_score & CRL_SCORE_SCOPE)) {
				ctx->error = X509_V_ERR_DIFFERENT_CRL_SCOPE;
				ok = ctx->verify_cb(0, ctx);
				if (!ok)
					goto err;
			}

			if (!(ctx->current_crl_score & CRL_SCORE_SAME_PATH)) {
				if (check_crl_path(ctx,
				    ctx->current_issuer) <= 0) {
					ctx->error = X509_V_ERR_CRL_PATH_VALIDATION_ERROR;
					ok = ctx->verify_cb(0, ctx);
					if (!ok)
						goto err;
				}
			}

			if (crl->idp_flags & IDP_INVALID) {
				ctx->error = X509_V_ERR_INVALID_EXTENSION;
				ok = ctx->verify_cb(0, ctx);
				if (!ok)
					goto err;
			}


		}

		if (!(ctx->current_crl_score & CRL_SCORE_TIME)) {
			ok = check_crl_time(ctx, crl, 1);
			if (!ok)
				goto err;
		}

		if (!x509_crl_verify_parent_signature(issuer, crl, &ctx->error)) {
			ok = ctx->verify_cb(0, ctx);
			if (!ok)
				goto err;
		}
	}

	ok = 1;

 err:
	return ok;
}

/* Check certificate against CRL */
static int
x509_vfy_cert_crl(X509_STORE_CTX *ctx, X509_CRL *crl, X509 *x)
{
	int ok;
	X509_REVOKED *rev;

	/* The rules changed for this... previously if a CRL contained
	 * unhandled critical extensions it could still be used to indicate
	 * a certificate was revoked. This has since been changed since
	 * critical extension can change the meaning of CRL entries.
	 */
	if (!(ctx->param->flags & X509_V_FLAG_IGNORE_CRITICAL) &&
	    (crl->flags & EXFLAG_CRITICAL)) {
		ctx->error = X509_V_ERR_UNHANDLED_CRITICAL_CRL_EXTENSION;
		ok = ctx->verify_cb(0, ctx);
		if (!ok)
			return 0;
	}
	/* Look for serial number of certificate in CRL
	 * If found make sure reason is not removeFromCRL.
	 */
	if (X509_CRL_get0_by_cert(crl, &rev, x)) {
		if (rev->reason == CRL_REASON_REMOVE_FROM_CRL)
			return 2;
		ctx->error = X509_V_ERR_CERT_REVOKED;
		ok = ctx->verify_cb(0, ctx);
		if (!ok)
			return 0;
	}

	return 1;
}

int
x509_vfy_check_policy(X509_STORE_CTX *ctx)
{
	X509 *current_cert = NULL;
	int ret;

	if (ctx->parent != NULL)
		return 1;

	ret = X509_policy_check(ctx->chain, ctx->param->policies,
	    ctx->param->flags, &current_cert);
	if (ret != X509_V_OK) {
		ctx->current_cert = current_cert;
		ctx->error = ret;
		if (ret == X509_V_ERR_OUT_OF_MEM)
			return 0;
		return ctx->verify_cb(0, ctx);
	}

	if (ctx->param->flags & X509_V_FLAG_NOTIFY_POLICY) {
		ctx->current_cert = NULL;
		/*
		 * Verification errors need to be "sticky", a callback may have
		 * allowed an SSL handshake to continue despite an error, and
		 * we must then remain in an error state.  Therefore, we MUST
		 * NOT clear earlier verification errors by setting the error
		 * to X509_V_OK.
		 */
		if (!ctx->verify_cb(2, ctx))
			return 0;
	}

	return 1;
}

/*
 * Inform the verify callback of an error.
 *
 * If x is not NULL it is the error cert, otherwise use the chain cert
 * at depth.
 *
 * If err is not X509_V_OK, that's the error value, otherwise leave
 * unchanged (presumably set by the caller).
 *
 * Returns 0 to abort verification with an error, non-zero to continue.
 */
static int
verify_cb_cert(X509_STORE_CTX *ctx, X509 *x, int depth, int err)
{
	ctx->error_depth = depth;
	ctx->current_cert = (x != NULL) ? x : sk_X509_value(ctx->chain, depth);
	if (err != X509_V_OK)
		ctx->error = err;
	return ctx->verify_cb(0, ctx);
}

/*
 * Check certificate validity times.
 *
 * If depth >= 0, invoke verification callbacks on error, otherwise just return
 * the validation status.
 *
 * Return 1 on success, 0 otherwise.
 */
int
x509_check_cert_time(X509_STORE_CTX *ctx, X509 *x, int depth)
{
	time_t ptime;
	int i;

	if (ctx->param->flags & X509_V_FLAG_USE_CHECK_TIME)
		ptime = ctx->param->check_time;
	else if (ctx->param->flags & X509_V_FLAG_NO_CHECK_TIME)
		return 1;
	else
		ptime = time(NULL);

	i = X509_cmp_time(X509_get_notBefore(x), &ptime);

	if (i >= 0 && depth < 0)
		return 0;
	if (i == 0 && !verify_cb_cert(ctx, x, depth,
	    X509_V_ERR_ERROR_IN_CERT_NOT_BEFORE_FIELD))
		return 0;
	if (i > 0 && !verify_cb_cert(ctx, x, depth,
	    X509_V_ERR_CERT_NOT_YET_VALID))
		return 0;

	i = X509_cmp_time_internal(X509_get_notAfter(x), &ptime, 1);

	if (i <= 0 && depth < 0)
		return 0;
	if (i == 0 && !verify_cb_cert(ctx, x, depth,
	    X509_V_ERR_ERROR_IN_CERT_NOT_AFTER_FIELD))
		return 0;
	if (i < 0 && !verify_cb_cert(ctx, x, depth,
	    X509_V_ERR_CERT_HAS_EXPIRED))
		return 0;

	return 1;
}

static int
x509_vfy_internal_verify(X509_STORE_CTX *ctx, int chain_verified)
{
	int n = sk_X509_num(ctx->chain) - 1;
	X509 *xi = sk_X509_value(ctx->chain, n);
	X509 *xs;

	if (ctx->check_issued(ctx, xi, xi))
		xs = xi;
	else {
		if (ctx->param->flags & X509_V_FLAG_PARTIAL_CHAIN) {
			xs = xi;
			goto check_cert;
		}
		if (n <= 0)
			return verify_cb_cert(ctx, xi, 0,
			    X509_V_ERR_UNABLE_TO_VERIFY_LEAF_SIGNATURE);
		n--;
		ctx->error_depth = n;
		xs = sk_X509_value(ctx->chain, n);
	}

	/*
	 * Do not clear ctx->error=0, it must be "sticky", only the
	 * user's callback is allowed to reset errors (at its own
	 * peril).
	 */
	while (n >= 0) {

		/*
		 * Skip signature check for self signed certificates
		 * unless explicitly asked for.  It doesn't add any
		 * security and just wastes time.  If the issuer's
		 * public key is unusable, report the issuer
		 * certificate and its depth (rather than the depth of
		 * the subject).
		 */
		if (!chain_verified && ( xs != xi ||
		    (ctx->param->flags & X509_V_FLAG_CHECK_SS_SIGNATURE))) {
			EVP_PKEY *pkey;
			if ((pkey = X509_get_pubkey(xi)) == NULL) {
				if (!verify_cb_cert(ctx, xi, xi != xs ? n+1 : n,
				    X509_V_ERR_UNABLE_TO_DECODE_ISSUER_PUBLIC_KEY))
					return 0;
			} else if (X509_verify(xs, pkey) <= 0) {
				if (!verify_cb_cert(ctx, xs, n,
				    X509_V_ERR_CERT_SIGNATURE_FAILURE)) {
					EVP_PKEY_free(pkey);
					return 0;
				}
			}
			EVP_PKEY_free(pkey);
		}
check_cert:
		/* Calls verify callback as needed */
		if (!chain_verified && !x509_check_cert_time(ctx, xs, n))
			return 0;

		/*
		 * Signal success at this depth.  However, the
		 * previous error (if any) is retained.
		 */
		ctx->current_issuer = xi;
		ctx->current_cert = xs;
		ctx->error_depth = n;
		if (!ctx->verify_cb(1, ctx))
			return 0;

		if (--n >= 0) {
			xi = xs;
			xs = sk_X509_value(ctx->chain, n);
		}
	}
	return 1;
}

static int
internal_verify(X509_STORE_CTX *ctx)
{
	return x509_vfy_internal_verify(ctx, 0);
}

/*
 * Internal verify, but with a chain where the verification
 * math has already been performed.
 */
int
x509_vfy_callback_indicate_completion(X509_STORE_CTX *ctx)
{
	return x509_vfy_internal_verify(ctx, 1);
}

int
X509_cmp_current_time(const ASN1_TIME *ctm)
{
	return X509_cmp_time(ctm, NULL);
}
LCRYPTO_ALIAS(X509_cmp_current_time);

/*
 * Compare a possibly unvalidated ASN1_TIME string against a time_t
 * using RFC 5280 rules for the time string. If *cmp_time is NULL
 * the current system time is used.
 *
 * XXX NOTE that unlike what you expect a "cmp" function to do in C,
 * XXX this one is "special", and returns 0 for error.
 *
 * Returns:
 * -1 if the ASN1_time is earlier than OR the same as *cmp_time.
 * 1 if the ASN1_time is later than *cmp_time.
 * 0 on error.
 */
static int
X509_cmp_time_internal(const ASN1_TIME *ctm, time_t *cmp_time, int is_notafter)
{
	time_t compare, cert_time;

	if (cmp_time == NULL)
		compare = time(NULL);
	else
		compare = *cmp_time;

	if (!x509_verify_asn1_time_to_time_t(ctm, is_notafter, &cert_time))
		return 0; /* invalid time */

	if (cert_time <= compare)
		return -1; /* 0 is used for error, so map same to less than */

	return 1;
}

int
X509_cmp_time(const ASN1_TIME *ctm, time_t *cmp_time)
{
	return X509_cmp_time_internal(ctm, cmp_time, 0);
}
LCRYPTO_ALIAS(X509_cmp_time);


ASN1_TIME *
X509_gmtime_adj(ASN1_TIME *s, long adj)
{
	return X509_time_adj(s, adj, NULL);
}
LCRYPTO_ALIAS(X509_gmtime_adj);

ASN1_TIME *
X509_time_adj(ASN1_TIME *s, long offset_sec, time_t *in_time)
{
	return X509_time_adj_ex(s, 0, offset_sec, in_time);
}
LCRYPTO_ALIAS(X509_time_adj);

ASN1_TIME *
X509_time_adj_ex(ASN1_TIME *s, int offset_day, long offset_sec, time_t *in_time)
{
	time_t t;
	if (in_time == NULL)
		t = time(NULL);
	else
		t = *in_time;

	return ASN1_TIME_adj(s, t, offset_day, offset_sec);
}
LCRYPTO_ALIAS(X509_time_adj_ex);

int
X509_get_pubkey_parameters(EVP_PKEY *pkey, STACK_OF(X509) *chain)
{
	EVP_PKEY *ktmp = NULL, *ktmp2;
	int i, j;

	if ((pkey != NULL) && !EVP_PKEY_missing_parameters(pkey))
		return 1;

	for (i = 0; i < sk_X509_num(chain); i++) {
		ktmp = X509_get0_pubkey(sk_X509_value(chain, i));
		if (ktmp == NULL) {
			X509error(X509_R_UNABLE_TO_GET_CERTS_PUBLIC_KEY);
			return 0;
		}
		if (!EVP_PKEY_missing_parameters(ktmp))
			break;
		else
			ktmp = NULL;
	}
	if (ktmp == NULL) {
		X509error(X509_R_UNABLE_TO_FIND_PARAMETERS_IN_CHAIN);
		return 0;
	}

	/* first, populate the other certs */
	for (j = i - 1; j >= 0; j--) {
		if ((ktmp2 = X509_get0_pubkey(sk_X509_value(chain, j))) == NULL)
			return 0;
		if (!EVP_PKEY_copy_parameters(ktmp2, ktmp))
			return 0;
	}

	if (pkey != NULL)
		if (!EVP_PKEY_copy_parameters(pkey, ktmp))
			return 0;
	return 1;
}
LCRYPTO_ALIAS(X509_get_pubkey_parameters);

int
X509_STORE_CTX_get_ex_new_index(long argl, void *argp, CRYPTO_EX_new *new_func,
    CRYPTO_EX_dup *dup_func, CRYPTO_EX_free *free_func)
{
	/* This function is (usually) called only once, by
	 * SSL_get_ex_data_X509_STORE_CTX_idx (ssl/ssl_cert.c). */
	return CRYPTO_get_ex_new_index(CRYPTO_EX_INDEX_X509_STORE_CTX,
	    argl, argp, new_func, dup_func, free_func);
}
LCRYPTO_ALIAS(X509_STORE_CTX_get_ex_new_index);

int
X509_STORE_CTX_set_ex_data(X509_STORE_CTX *ctx, int idx, void *data)
{
	return CRYPTO_set_ex_data(&ctx->ex_data, idx, data);
}
LCRYPTO_ALIAS(X509_STORE_CTX_set_ex_data);

void *
X509_STORE_CTX_get_ex_data(X509_STORE_CTX *ctx, int idx)
{
	return CRYPTO_get_ex_data(&ctx->ex_data, idx);
}
LCRYPTO_ALIAS(X509_STORE_CTX_get_ex_data);

int
X509_STORE_CTX_get_error(X509_STORE_CTX *ctx)
{
	return ctx->error;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get_error);

void
X509_STORE_CTX_set_error(X509_STORE_CTX *ctx, int err)
{
	ctx->error = err;
}
LCRYPTO_ALIAS(X509_STORE_CTX_set_error);

int
X509_STORE_CTX_get_error_depth(X509_STORE_CTX *ctx)
{
	return ctx->error_depth;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get_error_depth);

void
X509_STORE_CTX_set_error_depth(X509_STORE_CTX *ctx, int depth)
{
	ctx->error_depth = depth;
}
LCRYPTO_ALIAS(X509_STORE_CTX_set_error_depth);

X509 *
X509_STORE_CTX_get_current_cert(X509_STORE_CTX *ctx)
{
	return ctx->current_cert;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get_current_cert);

void
X509_STORE_CTX_set_current_cert(X509_STORE_CTX *ctx, X509 *x)
{
	ctx->current_cert = x;
}
LCRYPTO_ALIAS(X509_STORE_CTX_set_current_cert);

STACK_OF(X509) *
X509_STORE_CTX_get_chain(X509_STORE_CTX *ctx)
{
	return ctx->chain;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get_chain);

STACK_OF(X509) *
X509_STORE_CTX_get0_chain(X509_STORE_CTX *xs)
{
	return xs->chain;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get0_chain);

STACK_OF(X509) *
X509_STORE_CTX_get1_chain(X509_STORE_CTX *ctx)
{
	int i;
	X509 *x;
	STACK_OF(X509) *chain;

	if (!ctx->chain || !(chain = sk_X509_dup(ctx->chain)))
		return NULL;
	for (i = 0; i < sk_X509_num(chain); i++) {
		x = sk_X509_value(chain, i);
		CRYPTO_add(&x->references, 1, CRYPTO_LOCK_X509);
	}
	return chain;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get1_chain);

X509 *
X509_STORE_CTX_get0_current_issuer(X509_STORE_CTX *ctx)
{
	return ctx->current_issuer;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get0_current_issuer);

X509_CRL *
X509_STORE_CTX_get0_current_crl(X509_STORE_CTX *ctx)
{
	return ctx->current_crl;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get0_current_crl);

X509_STORE_CTX *
X509_STORE_CTX_get0_parent_ctx(X509_STORE_CTX *ctx)
{
	return ctx->parent;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get0_parent_ctx);

X509_STORE *
X509_STORE_CTX_get0_store(X509_STORE_CTX *xs)
{
	return xs->store;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get0_store);

void
X509_STORE_CTX_set_cert(X509_STORE_CTX *ctx, X509 *x)
{
	ctx->cert = x;
}
LCRYPTO_ALIAS(X509_STORE_CTX_set_cert);

void
X509_STORE_CTX_set_chain(X509_STORE_CTX *ctx, STACK_OF(X509) *sk)
{
	ctx->untrusted = sk;
}
LCRYPTO_ALIAS(X509_STORE_CTX_set_chain);

void
X509_STORE_CTX_set0_crls(X509_STORE_CTX *ctx, STACK_OF(X509_CRL) *sk)
{
	ctx->crls = sk;
}
LCRYPTO_ALIAS(X509_STORE_CTX_set0_crls);

/*
 * This function is used to set the X509_STORE_CTX purpose and trust
 * values. This is intended to be used when another structure has its
 * own trust and purpose values which (if set) will be inherited by
 * the ctx. If they aren't set then we will usually have a default
 * purpose in mind which should then be used to set the trust value.
 * An example of this is SSL use: an SSL structure will have its own
 * purpose and trust settings which the application can set: if they
 * aren't set then we use the default of SSL client/server.
 */
int
X509_STORE_CTX_set_purpose(X509_STORE_CTX *ctx, int purpose_id)
{
	const X509_PURPOSE *purpose;
	int idx;

	/* XXX - Match wacky/documented behavior. Do we need to keep this? */
	if (purpose_id == 0)
		return 1;

	if (purpose_id < X509_PURPOSE_MIN || purpose_id > X509_PURPOSE_MAX) {
		X509error(X509_R_UNKNOWN_PURPOSE_ID);
		return 0;
	}
	idx = purpose_id - X509_PURPOSE_MIN;
	if ((purpose = X509_PURPOSE_get0(idx)) == NULL) {
		X509error(X509_R_UNKNOWN_PURPOSE_ID);
		return 0;
	}

	/* XXX - Succeeding while ignoring purpose_id and trust is awful. */
	if (ctx->param->purpose == 0)
		ctx->param->purpose = purpose_id;
	if (ctx->param->trust == 0)
		ctx->param->trust = X509_PURPOSE_get_trust(purpose);

	return 1;
}
LCRYPTO_ALIAS(X509_STORE_CTX_set_purpose);

int
X509_STORE_CTX_set_trust(X509_STORE_CTX *ctx, int trust_id)
{
	/* XXX - Match wacky/documented behavior. Do we need to keep this? */
	if (trust_id == 0)
		return 1;

	if (trust_id < X509_TRUST_MIN || trust_id > X509_TRUST_MAX) {
		X509error(X509_R_UNKNOWN_TRUST_ID);
		return 0;
	}

	/* XXX - Succeeding while ignoring the trust_id is awful. */
	if (ctx->param->trust == 0)
		ctx->param->trust = trust_id;

	return 1;
}
LCRYPTO_ALIAS(X509_STORE_CTX_set_trust);

X509_STORE_CTX *
X509_STORE_CTX_new(void)
{
	X509_STORE_CTX *ctx;

	ctx = calloc(1, sizeof(X509_STORE_CTX));
	if (!ctx) {
		X509error(ERR_R_MALLOC_FAILURE);
		return NULL;
	}
	return ctx;
}
LCRYPTO_ALIAS(X509_STORE_CTX_new);

void
X509_STORE_CTX_free(X509_STORE_CTX *ctx)
{
	if (ctx == NULL)
		return;

	X509_STORE_CTX_cleanup(ctx);
	free(ctx);
}
LCRYPTO_ALIAS(X509_STORE_CTX_free);

int
X509_STORE_CTX_init(X509_STORE_CTX *ctx, X509_STORE *store, X509 *leaf,
    STACK_OF(X509) *untrusted)
{
	int param_ret = 1;

	/*
	 * Make sure everything is initialized properly even in case of an
	 * early return due to an error.
	 *
	 * While this 'ctx' can be reused, X509_STORE_CTX_cleanup() will have
	 * freed everything and memset ex_data anyway.  This also allows us
	 * to safely use X509_STORE_CTX variables from the stack which will
	 * have uninitialized data.
	 */
	memset(ctx, 0, sizeof(*ctx));

	/*
	 * Start with this set to not valid - it will be set to valid
	 * in X509_verify_cert.
	 */
	ctx->error = X509_V_ERR_INVALID_CALL;

	/*
	 * Set values other than 0.  Keep this in the same order as
	 * X509_STORE_CTX except for values that may fail.  All fields that
	 * may fail should go last to make sure 'ctx' is as consistent as
	 * possible even on early exits.
	 */
	ctx->store = store;
	ctx->cert = leaf;
	ctx->untrusted = untrusted;

	if (store && store->verify)
		ctx->verify = store->verify;
	else
		ctx->verify = internal_verify;

	if (store && store->verify_cb)
		ctx->verify_cb = store->verify_cb;
	else
		ctx->verify_cb = null_callback;

	ctx->get_issuer = X509_STORE_CTX_get1_issuer;
	ctx->check_issued = check_issued;

	ctx->param = X509_VERIFY_PARAM_new();
	if (!ctx->param) {
		X509error(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	/* Inherit callbacks and flags from X509_STORE if not set
	 * use defaults.
	 */
	if (store)
		param_ret = X509_VERIFY_PARAM_inherit(ctx->param, store->param);
	else
		ctx->param->inh_flags |= X509_VP_FLAG_DEFAULT|X509_VP_FLAG_ONCE;

	if (param_ret)
		param_ret = X509_VERIFY_PARAM_inherit(ctx->param,
		    X509_VERIFY_PARAM_lookup("default"));

	if (param_ret == 0) {
		X509error(ERR_R_MALLOC_FAILURE);
		return 0;
	}

	if (CRYPTO_new_ex_data(CRYPTO_EX_INDEX_X509_STORE_CTX, ctx,
	    &ctx->ex_data) == 0) {
		X509error(ERR_R_MALLOC_FAILURE);
		return 0;
	}
	return 1;
}
LCRYPTO_ALIAS(X509_STORE_CTX_init);

/* Set alternative lookup method: just a STACK of trusted certificates.
 * This avoids X509_STORE nastiness where it isn't needed.
 */

void
X509_STORE_CTX_trusted_stack(X509_STORE_CTX *ctx, STACK_OF(X509) *trusted)
{
	X509_STORE_CTX_set0_trusted_stack(ctx, trusted);
}
LCRYPTO_ALIAS(X509_STORE_CTX_trusted_stack);

void
X509_STORE_CTX_set0_trusted_stack(X509_STORE_CTX *ctx, STACK_OF(X509) *trusted)
{
	ctx->trusted = trusted;
	ctx->get_issuer = x509_vfy_get_trusted_issuer;
}
LCRYPTO_ALIAS(X509_STORE_CTX_set0_trusted_stack);

void
X509_STORE_CTX_cleanup(X509_STORE_CTX *ctx)
{
	if (ctx->param != NULL) {
		if (ctx->parent == NULL)
			X509_VERIFY_PARAM_free(ctx->param);
		ctx->param = NULL;
	}
	if (ctx->chain != NULL) {
		sk_X509_pop_free(ctx->chain, X509_free);
		ctx->chain = NULL;
	}
	CRYPTO_free_ex_data(CRYPTO_EX_INDEX_X509_STORE_CTX, ctx, &ctx->ex_data);
	memset(&ctx->ex_data, 0, sizeof(CRYPTO_EX_DATA));
}
LCRYPTO_ALIAS(X509_STORE_CTX_cleanup);

void
X509_STORE_CTX_set_depth(X509_STORE_CTX *ctx, int depth)
{
	X509_VERIFY_PARAM_set_depth(ctx->param, depth);
}
LCRYPTO_ALIAS(X509_STORE_CTX_set_depth);

void
X509_STORE_CTX_set_flags(X509_STORE_CTX *ctx, unsigned long flags)
{
	X509_VERIFY_PARAM_set_flags(ctx->param, flags);
}
LCRYPTO_ALIAS(X509_STORE_CTX_set_flags);

void
X509_STORE_CTX_set_time(X509_STORE_CTX *ctx, unsigned long flags, time_t t)
{
	X509_VERIFY_PARAM_set_time(ctx->param, t);
}
LCRYPTO_ALIAS(X509_STORE_CTX_set_time);

int
(*X509_STORE_CTX_get_verify_cb(X509_STORE_CTX *ctx))(int, X509_STORE_CTX *)
{
	return ctx->verify_cb;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get_verify_cb);

void
X509_STORE_CTX_set_verify_cb(X509_STORE_CTX *ctx,
    int (*verify_cb)(int, X509_STORE_CTX *))
{
	ctx->verify_cb = verify_cb;
}
LCRYPTO_ALIAS(X509_STORE_CTX_set_verify_cb);

int
(*X509_STORE_CTX_get_verify(X509_STORE_CTX *ctx))(X509_STORE_CTX *)
{
	return ctx->verify;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get_verify);

void
X509_STORE_CTX_set_verify(X509_STORE_CTX *ctx, int (*verify)(X509_STORE_CTX *))
{
	ctx->verify = verify;
}
LCRYPTO_ALIAS(X509_STORE_CTX_set_verify);

X509_STORE_CTX_check_issued_fn
X509_STORE_get_check_issued(X509_STORE *store)
{
	return store->check_issued;
}
LCRYPTO_ALIAS(X509_STORE_get_check_issued);

void
X509_STORE_set_check_issued(X509_STORE *store,
    X509_STORE_CTX_check_issued_fn check_issued)
{
	store->check_issued = check_issued;
}
LCRYPTO_ALIAS(X509_STORE_set_check_issued);

X509_STORE_CTX_check_issued_fn
X509_STORE_CTX_get_check_issued(X509_STORE_CTX *ctx)
{
	return ctx->check_issued;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get_check_issued);

X509 *
X509_STORE_CTX_get0_cert(X509_STORE_CTX *ctx)
{
	return ctx->cert;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get0_cert);

STACK_OF(X509) *
X509_STORE_CTX_get0_untrusted(X509_STORE_CTX *ctx)
{
	return ctx->untrusted;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get0_untrusted);

void
X509_STORE_CTX_set0_untrusted(X509_STORE_CTX *ctx, STACK_OF(X509) *sk)
{
	ctx->untrusted = sk;
}
LCRYPTO_ALIAS(X509_STORE_CTX_set0_untrusted);

void
X509_STORE_CTX_set0_verified_chain(X509_STORE_CTX *ctx, STACK_OF(X509) *sk)
{
	sk_X509_pop_free(ctx->chain, X509_free);
	ctx->chain = sk;
}
LCRYPTO_ALIAS(X509_STORE_CTX_set0_verified_chain);

int
X509_STORE_CTX_get_num_untrusted(X509_STORE_CTX *ctx)
{
	return ctx->num_untrusted;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get_num_untrusted);

int
X509_STORE_CTX_set_default(X509_STORE_CTX *ctx, const char *name)
{
	const X509_VERIFY_PARAM *param;
	param = X509_VERIFY_PARAM_lookup(name);
	if (!param)
		return 0;
	return X509_VERIFY_PARAM_inherit(ctx->param, param);
}
LCRYPTO_ALIAS(X509_STORE_CTX_set_default);

X509_VERIFY_PARAM *
X509_STORE_CTX_get0_param(X509_STORE_CTX *ctx)
{
	return ctx->param;
}
LCRYPTO_ALIAS(X509_STORE_CTX_get0_param);

void
X509_STORE_CTX_set0_param(X509_STORE_CTX *ctx, X509_VERIFY_PARAM *param)
{
	if (ctx->param)
		X509_VERIFY_PARAM_free(ctx->param);
	ctx->param = param;
}
LCRYPTO_ALIAS(X509_STORE_CTX_set0_param);

/*
 * Check if |bits| are adequate for |security level|.
 * Returns 1 if ok, 0 otherwise.
 */
static int
enough_bits_for_security_level(int bits, int level)
{
	/*
	 * Sigh. OpenSSL does this silly squashing, so we will
	 * too. Derp for Derp compatibility being important.
	 */
	if (level < 0)
		level = 0;
	if (level > 5)
		level = 5;

	switch (level) {
	case 0:
		return 1;
	case 1:
		return bits >= 80;
	case 2:
		return bits >= 112;
	case 3:
		return bits >= 128;
	case 4:
		return bits >= 192;
	case 5:
		return bits >= 256;
	default:
		return 0;
	}
}

/*
 * Check whether the public key of |cert| meets the security level of |ctx|.
 *
 * Returns 1 on success, 0 otherwise.
 */
static int
check_key_level(X509_STORE_CTX *ctx, X509 *cert)
{
	EVP_PKEY *pkey;
	int bits;

	/* Unsupported or malformed keys are not secure */
	if ((pkey = X509_get0_pubkey(cert)) == NULL)
		return 0;

	if ((bits = EVP_PKEY_security_bits(pkey)) <= 0)
		return 0;

	return enough_bits_for_security_level(bits, ctx->param->security_level);
}

/*
 * Check whether the signature digest algorithm of |cert| meets the security
 * level of |ctx|.  Do not check trust anchors (self-signed or not).
 *
 * Returns 1 on success, 0 otherwise.
 */
static int
check_sig_level(X509_STORE_CTX *ctx, X509 *cert)
{
	int bits;

	if (!X509_get_signature_info(cert, NULL, NULL, &bits, NULL))
		return 0;

	return enough_bits_for_security_level(bits, ctx->param->security_level);
}

int
x509_vfy_check_security_level(X509_STORE_CTX *ctx)
{
	int num = sk_X509_num(ctx->chain);
	int i;

	if (ctx->param->security_level <= 0)
		return 1;

	for (i = 0; i < num; i++) {
		X509 *cert = sk_X509_value(ctx->chain, i);

		/*
		 * We've already checked the security of the leaf key, so here
		 * we only check the security of issuer keys.
		 */
		if (i > 0) {
			if (!check_key_level(ctx, cert) &&
			    !verify_cb_cert(ctx, cert, i,
			    X509_V_ERR_CA_KEY_TOO_SMALL))
				return 0;
		}

		/*
		 * We also check the signature algorithm security of all certs
		 * except those of the trust anchor at index num - 1.
		 */
		if (i == num - 1)
			break;

		if (!check_sig_level(ctx, cert) &&
		    !verify_cb_cert(ctx, cert, i, X509_V_ERR_CA_MD_TOO_WEAK))
			return 0;
	}
	return 1;
}
