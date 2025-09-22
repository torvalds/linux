/*	$OpenBSD: if_agereg.h,v 1.3 2009/07/28 13:53:56 kevlo Exp $	*/

/*-
 * Copyright (c) 2008, Pyun YongHyeon <yongari@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 * $FreeBSD: src/sys/dev/age/if_agereg.h,v 1.1 2008/05/19 01:39:59 yongari Exp $
 */

#ifndef	_IF_AGEREG_H
#define	_IF_AGEREG_H

#define	AGE_PCIR_BAR			0x10

/*
 * Attansic Technology Corp. PCI vendor ID
 */
#define	VENDORID_ATTANSIC		0x1969

/*
 * Attansic L1 device ID
 */
#define	DEVICEID_ATTANSIC_L1		0x1048

#define	AGE_VPD_REG_CONF_START		0x0100
#define	AGE_VPD_REG_CONF_END		0x01FF
#define	AGE_VPD_REG_CONF_SIG		0x5A

#define	AGE_SPI_CTRL			0x200
#define	SPI_STAT_NOT_READY		0x00000001
#define	SPI_STAT_WR_ENB			0x00000002
#define	SPI_STAT_WRP_ENB		0x00000080
#define	SPI_INST_MASK			0x000000FF
#define	SPI_START			0x00000100
#define	SPI_INST_START			0x00000800
#define	SPI_VPD_ENB			0x00002000
#define	SPI_LOADER_START		0x00008000
#define	SPI_CS_HI_MASK			0x00030000
#define	SPI_CS_HOLD_MASK		0x000C0000
#define	SPI_CLK_LO_MASK			0x00300000
#define	SPI_CLK_HI_MASK			0x00C00000
#define	SPI_CS_SETUP_MASK		0x03000000
#define	SPI_EPROM_PG_MASK		0x0C000000
#define	SPI_INST_SHIFT			8
#define	SPI_CS_HI_SHIFT			16
#define	SPI_CS_HOLD_SHIFT		18
#define	SPI_CLK_LO_SHIFT		20
#define	SPI_CLK_HI_SHIFT		22
#define	SPI_CS_SETUP_SHIFT		24
#define	SPI_EPROM_PG_SHIFT		26
#define	SPI_WAIT_READY			0x10000000

#define	AGE_SPI_ADDR			0x204	/* 16bits */

#define	AGE_SPI_DATA			0x208

#define	AGE_SPI_CONFIG			0x20C

#define	AGE_SPI_OP_PROGRAM		0x210	/* 8bits */

#define	AGE_SPI_OP_SC_ERASE		0x211	/* 8bits */

#define	AGE_SPI_OP_CHIP_ERASE		0x212	/* 8bits */

#define	AGE_SPI_OP_RDID			0x213	/* 8bits */

#define	AGE_SPI_OP_WREN			0x214	/* 8bits */

#define	AGE_SPI_OP_RDSR			0x215	/* 8bits */

#define	AGE_SPI_OP_WRSR			0x216	/* 8bits */

#define	AGE_SPI_OP_READ			0x217	/* 8bits */

#define	AGE_TWSI_CTRL			0x218
#define	TWSI_CTRL_SW_LD_START		0x00000800
#define	TWSI_CTRL_HW_LD_START		0x00001000
#define	TWSI_CTRL_LD_EXIST		0x00400000

#define AGE_DEV_MISC_CTRL		0x21C

#define	AGE_MASTER_CFG			0x1400
#define	MASTER_RESET			0x00000001
#define	MASTER_MTIMER_ENB		0x00000002
#define	MASTER_ITIMER_ENB		0x00000004
#define	MASTER_MANUAL_INT_ENB		0x00000008
#define	MASTER_CHIP_REV_MASK		0x00FF0000
#define	MASTER_CHIP_ID_MASK		0xFF000000
#define	MASTER_CHIP_REV_SHIFT		16
#define	MASTER_CHIP_ID_SHIFT		24

/* Number of ticks per usec for L1. */
#define	AGE_TICK_USECS			2
#define	AGE_USECS(x)			((x) / AGE_TICK_USECS)

#define	AGE_MANUAL_TIMER		0x1404

#define	AGE_IM_TIMER			0x1408	/* 16bits */
#define	AGE_IM_TIMER_MIN		0
#define	AGE_IM_TIMER_MAX		130000	/* 130ms */
#define	AGE_IM_TIMER_DEFAULT		100

#define	AGE_GPHY_CTRL			0x140C	/* 16bits */
#define	GPHY_CTRL_RST			0x0000
#define	GPHY_CTRL_CLR			0x0001

#define	AGE_INTR_CLR_TIMER		0x140E	/* 16bits */

#define	AGE_IDLE_STATUS			0x1410
#define	IDLE_STATUS_RXMAC		0x00000001
#define	IDLE_STATUS_TXMAC		0x00000002
#define	IDLE_STATUS_RXQ			0x00000004
#define	IDLE_STATUS_TXQ			0x00000008
#define	IDLE_STATUS_DMARD		0x00000010
#define	IDLE_STATUS_DMAWR		0x00000020
#define	IDLE_STATUS_SMB			0x00000040
#define	IDLE_STATUS_CMB			0x00000080

