/*	$OpenBSD: tls12_lib.c,v 1.6 2022/11/26 16:08:56 tb Exp $ */
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

#include "ssl_local.h"

static int
tls12_finished_verify_data(SSL *s, const char *finished_label,
    size_t finished_label_len, uint8_t *verify_data, size_t verify_data_len,
    size_t *out_len)
{
	uint8_t transcript_hash[EVP_MAX_MD_SIZE];
	size_t transcript_hash_len;

	*out_len = 0;

	if (s->session->master_key_length == 0)
		return 0;

	if (verify_data_len < TLS1_FINISH_MAC_LENGTH)
		return 0;

	if (!tls1_transcript_hash_value(s, transcript_hash,
	    sizeof(transcript_hash), &transcript_hash_len))
		return 0;

	if (!tls1_PRF(s, s->session->master_key, s->session->master_key_length,
	    finished_label, finished_label_len, transcript_hash,
	    transcript_hash_len, NULL, 0, NULL, 0, NULL, 0, verify_data,
	    TLS1_FINISH_MAC_LENGTH))
		return 0;

	*out_len = TLS1_FINISH_MAC_LENGTH;

	return 1;
}

static int
tls12_client_finished_verify_data(SSL *s, uint8_t *verify_data,
    size_t verify_data_len, size_t *out_len)
{
	return tls12_finished_verify_data(s, TLS_MD_CLIENT_FINISH_CONST,
	    TLS_MD_CLIENT_FINISH_CONST_SIZE, verify_data, verify_data_len,
	    out_len);
}

static int
tls12_server_finished_verify_data(SSL *s, uint8_t *verify_data,
    size_t verify_data_len, size_t *out_len)
{
	return tls12_finished_verify_data(s, TLS_MD_SERVER_FINISH_CONST,
	    TLS_MD_SERVER_FINISH_CONST_SIZE, verify_data, verify_data_len,
	    out_len);
}

int
tls12_derive_finished(SSL *s)
{
	if (!s->server) {
		return tls12_client_finished_verify_data(s,
		    s->s3->hs.finished, sizeof(s->s3->hs.finished),
		    &s->s3->hs.finished_len);
	} else {
		return tls12_server_finished_verify_data(s,
		    s->s3->hs.finished, sizeof(s->s3->hs.finished),
		    &s->s3->hs.finished_len);
	}
}

int
tls12_derive_peer_finished(SSL *s)
{
	if (s->server) {
		return tls12_client_finished_verify_data(s,
		    s->s3->hs.peer_finished, sizeof(s->s3->hs.peer_finished),
		    &s->s3->hs.peer_finished_len);
	} else {
		return tls12_server_finished_verify_data(s,
		    s->s3->hs.peer_finished, sizeof(s->s3->hs.peer_finished),
		    &s->s3->hs.peer_finished_len);
	}
}

int
tls12_derive_master_secret(SSL *s, uint8_t *premaster_secret,
    size_t premaster_secret_len)
{
	s->session->master_key_length = 0;

	if (premaster_secret_len == 0)
		return 0;

	CTASSERT(sizeof(s->session->master_key) == SSL_MAX_MASTER_KEY_LENGTH);

	if (!tls1_PRF(s, premaster_secret, premaster_secret_len,
	    TLS_MD_MASTER_SECRET_CONST, TLS_MD_MASTER_SECRET_CONST_SIZE,
	    s->s3->client_random, SSL3_RANDOM_SIZE, NULL, 0,
	    s->s3->server_random, SSL3_RANDOM_SIZE, NULL, 0,
	    s->session->master_key, sizeof(s->session->master_key)))
		return 0;

	s->session->master_key_length = SSL_MAX_MASTER_KEY_LENGTH;

	return 1;
}
