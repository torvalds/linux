/*
 *  Routines for Gravis UltraSound soundcards - Simple instrument handlers
 *  Copyright (c) by Jaroslav Kysela <perex@perex.cz>
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
#include <linux/time.h>
#include <sound/core.h>
#include <sound/gus.h>
#include "gus_tables.h"

/*
 *
 */

static void interrupt_wave(struct snd_gus_card *gus, struct snd_gus_voice *voice);
static void interrupt_volume(struct snd_gus_card *gus, struct snd_gus_voice *voice);
static void interrupt_effect(struct snd_gus_card *gus, struct snd_gus_voice *voice);

static void sample_start(struct snd_gus_card *gus, struct snd_gus_voice *voice, snd_seq_position_t position);
static void sample_stop(struct snd_gus_card *gus, struct snd_gus_voice *voice, int mode);
static void sample_freq(struct snd_gus_card *gus, struct snd_gus_voice *voice, snd_seq_frequency_t freq);
static void sample_volume(struct snd_gus_card *card, struct snd_gus_voice *voice, struct snd_seq_ev_volume *volume);
static void sample_loop(struct snd_gus_card *card, struct snd_gus_voice *voice, struct snd_seq_ev_loop *loop);
static void sample_pos(struct snd_gus_card *card, struct snd_gus_voice *voice, snd_seq_position_t position);
static void sample_private1(struct snd_gus_card *card, struct snd_gus_voice *voice, unsigned char *data);

static struct snd_gus_sample_ops sample_ops = {
	sample_start,
	sample_stop,
	sample_freq,
	sample_volume,
	sample_loop,
	sample_pos,
	sample_private1
};

#if 0

static void note_stop(struct snd_gus_card *gus, struct snd_gus_voice *voice, int wait);
static void note_wait(struct snd_gus_card *gus, struct snd_gus_voice *voice);
static void note_off(struct snd_gus_card *gus, struct snd_gus_voice *voice);
static void note_volume(struct snd_gus_card *card, struct snd_gus_voice *voice);
static void note_pitchbend(struct snd_gus_card *card, struct snd_gus_voice *voice);
static void note_vibrato(struct snd_gus_card *card, struct snd_gus_voice *voice);
static void note_tremolo(struct snd_gus_card *card, struct snd_gus_voice *voice);

static struct snd_gus_note_handlers note_commands = {
	note_stop,
	note_wait,
	note_off,
	note_volume,
	note_pitchbend,
	note_vibrato,
	note_tremolo
};

static void chn_trigger_down(struct snd_gus_card *card, ultra_channel_t *channel, ultra_instrument_t *instrument, unsigned char note, unsigned char velocity, unsigned char priority );
static void chn_trigger_up( ultra_card_t *card, ultra_note_t *note );
static void chn_control( ultra_card_t *card, ultra_channel_t *channel, unsigned short p1, unsigned short p2 );

static struct ULTRA_STRU_INSTRUMENT_CHANNEL_COMMANDS channel_commands = {
  chn_trigger_down,
  chn_trigger_up,
  chn_control
};

#endif

static void do_volume_envelope(struct snd_gus_card *card, struct snd_gus_voice *voice);
static void do_pan_envelope(struct snd_gus_card *card, struct snd_gus_voice *voice);

/*
 *
 */

static void interrupt_wave(struct snd_gus_card *gus, struct snd_gus_voice *voice)
{
	spin_lock(&gus->event_lock);
	snd_gf1_stop_voice(gus, voice->number);
	spin_lock(&gus->reg_lock);
	snd_gf1_select_voice(gus, voice->number);
	snd_gf1_write16(gus, SNDRV_GF1_VW_VOLUME, 0);
	spin_unlock(&gus->reg_lock);
	voice->flags &= ~SNDRV_GF1_VFLG_RUNNING;
	spin_unlock(&gus->event_lock);
}

