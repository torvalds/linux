/*
 * sound/oss/ad1848_mixer.h
 *
 * Definitions for the mixer of AD1848 and compatible codecs.
 */

/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */


/*
 * The AD1848 codec has generic input lines called Line, Aux1 and Aux2.
 * Sound card manufacturers have connected actual inputs (CD, synth, line,
 * etc) to these inputs in different order. Therefore it's difficult
 * to assign mixer channels to these inputs correctly. The following
 * contains two alternative mappings. The first one is for GUS MAX and
 * the second is just a generic one (line1, line2 and line3).
 * (Actually this is not a mapping but rather some kind of interleaving
 * solution).
 */
#define MODE1_REC_DEVICES		(SOUND_MASK_LINE3 | SOUND_MASK_MIC | \
					 SOUND_MASK_LINE1 | SOUND_MASK_IMIX)

#define SPRO_REC_DEVICES		(SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD | SOUND_MASK_LINE1)

#define MODE1_MIXER_DEVICES		(SOUND_MASK_LINE1 | SOUND_MASK_MIC | \
					 SOUND_MASK_LINE2 | \
					 SOUND_MASK_IGAIN | \
					 SOUND_MASK_PCM | SOUND_MASK_IMIX)

#define MODE2_MIXER_DEVICES		(SOUND_MASK_LINE1 | SOUND_MASK_LINE2 | \
					 SOUND_MASK_MIC | \
					 SOUND_MASK_LINE3 | SOUND_MASK_SPEAKER | \
					 SOUND_MASK_IGAIN | \
					 SOUND_MASK_PCM | SOUND_MASK_IMIX)

#define MODE3_MIXER_DEVICES		(MODE2_MIXER_DEVICES | SOUND_MASK_VOLUME)

/* OPTi 82C930 has no IMIX level control, but it can still be selected as an
 * input
 */
#define C930_MIXER_DEVICES	(SOUND_MASK_LINE1 | SOUND_MASK_LINE2 | \
				 SOUND_MASK_MIC | SOUND_MASK_VOLUME | \
				 SOUND_MASK_LINE3 | \
				 SOUND_MASK_IGAIN | SOUND_MASK_PCM)

#define SPRO_MIXER_DEVICES	(SOUND_MASK_VOLUME | SOUND_MASK_PCM | \
				 SOUND_MASK_LINE | SOUND_MASK_SYNTH | \
				 SOUND_MASK_CD | SOUND_MASK_MIC | \
				 SOUND_MASK_SPEAKER | SOUND_MASK_LINE1 | \
				 SOUND_MASK_OGAIN)

struct mixer_def {
	unsigned int regno:6;		/* register number for volume */
	unsigned int polarity:1;	/* volume polarity: 0=normal, 1=reversed */
	unsigned int bitpos:3;		/* position of bits in register for volume */
	unsigned int nbits:3;		/* number of bits in register for volume */
	unsigned int mutereg:6;		/* register number for mute bit */
	unsigned int mutepol:1;		/* mute polarity: 0=normal, 1=reversed */
	unsigned int mutepos:4;		/* position of mute bit in register */
	unsigned int recreg:6;		/* register number for recording bit */
	unsigned int recpol:1;		/* recording polarity: 0=normal, 1=reversed */
	unsigned int recpos:4;		/* position of recording bit in register */
};

static char mix_cvt[101] = {
	 0, 0, 3, 7,10,13,16,19,21,23,26,28,30,32,34,35,37,39,40,42,
	43,45,46,47,49,50,51,52,53,55,56,57,58,59,60,61,62,63,64,65,
	65,66,67,68,69,70,70,71,72,73,73,74,75,75,76,77,77,78,79,79,
	80,81,81,82,82,83,84,84,85,85,86,86,87,87,88,88,89,89,90,90,
	91,91,92,92,93,93,94,94,95,95,96,96,96,97,97,98,98,98,99,99,
	100
};

