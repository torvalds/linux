/*
 * Copyright (c) 2019 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include "fido.h"
#include "fido/bio.h"
#include "fido/es256.h"

#define CMD_ENROLL_BEGIN	0x01
#define CMD_ENROLL_NEXT		0x02
#define CMD_ENROLL_CANCEL	0x03
#define CMD_ENUM		0x04
#define CMD_SET_NAME		0x05
#define CMD_ENROLL_REMOVE	0x06
#define CMD_GET_INFO		0x07

static int
bio_prepare_hmac(uint8_t cmd, cbor_item_t **argv, size_t argc,
    cbor_item_t **param, fido_blob_t *hmac_data)
{
	const uint8_t	 prefix[2] = { 0x01 /* modality */, cmd };
	int		 ok = -1;
	size_t		 cbor_alloc_len;
	size_t		 cbor_len;
	unsigned char	*cbor = NULL;

	if (argv == NULL || param == NULL)
		return (fido_blob_set(hmac_data, prefix, sizeof(prefix)));

	if ((*param = cbor_flatten_vector(argv, argc)) == NULL) {
		fido_log_debug("%s: cbor_flatten_vector", __func__);
		goto fail;
	}

	if ((cbor_len = cbor_serialize_alloc(*param, &cbor,
	    &cbor_alloc_len)) == 0 || cbor_len > SIZE_MAX - sizeof(prefix)) {
		fido_log_debug("%s: cbor_serialize_alloc", __func__);
		goto fail;
	}

	if ((hmac_data->ptr = malloc(cbor_len + sizeof(prefix))) == NULL) {
		fido_log_debug("%s: malloc", __func__);
		goto fail;
	}

	memcpy(hmac_data->ptr, prefix, sizeof(prefix));
	memcpy(hmac_data->ptr + sizeof(prefix), cbor, cbor_len);
	hmac_data->len = cbor_len + sizeof(prefix);

	ok = 0;
fail:
	free(cbor);

	return (ok);
}

static int
bio_tx(fido_dev_t *dev, uint8_t subcmd, cbor_item_t **sub_argv, size_t sub_argc,
    const char *pin, const fido_blob_t *token, int *ms)
{
	cbor_item_t	*argv[5];
	es256_pk_t	*pk = NULL;
	fido_blob_t	*ecdh = NULL;
	fido_blob_t	 f;
	fido_blob_t	 hmac;
	const uint8_t	 cmd = CTAP_CBOR_BIO_ENROLL_PRE;
	int		 r = FIDO_ERR_INTERNAL;

	memset(&f, 0, sizeof(f));
	memset(&hmac, 0, sizeof(hmac));
	memset(&argv, 0, sizeof(argv));

	/* modality, subCommand */
	if ((argv[0] = cbor_build_uint8(1)) == NULL ||
	    (argv[1] = cbor_build_uint8(subcmd)) == NULL) {
		fido_log_debug("%s: cbor encode", __func__);
		goto fail;
	}

	/* subParams */
	if (pin || token) {
		if (bio_prepare_hmac(subcmd, sub_argv, sub_argc, &argv[2],
		    &hmac) < 0) {
			fido_log_debug("%s: bio_prepare_hmac", __func__);
			goto fail;
		}
	}

	/* pinProtocol, pinAuth */
	if (pin) {
		if ((r = fido_do_ecdh(dev, &pk, &ecdh, ms)) != FIDO_OK) {
			fido_log_debug("%s: fido_do_ecdh", __func__);
			goto fail;
		}
		if ((r = cbor_add_uv_params(dev, cmd, &hmac, pk, ecdh, pin,
		    NULL, &argv[4], &argv[3], ms)) != FIDO_OK) {
			fido_log_debug("%s: cbor_add_uv_params", __func__);
			goto fail;
		}
	} else if (token) {
		if ((argv[3] = cbor_encode_pin_opt(dev)) == NULL ||
		    (argv[4] = cbor_encode_pin_auth(dev, token, &hmac)) == NULL) {
			fido_log_debug("%s: encode pin", __func__);
			goto fail;
		}
	}

	/* framing and transmission */
	if (cbor_build_frame(cmd, argv, nitems(argv), &f) < 0 ||
	    fido_tx(dev, CTAP_CMD_CBOR, f.ptr, f.len, ms) < 0) {
		fido_log_debug("%s: fido_tx", __func__);
		r = FIDO_ERR_TX;
		goto fail;
	}

	r = FIDO_OK;
fail:
	cbor_vector_free(argv, nitems(argv));
	es256_pk_free(&pk);
	fido_blob_free(&ecdh);
	free(f.ptr);
	free(hmac.ptr);

	return (r);
}

