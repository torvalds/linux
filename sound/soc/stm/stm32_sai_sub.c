/*
 * STM32 ALSA SoC Digital Audio Interface (SAI) driver.
 *
 * Copyright (C) 2016, STMicroelectronics - All Rights Reserved
 * Author(s): Olivier Moysan <olivier.moysan@st.com> for STMicroelectronics.
 *
 * License terms: GPL V2.0.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
 * details.
 */

#include <linux/clk.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>

#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#include "stm32_sai.h"

#define SAI_FREE_PROTOCOL	0x0

#define SAI_SLOT_SIZE_AUTO	0x0
#define SAI_SLOT_SIZE_16	0x1
#define SAI_SLOT_SIZE_32	0x2

#define SAI_DATASIZE_8		0x2
#define SAI_DATASIZE_10		0x3
#define SAI_DATASIZE_16		0x4
#define SAI_DATASIZE_20		0x5
#define SAI_DATASIZE_24		0x6
#define SAI_DATASIZE_32		0x7

#define STM_SAI_FIFO_SIZE	8
#define STM_SAI_DAI_NAME_SIZE	15

#define STM_SAI_IS_PLAYBACK(ip)	((ip)->dir == SNDRV_PCM_STREAM_PLAYBACK)
#define STM_SAI_IS_CAPTURE(ip)	((ip)->dir == SNDRV_PCM_STREAM_CAPTURE)

#define STM_SAI_A_ID		0x0
#define STM_SAI_B_ID		0x1

#define STM_SAI_BLOCK_NAME(x)	(((x)->id == STM_SAI_A_ID) ? "A" : "B")

/**
 * struct stm32_sai_sub_data - private data of SAI sub block (block A or B)
 * @pdev: device data pointer
 * @regmap: SAI register map pointer
 * @dma_params: dma configuration data for rx or tx channel
 * @cpu_dai_drv: DAI driver data pointer
 * @cpu_dai: DAI runtime data pointer
 * @substream: PCM substream data pointer
 * @pdata: SAI block parent data pointer
 * @sai_ck: kernel clock feeding the SAI clock generator
 * @phys_addr: SAI registers physical base address
 * @mclk_rate: SAI block master clock frequency (Hz). set at init
 * @id: SAI sub block id corresponding to sub-block A or B
 * @dir: SAI block direction (playback or capture). set at init
 * @master: SAI block mode flag. (true=master, false=slave) set at init
 * @fmt: SAI block format. relevant only for custom protocols. set at init
 * @sync: SAI block synchronization mode. (none, internal or external)
 * @fs_length: frame synchronization length. depends on protocol settings
 * @slots: rx or tx slot number
 * @slot_width: rx or tx slot width in bits
 * @slot_mask: rx or tx active slots mask. set at init or at runtime
 * @data_size: PCM data width. corresponds to PCM substream width.
 */
struct stm32_sai_sub_data {
	struct platform_device *pdev;
	struct regmap *regmap;
	struct snd_dmaengine_dai_dma_data dma_params;
	struct snd_soc_dai_driver *cpu_dai_drv;
	struct snd_soc_dai *cpu_dai;
	struct snd_pcm_substream *substream;
	struct stm32_sai_data *pdata;
	struct clk *sai_ck;
	dma_addr_t phys_addr;
	unsigned int mclk_rate;
	unsigned int id;
	int dir;
	bool master;
	int fmt;
	int sync;
	int fs_length;
	int slots;
	int slot_width;
	int slot_mask;
	int data_size;
};

enum stm32_sai_fifo_th {
	STM_SAI_FIFO_TH_EMPTY,
	STM_SAI_FIFO_TH_QUARTER,
	STM_SAI_FIFO_TH_HALF,
	STM_SAI_FIFO_TH_3_QUARTER,
	STM_SAI_FIFO_TH_FULL,
};