static void interrupt_volume(struct snd_gus_card *gus, struct snd_gus_voice *voice)
{
	spin_lock(&gus->event_lock);
	if (voice->flags & SNDRV_GF1_VFLG_RUNNING)
		do_volume_envelope(gus, voice);
	else
		snd_gf1_stop_voice(gus, voice->number);
	spin_unlock(&gus->event_lock);
}

static void interrupt_effect(struct snd_gus_card *gus, struct snd_gus_voice *voice)
{
	spin_lock(&gus->event_lock);
	if ((voice->flags & (SNDRV_GF1_VFLG_RUNNING|SNDRV_GF1_VFLG_EFFECT_TIMER1)) ==
	                    (SNDRV_GF1_VFLG_RUNNING|SNDRV_GF1_VFLG_EFFECT_TIMER1))
		do_pan_envelope(gus, voice);
	spin_unlock(&gus->event_lock);
}

/*
 *
 */

static void do_volume_envelope(struct snd_gus_card *gus, struct snd_gus_voice *voice)
{
	unsigned short next, rate, old_volume;
	int program_next_ramp;
	unsigned long flags;
  
	if (!gus->gf1.volume_ramp) {
		spin_lock_irqsave(&gus->reg_lock, flags);
		snd_gf1_select_voice(gus, voice->number);
		snd_gf1_ctrl_stop(gus, SNDRV_GF1_VB_VOLUME_CONTROL);
		snd_gf1_write16(gus, SNDRV_GF1_VW_VOLUME, voice->gf1_volume);
		/* printk("gf1_volume = 0x%x\n", voice->gf1_volume); */
		spin_unlock_irqrestore(&gus->reg_lock, flags);
		return;
	}
	program_next_ramp = 0;
	rate = next = 0;
	while (1) {
		program_next_ramp = 0;
		rate = next = 0;
		switch (voice->venv_state) {
		case VENV_BEFORE:
			voice->venv_state = VENV_ATTACK;
			voice->venv_value_next = 0;
			spin_lock_irqsave(&gus->reg_lock, flags);
			snd_gf1_select_voice(gus, voice->number);
			snd_gf1_ctrl_stop(gus, SNDRV_GF1_VB_VOLUME_CONTROL);
			snd_gf1_write16(gus, SNDRV_GF1_VW_VOLUME, SNDRV_GF1_MIN_VOLUME);
			spin_unlock_irqrestore(&gus->reg_lock, flags);
			break;
		case VENV_ATTACK:
			voice->venv_state = VENV_SUSTAIN;
			program_next_ramp++;
			next = 255;
			rate = gus->gf1.volume_ramp;
			break;
		case VENV_SUSTAIN:
			voice->venv_state = VENV_RELEASE;
			spin_lock_irqsave(&gus->reg_lock, flags);
			snd_gf1_select_voice(gus, voice->number);
			snd_gf1_ctrl_stop(gus, SNDRV_GF1_VB_VOLUME_CONTROL);
 			snd_gf1_write16(gus, SNDRV_GF1_VW_VOLUME, ((int)voice->gf1_volume * (int)voice->venv_value_next) / 255);
			spin_unlock_irqrestore(&gus->reg_lock, flags);
			return;
		case VENV_RELEASE:
			voice->venv_state = VENV_DONE;
			program_next_ramp++;
			next = 0;
			rate = gus->gf1.volume_ramp;
			break;
		case VENV_DONE:
			snd_gf1_stop_voice(gus, voice->number);
			voice->flags &= ~SNDRV_GF1_VFLG_RUNNING;
			return;
		case VENV_VOLUME:
			program_next_ramp++;
			next = voice->venv_value_next;
			rate = gus->gf1.volume_ramp;
			voice->venv_state = voice->venv_state_prev;
			break;
		}
		voice->venv_value_next = next;
		if (!program_next_ramp)
			continue;
		spin_lock_irqsave(&gus->reg_lock, flags);
		snd_gf1_select_voice(gus, voice->number);
		snd_gf1_ctrl_stop(gus, SNDRV_GF1_VB_VOLUME_CONTROL);
		old_volume = snd_gf1_read16(gus, SNDRV_GF1_VW_VOLUME) >> 8;
		if (!rate) {
			spin_unlock_irqrestore(&gus->reg_lock, flags);			
			continue;
		}
		next = (((int)voice->gf1_volume * (int)next) / 255) >> 8;
		if (old_volume < SNDRV_GF1_MIN_OFFSET)
			old_volume = SNDRV_GF1_MIN_OFFSET;
		if (next < SNDRV_GF1_MIN_OFFSET)
			next = SNDRV_GF1_MIN_OFFSET;
		if (next > SNDRV_GF1_MAX_OFFSET)
			next = SNDRV_GF1_MAX_OFFSET;
		if (old_volume == next) {
			spin_unlock_irqrestore(&gus->reg_lock, flags);
			continue;
		}
		voice->volume_control &= ~0xc3;
		voice->volume_control |= 0x20;
		if (old_volume > next) {
			snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_START, next);
			snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_END, old_volume);
			voice->volume_control |= 0x40;
		} else {
			snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_START, old_volume);
			snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_END, next);
		}
		snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_RATE, rate);
		snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_CONTROL, voice->volume_control);
		if (!gus->gf1.enh_mode) {
			snd_gf1_delay(gus);
			snd_gf1_write8(gus, SNDRV_GF1_VB_VOLUME_CONTROL, voice->volume_control);
		}
		spin_unlock_irqrestore(&gus->reg_lock, flags);			
		return;
	}
}

