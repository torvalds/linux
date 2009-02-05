/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 *  Routines for control of GF1 chip (PCM things)
 *
 *  InterWave chips supports interleaved DMA, but this feature isn't used in
 *  this code.
 *  
 *  This code emulates autoinit DMA transfer for playback, recording by GF1
 *  chip doesn't support autoinit DMA.
 *
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 *
 */

#include <asm/dma.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/gus.h>
#include <sound/pcm_params.h>
#include "gus_tables.h"

/* maximum rate */

#define SNDRV_GF1_PCM_RATE		48000

#define SNDRV_GF1_PCM_PFLG_NONE		0
#define SNDRV_GF1_PCM_PFLG_ACTIVE	(1<<0)
#define SNDRV_GF1_PCM_PFLG_NEUTRAL	(2<<0)

struct gus_pcm_private {
	struct snd_gus_card * gus;
	struct snd_pcm_substream *substream;
	spinlock_t lock;
	unsigned int voices;
	struct snd_gus_voice *pvoices[2];
	unsigned int memory;
	unsigned short flags;
	unsigned char voice_ctrl, ramp_ctrl;
	unsigned int bpos;
	unsigned int blocks;
	unsigned int block_size;
	unsigned int dma_size;
	wait_queue_head_t sleep;
	atomic_t dma_count;
	int final_volume;
};

static int snd_gf1_pcm_use_dma = 1;

static void snd_gf1_pcm_block_change_ack(struct snd_gus_card * gus, void *private_data)
{
	struct gus_pcm_private *pcmp = private_data;

	if (pcmp) {
		atomic_dec(&pcmp->dma_count);
		wake_up(&pcmp->sleep);
	}
}

static int snd_gf1_pcm_block_change(struct snd_pcm_substream *substream,
				    unsigned int offset,
				    unsigned int addr,
				    unsigned int count)
{
	struct snd_gf1_dma_block block;
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct gus_pcm_private *pcmp = runtime->private_data;

	count += offset & 31;
	offset &= ~31;
	/*
	snd_printk(KERN_DEBUG "block change - offset = 0x%x, count = 0x%x\n",
		   offset, count);
	*/
	memset(&block, 0, sizeof(block));
	block.cmd = SNDRV_GF1_DMA_IRQ;
	if (snd_pcm_format_unsigned(runtime->format))
		block.cmd |= SNDRV_GF1_DMA_UNSIGNED;
	if (snd_pcm_format_width(runtime->format) == 16)
		block.cmd |= SNDRV_GF1_DMA_16BIT;
	block.addr = addr & ~31;
	block.buffer = runtime->dma_area + offset;
	block.buf_addr = runtime->dma_addr + offset;
	block.count = count;
	block.private_data = pcmp;
	block.ack = snd_gf1_pcm_block_change_ack;
	if (!snd_gf1_dma_transfer_block(pcmp->gus, &block, 0, 0))
		atomic_inc(&pcmp->dma_count);
	return 0;
}

