/*
 * Mediatek ALSA SoC AFE platform driver
 *
 * Copyright (c) 2015 MediaTek Inc.
 * Author: Koro Chen <koro.chen@mediatek.com>
 *             Sascha Hauer <s.hauer@pengutronix.de>
 *             Hidalgo Huang <hidalgo.huang@mediatek.com>
 *             Ir Lian <ir.lian@mediatek.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include "mtk-afe-common.h"

/*****************************************************************************
 *                  R E G I S T E R       D E F I N I T I O N
 *****************************************************************************/
#define AUDIO_TOP_CON0		0x0000
#define AUDIO_TOP_CON1		0x0004
#define AFE_DAC_CON0		0x0010
#define AFE_DAC_CON1		0x0014
#define AFE_I2S_CON1		0x0034
#define AFE_I2S_CON2		0x0038
#define AFE_CONN_24BIT		0x006c
#define AFE_MEMIF_MSB		0x00cc

#define AFE_CONN1		0x0024
#define AFE_CONN2		0x0028
#define AFE_CONN3		0x002c
#define AFE_CONN7		0x0460
#define AFE_CONN8		0x0464
#define AFE_HDMI_CONN0		0x0390

/* Memory interface */
#define AFE_DL1_BASE		0x0040
#define AFE_DL1_CUR		0x0044
#define AFE_DL1_END		0x0048
#define AFE_DL2_BASE		0x0050
#define AFE_DL2_CUR		0x0054
#define AFE_AWB_BASE		0x0070
#define AFE_AWB_CUR		0x007c
#define AFE_VUL_BASE		0x0080
#define AFE_VUL_CUR		0x008c
#define AFE_VUL_END		0x0088
#define AFE_DAI_BASE		0x0090
#define AFE_DAI_CUR		0x009c
#define AFE_MOD_PCM_BASE	0x0330
#define AFE_MOD_PCM_CUR		0x033c
#define AFE_HDMI_OUT_BASE	0x0374
#define AFE_HDMI_OUT_CUR	0x0378
#define AFE_HDMI_OUT_END	0x037c

#define AFE_ADDA_TOP_CON0	0x0120
#define AFE_ADDA2_TOP_CON0	0x0600

#define AFE_HDMI_OUT_CON0	0x0370

#define AFE_IRQ_MCU_CON		0x03a0
#define AFE_IRQ_STATUS		0x03a4
#define AFE_IRQ_CLR		0x03a8
#define AFE_IRQ_CNT1		0x03ac
#define AFE_IRQ_CNT2		0x03b0
#define AFE_IRQ_MCU_EN		0x03b4
#define AFE_IRQ_CNT5		0x03bc
#define AFE_IRQ_CNT7		0x03dc

#define AFE_TDM_CON1		0x0548
#define AFE_TDM_CON2		0x054c

#define AFE_BASE_END_OFFSET	8
#define AFE_IRQ_STATUS_BITS	0xff

/* AUDIO_TOP_CON0 (0x0000) */
#define AUD_TCON0_PDN_SPDF		(0x1 << 21)
#define AUD_TCON0_PDN_HDMI		(0x1 << 20)
#define AUD_TCON0_PDN_24M		(0x1 << 9)
#define AUD_TCON0_PDN_22M		(0x1 << 8)
#define AUD_TCON0_PDN_AFE		(0x1 << 2)

/* AFE_I2S_CON1 (0x0034) */
#define AFE_I2S_CON1_LOW_JITTER_CLK	(0x1 << 12)
#define AFE_I2S_CON1_RATE(x)		(((x) & 0xf) << 8)
#define AFE_I2S_CON1_FORMAT_I2S		(0x1 << 3)
#define AFE_I2S_CON1_EN			(0x1 << 0)

/* AFE_I2S_CON2 (0x0038) */
#define AFE_I2S_CON2_LOW_JITTER_CLK	(0x1 << 12)
#define AFE_I2S_CON2_RATE(x)		(((x) & 0xf) << 8)
#define AFE_I2S_CON2_FORMAT_I2S		(0x1 << 3)
#define AFE_I2S_CON2_EN			(0x1 << 0)

/* AFE_CONN_24BIT (0x006c) */
#define AFE_CONN_24BIT_O04		(0x1 << 4)
#define AFE_CONN_24BIT_O03		(0x1 << 3)

/* AFE_HDMI_CONN0 (0x0390) */
#define AFE_HDMI_CONN0_O37_I37		(0x7 << 21)
#define AFE_HDMI_CONN0_O36_I36		(0x6 << 18)
#define AFE_HDMI_CONN0_O35_I33		(0x3 << 15)
#define AFE_HDMI_CONN0_O34_I32		(0x2 << 12)
#define AFE_HDMI_CONN0_O33_I35		(0x5 << 9)
#define AFE_HDMI_CONN0_O32_I34		(0x4 << 6)
#define AFE_HDMI_CONN0_O31_I31		(0x1 << 3)
#define AFE_HDMI_CONN0_O30_I30		(0x0 << 0)

/* AFE_TDM_CON1 (0x0548) */
#define AFE_TDM_CON1_LRCK_WIDTH(x)	(((x) - 1) << 24)
#define AFE_TDM_CON1_32_BCK_CYCLES	(0x2 << 12)
#define AFE_TDM_CON1_WLEN_32BIT		(0x2 << 8)
#define AFE_TDM_CON1_MSB_ALIGNED	(0x1 << 4)
#define AFE_TDM_CON1_1_BCK_DELAY	(0x1 << 3)
#define AFE_TDM_CON1_BCK_INV		(0x1 << 1)
#define AFE_TDM_CON1_EN			(0x1 << 0)

enum afe_tdm_ch_start {
	AFE_TDM_CH_START_O30_O31 = 0,
	AFE_TDM_CH_START_O32_O33,
	AFE_TDM_CH_START_O34_O35,
	AFE_TDM_CH_START_O36_O37,
	AFE_TDM_CH_ZERO,
};

