// SPDX-License-Identifier: GPL-2.0
/*
 * MediaTek 8365 ALSA SoC AFE platform driver
 *
 * Copyright (c) 2024 MediaTek Inc.
 * Authors: Jia Zeng <jia.zeng@mediatek.com>
 *          Alexandre Mergnat <amergnat@baylibre.com>
 */

#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/dma-mapping.h>
#include <linux/pm_runtime.h>
#include <sound/soc.h>
#include <sound/pcm_params.h>
#include "mt8365-afe-common.h"
#include "mt8365-afe-clk.h"
#include "mt8365-reg.h"
#include "../common/mtk-base-afe.h"
#include "../common/mtk-afe-platform-driver.h"
#include "../common/mtk-afe-fe-dai.h"

#define AFE_BASE_END_OFFSET 8

static unsigned int mCM2Input;

static const unsigned int mt8365_afe_backup_list[] = {
	AUDIO_TOP_CON0,
	AFE_CONN0,
	AFE_CONN1,
	AFE_CONN3,
	AFE_CONN4,
	AFE_CONN5,
	AFE_CONN6,
	AFE_CONN7,
	AFE_CONN8,
	AFE_CONN9,
	AFE_CONN10,
	AFE_CONN11,
	AFE_CONN12,
	AFE_CONN13,
	AFE_CONN14,
	AFE_CONN15,
	AFE_CONN16,
	AFE_CONN17,
	AFE_CONN18,
	AFE_CONN19,
	AFE_CONN20,
	AFE_CONN21,
	AFE_CONN26,
	AFE_CONN27,
	AFE_CONN28,
	AFE_CONN29,
	AFE_CONN30,
	AFE_CONN31,
	AFE_CONN32,
	AFE_CONN33,
	AFE_CONN34,
	AFE_CONN35,
	AFE_CONN36,
	AFE_CONN_24BIT,
	AFE_CONN_24BIT_1,
	AFE_DAC_CON0,
	AFE_DAC_CON1,
	AFE_DL1_BASE,
	AFE_DL1_END,
	AFE_DL2_BASE,
	AFE_DL2_END,
	AFE_VUL_BASE,
	AFE_VUL_END,
	AFE_AWB_BASE,
	AFE_AWB_END,
	AFE_VUL3_BASE,
	AFE_VUL3_END,
	AFE_HDMI_OUT_BASE,
	AFE_HDMI_OUT_END,
	AFE_HDMI_IN_2CH_BASE,
	AFE_HDMI_IN_2CH_END,
	AFE_ADDA_UL_DL_CON0,
	AFE_ADDA_DL_SRC2_CON0,
	AFE_ADDA_DL_SRC2_CON1,
	AFE_I2S_CON,
	AFE_I2S_CON1,
	AFE_I2S_CON2,
	AFE_I2S_CON3,
	AFE_ADDA_UL_SRC_CON0,
	AFE_AUD_PAD_TOP,
	AFE_HD_ENGEN_ENABLE,
};

static const struct snd_pcm_hardware mt8365_afe_hardware = {
	.info = (SNDRV_PCM_INFO_MMAP |
		 SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_MMAP_VALID),
	.buffer_bytes_max = 256 * 1024,
	.period_bytes_min = 512,
	.period_bytes_max = 128 * 1024,
	.periods_min = 2,
	.periods_max = 256,
	.fifo_size = 0,
};

struct mt8365_afe_rate {
	unsigned int rate;
	unsigned int reg_val;
};

static const struct mt8365_afe_rate mt8365_afe_fs_rates[] = {
	{ .rate = 8000, .reg_val = MT8365_FS_8K },
	{ .rate = 11025, .reg_val = MT8365_FS_11D025K },
	{ .rate = 12000, .reg_val = MT8365_FS_12K },
	{ .rate = 16000, .reg_val = MT8365_FS_16K },
	{ .rate = 22050, .reg_val = MT8365_FS_22D05K },
	{ .rate = 24000, .reg_val = MT8365_FS_24K },
	{ .rate = 32000, .reg_val = MT8365_FS_32K },
	{ .rate = 44100, .reg_val = MT8365_FS_44D1K },
	{ .rate = 48000, .reg_val = MT8365_FS_48K },
	{ .rate = 88200, .reg_val = MT8365_FS_88D2K },
	{ .rate = 96000, .reg_val = MT8365_FS_96K },
	{ .rate = 176400, .reg_val = MT8365_FS_176D4K },
	{ .rate = 192000, .reg_val = MT8365_FS_192K },
};

int mt8365_afe_fs_timing(unsigned int rate)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mt8365_afe_fs_rates); i++)
		if (mt8365_afe_fs_rates[i].rate == rate)
			return mt8365_afe_fs_rates[i].reg_val;

	return -EINVAL;
}

bool mt8365_afe_rate_supported(unsigned int rate, unsigned int id)
{
	switch (id) {
	case MT8365_AFE_IO_TDM_IN:
		if (rate >= 8000 && rate <= 192000)
			return true;
		break;
	case MT8365_AFE_IO_DMIC:
		if (rate >= 8000 && rate <= 48000)
			return true;
		break;
	default:
		break;
	}

	return false;
}

bool mt8365_afe_channel_supported(unsigned int channel, unsigned int id)
{
	switch (id) {
	case MT8365_AFE_IO_TDM_IN:
		if (channel >= 1 && channel <= 8)
			return true;
		break;
	case MT8365_AFE_IO_DMIC:
		if (channel >= 1 && channel <= 8)
			return true;
		break;
	default:
		break;
	}

	return false;
}

static bool mt8365_afe_clk_group_44k(int sample_rate)
{
	if (sample_rate == 11025 ||
	    sample_rate == 22050 ||
	    sample_rate == 44100 ||
	    sample_rate == 88200 ||
	    sample_rate == 176400)
		return true;
	else
		return false;
}

static bool mt8365_afe_clk_group_48k(int sample_rate)
{
	return (!mt8365_afe_clk_group_44k(sample_rate));
}

int mt8365_dai_set_priv(struct mtk_base_afe *afe, int id,
			int priv_size, const void *priv_data)
{
	struct mt8365_afe_private *afe_priv = afe->platform_priv;
	void *temp_data;

	temp_data = devm_kzalloc(afe->dev, priv_size, GFP_KERNEL);
	if (!temp_data)
		return -ENOMEM;

	if (priv_data)
		memcpy(temp_data, priv_data, priv_size);

	afe_priv->dai_priv[id] = temp_data;

	return 0;
}

static int mt8365_afe_irq_direction_enable(struct mtk_base_afe *afe,
					   int irq_id, int direction)
{
	struct mtk_base_afe_irq *irq;

	if (irq_id >= MT8365_AFE_IRQ_NUM)
		return -1;

	irq = &afe->irqs[irq_id];

	if (direction == MT8365_AFE_IRQ_DIR_MCU) {
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_DSP_EN,
				   (1 << irq->irq_data->irq_clr_shift),
				   0);
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_EN,
				   (1 << irq->irq_data->irq_clr_shift),
				   (1 << irq->irq_data->irq_clr_shift));
	} else if (direction == MT8365_AFE_IRQ_DIR_DSP) {
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_DSP_EN,
				   (1 << irq->irq_data->irq_clr_shift),
				   (1 << irq->irq_data->irq_clr_shift));
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_EN,
				   (1 << irq->irq_data->irq_clr_shift),
				   0);
	} else {
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_DSP_EN,
				   (1 << irq->irq_data->irq_clr_shift),
				   (1 << irq->irq_data->irq_clr_shift));
		regmap_update_bits(afe->regmap, AFE_IRQ_MCU_EN,
				   (1 << irq->irq_data->irq_clr_shift),
				   (1 << irq->irq_data->irq_clr_shift));
	}
	return 0;
}

static int mt8365_memif_fs(struct snd_pcm_substream *substream,
			   unsigned int rate)
{
	return mt8365_afe_fs_timing(rate);
}

static int mt8365_irq_fs(struct snd_pcm_substream *substream,
			 unsigned int rate)
{
	return mt8365_memif_fs(substream, rate);
}

static const struct mt8365_cm_ctrl_reg cm_ctrl_reg[MT8365_CM_NUM] = {
	[MT8365_CM1] = {
		.con0 = AFE_CM1_CON0,
		.con1 = AFE_CM1_CON1,
		.con2 = AFE_CM1_CON2,
		.con3 = AFE_CM1_CON3,
		.con4 = AFE_CM1_CON4,
	},
	[MT8365_CM2] = {
		.con0 = AFE_CM2_CON0,
		.con1 = AFE_CM2_CON1,
		.con2 = AFE_CM2_CON2,
		.con3 = AFE_CM2_CON3,
		.con4 = AFE_CM2_CON4,
	}
};