static void snd_gf1_pcm_trigger_up(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct gus_pcm_private *pcmp = runtime->private_data;
	struct snd_gus_card * gus = pcmp->gus;
	unsigned long flags;
	unsigned char voice_ctrl, ramp_ctrl;
	unsigned short rate;
	unsigned int curr, begin, end;
	unsigned short vol;
	unsigned char pan;
	unsigned int voice;

	spin_lock_irqsave(&pcmp->lock, flags);
	if (pcmp->flags & SNDRV_GF1_PCM_PFLG_ACTIVE) {
		spin_unlock_irqrestore(&pcmp->lock, flags);
		return;
	}
	pcmp->flags |= SNDRV_GF1_PCM_PFLG_ACTIVE;
	pcmp->final_volume = 0;
	spin_unlock_irqrestore(&pcmp->lock, flags);
	rate = snd_gf1_translate_freq(gus, runtime->rate << 4);
	/* enable WAVE IRQ */
	voice_ctrl = snd_pcm_format_width(runtime->format) == 16 ? 0x24 : 0x20;
	/* enable RAMP IRQ + rollover */
	ramp_ctrl = 0x24;
	if (pcmp->blocks == 1) {
		voice_ctrl |= 0x08;	/* loop enable */
		ramp_ctrl &= ~0x04;	/* disable rollover */
	}
	for (voice = 0; voice < pcmp->voices; voice++) {
		begin = pcmp->memory + voice * (pcmp->dma_size / runtime->channels);
		curr = begin + (pcmp->bpos * pcmp->block_size) / runtime->channels;
		end = curr + (pcmp->block_size / runtime->channels);
		end -= snd_pcm_format_width(runtime->format) == 16 ? 2 : 1;
		/*
		snd_printk(KERN_DEBUG "init: curr=0x%x, begin=0x%x, end=0x%x, "
			   "ctrl=0x%x, ramp=0x%x, rate=0x%x\n",
			   curr, begin, end, voice_ctrl, ramp_ctrl, rate);
		*/
		pan = runtime->channels == 2 ? (!voice ? 1 : 14) : 8;
		vol = !voice ? gus->gf1.pcm_volume_level_left : gus->gf1.pcm_volume_level_right;
		spin_lock_irqsave(&gus->reg_lock, flags);
		snd_gf1_select_voice(gus, pcmp->pvoices[voice]->number);
		snd_gf1_write8(gus, SNDRV_GF1_VB_PAN, pan);
		snd_gf1_write16(gus, SNDRV_GF1_VW_FREQUENCY, rate);
		snd_gf1_write_addr(gus, SNDRV_GF1_VA_START, begin << 4, voice_ctrl & 4);
		snd_gf1_write_addr(gus, SNDRV_GF1_VA_END, end << 4, voice_ctrl & 4);
		snd_gf1_write_addr(gus, SNDRV_GF1_VA_CURRENT, curr << 4, voice_ctrl & 4);
		snd_gf1_write16(gus, SNDRV_GF1_VW_VOLUME, SNDRV_GF1_MIN_VOLUME << 4);
		snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_RATE, 0x2f);
		snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_START, SNDRV_GF1_MIN_OFFSET);
		snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_END, vol >> 8);
		snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_CONTROL, ramp_ctrl);
		if (!gus->gf1.enh_mode) {
			snd_gf1_delay(gus);
			snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_CONTROL, ramp_ctrl);
		}
		spin_unlock_irqrestore(&gus->reg_lock, flags);
	}
	spin_lock_irqsave(&gus->reg_lock, flags);
	for (voice = 0; voice < pcmp->voices; voice++) {
		snd_gf1_select_voice(gus, pcmp->pvoices[voice]->number);
		if (gus->gf1.enh_mode)
			snd_gf1_write8(gus, SNDRV_GF1_VB_MODE, 0x00);	/* deactivate voice */
		snd_gf1_write8(gus, SNDRV_GF1_VB_ADDRESS_CONTROL, voice_ctrl);
		voice_ctrl &= ~0x20;
	}
	voice_ctrl |= 0x20;
	if (!gus->gf1.enh_mode) {
		snd_gf1_delay(gus);
		for (voice = 0; voice < pcmp->voices; voice++) {
			snd_gf1_select_voice(gus, pcmp->pvoices[voice]->number);
			snd_gf1_write8(gus, SNDRV_GF1_VB_ADDRESS_CONTROL, voice_ctrl);
			voice_ctrl &= ~0x20;	/* disable IRQ for next voice */
		}
	}
	spin_unlock_irqrestore(&gus->reg_lock, flags);
}

static void snd_gf1_pcm_interrupt_wave(struct snd_gus_card * gus,
				       struct snd_gus_voice *pvoice)
{
	struct gus_pcm_private * pcmp;
	struct snd_pcm_runtime *runtime;
	unsigned char voice_ctrl, ramp_ctrl;
	unsigned int idx;
	unsigned int end, step;

	if (!pvoice->private_data) {
		snd_printd("snd_gf1_pcm: unknown wave irq?\n");
		snd_gf1_smart_stop_voice(gus, pvoice->number);
		return;
	}
	pcmp = pvoice->private_data;
	if (pcmp == NULL) {
		snd_printd("snd_gf1_pcm: unknown wave irq?\n");
		snd_gf1_smart_stop_voice(gus, pvoice->number);
		return;
	}		
	gus = pcmp->gus;
	runtime = pcmp->substream->runtime;

