/*	$OpenBSD: mtwreg.h,v 1.2 2022/07/27 06:41:04 hastings Exp $	*/
/*
 * Copyright (c) 2007 Damien Bergamini <damien.bergamini@free.fr>
 * Copyright (c) 2021 James Hastings
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

#define MTW_ASIC_VER			0x0000
#define MTW_CMB_CTRL			0x0020
#define MTW_EFUSE_CTRL			0x0024
#define MTW_EFUSE_DATA0			0x0028
#define MTW_EFUSE_DATA1			0x002c
#define MTW_EFUSE_DATA2			0x0030
#define MTW_EFUSE_DATA3			0x0034
#define MTW_OSC_CTRL			0x0038
#define MTW_COEX_CFG0			0x0040
#define MTW_PLL_CTRL			0x0050
#define MTW_LDO_CFG0			0x006c
#define MTW_LDO_CFG1			0x0070
#define MTW_WLAN_CTRL			0x0080

/* SCH/DMA registers */
#define MTW_INT_STATUS			0x0200
#define RT2860_INT_MASK			0x0204
#define MTW_WPDMA_GLO_CFG		0x0208
#define RT2860_WPDMA_RST_IDX		0x020c
#define RT2860_DELAY_INT_CFG		0x0210
#define MTW_WMM_AIFSN_CFG		0x0214
#define MTW_WMM_CWMIN_CFG		0x0218
#define MTW_WMM_CWMAX_CFG		0x021c
#define MTW_WMM_TXOP0_CFG		0x0220
#define MTW_WMM_TXOP1_CFG		0x0224
#define RT2860_GPIO_CTRL		0x0228
#define RT2860_MCU_CMD_REG		0x022c
#define MTW_MCU_DMA_ADDR		0x0230
#define MTW_MCU_DMA_LEN			0x0234
#define MTW_USB_DMA_CFG			0x0238
#define RT2860_TX_BASE_PTR(qid)		(0x0230 + (qid) * 16)
#define RT2860_TX_MAX_CNT(qid)		(0x0234 + (qid) * 16)
#define RT2860_TX_CTX_IDX(qid)		(0x0238 + (qid) * 16)
#define RT2860_TX_DTX_IDX(qid)		(0x023c + (qid) * 16)
#define MTW_TSO_CTRL			0x0250
#define MTW_HDR_TRANS_CTRL		0x0260
#define RT2860_RX_BASE_PTR		0x0290
#define RT2860_RX_MAX_CNT		0x0294
#define RT2860_RX_CALC_IDX		0x0298
#define RT2860_FS_DRX_IDX		0x029c
#define MTW_US_CYC_CNT			0x02a4

#define MTW_TX_RING_BASE		0x0300
#define MTW_RX_RING_BASE		0x03c0

/* Packet Buffer registers */
#define MTW_SYS_CTRL			0x0400
#define MTW_PBF_CFG			0x0404
#define MTW_TX_MAX_PCNT			0x0408
#define MTW_RX_MAX_PCNT			0x040c
#define MTW_PBF_CTRL			0x0410
#define RT2860_BUF_CTRL			0x0410
#define RT2860_MCU_INT_STA		0x0414
#define RT2860_MCU_INT_ENA		0x0418
#define RT2860_TXQ_IO(qid)		(0x041c + (qid) * 4)
#define MTW_BCN_OFFSET0			0x041c
#define MTW_BCN_OFFSET1			0x0420
#define MTW_BCN_OFFSET2			0x0424
#define MTW_BCN_OFFSET3			0x0428
#define RT2860_RX0Q_IO			0x0424
#define MTW_RXQ_STA			0x0430
#define MTW_TXQ_STA			0x0434
#define MTW_TXRXQ_PCNT			0x0438

/* RF registers */
#define MTW_RF_CSR			0x0500
#define MTW_RF_BYPASS0			0x0504
#define MTW_RF_BYPASS1			0x0508
#define MTW_RF_SETTING0			0x050C
#define MTW_RF_MISC			0x0518
#define MTW_RF_DATA_WR			0x0524
#define MTW_RF_CTRL			0x0528
#define MTW_RF_DATA_RD			0x052c

/* MCU registers */
#define MTW_MCU_RESET_CTL		0x070c
#define MTW_MCU_INT_LEVEL		0x0718
#define MTW_MCU_COM_REG0		0x0730
#define MTW_MCU_COM_REG1		0x0734
#define MTW_MCU_COM_REG2		0x0738
#define MTW_MCU_COM_REG3		0x073c
#define MTW_FCE_PSE_CTRL		0x0800
#define MTW_FCE_PARAMETERS		0x0804
#define MTW_FCE_CSO			0x0808
#define MTW_FCE_L2_STUFF		0x080c
#define MTW_FCE_WLAN_FLOW_CTRL		0x0824
#define MTW_TX_CPU_FCE_BASE		0x09a0
#define MTW_TX_CPU_FCE_MAX_COUNT	0x09a4
#define MTW_MCU_FW_IDX			0x09a8
#define MTW_FCE_PDMA			0x09c4
#define MTW_FCE_SKIP_FS			0x0a6c

/* MAC registers */
#define MTW_MAC_VER_ID			0x1000
#define MTW_MAC_SYS_CTRL		0x1004
#define MTW_MAC_ADDR_DW0		0x1008
#define MTW_MAC_ADDR_DW1		0x100c
#define MTW_MAC_BSSID_DW0		0x1010
#define MTW_MAC_BSSID_DW1		0x1014
#define MTW_MAX_LEN_CFG			0x1018
#define MTW_BBP_CSR			0x101c
#define MTW_LED_CFG			0x102c
#define MTW_AMPDU_MAX_LEN_20M1S		0x1030
#define MTW_AMPDU_MAX_LEN_20M2S		0x1034
#define MTW_AMPDU_MAX_LEN_40M1S		0x1038
#define MTW_AMPDU_MAX_LEN_40M2S		0x103c
#define MTW_AMPDU_MAX_LEN		0x1040

/* MAC Timing control registers */
#define MTW_XIFS_TIME_CFG		0x1100
#define MTW_BKOFF_SLOT_CFG		0x1104
#define RT2860_NAV_TIME_CFG		0x1108
#define RT2860_CH_TIME_CFG		0x110c
#define RT2860_PBF_LIFE_TIMER		0x1110
#define MTW_BCN_TIME_CFG		0x1114
#define MTW_TBTT_SYNC_CFG		0x1118
#define MTW_TSF_TIMER_DW0		0x111c
#define MTW_TSF_TIMER_DW1		0x1120
#define RT2860_TBTT_TIMER		0x1124
#define MTW_INT_TIMER_CFG		0x1128
#define RT2860_INT_TIMER_EN		0x112c
#define RT2860_CH_IDLE_TIME		0x1130

/* MAC Power Save configuration registers */
#define MTW_MAC_STATUS_REG		0x1200
#define MTW_PWR_PIN_CFG			0x1204
#define MTW_AUTO_WAKEUP_CFG		0x1208
#define MTW_AUX_CLK_CFG			0x120c
#define MTW_BBP_PA_MODE_CFG0		0x1214
#define MTW_BBP_PA_MODE_CFG1		0x1218
#define MTW_RF_PA_MODE_CFG0		0x121c
#define MTW_RF_PA_MODE_CFG1		0x1220
#define MTW_RF_PA_MODE_ADJ0		0x1228
#define MTW_RF_PA_MODE_ADJ1		0x122c
#define MTW_DACCLK_EN_DLY_CFG		0x1264 /* MT7612 */