#define	AGE_MDIO			0x1414
#define	MDIO_DATA_MASK			0x0000FFFF
#define	MDIO_REG_ADDR_MASK		0x001F0000
#define	MDIO_OP_READ			0x00200000
#define	MDIO_OP_WRITE			0x00000000
#define	MDIO_SUP_PREAMBLE		0x00400000
#define	MDIO_OP_EXECUTE			0x00800000
#define	MDIO_CLK_25_4			0x00000000
#define	MDIO_CLK_25_6			0x02000000
#define	MDIO_CLK_25_8			0x03000000
#define	MDIO_CLK_25_10			0x04000000
#define	MDIO_CLK_25_14			0x05000000
#define	MDIO_CLK_25_20			0x06000000
#define	MDIO_CLK_25_28			0x07000000
#define	MDIO_OP_BUSY			0x08000000
#define	MDIO_DATA_SHIFT			0
#define	MDIO_REG_ADDR_SHIFT		16

#define	MDIO_REG_ADDR(x)	\
	(((x) << MDIO_REG_ADDR_SHIFT) & MDIO_REG_ADDR_MASK)
/* Default PHY address. */
#define	AGE_PHY_ADDR			0

#define	AGE_PHY_STATUS			0x1418

#define	AGE_BIST0			0x141C
#define	BIST0_ENB			0x00000001
#define	BIST0_SRAM_FAIL			0x00000002
#define	BIST0_FUSE_FLAG			0x00000004

#define	AGE_BIST1			0x1420
#define	BIST1_ENB			0x00000001
#define	BIST1_SRAM_FAIL			0x00000002
#define	BIST1_FUSE_FLAG			0x00000004

#define	AGE_MAC_CFG			0x1480
#define	MAC_CFG_TX_ENB			0x00000001
#define	MAC_CFG_RX_ENB			0x00000002
#define	MAC_CFG_TX_FC			0x00000004
#define	MAC_CFG_RX_FC			0x00000008
#define	MAC_CFG_LOOP			0x00000010
#define	MAC_CFG_FULL_DUPLEX		0x00000020
#define	MAC_CFG_TX_CRC_ENB		0x00000040
#define	MAC_CFG_TX_AUTO_PAD		0x00000080
#define	MAC_CFG_TX_LENCHK		0x00000100
#define	MAC_CFG_RX_JUMBO_ENB		0x00000200
#define	MAC_CFG_PREAMBLE_MASK		0x00003C00
#define	MAC_CFG_VLAN_TAG_STRIP		0x00004000
#define	MAC_CFG_PROMISC			0x00008000
#define	MAC_CFG_TX_PAUSE		0x00010000
#define	MAC_CFG_SCNT			0x00020000
#define	MAC_CFG_SYNC_RST_TX		0x00040000
#define	MAC_CFG_SPEED_MASK		0x00300000
#define	MAC_CFG_SPEED_10_100		0x00100000
#define	MAC_CFG_SPEED_1000		0x00200000
#define	MAC_CFG_DBG_TX_BACKOFF		0x00400000
#define	MAC_CFG_TX_JUMBO_ENB		0x00800000
#define	MAC_CFG_RXCSUM_ENB		0x01000000
#define	MAC_CFG_ALLMULTI		0x02000000
#define	MAC_CFG_BCAST			0x04000000
#define	MAC_CFG_DBG			0x08000000
#define	MAC_CFG_PREAMBLE_SHIFT		10
#define	MAC_CFG_PREAMBLE_DEFAULT	7

#define	AGE_IPG_IFG_CFG			0x1484
#define	IPG_IFG_IPGT_MASK		0x0000007F
#define	IPG_IFG_MIFG_MASK		0x0000FF00
#define	IPG_IFG_IPG1_MASK		0x007F0000
#define	IPG_IFG_IPG2_MASK		0x7F000000
#define	IPG_IFG_IPGT_SHIFT		0
#define	IPG_IFG_IPGT_DEFAULT		0x60
#define	IPG_IFG_MIFG_SHIFT		8
#define	IPG_IFG_MIFG_DEFAULT		0x50
#define	IPG_IFG_IPG1_SHIFT		16
#define	IPG_IFG_IPG1_DEFAULT		0x40
#define	IPG_IFG_IPG2_SHIFT		24
#define	IPG_IFG_IPG2_DEFAULT		0x60

/* station address */
#define	AGE_PAR0			0x1488
#define	AGE_PAR1			0x148C

/* 64bit multicast hash register. */
#define	AGE_MAR0			0x1490
#define	AGE_MAR1			0x1494

/* half-duplex parameter configuration. */
#define	AGE_HDPX_CFG			0x1498
#define	HDPX_CFG_LCOL_MASK		0x000003FF
#define	HDPX_CFG_RETRY_MASK		0x0000F000
#define	HDPX_CFG_EXC_DEF_EN		0x00010000
#define	HDPX_CFG_NO_BACK_C		0x00020000
#define	HDPX_CFG_NO_BACK_P		0x00040000
#define	HDPX_CFG_ABEBE			0x00080000
#define	HDPX_CFG_ABEBT_MASK		0x00F00000
#define	HDPX_CFG_JAMIPG_MASK		0x0F000000
#define	HDPX_CFG_LCOL_SHIFT		0
#define	HDPX_CFG_LCOL_DEFAULT		0x37
#define	HDPX_CFG_RETRY_SHIFT		12
#define	HDPX_CFG_RETRY_DEFAULT		0x0F
#define	HDPX_CFG_ABEBT_SHIFT		20
#define	HDPX_CFG_ABEBT_DEFAULT		0x0A
#define	HDPX_CFG_JAMIPG_SHIFT		24
#define	HDPX_CFG_JAMIPG_DEFAULT		0x07