	spin_lock(&gus->reg_lock);
	snd_gf1_select_voice(gus, pvoice->number);
	voice_ctrl = snd_gf1_read8(gus, SNDRV_GF1_VB_ADDRESS_CONTROL) & ~0x8b;
	ramp_ctrl = (snd_gf1_read8(gus, SNDRV_GF1_VB_VOLUME_CONTROL) & ~0xa4) | 0x03;
#if 0
	snd_gf1_select_voice(gus, pvoice->number);
	printk(KERN_DEBUG "position = 0x%x\n",
	       (snd_gf1_read_addr(gus, SNDRV_GF1_VA_CURRENT, voice_ctrl & 4) >> 4));
	snd_gf1_select_voice(gus, pcmp->pvoices[1]->number);
	printk(KERN_DEBUG "position = 0x%x\n",
	       (snd_gf1_read_addr(gus, SNDRV_GF1_VA_CURRENT, voice_ctrl & 4) >> 4));
	snd_gf1_select_voice(gus, pvoice->number);
#endif
	pcmp->bpos++;
	pcmp->bpos %= pcmp->blocks;
	if (pcmp->bpos + 1 >= pcmp->blocks) {	/* last block? */
		voice_ctrl |= 0x08;	/* enable loop */
	} else {
		ramp_ctrl |= 0x04;	/* enable rollover */
	}
	end = pcmp->memory + (((pcmp->bpos + 1) * pcmp->block_size) / runtime->channels);
	end -= voice_ctrl & 4 ? 2 : 1;
	step = pcmp->dma_size / runtime->channels;
	voice_ctrl |= 0x20;
	if (!pcmp->final_volume) {
		ramp_ctrl |= 0x20;
		ramp_ctrl &= ~0x03;
	}
	for (idx = 0; idx < pcmp->voices; idx++, end += step) {
		snd_gf1_select_voice(gus, pcmp->pvoices[idx]->number);
		snd_gf1_write_addr(gus, SNDRV_GF1_VA_END, end << 4, voice_ctrl & 4);
		snd_gf1_write8(gus, SNDRV_GF1_VB_ADDRESS_CONTROL, voice_ctrl);
		snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_CONTROL, ramp_ctrl);
		voice_ctrl &= ~0x20;
	}
	if (!gus->gf1.enh_mode) {
		snd_gf1_delay(gus);
		voice_ctrl |= 0x20;
		for (idx = 0; idx < pcmp->voices; idx++) {
			snd_gf1_select_voice(gus, pcmp->pvoices[idx]->number);
			snd_gf1_write8(gus, SNDRV_GF1_VB_ADDRESS_CONTROL, voice_ctrl);
			snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_CONTROL, ramp_ctrl);
			voice_ctrl &= ~0x20;
		}
	}
	spin_unlock(&gus->reg_lock);

	snd_pcm_period_elapsed(pcmp->substream);
#if 0
	if ((runtime->flags & SNDRV_PCM_FLG_MMAP) &&
	    *runtime->state == SNDRV_PCM_STATE_RUNNING) {
		end = pcmp->bpos * pcmp->block_size;
		if (runtime->channels > 1) {
			snd_gf1_pcm_block_change(pcmp->substream, end, pcmp->memory + (end / 2), pcmp->block_size / 2);
			snd_gf1_pcm_block_change(pcmp->substream, end + (pcmp->block_size / 2), pcmp->memory + (pcmp->dma_size / 2) + (end / 2), pcmp->block_size / 2);
		} else {
			snd_gf1_pcm_block_change(pcmp->substream, end, pcmp->memory + end, pcmp->block_size);
		}
	}
#endif
}

static void snd_gf1_pcm_interrupt_volume(struct snd_gus_card * gus,
					 struct snd_gus_voice * pvoice)
{
	unsigned short vol;
	int cvoice;
	struct gus_pcm_private *pcmp = pvoice->private_data;

	/* stop ramp, but leave rollover bit untouched */
	spin_lock(&gus->reg_lock);
	snd_gf1_select_voice(gus, pvoice->number);
	snd_gf1_ctrl_stop(gus, SNDRV_GF1_VB_VOLUME_CONTROL);
	spin_unlock(&gus->reg_lock);
	if (pcmp == NULL)
		return;
	/* are we active? */
	if (!(pcmp->flags & SNDRV_GF1_PCM_PFLG_ACTIVE))
		return;
	/* load real volume - better precision */
	cvoice = pcmp->pvoices[0] == pvoice ? 0 : 1;
	if (pcmp->substream == NULL)
		return;
	vol = !cvoice ? gus->gf1.pcm_volume_level_left : gus->gf1.pcm_volume_level_right;
	spin_lock(&gus->reg_lock);
	snd_gf1_select_voice(gus, pvoice->number);
	snd_gf1_write16(gus, SNDRV_GF1_VW_VOLUME, vol);
	pcmp->final_volume = 1;
	spin_unlock(&gus->reg_lock);
}

static void snd_gf1_pcm_volume_change(struct snd_gus_card * gus)
{
}

static int snd_gf1_pcm_poke_block(struct snd_gus_card *gus, unsigned char *buf,
				  unsigned int pos, unsigned int count,
				  int w16, int invert)
{
	unsigned int len;
	unsigned long flags;

	/*
	printk(KERN_DEBUG
	       "poke block; buf = 0x%x, pos = %i, count = %i, port = 0x%x\n",
	       (int)buf, pos, count, gus->gf1.port);
	*/
	while (count > 0) {
		len = count;
		if (len > 512)		/* limit, to allow IRQ */
			len = 512;
		count -= len;
		if (gus->interwave) {
			spin_lock_irqsave(&gus->reg_lock, flags);
			snd_gf1_write8(gus, SNDRV_GF1_GB_MEMORY_CONTROL, 0x01 | (invert ? 0x08 : 0x00));
			snd_gf1_dram_addr(gus, pos);
			if (w16) {
				outb(SNDRV_GF1_GW_DRAM_IO16, GUSP(gus, GF1REGSEL));
				outsw(GUSP(gus, GF1DATALOW), buf, len >> 1);
			} else {
				outsb(GUSP(gus, DRAM), buf, len);
			}
			spin_unlock_irqrestore(&gus->reg_lock, flags);
			buf += 512;
			pos += 512;
		} else {
			invert = invert ? 0x80 : 0x00;
			if (w16) {
				len >>= 1;
				while (len--) {
					snd_gf1_poke(gus, pos++, *buf++);
					snd_gf1_poke(gus, pos++, *buf++ ^ invert);
				}
			} else {
				while (len--)
					snd_gf1_poke(gus, pos++, *buf++ ^ invert);
			}
		}
		if (count > 0 && !in_interrupt()) {
			schedule_timeout_interruptible(1);
			if (signal_pending(current))
				return -EAGAIN;
		}
	}
	return 0;
}

