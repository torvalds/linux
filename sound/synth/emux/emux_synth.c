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
#include <sound/asoundef.h>

/*
 * Prototypes
 */

/*
 * Ensure a value is between two points
 * macro evaluates its args more than once, so changed to upper-case.
 */
#define LIMITVALUE(x, a, b) do { if ((x) < (a)) (x) = (a); else if ((x) > (b)) (x) = (b); } while (0)
#define LIMITMAX(x, a) do {if ((x) > (a)) (x) = (a); } while (0)

static int get_zone(struct snd_emux *emu, struct snd_emux_port *port,
		    int *notep, int vel, struct snd_midi_channel *chan,
		    struct snd_sf_zone **table);
static int get_bank(struct snd_emux_port *port, struct snd_midi_channel *chan);
static void terminate_note1(struct snd_emux *emu, int note,
			    struct snd_midi_channel *chan, int free);
static void exclusive_note_off(struct snd_emux *emu, struct snd_emux_port *port,
			       int exclass);
static void terminate_voice(struct snd_emux *emu, struct snd_emux_voice *vp, int free);
static void update_voice(struct snd_emux *emu, struct snd_emux_voice *vp, int update);
static void setup_voice(struct snd_emux_voice *vp);
static int calc_pan(struct snd_emux_voice *vp);
static int calc_volume(struct snd_emux_voice *vp);
static int calc_pitch(struct snd_emux_voice *vp);


/*
 * Start a note.
 */
void
snd_emux_note_on(void *p, int note, int vel, struct snd_midi_channel *chan)
{
	struct snd_emux *emu;
	int i, key, nvoices;
	struct snd_emux_voice *vp;
	struct snd_sf_zone *table[SNDRV_EMUX_MAX_MULTI_VOICES];
	unsigned long flags;
	struct snd_emux_port *port;

	port = p;
	snd_assert(port != NULL && chan != NULL, return);

	emu = port->emu;
	snd_assert(emu != NULL, return);
	snd_assert(emu->ops.get_voice != NULL, return);
	snd_assert(emu->ops.trigger != NULL, return);

	key = note; /* remember the original note */
	nvoices = get_zone(emu, port, &note, vel, chan, table);
	if (! nvoices)
		return;

	/* exclusive note off */
	for (i = 0; i < nvoices; i++) {
		struct snd_sf_zone *zp = table[i];
		if (zp && zp->v.exclusiveClass)
			exclusive_note_off(emu, port, zp->v.exclusiveClass);
	}

#if 0 // seems not necessary
	/* Turn off the same note on the same channel. */
	terminate_note1(emu, key, chan, 0);
#endif

	spin_lock_irqsave(&emu->voice_lock, flags);
	for (i = 0; i < nvoices; i++) {

		/* set up each voice parameter */
		/* at this stage, we don't trigger the voice yet. */

		if (table[i] == NULL)
			continue;

		vp = emu->ops.get_voice(emu, port);
		if (vp == NULL || vp->ch < 0)
			continue;
		if (STATE_IS_PLAYING(vp->state))
			emu->ops.terminate(vp);

		vp->time = emu->use_time++;
		vp->chan = chan;
		vp->port = port;
		vp->key = key;
		vp->note = note;
		vp->velocity = vel;
		vp->zone = table[i];
		if (vp->zone->sample)
			vp->block = vp->zone->sample->block;
		else
			vp->block = NULL;

		setup_voice(vp);

		vp->state = SNDRV_EMUX_ST_STANDBY;
		if (emu->ops.prepare) {
			vp->state = SNDRV_EMUX_ST_OFF;
			if (emu->ops.prepare(vp) >= 0)
				vp->state = SNDRV_EMUX_ST_STANDBY;
		}
	}

	/* start envelope now */
	for (i = 0; i < emu->max_voices; i++) {
		vp = &emu->voices[i];
		if (vp->state == SNDRV_EMUX_ST_STANDBY &&
		    vp->chan == chan) {
			emu->ops.trigger(vp);
			vp->state = SNDRV_EMUX_ST_ON;
			vp->ontime = jiffies; /* remember the trigger timing */
		}
	}
	spin_unlock_irqrestore(&emu->voice_lock, flags);

#ifdef SNDRV_EMUX_USE_RAW_EFFECT
	if (port->port_mode == SNDRV_EMUX_PORT_MODE_OSS_SYNTH) {
		/* clear voice position for the next note on this channel */
		struct snd_emux_effect_table *fx = chan->private;
		if (fx) {
			fx->flag[EMUX_FX_SAMPLE_START] = 0;
			fx->flag[EMUX_FX_COARSE_SAMPLE_START] = 0;
		}
	}
#endif
}

