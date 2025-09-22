/* $OpenBSD: tls12_record_layer.c,v 1.42 2024/02/03 15:58:34 beck Exp $ */
/*
 * Copyright (c) 2020 Joel Sing <jsing@openbsd.org>
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

#include <limits.h>
#include <stdlib.h>

#include <openssl/evp.h>

#include "ssl_local.h"

#define TLS12_RECORD_SEQ_NUM_LEN	8
#define TLS12_AEAD_FIXED_NONCE_MAX_LEN	12

struct tls12_record_protection {
	uint16_t epoch;
	uint8_t seq_num[TLS12_RECORD_SEQ_NUM_LEN];

	EVP_AEAD_CTX *aead_ctx;

	uint8_t *aead_nonce;
	size_t aead_nonce_len;

	uint8_t *aead_fixed_nonce;
	size_t aead_fixed_nonce_len;

	size_t aead_variable_nonce_len;
	size_t aead_tag_len;

	int aead_xor_nonces;
	int aead_variable_nonce_in_record;

	EVP_CIPHER_CTX *cipher_ctx;
	EVP_MD_CTX *hash_ctx;

	int stream_mac;

	uint8_t *mac_key;
	size_t mac_key_len;
};

static struct tls12_record_protection *
tls12_record_protection_new(void)
{
	return calloc(1, sizeof(struct tls12_record_protection));
}

static void
tls12_record_protection_clear(struct tls12_record_protection *rp)
{
	EVP_AEAD_CTX_free(rp->aead_ctx);

	freezero(rp->aead_nonce, rp->aead_nonce_len);
	freezero(rp->aead_fixed_nonce, rp->aead_fixed_nonce_len);

	EVP_CIPHER_CTX_free(rp->cipher_ctx);
	EVP_MD_CTX_free(rp->hash_ctx);

	freezero(rp->mac_key, rp->mac_key_len);

	memset(rp, 0, sizeof(*rp));
}

static void
tls12_record_protection_free(struct tls12_record_protection *rp)
{
	if (rp == NULL)
		return;

	tls12_record_protection_clear(rp);

	freezero(rp, sizeof(struct tls12_record_protection));
}

static int
tls12_record_protection_engaged(struct tls12_record_protection *rp)
{
	return rp->aead_ctx != NULL || rp->cipher_ctx != NULL;
}

static int
tls12_record_protection_unused(struct tls12_record_protection *rp)
{
	return rp->aead_ctx == NULL && rp->cipher_ctx == NULL &&
	    rp->hash_ctx == NULL && rp->mac_key == NULL;
}

static int
tls12_record_protection_eiv_len(struct tls12_record_protection *rp,
    size_t *out_eiv_len)
{
	int eiv_len;

	*out_eiv_len = 0;

	if (rp->cipher_ctx == NULL)
		return 0;

	eiv_len = 0;
	if (EVP_CIPHER_CTX_mode(rp->cipher_ctx) == EVP_CIPH_CBC_MODE)
		eiv_len = EVP_CIPHER_CTX_iv_length(rp->cipher_ctx);
	if (eiv_len < 0 || eiv_len > EVP_MAX_IV_LENGTH)
		return 0;

	*out_eiv_len = eiv_len;

	return 1;
}

static int
tls12_record_protection_block_size(struct tls12_record_protection *rp,
    size_t *out_block_size)
{
	int block_size;

	*out_block_size = 0;

	if (rp->cipher_ctx == NULL)
		return 0;

	block_size = EVP_CIPHER_CTX_block_size(rp->cipher_ctx);
	if (block_size < 0 || block_size > EVP_MAX_BLOCK_LENGTH)
		return 0;

	*out_block_size = block_size;

	return 1;
}

static int
tls12_record_protection_mac_len(struct tls12_record_protection *rp,
    size_t *out_mac_len)
{
	int mac_len;

	*out_mac_len = 0;

	if (rp->hash_ctx == NULL)
		return 0;

	mac_len = EVP_MD_CTX_size(rp->hash_ctx);
	if (mac_len <= 0 || mac_len > EVP_MAX_MD_SIZE)
		return 0;

	*out_mac_len = mac_len;

	return 1;
}

struct tls12_record_layer {
	uint16_t version;
	uint16_t initial_epoch;
	int dtls;

	uint8_t alert_desc;

	const EVP_AEAD *aead;
	const EVP_CIPHER *cipher;
	const EVP_MD *handshake_hash;
	const EVP_MD *mac_hash;

	/* Pointers to active record protection (memory is not owned). */
	struct tls12_record_protection *read;
	struct tls12_record_protection *write;

	struct tls12_record_protection *read_current;
	struct tls12_record_protection *write_current;
	struct tls12_record_protection *write_previous;
};

struct tls12_record_layer *
tls12_record_layer_new(void)
{
	struct tls12_record_layer *rl;

	if ((rl = calloc(1, sizeof(struct tls12_record_layer))) == NULL)
		goto err;
	if ((rl->read_current = tls12_record_protection_new()) == NULL)
		goto err;
	if ((rl->write_current = tls12_record_protection_new()) == NULL)
		goto err;

	rl->read = rl->read_current;
	rl->write = rl->write_current;

	return rl;

 err:
	tls12_record_layer_free(rl);

	return NULL;
}

