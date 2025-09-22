/*	$OpenBSD: sbdsp.c,v 1.44 2022/11/02 10:41:34 kn Exp $	*/

/*
 * Copyright (c) 1991-1993 Regents of the University of California.
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
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * SoundBlaster Pro code provided by John Kohl, based on lots of
 * information he gleaned from Steve Haehnichen <steve@vigra.com>'s
 * SBlast driver for 386BSD and DOS driver code from Daniel Sachs
 * <sachs@meibm15.cen.uiuc.edu>.
 * Lots of rewrites by Lennart Augustsson <augustss@cs.chalmers.se>
 * with information from SB "Hardware Programming Guide" and the
 * Linux drivers.
 */

#include "midi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/device.h>
#include <sys/buf.h>
#include <sys/fcntl.h>

#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/bus.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>

#include <dev/isa/sbreg.h>
#include <dev/isa/sbdspvar.h>


#ifdef AUDIO_DEBUG
#define DPRINTF(x)	if (sbdspdebug) printf x
#define DPRINTFN(n,x)	if (sbdspdebug >= (n)) printf x
int	sbdspdebug = 0;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

#ifndef SBDSP_NPOLL
#define SBDSP_NPOLL 3000
#endif

struct {
	int wdsp;
	int rdsp;
	int wmidi;
} sberr;

/*
 * Time constant routines follow.  See SBK, section 12.
 * Although they don't come out and say it (in the docs),
 * the card clearly uses a 1MHz countdown timer, as the
 * low-speed formula (p. 12-4) is:
 *	tc = 256 - 10^6 / sr
 * In high-speed mode, the constant is the upper byte of a 16-bit counter,
 * and a 256MHz clock is used:
 *	tc = 65536 - 256 * 10^ 6 / sr
 * Since we can only use the upper byte of the HS TC, the two formulae
 * are equivalent.  (Why didn't they say so?)  E.g.,
 *	(65536 - 256 * 10 ^ 6 / x) >> 8 = 256 - 10^6 / x
 *
 * The crossover point (from low- to high-speed modes) is different
 * for the SBPRO and SB20.  The table on p. 12-5 gives the following data:
 *
 *				SBPRO			SB20
 *				-----			--------
 * input ls min			4	KHz		4	KHz
 * input ls max			23	KHz		13	KHz
 * input hs max			44.1	KHz		15	KHz
 * output ls min		4	KHz		4	KHz
 * output ls max		23	KHz		23	KHz
 * output hs max		44.1	KHz		44.1	KHz
 */
/* XXX Should we round the tc?
#define SB_RATE_TO_TC(x) (((65536 - 256 * 1000000 / (x)) + 128) >> 8)
*/
#define SB_RATE_TO_TC(x) (256 - 1000000 / (x))
#define SB_TC_TO_RATE(tc) (1000000 / (256 - (tc)))

struct sbmode {
	short	model;
	u_char	channels;
	u_char	precision;
	u_short	lowrate, highrate;
	u_char	cmd;
	u_char	cmdchan;
};
static struct sbmode sbpmodes[] = {
 { SB_1,    1,  8,  4000, 22727, SB_DSP_WDMA      },
 { SB_20,   1,  8,  4000, 22727, SB_DSP_WDMA_LOOP },
 { SB_2x,   1,  8,  4000, 22727, SB_DSP_WDMA_LOOP },
 { SB_2x,   1,  8, 22727, 45454, SB_DSP_HS_OUTPUT },
 { SB_PRO,  1,  8,  4000, 22727, SB_DSP_WDMA_LOOP },
 { SB_PRO,  1,  8, 22727, 45454, SB_DSP_HS_OUTPUT },
 { SB_PRO,  2,  8, 11025, 22727, SB_DSP_HS_OUTPUT },
 /* Yes, we write the record mode to set 16-bit playback mode. weird, huh? */
 { SB_JAZZ, 1,  8,  4000, 22727, SB_DSP_WDMA_LOOP, SB_DSP_RECORD_MONO },
 { SB_JAZZ, 1,  8, 22727, 45454, SB_DSP_HS_OUTPUT, SB_DSP_RECORD_MONO },
 { SB_JAZZ, 2,  8, 11025, 22727, SB_DSP_HS_OUTPUT, SB_DSP_RECORD_STEREO },
 { SB_JAZZ, 1, 16,  4000, 22727, SB_DSP_WDMA_LOOP, JAZZ16_RECORD_MONO },
 { SB_JAZZ, 1, 16, 22727, 45454, SB_DSP_HS_OUTPUT, JAZZ16_RECORD_MONO },
 { SB_JAZZ, 2, 16, 11025, 22727, SB_DSP_HS_OUTPUT, JAZZ16_RECORD_STEREO },
 { SB_16,   1,  8,  5000, 45000, SB_DSP16_WDMA_8  },
 { SB_16,   2,  8,  5000, 45000, SB_DSP16_WDMA_8  },
#define PLAY16 15 /* must be the index of the next entry in the table */
 { SB_16,   1, 16,  5000, 45000, SB_DSP16_WDMA_16 },
 { SB_16,   2, 16,  5000, 45000, SB_DSP16_WDMA_16 },
 { -1 }
};
static struct sbmode sbrmodes[] = {
 { SB_1,    1,  8,  4000, 12987, SB_DSP_RDMA      },
 { SB_20,   1,  8,  4000, 12987, SB_DSP_RDMA_LOOP },
 { SB_2x,   1,  8,  4000, 12987, SB_DSP_RDMA_LOOP },
 { SB_2x,   1,  8, 12987, 14925, SB_DSP_HS_INPUT  },
 { SB_PRO,  1,  8,  4000, 22727, SB_DSP_RDMA_LOOP, SB_DSP_RECORD_MONO },
 { SB_PRO,  1,  8, 22727, 45454, SB_DSP_HS_INPUT,  SB_DSP_RECORD_MONO },
 { SB_PRO,  2,  8, 11025, 22727, SB_DSP_HS_INPUT,  SB_DSP_RECORD_STEREO },
 { SB_JAZZ, 1,  8,  4000, 22727, SB_DSP_RDMA_LOOP, SB_DSP_RECORD_MONO },
 { SB_JAZZ, 1,  8, 22727, 45454, SB_DSP_HS_INPUT,  SB_DSP_RECORD_MONO },
 { SB_JAZZ, 2,  8, 11025, 22727, SB_DSP_HS_INPUT,  SB_DSP_RECORD_STEREO },
 { SB_JAZZ, 1, 16,  4000, 22727, SB_DSP_RDMA_LOOP, JAZZ16_RECORD_MONO },
 { SB_JAZZ, 1, 16, 22727, 45454, SB_DSP_HS_INPUT,  JAZZ16_RECORD_MONO },
 { SB_JAZZ, 2, 16, 11025, 22727, SB_DSP_HS_INPUT,  JAZZ16_RECORD_STEREO },
 { SB_16,   1,  8,  5000, 45000, SB_DSP16_RDMA_8  },
 { SB_16,   2,  8,  5000, 45000, SB_DSP16_RDMA_8  },
 { SB_16,   1, 16,  5000, 45000, SB_DSP16_RDMA_16 },
 { SB_16,   2, 16,  5000, 45000, SB_DSP16_RDMA_16 },
 { -1 }
};

static struct audio_params sbdsp_audio_default =
	{44100, AUDIO_ENCODING_SLINEAR_LE, 16, 2, 1, 2};

void	sbversion(struct sbdsp_softc *);
void	sbdsp_jazz16_probe(struct sbdsp_softc *);
void	sbdsp_set_mixer_gain(struct sbdsp_softc *sc, int port);
void	sbdsp_to(void *);
void	sbdsp_pause(struct sbdsp_softc *);
int	sbdsp_set_timeconst(struct sbdsp_softc *, int);
int	sbdsp16_set_rate(struct sbdsp_softc *, int, int);
int	sbdsp_set_in_ports(struct sbdsp_softc *, int);
void	sbdsp_set_ifilter(void *, int);
int	sbdsp_get_ifilter(void *);

int	sbdsp_block_output(void *);
int	sbdsp_block_input(void *);
static	int sbdsp_adjust(int, int);

int	sbdsp_midi_intr(void *);

#ifdef AUDIO_DEBUG
void	sb_printsc(struct sbdsp_softc *);

void
sb_printsc(struct sbdsp_softc *sc)
{
	int i;

	printf("open %d dmachan %d/%d %d/%d iobase 0x%x irq %d\n",
	    (int)sc->sc_open, sc->sc_i.run, sc->sc_o.run,
	    sc->sc_drq8, sc->sc_drq16,
	    sc->sc_iobase, sc->sc_irq);
	printf("irate %d itc %x orate %d otc %x\n",
	    sc->sc_i.rate, sc->sc_i.tc,
	    sc->sc_o.rate, sc->sc_o.tc);
	printf("spkron %u nintr %lu\n",
	    sc->spkr_state, sc->sc_interrupts);
	printf("intr8 %p arg8 %p\n",
	    sc->sc_intr8, sc->sc_arg16);
	printf("intr16 %p arg16 %p\n",
	    sc->sc_intr8, sc->sc_arg16);
	printf("gain:");
	for (i = 0; i < SB_NDEVS; i++)
		printf(" %u,%u", sc->gain[i][SB_LEFT], sc->gain[i][SB_RIGHT]);
	printf("\n");
}
#endif /* AUDIO_DEBUG */

/*
 * Probe / attach routines.
 */

/*
 * Probe for the soundblaster hardware.
 */
