/*	$OpenBSD: am7930.c,v 1.8 2022/10/26 20:19:07 kn Exp $	*/
/*	$NetBSD: am7930.c,v 1.44 2001/11/13 13:14:34 lukem Exp $	*/

/*
 * Copyright (c) 1995 Rolf Grossmann
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
 *      This product includes software developed by Rolf Grossmann.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Front-end attachment independent layer for AMD 79c30
 * audio driver.  No ISDN support.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/proc.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <dev/ic/am7930reg.h>
#include <dev/ic/am7930var.h>

#ifdef AUDIO_DEBUG
int	am7930debug = 0;
#define	DPRINTF(x)	if (am7930debug) printf x
#else
#define	DPRINTF(x)
#endif


/* The following tables stolen from former (4.4Lite's) sys/sparc/bsd_audio.c */

/*
 * gx, gr & stg gains.  this table must contain 256 elements with
 * the 0th being "infinity" (the magic value 9008).  The remaining
 * elements match sun's gain curve (but with higher resolution):
 * -18 to 0dB in .16dB steps then 0 to 12dB in .08dB steps.
 */
static const uint16_t gx_coeff[256] = {
	0x9008, 0x8e7c, 0x8e51, 0x8e45, 0x8d42, 0x8d3b, 0x8c36, 0x8c33,
	0x8b32, 0x8b2a, 0x8b2b, 0x8b2c, 0x8b25, 0x8b23, 0x8b22, 0x8b22,
	0x9122, 0x8b1a, 0x8aa3, 0x8aa3, 0x8b1c, 0x8aa6, 0x912d, 0x912b,
	0x8aab, 0x8b12, 0x8aaa, 0x8ab2, 0x9132, 0x8ab4, 0x913c, 0x8abb,
	0x9142, 0x9144, 0x9151, 0x8ad5, 0x8aeb, 0x8a79, 0x8a5a, 0x8a4a,
	0x8b03, 0x91c2, 0x91bb, 0x8a3f, 0x8a33, 0x91b2, 0x9212, 0x9213,
	0x8a2c, 0x921d, 0x8a23, 0x921a, 0x9222, 0x9223, 0x922d, 0x9231,
	0x9234, 0x9242, 0x925b, 0x92dd, 0x92c1, 0x92b3, 0x92ab, 0x92a4,
	0x92a2, 0x932b, 0x9341, 0x93d3, 0x93b2, 0x93a2, 0x943c, 0x94b2,
	0x953a, 0x9653, 0x9782, 0x9e21, 0x9d23, 0x9cd2, 0x9c23, 0x9baa,
	0x9bde, 0x9b33, 0x9b22, 0x9b1d, 0x9ab2, 0xa142, 0xa1e5, 0x9a3b,
	0xa213, 0xa1a2, 0xa231, 0xa2eb, 0xa313, 0xa334, 0xa421, 0xa54b,
	0xada4, 0xac23, 0xab3b, 0xaaab, 0xaa5c, 0xb1a3, 0xb2ca, 0xb3bd,
	0xbe24, 0xbb2b, 0xba33, 0xc32b, 0xcb5a, 0xd2a2, 0xe31d, 0x0808,
	0x72ba, 0x62c2, 0x5c32, 0x52db, 0x513e, 0x4cce, 0x43b2, 0x4243,
	0x41b4, 0x3b12, 0x3bc3, 0x3df2, 0x34bd, 0x3334, 0x32c2, 0x3224,
	0x31aa, 0x2a7b, 0x2aaa, 0x2b23, 0x2bba, 0x2c42, 0x2e23, 0x25bb,
	0x242b, 0x240f, 0x231a, 0x22bb, 0x2241, 0x2223, 0x221f, 0x1a33,
	0x1a4a, 0x1acd, 0x2132, 0x1b1b, 0x1b2c, 0x1b62, 0x1c12, 0x1c32,
	0x1d1b, 0x1e71, 0x16b1, 0x1522, 0x1434, 0x1412, 0x1352, 0x1323,
	0x1315, 0x12bc, 0x127a, 0x1235, 0x1226, 0x11a2, 0x1216, 0x0a2a,
	0x11bc, 0x11d1, 0x1163, 0x0ac2, 0x0ab2, 0x0aab, 0x0b1b, 0x0b23,
	0x0b33, 0x0c0f, 0x0bb3, 0x0c1b, 0x0c3e, 0x0cb1, 0x0d4c, 0x0ec1,
	0x079a, 0x0614, 0x0521, 0x047c, 0x0422, 0x03b1, 0x03e3, 0x0333,
	0x0322, 0x031c, 0x02aa, 0x02ba, 0x02f2, 0x0242, 0x0232, 0x0227,
	0x0222, 0x021b, 0x01ad, 0x0212, 0x01b2, 0x01bb, 0x01cb, 0x01f6,
	0x0152, 0x013a, 0x0133, 0x0131, 0x012c, 0x0123, 0x0122, 0x00a2,
	0x011b, 0x011e, 0x0114, 0x00b1, 0x00aa, 0x00b3, 0x00bd, 0x00ba,
	0x00c5, 0x00d3, 0x00f3, 0x0062, 0x0051, 0x0042, 0x003b, 0x0033,
	0x0032, 0x002a, 0x002c, 0x0025, 0x0023, 0x0022, 0x001a, 0x0021,
	0x001b, 0x001b, 0x001d, 0x0015, 0x0013, 0x0013, 0x0012, 0x0012,
	0x000a, 0x000a, 0x0011, 0x0011, 0x000b, 0x000b, 0x000c, 0x000e,
};

