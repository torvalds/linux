/* $FreeBSD$ */
/*-
 * Copyright (c) 2010, 2011 Rick van der Zwet <info@rickvanderzwet.nl>
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

/*-
 * Copyright (c) 2008 Johann Christian Rode <jcrode@gmx.net>
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

/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1997, 1998, 1999, 2000-2003
 *	Bill Paul <wpaul@windriver.com>.  All rights reserved.
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
 *	This product includes software developed by Ravikanth.
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
 */

/*
 * Register definitions for the Moschip MCS7x30 ethernet controller.
 */
#define	MOS_MCAST_TABLE 	0x00
#define	MOS_IPG0		0x08
#define	MOS_IPG1		0x09
#define	MOS_PHY_DATA0		0x0a
#define	MOS_PHY_DATA1		0x0b
#define	MOS_PHY_CTL		0x0c
#define	MOS_PHY_STS		0x0d
#define	MOS_PHY_DATA		MOS_PHY_DATA0
#define	MOS_CTL			0x0e
#define	MOS_MAC0		0x0f
#define	MOS_MAC1		0x10
#define	MOS_MAC2		0x11
#define	MOS_MAC3		0x12
#define	MOS_MAC4		0x13
#define	MOS_MAC5		0x14
#define	MOS_MAC			MOS_MAC0
/* apparently only available on hardware rev. C */
#define	MOS_FRAME_DROP_CNT	0x15
#define	MOS_PAUSE_TRHD		0x16

#define	MOS_PHYCTL_PHYADDR	0x1f
#define	MOS_PHYCTL_WRITE	0x20
#define	MOS_PHYCTL_READ		0x40

#define	MOS_PHYSTS_PHYREG	0x1f
#define	MOS_PHYSTS_READY	0x40
#define	MOS_PHYSTS_PENDING	0x80

#define	MOS_CTL_RX_PROMISC	0x01
#define	MOS_CTL_ALLMULTI	0x02
#define	MOS_CTL_SLEEP		0x04
#define	MOS_CTL_TX_ENB		0x08
/*
 * The documentation calls this bit 'reserved', but in the FreeBSD driver
 * provided by the vendor, this enables the receiver.
 */
#define	MOS_CTL_RX_ENB		0x10
#define	MOS_CTL_FDX_ENB		0x20
/* 0 = 10 Mbps, 1 = 100 Mbps */
#define	MOS_CTL_SPEEDSEL	0x40
/* 0 = PHY controls speed/duplex mode, 1 = bridge controls speed/duplex mode */
#define	MOS_CTL_BS_ENB		0x80

#define	MOS_RXSTS_SHORT_FRAME	0x01
#define	MOS_RXSTS_LENGTH_ERROR	0x02
#define	MOS_RXSTS_ALIGN_ERROR	0x04
#define	MOS_RXSTS_CRC_ERROR	0x08
#define	MOS_RXSTS_LARGE_FRAME	0x10
#define	MOS_RXSTS_VALID		0x20
/*
 * The EtherType field of an Ethernet frame can contain values other than
 * the frame length, hence length errors are ignored.
 */
#define	MOS_RXSTS_MASK		0x3d

#define	MOS_PAUSE_TRHD_DEFAULT	0
#define	MOS_PAUSE_REWRITES	3

#define	MOS_TIMEOUT		1000

#define	MOS_RX_LIST_CNT		1
#define	MOS_TX_LIST_CNT		1

/* Maximum size of a fast ethernet frame plus one byte for the status */
#define	MOS_BUFSZ	 	(ETHER_MAX_LEN+1)

/*
 * USB endpoints.
 */
#define	MOS_ENDPT_RX		0
#define	MOS_ENDPT_TX		1
#define	MOS_ENDPT_INTR		2
#define	MOS_ENDPT_MAX		3

/*
 * USB vendor requests.
 */
#define	MOS_UR_READREG		0x0e
#define	MOS_UR_WRITEREG		0x0d

#define	MOS_CONFIG_IDX		0
#define	MOS_IFACE_IDX		0

#define	MCS7730	0x0001
#define	MCS7830	0x0002
#define	MCS7832	0x0004

#define	MOS_INC(x, y)           (x) = (x + 1) % y

struct mos_softc {
	struct usb_ether sc_ue;
	struct ifnet ifp;

	struct mtx sc_mtx;
	struct usb_xfer *sc_xfer[MOS_ENDPT_MAX];

	uint16_t mos_flags;

	int mos_link;
	unsigned char mos_ipgs[2];
	unsigned char mos_phyaddrs[2];
};

#define	GET_MII(sc)		uether_getmii(&(sc)->sc_ue)
#define	MOS_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	MOS_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	MOS_LOCK_ASSERT(_sc, t)	mtx_assert(&(_sc)->sc_mtx, t)
