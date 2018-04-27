// SPDX-License-Identifier: GPL-2.0
//
// Mediatek ALSA SoC AFE platform driver for 6797
//
// Copyright (c) 2018 MediaTek Inc.
// Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>

#include "mt6797-afe-common.h"
#include "mt6797-afe-clk.h"
#include "mt6797-interconnection.h"
#include "mt6797-reg.h"
#include "../common/mtk-afe-platform-driver.h"
#include "../common/mtk-afe-fe-dai.h"

enum {
	MTK_AFE_RATE_8K = 0,
	MTK_AFE_RATE_11K = 1,
	MTK_AFE_RATE_12K = 2,
	MTK_AFE_RATE_384K = 3,
	MTK_AFE_RATE_16K = 4,
	MTK_AFE_RATE_22K = 5,
	MTK_AFE_RATE_24K = 6,
	MTK_AFE_RATE_130K = 7,
	MTK_AFE_RATE_32K = 8,
	MTK_AFE_RATE_44K = 9,
	MTK_AFE_RATE_48K = 10,
	MTK_AFE_RATE_88K = 11,
	MTK_AFE_RATE_96K = 12,
	MTK_AFE_RATE_174K = 13,
	MTK_AFE_RATE_192K = 14,
	MTK_AFE_RATE_260K = 15,
};

enum {
	MTK_AFE_DAI_MEMIF_RATE_8K = 0,
	MTK_AFE_DAI_MEMIF_RATE_16K = 1,
	MTK_AFE_DAI_MEMIF_RATE_32K = 2,
};

enum {
	MTK_AFE_PCM_RATE_8K = 0,
	MTK_AFE_PCM_RATE_16K = 1,
	MTK_AFE_PCM_RATE_32K = 2,
	MTK_AFE_PCM_RATE_48K = 3,
};

unsigned int mt6797_general_rate_transform(struct device *dev,
					   unsigned int rate)
{
	switch (rate) {
	case 8000:
		return MTK_AFE_RATE_8K;
	case 11025:
		return MTK_AFE_RATE_11K;
	case 12000:
		return MTK_AFE_RATE_12K;
	case 16000:
		return MTK_AFE_RATE_16K;
	case 22050:
		return MTK_AFE_RATE_22K;
	case 24000:
		return MTK_AFE_RATE_24K;
	case 32000:
		return MTK_AFE_RATE_32K;
	case 44100:
		return MTK_AFE_RATE_44K;
	case 48000:
		return MTK_AFE_RATE_48K;
	case 88200:
		return MTK_AFE_RATE_88K;
	case 96000:
		return MTK_AFE_RATE_96K;
	case 130000:
		return MTK_AFE_RATE_130K;
	case 176400:
		return MTK_AFE_RATE_174K;
	case 192000:
		return MTK_AFE_RATE_192K;
	case 260000:
		return MTK_AFE_RATE_260K;
	default:
		dev_warn(dev, "%s(), rate %u invalid, use %d!!!\n",
			 __func__, rate, MTK_AFE_RATE_48K);
		return MTK_AFE_RATE_48K;
	}
}

static unsigned int dai_memif_rate_transform(struct device *dev,
					     unsigned int rate)
{
	switch (rate) {
	case 8000:
		return MTK_AFE_DAI_MEMIF_RATE_8K;
	case 16000:
		return MTK_AFE_DAI_MEMIF_RATE_16K;
	case 32000:
		return MTK_AFE_DAI_MEMIF_RATE_32K;
	default:
		dev_warn(dev, "%s(), rate %u invalid, use %d!!!\n",
			 __func__, rate, MTK_AFE_DAI_MEMIF_RATE_16K);
		return MTK_AFE_DAI_MEMIF_RATE_16K;
	}
}

unsigned int mt6797_rate_transform(struct device *dev,
				   unsigned int rate, int aud_blk)
{
	switch (aud_blk) {
	case MT6797_MEMIF_DAI:
	case MT6797_MEMIF_MOD_DAI:
		return dai_memif_rate_transform(dev, rate);
	default:
		return mt6797_general_rate_transform(dev, rate);
	}
}

static const struct snd_pcm_hardware mt6797_afe_hardware = {
	.info = SNDRV_PCM_INFO_MMAP |
		SNDRV_PCM_INFO_INTERLEAVED |
		SNDRV_PCM_INFO_MMAP_VALID,
	.formats = SNDRV_PCM_FMTBIT_S16_LE |
		   SNDRV_PCM_FMTBIT_S24_LE |
		   SNDRV_PCM_FMTBIT_S32_LE,
	.period_bytes_min = 256,
	.period_bytes_max = 4 * 48 * 1024,
	.periods_min = 2,
	.periods_max = 256,
	.buffer_bytes_max = 8 * 48 * 1024,
	.fifo_size = 0,
};

static int mt6797_memif_fs(struct snd_pcm_substream *substream,
			   unsigned int rate)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	int id = rtd->cpu_dai->id;

	return mt6797_rate_transform(afe->dev, rate, id);
}

static int mt6797_irq_fs(struct snd_pcm_substream *substream, unsigned int rate)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);

	return mt6797_general_rate_transform(afe->dev, rate);
}

