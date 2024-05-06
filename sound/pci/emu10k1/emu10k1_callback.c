// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  synth callback routines for Emu10k1
 *
 *  Copyright (C) 2000 Takashi Iwai <tiwai@suse.de>
 */

#include <linux/export.h>
#include "emu10k1_synth_local.h"
#include <sound/asoundef.h>

/* voice status */
enum {
	V_FREE=0, V_OFF, V_RELEASED, V_PLAYING, V_END
};

/* Keeps track of what we are finding */
struct best_voice {
	unsigned int time;
	int voice;
};

/*
 * prototypes
 */
static void lookup_voices(struct snd_emux *emux, struct snd_emu10k1 *hw,
			  struct best_voice *best, int active_only);
static struct snd_emux_voice *get_voice(struct snd_emux *emux,
					struct snd_emux_port *port);
static int start_voice(struct snd_emux_voice *vp);
static void trigger_voice(struct snd_emux_voice *vp);
static void release_voice(struct snd_emux_voice *vp);
static void update_voice(struct snd_emux_voice *vp, int update);
static void terminate_voice(struct snd_emux_voice *vp);
static void free_voice(struct snd_emux_voice *vp);
static u32 make_fmmod(struct snd_emux_voice *vp);
static u32 make_fm2frq2(struct snd_emux_voice *vp);
static int get_pitch_shift(struct snd_emux *emu);

/*
 * Ensure a value is between two points
 * macro evaluates its args more than once, so changed to upper-case.
 */
#define LIMITVALUE(x, a, b) do { if ((x) < (a)) (x) = (a); else if ((x) > (b)) (x) = (b); } while (0)
#define LIMITMAX(x, a) do {if ((x) > (a)) (x) = (a); } while (0)


/*
 * set up operators
 */
static const struct snd_emux_operators emu10k1_ops = {
	.owner =	THIS_MODULE,
	.get_voice =	get_voice,
	.prepare =	start_voice,
	.trigger =	trigger_voice,
	.release =	release_voice,
	.update =	update_voice,
	.terminate =	terminate_voice,
	.free_voice =	free_voice,
	.sample_new =	snd_emu10k1_sample_new,
	.sample_free =	snd_emu10k1_sample_free,
	.get_pitch_shift = get_pitch_shift,
};

void
snd_emu10k1_ops_setup(struct snd_emux *emux)
{
	emux->ops = emu10k1_ops;
}


/*
 * get more voice for pcm
 *
 * terminate most inactive voice and give it as a pcm voice.
 *
 * voice_lock is already held.
 */
int
snd_emu10k1_synth_get_voice(struct snd_emu10k1 *hw)
{
	struct snd_emux *emu;
	struct snd_emux_voice *vp;
	struct best_voice best[V_END];
	int i;

	emu = hw->synth;

	lookup_voices(emu, hw, best, 1); /* no OFF voices */
	for (i = 0; i < V_END; i++) {
		if (best[i].voice >= 0) {
			int ch;
			vp = &emu->voices[best[i].voice];
			ch = vp->ch;
			if (ch < 0) {
				/*
				dev_warn(emu->card->dev,
				       "synth_get_voice: ch < 0 (%d) ??", i);
				*/
				continue;
			}
			vp->emu->num_voices--;
			vp->ch = -1;
			vp->state = SNDRV_EMUX_ST_OFF;
			return ch;
		}
	}

	/* not found */
	return -ENOMEM;
}


/*
 * turn off the voice (not terminated)
 */
static void
release_voice(struct snd_emux_voice *vp)
{
	struct snd_emu10k1 *hw;
	
	hw = vp->hw;
	snd_emu10k1_ptr_write_multiple(hw, vp->ch,
		DCYSUSM, (unsigned char)vp->reg.parm.modrelease | DCYSUSM_PHASE1_MASK,
		DCYSUSV, (unsigned char)vp->reg.parm.volrelease | DCYSUSV_PHASE1_MASK | DCYSUSV_CHANNELENABLE_MASK,
		REGLIST_END);
}


/*
 * terminate the voice
 */
static void
terminate_voice(struct snd_emux_voice *vp)
{
	struct snd_emu10k1 *hw;
	
	if (snd_BUG_ON(!vp))
		return;
	hw = vp->hw;
	snd_emu10k1_ptr_write_multiple(hw, vp->ch,
		DCYSUSV, 0,
		VTFT, VTFT_FILTERTARGET_MASK,
		CVCF, CVCF_CURRENTFILTER_MASK,
		PTRX, 0,
		CPF, 0,
		REGLIST_END);
	if (vp->block) {
		struct snd_emu10k1_memblk *emem;
		emem = (struct snd_emu10k1_memblk *)vp->block;
		if (emem->map_locked > 0)
			emem->map_locked--;
	}
}

