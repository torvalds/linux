/*
 * Copyright (c) 2018 Yubico AB. All rights reserved.
 * Use of this source code is governed by a BSD-style
 * license that can be found in the LICENSE file.
 */

#include "fido.h"
#include "packed.h"

PACKED_TYPE(frame_t,
struct frame {
	uint32_t cid; /* channel id */
	union {
		uint8_t type;
		struct {
			uint8_t cmd;
			uint8_t bcnth;
			uint8_t bcntl;
			uint8_t data[CTAP_MAX_REPORT_LEN - CTAP_INIT_HEADER_LEN];
		} init;
		struct {
			uint8_t seq;
			uint8_t data[CTAP_MAX_REPORT_LEN - CTAP_CONT_HEADER_LEN];
		} cont;
	} body;
})

#ifndef MIN
#define MIN(x, y) ((x) > (y) ? (y) : (x))
#endif

static int
tx_pkt(fido_dev_t *d, const void *pkt, size_t len, int *ms)
{
	struct timespec ts;
	int n;

	if (fido_time_now(&ts) != 0)
		return (-1);

	n = d->io.write(d->io_handle, pkt, len);

	if (fido_time_delta(&ts, ms) != 0)
		return (-1);

	return (n);
}

static int
tx_empty(fido_dev_t *d, uint8_t cmd, int *ms)
{
	struct frame	*fp;
	unsigned char	 pkt[sizeof(*fp) + 1];
	const size_t	 len = d->tx_len + 1;
	int		 n;

	memset(&pkt, 0, sizeof(pkt));
	fp = (struct frame *)(pkt + 1);
	fp->cid = d->cid;
	fp->body.init.cmd = CTAP_FRAME_INIT | cmd;

	if (len > sizeof(pkt) || (n = tx_pkt(d, pkt, len, ms)) < 0 ||
	    (size_t)n != len)
		return (-1);

	return (0);
}

static size_t
tx_preamble(fido_dev_t *d, uint8_t cmd, const void *buf, size_t count, int *ms)
{
	struct frame	*fp;
	unsigned char	 pkt[sizeof(*fp) + 1];
	const size_t	 len = d->tx_len + 1;
	int		 n;

	if (d->tx_len - CTAP_INIT_HEADER_LEN > sizeof(fp->body.init.data))
		return (0);

	memset(&pkt, 0, sizeof(pkt));
	fp = (struct frame *)(pkt + 1);
	fp->cid = d->cid;
	fp->body.init.cmd = CTAP_FRAME_INIT | cmd;
	fp->body.init.bcnth = (count >> 8) & 0xff;
	fp->body.init.bcntl = count & 0xff;
	count = MIN(count, d->tx_len - CTAP_INIT_HEADER_LEN);
	memcpy(&fp->body.init.data, buf, count);

	if (len > sizeof(pkt) || (n = tx_pkt(d, pkt, len, ms)) < 0 ||
	    (size_t)n != len)
		return (0);

	return (count);
}

static size_t
tx_frame(fido_dev_t *d, uint8_t seq, const void *buf, size_t count, int *ms)
{
	struct frame	*fp;
	unsigned char	 pkt[sizeof(*fp) + 1];
	const size_t	 len = d->tx_len + 1;
	int		 n;

	if (d->tx_len - CTAP_CONT_HEADER_LEN > sizeof(fp->body.cont.data))
		return (0);

	memset(&pkt, 0, sizeof(pkt));
	fp = (struct frame *)(pkt + 1);
	fp->cid = d->cid;
	fp->body.cont.seq = seq;
	count = MIN(count, d->tx_len - CTAP_CONT_HEADER_LEN);
	memcpy(&fp->body.cont.data, buf, count);

	if (len > sizeof(pkt) || (n = tx_pkt(d, pkt, len, ms)) < 0 ||
	    (size_t)n != len)
		return (0);

	return (count);
}

static int
tx(fido_dev_t *d, uint8_t cmd, const unsigned char *buf, size_t count, int *ms)
{
	size_t n, sent;

	if ((sent = tx_preamble(d, cmd, buf, count, ms)) == 0) {
		fido_log_debug("%s: tx_preamble", __func__);
		return (-1);
	}

	for (uint8_t seq = 0; sent < count; sent += n) {
		if (seq & 0x80) {
			fido_log_debug("%s: seq & 0x80", __func__);
			return (-1);
		}
		if ((n = tx_frame(d, seq++, buf + sent, count - sent,
		    ms)) == 0) {
			fido_log_debug("%s: tx_frame", __func__);
			return (-1);
		}
	}

	return (0);
}

static int
transport_tx(fido_dev_t *d, uint8_t cmd, const void *buf, size_t count, int *ms)
{
	struct timespec ts;
	int n;

	if (fido_time_now(&ts) != 0)
		return (-1);

	n = d->transport.tx(d, cmd, buf, count);

	if (fido_time_delta(&ts, ms) != 0)
		return (-1);

	return (n);
}

int
fido_tx(fido_dev_t *d, uint8_t cmd, const void *buf, size_t count, int *ms)
{
	fido_log_debug("%s: dev=%p, cmd=0x%02x", __func__, (void *)d, cmd);
	fido_log_xxd(buf, count, "%s", __func__);

	if (d->transport.tx != NULL)
		return (transport_tx(d, cmd, buf, count, ms));
	if (d->io_handle == NULL || d->io.write == NULL || count > UINT16_MAX) {
		fido_log_debug("%s: invalid argument", __func__);
		return (-1);
	}

	return (count == 0 ? tx_empty(d, cmd, ms) : tx(d, cmd, buf, count, ms));
}

