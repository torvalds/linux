/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-3-Clause
 *
 * Copyright 2008 by Marco Trillo. All rights reserved.
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */
/*-
 * Copyright (c) 2002, 2003 Tsubai Masanari.  All rights reserved.
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
 *
 * 	NetBSD: snapper.c,v 1.28 2008/05/16 03:49:54 macallan Exp
 *	Id: snapper.c,v 1.11 2002/10/31 17:42:13 tsubai Exp 
 */

/*
 *	Apple Snapper audio.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/malloc.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <machine/dbdma.h>
#include <machine/intr_machdep.h>
#include <machine/resource.h>
#include <machine/bus.h>
#include <machine/pio.h>
#include <sys/rman.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>
#include <dev/ofw/ofw_bus.h>

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>

#include "mixer_if.h"

extern kobj_class_t i2s_mixer_class;
extern device_t	i2s_mixer;

struct snapper_softc
{
	device_t sc_dev;
	uint32_t sc_addr;
};

static int	snapper_probe(device_t);
static int 	snapper_attach(device_t);
static int	snapper_init(struct snd_mixer *m);
static int	snapper_uninit(struct snd_mixer *m);
static int	snapper_reinit(struct snd_mixer *m);
static int	snapper_set(struct snd_mixer *m, unsigned dev, unsigned left,
		    unsigned right);
static u_int32_t	snapper_setrecsrc(struct snd_mixer *m, u_int32_t src);

static device_method_t snapper_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		snapper_probe),
	DEVMETHOD(device_attach,	snapper_attach),

	{ 0, 0 }
};

static driver_t snapper_driver = {
	"snapper",
	snapper_methods,
	sizeof(struct snapper_softc)
};
static devclass_t snapper_devclass;

DRIVER_MODULE(snapper, iicbus, snapper_driver, snapper_devclass, 0, 0);
MODULE_VERSION(snapper, 1);
MODULE_DEPEND(snapper, iicbus, 1, 1, 1);

static kobj_method_t snapper_mixer_methods[] = {
	KOBJMETHOD(mixer_init, 		snapper_init),
	KOBJMETHOD(mixer_uninit, 	snapper_uninit),
	KOBJMETHOD(mixer_reinit, 	snapper_reinit),
	KOBJMETHOD(mixer_set, 		snapper_set),
	KOBJMETHOD(mixer_setrecsrc, 	snapper_setrecsrc),
	KOBJMETHOD_END
};

MIXER_DECLARE(snapper_mixer);

#define SNAPPER_IICADDR	0x6a	/* Hard-coded I2C slave addr */

