/*
 * Copyright (c) 2018-2021 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <openssl/hmac.h>
#include <openssl/sha.h>
#include "fido.h"

static int
check_key_type(cbor_item_t *item)
{
	if (item->type == CBOR_TYPE_UINT || item->type == CBOR_TYPE_NEGINT ||
	    item->type == CBOR_TYPE_STRING)
		return (0);

	fido_log_debug("%s: invalid type: %d", __func__, item->type);

	return (-1);
}

/*
 * Validate CTAP2 canonical CBOR encoding rules for maps.
 */
static int
ctap_check_cbor(cbor_item_t *prev, cbor_item_t *curr)
{
	size_t	curr_len;
	size_t	prev_len;

	if (check_key_type(prev) < 0 || check_key_type(curr) < 0)
		return (-1);

	if (prev->type != curr->type) {
		if (prev->type < curr->type)
			return (0);
		fido_log_debug("%s: unsorted types", __func__);
		return (-1);
	}

	if (curr->type == CBOR_TYPE_UINT || curr->type == CBOR_TYPE_NEGINT) {
		if (cbor_int_get_width(curr) >= cbor_int_get_width(prev) &&
		    cbor_get_int(curr) > cbor_get_int(prev))
			return (0);
	} else {
		curr_len = cbor_string_length(curr);
		prev_len = cbor_string_length(prev);

		if (curr_len > prev_len || (curr_len == prev_len &&
		    memcmp(cbor_string_handle(prev), cbor_string_handle(curr),
		    curr_len) < 0))
			return (0);
	}

	fido_log_debug("%s: invalid cbor", __func__);

	return (-1);
}

int
cbor_map_iter(const cbor_item_t *item, void *arg, int(*f)(const cbor_item_t *,
    const cbor_item_t *, void *))
{
	struct cbor_pair	*v;
	size_t			 n;

	if ((v = cbor_map_handle(item)) == NULL) {
		fido_log_debug("%s: cbor_map_handle", __func__);
		return (-1);
	}

	n = cbor_map_size(item);

	for (size_t i = 0; i < n; i++) {
		if (v[i].key == NULL || v[i].value == NULL) {
			fido_log_debug("%s: key=%p, value=%p for i=%zu",
			    __func__, (void *)v[i].key, (void *)v[i].value, i);
			return (-1);
		}
		if (i && ctap_check_cbor(v[i - 1].key, v[i].key) < 0) {
			fido_log_debug("%s: ctap_check_cbor", __func__);
			return (-1);
		}
		if (f(v[i].key, v[i].value, arg) < 0) {
			fido_log_debug("%s: iterator < 0 on i=%zu", __func__,
			    i);
			return (-1);
		}
	}

	return (0);
}

int
cbor_array_iter(const cbor_item_t *item, void *arg, int(*f)(const cbor_item_t *,
    void *))
{
	cbor_item_t	**v;
	size_t		  n;

	if ((v = cbor_array_handle(item)) == NULL) {
		fido_log_debug("%s: cbor_array_handle", __func__);
		return (-1);
	}

	n = cbor_array_size(item);

	for (size_t i = 0; i < n; i++)
		if (v[i] == NULL || f(v[i], arg) < 0) {
			fido_log_debug("%s: iterator < 0 on i=%zu,%p",
			    __func__, i, (void *)v[i]);
			return (-1);
		}

	return (0);
}

int
cbor_parse_reply(const unsigned char *blob, size_t blob_len, void *arg,
    int(*parser)(const cbor_item_t *, const cbor_item_t *, void *))
{
	cbor_item_t		*item = NULL;
	struct cbor_load_result	 cbor;
	int			 r;

	if (blob_len < 1) {
		fido_log_debug("%s: blob_len=%zu", __func__, blob_len);
		r = FIDO_ERR_RX;
		goto fail;
	}

	if (blob[0] != FIDO_OK) {
		fido_log_debug("%s: blob[0]=0x%02x", __func__, blob[0]);
		r = blob[0];
		goto fail;
	}

	if ((item = cbor_load(blob + 1, blob_len - 1, &cbor)) == NULL) {
		fido_log_debug("%s: cbor_load", __func__);
		r = FIDO_ERR_RX_NOT_CBOR;
		goto fail;
	}

	if (cbor_isa_map(item) == false ||
	    cbor_map_is_definite(item) == false) {
		fido_log_debug("%s: cbor type", __func__);
		r = FIDO_ERR_RX_INVALID_CBOR;
		goto fail;
	}

	if (cbor_map_iter(item, arg, parser) < 0) {
		fido_log_debug("%s: cbor_map_iter", __func__);
		r = FIDO_ERR_RX_INVALID_CBOR;
		goto fail;
	}

	r = FIDO_OK;
fail:
	if (item != NULL)
		cbor_decref(&item);

	return (r);
}

void
cbor_vector_free(cbor_item_t **item, size_t len)
{
	for (size_t i = 0; i < len; i++)
		if (item[i] != NULL)
			cbor_decref(&item[i]);
}

int
cbor_bytestring_copy(const cbor_item_t *item, unsigned char **buf, size_t *len)
{
	if (*buf != NULL || *len != 0) {
		fido_log_debug("%s: dup", __func__);
		return (-1);
	}

	if (cbor_isa_bytestring(item) == false ||
	    cbor_bytestring_is_definite(item) == false) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	*len = cbor_bytestring_length(item);
	if ((*buf = malloc(*len)) == NULL) {
		*len = 0;
		return (-1);
	}

	memcpy(*buf, cbor_bytestring_handle(item), *len);

	return (0);
}

int
cbor_string_copy(const cbor_item_t *item, char **str)
{
	size_t len;

	if (*str != NULL) {
		fido_log_debug("%s: dup", __func__);
		return (-1);
	}

	if (cbor_isa_string(item) == false ||
	    cbor_string_is_definite(item) == false) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	if ((len = cbor_string_length(item)) == SIZE_MAX ||
	    (*str = malloc(len + 1)) == NULL)
		return (-1);

	memcpy(*str, cbor_string_handle(item), len);
	(*str)[len] = '\0';

	return (0);
}

