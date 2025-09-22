/* $OpenBSD: tls13_key_schedule.c,v 1.18 2022/11/26 16:08:56 tb Exp $ */
/*
 * Copyright (c) 2018, Bob Beck <beck@openbsd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <string.h>
#include <stdlib.h>

#include <openssl/hkdf.h>

#include "bytestring.h"
#include "ssl_local.h"
#include "tls13_internal.h"

int
tls13_secret_init(struct tls13_secret *secret, size_t len)
{
	if (secret->data != NULL)
		return 0;

	if ((secret->data = calloc(1, len)) == NULL)
		return 0;
	secret->len = len;

	return 1;
}

void
tls13_secret_cleanup(struct tls13_secret *secret)
{
	freezero(secret->data, secret->len);
	secret->data = NULL;
	secret->len = 0;
}

/*
 * Allocate a set of secrets for a key schedule using
 * a size of hash_length from RFC 8446 section 7.1.
 */
struct tls13_secrets *
tls13_secrets_create(const EVP_MD *digest, int resumption)
{
	struct tls13_secrets *secrets = NULL;
	EVP_MD_CTX *mdctx = NULL;
	unsigned int mdlen;
	size_t hash_length;

	hash_length = EVP_MD_size(digest);

	if ((secrets = calloc(1, sizeof(struct tls13_secrets))) == NULL)
		goto err;

	if (!tls13_secret_init(&secrets->zeros, hash_length))
		goto err;
	if (!tls13_secret_init(&secrets->empty_hash, hash_length))
		goto err;

	if (!tls13_secret_init(&secrets->extracted_early, hash_length))
		goto err;
	if (!tls13_secret_init(&secrets->binder_key, hash_length))
		goto err;
	if (!tls13_secret_init(&secrets->client_early_traffic, hash_length))
		goto err;
	if (!tls13_secret_init(&secrets->early_exporter_master, hash_length))
		goto err;
	if (!tls13_secret_init(&secrets->derived_early, hash_length))
		goto err;
	if (!tls13_secret_init(&secrets->extracted_handshake, hash_length))
		goto err;
	if (!tls13_secret_init(&secrets->client_handshake_traffic, hash_length))
		goto err;
	if (!tls13_secret_init(&secrets->server_handshake_traffic, hash_length))
		goto err;
	if (!tls13_secret_init(&secrets->derived_handshake, hash_length))
		goto err;
	if (!tls13_secret_init(&secrets->extracted_master, hash_length))
		goto err;
	if (!tls13_secret_init(&secrets->client_application_traffic, hash_length))
		goto err;
	if (!tls13_secret_init(&secrets->server_application_traffic, hash_length))
		goto err;
	if (!tls13_secret_init(&secrets->exporter_master, hash_length))
		goto err;
	if (!tls13_secret_init(&secrets->resumption_master, hash_length))
		goto err;

	/*
	 * Calculate the hash of a zero-length string - this is needed during
	 * the "derived" step for key extraction.
	 */
	if ((mdctx = EVP_MD_CTX_new()) == NULL)
		goto err;
	if (!EVP_DigestInit_ex(mdctx, digest, NULL))
		goto err;
	if (!EVP_DigestUpdate(mdctx, secrets->zeros.data, 0))
		goto err;
	if (!EVP_DigestFinal_ex(mdctx, secrets->empty_hash.data, &mdlen))
		goto err;
	EVP_MD_CTX_free(mdctx);
	mdctx = NULL;

	if (secrets->empty_hash.len != mdlen)
		goto err;

	secrets->digest = digest;
	secrets->resumption = resumption;
	secrets->init_done = 1;

	return secrets;

 err:
	tls13_secrets_destroy(secrets);
	EVP_MD_CTX_free(mdctx);

	return NULL;
}

void
tls13_secrets_destroy(struct tls13_secrets *secrets)
{
	if (secrets == NULL)
		return;

	/* you can never be too sure :) */
	tls13_secret_cleanup(&secrets->zeros);
	tls13_secret_cleanup(&secrets->empty_hash);

	tls13_secret_cleanup(&secrets->extracted_early);
	tls13_secret_cleanup(&secrets->binder_key);
	tls13_secret_cleanup(&secrets->client_early_traffic);
	tls13_secret_cleanup(&secrets->early_exporter_master);
	tls13_secret_cleanup(&secrets->derived_early);
	tls13_secret_cleanup(&secrets->extracted_handshake);
	tls13_secret_cleanup(&secrets->client_handshake_traffic);
	tls13_secret_cleanup(&secrets->server_handshake_traffic);
	tls13_secret_cleanup(&secrets->derived_handshake);
	tls13_secret_cleanup(&secrets->extracted_master);
	tls13_secret_cleanup(&secrets->client_application_traffic);
	tls13_secret_cleanup(&secrets->server_application_traffic);
	tls13_secret_cleanup(&secrets->exporter_master);
	tls13_secret_cleanup(&secrets->resumption_master);

	freezero(secrets, sizeof(struct tls13_secrets));
}

