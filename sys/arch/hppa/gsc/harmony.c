/*	$OpenBSD: harmony.c,v 1.42 2025/06/28 13:24:21 miod Exp $	*/

/*
 * Copyright (c) 2003 Jason L. Wright (jason@thought.net)
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

/*
 * Harmony (CS4215/AD1849 LASI) audio interface.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/malloc.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/iomod.h>
#include <machine/autoconf.h>
#include <machine/bus.h>

#include <hppa/dev/cpudevs.h>
#include <hppa/gsc/gscbusvar.h>
#include <hppa/gsc/harmonyreg.h>
#include <hppa/gsc/harmonyvar.h>

int     harmony_open(void *, int);
void    harmony_close(void *);
int     harmony_set_params(void *, int, int, struct audio_params *,
    struct audio_params *);
int     harmony_round_blocksize(void *, int);
int     harmony_commit_settings(void *);
int     harmony_halt_output(void *);
int     harmony_halt_input(void *);
int     harmony_set_port(void *, mixer_ctrl_t *);
int     harmony_get_port(void *, mixer_ctrl_t *);
int     harmony_query_devinfo(void *addr, mixer_devinfo_t *);
void *  harmony_allocm(void *, int, size_t, int, int);
void    harmony_freem(void *, void *, int);
size_t  harmony_round_buffersize(void *, int, size_t);
int     harmony_trigger_output(void *, void *, void *, int,
    void (*intr)(void *), void *, struct audio_params *);
int     harmony_trigger_input(void *, void *, void *, int,
    void (*intr)(void *), void *, struct audio_params *);

const struct audio_hw_if harmony_sa_hw_if = {
	.open = harmony_open,
	.close = harmony_close,
	.set_params = harmony_set_params,
	.round_blocksize = harmony_round_blocksize,
	.commit_settings = harmony_commit_settings,
	.halt_output = harmony_halt_output,
	.halt_input = harmony_halt_input,
	.set_port = harmony_set_port,
	.get_port = harmony_get_port,
	.query_devinfo = harmony_query_devinfo,
	.allocm = harmony_allocm,
	.freem = harmony_freem,
	.round_buffersize = harmony_round_buffersize,
	.trigger_output = harmony_trigger_output,
	.trigger_input = harmony_trigger_input,
};

int harmony_match(struct device *, void *, void *);
void harmony_attach(struct device *, struct device *, void *);
int harmony_intr(void *);
void harmony_intr_enable(struct harmony_softc *);
void harmony_intr_disable(struct harmony_softc *);
u_int32_t harmony_speed_bits(struct harmony_softc *, u_long *);
int harmony_set_gainctl(struct harmony_softc *);
void harmony_reset_codec(struct harmony_softc *);
void harmony_start_cp(struct harmony_softc *);
void harmony_try_more(struct harmony_softc *);

void harmony_acc_tmo(void *);
#define	ADD_CLKALLICA(sc) do {						\
	(sc)->sc_acc <<= 1;						\
	(sc)->sc_acc |= READ_REG((sc), HARMONY_DIAG) & DIAG_CO;		\
	if ((sc)->sc_acc_cnt++ && !((sc)->sc_acc_cnt % 32))		\
		enqueue_randomness((sc)->sc_acc_num ^= (sc)->sc_acc);	\
} while(0)

int
harmony_match(struct device *parent, void *match, void *aux)
{
	struct gsc_attach_args *ga = aux;
	bus_space_handle_t bh;
	u_int32_t cntl;

	if (ga->ga_type.iodc_type == HPPA_TYPE_FIO) {
		if (ga->ga_type.iodc_sv_model == HPPA_FIO_A1 ||
		    ga->ga_type.iodc_sv_model == HPPA_FIO_A2NB ||
		    ga->ga_type.iodc_sv_model == HPPA_FIO_A1NB ||
		    ga->ga_type.iodc_sv_model == HPPA_FIO_A2) {
			if (bus_space_map(ga->ga_iot, ga->ga_hpa,
			    HARMONY_NREGS, 0, &bh) != 0)
				return (0);
			cntl = bus_space_read_4(ga->ga_iot, bh, HARMONY_ID) &
			    ID_REV_MASK;
			bus_space_unmap(ga->ga_iot, bh, HARMONY_NREGS);
			if (cntl == ID_REV_TS || cntl == ID_REV_NOTS)
				return (1);
		}
	}
	return (0);
}

void
harmony_attach(struct device *parent, struct device *self, void *aux)
{
	struct harmony_softc *sc = (struct harmony_softc *)self;
	struct gsc_attach_args *ga = aux;
	u_int8_t rev;
	u_int32_t cntl;
	int i;

	sc->sc_bt = ga->ga_iot;
	sc->sc_dmat = ga->ga_dmatag;

	if (bus_space_map(sc->sc_bt, ga->ga_hpa, HARMONY_NREGS, 0,
	    &sc->sc_bh) != 0) {
		printf(": couldn't map registers\n");
		return;
	}

	cntl = READ_REG(sc, HARMONY_ID);
	sc->sc_teleshare = (cntl & ID_REV_MASK) == ID_REV_TS;

	if (bus_dmamem_alloc(sc->sc_dmat, sizeof(struct harmony_empty),
	    PAGE_SIZE, 0, &sc->sc_empty_seg, 1, &sc->sc_empty_rseg,
	    BUS_DMA_NOWAIT) != 0) {
		printf(": couldn't alloc DMA memory\n");
		bus_space_unmap(sc->sc_bt, sc->sc_bh, HARMONY_NREGS);
		return;
	}
	if (bus_dmamem_map(sc->sc_dmat, &sc->sc_empty_seg, 1,
	    sizeof(struct harmony_empty), (caddr_t *)&sc->sc_empty_kva,
	    BUS_DMA_NOWAIT) != 0) {
		printf(": couldn't map DMA memory\n");
		bus_dmamem_free(sc->sc_dmat, &sc->sc_empty_seg,
		    sc->sc_empty_rseg);
		bus_space_unmap(sc->sc_bt, sc->sc_bh, HARMONY_NREGS);
		return;
	}
	if (bus_dmamap_create(sc->sc_dmat, sizeof(struct harmony_empty), 1,
	    sizeof(struct harmony_empty), 0, BUS_DMA_NOWAIT,
	    &sc->sc_empty_map) != 0) {
		printf(": can't create DMA map\n");
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->sc_empty_kva,
		    sizeof(struct harmony_empty));
		bus_dmamem_free(sc->sc_dmat, &sc->sc_empty_seg,
		    sc->sc_empty_rseg);
		bus_space_unmap(sc->sc_bt, sc->sc_bh, HARMONY_NREGS);
		return;
	}
	if (bus_dmamap_load(sc->sc_dmat, sc->sc_empty_map, sc->sc_empty_kva,
	    sizeof(struct harmony_empty), NULL, BUS_DMA_NOWAIT) != 0) {
		printf(": can't load DMA map\n");
		bus_dmamap_destroy(sc->sc_dmat, sc->sc_empty_map);
		bus_dmamem_unmap(sc->sc_dmat, (caddr_t)sc->sc_empty_kva,
		    sizeof(struct harmony_empty));
		bus_dmamem_free(sc->sc_dmat, &sc->sc_empty_seg,
		    sc->sc_empty_rseg);
		bus_space_unmap(sc->sc_bt, sc->sc_bh, HARMONY_NREGS);
		return;
	}

	sc->sc_playback_empty = 0;
	for (i = 0; i < PLAYBACK_EMPTYS; i++)
		sc->sc_playback_paddrs[i] =
		    sc->sc_empty_map->dm_segs[0].ds_addr +
		    offsetof(struct harmony_empty, playback[i][0]);

	sc->sc_capture_empty = 0;
	for (i = 0; i < CAPTURE_EMPTYS; i++)
		sc->sc_capture_paddrs[i] =
		    sc->sc_empty_map->dm_segs[0].ds_addr +
		    offsetof(struct harmony_empty, playback[i][0]);

	bus_dmamap_sync(sc->sc_dmat, sc->sc_empty_map,
	    offsetof(struct harmony_empty, playback[0][0]),
	    PLAYBACK_EMPTYS * HARMONY_BUFSIZE, BUS_DMASYNC_PREWRITE);

	(void)gsc_intr_establish((struct gsc_softc *)parent, ga->ga_irq,
	    IPL_AUDIO, harmony_intr, sc, sc->sc_dv.dv_xname);

	/* set defaults */
	sc->sc_in_port = HARMONY_IN_LINE;
	sc->sc_out_port = HARMONY_OUT_SPEAKER;
	sc->sc_input_lvl.left = sc->sc_input_lvl.right = 240;
	sc->sc_output_lvl.left = sc->sc_output_lvl.right = 244;
	sc->sc_monitor_lvl.left = sc->sc_monitor_lvl.right = 208;
	sc->sc_outputgain = 0;

	/* reset chip, and push default gain controls */
	harmony_reset_codec(sc);

	cntl = READ_REG(sc, HARMONY_CNTL);
	rev = (cntl & CNTL_CODEC_REV_MASK) >> CNTL_CODEC_REV_SHIFT;
	printf(": rev %u", rev);

	if (sc->sc_teleshare)
		printf(", teleshare");
	printf("\n");

	if ((rev & CS4215_REV_VER) >= CS4215_REV_VER_E)
		sc->sc_hasulinear8 = 1;

	audio_attach_mi(&harmony_sa_hw_if, sc, NULL, &sc->sc_dv);

	timeout_set(&sc->sc_acc_tmo, harmony_acc_tmo, sc);
	sc->sc_acc_num = 0xa5a5a5a5;
}