static void do_pan_envelope(struct snd_gus_card *gus, struct snd_gus_voice *voice)
{
	unsigned long flags;
	unsigned char old_pan;

#if 0
	snd_gf1_select_voice(gus, voice->number);
	printk(" -%i- do_pan_envelope - flags = 0x%x (0x%x -> 0x%x)\n",
		voice->number,
		voice->flags,
		voice->gf1_pan,
		snd_gf1_i_read8(gus, SNDRV_GF1_VB_PAN) & 0x0f);
#endif
	if (gus->gf1.enh_mode) {
		voice->flags &= ~(SNDRV_GF1_VFLG_EFFECT_TIMER1|SNDRV_GF1_VFLG_PAN);
		return;
	}
	if (!gus->gf1.smooth_pan) {
		spin_lock_irqsave(&gus->reg_lock, flags);			
		snd_gf1_select_voice(gus, voice->number);
		snd_gf1_write8(gus, SNDRV_GF1_VB_PAN, voice->gf1_pan);
		spin_unlock_irqrestore(&gus->reg_lock, flags);
		return;
	}
	if (!(voice->flags & SNDRV_GF1_VFLG_PAN))		/* before */
		voice->flags |= SNDRV_GF1_VFLG_EFFECT_TIMER1|SNDRV_GF1_VFLG_PAN;
	spin_lock_irqsave(&gus->reg_lock, flags);			
	snd_gf1_select_voice(gus, voice->number);
	old_pan = snd_gf1_read8(gus, SNDRV_GF1_VB_PAN) & 0x0f;
	if (old_pan > voice->gf1_pan )
		old_pan--;
	if (old_pan < voice->gf1_pan)
		old_pan++;
	snd_gf1_write8(gus, SNDRV_GF1_VB_PAN, old_pan);
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	if (old_pan == voice->gf1_pan)			/* the goal was reached */
		voice->flags &= ~(SNDRV_GF1_VFLG_EFFECT_TIMER1|SNDRV_GF1_VFLG_PAN);
#if 0
	snd_gf1_select_voice(gus, voice->number);
	printk(" -%i- (1) do_pan_envelope - flags = 0x%x (0x%x -> 0x%x)\n",
	       voice->number,
	       voice->flags,
	       voice->gf1_pan,
	       snd_gf1_i_read8(gus, GF1_VB_PAN) & 0x0f);
#endif
}

