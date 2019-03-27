/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * 1. Redistributions of source code must retain the
 * Copyright (c) 1997 Amancio Hasty, 1999 Roger Hardiman
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Amancio Hasty and
 *      Roger Hardiman
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * This is part of the Driver for Video Capture Cards (Frame grabbers)
 * and TV Tuner cards using the Brooktree Bt848, Bt848A, Bt849A, Bt878, Bt879
 * chipset.
 * Copyright Roger Hardiman and Amancio Hasty.
 *
 * bktr_tuner : This deals with controlling the tuner fitted to TV cards.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#ifdef __NetBSD__
#include <sys/proc.h>
#endif

#ifdef __FreeBSD__
#if (__FreeBSD_version < 500000)
#include <machine/clock.h>              /* for DELAY */
#include <pci/pcivar.h>
#else
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/selinfo.h>
#include <dev/pci/pcivar.h>
#endif

#include <machine/bus.h>
#include <sys/bus.h>
#endif

#ifdef __NetBSD__
#include <dev/ic/bt8xx.h>	/* NetBSD .h file location */
#include <dev/pci/bktr/bktr_reg.h>
#include <dev/pci/bktr/bktr_tuner.h>
#include <dev/pci/bktr/bktr_card.h>
#include <dev/pci/bktr/bktr_core.h>
#else
#include <dev/bktr/ioctl_meteor.h>
#include <dev/bktr/ioctl_bt848.h>	/* extensions to ioctl_meteor.h */
#include <dev/bktr/bktr_reg.h>
#include <dev/bktr/bktr_tuner.h>
#include <dev/bktr/bktr_card.h>
#include <dev/bktr/bktr_core.h>
#endif



#if defined( TUNER_AFC )
#define AFC_DELAY               10000   /* 10 millisend delay */
#define AFC_BITS                0x07
#define AFC_FREQ_MINUS_125      0x00
#define AFC_FREQ_MINUS_62       0x01
#define AFC_FREQ_CENTERED       0x02
#define AFC_FREQ_PLUS_62        0x03
#define AFC_FREQ_PLUS_125       0x04
#define AFC_MAX_STEP            (5 * FREQFACTOR) /* no more than 5 MHz */
#endif /* TUNER_AFC */

  
#define TTYPE_XXX               0
#define TTYPE_NTSC              1
#define TTYPE_NTSC_J            2
#define TTYPE_PAL               3
#define TTYPE_PAL_M             4
#define TTYPE_PAL_N             5
#define TTYPE_SECAM             6
  
#define TSA552x_CB_MSB          (0x80)
#define TSA552x_CB_CP           (1<<6)	/* set this for fast tuning */
#define TSA552x_CB_T2           (1<<5)	/* test mode - Normally set to 0 */
#define TSA552x_CB_T1           (1<<4)	/* test mode - Normally set to 0 */
#define TSA552x_CB_T0           (1<<3)	/* test mode - Normally set to 1 */
#define TSA552x_CB_RSA          (1<<2)	/* 0 for 31.25 khz, 1 for 62.5 kHz */
#define TSA552x_CB_RSB          (1<<1)	/* 0 for FM 50kHz steps, 1 = Use RSA*/
#define TSA552x_CB_OS           (1<<0)	/* Set to 0 for normal operation */

#define TSA552x_RADIO           (TSA552x_CB_MSB |       \
                                 TSA552x_CB_T0)

/* raise the charge pump voltage for fast tuning */
#define TSA552x_FCONTROL        (TSA552x_CB_MSB |       \
                                 TSA552x_CB_CP  |       \
                                 TSA552x_CB_T0  |       \
                                 TSA552x_CB_RSA |       \
                                 TSA552x_CB_RSB)
  
/* lower the charge pump voltage for better residual oscillator FM */
#define TSA552x_SCONTROL        (TSA552x_CB_MSB |       \
                                 TSA552x_CB_T0  |       \
                                 TSA552x_CB_RSA |       \
                                 TSA552x_CB_RSB)
  
/* The control value for the ALPS TSCH5 Tuner */
#define TSCH5_FCONTROL          0x82
#define TSCH5_RADIO             0x86
  
/* The control value for the ALPS TSBH1 Tuner */
#define TSBH1_FCONTROL		0xce


static void mt2032_set_tv_freq(bktr_ptr_t bktr, unsigned int freq);


