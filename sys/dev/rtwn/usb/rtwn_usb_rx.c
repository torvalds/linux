/*	$OpenBSD: if_urtwn.c,v 1.16 2011/02/10 17:26:40 jakemsr Exp $	*/

/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2014 Kevin Lo <kevlo@FreeBSD.org>
 * Copyright (c) 2015-2016 Andriy Voskoboinyk <avos@FreeBSD.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_wlan.h"

#include <sys/param.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>

#include <net/if.h>
#include <net/if_var.h>
#include <net/ethernet.h>
#include <net/if_dl.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>
#include <net80211/ieee80211_radiotap.h>
#ifdef	IEEE80211_SUPPORT_SUPERG
#include <net80211/ieee80211_superg.h>
#endif

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>

#include <dev/rtwn/if_rtwnreg.h>
#include <dev/rtwn/if_rtwnvar.h>

#include <dev/rtwn/if_rtwn_debug.h>
#include <dev/rtwn/if_rtwn_ridx.h>
#include <dev/rtwn/if_rtwn_rx.h>
#include <dev/rtwn/if_rtwn_task.h>
#include <dev/rtwn/if_rtwn_tx.h>

#include <dev/rtwn/usb/rtwn_usb_var.h>
#include <dev/rtwn/usb/rtwn_usb_rx.h>

static struct mbuf *	rtwn_rxeof(struct rtwn_softc *, struct rtwn_data *,
			    uint8_t *, int);

static int
rtwn_rx_check_pre_alloc(struct rtwn_softc *sc,
    struct rtwn_rx_stat_common *stat)
{
	uint32_t rxdw0;
	int pktlen;

	RTWN_ASSERT_LOCKED(sc);

	/*
	 * don't pass packets to the ieee80211 framework if the driver isn't
	 * RUNNING.
	 */
	if (!(sc->sc_flags & RTWN_RUNNING))
		return (-1);

	rxdw0 = le32toh(stat->rxdw0);
	if (__predict_false(rxdw0 & (RTWN_RXDW0_CRCERR | RTWN_RXDW0_ICVERR))) {
		/*
		 * This should not happen since we setup our Rx filter
		 * to not receive these frames.
		 */
		RTWN_DPRINTF(sc, RTWN_DEBUG_RECV,
		    "%s: RX flags error (%s)\n", __func__,
		    rxdw0 & RTWN_RXDW0_CRCERR ? "CRC" : "ICV");
		return (-1);
	}

	pktlen = MS(rxdw0, RTWN_RXDW0_PKTLEN);
	if (__predict_false(pktlen < sizeof(struct ieee80211_frame_ack))) {
		/*
		 * Should not happen (because of Rx filter setup).
		 */
		RTWN_DPRINTF(sc, RTWN_DEBUG_RECV,
		    "%s: frame is too short: %d\n", __func__, pktlen);
		return (-1);
	}

	return (0);
}

static struct mbuf *
rtwn_rx_copy_to_mbuf(struct rtwn_softc *sc, struct rtwn_rx_stat_common *stat,
    int totlen)
{
	struct ieee80211com *ic = &sc->sc_ic;
	struct mbuf *m;

	RTWN_ASSERT_LOCKED(sc);

	/* Dump Rx descriptor. */
	RTWN_DPRINTF(sc, RTWN_DEBUG_RECV_DESC,
	    "%s: dw: 0 %08X, 1 %08X, 2 %08X, 3 %08X, 4 %08X, tsfl %08X\n",
	    __func__, le32toh(stat->rxdw0), le32toh(stat->rxdw1),
	    le32toh(stat->rxdw2), le32toh(stat->rxdw3), le32toh(stat->rxdw4),
	    le32toh(stat->tsf_low));

	if (rtwn_rx_check_pre_alloc(sc, stat) != 0)
		goto fail;

	m = m_get2(totlen, M_NOWAIT, MT_DATA, M_PKTHDR);
	if (__predict_false(m == NULL)) {
		device_printf(sc->sc_dev, "%s: could not allocate RX mbuf\n",
		    __func__);
		goto fail;
	}

	/* Finalize mbuf. */
	memcpy(mtod(m, uint8_t *), (uint8_t *)stat, totlen);
	m->m_pkthdr.len = m->m_len = totlen;

	if (rtwn_check_frame(sc, m) != 0) {
		m_freem(m);
		goto fail;
	}

	return (m);
fail:
	counter_u64_add(ic->ic_ierrors, 1);
	return (NULL);
}