/*
 * Release a note in response to a midi note off.
 */
void
snd_emux_note_off(void *p, int note, int vel, struct snd_midi_channel *chan)
{
	int ch;
	struct snd_emux *emu;
	struct snd_emux_voice *vp;
	unsigned long flags;
	struct snd_emux_port *port;

	port = p;
	snd_assert(port != NULL && chan != NULL, return);

	emu = port->emu;
	snd_assert(emu != NULL, return);
	snd_assert(emu->ops.release != NULL, return);

	spin_lock_irqsave(&emu->voice_lock, flags);
	for (ch = 0; ch < emu->max_voices; ch++) {
		vp = &emu->voices[ch];
		if (STATE_IS_PLAYING(vp->state) &&
		    vp->chan == chan && vp->key == note) {
			vp->state = SNDRV_EMUX_ST_RELEASED;
			if (vp->ontime == jiffies) {
				/* if note-off is sent too shortly after
				 * note-on, emuX engine cannot produce the sound
				 * correctly.  so we'll release this note
				 * a bit later via timer callback.
				 */
				vp->state = SNDRV_EMUX_ST_PENDING;
				if (! emu->timer_active) {
					emu->tlist.expires = jiffies + 1;
					add_timer(&emu->tlist);
					emu->timer_active = 1;
				}
			} else
				/* ok now release the note */
				emu->ops.release(vp);
		}
	}
	spin_unlock_irqrestore(&emu->voice_lock, flags);
}

/*
 * timer callback
 *
 * release the pending note-offs
 */
void snd_emux_timer_callback(unsigned long data)
{
	struct snd_emux *emu = (struct snd_emux *) data;
	struct snd_emux_voice *vp;
	unsigned long flags;
	int ch, do_again = 0;

	spin_lock_irqsave(&emu->voice_lock, flags);
	for (ch = 0; ch < emu->max_voices; ch++) {
		vp = &emu->voices[ch];
		if (vp->state == SNDRV_EMUX_ST_PENDING) {
			if (vp->ontime == jiffies)
				do_again++; /* release this at the next interrupt */
			else {
				emu->ops.release(vp);
				vp->state = SNDRV_EMUX_ST_RELEASED;
			}
		}
	}
	if (do_again) {
		emu->tlist.expires = jiffies + 1;
		add_timer(&emu->tlist);
		emu->timer_active = 1;
	} else
		emu->timer_active = 0;
	spin_unlock_irqrestore(&emu->voice_lock, flags);
}

/*
 * key pressure change
 */
void
snd_emux_key_press(void *p, int note, int vel, struct snd_midi_channel *chan)
{
	int ch;
	struct snd_emux *emu;
	struct snd_emux_voice *vp;
	unsigned long flags;
	struct snd_emux_port *port;

	port = p;
	snd_assert(port != NULL && chan != NULL, return);

	emu = port->emu;
	snd_assert(emu != NULL, return);
	snd_assert(emu->ops.update != NULL, return);

	spin_lock_irqsave(&emu->voice_lock, flags);
	for (ch = 0; ch < emu->max_voices; ch++) {
		vp = &emu->voices[ch];
		if (vp->state == SNDRV_EMUX_ST_ON &&
		    vp->chan == chan && vp->key == note) {
			vp->velocity = vel;
			update_voice(emu, vp, SNDRV_EMUX_UPDATE_VOLUME);
		}
	}
	spin_unlock_irqrestore(&emu->voice_lock, flags);
}


/*
 * Modulate the voices which belong to the channel
 */
void
snd_emux_update_channel(struct snd_emux_port *port, struct snd_midi_channel *chan, int update)
{
	struct snd_emux *emu;
	struct snd_emux_voice *vp;
	int i;
	unsigned long flags;

	if (! update)
		return;

	emu = port->emu;
	snd_assert(emu != NULL, return);
	snd_assert(emu->ops.update != NULL, return);

	spin_lock_irqsave(&emu->voice_lock, flags);
	for (i = 0; i < emu->max_voices; i++) {
		vp = &emu->voices[i];
		if (vp->chan == chan)
			update_voice(emu, vp, update);
	}
	spin_unlock_irqrestore(&emu->voice_lock, flags);
}

/*
 * Modulate all the voices which belong to the port.
 */
