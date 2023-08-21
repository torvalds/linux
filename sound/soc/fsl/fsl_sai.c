// SPDX-License-Identifier: GPL-2.0+
//
// Freescale ALSA SoC Digital Audio Interface (SAI) driver.
//
// Copyright 2012-2015 Freescale Semiconductor, Inc.

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_qos.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>
#include <linux/mfd/syscon.h>
#include <linux/mfd/syscon/imx6q-iomuxc-gpr.h>

#include "fsl_sai.h"
#include "fsl_utils.h"
#include "imx-pcm.h"

#define FSL_SAI_FLAGS (FSL_SAI_CSR_SEIE |\
		       FSL_SAI_CSR_FEIE)

static const unsigned int fsl_sai_rates[] = {
	8000, 11025, 12000, 16000, 22050,
	24000, 32000, 44100, 48000, 64000,
	88200, 96000, 176400, 192000, 352800,
	384000, 705600, 768000, 1411200, 2822400,
};

static const struct snd_pcm_hw_constraint_list fsl_sai_rate_constraints = {
	.count = ARRAY_SIZE(fsl_sai_rates),
	.list = fsl_sai_rates,
};

/**
 * fsl_sai_dir_is_synced - Check if stream is synced by the opposite stream
 *
 * SAI supports synchronous mode using bit/frame clocks of either Transmitter's
 * or Receiver's for both streams. This function is used to check if clocks of
 * the stream's are synced by the opposite stream.
 *
 * @sai: SAI context
 * @dir: stream direction
 */
static inline bool fsl_sai_dir_is_synced(struct fsl_sai *sai, int dir)
{
	int adir = (dir == TX) ? RX : TX;

	/* current dir in async mode while opposite dir in sync mode */
	return !sai->synchronous[dir] && sai->synchronous[adir];
}

static struct pinctrl_state *fsl_sai_get_pins_state(struct fsl_sai *sai, u32 bclk)
{
	struct pinctrl_state *state = NULL;

	if (sai->is_pdm_mode) {
		/* DSD512@44.1kHz, DSD512@48kHz */
		if (bclk >= 22579200)
			state = pinctrl_lookup_state(sai->pinctrl, "dsd512");

		/* Get default DSD state */
		if (IS_ERR_OR_NULL(state))
			state = pinctrl_lookup_state(sai->pinctrl, "dsd");
	} else {
		/* 706k32b2c, 768k32b2c, etc */
		if (bclk >= 45158400)
			state = pinctrl_lookup_state(sai->pinctrl, "pcm_b2m");
	}

	/* Get default state */
	if (IS_ERR_OR_NULL(state))
		state = pinctrl_lookup_state(sai->pinctrl, "default");

	return state;
}

static irqreturn_t fsl_sai_isr(int irq, void *devid)
{
	struct fsl_sai *sai = (struct fsl_sai *)devid;
	unsigned int ofs = sai->soc_data->reg_offset;
	struct device *dev = &sai->pdev->dev;
	u32 flags, xcsr, mask;
	irqreturn_t iret = IRQ_NONE;

	/*
	 * Both IRQ status bits and IRQ mask bits are in the xCSR but
	 * different shifts. And we here create a mask only for those
	 * IRQs that we activated.
	 */
	mask = (FSL_SAI_FLAGS >> FSL_SAI_CSR_xIE_SHIFT) << FSL_SAI_CSR_xF_SHIFT;

	/* Tx IRQ */
	regmap_read(sai->regmap, FSL_SAI_TCSR(ofs), &xcsr);
	flags = xcsr & mask;

	if (flags)
		iret = IRQ_HANDLED;
	else
		goto irq_rx;

	if (flags & FSL_SAI_CSR_WSF)
		dev_dbg(dev, "isr: Start of Tx word detected\n");

	if (flags & FSL_SAI_CSR_SEF)
		dev_dbg(dev, "isr: Tx Frame sync error detected\n");

	if (flags & FSL_SAI_CSR_FEF)
		dev_dbg(dev, "isr: Transmit underrun detected\n");

	if (flags & FSL_SAI_CSR_FWF)
		dev_dbg(dev, "isr: Enabled transmit FIFO is empty\n");

	if (flags & FSL_SAI_CSR_FRF)
		dev_dbg(dev, "isr: Transmit FIFO watermark has been reached\n");

	flags &= FSL_SAI_CSR_xF_W_MASK;
	xcsr &= ~FSL_SAI_CSR_xF_MASK;

	if (flags)
		regmap_write(sai->regmap, FSL_SAI_TCSR(ofs), flags | xcsr);

irq_rx:
	/* Rx IRQ */
	regmap_read(sai->regmap, FSL_SAI_RCSR(ofs), &xcsr);
	flags = xcsr & mask;

	if (flags)
		iret = IRQ_HANDLED;
	else
		goto out;

	if (flags & FSL_SAI_CSR_WSF)
		dev_dbg(dev, "isr: Start of Rx word detected\n");

	if (flags & FSL_SAI_CSR_SEF)
		dev_dbg(dev, "isr: Rx Frame sync error detected\n");

	if (flags & FSL_SAI_CSR_FEF)
		dev_dbg(dev, "isr: Receive overflow detected\n");

	if (flags & FSL_SAI_CSR_FWF)
		dev_dbg(dev, "isr: Enabled receive FIFO is full\n");

	if (flags & FSL_SAI_CSR_FRF)
		dev_dbg(dev, "isr: Receive FIFO watermark has been reached\n");

	flags &= FSL_SAI_CSR_xF_W_MASK;
	xcsr &= ~FSL_SAI_CSR_xF_MASK;

	if (flags)
		regmap_write(sai->regmap, FSL_SAI_RCSR(ofs), flags | xcsr);

out:
	return iret;
}

static int fsl_sai_set_dai_tdm_slot(struct snd_soc_dai *cpu_dai, u32 tx_mask,
				u32 rx_mask, int slots, int slot_width)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);

	sai->slots = slots;
	sai->slot_width = slot_width;

	return 0;
}

static int fsl_sai_set_dai_bclk_ratio(struct snd_soc_dai *dai,
				      unsigned int ratio)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(dai);

	sai->bclk_ratio = ratio;

	return 0;
}

static int fsl_sai_set_dai_sysclk_tr(struct snd_soc_dai *cpu_dai,
		int clk_id, unsigned int freq, bool tx)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned int ofs = sai->soc_data->reg_offset;
	u32 val_cr2 = 0;

	switch (clk_id) {
	case FSL_SAI_CLK_BUS:
		val_cr2 |= FSL_SAI_CR2_MSEL_BUS;
		break;
	case FSL_SAI_CLK_MAST1:
		val_cr2 |= FSL_SAI_CR2_MSEL_MCLK1;
		break;
	case FSL_SAI_CLK_MAST2:
		val_cr2 |= FSL_SAI_CR2_MSEL_MCLK2;
		break;
	case FSL_SAI_CLK_MAST3:
		val_cr2 |= FSL_SAI_CR2_MSEL_MCLK3;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(sai->regmap, FSL_SAI_xCR2(tx, ofs),
			   FSL_SAI_CR2_MSEL_MASK, val_cr2);

	return 0;
}

static int fsl_sai_set_mclk_rate(struct snd_soc_dai *dai, int clk_id, unsigned int freq)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(dai);
	int ret;

	fsl_asoc_reparent_pll_clocks(dai->dev, sai->mclk_clk[clk_id],
				     sai->pll8k_clk, sai->pll11k_clk, freq);

	ret = clk_set_rate(sai->mclk_clk[clk_id], freq);
	if (ret < 0)
		dev_err(dai->dev, "failed to set clock rate (%u): %d\n", freq, ret);

	return ret;
}

