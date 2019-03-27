/* $FreeBSD$ */
/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010 Hans Petter Selasky. All rights reserved.
 * Copyright (c) 2009 Diego Giagio. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Thanks to Diego Giagio for figuring out the programming details for
 * the Apple iPhone Ethernet driver.
 */

#ifndef _IF_IPHETHVAR_H_
#define	_IF_IPHETHVAR_H_

#define	IPHETH_USBINTF_CLASS    255
#define	IPHETH_USBINTF_SUBCLASS 253
#define	IPHETH_USBINTF_PROTO    1

#define	IPHETH_BUF_SIZE         1516
#define	IPHETH_TX_TIMEOUT       5000	/* ms */

#define	IPHETH_RX_FRAMES_MAX	1
#define	IPHETH_TX_FRAMES_MAX	8

#define	IPHETH_RX_ADJ		2

#define	IPHETH_CFG_INDEX	0
#define	IPHETH_IF_INDEX		2
#define	IPHETH_ALT_INTFNUM      1

#define	IPHETH_CTRL_ENDP        0x00
#define	IPHETH_CTRL_BUF_SIZE    0x40
#define	IPHETH_CTRL_TIMEOUT     5000	/* ms */

#define	IPHETH_CMD_GET_MACADDR   0x00
#define	IPHETH_CMD_CARRIER_CHECK 0x45

#define	IPHETH_CARRIER_ON       0x04

enum {
	IPHETH_BULK_TX,
	IPHETH_BULK_RX,
	IPHETH_N_TRANSFER,
};

struct ipheth_softc {
	struct usb_ether sc_ue;
	struct mtx sc_mtx;

	struct usb_xfer *sc_xfer[IPHETH_N_TRANSFER];
	struct mbuf *sc_rx_buf[IPHETH_RX_FRAMES_MAX];
	struct mbuf *sc_tx_buf[IPHETH_TX_FRAMES_MAX];

	uint8_t	sc_data[IPHETH_CTRL_BUF_SIZE];
	uint8_t	sc_iface_no;
	uint8_t	sc_carrier_on;
};

#define	IPHETH_LOCK(_sc)			mtx_lock(&(_sc)->sc_mtx)
#define	IPHETH_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	IPHETH_LOCK_ASSERT(_sc, t)	mtx_assert(&(_sc)->sc_mtx, t)

#endif					/* _IF_IPHETHVAR_H_ */
