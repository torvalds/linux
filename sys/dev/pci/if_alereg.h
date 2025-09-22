/*	$OpenBSD: if_alereg.h,v 1.4 2022/01/09 05:42:46 jsg Exp $	*/
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
 * $FreeBSD: src/sys/dev/ale/if_alereg.h,v 1.1 2008/11/12 09:52:06 yongari Exp $
 */

#ifndef	_IF_ALEREG_H
#define	_IF_ALEREG_H

#define ALE_PCIR_BAR			0x10

#define	ALE_SPI_CTRL			0x200
#define	SPI_VPD_ENB			0x00002000

#define	ALE_SPI_ADDR			0x204	/* 16bits */

#define	ALE_SPI_DATA			0x208

#define	ALE_SPI_CONFIG			0x20C

#define	ALE_SPI_OP_PROGRAM		0x210	/* 8bits */

#define	ALE_SPI_OP_SC_ERASE		0x211	/* 8bits */

#define	ALE_SPI_OP_CHIP_ERASE		0x212	/* 8bits */

#define	ALE_SPI_OP_RDID			0x213	/* 8bits */

#define	ALE_SPI_OP_WREN			0x214	/* 8bits */

#define	ALE_SPI_OP_RDSR			0x215	/* 8bits */

#define	ALE_SPI_OP_WRSR			0x216	/* 8bits */

#define	ALE_SPI_OP_READ			0x217	/* 8bits */

#define	ALE_TWSI_CTRL			0x218
#define	TWSI_CTRL_SW_LD_START		0x00000800
#define	TWSI_CTRL_HW_LD_START		0x00001000
#define	TWSI_CTRL_LD_EXIST		0x00400000

#define ALE_DEV_MISC_CTRL		0x21C

#define	ALE_PCIE_PHYMISC		0x1000
#define	PCIE_PHYMISC_FORCE_RCV_DET	0x00000004

#define	ALE_MASTER_CFG			0x1400
#define	MASTER_RESET			0x00000001
#define	MASTER_MTIMER_ENB		0x00000002
#define	MASTER_IM_TX_TIMER_ENB		0x00000004
#define	MASTER_MANUAL_INT_ENB		0x00000008
#define	MASTER_IM_RX_TIMER_ENB		0x00000020
#define	MASTER_INT_RDCLR		0x00000040
#define	MASTER_LED_MODE			0x00000200
#define	MASTER_CHIP_REV_MASK		0x00FF0000
#define	MASTER_CHIP_ID_MASK		0xFF000000
#define	MASTER_CHIP_REV_SHIFT		16
#define	MASTER_CHIP_ID_SHIFT		24

/* Number of ticks per usec for AR81xx. */
#define	ALE_TICK_USECS			2
#define	ALE_USECS(x)			((x) / ALE_TICK_USECS)

#define	ALE_MANUAL_TIMER		0x1404

#define	ALE_IM_TIMER			0x1408
#define	IM_TIMER_TX_MASK		0x0000FFFF
#define	IM_TIMER_RX_MASK		0xFFFF0000
#define	IM_TIMER_TX_SHIFT		0
#define	IM_TIMER_RX_SHIFT		16
#define	ALE_IM_TIMER_MIN		0
#define	ALE_IM_TIMER_MAX		130000	/* 130ms */
#define	ALE_IM_RX_TIMER_DEFAULT		30
#define	ALE_IM_TX_TIMER_DEFAULT		1000

#define	ALE_GPHY_CTRL			0x140C	/* 16bits */
#define	GPHY_CTRL_EXT_RESET		0x0001
#define	GPHY_CTRL_PIPE_MOD		0x0002
#define	GPHY_CTRL_BERT_START		0x0010
#define	GPHY_CTRL_GALE_25M_ENB		0x0020
#define	GPHY_CTRL_LPW_EXIT		0x0040
#define	GPHY_CTRL_PHY_IDDQ		0x0080
#define	GPHY_CTRL_PHY_IDDQ_DIS		0x0100
#define	GPHY_CTRL_PCLK_SEL_DIS		0x0200
#define	GPHY_CTRL_HIB_EN		0x0400
#define	GPHY_CTRL_HIB_PULSE		0x0800
#define	GPHY_CTRL_SEL_ANA_RESET		0x1000
#define	GPHY_CTRL_PHY_PLL_ON		0x2000
#define	GPHY_CTRL_PWDOWN_HW		0x4000

