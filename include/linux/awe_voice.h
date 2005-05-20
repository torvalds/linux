/*
 * sound/awe_voice.h
 *
 * Voice information definitions for the low level driver for the 
 * AWE32/SB32/AWE64 wave table synth.
 *   version 0.4.4; Jan. 4, 2000
 *
 * Copyright (C) 1996-2000 Takashi Iwai
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef AWE_VOICE_H
#define AWE_VOICE_H

#ifndef SAMPLE_TYPE_AWE32
#define SAMPLE_TYPE_AWE32	0x20
#endif

#define _LINUX_PATCHKEY_H_INDIRECT
#include <linux/patchkey.h>
#undef _LINUX_PATCHKEY_H_INDIRECT

/*----------------------------------------------------------------
 * patch information record
 *----------------------------------------------------------------*/

/* patch interface header: 16 bytes */
typedef struct awe_patch_info {
	short key;			/* use AWE_PATCH here */
#define AWE_PATCH	_PATCHKEY(0x07)

	short device_no;		/* synthesizer number */
	unsigned short sf_id;		/* file id (should be zero) */
	short optarg;			/* optional argument */
	int len;			/* data length (without this header) */

	short type;			/* patch operation type */
#define AWE_LOAD_INFO		0	/* awe_voice_rec */
#define AWE_LOAD_DATA		1	/* awe_sample_info */
#define AWE_OPEN_PATCH		2	/* awe_open_parm */
#define AWE_CLOSE_PATCH		3	/* none */
#define AWE_UNLOAD_PATCH	4	/* none */
#define AWE_REPLACE_DATA	5	/* awe_sample_info (optarg=#channels)*/
#define AWE_MAP_PRESET		6	/* awe_voice_map */
/*#define AWE_PROBE_INFO	7*/	/* awe_voice_map (pat only) */
#define AWE_PROBE_DATA		8	/* optarg=sample */
#define AWE_REMOVE_INFO		9	/* optarg=(bank<<8)|instr */
#define AWE_LOAD_CHORUS_FX	0x10	/* awe_chorus_fx_rec (optarg=mode) */
#define AWE_LOAD_REVERB_FX	0x11	/* awe_reverb_fx_rec (optarg=mode) */

	short reserved;			/* word alignment data */

	/* the actual patch data begins after this */
#if defined(AWE_COMPAT_030) && AWE_COMPAT_030
	char data[0];
#endif
} awe_patch_info;

/*#define AWE_PATCH_INFO_SIZE	16*/
#define AWE_PATCH_INFO_SIZE	sizeof(awe_patch_info)


/*----------------------------------------------------------------
 * open patch
 *----------------------------------------------------------------*/

#define AWE_PATCH_NAME_LEN	32

typedef struct _awe_open_parm {
	unsigned short type;		/* sample type */
#define AWE_PAT_TYPE_MISC	0
#define AWE_PAT_TYPE_GM		1
#define AWE_PAT_TYPE_GS		2
#define AWE_PAT_TYPE_MT32	3
#define AWE_PAT_TYPE_XG		4
#define AWE_PAT_TYPE_SFX	5
#define AWE_PAT_TYPE_GUS	6
#define AWE_PAT_TYPE_MAP	7

#define AWE_PAT_LOCKED		0x100	/* lock the samples */
#define AWE_PAT_SHARED		0x200	/* sample is shared */

	short reserved;
	char name[AWE_PATCH_NAME_LEN];
} awe_open_parm;

/*#define AWE_OPEN_PARM_SIZE	28*/
#define AWE_OPEN_PARM_SIZE	sizeof(awe_open_parm)


/*----------------------------------------------------------------
 * raw voice information record
 *----------------------------------------------------------------*/

