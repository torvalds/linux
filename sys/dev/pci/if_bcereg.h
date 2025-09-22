/* $OpenBSD: if_bcereg.h,v 1.3 2006/11/08 01:32:00 brad Exp $ */
/* $NetBSD: if_bcereg.h,v 1.3 2003/09/29 01:53:02 mrg Exp $	 */

/*
 * Copyright (c) 2003 Clifford Wright. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Broadcom BCM440x */

/* PCI registers defined in the PCI 2.2 spec. */
#define BCE_PCI_BAR0			0x10

/* Sonics SB register access */
#define BCE_REG_WIN			0x80
#define BCE_SONICS_WIN			0x18002000

/* Sonics PCI control */
#define BCE_SPCI_TR2			0x0108	/* Sonics to PCI translation
						 * 2 */
/* bit defines */
#define SBTOPCI_PREF			0x4	/* prefetch enable */
#define SBTOPCI_BURST			0x8	/* burst enable */
#define BCE_SBINTVEC			0x0f94
/* interrupt bits */
#define SBIV_ENET0			0x02	/* enable for enet 0 */
#define SBIV_ENET1			0x40	/* enable for enet 1 */


/* Host Interface Registers */

#define BCE_DEVCTL			0x0000	/* device control */
/* device control bits */
#define BCE_DC_IP			0x00000400	/* internal phy present */
#define BCE_DC_ER			0x00008000	/* ephy reset */
/* Interrupt Control */
#define BCE_INT_STS			0x0020
#define BCE_INT_MASK			0x0024
/* bits for both status, and mask */
#define I_TO				0x00000080	/* general timeout */
#define I_PC				0x00000400	/* descriptor error */
#define I_PD				0x00000800	/* data error */
#define I_DE				0x00001000	/* desc. protocol error */
#define I_RU				0x00002000	/* rx desc. underflow */
#define I_RO				0x00004000	/* rx fifo overflow */
#define I_XU				0x00008000	/* tx fifo underflow */
#define I_RI				0x00010000	/* receive interrupt */
#define I_XI				0x01000000	/* transmit interrupt */

/* Ethernet MAC Control */
#define BCE_MACCTL			0x00A8	/* ethernet mac control */
/* mac control bits */
#define BCE_EMC_CRC32_ENAB		0x00000001	/* crc32 generation */
#define BCE_EMC_PDOWN			0x00000004	/* PHY powerdown */
#define BCE_EMC_EDET			0x00000008	/* PHY energy detect */
#define BCE_EMC_LED			0x000000e0	/* PHY LED control */

/* DMA Interrupt control */
#define BCE_DMAI_CTL			0x0100

/* DMA registers */
#define BCE_DMA_TXCTL			0x0200	/* transmit control */
/* transmit control bits */
#define XC_XE				0x1	/* transmit enable */
#define XC_LE				0x4	/* loopback enable */
#define BCE_DMA_TXADDR			0x0204	/* tx ring base address */
#define BCE_DMA_DPTR			0x0208	/* last tx descriptor */
#define BCE_DMA_TXSTATUS		0x020C	/* active desc, etc */
#define BCE_DMA_RXCTL			0x0210	/* enable, etc */
#define BCE_DMA_RXADDR			0x0214	/* rx ring base address */
#define BCE_DMA_RXDPTR			0x0218	/* last descriptor */
#define BCE_DMA_RXSTATUS		0x021C	/* active desc, etc */
/* receive status bits */
#define RS_CD_MASK			0x0fff	/* current descriptor pointer */
#define RS_DMA_IDLE			0x2000	/* DMA is idle */
#define RS_ERROR			0xf0000	/* had an error */

/* Ethernet MAC control registers */
#define BCE_RX_CTL			0x0400	/* receive config */
/* config bits */
#define ERC_DB				0x00000001	/* disable broadcast */
#define ERC_AM				0x00000002	/* rx all multicast */
#define ERC_PE				0x00000008	/* promiscuous enable */

#define BCE_RX_MAX			0x0404	/* max packet length */
#define BCE_TX_MAX			0x0408
#define BCE_MI_CTL			0x0410
#define BCE_MI_COMM			0x0414
#define BCE_MI_STS			0x041C
/* mii status bits */
#define BCE_MIINTR			0x00000001	/* mii mdio interrupt */

#define BCE_FILT_LOW			0x0420	/* mac low 4 bytes */
#define BCE_FILT_HI			0x0424	/* mac hi 2 bytes */
#define BCE_FILT_CTL			0x0428	/* packet filter ctrl */
#define BCE_ENET_CTL			0x042C
/* bits for mac control */
#define EC_EE				0x00000001	/* emac enable */
#define EC_ED				0x00000002	/* disable emac */
#define EC_ES				0x00000004	/* soft reset emac */
#define EC_EP				0x00000008	/* external phy */
#define BCE_TX_CTL			0x0430
/* bits for transmit control */
#define EXC_FD				0x00000001	/* full duplex */
#define BCE_TX_WATER			0x0434	/* tx watermark */

/* statistics counters */
#define BCE_RX_PKTS			0x058C

/* SiliconBackplane registers */
#define BCE_SBIMSTATE			0x0f90
#define BCE_SBTMSTATELOW		0x0f98
#define BCE_SBTMSTATEHI			0x0f9C
#define SBTML_RESET			0x1	/* reset */
#define SBTML_REJ			0x2	/* reject */
#define SBTML_CLK			0x10000	/* clock enable */
#define SBTML_FGC			0x20000	/* force gated clocks on */

/* MI communication register */
#define BCE_MICOMM_DATA			0x0000FFFF

#define BCE_MIREG(x)			((x & 0x1F) << 18)
#define BCE_MIPHY(x)			((x & 0x1F) << 23)

/* SPROM constants */
#define BCE_SPROM_BASE			0x1000
#define BCE_PHY				(BCE_SPROM_BASE + 0x5A) /* MII Address */
#define	BCE_ENET0			(BCE_SPROM_BASE + 0x4F)
#define	BCE_ENET1			(BCE_SPROM_BASE + 0x4E)
#define	BCE_ENET2			(BCE_SPROM_BASE + 0x51)
#define	BCE_ENET3			(BCE_SPROM_BASE + 0x50)
#define	BCE_ENET4			(BCE_SPROM_BASE + 0x53)
#define	BCE_ENET5			(BCE_SPROM_BASE + 0x52)

#define  SBIM_INBAND_ERROR		0x20000
#define  SBIM_TIMEOUT			0x40000
#define  SBIM_ERRORBITS			(SBIM_INBAND_ERROR|SBIM_TIMEOUT)