static int snd_gf1_pcm_playback_copy(struct snd_pcm_substream *substream,
				     int voice,
				     snd_pcm_uframes_t pos,
				     void __user *src,
				     snd_pcm_uframes_t count)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct gus_pcm_private *pcmp = runtime->private_data;
	unsigned int bpos, len;
	
	bpos = samples_to_bytes(runtime, pos) + (voice * (pcmp->dma_size / 2));
	len = samples_to_bytes(runtime, count);
	if (snd_BUG_ON(bpos > pcmp->dma_size))
		return -EIO;
	if (snd_BUG_ON(bpos + len > pcmp->dma_size))
		return -EIO;
	if (copy_from_user(runtime->dma_area + bpos, src, len))
		return -EFAULT;
	if (snd_gf1_pcm_use_dma && len > 32) {
		return snd_gf1_pcm_block_change(substream, bpos, pcmp->memory + bpos, len);
	} else {
		struct snd_gus_card *gus = pcmp->gus;
		int err, w16, invert;

		w16 = (snd_pcm_format_width(runtime->format) == 16);
		invert = snd_pcm_format_unsigned(runtime->format);
		if ((err = snd_gf1_pcm_poke_block(gus, runtime->dma_area + bpos, pcmp->memory + bpos, len, w16, invert)) < 0)
			return err;
	}
	return 0;
}

static int snd_gf1_pcm_playback_silence(struct snd_pcm_substream *substream,
					int voice,
					snd_pcm_uframes_t pos,
					snd_pcm_uframes_t count)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct gus_pcm_private *pcmp = runtime->private_data;
	unsigned int bpos, len;
	
	bpos = samples_to_bytes(runtime, pos) + (voice * (pcmp->dma_size / 2));
	len = samples_to_bytes(runtime, count);
	if (snd_BUG_ON(bpos > pcmp->dma_size))
		return -EIO;
	if (snd_BUG_ON(bpos + len > pcmp->dma_size))
		return -EIO;
	snd_pcm_format_set_silence(runtime->format, runtime->dma_area + bpos, count);
	if (snd_gf1_pcm_use_dma && len > 32) {
		return snd_gf1_pcm_block_change(substream, bpos, pcmp->memory + bpos, len);
	} else {
		struct snd_gus_card *gus = pcmp->gus;
		int err, w16, invert;

		w16 = (snd_pcm_format_width(runtime->format) == 16);
		invert = snd_pcm_format_unsigned(runtime->format);
		if ((err = snd_gf1_pcm_poke_block(gus, runtime->dma_area + bpos, pcmp->memory + bpos, len, w16, invert)) < 0)
			return err;
	}
	return 0;
}

static int snd_gf1_pcm_playback_hw_params(struct snd_pcm_substream *substream,
					  struct snd_pcm_hw_params *hw_params)
{
	struct snd_gus_card *gus = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct gus_pcm_private *pcmp = runtime->private_data;
	int err;
	
	if ((err = snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params))) < 0)
		return err;
	if (err > 0) {	/* change */
		struct snd_gf1_mem_block *block;
		if (pcmp->memory > 0) {
			snd_gf1_mem_free(&gus->gf1.mem_alloc, pcmp->memory);
			pcmp->memory = 0;
		}
		if ((block = snd_gf1_mem_alloc(&gus->gf1.mem_alloc,
		                               SNDRV_GF1_MEM_OWNER_DRIVER,
					       "GF1 PCM",
		                               runtime->dma_bytes, 1, 32,
		                               NULL)) == NULL)
			return -ENOMEM;
		pcmp->memory = block->ptr;
	}
	pcmp->voices = params_channels(hw_params);
	if (pcmp->pvoices[0] == NULL) {
		if ((pcmp->pvoices[0] = snd_gf1_alloc_voice(pcmp->gus, SNDRV_GF1_VOICE_TYPE_PCM, 0, 0)) == NULL)
			return -ENOMEM;
		pcmp->pvoices[0]->handler_wave = snd_gf1_pcm_interrupt_wave;
		pcmp->pvoices[0]->handler_volume = snd_gf1_pcm_interrupt_volume;
		pcmp->pvoices[0]->volume_change = snd_gf1_pcm_volume_change;
		pcmp->pvoices[0]->private_data = pcmp;
	}
	if (pcmp->voices > 1 && pcmp->pvoices[1] == NULL) {
		if ((pcmp->pvoices[1] = snd_gf1_alloc_voice(pcmp->gus, SNDRV_GF1_VOICE_TYPE_PCM, 0, 0)) == NULL)
			return -ENOMEM;
		pcmp->pvoices[1]->handler_wave = snd_gf1_pcm_interrupt_wave;
		pcmp->pvoices[1]->handler_volume = snd_gf1_pcm_interrupt_volume;
		pcmp->pvoices[1]->volume_change = snd_gf1_pcm_volume_change;
		pcmp->pvoices[1]->private_data = pcmp;
	} else if (pcmp->voices == 1) {
		if (pcmp->pvoices[1]) {
			snd_gf1_free_voice(pcmp->gus, pcmp->pvoices[1]);
			pcmp->pvoices[1] = NULL;
		}
	}
	return 0;
}

