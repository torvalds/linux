/*	$OpenBSD: cs4231.c,v 1.44 2022/10/26 20:19:09 kn Exp $	*/

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
 *
 * Effort sponsored in part by the Defense Advanced Research Projects
 * Agency (DARPA) and Air Force Research Laboratory, Air Force
 * Materiel Command, USAF, under agreement number F30602-01-2-0537.
 *
 */

/*
 * Driver for CS4231 based audio found in some sun4m systems (cs4231)
 * based on ideas from the S/Linux project and the NetBSD project.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/errno.h>
#include <sys/ioctl.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/malloc.h>

#include <machine/bus.h>
#include <machine/intr.h>
#include <machine/autoconf.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <dev/ic/ad1848reg.h>
#include <dev/ic/cs4231reg.h>
#include <dev/ic/apcdmareg.h>
#include <dev/sbus/sbusvar.h>
#include <dev/sbus/cs4231var.h>

#define	CSAUDIO_DAC_LVL		0
#define	CSAUDIO_LINE_IN_LVL	1
#define	CSAUDIO_MIC_LVL		2
#define	CSAUDIO_CD_LVL		3
#define	CSAUDIO_MONITOR_LVL	4
#define	CSAUDIO_OUTPUT_LVL	5
#define	CSAUDIO_LINE_IN_MUTE	6
#define	CSAUDIO_DAC_MUTE	7
#define	CSAUDIO_CD_MUTE		8
#define	CSAUDIO_MIC_MUTE	9
#define	CSAUDIO_MONITOR_MUTE	10
#define	CSAUDIO_OUTPUT_MUTE	11
#define	CSAUDIO_REC_LVL		12
#define	CSAUDIO_RECORD_SOURCE	13
#define	CSAUDIO_OUTPUT		14
#define	CSAUDIO_INPUT_CLASS	15
#define	CSAUDIO_OUTPUT_CLASS	16
#define	CSAUDIO_RECORD_CLASS	17
#define	CSAUDIO_MONITOR_CLASS	18

#define	CSPORT_AUX2		0
#define	CSPORT_AUX1		1
#define	CSPORT_DAC		2
#define	CSPORT_LINEIN		3
#define	CSPORT_MONO		4
#define	CSPORT_MONITOR		5
#define	CSPORT_SPEAKER		6
#define	CSPORT_LINEOUT		7
#define	CSPORT_HEADPHONE	8
#define	CSPORT_MICROPHONE	9

#define MIC_IN_PORT	0
#define LINE_IN_PORT	1
#define AUX1_IN_PORT	2
#define DAC_IN_PORT	3

#ifdef AUDIO_DEBUG
#define	DPRINTF(x)	printf x
#else
#define	DPRINTF(x)
#endif

#define	CS_TIMEOUT	90000

#define	CS_PC_LINEMUTE	XCTL0_ENABLE
#define	CS_PC_HDPHMUTE	XCTL1_ENABLE
#define	CS_AFS_TI	0x40		/* timer interrupt */
#define	CS_AFS_CI	0x20		/* capture interrupt */
#define	CS_AFS_PI	0x10		/* playback interrupt */
#define	CS_AFS_CU	0x08		/* capture underrun */
#define	CS_AFS_CO	0x04		/* capture overrun */
#define	CS_AFS_PO	0x02		/* playback overrun */
#define	CS_AFS_PU	0x01		/* playback underrun */

#define CS_WRITE(sc,r,v)	\
    bus_space_write_1((sc)->sc_bustag, (sc)->sc_regs, (r) << 2, (v))
#define	CS_READ(sc,r)		\
    bus_space_read_1((sc)->sc_bustag, (sc)->sc_regs, (r) << 2)

#define	APC_WRITE(sc,r,v)	\
    bus_space_write_4(sc->sc_bustag, sc->sc_regs, r, v)
#define	APC_READ(sc,r)		\
    bus_space_read_4(sc->sc_bustag, sc->sc_regs, r)

int	cs4231_match(struct device *, void *, void *);
void	cs4231_attach(struct device *, struct device *, void *);
int	cs4231_intr(void *);

int	cs4231_set_speed(struct cs4231_softc *, u_long *);
void	cs4231_setup_output(struct cs4231_softc *sc);

void		cs4231_write(struct cs4231_softc *, u_int8_t, u_int8_t);
u_int8_t	cs4231_read(struct cs4231_softc *, u_int8_t);

/* Audio interface */
int	cs4231_open(void *, int);
void	cs4231_close(void *);
int	cs4231_set_params(void *, int, int, struct audio_params *,
    struct audio_params *);
int	cs4231_round_blocksize(void *, int);
int	cs4231_commit_settings(void *);
int	cs4231_halt_output(void *);
int	cs4231_halt_input(void *);
int	cs4231_set_port(void *, mixer_ctrl_t *);
int	cs4231_get_port(void *, mixer_ctrl_t *);
int	cs4231_query_devinfo(void *, mixer_devinfo_t *);
void *	cs4231_alloc(void *, int, size_t, int, int);
void	cs4231_free(void *, void *, int);
int	cs4231_trigger_output(void *, void *, void *, int,
    void (*)(void *), void *, struct audio_params *);
int	cs4231_trigger_input(void *, void *, void *, int,
    void (*)(void *), void *, struct audio_params *);

