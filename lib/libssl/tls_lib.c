/* $OpenBSD: tls_lib.c,v 1.3 2022/11/26 16:08:56 tb Exp $ */
/*
 * Copyright (c) 2019, 2021 Joel Sing <jsing@openbsd.org>
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

int
tls_process_peer_certs(SSL *s, STACK_OF(X509) *peer_certs)
{
	STACK_OF(X509) *peer_certs_no_leaf;
	X509 *peer_cert = NULL;
	EVP_PKEY *pkey;
	int cert_type;
	int ret = 0;

	if (sk_X509_num(peer_certs) < 1)
		goto err;
	peer_cert = sk_X509_value(peer_certs, 0);
	X509_up_ref(peer_cert);

	if ((pkey = X509_get0_pubkey(peer_cert)) == NULL) {
		SSLerror(s, SSL_R_NO_PUBLICKEY);
		goto err;
	}
	if (EVP_PKEY_missing_parameters(pkey)) {
		SSLerror(s, SSL_R_UNABLE_TO_FIND_PUBLIC_KEY_PARAMETERS);
		goto err;
	}
	if ((cert_type = ssl_cert_type(pkey)) < 0) {
		SSLerror(s, SSL_R_UNKNOWN_CERTIFICATE_TYPE);
		goto err;
	}

	s->session->peer_cert_type = cert_type;

	X509_free(s->session->peer_cert);
	s->session->peer_cert = peer_cert;
	peer_cert = NULL;

	sk_X509_pop_free(s->s3->hs.peer_certs, X509_free);
	if ((s->s3->hs.peer_certs = X509_chain_up_ref(peer_certs)) == NULL)
		goto err;

	if ((peer_certs_no_leaf = X509_chain_up_ref(peer_certs)) == NULL)
		goto err;
	X509_free(sk_X509_shift(peer_certs_no_leaf));
	sk_X509_pop_free(s->s3->hs.peer_certs_no_leaf, X509_free);
	s->s3->hs.peer_certs_no_leaf = peer_certs_no_leaf;

	ret = 1;
 err:
	X509_free(peer_cert);

	return ret;
}
