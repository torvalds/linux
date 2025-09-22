/*	$OpenBSD: i2s.c,v 1.39 2024/05/22 05:51:49 jsg Exp $	*/
/*	$NetBSD: i2s.c,v 1.1 2003/12/27 02:19:34 grant Exp $	*/

/*-
 * Copyright (c) 2002 Tsubai Masanari.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/audioio.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/systm.h>

#include <dev/audio_if.h>
#include <dev/ofw/openfirm.h>
#include <macppc/dev/dbdma.h>

#include <uvm/uvm_extern.h>

#include <machine/autoconf.h>
#include <machine/pio.h>

#include <macppc/dev/i2svar.h>
#include <macppc/dev/i2sreg.h>
#include <macppc/pci/macobio.h>

#ifdef I2S_DEBUG
# define DPRINTF(x) printf x 
#else
# define DPRINTF(x)
#endif

void	i2s_mute(u_int, int);
int	i2s_cint(void *);
u_int	i2s_gpio_offset(struct i2s_softc *, char *, int *);

int	i2s_intr(void *);
int	i2s_iintr(void *);

struct cfdriver i2s_cd = {
	NULL, "i2s", DV_DULL
};

void
i2s_attach(struct device *parent, struct i2s_softc *sc, struct confargs *ca)
{
	int cirq, oirq, iirq, cirq_type, oirq_type, iirq_type;
	u_int32_t reg[6], intr[6];
	char compat[32];
	int child;

	sc->sc_node = OF_child(ca->ca_node);
	sc->sc_baseaddr = ca->ca_baseaddr;

	OF_getprop(sc->sc_node, "reg", reg, sizeof reg);

	child = OF_child(sc->sc_node);
	memset(compat, 0, sizeof(compat));
	OF_getprop(child, "compatible", compat, sizeof(compat));

	/* Deal with broken device-tree on PowerMac7,2 and 7,3. */
	if (strcmp(compat, "AOAK2") == 0) {
		reg[0] += ca->ca_reg[0];
		reg[2] += ca->ca_reg[2];
		reg[4] += ca->ca_reg[2];
	}

	reg[0] += sc->sc_baseaddr;
	reg[2] += sc->sc_baseaddr;
	reg[4] += sc->sc_baseaddr;

	sc->sc_reg = mapiodev(reg[0], reg[1]);

	sc->sc_dmat = ca->ca_dmat;
	sc->sc_odma = mapiodev(reg[2], reg[3]); /* out */
	sc->sc_idma = mapiodev(reg[4], reg[5]); /* in */
	sc->sc_odbdma = dbdma_alloc(sc->sc_dmat, I2S_DMALIST_MAX);
	sc->sc_odmacmd = sc->sc_odbdma->d_addr;
	sc->sc_idbdma = dbdma_alloc(sc->sc_dmat, I2S_DMALIST_MAX);
	sc->sc_idmacmd = sc->sc_idbdma->d_addr;

	OF_getprop(sc->sc_node, "interrupts", intr, sizeof intr);
	cirq = intr[0];
	oirq = intr[2];
	iirq = intr[4];
	cirq_type = (intr[1] & 1) ? IST_LEVEL : IST_EDGE;
	oirq_type = (intr[3] & 1) ? IST_LEVEL : IST_EDGE;
	iirq_type = (intr[5] & 1) ? IST_LEVEL : IST_EDGE;

	/* intr_establish(cirq, cirq_type, IPL_AUDIO, i2s_intr, sc); */
	mac_intr_establish(parent, oirq, oirq_type, IPL_AUDIO | IPL_MPSAFE,
	    i2s_intr, sc, sc->sc_dev.dv_xname);
	mac_intr_establish(parent, iirq, iirq_type, IPL_AUDIO | IPL_MPSAFE,
	    i2s_iintr, sc, sc->sc_dev.dv_xname);

	printf(": irq %d,%d,%d\n", cirq, oirq, iirq);

	/* Need to be explicitly turned on some G5. */
	macobio_enable(I2SClockOffset, I2S0CLKEN|I2S0EN);

	i2s_set_rate(sc, 44100);
	sc->sc_mute = 0;
	i2s_gpio_init(sc, ca->ca_node, parent);
}

