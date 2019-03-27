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
 * 	NetBSD: tumbler.c,v 1.28 2008/05/16 03:49:54 macallan Exp
 *	Id: tumbler.c,v 1.11 2002/10/31 17:42:13 tsubai Exp 
 */

/*
 *	Apple I2S audio controller.
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

struct tumbler_softc
{
	device_t sc_dev;
	uint32_t sc_addr;
};

static int	tumbler_probe(device_t);
static int 	tumbler_attach(device_t);
static int	tumbler_init(struct snd_mixer *m);
static int	tumbler_uninit(struct snd_mixer *m);
static int	tumbler_reinit(struct snd_mixer *m);
static int	tumbler_set(struct snd_mixer *m, unsigned dev, unsigned left,
		    unsigned right);
static u_int32_t	tumbler_setrecsrc(struct snd_mixer *m, u_int32_t src);

static device_method_t tumbler_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		tumbler_probe),
	DEVMETHOD(device_attach,	tumbler_attach),

	{ 0, 0 }
};

static driver_t tumbler_driver = {
	"tumbler",
	tumbler_methods,
	sizeof(struct tumbler_softc)
};
static devclass_t tumbler_devclass;

DRIVER_MODULE(tumbler, iicbus, tumbler_driver, tumbler_devclass, 0, 0);
MODULE_VERSION(tumbler, 1);
MODULE_DEPEND(tumbler, iicbus, 1, 1, 1);

static kobj_method_t tumbler_mixer_methods[] = {
	KOBJMETHOD(mixer_init, 		tumbler_init),
	KOBJMETHOD(mixer_uninit, 	tumbler_uninit),
	KOBJMETHOD(mixer_reinit, 	tumbler_reinit),
	KOBJMETHOD(mixer_set, 		tumbler_set),
	KOBJMETHOD(mixer_setrecsrc, 	tumbler_setrecsrc),
	KOBJMETHOD_END
};

MIXER_DECLARE(tumbler_mixer);

#define TUMBLER_IICADDR		0x68	/* Tumbler I2C slave address */

/* Tumbler (Texas Instruments TAS3001) registers. */
#define TUMBLER_MCR		0x01	/* Main control register (1byte) */
#define TUMBLER_DRC         	0x02    /* Dynamic Range Compression (2bytes) */
#define TUMBLER_VOLUME		0x04	/* Volume (6bytes) */
#define TUMBLER_TREBLE		0x05	/* Treble control (1byte) */
#define TUMBLER_BASS		0x06	/* Bass control (1byte) */
#define TUMBLER_MIXER1		0x07	/* Mixer1 (3bytes) */
#define TUMBLER_MIXER2		0x08	/* Mixer2 (3bytes) */
#define TUMBLER_LB0		0x0a	/* Left biquad 0 (15bytes) */
#define TUMBLER_LB1		0x0b	/* Left biquad 1 (15bytes) */
#define TUMBLER_LB2		0x0c	/* Left biquad 2 (15bytes) */
#define TUMBLER_LB3		0x0d	/* Left biquad 3 (15bytes) */
#define TUMBLER_LB4		0x0e	/* Left biquad 4 (15bytes) */
#define TUMBLER_LB5		0x0f	/* Left biquad 5 (15bytes) */
#define TUMBLER_RB0		0x13	/* Right biquad 0 (15bytes) */
#define TUMBLER_RB1		0x14	/* Right biquad 1 (15bytes) */
#define TUMBLER_RB2		0x15	/* Right biquad 2 (15bytes) */
#define TUMBLER_RB3		0x16	/* Right biquad 3 (15bytes) */
#define TUMBLER_RB4		0x17	/* Right biquad 4 (15bytes) */
#define TUMBLER_RB5		0x18	/* Right biquad 5 (15bytes) */
#define TUMBLER_MCR_FL	0x80	/* Fast load */
#define TUMBLER_MCR_SC	0x40	/* SCLK frequency */
#define  TUMBLER_MCR_SC_32	0x00	/*  32fs */
#define  TUMBLER_MCR_SC_64	0x40	/*  64fs */
#define TUMBLER_MCR_SM	0x30	/* Output serial port mode */
#define  TUMBLER_MCR_SM_L	0x00	/*  Left justified */
#define  TUMBLER_MCR_SM_R	0x10	/*  Right justified */
#define  TUMBLER_MCR_SM_I2S 0x20	/*  I2S */
#define TUMBLER_MCR_ISM    0x0C    /* Input serial mode */
#define  TUMBLER_MCR_ISM_L 0x00
#define  TUMBLER_MCR_ISM_R 0x04
#define  TUMBLER_MCR_ISM_I2S 0x08
#define TUMBLER_MCR_W	0x03	/* Serial port word length */
#define  TUMBLER_MCR_W_16	0x00	/*  16 bit */
#define  TUMBLER_MCR_W_18	0x01	/*  18 bit */
#define  TUMBLER_MCR_W_20	0x02	/*  20 bit */
#define TUMBLER_DRC_COMP_31 0xc0    /* 3:1 compression */
#define TUMBLER_DRC_ENABLE  0x01    /* enable DRC */
#define TUMBLER_DRC_DEFL_TH 0xa0    /* default compression threshold */

