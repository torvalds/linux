/*	$OpenBSD: snapper.c,v 1.44 2022/10/26 20:19:07 kn Exp $	*/
/*	$NetBSD: snapper.c,v 1.1 2003/12/27 02:19:34 grant Exp $	*/

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

/*
 * Datasheet is available from
 * http://focus.ti.com/docs/prod/folders/print/tas3004.html
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

#ifdef SNAPPER_DEBUG
# define DPRINTF printf
#else
# define DPRINTF while (0) printf
#endif

/* XXX */
#define snapper_softc i2s_softc

/* XXX */
int kiic_write(struct device *, int, int, const void *, int);
int kiic_writereg(struct device *, int, u_int);

void snapper_init(struct snapper_softc *);
int snapper_match(struct device *, void *, void *);
void snapper_attach(struct device *, struct device *, void *);
void snapper_defer(struct device *);
void snapper_set_volume(struct snapper_softc *, int, int);
void snapper_set_bass(struct snapper_softc *, int);
void snapper_set_treble(struct snapper_softc *, int);
void snapper_set_input(struct snapper_softc *, int);

int tas3004_write(struct snapper_softc *, u_int, const void *);
int tas3004_init(struct snapper_softc *);

const struct cfattach snapper_ca = {
	sizeof(struct snapper_softc), snapper_match, snapper_attach
};
struct cfdriver snapper_cd = {
	NULL, "snapper", DV_DULL
};

