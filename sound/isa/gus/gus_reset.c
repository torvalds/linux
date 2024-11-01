// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <sound/core.h>
#include <sound/gus.h>

extern int snd_gf1_synth_init(struct snd_gus_card * gus);
extern void snd_gf1_synth_done(struct snd_gus_card * gus);

/*
 *  ok.. default interrupt handlers...
 */

static void snd_gf1_default_interrupt_handler_midi_out(struct snd_gus_card * gus)
{
	snd_gf1_uart_cmd(gus, gus->gf1.uart_cmd &= ~0x20);
}

static void snd_gf1_default_interrupt_handler_midi_in(struct snd_gus_card * gus)
{
	snd_gf1_uart_cmd(gus, gus->gf1.uart_cmd &= ~0x80);
}

static void snd_gf1_default_interrupt_handler_timer1(struct snd_gus_card * gus)
{
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_SOUND_BLASTER_CONTROL, gus->gf1.timer_enabled &= ~4);
}

static void snd_gf1_default_interrupt_handler_timer2(struct snd_gus_card * gus)
{
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_SOUND_BLASTER_CONTROL, gus->gf1.timer_enabled &= ~8);
}

static void snd_gf1_default_interrupt_handler_wave_and_volume(struct snd_gus_card * gus, struct snd_gus_voice * voice)
{
	snd_gf1_i_ctrl_stop(gus, 0x00);
	snd_gf1_i_ctrl_stop(gus, 0x0d);
}

static void snd_gf1_default_interrupt_handler_dma_write(struct snd_gus_card * gus)
{
	snd_gf1_i_write8(gus, 0x41, 0x00);
}

static void snd_gf1_default_interrupt_handler_dma_read(struct snd_gus_card * gus)
{
	snd_gf1_i_write8(gus, 0x49, 0x00);
}

void snd_gf1_set_default_handlers(struct snd_gus_card * gus, unsigned int what)
{
	if (what & SNDRV_GF1_HANDLER_MIDI_OUT)
		gus->gf1.interrupt_handler_midi_out = snd_gf1_default_interrupt_handler_midi_out;
	if (what & SNDRV_GF1_HANDLER_MIDI_IN)
		gus->gf1.interrupt_handler_midi_in = snd_gf1_default_interrupt_handler_midi_in;
	if (what & SNDRV_GF1_HANDLER_TIMER1)
		gus->gf1.interrupt_handler_timer1 = snd_gf1_default_interrupt_handler_timer1;
	if (what & SNDRV_GF1_HANDLER_TIMER2)
		gus->gf1.interrupt_handler_timer2 = snd_gf1_default_interrupt_handler_timer2;
	if (what & SNDRV_GF1_HANDLER_VOICE) {
		struct snd_gus_voice *voice;
		
		voice = &gus->gf1.voices[what & 0xffff];
		voice->handler_wave =
		voice->handler_volume = snd_gf1_default_interrupt_handler_wave_and_volume;
		voice->handler_effect = NULL;
		voice->volume_change = NULL;
	}
	if (what & SNDRV_GF1_HANDLER_DMA_WRITE)
		gus->gf1.interrupt_handler_dma_write = snd_gf1_default_interrupt_handler_dma_write;
	if (what & SNDRV_GF1_HANDLER_DMA_READ)
		gus->gf1.interrupt_handler_dma_read = snd_gf1_default_interrupt_handler_dma_read;
}

/*

 */

static void snd_gf1_clear_regs(struct snd_gus_card * gus)
{
	unsigned long flags;

	spin_lock_irqsave(&gus->reg_lock, flags);
	inb(GUSP(gus, IRQSTAT));
	snd_gf1_write8(gus, 0x41, 0);	/* DRAM DMA Control Register */
	snd_gf1_write8(gus, 0x45, 0);	/* Timer Control */
	snd_gf1_write8(gus, 0x49, 0);	/* Sampling Control Register */
	spin_unlock_irqrestore(&gus->reg_lock, flags);
}

static void snd_gf1_look_regs(struct snd_gus_card * gus)
{
	unsigned long flags;

	spin_lock_irqsave(&gus->reg_lock, flags);
	snd_gf1_look8(gus, 0x41);	/* DRAM DMA Control Register */
	snd_gf1_look8(gus, 0x49);	/* Sampling Control Register */
	inb(GUSP(gus, IRQSTAT));
	snd_gf1_read8(gus, 0x0f);	/* IRQ Source Register */
	spin_unlock_irqrestore(&gus->reg_lock, flags);
}