/* ADDA BE DAIs */
enum {
	MTK_AFE_ADDA_DL_RATE_8K = 0,
	MTK_AFE_ADDA_DL_RATE_11K = 1,
	MTK_AFE_ADDA_DL_RATE_12K = 2,
	MTK_AFE_ADDA_DL_RATE_16K = 3,
	MTK_AFE_ADDA_DL_RATE_22K = 4,
	MTK_AFE_ADDA_DL_RATE_24K = 5,
	MTK_AFE_ADDA_DL_RATE_32K = 6,
	MTK_AFE_ADDA_DL_RATE_44K = 7,
	MTK_AFE_ADDA_DL_RATE_48K = 8,
	MTK_AFE_ADDA_DL_RATE_96K = 9,
	MTK_AFE_ADDA_DL_RATE_192K = 10,
};

enum {
	MTK_AFE_ADDA_UL_RATE_8K = 0,
	MTK_AFE_ADDA_UL_RATE_16K = 1,
	MTK_AFE_ADDA_UL_RATE_32K = 2,
	MTK_AFE_ADDA_UL_RATE_48K = 3,
	MTK_AFE_ADDA_UL_RATE_96K = 4,
	MTK_AFE_ADDA_UL_RATE_192K = 5,
	MTK_AFE_ADDA_UL_RATE_48K_HD = 6,
};

static unsigned int adda_dl_rate_transform(struct mtk_base_afe *afe,
					   unsigned int rate)
{
	switch (rate) {
	case 8000:
		return MTK_AFE_ADDA_DL_RATE_8K;
	case 11025:
		return MTK_AFE_ADDA_DL_RATE_11K;
	case 12000:
		return MTK_AFE_ADDA_DL_RATE_12K;
	case 16000:
		return MTK_AFE_ADDA_DL_RATE_16K;
	case 22050:
		return MTK_AFE_ADDA_DL_RATE_22K;
	case 24000:
		return MTK_AFE_ADDA_DL_RATE_24K;
	case 32000:
		return MTK_AFE_ADDA_DL_RATE_32K;
	case 44100:
		return MTK_AFE_ADDA_DL_RATE_44K;
	case 48000:
		return MTK_AFE_ADDA_DL_RATE_48K;
	case 96000:
		return MTK_AFE_ADDA_DL_RATE_96K;
	case 192000:
		return MTK_AFE_ADDA_DL_RATE_192K;
	default:
		dev_warn(afe->dev, "%s(), rate %d invalid, use 48kHz!!!\n",
			 __func__, rate);
		return MTK_AFE_ADDA_DL_RATE_48K;
	}
}

static unsigned int adda_ul_rate_transform(struct mtk_base_afe *afe,
					   unsigned int rate)
{
	switch (rate) {
	case 8000:
		return MTK_AFE_ADDA_UL_RATE_8K;
	case 16000:
		return MTK_AFE_ADDA_UL_RATE_16K;
	case 32000:
		return MTK_AFE_ADDA_UL_RATE_32K;
	case 48000:
		return MTK_AFE_ADDA_UL_RATE_48K;
	case 96000:
		return MTK_AFE_ADDA_UL_RATE_96K;
	case 192000:
		return MTK_AFE_ADDA_UL_RATE_192K;
	default:
		dev_warn(afe->dev, "%s(), rate %d invalid, use 48kHz!!!\n",
			 __func__, rate);
		return MTK_AFE_ADDA_UL_RATE_48K;
	}
}