#define	AGE_FRAME_SIZE			0x149C

#define	AGE_WOL_CFG			0x14A0
#define	WOL_CFG_PATTERN			0x00000001
#define	WOL_CFG_PATTERN_ENB		0x00000002
#define	WOL_CFG_MAGIC			0x00000004
#define	WOL_CFG_MAGIC_ENB		0x00000008
#define	WOL_CFG_LINK_CHG		0x00000010
#define	WOL_CFG_LINK_CHG_ENB		0x00000020
#define	WOL_CFG_PATTERN_DET		0x00000100
#define	WOL_CFG_MAGIC_DET		0x00000200
#define	WOL_CFG_LINK_CHG_DET		0x00000400
#define	WOL_CFG_CLK_SWITCH_ENB		0x00008000
#define	WOL_CFG_PATTERN0		0x00010000
#define	WOL_CFG_PATTERN1		0x00020000
#define	WOL_CFG_PATTERN2		0x00040000
#define	WOL_CFG_PATTERN3		0x00080000
#define	WOL_CFG_PATTERN4		0x00100000
#define	WOL_CFG_PATTERN5		0x00200000
#define	WOL_CFG_PATTERN6		0x00400000

/* WOL pattern length. */
#define	AGE_PATTERN_CFG0		0x14A4
#define	PATTERN_CFG_0_LEN_MASK		0x0000007F
#define	PATTERN_CFG_1_LEN_MASK		0x00007F00
#define	PATTERN_CFG_2_LEN_MASK		0x007F0000
#define	PATTERN_CFG_3_LEN_MASK		0x7F000000

#define	AGE_PATTERN_CFG1		0x14A8
#define	PATTERN_CFG_4_LEN_MASK		0x0000007F
#define	PATTERN_CFG_5_LEN_MASK		0x00007F00
#define	PATTERN_CFG_6_LEN_MASK		0x007F0000

#define	AGE_SRAM_RD_ADDR		0x1500

#define	AGE_SRAM_RD_LEN			0x1504

#define	AGE_SRAM_RRD_ADDR		0x1508

#define	AGE_SRAM_RRD_LEN		0x150C

#define	AGE_SRAM_TPD_ADDR		0x1510

#define	AGE_SRAM_TPD_LEN		0x1514

#define	AGE_SRAM_TRD_ADDR		0x1518

#define	AGE_SRAM_TRD_LEN		0x151C

#define	AGE_SRAM_RX_FIFO_ADDR		0x1520

#define	AGE_SRAM_RX_FIFO_LEN		0x1524

#define	AGE_SRAM_TX_FIFO_ADDR		0x1528

#define	AGE_SRAM_TX_FIFO_LEN		0x152C

#define	AGE_SRAM_TCPH_ADDR		0x1530
#define	SRAM_TCPH_ADDR_MASK		0x00000FFF
#define	SRAM_PATH_ADDR_MASK		0x0FFF0000
#define	SRAM_TCPH_ADDR_SHIFT		0
#define	SRAM_PATH_ADDR_SHIFT		16

#define	AGE_DMA_BLOCK			0x1534
#define	DMA_BLOCK_LOAD			0x00000001

/*
 * All descriptors and CMB/SMB share the same high address.
 */
#define	AGE_DESC_ADDR_HI		0x1540

#define	AGE_DESC_RD_ADDR_LO		0x1544

#define	AGE_DESC_RRD_ADDR_LO		0x1548

#define	AGE_DESC_TPD_ADDR_LO		0x154C

#define	AGE_DESC_CMB_ADDR_LO		0x1550

#define	AGE_DESC_SMB_ADDR_LO		0x1554

#define	AGE_DESC_RRD_RD_CNT		0x1558
#define	DESC_RD_CNT_MASK		0x000007FF
#define	DESC_RRD_CNT_MASK		0x07FF0000
#define	DESC_RD_CNT_SHIFT		0
#define	DESC_RRD_CNT_SHIFT		16

#define	AGE_DESC_TPD_CNT		0x155C
#define	DESC_TPD_CNT_MASK		0x00003FF
#define	DESC_TPD_CNT_SHIFT		0

#define	AGE_TXQ_CFG			0x1580
#define	TXQ_CFG_TPD_BURST_MASK		0x0000001F
#define	TXQ_CFG_ENB			0x00000020
#define	TXQ_CFG_ENHANCED_MODE		0x00000040
#define	TXQ_CFG_TPD_FETCH_THRESH_MASK	0x00003F00
#define	TXQ_CFG_TX_FIFO_BURST_MASK	0xFFFF0000
#define	TXQ_CFG_TPD_BURST_SHIFT		0
#define	TXQ_CFG_TPD_BURST_DEFAULT	4
#define	TXQ_CFG_TPD_FETCH_THRESH_SHIFT	8
#define	TXQ_CFG_TPD_FETCH_DEFAULT	16
#define	TXQ_CFG_TX_FIFO_BURST_SHIFT	16
#define	TXQ_CFG_TX_FIFO_BURST_DEFAULT	256

