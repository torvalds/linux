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

#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/dmaengine_pcm.h>
#include <sound/pcm_params.h>

#include "stm32_sai.h"

#define SAI_FREE_PROTOCOL	0x0
#define SAI_SPDIF_PROTOCOL	0x1

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

#define STM_SAI_IS_SUB_A(x)	((x)->id == STM_SAI_A_ID)
#define STM_SAI_IS_SUB_B(x)	((x)->id == STM_SAI_B_ID)
#define STM_SAI_BLOCK_NAME(x)	(((x)->id == STM_SAI_A_ID) ? "A" : "B")

#define SAI_SYNC_NONE		0x0
#define SAI_SYNC_INTERNAL	0x1
#define SAI_SYNC_EXTERNAL	0x2

#define STM_SAI_PROTOCOL_IS_SPDIF(ip)	((ip)->spdif)
#define STM_SAI_HAS_SPDIF(x)	((x)->pdata->conf->has_spdif)
#define STM_SAI_HAS_EXT_SYNC(x) (!STM_SAI_IS_F4(sai->pdata))

#define SAI_IEC60958_BLOCK_FRAMES	192
#define SAI_IEC60958_STATUS_BYTES	24

/**
 * struct stm32_sai_sub_data - private data of SAI sub block (block A or B)
 * @pdev: device data pointer
 * @regmap: SAI register map pointer
 * @regmap_config: SAI sub block register map configuration pointer
 * @dma_params: dma configuration data for rx or tx channel
 * @cpu_dai_drv: DAI driver data pointer
 * @cpu_dai: DAI runtime data pointer
 * @substream: PCM substream data pointer
 * @pdata: SAI block parent data pointer
 * @np_sync_provider: synchronization provider node
 * @sai_ck: kernel clock feeding the SAI clock generator
 * @phys_addr: SAI registers physical base address
 * @mclk_rate: SAI block master clock frequency (Hz). set at init
 * @id: SAI sub block id corresponding to sub-block A or B
 * @dir: SAI block direction (playback or capture). set at init
 * @master: SAI block mode flag. (true=master, false=slave) set at init
 * @spdif: SAI S/PDIF iec60958 mode flag. set at init
 * @fmt: SAI block format. relevant only for custom protocols. set at init
 * @sync: SAI block synchronization mode. (none, internal or external)
 * @synco: SAI block ext sync source (provider setting). (none, sub-block A/B)
 * @synci: SAI block ext sync source (client setting). (SAI sync provider index)
 * @fs_length: frame synchronization length. depends on protocol settings
 * @slots: rx or tx slot number
 * @slot_width: rx or tx slot width in bits
 * @slot_mask: rx or tx active slots mask. set at init or at runtime
 * @data_size: PCM data width. corresponds to PCM substream width.
 * @spdif_frm_cnt: S/PDIF playback frame counter
 * @iec958: iec958 data
 * @ctrl_lock: control lock
 */
struct stm32_sai_sub_data {
	struct platform_device *pdev;
	struct regmap *regmap;
	const struct regmap_config *regmap_config;
	struct snd_dmaengine_dai_dma_data dma_params;
	struct snd_soc_dai_driver *cpu_dai_drv;
	struct snd_soc_dai *cpu_dai;
	struct snd_pcm_substream *substream;
	struct stm32_sai_data *pdata;
	struct device_node *np_sync_provider;
	struct clk *sai_ck;
	dma_addr_t phys_addr;
	unsigned int mclk_rate;
	unsigned int id;
	int dir;
	bool master;
	bool spdif;
	int fmt;
	int sync;
	int synco;
	int synci;
	int fs_length;
	int slots;
	int slot_width;
	int slot_mask;
	int data_size;
	unsigned int spdif_frm_cnt;
	struct snd_aes_iec958 iec958;
	struct mutex ctrl_lock; /* protect resources accessed by controls */
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
	case STM_SAI_PDMCR_REGX:
	case STM_SAI_PDMLY_REGX:
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
	case STM_SAI_PDMCR_REGX:
	case STM_SAI_PDMLY_REGX:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config stm32_sai_sub_regmap_config_f4 = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = STM_SAI_DR_REGX,
	.readable_reg = stm32_sai_sub_readable_reg,
	.volatile_reg = stm32_sai_sub_volatile_reg,
	.writeable_reg = stm32_sai_sub_writeable_reg,
	.fast_io = true,
};

