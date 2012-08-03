/*
 * Copyright (C) ST-Ericsson SA 2012
 *
 * Author: Ola Lilja <ola.o.lilja@stericsson.com>,
 *         Roger Nilsson <roger.xr.nilsson@stericsson.com>
 *         for ST-Ericsson.
 *
 * License terms:
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>
#include <linux/mfd/dbx500-prcmu.h>

#include <mach/hardware.h>
#include <mach/msp.h>

#include <sound/soc.h>
#include <sound/soc-dai.h>

#include "ux500_msp_i2s.h"
#include "ux500_msp_dai.h"

static int setup_pcm_multichan(struct snd_soc_dai *dai,
			struct ux500_msp_config *msp_config)
{
	struct ux500_msp_i2s_drvdata *drvdata = dev_get_drvdata(dai->dev);
	struct msp_multichannel_config *multi =
					&msp_config->multichannel_config;

	if (drvdata->slots > 1) {
		msp_config->multichannel_configured = 1;

		multi->tx_multichannel_enable = true;
		multi->rx_multichannel_enable = true;
		multi->rx_comparison_enable_mode = MSP_COMPARISON_DISABLED;

		multi->tx_channel_0_enable = drvdata->tx_mask;
		multi->tx_channel_1_enable = 0;
		multi->tx_channel_2_enable = 0;
		multi->tx_channel_3_enable = 0;

		multi->rx_channel_0_enable = drvdata->rx_mask;
		multi->rx_channel_1_enable = 0;
		multi->rx_channel_2_enable = 0;
		multi->rx_channel_3_enable = 0;

		dev_dbg(dai->dev,
			"%s: Multichannel enabled. Slots: %d, TX: %u, RX: %u\n",
			__func__, drvdata->slots, multi->tx_channel_0_enable,
			multi->rx_channel_0_enable);
	}

	return 0;
}

static int setup_frameper(struct snd_soc_dai *dai, unsigned int rate,
			struct msp_protdesc *prot_desc)
{
	struct ux500_msp_i2s_drvdata *drvdata = dev_get_drvdata(dai->dev);

	switch (drvdata->slots) {
	case 1:
		switch (rate) {
		case 8000:
			prot_desc->frame_period =
				FRAME_PER_SINGLE_SLOT_8_KHZ;
			break;

		case 16000:
			prot_desc->frame_period =
				FRAME_PER_SINGLE_SLOT_16_KHZ;
			break;

		case 44100:
			prot_desc->frame_period =
				FRAME_PER_SINGLE_SLOT_44_1_KHZ;
			break;

		case 48000:
			prot_desc->frame_period =
				FRAME_PER_SINGLE_SLOT_48_KHZ;
			break;

		default:
			dev_err(dai->dev,
				"%s: Error: Unsupported sample-rate (freq = %d)!\n",
				__func__, rate);
			return -EINVAL;
		}
		break;

	case 2:
		prot_desc->frame_period = FRAME_PER_2_SLOTS;
		break;

	case 8:
		prot_desc->frame_period = FRAME_PER_8_SLOTS;
		break;

	case 16:
		prot_desc->frame_period = FRAME_PER_16_SLOTS;
		break;
	default:
		dev_err(dai->dev,
			"%s: Error: Unsupported slot-count (slots = %d)!\n",
			__func__, drvdata->slots);
		return -EINVAL;
	}

	prot_desc->clocks_per_frame =
			prot_desc->frame_period+1;

	dev_dbg(dai->dev, "%s: Clocks per frame: %u\n",
		__func__,
		prot_desc->clocks_per_frame);

	return 0;
}

static int setup_pcm_framing(struct snd_soc_dai *dai, unsigned int rate,
			struct msp_protdesc *prot_desc)
{
	struct ux500_msp_i2s_drvdata *drvdata = dev_get_drvdata(dai->dev);

	u32 frame_length = MSP_FRAME_LEN_1;
	prot_desc->frame_width = 0;

	switch (drvdata->slots) {
	case 1:
		frame_length = MSP_FRAME_LEN_1;
		break;

	case 2:
		frame_length = MSP_FRAME_LEN_2;
		break;

	case 8:
		frame_length = MSP_FRAME_LEN_8;
		break;

	case 16:
		frame_length = MSP_FRAME_LEN_16;
		break;
	default:
		dev_err(dai->dev,
			"%s: Error: Unsupported slot-count (slots = %d)!\n",
			__func__, drvdata->slots);
		return -EINVAL;
	}

	prot_desc->tx_frame_len_1 = frame_length;
	prot_desc->rx_frame_len_1 = frame_length;
	prot_desc->tx_frame_len_2 = frame_length;
	prot_desc->rx_frame_len_2 = frame_length;

	prot_desc->tx_elem_len_1 = MSP_ELEM_LEN_16;
	prot_desc->rx_elem_len_1 = MSP_ELEM_LEN_16;
	prot_desc->tx_elem_len_2 = MSP_ELEM_LEN_16;
	prot_desc->rx_elem_len_2 = MSP_ELEM_LEN_16;

	return setup_frameper(dai, rate, prot_desc);
}

static int setup_clocking(struct snd_soc_dai *dai,
			unsigned int fmt,
			struct ux500_msp_config *msp_config)
{
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;

	case SND_SOC_DAIFMT_NB_IF:
		msp_config->tx_fsync_pol ^= 1 << TFSPOL_SHIFT;
		msp_config->rx_fsync_pol ^= 1 << RFSPOL_SHIFT;

		break;

	default:
		dev_err(dai->dev,
			"%s: Error: Unsopported inversion (fmt = 0x%x)!\n",
			__func__, fmt);

		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		dev_dbg(dai->dev, "%s: Codec is master.\n", __func__);

		msp_config->iodelay = 0x20;
		msp_config->rx_fsync_sel = 0;
		msp_config->tx_fsync_sel = 1 << TFSSEL_SHIFT;
		msp_config->tx_clk_sel = 0;
		msp_config->rx_clk_sel = 0;
		msp_config->srg_clk_sel = 0x2 << SCKSEL_SHIFT;

		break;

	case SND_SOC_DAIFMT_CBS_CFS:
		dev_dbg(dai->dev, "%s: Codec is slave.\n", __func__);

		msp_config->tx_clk_sel = TX_CLK_SEL_SRG;
		msp_config->tx_fsync_sel = TX_SYNC_SRG_PROG;
		msp_config->rx_clk_sel = RX_CLK_SEL_SRG;
		msp_config->rx_fsync_sel = RX_SYNC_SRG;
		msp_config->srg_clk_sel = 1 << SCKSEL_SHIFT;

		break;

	default:
		dev_err(dai->dev, "%s: Error: Unsopported master (fmt = 0x%x)!\n",
			__func__, fmt);

		return -EINVAL;
	}

	return 0;
}

static int setup_pcm_protdesc(struct snd_soc_dai *dai,
				unsigned int fmt,
				struct msp_protdesc *prot_desc)
{
	prot_desc->rx_phase_mode = MSP_SINGLE_PHASE;
	prot_desc->tx_phase_mode = MSP_SINGLE_PHASE;
	prot_desc->rx_phase2_start_mode = MSP_PHASE2_START_MODE_IMEDIATE;
	prot_desc->tx_phase2_start_mode = MSP_PHASE2_START_MODE_IMEDIATE;
	prot_desc->rx_byte_order = MSP_BTF_MS_BIT_FIRST;
	prot_desc->tx_byte_order = MSP_BTF_MS_BIT_FIRST;
	prot_desc->tx_fsync_pol = MSP_FSYNC_POL(MSP_FSYNC_POL_ACT_HI);
	prot_desc->rx_fsync_pol = MSP_FSYNC_POL_ACT_HI << RFSPOL_SHIFT;

	if ((fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_DSP_A) {
		dev_dbg(dai->dev, "%s: DSP_A.\n", __func__);
		prot_desc->rx_clk_pol = MSP_RISING_EDGE;
		prot_desc->tx_clk_pol = MSP_FALLING_EDGE;

		prot_desc->rx_data_delay = MSP_DELAY_1;
		prot_desc->tx_data_delay = MSP_DELAY_1;
	} else {
		dev_dbg(dai->dev, "%s: DSP_B.\n", __func__);
		prot_desc->rx_clk_pol = MSP_FALLING_EDGE;
		prot_desc->tx_clk_pol = MSP_RISING_EDGE;

		prot_desc->rx_data_delay = MSP_DELAY_0;
		prot_desc->tx_data_delay = MSP_DELAY_0;
	}

	prot_desc->rx_half_word_swap = MSP_SWAP_NONE;
	prot_desc->tx_half_word_swap = MSP_SWAP_NONE;
	prot_desc->compression_mode = MSP_COMPRESS_MODE_LINEAR;
	prot_desc->expansion_mode = MSP_EXPAND_MODE_LINEAR;
	prot_desc->frame_sync_ignore = MSP_FSYNC_IGNORE;

	return 0;
}

static int setup_i2s_protdesc(struct msp_protdesc *prot_desc)
{
	prot_desc->rx_phase_mode = MSP_DUAL_PHASE;
	prot_desc->tx_phase_mode = MSP_DUAL_PHASE;
	prot_desc->rx_phase2_start_mode = MSP_PHASE2_START_MODE_FSYNC;
	prot_desc->tx_phase2_start_mode = MSP_PHASE2_START_MODE_FSYNC;
	prot_desc->rx_byte_order = MSP_BTF_MS_BIT_FIRST;
	prot_desc->tx_byte_order = MSP_BTF_MS_BIT_FIRST;
	prot_desc->tx_fsync_pol = MSP_FSYNC_POL(MSP_FSYNC_POL_ACT_LO);
	prot_desc->rx_fsync_pol = MSP_FSYNC_POL_ACT_LO << RFSPOL_SHIFT;

	prot_desc->rx_frame_len_1 = MSP_FRAME_LEN_1;
	prot_desc->rx_frame_len_2 = MSP_FRAME_LEN_1;
	prot_desc->tx_frame_len_1 = MSP_FRAME_LEN_1;
	prot_desc->tx_frame_len_2 = MSP_FRAME_LEN_1;
	prot_desc->rx_elem_len_1 = MSP_ELEM_LEN_16;
	prot_desc->rx_elem_len_2 = MSP_ELEM_LEN_16;
	prot_desc->tx_elem_len_1 = MSP_ELEM_LEN_16;
	prot_desc->tx_elem_len_2 = MSP_ELEM_LEN_16;

	prot_desc->rx_clk_pol = MSP_RISING_EDGE;
	prot_desc->tx_clk_pol = MSP_FALLING_EDGE;

	prot_desc->rx_data_delay = MSP_DELAY_0;
	prot_desc->tx_data_delay = MSP_DELAY_0;

	prot_desc->tx_half_word_swap = MSP_SWAP_NONE;
	prot_desc->rx_half_word_swap = MSP_SWAP_NONE;
	prot_desc->compression_mode = MSP_COMPRESS_MODE_LINEAR;
	prot_desc->expansion_mode = MSP_EXPAND_MODE_LINEAR;
	prot_desc->frame_sync_ignore = MSP_FSYNC_IGNORE;

	return 0;
}

static int setup_msp_config(struct snd_pcm_substream *substream,
			struct snd_soc_dai *dai,
			struct ux500_msp_config *msp_config)
{
	struct ux500_msp_i2s_drvdata *drvdata = dev_get_drvdata(dai->dev);
	struct msp_protdesc *prot_desc = &msp_config->protdesc;
	struct snd_pcm_runtime *runtime = substream->runtime;
	unsigned int fmt = drvdata->fmt;
	int ret;

	memset(msp_config, 0, sizeof(*msp_config));

	msp_config->f_inputclk = drvdata->master_clk;

	msp_config->tx_fifo_config = TX_FIFO_ENABLE;
	msp_config->rx_fifo_config = RX_FIFO_ENABLE;
	msp_config->def_elem_len = 1;
	msp_config->direction = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
				MSP_DIR_TX : MSP_DIR_RX;
	msp_config->data_size = MSP_DATA_BITS_32;
	msp_config->frame_freq = runtime->rate;

	dev_dbg(dai->dev, "%s: f_inputclk = %u, frame_freq = %u.\n",
	       __func__, msp_config->f_inputclk, msp_config->frame_freq);
	/* To avoid division by zero */
	prot_desc->clocks_per_frame = 1;

	dev_dbg(dai->dev, "%s: rate: %u, channels: %d.\n", __func__,
		runtime->rate, runtime->channels);
	switch (fmt &
		(SND_SOC_DAIFMT_FORMAT_MASK | SND_SOC_DAIFMT_MASTER_MASK)) {
	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS:
		dev_dbg(dai->dev, "%s: SND_SOC_DAIFMT_I2S.\n", __func__);

		msp_config->default_protdesc = 1;
		msp_config->protocol = MSP_I2S_PROTOCOL;
		break;

	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBM_CFM:
		dev_dbg(dai->dev, "%s: SND_SOC_DAIFMT_I2S.\n", __func__);

		msp_config->data_size = MSP_DATA_BITS_16;
		msp_config->protocol = MSP_I2S_PROTOCOL;

		ret = setup_i2s_protdesc(prot_desc);
		if (ret < 0)
			return ret;

		break;

	case SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBS_CFS:
	case SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBM_CFM:
	case SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBS_CFS:
	case SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBM_CFM:
		dev_dbg(dai->dev, "%s: PCM format.\n", __func__);

		msp_config->data_size = MSP_DATA_BITS_16;
		msp_config->protocol = MSP_PCM_PROTOCOL;

		ret = setup_pcm_protdesc(dai, fmt, prot_desc);
		if (ret < 0)
			return ret;

		ret = setup_pcm_multichan(dai, msp_config);
		if (ret < 0)
			return ret;

		ret = setup_pcm_framing(dai, runtime->rate, prot_desc);
		if (ret < 0)
			return ret;

		break;

	default:
		dev_err(dai->dev, "%s: Error: Unsopported format (%d)!\n",
			__func__, fmt);
		return -EINVAL;
	}

	return setup_clocking(dai, fmt, msp_config);
}