static int snd_gf1_pcm_playback_hw_free(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct gus_pcm_private *pcmp = runtime->private_data;

	snd_pcm_lib_free_pages(substream);
	if (pcmp->pvoices[0]) {
		snd_gf1_free_voice(pcmp->gus, pcmp->pvoices[0]);
		pcmp->pvoices[0] = NULL;
	}
	if (pcmp->pvoices[1]) {
		snd_gf1_free_voice(pcmp->gus, pcmp->pvoices[1]);
		pcmp->pvoices[1] = NULL;
	}
	if (pcmp->memory > 0) {
		snd_gf1_mem_free(&pcmp->gus->gf1.mem_alloc, pcmp->memory);
		pcmp->memory = 0;
	}
	return 0;
}

static int snd_gf1_pcm_playback_prepare(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct gus_pcm_private *pcmp = runtime->private_data;

	pcmp->bpos = 0;
	pcmp->dma_size = snd_pcm_lib_buffer_bytes(substream);
	pcmp->block_size = snd_pcm_lib_period_bytes(substream);
	pcmp->blocks = pcmp->dma_size / pcmp->block_size;
	return 0;
}

static int snd_gf1_pcm_playback_trigger(struct snd_pcm_substream *substream,
					int cmd)
{
	struct snd_gus_card *gus = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct gus_pcm_private *pcmp = runtime->private_data;
	int voice;

	if (cmd == SNDRV_PCM_TRIGGER_START) {
		snd_gf1_pcm_trigger_up(substream);
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		spin_lock(&pcmp->lock);
		pcmp->flags &= ~SNDRV_GF1_PCM_PFLG_ACTIVE;
		spin_unlock(&pcmp->lock);
		voice = pcmp->pvoices[0]->number;
		snd_gf1_stop_voices(gus, voice, voice);
		if (pcmp->pvoices[1]) {
			voice = pcmp->pvoices[1]->number;
			snd_gf1_stop_voices(gus, voice, voice);
		}
	} else {
		return -EINVAL;
	}
	return 0;
}

static snd_pcm_uframes_t snd_gf1_pcm_playback_pointer(struct snd_pcm_substream *substream)
{
	struct snd_gus_card *gus = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct gus_pcm_private *pcmp = runtime->private_data;
	unsigned int pos;
	unsigned char voice_ctrl;

	pos = 0;
	spin_lock(&gus->reg_lock);
	if (pcmp->flags & SNDRV_GF1_PCM_PFLG_ACTIVE) {
		snd_gf1_select_voice(gus, pcmp->pvoices[0]->number);
		voice_ctrl = snd_gf1_read8(gus, SNDRV_GF1_VB_ADDRESS_CONTROL);
		pos = (snd_gf1_read_addr(gus, SNDRV_GF1_VA_CURRENT, voice_ctrl & 4) >> 4) - pcmp->memory;
		if (substream->runtime->channels > 1)
			pos <<= 1;
		pos = bytes_to_frames(runtime, pos);
	}
	spin_unlock(&gus->reg_lock);
	return pos;
}

static struct snd_ratnum clock = {
	.num = 9878400/16,
	.den_min = 2,
	.den_max = 257,
	.den_step = 1,
};

static struct snd_pcm_hw_constraint_ratnums hw_constraints_clocks  = {
	.nrats = 1,
	.rats = &clock,
};

static int snd_gf1_pcm_capture_hw_params(struct snd_pcm_substream *substream,
					 struct snd_pcm_hw_params *hw_params)
{
	struct snd_gus_card *gus = snd_pcm_substream_chip(substream);

	gus->c_dma_size = params_buffer_bytes(hw_params);
	gus->c_period_size = params_period_bytes(hw_params);
	gus->c_pos = 0;
	gus->gf1.pcm_rcntrl_reg = 0x21;		/* IRQ at end, enable & start */
	if (params_channels(hw_params) > 1)
		gus->gf1.pcm_rcntrl_reg |= 2;
	if (gus->gf1.dma2 > 3)
		gus->gf1.pcm_rcntrl_reg |= 4;
	if (snd_pcm_format_unsigned(params_format(hw_params)))
		gus->gf1.pcm_rcntrl_reg |= 0x80;
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(hw_params));
}