static bool stm32_sai_sub_readable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STM_SAI_CR1_REGX:
	case STM_SAI_CR2_REGX:
	case STM_SAI_FRCR_REGX:
	case STM_SAI_SLOTR_REGX:
	case STM_SAI_IMR_REGX:
	case STM_SAI_SR_REGX:
	case STM_SAI_CLRFR_REGX:
	case STM_SAI_DR_REGX:
		return true;
	default:
		return false;
	}
}

static bool stm32_sai_sub_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STM_SAI_DR_REGX:
		return true;
	default:
		return false;
	}
}

static bool stm32_sai_sub_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case STM_SAI_CR1_REGX:
	case STM_SAI_CR2_REGX:
	case STM_SAI_FRCR_REGX:
	case STM_SAI_SLOTR_REGX:
	case STM_SAI_IMR_REGX:
	case STM_SAI_SR_REGX:
	case STM_SAI_CLRFR_REGX:
	case STM_SAI_DR_REGX:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config stm32_sai_sub_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = STM_SAI_DR_REGX,
	.readable_reg = stm32_sai_sub_readable_reg,
	.volatile_reg = stm32_sai_sub_volatile_reg,
	.writeable_reg = stm32_sai_sub_writeable_reg,
	.fast_io = true,
};

static irqreturn_t stm32_sai_isr(int irq, void *devid)
{
	struct stm32_sai_sub_data *sai = (struct stm32_sai_sub_data *)devid;
	struct snd_pcm_substream *substream = sai->substream;
	struct platform_device *pdev = sai->pdev;
	unsigned int sr, imr, flags;
	snd_pcm_state_t status = SNDRV_PCM_STATE_RUNNING;

	regmap_read(sai->regmap, STM_SAI_IMR_REGX, &imr);
	regmap_read(sai->regmap, STM_SAI_SR_REGX, &sr);

	flags = sr & imr;
	if (!flags)
		return IRQ_NONE;

	regmap_update_bits(sai->regmap, STM_SAI_CLRFR_REGX, SAI_XCLRFR_MASK,
			   SAI_XCLRFR_MASK);

	if (flags & SAI_XIMR_OVRUDRIE) {
		dev_err(&pdev->dev, "IT %s\n",
			STM_SAI_IS_PLAYBACK(sai) ? "underrun" : "overrun");
		status = SNDRV_PCM_STATE_XRUN;
	}

	if (flags & SAI_XIMR_MUTEDETIE)
		dev_dbg(&pdev->dev, "IT mute detected\n");

	if (flags & SAI_XIMR_WCKCFGIE) {
		dev_err(&pdev->dev, "IT wrong clock configuration\n");
		status = SNDRV_PCM_STATE_DISCONNECTED;
	}

	if (flags & SAI_XIMR_CNRDYIE)
		dev_warn(&pdev->dev, "IT Codec not ready\n");

	if (flags & SAI_XIMR_AFSDETIE) {
		dev_warn(&pdev->dev, "IT Anticipated frame synchro\n");
		status = SNDRV_PCM_STATE_XRUN;
	}

	if (flags & SAI_XIMR_LFSDETIE) {
		dev_warn(&pdev->dev, "IT Late frame synchro\n");
		status = SNDRV_PCM_STATE_XRUN;
	}

	if (status != SNDRV_PCM_STATE_RUNNING) {
		snd_pcm_stream_lock(substream);
		snd_pcm_stop(substream, SNDRV_PCM_STATE_XRUN);
		snd_pcm_stream_unlock(substream);
	}

	return IRQ_HANDLED;
}

static int stm32_sai_set_sysclk(struct snd_soc_dai *cpu_dai,
				int clk_id, unsigned int freq, int dir)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);

	if ((dir == SND_SOC_CLOCK_OUT) && sai->master) {
		sai->mclk_rate = freq;
		dev_dbg(cpu_dai->dev, "SAI MCLK frequency is %uHz\n", freq);
	}

	return 0;
}

