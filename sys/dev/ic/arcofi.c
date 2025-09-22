/*	$OpenBSD: arcofi.c,v 1.22 2025/07/14 23:49:08 jsg Exp $	*/

/*
 * Copyright (c) 2011 Miodrag Vallat.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 * Driver for the HP ``Audio1'' device, which is a FIFO layer around a
 * Siemens PSB 2160 ``ARCOFI'' phone quality audio chip.
 *
 * It is known to exist in two flavours: on-board the HP9000/425e as a DIO
 * device, an on-board the HP9000/{705,710,745,747} as a GIO device.
 *
 * The FIFO logic buffers up to 128 bytes. When using 8 bit samples and
 * the logic set to interrupt every half FIFO, the device will interrupt
 * 125 times per second.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/proc.h>
#include <sys/endian.h>
#include <sys/fcntl.h>

#include <sys/audioio.h>
#include <dev/audio_if.h>

#include <machine/autoconf.h>
#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ic/arcofivar.h>

#if 0
#define	ARCOFI_DEBUG
#endif

/*
 * Siemens PSB2160 registers
 */

/* CMDR */
#define	CMDR_AD		0x80	/* SP1/SP2 address convention */
#define	CMDR_READ	0x40
#define	CMDR_WRITE	0x00
#define	CMDR_PU		0x20	/* Power Up */
#define	CMDR_RCS	0x10	/* Receive and transmit in CH B2 */
#define	CMDR_MASK	0x0f

	/* command	     length	data */
#define	SOP_0	0x00	/*	5	CR4 CR3 CR2 CR1 */
#define	COP_1	0x01	/*	5	t1_hi t1_lo f1_hi f1_lo */
#define	COP_2	0x02	/*	3	gr1 gr2 */
#define	COP_3	0x03	/*	3	t2_hi t2_lo f2_hi f2_lo */
#define	SOP_4	0x04	/*	2	CR1 */
#define	SOP_5	0x05	/*	2	CR2 */
#define	SOP_6	0x06	/*	2	CR3 */
#define	SOP_7	0x07	/*	2	CR4 */
#define	COP_8	0x08	/*	3	dtmf_hi dtmf_lo */
#define	COP_9	0x09	/*	5	gz a3 a2 a1 */
#define	COP_A	0x0a	/*	9	fx1 to fx8 */
#define	COP_B	0x0b	/*	3	gx1 gx2 */
#define	COP_C	0x0c	/*	9	fr1 to fr 8 */
#define	COP_D	0x0d	/*	5	fr9 fr10 fx9 fx10 */
#define	COP_E	0x0e	/*	5	t3_hi t3_lo f3_hi f3_lo */

/* CR1 */
#define	CR1_GR		0x80	/* GR gain loaded from CRAM vs 0dB */
#define	CR1_GZ		0x40	/* Z gain loaded from CRAM vs -18dB */
#define	CR1_FX		0x20	/* X filter loaded from CRAM vs 0dB flat */
#define	CR1_FR		0x10	/* R filter loaded from CRAM vs 0dB flat */
#define	CR1_GX		0x08	/* GX gain loaded from CRAM vs 0dB */
#define	CR1_T_MASK	0x07	/* test mode */
#define	CR1_DLP		0x07	/* digital loopback via PCM registers */
#define	CR1_DLM		0x06	/* D/A output looped back to A/D input */
#define	CR1_DLS		0x05	/* digital loopback via converter registers */
#define	CR1_IDR		0x04	/* data RAM initialization */
#define	CR1_BYP		0x03	/* bypass analog frontend */
#define	CR1_ALM		0x02	/* analog loopback via MUX */
#define	CR1_ALS		0x01	/* analog loopback via converter registers */

/* CR2 */
#define	CR2_SD		0x80	/* SD pin set to input vs output */
#define	CR2_SC		0x40	/* SC pin set to input vs output */
#define	CR2_SB		0x20	/* SB pin set to input vs output */
#define	CR2_SA		0x10	/* SA pin set to input vs output */
#define	CR2_ELS		0x08	/* non-input S pins tristate SIP vs sending 0 */
#define	CR2_AM		0x04	/* only one device on the SLD bus */
#define	CR2_TR		0x02	/* three party conferencing */
#define	CR2_EFC		0x01	/* enable feature control */

/* CR3 */
#define	CR3_MIC_G_MASK	0xe0		/* MIC input analog gain  */
#define	CR3_MIC_X_INPUT		0xe0	/* MIC disabled, X input 15.1 dB */
#define	CR3_MIC_G_17		0xc0	/* 17 dB */
#define	CR3_MIC_G_22		0xa0	/* 22 dB */
#define	CR3_MIC_G_28		0x80	/* 28 dB */
#define	CR3_MIC_G_34		0x60	/* 34 dB */
#define	CR3_MIC_G_40		0x40	/* 40 dB */
#define	CR3_MIC_G_46		0x20	/* 46 dB */
#define	CR3_MIC_G_52		0x00	/* 52 dB (reset default) */
#define	CR3_AFEC_MASK	0x1c
#define	CR3_AFEC_MUTE		0x18	/* mute: Hout */
#define	CR3_AFEC_HFS		0x14	/* hands free: FHM, LS out */
#define	CR3_AFEC_LH3		0x10	/* loud hearing 3: MIC, H out, LS out */
#define	CR3_AFEC_LH2		0x0c	/* loud hearing 2: MIC, LS out */
#define	CR3_AFEC_LH1		0x08	/* loud hearing 1: LS out */
#define	CR3_AFEC_RDY		0x04	/* ready: MIC, H out */
#define	CR3_AFEC_POR		0x00	/* power on reset: all off */
#define	CR3_OPMODE_MASK	0x03
#define	CR3_OPMODE_LINEAR	0x02	/* linear (16 bit) */
#define	CR3_OPMODE_MIXED	0x01	/* mixed */
#define	CR3_OPMODE_NORMAL	0x00	/* normal (A/u-Law) */

