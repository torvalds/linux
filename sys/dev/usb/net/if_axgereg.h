/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013-2014 Kevin Lo
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
 * $FreeBSD$
 */

#define	AXGE_ACCESS_MAC			0x01
#define	AXGE_ACCESS_PHY			0x02
#define	AXGE_ACCESS_WAKEUP		0x03
#define	AXGE_ACCESS_EEPROM		0x04
#define	AXGE_ACCESS_EFUSE		0x05
#define	AXGE_RELOAD_EEPROM_EFUSE	0x06
#define	AXGE_WRITE_EFUSE_EN		0x09
#define	AXGE_WRITE_EFUSE_DIS		0x0A
#define	AXGE_ACCESS_MFAB		0x10

/* Physical link status register */
#define	AXGE_PLSR			0x02
#define	PLSR_USB_FS			0x01
#define	PLSR_USB_HS			0x02
#define	PLSR_USB_SS			0x04

/* EEPROM address register */
#define	AXGE_EAR			0x07

/* EEPROM data low register */
#define	AXGE_EDLR			0x08

/* EEPROM data high register */
#define	AXGE_EDHR			0x09

/* EEPROM command register */
#define	AXGE_ECR			0x0a

/* Rx control register */
#define	AXGE_RCR			0x0b
#define	RCR_STOP			0x0000
#define	RCR_PROMISC			0x0001
#define	RCR_ACPT_ALL_MCAST		0x0002
#define	RCR_AUTOPAD_BNDRY		0x0004
#define	RCR_ACPT_BCAST			0x0008
#define	RCR_ACPT_MCAST			0x0010
#define	RCR_ACPT_PHY_MCAST		0x0020
#define	RCR_START			0x0080
#define	RCR_DROP_CRCERR			0x0100
#define	RCR_IPE				0x0200
#define	RCR_TX_CRC_PAD			0x0400

/* Node id register */
#define	AXGE_NIDR			0x10

/* Multicast filter array */
#define	AXGE_MFA			0x16

/* Medium status register */
#define	AXGE_MSR			0x22
#define	MSR_GM				0x0001
#define	MSR_FD				0x0002
#define	MSR_EN_125MHZ			0x0008
#define	MSR_RFC				0x0010
#define	MSR_TFC				0x0020
#define	MSR_RE				0x0100
#define	MSR_PS				0x0200

/* Monitor mode status register */
#define	AXGE_MMSR			0x24
#define	MMSR_RWLC			0x02
#define	MMSR_RWMP			0x04
#define	MMSR_RWWF			0x08
#define	MMSR_RW_FLAG			0x10
#define	MMSR_PME_POL			0x20
#define	MMSR_PME_TYPE			0x40
#define	MMSR_PME_IND			0x80

/* GPIO control/status register */
#define	AXGE_GPIOCR			0x25

/* Ethernet PHY power & reset control register */
#define	AXGE_EPPRCR			0x26
#define	EPPRCR_BZ			0x0010
#define	EPPRCR_IPRL			0x0020
#define	EPPRCR_AUTODETACH		0x1000

#define	AXGE_RX_BULKIN_QCTRL		0x2e

#define	AXGE_CLK_SELECT			0x33
#define	AXGE_CLK_SELECT_BCS		0x01
#define	AXGE_CLK_SELECT_ACS		0x02
#define	AXGE_CLK_SELECT_ACSREQ		0x10
#define	AXGE_CLK_SELECT_ULR		0x08

/* COE Rx control register */
#define	AXGE_CRCR			0x34
#define	CRCR_IP				0x01
#define	CRCR_TCP			0x02
#define	CRCR_UDP			0x04
#define	CRCR_ICMP			0x08
#define	CRCR_IGMP			0x10
#define	CRCR_TCPV6			0x20
#define	CRCR_UDPV6			0x40
#define	CRCR_ICMPV6			0x80

