/*	$OpenBSD: rt2860reg.h,v 1.35 2018/10/02 02:05:34 kevlo Exp $	*/

/*-
 * Copyright (c) 2007
 *	Damien Bergamini <damien.bergamini@free.fr>
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

/* PCI registers */
#define RT2860_PCI_CFG			0x0000
#define RT2860_PCI_EECTRL		0x0004
#define RT2860_PCI_MCUCTRL		0x0008
#define RT2860_PCI_SYSCTRL		0x000c
#define RT2860_PCIE_JTAG		0x0010

/* RT3290 registers */
#define RT3290_CMB_CTRL			0x0020
#define RT3290_EFUSE_CTRL		0x0024
#define RT3290_EFUSE_DATA3		0x0028
#define RT3290_EFUSE_DATA2		0x002c
#define RT3290_EFUSE_DATA1		0x0030
#define RT3290_EFUSE_DATA0		0x0034
#define RT3290_OSC_CTRL			0x0038
#define RT3290_COEX_CFG0		0x0040
#define RT3290_PLL_CTRL			0x0050
#define RT3290_WLAN_CTRL		0x0080

#define RT3090_AUX_CTRL			0x010c

#define RT3070_OPT_14			0x0114

/* SCH/DMA registers */
#define RT2860_INT_STATUS		0x0200
#define RT2860_INT_MASK			0x0204
#define RT2860_WPDMA_GLO_CFG		0x0208
#define RT2860_WPDMA_RST_IDX		0x020c
#define RT2860_DELAY_INT_CFG		0x0210
#define RT2860_WMM_AIFSN_CFG		0x0214
#define RT2860_WMM_CWMIN_CFG		0x0218
#define RT2860_WMM_CWMAX_CFG		0x021c
#define RT2860_WMM_TXOP0_CFG		0x0220
#define RT2860_WMM_TXOP1_CFG		0x0224
#define RT2860_GPIO_CTRL		0x0228
#define RT2860_MCU_CMD_REG		0x022c
#define RT2860_TX_BASE_PTR(qid)		(0x0230 + (qid) * 16)
#define RT2860_TX_MAX_CNT(qid)		(0x0234 + (qid) * 16)
#define RT2860_TX_CTX_IDX(qid)		(0x0238 + (qid) * 16)
#define RT2860_TX_DTX_IDX(qid)		(0x023c + (qid) * 16)
#define RT2860_RX_BASE_PTR		0x0290
#define RT2860_RX_MAX_CNT		0x0294
#define RT2860_RX_CALC_IDX		0x0298
#define RT2860_FS_DRX_IDX		0x029c
#define RT2860_USB_DMA_CFG		0x02a0	/* RT2870 only */
#define RT2860_US_CYC_CNT		0x02a4

/* PBF registers */
#define RT2860_SYS_CTRL			0x0400
#define RT2860_HOST_CMD			0x0404
#define RT2860_PBF_CFG			0x0408
#define RT2860_MAX_PCNT			0x040c
#define RT2860_BUF_CTRL			0x0410
#define RT2860_MCU_INT_STA		0x0414
#define RT2860_MCU_INT_ENA		0x0418
#define RT2860_TXQ_IO(qid)		(0x041c + (qid) * 4)
#define RT2860_RX0Q_IO			0x0424
#define RT2860_BCN_OFFSET0		0x042c
#define RT2860_BCN_OFFSET1		0x0430
#define RT2860_TXRXQ_STA		0x0434
#define RT2860_TXRXQ_PCNT		0x0438
#define RT2860_PBF_DBG			0x043c
#define RT2860_CAP_CTRL			0x0440

/* RT3070 registers */
#define RT3070_RF_CSR_CFG		0x0500
#define RT3070_EFUSE_CTRL		0x0580
#define RT3070_EFUSE_DATA0		0x0590
#define RT3070_EFUSE_DATA1		0x0594
#define RT3070_EFUSE_DATA2		0x0598
#define RT3070_EFUSE_DATA3		0x059c
#define RT3090_OSC_CTRL			0x05a4
#define RT3070_LDO_CFG0			0x05d4
#define RT3070_GPIO_SWITCH		0x05dc

/* RT5592 registers */
#define RT5592_DEBUG_INDEX		0x05e8

/* MAC registers */
#define RT2860_ASIC_VER_ID		0x1000
#define RT2860_MAC_SYS_CTRL		0x1004
#define RT2860_MAC_ADDR_DW0		0x1008
#define RT2860_MAC_ADDR_DW1		0x100c
#define RT2860_MAC_BSSID_DW0		0x1010
#define RT2860_MAC_BSSID_DW1		0x1014
#define RT2860_MAX_LEN_CFG		0x1018
#define RT2860_BBP_CSR_CFG		0x101c
#define RT2860_RF_CSR_CFG0		0x1020
#define RT2860_RF_CSR_CFG1		0x1024
#define RT2860_RF_CSR_CFG2		0x1028
#define RT2860_LED_CFG			0x102c

/* undocumented registers */
#define RT2860_DEBUG			0x10f4

/* MAC Timing control registers */
#define RT2860_XIFS_TIME_CFG		0x1100
#define RT2860_BKOFF_SLOT_CFG		0x1104
#define RT2860_NAV_TIME_CFG		0x1108
#define RT2860_CH_TIME_CFG		0x110c
#define RT2860_PBF_LIFE_TIMER		0x1110
#define RT2860_BCN_TIME_CFG		0x1114
#define RT2860_TBTT_SYNC_CFG		0x1118
#define RT2860_TSF_TIMER_DW0		0x111c
#define RT2860_TSF_TIMER_DW1		0x1120
#define RT2860_TBTT_TIMER		0x1124
#define RT2860_INT_TIMER_CFG		0x1128
#define RT2860_INT_TIMER_EN		0x112c
#define RT2860_CH_IDLE_TIME		0x1130

/* MAC Power Save configuration registers */
#define RT2860_MAC_STATUS_REG		0x1200
#define RT2860_PWR_PIN_CFG		0x1204
#define RT2860_AUTO_WAKEUP_CFG		0x1208

/* MAC TX configuration registers */
#define RT2860_EDCA_AC_CFG(aci)		(0x1300 + (aci) * 4)
#define RT2860_EDCA_TID_AC_MAP		0x1310
#define RT2860_TX_PWR_CFG(ridx)		(0x1314 + (ridx) * 4)
#define RT2860_TX_PIN_CFG		0x1328
#define RT2860_TX_BAND_CFG		0x132c
#define RT2860_TX_SW_CFG0		0x1330
#define RT2860_TX_SW_CFG1		0x1334
#define RT2860_TX_SW_CFG2		0x1338
#define RT2860_TXOP_THRES_CFG		0x133c
#define RT2860_TXOP_CTRL_CFG		0x1340
#define RT2860_TX_RTS_CFG		0x1344
#define RT2860_TX_TIMEOUT_CFG		0x1348
#define RT2860_TX_RTY_CFG		0x134c
#define RT2860_TX_LINK_CFG		0x1350
#define RT2860_HT_FBK_CFG0		0x1354
#define RT2860_HT_FBK_CFG1		0x1358
#define RT2860_LG_FBK_CFG0		0x135c
#define RT2860_LG_FBK_CFG1		0x1360
#define RT2860_CCK_PROT_CFG		0x1364
#define RT2860_OFDM_PROT_CFG		0x1368
#define RT2860_MM20_PROT_CFG		0x136c
#define RT2860_MM40_PROT_CFG		0x1370
#define RT2860_GF20_PROT_CFG		0x1374
#define RT2860_GF40_PROT_CFG		0x1378
#define RT2860_EXP_CTS_TIME		0x137c
#define RT2860_EXP_ACK_TIME		0x1380

/* MAC RX configuration registers */
#define RT2860_RX_FILTR_CFG		0x1400
#define RT2860_AUTO_RSP_CFG		0x1404
#define RT2860_LEGACY_BASIC_RATE	0x1408
#define RT2860_HT_BASIC_RATE		0x140c
#define RT2860_HT_CTRL_CFG		0x1410
#define RT2860_SIFS_COST_CFG		0x1414
#define RT2860_RX_PARSER_CFG		0x1418

/* MAC Security configuration registers */
#define RT2860_TX_SEC_CNT0		0x1500
#define RT2860_RX_SEC_CNT0		0x1504
#define RT2860_CCMP_FC_MUTE		0x1508

/* MAC HCCA/PSMP configuration registers */
#define RT2860_TXOP_HLDR_ADDR0		0x1600
#define RT2860_TXOP_HLDR_ADDR1		0x1604
#define RT2860_TXOP_HLDR_ET		0x1608
#define RT2860_QOS_CFPOLL_RA_DW0	0x160c
#define RT2860_QOS_CFPOLL_A1_DW1	0x1610
#define RT2860_QOS_CFPOLL_QC		0x1614

/* MAC Statistics Counters */
#define RT2860_RX_STA_CNT0		0x1700
#define RT2860_RX_STA_CNT1		0x1704
#define RT2860_RX_STA_CNT2		0x1708
#define RT2860_TX_STA_CNT0		0x170c
#define RT2860_TX_STA_CNT1		0x1710
#define RT2860_TX_STA_CNT2		0x1714
#define RT2860_TX_STAT_FIFO		0x1718

/* RX WCID search table */
#define RT2860_WCID_ENTRY(wcid)		(0x1800 + (wcid) * 8)

#define RT2860_FW_BASE			0x2000
#define RT2870_FW_BASE			0x3000

/* Pair-wise key table */
#define RT2860_PKEY(wcid)		(0x4000 + (wcid) * 32)

/* IV/EIV table */
#define RT2860_IVEIV(wcid)		(0x6000 + (wcid) * 8)

/* WCID attribute table */
#define RT2860_WCID_ATTR(wcid)		(0x6800 + (wcid) * 4)

/* Shared Key Table */
#define RT2860_SKEY(vap, kidx)		(0x6c00 + (vap) * 128 + (kidx) * 32)

/* Shared Key Mode */
#define RT2860_SKEY_MODE_0_7		0x7000
#define RT2860_SKEY_MODE_8_15		0x7004
#define RT2860_SKEY_MODE_16_23		0x7008
#define RT2860_SKEY_MODE_24_31		0x700c

/* Shared Memory between MCU and host */
#define RT2860_H2M_MAILBOX		0x7010
#define RT2860_H2M_MAILBOX_CID		0x7014
#define RT2860_H2M_MAILBOX_STATUS	0x701c
#define RT2860_H2M_INTSRC		0x7024
#define RT2860_H2M_BBPAGENT		0x7028
#define RT2860_BCN_BASE(vap)		(0x7800 + (vap) * 512)


