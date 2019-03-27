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
#include <dev/ofw/ofw_bus.h>

#ifdef HAVE_KERNEL_OPTION_HEADERS
#include "opt_snd.h"
#endif

#include <dev/sound/pcm/sound.h>
#include <dev/sound/macio/aoa.h>

#include <powerpc/powermac/macgpiovar.h>

struct i2s_softc {
	struct aoa_softc 	 aoa;
	phandle_t 		 node;
	phandle_t		 soundnode;
	struct resource 	*reg;
	u_int 			 output_mask;
	struct mtx 		 port_mtx;
};

static int 	i2s_probe(device_t);
static int 	i2s_attach(device_t);
static void 	i2s_postattach(void *);
static int 	i2s_setup(struct i2s_softc *, u_int, u_int, u_int);
static void     i2s_mute_headphone (struct i2s_softc *, int);
static void     i2s_mute_lineout   (struct i2s_softc *, int);
static void     i2s_mute_speaker   (struct i2s_softc *, int);
static void 	i2s_set_outputs(void *, u_int);

static struct intr_config_hook 	*i2s_delayed_attach = NULL;

kobj_class_t	i2s_mixer_class = NULL;
device_t	i2s_mixer = NULL;

static device_method_t pcm_i2s_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		i2s_probe),
	DEVMETHOD(device_attach, 	i2s_attach),

	{ 0, 0 }
};

static driver_t pcm_i2s_driver = {
	"pcm",
	pcm_i2s_methods,
	PCM_SOFTC_SIZE
};

DRIVER_MODULE(pcm_i2s, macio, pcm_i2s_driver, pcm_devclass, 0, 0);
MODULE_DEPEND(pcm_i2s, sound, SOUND_MINVER, SOUND_PREFVER, SOUND_MAXVER);

static int	aoagpio_probe(device_t);
static int 	aoagpio_attach(device_t);

static device_method_t aoagpio_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		aoagpio_probe),
	DEVMETHOD(device_attach,	aoagpio_attach),

	{ 0, 0 }
};

struct aoagpio_softc {
	device_t 		 dev;
	int 			 ctrl;
	int			 detect_active; /* for extint-gpio */
	int			 level;		/* for extint-gpio */
	struct i2s_softc	*i2s;		/* for extint-gpio */
};

static driver_t aoagpio_driver = {
	"aoagpio",
	aoagpio_methods,
	sizeof(struct aoagpio_softc)
};
static devclass_t aoagpio_devclass;

DRIVER_MODULE(aoagpio, macgpio, aoagpio_driver, aoagpio_devclass, 0, 0);


/*****************************************************************************
			Probe and attachment routines.
 *****************************************************************************/
static int
i2s_probe(device_t self)
{
	const char 		*name;
	phandle_t		subchild;
	char			subchildname[255];

	name = ofw_bus_get_name(self);
	if (!name)
		return (ENXIO);

	if (strcmp(name, "i2s") != 0)
		return (ENXIO);

	/*
	 * Do not attach to "lightshow" I2S devices on Xserves. This controller
	 * is used there to control the LEDs on the front panel, and this
	 * driver can't handle it.
	 */
	subchild = OF_child(OF_child(ofw_bus_get_node(self)));
	if (subchild != 0 && OF_getprop(subchild, "name", subchildname,
	    sizeof(subchildname)) > 0 && strcmp(subchildname, "lightshow") == 0)
		return (ENXIO);
	
	device_set_desc(self, "Apple I2S Audio Controller");

	return (0);
}

static phandle_t of_find_firstchild_byname(phandle_t, const char *);

