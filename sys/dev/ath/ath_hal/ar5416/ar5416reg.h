/*-
 * SPDX-License-Identifier: ISC
 *
 * Copyright (c) 2002-2008 Sam Leffler, Errno Consulting
 * Copyright (c) 2002-2008 Atheros Communications, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
 *
 * $FreeBSD$
 */
#ifndef _DEV_ATH_AR5416REG_H
#define	_DEV_ATH_AR5416REG_H

#include <dev/ath/ath_hal/ar5212/ar5212reg.h>

/*
 * Register added starting with the AR5416
 */
#define	AR_MIRT			0x0020	/* interrupt rate threshold */
#define	AR_TIMT			0x0028	/* Tx Interrupt mitigation threshold */
#define	AR_RIMT			0x002C	/* Rx Interrupt mitigation threshold */
#define	AR_GTXTO		0x0064	/* global transmit timeout */
#define	AR_GTTM			0x0068	/* global transmit timeout mode */
#define	AR_CST			0x006C	/* carrier sense timeout */
#define	AR_MAC_LED		0x1f04	/* LED control */
#define	AR_WA			0x4004	/* PCIE work-arounds */
#define	AR_PCIE_PM_CTRL		0x4014
#define	AR_AHB_MODE		0x4024	/* AHB mode for dma */
#define	AR_INTR_SYNC_CAUSE_CLR	0x4028	/* clear interrupt */
#define	AR_INTR_SYNC_CAUSE	0x4028	/* check pending interrupts */
#define	AR_INTR_SYNC_ENABLE	0x402c	/* enable interrupts */
#define	AR_INTR_ASYNC_MASK	0x4030	/* asynchronous interrupt mask */
#define	AR_INTR_SYNC_MASK	0x4034	/* synchronous interrupt mask */
#define	AR_INTR_ASYNC_CAUSE	0x4038	/* check pending interrupts */
#define	AR_INTR_ASYNC_CAUSE_CLR	0x4038	/* clear pending interrupts */
#define	AR_INTR_ASYNC_ENABLE	0x403c	/* enable interrupts */
#define	AR5416_PCIE_SERDES	0x4040
#define	AR5416_PCIE_SERDES2	0x4044
#define	AR_GPIO_IN_OUT		0x4048	/* GPIO input/output register */
#define	AR_GPIO_OE_OUT		0x404c	/* GPIO output enable register */
#define	AR_GPIO_INTR_POL	0x4050	/* GPIO interrupt polarity */

#define	AR_GPIO_INPUT_EN_VAL	0x4054	/* GPIO input enable and value */
#define	AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_DEF     0x00000004
#define	AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_S       2
#define	AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_DEF    0x00000008
#define	AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_S      3
#define	AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_DEF       0x00000010
#define	AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_S         4
#define	AR_GPIO_INPUT_EN_VAL_RFSILENT_DEF        0x00000080
#define	AR_GPIO_INPUT_EN_VAL_RFSILENT_DEF_S      7
#define	AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_BB      0x00000400
#define	AR_GPIO_INPUT_EN_VAL_BT_PRIORITY_BB_S    10
#define	AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_BB     0x00000800
#define	AR_GPIO_INPUT_EN_VAL_BT_FREQUENCY_BB_S   11
#define	AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB        0x00001000
#define	AR_GPIO_INPUT_EN_VAL_BT_ACTIVE_BB_S      12
#define	AR_GPIO_INPUT_EN_VAL_RFSILENT_BB         0x00008000
#define	AR_GPIO_INPUT_EN_VAL_RFSILENT_BB_S       15
#define	AR_GPIO_RTC_RESET_OVERRIDE_ENABLE        0x00010000
#define	AR_GPIO_JTAG_DISABLE                     0x00020000

#define	AR_GPIO_INPUT_MUX1	0x4058
#define	AR_GPIO_INPUT_MUX1_BT_PRIORITY           0x00000f00
#define	AR_GPIO_INPUT_MUX1_BT_PRIORITY_S         8
#define	AR_GPIO_INPUT_MUX1_BT_FREQUENCY          0x0000f000
#define	AR_GPIO_INPUT_MUX1_BT_FREQUENCY_S        12
#define	AR_GPIO_INPUT_MUX1_BT_ACTIVE             0x000f0000
#define	AR_GPIO_INPUT_MUX1_BT_ACTIVE_S           16

#define	AR_GPIO_INPUT_MUX2	0x405c
#define	AR_GPIO_INPUT_MUX2_CLK25                 0x0000000f
#define	AR_GPIO_INPUT_MUX2_CLK25_S               0
#define	AR_GPIO_INPUT_MUX2_RFSILENT              0x000000f0
#define	AR_GPIO_INPUT_MUX2_RFSILENT_S            4
#define	AR_GPIO_INPUT_MUX2_RTC_RESET             0x00000f00
#define	AR_GPIO_INPUT_MUX2_RTC_RESET_S           8

#define	AR_GPIO_OUTPUT_MUX1	0x4060
#define	AR_GPIO_OUTPUT_MUX2	0x4064
#define	AR_GPIO_OUTPUT_MUX3	0x4068

#define	AR_GPIO_OUTPUT_MUX_AS_OUTPUT             0
#define	AR_GPIO_OUTPUT_MUX_AS_PCIE_ATTENTION_LED 1
#define	AR_GPIO_OUTPUT_MUX_AS_PCIE_POWER_LED     2
#define	AR_GPIO_OUTPUT_MUX_AS_TX_FRAME           3
#define	AR_GPIO_OUTPUT_MUX_AS_RX_CLEAR_EXTERNAL  4
#define	AR_GPIO_OUTPUT_MUX_AS_MAC_NETWORK_LED    5
#define	AR_GPIO_OUTPUT_MUX_AS_MAC_POWER_LED      6

#define	AR_EEPROM_STATUS_DATA	0x407c
#define	AR_OBS			0x4080
#define	AR_GPIO_PDPU		0x4088

#ifdef	AH_SUPPORT_AR9130
#define	AR_RTC_BASE		0x20000
#else
#define	AR_RTC_BASE		0x7000
#endif	/* AH_SUPPORT_AR9130 */