static void
bio_reset_template(fido_bio_template_t *t)
{
	free(t->name);
	t->name = NULL;
	fido_blob_reset(&t->id);
}

static void
bio_reset_template_array(fido_bio_template_array_t *ta)
{
	for (size_t i = 0; i < ta->n_alloc; i++)
		bio_reset_template(&ta->ptr[i]);

	free(ta->ptr);
	ta->ptr = NULL;
	memset(ta, 0, sizeof(*ta));
}

static int
decode_template(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	fido_bio_template_t *t = arg;

	if (cbor_isa_uint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8) {
		fido_log_debug("%s: cbor type", __func__);
		return (0); /* ignore */
	}

	switch (cbor_get_uint8(key)) {
	case 1: /* id */
		return (fido_blob_decode(val, &t->id));
	case 2: /* name */
		return (cbor_string_copy(val, &t->name));
	}

	return (0); /* ignore */
}

static int
decode_template_array(const cbor_item_t *item, void *arg)
{
	fido_bio_template_array_t *ta = arg;

	if (cbor_isa_map(item) == false ||
	    cbor_map_is_definite(item) == false) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	if (ta->n_rx >= ta->n_alloc) {
		fido_log_debug("%s: n_rx >= n_alloc", __func__);
		return (-1);
	}

	if (cbor_map_iter(item, &ta->ptr[ta->n_rx], decode_template) < 0) {
		fido_log_debug("%s: decode_template", __func__);
		return (-1);
	}

	ta->n_rx++;

	return (0);
}

static int
bio_parse_template_array(const cbor_item_t *key, const cbor_item_t *val,
    void *arg)
{
	fido_bio_template_array_t *ta = arg;

	if (cbor_isa_uint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8 ||
	    cbor_get_uint8(key) != 7) {
		fido_log_debug("%s: cbor type", __func__);
		return (0); /* ignore */
	}

	if (cbor_isa_array(val) == false ||
	    cbor_array_is_definite(val) == false) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	if (ta->ptr != NULL || ta->n_alloc != 0 || ta->n_rx != 0) {
		fido_log_debug("%s: ptr != NULL || n_alloc != 0 || n_rx != 0",
		    __func__);
		return (-1);
	}

	if ((ta->ptr = calloc(cbor_array_size(val), sizeof(*ta->ptr))) == NULL)
		return (-1);

	ta->n_alloc = cbor_array_size(val);

	if (cbor_array_iter(val, ta, decode_template_array) < 0) {
		fido_log_debug("%s: decode_template_array", __func__);
		return (-1);
	}

	return (0);
}

static int
bio_rx_template_array(fido_dev_t *dev, fido_bio_template_array_t *ta, int *ms)
{
	unsigned char	reply[FIDO_MAXMSG];
	int		reply_len;
	int		r;

	bio_reset_template_array(ta);

	if ((reply_len = fido_rx(dev, CTAP_CMD_CBOR, &reply, sizeof(reply),
	    ms)) < 0) {
		fido_log_debug("%s: fido_rx", __func__);
		return (FIDO_ERR_RX);
	}

	if ((r = cbor_parse_reply(reply, (size_t)reply_len, ta,
	    bio_parse_template_array)) != FIDO_OK) {
		fido_log_debug("%s: bio_parse_template_array" , __func__);
		return (r);
	}

	return (FIDO_OK);
}

static int
bio_get_template_array_wait(fido_dev_t *dev, fido_bio_template_array_t *ta,
    const char *pin, int *ms)
{
	int r;

	if ((r = bio_tx(dev, CMD_ENUM, NULL, 0, pin, NULL, ms)) != FIDO_OK ||
	    (r = bio_rx_template_array(dev, ta, ms)) != FIDO_OK)
		return (r);

	return (FIDO_OK);
}

