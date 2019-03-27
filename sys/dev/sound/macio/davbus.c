/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
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

/*
 *	Apple DAVbus audio controller.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <dev/ofw/ofw_bus.h>

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>

#include <dev/sound/macio/aoa.h>
#include <dev/sound/macio/davbusreg.h>

#include <machine/intr_machdep.h>
#include <machine/resource.h>
#include <machine/bus.h>

#include "mixer_if.h"

struct davbus_softc {
	struct aoa_softc 	 aoa;
	phandle_t 		 node;
	phandle_t 		 soundnode;
	struct resource 	*reg;
	struct mtx 		 mutex;
	int 			 device_id;
	u_int 			 output_mask;
	u_int 			(*read_status)(struct davbus_softc *, u_int);
	void			(*set_outputs)(struct davbus_softc *, u_int);
};

static int 	davbus_probe(device_t);
static int 	davbus_attach(device_t);
static void	davbus_cint(void *);

static device_method_t pcm_davbus_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		davbus_probe),
	DEVMETHOD(device_attach, 	davbus_attach),

	{ 0, 0 }
};

static driver_t pcm_davbus_driver = {
	"pcm",
	pcm_davbus_methods,
	PCM_SOFTC_SIZE
};

DRIVER_MODULE(pcm_davbus, macio, pcm_davbus_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(pcm_davbus, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);

/*****************************************************************************
			Probe and attachment routines.
 *****************************************************************************/
static int
davbus_probe(device_t self)
{
	const char 		*name;

	name = ofw_bus_get_name(self);
	if (!name)
		return (ENXIO);

	if (strcmp(name, "davbus") != 0)
		return (ENXIO);
	
	device_set_desc(self, "Apple DAVBus Audio Controller");

	return (0);
}

/*
 * Burgundy codec control
 */

static int	burgundy_init(struct snd_mixer *m);
static int	burgundy_uninit(struct snd_mixer *m);
static int	burgundy_reinit(struct snd_mixer *m);
static void 	burgundy_write_locked(struct davbus_softc *, u_int, u_int);
static void	burgundy_set_outputs(struct davbus_softc *d, u_int mask);
static u_int	burgundy_read_status(struct davbus_softc *d, u_int status);
static int	burgundy_set(struct snd_mixer *m, unsigned dev, unsigned left,
		    unsigned right);
static u_int32_t	burgundy_setrecsrc(struct snd_mixer *m, u_int32_t src);

static kobj_method_t burgundy_mixer_methods[] = {
	KOBJMETHOD(mixer_init, 		burgundy_init),
	KOBJMETHOD(mixer_uninit, 	burgundy_uninit),
	KOBJMETHOD(mixer_reinit, 	burgundy_reinit),
	KOBJMETHOD(mixer_set, 		burgundy_set),
	KOBJMETHOD(mixer_setrecsrc,	burgundy_setrecsrc),
	KOBJMETHOD_END
};

MIXER_DECLARE(burgundy_mixer);

static int
burgundy_init(struct snd_mixer *m)
{
	struct davbus_softc *d;

	d = mix_getdevinfo(m);

	d->read_status = burgundy_read_status;
	d->set_outputs = burgundy_set_outputs;

	/*
	 * We configure the Burgundy codec as follows:
	 *
	 * 	o Input subframe 0 is connected to input digital
	 *	  stream A (ISA).
	 *	o Stream A (ISA) is mixed in mixer 2 (MIX2).
	 *	o Output of mixer 2 (MIX2) is routed to output sources
	 *	  OS0 and OS1 which can be converted to analog.
	 *
	 */
	mtx_lock(&d->mutex);

	burgundy_write_locked(d, 0x16700, 0x40);
	
	burgundy_write_locked(d, BURGUNDY_MIX0_REG, 0); 
	burgundy_write_locked(d, BURGUNDY_MIX1_REG, 0);
	burgundy_write_locked(d, BURGUNDY_MIX2_REG, BURGUNDY_MIX_ISA);
	burgundy_write_locked(d, BURGUNDY_MIX3_REG, 0);

	burgundy_write_locked(d, BURGUNDY_OS_REG, BURGUNDY_OS0_MIX2 | 
	    BURGUNDY_OS1_MIX2);

	burgundy_write_locked(d, BURGUNDY_SDIN_REG, BURGUNDY_ISA_SF0);

	/* Set several digital scalers to unity gain. */
	burgundy_write_locked(d, BURGUNDY_MXS2L_REG, BURGUNDY_MXS_UNITY);
	burgundy_write_locked(d, BURGUNDY_MXS2R_REG, BURGUNDY_MXS_UNITY);
	burgundy_write_locked(d, BURGUNDY_OSS0L_REG, BURGUNDY_OSS_UNITY);
	burgundy_write_locked(d, BURGUNDY_OSS0R_REG, BURGUNDY_OSS_UNITY);
	burgundy_write_locked(d, BURGUNDY_OSS1L_REG, BURGUNDY_OSS_UNITY);
	burgundy_write_locked(d, BURGUNDY_OSS1R_REG, BURGUNDY_OSS_UNITY);
	burgundy_write_locked(d, BURGUNDY_ISSAL_REG, BURGUNDY_ISS_UNITY);
	burgundy_write_locked(d, BURGUNDY_ISSAR_REG, BURGUNDY_ISS_UNITY);

	burgundy_set_outputs(d, burgundy_read_status(d, 
	    bus_read_4(d->reg, DAVBUS_CODEC_STATUS)));

	mtx_unlock(&d->mutex);

	mix_setdevs(m, SOUND_MASK_VOLUME);

	return (0);
}