int
i2s_intr(void *v)
{
	struct i2s_softc *sc = v;
	struct dbdma_command *cmd = sc->sc_odmap;
	u_int16_t c, status;

	mtx_enter(&audio_lock);

	/* if not set we are not running */
	if (!cmd) {
		mtx_leave(&audio_lock);
		return (0);
	}
	DPRINTF(("i2s_intr: cmd %p\n", cmd));

	c = in16rb(&cmd->d_command);
	status = in16rb(&cmd->d_status);

	if (c >> 12 == DBDMA_CMD_OUT_LAST)
		sc->sc_odmap = sc->sc_odmacmd;
	else
		sc->sc_odmap++;

	if (c & (DBDMA_INT_ALWAYS << 4)) {
		cmd->d_status = 0;
		if (status)	/* status == 0x8400 */
			if (sc->sc_ointr)
				(*sc->sc_ointr)(sc->sc_oarg);
	}
	mtx_leave(&audio_lock);
	return 1;
}

int
i2s_iintr(void *v)
{
	struct i2s_softc *sc = v;
	struct dbdma_command *cmd = sc->sc_idmap;
	u_int16_t c, status;

	mtx_enter(&audio_lock);

	/* if not set we are not running */
	if (!cmd) {	
		mtx_leave(&audio_lock);
		return (0);
	}
	DPRINTF(("i2s_intr: cmd %p\n", cmd));

	c = in16rb(&cmd->d_command);
	status = in16rb(&cmd->d_status);

	if (c >> 12 == DBDMA_CMD_IN_LAST)
		sc->sc_idmap = sc->sc_idmacmd;
	else
		sc->sc_idmap++;

	if (c & (DBDMA_INT_ALWAYS << 4)) {
		cmd->d_status = 0;
		if (status)	/* status == 0x8400 */
			if (sc->sc_iintr)
				(*sc->sc_iintr)(sc->sc_iarg);
	}
	mtx_leave(&audio_lock);
	return 1;
}

int
i2s_open(void *h, int flags)
{
	return 0;
}

/*
 * Close function is called at splaudio().
 */
void
i2s_close(void *h)
{
	struct i2s_softc *sc = h;

	i2s_halt_output(sc);
	i2s_halt_input(sc);

	sc->sc_ointr = 0;
	sc->sc_iintr = 0;
}

int
i2s_set_params(void *h, int setmode, int usemode, struct audio_params *play,
    struct audio_params  *rec)
{
	struct i2s_softc *sc = h;
	struct audio_params *p;
	int mode;

	p = play; /* default to play */

	/*
	 * This device only has one clock, so make the sample rates match.
	 */
	if (play->sample_rate != rec->sample_rate &&
	    usemode == (AUMODE_PLAY | AUMODE_RECORD)) {
		if (setmode == AUMODE_PLAY) {
			rec->sample_rate = play->sample_rate;
			setmode |= AUMODE_RECORD;
		} else if (setmode == AUMODE_RECORD) {
			play->sample_rate = rec->sample_rate;
			setmode |= AUMODE_PLAY;
		} else
			return EINVAL;
	}

	for (mode = AUMODE_RECORD; mode != -1;
	     mode = mode == AUMODE_RECORD ? AUMODE_PLAY : -1) {
		if ((setmode & mode) == 0)
			continue;

		p = mode == AUMODE_PLAY ? play : rec;

		if (p->sample_rate < 4000)
			p->sample_rate = 4000;
		if (p->sample_rate > 50000)
			p->sample_rate = 50000;
		if (p->precision > 16)
			p->precision = 16;
		if (p->channels > 2)
			p->channels = 2;
		p->bps = AUDIO_BPS(p->precision);
		p->msb = 1;
		p->encoding = AUDIO_ENCODING_SLINEAR_BE;
	}

	/* Set the speed */
	if (i2s_set_rate(sc, play->sample_rate))
		return EINVAL;

	p->sample_rate = sc->sc_rate;
	return 0;
}

int
i2s_round_blocksize(void *h, int size)
{
	if (size < NBPG)
		size = NBPG;
	return size & ~PGOFSET;
}

int
i2s_halt_output(void *h)
{
	struct i2s_softc *sc = h;

	dbdma_stop(sc->sc_odma);
	dbdma_reset(sc->sc_odma);
	return 0;
}

int
i2s_halt_input(void *h)
{
	struct i2s_softc *sc = h;

	dbdma_stop(sc->sc_idma);
	dbdma_reset(sc->sc_idma);
	return 0;
}