int
sbdsp_probe(struct sbdsp_softc *sc)
{

	if (sbdsp_reset(sc) < 0) {
		DPRINTF(("sbdsp: couldn't reset card\n"));
		return 0;
	}
	/* if flags set, go and probe the jazz16 stuff */
	if (sc->sc_dev.dv_cfdata->cf_flags & 1)
		sbdsp_jazz16_probe(sc);
	else
		sbversion(sc);
	if (sc->sc_model == SB_UNK) {
		/* Unknown SB model found. */
		DPRINTF(("sbdsp: unknown SB model found\n"));
		return 0;
	}
	return 1;
}

/*
 * Try add-on stuff for Jazz16.
 */
void
sbdsp_jazz16_probe(struct sbdsp_softc *sc)
{
	static u_char jazz16_irq_conf[16] = {
	    -1, -1, 0x02, 0x03,
	    -1, 0x01, -1, 0x04,
	    -1, 0x02, 0x05, -1,
	    -1, -1, -1, 0x06};
	static u_char jazz16_drq_conf[8] = {
	    -1, 0x01, -1, 0x02,
	    -1, 0x03, -1, 0x04};

	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh;

	sbversion(sc);

	DPRINTF(("jazz16 probe\n"));

	if (bus_space_map(iot, JAZZ16_CONFIG_PORT, 1, 0, &ioh)) {
		DPRINTF(("bus map failed\n"));
		return;
	}

	if (jazz16_drq_conf[sc->sc_drq8] == (u_char)-1 ||
	    jazz16_irq_conf[sc->sc_irq] == (u_char)-1) {
		DPRINTF(("drq/irq check failed\n"));
		goto done;		/* give up, we can't do it. */
	}

	bus_space_write_1(iot, ioh, 0, JAZZ16_WAKEUP);
	delay(10000);			/* delay 10 ms */
	bus_space_write_1(iot, ioh, 0, JAZZ16_SETBASE);
	bus_space_write_1(iot, ioh, 0, sc->sc_iobase & 0x70);

	if (sbdsp_reset(sc) < 0) {
		DPRINTF(("sbdsp_reset check failed\n"));
		goto done;		/* XXX? what else could we do? */
	}

	if (sbdsp_wdsp(sc, JAZZ16_READ_VER)) {
		DPRINTF(("read16 setup failed\n"));
		goto done;
	}

	if (sbdsp_rdsp(sc) != JAZZ16_VER_JAZZ) {
		DPRINTF(("read16 failed\n"));
		goto done;
	}

	/* XXX set both 8 & 16-bit drq to same channel, it works fine. */
	sc->sc_drq16 = sc->sc_drq8;
	if (sbdsp_wdsp(sc, JAZZ16_SET_DMAINTR) ||
	    sbdsp_wdsp(sc, (jazz16_drq_conf[sc->sc_drq16] << 4) |
		jazz16_drq_conf[sc->sc_drq8]) ||
	    sbdsp_wdsp(sc, jazz16_irq_conf[sc->sc_irq])) {
		DPRINTF(("sbdsp: can't write jazz16 probe stuff\n"));
	} else {
		DPRINTF(("jazz16 detected!\n"));
		sc->sc_model = SB_JAZZ;
		sc->sc_mixer_model = SBM_CT1345; /* XXX really? */
	}

done:
	bus_space_unmap(iot, ioh, 1);
}

/*
 * Attach hardware to driver, attach hardware driver to audio
 * pseudo-device driver .
 */
void
sbdsp_attach(struct sbdsp_softc *sc)
{
	struct audio_params pparams, rparams;
        int i;
        u_int v;

	/*
	 * Create our DMA maps.
	 */
	if (sc->sc_drq8 != -1) {
		if (isa_dmamap_create(sc->sc_isa, sc->sc_drq8,
		    MAX_ISADMA, BUS_DMA_NOWAIT|BUS_DMA_ALLOCNOW)) {
			printf("%s: can't create map for drq %d\n",
			    sc->sc_dev.dv_xname, sc->sc_drq8);
			return;
		}
	}
	if (sc->sc_drq16 != -1 && sc->sc_drq16 != sc->sc_drq8) {
		if (isa_dmamap_create(sc->sc_isa, sc->sc_drq16,
		    MAX_ISADMA, BUS_DMA_NOWAIT|BUS_DMA_ALLOCNOW)) {
			printf("%s: can't create map for drq %d\n",
			    sc->sc_dev.dv_xname, sc->sc_drq16);
			return;
		}
	}

	pparams = sbdsp_audio_default;
	rparams = sbdsp_audio_default;
        sbdsp_set_params(sc, AUMODE_RECORD|AUMODE_PLAY, 0, &pparams, &rparams);

	sbdsp_set_in_ports(sc, 1 << SB_MIC_VOL);

	if (sc->sc_mixer_model != SBM_NONE) {
		/* Reset the mixer.*/
		sbdsp_mix_write(sc, SBP_MIX_RESET, SBP_MIX_RESET);
                /* And set our own default values */
		for (i = 0; i < SB_NDEVS; i++) {
			switch(i) {
			case SB_MIC_VOL:
			case SB_LINE_IN_VOL:
				v = 0;
				break;
			case SB_BASS:
			case SB_TREBLE:
				v = SB_ADJUST_GAIN(sc, AUDIO_MAX_GAIN/2);
				break;
			case SB_CD_IN_MUTE:
			case SB_MIC_IN_MUTE:
			case SB_LINE_IN_MUTE:
			case SB_MIDI_IN_MUTE:
			case SB_CD_SWAP:
			case SB_MIC_SWAP:
			case SB_LINE_SWAP:
			case SB_MIDI_SWAP:
			case SB_CD_OUT_MUTE:
			case SB_MIC_OUT_MUTE:
			case SB_LINE_OUT_MUTE:
				v = 0;
				break;
			default:
				v = SB_ADJUST_GAIN(sc, AUDIO_MAX_GAIN / 2);
				break;
			}
			sc->gain[i][SB_LEFT] = sc->gain[i][SB_RIGHT] = v;
			sbdsp_set_mixer_gain(sc, i);
		}
		sc->in_filter = 0;	/* no filters turned on, please */
	}

	printf(": dsp v%d.%02d%s\n",
	       SBVER_MAJOR(sc->sc_version), SBVER_MINOR(sc->sc_version),
	       sc->sc_model == SB_JAZZ ? ": <Jazz16>" : "");

	timeout_set(&sc->sc_tmo, sbdsp_to, sbdsp_to);
	sc->sc_fullduplex = ISSB16CLASS(sc) &&
		sc->sc_drq8 != -1 && sc->sc_drq16 != -1 &&
		sc->sc_drq8 != sc->sc_drq16;
}

void
sbdsp_mix_write(struct sbdsp_softc *sc, int mixerport, int val)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	mtx_enter(&audio_lock);
	bus_space_write_1(iot, ioh, SBP_MIXER_ADDR, mixerport);
	delay(20);
	bus_space_write_1(iot, ioh, SBP_MIXER_DATA, val);
	delay(30);
	mtx_leave(&audio_lock);
}

int
sbdsp_mix_read(struct sbdsp_softc *sc, int mixerport)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int val;

	mtx_enter(&audio_lock);
	bus_space_write_1(iot, ioh, SBP_MIXER_ADDR, mixerport);
	delay(20);
	val = bus_space_read_1(iot, ioh, SBP_MIXER_DATA);
	delay(30);
	mtx_leave(&audio_lock);
	return val;
}

/*
 * Various routines to interface to higher level audio driver
 */

int
sbdsp_set_params(void *addr, int setmode, int usemode,
    struct audio_params *play, struct audio_params *rec)
{
	struct sbdsp_softc *sc = addr;
	struct sbmode *m;
	u_int rate, tc, bmode;
	int model;
	int chan;
	struct audio_params *p;
	int mode;

	if (sc->sc_open == SB_OPEN_MIDI)
		return EBUSY;

	model = sc->sc_model;
	if (model > SB_16)
		model = SB_16;	/* later models work like SB16 */

	/*
	 * Prior to the SB16, we have only one clock, so make the sample
	 * rates match.
	 */
	if (!ISSB16CLASS(sc) &&
	    play->sample_rate != rec->sample_rate &&
	    usemode == (AUMODE_PLAY | AUMODE_RECORD)) {
		if (setmode == AUMODE_PLAY) {
			rec->sample_rate = play->sample_rate;
			setmode |= AUMODE_RECORD;
		} else if (setmode == AUMODE_RECORD) {
			play->sample_rate = rec->sample_rate;
			setmode |= AUMODE_PLAY;
		} else
			return (EINVAL);
	}