#define	ALE_INTR_CLR_TIMER		0x140E	/* 16bits */

#define	ALE_IDLE_STATUS			0x1410
#define	IDLE_STATUS_RXMAC		0x00000001
#define	IDLE_STATUS_TXMAC		0x00000002
#define	IDLE_STATUS_RXQ			0x00000004
#define	IDLE_STATUS_TXQ			0x00000008
#define	IDLE_STATUS_DMARD		0x00000010
#define	IDLE_STATUS_DMAWR		0x00000020
#define	IDLE_STATUS_SMB			0x00000040
#define	IDLE_STATUS_CMB			0x00000080

#define	ALE_MDIO			0x1414
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
#define	ALE_PHY_ADDR			0

#define	ALE_PHY_STATUS			0x1418
#define	PHY_STATUS_100M			0x00020000

/* Packet memory BIST. */
#define	ALE_BIST0			0x141C
#define	BIST0_ENB			0x00000001
#define	BIST0_SRAM_FAIL			0x00000002
#define	BIST0_FUSE_FLAG			0x00000004

/* PCIe retry buffer BIST. */
#define	ALE_BIST1			0x1420
#define	BIST1_ENB			0x00000001
#define	BIST1_SRAM_FAIL			0x00000002
#define	BIST1_FUSE_FLAG			0x00000004

#define	ALE_SERDES_LOCK			0x1424
#define	SERDES_LOCK_DET			0x00000001
#define	SERDES_LOCK_DET_ENB		0x00000002

#define	ALE_MAC_CFG			0x1480
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

#define	ALE_IPG_IFG_CFG			0x1484
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

/* Station address. */
#define	ALE_PAR0			0x1488
#define	ALE_PAR1			0x148C

/* 64bit multicast hash register. */
#define	ALE_MAR0			0x1490
#define	ALE_MAR1			0x1494

/* half-duplex parameter configuration. */
#define	ALE_HDPX_CFG			0x1498
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

#define	ALE_FRAME_SIZE			0x149C

#define	ALE_WOL_CFG			0x14A0
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
#define	ALE_PATTERN_CFG0		0x14A4
#define	PATTERN_CFG_0_LEN_MASK		0x0000007F
#define	PATTERN_CFG_1_LEN_MASK		0x00007F00
#define	PATTERN_CFG_2_LEN_MASK		0x007F0000
#define	PATTERN_CFG_3_LEN_MASK		0x7F000000

#define	ALE_PATTERN_CFG1		0x14A8
#define	PATTERN_CFG_4_LEN_MASK		0x0000007F
#define	PATTERN_CFG_5_LEN_MASK		0x00007F00
#define	PATTERN_CFG_6_LEN_MASK		0x007F0000

/* RSS */
#define	ALE_RSS_KEY0			0x14B0

#define	ALE_RSS_KEY1			0x14B4

#define	ALE_RSS_KEY2			0x14B8

#define	ALE_RSS_KEY3			0x14BC

#define	ALE_RSS_KEY4			0x14C0

#define	ALE_RSS_KEY5			0x14C4

#define	ALE_RSS_KEY6			0x14C8

#define	ALE_RSS_KEY7			0x14CC

#define	ALE_RSS_KEY8			0x14D0

#define	ALE_RSS_KEY9			0x14D4

#define	ALE_RSS_IDT_TABLE4		0x14E0

#define	ALE_RSS_IDT_TABLE5		0x14E4

#define	ALE_RSS_IDT_TABLE6		0x14E8

#define	ALE_RSS_IDT_TABLE7		0x14EC

#define	ALE_SRAM_RD_ADDR		0x1500

#define	ALE_SRAM_RD_LEN			0x1504

#define	ALE_SRAM_RRD_ADDR		0x1508

#define	ALE_SRAM_RRD_LEN		0x150C

#define	ALE_SRAM_TPD_ADDR		0x1510

#define	ALE_SRAM_TPD_LEN		0x1514

#define	ALE_SRAM_TRD_ADDR		0x1518

#define	ALE_SRAM_TRD_LEN		0x151C

#define	ALE_SRAM_RX_FIFO_ADDR		0x1520

#define	ALE_SRAM_RX_FIFO_LEN		0x1524

#define	ALE_SRAM_TX_FIFO_ADDR		0x1528

#define	ALE_SRAM_TX_FIFO_LEN		0x152C

