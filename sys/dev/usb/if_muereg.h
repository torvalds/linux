/*	$OpenBSD: if_muereg.h,v 1.2 2018/08/15 07:13:51 kevlo Exp $	*/

/*
 * Copyright (c) 2018 Kevin Lo <kevlo@openbsd.org>
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

/*
 * USB vendor requests.
 */
#define MUE_UR_WRITEREG         0xa0
#define MUE_UR_READREG		0xa1

/*
 * Offset of MAC address inside EEPROM.
 */
#define	MUE_EE_IND_OFFSET	0x00
#define	MUE_EE_MAC_OFFSET	0x01
#define	MUE_EE_LTM_OFFSET	0x3f

#define MUE_INT_STATUS		0x00c
#define MUE_HW_CFG		0x010
#define MUE_PMT_CTL		0x014
#define MUE_DP_SEL		0x024
#define MUE_DP_CMD		0x028
#define MUE_DP_ADDR		0x02c
#define MUE_DP_DATA		0x030
#define MUE_BURST_CAP		0x034
#define MUE_BULK_IN_DELAY	0x03c
#define MUE_E2P_CMD		0x040
#define MUE_E2P_DATA		0x044
#define MUE_RFE_CTL		0x060
#define MUE_USB_CFG0		0x080
#define MUE_USB_CFG1		0x084
#define MUE_FCT_RX_CTL		0x090
#define MUE_FCT_TX_CTL		0x094
#define MUE_FCT_RX_FIFO_END	0x098
#define MUE_FCT_TX_FIFO_END	0x098
#define MUE_FCT_FLOW		0x0a0
#define MUE_7800_RFE_CTL	0x0b0
#define MUE_7800_FCT_RX_CTL	0x0c0
#define MUE_7800_FCT_TX_CTL	0x0c4
#define MUE_7800_FCT_FLOW	0x0d0
#define MUE_LTM_INDEX(idx)	(0x0e0 + (idx) * 4)
#define MUE_MAC_CR		0x100
#define MUE_MAC_RX		0x104
#define MUE_MAC_TX		0x108
#define MUE_FLOW		0x10c
#define MUE_RX_ADDRH		0x118
#define MUE_RX_ADDRL		0x11c
#define MUE_MII_ACCESS		0x120
#define MUE_MII_DATA		0x124
#define MUE_ADDR_FILTX		0x300
#define MUE_7800_ADDR_FILTX	0x400

#define MUE_7800_BURST_CAP	MUE_FCT_RX_CTL
#define MUE_7800_BULK_IN_DELAY	MUE_FCT_TX_CTL

/* Hardware configuration register */
#define MUE_HW_CFG_SRST		0x00000001
#define MUE_HW_CFG_LRST		0x00000002
#define MUE_HW_CFG_BCE		0x00000004
#define MUE_HW_CFG_MEF		0x00000010
#define MUE_HW_CFG_BIR		0x00000080
#define MUE_HW_CFG_LED0_EN	0x00100000
#define MUE_HW_CFG_LED1_EN	0x00200000

/* Power management control register */
#define MUE_PMT_CTL_PHY_RST	0x00000010
#define MUE_PMT_CTL_READY	0x00000080

/* Data port select register */
#define MUE_DP_SEL_RSEL_MASK	0x0000000f
#define MUE_DP_SEL_VHF		0x00000001
#define MUE_DP_SEL_DPRDY	0x80000000
#define MUE_DP_SEL_VHF_HASH_LEN	16
#define MUE_DP_SEL_VHF_VLAN_LEN	128

/* Data port command register */
#define MUE_DP_CMD_WRITE	0x00000001

/* EEPROM command register */
#define MUE_E2P_CMD_ADDR_MASK	0x000001ff
#define MUE_E2P_CMD_READ	0x00000000
#define MUE_E2P_CMD_TIMEOUT	0x00000400
#define MUE_E2P_CMD_BUSY	0x80000000

/* Receive filtering engine control register */
#define MUE_RFE_CTL_PERFECT		0x00000002
#define MUE_RFE_CTL_MULTICAST_HASH	0x00000008
#define MUE_RFE_CTL_UNICAST		0x00000100
#define MUE_RFE_CTL_MULTICAST		0x00000200
#define MUE_RFE_CTL_BROADCAST		0x00000400

/* USB configuration register 0 */
#define MUE_USB_CFG0_BCE	0x00000020
#define MUE_USB_CFG0_BIR	0x00000040

/* USB configuration register 1 */
#define MUE_USB_CFG1_LTM_ENABLE		0x00000100
#define MUE_USB_CFG1_DEV_U1_INIT_EN	0x00000400
#define MUE_USB_CFG1_DEV_U2_INIT_EN	0x00001000

/* RX FIFO control register */
#define MUE_FCT_RX_CTL_EN	0x80000000

/* TX FIFO control register */
#define MUE_FCT_TX_CTL_EN	0x80000000

/* MAC control register */
#define MUE_MAC_CR_RST		0x00000001
#define MUE_MAC_CR_FULL_DUPLEX	0x00000008
#define MUE_MAC_CR_AUTO_SPEED	0x00000800
#define MUE_MAC_CR_AUTO_DUPLEX	0x00001000
#define MUE_MAC_CR_GMII_EN	0x00080000