enum {
	I2S_OUTPUT_CLASS,
	I2S_RECORD_CLASS,
	I2S_OUTPUT_SELECT,
	I2S_VOL_OUTPUT,
	I2S_INPUT_SELECT,
	I2S_VOL_INPUT,
	I2S_MUTE, 		/* should be before bass/treble */
	I2S_BASS,
	I2S_TREBLE,
	I2S_ENUM_LAST
};

int
i2s_set_port(void *h, mixer_ctrl_t *mc)
{
	struct i2s_softc *sc = h;
	int l, r;

	DPRINTF(("i2s_set_port dev = %d, type = %d\n", mc->dev, mc->type));

	l = mc->un.value.level[AUDIO_MIXER_LEVEL_LEFT];
	r = mc->un.value.level[AUDIO_MIXER_LEVEL_RIGHT];

	switch (mc->dev) {
	case I2S_OUTPUT_SELECT:
		/* No change necessary? */
		if (mc->un.mask == sc->sc_output_mask)
			return 0;

		i2s_mute(sc->sc_spkr, 1);
		i2s_mute(sc->sc_hp, 1);
		i2s_mute(sc->sc_line, 1);
		if (mc->un.mask & I2S_SELECT_SPEAKER)
			i2s_mute(sc->sc_spkr, 0);
		if (mc->un.mask & I2S_SELECT_HEADPHONE)
			i2s_mute(sc->sc_hp, 0);
		if (mc->un.mask & I2S_SELECT_LINEOUT)
			i2s_mute(sc->sc_line, 0);

		sc->sc_output_mask = mc->un.mask;
		return 0;

	case I2S_VOL_OUTPUT:
		(*sc->sc_setvolume)(sc, l, r);
		return 0;

	case I2S_MUTE:
		if (mc->type != AUDIO_MIXER_ENUM)
			return (EINVAL);

		sc->sc_mute = (mc->un.ord != 0);

		if (sc->sc_mute) {
			if (sc->sc_output_mask & I2S_SELECT_SPEAKER)
				i2s_mute(sc->sc_spkr, 1);
			if (sc->sc_output_mask & I2S_SELECT_HEADPHONE)
				i2s_mute(sc->sc_hp, 1);
			if (sc->sc_output_mask & I2S_SELECT_LINEOUT)
				i2s_mute(sc->sc_line, 1);
		} else {
			if (sc->sc_output_mask & I2S_SELECT_SPEAKER)
				i2s_mute(sc->sc_spkr, 0);
			if (sc->sc_output_mask & I2S_SELECT_HEADPHONE)
				i2s_mute(sc->sc_hp, 0);
			if (sc->sc_output_mask & I2S_SELECT_LINEOUT)
				i2s_mute(sc->sc_line, 0);
		}

		return (0);

	case I2S_BASS:
		if (sc->sc_setbass != NULL)
			(*sc->sc_setbass)(sc, l);
		return (0);

	case I2S_TREBLE:
		if (sc->sc_settreble != NULL)
			(*sc->sc_settreble)(sc, l);
		return (0);

	case I2S_INPUT_SELECT:
		/* no change necessary? */
		if (mc->un.mask == sc->sc_record_source)
			return 0;
		switch (mc->un.mask) {
		case I2S_SELECT_SPEAKER:
		case I2S_SELECT_HEADPHONE:
			/* XXX TO BE DONE */
			break;
		default: /* invalid argument */
			return EINVAL;
		}
		if (sc->sc_setinput != NULL)
			(*sc->sc_setinput)(sc, mc->un.mask);
		sc->sc_record_source = mc->un.mask;
		return 0;

	case I2S_VOL_INPUT:
		/* XXX TO BE DONE */
		return 0;
	}

	return ENXIO;
}

