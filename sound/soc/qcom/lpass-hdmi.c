// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2020 The Linux Foundation. All rights reserved.
 *
 * lpass-hdmi.c -- ALSA SoC HDMI-CPU DAI driver for QTi LPASS HDMI
 */


#include <linux/kernel.h>
#include <linux/module.h>
#include <sound/pcm_params.h>
#include <linux/regmap.h>
#include <sound/soc.h>
#include <sound/soc-dai.h>
#include <dt-bindings/sound/sc7180-lpass.h>
#include "lpass-lpaif-reg.h"
#include "lpass.h"

static int lpass_hdmi_daiops_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params, struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	snd_pcm_format_t format = params_format(params);
	unsigned int rate = params_rate(params);
	unsigned int channels = params_channels(params);
	int bitwidth;
	unsigned int word_length;
	unsigned int ch_sts_buf0;
	unsigned int ch_sts_buf1;
	unsigned int data_format;
	unsigned int sampling_freq;
	unsigned int ch = 0;
	struct lpass_dp_metadata_ctl *meta_ctl = drvdata->meta_ctl;
	struct lpass_sstream_ctl *sstream_ctl = drvdata->sstream_ctl;
	int ret;

	bitwidth = snd_pcm_format_width(format);
	if (bitwidth < 0) {
		dev_err(dai->dev, "%s invalid bit width given : %d\n",
					__func__, bitwidth);
		return bitwidth;
	}

	switch (bitwidth) {
	case 16:
		word_length = LPASS_DP_AUDIO_BITWIDTH16;
		break;
	case 24:
		word_length = LPASS_DP_AUDIO_BITWIDTH24;
		break;
	default:
		dev_err(dai->dev, "%s invalid bit width given : %d\n",
					__func__, bitwidth);
		return -EINVAL;
	}

	switch (rate) {
	case 32000:
		sampling_freq = LPASS_SAMPLING_FREQ32;
		break;
	case 44100:
		sampling_freq = LPASS_SAMPLING_FREQ44;
		break;
	case 48000:
		sampling_freq = LPASS_SAMPLING_FREQ48;
		break;
	default:
		dev_err(dai->dev, "%s invalid bit width given : %d\n",
					__func__, bitwidth);
		return -EINVAL;
	}
	data_format = LPASS_DATA_FORMAT_LINEAR;
	ch_sts_buf0 = (((data_format << LPASS_DATA_FORMAT_SHIFT) & LPASS_DATA_FORMAT_MASK)
				| ((sampling_freq << LPASS_FREQ_BIT_SHIFT) & LPASS_FREQ_BIT_MASK));
	ch_sts_buf1 = (word_length) & LPASS_WORDLENGTH_MASK;

	ret = regmap_field_write(drvdata->tx_ctl->soft_reset, LPASS_TX_CTL_RESET);
	if (ret)
		return ret;

	ret = regmap_field_write(drvdata->tx_ctl->soft_reset, LPASS_TX_CTL_CLEAR);
	if (ret)
		return ret;

	ret = regmap_field_write(drvdata->hdmitx_legacy_en, LPASS_HDMITX_LEGACY_DISABLE);
	if (ret)
		return ret;

	ret = regmap_field_write(drvdata->hdmitx_parity_calc_en, HDMITX_PARITY_CALC_EN);
	if (ret)
		return ret;

	ret = regmap_field_write(drvdata->vbit_ctl->replace_vbit, REPLACE_VBIT);
	if (ret)
		return ret;

	ret = regmap_field_write(drvdata->vbit_ctl->vbit_stream, LINEAR_PCM_DATA);
	if (ret)
		return ret;

	ret = regmap_field_write(drvdata->hdmitx_ch_msb[0], ch_sts_buf1);
	if (ret)
		return ret;

	ret = regmap_field_write(drvdata->hdmitx_ch_lsb[0], ch_sts_buf0);
	if (ret)
		return ret;

	ret = regmap_field_write(drvdata->hdmi_tx_dmactl[0]->use_hw_chs, HW_MODE);
	if (ret)
		return ret;

	ret = regmap_field_write(drvdata->hdmi_tx_dmactl[0]->hw_chs_sel, SW_MODE);
	if (ret)
		return ret;

	ret = regmap_field_write(drvdata->hdmi_tx_dmactl[0]->use_hw_usr, HW_MODE);
	if (ret)
		return ret;

	ret = regmap_field_write(drvdata->hdmi_tx_dmactl[0]->hw_usr_sel, SW_MODE);
	if (ret)
		return ret;

	ret = regmap_field_write(meta_ctl->mute, LPASS_MUTE_ENABLE);
	if (ret)
		return ret;

	ret = regmap_field_write(meta_ctl->as_sdp_cc, channels - 1);
	if (ret)
		return ret;

	ret = regmap_field_write(meta_ctl->as_sdp_ct, LPASS_META_DEFAULT_VAL);
	if (ret)
		return ret;

	ret = regmap_field_write(meta_ctl->aif_db4, LPASS_META_DEFAULT_VAL);
	if (ret)
		return ret;

	ret = regmap_field_write(meta_ctl->frequency, sampling_freq);
	if (ret)
		return ret;

	ret = regmap_field_write(meta_ctl->mst_index, LPASS_META_DEFAULT_VAL);
	if (ret)
		return ret;

	ret = regmap_field_write(meta_ctl->dptx_index, LPASS_META_DEFAULT_VAL);
	if (ret)
		return ret;

	ret = regmap_field_write(sstream_ctl->sstream_en, LPASS_SSTREAM_DISABLE);
	if (ret)
		return ret;

	ret = regmap_field_write(sstream_ctl->dma_sel, ch);
	if (ret)
		return ret;

	ret = regmap_field_write(sstream_ctl->auto_bbit_en, LPASS_SSTREAM_DEFAULT_ENABLE);
	if (ret)
		return ret;

	ret = regmap_field_write(sstream_ctl->layout, LPASS_SSTREAM_DEFAULT_DISABLE);
	if (ret)
		return ret;

	ret = regmap_field_write(sstream_ctl->layout_sp, LPASS_LAYOUT_SP_DEFAULT);
	if (ret)
		return ret;

	ret = regmap_field_write(sstream_ctl->dp_audio, LPASS_SSTREAM_DEFAULT_ENABLE);
	if (ret)
		return ret;

	ret = regmap_field_write(sstream_ctl->set_sp_on_en, LPASS_SSTREAM_DEFAULT_ENABLE);
	if (ret)
		return ret;

	ret = regmap_field_write(sstream_ctl->dp_sp_b_hw_en, LPASS_SSTREAM_DEFAULT_ENABLE);
	if (ret)
		return ret;

	ret = regmap_field_write(sstream_ctl->dp_staffing_en, LPASS_SSTREAM_DEFAULT_ENABLE);

	return ret;
}