/* MAC TX configuration registers */
#define MTW_EDCA_AC_CFG(aci)		(0x1300 + (aci) * 4)
#define MTW_EDCA_TID_AC_MAP		0x1310
#define MTW_TX_PWR_CFG(ridx)		(0x1314 + (ridx) * 4)
#define MTW_TX_PIN_CFG			0x1328
#define MTW_TX_BAND_CFG			0x132c
#define MTW_TX_SW_CFG0			0x1330
#define MTW_TX_SW_CFG1			0x1334
#define MTW_TX_SW_CFG2			0x1338
#define RT2860_TXOP_THRES_CFG		0x133c
#define MTW_TXOP_CTRL_CFG		0x1340
#define MTW_TX_RTS_CFG			0x1344
#define MTW_TX_TIMEOUT_CFG		0x1348
#define MTW_TX_RETRY_CFG		0x134c
#define MTW_TX_LINK_CFG			0x1350
#define MTW_HT_FBK_CFG0			0x1354
#define MTW_HT_FBK_CFG1			0x1358
#define MTW_LG_FBK_CFG0			0x135c
#define MTW_LG_FBK_CFG1			0x1360
#define MTW_CCK_PROT_CFG		0x1364
#define MTW_OFDM_PROT_CFG		0x1368
#define MTW_MM20_PROT_CFG		0x136c
#define MTW_MM40_PROT_CFG		0x1370
#define MTW_GF20_PROT_CFG		0x1374
#define MTW_GF40_PROT_CFG		0x1378
#define RT2860_EXP_CTS_TIME		0x137c
#define MTW_EXP_ACK_TIME		0x1380
#define MTW_TX_PWR_CFG5			0x1384
#define MTW_TX_PWR_CFG6			0x1388
#define MTW_TX_PWR_EXT_CFG(ridx)	(0x1390 + (ridx) * 4)
#define MTW_TX0_RF_GAIN_CORR		0x13a0
#define MTW_TX1_RF_GAIN_CORR		0x13a4
#define MTW_TX0_RF_GAIN_ATTEN		0x13a8
#define MTW_TX_ALC_CFG3			0x13ac
#define MTW_TX_ALC_CFG0			0x13b0
#define MTW_TX_ALC_CFG1			0x13b4
#define MTW_TX_ALC_CFG4			0x13c0
#define MTW_TX_ALC_VGA3			0x13c8
#define MTW_TX_PWR_CFG7			0x13d4
#define MTW_TX_PWR_CFG8			0x13d8
#define MTW_TX_PWR_CFG9			0x13dc
#define MTW_VHT20_PROT_CFG		0x13e0
#define MTW_VHT40_PROT_CFG		0x13e4
#define MTW_VHT80_PROT_CFG		0x13e8
#define MTW_TX_PIFS_CFG			0x13ec /* MT761X */

/* MAC RX configuration registers */
#define MTW_RX_FILTR_CFG		0x1400
#define MTW_AUTO_RSP_CFG		0x1404
#define MTW_LEGACY_BASIC_RATE		0x1408
#define MTW_HT_BASIC_RATE		0x140c
#define MTW_HT_CTRL_CFG			0x1410
#define RT2860_SIFS_COST_CFG		0x1414
#define RT2860_RX_PARSER_CFG		0x1418

/* MAC Security configuration registers */
#define RT2860_TX_SEC_CNT0		0x1500
#define RT2860_RX_SEC_CNT0		0x1504
#define RT2860_CCMP_FC_MUTE		0x1508
#define MTW_PN_PAD_MODE			0x150c /* MT761X */

/* MAC HCCA/PSMP configuration registers */
#define MTW_TXOP_HLDR_ADDR0		0x1600
#define MTW_TXOP_HLDR_ADDR1		0x1604
#define MTW_TXOP_HLDR_ET		0x1608
#define RT2860_QOS_CFPOLL_RA_DW0	0x160c
#define RT2860_QOS_CFPOLL_A1_DW1	0x1610
#define RT2860_QOS_CFPOLL_QC		0x1614
#define MTW_PROT_AUTO_TX_CFG		0x1648

/* MAC Statistics Counters */
#define MTW_RX_STA_CNT0		0x1700
#define MTW_RX_STA_CNT1		0x1704
#define MTW_RX_STA_CNT2		0x1708
#define MTW_TX_STA_CNT0		0x170c
#define MTW_TX_STA_CNT1		0x1710
#define MTW_TX_STA_CNT2		0x1714
#define MTW_TX_STAT_FIFO	0x1718

/* RX WCID search table */
#define MTW_WCID_ENTRY(wcid)		(0x1800 + (wcid) * 8)

/* MT761x Baseband */
#define MTW_BBP_CORE(x)			(0x2000 + (x) * 4)
#define MTW_BBP_IBI(x)			(0x2100 + (x) * 4)
#define MTW_BBP_AGC(x)			(0x2300 + (x) * 4)
#define MTW_BBP_TXC(x)			(0x2400 + (x) * 4)
#define MTW_BBP_RXC(x)			(0x2500 + (x) * 4)
#define MTW_BBP_TXQ(x)			(0x2600 + (x) * 4)
#define MTW_BBP_TXBE(x)			(0x2700 + (x) * 4)
#define MTW_BBP_RXFE(x)			(0x2800 + (x) * 4)
#define MTW_BBP_RXO(x)			(0x2900 + (x) * 4)
#define MTW_BBP_DFS(x)			(0x2a00 + (x) * 4)
#define MTW_BBP_TR(x)			(0x2b00 + (x) * 4)
#define MTW_BBP_CAL(x)			(0x2c00 + (x) * 4)
#define MTW_BBP_DSC(x)			(0x2e00 + (x) * 4)
#define MTW_BBP_PFMU(x)			(0x2f00 + (x) * 4)

#define MTW_SKEY_MODE_16_23		0x7008
#define MTW_SKEY_MODE_24_31		0x700c
#define MTW_H2M_MAILBOX			0x7010

/* Pair-wise key table */
#define MTW_PKEY(wcid)			(0x8000 + (wcid) * 32)

/* USB 3.0 DMA */
#define MTW_USB_U3DMA_CFG		0x9018

/* IV/EIV table */
#define MTW_IVEIV(wcid)			(0xa000 + (wcid) * 8)

/* WCID attribute table */
#define MTW_WCID_ATTR(wcid)		(0xa800 + (wcid) * 4)

/* Shared Key Table */
#define MTW_SKEY(vap, kidx)		((vap & 8) ? MTW_SKEY_1(vap, kidx) : \
					    MTW_SKEY_0(vap, kidx))
#define MTW_SKEY_0(vap, kidx)		(0xac00 + (4 * (vap) + (kidx)) * 32)
#define MTW_SKEY_1(vap, kidx)		(0xb400 + (4 * ((vap) & 7) + (kidx)) * 32)

/* Shared Key Mode */
#define MTW_SKEY_MODE_0_7		0xb000
#define MTW_SKEY_MODE_8_15		0xb004

/* Shared Key Mode */
#define MTW_SKEY_MODE_BASE		0xb000

/* Beacon */
#define MTW_BCN_BASE			0xc000

/* possible flags for register CMB_CTRL 0x0020 */
#define MTW_PLL_LD		(1U << 23)
#define MTW_XTAL_RDY		(1U << 22)

/* possible flags for register EFUSE_CTRL 0x0024 */
#define MTW_SEL_EFUSE		(1U << 31)
#define MTW_EFSROM_KICK		(1U << 30)
#define MTW_EFSROM_AIN_MASK	0x03ff0000
#define MTW_EFSROM_AIN_SHIFT	16
#define MTW_EFSROM_MODE_MASK	0x000000c0
#define MTW_EFUSE_AOUT_MASK	0x0000003f

/* possible flags for register OSC_CTRL 0x0038 */
#define MTW_OSC_EN		(1U << 31)
#define MTW_OSC_CAL_REQ		(1U << 30)
#define MTW_OSC_CLK_32K_VLD	(1U << 29)
#define MTW_OSC_CAL_ACK		(1U << 28)
#define MTW_OSC_CAL_CNT		(0xfff << 16)
#define MTW_OSC_REF_CYCLE	0x1fff

/* possible flags for register WLAN_CTRL 0x0080 */
#define MTW_GPIO_OUT_OE_ALL	(0xff << 24)
#define MTW_GPIO_OUT_ALL	(0xff << 16)
#define MTW_GPIO_IN_ALL		(0xff << 8)
#define MTW_THERM_CKEN		(1U << 9)
#define MTW_THERM_RST		(1U << 8)
#define MTW_INV_TR_SW0		(1U << 6)
#define MTW_FRC_WL_ANT_SET	(1U << 5)
#define MTW_PCIE_APP0_CLK_REQ	(1U << 4)
#define MTW_WLAN_RESET		(1U << 3)
#define MTW_WLAN_RESET_RF	(1U << 2)
#define MTW_WLAN_CLK_EN		(1U << 1)
#define MTW_WLAN_EN		(1U << 0)

/* possible flags for registers INT_STATUS/INT_MASK 0x0200 */
#define RT2860_TX_COHERENT	(1 << 17)
#define RT2860_RX_COHERENT	(1 << 16)
#define RT2860_MAC_INT_4	(1 << 15)
#define RT2860_MAC_INT_3	(1 << 14)
#define RT2860_MAC_INT_2	(1 << 13)
#define RT2860_MAC_INT_1	(1 << 12)
#define RT2860_MAC_INT_0	(1 << 11)
#define RT2860_TX_RX_COHERENT	(1 << 10)
#define RT2860_MCU_CMD_INT	(1 <<  9)
#define RT2860_TX_DONE_INT5	(1 <<  8)
#define RT2860_TX_DONE_INT4	(1 <<  7)
#define RT2860_TX_DONE_INT3	(1 <<  6)
#define RT2860_TX_DONE_INT2	(1 <<  5)
#define RT2860_TX_DONE_INT1	(1 <<  4)
#define RT2860_TX_DONE_INT0	(1 <<  3)
#define RT2860_RX_DONE_INT	(1 <<  2)
#define RT2860_TX_DLY_INT	(1 <<  1)
#define RT2860_RX_DLY_INT	(1 <<  0)