void
harmony_reset_codec(struct harmony_softc *sc)
{
	/* silence */
	WRITE_REG(sc, HARMONY_GAINCTL, GAINCTL_OUTPUT_LEFT_M |
	    GAINCTL_OUTPUT_RIGHT_M | GAINCTL_MONITOR_M);

	/* start reset */
	WRITE_REG(sc, HARMONY_RESET, RESET_RST);

	DELAY(100000);		/* wait at least 0.05 sec */

	harmony_set_gainctl(sc);
	WRITE_REG(sc, HARMONY_RESET, 0);
}

void
harmony_acc_tmo(void *v)
{
	struct harmony_softc *sc = v;

	ADD_CLKALLICA(sc);
	timeout_add(&sc->sc_acc_tmo, 1);
}

/*
 * interrupt handler
 */
int
harmony_intr(void *vsc)
{
	struct harmony_softc *sc = vsc;
	struct harmony_channel *c;
	u_int32_t dstatus;
	int r = 0;

	mtx_enter(&audio_lock);
	ADD_CLKALLICA(sc);

	harmony_intr_disable(sc);

	dstatus = READ_REG(sc, HARMONY_DSTATUS);

	if (dstatus & DSTATUS_PN) {
		struct harmony_dma *d;
		bus_addr_t nextaddr;
		bus_size_t togo;

		r = 1;
		c = &sc->sc_playback;
		d = c->c_current;
		togo = c->c_segsz - c->c_cnt;
		if (togo == 0) {
			nextaddr = d->d_map->dm_segs[0].ds_addr;
			c->c_cnt = togo = c->c_blksz;
		} else {
			nextaddr = c->c_lastaddr;
			if (togo > c->c_blksz)
				togo = c->c_blksz;
			c->c_cnt += togo;
		}

		bus_dmamap_sync(sc->sc_dmat, d->d_map,
		    nextaddr - d->d_map->dm_segs[0].ds_addr,
		    c->c_blksz, BUS_DMASYNC_PREWRITE);

		WRITE_REG(sc, HARMONY_PNXTADD, nextaddr);
		SYNC_REG(sc, HARMONY_PNXTADD, BUS_SPACE_BARRIER_WRITE);
		c->c_lastaddr = nextaddr + togo;
		harmony_try_more(sc);
	}

	dstatus = READ_REG(sc, HARMONY_DSTATUS);

	if (dstatus & DSTATUS_RN) {
		c = &sc->sc_capture;
		r = 1;
		harmony_start_cp(sc);
		if (sc->sc_capturing && c->c_intr != NULL)
			(*c->c_intr)(c->c_intrarg);
	}

	if (READ_REG(sc, HARMONY_OV) & OV_OV) {
		sc->sc_ov = 1;
		WRITE_REG(sc, HARMONY_OV, 0);
	} else
		sc->sc_ov = 0;

	harmony_intr_enable(sc);
	mtx_leave(&audio_lock);
	return (r);
}

