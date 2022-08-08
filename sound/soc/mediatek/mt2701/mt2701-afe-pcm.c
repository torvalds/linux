// SPDX-License-Identifier: GPL-2.0
/*
 * Mediatek ALSA SoC AFE platform driver for 2701
 *
 * Copyright (c) 2016 MediaTek Inc.
 * Author: Garlic Tseng <garlic.tseng@mediatek.com>
 *	   Ir Lian <ir.lian@mediatek.com>
 *	   Ryder Lee <ryder.lee@mediatek.com>
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pm_runtime.h>

#include "mt2701-afe-common.h"
#include "mt2701-afe-clock-ctrl.h"
#include "../common/mtk-afe-platform-driver.h"
#include "../common/mtk-afe-fe-dai.h"

static const struct snd_pcm_hardware mt2701_afe_hardware = {
	.info = SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED
		| SNDRV_PCM_INFO_RESUME | SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_S24_LE
		   | SNDRV_PCM_FMTBIT_S32_LE,
	.period_bytes_min = 1024,
	.period_bytes_max = 1024 * 256,
	.periods_min = 4,
	.periods_max = 1024,
	.buffer_bytes_max = 1024 * 1024,
	.fifo_size = 0,
};

struct mt2701_afe_rate {
	unsigned int rate;
	unsigned int regvalue;
};

static const struct mt2701_afe_rate mt2701_afe_i2s_rates[] = {
	{ .rate = 8000, .regvalue = 0 },
	{ .rate = 12000, .regvalue = 1 },
	{ .rate = 16000, .regvalue = 2 },
	{ .rate = 24000, .regvalue = 3 },
	{ .rate = 32000, .regvalue = 4 },
	{ .rate = 48000, .regvalue = 5 },
	{ .rate = 96000, .regvalue = 6 },
	{ .rate = 192000, .regvalue = 7 },
	{ .rate = 384000, .regvalue = 8 },
	{ .rate = 7350, .regvalue = 16 },
	{ .rate = 11025, .regvalue = 17 },
	{ .rate = 14700, .regvalue = 18 },
	{ .rate = 22050, .regvalue = 19 },
	{ .rate = 29400, .regvalue = 20 },
	{ .rate = 44100, .regvalue = 21 },
	{ .rate = 88200, .regvalue = 22 },
	{ .rate = 176400, .regvalue = 23 },
	{ .rate = 352800, .regvalue = 24 },
};

static const unsigned int mt2701_afe_backup_list[] = {
	AUDIO_TOP_CON0,
	AUDIO_TOP_CON4,
	AUDIO_TOP_CON5,
	ASYS_TOP_CON,
	AFE_CONN0,
	AFE_CONN1,
	AFE_CONN2,
	AFE_CONN3,
	AFE_CONN15,
	AFE_CONN16,
	AFE_CONN17,
	AFE_CONN18,
	AFE_CONN19,
	AFE_CONN20,
	AFE_CONN21,
	AFE_CONN22,
	AFE_DAC_CON0,
	AFE_MEMIF_PBUF_SIZE,
};

static int mt2701_dai_num_to_i2s(struct mtk_base_afe *afe, int num)
{
	struct mt2701_afe_private *afe_priv = afe->platform_priv;
	int val = num - MT2701_IO_I2S;

	if (val < 0 || val >= afe_priv->soc->i2s_num) {
		dev_err(afe->dev, "%s, num not available, num %d, val %d\n",
			__func__, num, val);
		return -EINVAL;
	}
	return val;
}

static int mt2701_afe_i2s_fs(unsigned int sample_rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt2701_afe_i2s_rates); i++)
		if (mt2701_afe_i2s_rates[i].rate == sample_rate)
			return mt2701_afe_i2s_rates[i].regvalue;

	return -EINVAL;
}

static int mt2701_afe_i2s_startup(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt2701_afe_private *afe_priv = afe->platform_priv;
	int i2s_num = mt2701_dai_num_to_i2s(afe, dai->id);
	bool mode = afe_priv->soc->has_one_heart_mode;

	if (i2s_num < 0)
		return i2s_num;

	return mt2701_afe_enable_mclk(afe, mode ? 1 : i2s_num);
}

static int mt2701_afe_i2s_path_disable(struct mtk_base_afe *afe,
				       struct mt2701_i2s_path *i2s_path,
				       int stream_dir)
{
	const struct mt2701_i2s_data *i2s_data = i2s_path->i2s_data[stream_dir];

	if (--i2s_path->on[stream_dir] < 0)
		i2s_path->on[stream_dir] = 0;

	if (i2s_path->on[stream_dir])
		return 0;

	/* disable i2s */
	regmap_update_bits(afe->regmap, i2s_data->i2s_ctrl_reg,
			   ASYS_I2S_CON_I2S_EN, 0);

	mt2701_afe_disable_i2s(afe, i2s_path, stream_dir);

	return 0;
}

static void mt2701_afe_i2s_shutdown(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt2701_afe_private *afe_priv = afe->platform_priv;
	int i2s_num = mt2701_dai_num_to_i2s(afe, dai->id);
	struct mt2701_i2s_path *i2s_path;
	bool mode = afe_priv->soc->has_one_heart_mode;

	if (i2s_num < 0)
		return;

	i2s_path = &afe_priv->i2s_path[i2s_num];

	if (i2s_path->occupied[substream->stream])
		i2s_path->occupied[substream->stream] = 0;
	else
		goto exit;

	mt2701_afe_i2s_path_disable(afe, i2s_path, substream->stream);

	/* need to disable i2s-out path when disable i2s-in */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		mt2701_afe_i2s_path_disable(afe, i2s_path, !substream->stream);

exit:
	/* disable mclk */
	mt2701_afe_disable_mclk(afe, mode ? 1 : i2s_num);
}

