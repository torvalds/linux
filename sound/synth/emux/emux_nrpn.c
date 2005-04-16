/*
 *  NRPN / SYSEX callbacks for Emu8k/Emu10k1
 *
 *  Copyright (c) 1999-2000 Takashi Iwai <tiwai@suse.de>
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
 * conversion from NRPN/control parameters to Emu8000 raw parameters
 */

/* NRPN / CC -> Emu8000 parameter converter */
typedef struct {
	int control;
	int effect;
	int (*convert)(int val);
} nrpn_conv_table;

/* effect sensitivity */

#define FX_CUTOFF	0
#define FX_RESONANCE	1
#define FX_ATTACK	2
#define FX_RELEASE	3
#define FX_VIBRATE	4
#define FX_VIBDEPTH	5
#define FX_VIBDELAY	6
#define FX_NUMS		7

/*
 * convert NRPN/control values
 */

static int send_converted_effect(nrpn_conv_table *table, int num_tables,
				 snd_emux_port_t *port, snd_midi_channel_t *chan,
				 int type, int val, int mode)
{
	int i, cval;
	for (i = 0; i < num_tables; i++) {
		if (table[i].control == type) {
			cval = table[i].convert(val);
			snd_emux_send_effect(port, chan, table[i].effect,
					     cval, mode);
			return 1;
		}
	}
	return 0;
}

#define DEF_FX_CUTOFF		170
#define DEF_FX_RESONANCE	6
#define DEF_FX_ATTACK		50
#define DEF_FX_RELEASE		50
#define DEF_FX_VIBRATE		30
#define DEF_FX_VIBDEPTH		4
#define DEF_FX_VIBDELAY		1500

/* effect sensitivities for GS NRPN:
 *  adjusted for chaos 8MB soundfonts
 */
static int gs_sense[] = 
{
	DEF_FX_CUTOFF, DEF_FX_RESONANCE, DEF_FX_ATTACK, DEF_FX_RELEASE,
	DEF_FX_VIBRATE, DEF_FX_VIBDEPTH, DEF_FX_VIBDELAY
};

/* effect sensitivies for XG controls:
 * adjusted for chaos 8MB soundfonts
 */
static int xg_sense[] = 
{
	DEF_FX_CUTOFF, DEF_FX_RESONANCE, DEF_FX_ATTACK, DEF_FX_RELEASE,
	DEF_FX_VIBRATE, DEF_FX_VIBDEPTH, DEF_FX_VIBDELAY
};


/*
 * AWE32 NRPN effects
 */

static int fx_delay(int val);
static int fx_attack(int val);
static int fx_hold(int val);
static int fx_decay(int val);
static int fx_the_value(int val);
static int fx_twice_value(int val);
static int fx_conv_pitch(int val);
static int fx_conv_Q(int val);

/* function for each NRPN */		/* [range]  units */
#define fx_env1_delay	fx_delay	/* [0,5900] 4msec */
#define fx_env1_attack	fx_attack	/* [0,5940] 1msec */
#define fx_env1_hold	fx_hold		/* [0,8191] 1msec */
#define fx_env1_decay	fx_decay	/* [0,5940] 4msec */
#define fx_env1_release	fx_decay	/* [0,5940] 4msec */
#define fx_env1_sustain	fx_the_value	/* [0,127] 0.75dB */
#define fx_env1_pitch	fx_the_value	/* [-127,127] 9.375cents */
#define fx_env1_cutoff	fx_the_value	/* [-127,127] 56.25cents */

#define fx_env2_delay	fx_delay	/* [0,5900] 4msec */
#define fx_env2_attack	fx_attack	/* [0,5940] 1msec */
#define fx_env2_hold	fx_hold		/* [0,8191] 1msec */
#define fx_env2_decay	fx_decay	/* [0,5940] 4msec */
#define fx_env2_release	fx_decay	/* [0,5940] 4msec */
#define fx_env2_sustain	fx_the_value	/* [0,127] 0.75dB */

#define fx_lfo1_delay	fx_delay	/* [0,5900] 4msec */
#define fx_lfo1_freq	fx_twice_value	/* [0,127] 84mHz */
#define fx_lfo1_volume	fx_twice_value	/* [0,127] 0.1875dB */
#define fx_lfo1_pitch	fx_the_value	/* [-127,127] 9.375cents */
#define fx_lfo1_cutoff	fx_twice_value	/* [-64,63] 56.25cents */

#define fx_lfo2_delay	fx_delay	/* [0,5900] 4msec */
#define fx_lfo2_freq	fx_twice_value	/* [0,127] 84mHz */
#define fx_lfo2_pitch	fx_the_value	/* [-127,127] 9.375cents */