/* possible flags for register WPDMA_GLO_CFG 0x0208 */
#define MTW_HDR_SEG_LEN_SHIFT		8
#define MTW_BIG_ENDIAN			(1 << 7)
#define MTW_TX_WB_DDONE			(1 << 6)
#define MTW_WPDMA_BT_SIZE_SHIFT		4
#define MTW_WPDMA_BT_SIZE16		0
#define MTW_WPDMA_BT_SIZE32		1
#define MTW_WPDMA_BT_SIZE64		2
#define MTW_WPDMA_BT_SIZE128		3
#define MTW_RX_DMA_BUSY			(1 << 3)
#define MTW_RX_DMA_EN			(1 << 2)
#define MTW_TX_DMA_BUSY			(1 << 1)
#define MTW_TX_DMA_EN			(1 << 0)

/* possible flags for register DELAY_INT_CFG */
#define RT2860_TXDLY_INT_EN		(1U << 31)
#define RT2860_TXMAX_PINT_SHIFT		24
#define RT2860_TXMAX_PTIME_SHIFT	16
#define RT2860_RXDLY_INT_EN		(1U << 15)
#define RT2860_RXMAX_PINT_SHIFT		8
#define RT2860_RXMAX_PTIME_SHIFT	0

/* possible flags for register GPIO_CTRL */
#define RT2860_GPIO_D_SHIFT	8
#define RT2860_GPIO_O_SHIFT	0

/* possible flags for register MCU_DMA_ADDR 0x0230 */
#define MTW_MCU_READY		(1U <<  0)

/* possible flags for register USB_DMA_CFG 0x0238 */
#define MTW_USB_TX_BUSY			(1U << 31)
#define MTW_USB_RX_BUSY			(1U << 30)
#define MTW_USB_EPOUT_VLD_SHIFT		24
#define MTW_USB_RX_WL_DROP		(1U << 25)
#define MTW_USB_TX_EN			(1U << 23)
#define MTW_USB_RX_EN			(1U << 22)
#define MTW_USB_RX_AGG_EN		(1U << 21)
#define MTW_USB_TXOP_HALT		(1U << 20)
#define MTW_USB_TX_CLEAR		(1U << 19)
#define MTW_USB_PHY_WD_EN		(1U << 16)
#define MTW_USB_PHY_MAN_RST		(1U << 15)
#define MTW_USB_RX_AGG_LMT(x)		((x) << 8)	/* in unit of 1KB */
#define MTW_USB_RX_AGG_TO(x)		((x) & 0xff)	/* in unit of 33ns */

/* possible flags for register US_CYC_CNT 0x02a4 */
#define RT2860_TEST_EN		(1 << 24)
#define RT2860_TEST_SEL_SHIFT	16
#define RT2860_BT_MODE_EN	(1 <<  8)
#define RT2860_US_CYC_CNT_SHIFT	0

/* possible flags for register PBF_CFG 0x0404 */
#define MTW_PBF_CFG_RX_DROP	(1 <<  8)
#define MTW_PBF_CFG_RX0Q_EN	(1 <<  4)
#define MTW_PBF_CFG_TX3Q_EN	(1 <<  3)
#define MTW_PBF_CFG_TX2Q_EN	(1 <<  2)
#define MTW_PBF_CFG_TX1Q_EN	(1 <<  1)
#define MTW_PBF_CFG_TX0Q_EN	(1 <<  0)

/* possible flags for register BUF_CTRL 0x0410 */
#define RT2860_WRITE_TXQ(qid)	(1 << (11 - (qid)))
#define RT2860_NULL0_KICK	(1 << 7)
#define RT2860_NULL1_KICK	(1 << 6)
#define RT2860_BUF_RESET	(1 << 5)
#define RT2860_READ_TXQ(qid)	(1 << (3 - (qid))
#define RT2860_READ_RX0Q	(1 << 0)

/* possible flags for registers MCU_INT_STA/MCU_INT_ENA */
#define RT2860_MCU_MAC_INT_8	(1 << 24)
#define RT2860_MCU_MAC_INT_7	(1 << 23)
#define RT2860_MCU_MAC_INT_6	(1 << 22)
#define RT2860_MCU_MAC_INT_4	(1 << 20)
#define RT2860_MCU_MAC_INT_3	(1 << 19)
#define RT2860_MCU_MAC_INT_2	(1 << 18)
#define RT2860_MCU_MAC_INT_1	(1 << 17)
#define RT2860_MCU_MAC_INT_0	(1 << 16)
#define RT2860_DTX0_INT		(1 << 11)
#define RT2860_DTX1_INT		(1 << 10)
#define RT2860_DTX2_INT		(1 <<  9)
#define RT2860_DRX0_INT		(1 <<  8)
#define RT2860_HCMD_INT		(1 <<  7)
#define RT2860_N0TX_INT		(1 <<  6)
#define RT2860_N1TX_INT		(1 <<  5)
#define RT2860_BCNTX_INT	(1 <<  4)
#define RT2860_MTX0_INT		(1 <<  3)
#define RT2860_MTX1_INT		(1 <<  2)
#define RT2860_MTX2_INT		(1 <<  1)
#define RT2860_MRX0_INT		(1 <<  0)

/* possible flags for register TXRXQ_PCNT 0x0438 */
#define MTW_RX0Q_PCNT_MASK	0xff000000
#define MTW_TX2Q_PCNT_MASK	0x00ff0000
#define MTW_TX1Q_PCNT_MASK	0x0000ff00
#define MTW_TX0Q_PCNT_MASK	0x000000ff

/* possible flags for register RF_CSR_CFG 0x0500 */
#define MTW_RF_CSR_KICK		(1U << 31)
#define MTW_RF_CSR_WRITE	(1U << 30)
#define MT7610_BANK_SHIFT	15
#define MT7601_BANK_SHIFT	14

/* possible flags for register FCE_L2_STUFF 0x080c */
#define MTW_L2S_WR_MPDU_LEN_EN	(1 << 4)

/* possible flag for register DEBUG_INDEX */
#define RT5592_SEL_XTAL		(1U << 31)

/* possible flags for register MAC_SYS_CTRL 0x1004 */
#define MTW_RX_TS_EN		(1 << 7)
#define MTW_WLAN_HALT_EN	(1 << 6)
#define MTW_PBF_LOOP_EN		(1 << 5)
#define MTW_CONT_TX_TEST	(1 << 4)
#define MTW_MAC_RX_EN		(1 << 3)
#define MTW_MAC_TX_EN		(1 << 2)
#define MTW_BBP_HRST		(1 << 1)
#define MTW_MAC_SRST		(1 << 0)

/* possible flags for register MAC_BSSID_DW1 0x100c */
#define RT2860_MULTI_BCN_NUM_SHIFT	18
#define RT2860_MULTI_BSSID_MODE_SHIFT	16

/* possible flags for register MAX_LEN_CFG 0x1018 */
#define RT2860_MIN_MPDU_LEN_SHIFT	16
#define RT2860_MAX_PSDU_LEN_SHIFT	12
#define RT2860_MAX_PSDU_LEN8K		0
#define RT2860_MAX_PSDU_LEN16K		1
#define RT2860_MAX_PSDU_LEN32K		2
#define RT2860_MAX_PSDU_LEN64K		3
#define RT2860_MAX_MPDU_LEN_SHIFT	0

/* possible flags for registers BBP_CSR_CFG 0x101c */
#define MTW_BBP_CSR_KICK		(1 << 17)
#define MTW_BBP_CSR_READ		(1 << 16)
#define MTW_BBP_ADDR_SHIFT		8
#define MTW_BBP_DATA_SHIFT		0

/* possible flags for register LED_CFG */
#define MTW_LED_MODE_ON			0
#define MTW_LED_MODE_DIM		1
#define MTW_LED_MODE_BLINK_TX		2
#define MTW_LED_MODE_SLOW_BLINK		3

/* possible flags for register XIFS_TIME_CFG 0x1100 */
#define MTW_BB_RXEND_EN			(1 << 29)
#define MTW_EIFS_TIME_SHIFT		20
#define MTW_OFDM_XIFS_TIME_SHIFT	16
#define MTW_OFDM_SIFS_TIME_SHIFT	8
#define MTW_CCK_SIFS_TIME_SHIFT		0

