/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997-2001 Gerd Knorr <kraxel@bytesex.org>
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
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * programming the msp34* sound processor family
 *
 * (c) 1997-2001 Gerd Knorr <kraxel@bytesex.org>
 *
 * what works and what doesn't:
 *
 *  AM-Mono
 *      Support for Hauppauge cards added (decoding handled by tuner) added by
 *      Frederic Crozat <fcrozat@mail.dotcom.fr>
 *
 *  FM-Mono
 *      should work. The stereo modes are backward compatible to FM-mono,
 *      therefore FM-Mono should be always available.
 *
 *  FM-Stereo (B/G, used in germany)
 *      should work, with autodetect
 *
 *  FM-Stereo (satellite)
 *      should work, no autodetect (i.e. default is mono, but you can
 *      switch to stereo -- untested)
 *
 *  NICAM (B/G, L , used in UK, Scandinavia, Spain and France)
 *      should work, with autodetect. Support for NICAM was added by
 *      Pekka Pietikainen <pp@netppl.fi>
 *
 *
 * TODO:
 *   - better SAT support
 *
 *
 * 980623  Thomas Sailer (sailer@ife.ee.ethz.ch)
 *         using soundcore instead of OSS
 *
 *
 * The FreeBSD modifications by Alexander Langer <alex@FreeBSD.org>
 * are in the public domain.  Please contact me (Alex) and not Gerd for
 * any problems you encounter under FreeBSD.
 * 
 * FreeBSD TODO: 
 * - mutex handling (currently not mp-safe)
 * - the various options here as loader tunables or compile time or whatever
 * - how does the new dolby flag work with the current dpl_* stuff?
 *   Maybe it's just enough to set the dolby flag to 1 and it works.
 *   As I don't have a dolby card myself, I can't test it, though.
 */

#include "opt_bktr.h"			/* Include any kernel config options */
#ifdef BKTR_NEW_MSP34XX_DRIVER		/* file only needed for new driver */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/vnode.h>

#include <sys/unistd.h>
#include <sys/kthread.h>
#include <sys/malloc.h>

#ifdef BKTR_USE_FREEBSD_SMBUS
#include <sys/bus.h>			/* required by bktr_reg.h */
#endif

#include <machine/bus.h>		/* required by bktr_reg.h */

#include <dev/bktr/ioctl_meteor.h>
#include <dev/bktr/ioctl_bt848.h>	/* extensions to ioctl_meteor.h */
#include <dev/bktr/bktr_reg.h>
#include <dev/bktr/bktr_tuner.h>
#include <dev/bktr/bktr_audio.h>
#include <dev/bktr/bktr_core.h>

#define VIDEO_MODE_PAL          0
#define VIDEO_MODE_NTSC         1
#define VIDEO_MODE_SECAM        2
#define VIDEO_MODE_AUTO         3

#define VIDEO_SOUND_MONO	1
#define VIDEO_SOUND_STEREO	2
#define VIDEO_SOUND_LANG1	4
#define VIDEO_SOUND_LANG2	8

#define DFP_COUNT 0x41

struct msp3400c {
	int simple;
	int nicam;
	int mode;
	int norm;
	int stereo;
	int nicam_on;
	int acb;
	int main, second;	/* sound carrier */
	int input;

	int muted;
	int left, right;	/* volume */
	int bass, treble;

	/* shadow register set */
	int dfp_regs[DFP_COUNT];

	/* thread */
	struct proc	    *kthread;

	int                  active,restart,rmmod;

	int                  watch_stereo;
	int		     halt_thread;
};

#define VIDEO_MODE_RADIO 16      /* norm magic for radio mode */

/* ---------------------------------------------------------------------- */

#define dprintk(...) do {						\
	if (bootverbose) {						\
		printf("%s: ", bktr_name(client));			\
		printf(__VA_ARGS__);					\
	}								\
} while (0)

/* ---------------------------------------------------------------------- */

#define I2C_MSP3400C       0x80
#define I2C_MSP3400C_DEM   0x10
#define I2C_MSP3400C_DFP   0x12

/* ----------------------------------------------------------------------- */
/* functions for talking to the MSP3400C Sound processor                   */

static int msp3400c_reset(bktr_ptr_t client)
{
	/* use our own which handles config(8) options */
	msp_dpl_reset(client, client->msp_addr);

        return 0;
}

static int
msp3400c_read(bktr_ptr_t client, int dev, int addr)
{
	/* use our own */
	return(msp_dpl_read(client, client->msp_addr, dev, addr));
}

static int
msp3400c_write(bktr_ptr_t client, int dev, int addr, int val)
{
	/* use our own */
	msp_dpl_write(client, client->msp_addr, dev, addr, val);

	return(0);
}

/* ------------------------------------------------------------------------ */

/* This macro is allowed for *constants* only, gcc must calculate it
   at compile time.  Remember -- no floats in kernel mode */
#define MSP_CARRIER(freq) ((int)((float)(freq/18.432)*(1<<24)))

#define MSP_MODE_AM_DETECT   0
#define MSP_MODE_FM_RADIO    2
#define MSP_MODE_FM_TERRA    3
#define MSP_MODE_FM_SAT      4
#define MSP_MODE_FM_NICAM1   5
#define MSP_MODE_FM_NICAM2   6
#define MSP_MODE_AM_NICAM    7
#define MSP_MODE_BTSC        8
#define MSP_MODE_EXTERN      9