#define	AGE_TX_JUMBO_TPD_TH_IPG		0x1584
#define	TX_JUMBO_TPD_TH_MASK		0x000007FF
#define	TX_JUMBO_TPD_IPG_MASK		0x001F0000
#define	TX_JUMBO_TPD_TH_SHIFT		0
#define	TX_JUMBO_TPD_IPG_SHIFT		16
#define	TX_JUMBO_TPD_IPG_DEFAULT	1

#define	AGE_RXQ_CFG			0x15A0
#define	RXQ_CFG_RD_BURST_MASK		0x000000FF
#define	RXQ_CFG_RRD_BURST_THRESH_MASK	0x0000FF00
#define	RXQ_CFG_RD_PREF_MIN_IPG_MASK	0x001F0000
#define	RXQ_CFG_CUT_THROUGH_ENB		0x40000000
#define	RXQ_CFG_ENB			0x80000000
#define	RXQ_CFG_RD_BURST_SHIFT		0
#define	RXQ_CFG_RD_BURST_DEFAULT	8
#define	RXQ_CFG_RRD_BURST_THRESH_SHIFT	8
#define	RXQ_CFG_RRD_BURST_THRESH_DEFAULT 8
#define	RXQ_CFG_RD_PREF_MIN_IPG_SHIFT	16
#define	RXQ_CFG_RD_PREF_MIN_IPG_DEFAULT	1

#define	AGE_RXQ_JUMBO_CFG		0x15A4
#define	RXQ_JUMBO_CFG_SZ_THRESH_MASK	0x000007FF
#define	RXQ_JUMBO_CFG_LKAH_MASK		0x00007800
#define	RXQ_JUMBO_CFG_RRD_TIMER_MASK	0xFFFF0000
#define	RXQ_JUMBO_CFG_SZ_THRESH_SHIFT	0
#define	RXQ_JUMBO_CFG_LKAH_SHIFT	11
#define	RXQ_JUMBO_CFG_LKAH_DEFAULT	0x01
#define	RXQ_JUMBO_CFG_RRD_TIMER_SHIFT	16

#define	AGE_RXQ_FIFO_PAUSE_THRESH	0x15A8
#define	RXQ_FIFO_PAUSE_THRESH_LO_MASK	0x00000FFF
#define	RXQ_FIFO_PAUSE_THRESH_HI_MASK	0x0FFF000
#define	RXQ_FIFO_PAUSE_THRESH_LO_SHIFT	0
#define	RXQ_FIFO_PAUSE_THRESH_HI_SHIFT	16

#define	AGE_RXQ_RRD_PAUSE_THRESH	0x15AC
#define	RXQ_RRD_PAUSE_THRESH_HI_MASK	0x00000FFF
#define	RXQ_RRD_PAUSE_THRESH_LO_MASK	0x0FFF0000
#define	RXQ_RRD_PAUSE_THRESH_HI_SHIFT	0
#define	RXQ_RRD_PAUSE_THRESH_LO_SHIFT	16

#define	AGE_DMA_CFG			0x15C0
#define	DMA_CFG_IN_ORDER		0x00000001
#define	DMA_CFG_ENH_ORDER		0x00000002
#define	DMA_CFG_OUT_ORDER		0x00000004
#define	DMA_CFG_RCB_64			0x00000000
#define	DMA_CFG_RCB_128			0x00000008
#define	DMA_CFG_RD_BURST_128		0x00000000
#define	DMA_CFG_RD_BURST_256		0x00000010
#define	DMA_CFG_RD_BURST_512		0x00000020
#define	DMA_CFG_RD_BURST_1024		0x00000030
#define	DMA_CFG_RD_BURST_2048		0x00000040
#define	DMA_CFG_RD_BURST_4096		0x00000050
#define	DMA_CFG_WR_BURST_128		0x00000000
#define	DMA_CFG_WR_BURST_256		0x00000080
#define	DMA_CFG_WR_BURST_512		0x00000100
#define	DMA_CFG_WR_BURST_1024		0x00000180
#define	DMA_CFG_WR_BURST_2048		0x00000200
#define	DMA_CFG_WR_BURST_4096		0x00000280
#define	DMA_CFG_RD_ENB			0x00000400
#define	DMA_CFG_WR_ENB			0x00000800
#define	DMA_CFG_RD_BURST_MASK		0x07
#define	DMA_CFG_RD_BURST_SHIFT		4
#define	DMA_CFG_WR_BURST_MASK		0x07
#define	DMA_CFG_WR_BURST_SHIFT		7

#define	AGE_CSMB_CTRL			0x15D0
#define	CSMB_CTRL_CMB_KICK		0x00000001
#define	CSMB_CTRL_SMB_KICK		0x00000002
#define	CSMB_CTRL_CMB_ENB		0x00000004
#define	CSMB_CTRL_SMB_ENB		0x00000008

/* CMB DMA Write Threshold Register */
#define	AGE_CMB_WR_THRESH		0x15D4
#define	CMB_WR_THRESH_RRD_MASK		0x000007FF
#define	CMB_WR_THRESH_TPD_MASK		0x07FF0000
#define	CMB_WR_THRESH_RRD_SHIFT		0
#define	CMB_WR_THRESH_RRD_DEFAULT	4
#define	CMB_WR_THRESH_TPD_SHIFT		16
#define	CMB_WR_THRESH_TPD_DEFAULT	4

