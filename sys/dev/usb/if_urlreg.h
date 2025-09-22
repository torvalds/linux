/*	$OpenBSD: if_urlreg.h,v 1.15 2013/10/28 16:08:08 mpi Exp $ */
/*	$NetBSD: if_urlreg.h,v 1.1 2002/03/28 21:09:11 ichiro Exp $	*/
/*
 * Copyright (c) 2001, 2002
 *     Shingo WATANABE <nabe@nabechan.org>.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
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
 *
 */

#define	URL_IFACE_INDEX		0
#define	URL_CONFIG_NO		1

#define	URL_TX_LIST_CNT		1
#define	URL_RX_LIST_CNT		1

#define	URL_TX_TIMEOUT		1000
#define	URL_TIMEOUT		10000

/* Packet length */
#define	URL_MAX_MTU		1536
#define	URL_MIN_FRAME_LEN	60
#define	URL_BUFSZ		URL_MAX_MTU

/* Request */
#define	URL_REQ_MEM		0x05

#define	URL_CMD_READMEM		1
#define	URL_CMD_WRITEMEM	2

/* Registers */
#define	URL_IDR0		0x0120 /* Ethernet Address, load from 93C46 */
#define	URL_IDR1		0x0121 /* Ethernet Address, load from 93C46 */
#define	URL_IDR2		0x0122 /* Ethernet Address, load from 93C46 */
#define	URL_IDR3		0x0123 /* Ethernet Address, load from 93C46 */
#define	URL_IDR4		0x0124 /* Ethernet Address, load from 93C46 */
#define	URL_IDR5		0x0125 /* Ethernet Address, load from 93C46 */

#define	URL_MAR0		0x0126 /* Multicast register */
#define	URL_MAR1		0x0127 /* Multicast register */
#define	URL_MAR2		0x0128 /* Multicast register */
#define	URL_MAR3		0x0129 /* Multicast register */
#define	URL_MAR4		0x012a /* Multicast register */
#define	URL_MAR5		0x012b /* Multicast register */
#define	URL_MAR6		0x012c /* Multicast register */
#define	URL_MAR7		0x012d /* Multicast register */
#define	URL_MAR			URL_MAR0

#define	URL_CR			0x012e /* Command Register */
#define	 URL_CR_WEPROM		(1<<5) /* EEPROM Write Enable */
#define	 URL_CR_SOFT_RST	(1<<4) /* Software Reset */
#define	 URL_CR_RE		(1<<3) /* Ethernet Receive Enable */
#define	 URL_CR_TE		(1<<2) /* Ethernet Transmit Enable */
#define	 URL_CR_EP3CLREN	(1<<1) /* Enable clearing the performance counter */
#define	 URL_CR_AUTOLOAD	(1<<0) /* Auto-load the contents of 93C46 */

#define	URL_TCR			0x012f /* Transmit Control Register */
#define	 URL_TCR_TXRR1		(1<<7) /* TX Retry Count */
#define	 URL_TCR_TXRR0		(1<<6) /* TX Retry Count */
#define	 URL_TCR_IFG1		(1<<4) /* Interframe Gap Time */
#define	 URL_TCR_IFG0		(1<<3) /* Interframe Gap Time */
#define	 URL_TCR_NOCRC		(1<<0) /* no CRC Append */

#define	URL_RCR			0x0130 /* Receive Configuration Register */
#define	 URL_RCR_TAIL		(1<<7)
#define	 URL_RCR_AER		(1<<6)
#define	 URL_RCR_AR		(1<<5)
#define	 URL_RCR_AM		(1<<4)
#define	 URL_RCR_AB		(1<<3)
#define	 URL_RCR_AD		(1<<2)
#define	 URL_RCR_AAM		(1<<1)
#define	 URL_RCR_AAP		(1<<0)

#define	URL_MSR			0x137 /* Media Status Register */
#define	 URL_MSR_TXFCE		(1<<7)
#define	 URL_MSR_RXFCE		(1<<6)
#define	 URL_MSR_DUPLEX		(1<<4)
#define	 URL_MSR_SPEED_100	(1<<3)
#define	 URL_MSR_LINK		(1<<2)
#define	 URL_MSR_TXPF		(1<<1)
#define	 URL_MSR_RXPF		(1<<0)

#define	URL_PHYADD		0x138 /* MII PHY Address select */
#define	 URL_PHYADD_MASK	0x1f /* MII PHY Address select */

#define	URL_PHYDAT		0x139 /* MII PHY data */

#define	URL_PHYCNT		0x13b /* MII PHY control */
#define	 URL_PHYCNT_PHYOWN	(1<<6) /* Own bit */
#define	 URL_PHYCNT_RWCR	(1<<5) /* MII management data R/W control */
#define	 URL_PHY_PHYOFF_MASK	0x1f /* PHY register offset */

#define	URL_BMCR		0x140 /* Basic mode control register */
#define	URL_BMSR		0x142 /* Basic mode status register */
#define	URL_ANAR		0x144 /* Auto-negotiation advertisement register */
#define	URL_ANLP		0x146 /* Auto-negotiation link partner ability register */


typedef	uWord url_rxhdr_t;	/* Receive Header */
#define	URL_RXHDR_BYTEC_MASK	(0x0fff) /* RX bytes count */
#define	URL_RXHDR_VALID_MASK	(0x1000) /* Valid packet */
#define	URL_RXHDR_RUNTPKT_MASK	(0x2000) /* Runt packet */
#define	URL_RXHDR_PHYPKT_MASK	(0x4000) /* Physical match packet */
#define	URL_RXHDR_MCASTPKT_MASK	(0x8000) /* Multicast packet */

#define	GET_IFP(sc)		(&(sc)->sc_ac.ac_if)
#define	GET_MII(sc)		(&(sc)->sc_mii)

struct url_chain {
	struct url_softc	*url_sc;
	struct usbd_xfer	*url_xfer;
	char			*url_buf;
	struct mbuf		*url_mbuf;
	int			url_idx;
};

struct url_cdata {
	struct url_chain	url_tx_chain[URL_TX_LIST_CNT];
	struct url_chain	url_rx_chain[URL_TX_LIST_CNT];
#if 0
	/* XXX: Interrupt Endpoint is not yet supported! */
	struct url_intrpkg	url_ibuf;
#endif
	int			url_tx_prod;
	int			url_tx_cons;
	int			url_tx_cnt;
	int			url_rx_prod;
};

struct url_softc {
	struct device		sc_dev;	/* base device */
	struct usbd_device	*sc_udev;

	/* USB */
	struct usbd_interface	*sc_ctl_iface;
	/* int			sc_ctl_iface_no; */
	int			sc_bulkin_no; /* bulk in endpoint */
	int			sc_bulkout_no; /* bulk out endpoint */
	int			sc_intrin_no; /* intr in endpoint */
	struct usbd_pipe	*sc_pipe_rx;
	struct usbd_pipe	*sc_pipe_tx;
	struct usbd_pipe	*sc_pipe_intr;
	struct timeout		sc_stat_ch;
	u_int			sc_rx_errs;
	/* u_int		sc_intr_errs; */
	struct timeval		sc_rx_notice;

	/* Ethernet */
	struct arpcom		sc_ac; /* ethernet common */
	struct mii_data		sc_mii;
	struct rwlock		sc_mii_lock;
	int			sc_link;
#define	sc_media url_mii.mii_media
	struct url_cdata	sc_cdata;

        int                     sc_refcnt;

	struct usb_task		sc_tick_task;
	struct usb_task		sc_stop_task;

	u_int16_t		sc_flags;
};