/* possible flags for RT2860_PCI_CFG */
#define RT2860_PCI_CFG_USB	(1 << 17)
#define RT2860_PCI_CFG_PCI	(1 << 16)

/* possible flags for register RT2860_PCI_EECTRL */
#define RT2860_C	(1 << 0)
#define RT2860_S	(1 << 1)
#define RT2860_D	(1 << 2)
#define RT2860_SHIFT_D	2
#define RT2860_Q	(1 << 3)
#define RT2860_SHIFT_Q	3

/* possible flags for register RT3290_CMB_CTRL */
#define RT3290_XTAL_RDY		(1U << 22)
#define RT3290_PLL_LD		(1U << 23)
#define RT3290_LDO_CORE_LEVEL	(0xf << 24)
#define RT3290_LDO_BGSEL	(3 << 29)
#define RT3290_LDO3_EN		(1U << 30)
#define RT3290_LDO0_EN		(1U << 31)

/* possible flags for register RT3290_OSC_CTRL */
#define RT3290_OSC_REF_CYCLE	0x1fff
#define RT3290_OSC_CAL_CNT	(0xfff << 16)
#define RT3290_OSC_CAL_ACK	(1U << 28)
#define RT3290_OSC_CLK_32K_VLD	(1U << 29)
#define RT3290_OSC_CAL_REQ	(1U << 30)
#define RT3290_ROSC_EN		(1U << 31)

/* possible flags for register RT3290_COEX_CFG0 */
#define RT3290_CFG0_DEF		(0x59 << 24)

/* possible flags for register RT3290_PLL_CTRL */
#define RT3290_PLL_CONTROL	 	(7 << 16)

/* possible flags for register RT3290_WLAN_CTRL */
#define RT3290_WLAN_EN			(1U << 0)
#define RT3290_WLAN_CLK_EN		(1U << 1)
#define RT3290_WLAN_RSV1		(1U << 2)
#define RT3290_WLAN_RESET		(1U << 3)
#define RT3290_PCIE_APP0_CLK_REQ	(1U << 4)
#define RT3290_FRC_WL_ANT_SET		(1U << 5)
#define RT3290_INV_TR_SW0		(1U << 6)
#define RT3290_RADIO_EN			(1U << 8)
#define RT3290_GPIO_IN_ALL		(0xff << 8)
#define RT3290_GPIO_OUT_ALL		(0xff << 16)
#define RT3290_GPIO_OUT_OE_ALL		(0xff << 24)

/* possible flags for registers INT_STATUS/INT_MASK */
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

/* possible flags for register WPDMA_GLO_CFG */
#define RT2860_HDR_SEG_LEN_SHIFT	8
#define RT2860_BIG_ENDIAN		(1 << 7)
#define RT2860_TX_WB_DDONE		(1 << 6)
#define RT2860_WPDMA_BT_SIZE_SHIFT	4
#define RT2860_WPDMA_BT_SIZE16		0
#define RT2860_WPDMA_BT_SIZE32		1
#define RT2860_WPDMA_BT_SIZE64		2
#define RT2860_WPDMA_BT_SIZE128		3
#define RT2860_RX_DMA_BUSY		(1 << 3)
#define RT2860_RX_DMA_EN		(1 << 2)
#define RT2860_TX_DMA_BUSY		(1 << 1)
#define RT2860_TX_DMA_EN		(1 << 0)

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

/* possible flags for register USB_DMA_CFG */
#define RT2860_USB_TX_BUSY		(1U << 31)
#define RT2860_USB_RX_BUSY		(1U << 30)
#define RT2860_USB_EPOUT_VLD_SHIFT	24
#define RT2860_USB_TX_EN		(1U << 23)
#define RT2860_USB_RX_EN		(1U << 22)
#define RT2860_USB_RX_AGG_EN		(1U << 21)
#define RT2860_USB_TXOP_HALT		(1U << 20)
#define RT2860_USB_TX_CLEAR		(1U << 19)
#define RT2860_USB_PHY_WD_EN		(1U << 16)
#define RT2860_USB_PHY_MAN_RST		(1U << 15)
#define RT2860_USB_RX_AGG_LMT(x)	((x) << 8)	/* in unit of 1KB */
#define RT2860_USB_RX_AGG_TO(x)		((x) & 0xff)	/* in unit of 33ns */

/* possible flags for register US_CYC_CNT */
#define RT2860_TEST_EN		(1 << 24)
#define RT2860_TEST_SEL_SHIFT	16
#define RT2860_BT_MODE_EN	(1 <<  8)
#define RT2860_US_CYC_CNT_SHIFT	0

/* possible flags for register SYS_CTRL */
#define RT2860_HST_PM_SEL	(1 << 16)
#define RT2860_CAP_MODE		(1 << 14)
#define RT2860_PME_OEN		(1 << 13)
#define RT2860_CLKSELECT	(1 << 12)
#define RT2860_PBF_CLK_EN	(1 << 11)
#define RT2860_MAC_CLK_EN	(1 << 10)
#define RT2860_DMA_CLK_EN	(1 <<  9)
#define RT2860_MCU_READY	(1 <<  7)
#define RT2860_ASY_RESET	(1 <<  4)
#define RT2860_PBF_RESET	(1 <<  3)
#define RT2860_MAC_RESET	(1 <<  2)
#define RT2860_DMA_RESET	(1 <<  1)
#define RT2860_MCU_RESET	(1 <<  0)

/* possible values for register HOST_CMD */
#define RT2860_MCU_CMD_SLEEP	0x30
#define RT2860_MCU_CMD_WAKEUP	0x31
#define RT2860_MCU_CMD_LEDS	0x50
#define RT2860_MCU_CMD_LED_RSSI	0x51
#define RT2860_MCU_CMD_LED1	0x52
#define RT2860_MCU_CMD_LED2	0x53
#define RT2860_MCU_CMD_LED3	0x54
#define RT2860_MCU_CMD_RFRESET	0x72
#define RT2860_MCU_CMD_ANTSEL	0x73
#define RT2860_MCU_CMD_BBP	0x80
#define RT2860_MCU_CMD_PSLEVEL	0x83

/* possible flags for register PBF_CFG */
#define RT2860_TX1Q_NUM_SHIFT	21
#define RT2860_TX2Q_NUM_SHIFT	16
#define RT2860_NULL0_MODE	(1 << 15)
#define RT2860_NULL1_MODE	(1 << 14)
#define RT2860_RX_DROP_MODE	(1 << 13)
#define RT2860_TX0Q_MANUAL	(1 << 12)
#define RT2860_TX1Q_MANUAL	(1 << 11)
#define RT2860_TX2Q_MANUAL	(1 << 10)
#define RT2860_RX0Q_MANUAL	(1 <<  9)
#define RT2860_HCCA_EN		(1 <<  8)
#define RT2860_TX0Q_EN		(1 <<  4)
#define RT2860_TX1Q_EN		(1 <<  3)
#define RT2860_TX2Q_EN		(1 <<  2)
#define RT2860_RX0Q_EN		(1 <<  1)

/* possible flags for register BUF_CTRL */
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

/* possible flags for register TXRXQ_PCNT */
#define RT2860_RX0Q_PCNT_MASK	0xff000000
#define RT2860_TX2Q_PCNT_MASK	0x00ff0000
#define RT2860_TX1Q_PCNT_MASK	0x0000ff00
#define RT2860_TX0Q_PCNT_MASK	0x000000ff

/* possible flags for register CAP_CTRL */
#define RT2860_CAP_ADC_FEQ		(1U << 31)
#define RT2860_CAP_START		(1U << 30)
#define RT2860_MAN_TRIG			(1U << 29)
#define RT2860_TRIG_OFFSET_SHIFT	16
#define RT2860_START_ADDR_SHIFT		0

/* possible flags for register RF_CSR_CFG */
#define RT3070_RF_KICK		(1 << 17)
#define RT3070_RF_WRITE		(1 << 16)

/* possible flags for register EFUSE_CTRL */
#define RT3070_SEL_EFUSE	(1U << 31)
#define RT3070_EFSROM_KICK	(1U << 30)
#define RT3070_EFSROM_AIN_MASK	0x03ff0000
#define RT3070_EFSROM_AIN_SHIFT	16
#define RT3070_EFSROM_MODE_MASK	0x000000c0
#define RT3070_EFUSE_AOUT_MASK	0x0000003f

/* possible flag for register DEBUG_INDEX */
#define RT5592_SEL_XTAL		(1U << 31)

/* possible flags for register MAC_SYS_CTRL */
#define RT2860_RX_TS_EN		(1 << 7)
#define RT2860_WLAN_HALT_EN	(1 << 6)
#define RT2860_PBF_LOOP_EN	(1 << 5)
#define RT2860_CONT_TX_TEST	(1 << 4)
#define RT2860_MAC_RX_EN	(1 << 3)
#define RT2860_MAC_TX_EN	(1 << 2)
#define RT2860_BBP_HRST		(1 << 1)
#define RT2860_MAC_SRST		(1 << 0)

/* possible flags for register MAC_BSSID_DW1 */
#define RT2860_MULTI_BCN_NUM_SHIFT	18
#define RT2860_MULTI_BSSID_MODE_SHIFT	16

/* possible flags for register MAX_LEN_CFG */
#define RT2860_MIN_MPDU_LEN_SHIFT	16
#define RT2860_MAX_PSDU_LEN_SHIFT	12
#define RT2860_MAX_PSDU_LEN8K		0
#define RT2860_MAX_PSDU_LEN16K		1
#define RT2860_MAX_PSDU_LEN32K		2
#define RT2860_MAX_PSDU_LEN64K		3
#define RT2860_MAX_MPDU_LEN_SHIFT	0

/* possible flags for registers BBP_CSR_CFG/H2M_BBPAGENT */
#define RT2860_BBP_RW_PARALLEL		(1 << 19)
#define RT2860_BBP_PAR_DUR_112_5	(1 << 18)
#define RT2860_BBP_CSR_KICK		(1 << 17)
#define RT2860_BBP_CSR_READ		(1 << 16)
#define RT2860_BBP_ADDR_SHIFT		8
#define RT2860_BBP_DATA_SHIFT		0

/* possible flags for register RF_CSR_CFG0 */
#define RT2860_RF_REG_CTRL		(1U << 31)
#define RT2860_RF_LE_SEL1		(1U << 30)
#define RT2860_RF_LE_STBY		(1U << 29)
#define RT2860_RF_REG_WIDTH_SHIFT	24
#define RT2860_RF_REG_0_SHIFT		0