static int
burgundy_uninit(struct snd_mixer *m)
{
	return (0);
}

static int
burgundy_reinit(struct snd_mixer *m)
{
	return (0);
}

static void
burgundy_write_locked(struct davbus_softc *d, u_int reg, u_int val)
{
	u_int size, addr, offset, data, i;

	size = (reg & 0x00FF0000) >> 16;
	addr = (reg & 0x0000FF00) >> 8;
	offset = reg & 0xFF;

	for (i = offset; i < offset + size; ++i) {
		data = BURGUNDY_CTRL_WRITE | (addr << 12) | 
		    ((size + offset - 1) << 10) | (i << 8) | (val & 0xFF);
		if (i == offset)
			data |= BURGUNDY_CTRL_RESET;

		bus_write_4(d->reg, DAVBUS_CODEC_CTRL, data);

		while (bus_read_4(d->reg, DAVBUS_CODEC_CTRL) &
		    DAVBUS_CODEC_BUSY)
			DELAY(1);
		
		val >>= 8; /* next byte. */
	}	
}

/* Must be called with d->mutex held. */
static void
burgundy_set_outputs(struct davbus_softc *d, u_int mask)
{
	u_int	x = 0;

	if (mask == d->output_mask)
		return;

	/*
	 *	Bordeaux card wirings:
	 *		Port 15:	RCA out
	 *		Port 16:	Minijack out
	 *		Port 17:	Internal speaker
	 *
	 *	B&W G3 wirings:
	 *		Port 14:	Minijack out
	 *		Port 17:	Internal speaker
	 */

	DPRINTF(("Enabled outputs:"));
	if (mask & (1 << 0)) {
		DPRINTF((" SPEAKER"));
		x |= BURGUNDY_P17M_EN;
	}
	if (mask & (1 << 1)) {
		DPRINTF((" HEADPHONES"));
		x |= BURGUNDY_P14L_EN | BURGUNDY_P14R_EN;	
	}
	DPRINTF(("\n"));

	burgundy_write_locked(d, BURGUNDY_MUTE_REG, x);
	d->output_mask = mask;
}

static u_int
burgundy_read_status(struct davbus_softc *d, u_int status)
{
	if (status & 0x4)
		return (1 << 1);
	else
		return (1 << 0);
}

static int
burgundy_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct davbus_softc *d;
	int lval, rval;

	lval = ((100 - left) * 15 / 100) & 0xf;
	rval = ((100 - right) * 15 / 100) & 0xf;
	DPRINTF(("volume %d %d\n", lval, rval));

	d = mix_getdevinfo(m);

	switch (dev) {
	case SOUND_MIXER_VOLUME:
		mtx_lock(&d->mutex);

		burgundy_write_locked(d, BURGUNDY_OL13_REG, lval);
		burgundy_write_locked(d, BURGUNDY_OL14_REG, (rval << 4) | lval);
		burgundy_write_locked(d, BURGUNDY_OL15_REG, (rval << 4) | lval);
		burgundy_write_locked(d, BURGUNDY_OL16_REG, (rval << 4) | lval);
		burgundy_write_locked(d, BURGUNDY_OL17_REG, lval);

		mtx_unlock(&d->mutex);

		return (left | (right << 8));
	}

	return (0);
}

static u_int32_t
burgundy_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	return (0);
}

/*
 * Screamer Codec Control
 */

static int	screamer_init(struct snd_mixer *m);
static int	screamer_uninit(struct snd_mixer *m);
static int	screamer_reinit(struct snd_mixer *m);
static void 	screamer_write_locked(struct davbus_softc *, u_int, u_int);
static void	screamer_set_outputs(struct davbus_softc *d, u_int mask);
static u_int	screamer_read_status(struct davbus_softc *d, u_int status);
static int	screamer_set(struct snd_mixer *m, unsigned dev, unsigned left,
		    unsigned right);
