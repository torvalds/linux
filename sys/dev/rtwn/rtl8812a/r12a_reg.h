/*-
 * Copyright (c) 2016 Andriy Voskoboinyk <avos@FreeBSD.org>
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

#ifndef R12A_REG_H
#define R12A_REG_H

#include <dev/rtwn/rtl8188e/r88e_reg.h>

/*
 * MAC registers.
 */
/* System Configuration. */
#define R12A_SDIO_CTRL			0x070
#define R12A_RF_B_CTRL			0x076
/* Rx DMA Configuration. */
#define R12A_RXDMA_PRO			0x290
#define R12A_EARLY_MODE_CONTROL		0x2bc
/* Protocol Configuration. */
#define R12A_TXPKT_EMPTY		0x41a
#define R12A_ARFR_5G(i)			(0x444 + (i) * 8)
#define R12A_CCK_CHECK			0x454
#define R12A_AMPDU_MAX_TIME		0x456
#define R12A_AMPDU_MAX_LENGTH		R92C_AGGLEN_LMT
#define R12A_DATA_SEC			0x483
#define R12A_ARFR_2G(i)			(0x48c + (i) * 8)
#define R12A_HT_SINGLE_AMPDU		0x4c7


/* Bits for R92C_MAC_PHY_CTRL. */
#define R12A_MAC_PHY_CRYSTALCAP_M	0x7ff80000
#define R12A_MAC_PHY_CRYSTALCAP_S	19

/* Bits for R92C_LEDCFG2. */
#define R12A_LEDCFG2_ENA		0x20

/* Bits for R12A_RXDMA_PRO. */
#define R12A_DMA_MODE			0x02
#define R12A_BURST_CNT_M		0x0c
#define R12A_BURST_CNT_S		2
#define R12A_BURST_SZ_M			0x30
#define R12A_BURST_SZ_S			4
#define R12A_BURST_SZ_USB3		0
#define R12A_BURST_SZ_USB2		1
#define R12A_BURST_SZ_USB1		2

/* Bits for R12A_CCK_CHECK. */
#define R12A_CCK_CHECK_BCN1		0x20
#define R12A_CCK_CHECK_5GHZ		0x80

/* Bits for R12A_DATA_SEC. */
#define R12A_DATA_SEC_NO_EXT		0x00
#define R12A_DATA_SEC_PRIM_UP_20	0x01
#define R12A_DATA_SEC_PRIM_DOWN_20	0x02
#define R12A_DATA_SEC_PRIM_UPPER_20	0x03
#define R12A_DATA_SEC_PRIM_LOWER_20	0x04
#define R12A_DATA_SEC_PRIM_UP_40	0x90
#define R12A_DATA_SEC_PRIM_DOWN_40	0xa0

/* Bits for R12A_HT_SINGLE_AMPDU. */
#define R12A_HT_SINGLE_AMPDU_PKT_ENA	0x80

/* Bits for R92C_RCR. */
#define R12A_RCR_DIS_CHK_14		0x00200000
#define R12A_RCR_TCP_OFFLD_EN		0x02000000
#define R12A_RCR_VHT_ACK		0x04000000


/*
 * Baseband registers.
 */