#define	ALE_SRAM_TCPH_ADDR		0x1530
#define	SRAM_TCPH_ADDR_MASK		0x00000FFF
#define	SRAM_PATH_ADDR_MASK		0x0FFF0000
#define	SRAM_TCPH_ADDR_SHIFT		0
#define	SRAM_PATH_ADDR_SHIFT		16

#define	ALE_DMA_BLOCK			0x1534
#define	DMA_BLOCK_LOAD			0x00000001

#define	ALE_RXF3_ADDR_HI		0x153C

#define	ALE_TPD_ADDR_HI			0x1540

#define	ALE_RXF0_PAGE0_ADDR_LO		0x1544

#define	ALE_RXF0_PAGE1_ADDR_LO		0x1548

#define	ALE_TPD_ADDR_LO			0x154C

#define	ALE_RXF1_ADDR_HI		0x1550

#define	ALE_RXF2_ADDR_HI		0x1554

#define	ALE_RXF_PAGE_SIZE		0x1558

#define	ALE_TPD_CNT			0x155C
#define	TPD_CNT_MASK			0x00003FF
#define	TPD_CNT_SHIFT			0

#define	ALE_RSS_IDT_TABLE0		0x1560

#define	ALE_RSS_IDT_TABLE1		0x1564

#define	ALE_RSS_IDT_TABLE2		0x1568

#define	ALE_RSS_IDT_TABLE3		0x156C

#define	ALE_RSS_HASH_VALUE		0x1570

#define	ALE_RSS_HASH_FLAG		0x1574

#define	ALE_RSS_CPU			0x157C

#define	ALE_TXQ_CFG			0x1580
#define	TXQ_CFG_TPD_BURST_MASK		0x0000000F
#define	TXQ_CFG_ENB			0x00000020
#define	TXQ_CFG_ENHANCED_MODE		0x00000040
#define	TXQ_CFG_TX_FIFO_BURST_MASK	0xFFFF0000
#define	TXQ_CFG_TPD_BURST_SHIFT		0
#define	TXQ_CFG_TPD_BURST_DEFAULT	4
#define	TXQ_CFG_TX_FIFO_BURST_SHIFT	16
#define	TXQ_CFG_TX_FIFO_BURST_DEFAULT	256

#define	ALE_TX_JUMBO_THRESH		0x1584
#define	TX_JUMBO_THRESH_MASK		0x000007FF
#define	TX_JUMBO_THRESH_SHIFT		0
#define	TX_JUMBO_THRESH_UNIT		8
#define	TX_JUMBO_THRESH_UNIT_SHIFT	3

#define	ALE_RXQ_CFG			0x15A0
#define	RXQ_CFG_ALIGN_32		0x00000000
#define	RXQ_CFG_ALIGN_64		0x00000001
#define	RXQ_CFG_ALIGN_128		0x00000002
#define	RXQ_CFG_ALIGN_256		0x00000003
#define	RXQ_CFG_QUEUE1_ENB		0x00000010
#define	RXQ_CFG_QUEUE2_ENB		0x00000020
#define	RXQ_CFG_QUEUE3_ENB		0x00000040
#define	RXQ_CFG_IPV6_CSUM_VERIFY	0x00000080
#define	RXQ_CFG_RSS_HASH_TBL_LEN_MASK	0x0000FF00
#define	RXQ_CFG_RSS_HASH_IPV4		0x00010000
#define	RXQ_CFG_RSS_HASH_IPV4_TCP	0x00020000
#define	RXQ_CFG_RSS_HASH_IPV6		0x00040000
#define	RXQ_CFG_RSS_HASH_IPV6_TCP	0x00080000
#define	RXQ_CFG_RSS_MODE_DIS		0x00000000
#define	RXQ_CFG_RSS_MODE_SQSINT		0x04000000
#define	RXQ_CFG_RSS_MODE_MQUESINT	0x08000000
#define	RXQ_CFG_RSS_MODE_MQUEMINT	0x0C000000
#define	RXQ_CFG_NIP_QUEUE_SEL_TBL	0x10000000
#define	RXQ_CFG_RSS_HASH_ENB		0x20000000
#define	RXQ_CFG_CUT_THROUGH_ENB		0x40000000
#define	RXQ_CFG_ENB			0x80000000
#define	RXQ_CFG_RSS_HASH_TBL_LEN_SHIFT	8

