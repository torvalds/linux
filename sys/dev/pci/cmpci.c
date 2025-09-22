/*	$OpenBSD: cmpci.c,v 1.54 2024/05/24 06:02:53 jsg Exp $	*/
/*	$NetBSD: cmpci.c,v 1.25 2004/10/26 06:32:20 xtraeme Exp $	*/

/*
 * Copyright (c) 2000, 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Takuya SHIOZAKI <tshiozak@NetBSD.org> .
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by ITOH Yasufumi.
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
 */

/*
 * C-Media CMI8x38, CMI8768 Audio Chip Support.
 *
 * TODO:
 *   - Joystick support.
 *
 */

#if defined(AUDIO_DEBUG) || defined(DEBUG)
#define DPRINTF(x) if (cmpcidebug) printf x
int cmpcidebug = 0;
#else
#define DPRINTF(x)
#endif

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/device.h>

#include <dev/pci/pcidevs.h>
#include <dev/pci/pcivar.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <dev/pci/cmpcireg.h>
#include <dev/pci/cmpcivar.h>

#include <machine/bus.h>
#include <machine/intr.h>

/*
 * Low-level HW interface
 */
uint8_t cmpci_mixerreg_read(struct cmpci_softc *, uint8_t);
void cmpci_mixerreg_write(struct cmpci_softc *, uint8_t, uint8_t);
void cmpci_reg_partial_write_1(struct cmpci_softc *, int, int,
						    unsigned, unsigned);
void cmpci_reg_partial_write_4(struct cmpci_softc *, int, int,
						    uint32_t, uint32_t);
void cmpci_reg_set_1(struct cmpci_softc *, int, uint8_t);
void cmpci_reg_clear_1(struct cmpci_softc *, int, uint8_t);
void cmpci_reg_set_4(struct cmpci_softc *, int, uint32_t);
void cmpci_reg_clear_4(struct cmpci_softc *, int, uint32_t);
void cmpci_reg_set_reg_misc(struct cmpci_softc *, uint32_t);
void cmpci_reg_clear_reg_misc(struct cmpci_softc *, uint32_t);
int cmpci_rate_to_index(int);
int cmpci_index_to_rate(int);
int cmpci_index_to_divider(int);

int cmpci_adjust(int, int);
void cmpci_set_mixer_gain(struct cmpci_softc *, int);
void cmpci_set_out_ports(struct cmpci_softc *);
int cmpci_set_in_ports(struct cmpci_softc *);

void cmpci_resume(struct cmpci_softc *);

/*
 * autoconf interface
 */
int cmpci_match(struct device *, void *, void *);
void cmpci_attach(struct device *, struct device *, void *);
int cmpci_activate(struct device *, int);

struct cfdriver cmpci_cd = {
	NULL, "cmpci", DV_DULL
};

const struct cfattach cmpci_ca = {
	sizeof (struct cmpci_softc), cmpci_match, cmpci_attach, NULL,
	cmpci_activate
};

/* interrupt */
int cmpci_intr(void *);

/*
 * DMA stuff
 */
int cmpci_alloc_dmamem(struct cmpci_softc *,
				   size_t, int,
				   int, caddr_t *);
int cmpci_free_dmamem(struct cmpci_softc *, caddr_t,
				  int);
struct cmpci_dmanode * cmpci_find_dmamem(struct cmpci_softc *,
						     caddr_t);

/*
 * Interface to machine independent layer
 */
int cmpci_open(void *, int);
void cmpci_close(void *);
int cmpci_set_params(void *, int, int,
				 struct audio_params *,
				 struct audio_params *);
int cmpci_round_blocksize(void *, int);
int cmpci_halt_output(void *);
int cmpci_halt_input(void *);
int cmpci_set_port(void *, mixer_ctrl_t *);
int cmpci_get_port(void *, mixer_ctrl_t *);
int cmpci_query_devinfo(void *, mixer_devinfo_t *);
void *cmpci_malloc(void *, int, size_t, int, int);
void cmpci_free(void *, void *, int);
size_t cmpci_round_buffersize(void *, int, size_t);
int cmpci_trigger_output(void *, void *, void *, int,
				     void (*)(void *), void *,
				     struct audio_params *);
int cmpci_trigger_input(void *, void *, void *, int,
				    void (*)(void *), void *,
				    struct audio_params *);

const struct audio_hw_if cmpci_hw_if = {
	.open = cmpci_open,
	.close = cmpci_close,
	.set_params = cmpci_set_params,
	.round_blocksize = cmpci_round_blocksize,
	.halt_output = cmpci_halt_output,
	.halt_input = cmpci_halt_input,
	.set_port = cmpci_set_port,
	.get_port = cmpci_get_port,
	.query_devinfo = cmpci_query_devinfo,
	.allocm = cmpci_malloc,
	.freem = cmpci_free,
	.round_buffersize = cmpci_round_buffersize,
	.trigger_output = cmpci_trigger_output,
	.trigger_input = cmpci_trigger_input,
};

/*
 * Low-level HW interface
 */

/* mixer register read/write */
uint8_t
cmpci_mixerreg_read(struct cmpci_softc *sc, uint8_t no)
{
	uint8_t ret;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh, CMPCI_REG_SBADDR, no);
	delay(10);
	ret = bus_space_read_1(sc->sc_iot, sc->sc_ioh, CMPCI_REG_SBDATA);
	delay(10);
	return ret;
}

void
cmpci_mixerreg_write(struct cmpci_softc *sc, uint8_t no, uint8_t val)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, CMPCI_REG_SBADDR, no);
	delay(10);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, CMPCI_REG_SBDATA, val);
	delay(10);
}

/* register partial write */
void
cmpci_reg_partial_write_1(struct cmpci_softc *sc, int no, int shift,
    unsigned mask, unsigned val)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, no,
	    (val<<shift) |
	    (bus_space_read_1(sc->sc_iot, sc->sc_ioh, no) & ~(mask<<shift)));
	delay(10);
}

void
cmpci_reg_partial_write_4(struct cmpci_softc *sc, int no, int shift,
    uint32_t mask, uint32_t val)
{
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, no,
	    (val<<shift) |
	    (bus_space_read_4(sc->sc_iot, sc->sc_ioh, no) & ~(mask<<shift)));
	delay(10);
}

/* register set/clear bit */
void
cmpci_reg_set_1(struct cmpci_softc *sc, int no, uint8_t mask)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, no,
	    (bus_space_read_1(sc->sc_iot, sc->sc_ioh, no) | mask));
	delay(10);
}

void
cmpci_reg_clear_1(struct cmpci_softc *sc, int no, uint8_t mask)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh, no,
	    (bus_space_read_1(sc->sc_iot, sc->sc_ioh, no) & ~mask));
	delay(10);
}

void
cmpci_reg_set_4(struct cmpci_softc *sc, int no, uint32_t mask)
{
	/* use cmpci_reg_set_reg_misc() for CMPCI_REG_MISC */
	KDASSERT(no != CMPCI_REG_MISC);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, no,
	    (bus_space_read_4(sc->sc_iot, sc->sc_ioh, no) | mask));
	delay(10);
}

void
cmpci_reg_clear_4(struct cmpci_softc *sc, int no, uint32_t mask)
{
	/* use cmpci_reg_clear_reg_misc() for CMPCI_REG_MISC */
	KDASSERT(no != CMPCI_REG_MISC);

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, no,
	    (bus_space_read_4(sc->sc_iot, sc->sc_ioh, no) & ~mask));
	delay(10);
}

/*
 * The CMPCI_REG_MISC register needs special handling, since one of
 * its bits has different read/write values.
 */
void
cmpci_reg_set_reg_misc(struct cmpci_softc *sc, uint32_t mask)
{
	sc->sc_reg_misc |= mask;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, CMPCI_REG_MISC,
	    sc->sc_reg_misc);
	delay(10);
}

void
cmpci_reg_clear_reg_misc(struct cmpci_softc *sc, uint32_t mask)
{
	sc->sc_reg_misc &= ~mask;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, CMPCI_REG_MISC,
	    sc->sc_reg_misc);
	delay(10);
}

/* rate */
static const struct {
	int rate;
	int divider;
} cmpci_rate_table[CMPCI_REG_NUMRATE] = {
#define _RATE(n) { n, CMPCI_REG_RATE_ ## n }
	_RATE(5512),
	_RATE(8000),
	_RATE(11025),
	_RATE(16000),
	_RATE(22050),
	_RATE(32000),
	_RATE(44100),
	_RATE(48000)
#undef	_RATE
};

int
cmpci_rate_to_index(int rate)
{
	int i;

	for (i = 0; i < CMPCI_REG_NUMRATE - 1; i++)
		if (rate <=
		    (cmpci_rate_table[i].rate + cmpci_rate_table[i+1].rate) / 2)
			return i;
	return i;  /* 48000 */
}

int
cmpci_index_to_rate(int index)
{
	return cmpci_rate_table[index].rate;
}

int
cmpci_index_to_divider(int index)
{
	return cmpci_rate_table[index].divider;
}

