/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2008 Stanislav Sedov <stas@FreeBSD.org>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Master configuration register
 */
#define	AE_MASTER_REG		0x1400

#define	AE_MASTER_SOFT_RESET	0x1	/* Reset adapter. */
#define	AE_MASTER_MTIMER_EN	0x2	/* Unknown. */
#define	AE_MASTER_IMT_EN	0x4	/* Interrupt moderation timer enable. */
#define	AE_MASTER_MANUAL_INT	0x8	/* Software manual interrupt. */
#define	AE_MASTER_REVNUM_SHIFT	16	/* Chip revision number. */
#define	AE_MASTER_REVNUM_MASK	0xff
#define	AE_MASTER_DEVID_SHIFT	24	/* PCI device id. */
#define	AE_MASTER_DEVID_MASK	0xff

/*
 * Interrupt status register
 */
#define	AE_ISR_REG		0x1600
#define	AE_ISR_TIMER		0x00000001	/* Counter expired. */
#define	AE_ISR_MANUAL		0x00000002	/* Manual interrupt occuried. */
#define	AE_ISR_RXF_OVERFLOW	0x00000004	/* RxF overflow occuried. */
#define	AE_ISR_TXF_UNDERRUN	0x00000008	/* TxF underrun occuried. */
#define	AE_ISR_TXS_OVERFLOW	0x00000010	/* TxS overflow occuried. */
#define	AE_ISR_RXS_OVERFLOW	0x00000020	/* Internal RxS ring overflow. */
#define	AE_ISR_LINK_CHG		0x00000040	/* Link state changed. */
#define	AE_ISR_TXD_UNDERRUN	0x00000080	/* TxD underrun occuried. */
#define	AE_ISR_RXD_OVERFLOW	0x00000100	/* RxD overflow occuried. */
#define	AE_ISR_DMAR_TIMEOUT	0x00000200	/* DMA read timeout. */
#define	AE_ISR_DMAW_TIMEOUT	0x00000400	/* DMA write timeout. */
#define	AE_ISR_PHY		0x00000800	/* PHY interrupt. */
#define	AE_ISR_TXS_UPDATED	0x00010000	/* Tx status updated. */
#define	AE_ISR_RXD_UPDATED	0x00020000	/* Rx status updated. */
#define	AE_ISR_TX_EARLY		0x00040000	/* TxMAC started transmit. */
#define	AE_ISR_FIFO_UNDERRUN	0x01000000	/* FIFO underrun. */
#define	AE_ISR_FRAME_ERROR	0x02000000	/* Frame receive error. */
#define	AE_ISR_FRAME_SUCCESS	0x04000000	/* Frame receive success. */
#define	AE_ISR_CRC_ERROR	0x08000000	/* CRC error occuried. */
#define	AE_ISR_PHY_LINKDOWN	0x10000000	/* PHY link down. */
#define	AE_ISR_DISABLE		0x80000000	/* Disable interrupts. */

#define	AE_ISR_TX_EVENT		(AE_ISR_TXF_UNDERRUN | AE_ISR_TXS_OVERFLOW | \
				 AE_ISR_TXD_UNDERRUN | AE_ISR_TXS_UPDATED | \
				 AE_ISR_TX_EARLY)
#define	AE_ISR_RX_EVENT		(AE_ISR_RXF_OVERFLOW | AE_ISR_RXS_OVERFLOW | \
				 AE_ISR_RXD_OVERFLOW | AE_ISR_RXD_UPDATED)

/* Interrupt mask register. */
#define	AE_IMR_REG		0x1604

#define	AE_IMR_DEFAULT		(AE_ISR_DMAR_TIMEOUT | AE_ISR_DMAW_TIMEOUT | \
				 AE_ISR_PHY_LINKDOWN | \
				 AE_ISR_TXS_UPDATED | AE_ISR_RXD_UPDATED )

/*
 * Ethernet address register.
 */
#define	AE_EADDR0_REG		0x1488	/* 5 - 2 bytes */
#define	AE_EADDR1_REG		0x148c	/* 1 - 0 bytes */

/*
 * Desriptor rings registers.
 * L2 supports 64-bit addressing but all rings base addresses
 * should have the same high 32 bits of address.
 */
#define	AE_DESC_ADDR_HI_REG	0x1540	/* High 32 bits of ring base address. */
#define	AE_RXD_ADDR_LO_REG	0x1554	/* Low 32 bits of RxD ring address. */
#define	AE_TXD_ADDR_LO_REG	0x1544	/* Low 32 bits of TxD ring address. */
#define	AE_TXS_ADDR_LO_REG	0x154c	/* Low 32 bits of TxS ring address. */
#define	AE_RXD_COUNT_REG	0x1558	/* Number of RxD descriptors in ring.
					   Should be 120-byte aligned (i.e.
					   the 'data' field of RxD should
					   have 128-byte alignment). */