static void set_enhanced_pan(struct snd_gus_card *gus, struct snd_gus_voice *voice, unsigned short pan)
{
	unsigned long flags;
	unsigned short vlo, vro;
  
	vlo = SNDRV_GF1_ATTEN((SNDRV_GF1_ATTEN_TABLE_SIZE-1) - pan);
	vro = SNDRV_GF1_ATTEN(pan);
	if (pan != SNDRV_GF1_ATTEN_TABLE_SIZE - 1 && pan != 0) {
		vlo >>= 1;
		vro >>= 1;
	}
	vlo <<= 4;
	vro <<= 4;
#if 0
	printk("vlo = 0x%x (0x%x), vro = 0x%x (0x%x)\n",
			vlo, snd_gf1_i_read16(gus, GF1_VW_OFFSET_LEFT),
			vro, snd_gf1_i_read16(gus, GF1_VW_OFFSET_RIGHT));
#endif
	spin_lock_irqsave(&gus->reg_lock, flags);			
	snd_gf1_select_voice(gus, voice->number);
        snd_gf1_write16(gus, SNDRV_GF1_VW_OFFSET_LEFT_FINAL, vlo);
	snd_gf1_write16(gus, SNDRV_GF1_VW_OFFSET_RIGHT_FINAL, vro);
	spin_unlock_irqrestore(&gus->reg_lock, flags);			
	voice->vlo = vlo;
	voice->vro = vro;
}

/*
 *
 */

