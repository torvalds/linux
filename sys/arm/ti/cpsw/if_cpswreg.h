/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Damjan Marion <dmarion@Freebsd.org>
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

#ifndef	_IF_CPSWREG_H
#define	_IF_CPSWREG_H

#define	CPSW_SS_OFFSET			0x0000
#define	CPSW_SS_IDVER			(CPSW_SS_OFFSET + 0x00)
#define	CPSW_SS_SOFT_RESET		(CPSW_SS_OFFSET + 0x08)
#define	CPSW_SS_STAT_PORT_EN		(CPSW_SS_OFFSET + 0x0C)
#define	CPSW_SS_PTYPE			(CPSW_SS_OFFSET + 0x10)
#define	CPSW_SS_FLOW_CONTROL		(CPSW_SS_OFFSET + 0x24)

#define	CPSW_PORT_OFFSET		0x0100
#define	CPSW_PORT_P_MAX_BLKS(p)		(CPSW_PORT_OFFSET + 0x08 + ((p) * 0x100))
#define	CPSW_PORT_P_BLK_CNT(p)		(CPSW_PORT_OFFSET + 0x0C + ((p) * 0x100))
#define	CPSW_PORT_P_VLAN(p)		(CPSW_PORT_OFFSET + 0x14 + ((p) * 0x100))
#define	CPSW_PORT_P_TX_PRI_MAP(p)	(CPSW_PORT_OFFSET + 0x118 + ((p-1) * 0x100))
#define	CPSW_PORT_P0_CPDMA_TX_PRI_MAP	(CPSW_PORT_OFFSET + 0x01C)
#define	CPSW_PORT_P0_CPDMA_RX_CH_MAP	(CPSW_PORT_OFFSET + 0x020)
#define	CPSW_PORT_P_SA_LO(p)		(CPSW_PORT_OFFSET + 0x120 + ((p-1) * 0x100))
#define	CPSW_PORT_P_SA_HI(p)		(CPSW_PORT_OFFSET + 0x124 + ((p-1) * 0x100))

#define	CPSW_CPDMA_OFFSET		0x0800
#define	CPSW_CPDMA_TX_CONTROL		(CPSW_CPDMA_OFFSET + 0x04)
#define	CPSW_CPDMA_TX_TEARDOWN		(CPSW_CPDMA_OFFSET + 0x08)
#define	CPSW_CPDMA_RX_CONTROL		(CPSW_CPDMA_OFFSET + 0x14)
#define	CPSW_CPDMA_RX_TEARDOWN		(CPSW_CPDMA_OFFSET + 0x18)
#define	CPSW_CPDMA_SOFT_RESET		(CPSW_CPDMA_OFFSET + 0x1c)
#define	CPSW_CPDMA_DMACONTROL		(CPSW_CPDMA_OFFSET + 0x20)
#define	CPSW_CPDMA_DMASTATUS		(CPSW_CPDMA_OFFSET + 0x24)
#define	CPSW_CPDMA_RX_BUFFER_OFFSET	(CPSW_CPDMA_OFFSET + 0x28)
#define	CPSW_CPDMA_TX_INTSTAT_RAW	(CPSW_CPDMA_OFFSET + 0x80)
#define	CPSW_CPDMA_TX_INTSTAT_MASKED	(CPSW_CPDMA_OFFSET + 0x84)
#define	CPSW_CPDMA_TX_INTMASK_SET	(CPSW_CPDMA_OFFSET + 0x88)
#define	CPSW_CPDMA_TX_INTMASK_CLEAR	(CPSW_CPDMA_OFFSET + 0x8C)
#define	CPSW_CPDMA_CPDMA_EOI_VECTOR	(CPSW_CPDMA_OFFSET + 0x94)
#define	CPSW_CPDMA_RX_INTSTAT_RAW	(CPSW_CPDMA_OFFSET + 0xA0)
#define	CPSW_CPDMA_RX_INTSTAT_MASKED	(CPSW_CPDMA_OFFSET + 0xA4)
#define	CPSW_CPDMA_RX_INTMASK_SET	(CPSW_CPDMA_OFFSET + 0xA8)
#define	CPSW_CPDMA_RX_INTMASK_CLEAR	(CPSW_CPDMA_OFFSET + 0xAc)
#define	 CPSW_CPDMA_RX_INT_THRESH(_ch)	(1 << (8 + ((_ch) & 7)))
#define	 CPSW_CPDMA_RX_INT(_ch)		(1 << (0 + ((_ch) & 7)))
#define	CPSW_CPDMA_DMA_INTSTAT_RAW	(CPSW_CPDMA_OFFSET + 0xB0)
#define	CPSW_CPDMA_DMA_INTSTAT_MASKED	(CPSW_CPDMA_OFFSET + 0xB4)
#define	CPSW_CPDMA_DMA_INTMASK_SET	(CPSW_CPDMA_OFFSET + 0xB8)
#define	CPSW_CPDMA_DMA_INTMASK_CLEAR	(CPSW_CPDMA_OFFSET + 0xBC)
#define	CPSW_CPDMA_RX_PENDTHRESH(p)	(CPSW_CPDMA_OFFSET + 0x0c0 + ((p) * 0x04))
#define	CPSW_CPDMA_RX_FREEBUFFER(p)	(CPSW_CPDMA_OFFSET + 0x0e0 + ((p) * 0x04))