static int
i2s_attach(device_t self)
{
	struct i2s_softc 	*sc;
	struct resource 	*dbdma_irq;
	void			*dbdma_ih;
	int 			 rid, oirq, err;
	phandle_t 		 port;
	
	sc = malloc(sizeof(*sc), M_DEVBUF, M_WAITOK | M_ZERO);

	sc->aoa.sc_dev = self;
	sc->node = ofw_bus_get_node(self);

	port = of_find_firstchild_byname(sc->node, "i2s-a");
	if (port == -1)
		return (ENXIO);
	sc->soundnode = of_find_firstchild_byname(port, "sound");
	if (sc->soundnode == -1)
		return (ENXIO);
 
	mtx_init(&sc->port_mtx, "port_mtx", NULL, MTX_DEF);

	/* Map the controller register space. */
	rid = 0;
	sc->reg = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->reg == NULL)
		return ENXIO;

	/* Map the DBDMA channel register space. */
	rid = 1;
	sc->aoa.sc_odma = bus_alloc_resource_any(self, SYS_RES_MEMORY, &rid, 
	    RF_ACTIVE);
	if (sc->aoa.sc_odma == NULL)
		return ENXIO;

	/* Establish the DBDMA channel edge-triggered interrupt. */
	rid = 1;
	dbdma_irq = bus_alloc_resource_any(self, SYS_RES_IRQ, 
	    &rid, RF_SHAREABLE | RF_ACTIVE);
	if (dbdma_irq == NULL)
		return (ENXIO);

	/* Now initialize the controller. */
	err = i2s_setup(sc, 44100, 16, 64);
	if (err != 0)
		return (err);

	snd_setup_intr(self, dbdma_irq, INTR_MPSAFE, aoa_interrupt,
	    sc, &dbdma_ih);

	oirq = rman_get_start(dbdma_irq);
	err = powerpc_config_intr(oirq, INTR_TRIGGER_EDGE, INTR_POLARITY_LOW);
	if (err != 0)
		return (err);

	/*
	 * Register a hook for delayed attach in order to allow
	 * the I2C controller to attach.
	 */
	if ((i2s_delayed_attach = malloc(sizeof(struct intr_config_hook), 
	    M_TEMP, M_WAITOK | M_ZERO)) == NULL)
		return (ENOMEM);

	i2s_delayed_attach->ich_func = i2s_postattach;
	i2s_delayed_attach->ich_arg = sc;

	if (config_intrhook_establish(i2s_delayed_attach) != 0)
		return (ENOMEM);

	return (aoa_attach(sc));
}

/*****************************************************************************
				GPIO routines.
 *****************************************************************************/

enum gpio_ctrl {
	AMP_MUTE,
	HEADPHONE_MUTE,
	LINEOUT_MUTE,
	AUDIO_HW_RESET,
	HEADPHONE_DETECT,
	LINEOUT_DETECT,
	GPIO_CTRL_NUM
};

#define GPIO_CTRL_EXTINT_SET               \
		((1 << HEADPHONE_DETECT) | \
		 (1 << LINEOUT_DETECT))

static struct aoagpio_softc *gpio_ctrls[GPIO_CTRL_NUM] = 
	{NULL, NULL, NULL, NULL, NULL, NULL};

static struct gpio_match {
	const char	*name;
	enum gpio_ctrl	 ctrl;
} gpio_controls[] = {
	{"headphone-mute",      HEADPHONE_MUTE},
	{"lineout-mute",	LINEOUT_MUTE},
	{"amp-mute",	    	AMP_MUTE},
	{"headphone-detect",    HEADPHONE_DETECT},
	{"lineout-detect",      LINEOUT_DETECT},
	{"line-output-detect",  LINEOUT_DETECT},
	{"audio-hw-reset",      AUDIO_HW_RESET},
	{"hw-reset",	    	AUDIO_HW_RESET},
	{NULL,		 	GPIO_CTRL_NUM}
};

static void 		i2s_cint(struct i2s_softc *);

static void
aoagpio_int(void *cookie)
{
	device_t 		 self = cookie;
	struct aoagpio_softc	*sc;

	sc = device_get_softc(self);
	
	if (macgpio_read(self) & GPIO_LEVEL_RO)
		sc->level = sc->detect_active;
	else
		sc->level = !(sc->detect_active);

	if (sc->i2s)
		i2s_cint(sc->i2s);
}

static int
aoagpio_probe(device_t gpio)
{
	phandle_t	 	 node;
	char			 bname[32];
	const char 		*name;
	struct gpio_match	*m;
	struct aoagpio_softc	*sc;

	node = ofw_bus_get_node(gpio);
	if (node == 0 || node == -1)
		return (EINVAL);

	bzero(bname, sizeof(bname));
	if (OF_getprop(node, "audio-gpio", bname, sizeof(bname)) > 2)
		name = bname;
	else
		name = ofw_bus_get_name(gpio);

	/* Try to find a match. */
	for (m = gpio_controls; m->name != NULL; m++) {
		if (strcmp(name, m->name) == 0) {
			sc = device_get_softc(gpio);
			gpio_ctrls[m->ctrl] = sc;
			sc->dev = gpio;
			sc->ctrl = m->ctrl;
			sc->level = 0;
			sc->detect_active = 0;
			sc->i2s = NULL;

			OF_getprop(node, "audio-gpio-active-state", 
				&sc->detect_active, sizeof(sc->detect_active));

			if ((1 << m->ctrl) & GPIO_CTRL_EXTINT_SET)
				aoagpio_int(gpio);

			device_set_desc(gpio, m->name);
			device_quiet(gpio);
			return (0);
		}
	}

	return (ENXIO);
}