/* possible flags for register BKOFF_SLOT_CFG 0x1104 */
#define MTW_CC_DELAY_TIME_SHIFT		8
#define MTW_SLOT_TIME			0

/* possible flags for register NAV_TIME_CFG */
#define RT2860_NAV_UPD			(1U << 31)
#define RT2860_NAV_UPD_VAL_SHIFT	16
#define RT2860_NAV_CLR_EN		(1U << 15)
#define RT2860_NAV_TIMER_SHIFT		0

/* possible flags for register CH_TIME_CFG */
#define RT2860_EIFS_AS_CH_BUSY	(1 << 4)
#define RT2860_NAV_AS_CH_BUSY	(1 << 3)
#define RT2860_RX_AS_CH_BUSY	(1 << 2)
#define RT2860_TX_AS_CH_BUSY	(1 << 1)
#define RT2860_CH_STA_TIMER_EN	(1 << 0)

/* possible values for register BCN_TIME_CFG 0x1114 */
#define MTW_TSF_INS_COMP_SHIFT		24
#define MTW_BCN_TX_EN			(1 << 20)
#define MTW_TBTT_TIMER_EN		(1 << 19)
#define MTW_TSF_SYNC_MODE_SHIFT		17
#define MTW_TSF_SYNC_MODE_DIS		0
#define MTW_TSF_SYNC_MODE_STA		1
#define MTW_TSF_SYNC_MODE_IBSS		2
#define MTW_TSF_SYNC_MODE_HOSTAP	3
#define MTW_TSF_TIMER_EN		(1 << 16)
#define MTW_BCN_INTVAL_SHIFT		0

/* possible flags for register TBTT_SYNC_CFG 0x1118 */
#define RT2860_BCN_CWMIN_SHIFT		20
#define RT2860_BCN_AIFSN_SHIFT		16
#define RT2860_BCN_EXP_WIN_SHIFT	8
#define RT2860_TBTT_ADJUST_SHIFT	0

/* possible flags for register INT_TIMER_CFG 0x1128 */
#define RT2860_GP_TIMER_SHIFT		16
#define RT2860_PRE_TBTT_TIMER_SHIFT	0

/* possible flags for register INT_TIMER_EN */
#define RT2860_GP_TIMER_EN	(1 << 1)
#define RT2860_PRE_TBTT_INT_EN	(1 << 0)

/* possible flags for register MAC_STATUS_REG 0x1200 */
#define MTW_RX_STATUS_BUSY	(1 << 1)
#define MTW_TX_STATUS_BUSY	(1 << 0)

/* possible flags for register PWR_PIN_CFG 0x1204 */
#define RT2860_IO_ADDA_PD	(1 << 3)
#define RT2860_IO_PLL_PD	(1 << 2)
#define RT2860_IO_RA_PE		(1 << 1)
#define RT2860_IO_RF_PE		(1 << 0)

/* possible flags for register AUTO_WAKEUP_CFG 0x1208 */
#define MTW_AUTO_WAKEUP_EN		(1 << 15)
#define MTW_SLEEP_TBTT_NUM_SHIFT	8
#define MTW_WAKEUP_LEAD_TIME_SHIFT	0

/* possible flags for register TX_PIN_CFG 0x1328 */
#define RT2860_TRSW_POL		(1U << 19)
#define RT2860_TRSW_EN		(1U << 18)
#define RT2860_RFTR_POL		(1U << 17)
#define RT2860_RFTR_EN		(1U << 16)
#define RT2860_LNA_PE_G1_POL	(1U << 15)
#define RT2860_LNA_PE_A1_POL	(1U << 14)
#define RT2860_LNA_PE_G0_POL	(1U << 13)
#define RT2860_LNA_PE_A0_POL	(1U << 12)
#define RT2860_LNA_PE_G1_EN	(1U << 11)
#define RT2860_LNA_PE_A1_EN	(1U << 10)
#define RT2860_LNA_PE1_EN	(RT2860_LNA_PE_A1_EN | RT2860_LNA_PE_G1_EN)
#define RT2860_LNA_PE_G0_EN	(1U <<  9)
#define RT2860_LNA_PE_A0_EN	(1U <<  8)
#define RT2860_LNA_PE0_EN	(RT2860_LNA_PE_A0_EN | RT2860_LNA_PE_G0_EN)
#define RT2860_PA_PE_G1_POL	(1U <<  7)
#define RT2860_PA_PE_A1_POL	(1U <<  6)
#define RT2860_PA_PE_G0_POL	(1U <<  5)
#define RT2860_PA_PE_A0_POL	(1U <<  4)
#define RT2860_PA_PE_G1_EN	(1U <<  3)
#define RT2860_PA_PE_A1_EN	(1U <<  2)
#define RT2860_PA_PE_G0_EN	(1U <<  1)
#define RT2860_PA_PE_A0_EN	(1U <<  0)

/* possible flags for register TX_BAND_CFG 0x132c */
#define MTW_TX_BAND_SEL_2G	(1 << 2)
#define MTW_TX_BAND_SEL_5G	(1 << 1)
#define MTW_TX_BAND_UPPER_40M	(1 << 0)

/* possible flags for register TX_SW_CFG0 0x1330 */
#define RT2860_DLY_RFTR_EN_SHIFT	24
#define RT2860_DLY_TRSW_EN_SHIFT	16
#define RT2860_DLY_PAPE_EN_SHIFT	8
#define RT2860_DLY_TXPE_EN_SHIFT	0

/* possible flags for register TX_SW_CFG1 0x1334 */
#define RT2860_DLY_RFTR_DIS_SHIFT	16
#define RT2860_DLY_TRSW_DIS_SHIFT	8
#define RT2860_DLY_PAPE_DIS SHIFT	0

/* possible flags for register TX_SW_CFG2 0x1338 */
#define RT2860_DLY_LNA_EN_SHIFT		24
#define RT2860_DLY_LNA_DIS_SHIFT	16
#define RT2860_DLY_DAC_EN_SHIFT		8
#define RT2860_DLY_DAC_DIS_SHIFT	0

/* possible flags for register TXOP_THRES_CFG 0x133c */
#define RT2860_TXOP_REM_THRES_SHIFT	24
#define RT2860_CF_END_THRES_SHIFT	16
#define RT2860_RDG_IN_THRES		8
#define RT2860_RDG_OUT_THRES		0

/* possible flags for register TXOP_CTRL_CFG 0x1340 */
#define MTW_TXOP_ED_CCA_EN		(1 << 20)
#define MTW_EXT_CW_MIN_SHIFT		16
#define MTW_EXT_CCA_DLY_SHIFT		8
#define MTW_EXT_CCA_EN			(1 << 7)
#define MTW_LSIG_TXOP_EN		(1 << 6)
#define MTW_TXOP_TRUN_EN_MIMOPS		(1 << 4)
#define MTW_TXOP_TRUN_EN_TXOP		(1 << 3)
#define MTW_TXOP_TRUN_EN_RATE		(1 << 2)
#define MTW_TXOP_TRUN_EN_AC		(1 << 1)
#define MTW_TXOP_TRUN_EN_TIMEOUT	(1 << 0)

/* possible flags for register TX_RTS_CFG 0x1344 */
#define MTW_RTS_FBK_EN			(1 << 24)
#define MTW_RTS_THRES_SHIFT		8
#define MTW_RTS_RTY_LIMIT_SHIFT		0

/* possible flags for register TX_TIMEOUT_CFG 0x1348 */
#define MTW_TXOP_TIMEOUT_SHIFT		16
#define MTW_RX_ACK_TIMEOUT_SHIFT	8
#define MTW_MPDU_LIFE_TIME_SHIFT	4

/* possible flags for register TX_RETRY_CFG 0x134c */
#define MTW_TX_AUTOFB_EN		(1 << 30)
#define MTW_AGG_RTY_MODE_TIMER		(1 << 29)
#define MTW_NAG_RTY_MODE_TIMER		(1 << 28)
#define MTW_LONG_RTY_THRES_SHIFT	16
#define MTW_LONG_RTY_LIMIT_SHIFT	8
#define MTW_SHORT_RTY_LIMIT_SHIFT	0

/* possible flags for register TX_LINK_CFG 0x1350 */
#define MTW_REMOTE_MFS_SHIFT		24
#define MTW_REMOTE_MFB_SHIFT		16
#define MTW_TX_CFACK_EN			(1 << 12)
#define MTW_TX_RDG_EN			(1 << 11)
#define MTW_TX_MRQ_EN			(1 << 10)
#define MTW_REMOTE_UMFS_EN		(1 <<  9)
#define MTW_TX_MFB_EN			(1 <<  8)
#define MTW_REMOTE_MFB_LT_SHIFT		0