static struct MSP_INIT_DATA_DEM {
	int fir1[6];
	int fir2[6];
	int cdo1;
	int cdo2;
	int ad_cv;
	int mode_reg;
	int dfp_src;
	int dfp_matrix;
} msp_init_data[] = {
	/* AM (for carrier detect / msp3400) */
	{ { 75, 19, 36, 35, 39, 40 }, { 75, 19, 36, 35, 39, 40 },
	  MSP_CARRIER(5.5), MSP_CARRIER(5.5),
	  0x00d0, 0x0500,   0x0020, 0x3000},

	/* AM (for carrier detect / msp3410) */
	{ { -1, -1, -8, 2, 59, 126 }, { -1, -1, -8, 2, 59, 126 },
	  MSP_CARRIER(5.5), MSP_CARRIER(5.5),
	  0x00d0, 0x0100,   0x0020, 0x3000},

	/* FM Radio */
	{ { -8, -8, 4, 6, 78, 107 }, { -8, -8, 4, 6, 78, 107 },
	  MSP_CARRIER(10.7), MSP_CARRIER(10.7),
	  0x00d0, 0x0480, 0x0020, 0x3000 },

	/* Terrestrial FM-mono + FM-stereo */
	{ {  3, 18, 27, 48, 66, 72 }, {  3, 18, 27, 48, 66, 72 },
	  MSP_CARRIER(5.5), MSP_CARRIER(5.5),
	  0x00d0, 0x0480,   0x0030, 0x3000},

	/* Sat FM-mono */
	{ {  1,  9, 14, 24, 33, 37 }, {  3, 18, 27, 48, 66, 72 },
	  MSP_CARRIER(6.5), MSP_CARRIER(6.5),
	  0x00c6, 0x0480,   0x0000, 0x3000},

	/* NICAM/FM --  B/G (5.5/5.85), D/K (6.5/5.85) */
	{ { -2, -8, -10, 10, 50, 86 }, {  3, 18, 27, 48, 66, 72 },
	  MSP_CARRIER(5.5), MSP_CARRIER(5.5),
	  0x00d0, 0x0040,   0x0120, 0x3000},

	/* NICAM/FM -- I (6.0/6.552) */
	{ {  2, 4, -6, -4, 40, 94 }, {  3, 18, 27, 48, 66, 72 },
	  MSP_CARRIER(6.0), MSP_CARRIER(6.0),
	  0x00d0, 0x0040,   0x0120, 0x3000},

	/* NICAM/AM -- L (6.5/5.85) */
	{ {  -2, -8, -10, 10, 50, 86 }, {  -4, -12, -9, 23, 79, 126 },
	  MSP_CARRIER(6.5), MSP_CARRIER(6.5),
	  0x00c6, 0x0140,   0x0120, 0x7c03},
};

struct CARRIER_DETECT {
	int   cdo;
	char *name;
};

static struct CARRIER_DETECT carrier_detect_main[] = {
	/* main carrier */
	{ MSP_CARRIER(4.5),        "4.5   NTSC"                   }, 
	{ MSP_CARRIER(5.5),        "5.5   PAL B/G"                }, 
	{ MSP_CARRIER(6.0),        "6.0   PAL I"                  },
	{ MSP_CARRIER(6.5),        "6.5   PAL D/K + SAT + SECAM"  }
};

static struct CARRIER_DETECT carrier_detect_55[] = {
	/* PAL B/G */
	{ MSP_CARRIER(5.7421875),  "5.742 PAL B/G FM-stereo"     }, 
	{ MSP_CARRIER(5.85),       "5.85  PAL B/G NICAM"         }
};

static struct CARRIER_DETECT carrier_detect_65[] = {
	/* PAL SAT / SECAM */
	{ MSP_CARRIER(5.85),       "5.85  PAL D/K + SECAM NICAM" },
	{ MSP_CARRIER(6.2578125),  "6.25  PAL D/K1 FM-stereo" },
	{ MSP_CARRIER(6.7421875),  "6.74  PAL D/K2 FM-stereo" },
	{ MSP_CARRIER(7.02),       "7.02  PAL SAT FM-stereo s/b" },
	{ MSP_CARRIER(7.20),       "7.20  PAL SAT FM-stereo s"   },
	{ MSP_CARRIER(7.38),       "7.38  PAL SAT FM-stereo b"   },
};

#define CARRIER_COUNT(x) (sizeof(x)/sizeof(struct CARRIER_DETECT))

/* ----------------------------------------------------------------------- */

#define SCART_MASK    0
#define SCART_IN1     1
#define SCART_IN2     2
#define SCART_IN1_DA  3
#define SCART_IN2_DA  4
#define SCART_IN3     5
#define SCART_IN4     6
#define SCART_MONO    7
#define SCART_MUTE    8

static int scarts[3][9] = {
  /* MASK    IN1     IN2     IN1_DA  IN2_DA  IN3     IN4     MONO    MUTE   */
  {  0x0320, 0x0000, 0x0200, -1,     -1,     0x0300, 0x0020, 0x0100, 0x0320 },
  {  0x0c40, 0x0440, 0x0400, 0x0c00, 0x0040, 0x0000, 0x0840, 0x0800, 0x0c40 },
  {  0x3080, 0x1000, 0x1080, 0x0000, 0x0080, 0x2080, 0x3080, 0x2000, 0x3000 },
};

static char *scart_names[] = {
  "mask", "in1", "in2", "in1 da", "in2 da", "in3", "in4", "mono", "mute"
};

static void
msp3400c_set_scart(bktr_ptr_t client, int in, int out)
{
	struct msp3400c *msp = client->msp3400c_info;

	if (-1 == scarts[out][in])
		return;

	dprintk("msp34xx: scart switch: %s => %d\n",scart_names[in],out);
	msp->acb &= ~scarts[out][SCART_MASK];
	msp->acb |=  scarts[out][in];
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x0013, msp->acb);
}

/* ------------------------------------------------------------------------ */

static void msp3400c_setcarrier(bktr_ptr_t client, int cdo1, int cdo2)
{
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x0093, cdo1 & 0xfff);
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x009b, cdo1 >> 12);
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x00a3, cdo2 & 0xfff);
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x00ab, cdo2 >> 12);
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x0056, 0); /*LOAD_REG_1/2*/
}