#define	AR_RTC_RC		AR_RTC_BASE + 0x00	/* reset control */
#define	AR_RTC_PLL_CONTROL	AR_RTC_BASE + 0x14
#define	AR_RTC_RESET		AR_RTC_BASE + 0x40	/* RTC reset register */
#define	AR_RTC_STATUS		AR_RTC_BASE + 0x44	/* system sleep status */
#define	AR_RTC_SLEEP_CLK	AR_RTC_BASE + 0x48
#define	AR_RTC_FORCE_WAKE	AR_RTC_BASE + 0x4c	/* control MAC force wake */
#define	AR_RTC_INTR_CAUSE	AR_RTC_BASE + 0x50	/* RTC interrupt cause/clear */
#define	AR_RTC_INTR_ENABLE	AR_RTC_BASE + 0x54	/* RTC interrupt enable */
#define	AR_RTC_INTR_MASK	AR_RTC_BASE + 0x58	/* RTC interrupt mask */

#ifdef	AH_SUPPORT_AR9130
/* RTC_DERIVED_* - only for AR9130 */
#define	AR_RTC_DERIVED_CLK		(AR_RTC_BASE + 0x0038)
#define	AR_RTC_DERIVED_CLK_PERIOD	0x0000fffe
#define	AR_RTC_DERIVED_CLK_PERIOD_S	1
#endif	/* AH_SUPPORT_AR9130 */

/* AR_USEC: 0x801c */
#define	AR5416_USEC_TX_LAT	0x007FC000	/* tx latency to start of SIGNAL (usec) */
#define	AR5416_USEC_TX_LAT_S	14		/* tx latency to start of SIGNAL (usec) */
#define	AR5416_USEC_RX_LAT	0x1F800000	/* rx latency to start of SIGNAL (usec) */
#define	AR5416_USEC_RX_LAT_S	23		/* rx latency to start of SIGNAL (usec) */

#define	AR_RESET_TSF		0x8020

/*
 * AR_SLEEP1 / AR_SLEEP2 are in the same place as in
 * AR5212, however the fields have changed.
 */
#define	AR5416_SLEEP1		0x80d4
#define	AR5416_SLEEP2		0x80d8
#define	AR_RXFIFO_CFG		0x8114
#define	AR_PHY_ERR_1		0x812c
#define	AR_PHY_ERR_MASK_1	0x8130	/* mask for AR_PHY_ERR_1 */
#define	AR_PHY_ERR_2		0x8134
#define	AR_PHY_ERR_MASK_2	0x8138	/* mask for AR_PHY_ERR_2 */
#define	AR_TSFOOR_THRESHOLD	0x813c
#define	AR_PHY_ERR_3		0x8168
#define	AR_PHY_ERR_MASK_3	0x816c	/* mask for AR_PHY_ERR_3 */
#define	AR_BT_COEX_WEIGHT2	0x81c4
#define	AR_TXOP_X		0x81ec	/* txop for legacy non-qos */
#define	AR_TXOP_0_3		0x81f0	/* txop for various tid's */
#define	AR_TXOP_4_7		0x81f4
#define	AR_TXOP_8_11		0x81f8
#define	AR_TXOP_12_15		0x81fc
/* generic timers based on tsf - all uS */
#define	AR_NEXT_TBTT		0x8200
#define	AR_NEXT_DBA		0x8204
#define	AR_NEXT_SWBA		0x8208
#define	AR_NEXT_CFP		0x8208
#define	AR_NEXT_HCF		0x820C
#define	AR_NEXT_TIM		0x8210
#define	AR_NEXT_DTIM		0x8214
#define	AR_NEXT_QUIET		0x8218
#define	AR_NEXT_NDP		0x821C
#define	AR5416_BEACON_PERIOD	0x8220
#define	AR_DBA_PERIOD		0x8224
#define	AR_SWBA_PERIOD		0x8228
#define	AR_HCF_PERIOD		0x822C
#define	AR_TIM_PERIOD		0x8230
#define	AR_DTIM_PERIOD		0x8234
#define	AR_QUIET_PERIOD		0x8238
#define	AR_NDP_PERIOD		0x823C
#define	AR_TIMER_MODE		0x8240
#define	AR_SLP32_MODE		0x8244
#define	AR_SLP32_WAKE		0x8248
#define	AR_SLP32_INC		0x824c
#define	AR_SLP_CNT		0x8250	/* 32kHz cycles with mac asleep */
#define	AR_SLP_CYCLE_CNT	0x8254	/* absolute number of 32kHz cycles */
#define	AR_SLP_MIB_CTRL		0x8258
#define	AR_2040_MODE		0x8318
#define	AR_EXTRCCNT		0x8328	/* extension channel rx clear count */
#define	AR_SELFGEN_MASK		0x832c	/* rx and cal chain masks */
#define	AR_PHY_ERR_MASK_REG	0x8338
#define	AR_PCU_TXBUF_CTRL	0x8340
#define	AR_PCU_MISC_MODE2	0x8344

/* DMA & PCI Registers in PCI space (usable during sleep)*/
#define	AR_RC_AHB		0x00000001	/* AHB reset */
#define	AR_RC_APB		0x00000002	/* APB reset */
#define	AR_RC_HOSTIF		0x00000100	/* host interface reset */

#define	AR_MIRT_VAL		0x0000ffff	/* in uS */
#define	AR_MIRT_VAL_S		16

#define	AR_TIMT_LAST		0x0000ffff	/* Last packet threshold */
#define	AR_TIMT_LAST_S		0
#define	AR_TIMT_FIRST		0xffff0000	/* First packet threshold */
#define	AR_TIMT_FIRST_S		16

#define	AR_RIMT_LAST		0x0000ffff	/* Last packet threshold */
#define	AR_RIMT_LAST_S		0
#define	AR_RIMT_FIRST		0xffff0000	/* First packet threshold */
#define	AR_RIMT_FIRST_S		16

#define	AR_GTXTO_TIMEOUT_COUNTER    0x0000FFFF  // Mask for timeout counter (in TUs)
#define	AR_GTXTO_TIMEOUT_LIMIT      0xFFFF0000  // Mask for timeout limit (in  TUs)
#define	AR_GTXTO_TIMEOUT_LIMIT_S    16      // Shift for timeout limit

#define	AR_GTTM_USEC          0x00000001 // usec strobe
#define	AR_GTTM_IGNORE_IDLE   0x00000002 // ignore channel idle
#define	AR_GTTM_RESET_IDLE    0x00000004 // reset counter on channel idle low
#define	AR_GTTM_CST_USEC      0x00000008 // CST usec strobe

