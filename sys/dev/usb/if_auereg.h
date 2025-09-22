/*	$OpenBSD: if_auereg.h,v 1.18 2015/06/18 10:02:49 mpi Exp $ */
/*	$NetBSD: if_auereg.h,v 1.16 2001/10/10 02:14:17 augustss Exp $	*/
/*
 * Copyright (c) 1997, 1998, 1999
 *	Bill Paul <wpaul@ee.columbia.edu>.  All rights reserved.
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
 *	This product includes software developed by Bill Paul.
 * 4. Neither the name of the author nor the names of any co-contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY Bill Paul AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL Bill Paul OR THE VOICES IN HIS HEAD
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD: src/sys/dev/usb/if_auereg.h,v 1.2 2000/01/08 06:52:36 wpaul Exp $
 */

/*
 * Register definitions for ADMtek Pegasus AN986 USB to Ethernet
 * chip. The Pegasus uses a total of four USB endpoints: the control
 * endpoint (0), a bulk read endpoint for receiving packets (1),
 * a bulk write endpoint for sending packets (2) and an interrupt
 * endpoint for passing RX and TX status (3). Endpoint 0 is used
 * to read and write the ethernet module's registers. All registers
 * are 8 bits wide.
 *
 * Packet transfer is done in 64 byte chunks. The last chunk in a
 * transfer is denoted by having a length less that 64 bytes. For
 * the RX case, the data includes an optional RX status word.
 */

#define AUE_UR_READREG		0xF0
#define AUE_UR_WRITEREG		0xF1

/*
 * Note that while the ADMtek technically has four
 * endpoints, the control endpoint (endpoint 0) is
 * regarded as special by the USB code and drivers
 * don't have direct access to it. (We access it
 * using usbd_do_request() when reading/writing
 * registers.) Consequently, our endpoint indexes
 * don't match those in the ADMtek Pegasus manual:
 * we consider the RX data endpoint to be index 0
 * and work up from there.
 */
#define AUE_ENDPT_RX		0x0
#define AUE_ENDPT_TX		0x1
#define AUE_ENDPT_INTR		0x2
#define AUE_ENDPT_MAX		0x3

#define AUE_CTL0		0x00
#define AUE_CTL1		0x01
#define AUE_CTL2		0x02
#define AUE_MAR0		0x08
#define AUE_MAR1		0x09
#define AUE_MAR2		0x0A
#define AUE_MAR3		0x0B
#define AUE_MAR4		0x0C
#define AUE_MAR5		0x0D
#define AUE_MAR6		0x0E
#define AUE_MAR7		0x0F
#define AUE_MAR			AUE_MAR0
#define AUE_PAR0		0x10
#define AUE_PAR1		0x11
#define AUE_PAR2		0x12
#define AUE_PAR3		0x13
#define AUE_PAR4		0x14
#define AUE_PAR5		0x15
#define AUE_PAR			AUE_PAR0
#define AUE_PAUSE0		0x18
#define AUE_PAUSE1		0x19
#define AUE_PAUSE		AUE_PAUSE0
#define AUE_RX_FLOWCTL_CNT	0x1A
#define AUE_RX_FLOWCTL_FIFO	0x1B
#define AUE_REG_1D		0x1D
#define AUE_EE_REG		0x20
#define AUE_EE_DATA0		0x21
#define AUE_EE_DATA1		0x22
#define AUE_EE_DATA		AUE_EE_DATA0
#define AUE_EE_CTL		0x23
#define AUE_PHY_ADDR		0x25
#define AUE_PHY_DATA0		0x26
#define AUE_PHY_DATA1		0x27
#define AUE_PHY_DATA		AUE_PHY_DATA0
#define AUE_PHY_CTL		0x28
#define AUE_USB_STS		0x2A
#define AUE_TXSTAT0		0x2B
#define AUE_TXSTAT1		0x2C
#define AUE_TXSTAT		AUE_TXSTAT0
#define AUE_RXSTAT		0x2D
#define AUE_PKTLOST0		0x2E
#define AUE_PKTLOST1		0x2F
#define AUE_PKTLOST		AUE_PKTLOST0

#define AUE_REG_7B		0x7B
#define AUE_GPIO0		0x7E
#define AUE_GPIO1		0x7F
#define AUE_REG_81		0x81

#define AUE_CTL0_INCLUDE_RXCRC	0x01
#define AUE_CTL0_ALLMULTI	0x02
#define AUE_CTL0_STOP_BACKOFF	0x04
#define AUE_CTL0_RXSTAT_APPEND	0x08
#define AUE_CTL0_WAKEON_ENB	0x10
#define AUE_CTL0_RXPAUSE_ENB	0x20
#define AUE_CTL0_RX_ENB		0x40
#define AUE_CTL0_TX_ENB		0x80

#define AUE_CTL1_HOMELAN	0x04
#define AUE_CTL1_RESETMAC	0x08
#define AUE_CTL1_SPEEDSEL	0x10	/* 0 = 10mbps, 1 = 100mbps */
#define AUE_CTL1_DUPLEX		0x20	/* 0 = half, 1 = full */
#define AUE_CTL1_DELAYHOME	0x40