void
tls12_record_layer_free(struct tls12_record_layer *rl)
{
	if (rl == NULL)
		return;

	tls12_record_protection_free(rl->read_current);
	tls12_record_protection_free(rl->write_current);
	tls12_record_protection_free(rl->write_previous);

	freezero(rl, sizeof(struct tls12_record_layer));
}

void
tls12_record_layer_alert(struct tls12_record_layer *rl, uint8_t *alert_desc)
{
	*alert_desc = rl->alert_desc;
}

int
tls12_record_layer_write_overhead(struct tls12_record_layer *rl,
    size_t *overhead)
{
	size_t block_size, eiv_len, mac_len;

	*overhead = 0;

	if (rl->write->aead_ctx != NULL) {
		*overhead = rl->write->aead_tag_len;
	} else if (rl->write->cipher_ctx != NULL) {
		eiv_len = 0;
		if (rl->version != TLS1_VERSION) {
			if (!tls12_record_protection_eiv_len(rl->write, &eiv_len))
				return 0;
		}
		if (!tls12_record_protection_block_size(rl->write, &block_size))
			return 0;
		if (!tls12_record_protection_mac_len(rl->write, &mac_len))
			return 0;

		*overhead = eiv_len + block_size + mac_len;
	}

	return 1;
}

int
tls12_record_layer_read_protected(struct tls12_record_layer *rl)
{
	return tls12_record_protection_engaged(rl->read);
}

int
tls12_record_layer_write_protected(struct tls12_record_layer *rl)
{
	return tls12_record_protection_engaged(rl->write);
}

void
tls12_record_layer_set_aead(struct tls12_record_layer *rl, const EVP_AEAD *aead)
{
	rl->aead = aead;
}

void
tls12_record_layer_set_cipher_hash(struct tls12_record_layer *rl,
    const EVP_CIPHER *cipher, const EVP_MD *handshake_hash,
    const EVP_MD *mac_hash)
{
	rl->cipher = cipher;
	rl->handshake_hash = handshake_hash;
	rl->mac_hash = mac_hash;
}

void
tls12_record_layer_set_version(struct tls12_record_layer *rl, uint16_t version)
{
	rl->version = version;
	rl->dtls = ((version >> 8) == DTLS1_VERSION_MAJOR);
}

void
tls12_record_layer_set_initial_epoch(struct tls12_record_layer *rl,
    uint16_t epoch)
{
	rl->initial_epoch = epoch;
}

uint16_t
tls12_record_layer_read_epoch(struct tls12_record_layer *rl)
{
	return rl->read->epoch;
}

uint16_t
tls12_record_layer_write_epoch(struct tls12_record_layer *rl)
{
	return rl->write->epoch;
}

int
tls12_record_layer_use_write_epoch(struct tls12_record_layer *rl, uint16_t epoch)
{
	if (rl->write->epoch == epoch)
		return 1;

	if (rl->write_current->epoch == epoch) {
		rl->write = rl->write_current;
		return 1;
	}

	if (rl->write_previous != NULL && rl->write_previous->epoch == epoch) {
		rl->write = rl->write_previous;
		return 1;
	}

	return 0;
}

void
tls12_record_layer_write_epoch_done(struct tls12_record_layer *rl, uint16_t epoch)
{
	if (rl->write_previous == NULL || rl->write_previous->epoch != epoch)
		return;

	rl->write = rl->write_current;

	tls12_record_protection_free(rl->write_previous);
	rl->write_previous = NULL;
}

void
tls12_record_layer_clear_read_state(struct tls12_record_layer *rl)
{
	tls12_record_protection_clear(rl->read);
	rl->read->epoch = rl->initial_epoch;
}

void
tls12_record_layer_clear_write_state(struct tls12_record_layer *rl)
{
	tls12_record_protection_clear(rl->write);
	rl->write->epoch = rl->initial_epoch;

	tls12_record_protection_free(rl->write_previous);
	rl->write_previous = NULL;
}

void
tls12_record_layer_reflect_seq_num(struct tls12_record_layer *rl)
{
	memcpy(rl->write->seq_num, rl->read->seq_num,
	    sizeof(rl->write->seq_num));
}