void
harmony_intr_enable(struct harmony_softc *sc)
{
	WRITE_REG(sc, HARMONY_DSTATUS, DSTATUS_IE);
	SYNC_REG(sc, HARMONY_DSTATUS, BUS_SPACE_BARRIER_WRITE);
}

void
harmony_intr_disable(struct harmony_softc *sc)
{
	WRITE_REG(sc, HARMONY_DSTATUS, 0);
	SYNC_REG(sc, HARMONY_DSTATUS, BUS_SPACE_BARRIER_WRITE);
}

int
harmony_open(void *vsc, int flags)
{
	struct harmony_softc *sc = vsc;

	if (sc->sc_open)
		return (EBUSY);
	sc->sc_open = 1;
	return (0);
}

void
harmony_close(void *vsc)
{
	struct harmony_softc *sc = vsc;

	/* XXX: not useful, halt_*() already called */
	harmony_halt_input(sc);
	harmony_halt_output(sc);
	harmony_intr_disable(sc);
	sc->sc_open = 0;
}

int
harmony_set_params(void *vsc, int setmode, int usemode,
    struct audio_params *p, struct audio_params *r)
{
	struct harmony_softc *sc = vsc;
	u_int32_t bits;

	switch (p->encoding) {
	case AUDIO_ENCODING_ULAW:
		bits = CNTL_FORMAT_ULAW;
		p->precision = 8;
		break;
	case AUDIO_ENCODING_ALAW:
		bits = CNTL_FORMAT_ALAW;
		p->precision = 8;
		break;
	case AUDIO_ENCODING_SLINEAR_BE:
		if (p->precision == 16) {
			bits = CNTL_FORMAT_SLINEAR16BE;
			break;
		}
		return (EINVAL);
	case AUDIO_ENCODING_ULINEAR_LE:
	case AUDIO_ENCODING_ULINEAR_BE:
		if (p->precision == 8) {
			bits = CNTL_FORMAT_ULINEAR8;
			break;
		}
		return (EINVAL);
	default:
		return (EINVAL);
	}