/* CR4 */
#define	CR4_DHF		0x80	/* TX digital high frequency enable */
#define	CR4_DTMF	0x40	/* DTMF generator enable */
#define	CR4_TG		0x20	/* tone ring enable */
#define	CR4_BT		0x10	/* beat tone generator enable */
#define	CR4_TM		0x08	/* incoming voice enable */
#define	CR4_BM		0x04	/* beat mode (3 tone vs 2 tone) */
#define	CR4_PM		0x02	/* tone sent to piezo vs loudspeaker */
#define	CR4_ULAW	0x01	/* u-Law vs A-Law */


/*
 * Glue logic registers
 * Note the register values here are symbolic, as actual addresses
 * depend upon the particular bus the device is connected to.
 */

#define	ARCOFI_ID		0	/* id (r) and reset (w) register */

#define	ARCOFI_CSR		1	/* status and control register */
#define	CSR_INTR_ENABLE			0x80
#define	CSR_INTR_REQUEST		0x40	/* unacknowledged interrupt */
/* 0x20 and 0x10 used in DIO flavours, to provide IPL */
#define	CSR_WIDTH_16			0x08	/* 16-bit samples */
#define	CSR_CTRL_FIFO_ENABLE		0x04	/* connect FIFO to CMDR */
#define	CSR_DATA_FIFO_ENABLE		0x01	/* connect FIFO to DU/DD */

#define	ARCOFI_FIFO_IR		2	/* FIFO interrupt register */
#define	FIFO_IR_ENABLE(ev)		((ev) << 4)
#define	FIFO_IR_EVENT(ev)		(ev)
#define	FIFO_IR_OUT_EMPTY		0x08
#define	FIFO_IR_CTRL_EMPTY		0x04
#define	FIFO_IR_OUT_HALF_EMPTY		0x02
#define	FIFO_IR_IN_HALF_EMPTY		0x01

#define	ARCOFI_FIFO_SR		3	/* FIFO status register (ro) */
#define	FIFO_SR_CTRL_FULL		0x20
#define	FIFO_SR_CTRL_EMPTY		0x10
#define	FIFO_SR_OUT_FULL		0x08
#define	FIFO_SR_OUT_EMPTY		0x04
#define	FIFO_SR_IN_FULL			0x02
#define	FIFO_SR_IN_EMPTY		0x01

#define	ARCOFI_FIFO_DATA	4	/* data FIFO port */

#define	ARCOFI_FIFO_CTRL	5	/* control FIFO port (wo) */

#define	ARCOFI_FIFO_SIZE	128


struct cfdriver arcofi_cd = {
	NULL, "arcofi", DV_DULL
};

#define	arcofi_read(sc, r) \
	bus_space_read_1((sc)->sc_iot, (sc)->sc_ioh, (sc)->sc_reg[r])
#define	arcofi_write(sc, r, v) \
	bus_space_write_1((sc)->sc_iot, (sc)->sc_ioh, (sc)->sc_reg[r], v)

int	arcofi_cmd(struct arcofi_softc *, uint8_t, const uint8_t *);
int	arcofi_cr3_to_portmask(uint, int);
int	arcofi_gain_to_mi(uint);
uint	arcofi_mi_to_gain(int);
uint	arcofi_portmask_to_cr3(int);
int	arcofi_set_param(struct arcofi_softc *, int, int, int,
	    struct audio_params *);

void	arcofi_close(void *);
int	arcofi_commit_settings(void *);
int	arcofi_get_port(void *, mixer_ctrl_t *);
int	arcofi_halt_input(void *);
int	arcofi_halt_output(void *);
int	arcofi_open(void *, int);
int	arcofi_query_devinfo(void *, mixer_devinfo_t *);
int	arcofi_round_blocksize(void *, int);
int	arcofi_set_params(void *, int, int, struct audio_params *,
	    struct audio_params *);
int	arcofi_set_port(void *, mixer_ctrl_t *);
int	arcofi_start_input(void *, void *, int, void (*)(void *), void *);
int	arcofi_start_output(void *, void *, int, void (*)(void *), void *);

const struct audio_hw_if arcofi_hw_if = {
	.open = arcofi_open,
	.close = arcofi_close,
	.set_params = arcofi_set_params,
	.round_blocksize = arcofi_round_blocksize,
	.commit_settings = arcofi_commit_settings,
	.start_output = arcofi_start_output,
	.start_input = arcofi_start_input,
	.halt_output = arcofi_halt_output,
	.halt_input = arcofi_halt_input,
	.set_port = arcofi_set_port,
	.get_port = arcofi_get_port,
	.query_devinfo = arcofi_query_devinfo,
};

/* mixer items */
#define	ARCOFI_PORT_AUDIO_IN_VOLUME	0	/* line in volume (GR) */
#define	ARCOFI_PORT_AUDIO_OUT_VOLUME	1	/* line out volume (GX) */
#define	ARCOFI_PORT_AUDIO_SPKR_VOLUME	2	/* speaker volume (GX) */
#define	ARCOFI_PORT_AUDIO_IN_MUTE	3	/* line in mute (MIC) */
#define	ARCOFI_PORT_AUDIO_OUT_MUTE	4	/* line out mute (H out) */
#define	ARCOFI_PORT_AUDIO_SPKR_MUTE	5	/* line in mute (LS out) */
/* mixer classes */
#define	ARCOFI_CLASS_INPUT		6
#define	ARCOFI_CLASS_OUTPUT		7

/*
 * Gain programming formulae are a complete mystery to me, and of course
 * no two chips are compatible - not even the PSB 2163 and PSB 2165
 * later ARCOFI chips, from the same manufacturer as the PSB 2160!
 *
 * Of course, the PSB 2160 datasheet does not give any set of values.
 * The following table is taken from the HP-UX audio driver (audio_shared.o
 * private_audio_gain_tab).
 */