static int mt2701_i2s_path_enable(struct mtk_base_afe *afe,
				  struct mt2701_i2s_path *i2s_path,
				  int stream_dir, int rate)
{
	const struct mt2701_i2s_data *i2s_data = i2s_path->i2s_data[stream_dir];
	struct mt2701_afe_private *afe_priv = afe->platform_priv;
	int reg, fs, w_len = 1; /* now we support bck 64bits only */
	unsigned int mask, val;

	/* no need to enable if already done */
	if (++i2s_path->on[stream_dir] != 1)
		return 0;

	fs = mt2701_afe_i2s_fs(rate);

	mask = ASYS_I2S_CON_FS |
	       ASYS_I2S_CON_I2S_COUPLE_MODE | /* 0 */
	       ASYS_I2S_CON_I2S_MODE |
	       ASYS_I2S_CON_WIDE_MODE;

	val = ASYS_I2S_CON_FS_SET(fs) |
	      ASYS_I2S_CON_I2S_MODE |
	      ASYS_I2S_CON_WIDE_MODE_SET(w_len);

	if (stream_dir == SNDRV_PCM_STREAM_CAPTURE) {
		mask |= ASYS_I2S_IN_PHASE_FIX;
		val |= ASYS_I2S_IN_PHASE_FIX;
		reg = ASMI_TIMING_CON1;
	} else {
		if (afe_priv->soc->has_one_heart_mode) {
			mask |= ASYS_I2S_CON_ONE_HEART_MODE;
			val |= ASYS_I2S_CON_ONE_HEART_MODE;
		}
		reg = ASMO_TIMING_CON1;
	}

	regmap_update_bits(afe->regmap, i2s_data->i2s_ctrl_reg, mask, val);

	regmap_update_bits(afe->regmap, reg,
			   i2s_data->i2s_asrc_fs_mask
			   << i2s_data->i2s_asrc_fs_shift,
			   fs << i2s_data->i2s_asrc_fs_shift);

	/* enable i2s */
	mt2701_afe_enable_i2s(afe, i2s_path, stream_dir);

	/* reset i2s hw status before enable */
	regmap_update_bits(afe->regmap, i2s_data->i2s_ctrl_reg,
			   ASYS_I2S_CON_RESET, ASYS_I2S_CON_RESET);
	udelay(1);
	regmap_update_bits(afe->regmap, i2s_data->i2s_ctrl_reg,
			   ASYS_I2S_CON_RESET, 0);
	udelay(1);
	regmap_update_bits(afe->regmap, i2s_data->i2s_ctrl_reg,
			   ASYS_I2S_CON_I2S_EN, ASYS_I2S_CON_I2S_EN);
	return 0;
}

static int mt2701_afe_i2s_prepare(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt2701_afe_private *afe_priv = afe->platform_priv;
	int ret, i2s_num = mt2701_dai_num_to_i2s(afe, dai->id);
	struct mt2701_i2s_path *i2s_path;
	bool mode = afe_priv->soc->has_one_heart_mode;

	if (i2s_num < 0)
		return i2s_num;

	i2s_path = &afe_priv->i2s_path[i2s_num];

	if (i2s_path->occupied[substream->stream])
		return -EBUSY;

	ret = mt2701_mclk_configuration(afe, mode ? 1 : i2s_num);
	if (ret)
		return ret;

	i2s_path->occupied[substream->stream] = 1;

	/* need to enable i2s-out path when enable i2s-in */
	if (substream->stream == SNDRV_PCM_STREAM_CAPTURE)
		mt2701_i2s_path_enable(afe, i2s_path, !substream->stream,
				       substream->runtime->rate);

	mt2701_i2s_path_enable(afe, i2s_path, substream->stream,
			       substream->runtime->rate);

	return 0;
}

static int mt2701_afe_i2s_set_sysclk(struct snd_soc_dai *dai, int clk_id,
				     unsigned int freq, int dir)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt2701_afe_private *afe_priv = afe->platform_priv;
	int i2s_num = mt2701_dai_num_to_i2s(afe, dai->id);
	bool mode = afe_priv->soc->has_one_heart_mode;

	if (i2s_num < 0)
		return i2s_num;

	/* mclk */
	if (dir == SND_SOC_CLOCK_IN) {
		dev_warn(dai->dev, "The SoCs doesn't support mclk input\n");
		return -EINVAL;
	}

	afe_priv->i2s_path[mode ? 1 : i2s_num].mclk_rate = freq;

	return 0;
}

static int mt2701_btmrg_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt2701_afe_private *afe_priv = afe->platform_priv;
	int ret;

	ret = mt2701_enable_btmrg_clk(afe);
	if (ret)
		return ret;

	afe_priv->mrg_enable[substream->stream] = 1;

	return 0;
}

static int mt2701_btmrg_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int stream_fs;
	u32 val, msk;

	stream_fs = params_rate(params);

	if (stream_fs != 8000 && stream_fs != 16000) {
		dev_err(afe->dev, "unsupported rate %d\n", stream_fs);
		return -EINVAL;
	}

	regmap_update_bits(afe->regmap, AFE_MRGIF_CON,
			   AFE_MRGIF_CON_I2S_MODE_MASK,
			   AFE_MRGIF_CON_I2S_MODE_32K);

	val = AFE_DAIBT_CON0_BT_FUNC_EN | AFE_DAIBT_CON0_BT_FUNC_RDY
	      | AFE_DAIBT_CON0_MRG_USE;
	msk = val;

	if (stream_fs == 16000)
		val |= AFE_DAIBT_CON0_BT_WIDE_MODE_EN;

	msk |= AFE_DAIBT_CON0_BT_WIDE_MODE_EN;

	regmap_update_bits(afe->regmap, AFE_DAIBT_CON0, msk, val);

	regmap_update_bits(afe->regmap, AFE_DAIBT_CON0,
			   AFE_DAIBT_CON0_DAIBT_EN,
			   AFE_DAIBT_CON0_DAIBT_EN);
	regmap_update_bits(afe->regmap, AFE_MRGIF_CON,
			   AFE_MRGIF_CON_MRG_I2S_EN,
			   AFE_MRGIF_CON_MRG_I2S_EN);
	regmap_update_bits(afe->regmap, AFE_MRGIF_CON,
			   AFE_MRGIF_CON_MRG_EN,
			   AFE_MRGIF_CON_MRG_EN);
	return 0;
}