static int ux500_msp_dai_startup(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	int ret = 0;
	struct ux500_msp_i2s_drvdata *drvdata = dev_get_drvdata(dai->dev);

	dev_dbg(dai->dev, "%s: MSP %d (%s): Enter.\n", __func__, dai->id,
		snd_pcm_stream_str(substream));

	/* Enable regulator */
	ret = regulator_enable(drvdata->reg_vape);
	if (ret != 0) {
		dev_err(drvdata->msp->dev,
			"%s: Failed to enable regulator!\n", __func__);
		return ret;
	}

	/* Enable clock */
	dev_dbg(dai->dev, "%s: Enabling MSP-clock.\n", __func__);
	clk_enable(drvdata->clk);

	return 0;
}

static void ux500_msp_dai_shutdown(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	int ret;
	struct ux500_msp_i2s_drvdata *drvdata = dev_get_drvdata(dai->dev);
	bool is_playback = (substream->stream == SNDRV_PCM_STREAM_PLAYBACK);

	dev_dbg(dai->dev, "%s: MSP %d (%s): Enter.\n", __func__, dai->id,
		snd_pcm_stream_str(substream));

	if (drvdata->vape_opp_constraint == 1) {
		prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP,
					"ux500_msp_i2s", 50);
		drvdata->vape_opp_constraint = 0;
	}

	if (ux500_msp_i2s_close(drvdata->msp,
				is_playback ? MSP_DIR_TX : MSP_DIR_RX)) {
		dev_err(dai->dev,
			"%s: Error: MSP %d (%s): Unable to close i2s.\n",
			__func__, dai->id, snd_pcm_stream_str(substream));
	}

	/* Disable clock */
	clk_disable(drvdata->clk);

	/* Disable regulator */
	ret = regulator_disable(drvdata->reg_vape);
	if (ret < 0)
		dev_err(dai->dev,
			"%s: ERROR: Failed to disable regulator (%d)!\n",
			__func__, ret);
}

