/*	$OpenBSD: if_ngbereg.h,v 1.2 2024/09/01 03:08:59 jsg Exp $	*/

/*
 * Copyright (c) 2015-2017 Beijing WangXun Technology Co., Ltd.
 * Copyright (c) 2023 Kevin Lo <kevlo@openbsd.org>
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

#define NGBE_PCIREG		PCI_MAPREG_START	/* BAR 0 */
#define	NGBE_MAX_VECTORS	8
#define NGBE_SP_MAX_TX_QUEUES	8
#define NGBE_SP_MAX_RX_QUEUES	8
#define NGBE_SP_RAR_ENTRIES	32
#define NGBE_SP_MC_TBL_SIZE	128
#define NGBE_SP_VFT_TBL_SIZE	128
#define NGBE_SP_RX_PB_SIZE	42

#define NGBE_PSR_VLAN_SWC_ENTRIES	32
#define NGBE_MAX_MTA			128
#define NGBE_MAX_VFTA_ENTRIES		128
#define NGBE_MAX_INTS_PER_SEC		8000
#define NGBE_MAX_JUMBO_FRAME_SIZE	9432

#define NGBE_TX_TIMEOUT			5
#define NGBE_LINK_UP_TIME		90
#define NGBE_MAX_FLASH_LOAD_POLL_TIME	10

/* Additional bittime to account for NGBE framing */
#define NGBE_ETH_FRAMING	20

/* Tx/Rx descriptor defines */
#define NGBE_DEFAULT_TXD	512
#define NGBE_DEFAULT_RXD	512

/* Flow control */
#define NGBE_DEFAULT_FCPAUSE	0xffff

/* Flow control defines */
#define NGBE_TAF_SYM_PAUSE	0x1
#define NGBE_TAF_ASM_PAUSE	0x2

/* Interrupt Registers */
#define NGBE_PX_MISC_IC			0x00100
#define NGBE_PX_MISC_IEN		0x00108
#define NGBE_PX_GPIE			0x00118
#define NGBE_PX_IC			0x00120
#define NGBE_PX_IMS			0x00140
#define NGBE_PX_IMC			0x00150
#define NGBE_PX_ISB_ADDR_L		0x00160
#define NGBE_PX_ISB_ADDR_H		0x00164
#define NGBE_PX_TRANSACTION_PENDING	0x00168
#define NGBE_PX_ITRSEL			0x00180
#define NGBE_PX_ITR(_i)			(0x00200 + (_i) * 4)
#define NGBE_PX_MISC_IVAR		0x004fc
#define NGBE_PX_IVAR(_i)		(0x00500 + (_i) * 4)

/* Receive DMA Registers */
#define NGBE_PX_RR_BAL(_i)		(0x01000 + ((_i) * 0x40))
#define NGBE_PX_RR_BAH(_i)		(0x01004 + ((_i) * 0x40))
#define NGBE_PX_RR_WP(_i)		(0x01008 + ((_i) * 0x40))
#define NGBE_PX_RR_RP(_i)		(0x0100c + ((_i) * 0x40))
#define NGBE_PX_RR_CFG(_i)		(0x01010 + ((_i) * 0x40))

/* Statistic */
#define NGBE_PX_MPRC(_i)		(0x01020 + ((_i) * 64))

/* Transmit DMA Registers */
#define NGBE_PX_TR_BAL(_i)		(0x03000 + ((_i) * 0x40))
#define NGBE_PX_TR_BAH(_i)		(0x03004 + ((_i) * 0x40))
#define NGBE_PX_TR_WP(_i)		(0x03008 + ((_i) * 0x40))
#define NGBE_PX_TR_RP(_i)		(0x0300c + ((_i) * 0x40))
#define NGBE_PX_TR_CFG(_i)		(0x03010 + ((_i) * 0x40))

/* Chip Control Registers */
#define NGBE_MIS_PWR			0x10000
#define NGBE_MIS_RST			0x1000c
#define NGBE_MIS_PRB_CTL		0x10010
#define NGBE_MIS_ST			0x10028
#define NGBE_MIS_SWSM			0x1002c
#define NGBE_MIS_RST_ST			0x10030

/* FMGR Registers */
#define NGBE_SPI_CMD			0x10104
#define NGBE_SPI_DATA			0x10108
#define NGBE_SPI_STATUS			0x1010c
#define NGBE_SPI_ILDR_STATUS		0x10120

/* Checksum and EEPROM Registers */
#define NGBE_CALSUM_CAP_STATUS		0x10224
#define NGBE_EEPROM_VERSION_STORE_REG	0x1022c

/* Sensors for PVT(Process Voltage Temperature) */
#define NGBE_TS_EN			0x10304
#define NGBE_TS_ALARM_THRE		0x1030c
#define NGBE_TS_DALARM_THRE		0x10310
#define NGBE_TS_INT_EN			0x10314
#define NGBE_TS_ALARM_ST		0x10318

/* MAC Registers */
#define NGBE_MAC_TX_CFG			0x11000
#define NGBE_MAC_RX_CFG			0x11004
#define NGBE_MAC_PKT_FLT		0x11008
#define NGBE_MAC_WDG_TIMEOUT		0x1100c
#define NGBE_MAC_RX_FLOW_CTRL		0x11090

/* Media-dependent registers. */
#define NGBE_MDIO_CLAUSE_SELECT		0x11220

/* Statistic */
#define NGBE_MMC_CONTROL		0x11800
#define NGBE_TX_FRAME_CNT_GOOD_BAD_LOW	0x1181c
#define NGBE_TX_BC_FRAMES_GOOD_LOW	0x11824
#define NGBE_TX_MC_FRAMES_GOOD_LOW	0x1182c
#define NGBE_RX_FRAME_CNT_GOOD_BAD_LOW	0x11900
#define NGBE_RX_BC_FRAMES_GOOD_LOW	0x11918
#define NGBE_RX_CRC_ERROR_FRAMES_LOW	0x11928
#define NGBE_RX_UNDERSIZE_FRAMES_GOOD	0x11938
#define NGBE_RX_OVERSIZE_FRAMES_GOOD	0x1193c
#define NGBE_RX_LEN_ERROR_FRAMES_LOW	0x11978
#define NGBE_MAC_LXOFFRXC		0x11988
#define NGBE_MAC_PXOFFRXC		0x119dc

