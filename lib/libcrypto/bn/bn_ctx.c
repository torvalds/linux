/*	$OpenBSD: bn_ctx.c,v 1.23 2025/05/10 05:54:38 tb Exp $ */
/*
 * Copyright (c) 2023 Joel Sing <jsing@openbsd.org>
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

#include <stddef.h>
#include <string.h>

#include <openssl/opensslconf.h>

#include "bn_local.h"
#include "err_local.h"

#define BN_CTX_INITIAL_LEN	8

struct bignum_ctx {
	BIGNUM **bignums;
	uint8_t *groups;
	uint8_t group;
	size_t index;
	size_t len;

	int error;
};

static int
bn_ctx_grow(BN_CTX *bctx)
{
	BIGNUM **bignums = NULL;
	uint8_t *groups = NULL;
	size_t len;

	if ((len = bctx->len) == 0) {
		len = BN_CTX_INITIAL_LEN;
	} else {
		if (SIZE_MAX - len < len)
			return 0;
		len *= 2;
	}

	if ((bignums = recallocarray(bctx->bignums, bctx->len, len,
	    sizeof(bctx->bignums[0]))) == NULL)
		return 0;
	bctx->bignums = bignums;

	if ((groups = reallocarray(bctx->groups, len,
	    sizeof(bctx->groups[0]))) == NULL)
		return 0;
	bctx->groups = groups;

	bctx->len = len;

	return 1;
}

BN_CTX *
BN_CTX_new(void)
{
	return calloc(1, sizeof(struct bignum_ctx));
}
LCRYPTO_ALIAS(BN_CTX_new);

void
BN_CTX_free(BN_CTX *bctx)
{
	size_t i;

	if (bctx == NULL)
		return;

	for (i = 0; i < bctx->len; i++) {
		BN_free(bctx->bignums[i]);
		bctx->bignums[i] = NULL;
	}

	free(bctx->bignums);
	free(bctx->groups);

	freezero(bctx, sizeof(*bctx));
}
LCRYPTO_ALIAS(BN_CTX_free);

void
BN_CTX_start(BN_CTX *bctx)
{
	bctx->group++;

	if (bctx->group == 0) {
		BNerror(BN_R_TOO_MANY_TEMPORARY_VARIABLES);
		bctx->error = 1;
		return;
	}
}
LCRYPTO_ALIAS(BN_CTX_start);

BIGNUM *
BN_CTX_get(BN_CTX *bctx)
{
	BIGNUM *bn = NULL;

	if (bctx->error)
		return NULL;

	if (bctx->group == 0) {
		BNerror(ERR_R_SHOULD_NOT_HAVE_BEEN_CALLED);
		bctx->error = 1;
		return NULL;
	}

	if (bctx->index == bctx->len) {
		if (!bn_ctx_grow(bctx)) {
			BNerror(BN_R_TOO_MANY_TEMPORARY_VARIABLES);
			bctx->error = 1;
			return NULL;
		}
	}

	if ((bn = bctx->bignums[bctx->index]) == NULL) {
		if ((bn = BN_new()) == NULL) {
			BNerror(BN_R_TOO_MANY_TEMPORARY_VARIABLES);
			bctx->error = 1;
			return NULL;
		}
		bctx->bignums[bctx->index] = bn;
	}
	bctx->groups[bctx->index] = bctx->group;
	bctx->index++;

	BN_zero(bn);

	return bn;
}
LCRYPTO_ALIAS(BN_CTX_get);

void
BN_CTX_end(BN_CTX *bctx)
{
	if (bctx == NULL || bctx->error || bctx->group == 0)
		return;

	while (bctx->index > 0 && bctx->groups[bctx->index - 1] == bctx->group) {
		BN_zero(bctx->bignums[bctx->index - 1]);
		bctx->groups[bctx->index - 1] = 0;
		bctx->index--;
	}

	bctx->group--;
}
LCRYPTO_ALIAS(BN_CTX_end);