/*
 * release the voice to system
 */
static void
free_voice(struct snd_emux_voice *vp)
{
	struct snd_emu10k1 *hw;
	
	hw = vp->hw;
	/* FIXME: emu10k1_synth is broken. */
	/* This can get called with hw == 0 */
	/* Problem apparent on plug, unplug then plug */
	/* on the Audigy 2 ZS Notebook. */
	if (hw && (vp->ch >= 0)) {
		snd_emu10k1_voice_free(hw, &hw->voices[vp->ch]);
		vp->emu->num_voices--;
		vp->ch = -1;
	}
}


/*
 * update registers
 */
static void
update_voice(struct snd_emux_voice *vp, int update)
{
	struct snd_emu10k1 *hw;
	
	hw = vp->hw;
	if (update & SNDRV_EMUX_UPDATE_VOLUME)
		snd_emu10k1_ptr_write(hw, IFATN_ATTENUATION, vp->ch, vp->avol);
	if (update & SNDRV_EMUX_UPDATE_PITCH)
		snd_emu10k1_ptr_write(hw, IP, vp->ch, vp->apitch);
	if (update & SNDRV_EMUX_UPDATE_PAN) {
		snd_emu10k1_ptr_write(hw, PTRX_FXSENDAMOUNT_A, vp->ch, vp->apan);
		snd_emu10k1_ptr_write(hw, PTRX_FXSENDAMOUNT_B, vp->ch, vp->aaux);
	}
	if (update & SNDRV_EMUX_UPDATE_FMMOD)
		snd_emu10k1_ptr_write(hw, FMMOD, vp->ch, make_fmmod(vp));
	if (update & SNDRV_EMUX_UPDATE_TREMFREQ)
		snd_emu10k1_ptr_write(hw, TREMFRQ, vp->ch, vp->reg.parm.tremfrq);
	if (update & SNDRV_EMUX_UPDATE_FM2FRQ2)
		snd_emu10k1_ptr_write(hw, FM2FRQ2, vp->ch, make_fm2frq2(vp));
	if (update & SNDRV_EMUX_UPDATE_Q)
		snd_emu10k1_ptr_write(hw, CCCA_RESONANCE, vp->ch, vp->reg.parm.filterQ);
}


/*
 * look up voice table - get the best voice in order of preference
 */
/* spinlock held! */
static void
lookup_voices(struct snd_emux *emu, struct snd_emu10k1 *hw,
	      struct best_voice *best, int active_only)
{
	struct snd_emux_voice *vp;
	struct best_voice *bp;
	int  i;

	for (i = 0; i < V_END; i++) {
		best[i].time = (unsigned int)-1; /* XXX MAX_?INT really */
		best[i].voice = -1;
	}

	/*
	 * Go through them all and get a best one to use.
	 * NOTE: could also look at volume and pick the quietest one.
	 */
	for (i = 0; i < emu->max_voices; i++) {
		int state, val;

		vp = &emu->voices[i];
		state = vp->state;
		if (state == SNDRV_EMUX_ST_OFF) {
			if (vp->ch < 0) {
				if (active_only)
					continue;
				bp = best + V_FREE;
			} else
				bp = best + V_OFF;
		}
		else if (state == SNDRV_EMUX_ST_RELEASED ||
			 state == SNDRV_EMUX_ST_PENDING) {
			bp = best + V_RELEASED;
#if 1
			val = snd_emu10k1_ptr_read(hw, CVCF_CURRENTVOL, vp->ch);
			if (! val)
				bp = best + V_OFF;
#endif
		}
		else if (state == SNDRV_EMUX_ST_STANDBY)
			continue;
		else if (state & SNDRV_EMUX_ST_ON)
			bp = best + V_PLAYING;
		else
			continue;

		/* check if sample is finished playing (non-looping only) */
		if (bp != best + V_OFF && bp != best + V_FREE &&
		    (vp->reg.sample_mode & SNDRV_SFNT_SAMPLE_SINGLESHOT)) {
			val = snd_emu10k1_ptr_read(hw, CCCA_CURRADDR, vp->ch);
			if (val >= vp->reg.loopstart)
				bp = best + V_OFF;
		}

		if (vp->time < bp->time) {
			bp->time = vp->time;
			bp->voice = i;
		}
	}
}

