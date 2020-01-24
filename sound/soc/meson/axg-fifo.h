/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Copyright (c) 2018 BayLibre, SAS.
 * Author: Jerome Brunet <jbrunet@baylibre.com>
 */

#ifndef _MESON_AXG_FIFO_H
#define _MESON_AXG_FIFO_H

struct clk;
struct platform_device;
struct regmap;
struct reset_control;

struct snd_soc_component_driver;
struct snd_soc_dai;
struct snd_soc_dai_driver;

struct snd_soc_pcm_runtime;

#define AXG_FIFO_CH_MAX			128
#define AXG_FIFO_RATES			(SNDRV_PCM_RATE_5512 |		\
					 SNDRV_PCM_RATE_8000_192000)
#define AXG_FIFO_FORMATS		(SNDRV_PCM_FMTBIT_S8 |		\
					 SNDRV_PCM_FMTBIT_S16_LE |	\
					 SNDRV_PCM_FMTBIT_S20_LE |	\
					 SNDRV_PCM_FMTBIT_S24_LE |	\
					 SNDRV_PCM_FMTBIT_S32_LE |	\
					 SNDRV_PCM_FMTBIT_IEC958_SUBFRAME_LE)

#define AXG_FIFO_BURST			8
#define AXG_FIFO_MIN_CNT		64
#define AXG_FIFO_MIN_DEPTH		(AXG_FIFO_BURST * AXG_FIFO_MIN_CNT)

#define FIFO_INT_ADDR_FINISH		BIT(0)
#define FIFO_INT_ADDR_INT		BIT(1)
#define FIFO_INT_COUNT_REPEAT		BIT(2)
#define FIFO_INT_COUNT_ONCE		BIT(3)
#define FIFO_INT_FIFO_ZERO		BIT(4)
#define FIFO_INT_FIFO_DEPTH		BIT(5)
#define FIFO_INT_MASK			GENMASK(7, 0)

#define FIFO_CTRL0			0x00
#define  CTRL0_DMA_EN			BIT(31)
#define  CTRL0_INT_EN(x)		((x) << 16)
#define  CTRL0_SEL_MASK			GENMASK(2, 0)
#define  CTRL0_SEL_SHIFT		0
#define FIFO_CTRL1			0x04
#define  CTRL1_INT_CLR(x)		((x) << 0)
#define  CTRL1_STATUS2_SEL_MASK		GENMASK(11, 8)
#define  CTRL1_STATUS2_SEL(x)		((x) << 8)
#define   STATUS2_SEL_DDR_READ		0
#define  CTRL1_THRESHOLD_MASK		GENMASK(23, 16)
#define  CTRL1_THRESHOLD(x)		((x) << 16)
#define  CTRL1_FRDDR_DEPTH_MASK		GENMASK(31, 24)
#define  CTRL1_FRDDR_DEPTH(x)		((x) << 24)
#define FIFO_START_ADDR			0x08
#define FIFO_FINISH_ADDR		0x0c
#define FIFO_INT_ADDR			0x10
#define FIFO_STATUS1			0x14
#define  STATUS1_INT_STS(x)		((x) << 0)
#define FIFO_STATUS2			0x18
#define FIFO_INIT_ADDR			0x24
#define FIFO_CTRL2			0x28

struct axg_fifo {
	struct regmap *map;
	struct clk *pclk;
	struct reset_control *arb;
	int irq;
};

struct axg_fifo_match_data {
	const struct snd_soc_component_driver *component_drv;
	struct snd_soc_dai_driver *dai_drv;
};

int axg_fifo_pcm_open(struct snd_soc_component *component,
		      struct snd_pcm_substream *ss);
int axg_fifo_pcm_close(struct snd_soc_component *component,
		       struct snd_pcm_substream *ss);
int axg_fifo_pcm_hw_params(struct snd_soc_component *component,
			   struct snd_pcm_substream *ss,
			   struct snd_pcm_hw_params *params);
int g12a_fifo_pcm_hw_params(struct snd_soc_component *component,
			    struct snd_pcm_substream *ss,
			    struct snd_pcm_hw_params *params);
int axg_fifo_pcm_hw_free(struct snd_soc_component *component,
			 struct snd_pcm_substream *ss);
snd_pcm_uframes_t axg_fifo_pcm_pointer(struct snd_soc_component *component,
				       struct snd_pcm_substream *ss);
int axg_fifo_pcm_trigger(struct snd_soc_component *component,
			 struct snd_pcm_substream *ss, int cmd);

int axg_fifo_pcm_new(struct snd_soc_pcm_runtime *rtd, unsigned int type);
int axg_fifo_probe(struct platform_device *pdev);

#endif /* _MESON_AXG_FIFO_H */