#define	AR_CST_TIMEOUT_COUNTER    0x0000FFFF  // Mask for timeout counter (in TUs)
#define	AR_CST_TIMEOUT_LIMIT      0xFFFF0000  // Mask for timeout limit (in  TUs)
#define	AR_CST_TIMEOUT_LIMIT_S    16      // Shift for timeout limit

/* MAC tx DMA size config  */
#define	AR_TXCFG_DMASZ_MASK	0x00000003
#define	AR_TXCFG_DMASZ_4B	0
#define	AR_TXCFG_DMASZ_8B	1
#define	AR_TXCFG_DMASZ_16B	2
#define	AR_TXCFG_DMASZ_32B	3
#define	AR_TXCFG_DMASZ_64B	4
#define	AR_TXCFG_DMASZ_128B	5
#define	AR_TXCFG_DMASZ_256B	6
#define	AR_TXCFG_DMASZ_512B	7
#define	AR_TXCFG_ATIM_TXPOLICY	0x00000800

/* MAC rx DMA size config  */
#define	AR_RXCFG_DMASZ_MASK	0x00000007
#define	AR_RXCFG_DMASZ_4B	0
#define	AR_RXCFG_DMASZ_8B	1
#define	AR_RXCFG_DMASZ_16B	2
#define	AR_RXCFG_DMASZ_32B	3
#define	AR_RXCFG_DMASZ_64B	4
#define	AR_RXCFG_DMASZ_128B	5
#define	AR_RXCFG_DMASZ_256B	6
#define	AR_RXCFG_DMASZ_512B	7

/* MAC Led registers */
#define	AR_CFG_SCLK_RATE_IND	0x00000003 /* sleep clock indication */
#define	AR_CFG_SCLK_RATE_IND_S	0
#define	AR_CFG_SCLK_32MHZ	0x00000000 /* Sleep clock rate */
#define	AR_CFG_SCLK_4MHZ	0x00000001 /* Sleep clock rate */
#define	AR_CFG_SCLK_1MHZ	0x00000002 /* Sleep clock rate */
#define	AR_CFG_SCLK_32KHZ	0x00000003 /* Sleep clock rate */
#define	AR_MAC_LED_BLINK_SLOW	0x00000008	/* LED slowest blink rate mode */
#define	AR_MAC_LED_BLINK_THRESH_SEL 0x00000070	/* LED blink threshold select */
#define	AR_MAC_LED_MODE		0x00000380	/* LED mode select */
#define	AR_MAC_LED_MODE_S	7
#define	AR_MAC_LED_MODE_PROP	0	/* Blink prop to filtered tx/rx */
#define	AR_MAC_LED_MODE_RPROP	1	/* Blink prop to unfiltered tx/rx */
#define	AR_MAC_LED_MODE_SPLIT	2	/* Blink power for tx/net for rx */
#define	AR_MAC_LED_MODE_RAND	3	/* Blink randomly */
#define	AR_MAC_LED_MODE_POWON	5	/* Power LED on (s/w control) */
#define	AR_MAC_LED_MODE_NETON	6	/* Network LED on (s/w control) */
#define	AR_MAC_LED_ASSOC	0x00000c00
#define	AR_MAC_LED_ASSOC_NONE	0x0	/* STA is not associated or trying */
#define	AR_MAC_LED_ASSOC_ACTIVE	0x1	/* STA is associated */
#define	AR_MAC_LED_ASSOC_PEND	0x2	/* STA is trying to associate */
#define	AR_MAC_LED_ASSOC_S	10

#define	AR_WA_BIT6		0x00000040
#define	AR_WA_BIT7		0x00000080
#define	AR_WA_D3_L1_DISABLE	0x00004000	/* */
#define	AR_WA_UNTIE_RESET_EN	0x00008000	/* ena PCI reset to POR */
#define	AR_WA_RESET_EN		0x00040000	/* ena AR_WA_UNTIE_RESET_EN */
#define	AR_WA_ANALOG_SHIFT	0x00100000
#define	AR_WA_POR_SHORT		0x00200000	/* PCIE phy reset control */
#define	AR_WA_BIT22		0x00400000
#define	AR_WA_BIT23		0x00800000

#define	AR_WA_DEFAULT		0x0000073f
#define	AR9280_WA_DEFAULT	0x0040073b	/* disable bit 2, see commit */
#define	AR9285_WA_DEFAULT	0x004a05cb

#define	AR_PCIE_PM_CTRL_ENA	0x00080000

#define	AR_AHB_EXACT_WR_EN	0x00000000	/* write exact bytes */
#define	AR_AHB_BUF_WR_EN	0x00000001	/* buffer write up to cacheline*/
#define	AR_AHB_EXACT_RD_EN	0x00000000	/* read exact bytes */
#define	AR_AHB_CACHELINE_RD_EN	0x00000002	/* read up to end of cacheline */
#define	AR_AHB_PREFETCH_RD_EN	0x00000004	/* prefetch up to page boundary*/
#define	AR_AHB_PAGE_SIZE_1K	0x00000000	/* set page-size as 1k */
#define	AR_AHB_PAGE_SIZE_2K	0x00000008	/* set page-size as 2k */
#define	AR_AHB_PAGE_SIZE_4K	0x00000010	/* set page-size as 4k */
/* Kiwi */
#define	AR_AHB_CUSTOM_BURST_EN	0x000000C0      /* set Custom Burst Mode */
#define	AR_AHB_CUSTOM_BURST_EN_S		6	/* set Custom Burst Mode */
#define	AR_AHB_CUSTOM_BURST_ASYNC_FIFO_VAL	3	/* set both bits in Async FIFO mode */

/* MAC PCU Registers */
#define	AR_STA_ID1_PRESERVE_SEQNUM	0x20000000 /* Don't replace seq num */

/* Extended PCU DIAG_SW control fields */
#define	AR_DIAG_DUAL_CHAIN_INFO	0x01000000	/* dual chain channel info */
#define	AR_DIAG_RX_ABORT	0x02000000	/* abort rx */
#define	AR_DIAG_SATURATE_CCNT	0x04000000	/* sat. cycle cnts (no shift) */
#define	AR_DIAG_OBS_PT_SEL2	0x08000000	/* observation point sel */
#define	AR_DIAG_RXCLEAR_CTL_LOW	0x10000000	/* force rx_clear(ctl) low/busy */
#define	AR_DIAG_RXCLEAR_EXT_LOW	0x20000000	/* force rx_clear(ext) low/busy */

#define	AR_TXOP_X_VAL	0x000000FF

