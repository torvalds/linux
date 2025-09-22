/*      $OpenBSD: eap.c,v 1.66 2024/09/01 03:08:56 jsg Exp $ */
/*	$NetBSD: eap.c,v 1.46 2001/09/03 15:07:37 reinoud Exp $ */

/*
 * Copyright (c) 1998, 1999 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson <augustss@netbsd.org> and Charles M. Hannum.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Debugging:   Andreas Gustafsson <gson@araneus.fi>
 * Testing:     Chuck Cranor       <chuck@maria.wustl.edu>
 *              Phil Nelson        <phil@cs.wwu.edu>
 *
 * ES1371/AC97:	Ezra Story         <ezy@panix.com>
 */

/* 
 * Ensoniq ES1370 + AK4531 and ES1371/ES1373 + AC97
 *
 * Documentation links:
 * 
 * ftp://ftp.alsa-project.org/pub/manuals/ensoniq/
 * ftp://ftp.alsa-project.org/pub/manuals/asahi_kasei/4531.pdf
 */

#include "midi.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/fcntl.h>
#include <sys/device.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>
#include <dev/midi_if.h>
#include <dev/ic/ac97.h>

#include <machine/bus.h>

#include <dev/pci/eapreg.h>

struct        cfdriver eap_cd = {
      NULL, "eap", DV_DULL
};

#define	PCI_CBIO		0x10

/* Debug */
#ifdef AUDIO_DEBUG
#define DPRINTF(x)	if (eapdebug) printf x
#define DPRINTFN(n,x)	if (eapdebug>(n)) printf x
int	eapdebug = 1;
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

int	eap_match(struct device *, void *, void *);
void	eap_attach(struct device *, struct device *, void *);
int	eap_activate(struct device *, int);
int	eap_intr(void *);

struct eap_dma {
	bus_dmamap_t map;
	caddr_t addr;
	bus_dma_segment_t segs[1];
	int nsegs;
	size_t size;
	struct eap_dma *next;
};

#define DMAADDR(p) ((p)->map->dm_segs[0].ds_addr)
#define KERNADDR(p) ((void *)((p)->addr))

struct eap_softc {
	struct device sc_dev;		/* base device */
	void *sc_ih;			/* interrupt vectoring */
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	bus_dma_tag_t sc_dmatag;	/* DMA tag */

	struct eap_dma *sc_dmas;

	void	(*sc_pintr)(void *);	/* dma completion intr handler */
	void	*sc_parg;		/* arg for sc_intr() */
#ifdef DIAGNOSTIC
	char	sc_prun;
#endif

	void	(*sc_rintr)(void *);	/* dma completion intr handler */
	void	*sc_rarg;		/* arg for sc_intr() */
#ifdef DIAGNOSTIC
	char	sc_rrun;
#endif

#if NMIDI > 0
	void	(*sc_iintr)(void *, int); /* midi input ready handler */
	void	(*sc_ointr)(void *);	/* midi output ready handler */
	void	*sc_arg;
	int	sc_uctrl;
	struct device *sc_mididev;
#endif

	u_short	sc_port[AK_NPORTS];	/* mirror of the hardware setting */
	u_int	sc_record_source;	/* recording source mask */
	u_int	sc_input_source;	/* input source mask */
	u_int	sc_mic_preamp;
	char    sc_1371;		/* Using ES1371/AC97 codec */
	char    sc_ct5880;		/* CT5880 chip */

	struct ac97_codec_if *codec_if;
	struct ac97_host_if host_if;

	int flags;
};

enum	ac97_host_flags eap_flags_codec(void *);
int	eap_allocmem(struct eap_softc *, size_t, size_t, struct eap_dma *);
int	eap_freemem(struct eap_softc *, struct eap_dma *);

#define EWRITE1(sc, r, x) bus_space_write_1((sc)->iot, (sc)->ioh, (r), (x))
#define EWRITE2(sc, r, x) bus_space_write_2((sc)->iot, (sc)->ioh, (r), (x))
#define EWRITE4(sc, r, x) bus_space_write_4((sc)->iot, (sc)->ioh, (r), (x))
#define EREAD1(sc, r) bus_space_read_1((sc)->iot, (sc)->ioh, (r))
#define EREAD2(sc, r) bus_space_read_2((sc)->iot, (sc)->ioh, (r))
#define EREAD4(sc, r) bus_space_read_4((sc)->iot, (sc)->ioh, (r))

const struct cfattach eap_ca = {
	sizeof(struct eap_softc), eap_match, eap_attach, NULL, eap_activate
};

int	eap_open(void *, int);
void	eap_close(void *);
int	eap_set_params(void *, int, int, struct audio_params *, struct audio_params *);
int	eap_round_blocksize(void *, int);
int	eap_trigger_output(void *, void *, void *, int, void (*)(void *),
	    void *, struct audio_params *);
int	eap_trigger_input(void *, void *, void *, int, void (*)(void *),
	    void *, struct audio_params *);
int	eap_halt_output(void *);
int	eap_halt_input(void *);
void	eap_resume(struct eap_softc *);
void    eap1370_write_codec(struct eap_softc *, int, int);
int	eap1370_mixer_set_port(void *, mixer_ctrl_t *);
int	eap1370_mixer_get_port(void *, mixer_ctrl_t *);
int	eap1371_mixer_set_port(void *, mixer_ctrl_t *);
int	eap1371_mixer_get_port(void *, mixer_ctrl_t *);
int	eap1370_query_devinfo(void *, mixer_devinfo_t *);
void   *eap_malloc(void *, int, size_t, int, int);
void	eap_free(void *, void *, int);
void	eap1370_set_mixer(struct eap_softc *sc, int a, int d);
u_int32_t eap1371_src_wait(struct eap_softc *sc);
void	eap1371_src_write(struct eap_softc *sc, int a, int d);
int	eap1371_query_devinfo(void *addr, mixer_devinfo_t *dip);

int     eap1371_attach_codec(void *sc, struct ac97_codec_if *);
int	eap1371_read_codec(void *sc, u_int8_t a, u_int16_t *d);
int	eap1371_write_codec(void *sc, u_int8_t a, u_int16_t d);
void    eap1371_reset_codec(void *sc);
#if NMIDI > 0
void	eap_midi_close(void *);
void	eap_midi_getinfo(void *, struct midi_info *);
int	eap_midi_open(void *, int, void (*)(void *, int),
	    void (*)(void *), void *);
int	eap_midi_output(void *, int);
#endif

const struct audio_hw_if eap1370_hw_if = {
	.open = eap_open,
	.close = eap_close,
	.set_params = eap_set_params,
	.round_blocksize = eap_round_blocksize,
	.halt_output = eap_halt_output,
	.halt_input = eap_halt_input,
	.set_port = eap1370_mixer_set_port,
	.get_port = eap1370_mixer_get_port,
	.query_devinfo = eap1370_query_devinfo,
	.allocm = eap_malloc,
	.freem = eap_free,
	.trigger_output = eap_trigger_output,
	.trigger_input = eap_trigger_input,
};