int
i2s_get_port(void *h, mixer_ctrl_t *mc)
{
	struct i2s_softc *sc = h;

	DPRINTF(("i2s_get_port dev = %d, type = %d\n", mc->dev, mc->type));

	switch (mc->dev) {
	case I2S_OUTPUT_SELECT:
		mc->un.mask = sc->sc_output_mask;
		return 0;

	case I2S_VOL_OUTPUT:
		mc->un.value.level[AUDIO_MIXER_LEVEL_LEFT] = sc->sc_vol_l;
		mc->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] = sc->sc_vol_r;
		return 0;

	case I2S_MUTE:
		mc->un.ord = sc->sc_mute;
		return (0);

	case I2S_INPUT_SELECT:
		mc->un.mask = sc->sc_record_source;
		return 0;

	case I2S_BASS:
		if (mc->un.value.num_channels != 1)
			return ENXIO;
		mc->un.value.level[AUDIO_MIXER_LEVEL_MONO] = sc->sc_bass;
		return 0;

	case I2S_TREBLE:
		if (mc->un.value.num_channels != 1)
			return ENXIO;
		mc->un.value.level[AUDIO_MIXER_LEVEL_MONO] = sc->sc_treble;
		return 0;

	case I2S_VOL_INPUT:
		/* XXX TO BE DONE */
		mc->un.value.level[AUDIO_MIXER_LEVEL_LEFT] = 0;
		mc->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] = 0;
		return 0;

	default:
		return ENXIO;
	}

	return 0;
}

int
i2s_query_devinfo(void *h, mixer_devinfo_t *dip)
{
	struct i2s_softc *sc = h;
	int n = 0;

	switch (dip->index) {

	case I2S_OUTPUT_SELECT:
		dip->mixer_class = I2S_OUTPUT_CLASS;
		strlcpy(dip->label.name, AudioNselect, sizeof(dip->label.name));
		dip->type = AUDIO_MIXER_SET;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->un.s.member[n].label.name, AudioNspeaker,
		    sizeof(dip->un.s.member[n].label.name));
		dip->un.s.member[n++].mask = I2S_SELECT_SPEAKER;
		if (sc->sc_hp) {
			strlcpy(dip->un.s.member[n].label.name,
			    AudioNheadphone,
			    sizeof(dip->un.s.member[n].label.name));
			dip->un.s.member[n++].mask = I2S_SELECT_HEADPHONE;
		}
		if (sc->sc_line) {
			strlcpy(dip->un.s.member[n].label.name,	AudioNline,
			    sizeof(dip->un.s.member[n].label.name));
			dip->un.s.member[n++].mask = I2S_SELECT_LINEOUT;
		}
		dip->un.s.num_mem = n;
		return 0;

	case I2S_VOL_OUTPUT:
		dip->mixer_class = I2S_OUTPUT_CLASS;
		strlcpy(dip->label.name, AudioNmaster, sizeof(dip->label.name));
		dip->type = AUDIO_MIXER_VALUE;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = I2S_MUTE;
		dip->un.v.num_channels = 2;
		dip->un.v.delta = 8;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof(dip->un.v.units.name));
		return 0;

	case I2S_MUTE:
		dip->mixer_class = I2S_OUTPUT_CLASS;
		dip->prev = I2S_VOL_OUTPUT;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmute, sizeof(dip->label.name));
		dip->type = AUDIO_MIXER_ENUM;
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNoff,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNon,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		return (0);
 
	case I2S_INPUT_SELECT:
		dip->mixer_class = I2S_RECORD_CLASS;
		strlcpy(dip->label.name, AudioNsource, sizeof(dip->label.name));
		dip->type = AUDIO_MIXER_SET;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		dip->un.s.num_mem = 2;
		strlcpy(dip->un.s.member[0].label.name, AudioNmicrophone,
		    sizeof(dip->un.s.member[0].label.name));
		dip->un.s.member[0].mask = I2S_SELECT_SPEAKER;
		strlcpy(dip->un.s.member[1].label.name, AudioNline,
		    sizeof(dip->un.s.member[1].label.name));
		dip->un.s.member[1].mask = I2S_SELECT_HEADPHONE;
		return 0;

	case I2S_VOL_INPUT:
		dip->mixer_class = I2S_RECORD_CLASS;
		strlcpy(dip->label.name, AudioNrecord, sizeof(dip->label.name));
		dip->type = AUDIO_MIXER_VALUE;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof(dip->un.v.units.name));
		return 0;

	case I2S_OUTPUT_CLASS:
		dip->mixer_class = I2S_OUTPUT_CLASS;
		strlcpy(dip->label.name, AudioCoutputs,
		    sizeof(dip->label.name));
		dip->type = AUDIO_MIXER_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		return 0;

	case I2S_RECORD_CLASS:
		dip->mixer_class = I2S_RECORD_CLASS;
		strlcpy(dip->label.name, AudioCrecord, sizeof(dip->label.name));
		dip->type = AUDIO_MIXER_CLASS;
		dip->next = dip->prev = AUDIO_MIXER_LAST;
		return 0;

	case I2S_BASS:
		if (sc->sc_setbass == NULL)
			return (ENXIO);
		dip->mixer_class = I2S_OUTPUT_CLASS;
		strlcpy(dip->label.name, AudioNbass, sizeof(dip->label.name));
		dip->type = AUDIO_MIXER_VALUE;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		dip->un.v.num_channels = 1;
		return (0);

	case I2S_TREBLE:
		if (sc->sc_settreble == NULL)
			return (ENXIO);
		dip->mixer_class = I2S_OUTPUT_CLASS;
		strlcpy(dip->label.name, AudioNtreble, sizeof(dip->label.name));
		dip->type = AUDIO_MIXER_VALUE;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		dip->un.v.num_channels = 1;
		return (0);
	}

	return ENXIO;
}