	/* Set first record info, then play info */
	for (mode = AUMODE_RECORD; mode != -1;
	     mode = mode == AUMODE_RECORD ? AUMODE_PLAY : -1) {
		if ((setmode & mode) == 0)
			continue;

		p = mode == AUMODE_PLAY ? play : rec;

		switch (model) {
		case SB_1:
		case SB_20:
			if (mode == AUMODE_PLAY) {
				if (p->sample_rate < 4000)
					p->sample_rate = 4000;
				else if (p->sample_rate > 22727)
					p->sample_rate = 22727; /* 22050 ? */
			} else {
				if (p->sample_rate < 4000)
					p->sample_rate = 4000;
				else if (p->sample_rate > 12987)
					p->sample_rate = 12987;
			}
			break;
		case SB_2x:
			if (mode == AUMODE_PLAY) {
				if (p->sample_rate < 4000)
					p->sample_rate = 4000;
				else if (p->sample_rate > 45454)
					p->sample_rate = 45454; /* 44100 ? */
			} else {
				if (p->sample_rate < 4000)
					p->sample_rate = 4000;
				else if (p->sample_rate > 14925)
					p->sample_rate = 14925; /* ??? */
			}
			break;
		case SB_PRO:
		case SB_JAZZ:
			if (p->channels == 2) {
				if (p->sample_rate < 11025)
					p->sample_rate = 11025;
				else if (p->sample_rate > 22727)
					p->sample_rate = 22727; /* 22050 ? */
			} else {
				if (p->sample_rate < 4000)
					p->sample_rate = 4000;
				else if (p->sample_rate > 45454)
					p->sample_rate = 45454; /* 44100 ? */
			}
			break;
		case SB_16:
			if (p->sample_rate < 5000)
				p->sample_rate = 5000;
			else if (p->sample_rate > 45000)
				p->sample_rate = 45000; /* 44100 ? */
			break;
		}

		/* Locate proper commands */
		for(m = mode == AUMODE_PLAY ? sbpmodes : sbrmodes;
		    m->model != -1; m++) {
			if (model == m->model &&
			    p->channels == m->channels &&
			    p->precision == m->precision &&
			    p->sample_rate >= m->lowrate &&
			    p->sample_rate <= m->highrate)
				break;
		}
		if (m->model == -1)
			return EINVAL;
		rate = p->sample_rate;
		tc = 1;
		bmode = -1;
		if (model == SB_16) {
			switch (p->encoding) {
			case AUDIO_ENCODING_SLINEAR_BE:
				if (p->precision == 16)
					return EINVAL;
				/* fall into */
			case AUDIO_ENCODING_SLINEAR_LE:
				bmode = SB_BMODE_SIGNED;
				break;
			case AUDIO_ENCODING_ULINEAR_BE:
				if (p->precision == 16)
					return EINVAL;
				/* fall into */
			case AUDIO_ENCODING_ULINEAR_LE:
				bmode = SB_BMODE_UNSIGNED;
				break;
			default:
				return EINVAL;
			}
			if (p->channels == 2)
				bmode |= SB_BMODE_STEREO;
		} else if (m->model == SB_JAZZ && m->precision == 16) {
			switch (p->encoding) {
			case AUDIO_ENCODING_SLINEAR_LE:
				break;
			default:
				return EINVAL;
			}
			tc = SB_RATE_TO_TC(p->sample_rate * p->channels);
			p->sample_rate = SB_TC_TO_RATE(tc) / p->channels;
		} else {
			switch (p->encoding) {
			case AUDIO_ENCODING_ULINEAR_BE:
			case AUDIO_ENCODING_ULINEAR_LE:
				break;
			default:
				return EINVAL;
			}
			tc = SB_RATE_TO_TC(p->sample_rate * p->channels);
			p->sample_rate = SB_TC_TO_RATE(tc) / p->channels;
		}

		chan = m->precision == 16 ? sc->sc_drq16 : sc->sc_drq8;
		if (mode == AUMODE_PLAY) {
			sc->sc_o.rate = rate;
			sc->sc_o.tc = tc;
			sc->sc_o.modep = m;
			sc->sc_o.bmode = bmode;
			sc->sc_o.dmachan = chan;
		} else {
			sc->sc_i.rate = rate;
			sc->sc_i.tc = tc;
			sc->sc_i.modep = m;
			sc->sc_i.bmode = bmode;
			sc->sc_i.dmachan = chan;
		}

		p->bps = AUDIO_BPS(p->precision);
		p->msb = 1;
		DPRINTF(("sbdsp_set_params: model=%d, mode=%d, rate=%ld, prec=%d, chan=%d, enc=%d -> tc=%02x, cmd=%02x, bmode=%02x, cmdchan=%02x\n",
			 sc->sc_model, mode, p->sample_rate, p->precision, p->channels,
			 p->encoding, tc, m->cmd, bmode, m->cmdchan));

	}

	/*
	 * XXX
	 * Should wait for chip to be idle.
	 */
	sc->sc_i.run = SB_NOTRUNNING;
	sc->sc_o.run = SB_NOTRUNNING;

	if (sc->sc_fullduplex &&
	    usemode == (AUMODE_PLAY | AUMODE_RECORD) &&
	    sc->sc_i.dmachan == sc->sc_o.dmachan) {
		DPRINTF(("sbdsp_set_params: fd=%d, usemode=%d, idma=%d, odma=%d\n", sc->sc_fullduplex, usemode, sc->sc_i.dmachan, sc->sc_o.dmachan));
		return EINVAL;
	}
	DPRINTF(("sbdsp_set_params ichan=%d, ochan=%d\n",
		 sc->sc_i.dmachan, sc->sc_o.dmachan));

	return 0;
}

void
sbdsp_set_ifilter(void *addr, int which)
{
	struct sbdsp_softc *sc = addr;
	int mixval;

	mixval = sbdsp_mix_read(sc, SBP_INFILTER) & ~SBP_IFILTER_MASK;
	switch (which) {
	case 0:
		mixval |= SBP_FILTER_OFF;
		break;
	case SB_TREBLE:
		mixval |= SBP_FILTER_ON | SBP_IFILTER_HIGH;
		break;
	case SB_BASS:
		mixval |= SBP_FILTER_ON | SBP_IFILTER_LOW;
		break;
	default:
		return;
	}
	sc->in_filter = mixval & SBP_IFILTER_MASK;
	sbdsp_mix_write(sc, SBP_INFILTER, mixval);
}

int
sbdsp_get_ifilter(void *addr)
{
	struct sbdsp_softc *sc = addr;

	sc->in_filter =
		sbdsp_mix_read(sc, SBP_INFILTER) & SBP_IFILTER_MASK;
	switch (sc->in_filter) {
	case SBP_FILTER_ON|SBP_IFILTER_HIGH:
		return SB_TREBLE;
	case SBP_FILTER_ON|SBP_IFILTER_LOW:
		return SB_BASS;
	default:
		return 0;
	}
}

int
sbdsp_set_in_ports(struct sbdsp_softc *sc, int mask)
{
	int bitsl, bitsr;
	int sbport;

	if (sc->sc_open == SB_OPEN_MIDI)
		return EBUSY;

	DPRINTF(("sbdsp_set_in_ports: model=%d, mask=%x\n",
		 sc->sc_mixer_model, mask));

	switch(sc->sc_mixer_model) {
	case SBM_NONE:
		return EINVAL;
	case SBM_CT1335:
		if (mask != (1 << SB_MIC_VOL))
			return EINVAL;
		break;
	case SBM_CT1345:
		switch (mask) {
		case 1 << SB_MIC_VOL:
			sbport = SBP_FROM_MIC;
			break;
		case 1 << SB_LINE_IN_VOL:
			sbport = SBP_FROM_LINE;
			break;
		case 1 << SB_CD_VOL:
			sbport = SBP_FROM_CD;
			break;
		default:
			return (EINVAL);
		}
		sbdsp_mix_write(sc, SBP_RECORD_SOURCE, sbport | sc->in_filter);
		break;
	case SBM_CT1XX5:
	case SBM_CT1745:
		if (mask & ~((1<<SB_MIDI_VOL) | (1<<SB_LINE_IN_VOL) |
			     (1<<SB_CD_VOL) | (1<<SB_MIC_VOL)))
			return EINVAL;
		bitsr = 0;
		if (mask & (1<<SB_MIDI_VOL))    bitsr |= SBP_MIDI_SRC_R;
		if (mask & (1<<SB_LINE_IN_VOL)) bitsr |= SBP_LINE_SRC_R;
		if (mask & (1<<SB_CD_VOL))      bitsr |= SBP_CD_SRC_R;
		bitsl = SB_SRC_R_TO_L(bitsr);
		if (mask & (1<<SB_MIC_VOL)) {
			bitsl |= SBP_MIC_SRC;
			bitsr |= SBP_MIC_SRC;
		}
		sbdsp_mix_write(sc, SBP_RECORD_SOURCE_L, bitsl);
		sbdsp_mix_write(sc, SBP_RECORD_SOURCE_R, bitsr);
		break;
	}
	sc->in_mask = mask;

	return 0;
}

int
sbdsp_speaker_ctl(void *addr, int newstate)
{
	struct sbdsp_softc *sc = addr;

	if (sc->sc_open == SB_OPEN_MIDI)
		return EBUSY;

	if ((newstate == SPKR_ON) &&
	    (sc->spkr_state == SPKR_OFF)) {
		sbdsp_spkron(sc);
		sc->spkr_state = SPKR_ON;
	}
	if ((newstate == SPKR_OFF) &&
	    (sc->spkr_state == SPKR_ON)) {
		sbdsp_spkroff(sc);
		sc->spkr_state = SPKR_OFF;
	}
	return 0;
}

int
sbdsp_round_blocksize(void *addr, int blk)
{
	return (blk + 3) & -4;	/* round to biggest sample size */
}

