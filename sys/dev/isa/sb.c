/*	$OpenBSD: sb.c,v 1.35 2022/11/02 10:41:34 kn Exp $	*/
/*	$NetBSD: sb.c,v 1.57 1998/01/12 09:43:46 thorpej Exp $	*/

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

#include "midi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/syslog.h>
#include <sys/device.h>

#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/bus.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>

#include <dev/isa/isavar.h>
#include <dev/isa/isadmavar.h>

#include <dev/isa/sbreg.h>
#include <dev/isa/sbvar.h>
#include <dev/isa/sbdspvar.h>

struct cfdriver sb_cd = {
	NULL, "sb", DV_DULL
};

#if NMIDI > 0
int	sb_mpu401_open(void *, int, void (*iintr)(void *, int),
		       void (*ointr)(void *), void *arg);
void	sb_mpu401_close(void *);
int	sb_mpu401_output(void *, int);
void	sb_mpu401_getinfo(void *, struct midi_info *);

const struct midi_hw_if sb_midi_hw_if = {
	sbdsp_midi_open,
	sbdsp_midi_close,
	sbdsp_midi_output,
	0,			/* flush */
	sbdsp_midi_getinfo,
	0,			/* ioctl */
};

const struct midi_hw_if sb_mpu401_hw_if = {
	sb_mpu401_open,
	sb_mpu401_close,
	sb_mpu401_output,
	0,			/* flush */
	sb_mpu401_getinfo,
	0,			/* ioctl */
};
#endif

/*
 * Define our interface to the higher level audio driver.
 */

const struct audio_hw_if sb_hw_if = {
	.open = sbdsp_open,
	.close = sbdsp_close,
	.set_params = sbdsp_set_params,
	.round_blocksize = sbdsp_round_blocksize,
	.halt_output = sbdsp_haltdma,
	.halt_input = sbdsp_haltdma,
	.set_port = sbdsp_mixer_set_port,
	.get_port = sbdsp_mixer_get_port,
	.query_devinfo = sbdsp_mixer_query_devinfo,
	.allocm = sb_malloc,
	.freem = sb_free,
	.round_buffersize = sb_round,
	.trigger_output = sbdsp_trigger_output,
	.trigger_input = sbdsp_trigger_input,
};

#ifdef AUDIO_DEBUG
#define DPRINTF(x)	if (sbdebug) printf x
int	sbdebug = 0;
#else
#define DPRINTF(x)
#endif

/*
 * Probe / attach routines.
 */