typedef struct mixer_def mixer_ent;
typedef mixer_ent mixer_ents[2];

/*
 * Most of the mixer entries work in backwards. Setting the polarity field
 * makes them to work correctly.
 *
 * The channel numbering used by individual sound cards is not fixed. Some
 * cards have assigned different meanings for the AUX1, AUX2 and LINE inputs.
 * The current version doesn't try to compensate this.
 */

#define MIX_ENT(name, reg_l, pola_l, pos_l, len_l, reg_r, pola_r, pos_r, len_r, mute_bit)	\
	[name] = {{reg_l, pola_l, pos_l, len_l, reg_l, 0, mute_bit, 0, 0, 8},			\
		  {reg_r, pola_r, pos_r, len_r, reg_r, 0, mute_bit, 0, 0, 8}}

#define MIX_ENT2(name, reg_l, pola_l, pos_l, len_l, mute_reg_l, mute_pola_l, mute_pos_l, \
		    rec_reg_l, rec_pola_l, rec_pos_l,					 \
		 reg_r, pola_r, pos_r, len_r, mute_reg_r, mute_pola_r, mute_pos_r,	 \
		    rec_reg_r, rec_pola_r, rec_pos_r)					 \
	[name] = {{reg_l, pola_l, pos_l, len_l, mute_reg_l, mute_pola_l, mute_pos_l,	 \
		    rec_reg_l, rec_pola_l, rec_pos_l},					 \
		  {reg_r, pola_r, pos_r, len_r, mute_reg_r, mute_pola_r, mute_pos_r,	 \
		    rec_reg_r, rec_pola_r, rec_pos_r}}

static mixer_ents ad1848_mix_devices[32] = {
	MIX_ENT(SOUND_MIXER_VOLUME,	27, 1, 0, 4,	29, 1, 0, 4,  8),
	MIX_ENT(SOUND_MIXER_BASS,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_TREBLE,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_SYNTH,	 4, 1, 0, 5,	 5, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_PCM,	 6, 1, 0, 6,	 7, 1, 0, 6,  7),
	MIX_ENT(SOUND_MIXER_SPEAKER,	26, 1, 0, 4,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_LINE,	18, 1, 0, 5,	19, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_MIC,	 0, 0, 5, 1,	 1, 0, 5, 1,  8),
	MIX_ENT(SOUND_MIXER_CD,		 2, 1, 0, 5,	 3, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_IMIX,	13, 1, 2, 6,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_ALTPCM,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_RECLEV,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_IGAIN,	 0, 0, 0, 4,	 1, 0, 0, 4,  8),
	MIX_ENT(SOUND_MIXER_OGAIN,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_LINE1,	 2, 1, 0, 5,	 3, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_LINE2,	 4, 1, 0, 5,	 5, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_LINE3,	18, 1, 0, 5,	19, 1, 0, 5,  7)
};

static mixer_ents iwave_mix_devices[32] = {
	MIX_ENT(SOUND_MIXER_VOLUME,	25, 1, 0, 5,	27, 1, 0, 5,  8),
	MIX_ENT(SOUND_MIXER_BASS,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_TREBLE,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_SYNTH,	 4, 1, 0, 5,	 5, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_PCM,	 6, 1, 0, 6,	 7, 1, 0, 6,  7),
	MIX_ENT(SOUND_MIXER_SPEAKER,	26, 1, 0, 4,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_LINE,	18, 1, 0, 5,	19, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_MIC,	 0, 0, 5, 1,	 1, 0, 5, 1,  8),
	MIX_ENT(SOUND_MIXER_CD,		 2, 1, 0, 5,	 3, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_IMIX,	16, 1, 0, 5,	17, 1, 0, 5,  8),
	MIX_ENT(SOUND_MIXER_ALTPCM,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_RECLEV,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_IGAIN,	 0, 0, 0, 4,	 1, 0, 0, 4,  8),
	MIX_ENT(SOUND_MIXER_OGAIN,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_LINE1,	 2, 1, 0, 5,	 3, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_LINE2,	 4, 1, 0, 5,	 5, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_LINE3,	18, 1, 0, 5,	19, 1, 0, 5,  7)
};