int
tls13_hkdf_expand_label(struct tls13_secret *out, const EVP_MD *digest,
    const struct tls13_secret *secret, const char *label,
    const struct tls13_secret *context)
{
	return tls13_hkdf_expand_label_with_length(out, digest, secret, label,
	    strlen(label), context);
}

int
tls13_hkdf_expand_label_with_length(struct tls13_secret *out,
    const EVP_MD *digest, const struct tls13_secret *secret,
    const uint8_t *label, size_t label_len, const struct tls13_secret *context)
{
	const char tls13_plabel[] = "tls13 ";
	uint8_t *hkdf_label = NULL;
	size_t hkdf_label_len;
	CBB cbb, child;
	int ret;

	if (!CBB_init(&cbb, 256))
		goto err;

	if (out->data == NULL || out->len == 0)
		goto err;

	if (!CBB_add_u16(&cbb, out->len))
		goto err;
	if (!CBB_add_u8_length_prefixed(&cbb, &child))
		goto err;
	if (!CBB_add_bytes(&child, tls13_plabel, strlen(tls13_plabel)))
		goto err;
	if (!CBB_add_bytes(&child, label, label_len))
		goto err;
	if (!CBB_add_u8_length_prefixed(&cbb, &child))
		goto err;
	if (!CBB_add_bytes(&child, context->data, context->len))
		goto err;
	if (!CBB_finish(&cbb, &hkdf_label, &hkdf_label_len))
		goto err;

	ret = HKDF_expand(out->data, out->len, digest, secret->data,
	    secret->len, hkdf_label, hkdf_label_len);

	free(hkdf_label);
	return(ret);
 err:
	CBB_cleanup(&cbb);
	return(0);
}

int
tls13_derive_secret(struct tls13_secret *out, const EVP_MD *digest,
    const struct tls13_secret *secret, const char *label,
    const struct tls13_secret *context)
{
	return tls13_hkdf_expand_label(out, digest, secret, label, context);
}

int
tls13_derive_secret_with_label_length(struct tls13_secret *out,
    const EVP_MD *digest, const struct tls13_secret *secret, const uint8_t *label,
    size_t label_len, const struct tls13_secret *context)
{
	return tls13_hkdf_expand_label_with_length(out, digest, secret, label,
	    label_len, context);
}

int
tls13_derive_early_secrets(struct tls13_secrets *secrets,
    uint8_t *psk, size_t psk_len, const struct tls13_secret *context)
{
	if (!secrets->init_done || secrets->early_done)
		return 0;

	if (!HKDF_extract(secrets->extracted_early.data,
	    &secrets->extracted_early.len, secrets->digest, psk, psk_len,
	    secrets->zeros.data, secrets->zeros.len))
		return 0;

	if (secrets->extracted_early.len != secrets->zeros.len)
		return 0;

	if (!tls13_derive_secret(&secrets->binder_key, secrets->digest,
	    &secrets->extracted_early,
	    secrets->resumption ? "res binder" : "ext binder",
	    &secrets->empty_hash))
		return 0;
	if (!tls13_derive_secret(&secrets->client_early_traffic,
	    secrets->digest, &secrets->extracted_early, "c e traffic",
	    context))
		return 0;
	if (!tls13_derive_secret(&secrets->early_exporter_master,
	    secrets->digest, &secrets->extracted_early, "e exp master",
	    context))
		return 0;
	if (!tls13_derive_secret(&secrets->derived_early,
	    secrets->digest, &secrets->extracted_early, "derived",
	    &secrets->empty_hash))
		return 0;

	/* RFC 8446 recommends */
	if (!secrets->insecure)
		explicit_bzero(secrets->extracted_early.data,
		    secrets->extracted_early.len);
	secrets->early_done = 1;
	return 1;
}

int
tls13_derive_handshake_secrets(struct tls13_secrets *secrets,
    const uint8_t *ecdhe, size_t ecdhe_len,
    const struct tls13_secret *context)
{
	if (!secrets->init_done || !secrets->early_done ||
	    secrets->handshake_done)
		return 0;

	if (!HKDF_extract(secrets->extracted_handshake.data,
	    &secrets->extracted_handshake.len, secrets->digest,
	    ecdhe, ecdhe_len, secrets->derived_early.data,
	    secrets->derived_early.len))
		return 0;

	if (secrets->extracted_handshake.len != secrets->zeros.len)
		return 0;

	/* XXX */
	if (!secrets->insecure)
		explicit_bzero(secrets->derived_early.data,
		    secrets->derived_early.len);

	if (!tls13_derive_secret(&secrets->client_handshake_traffic,
	    secrets->digest, &secrets->extracted_handshake, "c hs traffic",
	    context))
		return 0;
	if (!tls13_derive_secret(&secrets->server_handshake_traffic,
	    secrets->digest, &secrets->extracted_handshake, "s hs traffic",
	    context))
		return 0;
	if (!tls13_derive_secret(&secrets->derived_handshake,
	    secrets->digest, &secrets->extracted_handshake, "derived",
	    &secrets->empty_hash))
		return 0;

	/* RFC 8446 recommends */
	if (!secrets->insecure)
		explicit_bzero(secrets->extracted_handshake.data,
		    secrets->extracted_handshake.len);

	secrets->handshake_done = 1;

	return 1;
}