static void mt2701_btmrg_shutdown(struct snd_pcm_substream *substream,
				  struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt2701_afe_private *afe_priv = afe->platform_priv;

	/* if the other direction stream is not occupied */
	if (!afe_priv->mrg_enable[!substream->stream]) {
		regmap_update_bits(afe->regmap, AFE_DAIBT_CON0,
				   AFE_DAIBT_CON0_DAIBT_EN, 0);
		regmap_update_bits(afe->regmap, AFE_MRGIF_CON,
				   AFE_MRGIF_CON_MRG_EN, 0);
		regmap_update_bits(afe->regmap, AFE_MRGIF_CON,
				   AFE_MRGIF_CON_MRG_I2S_EN, 0);
		mt2701_disable_btmrg_clk(afe);
	}

	afe_priv->mrg_enable[substream->stream] = 0;
}

static int mt2701_simple_fe_startup(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mtk_base_afe_memif *memif_tmp;
	int stream_dir = substream->stream;

	/* can't run single DL & DLM at the same time */
	if (stream_dir == SNDRV_PCM_STREAM_PLAYBACK) {
		memif_tmp = &afe->memif[MT2701_MEMIF_DLM];
		if (memif_tmp->substream) {
			dev_warn(afe->dev, "memif is not available");
			return -EBUSY;
		}
	}

	return mtk_afe_fe_startup(substream, dai);
}

static int mt2701_simple_fe_hw_params(struct snd_pcm_substream *substream,
				      struct snd_pcm_hw_params *params,
				      struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int stream_dir = substream->stream;

	/* single DL use PAIR_INTERLEAVE */
	if (stream_dir == SNDRV_PCM_STREAM_PLAYBACK)
		regmap_update_bits(afe->regmap,
				   AFE_MEMIF_PBUF_SIZE,
				   AFE_MEMIF_PBUF_SIZE_DLM_MASK,
				   AFE_MEMIF_PBUF_SIZE_PAIR_INTERLEAVE);

	return mtk_afe_fe_hw_params(substream, params, dai);
}

static int mt2701_dlm_fe_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mtk_base_afe_memif *memif_tmp;
	const struct mtk_base_memif_data *memif_data;
	int i;

	for (i = MT2701_MEMIF_DL1; i < MT2701_MEMIF_DL_SINGLE_NUM; ++i) {
		memif_tmp = &afe->memif[i];
		if (memif_tmp->substream)
			return -EBUSY;
	}

	/* enable agent for all signal DL (due to hw design) */
	for (i = MT2701_MEMIF_DL1; i < MT2701_MEMIF_DL_SINGLE_NUM; ++i) {
		memif_data = afe->memif[i].data;
		regmap_update_bits(afe->regmap,
				   memif_data->agent_disable_reg,
				   1 << memif_data->agent_disable_shift,
				   0 << memif_data->agent_disable_shift);
	}

	return mtk_afe_fe_startup(substream, dai);
}

static void mt2701_dlm_fe_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	const struct mtk_base_memif_data *memif_data;
	int i;

	for (i = MT2701_MEMIF_DL1; i < MT2701_MEMIF_DL_SINGLE_NUM; ++i) {
		memif_data = afe->memif[i].data;
		regmap_update_bits(afe->regmap,
				   memif_data->agent_disable_reg,
				   1 << memif_data->agent_disable_shift,
				   1 << memif_data->agent_disable_shift);
	}

	return mtk_afe_fe_shutdown(substream, dai);
}

static int mt2701_dlm_fe_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int channels = params_channels(params);

	regmap_update_bits(afe->regmap,
			   AFE_MEMIF_PBUF_SIZE,
			   AFE_MEMIF_PBUF_SIZE_DLM_MASK,
			   AFE_MEMIF_PBUF_SIZE_FULL_INTERLEAVE);
	regmap_update_bits(afe->regmap,
			   AFE_MEMIF_PBUF_SIZE,
			   AFE_MEMIF_PBUF_SIZE_DLM_BYTE_MASK,
			   AFE_MEMIF_PBUF_SIZE_DLM_32BYTES);
	regmap_update_bits(afe->regmap,
			   AFE_MEMIF_PBUF_SIZE,
			   AFE_MEMIF_PBUF_SIZE_DLM_CH_MASK,
			   AFE_MEMIF_PBUF_SIZE_DLM_CH(channels));

	return mtk_afe_fe_hw_params(substream, params, dai);
}

static int mt2701_dlm_fe_trigger(struct snd_pcm_substream *substream,
				 int cmd, struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mtk_base_afe_memif *memif_tmp = &afe->memif[MT2701_MEMIF_DL1];

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		regmap_update_bits(afe->regmap, memif_tmp->data->enable_reg,
				   1 << memif_tmp->data->enable_shift,
				   1 << memif_tmp->data->enable_shift);
		mtk_afe_fe_trigger(substream, cmd, dai);
		return 0;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		mtk_afe_fe_trigger(substream, cmd, dai);
		regmap_update_bits(afe->regmap, memif_tmp->data->enable_reg,
				   1 << memif_tmp->data->enable_shift, 0);

		return 0;
	default:
		return -EINVAL;
	}
}

static int mt2701_memif_fs(struct snd_pcm_substream *substream,
			   unsigned int rate)
{
	struct snd_soc_pcm_runtime *rtd = asoc_substream_to_rtd(substream);
	int fs;

	if (asoc_rtd_to_cpu(rtd, 0)->id != MT2701_MEMIF_ULBT)
		fs = mt2701_afe_i2s_fs(rate);
	else
		fs = (rate == 16000 ? 1 : 0);

	return fs;
}

static int mt2701_irq_fs(struct snd_pcm_substream *substream, unsigned int rate)
{
	return mt2701_afe_i2s_fs(rate);
}

/* FE DAIs */
static const struct snd_soc_dai_ops mt2701_single_memif_dai_ops = {
	.startup	= mt2701_simple_fe_startup,
	.shutdown	= mtk_afe_fe_shutdown,
	.hw_params	= mt2701_simple_fe_hw_params,
	.hw_free	= mtk_afe_fe_hw_free,
	.prepare	= mtk_afe_fe_prepare,
	.trigger	= mtk_afe_fe_trigger,
};