/* COE Tx control register */
#define	AXGE_CTCR			0x35
#define	CTCR_IP				0x01
#define	CTCR_TCP			0x02
#define	CTCR_UDP			0x04
#define	CTCR_ICMP			0x08
#define	CTCR_IGMP			0x10
#define	CTCR_TCPV6			0x20
#define	CTCR_UDPV6			0x40
#define	CTCR_ICMPV6			0x80

/* Pause water level high register */
#define	AXGE_PWLHR			0x54

/* Pause water level low register */
#define	AXGE_PWLLR			0x55

#define	AXGE_CONFIG_IDX			0	/* config number 1 */
#define	AXGE_IFACE_IDX			0

#define	GET_MII(sc)		uether_getmii(&(sc)->sc_ue)

/* The interrupt endpoint is currently unused by the ASIX part. */
enum {
	AXGE_BULK_DT_WR,
	AXGE_BULK_DT_RD,
	AXGE_N_TRANSFER,
};

#define	AXGE_N_FRAMES	16

struct axge_frame_txhdr {
	uint32_t		len;
#define	AXGE_TXLEN_MASK		0x0001FFFF
#define	AXGE_VLAN_INSERT	0x20000000
#define	AXGE_CSUM_DISABLE	0x80000000
	uint32_t		mss;
#define	AXGE_MSS_MASK		0x00003FFF
#define	AXGE_PADDING		0x80008000
#define	AXGE_VLAN_TAG_MASK	0xFFFF0000
} __packed;

#define	AXGE_TXBYTES(x)		((x) & AXGE_TXLEN_MASK)

#define	AXGE_PHY_ADDR		3

struct axge_frame_rxhdr {
	uint32_t		status;
#define	AXGE_RX_L4_CSUM_ERR	0x00000001
#define	AXGE_RX_L3_CSUM_ERR	0x00000002
#define	AXGE_RX_L4_TYPE_UDP	0x00000004	
#define	AXGE_RX_L4_TYPE_ICMP	0x00000008	
#define	AXGE_RX_L4_TYPE_IGMP	0x0000000C	
#define	AXGE_RX_L4_TYPE_TCP	0x00000010
#define	AXGE_RX_L4_TYPE_MASK	0x0000001C
#define	AXGE_RX_L3_TYPE_IPV4	0x00000020
#define	AXGE_RX_L3_TYPE_IPV6	0x00000040
#define	AXGE_RX_L3_TYPE_MASK	0x00000060
#define	AXGE_RX_VLAN_IND_MASK	0x00000700
#define	AXGE_RX_GOOD_PKT	0x00000800
#define	AXGE_RX_VLAN_PRI_MASK	0x00007000
#define	AXGE_RX_MBCAST		0x00008000		
#define	AXGE_RX_LEN_MASK	0x1FFF0000
#define	AXGE_RX_CRC_ERR		0x20000000
#define	AXGE_RX_MII_ERR		0x40000000
#define	AXGE_RX_DROP_PKT	0x80000000
#define	AXGE_RX_LEN_SHIFT	16
} __packed;

#define	AXGE_RXBYTES(x)		(((x) & AXGE_RX_LEN_MASK) >> AXGE_RX_LEN_SHIFT)
#define	AXGE_RX_ERR(x)		\
	    ((x) & (AXGE_RX_CRC_ERR | AXGE_RX_MII_ERR | AXGE_RX_DROP_PKT))

struct axge_softc {
	struct usb_ether	sc_ue;
	struct mtx		sc_mtx;
	struct usb_xfer		*sc_xfer[AXGE_N_TRANSFER];

	int			sc_flags;
#define	AXGE_FLAG_LINK		0x0001	/* got a link */
};

#define	AXGE_LOCK(_sc)			mtx_lock(&(_sc)->sc_mtx)
#define	AXGE_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define	AXGE_LOCK_ASSERT(_sc, t)	mtx_assert(&(_sc)->sc_mtx, t)