const struct pci_matchid cmpci_devices[] = {
	{ PCI_VENDOR_CMI, PCI_PRODUCT_CMI_CMI8338A },
	{ PCI_VENDOR_CMI, PCI_PRODUCT_CMI_CMI8338B },
	{ PCI_VENDOR_CMI, PCI_PRODUCT_CMI_CMI8738 },
	{ PCI_VENDOR_CMI, PCI_PRODUCT_CMI_CMI8738B }
};

/*
 * interface to configure the device.
 */

int
cmpci_match(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, cmpci_devices,
	    nitems(cmpci_devices)));
}

void
cmpci_attach(struct device *parent, struct device *self, void *aux)
{
	struct cmpci_softc *sc = (struct cmpci_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	struct audio_attach_args aa;
	pci_intr_handle_t ih;
	char const *intrstr;
	int i, v, d;

	sc->sc_id = pa->pa_id;
	sc->sc_class = pa->pa_class;
	switch (PCI_PRODUCT(sc->sc_id)) {
	case PCI_PRODUCT_CMI_CMI8338A:
		/*FALLTHROUGH*/
	case PCI_PRODUCT_CMI_CMI8338B:
		sc->sc_capable = CMPCI_CAP_CMI8338;
		break;
	case PCI_PRODUCT_CMI_CMI8738:
		/*FALLTHROUGH*/
	case PCI_PRODUCT_CMI_CMI8738B:
		sc->sc_capable = CMPCI_CAP_CMI8738;
		break;
	}

	/* map I/O space */
	if (pci_mapreg_map(pa, CMPCI_PCI_IOBASEREG, PCI_MAPREG_TYPE_IO, 0,
			   &sc->sc_iot, &sc->sc_ioh, NULL, NULL, 0)) {
		printf(": can't map i/o space\n");
		return;
	}

	/* interrupt */
	if (pci_intr_map(pa, &ih)) {
		printf(": can't map interrupt\n");
		return;
	}
	intrstr = pci_intr_string(pa->pa_pc, ih);
	sc->sc_ih = pci_intr_establish(pa->pa_pc, ih, IPL_AUDIO | IPL_MPSAFE,
	    cmpci_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		return;
	}
	printf(": %s\n", intrstr);

	sc->sc_dmat = pa->pa_dmat;

	audio_attach_mi(&cmpci_hw_if, sc, NULL, &sc->sc_dev);

	/* attach OPL device */
	aa.type = AUDIODEV_TYPE_OPL;
	aa.hwif = NULL;
	aa.hdl = NULL;
	(void)config_found(&sc->sc_dev, &aa, audioprint);

	/* attach MPU-401 device */
	aa.type = AUDIODEV_TYPE_MPU;
	aa.hwif = NULL;
	aa.hdl = NULL;
	if (bus_space_subregion(sc->sc_iot, sc->sc_ioh,
	    CMPCI_REG_MPU_BASE, CMPCI_REG_MPU_SIZE, &sc->sc_mpu_ioh) == 0)
		sc->sc_mpudev = config_found(&sc->sc_dev, &aa, audioprint);

	/* get initial value (this is 0 and may be omitted but just in case) */
	sc->sc_reg_misc = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    CMPCI_REG_MISC) & ~CMPCI_REG_SPDIF48K;

	/* extra capabilities check */
	d = bus_space_read_4(sc->sc_iot, sc->sc_ioh, CMPCI_REG_INTR_CTRL) &
	    CMPCI_REG_CHIP_MASK2;
	if (d) {
		if (d & CMPCI_REG_CHIP_8768) {
			sc->sc_version = 68;
			sc->sc_capable |= CMPCI_CAP_4CH | CMPCI_CAP_6CH |
			    CMPCI_CAP_8CH;
		} else if (d & CMPCI_REG_CHIP_055) {
			sc->sc_version = 55;
			sc->sc_capable |= CMPCI_CAP_4CH | CMPCI_CAP_6CH;
		} else if (d & CMPCI_REG_CHIP_039) {
			sc->sc_version = 39;
			sc->sc_capable |= CMPCI_CAP_4CH |
			    ((d & CMPCI_REG_CHIP_039_6CH) ? CMPCI_CAP_6CH : 0);
		} else {
			/* unknown version */
			sc->sc_version = 0;
		}
	} else {
		d = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    CMPCI_REG_CHANNEL_FORMAT) & CMPCI_REG_CHIP_MASK1;
		if (d)
			sc->sc_version = 37;
		else
			sc->sc_version = 33;
	}

	cmpci_mixerreg_write(sc, CMPCI_SB16_MIXER_RESET, 0);
	cmpci_mixerreg_write(sc, CMPCI_SB16_MIXER_ADCMIX_L, 0);
	cmpci_mixerreg_write(sc, CMPCI_SB16_MIXER_ADCMIX_R, 0);
	cmpci_mixerreg_write(sc, CMPCI_SB16_MIXER_OUTMIX,
	    CMPCI_SB16_SW_CD|CMPCI_SB16_SW_MIC|CMPCI_SB16_SW_LINE);
	for (i = 0; i < CMPCI_NDEVS; i++) {
		switch(i) {
		/*
		 * CMI8738 defaults are
		 *  master:	0xe0	(0x00 - 0xf8)
		 *  FM, DAC:	0xc0	(0x00 - 0xf8)
		 *  PC speaker:	0x80	(0x00 - 0xc0)
		 *  others:	0
		 */
		/* volume */
		case CMPCI_MASTER_VOL:
			v = 128;	/* 224 */
			break;
		case CMPCI_FM_VOL:
		case CMPCI_DAC_VOL:
			v = 192;
			break;
		case CMPCI_PCSPEAKER:
			v = 128;
			break;

		/* booleans, set to true */
		case CMPCI_CD_MUTE:
		case CMPCI_MIC_MUTE:
		case CMPCI_LINE_IN_MUTE:
		case CMPCI_AUX_IN_MUTE:
			v = 1;
			break;

		/* volume with initial value 0 */
		case CMPCI_CD_VOL:
		case CMPCI_LINE_IN_VOL:
		case CMPCI_AUX_IN_VOL:
		case CMPCI_MIC_VOL:
		case CMPCI_MIC_RECVOL:
			/* FALLTHROUGH */

		/* others are cleared */
		case CMPCI_MIC_PREAMP:
		case CMPCI_RECORD_SOURCE:
		case CMPCI_PLAYBACK_MODE:
		case CMPCI_SPDIF_IN_SELECT:
		case CMPCI_SPDIF_IN_PHASE:
		case CMPCI_SPDIF_LOOP:
		case CMPCI_SPDIF_OUT_PLAYBACK:
		case CMPCI_SPDIF_OUT_VOLTAGE:
		case CMPCI_MONITOR_DAC:
		case CMPCI_REAR:
		case CMPCI_INDIVIDUAL:
		case CMPCI_REVERSE:
		case CMPCI_SURROUND:
		default:
			v = 0;
			break;
		}
		sc->sc_gain[i][CMPCI_LEFT] = sc->sc_gain[i][CMPCI_RIGHT] = v;
		cmpci_set_mixer_gain(sc, i);
	}

	sc->sc_play_channel = 0;
}

int
cmpci_activate(struct device *self, int act)
{
	struct cmpci_softc *sc = (struct cmpci_softc *)self;

	switch (act) {
	case DVACT_RESUME:
		cmpci_resume(sc);
		break;
	default:
		break;
	}
	return (config_activate_children(self, act));
}

void
cmpci_resume(struct cmpci_softc *sc)
{
	cmpci_mixerreg_write(sc, CMPCI_SB16_MIXER_RESET, 0);
}

int
cmpci_intr(void *handle)
{
	struct cmpci_softc *sc = handle;
	struct cmpci_channel *chan;
	uint32_t intrstat;
	uint16_t hwpos;

	mtx_enter(&audio_lock);
	intrstat = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
	    CMPCI_REG_INTR_STATUS);

	if (!(intrstat & CMPCI_REG_ANY_INTR)) {
		mtx_leave(&audio_lock);
		return 0;
	}

	delay(10);

	/* disable and reset intr */
	if (intrstat & CMPCI_REG_CH0_INTR)
		cmpci_reg_clear_4(sc, CMPCI_REG_INTR_CTRL,
		   CMPCI_REG_CH0_INTR_ENABLE);
	if (intrstat & CMPCI_REG_CH1_INTR)
		cmpci_reg_clear_4(sc, CMPCI_REG_INTR_CTRL,
		    CMPCI_REG_CH1_INTR_ENABLE);

	if (intrstat & CMPCI_REG_CH0_INTR) {
		chan = &sc->sc_ch0;
		if (chan->intr != NULL) {
			hwpos = bus_space_read_2(sc->sc_iot, sc->sc_ioh,
			    CMPCI_REG_DMA0_BYTES);
			hwpos = hwpos * chan->bps / chan->blksize;
			hwpos = chan->nblocks - hwpos - 1;
			while (chan->swpos != hwpos) {
				(*chan->intr)(chan->intr_arg);
				chan->swpos++;
				if (chan->swpos >= chan->nblocks)
					chan->swpos = 0;
				if (chan->swpos != hwpos) {
					DPRINTF(("%s: DMA0 hwpos=%d swpos=%d\n",
					    __func__, hwpos, chan->swpos));
				}
			}
		}
	}
	if (intrstat & CMPCI_REG_CH1_INTR) {
		chan = &sc->sc_ch1;
		if (chan->intr != NULL) {
			hwpos = bus_space_read_2(sc->sc_iot, sc->sc_ioh,
			    CMPCI_REG_DMA1_BYTES);
			hwpos = hwpos * chan->bps / chan->blksize;
			hwpos = chan->nblocks - hwpos - 1;
			while (chan->swpos != hwpos) {
				(*chan->intr)(chan->intr_arg);
				chan->swpos++;
				if (chan->swpos >= chan->nblocks)
					chan->swpos = 0;
				if (chan->swpos != hwpos) {
					DPRINTF(("%s: DMA1 hwpos=%d swpos=%d\n",
					    __func__, hwpos, chan->swpos));
				}
			}
		}
	}

	/* enable intr */
	if (intrstat & CMPCI_REG_CH0_INTR)
		cmpci_reg_set_4(sc, CMPCI_REG_INTR_CTRL,
		    CMPCI_REG_CH0_INTR_ENABLE);
	if (intrstat & CMPCI_REG_CH1_INTR)
		cmpci_reg_set_4(sc, CMPCI_REG_INTR_CTRL,
		    CMPCI_REG_CH1_INTR_ENABLE);