static int 
aoagpio_attach(device_t gpio)
{
	struct aoagpio_softc	*sc;
	struct resource		*r;
	void			*cookie;
	int			 irq, rid = 0;

	sc = device_get_softc(gpio);

	if ((1 << sc->ctrl) & GPIO_CTRL_EXTINT_SET) {
		r = bus_alloc_resource_any(gpio, SYS_RES_IRQ, &rid, RF_ACTIVE);
		if (r == NULL)
			return (ENXIO);

		irq = rman_get_start(r);
		DPRINTF(("interrupting at irq %d\n", irq));

		if (powerpc_config_intr(irq, INTR_TRIGGER_EDGE, 
		    INTR_POLARITY_LOW) != 0) 
			return (ENXIO);

		bus_setup_intr(gpio, r, INTR_TYPE_MISC | INTR_MPSAFE |
		    INTR_ENTROPY, NULL, aoagpio_int, gpio, &cookie);
	}

	return (0);
}

/*
 * I2S module registers
 */
#define I2S_INT			0x00
#define I2S_FORMAT		0x10
#define I2S_FRAMECOUNT		0x40
#define I2S_FRAMEMATCH		0x50
#define I2S_WORDSIZE		0x60

/* I2S_INT register definitions */
#define I2S_INT_CLKSTOPPEND     0x01000000  /* clock-stop interrupt pending */

/* I2S_FORMAT register definitions */
#define CLKSRC_49MHz		0x80000000      /* Use 49152000Hz Osc. */
#define CLKSRC_45MHz		0x40000000      /* Use 45158400Hz Osc. */
#define CLKSRC_18MHz		0x00000000      /* Use 18432000Hz Osc. */
#define MCLK_DIV_MASK		0x1f000000      /* MCLK = SRC / DIV */
#define SCLK_DIV_MASK		0x00f00000      /* SCLK = MCLK / DIV */
#define SCLK_MASTER		0x00080000      /* Master mode */
#define SCLK_SLAVE		0x00000000      /* Slave mode */
#define SERIAL_FORMAT		0x00070000
#define  SERIAL_SONY		0x00000000
#define  SERIAL_64x		0x00010000
#define  SERIAL_32x		0x00020000
#define  SERIAL_DAV		0x00040000
#define  SERIAL_SILICON		0x00050000

/* I2S_WORDSIZE register definitions */
#define INPUT_STEREO		(2 << 24)
#define INPUT_MONO		(1 << 24)
#define INPUT_16BIT		(0 << 16)
#define INPUT_24BIT		(3 << 16)
#define OUTPUT_STEREO		(2 << 8)
#define OUTPUT_MONO		(1 << 8)
#define OUTPUT_16BIT		(0 << 0)
#define OUTPUT_24BIT		(3 << 0)

/* Master clock, needed by some codecs. We hardcode this 
   to 256 * fs as this is valid for most codecs. */
#define MCLK_FS		256

/* Number of clock sources we can use. */
#define NCLKS	   3
static const struct i2s_clksrc {
	u_int cs_clock;
	u_int cs_reg;
} clksrc[NCLKS] = {
	{49152000, CLKSRC_49MHz},
	{45158400, CLKSRC_45MHz},
	{18432000, CLKSRC_18MHz}
};

/* Configure the I2S controller for the required settings.
   'rate' is the frame rate.
   'wordsize' is the sample size (usually 16 bits).
   'sclk_fs' is the SCLK/framerate ratio, which needs to be equal 
   or greater to the number of bits per frame. */