static int stm32_sai_set_dai_tdm_slot(struct snd_soc_dai *cpu_dai, u32 tx_mask,
				      u32 rx_mask, int slots, int slot_width)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int slotr, slotr_mask, slot_size;

	dev_dbg(cpu_dai->dev, "masks tx/rx:%#x/%#x, slots:%d, width:%d\n",
		tx_mask, rx_mask, slots, slot_width);

	switch (slot_width) {
	case 16:
		slot_size = SAI_SLOT_SIZE_16;
		break;
	case 32:
		slot_size = SAI_SLOT_SIZE_32;
		break;
	default:
		slot_size = SAI_SLOT_SIZE_AUTO;
		break;
	}

	slotr = SAI_XSLOTR_SLOTSZ_SET(slot_size) |
		SAI_XSLOTR_NBSLOT_SET(slots - 1);
	slotr_mask = SAI_XSLOTR_SLOTSZ_MASK | SAI_XSLOTR_NBSLOT_MASK;

	/* tx/rx mask set in machine init, if slot number defined in DT */
	if (STM_SAI_IS_PLAYBACK(sai)) {
		sai->slot_mask = tx_mask;
		slotr |= SAI_XSLOTR_SLOTEN_SET(tx_mask);
	}

	if (STM_SAI_IS_CAPTURE(sai)) {
		sai->slot_mask = rx_mask;
		slotr |= SAI_XSLOTR_SLOTEN_SET(rx_mask);
	}

	slotr_mask |= SAI_XSLOTR_SLOTEN_MASK;

	regmap_update_bits(sai->regmap, STM_SAI_SLOTR_REGX, slotr_mask, slotr);

	sai->slot_width = slot_width;
	sai->slots = slots;

	return 0;
}

static int stm32_sai_set_dai_fmt(struct snd_soc_dai *cpu_dai, unsigned int fmt)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int cr1 = 0, frcr = 0;
	int cr1_mask = 0, frcr_mask = 0;
	int ret;

	dev_dbg(cpu_dai->dev, "fmt %x\n", fmt);

	switch (fmt & SND_SOC_DAIFMT_FORMAT_MASK) {
	/* SCK active high for all protocols */
	case SND_SOC_DAIFMT_I2S:
		cr1 |= SAI_XCR1_CKSTR;
		frcr |= SAI_XFRCR_FSOFF | SAI_XFRCR_FSDEF;
		break;
	/* Left justified */
	case SND_SOC_DAIFMT_MSB:
		frcr |= SAI_XFRCR_FSPOL | SAI_XFRCR_FSDEF;
		break;
	/* Right justified */
	case SND_SOC_DAIFMT_LSB:
		frcr |= SAI_XFRCR_FSPOL | SAI_XFRCR_FSDEF;
		break;
	case SND_SOC_DAIFMT_DSP_A:
		frcr |= SAI_XFRCR_FSPOL | SAI_XFRCR_FSOFF;
		break;
	case SND_SOC_DAIFMT_DSP_B:
		frcr |= SAI_XFRCR_FSPOL;
		break;
	default:
		dev_err(cpu_dai->dev, "Unsupported protocol %#x\n",
			fmt & SND_SOC_DAIFMT_FORMAT_MASK);
		return -EINVAL;
	}

	cr1_mask |= SAI_XCR1_PRTCFG_MASK | SAI_XCR1_CKSTR;
	frcr_mask |= SAI_XFRCR_FSPOL | SAI_XFRCR_FSOFF |
		     SAI_XFRCR_FSDEF;

	/* DAI clock strobing. Invert setting previously set */
	switch (fmt & SND_SOC_DAIFMT_INV_MASK) {
	case SND_SOC_DAIFMT_NB_NF:
		break;
	case SND_SOC_DAIFMT_IB_NF:
		cr1 ^= SAI_XCR1_CKSTR;
		break;
	case SND_SOC_DAIFMT_NB_IF:
		frcr ^= SAI_XFRCR_FSPOL;
		break;
	case SND_SOC_DAIFMT_IB_IF:
		/* Invert fs & sck */
		cr1 ^= SAI_XCR1_CKSTR;
		frcr ^= SAI_XFRCR_FSPOL;
		break;
	default:
		dev_err(cpu_dai->dev, "Unsupported strobing %#x\n",
			fmt & SND_SOC_DAIFMT_INV_MASK);
		return -EINVAL;
	}
	cr1_mask |= SAI_XCR1_CKSTR;
	frcr_mask |= SAI_XFRCR_FSPOL;

	regmap_update_bits(sai->regmap, STM_SAI_FRCR_REGX, frcr_mask, frcr);

	/* DAI clock master masks */
	switch (fmt & SND_SOC_DAIFMT_MASTER_MASK) {
	case SND_SOC_DAIFMT_CBM_CFM:
		/* codec is master */
		cr1 |= SAI_XCR1_SLAVE;
		sai->master = false;
		break;
	case SND_SOC_DAIFMT_CBS_CFS:
		sai->master = true;
		break;
	default:
		dev_err(cpu_dai->dev, "Unsupported mode %#x\n",
			fmt & SND_SOC_DAIFMT_MASTER_MASK);
		return -EINVAL;
	}
	cr1_mask |= SAI_XCR1_SLAVE;

	ret = regmap_update_bits(sai->regmap, STM_SAI_CR1_REGX, cr1_mask, cr1);
	if (ret < 0) {
		dev_err(cpu_dai->dev, "Failed to update CR1 register\n");
		return ret;
	}

	sai->fmt = fmt;

	return 0;
}