#if 0
	if (intrstat & CMPCI_REG_UART_INTR && sc->sc_mpudev != NULL)
		mpu_intr(sc->sc_mpudev);
#endif

	mtx_leave(&audio_lock);
	return 1;
}

/* open/close */
int
cmpci_open(void *handle, int flags)
{
	return 0;
}

void
cmpci_close(void *handle)
{
}

int
cmpci_set_params(void *handle, int setmode, int usemode,
    struct audio_params *play, struct audio_params *rec)
{
	int i;
	struct cmpci_softc *sc = handle;

	for (i = 0; i < 2; i++) {
		int md_format;
		int md_divide;
		int md_index;
		int mode;
		struct audio_params *p;

		switch (i) {
		case 0:
			mode = AUMODE_PLAY;
			p = play;
			break;
		case 1:
			mode = AUMODE_RECORD;
			p = rec;
			break;
		default:
			return EINVAL;
		}

		if (!(setmode & mode))
			continue;

		if (setmode & AUMODE_RECORD) {
			if (p->channels > 2)
				p->channels = 2;
			sc->sc_play_channel = 0;
			cmpci_reg_clear_reg_misc(sc, CMPCI_REG_ENDBDAC);
			cmpci_reg_clear_reg_misc(sc, CMPCI_REG_XCHGDAC);
		} else {
			sc->sc_play_channel = 1;
			cmpci_reg_set_reg_misc(sc, CMPCI_REG_ENDBDAC);
			cmpci_reg_set_reg_misc(sc, CMPCI_REG_XCHGDAC);
		}

		cmpci_reg_clear_4(sc, CMPCI_REG_LEGACY_CTRL,
		    CMPCI_REG_NXCHG);
		if (sc->sc_capable & CMPCI_CAP_4CH)
			cmpci_reg_clear_4(sc, CMPCI_REG_CHANNEL_FORMAT,
			    CMPCI_REG_CHB3D);
		if (sc->sc_capable & CMPCI_CAP_6CH) {
			cmpci_reg_clear_4(sc, CMPCI_REG_CHANNEL_FORMAT,
			    CMPCI_REG_CHB3D5C);
			cmpci_reg_clear_4(sc, CMPCI_REG_LEGACY_CTRL,
		    	    CMPCI_REG_CHB3D6C);
			cmpci_reg_clear_reg_misc(sc, CMPCI_REG_ENCENTER);
		}
		if (sc->sc_capable & CMPCI_CAP_8CH)
			cmpci_reg_clear_4(sc, CMPCI_REG_8768_MISC,
			    CMPCI_REG_CHB3D8C);

		/* format */
		switch (p->channels) {
		case 1:
			md_format = CMPCI_REG_FORMAT_MONO;
			break;
		case 2:
			md_format = CMPCI_REG_FORMAT_STEREO;
			break;
		case 4:
			if (mode & AUMODE_PLAY) {
				if (sc->sc_capable & CMPCI_CAP_4CH) {
					cmpci_reg_clear_reg_misc(sc,
					    CMPCI_REG_N4SPK3D);
					cmpci_reg_set_4(sc,
					    CMPCI_REG_CHANNEL_FORMAT,
					    CMPCI_REG_CHB3D);
					cmpci_reg_set_4(sc,
					    CMPCI_REG_LEGACY_CTRL,
					    CMPCI_REG_NXCHG);
				} else
					p->channels = 2;
			}
			md_format = CMPCI_REG_FORMAT_STEREO;
			break;
		case 6:
			if (mode & AUMODE_PLAY) {
				if (sc->sc_capable & CMPCI_CAP_6CH) {
					cmpci_reg_clear_reg_misc(sc,
					    CMPCI_REG_N4SPK3D);
					cmpci_reg_set_4(sc,
					    CMPCI_REG_CHANNEL_FORMAT,
					    CMPCI_REG_CHB3D5C);
					cmpci_reg_set_4(sc,
					    CMPCI_REG_LEGACY_CTRL,
					    CMPCI_REG_CHB3D6C);
					cmpci_reg_set_reg_misc(sc,
					    CMPCI_REG_ENCENTER);
					cmpci_reg_set_4(sc,
					    CMPCI_REG_LEGACY_CTRL,
					    CMPCI_REG_NXCHG);
				} else
					p->channels = 2;
			}
			md_format = CMPCI_REG_FORMAT_STEREO;
			break;
		case 8:
			if (mode & AUMODE_PLAY) {
				if (sc->sc_capable & CMPCI_CAP_8CH) {
					cmpci_reg_clear_reg_misc(sc,
					    CMPCI_REG_N4SPK3D);
					cmpci_reg_set_4(sc,
					    CMPCI_REG_CHANNEL_FORMAT,
					    CMPCI_REG_CHB3D5C);
					cmpci_reg_set_4(sc,
					    CMPCI_REG_LEGACY_CTRL,
					    CMPCI_REG_CHB3D6C);
					cmpci_reg_set_reg_misc(sc,
					    CMPCI_REG_ENCENTER);
					cmpci_reg_set_4(sc,
					    CMPCI_REG_8768_MISC,
					    CMPCI_REG_CHB3D8C);
					cmpci_reg_set_4(sc,
					    CMPCI_REG_LEGACY_CTRL,
					    CMPCI_REG_NXCHG);
				} else
					p->channels = 2;
			}
			md_format = CMPCI_REG_FORMAT_STEREO;
			break;
		default:
			return (EINVAL);
		}
		if (p->precision >= 16) {
			p->precision = 16;
			p->encoding = AUDIO_ENCODING_SLINEAR_LE;
			md_format |= CMPCI_REG_FORMAT_16BIT;
		} else {
			p->precision = 8;
			p->encoding = AUDIO_ENCODING_ULINEAR_LE;
			md_format |= CMPCI_REG_FORMAT_8BIT;
		}
		p->bps = AUDIO_BPS(p->precision);
		p->msb = 1;
		if (mode & AUMODE_PLAY) {
			if (sc->sc_play_channel == 1) {
				cmpci_reg_partial_write_4(sc,
				   CMPCI_REG_CHANNEL_FORMAT,
				   CMPCI_REG_CH1_FORMAT_SHIFT,
				   CMPCI_REG_CH1_FORMAT_MASK, md_format);
			} else {
				cmpci_reg_partial_write_4(sc,
				   CMPCI_REG_CHANNEL_FORMAT,
				   CMPCI_REG_CH0_FORMAT_SHIFT,
				   CMPCI_REG_CH0_FORMAT_MASK, md_format);
			}
		} else {
			cmpci_reg_partial_write_4(sc,
			   CMPCI_REG_CHANNEL_FORMAT,
			   CMPCI_REG_CH1_FORMAT_SHIFT,
			   CMPCI_REG_CH1_FORMAT_MASK, md_format);
		}
		/* sample rate */
		md_index = cmpci_rate_to_index(p->sample_rate);
		md_divide = cmpci_index_to_divider(md_index);
		p->sample_rate = cmpci_index_to_rate(md_index);
		DPRINTF(("%s: sample:%d, divider=%d\n",
			 sc->sc_dev.dv_xname, (int)p->sample_rate, md_divide));
		if (mode & AUMODE_PLAY) {
			if (sc->sc_play_channel == 1) {
				cmpci_reg_partial_write_4(sc,
				    CMPCI_REG_FUNC_1, CMPCI_REG_ADC_FS_SHIFT,
				    CMPCI_REG_ADC_FS_MASK, md_divide);
				sc->sc_ch1.md_divide = md_divide;
			} else {
				cmpci_reg_partial_write_4(sc,
				    CMPCI_REG_FUNC_1, CMPCI_REG_DAC_FS_SHIFT,
				    CMPCI_REG_DAC_FS_MASK, md_divide);
				sc->sc_ch0.md_divide = md_divide;
			}
		} else {
			cmpci_reg_partial_write_4(sc,
			    CMPCI_REG_FUNC_1, CMPCI_REG_ADC_FS_SHIFT,
			    CMPCI_REG_ADC_FS_MASK, md_divide);
			sc->sc_ch1.md_divide = md_divide;
		}
	}

