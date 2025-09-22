/*	$OpenBSD: ce4231.c,v 1.42 2025/07/03 11:01:41 jsg Exp $	*/

/*
 * Copyright (c) 1999 Jason L. Wright (jason@thought.net)
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
 * Driver for CS4231 based audio found in some sun4u systems (cs4231)
 * based on ideas from the S/Linux project and the NetBSD project.
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/malloc.h>

#include <machine/cpu.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <sparc64/dev/ebusreg.h>
#include <sparc64/dev/ebusvar.h>
#include <sparc64/dev/ce4231var.h>

/* AD1418 provides basic registers, CS4231 extends with more */
#include <dev/ic/ad1848reg.h>
#include <dev/ic/cs4231reg.h>

/* Mixer classes and mixer knobs */
#define CSAUDIO_INPUT_CLASS	0
#define CSAUDIO_OUTPUT_CLASS	1
#define CSAUDIO_RECORD_CLASS	2
#define CSAUDIO_DAC_LVL		3
#define CSAUDIO_DAC_MUTE	4
#define CSAUDIO_OUTPUTS		5
#define CSAUDIO_CD_LVL		6
#define CSAUDIO_CD_MUTE		7
#define CSAUDIO_LINE_IN_LVL	8
#define CSAUDIO_LINE_IN_MUTE	9
#define CSAUDIO_MONITOR_LVL	10
#define CSAUDIO_MONITOR_MUTE	11
#define CSAUDIO_REC_LVL		12
#define CSAUDIO_RECORD_SOURCE	13
#define CSAUDIO_MIC_PREAMP	14

/* Recording sources */
#define REC_PORT_LINE	0
#define REC_PORT_CD	1
#define REC_PORT_MIC	2
#define REC_PORT_MIX	3

/* Output ports. */
#define OUT_PORT_LINE	0x1
#define OUT_PORT_HP	0x2
#define OUT_PORT_SPKR	0x4

/* Bits on the ADC reg that determine recording source */
#define CS_REC_SRC_BITS 0xc0

#ifdef AUDIO_DEBUG
#define	DPRINTF(x)	printf x
#else
#define	DPRINTF(x)
#endif

#define	CS_TIMEOUT	90000

/* Read/write CS4231 direct registers */
#define CS_WRITE(sc,r,v)	\
    bus_space_write_1((sc)->sc_bustag, (sc)->sc_cshandle, (r) << 2, (v))
#define	CS_READ(sc,r)		\
    bus_space_read_1((sc)->sc_bustag, (sc)->sc_cshandle, (r) << 2)

/* Read/write EBDMA playback registers */
#define	P_WRITE(sc,r,v)		\
    bus_space_write_4((sc)->sc_bustag, (sc)->sc_pdmahandle, (r), (v))
#define	P_READ(sc,r)		\
    bus_space_read_4((sc)->sc_bustag, (sc)->sc_pdmahandle, (r))

/* Read/write EBDMA capture registers */
#define	C_WRITE(sc,r,v)		\
    bus_space_write_4((sc)->sc_bustag, (sc)->sc_cdmahandle, (r), (v))
#define	C_READ(sc,r)		\
    bus_space_read_4((sc)->sc_bustag, (sc)->sc_cdmahandle, (r))

int	ce4231_match(struct device *, void *, void *);
void	ce4231_attach(struct device *, struct device *, void *);
int	ce4231_cintr(void *);
int	ce4231_pintr(void *);

int	ce4231_set_speed(struct ce4231_softc *, u_long *);

void	ce4231_set_outputs(struct ce4231_softc *, int);
int	ce4231_get_outputs(struct ce4231_softc *);

void		ce4231_write(struct ce4231_softc *, u_int8_t, u_int8_t);
u_int8_t	ce4231_read(struct ce4231_softc *, u_int8_t);

/* Audio interface */
int	ce4231_open(void *, int);
void	ce4231_close(void *);
int	ce4231_set_params(void *, int, int, struct audio_params *,
    struct audio_params *);
int	ce4231_round_blocksize(void *, int);
int	ce4231_commit_settings(void *);
int	ce4231_halt_output(void *);
int	ce4231_halt_input(void *);
int	ce4231_set_port(void *, mixer_ctrl_t *);
int	ce4231_get_port(void *, mixer_ctrl_t *);
int	ce4231_query_devinfo(void *addr, mixer_devinfo_t *);
void *	ce4231_alloc(void *, int, size_t, int, int);
void	ce4231_free(void *, void *, int);
int	ce4231_trigger_output(void *, void *, void *, int,
    void (*intr)(void *), void *arg, struct audio_params *);
int	ce4231_trigger_input(void *, void *, void *, int,
    void (*intr)(void *), void *arg, struct audio_params *);

const struct audio_hw_if ce4231_sa_hw_if = {
	.open = ce4231_open,
	.close = ce4231_close,
	.set_params = ce4231_set_params,
	.round_blocksize = ce4231_round_blocksize,
	.commit_settings = ce4231_commit_settings,
	.halt_output = ce4231_halt_output,
	.halt_input = ce4231_halt_input,
	.set_port = ce4231_set_port,
	.get_port = ce4231_get_port,
	.query_devinfo = ce4231_query_devinfo,
	.allocm = ce4231_alloc,
	.freem = ce4231_free,
	.trigger_output = ce4231_trigger_output,
	.trigger_input = ce4231_trigger_input,
};

const struct cfattach audioce_ca = {
	sizeof (struct ce4231_softc), ce4231_match, ce4231_attach
};

struct cfdriver audioce_cd = {
	NULL, "audioce", DV_DULL
};

int
ce4231_match(struct device *parent, void *vcf, void *aux)
{
	struct ebus_attach_args *ea = aux;

	if (!strcmp("SUNW,CS4231", ea->ea_name) ||
	    !strcmp("audio", ea->ea_name))
		return (1);
	return (0);
}