int
cbor_add_bytestring(cbor_item_t *item, const char *key,
    const unsigned char *value, size_t value_len)
{
	struct cbor_pair pair;
	int ok = -1;

	memset(&pair, 0, sizeof(pair));

	if ((pair.key = cbor_build_string(key)) == NULL ||
	    (pair.value = cbor_build_bytestring(value, value_len)) == NULL) {
		fido_log_debug("%s: cbor_build", __func__);
		goto fail;
	}

	if (!cbor_map_add(item, pair)) {
		fido_log_debug("%s: cbor_map_add", __func__);
		goto fail;
	}

	ok = 0;
fail:
	if (pair.key)
		cbor_decref(&pair.key);
	if (pair.value)
		cbor_decref(&pair.value);

	return (ok);
}

int
cbor_add_string(cbor_item_t *item, const char *key, const char *value)
{
	struct cbor_pair pair;
	int ok = -1;

	memset(&pair, 0, sizeof(pair));

	if ((pair.key = cbor_build_string(key)) == NULL ||
	    (pair.value = cbor_build_string(value)) == NULL) {
		fido_log_debug("%s: cbor_build", __func__);
		goto fail;
	}

	if (!cbor_map_add(item, pair)) {
		fido_log_debug("%s: cbor_map_add", __func__);
		goto fail;
	}

	ok = 0;
fail:
	if (pair.key)
		cbor_decref(&pair.key);
	if (pair.value)
		cbor_decref(&pair.value);

	return (ok);
}

int
cbor_add_bool(cbor_item_t *item, const char *key, fido_opt_t value)
{
	struct cbor_pair pair;
	int ok = -1;

	memset(&pair, 0, sizeof(pair));

	if ((pair.key = cbor_build_string(key)) == NULL ||
	    (pair.value = cbor_build_bool(value == FIDO_OPT_TRUE)) == NULL) {
		fido_log_debug("%s: cbor_build", __func__);
		goto fail;
	}

	if (!cbor_map_add(item, pair)) {
		fido_log_debug("%s: cbor_map_add", __func__);
		goto fail;
	}

	ok = 0;
fail:
	if (pair.key)
		cbor_decref(&pair.key);
	if (pair.value)
		cbor_decref(&pair.value);

	return (ok);
}

static int
cbor_add_uint8(cbor_item_t *item, const char *key, uint8_t value)
{
	struct cbor_pair pair;
	int ok = -1;

	memset(&pair, 0, sizeof(pair));

	if ((pair.key = cbor_build_string(key)) == NULL ||
	    (pair.value = cbor_build_uint8(value)) == NULL) {
		fido_log_debug("%s: cbor_build", __func__);
		goto fail;
	}

	if (!cbor_map_add(item, pair)) {
		fido_log_debug("%s: cbor_map_add", __func__);
		goto fail;
	}

	ok = 0;
fail:
	if (pair.key)
		cbor_decref(&pair.key);
	if (pair.value)
		cbor_decref(&pair.value);

	return (ok);
}

static int
cbor_add_arg(cbor_item_t *item, uint8_t n, cbor_item_t *arg)
{
	struct cbor_pair pair;
	int ok = -1;

	memset(&pair, 0, sizeof(pair));

	if (arg == NULL)
		return (0); /* empty argument */

	if ((pair.key = cbor_build_uint8(n)) == NULL) {
		fido_log_debug("%s: cbor_build", __func__);
		goto fail;
	}

	pair.value = arg;

	if (!cbor_map_add(item, pair)) {
		fido_log_debug("%s: cbor_map_add", __func__);
		goto fail;
	}

	ok = 0;
fail:
	if (pair.key)
		cbor_decref(&pair.key);

	return (ok);
}

cbor_item_t *
cbor_flatten_vector(cbor_item_t *argv[], size_t argc)
{
	cbor_item_t	*map;
	uint8_t		 i;

	if (argc > UINT8_MAX - 1)
		return (NULL);

	if ((map = cbor_new_definite_map(argc)) == NULL)
		return (NULL);

	for (i = 0; i < argc; i++)
		if (cbor_add_arg(map, (uint8_t)(i + 1), argv[i]) < 0)
			break;

	if (i != argc) {
		cbor_decref(&map);
		map = NULL;
	}

	return (map);
}

int
cbor_build_frame(uint8_t cmd, cbor_item_t *argv[], size_t argc, fido_blob_t *f)
{
	cbor_item_t	*flat = NULL;
	unsigned char	*cbor = NULL;
	size_t		 cbor_len;
	size_t		 cbor_alloc_len;
	int		 ok = -1;

	if ((flat = cbor_flatten_vector(argv, argc)) == NULL)
		goto fail;

	cbor_len = cbor_serialize_alloc(flat, &cbor, &cbor_alloc_len);
	if (cbor_len == 0 || cbor_len == SIZE_MAX) {
		fido_log_debug("%s: cbor_len=%zu", __func__, cbor_len);
		goto fail;
	}

	if ((f->ptr = malloc(cbor_len + 1)) == NULL)
		goto fail;

	f->len = cbor_len + 1;
	f->ptr[0] = cmd;
	memcpy(f->ptr + 1, cbor, f->len - 1);

	ok = 0;
fail:
	if (flat != NULL)
		cbor_decref(&flat);

	free(cbor);

	return (ok);
}

cbor_item_t *
cbor_encode_rp_entity(const fido_rp_t *rp)
{
	cbor_item_t *item = NULL;

	if ((item = cbor_new_definite_map(2)) == NULL)
		return (NULL);

	if ((rp->id && cbor_add_string(item, "id", rp->id) < 0) ||
	    (rp->name && cbor_add_string(item, "name", rp->name) < 0)) {
		cbor_decref(&item);
		return (NULL);
	}

	return (item);
}