/*
 * get an empty voice
 *
 * emu->voice_lock is already held.
 */
static struct snd_emux_voice *
get_voice(struct snd_emux *emu, struct snd_emux_port *port)
{
	struct snd_emu10k1 *hw;
	struct snd_emux_voice *vp;
	struct best_voice best[V_END];
	int i;

	hw = emu->hw;

	lookup_voices(emu, hw, best, 0);
	for (i = 0; i < V_END; i++) {
		if (best[i].voice >= 0) {
			vp = &emu->voices[best[i].voice];
			if (vp->ch < 0) {
				/* allocate a voice */
				struct snd_emu10k1_voice *hwvoice;
				if (snd_emu10k1_voice_alloc(hw, EMU10K1_SYNTH, 1, 1, NULL, &hwvoice) < 0)
					continue;
				vp->ch = hwvoice->number;
				emu->num_voices++;
			}
			return vp;
		}
	}

	/* not found */
	return NULL;
}

/*
 * prepare envelopes and LFOs
 */
static int
start_voice(struct snd_emux_voice *vp)
{
	unsigned int temp;
	int ch;
	u32 psst, dsl, map, ccca, vtarget;
	unsigned int addr, mapped_offset;
	struct snd_midi_channel *chan;
	struct snd_emu10k1 *hw;
	struct snd_emu10k1_memblk *emem;
	
	hw = vp->hw;
	ch = vp->ch;
	if (snd_BUG_ON(ch < 0))
		return -EINVAL;
	chan = vp->chan;

	emem = (struct snd_emu10k1_memblk *)vp->block;
	if (emem == NULL)
		return -EINVAL;
	emem->map_locked++;
	if (snd_emu10k1_memblk_map(hw, emem) < 0) {
		/* dev_err(hw->card->devK, "emu: cannot map!\n"); */
		return -ENOMEM;
	}
	mapped_offset = snd_emu10k1_memblk_offset(emem) >> 1;
	vp->reg.start += mapped_offset;
	vp->reg.end += mapped_offset;
	vp->reg.loopstart += mapped_offset;
	vp->reg.loopend += mapped_offset;

	/* set channel routing */
	/* A = left(0), B = right(1), C = reverb(c), D = chorus(d) */
	if (hw->audigy) {
		temp = FXBUS_MIDI_LEFT | (FXBUS_MIDI_RIGHT << 8) | 
			(FXBUS_MIDI_REVERB << 16) | (FXBUS_MIDI_CHORUS << 24);
		snd_emu10k1_ptr_write(hw, A_FXRT1, ch, temp);
	} else {
		temp = (FXBUS_MIDI_LEFT << 16) | (FXBUS_MIDI_RIGHT << 20) | 
			(FXBUS_MIDI_REVERB << 24) | (FXBUS_MIDI_CHORUS << 28);
		snd_emu10k1_ptr_write(hw, FXRT, ch, temp);
	}

	temp = vp->reg.parm.reverb;
	temp += (int)vp->chan->control[MIDI_CTL_E1_REVERB_DEPTH] * 9 / 10;
	LIMITMAX(temp, 255);
	addr = vp->reg.loopstart;
	psst = (temp << 24) | addr;

	addr = vp->reg.loopend;
	temp = vp->reg.parm.chorus;
	temp += (int)chan->control[MIDI_CTL_E3_CHORUS_DEPTH] * 9 / 10;
	LIMITMAX(temp, 255);
	dsl = (temp << 24) | addr;

	map = (hw->silent_page.addr << hw->address_mode) | (hw->address_mode ? MAP_PTI_MASK1 : MAP_PTI_MASK0);

	addr = vp->reg.start;
	temp = vp->reg.parm.filterQ;
	ccca = (temp << 28) | addr;
	if (vp->apitch < 0xe400)
		ccca |= CCCA_INTERPROM_0;
	else {
		unsigned int shift = (vp->apitch - 0xe000) >> 10;
		ccca |= shift << 25;
	}
	if (vp->reg.sample_mode & SNDRV_SFNT_SAMPLE_8BITS)
		ccca |= CCCA_8BITSELECT;

	vtarget = (unsigned int)vp->vtarget << 16;

	snd_emu10k1_ptr_write_multiple(hw, ch,
		/* channel to be silent and idle */
		DCYSUSV, 0,
		VTFT, VTFT_FILTERTARGET_MASK,
		CVCF, CVCF_CURRENTFILTER_MASK,
		PTRX, 0,
		CPF, 0,

		/* set pitch offset */
		IP, vp->apitch,

		/* set envelope parameters */
		ENVVAL, vp->reg.parm.moddelay,
		ATKHLDM, vp->reg.parm.modatkhld,
		DCYSUSM, vp->reg.parm.moddcysus,
		ENVVOL, vp->reg.parm.voldelay,
		ATKHLDV, vp->reg.parm.volatkhld,
		/* decay/sustain parameter for volume envelope is used
		   for triggerg the voice */

		/* cutoff and volume */
		IFATN, (unsigned int)vp->acutoff << 8 | (unsigned char)vp->avol,

		/* modulation envelope heights */
		PEFE, vp->reg.parm.pefe,

		/* lfo1/2 delay */
		LFOVAL1, vp->reg.parm.lfo1delay,
		LFOVAL2, vp->reg.parm.lfo2delay,

		/* lfo1 pitch & cutoff shift */
		FMMOD, make_fmmod(vp),
		/* lfo1 volume & freq */
		TREMFRQ, vp->reg.parm.tremfrq,
		/* lfo2 pitch & freq */
		FM2FRQ2, make_fm2frq2(vp),

		/* reverb and loop start (reverb 8bit, MSB) */
		PSST, psst,

		/* chorus & loop end (chorus 8bit, MSB) */
		DSL, dsl,

		/* clear filter delay memory */
		Z1, 0,
		Z2, 0,

		/* invalidate maps */
		MAPA, map,
		MAPB, map,

		/* Q & current address (Q 4bit value, MSB) */
		CCCA, ccca,

		/* reset volume */
		VTFT, vtarget | vp->ftarget,
		CVCF, vtarget | CVCF_CURRENTFILTER_MASK,

		REGLIST_END);

	hw->voices[ch].dirty = 1;
	return 0;
}