/* Interrupt Registers */
#define NGBE_BME_CTL			0x12020

/* Statistic */
#define NGBE_RDM_DRP_PKT		0x12500
#define NGBE_PX_GPRC			0x12504
#define NGBE_PX_GORC_MSB		0x1250c

/* Internal phy reg_offset [0,31] */
#define NGBE_PHY_CONFIG(offset)		(0x14000 + ((offset) * 4))

/* Port cfg Registers */
#define NGBE_CFG_PORT_CTL		0x14400
#define NGBE_CFG_PORT_ST		0x14404
#define NGBE_CFG_LAN_SPEED		0x14440

/* GPIO Registers */
#define NGBE_GPIO_DDR			0x14804
#define NGBE_GPIO_INTEN			0x14830
#define NGBE_GPIO_INTTYPE_LEVEL		0x14838
#define NGBE_GPIO_POLARITY		0x1483c
#define NGBE_GPIO_INTSTATUS		0x14840
#define NGBE_GPIO_EOI			0x1484c

/* PSR Control Registers */
#define NGBE_PSR_CTL			0x15000
#define NGBE_PSR_MAX_SZ			0x15020
#define NGBE_PSR_VLAN_CTL		0x15088

/* mcast/ucast overflow tbl */
#define NGBE_PSR_MC_TBL(_i)		(0x15200 + ((_i) * 4))
#define NGBE_PSR_UC_TBL(_i)		(0x15400 + ((_i) * 4))

/* Management Registers */
#define NGBE_PSR_MNG_FLEX_SEL		0x1582c
#define NGBE_PSR_MNG_FLEX_DW_L(_i)	(0x15a00 + ((_i) * 16))
#define NGBE_PSR_MNG_FLEX_DW_H(_i)	(0x15a04 + ((_i) * 16))
#define NGBE_PSR_MNG_FLEX_MSK(_i)	(0x15a08 + ((_i) * 16))

/* Wake Up Registers */
#define NGBE_PSR_LAN_FLEX_SEL		0x15b8c
#define NGBE_PSR_LAN_FLEX_DW_L(_i)	(0x15c00 + ((_i) * 16))
#define NGBE_PSR_LAN_FLEX_DW_H(_i)	(0x15c04 + ((_i) * 16))
#define NGBE_PSR_LAN_FLEX_MSK(_i)	(0x15c08 + ((_i) * 16))

/* VLAN tbl */
#define NGBE_PSR_VLAN_TBL(_i)		(0x16000 + ((_i) * 4))

/* MAC switcher */
#define NGBE_PSR_MAC_SWC_AD_L		0x16200
#define NGBE_PSR_MAC_SWC_AD_H		0x16204
#define NGBE_PSR_MAC_SWC_VM		0x16208
#define NGBE_PSR_MAC_SWC_IDX		0x16210

/* VLAN switch */
#define NGBE_PSR_VLAN_SWC		0x16220
#define NGBE_PSR_VLAN_SWC_VM_L		0x16224
#define NGBE_PSR_VLAN_SWC_IDX		0x16230

/* RSEC Registers */
#define NGBE_RSEC_CTL			0x17000
#define NGBE_RSEC_ST			0x17004

/* Transmit Global Control Registers */
#define NGBE_TDM_CTL			0x18000
#define NGBE_TDM_PB_THRE		0x18020

/* Statistic */
#define NGBE_PX_GPTC			0x18308
#define NGBE_PX_GOTC_MSB		0x18310

/* Receive packet buffer */
#define NGBE_RDB_PB_CTL			0x19000
#define NGBE_RDB_PB_SZ			0x19020
#define NGBE_RDB_RFCV			0x19200

/* Statistic */
#define NGBE_RDB_PFCMACDAL		0x19210
#define NGBE_RDB_PFCMACDAH		0x19214
#define NGBE_RDB_LXONTXC		0x1921c
#define NGBE_RDB_LXOFFTXC		0x19218

/* Flow Control Registers */
#define NGBE_RDB_RFCL			0x19220
#define NGBE_RDB_RFCH			0x19260
#define NGBE_RDB_RFCRT			0x192a0
#define NGBE_RDB_RFCC			0x192a4

/* Ring Assignment */
#define NGBE_RDB_PL_CFG(_i)		(0x19300 + ((_i) * 4))
#define NGBE_RDB_RSSTBL(_i)		(0x19400 + ((_i) * 4))
#define NGBE_RDB_RSSRK(_i)		(0x19480 + ((_i) * 4))
#define NGBE_RDB_RA_CTL			0x194f4

/* TDB */
#define NGBE_TDB_PB_SZ			0x1cc00

/* Security Control Registers */
#define NGBE_TSEC_CTL			0x1d000
#define NGBE_TSEC_BUF_AE		0x1d00c

/* MNG */
#define NGBE_MNG_SWFW_SYNC		0x1e008
#define NGBE_MNG_MBOX			0x1e100
#define NGBE_MNG_MBOX_CTL		0x1e044

/* Bits in NGBE_PX_MISC_IC register */
#define NGBE_PX_MISC_IC_PHY		0x00040000
#define NGBE_PX_MISC_IC_GPIO		0x04000000