#define	AE_TXD_BUFSIZE_REG	0x1548	/* Size of TxD ring in 4-byte units.
					   Should be 4-byte aligned. */
#define	AE_TXS_COUNT_REG	0x1550	/* Number of TxS descriptors in ring.
					   4 byte alignment. */
#define	AE_RXD_COUNT_MIN	16
#define	AE_RXD_COUNT_MAX	512
#define	AE_RXD_COUNT_DEFAULT	64
/* Padding to align frames on a 128-byte boundary. */
#define	AE_RXD_PADDING		120

#define	AE_TXD_BUFSIZE_MIN	4096
#define	AE_TXD_BUFSIZE_MAX	65536
#define	AE_TXD_BUFSIZE_DEFAULT	8192

#define	AE_TXS_COUNT_MIN	8	/* Not sure. */
#define	AE_TXS_COUNT_MAX	160
#define	AE_TXS_COUNT_DEFAULT	64	/* AE_TXD_BUFSIZE_DEFAULT / 128 */

/*
 * Inter-frame gap configuration register.
 */
#define	AE_IFG_REG		0x1484

#define	AE_IFG_TXIPG_DEFAULT	0x60	/* 96-bit IFG time. */
#define	AE_IFG_TXIPG_SHIFT	0
#define	AE_IFG_TXIPG_MASK	0x7f

#define	AE_IFG_RXIPG_DEFAULT	0x50	/* 80-bit IFG time. */
#define	AE_IFG_RXIPG_SHIFT	8
#define	AE_IFG_RXIPG_MASK	0xff00

#define	AE_IFG_IPGR1_DEFAULT	0x40	/* Carrier-sense window. */
#define	AE_IFG_IPGR1_SHIFT	16
#define	AE_IFG_IPGR1_MASK	0x7f0000

#define	AE_IFG_IPGR2_DEFAULT	0x60	/* IFG window. */
#define	AE_IFG_IPGR2_SHIFT	24
#define	AE_IFG_IPGR2_MASK	0x7f000000

/*
 * Half-duplex mode configuration register.
 */
#define	AE_HDPX_REG		0x1498

/* Collision window. */
#define	AE_HDPX_LCOL_SHIFT	0
#define	AE_HDPX_LCOL_MASK	0x000003ff
#define	AE_HDPX_LCOL_DEFAULT	0x37

/* Max retransmission time, after that the packet will be discarded. */
#define	AE_HDPX_RETRY_SHIFT	12
#define	AE_HDPX_RETRY_MASK	0x0000f000
#define	AE_HDPX_RETRY_DEFAULT	0x0f

/* Alternative binary exponential back-off time. */
#define	AE_HDPX_ABEBT_SHIFT	20
#define	AE_HDPX_ABEBT_MASK	0x00f00000
#define	AE_HDPX_ABEBT_DEFAULT	0x0a

/* IFG to start JAM for collision based flow control (8-bit time units).*/
#define	AE_HDPX_JAMIPG_SHIFT	24
#define	AE_HDPX_JAMIPG_MASK	0x0f000000
#define	AE_HDPX_JAMIPG_DEFAULT	0x07

/* Allow the transmission of a packet which has been excessively deferred. */
#define	AE_HDPX_EXC_EN		0x00010000
/* No back-off on collision, immediately start the retransmission. */
#define	AE_HDPX_NO_BACK_C	0x00020000
/* No back-off on backpressure, immediately start the transmission. */
#define	AE_HDPX_NO_BACK_P	0x00040000
/* Alternative binary exponential back-off enable. */
#define	AE_HDPX_ABEBE		0x00080000

/*
 * Interrupt moderation timer configuration register.
 */
#define	AE_IMT_REG		0x1408	/* Timer value in 2 us units. */
#define	AE_IMT_MAX		65000
#define	AE_IMT_MIN		50
#define	AE_IMT_DEFAULT		100	/* 200 microseconds. */

/*
 * Interrupt clearing timer configuration register.
 */
#define	AE_ICT_REG		0x140e	/* Maximum time allowed to clear
					   interrupt. In 2 us units.  */
#define	AE_ICT_DEFAULT		50000	/* 100ms */

/*
 * MTU configuration register.
 */
#define	AE_MTU_REG		0x149c	/* MTU size in bytes. */

/*
 * Cut-through configuration register.
 */
#define	AE_CUT_THRESH_REG	0x1590	/* Cut-through threshold in unknown units. */
#define	AE_CUT_THRESH_DEFAULT   0x177

/*
 * Flow-control configuration registers.
 */