const struct audio_hw_if eap1371_hw_if = {
	.open = eap_open,
	.close = eap_close,
	.set_params = eap_set_params,
	.round_blocksize = eap_round_blocksize,
	.halt_output = eap_halt_output,
	.halt_input = eap_halt_input,
	.set_port = eap1371_mixer_set_port,
	.get_port = eap1371_mixer_get_port,
	.query_devinfo = eap1371_query_devinfo,
	.allocm = eap_malloc,
	.freem = eap_free,
	.trigger_output = eap_trigger_output,
	.trigger_input = eap_trigger_input,
};

#if NMIDI > 0
const struct midi_hw_if eap_midi_hw_if = {
	eap_midi_open,
	eap_midi_close,
	eap_midi_output,
	0,				/* flush */
	eap_midi_getinfo,
	0,				/* ioctl */
};
#endif

const struct pci_matchid eap_devices[] = {
	{ PCI_VENDOR_CREATIVELABS, PCI_PRODUCT_CREATIVELABS_EV1938 },
	{ PCI_VENDOR_ENSONIQ, PCI_PRODUCT_ENSONIQ_AUDIOPCI },
	{ PCI_VENDOR_ENSONIQ, PCI_PRODUCT_ENSONIQ_AUDIOPCI97 },
	{ PCI_VENDOR_ENSONIQ, PCI_PRODUCT_ENSONIQ_CT5880 },
};

int
eap_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, eap_devices,
	    nitems(eap_devices)));
}

int
eap_activate(struct device *self, int act)
{
	struct eap_softc *sc = (struct eap_softc *)self;

	switch (act) {
	case DVACT_RESUME:
		eap_resume(sc);
		break;
	default:
		break;
	}
	return (config_activate_children(self, act));
}

void
eap1370_write_codec(struct eap_softc *sc, int a, int d)
{
	int icss, to;

	to = EAP_WRITE_TIMEOUT;
	do {
		icss = EREAD4(sc, EAP_ICSS);
		DPRINTFN(5,("eap: codec %d prog: icss=0x%08x\n", a, icss));
		if (!to--) {
			printf("%s: timeout writing to codec\n",
			    sc->sc_dev.dv_xname);
			return;
		}
	} while (icss & EAP_CWRIP);  /* XXX could use CSTAT here */
	EWRITE4(sc, EAP_CODEC, EAP_SET_CODEC(a, d));
}

/*
 * Reading and writing the CODEC is very convoluted.  This mimics the
 * FreeBSD and Linux drivers.
 */

static __inline void
eap1371_ready_codec(struct eap_softc *sc, u_int8_t a, u_int32_t wd)
{
	int to;
	u_int32_t src, t;

	for (to = 0; to < EAP_WRITE_TIMEOUT; to++) {
		if (!(EREAD4(sc, E1371_CODEC) & E1371_CODEC_WIP))
			break;
		delay(1);
	}
	if (to == EAP_WRITE_TIMEOUT)
		printf("%s: eap1371_ready_codec timeout 1\n",
		    sc->sc_dev.dv_xname);

	mtx_enter(&audio_lock);
	src = eap1371_src_wait(sc) & E1371_SRC_CTLMASK;
	EWRITE4(sc, E1371_SRC, src | E1371_SRC_STATE_OK);

	for (to = 0; to < EAP_READ_TIMEOUT; to++) {
		t = EREAD4(sc, E1371_SRC);
		if ((t & E1371_SRC_STATE_MASK) == 0)
			break;
		delay(1);
	}
	if (to == EAP_READ_TIMEOUT)
		printf("%s: eap1371_ready_codec timeout 2\n",
		    sc->sc_dev.dv_xname);

	for (to = 0; to < EAP_READ_TIMEOUT; to++) {
		t = EREAD4(sc, E1371_SRC);
		if ((t & E1371_SRC_STATE_MASK) == E1371_SRC_STATE_OK)
			break;
		delay(1);
	}
	if (to == EAP_READ_TIMEOUT)
		printf("%s: eap1371_ready_codec timeout 3\n",
		    sc->sc_dev.dv_xname);

	EWRITE4(sc, E1371_CODEC, wd);

	eap1371_src_wait(sc);
	EWRITE4(sc, E1371_SRC, src);

	mtx_leave(&audio_lock);
}

int
eap1371_read_codec(void *sc_, u_int8_t a, u_int16_t *d)
{
	struct eap_softc *sc = sc_;
	int to;
	u_int32_t t;

	eap1371_ready_codec(sc, a, E1371_SET_CODEC(a, 0) | E1371_CODEC_READ);

	for (to = 0; to < EAP_WRITE_TIMEOUT; to++) {
		if (!(EREAD4(sc, E1371_CODEC) & E1371_CODEC_WIP))
			break;
		delay(1);
	}
	if (to == EAP_WRITE_TIMEOUT)
		printf("%s: eap1371_read_codec timeout 1\n",
		    sc->sc_dev.dv_xname);

	for (to = 0; to < EAP_WRITE_TIMEOUT; to++) {
		t = EREAD4(sc, E1371_CODEC);
		if (t & E1371_CODEC_VALID)
			break;
		delay(1);
	}
	if (to == EAP_WRITE_TIMEOUT)
		printf("%s: eap1371_read_codec timeout 2\n",
		    sc->sc_dev.dv_xname);

	*d = (u_int16_t)t;

	DPRINTFN(10, ("eap1371: reading codec (%x) = %x\n", a, *d));

	return (0);
}

int
eap1371_write_codec(void *sc_, u_int8_t a, u_int16_t d)
{
	struct eap_softc *sc = sc_;

	eap1371_ready_codec(sc, a, E1371_SET_CODEC(a, d));

        DPRINTFN(10, ("eap1371: writing codec %x --> %x\n", d, a));

	return (0);
}

u_int32_t
eap1371_src_wait(struct eap_softc *sc)
{
	int to;
	u_int32_t src = 0;
	
	for (to = 0; to < EAP_READ_TIMEOUT; to++) {
		src = EREAD4(sc, E1371_SRC);
		if (!(src & E1371_SRC_RBUSY))
			return (src);
		delay(1);
	}
	printf("%s: eap1371_src_wait timeout\n", sc->sc_dev.dv_xname);
	return (src);
}

void
eap1371_src_write(struct eap_softc *sc, int a, int d)
{
	u_int32_t r;

	r = eap1371_src_wait(sc) & E1371_SRC_CTLMASK;
	r |= E1371_SRC_RAMWE | E1371_SRC_ADDR(a) | E1371_SRC_DATA(d);
	EWRITE4(sc, E1371_SRC, r);
}
	