/* RX/TX count-down timer to trigger CMB-write. */
#define	AGE_CMB_WR_TIMER		0x15D8
#define	CMB_WR_TIMER_RX_MASK		0x0000FFFF
#define	CMB_WR_TIMER_TX_MASK		0xFFFF0000
#define	CMB_WR_TIMER_RX_SHIFT		0
#define	CMB_WR_TIMER_TX_SHIFT		16

/* Number of packet received since last CMB write */
#define	AGE_CMB_RX_PKT_CNT		0x15DC

/* Number of packet transmitted since last CMB write */
#define	AGE_CMB_TX_PKT_CNT		0x15E0

/* SMB auto DMA timer register */
#define	AGE_SMB_TIMER			0x15E4

#define	AGE_MBOX			0x15F0
#define	MBOX_RD_PROD_IDX_MASK		0x000007FF
#define	MBOX_RRD_CONS_IDX_MASK		0x003FF800
#define	MBOX_TD_PROD_IDX_MASK		0xFFC00000
#define	MBOX_RD_PROD_IDX_SHIFT		0
#define	MBOX_RRD_CONS_IDX_SHIFT		11
#define	MBOX_TD_PROD_IDX_SHIFT		22

#define	AGE_INTR_STATUS			0x1600
#define	INTR_SMB			0x00000001
#define	INTR_MOD_TIMER			0x00000002
#define	INTR_MANUAL_TIMER		0x00000004
#define	INTR_RX_FIFO_OFLOW		0x00000008
#define	INTR_RD_UNDERRUN		0x00000010
#define	INTR_RRD_OFLOW			0x00000020
#define	INTR_TX_FIFO_UNDERRUN		0x00000040
#define	INTR_LINK_CHG			0x00000080
#define	INTR_HOST_RD_UNDERRUN		0x00000100
#define	INTR_HOST_RRD_OFLOW		0x00000200
#define	INTR_DMA_RD_TO_RST		0x00000400
#define	INTR_DMA_WR_TO_RST		0x00000800
#define	INTR_GPHY			0x00001000
#define	INTR_RX_PKT			0x00010000
#define	INTR_TX_PKT			0x00020000
#define	INTR_TX_DMA			0x00040000
#define	INTR_RX_DMA			0x00080000
#define	INTR_CMB_RX			0x00100000
#define	INTR_CMB_TX			0x00200000
#define	INTR_MAC_RX			0x00400000
#define	INTR_MAC_TX			0x00800000
#define	INTR_UNDERRUN			0x01000000
#define	INTR_FRAME_ERROR		0x02000000
#define	INTR_FRAME_OK			0x04000000
#define	INTR_CSUM_ERROR			0x08000000
#define	INTR_PHY_LINK_DOWN		0x10000000
#define	INTR_DIS_SMB			0x20000000
#define	INTR_DIS_DMA			0x40000000
#define	INTR_DIS_INT			0x80000000

/* Interrupt Mask Register */
#define	AGE_INTR_MASK			0x1604

#define	AGE_INTRS						\
	(INTR_SMB | INTR_DMA_RD_TO_RST | INTR_DMA_WR_TO_RST |	\
	INTR_CMB_TX | INTR_CMB_RX)

/* Statistics counters collected by the MAC. */
struct smb {
	/* Rx stats. */
	uint32_t rx_frames;
	uint32_t rx_bcast_frames;
	uint32_t rx_mcast_frames;
	uint32_t rx_pause_frames;
	uint32_t rx_control_frames;
	uint32_t rx_crcerrs;
	uint32_t rx_lenerrs;
	uint32_t rx_bytes;
	uint32_t rx_runts;
	uint32_t rx_fragments;
	uint32_t rx_pkts_64;
	uint32_t rx_pkts_65_127;
	uint32_t rx_pkts_128_255;
	uint32_t rx_pkts_256_511;
	uint32_t rx_pkts_512_1023;
	uint32_t rx_pkts_1024_1518;
	uint32_t rx_pkts_1519_max;
	uint32_t rx_pkts_truncated;
	uint32_t rx_fifo_oflows;
	uint32_t rx_desc_oflows;
	uint32_t rx_alignerrs;
	uint32_t rx_bcast_bytes;
	uint32_t rx_mcast_bytes;
	uint32_t rx_pkts_filtered;
	/* Tx stats. */
	uint32_t tx_frames;
	uint32_t tx_bcast_frames;
	uint32_t tx_mcast_frames;
	uint32_t tx_pause_frames;
	uint32_t tx_excess_defer;
	uint32_t tx_control_frames;
	uint32_t tx_deferred;
	uint32_t tx_bytes;
	uint32_t tx_pkts_64;
	uint32_t tx_pkts_65_127;
	uint32_t tx_pkts_128_255;
	uint32_t tx_pkts_256_511;
	uint32_t tx_pkts_512_1023;
	uint32_t tx_pkts_1024_1518;
	uint32_t tx_pkts_1519_max;
	uint32_t tx_single_colls;
	uint32_t tx_multi_colls;
	uint32_t tx_late_colls;
	uint32_t tx_excess_colls;
	uint32_t tx_underrun;
	uint32_t tx_desc_underrun;
	uint32_t tx_lenerrs;
	uint32_t tx_pkts_truncated;
	uint32_t tx_bcast_bytes;
	uint32_t tx_mcast_bytes;
	uint32_t updated;
} __packed;

