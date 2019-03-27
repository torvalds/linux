/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-3-Clause
 *
 * Copyright (c) 2004 Pyun YongHyeon
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*	$NetBSD: auxio.c,v 1.11 2003/07/15 03:36:04 lukem Exp $	*/

/*-
 * Copyright (c) 2000, 2001 Matthew R. Green
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
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
 */

/*
 * AUXIO registers support on the SBus & EBus2, used for the floppy driver
 * and to control the system LED, for the BLINK option.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/resource.h>
#include <sys/rman.h>

#include <dev/led/led.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/ofw_machdep.h>
#include <machine/resource.h>

#include <sparc64/sbus/sbusvar.h>
#include <dev/auxio/auxioreg.h>

/*
 * On sun4u, auxio exists with one register (LED) on the SBus, and 5
 * registers on the EBus2 (pci) (LED, PCIMODE, FREQUENCY, SCSI
 * OSCILLATOR, and TEMP SENSE.
 */

#define AUXIO_PCIO_LED	0
#define AUXIO_PCIO_PCI	1
#define AUXIO_PCIO_FREQ	2
#define AUXIO_PCIO_OSC	3
#define AUXIO_PCIO_TEMP	4
#define AUXIO_PCIO_NREG	5

struct auxio_softc {
	device_t		sc_dev;

	int			sc_nauxio;
	struct resource		*sc_res[AUXIO_PCIO_NREG];
	int			sc_rid[AUXIO_PCIO_NREG];
	bus_space_tag_t		sc_regt[AUXIO_PCIO_NREG];
	bus_space_handle_t	sc_regh[AUXIO_PCIO_NREG];
	struct cdev		*sc_led_dev;
	u_int32_t		sc_led_stat;

	int			sc_flags;
#define	AUXIO_LEDONLY		0x1
#define	AUXIO_EBUS		0x2
#define	AUXIO_SBUS		0x4

	struct mtx		sc_lock;
};

static void	auxio_led_func(void *arg, int onoff);
static int	auxio_attach_common(struct auxio_softc *);
static int	auxio_bus_probe(device_t);
static int	auxio_sbus_attach(device_t);
static int	auxio_ebus_attach(device_t);
static int	auxio_bus_detach(device_t);
static void	auxio_free_resource(struct auxio_softc *);
static __inline u_int32_t auxio_led_read(struct auxio_softc *);
static __inline void auxio_led_write(struct auxio_softc *, u_int32_t);

/* SBus */
static device_method_t auxio_sbus_methods[] = {
	DEVMETHOD(device_probe,		auxio_bus_probe),
	DEVMETHOD(device_attach,	auxio_sbus_attach),
	DEVMETHOD(device_detach,	auxio_bus_detach),

	DEVMETHOD_END
};

static driver_t auxio_sbus_driver = {
	"auxio",
	auxio_sbus_methods,
	sizeof(struct auxio_softc)
};

static devclass_t	auxio_devclass;
/* The probe order is handled by sbus(4). */
EARLY_DRIVER_MODULE(auxio, sbus, auxio_sbus_driver, auxio_devclass, 0, 0,
    BUS_PASS_DEFAULT);
MODULE_DEPEND(auxio, sbus, 1, 1, 1);

/* EBus */
static device_method_t auxio_ebus_methods[] = {
	DEVMETHOD(device_probe,		auxio_bus_probe),
	DEVMETHOD(device_attach,	auxio_ebus_attach),
	DEVMETHOD(device_detach,	auxio_bus_detach),

	DEVMETHOD_END
};

static driver_t auxio_ebus_driver = {
	"auxio",
	auxio_ebus_methods,
	sizeof(struct auxio_softc)
};

EARLY_DRIVER_MODULE(auxio, ebus, auxio_ebus_driver, auxio_devclass, 0, 0,
    BUS_PASS_DEFAULT);
MODULE_DEPEND(auxio, ebus, 1, 1, 1);
MODULE_VERSION(auxio, 1);

#define AUXIO_LOCK_INIT(sc)	\
	mtx_init(&sc->sc_lock, "auxio mtx", NULL, MTX_DEF)
#define AUXIO_LOCK(sc)		mtx_lock(&sc->sc_lock)
#define AUXIO_UNLOCK(sc)	mtx_unlock(&sc->sc_lock)
#define AUXIO_LOCK_DESTROY(sc)	mtx_destroy(&sc->sc_lock)