	return 0;
}

int
cmpci_round_blocksize(void *handle, int block)
{
	return ((block + 3) & -4);
}

int
cmpci_halt_output(void *handle)
{
	struct cmpci_softc *sc = handle;
	uint32_t reg_intr, reg_enable, reg_reset;

	mtx_enter(&audio_lock);
	if (sc->sc_play_channel == 1) {
		sc->sc_ch1.intr = NULL;
		reg_intr = CMPCI_REG_CH1_INTR_ENABLE;
		reg_enable = CMPCI_REG_CH1_ENABLE;
		reg_reset = CMPCI_REG_CH1_RESET;
	} else {
		sc->sc_ch0.intr = NULL;
		reg_intr = CMPCI_REG_CH0_INTR_ENABLE;
		reg_enable = CMPCI_REG_CH0_ENABLE;
		reg_reset = CMPCI_REG_CH0_RESET;
	}
	cmpci_reg_clear_4(sc, CMPCI_REG_INTR_CTRL, reg_intr);
	cmpci_reg_clear_4(sc, CMPCI_REG_FUNC_0, reg_enable);
	/* wait for reset DMA */
	cmpci_reg_set_4(sc, CMPCI_REG_FUNC_0, reg_reset);
	delay(10);
	cmpci_reg_clear_4(sc, CMPCI_REG_FUNC_0, reg_reset);
	mtx_leave(&audio_lock);
	return 0;
}

int
cmpci_halt_input(void *handle)
{
	struct cmpci_softc *sc = handle;

	mtx_enter(&audio_lock);
	sc->sc_ch1.intr = NULL;
	cmpci_reg_clear_4(sc, CMPCI_REG_INTR_CTRL, CMPCI_REG_CH1_INTR_ENABLE);
	cmpci_reg_clear_4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH1_ENABLE);
	/* wait for reset DMA */
	cmpci_reg_set_4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH1_RESET);
	delay(10);
	cmpci_reg_clear_4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH1_RESET);
	mtx_leave(&audio_lock);
	return 0;
}