/*
 * second stage play gain.
 */
static const uint16_t ger_coeff[] = {
	0x431f, /* 5. dB */
	0x331f, /* 5.5 dB */
	0x40dd, /* 6. dB */
	0x11dd, /* 6.5 dB */
	0x440f, /* 7. dB */
	0x411f, /* 7.5 dB */
	0x311f, /* 8. dB */
	0x5520, /* 8.5 dB */
	0x10dd, /* 9. dB */
	0x4211, /* 9.5 dB */
	0x410f, /* 10. dB */
	0x111f, /* 10.5 dB */
	0x600b, /* 11. dB */
	0x00dd, /* 11.5 dB */
	0x4210, /* 12. dB */
	0x110f, /* 13. dB */
	0x7200, /* 14. dB */
	0x2110, /* 15. dB */
	0x2200, /* 15.9 dB */
	0x000b, /* 16.9 dB */
	0x000f  /* 18. dB */
#define NGER (sizeof(ger_coeff) / sizeof(ger_coeff[0]))
};


/*
 * Reset chip and set boot-time softc defaults.
 */
void
am7930_init(struct am7930_softc *sc, int flag)
{
	DPRINTF(("am7930_init()\n"));

	/* set boot defaults */
	sc->sc_rlevel = 128;
	sc->sc_plevel = 128;
	sc->sc_mlevel = 0;
	sc->sc_out_port = AUDIOAMD_SPEAKER_VOL;
	sc->sc_mic_mute = 0;

	/* disable sample interrupts */
	AM7930_IWRITE(sc, AM7930_IREG_MUX_MCR4, 0);

	/* initialise voice and data, and disable interrupts */
	AM7930_IWRITE(sc, AM7930_IREG_INIT,
	    AM7930_INIT_PMS_ACTIVE | AM7930_INIT_INT_DISABLE);

	if (flag == AUDIOAMD_DMA_MODE) {
		/* configure PP for serial (SBP) mode */
		AM7930_IWRITE(sc, AM7930_IREG_PP_PPCR1, AM7930_PPCR1_SBP);

		/*
		 * Initialise the MUX unit - route the MAP to the PP
		 */
		AM7930_IWRITE(sc, AM7930_IREG_MUX_MCR1,
		    (AM7930_MCRCHAN_BA << 4) | AM7930_MCRCHAN_BD);
		AM7930_IWRITE(sc, AM7930_IREG_MUX_MCR2, AM7930_MCRCHAN_NC);
		AM7930_IWRITE(sc, AM7930_IREG_MUX_MCR3, AM7930_MCRCHAN_NC);
	} else {
		/*
		 * Initialize the MUX unit.  We use MCR3 to route the MAP
		 * through channel Bb.  MCR1 and MCR2 are unused.
		 * Setting the INT enable bit in MCR4 will generate an
		 * interrupt on each converted audio sample.
		 */
		AM7930_IWRITE(sc, AM7930_IREG_MUX_MCR1, 0);
		AM7930_IWRITE(sc, AM7930_IREG_MUX_MCR2, 0);
		AM7930_IWRITE(sc, AM7930_IREG_MUX_MCR3,
		    (AM7930_MCRCHAN_BB << 4) | AM7930_MCRCHAN_BA);
		AM7930_IWRITE(sc, AM7930_IREG_MUX_MCR4,
		    AM7930_MCR4_INT_ENABLE);
	}
}