static int fsl_sai_set_dai_sysclk(struct snd_soc_dai *cpu_dai,
		int clk_id, unsigned int freq, int dir)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int ret;

	if (dir == SND_SOC_CLOCK_IN)
		return 0;

	if (freq > 0 && clk_id != FSL_SAI_CLK_BUS) {
		if (clk_id < 0 || clk_id >= FSL_SAI_MCLK_MAX) {
			dev_err(cpu_dai->dev, "Unknown clock id: %d\n", clk_id);
			return -EINVAL;
		}

		if (IS_ERR_OR_NULL(sai->mclk_clk[clk_id])) {
			dev_err(cpu_dai->dev, "Unassigned clock: %d\n", clk_id);
			return -EINVAL;
		}

		if (sai->mclk_streams == 0) {
			ret = fsl_sai_set_mclk_rate(cpu_dai, clk_id, freq);
			if (ret < 0)
				return ret;
		}
	}

	ret = fsl_sai_set_dai_sysclk_tr(cpu_dai, clk_id, freq, true);
	if (ret) {
		dev_err(cpu_dai->dev, "Cannot set tx sysclk: %d\n", ret);
		return ret;
	}

	ret = fsl_sai_set_dai_sysclk_tr(cpu_dai, clk_id, freq, false);
	if (ret)
		dev_err(cpu_dai->dev, "Cannot set rx sysclk: %d\n", ret);

	return ret;
}

static int fsl_sai_set_dai_fmt_tr(struct snd_soc_dai *cpu_dai,
				unsigned int fmt, bool tx)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned int ofs = sai->soc_data->reg_offset;
	u32 val_cr2 = 0, val_cr4 = 0;

	if (!sai->is_lsb_first)
		val_cr4 |= FSL_SAI_CR4_MF;

	sai->is_pdm_mode = false;
	sai->is_dsp_mode = false;
	/* DAI mode */
	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	case SND_SOC_DAIFMT_I2S:
		/*
		 * Frame low, 1clk before data, one word length for frame sync,
		 * frame sync starts one serial clock cycle earlier,
		 * that is, together with the last bit of the previous
		 * data word.
		 */
		val_cr2 |= FSL_SAI_CR2_BCP;
		val_cr4 |= FSL_SAI_CR4_FSE | FSL_SAI_CR4_FSP;
		break;
	case SND_SOC_DAIFMT_LEFT_J:
		/*
		 * Frame high, one word length for frame sync,
		 * frame sync asserts with the first bit of the frame.
		 */
		val_cr2 |= FSL_SAI_CR2_BCP;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		/*
		 * Frame high, 1clk before data, one bit for frame sync,
		 * frame sync starts one serial clock cycle earlier,
		 * that is, together with the last bit of the previous
		 * data word.
		 */
		val_cr2 |= FSL_SAI_CR2_BCP;
		val_cr4 |= FSL_SAI_CR4_FSE;
		sai->is_dsp_mode = true;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		/*
		 * Frame high, one bit for frame sync,
		 * frame sync asserts with the first bit of the frame.
		 */
		val_cr2 |= FSL_SAI_CR2_BCP;
		sai->is_dsp_mode = true;
		break;
	case SND_SOC_DAIFMT_PDM:
		val_cr2 |= FSL_SAI_CR2_BCP;
		val_cr4 &= ~FSL_SAI_CR4_MF;
		sai->is_pdm_mode = true;
		break;
	case SND_SOC_DAIFMT_RIGHT_J:
		/* To be done */
	default:
		return -EINVAL;
	}

	/* DAI clock inversion */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_IB_IF:
		/* Invert both clocks */
		val_cr2 ^= FSL_SAI_CR2_BCP;
		val_cr4 ^= FSL_SAI_CR4_FSP;
		break;
	case SND_SOC_DAIFMT_IB_NF:
		/* Invert bit clock */
		val_cr2 ^= FSL_SAI_CR2_BCP;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		/* Invert frame clock */
		val_cr4 ^= FSL_SAI_CR4_FSP;
		break;
	case SND_SOC_DAIFMT_NB_NF:
		/* Nothing to do for both normal cases */
		break;
	default:
		return -EINVAL;
	}

	/* DAI clock provider masks */
	switch (fmt & SND_SOC_DAIFMT_CLOCK_PROVIDER_MASK) {
	case SND_SOC_DAIFMT_BP_FP:
		val_cr2 |= FSL_SAI_CR2_BCD_MSTR;
		val_cr4 |= FSL_SAI_CR4_FSD_MSTR;
		sai->is_consumer_mode = false;
		break;
	case SND_SOC_DAIFMT_BC_FC:
		sai->is_consumer_mode = true;
		break;
	case SND_SOC_DAIFMT_BP_FC:
		val_cr2 |= FSL_SAI_CR2_BCD_MSTR;
		sai->is_consumer_mode = false;
		break;
	case SND_SOC_DAIFMT_BC_FP:
		val_cr4 |= FSL_SAI_CR4_FSD_MSTR;
		sai->is_consumer_mode = true;
		break;
	default:
		return -EINVAL;
	}

	regmap_update_bits(sai->regmap, FSL_SAI_xCR2(tx, ofs),
			   FSL_SAI_CR2_BCP | FSL_SAI_CR2_BCD_MSTR, val_cr2);
	regmap_update_bits(sai->regmap, FSL_SAI_xCR4(tx, ofs),
			   FSL_SAI_CR4_MF | FSL_SAI_CR4_FSE |
			   FSL_SAI_CR4_FSP | FSL_SAI_CR4_FSD_MSTR, val_cr4);

	return 0;
}

static int fsl_sai_set_dai_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	int ret;

	ret = fsl_sai_set_dai_fmt_tr(cpu_dai, fmt, true);
	if (ret) {
		dev_err(cpu_dai->dev, "Cannot set tx format: %d\n", ret);
		return ret;
	}

	ret = fsl_sai_set_dai_fmt_tr(cpu_dai, fmt, false);
	if (ret)
		dev_err(cpu_dai->dev, "Cannot set rx format: %d\n", ret);

	return ret;
}

static int fsl_sai_set_bclk(struct snd_soc_dai *dai, bool tx, u32 freq)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(dai);
	unsigned int reg, ofs = sai->soc_data->reg_offset;
	unsigned long clk_rate;
	u32 savediv = 0, ratio, bestdiff = freq;
	int adir = tx ? RX : TX;
	int dir = tx ? TX : RX;
	u32 id;
	bool support_1_1_ratio = sai->verid.version >= 0x0301;

	/* Don't apply to consumer mode */
	if (sai->is_consumer_mode)
		return 0;

	/*
	 * There is no point in polling MCLK0 if it is identical to MCLK1.
	 * And given that MQS use case has to use MCLK1 though two clocks
	 * are the same, we simply skip MCLK0 and start to find from MCLK1.
	 */
	id = sai->soc_data->mclk0_is_mclk1 ? 1 : 0;

	for (; id < FSL_SAI_MCLK_MAX; id++) {
		int diff;

		clk_rate = clk_get_rate(sai->mclk_clk[id]);
		if (!clk_rate)
			continue;

		ratio = DIV_ROUND_CLOSEST(clk_rate, freq);
		if (!ratio || ratio > 512)
			continue;
		if (ratio == 1 && !support_1_1_ratio)
			continue;
		if ((ratio & 1) && ratio > 1)
			continue;

		diff = abs((long)clk_rate - ratio * freq);

		/*
		 * Drop the source that can not be
		 * divided into the required rate.
		 */
		if (diff != 0 && clk_rate / diff < 1000)
			continue;

		dev_dbg(dai->dev,
			"ratio %d for freq %dHz based on clock %ldHz\n",
			ratio, freq, clk_rate);


		if (diff < bestdiff) {
			savediv = ratio;
			sai->mclk_id[tx] = id;
			bestdiff = diff;
		}

		if (diff == 0)
			break;
	}

	if (savediv == 0) {
		dev_err(dai->dev, "failed to derive required %cx rate: %d\n",
				tx ? 'T' : 'R', freq);
		return -EINVAL;
	}

	dev_dbg(dai->dev, "best fit: clock id=%d, div=%d, deviation =%d\n",
			sai->mclk_id[tx], savediv, bestdiff);

	/*
	 * 1) For Asynchronous mode, we must set RCR2 register for capture, and
	 *    set TCR2 register for playback.
	 * 2) For Tx sync with Rx clock, we must set RCR2 register for playback
	 *    and capture.
	 * 3) For Rx sync with Tx clock, we must set TCR2 register for playback
	 *    and capture.
	 * 4) For Tx and Rx are both Synchronous with another SAI, we just
	 *    ignore it.
	 */
	if (fsl_sai_dir_is_synced(sai, adir))
		reg = FSL_SAI_xCR2(!tx, ofs);
	else if (!sai->synchronous[dir])
		reg = FSL_SAI_xCR2(tx, ofs);
	else
		return 0;

	regmap_update_bits(sai->regmap, reg, FSL_SAI_CR2_MSEL_MASK,
			   FSL_SAI_CR2_MSEL(sai->mclk_id[tx]));

	if (savediv == 1) {
		regmap_update_bits(sai->regmap, reg,
				   FSL_SAI_CR2_DIV_MASK | FSL_SAI_CR2_BYP,
				   FSL_SAI_CR2_BYP);
		if (fsl_sai_dir_is_synced(sai, adir))
			regmap_update_bits(sai->regmap, FSL_SAI_xCR2(tx, ofs),
					   FSL_SAI_CR2_BCI, FSL_SAI_CR2_BCI);
		else
			regmap_update_bits(sai->regmap, FSL_SAI_xCR2(tx, ofs),
					   FSL_SAI_CR2_BCI, 0);
	} else {
		regmap_update_bits(sai->regmap, reg,
				   FSL_SAI_CR2_DIV_MASK | FSL_SAI_CR2_BYP,
				   savediv / 2 - 1);
	}

	return 0;
}