#define AUE_CTL2_EP3_CLR	0x01	/* reading EP3 clrs status regs */
#define AUE_CTL2_RX_BADFRAMES	0x02
#define AUE_CTL2_RX_PROMISC	0x04
#define AUE_CTL2_LOOPBACK	0x08
#define AUE_CTL2_EEPROMWR_ENB	0x10
#define AUE_CTL2_EEPROM_LOAD	0x20

#define AUE_EECTL_WRITE		0x01
#define AUE_EECTL_READ		0x02
#define AUE_EECTL_DONE		0x04

#define AUE_PHYCTL_PHYREG	0x1F
#define AUE_PHYCTL_WRITE	0x20
#define AUE_PHYCTL_READ		0x40
#define AUE_PHYCTL_DONE		0x80

#define AUE_USBSTS_SUSPEND	0x01
#define AUE_USBSTS_RESUME	0x02

#define AUE_TXSTAT0_JABTIMO	0x04
#define AUE_TXSTAT0_CARLOSS	0x08
#define AUE_TXSTAT0_NOCARRIER	0x10
#define AUE_TXSTAT0_LATECOLL	0x20
#define AUE_TXSTAT0_EXCESSCOLL	0x40
#define AUE_TXSTAT0_UNDERRUN	0x80

#define AUE_TXSTAT1_PKTCNT	0x0F
#define AUE_TXSTAT1_FIFO_EMPTY	0x40
#define AUE_TXSTAT1_FIFO_FULL	0x80

#define AUE_RXSTAT_OVERRUN	0x01
#define AUE_RXSTAT_PAUSE	0x02

#define AUE_GPIO_IN0		0x01
#define AUE_GPIO_OUT0		0x02
#define AUE_GPIO_SEL0		0x04
#define AUE_GPIO_IN1		0x08
#define AUE_GPIO_OUT1		0x10
#define AUE_GPIO_SEL1		0x20

struct aue_intrpkt {
	u_int8_t		aue_txstat0;
	u_int8_t		aue_txstat1;
	u_int8_t		aue_rxstat;
	u_int8_t		aue_rxlostpkt0;
	u_int8_t		aue_rxlostpkt1;
	u_int8_t		aue_wakeupstat;
	u_int8_t		aue_rsvd;
	u_int8_t		_pad;
};
#define AUE_INTR_PKTLEN 8

struct aue_rxpkt {
	uWord			aue_pktlen;
	uByte			aue_rxstat;
};

#define AUE_RXSTAT_MCAST	0x01
#define AUE_RXSTAT_GIANT	0x02
#define AUE_RXSTAT_RUNT		0x04
#define AUE_RXSTAT_CRCERR	0x08
#define AUE_RXSTAT_DRIBBLE	0x10
#define AUE_RXSTAT_MASK		0x1E


/*************** The rest belongs in if_auevar.h *************/

#define AUE_TX_LIST_CNT		1
#define AUE_RX_LIST_CNT		1

struct aue_softc;

struct aue_chain {
	struct aue_softc	*aue_sc;
	struct usbd_xfer	*aue_xfer;
	char			*aue_buf;
	struct mbuf		*aue_mbuf;
	int			aue_idx;
};

struct aue_cdata {
	struct aue_chain	aue_tx_chain[AUE_TX_LIST_CNT];
	struct aue_chain	aue_rx_chain[AUE_RX_LIST_CNT];
	struct aue_intrpkt	aue_ibuf;
	int			aue_tx_prod;
	int			aue_tx_cons;
	int			aue_tx_cnt;
	int			aue_rx_prod;
};

struct aue_softc {
	struct device		aue_dev;

	struct arpcom		arpcom;
	struct mii_data		aue_mii;
#define GET_IFP(sc) (&(sc)->arpcom.ac_if)
#define GET_MII(sc) (&(sc)->aue_mii)

	struct timeout		aue_stat_ch;

	struct usbd_device	*aue_udev;
	struct usbd_interface	*aue_iface;
	u_int16_t		aue_vendor;
	u_int16_t		aue_product;
	int			aue_ed[AUE_ENDPT_MAX];
	struct usbd_pipe	*aue_ep[AUE_ENDPT_MAX];
	u_int8_t		aue_link;
	struct aue_cdata	aue_cdata;

	u_int16_t		aue_flags;

	int			aue_refcnt;
	u_int			aue_rx_errs;
	u_int			aue_intr_errs;
	struct timeval		aue_rx_notice;

	struct usb_task		aue_tick_task;
	struct usb_task		aue_stop_task;

	struct rwlock		aue_mii_lock;
};

#define AUE_TIMEOUT		1000
#define AUE_BUFSZ		1536
#define AUE_MIN_FRAMELEN	60
#define AUE_TX_TIMEOUT		10000 /* ms */
#define AUE_INTR_INTERVAL	100 /* ms */
