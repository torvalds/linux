/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Author: Kevin Wells <kevin.wells@nxp.com>
 *
 * Copyright (C) 2008 NXP Semiconductors
 * Copyright 2023 Timesys Corporation <piotr.wojtaszczyk@timesys.com>
 */

#ifndef __SOUND_SOC_LPC3XXX_I2S_H
#define __SOUND_SOC_LPC3XXX_I2S_H

#include <linux/bitfield.h>
#include <linux/types.h>
#include <linux/regmap.h>

struct lpc3xxx_i2s_info {
	struct device *dev;
	struct clk *clk;
	struct mutex lock; /* To serialize user-space access */
	struct regmap *regs;
	u32 streams_in_use;
	u32 clkrate;
	int freq;
	struct snd_dmaengine_dai_dma_data playback_dma_config;
	struct snd_dmaengine_dai_dma_data capture_dma_config;
};

int lpc3xxx_pcm_register(struct platform_device *pdev);

/* I2S controller register offsets */
#define LPC3XXX_REG_I2S_DAO		0x00
#define LPC3XXX_REG_I2S_DAI		0x04
#define LPC3XXX_REG_I2S_TX_FIFO	0x08
#define LPC3XXX_REG_I2S_RX_FIFO	0x0C
#define LPC3XXX_REG_I2S_STAT	0x10
#define LPC3XXX_REG_I2S_DMA0	0x14
#define LPC3XXX_REG_I2S_DMA1	0x18
#define LPC3XXX_REG_I2S_IRQ		0x1C
#define LPC3XXX_REG_I2S_TX_RATE	0x20
#define LPC3XXX_REG_I2S_RX_RATE	0x24

/* i2s_daO i2s_dai register definitions */
#define LPC3XXX_I2S_WW8      FIELD_PREP(0x3, 0) /* Word width is 8bit */
#define LPC3XXX_I2S_WW16     FIELD_PREP(0x3, 1) /* Word width is 16bit */
#define LPC3XXX_I2S_WW32     FIELD_PREP(0x3, 3) /* Word width is 32bit */
#define LPC3XXX_I2S_MONO     BIT(2)   /* Mono */
#define LPC3XXX_I2S_STOP     BIT(3)   /* Stop, diables the access to FIFO, mutes the channel */
#define LPC3XXX_I2S_RESET    BIT(4)   /* Reset the channel */
#define LPC3XXX_I2S_WS_SEL   BIT(5)   /* Channel Master(0) or slave(1) mode select */
#define LPC3XXX_I2S_WS_HP(s) FIELD_PREP(0x7FC0, s) /* Word select half period - 1 */
#define LPC3XXX_I2S_MUTE     BIT(15)  /* Mute the channel, Transmit channel only */

#define LPC3XXX_I2S_WW32_HP  0x1f /* Word select half period for 32bit word width */
#define LPC3XXX_I2S_WW16_HP  0x0f /* Word select half period for 16bit word width */
#define LPC3XXX_I2S_WW8_HP   0x7  /* Word select half period for 8bit word width */

/* i2s_stat register definitions */
#define LPC3XXX_I2S_IRQ_STAT     BIT(0)
#define LPC3XXX_I2S_DMA0_REQ     BIT(1)
#define LPC3XXX_I2S_DMA1_REQ     BIT(2)

/* i2s_dma0 Configuration register definitions */
#define LPC3XXX_I2S_DMA0_RX_EN     BIT(0)       /* Enable RX DMA1 */
#define LPC3XXX_I2S_DMA0_TX_EN     BIT(1)       /* Enable TX DMA1 */
#define LPC3XXX_I2S_DMA0_RX_DEPTH(s) FIELD_PREP(0xF00, s)  /* Set the DMA1 RX Request level */
#define LPC3XXX_I2S_DMA0_TX_DEPTH(s) FIELD_PREP(0xF0000, s) /* Set the DMA1 TX Request level */

/* i2s_dma1 Configuration register definitions */
#define LPC3XXX_I2S_DMA1_RX_EN     BIT(0)       /* Enable RX DMA1 */
#define LPC3XXX_I2S_DMA1_TX_EN     BIT(1)       /* Enable TX DMA1 */
#define LPC3XXX_I2S_DMA1_RX_DEPTH(s) FIELD_PREP(0x700, s) /* Set the DMA1 RX Request level */
#define LPC3XXX_I2S_DMA1_TX_DEPTH(s) FIELD_PREP(0x70000, s) /* Set the DMA1 TX Request level */

/* i2s_irq register definitions */
#define LPC3XXX_I2S_RX_IRQ_EN     BIT(0)       /* Enable RX IRQ */
#define LPC3XXX_I2S_TX_IRQ_EN     BIT(1)       /* Enable TX IRQ */
#define LPC3XXX_I2S_IRQ_RX_DEPTH(s)  FIELD_PREP(0xFF00, s)  /* valid values ar 0 to 7 */
#define LPC3XXX_I2S_IRQ_TX_DEPTH(s)  FIELD_PREP(0xFF0000, s) /* valid values ar 0 to 7 */

#endif