	if (sc->sc_outputgain)
		bits |= CNTL_OLB;

	if (p->channels == 1)
		bits |= CNTL_CHANS_MONO;
	else if (p->channels == 2)
		bits |= CNTL_CHANS_STEREO;
	else
		return (EINVAL);

	r->sample_rate = p->sample_rate;
	r->encoding = p->encoding;
	r->precision = p->precision;
	p->bps = AUDIO_BPS(p->precision);
	r->bps = AUDIO_BPS(r->precision);
	p->msb = r->msb = 1;

	bits |= harmony_speed_bits(sc, &p->sample_rate);
	sc->sc_cntlbits = bits;
	sc->sc_need_commit = 1;

	return (0);
}

int
harmony_round_blocksize(void *vsc, int blk)
{
	return (HARMONY_BUFSIZE);
}

int
harmony_commit_settings(void *vsc)
{
	struct harmony_softc *sc = vsc;
	u_int32_t reg;
	u_int8_t quietchar;
	int i;

	if (sc->sc_need_commit == 0)
		return (0);

	harmony_intr_disable(sc);

	for (;;) {
		reg = READ_REG(sc, HARMONY_DSTATUS);
		if ((reg & (DSTATUS_PC | DSTATUS_RC)) == 0)
			break;
	}

	/* Setting some bits in gainctl requires a reset */
	harmony_reset_codec(sc);

	/* set the silence character based on the encoding type */
	bus_dmamap_sync(sc->sc_dmat, sc->sc_empty_map,
	    offsetof(struct harmony_empty, playback[0][0]),
	    PLAYBACK_EMPTYS * HARMONY_BUFSIZE, BUS_DMASYNC_POSTWRITE);
	switch (sc->sc_cntlbits & CNTL_FORMAT_MASK) {
	case CNTL_FORMAT_ULAW:
		quietchar = 0x7f;
		break;
	case CNTL_FORMAT_ALAW:
		quietchar = 0x55;
		break;
	case CNTL_FORMAT_SLINEAR16BE:
	case CNTL_FORMAT_ULINEAR8:
	default:
		quietchar = 0;
		break;
	}
	for (i = 0; i < PLAYBACK_EMPTYS; i++)
		memset(&sc->sc_empty_kva->playback[i][0],
		    quietchar, HARMONY_BUFSIZE);
	bus_dmamap_sync(sc->sc_dmat, sc->sc_empty_map,
	    offsetof(struct harmony_empty, playback[0][0]),
	    PLAYBACK_EMPTYS * HARMONY_BUFSIZE, BUS_DMASYNC_PREWRITE);

	for (;;) {
		/* Wait for it to come out of control mode */
		reg = READ_REG(sc, HARMONY_CNTL);
		if ((reg & CNTL_C) == 0)
			break;
	}

	bus_space_write_4(sc->sc_bt, sc->sc_bh, HARMONY_CNTL,
	    sc->sc_cntlbits | CNTL_C);

	for (;;) {
		/* Wait for it to come out of control mode */
		reg = READ_REG(sc, HARMONY_CNTL);
		if ((reg & CNTL_C) == 0)
			break;
	}

	sc->sc_need_commit = 0;

	if (sc->sc_playing || sc->sc_capturing)
		harmony_intr_enable(sc);

	return (0);
}

int
harmony_halt_output(void *vsc)
{
	struct harmony_softc *sc = vsc;

	/* XXX: disable interrupts */
	sc->sc_playing = 0;
	return (0);
}

int
harmony_halt_input(void *vsc)
{
	struct harmony_softc *sc = vsc;

	/* XXX: disable interrupts */
	sc->sc_capturing = 0;
	return (0);
}