static const unsigned int mtk_afe_backup_list[] = {
	AUDIO_TOP_CON0,
	AFE_CONN1,
	AFE_CONN2,
	AFE_CONN7,
	AFE_CONN8,
	AFE_DAC_CON1,
	AFE_DL1_BASE,
	AFE_DL1_END,
	AFE_VUL_BASE,
	AFE_VUL_END,
	AFE_HDMI_OUT_BASE,
	AFE_HDMI_OUT_END,
	AFE_HDMI_CONN0,
	AFE_DAC_CON0,
};

struct mtk_afe {
	/* address for ioremap audio hardware register */
	void __iomem *base_addr;
	struct device *dev;
	struct regmap *regmap;
	struct mtk_afe_memif memif[MTK_AFE_MEMIF_NUM];
	struct clk *clocks[MTK_CLK_NUM];
	unsigned int backup_regs[ARRAY_SIZE(mtk_afe_backup_list)];
	bool suspended;
};

static const struct snd_pcm_hardware mtk_afe_hardware = {
	.info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_MMAP_VALID),
	.buffer_bytes_max = 256 * 1024,
	.period_bytes_min = 512,
	.period_bytes_max = 128 * 1024,
	.periods_min = 2,
	.periods_max = 256,
	.fifo_size = 0,
};

static snd_pcm_uframes_t mtk_afe_pcm_pointer
			 (struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);
	struct mtk_afe_memif *memif = &afe->memif[rtd->cpu_dai->id];
	unsigned int hw_ptr;
	int ret;

	ret = regmap_read(afe->regmap, memif->data->reg_ofs_cur, &hw_ptr);
	if (ret || hw_ptr == 0) {
		dev_err(afe->dev, "%s hw_ptr err\n", __func__);
		hw_ptr = memif->phys_buf_addr;
	}

	return bytes_to_frames(substream->runtime,
			       hw_ptr - memif->phys_buf_addr);
}

static const struct snd_pcm_ops mtk_afe_pcm_ops = {
	.ioctl = snd_pcm_lib_ioctl,
	.pointer = mtk_afe_pcm_pointer,
};

static int mtk_afe_pcm_new(struct snd_soc_pcm_runtime *rtd)
{
	size_t size;
	struct snd_card *card = rtd->card->snd_card;
	struct snd_pcm *pcm = rtd->pcm;

	size = mtk_afe_hardware.buffer_bytes_max;

	return snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
						     card->dev, size, size);
}

static void mtk_afe_pcm_free(struct snd_pcm *pcm)
{
	snd_pcm_lib_preallocate_free_for_all(pcm);
}

static const struct snd_soc_platform_driver mtk_afe_pcm_platform = {
	.ops = &mtk_afe_pcm_ops,
	.pcm_new = mtk_afe_pcm_new,
	.pcm_free = mtk_afe_pcm_free,
};

struct mtk_afe_rate {
	unsigned int rate;
	unsigned int regvalue;
};

static const struct mtk_afe_rate mtk_afe_i2s_rates[] = {
	{ .rate = 8000, .regvalue = 0 },
	{ .rate = 11025, .regvalue = 1 },
	{ .rate = 12000, .regvalue = 2 },
	{ .rate = 16000, .regvalue = 4 },
	{ .rate = 22050, .regvalue = 5 },
	{ .rate = 24000, .regvalue = 6 },
	{ .rate = 32000, .regvalue = 8 },
	{ .rate = 44100, .regvalue = 9 },
	{ .rate = 48000, .regvalue = 10 },
	{ .rate = 88000, .regvalue = 11 },
	{ .rate = 96000, .regvalue = 12 },
	{ .rate = 174000, .regvalue = 13 },
	{ .rate = 192000, .regvalue = 14 },
};

static int mtk_afe_i2s_fs(unsigned int sample_rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mtk_afe_i2s_rates); i++)
		if (mtk_afe_i2s_rates[i].rate == sample_rate)
			return mtk_afe_i2s_rates[i].regvalue;

	return -EINVAL;
}

static int mtk_afe_set_i2s(struct mtk_afe *afe, unsigned int rate)
{
	unsigned int val;
	int fs = mtk_afe_i2s_fs(rate);

	if (fs < 0)
		return -EINVAL;

	/* from external ADC */
	regmap_update_bits(afe->regmap, AFE_ADDA_TOP_CON0, 0x1, 0x1);
	regmap_update_bits(afe->regmap, AFE_ADDA2_TOP_CON0, 0x1, 0x1);

	/* set input */
	val = AFE_I2S_CON2_LOW_JITTER_CLK |
	      AFE_I2S_CON2_RATE(fs) |
	      AFE_I2S_CON2_FORMAT_I2S;

	regmap_update_bits(afe->regmap, AFE_I2S_CON2, ~AFE_I2S_CON2_EN, val);

	/* set output */
	val = AFE_I2S_CON1_LOW_JITTER_CLK |
	      AFE_I2S_CON1_RATE(fs) |
	      AFE_I2S_CON1_FORMAT_I2S;

	regmap_update_bits(afe->regmap, AFE_I2S_CON1, ~AFE_I2S_CON1_EN, val);
	return 0;
}

static void mtk_afe_set_i2s_enable(struct mtk_afe *afe, bool enable)
{
	unsigned int val;

	regmap_read(afe->regmap, AFE_I2S_CON2, &val);
	if (!!(val & AFE_I2S_CON2_EN) == enable)
		return;

	/* input */
	regmap_update_bits(afe->regmap, AFE_I2S_CON2, 0x1, enable);

	/* output */
	regmap_update_bits(afe->regmap, AFE_I2S_CON1, 0x1, enable);
}

static int mtk_afe_dais_enable_clks(struct mtk_afe *afe,
				    struct clk *m_ck, struct clk *b_ck)
{
	int ret;