/* wave table envelope & effect parameters to control EMU8000 */
typedef struct _awe_voice_parm {
	unsigned short moddelay;	/* modulation delay (0x8000) */
	unsigned short modatkhld;	/* modulation attack & hold time (0x7f7f) */
	unsigned short moddcysus;	/* modulation decay & sustain (0x7f7f) */
	unsigned short modrelease;	/* modulation release time (0x807f) */
	short modkeyhold, modkeydecay;	/* envelope change per key (not used) */
	unsigned short voldelay;	/* volume delay (0x8000) */
	unsigned short volatkhld;	/* volume attack & hold time (0x7f7f) */
	unsigned short voldcysus;	/* volume decay & sustain (0x7f7f) */
	unsigned short volrelease;	/* volume release time (0x807f) */
	short volkeyhold, volkeydecay;	/* envelope change per key (not used) */
	unsigned short lfo1delay;	/* LFO1 delay (0x8000) */
	unsigned short lfo2delay;	/* LFO2 delay (0x8000) */
	unsigned short pefe;		/* modulation pitch & cutoff (0x0000) */
	unsigned short fmmod;		/* LFO1 pitch & cutoff (0x0000) */
	unsigned short tremfrq;		/* LFO1 volume & freq (0x0000) */
	unsigned short fm2frq2;		/* LFO2 pitch & freq (0x0000) */
	unsigned char cutoff;		/* initial cutoff (0xff) */
	unsigned char filterQ;		/* initial filter Q [0-15] (0x0) */
	unsigned char chorus;		/* chorus send (0x00) */
	unsigned char reverb;		/* reverb send (0x00) */
	unsigned short reserved[4];	/* not used */
} awe_voice_parm;

typedef struct _awe_voice_parm_block {
	unsigned short moddelay;	/* modulation delay (0x8000) */
	unsigned char modatk, modhld;
	unsigned char moddcy, modsus;
	unsigned char modrel, moddummy;
	short modkeyhold, modkeydecay;	/* envelope change per key (not used) */
	unsigned short voldelay;	/* volume delay (0x8000) */
	unsigned char volatk, volhld;
	unsigned char voldcy, volsus;
	unsigned char volrel, voldummy;
	short volkeyhold, volkeydecay;	/* envelope change per key (not used) */
	unsigned short lfo1delay;	/* LFO1 delay (0x8000) */
	unsigned short lfo2delay;	/* LFO2 delay (0x8000) */
	unsigned char env1fc, env1pit;
	unsigned char lfo1fc, lfo1pit;
	unsigned char lfo1freq, lfo1vol;
	unsigned char lfo2freq, lfo2pit;
	unsigned char cutoff;		/* initial cutoff (0xff) */
	unsigned char filterQ;		/* initial filter Q [0-15] (0x0) */
	unsigned char chorus;		/* chorus send (0x00) */
	unsigned char reverb;		/* reverb send (0x00) */
	unsigned short reserved[4];	/* not used */
} awe_voice_parm_block;

#define AWE_VOICE_PARM_SIZE	48


/* wave table parameters: 92 bytes */
typedef struct _awe_voice_info {
	unsigned short sf_id;		/* file id (should be zero) */
	unsigned short sample;		/* sample id */
	int start, end;			/* sample offset correction */
	int loopstart, loopend;		/* loop offset correction */
	short rate_offset;		/* sample rate pitch offset */
	unsigned short mode;		/* sample mode */
#define AWE_MODE_ROMSOUND		0x8000
#define AWE_MODE_STEREO			1
#define AWE_MODE_LOOPING		2
#define AWE_MODE_NORELEASE		4	/* obsolete */
#define AWE_MODE_INIT_PARM		8

	short root;			/* midi root key */
	short tune;			/* pitch tuning (in cents) */
	signed char low, high;		/* key note range */
	signed char vellow, velhigh;	/* velocity range */
	signed char fixkey, fixvel;	/* fixed key, velocity */
	signed char pan, fixpan;	/* panning, fixed panning */
	short exclusiveClass;		/* exclusive class (0 = none) */
	unsigned char amplitude;	/* sample volume (127 max) */
	unsigned char attenuation;	/* attenuation (0.375dB) */
	short scaleTuning;		/* pitch scale tuning(%), normally 100 */
	awe_voice_parm parm;		/* voice envelope parameters */
	short index;			/* internal index (set by driver) */
} awe_voice_info;

