/*	$OpenBSD: if_axenreg.h,v 1.8 2024/10/07 07:35:40 kevlo Exp $	*/

/*
 * Copyright (c) 2013 Yojiro UO <yuo@openbsd.org>. All right reserved.
 *
 */

/*
 * Definitions for the ASIX Electronics AX88179 to ethernet controller.
 */

#define AXEN_PHY_ID		0x0003
#define AXEN_MCAST_FILTER_SIZE	8
/* unit: KB */
#define AXEN_BUFSZ_LS		8
#define AXEN_BUFSZ_HS		16
#define AXEN_BUFSZ_SS		24

#define AXEN_REV_UA1		0
#define AXEN_REV_UA2		1

/* receive header */
/* 
 *                     +-multicast/broadcast
 *                     |    +-rx_ok
 *                     |    |     ++-----L3_type (1:ipv4, 0/2:ipv6)
 *        pkt_len(13)  |    |     ||+ ++-L4_type(0: icmp, 1: UDP, 4: TCP)
 * |765|43210 76543210|7654 3210 7654 3210|
 *  ||+-crc_err              |+-L4_err |+-L4_CSUM_ERR
 *  |+-mii_err               +--L3_err +--L3_CSUM_ERR
 *  +-drop_err
 *
 * ex) pkt_hdr 0x00680820
 *      drop_err, crc_err: none
 *      pkt_length = 104 byte
 *      0x0820 = 0000 1000 0010 0000  => ipv4 icmp
 *
 * ex) pkt_hdr 0x004c8800
 *      drop_err, crc_err: none
 *      pkt_length = 76 byte
 *      0x8800 = 1000 1000 0000 0000 => ipv6 mcast icmp
 *
 *  [memo]
 *  0x0820: ipv4 icmp			0000 1000 0010 0000
 *  0x8820: ipv4 icmp (broadcast)	1000 1000 0010 0000
 *  0x0824: ipv4 udp (nping)		0000 1000 0010 0100
 *  0x0830: ipv4 tcp (ssh)		0000 1000 0011 0000
 *
 *  0x0800: ipv6 icmp			0000 1000 0000 0000
 *  0x8800: ipv6 icmp (multicast)	1000 1000 0000 0000
 *  0x8844: ipv6 UDP/MDNS mcast		1000 1000 0100 0100
 *  0x0850: ipv6 tcp (ssh)		0000 1000 0101 0000
 */

#define	AXEN_RXHDR_DROP_ERR	(1U << 31)
#define AXEN_RXHDR_MII_ERR	(1U << 30)
#define	AXEN_RXHDR_CRC_ERR	(1U << 29)
#define AXEN_RXHDR_MCAST	(1U << 15)
#define AXEN_RXHDR_RX_OK	(1U << 11)
#define	AXEN_RXHDR_L3_ERR	(1U << 9)
#define	AXEN_RXHDR_L4_ERR	(1U << 8)
#define AXEN_RXHDR_L3CSUM_ERR 	(1U << 1)
#define AXEN_RXHDR_L4CSUM_ERR	(1U << 0)

/* L4 packet type (3bit) */
#define AXEN_RXHDR_L4_TYPE_MASK	0x0000001c
#define AXEN_RXHDR_L4_TYPE_OFFSET	2
#define   AXEN_RXHDR_L4_TYPE_ICMP	0x0
#define   AXEN_RXHDR_L4_TYPE_UDP	0x1
#define   AXEN_RXHDR_L4_TYPE_TCP	0x4

/* L3 packet type (2bit) */
#define AXEN_RXHDR_L3_TYPE_MASK	0x00000600
#define AXEN_RXHDR_L3_TYPE_OFFSET	5
#define   AXEN_RXHDR_L3_TYPE_UNDEF	0x0
#define   AXEN_RXHDR_L3_TYPE_IPV4	0x1
#define   AXEN_RXHDR_L3_TYPE_IPV6	0x2

/*
 * commands
 */
#define AXEN_CMD_LEN(x)	(((x) & 0xF000) >> 12)
#define AXEN_CMD_DIR(x)	(((x) & 0x0F00) >> 8)
#define AXEN_CMD_CMD(x)	 ((x) & 0x00FF)

/* ---MAC--- */
/*   1byte cmd   */ 
#define AXEN_CMD_MAC_READ			0x1001
#define AXEN_CMD_MAC_WRITE			0x1101