static int mt8365_afe_cm2_mux_conn(struct mtk_base_afe *afe)
{
	struct mt8365_afe_private *afe_priv = afe->platform_priv;
	unsigned int input = afe_priv->cm2_mux_input;

	/* TDM_IN interconnect to CM2 */
	regmap_update_bits(afe->regmap, AFE_CM2_CONN0,
			   CM2_AFE_CM2_CONN_CFG1_MASK,
			   CM2_AFE_CM2_CONN_CFG1(TDM_IN_CH0));
	regmap_update_bits(afe->regmap, AFE_CM2_CONN0,
			   CM2_AFE_CM2_CONN_CFG2_MASK,
			   CM2_AFE_CM2_CONN_CFG2(TDM_IN_CH1));
	regmap_update_bits(afe->regmap, AFE_CM2_CONN0,
			   CM2_AFE_CM2_CONN_CFG3_MASK,
			   CM2_AFE_CM2_CONN_CFG3(TDM_IN_CH2));
	regmap_update_bits(afe->regmap, AFE_CM2_CONN0,
			   CM2_AFE_CM2_CONN_CFG4_MASK,
			   CM2_AFE_CM2_CONN_CFG4(TDM_IN_CH3));
	regmap_update_bits(afe->regmap, AFE_CM2_CONN0,
			   CM2_AFE_CM2_CONN_CFG5_MASK,
			   CM2_AFE_CM2_CONN_CFG5(TDM_IN_CH4));
	regmap_update_bits(afe->regmap, AFE_CM2_CONN0,
			   CM2_AFE_CM2_CONN_CFG6_MASK,
			   CM2_AFE_CM2_CONN_CFG6(TDM_IN_CH5));
	regmap_update_bits(afe->regmap, AFE_CM2_CONN1,
			   CM2_AFE_CM2_CONN_CFG7_MASK,
			   CM2_AFE_CM2_CONN_CFG7(TDM_IN_CH6));
	regmap_update_bits(afe->regmap, AFE_CM2_CONN1,
			   CM2_AFE_CM2_CONN_CFG8_MASK,
			   CM2_AFE_CM2_CONN_CFG8(TDM_IN_CH7));

	/* ref data interconnect to CM2 */
	if (input == MT8365_FROM_GASRC1) {
		regmap_update_bits(afe->regmap, AFE_CM2_CONN1,
				   CM2_AFE_CM2_CONN_CFG9_MASK,
				   CM2_AFE_CM2_CONN_CFG9(GENERAL1_ASRC_OUT_LCH));
		regmap_update_bits(afe->regmap, AFE_CM2_CONN1,
				   CM2_AFE_CM2_CONN_CFG10_MASK,
				   CM2_AFE_CM2_CONN_CFG10(GENERAL1_ASRC_OUT_RCH));
	} else if (input == MT8365_FROM_GASRC2) {
		regmap_update_bits(afe->regmap, AFE_CM2_CONN1,
				   CM2_AFE_CM2_CONN_CFG9_MASK,
				   CM2_AFE_CM2_CONN_CFG9(GENERAL2_ASRC_OUT_LCH));
		regmap_update_bits(afe->regmap, AFE_CM2_CONN1,
				   CM2_AFE_CM2_CONN_CFG10_MASK,
				   CM2_AFE_CM2_CONN_CFG10(GENERAL2_ASRC_OUT_RCH));
	} else if (input == MT8365_FROM_TDM_ASRC) {
		regmap_update_bits(afe->regmap, AFE_CM2_CONN1,
				   CM2_AFE_CM2_CONN_CFG9_MASK,
				   CM2_AFE_CM2_CONN_CFG9(TDM_OUT_ASRC_CH0));
		regmap_update_bits(afe->regmap, AFE_CM2_CONN1,
				   CM2_AFE_CM2_CONN_CFG10_MASK,
				   CM2_AFE_CM2_CONN_CFG10(TDM_OUT_ASRC_CH1));
		regmap_update_bits(afe->regmap, AFE_CM2_CONN1,
				   CM2_AFE_CM2_CONN_CFG11_MASK,
				   CM2_AFE_CM2_CONN_CFG11(TDM_OUT_ASRC_CH2));
		regmap_update_bits(afe->regmap, AFE_CM2_CONN1,
				   CM2_AFE_CM2_CONN_CFG12_MASK,
				   CM2_AFE_CM2_CONN_CFG12(TDM_OUT_ASRC_CH3));
		regmap_update_bits(afe->regmap, AFE_CM2_CONN2,
				   CM2_AFE_CM2_CONN_CFG13_MASK,
				   CM2_AFE_CM2_CONN_CFG13(TDM_OUT_ASRC_CH4));
		regmap_update_bits(afe->regmap, AFE_CM2_CONN2,
				   CM2_AFE_CM2_CONN_CFG14_MASK,
				   CM2_AFE_CM2_CONN_CFG14(TDM_OUT_ASRC_CH5));
		regmap_update_bits(afe->regmap, AFE_CM2_CONN2,
				   CM2_AFE_CM2_CONN_CFG15_MASK,
				   CM2_AFE_CM2_CONN_CFG15(TDM_OUT_ASRC_CH6));
		regmap_update_bits(afe->regmap, AFE_CM2_CONN2,
				   CM2_AFE_CM2_CONN_CFG16_MASK,
				   CM2_AFE_CM2_CONN_CFG16(TDM_OUT_ASRC_CH7));
	} else {
		dev_err(afe->dev, "%s wrong CM2 input %d\n", __func__, input);
		return -1;
	}

	return 0;
}

static int mt8365_afe_get_cm_update_cnt(struct mtk_base_afe *afe,
					enum mt8365_cm_num cmNum,
					unsigned int rate, unsigned int channel)
{
	unsigned int total_cnt, div_cnt, ch_pair, best_cnt;
	unsigned int ch_update_cnt[MT8365_CM_UPDATA_CNT_SET];
	int i;

	/* calculate cm update cnt
	 * total_cnt = clk / fs, clk is 26m or 24m or 22m
	 * div_cnt = total_cnt / ch_pair, max ch 16ch ,2ch is a set
	 * best_cnt < div_cnt ,we set best_cnt = div_cnt -10
	 * ch01 = best_cnt, ch23 = 2* ch01_up_cnt
	 * ch45 = 3* ch01_up_cnt ...ch1415 = 8* ch01_up_cnt
	 */

	if (cmNum == MT8365_CM1) {
		total_cnt = MT8365_CLK_26M / rate;
	} else if (cmNum == MT8365_CM2) {
		if (mt8365_afe_clk_group_48k(rate))
			total_cnt = MT8365_CLK_24M / rate;
		else
			total_cnt = MT8365_CLK_22M / rate;
	} else {
		return -1;
	}

	if (channel % 2)
		ch_pair = (channel / 2) + 1;
	else
		ch_pair = channel / 2;

	div_cnt =  total_cnt / ch_pair;
	best_cnt = div_cnt - 10;

	if (best_cnt <= 0)
		return -1;

	for (i = 0; i < ch_pair; i++)
		ch_update_cnt[i] = (i + 1) * best_cnt;

	switch (channel) {
	case 16:
		fallthrough;
	case 15:
		regmap_update_bits(afe->regmap, cm_ctrl_reg[cmNum].con4,
				   CM_AFE_CM_UPDATE_CNT2_MASK,
				   CM_AFE_CM_UPDATE_CNT2(ch_update_cnt[7]));
		fallthrough;
	case 14:
		fallthrough;
	case 13:
		regmap_update_bits(afe->regmap, cm_ctrl_reg[cmNum].con4,
				   CM_AFE_CM_UPDATE_CNT1_MASK,
				   CM_AFE_CM_UPDATE_CNT1(ch_update_cnt[6]));
		fallthrough;
	case 12:
		fallthrough;
	case 11:
		regmap_update_bits(afe->regmap, cm_ctrl_reg[cmNum].con3,
				   CM_AFE_CM_UPDATE_CNT2_MASK,
				   CM_AFE_CM_UPDATE_CNT2(ch_update_cnt[5]));
		fallthrough;
	case 10:
		fallthrough;
	case 9:
		regmap_update_bits(afe->regmap, cm_ctrl_reg[cmNum].con3,
				   CM_AFE_CM_UPDATE_CNT1_MASK,
				   CM_AFE_CM_UPDATE_CNT1(ch_update_cnt[4]));
		fallthrough;
	case 8:
		fallthrough;
	case 7:
		regmap_update_bits(afe->regmap, cm_ctrl_reg[cmNum].con2,
				   CM_AFE_CM_UPDATE_CNT2_MASK,
				   CM_AFE_CM_UPDATE_CNT2(ch_update_cnt[3]));
		fallthrough;
	case 6:
		fallthrough;
	case 5:
		regmap_update_bits(afe->regmap, cm_ctrl_reg[cmNum].con2,
				   CM_AFE_CM_UPDATE_CNT1_MASK,
				   CM_AFE_CM_UPDATE_CNT1(ch_update_cnt[2]));
		fallthrough;
	case 4:
		fallthrough;
	case 3:
		regmap_update_bits(afe->regmap, cm_ctrl_reg[cmNum].con1,
				   CM_AFE_CM_UPDATE_CNT2_MASK,
				   CM_AFE_CM_UPDATE_CNT2(ch_update_cnt[1]));
		fallthrough;
	case 2:
		fallthrough;
	case 1:
		regmap_update_bits(afe->regmap, cm_ctrl_reg[cmNum].con1,
				   CM_AFE_CM_UPDATE_CNT1_MASK,
				   CM_AFE_CM_UPDATE_CNT1(ch_update_cnt[0]));
		break;
	default:
		return -1;
	}

	return 0;
}

static int mt8365_afe_configure_cm(struct mtk_base_afe *afe,
				   enum mt8365_cm_num cmNum,
				   unsigned int channels,
				   unsigned int rate)
{
	unsigned int val, mask;
	unsigned int fs = mt8365_afe_fs_timing(rate);

	val = FIELD_PREP(CM_AFE_CM_CH_NUM_MASK, (channels - 1)) |
	      FIELD_PREP(CM_AFE_CM_START_DATA_MASK, 0);

	mask = CM_AFE_CM_CH_NUM_MASK |
	       CM_AFE_CM_START_DATA_MASK;

	if (cmNum == MT8365_CM1) {
		val |= FIELD_PREP(CM_AFE_CM1_IN_MODE_MASK, fs);

		mask |= CM_AFE_CM1_VUL_SEL |
			CM_AFE_CM1_IN_MODE_MASK;
	} else if (cmNum == MT8365_CM2) {
		if (mt8365_afe_clk_group_48k(rate))
			val |= FIELD_PREP(CM_AFE_CM2_CLK_SEL, 0);
		else
			val |= FIELD_PREP(CM_AFE_CM2_CLK_SEL, 1);

		val |= FIELD_PREP(CM_AFE_CM2_TDM_SEL, 1);

		mask |= CM_AFE_CM2_TDM_SEL |
			CM_AFE_CM1_IN_MODE_MASK |
			CM_AFE_CM2_CLK_SEL;

		mt8365_afe_cm2_mux_conn(afe);
	} else {
		return -1;
	}

	regmap_update_bits(afe->regmap, cm_ctrl_reg[cmNum].con0, mask, val);

	mt8365_afe_get_cm_update_cnt(afe, cmNum, rate, channels);

	return 0;
}