/*#define AWE_VOICE_INFO_SIZE	92*/
#define AWE_VOICE_INFO_SIZE	sizeof(awe_voice_info)

/*----------------------------------------------------------------*/

/* The info entry of awe_voice_rec is changed from 0 to 1
 * for some compilers refusing zero size array.
 * Due to this change, sizeof(awe_voice_rec) becomes different
 * from older versions.
 * Use AWE_VOICE_REC_SIZE instead.
 */

/* instrument info header: 4 bytes */
typedef struct _awe_voice_rec_hdr {
	unsigned char bank;		/* midi bank number */
	unsigned char instr;		/* midi preset number */
	char nvoices;			/* number of voices */
	char write_mode;		/* write mode; normally 0 */
#define AWE_WR_APPEND		0	/* append anyway */
#define AWE_WR_EXCLUSIVE	1	/* skip if already exists */
#define AWE_WR_REPLACE		2	/* replace if already exists */
} awe_voice_rec_hdr;

/*#define AWE_VOICE_REC_SIZE	4*/
#define AWE_VOICE_REC_SIZE	sizeof(awe_voice_rec_hdr)

/* the standard patch structure for one sample */
typedef struct _awe_voice_rec_patch {
	awe_patch_info		patch;
	awe_voice_rec_hdr	hdr;
	awe_voice_info		info;
} awe_voice_rec_patch;


/* obsolete data type */
#if defined(AWE_COMPAT_030) && AWE_COMPAT_030
#define AWE_INFOARRAY_SIZE	0
#else
#define AWE_INFOARRAY_SIZE	1
#endif

typedef struct _awe_voice_rec {
	unsigned char bank;		/* midi bank number */
	unsigned char instr;		/* midi preset number */
	short nvoices;			/* number of voices */
	/* voice information follows here */
	awe_voice_info info[AWE_INFOARRAY_SIZE];
} awe_voice_rec;


/*----------------------------------------------------------------
 * sample wave information
 *----------------------------------------------------------------*/

/* wave table sample header: 32 bytes */
typedef struct awe_sample_info {
	unsigned short sf_id;		/* file id (should be zero) */
	unsigned short sample;		/* sample id */
	int start, end;			/* start & end offset */
	int loopstart, loopend;		/* loop start & end offset */
	int size;			/* size (0 = ROM) */
	short checksum_flag;		/* use check sum = 1 */
	unsigned short mode_flags;	/* mode flags */
#define AWE_SAMPLE_8BITS	1	/* wave data is 8bits */
#define AWE_SAMPLE_UNSIGNED	2	/* wave data is unsigned */
#define AWE_SAMPLE_NO_BLANK	4	/* no blank loop is attached */
#define AWE_SAMPLE_SINGLESHOT	8	/* single-shot w/o loop */
#define AWE_SAMPLE_BIDIR_LOOP	16	/* bidirectional looping */
#define AWE_SAMPLE_STEREO_LEFT	32	/* stereo left sound */
#define AWE_SAMPLE_STEREO_RIGHT	64	/* stereo right sound */
#define AWE_SAMPLE_REVERSE_LOOP 128	/* reverse looping */
	unsigned int checksum;		/* check sum */
#if defined(AWE_COMPAT_030) && AWE_COMPAT_030
	unsigned short data[0];		/* sample data follows here */
#endif
} awe_sample_info;

/*#define AWE_SAMPLE_INFO_SIZE	32*/
#define AWE_SAMPLE_INFO_SIZE	sizeof(awe_sample_info)


/*----------------------------------------------------------------
 * voice preset mapping
 *----------------------------------------------------------------*/

typedef struct awe_voice_map {
	int map_bank, map_instr, map_key;	/* key = -1 means all keys */
	int src_bank, src_instr, src_key;
} awe_voice_map;

#define AWE_VOICE_MAP_SIZE	sizeof(awe_voice_map)


/*----------------------------------------------------------------
 * awe hardware controls
 *----------------------------------------------------------------*/

