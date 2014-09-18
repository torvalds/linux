#ifndef __SOUND_EMUX_LEGACY_H
#define __SOUND_EMUX_LEGACY_H

/*
 *  Copyright (c) 1999-2000 Takashi Iwai <tiwai@suse.de>
 *
 *  Definitions of OSS compatible headers for Emu8000 device informations
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

#include <sound/seq_oss_legacy.h>

/*
 * awe hardware controls
 */

#define _EMUX_OSS_DEBUG_MODE		0x00
#define _EMUX_OSS_REVERB_MODE		0x01
#define _EMUX_OSS_CHORUS_MODE		0x02
#define _EMUX_OSS_REMOVE_LAST_SAMPLES	0x03
#define _EMUX_OSS_INITIALIZE_CHIP	0x04
#define _EMUX_OSS_SEND_EFFECT		0x05
#define _EMUX_OSS_TERMINATE_CHANNEL	0x06
#define _EMUX_OSS_TERMINATE_ALL		0x07
#define _EMUX_OSS_INITIAL_VOLUME	0x08
#define _EMUX_OSS_INITIAL_ATTEN	_EMUX_OSS_INITIAL_VOLUME
#define _EMUX_OSS_RESET_CHANNEL		0x09
#define _EMUX_OSS_CHANNEL_MODE		0x0a
#define _EMUX_OSS_DRUM_CHANNELS		0x0b
#define _EMUX_OSS_MISC_MODE		0x0c
#define _EMUX_OSS_RELEASE_ALL		0x0d
#define _EMUX_OSS_NOTEOFF_ALL		0x0e
#define _EMUX_OSS_CHN_PRESSURE		0x0f
#define _EMUX_OSS_EQUALIZER		0x11

#define _EMUX_OSS_MODE_FLAG		0x80
#define _EMUX_OSS_COOKED_FLAG		0x40	/* not supported */
#define _EMUX_OSS_MODE_VALUE_MASK	0x3F


/*
 * mode type definitions
 */
enum {
/* 0*/	EMUX_MD_EXCLUSIVE_OFF,	/* obsolete */
/* 1*/	EMUX_MD_EXCLUSIVE_ON,	/* obsolete */
/* 2*/	EMUX_MD_VERSION,		/* read only */
/* 3*/	EMUX_MD_EXCLUSIVE_SOUND,	/* 0/1: exclusive note on (default=1) */
/* 4*/	EMUX_MD_REALTIME_PAN,	/* 0/1: do realtime pan change (default=1) */
/* 5*/	EMUX_MD_GUS_BANK,	/* bank number for GUS patches (default=0) */
/* 6*/	EMUX_MD_KEEP_EFFECT,	/* 0/1: keep effect values, (default=0) */
/* 7*/	EMUX_MD_ZERO_ATTEN,	/* attenuation of max volume (default=32) */
/* 8*/	EMUX_MD_CHN_PRIOR,	/* 0/1: set MIDI channel priority mode (default=1) */
/* 9*/	EMUX_MD_MOD_SENSE,	/* integer: modwheel sensitivity (def=18) */
/*10*/	EMUX_MD_DEF_PRESET,	/* integer: default preset number (def=0) */
/*11*/	EMUX_MD_DEF_BANK,	/* integer: default bank number (def=0) */
/*12*/	EMUX_MD_DEF_DRUM,	/* integer: default drumset number (def=0) */
/*13*/	EMUX_MD_TOGGLE_DRUM_BANK, /* 0/1: toggle drum flag with bank# (def=0) */
/*14*/	EMUX_MD_NEW_VOLUME_CALC,	/* 0/1: volume calculation mode (def=1) */
/*15*/	EMUX_MD_CHORUS_MODE,	/* integer: chorus mode (def=2) */
/*16*/	EMUX_MD_REVERB_MODE,	/* integer: chorus mode (def=4) */
/*17*/	EMUX_MD_BASS_LEVEL,	/* integer: bass level (def=5) */
/*18*/	EMUX_MD_TREBLE_LEVEL,	/* integer: treble level (def=9) */
/*19*/	EMUX_MD_DEBUG_MODE,	/* integer: debug level (def=0) */
/*20*/	EMUX_MD_PAN_EXCHANGE,	/* 0/1: exchange panning direction (def=0) */
	EMUX_MD_END,
};


