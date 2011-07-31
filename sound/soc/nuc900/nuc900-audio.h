/*
 * Copyright (c) 2010 Nuvoton technology corporation.
 *
 * Wan ZongShun <mcuos.com@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation;version 2 of the License.
 *
 */

#ifndef _NUC900_AUDIO_H
#define _NUC900_AUDIO_H

#include <linux/io.h>

/* Audio Control Registers */
#define ACTL_CON		0x00
#define ACTL_RESET		0x04
#define ACTL_RDSTB		0x08
#define ACTL_RDST_LENGTH	0x0C
#define ACTL_RDSTC		0x10
#define ACTL_RSR		0x14
#define ACTL_PDSTB		0x18
#define ACTL_PDST_LENGTH	0x1C
#define ACTL_PDSTC		0x20
#define ACTL_PSR		0x24
#define ACTL_IISCON		0x28
#define ACTL_ACCON		0x2C
#define ACTL_ACOS0		0x30
#define ACTL_ACOS1		0x34
#define ACTL_ACOS2		0x38
#define ACTL_ACIS0		0x3C
#define ACTL_ACIS1		0x40
#define ACTL_ACIS2		0x44
#define ACTL_COUNTER		0x48

/* bit definition of REG_ACTL_CON register */
#define R_DMA_IRQ		0x1000
#define T_DMA_IRQ		0x0800
#define IIS_AC_PIN_SEL		0x0100
#define FIFO_TH			0x0080
#define ADC_EN			0x0010
#define M80_EN			0x0008
#define ACLINK_EN		0x0004
#define IIS_EN			0x0002

/* bit definition of REG_ACTL_RESET register */
#define W5691_PLAY		0x20000
#define ACTL_RESET_BIT		0x10000
#define RECORD_RIGHT_CHNNEL	0x08000
#define RECORD_LEFT_CHNNEL	0x04000
#define PLAY_RIGHT_CHNNEL	0x02000
#define PLAY_LEFT_CHNNEL	0x01000
#define DAC_PLAY		0x00800
#define ADC_RECORD		0x00400
#define M80_PLAY		0x00200
#define AC_RECORD		0x00100
#define AC_PLAY			0x00080
#define IIS_RECORD		0x00040
#define IIS_PLAY		0x00020
#define DAC_RESET		0x00010
#define ADC_RESET		0x00008
#define M80_RESET		0x00004
#define AC_RESET		0x00002
#define IIS_RESET		0x00001

/* bit definition of REG_ACTL_ACCON register */
#define AC_BCLK_PU_EN		0x20
#define AC_R_FINISH		0x10
#define AC_W_FINISH		0x08
#define AC_W_RES		0x04
#define AC_C_RES		0x02

/* bit definition of ACTL_RSR register */
#define R_FIFO_EMPTY		0x04
#define R_DMA_END_IRQ		0x02
#define R_DMA_MIDDLE_IRQ	0x01

/* bit definition of ACTL_PSR register */
#define P_FIFO_EMPTY		0x04
#define P_DMA_END_IRQ		0x02
#define P_DMA_MIDDLE_IRQ	0x01

/* bit definition of ACTL_ACOS0 register */
#define SLOT1_VALID		0x01
#define SLOT2_VALID		0x02
#define SLOT3_VALID		0x04
#define SLOT4_VALID		0x08
#define VALID_FRAME		0x10

/* bit definition of ACTL_ACOS1 register */
#define R_WB			0x80

#define CODEC_READY		0x10
#define RESET_PRSR		0x00
#define AUDIO_WRITE(addr, val)	__raw_writel(val, addr)
#define AUDIO_READ(addr)	__raw_readl(addr)

struct nuc900_audio {
	void __iomem *mmio;
	spinlock_t lock;
	dma_addr_t dma_addr[2];
	unsigned long buffersize[2];
	unsigned long irq_num;
	struct snd_pcm_substream *substream;
	struct resource *res;
	struct clk *clk;
	struct device *dev;

};

extern struct nuc900_audio *nuc900_ac97_data;
extern struct snd_soc_dai nuc900_ac97_dai;
extern struct snd_soc_platform nuc900_soc_platform;

#endif /*end _NUC900_AUDIO_H */