#define _AWE_DEBUG_MODE			0x00
#define _AWE_REVERB_MODE		0x01
#define _AWE_CHORUS_MODE		0x02
#define _AWE_REMOVE_LAST_SAMPLES	0x03
#define _AWE_INITIALIZE_CHIP		0x04
#define _AWE_SEND_EFFECT		0x05
#define _AWE_TERMINATE_CHANNEL		0x06
#define _AWE_TERMINATE_ALL		0x07
#define _AWE_INITIAL_VOLUME		0x08
#define _AWE_INITIAL_ATTEN	_AWE_INITIAL_VOLUME
#define _AWE_RESET_CHANNEL		0x09
#define _AWE_CHANNEL_MODE		0x0a
#define _AWE_DRUM_CHANNELS		0x0b
#define _AWE_MISC_MODE			0x0c
#define _AWE_RELEASE_ALL		0x0d
#define _AWE_NOTEOFF_ALL		0x0e
#define _AWE_CHN_PRESSURE		0x0f
/*#define _AWE_GET_CURRENT_MODE		0x10*/
#define _AWE_EQUALIZER			0x11
/*#define _AWE_GET_MISC_MODE		0x12*/
/*#define _AWE_GET_FONTINFO		0x13*/

#define _AWE_MODE_FLAG			0x80
#define _AWE_COOKED_FLAG		0x40	/* not supported */
#define _AWE_MODE_VALUE_MASK		0x3F

/*----------------------------------------------------------------*/

#define _AWE_SET_CMD(p,dev,voice,cmd,p1,p2) \
{((char*)(p))[0] = SEQ_PRIVATE;\
 ((char*)(p))[1] = dev;\
 ((char*)(p))[2] = _AWE_MODE_FLAG|(cmd);\
 ((char*)(p))[3] = voice;\
 ((unsigned short*)(p))[2] = p1;\
 ((unsigned short*)(p))[3] = p2;}

/* buffered access */
#define _AWE_CMD(dev, voice, cmd, p1, p2) \
{_SEQ_NEEDBUF(8);\
 _AWE_SET_CMD(_seqbuf + _seqbufptr, dev, voice, cmd, p1, p2);\
 _SEQ_ADVBUF(8);}

/* direct access */
#define _AWE_CMD_NOW(seqfd,dev,voice,cmd,p1,p2) \
{struct seq_event_rec tmp;\
 _AWE_SET_CMD(&tmp, dev, voice, cmd, p1, p2);\
 ioctl(seqfd, SNDCTL_SEQ_OUTOFBAND, &tmp);}

/*----------------------------------------------------------------*/

/* set debugging mode */
#define AWE_DEBUG_MODE(dev,p1)	_AWE_CMD(dev, 0, _AWE_DEBUG_MODE, p1, 0)
/* set reverb mode; from 0 to 7 */
#define AWE_REVERB_MODE(dev,p1)	_AWE_CMD(dev, 0, _AWE_REVERB_MODE, p1, 0)
/* set chorus mode; from 0 to 7 */
#define AWE_CHORUS_MODE(dev,p1)	_AWE_CMD(dev, 0, _AWE_CHORUS_MODE, p1, 0)

/* reset channel */
#define AWE_RESET_CHANNEL(dev,ch) _AWE_CMD(dev, ch, _AWE_RESET_CHANNEL, 0, 0)
#define AWE_RESET_CONTROL(dev,ch) _AWE_CMD(dev, ch, _AWE_RESET_CHANNEL, 1, 0)

/* send an effect to all layers */
#define AWE_SEND_EFFECT(dev,voice,type,value) _AWE_CMD(dev,voice,_AWE_SEND_EFFECT,type,value)
#define AWE_ADD_EFFECT(dev,voice,type,value) _AWE_CMD(dev,voice,_AWE_SEND_EFFECT,((type)|0x80),value)
#define AWE_UNSET_EFFECT(dev,voice,type) _AWE_CMD(dev,voice,_AWE_SEND_EFFECT,((type)|0x40),0)
/* send an effect to a layer */
#define AWE_SEND_LAYER_EFFECT(dev,voice,layer,type,value) _AWE_CMD(dev,voice,_AWE_SEND_EFFECT,((layer+1)<<8|(type)),value)
#define AWE_ADD_LAYER_EFFECT(dev,voice,layer,type,value) _AWE_CMD(dev,voice,_AWE_SEND_EFFECT,((layer+1)<<8|(type)|0x80),value)
#define AWE_UNSET_LAYER_EFFECT(dev,voice,layer,type) _AWE_CMD(dev,voice,_AWE_SEND_EFFECT,((layer+1)<<8|(type)|0x40),0)

