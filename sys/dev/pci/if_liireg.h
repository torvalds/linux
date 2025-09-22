/*	$OpenBSD: if_liireg.h,v 1.4 2008/09/01 14:38:31 brad Exp $	*/

/*
 *  Copyright (c) 2007 The NetBSD Foundation.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 *  ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 *  PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 *  BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * PCI configuration space seems to be mapped in the first 0x100 bytes of
 * the register area.
 */

/* SPI Flash Control register */
#define LII_SFC		0x0200
#define 	SFC_STS_NON_RDY		0x00000001
#define 	SFC_STS_WEN		0x00000002
#define 	SFC_STS_WPEN		0x00000080
#define 	SFC_DEV_STS_MASK	0x000000ff
#define 	SFC_DEV_STS_SHIFT	0
#define 	SFC_INS_MASK		0x07
#define 	SFC_INS_SHIFT		8
#define 	SFC_START		0x00000800
#define 	SFC_EN_VPD		0x00002000
#define 	SFC_LDSTART		0x00008000
#define 	SFC_CS_HI_MASK		0x03
#define 	SFC_CS_HI_SHIFT		16
#define 	SFC_CS_HOLD_MASK	0x03
#define 	SFC_CS_HOLD_SHIFT	18
#define 	SFC_CLK_LO_MASK		0x03
#define 	SFC_CLK_LO_SHIFT	20
#define 	SFC_CLK_HI_MASK		0x03
#define 	SFC_CLK_HI_SHIFT	22
#define 	SFC_CS_SETUP_MASK	0x03
#define 	SFC_CS_SETUP_SHIFT	24
#define 	SFC_EROMPGSZ_MASK	0x03
#define 	SFC_EROMPGSZ_SHIFT	26
#define 	SFC_WAIT_READY		0x10000000

/* SPI Flash Address register */
#define LII_SF_ADDR	0x0204

/* SPI Flash Data register */
#define LII_SF_DATA	0x0208

/* SPI Flash Configuration register */
#define LII_SFCF	0x020c
#define 	SFCF_LD_ADDR_MASK	0x00ffffff
#define 	SFCF_LD_ADDR_SHIFT	0
#define 	SFCF_VPD_ADDR_MASK	0x03
#define 	SFCF_VPD_ADDR_SHIFT	24
#define 	SFCF_LD_EXISTS		0x04000000

/* SPI Flash op codes programmation registers */
#define LII_SFOP_PROGRAM	0x0210
#define LII_SFOP_SC_ERASE	0x0211
#define LII_SFOP_CHIP_ERASE	0x0212
#define LII_SFOP_RDID		0x0213
#define LII_SFOP_WREN		0x0214
#define LII_SFOP_RDSR		0x0215
#define LII_SFOP_WRSR		0x0216
#define LII_SFOP_READ		0x0217

/* TWSI Control register, whatever that is */
#define LII_TWSIC	0x0218
#define     TWSIC_LD_OFFSET_MASK        0x000000ff
#define     TWSIC_LD_OFFSET_SHIFT       0
#define     TWSIC_LD_SLV_ADDR_MASK      0x07
#define     TWSIC_LD_SLV_ADDR_SHIFT     8
#define     TWSIC_SW_LDSTART            0x00000800
#define     TWSIC_HW_LDSTART            0x00001000
#define     TWSIC_SMB_SLV_ADDR_MASK     0x7F
#define     TWSIC_SMB_SLV_ADDR_SHIFT    15
#define     TWSIC_LD_EXIST              0x00400000
#define     TWSIC_READ_FREQ_SEL_MASK    0x03
#define     TWSIC_READ_FREQ_SEL_SHIFT   23
#define     TWSIC_FREQ_SEL_100K         0
#define     TWSIC_FREQ_SEL_200K         1
#define     TWSIC_FREQ_SEL_300K         2
#define     TWSIC_FREQ_SEL_400K         3
#define     TWSIC_WRITE_FREQ_SEL_MASK   0x03
#define     TWSIC_WRITE_FREQ_SEL_SHIFT  24