const struct audio_hw_if snapper_hw_if = {
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

const uint8_t snapper_trebletab[] = {
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
	0x5d,	/* 7dB */
	0x59,	/* 8dB */
	0x53,	/* 9dB */
	0x4d,	/* 10dB */
	0x47,	/* 11dB */
	0x3f,	/* 12dB */
	0x36,	/* 13dB */
	0x2c,	/* 14dB */
	0x20,	/* 15dB */
	0x13,	/* 16dB */
	0x04,	/* 17dB */
	0x01,	/* 18dB */
};

const uint8_t snapper_basstab[] = {
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
	0x6f,	/* 1dB */
	0x6d,	/* 2dB */
	0x6a,	/* 3dB */
	0x67,	/* 4dB */
	0x65,	/* 5dB */
	0x62,	/* 6dB */
	0x5f,	/* 7dB */
	0x5b,	/* 8dB */
	0x55,	/* 9dB */
	0x4f,	/* 10dB */
	0x49,	/* 11dB */
	0x43,	/* 12dB */
	0x3b,	/* 13dB */
	0x33,	/* 14dB */
	0x29,	/* 15dB */
	0x1e,	/* 16dB */
	0x11,	/* 17dB */
	0x01,	/* 18dB */
};

struct {
	int high, mid, low;
} snapper_volumetab[] = {
	{ 0x07, 0xF1, 0x7B }, /* 18.0 */
	{ 0x07, 0x7F, 0xBB }, /* 17.5 */
	{ 0x07, 0x14, 0x57 }, /* 17.0 */
	{ 0x06, 0xAE, 0xF6 }, /* 16.5 */
	{ 0x06, 0x4F, 0x40 }, /* 16.0 */
	{ 0x05, 0xF4, 0xE5 }, /* 15.5 */
	{ 0x05, 0x9F, 0x98 }, /* 15.0 */
	{ 0x05, 0x4F, 0x10 }, /* 14.5 */
	{ 0x05, 0x03, 0x0A }, /* 14.0 */
	{ 0x04, 0xBB, 0x44 }, /* 13.5 */
	{ 0x04, 0x77, 0x83 }, /* 13.0 */
	{ 0x04, 0x37, 0x8B }, /* 12.5 */
	{ 0x03, 0xFB, 0x28 }, /* 12.0 */
	{ 0x03, 0xC2, 0x25 }, /* 11.5 */
	{ 0x03, 0x8C, 0x53 }, /* 11.0 */
	{ 0x03, 0x59, 0x83 }, /* 10.5 */
	{ 0x03, 0x29, 0x8B }, /* 10.0 */
	{ 0x02, 0xFC, 0x42 }, /* 9.5 */
	{ 0x02, 0xD1, 0x82 }, /* 9.0 */
	{ 0x02, 0xA9, 0x25 }, /* 8.5 */
	{ 0x02, 0x83, 0x0B }, /* 8.0 */
	{ 0x02, 0x5F, 0x12 }, /* 7.5 */
	{ 0x02, 0x3D, 0x1D }, /* 7.0 */
	{ 0x02, 0x1D, 0x0E }, /* 6.5 */
	{ 0x01, 0xFE, 0xCA }, /* 6.0 */
	{ 0x01, 0xE2, 0x37 }, /* 5.5 */
	{ 0x01, 0xC7, 0x3D }, /* 5.0 */
	{ 0x01, 0xAD, 0xC6 }, /* 4.5 */
	{ 0x01, 0x95, 0xBC }, /* 4.0 */
	{ 0x01, 0x7F, 0x09 }, /* 3.5 */
	{ 0x01, 0x69, 0x9C }, /* 3.0 */
	{ 0x01, 0x55, 0x62 }, /* 2.5 */
	{ 0x01, 0x42, 0x49 }, /* 2.0 */
	{ 0x01, 0x30, 0x42 }, /* 1.5 */
	{ 0x01, 0x1F, 0x3D }, /* 1.0 */
	{ 0x01, 0x0F, 0x2B }, /* 0.5 */
	{ 0x01, 0x00, 0x00 }, /* 0.0 */
	{ 0x00, 0xF1, 0xAE }, /* -0.5 */
	{ 0x00, 0xE4, 0x29 }, /* -1.0 */
	{ 0x00, 0xD7, 0x66 }, /* -1.5 */
	{ 0x00, 0xCB, 0x59 }, /* -2.0 */
	{ 0x00, 0xBF, 0xF9 }, /* -2.5 */
	{ 0x00, 0xB5, 0x3C }, /* -3.0 */
	{ 0x00, 0xAB, 0x19 }, /* -3.5 */
	{ 0x00, 0xA1, 0x86 }, /* -4.0 */
	{ 0x00, 0x98, 0x7D }, /* -4.5 */
	{ 0x00, 0x8F, 0xF6 }, /* -5.0 */
	{ 0x00, 0x87, 0xE8 }, /* -5.5 */
	{ 0x00, 0x80, 0x4E }, /* -6.0 */
	{ 0x00, 0x79, 0x20 }, /* -6.5 */
	{ 0x00, 0x72, 0x5A }, /* -7.0 */
	{ 0x00, 0x6B, 0xF4 }, /* -7.5 */
	{ 0x00, 0x65, 0xEA }, /* -8.0 */
	{ 0x00, 0x60, 0x37 }, /* -8.5 */
	{ 0x00, 0x5A, 0xD5 }, /* -9.0 */
	{ 0x00, 0x55, 0xC0 }, /* -9.5 */
	{ 0x00, 0x50, 0xF4 }, /* -10.0 */
	{ 0x00, 0x4C, 0x6D }, /* -10.5 */
	{ 0x00, 0x48, 0x27 }, /* -11.0 */
	{ 0x00, 0x44, 0x1D }, /* -11.5 */
	{ 0x00, 0x40, 0x4E }, /* -12.0 */
	{ 0x00, 0x3C, 0xB5 }, /* -12.5 */
	{ 0x00, 0x39, 0x50 }, /* -13.0 */
	{ 0x00, 0x36, 0x1B }, /* -13.5 */
	{ 0x00, 0x33, 0x14 }, /* -14.0 */
	{ 0x00, 0x30, 0x39 }, /* -14.5 */
	{ 0x00, 0x2D, 0x86 }, /* -15.0 */
	{ 0x00, 0x2A, 0xFA }, /* -15.5 */
	{ 0x00, 0x28, 0x93 }, /* -16.0 */
	{ 0x00, 0x26, 0x4E }, /* -16.5 */
	{ 0x00, 0x24, 0x29 }, /* -17.0 */
	{ 0x00, 0x22, 0x23 }, /* -17.5 */
	{ 0x00, 0x20, 0x3A }, /* -18.0 */
	{ 0x00, 0x1E, 0x6D }, /* -18.5 */
	{ 0x00, 0x1C, 0xB9 }, /* -19.0 */
	{ 0x00, 0x1B, 0x1E }, /* -19.5 */
	{ 0x00, 0x19, 0x9A }, /* -20.0 */
	{ 0x00, 0x18, 0x2B }, /* -20.5 */
	{ 0x00, 0x16, 0xD1 }, /* -21.0 */
	{ 0x00, 0x15, 0x8A }, /* -21.5 */
	{ 0x00, 0x14, 0x56 }, /* -22.0 */
	{ 0x00, 0x13, 0x33 }, /* -22.5 */
	{ 0x00, 0x12, 0x20 }, /* -23.0 */
	{ 0x00, 0x11, 0x1C }, /* -23.5 */
	{ 0x00, 0x10, 0x27 }, /* -24.0 */
	{ 0x00, 0x0F, 0x40 }, /* -24.5 */
	{ 0x00, 0x0E, 0x65 }, /* -25.0 */
	{ 0x00, 0x0D, 0x97 }, /* -25.5 */
	{ 0x00, 0x0C, 0xD5 }, /* -26.0 */
	{ 0x00, 0x0C, 0x1D }, /* -26.5 */
	{ 0x00, 0x0B, 0x6F }, /* -27.0 */
	{ 0x00, 0x0A, 0xCC }, /* -27.5 */
	{ 0x00, 0x0A, 0x31 }, /* -28.0 */
	{ 0x00, 0x09, 0x9F }, /* -28.5 */
	{ 0x00, 0x09, 0x15 }, /* -29.0 */
	{ 0x00, 0x08, 0x93 }, /* -29.5 */
	{ 0x00, 0x08, 0x18 }, /* -30.0 */
	{ 0x00, 0x07, 0xA5 }, /* -30.5 */
	{ 0x00, 0x07, 0x37 }, /* -31.0 */
	{ 0x00, 0x06, 0xD0 }, /* -31.5 */
	{ 0x00, 0x06, 0x6E }, /* -32.0 */
	{ 0x00, 0x06, 0x12 }, /* -32.5 */
	{ 0x00, 0x05, 0xBB }, /* -33.0 */
	{ 0x00, 0x05, 0x69 }, /* -33.5 */
	{ 0x00, 0x05, 0x1C }, /* -34.0 */
	{ 0x00, 0x04, 0xD2 }, /* -34.5 */
	{ 0x00, 0x04, 0x8D }, /* -35.0 */
	{ 0x00, 0x04, 0x4C }, /* -35.5 */
	{ 0x00, 0x04, 0x0F }, /* -36.0 */
	{ 0x00, 0x03, 0xD5 }, /* -36.5 */
	{ 0x00, 0x03, 0x9E }, /* -37.0 */
	{ 0x00, 0x03, 0x6A }, /* -37.5 */
	{ 0x00, 0x03, 0x39 }, /* -38.0 */
	{ 0x00, 0x03, 0x0B }, /* -38.5 */
	{ 0x00, 0x02, 0xDF }, /* -39.0 */
	{ 0x00, 0x02, 0xB6 }, /* -39.5 */
	{ 0x00, 0x02, 0x8F }, /* -40.0 */
	{ 0x00, 0x02, 0x6B }, /* -40.5 */
	{ 0x00, 0x02, 0x48 }, /* -41.0 */
	{ 0x00, 0x02, 0x27 }, /* -41.5 */
	{ 0x00, 0x02, 0x09 }, /* -42.0 */
	{ 0x00, 0x01, 0xEB }, /* -42.5 */
	{ 0x00, 0x01, 0xD0 }, /* -43.0 */
	{ 0x00, 0x01, 0xB6 }, /* -43.5 */
	{ 0x00, 0x01, 0x9E }, /* -44.0 */
	{ 0x00, 0x01, 0x86 }, /* -44.5 */
	{ 0x00, 0x01, 0x71 }, /* -45.0 */
	{ 0x00, 0x01, 0x5C }, /* -45.5 */
	{ 0x00, 0x01, 0x48 }, /* -46.0 */
	{ 0x00, 0x01, 0x36 }, /* -46.5 */
	{ 0x00, 0x01, 0x25 }, /* -47.0 */
	{ 0x00, 0x01, 0x14 }, /* -47.5 */
	{ 0x00, 0x01, 0x05 }, /* -48.0 */
	{ 0x00, 0x00, 0xF6 }, /* -48.5 */
	{ 0x00, 0x00, 0xE9 }, /* -49.0 */
	{ 0x00, 0x00, 0xDC }, /* -49.5 */
	{ 0x00, 0x00, 0xCF }, /* -50.0 */
	{ 0x00, 0x00, 0xC4 }, /* -50.5 */
	{ 0x00, 0x00, 0xB9 }, /* -51.0 */
	{ 0x00, 0x00, 0xAE }, /* -51.5 */
	{ 0x00, 0x00, 0xA5 }, /* -52.0 */
	{ 0x00, 0x00, 0x9B }, /* -52.5 */
	{ 0x00, 0x00, 0x93 }, /* -53.0 */
	{ 0x00, 0x00, 0x8B }, /* -53.5 */
	{ 0x00, 0x00, 0x83 }, /* -54.0 */
	{ 0x00, 0x00, 0x7B }, /* -54.5 */
	{ 0x00, 0x00, 0x75 }, /* -55.0 */
	{ 0x00, 0x00, 0x6E }, /* -55.5 */
	{ 0x00, 0x00, 0x68 }, /* -56.0 */
	{ 0x00, 0x00, 0x62 }, /* -56.5 */
	{ 0x00, 0x00, 0x0 } /* Mute? */

};

/* TAS3004 registers */
#define DEQ_MCR1	0x01	/* Main control register 1 (1byte) */
#define DEQ_DRC		0x02	/* Dynamic range compression (6bytes?) */
#define DEQ_VOLUME	0x04	/* Volume (6bytes) */
#define DEQ_TREBLE	0x05	/* Treble control (1byte) */
#define DEQ_BASS	0x06	/* Bass control (1byte) */
#define DEQ_MIXER_L	0x07	/* Mixer left gain (9bytes) */
#define DEQ_MIXER_R	0x08	/* Mixer right gain (9bytes) */
#define DEQ_LB0		0x0a	/* Left biquad 0 (15bytes) */
#define DEQ_LB1		0x0b	/* Left biquad 1 (15bytes) */
#define DEQ_LB2		0x0c	/* Left biquad 2 (15bytes) */
#define DEQ_LB3		0x0d	/* Left biquad 3 (15bytes) */
#define DEQ_LB4		0x0e	/* Left biquad 4 (15bytes) */
#define DEQ_LB5		0x0f	/* Left biquad 5 (15bytes) */
#define DEQ_LB6		0x10	/* Left biquad 6 (15bytes) */
#define DEQ_RB0		0x13	/* Right biquad 0 (15bytes) */
#define DEQ_RB1		0x14	/* Right biquad 1 (15bytes) */
#define DEQ_RB2		0x15	/* Right biquad 2 (15bytes) */
#define DEQ_RB3		0x16	/* Right biquad 3 (15bytes) */
#define DEQ_RB4		0x17	/* Right biquad 4 (15bytes) */
#define DEQ_RB5		0x18	/* Right biquad 5 (15bytes) */
#define DEQ_RB6		0x19	/* Right biquad 6 (15bytes) */
#define DEQ_LLB		0x21	/* Left loudness biquad (15bytes) */
#define DEQ_RLB		0x22	/* Right loudness biquad (15bytes) */
#define DEQ_LLB_GAIN	0x23	/* Left loudness biquad gain (3bytes) */
#define DEQ_RLB_GAIN	0x24	/* Right loudness biquad gain (3bytes) */
#define DEQ_ACR		0x40	/* Analog control register (1byte) */
#define DEQ_MCR2	0x43	/* Main control register 2 (1byte) */

#define DEQ_MCR1_FL	0x80	/* Fast load */
#define DEQ_MCR1_SC	0x40	/* SCLK frequency */
#define  DEQ_MCR1_SC_32	0x00	/*  32fs */
#define  DEQ_MCR1_SC_64	0x40	/*  64fs */
#define DEQ_MCR1_SM	0x30	/* Output serial port mode */
#define  DEQ_MCR1_SM_L	0x00	/*  Left justified */
#define  DEQ_MCR1_SM_R	0x10	/*  Right justified */
#define  DEQ_MCR1_SM_I2S 0x20	/*  I2S */
#define DEQ_MCR1_W	0x03	/* Serial port word length */
#define  DEQ_MCR1_W_16	0x00	/*  16 bit */
#define  DEQ_MCR1_W_18	0x01	/*  18 bit */
#define  DEQ_MCR1_W_20	0x02	/*  20 bit */

#define DEQ_MCR2_DL	0x80	/* Download */
#define DEQ_MCR2_AP	0x02	/* All pass mode */

#define DEQ_ACR_ADM	0x80	/* ADC output mode */
#define DEQ_ACR_LRB	0x40	/* Select B input */
#define DEQ_ACR_DM	0x0c	/* De-emphasis control */
#define  DEQ_ACR_DM_OFF	0x00	/*  off */
#define  DEQ_ACR_DM_48	0x04	/*  fs = 48kHz */
#define  DEQ_ACR_DM_44	0x08	/*  fs = 44.1kHz */
#define DEQ_ACR_INP	0x02	/* Analog input select */
#define  DEQ_ACR_INP_A	0x00	/*  A */
#define  DEQ_ACR_INP_B	0x02	/*  B */
#define DEQ_ACR_APD	0x01	/* Analog power down */

struct tas3004_reg {
	u_char MCR1[1];
	u_char DRC[6];
	u_char VOLUME[6];
	u_char TREBLE[1];
	u_char BASS[1];
	u_char MIXER_L[9];
	u_char MIXER_R[9];
	u_char LB0[15];
	u_char LB1[15];
	u_char LB2[15];
	u_char LB3[15];
	u_char LB4[15];
	u_char LB5[15];
	u_char LB6[15];
	u_char RB0[15];
	u_char RB1[15];
	u_char RB2[15];
	u_char RB3[15];
	u_char RB4[15];
	u_char RB5[15];
	u_char RB6[15];
	u_char LLB[15];
	u_char RLB[15];
	u_char LLB_GAIN[3];
	u_char RLB_GAIN[3];
	u_char ACR[1];
	u_char MCR2[1];
};

int
snapper_match(struct device *parent, void *match, void *aux)
{
	struct confargs *ca = aux;
	int soundbus, soundchip, soundcodec;
	char compat[32];

	if (strcmp(ca->ca_name, "i2s") != 0)
		return (0);

	if ((soundbus = OF_child(ca->ca_node)) == 0 ||
	    (soundchip = OF_child(soundbus)) == 0)
		return (0);

	bzero(compat, sizeof compat);
	OF_getprop(soundchip, "compatible", compat, sizeof compat);

	if (strcmp(compat, "AOAKeylargo") == 0 &&
	    strcmp(hw_prod, "PowerBook5,4") == 0)
		return (1);
	if (strcmp(compat, "snapper") == 0)
		return (1);

	if (OF_getprop(soundchip, "platform-tas-codec-ref",
	    &soundcodec, sizeof soundcodec) == sizeof soundcodec)
		return (1);

	return (0);
}

void
snapper_attach(struct device *parent, struct device *self, void *aux)
{
	struct snapper_softc *sc = (struct snapper_softc *)self;

	sc->sc_setvolume = snapper_set_volume;
	sc->sc_setbass = snapper_set_bass;
	sc->sc_settreble = snapper_set_treble;
	sc->sc_setinput = snapper_set_input;

	i2s_attach(parent, sc, aux);
	config_defer(self, snapper_defer);
}

void
snapper_defer(struct device *dev)
{
	struct snapper_softc *sc = (struct snapper_softc *)dev;
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

	audio_attach_mi(&snapper_hw_if, sc, NULL, &sc->sc_dev);

	/* kiic_setmode(sc->sc_i2c, I2C_STDSUBMODE); */
	snapper_init(sc);
}

void
snapper_set_volume(struct snapper_softc *sc, int left, int right)
{
	u_char vol[6];
	int nentries = sizeof(snapper_volumetab) / sizeof(snapper_volumetab[0]);
	int l, r;

	sc->sc_vol_l = left;
	sc->sc_vol_r = right;

	l = nentries - (left * nentries / 256);
	r = nentries - (right * nentries / 256);

	DPRINTF(" left %d vol %d %d, right %d vol %d %d\n",
		left, l, nentries,
		right, r, nentries);
	if (l >= nentries)
		l = nentries-1;
	if (r >= nentries)
		r = nentries-1;

	vol[0] = snapper_volumetab[l].high;
	vol[1] = snapper_volumetab[l].mid;
	vol[2] = snapper_volumetab[l].low;
	vol[3] = snapper_volumetab[r].high;
	vol[4] = snapper_volumetab[r].mid;
	vol[5] = snapper_volumetab[r].low;

	tas3004_write(sc, DEQ_VOLUME, vol);
}

void
snapper_set_treble(struct snapper_softc *sc, int value)
{
	uint8_t reg;

	if ((value >= 0) && (value <= 255) && (value != sc->sc_treble)) {
		reg = snapper_trebletab[(value >> 3) + 2];
		if (tas3004_write(sc, DEQ_TREBLE, &reg) < 0)
			return;
		sc->sc_treble = value;
	}
}

void
snapper_set_bass(struct snapper_softc *sc, int value)
{
	uint8_t reg;

	if ((value >= 0) && (value <= 255) && (value != sc->sc_bass)) {
		reg = snapper_basstab[(value >> 3) + 2];
		if (tas3004_write(sc, DEQ_BASS, &reg) < 0)
			return;
		sc->sc_bass = value;
	}
}

void
snapper_set_input(struct snapper_softc *sc, int mask)
{
	uint8_t val = 0;

	switch (mask) {
	case    1 << 0: /* microphone */
		val = DEQ_ACR_ADM | DEQ_ACR_LRB | DEQ_ACR_INP_B;
		break;
	case    1 << 1: /* line in */
		val = 0;
		break;
	}
	tas3004_write(sc, DEQ_ACR, &val);
}

const struct tas3004_reg tas3004_initdata = {
	{ DEQ_MCR1_SC_64 | DEQ_MCR1_SM_I2S | DEQ_MCR1_W_20 },	/* MCR1 */
	{ 1, 0, 0, 0, 0, 0 },					/* DRC */
	{ 0x00, 0xd7, 0x66, 0x00, 0xd7, 0x66 },			/* VOLUME */
	{ 0x72 },						/* TREBLE */
	{ 0x72 },						/* BASS */
	{ 0x10, 0x00, 0x00, 0, 0, 0, 0, 0, 0 },			/* MIXER_L */
	{ 0x10, 0x00, 0x00, 0, 0, 0, 0, 0, 0 },			/* MIXER_R */
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
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },	/* BIQUAD */
	{ 0, 0, 0 },						/* LLB_GAIN */
	{ 0, 0, 0 },						/* RLB_GAIN */
	{ DEQ_ACR_ADM | DEQ_ACR_LRB | DEQ_ACR_INP_B },		/* ACR */
	{ 0 }							/* MCR2 */
};

