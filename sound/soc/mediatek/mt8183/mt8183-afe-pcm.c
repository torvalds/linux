// SPDX-License-Identifier: GPL-2.0
//
// Mediatek ALSA SoC AFE platform driver for 8183
//
// Copyright (c) 2018 MediaTek Inc.
// Author: KaiChieh Chuang <kaichieh.chuang@mediatek.com>

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#include "mt8183-afe-common.h"
#include "mt8183-afe-clk.h"
#include "mt8183-interconnection.h"
#include "mt8183-reg.h"
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
	MTK_AFE_RATE_176K = 13,
	MTK_AFE_RATE_192K = 14,
	MTK_AFE_RATE_260K = 15,
};

enum {
	MTK_AFE_DAI_MEMIF_RATE_8K = 0,
	MTK_AFE_DAI_MEMIF_RATE_16K = 1,
	MTK_AFE_DAI_MEMIF_RATE_32K = 2,
	MTK_AFE_DAI_MEMIF_RATE_48K = 3,
};

enum {
	MTK_AFE_PCM_RATE_8K = 0,
	MTK_AFE_PCM_RATE_16K = 1,
	MTK_AFE_PCM_RATE_32K = 2,
	MTK_AFE_PCM_RATE_48K = 3,
};

unsigned int mt8183_general_rate_transform(struct device *dev,
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
		return MTK_AFE_RATE_176K;
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
	case 48000:
		return MTK_AFE_DAI_MEMIF_RATE_48K;
	default:
		dev_warn(dev, "%s(), rate %u invalid, use %d!!!\n",
			 __func__, rate, MTK_AFE_DAI_MEMIF_RATE_16K);
		return MTK_AFE_DAI_MEMIF_RATE_16K;
	}
}

unsigned int mt8183_rate_transform(struct device *dev,
				   unsigned int rate, int aud_blk)
{
	switch (aud_blk) {
	case MT8183_MEMIF_MOD_DAI:
		return dai_memif_rate_transform(dev, rate);
	default:
		return mt8183_general_rate_transform(dev, rate);
	}
}

static const struct snd_pcm_hardware mt8183_afe_hardware = {
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

static int mt8183_memif_fs(struct snd_pcm_substream *substream,
			   unsigned int rate)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);
	int id = snd_soc_rtd_to_cpu(rtd, 0)->id;

	return mt8183_rate_transform(afe->dev, rate, id);
}

static int mt8183_irq_fs(struct snd_pcm_substream *substream, unsigned int rate)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct snd_soc_component *component =
		snd_soc_rtdcom_lookup(rtd, AFE_PCM_NAME);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(component);

	return mt8183_general_rate_transform(afe->dev, rate);
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

static struct snd_soc_dai_driver mt8183_memif_dai_driver[] = {
	/* FE DAIs: memory intefaces to CPU */
	{
		.name = "DL1",
		.id = MT8183_MEMIF_DL1,
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
		.id = MT8183_MEMIF_DL2,
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
		.id = MT8183_MEMIF_DL3,
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
		.id = MT8183_MEMIF_VUL12,
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
		.id = MT8183_MEMIF_AWB,
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
		.id = MT8183_MEMIF_VUL2,
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
		.id = MT8183_MEMIF_AWB2,
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
		.name = "UL_MONO_1",
		.id = MT8183_MEMIF_MOD_DAI,
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
		.name = "HDMI",
		.id = MT8183_MEMIF_HDMI,
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

/* dma widget & routes*/
static const struct snd_kcontrol_new memif_ul1_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN21,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I2S0_CH1", AFE_CONN21,
				    I_I2S0_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul1_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN22,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I2S0_CH2", AFE_CONN21,
				    I_I2S0_CH2, 1, 0),
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
	SOC_DAPM_SINGLE_AUTODISABLE("I2S2_CH1", AFE_CONN5,
				    I_I2S2_CH1, 1, 0),
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
	SOC_DAPM_SINGLE_AUTODISABLE("I2S2_CH2", AFE_CONN6,
				    I_I2S2_CH2, 1, 0),
};

static const struct snd_kcontrol_new memif_ul3_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN32,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I2S2_CH1", AFE_CONN32,
				    I_I2S2_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul3_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN33,
				    I_ADDA_UL_CH2, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I2S2_CH2", AFE_CONN33,
				    I_I2S2_CH2, 1, 0),
};