/*
 * Tumbler codec.
 */

struct tumbler_reg {
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

const struct tumbler_reg tumbler_initdata = {
	{ TUMBLER_MCR_SC_64 | TUMBLER_MCR_SM_I2S | 
          TUMBLER_MCR_ISM_I2S | TUMBLER_MCR_W_16 },             /* MCR */
        { TUMBLER_DRC_COMP_31, TUMBLER_DRC_DEFL_TH },           /* DRC */
        { 0, 0, 0, 0, 0, 0 },				        /* VOLUME */
	{ 0x72 },						/* TREBLE */
	{ 0x3e },						/* BASS */
	{ 0x10, 0x00, 0x00 },	                		/* MIXER1 */
	{ 0x00, 0x00, 0x00 },		                	/* MIXER2 */
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
	{ 0x10, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }	/* BIQUAD */
};

const char tumbler_regsize[] = {
	0,					/* 0x00 */
	sizeof tumbler_initdata.MCR,		/* 0x01 */
        sizeof tumbler_initdata.DRC,            /* 0x02 */
	0,					/* 0x03 */
	sizeof tumbler_initdata.VOLUME,		/* 0x04 */
	sizeof tumbler_initdata.TREBLE,		/* 0x05 */
	sizeof tumbler_initdata.BASS,		/* 0x06 */
	sizeof tumbler_initdata.MIXER1, 	/* 0x07 */
	sizeof tumbler_initdata.MIXER2,	        /* 0x08 */
	0,					/* 0x09 */
	sizeof tumbler_initdata.LB0,		/* 0x0a */
	sizeof tumbler_initdata.LB1,		/* 0x0b */
	sizeof tumbler_initdata.LB2,		/* 0x0c */
	sizeof tumbler_initdata.LB3,		/* 0x0d */
	sizeof tumbler_initdata.LB4,		/* 0x0e */
	sizeof tumbler_initdata.LB5,		/* 0x0f */
	0,		                        /* 0x10 */
	0,					/* 0x11 */
	0,					/* 0x12 */
	sizeof tumbler_initdata.RB0,		/* 0x13 */
	sizeof tumbler_initdata.RB1,		/* 0x14 */
	sizeof tumbler_initdata.RB2,		/* 0x15 */
	sizeof tumbler_initdata.RB3,		/* 0x16 */
	sizeof tumbler_initdata.RB4,		/* 0x17 */
	sizeof tumbler_initdata.RB5		/* 0x18 */
};

/* dB = 20 * log (x) table. */
static u_int	tumbler_volume_table[100] = {      	
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
tumbler_write(struct tumbler_softc *sc, uint8_t reg, const void *data)
{
	u_int size;
	uint8_t buf[16];

	struct iic_msg msg[] = {
		{ sc->sc_addr, IIC_M_WR, 0, buf }
	};
		
	KASSERT(reg < sizeof(tumbler_regsize), ("bad reg"));
	size = tumbler_regsize[reg];
	msg[0].len = size + 1;
	buf[0] = reg;
	memcpy(&buf[1], data, size);

	iicbus_transfer(sc->sc_dev, msg, 1);

	return (0);
}

static int
tumbler_probe(device_t dev)
{
	const char *name;

	name = ofw_bus_get_name(dev);
	if (name == NULL)
		return (ENXIO);

	if (strcmp(name, "deq") == 0 && iicbus_get_addr(dev) == 
	    TUMBLER_IICADDR) {
		device_set_desc(dev, "Texas Instruments TAS3001 Audio Codec");
		return (0);
	}

	return (ENXIO);
}

static int
tumbler_attach(device_t dev)
{
	struct tumbler_softc *sc;
		
	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_addr = iicbus_get_addr(dev);

	i2s_mixer_class = &tumbler_mixer_class;
	i2s_mixer = dev;

	return (0);
}

static int
tumbler_init(struct snd_mixer *m)
{
	struct tumbler_softc *sc;
	u_int		x = 0;

	sc = device_get_softc(mix_getdevinfo(m));

        tumbler_write(sc, TUMBLER_LB0, tumbler_initdata.LB0);
	tumbler_write(sc, TUMBLER_LB1, tumbler_initdata.LB1);
	tumbler_write(sc, TUMBLER_LB2, tumbler_initdata.LB2);
	tumbler_write(sc, TUMBLER_LB3, tumbler_initdata.LB3);
	tumbler_write(sc, TUMBLER_LB4, tumbler_initdata.LB4);
	tumbler_write(sc, TUMBLER_LB5, tumbler_initdata.LB5);
	tumbler_write(sc, TUMBLER_RB0, tumbler_initdata.RB0);
	tumbler_write(sc, TUMBLER_RB1, tumbler_initdata.RB1);
	tumbler_write(sc, TUMBLER_RB1, tumbler_initdata.RB1);
	tumbler_write(sc, TUMBLER_RB2, tumbler_initdata.RB2);
	tumbler_write(sc, TUMBLER_RB3, tumbler_initdata.RB3);
	tumbler_write(sc, TUMBLER_RB4, tumbler_initdata.RB4);
	tumbler_write(sc, TUMBLER_RB5, tumbler_initdata.RB5);
	tumbler_write(sc, TUMBLER_MCR, tumbler_initdata.MCR);
	tumbler_write(sc, TUMBLER_DRC, tumbler_initdata.DRC);
        tumbler_write(sc, TUMBLER_VOLUME, tumbler_initdata.VOLUME);
	tumbler_write(sc, TUMBLER_TREBLE, tumbler_initdata.TREBLE);
	tumbler_write(sc, TUMBLER_BASS, tumbler_initdata.BASS);
	tumbler_write(sc, TUMBLER_MIXER1, tumbler_initdata.MIXER1);
	tumbler_write(sc, TUMBLER_MIXER2, tumbler_initdata.MIXER2);

	x |= SOUND_MASK_VOLUME;
	mix_setdevs(m, x);

	return (0);
}

static int
tumbler_uninit(struct snd_mixer *m)
{
	return (0);
}

static int
tumbler_reinit(struct snd_mixer *m)
{
	return (0);
}

static int
tumbler_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct tumbler_softc *sc;
	struct mtx *mixer_lock;
	int locked;
	u_int l, r;
	u_char reg[6];

	sc = device_get_softc(mix_getdevinfo(m));
	mixer_lock = mixer_get_lock(m);
	locked = mtx_owned(mixer_lock);

	switch (dev) {
	case SOUND_MIXER_VOLUME:
		if (left > 100 || right > 100)
			return (0);

		l = (left == 0 ? 0 : tumbler_volume_table[left - 1]);
		r = (right == 0 ? 0 : tumbler_volume_table[right - 1]);
		
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

		tumbler_write(sc, TUMBLER_VOLUME, reg);

		if (locked)
			mtx_lock(mixer_lock);

		return (left | (right << 8));
	}

	return (0);
}

static u_int32_t
tumbler_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	return (0);
}

