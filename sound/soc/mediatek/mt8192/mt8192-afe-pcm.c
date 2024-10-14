// SPDX-License-Identifier: GPL-2.0
//
// Mediatek ALSA SoC AFE platform driver for 8192
//
// Copyright (c) 2020 MediaTek Inc.
// Author: Shane Chien <shane.chien@mediatek.com>
//

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <sound/soc.h>

#include "../common/mtk-afe-fe-dai.h"
#include "../common/mtk-afe-platform-driver.h"

#include "mt8192-afe-common.h"
#include "mt8192-afe-clk.h"
#include "mt8192-afe-gpio.h"
#include "mt8192-interconnection.h"

static const struct snd_pcm_hardware mt8192_afe_hardware = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_MMAP_VALID),
	.formats = (SNDRV_PCM_FMTBIT_S16_LE |
		    SNDRV_PCM_FMTBIT_S24_LE |
		    SNDRV_PCM_FMTBIT_S32_LE),
	.period_bytes_min = 96,
	.period_bytes_max = 4 * 48 * 1024,
	.periods_min = 2,
	.periods_max = 256,
	.buffer_bytes_max = 4 * 48 * 1024,
	.fifo_size = 0,
};

static int mt8192_memif_fs(struct snd_pcm_substream *substream,
			   unsigned int rate)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	int id = snd_soc_rtd_to_cpu(rtd, 0)->id;

	return mt8192_rate_transform(afe->dev, rate, id);
}

static int mt8192_get_dai_fs(struct mtk_base_afe *afe,
			     int dai_id, unsigned int rate)
{
	return mt8192_rate_transform(afe->dev, rate, dai_id);
}

static int mt8192_irq_fs(struct snd_pcm_substream *substream, unsigned int rate)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);

	return mt8192_general_rate_transform(afe->dev, rate);
}

static int mt8192_get_memif_pbuf_size(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;

	if ((runtime->period_size * 1000) / runtime->rate > 10)
		return MT8192_MEMIF_PBUF_SIZE_256_BYTES;
	else
		return MT8192_MEMIF_PBUF_SIZE_32_BYTES;
}

#define MTK_PCM_RATES (SNDRV_PCM_RATE_8000_48000 |\
		       SNDRV_PCM_RATE_88200 |\
		       SNDRV_PCM_RATE_96000 |\
		       SNDRV_PCM_RATE_176400 |\
		       SNDRV_PCM_RATE_192000)

#define MTK_PCM_DAI_RATES (SNDRV_PCM_RATE_8000 |\
			   SNDRV_PCM_RATE_16000 |\
			   SNDRV_PCM_RATE_32000 |\
			   SNDRV_PCM_RATE_48000)

#define MTK_PCM_FORMATS (SNDRV_PCM_FMTBIT_S16_LE |\
			 SNDRV_PCM_FMTBIT_S24_LE |\
			 SNDRV_PCM_FMTBIT_S32_LE)