static int
i2s_setup(struct i2s_softc *sc, u_int rate, u_int wordsize, u_int sclk_fs)
{
	u_int mclk, mdiv, sdiv;
	u_int reg = 0, x, wordformat;
	u_int i;

	/* Make sure the settings are consistent... */
	if ((wordsize * 2) > sclk_fs)
		return (EINVAL);

	if (sclk_fs != 32 && sclk_fs != 64)
		return (EINVAL);
	
	/*
	 *	Find a clock source to derive the master clock (MCLK)
	 *	and the I2S bit block (SCLK) and set the divisors as
	 *	appropriate.
	 */
	mclk = rate * MCLK_FS;
	sdiv = MCLK_FS / sclk_fs;

	for (i = 0; i < NCLKS; ++i) {
		if ((clksrc[i].cs_clock % mclk) == 0) {
			reg = clksrc[i].cs_reg;
			mdiv = clksrc[i].cs_clock / mclk;
			break;
		}
	}
	if (reg == 0)
		return (EINVAL);
	
	switch (mdiv) {
	/* exception cases */
	case 1:
		x = 14;
		break;
	case 3:
		x = 13;
		break;
	case 5:
		x = 12;
		break;
	default:
		x = (mdiv / 2) - 1;
		break;
	}
	reg |= (x << 24) & MCLK_DIV_MASK;
		
	switch (sdiv) {
	case 1:
		x = 8;
		break;
	case 3:
		x = 9;
		break;
	default:
		x = (sdiv / 2) - 1;
		break;
	}
	reg |= (x << 20) & SCLK_DIV_MASK;

	/*
	 * 	XXX use master mode for now. This needs to be 
	 * 	revisited if we want to add recording from SPDIF some day.
	 */
	reg |= SCLK_MASTER;
	
	switch (sclk_fs) {
	case 64:
		reg |= SERIAL_64x;
		break;
	case 32:
		reg |= SERIAL_32x;
		break;
	}

	/* stereo input and output */
	wordformat = INPUT_STEREO | OUTPUT_STEREO;

	switch (wordsize) {
	case 16:
		wordformat |= INPUT_16BIT | OUTPUT_16BIT;
		break;
	case 24:
		wordformat |= INPUT_24BIT | OUTPUT_24BIT;
		break;
	default:
		return (EINVAL);
	}

	x = bus_read_4(sc->reg, I2S_WORDSIZE);
	if (x != wordformat)
		bus_write_4(sc->reg, I2S_WORDSIZE, wordformat);

	x = bus_read_4(sc->reg, I2S_FORMAT);
	if (x != reg) {
		/*
		 * 	XXX to change the format we need to stop the clock
		 * 	via the FCR registers. For now, rely on the firmware
		 *	to set sane defaults (44100).
		 */
		printf("i2s_setup: changing format not supported yet.\n");
		return (ENOTSUP);

#ifdef notyet
		if (obio_fcr_isset(OBIO_FCR1, I2S0CLKEN)) {
			
			bus_space_write_4(sc->sc_tag, sc->sc_bsh, I2S_INT, 
					  I2S_INT_CLKSTOPPEND);
			
			obio_fcr_clear(OBIO_FCR1, I2S0CLKEN);

			for (timo = 1000; timo > 0; timo--) {
				if (bus_space_read_4(sc->sc_tag, sc->sc_bsh,
				    I2S_INT) & I2S_INT_CLKSTOPPEND)
					break;
				
				DELAY(10);
			}

			if (timo == 0)
				printf("%s: timeout waiting for clock to stop\n",
					sc->sc_dev.dv_xname);
		}

		bus_space_write_4(sc->sc_tag, sc->sc_bsh, I2S_FORMAT, reg);
		
		obio_fcr_set(OBIO_FCR1, I2S0CLKEN);
#endif
	}

	return (0);
}


/* XXX this does not belong here. */
static phandle_t
of_find_firstchild_byname(phandle_t node, const char *req_name)
{
	char 		name[32]; /* max name len per OF spec. */
	phandle_t 	n;

	for (n = OF_child(node); n != -1; n = OF_peer(n)) {
		bzero(name, sizeof(name));
		OF_getprop(n, "name", name, sizeof(name));

		if (strcmp(name, req_name) == 0)
			return (n);
	}

	return (-1);
}


static u_int
gpio_read(enum gpio_ctrl ctrl)
{
	struct aoagpio_softc *sc;

	if ((sc = gpio_ctrls[ctrl]) == NULL)
		return (0);

	return (macgpio_read(sc->dev) & GPIO_DATA);
}