int
harmony_set_port(void *vsc, mixer_ctrl_t *cp)
{
	struct harmony_softc *sc = vsc;
	int err = EINVAL;

	switch (cp->dev) {
	case HARMONY_PORT_INPUT_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1)
			sc->sc_input_lvl.left = sc->sc_input_lvl.right =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO];
		else if (cp->un.value.num_channels == 2) {
			sc->sc_input_lvl.left =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT];
			sc->sc_input_lvl.right =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT];
		} else
			break;
		sc->sc_need_commit = 1;
		err = 0;
		break;
	case HARMONY_PORT_OUTPUT_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1)
			sc->sc_output_lvl.left = sc->sc_output_lvl.right =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO];
		else if (cp->un.value.num_channels == 2) {
			sc->sc_output_lvl.left =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT];
			sc->sc_output_lvl.right =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT];
		} else
			break;
		sc->sc_need_commit = 1;
		err = 0;
		break;
	case HARMONY_PORT_OUTPUT_GAIN:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		sc->sc_outputgain = cp->un.ord ? 1 : 0;
		err = 0;
		break;
	case HARMONY_PORT_MONITOR_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels != 1)
			break;
		sc->sc_monitor_lvl.left = sc->sc_input_lvl.right =
		    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO];
		sc->sc_need_commit = 1;
		err = 0;
		break;
	case HARMONY_PORT_RECORD_SOURCE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		if (cp->un.ord != HARMONY_IN_LINE &&
		    cp->un.ord != HARMONY_IN_MIC)
			break;
		sc->sc_in_port = cp->un.ord;
		err = 0;
		sc->sc_need_commit = 1;
		break;
	case HARMONY_PORT_OUTPUT_SOURCE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		if (cp->un.ord != HARMONY_OUT_LINE &&
		    cp->un.ord != HARMONY_OUT_SPEAKER &&
		    cp->un.ord != HARMONY_OUT_HEADPHONE)
			break;
		sc->sc_out_port = cp->un.ord;
		err = 0;
		sc->sc_need_commit = 1;
		break;
	}

	return (err);
}

int
harmony_get_port(void *vsc, mixer_ctrl_t *cp)
{
	struct harmony_softc *sc = vsc;
	int err = EINVAL;

	switch (cp->dev) {
	case HARMONY_PORT_INPUT_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1) {
			cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
			    sc->sc_input_lvl.left;
		} else if (cp->un.value.num_channels == 2) {
			cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
			    sc->sc_input_lvl.left;
			cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] =
			    sc->sc_input_lvl.right;
		} else
			break;
		err = 0;
		break;
	case HARMONY_PORT_INPUT_OV:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		cp->un.ord = sc->sc_ov ? 1 : 0;
		err = 0;
		break;
	case HARMONY_PORT_OUTPUT_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1) {
			cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
			    sc->sc_output_lvl.left;
		} else if (cp->un.value.num_channels == 2) {
			cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
			    sc->sc_output_lvl.left;
			cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] =
			    sc->sc_output_lvl.right;
		} else
			break;
		err = 0;
		break;
	case HARMONY_PORT_OUTPUT_GAIN:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		cp->un.ord = sc->sc_outputgain ? 1 : 0;
		err = 0;
		break;
	case HARMONY_PORT_MONITOR_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels != 1)
			break;
		cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
		    sc->sc_monitor_lvl.left;
		err = 0;
		break;
	case HARMONY_PORT_RECORD_SOURCE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		cp->un.ord = sc->sc_in_port;
		err = 0;
		break;
	case HARMONY_PORT_OUTPUT_SOURCE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		cp->un.ord = sc->sc_out_port;
		err = 0;
		break;
	}
	return (0);
}

int
harmony_query_devinfo(void *vsc, mixer_devinfo_t *dip)
{
	int err = 0;

	switch (dip->index) {
	case HARMONY_PORT_INPUT_LVL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = HARMONY_PORT_INPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNinput, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case HARMONY_PORT_INPUT_OV:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = HARMONY_PORT_INPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, "overrange", sizeof dip->label.name);
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNon,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		break;
	case HARMONY_PORT_OUTPUT_LVL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = HARMONY_PORT_OUTPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNoutput, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case HARMONY_PORT_OUTPUT_GAIN:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = HARMONY_PORT_OUTPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, "gain", sizeof dip->label.name);
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNon,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		break;
	case HARMONY_PORT_MONITOR_LVL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = HARMONY_PORT_MONITOR_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmonitor, sizeof dip->label.name);
		dip->un.v.num_channels = 1;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case HARMONY_PORT_RECORD_SOURCE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = HARMONY_PORT_RECORD_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNsource, sizeof dip->label.name);
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNmicrophone,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = HARMONY_IN_MIC;
		strlcpy(dip->un.e.member[1].label.name, AudioNline,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = HARMONY_IN_LINE;
		break;
	case HARMONY_PORT_OUTPUT_SOURCE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = HARMONY_PORT_MONITOR_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNoutput, sizeof dip->label.name);
		dip->un.e.num_mem = 3;
		strlcpy(dip->un.e.member[0].label.name, AudioNline,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = HARMONY_OUT_LINE;
		strlcpy(dip->un.e.member[1].label.name, AudioNspeaker,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = HARMONY_OUT_SPEAKER;
		strlcpy(dip->un.e.member[2].label.name, AudioNheadphone,
		    sizeof dip->un.e.member[2].label.name);
		dip->un.e.member[2].ord = HARMONY_OUT_HEADPHONE;
		break;
	case HARMONY_PORT_INPUT_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = HARMONY_PORT_INPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCinputs, sizeof dip->label.name);
		break;
	case HARMONY_PORT_OUTPUT_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = HARMONY_PORT_INPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCoutputs, sizeof dip->label.name);
		break;
	case HARMONY_PORT_MONITOR_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = HARMONY_PORT_INPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCmonitor, sizeof dip->label.name);
		break;
	case HARMONY_PORT_RECORD_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = HARMONY_PORT_RECORD_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCrecord, sizeof dip->label.name);
		break;
	default:
		err = ENXIO;
		break;
	}

	return (err);
}