static int fsl_sai_hw_params(struct snd_pcm_substream *substream,
		struct snd_pcm_hw_params *params,
		struct snd_soc_dai *cpu_dai)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned int ofs = sai->soc_data->reg_offset;
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	unsigned int channels = params_channels(params);
	struct snd_dmaengine_dai_dma_data *dma_params;
	struct fsl_sai_dl_cfg *dl_cfg = sai->dl_cfg;
	u32 word_width = params_width(params);
	int trce_mask = 0, dl_cfg_idx = 0;
	int dl_cfg_cnt = sai->dl_cfg_cnt;
	u32 dl_type = FSL_SAI_DL_I2S;
	u32 val_cr4 = 0, val_cr5 = 0;
	u32 slots = (channels == 1) ? 2 : channels;
	u32 slot_width = word_width;
	int adir = tx ? RX : TX;
	u32 pins, bclk;
	u32 watermark;
	int ret, i;

	if (sai->slot_width)
		slot_width = sai->slot_width;

	if (sai->slots)
		slots = sai->slots;
	else if (sai->bclk_ratio)
		slots = sai->bclk_ratio / slot_width;

	pins = DIV_ROUND_UP(channels, slots);

	/*
	 * PDM mode, channels are independent
	 * each channels are on one dataline/FIFO.
	 */
	if (sai->is_pdm_mode) {
		pins = channels;
		dl_type = FSL_SAI_DL_PDM;
	}

	for (i = 0; i < dl_cfg_cnt; i++) {
		if (dl_cfg[i].type == dl_type && dl_cfg[i].pins[tx] == pins) {
			dl_cfg_idx = i;
			break;
		}
	}

	if (hweight8(dl_cfg[dl_cfg_idx].mask[tx]) < pins) {
		dev_err(cpu_dai->dev, "channel not supported\n");
		return -EINVAL;
	}

	bclk = params_rate(params) * (sai->bclk_ratio ? sai->bclk_ratio : slots * slot_width);

	if (!IS_ERR_OR_NULL(sai->pinctrl)) {
		sai->pins_state = fsl_sai_get_pins_state(sai, bclk);
		if (!IS_ERR_OR_NULL(sai->pins_state)) {
			ret = pinctrl_select_state(sai->pinctrl, sai->pins_state);
			if (ret) {
				dev_err(cpu_dai->dev, "failed to set proper pins state: %d\n", ret);
				return ret;
			}
		}
	}

	if (!sai->is_consumer_mode) {
		ret = fsl_sai_set_bclk(cpu_dai, tx, bclk);
		if (ret)
			return ret;

		/* Do not enable the clock if it is already enabled */
		if (!(sai->mclk_streams & BIT(substream->stream))) {
			ret = clk_prepare_enable(sai->mclk_clk[sai->mclk_id[tx]]);
			if (ret)
				return ret;

			sai->mclk_streams |= BIT(substream->stream);
		}
	}

	if (!sai->is_dsp_mode && !sai->is_pdm_mode)
		val_cr4 |= FSL_SAI_CR4_SYWD(slot_width);

	val_cr5 |= FSL_SAI_CR5_WNW(slot_width);
	val_cr5 |= FSL_SAI_CR5_W0W(slot_width);

	if (sai->is_lsb_first || sai->is_pdm_mode)
		val_cr5 |= FSL_SAI_CR5_FBT(0);
	else
		val_cr5 |= FSL_SAI_CR5_FBT(word_width - 1);

	val_cr4 |= FSL_SAI_CR4_FRSZ(slots);

	/* Set to output mode to avoid tri-stated data pins */
	if (tx)
		val_cr4 |= FSL_SAI_CR4_CHMOD;

	/*
	 * For SAI provider mode, when Tx(Rx) sync with Rx(Tx) clock, Rx(Tx) will
	 * generate bclk and frame clock for Tx(Rx), we should set RCR4(TCR4),
	 * RCR5(TCR5) for playback(capture), or there will be sync error.
	 */

	if (!sai->is_consumer_mode && fsl_sai_dir_is_synced(sai, adir)) {
		regmap_update_bits(sai->regmap, FSL_SAI_xCR4(!tx, ofs),
				   FSL_SAI_CR4_SYWD_MASK | FSL_SAI_CR4_FRSZ_MASK |
				   FSL_SAI_CR4_CHMOD_MASK,
				   val_cr4);
		regmap_update_bits(sai->regmap, FSL_SAI_xCR5(!tx, ofs),
				   FSL_SAI_CR5_WNW_MASK | FSL_SAI_CR5_W0W_MASK |
				   FSL_SAI_CR5_FBT_MASK, val_cr5);
	}

	/*
	 * Combine mode has limation:
	 * - Can't used for singel dataline/FIFO case except the FIFO0
	 * - Can't used for multi dataline/FIFO case except the enabled FIFOs
	 *   are successive and start from FIFO0
	 *
	 * So for common usage, all multi fifo case disable the combine mode.
	 */
	if (hweight8(dl_cfg[dl_cfg_idx].mask[tx]) <= 1 || sai->is_multi_fifo_dma)
		regmap_update_bits(sai->regmap, FSL_SAI_xCR4(tx, ofs),
				   FSL_SAI_CR4_FCOMB_MASK, 0);
	else
		regmap_update_bits(sai->regmap, FSL_SAI_xCR4(tx, ofs),
				   FSL_SAI_CR4_FCOMB_MASK, FSL_SAI_CR4_FCOMB_SOFT);

	dma_params = tx ? &sai->dma_params_tx : &sai->dma_params_rx;
	dma_params->addr = sai->res->start + FSL_SAI_xDR0(tx) +
			   dl_cfg[dl_cfg_idx].start_off[tx] * 0x4;

	if (sai->is_multi_fifo_dma) {
		sai->audio_config[tx].words_per_fifo = min(slots, channels);
		if (tx) {
			sai->audio_config[tx].n_fifos_dst = pins;
			sai->audio_config[tx].stride_fifos_dst = dl_cfg[dl_cfg_idx].next_off[tx];
		} else {
			sai->audio_config[tx].n_fifos_src = pins;
			sai->audio_config[tx].stride_fifos_src = dl_cfg[dl_cfg_idx].next_off[tx];
		}
		dma_params->maxburst = sai->audio_config[tx].words_per_fifo * pins;
		dma_params->peripheral_config = &sai->audio_config[tx];
		dma_params->peripheral_size = sizeof(sai->audio_config[tx]);

		watermark = tx ? (sai->soc_data->fifo_depth - dma_params->maxburst) :
				 (dma_params->maxburst - 1);
		regmap_update_bits(sai->regmap, FSL_SAI_xCR1(tx, ofs),
				   FSL_SAI_CR1_RFW_MASK(sai->soc_data->fifo_depth),
				   watermark);
	}

	/* Find a proper tcre setting */
	for (i = 0; i < sai->soc_data->pins; i++) {
		trce_mask = (1 << (i + 1)) - 1;
		if (hweight8(dl_cfg[dl_cfg_idx].mask[tx] & trce_mask) == pins)
			break;
	}

	regmap_update_bits(sai->regmap, FSL_SAI_xCR3(tx, ofs),
			   FSL_SAI_CR3_TRCE_MASK,
			   FSL_SAI_CR3_TRCE((dl_cfg[dl_cfg_idx].mask[tx] & trce_mask)));

	regmap_update_bits(sai->regmap, FSL_SAI_xCR4(tx, ofs),
			   FSL_SAI_CR4_SYWD_MASK | FSL_SAI_CR4_FRSZ_MASK |
			   FSL_SAI_CR4_CHMOD_MASK,
			   val_cr4);
	regmap_update_bits(sai->regmap, FSL_SAI_xCR5(tx, ofs),
			   FSL_SAI_CR5_WNW_MASK | FSL_SAI_CR5_W0W_MASK |
			   FSL_SAI_CR5_FBT_MASK, val_cr5);
	regmap_write(sai->regmap, FSL_SAI_xMR(tx),
		     ~0UL - ((1 << min(channels, slots)) - 1));

	return 0;
}