#define   AXEN_USB_UPLINK			0x02
#define     AXEN_USB_FS				  0x01
#define     AXEN_USB_HS				  0x02
#define     AXEN_USB_SS				  0x04
#define   AXEN_GENERAL_STATUS			0x03
#define     AXEN_GENERAL_STATUS_MASK		  0x4
#define     AXEN_REV0				  0x0
#define     AXEN_REV1				  0x4
#define   AXEN_UNK_05				0x05
#define   AXEN_MAC_EEPROM_ADDR			0x07
#define   AXEN_MAC_EEPROM_READ			0x08
#define   AXEN_MAC_EEPROM_CMD			0x0a
#define     AXEN_EEPROM_READ			  0x04
#define     AXEN_EEPROM_WRITE			  0x08
#define     AXEN_EEPROM_BUSY			  0x10
#define   AXEN_MONITOR_MODE			0x24
#define     AXEN_MONITOR_NONE			  0x00
#define     AXEN_MONITOR_RWLC			  0x02
#define     AXEN_MONITOR_RWMP			  0x04
#define     AXEN_MONITOR_RWWF			  0x08
#define     AXEN_MONITOR_RW_FLAG		  0x10
#define     AXEN_MONITOR_PMEPOL			  0x20
#define     AXEN_MONITOR_PMETYPE		  0x40
#define   AXEN_UNK_28				0x28
#define   AXEN_PHYCLK				0x33
#define     AXEN_PHYCLK_BCS			  0x01
#define     AXEN_PHYCLK_ACS			  0x02
#define     AXEN_PHYCLK_ULR			  0x08
#define     AXEN_PHYCLK_ACSREQ			  0x10
#define   AXEN_RX_COE				0x34
#define	    AXEN_RXCOE_OFF			  0x00
#define	    AXEN_RXCOE_IPv4			  0x01
#define	    AXEN_RXCOE_TCPv4			  0x02
#define	    AXEN_RXCOE_UDPv4			  0x04
#define	    AXEN_RXCOE_ICMP			  0x08
#define	    AXEN_RXCOE_IGMP			  0x10
#define	    AXEN_RXCOE_TCPv6			  0x20
#define	    AXEN_RXCOE_UDPv6			  0x40
#define	    AXEN_RXCOE_ICMPv6			  0x80
#define   AXEN_TX_COE				0x35
#define	    AXEN_TXCOE_OFF			  0x00
#define	    AXEN_TXCOE_IPv4			  0x01
#define	    AXEN_TXCOE_TCPv4			  0x02
#define	    AXEN_TXCOE_UDPv4			  0x04
#define	    AXEN_TXCOE_ICMP			  0x08
#define	    AXEN_TXCOE_IGMP			  0x10
#define	    AXEN_TXCOE_TCPv6			  0x20
#define	    AXEN_TXCOE_UDPv6			  0x40
#define	    AXEN_TXCOE_ICMPv6			  0x80
#define   AXEN_PAUSE_HIGH_WATERMARK		0x54
#define   AXEN_PAUSE_LOW_WATERMARK		0x55


/*   2byte cmd   */ 
#define AXEN_CMD_MAC_READ2			0x2001
#define AXEN_CMD_MAC_WRITE2			0x2101

#define   AXEN_MAC_RXCTL			0x0b
#define     AXEN_RXCTL_STOP			  0x0000
#define     AXEN_RXCTL_PROMISC			  0x0001
#define     AXEN_RXCTL_ACPT_ALL_MCAST		  0x0002
#define     AXEN_RXCTL_HA8B			  0x0004		 
#define     AXEN_RXCTL_AUTOB			  0x0008
#define     AXEN_RXCTL_ACPT_BCAST		  0x0010
#define     AXEN_RXCTL_ACPT_PHY_MCAST		  0x0020
#define     AXEN_RXCTL_START			  0x0080
#define     AXEN_RXCTL_DROPCRCERR		  0x0100
#define     AXEN_RXCTL_IPE			  0x0200
#define     AXEN_RXCTL_TXPADCRC			  0x0400
#define   AXEN_MEDIUM_STATUS			0x22
#define	    AXEN_MEDIUM_NONE			  0x0000
#define	    AXEN_MEDIUM_GIGA			  0x0001
#define	    AXEN_MEDIUM_FDX			  0x0002
#define	    AXEN_MEDIUM_ALWAYS_ONE		  0x0004
#define	    AXEN_MEDIUM_EN_125MHZ		  0x0008
#define	    AXEN_MEDIUM_RXFLOW_CTRL_EN		  0x0010
#define	    AXEN_MEDIUM_TXFLOW_CTRL_EN		  0x0020
#define	    AXEN_MEDIUM_RECV_EN			  0x0100
#define	    AXEN_MEDIUM_PS			  0x0200
#define	    AXEN_MEDIUM_JUMBO_EN		  0x8040
#define   AXEN_PHYPWR_RSTCTL			0x26
#define     AXEN_PHYPWR_RSTCTL_BZ		  0x0010
#define     AXEN_PHYPWR_RSTCTL_IPRL		  0x0020
#define     AXEN_PHYPWR_RSTCTL_AUTODETACH	  0x1000

