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
#include <sys/condvar.h>
#include <sys/mbuf.h>
#include <sys/kernel.h>
#include <sys/socket.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/queue.h>
#include <sys/taskqueue.h>
#include <sys/bus.h>
#include <sys/endian.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usb_device.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_media.h>

#include <net80211/ieee80211_var.h>

#include <dev/rtwn/if_rtwnvar.h>
#include <dev/rtwn/if_rtwn_debug.h>

#include <dev/rtwn/usb/rtwn_usb_var.h>
#include <dev/rtwn/usb/rtwn_usb_ep.h>
#include <dev/rtwn/usb/rtwn_usb_rx.h>
#include <dev/rtwn/usb/rtwn_usb_tx.h>

#include <dev/rtwn/rtl8192c/usb/r92cu_reg.h>


static const struct usb_config rtwn_config_common[RTWN_N_TRANSFER] = {
	[RTWN_BULK_RX] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_IN,
		.flags = {
			.pipe_bof = 1,
			.short_xfer_ok = 1
		},
		.callback = rtwn_bulk_rx_callback,
	},
	[RTWN_BULK_TX_BE] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = RTWN_USB_TXBUFSZ,
		.flags = {
			.ext_buffer = 1,
			.pipe_bof = 1,
			.force_short_xfer = 1,
		},
		.callback = rtwn_bulk_tx_callback,
		.timeout = RTWN_TX_TIMEOUT,	/* ms */
	},
	[RTWN_BULK_TX_BK] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = RTWN_USB_TXBUFSZ,
		.flags = {
			.ext_buffer = 1,
			.pipe_bof = 1,
			.force_short_xfer = 1,
		},
		.callback = rtwn_bulk_tx_callback,
		.timeout = RTWN_TX_TIMEOUT,	/* ms */
	},
	[RTWN_BULK_TX_VI] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = RTWN_USB_TXBUFSZ,
		.flags = {
			.ext_buffer = 1,
			.pipe_bof = 1,
			.force_short_xfer = 1
		},
		.callback = rtwn_bulk_tx_callback,
		.timeout = RTWN_TX_TIMEOUT,	/* ms */
	},
	[RTWN_BULK_TX_VO] = {
		.type = UE_BULK,
		.endpoint = UE_ADDR_ANY,
		.direction = UE_DIR_OUT,
		.bufsize = RTWN_USB_TXBUFSZ,
		.flags = {
			.ext_buffer = 1,
			.pipe_bof = 1,
			.force_short_xfer = 1
		},
		.callback = rtwn_bulk_tx_callback,
		.timeout = RTWN_TX_TIMEOUT,	/* ms */
	},
};

static void
rtwn_usb_setup_queues(struct rtwn_usb_softc *uc)
{
	struct rtwn_softc *sc = &uc->uc_sc;
	int hasnq, haslq, nqueues, nqpages, nrempages;

	/* Get Tx queues to USB endpoints mapping. */
	hasnq = haslq = 0;
	switch (uc->ntx) {
	case 4:
	case 3:
		haslq = 1;
		/* FALLTHROUGH */
	case 2:
		hasnq = 1;
		/* FALLTHROUGH */
	default:
		break;
	}
	nqueues = 1 + hasnq + haslq;

	/* Get the number of pages for each queue. */
	nqpages = (sc->page_count - sc->npubqpages) / nqueues;

	/*
	 * The remaining pages are assigned to the high priority
	 * queue.
	 */
	nrempages = (sc->page_count - sc->npubqpages) % nqueues;

	sc->nhqpages = nqpages + nrempages;
	sc->nnqpages = (hasnq ? nqpages : 0);
	sc->nlqpages = (haslq ? nqpages : 0);
}

