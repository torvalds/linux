/*
 * Copyright (c) 2020-2022 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include <stdio.h>
#include <string.h>

#include "fido.h"
#include "fido/param.h"
#include "iso7816.h"

#define TX_CHUNK_SIZE	240

static const uint8_t aid[] = { 0xa0, 0x00, 0x00, 0x06, 0x47, 0x2f, 0x00, 0x01 };
static const uint8_t v_u2f[] = { 'U', '2', 'F', '_', 'V', '2' };
static const uint8_t v_fido[] = { 'F', 'I', 'D', 'O', '_', '2', '_', '0' };

static int
tx_short_apdu(fido_dev_t *d, const iso7816_header_t *h, const uint8_t *payload,
    uint8_t payload_len, uint8_t cla_flags)
{
	uint8_t apdu[5 + UINT8_MAX + 1];
	uint8_t sw[2];
	size_t apdu_len;
	int ok = -1;

	memset(&apdu, 0, sizeof(apdu));
	apdu[0] = h->cla | cla_flags;
	apdu[1] = h->ins;
	apdu[2] = h->p1;
	apdu[3] = h->p2;
	apdu[4] = payload_len;
	memcpy(&apdu[5], payload, payload_len);
	apdu_len = (size_t)(5 + payload_len + 1);

	if (d->io.write(d->io_handle, apdu, apdu_len) < 0) {
		fido_log_debug("%s: write", __func__);
		goto fail;
	}

	if (cla_flags & 0x10) {
		if (d->io.read(d->io_handle, sw, sizeof(sw), -1) != 2) {
			fido_log_debug("%s: read", __func__);
			goto fail;
		}
		if ((sw[0] << 8 | sw[1]) != SW_NO_ERROR) {
			fido_log_debug("%s: unexpected sw", __func__);
			goto fail;
		}
	}

	ok = 0;
fail:
	explicit_bzero(apdu, sizeof(apdu));

	return ok;
}

static int
nfc_do_tx(fido_dev_t *d, const uint8_t *apdu_ptr, size_t apdu_len)
{
	iso7816_header_t h;

	if (fido_buf_read(&apdu_ptr, &apdu_len, &h, sizeof(h)) < 0) {
		fido_log_debug("%s: header", __func__);
		return -1;
	}
	if (apdu_len < 2) {
		fido_log_debug("%s: apdu_len %zu", __func__, apdu_len);
		return -1;
	}

	apdu_len -= 2; /* trim le1 le2 */

	while (apdu_len > TX_CHUNK_SIZE) {
		if (tx_short_apdu(d, &h, apdu_ptr, TX_CHUNK_SIZE, 0x10) < 0) {
			fido_log_debug("%s: chain", __func__);
			return -1;
		}
		apdu_ptr += TX_CHUNK_SIZE;
		apdu_len -= TX_CHUNK_SIZE;
	}

	if (tx_short_apdu(d, &h, apdu_ptr, (uint8_t)apdu_len, 0) < 0) {
		fido_log_debug("%s: tx_short_apdu", __func__);
		return -1;
	}

	return 0;
}

int
fido_nfc_tx(fido_dev_t *d, uint8_t cmd, const unsigned char *buf, size_t count)
{
	iso7816_apdu_t *apdu = NULL;
	const uint8_t *ptr;
	size_t len;
	int ok = -1;

	switch (cmd) {
	case CTAP_CMD_INIT: /* select */
		if ((apdu = iso7816_new(0, 0xa4, 0x04, sizeof(aid))) == NULL ||
		    iso7816_add(apdu, aid, sizeof(aid)) < 0) {
			fido_log_debug("%s: iso7816", __func__);
			goto fail;
		}
		break;
	case CTAP_CMD_CBOR: /* wrap cbor */
		if (count > UINT16_MAX || (apdu = iso7816_new(0x80, 0x10, 0x00,
		    (uint16_t)count)) == NULL ||
		    iso7816_add(apdu, buf, count) < 0) {
			fido_log_debug("%s: iso7816", __func__);
			goto fail;
		}
		break;
	case CTAP_CMD_MSG: /* already an apdu */
		break;
	default:
		fido_log_debug("%s: cmd=%02x", __func__, cmd);
		goto fail;
	}

	if (apdu != NULL) {
		ptr = iso7816_ptr(apdu);
		len = iso7816_len(apdu);
	} else {
		ptr = buf;
		len = count;
	}

	if (nfc_do_tx(d, ptr, len) < 0) {
		fido_log_debug("%s: nfc_do_tx", __func__);
		goto fail;
	}

	ok = 0;
fail:
	iso7816_free(&apdu);

	return ok;
}

