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

#define xdr_round_up(x) (round_up((x), sizeof(__be32)))
#define xdr_object_len(x) (4 + xdr_round_up(x))

/*
 * rxgk_app.c
 */
int rxgk_yfs_decode_ticket(struct rxrpc_connection *conn, struct sk_buff *skb,
			   unsigned int ticket_offset, unsigned int ticket_len,
			   struct key **_key);
int rxgk_extract_token(struct rxrpc_connection *conn, struct sk_buff *skb,
		       unsigned int token_offset, unsigned int token_len,
		       struct key **_key);

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

/*
 * Apply decryption and checksumming functions to part of an skbuff.  The
 * offset and length are updated to reflect the actual content of the encrypted
 * region.
 */
static inline
int rxgk_decrypt_skb(const struct krb5_enctype *krb5,
		     struct crypto_aead *aead,
		     struct sk_buff *skb,
		     unsigned int *_offset, unsigned int *_len,
		     int *_error_code)
{
	struct scatterlist sg[16];
	size_t offset = 0, len = *_len;
	int nr_sg, ret;

	sg_init_table(sg, ARRAY_SIZE(sg));
	nr_sg = skb_to_sgvec(skb, sg, *_offset, len);
	if (unlikely(nr_sg < 0))
		return nr_sg;

	ret = crypto_krb5_decrypt(krb5, aead, sg, nr_sg,
				  &offset, &len);
	switch (ret) {
	case 0:
		*_offset += offset;
		*_len = len;
		break;
	case -EBADMSG: /* Checksum mismatch. */
	case -EPROTO:
		*_error_code = RXGK_SEALEDINCON;
		break;
	case -EMSGSIZE:
		*_error_code = RXGK_PACKETSHORT;
		break;
	case -ENOPKG: /* Would prefer RXGK_BADETYPE, but not available for YFS. */
	default:
		*_error_code = RXGK_INCONSISTENCY;
		break;
	}

	return ret;
}

/*
 * Check the MIC on a region of an skbuff.  The offset and length are updated
 * to reflect the actual content of the secure region.
 */
static inline
int rxgk_verify_mic_skb(const struct krb5_enctype *krb5,
			struct crypto_shash *shash,
			const struct krb5_buffer *metadata,
			struct sk_buff *skb,
			unsigned int *_offset, unsigned int *_len,
			u32 *_error_code)
{
	struct scatterlist sg[16];
	size_t offset = 0, len = *_len;
	int nr_sg, ret;

	sg_init_table(sg, ARRAY_SIZE(sg));
	nr_sg = skb_to_sgvec(skb, sg, *_offset, len);
	if (unlikely(nr_sg < 0))
		return nr_sg;

	ret = crypto_krb5_verify_mic(krb5, shash, metadata, sg, nr_sg,
				     &offset, &len);
	switch (ret) {
	case 0:
		*_offset += offset;
		*_len = len;
		break;
	case -EBADMSG: /* Checksum mismatch */
	case -EPROTO:
		*_error_code = RXGK_SEALEDINCON;
		break;
	case -EMSGSIZE:
		*_error_code = RXGK_PACKETSHORT;
		break;
	case -ENOPKG: /* Would prefer RXGK_BADETYPE, but not available for YFS. */
	default:
		*_error_code = RXGK_INCONSISTENCY;
		break;
	}

	return ret;
}