/*
 *  put selected GF1 voices to initial stage...
 */

void snd_gf1_smart_stop_voice(struct snd_gus_card * gus, unsigned short voice)
{
	unsigned long flags;

	spin_lock_irqsave(&gus->reg_lock, flags);
	snd_gf1_select_voice(gus, voice);
#if 0
	printk(KERN_DEBUG " -%i- smart stop voice - volume = 0x%x\n", voice, snd_gf1_i_read16(gus, SNDRV_GF1_VW_VOLUME));
#endif
	snd_gf1_ctrl_stop(gus, SNDRV_GF1_VB_ADDRESS_CONTROL);
	snd_gf1_ctrl_stop(gus, SNDRV_GF1_VB_VOLUME_CONTROL);
	spin_unlock_irqrestore(&gus->reg_lock, flags);
}

void snd_gf1_stop_voice(struct snd_gus_card * gus, unsigned short voice)
{
	unsigned long flags;

	spin_lock_irqsave(&gus->reg_lock, flags);
	snd_gf1_select_voice(gus, voice);
#if 0
	printk(KERN_DEBUG " -%i- stop voice - volume = 0x%x\n", voice, snd_gf1_i_read16(gus, SNDRV_GF1_VW_VOLUME));
#endif
	snd_gf1_ctrl_stop(gus, SNDRV_GF1_VB_ADDRESS_CONTROL);
	snd_gf1_ctrl_stop(gus, SNDRV_GF1_VB_VOLUME_CONTROL);
	if (gus->gf1.enh_mode)
		snd_gf1_write8(gus, SNDRV_GF1_VB_ACCUMULATOR, 0);
	spin_unlock_irqrestore(&gus->reg_lock, flags);
#if 0
	snd_gf1_lfo_shutdown(gus, voice, ULTRA_LFO_VIBRATO);
	snd_gf1_lfo_shutdown(gus, voice, ULTRA_LFO_TREMOLO);
#endif
}

static void snd_gf1_clear_voices(struct snd_gus_card * gus, unsigned short v_min,
				 unsigned short v_max)
{
	unsigned long flags;
	unsigned int daddr;
	unsigned short i, w_16;

	daddr = gus->gf1.default_voice_address << 4;
	for (i = v_min; i <= v_max; i++) {
#if 0
		if (gus->gf1.syn_voices)
			gus->gf1.syn_voices[i].flags = ~VFLG_DYNAMIC;
#endif
		spin_lock_irqsave(&gus->reg_lock, flags);
		snd_gf1_select_voice(gus, i);
		snd_gf1_ctrl_stop(gus, SNDRV_GF1_VB_ADDRESS_CONTROL);	/* Voice Control Register = voice stop */
		snd_gf1_ctrl_stop(gus, SNDRV_GF1_VB_VOLUME_CONTROL);	/* Volume Ramp Control Register = ramp off */
		if (gus->gf1.enh_mode)
			snd_gf1_write8(gus, SNDRV_GF1_VB_MODE, gus->gf1.memory ? 0x02 : 0x82);	/* Deactivate voice */
		w_16 = snd_gf1_read8(gus, SNDRV_GF1_VB_ADDRESS_CONTROL) & 0x04;
		snd_gf1_write16(gus, SNDRV_GF1_VW_FREQUENCY, 0x400);
		snd_gf1_write_addr(gus, SNDRV_GF1_VA_START, daddr, w_16);
		snd_gf1_write_addr(gus, SNDRV_GF1_VA_END, daddr, w_16);
		snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_START, 0);
		snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_END, 0);
		snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_RATE, 0);
		snd_gf1_write16(gus, SNDRV_GF1_VW_VOLUME, 0);
		snd_gf1_write_addr(gus, SNDRV_GF1_VA_CURRENT, daddr, w_16);
		snd_gf1_write8(gus, SNDRV_GF1_VB_PAN, 7);
		if (gus->gf1.enh_mode) {
			snd_gf1_write8(gus, SNDRV_GF1_VB_ACCUMULATOR, 0);
			snd_gf1_write16(gus, SNDRV_GF1_VW_EFFECT_VOLUME, 0);
			snd_gf1_write16(gus, SNDRV_GF1_VW_EFFECT_VOLUME_FINAL, 0);
		}
		spin_unlock_irqrestore(&gus->reg_lock, flags);
#if 0
		snd_gf1_lfo_shutdown(gus, i, ULTRA_LFO_VIBRATO);
		snd_gf1_lfo_shutdown(gus, i, ULTRA_LFO_TREMOLO);