static int stm32_sai_startup(struct snd_pcm_substream *substream,
			     struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int imr, cr2, ret;

	sai->substream = substream;

	ret = clk_prepare_enable(sai->sai_ck);
	if (ret < 0) {
		dev_err(cpu_dai->dev, "failed to enable clock: %d\n", ret);
		return ret;
	}

	/* Enable ITs */
	regmap_update_bits(sai->regmap, STM_SAI_SR_REGX,
			   SAI_XSR_MASK, (unsigned int)~SAI_XSR_MASK);

	regmap_update_bits(sai->regmap, STM_SAI_CLRFR_REGX,
			   SAI_XCLRFR_MASK, SAI_XCLRFR_MASK);

	imr = SAI_XIMR_OVRUDRIE;
	if (STM_SAI_IS_CAPTURE(sai)) {
		regmap_read(sai->regmap, STM_SAI_CR2_REGX, &cr2);
		if (cr2 & SAI_XCR2_MUTECNT_MASK)
			imr |= SAI_XIMR_MUTEDETIE;
	}

	if (sai->master)
		imr |= SAI_XIMR_WCKCFGIE;
	else
		imr |= SAI_XIMR_AFSDETIE | SAI_XIMR_LFSDETIE;

	regmap_update_bits(sai->regmap, STM_SAI_IMR_REGX,
			   SAI_XIMR_MASK, imr);

	return 0;
}

static int stm32_sai_set_config(struct snd_soc_dai *cpu_dai,
				struct snd_pcm_substream *substream,
				struct snd_pcm_hw_params *params)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int cr1, cr1_mask, ret;
	int fth = STM_SAI_FIFO_TH_HALF;

	/* FIFO config */
	regmap_update_bits(sai->regmap, STM_SAI_CR2_REGX,
			   SAI_XCR2_FFLUSH | SAI_XCR2_FTH_MASK,
			   SAI_XCR2_FFLUSH | SAI_XCR2_FTH_SET(fth));

	/* Mode, data format and channel config */
	cr1 = SAI_XCR1_PRTCFG_SET(SAI_FREE_PROTOCOL);
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		cr1 |= SAI_XCR1_DS_SET(SAI_DATASIZE_8);
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		cr1 |= SAI_XCR1_DS_SET(SAI_DATASIZE_16);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		cr1 |= SAI_XCR1_DS_SET(SAI_DATASIZE_32);
		break;
	default:
		dev_err(cpu_dai->dev, "Data format not supported");
		return -EINVAL;
	}
	cr1_mask = SAI_XCR1_DS_MASK | SAI_XCR1_PRTCFG_MASK;

	cr1_mask |= SAI_XCR1_RX_TX;
	if (STM_SAI_IS_CAPTURE(sai))
		cr1 |= SAI_XCR1_RX_TX;

	cr1_mask |= SAI_XCR1_MONO;
	if ((sai->slots == 2) && (params_channels(params) == 1))
		cr1 |= SAI_XCR1_MONO;

	ret = regmap_update_bits(sai->regmap, STM_SAI_CR1_REGX, cr1_mask, cr1);
	if (ret < 0) {
		dev_err(cpu_dai->dev, "Failed to update CR1 register\n");
		return ret;
	}

	/* DMA config */
	sai->dma_params.maxburst = STM_SAI_FIFO_SIZE * fth / sizeof(u32);
	snd_soc_dai_set_dma_data(cpu_dai, substream, (void *)&sai->dma_params);

	return 0;
}