void *
harmony_allocm(void *vsc, int dir, size_t size, int pool, int flags)
{
	struct harmony_softc *sc = vsc;
	struct harmony_dma *d;
	int rseg;

	d = (struct harmony_dma *)malloc(sizeof(struct harmony_dma), pool, flags);
	if (d == NULL)
		goto fail;

	if (bus_dmamap_create(sc->sc_dmat, size, 1, size, 0, BUS_DMA_NOWAIT,
	    &d->d_map) != 0)
		goto fail1;

	if (bus_dmamem_alloc(sc->sc_dmat, size, PAGE_SIZE, 0, &d->d_seg, 1,
	    &rseg, BUS_DMA_NOWAIT) != 0)
		goto fail2;

	if (bus_dmamem_map(sc->sc_dmat, &d->d_seg, 1, size, &d->d_kva,
	    BUS_DMA_NOWAIT) != 0)
		goto fail3;

	if (bus_dmamap_load(sc->sc_dmat, d->d_map, d->d_kva, size, NULL,
	    BUS_DMA_NOWAIT) != 0)
		goto fail4;

	d->d_next = sc->sc_dmas;
	sc->sc_dmas = d;
	d->d_size = size;
	return (d->d_kva);

fail4:
	bus_dmamem_unmap(sc->sc_dmat, d->d_kva, size);
fail3:
	bus_dmamem_free(sc->sc_dmat, &d->d_seg, 1);
fail2:
	bus_dmamap_destroy(sc->sc_dmat, d->d_map);
fail1:
	free(d, pool, sizeof *d);
fail:
	return (NULL);
}

void
harmony_freem(void *vsc, void *ptr, int pool)
{
	struct harmony_softc *sc = vsc;
	struct harmony_dma *d, **dd;

	for (dd = &sc->sc_dmas; (d = *dd) != NULL; dd = &(*dd)->d_next) {
		if (d->d_kva != ptr)
			continue;
		bus_dmamap_unload(sc->sc_dmat, d->d_map);
		bus_dmamem_unmap(sc->sc_dmat, d->d_kva, d->d_size);
		bus_dmamem_free(sc->sc_dmat, &d->d_seg, 1);
		bus_dmamap_destroy(sc->sc_dmat, d->d_map);
		free(d, pool, sizeof *d);
		return;
	}
	printf("%s: free rogue pointer\n", sc->sc_dv.dv_xname);
}

size_t
harmony_round_buffersize(void *vsc, int direction, size_t size)
{
	return ((size + HARMONY_BUFSIZE - 1) & (size_t)(-HARMONY_BUFSIZE));
}

int
harmony_trigger_output(void *vsc, void *start, void *end, int blksize,
    void (*intr)(void *), void *intrarg, struct audio_params *param)
{
	struct harmony_softc *sc = vsc;
	struct harmony_channel *c = &sc->sc_playback;
	struct harmony_dma *d;
	bus_addr_t nextaddr;
	bus_size_t togo;

	for (d = sc->sc_dmas; d->d_kva != start; d = d->d_next)
		/*EMPTY*/;
	if (d == NULL) {
		printf("%s: trigger_output: bad addr: %p\n",
		    sc->sc_dv.dv_xname, start);
		return (EINVAL);
	}

	c->c_intr = intr;
	c->c_intrarg = intrarg;
	c->c_blksz = blksize;
	c->c_current = d;
	c->c_segsz = (caddr_t)end - (caddr_t)start;
	c->c_cnt = 0;
	c->c_lastaddr = d->d_map->dm_segs[0].ds_addr;

	sc->sc_playing = 1;

	togo = c->c_segsz - c->c_cnt;
	if (togo == 0) {
		nextaddr = d->d_map->dm_segs[0].ds_addr;
		c->c_cnt = togo = c->c_blksz;
	} else {
		nextaddr = c->c_lastaddr;
		if (togo > c->c_blksz)
			togo = c->c_blksz;
		c->c_cnt += togo;
	}

	bus_dmamap_sync(sc->sc_dmat, d->d_map,
	    nextaddr - d->d_map->dm_segs[0].ds_addr,
	    c->c_blksz, BUS_DMASYNC_PREWRITE);

	mtx_enter(&audio_lock);
	WRITE_REG(sc, HARMONY_PNXTADD, nextaddr);
	c->c_theaddr = nextaddr;
	SYNC_REG(sc, HARMONY_PNXTADD, BUS_SPACE_BARRIER_WRITE);
	c->c_lastaddr = nextaddr + togo;

	harmony_start_cp(sc);
	harmony_intr_enable(sc);
	mtx_leave(&audio_lock);
	return (0);
}