/* Snapper (Texas Instruments TAS3004) registers. */
#define SNAPPER_MCR1	0x01	/* Main control register 1 (1byte) */
#define SNAPPER_DRC	0x02	/* Dynamic range compression (6bytes) */
#define SNAPPER_VOLUME	0x04	/* Volume (6bytes) */
#define SNAPPER_TREBLE	0x05	/* Treble control (1byte) */
#define SNAPPER_BASS	0x06	/* Bass control (1byte) */
#define SNAPPER_MIXER_L	0x07	/* Mixer left gain (9bytes) */
#define SNAPPER_MIXER_R	0x08	/* Mixer right gain (9bytes) */
#define SNAPPER_LB0	0x0a	/* Left biquad 0 (15bytes) */
#define SNAPPER_LB1	0x0b	/* Left biquad 1 (15bytes) */
#define SNAPPER_LB2	0x0c	/* Left biquad 2 (15bytes) */
#define SNAPPER_LB3	0x0d	/* Left biquad 3 (15bytes) */
#define SNAPPER_LB4	0x0e	/* Left biquad 4 (15bytes) */
#define SNAPPER_LB5	0x0f	/* Left biquad 5 (15bytes) */
#define SNAPPER_LB6	0x10	/* Left biquad 6 (15bytes) */
#define SNAPPER_RB0	0x13	/* Right biquad 0 (15bytes) */
#define SNAPPER_RB1	0x14	/* Right biquad 1 (15bytes) */
#define SNAPPER_RB2	0x15	/* Right biquad 2 (15bytes) */
#define SNAPPER_RB3	0x16	/* Right biquad 3 (15bytes) */
#define SNAPPER_RB4	0x17	/* Right biquad 4 (15bytes) */
#define SNAPPER_RB5	0x18	/* Right biquad 5 (15bytes) */
#define SNAPPER_RB6	0x19	/* Right biquad 6 (15bytes) */
#define SNAPPER_LLB	0x21	/* Left loudness biquad (15bytes) */
#define SNAPPER_RLB	0x22	/* Right loudness biquad (15bytes) */
#define SNAPPER_LLB_GAIN	0x23	/* Left loudness biquad gain (3bytes) */
#define SNAPPER_RLB_GAIN	0x24	/* Right loudness biquad gain (3bytes) */
#define SNAPPER_ACR		0x40	/* Analog control register (1byte) */
#define SNAPPER_MCR2	0x43	/* Main control register 2 (1byte) */
#define SNAPPER_MCR1_FL	0x80	/* Fast load */
#define SNAPPER_MCR1_SC	0x40	/* SCLK frequency */
#define  SNAPPER_MCR1_SC_32	0x00	/*  32fs */
#define  SNAPPER_MCR1_SC_64	0x40	/*  64fs */
#define SNAPPER_MCR1_SM	0x30	/* Output serial port mode */
#define  SNAPPER_MCR1_SM_L	0x00	/*  Left justified */
#define  SNAPPER_MCR1_SM_R	0x10	/*  Right justified */
#define  SNAPPER_MCR1_SM_I2S 0x20	/*  I2S */
#define SNAPPER_MCR1_W	0x03	/* Serial port word length */
#define  SNAPPER_MCR1_W_16	0x00	/*  16 bit */
#define  SNAPPER_MCR1_W_18	0x01	/*  18 bit */
#define  SNAPPER_MCR1_W_20	0x02	/*  20 bit */
#define  SNAPPER_MCR1_W_24	0x03	/*  24 bit */
#define SNAPPER_MCR2_DL	0x80	/* Download */
#define SNAPPER_MCR2_AP	0x02	/* All pass mode */
#define SNAPPER_ACR_ADM	0x80	/* ADC output mode */
#define SNAPPER_ACR_LRB	0x40	/* Select B input */
#define SNAPPER_ACR_DM	0x0c	/* De-emphasis control */
#define  SNAPPER_ACR_DM_OFF	0x00	/*  off */
#define  SNAPPER_ACR_DM_48	0x04	/*  fs = 48kHz */
#define  SNAPPER_ACR_DM_44	0x08	/*  fs = 44.1kHz */
#define SNAPPER_ACR_INP	0x02	/* Analog input select */
#define  SNAPPER_ACR_INP_A	0x00	/*  A */
#define  SNAPPER_ACR_INP_B	0x02	/*  B */
#define SNAPPER_ACR_APD	0x01	/* Analog power down */