int
sbdsp_open(void *addr, int flags)
{
	struct sbdsp_softc *sc = addr;

        DPRINTF(("sbdsp_open: sc=%p\n", sc));

	if ((flags & (FWRITE | FREAD)) == (FWRITE | FREAD) &&
	    !sc->sc_fullduplex)
		return ENXIO;
	if (sc->sc_open != SB_CLOSED)
		return EBUSY;
	if (sbdsp_reset(sc) != 0)
		return EIO;

	sbdsp_speaker_ctl(sc, (flags & FWRITE) ? SPKR_ON : SPKR_OFF);

	sc->sc_open = SB_OPEN_AUDIO;
	sc->sc_openflags = flags;
	sc->sc_intrm = 0;
	if (ISSBPRO(sc) &&
	    sbdsp_wdsp(sc, SB_DSP_RECORD_MONO) < 0) {
		DPRINTF(("sbdsp_open: can't set mono mode\n"));
		/* we'll readjust when it's time for DMA. */
	}

	/*
	 * Leave most things as they were; users must change things if
	 * the previous process didn't leave it they way they wanted.
	 * Looked at another way, it's easy to set up a configuration
	 * in one program and leave it for another to inherit.
	 */
	DPRINTF(("sbdsp_open: opened\n"));

	return 0;
}

void
sbdsp_close(void *addr)
{
	struct sbdsp_softc *sc = addr;

        DPRINTF(("sbdsp_close: sc=%p\n", sc));

	sc->sc_open = SB_CLOSED;
	sbdsp_spkroff(sc);
	sc->spkr_state = SPKR_OFF;
	sc->sc_intr8 = 0;
	sc->sc_intr16 = 0;
	sc->sc_intrm = 0;
	sbdsp_haltdma(sc);

	DPRINTF(("sbdsp_close: closed\n"));
}

/*
 * Lower-level routines
 */

/*
 * Reset the card.
 * Return non-zero if the card isn't detected.
 */
int
sbdsp_reset(struct sbdsp_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	sc->sc_intr8 = 0;
	sc->sc_intr16 = 0;
	if (sc->sc_i.run != SB_NOTRUNNING) {
		isa_dmaabort(sc->sc_isa, sc->sc_i.dmachan);
		sc->sc_i.run = SB_NOTRUNNING;
	}
	if (sc->sc_o.run != SB_NOTRUNNING) {
		isa_dmaabort(sc->sc_isa, sc->sc_o.dmachan);
		sc->sc_o.run = SB_NOTRUNNING;
	}

	/*
	 * See SBK, section 11.3.
	 * We pulse a reset signal into the card.
	 * Gee, what a brilliant hardware design.
	 */
	bus_space_write_1(iot, ioh, SBP_DSP_RESET, 1);
	delay(10);
	bus_space_write_1(iot, ioh, SBP_DSP_RESET, 0);
	delay(30);
	if (sbdsp_rdsp(sc) != SB_MAGIC)
		return -1;

	return 0;
}

/*
 * Write a byte to the dsp.
 * We are at the mercy of the card as we use a
 * polling loop and wait until it can take the byte.
 */
int
sbdsp_wdsp(struct sbdsp_softc *sc, int v)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int i;
	u_char x;

	for (i = SBDSP_NPOLL; --i >= 0; ) {
		x = bus_space_read_1(iot, ioh, SBP_DSP_WSTAT);
		delay(10);
		if ((x & SB_DSP_BUSY) == 0) {
			bus_space_write_1(iot, ioh, SBP_DSP_WRITE, v);
			delay(10);
			return 0;
		}
	}
	++sberr.wdsp;
	return -1;
}

/*
 * Read a byte from the DSP, using polling.
 */
int
sbdsp_rdsp(struct sbdsp_softc *sc)
{
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;
	int i;
	u_char x;

	for (i = SBDSP_NPOLL; --i >= 0; ) {
		x = bus_space_read_1(iot, ioh, SBP_DSP_RSTAT);
		delay(10);
		if (x & SB_DSP_READY) {
			x = bus_space_read_1(iot, ioh, SBP_DSP_READ);
			delay(10);
			return x;
		}
	}
	++sberr.rdsp;
	return -1;
}

/*
 * Doing certain things (like toggling the speaker) make
 * the SB hardware go away for a while, so pause a little.
 */
void
sbdsp_to(void *arg)
{
	wakeup(arg);
}

void
sbdsp_pause(struct sbdsp_softc *sc)
{
	timeout_add_msec(&sc->sc_tmo, 125);	/* 8x per second */
	tsleep_nsec(sbdsp_to, PWAIT, "sbpause", INFSLP);
}

/*
 * Turn on the speaker.  The SBK documentation says this operation
 * can take up to 1/10 of a second.  Higher level layers should
 * probably let the task sleep for this amount of time after
 * calling here.  Otherwise, things might not work (because
 * sbdsp_wdsp() and sbdsp_rdsp() will probably timeout.)
 *
 * These engineers had their heads up their ass when
 * they designed this card.
 */
void
sbdsp_spkron(struct sbdsp_softc *sc)
{
	(void)sbdsp_wdsp(sc, SB_DSP_SPKR_ON);
	sbdsp_pause(sc);
}

/*
 * Turn off the speaker; see comment above.
 */
void
sbdsp_spkroff(struct sbdsp_softc *sc)
{
	(void)sbdsp_wdsp(sc, SB_DSP_SPKR_OFF);
	sbdsp_pause(sc);
}

/*
 * Read the version number out of the card.
 * Store version information in the softc.
 */
void
sbversion(struct sbdsp_softc *sc)
{
	int v;

	sc->sc_model = SB_UNK;
	sc->sc_version = 0;
	if (sbdsp_wdsp(sc, SB_DSP_VERSION) < 0)
		return;
	v = sbdsp_rdsp(sc) << 8;
	v |= sbdsp_rdsp(sc);
	if (v < 0)
		return;
	sc->sc_version = v;
	switch(SBVER_MAJOR(v)) {
	case 1:
		sc->sc_mixer_model = SBM_NONE;
		sc->sc_model = SB_1;
		break;
	case 2:
		/* Some SB2 have a mixer, some don't. */
		sbdsp_mix_write(sc, SBP_1335_MASTER_VOL, 0x04);
		sbdsp_mix_write(sc, SBP_1335_MIDI_VOL,   0x06);
		/* Check if we can read back the mixer values. */
		if ((sbdsp_mix_read(sc, SBP_1335_MASTER_VOL) & 0x0e) == 0x04 &&
		    (sbdsp_mix_read(sc, SBP_1335_MIDI_VOL)   & 0x0e) == 0x06)
			sc->sc_mixer_model = SBM_CT1335;
		else
			sc->sc_mixer_model = SBM_NONE;
		if (SBVER_MINOR(v) == 0)
			sc->sc_model = SB_20;
		else
			sc->sc_model = SB_2x;
		break;
	case 3:
		sc->sc_mixer_model = SBM_CT1345;
		sc->sc_model = SB_PRO;
		break;
	case 4:
#if 0
/* XXX This does not work */
		/* Most SB16 have a tone controls, but some don't. */
		sbdsp_mix_write(sc, SB16P_TREBLE_L, 0x80);
		/* Check if we can read back the mixer value. */
		if ((sbdsp_mix_read(sc, SB16P_TREBLE_L) & 0xf0) == 0x80)
			sc->sc_mixer_model = SBM_CT1745;
		else
			sc->sc_mixer_model = SBM_CT1XX5;
#else
		sc->sc_mixer_model = SBM_CT1745;
#endif
#if 0
/* XXX figure out a good way of determining the model */
		/* XXX what about SB_32 */
		if (SBVER_MINOR(v) == 16)
			sc->sc_model = SB_64;
		else
#endif
			sc->sc_model = SB_16;
		break;
	}
}

/*
 * Halt a DMA in progress.
 */
int
sbdsp_haltdma(void *addr)
{
	struct sbdsp_softc *sc = addr;

	DPRINTF(("sbdsp_haltdma: sc=%p\n", sc));

	mtx_enter(&audio_lock);
	sbdsp_reset(sc);
	mtx_leave(&audio_lock);
	return 0;
}

int
sbdsp_set_timeconst(struct sbdsp_softc *sc, int tc)
{
	DPRINTF(("sbdsp_set_timeconst: sc=%p tc=%d\n", sc, tc));

	if (sbdsp_wdsp(sc, SB_DSP_TIMECONST) < 0 ||
	    sbdsp_wdsp(sc, tc) < 0)
		return EIO;

	return 0;
}

int
sbdsp16_set_rate(struct sbdsp_softc *sc, int cmd, int rate)
{
	DPRINTF(("sbdsp16_set_rate: sc=%p cmd=0x%02x rate=%d\n", sc, cmd, rate));

	if (sbdsp_wdsp(sc, cmd) < 0 ||
	    sbdsp_wdsp(sc, rate >> 8) < 0 ||
	    sbdsp_wdsp(sc, rate) < 0)
		return EIO;
	return 0;
}

