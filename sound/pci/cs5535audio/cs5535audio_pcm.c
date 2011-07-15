/*
 * Driver for audio on multifunction CS5535 companion device
 * Copyright (C) Jaya Kumar
 *
 * Based on Jaroslav Kysela and Takashi Iwai's examples.
 * This work was sponsored by CIS(M) Sdn Bhd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 * todo: add be fmt support, spdif, pm
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/initval.h>
#include <sound/asoundef.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/ac97_codec.h>
#include "cs5535audio.h"

static struct snd_pcm_hardware snd_cs5535audio_playback =
{
	.info =			(
				SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_INTERLEAVED |
		 		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 		SNDRV_PCM_INFO_MMAP_VALID |
		 		SNDRV_PCM_INFO_PAUSE |
				SNDRV_PCM_INFO_RESUME
				),
	.formats =		(
				SNDRV_PCM_FMTBIT_S16_LE
				),
	.rates =		(
				SNDRV_PCM_RATE_CONTINUOUS |
				SNDRV_PCM_RATE_8000_48000
				),
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(64*1024 - 16),
	.periods_min =		1,
	.periods_max =		CS5535AUDIO_MAX_DESCRIPTORS,
	.fifo_size =		0,
};

static struct snd_pcm_hardware snd_cs5535audio_capture =
{
	.info =			(
				SNDRV_PCM_INFO_MMAP |
				SNDRV_PCM_INFO_INTERLEAVED |
		 		SNDRV_PCM_INFO_BLOCK_TRANSFER |
		 		SNDRV_PCM_INFO_MMAP_VALID
				),
	.formats =		(
				SNDRV_PCM_FMTBIT_S16_LE
				),
	.rates =		(
				SNDRV_PCM_RATE_CONTINUOUS |
				SNDRV_PCM_RATE_8000_48000
				),
	.rate_min =		4000,
	.rate_max =		48000,
	.channels_min =		2,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(64*1024 - 16),
	.periods_min =		1,
	.periods_max =		CS5535AUDIO_MAX_DESCRIPTORS,
	.fifo_size =		0,
};

static int snd_cs5535audio_playback_open(struct snd_pcm_substream *substream)
{
	int err;
	struct cs5535audio *cs5535au = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw = snd_cs5535audio_playback;
	runtime->hw.rates = cs5535au->ac97->rates[AC97_RATES_FRONT_DAC];
	snd_pcm_limit_hw_rates(runtime);
	cs5535au->playback_substream = substream;
	runtime->private_data = &(cs5535au->dmas[CS5535AUDIO_DMA_PLAYBACK]);
	if ((err = snd_pcm_hw_constraint_integer(runtime,
				SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;

	return 0;
}

static int snd_cs5535audio_playback_close(struct snd_pcm_substream *substream)
{
	return 0;
}

#define CS5535AUDIO_DESC_LIST_SIZE \
	PAGE_ALIGN(CS5535AUDIO_MAX_DESCRIPTORS * sizeof(struct cs5535audio_dma_desc))

static int cs5535audio_build_dma_packets(struct cs5535audio *cs5535au,
					 struct cs5535audio_dma *dma,
					 struct snd_pcm_substream *substream,
					 unsigned int periods,
					 unsigned int period_bytes)
{
	unsigned int i;
	u32 addr, desc_addr, jmpprd_addr;
	struct cs5535audio_dma_desc *lastdesc;

	if (periods > CS5535AUDIO_MAX_DESCRIPTORS)
		return -ENOMEM;

	if (dma->desc_buf.area == NULL) {
		if (snd_dma_alloc_pages(SNDRV_DMA_TYPE_DEV,
					snd_dma_pci_data(cs5535au->pci),
					CS5535AUDIO_DESC_LIST_SIZE+1,
					&dma->desc_buf) < 0)
			return -ENOMEM;
		dma->period_bytes = dma->periods = 0;
	}

	if (dma->periods == periods && dma->period_bytes == period_bytes)
		return 0;

	/* the u32 cast is okay because in snd*create we successfully told
   	   pci alloc that we're only 32 bit capable so the uppper will be 0 */
	addr = (u32) substream->runtime->dma_addr;
	desc_addr = (u32) dma->desc_buf.addr;
	for (i = 0; i < periods; i++) {
		struct cs5535audio_dma_desc *desc =
			&((struct cs5535audio_dma_desc *) dma->desc_buf.area)[i];
		desc->addr = cpu_to_le32(addr);
		desc->size = cpu_to_le32(period_bytes);
		desc->ctlreserved = cpu_to_le16(PRD_EOP);
		desc_addr += sizeof(struct cs5535audio_dma_desc);
		addr += period_bytes;
	}
	/* we reserved one dummy descriptor at the end to do the PRD jump */
	lastdesc = &((struct cs5535audio_dma_desc *) dma->desc_buf.area)[periods];
	lastdesc->addr = cpu_to_le32((u32) dma->desc_buf.addr);
	lastdesc->size = 0;
	lastdesc->ctlreserved = cpu_to_le16(PRD_JMP);
	jmpprd_addr = cpu_to_le32(lastdesc->addr +
				  (sizeof(struct cs5535audio_dma_desc)*periods));

	dma->substream = substream;
	dma->period_bytes = period_bytes;
	dma->periods = periods;
	spin_lock_irq(&cs5535au->reg_lock);
	dma->ops->disable_dma(cs5535au);
	dma->ops->setup_prd(cs5535au, jmpprd_addr);
	spin_unlock_irq(&cs5535au->reg_lock);
	return 0;
}