static void msp3400c_setvolume(bktr_ptr_t client,
			       int muted, int left, int right)
{
	int vol = 0,val = 0,balance = 0;

	if (!muted) {
		vol     = (left > right) ? left : right;
		val     = (vol * 0x73 / 65535) << 8;
	}
	if (vol > 0) {
		balance = ((right-left) * 127) / vol;
	}

	dprintk("msp34xx: setvolume: mute=%s %d:%d  v=0x%02x b=0x%02x\n",
		muted ? "on" : "off", left, right, val>>8, balance);
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x0000, val); /* loudspeaker */
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x0006, val); /* headphones  */
	/* scart - on/off only */
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x0007, val ? 0x4000 : 0);
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x0001, balance << 8);
}

static void msp3400c_setbass(bktr_ptr_t client, int bass)
{
	int val = ((bass-32768) * 0x60 / 65535) << 8;

	dprintk("msp34xx: setbass: %d 0x%02x\n",bass, val>>8);
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x0002, val); /* loudspeaker */
}

static void msp3400c_settreble(bktr_ptr_t client, int treble)
{
	int val = ((treble-32768) * 0x60 / 65535) << 8;

	dprintk("msp34xx: settreble: %d 0x%02x\n",treble, val>>8);
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x0003, val); /* loudspeaker */
}

static void msp3400c_setmode(bktr_ptr_t client, int type)
{
	struct msp3400c *msp = client->msp3400c_info;
	int i;
	
	dprintk("msp3400: setmode: %d\n",type);
	msp->mode   = type;
	msp->stereo = VIDEO_SOUND_MONO;

	msp3400c_write(client,I2C_MSP3400C_DEM, 0x00bb,          /* ad_cv */
		       msp_init_data[type].ad_cv);
    
	for (i = 5; i >= 0; i--)                                   /* fir 1 */
		msp3400c_write(client,I2C_MSP3400C_DEM, 0x0001,
			       msp_init_data[type].fir1[i]);
    
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x0005, 0x0004); /* fir 2 */
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x0005, 0x0040);
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x0005, 0x0000);
	for (i = 5; i >= 0; i--)
		msp3400c_write(client,I2C_MSP3400C_DEM, 0x0005,
			       msp_init_data[type].fir2[i]);
    
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x0083,     /* MODE_REG */
		       msp_init_data[type].mode_reg);
    
	msp3400c_setcarrier(client, msp_init_data[type].cdo1,
			    msp_init_data[type].cdo2);
    
	msp3400c_write(client,I2C_MSP3400C_DEM, 0x0056, 0); /*LOAD_REG_1/2*/

	if (client->dolby) {
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0008,
			       0x0520); /* I2S1 */
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0009,
			       0x0620); /* I2S2 */
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x000b,
			       msp_init_data[type].dfp_src);
	} else {
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0008,
			       msp_init_data[type].dfp_src);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0009,
			       msp_init_data[type].dfp_src);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x000b,
			       msp_init_data[type].dfp_src);
	}
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x000a,
		       msp_init_data[type].dfp_src);
	msp3400c_write(client,I2C_MSP3400C_DFP, 0x000e,
		       msp_init_data[type].dfp_matrix);

	if (msp->nicam) {
		/* nicam prescale */
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0010, 0x5a00); /* was: 0x3000 */
	}
}

/* turn on/off nicam + stereo */
static void msp3400c_setstereo(bktr_ptr_t client, int mode)
{
	static char *strmode[] = { "0", "mono", "stereo", "3",
				   "lang1", "5", "6", "7", "lang2" };
	struct msp3400c *msp = client->msp3400c_info;
	int nicam=0; /* channel source: FM/AM or nicam */
	int src=0;

	/* switch demodulator */
	switch (msp->mode) {
	case MSP_MODE_FM_TERRA:
		dprintk("msp3400: FM setstereo: %s\n",strmode[mode]);
		msp3400c_setcarrier(client,msp->second,msp->main);
		switch (mode) {
		case VIDEO_SOUND_STEREO:
			msp3400c_write(client,I2C_MSP3400C_DFP, 0x000e, 0x3001);
			break;
		case VIDEO_SOUND_MONO:
		case VIDEO_SOUND_LANG1:
		case VIDEO_SOUND_LANG2:
			msp3400c_write(client,I2C_MSP3400C_DFP, 0x000e, 0x3000);
			break;
		}
		break;
	case MSP_MODE_FM_SAT:
		dprintk("msp3400: SAT setstereo: %s\n",strmode[mode]);
		switch (mode) {
		case VIDEO_SOUND_MONO:
			msp3400c_setcarrier(client, MSP_CARRIER(6.5), MSP_CARRIER(6.5));
			break;
		case VIDEO_SOUND_STEREO:
			msp3400c_setcarrier(client, MSP_CARRIER(7.2), MSP_CARRIER(7.02));
			break;
		case VIDEO_SOUND_LANG1:
			msp3400c_setcarrier(client, MSP_CARRIER(7.38), MSP_CARRIER(7.02));
			break;
		case VIDEO_SOUND_LANG2:
			msp3400c_setcarrier(client, MSP_CARRIER(7.38), MSP_CARRIER(7.02));
			break;
		}
		break;
	case MSP_MODE_FM_NICAM1:
	case MSP_MODE_FM_NICAM2:
	case MSP_MODE_AM_NICAM:
		dprintk("msp3400: NICAM setstereo: %s\n",strmode[mode]);
		msp3400c_setcarrier(client,msp->second,msp->main);
		if (msp->nicam_on)
			nicam=0x0100;
		break;
	case MSP_MODE_BTSC:
		dprintk("msp3400: BTSC setstereo: %s\n",strmode[mode]);
		nicam=0x0300;
		break;
	case MSP_MODE_EXTERN:
		dprintk("msp3400: extern setstereo: %s\n",strmode[mode]);
		nicam = 0x0200;
		break;
	case MSP_MODE_FM_RADIO:
		dprintk("msp3400: FM-Radio setstereo: %s\n",strmode[mode]);
		break;
	default:
		dprintk("msp3400: mono setstereo\n");
		return;
	}

	/* switch audio */
	switch (mode) {
	case VIDEO_SOUND_STEREO:
		src = 0x0020 | nicam;
#if 0 
		/* spatial effect */
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0005,0x4000);
#endif
		break;
	case VIDEO_SOUND_MONO:
		if (msp->mode == MSP_MODE_AM_NICAM) {
			dprintk("msp3400: switching to AM mono\n");
			/* AM mono decoding is handled by tuner, not MSP chip */
			/* SCART switching control register */
			msp3400c_set_scart(client,SCART_MONO,0);
			src = 0x0200;
			break;
		}
	case VIDEO_SOUND_LANG1:
		src = 0x0000 | nicam;
		break;
	case VIDEO_SOUND_LANG2:
		src = 0x0010 | nicam;
		break;
	}
	dprintk("msp3400: setstereo final source/matrix = 0x%x\n", src);

	if (client->dolby) {
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0008,0x0520);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0009,0x0620);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x000a,src);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x000b,src);
	} else {
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0008,src);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x0009,src);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x000a,src);
		msp3400c_write(client,I2C_MSP3400C_DFP, 0x000b,src);
	}
}

