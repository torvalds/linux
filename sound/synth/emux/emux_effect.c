/*
 *  Midi synth routines for the Emu8k/Emu10k1
 *
 *  Copyright (C) 1999 Steve Ratcliffe
 *  Copyright (c) 1999-2000 Takashi Iwai <tiwai@suse.de>
 *
 *  Contains code based on awe_wave.c by Takashi Iwai
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

#include "emux_voice.h"
#include <linux/slab.h>

#ifdef SNDRV_EMUX_USE_RAW_EFFECT
/*
 * effects table
 */

#define xoffsetof(type,tag)	((long)(&((type)NULL)->tag) - (long)(NULL))

#define parm_offset(tag)	xoffsetof(struct soundfont_voice_parm *, tag)

#define PARM_IS_BYTE		(1 << 0)
#define PARM_IS_WORD		(1 << 1)
#define PARM_IS_ALIGNED		(3 << 2)
#define PARM_IS_ALIGN_HI	(1 << 2)
#define PARM_IS_ALIGN_LO	(2 << 2)
#define PARM_IS_SIGNED		(1 << 4)

#define PARM_WORD	(PARM_IS_WORD)
#define PARM_BYTE_LO	(PARM_IS_BYTE|PARM_IS_ALIGN_LO)
#define PARM_BYTE_HI	(PARM_IS_BYTE|PARM_IS_ALIGN_HI)
#define PARM_BYTE	(PARM_IS_BYTE)
#define PARM_SIGN_LO	(PARM_IS_BYTE|PARM_IS_ALIGN_LO|PARM_IS_SIGNED)
#define PARM_SIGN_HI	(PARM_IS_BYTE|PARM_IS_ALIGN_HI|PARM_IS_SIGNED)

static struct emux_parm_defs {
	int type;	/* byte or word */
	int low, high;	/* value range */
	long offset;	/* offset in parameter record (-1 = not written) */
	int update;	/* flgas for real-time update */
} parm_defs[EMUX_NUM_EFFECTS] = {
	{PARM_WORD, 0, 0x8000, parm_offset(moddelay), 0},	/* env1 delay */
	{PARM_BYTE_LO, 1, 0x80, parm_offset(modatkhld), 0},	/* env1 attack */
	{PARM_BYTE_HI, 0, 0x7e, parm_offset(modatkhld), 0},	/* env1 hold */
	{PARM_BYTE_LO, 1, 0x7f, parm_offset(moddcysus), 0},	/* env1 decay */
	{PARM_BYTE_LO, 1, 0x7f, parm_offset(modrelease), 0},	/* env1 release */
	{PARM_BYTE_HI, 0, 0x7f, parm_offset(moddcysus), 0},	/* env1 sustain */
	{PARM_BYTE_HI, 0, 0xff, parm_offset(pefe), 0},	/* env1 pitch */
	{PARM_BYTE_LO, 0, 0xff, parm_offset(pefe), 0},	/* env1 fc */

	{PARM_WORD, 0, 0x8000, parm_offset(voldelay), 0},	/* env2 delay */
	{PARM_BYTE_LO, 1, 0x80, parm_offset(volatkhld), 0},	/* env2 attack */
	{PARM_BYTE_HI, 0, 0x7e, parm_offset(volatkhld), 0},	/* env2 hold */
	{PARM_BYTE_LO, 1, 0x7f, parm_offset(voldcysus), 0},	/* env2 decay */
	{PARM_BYTE_LO, 1, 0x7f, parm_offset(volrelease), 0},	/* env2 release */
	{PARM_BYTE_HI, 0, 0x7f, parm_offset(voldcysus), 0},	/* env2 sustain */

	{PARM_WORD, 0, 0x8000, parm_offset(lfo1delay), 0},	/* lfo1 delay */
	{PARM_BYTE_LO, 0, 0xff, parm_offset(tremfrq), SNDRV_EMUX_UPDATE_TREMFREQ},	/* lfo1 freq */
	{PARM_SIGN_HI, -128, 127, parm_offset(tremfrq), SNDRV_EMUX_UPDATE_TREMFREQ},	/* lfo1 vol */
	{PARM_SIGN_HI, -128, 127, parm_offset(fmmod), SNDRV_EMUX_UPDATE_FMMOD},	/* lfo1 pitch */
	{PARM_BYTE_LO, 0, 0xff, parm_offset(fmmod), SNDRV_EMUX_UPDATE_FMMOD},	/* lfo1 cutoff */

	{PARM_WORD, 0, 0x8000, parm_offset(lfo2delay), 0},	/* lfo2 delay */
	{PARM_BYTE_LO, 0, 0xff, parm_offset(fm2frq2), SNDRV_EMUX_UPDATE_FM2FRQ2},	/* lfo2 freq */
	{PARM_SIGN_HI, -128, 127, parm_offset(fm2frq2), SNDRV_EMUX_UPDATE_FM2FRQ2},	/* lfo2 pitch */

	{PARM_WORD, 0, 0xffff, -1, SNDRV_EMUX_UPDATE_PITCH},	/* initial pitch */
	{PARM_BYTE, 0, 0xff, parm_offset(chorus), 0},	/* chorus */
	{PARM_BYTE, 0, 0xff, parm_offset(reverb), 0},	/* reverb */
	{PARM_BYTE, 0, 0xff, parm_offset(cutoff), SNDRV_EMUX_UPDATE_VOLUME},	/* cutoff */
	{PARM_BYTE, 0, 15, parm_offset(filterQ), SNDRV_EMUX_UPDATE_Q},	/* resonance */

	{PARM_WORD, 0, 0xffff, -1, 0},	/* sample start */
	{PARM_WORD, 0, 0xffff, -1, 0},	/* loop start */
	{PARM_WORD, 0, 0xffff, -1, 0},	/* loop end */
	{PARM_WORD, 0, 0xffff, -1, 0},	/* coarse sample start */
	{PARM_WORD, 0, 0xffff, -1, 0},	/* coarse loop start */
	{PARM_WORD, 0, 0xffff, -1, 0},	/* coarse loop end */
	{PARM_BYTE, 0, 0xff, -1, SNDRV_EMUX_UPDATE_VOLUME},	/* initial attenuation */
};