/* possible flags for registers *_PROT_CFG */
#define RT2860_RTSTH_EN			(1 << 26)
#define RT2860_TXOP_ALLOW_GF40		(1 << 25)
#define RT2860_TXOP_ALLOW_GF20		(1 << 24)
#define RT2860_TXOP_ALLOW_MM40		(1 << 23)
#define RT2860_TXOP_ALLOW_MM20		(1 << 22)
#define RT2860_TXOP_ALLOW_OFDM		(1 << 21)
#define RT2860_TXOP_ALLOW_CCK		(1 << 20)
#define RT2860_TXOP_ALLOW_ALL		(0x3f << 20)
#define RT2860_PROT_NAV_SHORT		(1 << 18)
#define RT2860_PROT_NAV_LONG		(2 << 18)
#define RT2860_PROT_CTRL_RTS_CTS	(1 << 16)
#define RT2860_PROT_CTRL_CTS		(2 << 16)

/* possible flags for registers EXP_{CTS,ACK}_TIME */
#define RT2860_EXP_OFDM_TIME_SHIFT	16
#define RT2860_EXP_CCK_TIME_SHIFT	0

/* possible flags for register RX_FILTR_CFG 0x1400 */
#define MTW_DROP_CTRL_RSV	(1 << 16)
#define MTW_DROP_BAR		(1 << 15)
#define MTW_DROP_BA		(1 << 14)
#define MTW_DROP_PSPOLL		(1 << 13)
#define MTW_DROP_RTS		(1 << 12)
#define MTW_DROP_CTS		(1 << 11)
#define MTW_DROP_ACK		(1 << 10)
#define MTW_DROP_CFEND		(1 <<  9)
#define MTW_DROP_CFACK		(1 <<  8)
#define MTW_DROP_DUPL		(1 <<  7)
#define MTW_DROP_BC		(1 <<  6)
#define MTW_DROP_MC		(1 <<  5)
#define MTW_DROP_VER_ERR	(1 <<  4)
#define MTW_DROP_NOT_MYBSS	(1 <<  3)
#define MTW_DROP_UC_NOME	(1 <<  2)
#define MTW_DROP_PHY_ERR	(1 <<  1)
#define MTW_DROP_CRC_ERR	(1 <<  0)

/* possible flags for register AUTO_RSP_CFG 0x1404 */
#define MTW_CTRL_PWR_BIT	(1 << 7)
#define MTW_BAC_ACK_POLICY	(1 << 6)
#define MTW_CCK_SHORT_EN	(1 << 4)
#define MTW_CTS_40M_REF_EN	(1 << 3)
#define MTW_CTS_40M_MODE_EN	(1 << 2)
#define MTW_BAC_ACKPOLICY_EN	(1 << 1)
#define MTW_AUTO_RSP_EN		(1 << 0)

/* possible flags for register SIFS_COST_CFG */
#define RT2860_OFDM_SIFS_COST_SHIFT	8
#define RT2860_CCK_SIFS_COST_SHIFT	0

/* possible flags for register TXOP_HLDR_ET 0x1608 */
#define MTW_TXOP_ETM1_EN		(1 << 25)
#define MTW_TXOP_ETM0_EN		(1 << 24)
#define MTW_TXOP_ETM_THRES_SHIFT	16
#define MTW_TXOP_ETO_EN			(1 <<  8)
#define MTW_TXOP_ETO_THRES_SHIFT	1
#define MTW_PER_RX_RST_EN		(1 <<  0)

/* possible flags for register TX_STAT_FIFO 0x1718 */
#define MTW_TXQ_MCS_SHIFT	16
#define MTW_TXQ_WCID_SHIFT	8
#define MTW_TXQ_ACKREQ		(1 << 7)
#define MTW_TXQ_AGG		(1 << 6)
#define MTW_TXQ_OK		(1 << 5)
#define MTW_TXQ_PID_SHIFT	1
#define MTW_TXQ_VLD		(1 << 0)

/* possible flags for register TX_STAT_FIFO_EXT 0x1798 */
#define MTW_TXQ_PKTID_SHIFT	8
#define MTW_TXQ_RETRY_SHIFT	0

/* possible flags for register WCID_ATTR 0xa800 */
#define MTW_MODE_NOSEC		0
#define MTW_MODE_WEP40		1
#define MTW_MODE_WEP104		2
#define MTW_MODE_TKIP		3
#define MTW_MODE_AES_CCMP	4
#define MTW_MODE_CKIP40		5
#define MTW_MODE_CKIP104	6
#define MTW_MODE_CKIP128	7
#define MTW_RX_PKEY_EN		(1 << 0)

/* possible flags for MT7601 BBP register 47 */
#define MT7601_R47_MASK		0x07
#define MT7601_R47_TSSI		(0 << 0)
#define MT7601_R47_PKT		(1 << 0)
#define MT7601_R47_TXRATE	(1 << 1)
#define MT7601_R47_TEMP		(1 << 2)

#define MTW_RXQ_WLAN		0
#define MTW_RXQ_MCU		1
#define MTW_TXQ_MCU		5

enum mtw_phy_mode {
	MTW_PHY_CCK,
	MTW_PHY_OFDM,
	MTW_PHY_HT,
	MTW_PHY_HT_GF,
	MTW_PHY_VHT,
};

/* RT2860 TX descriptor */
struct rt2860_txd {
	uint32_t	sdp0;		/* Segment Data Pointer 0 */
	uint16_t	sdl1;		/* Segment Data Length 1 */
#define RT2860_TX_BURST	(1 << 15)
#define RT2860_TX_LS1	(1 << 14)	/* SDP1 is the last segment */

	uint16_t	sdl0;		/* Segment Data Length 0 */
#define RT2860_TX_DDONE	(1 << 15)
#define RT2860_TX_LS0	(1 << 14)	/* SDP0 is the last segment */

	uint32_t	sdp1;		/* Segment Data Pointer 1 */
	uint8_t		reserved[3];
	uint8_t		flags;
#define RT2860_TX_QSEL_SHIFT	1
#define RT2860_TX_QSEL_MGMT	(0 << 1)
#define RT2860_TX_QSEL_HCCA	(1 << 1)
#define RT2860_TX_QSEL_EDCA	(2 << 1)
#define RT2860_TX_WIV		(1 << 0)
} __packed;

/* TX descriptor */
struct mtw_txd {
	uint16_t	len;
	uint16_t	flags;
#define MTW_TXD_CMD		(1 << 14)
#define MTW_TXD_DATA		(0 << 14)
#define MTW_TXD_MCU		(2 << 11)
#define MTW_TXD_WLAN		(0 << 11)
#define MTW_TXD_QSEL_EDCA	(2 << 9)
#define MTW_TXD_QSEL_HCCA	(1 << 9)
#define MTW_TXD_QSEL_MGMT	(0 << 9)
#define MTW_TXD_WIV		(1 << 8)
#define MTW_TXD_CMD_SHIFT	4
#define MTW_TXD_80211		(1 << 3)
} __packed;

/* TX Wireless Information */
struct mtw_txwi {
	uint8_t		flags;
#define MTW_TX_MPDU_DSITY_SHIFT	5
#define MTW_TX_AMPDU		(1 << 4)
#define MTW_TX_TS		(1 << 3)
#define MTW_TX_CFACK		(1 << 2)
#define MTW_TX_MMPS		(1 << 1)
#define MTW_TX_FRAG		(1 << 0)

	uint8_t		txop;
#define MTW_TX_TXOP_HT		0
#define MTW_TX_TXOP_PIFS	1
#define MTW_TX_TXOP_SIFS	2
#define MTW_TX_TXOP_BACKOFF	3

	uint16_t	phy;
#define MT7650_PHY_MODE		0xe000
#define MT7601_PHY_MODE		0xc000
#define MT7601_PHY_SHIFT	14
#define MT7650_PHY_SHIFT	13
#define MT7650_PHY_SGI		(1 << 9)
#define MT7601_PHY_SGI		(1 << 8)
#define MTW_PHY_BW20		(0 << 7)
#define MTW_PHY_BW40		(1 << 7)
#define MTW_PHY_BW80		(2 << 7)
#define MTW_PHY_BW160		(3 << 7)
#define MTW_PHY_LDPC		(1 << 6)
#define MTW_PHY_MCS		0x3f
#define MTW_PHY_SHPRE		(1 << 3)