void    
ce4231_attach(struct device *parent, struct device *self, void *aux)
{
	struct ebus_attach_args *ea = aux;
	struct ce4231_softc *sc = (struct ce4231_softc *)self;
	mixer_ctrl_t cp;
	int node;

	node = ea->ea_node;

	sc->sc_last_format = 0xffffffff;

	/* Pass on the bus tags */
	sc->sc_bustag = ea->ea_memtag;
	sc->sc_dmatag = ea->ea_dmatag;

	/* Make sure things are sane. */
	if (ea->ea_nintrs != 2) {
		printf(": expected 2 interrupts, got %d\n", ea->ea_nintrs);
		return;
	}
	if (ea->ea_nregs != 4) {
		printf(": expected 4 register set, got %d\n",
		    ea->ea_nregs);
		return;
	}

	sc->sc_cih = bus_intr_establish(sc->sc_bustag, ea->ea_intrs[0],
	    IPL_AUDIO, BUS_INTR_ESTABLISH_MPSAFE, ce4231_cintr,
	    sc, self->dv_xname);
	if (sc->sc_cih == NULL) {
		printf(": couldn't establish capture interrupt\n");
		return;
	}
	sc->sc_pih = bus_intr_establish(sc->sc_bustag, ea->ea_intrs[1],
	    IPL_AUDIO, BUS_INTR_ESTABLISH_MPSAFE, ce4231_pintr,
	    sc, self->dv_xname);
	if (sc->sc_pih == NULL) {
		printf(": couldn't establish play interrupt1\n");
		return;
	}

	/* XXX what if prom has already mapped?! */

	if (ebus_bus_map(sc->sc_bustag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[0]), ea->ea_regs[0].size,
	    BUS_SPACE_MAP_LINEAR, 0, &sc->sc_cshandle) != 0) {
		printf(": couldn't map cs4231 registers\n");
		return;
	}

	if (ebus_bus_map(sc->sc_bustag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[1]), ea->ea_regs[1].size,
	    BUS_SPACE_MAP_LINEAR, 0, &sc->sc_pdmahandle) != 0) {
		printf(": couldn't map dma1 registers\n");
		return;
	}

	if (ebus_bus_map(sc->sc_bustag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[2]), ea->ea_regs[2].size,
	    BUS_SPACE_MAP_LINEAR, 0, &sc->sc_cdmahandle) != 0) {
		printf(": couldn't map dma2 registers\n");
		return;
	}

	if (ebus_bus_map(sc->sc_bustag, 0,
	    EBUS_PADDR_FROM_REG(&ea->ea_regs[3]), ea->ea_regs[3].size,
	    BUS_SPACE_MAP_LINEAR, 0, &sc->sc_auxhandle) != 0) {
		printf(": couldn't map aux registers\n");
		return;
	}

	printf(": nvaddrs %d\n", ea->ea_nvaddrs);

	audio_attach_mi(&ce4231_sa_hw_if, sc, NULL, &sc->sc_dev);

	/* Enable mode 2. */
	ce4231_write(sc, SP_MISC_INFO, ce4231_read(sc, SP_MISC_INFO) | MODE2);

	/* Attenuate DAC, CD and line-in.  -22.5 dB for all. */
	cp.dev = CSAUDIO_DAC_LVL;
	cp.type = AUDIO_MIXER_VALUE;
	cp.un.value.num_channels = 2;
	cp.un.value.level[AUDIO_MIXER_LEVEL_LEFT] = 195;
	cp.un.value.level[AUDIO_MIXER_LEVEL_RIGHT] = 195;
	ce4231_set_port(sc, &cp);

	cp.dev = CSAUDIO_CD_LVL;
	cp.un.value.level[AUDIO_MIXER_LEVEL_LEFT] = 135;
	cp.un.value.level[AUDIO_MIXER_LEVEL_RIGHT] = 135;
	ce4231_set_port(sc, &cp);

	cp.dev = CSAUDIO_LINE_IN_LVL;
	ce4231_set_port(sc, &cp);

	/* Unmute DAC, CD and line-in */
	cp.dev = CSAUDIO_DAC_MUTE;
	cp.type = AUDIO_MIXER_ENUM;
	cp.un.ord = 0;
	ce4231_set_port(sc, &cp);

	cp.dev = CSAUDIO_CD_MUTE;
	ce4231_set_port(sc, &cp);

	cp.dev = CSAUDIO_LINE_IN_MUTE;
	ce4231_set_port(sc, &cp);

	/* XXX get real burst... */
	sc->sc_burst = EBDCSR_BURST_8;
}

/*
 * Write to one of the indexed registers of cs4231.
 */
void
ce4231_write(struct ce4231_softc *sc, u_int8_t r, u_int8_t v)
{
	CS_WRITE(sc, AD1848_IADDR, r);
	CS_WRITE(sc, AD1848_IDATA, v);
}

/*
 * Read from one of the indexed registers of cs4231.
 */
u_int8_t
ce4231_read(struct ce4231_softc *sc, u_int8_t r)
{
	CS_WRITE(sc, AD1848_IADDR, r);
	return (CS_READ(sc, AD1848_IDATA));
}

int
ce4231_set_speed(struct ce4231_softc *sc, u_long *argp)
{
	/*
	 * The available speeds are in the following table. Keep the speeds in
	 * the increasing order.
	 */
	typedef struct {
		int speed;
		u_char bits;
	} speed_struct;
	u_long arg = *argp;

	static speed_struct speed_table[] = {
		{5510,	(0 << 1) | CLOCK_XTAL2},
		{5510,	(0 << 1) | CLOCK_XTAL2},
		{6620,	(7 << 1) | CLOCK_XTAL2},
		{8000,	(0 << 1) | CLOCK_XTAL1},
		{9600,	(7 << 1) | CLOCK_XTAL1},
		{11025,	(1 << 1) | CLOCK_XTAL2},
		{16000,	(1 << 1) | CLOCK_XTAL1},
		{18900,	(2 << 1) | CLOCK_XTAL2},
		{22050,	(3 << 1) | CLOCK_XTAL2},
		{27420,	(2 << 1) | CLOCK_XTAL1},
		{32000,	(3 << 1) | CLOCK_XTAL1},
		{33075,	(6 << 1) | CLOCK_XTAL2},
		{33075,	(4 << 1) | CLOCK_XTAL2},
		{44100,	(5 << 1) | CLOCK_XTAL2},
		{48000,	(6 << 1) | CLOCK_XTAL1},
	};

	int i, n, selected = -1;

	n = sizeof(speed_table) / sizeof(speed_struct);

	if (arg < speed_table[0].speed)
		selected = 0;
	if (arg > speed_table[n - 1].speed)
		selected = n - 1;

	for (i = 1; selected == -1 && i < n; i++) {
		if (speed_table[i].speed == arg)
			selected = i;
		else if (speed_table[i].speed > arg) {
			int diff1, diff2;

			diff1 = arg - speed_table[i - 1].speed;
			diff2 = speed_table[i].speed - arg;
			if (diff1 < diff2)
				selected = i - 1;
			else
				selected = i;
		}
	}

	if (selected == -1)
		selected = 3;

	sc->sc_speed_bits = speed_table[selected].bits;
	sc->sc_need_commit = 1;
	*argp = speed_table[selected].speed;

	return (0);
}