static void
msp3400c_print_mode(struct msp3400c *msp)
{
	if (msp->main == msp->second) {
		printf("bktr: msp3400: mono sound carrier: %d.%03d MHz\n",
		       msp->main/910000,(msp->main/910)%1000);
	} else {
		printf("bktr: msp3400: main sound carrier: %d.%03d MHz\n",
		       msp->main/910000,(msp->main/910)%1000);
	}
	if (msp->mode == MSP_MODE_FM_NICAM1 ||
	    msp->mode == MSP_MODE_FM_NICAM2)
		printf("bktr: msp3400: NICAM/FM carrier   : %d.%03d MHz\n",
		       msp->second/910000,(msp->second/910)%1000);
	if (msp->mode == MSP_MODE_AM_NICAM)
		printf("bktr: msp3400: NICAM/AM carrier   : %d.%03d MHz\n",
		       msp->second/910000,(msp->second/910)%1000);
	if (msp->mode == MSP_MODE_FM_TERRA &&
	    msp->main != msp->second) {
		printf("bktr: msp3400: FM-stereo carrier : %d.%03d MHz\n",
		       msp->second/910000,(msp->second/910)%1000);
	}
}

static void
msp3400c_restore_dfp(bktr_ptr_t client)
{
	struct msp3400c *msp = (struct msp3400c*)client->msp3400c_info;
	int i;

	for (i = 0; i < DFP_COUNT; i++) {
		if (-1 == msp->dfp_regs[i])
			continue;
		msp3400c_write(client,I2C_MSP3400C_DFP, i, msp->dfp_regs[i]);
	}
}

/* ----------------------------------------------------------------------- */

struct REGISTER_DUMP {
	int   addr;
	char *name;
};

struct REGISTER_DUMP d1[] = {
	{ 0x007e, "autodetect" },
	{ 0x0023, "C_AD_BITS " },
	{ 0x0038, "ADD_BITS  " },
	{ 0x003e, "CIB_BITS  " },
	{ 0x0057, "ERROR_RATE" },
};

static int
autodetect_stereo(bktr_ptr_t client)
{
	struct msp3400c *msp = (struct msp3400c*)client->msp3400c_info;
	int val;
	int newstereo = msp->stereo;
	int newnicam  = msp->nicam_on;
	int update = 0;

	switch (msp->mode) {
	case MSP_MODE_FM_TERRA:
		val = msp3400c_read(client, I2C_MSP3400C_DFP, 0x18);
		if (val > 32767)
			val -= 65536;
		dprintk("msp34xx: stereo detect register: %d\n",val);
		
		if (val > 4096) {
			newstereo = VIDEO_SOUND_STEREO | VIDEO_SOUND_MONO;
		} else if (val < -4096) {
			newstereo = VIDEO_SOUND_LANG1 | VIDEO_SOUND_LANG2;
		} else {
			newstereo = VIDEO_SOUND_MONO;
		}
		newnicam = 0;
		break;
	case MSP_MODE_FM_NICAM1:
	case MSP_MODE_FM_NICAM2:
	case MSP_MODE_AM_NICAM:
		val = msp3400c_read(client, I2C_MSP3400C_DEM, 0x23);
		dprintk("msp34xx: nicam sync=%d, mode=%d\n",val & 1, (val & 0x1e) >> 1);

		if (val & 1) {
			/* nicam synced */
			switch ((val & 0x1e) >> 1)  {
			case 0:
			case 8:
				newstereo = VIDEO_SOUND_STEREO;
				break;
			case 1:
			case 9:
				newstereo = VIDEO_SOUND_MONO
					| VIDEO_SOUND_LANG1;
				break;
			case 2:
			case 10:
				newstereo = VIDEO_SOUND_MONO
					| VIDEO_SOUND_LANG1
					| VIDEO_SOUND_LANG2;
				break;
			default:
				newstereo = VIDEO_SOUND_MONO;
				break;
			}
			newnicam=1;
		} else {
			newnicam = 0;
			newstereo = VIDEO_SOUND_MONO;
		}
		break;
	case MSP_MODE_BTSC:
		val = msp3400c_read(client, I2C_MSP3400C_DEM, 0x200);
		dprintk("msp3410: status=0x%x (pri=%s, sec=%s, %s%s%s)\n",
			val,
			(val & 0x0002) ? "no"     : "yes",
			(val & 0x0004) ? "no"     : "yes",
			(val & 0x0040) ? "stereo" : "mono",
			(val & 0x0080) ? ", nicam 2nd mono" : "",
			(val & 0x0100) ? ", bilingual/SAP"  : "");
		newstereo = VIDEO_SOUND_MONO;
		if (val & 0x0040) newstereo |= VIDEO_SOUND_STEREO;
		if (val & 0x0100) newstereo |= VIDEO_SOUND_LANG1;
		break;
	}
	if (newstereo != msp->stereo) {
		update = 1;
		dprintk("msp34xx: watch: stereo %d => %d\n",
			msp->stereo,newstereo);
		msp->stereo   = newstereo;
	}
	if (newnicam != msp->nicam_on) {
		update = 1;
		dprintk("msp34xx: watch: nicam %d => %d\n",
			msp->nicam_on,newnicam);
		msp->nicam_on = newnicam;
	}
	return update;
}