static const struct snd_soc_dai_ops mt2701_dlm_memif_dai_ops = {
	.startup	= mt2701_dlm_fe_startup,
	.shutdown	= mt2701_dlm_fe_shutdown,
	.hw_params	= mt2701_dlm_fe_hw_params,
	.hw_free	= mtk_afe_fe_hw_free,
	.prepare	= mtk_afe_fe_prepare,
	.trigger	= mt2701_dlm_fe_trigger,
};

/* I2S BE DAIs */
static const struct snd_soc_dai_ops mt2701_afe_i2s_ops = {
	.startup	= mt2701_afe_i2s_startup,
	.shutdown	= mt2701_afe_i2s_shutdown,
	.prepare	= mt2701_afe_i2s_prepare,
	.set_sysclk	= mt2701_afe_i2s_set_sysclk,
};

/* MRG BE DAIs */
static const struct snd_soc_dai_ops mt2701_btmrg_ops = {
	.startup = mt2701_btmrg_startup,
	.shutdown = mt2701_btmrg_shutdown,
	.hw_params = mt2701_btmrg_hw_params,
};

static struct snd_soc_dai_driver mt2701_afe_pcm_dais[] = {
	/* FE DAIs: memory intefaces to CPU */
	{
		.name = "PCMO0",
		.id = MT2701_MEMIF_DL1,
		.playback = {
			.stream_name = "DL1",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE
				| SNDRV_PCM_FMTBIT_S32_LE)
		},
		.ops = &mt2701_single_memif_dai_ops,
	},
	{
		.name = "PCM_multi",
		.id = MT2701_MEMIF_DLM,
		.playback = {
			.stream_name = "DLM",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE
				| SNDRV_PCM_FMTBIT_S32_LE)

		},
		.ops = &mt2701_dlm_memif_dai_ops,
	},
	{
		.name = "PCM0",
		.id = MT2701_MEMIF_UL1,
		.capture = {
			.stream_name = "UL1",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_48000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE
				| SNDRV_PCM_FMTBIT_S32_LE)
		},
		.ops = &mt2701_single_memif_dai_ops,
	},
	{
		.name = "PCM1",
		.id = MT2701_MEMIF_UL2,
		.capture = {
			.stream_name = "UL2",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE
				| SNDRV_PCM_FMTBIT_S32_LE)

		},
		.ops = &mt2701_single_memif_dai_ops,
	},
	{
		.name = "PCM_BT_DL",
		.id = MT2701_MEMIF_DLBT,
		.playback = {
			.stream_name = "DLBT",
			.channels_min = 1,
			.channels_max = 1,
			.rates = (SNDRV_PCM_RATE_8000
				| SNDRV_PCM_RATE_16000),
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &mt2701_single_memif_dai_ops,
	},
	{
		.name = "PCM_BT_UL",
		.id = MT2701_MEMIF_ULBT,
		.capture = {
			.stream_name = "ULBT",
			.channels_min = 1,
			.channels_max = 1,
			.rates = (SNDRV_PCM_RATE_8000
				| SNDRV_PCM_RATE_16000),
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &mt2701_single_memif_dai_ops,
	},
	/* BE DAIs */
	{
		.name = "I2S0",
		.id = MT2701_IO_I2S,
		.playback = {
			.stream_name = "I2S0 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE
				| SNDRV_PCM_FMTBIT_S32_LE)

		},
		.capture = {
			.stream_name = "I2S0 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE
				| SNDRV_PCM_FMTBIT_S32_LE)

		},
		.ops = &mt2701_afe_i2s_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "I2S1",
		.id = MT2701_IO_2ND_I2S,
		.playback = {
			.stream_name = "I2S1 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE
				| SNDRV_PCM_FMTBIT_S32_LE)
			},
		.capture = {
			.stream_name = "I2S1 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE
				| SNDRV_PCM_FMTBIT_S32_LE)
			},
		.ops = &mt2701_afe_i2s_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "I2S2",
		.id = MT2701_IO_3RD_I2S,
		.playback = {
			.stream_name = "I2S2 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE
				| SNDRV_PCM_FMTBIT_S32_LE)
			},
		.capture = {
			.stream_name = "I2S2 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE
				| SNDRV_PCM_FMTBIT_S32_LE)
			},
		.ops = &mt2701_afe_i2s_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "I2S3",
		.id = MT2701_IO_4TH_I2S,
		.playback = {
			.stream_name = "I2S3 Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE
				| SNDRV_PCM_FMTBIT_S32_LE)
			},
		.capture = {
			.stream_name = "I2S3 Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = (SNDRV_PCM_FMTBIT_S16_LE
				| SNDRV_PCM_FMTBIT_S24_LE
				| SNDRV_PCM_FMTBIT_S32_LE)
			},
		.ops = &mt2701_afe_i2s_ops,
		.symmetric_rate = 1,
	},
	{
		.name = "MRG BT",
		.id = MT2701_IO_MRG,
		.playback = {
			.stream_name = "BT Playback",
			.channels_min = 1,
			.channels_max = 1,
			.rates = (SNDRV_PCM_RATE_8000
				| SNDRV_PCM_RATE_16000),
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.capture = {
			.stream_name = "BT Capture",
			.channels_min = 1,
			.channels_max = 1,
			.rates = (SNDRV_PCM_RATE_8000
				| SNDRV_PCM_RATE_16000),
			.formats = SNDRV_PCM_FMTBIT_S16_LE,
		},
		.ops = &mt2701_btmrg_ops,
		.symmetric_rate = 1,
	}
};