	uint8_t		xflags;
#define MTW_TX_BAWINSIZE_SHIFT	2
#define MTW_TX_NSEQ		(1 << 1)
#define MTW_TX_ACK		(1 << 0)

	uint8_t		wcid;	/* Wireless Client ID */
	uint16_t	len;
#define MTW_TX_PID_SHIFT	12

	uint32_t	iv;
	uint32_t	eiv;
	uint32_t	reserved1;
} __packed;

/* RT2860 RX descriptor */
struct rt2860_rxd {
	uint32_t	sdp0;
	uint16_t	sdl1;	/* unused */
	uint16_t	sdl0;
#define MTW_RX_DDONE		(1 << 15)
#define MTW_RX_LS0		(1 << 14)

	uint32_t	sdp1;	/* unused */
	uint32_t	flags;
#define MTW_RX_DEC		(1 << 16)
#define MTW_RX_AMPDU		(1 << 15)
#define MTW_RX_L2PAD		(1 << 14)
#define MTW_RX_RSSI		(1 << 13)
#define MTW_RX_HTC		(1 << 12)
#define MTW_RX_AMSDU		(1 << 11)
#define MTW_RX_MICERR		(1 << 10)
#define MTW_RX_ICVERR		(1 << 9)
#define MTW_RX_CRCERR		(1 << 8)
#define MTW_RX_MYBSS		(1 << 7)
#define MTW_RX_BC		(1 << 6)
#define MTW_RX_MC		(1 << 5)
#define MTW_RX_UC2ME		(1 << 4)
#define MTW_RX_FRAG		(1 << 3)
#define MTW_RX_NULL		(1 << 2)
#define MTW_RX_DATA		(1 << 1)
#define MTW_RX_BA		(1 << 0)
} __packed;

/* RX descriptor */
struct mtw_rxd {
	uint16_t	len;
#define MTW_RXD_SELF_GEN	(1 << 15)
#define MTW_RXD_LEN		0x3fff

	uint16_t	flags;
} __packed;

/* RX Wireless Information */
struct mtw_rxwi {
	uint32_t	flags;
	uint8_t		wcid;
	uint8_t		keyidx;
#define MTW_RX_UDF_SHIFT	5
#define MTW_RX_BSS_IDX_SHIFT	2

	uint16_t	len;
#define MTW_RX_TID_SHIFT	12

	uint16_t	seq;
	uint16_t	phy;
	uint8_t		rssi[4];
	uint32_t	reserved1;
	uint32_t	reserved2;
	uint32_t	reserved3;
} __packed __aligned(4);

/* MCU Command */
struct mtw_mcu_cmd_8 {
	uint32_t	func;
	uint32_t	val;
} __packed __aligned(4);

struct mtw_mcu_cmd_16 {
	uint32_t	r1;
	uint32_t	r2;
	uint32_t	r3;
	uint32_t	r4;
} __packed __aligned(4);

#define MTW_DMA_PAD	4

/* first DMA segment contains TXWI + 802.11 header + 32-bit padding */
#define MTW_TXWI_DMASZ				\
	(sizeof (struct mtw_txwi) +		\
	 sizeof (struct ieee80211_htframe) +	\
	 sizeof (uint16_t))

#define MT7601_RF_7601	0x7601	/* 1T1R */
#define MT7610_RF_7610	0x7610	/* 1T1R */
#define MT7612_RF_7612	0x7612	/* 2T2R */

#define MTW_CONFIG_NO	1

/* USB vendor request */
#define MTW_RESET			0x1
#define MTW_WRITE_2			0x2
#define MTW_WRITE_REGION_1		0x6
#define MTW_READ_REGION_1		0x7
#define MTW_EEPROM_READ			0x9
#define MTW_WRITE_CFG			0x46
#define MTW_READ_CFG			0x47

/* eFUSE ROM */
#define MTW_EEPROM_CHIPID		0x00
#define MTW_EEPROM_VERSION		0x01
#define MTW_EEPROM_MAC01		0x02
#define MTW_EEPROM_MAC23		0x03
#define MTW_EEPROM_MAC45		0x04
#define MTW_EEPROM_ANTENNA		0x1a
#define MTW_EEPROM_CONFIG		0x1b
#define MTW_EEPROM_COUNTRY		0x1c
#define MTW_EEPROM_FREQ_OFFSET		0x1d
#define MTW_EEPROM_LED1			0x1e
#define MTW_EEPROM_LED2			0x1f
#define MTW_EEPROM_LED3			0x20
#define MTW_EEPROM_LNA			0x22
#define MTW_EEPROM_RSSI1_2GHZ		0x23
#define MTW_EEPROM_RSSI2_2GHZ		0x24
#define MTW_EEPROM_RSSI1_5GHZ		0x25
#define MTW_EEPROM_RSSI2_5GHZ		0x26
#define MTW_EEPROM_DELTAPWR		0x28
#define MTW_EEPROM_PWR2GHZ_BASE1	0x29
#define MTW_EEPROM_PWR2GHZ_BASE2	0x30
#define MTW_EEPROM_TSSI1_2GHZ		0x37
#define MTW_EEPROM_TSSI2_2GHZ		0x38
#define MTW_EEPROM_TSSI3_2GHZ		0x39
#define MTW_EEPROM_TSSI4_2GHZ		0x3a
#define MTW_EEPROM_TSSI5_2GHZ		0x3b
#define MTW_EEPROM_PWR5GHZ_BASE1	0x3c
#define MTW_NIC_CONF2			0x42
#define MTW_EEPROM_PWR5GHZ_BASE2	0x53
#define MTW_TXPWR_EXT_PA_5G		0x54
#define MTW_TXPWR_START_2G_0		0x56
#define MTW_TXPWR_START_2G_1		0x5c
#define MTW_TXPWR_START_5G_0		0x62
#define RT2860_EEPROM_TSSI1_5GHZ	0x6a
#define RT2860_EEPROM_TSSI2_5GHZ	0x6b
#define RT2860_EEPROM_TSSI3_5GHZ	0x6c
#define RT2860_EEPROM_TSSI4_5GHZ	0x6d
#define RT2860_EEPROM_TSSI5_5GHZ	0x6e
#define MTW_TX_TSSI_SLOPE		0x6e
#define MTW_EEPROM_RPWR			0x6f

#define MTW_RIDX_CCK1	 	0
#define MTW_RIDX_CCK11	 	3
#define MTW_RIDX_OFDM6	 	4
#define MTW_RIDX_MAX		11
static const struct rt2860_rate {
	uint8_t		rate;
	uint8_t		mcs;
	enum		ieee80211_phytype phy;
	uint8_t		ctl_ridx;
	uint16_t	sp_ack_dur;
	uint16_t	lp_ack_dur;
} rt2860_rates[] = {
	{   2, 0, IEEE80211_T_DS,   0, 314, 314 },
	{   4, 1, IEEE80211_T_DS,   1, 258, 162 },
	{  11, 2, IEEE80211_T_DS,   2, 223, 127 },
	{  22, 3, IEEE80211_T_DS,   3, 213, 117 },
	{  12, 0, IEEE80211_T_OFDM, 4,  60,  60 },
	{  18, 1, IEEE80211_T_OFDM, 4,  52,  52 },
	{  24, 2, IEEE80211_T_OFDM, 6,  48,  48 },
	{  36, 3, IEEE80211_T_OFDM, 6,  44,  44 },
	{  48, 4, IEEE80211_T_OFDM, 8,  44,  44 },
	{  72, 5, IEEE80211_T_OFDM, 8,  40,  40 },
	{  96, 6, IEEE80211_T_OFDM, 8,  40,  40 },
	{ 108, 7, IEEE80211_T_OFDM, 8,  40,  40 }
};

#define MT7601_RF_CHAN			\
	{  1, 0x99, 0x99, 0x09, 0x50 },	\
	{  2, 0x46, 0x44, 0x0a, 0x50 },	\
	{  3, 0xec, 0xee, 0x0a, 0x50 },	\
	{  4, 0x99, 0x99, 0x0b, 0x50 },	\
	{  5, 0x46, 0x44, 0x08, 0x51 },	\
	{  6, 0xec, 0xee, 0x08, 0x51 },	\
	{  7, 0x99, 0x99, 0x09, 0x51 },	\
	{  8, 0x46, 0x44, 0x0a, 0x51 },	\
	{  9, 0xec, 0xee, 0x0a, 0x51 },	\
	{ 10, 0x99, 0x99, 0x0b, 0x51 },	\
	{ 11, 0x46, 0x44, 0x08, 0x52 },	\
	{ 12, 0xec, 0xee, 0x08, 0x52 },	\
	{ 13, 0x99, 0x99, 0x09, 0x52 },	\
	{ 14, 0x33, 0x33, 0x0b, 0x52 }