static const struct snd_kcontrol_new memif_ul4_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN38,
				    I_ADDA_UL_CH1, 1, 0),
};

static const struct snd_kcontrol_new memif_ul4_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN39,
				    I_ADDA_UL_CH2, 1, 0),
};

static const struct snd_kcontrol_new memif_ul_mono_1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH1", AFE_CONN12,
				    I_ADDA_UL_CH1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("ADDA_UL_CH2", AFE_CONN12,
				    I_ADDA_UL_CH2, 1, 0),
};

static const struct snd_soc_dapm_widget mt8183_memif_widgets[] = {
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

	SND_SOC_DAPM_MIXER("UL4_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul4_ch1_mix, ARRAY_SIZE(memif_ul4_ch1_mix)),
	SND_SOC_DAPM_MIXER("UL4_CH2", SND_SOC_NOPM, 0, 0,
			   memif_ul4_ch2_mix, ARRAY_SIZE(memif_ul4_ch2_mix)),

	SND_SOC_DAPM_MIXER("UL_MONO_1_CH1", SND_SOC_NOPM, 0, 0,
			   memif_ul_mono_1_mix,
			   ARRAY_SIZE(memif_ul_mono_1_mix)),
};

static const struct snd_soc_dapm_route mt8183_memif_routes[] = {
	/* capture */
	{"UL1", NULL, "UL1_CH1"},
	{"UL1", NULL, "UL1_CH2"},
	{"UL1_CH1", "ADDA_UL_CH1", "ADDA Capture"},
	{"UL1_CH2", "ADDA_UL_CH2", "ADDA Capture"},
	{"UL1_CH1", "I2S0_CH1", "I2S0"},
	{"UL1_CH2", "I2S0_CH2", "I2S0"},

	{"UL2", NULL, "UL2_CH1"},
	{"UL2", NULL, "UL2_CH2"},
	{"UL2_CH1", "ADDA_UL_CH1", "ADDA Capture"},
	{"UL2_CH2", "ADDA_UL_CH2", "ADDA Capture"},
	{"UL2_CH1", "I2S2_CH1", "I2S2"},
	{"UL2_CH2", "I2S2_CH2", "I2S2"},

	{"UL3", NULL, "UL3_CH1"},
	{"UL3", NULL, "UL3_CH2"},
	{"UL3_CH1", "ADDA_UL_CH1", "ADDA Capture"},
	{"UL3_CH2", "ADDA_UL_CH2", "ADDA Capture"},
	{"UL3_CH1", "I2S2_CH1", "I2S2"},
	{"UL3_CH2", "I2S2_CH2", "I2S2"},

	{"UL4", NULL, "UL4_CH1"},
	{"UL4", NULL, "UL4_CH2"},
	{"UL4_CH1", "ADDA_UL_CH1", "ADDA Capture"},
	{"UL4_CH2", "ADDA_UL_CH2", "ADDA Capture"},

	{"UL_MONO_1", NULL, "UL_MONO_1_CH1"},
	{"UL_MONO_1_CH1", "ADDA_UL_CH1", "ADDA Capture"},
	{"UL_MONO_1_CH1", "ADDA_UL_CH2", "ADDA Capture"},
};

static const struct snd_soc_component_driver mt8183_afe_pcm_dai_component = {
	.name = "mt8183-afe-pcm-dai",
};

#define MT8183_MEMIF_BASE(_id, _en_reg, _fs_reg, _mono_reg)    \
	[MT8183_MEMIF_##_id] = { \
		.name = #_id,				\
		.id = MT8183_MEMIF_##_id,		\
		.reg_ofs_base = AFE_##_id##_BASE,	\
		.reg_ofs_cur = AFE_##_id##_CUR,		\
		.reg_ofs_end = AFE_##_id##_END,		\
		.reg_ofs_base_msb = AFE_##_id##_BASE_MSB,	\
		.reg_ofs_cur_msb = AFE_##_id##_CUR_MSB,		\
		.reg_ofs_end_msb = AFE_##_id##_END_MSB,		\
		.fs_reg = (_fs_reg),			\
		.fs_shift = _id##_MODE_SFT,		\
		.fs_maskbit = _id##_MODE_MASK,		\
		.mono_reg = (_mono_reg),		\
		.mono_shift = _id##_DATA_SFT,		\
		.enable_reg = (_en_reg),		\
		.enable_shift = _id##_ON_SFT,		\
		.hd_reg = AFE_MEMIF_HD_MODE,		\
		.hd_align_reg = AFE_MEMIF_HDALIGN,	\
		.hd_shift = _id##_HD_SFT,		\
		.hd_align_mshift = _id##_HD_ALIGN_SFT,	\
		.agent_disable_reg = -1,		\
		.agent_disable_shift = -1,		\
		.msb_reg = -1,				\
		.msb_shift = -1,			\
	}

#define MT8183_MEMIF(_id, _fs_reg, _mono_reg) \
		MT8183_MEMIF_BASE(_id, AFE_DAC_CON0, _fs_reg, _mono_reg)

/* For convenience with macros: missing register fields */
#define MOD_DAI_DATA_SFT	-1
#define HDMI_MODE_SFT		-1
#define HDMI_MODE_MASK		-1
#define HDMI_DATA_SFT		-1
#define HDMI_ON_SFT		-1

/* For convenience with macros: register name differences */
#define AFE_VUL12_BASE		AFE_VUL_D2_BASE
#define AFE_VUL12_CUR		AFE_VUL_D2_CUR
#define AFE_VUL12_END		AFE_VUL_D2_END
#define AFE_VUL12_BASE_MSB	AFE_VUL_D2_BASE_MSB
#define AFE_VUL12_CUR_MSB	AFE_VUL_D2_CUR_MSB
#define AFE_VUL12_END_MSB	AFE_VUL_D2_END_MSB
#define AWB2_HD_ALIGN_SFT	AWB2_ALIGN_SFT
#define VUL12_DATA_SFT		VUL12_MONO_SFT
#define AFE_HDMI_BASE		AFE_HDMI_OUT_BASE
#define AFE_HDMI_CUR		AFE_HDMI_OUT_CUR
#define AFE_HDMI_END		AFE_HDMI_OUT_END
#define AFE_HDMI_BASE_MSB	AFE_HDMI_OUT_BASE_MSB
#define AFE_HDMI_CUR_MSB	AFE_HDMI_OUT_CUR_MSB
#define AFE_HDMI_END_MSB	AFE_HDMI_OUT_END_MSB

static const struct mtk_base_memif_data memif_data[MT8183_MEMIF_NUM] = {
	MT8183_MEMIF(DL1, AFE_DAC_CON1, AFE_DAC_CON1),
	MT8183_MEMIF(DL2, AFE_DAC_CON1, AFE_DAC_CON1),
	MT8183_MEMIF(DL3, AFE_DAC_CON2, AFE_DAC_CON1),
	MT8183_MEMIF(VUL2, AFE_DAC_CON2, AFE_DAC_CON2),
	MT8183_MEMIF(AWB, AFE_DAC_CON1, AFE_DAC_CON1),
	MT8183_MEMIF(AWB2, AFE_DAC_CON2, AFE_DAC_CON2),
	MT8183_MEMIF(VUL12, AFE_DAC_CON0, AFE_DAC_CON0),
	MT8183_MEMIF(MOD_DAI, AFE_DAC_CON1, -1),
	/* enable control in tdm for sync start */
	MT8183_MEMIF_BASE(HDMI, -1, -1, -1),
};

#define MT8183_AFE_IRQ_BASE(_id, _fs_reg, _fs_shift, _fs_maskbit)	\
	[MT8183_IRQ_##_id] = {	\
		.id = MT8183_IRQ_##_id,			\
		.irq_cnt_reg = AFE_IRQ_MCU_CNT##_id,	\
		.irq_cnt_shift = 0,			\
		.irq_cnt_maskbit = 0x3ffff,		\
		.irq_fs_reg = _fs_reg,			\
		.irq_fs_shift = _fs_shift,		\
		.irq_fs_maskbit = _fs_maskbit,		\
		.irq_en_reg = AFE_IRQ_MCU_CON0,		\
		.irq_en_shift = IRQ##_id##_MCU_ON_SFT,	\
		.irq_clr_reg = AFE_IRQ_MCU_CLR,		\
		.irq_clr_shift = IRQ##_id##_MCU_CLR_SFT,	\
	}

#define MT8183_AFE_IRQ(_id) \
	MT8183_AFE_IRQ_BASE(_id, AFE_IRQ_MCU_CON1 + _id / 8 * 4,	\
			    IRQ##_id##_MCU_MODE_SFT,	\
			    IRQ##_id##_MCU_MODE_MASK)

#define MT8183_AFE_IRQ_NOFS(_id) MT8183_AFE_IRQ_BASE(_id, -1, -1, -1)

static const struct mtk_base_irq_data irq_data[MT8183_IRQ_NUM] = {
	MT8183_AFE_IRQ(0),
	MT8183_AFE_IRQ(1),
	MT8183_AFE_IRQ(2),
	MT8183_AFE_IRQ(3),
	MT8183_AFE_IRQ(4),
	MT8183_AFE_IRQ(5),
	MT8183_AFE_IRQ(6),
	MT8183_AFE_IRQ(7),
	MT8183_AFE_IRQ_NOFS(8),
	MT8183_AFE_IRQ(11),
	MT8183_AFE_IRQ(12),
};

static bool mt8183_is_volatile_reg(struct device *dev, unsigned int reg)
{
	/* these auto-gen reg has read-only bit, so put it as volatile */
	/* volatile reg cannot be cached, so cannot be set when power off */
	switch (reg) {
	case AUDIO_TOP_CON0 ... AUDIO_TOP_CON1:	/* reg bit controlled by CCF */
	case AUDIO_TOP_CON3:
	case AFE_DL1_CUR ... AFE_DL1_END:
	case AFE_DL2_CUR ... AFE_DL2_END:
	case AFE_AWB_END ... AFE_AWB_CUR:
	case AFE_VUL_END ... AFE_VUL_CUR:
	case AFE_MEMIF_MON0 ... AFE_MEMIF_MON9:
	case AFE_ADDA_SRC_DEBUG_MON0 ... AFE_ADDA_SRC_DEBUG_MON1:
	case AFE_ADDA_UL_SRC_MON0 ... AFE_ADDA_UL_SRC_MON1:
	case AFE_SIDETONE_MON:
	case AFE_SIDETONE_CON0 ... AFE_SIDETONE_COEFF:
	case AFE_BUS_MON0:
	case AFE_MRGIF_MON0 ... AFE_I2S_MON:
	case AFE_DAC_MON:
	case AFE_VUL2_END ... AFE_VUL2_CUR:
	case AFE_IRQ0_MCU_CNT_MON ... AFE_IRQ6_MCU_CNT_MON:
	case AFE_MOD_DAI_END ... AFE_MOD_DAI_CUR:
	case AFE_VUL_D2_END ... AFE_VUL_D2_CUR:
	case AFE_DL3_CUR ... AFE_DL3_END:
	case AFE_HDMI_OUT_CON0:
	case AFE_HDMI_OUT_CUR ... AFE_HDMI_OUT_END:
	case AFE_IRQ3_MCU_CNT_MON... AFE_IRQ4_MCU_CNT_MON:
	case AFE_IRQ_MCU_STATUS ... AFE_IRQ_MCU_CLR:
	case AFE_IRQ_MCU_MON2:
	case AFE_IRQ1_MCU_CNT_MON ... AFE_IRQ5_MCU_CNT_MON:
	case AFE_IRQ7_MCU_CNT_MON:
	case AFE_GAIN1_CUR:
	case AFE_GAIN2_CUR:
	case AFE_SRAM_DELSEL_CON0:
	case AFE_SRAM_DELSEL_CON2 ... AFE_SRAM_DELSEL_CON3:
	case AFE_ASRC_2CH_CON12 ... AFE_ASRC_2CH_CON13:
	case PCM_INTF_CON2:
	case FPGA_CFG0 ... FPGA_CFG1:
	case FPGA_CFG2 ... FPGA_CFG3:
	case AUDIO_TOP_DBG_MON0 ... AUDIO_TOP_DBG_MON1:
	case AFE_IRQ8_MCU_CNT_MON ... AFE_IRQ12_MCU_CNT_MON:
	case AFE_CBIP_MON0:
	case AFE_CBIP_SLV_MUX_MON0 ... AFE_CBIP_SLV_DECODER_MON0:
	case AFE_ADDA6_SRC_DEBUG_MON0:
	case AFE_ADD6A_UL_SRC_MON0... AFE_ADDA6_UL_SRC_MON1:
	case AFE_DL1_CUR_MSB:
	case AFE_DL2_CUR_MSB:
	case AFE_AWB_CUR_MSB:
	case AFE_VUL_CUR_MSB:
	case AFE_VUL2_CUR_MSB:
	case AFE_MOD_DAI_CUR_MSB:
	case AFE_VUL_D2_CUR_MSB:
	case AFE_DL3_CUR_MSB:
	case AFE_HDMI_OUT_CUR_MSB:
	case AFE_AWB2_END ... AFE_AWB2_CUR:
	case AFE_AWB2_CUR_MSB:
	case AFE_ADDA_DL_SDM_FIFO_MON ... AFE_ADDA_DL_SDM_OUT_MON:
	case AFE_CONNSYS_I2S_MON ... AFE_ASRC_2CH_CON0:
	case AFE_ASRC_2CH_CON2 ... AFE_ASRC_2CH_CON5:
	case AFE_ASRC_2CH_CON7 ... AFE_ASRC_2CH_CON8:
	case AFE_MEMIF_MON12 ... AFE_MEMIF_MON24:
	case AFE_ADDA_MTKAIF_MON0 ... AFE_ADDA_MTKAIF_MON1:
	case AFE_AUD_PAD_TOP:
	case AFE_GENERAL1_ASRC_2CH_CON0:
	case AFE_GENERAL1_ASRC_2CH_CON2 ... AFE_GENERAL1_ASRC_2CH_CON5:
	case AFE_GENERAL1_ASRC_2CH_CON7 ... AFE_GENERAL1_ASRC_2CH_CON8:
	case AFE_GENERAL1_ASRC_2CH_CON12 ... AFE_GENERAL1_ASRC_2CH_CON13:
	case AFE_GENERAL2_ASRC_2CH_CON0:
	case AFE_GENERAL2_ASRC_2CH_CON2 ... AFE_GENERAL2_ASRC_2CH_CON5:
	case AFE_GENERAL2_ASRC_2CH_CON7 ... AFE_GENERAL2_ASRC_2CH_CON8:
	case AFE_GENERAL2_ASRC_2CH_CON12 ... AFE_GENERAL2_ASRC_2CH_CON13:
		return true;
	default:
		return false;
	};
}

static const struct regmap_config mt8183_afe_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,

	.volatile_reg = mt8183_is_volatile_reg,

	.max_register = AFE_MAX_REGISTER,
	.num_reg_defaults_raw = AFE_MAX_REGISTER,

	.cache_type = REGCACHE_FLAT,
};

static irqreturn_t mt8183_afe_irq_handler(int irq_id, void *dev)
{
	struct mtk_base_afe *afe = dev;
	struct mtk_base_afe_irq *irq;
	unsigned int status;
	unsigned int status_mcu;
	unsigned int mcu_en;
	int ret;
	int i;
	irqreturn_t irq_ret = IRQ_HANDLED;

	/* get irq that is sent to MCU */
	regmap_read(afe->regmap, AFE_IRQ_MCU_EN, &mcu_en);

	ret = regmap_read(afe->regmap, AFE_IRQ_MCU_STATUS, &status);
	/* only care IRQ which is sent to MCU */
	status_mcu = status & mcu_en & AFE_IRQ_STATUS_BITS;

	if (ret || status_mcu == 0) {
		dev_err(afe->dev, "%s(), irq status err, ret %d, status 0x%x, mcu_en 0x%x\n",
			__func__, ret, status, mcu_en);

		irq_ret = IRQ_NONE;
		goto err_irq;
	}

	for (i = 0; i < MT8183_MEMIF_NUM; i++) {
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

	return irq_ret;
}

static int mt8183_afe_runtime_suspend(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	struct mt8183_afe_private *afe_priv = afe->platform_priv;
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

	/* make sure all irq status are cleared, twice intended */
	regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CLR, 0xffff, 0xffff);
	regmap_update_bits(afe->regmap, AFE_IRQ_MCU_CLR, 0xffff, 0xffff);

	/* cache only */
	regcache_cache_only(afe->regmap, true);
	regcache_mark_dirty(afe->regmap);

skip_regmap:
	return mt8183_afe_disable_clock(afe);
}

static int mt8183_afe_runtime_resume(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	struct mt8183_afe_private *afe_priv = afe->platform_priv;
	int ret;

	ret = mt8183_afe_enable_clock(afe);
	if (ret)
		return ret;

	if (!afe->regmap || afe_priv->pm_runtime_bypass_reg_ctl)
		goto skip_regmap;

	regcache_cache_only(afe->regmap, false);
	regcache_sync(afe->regmap);

	/* enable audio sys DCM for power saving */
	regmap_update_bits(afe->regmap, AUDIO_TOP_CON0, 0x1 << 29, 0x1 << 29);

	/* force cpu use 8_24 format when writing 32bit data */
	regmap_update_bits(afe->regmap, AFE_MEMIF_MSB,
			   CPU_HD_ALIGN_MASK_SFT, 0 << CPU_HD_ALIGN_SFT);

	/* set all output port to 24bit */
	regmap_write(afe->regmap, AFE_CONN_24BIT, 0xffffffff);
	regmap_write(afe->regmap, AFE_CONN_24BIT_1, 0xffffffff);

	/* enable AFE */
	regmap_update_bits(afe->regmap, AFE_DAC_CON0, 0x1, 0x1);

skip_regmap:
	return 0;
}

static int mt8183_dai_memif_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mt8183_memif_dai_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mt8183_memif_dai_driver);

	dai->dapm_widgets = mt8183_memif_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mt8183_memif_widgets);
	dai->dapm_routes = mt8183_memif_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mt8183_memif_routes);
	return 0;
}