static const struct snd_kcontrol_new mt2701_afe_o00_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I00 Switch", AFE_CONN0, 0, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_o01_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I01 Switch", AFE_CONN1, 1, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_o02_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I02 Switch", AFE_CONN2, 2, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_o03_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I03 Switch", AFE_CONN3, 3, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_o14_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I26 Switch", AFE_CONN14, 26, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_o15_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I12 Switch", AFE_CONN15, 12, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_o16_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I13 Switch", AFE_CONN16, 13, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_o17_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I14 Switch", AFE_CONN17, 14, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_o18_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I15 Switch", AFE_CONN18, 15, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_o19_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I16 Switch", AFE_CONN19, 16, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_o20_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I17 Switch", AFE_CONN20, 17, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_o21_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I18 Switch", AFE_CONN21, 18, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_o22_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I19 Switch", AFE_CONN22, 19, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_o31_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I35 Switch", AFE_CONN41, 9, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_i02_mix[] = {
	SOC_DAPM_SINGLE("I2S0 Switch", SND_SOC_NOPM, 0, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_multi_ch_out_i2s0[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("Multich I2S0 Out Switch",
				    ASYS_I2SO1_CON, 26, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_multi_ch_out_i2s1[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("Multich I2S1 Out Switch",
				    ASYS_I2SO2_CON, 26, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_multi_ch_out_i2s2[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("Multich I2S2 Out Switch",
				    PWR2_TOP_CON, 17, 1, 0),
};

static const struct snd_kcontrol_new mt2701_afe_multi_ch_out_i2s3[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("Multich I2S3 Out Switch",
				    PWR2_TOP_CON, 18, 1, 0),
};

static const struct snd_soc_dapm_widget mt2701_afe_pcm_widgets[] = {
	/* inter-connections */
	SND_SOC_DAPM_MIXER("I00", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I01", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I02", SND_SOC_NOPM, 0, 0, mt2701_afe_i02_mix,
			   ARRAY_SIZE(mt2701_afe_i02_mix)),
	SND_SOC_DAPM_MIXER("I03", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I12", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I13", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I14", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I15", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I16", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I17", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I18", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I19", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I26", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I35", SND_SOC_NOPM, 0, 0, NULL, 0),

	SND_SOC_DAPM_MIXER("O00", SND_SOC_NOPM, 0, 0, mt2701_afe_o00_mix,
			   ARRAY_SIZE(mt2701_afe_o00_mix)),
	SND_SOC_DAPM_MIXER("O01", SND_SOC_NOPM, 0, 0, mt2701_afe_o01_mix,
			   ARRAY_SIZE(mt2701_afe_o01_mix)),
	SND_SOC_DAPM_MIXER("O02", SND_SOC_NOPM, 0, 0, mt2701_afe_o02_mix,
			   ARRAY_SIZE(mt2701_afe_o02_mix)),
	SND_SOC_DAPM_MIXER("O03", SND_SOC_NOPM, 0, 0, mt2701_afe_o03_mix,
			   ARRAY_SIZE(mt2701_afe_o03_mix)),
	SND_SOC_DAPM_MIXER("O14", SND_SOC_NOPM, 0, 0, mt2701_afe_o14_mix,
			   ARRAY_SIZE(mt2701_afe_o14_mix)),
	SND_SOC_DAPM_MIXER("O15", SND_SOC_NOPM, 0, 0, mt2701_afe_o15_mix,
			   ARRAY_SIZE(mt2701_afe_o15_mix)),
	SND_SOC_DAPM_MIXER("O16", SND_SOC_NOPM, 0, 0, mt2701_afe_o16_mix,
			   ARRAY_SIZE(mt2701_afe_o16_mix)),
	SND_SOC_DAPM_MIXER("O17", SND_SOC_NOPM, 0, 0, mt2701_afe_o17_mix,
			   ARRAY_SIZE(mt2701_afe_o17_mix)),
	SND_SOC_DAPM_MIXER("O18", SND_SOC_NOPM, 0, 0, mt2701_afe_o18_mix,
			   ARRAY_SIZE(mt2701_afe_o18_mix)),
	SND_SOC_DAPM_MIXER("O19", SND_SOC_NOPM, 0, 0, mt2701_afe_o19_mix,
			   ARRAY_SIZE(mt2701_afe_o19_mix)),
	SND_SOC_DAPM_MIXER("O20", SND_SOC_NOPM, 0, 0, mt2701_afe_o20_mix,
			   ARRAY_SIZE(mt2701_afe_o20_mix)),
	SND_SOC_DAPM_MIXER("O21", SND_SOC_NOPM, 0, 0, mt2701_afe_o21_mix,
			   ARRAY_SIZE(mt2701_afe_o21_mix)),
	SND_SOC_DAPM_MIXER("O22", SND_SOC_NOPM, 0, 0, mt2701_afe_o22_mix,
			   ARRAY_SIZE(mt2701_afe_o22_mix)),
	SND_SOC_DAPM_MIXER("O31", SND_SOC_NOPM, 0, 0, mt2701_afe_o31_mix,
			   ARRAY_SIZE(mt2701_afe_o31_mix)),

	SND_SOC_DAPM_MIXER("I12I13", SND_SOC_NOPM, 0, 0,
			   mt2701_afe_multi_ch_out_i2s0,
			   ARRAY_SIZE(mt2701_afe_multi_ch_out_i2s0)),
	SND_SOC_DAPM_MIXER("I14I15", SND_SOC_NOPM, 0, 0,
			   mt2701_afe_multi_ch_out_i2s1,
			   ARRAY_SIZE(mt2701_afe_multi_ch_out_i2s1)),
	SND_SOC_DAPM_MIXER("I16I17", SND_SOC_NOPM, 0, 0,
			   mt2701_afe_multi_ch_out_i2s2,
			   ARRAY_SIZE(mt2701_afe_multi_ch_out_i2s2)),
	SND_SOC_DAPM_MIXER("I18I19", SND_SOC_NOPM, 0, 0,
			   mt2701_afe_multi_ch_out_i2s3,
			   ARRAY_SIZE(mt2701_afe_multi_ch_out_i2s3)),
};

static const struct snd_soc_dapm_route mt2701_afe_pcm_routes[] = {
	{"I12", NULL, "DL1"},
	{"I13", NULL, "DL1"},
	{"I35", NULL, "DLBT"},

	{"I2S0 Playback", NULL, "O15"},
	{"I2S0 Playback", NULL, "O16"},
	{"I2S1 Playback", NULL, "O17"},
	{"I2S1 Playback", NULL, "O18"},
	{"I2S2 Playback", NULL, "O19"},
	{"I2S2 Playback", NULL, "O20"},
	{"I2S3 Playback", NULL, "O21"},
	{"I2S3 Playback", NULL, "O22"},
	{"BT Playback", NULL, "O31"},

	{"UL1", NULL, "O00"},
	{"UL1", NULL, "O01"},
	{"UL2", NULL, "O02"},
	{"UL2", NULL, "O03"},
	{"ULBT", NULL, "O14"},

	{"I00", NULL, "I2S0 Capture"},
	{"I01", NULL, "I2S0 Capture"},
	{"I02", NULL, "I2S1 Capture"},
	{"I03", NULL, "I2S1 Capture"},
	/* I02,03 link to UL2, also need to open I2S0 */
	{"I02", "I2S0 Switch", "I2S0 Capture"},

	{"I26", NULL, "BT Capture"},

	{"I12I13", "Multich I2S0 Out Switch", "DLM"},
	{"I14I15", "Multich I2S1 Out Switch", "DLM"},
	{"I16I17", "Multich I2S2 Out Switch", "DLM"},
	{"I18I19", "Multich I2S3 Out Switch", "DLM"},

	{ "I12", NULL, "I12I13" },
	{ "I13", NULL, "I12I13" },
	{ "I14", NULL, "I14I15" },
	{ "I15", NULL, "I14I15" },
	{ "I16", NULL, "I16I17" },
	{ "I17", NULL, "I16I17" },
	{ "I18", NULL, "I18I19" },
	{ "I19", NULL, "I18I19" },

	{ "O00", "I00 Switch", "I00" },
	{ "O01", "I01 Switch", "I01" },
	{ "O02", "I02 Switch", "I02" },
	{ "O03", "I03 Switch", "I03" },
	{ "O14", "I26 Switch", "I26" },
	{ "O15", "I12 Switch", "I12" },
	{ "O16", "I13 Switch", "I13" },
	{ "O17", "I14 Switch", "I14" },
	{ "O18", "I15 Switch", "I15" },
	{ "O19", "I16 Switch", "I16" },
	{ "O20", "I17 Switch", "I17" },
	{ "O21", "I18 Switch", "I18" },
	{ "O22", "I19 Switch", "I19" },
	{ "O31", "I35 Switch", "I35" },
};

static int mt2701_afe_pcm_probe(struct snd_soc_component *component)
{
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);

	snd_soc_component_init_regmap(component, afe->regmap);

	return 0;
}

static const struct snd_soc_component_driver mt2701_afe_pcm_dai_component = {
	.probe = mt2701_afe_pcm_probe,
	.name = "mt2701-afe-pcm-dai",
	.dapm_widgets = mt2701_afe_pcm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt2701_afe_pcm_widgets),
	.dapm_routes = mt2701_afe_pcm_routes,
	.num_dapm_routes = ARRAY_SIZE(mt2701_afe_pcm_routes),
	.suspend = mtk_afe_suspend,
	.resume = mtk_afe_resume,
};

static const struct mtk_base_memif_data memif_data[MT2701_MEMIF_NUM] = {
	{
		.name = "DL1",
		.id = MT2701_MEMIF_DL1,
		.reg_ofs_base = AFE_DL1_BASE,
		.reg_ofs_cur = AFE_DL1_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = 0,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_DAC_CON3,
		.mono_shift = 16,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 1,
		.hd_reg = AFE_MEMIF_HD_CON0,
		.hd_shift = 0,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 6,
		.msb_reg = -1,
	},
	{
		.name = "DL2",
		.id = MT2701_MEMIF_DL2,
		.reg_ofs_base = AFE_DL2_BASE,
		.reg_ofs_cur = AFE_DL2_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = 5,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_DAC_CON3,
		.mono_shift = 17,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 2,
		.hd_reg = AFE_MEMIF_HD_CON0,
		.hd_shift = 2,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 7,
		.msb_reg = -1,
	},
	{
		.name = "DL3",
		.id = MT2701_MEMIF_DL3,
		.reg_ofs_base = AFE_DL3_BASE,
		.reg_ofs_cur = AFE_DL3_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = 10,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_DAC_CON3,
		.mono_shift = 18,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 3,
		.hd_reg = AFE_MEMIF_HD_CON0,
		.hd_shift = 4,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 8,
		.msb_reg = -1,
	},
	{
		.name = "DL4",
		.id = MT2701_MEMIF_DL4,
		.reg_ofs_base = AFE_DL4_BASE,
		.reg_ofs_cur = AFE_DL4_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = 15,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_DAC_CON3,
		.mono_shift = 19,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 4,
		.hd_reg = AFE_MEMIF_HD_CON0,
		.hd_shift = 6,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 9,
		.msb_reg = -1,
	},
	{
		.name = "DL5",
		.id = MT2701_MEMIF_DL5,
		.reg_ofs_base = AFE_DL5_BASE,
		.reg_ofs_cur = AFE_DL5_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = 20,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_DAC_CON3,
		.mono_shift = 20,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 5,
		.hd_reg = AFE_MEMIF_HD_CON0,
		.hd_shift = 8,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 10,
		.msb_reg = -1,
	},
	{
		.name = "DLM",
		.id = MT2701_MEMIF_DLM,
		.reg_ofs_base = AFE_DLMCH_BASE,
		.reg_ofs_cur = AFE_DLMCH_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = 0,
		.fs_maskbit = 0x1f,
		.mono_reg = -1,
		.mono_shift = -1,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 7,
		.hd_reg = AFE_MEMIF_PBUF_SIZE,
		.hd_shift = 28,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 12,
		.msb_reg = -1,
	},
	{
		.name = "UL1",
		.id = MT2701_MEMIF_UL1,
		.reg_ofs_base = AFE_VUL_BASE,
		.reg_ofs_cur = AFE_VUL_CUR,
		.fs_reg = AFE_DAC_CON2,
		.fs_shift = 0,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_DAC_CON4,
		.mono_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 10,
		.hd_reg = AFE_MEMIF_HD_CON1,
		.hd_shift = 0,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 0,
		.msb_reg = -1,
	},
	{
		.name = "UL2",
		.id = MT2701_MEMIF_UL2,
		.reg_ofs_base = AFE_UL2_BASE,
		.reg_ofs_cur = AFE_UL2_CUR,
		.fs_reg = AFE_DAC_CON2,
		.fs_shift = 5,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_DAC_CON4,
		.mono_shift = 2,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 11,
		.hd_reg = AFE_MEMIF_HD_CON1,
		.hd_shift = 2,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 1,
		.msb_reg = -1,
	},
	{
		.name = "UL3",
		.id = MT2701_MEMIF_UL3,
		.reg_ofs_base = AFE_UL3_BASE,
		.reg_ofs_cur = AFE_UL3_CUR,
		.fs_reg = AFE_DAC_CON2,
		.fs_shift = 10,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_DAC_CON4,
		.mono_shift = 4,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 12,
		.hd_reg = AFE_MEMIF_HD_CON0,
		.hd_shift = 0,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 2,
		.msb_reg = -1,
	},
	{
		.name = "UL4",
		.id = MT2701_MEMIF_UL4,
		.reg_ofs_base = AFE_UL4_BASE,
		.reg_ofs_cur = AFE_UL4_CUR,
		.fs_reg = AFE_DAC_CON2,
		.fs_shift = 15,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_DAC_CON4,
		.mono_shift = 6,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 13,
		.hd_reg = AFE_MEMIF_HD_CON0,
		.hd_shift = 6,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 3,
		.msb_reg = -1,
	},
	{
		.name = "UL5",
		.id = MT2701_MEMIF_UL5,
		.reg_ofs_base = AFE_UL5_BASE,
		.reg_ofs_cur = AFE_UL5_CUR,
		.fs_reg = AFE_DAC_CON2,
		.fs_shift = 20,
		.mono_reg = AFE_DAC_CON4,
		.mono_shift = 8,
		.fs_maskbit = 0x1f,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 14,
		.hd_reg = AFE_MEMIF_HD_CON0,
		.hd_shift = 8,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 4,
		.msb_reg = -1,
	},
	{
		.name = "DLBT",
		.id = MT2701_MEMIF_DLBT,
		.reg_ofs_base = AFE_ARB1_BASE,
		.reg_ofs_cur = AFE_ARB1_CUR,
		.fs_reg = AFE_DAC_CON3,
		.fs_shift = 10,
		.fs_maskbit = 0x1f,
		.mono_reg = AFE_DAC_CON3,
		.mono_shift = 22,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 8,
		.hd_reg = AFE_MEMIF_HD_CON0,
		.hd_shift = 14,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 13,
		.msb_reg = -1,
	},
	{
		.name = "ULBT",
		.id = MT2701_MEMIF_ULBT,
		.reg_ofs_base = AFE_DAI_BASE,
		.reg_ofs_cur = AFE_DAI_CUR,
		.fs_reg = AFE_DAC_CON2,
		.fs_shift = 30,
		.fs_maskbit = 0x1,
		.mono_reg = -1,
		.mono_shift = -1,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 17,
		.hd_reg = AFE_MEMIF_HD_CON1,
		.hd_shift = 20,
		.agent_disable_reg = AUDIO_TOP_CON5,
		.agent_disable_shift = 16,
		.msb_reg = -1,
	},
};

static const struct mtk_base_irq_data irq_data[MT2701_IRQ_ASYS_END] = {
	{
		.id = MT2701_IRQ_ASYS_IRQ1,
		.irq_cnt_reg = ASYS_IRQ1_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ1_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1f,
		.irq_en_reg = ASYS_IRQ1_CON,
		.irq_en_shift = 31,
		.irq_clr_reg = ASYS_IRQ_CLR,
		.irq_clr_shift = 0,
	},
	{
		.id = MT2701_IRQ_ASYS_IRQ2,
		.irq_cnt_reg = ASYS_IRQ2_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ2_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1f,
		.irq_en_reg = ASYS_IRQ2_CON,
		.irq_en_shift = 31,
		.irq_clr_reg = ASYS_IRQ_CLR,
		.irq_clr_shift = 1,
	},
	{
		.id = MT2701_IRQ_ASYS_IRQ3,
		.irq_cnt_reg = ASYS_IRQ3_CON,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0xffffff,
		.irq_fs_reg = ASYS_IRQ3_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0x1f,
		.irq_en_reg = ASYS_IRQ3_CON,
		.irq_en_shift = 31,
		.irq_clr_reg = ASYS_IRQ_CLR,
		.irq_clr_shift = 2,
	}
};

static const struct mt2701_i2s_data mt2701_i2s_data[][2] = {
	{
		{ ASYS_I2SO1_CON, 0, 0x1f },
		{ ASYS_I2SIN1_CON, 0, 0x1f },
	},
	{
		{ ASYS_I2SO2_CON, 5, 0x1f },
		{ ASYS_I2SIN2_CON, 5, 0x1f },
	},
	{
		{ ASYS_I2SO3_CON, 10, 0x1f },
		{ ASYS_I2SIN3_CON, 10, 0x1f },
	},
	{
		{ ASYS_I2SO4_CON, 15, 0x1f },
		{ ASYS_I2SIN4_CON, 15, 0x1f },
	},
	/* TODO - extend control registers supported by newer SoCs */
};

static irqreturn_t mt2701_asys_isr(int irq_id, void *dev)
{
	int id;
	struct mtk_base_afe *afe = dev;
	struct mtk_base_afe_memif *memif;
	struct mtk_base_afe_irq *irq;
	u32 status;

	regmap_read(afe->regmap, ASYS_IRQ_STATUS, &status);
	regmap_write(afe->regmap, ASYS_IRQ_CLR, status);

	for (id = 0; id < MT2701_MEMIF_NUM; ++id) {
		memif = &afe->memif[id];
		if (memif->irq_usage < 0)
			continue;

		irq = &afe->irqs[memif->irq_usage];
		if (status & 1 << irq->irq_data->irq_clr_shift)
			snd_pcm_period_elapsed(memif->substream);
	}

	return IRQ_HANDLED;
}

static int mt2701_afe_runtime_suspend(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);

	return mt2701_afe_disable_clock(afe);
}

static int mt2701_afe_runtime_resume(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);

	return mt2701_afe_enable_clock(afe);
}

static int mt2701_afe_pcm_dev_probe(struct platform_device *pdev)
{
	struct mtk_base_afe *afe;
	struct mt2701_afe_private *afe_priv;
	struct device *dev;
	int i, irq_id, ret;

	afe = devm_kzalloc(&pdev->dev, sizeof(*afe), GFP_KERNEL);
	if (!afe)
		return -ENOMEM;

	afe->platform_priv = devm_kzalloc(&pdev->dev, sizeof(*afe_priv),
					  GFP_KERNEL);
	if (!afe->platform_priv)
		return -ENOMEM;

	afe_priv = afe->platform_priv;
	afe_priv->soc = of_device_get_match_data(&pdev->dev);
	afe->dev = &pdev->dev;
	dev = afe->dev;

	afe_priv->i2s_path = devm_kcalloc(dev,
					  afe_priv->soc->i2s_num,
					  sizeof(struct mt2701_i2s_path),
					  GFP_KERNEL);
	if (!afe_priv->i2s_path)
		return -ENOMEM;

	irq_id = platform_get_irq_byname(pdev, "asys");
	if (irq_id < 0)
		return irq_id;

	ret = devm_request_irq(dev, irq_id, mt2701_asys_isr,
			       IRQF_TRIGGER_NONE, "asys-isr", (void *)afe);
	if (ret) {
		dev_err(dev, "could not request_irq for asys-isr\n");
		return ret;
	}

	afe->regmap = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(afe->regmap)) {
		dev_err(dev, "could not get regmap from parent\n");
		return PTR_ERR(afe->regmap);
	}

	mutex_init(&afe->irq_alloc_lock);

	/* memif initialize */
	afe->memif_size = MT2701_MEMIF_NUM;
	afe->memif = devm_kcalloc(dev, afe->memif_size, sizeof(*afe->memif),
				  GFP_KERNEL);
	if (!afe->memif)
		return -ENOMEM;

	for (i = 0; i < afe->memif_size; i++) {
		afe->memif[i].data = &memif_data[i];
		afe->memif[i].irq_usage = -1;
	}

	/* irq initialize */
	afe->irqs_size = MT2701_IRQ_ASYS_END;
	afe->irqs = devm_kcalloc(dev, afe->irqs_size, sizeof(*afe->irqs),
				 GFP_KERNEL);
	if (!afe->irqs)
		return -ENOMEM;

	for (i = 0; i < afe->irqs_size; i++)
		afe->irqs[i].irq_data = &irq_data[i];

	/* I2S initialize */
	for (i = 0; i < afe_priv->soc->i2s_num; i++) {
		afe_priv->i2s_path[i].i2s_data[SNDRV_PCM_STREAM_PLAYBACK] =
			&mt2701_i2s_data[i][SNDRV_PCM_STREAM_PLAYBACK];
		afe_priv->i2s_path[i].i2s_data[SNDRV_PCM_STREAM_CAPTURE] =
			&mt2701_i2s_data[i][SNDRV_PCM_STREAM_CAPTURE];
	}

	afe->mtk_afe_hardware = &mt2701_afe_hardware;
	afe->memif_fs = mt2701_memif_fs;
	afe->irq_fs = mt2701_irq_fs;
	afe->reg_back_up_list = mt2701_afe_backup_list;
	afe->reg_back_up_list_num = ARRAY_SIZE(mt2701_afe_backup_list);
	afe->runtime_resume = mt2701_afe_runtime_resume;
	afe->runtime_suspend = mt2701_afe_runtime_suspend;

	/* initial audio related clock */
	ret = mt2701_init_clock(afe);
	if (ret) {
		dev_err(dev, "init clock error\n");
		return ret;
	}

	platform_set_drvdata(pdev, afe);

	pm_runtime_enable(dev);
	if (!pm_runtime_enabled(dev)) {
		ret = mt2701_afe_runtime_resume(dev);
		if (ret)
			goto err_pm_disable;
	}
	pm_runtime_get_sync(dev);

	ret = devm_snd_soc_register_component(&pdev->dev, &mtk_afe_pcm_platform,
					      NULL, 0);
	if (ret) {
		dev_warn(dev, "err_platform\n");
		goto err_platform;
	}

	ret = devm_snd_soc_register_component(&pdev->dev,
					 &mt2701_afe_pcm_dai_component,
					 mt2701_afe_pcm_dais,
					 ARRAY_SIZE(mt2701_afe_pcm_dais));
	if (ret) {
		dev_warn(dev, "err_dai_component\n");
		goto err_platform;
	}

	return 0;

err_platform:
	pm_runtime_put_sync(dev);
err_pm_disable:
	pm_runtime_disable(dev);

	return ret;
}

static int mt2701_afe_pcm_dev_remove(struct platform_device *pdev)
{
	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		mt2701_afe_runtime_suspend(&pdev->dev);

	return 0;
}

static const struct mt2701_soc_variants mt2701_soc_v1 = {
	.i2s_num = 4,
};

static const struct mt2701_soc_variants mt2701_soc_v2 = {
	.has_one_heart_mode = true,
	.i2s_num = 4,
};

static const struct of_device_id mt2701_afe_pcm_dt_match[] = {
	{ .compatible = "mediatek,mt2701-audio", .data = &mt2701_soc_v1 },
	{ .compatible = "mediatek,mt7622-audio", .data = &mt2701_soc_v2 },
	{},
};
MODULE_DEVICE_TABLE(of, mt2701_afe_pcm_dt_match);

static const struct dev_pm_ops mt2701_afe_pm_ops = {
	SET_RUNTIME_PM_OPS(mt2701_afe_runtime_suspend,
			   mt2701_afe_runtime_resume, NULL)
};

static struct platform_driver mt2701_afe_pcm_driver = {
	.driver = {
		   .name = "mt2701-audio",
		   .of_match_table = mt2701_afe_pcm_dt_match,
#ifdef CONFIG_PM
		   .pm = &mt2701_afe_pm_ops,
#endif
	},
	.probe = mt2701_afe_pcm_dev_probe,
	.remove = mt2701_afe_pcm_dev_remove,
};

module_platform_driver(mt2701_afe_pcm_driver);

MODULE_DESCRIPTION("Mediatek ALSA SoC AFE platform driver for 2701");
MODULE_AUTHOR("Garlic Tseng <garlic.tseng@mediatek.com>");
MODULE_LICENSE("GPL v2");