static int snd_gf1_pcm_capture_hw_free(struct snd_pcm_substream *substream)
{
	return snd_pcm_lib_free_pages(substream);
}

static int snd_gf1_pcm_capture_prepare(struct snd_pcm_substream *substream)
{
	struct snd_gus_card *gus = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;

	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RECORD_RATE, runtime->rate_den - 2);
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_REC_DMA_CONTROL, 0);	/* disable sampling */
	snd_gf1_i_look8(gus, SNDRV_GF1_GB_REC_DMA_CONTROL);	/* Sampling Control Register */
	snd_dma_program(gus->gf1.dma2, runtime->dma_addr, gus->c_period_size, DMA_MODE_READ);
	return 0;
}

static int snd_gf1_pcm_capture_trigger(struct snd_pcm_substream *substream,
				       int cmd)
{
	struct snd_gus_card *gus = snd_pcm_substream_chip(substream);
	int val;
	
	if (cmd == SNDRV_PCM_TRIGGER_START) {
		val = gus->gf1.pcm_rcntrl_reg;
	} else if (cmd == SNDRV_PCM_TRIGGER_STOP) {
		val = 0;
	} else {
		return -EINVAL;
	}

	spin_lock(&gus->reg_lock);
	snd_gf1_write8(gus, SNDRV_GF1_GB_REC_DMA_CONTROL, val);
	snd_gf1_look8(gus, SNDRV_GF1_GB_REC_DMA_CONTROL);
	spin_unlock(&gus->reg_lock);
	return 0;
}

static snd_pcm_uframes_t snd_gf1_pcm_capture_pointer(struct snd_pcm_substream *substream)
{
	struct snd_gus_card *gus = snd_pcm_substream_chip(substream);
	int pos = snd_dma_pointer(gus->gf1.dma2, gus->c_period_size);
	pos = bytes_to_frames(substream->runtime, (gus->c_pos + pos) % gus->c_dma_size);
	return pos;
}

static void snd_gf1_pcm_interrupt_dma_read(struct snd_gus_card * gus)
{
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_REC_DMA_CONTROL, 0);	/* disable sampling */
	snd_gf1_i_look8(gus, SNDRV_GF1_GB_REC_DMA_CONTROL);	/* Sampling Control Register */
	if (gus->pcm_cap_substream != NULL) {
		snd_gf1_pcm_capture_prepare(gus->pcm_cap_substream); 
		snd_gf1_pcm_capture_trigger(gus->pcm_cap_substream, SNDRV_PCM_TRIGGER_START);
		gus->c_pos += gus->c_period_size;
		snd_pcm_period_elapsed(gus->pcm_cap_substream);
	}
}

static struct snd_pcm_hardware snd_gf1_pcm_playback =
{
	.info =			SNDRV_PCM_INFO_NONINTERLEAVED,
	.formats		= (SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8 |
				 SNDRV_PCM_FMTBIT_S16_LE | SNDRV_PCM_FMTBIT_U16_LE),
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_48000,
	.rate_min =		5510,
	.rate_max =		48000,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static struct snd_pcm_hardware snd_gf1_pcm_capture =
{
	.info =			(SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_INTERLEAVED |
				 SNDRV_PCM_INFO_MMAP_VALID),
	.formats =		SNDRV_PCM_FMTBIT_S8 | SNDRV_PCM_FMTBIT_U8,
	.rates =		SNDRV_PCM_RATE_CONTINUOUS | SNDRV_PCM_RATE_8000_44100,
	.rate_min =		5510,
	.rate_max =		44100,
	.channels_min =		1,
	.channels_max =		2,
	.buffer_bytes_max =	(128*1024),
	.period_bytes_min =	64,
	.period_bytes_max =	(128*1024),
	.periods_min =		1,
	.periods_max =		1024,
	.fifo_size =		0,
};

static void snd_gf1_pcm_playback_free(struct snd_pcm_runtime *runtime)
{
	kfree(runtime->private_data);
}

static int snd_gf1_pcm_playback_open(struct snd_pcm_substream *substream)
{
	struct gus_pcm_private *pcmp;
	struct snd_gus_card *gus = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	pcmp = kzalloc(sizeof(*pcmp), GFP_KERNEL);
	if (pcmp == NULL)
		return -ENOMEM;
	pcmp->gus = gus;
	spin_lock_init(&pcmp->lock);
	init_waitqueue_head(&pcmp->sleep);
	atomic_set(&pcmp->dma_count, 0);

	runtime->private_data = pcmp;
	runtime->private_free = snd_gf1_pcm_playback_free;

#if 0
	printk(KERN_DEBUG "playback.buffer = 0x%lx, gf1.pcm_buffer = 0x%lx\n",
	       (long) pcm->playback.buffer, (long) gus->gf1.pcm_buffer);
#endif
	if ((err = snd_gf1_dma_init(gus)) < 0)
		return err;
	pcmp->flags = SNDRV_GF1_PCM_PFLG_NONE;
	pcmp->substream = substream;
	runtime->hw = snd_gf1_pcm_playback;
	snd_pcm_limit_isa_dma_size(gus->gf1.dma1, &runtime->hw.buffer_bytes_max);
	snd_pcm_limit_isa_dma_size(gus->gf1.dma1, &runtime->hw.period_bytes_max);
	snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 64);
	return 0;
}

