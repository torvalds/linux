// SPDX-License-Identifier: GPL-2.0
/*
 * Mediatek ALSA SoC AFE platform driver for 8195
 *
 * Copyright (c) 2021 MediaTek Inc.
 * Author: Bicycle Tsai <bicycle.tsai@mediatek.com>
 *         Trevor Wu <trevor.wu@mediatek.com>
 */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include "mt8195-afe-common.h"
#include "mt8195-afe-clk.h"
#include "mt8195-reg.h"
#include "../common/mtk-afe-platform-driver.h"
#include "../common/mtk-afe-fe-dai.h"

#define MT8195_MEMIF_BUFFER_BYTES_ALIGN  (0x40)
#define MT8195_MEMIF_DL7_MAX_PERIOD_SIZE (0x3fff)

struct mtk_dai_memif_priv {
	unsigned int asys_timing_sel;
};

static const struct snd_pcm_hardware mt8195_afe_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		   SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,
	.period_bytes_min = 64,
	.period_bytes_max = 256 * 1024,
	.periods_min = 2,
	.periods_max = 256,
	.buffer_bytes_max = 256 * 2 * 1024,
};

struct mt8195_afe_rate {
	unsigned int rate;
	unsigned int reg_value;
};

static const struct mt8195_afe_rate mt8195_afe_rates[] = {
	{ .rate = 8000, .reg_value = 0, },
	{ .rate = 12000, .reg_value = 1, },
	{ .rate = 16000, .reg_value = 2, },
	{ .rate = 24000, .reg_value = 3, },
	{ .rate = 32000, .reg_value = 4, },
	{ .rate = 48000, .reg_value = 5, },
	{ .rate = 96000, .reg_value = 6, },
	{ .rate = 192000, .reg_value = 7, },
	{ .rate = 384000, .reg_value = 8, },
	{ .rate = 7350, .reg_value = 16, },
	{ .rate = 11025, .reg_value = 17, },
	{ .rate = 14700, .reg_value = 18, },
	{ .rate = 22050, .reg_value = 19, },
	{ .rate = 29400, .reg_value = 20, },
	{ .rate = 44100, .reg_value = 21, },
	{ .rate = 88200, .reg_value = 22, },
	{ .rate = 176400, .reg_value = 23, },
	{ .rate = 352800, .reg_value = 24, },
};

int mt8195_afe_fs_timing(unsigned int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt8195_afe_rates); i++)
		if (mt8195_afe_rates[i].rate == rate)
			return mt8195_afe_rates[i].reg_value;

	return -EINVAL;
}

static int mt8195_memif_fs(struct snd_pcm_substream *substream,
			   unsigned int rate)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component =
			snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	int id = asoc_rtd_to_cpu(rtd, 0)->id;
	struct mtk_base_afe_memif *memif = &afe->memif[id];
	int fs = mt8195_afe_fs_timing(rate);

	switch (memif->data->id) {
	case MT8195_AFE_MEMIF_DL10:
		fs = MT8195_ETDM_OUT3_1X_EN;
		break;
	case MT8195_AFE_MEMIF_UL8:
		fs = MT8195_ETDM_IN1_NX_EN;
		break;
	case MT8195_AFE_MEMIF_UL3:
		fs = MT8195_ETDM_IN2_NX_EN;
		break;
	default:
		break;
	}

	return fs;
}

static int mt8195_irq_fs(struct snd_pcm_substream *substream,
			 unsigned int rate)
{
	int fs = mt8195_memif_fs(substream, rate);

	switch (fs) {
	case MT8195_ETDM_IN1_NX_EN:
		fs = MT8195_ETDM_IN1_1X_EN;
		break;
	case MT8195_ETDM_IN2_NX_EN:
		fs = MT8195_ETDM_IN2_1X_EN;
		break;
	default:
		break;
	}

	return fs;
}

enum {
	MT8195_AFE_CM0,
	MT8195_AFE_CM1,
	MT8195_AFE_CM2,
	MT8195_AFE_CM_NUM,
};

struct mt8195_afe_channel_merge {
	int id;
	int reg;
	unsigned int sel_shift;
	unsigned int sel_maskbit;
	unsigned int sel_default;
	unsigned int ch_num_shift;
	unsigned int ch_num_maskbit;
	unsigned int en_shift;
	unsigned int en_maskbit;
	unsigned int update_cnt_shift;
	unsigned int update_cnt_maskbit;
	unsigned int update_cnt_default;
};

static const struct mt8195_afe_channel_merge
	mt8195_afe_cm[MT8195_AFE_CM_NUM] = {
	[MT8195_AFE_CM0] = {
		.id = MT8195_AFE_CM0,
		.reg = AFE_CM0_CON,
		.sel_shift = 30,
		.sel_maskbit = 0x1,
		.sel_default = 1,
		.ch_num_shift = 2,
		.ch_num_maskbit = 0x3f,
		.en_shift = 0,
		.en_maskbit = 0x1,
		.update_cnt_shift = 16,
		.update_cnt_maskbit = 0x1fff,
		.update_cnt_default = 0x3,
	},
	[MT8195_AFE_CM1] = {
		.id = MT8195_AFE_CM1,
		.reg = AFE_CM1_CON,
		.sel_shift = 30,
		.sel_maskbit = 0x1,
		.sel_default = 1,
		.ch_num_shift = 2,
		.ch_num_maskbit = 0x1f,
		.en_shift = 0,
		.en_maskbit = 0x1,
		.update_cnt_shift = 16,
		.update_cnt_maskbit = 0x1fff,
		.update_cnt_default = 0x3,
	},
	[MT8195_AFE_CM2] = {
		.id = MT8195_AFE_CM2,
		.reg = AFE_CM2_CON,
		.sel_shift = 30,
		.sel_maskbit = 0x1,
		.sel_default = 1,
		.ch_num_shift = 2,
		.ch_num_maskbit = 0x1f,
		.en_shift = 0,
		.en_maskbit = 0x1,
		.update_cnt_shift = 16,
		.update_cnt_maskbit = 0x1fff,
		.update_cnt_default = 0x3,
	},
};

static int mt8195_afe_memif_is_ul(int id)
{
	if (id >= MT8195_AFE_MEMIF_UL_START && id < MT8195_AFE_MEMIF_END)
		return 1;
	else
		return 0;
}

static const struct mt8195_afe_channel_merge*
mt8195_afe_found_cm(struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int id = -EINVAL;

	if (mt8195_afe_memif_is_ul(dai->id) == 0)
		return NULL;

	switch (dai->id) {
	case MT8195_AFE_MEMIF_UL9:
		id = MT8195_AFE_CM0;
		break;
	case MT8195_AFE_MEMIF_UL2:
		id = MT8195_AFE_CM1;
		break;
	case MT8195_AFE_MEMIF_UL10:
		id = MT8195_AFE_CM2;
		break;
	default:
		break;
	}

	if (id < 0) {
		dev_dbg(afe->dev, "%s, memif %d cannot find CM!\n",
			__func__, dai->id);
		return NULL;
	}

	return &mt8195_afe_cm[id];
}

static int mt8195_afe_config_cm(struct mtk_base_afe *afe,
				const struct mt8195_afe_channel_merge *cm,
				unsigned int channels)
{
	if (!cm)
		return -EINVAL;

	regmap_update_bits(afe->regmap,
			   cm->reg,
			   cm->sel_maskbit << cm->sel_shift,
			   cm->sel_default << cm->sel_shift);

	regmap_update_bits(afe->regmap,
			   cm->reg,
			   cm->ch_num_maskbit << cm->ch_num_shift,
			   (channels - 1) << cm->ch_num_shift);

	regmap_update_bits(afe->regmap,
			   cm->reg,
			   cm->update_cnt_maskbit << cm->update_cnt_shift,
			   cm->update_cnt_default << cm->update_cnt_shift);

	return 0;
}

static int mt8195_afe_enable_cm(struct mtk_base_afe *afe,
				const struct mt8195_afe_channel_merge *cm,
				bool enable)
{
	if (!cm)
		return -EINVAL;

	regmap_update_bits(afe->regmap,
			   cm->reg,
			   cm->en_maskbit << cm->en_shift,
			   enable << cm->en_shift);

	return 0;
}

static int
mt8195_afe_paired_memif_clk_prepare(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai,
				    int enable)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8195_afe_private *afe_priv = afe->platform_priv;
	int id = asoc_rtd_to_cpu(rtd, 0)->id;
	int clk_id;

	if (id != MT8195_AFE_MEMIF_DL8 && id != MT8195_AFE_MEMIF_DL10)
		return 0;

	if (enable) {
		clk_id = MT8195_CLK_AUD_MEMIF_DL10;
		mt8195_afe_prepare_clk(afe, afe_priv->clk[clk_id]);
		clk_id = MT8195_CLK_AUD_MEMIF_DL8;
		mt8195_afe_prepare_clk(afe, afe_priv->clk[clk_id]);
	} else {
		clk_id = MT8195_CLK_AUD_MEMIF_DL8;
		mt8195_afe_unprepare_clk(afe, afe_priv->clk[clk_id]);
		clk_id = MT8195_CLK_AUD_MEMIF_DL10;
		mt8195_afe_unprepare_clk(afe, afe_priv->clk[clk_id]);
	}

	return 0;
}

static int
mt8195_afe_paired_memif_clk_enable(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai,
				   int enable)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8195_afe_private *afe_priv = afe->platform_priv;
	int id = asoc_rtd_to_cpu(rtd, 0)->id;
	int clk_id;

	if (id != MT8195_AFE_MEMIF_DL8 && id != MT8195_AFE_MEMIF_DL10)
		return 0;

	if (enable) {
		/* DL8_DL10_MEM */
		clk_id = MT8195_CLK_AUD_MEMIF_DL10;
		mt8195_afe_enable_clk_atomic(afe, afe_priv->clk[clk_id]);
		udelay(1);
		/* DL8_DL10_AGENT */
		clk_id = MT8195_CLK_AUD_MEMIF_DL8;
		mt8195_afe_enable_clk_atomic(afe, afe_priv->clk[clk_id]);
	} else {
		/* DL8_DL10_AGENT */
		clk_id = MT8195_CLK_AUD_MEMIF_DL8;
		mt8195_afe_disable_clk_atomic(afe, afe_priv->clk[clk_id]);
		/* DL8_DL10_MEM */
		clk_id = MT8195_CLK_AUD_MEMIF_DL10;
		mt8195_afe_disable_clk_atomic(afe, afe_priv->clk[clk_id]);
	}

	return 0;
}

static int mt8195_afe_fe_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int id = asoc_rtd_to_cpu(rtd, 0)->id;
	int ret = 0;

	mt8195_afe_paired_memif_clk_prepare(substream, dai, 1);

	ret = mtk_afe_fe_startup(substream, dai);

	snd_pcm_hw_constraint_step(runtime, 0,
				   SNDRV_PCM_HW_PARAM_BUFFER_BYTES,
				   MT8195_MEMIF_BUFFER_BYTES_ALIGN);

	if (id != MT8195_AFE_MEMIF_DL7)
		goto out;

	ret = snd_pcm_hw_constraint_minmax(runtime,
					   SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
					   1,
					   MT8195_MEMIF_DL7_MAX_PERIOD_SIZE);
	if (ret < 0)
		dev_dbg(afe->dev, "hw_constraint_minmax failed\n");
out:
	return ret;
}

static void mt8195_afe_fe_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	mtk_afe_fe_shutdown(substream, dai);
	mt8195_afe_paired_memif_clk_prepare(substream, dai, 0);
}

static int mt8195_afe_fe_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int id = asoc_rtd_to_cpu(rtd, 0)->id;
	struct mtk_base_afe_memif *memif = &afe->memif[id];
	const struct mtk_base_memif_data *data = memif->data;
	const struct mt8195_afe_channel_merge *cm = mt8195_afe_found_cm(dai);
	unsigned int ch_num = params_channels(params);

	mt8195_afe_config_cm(afe, cm, params_channels(params));

	if (data->ch_num_reg >= 0) {
		regmap_update_bits(afe->regmap, data->ch_num_reg,
				   data->ch_num_maskbit << data->ch_num_shift,
				   ch_num << data->ch_num_shift);
	}

	return mtk_afe_fe_hw_params(substream, params, dai);
}

static int mt8195_afe_fe_hw_free(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	return mtk_afe_fe_hw_free(substream, dai);
}

static int mt8195_afe_fe_prepare(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	return mtk_afe_fe_prepare(substream, dai);
}

static int mt8195_afe_fe_trigger(struct snd_pcm_substream *substream, int cmd,
				 struct snd_soc_dai *dai)
{
	int ret = 0;
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	const struct mt8195_afe_channel_merge *cm = mt8195_afe_found_cm(dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		mt8195_afe_enable_cm(afe, cm, true);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		mt8195_afe_enable_cm(afe, cm, false);
		break;
	default:
		break;
	}

	ret = mtk_afe_fe_trigger(substream, cmd, dai);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		mt8195_afe_paired_memif_clk_enable(substream, dai, 1);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		mt8195_afe_paired_memif_clk_enable(substream, dai, 0);
		break;
	default:
		break;
	}

	return ret;
}