static int
rx_init(fido_dev_t *d, unsigned char *buf, size_t count, int ms)
{
	fido_ctap_info_t *attr = (fido_ctap_info_t *)buf;
	uint8_t f[64];
	int n;

	if (count != sizeof(*attr)) {
		fido_log_debug("%s: count=%zu", __func__, count);
		return -1;
	}

	memset(attr, 0, sizeof(*attr));

	if ((n = d->io.read(d->io_handle, f, sizeof(f), ms)) < 2 ||
	    (f[n - 2] << 8 | f[n - 1]) != SW_NO_ERROR) {
		fido_log_debug("%s: read", __func__);
		return -1;
	}

	n -= 2;

	if (n == sizeof(v_u2f) && memcmp(f, v_u2f, sizeof(v_u2f)) == 0)
		attr->flags = FIDO_CAP_CBOR;
	else if (n == sizeof(v_fido) && memcmp(f, v_fido, sizeof(v_fido)) == 0)
		attr->flags = FIDO_CAP_CBOR | FIDO_CAP_NMSG;
	else {
		fido_log_debug("%s: unknown version string", __func__);
#ifdef FIDO_FUZZ
		attr->flags = FIDO_CAP_CBOR | FIDO_CAP_NMSG;
#else
		return -1;
#endif
	}

	memcpy(&attr->nonce, &d->nonce, sizeof(attr->nonce)); /* XXX */

	return (int)count;
}

static int
tx_get_response(fido_dev_t *d, uint8_t count)
{
	uint8_t apdu[5];

	memset(apdu, 0, sizeof(apdu));
	apdu[1] = 0xc0; /* GET_RESPONSE */
	apdu[4] = count;

	if (d->io.write(d->io_handle, apdu, sizeof(apdu)) < 0) {
		fido_log_debug("%s: write", __func__);
		return -1;
	}

	return 0;
}

static int
rx_apdu(fido_dev_t *d, uint8_t sw[2], unsigned char **buf, size_t *count, int *ms)
{
	uint8_t f[256 + 2];
	struct timespec ts;
	int n, ok = -1;

	if (fido_time_now(&ts) != 0)
		goto fail;

	if ((n = d->io.read(d->io_handle, f, sizeof(f), *ms)) < 2) {
		fido_log_debug("%s: read", __func__);
		goto fail;
	}

	if (fido_time_delta(&ts, ms) != 0)
		goto fail;

	if (fido_buf_write(buf, count, f, (size_t)(n - 2)) < 0) {
		fido_log_debug("%s: fido_buf_write", __func__);
		goto fail;
	}

	memcpy(sw, f + n - 2, 2);

	ok = 0;
fail:
	explicit_bzero(f, sizeof(f));

	return ok;
}

static int
rx_msg(fido_dev_t *d, unsigned char *buf, size_t count, int ms)
{
	uint8_t sw[2];
	const size_t bufsiz = count;

	if (rx_apdu(d, sw, &buf, &count, &ms) < 0) {
		fido_log_debug("%s: preamble", __func__);
		return -1;
	}

	while (sw[0] == SW1_MORE_DATA)
		if (tx_get_response(d, sw[1]) < 0 ||
		    rx_apdu(d, sw, &buf, &count, &ms) < 0) {
			fido_log_debug("%s: chain", __func__);
			return -1;
		}

	if (fido_buf_write(&buf, &count, sw, sizeof(sw)) < 0) {
		fido_log_debug("%s: sw", __func__);
		return -1;
	}

	if (bufsiz - count > INT_MAX) {
		fido_log_debug("%s: bufsiz", __func__);
		return -1;
	}

	return (int)(bufsiz - count);
}

static int
rx_cbor(fido_dev_t *d, unsigned char *buf, size_t count, int ms)
{
	int r;

	if ((r = rx_msg(d, buf, count, ms)) < 2)
		return -1;

	return r - 2;
}

int
fido_nfc_rx(fido_dev_t *d, uint8_t cmd, unsigned char *buf, size_t count, int ms)
{
	switch (cmd) {
	case CTAP_CMD_INIT:
		return rx_init(d, buf, count, ms);
	case CTAP_CMD_CBOR:
		return rx_cbor(d, buf, count, ms);
	case CTAP_CMD_MSG:
		return rx_msg(d, buf, count, ms);
	default:
		fido_log_debug("%s: cmd=%02x", __func__, cmd);
		return -1;
	}
}

#ifdef USE_NFC
bool
fido_is_nfc(const char *path)
{
	return strncmp(path, FIDO_NFC_PREFIX, strlen(FIDO_NFC_PREFIX)) == 0;
}

int
fido_dev_set_nfc(fido_dev_t *d)
{
	if (d->io_handle != NULL) {
		fido_log_debug("%s: device open", __func__);
		return -1;
	}
	d->io_own = true;
	d->io = (fido_dev_io_t) {
		fido_nfc_open,
		fido_nfc_close,
		fido_nfc_read,
		fido_nfc_write,
	};
	d->transport = (fido_dev_transport_t) {
		fido_nfc_rx,
		fido_nfc_tx,
	};

	return 0;
}
#endif /* USE_NFC */