static int mtk_dai_adda_hw_params(struct snd_pcm_substream *substream,
				  struct snd_pcm_hw_params *params,
				  struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	unsigned int rate = params_rate(params);

	dev_dbg(afe->dev, "%s(), id %d, stream %d, rate %d\n",
		__func__, dai->id, substream->stream, rate);

	if (substream->stream == SNDRV_PCM_STREAM_PLAYBACK) {
		unsigned int dl_src2_con0 = 0;
		unsigned int dl_src2_con1 = 0;

		/* clean predistortion */
		regmap_write(afe->regmap, AFE_ADDA_PREDIS_CON0, 0);
		regmap_write(afe->regmap, AFE_ADDA_PREDIS_CON1, 0);

		/* set input sampling rate */
		dl_src2_con0 = adda_dl_rate_transform(afe, rate) << 28;

		/* set output mode */
		switch (rate) {
		case 192000:
			dl_src2_con0 |= (0x1 << 24); /* UP_SAMPLING_RATE_X2 */
			dl_src2_con0 |= 1 << 14;
			break;
		case 96000:
			dl_src2_con0 |= (0x2 << 24); /* UP_SAMPLING_RATE_X4 */
			dl_src2_con0 |= 1 << 14;
			break;
		default:
			dl_src2_con0 |= (0x3 << 24); /* UP_SAMPLING_RATE_X8 */
			break;
		}

		/* turn off mute function */
		dl_src2_con0 |= (0x03 << 11);

		/* set voice input data if input sample rate is 8k or 16k */
		if (rate == 8000 || rate == 16000)
			dl_src2_con0 |= 0x01 << 5;

		if (rate < 96000) {
			/* SA suggest apply -0.3db to audio/speech path */
			dl_src2_con1 = 0xf74f0000;
		} else {
			/* SA suggest apply -0.3db to audio/speech path
			 * with DL gain set to half,
			 * 0xFFFF = 0dB -> 0x8000 = 0dB when 96k, 192k
			 */
			dl_src2_con1 = 0x7ba70000;
		}

		/* turn on down-link gain */
		dl_src2_con0 = dl_src2_con0 | (0x01 << 1);

		regmap_write(afe->regmap, AFE_ADDA_DL_SRC2_CON0, dl_src2_con0);
		regmap_write(afe->regmap, AFE_ADDA_DL_SRC2_CON1, dl_src2_con1);
	} else {
		unsigned int voice_mode = 0;
		unsigned int ul_src_con0 = 0;	/* default value */

		/* Using Internal ADC */
		regmap_update_bits(afe->regmap,
				   AFE_ADDA_TOP_CON0,
				   0x1 << 0,
				   0x0 << 0);

		voice_mode = adda_ul_rate_transform(afe, rate);

		ul_src_con0 |= (voice_mode << 17) & (0x7 << 17);

		/* up8x txif sat on */
		regmap_write(afe->regmap, AFE_ADDA_NEWIF_CFG0, 0x03F87201);

		if (rate >= 96000) {	/* hires */
			/* use hires format [1 0 23] */
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_NEWIF_CFG0,
					   0x1 << 5,
					   0x1 << 5);

			regmap_update_bits(afe->regmap,
					   AFE_ADDA_NEWIF_CFG2,
					   0xf << 28,
					   voice_mode << 28);
		} else {	/* normal 8~48k */
			/* use fixed 260k anc path */
			regmap_update_bits(afe->regmap,
					   AFE_ADDA_NEWIF_CFG2,
					   0xf << 28,
					   8 << 28);

			/* ul_use_cic_out */
			ul_src_con0 |= 0x1 << 20;
		}

		regmap_update_bits(afe->regmap,
				   AFE_ADDA_NEWIF_CFG2,
				   0xf << 28,
				   8 << 28);

		regmap_update_bits(afe->regmap,
				   AFE_ADDA_UL_SRC_CON0,
				   0xfffffffe,
				   ul_src_con0);
	}

	return 0;
}

static const struct snd_soc_dai_ops mtk_dai_adda_ops = {
	.hw_params = mtk_dai_adda_hw_params,
};

#define MTK_PCM_RATES (SNDRV_PCM_RATE_8000_48000 |\
		       SNDRV_PCM_RATE_88200 |\
		       SNDRV_PCM_RATE_96000 |\
		       SNDRV_PCM_RATE_176400 |\
		       SNDRV_PCM_RATE_192000)

#define MTK_PCM_DAI_RATES (SNDRV_PCM_RATE_8000 |\
			   SNDRV_PCM_RATE_16000 |\
			   SNDRV_PCM_RATE_32000)

#define MTK_PCM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

#define MTK_ADDA_PLAYBACK_RATES (SNDRV_PCM_RATE_8000_48000 |\
				 SNDRV_PCM_RATE_96000 |\
				 SNDRV_PCM_RATE_192000)

#define MTK_ADDA_CAPTURE_RATES (SNDRV_PCM_RATE_8000 |\
				SNDRV_PCM_RATE_16000 |\
				SNDRV_PCM_RATE_32000 |\
				SNDRV_PCM_RATE_48000 |\
				SNDRV_PCM_RATE_96000 |\
				SNDRV_PCM_RATE_192000)

#define MTK_ADDA_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			  SNDRV_PCM_FMTBIT_S24_LE |\
			  SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mt6797_afe_pcm_dais[] = {
	/* FE DAIs: memory intefaces to CPU */
	{
		.name = "DL1",
		.id = MT6797_MEMIF_DL1,
		.playback = {
			.stream_name = "DL1",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "DL2",
		.id = MT6797_MEMIF_DL2,
		.playback = {
			.stream_name = "DL2",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "DL3",
		.id = MT6797_MEMIF_DL3,
		.playback = {
			.stream_name = "DL3",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL1",
		.id = MT6797_MEMIF_VUL12,
		.capture = {
			.stream_name = "UL1",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL2",
		.id = MT6797_MEMIF_AWB,
		.capture = {
			.stream_name = "UL2",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL3",
		.id = MT6797_MEMIF_VUL,
		.capture = {
			.stream_name = "UL3",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL_MONO_1",
		.id = MT6797_MEMIF_MOD_DAI,
		.capture = {
			.stream_name = "UL_MONO_1",
			.channels_min = 1,
			.channels_max = 1,
			.rates = MTK_PCM_DAI_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL_MONO_2",
		.id = MT6797_MEMIF_DAI,
		.capture = {
			.stream_name = "UL_MONO_2",
			.channels_min = 1,
			.channels_max = 1,
			.rates = MTK_PCM_DAI_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	/* BE DAIs */
	{
		.name = "ADDA",
		.id = MT6797_DAI_ADDA,
		.playback = {
			.stream_name = "ADDA Playback",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ADDA_PLAYBACK_RATES,
			.formats = MTK_ADDA_FORMATS,
		},
		.capture = {
			.stream_name = "ADDA Capture",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_ADDA_CAPTURE_RATES,
			.formats = MTK_ADDA_FORMATS,
		},
		.ops = &mtk_dai_adda_ops,
	},
};

/* dma widget & routes*/
static const struct snd_kcontrol_new memif_ul1_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN21,
				    I_ADDA_UL_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul1_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN22,
				    I_ADDA_UL_CH2, 1, 0),
};

static const struct snd_kcontrol_new memif_ul2_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN5,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN5,
				    I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN5,
				    I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN5,
				    I_DL3_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul2_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN6,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2", AFE_CONN6,
				    I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN6,
				    I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2", AFE_CONN6,
				    I_DL3_CH2, 1, 0),
};

static const struct snd_kcontrol_new memif_ul3_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN9,
				    I_ADDA_UL_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul3_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN10,
				    I_ADDA_UL_CH2, 1, 0),
};

static const struct snd_kcontrol_new memif_ul_mono_1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN12,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN12,
				    I_ADDA_UL_CH2, 1, 0),
};

static const struct snd_kcontrol_new memif_ul_mono_2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN11,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN11,
				    I_ADDA_UL_CH2, 1, 0),
};