static void cs5535audio_playback_enable_dma(struct cs5535audio *cs5535au)
{
	cs_writeb(cs5535au, ACC_BM0_CMD, BM_CTL_EN);
}

static void cs5535audio_playback_disable_dma(struct cs5535audio *cs5535au)
{
	cs_writeb(cs5535au, ACC_BM0_CMD, 0);
}

static void cs5535audio_playback_pause_dma(struct cs5535audio *cs5535au)
{
	cs_writeb(cs5535au, ACC_BM0_CMD, BM_CTL_PAUSE);
}

static void cs5535audio_playback_setup_prd(struct cs5535audio *cs5535au,
					   u32 prd_addr)
{
	cs_writel(cs5535au, ACC_BM0_PRD, prd_addr);
}

static u32 cs5535audio_playback_read_prd(struct cs5535audio *cs5535au)
{
	return cs_readl(cs5535au, ACC_BM0_PRD);
}

static u32 cs5535audio_playback_read_dma_pntr(struct cs5535audio *cs5535au)
{
	return cs_readl(cs5535au, ACC_BM0_PNTR);
}

static void cs5535audio_capture_enable_dma(struct cs5535audio *cs5535au)
{
	cs_writeb(cs5535au, ACC_BM1_CMD, BM_CTL_EN);
}

static void cs5535audio_capture_disable_dma(struct cs5535audio *cs5535au)
{
	cs_writeb(cs5535au, ACC_BM1_CMD, 0);
}

static void cs5535audio_capture_pause_dma(struct cs5535audio *cs5535au)
{
	cs_writeb(cs5535au, ACC_BM1_CMD, BM_CTL_PAUSE);
}

static void cs5535audio_capture_setup_prd(struct cs5535audio *cs5535au,
					  u32 prd_addr)
{
	cs_writel(cs5535au, ACC_BM1_PRD, prd_addr);
}

static u32 cs5535audio_capture_read_prd(struct cs5535audio *cs5535au)
{
	return cs_readl(cs5535au, ACC_BM1_PRD);
}

static u32 cs5535audio_capture_read_dma_pntr(struct cs5535audio *cs5535au)
{
	return cs_readl(cs5535au, ACC_BM1_PNTR);
}

static void cs5535audio_clear_dma_packets(struct cs5535audio *cs5535au,
					  struct cs5535audio_dma *dma,
					  struct snd_pcm_substream *substream)
{
	snd_dma_free_pages(&dma->desc_buf);
	dma->desc_buf.area = NULL;
	dma->substream = NULL;
}