const struct audio_hw_if cs4231_sa_hw_if = {
	.open = cs4231_open,
	.close = cs4231_close,
	.set_params = cs4231_set_params,
	.round_blocksize = cs4231_round_blocksize,
	.commit_settings = cs4231_commit_settings,
	.halt_output = cs4231_halt_output,
	.halt_input = cs4231_halt_input,
	.set_port = cs4231_set_port,
	.get_port = cs4231_get_port,
	.query_devinfo = cs4231_query_devinfo,
	.allocm = cs4231_alloc,
	.freem = cs4231_free,
	.trigger_output = cs4231_trigger_output,
	.trigger_input = cs4231_trigger_input,
};

const struct cfattach audiocs_ca = {
	sizeof (struct cs4231_softc), cs4231_match, cs4231_attach
};

struct cfdriver audiocs_cd = {
	NULL, "audiocs", DV_DULL
};

int
cs4231_match(struct device *parent, void *vcf, void *aux)
{
	struct sbus_attach_args *sa = aux;

	return (strcmp("SUNW,CS4231", sa->sa_name) == 0);
}

void    
cs4231_attach(struct device *parent, struct device *self, void *aux)
{
	struct sbus_attach_args *sa = aux;
	struct cs4231_softc *sc = (struct cs4231_softc *)self;
	int node;
	u_int32_t sbusburst, burst;

	node = sa->sa_node;

	/* Pass on the bus tags */
	sc->sc_bustag = sa->sa_bustag;
	sc->sc_dmatag = sa->sa_dmatag;

	/* Make sure things are sane. */
	if (sa->sa_nintr != 1) {
		printf(": expected 1 interrupt, got %d\n", sa->sa_nintr);
		return;
	}
	if (sa->sa_nreg != 1) {
		printf(": expected 1 register set, got %d\n",
		    sa->sa_nreg);
		return;
	}

	if (bus_intr_establish(sa->sa_bustag, sa->sa_pri, IPL_AUDIO, 0,
	    cs4231_intr, sc, self->dv_xname) == NULL) {
		printf(": couldn't establish interrupt, pri %d\n",
		    INTLEV(sa->sa_pri));
		return;
	}

	if (sbus_bus_map(sa->sa_bustag,
	    sa->sa_reg[0].sbr_slot,
	    (bus_addr_t)sa->sa_reg[0].sbr_offset,
	    (bus_size_t)sa->sa_reg[0].sbr_size,
	    BUS_SPACE_MAP_LINEAR, 0, &sc->sc_regs) != 0) {
		printf(": couldn't map registers\n");
		return;
	}

	sbusburst = ((struct sbus_softc *)parent)->sc_burst;
	if (sbusburst == 0)
		sbusburst = SBUS_BURST_32 - 1;	/* 1->16 */
	burst = getpropint(node, "burst-sizes", -1);
	if (burst == -1)
		burst = sbusburst;
	sc->sc_burst = burst & sbusburst;

	printf("\n");

	audio_attach_mi(&cs4231_sa_hw_if, sc, NULL, &sc->sc_dev);

	/* Default to speaker, unmuted, reasonable volume */
	sc->sc_out_port = CSPORT_SPEAKER;
	sc->sc_in_port = CSPORT_MICROPHONE;
	sc->sc_mute[CSPORT_SPEAKER] = 1;
	sc->sc_mute[CSPORT_MONITOR] = 1;
	sc->sc_volume[CSPORT_SPEAKER].left = 192;
	sc->sc_volume[CSPORT_SPEAKER].right = 192;
}

/*
 * Write to one of the indexed registers of cs4231.
 */
void
cs4231_write(struct cs4231_softc *sc, u_int8_t r, u_int8_t v)
{
	CS_WRITE(sc, AD1848_IADDR, r);
	CS_WRITE(sc, AD1848_IDATA, v);
}

/*
 * Read from one of the indexed registers of cs4231.
 */
u_int8_t
cs4231_read(struct cs4231_softc *sc, u_int8_t r)
{
	CS_WRITE(sc, AD1848_IADDR, r);
	return (CS_READ(sc, AD1848_IDATA));
}