#define R12A_CCK_RPT_FORMAT		0x804
#define R12A_OFDMCCK_EN			0x808
#define R12A_RX_PATH			R12A_OFDMCCK_EN
#define R12A_TX_PATH			0x80c
#define R12A_TXAGC_TABLE_SELECT		0x82c
#define R12A_PWED_TH			0x830
#define R12A_BW_INDICATION		0x834
#define R12A_CCA_ON_SEC			0x838
#define R12A_L1_PEAK_TH			0x848
#define R12A_FC_AREA			0x860
#define R12A_RFMOD			0x8ac
#define R12A_HSSI_PARAM2		0x8b0
#define R12A_ADC_BUF_CLK		0x8c4
#define R12A_ANTSEL_SW			0x900
#define R12A_SINGLETONE_CONT_TX		0x914
#define R12A_CCK_RX_PATH		0xa04
#define R12A_HSSI_PARAM1(chain)		(0xc00 + (chain) * 0x200)
#define R12A_TX_SCALE(chain)		(0xc1c + (chain) * 0x200)
#define R12A_TXAGC_CCK11_1(chain)	(0xc20 + (chain) * 0x200)
#define R12A_TXAGC_OFDM18_6(chain)	(0xc24 + (chain) * 0x200)
#define R12A_TXAGC_OFDM54_24(chain)	(0xc28 + (chain) * 0x200)
#define R12A_TXAGC_MCS3_0(chain)	(0xc2c + (chain) * 0x200)
#define R12A_TXAGC_MCS7_4(chain)	(0xc30 + (chain) * 0x200)
#define R12A_TXAGC_MCS11_8(chain)	(0xc34 + (chain) * 0x200)
#define R12A_TXAGC_MCS15_12(chain)	(0xc38 + (chain) * 0x200)
#define R12A_TXAGC_NSS1IX3_1IX0(chain)	(0xc3c + (chain) * 0x200)
#define R12A_TXAGC_NSS1IX7_1IX4(chain)	(0xc40 + (chain) * 0x200)
#define R12A_TXAGC_NSS2IX1_1IX8(chain)	(0xc44 + (chain) * 0x200)
#define R12A_TXAGC_NSS2IX5_2IX2(chain)	(0xc48 + (chain) * 0x200)
#define R12A_TXAGC_NSS2IX9_2IX6(chain)	(0xc4c + (chain) * 0x200)
#define R12A_INITIAL_GAIN(chain)	(0xc50 + (chain) * 0x200)
#define R12A_AFE_POWER_1(chain)		(0xc60 + (chain) * 0x200)
#define R12A_AFE_POWER_2(chain)		(0xc64 + (chain) * 0x200)
#define R12A_SLEEP_NAV(chain)		(0xc80 + (chain) * 0x200)
#define R12A_PMPD(chain)		(0xc84 + (chain) * 0x200)
#define R12A_LSSI_PARAM(chain)		(0xc90 + (chain) * 0x200)
#define R12A_RFE_PINMUX(chain)		(0xcb0 + (chain) * 0x200)
#define R12A_RFE_INV(chain)		(0xcb4 + (chain) * 0x200)
#define R12A_RFE(chain)			(0xcb8 + (chain) * 0x200)
#define R12A_HSPI_READBACK(chain)	(0xd04 + (chain) * 0x40)
#define R12A_LSSI_READBACK(chain)	(0xd08 + (chain) * 0x40)

/* Bits for R12A_CCK_RPT_FORMAT. */
#define R12A_CCK_RPT_FORMAT_HIPWR	0x00010000

/* Bits for R12A_OFDMCCK_EN. */
#define R12A_OFDMCCK_EN_CCK	0x10000000
#define R12A_OFDMCCK_EN_OFDM	0x20000000

/* Bits for R12A_CCA_ON_SEC. */
#define R12A_CCA_ON_SEC_EXT_CHAN_M	0xf0000000
#define R12A_CCA_ON_SEC_EXT_CHAN_S	28

/* Bits for R12A_RFE_PINMUX(i). */
#define R12A_RFE_PINMUX_PA_A_MASK	0x000000f0
#define R12A_RFE_PINMUX_LNA_MASK	0x0000f000

/* Bits for R12A_RFMOD. */
#define R12A_RFMOD_EXT_CHAN_M		0x3C
#define R12A_RFMOD_EXT_CHAN_S		2

/* Bits for R12A_HSSI_PARAM2. */
#define R12A_HSSI_PARAM2_READ_ADDR_MASK	0xff

/* Bits for R12A_HSSI_PARAM1(i). */
#define R12A_HSSI_PARAM1_PI		0x00000004

/* Bits for R12A_TX_SCALE(i). */
#define R12A_TX_SCALE_SWING_M		0xffe00000
#define R12A_TX_SCALE_SWING_S		21