/* Bits in NGBE_PX_MISC_IEN register */
#define NGBE_PX_MISC_IEN_ETH_LKDN	0x00000100
#define NGBE_PX_MISC_IEN_DEV_RST	0x00000400
#define NGBE_PX_MISC_IEN_STALL		0x00001000
#define NGBE_PX_MISC_IEN_ETH_EVENT	0x00020000
#define NGBE_PX_MISC_IEN_ETH_LK		0x00040000
#define NGBE_PX_MISC_IEN_ETH_AN		0x00080000
#define NGBE_PX_MISC_IEN_INT_ERR	0x00100000
#define NGBE_PX_MISC_IEN_VF_MBOX	0x00800000
#define NGBE_PX_MISC_IEN_GPIO		0x04000000
#define NGBE_PX_MISC_IEN_PCIE_REQ_ERR	0x08000000
#define NGBE_PX_MISC_IEN_OVER_HEAT	0x10000000
#define NGBE_PX_MISC_IEN_MNG_HOST_MBOX	0x40000000
#define NGBE_PX_MISC_IEN_TIMER		0x80000000

#define NGBE_PX_MISC_IEN_MASK						\
	(NGBE_PX_MISC_IEN_ETH_LKDN | NGBE_PX_MISC_IEN_DEV_RST |		\
	NGBE_PX_MISC_IEN_STALL | NGBE_PX_MISC_IEN_ETH_EVENT |		\
	NGBE_PX_MISC_IEN_ETH_LK | NGBE_PX_MISC_IEN_ETH_AN |		\
	NGBE_PX_MISC_IEN_INT_ERR | NGBE_PX_MISC_IEN_VF_MBOX |		\
	NGBE_PX_MISC_IEN_GPIO | NGBE_PX_MISC_IEN_PCIE_REQ_ERR |		\
	NGBE_PX_MISC_IEN_MNG_HOST_MBOX | NGBE_PX_MISC_IEN_TIMER)

/* Bits in NGBE_PX_GPIE register */
#define NGBE_PX_GPIE_MODEL	0x00000001

/* Bits in NGBE_PX_ITR register */
#define NGBE_MAX_EITR		0x00007ffc
#define NGBE_PX_ITR_CNT_WDIS	0x80000000

/* Bits in NGBE_PX_IVAR register */
#define NGBE_PX_IVAR_ALLOC_VAL	0x80

/* Bits in NGBE_PX_RR_CFG register */
#define NGBE_PX_RR_CFG_RR_EN		0x00000001
#define NGBE_PX_RR_CFG_SPLIT_MODE	0x04000000
#define NGBE_PX_RR_CFG_DROP_EN		0x40000000
#define NGBE_PX_RR_CFG_VLAN		0x80000000

#define NGBE_PX_RR_CFG_RR_BUF_SZ	0x00000f00
#define NGBE_PX_RR_CFG_RR_HDR_SZ	0x0000f000
#define NGBE_PX_RR_CFG_RR_SIZE_SHIFT	1
#define NGBE_PX_RR_CFG_BSIZEPKT_SHIFT	2
#define NGBE_PX_RR_CFG_RR_THER_SHIFT	16

/* Bits in NGBE_PX_TR_CFG register */
#define NGBE_PX_TR_CFG_ENABLE		(1)
#define NGBE_PX_TR_CFG_SWFLSH		0x04000000

#define NGBE_PX_TR_CFG_TR_SIZE_SHIFT	1
#define NGBE_PX_TR_CFG_WTHRESH_SHIFT	16

/* Bits in NGBE_MIS_RST register */
#define NGBE_MIS_RST_SW_RST	0x00000001
#define NGBE_MIS_RST_LAN0_RST	0x00000002
#define NGBE_MIS_RST_LAN1_RST	0x00000004
#define NGBE_MIS_RST_LAN2_RST	0x00000008
#define NGBE_MIS_RST_LAN3_RST	0x00000010

/* Bits in NGBE_MIS_PRB_CTL register */
#define NGBE_MIS_PRB_CTL_LAN3_UP	0x1
#define NGBE_MIS_PRB_CTL_LAN2_UP	0x2
#define NGBE_MIS_PRB_CTL_LAN1_UP	0x4
#define NGBE_MIS_PRB_CTL_LAN0_UP	0x8

/* Bits in NGBE_MIS_ST register */
#define NGBE_MIS_ST_MNG_INIT_DN		0x00000001
#define NGBE_MIS_ST_MNG_VETO		0x00000100
#define NGBE_MIS_ST_LAN0_ECC		0x00010000
#define NGBE_MIS_ST_LAN1_ECC		0x00020000
#define NGBE_MIS_ST_GPHY_IN_RST(_r)	(0x00000200 << (_r))

/* Bits in NGBE_MIS_SWSM register */
#define NGBE_MIS_SWSM_SMBI	1

/* Bits in NGBE_MIS_RST_ST register */
#define NGBE_MIS_RST_ST_RST_INIT	0x0000ff00

#define NGBE_MIS_RST_ST_RST_INI_SHIFT	8
#define NGBE_MIS_RST_ST_DEV_RST_ST_MASK	0x00180000

/* Bits in NGBE_SPI_STATUS register */
#define NGBE_SPI_STATUS_FLASH_BYPASS	0x80000000

/* Bits in NGBE_SPI_ILDR_STATUS register */
#define NGBE_SPI_ILDR_STATUS_PERST	0x00000001
#define NGBE_SPI_ILDR_STATUS_PWRRST	0x00000002
#define NGBE_SPI_ILDR_STATUS_SW_RESET	0x00000800

/* Bits in NGBE_TS_EN register */
#define NGBE_TS_EN_ENA	0x00000001

/* Bits in NGBE_TS_INT_EN register */
#define NGBE_TS_INT_EN_ALARM_INT_EN	0x00000001
#define NGBE_TS_INT_EN_DALARM_INT_EN	0x00000002

/* Bits in NGBE_TS_ALARM_ST register */
#define NGBE_TS_ALARM_ST_DALARM	0x00000002
#define NGBE_TS_ALARM_ST_ALARM	0x00000001

/* Bits in NGBE_MAC_TX_CFG register */
#define NGBE_MAC_TX_CFG_TE		0x00000001
#define NGBE_MAC_TX_CFG_SPEED_MASK	0x60000000
#define NGBE_MAC_TX_CFG_SPEED_1G	0x60000000