int
cs4231_set_speed(struct cs4231_softc *sc, u_long *argp)
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

	static const speed_struct speed_table[] = {
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
cs4231_open(void *vsc, int flags)
{
	struct cs4231_softc *sc = vsc;
	int tries;

	if (sc->sc_open)
		return (EBUSY);
	sc->sc_open = 1;

	sc->sc_capture.cs_intr = NULL;
	sc->sc_capture.cs_arg = NULL;
	sc->sc_capture.cs_locked = 0;

	sc->sc_playback.cs_intr = NULL;
	sc->sc_playback.cs_arg = NULL;
	sc->sc_playback.cs_locked = 0;

	APC_WRITE(sc, APC_CSR, APC_CSR_RESET);
	DELAY(10);
	APC_WRITE(sc, APC_CSR, 0);
	DELAY(10);
	APC_WRITE(sc, APC_CSR, APC_READ(sc, APC_CSR) | APC_CSR_CODEC_RESET);

	DELAY(20);

	APC_WRITE(sc, APC_CSR, APC_READ(sc, APC_CSR) & (~APC_CSR_CODEC_RESET));

	for (tries = CS_TIMEOUT;
	     tries && CS_READ(sc, AD1848_IADDR) == SP_IN_INIT; tries--)
		DELAY(10);
	if (tries == 0)
		printf("%s: timeout waiting for reset\n", sc->sc_dev.dv_xname);

	/* Turn on cs4231 mode */
	cs4231_write(sc, SP_MISC_INFO,
	    cs4231_read(sc, SP_MISC_INFO) | MODE2);

	cs4231_setup_output(sc);

	cs4231_write(sc, SP_PIN_CONTROL,
	    cs4231_read(sc, SP_PIN_CONTROL) | INTERRUPT_ENABLE);

	return (0);
}

void
cs4231_setup_output(struct cs4231_softc *sc)
{
	u_int8_t pc, mi, rm, lm;

	pc = cs4231_read(sc, SP_PIN_CONTROL) | CS_PC_HDPHMUTE | CS_PC_LINEMUTE;

	mi = cs4231_read(sc, CS_MONO_IO_CONTROL) | MONO_OUTPUT_MUTE;

	lm = cs4231_read(sc, SP_LEFT_OUTPUT_CONTROL);
	lm &= ~OUTPUT_ATTEN_BITS;
	lm |= ((~(sc->sc_volume[CSPORT_SPEAKER].left >> 2)) &
	    OUTPUT_ATTEN_BITS) | OUTPUT_MUTE;

	rm = cs4231_read(sc, SP_RIGHT_OUTPUT_CONTROL);
	rm &= ~OUTPUT_ATTEN_BITS;
	rm |= ((~(sc->sc_volume[CSPORT_SPEAKER].right >> 2)) &
	    OUTPUT_ATTEN_BITS) | OUTPUT_MUTE;

	if (sc->sc_mute[CSPORT_MONITOR]) {
		lm &= ~OUTPUT_MUTE;
		rm &= ~OUTPUT_MUTE;
	}

	switch (sc->sc_out_port) {
	case CSPORT_HEADPHONE:
		if (sc->sc_mute[CSPORT_SPEAKER])
			pc &= ~CS_PC_HDPHMUTE;
		break;
	case CSPORT_SPEAKER:
		if (sc->sc_mute[CSPORT_SPEAKER])
			mi &= ~MONO_OUTPUT_MUTE;
		break;
	case CSPORT_LINEOUT:
		if (sc->sc_mute[CSPORT_SPEAKER])
			pc &= ~CS_PC_LINEMUTE;
		break;
	}

	cs4231_write(sc, SP_LEFT_OUTPUT_CONTROL, lm);
	cs4231_write(sc, SP_RIGHT_OUTPUT_CONTROL, rm);
	cs4231_write(sc, SP_PIN_CONTROL, pc);
	cs4231_write(sc, CS_MONO_IO_CONTROL, mi);

	/* XXX doesn't really belong here... */
	switch (sc->sc_in_port) {
	case CSPORT_LINEIN:
		pc = LINE_INPUT;
		break;
	case CSPORT_AUX1:
		pc = AUX_INPUT;
		break;
	case CSPORT_DAC:
		pc = MIXED_DAC_INPUT;
		break;
	case CSPORT_MICROPHONE:
	default:
		pc = MIC_INPUT;
		break;
	}
	lm = cs4231_read(sc, SP_LEFT_INPUT_CONTROL);
	rm = cs4231_read(sc, SP_RIGHT_INPUT_CONTROL);
	lm &= ~(MIXED_DAC_INPUT | ATTEN_22_5);
	rm &= ~(MIXED_DAC_INPUT | ATTEN_22_5);
	lm |= pc | (sc->sc_adc.left >> 4);
	rm |= pc | (sc->sc_adc.right >> 4);
	cs4231_write(sc, SP_LEFT_INPUT_CONTROL, lm);
	cs4231_write(sc, SP_RIGHT_INPUT_CONTROL, rm);
}

void
cs4231_close(void *vsc)
{
	struct cs4231_softc *sc = vsc;

	cs4231_halt_input(sc);
	cs4231_halt_output(sc);
	cs4231_write(sc, SP_PIN_CONTROL,
	    cs4231_read(sc, SP_PIN_CONTROL) & (~INTERRUPT_ENABLE));
	sc->sc_open = 0;
}

int
cs4231_set_params(void *vsc, int setmode, int usemode,
    struct audio_params *p, struct audio_params *r)
{
	struct cs4231_softc *sc = (struct cs4231_softc *)vsc;
	int err, bits, enc = p->encoding;

	switch (enc) {
	case AUDIO_ENCODING_ULAW:
		if (p->precision != 8)
			return (EINVAL);
		bits = FMT_ULAW >> 5;
		break;
	case AUDIO_ENCODING_ALAW:
		if (p->precision != 8)
			return (EINVAL);
		bits = FMT_ALAW >> 5;
		break;
	case AUDIO_ENCODING_SLINEAR_LE:
		if (p->precision == 16)
			bits = FMT_TWOS_COMP >> 5;
		else
			return (EINVAL);
		break;
	case AUDIO_ENCODING_SLINEAR_BE:
		if (p->precision == 16)
			bits = FMT_TWOS_COMP_BE >> 5;
		else
			return (EINVAL);
		break;
	case AUDIO_ENCODING_ULINEAR_LE:
	case AUDIO_ENCODING_ULINEAR_BE:
		if (p->precision == 8)
			bits = FMT_PCM8 >> 5;
		else
			return (EINVAL);
		break;
	default:
		return (EINVAL);
	}

	if (p->channels != 1 && p->channels != 2)
		return (EINVAL);

	err = cs4231_set_speed(sc, &p->sample_rate);
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
cs4231_round_blocksize(void *vsc, int blk)
{
	return ((blk + 3) & (-4));
}

int
cs4231_commit_settings(void *vsc)
{
	struct cs4231_softc *sc = (struct cs4231_softc *)vsc;
	int tries;
	u_int8_t r, fs;

	if (sc->sc_need_commit == 0)
		return (0);

	fs = sc->sc_speed_bits | (sc->sc_format_bits << 5);
	if (sc->sc_channels == 2)
		fs |= FMT_STEREO;

	/* XXX: this is called before DMA is setup, useful ? */
	mtx_enter(&audio_lock);

	r = cs4231_read(sc, SP_INTERFACE_CONFIG) | AUTO_CAL_ENABLE;
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
cs4231_halt_output(void *vsc)
{
	struct cs4231_softc *sc = (struct cs4231_softc *)vsc;

	/* XXX Kills some capture bits */
	mtx_enter(&audio_lock);
	APC_WRITE(sc, APC_CSR, APC_READ(sc, APC_CSR) &
	    ~(APC_CSR_EI | APC_CSR_GIE | APC_CSR_PIE |
	      APC_CSR_EIE | APC_CSR_PDMA_GO | APC_CSR_PMIE));
	cs4231_write(sc, SP_INTERFACE_CONFIG,
	    cs4231_read(sc, SP_INTERFACE_CONFIG) & (~PLAYBACK_ENABLE));
	sc->sc_playback.cs_locked = 0;
	mtx_leave(&audio_lock);
	return (0);
}

int
cs4231_halt_input(void *vsc)
{
	struct cs4231_softc *sc = (struct cs4231_softc *)vsc;

	/* XXX Kills some playback bits */
	mtx_enter(&audio_lock);
	APC_WRITE(sc, APC_CSR, APC_CSR_CAPTURE_PAUSE);
	cs4231_write(sc, SP_INTERFACE_CONFIG,
	    cs4231_read(sc, SP_INTERFACE_CONFIG) & (~CAPTURE_ENABLE));
	sc->sc_capture.cs_locked = 0;
	mtx_leave(&audio_lock);
	return (0);
}

int
cs4231_set_port(void *vsc, mixer_ctrl_t *cp)
{
	struct cs4231_softc *sc = (struct cs4231_softc *)vsc;
	int error = EINVAL;

	DPRINTF(("cs4231_set_port: port=%d type=%d\n", cp->dev, cp->type));

	switch (cp->dev) {
	case CSAUDIO_DAC_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1)
			cs4231_write(sc, SP_LEFT_AUX1_CONTROL,
			    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] &
			    LINE_INPUT_ATTEN_BITS);
		else if (cp->un.value.num_channels == 2) {
			cs4231_write(sc, SP_LEFT_AUX1_CONTROL,
			    cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] &
			    LINE_INPUT_ATTEN_BITS);
			cs4231_write(sc, SP_RIGHT_AUX1_CONTROL,
			    cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] &
			    LINE_INPUT_ATTEN_BITS);
		} else
			break;
		error = 0;
		break;
	case CSAUDIO_LINE_IN_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1)
			cs4231_write(sc, CS_LEFT_LINE_CONTROL,
			    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] &
			    AUX_INPUT_ATTEN_BITS);
		else if (cp->un.value.num_channels == 2) {
			cs4231_write(sc, CS_LEFT_LINE_CONTROL,
			    cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] &
			    AUX_INPUT_ATTEN_BITS);
			cs4231_write(sc, CS_RIGHT_LINE_CONTROL,
			    cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] &
			    AUX_INPUT_ATTEN_BITS);
		} else
			break;
		error = 0;
		break;
	case CSAUDIO_MIC_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1) {
#if 0
			cs4231_write(sc, CS_MONO_IO_CONTROL,
			    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] &
			    MONO_INPUT_ATTEN_BITS);