/* possible flags for register RF_CSR_CFG1 */
#define RT2860_RF_DUR_5		(1 << 24)
#define RT2860_RF_REG_1_SHIFT	0

/* possible flags for register LED_CFG */
#define RT2860_LED_POL			(1 << 30)
#define RT2860_Y_LED_MODE_SHIFT		28
#define RT2860_G_LED_MODE_SHIFT		26
#define RT2860_R_LED_MODE_SHIFT		24
#define RT2860_LED_MODE_OFF		0
#define RT2860_LED_MODE_BLINK_TX	1
#define RT2860_LED_MODE_SLOW_BLINK	2
#define RT2860_LED_MODE_ON		3
#define RT2860_SLOW_BLK_TIME_SHIFT	16
#define RT2860_LED_OFF_TIME_SHIFT	8
#define RT2860_LED_ON_TIME_SHIFT	0

/* possible flags for register XIFS_TIME_CFG */
#define RT2860_BB_RXEND_EN		(1 << 29)
#define RT2860_EIFS_TIME_SHIFT		20
#define RT2860_OFDM_XIFS_TIME_SHIFT	16
#define RT2860_OFDM_SIFS_TIME_SHIFT	8
#define RT2860_CCK_SIFS_TIME_SHIFT	0

/* possible flags for register BKOFF_SLOT_CFG */
#define RT2860_CC_DELAY_TIME_SHIFT	8
#define RT2860_SLOT_TIME		0

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

/* possible values for register BCN_TIME_CFG */
#define RT2860_TSF_INS_COMP_SHIFT	24
#define RT2860_BCN_TX_EN		(1 << 20)
#define RT2860_TBTT_TIMER_EN		(1 << 19)
#define RT2860_TSF_SYNC_MODE_SHIFT	17
#define RT2860_TSF_SYNC_MODE_DIS	0
#define RT2860_TSF_SYNC_MODE_STA	1
#define RT2860_TSF_SYNC_MODE_IBSS	2
#define RT2860_TSF_SYNC_MODE_HOSTAP	3
#define RT2860_TSF_TIMER_EN		(1 << 16)
#define RT2860_BCN_INTVAL_SHIFT		0

/* possible flags for register TBTT_SYNC_CFG */
#define RT2860_BCN_CWMIN_SHIFT		20
#define RT2860_BCN_AIFSN_SHIFT		16
#define RT2860_BCN_EXP_WIN_SHIFT	8
#define RT2860_TBTT_ADJUST_SHIFT	0

/* possible flags for register INT_TIMER_CFG */
#define RT2860_GP_TIMER_SHIFT		16
#define RT2860_PRE_TBTT_TIMER_SHIFT	0

/* possible flags for register INT_TIMER_EN */
#define RT2860_GP_TIMER_EN	(1 << 1)
#define RT2860_PRE_TBTT_INT_EN	(1 << 0)

/* possible flags for register MAC_STATUS_REG */
#define RT2860_RX_STATUS_BUSY	(1 << 1)
#define RT2860_TX_STATUS_BUSY	(1 << 0)

/* possible flags for register PWR_PIN_CFG */
#define RT2860_IO_ADDA_PD	(1 << 3)
#define RT2860_IO_PLL_PD	(1 << 2)
#define RT2860_IO_RA_PE		(1 << 1)
#define RT2860_IO_RF_PE		(1 << 0)

/* possible flags for register AUTO_WAKEUP_CFG */
#define RT2860_AUTO_WAKEUP_EN		(1 << 15)
#define RT2860_SLEEP_TBTT_NUM_SHIFT	8
#define RT2860_WAKEUP_LEAD_TIME_SHIFT	0

/* possible flags for register TX_PIN_CFG */
#define RT3593_LNA_PE_G2_POL	(1U << 31)
#define RT3593_LNA_PE_A2_POL	(1U << 30)
#define RT3593_LNA_PE_G2_EN	(1U << 29)
#define RT3593_LNA_PE_A2_EN	(1U << 28)
#define RT3593_LNA_PE2_EN	(RT3593_LNA_PE_A2_EN | RT3593_LNA_PE_G2_EN)
#define RT3593_PA_PE_G2_POL	(1U << 27)
#define RT3593_PA_PE_A2_POL	(1U << 26)
#define RT3593_PA_PE_G2_EN	(1U << 25)
#define RT3593_PA_PE_A2_EN	(1U << 24)
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

/* possible flags for register TX_BAND_CFG */
#define RT2860_5G_BAND_SEL_N	(1 << 2)
#define RT2860_5G_BAND_SEL_P	(1 << 1)
#define RT2860_TX_BAND_SEL	(1 << 0)

/* possible flags for register TX_SW_CFG0 */
#define RT2860_DLY_RFTR_EN_SHIFT	24
#define RT2860_DLY_TRSW_EN_SHIFT	16
#define RT2860_DLY_PAPE_EN_SHIFT	8
#define RT2860_DLY_TXPE_EN_SHIFT	0

/* possible flags for register TX_SW_CFG1 */
#define RT2860_DLY_RFTR_DIS_SHIFT	16
#define RT2860_DLY_TRSW_DIS_SHIFT	8
#define RT2860_DLY_PAPE_DIS SHIFT	0

/* possible flags for register TX_SW_CFG2 */
#define RT2860_DLY_LNA_EN_SHIFT		24
#define RT2860_DLY_LNA_DIS_SHIFT	16
#define RT2860_DLY_DAC_EN_SHIFT		8
#define RT2860_DLY_DAC_DIS_SHIFT	0

/* possible flags for register TXOP_THRES_CFG */
#define RT2860_TXOP_REM_THRES_SHIFT	24
#define RT2860_CF_END_THRES_SHIFT	16
#define RT2860_RDG_IN_THRES		8
#define RT2860_RDG_OUT_THRES		0

/* possible flags for register TXOP_CTRL_CFG */
#define RT2860_EXT_CW_MIN_SHIFT		16
#define RT2860_EXT_CCA_DLY_SHIFT	8
#define RT2860_EXT_CCA_EN		(1 << 7)
#define RT2860_LSIG_TXOP_EN		(1 << 6)
#define RT2860_TXOP_TRUN_EN_MIMOPS	(1 << 4)
#define RT2860_TXOP_TRUN_EN_TXOP	(1 << 3)
#define RT2860_TXOP_TRUN_EN_RATE	(1 << 2)
#define RT2860_TXOP_TRUN_EN_AC		(1 << 1)
#define RT2860_TXOP_TRUN_EN_TIMEOUT	(1 << 0)

/* possible flags for register TX_RTS_CFG */
#define RT2860_RTS_FBK_EN		(1 << 24)
#define RT2860_RTS_THRES_SHIFT		8
#define RT2860_RTS_RTY_LIMIT_SHIFT	0

/* possible flags for register TX_TIMEOUT_CFG */
#define RT2860_TXOP_TIMEOUT_SHIFT	16
#define RT2860_RX_ACK_TIMEOUT_SHIFT	8
#define RT2860_MPDU_LIFE_TIME_SHIFT	4

/* possible flags for register TX_RTY_CFG */
#define RT2860_TX_AUTOFB_EN		(1 << 30)
#define RT2860_AGG_RTY_MODE_TIMER	(1 << 29)
#define RT2860_NAG_RTY_MODE_TIMER	(1 << 28)
#define RT2860_LONG_RTY_THRES_SHIFT	16
#define RT2860_LONG_RTY_LIMIT_SHIFT	8
#define RT2860_SHORT_RTY_LIMIT_SHIFT	0

/* possible flags for register TX_LINK_CFG */
#define RT2860_REMOTE_MFS_SHIFT		24
#define RT2860_REMOTE_MFB_SHIFT		16
#define RT2860_TX_CFACK_EN		(1 << 12)
#define RT2860_TX_RDG_EN		(1 << 11)
#define RT2860_TX_MRQ_EN		(1 << 10)
#define RT2860_REMOTE_UMFS_EN		(1 <<  9)
#define RT2860_TX_MFB_EN		(1 <<  8)
#define RT2860_REMOTE_MFB_LT_SHIFT	0

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

/* possible flags for register RX_FILTR_CFG */
#define RT2860_DROP_CTRL_RSV	(1 << 16)
#define RT2860_DROP_BAR		(1 << 15)
#define RT2860_DROP_BA		(1 << 14)
#define RT2860_DROP_PSPOLL	(1 << 13)
#define RT2860_DROP_RTS		(1 << 12)
#define RT2860_DROP_CTS		(1 << 11)
#define RT2860_DROP_ACK		(1 << 10)
#define RT2860_DROP_CFEND	(1 <<  9)
#define RT2860_DROP_CFACK	(1 <<  8)
#define RT2860_DROP_DUPL	(1 <<  7)
#define RT2860_DROP_BC		(1 <<  6)
#define RT2860_DROP_MC		(1 <<  5)
#define RT2860_DROP_VER_ERR	(1 <<  4)
#define RT2860_DROP_NOT_MYBSS	(1 <<  3)
#define RT2860_DROP_UC_NOME	(1 <<  2)
#define RT2860_DROP_PHY_ERR	(1 <<  1)
#define RT2860_DROP_CRC_ERR	(1 <<  0)

/* possible flags for register AUTO_RSP_CFG */
#define RT2860_CTRL_PWR_BIT	(1 << 7)
#define RT2860_BAC_ACK_POLICY	(1 << 6)
#define RT2860_CCK_SHORT_EN	(1 << 4)
#define RT2860_CTS_40M_REF_EN	(1 << 3)
#define RT2860_CTS_40M_MODE_EN	(1 << 2)
#define RT2860_BAC_ACKPOLICY_EN	(1 << 1)
#define RT2860_AUTO_RSP_EN	(1 << 0)

/* possible flags for register SIFS_COST_CFG */
#define RT2860_OFDM_SIFS_COST_SHIFT	8
#define RT2860_CCK_SIFS_COST_SHIFT	0

/* possible flags for register TXOP_HLDR_ET */
#define RT2860_TXOP_ETM1_EN		(1 << 25)
#define RT2860_TXOP_ETM0_EN		(1 << 24)
#define RT2860_TXOP_ETM_THRES_SHIFT	16
#define RT2860_TXOP_ETO_EN		(1 <<  8)
#define RT2860_TXOP_ETO_THRES_SHIFT	1
#define RT2860_PER_RX_RST_EN		(1 <<  0)

/* possible flags for register TX_STAT_FIFO */
#define RT2860_TXQ_MCS_SHIFT	16
#define RT2860_TXQ_WCID_SHIFT	8
#define RT2860_TXQ_ACKREQ	(1 << 7)
#define RT2860_TXQ_AGG		(1 << 6)
#define RT2860_TXQ_OK		(1 << 5)
#define RT2860_TXQ_PID_SHIFT	1
#define RT2860_TXQ_VLD		(1 << 0)