/* Bits in NGBE_MAC_RX_CFG register */
#define NGBE_MAC_RX_CFG_RE	0x00000001
#define NGBE_MAC_RX_CFG_JE	0x00000100

/* Bits in NGBE_MAC_PKT_FLT register */
#define NGBE_MAC_PKT_FLT_PR	0x00000001
#define NGBE_MAC_PKT_FLT_RA	0x80000000

/* Bits in NGBE_MAC_RX_FLOW_CTRL register */
#define NGBE_MAC_RX_FLOW_CTRL_RFE	0x00000001

/* Bits in NGBE_MMC_CONTROL register */
#define NGBE_MMC_CONTROL_RSTONRD	0x4
#define NGBE_MMC_CONTROL_UP		0x700

/* Bits in NGBE_CFG_PORT_CTL register */
#define NGBE_CFG_PORT_CTL_DRV_LOAD	0x00000008
#define NGBE_CFG_PORT_CTL_PFRSTD	0x00004000

/* Bits in NGBE_CFG_PORT_ST register */
#define NGBE_CFG_PORT_ST_LAN_ID(_r)	((0x00000300 & (_r)) >> 8)

/* Bits in NGBE_PSR_CTL register */
#define NGBE_PSR_CTL_MO		0x00000060
#define NGBE_PSR_CTL_MFE	0x00000080
#define NGBE_PSR_CTL_MPE	0x00000100
#define NGBE_PSR_CTL_UPE	0x00000200
#define NGBE_PSR_CTL_BAM	0x00000400
#define NGBE_PSR_CTL_PCSD	0x00002000
#define NGBE_PSR_CTL_SW_EN	0x00040000

#define NGBE_PSR_CTL_MO_SHIFT	5

/* Bits in NGBE_PSR_VLAN_CTL register */
#define NGBE_PSR_VLAN_CTL_CFIEN	0x20000000
#define NGBE_PSR_VLAN_CTL_VFE	0x40000000

/* Bits in NGBE_PSR_MAC_SWC_AD_H register */
#define NGBE_PSR_MAC_SWC_AD_H_AV	0x80000000

#define NGBE_PSR_MAC_SWC_AD_H_AD(v)	(((v) & 0xffff))
#define NGBE_PSR_MAC_SWC_AD_H_ADTYPE(v)	(((v) & 0x1) << 30)

/* Bits in NGBE_RSEC_CTL register */
#define NGBE_RSEC_CTL_RX_DIS	0x00000002
#define NGBE_RSEC_CTL_CRC_STRIP	0x00000004

/* Bits in NGBE_RSEC_ST register */
#define NGBE_RSEC_ST_RSEC_RDY	0x00000001

/* Bits in NGBE_TDM_CTL register */
#define NGBE_TDM_CTL_TE	0x1

/* Bits in NGBE_TDM_PB_THRE register */
#define NGBE_TXPKT_SIZE_MAX	0xa

/* Bits in NGBE_RDB_PB_CTL register */
#define NGBE_RDB_PB_CTL_PBEN	0x80000000

#define NGBE_RDB_PB_SZ_SHIFT	10

/* Bits in NGBE_RDB_RFCC register */
#define NGBE_RDB_RFCC_RFCE_802_3X	0x00000008

/* Bits in RFCL register */
#define NGBE_RDB_RFCL_XONE	0x80000000

/* Bits in RFCH register */
#define NGBE_RDB_RFCH_XOFFE	0x80000000

/* Bits in NGBE_RDB_PL_CFG register */
#define NGBE_RDB_PL_CFG_L4HDR		0x2
#define NGBE_RDB_PL_CFG_L3HDR		0x4
#define NGBE_RDB_PL_CFG_L2HDR		0x8
#define NGBE_RDB_PL_CFG_TUN_TUNHDR	0x10
#define NGBE_RDB_PL_CFG_TUN_OUTER_L2HDR	0x20

/* Bits in NGBE_RDB_RA_CTL register */
#define NGBE_RDB_RA_CTL_RSS_EN		0x00000004
#define NGBE_RDB_RA_CTL_RSS_IPV4_TCP	0x00010000
#define NGBE_RDB_RA_CTL_RSS_IPV4	0x00020000
#define NGBE_RDB_RA_CTL_RSS_IPV6	0x00100000
#define NGBE_RDB_RA_CTL_RSS_IPV6_TCP	0x00200000

/* Bits in NGBE_TDB_PB register */
#define NGBE_TDB_PB_SZ_MAX	0x00005000

/* NGBE_MNG_SWFW_SYNC definitions */
#define NGBE_MNG_SWFW_SYNC_SW_PHY	0x0001
#define NGBE_MNG_SWFW_SYNC_SW_MB	0x0004

/* Bits in NGBE_MNG_MBOX_CTL register */
#define NGBE_MNG_MBOX_CTL_SWRDY	0x1
#define NGBE_MNG_MBOX_CTL_FWRDY	0x4

#define NGBE_CHECKSUM_CAP_ST_PASS	0x80658383
#define NGBE_CHECKSUM_CAP_ST_FAIL	0x70657376

#define NGBE_CALSUM_COMMAND	0xe9

#define RGMII_FPGA	0x0080
#define OEM_MASK	0x00ff

/* PHY register definitions */
#define NGBE_MDIO_AUTO_NEG_STATUS	0x1a
#define NGBE_MDIO_AUTO_NEG_LSC		0x1d

/* Internal PHY control */
#define NGBE_INTERNAL_PHY_PAGE_OFFSET		0xa43
#define NGBE_INTERNAL_PHY_PAGE_SELECT_OFFSET	31
#define NGBE_INTERNAL_PHY_ID			0x000732

#define NGBE_INTPHY_INT_ANC	0x0008
#define NGBE_INTPHY_INT_LSC	0x0010

