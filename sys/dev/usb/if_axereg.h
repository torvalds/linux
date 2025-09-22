/*	$OpenBSD: if_axereg.h,v 1.26 2015/06/12 15:47:31 mpi Exp $	*/

/*
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
 * $FreeBSD: src/sys/dev/usb/if_axereg.h,v 1.2 2003/06/15 21:45:43 wpaul Exp $
 */

/*
 * Definitions for the ASIX Electronics AX88172 to ethernet controller.
 */


/*
 * Vendor specific commands
 * ASIX conveniently doesn't document the 'set NODEID' command in their
 * datasheet (thanks a lot guys).
 * To make handling these commands easier, I added some extra data
 * which is decided by the axe_cmd() routine. Commands are encoded
 * in 16 bites, with the format: LDCC. L and D are both nibbles in
 * the high byte. L represents the data length (0 to 15) and D
 * represents the direction (0 for vendor read, 1 for vendor write).
 * CC is the command byte, as specified in the manual.
 */

#define AXE_CMD_LEN(x)	(((x) & 0xF000) >> 12)
#define AXE_CMD_DIR(x)	(((x) & 0x0F00) >> 8)
#define AXE_CMD_CMD(x)	 ((x) & 0x00FF)

#define AXE_172_CMD_READ_RXTX_SRAM		0x2002
#define AXE_182_CMD_READ_RXTX_SRAM		0x8002
#define AXE_172_CMD_WRITE_RX_SRAM		0x0103
#define AXE_172_CMD_WRITE_TX_SRAM		0x0104
#define AXE_182_CMD_WRITE_RXTX_SRAM		0x8103
#define AXE_CMD_MII_OPMODE_SW			0x0106
#define AXE_CMD_MII_READ_REG			0x2007
#define AXE_CMD_MII_WRITE_REG			0x2108
#define AXE_CMD_MII_READ_OPMODE			0x1009
#define AXE_CMD_MII_OPMODE_HW			0x010A
#define AXE_CMD_SROM_READ			0x200B
#define AXE_CMD_SROM_WRITE			0x010C
#define AXE_CMD_SROM_WR_ENABLE			0x010D
#define AXE_CMD_SROM_WR_DISABLE			0x010E
#define AXE_CMD_RXCTL_READ			0x200F
#define AXE_CMD_RXCTL_WRITE			0x0110
#define AXE_CMD_READ_IPG012			0x3011
#define AXE_172_CMD_WRITE_IPG0			0x0112
#define AXE_172_CMD_WRITE_IPG1			0x0113
#define AXE_172_CMD_WRITE_IPG2			0x0114
#define AXE_178_CMD_WRITE_IPG012		0x0112
#define AXE_CMD_READ_MCAST			0x8015
#define AXE_CMD_WRITE_MCAST			0x8116
#define AXE_172_CMD_READ_NODEID			0x6017
#define AXE_172_CMD_WRITE_NODEID		0x6118
#define AXE_178_CMD_READ_NODEID			0x6013
#define AXE_178_CMD_WRITE_NODEID		0x6114
#define AXE_CMD_READ_PHYID			0x2019
#define AXE_172_CMD_READ_MEDIA			0x101A
#define AXE_178_CMD_READ_MEDIA			0x201A
#define AXE_CMD_WRITE_MEDIA			0x011B
#define AXE_CMD_READ_MONITOR_MODE		0x101C
#define AXE_CMD_WRITE_MONITOR_MODE		0x011D
#define AXE_CMD_READ_GPIO			0x101E
#define AXE_CMD_WRITE_GPIO			0x011F
#define AXE_CMD_SW_RESET_REG			0x0120
#define AXE_CMD_SW_PHY_STATUS			0x0021
#define AXE_CMD_SW_PHY_SELECT			0x0122

#define AXE_SW_RESET_CLEAR			0x00
#define AXE_SW_RESET_RR				0x01
#define AXE_SW_RESET_RT				0x02
#define AXE_SW_RESET_PRTE			0x04
#define AXE_SW_RESET_PRL			0x08
#define AXE_SW_RESET_BZ				0x10
#define AXE_SW_RESET_IPRL			0x20
#define AXE_SW_RESET_IPPD			0x40

/* AX88178 documentation says to always write this bit... */
#define AXE_178_RESET_MAGIC			0x40

#define AXE_178_MEDIA_GMII			0x0001
#define AXE_MEDIA_FULL_DUPLEX			0x0002
#define AXE_172_MEDIA_TX_ABORT_ALLOW		0x0004
/* AX88178 documentation says to always write 1 to reserved bit... */
#define AXE_178_MEDIA_MAGIC			0x0004
#define AXE_178_MEDIA_ENCK			0x0008
#define AXE_172_MEDIA_FLOW_CONTROL_EN		0x0010
#define AXE_178_MEDIA_RXFLOW_CONTROL_EN		0x0010
#define AXE_178_MEDIA_TXFLOW_CONTROL_EN		0x0020
#define AXE_178_MEDIA_JUMBO_EN			0x0040
#define AXE_178_MEDIA_LTPF_ONLY			0x0080
#define AXE_178_MEDIA_RX_EN			0x0100
#define AXE_178_MEDIA_100TX			0x0200
#define AXE_178_MEDIA_SBP			0x0800
#define AXE_178_MEDIA_SUPERMAC			0x1000

