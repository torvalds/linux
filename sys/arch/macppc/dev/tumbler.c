/*	$OpenBSD: tumbler.c,v 1.14 2022/10/26 20:19:07 kn Exp $	*/

/*-
 * Copyright (c) 2001,2003 Tsubai Masanari.  All rights reserved.
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

/*
 * Datasheet is available from
 * http://focus.ti.com/docs/prod/folders/print/tas3001.html
 */

#include <sys/param.h>
#include <sys/audioio.h>
#include <sys/device.h>
#include <sys/systm.h>

#include <dev/audio_if.h>
#include <dev/ofw/openfirm.h>
#include <macppc/dev/dbdma.h>

#include <machine/autoconf.h>

#include <macppc/dev/i2svar.h>

#ifdef TUMBLER_DEBUG
# define DPRINTF printf
#else
# define DPRINTF while (0) printf
#endif

/* XXX */
#define tumbler_softc i2s_softc

/* XXX */
int kiic_write(struct device *, int, int, const void *, int);
int kiic_writereg(struct device *, int, u_int);

void tumbler_init(struct tumbler_softc *);
int tumbler_match(struct device *, void *, void *);
void tumbler_attach(struct device *, struct device *, void *);
void tumbler_defer(struct device *);
void tumbler_set_volume(struct tumbler_softc *, int, int);
void tumbler_set_bass(struct tumbler_softc *, int);
void tumbler_set_treble(struct tumbler_softc *, int);

int tas3001_write(struct tumbler_softc *, u_int, const void *);
int tas3001_init(struct tumbler_softc *);

const struct cfattach tumbler_ca = {
	sizeof(struct tumbler_softc), tumbler_match, tumbler_attach
};
struct cfdriver tumbler_cd = {
	NULL, "tumbler", DV_DULL
};

const struct audio_hw_if tumbler_hw_if = {
	.open = i2s_open,
	.close = i2s_close,
	.set_params = i2s_set_params,
	.round_blocksize = i2s_round_blocksize,
	.halt_output = i2s_halt_output,
	.halt_input = i2s_halt_input,
	.set_port = i2s_set_port,
	.get_port = i2s_get_port,
	.query_devinfo = i2s_query_devinfo,
	.allocm = i2s_allocm,
	.round_buffersize = i2s_round_buffersize,
	.trigger_output = i2s_trigger_output,
	.trigger_input = i2s_trigger_input,
};

const uint8_t tumbler_trebletab[] = {
	0x96,	/* -18dB */
	0x94,	/* -17dB */
	0x92,	/* -16dB */
	0x90,	/* -15dB */
	0x8e,	/* -14dB */
	0x8c,	/* -13dB */
	0x8a,	/* -12dB */
	0x88,	/* -11dB */
	0x86,	/* -10dB */
	0x84,	/* -9dB */
	0x82,	/* -8dB */
	0x80,	/* -7dB */
	0x7e,	/* -6dB */
	0x7c,	/* -5dB */
	0x7a,	/* -4dB */
	0x78,	/* -3dB */
	0x76,	/* -2dB */
	0x74,	/* -1dB */
	0x72,	/* 0dB */
	0x70,	/* 1dB */
	0x6d,	/* 2dB */
	0x6b,	/* 3dB */
	0x68,	/* 4dB */
	0x65,	/* 5dB */
	0x62,	/* 6dB */
	0x5e,	/* 7dB */
	0x59,	/* 8dB */
	0x5a,	/* 9dB */
	0x4f,	/* 10dB */
	0x49,	/* 11dB */
	0x42,	/* 12dB */
	0x3a,	/* 13dB */
	0x32,	/* 14dB */
	0x28,	/* 15dB */
	0x1c,	/* 16dB */
	0x10,	/* 17dB */
	0x01,	/* 18dB */
};

