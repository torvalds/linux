/*
 * Copyright (c) 2018 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <openssl/sha.h>
#include "fido.h"
#include "fido/es256.h"

#define CTAP21_UV_TOKEN_PERM_MAKECRED	0x01
#define CTAP21_UV_TOKEN_PERM_ASSERT	0x02
#define CTAP21_UV_TOKEN_PERM_CRED_MGMT	0x04
#define CTAP21_UV_TOKEN_PERM_BIO	0x08
#define CTAP21_UV_TOKEN_PERM_LARGEBLOB	0x10
#define CTAP21_UV_TOKEN_PERM_CONFIG	0x20

int
fido_sha256(fido_blob_t *digest, const u_char *data, size_t data_len)
{
	if ((digest->ptr = calloc(1, SHA256_DIGEST_LENGTH)) == NULL)
		return (-1);

	digest->len = SHA256_DIGEST_LENGTH;

	if (SHA256(data, data_len, digest->ptr) != digest->ptr) {
		fido_blob_reset(digest);
		return (-1);
	}

	return (0);
}

static int
pin_sha256_enc(const fido_dev_t *dev, const fido_blob_t *shared,
    const fido_blob_t *pin, fido_blob_t **out)
{
	fido_blob_t	*ph = NULL;
	int		 r;

	if ((*out = fido_blob_new()) == NULL ||
	    (ph = fido_blob_new()) == NULL) {
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if (fido_sha256(ph, pin->ptr, pin->len) < 0 || ph->len < 16) {
		fido_log_debug("%s: SHA256", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	ph->len = 16; /* first 16 bytes */

	if (aes256_cbc_enc(dev, shared, ph, *out) < 0) {
		fido_log_debug("%s: aes256_cbc_enc", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	r = FIDO_OK;
fail:
	fido_blob_free(&ph);

	return (r);
}

static int
pad64(const char *pin, fido_blob_t **ppin)
{
	size_t	pin_len;
	size_t	ppin_len;

	pin_len = strlen(pin);
	if (pin_len < 4 || pin_len > 255) {
		fido_log_debug("%s: invalid pin length", __func__);
		return (FIDO_ERR_PIN_POLICY_VIOLATION);
	}

	if ((*ppin = fido_blob_new()) == NULL)
		return (FIDO_ERR_INTERNAL);

	ppin_len = (pin_len + 63U) & ~63U;
	if (ppin_len < pin_len || ((*ppin)->ptr = calloc(1, ppin_len)) == NULL) {
		fido_blob_free(ppin);
		return (FIDO_ERR_INTERNAL);
	}

	memcpy((*ppin)->ptr, pin, pin_len);
	(*ppin)->len = ppin_len;

	return (FIDO_OK);
}

static int
pin_pad64_enc(const fido_dev_t *dev, const fido_blob_t *shared,
    const char *pin, fido_blob_t **out)
{
	fido_blob_t *ppin = NULL;
	int	     r;

	if ((r = pad64(pin, &ppin)) != FIDO_OK) {
		fido_log_debug("%s: pad64", __func__);
		    goto fail;
	}

	if ((*out = fido_blob_new()) == NULL) {
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if (aes256_cbc_enc(dev, shared, ppin, *out) < 0) {
		fido_log_debug("%s: aes256_cbc_enc", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	r = FIDO_OK;
fail:
	fido_blob_free(&ppin);

	return (r);
}

static cbor_item_t *
encode_uv_permission(uint8_t cmd)
{
	switch (cmd) {
	case CTAP_CBOR_ASSERT:
		return (cbor_build_uint8(CTAP21_UV_TOKEN_PERM_ASSERT));
	case CTAP_CBOR_BIO_ENROLL_PRE:
		return (cbor_build_uint8(CTAP21_UV_TOKEN_PERM_BIO));
	case CTAP_CBOR_CONFIG:
		return (cbor_build_uint8(CTAP21_UV_TOKEN_PERM_CONFIG));
	case CTAP_CBOR_MAKECRED:
		return (cbor_build_uint8(CTAP21_UV_TOKEN_PERM_MAKECRED));
	case CTAP_CBOR_CRED_MGMT_PRE:
		return (cbor_build_uint8(CTAP21_UV_TOKEN_PERM_CRED_MGMT));
	case CTAP_CBOR_LARGEBLOB:
		return (cbor_build_uint8(CTAP21_UV_TOKEN_PERM_LARGEBLOB));
	default:
		fido_log_debug("%s: cmd 0x%02x", __func__, cmd);
		return (NULL);
	}
}

static int
ctap20_uv_token_tx(fido_dev_t *dev, const char *pin, const fido_blob_t *ecdh,
    const es256_pk_t *pk, int *ms)
{
	fido_blob_t	 f;
	fido_blob_t	*p = NULL;
	fido_blob_t	*phe = NULL;
	cbor_item_t	*argv[6];
	int		 r;

	memset(&f, 0, sizeof(f));
	memset(argv, 0, sizeof(argv));

	if (pin == NULL) {
		fido_log_debug("%s: NULL pin", __func__);
		r = FIDO_ERR_PIN_REQUIRED;
		goto fail;
	}

	if ((p = fido_blob_new()) == NULL || fido_blob_set(p,
	    (const unsigned char *)pin, strlen(pin)) < 0) {
		fido_log_debug("%s: fido_blob_set", __func__);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto fail;
	}

	if ((r = pin_sha256_enc(dev, ecdh, p, &phe)) != FIDO_OK) {
		fido_log_debug("%s: pin_sha256_enc", __func__);
		goto fail;
	}

	if ((argv[0] = cbor_encode_pin_opt(dev)) == NULL ||
	    (argv[1] = cbor_build_uint8(5)) == NULL ||
	    (argv[2] = es256_pk_encode(pk, 1)) == NULL ||
	    (argv[5] = fido_blob_encode(phe)) == NULL) {
		fido_log_debug("%s: cbor encode", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if (cbor_build_frame(CTAP_CBOR_CLIENT_PIN, argv, nitems(argv),
	    &f) < 0 || fido_tx(dev, CTAP_CMD_CBOR, f.ptr, f.len, ms) < 0) {
		fido_log_debug("%s: fido_tx", __func__);
		r = FIDO_ERR_TX;
		goto fail;
	}

	r = FIDO_OK;
fail:
	cbor_vector_free(argv, nitems(argv));
	fido_blob_free(&p);
	fido_blob_free(&phe);
	free(f.ptr);

	return (r);
}

static int
ctap21_uv_token_tx(fido_dev_t *dev, const char *pin, const fido_blob_t *ecdh,
    const es256_pk_t *pk, uint8_t cmd, const char *rpid, int *ms)
{
	fido_blob_t	 f;
	fido_blob_t	*p = NULL;
	fido_blob_t	*phe = NULL;
	cbor_item_t	*argv[10];
	uint8_t		 subcmd;
	int		 r;

	memset(&f, 0, sizeof(f));
	memset(argv, 0, sizeof(argv));

	if (pin != NULL) {
		if ((p = fido_blob_new()) == NULL || fido_blob_set(p,
		    (const unsigned char *)pin, strlen(pin)) < 0) {
			fido_log_debug("%s: fido_blob_set", __func__);
			r = FIDO_ERR_INVALID_ARGUMENT;
			goto fail;
		}
		if ((r = pin_sha256_enc(dev, ecdh, p, &phe)) != FIDO_OK) {
			fido_log_debug("%s: pin_sha256_enc", __func__);
			goto fail;
		}
		subcmd = 9; /* getPinUvAuthTokenUsingPinWithPermissions */
	} else {
		if (fido_dev_has_uv(dev) == false) {
			fido_log_debug("%s: fido_dev_has_uv", __func__);
			r = FIDO_ERR_PIN_REQUIRED;
			goto fail;
		}
		subcmd = 6; /* getPinUvAuthTokenUsingUvWithPermissions */
	}

	if ((argv[0] = cbor_encode_pin_opt(dev)) == NULL ||
	    (argv[1] = cbor_build_uint8(subcmd)) == NULL ||
	    (argv[2] = es256_pk_encode(pk, 1)) == NULL ||
	    (phe != NULL && (argv[5] = fido_blob_encode(phe)) == NULL) ||
	    (argv[8] = encode_uv_permission(cmd)) == NULL ||
	    (rpid != NULL && (argv[9] = cbor_build_string(rpid)) == NULL)) {
		fido_log_debug("%s: cbor encode", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if (cbor_build_frame(CTAP_CBOR_CLIENT_PIN, argv, nitems(argv),
	    &f) < 0 || fido_tx(dev, CTAP_CMD_CBOR, f.ptr, f.len, ms) < 0) {
		fido_log_debug("%s:  fido_tx", __func__);
		r = FIDO_ERR_TX;
		goto fail;
	}

	r = FIDO_OK;
fail:
	cbor_vector_free(argv, nitems(argv));
	fido_blob_free(&p);
	fido_blob_free(&phe);
	free(f.ptr);

	return (r);
}

static int
parse_uv_token(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	fido_blob_t *token = arg;

	if (cbor_isa_uint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8 ||
	    cbor_get_uint8(key) != 2) {
		fido_log_debug("%s: cbor type", __func__);
		return (0); /* ignore */
	}

	return (fido_blob_decode(val, token));
}

static int
uv_token_rx(fido_dev_t *dev, const fido_blob_t *ecdh, fido_blob_t *token,
    int *ms)
{
	fido_blob_t	*aes_token = NULL;
	unsigned char	 reply[FIDO_MAXMSG];
	int		 reply_len;
	int		 r;

	if ((aes_token = fido_blob_new()) == NULL) {
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if ((reply_len = fido_rx(dev, CTAP_CMD_CBOR, &reply, sizeof(reply),
	    ms)) < 0) {
		fido_log_debug("%s: fido_rx", __func__);
		r = FIDO_ERR_RX;
		goto fail;
	}

	if ((r = cbor_parse_reply(reply, (size_t)reply_len, aes_token,
	    parse_uv_token)) != FIDO_OK) {
		fido_log_debug("%s: parse_uv_token", __func__);
		goto fail;
	}

	if  (aes256_cbc_dec(dev, ecdh, aes_token, token) < 0) {
		fido_log_debug("%s: aes256_cbc_dec", __func__);
		r = FIDO_ERR_RX;
		goto fail;
	}

	r = FIDO_OK;
fail:
	fido_blob_free(&aes_token);

	return (r);
}

static int
uv_token_wait(fido_dev_t *dev, uint8_t cmd, const char *pin,
    const fido_blob_t *ecdh, const es256_pk_t *pk, const char *rpid,
    fido_blob_t *token, int *ms)
{
	int r;

	if (ecdh == NULL || pk == NULL)
		return (FIDO_ERR_INVALID_ARGUMENT);
	if (fido_dev_supports_permissions(dev))
		r = ctap21_uv_token_tx(dev, pin, ecdh, pk, cmd, rpid, ms);
	else
		r = ctap20_uv_token_tx(dev, pin, ecdh, pk, ms);
	if (r != FIDO_OK)
		return (r);

	return (uv_token_rx(dev, ecdh, token, ms));
}

int
fido_dev_get_uv_token(fido_dev_t *dev, uint8_t cmd, const char *pin,
    const fido_blob_t *ecdh, const es256_pk_t *pk, const char *rpid,
    fido_blob_t *token, int *ms)
{
	return (uv_token_wait(dev, cmd, pin, ecdh, pk, rpid, token, ms));
}

static int
fido_dev_change_pin_tx(fido_dev_t *dev, const char *pin, const char *oldpin,
    int *ms)
{
	fido_blob_t	 f;
	fido_blob_t	*ppine = NULL;
	fido_blob_t	*ecdh = NULL;
	fido_blob_t	*opin = NULL;
	fido_blob_t	*opinhe = NULL;
	cbor_item_t	*argv[6];
	es256_pk_t	*pk = NULL;
	int r;

	memset(&f, 0, sizeof(f));
	memset(argv, 0, sizeof(argv));

	if ((opin = fido_blob_new()) == NULL || fido_blob_set(opin,
	    (const unsigned char *)oldpin, strlen(oldpin)) < 0) {
		fido_log_debug("%s: fido_blob_set", __func__);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto fail;
	}

	if ((r = fido_do_ecdh(dev, &pk, &ecdh, ms)) != FIDO_OK) {
		fido_log_debug("%s: fido_do_ecdh", __func__);
		goto fail;
	}

	/* pad and encrypt new pin */
	if ((r = pin_pad64_enc(dev, ecdh, pin, &ppine)) != FIDO_OK) {
		fido_log_debug("%s: pin_pad64_enc", __func__);
		goto fail;
	}

	/* hash and encrypt old pin */
	if ((r = pin_sha256_enc(dev, ecdh, opin, &opinhe)) != FIDO_OK) {
		fido_log_debug("%s: pin_sha256_enc", __func__);
		goto fail;
	}

	if ((argv[0] = cbor_encode_pin_opt(dev)) == NULL ||
	    (argv[1] = cbor_build_uint8(4)) == NULL ||
	    (argv[2] = es256_pk_encode(pk, 1)) == NULL ||
	    (argv[3] = cbor_encode_change_pin_auth(dev, ecdh, ppine, opinhe)) == NULL ||
	    (argv[4] = fido_blob_encode(ppine)) == NULL ||
	    (argv[5] = fido_blob_encode(opinhe)) == NULL) {
		fido_log_debug("%s: cbor encode", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if (cbor_build_frame(CTAP_CBOR_CLIENT_PIN, argv, nitems(argv),
	    &f) < 0 || fido_tx(dev, CTAP_CMD_CBOR, f.ptr, f.len, ms) < 0) {
		fido_log_debug("%s: fido_tx", __func__);
		r = FIDO_ERR_TX;
		goto fail;
	}

	r = FIDO_OK;
fail:
	cbor_vector_free(argv, nitems(argv));
	es256_pk_free(&pk);
	fido_blob_free(&ppine);
	fido_blob_free(&ecdh);
	fido_blob_free(&opin);
	fido_blob_free(&opinhe);
	free(f.ptr);

	return (r);

}

static int
fido_dev_set_pin_tx(fido_dev_t *dev, const char *pin, int *ms)
{
	fido_blob_t	 f;
	fido_blob_t	*ppine = NULL;
	fido_blob_t	*ecdh = NULL;
	cbor_item_t	*argv[5];
	es256_pk_t	*pk = NULL;
	int		 r;

	memset(&f, 0, sizeof(f));
	memset(argv, 0, sizeof(argv));

	if ((r = fido_do_ecdh(dev, &pk, &ecdh, ms)) != FIDO_OK) {
		fido_log_debug("%s: fido_do_ecdh", __func__);
		goto fail;
	}

	if ((r = pin_pad64_enc(dev, ecdh, pin, &ppine)) != FIDO_OK) {
		fido_log_debug("%s: pin_pad64_enc", __func__);
		goto fail;
	}

	if ((argv[0] = cbor_encode_pin_opt(dev)) == NULL ||
	    (argv[1] = cbor_build_uint8(3)) == NULL ||
	    (argv[2] = es256_pk_encode(pk, 1)) == NULL ||
	    (argv[3] = cbor_encode_pin_auth(dev, ecdh, ppine)) == NULL ||
	    (argv[4] = fido_blob_encode(ppine)) == NULL) {
		fido_log_debug("%s: cbor encode", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if (cbor_build_frame(CTAP_CBOR_CLIENT_PIN, argv, nitems(argv),
	    &f) < 0 || fido_tx(dev, CTAP_CMD_CBOR, f.ptr, f.len, ms) < 0) {
		fido_log_debug("%s: fido_tx", __func__);
		r = FIDO_ERR_TX;
		goto fail;
	}

	r = FIDO_OK;
fail:
	cbor_vector_free(argv, nitems(argv));
	es256_pk_free(&pk);
	fido_blob_free(&ppine);
	fido_blob_free(&ecdh);
	free(f.ptr);

	return (r);
}

static int
fido_dev_set_pin_wait(fido_dev_t *dev, const char *pin, const char *oldpin,
    int *ms)
{
	int r;

	if (oldpin != NULL) {
		if ((r = fido_dev_change_pin_tx(dev, pin, oldpin,
		    ms)) != FIDO_OK) {
			fido_log_debug("%s: fido_dev_change_pin_tx", __func__);
			return (r);
		}
	} else {
		if ((r = fido_dev_set_pin_tx(dev, pin, ms)) != FIDO_OK) {
			fido_log_debug("%s: fido_dev_set_pin_tx", __func__);
			return (r);
		}
	}

	if ((r = fido_rx_cbor_status(dev, ms)) != FIDO_OK) {
		fido_log_debug("%s: fido_rx_cbor_status", __func__);
		return (r);
	}

	if (dev->flags & FIDO_DEV_PIN_UNSET) {
		dev->flags &= ~FIDO_DEV_PIN_UNSET;
		dev->flags |= FIDO_DEV_PIN_SET;
	}

	return (FIDO_OK);
}

int
fido_dev_set_pin(fido_dev_t *dev, const char *pin, const char *oldpin)
{
	int ms = dev->timeout_ms;

	return (fido_dev_set_pin_wait(dev, pin, oldpin, &ms));
}

static int
parse_retry_count(const uint8_t keyval, const cbor_item_t *key,
    const cbor_item_t *val, void *arg)
{
	int		*retries = arg;
	uint64_t	 n;

	if (cbor_isa_uint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8 ||
	    cbor_get_uint8(key) != keyval) {
		fido_log_debug("%s: cbor type", __func__);
		return (0); /* ignore */
	}

	if (cbor_decode_uint64(val, &n) < 0 || n > INT_MAX) {
		fido_log_debug("%s: cbor_decode_uint64", __func__);
		return (-1);
	}

	*retries = (int)n;

	return (0);
}

static int
parse_pin_retry_count(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	return (parse_retry_count(3, key, val, arg));
}

static int
parse_uv_retry_count(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	return (parse_retry_count(5, key, val, arg));
}

static int
fido_dev_get_retry_count_tx(fido_dev_t *dev, uint8_t subcmd, int *ms)
{
	fido_blob_t	 f;
	cbor_item_t	*argv[2];
	int		 r;

	memset(&f, 0, sizeof(f));
	memset(argv, 0, sizeof(argv));

	if ((argv[0] = cbor_build_uint8(1)) == NULL ||
	    (argv[1] = cbor_build_uint8(subcmd)) == NULL) {
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if (cbor_build_frame(CTAP_CBOR_CLIENT_PIN, argv, nitems(argv),
	    &f) < 0 || fido_tx(dev, CTAP_CMD_CBOR, f.ptr, f.len, ms) < 0) {
		fido_log_debug("%s: fido_tx", __func__);
		r = FIDO_ERR_TX;
		goto fail;
	}

	r = FIDO_OK;
fail:
	cbor_vector_free(argv, nitems(argv));
	free(f.ptr);

	return (r);
}

static int
fido_dev_get_pin_retry_count_rx(fido_dev_t *dev, int *retries, int *ms)
{
	unsigned char	reply[FIDO_MAXMSG];
	int		reply_len;
	int		r;

	*retries = 0;

	if ((reply_len = fido_rx(dev, CTAP_CMD_CBOR, &reply, sizeof(reply),
	    ms)) < 0) {
		fido_log_debug("%s: fido_rx", __func__);
		return (FIDO_ERR_RX);
	}

	if ((r = cbor_parse_reply(reply, (size_t)reply_len, retries,
	    parse_pin_retry_count)) != FIDO_OK) {
		fido_log_debug("%s: parse_pin_retry_count", __func__);
		return (r);
	}

	return (FIDO_OK);
}

static int
fido_dev_get_pin_retry_count_wait(fido_dev_t *dev, int *retries, int *ms)
{
	int r;

	if ((r = fido_dev_get_retry_count_tx(dev, 1, ms)) != FIDO_OK ||
	    (r = fido_dev_get_pin_retry_count_rx(dev, retries, ms)) != FIDO_OK)
		return (r);

	return (FIDO_OK);
}

int
fido_dev_get_retry_count(fido_dev_t *dev, int *retries)
{
	int ms = dev->timeout_ms;

	return (fido_dev_get_pin_retry_count_wait(dev, retries, &ms));
}

static int
fido_dev_get_uv_retry_count_rx(fido_dev_t *dev, int *retries, int *ms)
{
	unsigned char	reply[FIDO_MAXMSG];
	int		reply_len;
	int		r;

	*retries = 0;

	if ((reply_len = fido_rx(dev, CTAP_CMD_CBOR, &reply, sizeof(reply),
	    ms)) < 0) {
		fido_log_debug("%s: fido_rx", __func__);
		return (FIDO_ERR_RX);
	}

	if ((r = cbor_parse_reply(reply, (size_t)reply_len, retries,
	    parse_uv_retry_count)) != FIDO_OK) {
		fido_log_debug("%s: parse_uv_retry_count", __func__);
		return (r);
	}

	return (FIDO_OK);
}

static int
fido_dev_get_uv_retry_count_wait(fido_dev_t *dev, int *retries, int *ms)
{
	int r;

	if ((r = fido_dev_get_retry_count_tx(dev, 7, ms)) != FIDO_OK ||
	    (r = fido_dev_get_uv_retry_count_rx(dev, retries, ms)) != FIDO_OK)
		return (r);

	return (FIDO_OK);
}

int
fido_dev_get_uv_retry_count(fido_dev_t *dev, int *retries)
{
	int ms = dev->timeout_ms;

	return (fido_dev_get_uv_retry_count_wait(dev, retries, &ms));
}

int
cbor_add_uv_params(fido_dev_t *dev, uint8_t cmd, const fido_blob_t *hmac_data,
    const es256_pk_t *pk, const fido_blob_t *ecdh, const char *pin,
    const char *rpid, cbor_item_t **auth, cbor_item_t **opt, int *ms)
{
	fido_blob_t	*token = NULL;
	int		 r;

	if ((token = fido_blob_new()) == NULL) {
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if ((r = fido_dev_get_uv_token(dev, cmd, pin, ecdh, pk, rpid,
	    token, ms)) != FIDO_OK) {
		fido_log_debug("%s: fido_dev_get_uv_token", __func__);
		goto fail;
	}

	if ((*auth = cbor_encode_pin_auth(dev, token, hmac_data)) == NULL ||
	    (*opt = cbor_encode_pin_opt(dev)) == NULL) {
		fido_log_debug("%s: cbor encode", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	r = FIDO_OK;
fail:
	fido_blob_free(&token);

	return (r);
}