static int snd_gf1_pcm_playback_close(struct snd_pcm_substream *substream)
{
	struct snd_gus_card *gus = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct gus_pcm_private *pcmp = runtime->private_data;
	
	if (!wait_event_timeout(pcmp->sleep, (atomic_read(&pcmp->dma_count) <= 0), 2*HZ))
		snd_printk(KERN_ERR "gf1 pcm - serious DMA problem\n");

	snd_gf1_dma_done(gus);	
	return 0;
}

static int snd_gf1_pcm_capture_open(struct snd_pcm_substream *substream)
{
	struct snd_pcm_runtime *runtime = substream->runtime;
	struct snd_gus_card *gus = snd_pcm_substream_chip(substream);

	gus->gf1.interrupt_handler_dma_read = snd_gf1_pcm_interrupt_dma_read;
	gus->pcm_cap_substream = substream;
	substream->runtime->hw = snd_gf1_pcm_capture;
	snd_pcm_limit_isa_dma_size(gus->gf1.dma2, &runtime->hw.buffer_bytes_max);
	snd_pcm_limit_isa_dma_size(gus->gf1.dma2, &runtime->hw.period_bytes_max);
	snd_pcm_hw_constraint_ratnums(runtime, 0, SNDRV_PCM_HW_PARAM_RATE,
				      &hw_constraints_clocks);
	return 0;
}

static int snd_gf1_pcm_capture_close(struct snd_pcm_substream *substream)
{
	struct snd_gus_card *gus = snd_pcm_substream_chip(substream);

	gus->pcm_cap_substream = NULL;
	snd_gf1_set_default_handlers(gus, SNDRV_GF1_HANDLER_DMA_READ);
	return 0;
}

static int snd_gf1_pcm_volume_info(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_info *uinfo)
{
	uinfo->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	uinfo->count = 2;
	uinfo->value.integer.min = 0;
	uinfo->value.integer.max = 127;
	return 0;
}

static int snd_gf1_pcm_volume_get(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_gus_card *gus = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	
	spin_lock_irqsave(&gus->pcm_volume_level_lock, flags);
	ucontrol->value.integer.value[0] = gus->gf1.pcm_volume_level_left1;
	ucontrol->value.integer.value[1] = gus->gf1.pcm_volume_level_right1;
	spin_unlock_irqrestore(&gus->pcm_volume_level_lock, flags);
	return 0;
}

static int snd_gf1_pcm_volume_put(struct snd_kcontrol *kcontrol, struct snd_ctl_elem_value *ucontrol)
{
	struct snd_gus_card *gus = snd_kcontrol_chip(kcontrol);
	unsigned long flags;
	int change;
	unsigned int idx;
	unsigned short val1, val2, vol;
	struct gus_pcm_private *pcmp;
	struct snd_gus_voice *pvoice;
	
	val1 = ucontrol->value.integer.value[0] & 127;
	val2 = ucontrol->value.integer.value[1] & 127;
	spin_lock_irqsave(&gus->pcm_volume_level_lock, flags);
	change = val1 != gus->gf1.pcm_volume_level_left1 ||
	         val2 != gus->gf1.pcm_volume_level_right1;
	gus->gf1.pcm_volume_level_left1 = val1;
	gus->gf1.pcm_volume_level_right1 = val2;
	gus->gf1.pcm_volume_level_left = snd_gf1_lvol_to_gvol_raw(val1 << 9) << 4;
	gus->gf1.pcm_volume_level_right = snd_gf1_lvol_to_gvol_raw(val2 << 9) << 4;
	spin_unlock_irqrestore(&gus->pcm_volume_level_lock, flags);
	/* are we active? */
	spin_lock_irqsave(&gus->voice_alloc, flags);
	for (idx = 0; idx < 32; idx++) {
		pvoice = &gus->gf1.voices[idx];
		if (!pvoice->pcm)
			continue;
		pcmp = pvoice->private_data;
		if (!(pcmp->flags & SNDRV_GF1_PCM_PFLG_ACTIVE))
			continue;
		/* load real volume - better precision */
		spin_lock_irqsave(&gus->reg_lock, flags);
		snd_gf1_select_voice(gus, pvoice->number);
		snd_gf1_ctrl_stop(gus, SNDRV_GF1_VB_VOLUME_CONTROL);
		vol = pvoice == pcmp->pvoices[0] ? gus->gf1.pcm_volume_level_left : gus->gf1.pcm_volume_level_right;
		snd_gf1_write16(gus, SNDRV_GF1_VW_VOLUME, vol);
		pcmp->final_volume = 1;
		spin_unlock_irqrestore(&gus->reg_lock, flags);
	}
	spin_unlock_irqrestore(&gus->voice_alloc, flags);
	return change;
}