static const uint8_t tls12_max_seq_num[TLS12_RECORD_SEQ_NUM_LEN] = {
	0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

int
tls12_record_layer_inc_seq_num(struct tls12_record_layer *rl, uint8_t *seq_num)
{
	CBS max_seq_num;
	int i;

	/*
	 * RFC 5246 section 6.1 and RFC 6347 section 4.1 - both TLS and DTLS
	 * sequence numbers must not wrap. Note that for DTLS the first two
	 * bytes are used as an "epoch" and not part of the sequence number.
	 */
	CBS_init(&max_seq_num, seq_num, TLS12_RECORD_SEQ_NUM_LEN);
	if (rl->dtls) {
		if (!CBS_skip(&max_seq_num, 2))
			return 0;
	}
	if (CBS_mem_equal(&max_seq_num, tls12_max_seq_num,
	    CBS_len(&max_seq_num)))
		return 0;

	for (i = TLS12_RECORD_SEQ_NUM_LEN - 1; i >= 0; i--) {
		if (++seq_num[i] != 0)
			break;
	}

	return 1;
}

static int
tls12_record_layer_set_mac_key(struct tls12_record_protection *rp,
    const uint8_t *mac_key, size_t mac_key_len)
{
	freezero(rp->mac_key, rp->mac_key_len);
	rp->mac_key = NULL;
	rp->mac_key_len = 0;

	if (mac_key == NULL || mac_key_len == 0)
		return 1;

	if ((rp->mac_key = calloc(1, mac_key_len)) == NULL)
		return 0;

	memcpy(rp->mac_key, mac_key, mac_key_len);
	rp->mac_key_len = mac_key_len;

	return 1;
}

static int
tls12_record_layer_ccs_aead(struct tls12_record_layer *rl,
    struct tls12_record_protection *rp, int is_write, CBS *mac_key, CBS *key,
    CBS *iv)
{
	if (!tls12_record_protection_unused(rp))
		return 0;

	if ((rp->aead_ctx = EVP_AEAD_CTX_new()) == NULL)
		return 0;

	/* AES GCM cipher suites use variable nonce in record. */
	if (rl->aead == EVP_aead_aes_128_gcm() ||
	    rl->aead == EVP_aead_aes_256_gcm())
		rp->aead_variable_nonce_in_record = 1;

	/* ChaCha20 Poly1305 XORs the fixed and variable nonces. */
	if (rl->aead == EVP_aead_chacha20_poly1305())
		rp->aead_xor_nonces = 1;

	if (!CBS_stow(iv, &rp->aead_fixed_nonce, &rp->aead_fixed_nonce_len))
		return 0;

	rp->aead_nonce = calloc(1, EVP_AEAD_nonce_length(rl->aead));
	if (rp->aead_nonce == NULL)
		return 0;

	rp->aead_nonce_len = EVP_AEAD_nonce_length(rl->aead);
	rp->aead_tag_len = EVP_AEAD_max_overhead(rl->aead);
	rp->aead_variable_nonce_len = TLS12_RECORD_SEQ_NUM_LEN;

	if (rp->aead_xor_nonces) {
		/* Fixed nonce length must match, variable must not exceed. */
		if (rp->aead_fixed_nonce_len != rp->aead_nonce_len)
			return 0;
		if (rp->aead_variable_nonce_len > rp->aead_nonce_len)
			return 0;
	} else {
		/* Concatenated nonce length must equal AEAD nonce length. */
		if (rp->aead_fixed_nonce_len +
		    rp->aead_variable_nonce_len != rp->aead_nonce_len)
			return 0;
	}

	if (!EVP_AEAD_CTX_init(rp->aead_ctx, rl->aead, CBS_data(key),
	    CBS_len(key), EVP_AEAD_DEFAULT_TAG_LENGTH, NULL))
		return 0;

	return 1;
}

static int
tls12_record_layer_ccs_cipher(struct tls12_record_layer *rl,
    struct tls12_record_protection *rp, int is_write, CBS *mac_key, CBS *key,
    CBS *iv)
{
	EVP_PKEY *mac_pkey = NULL;
	int mac_type;
	int ret = 0;

	if (!tls12_record_protection_unused(rp))
		goto err;

	mac_type = EVP_PKEY_HMAC;
	rp->stream_mac = 0;

	if (CBS_len(iv) > INT_MAX || CBS_len(key) > INT_MAX)
		goto err;
	if (EVP_CIPHER_iv_length(rl->cipher) != CBS_len(iv))
		goto err;
	if (EVP_CIPHER_key_length(rl->cipher) != CBS_len(key))
		goto err;
	if (CBS_len(mac_key) > INT_MAX)
		goto err;
	if (EVP_MD_size(rl->mac_hash) != CBS_len(mac_key))
		goto err;
	if ((rp->cipher_ctx = EVP_CIPHER_CTX_new()) == NULL)
		goto err;
	if ((rp->hash_ctx = EVP_MD_CTX_new()) == NULL)
		goto err;

	if (!tls12_record_layer_set_mac_key(rp, CBS_data(mac_key),
	    CBS_len(mac_key)))
		goto err;

	if ((mac_pkey = EVP_PKEY_new_mac_key(mac_type, NULL, CBS_data(mac_key),
	    CBS_len(mac_key))) == NULL)
		goto err;

	if (!EVP_CipherInit_ex(rp->cipher_ctx, rl->cipher, NULL, CBS_data(key),
	    CBS_data(iv), is_write))
		goto err;

	if (EVP_DigestSignInit(rp->hash_ctx, NULL, rl->mac_hash, NULL,
	    mac_pkey) <= 0)
		goto err;

	ret = 1;

 err:
	EVP_PKEY_free(mac_pkey);

	return ret;
}

static int
tls12_record_layer_change_cipher_state(struct tls12_record_layer *rl,
    struct tls12_record_protection *rp, int is_write, CBS *mac_key, CBS *key,
    CBS *iv)
{
	if (rl->aead != NULL)
		return tls12_record_layer_ccs_aead(rl, rp, is_write, mac_key,
		    key, iv);

	return tls12_record_layer_ccs_cipher(rl, rp, is_write, mac_key,
	    key, iv);
}

int
tls12_record_layer_change_read_cipher_state(struct tls12_record_layer *rl,
    CBS *mac_key, CBS *key, CBS *iv)
{
	struct tls12_record_protection *read_new = NULL;
	int ret = 0;

	if ((read_new = tls12_record_protection_new()) == NULL)
		goto err;

	/* Read sequence number gets reset to zero. */

	/* DTLS epoch is incremented and is permitted to wrap. */
	if (rl->dtls)
		read_new->epoch = rl->read_current->epoch + 1;

	if (!tls12_record_layer_change_cipher_state(rl, read_new, 0,
	    mac_key, key, iv))
		goto err;

	tls12_record_protection_free(rl->read_current);
	rl->read = rl->read_current = read_new;
	read_new = NULL;

	ret = 1;

 err:
	tls12_record_protection_free(read_new);

	return ret;
}

int
tls12_record_layer_change_write_cipher_state(struct tls12_record_layer *rl,
    CBS *mac_key, CBS *key, CBS *iv)
{
	struct tls12_record_protection *write_new;
	int ret = 0;

	if ((write_new = tls12_record_protection_new()) == NULL)
		goto err;

	/* Write sequence number gets reset to zero. */

	/* DTLS epoch is incremented and is permitted to wrap. */
	if (rl->dtls)
		write_new->epoch = rl->write_current->epoch + 1;

	if (!tls12_record_layer_change_cipher_state(rl, write_new, 1,
	    mac_key, key, iv))
		goto err;

	if (rl->dtls) {
		tls12_record_protection_free(rl->write_previous);
		rl->write_previous = rl->write_current;
		rl->write_current = NULL;
	}
	tls12_record_protection_free(rl->write_current);
	rl->write = rl->write_current = write_new;
	write_new = NULL;

	ret = 1;

 err:
	tls12_record_protection_free(write_new);

	return ret;
}

static int
tls12_record_layer_build_seq_num(struct tls12_record_layer *rl, CBB *cbb,
    uint16_t epoch, uint8_t *seq_num, size_t seq_num_len)
{
	CBS seq;

	CBS_init(&seq, seq_num, seq_num_len);

	if (rl->dtls) {
		if (!CBB_add_u16(cbb, epoch))
			return 0;
		if (!CBS_skip(&seq, 2))
			return 0;
	}

	return CBB_add_bytes(cbb, CBS_data(&seq), CBS_len(&seq));
}

static int
tls12_record_layer_pseudo_header(struct tls12_record_layer *rl,
    uint8_t content_type, uint16_t record_len, CBS *seq_num, uint8_t **out,
    size_t *out_len)
{
	CBB cbb;

	*out = NULL;
	*out_len = 0;

	/* Build the pseudo-header used for MAC/AEAD. */
	if (!CBB_init(&cbb, 13))
		goto err;

	if (!CBB_add_bytes(&cbb, CBS_data(seq_num), CBS_len(seq_num)))
		goto err;
	if (!CBB_add_u8(&cbb, content_type))
		goto err;
	if (!CBB_add_u16(&cbb, rl->version))
		goto err;
	if (!CBB_add_u16(&cbb, record_len))
		goto err;

	if (!CBB_finish(&cbb, out, out_len))
		goto err;

	return 1;

 err:
	CBB_cleanup(&cbb);

	return 0;
}

static int
tls12_record_layer_mac(struct tls12_record_layer *rl, CBB *cbb,
    EVP_MD_CTX *hash_ctx, int stream_mac, CBS *seq_num, uint8_t content_type,
    const uint8_t *content, size_t content_len, size_t *out_len)
{
	EVP_MD_CTX *mac_ctx = NULL;
	uint8_t *header = NULL;
	size_t header_len = 0;
	size_t mac_len;
	uint8_t *mac;
	int ret = 0;

	if ((mac_ctx = EVP_MD_CTX_new()) == NULL)
		goto err;
	if (!EVP_MD_CTX_copy(mac_ctx, hash_ctx))
		goto err;

	if (!tls12_record_layer_pseudo_header(rl, content_type, content_len,
	    seq_num, &header, &header_len))
		goto err;

	if (EVP_DigestSignUpdate(mac_ctx, header, header_len) <= 0)
		goto err;
	if (EVP_DigestSignUpdate(mac_ctx, content, content_len) <= 0)
		goto err;
	if (EVP_DigestSignFinal(mac_ctx, NULL, &mac_len) <= 0)
		goto err;
	if (!CBB_add_space(cbb, &mac, mac_len))
		goto err;
	if (EVP_DigestSignFinal(mac_ctx, mac, &mac_len) <= 0)
		goto err;
	if (mac_len == 0)
		goto err;

	if (stream_mac) {
		if (!EVP_MD_CTX_copy(hash_ctx, mac_ctx))
			goto err;
	}

	*out_len = mac_len;
	ret = 1;

 err:
	EVP_MD_CTX_free(mac_ctx);
	freezero(header, header_len);

	return ret;
}

static int
tls12_record_layer_read_mac_cbc(struct tls12_record_layer *rl, CBB *cbb,
    uint8_t content_type, CBS *seq_num, const uint8_t *content,
    size_t content_len, size_t mac_len, size_t padding_len)
{
	uint8_t *header = NULL;
	size_t header_len = 0;
	uint8_t *mac = NULL;
	size_t out_mac_len = 0;
	int ret = 0;

	/*
	 * Must be constant time to avoid leaking details about CBC padding.
	 */

	if (!ssl3_cbc_record_digest_supported(rl->read->hash_ctx))
		goto err;

	if (!tls12_record_layer_pseudo_header(rl, content_type, content_len,
	    seq_num, &header, &header_len))
		goto err;

	if (!CBB_add_space(cbb, &mac, mac_len))
		goto err;
	if (!ssl3_cbc_digest_record(rl->read->hash_ctx, mac, &out_mac_len, header,
	    content, content_len + mac_len, content_len + mac_len + padding_len,
	    rl->read->mac_key, rl->read->mac_key_len))
		goto err;
	if (mac_len != out_mac_len)
		goto err;

	ret = 1;

 err:
	freezero(header, header_len);

	return ret;
}

static int
tls12_record_layer_read_mac(struct tls12_record_layer *rl, CBB *cbb,
    uint8_t content_type, CBS *seq_num, const uint8_t *content,
    size_t content_len)
{
	EVP_CIPHER_CTX *enc = rl->read->cipher_ctx;
	size_t out_len;

	if (EVP_CIPHER_CTX_mode(enc) == EVP_CIPH_CBC_MODE)
		return 0;

	return tls12_record_layer_mac(rl, cbb, rl->read->hash_ctx,
	    rl->read->stream_mac, seq_num, content_type, content, content_len,
	    &out_len);
}

static int
tls12_record_layer_write_mac(struct tls12_record_layer *rl, CBB *cbb,
    uint8_t content_type, CBS *seq_num, const uint8_t *content,
    size_t content_len, size_t *out_len)
{
	return tls12_record_layer_mac(rl, cbb, rl->write->hash_ctx,
	    rl->write->stream_mac, seq_num, content_type, content, content_len,
	    out_len);
}

static int
tls12_record_layer_aead_concat_nonce(struct tls12_record_layer *rl,
    struct tls12_record_protection *rp, CBS *seq_num)
{
	CBB cbb;

	if (rp->aead_variable_nonce_len > CBS_len(seq_num))
		return 0;

	/* Fixed nonce and variable nonce (sequence number) are concatenated. */
	if (!CBB_init_fixed(&cbb, rp->aead_nonce, rp->aead_nonce_len))
		goto err;
	if (!CBB_add_bytes(&cbb, rp->aead_fixed_nonce,
	    rp->aead_fixed_nonce_len))
		goto err;
	if (!CBB_add_bytes(&cbb, CBS_data(seq_num),
	    rp->aead_variable_nonce_len))
		goto err;
	if (!CBB_finish(&cbb, NULL, NULL))
		goto err;

	return 1;

 err:
	CBB_cleanup(&cbb);

	return 0;
}

static int
tls12_record_layer_aead_xored_nonce(struct tls12_record_layer *rl,
    struct tls12_record_protection *rp, CBS *seq_num)
{
	uint8_t *pad;
	CBB cbb;
	int i;

	if (rp->aead_variable_nonce_len > CBS_len(seq_num))
		return 0;
	if (rp->aead_fixed_nonce_len < rp->aead_variable_nonce_len)
		return 0;
	if (rp->aead_fixed_nonce_len != rp->aead_nonce_len)
		return 0;

	/*
	 * Variable nonce (sequence number) is right padded, before the fixed
	 * nonce is XOR'd in.
	 */
	if (!CBB_init_fixed(&cbb, rp->aead_nonce, rp->aead_nonce_len))
		goto err;
	if (!CBB_add_space(&cbb, &pad,
	    rp->aead_fixed_nonce_len - rp->aead_variable_nonce_len))
		goto err;
	if (!CBB_add_bytes(&cbb, CBS_data(seq_num),
	    rp->aead_variable_nonce_len))
		goto err;
	if (!CBB_finish(&cbb, NULL, NULL))
		goto err;

	for (i = 0; i < rp->aead_fixed_nonce_len; i++)
		rp->aead_nonce[i] ^= rp->aead_fixed_nonce[i];

	return 1;

 err:
	CBB_cleanup(&cbb);

	return 0;
}

static int
tls12_record_layer_open_record_plaintext(struct tls12_record_layer *rl,
    uint8_t content_type, CBS *fragment, struct tls_content *out)
{
	if (tls12_record_protection_engaged(rl->read))
		return 0;

	return tls_content_dup_data(out, content_type, CBS_data(fragment),
	    CBS_len(fragment));
}

static int
tls12_record_layer_open_record_protected_aead(struct tls12_record_layer *rl,
    uint8_t content_type, CBS *seq_num, CBS *fragment, struct tls_content *out)
{
	struct tls12_record_protection *rp = rl->read;
	uint8_t *header = NULL;
	size_t header_len = 0;
	uint8_t *content = NULL;
	size_t content_len = 0;
	size_t out_len = 0;
	CBS var_nonce;
	int ret = 0;

	if (rp->aead_xor_nonces) {
		if (!tls12_record_layer_aead_xored_nonce(rl, rp, seq_num))
			goto err;
	} else if (rp->aead_variable_nonce_in_record) {
		if (!CBS_get_bytes(fragment, &var_nonce,
		    rp->aead_variable_nonce_len))
			goto err;
		if (!tls12_record_layer_aead_concat_nonce(rl, rp, &var_nonce))
			goto err;
	} else {
		if (!tls12_record_layer_aead_concat_nonce(rl, rp, seq_num))
			goto err;
	}

	/* XXX EVP_AEAD_max_tag_len vs EVP_AEAD_CTX_tag_len. */
	if (CBS_len(fragment) < rp->aead_tag_len) {
		rl->alert_desc = SSL_AD_BAD_RECORD_MAC;
		goto err;
	}
	if (CBS_len(fragment) > SSL3_RT_MAX_ENCRYPTED_LENGTH) {
		rl->alert_desc = SSL_AD_RECORD_OVERFLOW;
		goto err;
	}

	content_len = CBS_len(fragment) - rp->aead_tag_len;
	if ((content = calloc(1, CBS_len(fragment))) == NULL) {
		content_len = 0;
		goto err;
	}

	if (!tls12_record_layer_pseudo_header(rl, content_type, content_len,
	    seq_num, &header, &header_len))
		goto err;

	if (!EVP_AEAD_CTX_open(rp->aead_ctx, content, &out_len, content_len,
	    rp->aead_nonce, rp->aead_nonce_len, CBS_data(fragment),
	    CBS_len(fragment), header, header_len)) {
		rl->alert_desc = SSL_AD_BAD_RECORD_MAC;
		goto err;
	}

	if (out_len > SSL3_RT_MAX_PLAIN_LENGTH) {
		rl->alert_desc = SSL_AD_RECORD_OVERFLOW;
		goto err;
	}

	if (out_len != content_len)
		goto err;

	tls_content_set_data(out, content_type, content, content_len);
	content = NULL;
	content_len = 0;

	ret = 1;

 err:
	freezero(header, header_len);
	freezero(content, content_len);

	return ret;
}

static int
tls12_record_layer_open_record_protected_cipher(struct tls12_record_layer *rl,
    uint8_t content_type, CBS *seq_num, CBS *fragment, struct tls_content *out)
{
	EVP_CIPHER_CTX *enc = rl->read->cipher_ctx;
	SSL3_RECORD_INTERNAL rrec;
	size_t block_size, eiv_len;
	uint8_t *mac = NULL;
	size_t mac_len = 0;
	uint8_t *out_mac = NULL;
	size_t out_mac_len = 0;
	uint8_t *content = NULL;
	size_t content_len = 0;
	size_t min_len;
	CBB cbb_mac;
	int ret = 0;

	memset(&cbb_mac, 0, sizeof(cbb_mac));
	memset(&rrec, 0, sizeof(rrec));

	if (!tls12_record_protection_block_size(rl->read, &block_size))
		goto err;

	/* Determine explicit IV length. */
	eiv_len = 0;
	if (rl->version != TLS1_VERSION) {
		if (!tls12_record_protection_eiv_len(rl->read, &eiv_len))
			goto err;
	}

	mac_len = 0;
	if (rl->read->hash_ctx != NULL) {
		if (!tls12_record_protection_mac_len(rl->read, &mac_len))
			goto err;
	}

	/* CBC has at least one padding byte. */
	min_len = eiv_len + mac_len;
	if (EVP_CIPHER_CTX_mode(enc) == EVP_CIPH_CBC_MODE)
		min_len += 1;

	if (CBS_len(fragment) < min_len) {
		rl->alert_desc = SSL_AD_BAD_RECORD_MAC;
		goto err;
	}
	if (CBS_len(fragment) > SSL3_RT_MAX_ENCRYPTED_LENGTH) {
		rl->alert_desc = SSL_AD_RECORD_OVERFLOW;
		goto err;
	}
	if (CBS_len(fragment) % block_size != 0) {
		rl->alert_desc = SSL_AD_BAD_RECORD_MAC;
		goto err;
	}

	if ((content = calloc(1, CBS_len(fragment))) == NULL)
		goto err;
	content_len = CBS_len(fragment);

	if (!EVP_Cipher(enc, content, CBS_data(fragment), CBS_len(fragment)))
		goto err;

	rrec.data = content;
	rrec.input = content;
	rrec.length = content_len;

	/*
	 * We now have to remove padding, extract MAC, calculate MAC
	 * and compare MAC in constant time.
	 */
	if (block_size > 1)
		ssl3_cbc_remove_padding(&rrec, eiv_len, mac_len);

	if ((mac = calloc(1, mac_len)) == NULL)
		goto err;

	if (!CBB_init(&cbb_mac, EVP_MAX_MD_SIZE))
		goto err;
	if (EVP_CIPHER_CTX_mode(enc) == EVP_CIPH_CBC_MODE) {
		ssl3_cbc_copy_mac(mac, &rrec, mac_len, rrec.length +
		    rrec.padding_length);
		rrec.length -= mac_len;
		if (!tls12_record_layer_read_mac_cbc(rl, &cbb_mac, content_type,
		    seq_num, rrec.input, rrec.length, mac_len,
		    rrec.padding_length))
			goto err;
	} else {
		rrec.length -= mac_len;
		memcpy(mac, rrec.data + rrec.length, mac_len);
		if (!tls12_record_layer_read_mac(rl, &cbb_mac, content_type,
		    seq_num, rrec.input, rrec.length))
			goto err;
	}
	if (!CBB_finish(&cbb_mac, &out_mac, &out_mac_len))
		goto err;
	if (mac_len != out_mac_len)
		goto err;

	if (timingsafe_memcmp(mac, out_mac, mac_len) != 0) {
		rl->alert_desc = SSL_AD_BAD_RECORD_MAC;
		goto err;
	}

	if (rrec.length > SSL3_RT_MAX_COMPRESSED_LENGTH + mac_len) {
		rl->alert_desc = SSL_AD_BAD_RECORD_MAC;
		goto err;
	}
	if (rrec.length > SSL3_RT_MAX_PLAIN_LENGTH) {
		rl->alert_desc = SSL_AD_RECORD_OVERFLOW;
		goto err;
	}

	tls_content_set_data(out, content_type, content, content_len);
	content = NULL;
	content_len = 0;

	/* Actual content is after EIV, minus padding and MAC. */
	if (!tls_content_set_bounds(out, eiv_len, rrec.length))
		goto err;

	ret = 1;

 err:
	CBB_cleanup(&cbb_mac);
	freezero(mac, mac_len);
	freezero(out_mac, out_mac_len);
	freezero(content, content_len);

	return ret;
}

int
tls12_record_layer_open_record(struct tls12_record_layer *rl, uint8_t *buf,
    size_t buf_len, struct tls_content *out)
{
	CBS cbs, fragment, seq_num;
	uint16_t version;
	uint8_t content_type;

	CBS_init(&cbs, buf, buf_len);
	CBS_init(&seq_num, rl->read->seq_num, sizeof(rl->read->seq_num));

	if (!CBS_get_u8(&cbs, &content_type))
		return 0;
	if (!CBS_get_u16(&cbs, &version))
		return 0;
	if (rl->dtls) {
		/*
		 * The DTLS sequence number is split into a 16 bit epoch and
		 * 48 bit sequence number, however for the purposes of record
		 * processing it is treated the same as a TLS 64 bit sequence
		 * number. DTLS also uses explicit read sequence numbers, which
		 * we need to extract from the DTLS record header.
		 */
		if (!CBS_get_bytes(&cbs, &seq_num, SSL3_SEQUENCE_SIZE))
			return 0;
		if (!CBS_write_bytes(&seq_num, rl->read->seq_num,
		    sizeof(rl->read->seq_num), NULL))
			return 0;
	}
	if (!CBS_get_u16_length_prefixed(&cbs, &fragment))
		return 0;

	if (rl->read->aead_ctx != NULL) {
		if (!tls12_record_layer_open_record_protected_aead(rl,
		    content_type, &seq_num, &fragment, out))
			return 0;
	} else if (rl->read->cipher_ctx != NULL) {
		if (!tls12_record_layer_open_record_protected_cipher(rl,
		    content_type, &seq_num, &fragment, out))
			return 0;
	} else {
		if (!tls12_record_layer_open_record_plaintext(rl,
		    content_type, &fragment, out))
			return 0;
	}

	if (!rl->dtls) {
		if (!tls12_record_layer_inc_seq_num(rl, rl->read->seq_num))
			return 0;
	}

	return 1;
}

static int
tls12_record_layer_seal_record_plaintext(struct tls12_record_layer *rl,
    uint8_t content_type, const uint8_t *content, size_t content_len, CBB *out)
{
	if (tls12_record_protection_engaged(rl->write))
		return 0;

	return CBB_add_bytes(out, content, content_len);
}

static int
tls12_record_layer_seal_record_protected_aead(struct tls12_record_layer *rl,
    uint8_t content_type, CBS *seq_num, const uint8_t *content,
    size_t content_len, CBB *out)
{
	struct tls12_record_protection *rp = rl->write;
	uint8_t *header = NULL;
	size_t header_len = 0;
	size_t enc_record_len, out_len;
	uint8_t *enc_data;
	int ret = 0;

	if (rp->aead_xor_nonces) {
		if (!tls12_record_layer_aead_xored_nonce(rl, rp, seq_num))
			goto err;
	} else {
		if (!tls12_record_layer_aead_concat_nonce(rl, rp, seq_num))
			goto err;
	}

	if (rp->aead_variable_nonce_in_record) {
		if (rp->aead_variable_nonce_len > CBS_len(seq_num))
			goto err;
		if (!CBB_add_bytes(out, CBS_data(seq_num),
		    rp->aead_variable_nonce_len))
			goto err;
	}

	if (!tls12_record_layer_pseudo_header(rl, content_type, content_len,
	    seq_num, &header, &header_len))
		goto err;

	/* XXX EVP_AEAD_max_tag_len vs EVP_AEAD_CTX_tag_len. */
	enc_record_len = content_len + rp->aead_tag_len;
	if (enc_record_len > SSL3_RT_MAX_ENCRYPTED_LENGTH)
		goto err;
	if (!CBB_add_space(out, &enc_data, enc_record_len))
		goto err;

	if (!EVP_AEAD_CTX_seal(rp->aead_ctx, enc_data, &out_len, enc_record_len,
	    rp->aead_nonce, rp->aead_nonce_len, content, content_len, header,
	    header_len))
		goto err;

	if (out_len != enc_record_len)
		goto err;

	ret = 1;

 err:
	freezero(header, header_len);

	return ret;
}

static int
tls12_record_layer_seal_record_protected_cipher(struct tls12_record_layer *rl,
    uint8_t content_type, CBS *seq_num, const uint8_t *content,
    size_t content_len, CBB *out)
{
	EVP_CIPHER_CTX *enc = rl->write->cipher_ctx;
	size_t block_size, eiv_len, mac_len, pad_len;
	uint8_t *enc_data, *eiv, *pad, pad_val;
	uint8_t *plain = NULL;
	size_t plain_len = 0;
	int ret = 0;
	CBB cbb;

	if (!CBB_init(&cbb, SSL3_RT_MAX_PLAIN_LENGTH))
		goto err;

	/* Add explicit IV if necessary. */
	eiv_len = 0;
	if (rl->version != TLS1_VERSION) {
		if (!tls12_record_protection_eiv_len(rl->write, &eiv_len))
			goto err;
	}
	if (eiv_len > 0) {
		if (!CBB_add_space(&cbb, &eiv, eiv_len))
			goto err;
		arc4random_buf(eiv, eiv_len);
	}

	if (!CBB_add_bytes(&cbb, content, content_len))
		goto err;

	mac_len = 0;
	if (rl->write->hash_ctx != NULL) {
		if (!tls12_record_layer_write_mac(rl, &cbb, content_type,
		    seq_num, content, content_len, &mac_len))
			goto err;
	}

	plain_len = eiv_len + content_len + mac_len;

	/* Add padding to block size, if necessary. */
	if (!tls12_record_protection_block_size(rl->write, &block_size))
		goto err;
	if (block_size > 1) {
		pad_len = block_size - (plain_len % block_size);
		pad_val = pad_len - 1;

		if (pad_len > 255)
			goto err;
		if (!CBB_add_space(&cbb, &pad, pad_len))
			goto err;
		memset(pad, pad_val, pad_len);
	}

	if (!CBB_finish(&cbb, &plain, &plain_len))
		goto err;

	if (plain_len % block_size != 0)
		goto err;
	if (plain_len > SSL3_RT_MAX_ENCRYPTED_LENGTH)
		goto err;

	if (!CBB_add_space(out, &enc_data, plain_len))
		goto err;
	if (!EVP_Cipher(enc, enc_data, plain, plain_len))
		goto err;

	ret = 1;

 err:
	CBB_cleanup(&cbb);
	freezero(plain, plain_len);

	return ret;
}

int
tls12_record_layer_seal_record(struct tls12_record_layer *rl,
    uint8_t content_type, const uint8_t *content, size_t content_len, CBB *cbb)
{
	uint8_t *seq_num_data = NULL;
	size_t seq_num_len = 0;
	CBB fragment, seq_num_cbb;
	CBS seq_num;
	int ret = 0;

	/*
	 * Construct the effective sequence number - this is used in both
	 * the DTLS header and for MAC calculations.
	 */
	if (!CBB_init(&seq_num_cbb, SSL3_SEQUENCE_SIZE))
		goto err;
	if (!tls12_record_layer_build_seq_num(rl, &seq_num_cbb, rl->write->epoch,
	    rl->write->seq_num, sizeof(rl->write->seq_num)))
		goto err;
	if (!CBB_finish(&seq_num_cbb, &seq_num_data, &seq_num_len))
		goto err;
	CBS_init(&seq_num, seq_num_data, seq_num_len);

	if (!CBB_add_u8(cbb, content_type))
		goto err;
	if (!CBB_add_u16(cbb, rl->version))
		goto err;
	if (rl->dtls) {
		if (!CBB_add_bytes(cbb, CBS_data(&seq_num), CBS_len(&seq_num)))
			goto err;
	}
	if (!CBB_add_u16_length_prefixed(cbb, &fragment))
		goto err;

	if (rl->write->aead_ctx != NULL) {
		if (!tls12_record_layer_seal_record_protected_aead(rl,
		    content_type, &seq_num, content, content_len, &fragment))
			goto err;
	} else if (rl->write->cipher_ctx != NULL) {
		if (!tls12_record_layer_seal_record_protected_cipher(rl,
		    content_type, &seq_num, content, content_len, &fragment))
			goto err;
	} else {
		if (!tls12_record_layer_seal_record_plaintext(rl,
		    content_type, content, content_len, &fragment))
			goto err;
	}

	if (!CBB_flush(cbb))
		goto err;

	if (!tls12_record_layer_inc_seq_num(rl, rl->write->seq_num))
		goto err;

	ret = 1;

 err:
	CBB_cleanup(&seq_num_cbb);
	free(seq_num_data);

	return ret;
}