/*
 * A kernel thread for msp3400 control -- we don't want to block the
 * in the ioctl while doing the sound carrier & stereo detect
 */

/* stereo/multilang monitoring */
static void watch_stereo(bktr_ptr_t client)
{
	struct msp3400c *msp = (struct msp3400c*)client->msp3400c_info;

	if (autodetect_stereo(client)) {
		if (msp->stereo & VIDEO_SOUND_STEREO)
			msp3400c_setstereo(client,VIDEO_SOUND_STEREO);
		else if (msp->stereo & VIDEO_SOUND_LANG1)
			msp3400c_setstereo(client,VIDEO_SOUND_LANG1);
		else
			msp3400c_setstereo(client,VIDEO_SOUND_MONO);
	}
	if (client->stereo_once)
		msp->watch_stereo = 0;

}

static void msp3400c_thread(void *data)
{
	bktr_ptr_t client = data;
	struct msp3400c *msp = (struct msp3400c*)client->msp3400c_info;
	
	struct CARRIER_DETECT *cd;
	int count, max1,max2,val1,val2, val,this;
	
	dprintk("msp3400: thread started\n");
	
	mtx_lock(&Giant);
	for (;;) {
		if (msp->rmmod)
			goto done;
		tsleep(msp->kthread, PRIBIO, "idle", 0);
		if (msp->rmmod)
			goto done;
		if (msp->halt_thread) {
			msp->watch_stereo = 0;
			msp->halt_thread = 0;
			continue;
		}
		
		if (VIDEO_MODE_RADIO == msp->norm ||
		    MSP_MODE_EXTERN  == msp->mode)
			continue;  /* nothing to do */
		
		msp->active = 1;
		
		if (msp->watch_stereo) {
			watch_stereo(client);
			msp->active = 0;
			continue;
		}

		/* some time for the tuner to sync */
		tsleep(msp->kthread, PRIBIO, "tuner sync", hz/2);
		
	restart:
		if (VIDEO_MODE_RADIO == msp->norm ||
		    MSP_MODE_EXTERN  == msp->mode)
			continue;  /* nothing to do */
		msp->restart = 0;
		msp3400c_setvolume(client, msp->muted, 0, 0);
		msp3400c_setmode(client, MSP_MODE_AM_DETECT /* +1 */ );
		val1 = val2 = 0;
		max1 = max2 = -1;
		msp->watch_stereo = 0;

		/* carrier detect pass #1 -- main carrier */
		cd = carrier_detect_main; count = CARRIER_COUNT(carrier_detect_main);

		if (client->amsound && (msp->norm == VIDEO_MODE_SECAM)) {
			/* autodetect doesn't work well with AM ... */
			max1 = 3;
			count = 0;
			dprintk("msp3400: AM sound override\n");
		}
		
		for (this = 0; this < count; this++) {
			msp3400c_setcarrier(client, cd[this].cdo,cd[this].cdo);

			tsleep(msp->kthread, PRIBIO, "carrier detect", hz/100);
			
			if (msp->restart)
				msp->restart = 0;

			val = msp3400c_read(client, I2C_MSP3400C_DFP, 0x1b);
			if (val > 32767)
				val -= 65536;
			if (val1 < val)
				val1 = val, max1 = this;
			dprintk("msp3400: carrier1 val: %5d / %s\n", val,cd[this].name);
		}
	
		/* carrier detect pass #2 -- second (stereo) carrier */
		switch (max1) {
		case 1: /* 5.5 */
			cd = carrier_detect_55; count = CARRIER_COUNT(carrier_detect_55);
			break;
		case 3: /* 6.5 */
			cd = carrier_detect_65; count = CARRIER_COUNT(carrier_detect_65);
			break;
		case 0: /* 4.5 */
		case 2: /* 6.0 */
		default:
			cd = NULL; count = 0;
			break;
		}
		
		if (client->amsound && (msp->norm == VIDEO_MODE_SECAM)) {
			/* autodetect doesn't work well with AM ... */
			cd = NULL; count = 0; max2 = 0;
		}
		for (this = 0; this < count; this++) {
			msp3400c_setcarrier(client, cd[this].cdo,cd[this].cdo);

			tsleep(msp->kthread, PRIBIO, "carrier detection", hz/100);
			if (msp->restart)
				goto restart;

			val = msp3400c_read(client, I2C_MSP3400C_DFP, 0x1b);
			if (val > 32767)
				val -= 65536;
			if (val2 < val)
				val2 = val, max2 = this;
			dprintk("msp3400: carrier2 val: %5d / %s\n", val,cd[this].name);
		}

		/* programm the msp3400 according to the results */
		msp->main   = carrier_detect_main[max1].cdo;
		switch (max1) {
		case 1: /* 5.5 */
			if (max2 == 0) {
				/* B/G FM-stereo */
				msp->second = carrier_detect_55[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_FM_TERRA);
				msp->nicam_on = 0;
				/* XXX why mono? this probably can do stereo... - Alex*/
				msp3400c_setstereo(client, VIDEO_SOUND_MONO);
				msp->watch_stereo = 1;
			} else if (max2 == 1 && msp->nicam) {
				/* B/G NICAM */
				msp->second = carrier_detect_55[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_FM_NICAM1);
				msp->nicam_on = 1;
				msp3400c_setcarrier(client, msp->second, msp->main);
				msp->watch_stereo = 1;
			} else {
				goto no_second;
			}
			break;
		case 2: /* 6.0 */
			/* PAL I NICAM */
			msp->second = MSP_CARRIER(6.552);
			msp3400c_setmode(client, MSP_MODE_FM_NICAM2);
			msp->nicam_on = 1;
			msp3400c_setcarrier(client, msp->second, msp->main);
			msp->watch_stereo = 1;
			break;
		case 3: /* 6.5 */
			if (max2 == 1 || max2 == 2) {
				/* D/K FM-stereo */
				msp->second = carrier_detect_65[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_FM_TERRA);
				msp->nicam_on = 0;
				msp3400c_setstereo(client, VIDEO_SOUND_MONO);
				msp->watch_stereo = 1;
			} else if (max2 == 0 &&
				   msp->norm == VIDEO_MODE_SECAM) {
				/* L NICAM or AM-mono */
				msp->second = carrier_detect_65[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_AM_NICAM);
				msp->nicam_on = 0;
				msp3400c_setstereo(client, VIDEO_SOUND_MONO);
				msp3400c_setcarrier(client, msp->second, msp->main);
				/* volume prescale for SCART (AM mono input) */
				msp3400c_write(client,I2C_MSP3400C_DFP, 0x000d, 0x1900);
				msp->watch_stereo = 1;
			} else if (max2 == 0 && msp->nicam) {
				/* D/K NICAM */
				msp->second = carrier_detect_65[max2].cdo;
				msp3400c_setmode(client, MSP_MODE_FM_NICAM1);
				msp->nicam_on = 1;
				msp3400c_setcarrier(client, msp->second, msp->main);
				msp->watch_stereo = 1;
			} else {
				goto no_second;
			}
			break;
		case 0: /* 4.5 */
		default:
		no_second:
			msp->second = carrier_detect_main[max1].cdo;
			msp3400c_setmode(client, MSP_MODE_FM_TERRA);
			msp->nicam_on = 0;
			msp3400c_setcarrier(client, msp->second, msp->main);
			msp->stereo = VIDEO_SOUND_MONO;
			msp3400c_setstereo(client, VIDEO_SOUND_MONO);
			break;
		}

		if (msp->watch_stereo)
			watch_stereo(client);

		/* unmute + restore dfp registers */
		msp3400c_setvolume(client, msp->muted, msp->left, msp->right);
		msp3400c_restore_dfp(client);

		if (bootverbose)
			msp3400c_print_mode(msp);
		
		msp->active = 0;
	}

done:
	dprintk("msp3400: thread: exit\n");
	msp->active = 0;

	msp->kthread = NULL;
	wakeup(&msp->kthread);
	mtx_unlock(&Giant);

	kproc_exit(0);
}