/* terminate sound on the channel/voice */
#define AWE_TERMINATE_CHANNEL(dev,voice) _AWE_CMD(dev,voice,_AWE_TERMINATE_CHANNEL,0,0)
/* terminate all sounds */
#define AWE_TERMINATE_ALL(dev) _AWE_CMD(dev, 0, _AWE_TERMINATE_ALL, 0, 0)
/* release all sounds (w/o sustain effect) */
#define AWE_RELEASE_ALL(dev) _AWE_CMD(dev, 0, _AWE_RELEASE_ALL, 0, 0)
/* note off all sounds (w sustain effect) */
#define AWE_NOTEOFF_ALL(dev) _AWE_CMD(dev, 0, _AWE_NOTEOFF_ALL, 0, 0)

/* set initial attenuation */
#define AWE_INITIAL_VOLUME(dev,atten) _AWE_CMD(dev, 0, _AWE_INITIAL_VOLUME, atten, 0)
#define AWE_INITIAL_ATTEN  AWE_INITIAL_VOLUME
/* relative attenuation */
#define AWE_SET_ATTEN(dev,atten)  _AWE_CMD(dev, 0, _AWE_INITIAL_VOLUME, atten, 1)

/* set channel playing mode; mode=0/1/2 */
#define AWE_SET_CHANNEL_MODE(dev,mode) _AWE_CMD(dev, 0, _AWE_CHANNEL_MODE, mode, 0)
#define AWE_PLAY_INDIRECT	0	/* indirect voice mode (default) */
#define AWE_PLAY_MULTI		1	/* multi note voice mode */
#define AWE_PLAY_DIRECT		2	/* direct single voice mode */
#define AWE_PLAY_MULTI2		3	/* sequencer2 mode; used internally */

/* set drum channel mask; channels is 32bit long value */
#define AWE_DRUM_CHANNELS(dev,channels) _AWE_CMD(dev, 0, _AWE_DRUM_CHANNELS, ((channels) & 0xffff), ((channels) >> 16))

/* set bass and treble control; values are from 0 to 11 */
#define AWE_EQUALIZER(dev,bass,treble) _AWE_CMD(dev, 0, _AWE_EQUALIZER, bass, treble)

/* remove last loaded samples */
#define AWE_REMOVE_LAST_SAMPLES(seqfd,dev) _AWE_CMD_NOW(seqfd, dev, 0, _AWE_REMOVE_LAST_SAMPLES, 0, 0)
/* initialize emu8000 chip */
#define AWE_INITIALIZE_CHIP(seqfd,dev) _AWE_CMD_NOW(seqfd, dev, 0, _AWE_INITIALIZE_CHIP, 0, 0)

/* set miscellaneous modes; meta command */
#define AWE_MISC_MODE(dev,mode,value) _AWE_CMD(dev, 0, _AWE_MISC_MODE, mode, value)
/* exclusive sound off; 1=off */
#define AWE_EXCLUSIVE_SOUND(dev,mode) AWE_MISC_MODE(dev,AWE_MD_EXCLUSIVE_SOUND,mode)
/* default GUS bank number */
#define AWE_SET_GUS_BANK(dev,bank) AWE_MISC_MODE(dev,AWE_MD_GUS_BANK,bank)
/* change panning position in realtime; 0=don't 1=do */
#define AWE_REALTIME_PAN(dev,mode) AWE_MISC_MODE(dev,AWE_MD_REALTIME_PAN,mode)

/* extended pressure controls; not portable with other sound drivers */
#define AWE_KEY_PRESSURE(dev,ch,note,vel) SEQ_START_NOTE(dev,ch,(note)+128,vel)
#define AWE_CHN_PRESSURE(dev,ch,vel) _AWE_CMD(dev,ch,_AWE_CHN_PRESSURE,vel,0)