static u_int32_t	screamer_setrecsrc(struct snd_mixer *m, u_int32_t src);

static kobj_method_t screamer_mixer_methods[] = {
	KOBJMETHOD(mixer_init, 		screamer_init),
	KOBJMETHOD(mixer_uninit, 	screamer_uninit),
	KOBJMETHOD(mixer_reinit, 	screamer_reinit),
	KOBJMETHOD(mixer_set, 		screamer_set),
	KOBJMETHOD(mixer_setrecsrc,	screamer_setrecsrc),
	KOBJMETHOD_END
};

MIXER_DECLARE(screamer_mixer);

static int
screamer_init(struct snd_mixer *m)
{
	struct davbus_softc *d;

	d = mix_getdevinfo(m);

	d->read_status = screamer_read_status;
	d->set_outputs = screamer_set_outputs;

	mtx_lock(&d->mutex);

	screamer_write_locked(d, SCREAMER_CODEC_ADDR0, SCREAMER_INPUT_CD | 
	    SCREAMER_DEFAULT_CD_GAIN);

	screamer_set_outputs(d, screamer_read_status(d, 
	    bus_read_4(d->reg, DAVBUS_CODEC_STATUS)));

	screamer_write_locked(d, SCREAMER_CODEC_ADDR2, 0);
	screamer_write_locked(d, SCREAMER_CODEC_ADDR4, 0);
	screamer_write_locked(d, SCREAMER_CODEC_ADDR5, 0);
	screamer_write_locked(d, SCREAMER_CODEC_ADDR6, 0);

	mtx_unlock(&d->mutex);

	mix_setdevs(m, SOUND_MASK_VOLUME);

	return (0);
}

static int
screamer_uninit(struct snd_mixer *m)
{
	return (0);
}

static int
screamer_reinit(struct snd_mixer *m)
{
	return (0);
}


static void
screamer_write_locked(struct davbus_softc *d, u_int reg, u_int val)
{
	u_int 		x;

	KASSERT(val == (val & 0xfff), ("bad val"));

	while (bus_read_4(d->reg, DAVBUS_CODEC_CTRL) & DAVBUS_CODEC_BUSY)
		DELAY(100);

	x = reg;
	x |= SCREAMER_CODEC_EMSEL0;
	x |= val;
	bus_write_4(d->reg, DAVBUS_CODEC_CTRL, x);

	while (bus_read_4(d->reg, DAVBUS_CODEC_CTRL) & DAVBUS_CODEC_BUSY)
		DELAY(100);
}

/* Must be called with d->mutex held. */
static void
screamer_set_outputs(struct davbus_softc *d, u_int mask)
{
	u_int 	x;

	if (mask == d->output_mask) {
		return;
	}

	x = SCREAMER_MUTE_SPEAKER | SCREAMER_MUTE_HEADPHONES;

	DPRINTF(("Enabled outputs: "));

	if (mask & (1 << 0)) {
		DPRINTF(("SPEAKER "));
		x &= ~SCREAMER_MUTE_SPEAKER;
	}
	if (mask & (1 << 1)) {
		DPRINTF(("HEADPHONES "));
		x &= ~SCREAMER_MUTE_HEADPHONES;
	}

	DPRINTF(("\n"));

	if (d->device_id == 5 || d->device_id == 11) {
		DPRINTF(("Enabling programmable output.\n"));
		x |= SCREAMER_PROG_OUTPUT0;
	}
	if (d->device_id == 8 || d->device_id == 11) {
		x &= ~SCREAMER_MUTE_SPEAKER;

		if (mask & (1 << 0))
			x |= SCREAMER_PROG_OUTPUT1; /* enable speaker. */
	}

	screamer_write_locked(d, SCREAMER_CODEC_ADDR1, x);
	d->output_mask = mask;
}

static u_int
screamer_read_status(struct davbus_softc *d, u_int status)
{
	int 	headphones;

	switch (d->device_id) {
	case 5: /* Sawtooth */
		headphones = (status & 0x4);
		break;

	case 8:
	case 11: /* iMac DV */
		/* The iMac DV has 2 headphone outputs. */
		headphones = (status & 0x7);
		break;

	default:
		headphones = (status & 0x8);
	}

	if (headphones)
		return (1 << 1);
	else
		return (1 << 0);
}