/* ----------------------------------------------------------------------- */
/* this one uses the automatic sound standard detection of newer           */
/* msp34xx chip versions                                                   */

static struct MODES {
	int retval;
	int main, second;
	char *name;
} modelist[] = {
	{ 0x0000, 0, 0, "ERROR" },
	{ 0x0001, 0, 0, "autodetect start" },
	{ 0x0002, MSP_CARRIER(4.5), MSP_CARRIER(4.72), "4.5/4.72  M Dual FM-Stereo" },
	{ 0x0003, MSP_CARRIER(5.5), MSP_CARRIER(5.7421875), "5.5/5.74  B/G Dual FM-Stereo" },
	{ 0x0004, MSP_CARRIER(6.5), MSP_CARRIER(6.2578125), "6.5/6.25  D/K1 Dual FM-Stereo" },
	{ 0x0005, MSP_CARRIER(6.5), MSP_CARRIER(6.7421875), "6.5/6.74  D/K2 Dual FM-Stereo" },
	{ 0x0006, MSP_CARRIER(6.5), MSP_CARRIER(6.5), "6.5  D/K FM-Mono (HDEV3)" },
	{ 0x0008, MSP_CARRIER(5.5), MSP_CARRIER(5.85), "5.5/5.85  B/G NICAM FM" },
	{ 0x0009, MSP_CARRIER(6.5), MSP_CARRIER(5.85), "6.5/5.85  L NICAM AM" },
	{ 0x000a, MSP_CARRIER(6.0), MSP_CARRIER(6.55), "6.0/6.55  I NICAM FM" },
	{ 0x000b, MSP_CARRIER(6.5), MSP_CARRIER(5.85), "6.5/5.85  D/K NICAM FM" },
	{ 0x000c, MSP_CARRIER(6.5), MSP_CARRIER(5.85), "6.5/5.85  D/K NICAM FM (HDEV2)" },
	{ 0x0020, MSP_CARRIER(4.5), MSP_CARRIER(4.5), "4.5  M BTSC-Stereo" },
	{ 0x0021, MSP_CARRIER(4.5), MSP_CARRIER(4.5), "4.5  M BTSC-Mono + SAP" },
	{ 0x0030, MSP_CARRIER(4.5), MSP_CARRIER(4.5), "4.5  M EIA-J Japan Stereo" },
	{ 0x0040, MSP_CARRIER(10.7), MSP_CARRIER(10.7), "10.7  FM-Stereo Radio" },
	{ 0x0050, MSP_CARRIER(6.5), MSP_CARRIER(6.5), "6.5  SAT-Mono" },
	{ 0x0051, MSP_CARRIER(7.02), MSP_CARRIER(7.20), "7.02/7.20  SAT-Stereo" },
	{ 0x0060, MSP_CARRIER(7.2), MSP_CARRIER(7.2), "7.2  SAT ADR" },
	{     -1, 0, 0, NULL }, /* EOF */
};
 