const uint8_t tumbler_basstab[] = {
	0x86,	/* -18dB */
	0x7f,	/* -17dB */
	0x7a,	/* -16dB */
	0x76,	/* -15dB */
	0x72,	/* -14dB */
	0x6e,	/* -13dB */
	0x6b,	/* -12dB */
	0x66,	/* -11dB */
	0x61,	/* -10dB */
	0x5d,	/* -9dB */
	0x5a,	/* -8dB */
	0x58,	/* -7dB */
	0x55,	/* -6dB */
	0x53,	/* -5dB */
	0x4f,	/* -4dB */
	0x4b,	/* -3dB */
	0x46,	/* -2dB */
	0x42,	/* -1dB */
	0x3e,	/* 0dB */
	0x3b,	/* 1dB */
	0x38,	/* 2dB */
	0x35,	/* 3dB */
	0x31,	/* 4dB */
	0x2e,	/* 5dB */
	0x2b,	/* 6dB */
	0x28,	/* 7dB */
	0x25,	/* 8dB */
	0x21,	/* 9dB */
	0x1c,	/* 10dB */
	0x18,	/* 11dB */
	0x16,	/* 12dB */
	0x13,	/* 13dB */
	0x10,	/* 14dB */
	0x0d,	/* 15dB */
	0x0a,	/* 16dB */
	0x06,	/* 17dB */
	0x01,	/* 18dB */
};

/* TAS3001 registers */
#define DEQ_MCR		0x01	/* Main Control Register (1byte) */
#define DEQ_DRC		0x02	/* Dynamic Range Compression (2bytes) */
#define DEQ_VOLUME	0x04	/* Volume (6bytes) */
#define DEQ_TREBLE	0x05	/* Treble Control Register (1byte) */
#define DEQ_BASS	0x06	/* Bass Control Register (1byte) */
#define DEQ_MIXER1	0x07	/* Mixer 1 (3bytes) */
#define DEQ_MIXER2	0x08	/* Mixer 2 (3bytes) */
#define DEQ_LB0		0x0a	/* Left Biquad 0 (15bytes) */
#define DEQ_LB1		0x0b	/* Left Biquad 1 (15bytes) */
#define DEQ_LB2		0x0c	/* Left Biquad 2 (15bytes) */
#define DEQ_LB3		0x0d	/* Left Biquad 3 (15bytes) */
#define DEQ_LB4		0x0e	/* Left Biquad 4 (15bytes) */
#define DEQ_LB5		0x0f	/* Left Biquad 5 (15bytes) */
#define DEQ_RB0		0x13	/* Right Biquad 0 (15bytes) */
#define DEQ_RB1		0x14	/* Right Biquad 1 (15bytes) */
#define DEQ_RB2		0x15	/* Right Biquad 2 (15bytes) */
#define DEQ_RB3		0x16	/* Right Biquad 3 (15bytes) */
#define DEQ_RB4		0x17	/* Right Biquad 4 (15bytes) */
#define DEQ_RB5		0x18	/* Right Biquad 5 (15bytes) */

#define DEQ_MCR_FL	0x80	/* Fast load */
#define DEQ_MCR_SC	0x40	/* SCLK frequency */
#define  DEQ_MCR_SC_32	0x00	/*  32fs */
#define  DEQ_MCR_SC_64	0x40	/*  64fs */
#define DEQ_MCR_OM	0x30	/* Output serial port mode */
#define  DEQ_MCR_OM_L	0x00	/*  Left justified */
#define  DEQ_MCR_OM_R	0x10	/*  Right justified */
#define  DEQ_MCR_OM_I2S	0x20	/*  I2S */
#define DEQ_MCR_IM	0x0c	/* Input serial port mode */
#define  DEQ_MCR_IM_L	0x00	/*  Left justified */
#define  DEQ_MCR_IM_R	0x04	/*  Right justified */
#define  DEQ_MCR_IM_I2S	0x08	/*  I2S */
#define DEQ_MCR_W	0x03	/* Serial port word length */
#define  DEQ_MCR_W_16	0x00	/*  16 bit */
#define  DEQ_MCR_W_18	0x01	/*  18 bit */
#define  DEQ_MCR_W_20	0x02	/*  20 bit */

#define DEQ_DRC_CR	0xc0	/* Compression ratio */
#define  DEQ_DRC_CR_31	0xc0	/*  3:1 */
#define DEQ_DRC_EN	0x01	/* Enable DRC */

#define DEQ_MCR_I2S	(DEQ_MCR_OM_I2S | DEQ_MCR_IM_I2S)

struct tas3001_reg {
	u_char MCR[1];
	u_char DRC[2];
	u_char VOLUME[6];
	u_char TREBLE[1];
	u_char BASS[1];
	u_char MIXER1[3];
	u_char MIXER2[3];
	u_char LB0[15];
	u_char LB1[15];
	u_char LB2[15];
	u_char LB3[15];
	u_char LB4[15];
	u_char LB5[15];
	u_char RB0[15];
	u_char RB1[15];
	u_char RB2[15];
	u_char RB3[15];
	u_char RB4[15];
	u_char RB5[15];
};

