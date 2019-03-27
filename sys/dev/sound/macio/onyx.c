/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright 2012 by Andreas Tobler. All rights reserved.
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

/*
 * Apple PCM3052 aka Onyx audio codec.
 *
 * Datasheet: http://www.ti.com/product/pcm3052a
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

struct onyx_softc
{
	device_t sc_dev;
	uint32_t sc_addr;
};

static int	onyx_probe(device_t);
static int	onyx_attach(device_t);
static int	onyx_init(struct snd_mixer *m);
static int	onyx_uninit(struct snd_mixer *m);
static int	onyx_reinit(struct snd_mixer *m);
static int	onyx_set(struct snd_mixer *m, unsigned dev, unsigned left,
			    unsigned right);
static u_int32_t	onyx_setrecsrc(struct snd_mixer *m, u_int32_t src);

static device_method_t onyx_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		onyx_probe),
	DEVMETHOD(device_attach,	onyx_attach),
	{ 0, 0 }
};

static driver_t onyx_driver = {
	"onyx",
	onyx_methods,
	sizeof(struct onyx_softc)
};
static devclass_t onyx_devclass;

DRIVER_MODULE(onyx, iicbus, onyx_driver, onyx_devclass, 0, 0);
MODULE_VERSION(onyx, 1);
MODULE_DEPEND(onyx, iicbus, 1, 1, 1);

static kobj_method_t onyx_mixer_methods[] = {
	KOBJMETHOD(mixer_init,		onyx_init),
	KOBJMETHOD(mixer_uninit,	onyx_uninit),
	KOBJMETHOD(mixer_reinit,	onyx_reinit),
	KOBJMETHOD(mixer_set,		onyx_set),
	KOBJMETHOD(mixer_setrecsrc,	onyx_setrecsrc),
	KOBJMETHOD_END
};

MIXER_DECLARE(onyx_mixer);

#define PCM3052_IICADDR	0x8C	/* Hard-coded I2C slave addr */

/*
 * PCM3052 registers.
 * Numbering in decimal as used in the data sheet.
 */
#define PCM3052_REG_LEFT_ATTN       65
#define PCM3052_REG_RIGHT_ATTN      66
#define PCM3052_REG_CONTROL         67
#define PCM3052_MRST                (1 << 7)
#define PCM3052_SRST                (1 << 6)
#define PCM3052_REG_DAC_CONTROL     68
#define PCM3052_OVR1                (1 << 6)
#define PCM3052_MUTE_L              (1 << 1)
#define PCM3052_MUTE_R              (1 << 0)
#define PCM3052_REG_DAC_DEEMPH      69
#define PCM3052_REG_DAC_FILTER      70
#define PCM3052_DAC_FILTER_ALWAYS   (1 << 2)
#define PCM3052_REG_OUT_PHASE       71
#define PCM3052_REG_ADC_CONTROL     72
#define PCM3052_REG_ADC_HPF_BP      75
#define PCM3052_HPF_ALWAYS          (1 << 2)
#define PCM3052_REG_INFO_1          77
#define PCM3052_REG_INFO_2          78
#define PCM3052_REG_INFO_3          79
#define PCM3052_REG_INFO_4          80

struct onyx_reg {
	u_char LEFT_ATTN;
	u_char RIGHT_ATTN;
	u_char CONTROL;
	u_char DAC_CONTROL;
	u_char DAC_DEEMPH;
	u_char DAC_FILTER;
	u_char OUT_PHASE;
	u_char ADC_CONTROL;
	u_char ADC_HPF_BP;
	u_char INFO_1;
	u_char INFO_2;
	u_char INFO_3;
	u_char INFO_4;
};

static const struct onyx_reg onyx_initdata = {
	0x80,				  /* LEFT_ATTN, Mute default */
	0x80,				  /* RIGHT_ATTN, Mute default */
	PCM3052_MRST | PCM3052_SRST,      /* CONTROL */
	0,                                /* DAC_CONTROL */
	0,				  /* DAC_DEEMPH */
	PCM3052_DAC_FILTER_ALWAYS,	  /* DAC_FILTER */
	0,				  /* OUT_PHASE */
	(-1 /* dB */ + 8) & 0xf,          /* ADC_CONTROL */
	PCM3052_HPF_ALWAYS,		  /* ADC_HPF_BP */
	(1 << 2),			  /* INFO_1 */
	2,				  /* INFO_2,  */
	0,				  /* INFO_3, CLK 0 (level II),
					     SF 0 (44.1 kHz) */
	1				  /* INFO_4, VALIDL/R 0,
					     WL 24-bit depth */
};