#define	CPSW_STATS_OFFSET		0x0900

#define	CPSW_STATERAM_OFFSET		0x0A00
#define	CPSW_CPDMA_TX_HDP(p)		(CPSW_STATERAM_OFFSET + 0x00 + ((p) * 0x04))
#define	CPSW_CPDMA_RX_HDP(p)		(CPSW_STATERAM_OFFSET + 0x20 + ((p) * 0x04))
#define	CPSW_CPDMA_TX_CP(p)		(CPSW_STATERAM_OFFSET + 0x40 + ((p) * 0x04))
#define	CPSW_CPDMA_RX_CP(p)		(CPSW_STATERAM_OFFSET + 0x60 + ((p) * 0x04))

#define	CPSW_CPTS_OFFSET		0x0C00

#define	CPSW_ALE_OFFSET			0x0D00
#define	CPSW_ALE_CONTROL		(CPSW_ALE_OFFSET + 0x08)
#define	 CPSW_ALE_CTL_ENABLE		(1U << 31)
#define	 CPSW_ALE_CTL_CLEAR_TBL		(1 << 30)
#define	 CPSW_ALE_CTL_BYPASS		(1 << 4)
#define	 CPSW_ALE_CTL_VLAN_AWARE	(1 << 2)
#define	CPSW_ALE_TBLCTL			(CPSW_ALE_OFFSET + 0x20)
#define	CPSW_ALE_TBLW2			(CPSW_ALE_OFFSET + 0x34)
#define	CPSW_ALE_TBLW1			(CPSW_ALE_OFFSET + 0x38)
#define	CPSW_ALE_TBLW0			(CPSW_ALE_OFFSET + 0x3C)
#define	 ALE_MCAST(_a)			((_a[1] >> 8) & 1)
#define	 ALE_MCAST_FWD			(3 << 30)
#define	 ALE_PORTS(_a)			((_a[2] >> 2) & 7)
#define	 ALE_TYPE(_a)			((_a[1] >> 28) & 3)
#define	 ALE_TYPE_ADDR			1
#define	 ALE_TYPE_VLAN			2
#define	 ALE_TYPE_VLAN_ADDR		3
#define	 ALE_VLAN(_a)			((_a[1] >> 16) & 0xfff)
#define	 ALE_VLAN_UNREGFLOOD(_a)	((_a[0] >> 8) & 7)
#define	 ALE_VLAN_REGFLOOD(_a)		((_a[0] >> 16) & 7)
#define	 ALE_VLAN_UNTAG(_a)		((_a[0] >> 24) & 7)
#define	 ALE_VLAN_MEMBERS(_a)		(_a[0] & 7)
#define	CPSW_ALE_PORTCTL(p)		(CPSW_ALE_OFFSET + 0x40 + ((p) * 0x04))
#define	 ALE_PORTCTL_NO_SA_UPDATE	(1 << 5)
#define	 ALE_PORTCTL_NO_LEARN		(1 << 4)
#define	 ALE_PORTCTL_INGRESS		(1 << 3)
#define	 ALE_PORTCTL_DROP_UNTAGGED	(1 << 2)
#define	 ALE_PORTCTL_FORWARD		3
#define	 ALE_PORTCTL_LEARN		2
#define	 ALE_PORTCTL_BLOCKED		1
#define	 ALE_PORTCTL_DISABLED		0