#define	ALE_RX_JUMBO_THRESH		0x15A4	/* 16bits */
#define	RX_JUMBO_THRESH_MASK		0x07FF
#define	RX_JUMBO_LKAH_MASK		0x7800
#define	RX_JUMBO_THRESH_MASK_SHIFT	0
#define	RX_JUMBO_THRESH_UNIT		8
#define	RX_JUMBO_THRESH_UNIT_SHIFT	3
#define	RX_JUMBO_LKAH_SHIFT		11
#define	RX_JUMBO_LKAH_DEFAULT		1

#define	ALE_RX_FIFO_PAUSE_THRESH	0x15A8
#define	RX_FIFO_PAUSE_THRESH_LO_MASK	0x00000FFF
#define	RX_FIFO_PAUSE_THRESH_HI_MASK	0x0FFF0000
#define	RX_FIFO_PAUSE_THRESH_LO_SHIFT	0
#define	RX_FIFO_PAUSE_THRESH_HI_SHIFT	16

#define	ALE_CMB_RXF1			0x15B4

#define	ALE_CMB_RXF2			0x15B8

#define	ALE_CMB_RXF3			0x15BC

#define	ALE_DMA_CFG			0x15C0
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
#define	DMA_CFG_RD_REQ_PRI		0x00000400
#define	DMA_CFG_RD_DELAY_CNT_MASK	0x0000F800
#define	DMA_CFG_WR_DELAY_CNT_MASK	0x000F0000
#define	DMA_CFG_TXCMB_ENB		0x00100000
#define	DMA_CFG_RXCMB_ENB		0x00200000
#define	DMA_CFG_RD_BURST_MASK		0x07
#define	DMA_CFG_RD_BURST_SHIFT		4
#define	DMA_CFG_WR_BURST_MASK		0x07
#define	DMA_CFG_WR_BURST_SHIFT		7
#define	DMA_CFG_RD_DELAY_CNT_SHIFT	11
#define	DMA_CFG_WR_DELAY_CNT_SHIFT	16
#define	DMA_CFG_RD_DELAY_CNT_DEFAULT	15
#define	DMA_CFG_WR_DELAY_CNT_DEFAULT	4

#define	ALE_SMB_STAT_TIMER		0x15C4

#define	ALE_INT_TRIG_THRESH		0x15C8
#define	INT_TRIG_TX_THRESH_MASK		0x0000FFFF
#define	INT_TRIG_RX_THRESH_MASK		0xFFFF0000
#define	INT_TRIG_TX_THRESH_SHIFT	0
#define	INT_TRIG_RX_THRESH_SHIFT	16

#define	ALE_INT_TRIG_TIMER		0x15CC
#define	INT_TRIG_TX_TIMER_MASK		0x0000FFFF
#define	INT_TRIG_RX_TIMER_MASK		0x0000FFFF
#define	INT_TRIG_TX_TIMER_SHIFT		0
#define	INT_TRIG_RX_TIMER_SHIFT		16

#define	ALE_RXF1_PAGE0_ADDR_LO		0x15D0

#define	ALE_RXF1_PAGE1_ADDR_LO		0x15D4

#define	ALE_RXF2_PAGE0_ADDR_LO		0x15D8

#define	ALE_RXF2_PAGE1_ADDR_LO		0x15DC

#define	ALE_RXF3_PAGE0_ADDR_LO		0x15E0

#define	ALE_RXF3_PAGE1_ADDR_LO		0x15E4

#define	ALE_MBOX_TPD_PROD_IDX		0x15F0

#define	ALE_RXF0_PAGE0			0x15F4

#define	ALE_RXF0_PAGE1			0x15F5

#define	ALE_RXF1_PAGE0			0x15F6

#define	ALE_RXF1_PAGE1			0x15F7

#define	ALE_RXF2_PAGE0			0x15F8

#define	ALE_RXF2_PAGE1			0x15F9

#define	ALE_RXF3_PAGE0			0x15FA

#define	ALE_RXF3_PAGE1			0x15FB

#define	RXF_VALID			0x01