	if (m_ck) {
		ret = clk_prepare_enable(m_ck);
		if (ret) {
			dev_err(afe->dev, "Failed to enable m_ck\n");
			return ret;
		}
	}

	if (b_ck) {
		ret = clk_prepare_enable(b_ck);
		if (ret) {
			dev_err(afe->dev, "Failed to enable b_ck\n");
			return ret;
		}
	}
	return 0;
}

static int mtk_afe_dais_set_clks(struct mtk_afe *afe,
				 struct clk *m_ck, unsigned int mck_rate,
				 struct clk *b_ck, unsigned int bck_rate)
{
	int ret;

	if (m_ck) {
		ret = clk_set_rate(m_ck, mck_rate);
		if (ret) {
			dev_err(afe->dev, "Failed to set m_ck rate\n");
			return ret;
		}
	}

	if (b_ck) {
		ret = clk_set_rate(b_ck, bck_rate);
		if (ret) {
			dev_err(afe->dev, "Failed to set b_ck rate\n");
			return ret;
		}
	}
	return 0;
}

static void mtk_afe_dais_disable_clks(struct mtk_afe *afe,
				      struct clk *m_ck, struct clk *b_ck)
{
	if (m_ck)
		clk_disable_unprepare(m_ck);
	if (b_ck)
		clk_disable_unprepare(b_ck);
}

static int mtk_afe_i2s_startup(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);

	if (dai->active)
		return 0;

	mtk_afe_dais_enable_clks(afe, afe->clocks[MTK_CLK_I2S1_M], NULL);
	mtk_afe_dais_enable_clks(afe, afe->clocks[MTK_CLK_I2S2_M], NULL);
	regmap_update_bits(afe->regmap, AUDIO_TOP_CON0,
			   AUD_TCON0_PDN_22M | AUD_TCON0_PDN_24M, 0);
	return 0;
}

static void mtk_afe_i2s_shutdown(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);

	if (dai->active)
		return;

	mtk_afe_set_i2s_enable(afe, false);
	regmap_update_bits(afe->regmap, AUDIO_TOP_CON0,
			   AUD_TCON0_PDN_22M | AUD_TCON0_PDN_24M,
			   AUD_TCON0_PDN_22M | AUD_TCON0_PDN_24M);
	mtk_afe_dais_disable_clks(afe, afe->clocks[MTK_CLK_I2S1_M], NULL);
	mtk_afe_dais_disable_clks(afe, afe->clocks[MTK_CLK_I2S2_M], NULL);
}

static int mtk_afe_i2s_prepare(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime * const runtime = substream->runtime;
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);
	int ret;

	mtk_afe_dais_set_clks(afe,
			      afe->clocks[MTK_CLK_I2S1_M], runtime->rate * 256,
			      NULL, 0);
	mtk_afe_dais_set_clks(afe,
			      afe->clocks[MTK_CLK_I2S2_M], runtime->rate * 256,
			      NULL, 0);
	/* config I2S */
	ret = mtk_afe_set_i2s(afe, substream->runtime->rate);
	if (ret)
		return ret;

	mtk_afe_set_i2s_enable(afe, true);

	return 0;
}

static int mtk_afe_hdmi_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);

	if (dai->active)
		return 0;

	mtk_afe_dais_enable_clks(afe, afe->clocks[MTK_CLK_I2S3_M],
				 afe->clocks[MTK_CLK_I2S3_B]);
	return 0;
}

static void mtk_afe_hdmi_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);

	if (dai->active)
		return;

	mtk_afe_dais_disable_clks(afe, afe->clocks[MTK_CLK_I2S3_M],
				  afe->clocks[MTK_CLK_I2S3_B]);
}

static int mtk_afe_hdmi_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime * const runtime = substream->runtime;
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);
	unsigned int val;

	mtk_afe_dais_set_clks(afe,
			      afe->clocks[MTK_CLK_I2S3_M], runtime->rate * 128,
			      afe->clocks[MTK_CLK_I2S3_B],
			      runtime->rate * runtime->channels * 32);

	val = AFE_TDM_CON1_BCK_INV |
	      AFE_TDM_CON1_1_BCK_DELAY |
	      AFE_TDM_CON1_MSB_ALIGNED | /* I2S mode */
	      AFE_TDM_CON1_WLEN_32BIT |
	      AFE_TDM_CON1_32_BCK_CYCLES |
	      AFE_TDM_CON1_LRCK_WIDTH(32);
	regmap_update_bits(afe->regmap, AFE_TDM_CON1, ~AFE_TDM_CON1_EN, val);

	/* set tdm2 config */
	switch (runtime->channels) {
	case 1:
	case 2:
		val = AFE_TDM_CH_START_O30_O31;
		val |= (AFE_TDM_CH_ZERO << 4);
		val |= (AFE_TDM_CH_ZERO << 8);
		val |= (AFE_TDM_CH_ZERO << 12);
		break;
	case 3:
	case 4:
		val = AFE_TDM_CH_START_O30_O31;
		val |= (AFE_TDM_CH_START_O32_O33 << 4);
		val |= (AFE_TDM_CH_ZERO << 8);
		val |= (AFE_TDM_CH_ZERO << 12);
		break;
	case 5:
	case 6:
		val = AFE_TDM_CH_START_O30_O31;
		val |= (AFE_TDM_CH_START_O32_O33 << 4);
		val |= (AFE_TDM_CH_START_O34_O35 << 8);
		val |= (AFE_TDM_CH_ZERO << 12);
		break;
	case 7:
	case 8:
		val = AFE_TDM_CH_START_O30_O31;
		val |= (AFE_TDM_CH_START_O32_O33 << 4);
		val |= (AFE_TDM_CH_START_O34_O35 << 8);
		val |= (AFE_TDM_CH_START_O36_O37 << 12);
		break;
	default:
		val = 0;
	}
	regmap_update_bits(afe->regmap, AFE_TDM_CON2, 0x0000ffff, val);

	regmap_update_bits(afe->regmap, AFE_HDMI_OUT_CON0,
			   0x000000f0, runtime->channels << 4);
	return 0;
}