void
snd_emux_update_port(struct snd_emux_port *port, int update)
{
	struct snd_emux *emu; 
	struct snd_emux_voice *vp;
	int i;
	unsigned long flags;

	if (! update)
		return;

	emu = port->emu;
	snd_assert(emu != NULL, return);
	snd_assert(emu->ops.update != NULL, return);

	spin_lock_irqsave(&emu->voice_lock, flags);
	for (i = 0; i < emu->max_voices; i++) {
		vp = &emu->voices[i];
		if (vp->port == port)
			update_voice(emu, vp, update);
	}
	spin_unlock_irqrestore(&emu->voice_lock, flags);
}


/*
 * Deal with a controller type event.  This includes all types of
 * control events, not just the midi controllers
 */
void
snd_emux_control(void *p, int type, struct snd_midi_channel *chan)
{
	struct snd_emux_port *port;

	port = p;
	snd_assert(port != NULL && chan != NULL, return);

	switch (type) {
	case MIDI_CTL_MSB_MAIN_VOLUME:
	case MIDI_CTL_MSB_EXPRESSION:
		snd_emux_update_channel(port, chan, SNDRV_EMUX_UPDATE_VOLUME);
		break;
		
	case MIDI_CTL_MSB_PAN:
		snd_emux_update_channel(port, chan, SNDRV_EMUX_UPDATE_PAN);
		break;

	case MIDI_CTL_SOFT_PEDAL:
#ifdef SNDRV_EMUX_USE_RAW_EFFECT
		/* FIXME: this is an emulation */
		if (chan->control[type] >= 64)
			snd_emux_send_effect(port, chan, EMUX_FX_CUTOFF, -160,
				     EMUX_FX_FLAG_ADD);
		else
			snd_emux_send_effect(port, chan, EMUX_FX_CUTOFF, 0,
				     EMUX_FX_FLAG_OFF);
#endif
		break;

	case MIDI_CTL_PITCHBEND:
		snd_emux_update_channel(port, chan, SNDRV_EMUX_UPDATE_PITCH);
		break;

	case MIDI_CTL_MSB_MODWHEEL:
	case MIDI_CTL_CHAN_PRESSURE:
		snd_emux_update_channel(port, chan,
					SNDRV_EMUX_UPDATE_FMMOD |
					SNDRV_EMUX_UPDATE_FM2FRQ2);
		break;

	}

	if (port->chset.midi_mode == SNDRV_MIDI_MODE_XG) {
		snd_emux_xg_control(port, chan, type);
	}
}


/*
 * terminate note - if free flag is true, free the terminated voice
 */
static void
terminate_note1(struct snd_emux *emu, int note, struct snd_midi_channel *chan, int free)
{
	int  i;
	struct snd_emux_voice *vp;
	unsigned long flags;

	spin_lock_irqsave(&emu->voice_lock, flags);
	for (i = 0; i < emu->max_voices; i++) {
		vp = &emu->voices[i];
		if (STATE_IS_PLAYING(vp->state) && vp->chan == chan &&
		    vp->key == note)
			terminate_voice(emu, vp, free);
	}
	spin_unlock_irqrestore(&emu->voice_lock, flags);
}


/*
 * terminate note - exported for midi emulation
 */
void
snd_emux_terminate_note(void *p, int note, struct snd_midi_channel *chan)
{
	struct snd_emux *emu;
	struct snd_emux_port *port;

	port = p;
	snd_assert(port != NULL && chan != NULL, return);

	emu = port->emu;
	snd_assert(emu != NULL, return);
	snd_assert(emu->ops.terminate != NULL, return);

	terminate_note1(emu, note, chan, 1);
}


/*
 * Terminate all the notes
 */
void
snd_emux_terminate_all(struct snd_emux *emu)
{
	int i;
	struct snd_emux_voice *vp;
	unsigned long flags;

	spin_lock_irqsave(&emu->voice_lock, flags);
	for (i = 0; i < emu->max_voices; i++) {
		vp = &emu->voices[i];
		if (STATE_IS_PLAYING(vp->state))
			terminate_voice(emu, vp, 0);
		if (vp->state == SNDRV_EMUX_ST_OFF) {
			if (emu->ops.free_voice)
				emu->ops.free_voice(vp);
			if (emu->ops.reset)
				emu->ops.reset(emu, i);
		}
		vp->time = 0;
	}
	/* initialize allocation time */
	emu->use_time = 0;
	spin_unlock_irqrestore(&emu->voice_lock, flags);
}

EXPORT_SYMBOL(snd_emux_terminate_all);

/*
 * Terminate all voices associated with the given port
 */