/* SL1 is at 0x0D80, SL2 is at 0x0DC0 */
#define	CPSW_SL_OFFSET			0x0D80
#define	CPSW_SL_MACCONTROL(p)		(CPSW_SL_OFFSET + (0x40 * (p)) + 0x04)
#define	 CPSW_SL_MACTL_IFCTL_B		(1 << 16)
#define	 CPSW_SL_MACTL_IFCTL_A		(1 << 15)
#define	 CPSW_SL_MACTL_GIG		(1 << 7)
#define	 CPSW_SL_MACTL_GMII_ENABLE	(1 << 5)
#define	 CPSW_SL_MACTL_FULLDUPLEX	(1 << 0)
#define	CPSW_SL_MACSTATUS(p)		(CPSW_SL_OFFSET + (0x40 * (p)) + 0x08)
#define	CPSW_SL_SOFT_RESET(p)		(CPSW_SL_OFFSET + (0x40 * (p)) + 0x0C)
#define	CPSW_SL_RX_MAXLEN(p)		(CPSW_SL_OFFSET + (0x40 * (p)) + 0x10)
#define	CPSW_SL_RX_PAUSE(p)		(CPSW_SL_OFFSET + (0x40 * (p)) + 0x18)
#define	CPSW_SL_TX_PAUSE(p)		(CPSW_SL_OFFSET + (0x40 * (p)) + 0x1C)
#define	CPSW_SL_RX_PRI_MAP(p)		(CPSW_SL_OFFSET + (0x40 * (p)) + 0x24)

#define	MDIO_OFFSET			0x1000
#define	MDIOCONTROL			(MDIO_OFFSET + 0x04)
#define	 MDIOCTL_ENABLE			(1 << 30)
#define	 MDIOCTL_FAULTENB		(1 << 18)
#define	MDIOLINKINTRAW			(MDIO_OFFSET + 0x10)
#define	MDIOLINKINTMASKED		(MDIO_OFFSET + 0x14)
#define	MDIOUSERACCESS0			(MDIO_OFFSET + 0x80)
#define	MDIOUSERPHYSEL0			(MDIO_OFFSET + 0x84)
#define	MDIOUSERACCESS1			(MDIO_OFFSET + 0x88)
#define	MDIOUSERPHYSEL1			(MDIO_OFFSET + 0x8C)
#define	 MDIO_PHYSEL_LINKINTENB		(1 << 6)
#define	 MDIO_PHYACCESS_GO		(1U << 31)
#define	 MDIO_PHYACCESS_WRITE		(1 << 30)
#define	 MDIO_PHYACCESS_ACK		(1 << 29)

#define	CPSW_WR_OFFSET			0x1200
#define	CPSW_WR_SOFT_RESET		(CPSW_WR_OFFSET + 0x04)
#define	CPSW_WR_CONTROL			(CPSW_WR_OFFSET + 0x08)
#define	CPSW_WR_INT_CONTROL		(CPSW_WR_OFFSET + 0x0c)
#define	 CPSW_WR_INT_C0_RX_PULSE	(1 << 16)
#define	 CPSW_WR_INT_C0_TX_PULSE	(1 << 17)
#define	 CPSW_WR_INT_C1_RX_PULSE	(1 << 18)
#define	 CPSW_WR_INT_C1_TX_PULSE	(1 << 19)
#define	 CPSW_WR_INT_C2_RX_PULSE	(1 << 20)
#define	 CPSW_WR_INT_C2_TX_PULSE	(1 << 21)
#define	 CPSW_WR_INT_PACE_EN						\
	(CPSW_WR_INT_C0_RX_PULSE | CPSW_WR_INT_C0_TX_PULSE |		\
	 CPSW_WR_INT_C1_RX_PULSE | CPSW_WR_INT_C1_TX_PULSE |		\
	 CPSW_WR_INT_C2_RX_PULSE | CPSW_WR_INT_C2_TX_PULSE)