cbor_item_t *
cbor_encode_user_entity(const fido_user_t *user)
{
	cbor_item_t		*item = NULL;
	const fido_blob_t	*id = &user->id;
	const char		*display = user->display_name;

	if ((item = cbor_new_definite_map(4)) == NULL)
		return (NULL);

	if ((id->ptr && cbor_add_bytestring(item, "id", id->ptr, id->len) < 0) ||
	    (user->icon && cbor_add_string(item, "icon", user->icon) < 0) ||
	    (user->name && cbor_add_string(item, "name", user->name) < 0) ||
	    (display && cbor_add_string(item, "displayName", display) < 0)) {
		cbor_decref(&item);
		return (NULL);
	}

	return (item);
}

cbor_item_t *
cbor_encode_pubkey_param(int cose_alg)
{
	cbor_item_t		*item = NULL;
	cbor_item_t		*body = NULL;
	struct cbor_pair	 alg;
	int			 ok = -1;

	memset(&alg, 0, sizeof(alg));

	if ((item = cbor_new_definite_array(1)) == NULL ||
	    (body = cbor_new_definite_map(2)) == NULL ||
	    cose_alg > -1 || cose_alg < INT16_MIN)
		goto fail;

	alg.key = cbor_build_string("alg");

	if (-cose_alg - 1 > UINT8_MAX)
		alg.value = cbor_build_negint16((uint16_t)(-cose_alg - 1));
	else
		alg.value = cbor_build_negint8((uint8_t)(-cose_alg - 1));

	if (alg.key == NULL || alg.value == NULL) {
		fido_log_debug("%s: cbor_build", __func__);
		goto fail;
	}

	if (cbor_map_add(body, alg) == false ||
	    cbor_add_string(body, "type", "public-key") < 0 ||
	    cbor_array_push(item, body) == false)
		goto fail;

	ok  = 0;
fail:
	if (ok < 0) {
		if (item != NULL) {
			cbor_decref(&item);
			item = NULL;
		}
	}

	if (body != NULL)
		cbor_decref(&body);
	if (alg.key != NULL)
		cbor_decref(&alg.key);
	if (alg.value != NULL)
		cbor_decref(&alg.value);

	return (item);
}

cbor_item_t *
cbor_encode_pubkey(const fido_blob_t *pubkey)
{
	cbor_item_t *cbor_key = NULL;

	if ((cbor_key = cbor_new_definite_map(2)) == NULL ||
	    cbor_add_bytestring(cbor_key, "id", pubkey->ptr, pubkey->len) < 0 ||
	    cbor_add_string(cbor_key, "type", "public-key") < 0) {
		if (cbor_key)
			cbor_decref(&cbor_key);
		return (NULL);
	}

	return (cbor_key);
}

cbor_item_t *
cbor_encode_pubkey_list(const fido_blob_array_t *list)
{
	cbor_item_t	*array = NULL;
	cbor_item_t	*key = NULL;

	if ((array = cbor_new_definite_array(list->len)) == NULL)
		goto fail;

	for (size_t i = 0; i < list->len; i++) {
		if ((key = cbor_encode_pubkey(&list->ptr[i])) == NULL ||
		    cbor_array_push(array, key) == false)
			goto fail;
		cbor_decref(&key);
	}

	return (array);
fail:
	if (key != NULL)
		cbor_decref(&key);
	if (array != NULL)
		cbor_decref(&array);

	return (NULL);
}

cbor_item_t *
cbor_encode_str_array(const fido_str_array_t *a)
{
	cbor_item_t	*array = NULL;
	cbor_item_t	*entry = NULL;

	if ((array = cbor_new_definite_array(a->len)) == NULL)
		goto fail;

	for (size_t i = 0; i < a->len; i++) {
		if ((entry = cbor_build_string(a->ptr[i])) == NULL ||
		    cbor_array_push(array, entry) == false)
			goto fail;
		cbor_decref(&entry);
	}

	return (array);
fail:
	if (entry != NULL)
		cbor_decref(&entry);
	if (array != NULL)
		cbor_decref(&array);

	return (NULL);
}

static int
cbor_encode_largeblob_key_ext(cbor_item_t *map)
{
	if (map == NULL ||
	    cbor_add_bool(map, "largeBlobKey", FIDO_OPT_TRUE) < 0)
		return (-1);

	return (0);
}

cbor_item_t *
cbor_encode_cred_ext(const fido_cred_ext_t *ext, const fido_blob_t *blob)
{
	cbor_item_t *item = NULL;
	size_t size = 0;

	if (ext->mask & FIDO_EXT_CRED_BLOB)
		size++;
	if (ext->mask & FIDO_EXT_HMAC_SECRET)
		size++;
	if (ext->mask & FIDO_EXT_CRED_PROTECT)
		size++;
	if (ext->mask & FIDO_EXT_LARGEBLOB_KEY)
		size++;
	if (ext->mask & FIDO_EXT_MINPINLEN)
		size++;

	if (size == 0 || (item = cbor_new_definite_map(size)) == NULL)
		return (NULL);

	if (ext->mask & FIDO_EXT_CRED_BLOB) {
		if (cbor_add_bytestring(item, "credBlob", blob->ptr,
		    blob->len) < 0) {
			cbor_decref(&item);
			return (NULL);
		}
	}
	if (ext->mask & FIDO_EXT_CRED_PROTECT) {
		if (ext->prot < 0 || ext->prot > UINT8_MAX ||
		    cbor_add_uint8(item, "credProtect",
		    (uint8_t)ext->prot) < 0) {
			cbor_decref(&item);
			return (NULL);
		}
	}
	if (ext->mask & FIDO_EXT_HMAC_SECRET) {
		if (cbor_add_bool(item, "hmac-secret", FIDO_OPT_TRUE) < 0) {
			cbor_decref(&item);
			return (NULL);
		}
	}
	if (ext->mask & FIDO_EXT_LARGEBLOB_KEY) {
		if (cbor_encode_largeblob_key_ext(item) < 0) {
			cbor_decref(&item);
			return (NULL);
		}
	}
	if (ext->mask & FIDO_EXT_MINPINLEN) {
		if (cbor_add_bool(item, "minPinLength", FIDO_OPT_TRUE) < 0) {
			cbor_decref(&item);
			return (NULL);
		}
	}

	return (item);
}