size_t
i2s_round_buffersize(void *h, int dir, size_t size)
{
	if (size > 65536)
		size = 65536;
	return size;
}

int
i2s_trigger_output(void *h, void *start, void *end, int bsize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct i2s_softc *sc = h;
	struct i2s_dma *p;
	struct dbdma_command *cmd = sc->sc_odmacmd;
	vaddr_t spa, pa, epa;
	int c;

	DPRINTF(("trigger_output %p %p 0x%x\n", start, end, bsize));

	for (p = sc->sc_dmas; p && p->addr != start; p = p->next)
		;
	if (!p)
		return -1;

	sc->sc_ointr = intr;
	sc->sc_oarg = arg;
	sc->sc_odmap = sc->sc_odmacmd;

	spa = p->segs[0].ds_addr;
	c = DBDMA_CMD_OUT_MORE;
	for (pa = spa, epa = spa + (end - start);
	    pa < epa; pa += bsize, cmd++) {

		if (pa + bsize == epa)
			c = DBDMA_CMD_OUT_LAST;

		DBDMA_BUILD(cmd, c, 0, bsize, pa, DBDMA_INT_ALWAYS,
			DBDMA_WAIT_NEVER, DBDMA_BRANCH_NEVER);
	}

	DBDMA_BUILD(cmd, DBDMA_CMD_NOP, 0, 0, 0,
		DBDMA_INT_NEVER, DBDMA_WAIT_NEVER, DBDMA_BRANCH_ALWAYS);
	dbdma_st32(&cmd->d_cmddep, sc->sc_odbdma->d_paddr);

	dbdma_start(sc->sc_odma, sc->sc_odbdma);

	return 0;
}

int
i2s_trigger_input(void *h, void *start, void *end, int bsize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct i2s_softc *sc = h;
	struct i2s_dma *p;
	struct dbdma_command *cmd = sc->sc_idmacmd;
	vaddr_t spa, pa, epa;
	int c;

	DPRINTF(("trigger_input %p %p 0x%x\n", start, end, bsize));

	for (p = sc->sc_dmas; p && p->addr != start; p = p->next)
		;
	if (!p)
		return -1;

	sc->sc_iintr = intr;
	sc->sc_iarg = arg;
	sc->sc_idmap = sc->sc_idmacmd;
   
	spa = p->segs[0].ds_addr;
	c = DBDMA_CMD_IN_MORE;
	for (pa = spa, epa = spa + (end - start);
	    pa < epa; pa += bsize, cmd++) {

		if (pa + bsize == epa)
			c = DBDMA_CMD_IN_LAST;

		DBDMA_BUILD(cmd, c, 0, bsize, pa, DBDMA_INT_ALWAYS,
			DBDMA_WAIT_NEVER, DBDMA_BRANCH_NEVER);
	}

	DBDMA_BUILD(cmd, DBDMA_CMD_NOP, 0, 0, 0,
		DBDMA_INT_NEVER, DBDMA_WAIT_NEVER, DBDMA_BRANCH_ALWAYS);
	dbdma_st32(&cmd->d_cmddep, sc->sc_idbdma->d_paddr);
		
	dbdma_start(sc->sc_idma, sc->sc_idbdma);
		
	return 0;
}