static int mtk_afe_hdmi_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);

	dev_info(afe->dev, "%s cmd=%d %s\n", __func__, cmd, dai->name);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		regmap_update_bits(afe->regmap, AUDIO_TOP_CON0,
				   AUD_TCON0_PDN_HDMI | AUD_TCON0_PDN_SPDF, 0);

		/* set connections:  O30~O37: L/R/LS/RS/C/LFE/CH7/CH8 */
		regmap_write(afe->regmap, AFE_HDMI_CONN0,
			     AFE_HDMI_CONN0_O30_I30 | AFE_HDMI_CONN0_O31_I31 |
			     AFE_HDMI_CONN0_O32_I34 | AFE_HDMI_CONN0_O33_I35 |
			     AFE_HDMI_CONN0_O34_I32 | AFE_HDMI_CONN0_O35_I33 |
			     AFE_HDMI_CONN0_O36_I36 | AFE_HDMI_CONN0_O37_I37);

		/* enable Out control */
		regmap_update_bits(afe->regmap, AFE_HDMI_OUT_CON0, 0x1, 0x1);

		/* enable tdm */
		regmap_update_bits(afe->regmap, AFE_TDM_CON1, 0x1, 0x1);

		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		/* disable tdm */
		regmap_update_bits(afe->regmap, AFE_TDM_CON1, 0x1, 0);

		/* disable Out control */
		regmap_update_bits(afe->regmap, AFE_HDMI_OUT_CON0, 0x1, 0);

		regmap_update_bits(afe->regmap, AUDIO_TOP_CON0,
				   AUD_TCON0_PDN_HDMI | AUD_TCON0_PDN_SPDF,
				   AUD_TCON0_PDN_HDMI | AUD_TCON0_PDN_SPDF);

		return 0;
	default:
		return -EINVAL;
	}
}

static int mtk_afe_dais_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mtk_afe_memif *memif = &afe->memif[rtd->cpu_dai->id];
	int ret;

	memif->substream = substream;

	snd_soc_set_runtime_hwparams(substream, &mtk_afe_hardware);

	/*
	 * Capture cannot use ping-pong buffer since hw_ptr at IRQ may be
	 * smaller than period_size due to AFE's internal buffer.
	 * This easily leads to overrun when avail_min is period_size.
	 * One more period can hold the possible unread buffer.
	 */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE) {
		ret = snd_pcm_hw_constraint_minmax(runtime,
						   SNDRV_PCM_HW_PARAM_PERIODS,
						   3,
						   mtk_afe_hardware.periods_max);
		if (ret < 0) {
			dev_err(afe->dev, "hw_constraint_minmax failed\n");
			return ret;
		}
	}
	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		dev_err(afe->dev, "snd_pcm_hw_constraint_integer failed\n");
	return ret;
}

static void mtk_afe_dais_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);
	struct mtk_afe_memif *memif = &afe->memif[rtd->cpu_dai->id];

	memif->substream = NULL;
}

static int mtk_afe_dais_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);
	struct mtk_afe_memif *memif = &afe->memif[rtd->cpu_dai->id];
	int msb_at_bit33 = 0;
	int ret;

	dev_dbg(afe->dev,
		"%s period = %u, rate= %u, channels=%u\n",
		__func__, params_period_size(params), params_rate(params),
		params_channels(params));

	ret = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
	if (ret < 0)
		return ret;

	msb_at_bit33 = upper_32_bits(substream->runtime->dma_addr) ? 1 : 0;
	memif->phys_buf_addr = lower_32_bits(substream->runtime->dma_addr);
	memif->buffer_size = substream->runtime->dma_bytes;

	/* start */
	regmap_write(afe->regmap,
		     memif->data->reg_ofs_base, memif->phys_buf_addr);
	/* end */
	regmap_write(afe->regmap,
		     memif->data->reg_ofs_base + AFE_BASE_END_OFFSET,
		     memif->phys_buf_addr + memif->buffer_size - 1);

	/* set MSB to 33-bit */
	regmap_update_bits(afe->regmap, AFE_MEMIF_MSB,
			   1 << memif->data->msb_shift,
			   msb_at_bit33 << memif->data->msb_shift);

	/* set channel */
	if (memif->data->mono_shift >= 0) {
		unsigned int mono = (params_channels(params) == 1) ? 1 : 0;

		regmap_update_bits(afe->regmap, AFE_DAC_CON1,
				   1 << memif->data->mono_shift,
				   mono << memif->data->mono_shift);
	}

	/* set rate */
	if (memif->data->fs_shift < 0)
		return 0;
	if (memif->data->id == MTK_AFE_MEMIF_DAI ||
	    memif->data->id == MTK_AFE_MEMIF_MOD_DAI) {
		unsigned int val;

		switch (params_rate(params)) {
		case 8000:
			val = 0;
			break;
		case 16000:
			val = 1;
			break;
		case 32000:
			val = 2;
			break;
		default:
			return -EINVAL;
		}

		if (memif->data->id == MTK_AFE_MEMIF_DAI)
			regmap_update_bits(afe->regmap, AFE_DAC_CON0,
					   0x3 << memif->data->fs_shift,
					   val << memif->data->fs_shift);
		else
			regmap_update_bits(afe->regmap, AFE_DAC_CON1,
					   0x3 << memif->data->fs_shift,
					   val << memif->data->fs_shift);

	} else {
		int fs = mtk_afe_i2s_fs(params_rate(params));

		if (fs < 0)
			return -EINVAL;

		regmap_update_bits(afe->regmap, AFE_DAC_CON1,
				   0xf << memif->data->fs_shift,
				   fs << memif->data->fs_shift);
	}

	return 0;
}

static int mtk_afe_dais_hw_free(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	return snd_pcm_lib_free_pages(substream);
}