static void sample_start(struct snd_gus_card *gus, struct snd_gus_voice *voice, snd_seq_position_t position)
{
	unsigned long flags;
	unsigned int begin, addr, addr_end, addr_start;
	int w_16;
	struct simple_instrument *simple;
	struct snd_seq_kinstr *instr;

	instr = snd_seq_instr_find(gus->gf1.ilist, &voice->instr, 0, 1);
	if (instr == NULL)
		return;
	voice->instr = instr->instr;	/* copy ID to speedup aliases */
	simple = KINSTR_DATA(instr);
	begin = simple->address.memory << 4;
	w_16 = simple->format & SIMPLE_WAVE_16BIT ? 0x04 : 0;
	addr_start = simple->loop_start;
	if (simple->format & SIMPLE_WAVE_LOOP) {
		addr_end = simple->loop_end;
	} else {
		addr_end = (simple->size << 4) - (w_16 ? 40 : 24);
	}
	if (simple->format & SIMPLE_WAVE_BACKWARD) {
		addr = simple->loop_end;
		if (position < simple->loop_end)
			addr -= position;
	} else {
		addr = position;
	}
	voice->control = 0x00;
	voice->mode = 0x20;		/* enable offset registers */
	if (simple->format & SIMPLE_WAVE_16BIT)
		voice->control |= 0x04;
	if (simple->format & SIMPLE_WAVE_BACKWARD)
		voice->control |= 0x40;
	if (simple->format & SIMPLE_WAVE_LOOP) {
		voice->control |= 0x08;
	} else {
		voice->control |= 0x20;
	}
	if (simple->format & SIMPLE_WAVE_BIDIR)
		voice->control |= 0x10;
	if (simple->format & SIMPLE_WAVE_ULAW)
		voice->mode |= 0x40;
	if (w_16) {
		addr = ((addr << 1) & ~0x1f) | (addr & 0x0f);
		addr_start = ((addr_start << 1) & ~0x1f) | (addr_start & 0x0f);
		addr_end = ((addr_end << 1) & ~0x1f) | (addr_end & 0x0f);
	}
	addr += begin;
	addr_start += begin;
	addr_end += begin;
	snd_gf1_stop_voice(gus, voice->number);	
	spin_lock_irqsave(&gus->reg_lock, flags);
	snd_gf1_select_voice(gus, voice->number);
	snd_gf1_write16(gus, SNDRV_GF1_VW_FREQUENCY, voice->fc_register + voice->fc_lfo);
	voice->venv_state = VENV_BEFORE;
	voice->volume_control = 0x03;
	snd_gf1_write_addr(gus, SNDRV_GF1_VA_START, addr_start, w_16);
	snd_gf1_write_addr(gus, SNDRV_GF1_VA_END, addr_end, w_16);
	snd_gf1_write_addr(gus, SNDRV_GF1_VA_CURRENT, addr, w_16);
	if (!gus->gf1.enh_mode) {
		snd_gf1_write8(gus, SNDRV_GF1_VB_PAN, voice->gf1_pan);
	} else {
		snd_gf1_write16(gus, SNDRV_GF1_VW_OFFSET_LEFT, voice->vlo);
		snd_gf1_write16(gus, SNDRV_GF1_VW_OFFSET_LEFT_FINAL, voice->vlo);
		snd_gf1_write16(gus, SNDRV_GF1_VW_OFFSET_RIGHT, voice->vro);
		snd_gf1_write16(gus, SNDRV_GF1_VW_OFFSET_RIGHT_FINAL, voice->vro);
		snd_gf1_write8(gus, SNDRV_GF1_VB_ACCUMULATOR, voice->effect_accumulator);
		snd_gf1_write16(gus, SNDRV_GF1_VW_EFFECT_VOLUME, voice->gf1_effect_volume);
		snd_gf1_write16(gus, SNDRV_GF1_VW_EFFECT_VOLUME_FINAL, voice->gf1_effect_volume);
	}
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	do_volume_envelope(gus, voice);
	spin_lock_irqsave(&gus->reg_lock, flags);
	snd_gf1_select_voice(gus, voice->number);
	if (gus->gf1.enh_mode)
		snd_gf1_write8(gus, SNDRV_GF1_VB_MODE, voice->mode);
	snd_gf1_write8(gus, SNDRV_GF1_VB_ADDRESS_CONTROL, voice->control);
	if (!gus->gf1.enh_mode) {
		snd_gf1_delay(gus);
		snd_gf1_write8(gus, SNDRV_GF1_VB_ADDRESS_CONTROL, voice->control );
	}
	spin_unlock_irqrestore(&gus->reg_lock, flags);
#if 0
	snd_gf1_print_voice_registers(gus);
#endif
	voice->flags |= SNDRV_GF1_VFLG_RUNNING;
	snd_seq_instr_free_use(gus->gf1.ilist, instr);
}

static void sample_stop(struct snd_gus_card *gus, struct snd_gus_voice *voice, int mode)
{
	unsigned char control;
	unsigned long flags;

	if (!(voice->flags & SNDRV_GF1_VFLG_RUNNING))
		return;
	switch (mode) {
	default:
		if (gus->gf1.volume_ramp > 0) {
			if (voice->venv_state < VENV_RELEASE) {
				voice->venv_state = VENV_RELEASE;
				do_volume_envelope(gus, voice);
			}
		}
		if (mode != SAMPLE_STOP_VENVELOPE) {
			snd_gf1_stop_voice(gus, voice->number);
			spin_lock_irqsave(&gus->reg_lock, flags);
			snd_gf1_select_voice(gus, voice->number);
			snd_gf1_write16(gus, SNDRV_GF1_VW_VOLUME, SNDRV_GF1_MIN_VOLUME);
			spin_unlock_irqrestore(&gus->reg_lock, flags);
			voice->flags &= ~SNDRV_GF1_VFLG_RUNNING;
		}
		break;
	case SAMPLE_STOP_LOOP:		/* disable loop only */
		spin_lock_irqsave(&gus->reg_lock, flags);
		snd_gf1_select_voice(gus, voice->number);
		control = snd_gf1_read8(gus, SNDRV_GF1_VB_ADDRESS_CONTROL);
		control &= ~(0x83 | 0x04);
		control |= 0x20;
		snd_gf1_write8(gus, SNDRV_GF1_VB_ADDRESS_CONTROL, control);
		spin_unlock_irqrestore(&gus->reg_lock, flags);
		break;
	}
}

