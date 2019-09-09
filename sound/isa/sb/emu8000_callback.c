// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  synth callback routines for the emu8000 (AWE32/64)
 *
 *  Copyright (C) 1999 Steve Ratcliffe
 *  Copyright (C) 1999-2000 Takashi Iwai <tiwai@suse.de>
 */

#include "emu8000_local.h"
#include <linux/export.h>
#include <sound/asoundef.h>

/*
 * prototypes
 */
static struct snd_emux_voice *get_voice(struct snd_emux *emu,
					struct snd_emux_port *port);
static int start_voice(struct snd_emux_voice *vp);
static void trigger_voice(struct snd_emux_voice *vp);
static void release_voice(struct snd_emux_voice *vp);
static void update_voice(struct snd_emux_voice *vp, int update);
static void reset_voice(struct snd_emux *emu, int ch);
static void terminate_voice(struct snd_emux_voice *vp);
static void sysex(struct snd_emux *emu, char *buf, int len, int parsed,
		  struct snd_midi_channel_set *chset);
#if IS_ENABLED(CONFIG_SND_SEQUENCER_OSS)
static int oss_ioctl(struct snd_emux *emu, int cmd, int p1, int p2);
#endif
static int load_fx(struct snd_emux *emu, int type, int mode,
		   const void __user *buf, long len);

static void set_pitch(struct snd_emu8000 *hw, struct snd_emux_voice *vp);
static void set_volume(struct snd_emu8000 *hw, struct snd_emux_voice *vp);
static void set_pan(struct snd_emu8000 *hw, struct snd_emux_voice *vp);
static void set_fmmod(struct snd_emu8000 *hw, struct snd_emux_voice *vp);
static void set_tremfreq(struct snd_emu8000 *hw, struct snd_emux_voice *vp);
static void set_fm2frq2(struct snd_emu8000 *hw, struct snd_emux_voice *vp);
static void set_filterQ(struct snd_emu8000 *hw, struct snd_emux_voice *vp);
static void snd_emu8000_tweak_voice(struct snd_emu8000 *emu, int ch);

/*
 * Ensure a value is between two points
 * macro evaluates its args more than once, so changed to upper-case.
 */
#define LIMITVALUE(x, a, b) do { if ((x) < (a)) (x) = (a); else if ((x) > (b)) (x) = (b); } while (0)
#define LIMITMAX(x, a) do {if ((x) > (a)) (x) = (a); } while (0)


/*
 * set up operators
 */
static const struct snd_emux_operators emu8000_ops = {
	.owner =	THIS_MODULE,
	.get_voice =	get_voice,
	.prepare =	start_voice,
	.trigger =	trigger_voice,
	.release =	release_voice,
	.update =	update_voice,
	.terminate =	terminate_voice,
	.reset =	reset_voice,
	.sample_new =	snd_emu8000_sample_new,
	.sample_free =	snd_emu8000_sample_free,
	.sample_reset = snd_emu8000_sample_reset,
	.load_fx =	load_fx,
	.sysex =	sysex,
#if IS_ENABLED(CONFIG_SND_SEQUENCER_OSS)
	.oss_ioctl =	oss_ioctl,
#endif
};

void
snd_emu8000_ops_setup(struct snd_emu8000 *hw)
{
	hw->emu->ops = emu8000_ops;
}



/*
 * Terminate a voice
 */
static void
release_voice(struct snd_emux_voice *vp)
{
	int dcysusv;
	struct snd_emu8000 *hw;

	hw = vp->hw;
	dcysusv = 0x8000 | (unsigned char)vp->reg.parm.modrelease;
	EMU8000_DCYSUS_WRITE(hw, vp->ch, dcysusv);
	dcysusv = 0x8000 | (unsigned char)vp->reg.parm.volrelease;
	EMU8000_DCYSUSV_WRITE(hw, vp->ch, dcysusv);
}


/*
 */
static void
terminate_voice(struct snd_emux_voice *vp)
{
	struct snd_emu8000 *hw; 

	hw = vp->hw;
	EMU8000_DCYSUSV_WRITE(hw, vp->ch, 0x807F);
}


/*
 */
static void
update_voice(struct snd_emux_voice *vp, int update)
{
	struct snd_emu8000 *hw;

	hw = vp->hw;
	if (update & SNDRV_EMUX_UPDATE_VOLUME)
		set_volume(hw, vp);
	if (update & SNDRV_EMUX_UPDATE_PITCH)
		set_pitch(hw, vp);
	if ((update & SNDRV_EMUX_UPDATE_PAN) &&
	    vp->port->ctrls[EMUX_MD_REALTIME_PAN])
		set_pan(hw, vp);
	if (update & SNDRV_EMUX_UPDATE_FMMOD)
		set_fmmod(hw, vp);
	if (update & SNDRV_EMUX_UPDATE_TREMFREQ)
		set_tremfreq(hw, vp);
	if (update & SNDRV_EMUX_UPDATE_FM2FRQ2)
		set_fm2frq2(hw, vp);
	if (update & SNDRV_EMUX_UPDATE_Q)
		set_filterQ(hw, vp);
}