int
rtwn_usb_setup_endpoints(struct rtwn_usb_softc *uc)
{
	struct usb_config *rtwn_config;
	struct rtwn_softc *sc = &uc->uc_sc;
	const uint8_t iface_index = RTWN_IFACE_INDEX;
	struct usb_endpoint *ep, *ep_end;
	uint8_t addr[RTWN_MAX_EPOUT];
	int error;

	/* Determine the number of bulk-out pipes. */
	uc->ntx = 0;
	ep = uc->uc_udev->endpoints;
	ep_end = uc->uc_udev->endpoints + uc->uc_udev->endpoints_max;
	for (; ep != ep_end; ep++) {
		uint8_t eaddr;

		if ((ep->edesc == NULL) || (ep->iface_index != iface_index))
			continue;

		eaddr = ep->edesc->bEndpointAddress;
		RTWN_DPRINTF(sc, RTWN_DEBUG_USB,
		    "%s: endpoint: addr %u, direction %s\n", __func__,
		    UE_GET_ADDR(eaddr), UE_GET_DIR(eaddr) == UE_DIR_OUT ?
		    "output" : "input");

		if (UE_GET_DIR(eaddr) == UE_DIR_OUT) {
			if (uc->ntx == RTWN_MAX_EPOUT)
				break;

			addr[uc->ntx++] = UE_GET_ADDR(eaddr);
		}
	}
	if (uc->ntx == 0 || uc->ntx > RTWN_MAX_EPOUT) {
		device_printf(sc->sc_dev,
		    "%s: invalid number of Tx bulk pipes (%d)\n", __func__,
		    uc->ntx);
		return (EINVAL);
	}

	rtwn_config = malloc(sizeof(rtwn_config_common), M_TEMP, M_WAITOK);
	memcpy(rtwn_config, rtwn_config_common, sizeof(rtwn_config_common));

	/* NB: keep in sync with rtwn_dma_init(). */
	rtwn_config[RTWN_BULK_TX_VO].endpoint = addr[0];
	switch (uc->ntx) {
	case 4:
	case 3:
		rtwn_config[RTWN_BULK_TX_BE].endpoint = addr[2];
		rtwn_config[RTWN_BULK_TX_BK].endpoint = addr[2];
		rtwn_config[RTWN_BULK_TX_VI].endpoint = addr[1];
		break;
	case 2:
		rtwn_config[RTWN_BULK_TX_BE].endpoint = addr[1];
		rtwn_config[RTWN_BULK_TX_BK].endpoint = addr[1];
		rtwn_config[RTWN_BULK_TX_VI].endpoint = addr[0];
		break;
	case 1:
		rtwn_config[RTWN_BULK_TX_BE].endpoint = addr[0];
		rtwn_config[RTWN_BULK_TX_BK].endpoint = addr[0];
		rtwn_config[RTWN_BULK_TX_VI].endpoint = addr[0];
		break;
	default:
		KASSERT(0, ("unhandled number of endpoints %d\n", uc->ntx));
		break;
	}

	rtwn_config[RTWN_BULK_RX].bufsize =
	    uc->uc_rx_buf_size * RTWN_USB_RXBUFSZ_UNIT;
	error = usbd_transfer_setup(uc->uc_udev, &iface_index,
	    uc->uc_xfer, rtwn_config, RTWN_N_TRANSFER, uc, &sc->sc_mtx);
	free(rtwn_config, M_TEMP);

	if (error) {
		device_printf(sc->sc_dev, "could not allocate USB transfers, "
		    "err=%s\n", usbd_errstr(error));
		return (error);
	}

	/* Assign pages for each queue (if not done). */
	if (sc->nhqpages == 0 && sc->nnqpages == 0 && sc->nlqpages == 0)
		rtwn_usb_setup_queues(uc);

	return (0);
}

uint16_t
rtwn_usb_get_qmap(struct rtwn_softc *sc)
{
	struct rtwn_usb_softc *uc = RTWN_USB_SOFTC(sc);

	switch (uc->ntx) {
	case 1:
		return (R92C_TRXDMA_CTRL_QMAP_HQ);
	case 2:
		return (R92C_TRXDMA_CTRL_QMAP_HQ_NQ);
	default:
		return (R92C_TRXDMA_CTRL_QMAP_3EP);
	}
}