static int fsl_sai_hw_free(struct snd_pcm_substream *substream,
		struct snd_soc_dai *cpu_dai)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	unsigned int ofs = sai->soc_data->reg_offset;

	regmap_update_bits(sai->regmap, FSL_SAI_xCR3(tx, ofs),
			   FSL_SAI_CR3_TRCE_MASK, 0);

	if (!sai->is_consumer_mode &&
			sai->mclk_streams & BIT(substream->stream)) {
		clk_disable_unprepare(sai->mclk_clk[sai->mclk_id[tx]]);
		sai->mclk_streams &= ~BIT(substream->stream);
	}

	return 0;
}

static void fsl_sai_config_disable(struct fsl_sai *sai, int dir)
{
	unsigned int ofs = sai->soc_data->reg_offset;
	bool tx = dir == TX;
	u32 xcsr, count = 100;

	regmap_update_bits(sai->regmap, FSL_SAI_xCSR(tx, ofs),
			   FSL_SAI_CSR_TERE | FSL_SAI_CSR_BCE, 0);

	/* TERE will remain set till the end of current frame */
	do {
		udelay(10);
		regmap_read(sai->regmap, FSL_SAI_xCSR(tx, ofs), &xcsr);
	} while (--count && xcsr & FSL_SAI_CSR_TERE);

	regmap_update_bits(sai->regmap, FSL_SAI_xCSR(tx, ofs),
			   FSL_SAI_CSR_FR, FSL_SAI_CSR_FR);

	/*
	 * For sai master mode, after several open/close sai,
	 * there will be no frame clock, and can't recover
	 * anymore. Add software reset to fix this issue.
	 * This is a hardware bug, and will be fix in the
	 * next sai version.
	 */
	if (!sai->is_consumer_mode) {
		/* Software Reset */
		regmap_write(sai->regmap, FSL_SAI_xCSR(tx, ofs), FSL_SAI_CSR_SR);
		/* Clear SR bit to finish the reset */
		regmap_write(sai->regmap, FSL_SAI_xCSR(tx, ofs), 0);
	}
}

static int fsl_sai_trigger(struct snd_pcm_substream *substream, int cmd,
		struct snd_soc_dai *cpu_dai)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	unsigned int ofs = sai->soc_data->reg_offset;

	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int adir = tx ? RX : TX;
	int dir = tx ? TX : RX;
	u32 xcsr;

	/*
	 * Asynchronous mode: Clear SYNC for both Tx and Rx.
	 * Rx sync with Tx clocks: Clear SYNC for Tx, set it for Rx.
	 * Tx sync with Rx clocks: Clear SYNC for Rx, set it for Tx.
	 */
	regmap_update_bits(sai->regmap, FSL_SAI_TCR2(ofs), FSL_SAI_CR2_SYNC,
			   sai->synchronous[TX] ? FSL_SAI_CR2_SYNC : 0);
	regmap_update_bits(sai->regmap, FSL_SAI_RCR2(ofs), FSL_SAI_CR2_SYNC,
			   sai->synchronous[RX] ? FSL_SAI_CR2_SYNC : 0);

	/*
	 * It is recommended that the transmitter is the last enabled
	 * and the first disabled.
	 */
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		regmap_update_bits(sai->regmap, FSL_SAI_xCSR(tx, ofs),
				   FSL_SAI_CSR_FRDE, FSL_SAI_CSR_FRDE);

		regmap_update_bits(sai->regmap, FSL_SAI_xCSR(tx, ofs),
				   FSL_SAI_CSR_TERE, FSL_SAI_CSR_TERE);
		/*
		 * Enable the opposite direction for synchronous mode
		 * 1. Tx sync with Rx: only set RE for Rx; set TE & RE for Tx
		 * 2. Rx sync with Tx: only set TE for Tx; set RE & TE for Rx
		 *
		 * RM recommends to enable RE after TE for case 1 and to enable
		 * TE after RE for case 2, but we here may not always guarantee
		 * that happens: "arecord 1.wav; aplay 2.wav" in case 1 enables
		 * TE after RE, which is against what RM recommends but should
		 * be safe to do, judging by years of testing results.
		 */
		if (fsl_sai_dir_is_synced(sai, adir))
			regmap_update_bits(sai->regmap, FSL_SAI_xCSR((!tx), ofs),
					   FSL_SAI_CSR_TERE, FSL_SAI_CSR_TERE);

		regmap_update_bits(sai->regmap, FSL_SAI_xCSR(tx, ofs),
				   FSL_SAI_CSR_xIE_MASK, FSL_SAI_FLAGS);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		regmap_update_bits(sai->regmap, FSL_SAI_xCSR(tx, ofs),
				   FSL_SAI_CSR_FRDE, 0);
		regmap_update_bits(sai->regmap, FSL_SAI_xCSR(tx, ofs),
				   FSL_SAI_CSR_xIE_MASK, 0);

		/* Check if the opposite FRDE is also disabled */
		regmap_read(sai->regmap, FSL_SAI_xCSR(!tx, ofs), &xcsr);

		/*
		 * If opposite stream provides clocks for synchronous mode and
		 * it is inactive, disable it before disabling the current one
		 */
		if (fsl_sai_dir_is_synced(sai, adir) && !(xcsr & FSL_SAI_CSR_FRDE))
			fsl_sai_config_disable(sai, adir);

		/*
		 * Disable current stream if either of:
		 * 1. current stream doesn't provide clocks for synchronous mode
		 * 2. current stream provides clocks for synchronous mode but no
		 *    more stream is active.
		 */
		if (!fsl_sai_dir_is_synced(sai, dir) || !(xcsr & FSL_SAI_CSR_FRDE))
			fsl_sai_config_disable(sai, dir);

		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int fsl_sai_startup(struct snd_pcm_substream *substream,
		struct snd_soc_dai *cpu_dai)
{
	struct fsl_sai *sai = snd_soc_dai_get_drvdata(cpu_dai);
	bool tx = substream->stream == SNDRV_PCM_STREAM_PLAYBACK;
	int ret;

	/*
	 * EDMA controller needs period size to be a multiple of
	 * tx/rx maxburst
	 */
	if (sai->soc_data->use_edma)
		snd_pcm_hw_constraint_step(substream->runtime, 0,
					   SNDRV_PCM_HW_PARAM_PERIOD_SIZE,
					   tx ? sai->dma_params_tx.maxburst :
					   sai->dma_params_rx.maxburst);

	ret = snd_pcm_hw_constraint_list(substream->runtime, 0,
			SNDRV_PCM_HW_PARAM_RATE, &fsl_sai_rate_constraints);

	return ret;
}

static const struct snd_soc_dai_ops fsl_sai_pcm_dai_ops = {
	.set_bclk_ratio	= fsl_sai_set_dai_bclk_ratio,
	.set_sysclk	= fsl_sai_set_dai_sysclk,
	.set_fmt	= fsl_sai_set_dai_fmt,
	.set_tdm_slot	= fsl_sai_set_dai_tdm_slot,
	.hw_params	= fsl_sai_hw_params,
	.hw_free	= fsl_sai_hw_free,
	.trigger	= fsl_sai_trigger,
	.startup	= fsl_sai_startup,
};