/*
 * Audio interface functions
 */
int
ce4231_open(void *addr, int flags)
{
	struct ce4231_softc *sc = addr;
	int tries;

	DPRINTF(("ce4231_open\n"));

	if (sc->sc_open)
		return (EBUSY);

	sc->sc_open = 1;
	sc->sc_rintr = 0;
	sc->sc_rarg = 0;
	sc->sc_pintr = 0;
	sc->sc_parg = 0;

	P_WRITE(sc, EBDMA_DCSR, EBDCSR_RESET);
	C_WRITE(sc, EBDMA_DCSR, EBDCSR_RESET);
	P_WRITE(sc, EBDMA_DCSR, sc->sc_burst);
	C_WRITE(sc, EBDMA_DCSR, sc->sc_burst);

	DELAY(20);

	for (tries = CS_TIMEOUT;
	     tries && CS_READ(sc, AD1848_IADDR) == SP_IN_INIT; tries--)
		DELAY(10);
	if (tries == 0)
		printf("%s: timeout waiting for reset\n", sc->sc_dev.dv_xname);

	ce4231_write(sc, SP_PIN_CONTROL,
	    ce4231_read(sc, SP_PIN_CONTROL) | INTERRUPT_ENABLE);

	return (0);
}

void
ce4231_close(void *addr)
{
	struct ce4231_softc *sc = addr;

	ce4231_halt_input(sc);
	ce4231_halt_output(sc);
	ce4231_write(sc, SP_PIN_CONTROL,
	    ce4231_read(sc, SP_PIN_CONTROL) & (~INTERRUPT_ENABLE));
	sc->sc_open = 0;
}

int
ce4231_set_params(void *addr, int setmode, int usemode, struct audio_params *p,
    struct audio_params *r)
{
	struct ce4231_softc *sc = (struct ce4231_softc *)addr;
	int err, bits, enc = p->encoding;

	if (p->precision > 16)
		p->precision = 16;
	switch (enc) {
	case AUDIO_ENCODING_ULAW:
		p->precision = 8;
		bits = FMT_ULAW >> 5;
		break;
	case AUDIO_ENCODING_ALAW:
		p->precision = 8;
		bits = FMT_ALAW >> 5;
		break;
	case AUDIO_ENCODING_SLINEAR_LE:
		p->precision = 16;
		bits = FMT_TWOS_COMP >> 5;
		break;
	case AUDIO_ENCODING_SLINEAR_BE:
		p->precision = 16;
		bits = FMT_TWOS_COMP_BE >> 5;
		break;
	case AUDIO_ENCODING_ULINEAR_LE:
	case AUDIO_ENCODING_ULINEAR_BE:
		p->precision = 8;
		bits = FMT_PCM8 >> 5;
		break;
	default:
		return (EINVAL);
	}

	if (p->channels > 2)
		p->channels = 2;

	err = ce4231_set_speed(sc, &p->sample_rate);
	if (err)
		return (err);

	p->bps = AUDIO_BPS(p->precision);
	r->bps = AUDIO_BPS(r->precision);
	p->msb = r->msb = 1;

	sc->sc_format_bits = bits;
	sc->sc_channels = p->channels;
	sc->sc_precision = p->precision;
	sc->sc_need_commit = 1;
	return (0);
}

int
ce4231_round_blocksize(void *addr, int blk)
{
	return ((blk + 3) & (-4));
}

int
ce4231_commit_settings(void *addr)
{
	struct ce4231_softc *sc = (struct ce4231_softc *)addr;
	int tries;
	u_int8_t r, fs;

	if (sc->sc_need_commit == 0)
		return (0);

	fs = sc->sc_speed_bits | (sc->sc_format_bits << 5);
	if (sc->sc_channels == 2)
		fs |= FMT_STEREO;

	if (sc->sc_last_format == fs) {
		sc->sc_need_commit = 0;
		return (0);
	}

	/* XXX: this code is called before DMA (this intrs) is stopped */
	mtx_enter(&audio_lock);

	r = ce4231_read(sc, SP_INTERFACE_CONFIG) | AUTO_CAL_ENABLE;
	CS_WRITE(sc, AD1848_IADDR, MODE_CHANGE_ENABLE);
	CS_WRITE(sc, AD1848_IADDR, MODE_CHANGE_ENABLE | SP_INTERFACE_CONFIG);
	CS_WRITE(sc, AD1848_IDATA, r);

	CS_WRITE(sc, AD1848_IADDR, MODE_CHANGE_ENABLE | SP_CLOCK_DATA_FORMAT);
	CS_WRITE(sc, AD1848_IDATA, fs);
	CS_READ(sc, AD1848_IDATA);
	CS_READ(sc, AD1848_IDATA);
	tries = CS_TIMEOUT;
	for (tries = CS_TIMEOUT;
	     tries && CS_READ(sc, AD1848_IADDR) == SP_IN_INIT; tries--)
		DELAY(10);
	if (tries == 0)
		printf("%s: timeout committing fspb\n", sc->sc_dev.dv_xname);

	CS_WRITE(sc, AD1848_IADDR, MODE_CHANGE_ENABLE | CS_REC_FORMAT);
	CS_WRITE(sc, AD1848_IDATA, fs);
	CS_READ(sc, AD1848_IDATA);
	CS_READ(sc, AD1848_IDATA);
	for (tries = CS_TIMEOUT;
	     tries && CS_READ(sc, AD1848_IADDR) == SP_IN_INIT; tries--)
		DELAY(10);
	if (tries == 0)
		printf("%s: timeout committing cdf\n", sc->sc_dev.dv_xname);

	CS_WRITE(sc, AD1848_IADDR, 0);
	for (tries = CS_TIMEOUT;
	     tries && CS_READ(sc, AD1848_IADDR) == SP_IN_INIT; tries--)
		DELAY(10);
	if (tries == 0)
		printf("%s: timeout waiting for !mce\n", sc->sc_dev.dv_xname);

	CS_WRITE(sc, AD1848_IADDR, SP_TEST_AND_INIT);
	for (tries = CS_TIMEOUT;
	     tries && CS_READ(sc, AD1848_IDATA) & AUTO_CAL_IN_PROG; tries--)
		DELAY(10);
	if (tries == 0)
		printf("%s: timeout waiting for autocalibration\n",
		    sc->sc_dev.dv_xname);

	mtx_leave(&audio_lock);

	sc->sc_need_commit = 0;
	return (0);
}