#define	ALE_INTR_STATUS			0x1600
#define	INTR_SMB			0x00000001
#define	INTR_TIMER			0x00000002
#define	INTR_MANUAL_TIMER		0x00000004
#define	INTR_RX_FIFO_OFLOW		0x00000008
#define	INTR_RXF0_OFLOW			0x00000010
#define	INTR_RXF1_OFLOW			0x00000020
#define	INTR_RXF2_OFLOW			0x00000040
#define	INTR_RXF3_OFLOW			0x00000080
#define	INTR_TX_FIFO_UNDERRUN		0x00000100
#define	INTR_RX0_PAGE_FULL		0x00000200
#define	INTR_DMA_RD_TO_RST		0x00000400
#define	INTR_DMA_WR_TO_RST		0x00000800
#define	INTR_GPHY			0x00001000
#define	INTR_TX_CREDIT			0x00002000
#define	INTR_GPHY_LOW_PW		0x00004000
#define	INTR_RX_PKT			0x00010000
#define	INTR_TX_PKT			0x00020000
#define	INTR_TX_DMA			0x00040000
#define	INTR_RX_PKT1			0x00080000
#define	INTR_RX_PKT2			0x00100000
#define	INTR_RX_PKT3			0x00200000
#define	INTR_MAC_RX			0x00400000
#define	INTR_MAC_TX			0x00800000
#define	INTR_UNDERRUN			0x01000000
#define	INTR_FRAME_ERROR		0x02000000
#define	INTR_FRAME_OK			0x04000000
#define	INTR_CSUM_ERROR			0x08000000
#define	INTR_PHY_LINK_DOWN		0x10000000
#define	INTR_DIS_INT			0x80000000

/* Interrupt Mask Register */
#define	ALE_INTR_MASK			0x1604

#define	ALE_INTRS						\
	(INTR_DMA_RD_TO_RST | INTR_DMA_WR_TO_RST |		\
	INTR_RX_PKT | INTR_TX_PKT | INTR_RX_FIFO_OFLOW |	\
	INTR_TX_FIFO_UNDERRUN)

/*
 * AR81xx requires register access to get MAC statistics
 * and the format of statistics seems to be the same of L1 .
 */
#define	ALE_RX_MIB_BASE			0x1700

#define	ALE_TX_MIB_BASE			0x1760

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
	uint32_t rx_rrs_errs;
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
} __packed;

#define	ALE_HOST_RXF0_PAGEOFF		0x1800

#define	ALE_TPD_CONS_IDX		0x1804

#define	ALE_HOST_RXF1_PAGEOFF		0x1808

#define	ALE_HOST_RXF2_PAGEOFF		0x180C

#define	ALE_HOST_RXF3_PAGEOFF		0x1810

#define	ALE_RXF0_CMB0_ADDR_LO		0x1820

#define	ALE_RXF0_CMB1_ADDR_LO		0x1824

#define	ALE_RXF1_CMB0_ADDR_LO		0x1828

#define	ALE_RXF1_CMB1_ADDR_LO		0x182C

#define	ALE_RXF2_CMB0_ADDR_LO		0x1830

#define	ALE_RXF2_CMB1_ADDR_LO		0x1834

#define	ALE_RXF3_CMB0_ADDR_LO		0x1838

#define	ALE_RXF3_CMB1_ADDR_LO		0x183C

#define	ALE_TX_CMB_ADDR_LO		0x1840

#define	ALE_SMB_ADDR_LO			0x1844

/*
 * RRS(receive return status) structure.
 *
 * Note:
 * Atheros AR81xx does not support descriptor based DMA on Rx
 * instead it just prepends a Rx status structure prior to a
 * received frame which also resides on the same Rx buffer.
 * This means driver should copy an entire frame from the
 * buffer to new mbuf chain which in turn greatly increases CPU
 * cycles and effectively nullify the advantage of DMA
 * operation of controller. So you should have fast CPU to cope
 * with the copy operation. Implementing flow-controls may help
 * a lot to minimize Rx FIFO overflows but it's not available
 * yet on FreeBSD and hardware doesn't seem to support
 * fine-grained Tx/Rx flow controls.
 */