static struct snd_kcontrol_new snd_gf1_pcm_volume_control =
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "PCM Playback Volume",
	.info = snd_gf1_pcm_volume_info,
	.get = snd_gf1_pcm_volume_get,
	.put = snd_gf1_pcm_volume_put
};

static struct snd_kcontrol_new snd_gf1_pcm_volume_control1 =
{
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "GPCM Playback Volume",
	.info = snd_gf1_pcm_volume_info,
	.get = snd_gf1_pcm_volume_get,
	.put = snd_gf1_pcm_volume_put
};

static struct snd_pcm_ops snd_gf1_pcm_playback_ops = {
	.open =		snd_gf1_pcm_playback_open,
	.close =	snd_gf1_pcm_playback_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_gf1_pcm_playback_hw_params,
	.hw_free =	snd_gf1_pcm_playback_hw_free,
	.prepare =	snd_gf1_pcm_playback_prepare,
	.trigger =	snd_gf1_pcm_playback_trigger,
	.pointer =	snd_gf1_pcm_playback_pointer,
	.copy =		snd_gf1_pcm_playback_copy,
	.silence =	snd_gf1_pcm_playback_silence,
};

static struct snd_pcm_ops snd_gf1_pcm_capture_ops = {
	.open =		snd_gf1_pcm_capture_open,
	.close =	snd_gf1_pcm_capture_close,
	.ioctl =	snd_pcm_lib_ioctl,
	.hw_params =	snd_gf1_pcm_capture_hw_params,
	.hw_free =	snd_gf1_pcm_capture_hw_free,
	.prepare =	snd_gf1_pcm_capture_prepare,
	.trigger =	snd_gf1_pcm_capture_trigger,
	.pointer =	snd_gf1_pcm_capture_pointer,
};

int snd_gf1_pcm_new(struct snd_gus_card * gus, int pcm_dev, int control_index, struct snd_pcm ** rpcm)
{
	struct snd_card *card;
	struct snd_kcontrol *kctl;
	struct snd_pcm *pcm;
	struct snd_pcm_substream *substream;
	int capture, err;

	if (rpcm)
		*rpcm = NULL;
	card = gus->card;
	capture = !gus->interwave && !gus->ess_flag && !gus->ace_flag ? 1 : 0;
	err = snd_pcm_new(card,
			  gus->interwave ? "AMD InterWave" : "GF1",
			  pcm_dev,
			  gus->gf1.pcm_channels / 2,
			  capture,
			  &pcm);
	if (err < 0)
		return err;
	pcm->private_data = gus;
	/* playback setup */
	snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK, &snd_gf1_pcm_playback_ops);

	for (substream = pcm->streams[SNDRV_PCM_STREAM_PLAYBACK].substream; substream; substream = substream->next)
		snd_pcm_lib_preallocate_pages(substream, SNDRV_DMA_TYPE_DEV,
					      snd_dma_isa_data(),
					      64*1024, gus->gf1.dma1 > 3 ? 128*1024 : 64*1024);
	
	pcm->info_flags = 0;
	pcm->dev_subclass = SNDRV_PCM_SUBCLASS_GENERIC_MIX;
	if (capture) {
		snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_CAPTURE, &snd_gf1_pcm_capture_ops);
		if (gus->gf1.dma2 == gus->gf1.dma1)
			pcm->info_flags |= SNDRV_PCM_INFO_HALF_DUPLEX;
		snd_pcm_lib_preallocate_pages(pcm->streams[SNDRV_PCM_STREAM_CAPTURE].substream,
					      SNDRV_DMA_TYPE_DEV, snd_dma_isa_data(),
					      64*1024, gus->gf1.dma2 > 3 ? 128*1024 : 64*1024);
	}
	strcpy(pcm->name, pcm->id);
	if (gus->interwave) {
		sprintf(pcm->name + strlen(pcm->name), " rev %c", gus->revision + 'A');
	}
	strcat(pcm->name, " (synth)");
	gus->pcm = pcm;

	if (gus->codec_flag)
		kctl = snd_ctl_new1(&snd_gf1_pcm_volume_control1, gus);
	else
		kctl = snd_ctl_new1(&snd_gf1_pcm_volume_control, gus);
	if ((err = snd_ctl_add(card, kctl)) < 0)
		return err;
	kctl->id.index = control_index;

	if (rpcm)
		*rpcm = pcm;
	return 0;
}