static int mt8365_afe_fe_startup(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int memif_num = snd_soc_rtd_to_cpu(rtd, 0)->id;
	struct mtk_base_afe_memif *memif = &afe->memif[memif_num];
	int ret;

	memif->substream = substream;

	snd_pcm_hw_constraint_step(substream->runtime, 0,
				   SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 16);

	snd_soc_set_runtime_hwparams(substream, afe->mtk_afe_hardware);

	ret = snd_pcm_hw_constraint_integer(runtime, SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		dev_err(afe->dev, "snd_pcm_hw_constraint_integer failed\n");

	mt8365_afe_enable_main_clk(afe);
	return ret;
}

static void mt8365_afe_fe_shutdown(struct snd_pcm_substream *substream,
				   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int memif_num = snd_soc_rtd_to_cpu(rtd, 0)->id;
	struct mtk_base_afe_memif *memif = &afe->memif[memif_num];

	memif->substream = NULL;

	mt8365_afe_disable_main_clk(afe);
}

static int mt8365_afe_fe_hw_params(struct snd_pcm_substream *substream,
				   struct snd_pcm_hw_params *params,
				   struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8365_afe_private *afe_priv = afe->platform_priv;
	struct mt8365_control_data *ctrl_data = &afe_priv->ctrl_data;
	int dai_id = snd_soc_rtd_to_cpu(rtd, 0)->id;
	struct mtk_base_afe_memif *memif = &afe->memif[dai_id];
	struct mt8365_fe_dai_data *fe_data = &afe_priv->fe_data[dai_id];
	size_t request_size = params_buffer_bytes(params);
	unsigned int channels = params_channels(params);
	unsigned int rate = params_rate(params);
	unsigned int base_end_offset = 8;
	int ret, fs;

	dev_info(afe->dev, "%s %s period = %d rate = %d channels = %d\n",
		 __func__, memif->data->name, params_period_size(params),
		 rate, channels);

	if (dai_id == MT8365_AFE_MEMIF_VUL2) {
		if (!ctrl_data->bypass_cm1)
			/* configure cm1 */
			mt8365_afe_configure_cm(afe, MT8365_CM1,
						channels, rate);
		else
			regmap_update_bits(afe->regmap, AFE_CM1_CON0,
					   CM_AFE_CM1_VUL_SEL,
					   CM_AFE_CM1_VUL_SEL);
	} else if (dai_id == MT8365_AFE_MEMIF_TDM_IN) {
		if (!ctrl_data->bypass_cm2)
			/* configure cm2 */
			mt8365_afe_configure_cm(afe, MT8365_CM2,
						channels, rate);
		else
			regmap_update_bits(afe->regmap, AFE_CM2_CON0,
					   CM_AFE_CM2_TDM_SEL,
					   ~CM_AFE_CM2_TDM_SEL);

		base_end_offset = 4;
	}

	if (request_size > fe_data->sram_size) {
		ret = snd_pcm_lib_malloc_pages(substream, request_size);
		if (ret < 0) {
			dev_err(afe->dev,
				"%s %s malloc pages %zu bytes failed %d\n",
				__func__, memif->data->name, request_size, ret);
			return ret;
		}

		fe_data->use_sram = false;

		mt8365_afe_emi_clk_on(afe);
	} else {
		struct snd_dma_buffer *dma_buf = &substream->dma_buffer;

		dma_buf->dev.type = SNDRV_DMA_TYPE_DEV;
		dma_buf->dev.dev = substream->pcm->card->dev;
		dma_buf->area = (unsigned char *)fe_data->sram_vir_addr;
		dma_buf->addr = fe_data->sram_phy_addr;
		dma_buf->bytes = request_size;
		snd_pcm_set_runtime_buffer(substream, dma_buf);

		fe_data->use_sram = true;
	}

	memif->phys_buf_addr = lower_32_bits(substream->runtime->dma_addr);
	memif->buffer_size = substream->runtime->dma_bytes;

	/* start */
	regmap_write(afe->regmap, memif->data->reg_ofs_base,
		     memif->phys_buf_addr);
	/* end */
	regmap_write(afe->regmap,
		     memif->data->reg_ofs_base + base_end_offset,
		     memif->phys_buf_addr + memif->buffer_size - 1);

	/* set channel */
	if (memif->data->mono_shift >= 0) {
		unsigned int mono = (params_channels(params) == 1) ? 1 : 0;

		if (memif->data->mono_reg < 0)
			dev_info(afe->dev, "%s mono_reg is NULL\n", __func__);
		else
			regmap_update_bits(afe->regmap, memif->data->mono_reg,
					   1 << memif->data->mono_shift,
					   mono << memif->data->mono_shift);
	}

	/* set rate */
	if (memif->data->fs_shift < 0)
		return 0;

	fs = afe->memif_fs(substream, params_rate(params));

	if (fs < 0)
		return -EINVAL;

	if (memif->data->fs_reg < 0)
		dev_info(afe->dev, "%s fs_reg is NULL\n", __func__);
	else
		regmap_update_bits(afe->regmap, memif->data->fs_reg,
				   memif->data->fs_maskbit << memif->data->fs_shift,
				   fs << memif->data->fs_shift);

	return 0;
}

static int mt8365_afe_fe_hw_free(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8365_afe_private *afe_priv = afe->platform_priv;
	int dai_id = snd_soc_rtd_to_cpu(rtd, 0)->id;
	struct mt8365_fe_dai_data *fe_data = &afe_priv->fe_data[dai_id];
	int ret = 0;

	if (fe_data->use_sram) {
		snd_pcm_set_runtime_buffer(substream, NULL);
	} else {
		ret = snd_pcm_lib_free_pages(substream);

		mt8365_afe_emi_clk_off(afe);
	}

	return ret;
}

static int mt8365_afe_fe_prepare(struct snd_pcm_substream *substream,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	int dai_id = snd_soc_rtd_to_cpu(rtd, 0)->id;
	struct mtk_base_afe_memif *memif = &afe->memif[dai_id];

	/* set format */
	if (memif->data->hd_reg >= 0) {
		switch (substream->runtime->format) {
		case SNDRV_PCM_FORMAT_S16_LE:
			regmap_update_bits(afe->regmap, memif->data->hd_reg,
					   3 << memif->data->hd_shift,
					   0 << memif->data->hd_shift);
			break;
		case SNDRV_PCM_FORMAT_S32_LE:
			regmap_update_bits(afe->regmap, memif->data->hd_reg,
					   3 << memif->data->hd_shift,
					   3 << memif->data->hd_shift);

			if (dai_id == MT8365_AFE_MEMIF_TDM_IN) {
				regmap_update_bits(afe->regmap,
						   memif->data->hd_reg,
						   3 << memif->data->hd_shift,
						   1 << memif->data->hd_shift);
				regmap_update_bits(afe->regmap,
						   memif->data->hd_reg,
						   1 << memif->data->hd_align_mshift,
						   1 << memif->data->hd_align_mshift);
			}
			break;
		case SNDRV_PCM_FORMAT_S24_LE:
			regmap_update_bits(afe->regmap, memif->data->hd_reg,
					   3 << memif->data->hd_shift,
					   1 << memif->data->hd_shift);
			break;
		default:
			return -EINVAL;
		}
	}

	mt8365_afe_irq_direction_enable(afe, memif->irq_usage,
					MT8365_AFE_IRQ_DIR_MCU);

	return 0;
}

static int mt8365_afe_fe_trigger(struct snd_pcm_substream *substream, int cmd,
				 struct snd_soc_dai *dai)
{
	struct snd_soc_pcm_runtime *rtd = snd_soc_substream_to_rtd(substream);
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8365_afe_private *afe_priv = afe->platform_priv;
	int dai_id = snd_soc_rtd_to_cpu(rtd, 0)->id;
	struct mt8365_control_data *ctrl_data = &afe_priv->ctrl_data;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		/* enable channel merge */
		if (dai_id == MT8365_AFE_MEMIF_VUL2 &&
		    !ctrl_data->bypass_cm1) {
			regmap_update_bits(afe->regmap, AFE_CM1_CON0,
					   CM_AFE_CM_ON, CM_AFE_CM_ON);
		} else if (dai_id == MT8365_AFE_MEMIF_TDM_IN &&
			   !ctrl_data->bypass_cm2) {
			regmap_update_bits(afe->regmap, AFE_CM2_CON0,
					   CM_AFE_CM_ON, CM_AFE_CM_ON);
		}
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		/* disable channel merge */
		if (dai_id == MT8365_AFE_MEMIF_VUL2 &&
		    !ctrl_data->bypass_cm1) {
			regmap_update_bits(afe->regmap, AFE_CM1_CON0,
					   CM_AFE_CM_ON, ~CM_AFE_CM_ON);
		} else if (dai_id == MT8365_AFE_MEMIF_TDM_IN &&
			   !ctrl_data->bypass_cm2) {
			regmap_update_bits(afe->regmap, AFE_CM2_CON0,
					   CM_AFE_CM_ON, ~CM_AFE_CM_ON);
		}
		break;
	default:
		break;
	}

	return mtk_afe_fe_trigger(substream, cmd, dai);
}

static int mt8365_afe_hw_gain1_startup(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);

	mt8365_afe_enable_main_clk(afe);
	return 0;
}

static void mt8365_afe_hw_gain1_shutdown(struct snd_pcm_substream *substream,
					 struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8365_afe_private *afe_priv = afe->platform_priv;
	struct mt8365_be_dai_data *be =
		&afe_priv->be_data[dai->id - MT8365_AFE_BACKEND_BASE];

	if (be->prepared[substream->stream]) {
		regmap_update_bits(afe->regmap, AFE_GAIN1_CON0,
				   AFE_GAIN1_CON0_EN_MASK, 0);
		be->prepared[substream->stream] = false;
	}
	mt8365_afe_disable_main_clk(afe);
}

static int mt8365_afe_hw_gain1_prepare(struct snd_pcm_substream *substream,
				       struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct mt8365_afe_private *afe_priv = afe->platform_priv;
	struct mt8365_be_dai_data *be =
		&afe_priv->be_data[dai->id - MT8365_AFE_BACKEND_BASE];

	int fs;
	unsigned int val1 = 0, val2 = 0;

	if (be->prepared[substream->stream]) {
		dev_info(afe->dev, "%s prepared already\n", __func__);
		return 0;
	}

	fs = mt8365_afe_fs_timing(substream->runtime->rate);
	regmap_update_bits(afe->regmap, AFE_GAIN1_CON0,
			   AFE_GAIN1_CON0_MODE_MASK, (unsigned int)fs << 4);

	regmap_read(afe->regmap, AFE_GAIN1_CON1, &val1);
	regmap_read(afe->regmap, AFE_GAIN1_CUR, &val2);
	if ((val1 & AFE_GAIN1_CON1_MASK) != (val2 & AFE_GAIN1_CUR_MASK))
		regmap_update_bits(afe->regmap, AFE_GAIN1_CUR,
				   AFE_GAIN1_CUR_MASK, val1);

	regmap_update_bits(afe->regmap, AFE_GAIN1_CON0,
			   AFE_GAIN1_CON0_EN_MASK, 1);
	be->prepared[substream->stream] = true;

	return 0;
}