/* Coalescing message block */
struct cmb {
	uint32_t intr_status;
	uint32_t rprod_cons;
#define	RRD_PROD_MASK			0x0000FFFF
#define	RD_CONS_MASK			0xFFFF0000
#define	RRD_PROD_SHIFT			0
#define	RD_CONS_SHIFT			16
	uint32_t tpd_cons;
#define	CMB_UPDATED			0x00000001
#define	TPD_CONS_MASK			0xFFFF0000
#define	TPD_CONS_SHIFT			16
} __packed;

/* Rx return descriptor */
struct rx_rdesc {
	uint32_t index;
#define	AGE_RRD_NSEGS_MASK		0x000000FF
#define	AGE_RRD_CONS_MASK		0xFFFF0000
#define	AGE_RRD_NSEGS_SHIFT		0
#define	AGE_RRD_CONS_SHIFT		16
	uint32_t len;
#define	AGE_RRD_CSUM_MASK		0x0000FFFF
#define	AGE_RRD_LEN_MASK		0xFFFF0000
#define	AGE_RRD_CSUM_SHIFT		0
#define	AGE_RRD_LEN_SHIFT		16
	uint32_t flags;
#define	AGE_RRD_ETHERNET		0x00000080
#define	AGE_RRD_VLAN			0x00000100
#define	AGE_RRD_ERROR			0x00000200
#define	AGE_RRD_IPV4			0x00000400
#define	AGE_RRD_UDP			0x00000800
#define	AGE_RRD_TCP			0x00001000
#define	AGE_RRD_BCAST			0x00002000
#define	AGE_RRD_MCAST			0x00004000
#define	AGE_RRD_PAUSE			0x00008000
#define	AGE_RRD_CRC			0x00010000
#define	AGE_RRD_CODE			0x00020000
#define	AGE_RRD_DRIBBLE			0x00040000
#define	AGE_RRD_RUNT			0x00080000
#define	AGE_RRD_OFLOW			0x00100000
#define	AGE_RRD_TRUNC			0x00200000
#define	AGE_RRD_IPCSUM_NOK		0x00400000
#define	AGE_RRD_TCP_UDPCSUM_NOK		0x00800000
#define	AGE_RRD_LENGTH_NOK		0x01000000
#define	AGE_RRD_DES_ADDR_FILTERED	0x02000000
	uint32_t vtags;
#define	AGE_RRD_VLAN_MASK		0xFFFF0000
#define	AGE_RRD_VLAN_SHIFT		16
} __packed;

#define	AGE_RX_NSEGS(x)		\
	(((x) & AGE_RRD_NSEGS_MASK) >> AGE_RRD_NSEGS_SHIFT)
#define	AGE_RX_CONS(x)		\
	(((x) & AGE_RRD_CONS_MASK) >> AGE_RRD_CONS_SHIFT)
#define	AGE_RX_CSUM(x)		\
	(((x) & AGE_RRD_CSUM_MASK) >> AGE_RRD_CSUM_SHIFT)
#define	AGE_RX_BYTES(x)		\
	(((x) & AGE_RRD_LEN_MASK) >> AGE_RRD_LEN_SHIFT)
#define	AGE_RX_VLAN(x)		\
	(((x) & AGE_RRD_VLAN_MASK) >> AGE_RRD_VLAN_SHIFT)
#define	AGE_RX_VLAN_TAG(x)	\
	(((x) >> 4) | (((x) & 7) << 13) | (((x) & 8) << 9))

/* Rx descriptor. */
struct rx_desc {
	uint64_t addr;
	uint32_t len;
#define	AGE_RD_LEN_MASK			0x0000FFFF
#define	AGE_CONS_UPD_REQ_MASK		0xFFFF0000
#define	AGE_RD_LEN_SHIFT		0
#define	AGE_CONS_UPD_REQ_SHIFT		16
} __packed;

/* Tx descriptor. */
struct tx_desc {
	uint64_t addr;
	uint32_t len;
#define	AGE_TD_VLAN_MASK		0xFFFF0000
#define	AGE_TD_PKT_INT			0x00008000
#define	AGE_TD_DMA_INT			0x00004000
#define	AGE_TD_BUFLEN_MASK		0x00003FFF
#define	AGE_TD_VLAN_SHIFT		16
#define	AGE_TX_VLAN_TAG(x)	\
	(((x) << 4) | ((x) >> 13) | (((x) >> 9) & 8))
#define	AGE_TD_BUFLEN_SHIFT		0
#define	AGE_TX_BYTES(x)		\
	(((x) << AGE_TD_BUFLEN_SHIFT) & AGE_TD_BUFLEN_MASK)
	uint32_t flags;
#define	AGE_TD_TSO_MSS			0xFFF80000
#define	AGE_TD_TSO_HDR			0x00040000
#define	AGE_TD_TSO_TCPHDR_LEN		0x0003C000
#define	AGE_TD_IPHDR_LEN		0x00003C00
#define	AGE_TD_LLC_SNAP			0x00000200
#define	AGE_TD_VLAN_TAGGED		0x00000100
#define	AGE_TD_UDPCSUM			0x00000080
#define	AGE_TD_TCPCSUM			0x00000040
#define	AGE_TD_IPCSUM			0x00000020
#define	AGE_TD_TSO_IPV4			0x00000010
#define	AGE_TD_TSO_IPV6			0x00000012
#define	AGE_TD_CSUM			0x00000008
#define	AGE_TD_INSERT_VLAN_TAG		0x00000004
#define	AGE_TD_COALESCE			0x00000002
#define	AGE_TD_EOP			0x00000001

#define	AGE_TD_CSUM_PLOADOFFSET		0x00FF0000
#define	AGE_TD_CSUM_XSUMOFFSET		0xFF000000
#define	AGE_TD_CSUM_XSUMOFFSET_SHIFT	24
#define	AGE_TD_CSUM_PLOADOFFSET_SHIFT	16
#define	AGE_TD_TSO_MSS_SHIFT		19
#define	AGE_TD_TSO_TCPHDR_LEN_SHIFT	14
#define	AGE_TD_IPHDR_LEN_SHIFT		10
} __packed;