/* PHY mdi standard config */
#define NGBE_MDI_PHY_ID1_OFFSET		2
#define NGBE_MDI_PHY_ID2_OFFSET		3
#define NGBE_MDI_PHY_ID_MASK		0xfffffc00
#define NGBE_MDI_PHY_SPEED_SELECT1	0x0040
#define NGBE_MDI_PHY_DUPLEX		0x0100
#define NGBE_MDI_PHY_RESTART_AN		0x0200
#define NGBE_MDI_PHY_ANE		0x1000
#define NGBE_MDI_PHY_SPEED_SELECT0	0x2000
#define NGBE_MDI_PHY_RESET		0x8000

#define NGBE_PHY_RST_WAIT_PERIOD	50

#define NGBE_SR_AN_MMD_ADV_REG1_PAUSE_SYM	0x400
#define NGBE_SR_AN_MMD_ADV_REG1_PAUSE_ASM	0x800

#define SPI_CMD_READ_DWORD	1
#define SPI_CLK_DIV		3
#define SPI_CLK_DIV_OFFSET	25
#define SPI_CLK_CMD_OFFSET	28
#define SPI_TIME_OUT_VALUE	10000

/* PCI bus info */
#define NGBE_PCI_LINK_STATUS	0xb2

#define NGBE_PCI_LINK_WIDTH	0x3f0
#define NGBE_PCI_LINK_WIDTH_1	0x10
#define NGBE_PCI_LINK_WIDTH_2	0x20
#define NGBE_PCI_LINK_WIDTH_4	0x40
#define NGBE_PCI_LINK_WIDTH_8	0x80

#define NGBE_PCI_LINK_SPEED		0xf
#define NGBE_PCI_LINK_SPEED_2500	0x1
#define NGBE_PCI_LINK_SPEED_5000	0x2
#define NGBE_PCI_LINK_SPEED_8000	0x3

/* Number of 100 microseconds we wait for PCI Express master disable */
#define NGBE_PCI_MASTER_DISABLE_TIMEOUT	800

/* Check whether address is multicast. This is little-endian specific check. */
#define NGBE_IS_MULTICAST(Address)					\
	(int)(((uint8_t *)(Address))[0] & ((uint8_t)0x01))

/* Check whether an address is broadcast. */
#define NGBE_IS_BROADCAST(Address)					\
	((((uint8_t *)(Address))[0] == ((uint8_t)0xff)) &&		\
	(((uint8_t *)(Address))[1] == ((uint8_t)0xff)))

/* Link speed */
#define NGBE_LINK_SPEED_UNKNOWN		0
#define NGBE_LINK_SPEED_100_FULL	1
#define NGBE_LINK_SPEED_1GB_FULL	2
#define NGBE_LINK_SPEED_10_FULL		8
#define NGBE_LINK_SPEED_AUTONEG						\
	(NGBE_LINK_SPEED_100_FULL | NGBE_LINK_SPEED_1GB_FULL |		\
	NGBE_LINK_SPEED_10_FULL)


#define NGBE_HI_MAX_BLOCK_BYTE_LENGTH	256
#define NGBE_HI_COMMAND_TIMEOUT	5000

/* CEM support */
#define FW_CEM_CMD_RESERVED		0x0
#define FW_CEM_RESP_STATUS_SUCCESS	0x1
#define FW_CEM_HDR_LEN			0x4

#define FW_CEM_CMD_DRIVER_INFO		0xdd
#define FW_CEM_CMD_DRIVER_INFO_LEN	0x5

#define FW_EEPROM_CHECK_STATUS		0xe9
#define FW_PHY_LED_CONF			0xf1
#define FW_DEFAULT_CHECKSUM		0xff

#define FW_CEM_MAX_RETRIES	3

#define NGBE_MAX_SCATTER	32
#define NGBE_TSO_SIZE		32767
#define NGBE_MAX_RX_DESC_POLL	10

/* Packet buffer allocation strategies */
#define PBA_STRATEGY_EQUAL	0
#define PBA_STRATEGY_WEIGHTED	1

/* BitTimes (BT) conversion */
#define NGBE_BT2KB(BT)	((BT + (8 * 1024 - 1)) / (8 * 1024))
#define NGBE_B2BT(BT)	(BT * 8)

/* Calculate Delay to respond to PFC */
#define NGBE_PFC_D	672

/* Calculate Cable Delay */
#define NGBE_CABLE_DC	5556

/* Calculate Interface Delay */
#define NGBE_PHY_D	12800
#define NGBE_MAC_D	4096
#define NGBE_XAUI_D	(2 * 1024)

#define NGBE_ID		(NGBE_MAC_D + NGBE_XAUI_D + NGBE_PHY_D)

/* Calculate Delay incurred from higher layer */
#define NGBE_HD	6144

/* Calculate PCI Bus delay for low thresholds */
#define NGBE_PCI_DELAY	10000

/* Calculate delay value in bit times */
#define NGBE_DV(_max_frame_link, _max_frame_tc)				\
	((36 * (NGBE_B2BT(_max_frame_link) + NGBE_PFC_D + 		\
	(2 * NGBE_CABLE_DC) + (2 * NGBE_ID) + NGBE_HD) / 25 + 1) +	\
	2 * NGBE_B2BT(_max_frame_tc))

/* Calculate low threshold delay values */
#define NGBE_LOW_DV_X540(_max_frame_tc)					\
	(2 * NGBE_B2BT(_max_frame_tc) + (36 * NGBE_PCI_DELAY / 25) + 1)

#define NGBE_LOW_DV(_max_frame_tc)					\
	(2 * NGBE_LOW_DV_X540(_max_frame_tc))

/* Compatibility glue. */
#define msec_delay(x)		DELAY(1000 * (x))
#define roundup2(size, unit)	(((size) + (unit) - 1) & ~((unit) - 1))
#define le32_to_cpup(x)		(le32toh(*(const uint32_t *)(x)))
#define le32_to_cpus(x)							\
	do { *((uint32_t *)(x)) = le32_to_cpup((x)); } while (0)