/*
 * XXX chip is full-duplex, but really attach-dependent.
 * For now we know of no half-duplex attachments.
 */
int
am7930_open(void *addr, int flags)
{
	struct am7930_softc *sc = addr;

	DPRINTF(("sa_open: unit %p\n", sc));
	if (sc->sc_open)
		return EBUSY;
	sc->sc_open = 1;
	sc->sc_locked = 0;

	sc->sc_glue->onopen(sc);
	DPRINTF(("saopen: ok -> sc=%p\n",sc));
	return 0;
}

void
am7930_close(void *addr)
{
	struct am7930_softc *sc = addr;

	DPRINTF(("sa_close: sc=%p\n", sc));
	sc->sc_glue->onclose(sc);
	sc->sc_open = 0;
	DPRINTF(("sa_close: closed.\n"));
}

int
am7930_set_params(void *addr, int setmode, int usemode,
    struct audio_params *play, struct audio_params *rec)
{
	struct am7930_softc *sc = addr;
	struct audio_params *p;
	int mode;

	for (mode = AUMODE_RECORD; mode != -1;
	    mode = mode == AUMODE_RECORD ? AUMODE_PLAY : -1) {
		if ((setmode & mode) == 0)
			continue;

		p = mode == AUMODE_PLAY ? play : rec;
		if (p == NULL)
			continue;

		p->encoding = AUDIO_ENCODING_ULAW;
		p->precision = sc->sc_glue->precision;
		p->bps = AUDIO_BPS(p->precision);
		p->msb = 0;
		p->channels = 1;
		/* no other rates supported by amd chip */
		p->sample_rate = 8000;
	}

	return 0;
}

int
am7930_round_blocksize(void *addr, int blk)
{
	return blk;
}

int
am7930_commit_settings(void *addr)
{
	struct am7930_softc *sc = addr;
	uint16_t ger, gr, gx, stgr;
	uint8_t mmr2, mmr3;
	int level;

	DPRINTF(("sa_commit.\n"));
	gx = gx_coeff[sc->sc_rlevel];
	stgr = gx_coeff[sc->sc_mlevel];

	level = (sc->sc_plevel * (256 + NGER)) >> 8;
	if (level >= 256) {
		ger = ger_coeff[level - 256];
		gr = gx_coeff[255];
	} else {
		ger = ger_coeff[0];
		gr = gx_coeff[level];
	}

	/* XXX: this is called before DMA is setup, useful ? */
	mtx_enter(&audio_lock);

	mmr2 = AM7930_IREAD(sc, AM7930_IREG_MAP_MMR2);
	if (sc->sc_out_port == AUDIOAMD_SPEAKER_VOL)
		mmr2 |= AM7930_MMR2_LS;
	else
		mmr2 &= ~AM7930_MMR2_LS;
	AM7930_IWRITE(sc, AM7930_IREG_MAP_MMR2, mmr2);

	mmr3 = AM7930_IREAD(sc, AM7930_IREG_MAP_MMR3);
	if (sc->sc_mic_mute)
		mmr3 |= AM7930_MMR3_MUTE;
	else
		mmr3 &= ~AM7930_MMR3_MUTE;
	AM7930_IWRITE(sc, AM7930_IREG_MAP_MMR3, mmr3);

	AM7930_IWRITE(sc, AM7930_IREG_MAP_MMR1,
	    AM7930_MMR1_GX | AM7930_MMR1_GER |
	    AM7930_MMR1_GR | AM7930_MMR1_STG);

	AM7930_IWRITE16(sc, AM7930_IREG_MAP_GX, gx);
	AM7930_IWRITE16(sc, AM7930_IREG_MAP_STG, stgr);
	AM7930_IWRITE16(sc, AM7930_IREG_MAP_GR, gr);
	AM7930_IWRITE16(sc, AM7930_IREG_MAP_GER, ger);

	mtx_leave(&audio_lock);

	return 0;
}