/*
 * effect parameters
 */
enum {

/* modulation envelope parameters */
/* 0*/	EMUX_FX_ENV1_DELAY,	/* WORD: ENVVAL */
/* 1*/	EMUX_FX_ENV1_ATTACK,	/* BYTE: up ATKHLD */
/* 2*/	EMUX_FX_ENV1_HOLD,	/* BYTE: lw ATKHLD */
/* 3*/	EMUX_FX_ENV1_DECAY,	/* BYTE: lw DCYSUS */
/* 4*/	EMUX_FX_ENV1_RELEASE,	/* BYTE: lw DCYSUS */
/* 5*/	EMUX_FX_ENV1_SUSTAIN,	/* BYTE: up DCYSUS */
/* 6*/	EMUX_FX_ENV1_PITCH,	/* BYTE: up PEFE */
/* 7*/	EMUX_FX_ENV1_CUTOFF,	/* BYTE: lw PEFE */

/* volume envelope parameters */
/* 8*/	EMUX_FX_ENV2_DELAY,	/* WORD: ENVVOL */
/* 9*/	EMUX_FX_ENV2_ATTACK,	/* BYTE: up ATKHLDV */
/*10*/	EMUX_FX_ENV2_HOLD,	/* BYTE: lw ATKHLDV */
/*11*/	EMUX_FX_ENV2_DECAY,	/* BYTE: lw DCYSUSV */
/*12*/	EMUX_FX_ENV2_RELEASE,	/* BYTE: lw DCYSUSV */
/*13*/	EMUX_FX_ENV2_SUSTAIN,	/* BYTE: up DCYSUSV */
	
/* LFO1 (tremolo & vibrato) parameters */
/*14*/	EMUX_FX_LFO1_DELAY,	/* WORD: LFO1VAL */
/*15*/	EMUX_FX_LFO1_FREQ,	/* BYTE: lo TREMFRQ */
/*16*/	EMUX_FX_LFO1_VOLUME,	/* BYTE: up TREMFRQ */
/*17*/	EMUX_FX_LFO1_PITCH,	/* BYTE: up FMMOD */
/*18*/	EMUX_FX_LFO1_CUTOFF,	/* BYTE: lo FMMOD */

/* LFO2 (vibrato) parameters */
/*19*/	EMUX_FX_LFO2_DELAY,	/* WORD: LFO2VAL */
/*20*/	EMUX_FX_LFO2_FREQ,	/* BYTE: lo FM2FRQ2 */
/*21*/	EMUX_FX_LFO2_PITCH,	/* BYTE: up FM2FRQ2 */

/* Other overall effect parameters */
/*22*/	EMUX_FX_INIT_PITCH,	/* SHORT: pitch offset */
/*23*/	EMUX_FX_CHORUS,		/* BYTE: chorus effects send (0-255) */
/*24*/	EMUX_FX_REVERB,		/* BYTE: reverb effects send (0-255) */
/*25*/	EMUX_FX_CUTOFF,		/* BYTE: up IFATN */
/*26*/	EMUX_FX_FILTERQ,		/* BYTE: up CCCA */

/* Sample / loop offset changes */
/*27*/	EMUX_FX_SAMPLE_START,	/* SHORT: offset */
/*28*/	EMUX_FX_LOOP_START,	/* SHORT: offset */
/*29*/	EMUX_FX_LOOP_END,	/* SHORT: offset */
/*30*/	EMUX_FX_COARSE_SAMPLE_START,	/* SHORT: upper word offset */
/*31*/	EMUX_FX_COARSE_LOOP_START,	/* SHORT: upper word offset */
/*32*/	EMUX_FX_COARSE_LOOP_END,		/* SHORT: upper word offset */
/*33*/	EMUX_FX_ATTEN,		/* BYTE: lo IFATN */

	EMUX_FX_END,
};
/* number of effects */
#define EMUX_NUM_EFFECTS  EMUX_FX_END

/* effect flag values */
#define EMUX_FX_FLAG_OFF	0
#define EMUX_FX_FLAG_SET	1
#define EMUX_FX_FLAG_ADD	2


#endif /* __SOUND_EMUX_LEGACY_H */