#define	NEGATIVE_GAINS	60
#define	POSITIVE_GAINS	14
static const uint16_t arcofi_gains[1 + NEGATIVE_GAINS + 1 + POSITIVE_GAINS] = {
	/* minus infinity */
	0x0988,

	0xf8b8, 0xf8b8, 0xf8b8, 0xf8b8, 0x099f, 0x099f, 0x099f, 0x099f,
	0x09af, 0x09af, 0x09af, 0x09cf, 0x09cf, 0x09cf, 0xf8a9, 0xf83a,
	0xf83a, 0xf82b, 0xf82d, 0xf8a3, 0xf8b2, 0xf8a1, 0xe8aa, 0xe84b,
	0xe89e, 0xe8d3, 0xe891, 0xe8b1, 0xd8aa, 0xd8cb, 0xd8a6, 0xd8b3,
	0xd842, 0xd8b1, 0xc8aa, 0xc8bb, 0xc888, 0xc853, 0xc852, 0xc8b1,
	0xb8aa, 0xb8ab, 0xb896, 0xb892, 0xb842, 0xb8b1, 0xa8aa, 0xa8bb,
	0x199f, 0x195b, 0x29c1, 0x2923, 0x29aa, 0x392b, 0xf998, 0xb988,
	0x1aac, 0x3aa1, 0xbaa1, 0xbb88,

	/* 0 */
	0x8888,

	0xd388, 0x5288, 0xb1a1, 0x31a1, 0x1192, 0x11d0, 0x30c0, 0x2050,
	0x1021, 0x1020, 0x1000, 0x0001, 0x0010, 0x0000
};

int
arcofi_open(void *v, int flags)
{
	struct arcofi_softc *sc = (struct arcofi_softc *)v;

	if ((flags & (FWRITE | FREAD)) == (FWRITE | FREAD))
		return ENXIO;
	if (sc->sc_open)
		return EBUSY;
	sc->sc_open = 1;
	KASSERT(sc->sc_mode == 0);

	return 0;
}

void
arcofi_close(void *v)
{
	struct arcofi_softc *sc = (struct arcofi_softc *)v;

	arcofi_halt_input(v);
	arcofi_halt_output(v);
	sc->sc_open = 0;
}

/*
 * Compute proper sample and hardware settings. Invoked both for record
 * and playback, as we don't support independent settings.
 */
int
arcofi_set_param(struct arcofi_softc *sc, int set, int use, int mode,
    struct audio_params *ap)
{
	if ((set & mode) == 0)
		return 0;

#ifdef ARCOFI_DEBUG
	printf("%s: set_param, mode %d encoding %d precision %d\n",
	    sc->sc_dev.dv_xname, mode, ap->encoding, ap->precision);
#endif
	switch (ap->precision) {
	case 8:
		switch (ap->encoding) {
		case AUDIO_ENCODING_ULAW:
			sc->sc_shadow.cr4 |= CR4_ULAW;
			break;
		case AUDIO_ENCODING_ALAW:
			sc->sc_shadow.cr4 &= ~CR4_ULAW;
			break;
		default:
			return EINVAL;
		}
		sc->sc_shadow.cr3 = (sc->sc_shadow.cr3 & ~CR3_OPMODE_MASK) |
		    CR3_OPMODE_NORMAL;
		break;
	case 16:
		switch (ap->encoding) {
		case AUDIO_ENCODING_SLINEAR_BE:
			break;
		default:
			return EINVAL;
		}
		sc->sc_shadow.cr3 = (sc->sc_shadow.cr3 & ~CR3_OPMODE_MASK) |
		    CR3_OPMODE_LINEAR;
		break;
	default:
		return EINVAL;
	}

	ap->bps = AUDIO_BPS(ap->precision);
	ap->msb = 1;
	ap->channels = 1;
	ap->sample_rate = 8000;

	return 0;
}

int
arcofi_set_params(void *v, int set, int use, struct audio_params *p,
    struct audio_params *r)
{
	struct arcofi_softc *sc = (struct arcofi_softc *)v;
	int rc;

	if (r != NULL) {
		rc = arcofi_set_param(sc, set, use, AUMODE_RECORD, r);
		if (rc != 0)
			return rc;
	}

	if (p != NULL) {
		rc = arcofi_set_param(sc, set, use, AUMODE_PLAY, p);
		if (rc != 0)
			return rc;
	}

	return 0;
}

int
arcofi_round_blocksize(void *v, int blksz)
{
	/*
	 * Round the size up to a multiple of half the FIFO, to favour
	 * smooth interrupt operation.
	 */
	return roundup(blksz, ARCOFI_FIFO_SIZE / 2);
}