static int fsl_sai_dai_probe(struct snd_soc_dai *cpu_dai)
{
	struct fsl_sai *sai = dev_get_drvdata(cpu_dai->dev);
	unsigned int ofs = sai->soc_data->reg_offset;

	/* Software Reset for both Tx and Rx */
	regmap_write(sai->regmap, FSL_SAI_TCSR(ofs), FSL_SAI_CSR_SR);
	regmap_write(sai->regmap, FSL_SAI_RCSR(ofs), FSL_SAI_CSR_SR);
	/* Clear SR bit to finish the reset */
	regmap_write(sai->regmap, FSL_SAI_TCSR(ofs), 0);
	regmap_write(sai->regmap, FSL_SAI_RCSR(ofs), 0);

	regmap_update_bits(sai->regmap, FSL_SAI_TCR1(ofs),
			   FSL_SAI_CR1_RFW_MASK(sai->soc_data->fifo_depth),
			   sai->soc_data->fifo_depth - sai->dma_params_tx.maxburst);
	regmap_update_bits(sai->regmap, FSL_SAI_RCR1(ofs),
			   FSL_SAI_CR1_RFW_MASK(sai->soc_data->fifo_depth),
			   sai->dma_params_rx.maxburst - 1);

	snd_soc_dai_init_dma_data(cpu_dai, &sai->dma_params_tx,
				&sai->dma_params_rx);

	return 0;
}

static int fsl_sai_dai_resume(struct snd_soc_component *component)
{
	struct fsl_sai *sai = snd_soc_component_get_drvdata(component);
	struct device *dev = &sai->pdev->dev;
	int ret;

	if (!IS_ERR_OR_NULL(sai->pinctrl) && !IS_ERR_OR_NULL(sai->pins_state)) {
		ret = pinctrl_select_state(sai->pinctrl, sai->pins_state);
		if (ret) {
			dev_err(dev, "failed to set proper pins state: %d\n", ret);
			return ret;
		}
	}

	return 0;
}

static struct snd_soc_dai_driver fsl_sai_dai_template = {
	.probe = fsl_sai_dai_probe,
	.playback = {
		.stream_name = "CPU-Playback",
		.channels_min = 1,
		.channels_max = 32,
		.rate_min = 8000,
		.rate_max = 2822400,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = FSL_SAI_FORMATS,
	},
	.capture = {
		.stream_name = "CPU-Capture",
		.channels_min = 1,
		.channels_max = 32,
		.rate_min = 8000,
		.rate_max = 2822400,
		.rates = SNDRV_PCM_RATE_KNOT,
		.formats = FSL_SAI_FORMATS,
	},
	.ops = &fsl_sai_pcm_dai_ops,
};

static const struct snd_soc_component_driver fsl_component = {
	.name			= "fsl-sai",
	.resume			= fsl_sai_dai_resume,
	.legacy_dai_naming	= 1,
};

static struct reg_default fsl_sai_reg_defaults_ofs0[] = {
	{FSL_SAI_TCR1(0), 0},
	{FSL_SAI_TCR2(0), 0},
	{FSL_SAI_TCR3(0), 0},
	{FSL_SAI_TCR4(0), 0},
	{FSL_SAI_TCR5(0), 0},
	{FSL_SAI_TDR0, 0},
	{FSL_SAI_TDR1, 0},
	{FSL_SAI_TDR2, 0},
	{FSL_SAI_TDR3, 0},
	{FSL_SAI_TDR4, 0},
	{FSL_SAI_TDR5, 0},
	{FSL_SAI_TDR6, 0},
	{FSL_SAI_TDR7, 0},
	{FSL_SAI_TMR, 0},
	{FSL_SAI_RCR1(0), 0},
	{FSL_SAI_RCR2(0), 0},
	{FSL_SAI_RCR3(0), 0},
	{FSL_SAI_RCR4(0), 0},
	{FSL_SAI_RCR5(0), 0},
	{FSL_SAI_RMR, 0},
};

static struct reg_default fsl_sai_reg_defaults_ofs8[] = {
	{FSL_SAI_TCR1(8), 0},
	{FSL_SAI_TCR2(8), 0},
	{FSL_SAI_TCR3(8), 0},
	{FSL_SAI_TCR4(8), 0},
	{FSL_SAI_TCR5(8), 0},
	{FSL_SAI_TDR0, 0},
	{FSL_SAI_TDR1, 0},
	{FSL_SAI_TDR2, 0},
	{FSL_SAI_TDR3, 0},
	{FSL_SAI_TDR4, 0},
	{FSL_SAI_TDR5, 0},
	{FSL_SAI_TDR6, 0},
	{FSL_SAI_TDR7, 0},
	{FSL_SAI_TMR, 0},
	{FSL_SAI_RCR1(8), 0},
	{FSL_SAI_RCR2(8), 0},
	{FSL_SAI_RCR3(8), 0},
	{FSL_SAI_RCR4(8), 0},
	{FSL_SAI_RCR5(8), 0},
	{FSL_SAI_RMR, 0},
	{FSL_SAI_MCTL, 0},
	{FSL_SAI_MDIV, 0},
};

static bool fsl_sai_readable_reg(struct device *dev, unsigned int reg)
{
	struct fsl_sai *sai = dev_get_drvdata(dev);
	unsigned int ofs = sai->soc_data->reg_offset;

	if (reg >= FSL_SAI_TCSR(ofs) && reg <= FSL_SAI_TCR5(ofs))
		return true;

	if (reg >= FSL_SAI_RCSR(ofs) && reg <= FSL_SAI_RCR5(ofs))
		return true;

	switch (reg) {
	case FSL_SAI_TFR0:
	case FSL_SAI_TFR1:
	case FSL_SAI_TFR2:
	case FSL_SAI_TFR3:
	case FSL_SAI_TFR4:
	case FSL_SAI_TFR5:
	case FSL_SAI_TFR6:
	case FSL_SAI_TFR7:
	case FSL_SAI_TMR:
	case FSL_SAI_RDR0:
	case FSL_SAI_RDR1:
	case FSL_SAI_RDR2:
	case FSL_SAI_RDR3:
	case FSL_SAI_RDR4:
	case FSL_SAI_RDR5:
	case FSL_SAI_RDR6:
	case FSL_SAI_RDR7:
	case FSL_SAI_RFR0:
	case FSL_SAI_RFR1:
	case FSL_SAI_RFR2:
	case FSL_SAI_RFR3:
	case FSL_SAI_RFR4:
	case FSL_SAI_RFR5:
	case FSL_SAI_RFR6:
	case FSL_SAI_RFR7:
	case FSL_SAI_RMR:
	case FSL_SAI_MCTL:
	case FSL_SAI_MDIV:
	case FSL_SAI_VERID:
	case FSL_SAI_PARAM:
	case FSL_SAI_TTCTN:
	case FSL_SAI_RTCTN:
	case FSL_SAI_TTCTL:
	case FSL_SAI_TBCTN:
	case FSL_SAI_TTCAP:
	case FSL_SAI_RTCTL:
	case FSL_SAI_RBCTN:
	case FSL_SAI_RTCAP:
		return true;
	default:
		return false;
	}
}

static bool fsl_sai_volatile_reg(struct device *dev, unsigned int reg)
{
	struct fsl_sai *sai = dev_get_drvdata(dev);
	unsigned int ofs = sai->soc_data->reg_offset;

	if (reg == FSL_SAI_TCSR(ofs) || reg == FSL_SAI_RCSR(ofs))
		return true;

	/* Set VERID and PARAM be volatile for reading value in probe */
	if (ofs == 8 && (reg == FSL_SAI_VERID || reg == FSL_SAI_PARAM))
		return true;

	switch (reg) {
	case FSL_SAI_TFR0:
	case FSL_SAI_TFR1:
	case FSL_SAI_TFR2:
	case FSL_SAI_TFR3:
	case FSL_SAI_TFR4:
	case FSL_SAI_TFR5:
	case FSL_SAI_TFR6:
	case FSL_SAI_TFR7:
	case FSL_SAI_RFR0:
	case FSL_SAI_RFR1:
	case FSL_SAI_RFR2:
	case FSL_SAI_RFR3:
	case FSL_SAI_RFR4:
	case FSL_SAI_RFR5:
	case FSL_SAI_RFR6:
	case FSL_SAI_RFR7:
	case FSL_SAI_RDR0:
	case FSL_SAI_RDR1:
	case FSL_SAI_RDR2:
	case FSL_SAI_RDR3:
	case FSL_SAI_RDR4:
	case FSL_SAI_RDR5:
	case FSL_SAI_RDR6:
	case FSL_SAI_RDR7:
		return true;
	default:
		return false;
	}
}