static void
gpio_write(enum gpio_ctrl ctrl, u_int x)
{
	struct aoagpio_softc 	*sc;
	u_int 			 reg;

	if ((sc = gpio_ctrls[ctrl]) == NULL)
		return;

	reg = GPIO_DDR_OUTPUT;
	if (x)
		reg |= GPIO_DATA;

	macgpio_write(sc->dev, reg);
}

static void 
i2s_cint(struct i2s_softc *sc)
{
	u_int mask = 0;

	if (gpio_ctrls[HEADPHONE_DETECT] && 
	    gpio_ctrls[HEADPHONE_DETECT]->level)
		mask |= 1 << 1;

	if (gpio_ctrls[LINEOUT_DETECT] && 
	    gpio_ctrls[LINEOUT_DETECT]->level)
		mask |= 1 << 2;

	if (mask == 0)
		mask = 1 << 0; /* fall back to speakers. */

	i2s_set_outputs(sc, mask);
}

#define reset_active	    0

/* these values are in microseconds */
#define RESET_SETUP_TIME	5000
#define RESET_HOLD_TIME		20000
#define RESET_RELEASE_TIME	10000

static void
i2s_audio_hw_reset(struct i2s_softc *sc)
{
	if (gpio_ctrls[AUDIO_HW_RESET]) {
		DPRINTF(("resetting codec\n"));

		gpio_write(AUDIO_HW_RESET, !reset_active);   /* Negate RESET */
		DELAY(RESET_SETUP_TIME);

		gpio_write(AUDIO_HW_RESET, reset_active);    /* Assert RESET */
		DELAY(RESET_HOLD_TIME);

		gpio_write(AUDIO_HW_RESET, !reset_active);   /* Negate RESET */
		DELAY(RESET_RELEASE_TIME);
	
	} else {
		DPRINTF(("no audio_hw_reset\n"));
	}
}

#define AMP_ACTIVE       0	      /* XXX OF */
#define HEADPHONE_ACTIVE 0	      /* XXX OF */
#define LINEOUT_ACTIVE   0	      /* XXX OF */

#define MUTE_CONTROL(xxx, yyy)				\
static void 						\
i2s_mute_##xxx(struct i2s_softc *sc, int mute)		\
{							\
	int 		x;				\
							\
	if (gpio_ctrls[yyy##_MUTE] == NULL)		\
		return;					\
	if (mute)					\
		x = yyy##_ACTIVE;			\
	else						\
		x = ! yyy##_ACTIVE;			\
							\
	if (x != gpio_read(yyy##_MUTE))			\
		gpio_write(yyy##_MUTE, x);		\
}

MUTE_CONTROL(speaker, AMP)
MUTE_CONTROL(headphone, HEADPHONE)
MUTE_CONTROL(lineout, LINEOUT)

static void
i2s_set_outputs(void *ptr, u_int mask)
{
	struct i2s_softc 	*sc = ptr;

	if (mask == sc->output_mask)
		return;

	mtx_lock(&sc->port_mtx);

	i2s_mute_speaker(sc, 1);
	i2s_mute_headphone(sc, 1);
	i2s_mute_lineout(sc, 1);

	DPRINTF(("enabled outputs: "));

	if (mask & (1 << 0)) {
		DPRINTF(("SPEAKER "));
		i2s_mute_speaker(sc, 0);
	} 
	if (mask & (1 << 1)) {
		DPRINTF(("HEADPHONE "));
		i2s_mute_headphone(sc, 0);
	}
	if (mask & (1 << 2)) {
		DPRINTF(("LINEOUT "));
		i2s_mute_lineout(sc, 0);
	}

	DPRINTF(("\n"));
	sc->output_mask = mask;

	mtx_unlock(&sc->port_mtx);
}

static void
i2s_postattach(void *xsc)
{
	struct i2s_softc 	*sc = xsc;
	device_t 		 self;
	int 			 i;

	self = sc->aoa.sc_dev;

	/* Reset the codec. */
	i2s_audio_hw_reset(sc);

	/* If we have a codec, initialize it. */
	if (i2s_mixer)
		mixer_init(self, i2s_mixer_class, i2s_mixer);

	/* Read initial port status. */
	i2s_cint(sc);

	/* Enable GPIO interrupt callback. */	
	for (i = 0; i < GPIO_CTRL_NUM; i++)
		if (gpio_ctrls[i])
			gpio_ctrls[i]->i2s = sc;

	config_intrhook_disestablish(i2s_delayed_attach);
	free(i2s_delayed_attach, M_TEMP);
}