void
eap_attach(struct device *parent, struct device *self, void *aux)
{
	struct eap_softc *sc = (struct eap_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	const struct audio_hw_if *eap_hw_if;
	char const *intrstr;
	pci_intr_handle_t ih;
	mixer_ctrl_t ctl;
	int i;
	int revision;

	/* Flag if we're "creative" */
	sc->sc_1371 = !(PCI_VENDOR(pa->pa_id) == PCI_VENDOR_ENSONIQ &&
	    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ENSONIQ_AUDIOPCI);

	revision = PCI_REVISION(pa->pa_class);
	if (sc->sc_1371) {
		if (PCI_VENDOR(pa->pa_id) == PCI_VENDOR_ENSONIQ &&
		    ((PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ENSONIQ_AUDIOPCI97 &&
		    (revision == EAP_ES1373_8 || revision == EAP_CT5880_A)) ||
		    PCI_PRODUCT(pa->pa_id) == PCI_PRODUCT_ENSONIQ_CT5880))
			sc->sc_ct5880 = 1;
	}

	/* Map I/O register */
	if (pci_mapreg_map(pa, PCI_CBIO, PCI_MAPREG_TYPE_IO, 0,
	    &sc->iot, &sc->ioh, NULL, NULL, 0)) {
		return;
	}

	sc->sc_dmatag = pa->pa_dmat;

	/* Map and establish the interrupt. */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_AUDIO | IPL_MPSAFE,
	    eap_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s\n", intrstr);

	if (!sc->sc_1371) {
		/* Enable interrupts and looping mode. */
		/* enable the parts we need */
		EWRITE4(sc, EAP_SIC, EAP_P2_INTR_EN | EAP_R1_INTR_EN);
		EWRITE4(sc, EAP_ICSC, EAP_CDC_EN);

		/* reset codec */
		/* normal operation */
		/* select codec clocks */
		eap1370_write_codec(sc, AK_RESET, AK_PD);
		eap1370_write_codec(sc, AK_RESET, AK_PD | AK_NRST);
		eap1370_write_codec(sc, AK_CS, 0x0);

		eap_hw_if = &eap1370_hw_if;

		/* Enable all relevant mixer switches. */
		ctl.dev = EAP_INPUT_SOURCE;
		ctl.type = AUDIO_MIXER_SET;
		ctl.un.mask = 1 << EAP_VOICE_VOL | 1 << EAP_FM_VOL |
		    1 << EAP_CD_VOL | 1 << EAP_LINE_VOL | 1 << EAP_AUX_VOL |
		    1 << EAP_MIC_VOL;
		eap_hw_if->set_port(sc, &ctl);

		ctl.type = AUDIO_MIXER_VALUE;
		ctl.un.value.num_channels = 1;
		for (ctl.dev = EAP_MASTER_VOL; ctl.dev < EAP_MIC_VOL;
		     ctl.dev++) {
			ctl.un.value.level[AUDIO_MIXER_LEVEL_MONO] = VOL_0DB;
			eap_hw_if->set_port(sc, &ctl);
		}
		ctl.un.value.level[AUDIO_MIXER_LEVEL_MONO] = 0;
		eap_hw_if->set_port(sc, &ctl);
		ctl.dev = EAP_MIC_PREAMP;
		ctl.type = AUDIO_MIXER_ENUM;
		ctl.un.ord = 0;
		eap_hw_if->set_port(sc, &ctl);
		ctl.dev = EAP_RECORD_SOURCE;
		ctl.type = AUDIO_MIXER_SET;
		ctl.un.mask = 1 << EAP_MIC_VOL;
		eap_hw_if->set_port(sc, &ctl);
	} else {
		/* clean slate */

                EWRITE4(sc, EAP_SIC, 0);
		EWRITE4(sc, EAP_ICSC, 0);
		EWRITE4(sc, E1371_LEGACY, 0);

		if (sc->sc_ct5880) {
			EWRITE4(sc, EAP_ICSS, EAP_CT5880_AC97_RESET);
			/* Let codec wake up */
			delay(20000);
		}

                /* Reset from es1371's perspective */
                EWRITE4(sc, EAP_ICSC, E1371_SYNC_RES);
                delay(20);
                EWRITE4(sc, EAP_ICSC, 0);

		/*
		 * Must properly reprogram sample rate converter,
		 * or it locks up.
		 *
		 * We don't know how to program it (no documentation),
		 * and the linux/oss magic recipe doesn't work (breaks
		 * full-duplex, by selecting different play and record
		 * rates). On the other hand, the sample rate converter
		 * can't be disabled (disabling it would disable DMA),
		 * so we use these magic defaults that make it "resample"
		 * 48kHz to 48kHz without breaking full-duplex.
		 */
		EWRITE4(sc, E1371_SRC, E1371_SRC_DISABLE);
		for (i = 0; i < 0x80; i++)
			eap1371_src_write(sc, i, 0);
		eap1371_src_write(sc, ESRC_ADC + ESRC_TRUNC_N, ESRC_SET_N(16));
		eap1371_src_write(sc, ESRC_ADC + ESRC_IREGS, ESRC_SET_VFI(16));
		eap1371_src_write(sc, ESRC_ADC + ESRC_VFF, 0);
		eap1371_src_write(sc, ESRC_ADC_VOLL, ESRC_SET_ADC_VOL(16));
		eap1371_src_write(sc, ESRC_ADC_VOLR, ESRC_SET_ADC_VOL(16));
		eap1371_src_write(sc, ESRC_DAC1 + ESRC_TRUNC_N, ESRC_SET_N(16));
		eap1371_src_write(sc, ESRC_DAC1 + ESRC_IREGS, ESRC_SET_VFI(16));
		eap1371_src_write(sc, ESRC_DAC1 + ESRC_VFF, 0);
		eap1371_src_write(sc, ESRC_DAC1_VOLL, ESRC_SET_DAC_VOLI(1));
		eap1371_src_write(sc, ESRC_DAC1_VOLR, ESRC_SET_DAC_VOLI(1));
		eap1371_src_write(sc, ESRC_DAC2 + ESRC_IREGS, ESRC_SET_VFI(16));
		eap1371_src_write(sc, ESRC_DAC2 + ESRC_TRUNC_N, ESRC_SET_N(16));
		eap1371_src_write(sc, ESRC_DAC2 + ESRC_VFF, 0);
		eap1371_src_write(sc, ESRC_DAC2_VOLL, ESRC_SET_DAC_VOLI(1));
		eap1371_src_write(sc, ESRC_DAC2_VOLR, ESRC_SET_DAC_VOLI(1));
		EWRITE4(sc, E1371_SRC, 0);

		/* Reset codec */

		/* Interrupt enable */
		sc->host_if.arg = sc;
		sc->host_if.attach = eap1371_attach_codec;
		sc->host_if.read = eap1371_read_codec;
		sc->host_if.write = eap1371_write_codec;
		sc->host_if.reset = eap1371_reset_codec;
		sc->host_if.flags = eap_flags_codec;
		sc->flags = AC97_HOST_DONT_READ;
	
		if (ac97_attach(&sc->host_if) == 0) {
			/* Interrupt enable */
			EWRITE4(sc, EAP_SIC, EAP_P2_INTR_EN | EAP_R1_INTR_EN);
		} else
			return;

		eap_hw_if = &eap1371_hw_if;
	}

	audio_attach_mi(eap_hw_if, sc, NULL, &sc->sc_dev);
#if NMIDI > 0
	sc->sc_mididev = midi_attach_mi(&eap_midi_hw_if, sc, &sc->sc_dev);
#endif
}

void
eap_resume(struct eap_softc *sc)
{
	int i;

	if (!sc->sc_1371) {
		/* Enable interrupts and looping mode. */
		/* enable the parts we need */
		EWRITE4(sc, EAP_SIC, EAP_P2_INTR_EN | EAP_R1_INTR_EN);
		EWRITE4(sc, EAP_ICSC, EAP_CDC_EN);

		/* reset codec */
		/* normal operation */
		/* select codec clocks */
		eap1370_write_codec(sc, AK_RESET, AK_PD);
		eap1370_write_codec(sc, AK_RESET, AK_PD | AK_NRST);
		eap1370_write_codec(sc, AK_CS, 0x0);

	} else {
		/* clean slate */

		EWRITE4(sc, EAP_SIC, 0);
		EWRITE4(sc, EAP_ICSC, 0);
		EWRITE4(sc, E1371_LEGACY, 0);

		if (sc->sc_ct5880) {
			EWRITE4(sc, EAP_ICSS, EAP_CT5880_AC97_RESET);
			/* Let codec wake up */
			delay(20000);
		}

		ac97_resume(&sc->host_if, sc->codec_if);

		EWRITE4(sc, E1371_SRC, E1371_SRC_DISABLE);
		for (i = 0; i < 0x80; i++)
			eap1371_src_write(sc, i, 0);
		eap1371_src_write(sc, ESRC_ADC + ESRC_TRUNC_N, ESRC_SET_N(16));
		eap1371_src_write(sc, ESRC_ADC + ESRC_IREGS, ESRC_SET_VFI(16));
		eap1371_src_write(sc, ESRC_ADC + ESRC_VFF, 0);
		eap1371_src_write(sc, ESRC_ADC_VOLL, ESRC_SET_ADC_VOL(16));
		eap1371_src_write(sc, ESRC_ADC_VOLR, ESRC_SET_ADC_VOL(16));
		eap1371_src_write(sc, ESRC_DAC1 + ESRC_TRUNC_N, ESRC_SET_N(16));
		eap1371_src_write(sc, ESRC_DAC1 + ESRC_IREGS, ESRC_SET_VFI(16));
		eap1371_src_write(sc, ESRC_DAC1 + ESRC_VFF, 0);
		eap1371_src_write(sc, ESRC_DAC1_VOLL, ESRC_SET_DAC_VOLI(1));
		eap1371_src_write(sc, ESRC_DAC1_VOLR, ESRC_SET_DAC_VOLI(1));
		eap1371_src_write(sc, ESRC_DAC2 + ESRC_IREGS, ESRC_SET_VFI(16));
		eap1371_src_write(sc, ESRC_DAC2 + ESRC_TRUNC_N, ESRC_SET_N(16));
		eap1371_src_write(sc, ESRC_DAC2 + ESRC_VFF, 0);
		eap1371_src_write(sc, ESRC_DAC2_VOLL, ESRC_SET_DAC_VOLI(1));
		eap1371_src_write(sc, ESRC_DAC2_VOLR, ESRC_SET_DAC_VOLI(1));
		EWRITE4(sc, E1371_SRC, 0);

		/* Interrupt enable */
		EWRITE4(sc, EAP_SIC, EAP_P2_INTR_EN | EAP_R1_INTR_EN);
	}
}


int
eap1371_attach_codec(void *sc_, struct ac97_codec_if *codec_if)
{
	struct eap_softc *sc = sc_;
	
	sc->codec_if = codec_if;
	return (0);
}

void
eap1371_reset_codec(void *sc_)
{
	struct eap_softc *sc = sc_;
	u_int32_t icsc;

	mtx_enter(&audio_lock);
	icsc = EREAD4(sc, EAP_ICSC);
	EWRITE4(sc, EAP_ICSC, icsc | E1371_SYNC_RES);
	delay(20);
	EWRITE4(sc, EAP_ICSC, icsc & ~E1371_SYNC_RES);
	delay(1);
	mtx_leave(&audio_lock);

	return;
}

int
eap_intr(void *p)
{
	struct eap_softc *sc = p;
	u_int32_t intr, sic;

	mtx_enter(&audio_lock);
	intr = EREAD4(sc, EAP_ICSS);
	if (!(intr & EAP_INTR)) {
		mtx_leave(&audio_lock);
		return (0);
	}
	sic = EREAD4(sc, EAP_SIC);
	DPRINTFN(5, ("eap_intr: ICSS=0x%08x, SIC=0x%08x\n", intr, sic));
	if (intr & EAP_I_ADC) {
#if 0
		/*
		 * XXX This is a hack!
		 * The EAP chip sometimes generates the recording interrupt
		 * while it is still transferring the data.  To make sure
		 * it has all arrived we busy wait until the count is right.
		 * The transfer we are waiting for is 8 longwords.
		 */
		int s, nw, n;

		EWRITE4(sc, EAP_MEMPAGE, EAP_ADC_PAGE);
		s = EREAD4(sc, EAP_ADC_CSR);
		nw = ((s & 0xffff) + 1) >> 2; /* # of words in DMA */
		n = 0;
		while (((EREAD4(sc, EAP_ADC_SIZE) >> 16) + 8) % nw == 0) {
			delay(10);
			if (++n > 100) {
				printf("eapintr: dma fix timeout");
				break;
			}
		}
		/* Continue with normal interrupt handling. */
#endif
		EWRITE4(sc, EAP_SIC, sic & ~EAP_R1_INTR_EN);
		EWRITE4(sc, EAP_SIC, sic | EAP_R1_INTR_EN);
		if (sc->sc_rintr)
			sc->sc_rintr(sc->sc_rarg);
	}
	if (intr & EAP_I_DAC2) {
		EWRITE4(sc, EAP_SIC, sic & ~EAP_P2_INTR_EN);
		EWRITE4(sc, EAP_SIC, sic | EAP_P2_INTR_EN);
		if (sc->sc_pintr)
			sc->sc_pintr(sc->sc_parg);
	}
#if NMIDI > 0
	if (intr & EAP_I_UART) {
		u_int32_t data;

		if (EREAD1(sc, EAP_UART_STATUS) & EAP_US_RXINT) {
			while (EREAD1(sc, EAP_UART_STATUS) & EAP_US_RXRDY) {
				data = EREAD1(sc, EAP_UART_DATA);
				if (sc->sc_iintr)
					sc->sc_iintr(sc->sc_arg, data);
			}
		}
		if (EREAD1(sc, EAP_UART_STATUS) & EAP_US_TXINT) {
			sc->sc_uctrl &= ~EAP_UC_TXINTEN;
			EWRITE1(sc, EAP_UART_CONTROL, sc->sc_uctrl);
			if (sc->sc_ointr) 
				sc->sc_ointr(sc->sc_arg);
		}
	}
#endif
	mtx_leave(&audio_lock);
	return (1);
}

int
eap_allocmem(struct eap_softc *sc, size_t size, size_t align, struct eap_dma *p)
{
	int error;

	p->size = size;
	error = bus_dmamem_alloc(sc->sc_dmatag, p->size, align, 0,
	    p->segs, nitems(p->segs),
	    &p->nsegs, BUS_DMA_NOWAIT);
	if (error)
		return (error);

	error = bus_dmamem_map(sc->sc_dmatag, p->segs, p->nsegs, p->size,
	    &p->addr, BUS_DMA_NOWAIT|BUS_DMA_COHERENT);
	if (error)
		goto free;

	error = bus_dmamap_create(sc->sc_dmatag, p->size, 1, p->size,
	    0, BUS_DMA_NOWAIT, &p->map);
	if (error)
		goto unmap;

	error = bus_dmamap_load(sc->sc_dmatag, p->map, p->addr, p->size, NULL,
	    BUS_DMA_NOWAIT);
	if (error)
		goto destroy;
	return (0);

destroy:
	bus_dmamap_destroy(sc->sc_dmatag, p->map);
unmap:
	bus_dmamem_unmap(sc->sc_dmatag, p->addr, p->size);
free:
	bus_dmamem_free(sc->sc_dmatag, p->segs, p->nsegs);
	return (error);
}

int
eap_freemem(struct eap_softc *sc, struct eap_dma *p)
{
	bus_dmamap_unload(sc->sc_dmatag, p->map);
	bus_dmamap_destroy(sc->sc_dmatag, p->map);
	bus_dmamem_unmap(sc->sc_dmatag, p->addr, p->size);
	bus_dmamem_free(sc->sc_dmatag, p->segs, p->nsegs);
	return (0);
}

int
eap_open(void *addr, int flags)
{
	return (0);
}

/*
 * Close function is called at splaudio().
 */
void
eap_close(void *addr)
{
	struct eap_softc *sc = addr;
    
	eap_halt_output(sc);
	eap_halt_input(sc);

	sc->sc_pintr = 0;
	sc->sc_rintr = 0;
}

int
eap_set_params(void *addr, int setmode, int usemode,
    struct audio_params *play, struct audio_params *rec)
{
	struct eap_softc *sc = addr;
	struct audio_params *p;
	int mode;
	u_int32_t div;

	/*
	 * The es1370 only has one clock, so make the sample rates match.
	 */
	if (!sc->sc_1371) {
		if (play->sample_rate != rec->sample_rate &&
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
	}

	for (mode = AUMODE_RECORD; mode != -1;
	    mode = mode == AUMODE_RECORD ? AUMODE_PLAY : -1) {
		if ((setmode & mode) == 0)
			continue;

		p = mode == AUMODE_PLAY ? play : rec;

		if (sc->sc_1371)
			p->sample_rate = 48000;
		if (p->sample_rate < 4000)
			p->sample_rate = 4000;
		if (p->sample_rate > 48000)
			p->sample_rate = 48000;
		if (p->precision > 16)
			p->precision = 16;
		if (p->channels > 2)
			p->channels = 2;
		switch (p->encoding) {
		case AUDIO_ENCODING_SLINEAR_LE:
			if (p->precision != 16)
				return EINVAL;
			break;
		case AUDIO_ENCODING_ULINEAR_LE:
		case AUDIO_ENCODING_ULINEAR_BE:
			if (p->precision != 8)
				return EINVAL;
		default:
			return (EINVAL);
		}
		p->bps = AUDIO_BPS(p->precision);
		p->msb = 1;
	}

	if (!sc->sc_1371) {
		/* Set the speed */
		DPRINTFN(2, ("eap_set_params: old ICSC = 0x%08x\n",
		    EREAD4(sc, EAP_ICSC)));
		div = EREAD4(sc, EAP_ICSC) & ~EAP_PCLKBITS;
		/*
		 * XXX
		 * The -2 isn't documented, but seemed to make the wall
		 * time match
		 * what I expect.  - mycroft
		 */
		if (usemode == AUMODE_RECORD)
			div |= EAP_SET_PCLKDIV(EAP_XTAL_FREQ /
			    rec->sample_rate - 2);
		else
			div |= EAP_SET_PCLKDIV(EAP_XTAL_FREQ /
			    play->sample_rate - 2);
		div |= EAP_CCB_INTRM;
		EWRITE4(sc, EAP_ICSC, div);
		DPRINTFN(2, ("eap_set_params: set ICSC = 0x%08x\n", div));
	}

	return (0);
}

int
eap_round_blocksize(void *addr, int blk)
{
	return ((blk + 31) & -32);	/* keep good alignment */
}

int
eap_trigger_output(
	void *addr,
	void *start,
	void *end,
	int blksize,
	void (*intr)(void *),
	void *arg,
	struct audio_params *param)
{
	struct eap_softc *sc = addr;
	struct eap_dma *p;
	u_int32_t icsc, sic;
	int sampshift;

#ifdef DIAGNOSTIC
	if (sc->sc_prun)
		panic("eap_trigger_output: already running");
	sc->sc_prun = 1;
#endif

	DPRINTFN(1, ("eap_trigger_output: sc=%p start=%p end=%p "
	    "blksize=%d intr=%p(%p)\n", addr, start, end, blksize, intr, arg));
	sc->sc_pintr = intr;
	sc->sc_parg = arg;
	mtx_enter(&audio_lock);
	sic = EREAD4(sc, EAP_SIC);
	sic &= ~(EAP_P2_S_EB | EAP_P2_S_MB | EAP_INC_BITS);
	sic |= EAP_SET_P2_ST_INC(0) | EAP_SET_P2_END_INC(param->precision / 8);
	sampshift = 0;
	if (param->precision == 16) {
		sic |= EAP_P2_S_EB;
		sampshift++;
	}
	if (param->channels == 2) {
		sic |= EAP_P2_S_MB;
		sampshift++;
	}
	EWRITE4(sc, EAP_SIC, sic & ~EAP_P2_INTR_EN);
	EWRITE4(sc, EAP_SIC, sic | EAP_P2_INTR_EN);

	for (p = sc->sc_dmas; p && KERNADDR(p) != start; p = p->next)
		;
	if (!p) {
		mtx_leave(&audio_lock);
		printf("eap_trigger_output: bad addr %p\n", start);
		return (EINVAL);
	}

	DPRINTF(("eap_trigger_output: DAC2_ADDR=0x%x, DAC2_SIZE=0x%x\n",
	    (int)DMAADDR(p),
	    (int)EAP_SET_SIZE(0, (((char *)end - (char *)start) >> 2) - 1)));
	EWRITE4(sc, EAP_MEMPAGE, EAP_DAC_PAGE);
	EWRITE4(sc, EAP_DAC2_ADDR, DMAADDR(p));
	EWRITE4(sc, EAP_DAC2_SIZE,
	    EAP_SET_SIZE(0, (((char *)end - (char *)start) >> 2) - 1));

	EWRITE4(sc, EAP_DAC2_CSR, (blksize >> sampshift) - 1);

	if (sc->sc_1371)
		EWRITE4(sc, E1371_SRC, 0);

	icsc = EREAD4(sc, EAP_ICSC);
	EWRITE4(sc, EAP_ICSC, icsc | EAP_DAC2_EN);

	DPRINTFN(1, ("eap_trigger_output: set ICSC = 0x%08x\n", icsc));
	mtx_leave(&audio_lock);
	return (0);
}

int
eap_trigger_input(
	void *addr,
	void *start,
	void *end,
	int blksize,
	void (*intr)(void *),
	void *arg,
	struct audio_params *param)
{
	struct eap_softc *sc = addr;
	struct eap_dma *p;
	u_int32_t icsc, sic;
	int sampshift;

#ifdef DIAGNOSTIC
	if (sc->sc_rrun)
		panic("eap_trigger_input: already running");
	sc->sc_rrun = 1;
#endif

	DPRINTFN(1, ("eap_trigger_input: sc=%p start=%p end=%p blksize=%d intr=%p(%p)\n",
	    addr, start, end, blksize, intr, arg));
	sc->sc_rintr = intr;
	sc->sc_rarg = arg;
	mtx_enter(&audio_lock);
	sic = EREAD4(sc, EAP_SIC);
	sic &= ~(EAP_R1_S_EB | EAP_R1_S_MB);
	sampshift = 0;
	if (param->precision == 16) {
		sic |= EAP_R1_S_EB;
		sampshift++;
	}
	if (param->channels == 2) {
		sic |= EAP_R1_S_MB;
		sampshift++;
	}
	EWRITE4(sc, EAP_SIC, sic & ~EAP_R1_INTR_EN);
	EWRITE4(sc, EAP_SIC, sic | EAP_R1_INTR_EN);

	for (p = sc->sc_dmas; p && KERNADDR(p) != start; p = p->next)
		;
	if (!p) {
		mtx_leave(&audio_lock);
		printf("eap_trigger_input: bad addr %p\n", start);
		return (EINVAL);
	}

	DPRINTF(("eap_trigger_input: ADC_ADDR=0x%x, ADC_SIZE=0x%x\n",
	    (int)DMAADDR(p),
	    (int)EAP_SET_SIZE(0, (((char *)end - (char *)start) >> 2) - 1)));
	EWRITE4(sc, EAP_MEMPAGE, EAP_ADC_PAGE);
	EWRITE4(sc, EAP_ADC_ADDR, DMAADDR(p));
	EWRITE4(sc, EAP_ADC_SIZE,
	    EAP_SET_SIZE(0, (((char *)end - (char *)start) >> 2) - 1));

	EWRITE4(sc, EAP_ADC_CSR, (blksize >> sampshift) - 1);

	if (sc->sc_1371)
		EWRITE4(sc, E1371_SRC, 0);

	icsc = EREAD4(sc, EAP_ICSC);
	EWRITE4(sc, EAP_ICSC, icsc | EAP_ADC_EN);

	DPRINTFN(1, ("eap_trigger_input: set ICSC = 0x%08x\n", icsc));
	mtx_leave(&audio_lock);
	return (0);
}

int
eap_halt_output(void *addr)
{
	struct eap_softc *sc = addr;
	u_int32_t icsc;
	
	DPRINTF(("eap: eap_halt_output\n"));
	mtx_enter(&audio_lock);
	icsc = EREAD4(sc, EAP_ICSC);
	EWRITE4(sc, EAP_ICSC, icsc & ~EAP_DAC2_EN);
#ifdef DIAGNOSTIC
	sc->sc_prun = 0;
#endif
	mtx_leave(&audio_lock);
	return (0);
}

int
eap_halt_input(void *addr)
{
	struct eap_softc *sc = addr;
	u_int32_t icsc;
    
	DPRINTF(("eap: eap_halt_input\n"));
	mtx_enter(&audio_lock);
	icsc = EREAD4(sc, EAP_ICSC);
	EWRITE4(sc, EAP_ICSC, icsc & ~EAP_ADC_EN);
#ifdef DIAGNOSTIC
	sc->sc_rrun = 0;
#endif
	mtx_leave(&audio_lock);
	return (0);
}

int
eap1371_mixer_set_port(void *addr, mixer_ctrl_t *cp)
{
	struct eap_softc *sc = addr;

	return (sc->codec_if->vtbl->mixer_set_port(sc->codec_if, cp));
}

int
eap1371_mixer_get_port(void *addr, mixer_ctrl_t *cp)
{
	struct eap_softc *sc = addr;

	return (sc->codec_if->vtbl->mixer_get_port(sc->codec_if, cp));
}

int
eap1371_query_devinfo(void *addr, mixer_devinfo_t *dip)
{
	struct eap_softc *sc = addr;

	return (sc->codec_if->vtbl->query_devinfo(sc->codec_if, dip));
}

void
eap1370_set_mixer(struct eap_softc *sc, int a, int d)
{
	eap1370_write_codec(sc, a, d);

	sc->sc_port[a] = d;
	DPRINTFN(1, ("eap1370_mixer_set_port port 0x%02x = 0x%02x\n", a, d));
}

int
eap1370_mixer_set_port(void *addr, mixer_ctrl_t *cp)
{
	struct eap_softc *sc = addr;
	int lval, rval, l, r, la, ra;
	int l1, r1, l2, r2, m, o1, o2;

	if (cp->dev == EAP_RECORD_SOURCE) {
		if (cp->type != AUDIO_MIXER_SET)
			return (EINVAL);
		m = sc->sc_record_source = cp->un.mask;
		l1 = l2 = r1 = r2 = 0;
		if (m & (1 << EAP_VOICE_VOL))
			l2 |= AK_M_VOICE, r2 |= AK_M_VOICE;
		if (m & (1 << EAP_FM_VOL))
			l1 |= AK_M_FM_L, r1 |= AK_M_FM_R;
		if (m & (1 << EAP_CD_VOL))
			l1 |= AK_M_CD_L, r1 |= AK_M_CD_R;
		if (m & (1 << EAP_LINE_VOL))
			l1 |= AK_M_LINE_L, r1 |= AK_M_LINE_R;
		if (m & (1 << EAP_AUX_VOL))
			l2 |= AK_M2_AUX_L, r2 |= AK_M2_AUX_R;
		if (m & (1 << EAP_MIC_VOL))
			l2 |= AK_M_TMIC, r2 |= AK_M_TMIC;
		eap1370_set_mixer(sc, AK_IN_MIXER1_L, l1);
		eap1370_set_mixer(sc, AK_IN_MIXER1_R, r1);
		eap1370_set_mixer(sc, AK_IN_MIXER2_L, l2);
		eap1370_set_mixer(sc, AK_IN_MIXER2_R, r2);
		return (0);
	}
	if (cp->dev == EAP_INPUT_SOURCE) {
		if (cp->type != AUDIO_MIXER_SET)
			return (EINVAL);
		m = sc->sc_input_source = cp->un.mask;
		o1 = o2 = 0;
		if (m & (1 << EAP_VOICE_VOL))
			o2 |= AK_M_VOICE_L | AK_M_VOICE_R;
		if (m & (1 << EAP_FM_VOL))
			o1 |= AK_M_FM_L | AK_M_FM_R;
		if (m & (1 << EAP_CD_VOL))
			o1 |= AK_M_CD_L | AK_M_CD_R;
		if (m & (1 << EAP_LINE_VOL))
			o1 |= AK_M_LINE_L | AK_M_LINE_R;
		if (m & (1 << EAP_AUX_VOL))
			o2 |= AK_M_AUX_L | AK_M_AUX_R;
		if (m & (1 << EAP_MIC_VOL))
			o1 |= AK_M_MIC;
		eap1370_set_mixer(sc, AK_OUT_MIXER1, o1);
		eap1370_set_mixer(sc, AK_OUT_MIXER2, o2);
		return (0);
	}
	if (cp->dev == EAP_MIC_PREAMP) {
		if (cp->type != AUDIO_MIXER_ENUM)
			return (EINVAL);
		if (cp->un.ord != 0 && cp->un.ord != 1)
			return (EINVAL);
		sc->sc_mic_preamp = cp->un.ord;
		eap1370_set_mixer(sc, AK_MGAIN, cp->un.ord);
		return (0);
	}
	if (cp->type != AUDIO_MIXER_VALUE)
		return (EINVAL);
	if (cp->un.value.num_channels == 1)
		lval = rval = cp->un.value.level[AUDIO_MIXER_LEVEL_MONO];
	else if (cp->un.value.num_channels == 2) {
		lval = cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT];
		rval = cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT];
	} else
		return (EINVAL);
	ra = -1;
	switch (cp->dev) {
	case EAP_MASTER_VOL:
		l = VOL_TO_ATT5(lval);
		r = VOL_TO_ATT5(rval);
		la = AK_MASTER_L;
		ra = AK_MASTER_R;
		break;
	case EAP_MIC_VOL:
		if (cp->un.value.num_channels != 1)
			return (EINVAL);
		la = AK_MIC;
		goto lr;
	case EAP_VOICE_VOL:
		la = AK_VOICE_L;
		ra = AK_VOICE_R;
		goto lr;
	case EAP_FM_VOL:
		la = AK_FM_L;
		ra = AK_FM_R;
		goto lr;
	case EAP_CD_VOL:
		la = AK_CD_L;
		ra = AK_CD_R;
		goto lr;
	case EAP_LINE_VOL:
		la = AK_LINE_L;
		ra = AK_LINE_R;
		goto lr;
	case EAP_AUX_VOL:
		la = AK_AUX_L;
		ra = AK_AUX_R;
	lr:
		l = VOL_TO_GAIN5(lval);
		r = VOL_TO_GAIN5(rval);
		break;
	default:
		return (EINVAL);
	}
	eap1370_set_mixer(sc, la, l);
	if (ra >= 0) {
		eap1370_set_mixer(sc, ra, r);
	}
	return (0);
}