static int mtk_afe_dais_trigger(struct snd_pcm_substream *substream, int cmd,
				struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime * const runtime = substream->runtime;
	struct mtk_afe *afe = snd_soc_platform_get_drvdata(rtd->platform);
	struct mtk_afe_memif *memif = &afe->memif[rtd->cpu_dai->id];
	unsigned int counter = runtime->period_size;

	dev_info(afe->dev, "%s %s cmd=%d\n", __func__, memif->data->name, cmd);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		if (memif->data->enable_shift >= 0)
			regmap_update_bits(afe->regmap, AFE_DAC_CON0,
					   1 << memif->data->enable_shift,
					   1 << memif->data->enable_shift);

		/* set irq counter */
		regmap_update_bits(afe->regmap,
				   memif->data->irq_reg_cnt,
				   0x3ffff << memif->data->irq_cnt_shift,
				   counter << memif->data->irq_cnt_shift);

		/* set irq fs */
		if (memif->data->irq_fs_shift >= 0) {
			int fs = mtk_afe_i2s_fs(runtime->rate);

			if (fs < 0)
				return -EINVAL;

			regmap_update_bits(afe->regmap,
					   AFE_IRQ_MCU_CON,
					   0xf << memif->data->irq_fs_shift,
					   fs << memif->data->irq_fs_shift);
		}
		/* enable interrupt */
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON,
				   1 << memif->data->irq_en_shift,
				   1 << memif->data->irq_en_shift);

		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		if (memif->data->enable_shift >= 0)
			regmap_update_bits(afe->regmap, AFE_DAC_CON0,
					   1 << memif->data->enable_shift, 0);
		/* disable interrupt */
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CON,
				   1 << memif->data->irq_en_shift,
				   0 << memif->data->irq_en_shift);
		/* and clear pending IRQ */
		regmap_write(afe->regmap, AFE_IRQ_CLR,
			     1 << memif->data->irq_clr_shift);
		return 0;
	default:
		return -EINVAL;
	}
}

/* FE DAIs */
static const struct snd_soc_dai_ops mtk_afe_dai_ops = {
	.startup	= mtk_afe_dais_startup,
	.shutdown	= mtk_afe_dais_shutdown,
	.hw_params	= mtk_afe_dais_hw_params,
	.hw_free	= mtk_afe_dais_hw_free,
	.trigger	= mtk_afe_dais_trigger,
};

/* BE DAIs */
static const struct snd_soc_dai_ops mtk_afe_i2s_ops = {
	.startup	= mtk_afe_i2s_startup,
	.shutdown	= mtk_afe_i2s_shutdown,
	.prepare	= mtk_afe_i2s_prepare,
};

static const struct snd_soc_dai_ops mtk_afe_hdmi_ops = {
	.startup	= mtk_afe_hdmi_startup,
	.shutdown	= mtk_afe_hdmi_shutdown,
	.prepare	= mtk_afe_hdmi_prepare,
	.trigger	= mtk_afe_hdmi_trigger,

};

static int mtk_afe_runtime_suspend(struct device *dev);
static int mtk_afe_runtime_resume(struct device *dev);

static int mtk_afe_dai_suspend(struct snd_soc_dai *dai)
{
	struct mtk_afe *afe = snd_soc_dai_get_drvdata(dai);
	int i;

	dev_dbg(afe->dev, "%s\n", __func__);
	if (pm_runtime_status_suspended(afe->dev) || afe->suspended)
		return 0;

	for (i = 0; i < ARRAY_SIZE(mtk_afe_backup_list); i++)
		regmap_read(afe->regmap, mtk_afe_backup_list[i],
			    &afe->backup_regs[i]);

	afe->suspended = true;
	mtk_afe_runtime_suspend(afe->dev);
	return 0;
}

static int mtk_afe_dai_resume(struct snd_soc_dai *dai)
{
	struct mtk_afe *afe = snd_soc_dai_get_drvdata(dai);
	int i = 0;

	dev_dbg(afe->dev, "%s\n", __func__);
	if (pm_runtime_status_suspended(afe->dev) || !afe->suspended)
		return 0;

	mtk_afe_runtime_resume(afe->dev);

	for (i = 0; i < ARRAY_SIZE(mtk_afe_backup_list); i++)
		regmap_write(afe->regmap, mtk_afe_backup_list[i],
			     afe->backup_regs[i]);

	afe->suspended = false;
	return 0;
}

static struct snd_soc_dai_driver mtk_afe_pcm_dais[] = {
	/* FE DAIs: memory intefaces to CPU */
	{
		.name = "DL1", /* downlink 1 */
		.id = MTK_AFE_MEMIF_DL1,
		.suspend = mtk_afe_dai_suspend,
		.resume = mtk_afe_dai_resume,
		.playback = {
			.stream_name = "DL1",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &mtk_afe_dai_ops,
	}, {
		.name = "VUL", /* voice uplink */
		.id = MTK_AFE_MEMIF_VUL,
		.suspend = mtk_afe_dai_suspend,
		.resume = mtk_afe_dai_resume,
		.capture = {
			.stream_name = "VUL",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &mtk_afe_dai_ops,
	}, {
	/* BE DAIs */
		.name = "I2S",
		.id = MTK_AFE_IO_I2S,
		.playback = {
			.stream_name = "I2S Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.stream_name = "I2S Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &mtk_afe_i2s_ops,
		.symmetric_rates = 1,
	},
};

static struct snd_soc_dai_driver mtk_afe_hdmi_dais[] = {
	/* FE DAIs */
	{
		.name = "HDMI",
		.id = MTK_AFE_MEMIF_HDMI,
		.suspend = mtk_afe_dai_suspend,
		.resume = mtk_afe_dai_resume,
		.playback = {
			.stream_name = "HDMI",
			.channels_min = 2,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &mtk_afe_dai_ops,
	}, {
	/* BE DAIs */
		.name = "HDMIO",
		.id = MTK_AFE_IO_HDMI,
		.playback = {
			.stream_name = "HDMIO Playback",
			.channels_min = 2,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_32000 | SNDRV_PCM_RATE_44100 |
				SNDRV_PCM_RATE_48000 | SNDRV_PCM_RATE_88200 |
				SNDRV_PCM_RATE_96000 | SNDRV_PCM_RATE_176400 |
				SNDRV_PCM_RATE_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &mtk_afe_hdmi_ops,
	},
};

static const struct snd_kcontrol_new mtk_afe_o03_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I05 Switch", AFE_CONN1, 21, 1, 0),
};

static const struct snd_kcontrol_new mtk_afe_o04_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I06 Switch", AFE_CONN2, 6, 1, 0),
};

static const struct snd_kcontrol_new mtk_afe_o09_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I03 Switch", AFE_CONN3, 0, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I17 Switch", AFE_CONN7, 30, 1, 0),
};

static const struct snd_kcontrol_new mtk_afe_o10_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I04 Switch", AFE_CONN3, 3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I18 Switch", AFE_CONN8, 0, 1, 0),
};