int
ce4231_halt_output(void *addr)
{
	struct ce4231_softc *sc = (struct ce4231_softc *)addr;

	P_WRITE(sc, EBDMA_DCSR,
	    P_READ(sc, EBDMA_DCSR) & ~EBDCSR_DMAEN);
	ce4231_write(sc, SP_INTERFACE_CONFIG,
	    ce4231_read(sc, SP_INTERFACE_CONFIG) & (~PLAYBACK_ENABLE));
	return (0);
}

int
ce4231_halt_input(void *addr)
{
	struct ce4231_softc *sc = (struct ce4231_softc *)addr;

	C_WRITE(sc, EBDMA_DCSR,
	    C_READ(sc, EBDMA_DCSR) & ~EBDCSR_DMAEN);
	ce4231_write(sc, SP_INTERFACE_CONFIG,
	    ce4231_read(sc, SP_INTERFACE_CONFIG) & (~CAPTURE_ENABLE));
	return (0);
}

void
ce4231_set_outputs(struct ce4231_softc *sc, int mask)
{
	u_int8_t val;

	val = ce4231_read(sc, CS_MONO_IO_CONTROL) & ~MONO_OUTPUT_MUTE;
	if (!(mask & OUT_PORT_SPKR))
		val |= MONO_OUTPUT_MUTE;
	ce4231_write(sc, CS_MONO_IO_CONTROL, val);

	val = ce4231_read(sc, SP_PIN_CONTROL) & ~(XCTL0_ENABLE | XCTL1_ENABLE);
	if (!(mask & OUT_PORT_LINE))
		val |= XCTL0_ENABLE;
	if (!(mask & OUT_PORT_HP))
		val |= XCTL1_ENABLE;
	ce4231_write(sc, SP_PIN_CONTROL, val);
}

int
ce4231_get_outputs(struct ce4231_softc *sc)
{
	int mask = 0;
	u_int8_t val;

	if (!(ce4231_read(sc, CS_MONO_IO_CONTROL) & MONO_OUTPUT_MUTE))
		mask |= OUT_PORT_SPKR;

	val = ce4231_read(sc, SP_PIN_CONTROL);
	if (!(val & XCTL0_ENABLE))
		mask |= OUT_PORT_LINE;
	if (!(val & XCTL1_ENABLE))
		mask |= OUT_PORT_HP;

	return (mask);
}