void
harmony_start_cp(struct harmony_softc *sc)
{
	struct harmony_channel *c = &sc->sc_capture;
	struct harmony_dma *d;
	bus_addr_t nextaddr;
	bus_size_t togo;

	if (sc->sc_capturing == 0) {
		WRITE_REG(sc, HARMONY_RNXTADD,
		    sc->sc_capture_paddrs[sc->sc_capture_empty]);
		if (++sc->sc_capture_empty == CAPTURE_EMPTYS)
			sc->sc_capture_empty = 0;
	} else {
		d = c->c_current;
		togo = c->c_segsz - c->c_cnt;
		if (togo == 0) {
			nextaddr = d->d_map->dm_segs[0].ds_addr;
			c->c_cnt = togo = c->c_blksz;
		} else {
			nextaddr = c->c_lastaddr;
			if (togo > c->c_blksz)
				togo = c->c_blksz;
			c->c_cnt += togo;
		}

		bus_dmamap_sync(sc->sc_dmat, d->d_map,
		    nextaddr - d->d_map->dm_segs[0].ds_addr,
		    c->c_blksz, BUS_DMASYNC_PREWRITE);

		WRITE_REG(sc, HARMONY_RNXTADD, nextaddr);
		SYNC_REG(sc, HARMONY_RNXTADD, BUS_SPACE_BARRIER_WRITE);
		c->c_lastaddr = nextaddr + togo;
	}

	timeout_add(&sc->sc_acc_tmo, 1);
}

int
harmony_trigger_input(void *vsc, void *start, void *end, int blksize,
    void (*intr)(void *), void *intrarg, struct audio_params *param)
{
	struct harmony_softc *sc = vsc;
	struct harmony_channel *c = &sc->sc_capture;
	struct harmony_dma *d;

	for (d = sc->sc_dmas; d->d_kva != start; d = d->d_next)
		/*EMPTY*/;
	if (d == NULL) {
		printf("%s: trigger_input: bad addr: %p\n",
		    sc->sc_dv.dv_xname, start);
		return (EINVAL);
	}

	c->c_intr = intr;
	c->c_intrarg = intrarg;
	c->c_blksz = blksize;
	c->c_current = d;
	c->c_segsz = (caddr_t)end - (caddr_t)start;
	c->c_cnt = 0;
	c->c_lastaddr = d->d_map->dm_segs[0].ds_addr;
	mtx_enter(&audio_lock);
	sc->sc_capturing = 1;
	harmony_start_cp(sc);
	harmony_intr_enable(sc);
	mtx_leave(&audio_lock);
	return (0);
}

static const struct speed_struct {
	u_int32_t speed;
	u_int32_t bits;
} harmony_speeds[] = {
	{ 5125, CNTL_RATE_5125 },
	{ 6615, CNTL_RATE_6615 },
	{ 8000, CNTL_RATE_8000 },
	{ 9600, CNTL_RATE_9600 },
	{ 11025, CNTL_RATE_11025 },
	{ 16000, CNTL_RATE_16000 },
	{ 18900, CNTL_RATE_18900 },
	{ 22050, CNTL_RATE_22050 },
	{ 27428, CNTL_RATE_27428 },
	{ 32000, CNTL_RATE_32000 },
	{ 33075, CNTL_RATE_33075 },
	{ 37800, CNTL_RATE_37800 },
	{ 44100, CNTL_RATE_44100 },
	{ 48000, CNTL_RATE_48000 },
};

u_int32_t
harmony_speed_bits(struct harmony_softc *sc, u_long *speedp)
{
	int i, n, selected = -1;

	n = sizeof(harmony_speeds) / sizeof(harmony_speeds[0]);

	if ((*speedp) <= harmony_speeds[0].speed)
		selected = 0;
	else if ((*speedp) >= harmony_speeds[n - 1].speed)
		selected = n - 1;
	else {
		for (i = 1; selected == -1 && i < n; i++) {
			if ((*speedp) == harmony_speeds[i].speed)
				selected = i;
			else if ((*speedp) < harmony_speeds[i].speed) {
				int diff1, diff2;

				diff1 = (*speedp) - harmony_speeds[i - 1].speed;
				diff2 = harmony_speeds[i].speed - (*speedp);
				if (diff1 < diff2)
					selected = i - 1;
				else
					selected = i;
			}
		}
	}

	if (selected == -1)
		selected = 2;

	*speedp = harmony_speeds[selected].speed;
	return (harmony_speeds[selected].bits);
}