int
sbmatch(struct sbdsp_softc *sc)
{
	static u_char drq_conf[8] = {
		0x01, 0x02, -1, 0x08, -1, 0x20, 0x40, 0x80
	};

	static u_char irq_conf[11] = {
		-1, -1, 0x01, -1, -1, 0x02, -1, 0x04, -1, 0x01, 0x08
	};

	if (sbdsp_probe(sc) == 0)
		return 0;

	/*
	 * Cannot auto-discover DMA channel.
	 */
	if (ISSBPROCLASS(sc)) {
		if (!SBP_DRQ_VALID(sc->sc_drq8)) {
			DPRINTF(("%s: configured dma chan %d invalid\n",
			    sc->sc_dev.dv_xname, sc->sc_drq8));
			return 0;
		}
	} else {
		if (!SB_DRQ_VALID(sc->sc_drq8)) {
			DPRINTF(("%s: configured dma chan %d invalid\n",
			    sc->sc_dev.dv_xname, sc->sc_drq8));
			return 0;
		}
	}

        if (0 <= sc->sc_drq16 && sc->sc_drq16 <= 3)
        	/* 
                 * XXX Some ViBRA16 cards seem to have two 8 bit DMA 
                 * channels.  I've no clue how to use them, so ignore
                 * one of them for now.  -- augustss@netbsd.org
                 */
        	sc->sc_drq16 = -1;

	if (ISSB16CLASS(sc)) {
		if (sc->sc_drq16 == -1)
			sc->sc_drq16 = sc->sc_drq8;
		if (!SB16_DRQ_VALID(sc->sc_drq16)) {
			DPRINTF(("%s: configured dma chan %d invalid\n",
			    sc->sc_dev.dv_xname, sc->sc_drq16));
			return 0;
		}
	} else
		sc->sc_drq16 = sc->sc_drq8;
	
	if (ISSBPROCLASS(sc)) {
		if (!SBP_IRQ_VALID(sc->sc_irq)) {
			DPRINTF(("%s: configured irq %d invalid\n",
			    sc->sc_dev.dv_xname, sc->sc_irq));
			return 0;
		}
	} else {
		if (!SB_IRQ_VALID(sc->sc_irq)) {
			DPRINTF(("%s: configured irq %d invalid\n",
			    sc->sc_dev.dv_xname, sc->sc_irq));
			return 0;
		}
	}

	if (ISSB16CLASS(sc)) {
		int w, r;
#if 0
		DPRINTF(("%s: old drq conf %02x\n", sc->sc_dev.dv_xname,
		    sbdsp_mix_read(sc, SBP_SET_DRQ)));
		DPRINTF(("%s: try drq conf %02x\n", sc->sc_dev.dv_xname,
		    drq_conf[sc->sc_drq16] | drq_conf[sc->sc_drq8]));
#endif
		w = drq_conf[sc->sc_drq16] | drq_conf[sc->sc_drq8];
		sbdsp_mix_write(sc, SBP_SET_DRQ, w);
		r = sbdsp_mix_read(sc, SBP_SET_DRQ) & 0xeb;
		if (r != w) {
			DPRINTF(("%s: setting drq mask %02x failed, got %02x\n", sc->sc_dev.dv_xname, w, r));
			return 0;
		}
#if 0
		DPRINTF(("%s: new drq conf %02x\n", sc->sc_dev.dv_xname,
		    sbdsp_mix_read(sc, SBP_SET_DRQ)));
#endif

#if 0
		DPRINTF(("%s: old irq conf %02x\n", sc->sc_dev.dv_xname,
		    sbdsp_mix_read(sc, SBP_SET_IRQ)));
		DPRINTF(("%s: try irq conf %02x\n", sc->sc_dev.dv_xname,
		    irq_conf[sc->sc_irq]));
#endif
		w = irq_conf[sc->sc_irq];
		sbdsp_mix_write(sc, SBP_SET_IRQ, w);
		r = sbdsp_mix_read(sc, SBP_SET_IRQ) & 0x0f;
		if (r != w) {
			DPRINTF(("%s: setting irq mask %02x failed, got %02x\n",
			    sc->sc_dev.dv_xname, w, r));
			return 0;
		}
#if 0
		DPRINTF(("%s: new irq conf %02x\n", sc->sc_dev.dv_xname,
		    sbdsp_mix_read(sc, SBP_SET_IRQ)));
#endif
	}

	return 1;
}


void
sbattach(struct sbdsp_softc *sc)
{
	struct audio_attach_args arg;
#if NMIDI > 0
	const struct midi_hw_if *mhw = &sb_midi_hw_if;
#endif

	sc->sc_ih = isa_intr_establish(sc->sc_ic, sc->sc_irq,
	    IST_EDGE, IPL_AUDIO | IPL_MPSAFE,
	    sbdsp_intr, sc, sc->sc_dev.dv_xname);

	sbdsp_attach(sc);

#if NMIDI > 0
	sc->sc_hasmpu = 0;
	if (ISSB16CLASS(sc) && sc->sc_mpu_sc.iobase != 0) {
		sc->sc_mpu_sc.iot = sc->sc_iot;
		if (mpu_find(&sc->sc_mpu_sc)) {
			sc->sc_hasmpu = 1;
			mhw = &sb_mpu401_hw_if;
		}
	}
	midi_attach_mi(mhw, sc, &sc->sc_dev);
#endif

	audio_attach_mi(&sb_hw_if, sc, NULL, &sc->sc_dev);

	arg.type = AUDIODEV_TYPE_OPL;
	arg.hwif = 0;
	arg.hdl = 0;
	(void)config_found(&sc->sc_dev, &arg, audioprint);
}

/*
 * Various routines to interface to higher level audio driver
 */

#if NMIDI > 0

#define SBMPU(a) (&((struct sbdsp_softc *)addr)->sc_mpu_sc)

int
sb_mpu401_open(void *addr, int flags, void (*iintr)(void *, int),
    void (*ointr)(void *), void *arg)
{
	return mpu_open(SBMPU(addr), flags, iintr, ointr, arg);
}

int
sb_mpu401_output(void *addr, int d)
{
	return mpu_output(SBMPU(addr), d);
}

void
sb_mpu401_close(void *addr)
{
	mpu_close(SBMPU(addr));
}

void
sb_mpu401_getinfo(void *addr, struct midi_info *mi)
{
	mi->name = "SB MPU-401 UART";
	mi->props = 0;
}
#endif