/*
 * Find a channel (voice) within the EMU that is not in use or at least
 * less in use than other channels.  Always returns a valid pointer
 * no matter what.  If there is a real shortage of voices then one
 * will be cut. Such is life.
 *
 * The channel index (vp->ch) must be initialized in this routine.
 * In Emu8k, it is identical with the array index.
 */
static struct snd_emux_voice *
get_voice(struct snd_emux *emu, struct snd_emux_port *port)
{
	int  i;
	struct snd_emux_voice *vp;
	struct snd_emu8000 *hw;

	/* what we are looking for, in order of preference */
	enum {
		OFF=0, RELEASED, PLAYING, END
	};

	/* Keeps track of what we are finding */
	struct best {
		unsigned int  time;
		int voice;
	} best[END];
	struct best *bp;

	hw = emu->hw;

	for (i = 0; i < END; i++) {
		best[i].time = (unsigned int)(-1); /* XXX MAX_?INT really */
		best[i].voice = -1;
	}

	/*
	 * Go through them all and get a best one to use.
	 */
	for (i = 0; i < emu->max_voices; i++) {
		int state, val;

		vp = &emu->voices[i];
		state = vp->state;

		if (state == SNDRV_EMUX_ST_OFF)
			bp = best + OFF;
		else if (state == SNDRV_EMUX_ST_RELEASED ||
			 state == SNDRV_EMUX_ST_PENDING) {
			bp = best + RELEASED;
			val = (EMU8000_CVCF_READ(hw, vp->ch) >> 16) & 0xffff;
			if (! val)
				bp = best + OFF;
		}
		else if (state & SNDRV_EMUX_ST_ON)
			bp = best + PLAYING;
		else
			continue;

		/* check if sample is finished playing (non-looping only) */
		if (state != SNDRV_EMUX_ST_OFF &&
		    (vp->reg.sample_mode & SNDRV_SFNT_SAMPLE_SINGLESHOT)) {
			val = EMU8000_CCCA_READ(hw, vp->ch) & 0xffffff;
			if (val >= vp->reg.loopstart)
				bp = best + OFF;
		}

		if (vp->time < bp->time) {
			bp->time = vp->time;
			bp->voice = i;
		}
	}

	for (i = 0; i < END; i++) {
		if (best[i].voice >= 0) {
			vp = &emu->voices[best[i].voice];
			vp->ch = best[i].voice;
			return vp;
		}
	}

	/* not found */
	return NULL;
}

/*
 */
static int
start_voice(struct snd_emux_voice *vp)
{
	unsigned int temp;
	int ch;
	int addr;
	struct snd_midi_channel *chan;
	struct snd_emu8000 *hw;

	hw = vp->hw;
	ch = vp->ch;
	chan = vp->chan;

	/* channel to be silent and idle */
	EMU8000_DCYSUSV_WRITE(hw, ch, 0x0080);
	EMU8000_VTFT_WRITE(hw, ch, 0x0000FFFF);
	EMU8000_CVCF_WRITE(hw, ch, 0x0000FFFF);
	EMU8000_PTRX_WRITE(hw, ch, 0);
	EMU8000_CPF_WRITE(hw, ch, 0);

	/* set pitch offset */
	set_pitch(hw, vp);

	/* set envelope parameters */
	EMU8000_ENVVAL_WRITE(hw, ch, vp->reg.parm.moddelay);
	EMU8000_ATKHLD_WRITE(hw, ch, vp->reg.parm.modatkhld);
	EMU8000_DCYSUS_WRITE(hw, ch, vp->reg.parm.moddcysus);
	EMU8000_ENVVOL_WRITE(hw, ch, vp->reg.parm.voldelay);
	EMU8000_ATKHLDV_WRITE(hw, ch, vp->reg.parm.volatkhld);
	/* decay/sustain parameter for volume envelope is used
	   for triggerg the voice */

	/* cutoff and volume */
	set_volume(hw, vp);

	/* modulation envelope heights */
	EMU8000_PEFE_WRITE(hw, ch, vp->reg.parm.pefe);

	/* lfo1/2 delay */
	EMU8000_LFO1VAL_WRITE(hw, ch, vp->reg.parm.lfo1delay);
	EMU8000_LFO2VAL_WRITE(hw, ch, vp->reg.parm.lfo2delay);

	/* lfo1 pitch & cutoff shift */
	set_fmmod(hw, vp);
	/* lfo1 volume & freq */
	set_tremfreq(hw, vp);
	/* lfo2 pitch & freq */
	set_fm2frq2(hw, vp);
	/* pan & loop start */
	set_pan(hw, vp);

	/* chorus & loop end (chorus 8bit, MSB) */
	addr = vp->reg.loopend - 1;
	temp = vp->reg.parm.chorus;
	temp += (int)chan->control[MIDI_CTL_E3_CHORUS_DEPTH] * 9 / 10;
	LIMITMAX(temp, 255);
	temp = (temp <<24) | (unsigned int)addr;
	EMU8000_CSL_WRITE(hw, ch, temp);

	/* Q & current address (Q 4bit value, MSB) */
	addr = vp->reg.start - 1;
	temp = vp->reg.parm.filterQ;
	temp = (temp<<28) | (unsigned int)addr;
	EMU8000_CCCA_WRITE(hw, ch, temp);

	/* clear unknown registers */
	EMU8000_00A0_WRITE(hw, ch, 0);
	EMU8000_0080_WRITE(hw, ch, 0);

	/* reset volume */
	temp = vp->vtarget << 16;
	EMU8000_VTFT_WRITE(hw, ch, temp | vp->ftarget);
	EMU8000_CVCF_WRITE(hw, ch, temp | 0xff00);

	return 0;
}