static const struct snd_soc_dapm_widget mtk_afe_pcm_widgets[] = {
	/* inter-connections */
	SND_SOC_DAPM_MIXER("I03", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I04", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I05", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I06", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I17", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I18", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("O03", SND_SOC_NOPM, 0, 0,
			   mtk_afe_o03_mix, ARRAY_SIZE(mtk_afe_o03_mix)),
	SND_SOC_DAPM_MIXER("O04", SND_SOC_NOPM, 0, 0,
			   mtk_afe_o04_mix, ARRAY_SIZE(mtk_afe_o04_mix)),
	SND_SOC_DAPM_MIXER("O09", SND_SOC_NOPM, 0, 0,
			   mtk_afe_o09_mix, ARRAY_SIZE(mtk_afe_o09_mix)),
	SND_SOC_DAPM_MIXER("O10", SND_SOC_NOPM, 0, 0,
			   mtk_afe_o10_mix, ARRAY_SIZE(mtk_afe_o10_mix)),
};

static const struct snd_soc_dapm_route mtk_afe_pcm_routes[] = {
	{"I05", NULL, "DL1"},
	{"I06", NULL, "DL1"},
	{"I2S Playback", NULL, "O03"},
	{"I2S Playback", NULL, "O04"},
	{"VUL", NULL, "O09"},
	{"VUL", NULL, "O10"},
	{"I03", NULL, "I2S Capture"},
	{"I04", NULL, "I2S Capture"},
	{"I17", NULL, "I2S Capture"},
	{"I18", NULL, "I2S Capture"},
	{ "O03", "I05 Switch", "I05" },
	{ "O04", "I06 Switch", "I06" },
	{ "O09", "I17 Switch", "I17" },
	{ "O09", "I03 Switch", "I03" },
	{ "O10", "I18 Switch", "I18" },
	{ "O10", "I04 Switch", "I04" },
};

static const struct snd_soc_dapm_route mtk_afe_hdmi_routes[] = {
	{"HDMIO Playback", NULL, "HDMI"},
};

static const struct snd_soc_component_driver mtk_afe_pcm_dai_component = {
	.name = "mtk-afe-pcm-dai",
	.dapm_widgets = mtk_afe_pcm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mtk_afe_pcm_widgets),
	.dapm_routes = mtk_afe_pcm_routes,
	.num_dapm_routes = ARRAY_SIZE(mtk_afe_pcm_routes),
};

static const struct snd_soc_component_driver mtk_afe_hdmi_dai_component = {
	.name = "mtk-afe-hdmi-dai",
	.dapm_routes = mtk_afe_hdmi_routes,
	.num_dapm_routes = ARRAY_SIZE(mtk_afe_hdmi_routes),
};

static const char *aud_clks[MTK_CLK_NUM] = {
	[MTK_CLK_INFRASYS_AUD] = "infra_sys_audio_clk",
	[MTK_CLK_TOP_PDN_AUD] = "top_pdn_audio",
	[MTK_CLK_TOP_PDN_AUD_BUS] = "top_pdn_aud_intbus",
	[MTK_CLK_I2S0_M] =  "i2s0_m",
	[MTK_CLK_I2S1_M] =  "i2s1_m",
	[MTK_CLK_I2S2_M] =  "i2s2_m",
	[MTK_CLK_I2S3_M] =  "i2s3_m",
	[MTK_CLK_I2S3_B] =  "i2s3_b",
	[MTK_CLK_BCK0] =  "bck0",
	[MTK_CLK_BCK1] =  "bck1",
};