static void msp3410d_thread(void *data)
{
	bktr_ptr_t client = data;
	struct msp3400c *msp = (struct msp3400c*)client->msp3400c_info;
	int mode,val,i,std;
	int timo = 0;
    
	dprintk("msp3410: thread started\n");
		
	mtx_lock(&Giant);
	for (;;) {
		if (msp->rmmod)
			goto done;
		if (!msp->watch_stereo)
			timo = 0;
		else
			timo = 10*hz;
		tsleep(msp->kthread, PRIBIO, "idle", timo);
		if (msp->rmmod)
			goto done;
		if (msp->halt_thread) {
			msp->watch_stereo = 0;
			msp->halt_thread = 0;
			dprintk("msp3410: thread halted\n");
			continue;
		}
		
		if (msp->mode == MSP_MODE_EXTERN)
			continue;
		
		msp->active = 1;
		
		if (msp->watch_stereo) {
			watch_stereo(client);
			msp->active = 0;
			continue;
		}
	
		/* some time for the tuner to sync */
		tsleep(msp->kthread, PRIBIO, "tuner sync", hz/2);

	restart:
		if (msp->mode == MSP_MODE_EXTERN)
			continue;
		msp->restart = 0;
		msp->watch_stereo = 0;

		/* put into sane state (and mute) */
		msp3400c_reset(client);

		/* start autodetect */
		switch (msp->norm) {
		case VIDEO_MODE_PAL:
			mode = 0x1003;
			std  = 1;
			break;
		case VIDEO_MODE_NTSC:  /* BTSC */
			mode = 0x2003;
			std  = 0x0020;
			break;
		case VIDEO_MODE_SECAM: 
			mode = 0x0003;
			std  = 1;
			break;
		case VIDEO_MODE_RADIO: 
			mode = 0x0003;
			std  = 0x0040;
			break;
		default:
			mode = 0x0003;
			std  = 1;
			break;
		}
		msp3400c_write(client, I2C_MSP3400C_DEM, 0x30, mode);
		msp3400c_write(client, I2C_MSP3400C_DEM, 0x20, std);

		if (bootverbose) {
			int i;
			for (i = 0; modelist[i].name != NULL; i++)
				if (modelist[i].retval == std)
					break;
			dprintk("msp3410: setting mode: %s (0x%04x)\n",
			       modelist[i].name ? modelist[i].name : "unknown",std);
		}

		if (std != 1) {
			/* programmed some specific mode */
			val = std;
		} else {
			/* triggered autodetect */
			for (;;) {
				tsleep(msp->kthread, PRIBIO, "autodetection", hz/10);
				if (msp->restart)
					goto restart;

				/* check results */
				val = msp3400c_read(client, I2C_MSP3400C_DEM, 0x7e);
				if (val < 0x07ff)
					break;
				dprintk("msp3410: detection still in progress\n");
			}
		}
		for (i = 0; modelist[i].name != NULL; i++)
			if (modelist[i].retval == val)
				break;
		dprintk("msp3410: current mode: %s (0x%04x)\n",
			modelist[i].name ? modelist[i].name : "unknown",
			val);
		msp->main   = modelist[i].main;
		msp->second = modelist[i].second;

		if (client->amsound && (msp->norm == VIDEO_MODE_SECAM) && (val != 0x0009)) {
			/* autodetection has failed, let backup */
			dprintk("msp3410: autodetection failed, switching to backup mode: %s (0x%04x)\n",
				modelist[8].name ? modelist[8].name : "unknown",val);
			val = 0x0009;
			msp3400c_write(client, I2C_MSP3400C_DEM, 0x20, val);
		}

		/* set various prescales */
		msp3400c_write(client, I2C_MSP3400C_DFP, 0x0d, 0x1900); /* scart */
		msp3400c_write(client, I2C_MSP3400C_DFP, 0x0e, 0x2403); /* FM */
		msp3400c_write(client, I2C_MSP3400C_DFP, 0x10, 0x5a00); /* nicam */

		/* set stereo */
		switch (val) {
		case 0x0008: /* B/G NICAM */
		case 0x000a: /* I NICAM */
			if (val == 0x0008)
				msp->mode = MSP_MODE_FM_NICAM1;
			else
				msp->mode = MSP_MODE_FM_NICAM2;
			/* just turn on stereo */
			msp->stereo = VIDEO_SOUND_STEREO;
			msp->nicam_on = 1;
			msp->watch_stereo = 1;
			msp3400c_setstereo(client,VIDEO_SOUND_STEREO);
			break;
		case 0x0009:			
			msp->mode = MSP_MODE_AM_NICAM;
			msp->stereo = VIDEO_SOUND_MONO;
			msp->nicam_on = 1;
			msp3400c_setstereo(client,VIDEO_SOUND_MONO);
			msp->watch_stereo = 1;
			break;
		case 0x0020: /* BTSC */
			/* just turn on stereo */
			msp->mode   = MSP_MODE_BTSC;
			msp->stereo = VIDEO_SOUND_STEREO;
			msp->nicam_on = 0;
			msp->watch_stereo = 1;
			msp3400c_setstereo(client,VIDEO_SOUND_STEREO);
			break;
		case 0x0040: /* FM radio */
			msp->mode   = MSP_MODE_FM_RADIO;
			msp->stereo = VIDEO_SOUND_STEREO;
			msp->nicam_on = 0;
			msp->watch_stereo = 0;
			/* scart routing */
			msp3400c_set_scart(client,SCART_IN2,0);
			msp3400c_write(client,I2C_MSP3400C_DFP, 0x08, 0x0220);
			msp3400c_write(client,I2C_MSP3400C_DFP, 0x09, 0x0220);
			msp3400c_write(client,I2C_MSP3400C_DFP, 0x0b, 0x0220);
			break;
		case 0x0003:
			msp->mode   = MSP_MODE_FM_TERRA;
			msp->stereo = VIDEO_SOUND_STEREO;
			msp->nicam_on = 0;
			msp->watch_stereo = 1;
			msp3400c_setstereo(client,VIDEO_SOUND_STEREO);
			break;
		}

		if (msp->watch_stereo)
			watch_stereo(client);
		
		/* unmute + restore dfp registers */
		msp3400c_setbass(client, msp->bass);
		msp3400c_settreble(client, msp->treble);
		msp3400c_setvolume(client, msp->muted, msp->left, msp->right);
		msp3400c_restore_dfp(client);

		msp->active = 0;
	}

done:
	dprintk("msp3410: thread: exit\n");
	msp->active = 0;

	msp->kthread = NULL;
	wakeup(&msp->kthread);
	mtx_unlock(&Giant);

	kproc_exit(0);
}