#define	 CPSW_WR_INT_PRESCALE_MASK	0xfff
#define	CPSW_WR_C_RX_THRESH_EN(p)	(CPSW_WR_OFFSET + (0x10 * (p)) + 0x10)
#define	CPSW_WR_C_RX_EN(p)		(CPSW_WR_OFFSET + (0x10 * (p)) + 0x14)
#define	CPSW_WR_C_TX_EN(p)		(CPSW_WR_OFFSET + (0x10 * (p)) + 0x18)
#define	CPSW_WR_C_MISC_EN(p)		(CPSW_WR_OFFSET + (0x10 * (p)) + 0x1C)
#define	CPSW_WR_C_RX_THRESH_STAT(p)	(CPSW_WR_OFFSET + (0x10 * (p)) + 0x40)
#define	CPSW_WR_C_RX_STAT(p)		(CPSW_WR_OFFSET + (0x10 * (p)) + 0x44)
#define	CPSW_WR_C_TX_STAT(p)		(CPSW_WR_OFFSET + (0x10 * (p)) + 0x48)
#define	CPSW_WR_C_MISC_STAT(p)		(CPSW_WR_OFFSET + (0x10 * (p)) + 0x4C)
#define	 CPSW_WR_C_MISC_EVNT_PEND	(1 << 4)
#define	 CPSW_WR_C_MISC_STAT_PEND	(1 << 3)
#define	 CPSW_WR_C_MISC_HOST_PEND	(1 << 2)
#define	 CPSW_WR_C_MISC_MDIOLINK	(1 << 1)
#define	 CPSW_WR_C_MISC_MDIOUSER	(1 << 0)
#define	CPSW_WR_C_RX_IMAX(p)		(CPSW_WR_OFFSET + (0x08 * (p)) + 0x70)
#define	CPSW_WR_C_TX_IMAX(p)		(CPSW_WR_OFFSET + (0x08 * (p)) + 0x74)
#define	 CPSW_WR_C_IMAX_MASK		0x3f
#define	 CPSW_WR_C_IMAX_MAX		63
#define	 CPSW_WR_C_IMAX_MIN		2
#define	 CPSW_WR_C_IMAX_US_MAX		500
#define	 CPSW_WR_C_IMAX_US_MIN		16

#define	CPSW_CPPI_RAM_OFFSET		0x2000
#define	CPSW_CPPI_RAM_SIZE		0x2000

#define	CPSW_MEMWINDOW_SIZE		0x4000

#define	 CPDMA_BD_SOP			(1 << 15)
#define	 CPDMA_BD_EOP			(1 << 14)
#define	 CPDMA_BD_OWNER			(1 << 13)
#define	 CPDMA_BD_EOQ			(1 << 12)
#define	 CPDMA_BD_TDOWNCMPLT		(1 << 11)
#define	 CPDMA_BD_PASS_CRC		(1 << 10)
#define	 CPDMA_BD_PKT_ERR_MASK		(3 << 4)
#define	 CPDMA_BD_TO_PORT		(1 << 4)
#define	 CPDMA_BD_PORT_MASK		3

struct cpsw_cpdma_bd {
	volatile uint32_t next;
	volatile uint32_t bufptr;
	volatile uint16_t buflen;
	volatile uint16_t bufoff;
	volatile uint16_t pktlen;
	volatile uint16_t flags;
};

#endif /*_IF_CPSWREG_H */