int
ce4231_set_port(void *addr, mixer_ctrl_t *cp)
{
	struct ce4231_softc *sc = (struct ce4231_softc *)addr;
	u_int8_t l, r;

	DPRINTF(("ce4231_set_port: dev=%d type=%d\n", cp->dev, cp->type));

	switch (cp->dev) {

	case CSAUDIO_DAC_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			return (EINVAL);
		l = ce4231_read(sc, SP_LEFT_OUTPUT_CONTROL) &
		    OUTPUT_ATTEN_MASK;
		r = ce4231_read(sc, SP_RIGHT_OUTPUT_CONTROL) &
		    OUTPUT_ATTEN_MASK;
		l |= (AUDIO_MAX_GAIN -
		    cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT]) >> 2;
		r |= (AUDIO_MAX_GAIN -
		    cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT]) >> 2;
		ce4231_write(sc, SP_LEFT_OUTPUT_CONTROL, l);
		ce4231_write(sc, SP_RIGHT_OUTPUT_CONTROL, r);
		break;
	case CSAUDIO_DAC_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			return (EINVAL);
		l = ce4231_read(sc, SP_LEFT_OUTPUT_CONTROL) & ~OUTPUT_MUTE;
		r = ce4231_read(sc, SP_RIGHT_OUTPUT_CONTROL) & ~OUTPUT_MUTE;
		if (cp->un.ord) {
			l |= OUTPUT_MUTE;
			r |= OUTPUT_MUTE;
		}
		ce4231_write(sc, SP_LEFT_OUTPUT_CONTROL, l);
		ce4231_write(sc, SP_RIGHT_OUTPUT_CONTROL, r);
		break;

	case CSAUDIO_OUTPUTS:
		if (cp->type != AUDIO_MIXER_SET)
			return (EINVAL);
		ce4231_set_outputs(sc, cp->un.mask);
		break;

	case CSAUDIO_CD_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			return (EINVAL);
		l = ce4231_read(sc, SP_LEFT_AUX1_CONTROL) &
		    AUX_INPUT_ATTEN_MASK;
		r = ce4231_read(sc, SP_RIGHT_AUX1_CONTROL) &
		    AUX_INPUT_ATTEN_MASK;
		l |= (AUDIO_MAX_GAIN -
		    cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT]) >> 3;
		r |= (AUDIO_MAX_GAIN -
		    cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT]) >> 3;
		ce4231_write(sc, SP_LEFT_AUX1_CONTROL, l);
		ce4231_write(sc, SP_RIGHT_AUX1_CONTROL, r);
		break;
	case CSAUDIO_CD_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			return (EINVAL);
		l = ce4231_read(sc, SP_LEFT_AUX1_CONTROL) & ~AUX_INPUT_MUTE;
		r = ce4231_read(sc, SP_RIGHT_AUX1_CONTROL) & ~AUX_INPUT_MUTE;
		if (cp->un.ord) {
			l |= AUX_INPUT_MUTE;
			r |= AUX_INPUT_MUTE;
		}
		ce4231_write(sc, SP_LEFT_AUX1_CONTROL, l);
		ce4231_write(sc, SP_RIGHT_AUX1_CONTROL, r);
		break;

	case CSAUDIO_LINE_IN_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			return (EINVAL);
		l = ce4231_read(sc, CS_LEFT_LINE_CONTROL) &
		    LINE_INPUT_ATTEN_MASK;
		r = ce4231_read(sc, CS_RIGHT_LINE_CONTROL) &
		    LINE_INPUT_ATTEN_MASK;
		l |= (AUDIO_MAX_GAIN -
		    cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT]) >> 3;
		r |= (AUDIO_MAX_GAIN -
		    cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT]) >> 3;
		ce4231_write(sc, CS_LEFT_LINE_CONTROL, l);
		ce4231_write(sc, CS_RIGHT_LINE_CONTROL, r);
		break;
	case CSAUDIO_LINE_IN_MUTE:
		l = ce4231_read(sc, CS_LEFT_LINE_CONTROL) & ~LINE_INPUT_MUTE;
		r = ce4231_read(sc, CS_RIGHT_LINE_CONTROL) & ~LINE_INPUT_MUTE;
		if (cp->un.ord) {
			l |= LINE_INPUT_MUTE;
			r |= LINE_INPUT_MUTE;
		}
		ce4231_write(sc, CS_LEFT_LINE_CONTROL, l);
		ce4231_write(sc, CS_RIGHT_LINE_CONTROL, r);
		break;

	case CSAUDIO_MONITOR_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			return (EINVAL);
		if (cp->un.value.num_channels != 1)
			return (EINVAL);
		l = ce4231_read(sc, SP_DIGITAL_MIX) & ~MIX_ATTEN_MASK;
		l |= (AUDIO_MAX_GAIN -
		    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO]) &
		    MIX_ATTEN_MASK;
		ce4231_write(sc, SP_DIGITAL_MIX, l);
		break;
	case CSAUDIO_MONITOR_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			return (EINVAL);
		l = ce4231_read(sc, SP_DIGITAL_MIX) & ~DIGITAL_MIX1_ENABLE;
		if (!cp->un.ord)
			l |= DIGITAL_MIX1_ENABLE;
		ce4231_write(sc, SP_DIGITAL_MIX, l);
		break;

	case CSAUDIO_REC_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			return (EINVAL);
		l = ce4231_read(sc, SP_LEFT_INPUT_CONTROL) & INPUT_GAIN_MASK;
		r = ce4231_read(sc, SP_RIGHT_INPUT_CONTROL) & INPUT_GAIN_MASK;
		l = cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] >> 4;
		r = cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] >> 4;
		ce4231_write(sc, SP_LEFT_INPUT_CONTROL, l);
		ce4231_write(sc, SP_RIGHT_INPUT_CONTROL, r);
		break;
	case CSAUDIO_RECORD_SOURCE:
		if (cp->type != AUDIO_MIXER_ENUM)
			return (EINVAL);
		l = ce4231_read(sc, SP_LEFT_INPUT_CONTROL) & INPUT_SOURCE_MASK;
		r = ce4231_read(sc, SP_RIGHT_INPUT_CONTROL) & INPUT_SOURCE_MASK;
		l |= cp->un.ord << 6;
		r |= cp->un.ord << 6;
		ce4231_write(sc, SP_LEFT_INPUT_CONTROL, l);
		ce4231_write(sc, SP_RIGHT_INPUT_CONTROL, r);
		break;

	case CSAUDIO_MIC_PREAMP:
		if (cp->type != AUDIO_MIXER_ENUM)
			return (EINVAL);
		l = ce4231_read(sc, SP_LEFT_INPUT_CONTROL) &
		    ~INPUT_MIC_GAIN_ENABLE;
		r = ce4231_read(sc, SP_RIGHT_INPUT_CONTROL) &
		    ~INPUT_MIC_GAIN_ENABLE;
		if (cp->un.ord) {
			l |= INPUT_MIC_GAIN_ENABLE;
			r |= INPUT_MIC_GAIN_ENABLE;
		}
		ce4231_write(sc, SP_LEFT_INPUT_CONTROL, l);
		ce4231_write(sc, SP_RIGHT_INPUT_CONTROL, r);
		break;

	default:
		return (EINVAL);
	}

	return (0);
}