/* PCI-Express Device Misc. Control register? (size unknown) */
#define LII_PCEDMC	0x021c
#define 	PCEDMC_RETRY_BUFDIS	0x01
#define 	PCEDMC_EXT_PIPE		0x02
#define 	PCEDMC_SPIROM_EXISTS	0x04
#define 	PCEDMC_SERDES_ENDIAN	0x08
#define 	PCEDMC_SERDES_SEL_DIN	0x10

/* PCI-Express PHY Miscellaneous register (size unknown) */
#define LII_PCEPM	0x1000
#define 	PCEPM_FORCE_RCV_DET	0x04

/* Selene Master Control register */
#define LII_SMC		0x1400
#define 	SMC_SOFT_RST		0x00000001
#define 	SMC_MTIMER_EN		0x00000002
#define 	SMC_ITIMER_EN		0x00000004
#define 	SMC_MANUAL_INT		0x00000008
#define 	SMC_REV_NUM_MASK	0xff
#define 	SMC_REV_NUM_SHIFT	16
#define 	SMC_DEV_ID_MASK		0xff
#define 	SMC_DEV_ID_SHIFT	24

/* Timer Initial Value register */
#define LII_TIV		0x1404

/* IRQ Moderator Timer Initial Value register */
#define LII_IMTIV	0x1408

/* PHY Control register */
#define LII_PHYC	0x140c
#define 	PHYC_ENABLE	0x0001

/* IRQ Anti-Lost Timer Initial Value register
    --> Time allowed for software to clear the interrupt */
#define LII_IALTIV	0x140e

/* Block Idle Status register
   --> Bit set if matching state machine is not idle */
#define LII_BIS		0x1410
#define 	BIS_RXMAC	0x00000001
#define		BIS_TXMAC	0x00000002
#define 	BIS_DMAR	0x00000004
#define 	BIS_DMAW	0x00000008

/* MDIO Control register */
#define LII_MDIOC	0x1414
#define 	MDIOC_DATA_MASK		0x0000ffff
#define 	MDIOC_DATA_SHIFT	0
#define 	MDIOC_REG_MASK		0x1f
#define 	MDIOC_REG_SHIFT		16
#define 	MDIOC_WRITE		0x00000000
#define 	MDIOC_READ		0x00200000
#define 	MDIOC_SUP_PREAMBLE	0x00400000
#define 	MDIOC_START		0x00800000
#define 	MDIOC_CLK_SEL_MASK	0x07
#define 	MDIOC_CLK_SEL_SHIFT	24
#define 	MDIOC_CLK_25_4		0
#define 	MDIOC_CLK_25_6		2
#define 	MDIOC_CLK_25_8		3
#define 	MDIOC_CLK_25_10		4
#define 	MDIOC_CLK_25_14		5
#define 	MDIOC_CLK_25_20		6
#define 	MDIOC_CLK_25_28		7
#define 	MDIOC_BUSY		0x08000000
/* Time to wait for MDIO, waiting for 2us in-between */
#define 	MDIO_WAIT_TIMES		10

/* SerDes Lock Detect Control and Status register */
#define LII_SERDES	0x1424
#define 	SERDES_LOCK_DETECT	0x01
#define 	SERDES_LOCK_DETECT_EN	0x02

/* MAC Control register */
#define LII_MACC	0x1480
#define 	MACC_TX_EN		0x00000001
#define 	MACC_RX_EN		0x00000002
#define 	MACC_TX_FLOW_EN		0x00000004
#define 	MACC_RX_FLOW_EN		0x00000008
#define 	MACC_LOOPBACK		0x00000010
#define 	MACC_FDX		0x00000020
#define 	MACC_ADD_CRC		0x00000040
#define 	MACC_PAD		0x00000080
#define 	MACC_PREAMBLE_LEN_MASK	0x0f
#define 	MACC_PREAMBLE_LEN_SHIFT	10
#define 	MACC_STRIP_VLAN		0x00004000
#define 	MACC_PROMISC_EN		0x00008000
#define 	MACC_DBG_TX_BKPRESSURE	0x00100000
#define 	MACC_ALLMULTI_EN	0x02000000
#define 	MACC_BCAST_EN		0x04000000
#define 	MACC_MACLP_CLK_PHY	0x08000000
#define 	MACC_HDX_LEFT_BUF_MASK	0x0f
#define 	MACC_HDX_LEFT_BUF_SHIFT	28