struct snapper_reg {
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

static const struct snapper_reg snapper_initdata = {
	{ SNAPPER_MCR1_SC_64 | SNAPPER_MCR1_SM_I2S | 
	  SNAPPER_MCR1_W_16 }, 					/* MCR1 */
	{ 1, 0, 0, 0, 0, 0 },					/* DRC */
	{ 0, 0, 0, 0, 0, 0 },					/* VOLUME */
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
	{ SNAPPER_ACR_ADM | SNAPPER_ACR_LRB | SNAPPER_ACR_INP_B },/* ACR */
	{ SNAPPER_MCR2_AP }					 /* MCR2 */
};

static const char snapper_regsize[] = {
	0,					/* 0x00 */
	sizeof snapper_initdata.MCR1,		/* 0x01 */
	sizeof snapper_initdata.DRC,		/* 0x02 */
	0,					/* 0x03 */
	sizeof snapper_initdata.VOLUME,		/* 0x04 */
	sizeof snapper_initdata.TREBLE,		/* 0x05 */
	sizeof snapper_initdata.BASS,		/* 0x06 */
	sizeof snapper_initdata.MIXER_L,	/* 0x07 */
	sizeof snapper_initdata.MIXER_R,	/* 0x08 */
	0,					/* 0x09 */
	sizeof snapper_initdata.LB0,		/* 0x0a */
	sizeof snapper_initdata.LB1,		/* 0x0b */
	sizeof snapper_initdata.LB2,		/* 0x0c */
	sizeof snapper_initdata.LB3,		/* 0x0d */
	sizeof snapper_initdata.LB4,		/* 0x0e */
	sizeof snapper_initdata.LB5,		/* 0x0f */
	sizeof snapper_initdata.LB6,		/* 0x10 */
	0,					/* 0x11 */
	0,					/* 0x12 */
	sizeof snapper_initdata.RB0,		/* 0x13 */
	sizeof snapper_initdata.RB1,		/* 0x14 */
	sizeof snapper_initdata.RB2,		/* 0x15 */
	sizeof snapper_initdata.RB3,		/* 0x16 */
	sizeof snapper_initdata.RB4,		/* 0x17 */
	sizeof snapper_initdata.RB5,		/* 0x18 */
	sizeof snapper_initdata.RB6,		/* 0x19 */
	0,0,0,0, 0,0,
	0,					/* 0x20 */
	sizeof snapper_initdata.LLB,		/* 0x21 */
	sizeof snapper_initdata.RLB,		/* 0x22 */
	sizeof snapper_initdata.LLB_GAIN,	/* 0x23 */
	sizeof snapper_initdata.RLB_GAIN,	/* 0x24 */
	0,0,0,0, 0,0,0,0, 0,0,0,
	0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
	sizeof snapper_initdata.ACR,		/* 0x40 */
	0,					/* 0x41 */
	0,					/* 0x42 */
	sizeof snapper_initdata.MCR2		/* 0x43 */
};

/* dB = 20 * log (x) table. */
static u_int	snapper_volume_table[100] = {      	
	0x00000148,   0x0000015C,   0x00000171,   0x00000186,   // -46.0,	-45.5,	-45.0,	-44.5,
	0x0000019E,   0x000001B6,   0x000001D0,   0x000001EB,   // -44.0,	-43.5,	-43.0,	-42.5,
	0x00000209,   0x00000227,   0x00000248,   0x0000026B,   // -42.0,	-41.5,	-41.0,	-40.5,
	0x0000028F,   0x000002B6,   0x000002DF,   0x0000030B,   // -40.0,	-39.5,	-39.0,	-38.5,
	0x00000339,   0x0000036A,   0x0000039E,   0x000003D5,   // -38.0,	-37.5,	-37.0,	-36.5,
	0x0000040F,   0x0000044C,   0x0000048D,   0x000004D2,   // -36.0,	-35.5,	-35.0,	-34.5,
	0x0000051C,   0x00000569,   0x000005BB,   0x00000612,   // -34.0,	-33.5,	-33.0,	-32.5,
	0x0000066E,   0x000006D0,   0x00000737,   0x000007A5,   // -32.0,	-31.5,	-31.0,	-30.5,
	0x00000818,   0x00000893,   0x00000915,   0x0000099F,   // -30.0,	-29.5,	-29.0,	-28.5,
	0x00000A31,   0x00000ACC,   0x00000B6F,   0x00000C1D,   // -28.0,	-27.5,	-27.0,	-26.5,
	0x00000CD5,   0x00000D97,   0x00000E65,   0x00000F40,   // -26.0,	-25.5,	-25.0,	-24.5,
	0x00001027,   0x0000111C,   0x00001220,   0x00001333,   // -24.0,	-23.5,	-23.0,	-22.5,
	0x00001456,   0x0000158A,   0x000016D1,   0x0000182B,   // -22.0,	-21.5,	-21.0,	-20.5,
	0x0000199A,   0x00001B1E,   0x00001CB9,   0x00001E6D,   // -20.0,	-19.5,	-19.0,	-18.5,
	0x0000203A,   0x00002223,   0x00002429,   0x0000264E,   // -18.0,	-17.5,	-17.0,	-16.5,
	0x00002893,   0x00002AFA,   0x00002D86,   0x00003039,   // -16.0,	-15.5,	-15.0,	-14.5,
	0x00003314,   0x0000361B,   0x00003950,   0x00003CB5,   // -14.0,	-13.5,	-13.0,	-12.5,
	0x0000404E,   0x0000441D,   0x00004827,   0x00004C6D,   // -12.0,	-11.5,	-11.0,	-10.5,
	0x000050F4,   0x000055C0,   0x00005AD5,   0x00006037,   // -10.0,	-9.5,	-9.0,	-8.5,
	0x000065EA,   0x00006BF4,   0x0000725A,   0x00007920,   // -8.0,	-7.5,	-7.0,	-6.5,
	0x0000804E,   0x000087EF,   0x00008FF6,   0x0000987D,   // -6.0,	-5.5,	-5.0,	-4.5,
	0x0000A186,   0x0000AB19,   0x0000B53C,   0x0000BFF9,   // -4.0,	-3.5,	-3.0,	-2.5,
	0x0000CB59,   0x0000D766,   0x0000E429,   0x0000F1AE,   // -2.0,	-1.5,	-1.0,	-0.5,
	0x00010000,   0x00010F2B,   0x00011F3D,   0x00013042,   // 0.0,   +0.5,	+1.0,	+1.5,
	0x00014249,   0x00015562,   0x0001699C,   0x00017F09    // 2.0,   +2.5,	+3.0,	+3.5,
};

static int
snapper_write(struct snapper_softc *sc, uint8_t reg, const void *data)
{
	u_int size;
	uint8_t buf[16];

	struct iic_msg msg[] = {
		{ sc->sc_addr, IIC_M_WR, 0, buf }
	};
		
	KASSERT(reg < sizeof(snapper_regsize), ("bad reg"));
	size = snapper_regsize[reg];
	msg[0].len = size + 1;
	buf[0] = reg;
	memcpy(&buf[1], data, size);

	iicbus_transfer(sc->sc_dev, msg, 1);

	return (0);
}

static int
snapper_probe(device_t dev)
{
	const char *name, *compat;

	name = ofw_bus_get_name(dev);
	if (name == NULL)
		return (ENXIO);

	if (strcmp(name, "deq") == 0) {
		if (iicbus_get_addr(dev) != SNAPPER_IICADDR)
			return (ENXIO);
	} else if (strcmp(name, "codec") == 0) {
		compat = ofw_bus_get_compat(dev);
		if (compat == NULL || strcmp(compat, "tas3004") != 0)
			return (ENXIO);
	} else {
		return (ENXIO);
	}

	device_set_desc(dev, "Texas Instruments TAS3004 Audio Codec");
	return (0);
}

static int
snapper_attach(device_t dev)
{
	struct snapper_softc *sc;
		
	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_addr = iicbus_get_addr(dev);

	i2s_mixer_class = &snapper_mixer_class;
	i2s_mixer = dev;

	return (0);
}

static int
snapper_init(struct snd_mixer *m)
{
	struct snapper_softc *sc;
	u_int		x = 0;

	sc = device_get_softc(mix_getdevinfo(m));

        snapper_write(sc, SNAPPER_LB0, snapper_initdata.LB0);
	snapper_write(sc, SNAPPER_LB1, snapper_initdata.LB1);
	snapper_write(sc, SNAPPER_LB2, snapper_initdata.LB2);
	snapper_write(sc, SNAPPER_LB3, snapper_initdata.LB3);
	snapper_write(sc, SNAPPER_LB4, snapper_initdata.LB4);
	snapper_write(sc, SNAPPER_LB5, snapper_initdata.LB5);
	snapper_write(sc, SNAPPER_LB6, snapper_initdata.LB6);
	snapper_write(sc, SNAPPER_RB0, snapper_initdata.RB0);
	snapper_write(sc, SNAPPER_RB1, snapper_initdata.RB1);
	snapper_write(sc, SNAPPER_RB1, snapper_initdata.RB1);
	snapper_write(sc, SNAPPER_RB2, snapper_initdata.RB2);
	snapper_write(sc, SNAPPER_RB3, snapper_initdata.RB3);
	snapper_write(sc, SNAPPER_RB4, snapper_initdata.RB4);
	snapper_write(sc, SNAPPER_RB5, snapper_initdata.RB5);
	snapper_write(sc, SNAPPER_RB6, snapper_initdata.RB6);
	snapper_write(sc, SNAPPER_MCR1, snapper_initdata.MCR1);
	snapper_write(sc, SNAPPER_MCR2, snapper_initdata.MCR2);
	snapper_write(sc, SNAPPER_DRC, snapper_initdata.DRC);
	snapper_write(sc, SNAPPER_VOLUME, snapper_initdata.VOLUME);
	snapper_write(sc, SNAPPER_TREBLE, snapper_initdata.TREBLE);
	snapper_write(sc, SNAPPER_BASS, snapper_initdata.BASS);
	snapper_write(sc, SNAPPER_MIXER_L, snapper_initdata.MIXER_L);
	snapper_write(sc, SNAPPER_MIXER_R, snapper_initdata.MIXER_R);
	snapper_write(sc, SNAPPER_LLB, snapper_initdata.LLB);
	snapper_write(sc, SNAPPER_RLB, snapper_initdata.RLB);
	snapper_write(sc, SNAPPER_LLB_GAIN, snapper_initdata.LLB_GAIN);
	snapper_write(sc, SNAPPER_RLB_GAIN, snapper_initdata.RLB_GAIN);
	snapper_write(sc, SNAPPER_ACR, snapper_initdata.ACR);

	x |= SOUND_MASK_VOLUME;
	mix_setdevs(m, x);

	return (0);
}

static int
snapper_uninit(struct snd_mixer *m)
{
	return (0);
}

static int
snapper_reinit(struct snd_mixer *m)
{
	return (0);
}

static int
snapper_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct snapper_softc *sc;
	struct mtx *mixer_lock;
	int locked;
	u_int l, r;
	u_char reg[6];