static void sample_freq(struct snd_gus_card *gus, struct snd_gus_voice *voice, snd_seq_frequency_t freq)
{
	unsigned long flags;

	spin_lock_irqsave(&gus->reg_lock, flags);
	voice->fc_register = snd_gf1_translate_freq(gus, freq);
	snd_gf1_select_voice(gus, voice->number);
	snd_gf1_write16(gus, SNDRV_GF1_VW_FREQUENCY, voice->fc_register + voice->fc_lfo);
	spin_unlock_irqrestore(&gus->reg_lock, flags);
}

static void sample_volume(struct snd_gus_card *gus, struct snd_gus_voice *voice, struct snd_seq_ev_volume *volume)
{
	if (volume->volume >= 0) {
		volume->volume &= 0x3fff;
		voice->gf1_volume = snd_gf1_lvol_to_gvol_raw(volume->volume << 2) << 4;
		voice->venv_state_prev = VENV_SUSTAIN;
		voice->venv_state = VENV_VOLUME;
		do_volume_envelope(gus, voice);
        }
	if (volume->lr >= 0) {
		volume->lr &= 0x3fff;
		if (!gus->gf1.enh_mode) {
			voice->gf1_pan = (volume->lr >> 10) & 15;
			if (!gus->gf1.full_range_pan) {
				if (voice->gf1_pan == 0)
					voice->gf1_pan++;
				if (voice->gf1_pan == 15)
					voice->gf1_pan--;
			}
			voice->flags &= ~SNDRV_GF1_VFLG_PAN;	/* before */
			do_pan_envelope(gus, voice);
		} else {
			set_enhanced_pan(gus, voice, volume->lr >> 7);
		}
	}
}

static void sample_loop(struct snd_gus_card *gus, struct snd_gus_voice *voice, struct snd_seq_ev_loop *loop)
{
	unsigned long flags;
	int w_16 = voice->control & 0x04;
	unsigned int begin, addr_start, addr_end;
	struct simple_instrument *simple;
	struct snd_seq_kinstr *instr;

#if 0
	printk("voice_loop: start = 0x%x, end = 0x%x\n", loop->start, loop->end);
#endif
	instr = snd_seq_instr_find(gus->gf1.ilist, &voice->instr, 0, 1);
	if (instr == NULL)
		return;
	voice->instr = instr->instr;	/* copy ID to speedup aliases */
	simple = KINSTR_DATA(instr);
	begin = simple->address.memory;
	addr_start = loop->start;
	addr_end = loop->end;
	addr_start = (((addr_start << 1) & ~0x1f) | (addr_start & 0x0f)) + begin;
	addr_end = (((addr_end << 1) & ~0x1f) | (addr_end & 0x0f)) + begin;
	spin_lock_irqsave(&gus->reg_lock, flags);
	snd_gf1_select_voice(gus, voice->number);
	snd_gf1_write_addr(gus, SNDRV_GF1_VA_START, addr_start, w_16);
	snd_gf1_write_addr(gus, SNDRV_GF1_VA_END, addr_end, w_16);
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	snd_seq_instr_free_use(gus->gf1.ilist, instr);
}