static struct snd_soc_dai_driver mt8192_memif_dai_driver[] = {
	/* FE DAIs: memory intefaces to CPU */
	{
		.name = "DL1",
		.id = MT8192_MEMIF_DL1,
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
		.name = "DL12",
		.id = MT8192_MEMIF_DL12,
		.playback = {
			.stream_name = "DL12",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "DL2",
		.id = MT8192_MEMIF_DL2,
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
		.id = MT8192_MEMIF_DL3,
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
		.name = "DL4",
		.id = MT8192_MEMIF_DL4,
		.playback = {
			.stream_name = "DL4",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "DL5",
		.id = MT8192_MEMIF_DL5,
		.playback = {
			.stream_name = "DL5",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "DL6",
		.id = MT8192_MEMIF_DL6,
		.playback = {
			.stream_name = "DL6",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "DL7",
		.id = MT8192_MEMIF_DL7,
		.playback = {
			.stream_name = "DL7",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "DL8",
		.id = MT8192_MEMIF_DL8,
		.playback = {
			.stream_name = "DL8",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "DL9",
		.id = MT8192_MEMIF_DL9,
		.playback = {
			.stream_name = "DL9",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL1",
		.id = MT8192_MEMIF_VUL12,
		.capture = {
			.stream_name = "UL1",
			.channels_min = 1,
			.channels_max = 4,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL2",
		.id = MT8192_MEMIF_AWB,
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
		.id = MT8192_MEMIF_VUL2,
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
		.name = "UL4",
		.id = MT8192_MEMIF_AWB2,
		.capture = {
			.stream_name = "UL4",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL5",
		.id = MT8192_MEMIF_VUL3,
		.capture = {
			.stream_name = "UL5",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL6",
		.id = MT8192_MEMIF_VUL4,
		.capture = {
			.stream_name = "UL6",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL7",
		.id = MT8192_MEMIF_VUL5,
		.capture = {
			.stream_name = "UL7",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL8",
		.id = MT8192_MEMIF_VUL6,
		.capture = {
			.stream_name = "UL8",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL_MONO_1",
		.id = MT8192_MEMIF_MOD_DAI,
		.capture = {
			.stream_name = "UL_MONO_1",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_DAI_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL_MONO_2",
		.id = MT8192_MEMIF_DAI,
		.capture = {
			.stream_name = "UL_MONO_2",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_DAI_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "UL_MONO_3",
		.id = MT8192_MEMIF_DAI2,
		.capture = {
			.stream_name = "UL_MONO_3",
			.channels_min = 1,
			.channels_max = 2,
			.rates = MTK_PCM_DAI_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
	{
		.name = "HDMI",
		.id = MT8192_MEMIF_HDMI,
		.playback = {
			.stream_name = "HDMI",
			.channels_min = 2,
			.channels_max = 8,
			.rates = MTK_PCM_RATES,
			.formats = MTK_PCM_FORMATS,
		},
		.ops = &mtk_afe_fe_ops,
	},
};

static int ul_tinyconn_event(struct snd_soc_dapm_widget *w,
			     struct snd_kcontrol *kcontrol,
			     int event)
{
	struct snd_soc_component *cmpnt = snd_soc_dapm_to_component(w->dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(cmpnt);
	unsigned int reg_shift;
	unsigned int reg_mask_shift;

	dev_dbg(afe->dev, "%s(), event 0x%x\n", __func__, event);

	if (strstr(w->name, "UL1")) {
		reg_shift = VUL1_USE_TINY_SFT;
		reg_mask_shift = VUL1_USE_TINY_MASK_SFT;
	} else if (strstr(w->name, "UL2")) {
		reg_shift = VUL2_USE_TINY_SFT;
		reg_mask_shift = VUL2_USE_TINY_MASK_SFT;
	} else if (strstr(w->name, "UL3")) {
		reg_shift = VUL12_USE_TINY_SFT;
		reg_mask_shift = VUL12_USE_TINY_MASK_SFT;
	} else if (strstr(w->name, "UL4")) {
		reg_shift = AWB2_USE_TINY_SFT;
		reg_mask_shift = AWB2_USE_TINY_MASK_SFT;
	} else {
		reg_shift = AWB2_USE_TINY_SFT;
		reg_mask_shift = AWB2_USE_TINY_MASK_SFT;
		dev_warn(afe->dev, "%s(), err widget name %s, default use UL4",
			 __func__, w->name);
	}

	switch (event) {
	case SND_SOC_DAPM_PRE_PMU:
		regmap_update_bits(afe->regmap, AFE_MEMIF_CONN, reg_mask_shift,
				   0x1 << reg_shift);
		break;
	case SND_SOC_DAPM_PRE_PMD:
		regmap_update_bits(afe->regmap, AFE_MEMIF_CONN, reg_mask_shift,
				   0x0 << reg_shift);
		break;
	default:
		break;
	}

	return 0;
}

/* dma widget & routes*/
static const struct snd_kcontrol_new memif_ul1_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN21,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN21,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH3", AFE_CONN21,
				    I_ADDA_UL_CH3, 1, 0),
};

static const struct snd_kcontrol_new memif_ul1_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN22,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN22,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH3", AFE_CONN22,
				    I_ADDA_UL_CH3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH4", AFE_CONN22,
				    I_ADDA_UL_CH4, 1, 0),
};

static const struct snd_kcontrol_new memif_ul1_ch3_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN9,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN9,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH3", AFE_CONN9,
				    I_ADDA_UL_CH3, 1, 0),
};

static const struct snd_kcontrol_new memif_ul1_ch4_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN10,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN10,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH3", AFE_CONN10,
				    I_ADDA_UL_CH3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH4", AFE_CONN10,
				    I_ADDA_UL_CH4, 1, 0),
};

static const struct snd_kcontrol_new memif_ul2_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I2S0_CH1", AFE_CONN5,
				    I_I2S0_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN5,
				    I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH1", AFE_CONN5,
				    I_DL12_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN5,
				    I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN5,
				    I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH1", AFE_CONN5_1,
				    I_DL4_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH1", AFE_CONN5_1,
				    I_DL5_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH1", AFE_CONN5_1,
				    I_DL6_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN5,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN5,
				    I_PCM_2_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I2S2_CH1", AFE_CONN5,
				    I_I2S2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I2S6_CH1", AFE_CONN5_1,
				    I_I2S6_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I2S8_CH1", AFE_CONN5_1,
				    I_I2S8_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("CONNSYS_I2S_CH1", AFE_CONN5_1,
				    I_CONNSYS_I2S_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("SRC_1_OUT_CH1", AFE_CONN5_1,
				    I_SRC_1_OUT_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul2_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I2S0_CH2", AFE_CONN6,
				    I_I2S0_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2", AFE_CONN6,
				    I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH2", AFE_CONN6,
				    I_DL12_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN6,
				    I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2", AFE_CONN6,
				    I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH2", AFE_CONN6_1,
				    I_DL4_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL5_CH2", AFE_CONN6_1,
				    I_DL5_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH2", AFE_CONN6_1,
				    I_DL6_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN6,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN6,
				    I_PCM_2_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I2S2_CH2", AFE_CONN6,
				    I_I2S2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I2S6_CH2", AFE_CONN6_1,
				    I_I2S6_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I2S8_CH2", AFE_CONN6_1,
				    I_I2S8_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("CONNSYS_I2S_CH2", AFE_CONN6_1,
				    I_CONNSYS_I2S_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("SRC_1_OUT_CH2", AFE_CONN6_1,
				    I_SRC_1_OUT_CH2, 1, 0),
};

static const struct snd_kcontrol_new memif_ul3_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("CONNSYS_I2S_CH1", AFE_CONN32_1,
				    I_CONNSYS_I2S_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN32,
				    I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN32,
				    I_DL2_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul3_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("CONNSYS_I2S_CH2", AFE_CONN33_1,
				    I_CONNSYS_I2S_CH2, 1, 0),
};

static const struct snd_kcontrol_new memif_ul4_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN38,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I2S0_CH1", AFE_CONN38,
				    I_I2S0_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul4_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN39,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I2S0_CH2", AFE_CONN39,
				    I_I2S0_CH2, 1, 0),
};

static const struct snd_kcontrol_new memif_ul5_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN44,
				    I_ADDA_UL_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul5_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN45,
				    I_ADDA_UL_CH2, 1, 0),
};

static const struct snd_kcontrol_new memif_ul6_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN46,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH1", AFE_CONN46,
				    I_DL1_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH1", AFE_CONN46,
				    I_DL12_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH1", AFE_CONN46_1,
				    I_DL6_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH1", AFE_CONN46,
				    I_DL2_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH1", AFE_CONN46,
				    I_DL3_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH1", AFE_CONN46_1,
				    I_DL4_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN46,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN46,
				    I_PCM_2_CAP_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul6_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN47,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL1_CH2", AFE_CONN47,
				    I_DL1_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL12_CH2", AFE_CONN47,
				    I_DL12_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL6_CH2", AFE_CONN47_1,
				    I_DL6_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL2_CH2", AFE_CONN47,
				    I_DL2_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL3_CH2", AFE_CONN47,
				    I_DL3_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("DL4_CH2", AFE_CONN47_1,
				    I_DL4_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN47,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN47,
				    I_PCM_2_CAP_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul7_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN48,
				    I_ADDA_UL_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul7_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN49,
				    I_ADDA_UL_CH2, 1, 0),
};

static const struct snd_kcontrol_new memif_ul8_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN50,
				    I_ADDA_UL_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul8_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN51,
				    I_ADDA_UL_CH2, 1, 0),
};

static const struct snd_kcontrol_new memif_ul_mono_1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_1_CAP_CH1", AFE_CONN12,
				    I_PCM_1_CAP_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("PCM_2_CAP_CH1", AFE_CONN12,
				    I_PCM_2_CAP_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul_mono_2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN11,
				    I_ADDA_UL_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul_mono_3_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN35,
				    I_ADDA_UL_CH1, 1, 0),
};

/* TINYCONN MUX */
enum {
	TINYCONN_CH1_MUX_I2S0 = 0x14,
	TINYCONN_CH2_MUX_I2S0 = 0x15,
	TINYCONN_CH1_MUX_I2S6 = 0x1a,
	TINYCONN_CH2_MUX_I2S6 = 0x1b,
	TINYCONN_CH1_MUX_I2S8 = 0x1c,
	TINYCONN_CH2_MUX_I2S8 = 0x1d,
	TINYCONN_MUX_NONE = 0x1f,
};

static const char * const tinyconn_mux_map[] = {
	"NONE",
	"I2S0_CH1",
	"I2S0_CH2",
	"I2S6_CH1",
	"I2S6_CH2",
	"I2S8_CH1",
	"I2S8_CH2",
};

static int tinyconn_mux_map_value[] = {
	TINYCONN_MUX_NONE,
	TINYCONN_CH1_MUX_I2S0,
	TINYCONN_CH2_MUX_I2S0,
	TINYCONN_CH1_MUX_I2S6,
	TINYCONN_CH2_MUX_I2S6,
	TINYCONN_CH1_MUX_I2S8,
	TINYCONN_CH2_MUX_I2S8,
};

static SOC_VALUE_ENUM_SINGLE_DECL(ul4_tinyconn_ch1_mux_map_enum,
				  AFE_TINY_CONN0,
				  O_2_CFG_SFT,
				  O_2_CFG_MASK,
				  tinyconn_mux_map,
				  tinyconn_mux_map_value);
static SOC_VALUE_ENUM_SINGLE_DECL(ul4_tinyconn_ch2_mux_map_enum,
				  AFE_TINY_CONN0,
				  O_3_CFG_SFT,
				  O_3_CFG_MASK,
				  tinyconn_mux_map,
				  tinyconn_mux_map_value);

static const struct snd_kcontrol_new ul4_tinyconn_ch1_mux_control =
	SOC_DAPM_ENUM("UL4_TINYCONN_CH1_MUX", ul4_tinyconn_ch1_mux_map_enum);
static const struct snd_kcontrol_new ul4_tinyconn_ch2_mux_control =
	SOC_DAPM_ENUM("UL4_TINYCONN_CH2_MUX", ul4_tinyconn_ch2_mux_map_enum);

static const struct snd_soc_dapm_widget mt8192_memif_widgets[] = {
	/* inter-connections */
	SND_SOC_DAPM_MIXER("UL1_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul1_ch1_mix, ARRAY_SIZE(memif_ul1_ch1_mix)),
	SND_SOC_DAPM_MIXER("UL1_CH2", SND_SOC_NOPM, 0, 0,
			   memif_ul1_ch2_mix, ARRAY_SIZE(memif_ul1_ch2_mix)),
	SND_SOC_DAPM_MIXER("UL1_CH3", SND_SOC_NOPM, 0, 0,
			   memif_ul1_ch3_mix, ARRAY_SIZE(memif_ul1_ch3_mix)),
	SND_SOC_DAPM_MIXER("UL1_CH4", SND_SOC_NOPM, 0, 0,
			   memif_ul1_ch4_mix, ARRAY_SIZE(memif_ul1_ch4_mix)),

	SND_SOC_DAPM_MIXER("UL2_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul2_ch1_mix, ARRAY_SIZE(memif_ul2_ch1_mix)),
	SND_SOC_DAPM_MIXER("UL2_CH2", SND_SOC_NOPM, 0, 0,
			   memif_ul2_ch2_mix, ARRAY_SIZE(memif_ul2_ch2_mix)),

	SND_SOC_DAPM_MIXER("UL3_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul3_ch1_mix, ARRAY_SIZE(memif_ul3_ch1_mix)),
	SND_SOC_DAPM_MIXER("UL3_CH2", SND_SOC_NOPM, 0, 0,
			   memif_ul3_ch2_mix, ARRAY_SIZE(memif_ul3_ch2_mix)),

	SND_SOC_DAPM_MIXER("UL4_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul4_ch1_mix, ARRAY_SIZE(memif_ul4_ch1_mix)),
	SND_SOC_DAPM_MIXER("UL4_CH2", SND_SOC_NOPM, 0, 0,
			   memif_ul4_ch2_mix, ARRAY_SIZE(memif_ul4_ch2_mix)),
	SND_SOC_DAPM_MUX_E("UL4_TINYCONN_CH1_MUX", SND_SOC_NOPM, 0, 0,
			   &ul4_tinyconn_ch1_mux_control,
			   ul_tinyconn_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),
	SND_SOC_DAPM_MUX_E("UL4_TINYCONN_CH2_MUX", SND_SOC_NOPM, 0, 0,
			   &ul4_tinyconn_ch2_mux_control,
			   ul_tinyconn_event,
			   SND_SOC_DAPM_PRE_PMU | SND_SOC_DAPM_PRE_PMD),

	SND_SOC_DAPM_MIXER("UL5_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul5_ch1_mix, ARRAY_SIZE(memif_ul5_ch1_mix)),
	SND_SOC_DAPM_MIXER("UL5_CH2", SND_SOC_NOPM, 0, 0,
			   memif_ul5_ch2_mix, ARRAY_SIZE(memif_ul5_ch2_mix)),

	SND_SOC_DAPM_MIXER("UL6_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul6_ch1_mix, ARRAY_SIZE(memif_ul6_ch1_mix)),
	SND_SOC_DAPM_MIXER("UL6_CH2", SND_SOC_NOPM, 0, 0,
			   memif_ul6_ch2_mix, ARRAY_SIZE(memif_ul6_ch2_mix)),

	SND_SOC_DAPM_MIXER("UL7_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul7_ch1_mix, ARRAY_SIZE(memif_ul7_ch1_mix)),
	SND_SOC_DAPM_MIXER("UL7_CH2", SND_SOC_NOPM, 0, 0,
			   memif_ul7_ch2_mix, ARRAY_SIZE(memif_ul7_ch2_mix)),

	SND_SOC_DAPM_MIXER("UL8_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul8_ch1_mix, ARRAY_SIZE(memif_ul8_ch1_mix)),
	SND_SOC_DAPM_MIXER("UL8_CH2", SND_SOC_NOPM, 0, 0,
			   memif_ul8_ch2_mix, ARRAY_SIZE(memif_ul8_ch2_mix)),

	SND_SOC_DAPM_MIXER("UL_MONO_1_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul_mono_1_mix,
			   ARRAY_SIZE(memif_ul_mono_1_mix)),

	SND_SOC_DAPM_MIXER("UL_MONO_2_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul_mono_2_mix,
			   ARRAY_SIZE(memif_ul_mono_2_mix)),

	SND_SOC_DAPM_MIXER("UL_MONO_3_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul_mono_3_mix,
			   ARRAY_SIZE(memif_ul_mono_3_mix)),

	SND_SOC_DAPM_INPUT("UL1_VIRTUAL_INPUT"),
	SND_SOC_DAPM_INPUT("UL2_VIRTUAL_INPUT"),
	SND_SOC_DAPM_INPUT("UL6_VIRTUAL_INPUT"),
};

static const struct snd_soc_dapm_route mt8192_memif_routes[] = {
	{"UL1", NULL, "UL1_CH1"},
	{"UL1", NULL, "UL1_CH2"},
	{"UL1", NULL, "UL1_CH3"},
	{"UL1", NULL, "UL1_CH4"},
	{"UL1_CH1", "ADDA_UL_CH1", "ADDA_UL_Mux"},
	{"UL1_CH1", "ADDA_UL_CH2", "ADDA_UL_Mux"},
	{"UL1_CH1", "ADDA_UL_CH3", "ADDA_CH34_UL_Mux"},
	{"UL1_CH2", "ADDA_UL_CH1", "ADDA_UL_Mux"},
	{"UL1_CH2", "ADDA_UL_CH2", "ADDA_UL_Mux"},
	{"UL1_CH2", "ADDA_UL_CH3", "ADDA_CH34_UL_Mux"},
	{"UL1_CH2", "ADDA_UL_CH4", "ADDA_CH34_UL_Mux"},
	{"UL1_CH3", "ADDA_UL_CH1", "ADDA_UL_Mux"},
	{"UL1_CH3", "ADDA_UL_CH2", "ADDA_UL_Mux"},
	{"UL1_CH3", "ADDA_UL_CH3", "ADDA_CH34_UL_Mux"},
	{"UL1_CH4", "ADDA_UL_CH1", "ADDA_UL_Mux"},
	{"UL1_CH4", "ADDA_UL_CH2", "ADDA_UL_Mux"},
	{"UL1_CH4", "ADDA_UL_CH3", "ADDA_CH34_UL_Mux"},
	{"UL1_CH4", "ADDA_UL_CH4", "ADDA_CH34_UL_Mux"},

	{"UL2", NULL, "UL2_CH1"},
	{"UL2", NULL, "UL2_CH2"},
	{"UL2_CH1", "I2S0_CH1", "I2S0"},
	{"UL2_CH2", "I2S0_CH2", "I2S0"},
	{"UL2_CH1", "I2S2_CH1", "I2S2"},
	{"UL2_CH2", "I2S2_CH2", "I2S2"},
	{"UL2_CH1", "I2S6_CH1", "I2S6"},
	{"UL2_CH2", "I2S6_CH2", "I2S6"},
	{"UL2_CH1", "I2S8_CH1", "I2S8"},
	{"UL2_CH2", "I2S8_CH2", "I2S8"},

	{"UL2_CH1", "PCM_1_CAP_CH1", "PCM 1 Capture"},
	{"UL2_CH2", "PCM_1_CAP_CH1", "PCM 1 Capture"},
	{"UL2_CH1", "PCM_2_CAP_CH1", "PCM 2 Capture"},
	{"UL2_CH2", "PCM_2_CAP_CH1", "PCM 2 Capture"},

	{"UL_MONO_1", NULL, "UL_MONO_1_CH1"},
	{"UL_MONO_1_CH1", "PCM_1_CAP_CH1", "PCM 1 Capture"},
	{"UL_MONO_1_CH1", "PCM_2_CAP_CH1", "PCM 2 Capture"},

	{"UL_MONO_2", NULL, "UL_MONO_2_CH1"},
	{"UL_MONO_2_CH1", "ADDA_UL_CH1", "ADDA_UL_Mux"},

	{"UL_MONO_3", NULL, "UL_MONO_3_CH1"},
	{"UL_MONO_3_CH1", "ADDA_UL_CH1", "ADDA_UL_Mux"},

	{"UL2_CH1", "CONNSYS_I2S_CH1", "Connsys I2S"},
	{"UL2_CH2", "CONNSYS_I2S_CH2", "Connsys I2S"},

	{"UL3", NULL, "UL3_CH1"},
	{"UL3", NULL, "UL3_CH2"},
	{"UL3_CH1", "CONNSYS_I2S_CH1", "Connsys I2S"},
	{"UL3_CH2", "CONNSYS_I2S_CH2", "Connsys I2S"},

	{"UL4", NULL, "UL4_CH1"},
	{"UL4", NULL, "UL4_CH2"},
	{"UL4", NULL, "UL4_TINYCONN_CH1_MUX"},
	{"UL4", NULL, "UL4_TINYCONN_CH2_MUX"},
	{"UL4_CH1", "ADDA_UL_CH1", "ADDA_UL_Mux"},
	{"UL4_CH2", "ADDA_UL_CH2", "ADDA_UL_Mux"},
	{"UL4_CH1", "I2S0_CH1", "I2S0"},
	{"UL4_CH2", "I2S0_CH2", "I2S0"},
	{"UL4_TINYCONN_CH1_MUX", "I2S0_CH1", "I2S0"},
	{"UL4_TINYCONN_CH2_MUX", "I2S0_CH2", "I2S0"},

	{"UL5", NULL, "UL5_CH1"},
	{"UL5", NULL, "UL5_CH2"},
	{"UL5_CH1", "ADDA_UL_CH1", "ADDA_UL_Mux"},
	{"UL5_CH2", "ADDA_UL_CH2", "ADDA_UL_Mux"},

	{"UL6", NULL, "UL6_CH1"},
	{"UL6", NULL, "UL6_CH2"},

	{"UL6_CH1", "ADDA_UL_CH1", "ADDA_UL_Mux"},
	{"UL6_CH2", "ADDA_UL_CH2", "ADDA_UL_Mux"},
	{"UL6_CH1", "PCM_1_CAP_CH1", "PCM 1 Capture"},
	{"UL6_CH2", "PCM_1_CAP_CH1", "PCM 1 Capture"},
	{"UL6_CH1", "PCM_2_CAP_CH1", "PCM 2 Capture"},
	{"UL6_CH2", "PCM_2_CAP_CH1", "PCM 2 Capture"},

	{"UL7", NULL, "UL7_CH1"},
	{"UL7", NULL, "UL7_CH2"},
	{"UL7_CH1", "ADDA_UL_CH1", "ADDA_UL_Mux"},
	{"UL7_CH2", "ADDA_UL_CH2", "ADDA_UL_Mux"},

	{"UL8", NULL, "UL8_CH1"},
	{"UL8", NULL, "UL8_CH2"},
	{"UL8_CH1", "ADDA_UL_CH1", "ADDA_UL_Mux"},
	{"UL8_CH2", "ADDA_UL_CH2", "ADDA_UL_Mux"},
};

static const struct mtk_base_memif_data memif_data[MT8192_MEMIF_NUM] = {
	[MT8192_MEMIF_DL1] = {
		.name = "DL1",
		.id = MT8192_MEMIF_DL1,
		.reg_ofs_base = AFE_DL1_BASE,
		.reg_ofs_cur = AFE_DL1_CUR,
		.reg_ofs_end = AFE_DL1_END,
		.reg_ofs_base_msb = AFE_DL1_BASE_MSB,
		.reg_ofs_cur_msb = AFE_DL1_CUR_MSB,
		.reg_ofs_end_msb = AFE_DL1_END_MSB,
		.fs_reg = AFE_DL1_CON0,
		.fs_shift = DL1_MODE_SFT,
		.fs_maskbit = DL1_MODE_MASK,
		.mono_reg = AFE_DL1_CON0,
		.mono_shift = DL1_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DL1_ON_SFT,
		.hd_reg = AFE_DL1_CON0,
		.hd_shift = DL1_HD_MODE_SFT,
		.hd_align_reg = AFE_DL1_CON0,
		.hd_align_mshift = DL1_HALIGN_SFT,
		.pbuf_reg = AFE_DL1_CON0,
		.pbuf_shift = DL1_PBUF_SIZE_SFT,
		.minlen_reg = AFE_DL1_CON0,
		.minlen_shift = DL1_MINLEN_SFT,
	},
	[MT8192_MEMIF_DL12] = {
		.name = "DL12",
		.id = MT8192_MEMIF_DL12,
		.reg_ofs_base = AFE_DL12_BASE,
		.reg_ofs_cur = AFE_DL12_CUR,
		.reg_ofs_end = AFE_DL12_END,
		.reg_ofs_base_msb = AFE_DL12_BASE_MSB,
		.reg_ofs_cur_msb = AFE_DL12_CUR_MSB,
		.reg_ofs_end_msb = AFE_DL12_END_MSB,
		.fs_reg = AFE_DL12_CON0,
		.fs_shift = DL12_MODE_SFT,
		.fs_maskbit = DL12_MODE_MASK,
		.mono_reg = AFE_DL12_CON0,
		.mono_shift = DL12_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DL12_ON_SFT,
		.hd_reg = AFE_DL12_CON0,
		.hd_shift = DL12_HD_MODE_SFT,
		.hd_align_reg = AFE_DL12_CON0,
		.hd_align_mshift = DL12_HALIGN_SFT,
		.pbuf_reg = AFE_DL12_CON0,
		.pbuf_shift = DL12_PBUF_SIZE_SFT,
		.minlen_reg = AFE_DL12_CON0,
		.minlen_shift = DL12_MINLEN_SFT,
	},
	[MT8192_MEMIF_DL2] = {
		.name = "DL2",
		.id = MT8192_MEMIF_DL2,
		.reg_ofs_base = AFE_DL2_BASE,
		.reg_ofs_cur = AFE_DL2_CUR,
		.reg_ofs_end = AFE_DL2_END,
		.reg_ofs_base_msb = AFE_DL2_BASE_MSB,
		.reg_ofs_cur_msb = AFE_DL2_CUR_MSB,
		.reg_ofs_end_msb = AFE_DL2_END_MSB,
		.fs_reg = AFE_DL2_CON0,
		.fs_shift = DL2_MODE_SFT,
		.fs_maskbit = DL2_MODE_MASK,
		.mono_reg = AFE_DL2_CON0,
		.mono_shift = DL2_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DL2_ON_SFT,
		.hd_reg = AFE_DL2_CON0,
		.hd_shift = DL2_HD_MODE_SFT,
		.hd_align_reg = AFE_DL2_CON0,
		.hd_align_mshift = DL2_HALIGN_SFT,
		.pbuf_reg = AFE_DL2_CON0,
		.pbuf_shift = DL2_PBUF_SIZE_SFT,
		.minlen_reg = AFE_DL2_CON0,
		.minlen_shift = DL2_MINLEN_SFT,
	},
	[MT8192_MEMIF_DL3] = {
		.name = "DL3",
		.id = MT8192_MEMIF_DL3,
		.reg_ofs_base = AFE_DL3_BASE,
		.reg_ofs_cur = AFE_DL3_CUR,
		.reg_ofs_end = AFE_DL3_END,
		.reg_ofs_base_msb = AFE_DL3_BASE_MSB,
		.reg_ofs_cur_msb = AFE_DL3_CUR_MSB,
		.reg_ofs_end_msb = AFE_DL3_END_MSB,
		.fs_reg = AFE_DL3_CON0,
		.fs_shift = DL3_MODE_SFT,
		.fs_maskbit = DL3_MODE_MASK,
		.mono_reg = AFE_DL3_CON0,
		.mono_shift = DL3_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DL3_ON_SFT,
		.hd_reg = AFE_DL3_CON0,
		.hd_shift = DL3_HD_MODE_SFT,
		.hd_align_reg = AFE_DL3_CON0,
		.hd_align_mshift = DL3_HALIGN_SFT,
		.pbuf_reg = AFE_DL3_CON0,
		.pbuf_shift = DL3_PBUF_SIZE_SFT,
		.minlen_reg = AFE_DL3_CON0,
		.minlen_shift = DL3_MINLEN_SFT,
	},
	[MT8192_MEMIF_DL4] = {
		.name = "DL4",
		.id = MT8192_MEMIF_DL4,
		.reg_ofs_base = AFE_DL4_BASE,
		.reg_ofs_cur = AFE_DL4_CUR,
		.reg_ofs_end = AFE_DL4_END,
		.reg_ofs_base_msb = AFE_DL4_BASE_MSB,
		.reg_ofs_cur_msb = AFE_DL4_CUR_MSB,
		.reg_ofs_end_msb = AFE_DL4_END_MSB,
		.fs_reg = AFE_DL4_CON0,
		.fs_shift = DL4_MODE_SFT,
		.fs_maskbit = DL4_MODE_MASK,
		.mono_reg = AFE_DL4_CON0,
		.mono_shift = DL4_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DL4_ON_SFT,
		.hd_reg = AFE_DL4_CON0,
		.hd_shift = DL4_HD_MODE_SFT,
		.hd_align_reg = AFE_DL4_CON0,
		.hd_align_mshift = DL4_HALIGN_SFT,
		.pbuf_reg = AFE_DL4_CON0,
		.pbuf_shift = DL4_PBUF_SIZE_SFT,
		.minlen_reg = AFE_DL4_CON0,
		.minlen_shift = DL4_MINLEN_SFT,
	},
	[MT8192_MEMIF_DL5] = {
		.name = "DL5",
		.id = MT8192_MEMIF_DL5,
		.reg_ofs_base = AFE_DL5_BASE,
		.reg_ofs_cur = AFE_DL5_CUR,
		.reg_ofs_end = AFE_DL5_END,
		.reg_ofs_base_msb = AFE_DL5_BASE_MSB,
		.reg_ofs_cur_msb = AFE_DL5_CUR_MSB,
		.reg_ofs_end_msb = AFE_DL5_END_MSB,
		.fs_reg = AFE_DL5_CON0,
		.fs_shift = DL5_MODE_SFT,
		.fs_maskbit = DL5_MODE_MASK,
		.mono_reg = AFE_DL5_CON0,
		.mono_shift = DL5_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DL5_ON_SFT,
		.hd_reg = AFE_DL5_CON0,
		.hd_shift = DL5_HD_MODE_SFT,
		.hd_align_reg = AFE_DL5_CON0,
		.hd_align_mshift = DL5_HALIGN_SFT,
		.pbuf_reg = AFE_DL5_CON0,
		.pbuf_shift = DL5_PBUF_SIZE_SFT,
		.minlen_reg = AFE_DL5_CON0,
		.minlen_shift = DL5_MINLEN_SFT,
	},
	[MT8192_MEMIF_DL6] = {
		.name = "DL6",
		.id = MT8192_MEMIF_DL6,
		.reg_ofs_base = AFE_DL6_BASE,
		.reg_ofs_cur = AFE_DL6_CUR,
		.reg_ofs_end = AFE_DL6_END,
		.reg_ofs_base_msb = AFE_DL6_BASE_MSB,
		.reg_ofs_cur_msb = AFE_DL6_CUR_MSB,
		.reg_ofs_end_msb = AFE_DL6_END_MSB,
		.fs_reg = AFE_DL6_CON0,
		.fs_shift = DL6_MODE_SFT,
		.fs_maskbit = DL6_MODE_MASK,
		.mono_reg = AFE_DL6_CON0,
		.mono_shift = DL6_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DL6_ON_SFT,
		.hd_reg = AFE_DL6_CON0,
		.hd_shift = DL6_HD_MODE_SFT,
		.hd_align_reg = AFE_DL6_CON0,
		.hd_align_mshift = DL6_HALIGN_SFT,
		.pbuf_reg = AFE_DL6_CON0,
		.pbuf_shift = DL6_PBUF_SIZE_SFT,
		.minlen_reg = AFE_DL6_CON0,
		.minlen_shift = DL6_MINLEN_SFT,
	},
	[MT8192_MEMIF_DL7] = {
		.name = "DL7",
		.id = MT8192_MEMIF_DL7,
		.reg_ofs_base = AFE_DL7_BASE,
		.reg_ofs_cur = AFE_DL7_CUR,
		.reg_ofs_end = AFE_DL7_END,
		.reg_ofs_base_msb = AFE_DL7_BASE_MSB,
		.reg_ofs_cur_msb = AFE_DL7_CUR_MSB,
		.reg_ofs_end_msb = AFE_DL7_END_MSB,
		.fs_reg = AFE_DL7_CON0,
		.fs_shift = DL7_MODE_SFT,
		.fs_maskbit = DL7_MODE_MASK,
		.mono_reg = AFE_DL7_CON0,
		.mono_shift = DL7_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DL7_ON_SFT,
		.hd_reg = AFE_DL7_CON0,
		.hd_shift = DL7_HD_MODE_SFT,
		.hd_align_reg = AFE_DL7_CON0,
		.hd_align_mshift = DL7_HALIGN_SFT,
		.pbuf_reg = AFE_DL7_CON0,
		.pbuf_shift = DL7_PBUF_SIZE_SFT,
		.minlen_reg = AFE_DL7_CON0,
		.minlen_shift = DL7_MINLEN_SFT,
	},
	[MT8192_MEMIF_DL8] = {
		.name = "DL8",
		.id = MT8192_MEMIF_DL8,
		.reg_ofs_base = AFE_DL8_BASE,
		.reg_ofs_cur = AFE_DL8_CUR,
		.reg_ofs_end = AFE_DL8_END,
		.reg_ofs_base_msb = AFE_DL8_BASE_MSB,
		.reg_ofs_cur_msb = AFE_DL8_CUR_MSB,
		.reg_ofs_end_msb = AFE_DL8_END_MSB,
		.fs_reg = AFE_DL8_CON0,
		.fs_shift = DL8_MODE_SFT,
		.fs_maskbit = DL8_MODE_MASK,
		.mono_reg = AFE_DL8_CON0,
		.mono_shift = DL8_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DL8_ON_SFT,
		.hd_reg = AFE_DL8_CON0,
		.hd_shift = DL8_HD_MODE_SFT,
		.hd_align_reg = AFE_DL8_CON0,
		.hd_align_mshift = DL8_HALIGN_SFT,
		.pbuf_reg = AFE_DL8_CON0,
		.pbuf_shift = DL8_PBUF_SIZE_SFT,
		.minlen_reg = AFE_DL8_CON0,
		.minlen_shift = DL8_MINLEN_SFT,
	},
	[MT8192_MEMIF_DL9] = {
		.name = "DL9",
		.id = MT8192_MEMIF_DL9,
		.reg_ofs_base = AFE_DL9_BASE,
		.reg_ofs_cur = AFE_DL9_CUR,
		.reg_ofs_end = AFE_DL9_END,
		.reg_ofs_base_msb = AFE_DL9_BASE_MSB,
		.reg_ofs_cur_msb = AFE_DL9_CUR_MSB,
		.reg_ofs_end_msb = AFE_DL9_END_MSB,
		.fs_reg = AFE_DL9_CON0,
		.fs_shift = DL9_MODE_SFT,
		.fs_maskbit = DL9_MODE_MASK,
		.mono_reg = AFE_DL9_CON0,
		.mono_shift = DL9_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DL9_ON_SFT,
		.hd_reg = AFE_DL9_CON0,
		.hd_shift = DL9_HD_MODE_SFT,
		.hd_align_reg = AFE_DL9_CON0,
		.hd_align_mshift = DL9_HALIGN_SFT,
		.pbuf_reg = AFE_DL9_CON0,
		.pbuf_shift = DL9_PBUF_SIZE_SFT,
		.minlen_reg = AFE_DL9_CON0,
		.minlen_shift = DL9_MINLEN_SFT,
	},
	[MT8192_MEMIF_DAI] = {
		.name = "DAI",
		.id = MT8192_MEMIF_DAI,
		.reg_ofs_base = AFE_DAI_BASE,
		.reg_ofs_cur = AFE_DAI_CUR,
		.reg_ofs_end = AFE_DAI_END,
		.reg_ofs_base_msb = AFE_DAI_BASE_MSB,
		.reg_ofs_cur_msb = AFE_DAI_CUR_MSB,
		.reg_ofs_end_msb = AFE_DAI_END_MSB,
		.fs_reg = AFE_DAI_CON0,
		.fs_shift = DAI_MODE_SFT,
		.fs_maskbit = DAI_MODE_MASK,
		.mono_reg = AFE_DAI_CON0,
		.mono_shift = DAI_DUPLICATE_WR_SFT,
		.mono_invert = 1,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DAI_ON_SFT,
		.hd_reg = AFE_DAI_CON0,
		.hd_shift = DAI_HD_MODE_SFT,
		.hd_align_reg = AFE_DAI_CON0,
		.hd_align_mshift = DAI_HALIGN_SFT,
	},
	[MT8192_MEMIF_MOD_DAI] = {
		.name = "MOD_DAI",
		.id = MT8192_MEMIF_MOD_DAI,
		.reg_ofs_base = AFE_MOD_DAI_BASE,
		.reg_ofs_cur = AFE_MOD_DAI_CUR,
		.reg_ofs_end = AFE_MOD_DAI_END,
		.reg_ofs_base_msb = AFE_MOD_DAI_BASE_MSB,
		.reg_ofs_cur_msb = AFE_MOD_DAI_CUR_MSB,
		.reg_ofs_end_msb = AFE_MOD_DAI_END_MSB,
		.fs_reg = AFE_MOD_DAI_CON0,
		.fs_shift = MOD_DAI_MODE_SFT,
		.fs_maskbit = MOD_DAI_MODE_MASK,
		.mono_reg = AFE_MOD_DAI_CON0,
		.mono_shift = MOD_DAI_DUPLICATE_WR_SFT,
		.mono_invert = 1,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = MOD_DAI_ON_SFT,
		.hd_reg = AFE_MOD_DAI_CON0,
		.hd_shift = MOD_DAI_HD_MODE_SFT,
		.hd_align_reg = AFE_MOD_DAI_CON0,
		.hd_align_mshift = MOD_DAI_HALIGN_SFT,
	},
	[MT8192_MEMIF_DAI2] = {
		.name = "DAI2",
		.id = MT8192_MEMIF_DAI2,
		.reg_ofs_base = AFE_DAI2_BASE,
		.reg_ofs_cur = AFE_DAI2_CUR,
		.reg_ofs_end = AFE_DAI2_END,
		.reg_ofs_base_msb = AFE_DAI2_BASE_MSB,
		.reg_ofs_cur_msb = AFE_DAI2_CUR_MSB,
		.reg_ofs_end_msb = AFE_DAI2_END_MSB,
		.fs_reg = AFE_DAI2_CON0,
		.fs_shift = DAI2_MODE_SFT,
		.fs_maskbit = DAI2_MODE_MASK,
		.mono_reg = AFE_DAI2_CON0,
		.mono_shift = DAI2_DUPLICATE_WR_SFT,
		.mono_invert = 1,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = DAI2_ON_SFT,
		.hd_reg = AFE_DAI2_CON0,
		.hd_shift = DAI2_HD_MODE_SFT,
		.hd_align_reg = AFE_DAI2_CON0,
		.hd_align_mshift = DAI2_HALIGN_SFT,
	},
	[MT8192_MEMIF_VUL12] = {
		.name = "VUL12",
		.id = MT8192_MEMIF_VUL12,
		.reg_ofs_base = AFE_VUL12_BASE,
		.reg_ofs_cur = AFE_VUL12_CUR,
		.reg_ofs_end = AFE_VUL12_END,
		.reg_ofs_base_msb = AFE_VUL12_BASE_MSB,
		.reg_ofs_cur_msb = AFE_VUL12_CUR_MSB,
		.reg_ofs_end_msb = AFE_VUL12_END_MSB,
		.fs_reg = AFE_VUL12_CON0,
		.fs_shift = VUL12_MODE_SFT,
		.fs_maskbit = VUL12_MODE_MASK,
		.mono_reg = AFE_VUL12_CON0,
		.mono_shift = VUL12_MONO_SFT,
		.quad_ch_reg = AFE_VUL12_CON0,
		.quad_ch_shift = VUL12_4CH_EN_SFT,
		.quad_ch_mask = VUL12_4CH_EN_MASK,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = VUL12_ON_SFT,
		.hd_reg = AFE_VUL12_CON0,
		.hd_shift = VUL12_HD_MODE_SFT,
		.hd_align_reg = AFE_VUL12_CON0,
		.hd_align_mshift = VUL12_HALIGN_SFT,
	},
	[MT8192_MEMIF_VUL2] = {
		.name = "VUL2",
		.id = MT8192_MEMIF_VUL2,
		.reg_ofs_base = AFE_VUL2_BASE,
		.reg_ofs_cur = AFE_VUL2_CUR,
		.reg_ofs_end = AFE_VUL2_END,
		.reg_ofs_base_msb = AFE_VUL2_BASE_MSB,
		.reg_ofs_cur_msb = AFE_VUL2_CUR_MSB,
		.reg_ofs_end_msb = AFE_VUL2_END_MSB,
		.fs_reg = AFE_VUL2_CON0,
		.fs_shift = VUL2_MODE_SFT,
		.fs_maskbit = VUL2_MODE_MASK,
		.mono_reg = AFE_VUL2_CON0,
		.mono_shift = VUL2_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = VUL2_ON_SFT,
		.hd_reg = AFE_VUL2_CON0,
		.hd_shift = VUL2_HD_MODE_SFT,
		.hd_align_reg = AFE_VUL2_CON0,
		.hd_align_mshift = VUL2_HALIGN_SFT,
	},
	[MT8192_MEMIF_AWB] = {
		.name = "AWB",
		.id = MT8192_MEMIF_AWB,
		.reg_ofs_base = AFE_AWB_BASE,
		.reg_ofs_cur = AFE_AWB_CUR,
		.reg_ofs_end = AFE_AWB_END,
		.reg_ofs_base_msb = AFE_AWB_BASE_MSB,
		.reg_ofs_cur_msb = AFE_AWB_CUR_MSB,
		.reg_ofs_end_msb = AFE_AWB_END_MSB,
		.fs_reg = AFE_AWB_CON0,
		.fs_shift = AWB_MODE_SFT,
		.fs_maskbit = AWB_MODE_MASK,
		.mono_reg = AFE_AWB_CON0,
		.mono_shift = AWB_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = AWB_ON_SFT,
		.hd_reg = AFE_AWB_CON0,
		.hd_shift = AWB_HD_MODE_SFT,
		.hd_align_reg = AFE_AWB_CON0,
		.hd_align_mshift = AWB_HALIGN_SFT,
	},
	[MT8192_MEMIF_AWB2] = {
		.name = "AWB2",
		.id = MT8192_MEMIF_AWB2,
		.reg_ofs_base = AFE_AWB2_BASE,
		.reg_ofs_cur = AFE_AWB2_CUR,
		.reg_ofs_end = AFE_AWB2_END,
		.reg_ofs_base_msb = AFE_AWB2_BASE_MSB,
		.reg_ofs_cur_msb = AFE_AWB2_CUR_MSB,
		.reg_ofs_end_msb = AFE_AWB2_END_MSB,
		.fs_reg = AFE_AWB2_CON0,
		.fs_shift = AWB2_MODE_SFT,
		.fs_maskbit = AWB2_MODE_MASK,
		.mono_reg = AFE_AWB2_CON0,
		.mono_shift = AWB2_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = AWB2_ON_SFT,
		.hd_reg = AFE_AWB2_CON0,
		.hd_shift = AWB2_HD_MODE_SFT,
		.hd_align_reg = AFE_AWB2_CON0,
		.hd_align_mshift = AWB2_HALIGN_SFT,
	},
	[MT8192_MEMIF_VUL3] = {
		.name = "VUL3",
		.id = MT8192_MEMIF_VUL3,
		.reg_ofs_base = AFE_VUL3_BASE,
		.reg_ofs_cur = AFE_VUL3_CUR,
		.reg_ofs_end = AFE_VUL3_END,
		.reg_ofs_base_msb = AFE_VUL3_BASE_MSB,
		.reg_ofs_cur_msb = AFE_VUL3_CUR_MSB,
		.reg_ofs_end_msb = AFE_VUL3_END_MSB,
		.fs_reg = AFE_VUL3_CON0,
		.fs_shift = VUL3_MODE_SFT,
		.fs_maskbit = VUL3_MODE_MASK,
		.mono_reg = AFE_VUL3_CON0,
		.mono_shift = VUL3_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = VUL3_ON_SFT,
		.hd_reg = AFE_VUL3_CON0,
		.hd_shift = VUL3_HD_MODE_SFT,
		.hd_align_reg = AFE_VUL3_CON0,
		.hd_align_mshift = VUL3_HALIGN_SFT,
	},
	[MT8192_MEMIF_VUL4] = {
		.name = "VUL4",
		.id = MT8192_MEMIF_VUL4,
		.reg_ofs_base = AFE_VUL4_BASE,
		.reg_ofs_cur = AFE_VUL4_CUR,
		.reg_ofs_end = AFE_VUL4_END,
		.reg_ofs_base_msb = AFE_VUL4_BASE_MSB,
		.reg_ofs_cur_msb = AFE_VUL4_CUR_MSB,
		.reg_ofs_end_msb = AFE_VUL4_END_MSB,
		.fs_reg = AFE_VUL4_CON0,
		.fs_shift = VUL4_MODE_SFT,
		.fs_maskbit = VUL4_MODE_MASK,
		.mono_reg = AFE_VUL4_CON0,
		.mono_shift = VUL4_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = VUL4_ON_SFT,
		.hd_reg = AFE_VUL4_CON0,
		.hd_shift = VUL4_HD_MODE_SFT,
		.hd_align_reg = AFE_VUL4_CON0,
		.hd_align_mshift = VUL4_HALIGN_SFT,
	},
	[MT8192_MEMIF_VUL5] = {
		.name = "VUL5",
		.id = MT8192_MEMIF_VUL5,
		.reg_ofs_base = AFE_VUL5_BASE,
		.reg_ofs_cur = AFE_VUL5_CUR,
		.reg_ofs_end = AFE_VUL5_END,
		.reg_ofs_base_msb = AFE_VUL5_BASE_MSB,
		.reg_ofs_cur_msb = AFE_VUL5_CUR_MSB,
		.reg_ofs_end_msb = AFE_VUL5_END_MSB,
		.fs_reg = AFE_VUL5_CON0,
		.fs_shift = VUL5_MODE_SFT,
		.fs_maskbit = VUL5_MODE_MASK,
		.mono_reg = AFE_VUL5_CON0,
		.mono_shift = VUL5_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = VUL5_ON_SFT,
		.hd_reg = AFE_VUL5_CON0,
		.hd_shift = VUL5_HD_MODE_SFT,
		.hd_align_reg = AFE_VUL5_CON0,
		.hd_align_mshift = VUL5_HALIGN_SFT,
	},
	[MT8192_MEMIF_VUL6] = {
		.name = "VUL6",
		.id = MT8192_MEMIF_VUL6,
		.reg_ofs_base = AFE_VUL6_BASE,
		.reg_ofs_cur = AFE_VUL6_CUR,
		.reg_ofs_end = AFE_VUL6_END,
		.reg_ofs_base_msb = AFE_VUL6_BASE_MSB,
		.reg_ofs_cur_msb = AFE_VUL6_CUR_MSB,
		.reg_ofs_end_msb = AFE_VUL6_END_MSB,
		.fs_reg = AFE_VUL6_CON0,
		.fs_shift = VUL6_MODE_SFT,
		.fs_maskbit = VUL6_MODE_MASK,
		.mono_reg = AFE_VUL6_CON0,
		.mono_shift = VUL6_MONO_SFT,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = VUL6_ON_SFT,
		.hd_reg = AFE_VUL6_CON0,
		.hd_shift = VUL6_HD_MODE_SFT,
		.hd_align_reg = AFE_VUL6_CON0,
		.hd_align_mshift = VUL6_HALIGN_SFT,
	},
	[MT8192_MEMIF_HDMI] = {
		.name = "HDMI",
		.id = MT8192_MEMIF_HDMI,
		.reg_ofs_base = AFE_HDMI_OUT_BASE,
		.reg_ofs_cur = AFE_HDMI_OUT_CUR,
		.reg_ofs_end = AFE_HDMI_OUT_END,
		.reg_ofs_base_msb = AFE_HDMI_OUT_BASE_MSB,
		.reg_ofs_cur_msb = AFE_HDMI_OUT_CUR_MSB,
		.reg_ofs_end_msb = AFE_HDMI_OUT_END_MSB,
		.fs_reg = -1,
		.fs_shift = -1,
		.fs_maskbit = -1,
		.mono_reg = -1,
		.mono_shift = -1,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = HDMI_OUT_ON_SFT,
		.hd_reg = AFE_HDMI_OUT_CON0,
		.hd_shift = HDMI_OUT_HD_MODE_SFT,
		.hd_align_reg = AFE_HDMI_OUT_CON0,
		.hd_align_mshift = HDMI_OUT_HALIGN_SFT,
		.pbuf_reg = AFE_HDMI_OUT_CON0,
		.minlen_reg = AFE_HDMI_OUT_CON0,
		.minlen_shift = HDMI_OUT_MINLEN_SFT,
	},
};

static const struct mtk_base_irq_data irq_data[MT8192_IRQ_NUM] = {
	[MT8192_IRQ_0] = {
		.id = MT8192_IRQ_0,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT0,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON1,
		.irq_fs_shift = IRQ0_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ0_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ0_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ0_MCU_CLR_SFT,
	},
	[MT8192_IRQ_1] = {
		.id = MT8192_IRQ_1,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT1,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON1,
		.irq_fs_shift = IRQ1_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ1_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ1_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ1_MCU_CLR_SFT,
	},
	[MT8192_IRQ_2] = {
		.id = MT8192_IRQ_2,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT2,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON1,
		.irq_fs_shift = IRQ2_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ2_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ2_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ2_MCU_CLR_SFT,
	},
	[MT8192_IRQ_3] = {
		.id = MT8192_IRQ_3,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT3,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON1,
		.irq_fs_shift = IRQ3_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ3_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ3_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ3_MCU_CLR_SFT,
	},
	[MT8192_IRQ_4] = {
		.id = MT8192_IRQ_4,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT4,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON1,
		.irq_fs_shift = IRQ4_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ4_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ4_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ4_MCU_CLR_SFT,
	},
	[MT8192_IRQ_5] = {
		.id = MT8192_IRQ_5,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT5,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON1,
		.irq_fs_shift = IRQ5_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ5_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ5_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ5_MCU_CLR_SFT,
	},
	[MT8192_IRQ_6] = {
		.id = MT8192_IRQ_6,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT6,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON1,
		.irq_fs_shift = IRQ6_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ6_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ6_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ6_MCU_CLR_SFT,
	},
	[MT8192_IRQ_7] = {
		.id = MT8192_IRQ_7,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT7,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON1,
		.irq_fs_shift = IRQ7_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ7_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ7_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ7_MCU_CLR_SFT,
	},
	[MT8192_IRQ_8] = {
		.id = MT8192_IRQ_8,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT8,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON2,
		.irq_fs_shift = IRQ8_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ8_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ8_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ8_MCU_CLR_SFT,
	},
	[MT8192_IRQ_9] = {
		.id = MT8192_IRQ_9,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT9,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON2,
		.irq_fs_shift = IRQ9_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ9_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ9_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ9_MCU_CLR_SFT,
	},
	[MT8192_IRQ_10] = {
		.id = MT8192_IRQ_10,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT10,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON2,
		.irq_fs_shift = IRQ10_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ10_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ10_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ10_MCU_CLR_SFT,
	},
	[MT8192_IRQ_11] = {
		.id = MT8192_IRQ_11,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT11,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON2,
		.irq_fs_shift = IRQ11_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ11_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ11_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ11_MCU_CLR_SFT,
	},
	[MT8192_IRQ_12] = {
		.id = MT8192_IRQ_12,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT12,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON2,
		.irq_fs_shift = IRQ12_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ12_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ12_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ12_MCU_CLR_SFT,
	},
	[MT8192_IRQ_13] = {
		.id = MT8192_IRQ_13,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT13,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON2,
		.irq_fs_shift = IRQ13_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ13_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ13_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ13_MCU_CLR_SFT,
	},
	[MT8192_IRQ_14] = {
		.id = MT8192_IRQ_14,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT14,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON2,
		.irq_fs_shift = IRQ14_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ14_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ14_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ14_MCU_CLR_SFT,
	},
	[MT8192_IRQ_15] = {
		.id = MT8192_IRQ_15,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT15,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON2,
		.irq_fs_shift = IRQ15_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ15_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ15_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ15_MCU_CLR_SFT,
	},
	[MT8192_IRQ_16] = {
		.id = MT8192_IRQ_16,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT16,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON3,
		.irq_fs_shift = IRQ16_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ16_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ16_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ16_MCU_CLR_SFT,
	},
	[MT8192_IRQ_17] = {
		.id = MT8192_IRQ_17,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT17,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON3,
		.irq_fs_shift = IRQ17_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ17_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ17_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ17_MCU_CLR_SFT,
	},
	[MT8192_IRQ_18] = {
		.id = MT8192_IRQ_18,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT18,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON3,
		.irq_fs_shift = IRQ18_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ18_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ18_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ18_MCU_CLR_SFT,
	},
	[MT8192_IRQ_19] = {
		.id = MT8192_IRQ_19,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT19,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON3,
		.irq_fs_shift = IRQ19_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ19_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ19_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ19_MCU_CLR_SFT,
	},
	[MT8192_IRQ_20] = {
		.id = MT8192_IRQ_20,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT20,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON3,
		.irq_fs_shift = IRQ20_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ20_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ20_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ20_MCU_CLR_SFT,
	},
	[MT8192_IRQ_21] = {
		.id = MT8192_IRQ_21,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT21,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON3,
		.irq_fs_shift = IRQ21_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ21_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ21_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ21_MCU_CLR_SFT,
	},
	[MT8192_IRQ_22] = {
		.id = MT8192_IRQ_22,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT22,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON3,
		.irq_fs_shift = IRQ22_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ22_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ22_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ22_MCU_CLR_SFT,
	},
	[MT8192_IRQ_23] = {
		.id = MT8192_IRQ_23,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT23,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON3,
		.irq_fs_shift = IRQ23_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ23_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ23_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ23_MCU_CLR_SFT,
	},
	[MT8192_IRQ_24] = {
		.id = MT8192_IRQ_24,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT24,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON4,
		.irq_fs_shift = IRQ24_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ24_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ24_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ24_MCU_CLR_SFT,
	},
	[MT8192_IRQ_25] = {
		.id = MT8192_IRQ_25,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT25,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON4,
		.irq_fs_shift = IRQ25_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ25_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ25_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ25_MCU_CLR_SFT,
	},
	[MT8192_IRQ_26] = {
		.id = MT8192_IRQ_26,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT26,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = AFE_IRQ_MCU_CON4,
		.irq_fs_shift = IRQ26_MCU_MODE_SFT,
		.irq_fs_maskbit = IRQ26_MCU_MODE_MASK,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ26_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ26_MCU_CLR_SFT,
	},
	[MT8192_IRQ_31] = {
		.id = MT8192_IRQ_31,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT31,
		.irq_cnt_shift = AFE_IRQ_CNT_SHIFT,
		.irq_cnt_maskbit = AFE_IRQ_CNT_MASK,
		.irq_fs_reg = -1,
		.irq_fs_shift = -1,
		.irq_fs_maskbit = -1,
		.irq_en_reg = AFE_IRQ_MCU_CON0,
		.irq_en_shift = IRQ31_MCU_ON_SFT,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = IRQ31_MCU_CLR_SFT,
	},
};

static const int memif_irq_usage[MT8192_MEMIF_NUM] = {
	[MT8192_MEMIF_DL1] = MT8192_IRQ_0,
	[MT8192_MEMIF_DL2] = MT8192_IRQ_1,
	[MT8192_MEMIF_DL3] = MT8192_IRQ_2,
	[MT8192_MEMIF_DL4] = MT8192_IRQ_3,
	[MT8192_MEMIF_DL5] = MT8192_IRQ_4,
	[MT8192_MEMIF_DL6] = MT8192_IRQ_5,
	[MT8192_MEMIF_DL7] = MT8192_IRQ_6,
	[MT8192_MEMIF_DL8] = MT8192_IRQ_7,
	[MT8192_MEMIF_DL9] = MT8192_IRQ_8,
	[MT8192_MEMIF_DL12] = MT8192_IRQ_9,
	[MT8192_MEMIF_DAI] = MT8192_IRQ_10,
	[MT8192_MEMIF_MOD_DAI] = MT8192_IRQ_11,
	[MT8192_MEMIF_DAI2] = MT8192_IRQ_12,
	[MT8192_MEMIF_VUL12] = MT8192_IRQ_13,
	[MT8192_MEMIF_VUL2] = MT8192_IRQ_14,
	[MT8192_MEMIF_AWB] = MT8192_IRQ_15,
	[MT8192_MEMIF_AWB2] = MT8192_IRQ_16,
	[MT8192_MEMIF_VUL3] = MT8192_IRQ_17,
	[MT8192_MEMIF_VUL4] = MT8192_IRQ_18,
	[MT8192_MEMIF_VUL5] = MT8192_IRQ_19,
	[MT8192_MEMIF_VUL6] = MT8192_IRQ_20,
	[MT8192_MEMIF_HDMI] = MT8192_IRQ_31,
};

static bool mt8192_is_volatile_reg(struct device *dev, unsigned int reg)
{
	/* these auto-gen reg has read-only bit, so put it as volatile */
	/* volatile reg cannot be cached, so cannot be set when power off */
	switch (reg) {
	case AUDIO_TOP_CON0:	/* reg bit controlled by CCF */
	case AUDIO_TOP_CON1:	/* reg bit controlled by CCF */
	case AUDIO_TOP_CON2:
	case AUDIO_TOP_CON3:
	case AFE_DL1_CUR_MSB:
	case AFE_DL1_CUR:
	case AFE_DL1_END:
	case AFE_DL2_CUR_MSB:
	case AFE_DL2_CUR:
	case AFE_DL2_END:
	case AFE_DL3_CUR_MSB:
	case AFE_DL3_CUR:
	case AFE_DL3_END:
	case AFE_DL4_CUR_MSB:
	case AFE_DL4_CUR:
	case AFE_DL4_END:
	case AFE_DL12_CUR_MSB:
	case AFE_DL12_CUR:
	case AFE_DL12_END:
	case AFE_ADDA_SRC_DEBUG_MON0:
	case AFE_ADDA_SRC_DEBUG_MON1:
	case AFE_ADDA_UL_SRC_MON0:
	case AFE_ADDA_UL_SRC_MON1:
	case AFE_SECURE_CON0:
	case AFE_SRAM_BOUND:
	case AFE_SECURE_CON1:
	case AFE_VUL_CUR_MSB:
	case AFE_VUL_CUR:
	case AFE_VUL_END:
	case AFE_ADDA_3RD_DAC_DL_SDM_FIFO_MON:
	case AFE_ADDA_3RD_DAC_DL_SRC_LCH_MON:
	case AFE_ADDA_3RD_DAC_DL_SRC_RCH_MON:
	case AFE_ADDA_3RD_DAC_DL_SDM_OUT_MON:
	case AFE_SIDETONE_MON:
	case AFE_SIDETONE_CON0:
	case AFE_SIDETONE_COEFF:
	case AFE_VUL2_CUR_MSB:
	case AFE_VUL2_CUR:
	case AFE_VUL2_END:
	case AFE_VUL3_CUR_MSB:
	case AFE_VUL3_CUR:
	case AFE_VUL3_END:
	case AFE_I2S_MON:
	case AFE_DAC_MON:
	case AFE_IRQ0_MCU_CNT_MON:
	case AFE_IRQ6_MCU_CNT_MON:
	case AFE_VUL4_CUR_MSB:
	case AFE_VUL4_CUR:
	case AFE_VUL4_END:
	case AFE_VUL12_CUR_MSB:
	case AFE_VUL12_CUR:
	case AFE_VUL12_END:
	case AFE_IRQ3_MCU_CNT_MON:
	case AFE_IRQ4_MCU_CNT_MON:
	case AFE_IRQ_MCU_STATUS:
	case AFE_IRQ_MCU_CLR:
	case AFE_IRQ_MCU_MON2:
	case AFE_IRQ1_MCU_CNT_MON:
	case AFE_IRQ2_MCU_CNT_MON:
	case AFE_IRQ5_MCU_CNT_MON:
	case AFE_IRQ7_MCU_CNT_MON:
	case AFE_IRQ_MCU_MISS_CLR:
	case AFE_GAIN1_CUR:
	case AFE_GAIN2_CUR:
	case AFE_SRAM_DELSEL_CON1:
	case PCM_INTF_CON2:
	case FPGA_CFG0:
	case FPGA_CFG1:
	case FPGA_CFG2:
	case FPGA_CFG3:
	case AUDIO_TOP_DBG_MON0:
	case AUDIO_TOP_DBG_MON1:
	case AFE_IRQ8_MCU_CNT_MON:
	case AFE_IRQ11_MCU_CNT_MON:
	case AFE_IRQ12_MCU_CNT_MON:
	case AFE_IRQ9_MCU_CNT_MON:
	case AFE_IRQ10_MCU_CNT_MON:
	case AFE_IRQ13_MCU_CNT_MON:
	case AFE_IRQ14_MCU_CNT_MON:
	case AFE_IRQ15_MCU_CNT_MON:
	case AFE_IRQ16_MCU_CNT_MON:
	case AFE_IRQ17_MCU_CNT_MON:
	case AFE_IRQ18_MCU_CNT_MON:
	case AFE_IRQ19_MCU_CNT_MON:
	case AFE_IRQ20_MCU_CNT_MON:
	case AFE_IRQ21_MCU_CNT_MON:
	case AFE_IRQ22_MCU_CNT_MON:
	case AFE_IRQ23_MCU_CNT_MON:
	case AFE_IRQ24_MCU_CNT_MON:
	case AFE_IRQ25_MCU_CNT_MON:
	case AFE_IRQ26_MCU_CNT_MON:
	case AFE_IRQ31_MCU_CNT_MON:
	case AFE_CBIP_MON0:
	case AFE_CBIP_SLV_MUX_MON0:
	case AFE_CBIP_SLV_DECODER_MON0:
	case AFE_ADDA6_MTKAIF_MON0:
	case AFE_ADDA6_MTKAIF_MON1:
	case AFE_AWB_CUR_MSB:
	case AFE_AWB_CUR:
	case AFE_AWB_END:
	case AFE_AWB2_CUR_MSB:
	case AFE_AWB2_CUR:
	case AFE_AWB2_END:
	case AFE_DAI_CUR_MSB:
	case AFE_DAI_CUR:
	case AFE_DAI_END:
	case AFE_DAI2_CUR_MSB:
	case AFE_DAI2_CUR:
	case AFE_DAI2_END:
	case AFE_ADDA6_SRC_DEBUG_MON0:
	case AFE_ADD6A_UL_SRC_MON0:
	case AFE_ADDA6_UL_SRC_MON1:
	case AFE_MOD_DAI_CUR_MSB:
	case AFE_MOD_DAI_CUR:
	case AFE_MOD_DAI_END:
	case AFE_HDMI_OUT_CUR_MSB:
	case AFE_HDMI_OUT_CUR:
	case AFE_HDMI_OUT_END:
	case AFE_AWB_RCH_MON:
	case AFE_AWB_LCH_MON:
	case AFE_VUL_RCH_MON:
	case AFE_VUL_LCH_MON:
	case AFE_VUL12_RCH_MON:
	case AFE_VUL12_LCH_MON:
	case AFE_VUL2_RCH_MON:
	case AFE_VUL2_LCH_MON:
	case AFE_DAI_DATA_MON:
	case AFE_MOD_DAI_DATA_MON:
	case AFE_DAI2_DATA_MON:
	case AFE_AWB2_RCH_MON:
	case AFE_AWB2_LCH_MON:
	case AFE_VUL3_RCH_MON:
	case AFE_VUL3_LCH_MON:
	case AFE_VUL4_RCH_MON:
	case AFE_VUL4_LCH_MON:
	case AFE_VUL5_RCH_MON:
	case AFE_VUL5_LCH_MON:
	case AFE_VUL6_RCH_MON:
	case AFE_VUL6_LCH_MON:
	case AFE_DL1_RCH_MON:
	case AFE_DL1_LCH_MON:
	case AFE_DL2_RCH_MON:
	case AFE_DL2_LCH_MON:
	case AFE_DL12_RCH1_MON:
	case AFE_DL12_LCH1_MON:
	case AFE_DL12_RCH2_MON:
	case AFE_DL12_LCH2_MON:
	case AFE_DL3_RCH_MON:
	case AFE_DL3_LCH_MON:
	case AFE_DL4_RCH_MON:
	case AFE_DL4_LCH_MON:
	case AFE_DL5_RCH_MON:
	case AFE_DL5_LCH_MON:
	case AFE_DL6_RCH_MON:
	case AFE_DL6_LCH_MON:
	case AFE_DL7_RCH_MON:
	case AFE_DL7_LCH_MON:
	case AFE_DL8_RCH_MON:
	case AFE_DL8_LCH_MON:
	case AFE_VUL5_CUR_MSB:
	case AFE_VUL5_CUR:
	case AFE_VUL5_END:
	case AFE_VUL6_CUR_MSB:
	case AFE_VUL6_CUR:
	case AFE_VUL6_END:
	case AFE_ADDA_DL_SDM_FIFO_MON:
	case AFE_ADDA_DL_SRC_LCH_MON:
	case AFE_ADDA_DL_SRC_RCH_MON:
	case AFE_ADDA_DL_SDM_OUT_MON:
	case AFE_CONNSYS_I2S_MON:
	case AFE_ASRC_2CH_CON0:
	case AFE_ASRC_2CH_CON2:
	case AFE_ASRC_2CH_CON3:
	case AFE_ASRC_2CH_CON4:
	case AFE_ASRC_2CH_CON5:
	case AFE_ASRC_2CH_CON7:
	case AFE_ASRC_2CH_CON8:
	case AFE_ASRC_2CH_CON12:
	case AFE_ASRC_2CH_CON13:
	case AFE_DL9_CUR_MSB:
	case AFE_DL9_CUR:
	case AFE_DL9_END:
	case AFE_ADDA_MTKAIF_MON0:
	case AFE_ADDA_MTKAIF_MON1:
	case AFE_DL_NLE_R_MON0:
	case AFE_DL_NLE_R_MON1:
	case AFE_DL_NLE_R_MON2:
	case AFE_DL_NLE_L_MON0:
	case AFE_DL_NLE_L_MON1:
	case AFE_DL_NLE_L_MON2:
	case AFE_GENERAL1_ASRC_2CH_CON0:
	case AFE_GENERAL1_ASRC_2CH_CON2:
	case AFE_GENERAL1_ASRC_2CH_CON3:
	case AFE_GENERAL1_ASRC_2CH_CON4:
	case AFE_GENERAL1_ASRC_2CH_CON5:
	case AFE_GENERAL1_ASRC_2CH_CON7:
	case AFE_GENERAL1_ASRC_2CH_CON8:
	case AFE_GENERAL1_ASRC_2CH_CON12:
	case AFE_GENERAL1_ASRC_2CH_CON13:
	case AFE_GENERAL2_ASRC_2CH_CON0:
	case AFE_GENERAL2_ASRC_2CH_CON2:
	case AFE_GENERAL2_ASRC_2CH_CON3:
	case AFE_GENERAL2_ASRC_2CH_CON4:
	case AFE_GENERAL2_ASRC_2CH_CON5:
	case AFE_GENERAL2_ASRC_2CH_CON7:
	case AFE_GENERAL2_ASRC_2CH_CON8:
	case AFE_GENERAL2_ASRC_2CH_CON12:
	case AFE_GENERAL2_ASRC_2CH_CON13:
	case AFE_DL9_RCH_MON:
	case AFE_DL9_LCH_MON:
	case AFE_DL5_CUR_MSB:
	case AFE_DL5_CUR:
	case AFE_DL5_END:
	case AFE_DL6_CUR_MSB:
	case AFE_DL6_CUR:
	case AFE_DL6_END:
	case AFE_DL7_CUR_MSB:
	case AFE_DL7_CUR:
	case AFE_DL7_END:
	case AFE_DL8_CUR_MSB:
	case AFE_DL8_CUR:
	case AFE_DL8_END:
	case AFE_PROT_SIDEBAND_MON:
	case AFE_DOMAIN_SIDEBAND0_MON:
	case AFE_DOMAIN_SIDEBAND1_MON:
	case AFE_DOMAIN_SIDEBAND2_MON:
	case AFE_DOMAIN_SIDEBAND3_MON:
	case AFE_APLL1_TUNER_CFG:	/* [20:31] is monitor */
	case AFE_APLL2_TUNER_CFG:	/* [20:31] is monitor */
	case AFE_DAC_CON0:
	case AFE_IRQ_MCU_CON0:
	case AFE_IRQ_MCU_EN:
		return true;
	default:
		return false;
	};
}

static const struct regmap_config mt8192_afe_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.volatile_reg = mt8192_is_volatile_reg,
	.max_register = AFE_MAX_REGISTER,
	.num_reg_defaults_raw = AFE_MAX_REGISTER,
	.cache_type = REGCACHE_FLAT,
};

static irqreturn_t mt8192_afe_irq_handler(int irq_id, void *dev)
{
	struct mtk_base_afe *afe = dev;
	struct mtk_base_afe_irq *irq;
	unsigned int status;
	unsigned int status_mcu;
	unsigned int mcu_en;
	int ret;
	int i;

	/* get irq that is sent to MCU */
	regmap_read(afe->regmap, AFE_IRQ_MCU_EN, &mcu_en);

	ret = regmap_read(afe->regmap, AFE_IRQ_MCU_STATUS, &status);
	/* only care IRQ which is sent to MCU */
	status_mcu = status & mcu_en & AFE_IRQ_STATUS_BITS;

	if (ret || status_mcu == 0) {
		dev_err(afe->dev, "%s(), irq status err, ret %d, status 0x%x, mcu_en 0x%x\n",
			__func__, ret, status, mcu_en);

		goto err_irq;
	}

	for (i = 0; i < MT8192_MEMIF_NUM; i++) {
		struct mtk_base_afe_memif *memif = &afe->memif[i];

		if (!memif->substream)
			continue;

		if (memif->irq_usage < 0)
			continue;

		irq = &afe->irqs[memif->irq_usage];

		if (status_mcu & (1 << irq->irq_data->irq_en_shift))
			snd_pcm_period_elapsed(memif->substream);
	}

err_irq:
	/* clear irq */
	regmap_write(afe->regmap,
		     AFE_IRQ_MCU_CLR,
		     status_mcu);

	return IRQ_HANDLED;
}

static int mt8192_afe_runtime_suspend(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	unsigned int value;
	int ret;

	if (!afe->regmap || afe_priv->pm_runtime_bypass_reg_ctl)
		goto skip_regmap;

	/* disable AFE */
	regmap_update_bits(afe->regmap, AFE_DAC_CON0, AFE_ON_MASK_SFT, 0x0);

	ret = regmap_read_poll_timeout(afe->regmap,
				       AFE_DAC_MON,
				       value,
				       (value & AFE_ON_RETM_MASK_SFT) == 0,
				       20,
				       1 * 1000 * 1000);
	if (ret)
		dev_warn(afe->dev, "%s(), ret %d\n", __func__, ret);

	/* make sure all irq status are cleared */
	regmap_write(afe->regmap, AFE_IRQ_MCU_CLR, 0xffffffff);
	regmap_write(afe->regmap, AFE_IRQ_MCU_CLR, 0xffffffff);

	/* reset sgen */
	regmap_write(afe->regmap, AFE_SINEGEN_CON0, 0x0);
	regmap_update_bits(afe->regmap, AFE_SINEGEN_CON2,
			   INNER_LOOP_BACK_MODE_MASK_SFT,
			   0x3f << INNER_LOOP_BACK_MODE_SFT);

	/* cache only */
	regcache_cache_only(afe->regmap, true);
	regcache_mark_dirty(afe->regmap);

skip_regmap:
	mt8192_afe_disable_clock(afe);
	return 0;
}

static int mt8192_afe_runtime_resume(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	struct mt8192_afe_private *afe_priv = afe->platform_priv;
	int ret;

	ret = mt8192_afe_enable_clock(afe);
	if (ret)
		return ret;

	if (!afe->regmap || afe_priv->pm_runtime_bypass_reg_ctl)
		goto skip_regmap;

	regcache_cache_only(afe->regmap, false);
	regcache_sync(afe->regmap);

	/* enable audio sys DCM for power saving */
	regmap_update_bits(afe_priv->infracfg,
			   PERI_BUS_DCM_CTRL, 0x1 << 29, 0x1 << 29);
	regmap_update_bits(afe->regmap, AUDIO_TOP_CON0, 0x1 << 29, 0x1 << 29);

	/* force cpu use 8_24 format when writing 32bit data */
	regmap_update_bits(afe->regmap, AFE_MEMIF_CON0,
			   CPU_HD_ALIGN_MASK_SFT, 0 << CPU_HD_ALIGN_SFT);

	/* set all output port to 24bit */
	regmap_write(afe->regmap, AFE_CONN_24BIT, 0xffffffff);
	regmap_write(afe->regmap, AFE_CONN_24BIT_1, 0xffffffff);

	/* enable AFE */
	regmap_update_bits(afe->regmap, AFE_DAC_CON0, AFE_ON_MASK_SFT, 0x1);

skip_regmap:
	return 0;
}

static int mt8192_dai_memif_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mt8192_memif_dai_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mt8192_memif_dai_driver);

	dai->dapm_widgets = mt8192_memif_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mt8192_memif_widgets);
	dai->dapm_routes = mt8192_memif_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mt8192_memif_routes);
	return 0;
}

typedef int (*dai_register_cb)(struct mtk_base_afe *);
static const dai_register_cb dai_register_cbs[] = {
	mt8192_dai_adda_register,
	mt8192_dai_i2s_register,
	mt8192_dai_pcm_register,
	mt8192_dai_tdm_register,
	mt8192_dai_memif_register,
};

static int mt8192_afe_pcm_dev_probe(struct platform_device *pdev)
{
	struct mtk_base_afe *afe;
	struct mt8192_afe_private *afe_priv;
	struct device *dev;
	struct reset_control *rstc;
	int i, ret, irq_id;

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(34));
	if (ret)
		return ret;

	afe = devm_kzalloc(&pdev->dev, sizeof(*afe), GFP_KERNEL);
	if (!afe)
		return -ENOMEM;
	platform_set_drvdata(pdev, afe);

	afe->platform_priv = devm_kzalloc(&pdev->dev, sizeof(*afe_priv),
					  GFP_KERNEL);
	if (!afe->platform_priv)
		return -ENOMEM;
	afe_priv = afe->platform_priv;

	afe->dev = &pdev->dev;
	dev = afe->dev;

	/* init audio related clock */
	ret = mt8192_init_clock(afe);
	if (ret) {
		dev_err(dev, "init clock error\n");
		return ret;
	}

	/* reset controller to reset audio regs before regmap cache */
	rstc = devm_reset_control_get_exclusive(dev, "audiosys");
	if (IS_ERR(rstc))
		return dev_err_probe(dev, PTR_ERR(rstc), "could not get audiosys reset\n");

	ret = reset_control_reset(rstc);
	if (ret)
		return dev_err_probe(dev, ret, "failed to trigger audio reset\n");

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	/* regmap init */
	afe->regmap = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(afe->regmap))
		return dev_err_probe(dev, PTR_ERR(afe->regmap),
				     "could not get regmap from parent");

	ret = regmap_attach_dev(dev, afe->regmap, &mt8192_afe_regmap_config);
	if (ret)
		return dev_err_probe(dev, ret, "regmap_attach_dev fail\n");

	/* enable clock for regcache get default value from hw */
	afe_priv->pm_runtime_bypass_reg_ctl = true;
	pm_runtime_get_sync(&pdev->dev);

	ret = regmap_reinit_cache(afe->regmap, &mt8192_afe_regmap_config);
	if (ret)
		return dev_err_probe(dev, ret, "regmap_reinit_cache fail\n");

	pm_runtime_put_sync(&pdev->dev);
	afe_priv->pm_runtime_bypass_reg_ctl = false;

	regcache_cache_only(afe->regmap, true);
	regcache_mark_dirty(afe->regmap);

	/* init memif */
	afe->memif_size = MT8192_MEMIF_NUM;
	afe->memif = devm_kcalloc(dev, afe->memif_size, sizeof(*afe->memif),
				  GFP_KERNEL);
	if (!afe->memif)
		return -ENOMEM;

	for (i = 0; i < afe->memif_size; i++) {
		afe->memif[i].data = &memif_data[i];
		afe->memif[i].irq_usage = memif_irq_usage[i];
		afe->memif[i].const_irq = 1;
	}

	mutex_init(&afe->irq_alloc_lock);	/* needed when dynamic irq */

	/* init irq */
	afe->irqs_size = MT8192_IRQ_NUM;
	afe->irqs = devm_kcalloc(dev, afe->irqs_size, sizeof(*afe->irqs),
				 GFP_KERNEL);
	if (!afe->irqs)
		return -ENOMEM;

	for (i = 0; i < afe->irqs_size; i++)
		afe->irqs[i].irq_data = &irq_data[i];

	/* request irq */
	irq_id = platform_get_irq(pdev, 0);
	if (irq_id < 0)
		return irq_id;

	ret = devm_request_irq(dev, irq_id, mt8192_afe_irq_handler,
			       IRQF_TRIGGER_NONE, "asys-isr", (void *)afe);
	if (ret)
		return dev_err_probe(dev, ret, "could not request_irq for Afe_ISR_Handle\n");

	/* init sub_dais */
	INIT_LIST_HEAD(&afe->sub_dais);

	for (i = 0; i < ARRAY_SIZE(dai_register_cbs); i++) {
		ret = dai_register_cbs[i](afe);
		if (ret)
			return dev_err_probe(afe->dev, ret, "dai %d register fail", i);
	}

	/* init dai_driver and component_driver */
	ret = mtk_afe_combine_sub_dai(afe);
	if (ret)
		return dev_err_probe(afe->dev, ret, "mtk_afe_combine_sub_dai fail\n");

	/* others */
	afe->mtk_afe_hardware = &mt8192_afe_hardware;
	afe->memif_fs = mt8192_memif_fs;
	afe->irq_fs = mt8192_irq_fs;
	afe->get_dai_fs = mt8192_get_dai_fs;
	afe->get_memif_pbuf_size = mt8192_get_memif_pbuf_size;
	afe->memif_32bit_supported = 1;

	afe->runtime_resume = mt8192_afe_runtime_resume;
	afe->runtime_suspend = mt8192_afe_runtime_suspend;

	/* register platform */
	ret = devm_snd_soc_register_component(&pdev->dev,
					      &mtk_afe_pcm_platform,
					      afe->dai_drivers,
					      afe->num_dai_drivers);
	if (ret)
		return dev_err_probe(dev, ret, "Couldn't register AFE component\n");

	return 0;
}

static void mt8192_afe_pcm_dev_remove(struct platform_device *pdev)
{
	struct mtk_base_afe *afe = platform_get_drvdata(pdev);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		mt8192_afe_runtime_suspend(&pdev->dev);

	/* disable afe clock */
	mt8192_afe_disable_clock(afe);
}

static const struct of_device_id mt8192_afe_pcm_dt_match[] = {
	{ .compatible = "mediatek,mt8192-audio", },
	{},
};
MODULE_DEVICE_TABLE(of, mt8192_afe_pcm_dt_match);

static const struct dev_pm_ops mt8192_afe_pm_ops = {
	SET_RUNTIME_PM_OPS(mt8192_afe_runtime_suspend,
			   mt8192_afe_runtime_resume, NULL)
};

static struct platform_driver mt8192_afe_pcm_driver = {
	.driver = {
		   .name = "mt8192-audio",
		   .of_match_table = mt8192_afe_pcm_dt_match,
		   .pm = &mt8192_afe_pm_ops,
	},
	.probe = mt8192_afe_pcm_dev_probe,
	.remove = mt8192_afe_pcm_dev_remove,
};

module_platform_driver(mt8192_afe_pcm_driver);

MODULE_DESCRIPTION("Mediatek ALSA SoC AFE platform driver for 8192");
MODULE_AUTHOR("Shane Chien <shane.chien@mediatek.com>");
MODULE_LICENSE("GPL v2");