int
ce4231_get_port(void *addr, mixer_ctrl_t *cp)
{
	struct ce4231_softc *sc = (struct ce4231_softc *)addr;

	DPRINTF(("ce4231_get_port: port=%d type=%d\n", cp->dev, cp->type));

	switch (cp->dev) {

	case CSAUDIO_DAC_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			return (EINVAL);
		cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
		    AUDIO_MAX_GAIN - ((ce4231_read(sc, SP_LEFT_OUTPUT_CONTROL) &
		    OUTPUT_ATTEN_BITS) << 2);
		cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] =
		    AUDIO_MAX_GAIN - ((ce4231_read(sc, SP_RIGHT_OUTPUT_CONTROL) &
		    OUTPUT_ATTEN_BITS) << 2);
		break;
	case CSAUDIO_DAC_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			return (EINVAL);
		cp->un.ord = (ce4231_read(sc, SP_LEFT_OUTPUT_CONTROL) &
		    OUTPUT_MUTE) ? 1 : 0;
		break;

	case CSAUDIO_OUTPUTS:
		if (cp->type != AUDIO_MIXER_SET)
			return (EINVAL);
		cp->un.mask = ce4231_get_outputs(sc);
		break;

	case CSAUDIO_CD_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			return (EINVAL);
		cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
		    AUDIO_MAX_GAIN - ((ce4231_read(sc, SP_LEFT_AUX1_CONTROL) &
		    AUX_INPUT_ATTEN_BITS) << 3);
		cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] =
		    AUDIO_MAX_GAIN - ((ce4231_read(sc, SP_RIGHT_AUX1_CONTROL) &
		    AUX_INPUT_ATTEN_BITS) << 3);
		break;
	case CSAUDIO_CD_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			return (EINVAL);
		cp->un.ord = (ce4231_read(sc, SP_LEFT_AUX1_CONTROL) &
		    AUX_INPUT_MUTE) ? 1 : 0;
		break;

	case CSAUDIO_LINE_IN_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			return (EINVAL);
		cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
		    AUDIO_MAX_GAIN - ((ce4231_read(sc, CS_LEFT_LINE_CONTROL) &
		    LINE_INPUT_ATTEN_BITS) << 3);
		cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] =
		    AUDIO_MAX_GAIN - ((ce4231_read(sc, CS_RIGHT_LINE_CONTROL) &
		    LINE_INPUT_ATTEN_BITS) << 3);
		break;
	case CSAUDIO_LINE_IN_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			return (EINVAL);
		cp->un.ord = (ce4231_read(sc, CS_LEFT_LINE_CONTROL) &
		    LINE_INPUT_MUTE) ? 1 : 0;
		break;

	case CSAUDIO_MONITOR_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			return (EINVAL);
		if (cp->un.value.num_channels != 1)
			return (EINVAL);
		cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
		    AUDIO_MAX_GAIN - (ce4231_read(sc, SP_DIGITAL_MIX) &
		    MIX_ATTEN_MASK);
		break;
	case CSAUDIO_MONITOR_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			return (EINVAL);
		cp->un.ord = (ce4231_read(sc, SP_DIGITAL_MIX) &
		    DIGITAL_MIX1_ENABLE) ? 0 : 1;
		break;

	case CSAUDIO_REC_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			return (EINVAL);
		cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
		    (ce4231_read(sc, SP_LEFT_INPUT_CONTROL) &
		    ~INPUT_GAIN_MASK) << 4;
		cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] =
		    (ce4231_read(sc, SP_RIGHT_INPUT_CONTROL) &
		    ~INPUT_GAIN_MASK) << 4;
		break;
	case CSAUDIO_RECORD_SOURCE:
		if (cp->type != AUDIO_MIXER_ENUM)
			return (EINVAL);
		cp->un.ord = (ce4231_read(sc, SP_LEFT_INPUT_CONTROL) &
		    CS_REC_SRC_BITS) >> 6;
		break;

	case CSAUDIO_MIC_PREAMP:
		if (cp->type != AUDIO_MIXER_ENUM)
			return (EINVAL);
		cp->un.ord = (ce4231_read(sc, SP_LEFT_INPUT_CONTROL) &
		    INPUT_MIC_GAIN_ENABLE) ? 1 : 0;
		break;

	default:
		return (EINVAL);
	}
	return (0);
}

int
ce4231_query_devinfo(void *addr, mixer_devinfo_t *dip)
{
	size_t nsize = MAX_AUDIO_DEV_LEN;
	int err = 0;

	switch (dip->index) {
	case CSAUDIO_INPUT_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = CSAUDIO_INPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCinputs, nsize);
		break;
	case CSAUDIO_OUTPUT_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = CSAUDIO_OUTPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCoutputs, nsize);
		break;
	case CSAUDIO_RECORD_CLASS:
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = CSAUDIO_RECORD_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCrecord, nsize);
		break;

	case CSAUDIO_DAC_LVL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = CSAUDIO_OUTPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = CSAUDIO_DAC_MUTE;
		strlcpy(dip->label.name, AudioNdac, nsize);
		dip->un.v.num_channels = 2;
		dip->un.v.delta = 4;
		strlcpy(dip->un.v.units.name, AudioNvolume, nsize);
		break;
	case CSAUDIO_DAC_MUTE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = CSAUDIO_OUTPUT_CLASS;
		dip->prev = CSAUDIO_DAC_LVL;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmute, nsize);
		goto onoff;

	case CSAUDIO_OUTPUTS:
		dip->type = AUDIO_MIXER_SET;
		dip->mixer_class = CSAUDIO_OUTPUT_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNoutput, nsize);
		dip->un.s.num_mem = 3;
		strlcpy(dip->un.s.member[0].label.name, AudioNline, nsize);
		dip->un.s.member[0].mask = OUT_PORT_LINE;
		strlcpy(dip->un.s.member[1].label.name, AudioNheadphone, nsize);
		dip->un.s.member[1].mask = OUT_PORT_HP;
		strlcpy(dip->un.s.member[2].label.name, AudioNspeaker, nsize);
		dip->un.s.member[2].mask = OUT_PORT_SPKR;
		break;

	case CSAUDIO_CD_LVL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = CSAUDIO_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = CSAUDIO_CD_MUTE;
		strlcpy(dip->label.name, AudioNcd, nsize);
		dip->un.v.num_channels = 2;
		dip->un.v.delta = 8;
		strlcpy(dip->un.v.units.name, AudioNvolume, nsize);
		break;
	case CSAUDIO_CD_MUTE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = CSAUDIO_INPUT_CLASS;
		dip->prev = CSAUDIO_CD_LVL;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmute, nsize);
		goto onoff;

	case CSAUDIO_LINE_IN_LVL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = CSAUDIO_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = CSAUDIO_LINE_IN_MUTE;
		strlcpy(dip->label.name, AudioNline, nsize);
		dip->un.v.num_channels = 2;
		dip->un.v.delta = 8;
		strlcpy(dip->un.v.units.name, AudioNvolume, nsize);
		break;
	case CSAUDIO_LINE_IN_MUTE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = CSAUDIO_INPUT_CLASS;
		dip->prev = CSAUDIO_LINE_IN_LVL;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmute, nsize);
		goto onoff;

	case CSAUDIO_MONITOR_LVL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = CSAUDIO_OUTPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = CSAUDIO_MONITOR_MUTE;
		strlcpy(dip->label.name, AudioNmonitor, nsize);
		dip->un.v.num_channels = 1;
		dip->un.v.delta = 4;
		strlcpy(dip->un.v.units.name, AudioNvolume, nsize);
		break;
	case CSAUDIO_MONITOR_MUTE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = CSAUDIO_OUTPUT_CLASS;
		dip->prev = CSAUDIO_MONITOR_LVL;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNmute, nsize);
		goto onoff;

	case CSAUDIO_REC_LVL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = CSAUDIO_RECORD_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNvolume, nsize);
		dip->un.v.num_channels = 2;
		dip->un.v.delta = 16;
		strlcpy(dip->un.v.units.name, AudioNvolume, nsize);
		break;
	case CSAUDIO_RECORD_SOURCE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = CSAUDIO_RECORD_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNsource, nsize);
		dip->un.e.num_mem = 4;
		strlcpy(dip->un.e.member[0].label.name, AudioNline, nsize);
		dip->un.e.member[0].ord = REC_PORT_LINE;
		strlcpy(dip->un.e.member[1].label.name, AudioNcd, nsize);
		dip->un.e.member[1].ord = REC_PORT_CD;
		strlcpy(dip->un.e.member[2].label.name, AudioNmicrophone, nsize);
		dip->un.e.member[2].ord = REC_PORT_MIC;
		strlcpy(dip->un.e.member[3].label.name, AudioNmixerout, nsize);
		dip->un.e.member[3].ord = REC_PORT_MIX;
		break;

	case CSAUDIO_MIC_PREAMP:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = CSAUDIO_RECORD_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		snprintf(dip->label.name, nsize, "%s_%s", AudioNmicrophone,
		   AudioNpreamp);
		goto onoff;