/* possible flags for register WCID_ATTR */
#define RT2860_MODE_NOSEC	0
#define RT2860_MODE_WEP40	1
#define RT2860_MODE_WEP104	2
#define RT2860_MODE_TKIP	3
#define RT2860_MODE_AES_CCMP	4
#define RT2860_MODE_CKIP40	5
#define RT2860_MODE_CKIP104	6
#define RT2860_MODE_CKIP128	7
#define RT2860_RX_PKEY_EN	(1 << 0)

/* possible flags for register H2M_MAILBOX */
#define RT2860_H2M_BUSY		(1 << 24)
#define RT2860_TOKEN_NO_INTR	0xff


/* possible flags for MCU command RT2860_MCU_CMD_LEDS */
#define RT2860_LED_RADIO	(1 << 13)
#define RT2860_LED_LINK_2GHZ	(1 << 14)
#define RT2860_LED_LINK_5GHZ	(1 << 15)


/* possible flags for RT3020 RF register 1 */
#define RT3070_RF_BLOCK	(1 << 0)
#define RT3070_PLL_PD	(1 << 1)
#define RT3070_RX0_PD	(1 << 2)
#define RT3070_TX0_PD	(1 << 3)
#define RT3070_RX1_PD	(1 << 4)
#define RT3070_TX1_PD	(1 << 5)
#define RT3070_RX2_PD	(1 << 6)
#define RT3070_TX2_PD	(1 << 7)

/* possible flags for RT3020 RF register 7 */
#define RT3070_TUNE	(1 << 0)

/* possible flags for RT3020 RF register 15 */
#define RT3070_TX_LO2	(1 << 3)

/* possible flags for RT3020 RF register 17 */
#define RT3070_TX_LO1	(1 << 3)

/* possible flags for RT3020 RF register 20 */
#define RT3070_RX_LO1	(1 << 3)

/* possible flags for RT3020 RF register 21 */
#define RT3070_RX_LO2	(1 << 3)
#define RT3070_RX_CTB	(1 << 7)

/* possible flags for RT3020 RF register 22 */
#define RT3070_BB_LOOPBACK	(1 << 0)

/* possible flags for RT3053 RF register 1 */
#define RT3593_VCO	(1 << 0)

/* possible flags for RT3053 RF register 2 */
#define RT3593_RESCAL	(1 << 7)

/* possible flags for RT3053 RF register 3 */
#define RT3593_VCOCAL	(1 << 7)

/* possible flags for RT3053 RF register 6 */
#define RT3593_VCO_IC	(1 << 6)

/* possible flags for RT3053 RF register 18 */
#define RT3593_AUTOTUNE_BYPASS	(1 << 6)

/* possible flags for RT3053 RF register 20 */
#define RT3593_LDO_PLL_VC_MASK	0x0e
#define RT3593_LDO_RF_VC_MASK	0xe0

/* possible flags for RT3053 RF register 22 */
#define RT3593_CP_IC_MASK	0xe0
#define RT3593_CP_IC_SHIFT	5

/* possible flags for RT5390 RF register 38. */
#define RT5390_RX_LO1	(1 << 5)

/* possible flags for RT5390 RF register 39. */
#define RT5390_RX_LO2	(1 << 7)

/* possible flags for RT5390 RF register 42 */
#define RT5390_RX_CTB	(1 << 6)

/* possible flags for RT3053 RF register 46 */
#define RT3593_RX_CTB	(1 << 5)

/* possible flags for RT3053 RF register 50 */
#define RT3593_TX_LO2	(1 << 4)

/* possible flags for RT3053 RF register 51 */
#define RT3593_TX_LO1	(1 << 4)

/* Possible flags for RT5390 BBP register 4. */
#define RT5390_MAC_IF_CTRL	(1 << 6)

/* possible flags for RT5390 BBP register 105. */
#define RT5390_MLD			(1 << 2)
#define RT5390_EN_SIG_MODULATION	(1 << 3)

#define RT3090_DEF_LNA	10

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

/* RT2870 TX descriptor */
struct rt2870_txd {
	uint16_t	len;
	uint8_t		pad;
	uint8_t		flags;
} __packed;

/* TX Wireless Information */
struct rt2860_txwi {
	uint8_t		flags;
#define RT2860_TX_MPDU_DSITY_SHIFT	5
#define RT2860_TX_AMPDU			(1 << 4)
#define RT2860_TX_TS			(1 << 3)
#define RT2860_TX_CFACK			(1 << 2)
#define RT2860_TX_MMPS			(1 << 1)
#define RT2860_TX_FRAG			(1 << 0)

	uint8_t		txop;
#define RT2860_TX_TXOP_HT	0
#define RT2860_TX_TXOP_PIFS	1
#define RT2860_TX_TXOP_SIFS	2
#define RT2860_TX_TXOP_BACKOFF	3

	uint16_t	phy;
#define RT2860_PHY_MODE		0xc000
#define RT2860_PHY_CCK		(0 << 14)
#define RT2860_PHY_OFDM		(1 << 14)
#define RT2860_PHY_HT		(2 << 14)
#define RT2860_PHY_HT_GF	(3 << 14)
#define RT2860_PHY_SGI		(1 << 8)
#define RT2860_PHY_BW40		(1 << 7)
#define RT2860_PHY_MCS		0x7f
#define RT2860_PHY_SHPRE	(1 << 3)

	uint8_t		xflags;
#define RT2860_TX_BAWINSIZE_SHIFT	2
#define RT2860_TX_NSEQ			(1 << 1)
#define RT2860_TX_ACK			(1 << 0)

	uint8_t		wcid;	/* Wireless Client ID */
	uint16_t	len;
#define RT2860_TX_PID_SHIFT	12

	uint32_t	iv;
	uint32_t	eiv;
} __packed;

/* RT2860 RX descriptor */
struct rt2860_rxd {
	uint32_t	sdp0;
	uint16_t	sdl1;	/* unused */
	uint16_t	sdl0;
#define RT2860_RX_DDONE	(1 << 15)
#define RT2860_RX_LS0	(1 << 14)

	uint32_t	sdp1;	/* unused */
	uint32_t	flags;
#define RT2860_RX_DEC		(1 << 16)
#define RT2860_RX_AMPDU		(1 << 15)
#define RT2860_RX_L2PAD		(1 << 14)
#define RT2860_RX_RSSI		(1 << 13)
#define RT2860_RX_HTC		(1 << 12)
#define RT2860_RX_AMSDU		(1 << 11)
#define RT2860_RX_MICERR	(1 << 10)
#define RT2860_RX_ICVERR	(1 <<  9)
#define RT2860_RX_CRCERR	(1 <<  8)
#define RT2860_RX_MYBSS		(1 <<  7)
#define RT2860_RX_BC		(1 <<  6)
#define RT2860_RX_MC		(1 <<  5)
#define RT2860_RX_UC2ME		(1 <<  4)
#define RT2860_RX_FRAG		(1 <<  3)
#define RT2860_RX_NULL		(1 <<  2)
#define RT2860_RX_DATA		(1 <<  1)
#define RT2860_RX_BA		(1 <<  0)
} __packed;

/* RT2870 RX descriptor */
struct rt2870_rxd {
	/* single 32-bit field */
	uint32_t	flags;
} __packed;

/* RX Wireless Information */
struct rt2860_rxwi {
	uint8_t		wcid;
	uint8_t		keyidx;
#define RT2860_RX_UDF_SHIFT	5
#define RT2860_RX_BSS_IDX_SHIFT	2

	uint16_t	len;
#define RT2860_RX_TID_SHIFT	12

	uint16_t	seq;
	uint16_t	phy;
	uint8_t		rssi[3];
	uint8_t		reserved1;
	uint8_t		snr[2];
	uint16_t	reserved2;
} __packed;


/* first DMA segment contains TXWI + 802.11 header + 32-bit padding */
#define RT2860_TXWI_DMASZ			\
	(sizeof (struct rt2860_txwi) +		\
	 sizeof (struct ieee80211_htframe) +	\
	 sizeof (uint16_t))

#define RT2860_RF1	0
#define RT2860_RF2	2
#define RT2860_RF3	1
#define RT2860_RF4	3

#define RT2860_RF_2820	0x0001	/* 2T3R */
#define RT2860_RF_2850	0x0002	/* dual-band 2T3R */
#define RT2860_RF_2720	0x0003	/* 1T2R */
#define RT2860_RF_2750	0x0004	/* dual-band 1T2R */
#define RT3070_RF_3020	0x0005	/* 1T1R */
#define RT3070_RF_2020	0x0006	/* b/g */
#define RT3070_RF_3021	0x0007	/* 1T2R */
#define RT3070_RF_3022	0x0008	/* 2T2R */
#define RT3070_RF_3052	0x0009	/* dual-band 2T2R */
#define RT3070_RF_3320	0x000b	/* 1T1R */
#define RT3070_RF_3053	0x000d	/* dual-band 3T3R */
#define RT5592_RF_5592	0x000f	/* dual-band 2T2R */
#define RT3290_RF_3290	0x3290	/* 1T1R */
#define RT5390_RF_5360	0x5360	/* 1T1R */
#define RT5390_RF_5370	0x5370	/* 1T1R */
#define RT5390_RF_5372	0x5372	/* 2T2R */
#define RT5390_RF_5390	0x5390	/* 1T1R */
#define RT5390_RF_5392	0x5392	/* 2T2R */

/* USB commands for RT2870 only */
#define RT2870_RESET		1
#define RT2870_WRITE_2		2
#define RT2870_WRITE_REGION_1	6
#define RT2870_READ_REGION_1	7
#define RT2870_EEPROM_READ	9

#define RT2860_EEPROM_DELAY	1	/* minimum hold time (microsecond) */