static int
screamer_set(struct snd_mixer *m, unsigned dev, unsigned left, unsigned right)
{
	struct davbus_softc *d;
	int lval, rval;

	lval = ((100 - left) * 15 / 100) & 0xf;
	rval = ((100 - right) * 15 / 100) & 0xf;
	DPRINTF(("volume %d %d\n", lval, rval));

	d = mix_getdevinfo(m);

	switch (dev) {
	case SOUND_MIXER_VOLUME:
		mtx_lock(&d->mutex);
		screamer_write_locked(d, SCREAMER_CODEC_ADDR2, (lval << 6) |
		    rval);
		screamer_write_locked(d, SCREAMER_CODEC_ADDR4, (lval << 6) | 
		    rval);
		mtx_unlock(&d->mutex);

		return (left | (right << 8));
	}

	return (0);
}

static u_int32_t
screamer_setrecsrc(struct snd_mixer *m, u_int32_t src)
{
	return (0);
}

static int
davbus_attach(device_t self)
{
	struct davbus_softc 	*sc;
	struct resource 	*dbdma_irq, *cintr;
	void 			*cookie;
	char			 compat[64];
	int 			 rid, oirq, err;

	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);

	sc->aoa.sc_dev = self;
	sc->node = ofw_bus_get_node(self);
	sc->soundnode = OF_child(sc->node);

	/* Map the controller register space. */
	rid = 0;
	sc->reg = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->reg == NULL) 
		return (ENXIO);

	/* Map the DBDMA channel register space. */
	rid = 1;
	sc->aoa.sc_odma = bus_alloc_resource_any(self, SYS_RES_MEMORY, 
	    &rid, RF_ACTIVE);
	if (sc->aoa.sc_odma == NULL)
		return (ENXIO);

	/* Establish the DBDMA channel edge-triggered interrupt. */
	rid = 1;
	dbdma_irq = bus_alloc_resource_any(self, SYS_RES_IRQ, 
	    &rid, RF_SHAREABLE | RF_ACTIVE);
	if (dbdma_irq == NULL)
		return (ENXIO);

	oirq = rman_get_start(dbdma_irq);
	
	DPRINTF(("interrupting at irq %d\n", oirq));

	err = powerpc_config_intr(oirq, INTR_TRIGGER_EDGE, INTR_POLARITY_LOW);
	if (err != 0)
		return (err);
		
	snd_setup_intr(self, dbdma_irq, INTR_MPSAFE, aoa_interrupt,
	    sc, &cookie);

	/* Now initialize the controller. */

	bzero(compat, sizeof(compat));
	OF_getprop(sc->soundnode, "compatible", compat, sizeof(compat));
	OF_getprop(sc->soundnode, "device-id", &sc->device_id, sizeof(u_int));

	mtx_init(&sc->mutex, "DAVbus", NULL, MTX_DEF);

	device_printf(self, "codec: <%s>\n", compat);

	/* Setup the control interrupt. */
	rid = 0;
	cintr = bus_alloc_resource_any(self, SYS_RES_IRQ, 
	     &rid, RF_SHAREABLE | RF_ACTIVE);
	if (cintr != NULL) 
		bus_setup_intr(self, cintr, INTR_TYPE_MISC | INTR_MPSAFE,
		    NULL, davbus_cint, sc, &cookie);
	
	/* Initialize controller registers. */
        bus_write_4(sc->reg, DAVBUS_SOUND_CTRL, DAVBUS_INPUT_SUBFRAME0 | 
	    DAVBUS_OUTPUT_SUBFRAME0 | DAVBUS_RATE_44100 | DAVBUS_INTR_PORTCHG);

	/* Attach DBDMA engine and PCM layer */
	err = aoa_attach(sc);
	if (err)
		return (err);

	/* Install codec module */
	if (strcmp(compat, "screamer") == 0)
		mixer_init(self, &screamer_mixer_class, sc);
	else if (strcmp(compat, "burgundy") == 0)
		mixer_init(self, &burgundy_mixer_class, sc);

	return (0);
}

static void 
davbus_cint(void *ptr)
{
	struct davbus_softc *d = ptr;
	u_int	reg, status, mask;

	mtx_lock(&d->mutex);

	reg = bus_read_4(d->reg, DAVBUS_SOUND_CTRL);
	if (reg & DAVBUS_PORTCHG) {
		
		status = bus_read_4(d->reg, DAVBUS_CODEC_STATUS);
		
		if (d->read_status && d->set_outputs) {

			mask = (*d->read_status)(d, status);
			(*d->set_outputs)(d, mask);
		}

		/* Clear the interrupt. */
		bus_write_4(d->reg, DAVBUS_SOUND_CTRL, reg);
	}

	mtx_unlock(&d->mutex);
}