enum ngbe_media_type {
	ngbe_media_type_unknown = 0,
	ngbe_media_type_fiber,
	ngbe_media_type_copper,
	ngbe_media_type_backplane,
	ngbe_media_type_virtual
};

/* Flow Control Settings */
enum ngbe_fc_mode {
	ngbe_fc_none = 0,
	ngbe_fc_rx_pause,
	ngbe_fc_tx_pause,
	ngbe_fc_full,
	ngbe_fc_default
};

enum ngbe_eeprom_type {
	ngbe_eeprom_uninitialized = 0,
	ngbe_eeprom_spi,
	ngbe_flash,
	ngbe_eeprom_none	/* No NVM support */
};

enum ngbe_phy_type {
	ngbe_phy_unknown = 0,
	ngbe_phy_none,
	ngbe_phy_internal,
};

enum ngbe_reset_type {
	NGBE_LAN_RESET = 0,
	NGBE_SW_RESET,
	NGBE_GLOBAL_RESET
};

enum ngbe_isb_idx {
	NGBE_ISB_HEADER,
	NGBE_ISB_MISC,
	NGBE_ISB_VEC0,
	NGBE_ISB_VEC1,
	NGBE_ISB_MAX
};

/* PCI bus types */
enum ngbe_bus_type {
	ngbe_bus_type_unknown	= 0,
	ngbe_bus_type_pci,
	ngbe_bus_type_pcix,
	ngbe_bus_type_pci_express,
	ngbe_bus_type_internal,
	ngbe_bus_type_reserved
};

/* PCI bus speeds */
enum ngbe_bus_speed {
	ngbe_bus_speed_unknown	= 0,
	ngbe_bus_speed_33	= 33,
	ngbe_bus_speed_66	= 66,
	ngbe_bus_speed_100	= 100,
	ngbe_bus_speed_120	= 120,
	ngbe_bus_speed_133	= 133,
	ngbe_bus_speed_2500	= 2500,
	ngbe_bus_speed_5000	= 5000,
	ngbe_bus_speed_8000	= 8000,
	ngbe_bus_speed_reserved
};

/* PCI bus widths */
enum ngbe_bus_width {
	ngbe_bus_width_unknown	= 0,
	ngbe_bus_width_pcie_x1	= 1,
	ngbe_bus_width_pcie_x2	= 2,
	ngbe_bus_width_pcie_x4	= 4,
	ngbe_bus_width_pcie_x8	= 8,
	ngbe_bus_width_32	= 32,
	ngbe_bus_width_64	= 64,
	ngbe_bus_width_reserved
};

/* Host interface command structures */
struct ngbe_hic_hdr {
	uint8_t	cmd;
	uint8_t	buf_len;
	union {
		uint8_t	cmd_resv;
		uint8_t	ret_status;
	} cmd_or_resp;
	uint8_t	checksum;
};

struct ngbe_hic_hdr2_req {
	uint8_t	cmd;
	uint8_t	buf_lenh;
	uint8_t	buf_lenl;
	uint8_t	checksum;
};

struct ngbe_hic_hdr2_rsp {
	uint8_t	cmd;
	uint8_t	buf_lenl;
	uint8_t	buf_lenh_status;
	uint8_t	checksum;
};

union ngbe_hic_hdr2 {
	struct ngbe_hic_hdr2_req	req;
	struct ngbe_hic_hdr2_rsp	rsp;
};

struct ngbe_hic_drv_info {
	struct ngbe_hic_hdr	hdr;
	uint8_t			port_num;
	uint8_t			ver_sub;
	uint8_t			ver_build;
	uint8_t			ver_min;
	uint8_t			ver_maj;
	uint8_t			pad;
	uint16_t		pad2;
};

struct ngbe_hic_read_shadow_ram {
	union ngbe_hic_hdr2	hdr;
	uint32_t		address;
	uint16_t		length;
	uint16_t		pad2;
	uint16_t		data;
	uint16_t		pad3;
};

struct ngbe_osdep {
	bus_dma_tag_t		os_dmat;
	bus_space_tag_t		os_memt;
	bus_space_handle_t	os_memh;

	bus_size_t		os_memsize;
	bus_addr_t		os_membase;

	void			*os_sc;
	struct pci_attach_args	os_pa;
};

/* Forward declaration. */
struct ngbe_hw;
struct ngbe_softc;

/* Iterator type for walking multicast address lists */
typedef uint8_t * (*ngbe_mc_addr_itr) (struct ngbe_hw *, uint8_t **, \
	uint32_t *);

struct ngbe_eeprom_operations {
	void	(*init_params)(struct ngbe_hw *);
	int	(*eeprom_chksum_cap_st)(struct ngbe_softc *, uint16_t,
		    uint32_t *);
	int	(*phy_led_oem_chk)(struct ngbe_softc *, uint32_t *);
};

struct ngbe_mac_operations {
	int			(*init_hw)(struct ngbe_softc *);
	int			(*reset_hw)(struct ngbe_softc *);
	int			(*start_hw)(struct ngbe_softc *);
	void			(*clear_hw_cntrs)(struct ngbe_hw *);
	enum ngbe_media_type	(*get_media_type)(struct ngbe_hw *);
	void			(*get_mac_addr)(struct ngbe_hw *, uint8_t *);
	int			(*stop_adapter)(struct ngbe_softc *);
	void			(*get_bus_info)(struct ngbe_softc *);
	void			(*set_lan_id)(struct ngbe_hw *);
	void			(*enable_rx_dma)(struct ngbe_hw *, uint32_t);
	void			(*disable_sec_rx_path)(struct ngbe_hw *);
	void			(*enable_sec_rx_path)(struct ngbe_hw *);
	int			(*acquire_swfw_sync)(struct ngbe_softc *,
				    uint32_t);
	void			(*release_swfw_sync)(struct ngbe_softc *,
				    uint32_t);