int
fido_bio_dev_get_template_array(fido_dev_t *dev, fido_bio_template_array_t *ta,
    const char *pin)
{
	int ms = dev->timeout_ms;

	if (pin == NULL)
		return (FIDO_ERR_INVALID_ARGUMENT);

	return (bio_get_template_array_wait(dev, ta, pin, &ms));
}

static int
bio_set_template_name_wait(fido_dev_t *dev, const fido_bio_template_t *t,
    const char *pin, int *ms)
{
	cbor_item_t	*argv[2];
	int		 r = FIDO_ERR_INTERNAL;

	memset(&argv, 0, sizeof(argv));

	if ((argv[0] = fido_blob_encode(&t->id)) == NULL ||
	    (argv[1] = cbor_build_string(t->name)) == NULL) {
		fido_log_debug("%s: cbor encode", __func__);
		goto fail;
	}

	if ((r = bio_tx(dev, CMD_SET_NAME, argv, 2, pin, NULL,
	    ms)) != FIDO_OK ||
	    (r = fido_rx_cbor_status(dev, ms)) != FIDO_OK) {
		fido_log_debug("%s: tx/rx", __func__);
		goto fail;
	}

	r = FIDO_OK;
fail:
	cbor_vector_free(argv, nitems(argv));

	return (r);
}

int
fido_bio_dev_set_template_name(fido_dev_t *dev, const fido_bio_template_t *t,
    const char *pin)
{
	int ms = dev->timeout_ms;

	if (pin == NULL || t->name == NULL)
		return (FIDO_ERR_INVALID_ARGUMENT);

	return (bio_set_template_name_wait(dev, t, pin, &ms));
}

static void
bio_reset_enroll(fido_bio_enroll_t *e)
{
	e->remaining_samples = 0;
	e->last_status = 0;

	if (e->token)
		fido_blob_free(&e->token);
}

static int
bio_parse_enroll_status(const cbor_item_t *key, const cbor_item_t *val,
    void *arg)
{
	fido_bio_enroll_t *e = arg;
	uint64_t x;

	if (cbor_isa_uint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8) {
		fido_log_debug("%s: cbor type", __func__);
		return (0); /* ignore */
	}

	switch (cbor_get_uint8(key)) {
	case 5:
		if (cbor_decode_uint64(val, &x) < 0 || x > UINT8_MAX) {
			fido_log_debug("%s: cbor_decode_uint64", __func__);
			return (-1);
		}
		e->last_status = (uint8_t)x;
		break;
	case 6:
		if (cbor_decode_uint64(val, &x) < 0 || x > UINT8_MAX) {
			fido_log_debug("%s: cbor_decode_uint64", __func__);
			return (-1);
		}
		e->remaining_samples = (uint8_t)x;
		break;
	default:
		return (0); /* ignore */
	}

	return (0);
}

static int
bio_parse_template_id(const cbor_item_t *key, const cbor_item_t *val,
    void *arg)
{
	fido_blob_t *id = arg;

	if (cbor_isa_uint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8 ||
	    cbor_get_uint8(key) != 4) {
		fido_log_debug("%s: cbor type", __func__);
		return (0); /* ignore */
	}

	return (fido_blob_decode(val, id));
}

static int
bio_rx_enroll_begin(fido_dev_t *dev, fido_bio_template_t *t,
    fido_bio_enroll_t *e, int *ms)
{
	unsigned char	reply[FIDO_MAXMSG];
	int		reply_len;
	int		r;

	bio_reset_template(t);

	e->remaining_samples = 0;
	e->last_status = 0;

	if ((reply_len = fido_rx(dev, CTAP_CMD_CBOR, &reply, sizeof(reply),
	    ms)) < 0) {
		fido_log_debug("%s: fido_rx", __func__);
		return (FIDO_ERR_RX);
	}

	if ((r = cbor_parse_reply(reply, (size_t)reply_len, e,
	    bio_parse_enroll_status)) != FIDO_OK) {
		fido_log_debug("%s: bio_parse_enroll_status", __func__);
		return (r);
	}
	if ((r = cbor_parse_reply(reply, (size_t)reply_len, &t->id,
	    bio_parse_template_id)) != FIDO_OK) {
		fido_log_debug("%s: bio_parse_template_id", __func__);
		return (r);
	}

	return (FIDO_OK);
}