#endif
	}
}

void snd_gf1_stop_voices(struct snd_gus_card * gus, unsigned short v_min, unsigned short v_max)
{
	unsigned long flags;
	short i, ramp_ok;
	unsigned short ramp_end;

	if (!in_interrupt()) {	/* this can't be done in interrupt */
		for (i = v_min, ramp_ok = 0; i <= v_max; i++) {
			spin_lock_irqsave(&gus->reg_lock, flags);
			snd_gf1_select_voice(gus, i);
			ramp_end = snd_gf1_read16(gus, 9) >> 8;
			if (ramp_end > SNDRV_GF1_MIN_OFFSET) {
				ramp_ok++;
				snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_RATE, 20);	/* ramp rate */
				snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_START, SNDRV_GF1_MIN_OFFSET);	/* ramp start */
				snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_END, ramp_end);	/* ramp end */
				snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_CONTROL, 0x40);	/* ramp down */
				if (gus->gf1.enh_mode) {
					snd_gf1_delay(gus);
					snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_CONTROL, 0x40);
				}
			}
			spin_unlock_irqrestore(&gus->reg_lock, flags);
		}
		msleep_interruptible(50);
	}
	snd_gf1_clear_voices(gus, v_min, v_max);
}

static void snd_gf1_alloc_voice_use(struct snd_gus_card * gus, 
				    struct snd_gus_voice * pvoice,
				    int type, int client, int port)
{
	pvoice->use = 1;
	switch (type) {
	case SNDRV_GF1_VOICE_TYPE_PCM:
		gus->gf1.pcm_alloc_voices++;
		pvoice->pcm = 1;
		break;
	case SNDRV_GF1_VOICE_TYPE_SYNTH:
		pvoice->synth = 1;
		pvoice->client = client;
		pvoice->port = port;
		break;
	case SNDRV_GF1_VOICE_TYPE_MIDI:
		pvoice->midi = 1;
		pvoice->client = client;
		pvoice->port = port;
		break;
	}
}

struct snd_gus_voice *snd_gf1_alloc_voice(struct snd_gus_card * gus, int type, int client, int port)
{
	struct snd_gus_voice *pvoice;
	unsigned long flags;
	int idx;

	spin_lock_irqsave(&gus->voice_alloc, flags);
	if (type == SNDRV_GF1_VOICE_TYPE_PCM) {
		if (gus->gf1.pcm_alloc_voices >= gus->gf1.pcm_channels) {
			spin_unlock_irqrestore(&gus->voice_alloc, flags);
			return NULL;
		}
	}
	for (idx = 0; idx < 32; idx++) {
		pvoice = &gus->gf1.voices[idx];
		if (!pvoice->use) {
			snd_gf1_alloc_voice_use(gus, pvoice, type, client, port);
			spin_unlock_irqrestore(&gus->voice_alloc, flags);
			return pvoice;
		}
	} 
	for (idx = 0; idx < 32; idx++) {
		pvoice = &gus->gf1.voices[idx];
		if (pvoice->midi && !pvoice->client) {
			snd_gf1_clear_voices(gus, pvoice->number, pvoice->number);
			snd_gf1_alloc_voice_use(gus, pvoice, type, client, port);
			spin_unlock_irqrestore(&gus->voice_alloc, flags);
			return pvoice;
		}
	} 
	spin_unlock_irqrestore(&gus->voice_alloc, flags);
	return NULL;
}

void snd_gf1_free_voice(struct snd_gus_card * gus, struct snd_gus_voice *voice)
{
	unsigned long flags;
	void (*private_free)(struct snd_gus_voice *voice);

	if (voice == NULL || !voice->use)
		return;
	snd_gf1_set_default_handlers(gus, SNDRV_GF1_HANDLER_VOICE | voice->number);
	snd_gf1_clear_voices(gus, voice->number, voice->number);
	spin_lock_irqsave(&gus->voice_alloc, flags);
	private_free = voice->private_free;
	voice->private_free = NULL;
	voice->private_data = NULL;
	if (voice->pcm)
		gus->gf1.pcm_alloc_voices--;
	voice->use = voice->pcm = 0;
	voice->sample_ops = NULL;
	spin_unlock_irqrestore(&gus->voice_alloc, flags);
	if (private_free)
		private_free(voice);
}

/*
 *  call this function only by start of driver
 */