#define	AR_RESET_TSF_ONCE	0x01000000	/* reset tsf once; self-clears*/

/* Interrupts */
#define	AR_ISR_TXMINTR		0x00080000	/* Maximum interrupt tx rate */
#define	AR_ISR_RXMINTR		0x01000000	/* Maximum interrupt rx rate */
#define	AR_ISR_GENTMR		0x10000000	/* OR of generic timer bits in S5 */
#define	AR_ISR_TXINTM		0x40000000	/* Tx int after mitigation */
#define	AR_ISR_RXINTM		0x80000000	/* Rx int after mitigation */

#define	AR_ISR_S2_CST		0x00400000	/* Carrier sense timeout */
#define	AR_ISR_S2_GTT		0x00800000	/* Global transmit timeout */
#define	AR_ISR_S2_TSFOOR	0x40000000	/* RX TSF out of range */

#define	AR_ISR_S5		0x0098
#define	AR_ISR_S5_S		0x00d8
#define	AR_ISR_S5_GENTIMER7	0x00000080 // Mask for timer 7 trigger
#define	AR_ISR_S5_TIM_TIMER	0x00000010 // TIM Timer ISR
#define	AR_ISR_S5_DTIM_TIMER	0x00000020 // DTIM Timer ISR
#define	AR_ISR_S5_GENTIMER_TRIG	0x0000FF80 // ISR for generic timer trigger 7-15
#define	AR_ISR_S5_GENTIMER_TRIG_S	0
#define	AR_ISR_S5_GENTIMER_THRESH	0xFF800000 // ISR for generic timer threshold 7-15
#define	AR_ISR_S5_GENTIMER_THRESH_S	16

#define	AR_INTR_SPURIOUS	0xffffffff
#define	AR_INTR_RTC_IRQ		0x00000001	/* rtc in shutdown state */
#define	AR_INTR_MAC_IRQ		0x00000002	/* pending mac interrupt */
#define	AR_INTR_EEP_PROT_ACCESS	0x00000004	/* eeprom protected access */
#define	AR_INTR_MAC_AWAKE	0x00020000	/* mac is awake */
#define	AR_INTR_MAC_ASLEEP	0x00040000	/* mac is asleep */

/* Interrupt Mask Registers */
#define	AR_IMR_TXMINTR		0x00080000	/* Maximum interrupt tx rate */
#define	AR_IMR_RXMINTR		0x01000000	/* Maximum interrupt rx rate */
#define	AR_IMR_TXINTM		0x40000000	/* Tx int after mitigation */
#define	AR_IMR_RXINTM		0x80000000	/* Rx int after mitigation */

#define	AR_IMR_S2_CST		0x00400000	/* Carrier sense timeout */
#define	AR_IMR_S2_GTT		0x00800000	/* Global transmit timeout */

/* synchronous interrupt signals */
#define	AR_INTR_SYNC_RTC_IRQ		0x00000001
#define	AR_INTR_SYNC_MAC_IRQ		0x00000002
#define	AR_INTR_SYNC_EEPROM_ILLEGAL_ACCESS	0x00000004
#define	AR_INTR_SYNC_APB_TIMEOUT	0x00000008
#define	AR_INTR_SYNC_PCI_MODE_CONFLICT	0x00000010
#define	AR_INTR_SYNC_HOST1_FATAL	0x00000020
#define	AR_INTR_SYNC_HOST1_PERR		0x00000040
#define	AR_INTR_SYNC_TRCV_FIFO_PERR	0x00000080
#define	AR_INTR_SYNC_RADM_CPL_EP	0x00000100
#define	AR_INTR_SYNC_RADM_CPL_DLLP_ABORT	0x00000200
#define	AR_INTR_SYNC_RADM_CPL_TLP_ABORT	0x00000400
#define	AR_INTR_SYNC_RADM_CPL_ECRC_ERR	0x00000800
#define	AR_INTR_SYNC_RADM_CPL_TIMEOUT	0x00001000
#define	AR_INTR_SYNC_LOCAL_TIMEOUT	0x00002000
#define	AR_INTR_SYNC_PM_ACCESS		0x00004000
#define	AR_INTR_SYNC_MAC_AWAKE		0x00008000
#define	AR_INTR_SYNC_MAC_ASLEEP		0x00010000
#define	AR_INTR_SYNC_MAC_SLEEP_ACCESS	0x00020000
#define	AR_INTR_SYNC_ALL		0x0003FFFF

/* default synchronous interrupt signals enabled */
#define	AR_INTR_SYNC_DEFAULT \
	(AR_INTR_SYNC_HOST1_FATAL | AR_INTR_SYNC_HOST1_PERR | \
	 AR_INTR_SYNC_RADM_CPL_EP | AR_INTR_SYNC_RADM_CPL_DLLP_ABORT | \
	 AR_INTR_SYNC_RADM_CPL_TLP_ABORT | AR_INTR_SYNC_RADM_CPL_ECRC_ERR | \
	 AR_INTR_SYNC_RADM_CPL_TIMEOUT | AR_INTR_SYNC_LOCAL_TIMEOUT | \
	 AR_INTR_SYNC_MAC_SLEEP_ACCESS)

#define	AR_INTR_SYNC_MASK_GPIO		0xFFFC0000
#define	AR_INTR_SYNC_MASK_GPIO_S	18

#define	AR_INTR_SYNC_ENABLE_GPIO	0xFFFC0000
#define	AR_INTR_SYNC_ENABLE_GPIO_S	18

#define	AR_INTR_ASYNC_MASK_GPIO		0xFFFC0000	/* async int mask */
#define	AR_INTR_ASYNC_MASK_GPIO_S	18

#define	AR_INTR_ASYNC_CAUSE_GPIO	0xFFFC0000	/* GPIO interrupts */
#define	AR_INTR_ASYNC_USED	(AR_INTR_MAC_IRQ | AR_INTR_ASYNC_CAUSE_GPIO)

#define	AR_INTR_ASYNC_ENABLE_GPIO	0xFFFC0000	/* enable interrupts */
#define	AR_INTR_ASYNC_ENABLE_GPIO_S	18

/* RTC registers */
#define	AR_RTC_RC_M		0x00000003
#define	AR_RTC_RC_MAC_WARM	0x00000001
#define	AR_RTC_RC_MAC_COLD	0x00000002
#ifdef	AH_SUPPORT_AR9130
#define AR_RTC_RC_COLD_RESET    0x00000004
#define AR_RTC_RC_WARM_RESET    0x00000008
#endif	/* AH_SUPPORT_AR9130 */
#define	AR_RTC_PLL_DIV		0x0000001f
#define	AR_RTC_PLL_DIV_S	0
#define	AR_RTC_PLL_DIV2		0x00000020
#define	AR_RTC_PLL_REFDIV_5	0x000000c0