struct rx_rs {
	uint32_t	seqno;
#define	ALE_RD_SEQNO_MASK		0x0000FFFF
#define	ALE_RD_HASH_MASK		0xFFFF0000
#define	ALE_RD_SEQNO_SHIFT		0
#define	ALE_RD_HASH_SHIFT		16
#define	ALE_RX_SEQNO(x)		\
	(((x) & ALE_RD_SEQNO_MASK) >> ALE_RD_SEQNO_SHIFT)
	uint32_t	length;
#define	ALE_RD_CSUM_MASK		0x0000FFFF
#define	ALE_RD_LEN_MASK			0x3FFF0000
#define	ALE_RD_CPU_MASK			0xC0000000
#define	ALE_RD_CSUM_SHIFT		0
#define	ALE_RD_LEN_SHIFT		16
#define	ALE_RD_CPU_SHIFT		30
#define	ALE_RX_CSUM(x)		\
	(((x) & ALE_RD_CSUM_MASK) >> ALE_RD_CSUM_SHIFT)
#define	ALE_RX_BYTES(x)		\
	(((x) & ALE_RD_LEN_MASK) >> ALE_RD_LEN_SHIFT)
#define	ALE_RX_CPU(x)		\
	(((x) & ALE_RD_CPU_MASK) >> ALE_RD_CPU_SHIFT)
	uint32_t	flags;
#define	ALE_RD_RSS_IPV4			0x00000001
#define	ALE_RD_RSS_IPV4_TCP		0x00000002
#define	ALE_RD_RSS_IPV6			0x00000004
#define	ALE_RD_RSS_IPV6_TCP		0x00000008
#define	ALE_RD_IPV6			0x00000010
#define	ALE_RD_IPV4_FRAG		0x00000020
#define	ALE_RD_IPV4_DF			0x00000040
#define	ALE_RD_802_3			0x00000080
#define	ALE_RD_VLAN			0x00000100
#define	ALE_RD_ERROR			0x00000200
#define	ALE_RD_IPV4			0x00000400
#define	ALE_RD_UDP			0x00000800
#define	ALE_RD_TCP			0x00001000
#define	ALE_RD_BCAST			0x00002000
#define	ALE_RD_MCAST			0x00004000
#define	ALE_RD_PAUSE			0x00008000
#define	ALE_RD_CRC			0x00010000
#define	ALE_RD_CODE			0x00020000
#define	ALE_RD_DRIBBLE			0x00040000
#define	ALE_RD_RUNT			0x00080000
#define	ALE_RD_OFLOW			0x00100000
#define	ALE_RD_TRUNC			0x00200000
#define	ALE_RD_IPCSUM_NOK		0x00400000
#define	ALE_RD_TCP_UDPCSUM_NOK		0x00800000
#define	ALE_RD_LENGTH_NOK		0x01000000
#define	ALE_RD_DES_ADDR_FILTERED	0x02000000
	uint32_t vtags;
#define	ALE_RD_HASH_HI_MASK		0x0000FFFF
#define	ALE_RD_HASH_HI_SHIFT		0
#define	ALE_RD_VLAN_MASK		0xFFFF0000
#define	ALE_RD_VLAN_SHIFT		16
#define	ALE_RX_VLAN(x)		\
	(((x) & ALE_RD_VLAN_MASK) >> ALE_RD_VLAN_SHIFT)
#define	ALE_RX_VLAN_TAG(x)	\
	(((x) >> 4) | (((x) & 7) << 13) | (((x) & 8) << 9))
} __packed;

/* Tx descriptor. */
struct tx_desc {
	uint64_t addr;
	uint32_t len;
#define	ALE_TD_VLAN_MASK		0xFFFF0000
#define	ALE_TD_PKT_INT			0x00008000
#define	ALE_TD_DMA_INT			0x00004000
#define	ALE_TD_BUFLEN_MASK		0x00003FFF
#define	ALE_TD_VLAN_SHIFT		16
#define	ALE_TX_VLAN_TAG(x)	\
	(((x) << 4) | ((x) >> 13) | (((x) >> 9) & 8))
#define	ALE_TD_BUFLEN_SHIFT		0
#define	ALE_TX_BYTES(x)		\
	(((x) << ALE_TD_BUFLEN_SHIFT) & ALE_TD_BUFLEN_MASK)
	uint32_t flags;
#define	ALE_TD_MSS			0xFFF80000
#define	ALE_TD_TSO_HDR			0x00040000
#define	ALE_TD_TCPHDR_LEN		0x0003C000
#define	ALE_TD_IPHDR_LEN		0x00003C00
#define	ALE_TD_IPV6HDR_LEN2		0x00003C00
#define	ALE_TD_LLC_SNAP			0x00000200
#define	ALE_TD_VLAN_TAGGED		0x00000100
#define	ALE_TD_UDPCSUM			0x00000080
#define	ALE_TD_TCPCSUM			0x00000040
#define	ALE_TD_IPCSUM			0x00000020
#define	ALE_TD_IPV6HDR_LEN1		0x000000E0
#define	ALE_TD_TSO			0x00000010
#define	ALE_TD_CXSUM			0x00000008
#define	ALE_TD_INSERT_VLAN_TAG		0x00000004
#define	ALE_TD_IPV6			0x00000002
#define	ALE_TD_EOP			0x00000001

#define	ALE_TD_CSUM_PLOADOFFSET		0x00FF0000
#define	ALE_TD_CSUM_XSUMOFFSET		0xFF000000
#define	ALE_TD_CSUM_XSUMOFFSET_SHIFT	24
#define	ALE_TD_CSUM_PLOADOFFSET_SHIFT	16
#define	ALE_TD_MSS_SHIFT		19
#define	ALE_TD_TCPHDR_LEN_SHIFT		14
#define	ALE_TD_IPHDR_LEN_SHIFT		10
} __packed;

