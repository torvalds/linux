/*
 * Copyright (c) 2018-2021 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include "fido.h"

static int
decode_string(const cbor_item_t *item, void *arg)
{
	fido_str_array_t	*a = arg;
	const size_t		 i = a->len;

	/* keep ptr[x] and len consistent */
	if (cbor_string_copy(item, &a->ptr[i]) < 0) {
		fido_log_debug("%s: cbor_string_copy", __func__);
		return (-1);
	}

	a->len++;

	return (0);
}

static int
decode_string_array(const cbor_item_t *item, fido_str_array_t *v)
{
	v->ptr = NULL;
	v->len = 0;

	if (cbor_isa_array(item) == false ||
	    cbor_array_is_definite(item) == false) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	v->ptr = calloc(cbor_array_size(item), sizeof(char *));
	if (v->ptr == NULL)
		return (-1);

	if (cbor_array_iter(item, v, decode_string) < 0) {
		fido_log_debug("%s: decode_string", __func__);
		return (-1);
	}

	return (0);
}

static int
decode_aaguid(const cbor_item_t *item, unsigned char *aaguid, size_t aaguid_len)
{
	if (cbor_isa_bytestring(item) == false ||
	    cbor_bytestring_is_definite(item) == false ||
	    cbor_bytestring_length(item) != aaguid_len) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	memcpy(aaguid, cbor_bytestring_handle(item), aaguid_len);

	return (0);
}

static int
decode_option(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	fido_opt_array_t	*o = arg;
	const size_t		 i = o->len;

	if (cbor_isa_float_ctrl(val) == false ||
	    cbor_float_get_width(val) != CBOR_FLOAT_0 ||
	    cbor_is_bool(val) == false) {
		fido_log_debug("%s: cbor type", __func__);
		return (0); /* ignore */
	}

	if (cbor_string_copy(key, &o->name[i]) < 0) {
		fido_log_debug("%s: cbor_string_copy", __func__);
		return (0); /* ignore */
	}

	/* keep name/value and len consistent */
	o->value[i] = cbor_ctrl_value(val) == CBOR_CTRL_TRUE;
	o->len++;

	return (0);
}

static int
decode_options(const cbor_item_t *item, fido_opt_array_t *o)
{
	o->name = NULL;
	o->value = NULL;
	o->len = 0;

	if (cbor_isa_map(item) == false ||
	    cbor_map_is_definite(item) == false) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	o->name = calloc(cbor_map_size(item), sizeof(char *));
	o->value = calloc(cbor_map_size(item), sizeof(bool));
	if (o->name == NULL || o->value == NULL)
		return (-1);

	return (cbor_map_iter(item, o, decode_option));
}

static int
decode_protocol(const cbor_item_t *item, void *arg)
{
	fido_byte_array_t	*p = arg;
	const size_t		 i = p->len;

	if (cbor_isa_uint(item) == false ||
	    cbor_int_get_width(item) != CBOR_INT_8) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	/* keep ptr[x] and len consistent */
	p->ptr[i] = cbor_get_uint8(item);
	p->len++;

	return (0);
}

static int
decode_protocols(const cbor_item_t *item, fido_byte_array_t *p)
{
	p->ptr = NULL;
	p->len = 0;

	if (cbor_isa_array(item) == false ||
	    cbor_array_is_definite(item) == false) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	p->ptr = calloc(cbor_array_size(item), sizeof(uint8_t));
	if (p->ptr == NULL)
		return (-1);

	if (cbor_array_iter(item, p, decode_protocol) < 0) {
		fido_log_debug("%s: decode_protocol", __func__);
		return (-1);
	}

	return (0);
}

static int
decode_algorithm_entry(const cbor_item_t *key, const cbor_item_t *val,
    void *arg)
{
	fido_algo_t *alg = arg;
	char *name = NULL;
	int ok = -1;

	if (cbor_string_copy(key, &name) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		ok = 0; /* ignore */
		goto out;
	}

	if (!strcmp(name, "alg")) {
		if (cbor_isa_negint(val) == false ||
		    cbor_get_int(val) > INT_MAX || alg->cose != 0) {
			fido_log_debug("%s: alg", __func__);
			goto out;
		}
		alg->cose = -(int)cbor_get_int(val) - 1;
	} else if (!strcmp(name, "type")) {
		if (cbor_string_copy(val, &alg->type) < 0) {
			fido_log_debug("%s: type", __func__);
			goto out;
		}
	}

	ok = 0;
out:
	free(name);

	return (ok);
}

