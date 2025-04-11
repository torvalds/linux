/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Common bits for GSSAPI-based RxRPC security.
 *
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <crypto/krb5.h>
#include <crypto/skcipher.h>
#include <crypto/hash.h>

/*
 * Per-key number context.  This is replaced when the connection is rekeyed.
 */
struct rxgk_context {
	refcount_t		usage;
	unsigned int		key_number;	/* Rekeying number (goes in the rx header) */
	unsigned long		flags;
#define RXGK_TK_NEEDS_REKEY	0		/* Set if this needs rekeying */
	unsigned long		expiry;		/* Expiration time of this key */
	long long		bytes_remaining; /* Remaining Tx lifetime of this key */
	const struct krb5_enctype *krb5;	/* RxGK encryption type */
	const struct rxgk_key	*key;

	/* We need up to 7 keys derived from the transport key, but we don't
	 * actually need the transport key.  Each key is derived by
	 * DK(TK,constant).
	 */
	struct crypto_aead	*tx_enc;	/* Transmission key */
	struct crypto_aead	*rx_enc;	/* Reception key */
	struct crypto_shash	*tx_Kc;		/* Transmission checksum key */
	struct crypto_shash	*rx_Kc;		/* Reception checksum key */
	struct crypto_aead	*resp_enc;	/* Response packet enc key */
};

/*
 * rxgk_kdf.c
 */
void rxgk_put(struct rxgk_context *gk);
struct rxgk_context *rxgk_generate_transport_key(struct rxrpc_connection *conn,
						 const struct rxgk_key *key,
						 unsigned int key_number,
						 gfp_t gfp);
int rxgk_set_up_token_cipher(const struct krb5_buffer *server_key,
			     struct crypto_aead **token_key,
			     unsigned int enctype,
			     const struct krb5_enctype **_krb5,
			     gfp_t gfp);