#define	ALE_TX_RING_CNT		256	/* Should be multiple of 4. */
#define	ALE_TX_RING_CNT_MIN	32
#define	ALE_TX_RING_CNT_MAX	1020
#define	ALE_TX_RING_ALIGN	8
#define	ALE_RX_PAGE_ALIGN	32
#define	ALE_RX_PAGES		2
#define	ALE_CMB_ALIGN		32

#define	ALE_TSO_MAXSEGSIZE	4096
#define	ALE_TSO_MAXSIZE		(65535 + sizeof(struct ether_vlan_header))
#define	ALE_MAXTXSEGS		32

#define	ALE_ADDR_LO(x)		((uint64_t) (x) & 0xFFFFFFFF)
#define	ALE_ADDR_HI(x)		((uint64_t) (x) >> 32)

/* Water mark to kick reclaiming Tx buffers. */
#define	ALE_TX_DESC_HIWAT	(ALE_TX_RING_CNT - ((ALE_TX_RING_CNT * 4) / 10))

#define	ALE_MSI_MESSAGES	1
#define	ALE_MSIX_MESSAGES	1

/*
 * TODO : Should get real jumbo MTU size.
 * The hardware seems to have trouble in dealing with large
 * frame length. If you encounter instability issue, use
 * lower MTU size.
 */
#define	ALE_JUMBO_FRAMELEN	8132
#define	ALE_JUMBO_MTU		\
	(ALE_JUMBO_FRAMELEN - sizeof(struct ether_vlan_header) - ETHER_CRC_LEN)
#define	ALE_MAX_FRAMELEN	(ETHER_MAX_LEN + EVL_ENCAPLEN)

#define	ALE_DESC_INC(x, y)	((x) = ((x) + 1) % (y))

struct ale_txdesc {
	struct mbuf		*tx_m;
	bus_dmamap_t		tx_dmamap;
};

struct ale_rx_page {
	bus_dmamap_t		page_map;
	bus_dma_segment_t	page_seg;
	uint8_t			*page_addr;
	bus_addr_t		page_paddr;
	bus_dmamap_t		cmb_map;
	bus_dma_segment_t	cmb_seg;
	uint32_t		*cmb_addr;
	bus_addr_t		cmb_paddr;
	uint32_t		cons;
};

struct ale_chain_data{
	struct ale_txdesc	ale_txdesc[ALE_TX_RING_CNT];
	bus_dmamap_t		ale_tx_ring_map;
	bus_dma_segment_t	ale_tx_ring_seg;
	bus_dmamap_t		ale_rx_mblock_map[ALE_RX_PAGES];
	bus_dma_segment_t	ale_rx_mblock_seg[ALE_RX_PAGES];
	struct tx_desc		*ale_tx_ring;
	bus_addr_t		ale_tx_ring_paddr;
	uint32_t		*ale_tx_cmb;
	bus_addr_t		ale_tx_cmb_paddr;
	bus_dmamap_t		ale_tx_cmb_map;
	bus_dma_segment_t	ale_tx_cmb_seg;

	uint32_t		ale_tx_prod;
	uint32_t		ale_tx_cons;
	int			ale_tx_cnt;
	struct ale_rx_page	ale_rx_page[ALE_RX_PAGES];
	int			ale_rx_curp;
	uint16_t		ale_rx_seqno;
};

#define	ALE_TX_RING_SZ		\
	(sizeof(struct tx_desc) * ALE_TX_RING_CNT)
#define	ALE_RX_PAGE_SZ_MIN	(8 * 1024)
#define	ALE_RX_PAGE_SZ_MAX	(1024 * 1024)
#define	ALE_RX_FRAMES_PAGE	128
#define	ALE_RX_PAGE_SZ		\
	(roundup(ALE_MAX_FRAMELEN, ALE_RX_PAGE_ALIGN) * ALE_RX_FRAMES_PAGE)