static int lpass_hdmi_daiops_prepare(struct snd_pcm_substream *substream,
		struct snd_soc_dai *dai)
{
	int ret;
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);

	ret = regmap_field_write(drvdata->sstream_ctl->sstream_en, LPASS_SSTREAM_ENABLE);
	if (ret)
		return ret;

	ret = regmap_field_write(drvdata->meta_ctl->mute, LPASS_MUTE_DISABLE);

	return ret;
}

static int lpass_hdmi_daiops_trigger(struct snd_pcm_substream *substream,
		int cmd, struct snd_soc_dai *dai)
{
	struct lpass_data *drvdata = snd_soc_dai_get_drvdata(dai);
	struct lpass_dp_metadata_ctl *meta_ctl = drvdata->meta_ctl;
	struct lpass_sstream_ctl *sstream_ctl = drvdata->sstream_ctl;
	int ret = -EINVAL;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		ret = regmap_field_write(sstream_ctl->sstream_en, LPASS_SSTREAM_ENABLE);
		if (ret)
			return ret;

		ret = regmap_field_write(meta_ctl->mute, LPASS_MUTE_DISABLE);
		if (ret)
			return ret;

		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		ret = regmap_field_write(sstream_ctl->sstream_en, LPASS_SSTREAM_DISABLE);
		if (ret)
			return ret;

		ret = regmap_field_write(meta_ctl->mute, LPASS_MUTE_ENABLE);
		if (ret)
			return ret;

		ret = regmap_field_write(sstream_ctl->dp_audio, 0);
		if (ret)
			return ret;

		break;
	}
	return ret;
}

const struct snd_soc_dai_ops asoc_qcom_lpass_hdmi_dai_ops = {
	.hw_params	= lpass_hdmi_daiops_hw_params,
	.prepare	= lpass_hdmi_daiops_prepare,
	.trigger	= lpass_hdmi_daiops_trigger,
};
EXPORT_SYMBOL_GPL(asoc_qcom_lpass_hdmi_dai_ops);

MODULE_DESCRIPTION("QTi LPASS HDMI Driver");
MODULE_LICENSE("GPL");