static int
decode_algorithm(const cbor_item_t *item, void *arg)
{
	fido_algo_array_t *aa = arg;
	const size_t i = aa->len;

	if (cbor_isa_map(item) == false ||
	    cbor_map_is_definite(item) == false) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	memset(&aa->ptr[i], 0, sizeof(aa->ptr[i]));

	if (cbor_map_iter(item, &aa->ptr[i], decode_algorithm_entry) < 0) {
		fido_log_debug("%s: decode_algorithm_entry", __func__);
		fido_algo_free(&aa->ptr[i]);
		return (-1);
	}

	/* keep ptr[x] and len consistent */
	aa->len++;

	return (0);
}

static int
decode_algorithms(const cbor_item_t *item, fido_algo_array_t *aa)
{
	aa->ptr = NULL;
	aa->len = 0;

	if (cbor_isa_array(item) == false ||
	    cbor_array_is_definite(item) == false) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	aa->ptr = calloc(cbor_array_size(item), sizeof(fido_algo_t));
	if (aa->ptr == NULL)
		return (-1);

	if (cbor_array_iter(item, aa, decode_algorithm) < 0) {
		fido_log_debug("%s: decode_algorithm", __func__);
		return (-1);
	}

	return (0);
}

static int
parse_reply_element(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	fido_cbor_info_t *ci = arg;

	if (cbor_isa_uint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8) {
		fido_log_debug("%s: cbor type", __func__);
		return (0); /* ignore */
	}

	switch (cbor_get_uint8(key)) {
	case 1: /* versions */
		return (decode_string_array(val, &ci->versions));
	case 2: /* extensions */
		return (decode_string_array(val, &ci->extensions));
	case 3: /* aaguid */
		return (decode_aaguid(val, ci->aaguid, sizeof(ci->aaguid)));
	case 4: /* options */
		return (decode_options(val, &ci->options));
	case 5: /* maxMsgSize */
		return (cbor_decode_uint64(val, &ci->maxmsgsiz));
	case 6: /* pinProtocols */
		return (decode_protocols(val, &ci->protocols));
	case 7: /* maxCredentialCountInList */
		return (cbor_decode_uint64(val, &ci->maxcredcntlst));
	case 8: /* maxCredentialIdLength */
		return (cbor_decode_uint64(val, &ci->maxcredidlen));
	case 9: /* transports */
		return (decode_string_array(val, &ci->transports));
	case 10: /* algorithms */
		return (decode_algorithms(val, &ci->algorithms));
	case 11: /* maxSerializedLargeBlobArray */
		return (cbor_decode_uint64(val, &ci->maxlargeblob));
	case 14: /* fwVersion */
		return (cbor_decode_uint64(val, &ci->fwversion));
	case 15: /* maxCredBlobLen */
		return (cbor_decode_uint64(val, &ci->maxcredbloblen));
	default: /* ignore */
		fido_log_debug("%s: cbor type", __func__);
		return (0);
	}
}

static int
fido_dev_get_cbor_info_tx(fido_dev_t *dev, int *ms)
{
	const unsigned char cbor[] = { CTAP_CBOR_GETINFO };

	fido_log_debug("%s: dev=%p", __func__, (void *)dev);

	if (fido_tx(dev, CTAP_CMD_CBOR, cbor, sizeof(cbor), ms) < 0) {
		fido_log_debug("%s: fido_tx", __func__);
		return (FIDO_ERR_TX);
	}

	return (FIDO_OK);
}

static int
fido_dev_get_cbor_info_rx(fido_dev_t *dev, fido_cbor_info_t *ci, int *ms)
{
	unsigned char	reply[FIDO_MAXMSG];
	int		reply_len;

	fido_log_debug("%s: dev=%p, ci=%p, ms=%d", __func__, (void *)dev,
	    (void *)ci, *ms);

	fido_cbor_info_reset(ci);

	if ((reply_len = fido_rx(dev, CTAP_CMD_CBOR, &reply, sizeof(reply),
	    ms)) < 0) {
		fido_log_debug("%s: fido_rx", __func__);
		return (FIDO_ERR_RX);
	}

	return (cbor_parse_reply(reply, (size_t)reply_len, ci,
	    parse_reply_element));
}

int
fido_dev_get_cbor_info_wait(fido_dev_t *dev, fido_cbor_info_t *ci, int *ms)
{
	int r;

#ifdef USE_WINHELLO
	if (dev->flags & FIDO_DEV_WINHELLO)
		return (fido_winhello_get_cbor_info(dev, ci));
#endif
	if ((r = fido_dev_get_cbor_info_tx(dev, ms)) != FIDO_OK ||
	    (r = fido_dev_get_cbor_info_rx(dev, ci, ms)) != FIDO_OK)
		return (r);

	return (FIDO_OK);
}