static int ux500_msp_dai_prepare(struct snd_pcm_substream *substream,
				struct snd_soc_dai *dai)
{
	int ret = 0;
	struct ux500_msp_i2s_drvdata *drvdata = dev_get_drvdata(dai->dev);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ux500_msp_config msp_config;

	dev_dbg(dai->dev, "%s: MSP %d (%s): Enter (rate = %d).\n", __func__,
		dai->id, snd_pcm_stream_str(substream), runtime->rate);

	setup_msp_config(substream, dai, &msp_config);

	ret = ux500_msp_i2s_open(drvdata->msp, &msp_config);
	if (ret < 0) {
		dev_err(dai->dev, "%s: Error: msp_setup failed (ret = %d)!\n",
			__func__, ret);
		return ret;
	}

	/* Set OPP-level */
	if ((drvdata->fmt & SND_SOC_DAIFMT_MASTER_MASK) &&
		(drvdata->msp->f_bitclk > 19200000)) {
		/* If the bit-clock is higher than 19.2MHz, Vape should be
		 * run in 100% OPP. Only when bit-clock is used (MSP master) */
		prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP,
					"ux500-msp-i2s", 100);
		drvdata->vape_opp_constraint = 1;
	} else {
		prcmu_qos_update_requirement(PRCMU_QOS_APE_OPP,
					"ux500-msp-i2s", 50);
		drvdata->vape_opp_constraint = 0;
	}

	return ret;
}

