/* $OpenBSD: tls12_key_schedule.c,v 1.4 2024/02/03 15:58:34 beck Exp $ */
/*
 * Copyright (c) 2021 Joel Sing <jsing@openbsd.org>
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

#include <stdlib.h>

#include <openssl/evp.h>

#include "bytestring.h"
#include "ssl_local.h"
#include "tls12_internal.h"

struct tls12_key_block {
	CBS client_write_mac_key;
	CBS server_write_mac_key;
	CBS client_write_key;
	CBS server_write_key;
	CBS client_write_iv;
	CBS server_write_iv;

	uint8_t *key_block;
	size_t key_block_len;
};

struct tls12_key_block *
tls12_key_block_new(void)
{
	return calloc(1, sizeof(struct tls12_key_block));
}

static void
tls12_key_block_clear(struct tls12_key_block *kb)
{
	CBS_init(&kb->client_write_mac_key, NULL, 0);
	CBS_init(&kb->server_write_mac_key, NULL, 0);
	CBS_init(&kb->client_write_key, NULL, 0);
	CBS_init(&kb->server_write_key, NULL, 0);
	CBS_init(&kb->client_write_iv, NULL, 0);
	CBS_init(&kb->server_write_iv, NULL, 0);

	freezero(kb->key_block, kb->key_block_len);
	kb->key_block = NULL;
	kb->key_block_len = 0;
}

void
tls12_key_block_free(struct tls12_key_block *kb)
{
	if (kb == NULL)
		return;

	tls12_key_block_clear(kb);

	freezero(kb, sizeof(struct tls12_key_block));
}

void
tls12_key_block_client_write(struct tls12_key_block *kb, CBS *mac_key,
    CBS *key, CBS *iv)
{
	CBS_dup(&kb->client_write_mac_key, mac_key);
	CBS_dup(&kb->client_write_key, key);
	CBS_dup(&kb->client_write_iv, iv);
}

void
tls12_key_block_server_write(struct tls12_key_block *kb, CBS *mac_key,
    CBS *key, CBS *iv)
{
	CBS_dup(&kb->server_write_mac_key, mac_key);
	CBS_dup(&kb->server_write_key, key);
	CBS_dup(&kb->server_write_iv, iv);
}

int
tls12_key_block_generate(struct tls12_key_block *kb, SSL *s,
    const EVP_AEAD *aead, const EVP_CIPHER *cipher, const EVP_MD *mac_hash)
{
	size_t mac_key_len = 0, key_len = 0, iv_len = 0;
	uint8_t *key_block = NULL;
	size_t key_block_len = 0;
	CBS cbs;

	/*
	 * Generate a TLSv1.2 key block and partition into individual secrets,
	 * as per RFC 5246 section 6.3.
	 */

	tls12_key_block_clear(kb);

	/* Must have AEAD or cipher/MAC pair. */
	if (aead == NULL && (cipher == NULL || mac_hash == NULL))
		goto err;

	if (aead != NULL) {
		key_len = EVP_AEAD_key_length(aead);

		/* AEAD fixed nonce length. */
		if (aead == EVP_aead_aes_128_gcm() ||
		    aead == EVP_aead_aes_256_gcm())
			iv_len = 4;
		else if (aead == EVP_aead_chacha20_poly1305())
			iv_len = 12;
		else
			goto err;
	} else if (cipher != NULL && mac_hash != NULL) {
		/*
		 * A negative integer return value will be detected via the
		 * EVP_MAX_* checks against the size_t variables below.
		 */
		mac_key_len = EVP_MD_size(mac_hash);
		key_len = EVP_CIPHER_key_length(cipher);
		iv_len = EVP_CIPHER_iv_length(cipher);
	}

	if (mac_key_len > EVP_MAX_MD_SIZE)
		goto err;
	if (key_len > EVP_MAX_KEY_LENGTH)
		goto err;
	if (iv_len > EVP_MAX_IV_LENGTH)
		goto err;

	key_block_len = 2 * mac_key_len + 2 * key_len + 2 * iv_len;
	if ((key_block = calloc(1, key_block_len)) == NULL)
		goto err;

	if (!tls1_generate_key_block(s, key_block, key_block_len))
		goto err;

	kb->key_block = key_block;
	kb->key_block_len = key_block_len;
	key_block = NULL;
	key_block_len = 0;

	/* Partition key block into individual secrets. */
	CBS_init(&cbs, kb->key_block, kb->key_block_len);
	if (!CBS_get_bytes(&cbs, &kb->client_write_mac_key, mac_key_len))
		goto err;
	if (!CBS_get_bytes(&cbs, &kb->server_write_mac_key, mac_key_len))
		goto err;
	if (!CBS_get_bytes(&cbs, &kb->client_write_key, key_len))
		goto err;
	if (!CBS_get_bytes(&cbs, &kb->server_write_key, key_len))
		goto err;
	if (!CBS_get_bytes(&cbs, &kb->client_write_iv, iv_len))
		goto err;
	if (!CBS_get_bytes(&cbs, &kb->server_write_iv, iv_len))
		goto err;
	if (CBS_len(&cbs) != 0)
		goto err;

	return 1;

 err:
	tls12_key_block_clear(kb);
	freezero(key_block, key_block_len);

	return 0;
}