#define	AGE_TX_RING_CNT		256
#define	AGE_RX_RING_CNT		256
#define	AGE_RR_RING_CNT		(AGE_TX_RING_CNT + AGE_RX_RING_CNT)
/* The following ring alignments are just guessing. */
#define	AGE_TX_RING_ALIGN	16
#define	AGE_RX_RING_ALIGN	16
#define	AGE_RR_RING_ALIGN	16
#define	AGE_CMB_ALIGN		16
#define	AGE_SMB_ALIGN		16

#define	AGE_TSO_MAXSEGSIZE	4096
#define	AGE_TSO_MAXSIZE		(65535 + sizeof(struct ether_vlan_header))
#define	AGE_MAXTXSEGS		32

#define	AGE_ADDR_LO(x)		((uint64_t) (x) & 0xFFFFFFFF)
#define	AGE_ADDR_HI(x)		((uint64_t) (x) >> 32)

#define	AGE_MSI_MESSAGES	1
#define	AGE_MSIX_MESSAGES	1

#define AGE_JUMBO_FRAMELEN	10240
#define AGE_JUMBO_MTU		\
	(AGE_JUMBO_FRAMELEN - EVL_ENCAPLEN - \
	ETHER_HDR_LEN - ETHER_CRC_LEN)

#define	AGE_DESC_INC(x, y)	((x) = ((x) + 1) % (y))

#define	AGE_PROC_MIN		30
#define	AGE_PROC_MAX		(AGE_RX_RING_CNT - 1)
#define	AGE_PROC_DEFAULT	(AGE_RX_RING_CNT / 2)

struct age_txdesc {
	struct mbuf		*tx_m;
	bus_dmamap_t		tx_dmamap;
	struct tx_desc		*tx_desc;
};

struct age_rxdesc {
	struct mbuf 		*rx_m;
	bus_dmamap_t		rx_dmamap;
	struct rx_desc		*rx_desc;
};

struct age_chain_data{
	struct age_txdesc	age_txdesc[AGE_TX_RING_CNT];
	struct age_rxdesc	age_rxdesc[AGE_RX_RING_CNT];
	bus_dmamap_t		age_tx_ring_map;
	bus_dma_segment_t	age_tx_ring_seg;
	bus_dmamap_t		age_rx_ring_map;
	bus_dma_segment_t	age_rx_ring_seg;
	bus_dmamap_t		age_rx_sparemap;
	bus_dmamap_t		age_rr_ring_map;
	bus_dma_segment_t	age_rr_ring_seg;
	bus_dmamap_t		age_cmb_block_map;
	bus_dma_segment_t	age_cmb_block_seg;
	bus_dmamap_t		age_smb_block_map;
	bus_dma_segment_t	age_smb_block_seg;

	int			age_tx_prod;
	int			age_tx_cons;
	int			age_tx_cnt;
	int			age_rx_cons;
	int			age_rr_cons;
	int			age_rxlen;

	struct mbuf		*age_rxhead;
	struct mbuf		*age_rxtail;
	struct mbuf		*age_rxprev_tail;
};

struct age_ring_data {
	struct tx_desc		*age_tx_ring;
	bus_dma_segment_t	age_tx_ring_seg;
	bus_addr_t		age_tx_ring_paddr;
	struct rx_desc		*age_rx_ring;
	bus_dma_segment_t	age_rx_ring_seg;
	bus_addr_t		age_rx_ring_paddr;
	struct rx_rdesc		*age_rr_ring;
	bus_dma_segment_t	age_rr_ring_seg;
	bus_addr_t		age_rr_ring_paddr;
	struct cmb		*age_cmb_block;
	bus_dma_segment_t	age_cmb_block_seg;
	bus_addr_t		age_cmb_block_paddr;
	struct smb		*age_smb_block;
	bus_dma_segment_t	age_smb_block_seg;
	bus_addr_t		age_smb_block_paddr;
};

#define AGE_TX_RING_SZ		\
    (sizeof(struct tx_desc) * AGE_TX_RING_CNT)
#define AGE_RX_RING_SZ		\
    (sizeof(struct rx_desc) * AGE_RX_RING_CNT)
#define	AGE_RR_RING_SZ		\
    (sizeof(struct rx_rdesc) * AGE_RR_RING_CNT)
#define	AGE_CMB_BLOCK_SZ	sizeof(struct cmb)
#define	AGE_SMB_BLOCK_SZ	sizeof(struct smb)