int
sbdsp_trigger_input(void *addr, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct sbdsp_softc *sc = addr;
	int stereo = param->channels == 2;
	int width = param->precision;
	int filter;
	int rc;

#ifdef DIAGNOSTIC
	if (stereo && (blksize & 1)) {
		DPRINTF(("stereo record odd bytes (%d)\n", blksize));
		return (EIO);
	}
#endif

	sc->sc_intrr = intr;
	sc->sc_argr = arg;

	if (width == 8) {
#ifdef DIAGNOSTIC
		if (sc->sc_i.dmachan != sc->sc_drq8) {
			printf("sbdsp_trigger_input: width=%d bad chan %d\n",
			    width, sc->sc_i.dmachan);			
			return (EIO);
		}
#endif
		sc->sc_intr8 = sbdsp_block_input;
		sc->sc_arg8 = addr;
	} else {
#ifdef DIAGNOSTIC
		if (sc->sc_i.dmachan != sc->sc_drq16) {
			printf("sbdsp_trigger_input: width=%d bad chan %d\n",
			    width, sc->sc_i.dmachan);
			return (EIO);
		}
#endif
		sc->sc_intr16 = sbdsp_block_input;
		sc->sc_arg16 = addr;
	}

	if ((sc->sc_model == SB_JAZZ) ? (sc->sc_i.dmachan > 3) : (width == 16))
		blksize >>= 1;
	--blksize;
	sc->sc_i.blksize = blksize;

	if (ISSBPRO(sc)) {
		if (sbdsp_wdsp(sc, sc->sc_i.modep->cmdchan) < 0)
			return (EIO);
		filter = stereo ? SBP_FILTER_OFF : sc->in_filter;
		sbdsp_mix_write(sc, SBP_INFILTER,
		    (sbdsp_mix_read(sc, SBP_INFILTER) & ~SBP_IFILTER_MASK) |
		    filter);
	}

	if (ISSB16CLASS(sc)) {
		if (sbdsp16_set_rate(sc, SB_DSP16_INPUTRATE, sc->sc_i.rate)) {
			DPRINTF(("sbdsp_trigger_input: rate=%d set failed\n",
				 sc->sc_i.rate));
			return (EIO);
		}
	} else {
		if (sbdsp_set_timeconst(sc, sc->sc_i.tc)) {
			DPRINTF(("sbdsp_trigger_input: tc=%d set failed\n",
				 sc->sc_i.rate));
			return (EIO);
		}
	}

	DPRINTF(("sbdsp: dma start loop input start=%p end=%p chan=%d\n",
	    start, end, sc->sc_i.dmachan));
	mtx_enter(&audio_lock);
	isa_dmastart(sc->sc_isa, sc->sc_i.dmachan, start, (char *)end -
	    (char *)start, NULL, DMAMODE_READ | DMAMODE_LOOP, BUS_DMA_NOWAIT);
	rc = sbdsp_block_input(addr);
	mtx_leave(&audio_lock);
	return rc;
}

int
sbdsp_block_input(void *addr)
{
	struct sbdsp_softc *sc = addr;
	int cc = sc->sc_i.blksize;

	DPRINTFN(2, ("sbdsp_block_input: sc=%p cc=%d\n", addr, cc));

	if (sc->sc_i.run != SB_NOTRUNNING)
		sc->sc_intrr(sc->sc_argr);

	if (sc->sc_model == SB_1) {
		/* Non-looping mode, start DMA */
		if (sbdsp_wdsp(sc, sc->sc_i.modep->cmd) < 0 ||
		    sbdsp_wdsp(sc, cc) < 0 ||
		    sbdsp_wdsp(sc, cc >> 8) < 0) {
			DPRINTF(("sbdsp_block_input: SB1 DMA start failed\n"));
			return (EIO);
		}
		sc->sc_i.run = SB_RUNNING;
	} else if (sc->sc_i.run == SB_NOTRUNNING) {
		/* Initialize looping PCM */
		if (ISSB16CLASS(sc)) {
			DPRINTFN(3, ("sbdsp16 input command cmd=0x%02x bmode=0x%02x cc=%d\n",
			    sc->sc_i.modep->cmd, sc->sc_i.bmode, cc));
			if (sbdsp_wdsp(sc, sc->sc_i.modep->cmd) < 0 ||
			    sbdsp_wdsp(sc, sc->sc_i.bmode) < 0 ||
			    sbdsp_wdsp(sc, cc) < 0 ||
			    sbdsp_wdsp(sc, cc >> 8) < 0) {
				DPRINTF(("sbdsp_block_input: SB16 DMA start failed\n"));
				return (EIO);
			}
		} else {
			DPRINTF(("sbdsp_block_input: set blocksize=%d\n", cc));
			if (sbdsp_wdsp(sc, SB_DSP_BLOCKSIZE) < 0 ||
			    sbdsp_wdsp(sc, cc) < 0 ||
			    sbdsp_wdsp(sc, cc >> 8) < 0) {
				DPRINTF(("sbdsp_block_input: SB2 DMA blocksize failed\n"));
				return (EIO);
			}
			if (sbdsp_wdsp(sc, sc->sc_i.modep->cmd) < 0) {
				DPRINTF(("sbdsp_block_input: SB2 DMA start failed\n"));
				return (EIO);
			}
		}
		sc->sc_i.run = SB_LOOPING;
	}

	return (0);
}

int
sbdsp_trigger_output(void *addr, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct sbdsp_softc *sc = addr;
	int stereo = param->channels == 2;
	int width = param->precision;
	int cmd;
	int rc;

#ifdef DIAGNOSTIC
	if (stereo && (blksize & 1)) {
		DPRINTF(("stereo playback odd bytes (%d)\n", blksize));
		return (EIO);
	}
#endif

	sc->sc_intrp = intr;
	sc->sc_argp = arg;

	if (width == 8) {
#ifdef DIAGNOSTIC
		if (sc->sc_o.dmachan != sc->sc_drq8) {
			printf("sbdsp_trigger_output: width=%d bad chan %d\n",
			    width, sc->sc_o.dmachan);
			return (EIO);
		}
#endif
		sc->sc_intr8 = sbdsp_block_output;
		sc->sc_arg8 = addr;
	} else {
#ifdef DIAGNOSTIC
		if (sc->sc_o.dmachan != sc->sc_drq16) {
			printf("sbdsp_trigger_output: width=%d bad chan %d\n",
			    width, sc->sc_o.dmachan);
			return (EIO);
		}
#endif
		sc->sc_intr16 = sbdsp_block_output;
		sc->sc_arg16 = addr;
	}

	if ((sc->sc_model == SB_JAZZ) ? (sc->sc_o.dmachan > 3) : (width == 16))
		blksize >>= 1;
	--blksize;
	sc->sc_o.blksize = blksize;

	if (ISSBPRO(sc)) {
		/* make sure we re-set stereo mixer bit when we start output. */
		sbdsp_mix_write(sc, SBP_STEREO,
		    (sbdsp_mix_read(sc, SBP_STEREO) & ~SBP_PLAYMODE_MASK) |
		    (stereo ?  SBP_PLAYMODE_STEREO : SBP_PLAYMODE_MONO));
		cmd = sc->sc_o.modep->cmdchan;
		if (cmd && sbdsp_wdsp(sc, cmd) < 0)
			return (EIO);
	}

	if (ISSB16CLASS(sc)) {
		if (sbdsp16_set_rate(sc, SB_DSP16_OUTPUTRATE, sc->sc_o.rate)) {
			DPRINTF(("sbdsp_trigger_output: rate=%d set failed\n",
				 sc->sc_o.rate));
			return (EIO);
		}
	} else {
		if (sbdsp_set_timeconst(sc, sc->sc_o.tc)) {
			DPRINTF(("sbdsp_trigger_output: tc=%d set failed\n",
				 sc->sc_o.rate));
			return (EIO);
		}
	}

	DPRINTF(("sbdsp: dma start loop output start=%p end=%p chan=%d\n",
	    start, end, sc->sc_o.dmachan));
	mtx_enter(&audio_lock);
	isa_dmastart(sc->sc_isa, sc->sc_o.dmachan, start, (char *)end -
	    (char *)start, NULL, DMAMODE_WRITE | DMAMODE_LOOP, BUS_DMA_NOWAIT);
	rc = sbdsp_block_output(addr);
	mtx_leave(&audio_lock);
	return rc;
}

int
sbdsp_block_output(void *addr)
{
	struct sbdsp_softc *sc = addr;
	int cc = sc->sc_o.blksize;

	DPRINTFN(2, ("sbdsp_block_output: sc=%p cc=%d\n", addr, cc));

	if (sc->sc_o.run != SB_NOTRUNNING)
		sc->sc_intrp(sc->sc_argp);

	if (sc->sc_model == SB_1) {
		/* Non-looping mode, initialized. Start DMA and PCM */
		if (sbdsp_wdsp(sc, sc->sc_o.modep->cmd) < 0 ||
		    sbdsp_wdsp(sc, cc) < 0 ||
		    sbdsp_wdsp(sc, cc >> 8) < 0) {
			DPRINTF(("sbdsp_block_output: SB1 DMA start failed\n"));
			return (EIO);
		}
		sc->sc_o.run = SB_RUNNING;
	} else if (sc->sc_o.run == SB_NOTRUNNING) {
		/* Initialize looping PCM */
		if (ISSB16CLASS(sc)) {
			DPRINTF(("sbdsp_block_output: SB16 cmd=0x%02x bmode=0x%02x cc=%d\n",
			    sc->sc_o.modep->cmd,sc->sc_o.bmode, cc));
			if (sbdsp_wdsp(sc, sc->sc_o.modep->cmd) < 0 ||
			    sbdsp_wdsp(sc, sc->sc_o.bmode) < 0 ||
			    sbdsp_wdsp(sc, cc) < 0 ||
			    sbdsp_wdsp(sc, cc >> 8) < 0) {
				DPRINTF(("sbdsp_block_output: SB16 DMA start failed\n"));
				return (EIO);
			}
		} else {
			DPRINTF(("sbdsp_block_output: set blocksize=%d\n", cc));
			if (sbdsp_wdsp(sc, SB_DSP_BLOCKSIZE) < 0 ||
			    sbdsp_wdsp(sc, cc) < 0 ||
			    sbdsp_wdsp(sc, cc >> 8) < 0) {
				DPRINTF(("sbdsp_block_output: SB2 DMA blocksize failed\n"));
				return (EIO);
			}
			if (sbdsp_wdsp(sc, sc->sc_o.modep->cmd) < 0) {
				DPRINTF(("sbdsp_block_output: SB2 DMA start failed\n"));
				return (EIO);
			}
		}
		sc->sc_o.run = SB_LOOPING;
	}

	return (0);
}