int
arcofi_commit_settings(void *v)
{
	struct arcofi_softc *sc = (struct arcofi_softc *)v;
	int rc;
	uint8_t cmd[2], csr, ocsr;

#ifdef ARCOFI_DEBUG
	printf("%s: commit_settings, gr %04x gx %04x cr3 %02x cr4 %02x mute %d\n",
	    sc->sc_dev.dv_xname,
	    arcofi_gains[sc->sc_shadow.gr_idx],
	    arcofi_gains[sc->sc_shadow.gx_idx],
	    sc->sc_shadow.cr3, sc->sc_shadow.cr4, sc->sc_shadow.output_mute);
#endif

	if (bcmp(&sc->sc_active, &sc->sc_shadow, sizeof(sc->sc_active)) == 0)
		return 0;

	mtx_enter(&audio_lock);

	if (sc->sc_active.gr_idx != sc->sc_shadow.gr_idx) {
		cmd[0] = arcofi_gains[sc->sc_shadow.gr_idx] >> 8;
		cmd[1] = arcofi_gains[sc->sc_shadow.gr_idx];
		if ((rc = arcofi_cmd(sc, COP_2, cmd)) != 0)
			goto error;
		sc->sc_active.gr_idx = sc->sc_shadow.gr_idx;
	}

	if (sc->sc_active.gx_idx != sc->sc_shadow.gx_idx ||
	    sc->sc_active.output_mute != sc->sc_shadow.output_mute) {
		if (sc->sc_shadow.output_mute) {
			cmd[0] = arcofi_gains[0] >> 8;
			cmd[1] = arcofi_gains[0];
		} else {
			cmd[0] = arcofi_gains[sc->sc_shadow.gx_idx] >> 8;
			cmd[1] = arcofi_gains[sc->sc_shadow.gx_idx];
		}
		if ((rc = arcofi_cmd(sc, COP_B, cmd)) != 0)
			goto error;
		sc->sc_active.gx_idx = sc->sc_shadow.gx_idx;
		sc->sc_active.output_mute = sc->sc_shadow.output_mute;
	}

	if (sc->sc_active.cr3 != sc->sc_shadow.cr3) {
		cmd[0] = sc->sc_shadow.cr3;
		if ((rc = arcofi_cmd(sc, SOP_6, cmd)) != 0)
			goto error;
		sc->sc_active.cr3 = sc->sc_shadow.cr3;

		ocsr = arcofi_read(sc, ARCOFI_CSR);
		if ((sc->sc_active.cr3 & CR3_OPMODE_MASK) != CR3_OPMODE_NORMAL)
			csr = ocsr | CSR_WIDTH_16;
		else
			csr = ocsr & ~CSR_WIDTH_16;
		if (csr != ocsr)
			arcofi_write(sc, ARCOFI_CSR, csr);
	}

	if (sc->sc_active.cr4 != sc->sc_shadow.cr4) {
		cmd[0] = sc->sc_shadow.cr4;
		if ((rc = arcofi_cmd(sc, SOP_7, cmd)) != 0)
			goto error;
		sc->sc_active.cr4 = sc->sc_shadow.cr4;
	}

	rc = 0;
error:
	mtx_leave(&audio_lock);
	return rc;
}

int
arcofi_start_input(void *v, void *rbuf, int rsz, void (*cb)(void *),
    void *cbarg)
{
	struct arcofi_softc *sc = (struct arcofi_softc *)v;

#ifdef ARCOFI_DEBUG
	printf("%s: start_input, mode %d\n",
	    sc->sc_dev.dv_xname, sc->sc_mode);
#endif

	/* enable data FIFO if becoming active */
	if (sc->sc_mode == 0)
		arcofi_write(sc, ARCOFI_CSR,
		    arcofi_read(sc, ARCOFI_CSR) | CSR_DATA_FIFO_ENABLE);
	sc->sc_mode |= AUMODE_RECORD;

	sc->sc_recv.buf = (uint8_t *)rbuf;
	sc->sc_recv.past = (uint8_t *)rbuf + rsz;
	sc->sc_recv.cb = cb;
	sc->sc_recv.cbarg = cbarg;

	/* enable input FIFO interrupts */
	arcofi_write(sc, ARCOFI_FIFO_IR, arcofi_read(sc, ARCOFI_FIFO_IR) |
	    FIFO_IR_ENABLE(FIFO_IR_IN_HALF_EMPTY));

	return 0;
}

int
arcofi_start_output(void *v, void *wbuf, int wsz, void (*cb)(void *),
    void *cbarg)
{
	struct arcofi_softc *sc = (struct arcofi_softc *)v;

#ifdef ARCOFI_DEBUG
	printf("%s: start_output, mode %d\n",
	    sc->sc_dev.dv_xname, sc->sc_mode);
#endif

	/* enable data FIFO if becoming active */
	if (sc->sc_mode == 0)
		arcofi_write(sc, ARCOFI_CSR,
		    arcofi_read(sc, ARCOFI_CSR) | CSR_DATA_FIFO_ENABLE);
	sc->sc_mode |= AUMODE_PLAY;

	sc->sc_xmit.buf = (uint8_t *)wbuf;
	sc->sc_xmit.past = (uint8_t *)wbuf + wsz;
	sc->sc_xmit.cb = cb;
	sc->sc_xmit.cbarg = cbarg;

	/* enable output FIFO interrupts */
	arcofi_write(sc, ARCOFI_FIFO_IR, arcofi_read(sc, ARCOFI_FIFO_IR) |
	    FIFO_IR_ENABLE(FIFO_IR_OUT_HALF_EMPTY));

	return 0;
}

int
arcofi_halt_input(void *v)
{
	struct arcofi_softc *sc = (struct arcofi_softc *)v;

	mtx_enter(&audio_lock);

	/* disable input FIFO interrupts */
	arcofi_write(sc, ARCOFI_FIFO_IR, arcofi_read(sc, ARCOFI_FIFO_IR) &
	    ~FIFO_IR_ENABLE(FIFO_IR_IN_HALF_EMPTY));
	/* disable data FIFO if becoming idle */
	sc->sc_mode &= ~AUMODE_RECORD;
	if (sc->sc_mode == 0)
		arcofi_write(sc, ARCOFI_CSR,
		    arcofi_read(sc, ARCOFI_CSR) & ~CSR_DATA_FIFO_ENABLE);

	mtx_leave(&audio_lock);
	return 0;
}

int
arcofi_halt_output(void *v)
{
	struct arcofi_softc *sc = (struct arcofi_softc *)v;

	mtx_enter(&audio_lock);

	/* disable output FIFO interrupts */
	arcofi_write(sc, ARCOFI_FIFO_IR, arcofi_read(sc, ARCOFI_FIFO_IR) &
	    ~FIFO_IR_ENABLE(FIFO_IR_OUT_HALF_EMPTY));
	/* disable data FIFO if becoming idle */
	sc->sc_mode &= ~AUMODE_PLAY;
	if (sc->sc_mode == 0)
		arcofi_write(sc, ARCOFI_CSR,
		    arcofi_read(sc, ARCOFI_CSR) & ~CSR_DATA_FIFO_ENABLE);

	mtx_leave(&audio_lock);
	return 0;
}