/*----------------------------------------------------------------*/

/* reverb mode parameters */
#define	AWE_REVERB_ROOM1	0
#define AWE_REVERB_ROOM2	1
#define	AWE_REVERB_ROOM3	2
#define	AWE_REVERB_HALL1	3
#define	AWE_REVERB_HALL2	4
#define	AWE_REVERB_PLATE	5
#define	AWE_REVERB_DELAY	6
#define	AWE_REVERB_PANNINGDELAY 7
#define AWE_REVERB_PREDEFINED	8
/* user can define reverb modes up to 32 */
#define AWE_REVERB_NUMBERS	32

typedef struct awe_reverb_fx_rec {
	unsigned short parms[28];
} awe_reverb_fx_rec;

/*----------------------------------------------------------------*/

/* chorus mode parameters */
#define AWE_CHORUS_1		0
#define	AWE_CHORUS_2		1
#define	AWE_CHORUS_3		2
#define	AWE_CHORUS_4		3
#define	AWE_CHORUS_FEEDBACK	4
#define	AWE_CHORUS_FLANGER	5
#define	AWE_CHORUS_SHORTDELAY	6
#define	AWE_CHORUS_SHORTDELAY2	7
#define AWE_CHORUS_PREDEFINED	8
/* user can define chorus modes up to 32 */
#define AWE_CHORUS_NUMBERS	32

typedef struct awe_chorus_fx_rec {
	unsigned short feedback;	/* feedback level (0xE600-0xE6FF) */
	unsigned short delay_offset;	/* delay (0-0x0DA3) [1/44100 sec] */
	unsigned short lfo_depth;	/* LFO depth (0xBC00-0xBCFF) */
	unsigned int delay;	/* right delay (0-0xFFFFFFFF) [1/256/44100 sec] */
	unsigned int lfo_freq;		/* LFO freq LFO freq (0-0xFFFFFFFF) */
} awe_chorus_fx_rec;

/*----------------------------------------------------------------*/

/* misc mode types */
enum {
/* 0*/	AWE_MD_EXCLUSIVE_OFF,	/* obsolete */
/* 1*/	AWE_MD_EXCLUSIVE_ON,	/* obsolete */
/* 2*/	AWE_MD_VERSION,		/* read only */
/* 3*/	AWE_MD_EXCLUSIVE_SOUND,	/* 0/1: exclusive note on (default=1) */
/* 4*/	AWE_MD_REALTIME_PAN,	/* 0/1: do realtime pan change (default=1) */
/* 5*/	AWE_MD_GUS_BANK,	/* bank number for GUS patches (default=0) */
/* 6*/	AWE_MD_KEEP_EFFECT,	/* 0/1: keep effect values, (default=0) */
/* 7*/	AWE_MD_ZERO_ATTEN,	/* attenuation of max volume (default=32) */
/* 8*/	AWE_MD_CHN_PRIOR,	/* 0/1: set MIDI channel priority mode (default=1) */
/* 9*/	AWE_MD_MOD_SENSE,	/* integer: modwheel sensitivity (def=18) */
/*10*/	AWE_MD_DEF_PRESET,	/* integer: default preset number (def=0) */
/*11*/	AWE_MD_DEF_BANK,	/* integer: default bank number (def=0) */
/*12*/	AWE_MD_DEF_DRUM,	/* integer: default drumset number (def=0) */
/*13*/	AWE_MD_TOGGLE_DRUM_BANK, /* 0/1: toggle drum flag with bank# (def=0) */
/*14*/	AWE_MD_NEW_VOLUME_CALC,	/* 0/1: volume calculation mode (def=1) */
/*15*/	AWE_MD_CHORUS_MODE,	/* integer: chorus mode (def=2) */
/*16*/	AWE_MD_REVERB_MODE,	/* integer: chorus mode (def=4) */
/*17*/	AWE_MD_BASS_LEVEL,	/* integer: bass level (def=5) */
/*18*/	AWE_MD_TREBLE_LEVEL,	/* integer: treble level (def=9) */
/*19*/	AWE_MD_DEBUG_MODE,	/* integer: debug level (def=0) */
/*20*/	AWE_MD_PAN_EXCHANGE,	/* 0/1: exchange panning direction (def=0) */
	AWE_MD_END,
};