int
harmony_set_gainctl(struct harmony_softc *sc)
{
	u_int32_t bits, mask, val, old;

	/* XXX leave these bits alone or the chip will not come out of CNTL */
	bits = GAINCTL_LE | GAINCTL_HE | GAINCTL_SE | GAINCTL_IS_MASK;

	/* input level */
	bits |= ((sc->sc_input_lvl.left >> (8 - GAINCTL_INPUT_BITS)) <<
	    GAINCTL_INPUT_LEFT_S) & GAINCTL_INPUT_LEFT_M;
	bits |= ((sc->sc_input_lvl.right >> (8 - GAINCTL_INPUT_BITS)) <<
	    GAINCTL_INPUT_RIGHT_S) & GAINCTL_INPUT_RIGHT_M;

	/* output level (inverted) */
	mask = (1 << GAINCTL_OUTPUT_BITS) - 1;
	val = mask - (sc->sc_output_lvl.left >> (8 - GAINCTL_OUTPUT_BITS));
	bits |= (val << GAINCTL_OUTPUT_LEFT_S) & GAINCTL_OUTPUT_LEFT_M;
	val = mask - (sc->sc_output_lvl.right >> (8 - GAINCTL_OUTPUT_BITS));
	bits |= (val << GAINCTL_OUTPUT_RIGHT_S) & GAINCTL_OUTPUT_RIGHT_M;

	/* monitor level (inverted) */
	mask = (1 << GAINCTL_MONITOR_BITS) - 1;
	val = mask - (sc->sc_monitor_lvl.left >> (8 - GAINCTL_MONITOR_BITS));
	bits |= (val << GAINCTL_MONITOR_S) & GAINCTL_MONITOR_M;

	/* XXX messing with these causes CNTL_C to get stuck... grr. */
	bits &= ~GAINCTL_IS_MASK;
	if (sc->sc_in_port == HARMONY_IN_MIC)
		bits |= GAINCTL_IS_LINE;
	else
		bits |= GAINCTL_IS_MICROPHONE;

	/* XXX messing with these causes CNTL_C to get stuck... grr. */
	bits &= ~(GAINCTL_LE | GAINCTL_HE | GAINCTL_SE);
	if (sc->sc_out_port == HARMONY_OUT_LINE)
		bits |= GAINCTL_LE;
	else if (sc->sc_out_port == HARMONY_OUT_SPEAKER)
		bits |= GAINCTL_SE;
	else
		bits |= GAINCTL_HE;

	mask = GAINCTL_LE | GAINCTL_HE | GAINCTL_SE | GAINCTL_IS_MASK;
	old = bus_space_read_4(sc->sc_bt, sc->sc_bh, HARMONY_GAINCTL);
	bus_space_write_4(sc->sc_bt, sc->sc_bh, HARMONY_GAINCTL, bits);
	if ((old & mask) != (bits & mask))
		return (1);
	return (0);
}

void
harmony_try_more(struct harmony_softc *sc)
{
	struct harmony_channel *c = &sc->sc_playback;
	struct harmony_dma *d = c->c_current;
	u_int32_t cur;
	int i, nsegs;

	cur = bus_space_read_4(sc->sc_bt, sc->sc_bh, HARMONY_PCURADD);
	cur &= PCURADD_BUFMASK;
	nsegs = 0;

#ifdef DIAGNOSTIC
	if (cur < d->d_map->dm_segs[0].ds_addr ||
	    cur >= (d->d_map->dm_segs[0].ds_addr + c->c_segsz))
		panic("%s: bad current %x < %lx || %x > %lx",
		    sc->sc_dv.dv_xname, cur, d->d_map->dm_segs[0].ds_addr, cur,
		    d->d_map->dm_segs[0].ds_addr + c->c_segsz);
#endif /* DIAGNOSTIC */

	if (cur > c->c_theaddr) {
		nsegs = (cur - c->c_theaddr) / HARMONY_BUFSIZE;
	} else if (cur < c->c_theaddr) {
		nsegs = (d->d_map->dm_segs[0].ds_addr + c->c_segsz -
		    c->c_theaddr) / HARMONY_BUFSIZE;
		nsegs += (cur - d->d_map->dm_segs[0].ds_addr) /
		    HARMONY_BUFSIZE;
	}

	if (nsegs != 0 && c->c_intr != NULL) {
		for (i = 0; i < nsegs; i++)
			(*c->c_intr)(c->c_intrarg);
		c->c_theaddr = cur;
	}
}

struct cfdriver harmony_cd = {
	NULL, "harmony", DV_DULL
};

const struct cfattach harmony_ca = {
	sizeof(struct harmony_softc), harmony_match, harmony_attach
};