/*
 * Start envelope
 */
static void
trigger_voice(struct snd_emux_voice *vp)
{
	unsigned int ptarget;
	struct snd_emu10k1 *hw;
	struct snd_emu10k1_memblk *emem;
	
	hw = vp->hw;

	emem = (struct snd_emu10k1_memblk *)vp->block;
	if (! emem || emem->mapped_page < 0)
		return; /* not mapped */

#if 0
	ptarget = (unsigned int)vp->ptarget << 16;
#else
	ptarget = IP_TO_CP(vp->apitch);
#endif
	snd_emu10k1_ptr_write_multiple(hw, vp->ch,
		/* set pitch target and pan (volume) */
		PTRX, ptarget | (vp->apan << 8) | vp->aaux,

		/* current pitch and fractional address */
		CPF, ptarget,

		/* enable envelope engine */
		DCYSUSV, vp->reg.parm.voldcysus | DCYSUSV_CHANNELENABLE_MASK,

		REGLIST_END);
}

#define MOD_SENSE 18

/* calculate lfo1 modulation height and cutoff register */
static u32
make_fmmod(struct snd_emux_voice *vp)
{
	short pitch;
	unsigned char cutoff;
	int modulation;

	pitch = (char)(vp->reg.parm.fmmod>>8);
	cutoff = (vp->reg.parm.fmmod & 0xff);
	modulation = vp->chan->gm_modulation + vp->chan->midi_pressure;
	pitch += (MOD_SENSE * modulation) / 1200;
	LIMITVALUE(pitch, -128, 127);
	return ((unsigned char)pitch << 8) | cutoff;
}

/* calculate set lfo2 pitch & frequency register */
static u32
make_fm2frq2(struct snd_emux_voice *vp)
{
	short pitch;
	unsigned char freq;
	int modulation;

	pitch = (char)(vp->reg.parm.fm2frq2>>8);
	freq = vp->reg.parm.fm2frq2 & 0xff;
	modulation = vp->chan->gm_modulation + vp->chan->midi_pressure;
	pitch += (MOD_SENSE * modulation) / 1200;
	LIMITVALUE(pitch, -128, 127);
	return ((unsigned char)pitch << 8) | freq;
}

static int get_pitch_shift(struct snd_emux *emu)
{
	struct snd_emu10k1 *hw = emu->hw;

	return (hw->card_capabilities->emu_model &&
			hw->emu1010.word_clock == 44100) ? 0 : -501;
}