struct tls12_reserved_label {
	const char *label;
	size_t label_len;
};

/*
 * RFC 5705 section 6.
 */
static const struct tls12_reserved_label tls12_reserved_labels[] = {
	{
		.label = TLS_MD_CLIENT_FINISH_CONST,
		.label_len = TLS_MD_CLIENT_FINISH_CONST_SIZE,
	},
	{
		.label = TLS_MD_SERVER_FINISH_CONST,
		.label_len = TLS_MD_SERVER_FINISH_CONST_SIZE,
	},
	{
		.label = TLS_MD_MASTER_SECRET_CONST,
		.label_len = TLS_MD_MASTER_SECRET_CONST_SIZE,
	},
	{
		.label = TLS_MD_KEY_EXPANSION_CONST,
		.label_len = TLS_MD_KEY_EXPANSION_CONST_SIZE,
	},
	{
		.label = NULL,
		.label_len = 0,
	},
};

int
tls12_exporter(SSL *s, const uint8_t *label, size_t label_len,
    const uint8_t *context_value, size_t context_value_len, int use_context,
    uint8_t *out, size_t out_len)
{
	uint8_t *data = NULL;
	size_t data_len = 0;
	CBB cbb, context;
	CBS seed;
	size_t i;
	int ret = 0;

	/*
	 * RFC 5705 - Key Material Exporters for TLS.
	 */

	memset(&cbb, 0, sizeof(cbb));

	if (!SSL_is_init_finished(s)) {
		SSLerror(s, SSL_R_BAD_STATE);
		goto err;
	}

	if (s->s3->hs.negotiated_tls_version >= TLS1_3_VERSION)
		goto err;

	/*
	 * Due to exceptional design choices, we need to build a concatenation
	 * of the label and the seed value, before checking for reserved
	 * labels. This prevents a reserved label from being split across the
	 * label and the seed (that includes the client random), which are
	 * concatenated by the PRF.
	 */
	if (!CBB_init(&cbb, 0))
		goto err;
	if (!CBB_add_bytes(&cbb, label, label_len))
		goto err;
	if (!CBB_add_bytes(&cbb, s->s3->client_random, SSL3_RANDOM_SIZE))
		goto err;
	if (!CBB_add_bytes(&cbb, s->s3->server_random, SSL3_RANDOM_SIZE))
		goto err;
	if (use_context) {
		if (!CBB_add_u16_length_prefixed(&cbb, &context))
			goto err;
		if (context_value_len > 0) {
			if (!CBB_add_bytes(&context, context_value,
			    context_value_len))
				goto err;
		}
	}
	if (!CBB_finish(&cbb, &data, &data_len))
		goto err;

	/*
	 * Ensure that the block (label + seed) does not start with a reserved
	 * label - in an ideal world we would ensure that the label has an
	 * explicitly permitted prefix instead, but of course this also got
	 * messed up by the standards.
	 */
	for (i = 0; tls12_reserved_labels[i].label != NULL; i++) {
		/* XXX - consider adding/using CBS_has_prefix(). */
		if (tls12_reserved_labels[i].label_len > data_len)
			goto err;
		if (memcmp(data, tls12_reserved_labels[i].label,
		    tls12_reserved_labels[i].label_len) == 0) {
			SSLerror(s, SSL_R_TLS_ILLEGAL_EXPORTER_LABEL);
			goto err;
		}
	}

	CBS_init(&seed, data, data_len);
	if (!CBS_skip(&seed, label_len))
		goto err;

	if (!tls1_PRF(s, s->session->master_key, s->session->master_key_length,
	    label, label_len, CBS_data(&seed), CBS_len(&seed), NULL, 0, NULL, 0,
	    NULL, 0, out, out_len))
		goto err;

	ret = 1;

 err:
	freezero(data, data_len);
	CBB_cleanup(&cbb);

	return ret;
}