/*----------------------------------------------------------------*/

/* effect parameters */
enum {

/* modulation envelope parameters */
/* 0*/	AWE_FX_ENV1_DELAY,	/* WORD: ENVVAL */
/* 1*/	AWE_FX_ENV1_ATTACK,	/* BYTE: up ATKHLD */
/* 2*/	AWE_FX_ENV1_HOLD,	/* BYTE: lw ATKHLD */
/* 3*/	AWE_FX_ENV1_DECAY,	/* BYTE: lw DCYSUS */
/* 4*/	AWE_FX_ENV1_RELEASE,	/* BYTE: lw DCYSUS */
/* 5*/	AWE_FX_ENV1_SUSTAIN,	/* BYTE: up DCYSUS */
/* 6*/	AWE_FX_ENV1_PITCH,	/* BYTE: up PEFE */
/* 7*/	AWE_FX_ENV1_CUTOFF,	/* BYTE: lw PEFE */

/* volume envelope parameters */
/* 8*/	AWE_FX_ENV2_DELAY,	/* WORD: ENVVOL */
/* 9*/	AWE_FX_ENV2_ATTACK,	/* BYTE: up ATKHLDV */
/*10*/	AWE_FX_ENV2_HOLD,	/* BYTE: lw ATKHLDV */
/*11*/	AWE_FX_ENV2_DECAY,	/* BYTE: lw DCYSUSV */
/*12*/	AWE_FX_ENV2_RELEASE,	/* BYTE: lw DCYSUSV */
/*13*/	AWE_FX_ENV2_SUSTAIN,	/* BYTE: up DCYSUSV */
	
/* LFO1 (tremolo & vibrato) parameters */
/*14*/	AWE_FX_LFO1_DELAY,	/* WORD: LFO1VAL */
/*15*/	AWE_FX_LFO1_FREQ,	/* BYTE: lo TREMFRQ */
/*16*/	AWE_FX_LFO1_VOLUME,	/* BYTE: up TREMFRQ */
/*17*/	AWE_FX_LFO1_PITCH,	/* BYTE: up FMMOD */
/*18*/	AWE_FX_LFO1_CUTOFF,	/* BYTE: lo FMMOD */

/* LFO2 (vibrato) parameters */
/*19*/	AWE_FX_LFO2_DELAY,	/* WORD: LFO2VAL */
/*20*/	AWE_FX_LFO2_FREQ,	/* BYTE: lo FM2FRQ2 */
/*21*/	AWE_FX_LFO2_PITCH,	/* BYTE: up FM2FRQ2 */

/* Other overall effect parameters */
/*22*/	AWE_FX_INIT_PITCH,	/* SHORT: pitch offset */
/*23*/	AWE_FX_CHORUS,		/* BYTE: chorus effects send (0-255) */
/*24*/	AWE_FX_REVERB,		/* BYTE: reverb effects send (0-255) */
/*25*/	AWE_FX_CUTOFF,		/* BYTE: up IFATN */
/*26*/	AWE_FX_FILTERQ,		/* BYTE: up CCCA */

/* Sample / loop offset changes */
/*27*/	AWE_FX_SAMPLE_START,	/* SHORT: offset */
/*28*/	AWE_FX_LOOP_START,	/* SHORT: offset */
/*29*/	AWE_FX_LOOP_END,	/* SHORT: offset */
/*30*/	AWE_FX_COARSE_SAMPLE_START,	/* SHORT: upper word offset */
/*31*/	AWE_FX_COARSE_LOOP_START,	/* SHORT: upper word offset */
/*32*/	AWE_FX_COARSE_LOOP_END,		/* SHORT: upper word offset */
/*33*/	AWE_FX_ATTEN,		/* BYTE: lo IFATN */

	AWE_FX_END,
};

#endif /* AWE_VOICE_H */
