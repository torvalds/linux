/*
 * Copyright (c) 2018-2021 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <openssl/sha.h>
#include <openssl/x509.h>

#include "fido.h"
#include "fido/es256.h"

#ifndef FIDO_MAXMSG_CRED
#define FIDO_MAXMSG_CRED	4096
#endif

static int
parse_makecred_reply(const cbor_item_t *key, const cbor_item_t *val, void *arg)
{
	fido_cred_t *cred = arg;

	if (cbor_isa_uint(key) == false ||
	    cbor_int_get_width(key) != CBOR_INT_8) {
		fido_log_debug("%s: cbor type", __func__);
		return (0); /* ignore */
	}

	switch (cbor_get_uint8(key)) {
	case 1: /* fmt */
		return (cbor_decode_fmt(val, &cred->fmt));
	case 2: /* authdata */
		if (fido_blob_decode(val, &cred->authdata_raw) < 0) {
			fido_log_debug("%s: fido_blob_decode", __func__);
			return (-1);
		}
		return (cbor_decode_cred_authdata(val, cred->type,
		    &cred->authdata_cbor, &cred->authdata, &cred->attcred,
		    &cred->authdata_ext));
	case 3: /* attestation statement */
		return (cbor_decode_attstmt(val, &cred->attstmt));
	case 5: /* large blob key */
		return (fido_blob_decode(val, &cred->largeblob_key));
	default: /* ignore */
		fido_log_debug("%s: cbor type", __func__);
		return (0);
	}
}