int
fido_dev_get_cbor_info(fido_dev_t *dev, fido_cbor_info_t *ci)
{
	int ms = dev->timeout_ms;

	return (fido_dev_get_cbor_info_wait(dev, ci, &ms));
}

/*
 * get/set functions for fido_cbor_info_t; always at the end of the file
 */

fido_cbor_info_t *
fido_cbor_info_new(void)
{
	return (calloc(1, sizeof(fido_cbor_info_t)));
}

void
fido_cbor_info_reset(fido_cbor_info_t *ci)
{
	fido_str_array_free(&ci->versions);
	fido_str_array_free(&ci->extensions);
	fido_str_array_free(&ci->transports);
	fido_opt_array_free(&ci->options);
	fido_byte_array_free(&ci->protocols);
	fido_algo_array_free(&ci->algorithms);
}

void
fido_cbor_info_free(fido_cbor_info_t **ci_p)
{
	fido_cbor_info_t *ci;

	if (ci_p == NULL || (ci = *ci_p) ==  NULL)
		return;
	fido_cbor_info_reset(ci);
	free(ci);
	*ci_p = NULL;
}

char **
fido_cbor_info_versions_ptr(const fido_cbor_info_t *ci)
{
	return (ci->versions.ptr);
}

size_t
fido_cbor_info_versions_len(const fido_cbor_info_t *ci)
{
	return (ci->versions.len);
}

char **
fido_cbor_info_extensions_ptr(const fido_cbor_info_t *ci)
{
	return (ci->extensions.ptr);
}

size_t
fido_cbor_info_extensions_len(const fido_cbor_info_t *ci)
{
	return (ci->extensions.len);
}

char **
fido_cbor_info_transports_ptr(const fido_cbor_info_t *ci)
{
	return (ci->transports.ptr);
}

size_t
fido_cbor_info_transports_len(const fido_cbor_info_t *ci)
{
	return (ci->transports.len);
}

const unsigned char *
fido_cbor_info_aaguid_ptr(const fido_cbor_info_t *ci)
{
	return (ci->aaguid);
}

size_t
fido_cbor_info_aaguid_len(const fido_cbor_info_t *ci)
{
	return (sizeof(ci->aaguid));
}

char **
fido_cbor_info_options_name_ptr(const fido_cbor_info_t *ci)
{
	return (ci->options.name);
}

const bool *
fido_cbor_info_options_value_ptr(const fido_cbor_info_t *ci)
{
	return (ci->options.value);
}

size_t
fido_cbor_info_options_len(const fido_cbor_info_t *ci)
{
	return (ci->options.len);
}

uint64_t
fido_cbor_info_maxcredbloblen(const fido_cbor_info_t *ci)
{
	return (ci->maxcredbloblen);
}

uint64_t
fido_cbor_info_maxmsgsiz(const fido_cbor_info_t *ci)
{
	return (ci->maxmsgsiz);
}

uint64_t
fido_cbor_info_maxcredcntlst(const fido_cbor_info_t *ci)
{
	return (ci->maxcredcntlst);
}

uint64_t
fido_cbor_info_maxcredidlen(const fido_cbor_info_t *ci)
{
	return (ci->maxcredidlen);
}

uint64_t
fido_cbor_info_maxlargeblob(const fido_cbor_info_t *ci)
{
	return (ci->maxlargeblob);
}

uint64_t
fido_cbor_info_fwversion(const fido_cbor_info_t *ci)
{
	return (ci->fwversion);
}

const uint8_t *
fido_cbor_info_protocols_ptr(const fido_cbor_info_t *ci)
{
	return (ci->protocols.ptr);
}

size_t
fido_cbor_info_protocols_len(const fido_cbor_info_t *ci)
{
	return (ci->protocols.len);
}

size_t
fido_cbor_info_algorithm_count(const fido_cbor_info_t *ci)
{
	return (ci->algorithms.len);
}

const char *
fido_cbor_info_algorithm_type(const fido_cbor_info_t *ci, size_t idx)
{
	if (idx >= ci->algorithms.len)
		return (NULL);

	return (ci->algorithms.ptr[idx].type);
}

int
fido_cbor_info_algorithm_cose(const fido_cbor_info_t *ci, size_t idx)
{
	if (idx >= ci->algorithms.len)
		return (0);

	return (ci->algorithms.ptr[idx].cose);
}