typedef int (*dai_register_cb)(struct mtk_base_afe *);
static const dai_register_cb dai_register_cbs[] = {
	mt8183_dai_adda_register,
	mt8183_dai_i2s_register,
	mt8183_dai_pcm_register,
	mt8183_dai_tdm_register,
	mt8183_dai_hostless_register,
	mt8183_dai_memif_register,
};

static int mt8183_afe_pcm_dev_probe(struct platform_device *pdev)
{
	struct mtk_base_afe *afe;
	struct mt8183_afe_private *afe_priv;
	struct device *dev = &pdev->dev;
	struct reset_control *rstc;
	int i, irq_id, ret;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(34));
	if (ret)
		return ret;

	afe = devm_kzalloc(dev, sizeof(*afe), GFP_KERNEL);
	if (!afe)
		return -ENOMEM;
	platform_set_drvdata(pdev, afe);

	afe->platform_priv = devm_kzalloc(dev, sizeof(*afe_priv), GFP_KERNEL);
	if (!afe->platform_priv)
		return -ENOMEM;

	afe_priv = afe->platform_priv;
	afe->dev = dev;

	ret = of_reserved_mem_device_init(dev);
	if (ret) {
		dev_info(dev, "no reserved memory found, pre-allocating buffers instead\n");
		afe->preallocate_buffers = true;
	}

	/* initial audio related clock */
	ret = mt8183_init_clock(afe);
	if (ret) {
		dev_err(dev, "init clock error\n");
		return ret;
	}

	pm_runtime_enable(dev);

	/* regmap init */
	afe->regmap = syscon_node_to_regmap(dev->parent->of_node);
	if (IS_ERR(afe->regmap)) {
		dev_err(dev, "could not get regmap from parent\n");
		ret = PTR_ERR(afe->regmap);
		goto err_pm_disable;
	}
	ret = regmap_attach_dev(dev, afe->regmap, &mt8183_afe_regmap_config);
	if (ret) {
		dev_warn(dev, "regmap_attach_dev fail, ret %d\n", ret);
		goto err_pm_disable;
	}

	rstc = devm_reset_control_get(dev, "audiosys");
	if (IS_ERR(rstc)) {
		ret = PTR_ERR(rstc);
		dev_err(dev, "could not get audiosys reset:%d\n", ret);
		goto err_pm_disable;
	}

	ret = reset_control_reset(rstc);
	if (ret) {
		dev_err(dev, "failed to trigger audio reset:%d\n", ret);
		goto err_pm_disable;
	}

	/* enable clock for regcache get default value from hw */
	afe_priv->pm_runtime_bypass_reg_ctl = true;
	pm_runtime_get_sync(dev);

	ret = regmap_reinit_cache(afe->regmap, &mt8183_afe_regmap_config);
	if (ret) {
		dev_err(dev, "regmap_reinit_cache fail, ret %d\n", ret);
		goto err_pm_disable;
	}

	pm_runtime_put_sync(dev);
	afe_priv->pm_runtime_bypass_reg_ctl = false;

	regcache_cache_only(afe->regmap, true);
	regcache_mark_dirty(afe->regmap);

	/* init memif */
	afe->memif_size = MT8183_MEMIF_NUM;
	afe->memif = devm_kcalloc(dev, afe->memif_size, sizeof(*afe->memif),
				  GFP_KERNEL);
	if (!afe->memif) {
		ret = -ENOMEM;
		goto err_pm_disable;
	}

	for (i = 0; i < afe->memif_size; i++) {
		afe->memif[i].data = &memif_data[i];
		afe->memif[i].irq_usage = -1;
	}

	afe->memif[MT8183_MEMIF_HDMI].irq_usage = MT8183_IRQ_8;
	afe->memif[MT8183_MEMIF_HDMI].const_irq = 1;

	mutex_init(&afe->irq_alloc_lock);

	/* init memif */
	/* irq initialize */
	afe->irqs_size = MT8183_IRQ_NUM;
	afe->irqs = devm_kcalloc(dev, afe->irqs_size, sizeof(*afe->irqs),
				 GFP_KERNEL);
	if (!afe->irqs) {
		ret = -ENOMEM;
		goto err_pm_disable;
	}

	for (i = 0; i < afe->irqs_size; i++)
		afe->irqs[i].irq_data = &irq_data[i];

	/* request irq */
	irq_id = platform_get_irq(pdev, 0);
	if (irq_id < 0) {
		ret = irq_id;
		goto err_pm_disable;
	}

	ret = devm_request_irq(dev, irq_id, mt8183_afe_irq_handler,
			       IRQF_TRIGGER_NONE, "asys-isr", (void *)afe);
	if (ret) {
		dev_err(dev, "could not request_irq for asys-isr\n");
		goto err_pm_disable;
	}

	/* init sub_dais */
	INIT_LIST_HEAD(&afe->sub_dais);

	for (i = 0; i < ARRAY_SIZE(dai_register_cbs); i++) {
		ret = dai_register_cbs[i](afe);
		if (ret) {
			dev_warn(dev, "dai register i %d fail, ret %d\n",
				 i, ret);
			goto err_pm_disable;
		}
	}

	/* init dai_driver and component_driver */
	ret = mtk_afe_combine_sub_dai(afe);
	if (ret) {
		dev_warn(dev, "mtk_afe_combine_sub_dai fail, ret %d\n", ret);
		goto err_pm_disable;
	}

	afe->mtk_afe_hardware = &mt8183_afe_hardware;
	afe->memif_fs = mt8183_memif_fs;
	afe->irq_fs = mt8183_irq_fs;

	afe->runtime_resume = mt8183_afe_runtime_resume;
	afe->runtime_suspend = mt8183_afe_runtime_suspend;

	/* register component */
	ret = devm_snd_soc_register_component(dev, &mtk_afe_pcm_platform,
					      NULL, 0);
	if (ret) {
		dev_warn(dev, "err_platform\n");
		goto err_pm_disable;
	}

	ret = devm_snd_soc_register_component(dev, &mt8183_afe_pcm_dai_component,
					      afe->dai_drivers,
					      afe->num_dai_drivers);
	if (ret) {
		dev_warn(dev, "err_dai_component\n");
		goto err_pm_disable;
	}

	return ret;