#define AXEN_CMD_EEPROM_READ			0x2004
#define	    AXEN_EEPROM_STAT			  0x43

/*   5byte cmd   */ 
#define AXEN_CMD_MAC_SET_RXSR			0x5101
#define   AXEN_RX_BULKIN_QCTRL			  0x2e

/*   6byte cmd   */ 
#define AXEN_CMD_MAC_READ_ETHER			0x6001
#define AXEN_CMD_MAC_WRITE_ETHER		0x6101
#define   AXEN_CMD_MAC_NODE_ID			  0x10

/*   8byte cmd   */ 
#define AXEN_CMD_MAC_READ_FILTER		0x8001
#define AXEN_CMD_MAC_WRITE_FILTER		0x8101
#define   AXEN_FILTER_MULTI		 	  0x16

/* ---PHY--- */
/*   2byte cmd   */ 
#define AXEN_CMD_MII_READ_REG			0x2002
#define AXEN_CMD_MII_WRITE_REG			0x2102



/* ========= */
#define AXEN_GPIO0_EN		0x01
#define AXEN_GPIO0		0x02
#define AXEN_GPIO1_EN		0x04
#define AXEN_GPIO1		0x08
#define AXEN_GPIO2_EN		0x10
#define AXEN_GPIO2		0x20
#define AXEN_GPIO_RELOAD_EEPROM	0x80


#define AXEN_TIMEOUT		1000

#define AXEN_RX_LIST_CNT		1
#define AXEN_TX_LIST_CNT		1


/*
 * The interrupt endpoint is currently unused
 * by the ASIX part.
 */
#define AXEN_ENDPT_RX		0x0
#define AXEN_ENDPT_TX		0x1
#define AXEN_ENDPT_INTR		0x2
#define AXEN_ENDPT_MAX		0x3

struct axen_type {
	struct usb_devno	axen_dev;
	u_int16_t		axen_flags;
#define AX178A	0x0001		/* AX88178a */
#define AX179	0x0002		/* AX88179 */
#define AX179A	0x0004		/* AX88179a */
#define AX772D	0x0008		/* AX88772d */
};

struct axen_softc;

struct axen_chain {
	struct axen_softc	*axen_sc;
	struct usbd_xfer	*axen_xfer;
	char			*axen_buf;
	struct mbuf		*axen_mbuf;
	int			axen_accum;
	int			axen_idx;
};

struct axen_cdata {
	struct axen_chain	axen_tx_chain[AXEN_TX_LIST_CNT];
	struct axen_chain	axen_rx_chain[AXEN_RX_LIST_CNT];
	int			axen_tx_prod;
	int			axen_tx_cons;
	int			axen_tx_cnt;
	int			axen_rx_prod;
};

struct axen_qctrl {
	u_int8_t		ctrl;
	u_int8_t		timer_low;
	u_int8_t		timer_high;
	u_int8_t		bufsize;
	u_int8_t		ifg;
} __packed;

struct axen_sframe_hdr {
	u_int32_t		plen; /* packet length */
	u_int32_t		gso;
} __packed;

struct axen_softc {
	struct device		axen_dev;
#define GET_MII(sc) (&(sc)->axen_mii)
	struct arpcom		arpcom;
#define GET_IFP(sc) (&(sc)->arpcom.ac_if)
	struct mii_data		axen_mii;
	struct usbd_device	*axen_udev;
	struct usbd_interface	*axen_iface;

	u_int16_t		axen_vendor;
	u_int16_t		axen_product;

	u_int16_t		axen_flags;

	int			axen_ed[AXEN_ENDPT_MAX];
	struct usbd_pipe	*axen_ep[AXEN_ENDPT_MAX];
	int			axen_unit;
	struct axen_cdata	axen_cdata;
	struct timeout		axen_stat_ch;

	int			axen_refcnt;

	struct usb_task		axen_tick_task;
	struct usb_task		axen_stop_task;

	struct rwlock		axen_mii_lock;

	int			axen_link;
	unsigned char		axen_ipgs[3];
	int			axen_phyno;
	struct timeval		axen_rx_notice;
	u_int			axen_bufsz;
	int			axen_rev;
};