/* rate = fs = LRCLK
 * SCLK = 64*LRCLK (I2S)
 * MCLK = 256fs (typ. -- changeable)
 * MCLK = clksrc / mdiv
 *  SCLK = MCLK / sdiv
 * rate = SCLK / 64    ( = LRCLK = fs)
 */
int
i2s_set_rate(struct i2s_softc *sc, int rate)
{
	u_int reg = 0;
	int MCLK;
	int clksrc, mdiv, sdiv;
	int mclk_fs;
	int timo;

	/* sanify */
	if (rate > (48000 + 44100) / 2)
		rate = 48000;
	else
		rate = 44100;

	switch (rate) {
	case 44100:
		clksrc = 45158400;		/* 45MHz */
		reg = CLKSRC_45MHz;
		mclk_fs = 256;
		break;

	case 48000:
		clksrc = 49152000;		/* 49MHz */
		reg = CLKSRC_49MHz;
		mclk_fs = 256;
		break;

	default:
		return EINVAL;
	}

	MCLK = rate * mclk_fs;
	mdiv = clksrc / MCLK;			/* 4 */
	sdiv = mclk_fs / 64;			/* 4 */

	switch (mdiv) {
	case 1:
		reg |= MCLK_DIV1;
		break;
	case 3:
		reg |= MCLK_DIV3;
		break;
	case 5:
		reg |= MCLK_DIV5;
		break;
	default:
		reg |= ((mdiv / 2 - 1) << 24) & 0x1f000000;
		break;
	}

	switch (sdiv) {
	case 1:
		reg |= SCLK_DIV1;
		break;
	case 3:
		reg |= SCLK_DIV3;
		break;
	default:
		reg |= ((sdiv / 2 - 1) << 20) & 0x00f00000;
		break;
	}

	reg |= SCLK_MASTER;	/* XXX master mode */

	reg |= SERIAL_64x;

	if (sc->sc_rate == rate)
		return (0);

	/* stereo input and output */
	DPRINTF(("I2SSetDataWordSizeReg 0x%08x -> 0x%08x\n",
	    in32rb(sc->sc_reg + I2S_WORDSIZE), 0x02000200));
	out32rb(sc->sc_reg + I2S_WORDSIZE, 0x02000200);

	/* Clear CLKSTOPPEND */
	out32rb(sc->sc_reg + I2S_INT, I2S_INT_CLKSTOPPEND);

	macobio_disable(I2SClockOffset, I2S0CLKEN);

	/* Wait until clock is stopped */
	for (timo = 50; timo > 0; timo--) {
		if (in32rb(sc->sc_reg + I2S_INT) & I2S_INT_CLKSTOPPEND)
			goto done;
		delay(10);
	}

	printf("i2s_set_rate: timeout\n");

done:
	DPRINTF(("I2SSetSerialFormatReg 0x%x -> 0x%x\n",
	    in32rb(sc->sc_reg + I2S_FORMAT), reg));
	out32rb(sc->sc_reg + I2S_FORMAT, reg);

	macobio_enable(I2SClockOffset, I2S0CLKEN);

	sc->sc_rate = rate;

	return 0;
}

void
i2s_mute(u_int offset, int mute)
{
	if (offset == 0)
		return;

	DPRINTF(("gpio: %x, %d -> ", offset, macobio_read(offset) & GPIO_DATA));

	/* 0 means mute */
	if (mute == (macobio_read(offset) & GPIO_DATA))
		macobio_write(offset, !mute | GPIO_DDR_OUTPUT);

	DPRINTF(("%d\n", macobio_read(offset) & GPIO_DATA));
}

