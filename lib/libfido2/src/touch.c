/*
 * Copyright (c) 2018-2022 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <openssl/sha.h>
#include "fido.h"

int
fido_dev_get_touch_begin(fido_dev_t *dev)
{
	fido_blob_t	 f;
	cbor_item_t	*argv[9];
	const char	*clientdata = FIDO_DUMMY_CLIENTDATA;
	const uint8_t	 user_id = FIDO_DUMMY_USER_ID;
	unsigned char	 cdh[SHA256_DIGEST_LENGTH];
	fido_rp_t	 rp;
	fido_user_t	 user;
	int		 ms = dev->timeout_ms;
	int		 r = FIDO_ERR_INTERNAL;

	memset(&f, 0, sizeof(f));
	memset(argv, 0, sizeof(argv));
	memset(cdh, 0, sizeof(cdh));
	memset(&rp, 0, sizeof(rp));
	memset(&user, 0, sizeof(user));

	if (fido_dev_is_fido2(dev) == false)
		return (u2f_get_touch_begin(dev, &ms));

	if (SHA256((const void *)clientdata, strlen(clientdata), cdh) != cdh) {
		fido_log_debug("%s: sha256", __func__);
		return (FIDO_ERR_INTERNAL);
	}

	if ((rp.id = strdup(FIDO_DUMMY_RP_ID)) == NULL ||
	    (user.name = strdup(FIDO_DUMMY_USER_NAME)) == NULL) {
		fido_log_debug("%s: strdup", __func__);
		goto fail;
	}

	if (fido_blob_set(&user.id, &user_id, sizeof(user_id)) < 0) {
		fido_log_debug("%s: fido_blob_set", __func__);
		goto fail;
	}

	if ((argv[0] = cbor_build_bytestring(cdh, sizeof(cdh))) == NULL ||
	    (argv[1] = cbor_encode_rp_entity(&rp)) == NULL ||
	    (argv[2] = cbor_encode_user_entity(&user)) == NULL ||
	    (argv[3] = cbor_encode_pubkey_param(COSE_ES256)) == NULL) {
		fido_log_debug("%s: cbor encode", __func__);
		goto fail;
	}

	if (fido_dev_supports_pin(dev)) {
		if ((argv[7] = cbor_new_definite_bytestring()) == NULL ||
		    (argv[8] = cbor_encode_pin_opt(dev)) == NULL) {
			fido_log_debug("%s: cbor encode", __func__);
			goto fail;
		}
	}

	if (cbor_build_frame(CTAP_CBOR_MAKECRED, argv, nitems(argv), &f) < 0 ||
	    fido_tx(dev, CTAP_CMD_CBOR, f.ptr, f.len, &ms) < 0) {
		fido_log_debug("%s: fido_tx", __func__);
		r = FIDO_ERR_TX;
		goto fail;
	}

	r = FIDO_OK;
fail:
	cbor_vector_free(argv, nitems(argv));
	free(f.ptr);
	free(rp.id);
	free(user.name);
	free(user.id.ptr);

	return (r);
}

int
fido_dev_get_touch_status(fido_dev_t *dev, int *touched, int ms)
{
	int r;

	*touched = 0;

	if (fido_dev_is_fido2(dev) == false)
		return (u2f_get_touch_status(dev, touched, &ms));

	switch ((r = fido_rx_cbor_status(dev, &ms))) {
	case FIDO_ERR_PIN_AUTH_INVALID:
	case FIDO_ERR_PIN_INVALID:
	case FIDO_ERR_PIN_NOT_SET:
	case FIDO_ERR_SUCCESS:
		*touched = 1;
		break;
	case FIDO_ERR_RX:
		/* ignore */
		break;
	default:
		fido_log_debug("%s: fido_rx_cbor_status", __func__);
		return (r);
	}

	return (FIDO_OK);
}
