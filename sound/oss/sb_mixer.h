/*
 * sound/sb_mixer.h
 * 
 * Definitions for the SB Pro and SB16 mixers
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */

/*
 * Modified:
 *	Hunyue Yau	Jan 6 1994
 *	Added defines for the Sound Galaxy NX Pro mixer.
 *
 *	Rolf Fokkens	Dec 20 1998
 *	Added defines for some ES188x chips.
 *
 *	Rolf Fokkens	Dec 27 1998
 *	Moved static stuff to sb_mixer.c
 *
 */
/*
 * Mixer registers
 * 
 * NOTE!	RECORD_SRC == IN_FILTER
 */

/* 
 * Mixer registers of SB Pro
 */
#define VOC_VOL		0x04
#define MIC_VOL		0x0A
#define MIC_MIX		0x0A
#define RECORD_SRC	0x0C
#define IN_FILTER	0x0C
#define OUT_FILTER	0x0E
#define MASTER_VOL	0x22
#define FM_VOL		0x26
#define CD_VOL		0x28
#define LINE_VOL	0x2E
#define IRQ_NR		0x80
#define DMA_NR		0x81
#define IRQ_STAT	0x82
#define OPSW		0x3c

/*
 * Additional registers on the SG NX Pro 
 */
#define COVOX_VOL	0x42
#define TREBLE_LVL	0x44
#define BASS_LVL	0x46

#define FREQ_HI         (1 << 3)/* Use High-frequency ANFI filters */
#define FREQ_LOW        0	/* Use Low-frequency ANFI filters */
#define FILT_ON         0	/* Yes, 0 to turn it on, 1 for off */
#define FILT_OFF        (1 << 5)

#define MONO_DAC	0x00
#define STEREO_DAC	0x02

/*
 * Mixer registers of SB16
 */
#define SB16_OMASK	0x3c
#define SB16_IMASK_L	0x3d
#define SB16_IMASK_R	0x3e

#define LEFT_CHN	0
#define RIGHT_CHN	1

/*
 * 3DSE register of AWE32/64
 */
#define AWE_3DSE	0x90

/*
 * Mixer registers of ALS007
 */
#define ALS007_RECORD_SRC	0x6c
#define ALS007_OUTPUT_CTRL1	0x3c
#define ALS007_OUTPUT_CTRL2	0x4c

#define MIX_ENT(name, reg_l, bit_l, len_l, reg_r, bit_r, len_r)	\
	{{reg_l, bit_l, len_l}, {reg_r, bit_r, len_r}}

/*
 *	Recording sources (SB Pro)
 */

#define SRC__MIC         1	/* Select Microphone recording source */
#define SRC__CD          3	/* Select CD recording source */
#define SRC__LINE        7	/* Use Line-in for recording source */

/*
 *	Recording sources for ALS-007
 */

#define ALS007_MIC	4
#define ALS007_LINE	6
#define ALS007_CD	2
#define ALS007_SYNTH	7