	/* Link */
	int			(*setup_link)(struct ngbe_softc *, uint32_t,
				    int);
	int			(*check_link)(struct ngbe_hw *, uint32_t *,
				    int *, int);
	void			(*get_link_capabilities)(struct ngbe_hw *,
				    uint32_t *, int *);

	/* Packet Buffer manipulation */
	void			(*setup_rxpba)(struct ngbe_hw *, int, uint32_t,
				    int);

	/* RAR, Multicast, VLAN */
	int			(*set_rar)(struct ngbe_softc *, uint32_t,
				    uint8_t *, uint64_t, uint32_t);
	void			(*init_rx_addrs)(struct ngbe_softc *);
	void			(*update_mc_addr_list)(struct ngbe_hw *,
				    uint8_t *, uint32_t, ngbe_mc_addr_itr, int);

	void			(*clear_vfta)(struct ngbe_hw *);
	void			(*init_uta_tables)(struct ngbe_hw *);

	/* Flow Control */
	int			(*fc_enable)(struct ngbe_softc *);
	int			(*setup_fc)(struct ngbe_softc *);

	/* Manageability interface */
	int			(*set_fw_drv_ver)(struct ngbe_softc *, uint8_t,
				    uint8_t, uint8_t, uint8_t);
	void			(*init_thermal_sensor_thresh)(struct ngbe_hw *);
	void			(*disable_rx)(struct ngbe_hw *);
	void			(*enable_rx)(struct ngbe_hw *);
};

struct ngbe_phy_operations {
	int		(*identify)(struct ngbe_softc *);
	int		(*init)(struct ngbe_softc *);
	int		(*reset)(struct ngbe_softc *);
	int		(*read_reg)(struct ngbe_hw *, uint32_t, uint32_t,
			    uint16_t *);
	int		(*write_reg)(struct ngbe_hw *, uint32_t, uint32_t,
			    uint16_t);
	int		(*setup_link)(struct ngbe_softc *, uint32_t, int);
	void		(*phy_led_ctrl)(struct ngbe_softc *);
	int		(*check_overtemp)(struct ngbe_hw *);
	void		(*check_event)(struct ngbe_softc *);
	void		(*get_adv_pause)(struct ngbe_hw *, uint8_t *);
	void		(*get_lp_adv_pause)(struct ngbe_hw *, uint8_t *);
	int		(*set_adv_pause)(struct ngbe_hw *, uint16_t);
	int		(*setup_once)(struct ngbe_softc *);
};

struct ngbe_addr_filter_info {
	uint32_t	num_mc_addrs;
	uint32_t	rar_used_count;
	uint32_t	mta_in_use;
	uint32_t	overflow_promisc;
	int		user_set_promisc;
};

/* Bus parameters */
struct ngbe_bus_info {
	enum ngbe_bus_speed	speed;
	enum ngbe_bus_width	width;
	enum ngbe_bus_type	type;
	uint16_t		lan_id;
};

struct ngbe_eeprom_info {
	struct ngbe_eeprom_operations	ops;
	enum ngbe_eeprom_type		type;
	uint16_t			sw_region_offset;
};

struct ngbe_mac_info {
	struct ngbe_mac_operations	ops;
	uint8_t				addr[ETHER_ADDR_LEN];
	uint8_t				perm_addr[ETHER_ADDR_LEN];
	uint32_t			mta_shadow[NGBE_MAX_MTA];
	int				mc_filter_type;
	uint32_t			mcft_size;
	uint32_t			vft_shadow[NGBE_MAX_VFTA_ENTRIES];
	uint32_t			vft_size;
	uint32_t			num_rar_entries;
	uint32_t			rx_pb_size;
	uint32_t			max_tx_queues;
	uint32_t			max_rx_queues;
	int				autotry_restart;
	int				set_lben;
	int				autoneg;
};

/* Flow control parameters */
struct ngbe_fc_info {
	uint32_t		high_water;
	uint32_t		low_water;
	uint16_t		pause_time;
	int			strict_ieee;
	int			disable_fc_autoneg;
	int			fc_was_autonegged;
	enum ngbe_fc_mode	current_mode;
	enum ngbe_fc_mode	requested_mode;
};

struct ngbe_phy_info {
	struct ngbe_phy_operations	ops;
	enum ngbe_phy_type		type;
	uint32_t			addr;
	uint32_t			id;
	uint32_t			phy_semaphore_mask;
	enum ngbe_media_type		media_type;
	uint32_t			autoneg_advertised;
	int				reset_if_overtemp;
	uint32_t			force_speed;
};

struct ngbe_hw {
	void				*back;
	struct ngbe_mac_info		mac;
	struct ngbe_addr_filter_info	addr_ctrl;
	struct ngbe_fc_info		fc;
	struct ngbe_phy_info		phy;
	struct ngbe_eeprom_info		eeprom;
	struct ngbe_bus_info		bus;
	uint32_t			subsystem_device_id;
	int				adapter_stopped;
	enum ngbe_reset_type		reset_type;
	int				force_full_reset;
};

/* Transmit Descriptor */
union ngbe_tx_desc {
	struct {
		uint64_t	buffer_addr;
		uint32_t	cmd_type_len;
		uint32_t	olinfo_status;
	} read;
	struct {
		uint64_t	rsvd;
		uint32_t	nxtseq_seed;
		uint32_t	status;
	} wb;
};
#define NGBE_TXD_DTYP_DATA	0x00000000
#define NGBE_TXD_DTYP_CTXT	0x00100000
#define NGBE_TXD_STAT_DD	0x00000001
#define NGBE_TXD_L4CS		0x00000200
#define NGBE_TXD_IIPCS		0x00000400
#define NGBE_TXD_EOP		0x01000000
#define NGBE_TXD_IFCS		0x02000000
#define NGBE_TXD_RS		0x08000000
#define NGBE_TXD_VLE		0x40000000
#define NGBE_TXD_MACLEN_SHIFT	9
#define NGBE_TXD_PAYLEN_SHIFT	13
#define NGBE_TXD_VLAN_SHIFT	16