static mixer_ents cs42xb_mix_devices[32] = {
	/* Digital master volume actually has seven bits, but we only use
	   six to avoid the discontinuity when the analog gain kicks in. */
	MIX_ENT(SOUND_MIXER_VOLUME,	46, 1, 0, 6,	47, 1, 0, 6,  7),
	MIX_ENT(SOUND_MIXER_BASS,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_TREBLE,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_SYNTH,	 4, 1, 0, 5,	 5, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_PCM,	 6, 1, 0, 6,	 7, 1, 0, 6,  7),
	MIX_ENT(SOUND_MIXER_SPEAKER,	26, 1, 0, 4,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_LINE,	18, 1, 0, 5,	19, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_MIC,	34, 1, 0, 5,	35, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_CD,		 2, 1, 0, 5,	 3, 1, 0, 5,  7),
	/* For the IMIX entry, it was not possible to use the MIX_ENT macro
	   because the mute bit is in different positions for the two
	   channels and requires reverse polarity. */
	[SOUND_MIXER_IMIX] = {{13, 1, 2, 6, 13, 1, 0, 0, 0, 8},
		      {42, 1, 0, 6, 42, 1, 7, 0, 0, 8}},
	MIX_ENT(SOUND_MIXER_ALTPCM,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_RECLEV,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_IGAIN,	 0, 0, 0, 4,	 1, 0, 0, 4,  8),
	MIX_ENT(SOUND_MIXER_OGAIN,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_LINE1,	 2, 1, 0, 5,	 3, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_LINE2,	 4, 1, 0, 5,	 5, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_LINE3,	38, 1, 0, 6,	39, 1, 0, 6,  7)
};

/* OPTi 82C930 has somewhat different port addresses.
 * Note: VOLUME == SPEAKER, SYNTH == LINE2, LINE == LINE3, CD == LINE1
 * VOLUME, SYNTH, LINE, CD are not enabled above.
 * MIC is level of mic monitoring direct to output. Same for CD, LINE, etc.
 */
static mixer_ents c930_mix_devices[32] = {
	MIX_ENT(SOUND_MIXER_VOLUME,	22, 1, 1, 5,	23, 1, 1, 5,  7),
	MIX_ENT(SOUND_MIXER_BASS,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_TREBLE,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_SYNTH,	 4, 1, 1, 4,	 5, 1, 1, 4,  7),
	MIX_ENT(SOUND_MIXER_PCM,	 6, 1, 0, 5,	 7, 1, 0, 5,  7),
	MIX_ENT(SOUND_MIXER_SPEAKER,	22, 1, 1, 5,	23, 1, 1, 5,  7),
	MIX_ENT(SOUND_MIXER_LINE,	18, 1, 1, 4,	19, 1, 1, 4,  7),
	MIX_ENT(SOUND_MIXER_MIC,	20, 1, 1, 4,	21, 1, 1, 4,  7),
	MIX_ENT(SOUND_MIXER_CD,		 2, 1, 1, 4,	 3, 1, 1, 4,  7),
	MIX_ENT(SOUND_MIXER_IMIX,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_ALTPCM,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_RECLEV,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_IGAIN,	 0, 0, 0, 4,	 1, 0, 0, 4,  8),
	MIX_ENT(SOUND_MIXER_OGAIN,	 0, 0, 0, 0,	 0, 0, 0, 0,  8),
	MIX_ENT(SOUND_MIXER_LINE1,	 2, 1, 1, 4,	 3, 1, 1, 4,  7),
	MIX_ENT(SOUND_MIXER_LINE2,	 4, 1, 1, 4,	 5, 1, 1, 4,  7),
	MIX_ENT(SOUND_MIXER_LINE3,	18, 1, 1, 4,	19, 1, 1, 4,  7)
};