/* set byte effect value */
static void
effect_set_byte(unsigned char *valp, struct snd_midi_channel *chan, int type)
{
	short effect;
	struct snd_emux_effect_table *fx = chan->private;

	effect = fx->val[type];
	if (fx->flag[type] == EMUX_FX_FLAG_ADD) {
		if (parm_defs[type].type & PARM_IS_SIGNED)
			effect += *(char*)valp;
		else
			effect += *valp;
	}
	if (effect < parm_defs[type].low)
		effect = parm_defs[type].low;
	else if (effect > parm_defs[type].high)
		effect = parm_defs[type].high;
	*valp = (unsigned char)effect;
}

/* set word effect value */
static void
effect_set_word(unsigned short *valp, struct snd_midi_channel *chan, int type)
{
	int effect;
	struct snd_emux_effect_table *fx = chan->private;

	effect = *(unsigned short*)&fx->val[type];
	if (fx->flag[type] == EMUX_FX_FLAG_ADD)
		effect += *valp;
	if (effect < parm_defs[type].low)
		effect = parm_defs[type].low;
	else if (effect > parm_defs[type].high)
		effect = parm_defs[type].high;
	*valp = (unsigned short)effect;
}

/* address offset */
static int
effect_get_offset(struct snd_midi_channel *chan, int lo, int hi, int mode)
{
	int addr = 0;
	struct snd_emux_effect_table *fx = chan->private;

	if (fx->flag[hi])
		addr = (short)fx->val[hi];
	addr = addr << 15;
	if (fx->flag[lo])
		addr += (short)fx->val[lo];
	if (!(mode & SNDRV_SFNT_SAMPLE_8BITS))
		addr /= 2;
	return addr;
}

#if IS_ENABLED(CONFIG_SND_SEQUENCER_OSS)
/* change effects - for OSS sequencer compatibility */
void
snd_emux_send_effect_oss(struct snd_emux_port *port,
			 struct snd_midi_channel *chan, int type, int val)
{
	int mode;

	if (type & 0x40)
		mode = EMUX_FX_FLAG_OFF;
	else if (type & 0x80)
		mode = EMUX_FX_FLAG_ADD;
	else
		mode = EMUX_FX_FLAG_SET;
	type &= 0x3f;

	snd_emux_send_effect(port, chan, type, val, mode);
}
#endif

/* Modify the effect value.
 * if update is necessary, call emu8000_control
 */