static int ux500_msp_dai_hw_params(struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params,
				struct snd_soc_dai *dai)
{
	unsigned int mask, slots_active;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct ux500_msp_i2s_drvdata *drvdata = dev_get_drvdata(dai->dev);

	dev_dbg(dai->dev, "%s: MSP %d (%s): Enter.\n",
			__func__, dai->id, snd_pcm_stream_str(substream));

	switch (drvdata->fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		snd_pcm_hw_constraint_minmax(runtime,
				SNDRV_PCM_HW_PARAM_CHANNELS,
				1, 2);
		break;

	case SND_SOC_DAIFMT_DSP_B:
	case SND_SOC_DAIFMT_DSP_A:
		mask = substream->stream == SNDRV_PCM_STREAM_PLAYBACK ?
			drvdata->tx_mask :
			drvdata->rx_mask;

		slots_active = hweight32(mask);
		dev_dbg(dai->dev, "TDM-slots active: %d", slots_active);

		snd_pcm_hw_constraint_minmax(runtime,
				SNDRV_PCM_HW_PARAM_CHANNELS,
				slots_active, slots_active);
		break;

	default:
		dev_err(dai->dev,
			"%s: Error: Unsupported protocol (fmt = 0x%x)!\n",
			__func__, drvdata->fmt);
		return -EINVAL;
	}