void
snd_emux_sounds_off_all(struct snd_emux_port *port)
{
	int i;
	struct snd_emux *emu;
	struct snd_emux_voice *vp;
	unsigned long flags;

	snd_assert(port != NULL, return);
	emu = port->emu;
	snd_assert(emu != NULL, return);
	snd_assert(emu->ops.terminate != NULL, return);

	spin_lock_irqsave(&emu->voice_lock, flags);
	for (i = 0; i < emu->max_voices; i++) {
		vp = &emu->voices[i];
		if (STATE_IS_PLAYING(vp->state) &&
		    vp->port == port)
			terminate_voice(emu, vp, 0);
		if (vp->state == SNDRV_EMUX_ST_OFF) {
			if (emu->ops.free_voice)
				emu->ops.free_voice(vp);
			if (emu->ops.reset)
				emu->ops.reset(emu, i);
		}
	}
	spin_unlock_irqrestore(&emu->voice_lock, flags);
}


/*
 * Terminate all voices that have the same exclusive class.  This
 * is mainly for drums.
 */
static void
exclusive_note_off(struct snd_emux *emu, struct snd_emux_port *port, int exclass)
{
	struct snd_emux_voice *vp;
	int  i;
	unsigned long flags;

	spin_lock_irqsave(&emu->voice_lock, flags);
	for (i = 0; i < emu->max_voices; i++) {
		vp = &emu->voices[i];
		if (STATE_IS_PLAYING(vp->state) && vp->port == port &&
		    vp->reg.exclusiveClass == exclass) {
			terminate_voice(emu, vp, 0);
		}
	}
	spin_unlock_irqrestore(&emu->voice_lock, flags);
}

/*
 * terminate a voice
 * if free flag is true, call free_voice after termination
 */
static void
terminate_voice(struct snd_emux *emu, struct snd_emux_voice *vp, int free)
{
	emu->ops.terminate(vp);
	vp->time = emu->use_time++;
	vp->chan = NULL;
	vp->port = NULL;
	vp->zone = NULL;
	vp->block = NULL;
	vp->state = SNDRV_EMUX_ST_OFF;
	if (free && emu->ops.free_voice)
		emu->ops.free_voice(vp);
}


/*
 * Modulate the voice
 */
static void
update_voice(struct snd_emux *emu, struct snd_emux_voice *vp, int update)
{
	if (!STATE_IS_PLAYING(vp->state))
		return;

	if (vp->chan == NULL || vp->port == NULL)
		return;
	if (update & SNDRV_EMUX_UPDATE_VOLUME)
		calc_volume(vp);
	if (update & SNDRV_EMUX_UPDATE_PITCH)
		calc_pitch(vp);
	if (update & SNDRV_EMUX_UPDATE_PAN) {
		if (! calc_pan(vp) && (update == SNDRV_EMUX_UPDATE_PAN))
			return;
	}
	emu->ops.update(vp, update);
}


#if 0 // not used
/* table for volume target calculation */
static unsigned short voltarget[16] = { 
	0xEAC0, 0xE0C8, 0xD740, 0xCE20, 0xC560, 0xBD08, 0xB500, 0xAD58,
	0xA5F8, 0x9EF0, 0x9830, 0x91C0, 0x8B90, 0x85A8, 0x8000, 0x7A90
};
#endif

#define LO_BYTE(v)	((v) & 0xff)
#define HI_BYTE(v)	(((v) >> 8) & 0xff)

/*
 * Sets up the voice structure by calculating some values that
 * will be needed later.
 */