void
snd_emux_send_effect(struct snd_emux_port *port, struct snd_midi_channel *chan,
		     int type, int val, int mode)
{
	int i;
	int offset;
	unsigned char *srcp, *origp;
	struct snd_emux *emu;
	struct snd_emux_effect_table *fx;
	unsigned long flags;

	emu = port->emu;
	fx = chan->private;
	if (emu == NULL || fx == NULL)
		return;
	if (type < 0 || type >= EMUX_NUM_EFFECTS)
		return;

	fx->val[type] = val;
	fx->flag[type] = mode;

	/* do we need to modify the register in realtime ? */
	if (! parm_defs[type].update || (offset = parm_defs[type].offset) < 0)
		return;

#ifdef SNDRV_LITTLE_ENDIAN
	if (parm_defs[type].type & PARM_IS_ALIGN_HI)
		offset++;
#else
	if (parm_defs[type].type & PARM_IS_ALIGN_LO)
		offset++;
#endif
	/* modify the register values */
	spin_lock_irqsave(&emu->voice_lock, flags);
	for (i = 0; i < emu->max_voices; i++) {
		struct snd_emux_voice *vp = &emu->voices[i];
		if (!STATE_IS_PLAYING(vp->state) || vp->chan != chan)
			continue;
		srcp = (unsigned char*)&vp->reg.parm + offset;
		origp = (unsigned char*)&vp->zone->v.parm + offset;
		if (parm_defs[i].type & PARM_IS_BYTE) {
			*srcp = *origp;
			effect_set_byte(srcp, chan, type);
		} else {
			*(unsigned short*)srcp = *(unsigned short*)origp;
			effect_set_word((unsigned short*)srcp, chan, type);
		}
	}
	spin_unlock_irqrestore(&emu->voice_lock, flags);

	/* activate them */
	snd_emux_update_channel(port, chan, parm_defs[type].update);
}


/* copy wavetable registers to voice table */
void
snd_emux_setup_effect(struct snd_emux_voice *vp)
{
	struct snd_midi_channel *chan = vp->chan;
	struct snd_emux_effect_table *fx;
	unsigned char *srcp;
	int i;

	if (! (fx = chan->private))
		return;

	/* modify the register values via effect table */
	for (i = 0; i < EMUX_FX_END; i++) {
		int offset;
		if (! fx->flag[i] || (offset = parm_defs[i].offset) < 0)
			continue;
#ifdef SNDRV_LITTLE_ENDIAN
		if (parm_defs[i].type & PARM_IS_ALIGN_HI)
			offset++;
#else
		if (parm_defs[i].type & PARM_IS_ALIGN_LO)
			offset++;
#endif
		srcp = (unsigned char*)&vp->reg.parm + offset;
		if (parm_defs[i].type & PARM_IS_BYTE)
			effect_set_byte(srcp, chan, i);
		else
			effect_set_word((unsigned short*)srcp, chan, i);
	}

	/* correct sample and loop points */
	vp->reg.start += effect_get_offset(chan, EMUX_FX_SAMPLE_START,
					   EMUX_FX_COARSE_SAMPLE_START,
					   vp->reg.sample_mode);

	vp->reg.loopstart += effect_get_offset(chan, EMUX_FX_LOOP_START,
					       EMUX_FX_COARSE_LOOP_START,
					       vp->reg.sample_mode);

	vp->reg.loopend += effect_get_offset(chan, EMUX_FX_LOOP_END,
					     EMUX_FX_COARSE_LOOP_END,
					     vp->reg.sample_mode);
}

/*
 * effect table
 */
void
snd_emux_create_effect(struct snd_emux_port *p)
{
	int i;
	p->effect = kcalloc(p->chset.max_channels,
			    sizeof(struct snd_emux_effect_table), GFP_KERNEL);
	if (p->effect) {
		for (i = 0; i < p->chset.max_channels; i++)
			p->chset.channels[i].private = p->effect + i;
	} else {
		for (i = 0; i < p->chset.max_channels; i++)
			p->chset.channels[i].private = NULL;
	}
}

void
snd_emux_delete_effect(struct snd_emux_port *p)
{
	kfree(p->effect);
	p->effect = NULL;
}

void
snd_emux_clear_effect(struct snd_emux_port *p)
{
	if (p->effect) {
		memset(p->effect, 0, sizeof(struct snd_emux_effect_table) *
		       p->chset.max_channels);
	}
}

#endif /* SNDRV_EMUX_USE_RAW_EFFECT */
