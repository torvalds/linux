/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2020 NXP
 */

#ifndef _FSL_AUD2HTX_H
#define _FSL_AUD2HTX_H

#define FSL_AUD2HTX_FORMATS (SNDRV_PCM_FMTBIT_S24_LE | \
			     SNDRV_PCM_FMTBIT_S32_LE)

/* AUD2HTX Register Map */
#define AUD2HTX_CTRL          0x0   /* AUD2HTX Control Register */
#define AUD2HTX_CTRL_EXT      0x4   /* AUD2HTX Control Extended Register */
#define AUD2HTX_WR            0x8   /* AUD2HTX Write Register */
#define AUD2HTX_STATUS        0xC   /* AUD2HTX Status Register */
#define AUD2HTX_IRQ_NOMASK    0x10  /* AUD2HTX Nonmasked Interrupt Flags Register */
#define AUD2HTX_IRQ_MASKED    0x14  /* AUD2HTX Masked Interrupt Flags Register */
#define AUD2HTX_IRQ_MASK      0x18  /* AUD2HTX IRQ Masks Register */

/* AUD2HTX Control Register */
#define AUD2HTX_CTRL_EN          BIT(0)

/* AUD2HTX Control Extended Register */
#define AUD2HTX_CTRE_DE          BIT(0)
#define AUD2HTX_CTRE_DT_SHIFT    0x1
#define AUD2HTX_CTRE_DT_WIDTH    0x2
#define AUD2HTX_CTRE_DT_MASK     ((BIT(AUD2HTX_CTRE_DT_WIDTH) - 1) \
				 << AUD2HTX_CTRE_DT_SHIFT)
#define AUD2HTX_CTRE_WL_SHIFT    16
#define AUD2HTX_CTRE_WL_WIDTH    5
#define AUD2HTX_CTRE_WL_MASK     ((BIT(AUD2HTX_CTRE_WL_WIDTH) - 1) \
				 << AUD2HTX_CTRE_WL_SHIFT)
#define AUD2HTX_CTRE_WH_SHIFT    24
#define AUD2HTX_CTRE_WH_WIDTH    5
#define AUD2HTX_CTRE_WH_MASK     ((BIT(AUD2HTX_CTRE_WH_WIDTH) - 1) \
				 << AUD2HTX_CTRE_WH_SHIFT)

/* AUD2HTX IRQ Masks Register */
#define AUD2HTX_WM_HIGH_IRQ_MASK BIT(2)
#define AUD2HTX_WM_LOW_IRQ_MASK  BIT(1)
#define AUD2HTX_OVF_MASK         BIT(0)

#define AUD2HTX_FIFO_DEPTH       0x20
#define AUD2HTX_WTMK_LOW         0x10
#define AUD2HTX_WTMK_HIGH        0x10
#define AUD2HTX_MAXBURST         0x10

/**
 * fsl_aud2htx: AUD2HTX private data
 *
 * @pdev: platform device pointer
 * @regmap: regmap handler
 * @bus_clk: clock source to access register
 * @dma_params_rx: DMA parameters for receive channel
 * @dma_params_tx: DMA parameters for transmit channel
 */
struct fsl_aud2htx {
	struct platform_device *pdev;
	struct regmap *regmap;
	struct clk *bus_clk;

	struct snd_dmaengine_dai_dma_data dma_params_rx;
	struct snd_dmaengine_dai_dma_data dma_params_tx;
};

#endif /* _FSL_AUD2HTX_H */