static void
setup_voice(struct snd_emux_voice *vp)
{
	struct soundfont_voice_parm *parm;
	int pitch;

	/* copy the original register values */
	vp->reg = vp->zone->v;

#ifdef SNDRV_EMUX_USE_RAW_EFFECT
	snd_emux_setup_effect(vp);
#endif

	/* reset status */
	vp->apan = -1;
	vp->avol = -1;
	vp->apitch = -1;

	calc_volume(vp);
	calc_pitch(vp);
	calc_pan(vp);

	parm = &vp->reg.parm;

	/* compute filter target and correct modulation parameters */
	if (LO_BYTE(parm->modatkhld) >= 0x80 && parm->moddelay >= 0x8000) {
		parm->moddelay = 0xbfff;
		pitch = (HI_BYTE(parm->pefe) << 4) + vp->apitch;
		if (pitch > 0xffff)
			pitch = 0xffff;
		/* calculate filter target */
		vp->ftarget = parm->cutoff + LO_BYTE(parm->pefe);
		LIMITVALUE(vp->ftarget, 0, 255);
		vp->ftarget <<= 8;
	} else {
		vp->ftarget = parm->cutoff;
		vp->ftarget <<= 8;
		pitch = vp->apitch;
	}

	/* compute pitch target */
	if (pitch != 0xffff) {
		vp->ptarget = 1 << (pitch >> 12);
		if (pitch & 0x800) vp->ptarget += (vp->ptarget*0x102e)/0x2710;
		if (pitch & 0x400) vp->ptarget += (vp->ptarget*0x764)/0x2710;
		if (pitch & 0x200) vp->ptarget += (vp->ptarget*0x389)/0x2710;
		vp->ptarget += (vp->ptarget >> 1);
		if (vp->ptarget > 0xffff) vp->ptarget = 0xffff;
	} else
		vp->ptarget = 0xffff;

	if (LO_BYTE(parm->modatkhld) >= 0x80) {
		parm->modatkhld &= ~0xff;
		parm->modatkhld |= 0x7f;
	}

	/* compute volume target and correct volume parameters */
	vp->vtarget = 0;
#if 0 /* FIXME: this leads to some clicks.. */
	if (LO_BYTE(parm->volatkhld) >= 0x80 && parm->voldelay >= 0x8000) {
		parm->voldelay = 0xbfff;
		vp->vtarget = voltarget[vp->avol % 0x10] >> (vp->avol >> 4);
	}
#endif

	if (LO_BYTE(parm->volatkhld) >= 0x80) {
		parm->volatkhld &= ~0xff;
		parm->volatkhld |= 0x7f;
	}
}

/*
 * calculate pitch parameter
 */
static unsigned char pan_volumes[256] = {
0x00,0x03,0x06,0x09,0x0c,0x0f,0x12,0x14,0x17,0x1a,0x1d,0x20,0x22,0x25,0x28,0x2a,
0x2d,0x30,0x32,0x35,0x37,0x3a,0x3c,0x3f,0x41,0x44,0x46,0x49,0x4b,0x4d,0x50,0x52,
0x54,0x57,0x59,0x5b,0x5d,0x60,0x62,0x64,0x66,0x68,0x6a,0x6c,0x6f,0x71,0x73,0x75,
0x77,0x79,0x7b,0x7c,0x7e,0x80,0x82,0x84,0x86,0x88,0x89,0x8b,0x8d,0x8f,0x90,0x92,
0x94,0x96,0x97,0x99,0x9a,0x9c,0x9e,0x9f,0xa1,0xa2,0xa4,0xa5,0xa7,0xa8,0xaa,0xab,
0xad,0xae,0xaf,0xb1,0xb2,0xb3,0xb5,0xb6,0xb7,0xb9,0xba,0xbb,0xbc,0xbe,0xbf,0xc0,
0xc1,0xc2,0xc3,0xc5,0xc6,0xc7,0xc8,0xc9,0xca,0xcb,0xcc,0xcd,0xce,0xcf,0xd0,0xd1,
0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,0xd7,0xd8,0xd9,0xda,0xdb,0xdc,0xdc,0xdd,0xde,0xdf,
0xdf,0xe0,0xe1,0xe2,0xe2,0xe3,0xe4,0xe4,0xe5,0xe6,0xe6,0xe7,0xe8,0xe8,0xe9,0xe9,
0xea,0xeb,0xeb,0xec,0xec,0xed,0xed,0xee,0xee,0xef,0xef,0xf0,0xf0,0xf1,0xf1,0xf1,
0xf2,0xf2,0xf3,0xf3,0xf3,0xf4,0xf4,0xf5,0xf5,0xf5,0xf6,0xf6,0xf6,0xf7,0xf7,0xf7,
0xf7,0xf8,0xf8,0xf8,0xf9,0xf9,0xf9,0xf9,0xf9,0xfa,0xfa,0xfa,0xfa,0xfb,0xfb,0xfb,
0xfb,0xfb,0xfc,0xfc,0xfc,0xfc,0xfc,0xfc,0xfc,0xfd,0xfd,0xfd,0xfd,0xfd,0xfd,0xfd,
0xfd,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,0xfe,
0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
};