static int mt8195_afe_fe_set_fmt(struct snd_soc_dai *dai, unsigned int fmt)
{
	return 0;
}

static const struct snd_soc_dai_ops mt8195_afe_fe_dai_ops = {
	.startup	= mt8195_afe_fe_startup,
	.shutdown	= mt8195_afe_fe_shutdown,
	.hw_params	= mt8195_afe_fe_hw_params,
	.hw_free	= mt8195_afe_fe_hw_free,
	.prepare	= mt8195_afe_fe_prepare,
	.trigger	= mt8195_afe_fe_trigger,
	.set_fmt	= mt8195_afe_fe_set_fmt,
};

#define MTK_PCM_RATES (SNDRV_PCM_RATE_8000_48000 |\
		       SNDRV_PCM_RATE_88200 |\
		       SNDRV_PCM_RATE_96000 |\
		       SNDRV_PCM_RATE_176400 |\
		       SNDRV_PCM_RATE_192000 |\
		       SNDRV_PCM_RATE_352800 |\
		       SNDRV_PCM_RATE_384000)

#define MTK_PCM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mt8195_memif_dai_driver[] = {
	/* FE DAIs: memory intefaces to CPU */
	{
		.name = "DL2",
		.id = MT8195_AFE_MEMIF_DL2,
		.playback = {
			.stream_name = "DL2",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "DL3",
		.id = MT8195_AFE_MEMIF_DL3,
		.playback = {
			.stream_name = "DL3",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "DL6",
		.id = MT8195_AFE_MEMIF_DL6,
		.playback = {
			.stream_name = "DL6",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "DL7",
		.id = MT8195_AFE_MEMIF_DL7,
		.playback = {
			.stream_name = "DL7",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "DL8",
		.id = MT8195_AFE_MEMIF_DL8,
		.playback = {
			.stream_name = "DL8",
			.channels_min = 1,
			.channels_max = 24,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "DL10",
		.id = MT8195_AFE_MEMIF_DL10,
		.playback = {
			.stream_name = "DL10",
			.channels_min = 1,
			.channels_max = 8,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "DL11",
		.id = MT8195_AFE_MEMIF_DL11,
		.playback = {
			.stream_name = "DL11",
			.channels_min = 1,
			.channels_max = 48,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "UL1",
		.id = MT8195_AFE_MEMIF_UL1,
		.capture = {
			.stream_name = "UL1",
			.channels_min = 1,
			.channels_max = 8,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "UL2",
		.id = MT8195_AFE_MEMIF_UL2,
		.capture = {
			.stream_name = "UL2",
			.channels_min = 1,
			.channels_max = 8,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "UL3",
		.id = MT8195_AFE_MEMIF_UL3,
		.capture = {
			.stream_name = "UL3",
			.channels_min = 1,
			.channels_max = 16,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "UL4",
		.id = MT8195_AFE_MEMIF_UL4,
		.capture = {
			.stream_name = "UL4",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "UL5",
		.id = MT8195_AFE_MEMIF_UL5,
		.capture = {
			.stream_name = "UL5",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "UL6",
		.id = MT8195_AFE_MEMIF_UL6,
		.capture = {
			.stream_name = "UL6",
			.channels_min = 1,
			.channels_max = 8,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "UL8",
		.id = MT8195_AFE_MEMIF_UL8,
		.capture = {
			.stream_name = "UL8",
			.channels_min = 1,
			.channels_max = 24,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "UL9",
		.id = MT8195_AFE_MEMIF_UL9,
		.capture = {
			.stream_name = "UL9",
			.channels_min = 1,
			.channels_max = 32,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
	{
		.name = "UL10",
		.id = MT8195_AFE_MEMIF_UL10,
		.capture = {
			.stream_name = "UL10",
			.channels_min = 1,
			.channels_max = 4,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mt8195_afe_fe_dai_ops,
	},
};

static const struct snd_kcontrol_new o002_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I000 Switch", AFE_CONN2, 0, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I012 Switch", AFE_CONN2, 12, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I020 Switch", AFE_CONN2, 20, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I022 Switch", AFE_CONN2, 22, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I070 Switch", AFE_CONN2_2, 6, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I072 Switch", AFE_CONN2_2, 8, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I168 Switch", AFE_CONN2_5, 8, 1, 0),
};

static const struct snd_kcontrol_new o003_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I001 Switch", AFE_CONN3, 1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I013 Switch", AFE_CONN3, 13, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I021 Switch", AFE_CONN3, 21, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I023 Switch", AFE_CONN3, 23, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I071 Switch", AFE_CONN3_2, 7, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I073 Switch", AFE_CONN3_2, 9, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I169 Switch", AFE_CONN3_5, 9, 1, 0),
};

static const struct snd_kcontrol_new o004_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I000 Switch", AFE_CONN4, 0, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I014 Switch", AFE_CONN4, 14, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I024 Switch", AFE_CONN4, 24, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I074 Switch", AFE_CONN4_2, 10, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I170 Switch", AFE_CONN4_5, 10, 1, 0),
};

static const struct snd_kcontrol_new o005_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I001 Switch", AFE_CONN5, 1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I015 Switch", AFE_CONN5, 15, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I025 Switch", AFE_CONN5, 25, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I075 Switch", AFE_CONN5_2, 11, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I171 Switch", AFE_CONN5_5, 11, 1, 0),
};

static const struct snd_kcontrol_new o006_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I000 Switch", AFE_CONN6, 0, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I016 Switch", AFE_CONN6, 16, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I026 Switch", AFE_CONN6, 26, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I076 Switch", AFE_CONN6_2, 12, 1, 0),
};

static const struct snd_kcontrol_new o007_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I001 Switch", AFE_CONN7, 1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I017 Switch", AFE_CONN7, 17, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I027 Switch", AFE_CONN7, 27, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I077 Switch", AFE_CONN7_2, 13, 1, 0),
};

static const struct snd_kcontrol_new o008_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I018 Switch", AFE_CONN8, 18, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I028 Switch", AFE_CONN8, 28, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I078 Switch", AFE_CONN8_2, 14, 1, 0),
};

static const struct snd_kcontrol_new o009_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I019 Switch", AFE_CONN9, 19, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I029 Switch", AFE_CONN9, 29, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I079 Switch", AFE_CONN9_2, 15, 1, 0),
};

static const struct snd_kcontrol_new o010_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I022 Switch", AFE_CONN10, 22, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I030 Switch", AFE_CONN10, 30, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I046 Switch", AFE_CONN10_1, 14, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I072 Switch", AFE_CONN10_2, 8, 1, 0),
};

static const struct snd_kcontrol_new o011_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I023 Switch", AFE_CONN11, 23, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I031 Switch", AFE_CONN11, 31, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I047 Switch", AFE_CONN11_1, 15, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I073 Switch", AFE_CONN11_2, 9, 1, 0),
};

static const struct snd_kcontrol_new o012_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I024 Switch", AFE_CONN12, 24, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I032 Switch", AFE_CONN12_1, 0, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I048 Switch", AFE_CONN12_1, 16, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I074 Switch", AFE_CONN12_2, 10, 1, 0),
};

static const struct snd_kcontrol_new o013_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I025 Switch", AFE_CONN13, 25, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I033 Switch", AFE_CONN13_1, 1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I049 Switch", AFE_CONN13_1, 17, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I075 Switch", AFE_CONN13_2, 11, 1, 0),
};

static const struct snd_kcontrol_new o014_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I026 Switch", AFE_CONN14, 26, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I034 Switch", AFE_CONN14_1, 2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I050 Switch", AFE_CONN14_1, 18, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I076 Switch", AFE_CONN14_2, 12, 1, 0),
};

static const struct snd_kcontrol_new o015_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I027 Switch", AFE_CONN15, 27, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I035 Switch", AFE_CONN15_1, 3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I051 Switch", AFE_CONN15_1, 19, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I077 Switch", AFE_CONN15_2, 13, 1, 0),
};

static const struct snd_kcontrol_new o016_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I028 Switch", AFE_CONN16, 28, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I036 Switch", AFE_CONN16_1, 4, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I052 Switch", AFE_CONN16_1, 20, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I078 Switch", AFE_CONN16_2, 14, 1, 0),
};

static const struct snd_kcontrol_new o017_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I029 Switch", AFE_CONN17, 29, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I037 Switch", AFE_CONN17_1, 5, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I053 Switch", AFE_CONN17_1, 21, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I079 Switch", AFE_CONN17_2, 15, 1, 0),
};

static const struct snd_kcontrol_new o018_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I038 Switch", AFE_CONN18_1, 6, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I080 Switch", AFE_CONN18_2, 16, 1, 0),
};

static const struct snd_kcontrol_new o019_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I039 Switch", AFE_CONN19_1, 7, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I081 Switch", AFE_CONN19_2, 17, 1, 0),
};

static const struct snd_kcontrol_new o020_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I040 Switch", AFE_CONN20_1, 8, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I082 Switch", AFE_CONN20_2, 18, 1, 0),
};

static const struct snd_kcontrol_new o021_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I041 Switch", AFE_CONN21_1, 9, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I083 Switch", AFE_CONN21_2, 19, 1, 0),
};

static const struct snd_kcontrol_new o022_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I042 Switch", AFE_CONN22_1, 10, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I084 Switch", AFE_CONN22_2, 20, 1, 0),
};

static const struct snd_kcontrol_new o023_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I043 Switch", AFE_CONN23_1, 11, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I085 Switch", AFE_CONN23_2, 21, 1, 0),
};

static const struct snd_kcontrol_new o024_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I044 Switch", AFE_CONN24_1, 12, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I086 Switch", AFE_CONN24_2, 22, 1, 0),
};

static const struct snd_kcontrol_new o025_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I045 Switch", AFE_CONN25_1, 13, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I087 Switch", AFE_CONN25_2, 23, 1, 0),
};

static const struct snd_kcontrol_new o026_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I046 Switch", AFE_CONN26_1, 14, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I088 Switch", AFE_CONN26_2, 24, 1, 0),
};

static const struct snd_kcontrol_new o027_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I047 Switch", AFE_CONN27_1, 15, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I089 Switch", AFE_CONN27_2, 25, 1, 0),
};

static const struct snd_kcontrol_new o028_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I048 Switch", AFE_CONN28_1, 16, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I090 Switch", AFE_CONN28_2, 26, 1, 0),
};

static const struct snd_kcontrol_new o029_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I049 Switch", AFE_CONN29_1, 17, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I091 Switch", AFE_CONN29_2, 27, 1, 0),
};

static const struct snd_kcontrol_new o030_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I050 Switch", AFE_CONN30_1, 18, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I092 Switch", AFE_CONN30_2, 28, 1, 0),
};

static const struct snd_kcontrol_new o031_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I051 Switch", AFE_CONN31_1, 19, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I093 Switch", AFE_CONN31_2, 29, 1, 0),
};

static const struct snd_kcontrol_new o032_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I052 Switch", AFE_CONN32_1, 20, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I094 Switch", AFE_CONN32_2, 30, 1, 0),
};

static const struct snd_kcontrol_new o033_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I053 Switch", AFE_CONN33_1, 21, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I095 Switch", AFE_CONN33_2, 31, 1, 0),
};

static const struct snd_kcontrol_new o034_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I000 Switch", AFE_CONN34, 0, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I002 Switch", AFE_CONN34, 2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I012 Switch", AFE_CONN34, 12, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I020 Switch", AFE_CONN34, 20, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I070 Switch", AFE_CONN34_2, 6, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I072 Switch", AFE_CONN34_2, 8, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I168 Switch", AFE_CONN34_5, 8, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I170 Switch", AFE_CONN34_5, 10, 1, 0),
};

static const struct snd_kcontrol_new o035_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I001 Switch", AFE_CONN35, 1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I003 Switch", AFE_CONN35, 3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I013 Switch", AFE_CONN35, 13, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I021 Switch", AFE_CONN35, 21, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I071 Switch", AFE_CONN35_2, 7, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I073 Switch", AFE_CONN35_2, 9, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I137 Switch", AFE_CONN35_4, 9, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I139 Switch", AFE_CONN35_4, 11, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I168 Switch", AFE_CONN35_5, 8, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I169 Switch", AFE_CONN35_5, 9, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I170 Switch", AFE_CONN35_5, 10, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I171 Switch", AFE_CONN35_5, 11, 1, 0),
};

static const struct snd_kcontrol_new o036_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I000 Switch", AFE_CONN36, 0, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I012 Switch", AFE_CONN36, 12, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I020 Switch", AFE_CONN36, 20, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I070 Switch", AFE_CONN36_2, 6, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I168 Switch", AFE_CONN36_5, 8, 1, 0),
};

static const struct snd_kcontrol_new o037_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I001 Switch", AFE_CONN37, 1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I013 Switch", AFE_CONN37, 13, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I021 Switch", AFE_CONN37, 21, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I071 Switch", AFE_CONN37_2, 7, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I169 Switch", AFE_CONN37_5, 9, 1, 0),
};

static const struct snd_kcontrol_new o038_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I022 Switch", AFE_CONN38, 22, 1, 0),
};

static const struct snd_kcontrol_new o039_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I023 Switch", AFE_CONN39, 23, 1, 0),
};