#define	AR_RTC_SOWL_PLL_DIV		0x000003ff
#define	AR_RTC_SOWL_PLL_DIV_S		0
#define	AR_RTC_SOWL_PLL_REFDIV		0x00003C00
#define	AR_RTC_SOWL_PLL_REFDIV_S	10
#define	AR_RTC_SOWL_PLL_CLKSEL		0x0000C000
#define	AR_RTC_SOWL_PLL_CLKSEL_S	14

#define	AR_RTC_RESET_EN		0x00000001	/* Reset RTC bit */

#define	AR_RTC_PM_STATUS_M	0x0000000f	/* Pwr Mgmt Status */
#ifdef	AH_SUPPORT_AR9130
#define	AR_RTC_STATUS_M		0x0000000f	/* RTC Status */
#else
#define	AR_RTC_STATUS_M		0x0000003f	/* RTC Status */
#endif	/* AH_SUPPORT_AR9130 */
#define	AR_RTC_STATUS_SHUTDOWN	0x00000001
#define	AR_RTC_STATUS_ON	0x00000002
#define	AR_RTC_STATUS_SLEEP	0x00000004
#define	AR_RTC_STATUS_WAKEUP	0x00000008
#define	AR_RTC_STATUS_COLDRESET	0x00000010	/* Not currently used */
#define	AR_RTC_STATUS_PLLCHANGE	0x00000020	/* Not currently used */

#define	AR_RTC_SLEEP_DERIVED_CLK	0x2

#define	AR_RTC_FORCE_WAKE_EN	0x00000001	/* enable force wake */
#define	AR_RTC_FORCE_WAKE_ON_INT 0x00000002	/* auto-wake on MAC interrupt */

#define	AR_RTC_PLL_CLKSEL	0x00000300
#define	AR_RTC_PLL_CLKSEL_S	8

/* AR9280: rf long shift registers */
#define	AR_AN_RF2G1_CH0         0x7810
#define	AR_AN_RF5G1_CH0         0x7818
#define	AR_AN_RF2G1_CH1         0x7834
#define	AR_AN_RF5G1_CH1         0x783C
#define	AR_AN_TOP2		0x7894
#define	AR_AN_SYNTH9            0x7868

#define	AR_AN_RF2G1_CH0_OB      0x03800000
#define	AR_AN_RF2G1_CH0_OB_S    23
#define	AR_AN_RF2G1_CH0_DB      0x1C000000
#define	AR_AN_RF2G1_CH0_DB_S    26

#define	AR_AN_RF5G1_CH0_OB5     0x00070000
#define	AR_AN_RF5G1_CH0_OB5_S   16
#define	AR_AN_RF5G1_CH0_DB5     0x00380000
#define	AR_AN_RF5G1_CH0_DB5_S   19

#define	AR_AN_RF2G1_CH1_OB      0x03800000
#define	AR_AN_RF2G1_CH1_OB_S    23
#define	AR_AN_RF2G1_CH1_DB      0x1C000000
#define	AR_AN_RF2G1_CH1_DB_S    26

#define	AR_AN_RF5G1_CH1_OB5     0x00070000
#define	AR_AN_RF5G1_CH1_OB5_S   16
#define	AR_AN_RF5G1_CH1_DB5     0x00380000
#define	AR_AN_RF5G1_CH1_DB5_S   19

#define AR_AN_TOP1                  0x7890
#define AR_AN_TOP1_DACIPMODE        0x00040000
#define AR_AN_TOP1_DACIPMODE_S      18

#define	AR_AN_TOP2_XPABIAS_LVL      0xC0000000
#define	AR_AN_TOP2_XPABIAS_LVL_S    30
#define	AR_AN_TOP2_LOCALBIAS        0x00200000
#define	AR_AN_TOP2_LOCALBIAS_S      21
#define	AR_AN_TOP2_PWDCLKIND        0x00400000
#define	AR_AN_TOP2_PWDCLKIND_S      22

#define	AR_AN_SYNTH9_REFDIVA    0xf8000000
#define	AR_AN_SYNTH9_REFDIVA_S  27

#define	AR9271_AN_RF2G6_OFFS	0x07f00000
#define	AR9271_AN_RF2G6_OFFS_S	20

/* Sleep control */
#define	AR5416_SLEEP1_ASSUME_DTIM	0x00080000
#define	AR5416_SLEEP1_CAB_TIMEOUT	0xFFE00000	/* Cab timeout (TU) */
#define	AR5416_SLEEP1_CAB_TIMEOUT_S	21

#define	AR5416_SLEEP2_BEACON_TIMEOUT	0xFFE00000	/* Beacon timeout (TU)*/
#define	AR5416_SLEEP2_BEACON_TIMEOUT_S	21

/* Sleep Registers */
#define	AR_SLP32_HALFCLK_LATENCY      0x000FFFFF	/* rising <-> falling edge */
#define	AR_SLP32_ENA		0x00100000
#define	AR_SLP32_TSF_WRITE_STATUS      0x00200000	/* tsf update in progress */

#define	AR_SLP32_WAKE_XTL_TIME	0x0000FFFF	/* time to wake crystal */

#define	AR_SLP32_TST_INC	0x000FFFFF

#define	AR_SLP_MIB_CLEAR	0x00000001	/* clear pending */
#define	AR_SLP_MIB_PENDING	0x00000002	/* clear counters */

#define	AR_TIMER_MODE_TBTT		0x00000001
#define	AR_TIMER_MODE_DBA		0x00000002
#define	AR_TIMER_MODE_SWBA		0x00000004
#define	AR_TIMER_MODE_HCF		0x00000008
#define	AR_TIMER_MODE_TIM		0x00000010
#define	AR_TIMER_MODE_DTIM		0x00000020
#define	AR_TIMER_MODE_QUIET		0x00000040
#define	AR_TIMER_MODE_NDP		0x00000080
#define	AR_TIMER_MODE_OVERFLOW_INDEX	0x00000700
#define	AR_TIMER_MODE_OVERFLOW_INDEX_S	8
#define	AR_TIMER_MODE_THRESH		0xFFFFF000
#define	AR_TIMER_MODE_THRESH_S		12