static const struct regmap_config stm32_sai_sub_regmap_config_h7 = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.max_register = STM_SAI_PDMLY_REGX,
	.readable_reg = stm32_sai_sub_readable_reg,
	.volatile_reg = stm32_sai_sub_volatile_reg,
	.writeable_reg = stm32_sai_sub_writeable_reg,
	.fast_io = true,
};

static int snd_pcm_iec958_info(struct snd_kcontrol *kcontrol,
			       struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_IEC958;
	uinfo->count = 1;

	return 0;
}

static int snd_pcm_iec958_get(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *uctl)
{
	struct stm32_sai_sub_data *sai = snd_kcontrol_chip(kcontrol);

	mutex_lock(&sai->ctrl_lock);
	memcpy(uctl->value.iec958.status, sai->iec958.status, 4);
	mutex_unlock(&sai->ctrl_lock);

	return 0;
}

static int snd_pcm_iec958_put(struct snd_kcontrol *kcontrol,
			      struct snd_ctl_elem_value *uctl)
{
	struct stm32_sai_sub_data *sai = snd_kcontrol_chip(kcontrol);

	mutex_lock(&sai->ctrl_lock);
	memcpy(sai->iec958.status, uctl->value.iec958.status, 4);
	mutex_unlock(&sai->ctrl_lock);

	return 0;
}

static const struct snd_kcontrol_new iec958_ctls = {
	.access = (SNDRV_CTL_ELEM_ACCESS_READWRITE |
			SNDRV_CTL_ELEM_ACCESS_VOLATILE),
	.iface = SNDRV_CTL_ELEM_IFACE_PCM,
	.name = SNDRV_CTL_NAME_IEC958("", PLAYBACK, DEFAULT),
	.info = snd_pcm_iec958_info,
	.get = snd_pcm_iec958_get,
	.put = snd_pcm_iec958_put,
};

static irqreturn_t stm32_sai_isr(int irq, void *devid)
{
	struct stm32_sai_sub_data *sai = (struct stm32_sai_sub_data *)devid;
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

	if (!sai->substream) {
		dev_err(&pdev->dev, "Device stopped. Spurious IRQ 0x%x\n", sr);
		return IRQ_NONE;
	}

	if (flags & SAI_XIMR_OVRUDRIE) {
		dev_err(&pdev->dev, "IRQ %s\n",
			STM_SAI_IS_PLAYBACK(sai) ? "underrun" : "overrun");
		status = SNDRV_PCM_STATE_XRUN;
	}

	if (flags & SAI_XIMR_MUTEDETIE)
		dev_dbg(&pdev->dev, "IRQ mute detected\n");

	if (flags & SAI_XIMR_WCKCFGIE) {
		dev_err(&pdev->dev, "IRQ wrong clock configuration\n");
		status = SNDRV_PCM_STATE_DISCONNECTED;
	}

	if (flags & SAI_XIMR_CNRDYIE)
		dev_err(&pdev->dev, "IRQ Codec not ready\n");

	if (flags & SAI_XIMR_AFSDETIE) {
		dev_err(&pdev->dev, "IRQ Anticipated frame synchro\n");
		status = SNDRV_PCM_STATE_XRUN;
	}

	if (flags & SAI_XIMR_LFSDETIE) {
		dev_err(&pdev->dev, "IRQ Late frame synchro\n");
		status = SNDRV_PCM_STATE_XRUN;
	}

	if (status != SNDRV_PCM_STATE_RUNNING)
		snd_pcm_stop_xrun(sai->substream);

	return IRQ_HANDLED;
}

