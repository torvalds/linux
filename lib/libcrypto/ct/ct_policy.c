/*	$OpenBSD: ct_policy.c,v 1.7 2025/05/10 05:54:38 tb Exp $ */
/*
 * Implementations of Certificate Transparency SCT policies.
 * Written by Rob Percival (robpercival@google.com) for the OpenSSL project.
 */
/* ====================================================================
 * Copyright (c) 2016 The OpenSSL Project.  All rights reserved.
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
 */

#ifdef OPENSSL_NO_CT
# error "CT is disabled"
#endif

#include <time.h>

#include <openssl/ct.h>

#include "ct_local.h"
#include "err_local.h"

/*
 * Number of seconds in the future that an SCT timestamp can be, by default,
 * without being considered invalid. This is added to time() when setting a
 * default value for CT_POLICY_EVAL_CTX.epoch_time_in_ms.
 * It can be overridden by calling CT_POLICY_EVAL_CTX_set_time().
 */
static const time_t SCT_CLOCK_DRIFT_TOLERANCE = 300;

CT_POLICY_EVAL_CTX *
CT_POLICY_EVAL_CTX_new(void)
{
	CT_POLICY_EVAL_CTX *ctx = calloc(1, sizeof(CT_POLICY_EVAL_CTX));

	if (ctx == NULL) {
		CTerror(ERR_R_MALLOC_FAILURE);
		return NULL;
	}

	/* time(NULL) shouldn't ever fail, so don't bother checking for -1. */
	ctx->epoch_time_in_ms = (uint64_t)(time(NULL) + SCT_CLOCK_DRIFT_TOLERANCE) *
            1000;

	return ctx;
}
LCRYPTO_ALIAS(CT_POLICY_EVAL_CTX_new);

void
CT_POLICY_EVAL_CTX_free(CT_POLICY_EVAL_CTX *ctx)
{
	if (ctx == NULL)
		return;
	X509_free(ctx->cert);
	X509_free(ctx->issuer);
	free(ctx);
}
LCRYPTO_ALIAS(CT_POLICY_EVAL_CTX_free);

int
CT_POLICY_EVAL_CTX_set1_cert(CT_POLICY_EVAL_CTX *ctx, X509 *cert)
{
	if (!X509_up_ref(cert))
		return 0;
	ctx->cert = cert;
	return 1;
}
LCRYPTO_ALIAS(CT_POLICY_EVAL_CTX_set1_cert);

int
CT_POLICY_EVAL_CTX_set1_issuer(CT_POLICY_EVAL_CTX *ctx, X509 *issuer)
{
	if (!X509_up_ref(issuer))
		return 0;
	ctx->issuer = issuer;
	return 1;
}
LCRYPTO_ALIAS(CT_POLICY_EVAL_CTX_set1_issuer);

void
CT_POLICY_EVAL_CTX_set_shared_CTLOG_STORE(CT_POLICY_EVAL_CTX *ctx,
    CTLOG_STORE *log_store)
{
	ctx->log_store = log_store;
}
LCRYPTO_ALIAS(CT_POLICY_EVAL_CTX_set_shared_CTLOG_STORE);

void
CT_POLICY_EVAL_CTX_set_time(CT_POLICY_EVAL_CTX *ctx, uint64_t time_in_ms)
{
	ctx->epoch_time_in_ms = time_in_ms;
}
LCRYPTO_ALIAS(CT_POLICY_EVAL_CTX_set_time);

X509 *
CT_POLICY_EVAL_CTX_get0_cert(const CT_POLICY_EVAL_CTX *ctx)
{
	return ctx->cert;
}
LCRYPTO_ALIAS(CT_POLICY_EVAL_CTX_get0_cert);

X509 *
CT_POLICY_EVAL_CTX_get0_issuer(const CT_POLICY_EVAL_CTX *ctx)
{
	return ctx->issuer;
}
LCRYPTO_ALIAS(CT_POLICY_EVAL_CTX_get0_issuer);

const CTLOG_STORE *
CT_POLICY_EVAL_CTX_get0_log_store(const CT_POLICY_EVAL_CTX *ctx)
{
	return ctx->log_store;
}
LCRYPTO_ALIAS(CT_POLICY_EVAL_CTX_get0_log_store);

uint64_t
CT_POLICY_EVAL_CTX_get_time(const CT_POLICY_EVAL_CTX *ctx)
{
	return ctx->epoch_time_in_ms;
}
LCRYPTO_ALIAS(CT_POLICY_EVAL_CTX_get_time);