static int
rx_frame(fido_dev_t *d, struct frame *fp, int *ms)
{
	struct timespec ts;
	int n;

	memset(fp, 0, sizeof(*fp));

	if (fido_time_now(&ts) != 0)
		return (-1);

	if (d->rx_len > sizeof(*fp) || (n = d->io.read(d->io_handle,
	    (unsigned char *)fp, d->rx_len, *ms)) < 0 || (size_t)n != d->rx_len)
		return (-1);

	return (fido_time_delta(&ts, ms));
}

static int
rx_preamble(fido_dev_t *d, uint8_t cmd, struct frame *fp, int *ms)
{
	do {
		if (rx_frame(d, fp, ms) < 0)
			return (-1);
#ifdef FIDO_FUZZ
		fp->cid = d->cid;
#endif
	} while (fp->cid != d->cid || (fp->cid == d->cid &&
	    fp->body.init.cmd == (CTAP_FRAME_INIT | CTAP_KEEPALIVE)));

	if (d->rx_len > sizeof(*fp))
		return (-1);

	fido_log_xxd(fp, d->rx_len, "%s", __func__);
#ifdef FIDO_FUZZ
	fp->body.init.cmd = (CTAP_FRAME_INIT | cmd);
#endif

	if (fp->cid != d->cid || fp->body.init.cmd != (CTAP_FRAME_INIT | cmd)) {
		fido_log_debug("%s: cid (0x%x, 0x%x), cmd (0x%02x, 0x%02x)",
		    __func__, fp->cid, d->cid, fp->body.init.cmd, cmd);
		return (-1);
	}

	return (0);
}

static int
rx(fido_dev_t *d, uint8_t cmd, unsigned char *buf, size_t count, int *ms)
{
	struct frame f;
	size_t r, payload_len, init_data_len, cont_data_len;

	if (d->rx_len <= CTAP_INIT_HEADER_LEN ||
	    d->rx_len <= CTAP_CONT_HEADER_LEN)
		return (-1);

	init_data_len = d->rx_len - CTAP_INIT_HEADER_LEN;
	cont_data_len = d->rx_len - CTAP_CONT_HEADER_LEN;

	if (init_data_len > sizeof(f.body.init.data) ||
	    cont_data_len > sizeof(f.body.cont.data))
		return (-1);

	if (rx_preamble(d, cmd, &f, ms) < 0) {
		fido_log_debug("%s: rx_preamble", __func__);
		return (-1);
	}

	payload_len = (size_t)((f.body.init.bcnth << 8) | f.body.init.bcntl);
	fido_log_debug("%s: payload_len=%zu", __func__, payload_len);

	if (count < payload_len) {
		fido_log_debug("%s: count < payload_len", __func__);
		return (-1);
	}

	if (payload_len < init_data_len) {
		memcpy(buf, f.body.init.data, payload_len);
		return ((int)payload_len);
	}

	memcpy(buf, f.body.init.data, init_data_len);
	r = init_data_len;

	for (int seq = 0; r < payload_len; seq++) {
		if (rx_frame(d, &f, ms) < 0) {
			fido_log_debug("%s: rx_frame", __func__);
			return (-1);
		}

		fido_log_xxd(&f, d->rx_len, "%s", __func__);
#ifdef FIDO_FUZZ
		f.cid = d->cid;
		f.body.cont.seq = (uint8_t)seq;
#endif

		if (f.cid != d->cid || f.body.cont.seq != seq) {
			fido_log_debug("%s: cid (0x%x, 0x%x), seq (%d, %d)",
			    __func__, f.cid, d->cid, f.body.cont.seq, seq);
			return (-1);
		}

		if (payload_len - r > cont_data_len) {
			memcpy(buf + r, f.body.cont.data, cont_data_len);
			r += cont_data_len;
		} else {
			memcpy(buf + r, f.body.cont.data, payload_len - r);
			r += payload_len - r; /* break */
		}
	}

	return ((int)r);
}

static int
transport_rx(fido_dev_t *d, uint8_t cmd, void *buf, size_t count, int *ms)
{
	struct timespec ts;
	int n;

	if (fido_time_now(&ts) != 0)
		return (-1);

	n = d->transport.rx(d, cmd, buf, count, *ms);

	if (fido_time_delta(&ts, ms) != 0)
		return (-1);

	return (n);
}

int
fido_rx(fido_dev_t *d, uint8_t cmd, void *buf, size_t count, int *ms)
{
	int n;

	fido_log_debug("%s: dev=%p, cmd=0x%02x, ms=%d", __func__, (void *)d,
	    cmd, *ms);

	if (d->transport.rx != NULL)
		return (transport_rx(d, cmd, buf, count, ms));
	if (d->io_handle == NULL || d->io.read == NULL || count > UINT16_MAX) {
		fido_log_debug("%s: invalid argument", __func__);
		return (-1);
	}
	if ((n = rx(d, cmd, buf, count, ms)) >= 0)
		fido_log_xxd(buf, (size_t)n, "%s", __func__);

	return (n);
}

int
fido_rx_cbor_status(fido_dev_t *d, int *ms)
{
	unsigned char	reply[FIDO_MAXMSG];
	int		reply_len;

	if ((reply_len = fido_rx(d, CTAP_CMD_CBOR, &reply, sizeof(reply),
	    ms)) < 0 || (size_t)reply_len < 1) {
		fido_log_debug("%s: fido_rx", __func__);
		return (FIDO_ERR_RX);
	}

	return (reply[0]);
}