#define fx_init_pitch	fx_conv_pitch	/* [-8192,8192] cents */
#define fx_chorus	fx_the_value	/* [0,255] -- */
#define fx_reverb	fx_the_value	/* [0,255] -- */
#define fx_cutoff	fx_twice_value	/* [0,127] 62Hz */
#define fx_filterQ	fx_conv_Q	/* [0,127] -- */

static int fx_delay(int val)
{
	return (unsigned short)snd_sf_calc_parm_delay(val);
}

static int fx_attack(int val)
{
	return (unsigned short)snd_sf_calc_parm_attack(val);
}

static int fx_hold(int val)
{
	return (unsigned short)snd_sf_calc_parm_hold(val);
}

static int fx_decay(int val)
{
	return (unsigned short)snd_sf_calc_parm_decay(val);
}

static int fx_the_value(int val)
{
	return (unsigned short)(val & 0xff);
}

static int fx_twice_value(int val)
{
	return (unsigned short)((val * 2) & 0xff);
}

static int fx_conv_pitch(int val)
{
	return (short)(val * 4096 / 1200);
}

static int fx_conv_Q(int val)
{
	return (unsigned short)((val / 8) & 0xff);
}


static nrpn_conv_table awe_effects[] =
{
	{ 0, EMUX_FX_LFO1_DELAY,	fx_lfo1_delay},
	{ 1, EMUX_FX_LFO1_FREQ,	fx_lfo1_freq},
	{ 2, EMUX_FX_LFO2_DELAY,	fx_lfo2_delay},
	{ 3, EMUX_FX_LFO2_FREQ,	fx_lfo2_freq},

	{ 4, EMUX_FX_ENV1_DELAY,	fx_env1_delay},
	{ 5, EMUX_FX_ENV1_ATTACK,fx_env1_attack},
	{ 6, EMUX_FX_ENV1_HOLD,	fx_env1_hold},
	{ 7, EMUX_FX_ENV1_DECAY,	fx_env1_decay},
	{ 8, EMUX_FX_ENV1_SUSTAIN,	fx_env1_sustain},
	{ 9, EMUX_FX_ENV1_RELEASE,	fx_env1_release},

	{10, EMUX_FX_ENV2_DELAY,	fx_env2_delay},
	{11, EMUX_FX_ENV2_ATTACK,	fx_env2_attack},
	{12, EMUX_FX_ENV2_HOLD,	fx_env2_hold},
	{13, EMUX_FX_ENV2_DECAY,	fx_env2_decay},
	{14, EMUX_FX_ENV2_SUSTAIN,	fx_env2_sustain},
	{15, EMUX_FX_ENV2_RELEASE,	fx_env2_release},

	{16, EMUX_FX_INIT_PITCH,	fx_init_pitch},
	{17, EMUX_FX_LFO1_PITCH,	fx_lfo1_pitch},
	{18, EMUX_FX_LFO2_PITCH,	fx_lfo2_pitch},
	{19, EMUX_FX_ENV1_PITCH,	fx_env1_pitch},
	{20, EMUX_FX_LFO1_VOLUME,	fx_lfo1_volume},
	{21, EMUX_FX_CUTOFF,		fx_cutoff},
	{22, EMUX_FX_FILTERQ,	fx_filterQ},
	{23, EMUX_FX_LFO1_CUTOFF,	fx_lfo1_cutoff},
	{24, EMUX_FX_ENV1_CUTOFF,	fx_env1_cutoff},
	{25, EMUX_FX_CHORUS,		fx_chorus},
	{26, EMUX_FX_REVERB,		fx_reverb},
};


/*
 * GS(SC88) NRPN effects; still experimental
 */

/* cutoff: quarter semitone step, max=255 */
static int gs_cutoff(int val)
{
	return (val - 64) * gs_sense[FX_CUTOFF] / 50;
}

/* resonance: 0 to 15(max) */
static int gs_filterQ(int val)
{
	return (val - 64) * gs_sense[FX_RESONANCE] / 50;
}

/* attack: */
static int gs_attack(int val)
{
	return -(val - 64) * gs_sense[FX_ATTACK] / 50;
}

/* decay: */
static int gs_decay(int val)
{
	return -(val - 64) * gs_sense[FX_RELEASE] / 50;
}

/* release: */
static int gs_release(int val)
{
	return -(val - 64) * gs_sense[FX_RELEASE] / 50;
}

/* vibrato freq: 0.042Hz step, max=255 */
static int gs_vib_rate(int val)
{
	return (val - 64) * gs_sense[FX_VIBRATE] / 50;
}

