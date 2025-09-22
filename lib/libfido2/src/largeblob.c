/*
 * Copyright (c) 2020 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <openssl/sha.h>

#include "fido.h"
#include "fido/es256.h"

#define LARGEBLOB_DIGEST_LENGTH	16
#define LARGEBLOB_NONCE_LENGTH	12
#define LARGEBLOB_TAG_LENGTH	16

typedef struct largeblob {
	size_t origsiz;
	fido_blob_t ciphertext;
	fido_blob_t nonce;
} largeblob_t;

static largeblob_t *
largeblob_new(void)
{
	return calloc(1, sizeof(largeblob_t));
}

static void
largeblob_reset(largeblob_t *blob)
{
	fido_blob_reset(&blob->ciphertext);
	fido_blob_reset(&blob->nonce);
	blob->origsiz = 0;
}

static void
largeblob_free(largeblob_t **blob_ptr)
{
	largeblob_t *blob;

	if (blob_ptr == NULL || (blob = *blob_ptr) == NULL)
		return;
	largeblob_reset(blob);
	free(blob);
	*blob_ptr = NULL;
}

static int
largeblob_aad(fido_blob_t *aad, uint64_t size)
{
	uint8_t buf[4 + sizeof(uint64_t)];

	buf[0] = 0x62; /* b */
	buf[1] = 0x6c; /* l */
	buf[2] = 0x6f; /* o */
	buf[3] = 0x62; /* b */
	size = htole64(size);
	memcpy(&buf[4], &size, sizeof(uint64_t));

	return fido_blob_set(aad, buf, sizeof(buf));
}

static fido_blob_t *
largeblob_decrypt(const largeblob_t *blob, const fido_blob_t *key)
{
	fido_blob_t *plaintext = NULL, *aad = NULL;
	int ok = -1;

	if ((plaintext = fido_blob_new()) == NULL ||
	    (aad = fido_blob_new()) == NULL) {
		fido_log_debug("%s: fido_blob_new", __func__);
		goto fail;
	}
	if (largeblob_aad(aad, blob->origsiz) < 0) {
		fido_log_debug("%s: largeblob_aad", __func__);
		goto fail;
	}
	if (aes256_gcm_dec(key, &blob->nonce, aad, &blob->ciphertext,
	    plaintext) < 0) {
		fido_log_debug("%s: aes256_gcm_dec", __func__);
		goto fail;
	}

	ok = 0;
fail:
	fido_blob_free(&aad);

	if (ok < 0)
		fido_blob_free(&plaintext);

	return plaintext;
}

static int
largeblob_get_nonce(largeblob_t *blob)
{
	uint8_t buf[LARGEBLOB_NONCE_LENGTH];
	int ok = -1;

	if (fido_get_random(buf, sizeof(buf)) < 0) {
		fido_log_debug("%s: fido_get_random", __func__);
		goto fail;
	}
	if (fido_blob_set(&blob->nonce, buf, sizeof(buf)) < 0) {
		fido_log_debug("%s: fido_blob_set", __func__);
		goto fail;
	}

	ok = 0;
fail:
	explicit_bzero(buf, sizeof(buf));

	return ok;
}

static int
largeblob_seal(largeblob_t *blob, const fido_blob_t *body,
    const fido_blob_t *key)
{
	fido_blob_t *plaintext = NULL, *aad = NULL;
	int ok = -1;

	if ((plaintext = fido_blob_new()) == NULL ||
	    (aad = fido_blob_new()) == NULL) {
		fido_log_debug("%s: fido_blob_new", __func__);
		goto fail;
	}
	if (fido_compress(plaintext, body) != FIDO_OK) {
		fido_log_debug("%s: fido_compress", __func__);
		goto fail;
	}
	if (largeblob_aad(aad, body->len) < 0) {
		fido_log_debug("%s: largeblob_aad", __func__);
		goto fail;
	}
	if (largeblob_get_nonce(blob) < 0) {
		fido_log_debug("%s: largeblob_get_nonce", __func__);
		goto fail;
	}
	if (aes256_gcm_enc(key, &blob->nonce, aad, plaintext,
	    &blob->ciphertext) < 0) {
		fido_log_debug("%s: aes256_gcm_enc", __func__);
		goto fail;
	}
	blob->origsiz = body->len;

	ok = 0;
fail:
	fido_blob_free(&plaintext);
	fido_blob_free(&aad);

	return ok;
}