#define	ALE_TX_CMB_SZ		(sizeof(uint32_t))
#define	ALE_RX_CMB_SZ		(sizeof(uint32_t))

#define	ALE_PROC_MIN		(ALE_RX_FRAMES_PAGE / 4)
#define	ALE_PROC_MAX		\
	((ALE_RX_PAGE_SZ * ALE_RX_PAGES) / ETHER_MAX_LEN)
#define	ALE_PROC_DEFAULT	(ALE_PROC_MAX / 4)

struct ale_hw_stats {
	/* Rx stats. */
	uint32_t rx_frames;
	uint32_t rx_bcast_frames;
	uint32_t rx_mcast_frames;
	uint32_t rx_pause_frames;
	uint32_t rx_control_frames;
	uint32_t rx_crcerrs;
	uint32_t rx_lenerrs;
	uint64_t rx_bytes;
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
	uint32_t rx_rrs_errs;
	uint32_t rx_alignerrs;
	uint64_t rx_bcast_bytes;
	uint64_t rx_mcast_bytes;
	uint32_t rx_pkts_filtered;
	/* Tx stats. */
	uint32_t tx_frames;
	uint32_t tx_bcast_frames;
	uint32_t tx_mcast_frames;
	uint32_t tx_pause_frames;
	uint32_t tx_excess_defer;
	uint32_t tx_control_frames;
	uint32_t tx_deferred;
	uint64_t tx_bytes;
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
	uint64_t tx_bcast_bytes;
	uint64_t tx_mcast_bytes;
	/* Misc. */
	uint32_t reset_brk_seq;
};

/*
 * Software state per device.
 */
struct ale_softc {
	struct device		sc_dev;
	struct arpcom		sc_arpcom;

	bus_space_tag_t		sc_mem_bt;
	bus_space_handle_t	sc_mem_bh;
	bus_size_t		sc_mem_size;
	bus_dma_tag_t		sc_dmat;
	pci_chipset_tag_t	sc_pct;
	pcitag_t		sc_pcitag;

	void			*sc_irq_handle;

	struct mii_data		sc_miibus;
	int			ale_phyaddr;

	int			ale_rev;
	int			ale_chip_rev;
	uint8_t			ale_eaddr[ETHER_ADDR_LEN];
	uint32_t		ale_dma_rd_burst;
	uint32_t		ale_dma_wr_burst;
	int			ale_flags;
#define	ALE_FLAG_PCIE		0x0001
#define	ALE_FLAG_PCIX		0x0002
#define	ALE_FLAG_MSI		0x0004
#define	ALE_FLAG_MSIX		0x0008
#define	ALE_FLAG_PMCAP		0x0010
#define	ALE_FLAG_FASTETHER	0x0020
#define	ALE_FLAG_JUMBO		0x0040
#define	ALE_FLAG_RXCSUM_BUG	0x0080
#define	ALE_FLAG_TXCSUM_BUG	0x0100
#define	ALE_FLAG_TXCMB_BUG	0x0200
#define	ALE_FLAG_DETACH		0x4000
#define	ALE_FLAG_LINK		0x8000

	struct timeout		ale_tick_ch;
	struct ale_hw_stats	ale_stats;
	struct ale_chain_data	ale_cdata;
	int			ale_int_rx_mod;
	int			ale_int_tx_mod;
	int			ale_max_frame_size;
	int			ale_pagesize;

};

/* Register access macros. */
#define	CSR_WRITE_4(_sc, reg, val)	\
	bus_space_write_4((sc)->sc_mem_bt, (sc)->sc_mem_bh, (reg), (val))
#define	CSR_WRITE_2(_sc, reg, val)	\
	bus_space_write_2((sc)->sc_mem_bt, (sc)->sc_mem_bh, (reg), (val))
#define	CSR_WRITE_1(_sc, reg, val)	\
	bus_space_write_1((sc)->sc_mem_bt, (sc)->sc_mem_bh, (reg), (val))
#define	CSR_READ_2(_sc, reg)		\
	bus_space_read_2((sc)->sc_mem_bt, (sc)->sc_mem_bh, (reg))
#define	CSR_READ_4(_sc, reg)		\
	bus_space_read_4((sc)->sc_mem_bt, (sc)->sc_mem_bh, (reg))

#define	ALE_TX_TIMEOUT		5
#define	ALE_RESET_TIMEOUT	100
#define	ALE_TIMEOUT		1000
#define	ALE_PHY_TIMEOUT		1000

#endif	/* _IF_ALEREG_H */
