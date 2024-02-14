// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Routines for GF1 DMA control
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <asm/dma.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/gus.h>

static void snd_gf1_dma_ack(struct snd_gus_card * gus)
{
	unsigned long flags;

	spin_lock_irqsave(&gus->reg_lock, flags);
	snd_gf1_write8(gus, SNDRV_GF1_GB_DRAM_DMA_CONTROL, 0x00);
	snd_gf1_look8(gus, SNDRV_GF1_GB_DRAM_DMA_CONTROL);
	spin_unlock_irqrestore(&gus->reg_lock, flags);
}

static void snd_gf1_dma_program(struct snd_gus_card * gus,
				unsigned int addr,
				unsigned long buf_addr,
				unsigned int count,
				unsigned int cmd)
{
	unsigned long flags;
	unsigned int address;
	unsigned char dma_cmd;
	unsigned int address_high;

	snd_printdd("dma_transfer: addr=0x%x, buf=0x%lx, count=0x%x\n",
		    addr, buf_addr, count);

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
	snd_printk(KERN_DEBUG "address = 0x%x, count = 0x%x, dma_cmd = 0x%x\n",
		   address << 1, count, dma_cmd);
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

static struct snd_gf1_dma_block *snd_gf1_dma_next_block(struct snd_gus_card * gus)
{
	struct snd_gf1_dma_block *block;

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


static void snd_gf1_dma_interrupt(struct snd_gus_card * gus)
{
	struct snd_gf1_dma_block *block;

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
	if (!block)
		return;
	snd_gf1_dma_program(gus, block->addr, block->buf_addr, block->count, (unsigned short) block->cmd);
	kfree(block);
#if 0
	snd_printd(KERN_DEBUG "program dma (IRQ) - "
		   "addr = 0x%x, buffer = 0x%lx, count = 0x%x, cmd = 0x%x\n",
		   block->addr, block->buf_addr, block->count, block->cmd);
#endif
}

int snd_gf1_dma_init(struct snd_gus_card * gus)
{
	mutex_lock(&gus->dma_mutex);
	gus->gf1.dma_shared++;
	if (gus->gf1.dma_shared > 1) {
		mutex_unlock(&gus->dma_mutex);
		return 0;
	}
	gus->gf1.interrupt_handler_dma_write = snd_gf1_dma_interrupt;
	gus->gf1.dma_data_pcm = 
	gus->gf1.dma_data_pcm_last =
	gus->gf1.dma_data_synth = 
	gus->gf1.dma_data_synth_last = NULL;
	mutex_unlock(&gus->dma_mutex);
	return 0;
}

int snd_gf1_dma_done(struct snd_gus_card * gus)
{
	struct snd_gf1_dma_block *block;

	mutex_lock(&gus->dma_mutex);
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
	mutex_unlock(&gus->dma_mutex);
	return 0;
}

int snd_gf1_dma_transfer_block(struct snd_gus_card * gus,
			       struct snd_gf1_dma_block * __block,
			       int atomic,
			       int synth)
{
	unsigned long flags;
	struct snd_gf1_dma_block *block;

	block = kmalloc(sizeof(*block), atomic ? GFP_ATOMIC : GFP_KERNEL);
	if (!block)
		return -ENOMEM;

	*block = *__block;
	block->next = NULL;

	snd_printdd("addr = 0x%x, buffer = 0x%lx, count = 0x%x, cmd = 0x%x\n",
		    block->addr, (long) block->buffer, block->count,
		    block->cmd);

	snd_printdd("gus->gf1.dma_data_pcm_last = 0x%lx\n",
		    (long)gus->gf1.dma_data_pcm_last);
	snd_printdd("gus->gf1.dma_data_pcm = 0x%lx\n",
		    (long)gus->gf1.dma_data_pcm);

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