/* MAC receive register */
#define MUE_MAC_RX_RXEN			0x00000001
#define MUE_MAC_RX_MAX_SIZE_MASK	0x3fff0000
#define MUE_MAC_RX_MAX_SIZE_SHIFT	16
#define MUE_MAC_RX_MAX_LEN(x)	\
	(((x) << MUE_MAC_RX_MAX_SIZE_SHIFT) & MUE_MAC_RX_MAX_SIZE_MASK)

/* MAC transmit register */
#define MUE_MAC_TX_TXEN		0x00000001

/* Flow control register */
#define MUE_FLOW_PAUSE_TIME	0x0000ffff
#define MUE_FLOW_RX_FCEN	0x20000000
#define MUE_FLOW_TX_FCEN	0x40000000

/* MII access register */
#define MUE_MII_ACCESS_READ		0x00000000
#define MUE_MII_ACCESS_BUSY		0x00000001
#define MUE_MII_ACCESS_WRITE		0x00000002
#define MUE_MII_ACCESS_REGADDR_MASK	0x000007c0
#define MUE_MII_ACCESS_REGADDR_SHIFT	6
#define MUE_MII_ACCESS_PHYADDR_MASK	0x0000f800
#define MUE_MII_ACCESS_PHYADDR_SHIFT	11
#define MUE_MII_ACCESS_REGADDR(x)	\
	(((x) << MUE_MII_ACCESS_REGADDR_SHIFT) & MUE_MII_ACCESS_REGADDR_MASK)
#define MUE_MII_ACCESS_PHYADDR(x)	\
	(((x) << MUE_MII_ACCESS_PHYADDR_SHIFT) & MUE_MII_ACCESS_PHYADDR_MASK)

/* MAC address perfect filter register */
#define MUE_ADDR_FILTX_VALID	0x80000000

#define MUE_DEFAULT_BULKIN_DELAY	0x00002000
#define MUE_7800_DEFAULT_BULKIN_DELAY	0x00000800

#define MUE_BURST_MAX_BUFSZ		129
#define MUE_BURST_MIN_BUFSZ		37
#define MUE_7800_BURST_MAX_BUFSZ	24
#define MUE_7800_BURST_MIN_BUFSZ	12

#define MUE_7800_BUFSZ		12288
#define MUE_MAX_BUFSZ		18944
#define MUE_MIN_BUFSZ		8256

#define MUE_EEPROM_INDICATOR	0xa5

/*
 * The interrupt endpoint is currently unused by the Moschip part.
 */
#define MUE_ENDPT_RX		0x0
#define MUE_ENDPT_TX		0x1
#define MUE_ENDPT_INTR		0x2
#define MUE_ENDPT_MAX		0x3

#define MUE_RX_LIST_CNT		1
#define MUE_TX_LIST_CNT		1

struct mue_softc;

struct mue_chain {
	struct mue_softc	*mue_sc;
	struct usbd_xfer	*mue_xfer;
	char			*mue_buf;
	struct mbuf		*mue_mbuf;
	int			mue_accum;
	int			mue_idx;
};

struct mue_cdata {
	struct mue_chain	mue_tx_chain[MUE_TX_LIST_CNT];
	struct mue_chain	mue_rx_chain[MUE_RX_LIST_CNT];
	int			mue_tx_prod;
	int			mue_tx_cons;
	int			mue_tx_cnt;
	int			mue_rx_prod;
};

struct mue_rxbuf_hdr {
	uint32_t		rx_cmd_a;
#define MUE_RX_CMD_A_RED	0x00400000
#define MUE_RX_CMD_A_LEN_MASK	0x00003fff

	uint32_t		rx_cmd_b;
	uint16_t		rx_cmd_c;
} __packed;

struct mue_txbuf_hdr {
	uint32_t		tx_cmd_a;
#define MUE_TX_CMD_A_FCS	0x00400000
#define MUE_TX_CMD_A_LEN_MASK	0x000fffff

	uint32_t		tx_cmd_b;
} __packed;

struct mue_softc {
	struct device		mue_dev;

	struct arpcom		arpcom;
	struct mii_data		mue_mii;
#define GET_MII(sc)	(&(sc)->mue_mii)
#define GET_IFP(sc)	(&(sc)->arpcom.ac_if)

	int			mue_ed[MUE_ENDPT_MAX];
	struct usbd_pipe	*mue_ep[MUE_ENDPT_MAX];
	struct mue_cdata	mue_cdata;
	struct timeout		mue_stat_ch;

	struct usbd_device	*mue_udev;
	struct usbd_interface	*mue_iface;

	struct usb_task		mue_tick_task;
	struct usb_task		mue_stop_task;

	struct rwlock		mue_mii_lock;

	struct timeval		mue_rx_notice;

	uint16_t		mue_product;
	uint16_t		mue_flags;

	int			mue_refcnt;

	int			mue_phyno;
	int			mue_bufsz;
	int			mue_link;
	int			mue_eeprom_present;
};