/*
 * Convert gain table index to AUDIO_MIN_GAIN..AUDIO_MAX_GAIN scale.
 */
int
arcofi_gain_to_mi(uint idx)
{
	if (idx == 0)
		return AUDIO_MIN_GAIN;
	if (idx == nitems(arcofi_gains) - 1)
		return AUDIO_MAX_GAIN;

	return ((idx - 1) * (AUDIO_MAX_GAIN - AUDIO_MIN_GAIN)) /
	    (nitems(arcofi_gains) - 1) + AUDIO_MIN_GAIN + 1;
}

/*
 * Convert AUDIO_MIN_GAIN..AUDIO_MAX_GAIN scale to gain table index.
 */
uint
arcofi_mi_to_gain(int lvl)
{
	if (lvl <= AUDIO_MIN_GAIN)
		return 0;
	if (lvl >= AUDIO_MAX_GAIN)
		return nitems(arcofi_gains) - 1;

	return ((lvl - AUDIO_MIN_GAIN - 1) * (nitems(arcofi_gains) - 1)) /
	    (AUDIO_MAX_GAIN - AUDIO_MIN_GAIN);
}

/*
 * Input and output ports definition (used to be in <audioio.h>)
 */
#define	AUDIO_SPEAKER		0x01	/* built-in speaker */
#define	AUDIO_LINE_IN		0x02	/* line in	 */
#define	AUDIO_LINE_OUT		0x04	/* line out	 */

/*
 * The mapping between the available inputs and outputs, and CR3, is as
 * follows:
 * - the `line in' connector is the `MIC' input.
 * - the `line out' connector is the `H out' (headphones) output.
 * - the internal `speaker' is the `LS out' (loudspeaker) output.
 *
 * Each of these can be enabled or disabled independently, except for
 * MIC enabled with H out and LS out disabled, which is not allowed
 * by the chip (and makes no sense for a chip which was intended to
 * be used in phones, not voice recorders); we cheat by keeping one
 * output source enabled, but with the output gain forced to minus
 * infinity to mute it.
 *
 * The truth table is thus:
 *
 *	MIC	LS out	H out	AFEC
 *	off	off	off	POR
 *	off	off	on	MUTE
 *	off	on	off	LH1
 *	off	on	on	LH3, X input enabled
 *	on	off	off	RDY, GX forced to minus infinity
 *	on	off	on	RDY
 *	on	on	off	LH2
 *	on	on	on	LH3
 */

/*
 * Convert logical port enable settings to a valid CR3 value.
 */
uint
arcofi_portmask_to_cr3(int mask)
{
	switch (mask) {
	default:
	case 0:
		return CR3_MIC_G_17 | CR3_AFEC_POR;
	case AUDIO_LINE_OUT:
		return CR3_MIC_G_17 | CR3_AFEC_MUTE;
	case AUDIO_SPEAKER:
		return CR3_MIC_G_17 | CR3_AFEC_LH1;
	case AUDIO_SPEAKER | AUDIO_LINE_OUT:
		return CR3_MIC_X_INPUT | CR3_AFEC_LH3;
	case AUDIO_LINE_IN:
		/* since we can't do this, just... */
		/* FALLTHROUGH */
	case AUDIO_LINE_IN | AUDIO_LINE_OUT:
		return CR3_MIC_G_17 | CR3_AFEC_RDY;
	case AUDIO_LINE_IN | AUDIO_SPEAKER:
		return CR3_MIC_G_17 | CR3_AFEC_LH2;
	case AUDIO_LINE_IN | AUDIO_SPEAKER | AUDIO_LINE_OUT:
		return CR3_MIC_G_17 | CR3_AFEC_LH3;
	}
}

/*
 * Convert CR3 to an enabled ports mask.
 */
int
arcofi_cr3_to_portmask(uint cr3, int output_mute)
{
	switch (cr3 & CR3_AFEC_MASK) {
	default:
	case CR3_AFEC_POR:
		return 0;
	case CR3_AFEC_RDY:
		return output_mute ?
		    AUDIO_LINE_IN : AUDIO_LINE_IN | AUDIO_LINE_OUT;
	case CR3_AFEC_HFS:
	case CR3_AFEC_LH1:
		return AUDIO_SPEAKER;
	case CR3_AFEC_LH2:
		return AUDIO_LINE_IN | AUDIO_SPEAKER;
	case CR3_AFEC_LH3:
		if ((cr3 & CR3_MIC_G_MASK) == CR3_MIC_X_INPUT)
			return AUDIO_SPEAKER | AUDIO_LINE_OUT;
		else
			return AUDIO_LINE_IN | AUDIO_SPEAKER | AUDIO_LINE_OUT;
	case CR3_AFEC_MUTE:
		return AUDIO_LINE_OUT;
	}
}