static const struct TUNER tuners[] = {
/* XXX FIXME: fill in the band-switch crosspoints */
	/* NO_TUNER */
	{ "<no>",				/* the 'name' */
	   TTYPE_XXX,				/* input type */
 	   { 0x00,				/* control byte for Tuner PLL */
 	     0x00,
 	     0x00,
 	     0x00 },
	   { 0x00, 0x00 },			/* band-switch crosspoints */
	   { 0x00, 0x00, 0x00,0x00} },		/* the band-switch values */

	/* TEMIC_NTSC */
	{ "Temic NTSC",				/* the 'name' */
	   TTYPE_NTSC,				/* input type */
	   { TSA552x_SCONTROL,			/* control byte for Tuner PLL */
	     TSA552x_SCONTROL,
	     TSA552x_SCONTROL,
	     0x00 },
	   { 0x00, 0x00},			/* band-switch crosspoints */
	   { 0x02, 0x04, 0x01, 0x00 } },	/* the band-switch values */

	/* TEMIC_PAL */
	{ "Temic PAL",				/* the 'name' */
	   TTYPE_PAL,				/* input type */
	   { TSA552x_SCONTROL,			/* control byte for Tuner PLL */
	     TSA552x_SCONTROL,
	     TSA552x_SCONTROL,
	     0x00 },
	   { 0x00, 0x00 },			/* band-switch crosspoints */
	   { 0x02, 0x04, 0x01, 0x00 } },	/* the band-switch values */

	/* TEMIC_SECAM */
	{ "Temic SECAM",			/* the 'name' */
	   TTYPE_SECAM,				/* input type */
	   { TSA552x_SCONTROL,			/* control byte for Tuner PLL */
	     TSA552x_SCONTROL,
	     TSA552x_SCONTROL,
	     0x00 },
	   { 0x00, 0x00 },			/* band-switch crosspoints */
	   { 0x02, 0x04, 0x01,0x00 } },		/* the band-switch values */

	/* PHILIPS_NTSC */
	{ "Philips NTSC",			/* the 'name' */
	   TTYPE_NTSC,				/* input type */
	   { TSA552x_SCONTROL,			/* control byte for Tuner PLL */
	     TSA552x_SCONTROL,
	     TSA552x_SCONTROL,
	     0x00 },
	   { 0x00, 0x00 },			/* band-switch crosspoints */
	   { 0xa0, 0x90, 0x30, 0x00 } },	/* the band-switch values */

	/* PHILIPS_PAL */
	{ "Philips PAL",			/* the 'name' */
	   TTYPE_PAL,				/* input type */
	   { TSA552x_SCONTROL,			/* control byte for Tuner PLL */
	     TSA552x_SCONTROL,
	     TSA552x_SCONTROL,
	     0x00 },
	   { 0x00, 0x00 },			/* band-switch crosspoints */
	   { 0xa0, 0x90, 0x30, 0x00 } },	/* the band-switch values */

	/* PHILIPS_SECAM */
	{ "Philips SECAM",			/* the 'name' */
	   TTYPE_SECAM,				/* input type */
	   { TSA552x_SCONTROL,			/* control byte for Tuner PLL */
	     TSA552x_SCONTROL,
	     TSA552x_SCONTROL,
	     0x00 },
	   { 0x00, 0x00 },			/* band-switch crosspoints */
	   { 0xa7, 0x97, 0x37, 0x00 } },	/* the band-switch values */

	/* TEMIC_PAL I */
	{ "Temic PAL I",			/* the 'name' */
	   TTYPE_PAL,				/* input type */
	   { TSA552x_SCONTROL,			/* control byte for Tuner PLL */
	     TSA552x_SCONTROL,
	     TSA552x_SCONTROL,
	     0x00 },
	   { 0x00, 0x00 },			/* band-switch crosspoints */
	   { 0x02, 0x04, 0x01,0x00 } },		/* the band-switch values */

	/* PHILIPS_PALI */
	{ "Philips PAL I",			/* the 'name' */
	   TTYPE_PAL,				/* input type */
	   { TSA552x_SCONTROL,			/* control byte for Tuner PLL */
	     TSA552x_SCONTROL,
	     TSA552x_SCONTROL,
	     0x00 },
          { 0x00, 0x00 },                      /* band-switch crosspoints */
          { 0xa0, 0x90, 0x30,0x00 } },         /* the band-switch values */

       /* PHILIPS_FR1236_NTSC */
       { "Philips FR1236 NTSC FM",             /* the 'name' */
          TTYPE_NTSC,                          /* input type */
	  { TSA552x_FCONTROL,			/* control byte for Tuner PLL */
	    TSA552x_FCONTROL,
	    TSA552x_FCONTROL,
	    TSA552x_RADIO  },
          { 0x00, 0x00 },			/* band-switch crosspoints */
	  { 0xa0, 0x90, 0x30,0xa4 } },		/* the band-switch values */

	/* PHILIPS_FR1216_PAL */
	{ "Philips FR1216 PAL FM" ,		/* the 'name' */
	   TTYPE_PAL,				/* input type */
	   { TSA552x_FCONTROL,			/* control byte for Tuner PLL */
	     TSA552x_FCONTROL,
	     TSA552x_FCONTROL,
	     TSA552x_RADIO },
	   { 0x00, 0x00 },			/* band-switch crosspoints */
	   { 0xa0, 0x90, 0x30, 0xa4 } },	/* the band-switch values */

	/* PHILIPS_FR1236_SECAM */
	{ "Philips FR1236 SECAM FM",		/* the 'name' */
	   TTYPE_SECAM,				/* input type */
	   { TSA552x_FCONTROL,			/* control byte for Tuner PLL */
	     TSA552x_FCONTROL,
	     TSA552x_FCONTROL,
	     TSA552x_RADIO },
	   { 0x00, 0x00 },			/* band-switch crosspoints */
	   { 0xa7, 0x97, 0x37, 0xa4 } },	/* the band-switch values */

        /* ALPS TSCH5 NTSC */
        { "ALPS TSCH5 NTSC FM",                 /* the 'name' */
           TTYPE_NTSC,                          /* input type */
           { TSCH5_FCONTROL,                    /* control byte for Tuner PLL */
             TSCH5_FCONTROL,
             TSCH5_FCONTROL,
             TSCH5_RADIO },
           { 0x00, 0x00 },                      /* band-switch crosspoints */
           { 0x14, 0x12, 0x11, 0x04 } },        /* the band-switch values */

        /* ALPS TSBH1 NTSC */
        { "ALPS TSBH1 NTSC",                    /* the 'name' */
           TTYPE_NTSC,                          /* input type */
           { TSBH1_FCONTROL,                    /* control byte for Tuner PLL */
             TSBH1_FCONTROL,
             TSBH1_FCONTROL,
             0x00 },
           { 0x00, 0x00 },                      /* band-switch crosspoints */
           { 0x01, 0x02, 0x08, 0x00 } },        /* the band-switch values */

	/* MT2032 Microtune */
	{ "MT2032",				/* the 'name' */
	   TTYPE_PAL,				/* input type */
	   { TSA552x_SCONTROL,			/* control byte for Tuner PLL */
	     TSA552x_SCONTROL,
	     TSA552x_SCONTROL,
	     0x00 },
	   { 0x00, 0x00 },			/* band-switch crosspoints */
	   { 0xa0, 0x90, 0x30, 0x00 } },	/* the band-switch values */

	 /* LG TPI8PSB12P PAL */
	 { "LG TPI8PSB12P PAL",                 /* the 'name' */
	   TTYPE_PAL,                           /* input type */
	   { TSA552x_SCONTROL,                  /* control byte for Tuner PLL */
	     TSA552x_SCONTROL,
	     TSA552x_SCONTROL,
	     0x00 },
	   { 0x00, 0x00 },                      /* band-switch crosspoints */
	   { 0xa0, 0x90, 0x30, 0x8e } },        /* the band-switch values */

	 /* PHILIPS FI1216 */
	 { "PHILIPS_FI1216",                    /* the 'name' */
	   TTYPE_PAL,                           /* input type */
	   { TSBH1_FCONTROL,                    /* control byte for Tuner PLL */
	     TSBH1_FCONTROL,
	     TSBH1_FCONTROL,
	     0x00 },
	   { 0x00, 0x00 },                      /* band-switch crosspoints */
	   { 0x01, 0x02, 0x04, 0x00 } },        /* the band-switch values */
};