int
am7930_halt_output(void *addr)
{
	struct am7930_softc *sc = addr;

	/* XXX only halt, if input is also halted ?? */
	AM7930_IWRITE(sc, AM7930_IREG_INIT,
	    AM7930_INIT_PMS_ACTIVE | AM7930_INIT_INT_DISABLE);
	sc->sc_locked = 0;
	return 0;
}

int
am7930_halt_input(void *addr)
{
	struct am7930_softc *sc = addr;

	/* XXX only halt, if output is also halted ?? */
	AM7930_IWRITE(sc, AM7930_IREG_INIT,
	    AM7930_INIT_PMS_ACTIVE | AM7930_INIT_INT_DISABLE);
	sc->sc_locked = 0;
	return 0;
}

/*
 * Attach-dependent channel set/query
 */
int
am7930_set_port(void *addr, mixer_ctrl_t *cp)
{
	struct am7930_softc *sc = addr;

	DPRINTF(("am7930_set_port: port=%d", cp->dev));
	if (cp->dev == AUDIOAMD_RECORD_SOURCE ||
	    cp->dev == AUDIOAMD_MONITOR_OUTPUT ||
	    cp->dev == AUDIOAMD_MIC_MUTE) {
		if (cp->type != AUDIO_MIXER_ENUM)
			return EINVAL;
	} else if (cp->type != AUDIO_MIXER_VALUE ||
	    cp->un.value.num_channels != 1) {
		return EINVAL;
	}

	switch(cp->dev) {
	case AUDIOAMD_MIC_VOL:
		sc->sc_rlevel = cp->un.value.level[AUDIO_MIXER_LEVEL_MONO];
		break;
	case AUDIOAMD_SPEAKER_VOL:
	case AUDIOAMD_HEADPHONES_VOL:
		sc->sc_plevel = cp->un.value.level[AUDIO_MIXER_LEVEL_MONO];
		break;
	case AUDIOAMD_MONITOR_VOL:
		sc->sc_mlevel = cp->un.value.level[AUDIO_MIXER_LEVEL_MONO];
		break;
	case AUDIOAMD_RECORD_SOURCE:
		if (cp->un.ord != AUDIOAMD_MIC_VOL)
			return EINVAL;
		break;
	case AUDIOAMD_MIC_MUTE:
		sc->sc_mic_mute = cp->un.ord;
		break;
	case AUDIOAMD_MONITOR_OUTPUT:
		if (cp->un.ord != AUDIOAMD_SPEAKER_VOL &&
		    cp->un.ord != AUDIOAMD_HEADPHONES_VOL)
			return EINVAL;
		sc->sc_out_port = cp->un.ord;
		break;
	default:
		return EINVAL;
		/* NOTREACHED */
	}
	return 0;
}