#define RT2860_EEPROM_CHIPID		0x00
#define RT2860_EEPROM_VERSION		0x01
#define RT2860_EEPROM_MAC01		0x02
#define RT2860_EEPROM_MAC23		0x03
#define RT2860_EEPROM_MAC45		0x04
#define RT2860_EEPROM_PCIE_PSLEVEL	0x11
#define RT2860_EEPROM_REV		0x12
#define RT2860_EEPROM_ANTENNA		0x1a
#define RT2860_EEPROM_CONFIG		0x1b
#define RT2860_EEPROM_COUNTRY		0x1c
#define RT2860_EEPROM_FREQ_LEDS		0x1d
#define RT2860_EEPROM_LED1		0x1e
#define RT2860_EEPROM_LED2		0x1f
#define RT2860_EEPROM_LED3		0x20
#define RT2860_EEPROM_LNA		0x22
#define RT2860_EEPROM_RSSI1_2GHZ	0x23
#define RT2860_EEPROM_RSSI2_2GHZ	0x24
#define RT2860_EEPROM_RSSI1_5GHZ	0x25
#define RT2860_EEPROM_RSSI2_5GHZ	0x26
#define RT2860_EEPROM_DELTAPWR		0x28
#define RT2860_EEPROM_PWR2GHZ_BASE1	0x29
#define RT2860_EEPROM_PWR2GHZ_BASE2	0x30
#define RT2860_EEPROM_TSSI1_2GHZ	0x37
#define RT2860_EEPROM_TSSI2_2GHZ	0x38
#define RT2860_EEPROM_TSSI3_2GHZ	0x39
#define RT2860_EEPROM_TSSI4_2GHZ	0x3a
#define RT2860_EEPROM_TSSI5_2GHZ	0x3b
#define RT2860_EEPROM_PWR5GHZ_BASE1	0x3c
#define RT2860_EEPROM_PWR5GHZ_BASE2	0x53
#define RT2860_EEPROM_TSSI1_5GHZ	0x6a
#define RT2860_EEPROM_TSSI2_5GHZ	0x6b
#define RT2860_EEPROM_TSSI3_5GHZ	0x6c
#define RT2860_EEPROM_TSSI4_5GHZ	0x6d
#define RT2860_EEPROM_TSSI5_5GHZ	0x6e
#define RT2860_EEPROM_RPWR		0x6f
#define RT2860_EEPROM_BBP_BASE		0x78
#define RT3071_EEPROM_RF_BASE		0x82

/* EEPROM registers for RT3593. */
#define RT3593_EEPROM_FREQ_LEDS		0x21
#define RT3593_EEPROM_FREQ		0x22
#define RT3593_EEPROM_LED1		0x22
#define RT3593_EEPROM_LED2		0x23
#define RT3593_EEPROM_LED3		0x24
#define RT3593_EEPROM_LNA		0x26
#define RT3593_EEPROM_LNA_5GHZ		0x27
#define RT3593_EEPROM_RSSI1_2GHZ	0x28
#define RT3593_EEPROM_RSSI2_2GHZ	0x29
#define RT3593_EEPROM_RSSI1_5GHZ	0x2a
#define RT3593_EEPROM_RSSI2_5GHZ	0x2b
#define RT3593_EEPROM_PWR2GHZ_BASE1	0x30
#define RT3593_EEPROM_PWR2GHZ_BASE2	0x37
#define RT3593_EEPROM_PWR2GHZ_BASE3	0x3e
#define RT3593_EEPROM_PWR5GHZ_BASE1	0x4b
#define RT3593_EEPROM_PWR5GHZ_BASE2	0x65
#define RT3593_EEPROM_PWR5GHZ_BASE3	0x7f

/*
 * EEPROM IQ calibration.
 */
#define RT5390_EEPROM_IQ_GAIN_CAL_TX0_2GHZ			0x130
#define RT5390_EEPROM_IQ_PHASE_CAL_TX0_2GHZ			0x131
#define RT5390_EEPROM_IQ_GAIN_CAL_TX1_2GHZ			0x133
#define RT5390_EEPROM_IQ_PHASE_CAL_TX1_2GHZ			0x134
#define RT5390_EEPROM_RF_IQ_COMPENSATION_CTL			0x13c
#define RT5390_EEPROM_RF_IQ_IMBALANCE_COMPENSATION_CTL		0x13d
#define RT5390_EEPROM_IQ_GAIN_CAL_TX0_CH36_TO_CH64_5GHZ		0x144
#define RT5390_EEPROM_IQ_PHASE_CAL_TX0_CH36_TO_CH64_5GHZ	0x145
#define RT5390_EEPROM_IQ_GAIN_CAL_TX0_CH100_TO_CH138_5GHZ	0x146
#define RT5390_EEPROM_IQ_PHASE_CAL_TX0_CH100_TO_CH138_5GHZ	0x147
#define RT5390_EEPROM_IQ_GAIN_CAL_TX0_CH140_TO_CH165_5GHZ	0x148
#define RT5390_EEPROM_IQ_PHASE_CAL_TX0_CH140_TO_CH165_5GHZ	0x149
#define RT5390_EEPROM_IQ_GAIN_CAL_TX1_CH36_TO_CH64_5GHZ		0x14a
#define RT5390_EEPROM_IQ_PHASE_CAL_TX1_CH36_TO_CH64_5GHZ	0x14b
#define RT5390_EEPROM_IQ_GAIN_CAL_TX1_CH100_TO_CH138_5GHZ	0x14c
#define RT5390_EEPROM_IQ_PHASE_CAL_TX1_CH100_TO_CH138_5GHZ	0x14d
#define RT5390_EEPROM_IQ_GAIN_CAL_TX1_CH140_TO_CH165_5GHZ	0x14e
#define RT5390_EEPROM_IQ_PHASE_CAL_TX1_CH140_TO_CH165_5GHZ	0x14f

#define RT2860_RIDX_CCK1	 0
#define RT2860_RIDX_CCK11	 3
#define RT2860_RIDX_OFDM6	 4
#define RT2860_RIDX_MAX		11
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

/*
 * Control and status registers access macros.
 */
#define RAL_READ(sc, reg)						\
	bus_space_read_4((sc)->sc_st, (sc)->sc_sh, (reg))

#define RAL_WRITE(sc, reg, val)						\
	bus_space_write_4((sc)->sc_st, (sc)->sc_sh, (reg), (val))

#define RAL_BARRIER_WRITE(sc)						\
	bus_space_barrier((sc)->sc_st, (sc)->sc_sh, 0, 0x1800,		\
	    BUS_SPACE_BARRIER_WRITE)

#define RAL_BARRIER_READ_WRITE(sc)					\
	bus_space_barrier((sc)->sc_st, (sc)->sc_sh, 0, 0x1800,		\
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE)

#define RAL_WRITE_REGION_1(sc, offset, datap, count)			\
	bus_space_write_region_1((sc)->sc_st, (sc)->sc_sh, (offset),	\
	    (datap), (count))

#define RAL_SET_REGION_4(sc, offset, val, count)			\
	bus_space_set_region_4((sc)->sc_st, (sc)->sc_sh, (offset),	\
	    (val), (count))

/*
 * EEPROM access macro.
 */
#define RT2860_EEPROM_CTL(sc, val) do {					\
	RAL_WRITE((sc), RT2860_PCI_EECTRL, (val));			\
	RAL_BARRIER_READ_WRITE((sc));					\
	DELAY(RT2860_EEPROM_DELAY);					\
} while (/* CONSTCOND */0)

/*
 * Default values for MAC registers; values taken from the reference driver.
 */
#define RT2860_DEF_MAC					\
	{ RT2860_BCN_OFFSET0,		0xf8f0e8e0 },	\
	{ RT2860_BCN_OFFSET1,		0x6f77d0c8 },	\
	{ RT2860_LEGACY_BASIC_RATE,	0x0000013f },	\
	{ RT2860_HT_BASIC_RATE,		0x00008003 },	\
	{ RT2860_MAC_SYS_CTRL,		0x00000000 },	\
	{ RT2860_RX_FILTR_CFG,		0x00017f97 },	\
	{ RT2860_BKOFF_SLOT_CFG,	0x00000209 },	\
	{ RT2860_TX_SW_CFG0,		0x00000000 },	\
	{ RT2860_TX_SW_CFG1,		0x00080606 },	\
	{ RT2860_TX_LINK_CFG,		0x00001020 },	\
	{ RT2860_TX_TIMEOUT_CFG,	0x000a2090 },	\
	{ RT2860_MAX_LEN_CFG,		0x00001f00 },	\
	{ RT2860_LED_CFG,		0x7f031e46 },	\
	{ RT2860_WMM_AIFSN_CFG,		0x00002273 },	\
	{ RT2860_WMM_CWMIN_CFG,		0x00002344 },	\
	{ RT2860_WMM_CWMAX_CFG,		0x000034aa },	\
	{ RT2860_MAX_PCNT,		0x1f3fbf9f },	\
	{ RT2860_TX_RTY_CFG,		0x47d01f0f },	\
	{ RT2860_AUTO_RSP_CFG,		0x00000013 },	\
	{ RT2860_CCK_PROT_CFG,		0x05740003 },	\
	{ RT2860_OFDM_PROT_CFG,		0x05740003 },	\
	{ RT2860_GF20_PROT_CFG,		0x01744004 },	\
	{ RT2860_GF40_PROT_CFG,		0x03f44084 },	\
	{ RT2860_MM20_PROT_CFG,		0x01744004 },	\
	{ RT2860_MM40_PROT_CFG,		0x03f54084 },	\
	{ RT2860_TXOP_CTRL_CFG,		0x0000583f },	\
	{ RT2860_TXOP_HLDR_ET,		0x00000002 },	\
	{ RT2860_TX_RTS_CFG,		0x00092b20 },	\
	{ RT2860_EXP_ACK_TIME,		0x002400ca },	\
	{ RT2860_XIFS_TIME_CFG,		0x33a41010 },	\
	{ RT2860_PWR_PIN_CFG,		0x00000003 }

/* XXX only a few registers differ from above, try to merge? */
#define RT2870_DEF_MAC					\
	{ RT2860_BCN_OFFSET0,		0xf8f0e8e0 },	\
	{ RT2860_LEGACY_BASIC_RATE,	0x0000013f },	\
	{ RT2860_HT_BASIC_RATE,		0x00008003 },	\
	{ RT2860_MAC_SYS_CTRL,		0x00000000 },	\
	{ RT2860_BKOFF_SLOT_CFG,	0x00000209 },	\
	{ RT2860_TX_SW_CFG0,		0x00000000 },	\
	{ RT2860_TX_SW_CFG1,		0x00080606 },	\
	{ RT2860_TX_LINK_CFG,		0x00001020 },	\
	{ RT2860_TX_TIMEOUT_CFG,	0x000a2090 },	\
	{ RT2860_LED_CFG,		0x7f031e46 },	\
	{ RT2860_WMM_AIFSN_CFG,		0x00002273 },	\
	{ RT2860_WMM_CWMIN_CFG,		0x00002344 },	\
	{ RT2860_WMM_CWMAX_CFG,		0x000034aa },	\
	{ RT2860_MAX_PCNT,		0x1f3fbf9f },	\
	{ RT2860_TX_RTY_CFG,		0x47d01f0f },	\
	{ RT2860_AUTO_RSP_CFG,		0x00000013 },	\
	{ RT2860_CCK_PROT_CFG,		0x05740003 },	\
	{ RT2860_OFDM_PROT_CFG,		0x05740003 },	\
	{ RT2860_PBF_CFG,		0x00f40006 },	\
	{ RT2860_WPDMA_GLO_CFG,		0x00000030 },	\
	{ RT2860_GF20_PROT_CFG,		0x01744004 },	\
	{ RT2860_GF40_PROT_CFG,		0x03f44084 },	\
	{ RT2860_MM20_PROT_CFG,		0x01744004 },	\
	{ RT2860_MM40_PROT_CFG,		0x03f44084 },	\
	{ RT2860_TXOP_CTRL_CFG,		0x0000583f },	\
	{ RT2860_TXOP_HLDR_ET,		0x00000002 },	\
	{ RT2860_TX_RTS_CFG,		0x00092b20 },	\
	{ RT2860_EXP_ACK_TIME,		0x002400ca },	\
	{ RT2860_XIFS_TIME_CFG,		0x33a41010 },	\
	{ RT2860_PWR_PIN_CFG,		0x00000003 }