#define	AE_FLOW_THRESH_HI_REG	0x15a8	/* High watermark of RxD
					   overflow threshold. */
#define	AE_FLOW_THRESH_LO_REG	0x15aa	/* Lower watermark of RxD
					   overflow threshold */

/*
 * Mailbox configuration registers.
*/
#define	AE_MB_TXD_IDX_REG	0x15f0	/* TxD read index. */
#define	AE_MB_RXD_IDX_REG	0x15f4	/* RxD write index. */

/*
 * DMA configuration registers.
 */
#define	AE_DMAREAD_REG		0x1580	/* Read DMA configuration register. */
#define	AE_DMAREAD_EN		1
#define	AE_DMAWRITE_REG		0x15a0	/* Write DMA configuration register. */
#define	AE_DMAWRITE_EN		1

/*
 * MAC configuration register.
 */
#define	AE_MAC_REG		0x1480

#define	AE_MAC_TX_EN		0x00000001	/* Enable transmit. */
#define	AE_MAC_RX_EN		0x00000002	/* Enable receive. */
#define	AE_MAC_TX_FLOW_EN	0x00000004	/* Enable Tx flow control. */
#define	AE_MAC_RX_FLOW_EN	0x00000008	/* Enable Rx flow control. */
#define	AE_MAC_LOOPBACK		0x00000010	/* Loopback at MII. */
#define	AE_MAC_FULL_DUPLEX	0x00000020	/* Enable full-duplex. */
#define	AE_MAC_TX_CRC_EN	0x00000040	/* Enable CRC generation. */
#define	AE_MAC_TX_AUTOPAD	0x00000080	/* Pad short frames. */
#define	AE_MAC_PREAMBLE_MASK	0x00003c00	/* Preamble length. */
#define	AE_MAC_PREAMBLE_SHIFT	10
#define	AE_MAC_PREAMBLE_DEFAULT	0x07		/* By standard. */
#define	AE_MAC_RMVLAN_EN	0x00004000	/* Remove VLAN tags in
						   incoming packets. */
#define	AE_MAC_PROMISC_EN	0x00008000	/* Enable promiscue mode. */
#define	AE_MAC_TX_MAXBACKOFF	0x00100000	/* Unknown. */
#define	AE_MAC_MCAST_EN		0x02000000	/* Pass all multicast frames. */
#define	AE_MAC_BCAST_EN		0x04000000	/* Pass all broadcast frames. */
#define	AE_MAC_CLK_PHY		0x08000000	/* If 1 uses loopback clock
						   PHY, if 0 - system clock. */
#define	AE_HALFBUF_MASK		0xf0000000	/* Half-duplex retry buffer. */
#define	AE_HALFBUF_SHIFT	28
#define	AE_HALFBUF_DEFAULT	2		/* XXX: From Linux. */

/*
 * MDIO control register.
 */
#define	AE_MDIO_REG		0x1414                                                                                                                   
#define	AE_MDIO_DATA_MASK	0xffff
#define	AE_MDIO_DATA_SHIFT	0
#define	AE_MDIO_REGADDR_MASK	0x1f0000
#define	AE_MDIO_REGADDR_SHIFT	16
#define	AE_MDIO_READ		0x00200000	/* Read operation. */
#define	AE_MDIO_SUP_PREAMBLE	0x00400000	/* Suppress preamble. */
#define	AE_MDIO_START		0x00800000	/* Initiate MDIO transfer. */
#define	AE_MDIO_CLK_SHIFT	24		/* Clock selection. */
#define	AE_MDIO_CLK_MASK	0x07000000	/* Clock selection. */
#define	AE_MDIO_CLK_25_4	0		/* Dividers? */
#define	AE_MDIO_CLK_25_6	2
#define	AE_MDIO_CLK_25_8	3
#define	AE_MDIO_CLK_25_10	4
#define	AE_MDIO_CLK_25_14	5
#define	AE_MDIO_CLK_25_20	6
#define	AE_MDIO_CLK_25_28	7
#define	AE_MDIO_BUSY		0x08000000	/* MDIO is busy. */

/*
 * Idle status register.
 */
#define	AE_IDLE_REG		0x1410

/*
 * Idle status bits.
 * If bit is set then the corresponding module is in non-idle state.
 */
#define	AE_IDLE_RXMAC		1
#define	AE_IDLE_TXMAC		2
#define	AE_IDLE_DMAREAD		8
#define	AE_IDLE_DMAWRITE	4

/*
 * Multicast hash tables registers.
 */
#define	AE_REG_MHT0		0x1490
#define	AE_REG_MHT1		0x1494

/*
 * Wake on lan (WOL).
 */
#define	AE_WOL_REG		0x14a0
#define	AE_WOL_MAGIC		0x00000004
#define	AE_WOL_MAGIC_PME	0x00000008
#define	AE_WOL_LNKCHG		0x00000010
#define	AE_WOL_LNKCHG_PME	0x00000020

