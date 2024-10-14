// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Routine for IRQ handling from GF1/InterWave chip
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <sound/core.h>
#include <sound/info.h>
#include <sound/gus.h>

#ifdef CONFIG_SND_DEBUG
#define STAT_ADD(x)	((x)++)
#else
#define STAT_ADD(x)	while (0) { ; }
#endif

irqreturn_t snd_gus_interrupt(int irq, void *dev_id)
{
	struct snd_gus_card * gus = dev_id;
	unsigned char status;
	int loop = 100;
	int handled = 0;

__again:
	status = inb(gus->gf1.reg_irqstat);
	if (status == 0)
		return IRQ_RETVAL(handled);
	handled = 1;
	if (status & 0x02) {
		STAT_ADD(gus->gf1.interrupt_stat_midi_in);
		if (gus->gf1.interrupt_handler_midi_in)
			gus->gf1.interrupt_handler_midi_in(gus);
	}
	if (status & 0x01) {
		STAT_ADD(gus->gf1.interrupt_stat_midi_out);
		if (gus->gf1.interrupt_handler_midi_out)
			gus->gf1.interrupt_handler_midi_out(gus);
	}
	if (status & (0x20 | 0x40)) {
		unsigned int already, _current_;
		unsigned char voice_status, voice;
		struct snd_gus_voice *pvoice;

		already = 0;
		while (((voice_status = snd_gf1_i_read8(gus, SNDRV_GF1_GB_VOICES_IRQ)) & 0xc0) != 0xc0) {
			voice = voice_status & 0x1f;
			_current_ = 1 << voice;
			if (already & _current_)
				continue;	/* multi request */
			already |= _current_;	/* mark request */
#if 0
			dev_dbg(gus->card->dev,
				"voice = %i, voice_status = 0x%x, voice_verify = %i\n",
				voice, voice_status, inb(GUSP(gus, GF1PAGE)));
#endif
			pvoice = &gus->gf1.voices[voice]; 
			if (pvoice->use) {
				if (!(voice_status & 0x80)) {	/* voice position IRQ */
					STAT_ADD(pvoice->interrupt_stat_wave);
					pvoice->handler_wave(gus, pvoice);
				}
				if (!(voice_status & 0x40)) {	/* volume ramp IRQ */
					STAT_ADD(pvoice->interrupt_stat_volume);
					pvoice->handler_volume(gus, pvoice);
				}
			} else {
				STAT_ADD(gus->gf1.interrupt_stat_voice_lost);
				snd_gf1_i_ctrl_stop(gus, SNDRV_GF1_VB_ADDRESS_CONTROL);
				snd_gf1_i_ctrl_stop(gus, SNDRV_GF1_VB_VOLUME_CONTROL);
			}
		}
	}
	if (status & 0x04) {
		STAT_ADD(gus->gf1.interrupt_stat_timer1);
		if (gus->gf1.interrupt_handler_timer1)
			gus->gf1.interrupt_handler_timer1(gus);
	}
	if (status & 0x08) {
		STAT_ADD(gus->gf1.interrupt_stat_timer2);
		if (gus->gf1.interrupt_handler_timer2)
			gus->gf1.interrupt_handler_timer2(gus);
	}
	if (status & 0x80) {
		if (snd_gf1_i_look8(gus, SNDRV_GF1_GB_DRAM_DMA_CONTROL) & 0x40) {
			STAT_ADD(gus->gf1.interrupt_stat_dma_write);
			if (gus->gf1.interrupt_handler_dma_write)
				gus->gf1.interrupt_handler_dma_write(gus);
		}
		if (snd_gf1_i_look8(gus, SNDRV_GF1_GB_REC_DMA_CONTROL) & 0x40) {
			STAT_ADD(gus->gf1.interrupt_stat_dma_read);
			if (gus->gf1.interrupt_handler_dma_read)
				gus->gf1.interrupt_handler_dma_read(gus);
		}
	}
	if (--loop > 0)
		goto __again;
	return IRQ_NONE;
}

#ifdef CONFIG_SND_DEBUG
static void snd_gus_irq_info_read(struct snd_info_entry *entry, 
				  struct snd_info_buffer *buffer)
{
	struct snd_gus_card *gus;
	struct snd_gus_voice *pvoice;
	int idx;

	gus = entry->private_data;
	snd_iprintf(buffer, "midi out = %u\n", gus->gf1.interrupt_stat_midi_out);
	snd_iprintf(buffer, "midi in = %u\n", gus->gf1.interrupt_stat_midi_in);
	snd_iprintf(buffer, "timer1 = %u\n", gus->gf1.interrupt_stat_timer1);
	snd_iprintf(buffer, "timer2 = %u\n", gus->gf1.interrupt_stat_timer2);
	snd_iprintf(buffer, "dma write = %u\n", gus->gf1.interrupt_stat_dma_write);
	snd_iprintf(buffer, "dma read = %u\n", gus->gf1.interrupt_stat_dma_read);
	snd_iprintf(buffer, "voice lost = %u\n", gus->gf1.interrupt_stat_voice_lost);
	for (idx = 0; idx < 32; idx++) {
		pvoice = &gus->gf1.voices[idx];
		snd_iprintf(buffer, "voice %i: wave = %u, volume = %u\n",
					idx,
					pvoice->interrupt_stat_wave,
					pvoice->interrupt_stat_volume);
	}
}

void snd_gus_irq_profile_init(struct snd_gus_card *gus)
{
	snd_card_ro_proc_new(gus->card, "gusirq", gus, snd_gus_irq_info_read);
}

#endif