#define NGBE_PTYPE_PKT_IPV6	0x08
#define NGBE_PTYPE_PKT_IP	0x20
#define NGBE_PTYPE_TYP_IP	0x02
#define NGBE_PTYPE_TYP_UDP	0x03
#define NGBE_PTYPE_TYP_TCP	0x04

/* Receive Descriptor */
union ngbe_rx_desc {
	struct {
		uint64_t	pkt_addr;
		uint64_t	hdr_addr;
	} read;
	struct {
		struct {
			union {
				uint32_t	data;
				struct {
					uint16_t	pkt_info;
					uint16_t	hdr_info;
				} hs_rss;
			} lo_dword;
			union {
				uint32_t	rss;
				struct {
					uint16_t	ip_id;
					uint16_t	csum;
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			uint32_t	status_error;
			uint16_t	length;
			uint16_t	vlan;
		} upper;
	} wb;
};
#define NGBE_RXD_STAT_DD	0x00000001
#define NGBE_RXD_STAT_EOP	0x00000002
#define NGBE_RXD_STAT_VP	0x00000020
#define NGBE_RXD_STAT_L4CS	0x00000080
#define NGBE_RXD_STAT_IPCS	0x00000100
#define NGBE_RXD_ERR_RXE	0x20000000
#define NGBE_RXD_ERR_TCPE	0x40000000
#define NGBE_RXD_ERR_IPE	0x80000000
#define NGBE_RXD_RSSTYPE_MASK	0x0000000f

/* RSS hash results */
#define NGBE_RXD_RSSTYPE_NONE	0x00000000

/* Context descriptor */
struct ngbe_tx_context_desc {
	uint32_t	vlan_macip_lens;
	uint32_t	seqnum_seed;
	uint32_t	type_tucmd_mlhl;
	uint32_t	mss_l4len_idx;
};

struct ngbe_tx_buf {
	uint32_t	eop_index;
	struct mbuf	*m_head;
	bus_dmamap_t	map;
};

struct ngbe_rx_buf {
	struct mbuf	*buf;
	struct mbuf	*fmp;
	bus_dmamap_t	map;
};

struct ngbe_dma_alloc {
	caddr_t			dma_vaddr;
	bus_dma_tag_t		dma_tag;
	bus_dmamap_t		dma_map;
	bus_dma_segment_t	dma_seg;
	bus_size_t		dma_size;
	int			dma_nseg;
};

struct tx_ring {
	struct ngbe_softc	*sc;
	struct ifqueue		*ifq;
	uint32_t		me;
	uint32_t		watchdog_timer;
	union ngbe_tx_desc	*tx_base;
	struct ngbe_tx_buf	*tx_buffers;
	struct ngbe_dma_alloc	txdma;
	uint32_t		next_avail_desc;
	uint32_t		next_to_clean;
	bus_dma_tag_t		txtag;
};

struct rx_ring {
	struct ngbe_softc	*sc;
	struct ifiqueue		*ifiq;
	uint32_t		me;
	union ngbe_rx_desc	*rx_base;
	struct ngbe_rx_buf	*rx_buffers;
	struct ngbe_dma_alloc	rxdma;
	uint32_t		last_desc_filled;
	uint32_t		next_to_check;
	struct timeout		rx_refill;
	struct if_rxring	rx_ring;
};

struct ngbe_queue {
	struct ngbe_softc	*sc;
	uint32_t		msix;
	uint32_t		eims;
	char			name[16];
	pci_intr_handle_t	ih;
	void			*tag;
	struct tx_ring		*txr;
	struct rx_ring		*rxr;
};

struct ngbe_softc {
	struct device		sc_dev;
	struct arpcom		sc_ac;
	struct ifmedia		sc_media;
	struct intrmap		*sc_intrmap;

	struct ngbe_osdep	osdep;
	struct ngbe_hw		hw;

	void			*tag;

	uint32_t		led_conf;
	uint32_t		gphy_efuse[2];
	uint32_t		link_speed;
	uint32_t		linkvec;
	int			link_up;

	int			num_tx_desc;
	int			num_rx_desc;

	struct ngbe_dma_alloc	isbdma;
	uint32_t		*isb_base;

	unsigned int		sc_nqueues;
	struct ngbe_queue	*queues;

	struct tx_ring          *tx_rings;
	struct rx_ring          *rx_rings;

	/* Multicast array memory */
	uint8_t			*mta;
};

#define DEVNAME(_sc)	((_sc)->sc_dev.dv_xname)

#define NGBE_FAILED_READ_REG	0xffffffff

/* Register READ/WRITE macros */
#define	NGBE_WRITE_FLUSH(a)						\
	NGBE_READ_REG(a, NGBE_MIS_PWR)
#define NGBE_READ_REG(a, reg)						\
	bus_space_read_4(((struct ngbe_osdep *)(a)->back)->os_memt,	\
	((struct ngbe_osdep *)(a)->back)->os_memh, reg)
#define NGBE_WRITE_REG(a, reg, value)					\
	bus_space_write_4(((struct ngbe_osdep *)(a)->back)->os_memt,	\
	((struct ngbe_osdep *)(a)->back)->os_memh, reg, value)
#define NGBE_READ_REG_ARRAY(a, reg, offset)				\
	bus_space_read_4(((struct ngbe_osdep *)(a)->back)->os_memt,	\
	((struct ngbe_osdep *)(a)->back)->os_memh, (reg + ((offset) << 2)))
#define NGBE_WRITE_REG_ARRAY(a, reg, offset, value)			\
	bus_space_write_4(((struct ngbe_osdep *)(a)->back)->os_memt,	\
	((struct ngbe_osdep *)(a)->back)->os_memh, 			\
	(reg + ((offset) << 2)), value)