static int stm32_sai_set_slots(struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int slotr, slot_sz;

	regmap_read(sai->regmap, STM_SAI_SLOTR_REGX, &slotr);

	/*
	 * If SLOTSZ is set to auto in SLOTR, align slot width on data size
	 * By default slot width = data size, if not forced from DT
	 */
	slot_sz = slotr & SAI_XSLOTR_SLOTSZ_MASK;
	if (slot_sz == SAI_XSLOTR_SLOTSZ_SET(SAI_SLOT_SIZE_AUTO))
		sai->slot_width = sai->data_size;

	if (sai->slot_width < sai->data_size) {
		dev_err(cpu_dai->dev,
			"Data size %d larger than slot width\n",
			sai->data_size);
		return -EINVAL;
	}

	/* Slot number is set to 2, if not specified in DT */
	if (!sai->slots)
		sai->slots = 2;

	/* The number of slots in the audio frame is equal to NBSLOT[3:0] + 1*/
	regmap_update_bits(sai->regmap, STM_SAI_SLOTR_REGX,
			   SAI_XSLOTR_NBSLOT_MASK,
			   SAI_XSLOTR_NBSLOT_SET((sai->slots - 1)));

	/* Set default slots mask if not already set from DT */
	if (!(slotr & SAI_XSLOTR_SLOTEN_MASK)) {
		sai->slot_mask = (1 << sai->slots) - 1;
		regmap_update_bits(sai->regmap,
				   STM_SAI_SLOTR_REGX, SAI_XSLOTR_SLOTEN_MASK,
				   SAI_XSLOTR_SLOTEN_SET(sai->slot_mask));
	}

	dev_dbg(cpu_dai->dev, "slots %d, slot width %d\n",
		sai->slots, sai->slot_width);

	return 0;
}