const char tas3004_regsize[] = {
	0,					/* 0x00 */
	sizeof tas3004_initdata.MCR1,		/* 0x01 */
	sizeof tas3004_initdata.DRC,		/* 0x02 */
	0,					/* 0x03 */
	sizeof tas3004_initdata.VOLUME,		/* 0x04 */
	sizeof tas3004_initdata.TREBLE,		/* 0x05 */
	sizeof tas3004_initdata.BASS,		/* 0x06 */
	sizeof tas3004_initdata.MIXER_L,	/* 0x07 */
	sizeof tas3004_initdata.MIXER_R,	/* 0x08 */
	0,					/* 0x09 */
	sizeof tas3004_initdata.LB0,		/* 0x0a */
	sizeof tas3004_initdata.LB1,		/* 0x0b */
	sizeof tas3004_initdata.LB2,		/* 0x0c */
	sizeof tas3004_initdata.LB3,		/* 0x0d */
	sizeof tas3004_initdata.LB4,		/* 0x0e */
	sizeof tas3004_initdata.LB5,		/* 0x0f */
	sizeof tas3004_initdata.LB6,		/* 0x10 */
	0,					/* 0x11 */
	0,					/* 0x12 */
	sizeof tas3004_initdata.RB0,		/* 0x13 */
	sizeof tas3004_initdata.RB1,		/* 0x14 */
	sizeof tas3004_initdata.RB2,		/* 0x15 */
	sizeof tas3004_initdata.RB3,		/* 0x16 */
	sizeof tas3004_initdata.RB4,		/* 0x17 */
	sizeof tas3004_initdata.RB5,		/* 0x18 */
	sizeof tas3004_initdata.RB6,		/* 0x19 */
	0,0,0,0, 0,0,
	0,					/* 0x20 */
	sizeof tas3004_initdata.LLB,		/* 0x21 */
	sizeof tas3004_initdata.RLB,		/* 0x22 */
	sizeof tas3004_initdata.LLB_GAIN,	/* 0x23 */
	sizeof tas3004_initdata.RLB_GAIN,	/* 0x24 */
	0,0,0,0, 0,0,0,0, 0,0,0,
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	sizeof tas3004_initdata.ACR,		/* 0x40 */
	0,					/* 0x41 */
	0,					/* 0x42 */
	sizeof tas3004_initdata.MCR2		/* 0x43 */
};