/*
 * Start envelope
 */
static void
trigger_voice(struct snd_emux_voice *vp)
{
	int ch = vp->ch;
	unsigned int temp;
	struct snd_emu8000 *hw;

	hw = vp->hw;

	/* set reverb and pitch target */
	temp = vp->reg.parm.reverb;
	temp += (int)vp->chan->control[MIDI_CTL_E1_REVERB_DEPTH] * 9 / 10;
	LIMITMAX(temp, 255);
	temp = (temp << 8) | (vp->ptarget << 16) | vp->aaux;
	EMU8000_PTRX_WRITE(hw, ch, temp);
	EMU8000_CPF_WRITE(hw, ch, vp->ptarget << 16);
	EMU8000_DCYSUSV_WRITE(hw, ch, vp->reg.parm.voldcysus);
}

/*
 * reset voice parameters
 */
static void
reset_voice(struct snd_emux *emu, int ch)
{
	struct snd_emu8000 *hw;

	hw = emu->hw;
	EMU8000_DCYSUSV_WRITE(hw, ch, 0x807F);
	snd_emu8000_tweak_voice(hw, ch);
}

/*
 * Set the pitch of a possibly playing note.
 */
static void
set_pitch(struct snd_emu8000 *hw, struct snd_emux_voice *vp)
{
	EMU8000_IP_WRITE(hw, vp->ch, vp->apitch);
}

/*
 * Set the volume of a possibly already playing note
 */
static void
set_volume(struct snd_emu8000 *hw, struct snd_emux_voice *vp)
{
	int  ifatn;

	ifatn = (unsigned char)vp->acutoff;
	ifatn = (ifatn << 8);
	ifatn |= (unsigned char)vp->avol;
	EMU8000_IFATN_WRITE(hw, vp->ch, ifatn);
}

/*
 * Set pan and loop start address.
 */
static void
set_pan(struct snd_emu8000 *hw, struct snd_emux_voice *vp)
{
	unsigned int temp;

	temp = ((unsigned int)vp->apan<<24) | ((unsigned int)vp->reg.loopstart - 1);
	EMU8000_PSST_WRITE(hw, vp->ch, temp);
}

#define MOD_SENSE 18

static void
set_fmmod(struct snd_emu8000 *hw, struct snd_emux_voice *vp)
{
	unsigned short fmmod;
	short pitch;
	unsigned char cutoff;
	int modulation;

	pitch = (char)(vp->reg.parm.fmmod>>8);
	cutoff = (vp->reg.parm.fmmod & 0xff);
	modulation = vp->chan->gm_modulation + vp->chan->midi_pressure;
	pitch += (MOD_SENSE * modulation) / 1200;
	LIMITVALUE(pitch, -128, 127);
	fmmod = ((unsigned char)pitch<<8) | cutoff;
	EMU8000_FMMOD_WRITE(hw, vp->ch, fmmod);
}

/* set tremolo (lfo1) volume & frequency */
static void
set_tremfreq(struct snd_emu8000 *hw, struct snd_emux_voice *vp)
{
	EMU8000_TREMFRQ_WRITE(hw, vp->ch, vp->reg.parm.tremfrq);
}