	return 0;
}

static int ux500_msp_dai_set_dai_fmt(struct snd_soc_dai *dai,
				unsigned int fmt)
{
	struct ux500_msp_i2s_drvdata *drvdata = dev_get_drvdata(dai->dev);

	dev_dbg(dai->dev, "%s: MSP %d: Enter.\n", __func__, dai->id);

	switch (fmt & (SND_SOC_DAIFMT_FORMAT_MASK |
		SND_SOC_DAIFMT_MASTER_MASK)) {
	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBS_CFS:
	case SND_SOC_DAIFMT_I2S | SND_SOC_DAIFMT_CBM_CFM:
	case SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBS_CFS:
	case SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBM_CFM:
	case SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBS_CFS:
	case SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_CBM_CFM:
		break;

	default:
		dev_err(dai->dev,
			"%s: Error: Unsupported protocol/master (fmt = 0x%x)!\n",
			__func__, drvdata->fmt);
		return -EINVAL;
	}

	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
	case SND_SOC_DAIFMT_NB_IF:
	case SND_SOC_DAIFMT_IB_IF:
		break;

	default:
		dev_err(dai->dev,
			"%s: Error: Unsupported inversion (fmt = 0x%x)!\n",
			__func__, drvdata->fmt);
		return -EINVAL;
	}

	drvdata->fmt = fmt;
	return 0;
}