int
i2s_cint(void *v)
{
	struct i2s_softc *sc = v;
	u_int sense;

	sc->sc_output_mask = 0;
	i2s_mute(sc->sc_spkr, 1);
	i2s_mute(sc->sc_hp, 1);
	i2s_mute(sc->sc_line, 1);

	if (sc->sc_hp_detect)
		sense = macobio_read(sc->sc_hp_detect);
	else
		sense = !sc->sc_hp_active << 1;
	DPRINTF(("headphone detect = 0x%x\n", sense));

	if (((sense & 0x02) >> 1) == sc->sc_hp_active) {
		DPRINTF(("headphone is inserted\n"));
		sc->sc_output_mask |= I2S_SELECT_HEADPHONE;
		if (!sc->sc_mute)
			i2s_mute(sc->sc_hp, 0);
	} else {
		DPRINTF(("headphone is NOT inserted\n"));
	}

	if (sc->sc_line_detect)
		sense = macobio_read(sc->sc_line_detect);
	else
		sense = !sc->sc_line_active << 1;
	DPRINTF(("lineout detect = 0x%x\n", sense));

	if (((sense & 0x02) >> 1) == sc->sc_line_active) {
		DPRINTF(("lineout is inserted\n"));
		sc->sc_output_mask |= I2S_SELECT_LINEOUT;
		if (!sc->sc_mute)
			i2s_mute(sc->sc_line, 0);
	} else {
		DPRINTF(("lineout is NOT inserted\n"));
	}

	if (sc->sc_output_mask == 0) {
		sc->sc_output_mask |= I2S_SELECT_SPEAKER;
		if (!sc->sc_mute)
			i2s_mute(sc->sc_spkr, 0);
	}

	return 1;
}

u_int
i2s_gpio_offset(struct i2s_softc *sc, char *name, int *irq)
{
	u_int32_t reg[2];
	u_int32_t intr[2];
	int gpio;

	if (OF_getprop(sc->sc_node, name, &gpio,
            sizeof(gpio)) != sizeof(gpio) ||
	    OF_getprop(gpio, "reg", &reg[0],
	    sizeof(reg[0])) != sizeof(reg[0]) ||
	    OF_getprop(OF_parent(gpio), "reg", &reg[1],
	    sizeof(reg[1])) != sizeof(reg[1]))
		return (0);

	if (irq && OF_getprop(gpio, "interrupts",
	    intr, sizeof(intr)) == sizeof(intr)) {
		*irq = intr[0];
	}

	return (reg[0] + reg[1]);
}

void
i2s_gpio_init(struct i2s_softc *sc, int node, struct device *parent)
{
	int gpio;
	int hp_detect_intr = -1, line_detect_intr = -1;

	sc->sc_spkr = i2s_gpio_offset(sc, "platform-amp-mute", NULL);
	sc->sc_hp = i2s_gpio_offset(sc, "platform-headphone-mute", NULL);
	sc->sc_hp_detect = i2s_gpio_offset(sc, "platform-headphone-detect",
	    &hp_detect_intr);
	sc->sc_line = i2s_gpio_offset(sc, "platform-lineout-mute", NULL);
	sc->sc_line_detect = i2s_gpio_offset(sc, "platform-lineout-detect",
	    &line_detect_intr);
	sc->sc_hw_reset = i2s_gpio_offset(sc, "platform-hw-reset", NULL);

	gpio = OF_getnodebyname(OF_parent(node), "gpio");
	DPRINTF((" /gpio 0x%x\n", gpio));
	for (gpio = OF_child(gpio); gpio; gpio = OF_peer(gpio)) {
		char name[64], audio_gpio[64];
		int intr[2];
		uint32_t reg;

		reg = 0;
		bzero(name, sizeof name);
		bzero(audio_gpio, sizeof audio_gpio);
		OF_getprop(gpio, "name", name, sizeof name);
		OF_getprop(gpio, "audio-gpio", audio_gpio, sizeof audio_gpio);
		if (OF_getprop(gpio, "reg", &reg, sizeof(reg)) == -1)
			OF_getprop(gpio, "AAPL,address", &reg, sizeof(reg));

		if (reg > sc->sc_baseaddr)
			reg = (reg - sc->sc_baseaddr);

		/* gpio5 */
		if (sc->sc_hp == 0 && strcmp(audio_gpio, "headphone-mute") == 0)
			sc->sc_hp = reg;

		/* gpio6 */
		if (sc->sc_spkr == 0 && strcmp(audio_gpio, "amp-mute") == 0)
			sc->sc_spkr = reg;

		/* extint-gpio15 */
		if (sc->sc_hp_detect == 0 &&
		    strcmp(audio_gpio, "headphone-detect") == 0) {
			sc->sc_hp_detect = reg;
			OF_getprop(gpio, "audio-gpio-active-state",
			    &sc->sc_hp_active, 4);
			OF_getprop(gpio, "interrupts", intr, 8);
			hp_detect_intr = intr[0];
		}

		/* gpio11 (keywest-11) */
		if (sc->sc_hw_reset == 0 &&
		    strcmp(audio_gpio, "audio-hw-reset") == 0)
			sc->sc_hw_reset = reg;
	}
	DPRINTF((" amp-mute 0x%x\n", sc->sc_spkr));
	DPRINTF((" headphone-mute 0x%x\n", sc->sc_hp));
	DPRINTF((" headphone-detect 0x%x\n", sc->sc_hp_detect));
	DPRINTF((" headphone-detect active %x\n", sc->sc_hp_active));
	DPRINTF((" headphone-detect intr %x\n", hp_detect_intr));
	DPRINTF((" lineout-mute 0x%x\n", sc->sc_line));
	DPRINTF((" lineout-detect 0x%x\n", sc->sc_line_detect));
	DPRINTF((" lineout-detect active 0x%x\n", sc->sc_line_active));
	DPRINTF((" lineout-detect intr 0x%x\n", line_detect_intr));
	DPRINTF((" audio-hw-reset 0x%x\n", sc->sc_hw_reset));

	if (hp_detect_intr != -1)
		mac_intr_establish(parent, hp_detect_intr, IST_EDGE,
		    IPL_AUDIO | IPL_MPSAFE, i2s_cint, sc, sc->sc_dev.dv_xname);

	if (line_detect_intr != -1)
		mac_intr_establish(parent, line_detect_intr, IST_EDGE,
		    IPL_AUDIO | IPL_MPSAFE, i2s_cint, sc, sc->sc_dev.dv_xname);

	/* Enable headphone interrupt? */
	macobio_write(sc->sc_hp_detect, 0x80);

	/* Update headphone status. */
	i2s_cint(sc);
}