static bool fsl_sai_writeable_reg(struct device *dev, unsigned int reg)
{
	struct fsl_sai *sai = dev_get_drvdata(dev);
	unsigned int ofs = sai->soc_data->reg_offset;

	if (reg >= FSL_SAI_TCSR(ofs) && reg <= FSL_SAI_TCR5(ofs))
		return true;

	if (reg >= FSL_SAI_RCSR(ofs) && reg <= FSL_SAI_RCR5(ofs))
		return true;

	switch (reg) {
	case FSL_SAI_TDR0:
	case FSL_SAI_TDR1:
	case FSL_SAI_TDR2:
	case FSL_SAI_TDR3:
	case FSL_SAI_TDR4:
	case FSL_SAI_TDR5:
	case FSL_SAI_TDR6:
	case FSL_SAI_TDR7:
	case FSL_SAI_TMR:
	case FSL_SAI_RMR:
	case FSL_SAI_MCTL:
	case FSL_SAI_MDIV:
	case FSL_SAI_TTCTL:
	case FSL_SAI_RTCTL:
		return true;
	default:
		return false;
	}
}

static struct regmap_config fsl_sai_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.fast_io = true,

	.max_register = FSL_SAI_RMR,
	.reg_defaults = fsl_sai_reg_defaults_ofs0,
	.num_reg_defaults = ARRAY_SIZE(fsl_sai_reg_defaults_ofs0),
	.readable_reg = fsl_sai_readable_reg,
	.volatile_reg = fsl_sai_volatile_reg,
	.writeable_reg = fsl_sai_writeable_reg,
	.cache_type = REGCACHE_FLAT,
};

static int fsl_sai_check_version(struct device *dev)
{
	struct fsl_sai *sai = dev_get_drvdata(dev);
	unsigned char ofs = sai->soc_data->reg_offset;
	unsigned int val;
	int ret;

	if (FSL_SAI_TCSR(ofs) == FSL_SAI_VERID)
		return 0;

	ret = regmap_read(sai->regmap, FSL_SAI_VERID, &val);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "VERID: 0x%016X\n", val);

	sai->verid.version = val &
		(FSL_SAI_VERID_MAJOR_MASK | FSL_SAI_VERID_MINOR_MASK);
	sai->verid.version >>= FSL_SAI_VERID_MINOR_SHIFT;
	sai->verid.feature = val & FSL_SAI_VERID_FEATURE_MASK;

	ret = regmap_read(sai->regmap, FSL_SAI_PARAM, &val);
	if (ret < 0)
		return ret;

	dev_dbg(dev, "PARAM: 0x%016X\n", val);

	/* Max slots per frame, power of 2 */
	sai->param.slot_num = 1 <<
		((val & FSL_SAI_PARAM_SPF_MASK) >> FSL_SAI_PARAM_SPF_SHIFT);

	/* Words per fifo, power of 2 */
	sai->param.fifo_depth = 1 <<
		((val & FSL_SAI_PARAM_WPF_MASK) >> FSL_SAI_PARAM_WPF_SHIFT);

	/* Number of datalines implemented */
	sai->param.dataline = val & FSL_SAI_PARAM_DLN_MASK;

	return 0;
}

/*
 * Calculate the offset between first two datalines, don't
 * different offset in one case.
 */
static unsigned int fsl_sai_calc_dl_off(unsigned long dl_mask)
{
	int fbidx, nbidx, offset;

	fbidx = find_first_bit(&dl_mask, FSL_SAI_DL_NUM);
	nbidx = find_next_bit(&dl_mask, FSL_SAI_DL_NUM, fbidx + 1);
	offset = nbidx - fbidx - 1;

	return (offset < 0 || offset >= (FSL_SAI_DL_NUM - 1) ? 0 : offset);
}

/*
 * read the fsl,dataline property from dts file.
 * It has 3 value for each configuration, first one means the type:
 * I2S(1) or PDM(2), second one is dataline mask for 'rx', third one is
 * dataline mask for 'tx'. for example
 *
 * fsl,dataline = <1 0xff 0xff 2 0xff 0x11>,
 *
 * It means I2S type rx mask is 0xff, tx mask is 0xff, PDM type
 * rx mask is 0xff, tx mask is 0x11 (dataline 1 and 4 enabled).
 *
 */
static int fsl_sai_read_dlcfg(struct fsl_sai *sai)
{
	struct platform_device *pdev = sai->pdev;
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	int ret, elems, i, index, num_cfg;
	char *propname = "fsl,dataline";
	struct fsl_sai_dl_cfg *cfg;
	unsigned long dl_mask;
	unsigned int soc_dl;
	u32 rx, tx, type;

	elems = of_property_count_u32_elems(np, propname);

	if (elems <= 0) {
		elems = 0;
	} else if (elems % 3) {
		dev_err(dev, "Number of elements must be divisible to 3.\n");
		return -EINVAL;
	}

	num_cfg = elems / 3;
	/*  Add one more for default value */
	cfg = devm_kzalloc(&pdev->dev, (num_cfg + 1) * sizeof(*cfg), GFP_KERNEL);
	if (!cfg)
		return -ENOMEM;

	/* Consider default value "0 0xFF 0xFF" if property is missing */
	soc_dl = BIT(sai->soc_data->pins) - 1;
	cfg[0].type = FSL_SAI_DL_DEFAULT;
	cfg[0].pins[0] = sai->soc_data->pins;
	cfg[0].mask[0] = soc_dl;
	cfg[0].start_off[0] = 0;
	cfg[0].next_off[0] = 0;

	cfg[0].pins[1] = sai->soc_data->pins;
	cfg[0].mask[1] = soc_dl;
	cfg[0].start_off[1] = 0;
	cfg[0].next_off[1] = 0;
	for (i = 1, index = 0; i < num_cfg + 1; i++) {
		/*
		 * type of dataline
		 * 0 means default mode
		 * 1 means I2S mode
		 * 2 means PDM mode
		 */
		ret = of_property_read_u32_index(np, propname, index++, &type);
		if (ret)
			return -EINVAL;

		ret = of_property_read_u32_index(np, propname, index++, &rx);
		if (ret)
			return -EINVAL;

		ret = of_property_read_u32_index(np, propname, index++, &tx);
		if (ret)
			return -EINVAL;

		if ((rx & ~soc_dl) || (tx & ~soc_dl)) {
			dev_err(dev, "dataline cfg[%d] setting error, mask is 0x%x\n", i, soc_dl);
			return -EINVAL;
		}

		rx = rx & soc_dl;
		tx = tx & soc_dl;

		cfg[i].type = type;
		cfg[i].pins[0] = hweight8(rx);
		cfg[i].mask[0] = rx;
		dl_mask = rx;
		cfg[i].start_off[0] = find_first_bit(&dl_mask, FSL_SAI_DL_NUM);
		cfg[i].next_off[0] = fsl_sai_calc_dl_off(rx);

		cfg[i].pins[1] = hweight8(tx);
		cfg[i].mask[1] = tx;
		dl_mask = tx;
		cfg[i].start_off[1] = find_first_bit(&dl_mask, FSL_SAI_DL_NUM);
		cfg[i].next_off[1] = fsl_sai_calc_dl_off(tx);
	}

	sai->dl_cfg = cfg;
	sai->dl_cfg_cnt = num_cfg + 1;
	return 0;
}

static int fsl_sai_runtime_suspend(struct device *dev);
static int fsl_sai_runtime_resume(struct device *dev);