static int ux500_msp_dai_set_tdm_slot(struct snd_soc_dai *dai,
				unsigned int tx_mask,
				unsigned int rx_mask,
				int slots, int slot_width)
{
	struct ux500_msp_i2s_drvdata *drvdata = dev_get_drvdata(dai->dev);
	unsigned int cap;

	switch (slots) {
	case 1:
		cap = 0x01;
		break;
	case 2:
		cap = 0x03;
		break;
	case 8:
		cap = 0xFF;
		break;
	case 16:
		cap = 0xFFFF;
		break;
	default:
		dev_err(dai->dev, "%s: Error: Unsupported slot-count (%d)!\n",
			__func__, slots);
		return -EINVAL;
	}
	drvdata->slots = slots;

	if (!(slot_width == 16)) {
		dev_err(dai->dev, "%s: Error: Unsupported slot-width (%d)!\n",
			__func__, slot_width);
		return -EINVAL;
	}
	drvdata->slot_width = slot_width;

	drvdata->tx_mask = tx_mask & cap;
	drvdata->rx_mask = rx_mask & cap;

	return 0;
}

static int ux500_msp_dai_set_dai_sysclk(struct snd_soc_dai *dai,
					int clk_id, unsigned int freq, int dir)
{
	struct ux500_msp_i2s_drvdata *drvdata = dev_get_drvdata(dai->dev);

	dev_dbg(dai->dev, "%s: MSP %d: Enter. clk-id: %d, freq: %u.\n",
		__func__, dai->id, clk_id, freq);

	switch (clk_id) {
	case UX500_MSP_MASTER_CLOCK:
		drvdata->master_clk = freq;
		break;

	default:
		dev_err(dai->dev, "%s: MSP %d: Invalid clk-id (%d)!\n",
			__func__, dai->id, clk_id);
		return -EINVAL;
	}

	return 0;
}

static int ux500_msp_dai_trigger(struct snd_pcm_substream *substream,
				int cmd, struct snd_soc_dai *dai)
{
	int ret = 0;
	struct ux500_msp_i2s_drvdata *drvdata = dev_get_drvdata(dai->dev);

	dev_dbg(dai->dev, "%s: MSP %d (%s): Enter (msp->id = %d, cmd = %d).\n",
		__func__, dai->id, snd_pcm_stream_str(substream),
		(int)drvdata->msp->id, cmd);

	ret = ux500_msp_i2s_trigger(drvdata->msp, cmd, substream->stream);

	return ret;
}

static int ux500_msp_dai_probe(struct snd_soc_dai *dai)
{
	struct ux500_msp_i2s_drvdata *drvdata = dev_get_drvdata(dai->dev);

	drvdata->playback_dma_data.dma_cfg = drvdata->msp->dma_cfg_tx;
	drvdata->capture_dma_data.dma_cfg = drvdata->msp->dma_cfg_rx;

	dai->playback_dma_data = &drvdata->playback_dma_data;
	dai->capture_dma_data = &drvdata->capture_dma_data;

	drvdata->playback_dma_data.data_size = drvdata->slot_width;
	drvdata->capture_dma_data.data_size = drvdata->slot_width;

	return 0;
}

static struct snd_soc_dai_ops ux500_msp_dai_ops[] = {
	{
		.set_sysclk = ux500_msp_dai_set_dai_sysclk,
		.set_fmt = ux500_msp_dai_set_dai_fmt,
		.set_tdm_slot = ux500_msp_dai_set_tdm_slot,
		.startup = ux500_msp_dai_startup,
		.shutdown = ux500_msp_dai_shutdown,
		.prepare = ux500_msp_dai_prepare,
		.trigger = ux500_msp_dai_trigger,
		.hw_params = ux500_msp_dai_hw_params,
	}
};