static int snd_cs5535audio_hw_params(struct snd_pcm_substream *substream,
				     struct snd_pcm_hw_params *hw_params)
{
	struct cs5535audio *cs5535au = snd_pcm_substream_chip(substream);
	struct cs5535audio_dma *dma = substream->runtime->private_data;
	int err;

	err = snd_pcm_lib_malloc_pages(substream,
					params_buffer_bytes(hw_params));
	if (err < 0)
		return err;
	dma->buf_addr = substream->runtime->dma_addr;
	dma->buf_bytes = params_buffer_bytes(hw_params);

	err = cs5535audio_build_dma_packets(cs5535au, dma, substream,
					    params_periods(hw_params),
					    params_period_bytes(hw_params));
	if (!err)
		dma->pcm_open_flag = 1;

	return err;
}

static int snd_cs5535audio_hw_free(struct snd_pcm_substream *substream)
{
	struct cs5535audio *cs5535au = snd_pcm_substream_chip(substream);
	struct cs5535audio_dma *dma = substream->runtime->private_data;

	if (dma->pcm_open_flag) {
		if (substream == cs5535au->playback_substream)
			snd_ac97_update_power(cs5535au->ac97,
					AC97_PCM_FRONT_DAC_RATE, 0);
		else
			snd_ac97_update_power(cs5535au->ac97,
					AC97_PCM_LR_ADC_RATE, 0);
		dma->pcm_open_flag = 0;
	}
	cs5535audio_clear_dma_packets(cs5535au, dma, substream);
	return snd_pcm_lib_free_pages(substream);
}

static int snd_cs5535audio_playback_prepare(struct snd_pcm_substream *substream)
{
	struct cs5535audio *cs5535au = snd_pcm_substream_chip(substream);
	return snd_ac97_set_rate(cs5535au->ac97, AC97_PCM_FRONT_DAC_RATE,
				 substream->runtime->rate);
}

static int snd_cs5535audio_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct cs5535audio *cs5535au = snd_pcm_substream_chip(substream);
	struct cs5535audio_dma *dma = substream->runtime->private_data;
	int err = 0;

	spin_lock(&cs5535au->reg_lock);
	switch (cmd) {
	case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
		dma->ops->pause_dma(cs5535au);
		break;
	case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
		dma->ops->enable_dma(cs5535au);
		break;
	case SNDRV_PCM_TRIGGER_START:
		dma->ops->enable_dma(cs5535au);
		break;
	case SNDRV_PCM_TRIGGER_RESUME:
		dma->ops->enable_dma(cs5535au);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
		dma->ops->disable_dma(cs5535au);
		break;
	case SNDRV_PCM_TRIGGER_SUSPEND:
		dma->ops->disable_dma(cs5535au);
		break;
	default:
		snd_printk(KERN_ERR "unhandled trigger\n");
		err = -EINVAL;
		break;
	}
	spin_unlock(&cs5535au->reg_lock);
	return err;
}

static snd_pcm_uframes_t snd_cs5535audio_pcm_pointer(struct snd_pcm_substream
							*substream)
{
	struct cs5535audio *cs5535au = snd_pcm_substream_chip(substream);
	u32 curdma;
	struct cs5535audio_dma *dma;

	dma = substream->runtime->private_data;
	curdma = dma->ops->read_dma_pntr(cs5535au);
	if (curdma < dma->buf_addr) {
		snd_printk(KERN_ERR "curdma=%x < %x bufaddr.\n",
					curdma, dma->buf_addr);
		return 0;
	}
	curdma -= dma->buf_addr;
	if (curdma >= dma->buf_bytes) {
		snd_printk(KERN_ERR "diff=%x >= %x buf_bytes.\n",
					curdma, dma->buf_bytes);
		return 0;
	}
	return bytes_to_frames(substream->runtime, curdma);
}