static void stm32_sai_set_frame(struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int fs_active, offset, format;
	int frcr, frcr_mask;

	format = sai->fmt & SND_SOC_DAIFMT_FORMAT_MASK;
	sai->fs_length = sai->slot_width * sai->slots;

	fs_active = sai->fs_length / 2;
	if ((format == SND_SOC_DAIFMT_DSP_A) ||
	    (format == SND_SOC_DAIFMT_DSP_B))
		fs_active = 1;

	frcr = SAI_XFRCR_FRL_SET((sai->fs_length - 1));
	frcr |= SAI_XFRCR_FSALL_SET((fs_active - 1));
	frcr_mask = SAI_XFRCR_FRL_MASK | SAI_XFRCR_FSALL_MASK;

	dev_dbg(cpu_dai->dev, "frame length %d, frame active %d\n",
		sai->fs_length, fs_active);

	regmap_update_bits(sai->regmap, STM_SAI_FRCR_REGX, frcr_mask, frcr);

	if ((sai->fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_LSB) {
		offset = sai->slot_width - sai->data_size;

		regmap_update_bits(sai->regmap, STM_SAI_SLOTR_REGX,
				   SAI_XSLOTR_FBOFF_MASK,
				   SAI_XSLOTR_FBOFF_SET(offset));
	}
}

static int stm32_sai_configure_clock(struct snd_soc_dai *cpu_dai,
				     struct snd_pcm_hw_params *params)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int cr1, mask, div = 0;
	int sai_clk_rate, ret;

	if (!sai->mclk_rate) {
		dev_err(cpu_dai->dev, "Mclk rate is null\n");
		return -EINVAL;
	}

	if (!(params_rate(params) % 11025))
		clk_set_parent(sai->sai_ck, sai->pdata->clk_x11k);
	else
		clk_set_parent(sai->sai_ck, sai->pdata->clk_x8k);
	sai_clk_rate = clk_get_rate(sai->sai_ck);

	/*
	 * mclk_rate = 256 * fs
	 * MCKDIV = 0 if sai_ck < 3/2 * mclk_rate
	 * MCKDIV = sai_ck / (2 * mclk_rate) otherwise
	 */
	if (2 * sai_clk_rate >= 3 * sai->mclk_rate)
		div = DIV_ROUND_CLOSEST(sai_clk_rate, 2 * sai->mclk_rate);

	if (div > SAI_XCR1_MCKDIV_MAX) {
		dev_err(cpu_dai->dev, "Divider %d out of range\n", div);
		return -EINVAL;
	}
	dev_dbg(cpu_dai->dev, "SAI clock %d, divider %d\n", sai_clk_rate, div);

	mask = SAI_XCR1_MCKDIV_MASK;
	cr1 = SAI_XCR1_MCKDIV_SET(div);
	ret = regmap_update_bits(sai->regmap, STM_SAI_CR1_REGX, mask, cr1);
	if (ret < 0) {
		dev_err(cpu_dai->dev, "Failed to update CR1 register\n");
		return ret;
	}

	return 0;
}

static int stm32_sai_hw_params(struct snd_pcm_substream *substream,
			       struct snd_pcm_hw_params *params,
			       struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int ret;

	sai->data_size = params_width(params);

	ret = stm32_sai_set_slots(cpu_dai);
	if (ret < 0)
		return ret;
	stm32_sai_set_frame(cpu_dai);

	ret = stm32_sai_set_config(cpu_dai, substream, params);
	if (ret)
		return ret;

	if (sai->master)
		ret = stm32_sai_configure_clock(cpu_dai, params);

	return ret;
}

static int stm32_sai_trigger(struct snd_pcm_substream *substream, int cmd,
			     struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int ret;

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dev_dbg(cpu_dai->dev, "Enable DMA and SAI\n");

		regmap_update_bits(sai->regmap, STM_SAI_CR1_REGX,
				   SAI_XCR1_DMAEN, SAI_XCR1_DMAEN);

		/* Enable SAI */
		ret = regmap_update_bits(sai->regmap, STM_SAI_CR1_REGX,
					 SAI_XCR1_SAIEN, SAI_XCR1_SAIEN);
		if (ret < 0)
			dev_err(cpu_dai->dev, "Failed to update CR1 register\n");
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
	case SNDRV_PCM_TRIGGER_STOP:
		dev_dbg(cpu_dai->dev, "Disable DMA and SAI\n");

		regmap_update_bits(sai->regmap, STM_SAI_CR1_REGX,
				   SAI_XCR1_DMAEN,
				   (unsigned int)~SAI_XCR1_DMAEN);

		ret = regmap_update_bits(sai->regmap, STM_SAI_CR1_REGX,
					 SAI_XCR1_SAIEN,
					 (unsigned int)~SAI_XCR1_SAIEN);
		if (ret < 0)
			dev_err(cpu_dai->dev, "Failed to update CR1 register\n");
		break;
	default:
		return -EINVAL;
	}

	return ret;
}