struct age_stats {
	/* Rx stats. */
	uint64_t rx_frames;
	uint64_t rx_bcast_frames;
	uint64_t rx_mcast_frames;
	uint32_t rx_pause_frames;
	uint32_t rx_control_frames;
	uint32_t rx_crcerrs;
	uint32_t rx_lenerrs;
	uint64_t rx_bytes;
	uint32_t rx_runts;
	uint64_t rx_fragments;
	uint64_t rx_pkts_64;
	uint64_t rx_pkts_65_127;
	uint64_t rx_pkts_128_255;
	uint64_t rx_pkts_256_511;
	uint64_t rx_pkts_512_1023;
	uint64_t rx_pkts_1024_1518;
	uint64_t rx_pkts_1519_max;
	uint64_t rx_pkts_truncated;
	uint32_t rx_fifo_oflows;
	uint32_t rx_desc_oflows;
	uint32_t rx_alignerrs;
	uint64_t rx_bcast_bytes;
	uint64_t rx_mcast_bytes;
	uint64_t rx_pkts_filtered;
	/* Tx stats. */
	uint64_t tx_frames;
	uint64_t tx_bcast_frames;
	uint64_t tx_mcast_frames;
	uint32_t tx_pause_frames;
	uint32_t tx_excess_defer;
	uint32_t tx_control_frames;
	uint32_t tx_deferred;
	uint64_t tx_bytes;
	uint64_t tx_pkts_64;
	uint64_t tx_pkts_65_127;
	uint64_t tx_pkts_128_255;
	uint64_t tx_pkts_256_511;
	uint64_t tx_pkts_512_1023;
	uint64_t tx_pkts_1024_1518;
	uint64_t tx_pkts_1519_max;
	uint32_t tx_single_colls;
	uint32_t tx_multi_colls;
	uint32_t tx_late_colls;
	uint32_t tx_excess_colls;
	uint32_t tx_underrun;
	uint32_t tx_desc_underrun;
	uint32_t tx_lenerrs;
	uint32_t tx_pkts_truncated;
	uint64_t tx_bcast_bytes;
	uint64_t tx_mcast_bytes;
};

/*
 * Software state per device.
 */
struct age_softc {
	struct device 		sc_dev;
	struct arpcom		sc_arpcom;

	bus_space_tag_t		sc_mem_bt;
	bus_space_handle_t	sc_mem_bh;
	bus_size_t		sc_mem_size;
	bus_dma_tag_t		sc_dmat;
	pci_chipset_tag_t	sc_pct;
	pcitag_t		sc_pcitag;

	void			*sc_irq_handle;

	struct mii_data		sc_miibus;
	int			age_rev;
	int			age_chip_rev;
	int			age_phyaddr;

	uint8_t			age_eaddr[ETHER_ADDR_LEN];
	uint32_t		age_dma_rd_burst;
	uint32_t		age_dma_wr_burst;

	uint32_t		age_flags;
#define AGE_FLAG_PCIE		0x0001
#define AGE_FLAG_PCIX		0x0002
#define AGE_FLAG_MSI		0x0004
#define AGE_FLAG_MSIX		0x0008
#define AGE_FLAG_PMCAP		0x0010
#define AGE_FLAG_DETACH		0x4000
#define AGE_FLAG_LINK		0x8000

	struct timeout		age_tick_ch;
	struct age_stats	age_stat;
	struct age_chain_data	age_cdata;
	struct age_ring_data	age_rdata;
	int			age_process_limit;
	int			age_int_mod;
	int			age_max_frame_size;
	int			age_morework;
	int			age_rr_prod;
	int			age_tpd_cons;

	int			age_txd_spare;
};

/* Register access macros. */
#define CSR_WRITE_4(sc, reg, val)	\
	bus_space_write_4((sc)->sc_mem_bt, (sc)->sc_mem_bh, (reg), (val))
#define CSR_WRITE_2(sc, reg, val)	\
	bus_space_write_2((sc)->sc_mem_bt, (sc)->sc_mem_bh, (reg), (val))
#define CSR_READ_2(sc, reg)		\
	bus_space_read_2((sc)->sc_mem_bt, (sc)->sc_mem_bh, (reg))
#define CSR_READ_4(sc, reg)		\
	bus_space_read_4((sc)->sc_mem_bt, (sc)->sc_mem_bh, (reg))


#define	AGE_COMMIT_MBOX(_sc)						\
do {									\
	CSR_WRITE_4(_sc, AGE_MBOX,					\
	    (((_sc)->age_cdata.age_rx_cons << MBOX_RD_PROD_IDX_SHIFT) &	\
	    MBOX_RD_PROD_IDX_MASK) |					\
	    (((_sc)->age_cdata.age_rr_cons <<				\
	    MBOX_RRD_CONS_IDX_SHIFT) & MBOX_RRD_CONS_IDX_MASK) |	\
	    (((_sc)->age_cdata.age_tx_prod << MBOX_TD_PROD_IDX_SHIFT) &	\
	    MBOX_TD_PROD_IDX_MASK));					\
} while (0)

#define	AGE_RXCHAIN_RESET(_sc)						\
do {									\
	(_sc)->age_cdata.age_rxhead = NULL;				\
	(_sc)->age_cdata.age_rxtail = NULL;				\
	(_sc)->age_cdata.age_rxprev_tail = NULL;			\
	(_sc)->age_cdata.age_rxlen = 0;					\
} while (0)

#define	AGE_TX_TIMEOUT		5
#define AGE_RESET_TIMEOUT	100
#define AGE_TIMEOUT		1000
#define AGE_PHY_TIMEOUT		1000

#endif	/* _IF_AGEREG_H */