static int
bio_enroll_begin_wait(fido_dev_t *dev, fido_bio_template_t *t,
    fido_bio_enroll_t *e, uint32_t timo_ms, int *ms)
{
	cbor_item_t	*argv[3];
	const uint8_t	 cmd = CMD_ENROLL_BEGIN;
	int		 r = FIDO_ERR_INTERNAL;

	memset(&argv, 0, sizeof(argv));

	if ((argv[2] = cbor_build_uint(timo_ms)) == NULL) {
		fido_log_debug("%s: cbor encode", __func__);
		goto fail;
	}

	if ((r = bio_tx(dev, cmd, argv, 3, NULL, e->token, ms)) != FIDO_OK ||
	    (r = bio_rx_enroll_begin(dev, t, e, ms)) != FIDO_OK) {
		fido_log_debug("%s: tx/rx", __func__);
		goto fail;
	}

	r = FIDO_OK;
fail:
	cbor_vector_free(argv, nitems(argv));

	return (r);
}

int
fido_bio_dev_enroll_begin(fido_dev_t *dev, fido_bio_template_t *t,
    fido_bio_enroll_t *e, uint32_t timo_ms, const char *pin)
{
	es256_pk_t	*pk = NULL;
	fido_blob_t	*ecdh = NULL;
	fido_blob_t	*token = NULL;
	int		 ms = dev->timeout_ms;
	int		 r;

	if (pin == NULL || e->token != NULL)
		return (FIDO_ERR_INVALID_ARGUMENT);

	if ((token = fido_blob_new()) == NULL) {
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if ((r = fido_do_ecdh(dev, &pk, &ecdh, &ms)) != FIDO_OK) {
		fido_log_debug("%s: fido_do_ecdh", __func__);
		goto fail;
	}

	if ((r = fido_dev_get_uv_token(dev, CTAP_CBOR_BIO_ENROLL_PRE, pin, ecdh,
	    pk, NULL, token, &ms)) != FIDO_OK) {
		fido_log_debug("%s: fido_dev_get_uv_token", __func__);
		goto fail;
	}

	e->token = token;
	token = NULL;
fail:
	es256_pk_free(&pk);
	fido_blob_free(&ecdh);
	fido_blob_free(&token);

	if (r != FIDO_OK)
		return (r);

	return (bio_enroll_begin_wait(dev, t, e, timo_ms, &ms));
}

static int
bio_rx_enroll_continue(fido_dev_t *dev, fido_bio_enroll_t *e, int *ms)
{
	unsigned char	reply[FIDO_MAXMSG];
	int		reply_len;
	int		r;

	e->remaining_samples = 0;
	e->last_status = 0;

	if ((reply_len = fido_rx(dev, CTAP_CMD_CBOR, &reply, sizeof(reply),
	    ms)) < 0) {
		fido_log_debug("%s: fido_rx", __func__);
		return (FIDO_ERR_RX);
	}

	if ((r = cbor_parse_reply(reply, (size_t)reply_len, e,
	    bio_parse_enroll_status)) != FIDO_OK) {
		fido_log_debug("%s: bio_parse_enroll_status", __func__);
		return (r);
	}

	return (FIDO_OK);
}

static int
bio_enroll_continue_wait(fido_dev_t *dev, const fido_bio_template_t *t,
    fido_bio_enroll_t *e, uint32_t timo_ms, int *ms)
{
	cbor_item_t	*argv[3];
	const uint8_t	 cmd = CMD_ENROLL_NEXT;
	int		 r = FIDO_ERR_INTERNAL;

	memset(&argv, 0, sizeof(argv));

	if ((argv[0] = fido_blob_encode(&t->id)) == NULL ||
	    (argv[2] = cbor_build_uint(timo_ms)) == NULL) {
		fido_log_debug("%s: cbor encode", __func__);
		goto fail;
	}

	if ((r = bio_tx(dev, cmd, argv, 3, NULL, e->token, ms)) != FIDO_OK ||
	    (r = bio_rx_enroll_continue(dev, e, ms)) != FIDO_OK) {
		fido_log_debug("%s: tx/rx", __func__);
		goto fail;
	}

	r = FIDO_OK;
fail:
	cbor_vector_free(argv, nitems(argv));

	return (r);
}

int
fido_bio_dev_enroll_continue(fido_dev_t *dev, const fido_bio_template_t *t,
    fido_bio_enroll_t *e, uint32_t timo_ms)
{
	int ms = dev->timeout_ms;

	if (e->token == NULL)
		return (FIDO_ERR_INVALID_ARGUMENT);

	return (bio_enroll_continue_wait(dev, t, e, timo_ms, &ms));
}

static int
bio_enroll_cancel_wait(fido_dev_t *dev, int *ms)
{
	const uint8_t	cmd = CMD_ENROLL_CANCEL;
	int		r;

	if ((r = bio_tx(dev, cmd, NULL, 0, NULL, NULL, ms)) != FIDO_OK ||
	    (r = fido_rx_cbor_status(dev, ms)) != FIDO_OK) {
		fido_log_debug("%s: tx/rx", __func__);
		return (r);
	}

	return (FIDO_OK);
}

int
fido_bio_dev_enroll_cancel(fido_dev_t *dev)
{
	int ms = dev->timeout_ms;

	return (bio_enroll_cancel_wait(dev, &ms));
}

static int
bio_enroll_remove_wait(fido_dev_t *dev, const fido_bio_template_t *t,
    const char *pin, int *ms)
{
	cbor_item_t	*argv[1];
	const uint8_t	 cmd = CMD_ENROLL_REMOVE;
	int		 r = FIDO_ERR_INTERNAL;

	memset(&argv, 0, sizeof(argv));

	if ((argv[0] = fido_blob_encode(&t->id)) == NULL) {
		fido_log_debug("%s: cbor encode", __func__);
		goto fail;
	}

	if ((r = bio_tx(dev, cmd, argv, 1, pin, NULL, ms)) != FIDO_OK ||
	    (r = fido_rx_cbor_status(dev, ms)) != FIDO_OK) {
		fido_log_debug("%s: tx/rx", __func__);
		goto fail;
	}

	r = FIDO_OK;
fail:
	cbor_vector_free(argv, nitems(argv));

	return (r);
}

int
fido_bio_dev_enroll_remove(fido_dev_t *dev, const fido_bio_template_t *t,
    const char *pin)
{
	int ms = dev->timeout_ms;

	return (bio_enroll_remove_wait(dev, t, pin, &ms));
}

static void
bio_reset_info(fido_bio_info_t *i)
{
	i->type = 0;
	i->max_samples = 0;
}

static int
bio_parse_info(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	fido_bio_info_t	*i = arg;
	uint64_t	 x;

	if (cbor_isa_uint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8) {
		fido_log_debug("%s: cbor type", __func__);
		return (0); /* ignore */
	}

	switch (cbor_get_uint8(key)) {
	case 2:
		if (cbor_decode_uint64(val, &x) < 0 || x > UINT8_MAX) {
			fido_log_debug("%s: cbor_decode_uint64", __func__);
			return (-1);
		}
		i->type = (uint8_t)x;
		break;
	case 3:
		if (cbor_decode_uint64(val, &x) < 0 || x > UINT8_MAX) {
			fido_log_debug("%s: cbor_decode_uint64", __func__);
			return (-1);
		}
		i->max_samples = (uint8_t)x;
		break;
	default:
		return (0); /* ignore */
	}

	return (0);
}

static int
bio_rx_info(fido_dev_t *dev, fido_bio_info_t *i, int *ms)
{
	unsigned char	reply[FIDO_MAXMSG];
	int		reply_len;
	int		r;

	bio_reset_info(i);

	if ((reply_len = fido_rx(dev, CTAP_CMD_CBOR, &reply, sizeof(reply),
	    ms)) < 0) {
		fido_log_debug("%s: fido_rx", __func__);
		return (FIDO_ERR_RX);
	}

	if ((r = cbor_parse_reply(reply, (size_t)reply_len, i,
	    bio_parse_info)) != FIDO_OK) {
		fido_log_debug("%s: bio_parse_info" , __func__);
		return (r);
	}

	return (FIDO_OK);
}

static int
bio_get_info_wait(fido_dev_t *dev, fido_bio_info_t *i, int *ms)
{
	int r;

	if ((r = bio_tx(dev, CMD_GET_INFO, NULL, 0, NULL, NULL,
	    ms)) != FIDO_OK ||
	    (r = bio_rx_info(dev, i, ms)) != FIDO_OK) {
		fido_log_debug("%s: tx/rx", __func__);
		return (r);
	}

	return (FIDO_OK);
}

int
fido_bio_dev_get_info(fido_dev_t *dev, fido_bio_info_t *i)
{
	int ms = dev->timeout_ms;

	return (bio_get_info_wait(dev, i, &ms));
}

const char *
fido_bio_template_name(const fido_bio_template_t *t)
{
	return (t->name);
}

const unsigned char *
fido_bio_template_id_ptr(const fido_bio_template_t *t)
{
	return (t->id.ptr);
}

size_t
fido_bio_template_id_len(const fido_bio_template_t *t)
{
	return (t->id.len);
}

size_t
fido_bio_template_array_count(const fido_bio_template_array_t *ta)
{
	return (ta->n_rx);
}

fido_bio_template_array_t *
fido_bio_template_array_new(void)
{
	return (calloc(1, sizeof(fido_bio_template_array_t)));
}

fido_bio_template_t *
fido_bio_template_new(void)
{
	return (calloc(1, sizeof(fido_bio_template_t)));
}

void
fido_bio_template_array_free(fido_bio_template_array_t **tap)
{
	fido_bio_template_array_t *ta;

	if (tap == NULL || (ta = *tap) == NULL)
		return;

	bio_reset_template_array(ta);
	free(ta);
	*tap = NULL;
}

void
fido_bio_template_free(fido_bio_template_t **tp)
{
	fido_bio_template_t *t;

	if (tp == NULL || (t = *tp) == NULL)
		return;

	bio_reset_template(t);
	free(t);
	*tp = NULL;
}

int
fido_bio_template_set_name(fido_bio_template_t *t, const char *name)
{
	free(t->name);
	t->name = NULL;

	if (name && (t->name = strdup(name)) == NULL)
		return (FIDO_ERR_INTERNAL);

	return (FIDO_OK);
}

int
fido_bio_template_set_id(fido_bio_template_t *t, const unsigned char *ptr,
    size_t len)
{
	fido_blob_reset(&t->id);

	if (ptr && fido_blob_set(&t->id, ptr, len) < 0)
		return (FIDO_ERR_INTERNAL);

	return (FIDO_OK);
}

const fido_bio_template_t *
fido_bio_template(const fido_bio_template_array_t *ta, size_t idx)
{
	if (idx >= ta->n_alloc)
		return (NULL);

	return (&ta->ptr[idx]);
}

fido_bio_enroll_t *
fido_bio_enroll_new(void)
{
	return (calloc(1, sizeof(fido_bio_enroll_t)));
}

fido_bio_info_t *
fido_bio_info_new(void)
{
	return (calloc(1, sizeof(fido_bio_info_t)));
}

uint8_t
fido_bio_info_type(const fido_bio_info_t *i)
{
	return (i->type);
}

uint8_t
fido_bio_info_max_samples(const fido_bio_info_t *i)
{
	return (i->max_samples);
}

void
fido_bio_enroll_free(fido_bio_enroll_t **ep)
{
	fido_bio_enroll_t *e;

	if (ep == NULL || (e = *ep) == NULL)
		return;

	bio_reset_enroll(e);

	free(e);
	*ep = NULL;
}

void
fido_bio_info_free(fido_bio_info_t **ip)
{
	fido_bio_info_t *i;

	if (ip == NULL || (i = *ip) == NULL)
		return;

	free(i);
	*ip = NULL;
}

uint8_t
fido_bio_enroll_remaining_samples(const fido_bio_enroll_t *e)
{
	return (e->remaining_samples);
}

uint8_t
fido_bio_enroll_last_status(const fido_bio_enroll_t *e)
{
	return (e->last_status);
}
