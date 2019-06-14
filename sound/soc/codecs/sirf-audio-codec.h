/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * SiRF inner codec controllers define
 *
 * Copyright (c) 2011 Cambridge Silicon Radio Limited, a CSR plc group company.
 */

#ifndef _SIRF_AUDIO_CODEC_H
#define _SIRF_AUDIO_CODEC_H


#define AUDIO_IC_CODEC_PWR			(0x00E0)
#define AUDIO_IC_CODEC_CTRL0			(0x00E4)
#define AUDIO_IC_CODEC_CTRL1			(0x00E8)
#define AUDIO_IC_CODEC_CTRL2			(0x00EC)
#define AUDIO_IC_CODEC_CTRL3			(0x00F0)

#define MICBIASEN		(1 << 3)

#define IC_RDACEN		(1 << 0)
#define IC_LDACEN		(1 << 1)
#define IC_HSREN		(1 << 2)
#define IC_HSLEN		(1 << 3)
#define IC_SPEN			(1 << 4)
#define IC_CPEN			(1 << 5)

#define IC_HPRSELR		(1 << 6)
#define IC_HPLSELR		(1 << 7)
#define IC_HPRSELL		(1 << 8)
#define IC_HPLSELL		(1 << 9)
#define IC_SPSELR		(1 << 10)
#define IC_SPSELL		(1 << 11)

#define IC_MONOR		(1 << 12)
#define IC_MONOL		(1 << 13)

#define IC_RXOSRSEL		(1 << 28)
#define IC_CPFREQ		(1 << 29)
#define IC_HSINVEN		(1 << 30)

#define IC_MICINREN		(1 << 0)
#define IC_MICINLEN		(1 << 1)
#define IC_MICIN1SEL		(1 << 2)
#define IC_MICIN2SEL		(1 << 3)
#define IC_MICDIFSEL		(1 << 4)
#define	IC_LINEIN1SEL		(1 << 5)
#define	IC_LINEIN2SEL		(1 << 6)
#define	IC_RADCEN		(1 << 7)
#define	IC_LADCEN		(1 << 8)
#define	IC_ALM			(1 << 9)

#define IC_DIGMICEN             (1 << 22)
#define IC_DIGMICFREQ           (1 << 23)
#define IC_ADC14B_12            (1 << 24)
#define IC_FIRDAC_HSL_EN        (1 << 25)
#define IC_FIRDAC_HSR_EN        (1 << 26)
#define IC_FIRDAC_LOUT_EN       (1 << 27)
#define IC_POR                  (1 << 28)
#define IC_CODEC_CLK_EN         (1 << 29)
#define IC_HP_3DB_BOOST         (1 << 30)

#define IC_ADC_LEFT_GAIN_SHIFT	16
#define IC_ADC_RIGHT_GAIN_SHIFT 10
#define IC_ADC_GAIN_MASK	0x3F
#define IC_MIC_MAX_GAIN		0x39

#define IC_RXPGAR_MASK		0x3F
#define IC_RXPGAR_SHIFT		14
#define IC_RXPGAL_MASK		0x3F
#define IC_RXPGAL_SHIFT		21
#define IC_RXPGAR		0x7B
#define IC_RXPGAL		0x7B

#define AUDIO_PORT_TX_FIFO_LEVEL_CHECK_MASK     0x3F
#define AUDIO_PORT_TX_FIFO_SC_OFFSET    0
#define AUDIO_PORT_TX_FIFO_LC_OFFSET    10
#define AUDIO_PORT_TX_FIFO_HC_OFFSET    20

#define TX_FIFO_SC(x)           (((x) & AUDIO_PORT_TX_FIFO_LEVEL_CHECK_MASK) \
				<< AUDIO_PORT_TX_FIFO_SC_OFFSET)
#define TX_FIFO_LC(x)           (((x) & AUDIO_PORT_TX_FIFO_LEVEL_CHECK_MASK) \
				<< AUDIO_PORT_TX_FIFO_LC_OFFSET)
#define TX_FIFO_HC(x)           (((x) & AUDIO_PORT_TX_FIFO_LEVEL_CHECK_MASK) \
				<< AUDIO_PORT_TX_FIFO_HC_OFFSET)

#define AUDIO_PORT_RX_FIFO_LEVEL_CHECK_MASK     0x0F
#define AUDIO_PORT_RX_FIFO_SC_OFFSET    0
#define AUDIO_PORT_RX_FIFO_LC_OFFSET    10
#define AUDIO_PORT_RX_FIFO_HC_OFFSET    20

#define RX_FIFO_SC(x)           (((x) & AUDIO_PORT_RX_FIFO_LEVEL_CHECK_MASK) \
				<< AUDIO_PORT_RX_FIFO_SC_OFFSET)
#define RX_FIFO_LC(x)           (((x) & AUDIO_PORT_RX_FIFO_LEVEL_CHECK_MASK) \
				<< AUDIO_PORT_RX_FIFO_LC_OFFSET)
#define RX_FIFO_HC(x)           (((x) & AUDIO_PORT_RX_FIFO_LEVEL_CHECK_MASK) \
				<< AUDIO_PORT_RX_FIFO_HC_OFFSET)
#define AUDIO_PORT_IC_CODEC_TX_CTRL		(0x00F4)
#define AUDIO_PORT_IC_CODEC_RX_CTRL		(0x00F8)

#define AUDIO_PORT_IC_TXFIFO_OP			(0x00FC)
#define AUDIO_PORT_IC_TXFIFO_LEV_CHK		(0x0100)
#define AUDIO_PORT_IC_TXFIFO_STS		(0x0104)
#define AUDIO_PORT_IC_TXFIFO_INT		(0x0108)
#define AUDIO_PORT_IC_TXFIFO_INT_MSK		(0x010C)

#define AUDIO_PORT_IC_RXFIFO_OP			(0x0110)
#define AUDIO_PORT_IC_RXFIFO_LEV_CHK		(0x0114)
#define AUDIO_PORT_IC_RXFIFO_STS		(0x0118)
#define AUDIO_PORT_IC_RXFIFO_INT		(0x011C)
#define AUDIO_PORT_IC_RXFIFO_INT_MSK		(0x0120)

#define AUDIO_FIFO_START		(1 << 0)
#define AUDIO_FIFO_RESET		(1 << 1)

#define AUDIO_FIFO_FULL			(1 << 0)
#define AUDIO_FIFO_EMPTY		(1 << 1)
#define AUDIO_FIFO_OFLOW		(1 << 2)
#define AUDIO_FIFO_UFLOW		(1 << 3)

#define IC_TX_ENABLE		(0x03)
#define IC_RX_ENABLE_MONO	(0x01)
#define IC_RX_ENABLE_STEREO	(0x03)

#endif /*__SIRF_AUDIO_CODEC_H*/