static int fsl_sai_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device *dev = &pdev->dev;
	struct fsl_sai *sai;
	struct regmap *gpr;
	void __iomem *base;
	char tmp[8];
	int irq, ret, i;
	int index;
	u32 dmas[4];

	sai = devm_kzalloc(dev, sizeof(*sai), GFP_KERNEL);
	if (!sai)
		return -ENOMEM;

	sai->pdev = pdev;
	sai->soc_data = of_device_get_match_data(dev);

	sai->is_lsb_first = of_property_read_bool(np, "lsb-first");

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &sai->res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	if (sai->soc_data->reg_offset == 8) {
		fsl_sai_regmap_config.reg_defaults = fsl_sai_reg_defaults_ofs8;
		fsl_sai_regmap_config.max_register = FSL_SAI_MDIV;
		fsl_sai_regmap_config.num_reg_defaults =
			ARRAY_SIZE(fsl_sai_reg_defaults_ofs8);
	}

	sai->regmap = devm_regmap_init_mmio(dev, base, &fsl_sai_regmap_config);
	if (IS_ERR(sai->regmap)) {
		dev_err(dev, "regmap init failed\n");
		return PTR_ERR(sai->regmap);
	}

	sai->bus_clk = devm_clk_get(dev, "bus");
	/* Compatible with old DTB cases */
	if (IS_ERR(sai->bus_clk) && PTR_ERR(sai->bus_clk) != -EPROBE_DEFER)
		sai->bus_clk = devm_clk_get(dev, "sai");
	if (IS_ERR(sai->bus_clk)) {
		dev_err(dev, "failed to get bus clock: %ld\n",
				PTR_ERR(sai->bus_clk));
		/* -EPROBE_DEFER */
		return PTR_ERR(sai->bus_clk);
	}

	for (i = 1; i < FSL_SAI_MCLK_MAX; i++) {
		sprintf(tmp, "mclk%d", i);
		sai->mclk_clk[i] = devm_clk_get(dev, tmp);
		if (IS_ERR(sai->mclk_clk[i])) {
			dev_err(dev, "failed to get mclk%d clock: %ld\n",
					i, PTR_ERR(sai->mclk_clk[i]));
			sai->mclk_clk[i] = NULL;
		}
	}

	if (sai->soc_data->mclk0_is_mclk1)
		sai->mclk_clk[0] = sai->mclk_clk[1];
	else
		sai->mclk_clk[0] = sai->bus_clk;

	fsl_asoc_get_pll_clocks(&pdev->dev, &sai->pll8k_clk,
				&sai->pll11k_clk);

	/* Use Multi FIFO mode depending on the support from SDMA script */
	ret = of_property_read_u32_array(np, "dmas", dmas, 4);
	if (!sai->soc_data->use_edma && !ret && dmas[2] == IMX_DMATYPE_MULTI_SAI)
		sai->is_multi_fifo_dma = true;

	/* read dataline mask for rx and tx*/
	ret = fsl_sai_read_dlcfg(sai);
	if (ret < 0) {
		dev_err(dev, "failed to read dlcfg %d\n", ret);
		return ret;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, fsl_sai_isr, IRQF_SHARED,
			       np->name, sai);
	if (ret) {
		dev_err(dev, "failed to claim irq %u\n", irq);
		return ret;
	}

	memcpy(&sai->cpu_dai_drv, &fsl_sai_dai_template,
	       sizeof(fsl_sai_dai_template));

	/* Sync Tx with Rx as default by following old DT binding */
	sai->synchronous[RX] = true;
	sai->synchronous[TX] = false;
	sai->cpu_dai_drv.symmetric_rate = 1;
	sai->cpu_dai_drv.symmetric_channels = 1;
	sai->cpu_dai_drv.symmetric_sample_bits = 1;

	if (of_property_read_bool(np, "fsl,sai-synchronous-rx") &&
	    of_property_read_bool(np, "fsl,sai-asynchronous")) {
		/* error out if both synchronous and asynchronous are present */
		dev_err(dev, "invalid binding for synchronous mode\n");
		return -EINVAL;
	}

	if (of_property_read_bool(np, "fsl,sai-synchronous-rx")) {
		/* Sync Rx with Tx */
		sai->synchronous[RX] = false;
		sai->synchronous[TX] = true;
	} else if (of_property_read_bool(np, "fsl,sai-asynchronous")) {
		/* Discard all settings for asynchronous mode */
		sai->synchronous[RX] = false;
		sai->synchronous[TX] = false;
		sai->cpu_dai_drv.symmetric_rate = 0;
		sai->cpu_dai_drv.symmetric_channels = 0;
		sai->cpu_dai_drv.symmetric_sample_bits = 0;
	}

	sai->mclk_direction_output = of_property_read_bool(np, "fsl,sai-mclk-direction-output");

	if (sai->mclk_direction_output &&
	    of_device_is_compatible(np, "fsl,imx6ul-sai")) {
		gpr = syscon_regmap_lookup_by_compatible("fsl,imx6ul-iomuxc-gpr");
		if (IS_ERR(gpr)) {
			dev_err(dev, "cannot find iomuxc registers\n");
			return PTR_ERR(gpr);
		}

		index = of_alias_get_id(np, "sai");
		if (index < 0)
			return index;

		regmap_update_bits(gpr, IOMUXC_GPR1, MCLK_DIR(index),
				   MCLK_DIR(index));
	}

	sai->dma_params_rx.addr = sai->res->start + FSL_SAI_RDR0;
	sai->dma_params_tx.addr = sai->res->start + FSL_SAI_TDR0;
	sai->dma_params_rx.maxburst =
		sai->soc_data->max_burst[RX] ? sai->soc_data->max_burst[RX] : FSL_SAI_MAXBURST_RX;
	sai->dma_params_tx.maxburst =
		sai->soc_data->max_burst[TX] ? sai->soc_data->max_burst[TX] : FSL_SAI_MAXBURST_TX;

	sai->pinctrl = devm_pinctrl_get(&pdev->dev);

	platform_set_drvdata(pdev, sai);
	pm_runtime_enable(dev);
	if (!pm_runtime_enabled(dev)) {
		ret = fsl_sai_runtime_resume(dev);
		if (ret)
			goto err_pm_disable;
	}

	ret = pm_runtime_resume_and_get(dev);
	if (ret < 0)
		goto err_pm_get_sync;

	/* Get sai version */
	ret = fsl_sai_check_version(dev);
	if (ret < 0)
		dev_warn(dev, "Error reading SAI version: %d\n", ret);

	/* Select MCLK direction */
	if (sai->mclk_direction_output &&
	    sai->soc_data->max_register >= FSL_SAI_MCTL) {
		regmap_update_bits(sai->regmap, FSL_SAI_MCTL,
				   FSL_SAI_MCTL_MCLK_EN, FSL_SAI_MCTL_MCLK_EN);
	}

	ret = pm_runtime_put_sync(dev);
	if (ret < 0 && ret != -ENOSYS)
		goto err_pm_get_sync;

	/*
	 * Register platform component before registering cpu dai for there
	 * is not defer probe for platform component in snd_soc_add_pcm_runtime().
	 */
	if (sai->soc_data->use_imx_pcm) {
		ret = imx_pcm_dma_init(pdev);
		if (ret) {
			dev_err_probe(dev, ret, "PCM DMA init failed\n");
			if (!IS_ENABLED(CONFIG_SND_SOC_IMX_PCM_DMA))
				dev_err(dev, "Error: You must enable the imx-pcm-dma support!\n");
			goto err_pm_get_sync;
		}
	} else {
		ret = devm_snd_dmaengine_pcm_register(dev, NULL, 0);
		if (ret) {
			dev_err_probe(dev, ret, "Registering PCM dmaengine failed\n");
			goto err_pm_get_sync;
		}
	}

	ret = devm_snd_soc_register_component(dev, &fsl_component,
					      &sai->cpu_dai_drv, 1);
	if (ret)
		goto err_pm_get_sync;

	return ret;

err_pm_get_sync:
	if (!pm_runtime_status_suspended(dev))
		fsl_sai_runtime_suspend(dev);
err_pm_disable:
	pm_runtime_disable(dev);

	return ret;
}

static void fsl_sai_remove(struct platform_device *pdev)
{
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev))
		fsl_sai_runtime_suspend(&pdev->dev);
}

static const struct fsl_sai_soc_data fsl_sai_vf610_data = {
	.use_imx_pcm = false,
	.use_edma = false,
	.fifo_depth = 32,
	.pins = 1,
	.reg_offset = 0,
	.mclk0_is_mclk1 = false,
	.flags = 0,
	.max_register = FSL_SAI_RMR,
};

static const struct fsl_sai_soc_data fsl_sai_imx6sx_data = {
	.use_imx_pcm = true,
	.use_edma = false,
	.fifo_depth = 32,
	.pins = 1,
	.reg_offset = 0,
	.mclk0_is_mclk1 = true,
	.flags = 0,
	.max_register = FSL_SAI_RMR,
};