int
tumbler_match(struct device *parent, void *match, void *aux)
{
	struct confargs *ca = aux;
	int soundbus, soundchip;
	char compat[32];

	if (strcmp(ca->ca_name, "i2s") != 0)
		return (0);

	if ((soundbus = OF_child(ca->ca_node)) == 0 ||
	    (soundchip = OF_child(soundbus)) == 0)
		return (0);

	bzero(compat, sizeof compat);
	OF_getprop(soundchip, "compatible", compat, sizeof compat);

	if (strcmp(compat, "tumbler") != 0)
		return (0);

	return (1);
}

void
tumbler_attach(struct device *parent, struct device *self, void *aux)
{
	struct tumbler_softc *sc = (struct tumbler_softc *)self;

	sc->sc_setvolume = tumbler_set_volume;
	sc->sc_setbass = tumbler_set_bass;
	sc->sc_settreble = tumbler_set_treble;

	i2s_attach(parent, sc, aux);
	config_defer(self, tumbler_defer);
}

void
tumbler_defer(struct device *dev)
{
	struct tumbler_softc *sc = (struct tumbler_softc *)dev;
	struct device *dv;

	TAILQ_FOREACH(dv, &alldevs, dv_list)
		if (strcmp(dv->dv_cfdata->cf_driver->cd_name, "kiic") == 0 &&
		    strcmp(dv->dv_parent->dv_cfdata->cf_driver->cd_name, "macobio") == 0)
			sc->sc_i2c = dv;
	if (sc->sc_i2c == NULL) {
		printf("%s: unable to find i2c\n", sc->sc_dev.dv_xname);
		return;
	}

	/* XXX If i2c has failed to attach, what should we do? */

	audio_attach_mi(&tumbler_hw_if, sc, NULL, &sc->sc_dev);

	tumbler_init(sc);
}

void
tumbler_set_volume(struct tumbler_softc *sc, int left, int right)
{
	u_char vol[6];

	sc->sc_vol_l = left;
	sc->sc_vol_r = right;

	left <<= 6;	/* XXX for now */
	right <<= 6;

	vol[0] = left >> 16;
	vol[1] = left >> 8;
	vol[2] = left;
	vol[3] = right >> 16;
	vol[4] = right >> 8;
	vol[5] = right;

	tas3001_write(sc, DEQ_VOLUME, vol);
}

void
tumbler_set_treble(struct tumbler_softc *sc, int value)
{
	uint8_t reg;

	if ((value >= 0) && (value <= 255) && (value != sc->sc_treble)) {
		reg = tumbler_trebletab[(value >> 3) + 2];
		if (tas3001_write(sc, DEQ_TREBLE, &reg) < 0)
			return;
		sc->sc_treble = value;
	}
}

void
tumbler_set_bass(struct tumbler_softc *sc, int value)
{
	uint8_t reg;

	if ((value >= 0) && (value <= 255) && (value != sc->sc_bass)) {
		reg = tumbler_basstab[(value >> 3) + 2];
		if (tas3001_write(sc, DEQ_BASS, &reg) < 0)
			return;
		sc->sc_bass = value;
	}
}

const struct tas3001_reg tas3001_initdata = {
	{ DEQ_MCR_SC_64 | DEQ_MCR_I2S | DEQ_MCR_W_20 },		/* MCR */
	{ DEQ_DRC_CR_31, 0xa0 },				/* DRC */
	{ 0, 0, 0, 0, 0, 0 },					/* VOLUME */
	{ 0x72 },						/* TREBLE */
	{ 0x3e },						/* BASS */
	{ 0x10, 0x00, 0x00 },					/* MIXER1 */
	{ 0x00, 0x00, 0x00 },					/* MIXER2 */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
};