/*
 * Default values for MAC registers.
 */
#define MT7601_DEF_MAC					\
	{ MTW_BCN_OFFSET0,		0x18100800 },	\
	{ MTW_BCN_OFFSET1,		0x38302820 },	\
	{ MTW_BCN_OFFSET2,		0x58504840 },	\
	{ MTW_BCN_OFFSET3,		0x78706860 },	\
	{ MTW_MAC_SYS_CTRL,		0x0000000c },	\
	{ MTW_MAX_LEN_CFG,		0x000a3fff },	\
	{ MTW_AMPDU_MAX_LEN_20M1S,	0x77777777 },	\
	{ MTW_AMPDU_MAX_LEN_20M2S,	0x77777777 },	\
	{ MTW_AMPDU_MAX_LEN_40M1S,	0x77777777 },	\
	{ MTW_AMPDU_MAX_LEN_40M2S,	0x77777777 },	\
	{ MTW_XIFS_TIME_CFG,		0x33a41010 },	\
	{ MTW_BKOFF_SLOT_CFG,		0x00000209 },	\
	{ MTW_TBTT_SYNC_CFG,		0x00422010 },	\
	{ MTW_INT_TIMER_CFG,		0x00000000 },	\
	{ MTW_PWR_PIN_CFG,		0x00000000 },	\
	{ MTW_AUTO_WAKEUP_CFG,		0x00000014 },	\
	{ MTW_EDCA_AC_CFG(0),		0x000a4360 },	\
	{ MTW_EDCA_AC_CFG(1),		0x000a4700 },	\
	{ MTW_EDCA_AC_CFG(2),		0x00043338 },	\
	{ MTW_EDCA_AC_CFG(3),		0x0003222f },	\
	{ MTW_TX_PIN_CFG,		0x33150f0f },	\
	{ MTW_TX_BAND_CFG,		0x00000005 },	\
	{ MTW_TX_SW_CFG0,		0x00000402 },	\
	{ MTW_TX_SW_CFG1,		0x00000000 },	\
	{ MTW_TX_SW_CFG2,		0x00000000 },	\
	{ MTW_TXOP_CTRL_CFG,		0x0000583f },	\
	{ MTW_TX_RTS_CFG,		0x01100020 },	\
	{ MTW_TX_TIMEOUT_CFG,		0x000a2090 },	\
	{ MTW_TX_RETRY_CFG,		0x47d01f0f },	\
	{ MTW_TX_LINK_CFG,		0x007f1820 },	\
	{ MTW_HT_FBK_CFG1,		0xedcba980 },	\
	{ MTW_CCK_PROT_CFG,		0x07f40000 },	\
	{ MTW_OFDM_PROT_CFG,		0x07f60000 },	\
	{ MTW_MM20_PROT_CFG,		0x01750003 },	\
	{ MTW_MM40_PROT_CFG,		0x03f50003 },	\
	{ MTW_GF20_PROT_CFG,		0x01750003 },	\
	{ MTW_GF40_PROT_CFG,		0x03f50003 },	\
	{ MTW_EXP_ACK_TIME,		0x002400ca },	\
	{ MTW_TX_PWR_CFG5,		0x00000000 },	\
	{ MTW_TX_PWR_CFG6,		0x01010101 },	\
	{ MTW_TX0_RF_GAIN_CORR,		0x003b0005 },	\
	{ MTW_TX1_RF_GAIN_CORR,		0x00000000 },	\
	{ MTW_TX0_RF_GAIN_ATTEN,	0x00006969 },	\
	{ MTW_TX_ALC_CFG3,		0x6c6c6c6c },	\
	{ MTW_TX_ALC_CFG0,		0x2f2f0005 },	\
	{ MTW_TX_ALC_CFG4,		0x00000400 },	\
	{ MTW_TX_ALC_VGA3,		0x00060006 },	\
	{ MTW_RX_FILTR_CFG,		0x00015f97 },	\
	{ MTW_AUTO_RSP_CFG,		0x00000003 },	\
	{ MTW_LEGACY_BASIC_RATE,	0x0000015f },	\
	{ MTW_HT_BASIC_RATE,		0x00008003 },	\
	{ MTW_RX_MAX_PCNT,		0x0000009f },	\
	{ MTW_WPDMA_GLO_CFG,		0x00000030 },	\
	{ MTW_WMM_AIFSN_CFG,		0x00002273 },	\
	{ MTW_WMM_CWMIN_CFG,		0x00002344 },	\
	{ MTW_WMM_CWMAX_CFG,		0x000034aa },	\
	{ MTW_TSO_CTRL,			0x00000000 },	\
	{ MTW_SYS_CTRL,			0x00080c00 },	\
	{ MTW_FCE_PSE_CTRL,		0x00000001 },	\
	{ MTW_AUX_CLK_CFG,		0x00000000 },	\
	{ MTW_BBP_PA_MODE_CFG0,		0x010055ff },	\
	{ MTW_BBP_PA_MODE_CFG1,		0x00550055 },	\
	{ MTW_RF_PA_MODE_CFG0,		0x010055ff },	\
	{ MTW_RF_PA_MODE_CFG1,		0x00550055 },	\
	{ 0x0a38,			0x00000000 },	\
	{ MTW_BBP_CSR,			0x00000000 },	\
	{ MTW_PBF_CFG,			0x7f723c1f }

/*
 * Default values for Baseband registers
 */
#define MT7601_DEF_BBP	\
	{   1, 0x04 },	\
	{   4, 0x40 },	\
	{  20, 0x06 },	\
	{  31, 0x08 },	\
	{ 178, 0xff },	\
	{  66, 0x14 },	\
	{  68, 0x8b },	\
	{  69, 0x12 },	\
	{  70, 0x09 },	\
	{  73, 0x11 },	\
	{  75, 0x60 },	\
	{  76, 0x44 },	\
	{  84, 0x9a },	\
	{  86, 0x38 },	\
	{  91, 0x07 },	\
	{  92, 0x02 },	\
	{  99, 0x50 },	\
	{ 101, 0x00 },	\
	{ 103, 0xc0 },	\
	{ 104, 0x92 },	\
	{ 105, 0x3c },	\
	{ 106, 0x03 },	\
	{ 128, 0x12 },	\
	{ 142, 0x04 },	\
	{ 143, 0x37 },	\
	{ 142, 0x03 },	\
	{ 143, 0x99 },	\
	{ 160, 0xeb },	\
	{ 161, 0xc4 },	\
	{ 162, 0x77 },	\
	{ 163, 0xf9 },	\
	{ 164, 0x88 },	\
	{ 165, 0x80 },	\
	{ 166, 0xff },	\
	{ 167, 0xe4 },	\
	{ 195, 0x00 },	\
	{ 196, 0x00 },	\
	{ 195, 0x01 },	\
	{ 196, 0x04 },	\
	{ 195, 0x02 },	\
	{ 196, 0x20 },	\
	{ 195, 0x03 },	\
	{ 196, 0x0a },	\
	{ 195, 0x06 },	\
	{ 196, 0x16 },	\
	{ 195, 0x07 },	\
	{ 196, 0x05 },	\
	{ 195, 0x08 },	\
	{ 196, 0x37 },	\
	{ 195, 0x0a },	\
	{ 196, 0x15 },	\
	{ 195, 0x0b },	\
	{ 196, 0x17 },	\
	{ 195, 0x0c },	\
	{ 196, 0x06 },	\
	{ 195, 0x0d },	\
	{ 196, 0x09 },	\
	{ 195, 0x0e },	\
	{ 196, 0x05 },	\
	{ 195, 0x0f },	\
	{ 196, 0x09 },	\
	{ 195, 0x10 },	\
	{ 196, 0x20 },	\
	{ 195, 0x20 },	\
	{ 196, 0x17 },	\
	{ 195, 0x21 },	\
	{ 196, 0x06 },	\
	{ 195, 0x22 },	\
	{ 196, 0x09 },	\
	{ 195, 0x23 },	\
	{ 196, 0x17 },	\
	{ 195, 0x24 },	\
	{ 196, 0x06 },	\
	{ 195, 0x25 },	\
	{ 196, 0x09 },	\
	{ 195, 0x26 },	\
	{ 196, 0x17 },	\
	{ 195, 0x27 },	\
	{ 196, 0x06 },	\
	{ 195, 0x28 },	\
	{ 196, 0x09 },	\
	{ 195, 0x29 },	\
	{ 196, 0x05 },	\
	{ 195, 0x2a },	\
	{ 196, 0x09 },	\
	{ 195, 0x80 },	\
	{ 196, 0x8b },	\
	{ 195, 0x81 },	\
	{ 196, 0x12 },	\
	{ 195, 0x82 },	\
	{ 196, 0x09 },	\
	{ 195, 0x83 },	\
	{ 196, 0x17 },	\
	{ 195, 0x84 },	\
	{ 196, 0x11 },	\
	{ 195, 0x85 },	\
	{ 196, 0x00 },	\
	{ 195, 0x86 },	\
	{ 196, 0x00 },	\
	{ 195, 0x87 },	\
	{ 196, 0x18 },	\
	{ 195, 0x88 },	\
	{ 196, 0x60 },	\
	{ 195, 0x89 },	\
	{ 196, 0x44 },	\
	{ 195, 0x8a },	\
	{ 196, 0x8b },	\
	{ 195, 0x8b },	\
	{ 196, 0x8b },	\
	{ 195, 0x8c },	\
	{ 196, 0x8b },	\
	{ 195, 0x8d },	\
	{ 196, 0x8b },	\
	{ 195, 0x8e },	\
	{ 196, 0x09 },	\
	{ 195, 0x8f },	\
	{ 196, 0x09 },	\
	{ 195, 0x90 },	\
	{ 196, 0x09 },	\
	{ 195, 0x91 },	\
	{ 196, 0x09 },	\
	{ 195, 0x92 },	\
	{ 196, 0x11 },	\
	{ 195, 0x93 },	\
	{ 196, 0x11 },	\
	{ 195, 0x94 },	\
	{ 196, 0x11 },	\
	{ 195, 0x95 },	\
	{ 196, 0x11 },	\
	{  47, 0x80 },	\
	{  60, 0x80 },	\
	{ 150, 0xd2 },	\
	{ 151, 0x32 },	\
	{ 152, 0x23 },	\
	{ 153, 0x41 },	\
	{ 154, 0x00 },	\
	{ 155, 0x4f },	\
	{ 253, 0x7e },	\
	{ 195, 0x30 },	\
	{ 196, 0x32 },	\
	{ 195, 0x31 },	\
	{ 196, 0x23 },	\
	{ 195, 0x32 },	\
	{ 196, 0x45 },	\
	{ 195, 0x35 },	\
	{ 196, 0x4a },	\
	{ 195, 0x36 },	\
	{ 196, 0x5a },	\
	{ 195, 0x37 },	\
	{ 196, 0x5a }