int
eap1370_mixer_get_port(void *addr, mixer_ctrl_t *cp)
{
	struct eap_softc *sc = addr;
	int la, ra, l, r;

	switch (cp->dev) {
	case EAP_RECORD_SOURCE:
		if (cp->type != AUDIO_MIXER_SET)
			return (EINVAL);
		cp->un.mask = sc->sc_record_source;
		return (0);
	case EAP_INPUT_SOURCE:
		if (cp->type != AUDIO_MIXER_SET)
			return (EINVAL);
		cp->un.mask = sc->sc_input_source;
		return (0);
	case EAP_MIC_PREAMP:
		if (cp->type != AUDIO_MIXER_ENUM)
			return (EINVAL);
		cp->un.ord = sc->sc_mic_preamp;
		return (0);
	case EAP_MASTER_VOL:
		l = ATT5_TO_VOL(sc->sc_port[AK_MASTER_L]);
		r = ATT5_TO_VOL(sc->sc_port[AK_MASTER_R]);
		break;
	case EAP_MIC_VOL:
		if (cp->un.value.num_channels != 1)
			return (EINVAL);
		la = ra = AK_MIC;
		goto lr;
	case EAP_VOICE_VOL:
		la = AK_VOICE_L;
		ra = AK_VOICE_R;
		goto lr;
	case EAP_FM_VOL:
		la = AK_FM_L;
		ra = AK_FM_R;
		goto lr;
	case EAP_CD_VOL:
		la = AK_CD_L;
		ra = AK_CD_R;
		goto lr;
	case EAP_LINE_VOL:
		la = AK_LINE_L;
		ra = AK_LINE_R;
		goto lr;
	case EAP_AUX_VOL:
		la = AK_AUX_L;
		ra = AK_AUX_R;
	lr:
		l = GAIN5_TO_VOL(sc->sc_port[la]);
		r = GAIN5_TO_VOL(sc->sc_port[ra]);
		break;
	default:
		return (EINVAL);
	}
	if (cp->un.value.num_channels == 1)
		cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] = (l+r) / 2;
	else if (cp->un.value.num_channels == 2) {
		cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT]  = l;
		cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] = r;
	} else
		return (EINVAL);
	return (0);
}

