/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 2003-2005 Craig Boston
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul, THE VOICES IN HIS HEAD OR
 * THE CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _USB_IF_CDCEREG_H_
#define	_USB_IF_CDCEREG_H_

#define	CDCE_FRAMES_MAX	8		/* units */
#define	CDCE_IND_SIZE_MAX 32            /* bytes */

#define	CDCE_NCM_TX_MINLEN 512		/* bytes, must be power of two */
#define	CDCE_NCM_TX_MAXLEN (16384 + 4)	/* bytes, must be short terminated */
#define	CDCE_NCM_TX_FRAMES_MAX 8	/* units */

#define	CDCE_NCM_RX_MAXLEN (1UL << 14)	/* bytes */
#define	CDCE_NCM_RX_FRAMES_MAX 1	/* units */

#define	CDCE_NCM_SUBFRAMES_MAX 32	/* units */

#define	CDCE_NCM_ALIGN(rem,off,mod) \
    ((uint32_t)(((uint32_t)(rem)) - \
    ((uint32_t)((-(uint32_t)(off)) & (-(uint32_t)(mod))))))

#ifndef CDCE_HAVE_NCM
#define	CDCE_HAVE_NCM 1
#endif

enum {
	CDCE_BULK_RX,
	CDCE_BULK_TX,
	CDCE_INTR_RX,
	CDCE_INTR_TX,
	CDCE_N_TRANSFER,
};

struct cdce_ncm {
	struct usb_ncm16_hdr hdr;
	struct usb_ncm16_dpt dpt;
	struct usb_ncm16_dp dp[CDCE_NCM_SUBFRAMES_MAX];
	uint32_t rx_max;
	uint32_t tx_max;
	uint16_t tx_remainder;
	uint16_t tx_modulus;
	uint16_t tx_struct_align;
	uint16_t tx_seq;
	uint16_t tx_nframe;
};

struct cdce_softc {
	struct usb_ether	sc_ue;
	struct mtx		sc_mtx;
#if CDCE_HAVE_NCM
	struct cdce_ncm		sc_ncm;
#endif
	struct usb_xfer	*sc_xfer[CDCE_N_TRANSFER];
	struct mbuf		*sc_rx_buf[CDCE_FRAMES_MAX];
	struct mbuf		*sc_tx_buf[CDCE_FRAMES_MAX];

	int 			sc_flags;
#define	CDCE_FLAG_ZAURUS	0x0001
#define	CDCE_FLAG_NO_UNION	0x0002
#define	CDCE_FLAG_RX_DATA	0x0010

	uint8_t sc_eaddr_str_index;
	uint8_t	sc_ifaces_index[2];
	uint8_t sc_notify_state;
#define	CDCE_NOTIFY_NETWORK_CONNECTION	0
#define	CDCE_NOTIFY_SPEED_CHANGE	1
#define	CDCE_NOTIFY_DONE		2
};

#define	CDCE_LOCK(_sc)			mtx_lock(&(_sc)->sc_mtx)
#define	CDCE_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	CDCE_LOCK_ASSERT(_sc, t)	mtx_assert(&(_sc)->sc_mtx, t)
#endif					/* _USB_IF_CDCEREG_H_ */