static struct mbuf *
rtwn_rxeof_fragmented(struct rtwn_usb_softc *uc, struct rtwn_data *data,
    uint8_t *buf, int len)
{
	struct rtwn_softc *sc = &uc->uc_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct rtwn_rx_stat_common *stat = &uc->uc_rx_stat;
	uint32_t rxdw0;
	int totlen, pktlen, infosz, min_len;
	int orig_len = len;
	int alloc_mbuf = 0;

	/* Check if Rx descriptor is not truncated. */
	if (uc->uc_rx_stat_len < sizeof(*stat)) {
		min_len = min(sizeof(*stat) - uc->uc_rx_stat_len, len);
		memcpy((uint8_t *)stat + uc->uc_rx_stat_len, buf, min_len);

		uc->uc_rx_stat_len += min_len;
		buf += min_len;
		len -= min_len;

		if (uc->uc_rx_stat_len < sizeof(*stat))
			goto end;

		KASSERT(data->m == NULL, ("%s: data->m != NULL!\n", __func__));
		alloc_mbuf = 1;

		/* Dump Rx descriptor. */
		RTWN_DPRINTF(sc, RTWN_DEBUG_RECV_DESC,
		    "%s: dw: 0 %08X, 1 %08X, 2 %08X, 3 %08X, 4 %08X, "
		    "tsfl %08X\n", __func__, le32toh(stat->rxdw0),
		    le32toh(stat->rxdw1), le32toh(stat->rxdw2),
		    le32toh(stat->rxdw3), le32toh(stat->rxdw4),
		    le32toh(stat->tsf_low));
	}

	rxdw0 = le32toh(stat->rxdw0);
	pktlen = MS(rxdw0, RTWN_RXDW0_PKTLEN);
	infosz = MS(rxdw0, RTWN_RXDW0_INFOSZ) * 8;
	totlen = sizeof(*stat) + infosz + pktlen;
	if (alloc_mbuf) {
		if (rtwn_rx_check_pre_alloc(sc, stat) == 0) {
			data->m = m_getm(NULL, totlen, M_NOWAIT, MT_DATA);
			if (data->m != NULL) {
				m_copyback(data->m, 0, uc->uc_rx_stat_len,
				    (caddr_t)stat);

				if (rtwn_check_frame(sc, data->m) != 0) {
					m_freem(data->m);
					data->m = NULL;
					counter_u64_add(ic->ic_ierrors, 1);
				}
			} else
				counter_u64_add(ic->ic_ierrors, 1);
		} else
			counter_u64_add(ic->ic_ierrors, 1);

		uc->uc_rx_off = sizeof(*stat);
	}

	/* If mbuf allocation fails just discard the data. */
	min_len = min(totlen - uc->uc_rx_off, len);
	if (data->m != NULL)
		m_copyback(data->m, uc->uc_rx_off, min_len, buf);

	uc->uc_rx_off += min_len;
	if (uc->uc_rx_off == totlen) {
		/* Align next frame. */
		min_len = rtwn_usb_align_rx(uc,
		    orig_len - len + min_len, orig_len);
		min_len -= (orig_len - len);
		KASSERT(len >= min_len, ("%s: len (%d) < min_len (%d)!\n",
		    __func__, len, min_len));

		/* Clear mbuf stats. */
		uc->uc_rx_stat_len = 0;
		uc->uc_rx_off = 0;
	}
	len -= min_len;
	buf += min_len;
end:
	if (uc->uc_rx_stat_len == 0)
		return (rtwn_rxeof(sc, data, buf, len));
	else
		return (NULL);
}

static struct mbuf *
rtwn_rxeof(struct rtwn_softc *sc, struct rtwn_data *data, uint8_t *buf,
    int len)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);
	struct rtwn_rx_stat_common *stat;
	struct mbuf *m, *m0 = NULL;
	uint32_t rxdw0;
	int totlen, pktlen, infosz;

	/* Prepend defragmented frame (if any). */
	if (data->m != NULL) {
		m0 = m = data->m;
		data->m = NULL;
	}

	/* Process packets. */
	while (len >= sizeof(*stat)) {
		stat = (struct rtwn_rx_stat_common *)buf;
		rxdw0 = le32toh(stat->rxdw0);

		pktlen = MS(rxdw0, RTWN_RXDW0_PKTLEN);
		if (__predict_false(pktlen == 0))
			break;

		infosz = MS(rxdw0, RTWN_RXDW0_INFOSZ) * 8;

		/* Make sure everything fits in xfer. */
		totlen = sizeof(*stat) + infosz + pktlen;
		if (totlen > len) {
			RTWN_DPRINTF(sc, RTWN_DEBUG_RECV,
			    "%s: frame is fragmented (totlen %d len %d)\n",
			    __func__, totlen, len);
			break;
		}

		if (m0 == NULL)
			m0 = m = rtwn_rx_copy_to_mbuf(sc, stat, totlen);
		else {
			m->m_nextpkt = rtwn_rx_copy_to_mbuf(sc, stat, totlen);
			if (m->m_nextpkt != NULL)
				m = m->m_nextpkt;
		}

		/* Align next frame. */
		totlen = rtwn_usb_align_rx(uc, totlen, len);
		buf += totlen;
		len -= totlen;
	}

	if (len > 0)
		(void)rtwn_rxeof_fragmented(uc, data, buf, len);

	return (m0);
}

static struct mbuf *
rtwn_report_intr(struct rtwn_usb_softc *uc, struct usb_xfer *xfer,
    struct rtwn_data *data)
{
	struct rtwn_softc *sc = &uc->uc_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	uint8_t *buf;
	int len;