err_pm_disable:
	pm_runtime_disable(dev);
	return ret;
}

static void mt8183_afe_pcm_dev_remove(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;

	pm_runtime_disable(dev);
	if (!pm_runtime_status_suspended(dev))
		mt8183_afe_runtime_suspend(dev);
}

static const struct of_device_id mt8183_afe_pcm_dt_match[] = {
	{ .compatible = "mediatek,mt8183-audio", },
	{},
};
MODULE_DEVICE_TABLE(of, mt8183_afe_pcm_dt_match);

static const struct dev_pm_ops mt8183_afe_pm_ops = {
	RUNTIME_PM_OPS(mt8183_afe_runtime_suspend,
		       mt8183_afe_runtime_resume, NULL)
};

static struct platform_driver mt8183_afe_pcm_driver = {
	.driver = {
		   .name = "mt8183-audio",
		   .of_match_table = mt8183_afe_pcm_dt_match,
		   .pm = pm_ptr(&mt8183_afe_pm_ops),
	},
	.probe = mt8183_afe_pcm_dev_probe,
	.remove = mt8183_afe_pcm_dev_remove,
};

module_platform_driver(mt8183_afe_pcm_driver);

MODULE_DESCRIPTION("Mediatek ALSA SoC AFE platform driver for 8183");
MODULE_AUTHOR("KaiChieh Chuang <kaichieh.chuang@mediatek.com>");
MODULE_LICENSE("GPL v2");