int
am7930_get_port(void *addr, mixer_ctrl_t *cp)
{
	struct am7930_softc *sc = addr;

	DPRINTF(("am7930_get_port: port=%d\n", cp->dev));
	if (cp->dev == AUDIOAMD_RECORD_SOURCE ||
	    cp->dev == AUDIOAMD_MONITOR_OUTPUT ||
	    cp->dev == AUDIOAMD_MIC_MUTE) {
		if (cp->type != AUDIO_MIXER_ENUM)
			return EINVAL;
	} else if (cp->type != AUDIO_MIXER_VALUE ||
	    cp->un.value.num_channels != 1) {
		return EINVAL;
	}

	switch(cp->dev) {
	case AUDIOAMD_MIC_VOL:
		cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] = sc->sc_rlevel;
		break;
	case AUDIOAMD_SPEAKER_VOL:
	case AUDIOAMD_HEADPHONES_VOL:
		cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] = sc->sc_plevel;
		break;
	case AUDIOAMD_MONITOR_VOL:
		cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] = sc->sc_mlevel;
		break;
	case AUDIOAMD_RECORD_SOURCE:
		cp->un.ord = AUDIOAMD_MIC_VOL;
		break;
	case AUDIOAMD_MIC_MUTE:
		cp->un.ord = sc->sc_mic_mute;
		break;
	case AUDIOAMD_MONITOR_OUTPUT:
		cp->un.ord = sc->sc_out_port;
		break;
	default:
		return EINVAL;
		/* NOTREACHED */
	}
	return 0;
}


/*
 * Define mixer control facilities.
 */
int
am7930_query_devinfo(void *addr, mixer_devinfo_t *dip)
{
	DPRINTF(("am7930_query_devinfo()\n"));

	switch(dip->index) {
	case AUDIOAMD_MIC_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = AUDIOAMD_INPUT_CLASS;
		dip->prev =  AUDIO_MIXER_LAST;
		dip->next = AUDIOAMD_MIC_MUTE;
		strlcpy(dip->label.name, AudioNmicrophone,
		    sizeof dip->label.name);
		dip->un.v.num_channels = 1;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case AUDIOAMD_SPEAKER_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = AUDIOAMD_OUTPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNspeaker,
		    sizeof dip->label.name);
		dip->un.v.num_channels = 1;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case AUDIOAMD_HEADPHONES_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = AUDIOAMD_OUTPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNheadphone,
		    sizeof dip->label.name);
		dip->un.v.num_channels = 1;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case AUDIOAMD_MONITOR_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = AUDIOAMD_MONITOR_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmonitor,
		    sizeof dip->label.name);
		dip->un.v.num_channels = 1;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case AUDIOAMD_RECORD_SOURCE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = AUDIOAMD_RECORD_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNsource,
		    sizeof dip->label.name);
		dip->un.e.num_mem = 1;
		strlcpy(dip->un.e.member[0].label.name, AudioNmicrophone,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = AUDIOAMD_MIC_VOL;
		break;
	case AUDIOAMD_MONITOR_OUTPUT:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = AUDIOAMD_MONITOR_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNoutput,
		    sizeof dip->label.name);
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNspeaker,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = AUDIOAMD_SPEAKER_VOL;
		strlcpy(dip->un.e.member[1].label.name, AudioNheadphone,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = AUDIOAMD_HEADPHONES_VOL;
		break;
	case AUDIOAMD_MIC_MUTE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = AUDIOAMD_INPUT_CLASS;
		dip->prev =  AUDIOAMD_MIC_VOL;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmute,
		    sizeof dip->label.name);
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNon,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		break;
	case AUDIOAMD_INPUT_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = AUDIOAMD_INPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCinputs,
		    sizeof dip->label.name);
		break;
	case AUDIOAMD_OUTPUT_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = AUDIOAMD_OUTPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCoutputs,
		    sizeof dip->label.name);
		break;
	case AUDIOAMD_RECORD_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = AUDIOAMD_RECORD_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCrecord,
		    sizeof dip->label.name);
		break;
	case AUDIOAMD_MONITOR_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = AUDIOAMD_MONITOR_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCmonitor,
		    sizeof dip->label.name);
		break;
	default:
		return ENXIO;
		/*NOTREACHED*/
	}

	DPRINTF(("AUDIO_MIXER_DEVINFO: name=%s\n", dip->label.name));

	return 0;
}