int
eap1370_query_devinfo(void *addr, mixer_devinfo_t *dip)
{
	switch (dip->index) {
	case EAP_MASTER_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = EAP_OUTPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmaster, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		return (0);
	case EAP_VOICE_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = EAP_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNdac, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		return (0);
	case EAP_FM_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = EAP_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNfmsynth,
		    sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		return (0);
	case EAP_CD_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = EAP_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNcd, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		return (0);
	case EAP_LINE_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = EAP_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNline, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		return (0);
	case EAP_AUX_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = EAP_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNaux, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		return (0);
	case EAP_MIC_VOL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = EAP_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = EAP_MIC_PREAMP;
		strlcpy(dip->label.name, AudioNmicrophone,
		    sizeof dip->label.name);
		dip->un.v.num_channels = 1;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		return (0);
	case EAP_RECORD_SOURCE:
		dip->mixer_class = EAP_RECORD_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNsource, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_SET;
		dip->un.s.num_mem = 6;
		strlcpy(dip->un.s.member[0].label.name, AudioNmicrophone,
		    sizeof dip->un.s.member[0].label.name);
		dip->un.s.member[0].mask = 1 << EAP_MIC_VOL;
		strlcpy(dip->un.s.member[1].label.name, AudioNcd,
		    sizeof dip->un.s.member[1].label.name);
		dip->un.s.member[1].mask = 1 << EAP_CD_VOL;
		strlcpy(dip->un.s.member[2].label.name, AudioNline,
		    sizeof dip->un.s.member[2].label.name);
		dip->un.s.member[2].mask = 1 << EAP_LINE_VOL;
		strlcpy(dip->un.s.member[3].label.name, AudioNfmsynth,
		    sizeof dip->un.s.member[3].label.name);
		dip->un.s.member[3].mask = 1 << EAP_FM_VOL;
		strlcpy(dip->un.s.member[4].label.name, AudioNaux,
		    sizeof dip->un.s.member[4].label.name);
		dip->un.s.member[4].mask = 1 << EAP_AUX_VOL;
		strlcpy(dip->un.s.member[5].label.name, AudioNdac,
		    sizeof dip->un.s.member[5].label.name);
		dip->un.s.member[5].mask = 1 << EAP_VOICE_VOL;
		return (0);
	case EAP_INPUT_SOURCE:
		dip->mixer_class = EAP_INPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNsource, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_SET;
		dip->un.s.num_mem = 6;
		strlcpy(dip->un.s.member[0].label.name, AudioNmicrophone,
		    sizeof dip->un.s.member[0].label.name);
		dip->un.s.member[0].mask = 1 << EAP_MIC_VOL;
		strlcpy(dip->un.s.member[1].label.name, AudioNcd,
		    sizeof dip->un.s.member[1].label.name);
		dip->un.s.member[1].mask = 1 << EAP_CD_VOL;
		strlcpy(dip->un.s.member[2].label.name, AudioNline,
		    sizeof dip->un.s.member[2].label.name);
		dip->un.s.member[2].mask = 1 << EAP_LINE_VOL;
		strlcpy(dip->un.s.member[3].label.name, AudioNfmsynth,
		    sizeof dip->un.s.member[3].label.name);
		dip->un.s.member[3].mask = 1 << EAP_FM_VOL;
		strlcpy(dip->un.s.member[4].label.name, AudioNaux,
		    sizeof dip->un.s.member[4].label.name);
		dip->un.s.member[4].mask = 1 << EAP_AUX_VOL;
		strlcpy(dip->un.s.member[5].label.name, AudioNdac,
		    sizeof dip->un.s.member[5].label.name);
		dip->un.s.member[5].mask = 1 << EAP_VOICE_VOL;
		return (0);
	case EAP_MIC_PREAMP:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = EAP_INPUT_CLASS;
		dip->prev = EAP_MIC_VOL;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNpreamp, sizeof dip->label.name);
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNon,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		return (0);
	case EAP_OUTPUT_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = EAP_OUTPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCoutputs,
		    sizeof dip->label.name);
		return (0);
	case EAP_RECORD_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = EAP_RECORD_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCrecord, sizeof dip->label.name);
		return (0);
	case EAP_INPUT_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = EAP_INPUT_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCinputs, sizeof dip->label.name);
		return (0);
	}
	return (ENXIO);
}