/* Bits for R12A_TXAGC_CCK11_1(i). */
#define R12A_TXAGC_CCK1_M		0x000000ff
#define R12A_TXAGC_CCK1_S		0
#define R12A_TXAGC_CCK2_M		0x0000ff00
#define R12A_TXAGC_CCK2_S		8
#define R12A_TXAGC_CCK55_M		0x00ff0000
#define R12A_TXAGC_CCK55_S		16
#define R12A_TXAGC_CCK11_M		0xff000000
#define R12A_TXAGC_CCK11_S		24

/* Bits for R12A_TXAGC_OFDM18_6(i). */
#define R12A_TXAGC_OFDM06_M		0x000000ff
#define R12A_TXAGC_OFDM06_S		0
#define R12A_TXAGC_OFDM09_M		0x0000ff00
#define R12A_TXAGC_OFDM09_S		8
#define R12A_TXAGC_OFDM12_M		0x00ff0000
#define R12A_TXAGC_OFDM12_S		16
#define R12A_TXAGC_OFDM18_M		0xff000000
#define R12A_TXAGC_OFDM18_S		24

/* Bits for R12A_TXAGC_OFDM54_24(i). */
#define R12A_TXAGC_OFDM24_M		0x000000ff
#define R12A_TXAGC_OFDM24_S		0
#define R12A_TXAGC_OFDM36_M		0x0000ff00
#define R12A_TXAGC_OFDM36_S		8
#define R12A_TXAGC_OFDM48_M		0x00ff0000
#define R12A_TXAGC_OFDM48_S		16
#define R12A_TXAGC_OFDM54_M		0xff000000
#define R12A_TXAGC_OFDM54_S		24

/* Bits for R12A_TXAGC_MCS3_0(i). */
#define R12A_TXAGC_MCS0_M		0x000000ff
#define R12A_TXAGC_MCS0_S		0
#define R12A_TXAGC_MCS1_M		0x0000ff00
#define R12A_TXAGC_MCS1_S		8
#define R12A_TXAGC_MCS2_M		0x00ff0000
#define R12A_TXAGC_MCS2_S		16
#define R12A_TXAGC_MCS3_M		0xff000000
#define R12A_TXAGC_MCS3_S		24

/* Bits for R12A_TXAGC_MCS7_4(i). */
#define R12A_TXAGC_MCS4_M		0x000000ff
#define R12A_TXAGC_MCS4_S		0
#define R12A_TXAGC_MCS5_M		0x0000ff00
#define R12A_TXAGC_MCS5_S		8
#define R12A_TXAGC_MCS6_M		0x00ff0000
#define R12A_TXAGC_MCS6_S		16
#define R12A_TXAGC_MCS7_M		0xff000000
#define R12A_TXAGC_MCS7_S		24

/* Bits for R12A_TXAGC_MCS11_8(i). */
#define R12A_TXAGC_MCS8_M		0x000000ff
#define R12A_TXAGC_MCS8_S		0
#define R12A_TXAGC_MCS9_M		0x0000ff00
#define R12A_TXAGC_MCS9_S		8
#define R12A_TXAGC_MCS10_M		0x00ff0000
#define R12A_TXAGC_MCS10_S		16
#define R12A_TXAGC_MCS11_M		0xff000000
#define R12A_TXAGC_MCS11_S		24

/* Bits for R12A_TXAGC_MCS15_12(i). */
#define R12A_TXAGC_MCS12_M		0x000000ff
#define R12A_TXAGC_MCS12_S		0
#define R12A_TXAGC_MCS13_M		0x0000ff00
#define R12A_TXAGC_MCS13_S		8
#define R12A_TXAGC_MCS14_M		0x00ff0000
#define R12A_TXAGC_MCS14_S		16
#define R12A_TXAGC_MCS15_M		0xff000000
#define R12A_TXAGC_MCS15_S		24


/*
 * RF (6052) registers.
 */
#define R12A_RF_LCK		0xb4

/* Bits for R12A_RF_LCK. */
#define R12A_RF_LCK_MODE	0x4000

#endif	/* R12A_REG_H */