/* PCU Misc modes */
#define	AR_PCU_FORCE_BSSID_MATCH	0x00000001 /* force bssid to match */
#define	AR_PCU_MIC_NEW_LOC_ENA		0x00000004 /* tx/rx mic keys together */
#define	AR_PCU_TX_ADD_TSF		0x00000008 /* add tx_tsf + int_tsf */
#define	AR_PCU_CCK_SIFS_MODE		0x00000010 /* assume 11b sifs */
#define	AR_PCU_RX_ANT_UPDT		0x00000800 /* KC_RX_ANT_UPDATE */
#define	AR_PCU_TXOP_TBTT_LIMIT_ENA	0x00001000 /* enforce txop / tbtt */
#define	AR_PCU_MISS_BCN_IN_SLEEP	0x00004000 /* count bmiss's when sleeping */
#define	AR_PCU_BUG_12306_FIX_ENA	0x00020000 /* use rx_clear to count sifs */
#define	AR_PCU_FORCE_QUIET_COLL		0x00040000 /* kill xmit for channel change */
#define	AR_PCU_BT_ANT_PREVENT_RX	0x00100000
#define	AR_PCU_BT_ANT_PREVENT_RX_S	20
#define	AR_PCU_TBTT_PROTECT		0x00200000 /* no xmit up to tbtt+20 uS */
#define	AR_PCU_CLEAR_VMF		0x01000000 /* clear vmf mode (fast cc)*/
#define	AR_PCU_CLEAR_BA_VALID		0x04000000 /* clear ba state */
#define	AR_PCU_SEL_EVM			0x08000000 /* select EVM data or PLCP header */

#define	AR_PCU_MISC_MODE2_MGMT_CRYPTO_ENABLE		0x00000002
#define	AR_PCU_MISC_MODE2_NO_CRYPTO_FOR_NON_DATA_PKT	0x00000004
/*
 * This bit enables the Multicast search based on both MAC Address and Key ID. 
 * If bit is 0, then Multicast search is based on MAC address only.
 * For Merlin and above only.
 */
#define	AR_PCU_MISC_MODE2_ADHOC_MCAST_KEYID_ENABLE	0x00000040
#define	AR_PCU_MISC_MODE2_ENABLE_AGGWEP	0x00020000	/* Kiwi or later? */
#define	AR_PCU_MISC_MODE2_HWWAR1	0x00100000
#define	AR_PCU_MISC_MODE2_HWWAR2	0x02000000

/* For Kiwi */
#define	AR_MAC_PCU_ASYNC_FIFO_REG3		0x8358
#define	AR_MAC_PCU_ASYNC_FIFO_REG3_DATAPATH_SEL	0x00000400
#define	AR_MAC_PCU_ASYNC_FIFO_REG3_SOFT_RESET	0x80000000

/* TSF2. For Kiwi only */
#define	AR_TSF2_L32			0x8390
#define	AR_TSF2_U32			0x8394

/* MAC Direct Connect Control. For Kiwi only */
#define	AR_DIRECT_CONNECT		0x83A0
#define	AR_DC_AP_STA_EN			0x00000001

/* GPIO Interrupt */
#define	AR_INTR_GPIO		0x3FF00000	/* gpio interrupted */
#define	AR_INTR_GPIO_S		20

#define	AR_GPIO_OUT_CTRL	0x000003FF	/* 0 = out, 1 = in */
#define	AR_GPIO_OUT_VAL		0x000FFC00
#define	AR_GPIO_OUT_VAL_S	10
#define	AR_GPIO_INTR_CTRL	0x3FF00000
#define	AR_GPIO_INTR_CTRL_S	20

#define	AR_GPIO_IN_VAL		0x0FFFC000	/* pre-9280 */
#define	AR_GPIO_IN_VAL_S	14
#define	AR928X_GPIO_IN_VAL	0x000FFC00
#define	AR928X_GPIO_IN_VAL_S	10
#define	AR9285_GPIO_IN_VAL	0x00FFF000
#define	AR9285_GPIO_IN_VAL_S	12
#define	AR9287_GPIO_IN_VAL	0x003FF800
#define	AR9287_GPIO_IN_VAL_S	11

#define	AR_GPIO_OE_OUT_DRV	0x3	/* 2 bit mask shifted by 2*bitpos */
#define	AR_GPIO_OE_OUT_DRV_NO	0x0	/* tristate */
#define	AR_GPIO_OE_OUT_DRV_LOW	0x1	/* drive if low */
#define	AR_GPIO_OE_OUT_DRV_HI	0x2	/* drive if high */
#define	AR_GPIO_OE_OUT_DRV_ALL	0x3	/* drive always */

#define	AR_GPIO_INTR_POL_VAL	0x1FFF
#define	AR_GPIO_INTR_POL_VAL_S	0

#define	AR_GPIO_JTAG_DISABLE	0x00020000

#define	AR_2040_JOINED_RX_CLEAR	0x00000001	/* use ctl + ext rx_clear for cca */

#define	AR_PCU_TXBUF_CTRL_SIZE_MASK	0x7FF
#define	AR_PCU_TXBUF_CTRL_USABLE_SIZE	0x700
#define	AR_9285_PCU_TXBUF_CTRL_USABLE_SIZE 0x380

/* IFS, SIFS, slot, etc for Async FIFO mode (Kiwi) */
#define	AR_D_GBL_IFS_SIFS_ASYNC_FIFO_DUR	0x000003AB
#define	AR_TIME_OUT_ACK_CTS_ASYNC_FIFO_DUR	0x16001D56
#define	AR_USEC_ASYNC_FIFO_DUR			0x12e00074
#define	AR_D_GBL_IFS_SLOT_ASYNC_FIFO_DUR	0x00000420
#define	AR_D_GBL_IFS_EIFS_ASYNC_FIFO_DUR	0x0000A5EB

/* Used by Kiwi Async FIFO */
#define	AR_MAC_PCU_LOGIC_ANALYZER		0x8264
#define	AR_MAC_PCU_LOGIC_ANALYZER_DISBUG20768	0x20000000

/* Eeprom defines */
#define	AR_EEPROM_STATUS_DATA_VAL           0x0000ffff
#define	AR_EEPROM_STATUS_DATA_VAL_S         0
#define	AR_EEPROM_STATUS_DATA_BUSY          0x00010000
#define	AR_EEPROM_STATUS_DATA_BUSY_ACCESS   0x00020000
#define	AR_EEPROM_STATUS_DATA_PROT_ACCESS   0x00040000
#define	AR_EEPROM_STATUS_DATA_ABSENT_ACCESS 0x00080000