void *
i2s_allocm(void *h, int dir, size_t size, int type, int flags)
{
	struct i2s_softc *sc = h;
	struct i2s_dma *p;
	int error;

	if (size > I2S_DMALIST_MAX * I2S_DMASEG_MAX)
		return (NULL);

	p = malloc(sizeof(*p), type, flags | M_ZERO);
	if (!p)
		return (NULL);

	/* convert to the bus.h style, not used otherwise */
	if (flags & M_NOWAIT)
		flags = BUS_DMA_NOWAIT;

	p->size = size;
	if ((error = bus_dmamem_alloc(sc->sc_dmat, p->size, NBPG, 0, p->segs,
	    1, &p->nsegs, flags)) != 0) {
		printf("%s: unable to allocate dma, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		free(p, type, sizeof *p);
		return NULL;
	}

	if ((error = bus_dmamem_map(sc->sc_dmat, p->segs, p->nsegs, p->size,
	    &p->addr, flags | BUS_DMA_COHERENT)) != 0) {
		printf("%s: unable to map dma, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		bus_dmamem_free(sc->sc_dmat, p->segs, p->nsegs);
		free(p, type, sizeof *p);
		return NULL;
	}

	if ((error = bus_dmamap_create(sc->sc_dmat, p->size, 1,
	    p->size, 0, flags, &p->map)) != 0) {
		printf("%s: unable to create dma map, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		bus_dmamem_unmap(sc->sc_dmat, p->addr, size);
		bus_dmamem_free(sc->sc_dmat, p->segs, p->nsegs);
		free(p, type, sizeof *p);
		return NULL;
	}

	if ((error = bus_dmamap_load(sc->sc_dmat, p->map, p->addr, p->size,
	    NULL, flags)) != 0) {
		printf("%s: unable to load dma map, error = %d\n",
		    sc->sc_dev.dv_xname, error);
		bus_dmamap_destroy(sc->sc_dmat, p->map);
		bus_dmamem_unmap(sc->sc_dmat, p->addr, size);
		bus_dmamem_free(sc->sc_dmat, p->segs, p->nsegs);
		free(p, type, sizeof *p);
		return NULL;
	}

	p->next = sc->sc_dmas;
	sc->sc_dmas = p;

	return p->addr;
}

#define reset_active 0

int
deq_reset(struct i2s_softc *sc)
{
	if (sc->sc_hw_reset == 0)
		return (-1);

	macobio_write(sc->sc_hw_reset, !reset_active | GPIO_DDR_OUTPUT);
	delay(1000000);

	macobio_write(sc->sc_hw_reset, reset_active | GPIO_DDR_OUTPUT);
	delay(1);

	macobio_write(sc->sc_hw_reset, !reset_active | GPIO_DDR_OUTPUT);
	delay(10000);

	return (0);
}
