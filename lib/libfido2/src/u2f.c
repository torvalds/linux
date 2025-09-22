/*
 * Copyright (c) 2018-2021 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <openssl/sha.h>
#include <openssl/x509.h>

#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <errno.h>

#include "fido.h"
#include "fido/es256.h"

#define U2F_PACE_MS (100)

#if defined(_MSC_VER)
static int
usleep(unsigned int usec)
{
	Sleep(usec / 1000);

	return (0);
}
#endif

static int
delay_ms(unsigned int ms, int *ms_remain)
{
	if (*ms_remain > -1 && (unsigned int)*ms_remain < ms)
		ms = (unsigned int)*ms_remain;

	if (ms > UINT_MAX / 1000) {
		fido_log_debug("%s: ms=%u", __func__, ms);
		return (-1);
	}

	if (usleep(ms * 1000) < 0) {
		fido_log_error(errno, "%s: usleep", __func__);
		return (-1);
	}

	if (*ms_remain > -1)
		*ms_remain -= (int)ms;

	return (0);
}

static int
sig_get(fido_blob_t *sig, const unsigned char **buf, size_t *len)
{
	sig->len = *len; /* consume the whole buffer */
	if ((sig->ptr = calloc(1, sig->len)) == NULL ||
	    fido_buf_read(buf, len, sig->ptr, sig->len) < 0) {
		fido_log_debug("%s: fido_buf_read", __func__);
		fido_blob_reset(sig);
		return (-1);
	}

	return (0);
}

static int
x5c_get(fido_blob_t *x5c, const unsigned char **buf, size_t *len)
{
	X509	*cert = NULL;
	int	 ok = -1;

	if (*len > LONG_MAX) {
		fido_log_debug("%s: invalid len %zu", __func__, *len);
		goto fail;
	}

	/* find out the certificate's length */
	const unsigned char *end = *buf;
	if ((cert = d2i_X509(NULL, &end, (long)*len)) == NULL || end <= *buf ||
	    (x5c->len = (size_t)(end - *buf)) >= *len) {
		fido_log_debug("%s: d2i_X509", __func__);
		goto fail;
	}

	/* read accordingly */
	if ((x5c->ptr = calloc(1, x5c->len)) == NULL ||
	    fido_buf_read(buf, len, x5c->ptr, x5c->len) < 0) {
		fido_log_debug("%s: fido_buf_read", __func__);
		goto fail;
	}

	ok = 0;
fail:
	if (cert != NULL)
		X509_free(cert);

	if (ok < 0)
		fido_blob_reset(x5c);

	return (ok);
}

static int
authdata_fake(const char *rp_id, uint8_t flags, uint32_t sigcount,
    fido_blob_t *fake_cbor_ad)
{
	fido_authdata_t	 ad;
	cbor_item_t	*item = NULL;
	size_t		 alloc_len;

	memset(&ad, 0, sizeof(ad));

	if (SHA256((const void *)rp_id, strlen(rp_id),
	    ad.rp_id_hash) != ad.rp_id_hash) {
		fido_log_debug("%s: sha256", __func__);
		return (-1);
	}

	ad.flags = flags; /* XXX translate? */
	ad.sigcount = sigcount;

	if ((item = cbor_build_bytestring((const unsigned char *)&ad,
	    sizeof(ad))) == NULL) {
		fido_log_debug("%s: cbor_build_bytestring", __func__);
		return (-1);
	}

	if (fake_cbor_ad->ptr != NULL ||
	    (fake_cbor_ad->len = cbor_serialize_alloc(item, &fake_cbor_ad->ptr,
	    &alloc_len)) == 0) {
		fido_log_debug("%s: cbor_serialize_alloc", __func__);
		cbor_decref(&item);
		return (-1);
	}

	cbor_decref(&item);

	return (0);
}