/*
 * Default values for BBP registers; values taken from the reference driver.
 */
#define RT2860_DEF_BBP	\
	{  65, 0x2c },	\
	{  66, 0x38 },	\
	{  68, 0x0b },	\
	{  69, 0x12 },	\
	{  70, 0x0a },	\
	{  73, 0x10 },	\
	{  81, 0x37 },	\
	{  82, 0x62 },	\
	{  83, 0x6a },	\
	{  84, 0x99 },	\
	{  86, 0x00 },	\
	{  91, 0x04 },	\
	{  92, 0x00 },	\
	{ 103, 0x00 },	\
	{ 105, 0x05 },	\
	{ 106, 0x35 }

#define RT3290_DEF_BBP	\
	{  31, 0x08 },	\
	{  68, 0x0b },	\
	{  73, 0x13 },	\
	{  75, 0x46 },	\
	{  76, 0x28 },	\
	{  77, 0x59 },	\
	{  82, 0x62 },	\
	{  83, 0x7a },	\
	{  84, 0x9a },	\
	{  86, 0x38 },	\
	{  91, 0x04 },	\
	{ 103, 0xc0 },	\
	{ 104, 0x92 },	\
	{ 105, 0x3c },	\
	{ 106, 0x03 },	\
	{ 128, 0x12 }

#define RT5390_DEF_BBP	\
	{  31, 0x08 },	\
	{  65, 0x2c },	\
	{  66, 0x38 },	\
	{  68, 0x0b },	\
	{  69, 0x0d },	\
	{  70, 0x06 },	\
	{  73, 0x13 },	\
	{  75, 0x46 },	\
	{  76, 0x28 },	\
	{  77, 0x59 },	\
	{  81, 0x37 },	\
	{  82, 0x62 },	\
	{  83, 0x7a },	\
	{  84, 0x9a },	\
	{  86, 0x38 },	\
	{  91, 0x04 },	\
	{  92, 0x02 },	\
	{ 103, 0xc0 },	\
	{ 104, 0x92 },	\
	{ 105, 0x3c },	\
	{ 106, 0x03 },	\
	{ 128, 0x12 }

#define RT5592_DEF_BBP	\
	{  20, 0x06 },	\
	{  31, 0x08 },	\
	{  65, 0x2c },	\
	{  66, 0x38 },	\
	{  68, 0xdd },	\
	{  69, 0x1a },	\
	{  70, 0x05 },	\
	{  73, 0x13 },	\
	{  74, 0x0f },	\
	{  75, 0x4f },	\
	{  76, 0x28 },	\
	{  77, 0x59 },	\
	{  81, 0x37 },	\
	{  82, 0x62 },	\
	{  83, 0x6a },	\
	{  84, 0x9a },	\
	{  86, 0x38 },	\
	{  88, 0x90 },	\
	{  91, 0x04 },	\
	{  92, 0x02 },	\
	{  95, 0x9a },	\
	{  98, 0x12 },	\
	{ 103, 0xc0 },	\
	{ 104, 0x92 },	\
	{ 105, 0x3c },	\
	{ 106, 0x35 },	\
	{ 128, 0x12 },	\
	{ 134, 0xd0 },	\
	{ 135, 0xf6 },	\
	{ 137, 0x0f }

/*
 * Default settings for RF registers; values derived from the reference driver.
 */
#define RT2860_RF2850						\
	{   1, 0x100bb3, 0x1301e1, 0x05a014, 0x001402 },	\
	{   2, 0x100bb3, 0x1301e1, 0x05a014, 0x001407 },	\
	{   3, 0x100bb3, 0x1301e2, 0x05a014, 0x001402 },	\
	{   4, 0x100bb3, 0x1301e2, 0x05a014, 0x001407 },	\
	{   5, 0x100bb3, 0x1301e3, 0x05a014, 0x001402 },	\
	{   6, 0x100bb3, 0x1301e3, 0x05a014, 0x001407 },	\
	{   7, 0x100bb3, 0x1301e4, 0x05a014, 0x001402 },	\
	{   8, 0x100bb3, 0x1301e4, 0x05a014, 0x001407 },	\
	{   9, 0x100bb3, 0x1301e5, 0x05a014, 0x001402 },	\
	{  10, 0x100bb3, 0x1301e5, 0x05a014, 0x001407 },	\
	{  11, 0x100bb3, 0x1301e6, 0x05a014, 0x001402 },	\
	{  12, 0x100bb3, 0x1301e6, 0x05a014, 0x001407 },	\
	{  13, 0x100bb3, 0x1301e7, 0x05a014, 0x001402 },	\
	{  14, 0x100bb3, 0x1301e8, 0x05a014, 0x001404 },	\
	{  36, 0x100bb3, 0x130266, 0x056014, 0x001408 },	\
	{  38, 0x100bb3, 0x130267, 0x056014, 0x001404 },	\
	{  40, 0x100bb2, 0x1301a0, 0x056014, 0x001400 },	\
	{  44, 0x100bb2, 0x1301a0, 0x056014, 0x001408 },	\
	{  46, 0x100bb2, 0x1301a1, 0x056014, 0x001402 },	\
	{  48, 0x100bb2, 0x1301a1, 0x056014, 0x001406 },	\
	{  52, 0x100bb2, 0x1301a2, 0x056014, 0x001404 },	\
	{  54, 0x100bb2, 0x1301a2, 0x056014, 0x001408 },	\
	{  56, 0x100bb2, 0x1301a3, 0x056014, 0x001402 },	\
	{  60, 0x100bb2, 0x1301a4, 0x056014, 0x001400 },	\
	{  62, 0x100bb2, 0x1301a4, 0x056014, 0x001404 },	\
	{  64, 0x100bb2, 0x1301a4, 0x056014, 0x001408 },	\
	{ 100, 0x100bb2, 0x1301ac, 0x05e014, 0x001400 },	\
	{ 102, 0x100bb2, 0x1701ac, 0x15e014, 0x001404 },	\
	{ 104, 0x100bb2, 0x1701ac, 0x15e014, 0x001408 },	\
	{ 108, 0x100bb3, 0x17028c, 0x15e014, 0x001404 },	\
	{ 110, 0x100bb3, 0x13028d, 0x05e014, 0x001400 },	\
	{ 112, 0x100bb3, 0x13028d, 0x05e014, 0x001406 },	\
	{ 116, 0x100bb3, 0x13028e, 0x05e014, 0x001408 },	\
	{ 118, 0x100bb3, 0x13028f, 0x05e014, 0x001404 },	\
	{ 120, 0x100bb1, 0x1300e0, 0x05e014, 0x001400 },	\
	{ 124, 0x100bb1, 0x1300e0, 0x05e014, 0x001404 },	\
	{ 126, 0x100bb1, 0x1300e0, 0x05e014, 0x001406 },	\
	{ 128, 0x100bb1, 0x1300e0, 0x05e014, 0x001408 },	\
	{ 132, 0x100bb1, 0x1300e1, 0x05e014, 0x001402 },	\
	{ 134, 0x100bb1, 0x1300e1, 0x05e014, 0x001404 },	\
	{ 136, 0x100bb1, 0x1300e1, 0x05e014, 0x001406 },	\
	{ 140, 0x100bb1, 0x1300e2, 0x05e014, 0x001400 },	\
	{ 149, 0x100bb1, 0x1300e2, 0x05e014, 0x001409 },	\
	{ 151, 0x100bb1, 0x1300e3, 0x05e014, 0x001401 },	\
	{ 153, 0x100bb1, 0x1300e3, 0x05e014, 0x001403 },	\
	{ 157, 0x100bb1, 0x1300e3, 0x05e014, 0x001407 },	\
	{ 159, 0x100bb1, 0x1300e3, 0x05e014, 0x001409 },	\
	{ 161, 0x100bb1, 0x1300e4, 0x05e014, 0x001401 },	\
	{ 165, 0x100bb1, 0x1300e4, 0x05e014, 0x001405 },	\
	{ 167, 0x100bb1, 0x1300f4, 0x05e014, 0x001407 },	\
	{ 169, 0x100bb1, 0x1300f4, 0x05e014, 0x001409 },	\
	{ 171, 0x100bb1, 0x1300f5, 0x05e014, 0x001401 },	\
	{ 173, 0x100bb1, 0x1300f5, 0x05e014, 0x001403 }

