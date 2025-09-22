/*
 * Copyright (c) 2018-2021 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <openssl/sha.h>

#include "fido.h"
#include "fido/es256.h"
#include "fido/rs256.h"
#include "fido/eddsa.h"

static int
adjust_assert_count(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	fido_assert_t	*assert = arg;
	uint64_t	 n;

	/* numberOfCredentials; see section 6.2 */
	if (cbor_isa_uint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8 ||
	    cbor_get_uint8(key) != 5) {
		fido_log_debug("%s: cbor_type", __func__);
		return (0); /* ignore */
	}

	if (cbor_decode_uint64(val, &n) < 0 || n > SIZE_MAX) {
		fido_log_debug("%s: cbor_decode_uint64", __func__);
		return (-1);
	}

	if (assert->stmt_len != 0 || assert->stmt_cnt != 1 ||
	    (size_t)n < assert->stmt_cnt) {
		fido_log_debug("%s: stmt_len=%zu, stmt_cnt=%zu, n=%zu",
		    __func__, assert->stmt_len, assert->stmt_cnt, (size_t)n);
		return (-1);
	}

	if (fido_assert_set_count(assert, (size_t)n) != FIDO_OK) {
		fido_log_debug("%s: fido_assert_set_count", __func__);
		return (-1);
	}

	assert->stmt_len = 0; /* XXX */

	return (0);
}

static int
parse_assert_reply(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	fido_assert_stmt *stmt = arg;

	if (cbor_isa_uint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8) {
		fido_log_debug("%s: cbor type", __func__);
		return (0); /* ignore */
	}

	switch (cbor_get_uint8(key)) {
	case 1: /* credential id */
		return (cbor_decode_cred_id(val, &stmt->id));
	case 2: /* authdata */
		return (cbor_decode_assert_authdata(val, &stmt->authdata_cbor,
		    &stmt->authdata, &stmt->authdata_ext));
	case 3: /* signature */
		return (fido_blob_decode(val, &stmt->sig));
	case 4: /* user attributes */
		return (cbor_decode_user(val, &stmt->user));
	case 7: /* large blob key */
		return (fido_blob_decode(val, &stmt->largeblob_key));
	default: /* ignore */
		fido_log_debug("%s: cbor type", __func__);
		return (0);
	}
}