/* set lfo2 pitch & frequency */
static void
set_fm2frq2(struct snd_emu8000 *hw, struct snd_emux_voice *vp)
{
	unsigned short fm2frq2;
	short pitch;
	unsigned char freq;
	int modulation;

	pitch = (char)(vp->reg.parm.fm2frq2>>8);
	freq = vp->reg.parm.fm2frq2 & 0xff;
	modulation = vp->chan->gm_modulation + vp->chan->midi_pressure;
	pitch += (MOD_SENSE * modulation) / 1200;
	LIMITVALUE(pitch, -128, 127);
	fm2frq2 = ((unsigned char)pitch<<8) | freq;
	EMU8000_FM2FRQ2_WRITE(hw, vp->ch, fm2frq2);
}

/* set filterQ */
static void
set_filterQ(struct snd_emu8000 *hw, struct snd_emux_voice *vp)
{
	unsigned int addr;
	addr = EMU8000_CCCA_READ(hw, vp->ch) & 0xffffff;
	addr |= (vp->reg.parm.filterQ << 28);
	EMU8000_CCCA_WRITE(hw, vp->ch, addr);
}

/*
 * set the envelope & LFO parameters to the default values
 */
static void
snd_emu8000_tweak_voice(struct snd_emu8000 *emu, int i)
{
	/* set all mod/vol envelope shape to minimum */
	EMU8000_ENVVOL_WRITE(emu, i, 0x8000);
	EMU8000_ENVVAL_WRITE(emu, i, 0x8000);
	EMU8000_DCYSUS_WRITE(emu, i, 0x7F7F);
	EMU8000_ATKHLDV_WRITE(emu, i, 0x7F7F);
	EMU8000_ATKHLD_WRITE(emu, i, 0x7F7F);
	EMU8000_PEFE_WRITE(emu, i, 0);  /* mod envelope height to zero */
	EMU8000_LFO1VAL_WRITE(emu, i, 0x8000); /* no delay for LFO1 */
	EMU8000_LFO2VAL_WRITE(emu, i, 0x8000);
	EMU8000_IP_WRITE(emu, i, 0xE000);	/* no pitch shift */
	EMU8000_IFATN_WRITE(emu, i, 0xFF00);	/* volume to minimum */
	EMU8000_FMMOD_WRITE(emu, i, 0);
	EMU8000_TREMFRQ_WRITE(emu, i, 0);
	EMU8000_FM2FRQ2_WRITE(emu, i, 0);
}

/*
 * sysex callback
 */
static void
sysex(struct snd_emux *emu, char *buf, int len, int parsed, struct snd_midi_channel_set *chset)
{
	struct snd_emu8000 *hw;

	hw = emu->hw;

	switch (parsed) {
	case SNDRV_MIDI_SYSEX_GS_CHORUS_MODE:
		hw->chorus_mode = chset->gs_chorus_mode;
		snd_emu8000_update_chorus_mode(hw);
		break;

	case SNDRV_MIDI_SYSEX_GS_REVERB_MODE:
		hw->reverb_mode = chset->gs_reverb_mode;
		snd_emu8000_update_reverb_mode(hw);
		break;
	}
}


#if IS_ENABLED(CONFIG_SND_SEQUENCER_OSS)
/*
 * OSS ioctl callback
 */
static int
oss_ioctl(struct snd_emux *emu, int cmd, int p1, int p2)
{
	struct snd_emu8000 *hw;

	hw = emu->hw;

	switch (cmd) {
	case _EMUX_OSS_REVERB_MODE:
		hw->reverb_mode = p1;
		snd_emu8000_update_reverb_mode(hw);
		break;

	case _EMUX_OSS_CHORUS_MODE:
		hw->chorus_mode = p1;
		snd_emu8000_update_chorus_mode(hw);
		break;

	case _EMUX_OSS_INITIALIZE_CHIP:
		/* snd_emu8000_init(hw); */ /*ignored*/
		break;

	case _EMUX_OSS_EQUALIZER:
		hw->bass_level = p1;
		hw->treble_level = p2;
		snd_emu8000_update_equalizer(hw);
		break;
	}
	return 0;
}
#endif


/*
 * additional patch keys
 */

#define SNDRV_EMU8000_LOAD_CHORUS_FX	0x10	/* optarg=mode */
#define SNDRV_EMU8000_LOAD_REVERB_FX	0x11	/* optarg=mode */


/*
 * callback routine
 */

static int
load_fx(struct snd_emux *emu, int type, int mode, const void __user *buf, long len)
{
	struct snd_emu8000 *hw;
	hw = emu->hw;

	/* skip header */
	buf += 16;
	len -= 16;

	switch (type) {
	case SNDRV_EMU8000_LOAD_CHORUS_FX:
		return snd_emu8000_load_chorus_fx(hw, mode, buf, len);
	case SNDRV_EMU8000_LOAD_REVERB_FX:
		return snd_emu8000_load_reverb_fx(hw, mode, buf, len);
	}
	return -EINVAL;
}