int
tls13_derive_application_secrets(struct tls13_secrets *secrets,
    const struct tls13_secret *context)
{
	if (!secrets->init_done || !secrets->early_done ||
	    !secrets->handshake_done || secrets->schedule_done)
		return 0;

	if (!HKDF_extract(secrets->extracted_master.data,
	    &secrets->extracted_master.len, secrets->digest,
	    secrets->zeros.data, secrets->zeros.len,
	    secrets->derived_handshake.data, secrets->derived_handshake.len))
		return 0;

	if (secrets->extracted_master.len != secrets->zeros.len)
		return 0;

	/* XXX */
	if (!secrets->insecure)
		explicit_bzero(secrets->derived_handshake.data,
		    secrets->derived_handshake.len);

	if (!tls13_derive_secret(&secrets->client_application_traffic,
	    secrets->digest, &secrets->extracted_master, "c ap traffic",
	    context))
		return 0;
	if (!tls13_derive_secret(&secrets->server_application_traffic,
	    secrets->digest, &secrets->extracted_master, "s ap traffic",
	    context))
		return 0;
	if (!tls13_derive_secret(&secrets->exporter_master,
	    secrets->digest, &secrets->extracted_master, "exp master",
	    context))
		return 0;
	if (!tls13_derive_secret(&secrets->resumption_master,
	    secrets->digest, &secrets->extracted_master, "res master",
	    context))
		return 0;

	/* RFC 8446 recommends */
	if (!secrets->insecure)
		explicit_bzero(secrets->extracted_master.data,
		    secrets->extracted_master.len);

	secrets->schedule_done = 1;

	return 1;
}

int
tls13_update_client_traffic_secret(struct tls13_secrets *secrets)
{
	struct tls13_secret context = { .data = "", .len = 0 };

	if (!secrets->init_done || !secrets->early_done ||
	    !secrets->handshake_done || !secrets->schedule_done)
		return 0;

	return tls13_hkdf_expand_label(&secrets->client_application_traffic,
	    secrets->digest, &secrets->client_application_traffic,
	    "traffic upd", &context);
}

int
tls13_update_server_traffic_secret(struct tls13_secrets *secrets)
{
	struct tls13_secret context = { .data = "", .len = 0 };

	if (!secrets->init_done || !secrets->early_done ||
	    !secrets->handshake_done || !secrets->schedule_done)
		return 0;

	return tls13_hkdf_expand_label(&secrets->server_application_traffic,
	    secrets->digest, &secrets->server_application_traffic,
	    "traffic upd", &context);
}

int
tls13_exporter(struct tls13_ctx *ctx, const uint8_t *label, size_t label_len,
    const uint8_t *context_value, size_t context_value_len, uint8_t *out,
    size_t out_len)
{
	struct tls13_secret context, export_out, export_secret;
	struct tls13_secrets *secrets = ctx->hs->tls13.secrets;
	EVP_MD_CTX *md_ctx = NULL;
	unsigned int md_out_len;
	int md_len;
	int ret = 0;

	/*
	 * RFC 8446 Section 7.5.
	 */

	memset(&context, 0, sizeof(context));
	memset(&export_secret, 0, sizeof(export_secret));

	export_out.data = out;
	export_out.len = out_len;

	if (!ctx->handshake_completed)
		return 0;

	md_len = EVP_MD_size(secrets->digest);
	if (md_len <= 0 || md_len > EVP_MAX_MD_SIZE)
		goto err;

	if (!tls13_secret_init(&export_secret, md_len))
		goto err;
	if (!tls13_secret_init(&context, md_len))
		goto err;

	/* In TLSv1.3 no context is equivalent to an empty context. */
	if (context_value == NULL) {
		context_value = "";
		context_value_len = 0;
	}

	if ((md_ctx = EVP_MD_CTX_new()) == NULL)
		goto err;
	if (!EVP_DigestInit_ex(md_ctx, secrets->digest, NULL))
		goto err;
	if (!EVP_DigestUpdate(md_ctx, context_value, context_value_len))
		goto err;
	if (!EVP_DigestFinal_ex(md_ctx, context.data, &md_out_len))
		goto err;
	if (md_len != md_out_len)
		goto err;

	if (!tls13_derive_secret_with_label_length(&export_secret,
	    secrets->digest, &secrets->exporter_master, label, label_len,
	    &secrets->empty_hash))
		goto err;

	if (!tls13_hkdf_expand_label(&export_out, secrets->digest,
	    &export_secret, "exporter", &context))
		goto err;

	ret = 1;

 err:
	EVP_MD_CTX_free(md_ctx);
	tls13_secret_cleanup(&context);
	tls13_secret_cleanup(&export_secret);

	return ret;
}