	sc = device_get_softc(mix_getdevinfo(m));
	mixer_lock = mixer_get_lock(m);
	locked = mtx_owned(mixer_lock);

	if (left > 100 || right > 100)
		return (0);

	l = (left == 0) ? 0 : snapper_volume_table[left - 1];
	r = (right == 0) ? 0 : snapper_volume_table[right - 1];

	switch (dev) {
	case SOUND_MIXER_VOLUME:
		reg[0] = (l & 0xff0000) >> 16;
		reg[1] = (l & 0x00ff00) >> 8;
		reg[2] = l & 0x0000ff;
		reg[3] = (r & 0xff0000) >> 16;
		reg[4] = (r & 0x00ff00) >> 8;
		reg[5] = r & 0x0000ff;

		/*
		 * We need to unlock the mixer lock because iicbus_transfer()
		 * may sleep. The mixer lock itself is unnecessary here
		 * because it is meant to serialize hardware access, which
		 * is taken care of by the I2C layer, so this is safe.
		 */

		if (locked)
			mtx_unlock(mixer_lock);

		snapper_write(sc, SNAPPER_VOLUME, reg);

		if (locked)
			mtx_lock(mixer_lock);

		return (left | (right << 8));
	}

	return (0);
}

static u_int32_t
snapper_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	return (0);
}

