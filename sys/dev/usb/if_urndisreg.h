/*	$OpenBSD: if_urndisreg.h,v 1.20 2016/09/16 11:13:37 mikeb Exp $ */

/*
 * Copyright (c) 2010 Jonathan Armani <armani@openbsd.org>
 * Copyright (c) 2010 Fabien Romano <fabien@openbsd.org>
 * Copyright (c) 2010 Michael Knudsen <mk@openbsd.org>
 * All rights reserved.
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

#define RNDIS_RX_LIST_CNT	1
#define RNDIS_TX_LIST_CNT	1
#define RNDIS_BUFSZ		1562

struct urndis_chain {
	struct urndis_softc	*sc_softc;
	struct usbd_xfer	*sc_xfer;
	char			*sc_buf;
	struct mbuf		*sc_mbuf;
	int			 sc_idx;
};

struct urndis_cdata {
	struct urndis_chain	sc_rx_chain[RNDIS_RX_LIST_CNT];
	struct urndis_chain	sc_tx_chain[RNDIS_TX_LIST_CNT];
	int			sc_tx_cnt;
};

#define GET_IFP(sc) (&(sc)->sc_arpcom.ac_if)
struct urndis_softc {
	struct device			sc_dev;

	char				sc_attached;
	struct arpcom			sc_arpcom;

	/* RNDIS device info */
	u_int32_t			sc_lim_pktsz;
	u_int32_t			sc_filter;

	/* USB goo */
	struct usbd_device		*sc_udev;
	int				sc_ifaceno_ctl;
	struct usbd_interface		*sc_iface_data;

	struct timeval			sc_rx_notice;
	int				sc_bulkin_no;
	struct usbd_pipe		*sc_bulkin_pipe;
	int				sc_bulkout_no;
	struct usbd_pipe		*sc_bulkout_pipe;

	struct urndis_cdata		sc_data;
};