static int
onyx_write(struct onyx_softc *sc, uint8_t reg, const uint8_t value)
{
	u_int size;
	uint8_t buf[16];

	struct iic_msg msg[] = {
		{ sc->sc_addr, IIC_M_WR, 0, buf }
	};

	size = 1;
	msg[0].len = size + 1;
	buf[0] = reg;
	buf[1] = value;

	iicbus_transfer(sc->sc_dev, msg, 1);

	return (0);
}

static int
onyx_probe(device_t dev)
{
	const char *name, *compat;

	name = ofw_bus_get_name(dev);
	if (name == NULL)
		return (ENXIO);

	if (strcmp(name, "codec") == 0) {
		if (iicbus_get_addr(dev) != PCM3052_IICADDR)
			return (ENXIO);
	} else if (strcmp(name, "codec") == 0) {
		compat = ofw_bus_get_compat(dev);
		if (compat == NULL || strcmp(compat, "pcm3052") != 0)
			return (ENXIO);
	} else
		return (ENXIO);

	device_set_desc(dev, "Texas Instruments PCM3052 Audio Codec");
	return (0);
}

static int
onyx_attach(device_t dev)
{
	struct onyx_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_addr = iicbus_get_addr(dev);

	i2s_mixer_class = &onyx_mixer_class;
	i2s_mixer = dev;

	return (0);
}

static int
onyx_init(struct snd_mixer *m)
{
	struct onyx_softc *sc;
	u_int  x = 0;

	sc = device_get_softc(mix_getdevinfo(m));

	onyx_write(sc, PCM3052_REG_LEFT_ATTN, onyx_initdata.LEFT_ATTN);
	onyx_write(sc, PCM3052_REG_RIGHT_ATTN, onyx_initdata.RIGHT_ATTN);
	onyx_write(sc, PCM3052_REG_CONTROL, onyx_initdata.CONTROL);
	onyx_write(sc, PCM3052_REG_DAC_CONTROL,
		      onyx_initdata.DAC_CONTROL);
	onyx_write(sc, PCM3052_REG_DAC_DEEMPH, onyx_initdata.DAC_DEEMPH);
	onyx_write(sc, PCM3052_REG_DAC_FILTER, onyx_initdata.DAC_FILTER);
	onyx_write(sc, PCM3052_REG_OUT_PHASE, onyx_initdata.OUT_PHASE);
	onyx_write(sc, PCM3052_REG_ADC_CONTROL,
		      onyx_initdata.ADC_CONTROL);
	onyx_write(sc, PCM3052_REG_ADC_HPF_BP, onyx_initdata.ADC_HPF_BP);
	onyx_write(sc, PCM3052_REG_INFO_1, onyx_initdata.INFO_1);
	onyx_write(sc, PCM3052_REG_INFO_2, onyx_initdata.INFO_2);
	onyx_write(sc, PCM3052_REG_INFO_3, onyx_initdata.INFO_3);
	onyx_write(sc, PCM3052_REG_INFO_4, onyx_initdata.INFO_4);

	x |= SOUND_MASK_VOLUME;
	mix_setdevs(m, x);

	return (0);
}

static int
onyx_uninit(struct snd_mixer *m)
{
	return (0);
}

static int
onyx_reinit(struct snd_mixer *m)
{
	return (0);
}

static int
onyx_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct onyx_softc *sc;
	struct mtx *mixer_lock;
	int locked;
	uint8_t l, r;

	sc = device_get_softc(mix_getdevinfo(m));
	mixer_lock = mixer_get_lock(m);
	locked = mtx_owned(mixer_lock);

	switch (dev) {
	case SOUND_MIXER_VOLUME:

		/*
		 * We need to unlock the mixer lock because iicbus_transfer()
		 * may sleep. The mixer lock itself is unnecessary here
		 * because it is meant to serialize hardware access, which
		 * is taken care of by the I2C layer, so this is safe.
		 */
		if (left > 100 || right > 100)
			return (0);

		l = left + 128;
		r = right + 128;

		if (locked)
			mtx_unlock(mixer_lock);

		onyx_write(sc, PCM3052_REG_LEFT_ATTN, l);
		onyx_write(sc, PCM3052_REG_RIGHT_ATTN, r);

		if (locked)
			mtx_lock(mixer_lock);

		return (left | (right << 8));
	}

	return (0);
}

static u_int32_t
onyx_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	return (0);
}