#define AXE_RXCMD_PROMISC			0x0001
#define AXE_RXCMD_ALLMULTI			0x0002
#define AXE_172_RXCMD_UNICAST			0x0004
#define AXE_178_RXCMD_KEEP_INVALID_CRC		0x0004
#define AXE_RXCMD_BROADCAST			0x0008
#define AXE_RXCMD_MULTICAST			0x0010
#define AXE_RXCMD_ENABLE			0x0080
#define AXE_178_RXCMD_MFB			0x0300

#define        AXE_PHY_SEL_PRI         1
#define        AXE_PHY_SEL_SEC         0
#define        AXE_PHY_TYPE_MASK       0xE0
#define        AXE_PHY_TYPE_SHIFT      5
#define        AXE_PHY_TYPE(x)         \
       (((x) & AXE_PHY_TYPE_MASK) >> AXE_PHY_TYPE_SHIFT)

#define        PHY_TYPE_100_HOME       0       /* 10/100 or 1M HOME PHY */
#define        PHY_TYPE_GIG            1       /* Gigabit PHY */
#define        PHY_TYPE_SPECIAL        4       /* Special case */
#define        PHY_TYPE_RSVD           5       /* Reserved */
#define        PHY_TYPE_NON_SUP        7       /* Non-supported PHY */

#define        AXE_PHY_NO_MASK         0x1F
#define        AXE_PHY_NO(x)           ((x) & AXE_PHY_NO_MASK)

#define        AXE_PHY_NO_AX772_EPHY   0x10    /* Embedded 10/100 PHY of AX88772 */

#define AXE_GPIO0_EN		0x01
#define AXE_GPIO0		0x02
#define AXE_GPIO1_EN		0x04
#define AXE_GPIO1		0x08
#define AXE_GPIO2_EN		0x10
#define AXE_GPIO2		0x20
#define AXE_GPIO_RELOAD_EEPROM	0x80

#define AXE_PHY_MODE_MARVELL		0x00
#define AXE_PHY_MODE_CICADA		0x01
#define AXE_PHY_MODE_AGERE		0x02
#define AXE_PHY_MODE_CICADA_V2		0x05
#define AXE_PHY_MODE_CICADA_V2_ASIX	0x09
#define AXE_PHY_MODE_REALTEK_8211CL	0x0c
#define AXE_PHY_MODE_REALTEK_8211BN	0x0d
#define AXE_PHY_MODE_REALTEK_8251CL	0x0e
#define AXE_PHY_MODE_ATTANSIC		0x40

#define AXE_772B_RXCMD_RH1M	0x0100
#define AXE_772B_RXCMD_RH2M	0x0200
#define AXE_772B_RXCMD_RH3M	0x0400

#define AXE_RH1M_RXLEN_MASK	0x07ff

#define AXE_TIMEOUT		1000
#define AXE_172_BUFSZ		1536
#define AXE_178_MIN_BUFSZ	2048
#define AXE_178_MAX_BUFSZ	16384

#define AXE_RX_LIST_CNT		1
#define AXE_TX_LIST_CNT		1

#define AXE_CTL_READ		0x01
#define AXE_CTL_WRITE		0x02

/* EEPROM Map */
#define	AXE_EEPROM_772B_NODEID		0x04

/*
 * The interrupt endpoint is currently unused
 * by the ASIX part.
 */
#define AXE_ENDPT_RX		0x0
#define AXE_ENDPT_TX		0x1
#define AXE_ENDPT_INTR		0x2
#define AXE_ENDPT_MAX		0x3

struct axe_type {
	struct usb_devno	axe_dev;
	u_int16_t		axe_flags;
#define AX178	0x0001		/* AX88178 */
#define AX772	0x0002		/* AX88772 */
#define AX772B	0x0004		/* AX88772B */
};

struct axe_softc;

struct axe_chain {
	struct axe_softc	*axe_sc;
	struct usbd_xfer	*axe_xfer;
	char			*axe_buf;
	struct mbuf		*axe_mbuf;
	int			axe_accum;
	int			axe_idx;
};

struct axe_cdata {
	struct axe_chain	axe_tx_chain[AXE_TX_LIST_CNT];
	struct axe_chain	axe_rx_chain[AXE_RX_LIST_CNT];
	int			axe_tx_prod;
	int			axe_tx_cons;
	int			axe_tx_cnt;
	int			axe_rx_prod;
};

struct axe_sframe_hdr {
	u_int16_t		len;
	u_int16_t		ilen;
} __packed;

struct axe_softc {
	struct device		axe_dev;
#define GET_MII(sc) (&(sc)->axe_mii)
	struct arpcom		arpcom;
#define GET_IFP(sc) (&(sc)->arpcom.ac_if)
	struct mii_data		axe_mii;
	struct usbd_device	*axe_udev;
	struct usbd_interface	*axe_iface;

	u_int16_t		axe_vendor;
	u_int16_t		axe_product;

	u_int16_t		axe_flags;

	int			axe_ed[AXE_ENDPT_MAX];
	struct usbd_pipe	*axe_ep[AXE_ENDPT_MAX];
	int			axe_unit;
	struct axe_cdata	axe_cdata;
	struct timeout		axe_stat_ch;

	int			axe_refcnt;

	struct usb_task		axe_tick_task;
	struct usb_task		axe_stop_task;

	struct rwlock		axe_mii_lock;

	int			axe_link;
	unsigned char		axe_ipgs[3];
	unsigned char 		axe_phyaddrs[2];
	int			axe_phyno;
	struct timeval		axe_rx_notice;
	u_int			axe_bufsz;
};
