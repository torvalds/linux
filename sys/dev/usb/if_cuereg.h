/*	$OpenBSD: if_cuereg.h,v 1.13 2024/09/01 03:09:00 jsg Exp $ */
/*	$NetBSD: if_cuereg.h,v 1.14 2001/01/21 22:09:24 augustss Exp $	*/
/*
 * Copyright (c) 1997, 1998, 1999, 2000
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
 * $FreeBSD: src/sys/dev/usb/if_cuereg.h,v 1.3 2000/01/16 22:45:06 wpaul Exp $
 */

/*
 * Definitions for the CATC Netmate II USB to ethernet controller.
 */


/*
 * Vendor specific control commands.
 */
#define CUE_CMD_READSRAM			0xF1
#define CUE_CMD_GET_MACADDR			0xF2
#define CUE_CMD_RESET				0xF4
#define CUE_CMD_WRITEREG			0xFA
#define CUE_CMD_READREG				0xFB
#define CUE_CMD_WRITESRAM			0xFC

/*
 * Internal registers
 */
#define CUE_TX_BUFCNT				0x20
#define CUE_RX_BUFCNT				0x21
#define CUE_ADVANCED_OPMODES			0x22
#define CUE_TX_BUFPKTS				0x23
#define CUE_RX_BUFPKTS				0x24
#define CUE_RX_MAXCHAIN				0x25

#define CUE_ETHCTL				0x60
#define CUE_ETHSTS				0x61
#define CUE_PAR5				0x62
#define CUE_PAR4				0x63
#define CUE_PAR3				0x64
#define CUE_PAR2				0x65
#define CUE_PAR1				0x66
#define CUE_PAR0				0x67

/* Error counters, all 16 bits wide. */
#define CUE_TX_SINGLECOLL			0x69
#define CUE_TX_MULTICOLL			0x6B
#define CUE_TX_EXCESSCOLL			0x6D
#define CUE_RX_FRAMEERR				0x6F

#define CUE_LEDCTL				0x81

/* Advanced operating mode register */
#define CUE_AOP_SRAMWAITS			0x03
#define CUE_AOP_EMBED_RXLEN			0x08
#define CUE_AOP_RXCOMBINE			0x10
#define CUE_AOP_TXCOMBINE			0x20
#define CUE_AOP_EVEN_PKT_READS			0x40
#define CUE_AOP_LOOPBK				0x80

/* Ethernet control register */
#define CUE_ETHCTL_RX_ON			0x01
#define CUE_ETHCTL_LINK_POLARITY		0x02
#define CUE_ETHCTL_LINK_FORCE_OK		0x04
#define CUE_ETHCTL_MCAST_ON			0x08
#define CUE_ETHCTL_PROMISC			0x10

/* Ethernet status register */
#define CUE_ETHSTS_NO_CARRIER			0x01
#define CUE_ETHSTS_LATECOLL			0x02
#define CUE_ETHSTS_EXCESSCOLL			0x04
#define CUE_ETHSTS_TXBUF_AVAIL			0x08
#define CUE_ETHSTS_BAD_POLARITY			0x10
#define CUE_ETHSTS_LINK_OK			0x20

/* LED control register */
#define CUE_LEDCTL_BLINK_1X			0x00
#define CUE_LEDCTL_BLINK_2X			0x01
#define CUE_LEDCTL_BLINK_QUARTER_ON		0x02
#define CUE_LEDCTL_BLINK_QUARTER_OFF		0x03
#define CUE_LEDCTL_OFF				0x04
#define CUE_LEDCTL_FOLLOW_LINK			0x08

/*
 * Address in ASIC's internal SRAM where the
 * multicast hash table lives. The table is 64 bytes long,
 * giving us a 512-bit table. We have to set the bit that
 * corresponds to the broadcast address in order to enable
 * reception of broadcast frames.
 */
#define CUE_MCAST_TABLE_ADDR			0xFA80
#define CUE_MCAST_TABLE_LEN			64

#define CUE_TIMEOUT		1000
#define CUE_BUFSZ		1536
#define CUE_MIN_FRAMELEN	60
#define CUE_RX_FRAMES		1
#define CUE_TX_FRAMES		1

#define CUE_RX_LIST_CNT		1
#define CUE_TX_LIST_CNT		1

#define CUE_CTL_READ		0x01
#define CUE_CTL_WRITE		0x02

#define CUE_CONFIG_NO		1
#define CUE_IFACE_IDX		0

/*
 * The interrupt endpoint is currently unused by the CATC part.
 */
#define CUE_ENDPT_RX		0x0
#define CUE_ENDPT_TX		0x1
#define CUE_ENDPT_INTR		0x2
#define CUE_ENDPT_MAX		0x3

struct cue_type {
	u_int16_t		cue_vid;
	u_int16_t		cue_did;
};

struct cue_softc;

struct cue_chain {
	struct cue_softc	*cue_sc;
	struct usbd_xfer	*cue_xfer;
	char			*cue_buf;
	struct mbuf		*cue_mbuf;
	int			cue_idx;
};

struct cue_cdata {
	struct cue_chain	cue_tx_chain[CUE_TX_LIST_CNT];
	struct cue_chain	cue_rx_chain[CUE_RX_LIST_CNT];
	int			cue_tx_prod;
	int			cue_tx_cons;
	int			cue_tx_cnt;
	int			cue_rx_prod;
};

struct cue_softc {
	struct device		cue_dev;

	struct arpcom		arpcom;
#define GET_IFP(sc) (&(sc)->arpcom.ac_if)

	struct timeout		cue_stat_ch;

	struct usbd_device	*cue_udev;
	struct usbd_interface	*cue_iface;
	u_int16_t		cue_vendor;
	u_int16_t		cue_product;
	int			cue_ed[CUE_ENDPT_MAX];
	struct usbd_pipe	*cue_ep[CUE_ENDPT_MAX];
	u_int8_t		cue_mctab[CUE_MCAST_TABLE_LEN];
	int			cue_if_flags;
	u_int16_t		cue_rxfilt;
	struct cue_cdata	cue_cdata;

	u_int			cue_rx_errs;
	struct timeval		cue_rx_notice;

	struct usb_task		cue_tick_task;
	struct usb_task		cue_stop_task;
};