static const struct fsl_sai_soc_data fsl_sai_imx7ulp_data = {
	.use_imx_pcm = true,
	.use_edma = false,
	.fifo_depth = 16,
	.pins = 2,
	.reg_offset = 8,
	.mclk0_is_mclk1 = false,
	.flags = PMQOS_CPU_LATENCY,
	.max_register = FSL_SAI_RMR,
};

static const struct fsl_sai_soc_data fsl_sai_imx8mq_data = {
	.use_imx_pcm = true,
	.use_edma = false,
	.fifo_depth = 128,
	.pins = 8,
	.reg_offset = 8,
	.mclk0_is_mclk1 = false,
	.flags = 0,
	.max_register = FSL_SAI_RMR,
};

static const struct fsl_sai_soc_data fsl_sai_imx8qm_data = {
	.use_imx_pcm = true,
	.use_edma = true,
	.fifo_depth = 64,
	.pins = 4,
	.reg_offset = 0,
	.mclk0_is_mclk1 = false,
	.flags = 0,
	.max_register = FSL_SAI_RMR,
};

static const struct fsl_sai_soc_data fsl_sai_imx8mm_data = {
	.use_imx_pcm = true,
	.use_edma = false,
	.fifo_depth = 128,
	.reg_offset = 8,
	.mclk0_is_mclk1 = false,
	.pins = 8,
	.flags = 0,
	.max_register = FSL_SAI_MCTL,
};

static const struct fsl_sai_soc_data fsl_sai_imx8mn_data = {
	.use_imx_pcm = true,
	.use_edma = false,
	.fifo_depth = 128,
	.reg_offset = 8,
	.mclk0_is_mclk1 = false,
	.pins = 8,
	.flags = 0,
	.max_register = FSL_SAI_MDIV,
};

static const struct fsl_sai_soc_data fsl_sai_imx8mp_data = {
	.use_imx_pcm = true,
	.use_edma = false,
	.fifo_depth = 128,
	.reg_offset = 8,
	.mclk0_is_mclk1 = false,
	.pins = 8,
	.flags = 0,
	.max_register = FSL_SAI_MDIV,
	.mclk_with_tere = true,
};

static const struct fsl_sai_soc_data fsl_sai_imx8ulp_data = {
	.use_imx_pcm = true,
	.use_edma = true,
	.fifo_depth = 16,
	.reg_offset = 8,
	.mclk0_is_mclk1 = false,
	.pins = 4,
	.flags = PMQOS_CPU_LATENCY,
	.max_register = FSL_SAI_RTCAP,
};

static const struct fsl_sai_soc_data fsl_sai_imx93_data = {
	.use_imx_pcm = true,
	.use_edma = true,
	.fifo_depth = 128,
	.reg_offset = 8,
	.mclk0_is_mclk1 = false,
	.pins = 4,
	.flags = 0,
	.max_register = FSL_SAI_MCTL,
	.max_burst = {8, 8},
};

static const struct of_device_id fsl_sai_ids[] = {
	{ .compatible = "fsl,vf610-sai", .data = &fsl_sai_vf610_data },
	{ .compatible = "fsl,imx6sx-sai", .data = &fsl_sai_imx6sx_data },
	{ .compatible = "fsl,imx6ul-sai", .data = &fsl_sai_imx6sx_data },
	{ .compatible = "fsl,imx7ulp-sai", .data = &fsl_sai_imx7ulp_data },
	{ .compatible = "fsl,imx8mq-sai", .data = &fsl_sai_imx8mq_data },
	{ .compatible = "fsl,imx8qm-sai", .data = &fsl_sai_imx8qm_data },
	{ .compatible = "fsl,imx8mm-sai", .data = &fsl_sai_imx8mm_data },
	{ .compatible = "fsl,imx8mp-sai", .data = &fsl_sai_imx8mp_data },
	{ .compatible = "fsl,imx8ulp-sai", .data = &fsl_sai_imx8ulp_data },
	{ .compatible = "fsl,imx8mn-sai", .data = &fsl_sai_imx8mn_data },
	{ .compatible = "fsl,imx93-sai", .data = &fsl_sai_imx93_data },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, fsl_sai_ids);

static int fsl_sai_runtime_suspend(struct device *dev)
{
	struct fsl_sai *sai = dev_get_drvdata(dev);

	if (sai->mclk_streams & BIT(SNDRV_PCM_STREAM_CAPTURE))
		clk_disable_unprepare(sai->mclk_clk[sai->mclk_id[0]]);

	if (sai->mclk_streams & BIT(SNDRV_PCM_STREAM_PLAYBACK))
		clk_disable_unprepare(sai->mclk_clk[sai->mclk_id[1]]);

	clk_disable_unprepare(sai->bus_clk);

	if (sai->soc_data->flags & PMQOS_CPU_LATENCY)
		cpu_latency_qos_remove_request(&sai->pm_qos_req);

	regcache_cache_only(sai->regmap, true);

	return 0;
}

static int fsl_sai_runtime_resume(struct device *dev)
{
	struct fsl_sai *sai = dev_get_drvdata(dev);
	unsigned int ofs = sai->soc_data->reg_offset;
	int ret;

	ret = clk_prepare_enable(sai->bus_clk);
	if (ret) {
		dev_err(dev, "failed to enable bus clock: %d\n", ret);
		return ret;
	}

	if (sai->mclk_streams & BIT(SNDRV_PCM_STREAM_PLAYBACK)) {
		ret = clk_prepare_enable(sai->mclk_clk[sai->mclk_id[1]]);
		if (ret)
			goto disable_bus_clk;
	}

	if (sai->mclk_streams & BIT(SNDRV_PCM_STREAM_CAPTURE)) {
		ret = clk_prepare_enable(sai->mclk_clk[sai->mclk_id[0]]);
		if (ret)
			goto disable_tx_clk;
	}

	if (sai->soc_data->flags & PMQOS_CPU_LATENCY)
		cpu_latency_qos_add_request(&sai->pm_qos_req, 0);

	regcache_cache_only(sai->regmap, false);
	regcache_mark_dirty(sai->regmap);
	regmap_write(sai->regmap, FSL_SAI_TCSR(ofs), FSL_SAI_CSR_SR);
	regmap_write(sai->regmap, FSL_SAI_RCSR(ofs), FSL_SAI_CSR_SR);
	usleep_range(1000, 2000);
	regmap_write(sai->regmap, FSL_SAI_TCSR(ofs), 0);
	regmap_write(sai->regmap, FSL_SAI_RCSR(ofs), 0);

	ret = regcache_sync(sai->regmap);
	if (ret)
		goto disable_rx_clk;

	if (sai->soc_data->mclk_with_tere && sai->mclk_direction_output)
		regmap_update_bits(sai->regmap, FSL_SAI_TCSR(ofs),
				   FSL_SAI_CSR_TERE, FSL_SAI_CSR_TERE);

	return 0;

disable_rx_clk:
	if (sai->mclk_streams & BIT(SNDRV_PCM_STREAM_CAPTURE))
		clk_disable_unprepare(sai->mclk_clk[sai->mclk_id[0]]);
disable_tx_clk:
	if (sai->mclk_streams & BIT(SNDRV_PCM_STREAM_PLAYBACK))
		clk_disable_unprepare(sai->mclk_clk[sai->mclk_id[1]]);
disable_bus_clk:
	clk_disable_unprepare(sai->bus_clk);

	return ret;
}

static const struct dev_pm_ops fsl_sai_pm_ops = {
	SET_RUNTIME_PM_OPS(fsl_sai_runtime_suspend,
			   fsl_sai_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static struct platform_driver fsl_sai_driver = {
	.probe = fsl_sai_probe,
	.remove_new = fsl_sai_remove,
	.driver = {
		.name = "fsl-sai",
		.pm = &fsl_sai_pm_ops,
		.of_match_table = fsl_sai_ids,
	},
};
module_platform_driver(fsl_sai_driver);

MODULE_DESCRIPTION("Freescale Soc SAI Interface");
MODULE_AUTHOR("Xiubo Li, <Li.Xiubo@freescale.com>");
MODULE_ALIAS("platform:fsl-sai");
MODULE_LICENSE("GPL");