onoff:
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNon, nsize);
		dip->un.e.member[0].ord = 1;
		strlcpy(dip->un.e.member[1].label.name, AudioNoff, nsize);
		dip->un.e.member[1].ord = 0;
		break;

	default:
		err = ENXIO;
	}

	return (err);
}

/*
 * Hardware interrupt handler
 */
/*
 * Don't bother with the AD1848_STATUS register.  It's interrupt bit gets
 * set for both recording and playback interrupts.  But we have separate
 * handlers for playback and recording, and if we clear the status in
 * one handler while there is an interrupt pending for the other direction
 * as well, we'll never notice the interrupt for the other direction.
 *
 * Instead rely solely on CS_IRQ_STATUS, which has separate bits for
 * playback and recording interrupts.  Also note that resetting
 * AD1848_STATUS clears the interrupt bits in CS_IRQ_STATUS.
 */

int
ce4231_pintr(void *v)
{
	struct ce4231_softc *sc = (struct ce4231_softc *)v;
	u_int32_t csr;
	u_int8_t reg;
	struct cs_dma *p;
	struct cs_chdma *chdma = &sc->sc_pchdma;
	int r = 0;

	mtx_enter(&audio_lock);
	csr = P_READ(sc, EBDMA_DCSR);

	reg = ce4231_read(sc, CS_IRQ_STATUS);
	if (reg & CS_IRQ_PI) {
		ce4231_write(sc, SP_LOWER_BASE_COUNT, 0xff);
		ce4231_write(sc, SP_UPPER_BASE_COUNT, 0xff);
		ce4231_write(sc, CS_IRQ_STATUS, reg & ~CS_IRQ_PI);
	}

	P_WRITE(sc, EBDMA_DCSR, csr);

	if (csr & EBDCSR_INT)
		r = 1;

	if ((csr & EBDCSR_TC) || ((csr & EBDCSR_A_LOADED) == 0)) {
		u_long nextaddr, togo;

		p = chdma->cur_dma;
		togo = chdma->segsz - chdma->count;
		if (togo == 0) {
			nextaddr = (u_int32_t)p->dmamap->dm_segs[0].ds_addr;
			chdma->count = togo = chdma->blksz;
		} else {
			nextaddr = chdma->lastaddr;
			if (togo > chdma->blksz)
				togo = chdma->blksz;
			chdma->count += togo;
		}

		P_WRITE(sc, EBDMA_DCNT, togo);
		P_WRITE(sc, EBDMA_DADDR, nextaddr);
		chdma->lastaddr = nextaddr + togo;

		if (sc->sc_pintr != NULL)
			(*sc->sc_pintr)(sc->sc_parg);
		r = 1;
	}
	mtx_leave(&audio_lock);
	return (r);
}

int
ce4231_cintr(void *v)
{
	struct ce4231_softc *sc = (struct ce4231_softc *)v;
	u_int32_t csr;
	u_int8_t reg;
	struct cs_dma *p;
	struct cs_chdma *chdma = &sc->sc_rchdma;
	int r = 0;

	mtx_enter(&audio_lock);
	csr = C_READ(sc, EBDMA_DCSR);

	reg = ce4231_read(sc, CS_IRQ_STATUS);
	if (reg & CS_IRQ_CI) {
		ce4231_write(sc, CS_LOWER_REC_CNT, 0xff);
		ce4231_write(sc, CS_UPPER_REC_CNT, 0xff);
		ce4231_write(sc, CS_IRQ_STATUS, reg & ~CS_IRQ_CI);
	}

	C_WRITE(sc, EBDMA_DCSR, csr);

	if (csr & EBDCSR_INT)
		r = 1;

	if ((csr & EBDCSR_TC) || ((csr & EBDCSR_A_LOADED) == 0)) {
		u_long nextaddr, togo;

		p = chdma->cur_dma;
		togo = chdma->segsz - chdma->count;
		if (togo == 0) {
			nextaddr = (u_int32_t)p->dmamap->dm_segs[0].ds_addr;
			chdma->count = togo = chdma->blksz;
		} else {
			nextaddr = chdma->lastaddr;
			if (togo > chdma->blksz)
				togo = chdma->blksz;
			chdma->count += togo;
		}

		C_WRITE(sc, EBDMA_DCNT, togo);
		C_WRITE(sc, EBDMA_DADDR, nextaddr);
		chdma->lastaddr = nextaddr + togo;

		if (sc->sc_rintr != NULL)
			(*sc->sc_rintr)(sc->sc_rarg);
		r = 1;
	}
	mtx_leave(&audio_lock);
	return (r);
}