void *
eap_malloc(void *addr, int direction, size_t size, int pool, int flags)
{
	struct eap_softc *sc = addr;
	struct eap_dma *p;
	int error;

	p = malloc(sizeof(*p), pool, flags);
	if (!p)
		return (0);
	error = eap_allocmem(sc, size, 16, p);
	if (error) {
		free(p, pool, sizeof(*p));
		return (0);
	}
	p->next = sc->sc_dmas;
	sc->sc_dmas = p;
	return (KERNADDR(p));
}

void
eap_free(void *addr, void *ptr, int pool)
{
	struct eap_softc *sc = addr;
	struct eap_dma **pp, *p;

	for (pp = &sc->sc_dmas; (p = *pp) != NULL; pp = &p->next) {
		if (KERNADDR(p) == ptr) {
			eap_freemem(sc, p);
			*pp = p->next;
			free(p, pool, sizeof(*p));
			return;
		}
	}
}

enum ac97_host_flags
eap_flags_codec(void *v)
{
      struct eap_softc *sc = v;

      return (sc->flags);
}
#if NMIDI > 0
int
eap_midi_open(void *addr, int flags,
    void (*iintr)(void *, int),
    void (*ointr)(void *),
    void *arg)
{
	struct eap_softc *sc = addr;

	sc->sc_iintr = iintr;
	sc->sc_ointr = ointr;
	sc->sc_arg = arg;

	EWRITE4(sc, EAP_ICSC, EREAD4(sc, EAP_ICSC) | EAP_UART_EN);
	sc->sc_uctrl = 0;
	if (flags & FREAD)
		sc->sc_uctrl |= EAP_UC_RXINTEN;
	EWRITE1(sc, EAP_UART_CONTROL, sc->sc_uctrl);

	return (0);
}

void
eap_midi_close(void *addr)
{
	struct eap_softc *sc = addr;

	/* give uart a chance to drain */
	tsleep_nsec(sc, PWAIT, "eapclm", MSEC_TO_NSEC(100));

	EWRITE1(sc, EAP_UART_CONTROL, 0);
	EWRITE4(sc, EAP_ICSC, EREAD4(sc, EAP_ICSC) & ~EAP_UART_EN);

	sc->sc_iintr = 0;
	sc->sc_ointr = 0;
}

int
eap_midi_output(void *addr, int d)
{
	struct eap_softc *sc = addr;

	if (!(EREAD1(sc, EAP_UART_STATUS) & EAP_US_TXRDY))
		return 0;
	EWRITE1(sc, EAP_UART_DATA, d);
	sc->sc_uctrl |= EAP_UC_TXINTEN;
	EWRITE1(sc, EAP_UART_CONTROL, sc->sc_uctrl);
	return 1;
}

void
eap_midi_getinfo(void *addr, struct midi_info *mi)
{
	mi->name = "AudioPCI MIDI UART";
	mi->props = MIDI_PROP_CAN_INPUT | MIDI_PROP_OUT_INTR;
}

#endif