/*
 * Only the DSP unit on the sound blaster generates interrupts.
 * There are three cases of interrupt: reception of a midi byte
 * (when mode is enabled), completion of dma transmission, or
 * completion of a dma reception.
 *
 * If there is interrupt sharing or a spurious interrupt occurs
 * there is no way to distinguish this on an SB2.  So if you have
 * an SB2 and experience problems, buy an SB16 (it's only $40).
 */
int
sbdsp_intr(void *arg)
{
	struct sbdsp_softc *sc = arg;
	u_char irq;

	mtx_enter(&audio_lock);
	DPRINTFN(2, ("sbdsp_intr: intr8=%p, intr16=%p\n",
		   sc->sc_intr8, sc->sc_intr16));
	if (ISSB16CLASS(sc)) {		
		bus_space_write_1(sc->sc_iot, sc->sc_ioh,
		    SBP_MIXER_ADDR, SBP_IRQ_STATUS);
		delay(20);
		irq = bus_space_read_1(sc->sc_iot, sc->sc_ioh,
		    SBP_MIXER_DATA);
		delay(30);
		if ((irq & (SBP_IRQ_DMA8 | SBP_IRQ_DMA16 | SBP_IRQ_MPU401)) == 0) {
			DPRINTF(("sbdsp_intr: Spurious interrupt 0x%x\n", irq));
			mtx_leave(&audio_lock);
			return 0;
		}
	} else {
		/* XXXX CHECK FOR INTERRUPT */
		irq = SBP_IRQ_DMA8;
	}

	sc->sc_interrupts++;
	delay(10);		/* XXX why? */

	/* clear interrupt */
	if (irq & SBP_IRQ_DMA8) {
		bus_space_read_1(sc->sc_iot, sc->sc_ioh, SBP_DSP_IRQACK8);
		if (sc->sc_intr8)
			sc->sc_intr8(sc->sc_arg8);
	}
	if (irq & SBP_IRQ_DMA16) {
		bus_space_read_1(sc->sc_iot, sc->sc_ioh, SBP_DSP_IRQACK16);
		if (sc->sc_intr16)
			sc->sc_intr16(sc->sc_arg16);
	}
#if NMIDI > 0
	if ((irq & SBP_IRQ_MPU401) && sc->sc_hasmpu) {
		mpu_intr(&sc->sc_mpu_sc);
	}
#endif
	mtx_leave(&audio_lock);
	return 1;
}

/* Like val & mask, but make sure the result is correctly rounded. */
#define MAXVAL 256
static int
sbdsp_adjust(int val, int mask)
{
	val += (MAXVAL - mask) >> 1;
	if (val >= MAXVAL)
		val = MAXVAL-1;
	return val & mask;
}

void
sbdsp_set_mixer_gain(struct sbdsp_softc *sc, int port)
{
	int src, gain;

	switch(sc->sc_mixer_model) {
	case SBM_NONE:
		return;
	case SBM_CT1335:
		gain = SB_1335_GAIN(sc->gain[port][SB_LEFT]);
		switch(port) {
		case SB_MASTER_VOL:
			src = SBP_1335_MASTER_VOL;
			break;
		case SB_MIDI_VOL:
			src = SBP_1335_MIDI_VOL;
			break;
		case SB_CD_VOL:
			src = SBP_1335_CD_VOL;
			break;
		case SB_VOICE_VOL:
			src = SBP_1335_VOICE_VOL;
			gain = SB_1335_MASTER_GAIN(sc->gain[port][SB_LEFT]);
			break;
		default:
			return;
		}
		sbdsp_mix_write(sc, src, gain);
		break;
	case SBM_CT1345:
		gain = SB_STEREO_GAIN(sc->gain[port][SB_LEFT],
				      sc->gain[port][SB_RIGHT]);
		switch (port) {
		case SB_MIC_VOL:
			src = SBP_MIC_VOL;
			gain = SB_MIC_GAIN(sc->gain[port][SB_LEFT]);
			break;
		case SB_MASTER_VOL:
			src = SBP_MASTER_VOL;
			break;
		case SB_LINE_IN_VOL:
			src = SBP_LINE_VOL;
			break;
		case SB_VOICE_VOL:
			src = SBP_VOICE_VOL;
			break;
		case SB_MIDI_VOL:
			src = SBP_MIDI_VOL;
			break;
		case SB_CD_VOL:
			src = SBP_CD_VOL;
			break;
		default:
			return;
		}
		sbdsp_mix_write(sc, src, gain);
		break;
	case SBM_CT1XX5:
	case SBM_CT1745:
		switch (port) {
		case SB_MIC_VOL:
			src = SB16P_MIC_L;
			break;
		case SB_MASTER_VOL:
			src = SB16P_MASTER_L;
			break;
		case SB_LINE_IN_VOL:
			src = SB16P_LINE_L;
			break;
		case SB_VOICE_VOL:
			src = SB16P_VOICE_L;
			break;
		case SB_MIDI_VOL:
			src = SB16P_MIDI_L;
			break;
		case SB_CD_VOL:
			src = SB16P_CD_L;
			break;
		case SB_INPUT_GAIN:
			src = SB16P_INPUT_GAIN_L;
			break;
		case SB_OUTPUT_GAIN:
			src = SB16P_OUTPUT_GAIN_L;
			break;
		case SB_TREBLE:
			src = SB16P_TREBLE_L;
			break;
		case SB_BASS:
			src = SB16P_BASS_L;
			break;
		case SB_PCSPEAKER:
			sbdsp_mix_write(sc, SB16P_PCSPEAKER, sc->gain[port][SB_LEFT]);
			return;
		default:
			return;
		}
		sbdsp_mix_write(sc, src, sc->gain[port][SB_LEFT]);
		sbdsp_mix_write(sc, SB16P_L_TO_R(src), sc->gain[port][SB_RIGHT]);
		break;
	}
}

int
sbdsp_mixer_set_port(void *addr, mixer_ctrl_t *cp)
{
	struct sbdsp_softc *sc = addr;
	int lgain, rgain;
	int mask, bits;
	int lmask, rmask, lbits, rbits;
	int mute, swap;

	if (sc->sc_open == SB_OPEN_MIDI)
		return EBUSY;

	DPRINTF(("sbdsp_mixer_set_port: port=%d num_channels=%d\n", cp->dev,
	    cp->un.value.num_channels));

	if (sc->sc_mixer_model == SBM_NONE)
		return EINVAL;

	switch (cp->dev) {
	case SB_TREBLE:
	case SB_BASS:
		if (sc->sc_mixer_model == SBM_CT1345 ||
                    sc->sc_mixer_model == SBM_CT1XX5) {
			if (cp->type != AUDIO_MIXER_ENUM)
				return EINVAL;
			switch (cp->dev) {
			case SB_TREBLE:
				sbdsp_set_ifilter(addr, cp->un.ord ? SB_TREBLE : 0);
				return 0;
			case SB_BASS:
				sbdsp_set_ifilter(addr, cp->un.ord ? SB_BASS : 0);
				return 0;
			}
		}
	case SB_PCSPEAKER:
	case SB_INPUT_GAIN:
	case SB_OUTPUT_GAIN:
		if (!ISSBM1745(sc))
			return EINVAL;
	case SB_MIC_VOL:
	case SB_LINE_IN_VOL:
		if (sc->sc_mixer_model == SBM_CT1335)
			return EINVAL;
	case SB_VOICE_VOL:
	case SB_MIDI_VOL:
	case SB_CD_VOL:
	case SB_MASTER_VOL:
		if (cp->type != AUDIO_MIXER_VALUE)
			return EINVAL;

		/*
		 * All the mixer ports are stereo except for the microphone.
		 * If we get a single-channel gain value passed in, then we
		 * duplicate it to both left and right channels.
		 */

		switch (cp->dev) {
		case SB_MIC_VOL:
			if (cp->un.value.num_channels != 1)
				return EINVAL;

			lgain = rgain = SB_ADJUST_MIC_GAIN(sc,
			  cp->un.value.level[AUDIO_MIXER_LEVEL_MONO]);
			break;
		case SB_PCSPEAKER:
			if (cp->un.value.num_channels != 1)
				return EINVAL;
			/* fall into */
		case SB_INPUT_GAIN:
		case SB_OUTPUT_GAIN:
			lgain = rgain = SB_ADJUST_2_GAIN(sc,
			  cp->un.value.level[AUDIO_MIXER_LEVEL_MONO]);
			break;
		default:
			switch (cp->un.value.num_channels) {
			case 1:
				lgain = rgain = SB_ADJUST_GAIN(sc,
				  cp->un.value.level[AUDIO_MIXER_LEVEL_MONO]);
				break;
			case 2:
				if (sc->sc_mixer_model == SBM_CT1335)
					return EINVAL;
				lgain = SB_ADJUST_GAIN(sc,
				  cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT]);
				rgain = SB_ADJUST_GAIN(sc,
				  cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT]);
				break;
			default:
				return EINVAL;
			}
			break;
		}
		sc->gain[cp->dev][SB_LEFT]  = lgain;
		sc->gain[cp->dev][SB_RIGHT] = rgain;

		sbdsp_set_mixer_gain(sc, cp->dev);
		break;

	case SB_RECORD_SOURCE:
		if (ISSBM1745(sc)) {
			if (cp->type != AUDIO_MIXER_SET)
				return EINVAL;
			return sbdsp_set_in_ports(sc, cp->un.mask);
		} else {
			if (cp->type != AUDIO_MIXER_ENUM)
				return EINVAL;
			sc->in_port = cp->un.ord;
			return sbdsp_set_in_ports(sc, 1 << cp->un.ord);
		}
		break;

	case SB_AGC:
		if (!ISSBM1745(sc) || cp->type != AUDIO_MIXER_ENUM)
			return EINVAL;
		sbdsp_mix_write(sc, SB16P_AGC, cp->un.ord & 1);
		break;

	case SB_CD_OUT_MUTE:
		mask = SB16P_SW_CD;
		goto omute;
	case SB_MIC_OUT_MUTE:
		mask = SB16P_SW_MIC;
		goto omute;
	case SB_LINE_OUT_MUTE:
		mask = SB16P_SW_LINE;
	omute:
		if (cp->type != AUDIO_MIXER_ENUM)
			return EINVAL;
		bits = sbdsp_mix_read(sc, SB16P_OSWITCH);
		sc->gain[cp->dev][SB_LR] = cp->un.ord != 0;
		if (cp->un.ord)
			bits = bits & ~mask;
		else
			bits = bits | mask;
		sbdsp_mix_write(sc, SB16P_OSWITCH, bits);
		break;

	case SB_MIC_IN_MUTE:
	case SB_MIC_SWAP:
		lmask = rmask = SB16P_SW_MIC;
		goto imute;
	case SB_CD_IN_MUTE:
	case SB_CD_SWAP:
		lmask = SB16P_SW_CD_L;
		rmask = SB16P_SW_CD_R;
		goto imute;
	case SB_LINE_IN_MUTE:
	case SB_LINE_SWAP:
		lmask = SB16P_SW_LINE_L;
		rmask = SB16P_SW_LINE_R;
		goto imute;
	case SB_MIDI_IN_MUTE:
	case SB_MIDI_SWAP:
		lmask = SB16P_SW_MIDI_L;
		rmask = SB16P_SW_MIDI_R;
	imute:
		if (cp->type != AUDIO_MIXER_ENUM)
			return EINVAL;
		mask = lmask | rmask;
		lbits = sbdsp_mix_read(sc, SB16P_ISWITCH_L) & ~mask;
		rbits = sbdsp_mix_read(sc, SB16P_ISWITCH_R) & ~mask;
		sc->gain[cp->dev][SB_LR] = cp->un.ord != 0;
		if (SB_IS_IN_MUTE(cp->dev)) {
			mute = cp->dev;
			swap = mute - SB_CD_IN_MUTE + SB_CD_SWAP;
		} else {
			swap = cp->dev;
			mute = swap + SB_CD_IN_MUTE - SB_CD_SWAP;
		}
		if (sc->gain[swap][SB_LR]) {
			mask = lmask;
			lmask = rmask;
			rmask = mask;
		}
		if (!sc->gain[mute][SB_LR]) {
			lbits = lbits | lmask;
			rbits = rbits | rmask;
		}
		sbdsp_mix_write(sc, SB16P_ISWITCH_L, lbits);
		sbdsp_mix_write(sc, SB16P_ISWITCH_L, rbits);
		break;

	default:
		return EINVAL;
	}

	return 0;
}