void *
ce4231_alloc(void *addr, int direction, size_t size, int pool, int flags)
{
	struct ce4231_softc *sc = (struct ce4231_softc *)addr;
	bus_dma_tag_t dmat = sc->sc_dmatag;
	struct cs_dma *p;

	p = (struct cs_dma *)malloc(sizeof(struct cs_dma), pool, flags);
	if (p == NULL)
		return (NULL);

	if (bus_dmamap_create(dmat, size, 1, size, 0,
	    BUS_DMA_NOWAIT, &p->dmamap) != 0)
		goto fail;

	p->size = size;

	if (bus_dmamem_alloc(dmat, size, 64*1024, 0, p->segs,
	    sizeof(p->segs)/sizeof(p->segs[0]), &p->nsegs,
	    BUS_DMA_NOWAIT) != 0)
		goto fail1;

	if (bus_dmamem_map(dmat, p->segs, p->nsegs, p->size,
	    &p->addr, BUS_DMA_NOWAIT | BUS_DMA_COHERENT) != 0)
		goto fail2;

	if (bus_dmamap_load(dmat, p->dmamap, p->addr, size, NULL,
	    BUS_DMA_NOWAIT) != 0)
		goto fail3;

	p->next = sc->sc_dmas;
	sc->sc_dmas = p;
	return (p->addr);

fail3:
	bus_dmamem_unmap(dmat, p->addr, p->size);
fail2:
	bus_dmamem_free(dmat, p->segs, p->nsegs);
fail1:
	bus_dmamap_destroy(dmat, p->dmamap);
fail:
	free(p, pool, 0);
	return (NULL);
}

void
ce4231_free(void *addr, void *ptr, int pool)
{
	struct ce4231_softc *sc = addr;
	bus_dma_tag_t dmat = sc->sc_dmatag;
	struct cs_dma *p, **pp;

	for (pp = &sc->sc_dmas; (p = *pp) != NULL; pp = &(*pp)->next) {
		if (p->addr != ptr)
			continue;
		bus_dmamap_unload(dmat, p->dmamap);
		bus_dmamem_unmap(dmat, p->addr, p->size);
		bus_dmamem_free(dmat, p->segs, p->nsegs);
		bus_dmamap_destroy(dmat, p->dmamap);
		*pp = p->next;
		free(p, pool, 0);
		return;
	}
	printf("%s: attempt to free rogue pointer\n", sc->sc_dev.dv_xname);
}

int
ce4231_trigger_output(void *addr, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct ce4231_softc *sc = addr;
	struct cs_dma *p;
	struct cs_chdma *chdma = &sc->sc_pchdma;
	u_int32_t csr;
	vaddr_t n;

	sc->sc_pintr = intr;
	sc->sc_parg = arg;

	for (p = sc->sc_dmas; p->addr != start; p = p->next)
		/*EMPTY*/;
	if (p == NULL) {
		printf("%s: trigger_output: bad addr: %p\n",
		    sc->sc_dev.dv_xname, start);
		return (EINVAL);
	}

	n = (char *)end - (char *)start;

	/*
	 * Do only `blksize' at a time, so audio_pint() is kept
	 * synchronous with us...
	 */
	chdma->cur_dma = p;
	chdma->blksz = blksize;
	chdma->segsz = n;

	if (n > chdma->blksz)
		n = chdma->blksz;

	chdma->count = n;

	csr = P_READ(sc, EBDMA_DCSR);
	if (csr & EBDCSR_DMAEN) {
		P_WRITE(sc, EBDMA_DCNT, (u_long)n);
		P_WRITE(sc, EBDMA_DADDR,
		    (u_long)p->dmamap->dm_segs[0].ds_addr);
	} else {
		P_WRITE(sc, EBDMA_DCSR, EBDCSR_RESET);
		P_WRITE(sc, EBDMA_DCSR, sc->sc_burst);

		P_WRITE(sc, EBDMA_DCNT, (u_long)n);
		P_WRITE(sc, EBDMA_DADDR,
		    (u_long)p->dmamap->dm_segs[0].ds_addr);

		P_WRITE(sc, EBDMA_DCSR, sc->sc_burst | EBDCSR_DMAEN |
		    EBDCSR_INTEN | EBDCSR_CNTEN | EBDCSR_NEXTEN);

		ce4231_write(sc, SP_LOWER_BASE_COUNT, 0xff);
		ce4231_write(sc, SP_UPPER_BASE_COUNT, 0xff);
		ce4231_write(sc, SP_INTERFACE_CONFIG,
		    ce4231_read(sc, SP_INTERFACE_CONFIG) | PLAYBACK_ENABLE);
	}
	chdma->lastaddr = p->dmamap->dm_segs[0].ds_addr + n;

	return (0);
}

int
ce4231_trigger_input(void *addr, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct ce4231_softc *sc = addr;
	struct cs_dma *p;
	struct cs_chdma *chdma = &sc->sc_rchdma;
	u_int32_t csr;
	vaddr_t n;

	sc->sc_rintr = intr;
	sc->sc_rarg = arg;

	for (p = sc->sc_dmas; p->addr != start; p = p->next)
		/*EMPTY*/;
	if (p == NULL) {
		printf("%s: trigger_input: bad addr: %p\n",
		    sc->sc_dev.dv_xname, start);
		return (EINVAL);
	}

	n = (char *)end - (char *)start;

	/*
	 * Do only `blksize' at a time, so audio_rint() is kept
	 * synchronous with us...
	 */
	chdma->cur_dma = p;
	chdma->blksz = blksize;
	chdma->segsz = n;

	if (n > chdma->blksz)
		n = chdma->blksz;

	chdma->count = n;

	csr = C_READ(sc, EBDMA_DCSR);
	if (csr & EBDCSR_DMAEN) {
		C_WRITE(sc, EBDMA_DCNT, (u_long)n);
		C_WRITE(sc, EBDMA_DADDR,
		    (u_long)p->dmamap->dm_segs[0].ds_addr);
	} else {
		C_WRITE(sc, EBDMA_DCSR, EBDCSR_RESET);
		C_WRITE(sc, EBDMA_DCSR, sc->sc_burst);

		C_WRITE(sc, EBDMA_DCNT, (u_long)n);
		C_WRITE(sc, EBDMA_DADDR,
		    (u_long)p->dmamap->dm_segs[0].ds_addr);

		C_WRITE(sc, EBDMA_DCSR, sc->sc_burst | EBDCSR_WRITE |
		    EBDCSR_DMAEN | EBDCSR_INTEN | EBDCSR_CNTEN | EBDCSR_NEXTEN);

		ce4231_write(sc, CS_LOWER_REC_CNT, 0xff);
		ce4231_write(sc, CS_UPPER_REC_CNT, 0xff);
		ce4231_write(sc, SP_INTERFACE_CONFIG,
		    ce4231_read(sc, SP_INTERFACE_CONFIG) | CAPTURE_ENABLE);
	}
	chdma->lastaddr = p->dmamap->dm_segs[0].ds_addr + n;

	return (0);
}