static void sample_pos(struct snd_gus_card *gus, struct snd_gus_voice *voice, snd_seq_position_t position)
{
	unsigned long flags;
	int w_16 = voice->control & 0x04;
	unsigned int begin, addr;
	struct simple_instrument *simple;
	struct snd_seq_kinstr *instr;

#if 0
	printk("voice_loop: start = 0x%x, end = 0x%x\n", loop->start, loop->end);
#endif
	instr = snd_seq_instr_find(gus->gf1.ilist, &voice->instr, 0, 1);
	if (instr == NULL)
		return;
	voice->instr = instr->instr;	/* copy ID to speedup aliases */
	simple = KINSTR_DATA(instr);
	begin = simple->address.memory;
	addr = (((position << 1) & ~0x1f) | (position & 0x0f)) + begin;
	spin_lock_irqsave(&gus->reg_lock, flags);
	snd_gf1_select_voice(gus, voice->number);
	snd_gf1_write_addr(gus, SNDRV_GF1_VA_CURRENT, addr, w_16);
	spin_unlock_irqrestore(&gus->reg_lock, flags);
	snd_seq_instr_free_use(gus->gf1.ilist, instr);
}

#if 0

static unsigned char get_effects_mask( ultra_card_t *card, int value )
{
  if ( value > 7 ) return 0;
  if ( card -> gf1.effects && card -> gf1.effects -> chip_type == ULTRA_EFFECT_CHIP_INTERWAVE )
    return card -> gf1.effects -> chip.interwave.voice_output[ value ];
  return 0;
}

#endif

static void sample_private1(struct snd_gus_card *card, struct snd_gus_voice *voice, unsigned char *data)
{
#if 0
  unsigned long flags;
  unsigned char uc;

  switch ( *data ) {
    case ULTRA_PRIV1_IW_EFFECT:
      uc = get_effects_mask( card, ultra_get_byte( data, 4 ) );
      uc |= get_effects_mask( card, ultra_get_byte( data, 4 ) >> 4 );
      uc |= get_effects_mask( card, ultra_get_byte( data, 5 ) );
      uc |= get_effects_mask( card, ultra_get_byte( data, 5 ) >> 4 );
      voice -> data.simple.effect_accumulator = uc;
      voice -> data.simple.effect_volume = ultra_translate_voice_volume( card, ultra_get_word( data, 2 ) ) << 4;
      if ( !card -> gf1.enh_mode ) return;
      if ( voice -> flags & VFLG_WAIT_FOR_START ) return;
      if ( voice -> flags & VFLG_RUNNING )
        {
          CLI( &flags );
          gf1_select_voice( card, voice -> number );
          ultra_write8( card, GF1_VB_ACCUMULATOR, voice -> data.simple.effect_accumulator );
          ultra_write16( card, GF1_VW_EFFECT_VOLUME_FINAL, voice -> data.simple.effect_volume );
          STI( &flags );
        }
      break;
   case ULTRA_PRIV1_IW_LFO:
     ultra_lfo_command( card, voice -> number, data );
  }
#endif
}

#if 0

/*
 *
 */

static void note_stop( ultra_card_t *card, ultra_voice_t *voice, int wait )
{
}

static void note_wait( ultra_card_t *card, ultra_voice_t *voice )
{
}

static void note_off( ultra_card_t *card, ultra_voice_t *voice )
{
}

static void note_volume( ultra_card_t *card, ultra_voice_t *voice )
{
}

static void note_pitchbend( ultra_card_t *card, ultra_voice_t *voice )
{
}

static void note_vibrato( ultra_card_t *card, ultra_voice_t *voice )
{
}

static void note_tremolo( ultra_card_t *card, ultra_voice_t *voice )
{
}

/*
 *
 */
 
static void chn_trigger_down( ultra_card_t *card, ultra_channel_t *channel, ultra_instrument_t *instrument, unsigned char note, unsigned char velocity, unsigned char priority )
{
}

static void chn_trigger_up( ultra_card_t *card, ultra_note_t *note )
{
}

static void chn_control( ultra_card_t *card, ultra_channel_t *channel, unsigned short p1, unsigned short p2 )
{
}

/*
 *
 */
 
#endif

void snd_gf1_simple_init(struct snd_gus_voice *voice)
{
	voice->handler_wave = interrupt_wave;
	voice->handler_volume = interrupt_volume;
	voice->handler_effect = interrupt_effect;
	voice->volume_change = NULL;
	voice->sample_ops = &sample_ops;
}