static const struct snd_pcm_hardware mt8365_hostless_hardware = {
	.info = (SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
		 SNDRV_PCM_INFO_MMAP_VALID),
	.period_bytes_min = 256,
	.period_bytes_max = 4 * 48 * 1024,
	.periods_min = 2,
	.periods_max = 256,
	.buffer_bytes_max = 8 * 48 * 1024,
	.fifo_size = 0,
};

/* dai ops */
static int mtk_dai_hostless_startup(struct snd_pcm_substream *substream,
				    struct snd_soc_dai *dai)
{
	struct mtk_base_afe *afe = snd_soc_dai_get_drvdata(dai);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int ret;

	snd_soc_set_runtime_hwparams(substream, &mt8365_hostless_hardware);

	ret = snd_pcm_hw_constraint_integer(runtime,
					    SNDRV_PCM_HW_PARAM_PERIODS);
	if (ret < 0)
		dev_err(afe->dev, "snd_pcm_hw_constraint_integer failed\n");
	return ret;
}

/* FE DAIs */
static const struct snd_soc_dai_ops mt8365_afe_fe_dai_ops = {
	.startup	= mt8365_afe_fe_startup,
	.shutdown	= mt8365_afe_fe_shutdown,
	.hw_params	= mt8365_afe_fe_hw_params,
	.hw_free	= mt8365_afe_fe_hw_free,
	.prepare	= mt8365_afe_fe_prepare,
	.trigger	= mt8365_afe_fe_trigger,
};

static const struct snd_soc_dai_ops mt8365_dai_hostless_ops = {
	.startup = mtk_dai_hostless_startup,
};

static const struct snd_soc_dai_ops mt8365_afe_hw_gain1_ops = {
	.startup	= mt8365_afe_hw_gain1_startup,
	.shutdown	= mt8365_afe_hw_gain1_shutdown,
	.prepare	= mt8365_afe_hw_gain1_prepare,
};