static const struct snd_kcontrol_new o040_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I002 Switch", AFE_CONN40, 2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I012 Switch", AFE_CONN40, 12, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I022 Switch", AFE_CONN40, 22, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I168 Switch", AFE_CONN40_5, 8, 1, 0),
};

static const struct snd_kcontrol_new o041_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I003 Switch", AFE_CONN41, 3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I013 Switch", AFE_CONN41, 13, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I023 Switch", AFE_CONN41, 23, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I169 Switch", AFE_CONN41_5, 9, 1, 0),
};

static const struct snd_kcontrol_new o042_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I014 Switch", AFE_CONN42, 14, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I024 Switch", AFE_CONN42, 24, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I170 Switch", AFE_CONN42_5, 10, 1, 0),
};

static const struct snd_kcontrol_new o043_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I015 Switch", AFE_CONN43, 15, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I025 Switch", AFE_CONN43, 25, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I171 Switch", AFE_CONN43_5, 11, 1, 0),
};

static const struct snd_kcontrol_new o044_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I016 Switch", AFE_CONN44, 16, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I026 Switch", AFE_CONN44, 26, 1, 0),
};

static const struct snd_kcontrol_new o045_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I017 Switch", AFE_CONN45, 17, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I027 Switch", AFE_CONN45, 27, 1, 0),
};

static const struct snd_kcontrol_new o046_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I018 Switch", AFE_CONN46, 18, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I028 Switch", AFE_CONN46, 28, 1, 0),
};

static const struct snd_kcontrol_new o047_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I019 Switch", AFE_CONN47, 19, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I029 Switch", AFE_CONN47, 29, 1, 0),
};

static const struct snd_kcontrol_new o182_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I024 Switch", AFE_CONN182, 24, 1, 0),
};

static const struct snd_kcontrol_new o183_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I025 Switch", AFE_CONN183, 25, 1, 0),
};

static const char * const dl8_dl11_data_sel_mux_text[] = {
	"dl8", "dl11",
};

static SOC_ENUM_SINGLE_DECL(dl8_dl11_data_sel_mux_enum,
	AFE_DAC_CON2, 0, dl8_dl11_data_sel_mux_text);

static const struct snd_kcontrol_new dl8_dl11_data_sel_mux =
	SOC_DAPM_ENUM("DL8_DL11 Sink", dl8_dl11_data_sel_mux_enum);