int
arcofi_set_port(void *v, mixer_ctrl_t *mc)
{
	struct arcofi_softc *sc = (struct arcofi_softc *)v;
	int portmask;

	/* check for proper type */
	switch (mc->dev) {
	/* volume settings */
	case ARCOFI_PORT_AUDIO_IN_VOLUME:
	case ARCOFI_PORT_AUDIO_OUT_VOLUME:
	case ARCOFI_PORT_AUDIO_SPKR_VOLUME:
		if (mc->un.value.num_channels != 1)
			return EINVAL;
		break;
	/* mute settings */
	case ARCOFI_PORT_AUDIO_IN_MUTE:
	case ARCOFI_PORT_AUDIO_OUT_MUTE:
	case ARCOFI_PORT_AUDIO_SPKR_MUTE:
		if (mc->type != AUDIO_MIXER_ENUM)
			return EINVAL;
		portmask = arcofi_cr3_to_portmask(sc->sc_shadow.cr3,
		    sc->sc_shadow.output_mute);
#ifdef ARCOFI_DEBUG
		printf("%s: set_port cr3 %02x -> mask %02x\n",
		    sc->sc_dev.dv_xname, sc->sc_shadow.cr3, portmask);
#endif
		break;
	default:
		return EINVAL;
	}

	switch (mc->dev) {
	/* volume settings */
	case ARCOFI_PORT_AUDIO_IN_VOLUME:
		sc->sc_shadow.gr_idx =
		    arcofi_mi_to_gain(mc->un.value.level[AUDIO_MIXER_LEVEL_MONO]);
		return 0;
	case ARCOFI_PORT_AUDIO_OUT_VOLUME:
	case ARCOFI_PORT_AUDIO_SPKR_VOLUME:
		sc->sc_shadow.gx_idx =
		    arcofi_mi_to_gain(mc->un.value.level[AUDIO_MIXER_LEVEL_MONO]);
		return 0;

	/* mute settings */
	case ARCOFI_PORT_AUDIO_IN_MUTE:
		if (mc->un.ord)
			portmask &= ~AUDIO_LINE_IN;
		else
			portmask |= AUDIO_LINE_IN;
		break;
	case ARCOFI_PORT_AUDIO_OUT_MUTE:
		if (mc->un.ord)
			portmask &= ~AUDIO_LINE_OUT;
		else
			portmask |= AUDIO_LINE_OUT;
		break;
	case ARCOFI_PORT_AUDIO_SPKR_MUTE:
		if (mc->un.ord)
			portmask &= ~AUDIO_SPEAKER;
		else
			portmask |= AUDIO_SPEAKER;
		break;
	}

	sc->sc_shadow.cr3 = (sc->sc_shadow.cr3 & CR3_OPMODE_MASK) |
	    arcofi_portmask_to_cr3(portmask);
	sc->sc_shadow.output_mute = (portmask == AUDIO_LINE_IN);
#ifdef ARCOFI_DEBUG
	printf("%s: set_port mask %02x -> cr3 %02x m %d\n",
	    sc->sc_dev.dv_xname, portmask,
	    sc->sc_shadow.cr3, sc->sc_shadow.output_mute);
#endif

	return 0;
}

int
arcofi_get_port(void *v, mixer_ctrl_t *mc)
{
	struct arcofi_softc *sc = (struct arcofi_softc *)v;
	int portmask;

	/* check for proper type */
	switch (mc->dev) {
	/* volume settings */
	case ARCOFI_PORT_AUDIO_IN_VOLUME:
	case ARCOFI_PORT_AUDIO_OUT_VOLUME:
	case ARCOFI_PORT_AUDIO_SPKR_VOLUME:
		if (mc->un.value.num_channels != 1)
			return EINVAL;
		break;

	/* mute settings */
	case ARCOFI_PORT_AUDIO_IN_MUTE:
	case ARCOFI_PORT_AUDIO_OUT_MUTE:
	case ARCOFI_PORT_AUDIO_SPKR_MUTE:
		if (mc->type != AUDIO_MIXER_ENUM)
			return EINVAL;
		portmask = arcofi_cr3_to_portmask(sc->sc_shadow.cr3,
		    sc->sc_shadow.output_mute);
#ifdef ARCOFI_DEBUG
		printf("%s: get_port cr3 %02x -> mask %02x\n",
		    sc->sc_dev.dv_xname, sc->sc_shadow.cr3, portmask);
#endif
		break;
	default:
		return EINVAL;
	}

	switch (mc->dev) {
	/* volume settings */
	case ARCOFI_PORT_AUDIO_IN_VOLUME:
		mc->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
		    arcofi_gain_to_mi(sc->sc_shadow.gr_idx);
		break;
	case ARCOFI_PORT_AUDIO_OUT_VOLUME:
	case ARCOFI_PORT_AUDIO_SPKR_VOLUME:
		mc->un.value.level[AUDIO_MIXER_LEVEL_MONO] =
		    arcofi_gain_to_mi(sc->sc_shadow.gx_idx);
		break;

	/* mute settings */
	case ARCOFI_PORT_AUDIO_IN_MUTE:
		mc->un.ord = portmask & AUDIO_LINE_IN ? 0 : 1;
		break;
	case ARCOFI_PORT_AUDIO_OUT_MUTE:
		mc->un.ord = portmask & AUDIO_LINE_OUT ? 0 : 1;
		break;
	case ARCOFI_PORT_AUDIO_SPKR_MUTE:
		mc->un.ord = portmask & AUDIO_SPEAKER ? 0 : 1;
		break;
	}

	return 0;
}