static const struct mtk_afe_memif_data memif_data[MTK_AFE_MEMIF_NUM] = {
	{
		.name = "DL1",
		.id = MTK_AFE_MEMIF_DL1,
		.reg_ofs_base = AFE_DL1_BASE,
		.reg_ofs_cur = AFE_DL1_CUR,
		.fs_shift = 0,
		.mono_shift = 21,
		.enable_shift = 1,
		.irq_reg_cnt = AFE_IRQ_CNT1,
		.irq_cnt_shift = 0,
		.irq_en_shift = 0,
		.irq_fs_shift = 4,
		.irq_clr_shift = 0,
		.msb_shift = 0,
	}, {
		.name = "DL2",
		.id = MTK_AFE_MEMIF_DL2,
		.reg_ofs_base = AFE_DL2_BASE,
		.reg_ofs_cur = AFE_DL2_CUR,
		.fs_shift = 4,
		.mono_shift = 22,
		.enable_shift = 2,
		.irq_reg_cnt = AFE_IRQ_CNT1,
		.irq_cnt_shift = 20,
		.irq_en_shift = 2,
		.irq_fs_shift = 16,
		.irq_clr_shift = 2,
		.msb_shift = 1,
	}, {
		.name = "VUL",
		.id = MTK_AFE_MEMIF_VUL,
		.reg_ofs_base = AFE_VUL_BASE,
		.reg_ofs_cur = AFE_VUL_CUR,
		.fs_shift = 16,
		.mono_shift = 27,
		.enable_shift = 3,
		.irq_reg_cnt = AFE_IRQ_CNT2,
		.irq_cnt_shift = 0,
		.irq_en_shift = 1,
		.irq_fs_shift = 8,
		.irq_clr_shift = 1,
		.msb_shift = 6,
	}, {
		.name = "DAI",
		.id = MTK_AFE_MEMIF_DAI,
		.reg_ofs_base = AFE_DAI_BASE,
		.reg_ofs_cur = AFE_DAI_CUR,
		.fs_shift = 24,
		.mono_shift = -1,
		.enable_shift = 4,
		.irq_reg_cnt = AFE_IRQ_CNT2,
		.irq_cnt_shift = 20,
		.irq_en_shift = 3,
		.irq_fs_shift = 20,
		.irq_clr_shift = 3,
		.msb_shift = 5,
	}, {
		.name = "AWB",
		.id = MTK_AFE_MEMIF_AWB,
		.reg_ofs_base = AFE_AWB_BASE,
		.reg_ofs_cur = AFE_AWB_CUR,
		.fs_shift = 12,
		.mono_shift = 24,
		.enable_shift = 6,
		.irq_reg_cnt = AFE_IRQ_CNT7,
		.irq_cnt_shift = 0,
		.irq_en_shift = 14,
		.irq_fs_shift = 24,
		.irq_clr_shift = 6,
		.msb_shift = 3,
	}, {
		.name = "MOD_DAI",
		.id = MTK_AFE_MEMIF_MOD_DAI,
		.reg_ofs_base = AFE_MOD_PCM_BASE,
		.reg_ofs_cur = AFE_MOD_PCM_CUR,
		.fs_shift = 30,
		.mono_shift = 30,
		.enable_shift = 7,
		.irq_reg_cnt = AFE_IRQ_CNT2,
		.irq_cnt_shift = 20,
		.irq_en_shift = 3,
		.irq_fs_shift = 20,
		.irq_clr_shift = 3,
		.msb_shift = 4,
	}, {
		.name = "HDMI",
		.id = MTK_AFE_MEMIF_HDMI,
		.reg_ofs_base = AFE_HDMI_OUT_BASE,
		.reg_ofs_cur = AFE_HDMI_OUT_CUR,
		.fs_shift = -1,
		.mono_shift = -1,
		.enable_shift = -1,
		.irq_reg_cnt = AFE_IRQ_CNT5,
		.irq_cnt_shift = 0,
		.irq_en_shift = 12,
		.irq_fs_shift = -1,
		.irq_clr_shift = 4,
		.msb_shift = 8,
	},
};

static const struct regmap_config mtk_afe_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = AFE_ADDA2_TOP_CON0,
	.cache_type = REGCACHE_NONE,
};

static irqreturn_t mtk_afe_irq_handler(int irq, void *dev_id)
{
	struct mtk_afe *afe = dev_id;
	unsigned int reg_value;
	int i, ret;

	ret = regmap_read(afe->regmap, AFE_IRQ_STATUS, &reg_value);
	if (ret) {
		dev_err(afe->dev, "%s irq status err\n", __func__);
		reg_value = AFE_IRQ_STATUS_BITS;
		goto err_irq;
	}

	for (i = 0; i < MTK_AFE_MEMIF_NUM; i++) {
		struct mtk_afe_memif *memif = &afe->memif[i];

		if (!(reg_value & (1 << memif->data->irq_clr_shift)))
			continue;

		snd_pcm_period_elapsed(memif->substream);
	}

err_irq:
	/* clear irq */
	regmap_write(afe->regmap, AFE_IRQ_CLR, reg_value & AFE_IRQ_STATUS_BITS);

	return IRQ_HANDLED;
}

static int mtk_afe_runtime_suspend(struct device *dev)
{
	struct mtk_afe *afe = dev_get_drvdata(dev);

	/* disable AFE */
	regmap_update_bits(afe->regmap, AFE_DAC_CON0, 0x1, 0);

	/* disable AFE clk */
	regmap_update_bits(afe->regmap, AUDIO_TOP_CON0,
			   AUD_TCON0_PDN_AFE, AUD_TCON0_PDN_AFE);

	clk_disable_unprepare(afe->clocks[MTK_CLK_BCK0]);
	clk_disable_unprepare(afe->clocks[MTK_CLK_BCK1]);
	clk_disable_unprepare(afe->clocks[MTK_CLK_TOP_PDN_AUD]);
	clk_disable_unprepare(afe->clocks[MTK_CLK_TOP_PDN_AUD_BUS]);
	clk_disable_unprepare(afe->clocks[MTK_CLK_INFRASYS_AUD]);
	return 0;
}

static int mtk_afe_runtime_resume(struct device *dev)
{
	struct mtk_afe *afe = dev_get_drvdata(dev);
	int ret;

	ret = clk_prepare_enable(afe->clocks[MTK_CLK_INFRASYS_AUD]);
	if (ret)
		return ret;

	ret = clk_prepare_enable(afe->clocks[MTK_CLK_TOP_PDN_AUD_BUS]);
	if (ret)
		goto err_infra;

	ret = clk_prepare_enable(afe->clocks[MTK_CLK_TOP_PDN_AUD]);
	if (ret)
		goto err_top_aud_bus;

	ret = clk_prepare_enable(afe->clocks[MTK_CLK_BCK0]);
	if (ret)
		goto err_top_aud;

	ret = clk_prepare_enable(afe->clocks[MTK_CLK_BCK1]);
	if (ret)
		goto err_bck0;

	/* enable AFE clk */
	regmap_update_bits(afe->regmap, AUDIO_TOP_CON0, AUD_TCON0_PDN_AFE, 0);

	/* set O3/O4 16bits */
	regmap_update_bits(afe->regmap, AFE_CONN_24BIT,
			   AFE_CONN_24BIT_O03 | AFE_CONN_24BIT_O04, 0);

	/* unmask all IRQs */
	regmap_update_bits(afe->regmap, AFE_IRQ_MCU_EN, 0xff, 0xff);

	/* enable AFE */
	regmap_update_bits(afe->regmap, AFE_DAC_CON0, 0x1, 0x1);
	return 0;

err_bck0:
	clk_disable_unprepare(afe->clocks[MTK_CLK_BCK0]);
err_top_aud:
	clk_disable_unprepare(afe->clocks[MTK_CLK_TOP_PDN_AUD]);
err_top_aud_bus:
	clk_disable_unprepare(afe->clocks[MTK_CLK_TOP_PDN_AUD_BUS]);
err_infra:
	clk_disable_unprepare(afe->clocks[MTK_CLK_INFRASYS_AUD]);
	return ret;
}