/* K2 (9271) */
#define	AR9271_CLOCK_CONTROL		0x50040
#define	AR9271_CLOCK_SELECTION_22	0x0
#define	AR9271_CLOCK_SELECTION_88	0x1
#define	AR9271_CLOCK_SELECTION_44	0x2
#define	AR9271_CLOCK_SELECTION_117	0x4
#define	AR9271_CLOCK_SELECTION_OSC_40	0x6
#define	AR9271_CLOCK_SELECTION_RTC	0x7
#define	AR9271_SPI_SEL			0x100
#define	AR9271_UART_SEL			0x200

#define	AR9271_RESET_POWER_DOWN_CONTROL	0x50044
#define	AR9271_RADIO_RF_RST		0x20
#define	AR9271_GATE_MAC_CTL		0x4000
#define	AR9271_MAIN_PLL_PWD_CTL		0x40000

#define	AR9271_CLKMISC			0x4090
#define	AR9271_OSC_to_10M_EN		0x00000001

/*
 * AR5212 defines the MAC revision mask as 0xF, but both ath9k and
 * the Atheros HAL define it as 0x7. 
 *
 * What this means however is AR5416 silicon revisions have
 * changed. The below macros are for what is contained in the
 * lower four bits; if the lower three bits are taken into account
 * the revisions become 1.0 => 0x0, 2.0 => 0x1, 2.2 => 0x2.
 */

/* These are the legacy revisions, with a four bit AR_SREV_REVISION mask */
#define	AR_SREV_REVISION_OWL_10		0x08
#define	AR_SREV_REVISION_OWL_20		0x09
#define	AR_SREV_REVISION_OWL_22		0x0a

#define	AR_RAD5133_SREV_MAJOR		0xc0	/* Fowl: 2+5G/3x3 */
#define	AR_RAD2133_SREV_MAJOR		0xd0	/* Fowl: 2G/3x3   */
#define	AR_RAD5122_SREV_MAJOR		0xe0	/* Fowl: 5G/2x2   */
#define	AR_RAD2122_SREV_MAJOR		0xf0	/* Fowl: 2+5G/2x2 */

/* Test macro for owl 1.0 */
#define	IS_5416V1(_ah)	(AR_SREV_OWL((_ah)) && AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_OWL_10)
#define	IS_5416V2(_ah)	(AR_SREV_OWL((_ah)) && AH_PRIVATE((_ah))->ah_macRev >= AR_SREV_REVISION_OWL_20)
#define	IS_5416V2_2(_ah)	(AR_SREV_OWL((_ah)) && AH_PRIVATE((_ah))->ah_macRev == AR_SREV_REVISION_OWL_22)

/* Misc; compatibility with Atheros HAL */
#define	AR_SREV_5416_V20_OR_LATER(_ah)	(AR_SREV_HOWL((_ah)) || AR_SREV_OWL_20_OR_LATER(_ah))
#define	AR_SREV_5416_V22_OR_LATER(_ah)	(AR_SREV_HOWL((_ah)) || AR_SREV_OWL_22_OR_LATER(_ah)) 

/* Expanded Mac Silicon Rev (16 bits starting with Sowl) */
#define	AR_XSREV_ID		0xFFFFFFFF	/* Chip ID */
#define	AR_XSREV_ID_S		0
#define	AR_XSREV_VERSION	0xFFFC0000	/* Chip version */
#define	AR_XSREV_VERSION_S	18
#define	AR_XSREV_TYPE		0x0003F000	/* Chip type */
#define	AR_XSREV_TYPE_S		12
#define	AR_XSREV_TYPE_CHAIN	0x00001000	/* Chain Mode (1:3 chains,
						 * 0:2 chains) */
#define	AR_XSREV_TYPE_HOST_MODE 0x00002000	/* Host Mode (1:PCI, 0:PCIe) */
#define	AR_XSREV_REVISION	0x00000F00
#define	AR_XSREV_REVISION_S	8

#define	AR_XSREV_VERSION_OWL_PCI	0x0D
#define	AR_XSREV_VERSION_OWL_PCIE	0x0C


/*
 * These are from ath9k/Atheros and assume an AR_SREV version mask
 * of 0x07, rather than 0x0F which is being used in the FreeBSD HAL.
 * Thus, don't use these values as they're incorrect here; use
 * AR_SREV_REVISION_OWL_{10,20,22}.
 */
#if 0
#define	AR_XSREV_REVISION_OWL_10	0	/* Owl 1.0 */
#define	AR_XSREV_REVISION_OWL_20	1	/* Owl 2.0/2.1 */
#define	AR_XSREV_REVISION_OWL_22	2	/* Owl 2.2 */
#endif

#define	AR_XSREV_VERSION_HOWL		0x14	/* Howl (AR9130) */
#define	AR_XSREV_VERSION_SOWL		0x40	/* Sowl (AR9160) */
#define	AR_XSREV_REVISION_SOWL_10	0	/* Sowl 1.0 */
#define	AR_XSREV_REVISION_SOWL_11	1	/* Sowl 1.1 */
#define	AR_XSREV_VERSION_MERLIN		0x80	/* Merlin Version */
#define	AR_XSREV_REVISION_MERLIN_10	0	/* Merlin 1.0 */
#define	AR_XSREV_REVISION_MERLIN_20	1	/* Merlin 2.0 */
#define	AR_XSREV_REVISION_MERLIN_21	2	/* Merlin 2.1 */
#define	AR_XSREV_VERSION_KITE		0xC0	/* Kite Version */
#define	AR_XSREV_REVISION_KITE_10	0	/* Kite 1.0 */
#define	AR_XSREV_REVISION_KITE_11	1	/* Kite 1.1 */
#define	AR_XSREV_REVISION_KITE_12	2	/* Kite 1.2 */
#define	AR_XSREV_VERSION_KIWI		0x180	/* Kiwi (AR9287) */
#define	AR_XSREV_REVISION_KIWI_10	0	/* Kiwi 1.0 */
#define	AR_XSREV_REVISION_KIWI_11	1	/* Kiwi 1.1 */
#define	AR_XSREV_REVISION_KIWI_12	2	/* Kiwi 1.2 */
#define	AR_XSREV_REVISION_KIWI_13	3	/* Kiwi 1.3 */