cbor_item_t *
cbor_encode_cred_opt(fido_opt_t rk, fido_opt_t uv)
{
	cbor_item_t *item = NULL;

	if ((item = cbor_new_definite_map(2)) == NULL)
		return (NULL);
	if ((rk != FIDO_OPT_OMIT && cbor_add_bool(item, "rk", rk) < 0) ||
	    (uv != FIDO_OPT_OMIT && cbor_add_bool(item, "uv", uv) < 0)) {
		cbor_decref(&item);
		return (NULL);
	}

	return (item);
}

cbor_item_t *
cbor_encode_assert_opt(fido_opt_t up, fido_opt_t uv)
{
	cbor_item_t *item = NULL;

	if ((item = cbor_new_definite_map(2)) == NULL)
		return (NULL);
	if ((up != FIDO_OPT_OMIT && cbor_add_bool(item, "up", up) < 0) ||
	    (uv != FIDO_OPT_OMIT && cbor_add_bool(item, "uv", uv) < 0)) {
		cbor_decref(&item);
		return (NULL);
	}

	return (item);
}

cbor_item_t *
cbor_encode_pin_auth(const fido_dev_t *dev, const fido_blob_t *secret,
    const fido_blob_t *data)
{
	const EVP_MD	*md = NULL;
	unsigned char	 dgst[SHA256_DIGEST_LENGTH];
	unsigned int	 dgst_len;
	size_t		 outlen;
	uint8_t		 prot;
	fido_blob_t	 key;

	key.ptr = secret->ptr;
	key.len = secret->len;

	if ((prot = fido_dev_get_pin_protocol(dev)) == 0) {
		fido_log_debug("%s: fido_dev_get_pin_protocol", __func__);
		return (NULL);
	}

	/* select hmac portion of the shared secret */
	if (prot == CTAP_PIN_PROTOCOL2 && key.len > 32)
		key.len = 32;

	if ((md = EVP_sha256()) == NULL || HMAC(md, key.ptr,
	    (int)key.len, data->ptr, data->len, dgst,
	    &dgst_len) == NULL || dgst_len != SHA256_DIGEST_LENGTH)
		return (NULL);

	outlen = (prot == CTAP_PIN_PROTOCOL1) ? 16 : dgst_len;

	return (cbor_build_bytestring(dgst, outlen));
}

cbor_item_t *
cbor_encode_pin_opt(const fido_dev_t *dev)
{
	uint8_t	    prot;

	if ((prot = fido_dev_get_pin_protocol(dev)) == 0) {
		fido_log_debug("%s: fido_dev_get_pin_protocol", __func__);
		return (NULL);
	}

	return (cbor_build_uint8(prot));
}

cbor_item_t *
cbor_encode_change_pin_auth(const fido_dev_t *dev, const fido_blob_t *secret,
    const fido_blob_t *new_pin_enc, const fido_blob_t *pin_hash_enc)
{
	unsigned char	 dgst[SHA256_DIGEST_LENGTH];
	unsigned int	 dgst_len;
	cbor_item_t	*item = NULL;
	const EVP_MD	*md = NULL;
	HMAC_CTX	*ctx = NULL;
	fido_blob_t	 key;
	uint8_t		 prot;
	size_t		 outlen;

	key.ptr = secret->ptr;
	key.len = secret->len;

	if ((prot = fido_dev_get_pin_protocol(dev)) == 0) {
		fido_log_debug("%s: fido_dev_get_pin_protocol", __func__);
		goto fail;
	}

	if (prot == CTAP_PIN_PROTOCOL2 && key.len > 32)
		key.len = 32;

	if ((ctx = HMAC_CTX_new()) == NULL ||
	    (md = EVP_sha256())  == NULL ||
	    HMAC_Init_ex(ctx, key.ptr, (int)key.len, md, NULL) == 0 ||
	    HMAC_Update(ctx, new_pin_enc->ptr, new_pin_enc->len) == 0 ||
	    HMAC_Update(ctx, pin_hash_enc->ptr, pin_hash_enc->len) == 0 ||
	    HMAC_Final(ctx, dgst, &dgst_len) == 0 ||
	    dgst_len != SHA256_DIGEST_LENGTH) {
		fido_log_debug("%s: HMAC", __func__);
		goto fail;
	}

	outlen = (prot == CTAP_PIN_PROTOCOL1) ? 16 : dgst_len;

	if ((item = cbor_build_bytestring(dgst, outlen)) == NULL) {
		fido_log_debug("%s: cbor_build_bytestring", __func__);
		goto fail;
	}

fail:
	HMAC_CTX_free(ctx);

	return (item);
}