static void stm32_sai_shutdown(struct snd_pcm_substream *substream,
			       struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);

	regmap_update_bits(sai->regmap, STM_SAI_IMR_REGX, SAI_XIMR_MASK, 0);

	clk_disable_unprepare(sai->sai_ck);
	sai->substream = NULL;
}

static int stm32_sai_dai_probe(struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = dev_get_drvdata(cpu_dai->dev);

	sai->dma_params.addr = (dma_addr_t)(sai->phys_addr + STM_SAI_DR_REGX);
	sai->dma_params.maxburst = 1;
	/* Buswidth will be set by framework at runtime */
	sai->dma_params.addr_width = DMA_SLAVE_BUSWIDTH_UNDEFINED;

	if (STM_SAI_IS_PLAYBACK(sai))
		snd_soc_dai_init_dma_data(cpu_dai, &sai->dma_params, NULL);
	else
		snd_soc_dai_init_dma_data(cpu_dai, NULL, &sai->dma_params);

	return 0;
}

static const struct snd_soc_dai_ops stm32_sai_pcm_dai_ops = {
	.set_sysclk	= stm32_sai_set_sysclk,
	.set_fmt	= stm32_sai_set_dai_fmt,
	.set_tdm_slot	= stm32_sai_set_dai_tdm_slot,
	.startup	= stm32_sai_startup,
	.hw_params	= stm32_sai_hw_params,
	.trigger	= stm32_sai_trigger,
	.shutdown	= stm32_sai_shutdown,
};

static const struct snd_pcm_hardware stm32_sai_pcm_hw = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_MMAP,
	.buffer_bytes_max = 8 * PAGE_SIZE,
	.period_bytes_min = 1024, /* 5ms at 48kHz */
	.period_bytes_max = PAGE_SIZE,
	.periods_min = 2,
	.periods_max = 8,
};

static struct snd_soc_dai_driver stm32_sai_playback_dai[] = {
{
		.probe = stm32_sai_dai_probe,
		.id = 1, /* avoid call to fmt_single_name() */
		.playback = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			/* DMA does not support 24 bits transfers */
			.formats =
				SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &stm32_sai_pcm_dai_ops,
	}
};

static struct snd_soc_dai_driver stm32_sai_capture_dai[] = {
{
		.probe = stm32_sai_dai_probe,
		.id = 1, /* avoid call to fmt_single_name() */
		.capture = {
			.channels_min = 1,
			.channels_max = 2,
			.rate_min = 8000,
			.rate_max = 192000,
			.rates = SNDRV_PCM_RATE_CONTINUOUS,
			/* DMA does not support 24 bits transfers */
			.formats =
				SNDRV_PCM_FMTBIT_S8 |
				SNDRV_PCM_FMTBIT_S16_LE |
				SNDRV_PCM_FMTBIT_S32_LE,
		},
		.ops = &stm32_sai_pcm_dai_ops,
	}
};

static const struct snd_dmaengine_pcm_config stm32_sai_pcm_config = {
	.pcm_hardware	= &stm32_sai_pcm_hw,
	.prepare_slave_config	= snd_dmaengine_pcm_prepare_slave_config,
};

static const struct snd_soc_component_driver stm32_component = {
	.name = "stm32-sai",
};

static const struct of_device_id stm32_sai_sub_ids[] = {
	{ .compatible = "st,stm32-sai-sub-a",
	  .data = (void *)STM_SAI_A_ID},
	{ .compatible = "st,stm32-sai-sub-b",
	  .data = (void *)STM_SAI_B_ID},
	{}
};
MODULE_DEVICE_TABLE(of, stm32_sai_sub_ids);

static int stm32_sai_sub_parse_of(struct platform_device *pdev,
				  struct stm32_sai_sub_data *sai)
{
	struct device_node *np = pdev->dev.of_node;
	struct resource *res;
	void __iomem *base;

	if (!np)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	dev_err(&pdev->dev, "res %pr\n", res);

	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	sai->phys_addr = res->start;
	sai->regmap = devm_regmap_init_mmio(&pdev->dev, base,
					    &stm32_sai_sub_regmap_config);