static __inline void
auxio_led_write(struct auxio_softc *sc, u_int32_t v)
{
	if (sc->sc_flags & AUXIO_EBUS)
		bus_space_write_4(sc->sc_regt[AUXIO_PCIO_LED],
		    sc->sc_regh[AUXIO_PCIO_LED], 0, v);
	else
		bus_space_write_1(sc->sc_regt[AUXIO_PCIO_LED],
		    sc->sc_regh[AUXIO_PCIO_LED], 0, v);
}

static __inline u_int32_t
auxio_led_read(struct auxio_softc *sc)
{
	u_int32_t led;

	if (sc->sc_flags & AUXIO_EBUS)
		led = bus_space_read_4(sc->sc_regt[AUXIO_PCIO_LED],
		    sc->sc_regh[AUXIO_PCIO_LED], 0);
	else
		led = bus_space_read_1(sc->sc_regt[AUXIO_PCIO_LED],
		    sc->sc_regh[AUXIO_PCIO_LED], 0);

	return (led);
}

static void
auxio_led_func(void *arg, int onoff)
{
	struct auxio_softc *sc;
	u_int32_t led;

	sc = (struct auxio_softc *)arg;

	AUXIO_LOCK(sc);
	/*
	 * NB: We must not touch the other bits of the SBus AUXIO reg.
	 */
	led = auxio_led_read(sc);
	if (onoff)
		led |= AUXIO_LED_LED;
	else
		led &= ~AUXIO_LED_LED;
	auxio_led_write(sc, led);
	AUXIO_UNLOCK(sc);
}

static int
auxio_bus_probe(device_t dev)
{
	const char *name;

	name = ofw_bus_get_name(dev);
	if (strcmp("auxio", name) == 0) {
		device_set_desc(dev, "Sun Auxiliary I/O");
		return (0);
	}

	return (ENXIO);
}

static int
auxio_ebus_attach(device_t dev)
{
	struct auxio_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	AUXIO_LOCK_INIT(sc);
	sc->sc_nauxio = AUXIO_PCIO_NREG;
	sc->sc_flags = AUXIO_LEDONLY | AUXIO_EBUS;

	return(auxio_attach_common(sc));
}

static int
auxio_attach_common(struct auxio_softc *sc)
{
	struct resource *res;
	int i;

	for (i = 0; i < sc->sc_nauxio; i++) {
		sc->sc_rid[i] = i;
		res = bus_alloc_resource_any(sc->sc_dev, SYS_RES_MEMORY,
		    &sc->sc_rid[i], RF_ACTIVE);
		if (res == NULL) {
			device_printf(sc->sc_dev,
			    "could not allocate resources\n");
			goto attach_fail;
		}
		sc->sc_res[i] = res;
		sc->sc_regt[i] = rman_get_bustag(res);
		sc->sc_regh[i] = rman_get_bushandle(res);
	}

	sc->sc_led_stat = auxio_led_read(sc) & AUXIO_LED_LED;
	sc->sc_led_dev = led_create(auxio_led_func, sc, "auxioled");
	/* turn on the LED */
	auxio_led_func(sc, 1);

	return (0);

attach_fail:
	auxio_free_resource(sc);

	return (ENXIO);
}

static int
auxio_bus_detach(device_t dev)
{
	struct auxio_softc *sc;

	sc = device_get_softc(dev);
	led_destroy(sc->sc_led_dev);
	auxio_led_func(sc, sc->sc_led_stat);
	auxio_free_resource(sc);

	return (0);
}

static void
auxio_free_resource(struct auxio_softc *sc)
{
	int i;

	for (i = 0; i < sc->sc_nauxio; i++)
		if (sc->sc_res[i])
			bus_release_resource(sc->sc_dev, SYS_RES_MEMORY,
			    sc->sc_rid[i], sc->sc_res[i]);
	AUXIO_LOCK_DESTROY(sc);
}

static int
auxio_sbus_attach(device_t dev)
{
	struct auxio_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	AUXIO_LOCK_INIT(sc);
	sc->sc_nauxio = 1;
	sc->sc_flags = AUXIO_LEDONLY | AUXIO_SBUS;

	return (auxio_attach_common(sc));
}