/* MAC IPG/IFG Control register */
#define LII_MIPFG	0x1484
#define 	MIPFG_IPGT_MASK		0x0000007f
#define 	MIPFG_IPGT_SHIFT	0
#define 	MIPFG_MIFG_MASK		0xff
#define 	MIPFG_MIFG_SHIFT	8
#define 	MIPFG_IPGR1_MASK	0x7f
#define 	MIPFG_IPGR1_SHIFT	16
#define 	MIPFG_IPGR2_MASK	0x7f
#define 	MIPFG_IPGR2_SHIFT	24

/* MAC Address registers */
#define LII_MAC_ADDR_0	0x1488
#define LII_MAC_ADDR_1	0x148c

/* Multicast Hash Table register */
#define LII_MHT		0x1490

/* MAC Half-Duplex Control register */
#define LII_MHDC	0x1498
#define 	MHDC_LCOL_MASK		0x000003ff
#define 	MHDC_LCOL_SHIFT		0
#define 	MHDC_RETRY_MASK		0x0f
#define 	MHDC_RETRY_SHIFT	12
#define 	MHDC_EXC_DEF_EN		0x00010000
#define 	MHDC_NO_BACK_C		0x00020000
#define 	MHDC_NO_BACK_P		0x00040000
#define 	MHDC_ABEDE		0x00080000
#define 	MHDC_ABEBT_MASK		0x0f
#define 	MHDC_ABEBT_SHIFT	20
#define 	MHDC_JAMIPG_MASK	0x0f
#define 	MHDC_JAMIPG_SHIFT	24

/* MTU Control register */
#define LII_MTU		0x149c

/* WOL Control register */
#define LII_WOLC
#define 	WOLC_PATTERN_EN		0x00000001
#define 	WOLC_PATTERN_PME_EN	0x00000002
#define 	WOLC_MAGIC_EN		0x00000004
#define 	WOLC_MAGIC_PME_EN	0x00000008
#define 	WOLC_LINK_CHG_EN	0x00000010
#define 	WOLC_LINK_CHG_PME_EN	0x00000020
#define 	WOLC_PATTERN_ST		0x00000100
#define 	WOLC_MAGIC_ST		0x00000200
#define 	WOLC_LINK_CHG_ST	0x00000400
#define 	WOLC_PT0_EN		0x00010000
#define 	WOLC_PT1_EN		0x00020000
#define 	WOLC_PT2_EN		0x00040000
#define 	WOLC_PT3_EN		0x00080000
#define 	WOLC_PT4_EN		0x00100000
#define 	WOLC_PT0_MATCH		0x01000000
#define 	WOLC_PT1_MATCH		0x02000000
#define 	WOLC_PT2_MATCH		0x04000000
#define 	WOLC_PT3_MATCH		0x08000000
#define 	WOLC_PT4_MATCH		0x10000000

/* Internal SRAM Partition register */
#define LII_SRAM_TXRAM_END	0x1500
#define LII_SRAM_RXRAM_END	0x1502

/* Descriptor Control registers */
#define LII_DESC_BASE_ADDR_HI	0x1540
#define LII_TXD_BASE_ADDR_LO	0x1544
#define LII_TXD_BUFFER_SIZE	0x1548
#define LII_TXS_BASE_ADDR_LO	0x154c
#define LII_TXS_NUM_ENTRIES	0x1550
#define LII_RXD_BASE_ADDR_LO	0x1554
#define LII_RXD_NUM_ENTRIES	0x1558

/* DMAR Control register */
#define LII_DMAR	0x1580
#define 	DMAR_EN		0x01

/* TX Cur-Through Control register */
#define LII_TX_CUT_THRESH	0x1590

/* DMAW Control register */
#define LII_DMAW	0x15a0
#define 	DMAW_EN		0x01

/* Flow Control registers */
#define LII_PAUSE_ON_TH		0x15a8
#define LII_PAUSE_OFF_TH	0x15aa