int
sbdsp_mixer_get_port(void *addr, mixer_ctrl_t *cp)
{
	struct sbdsp_softc *sc = addr;

	if (sc->sc_open == SB_OPEN_MIDI)
		return EBUSY;

	DPRINTF(("sbdsp_mixer_get_port: port=%d\n", cp->dev));

	if (sc->sc_mixer_model == SBM_NONE)
		return EINVAL;

	switch (cp->dev) {
	case SB_TREBLE:
	case SB_BASS:
		if (sc->sc_mixer_model == SBM_CT1345 ||
                    sc->sc_mixer_model == SBM_CT1XX5) {
			switch (cp->dev) {
			case SB_TREBLE:
				cp->un.ord = sbdsp_get_ifilter(addr) == SB_TREBLE;
				return 0;
			case SB_BASS:
				cp->un.ord = sbdsp_get_ifilter(addr) == SB_BASS;
				return 0;
			}
		}
	case SB_PCSPEAKER:
	case SB_INPUT_GAIN:
	case SB_OUTPUT_GAIN:
		if (!ISSBM1745(sc))
			return EINVAL;
	case SB_MIC_VOL:
	case SB_LINE_IN_VOL:
		if (sc->sc_mixer_model == SBM_CT1335)
			return EINVAL;
	case SB_VOICE_VOL:
	case SB_MIDI_VOL:
	case SB_CD_VOL:
	case SB_MASTER_VOL:
		switch (cp->dev) {
		case SB_MIC_VOL:
		case SB_PCSPEAKER:
			if (cp->un.value.num_channels != 1)
				return EINVAL;
			/* fall into */
		default:
			switch (cp->un.value.num_channels) {
			case 1:
				cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
					sc->gain[cp->dev][SB_LEFT];
				break;
			case 2:
				cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
					sc->gain[cp->dev][SB_LEFT];
				cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] =
					sc->gain[cp->dev][SB_RIGHT];
				break;
			default:
				return EINVAL;
			}
			break;
		}
		break;

	case SB_RECORD_SOURCE:
		if (ISSBM1745(sc))
			cp->un.mask = sc->in_mask;
		else
			cp->un.ord = sc->in_port;
		break;

	case SB_AGC:
		if (!ISSBM1745(sc))
			return EINVAL;
		cp->un.ord = sbdsp_mix_read(sc, SB16P_AGC);
		break;

	case SB_CD_IN_MUTE:
	case SB_MIC_IN_MUTE:
	case SB_LINE_IN_MUTE:
	case SB_MIDI_IN_MUTE:
	case SB_CD_SWAP:
	case SB_MIC_SWAP:
	case SB_LINE_SWAP:
	case SB_MIDI_SWAP:
	case SB_CD_OUT_MUTE:
	case SB_MIC_OUT_MUTE:
	case SB_LINE_OUT_MUTE:
		cp->un.ord = sc->gain[cp->dev][SB_LR];
		break;

	default:
		return EINVAL;
	}

	return 0;
}