const char tas3001_regsize[] = {
	0,					/* 0x00 */
	sizeof tas3001_initdata.MCR,		/* 0x01 */
	sizeof tas3001_initdata.DRC,		/* 0x02 */
	0,					/* 0x03 */
	sizeof tas3001_initdata.VOLUME,		/* 0x04 */
	sizeof tas3001_initdata.TREBLE,		/* 0x05 */
	sizeof tas3001_initdata.BASS,		/* 0x06 */
	sizeof tas3001_initdata.MIXER1,		/* 0x07 */
	sizeof tas3001_initdata.MIXER2,		/* 0x08 */
	0,					/* 0x09 */
	sizeof tas3001_initdata.LB0,		/* 0x0a */
	sizeof tas3001_initdata.LB1,		/* 0x0b */
	sizeof tas3001_initdata.LB2,		/* 0x0c */
	sizeof tas3001_initdata.LB3,		/* 0x0d */
	sizeof tas3001_initdata.LB4,		/* 0x0e */
	sizeof tas3001_initdata.LB5,		/* 0x0f */
	0,					/* 0x10 */
	0,					/* 0x11 */
	0,					/* 0x12 */
	sizeof tas3001_initdata.RB0,		/* 0x13 */
	sizeof tas3001_initdata.RB1,		/* 0x14 */
	sizeof tas3001_initdata.RB2,		/* 0x15 */
	sizeof tas3001_initdata.RB3,		/* 0x16 */
	sizeof tas3001_initdata.RB4,		/* 0x17 */
	sizeof tas3001_initdata.RB5		/* 0x18 */
};

#define DEQaddr 0x68

int
tas3001_write(struct tumbler_softc *sc, u_int reg, const void *data)
{
	int size;

	KASSERT(reg < sizeof tas3001_regsize);
	size = tas3001_regsize[reg];
	KASSERT(size > 0);

	if (kiic_write(sc->sc_i2c, DEQaddr, reg, data, size))
		return (-1);

	return (0);
}

#define DEQ_WRITE(sc, reg, addr) \
	if (tas3001_write(sc, reg, addr)) goto err

int
tas3001_init(struct tumbler_softc *sc)
{
	deq_reset(sc);

	/* Initialize TAS3001 registers. */
	DEQ_WRITE(sc, DEQ_LB0, tas3001_initdata.LB0);
	DEQ_WRITE(sc, DEQ_LB1, tas3001_initdata.LB1);
	DEQ_WRITE(sc, DEQ_LB2, tas3001_initdata.LB2);
	DEQ_WRITE(sc, DEQ_LB3, tas3001_initdata.LB3);
	DEQ_WRITE(sc, DEQ_LB4, tas3001_initdata.LB4);
	DEQ_WRITE(sc, DEQ_LB5, tas3001_initdata.LB5);
	DEQ_WRITE(sc, DEQ_RB0, tas3001_initdata.RB0);
	DEQ_WRITE(sc, DEQ_RB1, tas3001_initdata.RB1);
	DEQ_WRITE(sc, DEQ_RB1, tas3001_initdata.RB1);
	DEQ_WRITE(sc, DEQ_RB2, tas3001_initdata.RB2);
	DEQ_WRITE(sc, DEQ_RB3, tas3001_initdata.RB3);
	DEQ_WRITE(sc, DEQ_RB4, tas3001_initdata.RB4);
	DEQ_WRITE(sc, DEQ_MCR, tas3001_initdata.MCR);
	DEQ_WRITE(sc, DEQ_DRC, tas3001_initdata.DRC);
	DEQ_WRITE(sc, DEQ_VOLUME, tas3001_initdata.VOLUME);
	DEQ_WRITE(sc, DEQ_TREBLE, tas3001_initdata.TREBLE);
	DEQ_WRITE(sc, DEQ_BASS, tas3001_initdata.BASS);
	DEQ_WRITE(sc, DEQ_MIXER1, tas3001_initdata.MIXER1);
	DEQ_WRITE(sc, DEQ_MIXER2, tas3001_initdata.MIXER2);

	return (0);
err:
	printf("%s: tas3001_init: error\n", sc->sc_dev.dv_xname);
	return (-1);
}

void
tumbler_init(struct tumbler_softc *sc)
{

	/* "sample-rates" (44100, 48000) */
	i2s_set_rate(sc, 44100);

#if 1
	/* Enable I2C interrupts. */
#define IER 4
#define I2C_INT_DATA 0x01
#define I2C_INT_ADDR 0x02
#define I2C_INT_STOP 0x04
	kiic_writereg(sc->sc_i2c, IER,I2C_INT_DATA|I2C_INT_ADDR|I2C_INT_STOP);
#endif

	if (tas3001_init(sc))
		return;

	tumbler_set_volume(sc, 80, 80);
}