/* Mailbox registers */
#define LII_MB_TXD_WR_IDX	0x15f0
#define LII_MB_RXD_RD_IDX	0x15f4

/* Interrupt Status register */
#define LII_ISR		0x1600
#define 	ISR_TIMER		0x00000001
#define 	ISR_MANUAL		0x00000002
#define 	ISR_RXF_OV		0x00000004
#define 	ISR_TXF_UR		0x00000008
#define 	ISR_TXS_OV		0x00000010
#define 	ISR_RXS_OV		0x00000020
#define 	ISR_LINK_CHG		0x00000040
#define 	ISR_HOST_TXD_UR		0x00000080
#define 	ISR_HOST_RXD_OV		0x00000100
#define 	ISR_DMAR_TO_RST		0x00000200
#define 	ISR_DMAW_TO_RST		0x00000400
#define 	ISR_PHY			0x00000800
#define 	ISR_TS_UPDATE		0x00010000
#define 	ISR_RS_UPDATE		0x00020000
#define 	ISR_TX_EARLY		0x00040000
#define 	ISR_UR_DETECTED		0x01000000
#define 	ISR_FERR_DETECTED	0x02000000
#define 	ISR_NFERR_DETECTED	0x04000000
#define 	ISR_CERR_DETECTED	0x08000000
#define 	ISR_PHY_LINKDOWN	0x10000000
#define 	ISR_DIS_INT		0x80000000

#define 	ISR_TX_EVENT		(ISR_TXF_UR | ISR_TXS_OV | \
					 ISR_HOST_TXD_UR | ISR_TS_UPDATE | \
					 ISR_TX_EARLY)
#define 	ISR_RX_EVENT		(ISR_RXF_OV | ISR_RXS_OV | \
					 ISR_HOST_RXD_OV | ISR_RS_UPDATE)

/* Interrupt Mask register */
#define LII_IMR		0x1604
#define 	IMR_NORMAL_MASK		(ISR_DMAR_TO_RST | ISR_DMAW_TO_RST | \
					 ISR_PHY | ISR_PHY_LINKDOWN | \
					 ISR_TS_UPDATE | ISR_RS_UPDATE)

/* MAC RX Statistics registers */
#define LII_STS_RX_PAUSE	0x1700
#define LII_STS_RXD_OV		0x1704
#define LII_STS_RXS_OV		0x1708
#define LII_STS_RX_FILTER	0x170c

struct tx_pkt_header {
	uint16_t	txph_size;
#define LII_TXH_ADD_VLAN_TAG	0x8000
	uint16_t	txph_vlan;
} __packed;

struct tx_pkt_status {
	uint16_t	txps_size;
	uint16_t	txps_flags :15;
#define LII_TXF_SUCCESS		0x0001
#define LII_TXF_BCAST		0x0002
#define LII_TXF_MCAST		0x0004
#define LII_TXF_PAUSE		0x0008
#define LII_TXF_CTRL		0x0010
#define LII_TXF_DEFER		0x0020
#define LII_TXF_EXC_DEFER	0x0040
#define LII_TXF_SINGLE_COL	0x0080
#define LII_TXF_MULTI_COL	0x0100
#define LII_TXF_LATE_COL	0x0200
#define LII_TXF_ABORT_COL	0x0400
#define LII_TXF_UNDERRUN	0x0800
	uint16_t	txps_update:1;
} __packed;

struct rx_pkt {
	uint16_t	rxp_size;
	uint16_t	rxp_flags :15;
#define LII_RXF_SUCCESS		0x0001
#define LII_RXF_BCAST		0x0002
#define LII_RXF_MCAST		0x0004
#define LII_RXF_PAUSE		0x0008
#define LII_RXF_CTRL		0x0010
#define LII_RXF_CRC		0x0020
#define LII_RXF_CODE		0x0040
#define LII_RXF_RUNT		0x0080
#define LII_RXF_FRAG		0x0100
#define LII_RXF_TRUNC		0x0200
#define LII_RXF_ALIGN		0x0400
#define LII_RXF_VLAN		0x0800
	uint16_t	rxp_update:1;
	uint16_t	rxp_vlan;
	uint16_t	__pad;
	uint8_t		rxp_data[1528];
} __packed;