/* vibrato depth: max=127, 1 octave */
static int gs_vib_depth(int val)
{
	return (val - 64) * gs_sense[FX_VIBDEPTH] / 50;
}

/* vibrato delay: -0.725msec step */
static int gs_vib_delay(int val)
{
	return -(val - 64) * gs_sense[FX_VIBDELAY] / 50;
}

static nrpn_conv_table gs_effects[] =
{
	{32, EMUX_FX_CUTOFF,	gs_cutoff},
	{33, EMUX_FX_FILTERQ,	gs_filterQ},
	{99, EMUX_FX_ENV2_ATTACK, gs_attack},
	{100, EMUX_FX_ENV2_DECAY, gs_decay},
	{102, EMUX_FX_ENV2_RELEASE, gs_release},
	{8, EMUX_FX_LFO1_FREQ, gs_vib_rate},
	{9, EMUX_FX_LFO1_VOLUME, gs_vib_depth},
	{10, EMUX_FX_LFO1_DELAY, gs_vib_delay},
};


/*
 * NRPN events
 */
void
snd_emux_nrpn(void *p, snd_midi_channel_t *chan, snd_midi_channel_set_t *chset)
{
	snd_emux_port_t *port;

	port = p;
	snd_assert(port != NULL, return);
	snd_assert(chan != NULL, return);

	if (chan->control[MIDI_CTL_NONREG_PARM_NUM_MSB] == 127 &&
	    chan->control[MIDI_CTL_NONREG_PARM_NUM_LSB] <= 26) {
		int val;
		/* Win/DOS AWE32 specific NRPNs */
		/* both MSB/LSB necessary */
		val = (chan->control[MIDI_CTL_MSB_DATA_ENTRY] << 7) |
			chan->control[MIDI_CTL_LSB_DATA_ENTRY]; 
		val -= 8192;
		send_converted_effect
			(awe_effects, ARRAY_SIZE(awe_effects),
			 port, chan, chan->control[MIDI_CTL_NONREG_PARM_NUM_LSB],
			 val, EMUX_FX_FLAG_SET);
		return;
	}

	if (port->chset.midi_mode == SNDRV_MIDI_MODE_GS &&
	    chan->control[MIDI_CTL_NONREG_PARM_NUM_MSB] == 1) {
		int val;
		/* GS specific NRPNs */
		/* only MSB is valid */
		val = chan->control[MIDI_CTL_MSB_DATA_ENTRY];
		send_converted_effect
			(gs_effects, ARRAY_SIZE(gs_effects),
			 port, chan, chan->control[MIDI_CTL_NONREG_PARM_NUM_LSB],
			 val, EMUX_FX_FLAG_ADD);
		return;
	}
}


/*
 * XG control effects; still experimental
 */

/* cutoff: quarter semitone step, max=255 */
static int xg_cutoff(int val)
{
	return (val - 64) * xg_sense[FX_CUTOFF] / 64;
}

/* resonance: 0(open) to 15(most nasal) */
static int xg_filterQ(int val)
{
	return (val - 64) * xg_sense[FX_RESONANCE] / 64;
}

/* attack: */
static int xg_attack(int val)
{
	return -(val - 64) * xg_sense[FX_ATTACK] / 64;
}

/* release: */
static int xg_release(int val)
{
	return -(val - 64) * xg_sense[FX_RELEASE] / 64;
}

static nrpn_conv_table xg_effects[] =
{
	{71, EMUX_FX_CUTOFF,	xg_cutoff},
	{74, EMUX_FX_FILTERQ,	xg_filterQ},
	{72, EMUX_FX_ENV2_RELEASE, xg_release},
	{73, EMUX_FX_ENV2_ATTACK, xg_attack},
};

int
snd_emux_xg_control(snd_emux_port_t *port, snd_midi_channel_t *chan, int param)
{
	return send_converted_effect(xg_effects, ARRAY_SIZE(xg_effects),
				     port, chan, param,
				     chan->control[param],
				     EMUX_FX_FLAG_ADD);
}

/*
 * receive sysex
 */
void
snd_emux_sysex(void *p, unsigned char *buf, int len, int parsed, snd_midi_channel_set_t *chset)
{
	snd_emux_port_t *port;
	snd_emux_t *emu;

	port = p;
	snd_assert(port != NULL, return);
	snd_assert(chset != NULL, return);
	emu = port->emu;

	switch (parsed) {
	case SNDRV_MIDI_SYSEX_GS_MASTER_VOLUME:
		snd_emux_update_port(port, SNDRV_EMUX_UPDATE_VOLUME);
		break;
	default:
		if (emu->ops.sysex)
			emu->ops.sysex(emu, buf, len, parsed, chset);
		break;
	}
}