static const struct snd_kcontrol_new mtk_adda_dl_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN3, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN3, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN3, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN3,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN3,
				    I_ADDA_UL_CH1, 1, 0),
};

static const struct snd_kcontrol_new mtk_adda_dl_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN4, I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2", AFE_CONN4, I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN4, I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN4, I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN4, I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2", AFE_CONN4, I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN4,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN4,
				    I_ADDA_UL_CH1, 1, 0),
};

static int mtk_adda_ul_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);

	dev_dbg(afe->dev, "%s(), name %s, event 0x%x\n",
		__func__, w->name, event);

	switch (event) {
	case SND_SOC_DAPM_POST_PMD:
		/* should delayed 1/fs(smallest is 8k) = 125us before afe off */
		usleep_range(125, 135);
		break;
	default:
		break;
	}

	return 0;
}

enum {
	SUPPLY_SEQ_AUD_TOP_PDN,
	SUPPLY_SEQ_ADDA_AFE_ON,
	SUPPLY_SEQ_ADDA_DL_ON,
	SUPPLY_SEQ_ADDA_UL_ON,
};

static const struct snd_soc_dapm_widget mt6797_afe_pcm_widgets[] = {
	/* memif */
	SND_SOC_DAPM_MIXER("UL1_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul1_ch1_mix, ARRAY_SIZE(memif_ul1_ch1_mix)),
	SND_SOC_DAPM_MIXER("UL1_CH2", SND_SOC_NOPM, 0, 0,
			   memif_ul1_ch2_mix, ARRAY_SIZE(memif_ul1_ch2_mix)),

	SND_SOC_DAPM_MIXER("UL2_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul2_ch1_mix, ARRAY_SIZE(memif_ul2_ch1_mix)),
	SND_SOC_DAPM_MIXER("UL2_CH2", SND_SOC_NOPM, 0, 0,
			   memif_ul2_ch2_mix, ARRAY_SIZE(memif_ul2_ch2_mix)),

	SND_SOC_DAPM_MIXER("UL3_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul3_ch1_mix, ARRAY_SIZE(memif_ul3_ch1_mix)),
	SND_SOC_DAPM_MIXER("UL3_CH2", SND_SOC_NOPM, 0, 0,
			   memif_ul3_ch2_mix, ARRAY_SIZE(memif_ul3_ch2_mix)),

	SND_SOC_DAPM_MIXER("UL_MONO_1_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul_mono_1_mix,
			   ARRAY_SIZE(memif_ul_mono_1_mix)),

	SND_SOC_DAPM_MIXER("UL_MONO_2_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul_mono_2_mix,
			   ARRAY_SIZE(memif_ul_mono_2_mix)),

	/* adda */
	SND_SOC_DAPM_MIXER("ADDA_DL_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_adda_dl_ch1_mix,
			   ARRAY_SIZE(mtk_adda_dl_ch1_mix)),
	SND_SOC_DAPM_MIXER("ADDA_DL_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_adda_dl_ch2_mix,
			   ARRAY_SIZE(mtk_adda_dl_ch2_mix)),

	SND_SOC_DAPM_SUPPLY_S("ADDA Enable", SUPPLY_SEQ_ADDA_AFE_ON,
			      AFE_ADDA_UL_DL_CON0, ADDA_AFE_ON_SFT, 0,
			      NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("ADDA Playback Enable", SUPPLY_SEQ_ADDA_DL_ON,
			      AFE_ADDA_DL_SRC2_CON0,
			      DL_2_SRC_ON_TMP_CTL_PRE_SFT, 0,
			      NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("ADDA Capture Enable", SUPPLY_SEQ_ADDA_UL_ON,
			      AFE_ADDA_UL_SRC_CON0,
			      UL_SRC_ON_TMP_CTL_SFT, 0,
			      mtk_adda_ul_event,
			      SND_SOC_DAPM_POST_PMD),

	SND_SOC_DAPM_SUPPLY_S("aud_dac_clk", SUPPLY_SEQ_AUD_TOP_PDN,
			      AUDIO_TOP_CON0, PDN_DAC_SFT, 1,
			      NULL, 0),
	SND_SOC_DAPM_SUPPLY_S("aud_dac_predis_clk", SUPPLY_SEQ_AUD_TOP_PDN,
			      AUDIO_TOP_CON0, PDN_DAC_PREDIS_SFT, 1,
			      NULL, 0),

	SND_SOC_DAPM_SUPPLY_S("aud_adc_clk", SUPPLY_SEQ_AUD_TOP_PDN,
			      AUDIO_TOP_CON0, PDN_ADC_SFT, 1,
			      NULL, 0),

	SND_SOC_DAPM_CLOCK_SUPPLY("mtkaif_26m_clk"),
};

static const struct snd_soc_dapm_route mt6797_afe_pcm_routes[] = {
	/* capture */
	{"UL1", NULL, "UL1_CH1"},
	{"UL1", NULL, "UL1_CH2"},
	{"UL1_CH1", "ADDA_UL_CH1", "ADDA Capture"},
	{"UL1_CH2", "ADDA_UL_CH2", "ADDA Capture"},

	{"UL2", NULL, "UL2_CH1"},
	{"UL2", NULL, "UL2_CH2"},
	{"UL2_CH1", "ADDA_UL_CH1", "ADDA Capture"},
	{"UL2_CH2", "ADDA_UL_CH2", "ADDA Capture"},

	{"UL3", NULL, "UL3_CH1"},
	{"UL3", NULL, "UL3_CH2"},
	{"UL3_CH1", "ADDA_UL_CH1", "ADDA Capture"},
	{"UL3_CH2", "ADDA_UL_CH2", "ADDA Capture"},

	{"UL_MONO_1", NULL, "UL_MONO_1_CH1"},
	{"UL_MONO_1_CH1", "ADDA_UL_CH1", "ADDA Capture"},
	{"UL_MONO_1_CH1", "ADDA_UL_CH2", "ADDA Capture"},

	{"UL_MONO_2", NULL, "UL_MONO_2_CH1"},
	{"UL_MONO_2_CH1", "ADDA_UL_CH1", "ADDA Capture"},
	{"UL_MONO_2_CH1", "ADDA_UL_CH2", "ADDA Capture"},

	/* playback */
	{"ADDA_DL_CH1", "DL1_CH1", "DL1"},
	{"ADDA_DL_CH2", "DL1_CH1", "DL1"},
	{"ADDA_DL_CH2", "DL1_CH2", "DL1"},

	{"ADDA_DL_CH1", "DL2_CH1", "DL2"},
	{"ADDA_DL_CH2", "DL2_CH1", "DL2"},
	{"ADDA_DL_CH2", "DL2_CH2", "DL2"},

	{"ADDA_DL_CH1", "DL3_CH1", "DL3"},
	{"ADDA_DL_CH2", "DL3_CH1", "DL3"},
	{"ADDA_DL_CH2", "DL3_CH2", "DL3"},

	{"ADDA Playback", NULL, "ADDA_DL_CH1"},
	{"ADDA Playback", NULL, "ADDA_DL_CH2"},

	/* adda enable */
	{"ADDA Playback", NULL, "ADDA Enable"},
	{"ADDA Playback", NULL, "ADDA Playback Enable"},
	{"ADDA Capture", NULL, "ADDA Enable"},
	{"ADDA Capture", NULL, "ADDA Capture Enable"},

	/* clk */
	{"ADDA Playback", NULL, "mtkaif_26m_clk"},
	{"ADDA Playback", NULL, "aud_dac_clk"},
	{"ADDA Playback", NULL, "aud_dac_predis_clk"},

	{"ADDA Capture", NULL, "mtkaif_26m_clk"},
	{"ADDA Capture", NULL, "aud_adc_clk"},
};

static const struct snd_soc_component_driver mt6797_afe_pcm_dai_component = {
	.name = "mt6797-afe-pcm-dai",
	.dapm_widgets = mt6797_afe_pcm_widgets,
	.num_dapm_widgets = ARRAY_SIZE(mt6797_afe_pcm_widgets),
	.dapm_routes = mt6797_afe_pcm_routes,
	.num_dapm_routes = ARRAY_SIZE(mt6797_afe_pcm_routes),
};

static const struct mtk_base_memif_data memif_data[MT6797_MEMIF_NUM] = {
	[MT6797_MEMIF_DL1] = {
		.name = "DL1",
		.id = MT6797_MEMIF_DL1,
		.reg_ofs_base = AFE_DL1_BASE,
		.reg_ofs_cur = AFE_DL1_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = DL1_MODE_SFT,
		.fs_maskbit = DL1_MODE_MASK,
		.mono_reg = AFE_DAC_CON1,
		.mono_shift = DL1_DATA_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DL1_ON_SFT,
		.hd_reg = AFE_MEMIF_HD_MODE,
		.hd_shift = DL1_HD_SFT,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
		.msb_reg = -1,
		.msb_shift = -1,
	},
	[MT6797_MEMIF_DL2] = {
		.name = "DL2",
		.id = MT6797_MEMIF_DL2,
		.reg_ofs_base = AFE_DL2_BASE,
		.reg_ofs_cur = AFE_DL2_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = DL2_MODE_SFT,
		.fs_maskbit = DL2_MODE_MASK,
		.mono_reg = AFE_DAC_CON1,
		.mono_shift = DL2_DATA_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DL2_ON_SFT,
		.hd_reg = AFE_MEMIF_HD_MODE,
		.hd_shift = DL2_HD_SFT,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
		.msb_reg = -1,
		.msb_shift = -1,
	},
	[MT6797_MEMIF_DL3] = {
		.name = "DL3",
		.id = MT6797_MEMIF_DL3,
		.reg_ofs_base = AFE_DL3_BASE,
		.reg_ofs_cur = AFE_DL3_CUR,
		.fs_reg = AFE_DAC_CON0,
		.fs_shift = DL3_MODE_SFT,
		.fs_maskbit = DL3_MODE_MASK,
		.mono_reg = AFE_DAC_CON1,
		.mono_shift = DL3_DATA_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DL3_ON_SFT,
		.hd_reg = AFE_MEMIF_HD_MODE,
		.hd_shift = DL3_HD_SFT,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
		.msb_reg = -1,
		.msb_shift = -1,
	},
	[MT6797_MEMIF_VUL] = {
		.name = "VUL",
		.id = MT6797_MEMIF_VUL,
		.reg_ofs_base = AFE_VUL_BASE,
		.reg_ofs_cur = AFE_VUL_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = VUL_MODE_SFT,
		.fs_maskbit = VUL_MODE_MASK,
		.mono_reg = AFE_DAC_CON1,
		.mono_shift = VUL_DATA_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = VUL_ON_SFT,
		.hd_reg = AFE_MEMIF_HD_MODE,
		.hd_shift = VUL_HD_SFT,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
		.msb_reg = -1,
		.msb_shift = -1,
	},
	[MT6797_MEMIF_AWB] = {
		.name = "AWB",
		.id = MT6797_MEMIF_AWB,
		.reg_ofs_base = AFE_AWB_BASE,
		.reg_ofs_cur = AFE_AWB_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = AWB_MODE_SFT,
		.fs_maskbit = AWB_MODE_MASK,
		.mono_reg = AFE_DAC_CON1,
		.mono_shift = AWB_DATA_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = AWB_ON_SFT,
		.hd_reg = AFE_MEMIF_HD_MODE,
		.hd_shift = AWB_HD_SFT,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
		.msb_reg = -1,
		.msb_shift = -1,
	},
	[MT6797_MEMIF_VUL12] = {
		.name = "VUL12",
		.id = MT6797_MEMIF_VUL12,
		.reg_ofs_base = AFE_VUL_D2_BASE,
		.reg_ofs_cur = AFE_VUL_D2_CUR,
		.fs_reg = AFE_DAC_CON0,
		.fs_shift = VUL_DATA2_MODE_SFT,
		.fs_maskbit = VUL_DATA2_MODE_MASK,
		.mono_reg = AFE_DAC_CON0,
		.mono_shift = VUL_DATA2_DATA_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = VUL_DATA2_ON_SFT,
		.hd_reg = AFE_MEMIF_HD_MODE,
		.hd_shift = VUL_DATA2_HD_SFT,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
		.msb_reg = -1,
		.msb_shift = -1,
	},
	[MT6797_MEMIF_DAI] = {
		.name = "DAI",
		.id = MT6797_MEMIF_DAI,
		.reg_ofs_base = AFE_DAI_BASE,
		.reg_ofs_cur = AFE_DAI_CUR,
		.fs_reg = AFE_DAC_CON0,
		.fs_shift = DAI_MODE_SFT,
		.fs_maskbit = DAI_MODE_MASK,
		.mono_reg = -1,
		.mono_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DAI_ON_SFT,
		.hd_reg = AFE_MEMIF_HD_MODE,
		.hd_shift = DAI_HD_SFT,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
		.msb_reg = -1,
		.msb_shift = -1,
	},
	[MT6797_MEMIF_MOD_DAI] = {
		.name = "MOD_DAI",
		.id = MT6797_MEMIF_MOD_DAI,
		.reg_ofs_base = AFE_MOD_DAI_BASE,
		.reg_ofs_cur = AFE_MOD_DAI_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = MOD_DAI_MODE_SFT,
		.fs_maskbit = MOD_DAI_MODE_MASK,
		.mono_reg = -1,
		.mono_shift = 0,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = MOD_DAI_ON_SFT,
		.hd_reg = AFE_MEMIF_HD_MODE,
		.hd_shift = MOD_DAI_HD_SFT,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
		.msb_reg = -1,
		.msb_shift = -1,
	},
};

static const struct mtk_base_irq_data irq_data[MT6797_IRQ_NUM] = {
	[MT6797_IRQ_1] = {
		.id = MT6797_IRQ_1,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT1,
		.irq_cnt_shift = AFE_IRQ_MCU_CNT1_SFT,
		.irq_cnt_maskbit = AFE_IRQ_MCU_CNT1_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON,
		.irq_fs_shift = IRQ1_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ1_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON,
		.irq_en_shift = IRQ1_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ1_MCU_CLR_SFT,
	},
	[MT6797_IRQ_2] = {
		.id = MT6797_IRQ_2,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT2,
		.irq_cnt_shift = AFE_IRQ_MCU_CNT2_SFT,
		.irq_cnt_maskbit = AFE_IRQ_MCU_CNT2_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON,
		.irq_fs_shift = IRQ2_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ2_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON,
		.irq_en_shift = IRQ2_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ2_MCU_CLR_SFT,
	},
	[MT6797_IRQ_3] = {
		.id = MT6797_IRQ_3,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT3,
		.irq_cnt_shift = AFE_IRQ_MCU_CNT3_SFT,
		.irq_cnt_maskbit = AFE_IRQ_MCU_CNT3_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON,
		.irq_fs_shift = IRQ3_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ3_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON,
		.irq_en_shift = IRQ3_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ3_MCU_CLR_SFT,
	},
	[MT6797_IRQ_4] = {
		.id = MT6797_IRQ_4,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT4,
		.irq_cnt_shift = AFE_IRQ_MCU_CNT4_SFT,
		.irq_cnt_maskbit = AFE_IRQ_MCU_CNT4_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON,
		.irq_fs_shift = IRQ4_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ4_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON,
		.irq_en_shift = IRQ4_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ4_MCU_CLR_SFT,
	},
	[MT6797_IRQ_7] = {
		.id = MT6797_IRQ_7,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT7,
		.irq_cnt_shift = AFE_IRQ_MCU_CNT7_SFT,
		.irq_cnt_maskbit = AFE_IRQ_MCU_CNT7_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON,
		.irq_fs_shift = IRQ7_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ7_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON,
		.irq_en_shift = IRQ7_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ7_MCU_CLR_SFT,
	},
};

static const struct regmap_config mt6797_afe_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = AFE_MAX_REGISTER,
};

static irqreturn_t mt6797_afe_irq_handler(int irq_id, void *dev)
{
	struct mtk_base_afe *afe = dev;
	struct mtk_base_afe_irq *irq;
	unsigned int status;
	unsigned int mcu_en;
	int ret;
	int i;
	irqreturn_t irq_ret = IRQ_HANDLED;

	/* get irq that is sent to MCU */
	regmap_read(afe->regmap, AFE_IRQ_MCU_EN, &mcu_en);

	ret = regmap_read(afe->regmap, AFE_IRQ_MCU_STATUS, &status);
	if (ret || (status & mcu_en) == 0) {
		dev_err(afe->dev, "%s(), irq status err, ret %d, status 0x%x, mcu_en 0x%x\n",
			__func__, ret, status, mcu_en);

		/* only clear IRQ which is sent to MCU */
		status = mcu_en & AFE_IRQ_STATUS_BITS;

		irq_ret = IRQ_NONE;
		goto err_irq;
	}

	for (i = 0; i < MT6797_MEMIF_NUM; i++) {
		struct mtk_base_afe_memif *memif = &afe->memif[i];

		if (!memif->substream)
			continue;

		irq = &afe->irqs[memif->irq_usage];

		if (status & (1 << irq->irq_data->irq_en_shift))
			snd_pcm_period_elapsed(memif->substream);
	}

err_irq:
	/* clear irq */
	regmap_write(afe->regmap,
		     AFE_IRQ_MCU_CLR,
		     status & AFE_IRQ_STATUS_BITS);

	return irq_ret;
}

static int mt6797_afe_runtime_suspend(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	unsigned int afe_on_retm;
	int retry = 0;

	/* disable AFE */
	regmap_update_bits(afe->regmap, AFE_DAC_CON0, AFE_ON_MASK_SFT, 0x0);
	do {
		regmap_read(afe->regmap, AFE_DAC_CON0, &afe_on_retm);
		if ((afe_on_retm & AFE_ON_RETM_MASK_SFT) == 0)
			break;

		udelay(10);
	} while (++retry < 100000);

	if (retry)
		dev_warn(afe->dev, "%s(), retry %d\n", __func__, retry);

	/* make sure all irq status are cleared */
	regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CLR, 0xffff, 0xffff);

	return mt6797_afe_disable_clock(afe);
}

static int mt6797_afe_runtime_resume(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	int ret;

	ret = mt6797_afe_enable_clock(afe);
	if (ret)
		return ret;

	/* irq signal to mcu only */
	regmap_write(afe->regmap, AFE_IRQ_MCU_EN, AFE_IRQ_MCU_EN_MASK_SFT);

	/* force all memif use normal mode */
	regmap_update_bits(afe->regmap, AFE_MEMIF_HDALIGN,
			   0x7ff << 16, 0x7ff << 16);
	/* force cpu use normal mode when access sram data */
	regmap_update_bits(afe->regmap, AFE_MEMIF_MSB,
			   CPU_COMPACT_MODE_MASK_SFT, 0);
	/* force cpu use 8_24 format when writing 32bit data */
	regmap_update_bits(afe->regmap, AFE_MEMIF_MSB,
			   CPU_HD_ALIGN_MASK_SFT, 0);

	/* set all output port to 24bit */
	regmap_update_bits(afe->regmap, AFE_CONN_24BIT,
			   0x3fffffff, 0x3fffffff);

	/* enable AFE */
	regmap_update_bits(afe->regmap, AFE_DAC_CON0,
			   AFE_ON_MASK_SFT,
			   0x1 << AFE_ON_SFT);

	return 0;
}

static int mt6797_afe_pcm_dev_probe(struct platform_device *pdev)
{
	struct mtk_base_afe *afe;
	struct mt6797_afe_private *afe_priv;
	struct resource *res;
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
	afe->dev = &pdev->dev;
	dev = afe->dev;

	/* initial audio related clock */
	ret = mt6797_init_clock(afe);
	if (ret) {
		dev_err(dev, "init clock error\n");
		return ret;
	}

	/* regmap init */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	afe->base_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(afe->base_addr))
		return PTR_ERR(afe->base_addr);

	afe->regmap = devm_regmap_init_mmio(&pdev->dev, afe->base_addr,
					    &mt6797_afe_regmap_config);
	if (IS_ERR(afe->regmap))
		return PTR_ERR(afe->regmap);

	/* init memif */
	afe->memif_size = MT6797_MEMIF_NUM;
	afe->memif = devm_kcalloc(dev, afe->memif_size, sizeof(*afe->memif),
				  GFP_KERNEL);
	if (!afe->memif)
		return -ENOMEM;

	for (i = 0; i < afe->memif_size; i++) {
		afe->memif[i].data = &memif_data[i];
		afe->memif[i].irq_usage = -1;
	}

	mutex_init(&afe->irq_alloc_lock);

	/* irq initialize */
	afe->irqs_size = MT6797_IRQ_NUM;
	afe->irqs = devm_kcalloc(dev, afe->irqs_size, sizeof(*afe->irqs),
				 GFP_KERNEL);
	if (!afe->irqs)
		return -ENOMEM;

	for (i = 0; i < afe->irqs_size; i++)
		afe->irqs[i].irq_data = &irq_data[i];

	/* request irq */
	irq_id = platform_get_irq(pdev, 0);
	if (!irq_id) {
		dev_err(dev, "%s no irq found\n", dev->of_node->name);
		return -ENXIO;
	}
	ret = devm_request_irq(dev, irq_id, mt6797_afe_irq_handler,
			       IRQF_TRIGGER_NONE, "asys-isr", (void *)afe);
	if (ret) {
		dev_err(dev, "could not request_irq for asys-isr\n");
		return ret;
	}

	afe->mtk_afe_hardware = &mt6797_afe_hardware;
	afe->memif_fs = mt6797_memif_fs;
	afe->irq_fs = mt6797_irq_fs;

	afe->runtime_resume = mt6797_afe_runtime_resume;
	afe->runtime_suspend = mt6797_afe_runtime_suspend;

	platform_set_drvdata(pdev, afe);

	pm_runtime_enable(dev);
	if (!pm_runtime_enabled(dev))
		goto err_pm_disable;
	pm_runtime_get_sync(&pdev->dev);

	ret = devm_snd_soc_register_component(dev, &mtk_afe_pcm_platform,
					      NULL, 0);
	if (ret) {
		dev_warn(dev, "err_platform\n");
		goto err_pm_disable;
	}

	ret = devm_snd_soc_register_component(afe->dev,
				     &mt6797_afe_pcm_dai_component,
				     mt6797_afe_pcm_dais,
				     ARRAY_SIZE(mt6797_afe_pcm_dais));
	if (ret) {
		dev_warn(dev, "err_dai_component\n");
		goto err_pm_disable;
	}

	return 0;

err_pm_disable:
	pm_runtime_disable(dev);

	return ret;
}

static int mt6797_afe_pcm_dev_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		mt6797_afe_runtime_suspend(&pdev->dev);
	pm_runtime_put_sync(&pdev->dev);

	return 0;
}

static const struct of_device_id mt6797_afe_pcm_dt_match[] = {
	{ .compatible = "mediatek,mt6797-audio", },
	{},
};
MODULE_DEVICE_TABLE(of, mt6797_afe_pcm_dt_match);

static const struct dev_pm_ops mt6797_afe_pm_ops = {
	SET_RUNTIME_PM_OPS(mt6797_afe_runtime_suspend,
			   mt6797_afe_runtime_resume, NULL)
};

static struct platform_driver mt6797_afe_pcm_driver = {
	.driver = {
		   .name = "mt6797-audio",
		   .of_match_table = mt6797_afe_pcm_dt_match,
#ifdef CONFIG_PM
		   .pm = &mt6797_afe_pm_ops,
#endif
	},
	.probe = mt6797_afe_pcm_dev_probe,
	.remove = mt6797_afe_pcm_dev_remove,
};

module_platform_driver(mt6797_afe_pcm_driver);

MODULE_DESCRIPTION("Mediatek ALSA SoC AFE platform driver for 6797");
MODULE_AUTHOR("KaiChieh Chuang <kaichieh.chuang@mediatek.com>");
MODULE_LICENSE("GPL v2");