static struct snd_soc_dai_driver ux500_msp_dai_drv[UX500_NBR_OF_DAI] = {
	{
		.name = "ux500-msp-i2s.0",
		.probe = ux500_msp_dai_probe,
		.id = 0,
		.suspend = NULL,
		.resume = NULL,
		.playback = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.capture = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.ops = ux500_msp_dai_ops,
	},
	{
		.name = "ux500-msp-i2s.1",
		.probe = ux500_msp_dai_probe,
		.id = 1,
		.suspend = NULL,
		.resume = NULL,
		.playback = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.capture = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.ops = ux500_msp_dai_ops,
	},
	{
		.name = "ux500-msp-i2s.2",
		.id = 2,
		.probe = ux500_msp_dai_probe,
		.suspend = NULL,
		.resume = NULL,
		.playback = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.capture = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.ops = ux500_msp_dai_ops,
	},
	{
		.name = "ux500-msp-i2s.3",
		.probe = ux500_msp_dai_probe,
		.id = 3,
		.suspend = NULL,
		.resume = NULL,
		.playback = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.capture = {
			.channels_min = UX500_MSP_MIN_CHANNELS,
			.channels_max = UX500_MSP_MAX_CHANNELS,
			.rates = UX500_I2S_RATES,
			.formats = UX500_I2S_FORMATS,
		},
		.ops = ux500_msp_dai_ops,
	},
};

static int __devinit ux500_msp_drv_probe(struct platform_device *pdev)
{
	struct ux500_msp_i2s_drvdata *drvdata;
	int ret = 0;

	dev_dbg(&pdev->dev, "%s: Enter (pdev->name = %s).\n", __func__,
		pdev->name);

	drvdata = devm_kzalloc(&pdev->dev,
				sizeof(struct ux500_msp_i2s_drvdata),
				GFP_KERNEL);
	drvdata->fmt = 0;
	drvdata->slots = 1;
	drvdata->tx_mask = 0x01;
	drvdata->rx_mask = 0x01;
	drvdata->slot_width = 16;
	drvdata->master_clk = MSP_INPUT_FREQ_APB;

	drvdata->reg_vape = devm_regulator_get(&pdev->dev, "v-ape");
	if (IS_ERR(drvdata->reg_vape)) {
		ret = (int)PTR_ERR(drvdata->reg_vape);
		dev_err(&pdev->dev,
			"%s: ERROR: Failed to get Vape supply (%d)!\n",
			__func__, ret);
		return ret;
	}
	prcmu_qos_add_requirement(PRCMU_QOS_APE_OPP, (char *)pdev->name, 50);

	drvdata->clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(drvdata->clk)) {
		ret = (int)PTR_ERR(drvdata->clk);
		dev_err(&pdev->dev, "%s: ERROR: clk_get failed (%d)!\n",
			__func__, ret);
		goto err_clk;
	}

	ret = ux500_msp_i2s_init_msp(pdev, &drvdata->msp,
				pdev->dev.platform_data);
	if (!drvdata->msp) {
		dev_err(&pdev->dev,
			"%s: ERROR: Failed to init MSP-struct (%d)!",
			__func__, ret);
		goto err_init_msp;
	}
	dev_set_drvdata(&pdev->dev, drvdata);

	ret = snd_soc_register_dai(&pdev->dev,
				&ux500_msp_dai_drv[drvdata->msp->id]);
	if (ret < 0) {
		dev_err(&pdev->dev, "Error: %s: Failed to register MSP%d!\n",
			__func__, drvdata->msp->id);
		goto err_init_msp;
	}

	return 0;

err_init_msp:
	clk_put(drvdata->clk);

err_clk:
	devm_regulator_put(drvdata->reg_vape);

	return ret;
}

static int __devexit ux500_msp_drv_remove(struct platform_device *pdev)
{
	struct ux500_msp_i2s_drvdata *drvdata = dev_get_drvdata(&pdev->dev);

	snd_soc_unregister_dais(&pdev->dev, ARRAY_SIZE(ux500_msp_dai_drv));

	devm_regulator_put(drvdata->reg_vape);
	prcmu_qos_remove_requirement(PRCMU_QOS_APE_OPP, "ux500_msp_i2s");

	clk_put(drvdata->clk);

	ux500_msp_i2s_cleanup_msp(pdev, drvdata->msp);

	return 0;
}

static struct platform_driver msp_i2s_driver = {
	.driver = {
		.name = "ux500-msp-i2s",
		.owner = THIS_MODULE,
	},
	.probe = ux500_msp_drv_probe,
	.remove = ux500_msp_drv_remove,
};
module_platform_driver(msp_i2s_driver);

MODULE_LICENSE("GPL v2");
