/*	$OpenBSD: evp_pkey_cleanup.c,v 1.6 2025/05/21 03:53:20 kenjiro Exp $ */

/*
 * Copyright (c) 2022 Theo Buehler <tb@openbsd.org>
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

#include <stdio.h>

#include <openssl/evp.h>

#include "evp_local.h"

struct pkey_cleanup_test {
	const char *name;
	int nid;
	void (*free)(void);
};

int pkey_ids[] = {
	EVP_PKEY_CMAC,
	EVP_PKEY_DH,
	EVP_PKEY_DSA,
	EVP_PKEY_EC,
	EVP_PKEY_ED25519,
	EVP_PKEY_HMAC,
	EVP_PKEY_RSA,
	EVP_PKEY_RSA_PSS,
	EVP_PKEY_X25519,
	EVP_PKEY_HKDF,
	EVP_PKEY_TLS1_PRF,
};

static const size_t N_PKEY_IDS = sizeof(pkey_ids) / sizeof(pkey_ids[0]);

static int
test_evp_pkey_ctx_cleanup(int nid)
{
	EVP_PKEY_CTX *pkey_ctx = NULL;
	void *data;
	int failed = 1;

	if ((pkey_ctx = EVP_PKEY_CTX_new_id(nid, NULL)) == NULL) {
		fprintf(stderr, "EVP_PKEY_CTX_new_id(%d, NULL) failed\n", nid);
		goto err;
	}

	data = EVP_PKEY_CTX_get_data(pkey_ctx);

	EVP_PKEY_CTX_set_data(pkey_ctx, NULL);
	if (pkey_ctx->pmeth->cleanup != NULL)
		pkey_ctx->pmeth->cleanup(pkey_ctx);

	EVP_PKEY_CTX_set_data(pkey_ctx, data);

	failed = 0;

 err:
	EVP_PKEY_CTX_free(pkey_ctx);

	return failed;
}

int
main(void)
{
	size_t i;
	int failed = 0;

	for (i = 0; i < N_PKEY_IDS; i++)
		failed |= test_evp_pkey_ctx_cleanup(pkey_ids[i]);

	return failed;
}
