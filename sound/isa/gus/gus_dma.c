/*
 *  Routines for GF1 DMA control
 *  Copyright (c) by Jaroslav Kysela <perex@suse.cz>
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

#include <sound/driver.h>
#include <asm/dma.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/gus.h>

static void snd_gf1_dma_ack(snd_gus_card_t * gus)
{
	unsigned long flags;

	spin_lock_irqsave(&gus->reg_lock, flags);
	snd_gf1_write8(gus, SNDRV_GF1_GB_DRAM_DMA_CONTROL, 0x00);
	snd_gf1_look8(gus, SNDRV_GF1_GB_DRAM_DMA_CONTROL);
	spin_unlock_irqrestore(&gus->reg_lock, flags);
}

static void snd_gf1_dma_program(snd_gus_card_t * gus,
				unsigned int addr,
				unsigned long buf_addr,
				unsigned int count,
				unsigned int cmd)
{
	unsigned long flags;
	unsigned int address;
	unsigned char dma_cmd;
	unsigned int address_high;

	// snd_printk("dma_transfer: addr=0x%x, buf=0x%lx, count=0x%x\n", addr, (long) buf, count);

	if (gus->gf1.dma1 > 3) {
		if (gus->gf1.enh_mode) {
			address = addr >> 1;
		} else {
			if (addr & 0x1f) {
				snd_printd("snd_gf1_dma_transfer: unaligned address (0x%x)?\n", addr);
				return;
			}
			address = (addr & 0x000c0000) | ((addr & 0x0003ffff) >> 1);
		}
	} else {
		address = addr;
	}

	dma_cmd = SNDRV_GF1_DMA_ENABLE | (unsigned short) cmd;
#if 0
	dma_cmd |= 0x08;
#endif
	if (dma_cmd & SNDRV_GF1_DMA_16BIT) {
		count++;
		count &= ~1;	/* align */
	}
	if (gus->gf1.dma1 > 3) {
		dma_cmd |= SNDRV_GF1_DMA_WIDTH16;
		count++;
		count &= ~1;	/* align */
	}
	snd_gf1_dma_ack(gus);
	snd_dma_program(gus->gf1.dma1, buf_addr, count, dma_cmd & SNDRV_GF1_DMA_READ ? DMA_MODE_READ : DMA_MODE_WRITE);
#if 0
	snd_printk("address = 0x%x, count = 0x%x, dma_cmd = 0x%x\n", address << 1, count, dma_cmd);
#endif
	spin_lock_irqsave(&gus->reg_lock, flags);
	if (gus->gf1.enh_mode) {
		address_high = ((address >> 16) & 0x000000f0) | (address & 0x0000000f);
		snd_gf1_write16(gus, SNDRV_GF1_GW_DRAM_DMA_LOW, (unsigned short) (address >> 4));
		snd_gf1_write8(gus, SNDRV_GF1_GB_DRAM_DMA_HIGH, (unsigned char) address_high);
	} else
		snd_gf1_write16(gus, SNDRV_GF1_GW_DRAM_DMA_LOW, (unsigned short) (address >> 4));
	snd_gf1_write8(gus, SNDRV_GF1_GB_DRAM_DMA_CONTROL, dma_cmd);
	spin_unlock_irqrestore(&gus->reg_lock, flags);
}

static snd_gf1_dma_block_t *snd_gf1_dma_next_block(snd_gus_card_t * gus)
{
	snd_gf1_dma_block_t *block;

	/* PCM block have bigger priority than synthesizer one */
	if (gus->gf1.dma_data_pcm) {
		block = gus->gf1.dma_data_pcm;
		if (gus->gf1.dma_data_pcm_last == block) {
			gus->gf1.dma_data_pcm =
			gus->gf1.dma_data_pcm_last = NULL;
		} else {
			gus->gf1.dma_data_pcm = block->next;
		}
	} else if (gus->gf1.dma_data_synth) {
		block = gus->gf1.dma_data_synth;
		if (gus->gf1.dma_data_synth_last == block) {
			gus->gf1.dma_data_synth =
			gus->gf1.dma_data_synth_last = NULL;
		} else {
			gus->gf1.dma_data_synth = block->next;
		}
	} else {
		block = NULL;
	}
	if (block) {
		gus->gf1.dma_ack = block->ack;
		gus->gf1.dma_private_data = block->private_data;
	}
	return block;
}