/* scaling factor for frequencies expressed as ints */
#define FREQFACTOR		16

/*
 * Format:
 *	entry 0:         MAX legal channel
 *	entry 1:         IF frequency
 *			 expressed as fi{mHz} * 16,
 *			 eg 45.75mHz == 45.75 * 16 = 732
 *	entry 2:         [place holder/future]
 *	entry 3:         base of channel record 0
 *	entry 3 + (x*3): base of channel record 'x'
 *	entry LAST:      NULL channel entry marking end of records
 *
 * Record:
 *	int 0:		base channel
 *	int 1:		frequency of base channel,
 *			 expressed as fb{mHz} * 16,
 *	int 2:		offset frequency between channels,
 *			 expressed as fo{mHz} * 16,
 */

/*
 * North American Broadcast Channels:
 *
 *  2:  55.25 mHz -  4:  67.25 mHz
 *  5:  77.25 mHz -  6:	 83.25 mHz
 *  7: 175.25 mHz - 13:	211.25 mHz
 * 14: 471.25 mHz - 83:	885.25 mHz
 *
 * IF freq: 45.75 mHz
 */
#define OFFSET	6.00
static int nabcst[] = {
	83,	(int)( 45.75 * FREQFACTOR),	0,
	14,	(int)(471.25 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	 7,	(int)(175.25 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	 5,	(int)( 77.25 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	 2,	(int)( 55.25 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	 0
};
#undef OFFSET

/*
 * North American Cable Channels, IRC:
 *
 *  2:  55.25 mHz -  4:  67.25 mHz
 *  5:  77.25 mHz -  6:  83.25 mHz
 *  7: 175.25 mHz - 13: 211.25 mHz
 * 14: 121.25 mHz - 22: 169.25 mHz
 * 23: 217.25 mHz - 94: 643.25 mHz
 * 95:  91.25 mHz - 99: 115.25 mHz
 *
 * IF freq: 45.75 mHz
 */
#define OFFSET	6.00
static int irccable[] = {
	116,    (int)( 45.75 * FREQFACTOR),     0,
	100,    (int)(649.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
	95,	(int)( 91.25 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	23,	(int)(217.25 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	14,	(int)(121.25 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	 7,	(int)(175.25 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	 5,	(int)( 77.25 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	 2,	(int)( 55.25 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	 0
};
#undef OFFSET

/*
 * North American Cable Channels, HRC:
 *
 * 2:   54 mHz  - 4:    66 mHz
 * 5:   78 mHz  - 6:    84 mHz
 * 7:  174 mHz  - 13:  210 mHz
 * 14: 120 mHz  - 22:  168 mHz
 * 23: 216 mHz  - 94:  642 mHz
 * 95:  90 mHz  - 99:  114 mHz
 *
 * IF freq: 45.75 mHz
 */
#define OFFSET  6.00
static int hrccable[] = {
	116,    (int)( 45.75 * FREQFACTOR),     0,
	100,    (int)(648.00 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
	95,	(int)( 90.00 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	23,	(int)(216.00 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	14,	(int)(120.00 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	7,	(int)(174.00 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	5,	(int)( 78.00 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	2,	(int)( 54.00 * FREQFACTOR),	(int)(OFFSET * FREQFACTOR),
	0
};
#undef OFFSET

/*
 * Western European broadcast channels:
 *
 * (there are others that appear to vary between countries - rmt)
 *
 * here's the table Philips provides:
 * caution, some of the offsets don't compute...
 *
 *  1	 4525	700	N21
 * 
 *  2	 4825	700	E2
 *  3	 5525	700	E3
 *  4	 6225	700	E4
 * 
 *  5	17525	700	E5
 *  6	18225	700	E6
 *  7	18925	700	E7
 *  8	19625	700	E8
 *  9	20325	700	E9
 * 10	21025	700	E10
 * 11	21725	700	E11
 * 12	22425	700	E12
 * 
 * 13	 5375	700	ITA
 * 14	 6225	700	ITB
 * 
 * 15	 8225	700	ITC
 * 
 * 16	17525	700	ITD
 * 17	18325	700	ITE
 * 
 * 18	19225	700	ITF
 * 19	20125	700	ITG
 * 20	21025	700	ITH
 * 
 * 21	47125	800	E21
 * 22	47925	800	E22
 * 23	48725	800	E23
 * 24	49525	800	E24
 * 25	50325	800	E25
 * 26	51125	800	E26
 * 27	51925	800	E27
 * 28	52725	800	E28
 * 29	53525	800	E29
 * 30	54325	800	E30
 * 31	55125	800	E31
 * 32	55925	800	E32
 * 33	56725	800	E33
 * 34	57525	800	E34
 * 35	58325	800	E35
 * 36	59125	800	E36
 * 37	59925	800	E37
 * 38	60725	800	E38
 * 39	61525	800	E39
 * 40	62325	800	E40
 * 41	63125	800	E41
 * 42	63925	800	E42
 * 43	64725	800	E43
 * 44	65525	800	E44
 * 45	66325	800	E45
 * 46	67125	800	E46
 * 47	67925	800	E47
 * 48	68725	800	E48
 * 49	69525	800	E49
 * 50	70325	800	E50
 * 51	71125	800	E51
 * 52	71925	800	E52
 * 53	72725	800	E53
 * 54	73525	800	E54
 * 55	74325	800	E55
 * 56	75125	800	E56
 * 57	75925	800	E57
 * 58	76725	800	E58
 * 59	77525	800	E59
 * 60	78325	800	E60
 * 61	79125	800	E61
 * 62	79925	800	E62
 * 63	80725	800	E63
 * 64	81525	800	E64
 * 65	82325	800	E65
 * 66	83125	800	E66
 * 67	83925	800	E67
 * 68	84725	800	E68
 * 69	85525	800	E69
 * 
 * 70	 4575	800	IA
 * 71	 5375	800	IB
 * 72	 6175	800	IC
 * 
 * 74	 6925	700	S01
 * 75	 7625	700	S02
 * 76	 8325	700	S03
 * 
 * 80	10525	700	S1
 * 81	11225	700	S2
 * 82	11925	700	S3
 * 83	12625	700	S4
 * 84	13325	700	S5
 * 85	14025	700	S6
 * 86	14725	700	S7
 * 87	15425	700	S8
 * 88	16125	700	S9
 * 89	16825	700	S10
 * 90	23125	700	S11
 * 91	23825	700	S12
 * 92	24525	700	S13
 * 93	25225	700	S14
 * 94	25925	700	S15
 * 95	26625	700	S16
 * 96	27325	700	S17
 * 97	28025	700	S18
 * 98	28725	700	S19
 * 99	29425	700	S20
 *
 *
 * Channels S21 - S41 are taken from
 * http://gemma.apple.com:80/dev/technotes/tn/tn1012.html
 *
 * 100	30325	800	S21
 * 101	31125	800	S22
 * 102	31925	800	S23
 * 103	32725	800	S24
 * 104	33525	800	S25
 * 105	34325	800	S26         
 * 106	35125	800	S27         
 * 107	35925	800	S28         
 * 108	36725	800	S29         
 * 109	37525	800	S30         
 * 110	38325	800	S31         
 * 111	39125	800	S32         
 * 112	39925	800	S33         
 * 113	40725	800	S34         
 * 114	41525	800	S35         
 * 115	42325	800	S36         
 * 116	43125	800	S37         
 * 117	43925	800	S38         
 * 118	44725	800	S39         
 * 119	45525	800	S40         
 * 120	46325	800	S41
 * 
 * 121	 3890	000	IFFREQ
 * 
 */
static int weurope[] = {
       121,     (int)( 38.90 * FREQFACTOR),     0, 
       100,     (int)(303.25 * FREQFACTOR),     (int)(8.00 * FREQFACTOR), 
        90,     (int)(231.25 * FREQFACTOR),     (int)(7.00 * FREQFACTOR),
        80,     (int)(105.25 * FREQFACTOR),     (int)(7.00 * FREQFACTOR),  
        74,     (int)( 69.25 * FREQFACTOR),     (int)(7.00 * FREQFACTOR),  
        21,     (int)(471.25 * FREQFACTOR),     (int)(8.00 * FREQFACTOR),
        17,     (int)(183.25 * FREQFACTOR),     (int)(9.00 * FREQFACTOR),
        16,     (int)(175.25 * FREQFACTOR),     (int)(9.00 * FREQFACTOR),
        15,     (int)(82.25 * FREQFACTOR),      (int)(8.50 * FREQFACTOR),
        13,     (int)(53.75 * FREQFACTOR),      (int)(8.50 * FREQFACTOR),
         5,     (int)(175.25 * FREQFACTOR),     (int)(7.00 * FREQFACTOR),
         2,     (int)(48.25 * FREQFACTOR),      (int)(7.00 * FREQFACTOR),
	 0
};

/*
 * Japanese Broadcast Channels:
 *
 *  1:  91.25MHz -  3: 103.25MHz
 *  4: 171.25MHz -  7: 189.25MHz
 *  8: 193.25MHz - 12: 217.25MHz  (VHF)
 * 13: 471.25MHz - 62: 765.25MHz  (UHF)
 *
 * IF freq: 58.75 mHz
 */
#define OFFSET  6.00
#define IF_FREQ 58.75
static int jpnbcst[] = {
	62,     (int)(IF_FREQ * FREQFACTOR),    0,
	13,     (int)(471.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
	 8,     (int)(193.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
	 4,     (int)(171.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
	 1,     (int)( 91.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
	 0
};
#undef IF_FREQ
#undef OFFSET

/*
 * Japanese Cable Channels:
 *
 *  1:  91.25MHz -  3: 103.25MHz
 *  4: 171.25MHz -  7: 189.25MHz
 *  8: 193.25MHz - 12: 217.25MHz
 * 13: 109.25MHz - 21: 157.25MHz
 * 22: 165.25MHz
 * 23: 223.25MHz - 63: 463.25MHz
 *
 * IF freq: 58.75 mHz
 */
#define OFFSET  6.00
#define IF_FREQ 58.75
static int jpncable[] = {
	63,     (int)(IF_FREQ * FREQFACTOR),    0,
	23,     (int)(223.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
	22,     (int)(165.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
	13,     (int)(109.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
	 8,     (int)(193.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
	 4,     (int)(171.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
	 1,     (int)( 91.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
	 0
};
#undef IF_FREQ
#undef OFFSET

/*
 * xUSSR Broadcast Channels:
 *
 *  1:  49.75MHz -  2:  59.25MHz
 *  3:  77.25MHz -  5:  93.25MHz
 *  6: 175.25MHz - 12: 223.25MHz
 * 13-20 - not exist
 * 21: 471.25MHz - 34: 575.25MHz
 * 35: 583.25MHz - 69: 855.25MHz
 *
 * Cable channels
 *
 * 70: 111.25MHz - 77: 167.25MHz
 * 78: 231.25MHz -107: 463.25MHz
 *
 * IF freq: 38.90 MHz
 */
#define IF_FREQ 38.90
static int xussr[] = {
      107,     (int)(IF_FREQ * FREQFACTOR),    0,
       78,     (int)(231.25 * FREQFACTOR),     (int)(8.00 * FREQFACTOR), 
       70,     (int)(111.25 * FREQFACTOR),     (int)(8.00 * FREQFACTOR),
       35,     (int)(583.25 * FREQFACTOR),     (int)(8.00 * FREQFACTOR), 
       21,     (int)(471.25 * FREQFACTOR),     (int)(8.00 * FREQFACTOR),
        6,     (int)(175.25 * FREQFACTOR),     (int)(8.00 * FREQFACTOR),  
        3,     (int)( 77.25 * FREQFACTOR),     (int)(8.00 * FREQFACTOR),  
        1,     (int)( 49.75 * FREQFACTOR),     (int)(9.50 * FREQFACTOR),
        0
};
#undef IF_FREQ

/*
 * Australian broadcast channels
 */
#define OFFSET	7.00
#define IF_FREQ 38.90 
static int australia[] = {
       83,     (int)(IF_FREQ * FREQFACTOR),    0,
       28,     (int)(527.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
       10,     (int)(209.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
        6,     (int)(175.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
        4,     (int)( 95.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
        3,     (int)( 86.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
        1,     (int)( 57.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR),
        0
};
#undef OFFSET
#undef IF_FREQ

/* 
 * France broadcast channels
 */
#define OFFSET 8.00
#define IF_FREQ 38.90
static int france[] = {
        69,     (int)(IF_FREQ * FREQFACTOR),     0,
        21,     (int)(471.25 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR), /* 21 -> 69 */
         5,     (int)(176.00 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR), /* 5 -> 10 */
         4,     (int)( 63.75 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR), /* 4    */
         3,     (int)( 60.50 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR), /* 3    */
         1,     (int)( 47.75 * FREQFACTOR),     (int)(OFFSET * FREQFACTOR), /* 1  2 */
         0
}; 
#undef OFFSET
#undef IF_FREQ

static struct {
        int     *ptr;
        char    name[BT848_MAX_CHNLSET_NAME_LEN];
} freqTable[] = {
        {NULL,          ""},
        {nabcst,        "nabcst"},
        {irccable,      "cableirc"},
        {hrccable,      "cablehrc"},
        {weurope,       "weurope"},
        {jpnbcst,       "jpnbcst"},
        {jpncable,      "jpncable"},
        {xussr,         "xussr"},
        {australia,     "australia"},
        {france,        "france"},
 
};

#define TBL_CHNL	freqTable[ bktr->tuner.chnlset ].ptr[ x ]
#define TBL_BASE_FREQ	freqTable[ bktr->tuner.chnlset ].ptr[ x + 1 ]
#define TBL_OFFSET	freqTable[ bktr->tuner.chnlset ].ptr[ x + 2 ]
static int
frequency_lookup( bktr_ptr_t bktr, int channel )
{
	int	x;

	/* check for "> MAX channel" */
	x = 0;
	if ( channel > TBL_CHNL )
		return( -1 );

	/* search the table for data */
	for ( x = 3; TBL_CHNL; x += 3 ) {
		if ( channel >= TBL_CHNL ) {
			return( TBL_BASE_FREQ +
				 ((channel - TBL_CHNL) * TBL_OFFSET) );
		}
	}

	/* not found, must be below the MIN channel */
	return( -1 );
}
#undef TBL_OFFSET
#undef TBL_BASE_FREQ
#undef TBL_CHNL


#define	TBL_IF	(bktr->format_params == BT848_IFORM_F_NTSCJ || \
                 bktr->format_params == BT848_IFORM_F_NTSCM ? \
                 nabcst[1] : weurope[1])


/* Initialise the tuner structures in the bktr_softc */
/* This is needed as the tuner details are no longer globally declared */

void    select_tuner( bktr_ptr_t bktr, int tuner_type ) {
	if (tuner_type < Bt848_MAX_TUNER) {
		bktr->card.tuner = &tuners[ tuner_type ];
	} else {
		bktr->card.tuner = NULL;
	}
}

/*
 * Tuner Notes:
 * Programming the tuner properly is quite complicated.
 * Here are some notes, based on a FM1246 data sheet for a PAL-I tuner.
 * The tuner (front end) covers 45.75 Mhz - 855.25 Mhz and an FM band of
 * 87.5 Mhz to 108.0 Mhz.
 *
 * RF and IF.  RF = radio frequencies, it is the transmitted signal.
 *             IF is the Intermediate Frequency (the offset from the base
 *             signal where the video, color,  audio and NICAM signals are.
 *
 * Eg, Picture at 38.9 Mhz, Colour at 34.47 MHz, sound at 32.9 MHz
 * NICAM at 32.348 Mhz.
 * Strangely enough, there is an IF (intermediate frequency) for
 * FM Radio which is 10.7 Mhz.
 *
 * The tuner also works in Bands. Philips bands are
 * FM radio band 87.50 to 108.00 MHz
 * Low band 45.75 to 170.00 MHz
 * Mid band 170.00 to 450.00 MHz
 * High band 450.00 to 855.25 MHz
 *
 *
 * Now we need to set the PLL on the tuner to the required freuqncy.
 * It has a programmable divisor.
 * For TV we want
 *  N = 16 (freq RF(pc) + freq IF(pc))  pc is picture carrier and RF and IF
 *  are in MHz.

 * For RADIO we want a different equation.
 *  freq IF is 10.70 MHz (so the data sheet tells me)
 * N = (freq RF + freq IF) / step size
 * The step size must be set to 50 khz (so the data sheet tells me)
 * (note this is 50 kHz, the other things are in MHz)
 * so we end up with N = 20x(freq RF + 10.7)
 *
 */

#define LOW_BAND 0
#define MID_BAND 1
#define HIGH_BAND 2
#define FM_RADIO_BAND 3


/* Check if these are correct for other than Philips PAL */
#define STATUSBIT_COLD   0x80
#define STATUSBIT_LOCK   0x40
#define STATUSBIT_TV     0x20
#define STATUSBIT_STEREO 0x10 /* valid if FM (aka not TV) */
#define STATUSBIT_ADC    0x07

/*
 * set the frequency of the tuner
 * If 'type' is TV_FREQUENCY, the frequency is freq MHz*16
 * If 'type' is FM_RADIO_FREQUENCY, the frequency is freq MHz * 100 
 * (note *16 gives is 4 bits of fraction, eg steps of nnn.0625)
 *
 */
int
tv_freq( bktr_ptr_t bktr, int frequency, int type )
{
	const struct TUNER*	tuner;
	u_char			addr;
	u_char			control;
	u_char			band;
	int			N;
	int			band_select = 0;
#if defined( TEST_TUNER_AFC )
	int			oldFrequency, afcDelta;
#endif

	tuner = bktr->card.tuner;
	if ( tuner == NULL )
		return( -1 );

	if (tuner == &tuners[TUNER_MT2032]) {
		mt2032_set_tv_freq(bktr, frequency);
		return 0;
	}
	if (type == TV_FREQUENCY) {
		/*
		 * select the band based on frequency
		 * XXX FIXME: get the cross-over points from the tuner struct
		 */
		if ( frequency < (160 * FREQFACTOR  ) )
		    band_select = LOW_BAND;
		else if ( frequency < (454 * FREQFACTOR ) )
		    band_select = MID_BAND;
		else
		    band_select = HIGH_BAND;

#if defined( TEST_TUNER_AFC )
		if ( bktr->tuner.afc )
			frequency -= 4;
#endif
		/*
		 * N = 16 * { fRF(pc) + fIF(pc) }
		 * or N = 16* fRF(pc) + 16*fIF(pc) }
		 * where:
		 *  pc is picture carrier, fRF & fIF are in MHz
		 *
		 * fortunatly, frequency is passed in as MHz * 16
		 * and the TBL_IF frequency is also stored in MHz * 16
		 */
		N = frequency + TBL_IF;

		/* set the address of the PLL */
		addr    = bktr->card.tuner_pllAddr;
		control = tuner->pllControl[ band_select ];
		band    = tuner->bandAddrs[ band_select ];

		if(!(band && control))		/* Don't try to set un-	*/
		  return(-1);			/* supported modes.	*/

		if ( frequency > bktr->tuner.frequency ) {
			i2cWrite( bktr, addr, (N>>8) & 0x7f, N & 0xff );
			i2cWrite( bktr, addr, control, band );
	        }
	        else {
			i2cWrite( bktr, addr, control, band );
			i2cWrite( bktr, addr, (N>>8) & 0x7f, N & 0xff );
       		}

#if defined( TUNER_AFC )
		if ( bktr->tuner.afc == TRUE ) {
#if defined( TEST_TUNER_AFC )
			oldFrequency = frequency;
#endif
			if ( (N = do_afc( bktr, addr, N )) < 0 ) {
			    /* AFC failed, restore requested frequency */
			    N = frequency + TBL_IF;
#if defined( TEST_TUNER_AFC )
			    printf("%s: do_afc: failed to lock\n",
				   bktr_name(bktr));
#endif
			    i2cWrite( bktr, addr, (N>>8) & 0x7f, N & 0xff );
			}
			else
			    frequency = N - TBL_IF;
#if defined( TEST_TUNER_AFC )
 printf("%s: do_afc: returned freq %d (%d %% %d)\n", bktr_name(bktr), frequency, frequency / 16, frequency % 16);
			    afcDelta = frequency - oldFrequency;
 printf("%s: changed by: %d clicks (%d mod %d)\n", bktr_name(bktr), afcDelta, afcDelta / 16, afcDelta % 16);
#endif
			}
#endif /* TUNER_AFC */

		bktr->tuner.frequency = frequency;
	}

	if ( type == FM_RADIO_FREQUENCY ) {
		band_select = FM_RADIO_BAND;

		/*
		 * N = { fRF(pc) + fIF(pc) }/step_size
                 * The step size is 50kHz for FM radio.
		 * (eg after 102.35MHz comes 102.40 MHz)
		 * fIF is 10.7 MHz (as detailed in the specs)
		 *
		 * frequency is passed in as MHz * 100
		 *
		 * So, we have N = (frequency/100 + 10.70)  /(50/1000)
		 */
		N = (frequency + 1070)/5;

		/* set the address of the PLL */
		addr    = bktr->card.tuner_pllAddr;
		control = tuner->pllControl[ band_select ];
		band    = tuner->bandAddrs[ band_select ];

		if(!(band && control))		/* Don't try to set un-	*/
		  return(-1);			/* supported modes.	*/
	  
		band |= bktr->tuner.radio_mode; /* tuner.radio_mode is set in
						 * the ioctls RADIO_SETMODE
						 * and RADIO_GETMODE */

		i2cWrite( bktr, addr, control, band );
		i2cWrite( bktr, addr, (N>>8) & 0x7f, N & 0xff );

		bktr->tuner.frequency = (N * 5) - 1070;


	}
 

	return( 0 );
}



#if defined( TUNER_AFC )
/*
 * 
 */
int
do_afc( bktr_ptr_t bktr, int addr, int frequency )
{
	int step;
	int status;
	int origFrequency;

	origFrequency = frequency;

	/* wait for first setting to take effect */
	tsleep( BKTR_SLEEP, PZERO, "tuning", hz/8 );

	if ( (status = i2cRead( bktr, addr + 1 )) < 0 )
		return( -1 );

#if defined( TEST_TUNER_AFC )
 printf( "%s: Original freq: %d, status: 0x%02x\n", bktr_name(bktr), frequency, status );
#endif
	for ( step = 0; step < AFC_MAX_STEP; ++step ) {
		if ( (status = i2cRead( bktr, addr + 1 )) < 0 )
			goto fubar;
		if ( !(status & 0x40) ) {
#if defined( TEST_TUNER_AFC )
 printf( "%s: no lock!\n", bktr_name(bktr) );
#endif
			goto fubar;
		}

		switch( status & AFC_BITS ) {
		case AFC_FREQ_CENTERED:
#if defined( TEST_TUNER_AFC )
 printf( "%s: Centered, freq: %d, status: 0x%02x\n", bktr_name(bktr), frequency, status );
#endif
			return( frequency );

		case AFC_FREQ_MINUS_125:
		case AFC_FREQ_MINUS_62:
#if defined( TEST_TUNER_AFC )
 printf( "%s: Low, freq: %d, status: 0x%02x\n", bktr_name(bktr), frequency, status );
#endif
			--frequency;
			break;

		case AFC_FREQ_PLUS_62:
		case AFC_FREQ_PLUS_125:
#if defined( TEST_TUNER_AFC )
 printf( "%s: Hi, freq: %d, status: 0x%02x\n", bktr_name(bktr), frequency, status );
#endif
			++frequency;
			break;
		}

		i2cWrite( bktr, addr,
			  (frequency>>8) & 0x7f, frequency & 0xff );
		DELAY( AFC_DELAY );
	}

 fubar:
	i2cWrite( bktr, addr,
		  (origFrequency>>8) & 0x7f, origFrequency & 0xff );

	return( -1 );
}
#endif /* TUNER_AFC */
#undef TBL_IF


/*
 * Get the Tuner status and signal strength
 */
int     get_tuner_status( bktr_ptr_t bktr ) {
	if (bktr->card.tuner == &tuners[TUNER_MT2032])
		return 0;
	return i2cRead( bktr, bktr->card.tuner_pllAddr + 1 );
}

/*
 * set the channel of the tuner
 */
int
tv_channel( bktr_ptr_t bktr, int channel )
{
	int frequency;

	/* calculate the frequency according to tuner type */
	if ( (frequency = frequency_lookup( bktr, channel )) < 0 )
		return( -1 );

	/* set the new frequency */
	if ( tv_freq( bktr, frequency, TV_FREQUENCY ) < 0 )
		return( -1 );

	/* OK to update records */
	return( (bktr->tuner.channel = channel) );
}

/*
 * get channelset name
 */
int
tuner_getchnlset(struct bktr_chnlset *chnlset)
{
       if (( chnlset->index < CHNLSET_MIN ) ||
               ( chnlset->index > CHNLSET_MAX ))
                       return( EINVAL );

       memcpy(&chnlset->name, &freqTable[chnlset->index].name,
               BT848_MAX_CHNLSET_NAME_LEN);

       chnlset->max_channel=freqTable[chnlset->index].ptr[0];
       return( 0 );
}




#define	TDA9887_ADDR	0x86

static int
TDA9887_init(bktr_ptr_t bktr, int output2_enable)
{
	u_char addr = TDA9887_ADDR;

	i2cWrite(bktr, addr, 0, output2_enable ? 0x50 : 0xd0);
	i2cWrite(bktr, addr, 1, 0x6e); /* takeover point / de-emphasis */

	/* PAL BG: 0x09  PAL I: 0x0a  NTSC: 0x04 */
#ifdef MT2032_NTSC
	i2cWrite(bktr, addr, 2, 0x04);
#else
	i2cWrite(bktr, addr, 2, 0x09);
#endif
	return 0;
}



#define MT2032_OPTIMIZE_VCO	 1

/* holds the value of XOGC register after init */
static int      MT2032_XOGC = 4;

/* card.tuner_pllAddr not set during init */
#define	MT2032_ADDR		0xc0

#ifndef MT2032_ADDR
#define	MT2032_ADDR		(bktr->card.tuner_pllAddr)
#endif

static int 
_MT2032_GetRegister(bktr_ptr_t bktr, u_char regNum)
{
	int		ch;

	if (i2cWrite(bktr, MT2032_ADDR, regNum, -1) == -1) {
		if (bootverbose)
			printf("%s: MT2032 write failed (i2c addr %#x)\n",
				bktr_name(bktr), MT2032_ADDR);
		return -1;
	}
	if ((ch = i2cRead(bktr, MT2032_ADDR + 1)) == -1) {
		if (bootverbose)
			printf("%s: MT2032 get register %d failed\n",
				bktr_name(bktr), regNum);
		return -1;
	}
	return ch;
}

static void 
_MT2032_SetRegister(bktr_ptr_t bktr, u_char regNum, u_char data)
{
	i2cWrite(bktr, MT2032_ADDR, regNum, data);
}

#define	MT2032_GetRegister(r)		_MT2032_GetRegister(bktr,r)
#define	MT2032_SetRegister(r,d)		_MT2032_SetRegister(bktr,r,d)


int 
mt2032_init(bktr_ptr_t bktr)
{
	u_char            rdbuf[22];
	int             xogc, xok = 0;
	int             i;
	int		x;

	TDA9887_init(bktr, 0);

	for (i = 0; i < 21; i++) {
		if ((x = MT2032_GetRegister(i)) == -1)
			break;
		rdbuf[i] = x;
	}
	if (i < 21)
		return -1;

	printf("%s: MT2032: Companycode=%02x%02x Part=%02x Revision=%02x\n",
		bktr_name(bktr),
		rdbuf[0x11], rdbuf[0x12], rdbuf[0x13], rdbuf[0x14]);
	if (rdbuf[0x13] != 4) {
		printf("%s: MT2032 not found or unknown type\n", bktr_name(bktr));
		return -1;
	}

	/* Initialize Registers per spec. */
	MT2032_SetRegister(2, 0xff);
	MT2032_SetRegister(3, 0x0f);
	MT2032_SetRegister(4, 0x1f);
	MT2032_SetRegister(6, 0xe4);
	MT2032_SetRegister(7, 0x8f);
	MT2032_SetRegister(8, 0xc3);
	MT2032_SetRegister(9, 0x4e);
	MT2032_SetRegister(10, 0xec);
	MT2032_SetRegister(13, 0x32);

	/* Adjust XOGC (register 7), wait for XOK */
	xogc = 7;
	do {
		DELAY(10000);
		xok = MT2032_GetRegister(0x0e) & 0x01;
		if (xok == 1) {
			break;
		}
		xogc--;
		if (xogc == 3) {
			xogc = 4;	/* min. 4 per spec */
			break;
		}
		MT2032_SetRegister(7, 0x88 + xogc);
	} while (xok != 1);

	TDA9887_init(bktr, 1);

	MT2032_XOGC = xogc;

	return 0;
}

static int 
MT2032_SpurCheck(int f1, int f2, int spectrum_from, int spectrum_to)
{
	int             n1 = 1, n2, f;

	f1 = f1 / 1000;		/* scale to kHz to avoid 32bit overflows */
	f2 = f2 / 1000;
	spectrum_from /= 1000;
	spectrum_to /= 1000;

	do {
		n2 = -n1;
		f = n1 * (f1 - f2);
		do {
			n2--;
			f = f - f2;
			if ((f > spectrum_from) && (f < spectrum_to)) {
				return 1;
			}
		} while ((f > (f2 - spectrum_to)) || (n2 > -5));
		n1++;
	} while (n1 < 5);

	return 0;
}

static int
MT2032_ComputeFreq(
		   int rfin,
		   int if1,
		   int if2,
		   int spectrum_from,
		   int spectrum_to,
		   unsigned char *buf,
		   int *ret_sel,
		   int xogc
)
{				/* all in Hz */
	int             fref, lo1, lo1n, lo1a, s, sel;
	int             lo1freq, desired_lo1, desired_lo2, lo2, lo2n, lo2a,
	                lo2num, lo2freq;
	int             nLO1adjust;

	fref = 5250 * 1000;	/* 5.25MHz */

	/* per spec 2.3.1 */
	desired_lo1 = rfin + if1;
	lo1 = (2 * (desired_lo1 / 1000) + (fref / 1000)) / (2 * fref / 1000);
	lo1freq = lo1 * fref;
	desired_lo2 = lo1freq - rfin - if2;

	/* per spec 2.3.2 */
	for (nLO1adjust = 1; nLO1adjust < 3; nLO1adjust++) {
		if (!MT2032_SpurCheck(lo1freq, desired_lo2, spectrum_from, spectrum_to)) {
			break;
		}
		if (lo1freq < desired_lo1) {
			lo1 += nLO1adjust;
		} else {
			lo1 -= nLO1adjust;
		}

		lo1freq = lo1 * fref;
		desired_lo2 = lo1freq - rfin - if2;
	}

	/* per spec 2.3.3 */
	s = lo1freq / 1000 / 1000;

	if (MT2032_OPTIMIZE_VCO) {
		if (s > 1890) {
			sel = 0;
		} else if (s > 1720) {
			sel = 1;
		} else if (s > 1530) {
			sel = 2;
		} else if (s > 1370) {
			sel = 3;
		} else {
			sel = 4;/* >1090 */
		}
	} else {
		if (s > 1790) {
			sel = 0;/* <1958 */
		} else if (s > 1617) {
			sel = 1;
		} else if (s > 1449) {
			sel = 2;
		} else if (s > 1291) {
			sel = 3;
		} else {
			sel = 4;/* >1090 */
		}
	}

	*ret_sel = sel;

	/* per spec 2.3.4 */
	lo1n = lo1 / 8;
	lo1a = lo1 - (lo1n * 8);
	lo2 = desired_lo2 / fref;
	lo2n = lo2 / 8;
	lo2a = lo2 - (lo2n * 8);
	/* scale to fit in 32bit arith */
	lo2num = ((desired_lo2 / 1000) % (fref / 1000)) * 3780 / (fref / 1000);
	lo2freq = (lo2a + 8 * lo2n) * fref + lo2num * (fref / 1000) / 3780 * 1000;

	if (lo1a < 0 || lo1a > 7 || lo1n < 17 || lo1n > 48 || lo2a < 0 ||
	    lo2a > 7 || lo2n < 17 || lo2n > 30) {
		printf("MT2032: parameter out of range\n");
		return -1;
	}
	/* set up MT2032 register map for transfer over i2c */
	buf[0] = lo1n - 1;
	buf[1] = lo1a | (sel << 4);
	buf[2] = 0x86;		/* LOGC */
	buf[3] = 0x0f;		/* reserved */
	buf[4] = 0x1f;
	buf[5] = (lo2n - 1) | (lo2a << 5);
	if (rfin < 400 * 1000 * 1000) {
		buf[6] = 0xe4;
	} else {
		buf[6] = 0xf4;	/* set PKEN per rev 1.2 */
	}

	buf[7] = 8 + xogc;
	buf[8] = 0xc3;		/* reserved */
	buf[9] = 0x4e;		/* reserved */
	buf[10] = 0xec;		/* reserved */
	buf[11] = (lo2num & 0xff);
	buf[12] = (lo2num >> 8) | 0x80;	/* Lo2RST */

	return 0;
}

static int 
MT2032_CheckLOLock(bktr_ptr_t bktr)
{
	int             t, lock = 0;
	for (t = 0; t < 10; t++) {
		lock = MT2032_GetRegister(0x0e) & 0x06;
		if (lock == 6) {
			break;
		}
		DELAY(1000);
	}
	return lock;
}

static int 
MT2032_OptimizeVCO(bktr_ptr_t bktr, int sel, int lock)
{
	int             tad1, lo1a;

	tad1 = MT2032_GetRegister(0x0f) & 0x07;

	if (tad1 == 0) {
		return lock;
	}
	if (tad1 == 1) {
		return lock;
	}
	if (tad1 == 2) {
		if (sel == 0) {
			return lock;
		} else {
			sel--;
		}
	} else {
		if (sel < 4) {
			sel++;
		} else {
			return lock;
		}
	}
	lo1a = MT2032_GetRegister(0x01) & 0x07;
	MT2032_SetRegister(0x01, lo1a | (sel << 4));
	lock = MT2032_CheckLOLock(bktr);
	return lock;
}

static int
MT2032_SetIFFreq(bktr_ptr_t bktr, int rfin, int if1, int if2, int from, int to)
{
	u_char          buf[21];
	int             lint_try, sel, lock = 0;

	if (MT2032_ComputeFreq(rfin, if1, if2, from, to, &buf[0], &sel, MT2032_XOGC) == -1)
		return -1;

	TDA9887_init(bktr, 0);

	/* send only the relevant registers per Rev. 1.2 */
	MT2032_SetRegister(0, buf[0x00]);
	MT2032_SetRegister(1, buf[0x01]);
	MT2032_SetRegister(2, buf[0x02]);

	MT2032_SetRegister(5, buf[0x05]);
	MT2032_SetRegister(6, buf[0x06]);
	MT2032_SetRegister(7, buf[0x07]);

	MT2032_SetRegister(11, buf[0x0B]);
	MT2032_SetRegister(12, buf[0x0C]);

	/* wait for PLLs to lock (per manual), retry LINT if not. */
	for (lint_try = 0; lint_try < 2; lint_try++) {
		lock = MT2032_CheckLOLock(bktr);

		if (MT2032_OPTIMIZE_VCO) {
			lock = MT2032_OptimizeVCO(bktr, sel, lock);
		}
		if (lock == 6) {
			break;
		}
		/* set LINT to re-init PLLs */
		MT2032_SetRegister(7, 0x80 + 8 + MT2032_XOGC);
		DELAY(10000);
		MT2032_SetRegister(7, 8 + MT2032_XOGC);
	}
	if (lock != 6)
		printf("%s: PLL didn't lock\n", bktr_name(bktr));

	MT2032_SetRegister(2, 0x20);

	TDA9887_init(bktr, 1);
	return 0;
}

static void
mt2032_set_tv_freq(bktr_ptr_t bktr, unsigned int freq)
{
	int if2,from,to;
	int stat, tad;

#ifdef MT2032_NTSC
	from=40750*1000;
	to=46750*1000;
	if2=45750*1000;
#else
	from=32900*1000;
	to=39900*1000;
	if2=38900*1000;
#endif

	if (MT2032_SetIFFreq(bktr, freq*62500 /* freq*1000*1000/16 */,
			1090*1000*1000, if2, from, to) == 0) {
		bktr->tuner.frequency = freq;
		stat = MT2032_GetRegister(0x0e);
		tad = MT2032_GetRegister(0x0f);
		if (bootverbose)
			printf("%s: frequency set to %d, st = %#x, tad = %#x\n",
				bktr_name(bktr), freq*62500, stat, tad);
	}
}