/*
 * Default values for RF registers
 */
#define MT7601_BANK0_RF	\
	{  0, 0x02 },	\
	{  1, 0x01 },	\
	{  2, 0x11 },	\
	{  3, 0xff },	\
	{  4, 0x0a },	\
	{  5, 0x20 },	\
	{  6, 0x00 },	\
	{  7, 0x00 },	\
	{  8, 0x00 },	\
	{  9, 0x00 },	\
	{ 10, 0x00 },	\
	{ 11, 0x21 },	\
	{ 13, 0x00 },	\
	{ 14, 0x7c },	\
	{ 15, 0x22 },	\
	{ 16, 0x80 },	\
	{ 17, 0x99 },	\
	{ 18, 0x99 },	\
	{ 19, 0x09 },	\
	{ 20, 0x50 },	\
	{ 21, 0xb0 },	\
	{ 22, 0x00 },	\
	{ 23, 0xc5 },	\
	{ 24, 0xfc },	\
	{ 25, 0x40 },	\
	{ 26, 0x4d },	\
	{ 27, 0x02 },	\
	{ 28, 0x72 },	\
	{ 29, 0x01 },	\
	{ 30, 0x00 },	\
	{ 31, 0x00 },	\
	{ 32, 0x00 },	\
	{ 33, 0x00 },	\
	{ 34, 0x23 },	\
	{ 35, 0x01 },	\
	{ 36, 0x00 },	\
	{ 37, 0x00 },	\
	{ 38, 0x00 },	\
	{ 39, 0x20 },	\
	{ 40, 0x00 },	\
	{ 41, 0xd0 },	\
	{ 42, 0x1b },	\
	{ 43, 0x02 },	\
	{ 44, 0x00 }

#define MT7601_BANK4_RF	\
	{  0, 0x01 },	\
	{  1, 0x00 },	\
	{  2, 0x00 },	\
	{  3, 0x00 },	\
	{  4, 0x00 },	\
	{  5, 0x08 },	\
	{  6, 0x00 },	\
	{  7, 0x5b },	\
	{  8, 0x52 },	\
	{  9, 0xb6 },	\
	{ 10, 0x57 },	\
	{ 11, 0x33 },	\
	{ 12, 0x22 },	\
	{ 13, 0x3d },	\
	{ 14, 0x3e },	\
	{ 15, 0x13 },	\
	{ 16, 0x22 },	\
	{ 17, 0x23 },	\
	{ 18, 0x02 },	\
	{ 19, 0xa4 },	\
	{ 20, 0x01 },	\
	{ 21, 0x12 },	\
	{ 22, 0x80 },	\
	{ 23, 0xb3 },	\
	{ 24, 0x00 },	\
	{ 25, 0x00 },	\
	{ 26, 0x00 },	\
	{ 27, 0x00 },	\
	{ 28, 0x18 },	\
	{ 29, 0xee },	\
	{ 30, 0x6b },	\
	{ 31, 0x31 },	\
	{ 32, 0x5d },	\
	{ 33, 0x00 },	\
	{ 34, 0x96 },	\
	{ 35, 0x55 },	\
	{ 36, 0x08 },	\
	{ 37, 0xbb },	\
	{ 38, 0xb3 },	\
	{ 39, 0xb3 },	\
	{ 40, 0x03 },	\
	{ 41, 0x00 },	\
	{ 42, 0x00 },	\
	{ 43, 0xc5 },	\
	{ 44, 0xc5 },	\
	{ 45, 0xc5 },	\
	{ 46, 0x07 },	\
	{ 47, 0xa8 },	\
	{ 48, 0xef },	\
	{ 49, 0x1a },	\
	{ 54, 0x07 },	\
	{ 55, 0xa7 },	\
	{ 56, 0xcc },	\
	{ 57, 0x14 },	\
	{ 58, 0x07 },	\
	{ 59, 0xa8 },	\
	{ 60, 0xd7 },	\
	{ 61, 0x10 },	\
	{ 62, 0x1c },	\
	{ 63, 0x00 }

#define MT7601_BANK5_RF	\
	{  0, 0x47 },	\
	{  1, 0x00 },	\
	{  2, 0x00 },	\
	{  3, 0x08 },	\
	{  4, 0x04 },	\
	{  5, 0x20 },	\
	{  6, 0x3a },	\
	{  7, 0x3a },	\
	{  8, 0x00 },	\
	{  9, 0x00 },	\
	{ 10, 0x10 },	\
	{ 11, 0x10 },	\
	{ 12, 0x10 },	\
	{ 13, 0x10 },	\
	{ 14, 0x10 },	\
	{ 15, 0x20 },	\
	{ 16, 0x22 },	\
	{ 17, 0x7c },	\
	{ 18, 0x00 },	\
	{ 19, 0x00 },	\
	{ 20, 0x00 },	\
	{ 21, 0xf1 },	\
	{ 22, 0x11 },	\
	{ 23, 0x02 },	\
	{ 24, 0x41 },	\
	{ 25, 0x20 },	\
	{ 26, 0x00 },	\
	{ 27, 0xd7 },	\
	{ 28, 0xa2 },	\
	{ 29, 0x20 },	\
	{ 30, 0x49 },	\
	{ 31, 0x20 },	\
	{ 32, 0x04 },	\
	{ 33, 0xf1 },	\
	{ 34, 0xa1 },	\
	{ 35, 0x01 },	\
	{ 41, 0x00 },	\
	{ 42, 0x00 },	\
	{ 43, 0x00 },	\
	{ 44, 0x00 },	\
	{ 45, 0x00 },	\
	{ 46, 0x00 },	\
	{ 47, 0x00 },	\
	{ 48, 0x00 },	\
	{ 49, 0x00 },	\
	{ 50, 0x00 },	\
	{ 51, 0x00 },	\
	{ 52, 0x00 },	\
	{ 53, 0x00 },	\
	{ 54, 0x00 },	\
	{ 55, 0x00 },	\
	{ 56, 0x00 },	\
	{ 57, 0x00 },	\
	{ 58, 0x31 },	\
	{ 59, 0x31 },	\
	{ 60, 0x0a },	\
	{ 61, 0x02 },	\
	{ 62, 0x00 },	\
	{ 63, 0x00 }