/* TODO: use u2f_get_touch_begin & u2f_get_touch_status instead */
static int
send_dummy_register(fido_dev_t *dev, int *ms)
{
	iso7816_apdu_t	*apdu = NULL;
	unsigned char	 challenge[SHA256_DIGEST_LENGTH];
	unsigned char	 application[SHA256_DIGEST_LENGTH];
	unsigned char	 reply[FIDO_MAXMSG];
	int		 r;

	/* dummy challenge & application */
	memset(&challenge, 0xff, sizeof(challenge));
	memset(&application, 0xff, sizeof(application));

	if ((apdu = iso7816_new(0, U2F_CMD_REGISTER, 0, 2 *
	    SHA256_DIGEST_LENGTH)) == NULL ||
	    iso7816_add(apdu, &challenge, sizeof(challenge)) < 0 ||
	    iso7816_add(apdu, &application, sizeof(application)) < 0) {
		fido_log_debug("%s: iso7816", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	do {
		if (fido_tx(dev, CTAP_CMD_MSG, iso7816_ptr(apdu),
		    iso7816_len(apdu), ms) < 0) {
			fido_log_debug("%s: fido_tx", __func__);
			r = FIDO_ERR_TX;
			goto fail;
		}
		if (fido_rx(dev, CTAP_CMD_MSG, &reply, sizeof(reply), ms) < 2) {
			fido_log_debug("%s: fido_rx", __func__);
			r = FIDO_ERR_RX;
			goto fail;
		}
		if (delay_ms(U2F_PACE_MS, ms) != 0) {
			fido_log_debug("%s: delay_ms", __func__);
			r = FIDO_ERR_RX;
			goto fail;
		}
	} while (((reply[0] << 8) | reply[1]) == SW_CONDITIONS_NOT_SATISFIED);

	r = FIDO_OK;
fail:
	iso7816_free(&apdu);

	return (r);
}

static int
key_lookup(fido_dev_t *dev, const char *rp_id, const fido_blob_t *key_id,
    int *found, int *ms)
{
	iso7816_apdu_t	*apdu = NULL;
	unsigned char	 challenge[SHA256_DIGEST_LENGTH];
	unsigned char	 rp_id_hash[SHA256_DIGEST_LENGTH];
	unsigned char	 reply[FIDO_MAXMSG];
	uint8_t		 key_id_len;
	int		 r;

	if (key_id->len > UINT8_MAX || rp_id == NULL) {
		fido_log_debug("%s: key_id->len=%zu, rp_id=%p", __func__,
		    key_id->len, (const void *)rp_id);
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto fail;
	}

	memset(&challenge, 0xff, sizeof(challenge));
	memset(&rp_id_hash, 0, sizeof(rp_id_hash));

	if (SHA256((const void *)rp_id, strlen(rp_id),
	    rp_id_hash) != rp_id_hash) {
		fido_log_debug("%s: sha256", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	key_id_len = (uint8_t)key_id->len;

	if ((apdu = iso7816_new(0, U2F_CMD_AUTH, U2F_AUTH_CHECK, (uint16_t)(2 *
	    SHA256_DIGEST_LENGTH + sizeof(key_id_len) + key_id_len))) == NULL ||
	    iso7816_add(apdu, &challenge, sizeof(challenge)) < 0 ||
	    iso7816_add(apdu, &rp_id_hash, sizeof(rp_id_hash)) < 0 ||
	    iso7816_add(apdu, &key_id_len, sizeof(key_id_len)) < 0 ||
	    iso7816_add(apdu, key_id->ptr, key_id_len) < 0) {
		fido_log_debug("%s: iso7816", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if (fido_tx(dev, CTAP_CMD_MSG, iso7816_ptr(apdu),
	    iso7816_len(apdu), ms) < 0) {
		fido_log_debug("%s: fido_tx", __func__);
		r = FIDO_ERR_TX;
		goto fail;
	}
	if (fido_rx(dev, CTAP_CMD_MSG, &reply, sizeof(reply), ms) != 2) {
		fido_log_debug("%s: fido_rx", __func__);
		r = FIDO_ERR_RX;
		goto fail;
	}

	switch ((reply[0] << 8) | reply[1]) {
	case SW_CONDITIONS_NOT_SATISFIED:
		*found = 1; /* key exists */
		break;
	case SW_WRONG_DATA:
		*found = 0; /* key does not exist */
		break;
	default:
		/* unexpected sw */
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	r = FIDO_OK;
fail:
	iso7816_free(&apdu);

	return (r);
}

static int
parse_auth_reply(fido_blob_t *sig, fido_blob_t *ad, const char *rp_id,
    const unsigned char *reply, size_t len)
{
	uint8_t		flags;
	uint32_t	sigcount;

	if (len < 2 || ((reply[len - 2] << 8) | reply[len - 1]) != SW_NO_ERROR) {
		fido_log_debug("%s: unexpected sw", __func__);
		return (FIDO_ERR_RX);
	}

	len -= 2;

	if (fido_buf_read(&reply, &len, &flags, sizeof(flags)) < 0 ||
	    fido_buf_read(&reply, &len, &sigcount, sizeof(sigcount)) < 0) {
		fido_log_debug("%s: fido_buf_read", __func__);
		return (FIDO_ERR_RX);
	}

	if (sig_get(sig, &reply, &len) < 0) {
		fido_log_debug("%s: sig_get", __func__);
		return (FIDO_ERR_RX);
	}

	if (authdata_fake(rp_id, flags, sigcount, ad) < 0) {
		fido_log_debug("%s; authdata_fake", __func__);
		return (FIDO_ERR_RX);
	}

	return (FIDO_OK);
}

static int
do_auth(fido_dev_t *dev, const fido_blob_t *cdh, const char *rp_id,
    const fido_blob_t *key_id, fido_blob_t *sig, fido_blob_t *ad, int *ms)
{
	iso7816_apdu_t	*apdu = NULL;
	unsigned char	 rp_id_hash[SHA256_DIGEST_LENGTH];
	unsigned char	 reply[FIDO_MAXMSG];
	int		 reply_len;
	uint8_t		 key_id_len;
	int		 r;

#ifdef FIDO_FUZZ
	*ms = 0; /* XXX */
#endif

	if (cdh->len != SHA256_DIGEST_LENGTH || key_id->len > UINT8_MAX ||
	    rp_id == NULL) {
		r = FIDO_ERR_INVALID_ARGUMENT;
		goto fail;
	}

	memset(&rp_id_hash, 0, sizeof(rp_id_hash));

	if (SHA256((const void *)rp_id, strlen(rp_id),
	    rp_id_hash) != rp_id_hash) {
		fido_log_debug("%s: sha256", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	key_id_len = (uint8_t)key_id->len;

	if ((apdu = iso7816_new(0, U2F_CMD_AUTH, U2F_AUTH_SIGN, (uint16_t)(2 *
	    SHA256_DIGEST_LENGTH + sizeof(key_id_len) + key_id_len))) == NULL ||
	    iso7816_add(apdu, cdh->ptr, cdh->len) < 0 ||
	    iso7816_add(apdu, &rp_id_hash, sizeof(rp_id_hash)) < 0 ||
	    iso7816_add(apdu, &key_id_len, sizeof(key_id_len)) < 0 ||
	    iso7816_add(apdu, key_id->ptr, key_id_len) < 0) {
		fido_log_debug("%s: iso7816", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	do {
		if (fido_tx(dev, CTAP_CMD_MSG, iso7816_ptr(apdu),
		    iso7816_len(apdu), ms) < 0) {
			fido_log_debug("%s: fido_tx", __func__);
			r = FIDO_ERR_TX;
			goto fail;
		}
		if ((reply_len = fido_rx(dev, CTAP_CMD_MSG, &reply,
		    sizeof(reply), ms)) < 2) {
			fido_log_debug("%s: fido_rx", __func__);
			r = FIDO_ERR_RX;
			goto fail;
		}
		if (delay_ms(U2F_PACE_MS, ms) != 0) {
			fido_log_debug("%s: delay_ms", __func__);
			r = FIDO_ERR_RX;
			goto fail;
		}
	} while (((reply[0] << 8) | reply[1]) == SW_CONDITIONS_NOT_SATISFIED);

	if ((r = parse_auth_reply(sig, ad, rp_id, reply,
	    (size_t)reply_len)) != FIDO_OK) {
		fido_log_debug("%s: parse_auth_reply", __func__);
		goto fail;
	}

fail:
	iso7816_free(&apdu);

	return (r);
}

static int
cbor_blob_from_ec_point(const uint8_t *ec_point, size_t ec_point_len,
    fido_blob_t *cbor_blob)
{
	es256_pk_t	*pk = NULL;
	cbor_item_t	*pk_cbor = NULL;
	size_t		 alloc_len;
	int		 ok = -1;

	/* only handle uncompressed points */
	if (ec_point_len != 65 || ec_point[0] != 0x04) {
		fido_log_debug("%s: unexpected format", __func__);
		goto fail;
	}

	if ((pk = es256_pk_new()) == NULL ||
	    es256_pk_set_x(pk, &ec_point[1]) < 0 ||
	    es256_pk_set_y(pk, &ec_point[33]) < 0) {
		fido_log_debug("%s: es256_pk_set", __func__);
		goto fail;
	}

	if ((pk_cbor = es256_pk_encode(pk, 0)) == NULL) {
		fido_log_debug("%s: es256_pk_encode", __func__);
		goto fail;
	}

	if ((cbor_blob->len = cbor_serialize_alloc(pk_cbor, &cbor_blob->ptr,
	    &alloc_len)) != 77) {
		fido_log_debug("%s: cbor_serialize_alloc", __func__);
		goto fail;
	}

	ok = 0;
fail:
	es256_pk_free(&pk);

	if (pk_cbor)
		cbor_decref(&pk_cbor);

	return (ok);
}

static int
encode_cred_attstmt(int cose_alg, const fido_blob_t *x5c,
    const fido_blob_t *sig, fido_blob_t *out)
{
	cbor_item_t		*item = NULL;
	cbor_item_t		*x5c_cbor = NULL;
	const uint8_t		 alg_cbor = (uint8_t)(-cose_alg - 1);
	struct cbor_pair	 kv[3];
	size_t			 alloc_len;
	int			 ok = -1;

	memset(&kv, 0, sizeof(kv));
	memset(out, 0, sizeof(*out));

	if ((item = cbor_new_definite_map(3)) == NULL) {
		fido_log_debug("%s: cbor_new_definite_map", __func__);
		goto fail;
	}

	if ((kv[0].key = cbor_build_string("alg")) == NULL ||
	    (kv[0].value = cbor_build_negint8(alg_cbor)) == NULL ||
	    !cbor_map_add(item, kv[0])) {
		fido_log_debug("%s: alg", __func__);
		goto fail;
	}

	if ((kv[1].key = cbor_build_string("sig")) == NULL ||
	    (kv[1].value = fido_blob_encode(sig)) == NULL ||
	    !cbor_map_add(item, kv[1])) {
		fido_log_debug("%s: sig", __func__);
		goto fail;
	}

	if ((kv[2].key = cbor_build_string("x5c")) == NULL ||
	    (kv[2].value = cbor_new_definite_array(1)) == NULL ||
	    (x5c_cbor = fido_blob_encode(x5c)) == NULL ||
	    !cbor_array_push(kv[2].value, x5c_cbor) ||
	    !cbor_map_add(item, kv[2])) {
		fido_log_debug("%s: x5c", __func__);
		goto fail;
	}

	if ((out->len = cbor_serialize_alloc(item, &out->ptr,
	    &alloc_len)) == 0) {
		fido_log_debug("%s: cbor_serialize_alloc", __func__);
		goto fail;
	}

	ok = 0;
fail:
	if (item != NULL)
		cbor_decref(&item);
	if (x5c_cbor != NULL)
		cbor_decref(&x5c_cbor);

	for (size_t i = 0; i < nitems(kv); i++) {
		if (kv[i].key)
			cbor_decref(&kv[i].key);
		if (kv[i].value)
			cbor_decref(&kv[i].value);
	}

	return (ok);
}

static int
encode_cred_authdata(const char *rp_id, const uint8_t *kh, uint8_t kh_len,
    const uint8_t *pubkey, size_t pubkey_len, fido_blob_t *out)
{
	fido_authdata_t	 	 authdata;
	fido_attcred_raw_t	 attcred_raw;
	fido_blob_t		 pk_blob;
	fido_blob_t		 authdata_blob;
	cbor_item_t		*authdata_cbor = NULL;
	unsigned char		*ptr;
	size_t			 len;
	size_t			 alloc_len;
	int			 ok = -1;

	memset(&pk_blob, 0, sizeof(pk_blob));
	memset(&authdata, 0, sizeof(authdata));
	memset(&authdata_blob, 0, sizeof(authdata_blob));
	memset(out, 0, sizeof(*out));

	if (rp_id == NULL) {
		fido_log_debug("%s: NULL rp_id", __func__);
		goto fail;
	}

	if (cbor_blob_from_ec_point(pubkey, pubkey_len, &pk_blob) < 0) {
		fido_log_debug("%s: cbor_blob_from_ec_point", __func__);
		goto fail;
	}

	if (SHA256((const void *)rp_id, strlen(rp_id),
	    authdata.rp_id_hash) != authdata.rp_id_hash) {
		fido_log_debug("%s: sha256", __func__);
		goto fail;
	}

	authdata.flags = (CTAP_AUTHDATA_ATT_CRED | CTAP_AUTHDATA_USER_PRESENT);
	authdata.sigcount = 0;

	memset(&attcred_raw.aaguid, 0, sizeof(attcred_raw.aaguid));
	attcred_raw.id_len = htobe16(kh_len);

	len = authdata_blob.len = sizeof(authdata) + sizeof(attcred_raw) +
	    kh_len + pk_blob.len;
	ptr = authdata_blob.ptr = calloc(1, authdata_blob.len);

	fido_log_debug("%s: ptr=%p, len=%zu", __func__, (void *)ptr, len);

	if (authdata_blob.ptr == NULL)
		goto fail;

	if (fido_buf_write(&ptr, &len, &authdata, sizeof(authdata)) < 0 ||
	    fido_buf_write(&ptr, &len, &attcred_raw, sizeof(attcred_raw)) < 0 ||
	    fido_buf_write(&ptr, &len, kh, kh_len) < 0 ||
	    fido_buf_write(&ptr, &len, pk_blob.ptr, pk_blob.len) < 0) {
		fido_log_debug("%s: fido_buf_write", __func__);
		goto fail;
	}

	if ((authdata_cbor = fido_blob_encode(&authdata_blob)) == NULL) {
		fido_log_debug("%s: fido_blob_encode", __func__);
		goto fail;
	}

	if ((out->len = cbor_serialize_alloc(authdata_cbor, &out->ptr,
	    &alloc_len)) == 0) {
		fido_log_debug("%s: cbor_serialize_alloc", __func__);
		goto fail;
	}

	ok = 0;
fail:
	if (authdata_cbor)
		cbor_decref(&authdata_cbor);

	fido_blob_reset(&pk_blob);
	fido_blob_reset(&authdata_blob);

	return (ok);
}

static int
parse_register_reply(fido_cred_t *cred, const unsigned char *reply, size_t len)
{
	fido_blob_t	 x5c;
	fido_blob_t	 sig;
	fido_blob_t	 ad;
	fido_blob_t	 stmt;
	uint8_t		 dummy;
	uint8_t		 pubkey[65];
	uint8_t		 kh_len = 0;
	uint8_t		*kh = NULL;
	int		 r;

	memset(&x5c, 0, sizeof(x5c));
	memset(&sig, 0, sizeof(sig));
	memset(&ad, 0, sizeof(ad));
	memset(&stmt, 0, sizeof(stmt));
	r = FIDO_ERR_RX;

	/* status word */
	if (len < 2 || ((reply[len - 2] << 8) | reply[len - 1]) != SW_NO_ERROR) {
		fido_log_debug("%s: unexpected sw", __func__);
		goto fail;
	}

	len -= 2;

	/* reserved byte */
	if (fido_buf_read(&reply, &len, &dummy, sizeof(dummy)) < 0 ||
	    dummy != 0x05) {
		fido_log_debug("%s: reserved byte", __func__);
		goto fail;
	}

	/* pubkey + key handle */
	if (fido_buf_read(&reply, &len, &pubkey, sizeof(pubkey)) < 0 ||
	    fido_buf_read(&reply, &len, &kh_len, sizeof(kh_len)) < 0 ||
	    (kh = calloc(1, kh_len)) == NULL ||
	    fido_buf_read(&reply, &len, kh, kh_len) < 0) {
		fido_log_debug("%s: fido_buf_read", __func__);
		goto fail;
	}

	/* x5c + sig */
	if (x5c_get(&x5c, &reply, &len) < 0 ||
	    sig_get(&sig, &reply, &len) < 0) {
		fido_log_debug("%s: x5c || sig", __func__);
		goto fail;
	}

	/* attstmt */
	if (encode_cred_attstmt(COSE_ES256, &x5c, &sig, &stmt) < 0) {
		fido_log_debug("%s: encode_cred_attstmt", __func__);
		goto fail;
	}

	/* authdata */
	if (encode_cred_authdata(cred->rp.id, kh, kh_len, pubkey,
	    sizeof(pubkey), &ad) < 0) {
		fido_log_debug("%s: encode_cred_authdata", __func__);
		goto fail;
	}

	if (fido_cred_set_fmt(cred, "fido-u2f") != FIDO_OK ||
	    fido_cred_set_authdata(cred, ad.ptr, ad.len) != FIDO_OK ||
	    fido_cred_set_attstmt(cred, stmt.ptr, stmt.len) != FIDO_OK) {
		fido_log_debug("%s: fido_cred_set", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	r = FIDO_OK;
fail:
	freezero(kh, kh_len);
	fido_blob_reset(&x5c);
	fido_blob_reset(&sig);
	fido_blob_reset(&ad);
	fido_blob_reset(&stmt);

	return (r);
}

int
u2f_register(fido_dev_t *dev, fido_cred_t *cred, int *ms)
{
	iso7816_apdu_t	*apdu = NULL;
	unsigned char	 rp_id_hash[SHA256_DIGEST_LENGTH];
	unsigned char	 reply[FIDO_MAXMSG];
	int		 reply_len;
	int		 found;
	int		 r;

	if (cred->rk == FIDO_OPT_TRUE || cred->uv == FIDO_OPT_TRUE) {
		fido_log_debug("%s: rk=%d, uv=%d", __func__, cred->rk,
		    cred->uv);
		return (FIDO_ERR_UNSUPPORTED_OPTION);
	}

	if (cred->type != COSE_ES256 || cred->cdh.ptr == NULL ||
	    cred->rp.id == NULL || cred->cdh.len != SHA256_DIGEST_LENGTH) {
		fido_log_debug("%s: type=%d, cdh=(%p,%zu)" , __func__,
		    cred->type, (void *)cred->cdh.ptr, cred->cdh.len);
		return (FIDO_ERR_INVALID_ARGUMENT);
	}

	for (size_t i = 0; i < cred->excl.len; i++) {
		if ((r = key_lookup(dev, cred->rp.id, &cred->excl.ptr[i],
		    &found, ms)) != FIDO_OK) {
			fido_log_debug("%s: key_lookup", __func__);
			return (r);
		}
		if (found) {
			if ((r = send_dummy_register(dev, ms)) != FIDO_OK) {
				fido_log_debug("%s: send_dummy_register",
				    __func__);
				return (r);
			}
			return (FIDO_ERR_CREDENTIAL_EXCLUDED);
		}
	}

	memset(&rp_id_hash, 0, sizeof(rp_id_hash));

	if (SHA256((const void *)cred->rp.id, strlen(cred->rp.id),
	    rp_id_hash) != rp_id_hash) {
		fido_log_debug("%s: sha256", __func__);
		return (FIDO_ERR_INTERNAL);
	}

	if ((apdu = iso7816_new(0, U2F_CMD_REGISTER, 0, 2 *
	    SHA256_DIGEST_LENGTH)) == NULL ||
	    iso7816_add(apdu, cred->cdh.ptr, cred->cdh.len) < 0 ||
	    iso7816_add(apdu, rp_id_hash, sizeof(rp_id_hash)) < 0) {
		fido_log_debug("%s: iso7816", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	do {
		if (fido_tx(dev, CTAP_CMD_MSG, iso7816_ptr(apdu),
		    iso7816_len(apdu), ms) < 0) {
			fido_log_debug("%s: fido_tx", __func__);
			r = FIDO_ERR_TX;
			goto fail;
		}
		if ((reply_len = fido_rx(dev, CTAP_CMD_MSG, &reply,
		    sizeof(reply), ms)) < 2) {
			fido_log_debug("%s: fido_rx", __func__);
			r = FIDO_ERR_RX;
			goto fail;
		}
		if (delay_ms(U2F_PACE_MS, ms) != 0) {
			fido_log_debug("%s: delay_ms", __func__);
			r = FIDO_ERR_RX;
			goto fail;
		}
	} while (((reply[0] << 8) | reply[1]) == SW_CONDITIONS_NOT_SATISFIED);

	if ((r = parse_register_reply(cred, reply,
	    (size_t)reply_len)) != FIDO_OK) {
		fido_log_debug("%s: parse_register_reply", __func__);
		goto fail;
	}
fail:
	iso7816_free(&apdu);

	return (r);
}

static int
u2f_authenticate_single(fido_dev_t *dev, const fido_blob_t *key_id,
    fido_assert_t *fa, size_t idx, int *ms)
{
	fido_blob_t	sig;
	fido_blob_t	ad;
	int		found;
	int		r;

	memset(&sig, 0, sizeof(sig));
	memset(&ad, 0, sizeof(ad));

	if ((r = key_lookup(dev, fa->rp_id, key_id, &found, ms)) != FIDO_OK) {
		fido_log_debug("%s: key_lookup", __func__);
		goto fail;
	}

	if (!found) {
		fido_log_debug("%s: not found", __func__);
		r = FIDO_ERR_CREDENTIAL_EXCLUDED;
		goto fail;
	}

	if (fido_blob_set(&fa->stmt[idx].id, key_id->ptr, key_id->len) < 0) {
		fido_log_debug("%s: fido_blob_set", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if (fa->up == FIDO_OPT_FALSE) {
		fido_log_debug("%s: checking for key existence only", __func__);
		r = FIDO_ERR_USER_PRESENCE_REQUIRED;
		goto fail;
	}

	if ((r = do_auth(dev, &fa->cdh, fa->rp_id, key_id, &sig, &ad,
	    ms)) != FIDO_OK) {
		fido_log_debug("%s: do_auth", __func__);
		goto fail;
	}

	if (fido_assert_set_authdata(fa, idx, ad.ptr, ad.len) != FIDO_OK ||
	    fido_assert_set_sig(fa, idx, sig.ptr, sig.len) != FIDO_OK) {
		fido_log_debug("%s: fido_assert_set", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	r = FIDO_OK;
fail:
	fido_blob_reset(&sig);
	fido_blob_reset(&ad);

	return (r);
}

int
u2f_authenticate(fido_dev_t *dev, fido_assert_t *fa, int *ms)
{
	size_t	nfound = 0;
	size_t	nauth_ok = 0;
	int	r;

	if (fa->uv == FIDO_OPT_TRUE || fa->allow_list.ptr == NULL) {
		fido_log_debug("%s: uv=%d, allow_list=%p", __func__, fa->uv,
		    (void *)fa->allow_list.ptr);
		return (FIDO_ERR_UNSUPPORTED_OPTION);
	}

	if ((r = fido_assert_set_count(fa, fa->allow_list.len)) != FIDO_OK) {
		fido_log_debug("%s: fido_assert_set_count", __func__);
		return (r);
	}

	for (size_t i = 0; i < fa->allow_list.len; i++) {
		switch ((r = u2f_authenticate_single(dev,
		    &fa->allow_list.ptr[i], fa, nfound, ms))) {
		case FIDO_OK:
			nauth_ok++;
			/* FALLTHROUGH */
		case FIDO_ERR_USER_PRESENCE_REQUIRED:
			nfound++;
			break;
		default:
			if (r != FIDO_ERR_CREDENTIAL_EXCLUDED) {
				fido_log_debug("%s: u2f_authenticate_single",
				    __func__);
				return (r);
			}
			/* ignore credentials that don't exist */
		}
	}

	fa->stmt_len = nfound;

	if (nfound == 0)
		return (FIDO_ERR_NO_CREDENTIALS);
	if (nauth_ok == 0)
		return (FIDO_ERR_USER_PRESENCE_REQUIRED);

	return (FIDO_OK);
}

int
u2f_get_touch_begin(fido_dev_t *dev, int *ms)
{
	iso7816_apdu_t	*apdu = NULL;
	const char	*clientdata = FIDO_DUMMY_CLIENTDATA;
	const char	*rp_id = FIDO_DUMMY_RP_ID;
	unsigned char	 clientdata_hash[SHA256_DIGEST_LENGTH];
	unsigned char	 rp_id_hash[SHA256_DIGEST_LENGTH];
	unsigned char	 reply[FIDO_MAXMSG];
	int		 r;

	memset(&clientdata_hash, 0, sizeof(clientdata_hash));
	memset(&rp_id_hash, 0, sizeof(rp_id_hash));

	if (SHA256((const void *)clientdata, strlen(clientdata),
	    clientdata_hash) != clientdata_hash || SHA256((const void *)rp_id,
	    strlen(rp_id), rp_id_hash) != rp_id_hash) {
		fido_log_debug("%s: sha256", __func__);
		return (FIDO_ERR_INTERNAL);
	}

	if ((apdu = iso7816_new(0, U2F_CMD_REGISTER, 0, 2 *
	    SHA256_DIGEST_LENGTH)) == NULL ||
	    iso7816_add(apdu, clientdata_hash, sizeof(clientdata_hash)) < 0 ||
	    iso7816_add(apdu, rp_id_hash, sizeof(rp_id_hash)) < 0) {
		fido_log_debug("%s: iso7816", __func__);
		r = FIDO_ERR_INTERNAL;
		goto fail;
	}

	if (dev->attr.flags & FIDO_CAP_WINK) {
		fido_tx(dev, CTAP_CMD_WINK, NULL, 0, ms);
		fido_rx(dev, CTAP_CMD_WINK, &reply, sizeof(reply), ms);
	}

	if (fido_tx(dev, CTAP_CMD_MSG, iso7816_ptr(apdu),
	    iso7816_len(apdu), ms) < 0) {
		fido_log_debug("%s: fido_tx", __func__);
		r = FIDO_ERR_TX;
		goto fail;
	}

	r = FIDO_OK;
fail:
	iso7816_free(&apdu);

	return (r);
}

int
u2f_get_touch_status(fido_dev_t *dev, int *touched, int *ms)
{
	unsigned char	reply[FIDO_MAXMSG];
	int		reply_len;
	int		r;

	if ((reply_len = fido_rx(dev, CTAP_CMD_MSG, &reply, sizeof(reply),
	    ms)) < 2) {
		fido_log_debug("%s: fido_rx", __func__);
		return (FIDO_OK); /* ignore */
	}

	switch ((reply[reply_len - 2] << 8) | reply[reply_len - 1]) {
	case SW_CONDITIONS_NOT_SATISFIED:
		if ((r = u2f_get_touch_begin(dev, ms)) != FIDO_OK) {
			fido_log_debug("%s: u2f_get_touch_begin", __func__);
			return (r);
		}
		*touched = 0;
		break;
	case SW_NO_ERROR:
		*touched = 1;
		break;
	default:
		fido_log_debug("%s: unexpected sw", __func__);
		return (FIDO_ERR_RX);
	}

	return (FIDO_OK);
}