int
arcofi_query_devinfo(void *v, mixer_devinfo_t *md)
{
	switch (md->index) {
	default:
		return ENXIO;

	/* items */
	case ARCOFI_PORT_AUDIO_IN_VOLUME:
		md->type = AUDIO_MIXER_VALUE;
		md->mixer_class = ARCOFI_CLASS_INPUT;
		md->prev = AUDIO_MIXER_LAST;
		md->next = ARCOFI_PORT_AUDIO_IN_MUTE;
		strlcpy(md->label.name, AudioNline,
		    sizeof md->label.name);
		goto mono_volume;
	case ARCOFI_PORT_AUDIO_OUT_VOLUME:
		md->type = AUDIO_MIXER_VALUE;
		md->mixer_class = ARCOFI_CLASS_OUTPUT;
		md->prev = AUDIO_MIXER_LAST;
		md->next = ARCOFI_PORT_AUDIO_OUT_MUTE;
		strlcpy(md->label.name, AudioNline,
		    sizeof md->label.name);
		goto mono_volume;
	case ARCOFI_PORT_AUDIO_SPKR_VOLUME:
		md->type = AUDIO_MIXER_VALUE;
		md->mixer_class = ARCOFI_CLASS_OUTPUT;
		md->prev = AUDIO_MIXER_LAST;
		md->next = ARCOFI_PORT_AUDIO_SPKR_MUTE;
		strlcpy(md->label.name, AudioNspeaker,
		    sizeof md->label.name);
		/* goto mono_volume; */
mono_volume:
		md->un.v.num_channels = 1;
		strlcpy(md->un.v.units.name, AudioNvolume,
		    sizeof md->un.v.units.name);
		break;

	case ARCOFI_PORT_AUDIO_IN_MUTE:
		md->type = AUDIO_MIXER_ENUM;
		md->mixer_class = ARCOFI_CLASS_INPUT;
		md->prev = ARCOFI_PORT_AUDIO_IN_VOLUME;
		md->next = AUDIO_MIXER_LAST;
		goto mute;
	case ARCOFI_PORT_AUDIO_OUT_MUTE:
		md->type = AUDIO_MIXER_ENUM;
		md->mixer_class = ARCOFI_CLASS_OUTPUT;
		md->prev = ARCOFI_PORT_AUDIO_OUT_VOLUME;
		md->next = AUDIO_MIXER_LAST;
		goto mute;
	case ARCOFI_PORT_AUDIO_SPKR_MUTE:
		md->type = AUDIO_MIXER_ENUM;
		md->mixer_class = ARCOFI_CLASS_OUTPUT;
		md->prev = ARCOFI_PORT_AUDIO_SPKR_VOLUME;
		md->next = AUDIO_MIXER_LAST;
		/* goto mute; */
mute:
		strlcpy(md->label.name, AudioNmute, sizeof md->label.name);
		md->un.e.num_mem = 2;
		strlcpy(md->un.e.member[0].label.name, AudioNoff,
		    sizeof md->un.e.member[0].label.name);
		md->un.e.member[0].ord = 0;
		strlcpy(md->un.e.member[1].label.name, AudioNon,
		    sizeof md->un.e.member[1].label.name);
		md->un.e.member[1].ord = 1;
		break;

	/* classes */
	case ARCOFI_CLASS_INPUT:
		md->type = AUDIO_MIXER_CLASS;
		md->mixer_class = ARCOFI_CLASS_INPUT;
		md->prev = AUDIO_MIXER_LAST;
		md->next = AUDIO_MIXER_LAST;
		strlcpy(md->label.name, AudioCinputs,
		    sizeof md->label.name);
		break;
	case ARCOFI_CLASS_OUTPUT:
		md->type = AUDIO_MIXER_CLASS;
		md->mixer_class = ARCOFI_CLASS_OUTPUT;
		md->prev = AUDIO_MIXER_LAST;
		md->next = AUDIO_MIXER_LAST;
		strlcpy(md->label.name, AudioCoutputs,
		    sizeof md->label.name);
		break;
	}

	return 0;
}

int
arcofi_hwintr(void *v)
{
	struct arcofi_softc *sc = (struct arcofi_softc *)v;
	uint8_t *cur, *past;
	uint8_t csr, fir, data;
	int rc = 0;

	csr = arcofi_read(sc, ARCOFI_CSR);
	if ((csr & CSR_INTR_REQUEST) == 0)
		return 0;

	fir = arcofi_read(sc, ARCOFI_FIFO_IR);

	/* receive */
	if (fir & FIFO_IR_EVENT(FIFO_IR_IN_HALF_EMPTY)) {
		rc = 1;
		cur = sc->sc_recv.buf;
		past = sc->sc_recv.past;

		while ((arcofi_read(sc, ARCOFI_FIFO_SR) &
		    FIFO_SR_IN_EMPTY) == 0) {
			data = arcofi_read(sc, ARCOFI_FIFO_DATA);
			if (cur != NULL && cur != past) {
				*cur++ = data;
				if (cur == past) {
					softintr_schedule(sc->sc_sih);
					break;
				}
			}
		}
		sc->sc_recv.buf = cur;

		if (cur == NULL || cur == past) {
			/* underrun, disable further interrupts */
			arcofi_write(sc, ARCOFI_FIFO_IR,
			    arcofi_read(sc, ARCOFI_FIFO_IR) &
			    ~FIFO_IR_ENABLE(FIFO_IR_IN_HALF_EMPTY));
		}
	}

	/* xmit */
	if (fir & FIFO_IR_EVENT(FIFO_IR_OUT_HALF_EMPTY)) {
		rc = 1;
		cur = sc->sc_xmit.buf;
		past = sc->sc_xmit.past;
		if (cur != NULL) {
			while ((arcofi_read(sc, ARCOFI_FIFO_SR) &
			    FIFO_SR_OUT_FULL) == 0) {
				if (cur != past)
					arcofi_write(sc, ARCOFI_FIFO_DATA,
					    *cur++);
				if (cur == past) {
					softintr_schedule(sc->sc_sih);
					break;
				}
			}
		}
		if (cur == NULL || cur == past) {
			/* disable further interrupts */
			arcofi_write(sc, ARCOFI_FIFO_IR,
			    arcofi_read(sc, ARCOFI_FIFO_IR) &
			    ~FIFO_IR_ENABLE(FIFO_IR_OUT_HALF_EMPTY));
		}
		sc->sc_xmit.buf = cur;
	}

	/* drain */
	if (fir & FIFO_IR_EVENT(FIFO_IR_OUT_EMPTY)) {
		rc = 1;
		arcofi_write(sc, ARCOFI_FIFO_IR,
		    arcofi_read(sc, ARCOFI_FIFO_IR) &
		    ~FIFO_IR_ENABLE(FIFO_IR_OUT_EMPTY));
		wakeup(&sc->sc_xmit);
	}

#ifdef ARCOFI_DEBUG
	if (rc == 0)
		printf("%s: unclaimed interrupt, csr %02x fir %02x fsr %02x\n",
		    sc->sc_dev.dv_xname, csr, fir,
		    arcofi_read(sc, ARCOFI_FIFO_SR));
#endif

	return rc;
}

