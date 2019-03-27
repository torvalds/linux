/*-
 * Copyright (c) 2010 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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
 *
 * $OpenBSD: if_urtwnreg.h,v 1.3 2010/11/16 18:02:59 damien Exp $
 * $FreeBSD$
 */

#ifndef RTWN_USBVAR_H
#define RTWN_USBVAR_H

#include <dev/rtwn/if_rtwnreg.h>	/* for struct rtwn_rx_stat_common */

#define RTWN_USB_RXBUFSZ_UNIT		(512)
#define RTWN_USB_RXBUFSZ_MIN		( 4)
#define RTWN_USB_RXBUFSZ_DEF		(24)
#define RTWN_USB_RXBUFSZ_MAX		(64)
#define RTWN_USB_TXBUFSZ		(16 * 1024)

#define RTWN_IFACE_INDEX		0

#define RTWN_USB_RX_LIST_COUNT		1
#define RTWN_USB_TX_LIST_COUNT		16

struct rtwn_data {
	uint8_t				*buf;
	/* 'id' is meaningful for beacons only */
	int				id;
	uint16_t			buflen;
	struct mbuf			*m;
	struct ieee80211_node		*ni;
	STAILQ_ENTRY(rtwn_data)	next;
};
typedef STAILQ_HEAD(, rtwn_data) rtwn_datahead;

enum {
	RTWN_BULK_RX,
	RTWN_BULK_TX_BE,	/* = WME_AC_BE */
	RTWN_BULK_TX_BK,	/* = WME_AC_BK */
	RTWN_BULK_TX_VI,	/* = WME_AC_VI */
	RTWN_BULK_TX_VO,	/* = WME_AC_VO */
	RTWN_N_TRANSFER = 5,
};

#define RTWN_EP_QUEUES		RTWN_BULK_RX

struct rtwn_usb_softc {
	struct rtwn_softc	uc_sc;		/* must be the first */
	struct usb_device	*uc_udev;
	struct usb_xfer		*uc_xfer[RTWN_N_TRANSFER];

	struct rtwn_data	uc_rx[RTWN_USB_RX_LIST_COUNT];
	rtwn_datahead		uc_rx_active;
	rtwn_datahead		uc_rx_inactive;
	int			uc_rx_buf_size;

	struct rtwn_rx_stat_common uc_rx_stat;
	int			uc_rx_stat_len;
	int			uc_rx_off;

	struct rtwn_data	uc_tx[RTWN_USB_TX_LIST_COUNT];
	rtwn_datahead		uc_tx_active;
	rtwn_datahead		uc_tx_inactive;
	rtwn_datahead		uc_tx_pending;

	int			(*uc_align_rx)(int, int);

	int			ntx;
	int			tx_agg_desc_num;
};
#define RTWN_USB_SOFTC(sc)	((struct rtwn_usb_softc *)(sc))

#define rtwn_usb_align_rx(_uc, _totlen, _len) \
	(((_uc)->uc_align_rx)((_totlen), (_len)))

#endif	/* RTWN_USBVAR_H */