static void snd_gf1_dma_interrupt(snd_gus_card_t * gus)
{
	snd_gf1_dma_block_t *block;

	snd_gf1_dma_ack(gus);
	if (gus->gf1.dma_ack)
		gus->gf1.dma_ack(gus, gus->gf1.dma_private_data);
	spin_lock(&gus->dma_lock);
	if (gus->gf1.dma_data_pcm == NULL &&
	    gus->gf1.dma_data_synth == NULL) {
	    	gus->gf1.dma_ack = NULL;
		gus->gf1.dma_flags &= ~SNDRV_GF1_DMA_TRIGGER;
		spin_unlock(&gus->dma_lock);
		return;
	}
	block = snd_gf1_dma_next_block(gus);
	spin_unlock(&gus->dma_lock);
	snd_gf1_dma_program(gus, block->addr, block->buf_addr, block->count, (unsigned short) block->cmd);
	kfree(block);
#if 0
	printk("program dma (IRQ) - addr = 0x%x, buffer = 0x%lx, count = 0x%x, cmd = 0x%x\n", addr, (long) buffer, count, cmd);
#endif
}

int snd_gf1_dma_init(snd_gus_card_t * gus)
{
	down(&gus->dma_mutex);
	gus->gf1.dma_shared++;
	if (gus->gf1.dma_shared > 1) {
		up(&gus->dma_mutex);
		return 0;
	}
	gus->gf1.interrupt_handler_dma_write = snd_gf1_dma_interrupt;
	gus->gf1.dma_data_pcm = 
	gus->gf1.dma_data_pcm_last =
	gus->gf1.dma_data_synth = 
	gus->gf1.dma_data_synth_last = NULL;
	up(&gus->dma_mutex);
	return 0;
}

int snd_gf1_dma_done(snd_gus_card_t * gus)
{
	snd_gf1_dma_block_t *block;

	down(&gus->dma_mutex);
	gus->gf1.dma_shared--;
	if (!gus->gf1.dma_shared) {
		snd_dma_disable(gus->gf1.dma1);
		snd_gf1_set_default_handlers(gus, SNDRV_GF1_HANDLER_DMA_WRITE);
		snd_gf1_dma_ack(gus);
		while ((block = gus->gf1.dma_data_pcm)) {
			gus->gf1.dma_data_pcm = block->next;
			kfree(block);
		}
		while ((block = gus->gf1.dma_data_synth)) {
			gus->gf1.dma_data_synth = block->next;
			kfree(block);
		}
		gus->gf1.dma_data_pcm_last =
		gus->gf1.dma_data_synth_last = NULL;
	}
	up(&gus->dma_mutex);
	return 0;
}

int snd_gf1_dma_transfer_block(snd_gus_card_t * gus,
			       snd_gf1_dma_block_t * __block,
			       int atomic,
			       int synth)
{
	unsigned long flags;
	snd_gf1_dma_block_t *block;

	block = kmalloc(sizeof(*block), atomic ? GFP_ATOMIC : GFP_KERNEL);
	if (block == NULL) {
		snd_printk("gf1: DMA transfer failure; not enough memory\n");
		return -ENOMEM;
	}
	*block = *__block;
	block->next = NULL;
#if 0
	printk("addr = 0x%x, buffer = 0x%lx, count = 0x%x, cmd = 0x%x\n", block->addr, (long) block->buffer, block->count, block->cmd);
#endif
#if 0
	printk("gus->gf1.dma_data_pcm_last = 0x%lx\n", (long)gus->gf1.dma_data_pcm_last);
	printk("gus->gf1.dma_data_pcm = 0x%lx\n", (long)gus->gf1.dma_data_pcm);
#endif
	spin_lock_irqsave(&gus->dma_lock, flags);
	if (synth) {
		if (gus->gf1.dma_data_synth_last) {
			gus->gf1.dma_data_synth_last->next = block;
			gus->gf1.dma_data_synth_last = block;
		} else {
			gus->gf1.dma_data_synth = 
			gus->gf1.dma_data_synth_last = block;
		}
	} else {
		if (gus->gf1.dma_data_pcm_last) {
			gus->gf1.dma_data_pcm_last->next = block;
			gus->gf1.dma_data_pcm_last = block;
		} else {
			gus->gf1.dma_data_pcm = 
			gus->gf1.dma_data_pcm_last = block;
		}
	}
	if (!(gus->gf1.dma_flags & SNDRV_GF1_DMA_TRIGGER)) {
		gus->gf1.dma_flags |= SNDRV_GF1_DMA_TRIGGER;
		block = snd_gf1_dma_next_block(gus);
		spin_unlock_irqrestore(&gus->dma_lock, flags);
		if (block == NULL)
			return 0;
		snd_gf1_dma_program(gus, block->addr, block->buf_addr, block->count, (unsigned short) block->cmd);
		kfree(block);
		return 0;
	}
	spin_unlock_irqrestore(&gus->dma_lock, flags);
	return 0;
}