#endif
		} else
			break;
		error = 0;
		break;
	case CSAUDIO_CD_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1) {
			cs4231_write(sc, SP_LEFT_AUX2_CONTROL,
			    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] &
			    LINE_INPUT_ATTEN_BITS);
		} else if (cp->un.value.num_channels == 2) {
			cs4231_write(sc, SP_LEFT_AUX2_CONTROL,
			    cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] &
			    LINE_INPUT_ATTEN_BITS);
			cs4231_write(sc, SP_RIGHT_AUX2_CONTROL,
			    cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] &
			    LINE_INPUT_ATTEN_BITS);
		} else
			break;
		error = 0;
		break;
	case CSAUDIO_MONITOR_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1)
			cs4231_write(sc, SP_DIGITAL_MIX,
			    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] << 2);
		else
			break;
		error = 0;
		break;
	case CSAUDIO_OUTPUT_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1) {
			sc->sc_volume[CSPORT_SPEAKER].left =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO];
			sc->sc_volume[CSPORT_SPEAKER].right =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO];
		}
		else if (cp->un.value.num_channels == 2) {
			sc->sc_volume[CSPORT_SPEAKER].left =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT];
			sc->sc_volume[CSPORT_SPEAKER].right =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT];
		}
		else
			break;

		cs4231_setup_output(sc);
		error = 0;
		break;
	case CSAUDIO_OUTPUT:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		if (cp->un.ord != CSPORT_LINEOUT &&
		    cp->un.ord != CSPORT_SPEAKER &&
		    cp->un.ord != CSPORT_HEADPHONE)
			return (EINVAL);
		sc->sc_out_port = cp->un.ord;
		cs4231_setup_output(sc);
		error = 0;
		break;
	case CSAUDIO_LINE_IN_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		sc->sc_mute[CSPORT_LINEIN] = cp->un.ord ? 1 : 0;
		error = 0;
		break;
	case CSAUDIO_DAC_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		sc->sc_mute[CSPORT_AUX1] = cp->un.ord ? 1 : 0;
		error = 0;
		break;
	case CSAUDIO_CD_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		sc->sc_mute[CSPORT_AUX2] = cp->un.ord ? 1 : 0;
		error = 0;
		break;
	case CSAUDIO_MIC_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		sc->sc_mute[CSPORT_MONO] = cp->un.ord ? 1 : 0;
		error = 0;
		break;
	case CSAUDIO_MONITOR_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		sc->sc_mute[CSPORT_MONITOR] = cp->un.ord ? 1 : 0;
		error = 0;
		break;
	case CSAUDIO_OUTPUT_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		sc->sc_mute[CSPORT_SPEAKER] = cp->un.ord ? 1 : 0;
		cs4231_setup_output(sc);
		error = 0;
		break;
	case CSAUDIO_REC_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1) {
			sc->sc_adc.left =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO];
			sc->sc_adc.right =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_MONO];
		} else if (cp->un.value.num_channels == 2) {
			sc->sc_adc.left =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT];
			sc->sc_adc.right =
			    cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT];
		} else
			break;
		cs4231_setup_output(sc);
		error = 0;
		break;
	case CSAUDIO_RECORD_SOURCE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		if (cp->un.ord == CSPORT_MICROPHONE ||
		    cp->un.ord == CSPORT_LINEIN ||
		    cp->un.ord == CSPORT_AUX1 ||
		    cp->un.ord == CSPORT_DAC) {
			sc->sc_in_port  = cp->un.ord;
			error = 0;
			cs4231_setup_output(sc);
		}
		break;
	}

	return (error);
}