static int mtk_afe_init_audio_clk(struct mtk_afe *afe)
{
	size_t i;

	for (i = 0; i < ARRAY_SIZE(aud_clks); i++) {
		afe->clocks[i] = devm_clk_get(afe->dev, aud_clks[i]);
		if (IS_ERR(afe->clocks[i])) {
			dev_err(afe->dev, "%s devm_clk_get %s fail\n",
				__func__, aud_clks[i]);
			return PTR_ERR(afe->clocks[i]);
		}
	}
	clk_set_rate(afe->clocks[MTK_CLK_BCK0], 22579200); /* 22M */
	clk_set_rate(afe->clocks[MTK_CLK_BCK1], 24576000); /* 24M */
	return 0;
}

static int mtk_afe_pcm_dev_probe(struct platform_device *pdev)
{
	int ret, i;
	unsigned int irq_id;
	struct mtk_afe *afe;
	struct resource *res;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(33));
	if (ret)
		return ret;

	afe = devm_kzalloc(&pdev->dev, sizeof(*afe), GFP_KERNEL);
	if (!afe)
		return -ENOMEM;

	afe->dev = &pdev->dev;

	irq_id = platform_get_irq(pdev, 0);
	if (!irq_id) {
		dev_err(afe->dev, "np %s no irq\n", afe->dev->of_node->name);
		return -ENXIO;
	}
	ret = devm_request_irq(afe->dev, irq_id, mtk_afe_irq_handler,
			       0, "Afe_ISR_Handle", (void *)afe);
	if (ret) {
		dev_err(afe->dev, "could not request_irq\n");
		return ret;
	}

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	afe->base_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(afe->base_addr))
		return PTR_ERR(afe->base_addr);

	afe->regmap = devm_regmap_init_mmio(&pdev->dev, afe->base_addr,
		&mtk_afe_regmap_config);
	if (IS_ERR(afe->regmap))
		return PTR_ERR(afe->regmap);

	/* initial audio related clock */
	ret = mtk_afe_init_audio_clk(afe);
	if (ret) {
		dev_err(afe->dev, "mtk_afe_init_audio_clk fail\n");
		return ret;
	}

	for (i = 0; i < MTK_AFE_MEMIF_NUM; i++)
		afe->memif[i].data = &memif_data[i];

	platform_set_drvdata(pdev, afe);

	pm_runtime_enable(&pdev->dev);
	if (!pm_runtime_enabled(&pdev->dev)) {
		ret = mtk_afe_runtime_resume(&pdev->dev);
		if (ret)
			goto err_pm_disable;
	}

	ret = snd_soc_register_platform(&pdev->dev, &mtk_afe_pcm_platform);
	if (ret)
		goto err_pm_disable;

	ret = snd_soc_register_component(&pdev->dev,
					 &mtk_afe_pcm_dai_component,
					 mtk_afe_pcm_dais,
					 ARRAY_SIZE(mtk_afe_pcm_dais));
	if (ret)
		goto err_platform;

	ret = snd_soc_register_component(&pdev->dev,
					 &mtk_afe_hdmi_dai_component,
					 mtk_afe_hdmi_dais,
					 ARRAY_SIZE(mtk_afe_hdmi_dais));
	if (ret)
		goto err_comp;

	dev_info(&pdev->dev, "MTK AFE driver initialized.\n");
	return 0;

err_comp:
	snd_soc_unregister_component(&pdev->dev);
err_platform:
	snd_soc_unregister_platform(&pdev->dev);
err_pm_disable:
	pm_runtime_disable(&pdev->dev);
	return ret;
}

static int mtk_afe_pcm_dev_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		mtk_afe_runtime_suspend(&pdev->dev);
	snd_soc_unregister_component(&pdev->dev);
	snd_soc_unregister_platform(&pdev->dev);
	return 0;
}

static const struct of_device_id mtk_afe_pcm_dt_match[] = {
	{ .compatible = "mediatek,mt8173-afe-pcm", },
	{ }
};
MODULE_DEVICE_TABLE(of, mtk_afe_pcm_dt_match);

static const struct dev_pm_ops mtk_afe_pm_ops = {
	SET_RUNTIME_PM_OPS(mtk_afe_runtime_suspend, mtk_afe_runtime_resume,
			   NULL)
};

static struct platform_driver mtk_afe_pcm_driver = {
	.driver = {
		   .name = "mtk-afe-pcm",
		   .of_match_table = mtk_afe_pcm_dt_match,
		   .pm = &mtk_afe_pm_ops,
	},
	.probe = mtk_afe_pcm_dev_probe,
	.remove = mtk_afe_pcm_dev_remove,
};

module_platform_driver(mtk_afe_pcm_driver);

MODULE_DESCRIPTION("Mediatek ALSA SoC AFE platform driver");
MODULE_AUTHOR("Koro Chen <koro.chen@mediatek.com>");
MODULE_LICENSE("GPL v2");