static int
cbor_encode_hmac_secret_param(const fido_dev_t *dev, cbor_item_t *item,
    const fido_blob_t *ecdh, const es256_pk_t *pk, const fido_blob_t *salt)
{
	cbor_item_t		*param = NULL;
	cbor_item_t		*argv[4];
	struct cbor_pair	 pair;
	fido_blob_t		*enc = NULL;
	uint8_t			 prot;
	int			 r;

	memset(argv, 0, sizeof(argv));
	memset(&pair, 0, sizeof(pair));

	if (item == NULL || ecdh == NULL || pk == NULL || salt->ptr == NULL) {
		fido_log_debug("%s: ecdh=%p, pk=%p, salt->ptr=%p", __func__,
		    (const void *)ecdh, (const void *)pk,
		    (const void *)salt->ptr);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if (salt->len != 32 && salt->len != 64) {
		fido_log_debug("%s: salt->len=%zu", __func__, salt->len);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if ((enc = fido_blob_new()) == NULL ||
	    aes256_cbc_enc(dev, ecdh, salt, enc) < 0) {
		fido_log_debug("%s: aes256_cbc_enc", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if ((prot = fido_dev_get_pin_protocol(dev)) == 0) {
		fido_log_debug("%s: fido_dev_get_pin_protocol", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	/* XXX not pin, but salt */
	if ((argv[0] = es256_pk_encode(pk, 1)) == NULL ||
	    (argv[1] = fido_blob_encode(enc)) == NULL ||
	    (argv[2] = cbor_encode_pin_auth(dev, ecdh, enc)) == NULL ||
	    (prot != 1 && (argv[3] = cbor_build_uint8(prot)) == NULL)) {
		fido_log_debug("%s: cbor encode", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if ((param = cbor_flatten_vector(argv, nitems(argv))) == NULL) {
		fido_log_debug("%s: cbor_flatten_vector", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if ((pair.key = cbor_build_string("hmac-secret")) == NULL) {
		fido_log_debug("%s: cbor_build", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	pair.value = param;

	if (!cbor_map_add(item, pair)) {
		fido_log_debug("%s: cbor_map_add", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	r = FIDO_OK;

fail:
	cbor_vector_free(argv, nitems(argv));

	if (param != NULL)
		cbor_decref(&param);
	if (pair.key != NULL)
		cbor_decref(&pair.key);

	fido_blob_free(&enc);

	return (r);
}

cbor_item_t *
cbor_encode_assert_ext(fido_dev_t *dev, const fido_assert_ext_t *ext,
    const fido_blob_t *ecdh, const es256_pk_t *pk)
{
	cbor_item_t *item = NULL;
	size_t size = 0;

	if (ext->mask & FIDO_EXT_CRED_BLOB)
		size++;
	if (ext->mask & FIDO_EXT_HMAC_SECRET)
		size++;
	if (ext->mask & FIDO_EXT_LARGEBLOB_KEY)
		size++;
	if (size == 0 || (item = cbor_new_definite_map(size)) == NULL)
		return (NULL);

	if (ext->mask & FIDO_EXT_CRED_BLOB) {
		if (cbor_add_bool(item, "credBlob", FIDO_OPT_TRUE) < 0) {
			cbor_decref(&item);
			return (NULL);
		}
	}
	if (ext->mask & FIDO_EXT_HMAC_SECRET) {
		if (cbor_encode_hmac_secret_param(dev, item, ecdh, pk,
		    &ext->hmac_salt) < 0) {
			cbor_decref(&item);
			return (NULL);
		}
	}
	if (ext->mask & FIDO_EXT_LARGEBLOB_KEY) {
		if (cbor_encode_largeblob_key_ext(item) < 0) {
			cbor_decref(&item);
			return (NULL);
		}
	}

	return (item);
}

int
cbor_decode_fmt(const cbor_item_t *item, char **fmt)
{
	char	*type = NULL;

	if (cbor_string_copy(item, &type) < 0) {
		fido_log_debug("%s: cbor_string_copy", __func__);
		return (-1);
	}

	if (strcmp(type, "packed") && strcmp(type, "fido-u2f") &&
	    strcmp(type, "none") && strcmp(type, "tpm")) {
		fido_log_debug("%s: type=%s", __func__, type);
		free(type);
		return (-1);
	}

	*fmt = type;

	return (0);
}

struct cose_key {
	int kty;
	int alg;
	int crv;
};

static int
find_cose_alg(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	struct cose_key *cose_key = arg;

	if (cbor_isa_uint(key) == true &&
	    cbor_int_get_width(key) == CBOR_INT_8) {
		switch (cbor_get_uint8(key)) {
		case 1:
			if (cbor_isa_uint(val) == false ||
			    cbor_get_int(val) > INT_MAX || cose_key->kty != 0) {
				fido_log_debug("%s: kty", __func__);
				return (-1);
			}

			cose_key->kty = (int)cbor_get_int(val);

			break;
		case 3:
			if (cbor_isa_negint(val) == false ||
			    cbor_get_int(val) > INT_MAX || cose_key->alg != 0) {
				fido_log_debug("%s: alg", __func__);
				return (-1);
			}

			cose_key->alg = -(int)cbor_get_int(val) - 1;

			break;
		}
	} else if (cbor_isa_negint(key) == true &&
	    cbor_int_get_width(key) == CBOR_INT_8) {
		if (cbor_get_uint8(key) == 0) {
			/* get crv if not rsa, otherwise ignore */
			if (cbor_isa_uint(val) == true &&
			    cbor_get_int(val) <= INT_MAX &&
			    cose_key->crv == 0)
				cose_key->crv = (int)cbor_get_int(val);
		}
	}

	return (0);
}

static int
get_cose_alg(const cbor_item_t *item, int *cose_alg)
{
	struct cose_key cose_key;

	memset(&cose_key, 0, sizeof(cose_key));

	*cose_alg = 0;

	if (cbor_isa_map(item) == false ||
	    cbor_map_is_definite(item) == false ||
	    cbor_map_iter(item, &cose_key, find_cose_alg) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	switch (cose_key.alg) {
	case COSE_ES256:
		if (cose_key.kty != COSE_KTY_EC2 ||
		    cose_key.crv != COSE_P256) {
			fido_log_debug("%s: invalid kty/crv", __func__);
			return (-1);
		}

		break;
	case COSE_EDDSA:
		if (cose_key.kty != COSE_KTY_OKP ||
		    cose_key.crv != COSE_ED25519) {
			fido_log_debug("%s: invalid kty/crv", __func__);
			return (-1);
		}

		break;
	case COSE_RS256:
		if (cose_key.kty != COSE_KTY_RSA) {
			fido_log_debug("%s: invalid kty/crv", __func__);
			return (-1);
		}

		break;
	default:
		fido_log_debug("%s: unknown alg %d", __func__, cose_key.alg);

		return (-1);
	}

	*cose_alg = cose_key.alg;

	return (0);
}

int
cbor_decode_pubkey(const cbor_item_t *item, int *type, void *key)
{
	if (get_cose_alg(item, type) < 0) {
		fido_log_debug("%s: get_cose_alg", __func__);
		return (-1);
	}

	switch (*type) {
	case COSE_ES256:
		if (es256_pk_decode(item, key) < 0) {
			fido_log_debug("%s: es256_pk_decode", __func__);
			return (-1);
		}
		break;
	case COSE_RS256:
		if (rs256_pk_decode(item, key) < 0) {
			fido_log_debug("%s: rs256_pk_decode", __func__);
			return (-1);
		}
		break;
	case COSE_EDDSA:
		if (eddsa_pk_decode(item, key) < 0) {
			fido_log_debug("%s: eddsa_pk_decode", __func__);
			return (-1);
		}
		break;
	default:
		fido_log_debug("%s: invalid cose_alg %d", __func__, *type);
		return (-1);
	}

	return (0);
}

static int
decode_attcred(const unsigned char **buf, size_t *len, int cose_alg,
    fido_attcred_t *attcred)
{
	cbor_item_t		*item = NULL;
	struct cbor_load_result	 cbor;
	uint16_t		 id_len;
	int			 ok = -1;

	fido_log_xxd(*buf, *len, "%s", __func__);

	if (fido_buf_read(buf, len, &attcred->aaguid,
	    sizeof(attcred->aaguid)) < 0) {
		fido_log_debug("%s: fido_buf_read aaguid", __func__);
		return (-1);
	}

	if (fido_buf_read(buf, len, &id_len, sizeof(id_len)) < 0) {
		fido_log_debug("%s: fido_buf_read id_len", __func__);
		return (-1);
	}

	attcred->id.len = (size_t)be16toh(id_len);
	if ((attcred->id.ptr = malloc(attcred->id.len)) == NULL)
		return (-1);

	fido_log_debug("%s: attcred->id.len=%zu", __func__, attcred->id.len);

	if (fido_buf_read(buf, len, attcred->id.ptr, attcred->id.len) < 0) {
		fido_log_debug("%s: fido_buf_read id", __func__);
		return (-1);
	}

	if ((item = cbor_load(*buf, *len, &cbor)) == NULL) {
		fido_log_debug("%s: cbor_load", __func__);
		goto fail;
	}

	if (cbor_decode_pubkey(item, &attcred->type, &attcred->pubkey) < 0) {
		fido_log_debug("%s: cbor_decode_pubkey", __func__);
		goto fail;
	}

	if (attcred->type != cose_alg) {
		fido_log_debug("%s: cose_alg mismatch (%d != %d)", __func__,
		    attcred->type, cose_alg);
		goto fail;
	}

	*buf += cbor.read;
	*len -= cbor.read;

	ok = 0;
fail:
	if (item != NULL)
		cbor_decref(&item);

	return (ok);
}

static int
decode_cred_extension(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	fido_cred_ext_t	*authdata_ext = arg;
	char		*type = NULL;
	int		 ok = -1;

	if (cbor_string_copy(key, &type) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		ok = 0; /* ignore */
		goto out;
	}

	if (strcmp(type, "hmac-secret") == 0) {
		if (cbor_isa_float_ctrl(val) == false ||
		    cbor_float_get_width(val) != CBOR_FLOAT_0 ||
		    cbor_is_bool(val) == false) {
			fido_log_debug("%s: cbor type", __func__);
			goto out;
		}
		if (cbor_ctrl_value(val) == CBOR_CTRL_TRUE)
			authdata_ext->mask |= FIDO_EXT_HMAC_SECRET;
	} else if (strcmp(type, "credProtect") == 0) {
		if (cbor_isa_uint(val) == false ||
		    cbor_int_get_width(val) != CBOR_INT_8) {
			fido_log_debug("%s: cbor type", __func__);
			goto out;
		}
		authdata_ext->mask |= FIDO_EXT_CRED_PROTECT;
		authdata_ext->prot = cbor_get_uint8(val);
	} else if (strcmp(type, "credBlob") == 0) {
		if (cbor_isa_float_ctrl(val) == false ||
		    cbor_float_get_width(val) != CBOR_FLOAT_0 ||
		    cbor_is_bool(val) == false) {
			fido_log_debug("%s: cbor type", __func__);
			goto out;
		}
		if (cbor_ctrl_value(val) == CBOR_CTRL_TRUE)
			authdata_ext->mask |= FIDO_EXT_CRED_BLOB;
	} else if (strcmp(type, "minPinLength") == 0) {
		if (cbor_isa_uint(val) == false ||
		    cbor_int_get_width(val) != CBOR_INT_8) {
			fido_log_debug("%s: cbor type", __func__);
			goto out;
		}
		authdata_ext->mask |= FIDO_EXT_MINPINLEN;
		authdata_ext->minpinlen = cbor_get_uint8(val);
	}

	ok = 0;
out:
	free(type);

	return (ok);
}

static int
decode_cred_extensions(const unsigned char **buf, size_t *len,
    fido_cred_ext_t *authdata_ext)
{
	cbor_item_t		*item = NULL;
	struct cbor_load_result	 cbor;
	int			 ok = -1;

	memset(authdata_ext, 0, sizeof(*authdata_ext));

	fido_log_xxd(*buf, *len, "%s", __func__);

	if ((item = cbor_load(*buf, *len, &cbor)) == NULL) {
		fido_log_debug("%s: cbor_load", __func__);
		goto fail;
	}

	if (cbor_isa_map(item) == false ||
	    cbor_map_is_definite(item) == false ||
	    cbor_map_iter(item, authdata_ext, decode_cred_extension) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		goto fail;
	}

	*buf += cbor.read;
	*len -= cbor.read;

	ok = 0;
fail:
	if (item != NULL)
		cbor_decref(&item);

	return (ok);
}

static int
decode_assert_extension(const cbor_item_t *key, const cbor_item_t *val,
    void *arg)
{
	fido_assert_extattr_t	*authdata_ext = arg;
	char			*type = NULL;
	int			 ok = -1;

	if (cbor_string_copy(key, &type) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		ok = 0; /* ignore */
		goto out;
	}

	if (strcmp(type, "hmac-secret") == 0) {
		if (fido_blob_decode(val, &authdata_ext->hmac_secret_enc) < 0) {
			fido_log_debug("%s: fido_blob_decode", __func__);
			goto out;
		}
		authdata_ext->mask |= FIDO_EXT_HMAC_SECRET;
	} else if (strcmp(type, "credBlob") == 0) {
		if (fido_blob_decode(val, &authdata_ext->blob) < 0) {
			fido_log_debug("%s: fido_blob_decode", __func__);
			goto out;
		}
		authdata_ext->mask |= FIDO_EXT_CRED_BLOB;
	}

	ok = 0;
out:
	free(type);

	return (ok);
}

static int
decode_assert_extensions(const unsigned char **buf, size_t *len,
    fido_assert_extattr_t *authdata_ext)
{
	cbor_item_t		*item = NULL;
	struct cbor_load_result	 cbor;
	int			 ok = -1;

	fido_log_xxd(*buf, *len, "%s", __func__);

	if ((item = cbor_load(*buf, *len, &cbor)) == NULL) {
		fido_log_debug("%s: cbor_load", __func__);
		goto fail;
	}

	if (cbor_isa_map(item) == false ||
	    cbor_map_is_definite(item) == false ||
	    cbor_map_iter(item, authdata_ext, decode_assert_extension) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		goto fail;
	}

	*buf += cbor.read;
	*len -= cbor.read;

	ok = 0;
fail:
	if (item != NULL)
		cbor_decref(&item);

	return (ok);
}

int
cbor_decode_cred_authdata(const cbor_item_t *item, int cose_alg,
    fido_blob_t *authdata_cbor, fido_authdata_t *authdata,
    fido_attcred_t *attcred, fido_cred_ext_t *authdata_ext)
{
	const unsigned char	*buf = NULL;
	size_t			 len;
	size_t			 alloc_len;

	if (cbor_isa_bytestring(item) == false ||
	    cbor_bytestring_is_definite(item) == false) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	if (authdata_cbor->ptr != NULL ||
	    (authdata_cbor->len = cbor_serialize_alloc(item,
	    &authdata_cbor->ptr, &alloc_len)) == 0) {
		fido_log_debug("%s: cbor_serialize_alloc", __func__);
		return (-1);
	}

	buf = cbor_bytestring_handle(item);
	len = cbor_bytestring_length(item);
	fido_log_xxd(buf, len, "%s", __func__);

	if (fido_buf_read(&buf, &len, authdata, sizeof(*authdata)) < 0) {
		fido_log_debug("%s: fido_buf_read", __func__);
		return (-1);
	}

	authdata->sigcount = be32toh(authdata->sigcount);

	if (attcred != NULL) {
		if ((authdata->flags & CTAP_AUTHDATA_ATT_CRED) == 0 ||
		    decode_attcred(&buf, &len, cose_alg, attcred) < 0)
			return (-1);
	}

	if (authdata_ext != NULL) {
		if ((authdata->flags & CTAP_AUTHDATA_EXT_DATA) != 0 &&
		    decode_cred_extensions(&buf, &len, authdata_ext) < 0)
			return (-1);
	}

	/* XXX we should probably ensure that len == 0 at this point */

	return (FIDO_OK);
}

int
cbor_decode_assert_authdata(const cbor_item_t *item, fido_blob_t *authdata_cbor,
    fido_authdata_t *authdata, fido_assert_extattr_t *authdata_ext)
{
	const unsigned char	*buf = NULL;
	size_t			 len;
	size_t			 alloc_len;

	if (cbor_isa_bytestring(item) == false ||
	    cbor_bytestring_is_definite(item) == false) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	if (authdata_cbor->ptr != NULL ||
	    (authdata_cbor->len = cbor_serialize_alloc(item,
	    &authdata_cbor->ptr, &alloc_len)) == 0) {
		fido_log_debug("%s: cbor_serialize_alloc", __func__);
		return (-1);
	}

	buf = cbor_bytestring_handle(item);
	len = cbor_bytestring_length(item);

	fido_log_debug("%s: buf=%p, len=%zu", __func__, (const void *)buf, len);

	if (fido_buf_read(&buf, &len, authdata, sizeof(*authdata)) < 0) {
		fido_log_debug("%s: fido_buf_read", __func__);
		return (-1);
	}

	authdata->sigcount = be32toh(authdata->sigcount);

	if ((authdata->flags & CTAP_AUTHDATA_EXT_DATA) != 0) {
		if (decode_assert_extensions(&buf, &len, authdata_ext) < 0) {
			fido_log_debug("%s: decode_assert_extensions",
			    __func__);
			return (-1);
		}
	}

	/* XXX we should probably ensure that len == 0 at this point */

	return (FIDO_OK);
}

static int
decode_x5c(const cbor_item_t *item, void *arg)
{
	fido_blob_t *x5c = arg;

	if (x5c->len)
		return (0); /* ignore */

	return (fido_blob_decode(item, x5c));
}

static int
decode_attstmt_entry(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	fido_attstmt_t	*attstmt = arg;
	char		*name = NULL;
	int		 ok = -1;

	if (cbor_string_copy(key, &name) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		ok = 0; /* ignore */
		goto out;
	}

	if (!strcmp(name, "alg")) {
		if (cbor_isa_negint(val) == false ||
		    cbor_get_int(val) > UINT16_MAX) {
			fido_log_debug("%s: alg", __func__);
			goto out;
		}
		attstmt->alg = -(int)cbor_get_int(val) - 1;
		if (attstmt->alg != COSE_ES256 && attstmt->alg != COSE_RS256 &&
		    attstmt->alg != COSE_EDDSA && attstmt->alg != COSE_RS1) {
			fido_log_debug("%s: unsupported attstmt->alg=%d",
			    __func__, attstmt->alg);
			goto out;
		}
	} else if (!strcmp(name, "sig")) {
		if (fido_blob_decode(val, &attstmt->sig) < 0) {
			fido_log_debug("%s: sig", __func__);
			goto out;
		}
	} else if (!strcmp(name, "x5c")) {
		if (cbor_isa_array(val) == false ||
		    cbor_array_is_definite(val) == false ||
		    cbor_array_iter(val, &attstmt->x5c, decode_x5c) < 0) {
			fido_log_debug("%s: x5c", __func__);
			goto out;
		}
	} else if (!strcmp(name, "certInfo")) {
		if (fido_blob_decode(val, &attstmt->certinfo) < 0) {
			fido_log_debug("%s: certinfo", __func__);
			goto out;
		}
	} else if (!strcmp(name, "pubArea")) {
		if (fido_blob_decode(val, &attstmt->pubarea) < 0) {
			fido_log_debug("%s: pubarea", __func__);
			goto out;
		}
	}

	ok = 0;
out:
	free(name);

	return (ok);
}

int
cbor_decode_attstmt(const cbor_item_t *item, fido_attstmt_t *attstmt)
{
	size_t alloc_len;

	if (cbor_isa_map(item) == false ||
	    cbor_map_is_definite(item) == false ||
	    cbor_map_iter(item, attstmt, decode_attstmt_entry) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	if (attstmt->cbor.ptr != NULL ||
	    (attstmt->cbor.len = cbor_serialize_alloc(item,
	    &attstmt->cbor.ptr, &alloc_len)) == 0) {
		fido_log_debug("%s: cbor_serialize_alloc", __func__);
		return (-1);
	}

	return (0);
}

int
cbor_decode_uint64(const cbor_item_t *item, uint64_t *n)
{
	if (cbor_isa_uint(item) == false) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	*n = cbor_get_int(item);

	return (0);
}

static int
decode_cred_id_entry(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	fido_blob_t	*id = arg;
	char		*name = NULL;
	int		 ok = -1;

	if (cbor_string_copy(key, &name) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		ok = 0; /* ignore */
		goto out;
	}

	if (!strcmp(name, "id"))
		if (fido_blob_decode(val, id) < 0) {
			fido_log_debug("%s: cbor_bytestring_copy", __func__);
			goto out;
		}

	ok = 0;
out:
	free(name);

	return (ok);
}

int
cbor_decode_cred_id(const cbor_item_t *item, fido_blob_t *id)
{
	if (cbor_isa_map(item) == false ||
	    cbor_map_is_definite(item) == false ||
	    cbor_map_iter(item, id, decode_cred_id_entry) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	return (0);
}

static int
decode_user_entry(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	fido_user_t	*user = arg;
	char		*name = NULL;
	int		 ok = -1;

	if (cbor_string_copy(key, &name) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		ok = 0; /* ignore */
		goto out;
	}

	if (!strcmp(name, "icon")) {
		if (cbor_string_copy(val, &user->icon) < 0) {
			fido_log_debug("%s: icon", __func__);
			goto out;
		}
	} else if (!strcmp(name, "name")) {
		if (cbor_string_copy(val, &user->name) < 0) {
			fido_log_debug("%s: name", __func__);
			goto out;
		}
	} else if (!strcmp(name, "displayName")) {
		if (cbor_string_copy(val, &user->display_name) < 0) {
			fido_log_debug("%s: display_name", __func__);
			goto out;
		}
	} else if (!strcmp(name, "id")) {
		if (fido_blob_decode(val, &user->id) < 0) {
			fido_log_debug("%s: id", __func__);
			goto out;
		}
	}

	ok = 0;
out:
	free(name);

	return (ok);
}

int
cbor_decode_user(const cbor_item_t *item, fido_user_t *user)
{
	if (cbor_isa_map(item) == false ||
	    cbor_map_is_definite(item) == false ||
	    cbor_map_iter(item, user, decode_user_entry) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	return (0);
}

static int
decode_rp_entity_entry(const cbor_item_t *key, const cbor_item_t *val,
    void *arg)
{
	fido_rp_t	*rp = arg;
	char		*name = NULL;
	int		 ok = -1;

	if (cbor_string_copy(key, &name) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		ok = 0; /* ignore */
		goto out;
	}

	if (!strcmp(name, "id")) {
		if (cbor_string_copy(val, &rp->id) < 0) {
			fido_log_debug("%s: id", __func__);
			goto out;
		}
	} else if (!strcmp(name, "name")) {
		if (cbor_string_copy(val, &rp->name) < 0) {
			fido_log_debug("%s: name", __func__);
			goto out;
		}
	}

	ok = 0;
out:
	free(name);

	return (ok);
}

int
cbor_decode_rp_entity(const cbor_item_t *item, fido_rp_t *rp)
{
	if (cbor_isa_map(item) == false ||
	    cbor_map_is_definite(item) == false ||
	    cbor_map_iter(item, rp, decode_rp_entity_entry) < 0) {
		fido_log_debug("%s: cbor type", __func__);
		return (-1);
	}

	return (0);
}

cbor_item_t *
cbor_build_uint(const uint64_t value)
{
	if (value <= UINT8_MAX)
		return cbor_build_uint8((uint8_t)value);
	else if (value <= UINT16_MAX)
		return cbor_build_uint16((uint16_t)value);
	else if (value <= UINT32_MAX)
		return cbor_build_uint32((uint32_t)value);

	return cbor_build_uint64(value);
}

int
cbor_array_append(cbor_item_t **array, cbor_item_t *item)
{
	cbor_item_t **v, *ret;
	size_t n;

	if ((v = cbor_array_handle(*array)) == NULL ||
	    (n = cbor_array_size(*array)) == SIZE_MAX ||
	    (ret = cbor_new_definite_array(n + 1)) == NULL)
		return -1;
	for (size_t i = 0; i < n; i++) {
		if (cbor_array_push(ret, v[i]) == 0) {
			cbor_decref(&ret);
			return -1;
		}
	}
	if (cbor_array_push(ret, item) == 0) {
		cbor_decref(&ret);
		return -1;
	}
	cbor_decref(array);
	*array = ret;

	return 0;
}

int
cbor_array_drop(cbor_item_t **array, size_t idx)
{
	cbor_item_t **v, *ret;
	size_t n;

	if ((v = cbor_array_handle(*array)) == NULL ||
	    (n = cbor_array_size(*array)) == 0 || idx >= n ||
	    (ret = cbor_new_definite_array(n - 1)) == NULL)
		return -1;
	for (size_t i = 0; i < n; i++) {
		if (i != idx && cbor_array_push(ret, v[i]) == 0) {
			cbor_decref(&ret);
			return -1;
		}
	}
	cbor_decref(array);
	*array = ret;

	return 0;
}