static int
fido_dev_get_assert_tx(fido_dev_t *dev, fido_assert_t *assert,
    const es256_pk_t *pk, const fido_blob_t *ecdh, const char *pin, int *ms)
{
	fido_blob_t	 f;
	fido_opt_t	 uv = assert->uv;
	cbor_item_t	*argv[7];
	const uint8_t	 cmd = CTAP_CBOR_ASSERT;
	int		 r;

	memset(argv, 0, sizeof(argv));
	memset(&f, 0, sizeof(f));

	/* do we have everything we need? */
	if (assert->rp_id == NULL || assert->cdh.ptr == NULL) {
		fido_log_debug("%s: rp_id=%p, cdh.ptr=%p", __func__,
		    (void *)assert->rp_id, (void *)assert->cdh.ptr);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto fail;
	}

	if ((argv[0] = cbor_build_string(assert->rp_id)) == NULL ||
	    (argv[1] = fido_blob_encode(&assert->cdh)) == NULL) {
		fido_log_debug("%s: cbor encode", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	/* allowed credentials */
	if (assert->allow_list.len) {
		const fido_blob_array_t *cl = &assert->allow_list;
		if ((argv[2] = cbor_encode_pubkey_list(cl)) == NULL) {
			fido_log_debug("%s: cbor_encode_pubkey_list", __func__);
			r = FIDO_ERR_INTERNAL;
			goto fail;
		}
	}

	if (assert->ext.mask)
		if ((argv[3] = cbor_encode_assert_ext(dev, &assert->ext, ecdh,
		    pk)) == NULL) {
			fido_log_debug("%s: cbor_encode_assert_ext", __func__);
			r = FIDO_ERR_INTERNAL;
			goto fail;
		}

	/* user verification */
	if (pin != NULL || (uv == FIDO_OPT_TRUE &&
	    fido_dev_supports_permissions(dev))) {
		if ((r = cbor_add_uv_params(dev, cmd, &assert->cdh, pk, ecdh,
		    pin, assert->rp_id, &argv[5], &argv[6], ms)) != FIDO_OK) {
			fido_log_debug("%s: cbor_add_uv_params", __func__);
			goto fail;
		}
		uv = FIDO_OPT_OMIT;
	}

	/* options */
	if (assert->up != FIDO_OPT_OMIT || uv != FIDO_OPT_OMIT)
		if ((argv[4] = cbor_encode_assert_opt(assert->up, uv)) == NULL) {
			fido_log_debug("%s: cbor_encode_assert_opt", __func__);
			r = FIDO_ERR_INTERNAL;
			goto fail;
		}

	/* frame and transmit */
	if (cbor_build_frame(cmd, argv, nitems(argv), &f) < 0 ||
	    fido_tx(dev, CTAP_CMD_CBOR, f.ptr, f.len, ms) < 0) {
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
fido_dev_get_assert_rx(fido_dev_t *dev, fido_assert_t *assert, int *ms)
{
	unsigned char	reply[FIDO_MAXMSG];
	int		reply_len;
	int		r;

	fido_assert_reset_rx(assert);

	if ((reply_len = fido_rx(dev, CTAP_CMD_CBOR, &reply, sizeof(reply),
	    ms)) < 0) {
		fido_log_debug("%s: fido_rx", __func__);
		return (FIDO_ERR_RX);
	}

	/* start with room for a single assertion */
	if ((assert->stmt = calloc(1, sizeof(fido_assert_stmt))) == NULL)
		return (FIDO_ERR_INTERNAL);

	assert->stmt_len = 0;
	assert->stmt_cnt = 1;

	/* adjust as needed */
	if ((r = cbor_parse_reply(reply, (size_t)reply_len, assert,
	    adjust_assert_count)) != FIDO_OK) {
		fido_log_debug("%s: adjust_assert_count", __func__);
		return (r);
	}

	/* parse the first assertion */
	if ((r = cbor_parse_reply(reply, (size_t)reply_len,
	    &assert->stmt[assert->stmt_len], parse_assert_reply)) != FIDO_OK) {
		fido_log_debug("%s: parse_assert_reply", __func__);
		return (r);
	}

	assert->stmt_len++;

	return (FIDO_OK);
}

static int
fido_get_next_assert_tx(fido_dev_t *dev, int *ms)
{
	const unsigned char cbor[] = { CTAP_CBOR_NEXT_ASSERT };

	if (fido_tx(dev, CTAP_CMD_CBOR, cbor, sizeof(cbor), ms) < 0) {
		fido_log_debug("%s: fido_tx", __func__);
		return (FIDO_ERR_TX);
	}

	return (FIDO_OK);
}

static int
fido_get_next_assert_rx(fido_dev_t *dev, fido_assert_t *assert, int *ms)
{
	unsigned char	reply[FIDO_MAXMSG];
	int		reply_len;
	int		r;

	if ((reply_len = fido_rx(dev, CTAP_CMD_CBOR, &reply, sizeof(reply),
	    ms)) < 0) {
		fido_log_debug("%s: fido_rx", __func__);
		return (FIDO_ERR_RX);
	}

	/* sanity check */
	if (assert->stmt_len >= assert->stmt_cnt) {
		fido_log_debug("%s: stmt_len=%zu, stmt_cnt=%zu", __func__,
		    assert->stmt_len, assert->stmt_cnt);
		return (FIDO_ERR_INTERNAL);
	}

	if ((r = cbor_parse_reply(reply, (size_t)reply_len,
	    &assert->stmt[assert->stmt_len], parse_assert_reply)) != FIDO_OK) {
		fido_log_debug("%s: parse_assert_reply", __func__);
		return (r);
	}

	return (FIDO_OK);
}

static int
fido_dev_get_assert_wait(fido_dev_t *dev, fido_assert_t *assert,
    const es256_pk_t *pk, const fido_blob_t *ecdh, const char *pin, int *ms)
{
	int r;

	if ((r = fido_dev_get_assert_tx(dev, assert, pk, ecdh, pin,
	    ms)) != FIDO_OK ||
	    (r = fido_dev_get_assert_rx(dev, assert, ms)) != FIDO_OK)
		return (r);

	while (assert->stmt_len < assert->stmt_cnt) {
		if ((r = fido_get_next_assert_tx(dev, ms)) != FIDO_OK ||
		    (r = fido_get_next_assert_rx(dev, assert, ms)) != FIDO_OK)
			return (r);
		assert->stmt_len++;
	}

	return (FIDO_OK);
}

static int
decrypt_hmac_secrets(const fido_dev_t *dev, fido_assert_t *assert,
    const fido_blob_t *key)
{
	for (size_t i = 0; i < assert->stmt_cnt; i++) {
		fido_assert_stmt *stmt = &assert->stmt[i];
		if (stmt->authdata_ext.hmac_secret_enc.ptr != NULL) {
			if (aes256_cbc_dec(dev, key,
			    &stmt->authdata_ext.hmac_secret_enc,
			    &stmt->hmac_secret) < 0) {
				fido_log_debug("%s: aes256_cbc_dec %zu",
				    __func__, i);
				return (-1);
			}
		}
	}

	return (0);
}

int
fido_dev_get_assert(fido_dev_t *dev, fido_assert_t *assert, const char *pin)
{
	fido_blob_t	*ecdh = NULL;
	es256_pk_t	*pk = NULL;
	int		 ms = dev->timeout_ms;
	int		 r;

#ifdef USE_WINHELLO
	if (dev->flags & FIDO_DEV_WINHELLO)
		return (fido_winhello_get_assert(dev, assert, pin, ms));
#endif

	if (assert->rp_id == NULL || assert->cdh.ptr == NULL) {
		fido_log_debug("%s: rp_id=%p, cdh.ptr=%p", __func__,
		    (void *)assert->rp_id, (void *)assert->cdh.ptr);
		return (FIDO_ERR_INVALID_ARGUMENT);
	}

	if (fido_dev_is_fido2(dev) == false) {
		if (pin != NULL || assert->ext.mask != 0)
			return (FIDO_ERR_UNSUPPORTED_OPTION);
		return (u2f_authenticate(dev, assert, &ms));
	}

	if (pin != NULL || (assert->uv == FIDO_OPT_TRUE &&
	    fido_dev_supports_permissions(dev)) ||
	    (assert->ext.mask & FIDO_EXT_HMAC_SECRET)) {
		if ((r = fido_do_ecdh(dev, &pk, &ecdh, &ms)) != FIDO_OK) {
			fido_log_debug("%s: fido_do_ecdh", __func__);
			goto fail;
		}
	}

	r = fido_dev_get_assert_wait(dev, assert, pk, ecdh, pin, &ms);
	if (r == FIDO_OK && (assert->ext.mask & FIDO_EXT_HMAC_SECRET))
		if (decrypt_hmac_secrets(dev, assert, ecdh) < 0) {
			fido_log_debug("%s: decrypt_hmac_secrets", __func__);
			r = FIDO_ERR_INTERNAL;
			goto fail;
		}

fail:
	es256_pk_free(&pk);
	fido_blob_free(&ecdh);

	return (r);
}

int
fido_check_flags(uint8_t flags, fido_opt_t up, fido_opt_t uv)
{
	fido_log_debug("%s: flags=%02x", __func__, flags);
	fido_log_debug("%s: up=%d, uv=%d", __func__, up, uv);

	if (up == FIDO_OPT_TRUE &&
	    (flags & CTAP_AUTHDATA_USER_PRESENT) == 0) {
		fido_log_debug("%s: CTAP_AUTHDATA_USER_PRESENT", __func__);
		return (-1); /* user not present */
	}

	if (uv == FIDO_OPT_TRUE &&
	    (flags & CTAP_AUTHDATA_USER_VERIFIED) == 0) {
		fido_log_debug("%s: CTAP_AUTHDATA_USER_VERIFIED", __func__);
		return (-1); /* user not verified */
	}

	return (0);
}

static int
check_extensions(int authdata_ext, int ext)
{
	/* XXX: largeBlobKey is not part of extensions map */
	ext &= ~FIDO_EXT_LARGEBLOB_KEY;
	if (authdata_ext != ext) {
		fido_log_debug("%s: authdata_ext=0x%x != ext=0x%x", __func__,
		    authdata_ext, ext);
		return (-1);
	}

	return (0);
}

int
fido_get_signed_hash(int cose_alg, fido_blob_t *dgst,
    const fido_blob_t *clientdata, const fido_blob_t *authdata_cbor)
{
	cbor_item_t		*item = NULL;
	unsigned char		*authdata_ptr = NULL;
	size_t			 authdata_len;
	struct cbor_load_result	 cbor;
	const EVP_MD		*md = NULL;
	EVP_MD_CTX		*ctx = NULL;
	int			 ok = -1;

	if ((item = cbor_load(authdata_cbor->ptr, authdata_cbor->len,
	    &cbor)) == NULL || cbor_isa_bytestring(item) == false ||
	    cbor_bytestring_is_definite(item) == false) {
		fido_log_debug("%s: authdata", __func__);
		goto fail;
	}

	authdata_ptr = cbor_bytestring_handle(item);
	authdata_len = cbor_bytestring_length(item);

	if (cose_alg != COSE_EDDSA) {
		if (dgst->len < SHA256_DIGEST_LENGTH ||
		    (md = EVP_sha256()) == NULL ||
		    (ctx = EVP_MD_CTX_new()) == NULL ||
		    EVP_DigestInit_ex(ctx, md, NULL) != 1 ||
		    EVP_DigestUpdate(ctx, authdata_ptr, authdata_len) != 1 ||
		    EVP_DigestUpdate(ctx, clientdata->ptr, clientdata->len) != 1 ||
		    EVP_DigestFinal_ex(ctx, dgst->ptr, NULL) != 1) {
			fido_log_debug("%s: sha256", __func__);
			goto fail;
		}
		dgst->len = SHA256_DIGEST_LENGTH;
	} else {
		if (SIZE_MAX - authdata_len < clientdata->len ||
		    dgst->len < authdata_len + clientdata->len) {
			fido_log_debug("%s: memcpy", __func__);
			goto fail;
		}
		memcpy(dgst->ptr, authdata_ptr, authdata_len);
		memcpy(dgst->ptr + authdata_len, clientdata->ptr,
		    clientdata->len);
		dgst->len = authdata_len + clientdata->len;
	}

	ok = 0;
fail:
	if (item != NULL)
		cbor_decref(&item);

	EVP_MD_CTX_free(ctx);

	return (ok);
}

int
fido_assert_verify(const fido_assert_t *assert, size_t idx, int cose_alg,
    const void *pk)
{
	unsigned char		 buf[1024]; /* XXX */
	fido_blob_t		 dgst;
	const fido_assert_stmt	*stmt = NULL;
	int			 ok = -1;
	int			 r;

	dgst.ptr = buf;
	dgst.len = sizeof(buf);

	if (idx >= assert->stmt_len || pk == NULL) {
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto out;
	}

	stmt = &assert->stmt[idx];

	/* do we have everything we need? */
	if (assert->cdh.ptr == NULL || assert->rp_id == NULL ||
	    stmt->authdata_cbor.ptr == NULL || stmt->sig.ptr == NULL) {
		fido_log_debug("%s: cdh=%p, rp_id=%s, authdata=%p, sig=%p",
		    __func__, (void *)assert->cdh.ptr, assert->rp_id,
		    (void *)stmt->authdata_cbor.ptr, (void *)stmt->sig.ptr);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto out;
	}

	if (fido_check_flags(stmt->authdata.flags, assert->up,
	    assert->uv) < 0) {
		fido_log_debug("%s: fido_check_flags", __func__);
		r = FIDO_ERR_INVALID_PARAM;
		goto out;
	}

	if (check_extensions(stmt->authdata_ext.mask, assert->ext.mask) < 0) {
		fido_log_debug("%s: check_extensions", __func__);
		r = FIDO_ERR_INVALID_PARAM;
		goto out;
	}

	if (fido_check_rp_id(assert->rp_id, stmt->authdata.rp_id_hash) != 0) {
		fido_log_debug("%s: fido_check_rp_id", __func__);
		r = FIDO_ERR_INVALID_PARAM;
		goto out;
	}

	if (fido_get_signed_hash(cose_alg, &dgst, &assert->cdh,
	    &stmt->authdata_cbor) < 0) {
		fido_log_debug("%s: fido_get_signed_hash", __func__);
		r = FIDO_ERR_INTERNAL;
		goto out;
	}

	switch (cose_alg) {
	case COSE_ES256:
		ok = es256_pk_verify_sig(&dgst, pk, &stmt->sig);
		break;
	case COSE_RS256:
		ok = rs256_pk_verify_sig(&dgst, pk, &stmt->sig);
		break;
	case COSE_EDDSA:
		ok = eddsa_pk_verify_sig(&dgst, pk, &stmt->sig);
		break;
	default:
		fido_log_debug("%s: unsupported cose_alg %d", __func__,
		    cose_alg);
		r = FIDO_ERR_UNSUPPORTED_OPTION;
		goto out;
	}

	if (ok < 0)
		r = FIDO_ERR_INVALID_SIG;
	else
		r = FIDO_OK;
out:
	explicit_bzero(buf, sizeof(buf));

	return (r);
}

int
fido_assert_set_clientdata(fido_assert_t *assert, const unsigned char *data,
    size_t data_len)
{
	if (!fido_blob_is_empty(&assert->cdh) ||
	    fido_blob_set(&assert->cd, data, data_len) < 0) {
		return (FIDO_ERR_INVALID_ARGUMENT);
	}
	if (fido_sha256(&assert->cdh, data, data_len) < 0) {
		fido_blob_reset(&assert->cd);
		return (FIDO_ERR_INTERNAL);
	}

	return (FIDO_OK);
}

int
fido_assert_set_clientdata_hash(fido_assert_t *assert,
    const unsigned char *hash, size_t hash_len)
{
	if (!fido_blob_is_empty(&assert->cd) ||
	    fido_blob_set(&assert->cdh, hash, hash_len) < 0)
		return (FIDO_ERR_INVALID_ARGUMENT);

	return (FIDO_OK);
}

int
fido_assert_set_hmac_salt(fido_assert_t *assert, const unsigned char *salt,
    size_t salt_len)
{
	if ((salt_len != 32 && salt_len != 64) ||
	    fido_blob_set(&assert->ext.hmac_salt, salt, salt_len) < 0)
		return (FIDO_ERR_INVALID_ARGUMENT);

	return (FIDO_OK);
}

int
fido_assert_set_hmac_secret(fido_assert_t *assert, size_t idx,
    const unsigned char *secret, size_t secret_len)
{
	if (idx >= assert->stmt_len || (secret_len != 32 && secret_len != 64) ||
	    fido_blob_set(&assert->stmt[idx].hmac_secret, secret,
	    secret_len) < 0)
		return (FIDO_ERR_INVALID_ARGUMENT);

	return (FIDO_OK);
}

int
fido_assert_set_rp(fido_assert_t *assert, const char *id)
{
	if (assert->rp_id != NULL) {
		free(assert->rp_id);
		assert->rp_id = NULL;
	}

	if (id == NULL)
		return (FIDO_ERR_INVALID_ARGUMENT);

	if ((assert->rp_id = strdup(id)) == NULL)
		return (FIDO_ERR_INTERNAL);

	return (FIDO_OK);
}

int
fido_assert_allow_cred(fido_assert_t *assert, const unsigned char *ptr,
    size_t len)
{
	fido_blob_t	 id;
	fido_blob_t	*list_ptr;
	int		 r;

	memset(&id, 0, sizeof(id));

	if (assert->allow_list.len == SIZE_MAX) {
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto fail;
	}

	if (fido_blob_set(&id, ptr, len) < 0 || (list_ptr =
	    recallocarray(assert->allow_list.ptr, assert->allow_list.len,
	    assert->allow_list.len + 1, sizeof(fido_blob_t))) == NULL) {
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto fail;
	}

	list_ptr[assert->allow_list.len++] = id;
	assert->allow_list.ptr = list_ptr;

	return (FIDO_OK);
fail:
	free(id.ptr);

	return (r);

}

int
fido_assert_set_extensions(fido_assert_t *assert, int ext)
{
	if (ext == 0)
		assert->ext.mask = 0;
	else {
		if ((ext & FIDO_EXT_ASSERT_MASK) != ext)
			return (FIDO_ERR_INVALID_ARGUMENT);
		assert->ext.mask |= ext;
	}

	return (FIDO_OK);
}

int
fido_assert_set_options(fido_assert_t *assert, bool up, bool uv)
{
	assert->up = up ? FIDO_OPT_TRUE : FIDO_OPT_FALSE;
	assert->uv = uv ? FIDO_OPT_TRUE : FIDO_OPT_FALSE;

	return (FIDO_OK);
}

int
fido_assert_set_up(fido_assert_t *assert, fido_opt_t up)
{
	assert->up = up;

	return (FIDO_OK);
}

int
fido_assert_set_uv(fido_assert_t *assert, fido_opt_t uv)
{
	assert->uv = uv;

	return (FIDO_OK);
}

const unsigned char *
fido_assert_clientdata_hash_ptr(const fido_assert_t *assert)
{
	return (assert->cdh.ptr);
}

size_t
fido_assert_clientdata_hash_len(const fido_assert_t *assert)
{
	return (assert->cdh.len);
}

fido_assert_t *
fido_assert_new(void)
{
	return (calloc(1, sizeof(fido_assert_t)));
}

void
fido_assert_reset_tx(fido_assert_t *assert)
{
	free(assert->rp_id);
	fido_blob_reset(&assert->cd);
	fido_blob_reset(&assert->cdh);
	fido_blob_reset(&assert->ext.hmac_salt);
	fido_free_blob_array(&assert->allow_list);
	memset(&assert->ext, 0, sizeof(assert->ext));
	memset(&assert->allow_list, 0, sizeof(assert->allow_list));
	assert->rp_id = NULL;
	assert->up = FIDO_OPT_OMIT;
	assert->uv = FIDO_OPT_OMIT;
}

static void fido_assert_reset_extattr(fido_assert_extattr_t *ext)
{
	fido_blob_reset(&ext->hmac_secret_enc);
	fido_blob_reset(&ext->blob);
	memset(ext, 0, sizeof(*ext));
}

void
fido_assert_reset_rx(fido_assert_t *assert)
{
	for (size_t i = 0; i < assert->stmt_cnt; i++) {
		free(assert->stmt[i].user.icon);
		free(assert->stmt[i].user.name);
		free(assert->stmt[i].user.display_name);
		fido_blob_reset(&assert->stmt[i].user.id);
		fido_blob_reset(&assert->stmt[i].id);
		fido_blob_reset(&assert->stmt[i].hmac_secret);
		fido_blob_reset(&assert->stmt[i].authdata_cbor);
		fido_blob_reset(&assert->stmt[i].largeblob_key);
		fido_blob_reset(&assert->stmt[i].sig);
		fido_assert_reset_extattr(&assert->stmt[i].authdata_ext);
		memset(&assert->stmt[i], 0, sizeof(assert->stmt[i]));
	}
	free(assert->stmt);
	assert->stmt = NULL;
	assert->stmt_len = 0;
	assert->stmt_cnt = 0;
}

void
fido_assert_free(fido_assert_t **assert_p)
{
	fido_assert_t *assert;

	if (assert_p == NULL || (assert = *assert_p) == NULL)
		return;
	fido_assert_reset_tx(assert);
	fido_assert_reset_rx(assert);
	free(assert);
	*assert_p = NULL;
}

size_t
fido_assert_count(const fido_assert_t *assert)
{
	return (assert->stmt_len);
}

const char *
fido_assert_rp_id(const fido_assert_t *assert)
{
	return (assert->rp_id);
}

uint8_t
fido_assert_flags(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (0);

	return (assert->stmt[idx].authdata.flags);
}

uint32_t
fido_assert_sigcount(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (0);

	return (assert->stmt[idx].authdata.sigcount);
}

const unsigned char *
fido_assert_authdata_ptr(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (NULL);

	return (assert->stmt[idx].authdata_cbor.ptr);
}

size_t
fido_assert_authdata_len(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (0);

	return (assert->stmt[idx].authdata_cbor.len);
}

const unsigned char *
fido_assert_sig_ptr(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (NULL);

	return (assert->stmt[idx].sig.ptr);
}

size_t
fido_assert_sig_len(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (0);

	return (assert->stmt[idx].sig.len);
}

const unsigned char *
fido_assert_id_ptr(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (NULL);

	return (assert->stmt[idx].id.ptr);
}

size_t
fido_assert_id_len(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (0);

	return (assert->stmt[idx].id.len);
}

const unsigned char *
fido_assert_user_id_ptr(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (NULL);

	return (assert->stmt[idx].user.id.ptr);
}

size_t
fido_assert_user_id_len(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (0);

	return (assert->stmt[idx].user.id.len);
}

const char *
fido_assert_user_icon(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (NULL);

	return (assert->stmt[idx].user.icon);
}

const char *
fido_assert_user_name(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (NULL);

	return (assert->stmt[idx].user.name);
}

const char *
fido_assert_user_display_name(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (NULL);

	return (assert->stmt[idx].user.display_name);
}

const unsigned char *
fido_assert_hmac_secret_ptr(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (NULL);

	return (assert->stmt[idx].hmac_secret.ptr);
}

size_t
fido_assert_hmac_secret_len(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (0);

	return (assert->stmt[idx].hmac_secret.len);
}

const unsigned char *
fido_assert_largeblob_key_ptr(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (NULL);

	return (assert->stmt[idx].largeblob_key.ptr);
}

size_t
fido_assert_largeblob_key_len(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (0);

	return (assert->stmt[idx].largeblob_key.len);
}

const unsigned char *
fido_assert_blob_ptr(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (NULL);

	return (assert->stmt[idx].authdata_ext.blob.ptr);
}

size_t
fido_assert_blob_len(const fido_assert_t *assert, size_t idx)
{
	if (idx >= assert->stmt_len)
		return (0);

	return (assert->stmt[idx].authdata_ext.blob.len);
}

static void
fido_assert_clean_authdata(fido_assert_stmt *stmt)
{
	fido_blob_reset(&stmt->authdata_cbor);
	fido_assert_reset_extattr(&stmt->authdata_ext);
	memset(&stmt->authdata, 0, sizeof(stmt->authdata));
}

int
fido_assert_set_authdata(fido_assert_t *assert, size_t idx,
    const unsigned char *ptr, size_t len)
{
	cbor_item_t		*item = NULL;
	fido_assert_stmt	*stmt = NULL;
	struct cbor_load_result	 cbor;
	int			 r;

	if (idx >= assert->stmt_len || ptr == NULL || len == 0)
		return (FIDO_ERR_INVALID_ARGUMENT);

	stmt = &assert->stmt[idx];
	fido_assert_clean_authdata(stmt);

	if ((item = cbor_load(ptr, len, &cbor)) == NULL) {
		fido_log_debug("%s: cbor_load", __func__);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto fail;
	}

	if (cbor_decode_assert_authdata(item, &stmt->authdata_cbor,
	    &stmt->authdata, &stmt->authdata_ext) < 0) {
		fido_log_debug("%s: cbor_decode_assert_authdata", __func__);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto fail;
	}

	r = FIDO_OK;
fail:
	if (item != NULL)
		cbor_decref(&item);

	if (r != FIDO_OK)
		fido_assert_clean_authdata(stmt);

	return (r);
}

int
fido_assert_set_authdata_raw(fido_assert_t *assert, size_t idx,
    const unsigned char *ptr, size_t len)
{
	cbor_item_t		*item = NULL;
	fido_assert_stmt	*stmt = NULL;
	int			 r;

	if (idx >= assert->stmt_len || ptr == NULL || len == 0)
		return (FIDO_ERR_INVALID_ARGUMENT);

	stmt = &assert->stmt[idx];
	fido_assert_clean_authdata(stmt);

	if ((item = cbor_build_bytestring(ptr, len)) == NULL) {
		fido_log_debug("%s: cbor_build_bytestring", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if (cbor_decode_assert_authdata(item, &stmt->authdata_cbor,
	    &stmt->authdata, &stmt->authdata_ext) < 0) {
		fido_log_debug("%s: cbor_decode_assert_authdata", __func__);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto fail;
	}

	r = FIDO_OK;
fail:
	if (item != NULL)
		cbor_decref(&item);

	if (r != FIDO_OK)
		fido_assert_clean_authdata(stmt);

	return (r);
}

int
fido_assert_set_sig(fido_assert_t *a, size_t idx, const unsigned char *ptr,
    size_t len)
{
	if (idx >= a->stmt_len || ptr == NULL || len == 0)
		return (FIDO_ERR_INVALID_ARGUMENT);
	if (fido_blob_set(&a->stmt[idx].sig, ptr, len) < 0)
		return (FIDO_ERR_INTERNAL);

	return (FIDO_OK);
}

/* XXX shrinking leaks memory; fortunately that shouldn't happen */
int
fido_assert_set_count(fido_assert_t *assert, size_t n)
{
	void *new_stmt;

#ifdef FIDO_FUZZ
	if (n > UINT8_MAX) {
		fido_log_debug("%s: n > UINT8_MAX", __func__);
		return (FIDO_ERR_INTERNAL);
	}
#endif

	new_stmt = recallocarray(assert->stmt, assert->stmt_cnt, n,
	    sizeof(fido_assert_stmt));
	if (new_stmt == NULL)
		return (FIDO_ERR_INTERNAL);

	assert->stmt = new_stmt;
	assert->stmt_cnt = n;
	assert->stmt_len = n;

	return (FIDO_OK);
}