int msp_attach(bktr_ptr_t bktr)
{
	struct msp3400c *msp;
	int              rev1,rev2,i;
	int		 err;
	char		 buf[20];

	msp = (struct msp3400c *) malloc(sizeof(struct msp3400c), M_DEVBUF, M_NOWAIT);
	if (msp == NULL)
                return ENOMEM;
	bktr->msp3400c_info = msp;
	
	memset(msp,0,sizeof(struct msp3400c));
	msp->left   = 65535;
	msp->right  = 65535;
	msp->bass   = 32768;
	msp->treble = 32768;
	msp->input  = -1;

	for (i = 0; i < DFP_COUNT; i++)
		msp->dfp_regs[i] = -1;
	
	msp3400c_reset(bktr);
	
	rev1 = msp3400c_read(bktr, I2C_MSP3400C_DFP, 0x1e);
	if (-1 != rev1)
		rev2 = msp3400c_read(bktr, I2C_MSP3400C_DFP, 0x1f);
	if ((-1 == rev1) || (0 == rev1 && 0 == rev2)) {
		free(msp, M_DEVBUF);
		bktr->msp3400c_info = NULL;
		printf("%s: msp3400: error while reading chip version\n", bktr_name(bktr));
		return ENXIO;
	}

#if 0
	/* this will turn on a 1kHz beep - might be useful for debugging... */
	msp3400c_write(bktr,I2C_MSP3400C_DFP, 0x0014, 0x1040);
#endif

	sprintf(buf,"MSP34%02d%c-%c%d",
		(rev2>>8)&0xff, (rev1&0xff)+'@', ((rev1>>8)&0xff)+'@', rev2&0x1f);
	msp->nicam = (((rev2>>8)&0xff) != 00) ? 1 : 0;
	
	if (bktr->mspsimple == -1) {
		/* default mode */
		/* msp->simple = (((rev2>>8)&0xff) == 0) ? 0 : 1; */
		msp->simple = ((rev1&0xff)+'@' > 'C');
	} else {
		/* use kenv value */
		msp->simple = bktr->mspsimple;
	}

	/* hello world :-) */
	if (bootverbose) {
		printf("%s: msp34xx: init: chip=%s", bktr_name(bktr), buf);
		if (msp->nicam)
			printf(", has NICAM support");
		printf("\n");
	}

	/* startup control thread */
	err = kproc_create(msp->simple ? msp3410d_thread : msp3400c_thread,
			     bktr, &msp->kthread, (RFFDG | RFPROC), 0,
			     "%s_msp34xx_thread", bktr->bktr_xname);
	if (err) {
		printf("%s: Error returned by kproc_create: %d", bktr_name(bktr), err);
		free(msp, M_DEVBUF);
		bktr->msp3400c_info = NULL;
		return ENXIO;
	}
	wakeup(msp->kthread);

	/* done */
	return 0;
}

int msp_detach(bktr_ptr_t client)
{
	struct msp3400c *msp  = (struct msp3400c*)client->msp3400c_info;
	
	/* shutdown control thread */
	if (msp->kthread) 
	{
		/* XXX mutex lock required */
		mtx_lock(&Giant);
		msp->rmmod = 1;
		msp->watch_stereo = 0;
		wakeup(msp->kthread);

		while (msp->kthread)
			tsleep(&msp->kthread, PRIBIO, "wait for kthread", hz/10);
		mtx_unlock(&Giant);
	}

	if (client->msp3400c_info != NULL) {
		free(client->msp3400c_info, M_DEVBUF);
		client->msp3400c_info = NULL;
	}

    	msp3400c_reset(client);
	
	return 0;
}

void msp_wake_thread(bktr_ptr_t client)
{
	struct msp3400c *msp  = (struct msp3400c*)client->msp3400c_info;

	msp3400c_setvolume(client,msp->muted,0,0);
	msp->watch_stereo=0;
	if (msp->active)
		msp->restart = 1;
	wakeup(msp->kthread);
}

void msp_halt_thread(bktr_ptr_t client)
{
	struct msp3400c *msp  = (struct msp3400c*)client->msp3400c_info;

	msp3400c_setvolume(client,msp->muted,0,0);
	if (msp->active)
		msp->restart = 1;
	msp->halt_thread = 1;
	wakeup(msp->kthread);
}
#endif /* BKTR_NEW_MSP34XX_DRIVER */