static int
calc_pan(struct snd_emux_voice *vp)
{
	struct snd_midi_channel *chan = vp->chan;
	int pan;

	/* pan & loop start (pan 8bit, MSB, 0:right, 0xff:left) */
	if (vp->reg.fixpan > 0)	/* 0-127 */
		pan = 255 - (int)vp->reg.fixpan * 2;
	else {
		pan = chan->control[MIDI_CTL_MSB_PAN] - 64;
		if (vp->reg.pan >= 0) /* 0-127 */
			pan += vp->reg.pan - 64;
		pan = 127 - (int)pan * 2;
	}
	LIMITVALUE(pan, 0, 255);

	if (vp->emu->linear_panning) {
		/* assuming linear volume */
		if (pan != vp->apan) {
			vp->apan = pan;
			if (pan == 0)
				vp->aaux = 0xff;
			else
				vp->aaux = (-pan) & 0xff;
			return 1;
		} else
			return 0;
	} else {
		/* using volume table */
		if (vp->apan != (int)pan_volumes[pan]) {
			vp->apan = pan_volumes[pan];
			vp->aaux = pan_volumes[255 - pan];
			return 1;
		}
		return 0;
	}
}


/*
 * calculate volume attenuation
 *
 * Voice volume is controlled by volume attenuation parameter.
 * So volume becomes maximum when avol is 0 (no attenuation), and
 * minimum when 255 (-96dB or silence).
 */

