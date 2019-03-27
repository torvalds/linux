/*	$FreeBSD$ */
/*	$OpenBSD: if_urndisreg.h,v 1.19 2013/11/21 14:08:05 mpi Exp $ */

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

#ifndef _IF_URNDISREG_H_
#define	_IF_URNDISREG_H_

#define	RNDIS_RESPONSE_LEN	1024	/* bytes */
#define	RNDIS_RX_MAXLEN		(16 * 1024)
#define	RNDIS_TX_FRAMES_MAX	8
#define	RNDIS_TX_MAXLEN		MCLBYTES

enum {
	URNDIS_BULK_RX,
	URNDIS_BULK_TX,
	URNDIS_INTR_RX,
	URNDIS_N_TRANSFER,
};

struct urndis_softc {

	struct usb_ether sc_ue;
	struct mtx sc_mtx;

	/* RNDIS device info */
	uint32_t sc_lim_pktsz;
	uint32_t sc_filter;

	struct usb_device *sc_udev;
	struct usb_xfer *sc_xfer[URNDIS_N_TRANSFER];

	uint8_t	sc_ifaceno_ctl;
	uint8_t	sc_response_buf[RNDIS_RESPONSE_LEN] __aligned(4);
};

#define	URNDIS_LOCK(sc) mtx_lock(&(sc)->sc_mtx)
#define	URNDIS_UNLOCK(sc) mtx_unlock(&(sc)->sc_mtx)
#define	URNDIS_LOCK_ASSERT(sc, what) mtx_assert(&(sc)->sc_mtx, (what))

#endif					/* _IF_URNDISREG_H_ */