#define DEQaddr 0x6a

int
tas3004_write(struct snapper_softc *sc, u_int reg, const void *data)
{
	int size;

	KASSERT(reg < sizeof tas3004_regsize);
	size = tas3004_regsize[reg];
	KASSERT(size > 0);

	if (kiic_write(sc->sc_i2c, DEQaddr, reg, data, size))
		return (-1);

	return (0);
}

#define DEQ_WRITE(sc, reg, addr) \
	if (tas3004_write(sc, reg, addr)) goto err

int
tas3004_init(struct snapper_softc *sc)
{
	deq_reset(sc);

	DEQ_WRITE(sc, DEQ_LB0, tas3004_initdata.LB0);
	DEQ_WRITE(sc, DEQ_LB1, tas3004_initdata.LB1);
	DEQ_WRITE(sc, DEQ_LB2, tas3004_initdata.LB2);
	DEQ_WRITE(sc, DEQ_LB3, tas3004_initdata.LB3);
	DEQ_WRITE(sc, DEQ_LB4, tas3004_initdata.LB4);
	DEQ_WRITE(sc, DEQ_LB5, tas3004_initdata.LB5);
	DEQ_WRITE(sc, DEQ_LB6, tas3004_initdata.LB6);
	DEQ_WRITE(sc, DEQ_RB0, tas3004_initdata.RB0);
	DEQ_WRITE(sc, DEQ_RB1, tas3004_initdata.RB1);
	DEQ_WRITE(sc, DEQ_RB1, tas3004_initdata.RB1);
	DEQ_WRITE(sc, DEQ_RB2, tas3004_initdata.RB2);
	DEQ_WRITE(sc, DEQ_RB3, tas3004_initdata.RB3);
	DEQ_WRITE(sc, DEQ_RB4, tas3004_initdata.RB4);
	DEQ_WRITE(sc, DEQ_RB5, tas3004_initdata.RB5);
	DEQ_WRITE(sc, DEQ_RB6, tas3004_initdata.RB6);
	DEQ_WRITE(sc, DEQ_MCR1, tas3004_initdata.MCR1);
	DEQ_WRITE(sc, DEQ_MCR2, tas3004_initdata.MCR2);
	DEQ_WRITE(sc, DEQ_DRC, tas3004_initdata.DRC);
	DEQ_WRITE(sc, DEQ_VOLUME, tas3004_initdata.VOLUME);
	DEQ_WRITE(sc, DEQ_TREBLE, tas3004_initdata.TREBLE);
	DEQ_WRITE(sc, DEQ_BASS, tas3004_initdata.BASS);
	DEQ_WRITE(sc, DEQ_MIXER_L, tas3004_initdata.MIXER_L);
	DEQ_WRITE(sc, DEQ_MIXER_R, tas3004_initdata.MIXER_R);
	DEQ_WRITE(sc, DEQ_LLB, tas3004_initdata.LLB);
	DEQ_WRITE(sc, DEQ_RLB, tas3004_initdata.RLB);
	DEQ_WRITE(sc, DEQ_LLB_GAIN, tas3004_initdata.LLB_GAIN);
	DEQ_WRITE(sc, DEQ_RLB_GAIN, tas3004_initdata.RLB_GAIN);
	DEQ_WRITE(sc, DEQ_ACR, tas3004_initdata.ACR);

	return (0);
err:
	printf("%s: tas3004_init failed\n", sc->sc_dev.dv_xname);
	return (-1);
}

void
snapper_init(struct snapper_softc *sc)
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

	if (tas3004_init(sc))
		return;

	snapper_set_volume(sc, 190, 190);
	snapper_set_treble(sc, 128); /* 0 dB */
	snapper_set_bass(sc, 128); /* 0 dB */

	/* Mic in, reflects tas3004_initdata.ACR */
	sc->sc_record_source = 1 << 1;
	snapper_set_input(sc, sc->sc_record_source);
}