void
arcofi_swintr(void *v)
{
	struct arcofi_softc *sc = (struct arcofi_softc *)v;
	int action;

	action = 0;
	mtx_enter(&audio_lock);
	if (sc->sc_recv.buf != NULL && sc->sc_recv.buf == sc->sc_recv.past)
		action |= AUMODE_RECORD;
	if (sc->sc_xmit.buf != NULL && sc->sc_xmit.buf == sc->sc_xmit.past)
		action |= AUMODE_PLAY;

	if (action & AUMODE_RECORD) {
		if (sc->sc_recv.cb)
			sc->sc_recv.cb(sc->sc_recv.cbarg);
	}
	if (action & AUMODE_PLAY) {
		if (sc->sc_xmit.cb)
			sc->sc_xmit.cb(sc->sc_xmit.cbarg);
	}
	mtx_leave(&audio_lock);
}

int
arcofi_cmd(struct arcofi_softc *sc, uint8_t cmd, const uint8_t *data)
{
	size_t len;
	uint8_t csr;
	int cnt;
	static const uint8_t cmdlen[] = {
	    [SOP_0] = 4,
	    [COP_1] = 4,
	    [COP_2] = 2,
	    [COP_3] = 2,
	    [SOP_4] = 1,
	    [SOP_5] = 1,
	    [SOP_6] = 1,
	    [SOP_7] = 1,
	    [COP_8] = 2,
	    [COP_9] = 4,
	    [COP_A] = 8,
	    [COP_B] = 2,
	    [COP_C] = 8,
	    [COP_D] = 4,
	    [COP_E] = 4
	};

	/*
	 * Compute command length.
	 */
	if (cmd >= nitems(cmdlen))
		return EINVAL;
	len = cmdlen[cmd];

	mtx_enter(&audio_lock);

	/*
	 * Disable all FIFO processing.
	 */
	csr = arcofi_read(sc, ARCOFI_CSR);
	arcofi_write(sc, ARCOFI_CSR,
	    csr & ~(CSR_DATA_FIFO_ENABLE | CSR_CTRL_FIFO_ENABLE));

	/*
	 * Fill the FIFO with the command bytes.
	 */
	arcofi_write(sc, ARCOFI_FIFO_CTRL, CMDR_PU | CMDR_WRITE | cmd);
	for (; len != 0; len--)
		arcofi_write(sc, ARCOFI_FIFO_CTRL, *data++);

	/*
	 * Enable command processing.
	 */
	arcofi_write(sc, ARCOFI_CSR,
	    (csr & ~CSR_DATA_FIFO_ENABLE) | CSR_CTRL_FIFO_ENABLE);

	/*
	 * Wait for the command FIFO to be empty.
	 */
	cnt = 100;
	while ((arcofi_read(sc, ARCOFI_FIFO_SR) & FIFO_SR_CTRL_EMPTY) == 0) {
		if (cnt-- == 0) {
			mtx_leave(&audio_lock);
			return EBUSY;
		}
		delay(10);
	}

	arcofi_write(sc, ARCOFI_CSR, csr);

	mtx_leave(&audio_lock);
	return 0;
}

void
arcofi_attach(struct arcofi_softc *sc, const char *version)
{
	int rc;
	uint8_t cmd[4];

	/*
	 * Reset logic.
	 */
	arcofi_write(sc, ARCOFI_ID, 0);
	delay(100000);
	arcofi_write(sc, ARCOFI_CSR, 0);

	/*
	 * Initialize the chip to default settings (8 bit, u-Law).
	 */
	sc->sc_active.cr3 =
	    arcofi_portmask_to_cr3(AUDIO_SPEAKER) | CR3_OPMODE_NORMAL;
	sc->sc_active.cr4 = CR4_TM | CR4_ULAW;
	sc->sc_active.gr_idx = sc->sc_active.gx_idx = 1 + NEGATIVE_GAINS;
	sc->sc_active.output_mute = 0;
	bcopy(&sc->sc_active, &sc->sc_shadow, sizeof(sc->sc_active));

	/* clear CRAM */
	cmd[0] = CR1_IDR;
	if ((rc = arcofi_cmd(sc, SOP_4, cmd)) != 0)
		goto error;
	delay(1000);

	/* set gain values before enabling them in CR1 */
	cmd[0] = arcofi_gains[sc->sc_active.gr_idx] >> 8;
	cmd[1] = arcofi_gains[sc->sc_active.gr_idx];
	if ((rc = arcofi_cmd(sc, COP_2, cmd)) != 0)
		goto error;
	/* same value for gx... */
	if ((rc = arcofi_cmd(sc, COP_B, cmd)) != 0)
		goto error;

	/* set all CR registers at once */
	cmd[0] = sc->sc_active.cr4;
	cmd[1] = sc->sc_active.cr3;
	cmd[2] = CR2_SD | CR2_SC | CR2_SB | CR2_SA | CR2_ELS | CR2_AM | CR2_EFC;
	cmd[3] = CR1_GR | CR1_GX;
	if ((rc = arcofi_cmd(sc, SOP_0, cmd)) != 0)
		goto error;

	arcofi_write(sc, ARCOFI_FIFO_IR, 0);
	arcofi_write(sc, ARCOFI_CSR, CSR_INTR_ENABLE);

	audio_attach_mi(&arcofi_hw_if, sc, NULL, &sc->sc_dev);
	return;

error:
	arcofi_write(sc, ARCOFI_ID, 0);
	printf("%s: command failed, error %d\n", __func__, rc);
}