/* tables for volume->attenuation calculation */
static unsigned char voltab1[128] = {
   0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x63,
   0x63, 0x2b, 0x29, 0x28, 0x27, 0x26, 0x25, 0x24, 0x23, 0x22,
   0x21, 0x20, 0x1f, 0x1e, 0x1e, 0x1d, 0x1c, 0x1b, 0x1b, 0x1a,
   0x19, 0x19, 0x18, 0x17, 0x17, 0x16, 0x16, 0x15, 0x15, 0x14,
   0x14, 0x13, 0x13, 0x13, 0x12, 0x12, 0x11, 0x11, 0x11, 0x10,
   0x10, 0x10, 0x0f, 0x0f, 0x0f, 0x0e, 0x0e, 0x0e, 0x0e, 0x0d,
   0x0d, 0x0d, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0b, 0x0b, 0x0b,
   0x0b, 0x0a, 0x0a, 0x0a, 0x0a, 0x09, 0x09, 0x09, 0x09, 0x09,
   0x08, 0x08, 0x08, 0x08, 0x08, 0x07, 0x07, 0x07, 0x07, 0x06,
   0x06, 0x06, 0x06, 0x06, 0x05, 0x05, 0x05, 0x05, 0x05, 0x04,
   0x04, 0x04, 0x04, 0x04, 0x03, 0x03, 0x03, 0x03, 0x03, 0x02,
   0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01, 0x01, 0x01,
   0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

static unsigned char voltab2[128] = {
   0x32, 0x31, 0x30, 0x2f, 0x2e, 0x2d, 0x2c, 0x2b, 0x2a, 0x2a,
   0x29, 0x28, 0x27, 0x26, 0x25, 0x24, 0x24, 0x23, 0x22, 0x21,
   0x21, 0x20, 0x1f, 0x1e, 0x1e, 0x1d, 0x1c, 0x1c, 0x1b, 0x1a,
   0x1a, 0x19, 0x19, 0x18, 0x18, 0x17, 0x16, 0x16, 0x15, 0x15,
   0x14, 0x14, 0x13, 0x13, 0x13, 0x12, 0x12, 0x11, 0x11, 0x10,
   0x10, 0x10, 0x0f, 0x0f, 0x0f, 0x0e, 0x0e, 0x0e, 0x0d, 0x0d,
   0x0d, 0x0c, 0x0c, 0x0c, 0x0b, 0x0b, 0x0b, 0x0b, 0x0a, 0x0a,
   0x0a, 0x0a, 0x09, 0x09, 0x09, 0x09, 0x09, 0x08, 0x08, 0x08,
   0x08, 0x08, 0x07, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06,
   0x06, 0x06, 0x06, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05, 0x05,
   0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x03, 0x03, 0x03, 0x03,
   0x03, 0x03, 0x02, 0x02, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01,
   0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00
};

static unsigned char expressiontab[128] = {
   0x7f, 0x6c, 0x62, 0x5a, 0x54, 0x50, 0x4b, 0x48, 0x45, 0x42,
   0x40, 0x3d, 0x3b, 0x39, 0x38, 0x36, 0x34, 0x33, 0x31, 0x30,
   0x2f, 0x2d, 0x2c, 0x2b, 0x2a, 0x29, 0x28, 0x27, 0x26, 0x25,
   0x24, 0x24, 0x23, 0x22, 0x21, 0x21, 0x20, 0x1f, 0x1e, 0x1e,
   0x1d, 0x1d, 0x1c, 0x1b, 0x1b, 0x1a, 0x1a, 0x19, 0x18, 0x18,
   0x17, 0x17, 0x16, 0x16, 0x15, 0x15, 0x15, 0x14, 0x14, 0x13,
   0x13, 0x12, 0x12, 0x11, 0x11, 0x11, 0x10, 0x10, 0x0f, 0x0f,
   0x0f, 0x0e, 0x0e, 0x0e, 0x0d, 0x0d, 0x0d, 0x0c, 0x0c, 0x0c,
   0x0b, 0x0b, 0x0b, 0x0a, 0x0a, 0x0a, 0x09, 0x09, 0x09, 0x09,
   0x08, 0x08, 0x08, 0x07, 0x07, 0x07, 0x07, 0x06, 0x06, 0x06,
   0x06, 0x05, 0x05, 0x05, 0x04, 0x04, 0x04, 0x04, 0x04, 0x03,
   0x03, 0x03, 0x03, 0x02, 0x02, 0x02, 0x02, 0x01, 0x01, 0x01,
   0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

/*
 * Magic to calculate the volume (actually attenuation) from all the
 * voice and channels parameters.
 */
static int
calc_volume(struct snd_emux_voice *vp)
{
	int vol;
	int main_vol, expression_vol, master_vol;
	struct snd_midi_channel *chan = vp->chan;
	struct snd_emux_port *port = vp->port;

	expression_vol = chan->control[MIDI_CTL_MSB_EXPRESSION];
	LIMITMAX(vp->velocity, 127);
	LIMITVALUE(expression_vol, 0, 127);
	if (port->port_mode == SNDRV_EMUX_PORT_MODE_OSS_SYNTH) {
		/* 0 - 127 */
		main_vol = chan->control[MIDI_CTL_MSB_MAIN_VOLUME];
		vol = (vp->velocity * main_vol * expression_vol) / (127*127);
		vol = vol * vp->reg.amplitude / 127;

		LIMITVALUE(vol, 0, 127);

		/* calc to attenuation */
		vol = snd_sf_vol_table[vol];

	} else {
		main_vol = chan->control[MIDI_CTL_MSB_MAIN_VOLUME] * vp->reg.amplitude / 127;
		LIMITVALUE(main_vol, 0, 127);

		vol = voltab1[main_vol] + voltab2[vp->velocity];
		vol = (vol * 8) / 3;
		vol += vp->reg.attenuation;
		vol += ((0x100 - vol) * expressiontab[expression_vol])/128;
	}

	master_vol = port->chset.gs_master_volume;
	LIMITVALUE(master_vol, 0, 127);
	vol += snd_sf_vol_table[master_vol];
	vol += port->volume_atten;

#ifdef SNDRV_EMUX_USE_RAW_EFFECT
	if (chan->private) {
		struct snd_emux_effect_table *fx = chan->private;
		vol += fx->val[EMUX_FX_ATTEN];
	}
#endif

	LIMITVALUE(vol, 0, 255);
	if (vp->avol == vol)
		return 0; /* value unchanged */

	vp->avol = vol;
	if (!SF_IS_DRUM_BANK(get_bank(port, chan))
	    && LO_BYTE(vp->reg.parm.volatkhld) < 0x7d) {
		int atten;
		if (vp->velocity < 70)
			atten = 70;
		else
			atten = vp->velocity;
		vp->acutoff = (atten * vp->reg.parm.cutoff + 0xa0) >> 7;
	} else {
		vp->acutoff = vp->reg.parm.cutoff;
	}

	return 1; /* value changed */
}

/*
 * calculate pitch offset
 *
 * 0xE000 is no pitch offset at 44100Hz sample.
 * Every 4096 is one octave.
 */

static int
calc_pitch(struct snd_emux_voice *vp)
{
	struct snd_midi_channel *chan = vp->chan;
	int offset;

	/* calculate offset */
	if (vp->reg.fixkey >= 0) {
		offset = (vp->reg.fixkey - vp->reg.root) * 4096 / 12;
	} else {
		offset = (vp->note - vp->reg.root) * 4096 / 12;
	}
	offset = (offset * vp->reg.scaleTuning) / 100;
	offset += vp->reg.tune * 4096 / 1200;
	if (chan->midi_pitchbend != 0) {
		/* (128 * 8192: 1 semitone) ==> (4096: 12 semitones) */
		offset += chan->midi_pitchbend * chan->gm_rpn_pitch_bend_range / 3072;
	}

	/* tuning via RPN:
	 *   coarse = -8192 to 8192 (100 cent per 128)
	 *   fine = -8192 to 8192 (max=100cent)
	 */
	/* 4096 = 1200 cents in emu8000 parameter */
	offset += chan->gm_rpn_coarse_tuning * 4096 / (12 * 128);
	offset += chan->gm_rpn_fine_tuning / 24;

#ifdef SNDRV_EMUX_USE_RAW_EFFECT
	/* add initial pitch correction */
	if (chan->private) {
		struct snd_emux_effect_table *fx = chan->private;
		if (fx->flag[EMUX_FX_INIT_PITCH])
			offset += fx->val[EMUX_FX_INIT_PITCH];
	}
#endif

	/* 0xe000: root pitch */
	offset += 0xe000 + vp->reg.rate_offset;
	offset += vp->emu->pitch_shift;
	LIMITVALUE(offset, 0, 0xffff);
	if (offset == vp->apitch)
		return 0; /* unchanged */
	vp->apitch = offset;
	return 1; /* value changed */
}

/*
 * Get the bank number assigned to the channel
 */
static int
get_bank(struct snd_emux_port *port, struct snd_midi_channel *chan)
{
	int val;

	switch (port->chset.midi_mode) {
	case SNDRV_MIDI_MODE_XG:
		val = chan->control[MIDI_CTL_MSB_BANK];
		if (val == 127)
			return 128; /* return drum bank */
		return chan->control[MIDI_CTL_LSB_BANK];

	case SNDRV_MIDI_MODE_GS:
		if (chan->drum_channel)
			return 128;
		/* ignore LSB (bank map) */
		return chan->control[MIDI_CTL_MSB_BANK];
		
	default:
		if (chan->drum_channel)
			return 128;
		return chan->control[MIDI_CTL_MSB_BANK];
	}
}


/* Look for the zones matching with the given note and velocity.
 * The resultant zones are stored on table.
 */
static int
get_zone(struct snd_emux *emu, struct snd_emux_port *port,
	 int *notep, int vel, struct snd_midi_channel *chan,
	 struct snd_sf_zone **table)
{
	int preset, bank, def_preset, def_bank;

	bank = get_bank(port, chan);
	preset = chan->midi_program;

	if (SF_IS_DRUM_BANK(bank)) {
		def_preset = port->ctrls[EMUX_MD_DEF_DRUM];
		def_bank = bank;
	} else {
		def_preset = preset;
		def_bank = port->ctrls[EMUX_MD_DEF_BANK];
	}

	return snd_soundfont_search_zone(emu->sflist, notep, vel, preset, bank,
					 def_preset, def_bank,
					 table, SNDRV_EMUX_MAX_MULTI_VOICES);
}

/*
 */
void
snd_emux_init_voices(struct snd_emux *emu)
{
	struct snd_emux_voice *vp;
	int i;
	unsigned long flags;

	spin_lock_irqsave(&emu->voice_lock, flags);
	for (i = 0; i < emu->max_voices; i++) {
		vp = &emu->voices[i];
		vp->ch = -1; /* not used */
		vp->state = SNDRV_EMUX_ST_OFF;
		vp->chan = NULL;
		vp->port = NULL;
		vp->time = 0;
		vp->emu = emu;
		vp->hw = emu->hw;
	}
	spin_unlock_irqrestore(&emu->voice_lock, flags);
}

/*
 */
void snd_emux_lock_voice(struct snd_emux *emu, int voice)
{
	unsigned long flags;

	spin_lock_irqsave(&emu->voice_lock, flags);
	if (emu->voices[voice].state == SNDRV_EMUX_ST_OFF)
		emu->voices[voice].state = SNDRV_EMUX_ST_LOCKED;
	else
		snd_printk("invalid voice for lock %d (state = %x)\n",
			   voice, emu->voices[voice].state);
	spin_unlock_irqrestore(&emu->voice_lock, flags);
}

EXPORT_SYMBOL(snd_emux_lock_voice);

/*
 */
void snd_emux_unlock_voice(struct snd_emux *emu, int voice)
{
	unsigned long flags;

	spin_lock_irqsave(&emu->voice_lock, flags);
	if (emu->voices[voice].state == SNDRV_EMUX_ST_LOCKED)
		emu->voices[voice].state = SNDRV_EMUX_ST_OFF;
	else
		snd_printk("invalid voice for unlock %d (state = %x)\n",
			   voice, emu->voices[voice].state);
	spin_unlock_irqrestore(&emu->voice_lock, flags);
}

EXPORT_SYMBOL(snd_emux_unlock_voice);