static int snd_cs5535audio_capture_open(struct snd_pcm_substream *substream)
{
	int err;
	struct cs5535audio *cs5535au = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	runtime->hw = snd_cs5535audio_capture;
	runtime->hw.rates = cs5535au->ac97->rates[AC97_RATES_ADC];
	snd_pcm_limit_hw_rates(runtime);
	cs5535au->capture_substream = substream;
	runtime->private_data = &(cs5535au->dmas[CS5535AUDIO_DMA_CAPTURE]);
	if ((err = snd_pcm_hw_constraint_integer(runtime,
					 SNDRV_PCM_HW_PARAM_PERIODS)) < 0)
		return err;
	olpc_capture_open(cs5535au->ac97);
	return 0;
}

static int snd_cs5535audio_capture_close(struct snd_pcm_substream *substream)
{
	struct cs5535audio *cs5535au = snd_pcm_substream_chip(substream);
	olpc_capture_close(cs5535au->ac97);
	return 0;
}

static int snd_cs5535audio_capture_prepare(struct snd_pcm_substream *substream)
{
	struct cs5535audio *cs5535au = snd_pcm_substream_chip(substream);
	return snd_ac97_set_rate(cs5535au->ac97, AC97_PCM_LR_ADC_RATE,
				 substream->runtime->rate);
}

static struct snd_pcm_ops snd_cs5535audio_playback_ops = {
	.open =		snd_cs5535audio_playback_open,
	.close =	snd_cs5535audio_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_cs5535audio_hw_params,
	.hw_free =	snd_cs5535audio_hw_free,
	.prepare =	snd_cs5535audio_playback_prepare,
	.trigger =	snd_cs5535audio_trigger,
	.pointer =	snd_cs5535audio_pcm_pointer,
};

static struct snd_pcm_ops snd_cs5535audio_capture_ops = {
	.open =		snd_cs5535audio_capture_open,
	.close =	snd_cs5535audio_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_cs5535audio_hw_params,
	.hw_free =	snd_cs5535audio_hw_free,
	.prepare =	snd_cs5535audio_capture_prepare,
	.trigger =	snd_cs5535audio_trigger,
	.pointer =	snd_cs5535audio_pcm_pointer,
};

static struct cs5535audio_dma_ops snd_cs5535audio_playback_dma_ops = {
        .type = CS5535AUDIO_DMA_PLAYBACK,
        .enable_dma = cs5535audio_playback_enable_dma,
        .disable_dma = cs5535audio_playback_disable_dma,
        .setup_prd = cs5535audio_playback_setup_prd,
        .read_prd = cs5535audio_playback_read_prd,
        .pause_dma = cs5535audio_playback_pause_dma,
        .read_dma_pntr = cs5535audio_playback_read_dma_pntr,
};

static struct cs5535audio_dma_ops snd_cs5535audio_capture_dma_ops = {
        .type = CS5535AUDIO_DMA_CAPTURE,
        .enable_dma = cs5535audio_capture_enable_dma,
        .disable_dma = cs5535audio_capture_disable_dma,
        .setup_prd = cs5535audio_capture_setup_prd,
        .read_prd = cs5535audio_capture_read_prd,
        .pause_dma = cs5535audio_capture_pause_dma,
        .read_dma_pntr = cs5535audio_capture_read_dma_pntr,
};

int __devinit snd_cs5535audio_pcm(struct cs5535audio *cs5535au)
{
	struct snd_pcm *pcm;
	int err;

	err = snd_pcm_new(cs5535au->card, "CS5535 Audio", 0, 1, 1, &pcm);
	if (err < 0)
		return err;

	cs5535au->dmas[CS5535AUDIO_DMA_PLAYBACK].ops =
					&snd_cs5535audio_playback_dma_ops;
	cs5535au->dmas[CS5535AUDIO_DMA_CAPTURE].ops =
					&snd_cs5535audio_capture_dma_ops;
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
					&snd_cs5535audio_playback_ops);
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE,
					&snd_cs5535audio_capture_ops);

	pcm->private_data = cs5535au;
	pcm->info_flags = 0;
	strcpy(pcm->name, "CS5535 Audio");

	snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
					snd_dma_pci_data(cs5535au->pci),
					64*1024, 128*1024);
	cs5535au->pcm = pcm;

	return 0;
}