static mixer_ents spro_mix_devices[32] = {
	MIX_ENT (SOUND_MIXER_VOLUME,	19, 0, 4, 4,			 19, 0, 0, 4,  8),
	MIX_ENT (SOUND_MIXER_BASS,	 0, 0, 0, 0,			  0, 0, 0, 0,  8),
	MIX_ENT (SOUND_MIXER_TREBLE,	 0, 0, 0, 0,			  0, 0, 0, 0,  8),
	MIX_ENT2(SOUND_MIXER_SYNTH,	 4, 1, 1, 4, 23, 0, 3,  0, 0, 8,
		 			 5, 1, 1, 4, 23, 0, 3, 0, 0, 8),
	MIX_ENT (SOUND_MIXER_PCM,	 6, 1, 1, 4,			  7, 1, 1, 4,  8),
	MIX_ENT (SOUND_MIXER_SPEAKER,	18, 0, 3, 2,			  0, 0, 0, 0,  8),
	MIX_ENT2(SOUND_MIXER_LINE,	20, 0, 4, 4, 17, 1, 4, 16, 0, 2,
		 			20, 0, 0, 4, 17, 1, 3, 16, 0, 1),
	MIX_ENT2(SOUND_MIXER_MIC,	18, 0, 0, 3, 17, 1, 0, 16, 0, 0,
		 			 0, 0, 0, 0,  0, 0, 0,  0, 0, 0),
	MIX_ENT2(SOUND_MIXER_CD,	21, 0, 4, 4, 17, 1, 2, 16, 0, 4,
					21, 0, 0, 4, 17, 1, 1, 16, 0, 3),
	MIX_ENT (SOUND_MIXER_IMIX,	 0, 0, 0, 0,			  0, 0, 0, 0,  8),
	MIX_ENT (SOUND_MIXER_ALTPCM,	 0, 0, 0, 0,			  0, 0, 0, 0,  8),
	MIX_ENT (SOUND_MIXER_RECLEV,	 0, 0, 0, 0,			  0, 0, 0, 0,  8),
	MIX_ENT (SOUND_MIXER_IGAIN,	 0, 0, 0, 0,			  0, 0, 0, 0,  8),
	MIX_ENT (SOUND_MIXER_OGAIN,	17, 1, 6, 1,			  0, 0, 0, 0,  8),
	/* This is external wavetable */
	MIX_ENT2(SOUND_MIXER_LINE1,	22, 0, 4, 4, 23, 1, 1, 23, 0, 4,
		 			22, 0, 0, 4, 23, 1, 0, 23, 0, 5),
};

static int default_mixer_levels[32] =
{
	0x3232,			/* Master Volume */
	0x3232,			/* Bass */
	0x3232,			/* Treble */
	0x4b4b,			/* FM */
	0x3232,			/* PCM */
	0x1515,			/* PC Speaker */
	0x2020,			/* Ext Line */
	0x1010,			/* Mic */
	0x4b4b,			/* CD */
	0x0000,			/* Recording monitor */
	0x4b4b,			/* Second PCM */
	0x4b4b,			/* Recording level */
	0x4b4b,			/* Input gain */
	0x4b4b,			/* Output gain */
	0x2020,			/* Line1 */
	0x2020,			/* Line2 */
	0x1515			/* Line3 (usually line in)*/
};

#define LEFT_CHN	0
#define RIGHT_CHN	1

/*
 * Channel enable bits for ioctl(SOUND_MIXER_PRIVATE1)
 */

#ifndef AUDIO_SPEAKER
#define	AUDIO_SPEAKER		0x01	/* Enable mono output */
#define	AUDIO_HEADPHONE		0x02	/* Sparc only */
#define	AUDIO_LINE_OUT		0x04	/* Sparc only */
#endif