	usbd_xfer_status(xfer, &len, NULL, NULL, NULL);

	if (__predict_false(len < sizeof(struct rtwn_rx_stat_common) &&
	    uc->uc_rx_stat_len == 0)) {
		counter_u64_add(ic->ic_ierrors, 1);
		return (NULL);
	}

	buf = data->buf;
	if (uc->uc_rx_stat_len > 0)
		return (rtwn_rxeof_fragmented(uc, data, data->buf, len));

	switch (rtwn_classify_intr(sc, buf, len)) {
	case RTWN_RX_DATA:
		return (rtwn_rxeof(sc, data, buf, len));
	case RTWN_RX_TX_REPORT:
		if (sc->sc_ratectl != RTWN_RATECTL_NET80211) {
			/* shouldn't happen */
			device_printf(sc->sc_dev,
			    "%s called while ratectl = %d!\n",
			    __func__, sc->sc_ratectl);
			break;
		}

		RTWN_NT_LOCK(sc);
		rtwn_handle_tx_report(sc, buf, len);
		RTWN_NT_UNLOCK(sc);

#ifdef IEEE80211_SUPPORT_SUPERG
		/*
		 * NB: this will executed only when 'report' bit is set.
		 */
		if (sc->sc_tx_n_active > 0 && --sc->sc_tx_n_active <= 1)
			rtwn_cmd_sleepable(sc, NULL, 0, rtwn_ff_flush_all);
#endif
		break;
	case RTWN_RX_OTHER:
		rtwn_handle_c2h_report(sc, buf, len);
		break;
	default:
		/* NOTREACHED */
		KASSERT(0, ("unknown Rx classification code"));
		break;
	}

	return (NULL);
}

static struct ieee80211_node *
rtwn_rx_frame(struct rtwn_softc *sc, struct mbuf *m)
{
	struct rtwn_rx_stat_common stat;

	/* Imitate PCIe layout. */
	m_copydata(m, 0, sizeof(stat), (caddr_t)&stat);
	m_adj(m, sizeof(stat));

	return (rtwn_rx_common(sc, m, &stat));
}

void
rtwn_bulk_rx_callback(struct usb_xfer *xfer, usb_error_t error)
{
	struct rtwn_usb_softc *uc = usbd_xfer_softc(xfer);
	struct rtwn_softc *sc = &uc->uc_sc;
	struct ieee80211com *ic = &sc->sc_ic;
	struct ieee80211_node *ni;
	struct mbuf *m = NULL, *next;
	struct rtwn_data *data;

	RTWN_ASSERT_LOCKED(sc);

	switch (USB_GET_STATE(xfer)) {
	case USB_ST_TRANSFERRED:
		data = STAILQ_FIRST(&uc->uc_rx_active);
		if (data == NULL)
			goto tr_setup;
		STAILQ_REMOVE_HEAD(&uc->uc_rx_active, next);
		m = rtwn_report_intr(uc, xfer, data);
		STAILQ_INSERT_TAIL(&uc->uc_rx_inactive, data, next);
		/* FALLTHROUGH */
	case USB_ST_SETUP:
tr_setup:
		data = STAILQ_FIRST(&uc->uc_rx_inactive);
		if (data == NULL) {
			KASSERT(m == NULL, ("mbuf isn't NULL"));
			goto finish;
		}
		STAILQ_REMOVE_HEAD(&uc->uc_rx_inactive, next);
		STAILQ_INSERT_TAIL(&uc->uc_rx_active, data, next);
		usbd_xfer_set_frame_data(xfer, 0, data->buf,
		    usbd_xfer_max_len(xfer));
		usbd_transfer_submit(xfer);

		/*
		 * To avoid LOR we should unlock our private mutex here to call
		 * ieee80211_input() because here is at the end of a USB
		 * callback and safe to unlock.
		 */
		while (m != NULL) {
			next = m->m_nextpkt;
			m->m_nextpkt = NULL;

			ni = rtwn_rx_frame(sc, m);

			RTWN_UNLOCK(sc);

			if (ni != NULL) {
				(void)ieee80211_input_mimo(ni, m);
				ieee80211_free_node(ni);
			} else {
				(void)ieee80211_input_mimo_all(ic, m);
			}
			RTWN_LOCK(sc);
			m = next;
		}
		break;
	default:
		/* needs it to the inactive queue due to a error. */
		data = STAILQ_FIRST(&uc->uc_rx_active);
		if (data != NULL) {
			STAILQ_REMOVE_HEAD(&uc->uc_rx_active, next);
			STAILQ_INSERT_TAIL(&uc->uc_rx_inactive, data, next);
		}
		if (error != USB_ERR_CANCELLED) {
			/* XXX restart device if frame was fragmented? */

			usbd_xfer_set_stall(xfer);
			counter_u64_add(ic->ic_ierrors, 1);
			goto tr_setup;
		}
		break;
	}
finish:
	/* Kick-start more transmit in case we stalled */
	rtwn_start(sc);
}