/*
 * PCIE configuration registers. Descriptions unknown.
 */
#define	AE_PCIE_LTSSM_TESTMODE_REG	0x12fc
#define	AE_PCIE_LTSSM_TESTMODE_DEFAULT	0x6500
#define	AE_PCIE_DLL_TX_CTRL_REG		0x1104
#define	AE_PCIE_DLL_TX_CTRL_SEL_NOR_CLK	0x0400
#define	AE_PCIE_DLL_TX_CTRL_DEFAULT	0x0568
#define	AE_PCIE_PHYMISC_REG		0x1000
#define	AE_PCIE_PHYMISC_FORCE_RCV_DET	0x4

/*
 * PHY enable register.
 */
#define	AE_PHY_ENABLE_REG	0x140c
#define	AE_PHY_ENABLE		1

/*
 * VPD registers.
 */
#define	AE_VPD_CAP_REG		0x6c	/* Command register. */
#define	AE_VPD_CAP_ID_MASK	0xff
#define	AE_VPD_CAP_ID_SHIFT	0
#define	AE_VPD_CAP_NEXT_MASK	0xff00
#define	AE_VPD_CAP_NEXT_SHIFT	8
#define	AE_VPD_CAP_ADDR_MASK	0x7fff0000
#define	AE_VPD_CAP_ADDR_SHIFT	16
#define	AE_VPD_CAP_DONE		0x80000000
                                                                                                   
#define	AE_VPD_DATA_REG		0x70	/* Data register. */

#define	AE_VPD_NREGS		64	/* Maximum number of VPD regs. */
#define	AE_VPD_SIG_MASK		0xff
#define	AE_VPD_SIG		0x5a	/* VPD block signature. */
#define	AE_VPD_REG_SHIFT	16	/* Register id offset. */

/*
 * SPI registers.
 */
#define	AE_SPICTL_REG		0x200
#define	AE_SPICTL_VPD_EN	0x2000	/* Enable VPD. */

/*
 * PHY-specific registers constants.
 */
#define	AE_PHY_DBG_ADDR		0x1d
#define	AE_PHY_DBG_DATA		0x1e
#define	AE_PHY_DBG_POWERSAVE	0x1000

/*
 * TxD flags.
 */
#define	AE_TXD_INSERT_VTAG	0x8000	/* Insert VLAN tag on transfer. */

/*
 * TxS flags.
 */
#define	AE_TXS_SUCCESS		0x0001	/* Packed transmitted successfully. */
#define	AE_TXS_BCAST		0x0002	/* Transmitted broadcast frame. */
#define	AE_TXS_MCAST		0x0004	/* Transmitted multicast frame. */
#define	AE_TXS_PAUSE		0x0008	/* Transmitted pause frame. */
#define	AE_TXS_CTRL		0x0010	/* Transmitted control frame. */
#define	AE_TXS_DEFER		0x0020	/* Frame transmitted with defer. */
#define	AE_TXS_EXCDEFER		0x0040	/* Excessive collision. */
#define	AE_TXS_SINGLECOL	0x0080	/* Single collision occuried. */
#define	AE_TXS_MULTICOL		0x0100	/* Multiple collisions occuried. */
#define	AE_TXS_LATECOL		0x0200	/* Late collision occuried. */
#define	AE_TXS_ABORTCOL		0x0400	/* Frame abort due to collisions. */
#define	AE_TXS_UNDERRUN		0x0800	/* Tx SRAM underrun occuried. */
#define	AE_TXS_UPDATE		0x8000

/*
 * RxD flags.
 */
#define	AE_RXD_SUCCESS		0x0001
#define	AE_RXD_BCAST		0x0002	/* Broadcast frame received. */
#define	AE_RXD_MCAST		0x0004	/* Multicast frame received. */
#define	AE_RXD_PAUSE		0x0008	/* Pause frame received. */
#define	AE_RXD_CTRL		0x0010	/* Control frame received. */
#define	AE_RXD_CRCERR		0x0020	/* Invalid frame CRC. */
#define	AE_RXD_CODEERR		0x0040	/* Invalid frame opcode. */
#define	AE_RXD_RUNT		0x0080	/* Runt frame received. */
#define	AE_RXD_FRAG		0x0100	/* Collision fragment received. */
#define	AE_RXD_TRUNC		0x0200	/* The frame was truncated due
					   to Rx SRAM underrun. */
#define	AE_RXD_ALIGN		0x0400	/* Frame alignment error. */
#define	AE_RXD_HAS_VLAN		0x0800	/* VLAN tag present. */
#define	AE_RXD_UPDATE		0x8000