int
sbdsp_mixer_query_devinfo(void *addr, mixer_devinfo_t *dip)
{
	struct sbdsp_softc *sc = addr;
	int chan, class, is1745;

	DPRINTF(("sbdsp_mixer_query_devinfo: model=%d index=%d\n",
		 sc->sc_mixer_model, dip->index));

	if (dip->index < 0)
		return ENXIO;

	if (sc->sc_mixer_model == SBM_NONE)
		return ENXIO;

	chan = sc->sc_mixer_model == SBM_CT1335 ? 1 : 2;
	is1745 = ISSBM1745(sc);
	class = is1745 ? SB_INPUT_CLASS : SB_OUTPUT_CLASS;

	switch (dip->index) {
	case SB_MASTER_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = SB_OUTPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmaster, sizeof dip->label.name);
		dip->un.v.num_channels = chan;
		strlcpy(dip->un.v.units.name, AudioNvolume, sizeof dip->un.v.units.name);
		return 0;
	case SB_MIDI_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = class;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = is1745 ? SB_MIDI_IN_MUTE : AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNfmsynth, sizeof dip->label.name);
		dip->un.v.num_channels = chan;
		strlcpy(dip->un.v.units.name, AudioNvolume, sizeof dip->un.v.units.name);
		return 0;
	case SB_CD_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = class;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = is1745 ? SB_CD_IN_MUTE : AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNcd, sizeof dip->label.name);
		dip->un.v.num_channels = chan;
		strlcpy(dip->un.v.units.name, AudioNvolume, sizeof dip->un.v.units.name);
		return 0;
	case SB_VOICE_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = class;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNdac, sizeof dip->label.name);
		dip->un.v.num_channels = chan;
		strlcpy(dip->un.v.units.name, AudioNvolume, sizeof dip->un.v.units.name);
		return 0;
	case SB_OUTPUT_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = SB_OUTPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCoutputs, sizeof dip->label.name);
		return 0;
	}

	if (sc->sc_mixer_model == SBM_CT1335)
		return ENXIO;

	switch (dip->index) {
	case SB_MIC_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = class;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = is1745 ? SB_MIC_IN_MUTE : AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmicrophone,
		    sizeof dip->label.name);
		dip->un.v.num_channels = 1;
		strlcpy(dip->un.v.units.name, AudioNvolume, sizeof dip->un.v.units.name);
		return 0;

	case SB_LINE_IN_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = class;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = is1745 ? SB_LINE_IN_MUTE : AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNline, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume, sizeof dip->un.v.units.name);
		return 0;

	case SB_RECORD_SOURCE:
		dip->mixer_class = SB_RECORD_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNsource, sizeof dip->label.name);
		if (ISSBM1745(sc)) {
			dip->type = AUDIO_MIXER_SET;
			dip->un.s.num_mem = 4;
			strlcpy(dip->un.s.member[0].label.name,
			    AudioNmicrophone,
			    sizeof dip->un.s.member[0].label.name);
			dip->un.s.member[0].mask = 1 << SB_MIC_VOL;
			strlcpy(dip->un.s.member[1].label.name,
			    AudioNcd, sizeof dip->un.s.member[1].label.name);
			dip->un.s.member[1].mask = 1 << SB_CD_VOL;
			strlcpy(dip->un.s.member[2].label.name,
			    AudioNline, sizeof dip->un.s.member[2].label.name);
			dip->un.s.member[2].mask = 1 << SB_LINE_IN_VOL;
			strlcpy(dip->un.s.member[3].label.name,
			    AudioNfmsynth,
			    sizeof dip->un.s.member[3].label.name);
			dip->un.s.member[3].mask = 1 << SB_MIDI_VOL;
		} else {
			dip->type = AUDIO_MIXER_ENUM;
			dip->un.e.num_mem = 3;
			strlcpy(dip->un.e.member[0].label.name,
			    AudioNmicrophone,
			    sizeof dip->un.e.member[0].label.name);
			dip->un.e.member[0].ord = SB_MIC_VOL;
			strlcpy(dip->un.e.member[1].label.name, AudioNcd,
			    sizeof dip->un.e.member[1].label.name);
			dip->un.e.member[1].ord = SB_CD_VOL;
			strlcpy(dip->un.e.member[2].label.name, AudioNline,
			    sizeof dip->un.e.member[2].label.name);
			dip->un.e.member[2].ord = SB_LINE_IN_VOL;
		}
		return 0;

	case SB_BASS:
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNbass, sizeof dip->label.name);
		if (sc->sc_mixer_model == SBM_CT1745) {
			dip->type = AUDIO_MIXER_VALUE;
			dip->mixer_class = SB_EQUALIZATION_CLASS;
			dip->un.v.num_channels = 2;
			strlcpy(dip->un.v.units.name, AudioNbass, sizeof dip->un.v.units.name);
		} else {
			dip->type = AUDIO_MIXER_ENUM;
			dip->mixer_class = SB_INPUT_CLASS;
			dip->un.e.num_mem = 2;
			strlcpy(dip->un.e.member[0].label.name, AudioNoff,
			    sizeof dip->un.e.member[0].label.name);
			dip->un.e.member[0].ord = 0;
			strlcpy(dip->un.e.member[1].label.name, AudioNon,
			    sizeof dip->un.e.member[1].label.name);
			dip->un.e.member[1].ord = 1;
		}
		return 0;

	case SB_TREBLE:
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNtreble, sizeof dip->label.name);
		if (sc->sc_mixer_model == SBM_CT1745) {
			dip->type = AUDIO_MIXER_VALUE;
			dip->mixer_class = SB_EQUALIZATION_CLASS;
			dip->un.v.num_channels = 2;
			strlcpy(dip->un.v.units.name, AudioNtreble, sizeof dip->un.v.units.name);
		} else {
			dip->type = AUDIO_MIXER_ENUM;
			dip->mixer_class = SB_INPUT_CLASS;
			dip->un.e.num_mem = 2;
			strlcpy(dip->un.e.member[0].label.name, AudioNoff,
			    sizeof dip->un.e.member[0].label.name);
			dip->un.e.member[0].ord = 0;
			strlcpy(dip->un.e.member[1].label.name, AudioNon,
			    sizeof dip->un.e.member[1].label.name);
			dip->un.e.member[1].ord = 1;
		}
		return 0;

	case SB_RECORD_CLASS:			/* record source class */
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = SB_RECORD_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCrecord, sizeof dip->label.name);
		return 0;

	case SB_INPUT_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = SB_INPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCinputs, sizeof dip->label.name);
		return 0;

	}

	if (sc->sc_mixer_model == SBM_CT1345)
		return ENXIO;

	switch(dip->index) {
	case SB_PCSPEAKER:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = SB_INPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, "pc_speaker", sizeof dip->label.name);
		dip->un.v.num_channels = 1;
		strlcpy(dip->un.v.units.name, AudioNvolume, sizeof dip->un.v.units.name);
		return 0;

	case SB_INPUT_GAIN:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = SB_INPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNinput, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume, sizeof dip->un.v.units.name);
		return 0;

	case SB_OUTPUT_GAIN:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = SB_OUTPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNoutput, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume, sizeof dip->un.v.units.name);
		return 0;

	case SB_AGC:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = SB_INPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, "agc", sizeof dip->label.name);
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNon,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		return 0;

	case SB_EQUALIZATION_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = SB_EQUALIZATION_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCequalization, sizeof dip->label.name);
		return 0;

	case SB_CD_IN_MUTE:
		dip->prev = SB_CD_VOL;
		dip->next = SB_CD_SWAP;
		dip->mixer_class = SB_INPUT_CLASS;
		goto mute;

	case SB_MIC_IN_MUTE:
		dip->prev = SB_MIC_VOL;
		dip->next = SB_MIC_SWAP;
		dip->mixer_class = SB_INPUT_CLASS;
		goto mute;

	case SB_LINE_IN_MUTE:
		dip->prev = SB_LINE_IN_VOL;
		dip->next = SB_LINE_SWAP;
		dip->mixer_class = SB_INPUT_CLASS;
		goto mute;

	case SB_MIDI_IN_MUTE:
		dip->prev = SB_MIDI_VOL;
		dip->next = SB_MIDI_SWAP;
		dip->mixer_class = SB_INPUT_CLASS;
		goto mute;

	case SB_CD_SWAP:
		dip->prev = SB_CD_IN_MUTE;
		dip->next = SB_CD_OUT_MUTE;
		goto swap;

	case SB_MIC_SWAP:
		dip->prev = SB_MIC_IN_MUTE;
		dip->next = SB_MIC_OUT_MUTE;
		goto swap;

	case SB_LINE_SWAP:
		dip->prev = SB_LINE_IN_MUTE;
		dip->next = SB_LINE_OUT_MUTE;
		goto swap;

	case SB_MIDI_SWAP:
		dip->prev = SB_MIDI_IN_MUTE;
		dip->next = AUDIO_MIXER_LAST;
	swap:
		dip->mixer_class = SB_INPUT_CLASS;
		strlcpy(dip->label.name, AudioNswap, sizeof dip->label.name);
		goto mute1;

	case SB_CD_OUT_MUTE:
		dip->prev = SB_CD_SWAP;
		dip->next = AUDIO_MIXER_LAST;
		dip->mixer_class = SB_OUTPUT_CLASS;
		goto mute;

	case SB_MIC_OUT_MUTE:
		dip->prev = SB_MIC_SWAP;
		dip->next = AUDIO_MIXER_LAST;
		dip->mixer_class = SB_OUTPUT_CLASS;
		goto mute;

	case SB_LINE_OUT_MUTE:
		dip->prev = SB_LINE_SWAP;
		dip->next = AUDIO_MIXER_LAST;
		dip->mixer_class = SB_OUTPUT_CLASS;
	mute:
		strlcpy(dip->label.name, AudioNmute, sizeof dip->label.name);
	mute1:
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNon,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		return 0;

	}

	return ENXIO;
}

void *
sb_malloc(void *addr, int direction, size_t size, int pool, int flags)
{
	struct sbdsp_softc *sc = addr;
	int drq;

	/* 8-bit has more restrictive alignment */
	if (sc->sc_drq8 != -1)
		drq = sc->sc_drq8;
	else
		drq = sc->sc_drq16;

	return isa_malloc(sc->sc_isa, drq, size, pool, flags);
}

void
sb_free(void *addr, void *ptr, int pool)
{
	isa_free(ptr, pool);
}

size_t
sb_round(void *addr, int direction, size_t size)
{
	if (size > MAX_ISADMA)
		size = MAX_ISADMA;
	return size;
}

#if NMIDI > 0
/*
 * MIDI related routines.
 */

int
sbdsp_midi_open(void *addr, int flags, void (*iintr)(void *, int),
    void (*ointr)(void *), void *arg)
{
	struct sbdsp_softc *sc = addr;

        DPRINTF(("sbdsp_midi_open: sc=%p\n", sc));

	if (sc->sc_open != SB_CLOSED)
		return EBUSY;
	if (sbdsp_reset(sc) != 0)
		return EIO;

	if (sc->sc_model >= SB_20)
		if (sbdsp_wdsp(sc, SB_MIDI_UART_INTR)) /* enter UART mode */
			return EIO;
	sc->sc_open = SB_OPEN_MIDI;
	sc->sc_openflags = flags;
	sc->sc_intr8 = sbdsp_midi_intr;
	sc->sc_arg8 = addr;
	sc->sc_intrm = iintr;
	sc->sc_argm = arg;
	return 0;
}

void
sbdsp_midi_close(void *addr)
{
	struct sbdsp_softc *sc = addr;

        DPRINTF(("sbdsp_midi_close: sc=%p\n", sc));

	if (sc->sc_model >= SB_20)
		sbdsp_reset(sc); /* exit UART mode */
	sc->sc_open = SB_CLOSED;
	sc->sc_intrm = 0;
}

int
sbdsp_midi_output(void *addr, int d)
{
	struct sbdsp_softc *sc = addr;

	if (sc->sc_model < SB_20 && sbdsp_wdsp(sc, SB_MIDI_WRITE))
		return 1;
	(void)sbdsp_wdsp(sc, d);
	return 1;
}

void
sbdsp_midi_getinfo(void *addr, struct midi_info *mi)
{
	struct sbdsp_softc *sc = addr;

	mi->name = sc->sc_model < SB_20 ? "SB MIDI cmd" : "SB MIDI UART";
	mi->props = MIDI_PROP_CAN_INPUT;
}

int
sbdsp_midi_intr(void *addr)
{
	struct sbdsp_softc *sc = addr;

	sc->sc_intrm(sc->sc_argm, sbdsp_rdsp(sc));
	return (0);
}

#endif