int
cs4231_get_port(void *vsc, mixer_ctrl_t *cp)
{
	struct cs4231_softc *sc = (struct cs4231_softc *)vsc;
	int error = EINVAL;

	DPRINTF(("cs4231_get_port: port=%d type=%d\n", cp->dev, cp->type));

	switch (cp->dev) {
	case CSAUDIO_DAC_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1)
			cp->un.value.level[AUDIO_MIXER_LEVEL_MONO]=
			    cs4231_read(sc, SP_LEFT_AUX1_CONTROL) &
			    LINE_INPUT_ATTEN_BITS;
		else if (cp->un.value.num_channels == 2) {
			cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
			    cs4231_read(sc, SP_LEFT_AUX1_CONTROL) &
			    LINE_INPUT_ATTEN_BITS;
			cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] =
			    cs4231_read(sc, SP_RIGHT_AUX1_CONTROL) &
			    LINE_INPUT_ATTEN_BITS;
		} else
			break;
		error = 0;
		break;
	case CSAUDIO_LINE_IN_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1)
			cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
			    cs4231_read(sc, CS_LEFT_LINE_CONTROL) & AUX_INPUT_ATTEN_BITS;
		else if (cp->un.value.num_channels == 2) {
			cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
			    cs4231_read(sc, CS_LEFT_LINE_CONTROL) & AUX_INPUT_ATTEN_BITS;
			cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] =
			    cs4231_read(sc, CS_RIGHT_LINE_CONTROL) & AUX_INPUT_ATTEN_BITS;
		} else
			break;
		error = 0;
		break;
	case CSAUDIO_MIC_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1) {
#if 0
			cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
			    cs4231_read(sc, CS_MONO_IO_CONTROL) &
			    MONO_INPUT_ATTEN_BITS;
#endif
		} else
			break;
		error = 0;
		break;
	case CSAUDIO_CD_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1)
			cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
			    cs4231_read(sc, SP_LEFT_AUX2_CONTROL) &
			    LINE_INPUT_ATTEN_BITS;
		else if (cp->un.value.num_channels == 2) {
			cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
			    cs4231_read(sc, SP_LEFT_AUX2_CONTROL) &
			    LINE_INPUT_ATTEN_BITS;
			cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] =
			    cs4231_read(sc, SP_RIGHT_AUX2_CONTROL) &
			    LINE_INPUT_ATTEN_BITS;
		}
		else
			break;
		error = 0;
		break;
	case CSAUDIO_MONITOR_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels != 1)
			break;
		cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
		    cs4231_read(sc, SP_DIGITAL_MIX) >> 2;
		error = 0;
		break;
	case CSAUDIO_OUTPUT_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1)
			cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
			    sc->sc_volume[CSPORT_SPEAKER].left;
		else if (cp->un.value.num_channels == 2) {
			cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
			    sc->sc_volume[CSPORT_SPEAKER].left;
			cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] =
			    sc->sc_volume[CSPORT_SPEAKER].right;
		}
		else
			break;
		error = 0;
		break;
	case CSAUDIO_LINE_IN_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		cp->un.ord = sc->sc_mute[CSPORT_LINEIN] ? 1 : 0;
		error = 0;
		break;
	case CSAUDIO_DAC_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		cp->un.ord = sc->sc_mute[CSPORT_AUX1] ? 1 : 0;
		error = 0;
		break;
	case CSAUDIO_CD_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		cp->un.ord = sc->sc_mute[CSPORT_AUX2] ? 1 : 0;
		error = 0;
		break;
	case CSAUDIO_MIC_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		cp->un.ord = sc->sc_mute[CSPORT_MONO] ? 1 : 0;
		error = 0;
		break;
	case CSAUDIO_MONITOR_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		cp->un.ord = sc->sc_mute[CSPORT_MONITOR] ? 1 : 0;
		error = 0;
		break;
	case CSAUDIO_OUTPUT_MUTE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		cp->un.ord = sc->sc_mute[CSPORT_SPEAKER] ? 1 : 0;
		error = 0;
		break;
	case CSAUDIO_REC_LVL:
		if (cp->type != AUDIO_MIXER_VALUE)
			break;
		if (cp->un.value.num_channels == 1) {
			cp->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
			    sc->sc_adc.left;
		} else if (cp->un.value.num_channels == 2) {
			cp->un.value.level[AUDIO_MIXER_LEVEL_LEFT] =
			    sc->sc_adc.left;
			cp->un.value.level[AUDIO_MIXER_LEVEL_RIGHT] =
			    sc->sc_adc.right;
		} else
			break;
		error = 0;
		break;
	case CSAUDIO_RECORD_SOURCE:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		cp->un.ord = sc->sc_in_port;
		error = 0;
		break;
	case CSAUDIO_OUTPUT:
		if (cp->type != AUDIO_MIXER_ENUM)
			break;
		cp->un.ord = sc->sc_out_port;
		error = 0;
		break;
	}
	return (error);
}