static struct snd_soc_dai_driver mt8365_memif_dai_driver[] = {
	/* FE DAIs: memory intefaces to CPU */
	{
		.name = "DL1",
		.id = MT8365_AFE_MEMIF_DL1,
		.playback = {
			.stream_name = "DL1",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &mt8365_afe_fe_dai_ops,
	}, {
		.name = "DL2",
		.id = MT8365_AFE_MEMIF_DL2,
		.playback = {
			.stream_name = "DL2",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &mt8365_afe_fe_dai_ops,
	}, {
		.name = "TDM_OUT",
		.id = MT8365_AFE_MEMIF_TDM_OUT,
		.playback = {
			.stream_name = "TDM_OUT",
			.channels_min = 1,
			.channels_max = 8,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &mt8365_afe_fe_dai_ops,
	}, {
		.name = "AWB",
		.id = MT8365_AFE_MEMIF_AWB,
		.capture = {
			.stream_name = "AWB",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &mt8365_afe_fe_dai_ops,
	}, {
		.name = "VUL",
		.id = MT8365_AFE_MEMIF_VUL,
		.capture = {
			.stream_name = "VUL",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &mt8365_afe_fe_dai_ops,
	}, {
		.name = "VUL2",
		.id = MT8365_AFE_MEMIF_VUL2,
		.capture = {
			.stream_name = "VUL2",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &mt8365_afe_fe_dai_ops,
	}, {
		.name = "VUL3",
		.id = MT8365_AFE_MEMIF_VUL3,
		.capture = {
			.stream_name = "VUL3",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &mt8365_afe_fe_dai_ops,
	}, {
		.name = "TDM_IN",
		.id = MT8365_AFE_MEMIF_TDM_IN,
		.capture = {
			.stream_name = "TDM_IN",
			.channels_min = 1,
			.channels_max = 16,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &mt8365_afe_fe_dai_ops,
	}, {
		.name = "Hostless FM DAI",
		.id = MT8365_AFE_IO_VIRTUAL_FM,
		.playback = {
			.stream_name = "Hostless FM DL",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
					  SNDRV_PCM_FMTBIT_S24_LE |
					   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "Hostless FM UL",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
					  SNDRV_PCM_FMTBIT_S24_LE |
					   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &mt8365_dai_hostless_ops,
	}, {
		.name = "HW_GAIN1",
		.id = MT8365_AFE_IO_HW_GAIN1,
		.playback = {
			.stream_name = "HW Gain 1 In",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.capture = {
			.stream_name = "HW Gain 1 Out",
			.channels_min = 1,
			.channels_max = 2,
			.rates = SNDRV_PCM_RATE_8000_192000,
			.formats = SNDRV_PCM_FMTBIT_S16_LE |
				   SNDRV_PCM_FMTBIT_S24_LE |
				   SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &mt8365_afe_hw_gain1_ops,
		.symmetric_rate = 1,
		.symmetric_channels = 1,
		.symmetric_sample_bits = 1,
	},
};

static const struct snd_kcontrol_new mt8365_afe_o00_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I05 Switch", AFE_CONN0, 5, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I07 Switch", AFE_CONN0, 7, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o01_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I06 Switch", AFE_CONN1, 6, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I08 Switch", AFE_CONN1, 8, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o03_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I05 Switch", AFE_CONN3, 5, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I07 Switch", AFE_CONN3, 7, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I00 Switch", AFE_CONN3, 0, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I10 Switch", AFE_CONN3, 10, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o04_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I06 Switch", AFE_CONN4, 6, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I08 Switch", AFE_CONN4, 8, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I01 Switch", AFE_CONN4, 1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I11 Switch", AFE_CONN4, 11, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o05_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I00 Switch", AFE_CONN5, 0, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I03 Switch", AFE_CONN5, 3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I05 Switch", AFE_CONN5, 5, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I07 Switch", AFE_CONN5, 7, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I09 Switch", AFE_CONN5, 9, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I14 Switch", AFE_CONN5, 14, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I16 Switch", AFE_CONN5, 16, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I18 Switch", AFE_CONN5, 18, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I20 Switch", AFE_CONN5, 20, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I23 Switch", AFE_CONN5, 23, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I10L Switch", AFE_CONN5, 10, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o06_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I01 Switch", AFE_CONN6, 1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I04 Switch", AFE_CONN6, 4, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I06 Switch", AFE_CONN6, 6, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I08 Switch", AFE_CONN6, 8, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I22 Switch", AFE_CONN6, 22, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I15 Switch", AFE_CONN6, 15, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I17 Switch", AFE_CONN6, 17, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I19 Switch", AFE_CONN6, 19, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I21 Switch", AFE_CONN6, 21, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I24 Switch", AFE_CONN6, 24, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I11L Switch", AFE_CONN6, 11, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o07_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I05 Switch", AFE_CONN7, 5, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I07 Switch", AFE_CONN7, 7, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o08_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I06 Switch", AFE_CONN8, 6, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I08 Switch", AFE_CONN8, 8, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o09_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I00 Switch", AFE_CONN9, 0, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I03 Switch", AFE_CONN9, 3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I09 Switch", AFE_CONN9, 9, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I14 Switch", AFE_CONN9, 14, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I16 Switch", AFE_CONN9, 16, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I18 Switch", AFE_CONN9, 18, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I20 Switch", AFE_CONN9, 20, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o10_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I01 Switch", AFE_CONN10, 1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I04 Switch", AFE_CONN10, 4, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I22 Switch", AFE_CONN10, 22, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I15 Switch", AFE_CONN10, 15, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I17 Switch", AFE_CONN10, 17, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I19 Switch", AFE_CONN10, 19, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I21 Switch", AFE_CONN10, 21, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o11_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I00 Switch", AFE_CONN11, 0, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I03 Switch", AFE_CONN11, 3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I09 Switch", AFE_CONN11, 9, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I14 Switch", AFE_CONN11, 14, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I16 Switch", AFE_CONN11, 16, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I18 Switch", AFE_CONN11, 18, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I20 Switch", AFE_CONN11, 20, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o12_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I01 Switch", AFE_CONN12, 1, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I04 Switch", AFE_CONN12, 4, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I22 Switch", AFE_CONN12, 22, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I15 Switch", AFE_CONN12, 15, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I17 Switch", AFE_CONN12, 17, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I19 Switch", AFE_CONN12, 19, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I21 Switch", AFE_CONN12, 21, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o13_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I00 Switch", AFE_CONN13, 0, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o14_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I01 Switch", AFE_CONN14, 1, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o15_mix[] = {
};

static const struct snd_kcontrol_new mt8365_afe_o16_mix[] = {
};

static const struct snd_kcontrol_new mt8365_afe_o17_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I03 Switch", AFE_CONN17, 3, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I14 Switch", AFE_CONN17, 14, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o18_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I04 Switch", AFE_CONN18, 4, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I15 Switch", AFE_CONN18, 15, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I23 Switch", AFE_CONN18, 23, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I25 Switch", AFE_CONN18, 25, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o19_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I04 Switch", AFE_CONN19, 4, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I16 Switch", AFE_CONN19, 16, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I23 Switch", AFE_CONN19, 23, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I24 Switch", AFE_CONN19, 24, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I25 Switch", AFE_CONN19, 25, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I26 Switch", AFE_CONN19, 26, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o20_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I17 Switch", AFE_CONN20, 17, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I24 Switch", AFE_CONN20, 24, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I26 Switch", AFE_CONN20, 26, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o21_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I18 Switch", AFE_CONN21, 18, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I23 Switch", AFE_CONN21, 23, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I25 Switch", AFE_CONN21, 25, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o22_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I19 Switch", AFE_CONN22, 19, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I24 Switch", AFE_CONN22, 24, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I26 Switch", AFE_CONN22, 26, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o23_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I20 Switch", AFE_CONN23, 20, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I23 Switch", AFE_CONN23, 23, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I25 Switch", AFE_CONN23, 25, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o24_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I21 Switch", AFE_CONN24, 21, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I24 Switch", AFE_CONN24, 24, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I26 Switch", AFE_CONN24, 26, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I23 Switch", AFE_CONN24, 23, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I25 Switch", AFE_CONN24, 25, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o25_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I27 Switch", AFE_CONN25, 27, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I23 Switch", AFE_CONN25, 23, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I25 Switch", AFE_CONN25, 25, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o26_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I28 Switch", AFE_CONN26, 28, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I24 Switch", AFE_CONN26, 24, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I26 Switch", AFE_CONN26, 26, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o27_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I05 Switch", AFE_CONN27, 5, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I07 Switch", AFE_CONN27, 7, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o28_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I06 Switch", AFE_CONN28, 6, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I08 Switch", AFE_CONN28, 8, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o29_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I05 Switch", AFE_CONN29, 5, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I07 Switch", AFE_CONN29, 7, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o30_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I06 Switch", AFE_CONN30, 6, 1, 0),
	SOC_DAPM_SINGLE_AUTODISABLE("I08 Switch", AFE_CONN30, 8, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o31_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I29 Switch", AFE_CONN31, 29, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o32_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I30 Switch", AFE_CONN32, 30, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o33_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I31 Switch", AFE_CONN33, 31, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o34_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I32 Switch", AFE_CONN34_1, 0, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o35_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I33 Switch", AFE_CONN35_1, 1, 1, 0),
};

static const struct snd_kcontrol_new mt8365_afe_o36_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("I34 Switch", AFE_CONN36_1, 2, 1, 0),
};

static const struct snd_kcontrol_new mtk_hw_gain1_in_ch1_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("CONNSYS_I2S_CH1 Switch", AFE_CONN13,
				    0, 1, 0),
};

static const struct snd_kcontrol_new mtk_hw_gain1_in_ch2_mix[] = {
	SOC_DAPM_SINGLE_AUTODISABLE("CONNSYS_I2S_CH2 Switch", AFE_CONN14,
				    1, 1, 0),
};

static int mt8365_afe_cm2_io_input_mux_get(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	ucontrol->value.integer.value[0] = mCM2Input;

	return 0;
}

static int mt8365_afe_cm2_io_input_mux_put(struct snd_kcontrol *kcontrol,
					   struct snd_ctl_elem_value *ucontrol)
{
	struct snd_soc_dapm_context *dapm =
		snd_soc_dapm_kcontrol_dapm(kcontrol);
	struct snd_soc_component *comp = snd_soc_dapm_to_component(dapm);
	struct mtk_base_afe *afe = snd_soc_component_get_drvdata(comp);
	struct mt8365_afe_private *afe_priv = afe->platform_priv;
	int ret;

	mCM2Input = ucontrol->value.enumerated.item[0];

	afe_priv->cm2_mux_input = mCM2Input;
	ret = snd_soc_dapm_put_enum_double(kcontrol, ucontrol);

	return ret;
}

static const char * const fmhwgain_text[] = {
	"OPEN", "FM_HW_GAIN_IO"
};

static const char * const ain_text[] = {
	"INT ADC", "EXT ADC",
};

static const char * const vul2_in_input_text[] = {
	"VUL2_IN_FROM_O17O18", "VUL2_IN_FROM_CM1",
};

static const char * const mt8365_afe_cm2_mux_text[] = {
	"OPEN", "FROM_GASRC1_OUT", "FROM_GASRC2_OUT", "FROM_TDM_ASRC_OUT",
};

static SOC_ENUM_SINGLE_VIRT_DECL(fmhwgain_enum, fmhwgain_text);
static SOC_ENUM_SINGLE_DECL(ain_enum, AFE_ADDA_TOP_CON0, 0, ain_text);
static SOC_ENUM_SINGLE_VIRT_DECL(vul2_in_input_enum, vul2_in_input_text);
static SOC_ENUM_SINGLE_VIRT_DECL(mt8365_afe_cm2_mux_input_enum,
	mt8365_afe_cm2_mux_text);

static const struct snd_kcontrol_new fmhwgain_mux =
	SOC_DAPM_ENUM("FM HW Gain Source", fmhwgain_enum);

static const struct snd_kcontrol_new ain_mux =
	SOC_DAPM_ENUM("AIN Source", ain_enum);

static const struct snd_kcontrol_new vul2_in_input_mux =
	SOC_DAPM_ENUM("VUL2 Input", vul2_in_input_enum);

static const struct snd_kcontrol_new mt8365_afe_cm2_mux_input_mux =
	SOC_DAPM_ENUM_EXT("CM2_MUX Source", mt8365_afe_cm2_mux_input_enum,
			  mt8365_afe_cm2_io_input_mux_get,
			  mt8365_afe_cm2_io_input_mux_put);

static const struct snd_soc_dapm_widget mt8365_memif_widgets[] = {
	/* inter-connections */
	SND_SOC_DAPM_MIXER("I00", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I01", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I03", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I04", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I05", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I06", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I07", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I08", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I05L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I06L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I07L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I08L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I09", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I10", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I11", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I10L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I11L", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I12", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I13", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I14", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I15", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I16", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I17", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I18", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I19", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I20", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I21", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I22", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I23", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I24", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I25", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I26", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I27", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I28", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I29", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I30", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I31", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I32", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I33", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("I34", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("O00", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o00_mix, ARRAY_SIZE(mt8365_afe_o00_mix)),
	SND_SOC_DAPM_MIXER("O01", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o01_mix, ARRAY_SIZE(mt8365_afe_o01_mix)),
	SND_SOC_DAPM_MIXER("O03", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o03_mix, ARRAY_SIZE(mt8365_afe_o03_mix)),
	SND_SOC_DAPM_MIXER("O04", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o04_mix, ARRAY_SIZE(mt8365_afe_o04_mix)),
	SND_SOC_DAPM_MIXER("O05", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o05_mix, ARRAY_SIZE(mt8365_afe_o05_mix)),
	SND_SOC_DAPM_MIXER("O06", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o06_mix, ARRAY_SIZE(mt8365_afe_o06_mix)),
	SND_SOC_DAPM_MIXER("O07", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o07_mix, ARRAY_SIZE(mt8365_afe_o07_mix)),
	SND_SOC_DAPM_MIXER("O08", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o08_mix, ARRAY_SIZE(mt8365_afe_o08_mix)),
	SND_SOC_DAPM_MIXER("O09", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o09_mix, ARRAY_SIZE(mt8365_afe_o09_mix)),
	SND_SOC_DAPM_MIXER("O10", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o10_mix, ARRAY_SIZE(mt8365_afe_o10_mix)),
	SND_SOC_DAPM_MIXER("O11", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o11_mix, ARRAY_SIZE(mt8365_afe_o11_mix)),
	SND_SOC_DAPM_MIXER("O12", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o12_mix, ARRAY_SIZE(mt8365_afe_o12_mix)),
	SND_SOC_DAPM_MIXER("O13", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o13_mix, ARRAY_SIZE(mt8365_afe_o13_mix)),
	SND_SOC_DAPM_MIXER("O14", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o14_mix, ARRAY_SIZE(mt8365_afe_o14_mix)),
	SND_SOC_DAPM_MIXER("O15", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o15_mix, ARRAY_SIZE(mt8365_afe_o15_mix)),
	SND_SOC_DAPM_MIXER("O16", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o16_mix, ARRAY_SIZE(mt8365_afe_o16_mix)),
	SND_SOC_DAPM_MIXER("O17", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o17_mix, ARRAY_SIZE(mt8365_afe_o17_mix)),
	SND_SOC_DAPM_MIXER("O18", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o18_mix, ARRAY_SIZE(mt8365_afe_o18_mix)),
	SND_SOC_DAPM_MIXER("O19", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o19_mix, ARRAY_SIZE(mt8365_afe_o19_mix)),
	SND_SOC_DAPM_MIXER("O20", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o20_mix, ARRAY_SIZE(mt8365_afe_o20_mix)),
	SND_SOC_DAPM_MIXER("O21", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o21_mix, ARRAY_SIZE(mt8365_afe_o21_mix)),
	SND_SOC_DAPM_MIXER("O22", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o22_mix, ARRAY_SIZE(mt8365_afe_o22_mix)),
	SND_SOC_DAPM_MIXER("O23", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o23_mix, ARRAY_SIZE(mt8365_afe_o23_mix)),
	SND_SOC_DAPM_MIXER("O24", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o24_mix, ARRAY_SIZE(mt8365_afe_o24_mix)),
	SND_SOC_DAPM_MIXER("O25", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o25_mix, ARRAY_SIZE(mt8365_afe_o25_mix)),
	SND_SOC_DAPM_MIXER("O26", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o26_mix, ARRAY_SIZE(mt8365_afe_o26_mix)),
	SND_SOC_DAPM_MIXER("O27", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o27_mix, ARRAY_SIZE(mt8365_afe_o27_mix)),
	SND_SOC_DAPM_MIXER("O28", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o28_mix, ARRAY_SIZE(mt8365_afe_o28_mix)),
	SND_SOC_DAPM_MIXER("O29", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o29_mix, ARRAY_SIZE(mt8365_afe_o29_mix)),
	SND_SOC_DAPM_MIXER("O30", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o30_mix, ARRAY_SIZE(mt8365_afe_o30_mix)),
	SND_SOC_DAPM_MIXER("O31", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o31_mix, ARRAY_SIZE(mt8365_afe_o31_mix)),
	SND_SOC_DAPM_MIXER("O32", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o32_mix, ARRAY_SIZE(mt8365_afe_o32_mix)),
	SND_SOC_DAPM_MIXER("O33", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o33_mix, ARRAY_SIZE(mt8365_afe_o33_mix)),
	SND_SOC_DAPM_MIXER("O34", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o34_mix, ARRAY_SIZE(mt8365_afe_o34_mix)),
	SND_SOC_DAPM_MIXER("O35", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o35_mix, ARRAY_SIZE(mt8365_afe_o35_mix)),
	SND_SOC_DAPM_MIXER("O36", SND_SOC_NOPM, 0, 0,
			   mt8365_afe_o36_mix, ARRAY_SIZE(mt8365_afe_o36_mix)),
	SND_SOC_DAPM_MIXER("CM2_Mux IO", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("CM1_IO", SND_SOC_NOPM, 0, 0, NULL, 0),
	SND_SOC_DAPM_MIXER("O17O18", SND_SOC_NOPM, 0, 0, NULL, 0),
	/* inter-connections */
	SND_SOC_DAPM_MIXER("HW_GAIN1_IN_CH1", SND_SOC_NOPM, 0, 0,
			   mtk_hw_gain1_in_ch1_mix,
			   ARRAY_SIZE(mtk_hw_gain1_in_ch1_mix)),
	SND_SOC_DAPM_MIXER("HW_GAIN1_IN_CH2", SND_SOC_NOPM, 0, 0,
			   mtk_hw_gain1_in_ch2_mix,
			   ARRAY_SIZE(mtk_hw_gain1_in_ch2_mix)),

	SND_SOC_DAPM_INPUT("DL Source"),

	SND_SOC_DAPM_MUX("CM2_Mux_IO Input Mux", SND_SOC_NOPM, 0, 0,
			 &mt8365_afe_cm2_mux_input_mux),

	SND_SOC_DAPM_MUX("AIN Mux", SND_SOC_NOPM, 0, 0, &ain_mux),
	SND_SOC_DAPM_MUX("VUL2 Input Mux", SND_SOC_NOPM, 0, 0,
			 &vul2_in_input_mux),

	SND_SOC_DAPM_MUX("FM HW Gain Mux", SND_SOC_NOPM, 0, 0, &fmhwgain_mux),

	SND_SOC_DAPM_INPUT("HW Gain 1 Out Endpoint"),
	SND_SOC_DAPM_OUTPUT("HW Gain 1 In Endpoint"),
};

static const struct snd_soc_dapm_route mt8365_memif_routes[] = {
	/* downlink */
	{"I00", NULL, "2ND I2S Capture"},
	{"I01", NULL, "2ND I2S Capture"},
	{"I05", NULL, "DL1"},
	{"I06", NULL, "DL1"},
	{"I07", NULL, "DL2"},
	{"I08", NULL, "DL2"},

	{"O03", "I05 Switch", "I05"},
	{"O04", "I06 Switch", "I06"},
	{"O00", "I05 Switch", "I05"},
	{"O01", "I06 Switch", "I06"},
	{"O07", "I05 Switch", "I05"},
	{"O08", "I06 Switch", "I06"},
	{"O27", "I05 Switch", "I05"},
	{"O28", "I06 Switch", "I06"},
	{"O29", "I05 Switch", "I05"},
	{"O30", "I06 Switch", "I06"},

	{"O03", "I07 Switch", "I07"},
	{"O04", "I08 Switch", "I08"},
	{"O00", "I07 Switch", "I07"},
	{"O01", "I08 Switch", "I08"},
	{"O07", "I07 Switch", "I07"},
	{"O08", "I08 Switch", "I08"},

	/* uplink */
	{"AWB", NULL, "O05"},
	{"AWB", NULL, "O06"},
	{"VUL", NULL, "O09"},
	{"VUL", NULL, "O10"},
	{"VUL3", NULL, "O11"},
	{"VUL3", NULL, "O12"},

	{"AIN Mux", "EXT ADC", "I2S Capture"},
	{"I03", NULL, "AIN Mux"},
	{"I04", NULL, "AIN Mux"},

	{"HW_GAIN1_IN_CH1", "CONNSYS_I2S_CH1", "Hostless FM DL"},
	{"HW_GAIN1_IN_CH2", "CONNSYS_I2S_CH2", "Hostless FM DL"},

	{"HW Gain 1 In Endpoint", NULL, "HW Gain 1 In"},
	{"HW Gain 1 Out", NULL, "HW Gain 1 Out Endpoint"},
	{"HW Gain 1 In", NULL, "HW_GAIN1_IN_CH1"},
	{"HW Gain 1 In", NULL, "HW_GAIN1_IN_CH2"},

	{"FM HW Gain Mux", "FM_HW_GAIN_IO", "HW Gain 1 Out"},
	{"Hostless FM UL", NULL, "FM HW Gain Mux"},
	{"Hostless FM UL", NULL, "FM 2ND I2S Mux"},

	{"O05", "I05 Switch", "I05L"},
	{"O06", "I06 Switch", "I06L"},
	{"O05", "I07 Switch", "I07L"},
	{"O06", "I08 Switch", "I08L"},

	{"O05", "I03 Switch", "I03"},
	{"O06", "I04 Switch", "I04"},
	{"O05", "I00 Switch", "I00"},
	{"O06", "I01 Switch", "I01"},
	{"O05", "I09 Switch", "I09"},
	{"O06", "I22 Switch", "I22"},
	{"O05", "I14 Switch", "I14"},
	{"O06", "I15 Switch", "I15"},
	{"O05", "I16 Switch", "I16"},
	{"O06", "I17 Switch", "I17"},
	{"O05", "I18 Switch", "I18"},
	{"O06", "I19 Switch", "I19"},
	{"O05", "I20 Switch", "I20"},
	{"O06", "I21 Switch", "I21"},
	{"O05", "I23 Switch", "I23"},
	{"O06", "I24 Switch", "I24"},

	{"O09", "I03 Switch", "I03"},
	{"O10", "I04 Switch", "I04"},
	{"O09", "I00 Switch", "I00"},
	{"O10", "I01 Switch", "I01"},
	{"O09", "I09 Switch", "I09"},
	{"O10", "I22 Switch", "I22"},
	{"O09", "I14 Switch", "I14"},
	{"O10", "I15 Switch", "I15"},
	{"O09", "I16 Switch", "I16"},
	{"O10", "I17 Switch", "I17"},
	{"O09", "I18 Switch", "I18"},
	{"O10", "I19 Switch", "I19"},
	{"O09", "I20 Switch", "I20"},
	{"O10", "I21 Switch", "I21"},

	{"O11", "I03 Switch", "I03"},
	{"O12", "I04 Switch", "I04"},
	{"O11", "I00 Switch", "I00"},
	{"O12", "I01 Switch", "I01"},
	{"O11", "I09 Switch", "I09"},
	{"O12", "I22 Switch", "I22"},
	{"O11", "I14 Switch", "I14"},
	{"O12", "I15 Switch", "I15"},
	{"O11", "I16 Switch", "I16"},
	{"O12", "I17 Switch", "I17"},
	{"O11", "I18 Switch", "I18"},
	{"O12", "I19 Switch", "I19"},
	{"O11", "I20 Switch", "I20"},
	{"O12", "I21 Switch", "I21"},

	/* CM2_Mux*/
	{"CM2_Mux IO", NULL, "CM2_Mux_IO Input Mux"},

	/* VUL2 */
	{"VUL2", NULL, "VUL2 Input Mux"},
	{"VUL2 Input Mux", "VUL2_IN_FROM_O17O18", "O17O18"},
	{"VUL2 Input Mux", "VUL2_IN_FROM_CM1", "CM1_IO"},

	{"O17O18", NULL, "O17"},
	{"O17O18", NULL, "O18"},
	{"CM1_IO", NULL, "O17"},
	{"CM1_IO", NULL, "O18"},
	{"CM1_IO", NULL, "O19"},
	{"CM1_IO", NULL, "O20"},
	{"CM1_IO", NULL, "O21"},
	{"CM1_IO", NULL, "O22"},
	{"CM1_IO", NULL, "O23"},
	{"CM1_IO", NULL, "O24"},
	{"CM1_IO", NULL, "O25"},
	{"CM1_IO", NULL, "O26"},
	{"CM1_IO", NULL, "O31"},
	{"CM1_IO", NULL, "O32"},
	{"CM1_IO", NULL, "O33"},
	{"CM1_IO", NULL, "O34"},
	{"CM1_IO", NULL, "O35"},
	{"CM1_IO", NULL, "O36"},

	{"O17", "I14 Switch", "I14"},
	{"O18", "I15 Switch", "I15"},
	{"O19", "I16 Switch", "I16"},
	{"O20", "I17 Switch", "I17"},
	{"O21", "I18 Switch", "I18"},
	{"O22", "I19 Switch", "I19"},
	{"O23", "I20 Switch", "I20"},
	{"O24", "I21 Switch", "I21"},
	{"O25", "I23 Switch", "I23"},
	{"O26", "I24 Switch", "I24"},
	{"O25", "I25 Switch", "I25"},
	{"O26", "I26 Switch", "I26"},

	{"O17", "I03 Switch", "I03"},
	{"O18", "I04 Switch", "I04"},
	{"O18", "I23 Switch", "I23"},
	{"O18", "I25 Switch", "I25"},
	{"O19", "I04 Switch", "I04"},
	{"O19", "I23 Switch", "I23"},
	{"O19", "I24 Switch", "I24"},
	{"O19", "I25 Switch", "I25"},
	{"O19", "I26 Switch", "I26"},
	{"O20", "I24 Switch", "I24"},
	{"O20", "I26 Switch", "I26"},
	{"O21", "I23 Switch", "I23"},
	{"O21", "I25 Switch", "I25"},
	{"O22", "I24 Switch", "I24"},
	{"O22", "I26 Switch", "I26"},

	{"O23", "I23 Switch", "I23"},
	{"O23", "I25 Switch", "I25"},
	{"O24", "I24 Switch", "I24"},
	{"O24", "I26 Switch", "I26"},
	{"O24", "I23 Switch", "I23"},
	{"O24", "I25 Switch", "I25"},
	{"O13", "I00 Switch", "I00"},
	{"O14", "I01 Switch", "I01"},
	{"O03", "I10 Switch", "I10"},
	{"O04", "I11 Switch", "I11"},
};

static const struct mtk_base_memif_data memif_data[MT8365_AFE_MEMIF_NUM] = {
	{
		.name = "DL1",
		.id = MT8365_AFE_MEMIF_DL1,
		.reg_ofs_base = AFE_DL1_BASE,
		.reg_ofs_cur = AFE_DL1_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = 0,
		.fs_maskbit = 0xf,
		.mono_reg = AFE_DAC_CON1,
		.mono_shift = 21,
		.hd_reg = AFE_MEMIF_PBUF_SIZE,
		.hd_shift = 16,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 1,
		.msb_reg = -1,
		.msb_shift = -1,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
	}, {
		.name = "DL2",
		.id = MT8365_AFE_MEMIF_DL2,
		.reg_ofs_base = AFE_DL2_BASE,
		.reg_ofs_cur = AFE_DL2_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = 4,
		.fs_maskbit = 0xf,
		.mono_reg = AFE_DAC_CON1,
		.mono_shift = 22,
		.hd_reg = AFE_MEMIF_PBUF_SIZE,
		.hd_shift = 18,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 2,
		.msb_reg = -1,
		.msb_shift = -1,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
	}, {
		.name = "TDM OUT",
		.id = MT8365_AFE_MEMIF_TDM_OUT,
		.reg_ofs_base = AFE_HDMI_OUT_BASE,
		.reg_ofs_cur = AFE_HDMI_OUT_CUR,
		.fs_reg = -1,
		.fs_shift = -1,
		.fs_maskbit = -1,
		.mono_reg = -1,
		.mono_shift = -1,
		.hd_reg = AFE_MEMIF_PBUF_SIZE,
		.hd_shift = 28,
		.enable_reg = AFE_HDMI_OUT_CON0,
		.enable_shift = 0,
		.msb_reg = -1,
		.msb_shift = -1,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
	}, {
		.name = "AWB",
		.id = MT8365_AFE_MEMIF_AWB,
		.reg_ofs_base = AFE_AWB_BASE,
		.reg_ofs_cur = AFE_AWB_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = 12,
		.fs_maskbit = 0xf,
		.mono_reg = AFE_DAC_CON1,
		.mono_shift = 24,
		.hd_reg = AFE_MEMIF_PBUF_SIZE,
		.hd_shift = 20,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 6,
		.msb_reg = AFE_MEMIF_MSB,
		.msb_shift = 17,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
	}, {
		.name = "VUL",
		.id = MT8365_AFE_MEMIF_VUL,
		.reg_ofs_base = AFE_VUL_BASE,
		.reg_ofs_cur = AFE_VUL_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = 16,
		.fs_maskbit = 0xf,
		.mono_reg = AFE_DAC_CON1,
		.mono_shift = 27,
		.hd_reg = AFE_MEMIF_PBUF_SIZE,
		.hd_shift = 22,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 3,
		.msb_reg = AFE_MEMIF_MSB,
		.msb_shift = 20,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
	}, {
		.name = "VUL2",
		.id = MT8365_AFE_MEMIF_VUL2,
		.reg_ofs_base = AFE_VUL_D2_BASE,
		.reg_ofs_cur = AFE_VUL_D2_CUR,
		.fs_reg = AFE_DAC_CON0,
		.fs_shift = 20,
		.fs_maskbit = 0xf,
		.mono_reg = -1,
		.mono_shift = -1,
		.hd_reg = AFE_MEMIF_PBUF_SIZE,
		.hd_shift = 14,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 9,
		.msb_reg = AFE_MEMIF_MSB,
		.msb_shift = 21,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
	}, {
		.name = "VUL3",
		.id = MT8365_AFE_MEMIF_VUL3,
		.reg_ofs_base = AFE_VUL3_BASE,
		.reg_ofs_cur = AFE_VUL3_CUR,
		.fs_reg = AFE_DAC_CON1,
		.fs_shift = 8,
		.fs_maskbit = 0xf,
		.mono_reg = AFE_DAC_CON0,
		.mono_shift = 13,
		.hd_reg = AFE_MEMIF_PBUF2_SIZE,
		.hd_shift = 10,
		.enable_reg = AFE_DAC_CON0,
		.enable_shift = 12,
		.msb_reg = AFE_MEMIF_MSB,
		.msb_shift = 27,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
	}, {
		.name = "TDM IN",
		.id = MT8365_AFE_MEMIF_TDM_IN,
		.reg_ofs_base = AFE_HDMI_IN_2CH_BASE,
		.reg_ofs_cur = AFE_HDMI_IN_2CH_CUR,
		.fs_reg = -1,
		.fs_shift = -1,
		.fs_maskbit = -1,
		.mono_reg = AFE_HDMI_IN_2CH_CON0,
		.mono_shift = 1,
		.hd_reg = AFE_MEMIF_PBUF2_SIZE,
		.hd_shift = 8,
		.hd_align_mshift = 5,
		.enable_reg = AFE_HDMI_IN_2CH_CON0,
		.enable_shift = 0,
		.msb_reg = AFE_MEMIF_MSB,
		.msb_shift = 28,
		.agent_disable_reg = -1,
		.agent_disable_shift = -1,
	},
};

static const struct mtk_base_irq_data irq_data[MT8365_AFE_IRQ_NUM] = {
	{
		.id = MT8365_AFE_IRQ1,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT1,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0x3ffff,
		.irq_en_reg = AFE_IRQ_MCU_CON,
		.irq_en_shift = 0,
		.irq_fs_reg = AFE_IRQ_MCU_CON,
		.irq_fs_shift = 4,
		.irq_fs_maskbit = 0xf,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 0,
	}, {
		.id = MT8365_AFE_IRQ2,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT2,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0x3ffff,
		.irq_en_reg = AFE_IRQ_MCU_CON,
		.irq_en_shift = 1,
		.irq_fs_reg = AFE_IRQ_MCU_CON,
		.irq_fs_shift = 8,
		.irq_fs_maskbit = 0xf,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 1,
	}, {
		.id = MT8365_AFE_IRQ3,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT3,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0x3ffff,
		.irq_en_reg = AFE_IRQ_MCU_CON,
		.irq_en_shift = 2,
		.irq_fs_reg = AFE_IRQ_MCU_CON,
		.irq_fs_shift = 16,
		.irq_fs_maskbit = 0xf,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 2,
	}, {
		.id = MT8365_AFE_IRQ4,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT4,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0x3ffff,
		.irq_en_reg = AFE_IRQ_MCU_CON,
		.irq_en_shift = 3,
		.irq_fs_reg = AFE_IRQ_MCU_CON,
		.irq_fs_shift = 20,
		.irq_fs_maskbit = 0xf,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 3,
	}, {
		.id = MT8365_AFE_IRQ5,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT5,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0x3ffff,
		.irq_en_reg = AFE_IRQ_MCU_CON2,
		.irq_en_shift = 3,
		.irq_fs_reg = -1,
		.irq_fs_shift = 0,
		.irq_fs_maskbit = 0x0,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 4,
	}, {
		.id = MT8365_AFE_IRQ6,
		.irq_cnt_reg = -1,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0x0,
		.irq_en_reg = AFE_IRQ_MCU_CON,
		.irq_en_shift = 13,
		.irq_fs_reg = -1,
		.irq_fs_shift = 0,
		.irq_fs_maskbit = 0x0,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 5,
	}, {
		.id = MT8365_AFE_IRQ7,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT7,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0x3ffff,
		.irq_en_reg = AFE_IRQ_MCU_CON,
		.irq_en_shift = 14,
		.irq_fs_reg = AFE_IRQ_MCU_CON,
		.irq_fs_shift = 24,
		.irq_fs_maskbit = 0xf,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 6,
	}, {
		.id = MT8365_AFE_IRQ8,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT8,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0x3ffff,
		.irq_en_reg = AFE_IRQ_MCU_CON,
		.irq_en_shift = 15,
		.irq_fs_reg = AFE_IRQ_MCU_CON,
		.irq_fs_shift = 28,
		.irq_fs_maskbit = 0xf,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 7,
	}, {
		.id = MT8365_AFE_IRQ9,
		.irq_cnt_reg = -1,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0x0,
		.irq_en_reg = AFE_IRQ_MCU_CON2,
		.irq_en_shift = 2,
		.irq_fs_reg = -1,
		.irq_fs_shift = 0,
		.irq_fs_maskbit = 0x0,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 8,
	}, {
		.id = MT8365_AFE_IRQ10,
		.irq_cnt_reg = AFE_IRQ_MCU_CNT10,
		.irq_cnt_shift = 0,
		.irq_cnt_maskbit = 0x3ffff,
		.irq_en_reg = AFE_IRQ_MCU_CON2,
		.irq_en_shift = 4,
		.irq_fs_reg = -1,
		.irq_fs_shift = 0,
		.irq_fs_maskbit = 0x0,
		.irq_clr_reg = AFE_IRQ_MCU_CLR,
		.irq_clr_shift = 9,
	},
};

static int memif_specified_irqs[MT8365_AFE_MEMIF_NUM] = {
	[MT8365_AFE_MEMIF_DL1] = MT8365_AFE_IRQ1,
	[MT8365_AFE_MEMIF_DL2] = MT8365_AFE_IRQ2,
	[MT8365_AFE_MEMIF_TDM_OUT] = MT8365_AFE_IRQ5,
	[MT8365_AFE_MEMIF_AWB] = MT8365_AFE_IRQ3,
	[MT8365_AFE_MEMIF_VUL] = MT8365_AFE_IRQ4,
	[MT8365_AFE_MEMIF_VUL2] = MT8365_AFE_IRQ7,
	[MT8365_AFE_MEMIF_VUL3] = MT8365_AFE_IRQ8,
	[MT8365_AFE_MEMIF_TDM_IN] = MT8365_AFE_IRQ10,
};

static const struct regmap_config mt8365_afe_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = MAX_REGISTER,
	.cache_type = REGCACHE_NONE,
};

static irqreturn_t mt8365_afe_irq_handler(int irq, void *dev_id)
{
	struct mtk_base_afe *afe = dev_id;
	unsigned int reg_value;
	unsigned int mcu_irq_mask;
	int i, ret;

	ret = regmap_read(afe->regmap, AFE_IRQ_MCU_STATUS, &reg_value);
	if (ret) {
		dev_err_ratelimited(afe->dev, "%s irq status err\n", __func__);
		reg_value = AFE_IRQ_STATUS_BITS;
		goto err_irq;
	}

	ret = regmap_read(afe->regmap, AFE_IRQ_MCU_EN, &mcu_irq_mask);
	if (ret) {
		dev_err_ratelimited(afe->dev, "%s irq mcu_en err\n", __func__);
		reg_value = AFE_IRQ_STATUS_BITS;
		goto err_irq;
	}

	/* only clr cpu irq */
	reg_value &= mcu_irq_mask;

	for (i = 0; i < MT8365_AFE_MEMIF_NUM; i++) {
		struct mtk_base_afe_memif *memif = &afe->memif[i];
		struct mtk_base_afe_irq *mcu_irq;

		if (memif->irq_usage < 0)
			continue;

		mcu_irq = &afe->irqs[memif->irq_usage];

		if (!(reg_value & (1 << mcu_irq->irq_data->irq_clr_shift)))
			continue;

		snd_pcm_period_elapsed(memif->substream);
	}

err_irq:
	/* clear irq */
	regmap_write(afe->regmap, AFE_IRQ_MCU_CLR,
		     reg_value & AFE_IRQ_STATUS_BITS);

	return IRQ_HANDLED;
}

static int __maybe_unused mt8365_afe_runtime_suspend(struct device *dev)
{
	return 0;
}

static int mt8365_afe_runtime_resume(struct device *dev)
{
	return 0;
}

static int __maybe_unused mt8365_afe_suspend(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	struct regmap *regmap = afe->regmap;
	int i;

	mt8365_afe_enable_main_clk(afe);

	if (!afe->reg_back_up)
		afe->reg_back_up =
			devm_kcalloc(dev, afe->reg_back_up_list_num,
				     sizeof(unsigned int), GFP_KERNEL);

	for (i = 0; i < afe->reg_back_up_list_num; i++)
		regmap_read(regmap, afe->reg_back_up_list[i],
			    &afe->reg_back_up[i]);

	mt8365_afe_disable_main_clk(afe);

	return 0;
}

static int __maybe_unused mt8365_afe_resume(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);
	struct regmap *regmap = afe->regmap;
	int i = 0;

	if (!afe->reg_back_up)
		return 0;

	mt8365_afe_enable_main_clk(afe);

	for (i = 0; i < afe->reg_back_up_list_num; i++)
		regmap_write(regmap, afe->reg_back_up_list[i],
			     afe->reg_back_up[i]);

	mt8365_afe_disable_main_clk(afe);

	return 0;
}

static int __maybe_unused mt8365_afe_dev_runtime_suspend(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);

	if (pm_runtime_status_suspended(dev) || afe->suspended)
		return 0;

	mt8365_afe_suspend(dev);
	afe->suspended = true;
	return 0;
}

static int __maybe_unused mt8365_afe_dev_runtime_resume(struct device *dev)
{
	struct mtk_base_afe *afe = dev_get_drvdata(dev);

	if (pm_runtime_status_suspended(dev) || !afe->suspended)
		return 0;

	mt8365_afe_resume(dev);
	afe->suspended = false;
	return 0;
}

static int mt8365_afe_init_registers(struct mtk_base_afe *afe)
{
	size_t i;

	static struct {
		unsigned int reg;
		unsigned int mask;
		unsigned int val;
	} init_regs[] = {
		{ AFE_CONN_24BIT, GENMASK(31, 0), GENMASK(31, 0) },
		{ AFE_CONN_24BIT_1, GENMASK(21, 0), GENMASK(21, 0) },
	};

	mt8365_afe_enable_main_clk(afe);

	for (i = 0; i < ARRAY_SIZE(init_regs); i++)
		regmap_update_bits(afe->regmap, init_regs[i].reg,
				   init_regs[i].mask, init_regs[i].val);

	mt8365_afe_disable_main_clk(afe);

	return 0;
}

static int mt8365_dai_memif_register(struct mtk_base_afe *afe)
{
	struct mtk_base_afe_dai *dai;

	dai = devm_kzalloc(afe->dev, sizeof(*dai), GFP_KERNEL);
	if (!dai)
		return -ENOMEM;

	list_add(&dai->list, &afe->sub_dais);

	dai->dai_drivers = mt8365_memif_dai_driver;
	dai->num_dai_drivers = ARRAY_SIZE(mt8365_memif_dai_driver);

	dai->dapm_widgets = mt8365_memif_widgets;
	dai->num_dapm_widgets = ARRAY_SIZE(mt8365_memif_widgets);
	dai->dapm_routes = mt8365_memif_routes;
	dai->num_dapm_routes = ARRAY_SIZE(mt8365_memif_routes);
	return 0;
}

typedef int (*dai_register_cb)(struct mtk_base_afe *);
static const dai_register_cb dai_register_cbs[] = {
	mt8365_dai_pcm_register,
	mt8365_dai_i2s_register,
	mt8365_dai_adda_register,
	mt8365_dai_dmic_register,
	mt8365_dai_memif_register,
};

static int mt8365_afe_pcm_dev_probe(struct platform_device *pdev)
{
	struct mtk_base_afe *afe;
	struct mt8365_afe_private *afe_priv;
	struct device *dev;
	int ret, i, sel_irq;
	unsigned int irq_id;
	struct resource *res;

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

	spin_lock_init(&afe_priv->afe_ctrl_lock);
	mutex_init(&afe_priv->afe_clk_mutex);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	afe->base_addr = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(afe->base_addr))
		return PTR_ERR(afe->base_addr);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (res) {
		afe_priv->afe_sram_vir_addr =
			devm_ioremap_resource(&pdev->dev, res);
		if (!IS_ERR(afe_priv->afe_sram_vir_addr)) {
			afe_priv->afe_sram_phy_addr = res->start;
			afe_priv->afe_sram_size = resource_size(res);
		}
	}

	/* initial audio related clock */
	ret = mt8365_afe_init_audio_clk(afe);
	if (ret)
		return dev_err_probe(afe->dev, ret, "mt8365_afe_init_audio_clk fail\n");

	afe->regmap = devm_regmap_init_mmio_clk(&pdev->dev, "top_audio_sel",
						afe->base_addr,
						&mt8365_afe_regmap_config);
	if (IS_ERR(afe->regmap))
		return PTR_ERR(afe->regmap);

	/* memif % irq initialize*/
	afe->memif_size = MT8365_AFE_MEMIF_NUM;
	afe->memif = devm_kcalloc(afe->dev, afe->memif_size,
				  sizeof(*afe->memif), GFP_KERNEL);
	if (!afe->memif)
		return -ENOMEM;

	afe->irqs_size = MT8365_AFE_IRQ_NUM;
	afe->irqs = devm_kcalloc(afe->dev, afe->irqs_size,
				 sizeof(*afe->irqs), GFP_KERNEL);
	if (!afe->irqs)
		return -ENOMEM;

	for (i = 0; i < afe->irqs_size; i++)
		afe->irqs[i].irq_data = &irq_data[i];

	ret = platform_get_irq(pdev, 0);
	if (ret < 0)
		return ret;

	irq_id = ret;
	ret = devm_request_irq(afe->dev, irq_id, mt8365_afe_irq_handler,
			       0, "Afe_ISR_Handle", (void *)afe);
	if (ret)
		return dev_err_probe(afe->dev, ret, "could not request_irq\n");

	/* init sub_dais */
	INIT_LIST_HEAD(&afe->sub_dais);

	for (i = 0; i < ARRAY_SIZE(dai_register_cbs); i++) {
		ret = dai_register_cbs[i](afe);
		if (ret) {
			dev_warn(afe->dev, "dai register i %d fail, ret %d\n",
				 i, ret);
			return ret;
		}
	}

	/* init dai_driver and component_driver */
	ret = mtk_afe_combine_sub_dai(afe);
	if (ret) {
		dev_warn(afe->dev, "mtk_afe_combine_sub_dai fail, ret %d\n",
			 ret);
		return ret;
	}

	for (i = 0; i < afe->memif_size; i++) {
		afe->memif[i].data = &memif_data[i];
		sel_irq = memif_specified_irqs[i];
		if (sel_irq >= 0) {
			afe->memif[i].irq_usage = sel_irq;
			afe->memif[i].const_irq = 1;
			afe->irqs[sel_irq].irq_occupyed = true;
		} else {
			afe->memif[i].irq_usage = -1;
		}
	}

	afe->mtk_afe_hardware = &mt8365_afe_hardware;
	afe->memif_fs = mt8365_memif_fs;
	afe->irq_fs = mt8365_irq_fs;

	ret = devm_pm_runtime_enable(&pdev->dev);
	if (ret)
		return ret;

	pm_runtime_get_sync(&pdev->dev);
	afe->reg_back_up_list = mt8365_afe_backup_list;
	afe->reg_back_up_list_num = ARRAY_SIZE(mt8365_afe_backup_list);
	afe->runtime_resume = mt8365_afe_runtime_resume;
	afe->runtime_suspend = mt8365_afe_runtime_suspend;

	/* open afe pdn for dapm read/write audio register */
	mt8365_afe_enable_top_cg(afe, MT8365_TOP_CG_AFE);

	/* Set 26m parent clk */
	mt8365_afe_set_clk_parent(afe,
				  afe_priv->clocks[MT8365_CLK_TOP_AUD_SEL],
				  afe_priv->clocks[MT8365_CLK_CLK26M]);

	ret = devm_snd_soc_register_component(&pdev->dev,
					      &mtk_afe_pcm_platform,
					      afe->dai_drivers,
					      afe->num_dai_drivers);
	if (ret) {
		dev_warn(dev, "err_platform\n");
		return ret;
	}

	mt8365_afe_init_registers(afe);

	return 0;
}

static void mt8365_afe_pcm_dev_remove(struct platform_device *pdev)
{
	struct mtk_base_afe *afe = platform_get_drvdata(pdev);

	mt8365_afe_disable_top_cg(afe, MT8365_TOP_CG_AFE);

	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		mt8365_afe_runtime_suspend(&pdev->dev);
}

static const struct of_device_id mt8365_afe_pcm_dt_match[] = {
	{ .compatible = "mediatek,mt8365-afe-pcm", },
	{ }
};
MODULE_DEVICE_TABLE(of, mt8365_afe_pcm_dt_match);

static const struct dev_pm_ops mt8365_afe_pm_ops = {
	SET_RUNTIME_PM_OPS(mt8365_afe_dev_runtime_suspend,
			   mt8365_afe_dev_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(mt8365_afe_suspend,
				mt8365_afe_resume)
};

static struct platform_driver mt8365_afe_pcm_driver = {
	.driver = {
		   .name = "mt8365-afe-pcm",
		   .of_match_table = mt8365_afe_pcm_dt_match,
		   .pm = &mt8365_afe_pm_ops,
	},
	.probe = mt8365_afe_pcm_dev_probe,
	.remove = mt8365_afe_pcm_dev_remove,
};

module_platform_driver(mt8365_afe_pcm_driver);

MODULE_DESCRIPTION("MediaTek ALSA SoC AFE platform driver");
MODULE_AUTHOR("Jia Zeng <jia.zeng@mediatek.com>");
MODULE_AUTHOR("Alexandre Mergnat <amergnat@baylibre.com>");
MODULE_LICENSE("GPL");