int snd_gf1_start(struct snd_gus_card * gus)
{
	unsigned long flags;
	unsigned int i;

	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 0);	/* reset GF1 */
	udelay(160);
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 1);	/* disable IRQ & DAC */
	udelay(160);
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_JOYSTICK_DAC_LEVEL, gus->joystick_dac);

	snd_gf1_set_default_handlers(gus, SNDRV_GF1_HANDLER_ALL);
	for (i = 0; i < 32; i++) {
		gus->gf1.voices[i].number = i;
		snd_gf1_set_default_handlers(gus, SNDRV_GF1_HANDLER_VOICE | i);
	}

	snd_gf1_uart_cmd(gus, 0x03);	/* huh.. this cleanup took me some time... */

	if (gus->gf1.enh_mode) {	/* enhanced mode !!!! */
		snd_gf1_i_write8(gus, SNDRV_GF1_GB_GLOBAL_MODE, snd_gf1_i_look8(gus, SNDRV_GF1_GB_GLOBAL_MODE) | 0x01);
		snd_gf1_i_write8(gus, SNDRV_GF1_GB_MEMORY_CONTROL, 0x01);
	}
	snd_gf1_clear_regs(gus);
	snd_gf1_select_active_voices(gus);
	snd_gf1_delay(gus);
	gus->gf1.default_voice_address = gus->gf1.memory > 0 ? 0 : 512 - 8;
	/* initialize LFOs & clear LFOs memory */
	if (gus->gf1.enh_mode && gus->gf1.memory) {
		gus->gf1.hw_lfo = 1;
		gus->gf1.default_voice_address += 1024;
	} else {
		gus->gf1.sw_lfo = 1;
	}
#if 0
	snd_gf1_lfo_init(gus);
#endif
	if (gus->gf1.memory > 0)
		for (i = 0; i < 4; i++)
			snd_gf1_poke(gus, gus->gf1.default_voice_address + i, 0);
	snd_gf1_clear_regs(gus);
	snd_gf1_clear_voices(gus, 0, 31);
	snd_gf1_look_regs(gus);
	udelay(160);
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 7);	/* Reset Register = IRQ enable, DAC enable */
	udelay(160);
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 7);	/* Reset Register = IRQ enable, DAC enable */
	if (gus->gf1.enh_mode) {	/* enhanced mode !!!! */
		snd_gf1_i_write8(gus, SNDRV_GF1_GB_GLOBAL_MODE, snd_gf1_i_look8(gus, SNDRV_GF1_GB_GLOBAL_MODE) | 0x01);
		snd_gf1_i_write8(gus, SNDRV_GF1_GB_MEMORY_CONTROL, 0x01);
	}
	while ((snd_gf1_i_read8(gus, SNDRV_GF1_GB_VOICES_IRQ) & 0xc0) != 0xc0);

	spin_lock_irqsave(&gus->reg_lock, flags);
	outb(gus->gf1.active_voice = 0, GUSP(gus, GF1PAGE));
	outb(gus->mix_cntrl_reg, GUSP(gus, MIXCNTRLREG));
	spin_unlock_irqrestore(&gus->reg_lock, flags);

	snd_gf1_timers_init(gus);
	snd_gf1_look_regs(gus);
	snd_gf1_mem_init(gus);
	snd_gf1_mem_proc_init(gus);
#ifdef CONFIG_SND_DEBUG
	snd_gus_irq_profile_init(gus);
#endif

#if 0
	if (gus->pnp_flag) {
		if (gus->chip.playback_fifo_size > 0)
			snd_gf1_i_write16(gus, SNDRV_GF1_GW_FIFO_RECORD_BASE_ADDR, gus->chip.playback_fifo_block->ptr >> 8);
		if (gus->chip.record_fifo_size > 0)
			snd_gf1_i_write16(gus, SNDRV_GF1_GW_FIFO_PLAY_BASE_ADDR, gus->chip.record_fifo_block->ptr >> 8);
		snd_gf1_i_write16(gus, SNDRV_GF1_GW_FIFO_SIZE, gus->chip.interwave_fifo_reg);
	}
#endif

	return 0;
}

/*
 *  call this function only by shutdown of driver
 */

int snd_gf1_stop(struct snd_gus_card * gus)
{
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_SOUND_BLASTER_CONTROL, 0); /* stop all timers */
	snd_gf1_stop_voices(gus, 0, 31);		/* stop all voices */
	snd_gf1_i_write8(gus, SNDRV_GF1_GB_RESET, 1);	/* disable IRQ & DAC */
	snd_gf1_timers_done(gus);
	snd_gf1_mem_done(gus);
#if 0
	snd_gf1_lfo_done(gus);
#endif
	return 0;
}