int
cs4231_query_devinfo(void *vsc, mixer_devinfo_t *dip)
{
	int err = 0;

	switch (dip->index) {
	case CSAUDIO_MIC_LVL:		/* mono/microphone mixer */
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = CSAUDIO_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = CSAUDIO_MIC_MUTE;
		strlcpy(dip->label.name, AudioNmicrophone,
		    sizeof dip->label.name);
		dip->un.v.num_channels = 1;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case CSAUDIO_DAC_LVL:		/* dacout */
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = CSAUDIO_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = CSAUDIO_DAC_MUTE;
		strlcpy(dip->label.name, AudioNdac,
		    sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case CSAUDIO_LINE_IN_LVL:	/* line */
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = CSAUDIO_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = CSAUDIO_LINE_IN_MUTE;
		strlcpy(dip->label.name, AudioNline, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case CSAUDIO_CD_LVL:		/* cd */
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = CSAUDIO_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = CSAUDIO_CD_MUTE;
		strlcpy(dip->label.name, AudioNcd, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case CSAUDIO_MONITOR_LVL:	/* monitor level */
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = CSAUDIO_MONITOR_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = CSAUDIO_MONITOR_MUTE;
		strlcpy(dip->label.name, AudioNmonitor,
		    sizeof dip->label.name);
		dip->un.v.num_channels = 1;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case CSAUDIO_OUTPUT_LVL:
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = CSAUDIO_OUTPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = CSAUDIO_OUTPUT_MUTE;
		strlcpy(dip->label.name, AudioNoutput, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case CSAUDIO_LINE_IN_MUTE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = CSAUDIO_INPUT_CLASS;
		dip->prev = CSAUDIO_LINE_IN_LVL;
		dip->next = AUDIO_MIXER_LAST;
		goto mute;
	case CSAUDIO_DAC_MUTE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = CSAUDIO_INPUT_CLASS;
		dip->prev = CSAUDIO_DAC_LVL;
		dip->next = AUDIO_MIXER_LAST;
		goto mute;
	case CSAUDIO_CD_MUTE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = CSAUDIO_INPUT_CLASS;
		dip->prev = CSAUDIO_CD_LVL;
		dip->next = AUDIO_MIXER_LAST;
		goto mute;
	case CSAUDIO_MIC_MUTE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = CSAUDIO_INPUT_CLASS;
		dip->prev = CSAUDIO_MIC_LVL;
		dip->next = AUDIO_MIXER_LAST;
		goto mute;
	case CSAUDIO_MONITOR_MUTE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = CSAUDIO_OUTPUT_CLASS;
		dip->prev = CSAUDIO_MONITOR_LVL;
		dip->next = AUDIO_MIXER_LAST;
		goto mute;
	case CSAUDIO_OUTPUT_MUTE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = CSAUDIO_OUTPUT_CLASS;
		dip->prev = CSAUDIO_OUTPUT_LVL;
		dip->next = AUDIO_MIXER_LAST;
		goto mute;

	mute:
		strlcpy(dip->label.name, AudioNmute, sizeof dip->label.name);
		dip->un.e.num_mem = 2;
		strlcpy(dip->un.e.member[0].label.name, AudioNon,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = 0;
		strlcpy(dip->un.e.member[1].label.name, AudioNoff,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = 1;
		break;
	case CSAUDIO_REC_LVL:		/* record level */
		dip->type = AUDIO_MIXER_VALUE;
		dip->mixer_class = CSAUDIO_RECORD_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = CSAUDIO_RECORD_SOURCE;
		strlcpy(dip->label.name, AudioNrecord, sizeof dip->label.name);
		dip->un.v.num_channels = 2;
		strlcpy(dip->un.v.units.name, AudioNvolume,
		    sizeof dip->un.v.units.name);
		break;
	case CSAUDIO_RECORD_SOURCE:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = CSAUDIO_RECORD_CLASS;
		dip->prev = CSAUDIO_REC_LVL;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNsource, sizeof dip->label.name);
		dip->un.e.num_mem = 4;
		strlcpy(dip->un.e.member[0].label.name, AudioNmicrophone,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = CSPORT_MICROPHONE;
		strlcpy(dip->un.e.member[1].label.name, AudioNline,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = CSPORT_LINEIN;
		strlcpy(dip->un.e.member[2].label.name, AudioNcd,
		    sizeof dip->un.e.member[2].label.name);
		dip->un.e.member[2].ord = CSPORT_AUX1;
		strlcpy(dip->un.e.member[3].label.name, AudioNdac,
		    sizeof dip->un.e.member[3].label.name);
		dip->un.e.member[3].ord = CSPORT_DAC;
		break;
	case CSAUDIO_OUTPUT:
		dip->type = AUDIO_MIXER_ENUM;
		dip->mixer_class = CSAUDIO_MONITOR_CLASS;
		dip->prev = dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioNoutput, sizeof dip->label.name);
		dip->un.e.num_mem = 3;
		strlcpy(dip->un.e.member[0].label.name, AudioNspeaker,
		    sizeof dip->un.e.member[0].label.name);
		dip->un.e.member[0].ord = CSPORT_SPEAKER;
		strlcpy(dip->un.e.member[1].label.name, AudioNline,
		    sizeof dip->un.e.member[1].label.name);
		dip->un.e.member[1].ord = CSPORT_LINEOUT;
		strlcpy(dip->un.e.member[2].label.name, AudioNheadphone,
		    sizeof dip->un.e.member[2].label.name);
		dip->un.e.member[2].ord = CSPORT_HEADPHONE;
		break;
	case CSAUDIO_INPUT_CLASS:	/* input class descriptor */
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = CSAUDIO_INPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCinputs, sizeof dip->label.name);
		break;
	case CSAUDIO_OUTPUT_CLASS:	/* output class descriptor */
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = CSAUDIO_OUTPUT_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCoutputs,
		    sizeof dip->label.name);
		break;
	case CSAUDIO_MONITOR_CLASS:	/* monitor class descriptor */
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = CSAUDIO_MONITOR_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCmonitor,
		    sizeof dip->label.name);
		break;
	case CSAUDIO_RECORD_CLASS:	/* record class descriptor */
		dip->type = AUDIO_MIXER_CLASS;
		dip->mixer_class = CSAUDIO_RECORD_CLASS;
		dip->prev = AUDIO_MIXER_LAST;
		dip->next = AUDIO_MIXER_LAST;
		strlcpy(dip->label.name, AudioCrecord, sizeof dip->label.name);
		break;
	default:
		err = ENXIO;
	}

	return (err);
}

/*
 * Hardware interrupt handler
 */
int
cs4231_intr(void *vsc)
{
	struct cs4231_softc *sc = (struct cs4231_softc *)vsc;
	u_int32_t csr;
	u_int8_t reg, status;
	struct cs_dma *p;
	int r = 0;

	mtx_enter(&audio_lock);
	csr = APC_READ(sc, APC_CSR);
	APC_WRITE(sc, APC_CSR, csr);

	if ((csr & APC_CSR_EIE) && (csr & APC_CSR_EI)) {
		printf("%s: error interrupt\n", sc->sc_dev.dv_xname);
		r = 1;
	}

	if ((csr & APC_CSR_PIE) && (csr & APC_CSR_PI)) {
		/* playback interrupt */
		r = 1;
	}

	if ((csr & APC_CSR_GIE) && (csr & APC_CSR_GI)) {
		/* general interrupt */
		status = CS_READ(sc, AD1848_STATUS);
		if (status & (INTERRUPT_STATUS | SAMPLE_ERROR)) {
			reg = cs4231_read(sc, CS_IRQ_STATUS);
			if (reg & CS_AFS_PI) {
				cs4231_write(sc, SP_LOWER_BASE_COUNT, 0xff);
				cs4231_write(sc, SP_UPPER_BASE_COUNT, 0xff);
			}
			if (reg & CS_AFS_CI) {
				cs4231_write(sc, CS_LOWER_REC_CNT, 0xff);
				cs4231_write(sc, CS_UPPER_REC_CNT, 0xff);
			}
			CS_WRITE(sc, AD1848_STATUS, 0);
		}
		r = 1;
	}


	if (csr & (APC_CSR_PI|APC_CSR_PMI|APC_CSR_PIE|APC_CSR_PD))
		r = 1;

	if ((csr & APC_CSR_PMIE) && (csr & APC_CSR_PMI)) {
		struct cs_channel *chan = &sc->sc_playback;
		u_long nextaddr, togo;

		p = chan->cs_curdma;
		togo = chan->cs_segsz - chan->cs_cnt;
		if (togo == 0) {
			nextaddr = (u_int32_t)p->dmamap->dm_segs[0].ds_addr;
			chan->cs_cnt = togo = chan->cs_blksz;
		} else {
			nextaddr = APC_READ(sc, APC_PNVA) + chan->cs_blksz;
			if (togo > chan->cs_blksz)
				togo = chan->cs_blksz;
			chan->cs_cnt += togo;
		}

		APC_WRITE(sc, APC_PNVA, nextaddr);
		APC_WRITE(sc, APC_PNC, togo);

		if (chan->cs_intr != NULL)
			(*chan->cs_intr)(chan->cs_arg);
		r = 1;
	}

	if ((csr & APC_CSR_CIE) && (csr & APC_CSR_CI)) {
		if (csr & APC_CSR_CD) {
			struct cs_channel *chan = &sc->sc_capture;
			u_long nextaddr, togo;

			p = chan->cs_curdma;
			togo = chan->cs_segsz - chan->cs_cnt;
			if (togo == 0) {
				nextaddr =
				    (u_int32_t)p->dmamap->dm_segs[0].ds_addr;
				chan->cs_cnt = togo = chan->cs_blksz;
			} else {
				nextaddr = APC_READ(sc, APC_CNVA) +
				    chan->cs_blksz;
				if (togo > chan->cs_blksz)
					togo = chan->cs_blksz;
				chan->cs_cnt += togo;
			}

			APC_WRITE(sc, APC_CNVA, nextaddr);
			APC_WRITE(sc, APC_CNC, togo);

			if (chan->cs_intr != NULL)
				(*chan->cs_intr)(chan->cs_arg);
		}
		r = 1;
	}

	if ((csr & APC_CSR_CMIE) && (csr & APC_CSR_CMI)) {
		/* capture empty */
		r = 1;
	}

	mtx_leave(&audio_lock);
	return (r);
}

void *
cs4231_alloc(void *vsc, int direction, size_t size, int pool, int flags)
{
	struct cs4231_softc *sc = (struct cs4231_softc *)vsc;
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
	    nitems(p->segs), &p->nsegs,
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
cs4231_free(void *vsc, void *ptr, int pool)
{
	struct cs4231_softc *sc = vsc;
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
cs4231_trigger_output(void *vsc, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct cs4231_softc *sc = vsc;
	struct cs_channel *chan = &sc->sc_playback;
	struct cs_dma *p;
	u_int32_t csr;
	u_long n;

	if (chan->cs_locked != 0) {
		printf("%s: trigger_output: already running\n",
		    sc->sc_dev.dv_xname);
		return (EINVAL);
	}

	chan->cs_locked = 1;
	chan->cs_intr = intr;
	chan->cs_arg = arg;

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
	chan->cs_blksz = blksize;
	chan->cs_curdma = p;
	chan->cs_segsz = n;

	if (n > chan->cs_blksz)
		n = chan->cs_blksz;

	chan->cs_cnt = n;

	mtx_enter(&audio_lock);
	csr = APC_READ(sc, APC_CSR);

	APC_WRITE(sc, APC_PNVA, (u_long)p->dmamap->dm_segs[0].ds_addr);
	APC_WRITE(sc, APC_PNC, (u_long)n);

	if ((csr & APC_CSR_PDMA_GO) == 0 || (csr & APC_CSR_PPAUSE) != 0) {
		APC_WRITE(sc, APC_CSR,
		    APC_READ(sc, APC_CSR) & ~(APC_CSR_PIE | APC_CSR_PPAUSE));
		APC_WRITE(sc, APC_CSR, APC_READ(sc, APC_CSR) |
		    APC_CSR_EI | APC_CSR_GIE | APC_CSR_PIE | APC_CSR_EIE |
		    APC_CSR_PMIE | APC_CSR_PDMA_GO);
		cs4231_write(sc, SP_LOWER_BASE_COUNT, 0xff);
		cs4231_write(sc, SP_UPPER_BASE_COUNT, 0xff);
		cs4231_write(sc, SP_INTERFACE_CONFIG,
		    cs4231_read(sc, SP_INTERFACE_CONFIG) | PLAYBACK_ENABLE);
	}
	mtx_leave(&audio_lock);
	return (0);
}

int
cs4231_trigger_input(void *vsc, void *start, void *end, int blksize,
    void (*intr)(void *), void *arg, struct audio_params *param)
{
	struct cs4231_softc *sc = vsc;
	struct cs_channel *chan = &sc->sc_capture;
	struct cs_dma *p;
	u_int32_t csr;
	u_long n;

	if (chan->cs_locked != 0) {
		printf("%s: trigger_input: already running\n",
		    sc->sc_dev.dv_xname);
		return (EINVAL);
	}
	chan->cs_locked = 1;
	chan->cs_intr = intr;
	chan->cs_arg = arg;

	for (p = sc->sc_dmas; p->addr != start; p = p->next)
		/*EMPTY*/;
	if (p == NULL) {
		printf("%s: trigger_input: bad addr: %p\n",
		    sc->sc_dev.dv_xname, start);
		return (EINVAL);
	}

	n = (char *)end - (char *)start;

	/*
	 * Do only `blksize' at a time, so audio_cint() is kept
	 * synchronous with us...
	 */
	chan->cs_blksz = blksize;
	chan->cs_curdma = p;
	chan->cs_segsz = n;

	if (n > chan->cs_blksz)
		n = chan->cs_blksz;
	chan->cs_cnt = n;

	mtx_enter(&audio_lock);
	APC_WRITE(sc, APC_CNVA, p->dmamap->dm_segs[0].ds_addr);
	APC_WRITE(sc, APC_CNC, (u_long)n);

	csr = APC_READ(sc, APC_CSR);
	if ((csr & APC_CSR_CDMA_GO) == 0 || (csr & APC_CSR_CPAUSE) != 0) {
		csr &= APC_CSR_CPAUSE;
		csr |= APC_CSR_GIE | APC_CSR_CMIE | APC_CSR_CIE | APC_CSR_EI |
		    APC_CSR_CDMA_GO;
		APC_WRITE(sc, APC_CSR, csr);
		cs4231_write(sc, CS_LOWER_REC_CNT, 0xff);
		cs4231_write(sc, CS_UPPER_REC_CNT, 0xff);
		cs4231_write(sc, SP_INTERFACE_CONFIG,
		    cs4231_read(sc, SP_INTERFACE_CONFIG) | CAPTURE_ENABLE);
	}

	if (APC_READ(sc, APC_CSR) & APC_CSR_CD) {
		u_long nextaddr, togo;

		p = chan->cs_curdma;
		togo = chan->cs_segsz - chan->cs_cnt;
		if (togo == 0) {
			nextaddr = (u_int32_t)p->dmamap->dm_segs[0].ds_addr;
			chan->cs_cnt = togo = chan->cs_blksz;
		} else {
			nextaddr = APC_READ(sc, APC_CNVA) + chan->cs_blksz;
			if (togo > chan->cs_blksz)
				togo = chan->cs_blksz;
			chan->cs_cnt += togo;
		}

		APC_WRITE(sc, APC_CNVA, nextaddr);
		APC_WRITE(sc, APC_CNC, togo);
	}

	mtx_leave(&audio_lock);
	return (0);
}