	/* Get direction property */
	if (of_property_match_string(np, "dma-names", "tx") >= 0) {
		sai->dir = SNDRV_PCM_STREAM_PLAYBACK;
	} else if (of_property_match_string(np, "dma-names", "rx") >= 0) {
		sai->dir = SNDRV_PCM_STREAM_CAPTURE;
	} else {
		dev_err(&pdev->dev, "Unsupported direction\n");
		return -EINVAL;
	}

	sai->sai_ck = devm_clk_get(&pdev->dev, "sai_ck");
	if (IS_ERR(sai->sai_ck)) {
		dev_err(&pdev->dev, "missing kernel clock sai_ck\n");
		return PTR_ERR(sai->sai_ck);
	}

	return 0;
}

static int stm32_sai_sub_dais_init(struct platform_device *pdev,
				   struct stm32_sai_sub_data *sai)
{
	sai->cpu_dai_drv = devm_kzalloc(&pdev->dev,
					sizeof(struct snd_soc_dai_driver),
					GFP_KERNEL);
	if (!sai->cpu_dai_drv)
		return -ENOMEM;

	sai->cpu_dai_drv->name = dev_name(&pdev->dev);
	if (STM_SAI_IS_PLAYBACK(sai)) {
		memcpy(sai->cpu_dai_drv, &stm32_sai_playback_dai,
		       sizeof(stm32_sai_playback_dai));
		sai->cpu_dai_drv->playback.stream_name = sai->cpu_dai_drv->name;
	} else {
		memcpy(sai->cpu_dai_drv, &stm32_sai_capture_dai,
		       sizeof(stm32_sai_capture_dai));
		sai->cpu_dai_drv->capture.stream_name = sai->cpu_dai_drv->name;
	}

	return 0;
}

static int stm32_sai_sub_probe(struct platform_device *pdev)
{
	struct stm32_sai_sub_data *sai;
	const struct of_device_id *of_id;
	int ret;

	sai = devm_kzalloc(&pdev->dev, sizeof(*sai), GFP_KERNEL);
	if (!sai)
		return -ENOMEM;

	of_id = of_match_device(stm32_sai_sub_ids, &pdev->dev);
	if (!of_id)
		return -EINVAL;
	sai->id = (uintptr_t)of_id->data;

	sai->pdev = pdev;
	platform_set_drvdata(pdev, sai);

	sai->pdata = dev_get_drvdata(pdev->dev.parent);
	if (!sai->pdata) {
		dev_err(&pdev->dev, "Parent device data not available\n");
		return -EINVAL;
	}

	ret = stm32_sai_sub_parse_of(pdev, sai);
	if (ret)
		return ret;

	ret = stm32_sai_sub_dais_init(pdev, sai);
	if (ret)
		return ret;

	ret = devm_request_irq(&pdev->dev, sai->pdata->irq, stm32_sai_isr,
			       IRQF_SHARED, dev_name(&pdev->dev), sai);
	if (ret) {
		dev_err(&pdev->dev, "irq request returned %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_component(&pdev->dev, &stm32_component,
					      sai->cpu_dai_drv, 1);
	if (ret)
		return ret;

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev,
					      &stm32_sai_pcm_config, 0);
	if (ret) {
		dev_err(&pdev->dev, "could not register pcm dma\n");
		return ret;
	}

	return 0;
}

static struct platform_driver stm32_sai_sub_driver = {
	.driver = {
		.name = "st,stm32-sai-sub",
		.of_match_table = stm32_sai_sub_ids,
	},
	.probe = stm32_sai_sub_probe,
};

module_platform_driver(stm32_sai_sub_driver);

MODULE_DESCRIPTION("STM32 Soc SAI sub-block Interface");
MODULE_AUTHOR("Olivier Moysan, <olivier.moysan@st.com>");
MODULE_ALIAS("platform:st,stm32-sai-sub");
MODULE_LICENSE("GPL v2");
