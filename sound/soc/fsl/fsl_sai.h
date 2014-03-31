/*
 * Copyright 2012-2013 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __FSL_SAI_H
#define __FSL_SAI_H

#include <sound/dmaengine_pcm.h>

#define FSL_SAI_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S20_3LE |\
			 SNDRV_PCM_FMTBIT_S24_LE)

/* SAI Transmit/Recieve Control Register */
#define FSL_SAI_TCSR		0x00
#define FSL_SAI_RCSR		0x80
#define FSL_SAI_CSR_TERE	BIT(31)
#define FSL_SAI_CSR_FWF		BIT(17)
#define FSL_SAI_CSR_FRIE	BIT(8)
#define FSL_SAI_CSR_FRDE	BIT(0)

/* SAI Transmit Data/FIFO/MASK Register */
#define FSL_SAI_TDR		0x20
#define FSL_SAI_TFR		0x40
#define FSL_SAI_TMR		0x60

/* SAI Recieve Data/FIFO/MASK Register */
#define FSL_SAI_RDR		0xa0
#define FSL_SAI_RFR		0xc0
#define FSL_SAI_RMR		0xe0

/* SAI Transmit and Recieve Configuration 1 Register */
#define FSL_SAI_TCR1		0x04
#define FSL_SAI_RCR1		0x84

/* SAI Transmit and Recieve Configuration 2 Register */
#define FSL_SAI_TCR2		0x08
#define FSL_SAI_RCR2		0x88
#define FSL_SAI_CR2_SYNC	BIT(30)
#define FSL_SAI_CR2_MSEL_MASK	(0xff << 26)
#define FSL_SAI_CR2_MSEL_BUS	0
#define FSL_SAI_CR2_MSEL_MCLK1	BIT(26)
#define FSL_SAI_CR2_MSEL_MCLK2	BIT(27)
#define FSL_SAI_CR2_MSEL_MCLK3	(BIT(26) | BIT(27))
#define FSL_SAI_CR2_BCP		BIT(25)
#define FSL_SAI_CR2_BCD_MSTR	BIT(24)

/* SAI Transmit and Recieve Configuration 3 Register */
#define FSL_SAI_TCR3		0x0c
#define FSL_SAI_RCR3		0x8c
#define FSL_SAI_CR3_TRCE	BIT(16)
#define FSL_SAI_CR3_WDFL(x)	(x)
#define FSL_SAI_CR3_WDFL_MASK	0x1f

/* SAI Transmit and Recieve Configuration 4 Register */
#define FSL_SAI_TCR4		0x10
#define FSL_SAI_RCR4		0x90
#define FSL_SAI_CR4_FRSZ(x)	(((x) - 1) << 16)
#define FSL_SAI_CR4_FRSZ_MASK	(0x1f << 16)
#define FSL_SAI_CR4_SYWD(x)	(((x) - 1) << 8)
#define FSL_SAI_CR4_SYWD_MASK	(0x1f << 8)
#define FSL_SAI_CR4_MF		BIT(4)
#define FSL_SAI_CR4_FSE		BIT(3)
#define FSL_SAI_CR4_FSP		BIT(1)
#define FSL_SAI_CR4_FSD_MSTR	BIT(0)

/* SAI Transmit and Recieve Configuration 5 Register */
#define FSL_SAI_TCR5		0x14
#define FSL_SAI_RCR5		0x94
#define FSL_SAI_CR5_WNW(x)	(((x) - 1) << 24)
#define FSL_SAI_CR5_WNW_MASK	(0x1f << 24)
#define FSL_SAI_CR5_W0W(x)	(((x) - 1) << 16)
#define FSL_SAI_CR5_W0W_MASK	(0x1f << 16)
#define FSL_SAI_CR5_FBT(x)	((x) << 8)
#define FSL_SAI_CR5_FBT_MASK	(0x1f << 8)

/* SAI type */
#define FSL_SAI_DMA		BIT(0)
#define FSL_SAI_USE_AC97	BIT(1)
#define FSL_SAI_NET		BIT(2)
#define FSL_SAI_TRA_SYN		BIT(3)
#define FSL_SAI_REC_SYN		BIT(4)
#define FSL_SAI_USE_I2S_SLAVE	BIT(5)

#define FSL_FMT_TRANSMITTER	0
#define FSL_FMT_RECEIVER	1

/* SAI clock sources */
#define FSL_SAI_CLK_BUS		0
#define FSL_SAI_CLK_MAST1	1
#define FSL_SAI_CLK_MAST2	2
#define FSL_SAI_CLK_MAST3	3

/* SAI data transfer numbers per DMA request */
#define FSL_SAI_MAXBURST_TX 6
#define FSL_SAI_MAXBURST_RX 6

struct fsl_sai {
	struct clk *clk;

	void __iomem *base;

	bool big_endian_regs;
	bool big_endian_data;

	struct snd_dmaengine_dai_dma_data dma_params_rx;
	struct snd_dmaengine_dai_dma_data dma_params_tx;
};

#endif /* __FSL_SAI_H */