static const struct snd_soc_dapm_widget mt8195_memif_widgets[] = {
	/* DL6 */
	SND_SOC_DAPM_MIXER("I000", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I001", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* DL3 */
	SND_SOC_DAPM_MIXER("I020", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I021", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* DL11 */
	SND_SOC_DAPM_MIXER("I022", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I023", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I024", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I025", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I026", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I027", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I028", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I029", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I030", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I031", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I032", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I033", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I034", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I035", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I036", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I037", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I038", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I039", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I040", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I041", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I042", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I043", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I044", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I045", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* DL11/DL8 */
	SND_SOC_DAPM_MIXER("I046", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I047", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I048", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I049", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I050", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I051", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I052", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I053", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I054", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I055", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I056", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I057", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I058", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I059", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I060", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I061", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I062", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I063", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I064", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I065", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I066", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I067", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I068", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I069", SND_SOC_NOPM, 0, 0, NULL, 0),

	/* DL2 */
	SND_SOC_DAPM_MIXER("I070", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I071", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MUX("DL8_DL11 Mux",
			 SND_SOC_NOPM, 0, 0, &dl8_dl11_data_sel_mux),

	/* UL9 */
	SND_SOC_DAPM_MIXER("O002", SND_SOC_NOPM, 0, 0,
			   o002_mix, ARRAY_SIZE(o002_mix)),
	SND_SOC_DAPM_MIXER("O003", SND_SOC_NOPM, 0, 0,
			   o003_mix, ARRAY_SIZE(o003_mix)),
	SND_SOC_DAPM_MIXER("O004", SND_SOC_NOPM, 0, 0,
			   o004_mix, ARRAY_SIZE(o004_mix)),
	SND_SOC_DAPM_MIXER("O005", SND_SOC_NOPM, 0, 0,
			   o005_mix, ARRAY_SIZE(o005_mix)),
	SND_SOC_DAPM_MIXER("O006", SND_SOC_NOPM, 0, 0,
			   o006_mix, ARRAY_SIZE(o006_mix)),
	SND_SOC_DAPM_MIXER("O007", SND_SOC_NOPM, 0, 0,
			   o007_mix, ARRAY_SIZE(o007_mix)),
	SND_SOC_DAPM_MIXER("O008", SND_SOC_NOPM, 0, 0,
			   o008_mix, ARRAY_SIZE(o008_mix)),
	SND_SOC_DAPM_MIXER("O009", SND_SOC_NOPM, 0, 0,
			   o009_mix, ARRAY_SIZE(o009_mix)),
	SND_SOC_DAPM_MIXER("O010", SND_SOC_NOPM, 0, 0,
			   o010_mix, ARRAY_SIZE(o010_mix)),
	SND_SOC_DAPM_MIXER("O011", SND_SOC_NOPM, 0, 0,
			   o011_mix, ARRAY_SIZE(o011_mix)),
	SND_SOC_DAPM_MIXER("O012", SND_SOC_NOPM, 0, 0,
			   o012_mix, ARRAY_SIZE(o012_mix)),
	SND_SOC_DAPM_MIXER("O013", SND_SOC_NOPM, 0, 0,
			   o013_mix, ARRAY_SIZE(o013_mix)),
	SND_SOC_DAPM_MIXER("O014", SND_SOC_NOPM, 0, 0,
			   o014_mix, ARRAY_SIZE(o014_mix)),
	SND_SOC_DAPM_MIXER("O015", SND_SOC_NOPM, 0, 0,
			   o015_mix, ARRAY_SIZE(o015_mix)),
	SND_SOC_DAPM_MIXER("O016", SND_SOC_NOPM, 0, 0,
			   o016_mix, ARRAY_SIZE(o016_mix)),
	SND_SOC_DAPM_MIXER("O017", SND_SOC_NOPM, 0, 0,
			   o017_mix, ARRAY_SIZE(o017_mix)),
	SND_SOC_DAPM_MIXER("O018", SND_SOC_NOPM, 0, 0,
			   o018_mix, ARRAY_SIZE(o018_mix)),
	SND_SOC_DAPM_MIXER("O019", SND_SOC_NOPM, 0, 0,
			   o019_mix, ARRAY_SIZE(o019_mix)),
	SND_SOC_DAPM_MIXER("O020", SND_SOC_NOPM, 0, 0,
			   o020_mix, ARRAY_SIZE(o020_mix)),
	SND_SOC_DAPM_MIXER("O021", SND_SOC_NOPM, 0, 0,
			   o021_mix, ARRAY_SIZE(o021_mix)),
	SND_SOC_DAPM_MIXER("O022", SND_SOC_NOPM, 0, 0,
			   o022_mix, ARRAY_SIZE(o022_mix)),
	SND_SOC_DAPM_MIXER("O023", SND_SOC_NOPM, 0, 0,
			   o023_mix, ARRAY_SIZE(o023_mix)),
	SND_SOC_DAPM_MIXER("O024", SND_SOC_NOPM, 0, 0,
			   o024_mix, ARRAY_SIZE(o024_mix)),
	SND_SOC_DAPM_MIXER("O025", SND_SOC_NOPM, 0, 0,
			   o025_mix, ARRAY_SIZE(o025_mix)),
	SND_SOC_DAPM_MIXER("O026", SND_SOC_NOPM, 0, 0,
			   o026_mix, ARRAY_SIZE(o026_mix)),
	SND_SOC_DAPM_MIXER("O027", SND_SOC_NOPM, 0, 0,
			   o027_mix, ARRAY_SIZE(o027_mix)),
	SND_SOC_DAPM_MIXER("O028", SND_SOC_NOPM, 0, 0,
			   o028_mix, ARRAY_SIZE(o028_mix)),
	SND_SOC_DAPM_MIXER("O029", SND_SOC_NOPM, 0, 0,
			   o029_mix, ARRAY_SIZE(o029_mix)),
	SND_SOC_DAPM_MIXER("O030", SND_SOC_NOPM, 0, 0,
			   o030_mix, ARRAY_SIZE(o030_mix)),
	SND_SOC_DAPM_MIXER("O031", SND_SOC_NOPM, 0, 0,
			   o031_mix, ARRAY_SIZE(o031_mix)),
	SND_SOC_DAPM_MIXER("O032", SND_SOC_NOPM, 0, 0,
			   o032_mix, ARRAY_SIZE(o032_mix)),
	SND_SOC_DAPM_MIXER("O033", SND_SOC_NOPM, 0, 0,
			   o033_mix, ARRAY_SIZE(o033_mix)),

	/* UL4 */
	SND_SOC_DAPM_MIXER("O034", SND_SOC_NOPM, 0, 0,
			   o034_mix, ARRAY_SIZE(o034_mix)),
	SND_SOC_DAPM_MIXER("O035", SND_SOC_NOPM, 0, 0,
			   o035_mix, ARRAY_SIZE(o035_mix)),

	/* UL5 */
	SND_SOC_DAPM_MIXER("O036", SND_SOC_NOPM, 0, 0,
			   o036_mix, ARRAY_SIZE(o036_mix)),
	SND_SOC_DAPM_MIXER("O037", SND_SOC_NOPM, 0, 0,
			   o037_mix, ARRAY_SIZE(o037_mix)),

	/* UL10 */
	SND_SOC_DAPM_MIXER("O038", SND_SOC_NOPM, 0, 0,
			   o038_mix, ARRAY_SIZE(o038_mix)),
	SND_SOC_DAPM_MIXER("O039", SND_SOC_NOPM, 0, 0,
			   o039_mix, ARRAY_SIZE(o039_mix)),
	SND_SOC_DAPM_MIXER("O182", SND_SOC_NOPM, 0, 0,
			   o182_mix, ARRAY_SIZE(o182_mix)),
	SND_SOC_DAPM_MIXER("O183", SND_SOC_NOPM, 0, 0,
			   o183_mix, ARRAY_SIZE(o183_mix)),

	/* UL2 */
	SND_SOC_DAPM_MIXER("O040", SND_SOC_NOPM, 0, 0,
			   o040_mix, ARRAY_SIZE(o040_mix)),
	SND_SOC_DAPM_MIXER("O041", SND_SOC_NOPM, 0, 0,
			   o041_mix, ARRAY_SIZE(o041_mix)),
	SND_SOC_DAPM_MIXER("O042", SND_SOC_NOPM, 0, 0,
			   o042_mix, ARRAY_SIZE(o042_mix)),
	SND_SOC_DAPM_MIXER("O043", SND_SOC_NOPM, 0, 0,
			   o043_mix, ARRAY_SIZE(o043_mix)),
	SND_SOC_DAPM_MIXER("O044", SND_SOC_NOPM, 0, 0,
			   o044_mix, ARRAY_SIZE(o044_mix)),
	SND_SOC_DAPM_MIXER("O045", SND_SOC_NOPM, 0, 0,
			   o045_mix, ARRAY_SIZE(o045_mix)),
	SND_SOC_DAPM_MIXER("O046", SND_SOC_NOPM, 0, 0,
			   o046_mix, ARRAY_SIZE(o046_mix)),
	SND_SOC_DAPM_MIXER("O047", SND_SOC_NOPM, 0, 0,
			   o047_mix, ARRAY_SIZE(o047_mix)),
};

static const struct snd_soc_dapm_route mt8195_memif_routes[] = {
	{"I000", NULL, "DL6"},
	{"I001", NULL, "DL6"},

	{"I020", NULL, "DL3"},
	{"I021", NULL, "DL3"},

	{"I022", NULL, "DL11"},
	{"I023", NULL, "DL11"},
	{"I024", NULL, "DL11"},
	{"I025", NULL, "DL11"},
	{"I026", NULL, "DL11"},
	{"I027", NULL, "DL11"},
	{"I028", NULL, "DL11"},
	{"I029", NULL, "DL11"},
	{"I030", NULL, "DL11"},
	{"I031", NULL, "DL11"},
	{"I032", NULL, "DL11"},
	{"I033", NULL, "DL11"},
	{"I034", NULL, "DL11"},
	{"I035", NULL, "DL11"},
	{"I036", NULL, "DL11"},
	{"I037", NULL, "DL11"},
	{"I038", NULL, "DL11"},
	{"I039", NULL, "DL11"},
	{"I040", NULL, "DL11"},
	{"I041", NULL, "DL11"},
	{"I042", NULL, "DL11"},
	{"I043", NULL, "DL11"},
	{"I044", NULL, "DL11"},
	{"I045", NULL, "DL11"},

	{"DL8_DL11 Mux", "dl8", "DL8"},
	{"DL8_DL11 Mux", "dl11", "DL11"},

	{"I046", NULL, "DL8_DL11 Mux"},
	{"I047", NULL, "DL8_DL11 Mux"},
	{"I048", NULL, "DL8_DL11 Mux"},
	{"I049", NULL, "DL8_DL11 Mux"},
	{"I050", NULL, "DL8_DL11 Mux"},
	{"I051", NULL, "DL8_DL11 Mux"},
	{"I052", NULL, "DL8_DL11 Mux"},
	{"I053", NULL, "DL8_DL11 Mux"},
	{"I054", NULL, "DL8_DL11 Mux"},
	{"I055", NULL, "DL8_DL11 Mux"},
	{"I056", NULL, "DL8_DL11 Mux"},
	{"I057", NULL, "DL8_DL11 Mux"},
	{"I058", NULL, "DL8_DL11 Mux"},
	{"I059", NULL, "DL8_DL11 Mux"},
	{"I060", NULL, "DL8_DL11 Mux"},
	{"I061", NULL, "DL8_DL11 Mux"},
	{"I062", NULL, "DL8_DL11 Mux"},
	{"I063", NULL, "DL8_DL11 Mux"},
	{"I064", NULL, "DL8_DL11 Mux"},
	{"I065", NULL, "DL8_DL11 Mux"},
	{"I066", NULL, "DL8_DL11 Mux"},
	{"I067", NULL, "DL8_DL11 Mux"},
	{"I068", NULL, "DL8_DL11 Mux"},
	{"I069", NULL, "DL8_DL11 Mux"},

	{"I070", NULL, "DL2"},
	{"I071", NULL, "DL2"},

	{"UL9", NULL, "O002"},
	{"UL9", NULL, "O003"},
	{"UL9", NULL, "O004"},
	{"UL9", NULL, "O005"},
	{"UL9", NULL, "O006"},
	{"UL9", NULL, "O007"},
	{"UL9", NULL, "O008"},
	{"UL9", NULL, "O009"},
	{"UL9", NULL, "O010"},
	{"UL9", NULL, "O011"},
	{"UL9", NULL, "O012"},
	{"UL9", NULL, "O013"},
	{"UL9", NULL, "O014"},
	{"UL9", NULL, "O015"},
	{"UL9", NULL, "O016"},
	{"UL9", NULL, "O017"},
	{"UL9", NULL, "O018"},
	{"UL9", NULL, "O019"},
	{"UL9", NULL, "O020"},
	{"UL9", NULL, "O021"},
	{"UL9", NULL, "O022"},
	{"UL9", NULL, "O023"},
	{"UL9", NULL, "O024"},
	{"UL9", NULL, "O025"},
	{"UL9", NULL, "O026"},
	{"UL9", NULL, "O027"},
	{"UL9", NULL, "O028"},
	{"UL9", NULL, "O029"},
	{"UL9", NULL, "O030"},
	{"UL9", NULL, "O031"},
	{"UL9", NULL, "O032"},
	{"UL9", NULL, "O033"},

	{"UL4", NULL, "O034"},
	{"UL4", NULL, "O035"},

	{"UL5", NULL, "O036"},
	{"UL5", NULL, "O037"},

	{"UL10", NULL, "O038"},
	{"UL10", NULL, "O039"},
	{"UL10", NULL, "O182"},
	{"UL10", NULL, "O183"},

	{"UL2", NULL, "O040"},
	{"UL2", NULL, "O041"},
	{"UL2", NULL, "O042"},
	{"UL2", NULL, "O043"},
	{"UL2", NULL, "O044"},
	{"UL2", NULL, "O045"},
	{"UL2", NULL, "O046"},
	{"UL2", NULL, "O047"},

	{"O004", "I000 Switch", "I000"},
	{"O005", "I001 Switch", "I001"},

	{"O006", "I000 Switch", "I000"},
	{"O007", "I001 Switch", "I001"},

	{"O010", "I022 Switch", "I022"},
	{"O011", "I023 Switch", "I023"},
	{"O012", "I024 Switch", "I024"},
	{"O013", "I025 Switch", "I025"},
	{"O014", "I026 Switch", "I026"},
	{"O015", "I027 Switch", "I027"},
	{"O016", "I028 Switch", "I028"},
	{"O017", "I029 Switch", "I029"},

	{"O010", "I046 Switch", "I046"},
	{"O011", "I047 Switch", "I047"},
	{"O012", "I048 Switch", "I048"},
	{"O013", "I049 Switch", "I049"},
	{"O014", "I050 Switch", "I050"},
	{"O015", "I051 Switch", "I051"},
	{"O016", "I052 Switch", "I052"},
	{"O017", "I053 Switch", "I053"},
	{"O002", "I022 Switch", "I022"},
	{"O003", "I023 Switch", "I023"},
	{"O004", "I024 Switch", "I024"},
	{"O005", "I025 Switch", "I025"},
	{"O006", "I026 Switch", "I026"},
	{"O007", "I027 Switch", "I027"},
	{"O008", "I028 Switch", "I028"},
	{"O009", "I029 Switch", "I029"},
	{"O010", "I030 Switch", "I030"},
	{"O011", "I031 Switch", "I031"},
	{"O012", "I032 Switch", "I032"},
	{"O013", "I033 Switch", "I033"},
	{"O014", "I034 Switch", "I034"},
	{"O015", "I035 Switch", "I035"},
	{"O016", "I036 Switch", "I036"},
	{"O017", "I037 Switch", "I037"},
	{"O018", "I038 Switch", "I038"},
	{"O019", "I039 Switch", "I039"},
	{"O020", "I040 Switch", "I040"},
	{"O021", "I041 Switch", "I041"},
	{"O022", "I042 Switch", "I042"},
	{"O023", "I043 Switch", "I043"},
	{"O024", "I044 Switch", "I044"},
	{"O025", "I045 Switch", "I045"},
	{"O026", "I046 Switch", "I046"},
	{"O027", "I047 Switch", "I047"},
	{"O028", "I048 Switch", "I048"},
	{"O029", "I049 Switch", "I049"},
	{"O030", "I050 Switch", "I050"},
	{"O031", "I051 Switch", "I051"},
	{"O032", "I052 Switch", "I052"},
	{"O033", "I053 Switch", "I053"},

	{"O002", "I000 Switch", "I000"},
	{"O003", "I001 Switch", "I001"},
	{"O002", "I020 Switch", "I020"},
	{"O003", "I021 Switch", "I021"},
	{"O002", "I070 Switch", "I070"},
	{"O003", "I071 Switch", "I071"},

	{"O034", "I000 Switch", "I000"},
	{"O035", "I001 Switch", "I001"},
	{"O034", "I002 Switch", "I002"},
	{"O035", "I003 Switch", "I003"},
	{"O034", "I012 Switch", "I012"},
	{"O035", "I013 Switch", "I013"},
	{"O034", "I020 Switch", "I020"},
	{"O035", "I021 Switch", "I021"},
	{"O034", "I070 Switch", "I070"},
	{"O035", "I071 Switch", "I071"},
	{"O034", "I072 Switch", "I072"},
	{"O035", "I073 Switch", "I073"},

	{"O036", "I000 Switch", "I000"},
	{"O037", "I001 Switch", "I001"},
	{"O036", "I012 Switch", "I012"},
	{"O037", "I013 Switch", "I013"},
	{"O036", "I020 Switch", "I020"},
	{"O037", "I021 Switch", "I021"},
	{"O036", "I070 Switch", "I070"},
	{"O037", "I071 Switch", "I071"},
	{"O036", "I168 Switch", "I168"},
	{"O037", "I169 Switch", "I169"},

	{"O038", "I022 Switch", "I022"},
	{"O039", "I023 Switch", "I023"},
	{"O182", "I024 Switch", "I024"},
	{"O183", "I025 Switch", "I025"},

	{"O040", "I022 Switch", "I022"},
	{"O041", "I023 Switch", "I023"},
	{"O042", "I024 Switch", "I024"},
	{"O043", "I025 Switch", "I025"},
	{"O044", "I026 Switch", "I026"},
	{"O045", "I027 Switch", "I027"},
	{"O046", "I028 Switch", "I028"},
	{"O047", "I029 Switch", "I029"},

	{"O040", "I002 Switch", "I002"},
	{"O041", "I003 Switch", "I003"},
	{"O002", "I012 Switch", "I012"},
	{"O003", "I013 Switch", "I013"},
	{"O004", "I014 Switch", "I014"},
	{"O005", "I015 Switch", "I015"},
	{"O006", "I016 Switch", "I016"},
	{"O007", "I017 Switch", "I017"},
	{"O008", "I018 Switch", "I018"},
	{"O009", "I019 Switch", "I019"},

	{"O040", "I012 Switch", "I012"},
	{"O041", "I013 Switch", "I013"},
	{"O042", "I014 Switch", "I014"},
	{"O043", "I015 Switch", "I015"},
	{"O044", "I016 Switch", "I016"},
	{"O045", "I017 Switch", "I017"},
	{"O046", "I018 Switch", "I018"},
	{"O047", "I019 Switch", "I019"},

	{"O002", "I072 Switch", "I072"},
	{"O003", "I073 Switch", "I073"},
	{"O004", "I074 Switch", "I074"},
	{"O005", "I075 Switch", "I075"},
	{"O006", "I076 Switch", "I076"},
	{"O007", "I077 Switch", "I077"},
	{"O008", "I078 Switch", "I078"},
	{"O009", "I079 Switch", "I079"},

	{"O010", "I072 Switch", "I072"},
	{"O011", "I073 Switch", "I073"},
	{"O012", "I074 Switch", "I074"},
	{"O013", "I075 Switch", "I075"},
	{"O014", "I076 Switch", "I076"},
	{"O015", "I077 Switch", "I077"},
	{"O016", "I078 Switch", "I078"},
	{"O017", "I079 Switch", "I079"},
	{"O018", "I080 Switch", "I080"},
	{"O019", "I081 Switch", "I081"},
	{"O020", "I082 Switch", "I082"},
	{"O021", "I083 Switch", "I083"},
	{"O022", "I084 Switch", "I084"},
	{"O023", "I085 Switch", "I085"},
	{"O024", "I086 Switch", "I086"},
	{"O025", "I087 Switch", "I087"},
	{"O026", "I088 Switch", "I088"},
	{"O027", "I089 Switch", "I089"},
	{"O028", "I090 Switch", "I090"},
	{"O029", "I091 Switch", "I091"},
	{"O030", "I092 Switch", "I092"},
	{"O031", "I093 Switch", "I093"},
	{"O032", "I094 Switch", "I094"},
	{"O033", "I095 Switch", "I095"},

	{"O002", "I168 Switch", "I168"},
	{"O003", "I169 Switch", "I169"},
	{"O004", "I170 Switch", "I170"},
	{"O005", "I171 Switch", "I171"},

	{"O034", "I168 Switch", "I168"},
	{"O035", "I168 Switch", "I168"},
	{"O035", "I169 Switch", "I169"},

	{"O034", "I170 Switch", "I170"},
	{"O035", "I170 Switch", "I170"},
	{"O035", "I171 Switch", "I171"},

	{"O040", "I168 Switch", "I168"},
	{"O041", "I169 Switch", "I169"},
	{"O042", "I170 Switch", "I170"},
	{"O043", "I171 Switch", "I171"},
};

static const char * const mt8195_afe_1x_en_sel_text[] = {
	"a1sys_a2sys", "a3sys", "a4sys",
};

static const unsigned int mt8195_afe_1x_en_sel_values[] = {
	0, 1, 2,
};

static int mt8195_memif_1x_en_sel_put(struct snd_kcontrol *kcontrol,
				      struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	struct mt8195_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_memif_priv *memif_priv;
	unsigned int dai_id = kcontrol->id.device;
	long val = ucontrol->value.integer.value[0];
	int ret = 0;

	memif_priv = afe_priv->dai_priv[dai_id];

	if (val == memif_priv->asys_timing_sel)
		return 0;

	ret = snd_soc_put_enum_double(kcontrol, ucontrol);

	memif_priv->asys_timing_sel = val;

	return ret;
}

static int mt8195_asys_irq_1x_en_sel_put(struct snd_kcontrol *kcontrol,
					 struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_component *component =
		snd_soc_kcontrol_component(kcontrol);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	struct mt8195_afe_private *afe_priv = afe->platform_priv;
	unsigned int id = kcontrol->id.device;
	long val = ucontrol->value.integer.value[0];
	int ret = 0;

	if (val == afe_priv->irq_priv[id].asys_timing_sel)
		return 0;

	ret = snd_soc_put_enum_double(kcontrol, ucontrol);

	afe_priv->irq_priv[id].asys_timing_sel = val;

	return ret;
}

static SOC_VALUE_ENUM_SINGLE_DECL(dl2_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 18, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(dl3_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 20, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(dl6_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 22, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(dl7_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 24, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(dl8_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 26, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(dl10_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 28, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(dl11_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 30, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(ul1_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 0, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(ul2_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 2, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(ul3_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 4, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(ul4_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 6, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(ul5_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 8, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(ul6_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 10, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(ul8_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 12, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(ul9_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 14, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(ul10_1x_en_sel_enum,
			A3_A4_TIMING_SEL1, 16, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);

static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq1_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 0, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq2_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 2, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq3_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 4, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq4_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 6, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq5_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 8, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq6_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 10, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq7_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 12, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq8_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 14, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq9_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 16, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq10_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 18, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq11_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 20, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq12_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 22, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq13_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 24, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq14_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 26, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq15_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 28, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);
static SOC_VALUE_ENUM_SINGLE_DECL(asys_irq16_1x_en_sel_enum,
			A3_A4_TIMING_SEL6, 30, 0x3,
			mt8195_afe_1x_en_sel_text,
			mt8195_afe_1x_en_sel_values);

static const struct snd_kcontrol_new mt8195_memif_controls[] = {
	MT8195_SOC_ENUM_EXT("dl2_1x_en_sel",
			    dl2_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_DL2),
	MT8195_SOC_ENUM_EXT("dl3_1x_en_sel",
			    dl3_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_DL3),
	MT8195_SOC_ENUM_EXT("dl6_1x_en_sel",
			    dl6_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_DL6),
	MT8195_SOC_ENUM_EXT("dl7_1x_en_sel",
			    dl7_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_DL7),
	MT8195_SOC_ENUM_EXT("dl8_1x_en_sel",
			    dl8_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_DL8),
	MT8195_SOC_ENUM_EXT("dl10_1x_en_sel",
			    dl10_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_DL10),
	MT8195_SOC_ENUM_EXT("dl11_1x_en_sel",
			    dl11_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_DL11),
	MT8195_SOC_ENUM_EXT("ul1_1x_en_sel",
			    ul1_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_UL1),
	MT8195_SOC_ENUM_EXT("ul2_1x_en_sel",
			    ul2_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_UL2),
	MT8195_SOC_ENUM_EXT("ul3_1x_en_sel",
			    ul3_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_UL3),
	MT8195_SOC_ENUM_EXT("ul4_1x_en_sel",
			    ul4_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_UL4),
	MT8195_SOC_ENUM_EXT("ul5_1x_en_sel",
			    ul5_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_UL5),
	MT8195_SOC_ENUM_EXT("ul6_1x_en_sel",
			    ul6_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_UL6),
	MT8195_SOC_ENUM_EXT("ul8_1x_en_sel",
			    ul8_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_UL8),
	MT8195_SOC_ENUM_EXT("ul9_1x_en_sel",
			    ul9_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_UL9),
	MT8195_SOC_ENUM_EXT("ul10_1x_en_sel",
			    ul10_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_memif_1x_en_sel_put,
			    MT8195_AFE_MEMIF_UL10),
	MT8195_SOC_ENUM_EXT("asys_irq1_1x_en_sel",
			    asys_irq1_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_13),
	MT8195_SOC_ENUM_EXT("asys_irq2_1x_en_sel",
			    asys_irq2_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_14),
	MT8195_SOC_ENUM_EXT("asys_irq3_1x_en_sel",
			    asys_irq3_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_15),
	MT8195_SOC_ENUM_EXT("asys_irq4_1x_en_sel",
			    asys_irq4_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_16),
	MT8195_SOC_ENUM_EXT("asys_irq5_1x_en_sel",
			    asys_irq5_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_17),
	MT8195_SOC_ENUM_EXT("asys_irq6_1x_en_sel",
			    asys_irq6_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_18),
	MT8195_SOC_ENUM_EXT("asys_irq7_1x_en_sel",
			    asys_irq7_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_19),
	MT8195_SOC_ENUM_EXT("asys_irq8_1x_en_sel",
			    asys_irq8_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_20),
	MT8195_SOC_ENUM_EXT("asys_irq9_1x_en_sel",
			    asys_irq9_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_21),
	MT8195_SOC_ENUM_EXT("asys_irq10_1x_en_sel",
			    asys_irq10_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_22),
	MT8195_SOC_ENUM_EXT("asys_irq11_1x_en_sel",
			    asys_irq11_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_23),
	MT8195_SOC_ENUM_EXT("asys_irq12_1x_en_sel",
			    asys_irq12_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_24),
	MT8195_SOC_ENUM_EXT("asys_irq13_1x_en_sel",
			    asys_irq13_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_25),
	MT8195_SOC_ENUM_EXT("asys_irq14_1x_en_sel",
			    asys_irq14_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_26),
	MT8195_SOC_ENUM_EXT("asys_irq15_1x_en_sel",
			    asys_irq15_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_27),
	MT8195_SOC_ENUM_EXT("asys_irq16_1x_en_sel",
			    asys_irq16_1x_en_sel_enum,
			    snd_soc_get_enum_double,
			    mt8195_asys_irq_1x_en_sel_put,
			    MT8195_AFE_IRQ_28),
};

static const struct snd_soc_component_driver mt8195_afe_pcm_dai_component = {
	.name = "mt8195-afe-pcm-dai",
};

static const struct mtk_base_memif_data memif_data[MT8195_AFE_MEMIF_NUM] = {
	[MT8195_AFE_MEMIF_DL2] = {
		.name = "DL2",
		.id = MT8195_AFE_MEMIF_DL2,
		.reg_ofs_base = AFE_DL2_BASE,
		.reg_ofs_cur = AFE_DL2_CUR,
		.reg_ofs_end = AFE_DL2_END,
		.fs_reg = AFE_MEMIF_AGENT_FS_CON0,
		.fs_shift = 10,
		.fs_maskbit = 0x1f,
		.mono_reg = -1,
		.mono_shift = 0,
		.int_odd_flag_reg = -1,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 18,
		.hd_reg = AFE_DL2_CON0,
		.hd_shift = 5,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 18,
		.ch_num_reg = AFE_DL2_CON0,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0x1f,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 18,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 18,
	},
	[MT8195_AFE_MEMIF_DL3] = {
		.name = "DL3",
		.id = MT8195_AFE_MEMIF_DL3,
		.reg_ofs_base = AFE_DL3_BASE,
		.reg_ofs_cur = AFE_DL3_CUR,
		.reg_ofs_end = AFE_DL3_END,
		.fs_reg = AFE_MEMIF_AGENT_FS_CON0,
		.fs_shift = 15,
		.fs_maskbit = 0x1f,
		.mono_reg = -1,
		.mono_shift = 0,
		.int_odd_flag_reg = -1,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 19,
		.hd_reg = AFE_DL3_CON0,
		.hd_shift = 5,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 19,
		.ch_num_reg = AFE_DL3_CON0,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0x1f,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 19,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 19,
	},
	[MT8195_AFE_MEMIF_DL6] = {
		.name = "DL6",
		.id = MT8195_AFE_MEMIF_DL6,
		.reg_ofs_base = AFE_DL6_BASE,
		.reg_ofs_cur = AFE_DL6_CUR,
		.reg_ofs_end = AFE_DL6_END,
		.fs_reg = AFE_MEMIF_AGENT_FS_CON1,
		.fs_shift = 0,
		.fs_maskbit = 0x1f,
		.mono_reg = -1,
		.mono_shift = 0,
		.int_odd_flag_reg = -1,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 22,
		.hd_reg = AFE_DL6_CON0,
		.hd_shift = 5,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 22,
		.ch_num_reg = AFE_DL6_CON0,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0x1f,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 22,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 22,
	},
	[MT8195_AFE_MEMIF_DL7] = {
		.name = "DL7",
		.id = MT8195_AFE_MEMIF_DL7,
		.reg_ofs_base = AFE_DL7_BASE,
		.reg_ofs_cur = AFE_DL7_CUR,
		.reg_ofs_end = AFE_DL7_END,
		.fs_reg = -1,
		.fs_shift = 0,
		.fs_maskbit = 0,
		.mono_reg = -1,
		.mono_shift = 0,
		.int_odd_flag_reg = -1,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 23,
		.hd_reg = AFE_DL7_CON0,
		.hd_shift = 5,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 23,
		.ch_num_reg = AFE_DL7_CON0,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0x1f,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 23,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 23,
	},
	[MT8195_AFE_MEMIF_DL8] = {
		.name = "DL8",
		.id = MT8195_AFE_MEMIF_DL8,
		.reg_ofs_base = AFE_DL8_BASE,
		.reg_ofs_cur = AFE_DL8_CUR,
		.reg_ofs_end = AFE_DL8_END,
		.fs_reg = AFE_MEMIF_AGENT_FS_CON1,
		.fs_shift = 10,
		.fs_maskbit = 0x1f,
		.mono_reg = -1,
		.mono_shift = 0,
		.int_odd_flag_reg = -1,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 24,
		.hd_reg = AFE_DL8_CON0,
		.hd_shift = 6,
		.agent_disable_reg = -1,
		.agent_disable_shift = 0,
		.ch_num_reg = AFE_DL8_CON0,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0x3f,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 24,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 24,
	},
	[MT8195_AFE_MEMIF_DL10] = {
		.name = "DL10",
		.id = MT8195_AFE_MEMIF_DL10,
		.reg_ofs_base = AFE_DL10_BASE,
		.reg_ofs_cur = AFE_DL10_CUR,
		.reg_ofs_end = AFE_DL10_END,
		.fs_reg = AFE_MEMIF_AGENT_FS_CON1,
		.fs_shift = 20,
		.fs_maskbit = 0x1f,
		.mono_reg = -1,
		.mono_shift = 0,
		.int_odd_flag_reg = -1,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 26,
		.hd_reg = AFE_DL10_CON0,
		.hd_shift = 5,
		.agent_disable_reg = -1,
		.agent_disable_shift = 0,
		.ch_num_reg = AFE_DL10_CON0,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0x1f,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 26,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 26,
	},
	[MT8195_AFE_MEMIF_DL11] = {
		.name = "DL11",
		.id = MT8195_AFE_MEMIF_DL11,
		.reg_ofs_base = AFE_DL11_BASE,
		.reg_ofs_cur = AFE_DL11_CUR,
		.reg_ofs_end = AFE_DL11_END,
		.fs_reg = AFE_MEMIF_AGENT_FS_CON1,
		.fs_shift = 25,
		.fs_maskbit = 0x1f,
		.mono_reg = -1,
		.mono_shift = 0,
		.int_odd_flag_reg = -1,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 27,
		.hd_reg = AFE_DL11_CON0,
		.hd_shift = 7,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 27,
		.ch_num_reg = AFE_DL11_CON0,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0x7f,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 27,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 27,
	},
	[MT8195_AFE_MEMIF_UL1] = {
		.name = "UL1",
		.id = MT8195_AFE_MEMIF_UL1,
		.reg_ofs_base = AFE_UL1_BASE,
		.reg_ofs_cur = AFE_UL1_CUR,
		.reg_ofs_end = AFE_UL1_END,
		.fs_reg = -1,
		.fs_shift = 0,
		.fs_maskbit = 0,
		.mono_reg = AFE_UL1_CON0,
		.mono_shift = 1,
		.int_odd_flag_reg = AFE_UL1_CON0,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 1,
		.hd_reg = AFE_UL1_CON0,
		.hd_shift = 5,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 0,
		.ch_num_reg = -1,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 0,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 0,
	},
	[MT8195_AFE_MEMIF_UL2] = {
		.name = "UL2",
		.id = MT8195_AFE_MEMIF_UL2,
		.reg_ofs_base = AFE_UL2_BASE,
		.reg_ofs_cur = AFE_UL2_CUR,
		.reg_ofs_end = AFE_UL2_END,
		.fs_reg = AFE_MEMIF_AGENT_FS_CON2,
		.fs_shift = 5,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_UL2_CON0,
		.mono_shift = 1,
		.int_odd_flag_reg = AFE_UL2_CON0,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 2,
		.hd_reg = AFE_UL2_CON0,
		.hd_shift = 5,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 1,
		.ch_num_reg = -1,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 1,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 1,
	},
	[MT8195_AFE_MEMIF_UL3] = {
		.name = "UL3",
		.id = MT8195_AFE_MEMIF_UL3,
		.reg_ofs_base = AFE_UL3_BASE,
		.reg_ofs_cur = AFE_UL3_CUR,
		.reg_ofs_end = AFE_UL3_END,
		.fs_reg = AFE_MEMIF_AGENT_FS_CON2,
		.fs_shift = 10,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_UL3_CON0,
		.mono_shift = 1,
		.int_odd_flag_reg = AFE_UL3_CON0,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 3,
		.hd_reg = AFE_UL3_CON0,
		.hd_shift = 5,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 2,
		.ch_num_reg = -1,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 2,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 2,
	},
	[MT8195_AFE_MEMIF_UL4] = {
		.name = "UL4",
		.id = MT8195_AFE_MEMIF_UL4,
		.reg_ofs_base = AFE_UL4_BASE,
		.reg_ofs_cur = AFE_UL4_CUR,
		.reg_ofs_end = AFE_UL4_END,
		.fs_reg = AFE_MEMIF_AGENT_FS_CON2,
		.fs_shift = 15,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_UL4_CON0,
		.mono_shift = 1,
		.int_odd_flag_reg = AFE_UL4_CON0,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 4,
		.hd_reg = AFE_UL4_CON0,
		.hd_shift = 5,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 3,
		.ch_num_reg = -1,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 3,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 3,
	},
	[MT8195_AFE_MEMIF_UL5] = {
		.name = "UL5",
		.id = MT8195_AFE_MEMIF_UL5,
		.reg_ofs_base = AFE_UL5_BASE,
		.reg_ofs_cur = AFE_UL5_CUR,
		.reg_ofs_end = AFE_UL5_END,
		.fs_reg = AFE_MEMIF_AGENT_FS_CON2,
		.fs_shift = 20,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_UL5_CON0,
		.mono_shift = 1,
		.int_odd_flag_reg = AFE_UL5_CON0,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 5,
		.hd_reg = AFE_UL5_CON0,
		.hd_shift = 5,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 4,
		.ch_num_reg = -1,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 4,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 4,
	},
	[MT8195_AFE_MEMIF_UL6] = {
		.name = "UL6",
		.id = MT8195_AFE_MEMIF_UL6,
		.reg_ofs_base = AFE_UL6_BASE,
		.reg_ofs_cur = AFE_UL6_CUR,
		.reg_ofs_end = AFE_UL6_END,
		.fs_reg = -1,
		.fs_shift = 0,
		.fs_maskbit = 0,
		.mono_reg = AFE_UL6_CON0,
		.mono_shift = 1,
		.int_odd_flag_reg = AFE_UL6_CON0,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 6,
		.hd_reg = AFE_UL6_CON0,
		.hd_shift = 5,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 5,
		.ch_num_reg = -1,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 5,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 5,
	},
	[MT8195_AFE_MEMIF_UL8] = {
		.name = "UL8",
		.id = MT8195_AFE_MEMIF_UL8,
		.reg_ofs_base = AFE_UL8_BASE,
		.reg_ofs_cur = AFE_UL8_CUR,
		.reg_ofs_end = AFE_UL8_END,
		.fs_reg = AFE_MEMIF_AGENT_FS_CON3,
		.fs_shift = 5,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_UL8_CON0,
		.mono_shift = 1,
		.int_odd_flag_reg = AFE_UL8_CON0,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 8,
		.hd_reg = AFE_UL8_CON0,
		.hd_shift = 5,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 7,
		.ch_num_reg = -1,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 7,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 7,
	},
	[MT8195_AFE_MEMIF_UL9] = {
		.name = "UL9",
		.id = MT8195_AFE_MEMIF_UL9,
		.reg_ofs_base = AFE_UL9_BASE,
		.reg_ofs_cur = AFE_UL9_CUR,
		.reg_ofs_end = AFE_UL9_END,
		.fs_reg = AFE_MEMIF_AGENT_FS_CON3,
		.fs_shift = 10,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_UL9_CON0,
		.mono_shift = 1,
		.int_odd_flag_reg = AFE_UL9_CON0,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 9,
		.hd_reg = AFE_UL9_CON0,
		.hd_shift = 5,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 8,
		.ch_num_reg = -1,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 8,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 8,
	},
	[MT8195_AFE_MEMIF_UL10] = {
		.name = "UL10",
		.id = MT8195_AFE_MEMIF_UL10,
		.reg_ofs_base = AFE_UL10_BASE,
		.reg_ofs_cur = AFE_UL10_CUR,
		.reg_ofs_end = AFE_UL10_END,
		.fs_reg = AFE_MEMIF_AGENT_FS_CON3,
		.fs_shift = 15,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_UL10_CON0,
		.mono_shift = 1,
		.int_odd_flag_reg = AFE_UL10_CON0,
		.int_odd_flag_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 10,
		.hd_reg = AFE_UL10_CON0,
		.hd_shift = 5,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 9,
		.ch_num_reg = -1,
		.ch_num_shift = 0,
		.ch_num_maskbit = 0,
		.msb_reg = AFE_NORMAL_BASE_ADR_MSB,
		.msb_shift = 9,
		.msb_end_reg = AFE_NORMAL_END_ADR_MSB,
		.msb_end_shift = 9,
	},
};

static const struct mtk_base_irq_data irq_data_array[MT8195_AFE_IRQ_NUM] = {
	[MT8195_AFE_IRQ_1] = {
		.id = MT8195_AFE_IRQ_1,
		.irq_cnt_reg = -1,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0,
		.irq_fs_reg = -1,
		.irq_fs_shift = 0,
		.irq_fs_maskbit = 0,
		.irq_en_reg = AFE_IRQ1_CON,
		.irq_en_shift = 31,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 0,
		.irq_status_shift = 16,
	},
	[MT8195_AFE_IRQ_2] = {
		.id = MT8195_AFE_IRQ_2,
		.irq_cnt_reg = -1,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0,
		.irq_fs_reg = -1,
		.irq_fs_shift = 0,
		.irq_fs_maskbit = 0,
		.irq_en_reg = AFE_IRQ2_CON,
		.irq_en_shift = 31,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 1,
		.irq_status_shift = 17,
	},
	[MT8195_AFE_IRQ_3] = {
		.id = MT8195_AFE_IRQ_3,
		.irq_cnt_reg = AFE_IRQ3_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = -1,
		.irq_fs_shift = 0,
		.irq_fs_maskbit = 0,
		.irq_en_reg = AFE_IRQ3_CON,
		.irq_en_shift = 31,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 2,
		.irq_status_shift = 18,
	},
	[MT8195_AFE_IRQ_8] = {
		.id = MT8195_AFE_IRQ_8,
		.irq_cnt_reg = -1,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0,
		.irq_fs_reg = -1,
		.irq_fs_shift = 0,
		.irq_fs_maskbit = 0,
		.irq_en_reg = AFE_IRQ8_CON,
		.irq_en_shift = 31,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 7,
		.irq_status_shift = 23,
	},
	[MT8195_AFE_IRQ_9] = {
		.id = MT8195_AFE_IRQ_9,
		.irq_cnt_reg = AFE_IRQ9_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = -1,
		.irq_fs_shift = 0,
		.irq_fs_maskbit = 0,
		.irq_en_reg = AFE_IRQ9_CON,
		.irq_en_shift = 31,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 8,
		.irq_status_shift = 24,
	},
	[MT8195_AFE_IRQ_10] = {
		.id = MT8195_AFE_IRQ_10,
		.irq_cnt_reg = -1,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0,
		.irq_fs_reg = -1,
		.irq_fs_shift = 0,
		.irq_fs_maskbit = 0,
		.irq_en_reg = AFE_IRQ10_CON,
		.irq_en_shift = 31,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 9,
		.irq_status_shift = 25,
	},
	[MT8195_AFE_IRQ_13] = {
		.id = MT8195_AFE_IRQ_13,
		.irq_cnt_reg = ASYS_IRQ1_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ1_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ1_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 0,
		.irq_status_shift = 0,
	},
	[MT8195_AFE_IRQ_14] = {
		.id = MT8195_AFE_IRQ_14,
		.irq_cnt_reg = ASYS_IRQ2_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ2_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ2_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 1,
		.irq_status_shift = 1,
	},
	[MT8195_AFE_IRQ_15] = {
		.id = MT8195_AFE_IRQ_15,
		.irq_cnt_reg = ASYS_IRQ3_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ3_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ3_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 2,
		.irq_status_shift = 2,
	},
	[MT8195_AFE_IRQ_16] = {
		.id = MT8195_AFE_IRQ_16,
		.irq_cnt_reg = ASYS_IRQ4_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ4_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ4_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 3,
		.irq_status_shift = 3,
	},
	[MT8195_AFE_IRQ_17] = {
		.id = MT8195_AFE_IRQ_17,
		.irq_cnt_reg = ASYS_IRQ5_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ5_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ5_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 4,
		.irq_status_shift = 4,
	},
	[MT8195_AFE_IRQ_18] = {
		.id = MT8195_AFE_IRQ_18,
		.irq_cnt_reg = ASYS_IRQ6_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ6_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ6_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 5,
		.irq_status_shift = 5,
	},
	[MT8195_AFE_IRQ_19] = {
		.id = MT8195_AFE_IRQ_19,
		.irq_cnt_reg = ASYS_IRQ7_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ7_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ7_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 6,
		.irq_status_shift = 6,
	},
	[MT8195_AFE_IRQ_20] = {
		.id = MT8195_AFE_IRQ_20,
		.irq_cnt_reg = ASYS_IRQ8_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ8_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ8_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 7,
		.irq_status_shift = 7,
	},
	[MT8195_AFE_IRQ_21] = {
		.id = MT8195_AFE_IRQ_21,
		.irq_cnt_reg = ASYS_IRQ9_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ9_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ9_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 8,
		.irq_status_shift = 8,
	},
	[MT8195_AFE_IRQ_22] = {
		.id = MT8195_AFE_IRQ_22,
		.irq_cnt_reg = ASYS_IRQ10_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ10_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ10_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 9,
		.irq_status_shift = 9,
	},
	[MT8195_AFE_IRQ_23] = {
		.id = MT8195_AFE_IRQ_23,
		.irq_cnt_reg = ASYS_IRQ11_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ11_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ11_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 10,
		.irq_status_shift = 10,
	},
	[MT8195_AFE_IRQ_24] = {
		.id = MT8195_AFE_IRQ_24,
		.irq_cnt_reg = ASYS_IRQ12_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ12_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ12_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 11,
		.irq_status_shift = 11,
	},
	[MT8195_AFE_IRQ_25] = {
		.id = MT8195_AFE_IRQ_25,
		.irq_cnt_reg = ASYS_IRQ13_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ13_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ13_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 12,
		.irq_status_shift = 12,
	},
	[MT8195_AFE_IRQ_26] = {
		.id = MT8195_AFE_IRQ_26,
		.irq_cnt_reg = ASYS_IRQ14_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ14_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ14_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 13,
		.irq_status_shift = 13,
	},
	[MT8195_AFE_IRQ_27] = {
		.id = MT8195_AFE_IRQ_27,
		.irq_cnt_reg = ASYS_IRQ15_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ15_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ15_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 14,
		.irq_status_shift = 14,
	},
	[MT8195_AFE_IRQ_28] = {
		.id = MT8195_AFE_IRQ_28,
		.irq_cnt_reg = ASYS_IRQ16_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ16_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1ffff,
		.irq_en_reg = ASYS_IRQ16_CON,
		.irq_en_shift = 31,
		.irq_clr_reg =  ASYS_IRQ_CLR,
		.irq_clr_shift = 15,
		.irq_status_shift = 15,
	},
};

static const int mt8195_afe_memif_const_irqs[MT8195_AFE_MEMIF_NUM] = {
	[MT8195_AFE_MEMIF_DL2] = MT8195_AFE_IRQ_13,
	[MT8195_AFE_MEMIF_DL3] = MT8195_AFE_IRQ_14,
	[MT8195_AFE_MEMIF_DL6] = MT8195_AFE_IRQ_15,
	[MT8195_AFE_MEMIF_DL7] = MT8195_AFE_IRQ_1,
	[MT8195_AFE_MEMIF_DL8] = MT8195_AFE_IRQ_16,
	[MT8195_AFE_MEMIF_DL10] = MT8195_AFE_IRQ_17,
	[MT8195_AFE_MEMIF_DL11] = MT8195_AFE_IRQ_18,
	[MT8195_AFE_MEMIF_UL1] = MT8195_AFE_IRQ_3,
	[MT8195_AFE_MEMIF_UL2] = MT8195_AFE_IRQ_19,
	[MT8195_AFE_MEMIF_UL3] = MT8195_AFE_IRQ_20,
	[MT8195_AFE_MEMIF_UL4] = MT8195_AFE_IRQ_21,
	[MT8195_AFE_MEMIF_UL5] = MT8195_AFE_IRQ_22,
	[MT8195_AFE_MEMIF_UL6] = MT8195_AFE_IRQ_9,
	[MT8195_AFE_MEMIF_UL8] = MT8195_AFE_IRQ_23,
	[MT8195_AFE_MEMIF_UL9] = MT8195_AFE_IRQ_24,
	[MT8195_AFE_MEMIF_UL10] = MT8195_AFE_IRQ_25,
};

static bool mt8195_is_volatile_reg(struct device *dev, unsigned int reg)
{
	/* these auto-gen reg has read-only bit, so put it as volatile */
	/* volatile reg cannot be cached, so cannot be set when power off */
	switch (reg) {
	case AUDIO_TOP_CON0:
	case AUDIO_TOP_CON1:
	case AUDIO_TOP_CON3:
	case AUDIO_TOP_CON4:
	case AUDIO_TOP_CON5:
	case AUDIO_TOP_CON6:
	case ASYS_IRQ_CLR:
	case ASYS_IRQ_STATUS:
	case ASYS_IRQ_MON1:
	case ASYS_IRQ_MON2:
	case AFE_IRQ_MCU_CLR:
	case AFE_IRQ_STATUS:
	case AFE_IRQ3_CON_MON:
	case AFE_IRQ_MCU_MON2:
	case ADSP_IRQ_STATUS:
	case AUDIO_TOP_STA0:
	case AUDIO_TOP_STA1:
	case AFE_GAIN1_CUR:
	case AFE_GAIN2_CUR:
	case AFE_IEC_BURST_INFO:
	case AFE_IEC_CHL_STAT0:
	case AFE_IEC_CHL_STAT1:
	case AFE_IEC_CHR_STAT0:
	case AFE_IEC_CHR_STAT1:
	case AFE_SPDIFIN_CHSTS1:
	case AFE_SPDIFIN_CHSTS2:
	case AFE_SPDIFIN_CHSTS3:
	case AFE_SPDIFIN_CHSTS4:
	case AFE_SPDIFIN_CHSTS5:
	case AFE_SPDIFIN_CHSTS6:
	case AFE_SPDIFIN_DEBUG1:
	case AFE_SPDIFIN_DEBUG2:
	case AFE_SPDIFIN_DEBUG3:
	case AFE_SPDIFIN_DEBUG4:
	case AFE_SPDIFIN_EC:
	case AFE_SPDIFIN_CKLOCK_CFG:
	case AFE_SPDIFIN_BR_DBG1:
	case AFE_SPDIFIN_CKFBDIV:
	case AFE_SPDIFIN_INT_EXT:
	case AFE_SPDIFIN_INT_EXT2:
	case SPDIFIN_FREQ_STATUS:
	case SPDIFIN_USERCODE1:
	case SPDIFIN_USERCODE2:
	case SPDIFIN_USERCODE3:
	case SPDIFIN_USERCODE4:
	case SPDIFIN_USERCODE5:
	case SPDIFIN_USERCODE6:
	case SPDIFIN_USERCODE7:
	case SPDIFIN_USERCODE8:
	case SPDIFIN_USERCODE9:
	case SPDIFIN_USERCODE10:
	case SPDIFIN_USERCODE11:
	case SPDIFIN_USERCODE12:
	case AFE_LINEIN_APLL_TUNER_MON:
	case AFE_EARC_APLL_TUNER_MON:
	case AFE_CM0_MON:
	case AFE_CM1_MON:
	case AFE_CM2_MON:
	case AFE_MPHONE_MULTI_DET_MON0:
	case AFE_MPHONE_MULTI_DET_MON1:
	case AFE_MPHONE_MULTI_DET_MON2:
	case AFE_MPHONE_MULTI2_DET_MON0:
	case AFE_MPHONE_MULTI2_DET_MON1:
	case AFE_MPHONE_MULTI2_DET_MON2:
	case AFE_ADDA_MTKAIF_MON0:
	case AFE_ADDA_MTKAIF_MON1:
	case AFE_AUD_PAD_TOP:
	case AFE_ADDA6_MTKAIF_MON0:
	case AFE_ADDA6_MTKAIF_MON1:
	case AFE_ADDA6_SRC_DEBUG_MON0:
	case AFE_ADDA6_UL_SRC_MON0:
	case AFE_ADDA6_UL_SRC_MON1:
	case AFE_ASRC11_NEW_CON8:
	case AFE_ASRC11_NEW_CON9:
	case AFE_ASRC12_NEW_CON8:
	case AFE_ASRC12_NEW_CON9:
	case AFE_LRCK_CNT:
	case AFE_DAC_MON0:
	case AFE_DL2_CUR:
	case AFE_DL3_CUR:
	case AFE_DL6_CUR:
	case AFE_DL7_CUR:
	case AFE_DL8_CUR:
	case AFE_DL10_CUR:
	case AFE_DL11_CUR:
	case AFE_UL1_CUR:
	case AFE_UL2_CUR:
	case AFE_UL3_CUR:
	case AFE_UL4_CUR:
	case AFE_UL5_CUR:
	case AFE_UL6_CUR:
	case AFE_UL8_CUR:
	case AFE_UL9_CUR:
	case AFE_UL10_CUR:
	case AFE_DL8_CHK_SUM1:
	case AFE_DL8_CHK_SUM2:
	case AFE_DL8_CHK_SUM3:
	case AFE_DL8_CHK_SUM4:
	case AFE_DL8_CHK_SUM5:
	case AFE_DL8_CHK_SUM6:
	case AFE_DL10_CHK_SUM1:
	case AFE_DL10_CHK_SUM2:
	case AFE_DL10_CHK_SUM3:
	case AFE_DL10_CHK_SUM4:
	case AFE_DL10_CHK_SUM5:
	case AFE_DL10_CHK_SUM6:
	case AFE_DL11_CHK_SUM1:
	case AFE_DL11_CHK_SUM2:
	case AFE_DL11_CHK_SUM3:
	case AFE_DL11_CHK_SUM4:
	case AFE_DL11_CHK_SUM5:
	case AFE_DL11_CHK_SUM6:
	case AFE_UL1_CHK_SUM1:
	case AFE_UL1_CHK_SUM2:
	case AFE_UL2_CHK_SUM1:
	case AFE_UL2_CHK_SUM2:
	case AFE_UL3_CHK_SUM1:
	case AFE_UL3_CHK_SUM2:
	case AFE_UL4_CHK_SUM1:
	case AFE_UL4_CHK_SUM2:
	case AFE_UL5_CHK_SUM1:
	case AFE_UL5_CHK_SUM2:
	case AFE_UL6_CHK_SUM1:
	case AFE_UL6_CHK_SUM2:
	case AFE_UL8_CHK_SUM1:
	case AFE_UL8_CHK_SUM2:
	case AFE_DL2_CHK_SUM1:
	case AFE_DL2_CHK_SUM2:
	case AFE_DL3_CHK_SUM1:
	case AFE_DL3_CHK_SUM2:
	case AFE_DL6_CHK_SUM1:
	case AFE_DL6_CHK_SUM2:
	case AFE_DL7_CHK_SUM1:
	case AFE_DL7_CHK_SUM2:
	case AFE_UL9_CHK_SUM1:
	case AFE_UL9_CHK_SUM2:
	case AFE_BUS_MON1:
	case UL1_MOD2AGT_CNT_LAT:
	case UL2_MOD2AGT_CNT_LAT:
	case UL3_MOD2AGT_CNT_LAT:
	case UL4_MOD2AGT_CNT_LAT:
	case UL5_MOD2AGT_CNT_LAT:
	case UL6_MOD2AGT_CNT_LAT:
	case UL8_MOD2AGT_CNT_LAT:
	case UL9_MOD2AGT_CNT_LAT:
	case UL10_MOD2AGT_CNT_LAT:
	case AFE_MEMIF_BUF_FULL_MON:
	case AFE_MEMIF_BUF_MON1:
	case AFE_MEMIF_BUF_MON3:
	case AFE_MEMIF_BUF_MON4:
	case AFE_MEMIF_BUF_MON5:
	case AFE_MEMIF_BUF_MON6:
	case AFE_MEMIF_BUF_MON7:
	case AFE_MEMIF_BUF_MON8:
	case AFE_MEMIF_BUF_MON9:
	case AFE_MEMIF_BUF_MON10:
	case DL2_AGENT2MODULE_CNT:
	case DL3_AGENT2MODULE_CNT:
	case DL6_AGENT2MODULE_CNT:
	case DL7_AGENT2MODULE_CNT:
	case DL8_AGENT2MODULE_CNT:
	case DL10_AGENT2MODULE_CNT:
	case DL11_AGENT2MODULE_CNT:
	case UL1_MODULE2AGENT_CNT:
	case UL2_MODULE2AGENT_CNT:
	case UL3_MODULE2AGENT_CNT:
	case UL4_MODULE2AGENT_CNT:
	case UL5_MODULE2AGENT_CNT:
	case UL6_MODULE2AGENT_CNT:
	case UL8_MODULE2AGENT_CNT:
	case UL9_MODULE2AGENT_CNT:
	case UL10_MODULE2AGENT_CNT:
	case AFE_DMIC0_SRC_DEBUG_MON0:
	case AFE_DMIC0_UL_SRC_MON0:
	case AFE_DMIC0_UL_SRC_MON1:
	case AFE_DMIC1_SRC_DEBUG_MON0:
	case AFE_DMIC1_UL_SRC_MON0:
	case AFE_DMIC1_UL_SRC_MON1:
	case AFE_DMIC2_SRC_DEBUG_MON0:
	case AFE_DMIC2_UL_SRC_MON0:
	case AFE_DMIC2_UL_SRC_MON1:
	case AFE_DMIC3_SRC_DEBUG_MON0:
	case AFE_DMIC3_UL_SRC_MON0:
	case AFE_DMIC3_UL_SRC_MON1:
	case DMIC_GAIN1_CUR:
	case DMIC_GAIN2_CUR:
	case DMIC_GAIN3_CUR:
	case DMIC_GAIN4_CUR:
	case ETDM_IN1_MONITOR:
	case ETDM_IN2_MONITOR:
	case ETDM_OUT1_MONITOR:
	case ETDM_OUT2_MONITOR:
	case ETDM_OUT3_MONITOR:
	case AFE_ADDA_SRC_DEBUG_MON0:
	case AFE_ADDA_SRC_DEBUG_MON1:
	case AFE_ADDA_DL_SDM_FIFO_MON:
	case AFE_ADDA_DL_SRC_LCH_MON:
	case AFE_ADDA_DL_SRC_RCH_MON:
	case AFE_ADDA_DL_SDM_OUT_MON:
	case AFE_GASRC0_NEW_CON8:
	case AFE_GASRC0_NEW_CON9:
	case AFE_GASRC0_NEW_CON12:
	case AFE_GASRC1_NEW_CON8:
	case AFE_GASRC1_NEW_CON9:
	case AFE_GASRC1_NEW_CON12:
	case AFE_GASRC2_NEW_CON8:
	case AFE_GASRC2_NEW_CON9:
	case AFE_GASRC2_NEW_CON12:
	case AFE_GASRC3_NEW_CON8:
	case AFE_GASRC3_NEW_CON9:
	case AFE_GASRC3_NEW_CON12:
	case AFE_GASRC4_NEW_CON8:
	case AFE_GASRC4_NEW_CON9:
	case AFE_GASRC4_NEW_CON12:
	case AFE_GASRC5_NEW_CON8:
	case AFE_GASRC5_NEW_CON9:
	case AFE_GASRC5_NEW_CON12:
	case AFE_GASRC6_NEW_CON8:
	case AFE_GASRC6_NEW_CON9:
	case AFE_GASRC6_NEW_CON12:
	case AFE_GASRC7_NEW_CON8:
	case AFE_GASRC7_NEW_CON9:
	case AFE_GASRC7_NEW_CON12:
	case AFE_GASRC8_NEW_CON8:
	case AFE_GASRC8_NEW_CON9:
	case AFE_GASRC8_NEW_CON12:
	case AFE_GASRC9_NEW_CON8:
	case AFE_GASRC9_NEW_CON9:
	case AFE_GASRC9_NEW_CON12:
	case AFE_GASRC10_NEW_CON8:
	case AFE_GASRC10_NEW_CON9:
	case AFE_GASRC10_NEW_CON12:
	case AFE_GASRC11_NEW_CON8:
	case AFE_GASRC11_NEW_CON9:
	case AFE_GASRC11_NEW_CON12:
	case AFE_GASRC12_NEW_CON8:
	case AFE_GASRC12_NEW_CON9:
	case AFE_GASRC12_NEW_CON12:
	case AFE_GASRC13_NEW_CON8:
	case AFE_GASRC13_NEW_CON9:
	case AFE_GASRC13_NEW_CON12:
	case AFE_GASRC14_NEW_CON8:
	case AFE_GASRC14_NEW_CON9:
	case AFE_GASRC14_NEW_CON12:
	case AFE_GASRC15_NEW_CON8:
	case AFE_GASRC15_NEW_CON9:
	case AFE_GASRC15_NEW_CON12:
	case AFE_GASRC16_NEW_CON8:
	case AFE_GASRC16_NEW_CON9:
	case AFE_GASRC16_NEW_CON12:
	case AFE_GASRC17_NEW_CON8:
	case AFE_GASRC17_NEW_CON9:
	case AFE_GASRC17_NEW_CON12:
	case AFE_GASRC18_NEW_CON8:
	case AFE_GASRC18_NEW_CON9:
	case AFE_GASRC18_NEW_CON12:
	case AFE_GASRC19_NEW_CON8:
	case AFE_GASRC19_NEW_CON9:
	case AFE_GASRC19_NEW_CON12:
		return true;
	default:
		return false;
	};
}

static const struct regmap_config mt8195_afe_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.volatile_reg = mt8195_is_volatile_reg,
	.max_register = AFE_MAX_REGISTER,
	.num_reg_defaults_raw = ((AFE_MAX_REGISTER / 4) + 1),
	.cache_type = REGCACHE_FLAT,
};

#define AFE_IRQ_CLR_BITS (0x387)
#define ASYS_IRQ_CLR_BITS (0xffff)

static irqreturn_t mt8195_afe_irq_handler(int irq_id, void *dev_id)
{
	struct mtk_base_afe *afe = dev_id;
	unsigned int val = 0;
	unsigned int asys_irq_clr_bits = 0;
	unsigned int afe_irq_clr_bits = 0;
	unsigned int irq_status_bits = 0;
	unsigned int irq_clr_bits = 0;
	unsigned int mcu_irq_mask = 0;
	int i = 0;
	int ret = 0;

	ret = regmap_read(afe->regmap, AFE_IRQ_STATUS, &val);
	if (ret) {
		dev_info(afe->dev, "%s irq status err\n", __func__);
		afe_irq_clr_bits = AFE_IRQ_CLR_BITS;
		asys_irq_clr_bits = ASYS_IRQ_CLR_BITS;
		goto err_irq;
	}

	ret = regmap_read(afe->regmap, AFE_IRQ_MASK, &mcu_irq_mask);
	if (ret) {
		dev_info(afe->dev, "%s read irq mask err\n", __func__);
		afe_irq_clr_bits = AFE_IRQ_CLR_BITS;
		asys_irq_clr_bits = ASYS_IRQ_CLR_BITS;
		goto err_irq;
	}

	/* only clr cpu irq */
	val &= mcu_irq_mask;

	for (i = 0; i < MT8195_AFE_MEMIF_NUM; i++) {
		struct mtk_base_afe_memif *memif = &afe->memif[i];
		struct mtk_base_irq_data const *irq_data;

		if (memif->irq_usage < 0)
			continue;

		irq_data = afe->irqs[memif->irq_usage].irq_data;

		irq_status_bits = BIT(irq_data->irq_status_shift);
		irq_clr_bits = BIT(irq_data->irq_clr_shift);

		if (!(val & irq_status_bits))
			continue;

		if (irq_data->irq_clr_reg == ASYS_IRQ_CLR)
			asys_irq_clr_bits |= irq_clr_bits;
		else
			afe_irq_clr_bits |= irq_clr_bits;

		snd_pcm_period_elapsed(memif->substream);
	}

err_irq:
	/* clear irq */
	if (asys_irq_clr_bits)
		regmap_write(afe->regmap, ASYS_IRQ_CLR, asys_irq_clr_bits);
	if (afe_irq_clr_bits)
		regmap_write(afe->regmap, AFE_IRQ_MCU_CLR, afe_irq_clr_bits);

	return IRQ_HANDLED;
}

static int mt8195_afe_runtime_suspend(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	struct mt8195_afe_private *afe_priv = afe->platform_priv;

	if (!afe->regmap || afe_priv->pm_runtime_bypass_reg_ctl)
		goto skip_regmap;

	mt8195_afe_disable_main_clock(afe);

	regcache_cache_only(afe->regmap, true);
	regcache_mark_dirty(afe->regmap);

skip_regmap:
	mt8195_afe_disable_reg_rw_clk(afe);

	return 0;
}

static int mt8195_afe_runtime_resume(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	struct mt8195_afe_private *afe_priv = afe->platform_priv;

	mt8195_afe_enable_reg_rw_clk(afe);

	if (!afe->regmap || afe_priv->pm_runtime_bypass_reg_ctl)
		goto skip_regmap;

	regcache_cache_only(afe->regmap, false);
	regcache_sync(afe->regmap);

	mt8195_afe_enable_main_clock(afe);
skip_regmap:
	return 0;
}

static int mt8195_afe_component_probe(struct snd_soc_component *component)
{
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	int ret = 0;

	snd_soc_component_init_regmap(component, afe->regmap);

	ret = mtk_afe_add_sub_dai_control(component);

	return ret;
}

static const struct snd_soc_component_driver mt8195_afe_component = {
	.name = AFE_PCM_NAME,
	.pointer = mtk_afe_pcm_pointer,
	.pcm_construct = mtk_afe_pcm_new,
	.probe = mt8195_afe_component_probe,
};

static int init_memif_priv_data(struct mtk_base_afe *afe)
{
	struct mt8195_afe_private *afe_priv = afe->platform_priv;
	struct mtk_dai_memif_priv *memif_priv;
	int i;

	for (i = MT8195_AFE_MEMIF_START; i < MT8195_AFE_MEMIF_END; i++) {
		memif_priv = devm_kzalloc(afe->dev,
					  sizeof(struct mtk_dai_memif_priv),
					  GFP_KERNEL);
		if (!memif_priv)
			return -ENOMEM;

		afe_priv->dai_priv[i] = memif_priv;
	}

	return 0;
}

static int mt8195_dai_memif_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mt8195_memif_dai_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mt8195_memif_dai_driver);

	dai->dapm_widgets = mt8195_memif_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mt8195_memif_widgets);
	dai->dapm_routes = mt8195_memif_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mt8195_memif_routes);
	dai->controls = mt8195_memif_controls;
	dai->num_controls = ARRAY_SIZE(mt8195_memif_controls);

	return init_memif_priv_data(afe);
}

typedef int (*dai_register_cb)(struct mtk_base_afe *);
static const dai_register_cb dai_register_cbs[] = {
	mt8195_dai_adda_register,
	mt8195_dai_etdm_register,
	mt8195_dai_pcm_register,
	mt8195_dai_memif_register,
};

static const struct reg_sequence mt8195_afe_reg_defaults[] = {
	{ AFE_IRQ_MASK, 0x387ffff },
	{ AFE_IRQ3_CON, BIT(30) },
	{ AFE_IRQ9_CON, BIT(30) },
	{ ETDM_IN1_CON4, 0x12000100 },
	{ ETDM_IN2_CON4, 0x12000100 },
};

static const struct reg_sequence mt8195_cg_patch[] = {
	{ AUDIO_TOP_CON0, 0xfffffffb },
	{ AUDIO_TOP_CON1, 0xfffffff8 },
};

static int mt8195_afe_init_registers(struct mtk_base_afe *afe)
{
	return regmap_multi_reg_write(afe->regmap,
			mt8195_afe_reg_defaults,
			ARRAY_SIZE(mt8195_afe_reg_defaults));
}

static void mt8195_afe_parse_of(struct mtk_base_afe *afe,
				struct device_node *np)
{
#if IS_ENABLED(CONFIG_SND_SOC_MT6359)
	struct mt8195_afe_private *afe_priv = afe->platform_priv;

	afe_priv->topckgen = syscon_regmap_lookup_by_phandle(afe->dev->of_node,
							     "mediatek,topckgen");
	if (IS_ERR(afe_priv->topckgen)) {
		dev_info(afe->dev, "%s() Cannot find topckgen controller: %ld\n",
			 __func__, PTR_ERR(afe_priv->topckgen));
	}
#endif
}

static int mt8195_afe_pcm_dev_probe(struct platform_device *pdev)
{
	struct mtk_base_afe *afe;
	struct mt8195_afe_private *afe_priv;
	struct device *dev = &pdev->dev;
	struct reset_control *rstc;
	int i, irq_id, ret;
	struct snd_soc_component *component;

	ret = of_reserved_mem_device_init(dev);
	if (ret) {
		dev_err(dev, "failed to assign memory region: %d\n", ret);
		return ret;
	}

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(33));
	if (ret)
		return ret;

	afe = devm_kzalloc(dev, sizeof(*afe), GFP_KERNEL);
	if (!afe)
		return -ENOMEM;

	afe->platform_priv = devm_kzalloc(dev, sizeof(*afe_priv),
					  GFP_KERNEL);
	if (!afe->platform_priv)
		return -ENOMEM;

	afe_priv = afe->platform_priv;
	afe->dev = &pdev->dev;

	afe->base_addr = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(afe->base_addr))
		return PTR_ERR(afe->base_addr);

	/* initial audio related clock */
	ret = mt8195_afe_init_clock(afe);
	if (ret) {
		dev_err(dev, "init clock error\n");
		return ret;
	}

	/* reset controller to reset audio regs before regmap cache */
	rstc = devm_reset_control_get_exclusive(dev, "audiosys");
	if (IS_ERR(rstc)) {
		ret = PTR_ERR(rstc);
		dev_err(dev, "could not get audiosys reset:%d\n", ret);
		return ret;
	}

	ret = reset_control_reset(rstc);
	if (ret) {
		dev_err(dev, "failed to trigger audio reset:%d\n", ret);
		return ret;
	}

	spin_lock_init(&afe_priv->afe_ctrl_lock);

	mutex_init(&afe->irq_alloc_lock);

	/* irq initialize */
	afe->irqs_size = MT8195_AFE_IRQ_NUM;
	afe->irqs = devm_kcalloc(dev, afe->irqs_size, sizeof(*afe->irqs),
				 GFP_KERNEL);
	if (!afe->irqs)
		return -ENOMEM;

	for (i = 0; i < afe->irqs_size; i++)
		afe->irqs[i].irq_data = &irq_data_array[i];

	/* init memif */
	afe->memif_size = MT8195_AFE_MEMIF_NUM;
	afe->memif = devm_kcalloc(dev, afe->memif_size, sizeof(*afe->memif),
				  GFP_KERNEL);
	if (!afe->memif)
		return -ENOMEM;

	for (i = 0; i < afe->memif_size; i++) {
		afe->memif[i].data = &memif_data[i];
		afe->memif[i].irq_usage = mt8195_afe_memif_const_irqs[i];
		afe->memif[i].const_irq = 1;
		afe->irqs[afe->memif[i].irq_usage].irq_occupyed = true;
	}

	/* request irq */
	irq_id = platform_get_irq(pdev, 0);
	if (irq_id < 0)
		return -ENXIO;

	ret = devm_request_irq(dev, irq_id, mt8195_afe_irq_handler,
			       IRQF_TRIGGER_NONE, "asys-isr", (void *)afe);
	if (ret) {
		dev_err(dev, "could not request_irq for asys-isr\n");
		return ret;
	}

	/* init sub_dais */
	INIT_LIST_HEAD(&afe->sub_dais);

	for (i = 0; i < ARRAY_SIZE(dai_register_cbs); i++) {
		ret = dai_register_cbs[i](afe);
		if (ret) {
			dev_warn(dev, "dai register i %d fail, ret %d\n",
				 i, ret);
			return ret;
		}
	}

	/* init dai_driver and component_driver */
	ret = mtk_afe_combine_sub_dai(afe);
	if (ret) {
		dev_warn(dev, "mtk_afe_combine_sub_dai fail, ret %d\n",
			 ret);
		return ret;
	}

	afe->mtk_afe_hardware = &mt8195_afe_hardware;
	afe->memif_fs = mt8195_memif_fs;
	afe->irq_fs = mt8195_irq_fs;

	afe->runtime_resume = mt8195_afe_runtime_resume;
	afe->runtime_suspend = mt8195_afe_runtime_suspend;

	platform_set_drvdata(pdev, afe);

	mt8195_afe_parse_of(afe, pdev->dev.of_node);

	pm_runtime_enable(dev);
	if (!pm_runtime_enabled(dev)) {
		ret = mt8195_afe_runtime_resume(dev);
		if (ret)
			return ret;
	}

	/* enable clock for regcache get default value from hw */
	afe_priv->pm_runtime_bypass_reg_ctl = true;
	pm_runtime_get_sync(dev);

	afe->regmap = devm_regmap_init_mmio(&pdev->dev, afe->base_addr,
					    &mt8195_afe_regmap_config);
	if (IS_ERR(afe->regmap)) {
		ret = PTR_ERR(afe->regmap);
		goto err_pm_put;
	}

	ret = regmap_register_patch(afe->regmap, mt8195_cg_patch,
				    ARRAY_SIZE(mt8195_cg_patch));
	if (ret < 0) {
		dev_err(dev, "Failed to apply cg patch\n");
		goto err_pm_put;
	}

	/* register component */
	ret = devm_snd_soc_register_component(dev, &mt8195_afe_component,
					      NULL, 0);
	if (ret) {
		dev_warn(dev, "err_platform\n");
		goto err_pm_put;
	}

	component = devm_kzalloc(dev, sizeof(*component), GFP_KERNEL);
	if (!component) {
		ret = -ENOMEM;
		goto err_pm_put;
	}

	ret = snd_soc_component_initialize(component,
					   &mt8195_afe_pcm_dai_component,
					   dev);
	if (ret)
		goto err_pm_put;

#ifdef CONFIG_DEBUG_FS
	component->debugfs_prefix = "pcm";
#endif

	ret = snd_soc_add_component(component,
				    afe->dai_drivers,
				    afe->num_dai_drivers);
	if (ret) {
		dev_warn(dev, "err_dai_component\n");
		goto err_pm_put;
	}

	mt8195_afe_init_registers(afe);

	pm_runtime_put_sync(dev);
	afe_priv->pm_runtime_bypass_reg_ctl = false;

	regcache_cache_only(afe->regmap, true);
	regcache_mark_dirty(afe->regmap);

	return 0;

err_pm_put:
	pm_runtime_put_sync(dev);
	pm_runtime_disable(dev);

	return ret;
}

static void mt8195_afe_pcm_dev_remove(struct platform_device *pdev)
{
	struct mtk_base_afe *afe = platform_get_drvdata(pdev);

	snd_soc_unregister_component(&pdev->dev);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		mt8195_afe_runtime_suspend(&pdev->dev);

	mt8195_afe_deinit_clock(afe);
}

static const struct of_device_id mt8195_afe_pcm_dt_match[] = {
	{.compatible = "mediatek,mt8195-audio", },
	{},
};
MODULE_DEVICE_TABLE(of, mt8195_afe_pcm_dt_match);

static const struct dev_pm_ops mt8195_afe_pm_ops = {
	SET_RUNTIME_PM_OPS(mt8195_afe_runtime_suspend,
			   mt8195_afe_runtime_resume, NULL)
};

static struct platform_driver mt8195_afe_pcm_driver = {
	.driver = {
		   .name = "mt8195-audio",
		   .of_match_table = mt8195_afe_pcm_dt_match,
		   .pm = &mt8195_afe_pm_ops,
	},
	.probe = mt8195_afe_pcm_dev_probe,
	.remove_new = mt8195_afe_pcm_dev_remove,
};

module_platform_driver(mt8195_afe_pcm_driver);

MODULE_DESCRIPTION("Mediatek ALSA SoC AFE platform driver for 8195");
MODULE_AUTHOR("Bicycle Tsai <bicycle.tsai@mediatek.com>");
MODULE_LICENSE("GPL v2");