/* Owl (AR5416) */
#define	AR_SREV_OWL(_ah) \
	((AH_PRIVATE((_ah))->ah_macVersion == AR_XSREV_VERSION_OWL_PCI) || \
	 (AH_PRIVATE((_ah))->ah_macVersion == AR_XSREV_VERSION_OWL_PCIE))

#define	AR_SREV_OWL_20_OR_LATER(_ah) \
	((AR_SREV_OWL(_ah) &&						\
	 AH_PRIVATE((_ah))->ah_macRev >= AR_SREV_REVISION_OWL_20) ||	\
	 AH_PRIVATE((_ah))->ah_macVersion >= AR_XSREV_VERSION_HOWL)

#define	AR_SREV_OWL_22_OR_LATER(_ah) \
	((AR_SREV_OWL(_ah) &&						\
	 AH_PRIVATE((_ah))->ah_macRev >= AR_SREV_REVISION_OWL_22) ||	\
	 AH_PRIVATE((_ah))->ah_macVersion >= AR_XSREV_VERSION_HOWL)

/* Howl (AR9130) */

#define AR_SREV_HOWL(_ah) \
	(AH_PRIVATE((_ah))->ah_macVersion == AR_XSREV_VERSION_HOWL)

#define	AR_SREV_9100(_ah)	AR_SREV_HOWL(_ah)

/* Sowl (AR9160) */

#define	AR_SREV_SOWL(_ah) \
	(AH_PRIVATE((_ah))->ah_macVersion == AR_XSREV_VERSION_SOWL)

#define	AR_SREV_SOWL_10_OR_LATER(_ah) \
	(AH_PRIVATE((_ah))->ah_macVersion >= AR_XSREV_VERSION_SOWL)

#define	AR_SREV_SOWL_11(_ah) \
	(AR_SREV_SOWL(_ah) && \
	 AH_PRIVATE((_ah))->ah_macRev == AR_XSREV_REVISION_SOWL_11)

/* Merlin (AR9280) */

#define	AR_SREV_MERLIN(_ah) \
	(AH_PRIVATE((_ah))->ah_macVersion == AR_XSREV_VERSION_MERLIN)

#define	AR_SREV_MERLIN_10_OR_LATER(_ah)	\
	(AH_PRIVATE((_ah))->ah_macVersion >= AR_XSREV_VERSION_MERLIN)

#define	AR_SREV_MERLIN_20(_ah) \
	(AR_SREV_MERLIN(_ah) && \
	 AH_PRIVATE((_ah))->ah_macRev >= AR_XSREV_REVISION_MERLIN_20)

#define	AR_SREV_MERLIN_20_OR_LATER(_ah) \
	((AH_PRIVATE((_ah))->ah_macVersion > AR_XSREV_VERSION_MERLIN) ||	\
	 (AR_SREV_MERLIN((_ah)) &&						\
	 AH_PRIVATE((_ah))->ah_macRev >= AR_XSREV_REVISION_MERLIN_20))

/* Kite (AR9285) */

#define	AR_SREV_KITE(_ah) \
	(AH_PRIVATE((_ah))->ah_macVersion == AR_XSREV_VERSION_KITE)

#define	AR_SREV_KITE_10_OR_LATER(_ah) \
	(AH_PRIVATE((_ah))->ah_macVersion >= AR_XSREV_VERSION_KITE)

#define	AR_SREV_KITE_11(_ah) \
	(AR_SREV_KITE(ah) && \
	 AH_PRIVATE((_ah))->ah_macRev == AR_XSREV_REVISION_KITE_11)

#define	AR_SREV_KITE_11_OR_LATER(_ah) \
	((AH_PRIVATE((_ah))->ah_macVersion > AR_XSREV_VERSION_KITE) ||	\
	 (AR_SREV_KITE((_ah)) &&					\
	 AH_PRIVATE((_ah))->ah_macRev >= AR_XSREV_REVISION_KITE_11))

#define	AR_SREV_KITE_12(_ah) \
	(AR_SREV_KITE(ah) && \
	 AH_PRIVATE((_ah))->ah_macRev == AR_XSREV_REVISION_KITE_12)

#define	AR_SREV_KITE_12_OR_LATER(_ah) \
	((AH_PRIVATE((_ah))->ah_macVersion > AR_XSREV_VERSION_KITE) ||	\
	 (AR_SREV_KITE((_ah)) &&					\
	 AH_PRIVATE((_ah))->ah_macRev >= AR_XSREV_REVISION_KITE_12))

#define	AR_SREV_9285E_20(_ah) \
	(AR_SREV_KITE_12_OR_LATER(_ah) && \
	((OS_REG_READ(_ah, AR_AN_SYNTH9) & 0x7) == 0x1))

#define AR_SREV_KIWI(_ah) \
	(AH_PRIVATE((_ah))->ah_macVersion == AR_XSREV_VERSION_KIWI)

#define AR_SREV_KIWI_10_OR_LATER(_ah) \
	(AH_PRIVATE((_ah))->ah_macVersion >= AR_XSREV_VERSION_KIWI)

/* XXX TODO: make these handle macVersion > Kiwi */
#define AR_SREV_KIWI_11_OR_LATER(_ah) \
	(AR_SREV_KIWI(_ah) && \
	 AH_PRIVATE((_ah))->ah_macRev >= AR_XSREV_REVISION_KIWI_11)

#define AR_SREV_KIWI_11(_ah) \
	(AR_SREV_KIWI(_ah) && \
	 AH_PRIVATE((_ah))->ah_macRev == AR_XSREV_REVISION_KIWI_11)

#define AR_SREV_KIWI_12(_ah) \
	(AR_SREV_KIWI(_ah) && \
	 AH_PRIVATE((_ah))->ah_macRev == AR_XSREV_REVISION_KIWI_12)

#define	AR_SREV_KIWI_12_OR_LATER(_ah) \
	(AR_SREV_KIWI(_ah) && \
	 AH_PRIVATE((_ah))->ah_macRev >= AR_XSREV_REVISION_KIWI_12)

#define	AR_SREV_KIWI_13_OR_LATER(_ah) \
	(AR_SREV_KIWI(_ah) && \
	 AH_PRIVATE((_ah))->ah_macRev >= AR_XSREV_REVISION_KIWI_13)


/* Not yet implemented chips */
#define	AR_SREV_9271(_ah)	0

#endif /* _DEV_ATH_AR5416REG_H */