#define RT3070_RF3052		\
	{ 0xf1, 2,  2 },	\
	{ 0xf1, 2,  7 },	\
	{ 0xf2, 2,  2 },	\
	{ 0xf2, 2,  7 },	\
	{ 0xf3, 2,  2 },	\
	{ 0xf3, 2,  7 },	\
	{ 0xf4, 2,  2 },	\
	{ 0xf4, 2,  7 },	\
	{ 0xf5, 2,  2 },	\
	{ 0xf5, 2,  7 },	\
	{ 0xf6, 2,  2 },	\
	{ 0xf6, 2,  7 },	\
	{ 0xf7, 2,  2 },	\
	{ 0xf8, 2,  4 },	\
	{ 0x56, 0,  4 },	\
	{ 0x56, 0,  6 },	\
	{ 0x56, 0,  8 },	\
	{ 0x57, 0,  0 },	\
	{ 0x57, 0,  2 },	\
	{ 0x57, 0,  4 },	\
	{ 0x57, 0,  8 },	\
	{ 0x57, 0, 10 },	\
	{ 0x58, 0,  0 },	\
	{ 0x58, 0,  4 },	\
	{ 0x58, 0,  6 },	\
	{ 0x58, 0,  8 },	\
	{ 0x5b, 0,  8 },	\
	{ 0x5b, 0, 10 },	\
	{ 0x5c, 0,  0 },	\
	{ 0x5c, 0,  4 },	\
	{ 0x5c, 0,  6 },	\
	{ 0x5c, 0,  8 },	\
	{ 0x5d, 0,  0 },	\
	{ 0x5d, 0,  2 },	\
	{ 0x5d, 0,  4 },	\
	{ 0x5d, 0,  8 },	\
	{ 0x5d, 0, 10 },	\
	{ 0x5e, 0,  0 },	\
	{ 0x5e, 0,  4 },	\
	{ 0x5e, 0,  6 },	\
	{ 0x5e, 0,  8 },	\
	{ 0x5f, 0,  0 },	\
	{ 0x5f, 0,  9 },	\
	{ 0x5f, 0, 11 },	\
	{ 0x60, 0,  1 },	\
	{ 0x60, 0,  5 },	\
	{ 0x60, 0,  7 },	\
	{ 0x60, 0,  9 },	\
	{ 0x61, 0,  1 },	\
	{ 0x61, 0,  3 },	\
	{ 0x61, 0,  5 },	\
	{ 0x61, 0,  7 },	\
	{ 0x61, 0,  9 }

#define RT5592_RF5592_20MHZ	\
	{ 0x1e2,  4, 10, 3 },	\
	{ 0x1e3,  4, 10, 3 },	\
	{ 0x1e4,  4, 10, 3 },	\
	{ 0x1e5,  4, 10, 3 },	\
	{ 0x1e6,  4, 10, 3 },	\
	{ 0x1e7,  4, 10, 3 },	\
	{ 0x1e8,  4, 10, 3 },	\
	{ 0x1e9,  4, 10, 3 },	\
	{ 0x1ea,  4, 10, 3 },	\
	{ 0x1eb,  4, 10, 3 },	\
	{ 0x1ec,  4, 10, 3 },	\
	{ 0x1ed,  4, 10, 3 },	\
	{ 0x1ee,  4, 10, 3 },	\
	{ 0x1f0,  8, 10, 3 },	\
	{  0xac,  8, 12, 1 },	\
	{  0xad,  0, 12, 1 },	\
	{  0xad,  4, 12, 1 },	\
	{  0xae,  0, 12, 1 },	\
	{  0xae,  4, 12, 1 },	\
	{  0xae,  8, 12, 1 },	\
	{  0xaf,  4, 12, 1 },	\
	{  0xaf,  8, 12, 1 },	\
	{  0xb0,  0, 12, 1 },	\
	{  0xb0,  8, 12, 1 },	\
	{  0xb1,  0, 12, 1 },	\
	{  0xb1,  4, 12, 1 },	\
	{  0xb7,  4, 12, 1 },	\
	{  0xb7,  8, 12, 1 },	\
	{  0xb8,  0, 12, 1 },	\
	{  0xb8,  8, 12, 1 },	\
	{  0xb9,  0, 12, 1 },	\
	{  0xb9,  4, 12, 1 },	\
	{  0xba,  0, 12, 1 },	\
	{  0xba,  4, 12, 1 },	\
	{  0xba,  8, 12, 1 },	\
	{  0xbb,  4, 12, 1 },	\
	{  0xbb,  8, 12, 1 },	\
	{  0xbc,  0, 12, 1 },	\
	{  0xbc,  8, 12, 1 },	\
	{  0xbd,  0, 12, 1 },	\
	{  0xbd,  4, 12, 1 },	\
	{  0xbe,  0, 12, 1 },	\
	{  0xbf,  6, 12, 1 },	\
	{  0xbf, 10, 12, 1 },	\
	{  0xc0,  2, 12, 1 },	\
	{  0xc0, 10, 12, 1 },	\
	{  0xc1,  2, 12, 1 },	\
	{  0xc1,  6, 12, 1 },	\
	{  0xc2,  2, 12, 1 },	\
	{  0xa4,  0, 12, 1 },	\
	{  0xa4,  4, 12, 1 },	\
	{  0xa5,  8, 12, 1 },	\
	{  0xa6,  0, 12, 1 }

#define RT5592_RF5592_40MHZ	\
	{ 0xf1,  2, 10, 3 },	\
	{ 0xf1,  7, 10, 3 },	\
	{ 0xf2,  2, 10, 3 },	\
	{ 0xf2,  7, 10, 3 },	\
	{ 0xf3,  2, 10, 3 },	\
	{ 0xf3,  7, 10, 3 },	\
	{ 0xf4,  2, 10, 3 },	\
	{ 0xf4,  7, 10, 3 },	\
	{ 0xf5,  2, 10, 3 },	\
	{ 0xf5,  7, 10, 3 },	\
	{ 0xf6,  2, 10, 3 },	\
	{ 0xf6,  7, 10, 3 },	\
	{ 0xf7,  2, 10, 3 },	\
	{ 0xf8,  4, 10, 3 },	\
	{ 0x56,  4, 12, 1 },	\
	{ 0x56,  6, 12, 1 },	\
	{ 0x56,  8, 12, 1 },	\
	{ 0x57,  0, 12, 1 },	\
	{ 0x57,  2, 12, 1 },	\
	{ 0x57,  4, 12, 1 },	\
	{ 0x57,  8, 12, 1 },	\
	{ 0x57, 10, 12, 1 },	\
	{ 0x58,  0, 12, 1 },	\
	{ 0x58,  4, 12, 1 },	\
	{ 0x58,  6, 12, 1 },	\
	{ 0x58,  8, 12, 1 },	\
	{ 0x5b,  8, 12, 1 },	\
	{ 0x5b, 10, 12, 1 },	\
	{ 0x5c,  0, 12, 1 },	\
	{ 0x5c,  4, 12, 1 },	\
	{ 0x5c,  6, 12, 1 },	\
	{ 0x5c,  8, 12, 1 },	\
	{ 0x5d,  0, 12, 1 },	\
	{ 0x5d,  2, 12, 1 },	\
	{ 0x5d,  4, 12, 1 },	\
	{ 0x5d,  8, 12, 1 },	\
	{ 0x5d, 10, 12, 1 },	\
	{ 0x5e,  0, 12, 1 },	\
	{ 0x5e,  4, 12, 1 },	\
	{ 0x5e,  6, 12, 1 },	\
	{ 0x5e,  8, 12, 1 },	\
	{ 0x5f,  0, 12, 1 },	\
	{ 0x5f,  9, 12, 1 },	\
	{ 0x5f, 11, 12, 1 },	\
	{ 0x60,  1, 12, 1 },	\
	{ 0x60,  5, 12, 1 },	\
	{ 0x60,  7, 12, 1 },	\
	{ 0x60,  9, 12, 1 },	\
	{ 0x61,  1, 12, 1 },	\
	{ 0x52,  0, 12, 1 },	\
	{ 0x52,  4, 12, 1 },	\
	{ 0x52,  8, 12, 1 },	\
	{ 0x53,  0, 12, 1 }

#define RT3070_DEF_RF	\
	{  4, 0x40 },	\
	{  5, 0x03 },	\
	{  6, 0x02 },	\
	{  7, 0x60 },	\
	{  9, 0x0f },	\
	{ 10, 0x41 },	\
	{ 11, 0x21 },	\
	{ 12, 0x7b },	\
	{ 14, 0x90 },	\
	{ 15, 0x58 },	\
	{ 16, 0xb3 },	\
	{ 17, 0x92 },	\
	{ 18, 0x2c },	\
	{ 19, 0x02 },	\
	{ 20, 0xba },	\
	{ 21, 0xdb },	\
	{ 24, 0x16 },	\
	{ 25, 0x01 },	\
	{ 29, 0x1f }

#define RT3290_DEF_RF	\
	{  1, 0x0f },	\
	{  2, 0x80 },	\
	{  3, 0x08 },	\
	{  4, 0x00 },	\
	{  6, 0xa0 },	\
	{  8, 0xf3 },	\
	{  9, 0x02 },	\
	{ 10, 0x53 },	\
	{ 11, 0x4a },	\
	{ 12, 0x46 },	\
	{ 13, 0x9f },	\
	{ 18, 0x03 },	\
	{ 22, 0x20 },	\
	{ 25, 0x80 },	\
	{ 27, 0x09 },	\
	{ 29, 0x10 },	\
	{ 30, 0x10 },	\
	{ 31, 0x80 },	\
	{ 32, 0x80 },	\
	{ 33, 0x00 },	\
	{ 34, 0x05 },	\
	{ 35, 0x12 },	\
	{ 36, 0x00 },	\
	{ 38, 0x85 },	\
	{ 39, 0x1b },	\
	{ 40, 0x0b },	\
	{ 41, 0xbb },	\
	{ 42, 0xd5 },	\
	{ 43, 0x7b },	\
	{ 44, 0x0e },	\
	{ 45, 0xa2 },	\
	{ 46, 0x73 },	\
	{ 47, 0x00 },	\
	{ 48, 0x10 },	\
	{ 49, 0x98 },	\
	{ 52, 0x38 },	\
	{ 53, 0x00 },	\
	{ 54, 0x78 },	\
	{ 55, 0x43 },	\
	{ 56, 0x02 },	\
	{ 57, 0x80 },	\
	{ 58, 0x7f },	\
	{ 59, 0x09 },	\
	{ 60, 0x45 },	\
	{ 61, 0xc1 }

#define RT3572_DEF_RF	\
	{  0, 0x70 },	\
	{  1, 0x81 },	\
	{  2, 0xf1 },	\
	{  3, 0x02 },	\
	{  4, 0x4c },	\
	{  5, 0x05 },	\
	{  6, 0x4a },	\
	{  7, 0xd8 },	\
	{  9, 0xc3 },	\
	{ 10, 0xf1 },	\
	{ 11, 0xb9 },	\
	{ 12, 0x70 },	\
	{ 13, 0x65 },	\
	{ 14, 0xa0 },	\
	{ 15, 0x53 },	\
	{ 16, 0x4c },	\
	{ 17, 0x23 },	\
	{ 18, 0xac },	\
	{ 19, 0x93 },	\
	{ 20, 0xb3 },	\
	{ 21, 0xd0 },	\
	{ 22, 0x00 },  	\
	{ 23, 0x3c },	\
	{ 24, 0x16 },	\
	{ 25, 0x15 },	\
	{ 26, 0x85 },	\
	{ 27, 0x00 },	\
	{ 28, 0x00 },	\
	{ 29, 0x9b },	\
	{ 30, 0x09 },	\
	{ 31, 0x10 }