static int
fido_dev_make_cred_tx(fido_dev_t *dev, fido_cred_t *cred, const char *pin,
    int *ms)
{
	fido_blob_t	 f;
	fido_blob_t	*ecdh = NULL;
	fido_opt_t	 uv = cred->uv;
	es256_pk_t	*pk = NULL;
	cbor_item_t	*argv[9];
	const uint8_t	 cmd = CTAP_CBOR_MAKECRED;
	int		 r;

	memset(&f, 0, sizeof(f));
	memset(argv, 0, sizeof(argv));

	if (cred->cdh.ptr == NULL || cred->type == 0) {
		fido_log_debug("%s: cdh=%p, type=%d", __func__,
		    (void *)cred->cdh.ptr, cred->type);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto fail;
	}

	if ((argv[0] = fido_blob_encode(&cred->cdh)) == NULL ||
	    (argv[1] = cbor_encode_rp_entity(&cred->rp)) == NULL ||
	    (argv[2] = cbor_encode_user_entity(&cred->user)) == NULL ||
	    (argv[3] = cbor_encode_pubkey_param(cred->type)) == NULL) {
		fido_log_debug("%s: cbor encode", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	/* excluded credentials */
	if (cred->excl.len)
		if ((argv[4] = cbor_encode_pubkey_list(&cred->excl)) == NULL) {
			fido_log_debug("%s: cbor_encode_pubkey_list", __func__);
			r = FIDO_ERR_INTERNAL;
			goto fail;
		}

	/* extensions */
	if (cred->ext.mask)
		if ((argv[5] = cbor_encode_cred_ext(&cred->ext,
		    &cred->blob)) == NULL) {
			fido_log_debug("%s: cbor_encode_cred_ext", __func__);
			r = FIDO_ERR_INTERNAL;
			goto fail;
		}

	/* user verification */
	if (pin != NULL || (uv == FIDO_OPT_TRUE &&
	    fido_dev_supports_permissions(dev))) {
		if ((r = fido_do_ecdh(dev, &pk, &ecdh, ms)) != FIDO_OK) {
			fido_log_debug("%s: fido_do_ecdh", __func__);
			goto fail;
		}
		if ((r = cbor_add_uv_params(dev, cmd, &cred->cdh, pk, ecdh,
		    pin, cred->rp.id, &argv[7], &argv[8], ms)) != FIDO_OK) {
			fido_log_debug("%s: cbor_add_uv_params", __func__);
			goto fail;
		}
		uv = FIDO_OPT_OMIT;
	}

	/* options */
	if (cred->rk != FIDO_OPT_OMIT || uv != FIDO_OPT_OMIT)
		if ((argv[6] = cbor_encode_cred_opt(cred->rk, uv)) == NULL) {
			fido_log_debug("%s: cbor_encode_cred_opt", __func__);
			r = FIDO_ERR_INTERNAL;
			goto fail;
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
	es256_pk_free(&pk);
	fido_blob_free(&ecdh);
	cbor_vector_free(argv, nitems(argv));
	free(f.ptr);

	return (r);
}

static int
fido_dev_make_cred_rx(fido_dev_t *dev, fido_cred_t *cred, int *ms)
{
	unsigned char	*reply;
	int		 reply_len;
	int		 r;

	fido_cred_reset_rx(cred);

	if ((reply = malloc(FIDO_MAXMSG_CRED)) == NULL) {
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if ((reply_len = fido_rx(dev, CTAP_CMD_CBOR, reply, FIDO_MAXMSG_CRED,
	    ms)) < 0) {
		fido_log_debug("%s: fido_rx", __func__);
		r = FIDO_ERR_RX;
		goto fail;
	}

	if ((r = cbor_parse_reply(reply, (size_t)reply_len, cred,
	    parse_makecred_reply)) != FIDO_OK) {
		fido_log_debug("%s: parse_makecred_reply", __func__);
		goto fail;
	}

	if (cred->fmt == NULL || fido_blob_is_empty(&cred->authdata_cbor) ||
	    fido_blob_is_empty(&cred->attcred.id)) {
		r = FIDO_ERR_INVALID_CBOR;
		goto fail;
	}

	r = FIDO_OK;
fail:
	free(reply);

	if (r != FIDO_OK)
		fido_cred_reset_rx(cred);

	return (r);
}

static int
fido_dev_make_cred_wait(fido_dev_t *dev, fido_cred_t *cred, const char *pin,
    int *ms)
{
	int  r;

	if ((r = fido_dev_make_cred_tx(dev, cred, pin, ms)) != FIDO_OK ||
	    (r = fido_dev_make_cred_rx(dev, cred, ms)) != FIDO_OK)
		return (r);

	return (FIDO_OK);
}

int
fido_dev_make_cred(fido_dev_t *dev, fido_cred_t *cred, const char *pin)
{
	int ms = dev->timeout_ms;

#ifdef USE_WINHELLO
	if (dev->flags & FIDO_DEV_WINHELLO)
		return (fido_winhello_make_cred(dev, cred, pin, ms));
#endif
	if (fido_dev_is_fido2(dev) == false) {
		if (pin != NULL || cred->rk == FIDO_OPT_TRUE ||
		    cred->ext.mask != 0)
			return (FIDO_ERR_UNSUPPORTED_OPTION);
		return (u2f_register(dev, cred, &ms));
	}

	return (fido_dev_make_cred_wait(dev, cred, pin, &ms));
}

static int
check_extensions(const fido_cred_ext_t *authdata_ext,
    const fido_cred_ext_t *ext)
{
	fido_cred_ext_t	 tmp;

	/* XXX: largeBlobKey is not part of the extensions map */
	memcpy(&tmp, ext, sizeof(tmp));
	tmp.mask &= ~FIDO_EXT_LARGEBLOB_KEY;

	return (timingsafe_bcmp(authdata_ext, &tmp, sizeof(*authdata_ext)));
}

int
fido_check_rp_id(const char *id, const unsigned char *obtained_hash)
{
	unsigned char expected_hash[SHA256_DIGEST_LENGTH];

	explicit_bzero(expected_hash, sizeof(expected_hash));

	if (SHA256((const unsigned char *)id, strlen(id),
	    expected_hash) != expected_hash) {
		fido_log_debug("%s: sha256", __func__);
		return (-1);
	}

	return (timingsafe_bcmp(expected_hash, obtained_hash,
	    SHA256_DIGEST_LENGTH));
}

static int
get_signed_hash_u2f(fido_blob_t *dgst, const unsigned char *rp_id,
    size_t rp_id_len, const fido_blob_t *clientdata, const fido_blob_t *id,
    const es256_pk_t *pk)
{
	const uint8_t	 zero = 0;
	const uint8_t	 four = 4; /* uncompressed point */
	const EVP_MD	*md = NULL;
	EVP_MD_CTX	*ctx = NULL;
	int		 ok = -1;

	if (dgst->len != SHA256_DIGEST_LENGTH ||
	    (md = EVP_sha256()) == NULL ||
	    (ctx = EVP_MD_CTX_new()) == NULL ||
	    EVP_DigestInit_ex(ctx, md, NULL) != 1 ||
	    EVP_DigestUpdate(ctx, &zero, sizeof(zero)) != 1 ||
	    EVP_DigestUpdate(ctx, rp_id, rp_id_len) != 1 ||
	    EVP_DigestUpdate(ctx, clientdata->ptr, clientdata->len) != 1 ||
	    EVP_DigestUpdate(ctx, id->ptr, id->len) != 1 ||
	    EVP_DigestUpdate(ctx, &four, sizeof(four)) != 1 ||
	    EVP_DigestUpdate(ctx, pk->x, sizeof(pk->x)) != 1 ||
	    EVP_DigestUpdate(ctx, pk->y, sizeof(pk->y)) != 1 ||
	    EVP_DigestFinal_ex(ctx, dgst->ptr, NULL) != 1) {
		fido_log_debug("%s: sha256", __func__);
		goto fail;
	}

	ok = 0;
fail:
	EVP_MD_CTX_free(ctx);

	return (ok);
}

static int
verify_attstmt(const fido_blob_t *dgst, const fido_attstmt_t *attstmt)
{
	BIO		*rawcert = NULL;
	X509		*cert = NULL;
	EVP_PKEY	*pkey = NULL;
	int		 ok = -1;

	/* openssl needs ints */
	if (attstmt->x5c.len > INT_MAX) {
		fido_log_debug("%s: x5c.len=%zu", __func__, attstmt->x5c.len);
		return (-1);
	}

	/* fetch key from x509 */
	if ((rawcert = BIO_new_mem_buf(attstmt->x5c.ptr,
	    (int)attstmt->x5c.len)) == NULL ||
	    (cert = d2i_X509_bio(rawcert, NULL)) == NULL ||
	    (pkey = X509_get_pubkey(cert)) == NULL) {
		fido_log_debug("%s: x509 key", __func__);
		goto fail;
	}

	switch (attstmt->alg) {
	case COSE_UNSPEC:
	case COSE_ES256:
		ok = es256_verify_sig(dgst, pkey, &attstmt->sig);
		break;
	case COSE_RS256:
		ok = rs256_verify_sig(dgst, pkey, &attstmt->sig);
		break;
	case COSE_RS1:
		ok = rs1_verify_sig(dgst, pkey, &attstmt->sig);
		break;
	case COSE_EDDSA:
		ok = eddsa_verify_sig(dgst, pkey, &attstmt->sig);
		break;
	default:
		fido_log_debug("%s: unknown alg %d", __func__, attstmt->alg);
		break;
	}

fail:
	BIO_free(rawcert);
	X509_free(cert);
	EVP_PKEY_free(pkey);

	return (ok);
}

int
fido_cred_verify(const fido_cred_t *cred)
{
	unsigned char	buf[SHA256_DIGEST_LENGTH];
	fido_blob_t	dgst;
	int		r;

	dgst.ptr = buf;
	dgst.len = sizeof(buf);

	/* do we have everything we need? */
	if (cred->cdh.ptr == NULL || cred->authdata_cbor.ptr == NULL ||
	    cred->attstmt.x5c.ptr == NULL || cred->attstmt.sig.ptr == NULL ||
	    cred->fmt == NULL || cred->attcred.id.ptr == NULL ||
	    cred->rp.id == NULL) {
		fido_log_debug("%s: cdh=%p, authdata=%p, x5c=%p, sig=%p, "
		    "fmt=%p id=%p, rp.id=%s", __func__, (void *)cred->cdh.ptr,
		    (void *)cred->authdata_cbor.ptr,
		    (void *)cred->attstmt.x5c.ptr,
		    (void *)cred->attstmt.sig.ptr, (void *)cred->fmt,
		    (void *)cred->attcred.id.ptr, cred->rp.id);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto out;
	}

	if (fido_check_rp_id(cred->rp.id, cred->authdata.rp_id_hash) != 0) {
		fido_log_debug("%s: fido_check_rp_id", __func__);
		r = FIDO_ERR_INVALID_PARAM;
		goto out;
	}

	if (fido_check_flags(cred->authdata.flags, FIDO_OPT_TRUE,
	    cred->uv) < 0) {
		fido_log_debug("%s: fido_check_flags", __func__);
		r = FIDO_ERR_INVALID_PARAM;
		goto out;
	}

	if (check_extensions(&cred->authdata_ext, &cred->ext) != 0) {
		fido_log_debug("%s: check_extensions", __func__);
		r = FIDO_ERR_INVALID_PARAM;
		goto out;
	}

	if (!strcmp(cred->fmt, "packed")) {
		if (fido_get_signed_hash(COSE_ES256, &dgst, &cred->cdh,
		    &cred->authdata_cbor) < 0) {
			fido_log_debug("%s: fido_get_signed_hash", __func__);
			r = FIDO_ERR_INTERNAL;
			goto out;
		}
	} else if (!strcmp(cred->fmt, "fido-u2f")) {
		if (get_signed_hash_u2f(&dgst, cred->authdata.rp_id_hash,
		    sizeof(cred->authdata.rp_id_hash), &cred->cdh,
		    &cred->attcred.id, &cred->attcred.pubkey.es256) < 0) {
			fido_log_debug("%s: get_signed_hash_u2f", __func__);
			r = FIDO_ERR_INTERNAL;
			goto out;
		}
	} else if (!strcmp(cred->fmt, "tpm")) {
		if (fido_get_signed_hash_tpm(&dgst, &cred->cdh,
		    &cred->authdata_raw, &cred->attstmt, &cred->attcred) < 0) {
			fido_log_debug("%s: fido_get_signed_hash_tpm", __func__);
			r = FIDO_ERR_INTERNAL;
			goto out;
		}
	} else {
		fido_log_debug("%s: unknown fmt %s", __func__, cred->fmt);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto out;
	}

	if (verify_attstmt(&dgst, &cred->attstmt) < 0) {
		fido_log_debug("%s: verify_attstmt", __func__);
		r = FIDO_ERR_INVALID_SIG;
		goto out;
	}

	r = FIDO_OK;
out:
	explicit_bzero(buf, sizeof(buf));

	return (r);
}

int
fido_cred_verify_self(const fido_cred_t *cred)
{
	unsigned char	buf[1024]; /* XXX */
	fido_blob_t	dgst;
	int		ok = -1;
	int		r;

	dgst.ptr = buf;
	dgst.len = sizeof(buf);

	/* do we have everything we need? */
	if (cred->cdh.ptr == NULL || cred->authdata_cbor.ptr == NULL ||
	    cred->attstmt.x5c.ptr != NULL || cred->attstmt.sig.ptr == NULL ||
	    cred->fmt == NULL || cred->attcred.id.ptr == NULL ||
	    cred->rp.id == NULL) {
		fido_log_debug("%s: cdh=%p, authdata=%p, x5c=%p, sig=%p, "
		    "fmt=%p id=%p, rp.id=%s", __func__, (void *)cred->cdh.ptr,
		    (void *)cred->authdata_cbor.ptr,
		    (void *)cred->attstmt.x5c.ptr,
		    (void *)cred->attstmt.sig.ptr, (void *)cred->fmt,
		    (void *)cred->attcred.id.ptr, cred->rp.id);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto out;
	}

	if (fido_check_rp_id(cred->rp.id, cred->authdata.rp_id_hash) != 0) {
		fido_log_debug("%s: fido_check_rp_id", __func__);
		r = FIDO_ERR_INVALID_PARAM;
		goto out;
	}

	if (fido_check_flags(cred->authdata.flags, FIDO_OPT_TRUE,
	    cred->uv) < 0) {
		fido_log_debug("%s: fido_check_flags", __func__);
		r = FIDO_ERR_INVALID_PARAM;
		goto out;
	}

	if (check_extensions(&cred->authdata_ext, &cred->ext) != 0) {
		fido_log_debug("%s: check_extensions", __func__);
		r = FIDO_ERR_INVALID_PARAM;
		goto out;
	}

	if (!strcmp(cred->fmt, "packed")) {
		if (fido_get_signed_hash(cred->attcred.type, &dgst, &cred->cdh,
		    &cred->authdata_cbor) < 0) {
			fido_log_debug("%s: fido_get_signed_hash", __func__);
			r = FIDO_ERR_INTERNAL;
			goto out;
		}
	} else if (!strcmp(cred->fmt, "fido-u2f")) {
		if (get_signed_hash_u2f(&dgst, cred->authdata.rp_id_hash,
		    sizeof(cred->authdata.rp_id_hash), &cred->cdh,
		    &cred->attcred.id, &cred->attcred.pubkey.es256) < 0) {
			fido_log_debug("%s: get_signed_hash_u2f", __func__);
			r = FIDO_ERR_INTERNAL;
			goto out;
		}
	} else {
		fido_log_debug("%s: unknown fmt %s", __func__, cred->fmt);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto out;
	}

	switch (cred->attcred.type) {
	case COSE_ES256:
		ok = es256_pk_verify_sig(&dgst, &cred->attcred.pubkey.es256,
		    &cred->attstmt.sig);
		break;
	case COSE_RS256:
		ok = rs256_pk_verify_sig(&dgst, &cred->attcred.pubkey.rs256,
		    &cred->attstmt.sig);
		break;
	case COSE_EDDSA:
		ok = eddsa_pk_verify_sig(&dgst, &cred->attcred.pubkey.eddsa,
		    &cred->attstmt.sig);
		break;
	default:
		fido_log_debug("%s: unsupported cose_alg %d", __func__,
		    cred->attcred.type);
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

fido_cred_t *
fido_cred_new(void)
{
	return (calloc(1, sizeof(fido_cred_t)));
}

static void
fido_cred_clean_authdata(fido_cred_t *cred)
{
	fido_blob_reset(&cred->authdata_cbor);
	fido_blob_reset(&cred->authdata_raw);
	fido_blob_reset(&cred->attcred.id);

	memset(&cred->authdata_ext, 0, sizeof(cred->authdata_ext));
	memset(&cred->authdata, 0, sizeof(cred->authdata));
	memset(&cred->attcred, 0, sizeof(cred->attcred));
}

static void
fido_cred_clean_attstmt(fido_attstmt_t *attstmt)
{
	fido_blob_reset(&attstmt->certinfo);
	fido_blob_reset(&attstmt->pubarea);
	fido_blob_reset(&attstmt->cbor);
	fido_blob_reset(&attstmt->x5c);
	fido_blob_reset(&attstmt->sig);

	memset(attstmt, 0, sizeof(*attstmt));
}

void
fido_cred_reset_tx(fido_cred_t *cred)
{
	fido_blob_reset(&cred->cd);
	fido_blob_reset(&cred->cdh);
	fido_blob_reset(&cred->user.id);
	fido_blob_reset(&cred->blob);

	free(cred->rp.id);
	free(cred->rp.name);
	free(cred->user.icon);
	free(cred->user.name);
	free(cred->user.display_name);
	fido_free_blob_array(&cred->excl);

	memset(&cred->rp, 0, sizeof(cred->rp));
	memset(&cred->user, 0, sizeof(cred->user));
	memset(&cred->excl, 0, sizeof(cred->excl));
	memset(&cred->ext, 0, sizeof(cred->ext));

	cred->type = 0;
	cred->rk = FIDO_OPT_OMIT;
	cred->uv = FIDO_OPT_OMIT;
}

void
fido_cred_reset_rx(fido_cred_t *cred)
{
	free(cred->fmt);
	cred->fmt = NULL;
	fido_cred_clean_authdata(cred);
	fido_cred_clean_attstmt(&cred->attstmt);
	fido_blob_reset(&cred->largeblob_key);
}

void
fido_cred_free(fido_cred_t **cred_p)
{
	fido_cred_t *cred;

	if (cred_p == NULL || (cred = *cred_p) == NULL)
		return;
	fido_cred_reset_tx(cred);
	fido_cred_reset_rx(cred);
	free(cred);
	*cred_p = NULL;
}

int
fido_cred_set_authdata(fido_cred_t *cred, const unsigned char *ptr, size_t len)
{
	cbor_item_t		*item = NULL;
	struct cbor_load_result	 cbor;
	int			 r = FIDO_ERR_INVALID_ARGUMENT;

	fido_cred_clean_authdata(cred);

	if (ptr == NULL || len == 0)
		goto fail;

	if ((item = cbor_load(ptr, len, &cbor)) == NULL) {
		fido_log_debug("%s: cbor_load", __func__);
		goto fail;
	}

	if (fido_blob_decode(item, &cred->authdata_raw) < 0) {
		fido_log_debug("%s: fido_blob_decode", __func__);
		goto fail;
	}

	if (cbor_decode_cred_authdata(item, cred->type, &cred->authdata_cbor,
	    &cred->authdata, &cred->attcred, &cred->authdata_ext) < 0) {
		fido_log_debug("%s: cbor_decode_cred_authdata", __func__);
		goto fail;
	}

	r = FIDO_OK;
fail:
	if (item != NULL)
		cbor_decref(&item);

	if (r != FIDO_OK)
		fido_cred_clean_authdata(cred);

	return (r);
}

int
fido_cred_set_authdata_raw(fido_cred_t *cred, const unsigned char *ptr,
    size_t len)
{
	cbor_item_t	*item = NULL;
	int		 r = FIDO_ERR_INVALID_ARGUMENT;

	fido_cred_clean_authdata(cred);

	if (ptr == NULL || len == 0)
		goto fail;

	if (fido_blob_set(&cred->authdata_raw, ptr, len) < 0) {
		fido_log_debug("%s: fido_blob_set", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if ((item = cbor_build_bytestring(ptr, len)) == NULL) {
		fido_log_debug("%s: cbor_build_bytestring", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if (cbor_decode_cred_authdata(item, cred->type, &cred->authdata_cbor,
	    &cred->authdata, &cred->attcred, &cred->authdata_ext) < 0) {
		fido_log_debug("%s: cbor_decode_cred_authdata", __func__);
		goto fail;
	}

	r = FIDO_OK;
fail:
	if (item != NULL)
		cbor_decref(&item);

	if (r != FIDO_OK)
		fido_cred_clean_authdata(cred);

	return (r);
}

int
fido_cred_set_id(fido_cred_t *cred, const unsigned char *ptr, size_t len)
{
	if (fido_blob_set(&cred->attcred.id, ptr, len) < 0)
		return (FIDO_ERR_INVALID_ARGUMENT);

	return (FIDO_OK);
}

int
fido_cred_set_x509(fido_cred_t *cred, const unsigned char *ptr, size_t len)
{
	if (fido_blob_set(&cred->attstmt.x5c, ptr, len) < 0)
		return (FIDO_ERR_INVALID_ARGUMENT);

	return (FIDO_OK);
}

int
fido_cred_set_sig(fido_cred_t *cred, const unsigned char *ptr, size_t len)
{
	if (fido_blob_set(&cred->attstmt.sig, ptr, len) < 0)
		return (FIDO_ERR_INVALID_ARGUMENT);

	return (FIDO_OK);
}

int
fido_cred_set_attstmt(fido_cred_t *cred, const unsigned char *ptr, size_t len)
{
	cbor_item_t		*item = NULL;
	struct cbor_load_result	 cbor;
	int			 r = FIDO_ERR_INVALID_ARGUMENT;

	fido_cred_clean_attstmt(&cred->attstmt);

	if (ptr == NULL || len == 0)
		goto fail;

	if ((item = cbor_load(ptr, len, &cbor)) == NULL) {
		fido_log_debug("%s: cbor_load", __func__);
		goto fail;
	}

	if (cbor_decode_attstmt(item, &cred->attstmt) < 0) {
		fido_log_debug("%s: cbor_decode_attstmt", __func__);
		goto fail;
	}

	r = FIDO_OK;
fail:
	if (item != NULL)
		cbor_decref(&item);

	if (r != FIDO_OK)
		fido_cred_clean_attstmt(&cred->attstmt);

	return (r);
}

int
fido_cred_exclude(fido_cred_t *cred, const unsigned char *id_ptr, size_t id_len)
{
	fido_blob_t id_blob;
	fido_blob_t *list_ptr;

	memset(&id_blob, 0, sizeof(id_blob));

	if (fido_blob_set(&id_blob, id_ptr, id_len) < 0)
		return (FIDO_ERR_INVALID_ARGUMENT);

	if (cred->excl.len == SIZE_MAX) {
		free(id_blob.ptr);
		return (FIDO_ERR_INVALID_ARGUMENT);
	}

	if ((list_ptr = recallocarray(cred->excl.ptr, cred->excl.len,
	    cred->excl.len + 1, sizeof(fido_blob_t))) == NULL) {
		free(id_blob.ptr);
		return (FIDO_ERR_INTERNAL);
	}

	list_ptr[cred->excl.len++] = id_blob;
	cred->excl.ptr = list_ptr;

	return (FIDO_OK);
}

int
fido_cred_set_clientdata(fido_cred_t *cred, const unsigned char *data,
    size_t data_len)
{
	if (!fido_blob_is_empty(&cred->cdh) ||
	    fido_blob_set(&cred->cd, data, data_len) < 0) {
		return (FIDO_ERR_INVALID_ARGUMENT);
	}
	if (fido_sha256(&cred->cdh, data, data_len) < 0) {
		fido_blob_reset(&cred->cd);
		return (FIDO_ERR_INTERNAL);
	}

	return (FIDO_OK);
}

int
fido_cred_set_clientdata_hash(fido_cred_t *cred, const unsigned char *hash,
    size_t hash_len)
{
	if (!fido_blob_is_empty(&cred->cd) ||
	    fido_blob_set(&cred->cdh, hash, hash_len) < 0)
		return (FIDO_ERR_INVALID_ARGUMENT);

	return (FIDO_OK);
}

int
fido_cred_set_rp(fido_cred_t *cred, const char *id, const char *name)
{
	fido_rp_t *rp = &cred->rp;

	if (rp->id != NULL) {
		free(rp->id);
		rp->id = NULL;
	}
	if (rp->name != NULL) {
		free(rp->name);
		rp->name = NULL;
	}

	if (id != NULL && (rp->id = strdup(id)) == NULL)
		goto fail;
	if (name != NULL && (rp->name = strdup(name)) == NULL)
		goto fail;

	return (FIDO_OK);
fail:
	free(rp->id);
	free(rp->name);
	rp->id = NULL;
	rp->name = NULL;

	return (FIDO_ERR_INTERNAL);
}

int
fido_cred_set_user(fido_cred_t *cred, const unsigned char *user_id,
    size_t user_id_len, const char *name, const char *display_name,
    const char *icon)
{
	fido_user_t *up = &cred->user;

	if (up->id.ptr != NULL) {
		free(up->id.ptr);
		up->id.ptr = NULL;
		up->id.len = 0;
	}
	if (up->name != NULL) {
		free(up->name);
		up->name = NULL;
	}
	if (up->display_name != NULL) {
		free(up->display_name);
		up->display_name = NULL;
	}
	if (up->icon != NULL) {
		free(up->icon);
		up->icon = NULL;
	}

	if (user_id != NULL && fido_blob_set(&up->id, user_id, user_id_len) < 0)
		goto fail;
	if (name != NULL && (up->name = strdup(name)) == NULL)
		goto fail;
	if (display_name != NULL &&
	    (up->display_name = strdup(display_name)) == NULL)
		goto fail;
	if (icon != NULL && (up->icon = strdup(icon)) == NULL)
		goto fail;

	return (FIDO_OK);
fail:
	free(up->id.ptr);
	free(up->name);
	free(up->display_name);
	free(up->icon);

	up->id.ptr = NULL;
	up->id.len = 0;
	up->name = NULL;
	up->display_name = NULL;
	up->icon = NULL;

	return (FIDO_ERR_INTERNAL);
}

int
fido_cred_set_extensions(fido_cred_t *cred, int ext)
{
	if (ext == 0)
		cred->ext.mask = 0;
	else {
		if ((ext & FIDO_EXT_CRED_MASK) != ext)
			return (FIDO_ERR_INVALID_ARGUMENT);
		cred->ext.mask |= ext;
	}

	return (FIDO_OK);
}

int
fido_cred_set_options(fido_cred_t *cred, bool rk, bool uv)
{
	cred->rk = rk ? FIDO_OPT_TRUE : FIDO_OPT_FALSE;
	cred->uv = uv ? FIDO_OPT_TRUE : FIDO_OPT_FALSE;

	return (FIDO_OK);
}

int
fido_cred_set_rk(fido_cred_t *cred, fido_opt_t rk)
{
	cred->rk = rk;

	return (FIDO_OK);
}

int
fido_cred_set_uv(fido_cred_t *cred, fido_opt_t uv)
{
	cred->uv = uv;

	return (FIDO_OK);
}

int
fido_cred_set_prot(fido_cred_t *cred, int prot)
{
	if (prot == 0) {
		cred->ext.mask &= ~FIDO_EXT_CRED_PROTECT;
		cred->ext.prot = 0;
	} else {
		if (prot != FIDO_CRED_PROT_UV_OPTIONAL &&
		    prot != FIDO_CRED_PROT_UV_OPTIONAL_WITH_ID &&
		    prot != FIDO_CRED_PROT_UV_REQUIRED)
			return (FIDO_ERR_INVALID_ARGUMENT);

		cred->ext.mask |= FIDO_EXT_CRED_PROTECT;
		cred->ext.prot = prot;
	}

	return (FIDO_OK);
}

int
fido_cred_set_pin_minlen(fido_cred_t *cred, size_t len)
{
	if (len == 0)
		cred->ext.mask &= ~FIDO_EXT_MINPINLEN;
	else
		cred->ext.mask |= FIDO_EXT_MINPINLEN;

	cred->ext.minpinlen = len;

	return (FIDO_OK);
}

int
fido_cred_set_blob(fido_cred_t *cred, const unsigned char *ptr, size_t len)
{
	if (ptr == NULL || len == 0)
		return (FIDO_ERR_INVALID_ARGUMENT);
	if (fido_blob_set(&cred->blob, ptr, len) < 0)
		return (FIDO_ERR_INTERNAL);

	cred->ext.mask |= FIDO_EXT_CRED_BLOB;

	return (FIDO_OK);
}

int
fido_cred_set_fmt(fido_cred_t *cred, const char *fmt)
{
	free(cred->fmt);
	cred->fmt = NULL;

	if (fmt == NULL)
		return (FIDO_ERR_INVALID_ARGUMENT);

	if (strcmp(fmt, "packed") && strcmp(fmt, "fido-u2f") &&
	    strcmp(fmt, "none") && strcmp(fmt, "tpm"))
		return (FIDO_ERR_INVALID_ARGUMENT);

	if ((cred->fmt = strdup(fmt)) == NULL)
		return (FIDO_ERR_INTERNAL);

	return (FIDO_OK);
}

int
fido_cred_set_type(fido_cred_t *cred, int cose_alg)
{
	if ((cose_alg != COSE_ES256 && cose_alg != COSE_RS256 &&
	    cose_alg != COSE_EDDSA) || cred->type != 0)
		return (FIDO_ERR_INVALID_ARGUMENT);

	cred->type = cose_alg;

	return (FIDO_OK);
}

int
fido_cred_type(const fido_cred_t *cred)
{
	return (cred->type);
}

uint8_t
fido_cred_flags(const fido_cred_t *cred)
{
	return (cred->authdata.flags);
}

uint32_t
fido_cred_sigcount(const fido_cred_t *cred)
{
	return (cred->authdata.sigcount);
}

const unsigned char *
fido_cred_clientdata_hash_ptr(const fido_cred_t *cred)
{
	return (cred->cdh.ptr);
}

size_t
fido_cred_clientdata_hash_len(const fido_cred_t *cred)
{
	return (cred->cdh.len);
}

const unsigned char *
fido_cred_x5c_ptr(const fido_cred_t *cred)
{
	return (cred->attstmt.x5c.ptr);
}

size_t
fido_cred_x5c_len(const fido_cred_t *cred)
{
	return (cred->attstmt.x5c.len);
}

const unsigned char *
fido_cred_sig_ptr(const fido_cred_t *cred)
{
	return (cred->attstmt.sig.ptr);
}

size_t
fido_cred_sig_len(const fido_cred_t *cred)
{
	return (cred->attstmt.sig.len);
}

const unsigned char *
fido_cred_authdata_ptr(const fido_cred_t *cred)
{
	return (cred->authdata_cbor.ptr);
}

size_t
fido_cred_authdata_len(const fido_cred_t *cred)
{
	return (cred->authdata_cbor.len);
}

const unsigned char *
fido_cred_authdata_raw_ptr(const fido_cred_t *cred)
{
	return (cred->authdata_raw.ptr);
}

size_t
fido_cred_authdata_raw_len(const fido_cred_t *cred)
{
	return (cred->authdata_raw.len);
}

const unsigned char *
fido_cred_attstmt_ptr(const fido_cred_t *cred)
{
	return (cred->attstmt.cbor.ptr);
}

size_t
fido_cred_attstmt_len(const fido_cred_t *cred)
{
	return (cred->attstmt.cbor.len);
}

const unsigned char *
fido_cred_pubkey_ptr(const fido_cred_t *cred)
{
	const void *ptr;

	switch (cred->attcred.type) {
	case COSE_ES256:
		ptr = &cred->attcred.pubkey.es256;
		break;
	case COSE_RS256:
		ptr = &cred->attcred.pubkey.rs256;
		break;
	case COSE_EDDSA:
		ptr = &cred->attcred.pubkey.eddsa;
		break;
	default:
		ptr = NULL;
		break;
	}

	return (ptr);
}

size_t
fido_cred_pubkey_len(const fido_cred_t *cred)
{
	size_t len;

	switch (cred->attcred.type) {
	case COSE_ES256:
		len = sizeof(cred->attcred.pubkey.es256);
		break;
	case COSE_RS256:
		len = sizeof(cred->attcred.pubkey.rs256);
		break;
	case COSE_EDDSA:
		len = sizeof(cred->attcred.pubkey.eddsa);
		break;
	default:
		len = 0;
		break;
	}

	return (len);
}

const unsigned char *
fido_cred_id_ptr(const fido_cred_t *cred)
{
	return (cred->attcred.id.ptr);
}

size_t
fido_cred_id_len(const fido_cred_t *cred)
{
	return (cred->attcred.id.len);
}

const unsigned char *
fido_cred_aaguid_ptr(const fido_cred_t *cred)
{
	return (cred->attcred.aaguid);
}

size_t
fido_cred_aaguid_len(const fido_cred_t *cred)
{
	return (sizeof(cred->attcred.aaguid));
}

int
fido_cred_prot(const fido_cred_t *cred)
{
	return (cred->ext.prot);
}

size_t
fido_cred_pin_minlen(const fido_cred_t *cred)
{
	return (cred->ext.minpinlen);
}

const char *
fido_cred_fmt(const fido_cred_t *cred)
{
	return (cred->fmt);
}

const char *
fido_cred_rp_id(const fido_cred_t *cred)
{
	return (cred->rp.id);
}

const char *
fido_cred_rp_name(const fido_cred_t *cred)
{
	return (cred->rp.name);
}

const char *
fido_cred_user_name(const fido_cred_t *cred)
{
	return (cred->user.name);
}

const char *
fido_cred_display_name(const fido_cred_t *cred)
{
	return (cred->user.display_name);
}

const unsigned char *
fido_cred_user_id_ptr(const fido_cred_t *cred)
{
	return (cred->user.id.ptr);
}

size_t
fido_cred_user_id_len(const fido_cred_t *cred)
{
	return (cred->user.id.len);
}

const unsigned char *
fido_cred_largeblob_key_ptr(const fido_cred_t *cred)
{
	return (cred->largeblob_key.ptr);
}

size_t
fido_cred_largeblob_key_len(const fido_cred_t *cred)
{
	return (cred->largeblob_key.len);
}