/* mixer device information */
int
cmpci_query_devinfo(void *handle, mixer_devinfo_t *dip)
{
	static const char *const mixer_port_names[] = {
		AudioNdac, AudioNfmsynth, AudioNcd, AudioNline, AudioNaux,
		AudioNmicrophone
	};
	static const char *const mixer_classes[] = {
		AudioCinputs, AudioCoutputs, AudioCrecord, CmpciCplayback,
		CmpciCspdif
	};
	struct cmpci_softc *sc = handle;
	int i;

	dip->prev = dip->next = AUDIO_MIXER_LAST;

	switch (dip->index) {
	case CMPCI_INPUT_CLASS:
	case CMPCI_OUTPUT_CLASS:
	case CMPCI_RECORD_CLASS:
	case CMPCI_PLAYBACK_CLASS:
	case CMPCI_SPDIF_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = dip->index;
		strlcpy(dip->label.name,
		    mixer_classes[dip->index - CMPCI_INPUT_CLASS],
		    sizeof dip->label.name);
		return 0;

	case CMPCI_AUX_IN_VOL:
		dip->un.v.delta = 1 << (8 - CMPCI_REG_AUX_VALBITS);
		goto vol1;
	case CMPCI_DAC_VOL:
	case CMPCI_FM_VOL:
	case CMPCI_CD_VOL:
	case CMPCI_LINE_IN_VOL:
	case CMPCI_MIC_VOL:
		dip->un.v.delta = 1 << (8 - CMPCI_SB16_MIXER_VALBITS);
	vol1:	dip->mixer_class = CMPCI_INPUT_CLASS;
		dip->next = dip->index + 6;	/* CMPCI_xxx_MUTE */
		strlcpy(dip->label.name, mixer_port_names[dip->index],
		    sizeof dip->label.name);
		dip->un.v.num_channels = (dip->index == CMPCI_MIC_VOL ? 1 : 2);
	vol:
		dip->type = AUDIO_MIXER_VALUE;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		return 0;

	case CMPCI_MIC_MUTE:
		dip->next = CMPCI_MIC_PREAMP;
		/* FALLTHROUGH */
	case CMPCI_DAC_MUTE:
	case CMPCI_FM_MUTE:
	case CMPCI_CD_MUTE:
	case CMPCI_LINE_IN_MUTE:
	case CMPCI_AUX_IN_MUTE:
		dip->prev = dip->index - 6;	/* CMPCI_xxx_VOL */
		dip->mixer_class = CMPCI_INPUT_CLASS;
		strlcpy(dip->label.name, AudioNmute, sizeof dip->label.name);
		goto on_off;
	on_off:
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNon,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		return 0;

	case CMPCI_MIC_PREAMP:
		dip->mixer_class = CMPCI_INPUT_CLASS;
		dip->prev = CMPCI_MIC_MUTE;
		strlcpy(dip->label.name, AudioNpreamp, sizeof dip->label.name);
		goto on_off;
	case CMPCI_PCSPEAKER:
		dip->mixer_class = CMPCI_INPUT_CLASS;
		strlcpy(dip->label.name, AudioNspeaker, sizeof dip->label.name);
		dip->un.v.num_channels = 1;
		dip->un.v.delta = 1 << (8 - CMPCI_SB16_MIXER_SPEAKER_VALBITS);
		goto vol;
	case CMPCI_RECORD_SOURCE:
		dip->mixer_class = CMPCI_RECORD_CLASS;
		strlcpy(dip->label.name, AudioNsource, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_SET;
		dip->un.s.num_mem = 7;
		strlcpy(dip->un.s.member[0].label.name, AudioNmicrophone,
		    sizeof dip->un.s.member[0].label.name);
		dip->un.s.member[0].mask = CMPCI_RECORD_SOURCE_MIC;
		strlcpy(dip->un.s.member[1].label.name, AudioNcd,
		    sizeof dip->un.s.member[1].label.name);
		dip->un.s.member[1].mask = CMPCI_RECORD_SOURCE_CD;
		strlcpy(dip->un.s.member[2].label.name, AudioNline,
		    sizeof dip->un.s.member[2].label.name);
		dip->un.s.member[2].mask = CMPCI_RECORD_SOURCE_LINE_IN;
		strlcpy(dip->un.s.member[3].label.name, AudioNaux,
		    sizeof dip->un.s.member[3].label.name);
		dip->un.s.member[3].mask = CMPCI_RECORD_SOURCE_AUX_IN;
		strlcpy(dip->un.s.member[4].label.name, AudioNwave,
		    sizeof dip->un.s.member[4].label.name);
		dip->un.s.member[4].mask = CMPCI_RECORD_SOURCE_WAVE;
		strlcpy(dip->un.s.member[5].label.name, AudioNfmsynth,
		    sizeof dip->un.s.member[5].label.name);
		dip->un.s.member[5].mask = CMPCI_RECORD_SOURCE_FM;
		strlcpy(dip->un.s.member[6].label.name, CmpciNspdif,
		    sizeof dip->un.s.member[6].label.name);
		dip->un.s.member[6].mask = CMPCI_RECORD_SOURCE_SPDIF;
		return 0;
	case CMPCI_MIC_RECVOL:
		dip->mixer_class = CMPCI_RECORD_CLASS;
		strlcpy(dip->label.name, AudioNmicrophone, sizeof dip->label.name);
		dip->un.v.num_channels = 1;
		dip->un.v.delta = 1 << (8 - CMPCI_REG_ADMIC_VALBITS);
		goto vol;

	case CMPCI_PLAYBACK_MODE:
		dip->mixer_class = CMPCI_PLAYBACK_CLASS;
		dip->type = AUDIO_MIXER_ENUM;
		strlcpy(dip->label.name, AudioNmode, sizeof dip->label.name);
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNdac,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = CMPCI_PLAYBACK_MODE_WAVE;
		strlcpy(dip->un.e.member[1].label.name, CmpciNspdif,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = CMPCI_PLAYBACK_MODE_SPDIF;
		return 0;
	case CMPCI_SPDIF_IN_SELECT:
		dip->mixer_class = CMPCI_SPDIF_CLASS;
		dip->type = AUDIO_MIXER_ENUM;
		dip->next = CMPCI_SPDIF_IN_PHASE;
		strlcpy(dip->label.name, AudioNinput, sizeof dip->label.name);
		i = 0;
		strlcpy(dip->un.e.member[i].label.name, CmpciNspdin1,
		    sizeof dip->un.e.member[i].label.name);
		dip->un.e.member[i++].ord = CMPCI_SPDIF_IN_SPDIN1;
		if (CMPCI_ISCAP(sc, 2ND_SPDIN)) {
			strlcpy(dip->un.e.member[i].label.name, CmpciNspdin2,
			    sizeof dip->un.e.member[i].label.name);
			dip->un.e.member[i++].ord = CMPCI_SPDIF_IN_SPDIN2;
		}
		strlcpy(dip->un.e.member[i].label.name, CmpciNspdout,
		    sizeof dip->un.e.member[i].label.name);
		dip->un.e.member[i++].ord = CMPCI_SPDIF_IN_SPDOUT;
		dip->un.e.num_mem = i;
		return 0;
	case CMPCI_SPDIF_IN_PHASE:
		dip->mixer_class = CMPCI_SPDIF_CLASS;
		dip->prev = CMPCI_SPDIF_IN_SELECT;
		strlcpy(dip->label.name, CmpciNphase, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, CmpciNpositive,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = CMPCI_SPDIF_IN_PHASE_POSITIVE;
		strlcpy(dip->un.e.member[1].label.name, CmpciNnegative,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = CMPCI_SPDIF_IN_PHASE_NEGATIVE;
		return 0;
	case CMPCI_SPDIF_LOOP:
		dip->mixer_class = CMPCI_SPDIF_CLASS;
		dip->next = CMPCI_SPDIF_OUT_PLAYBACK;
		strlcpy(dip->label.name, AudioNoutput, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, CmpciNplayback,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = CMPCI_SPDIF_LOOP_OFF;
		strlcpy(dip->un.e.member[1].label.name, CmpciNspdin,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = CMPCI_SPDIF_LOOP_ON;
		return 0;
	case CMPCI_SPDIF_OUT_PLAYBACK:
		dip->mixer_class = CMPCI_SPDIF_CLASS;
		dip->prev = CMPCI_SPDIF_LOOP;
		dip->next = CMPCI_SPDIF_OUT_VOLTAGE;
		strlcpy(dip->label.name, CmpciNplayback, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNwave,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = CMPCI_SPDIF_OUT_PLAYBACK_WAVE;
		strlcpy(dip->un.e.member[1].label.name, CmpciNlegacy,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = CMPCI_SPDIF_OUT_PLAYBACK_LEGACY;
		return 0;
	case CMPCI_SPDIF_OUT_VOLTAGE:
		dip->mixer_class = CMPCI_SPDIF_CLASS;
		dip->prev = CMPCI_SPDIF_OUT_PLAYBACK;
		strlcpy(dip->label.name, CmpciNvoltage, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, CmpciNhigh_v,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = CMPCI_SPDIF_OUT_VOLTAGE_HIGH;
		strlcpy(dip->un.e.member[1].label.name, CmpciNlow_v,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = CMPCI_SPDIF_OUT_VOLTAGE_LOW;
		return 0;
	case CMPCI_MONITOR_DAC:
		dip->mixer_class = CMPCI_SPDIF_CLASS;
		strlcpy(dip->label.name, AudioNmonitor, sizeof dip->label.name);
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 3;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = CMPCI_MONITOR_DAC_OFF;
		strlcpy(dip->un.e.member[1].label.name, CmpciNspdin,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = CMPCI_MONITOR_DAC_SPDIN;
		strlcpy(dip->un.e.member[2].label.name, CmpciNspdout,
		    sizeof dip->un.e.member[2].label.name);
		dip->un.e.member[2].ord = CMPCI_MONITOR_DAC_SPDOUT;
		return 0;

	case CMPCI_MASTER_VOL:
		dip->mixer_class = CMPCI_OUTPUT_CLASS;
		strlcpy(dip->label.name, AudioNmaster, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		dip->un.v.delta = 1 << (8 - CMPCI_SB16_MIXER_VALBITS);
		goto vol;
	case CMPCI_REAR:
		dip->mixer_class = CMPCI_OUTPUT_CLASS;
		dip->next = CMPCI_INDIVIDUAL;
		strlcpy(dip->label.name, CmpciNrear, sizeof dip->label.name);
		goto on_off;
	case CMPCI_INDIVIDUAL:
		dip->mixer_class = CMPCI_OUTPUT_CLASS;
		dip->prev = CMPCI_REAR;
		dip->next = CMPCI_REVERSE;
		strlcpy(dip->label.name, CmpciNindividual, sizeof dip->label.name);
		goto on_off;
	case CMPCI_REVERSE:
		dip->mixer_class = CMPCI_OUTPUT_CLASS;
		dip->prev = CMPCI_INDIVIDUAL;
		strlcpy(dip->label.name, CmpciNreverse, sizeof dip->label.name);
		goto on_off;
	case CMPCI_SURROUND:
		dip->mixer_class = CMPCI_OUTPUT_CLASS;
		strlcpy(dip->label.name, CmpciNsurround, sizeof dip->label.name);
		goto on_off;
	}

	return ENXIO;
}

int
cmpci_alloc_dmamem(struct cmpci_softc *sc, size_t size, int type, int flags,
    caddr_t *r_addr)
{
	int error = 0;
	struct cmpci_dmanode *n;
	int w;

	n = malloc(sizeof(struct cmpci_dmanode), type, flags);
	if (n == NULL) {
		error = ENOMEM;
		goto quit;
	}

	w = (flags & M_NOWAIT) ? BUS_DMA_NOWAIT : BUS_DMA_WAITOK;
#define CMPCI_DMABUF_ALIGN    0x4
#define CMPCI_DMABUF_BOUNDARY 0x0
	n->cd_tag = sc->sc_dmat;
	n->cd_size = size;
	error = bus_dmamem_alloc(n->cd_tag, n->cd_size,
	    CMPCI_DMABUF_ALIGN, CMPCI_DMABUF_BOUNDARY, n->cd_segs,
	    nitems(n->cd_segs), &n->cd_nsegs, w);
	if (error)
		goto mfree;
	error = bus_dmamem_map(n->cd_tag, n->cd_segs, n->cd_nsegs, n->cd_size,
	    &n->cd_addr, w | BUS_DMA_COHERENT);
	if (error)
		goto dmafree;
	error = bus_dmamap_create(n->cd_tag, n->cd_size, 1, n->cd_size, 0,
	    w, &n->cd_map);
	if (error)
		goto unmap;
	error = bus_dmamap_load(n->cd_tag, n->cd_map, n->cd_addr, n->cd_size,
	    NULL, w);
	if (error)
		goto destroy;

	n->cd_next = sc->sc_dmap;
	sc->sc_dmap = n;
	*r_addr = KVADDR(n);
	return 0;

 destroy:
	bus_dmamap_destroy(n->cd_tag, n->cd_map);
 unmap:
	bus_dmamem_unmap(n->cd_tag, n->cd_addr, n->cd_size);
 dmafree:
	bus_dmamem_free(n->cd_tag,
			n->cd_segs, nitems(n->cd_segs));
 mfree:
	free(n, type, 0);
 quit:
	return error;
}

int
cmpci_free_dmamem(struct cmpci_softc *sc, caddr_t addr, int type)
{
	struct cmpci_dmanode **nnp;

	for (nnp = &sc->sc_dmap; *nnp; nnp = &(*nnp)->cd_next) {
		if ((*nnp)->cd_addr == addr) {
			struct cmpci_dmanode *n = *nnp;
			bus_dmamap_unload(n->cd_tag, n->cd_map);
			bus_dmamap_destroy(n->cd_tag, n->cd_map);
			bus_dmamem_unmap(n->cd_tag, n->cd_addr, n->cd_size);
			bus_dmamem_free(n->cd_tag, n->cd_segs,
			    nitems(n->cd_segs));
			free(n, type, 0);
			return 0;
		}
	}
	return -1;
}

struct cmpci_dmanode *
cmpci_find_dmamem(struct cmpci_softc *sc, caddr_t addr)
{
	struct cmpci_dmanode *p;

	for (p = sc->sc_dmap; p; p = p->cd_next) {
		if (KVADDR(p) == (void *)addr)
			break;
	}
	return p;
}

#if 0
void cmpci_print_dmamem(struct cmpci_dmanode *p);

void
cmpci_print_dmamem(struct cmpci_dmanode *p)
{
	DPRINTF(("DMA at virt:%p, dmaseg:%p, mapseg:%p, size:%p\n",
		 (void *)p->cd_addr, (void *)p->cd_segs[0].ds_addr,
		 (void *)DMAADDR(p), (void *)p->cd_size));
}
#endif /* DEBUG */

void *
cmpci_malloc(void *handle, int direction, size_t size, int type,
    int flags)
{
	caddr_t addr;

	if (cmpci_alloc_dmamem(handle, size, type, flags, &addr))
		return NULL;
	return addr;
}

void
cmpci_free(void *handle, void *addr, int type)
{
	cmpci_free_dmamem(handle, addr, type);
}

#define MAXVAL 256
int
cmpci_adjust(int val, int mask)
{
	val += (MAXVAL - mask) >> 1;
	if (val >= MAXVAL)
		val = MAXVAL-1;
	return val & mask;
}

void
cmpci_set_mixer_gain(struct cmpci_softc *sc, int port)
{
	int src;
	int bits, mask;

	switch (port) {
	case CMPCI_MIC_VOL:
		cmpci_mixerreg_write(sc, CMPCI_SB16_MIXER_MIC,
		    CMPCI_ADJUST_MIC_GAIN(sc, sc->sc_gain[port][CMPCI_LR]));
		return;
	case CMPCI_MASTER_VOL:
		src = CMPCI_SB16_MIXER_MASTER_L;
		break;
	case CMPCI_LINE_IN_VOL:
		src = CMPCI_SB16_MIXER_LINE_L;
		break;
	case CMPCI_AUX_IN_VOL:
		bus_space_write_1(sc->sc_iot, sc->sc_ioh, CMPCI_REG_MIXER_AUX,
		    CMPCI_ADJUST_AUX_GAIN(sc, sc->sc_gain[port][CMPCI_LEFT],
					      sc->sc_gain[port][CMPCI_RIGHT]));
		return;
	case CMPCI_MIC_RECVOL:
		cmpci_reg_partial_write_1(sc, CMPCI_REG_MIXER25,
		    CMPCI_REG_ADMIC_SHIFT, CMPCI_REG_ADMIC_MASK,
		    CMPCI_ADJUST_ADMIC_GAIN(sc, sc->sc_gain[port][CMPCI_LR]));
		return;
	case CMPCI_DAC_VOL:
		src = CMPCI_SB16_MIXER_VOICE_L;
		break;
	case CMPCI_FM_VOL:
		src = CMPCI_SB16_MIXER_FM_L;
		break;
	case CMPCI_CD_VOL:
		src = CMPCI_SB16_MIXER_CDDA_L;
		break;
	case CMPCI_PCSPEAKER:
		cmpci_mixerreg_write(sc, CMPCI_SB16_MIXER_SPEAKER,
		    CMPCI_ADJUST_2_GAIN(sc, sc->sc_gain[port][CMPCI_LR]));
		return;
	case CMPCI_MIC_PREAMP:
		if (sc->sc_gain[port][CMPCI_LR])
			cmpci_reg_clear_1(sc, CMPCI_REG_MIXER25,
			    CMPCI_REG_MICGAINZ);
		else
			cmpci_reg_set_1(sc, CMPCI_REG_MIXER25,
			    CMPCI_REG_MICGAINZ);
		return;

	case CMPCI_DAC_MUTE:
		if (sc->sc_gain[port][CMPCI_LR])
			cmpci_reg_set_1(sc, CMPCI_REG_MIXER24,
			    CMPCI_REG_WSMUTE);
		else
			cmpci_reg_clear_1(sc, CMPCI_REG_MIXER24,
			    CMPCI_REG_WSMUTE);
		return;
	case CMPCI_FM_MUTE:
		if (sc->sc_gain[port][CMPCI_LR])
			cmpci_reg_set_1(sc, CMPCI_REG_MIXER24,
			    CMPCI_REG_FMMUTE);
		else
			cmpci_reg_clear_1(sc, CMPCI_REG_MIXER24,
			    CMPCI_REG_FMMUTE);
		return;
	case CMPCI_AUX_IN_MUTE:
		if (sc->sc_gain[port][CMPCI_LR])
			cmpci_reg_clear_1(sc, CMPCI_REG_MIXER25,
			    CMPCI_REG_VAUXRM|CMPCI_REG_VAUXLM);
		else
			cmpci_reg_set_1(sc, CMPCI_REG_MIXER25,
			    CMPCI_REG_VAUXRM|CMPCI_REG_VAUXLM);
		return;
	case CMPCI_CD_MUTE:
		mask = CMPCI_SB16_SW_CD;
		goto sbmute;
	case CMPCI_MIC_MUTE:
		mask = CMPCI_SB16_SW_MIC;
		goto sbmute;
	case CMPCI_LINE_IN_MUTE:
		mask = CMPCI_SB16_SW_LINE;
	sbmute:
		bits = cmpci_mixerreg_read(sc, CMPCI_SB16_MIXER_OUTMIX);
		if (sc->sc_gain[port][CMPCI_LR])
			bits = bits & ~mask;
		else
			bits = bits | mask;
		cmpci_mixerreg_write(sc, CMPCI_SB16_MIXER_OUTMIX, bits);
		return;

	case CMPCI_SPDIF_IN_SELECT:
	case CMPCI_MONITOR_DAC:
	case CMPCI_PLAYBACK_MODE:
	case CMPCI_SPDIF_LOOP:
	case CMPCI_SPDIF_OUT_PLAYBACK:
		cmpci_set_out_ports(sc);
		return;
	case CMPCI_SPDIF_OUT_VOLTAGE:
		if (CMPCI_ISCAP(sc, SPDOUT_VOLTAGE)) {
			if (sc->sc_gain[CMPCI_SPDIF_OUT_VOLTAGE][CMPCI_LR]
			    == CMPCI_SPDIF_OUT_VOLTAGE_HIGH)
				cmpci_reg_clear_reg_misc(sc, CMPCI_REG_5V);
			else
				cmpci_reg_set_reg_misc(sc, CMPCI_REG_5V);
		}
		return;
	case CMPCI_SURROUND:
		if (CMPCI_ISCAP(sc, SURROUND)) {
			if (sc->sc_gain[CMPCI_SURROUND][CMPCI_LR])
				cmpci_reg_set_1(sc, CMPCI_REG_MIXER24,
						CMPCI_REG_SURROUND);
			else
				cmpci_reg_clear_1(sc, CMPCI_REG_MIXER24,
						  CMPCI_REG_SURROUND);
		}
		return;
	case CMPCI_REAR:
		if (CMPCI_ISCAP(sc, REAR)) {
			if (sc->sc_gain[CMPCI_REAR][CMPCI_LR])
				cmpci_reg_set_reg_misc(sc, CMPCI_REG_N4SPK3D);
			else
				cmpci_reg_clear_reg_misc(sc, CMPCI_REG_N4SPK3D);
		}
		return;
	case CMPCI_INDIVIDUAL:
		if (CMPCI_ISCAP(sc, INDIVIDUAL_REAR)) {
			if (sc->sc_gain[CMPCI_REAR][CMPCI_LR])
				cmpci_reg_set_1(sc, CMPCI_REG_MIXER24,
						CMPCI_REG_INDIVIDUAL);
			else
				cmpci_reg_clear_1(sc, CMPCI_REG_MIXER24,
						  CMPCI_REG_INDIVIDUAL);
		}
		return;
	case CMPCI_REVERSE:
		if (CMPCI_ISCAP(sc, REVERSE_FR)) {
			if (sc->sc_gain[CMPCI_REVERSE][CMPCI_LR])
				cmpci_reg_set_1(sc, CMPCI_REG_MIXER24,
						CMPCI_REG_REVERSE_FR);
			else
				cmpci_reg_clear_1(sc, CMPCI_REG_MIXER24,
						  CMPCI_REG_REVERSE_FR);
		}
		return;
	case CMPCI_SPDIF_IN_PHASE:
		if (CMPCI_ISCAP(sc, SPDIN_PHASE)) {
			if (sc->sc_gain[CMPCI_SPDIF_IN_PHASE][CMPCI_LR]
			    == CMPCI_SPDIF_IN_PHASE_POSITIVE)
				cmpci_reg_clear_1(sc, CMPCI_REG_CHANNEL_FORMAT,
						  CMPCI_REG_SPDIN_PHASE);
			else
				cmpci_reg_set_1(sc, CMPCI_REG_CHANNEL_FORMAT,
						CMPCI_REG_SPDIN_PHASE);
		}
		return;
	default:
		return;
	}

	cmpci_mixerreg_write(sc, src,
	    CMPCI_ADJUST_GAIN(sc, sc->sc_gain[port][CMPCI_LEFT]));
	cmpci_mixerreg_write(sc, CMPCI_SB16_MIXER_L_TO_R(src),
	    CMPCI_ADJUST_GAIN(sc, sc->sc_gain[port][CMPCI_RIGHT]));
}

void
cmpci_set_out_ports(struct cmpci_softc *sc)
{
	struct cmpci_channel *chan;
	u_int8_t v;
	int enspdout = 0;

	if (!CMPCI_ISCAP(sc, SPDLOOP))
		return;

	/* SPDIF/out select */
	if (sc->sc_gain[CMPCI_SPDIF_LOOP][CMPCI_LR] == CMPCI_SPDIF_LOOP_OFF) {
		/* playback */
		cmpci_reg_clear_4(sc, CMPCI_REG_FUNC_1, CMPCI_REG_SPDIF_LOOP);
	} else {
		/* monitor SPDIF/in */
		cmpci_reg_set_4(sc, CMPCI_REG_FUNC_1, CMPCI_REG_SPDIF_LOOP);
	}

	/* SPDIF in select */
	v = sc->sc_gain[CMPCI_SPDIF_IN_SELECT][CMPCI_LR];
	if (v & CMPCI_SPDIFIN_SPDIFIN2)
		cmpci_reg_set_reg_misc(sc, CMPCI_REG_2ND_SPDIFIN);
	else
		cmpci_reg_clear_reg_misc(sc, CMPCI_REG_2ND_SPDIFIN);
	if (v & CMPCI_SPDIFIN_SPDIFOUT)
		cmpci_reg_set_reg_misc(sc, CMPCI_REG_SPDFLOOPI);
	else
		cmpci_reg_clear_reg_misc(sc, CMPCI_REG_SPDFLOOPI);

	if (sc->sc_play_channel == 1)
		chan = &sc->sc_ch1;
	else
		chan = &sc->sc_ch0;

	/* disable ac3 and 24 and 32 bit s/pdif modes */
	cmpci_reg_clear_4(sc, CMPCI_REG_CHANNEL_FORMAT, CMPCI_REG_AC3EN1);
	cmpci_reg_clear_reg_misc(sc, CMPCI_REG_AC3EN2);
	cmpci_reg_clear_reg_misc(sc, CMPCI_REG_SPD32SEL);
	cmpci_reg_clear_4(sc, CMPCI_REG_CHANNEL_FORMAT, CMPCI_REG_SPDIF_24);

	/* playback to ... */
	if (CMPCI_ISCAP(sc, SPDOUT) &&
	    sc->sc_gain[CMPCI_PLAYBACK_MODE][CMPCI_LR]
		== CMPCI_PLAYBACK_MODE_SPDIF &&
	    (chan->md_divide == CMPCI_REG_RATE_44100 ||
		(CMPCI_ISCAP(sc, SPDOUT_48K) &&
		    chan->md_divide == CMPCI_REG_RATE_48000))) {
		/* playback to SPDIF */
		if (sc->sc_play_channel == 0)
			cmpci_reg_set_4(sc, CMPCI_REG_FUNC_1,
			    CMPCI_REG_SPDIF0_ENABLE);
		else
			cmpci_reg_set_4(sc, CMPCI_REG_FUNC_1,
			    CMPCI_REG_SPDIF1_ENABLE);
		enspdout = 1;
		if (chan->md_divide == CMPCI_REG_RATE_48000)
			cmpci_reg_set_reg_misc(sc,
				CMPCI_REG_SPDIFOUT_48K | CMPCI_REG_SPDIF48K);
		else
			cmpci_reg_clear_reg_misc(sc,
				CMPCI_REG_SPDIFOUT_48K | CMPCI_REG_SPDIF48K);
		/* XXX assume sample rate <= 48kHz */
		cmpci_reg_clear_4(sc, CMPCI_REG_CHANNEL_FORMAT,
		    CMPCI_REG_DBL_SPD_RATE);
	} else {
		/* playback to DAC */
		if (sc->sc_play_channel == 0)
			cmpci_reg_clear_4(sc, CMPCI_REG_FUNC_1,
			    CMPCI_REG_SPDIF0_ENABLE);
		else
			cmpci_reg_clear_4(sc, CMPCI_REG_FUNC_1,
			    CMPCI_REG_SPDIF1_ENABLE);
		if (CMPCI_ISCAP(sc, SPDOUT_48K))
			cmpci_reg_clear_reg_misc(sc,
				CMPCI_REG_SPDIFOUT_48K | CMPCI_REG_SPDIF48K);
	}

	/* legacy to SPDIF/out or not */
	if (CMPCI_ISCAP(sc, SPDLEGACY)) {
		if (sc->sc_gain[CMPCI_SPDIF_OUT_PLAYBACK][CMPCI_LR]
		    == CMPCI_SPDIF_OUT_PLAYBACK_WAVE)
			cmpci_reg_clear_4(sc, CMPCI_REG_LEGACY_CTRL,
					CMPCI_REG_LEGACY_SPDIF_ENABLE);
		else {
			cmpci_reg_set_4(sc, CMPCI_REG_LEGACY_CTRL,
					CMPCI_REG_LEGACY_SPDIF_ENABLE);
			enspdout = 1;
		}
	}

	/* enable/disable SPDIF/out */
	if (CMPCI_ISCAP(sc, XSPDOUT) && enspdout)
		cmpci_reg_set_4(sc, CMPCI_REG_LEGACY_CTRL,
				CMPCI_REG_XSPDIF_ENABLE);
	else
		cmpci_reg_clear_4(sc, CMPCI_REG_LEGACY_CTRL,
				CMPCI_REG_XSPDIF_ENABLE);

	/* SPDIF monitor (digital to analog output) */
	if (CMPCI_ISCAP(sc, SPDIN_MONITOR)) {
		v = sc->sc_gain[CMPCI_MONITOR_DAC][CMPCI_LR];
		if (!(v & CMPCI_MONDAC_ENABLE))
			cmpci_reg_clear_1(sc, CMPCI_REG_MIXER24,
					CMPCI_REG_SPDIN_MONITOR);
		if (v & CMPCI_MONDAC_SPDOUT)
			cmpci_reg_set_4(sc, CMPCI_REG_FUNC_1,
					CMPCI_REG_SPDIFOUT_DAC);
		else
			cmpci_reg_clear_4(sc, CMPCI_REG_FUNC_1,
					CMPCI_REG_SPDIFOUT_DAC);
		if (v & CMPCI_MONDAC_ENABLE)
			cmpci_reg_set_1(sc, CMPCI_REG_MIXER24,
					CMPCI_REG_SPDIN_MONITOR);
	}
}

int
cmpci_set_in_ports(struct cmpci_softc *sc)
{
	int mask;
	int bitsl, bitsr;

	mask = sc->sc_in_mask;

	/*
	 * Note CMPCI_RECORD_SOURCE_CD, CMPCI_RECORD_SOURCE_LINE_IN and
	 * CMPCI_RECORD_SOURCE_FM are defined to the corresponding bit
	 * of the mixer register.
	 */
	bitsr = mask & (CMPCI_RECORD_SOURCE_CD | CMPCI_RECORD_SOURCE_LINE_IN |
	    CMPCI_RECORD_SOURCE_FM);

	bitsl = CMPCI_SB16_MIXER_SRC_R_TO_L(bitsr);
	if (mask & CMPCI_RECORD_SOURCE_MIC) {
		bitsl |= CMPCI_SB16_MIXER_MIC_SRC;
		bitsr |= CMPCI_SB16_MIXER_MIC_SRC;
	}
	cmpci_mixerreg_write(sc, CMPCI_SB16_MIXER_ADCMIX_L, bitsl);
	cmpci_mixerreg_write(sc, CMPCI_SB16_MIXER_ADCMIX_R, bitsr);

	if (mask & CMPCI_RECORD_SOURCE_AUX_IN)
		cmpci_reg_set_1(sc, CMPCI_REG_MIXER25,
		    CMPCI_REG_RAUXREN | CMPCI_REG_RAUXLEN);
	else
		cmpci_reg_clear_1(sc, CMPCI_REG_MIXER25,
		    CMPCI_REG_RAUXREN | CMPCI_REG_RAUXLEN);

	if (mask & CMPCI_RECORD_SOURCE_WAVE)
		cmpci_reg_set_1(sc, CMPCI_REG_MIXER24,
		    CMPCI_REG_WAVEINL | CMPCI_REG_WAVEINR);
	else
		cmpci_reg_clear_1(sc, CMPCI_REG_MIXER24,
		    CMPCI_REG_WAVEINL | CMPCI_REG_WAVEINR);

	if (CMPCI_ISCAP(sc, SPDIN) &&
	    (sc->sc_ch1.md_divide == CMPCI_REG_RATE_44100 ||
		(CMPCI_ISCAP(sc, SPDOUT_48K) &&
		    sc->sc_ch1.md_divide == CMPCI_REG_RATE_48000/* XXX? */))) {
		if (mask & CMPCI_RECORD_SOURCE_SPDIF) {
			/* enable SPDIF/in */
			cmpci_reg_set_4(sc,
					CMPCI_REG_FUNC_1,
					CMPCI_REG_SPDIF1_ENABLE);
		} else {
			cmpci_reg_clear_4(sc,
					CMPCI_REG_FUNC_1,
					CMPCI_REG_SPDIF1_ENABLE);
		}
	}

	return 0;
}

int
cmpci_set_port(void *handle, mixer_ctrl_t *cp)
{
	struct cmpci_softc *sc = handle;
	int lgain, rgain;

	switch (cp->dev) {
	case CMPCI_MIC_VOL:
	case CMPCI_PCSPEAKER:
	case CMPCI_MIC_RECVOL:
		if (cp->un.value.num_channels != 1)
			return EINVAL;
		/* FALLTHROUGH */
	case CMPCI_DAC_VOL:
	case CMPCI_FM_VOL:
	case CMPCI_CD_VOL:
	case CMPCI_LINE_IN_VOL:
	case CMPCI_AUX_IN_VOL:
	case CMPCI_MASTER_VOL:
		if (cp->type != AUDIO_MIXER_VALUE)
			return EINVAL;
		switch (cp->un.value.num_channels) {
		case 1:
			lgain = rgain =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO];
			break;
		case 2:
			lgain = cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT];
			rgain = cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT];
			break;
		default:
			return EINVAL;
		}
		sc->sc_gain[cp->dev][CMPCI_LEFT]  = lgain;
		sc->sc_gain[cp->dev][CMPCI_RIGHT] = rgain;

		cmpci_set_mixer_gain(sc, cp->dev);
		break;

	case CMPCI_RECORD_SOURCE:
		if (cp->type != AUDIO_MIXER_SET)
			return EINVAL;

		if (cp->un.mask & ~(CMPCI_RECORD_SOURCE_MIC |
		    CMPCI_RECORD_SOURCE_CD | CMPCI_RECORD_SOURCE_LINE_IN |
		    CMPCI_RECORD_SOURCE_AUX_IN | CMPCI_RECORD_SOURCE_WAVE |
		    CMPCI_RECORD_SOURCE_FM | CMPCI_RECORD_SOURCE_SPDIF))
			return EINVAL;

		if (cp->un.mask & CMPCI_RECORD_SOURCE_SPDIF)
			cp->un.mask = CMPCI_RECORD_SOURCE_SPDIF;

		sc->sc_in_mask = cp->un.mask;
		return cmpci_set_in_ports(sc);

	/* boolean */
	case CMPCI_DAC_MUTE:
	case CMPCI_FM_MUTE:
	case CMPCI_CD_MUTE:
	case CMPCI_LINE_IN_MUTE:
	case CMPCI_AUX_IN_MUTE:
	case CMPCI_MIC_MUTE:
	case CMPCI_MIC_PREAMP:
	case CMPCI_PLAYBACK_MODE:
	case CMPCI_SPDIF_IN_PHASE:
	case CMPCI_SPDIF_LOOP:
	case CMPCI_SPDIF_OUT_PLAYBACK:
	case CMPCI_SPDIF_OUT_VOLTAGE:
	case CMPCI_REAR:
	case CMPCI_INDIVIDUAL:
	case CMPCI_REVERSE:
	case CMPCI_SURROUND:
		if (cp->type != AUDIO_MIXER_ENUM)
			return EINVAL;
		sc->sc_gain[cp->dev][CMPCI_LR] = cp->un.ord != 0;
		cmpci_set_mixer_gain(sc, cp->dev);
		break;

	case CMPCI_SPDIF_IN_SELECT:
		switch (cp->un.ord) {
		case CMPCI_SPDIF_IN_SPDIN1:
		case CMPCI_SPDIF_IN_SPDIN2:
		case CMPCI_SPDIF_IN_SPDOUT:
			break;
		default:
			return EINVAL;
		}
		goto xenum;
	case CMPCI_MONITOR_DAC:
		switch (cp->un.ord) {
		case CMPCI_MONITOR_DAC_OFF:
		case CMPCI_MONITOR_DAC_SPDIN:
		case CMPCI_MONITOR_DAC_SPDOUT:
			break;
		default:
			return EINVAL;
		}
	xenum:
		if (cp->type != AUDIO_MIXER_ENUM)
			return EINVAL;
		sc->sc_gain[cp->dev][CMPCI_LR] = cp->un.ord;
		cmpci_set_mixer_gain(sc, cp->dev);
		break;

	default:
	    return EINVAL;
	}

	return 0;
}

int
cmpci_get_port(void *handle, mixer_ctrl_t *cp)
{
	struct cmpci_softc *sc = handle;

	switch (cp->dev) {
	case CMPCI_MIC_VOL:
	case CMPCI_PCSPEAKER:
	case CMPCI_MIC_RECVOL:
		if (cp->un.value.num_channels != 1)
			return EINVAL;
		/*FALLTHROUGH*/
	case CMPCI_DAC_VOL:
	case CMPCI_FM_VOL:
	case CMPCI_CD_VOL:
	case CMPCI_LINE_IN_VOL:
	case CMPCI_AUX_IN_VOL:
	case CMPCI_MASTER_VOL:
		switch (cp->un.value.num_channels) {
		case 1:
			cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
				sc->sc_gain[cp->dev][CMPCI_LEFT];
			break;
		case 2:
			cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
				sc->sc_gain[cp->dev][CMPCI_LEFT];
			cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] =
				sc->sc_gain[cp->dev][CMPCI_RIGHT];
			break;
		default:
			return EINVAL;
		}
		break;

	case CMPCI_RECORD_SOURCE:
		cp->un.mask = sc->sc_in_mask;
		break;

	case CMPCI_DAC_MUTE:
	case CMPCI_FM_MUTE:
	case CMPCI_CD_MUTE:
	case CMPCI_LINE_IN_MUTE:
	case CMPCI_AUX_IN_MUTE:
	case CMPCI_MIC_MUTE:
	case CMPCI_MIC_PREAMP:
	case CMPCI_PLAYBACK_MODE:
	case CMPCI_SPDIF_IN_SELECT:
	case CMPCI_SPDIF_IN_PHASE:
	case CMPCI_SPDIF_LOOP:
	case CMPCI_SPDIF_OUT_PLAYBACK:
	case CMPCI_SPDIF_OUT_VOLTAGE:
	case CMPCI_MONITOR_DAC:
	case CMPCI_REAR:
	case CMPCI_INDIVIDUAL:
	case CMPCI_REVERSE:
	case CMPCI_SURROUND:
		cp->un.ord = sc->sc_gain[cp->dev][CMPCI_LR];
		break;

	default:
		return EINVAL;
	}

	return 0;
}

size_t
cmpci_round_buffersize(void *handle, int direction, size_t bufsize)
{
	if (bufsize > 0x10000)
		bufsize = 0x10000;

	return bufsize;
}

int
cmpci_trigger_output(void *handle, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct cmpci_softc *sc = handle;
	struct cmpci_dmanode *p;
	struct cmpci_channel *chan;
	uint32_t reg_dma_base, reg_dma_bytes, reg_dma_samples, reg_dir,
	    reg_intr_enable, reg_enable;
	uint32_t length;
	size_t buffer_size = (caddr_t)end - (caddr_t)start;

	cmpci_set_out_ports(sc);

	if (sc->sc_play_channel == 1) {
		chan = &sc->sc_ch1;
		reg_dma_base = CMPCI_REG_DMA1_BASE;
		reg_dma_bytes = CMPCI_REG_DMA1_BYTES;
		reg_dma_samples = CMPCI_REG_DMA1_SAMPLES;
		reg_dir = CMPCI_REG_CH1_DIR;
		reg_intr_enable = CMPCI_REG_CH1_INTR_ENABLE;
		reg_enable = CMPCI_REG_CH1_ENABLE;
	} else {
		chan = &sc->sc_ch0;
		reg_dma_base = CMPCI_REG_DMA0_BASE;
		reg_dma_bytes = CMPCI_REG_DMA0_BYTES;
		reg_dma_samples = CMPCI_REG_DMA0_SAMPLES;
		reg_dir = CMPCI_REG_CH0_DIR;
		reg_intr_enable = CMPCI_REG_CH0_INTR_ENABLE;
		reg_enable = CMPCI_REG_CH0_ENABLE;
	}

	chan->bps = (param->channels > 1 ? 2 : 1) * param->bps;
	if (!chan->bps)
		return EINVAL;

	chan->intr = intr;
	chan->intr_arg = arg;
	chan->blksize = blksize;
	chan->nblocks = buffer_size / chan->blksize;
	chan->swpos = 0;

	/* set DMA frame */
	if (!(p = cmpci_find_dmamem(sc, start)))
		return EINVAL;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, reg_dma_base,
	    DMAADDR(p));
	delay(10);
	length = (buffer_size + 1) / chan->bps - 1;
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, reg_dma_bytes, length);
	delay(10);

	/* set interrupt count */
	length = (chan->blksize + chan->bps - 1) / chan->bps - 1;
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, reg_dma_samples, length);
	delay(10);

	/* start DMA */
	mtx_enter(&audio_lock);
	cmpci_reg_clear_4(sc, CMPCI_REG_FUNC_0, reg_dir); /* PLAY */
	cmpci_reg_set_4(sc, CMPCI_REG_INTR_CTRL, reg_intr_enable);
	cmpci_reg_set_4(sc, CMPCI_REG_FUNC_0, reg_enable);
	mtx_leave(&audio_lock);
	return 0;
}

int
cmpci_trigger_input(void *handle, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct cmpci_softc *sc = handle;
	struct cmpci_dmanode *p;
	struct cmpci_channel *chan = &sc->sc_ch1;
	size_t buffer_size = (caddr_t)end - (caddr_t)start;

	cmpci_set_in_ports(sc);

	chan->bps = param->channels * param->bps;
	if (!chan->bps)
		return EINVAL;

	chan->intr = intr;
	chan->intr_arg = arg;
	chan->blksize = blksize;
	chan->nblocks = buffer_size / chan->blksize;
	chan->swpos = 0;

	/* set DMA frame */
	if (!(p = cmpci_find_dmamem(sc, start)))
		return EINVAL;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, CMPCI_REG_DMA1_BASE,
	    DMAADDR(p));
	delay(10);
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, CMPCI_REG_DMA1_BYTES,
	    (buffer_size + 1) / chan->bps - 1);
	delay(10);

	/* set interrupt count */
	bus_space_write_2(sc->sc_iot, sc->sc_ioh, CMPCI_REG_DMA1_SAMPLES,
	    (chan->blksize + chan->bps - 1) / chan->bps - 1);
	delay(10);

	/* start DMA */
	mtx_enter(&audio_lock);
	cmpci_reg_set_4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH1_DIR); /* REC */
	cmpci_reg_set_4(sc, CMPCI_REG_INTR_CTRL, CMPCI_REG_CH1_INTR_ENABLE);
	cmpci_reg_set_4(sc, CMPCI_REG_FUNC_0, CMPCI_REG_CH1_ENABLE);
	mtx_leave(&audio_lock);
	return 0;
}

/* end of file */