#define RT3593_DEF_RF	\
	{  1, 0x03 },	\
	{  3, 0x80 },	\
	{  5, 0x00 },	\
	{  6, 0x40 },	\
	{  8, 0xf1 },	\
	{  9, 0x02 },	\
	{ 10, 0xd3 },	\
	{ 11, 0x40 },	\
	{ 12, 0x4e },	\
	{ 13, 0x12 },	\
	{ 18, 0x40 },	\
	{ 22, 0x20 },	\
	{ 30, 0x10 },	\
	{ 31, 0x80 },	\
	{ 32, 0x78 },	\
	{ 33, 0x3b },	\
	{ 34, 0x3c },	\
	{ 35, 0xe0 },	\
	{ 38, 0x86 },	\
	{ 39, 0x23 },	\
	{ 44, 0xd3 },	\
	{ 45, 0xbb },	\
	{ 46, 0x60 },	\
	{ 49, 0x81 },	\
	{ 50, 0x86 },	\
	{ 51, 0x75 },	\
	{ 52, 0x45 },	\
	{ 53, 0x18 },	\
	{ 54, 0x18 },	\
	{ 55, 0x18 },	\
	{ 56, 0xdb },	\
	{ 57, 0x6e }

#define RT5390_DEF_RF	\
	{  1, 0x0f },	\
	{  2, 0x80 },	\
	{  3, 0x88 },	\
	{  5, 0x10 },	\
	{  6, 0xa0 },	\
	{  7, 0x00 },	\
	{ 10, 0x53 },	\
	{ 11, 0x4a },	\
	{ 12, 0x46 },	\
	{ 13, 0x9f },	\
	{ 14, 0x00 },	\
	{ 15, 0x00 },	\
	{ 16, 0x00 },	\
	{ 18, 0x03 },	\
	{ 19, 0x00 },	\
	{ 20, 0x00 },	\
	{ 21, 0x00 },	\
	{ 22, 0x20 },  	\
	{ 23, 0x00 },	\
	{ 24, 0x00 },	\
	{ 25, 0xc0 },	\
	{ 26, 0x00 },	\
	{ 27, 0x09 },	\
	{ 28, 0x00 },	\
	{ 29, 0x10 },	\
	{ 30, 0x10 },	\
	{ 31, 0x80 },	\
	{ 32, 0x80 },	\
	{ 33, 0x00 },	\
	{ 34, 0x07 },	\
	{ 35, 0x12 },	\
	{ 36, 0x00 },	\
	{ 37, 0x08 },	\
	{ 38, 0x85 },	\
	{ 39, 0x1b },	\
	{ 40, 0x0b },	\
	{ 41, 0xbb },	\
	{ 42, 0xd2 },	\
	{ 43, 0x9a },	\
	{ 44, 0x0e },	\
	{ 45, 0xa2 },	\
	{ 46, 0x7b },	\
	{ 47, 0x00 },	\
	{ 48, 0x10 },	\
	{ 49, 0x94 },	\
	{ 52, 0x38 },	\
	{ 53, 0x84 },	\
	{ 54, 0x78 },	\
	{ 55, 0x44 },	\
	{ 56, 0x22 },	\
	{ 57, 0x80 },	\
	{ 58, 0x7f },	\
	{ 59, 0x8f },	\
	{ 60, 0x45 },	\
	{ 61, 0xdd },	\
	{ 62, 0x00 },	\
	{ 63, 0x00 }

#define RT5392_DEF_RF	\
	{  1, 0x17 },	\
	{  3, 0x88 },	\
	{  5, 0x10 },	\
	{  6, 0xe0 },	\
	{  7, 0x00 },	\
	{ 10, 0x53 },	\
	{ 11, 0x4a },	\
	{ 12, 0x46 },	\
	{ 13, 0x9f },	\
	{ 14, 0x00 },	\
	{ 15, 0x00 },	\
	{ 16, 0x00 },	\
	{ 18, 0x03 },	\
	{ 19, 0x4d },	\
	{ 20, 0x00 },	\
	{ 21, 0x8d },	\
	{ 22, 0x20 },  	\
	{ 23, 0x0b },	\
	{ 24, 0x44 },	\
	{ 25, 0x80 },	\
	{ 26, 0x82 },	\
	{ 27, 0x09 },	\
	{ 28, 0x00 },	\
	{ 29, 0x10 },	\
	{ 30, 0x10 },	\
	{ 31, 0x80 },	\
	{ 32, 0x20 },	\
	{ 33, 0xc0 },	\
	{ 34, 0x07 },	\
	{ 35, 0x12 },	\
	{ 36, 0x00 },	\
	{ 37, 0x08 },	\
	{ 38, 0x89 },	\
	{ 39, 0x1b },	\
	{ 40, 0x0f },	\
	{ 41, 0xbb },	\
	{ 42, 0xd5 },	\
	{ 43, 0x9b },	\
	{ 44, 0x0e },	\
	{ 45, 0xa2 },	\
	{ 46, 0x73 },	\
	{ 47, 0x0c },	\
	{ 48, 0x10 },	\
	{ 49, 0x94 },	\
	{ 50, 0x94 },	\
	{ 51, 0x3a },	\
	{ 52, 0x48 },	\
	{ 53, 0x44 },	\
	{ 54, 0x38 },	\
	{ 55, 0x43 },	\
	{ 56, 0xa1 },	\
	{ 57, 0x00 },	\
	{ 58, 0x39 },	\
	{ 59, 0x07 },	\
	{ 60, 0x45 },	\
	{ 61, 0x91 },	\
	{ 62, 0x39 },	\
	{ 63, 0x07 }

#define RT5592_DEF_RF	\
	{  1, 0x3f },	\
	{  3, 0x08 },	\
	{  5, 0x10 },	\
	{  6, 0xe4 },	\
	{  7, 0x00 },	\
	{ 14, 0x00 },	\
	{ 15, 0x00 },	\
	{ 16, 0x00 },	\
	{ 18, 0x03 },	\
	{ 19, 0x4d },	\
	{ 20, 0x10 },	\
	{ 21, 0x8d },	\
	{ 26, 0x82 },	\
	{ 28, 0x00 },	\
	{ 29, 0x10 },	\
	{ 33, 0xc0 },	\
	{ 34, 0x07 },	\
	{ 35, 0x12 },	\
	{ 47, 0x0c },	\
	{ 53, 0x22 },	\
	{ 63, 0x07 }

#define RT5592_2GHZ_DEF_RF	\
	{ 10, 0x90 },		\
	{ 11, 0x4a },		\
	{ 12, 0x52 },		\
	{ 13, 0x42 },		\
	{ 22, 0x40 },		\
	{ 24, 0x4a },		\
	{ 25, 0x80 },		\
	{ 27, 0x42 },		\
	{ 36, 0x80 },		\
	{ 37, 0x08 },		\
	{ 38, 0x89 },		\
	{ 39, 0x1b },		\
	{ 40, 0x0d },		\
	{ 41, 0x9b },		\
	{ 42, 0xd5 },		\
	{ 43, 0x72 },		\
	{ 44, 0x0e },		\
	{ 45, 0xa2 },		\
	{ 46, 0x6b },		\
	{ 48, 0x10 },		\
	{ 51, 0x3e },		\
	{ 52, 0x48 },		\
	{ 54, 0x38 },		\
	{ 56, 0xa1 },		\
	{ 57, 0x00 },		\
	{ 58, 0x39 },		\
	{ 60, 0x45 },		\
	{ 61, 0x91 },		\
	{ 62, 0x39 }

#define RT5592_5GHZ_DEF_RF	\
	{ 10, 0x97 },		\
	{ 11, 0x40 },		\
	{ 25, 0xbf },		\
	{ 27, 0x42 },		\
	{ 36, 0x00 },		\
	{ 37, 0x04 },		\
	{ 38, 0x85 },		\
	{ 40, 0x42 },		\
	{ 41, 0xbb },		\
	{ 42, 0xd7 },		\
	{ 45, 0x41 },		\
	{ 48, 0x00 },		\
	{ 57, 0x77 },		\
	{ 60, 0x05 },		\
	{ 61, 0x01 }

#define RT5592_CHAN_5GHZ	\
	{  36,  64, 12, 0x2e },	\
	{ 100, 165, 12, 0x0e },	\
	{  36,  64, 13, 0x22 },	\
	{ 100, 165, 13, 0x42 },	\
	{  36,  64, 22, 0x60 },	\
	{ 100, 165, 22, 0x40 },	\
	{  36,  64, 23, 0x7f },	\
	{ 100, 153, 23, 0x3c },	\
	{ 155, 165, 23, 0x38 },	\
	{  36,  50, 24, 0x09 },	\
	{  52,  64, 24, 0x07 },	\
	{ 100, 153, 24, 0x06 },	\
	{ 155, 165, 24, 0x05 },	\
	{  36,  64, 39, 0x1c },	\
	{ 100, 138, 39, 0x1a },	\
	{ 140, 165, 39, 0x18 },	\
	{  36,  64, 43, 0x5b },	\
	{ 100, 138, 43, 0x3b },	\
	{ 140, 165, 43, 0x1b },	\
	{  36,  64, 44, 0x40 },	\
	{ 100, 138, 44, 0x20 },	\
	{ 140, 165, 44, 0x10 },	\
	{  36,  64, 46, 0x00 },	\
	{ 100, 138, 46, 0x18 },	\
	{ 140, 165, 46, 0x08 },	\
	{  36,  64, 51, 0xfe },	\
	{ 100, 124, 51, 0xfc },	\
	{ 126, 165, 51, 0xec },	\
	{  36,  64, 52, 0x0c },	\
	{ 100, 138, 52, 0x06 },	\
	{ 140, 165, 52, 0x06 },	\
	{  36,  64, 54, 0xf8 },	\
	{ 100, 165, 54, 0xeb },	\
	{ 36,   50, 55, 0x06 },	\
	{ 52,   64, 55, 0x04 },	\
	{ 100, 138, 55, 0x01 },	\
	{ 140, 165, 55, 0x00 },	\
	{  36,  50, 56, 0xd3 },	\
	{  52, 128, 56, 0xbb },	\
	{ 130, 165, 56, 0xab },	\
	{  36,  64, 58, 0x15 },	\
	{ 100, 116, 58, 0x1d },	\
	{ 118, 165, 58, 0x15 },	\
	{  36,  64, 59, 0x7f },	\
	{ 100, 138, 59, 0x3f },	\
	{ 140, 165, 59, 0x7c },	\
	{  36,  64, 62, 0x15 },	\
	{ 100, 116, 62, 0x1d },	\
	{ 118, 165, 62, 0x15 }