static int
largeblob_get_tx(fido_dev_t *dev, size_t offset, size_t count, int *ms)
{
	fido_blob_t f;
	cbor_item_t *argv[3];
	int r;

	memset(argv, 0, sizeof(argv));
	memset(&f, 0, sizeof(f));

	if ((argv[0] = cbor_build_uint(count)) == NULL ||
	    (argv[2] = cbor_build_uint(offset)) == NULL) {
		fido_log_debug("%s: cbor encode", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}
	if (cbor_build_frame(CTAP_CBOR_LARGEBLOB, argv, nitems(argv), &f) < 0 ||
	    fido_tx(dev, CTAP_CMD_CBOR, f.ptr, f.len, ms) < 0) {
		fido_log_debug("%s: fido_tx", __func__);
		r = FIDO_ERR_TX;
		goto fail;
	}

	r = FIDO_OK;
fail:
	cbor_vector_free(argv, nitems(argv));
	free(f.ptr);

	return r;
}

static int
parse_largeblob_reply(const cbor_item_t *key, const cbor_item_t *val,
    void *arg)
{
	if (cbor_isa_uint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8 ||
	    cbor_get_uint8(key) != 1) {
		fido_log_debug("%s: cbor type", __func__);
		return 0; /* ignore */
	}

	return fido_blob_decode(val, arg);
}

static int
largeblob_get_rx(fido_dev_t *dev, fido_blob_t **chunk, int *ms)
{
	unsigned char reply[FIDO_MAXMSG];
	int reply_len, r;

	*chunk = NULL;
	if ((reply_len = fido_rx(dev, CTAP_CMD_CBOR, &reply, sizeof(reply),
	    ms)) < 0) {
		fido_log_debug("%s: fido_rx", __func__);
		return FIDO_ERR_RX;
	}
	if ((*chunk = fido_blob_new()) == NULL) {
		fido_log_debug("%s: fido_blob_new", __func__);
		return FIDO_ERR_INTERNAL;
	}
	if ((r = cbor_parse_reply(reply, (size_t)reply_len, *chunk,
	    parse_largeblob_reply)) != FIDO_OK) {
		fido_log_debug("%s: parse_largeblob_reply", __func__);
		fido_blob_free(chunk);
		return r;
	}

	return FIDO_OK;
}

static cbor_item_t *
largeblob_array_load(const uint8_t *ptr, size_t len)
{
	struct cbor_load_result cbor;
	cbor_item_t *item;

	if (len < LARGEBLOB_DIGEST_LENGTH) {
		fido_log_debug("%s: len", __func__);
		return NULL;
	}
	len -= LARGEBLOB_DIGEST_LENGTH;
	if ((item = cbor_load(ptr, len, &cbor)) == NULL) {
		fido_log_debug("%s: cbor_load", __func__);
		return NULL;
	}
	if (!cbor_isa_array(item) || !cbor_array_is_definite(item)) {
		fido_log_debug("%s: cbor type", __func__);
		cbor_decref(&item);
		return NULL;
	}

	return item;
}

static size_t
get_chunklen(fido_dev_t *dev)
{
	uint64_t maxchunklen;

	if ((maxchunklen = fido_dev_maxmsgsize(dev)) > SIZE_MAX)
		maxchunklen = SIZE_MAX;
	if (maxchunklen > FIDO_MAXMSG)
		maxchunklen = FIDO_MAXMSG;
	maxchunklen = maxchunklen > 64 ? maxchunklen - 64 : 0;

	return (size_t)maxchunklen;
}

static int
largeblob_do_decode(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	largeblob_t *blob = arg;
	uint64_t origsiz;

	if (cbor_isa_uint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8) {
		fido_log_debug("%s: cbor type", __func__);
		return 0; /* ignore */
	}

	switch (cbor_get_uint8(key)) {
	case 1: /* ciphertext */
		if (fido_blob_decode(val, &blob->ciphertext) < 0 ||
		    blob->ciphertext.len < LARGEBLOB_TAG_LENGTH)
			return -1;
		return 0;
	case 2: /* nonce */
		if (fido_blob_decode(val, &blob->nonce) < 0 ||
		    blob->nonce.len != LARGEBLOB_NONCE_LENGTH)
			return -1;
		return 0;
	case 3: /* origSize */
		if (!cbor_isa_uint(val) ||
		    (origsiz = cbor_get_int(val)) > SIZE_MAX)
			return -1;
		blob->origsiz = (size_t)origsiz;
		return 0;
	default: /* ignore */
		fido_log_debug("%s: cbor type", __func__);
		return 0;
	}
}

static int
largeblob_decode(largeblob_t *blob, const cbor_item_t *item)
{
	if (!cbor_isa_map(item) || !cbor_map_is_definite(item)) {
		fido_log_debug("%s: cbor type", __func__);
		return -1;
	}
	if (cbor_map_iter(item, blob, largeblob_do_decode) < 0) {
		fido_log_debug("%s: cbor_map_iter", __func__);
		return -1;
	}
	if (fido_blob_is_empty(&blob->ciphertext) ||
	    fido_blob_is_empty(&blob->nonce) || blob->origsiz == 0) {
		fido_log_debug("%s: incomplete blob", __func__);
		return -1;
	}

	return 0;
}

static cbor_item_t *
largeblob_encode(const fido_blob_t *body, const fido_blob_t *key)
{
	largeblob_t *blob;
	cbor_item_t *argv[3], *item = NULL;

	memset(argv, 0, sizeof(argv));
	if ((blob = largeblob_new()) == NULL ||
	    largeblob_seal(blob, body, key) < 0) {
		fido_log_debug("%s: largeblob_seal", __func__);
		goto fail;
	}
	if ((argv[0] = fido_blob_encode(&blob->ciphertext)) == NULL ||
	    (argv[1] = fido_blob_encode(&blob->nonce)) == NULL ||
	    (argv[2] = cbor_build_uint(blob->origsiz)) == NULL) {
		fido_log_debug("%s: cbor encode", __func__);
		goto fail;
	}
	item = cbor_flatten_vector(argv, nitems(argv));
fail:
	cbor_vector_free(argv, nitems(argv));
	largeblob_free(&blob);

	return item;
}

static int
largeblob_array_lookup(fido_blob_t *out, size_t *idx, const cbor_item_t *item,
    const fido_blob_t *key)
{
	cbor_item_t **v;
	fido_blob_t *plaintext = NULL;
	largeblob_t blob;
	int r;

	memset(&blob, 0, sizeof(blob));
	if (idx != NULL)
		*idx = 0;
	if ((v = cbor_array_handle(item)) == NULL)
		return FIDO_ERR_INVALID_ARGUMENT;
	for (size_t i = 0; i < cbor_array_size(item); i++) {
		if (largeblob_decode(&blob, v[i]) < 0 ||
		    (plaintext = largeblob_decrypt(&blob, key)) == NULL) {
			fido_log_debug("%s: largeblob_decode", __func__);
			largeblob_reset(&blob);
			continue;
		}
		if (idx != NULL)
			*idx = i;
		break;
	}
	if (plaintext == NULL) {
		fido_log_debug("%s: not found", __func__);
		return FIDO_ERR_NOTFOUND;
	}
	if (out != NULL)
		r = fido_uncompress(out, plaintext, blob.origsiz);
	else
		r = FIDO_OK;

	fido_blob_free(&plaintext);
	largeblob_reset(&blob);

	return r;
}

static int
largeblob_array_digest(u_char out[LARGEBLOB_DIGEST_LENGTH], const u_char *data,
    size_t len)
{
	u_char dgst[SHA256_DIGEST_LENGTH];

	if (data == NULL || len == 0)
		return -1;
	if (SHA256(data, len, dgst) != dgst)
		return -1;
	memcpy(out, dgst, LARGEBLOB_DIGEST_LENGTH);

	return 0;
}

static int
largeblob_array_check(const fido_blob_t *array)
{
	u_char expected_hash[LARGEBLOB_DIGEST_LENGTH];
	size_t body_len;

	fido_log_xxd(array->ptr, array->len, __func__);
	if (array->len < sizeof(expected_hash)) {
		fido_log_debug("%s: len %zu", __func__, array->len);
		return -1;
	}
	body_len = array->len - sizeof(expected_hash);
	if (largeblob_array_digest(expected_hash, array->ptr, body_len) < 0) {
		fido_log_debug("%s: largeblob_array_digest", __func__);
		return -1;
	}

	return timingsafe_bcmp(expected_hash, array->ptr + body_len,
	    sizeof(expected_hash));
}

static int
largeblob_get_array(fido_dev_t *dev, cbor_item_t **item, int *ms)
{
	fido_blob_t *array, *chunk = NULL;
	size_t n;
	int r;

	*item = NULL;
	if ((n = get_chunklen(dev)) == 0)
		return FIDO_ERR_INVALID_ARGUMENT;
	if ((array = fido_blob_new()) == NULL)
		return FIDO_ERR_INTERNAL;
	do {
		fido_blob_free(&chunk);
		if ((r = largeblob_get_tx(dev, array->len, n, ms)) != FIDO_OK ||
		    (r = largeblob_get_rx(dev, &chunk, ms)) != FIDO_OK) {
			fido_log_debug("%s: largeblob_get_wait %zu/%zu",
			    __func__, array->len, n);
			goto fail;
		}
		if (fido_blob_append(array, chunk->ptr, chunk->len) < 0) {
			fido_log_debug("%s: fido_blob_append", __func__);
			r = FIDO_ERR_INTERNAL;
			goto fail;
		}
	} while (chunk->len == n);

	if (largeblob_array_check(array) != 0)
		*item = cbor_new_definite_array(0); /* per spec */
	else
		*item = largeblob_array_load(array->ptr, array->len);
	if (*item == NULL)
		r = FIDO_ERR_INTERNAL;
	else
		r = FIDO_OK;
fail:
	fido_blob_free(&array);
	fido_blob_free(&chunk);

	return r;
}

static int
prepare_hmac(size_t offset, const u_char *data, size_t len, fido_blob_t *hmac)
{
	uint8_t buf[32 + 2 + sizeof(uint32_t) + SHA256_DIGEST_LENGTH];
	uint32_t u32_offset;

	if (data == NULL || len == 0) {
		fido_log_debug("%s: invalid data=%p, len=%zu", __func__,
		    (const void *)data, len);
		return -1;
	}
	if (offset > UINT32_MAX) {
		fido_log_debug("%s: invalid offset=%zu", __func__, offset);
		return -1;
	}

	memset(buf, 0xff, 32);
	buf[32] = CTAP_CBOR_LARGEBLOB;
	buf[33] = 0x00;
	u32_offset = htole32((uint32_t)offset);
	memcpy(&buf[34], &u32_offset, sizeof(uint32_t));
	if (SHA256(data, len, &buf[38]) != &buf[38]) {
		fido_log_debug("%s: SHA256", __func__);
		return -1;
	}

	return fido_blob_set(hmac, buf, sizeof(buf));
}

static int
largeblob_set_tx(fido_dev_t *dev, const fido_blob_t *token, const u_char *chunk,
    size_t chunk_len, size_t offset, size_t totalsiz, int *ms)
{
	fido_blob_t *hmac = NULL, f;
	cbor_item_t *argv[6];
	int r;

	memset(argv, 0, sizeof(argv));
	memset(&f, 0, sizeof(f));

	if ((argv[1] = cbor_build_bytestring(chunk, chunk_len)) == NULL ||
	    (argv[2] = cbor_build_uint(offset)) == NULL ||
	    (offset == 0 && (argv[3] = cbor_build_uint(totalsiz)) == NULL)) {
		fido_log_debug("%s: cbor encode", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}
	if (token != NULL) {
		if ((hmac = fido_blob_new()) == NULL ||
		    prepare_hmac(offset, chunk, chunk_len, hmac) < 0 ||
		    (argv[4] = cbor_encode_pin_auth(dev, token, hmac)) == NULL ||
		    (argv[5] = cbor_encode_pin_opt(dev)) == NULL) {
			fido_log_debug("%s: cbor_encode_pin_auth", __func__);
			r = FIDO_ERR_INTERNAL;
			goto fail;
		}
	}
	if (cbor_build_frame(CTAP_CBOR_LARGEBLOB, argv, nitems(argv), &f) < 0 ||
	    fido_tx(dev, CTAP_CMD_CBOR, f.ptr, f.len, ms) < 0) {
		fido_log_debug("%s: fido_tx", __func__);
		r = FIDO_ERR_TX;
		goto fail;
	}

	r = FIDO_OK;
fail:
	cbor_vector_free(argv, nitems(argv));
	fido_blob_free(&hmac);
	free(f.ptr);

	return r;
}

static int
largeblob_get_uv_token(fido_dev_t *dev, const char *pin, fido_blob_t **token,
    int *ms)
{
	es256_pk_t *pk = NULL;
	fido_blob_t *ecdh = NULL;
	int r;

	if ((*token = fido_blob_new()) == NULL)
		return FIDO_ERR_INTERNAL;
	if ((r = fido_do_ecdh(dev, &pk, &ecdh, ms)) != FIDO_OK) {
		fido_log_debug("%s: fido_do_ecdh", __func__);
		goto fail;
	}
	if ((r = fido_dev_get_uv_token(dev, CTAP_CBOR_LARGEBLOB, pin, ecdh, pk,
	    NULL, *token, ms)) != FIDO_OK) {
		fido_log_debug("%s: fido_dev_get_uv_token", __func__);
		goto fail;
	}

	r = FIDO_OK;
fail:
	if (r != FIDO_OK)
		fido_blob_free(token);

	fido_blob_free(&ecdh);
	es256_pk_free(&pk);

	return r;
}

static int
largeblob_set_array(fido_dev_t *dev, const cbor_item_t *item, const char *pin,
    int *ms)
{
	unsigned char dgst[SHA256_DIGEST_LENGTH];
	fido_blob_t cbor, *token = NULL;
	size_t chunklen, maxchunklen, totalsize;
	int r;

	memset(&cbor, 0, sizeof(cbor));

	if ((maxchunklen = get_chunklen(dev)) == 0) {
		fido_log_debug("%s: maxchunklen=%zu", __func__, maxchunklen);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto fail;
	}
	if (!cbor_isa_array(item) || !cbor_array_is_definite(item)) {
		fido_log_debug("%s: cbor type", __func__);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto fail;
	}
	if ((fido_blob_serialise(&cbor, item)) < 0) {
		fido_log_debug("%s: fido_blob_serialise", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}
	if (cbor.len > SIZE_MAX - sizeof(dgst)) {
		fido_log_debug("%s: cbor.len=%zu", __func__, cbor.len);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto fail;
	}
	if (SHA256(cbor.ptr, cbor.len, dgst) != dgst) {
		fido_log_debug("%s: SHA256", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}
	totalsize = cbor.len + sizeof(dgst) - 16; /* the first 16 bytes only */
	if (pin != NULL || fido_dev_supports_permissions(dev)) {
		if ((r = largeblob_get_uv_token(dev, pin, &token,
		    ms)) != FIDO_OK) {
			fido_log_debug("%s: largeblob_get_uv_token", __func__);
			goto fail;
		}
	}
	for (size_t offset = 0; offset < cbor.len; offset += chunklen) {
		if ((chunklen = cbor.len - offset) > maxchunklen)
			chunklen = maxchunklen;
		if ((r = largeblob_set_tx(dev, token, cbor.ptr + offset,
		    chunklen, offset, totalsize, ms)) != FIDO_OK ||
		    (r = fido_rx_cbor_status(dev, ms)) != FIDO_OK) {
			fido_log_debug("%s: body", __func__);
			goto fail;
		}
	}
	if ((r = largeblob_set_tx(dev, token, dgst, sizeof(dgst) - 16, cbor.len,
	    totalsize, ms)) != FIDO_OK ||
	    (r = fido_rx_cbor_status(dev, ms)) != FIDO_OK) {
		fido_log_debug("%s: dgst", __func__);
		goto fail;
	}

	r = FIDO_OK;
fail:
	fido_blob_free(&token);
	fido_blob_reset(&cbor);

	return r;
}

static int
largeblob_add(fido_dev_t *dev, const fido_blob_t *key, cbor_item_t *item,
    const char *pin, int *ms)
{
	cbor_item_t *array = NULL;
	size_t idx;
	int r;

	if ((r = largeblob_get_array(dev, &array, ms)) != FIDO_OK) {
		fido_log_debug("%s: largeblob_get_array", __func__);
		goto fail;
	}

	switch (r = largeblob_array_lookup(NULL, &idx, array, key)) {
	case FIDO_OK:
		if (!cbor_array_replace(array, idx, item)) {
			r = FIDO_ERR_INTERNAL;
			goto fail;
		}
		break;
	case FIDO_ERR_NOTFOUND:
		if (cbor_array_append(&array, item) < 0) {
			r = FIDO_ERR_INTERNAL;
			goto fail;
		}
		break;
	default:
		fido_log_debug("%s: largeblob_array_lookup", __func__);
		goto fail;
	}

	if ((r = largeblob_set_array(dev, array, pin, ms)) != FIDO_OK) {
		fido_log_debug("%s: largeblob_set_array", __func__);
		goto fail;
	}

	r = FIDO_OK;
fail:
	if (array != NULL)
		cbor_decref(&array);

	return r;
}

static int
largeblob_drop(fido_dev_t *dev, const fido_blob_t *key, const char *pin,
    int *ms)
{
	cbor_item_t *array = NULL;
	size_t idx;
	int r;

	if ((r = largeblob_get_array(dev, &array, ms)) != FIDO_OK) {
		fido_log_debug("%s: largeblob_get_array", __func__);
		goto fail;
	}
	if ((r = largeblob_array_lookup(NULL, &idx, array, key)) != FIDO_OK) {
		fido_log_debug("%s: largeblob_array_lookup", __func__);
		goto fail;
	}
	if (cbor_array_drop(&array, idx) < 0) {
		fido_log_debug("%s: cbor_array_drop", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}
	if ((r = largeblob_set_array(dev, array, pin, ms)) != FIDO_OK) {
		fido_log_debug("%s: largeblob_set_array", __func__);
		goto fail;
	}

	r = FIDO_OK;
fail:
	if (array != NULL)
		cbor_decref(&array);

	return r;
}

int
fido_dev_largeblob_get(fido_dev_t *dev, const unsigned char *key_ptr,
    size_t key_len, unsigned char **blob_ptr, size_t *blob_len)
{
	cbor_item_t *item = NULL;
	fido_blob_t key, body;
	int ms = dev->timeout_ms;
	int r;

	memset(&key, 0, sizeof(key));
	memset(&body, 0, sizeof(body));

	if (key_len != 32) {
		fido_log_debug("%s: invalid key len %zu", __func__, key_len);
		return FIDO_ERR_INVALID_ARGUMENT;
	}
	if (blob_ptr == NULL || blob_len == NULL) {
		fido_log_debug("%s: invalid blob_ptr=%p, blob_len=%p", __func__,
		    (const void *)blob_ptr, (const void *)blob_len);
		return FIDO_ERR_INVALID_ARGUMENT;
	}
	*blob_ptr = NULL;
	*blob_len = 0;
	if (fido_blob_set(&key, key_ptr, key_len) < 0) {
		fido_log_debug("%s: fido_blob_set", __func__);
		return FIDO_ERR_INTERNAL;
	}
	if ((r = largeblob_get_array(dev, &item, &ms)) != FIDO_OK) {
		fido_log_debug("%s: largeblob_get_array", __func__);
		goto fail;
	}
	if ((r = largeblob_array_lookup(&body, NULL, item, &key)) != FIDO_OK)
		fido_log_debug("%s: largeblob_array_lookup", __func__);
	else {
		*blob_ptr = body.ptr;
		*blob_len = body.len;
	}
fail:
	if (item != NULL)
		cbor_decref(&item);

	fido_blob_reset(&key);

	return r;
}

int
fido_dev_largeblob_set(fido_dev_t *dev, const unsigned char *key_ptr,
    size_t key_len, const unsigned char *blob_ptr, size_t blob_len,
    const char *pin)
{
	cbor_item_t *item = NULL;
	fido_blob_t key, body;
	int ms = dev->timeout_ms;
	int r;

	memset(&key, 0, sizeof(key));
	memset(&body, 0, sizeof(body));

	if (key_len != 32) {
		fido_log_debug("%s: invalid key len %zu", __func__, key_len);
		return FIDO_ERR_INVALID_ARGUMENT;
	}
	if (blob_ptr == NULL || blob_len == 0) {
		fido_log_debug("%s: invalid blob_ptr=%p, blob_len=%zu", __func__,
		    (const void *)blob_ptr, blob_len);
		return FIDO_ERR_INVALID_ARGUMENT;
	}
	if (fido_blob_set(&key, key_ptr, key_len) < 0 ||
	    fido_blob_set(&body, blob_ptr, blob_len) < 0) {
		fido_log_debug("%s: fido_blob_set", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}
	if ((item = largeblob_encode(&body, &key)) == NULL) {
		fido_log_debug("%s: largeblob_encode", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}
	if ((r = largeblob_add(dev, &key, item, pin, &ms)) != FIDO_OK)
		fido_log_debug("%s: largeblob_add", __func__);
fail:
	if (item != NULL)
		cbor_decref(&item);

	fido_blob_reset(&key);
	fido_blob_reset(&body);

	return r;
}

int
fido_dev_largeblob_remove(fido_dev_t *dev, const unsigned char *key_ptr,
    size_t key_len, const char *pin)
{
	fido_blob_t key;
	int ms = dev->timeout_ms;
	int r;

	memset(&key, 0, sizeof(key));

	if (key_len != 32) {
		fido_log_debug("%s: invalid key len %zu", __func__, key_len);
		return FIDO_ERR_INVALID_ARGUMENT;
	}
	if (fido_blob_set(&key, key_ptr, key_len) < 0) {
		fido_log_debug("%s: fido_blob_set", __func__);
		return FIDO_ERR_INTERNAL;
	}
	if ((r = largeblob_drop(dev, &key, pin, &ms)) != FIDO_OK)
		fido_log_debug("%s: largeblob_drop", __func__);

	fido_blob_reset(&key);

	return r;
}

int
fido_dev_largeblob_get_array(fido_dev_t *dev, unsigned char **cbor_ptr,
    size_t *cbor_len)
{
	cbor_item_t *item = NULL;
	fido_blob_t cbor;
	int ms = dev->timeout_ms;
	int r;

	memset(&cbor, 0, sizeof(cbor));

	if (cbor_ptr == NULL || cbor_len == NULL) {
		fido_log_debug("%s: invalid cbor_ptr=%p, cbor_len=%p", __func__,
		    (const void *)cbor_ptr, (const void *)cbor_len);
		return FIDO_ERR_INVALID_ARGUMENT;
	}
	*cbor_ptr = NULL;
	*cbor_len = 0;
	if ((r = largeblob_get_array(dev, &item, &ms)) != FIDO_OK) {
		fido_log_debug("%s: largeblob_get_array", __func__);
		return r;
	}
	if (fido_blob_serialise(&cbor, item) < 0) {
		fido_log_debug("%s: fido_blob_serialise", __func__);
		r = FIDO_ERR_INTERNAL;
	} else {
		*cbor_ptr = cbor.ptr;
		*cbor_len = cbor.len;
	}

	cbor_decref(&item);

	return r;
}

int
fido_dev_largeblob_set_array(fido_dev_t *dev, const unsigned char *cbor_ptr,
    size_t cbor_len, const char *pin)
{
	cbor_item_t *item = NULL;
	struct cbor_load_result cbor_result;
	int ms = dev->timeout_ms;
	int r;

	if (cbor_ptr == NULL || cbor_len == 0) {
		fido_log_debug("%s: invalid cbor_ptr=%p, cbor_len=%zu", __func__,
		    (const void *)cbor_ptr, cbor_len);
		return FIDO_ERR_INVALID_ARGUMENT;
	}
	if ((item = cbor_load(cbor_ptr, cbor_len, &cbor_result)) == NULL) {
		fido_log_debug("%s: cbor_load", __func__);
		return FIDO_ERR_INVALID_ARGUMENT;
	}
	if ((r = largeblob_set_array(dev, item, pin, &ms)) != FIDO_OK)
		fido_log_debug("%s: largeblob_set_array", __func__);

	cbor_decref(&item);

	return r;
}