static int stm32_sai_set_sysclk(struct snd_soc_dai *cpu_dai,
				int clk_id, unsigned int freq, int dir)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int ret;

	if ((dir == SND_SOC_CLOCK_OUT) && sai->master) {
		ret = regmap_update_bits(sai->regmap, STM_SAI_CR1_REGX,
					 SAI_XCR1_NODIV,
					 (unsigned int)~SAI_XCR1_NODIV);
		if (ret < 0)
			return ret;

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

	if (STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
		dev_warn(cpu_dai->dev, "Slot setting relevant only for TDM\n");
		return 0;
	}

	dev_dbg(cpu_dai->dev, "Masks tx/rx:%#x/%#x, slots:%d, width:%d\n",
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
	int cr1, frcr = 0;
	int cr1_mask, frcr_mask = 0;
	int ret;

	dev_dbg(cpu_dai->dev, "fmt %x\n", fmt);

	/* Do not generate master by default */
	cr1 = SAI_XCR1_NODIV;
	cr1_mask = SAI_XCR1_NODIV;

	cr1_mask |= SAI_XCR1_PRTCFG_MASK;
	if (STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
		cr1 |= SAI_XCR1_PRTCFG_SET(SAI_SPDIF_PROTOCOL);
		goto conf_update;
	}

	cr1 |= SAI_XCR1_PRTCFG_SET(SAI_FREE_PROTOCOL);

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

	cr1_mask |= SAI_XCR1_CKSTR;
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

	/* Set slave mode if sub-block is synchronized with another SAI */
	if (sai->sync) {
		dev_dbg(cpu_dai->dev, "Synchronized SAI configured as slave\n");
		cr1 |= SAI_XCR1_SLAVE;
		sai->master = false;
	}

	cr1_mask |= SAI_XCR1_SLAVE;

conf_update:
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

	if (STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
		snd_pcm_hw_constraint_mask64(substream->runtime,
					     SNDRV_PCM_HW_PARAM_FORMAT,
					     SNDRV_PCM_FMTBIT_S32_LE);
		snd_pcm_hw_constraint_single(substream->runtime,
					     SNDRV_PCM_HW_PARAM_CHANNELS, 2);
	}

	ret = clk_prepare_enable(sai->sai_ck);
	if (ret < 0) {
		dev_err(cpu_dai->dev, "Failed to enable clock: %d\n", ret);
		return ret;
	}

	/* Enable ITs */

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

	/*
	 * DMA bursts increment is set to 4 words.
	 * SAI fifo threshold is set to half fifo, to keep enough space
	 * for DMA incoming bursts.
	 */
	regmap_update_bits(sai->regmap, STM_SAI_CR2_REGX,
			   SAI_XCR2_FFLUSH | SAI_XCR2_FTH_MASK,
			   SAI_XCR2_FFLUSH |
			   SAI_XCR2_FTH_SET(STM_SAI_FIFO_TH_HALF));

	/* DS bits in CR1 not set for SPDIF (size forced to 24 bits).*/
	if (STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
		sai->spdif_frm_cnt = 0;
		return 0;
	}

	/* Mode, data format and channel config */
	cr1_mask = SAI_XCR1_DS_MASK;
	switch (params_format(params)) {
	case SNDRV_PCM_FORMAT_S8:
		cr1 = SAI_XCR1_DS_SET(SAI_DATASIZE_8);
		break;
	case SNDRV_PCM_FORMAT_S16_LE:
		cr1 = SAI_XCR1_DS_SET(SAI_DATASIZE_16);
		break;
	case SNDRV_PCM_FORMAT_S32_LE:
		cr1 = SAI_XCR1_DS_SET(SAI_DATASIZE_32);
		break;
	default:
		dev_err(cpu_dai->dev, "Data format not supported");
		return -EINVAL;
	}

	cr1_mask |= SAI_XCR1_MONO;
	if ((sai->slots == 2) && (params_channels(params) == 1))
		cr1 |= SAI_XCR1_MONO;

	ret = regmap_update_bits(sai->regmap, STM_SAI_CR1_REGX, cr1_mask, cr1);
	if (ret < 0) {
		dev_err(cpu_dai->dev, "Failed to update CR1 register\n");
		return ret;
	}

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

	dev_dbg(cpu_dai->dev, "Slots %d, slot width %d\n",
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

	dev_dbg(cpu_dai->dev, "Frame length %d, frame active %d\n",
		sai->fs_length, fs_active);

	regmap_update_bits(sai->regmap, STM_SAI_FRCR_REGX, frcr_mask, frcr);

	if ((sai->fmt & SND_SOC_DAIFMT_FORMAT_MASK) == SND_SOC_DAIFMT_LSB) {
		offset = sai->slot_width - sai->data_size;

		regmap_update_bits(sai->regmap, STM_SAI_SLOTR_REGX,
				   SAI_XSLOTR_FBOFF_MASK,
				   SAI_XSLOTR_FBOFF_SET(offset));
	}
}

static void stm32_sai_init_iec958_status(struct stm32_sai_sub_data *sai)
{
	unsigned char *cs = sai->iec958.status;

	cs[0] = IEC958_AES0_CON_NOT_COPYRIGHT | IEC958_AES0_CON_EMPHASIS_NONE;
	cs[1] = IEC958_AES1_CON_GENERAL;
	cs[2] = IEC958_AES2_CON_SOURCE_UNSPEC | IEC958_AES2_CON_CHANNEL_UNSPEC;
	cs[3] = IEC958_AES3_CON_CLOCK_1000PPM | IEC958_AES3_CON_FS_NOTID;
}

static void stm32_sai_set_iec958_status(struct stm32_sai_sub_data *sai,
					struct snd_pcm_runtime *runtime)
{
	if (!runtime)
		return;

	/* Force the sample rate according to runtime rate */
	mutex_lock(&sai->ctrl_lock);
	switch (runtime->rate) {
	case 22050:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_22050;
		break;
	case 44100:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_44100;
		break;
	case 88200:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_88200;
		break;
	case 176400:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_176400;
		break;
	case 24000:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_24000;
		break;
	case 48000:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_48000;
		break;
	case 96000:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_96000;
		break;
	case 192000:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_192000;
		break;
	case 32000:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_32000;
		break;
	default:
		sai->iec958.status[3] = IEC958_AES3_CON_FS_NOTID;
		break;
	}
	mutex_unlock(&sai->ctrl_lock);
}

static int stm32_sai_configure_clock(struct snd_soc_dai *cpu_dai,
				     struct snd_pcm_hw_params *params)
{
	struct stm32_sai_sub_data *sai = snd_soc_dai_get_drvdata(cpu_dai);
	int cr1, mask, div = 0;
	int sai_clk_rate, mclk_ratio, den, ret;
	int version = sai->pdata->conf->version;
	unsigned int rate = params_rate(params);

	if (!sai->mclk_rate) {
		dev_err(cpu_dai->dev, "Mclk rate is null\n");
		return -EINVAL;
	}

	if (!(rate % 11025))
		clk_set_parent(sai->sai_ck, sai->pdata->clk_x11k);
	else
		clk_set_parent(sai->sai_ck, sai->pdata->clk_x8k);
	sai_clk_rate = clk_get_rate(sai->sai_ck);

	if (STM_SAI_IS_F4(sai->pdata)) {
		/*
		 * mclk_rate = 256 * fs
		 * MCKDIV = 0 if sai_ck < 3/2 * mclk_rate
		 * MCKDIV = sai_ck / (2 * mclk_rate) otherwise
		 */
		if (2 * sai_clk_rate >= 3 * sai->mclk_rate)
			div = DIV_ROUND_CLOSEST(sai_clk_rate,
						2 * sai->mclk_rate);
	} else {
		/*
		 * TDM mode :
		 *   mclk on
		 *      MCKDIV = sai_ck / (ws x 256)	(NOMCK=0. OSR=0)
		 *      MCKDIV = sai_ck / (ws x 512)	(NOMCK=0. OSR=1)
		 *   mclk off
		 *      MCKDIV = sai_ck / (frl x ws)	(NOMCK=1)
		 * Note: NOMCK/NODIV correspond to same bit.
		 */
		if (STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
			div = DIV_ROUND_CLOSEST(sai_clk_rate,
						(params_rate(params) * 128));
		} else {
			if (sai->mclk_rate) {
				mclk_ratio = sai->mclk_rate / rate;
				if (mclk_ratio == 512) {
					mask = SAI_XCR1_OSR;
					cr1 = SAI_XCR1_OSR;
				} else if (mclk_ratio != 256) {
					dev_err(cpu_dai->dev,
						"Wrong mclk ratio %d\n",
						mclk_ratio);
					return -EINVAL;
				}
				div = DIV_ROUND_CLOSEST(sai_clk_rate,
							sai->mclk_rate);
			} else {
				/* mclk-fs not set, master clock not active */
				den = sai->fs_length * params_rate(params);
				div = DIV_ROUND_CLOSEST(sai_clk_rate, den);
			}
		}
	}

	if (div > SAI_XCR1_MCKDIV_MAX(version)) {
		dev_err(cpu_dai->dev, "Divider %d out of range\n", div);
		return -EINVAL;
	}
	dev_dbg(cpu_dai->dev, "SAI clock %d, divider %d\n", sai_clk_rate, div);

	mask = SAI_XCR1_MCKDIV_MASK(SAI_XCR1_MCKDIV_WIDTH(version));
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

	if (STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
		/* Rate not already set in runtime structure */
		substream->runtime->rate = params_rate(params);
		stm32_sai_set_iec958_status(sai, substream->runtime);
	} else {
		ret = stm32_sai_set_slots(cpu_dai);
		if (ret < 0)
			return ret;
		stm32_sai_set_frame(cpu_dai);
	}

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

		regmap_update_bits(sai->regmap, STM_SAI_IMR_REGX,
				   SAI_XIMR_MASK, 0);

		regmap_update_bits(sai->regmap, STM_SAI_CR1_REGX,
				   SAI_XCR1_SAIEN,
				   (unsigned int)~SAI_XCR1_SAIEN);

		ret = regmap_update_bits(sai->regmap, STM_SAI_CR1_REGX,
					 SAI_XCR1_DMAEN,
					 (unsigned int)~SAI_XCR1_DMAEN);
		if (ret < 0)
			dev_err(cpu_dai->dev, "Failed to update CR1 register\n");

		if (STM_SAI_PROTOCOL_IS_SPDIF(sai))
			sai->spdif_frm_cnt = 0;
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

	regmap_update_bits(sai->regmap, STM_SAI_CR1_REGX, SAI_XCR1_NODIV,
			   SAI_XCR1_NODIV);

	clk_disable_unprepare(sai->sai_ck);
	sai->substream = NULL;
}

static int stm32_sai_pcm_new(struct snd_soc_pcm_runtime *rtd,
			     struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = dev_get_drvdata(cpu_dai->dev);
	struct snd_kcontrol_new knew = iec958_ctls;

	if (STM_SAI_PROTOCOL_IS_SPDIF(sai)) {
		dev_dbg(&sai->pdev->dev, "%s: register iec controls", __func__);
		knew.device = rtd->pcm->device;
		return snd_ctl_add(rtd->pcm->card, snd_ctl_new1(&knew, sai));
	}

	return 0;
}

static int stm32_sai_dai_probe(struct snd_soc_dai *cpu_dai)
{
	struct stm32_sai_sub_data *sai = dev_get_drvdata(cpu_dai->dev);
	int cr1 = 0, cr1_mask;

	sai->dma_params.addr = (dma_addr_t)(sai->phys_addr + STM_SAI_DR_REGX);
	/*
	 * DMA supports 4, 8 or 16 burst sizes. Burst size 4 is the best choice,
	 * as it allows bytes, half-word and words transfers. (See DMA fifos
	 * constraints).
	 */
	sai->dma_params.maxburst = 4;
	/* Buswidth will be set by framework at runtime */
	sai->dma_params.addr_width = DMA_SLAVE_BUSWIDTH_UNDEFINED;

	if (STM_SAI_IS_PLAYBACK(sai))
		snd_soc_dai_init_dma_data(cpu_dai, &sai->dma_params, NULL);
	else
		snd_soc_dai_init_dma_data(cpu_dai, NULL, &sai->dma_params);

	/* Next settings are not relevant for spdif mode */
	if (STM_SAI_PROTOCOL_IS_SPDIF(sai))
		return 0;

	cr1_mask = SAI_XCR1_RX_TX;
	if (STM_SAI_IS_CAPTURE(sai))
		cr1 |= SAI_XCR1_RX_TX;

	/* Configure synchronization */
	if (sai->sync == SAI_SYNC_EXTERNAL) {
		/* Configure synchro client and provider */
		sai->pdata->set_sync(sai->pdata, sai->np_sync_provider,
				     sai->synco, sai->synci);
	}

	cr1_mask |= SAI_XCR1_SYNCEN_MASK;
	cr1 |= SAI_XCR1_SYNCEN_SET(sai->sync);

	return regmap_update_bits(sai->regmap, STM_SAI_CR1_REGX, cr1_mask, cr1);
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

static int stm32_sai_pcm_process_spdif(struct snd_pcm_substream *substream,
				       int channel, unsigned long hwoff,
				       void *buf, unsigned long bytes)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	struct stm32_sai_sub_data *sai = dev_get_drvdata(cpu_dai->dev);
	int *ptr = (int *)(runtime->dma_area + hwoff +
			   channel * (runtime->dma_bytes / runtime->channels));
	ssize_t cnt = bytes_to_samples(runtime, bytes);
	unsigned int frm_cnt = sai->spdif_frm_cnt;
	unsigned int byte;
	unsigned int mask;

	do {
		*ptr = ((*ptr >> 8) & 0x00ffffff);

		/* Set channel status bit */
		byte = frm_cnt >> 3;
		mask = 1 << (frm_cnt - (byte << 3));
		if (sai->iec958.status[byte] & mask)
			*ptr |= 0x04000000;
		ptr++;

		if (!(cnt % 2))
			frm_cnt++;

		if (frm_cnt == SAI_IEC60958_BLOCK_FRAMES)
			frm_cnt = 0;
	} while (--cnt);
	sai->spdif_frm_cnt = frm_cnt;

	return 0;
}

/* No support of mmap in S/PDIF mode */
static const struct snd_pcm_hardware stm32_sai_pcm_hw_spdif = {
	.info = SNDRV_PCM_INFO_INTERLEAVED,
	.buffer_bytes_max = 8 * PAGE_SIZE,
	.period_bytes_min = 1024,
	.period_bytes_max = PAGE_SIZE,
	.periods_min = 2,
	.periods_max = 8,
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
		.pcm_new = stm32_sai_pcm_new,
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
	.pcm_hardware = &stm32_sai_pcm_hw,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
};

static const struct snd_dmaengine_pcm_config stm32_sai_pcm_config_spdif = {
	.pcm_hardware = &stm32_sai_pcm_hw_spdif,
	.prepare_slave_config = snd_dmaengine_pcm_prepare_slave_config,
	.process = stm32_sai_pcm_process_spdif,
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
	struct of_phandle_args args;
	int ret;

	if (!np)
		return -ENODEV;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	sai->phys_addr = res->start;

	sai->regmap_config = &stm32_sai_sub_regmap_config_f4;
	/* Note: PDM registers not available for H7 sub-block B */
	if (STM_SAI_IS_H7(sai->pdata) && STM_SAI_IS_SUB_A(sai))
		sai->regmap_config = &stm32_sai_sub_regmap_config_h7;

	sai->regmap = devm_regmap_init_mmio_clk(&pdev->dev, "sai_ck",
						base, sai->regmap_config);
	if (IS_ERR(sai->regmap)) {
		dev_err(&pdev->dev, "Failed to initialize MMIO\n");
		return PTR_ERR(sai->regmap);
	}

	/* Get direction property */
	if (of_property_match_string(np, "dma-names", "tx") >= 0) {
		sai->dir = SNDRV_PCM_STREAM_PLAYBACK;
	} else if (of_property_match_string(np, "dma-names", "rx") >= 0) {
		sai->dir = SNDRV_PCM_STREAM_CAPTURE;
	} else {
		dev_err(&pdev->dev, "Unsupported direction\n");
		return -EINVAL;
	}

	/* Get spdif iec60958 property */
	sai->spdif = false;
	if (of_get_property(np, "st,iec60958", NULL)) {
		if (!STM_SAI_HAS_SPDIF(sai) ||
		    sai->dir == SNDRV_PCM_STREAM_CAPTURE) {
			dev_err(&pdev->dev, "S/PDIF IEC60958 not supported\n");
			return -EINVAL;
		}
		stm32_sai_init_iec958_status(sai);
		sai->spdif = true;
		sai->master = true;
	}

	/* Get synchronization property */
	args.np = NULL;
	ret = of_parse_phandle_with_fixed_args(np, "st,sync", 1, 0, &args);
	if (ret < 0  && ret != -ENOENT) {
		dev_err(&pdev->dev, "Failed to get st,sync property\n");
		return ret;
	}

	sai->sync = SAI_SYNC_NONE;
	if (args.np) {
		if (args.np == np) {
			dev_err(&pdev->dev, "%s sync own reference\n",
				np->name);
			of_node_put(args.np);
			return -EINVAL;
		}

		sai->np_sync_provider  = of_get_parent(args.np);
		if (!sai->np_sync_provider) {
			dev_err(&pdev->dev, "%s parent node not found\n",
				np->name);
			of_node_put(args.np);
			return -ENODEV;
		}

		sai->sync = SAI_SYNC_INTERNAL;
		if (sai->np_sync_provider != sai->pdata->pdev->dev.of_node) {
			if (!STM_SAI_HAS_EXT_SYNC(sai)) {
				dev_err(&pdev->dev,
					"External synchro not supported\n");
				of_node_put(args.np);
				return -EINVAL;
			}
			sai->sync = SAI_SYNC_EXTERNAL;

			sai->synci = args.args[0];
			if (sai->synci < 1 ||
			    (sai->synci > (SAI_GCR_SYNCIN_MAX + 1))) {
				dev_err(&pdev->dev, "Wrong SAI index\n");
				of_node_put(args.np);
				return -EINVAL;
			}

			if (of_property_match_string(args.np, "compatible",
						     "st,stm32-sai-sub-a") >= 0)
				sai->synco = STM_SAI_SYNC_OUT_A;

			if (of_property_match_string(args.np, "compatible",
						     "st,stm32-sai-sub-b") >= 0)
				sai->synco = STM_SAI_SYNC_OUT_B;

			if (!sai->synco) {
				dev_err(&pdev->dev, "Unknown SAI sub-block\n");
				of_node_put(args.np);
				return -EINVAL;
			}
		}

		dev_dbg(&pdev->dev, "%s synchronized with %s\n",
			pdev->name, args.np->full_name);
	}

	of_node_put(args.np);
	sai->sai_ck = devm_clk_get(&pdev->dev, "sai_ck");
	if (IS_ERR(sai->sai_ck)) {
		dev_err(&pdev->dev, "Missing kernel clock sai_ck\n");
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

	if (STM_SAI_IS_PLAYBACK(sai)) {
		memcpy(sai->cpu_dai_drv, &stm32_sai_playback_dai,
		       sizeof(stm32_sai_playback_dai));
		sai->cpu_dai_drv->playback.stream_name = sai->cpu_dai_drv->name;
	} else {
		memcpy(sai->cpu_dai_drv, &stm32_sai_capture_dai,
		       sizeof(stm32_sai_capture_dai));
		sai->cpu_dai_drv->capture.stream_name = sai->cpu_dai_drv->name;
	}
	sai->cpu_dai_drv->name = dev_name(&pdev->dev);

	return 0;
}

static int stm32_sai_sub_probe(struct platform_device *pdev)
{
	struct stm32_sai_sub_data *sai;
	const struct of_device_id *of_id;
	const struct snd_dmaengine_pcm_config *conf = &stm32_sai_pcm_config;
	int ret;

	sai = devm_kzalloc(&pdev->dev, sizeof(*sai), GFP_KERNEL);
	if (!sai)
		return -ENOMEM;

	of_id = of_match_device(stm32_sai_sub_ids, &pdev->dev);
	if (!of_id)
		return -EINVAL;
	sai->id = (uintptr_t)of_id->data;

	sai->pdev = pdev;
	mutex_init(&sai->ctrl_lock);
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
		dev_err(&pdev->dev, "IRQ request returned %d\n", ret);
		return ret;
	}

	ret = devm_snd_soc_register_component(&pdev->dev, &stm32_component,
					      sai->cpu_dai_drv, 1);
	if (ret)
		return ret;

	if (STM_SAI_PROTOCOL_IS_SPDIF(sai))
		conf = &stm32_sai_pcm_config_spdif;

	ret = devm_snd_dmaengine_pcm_register(&pdev->dev, conf, 0);
	if (ret) {
		dev_err(&pdev->dev, "Could not register pcm dma\n");
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
MODULE_AUTHOR("Olivier Moysan <olivier.moysan@st.com>");
MODULE_ALIAS("platform:st,stm32-sai-sub");
MODULE_LICENSE("GPL v2");
