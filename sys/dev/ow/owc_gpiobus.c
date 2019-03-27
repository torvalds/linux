/*-
 * Copyright (c) 2015 M. Warner Losh <imp@freebsd.org>
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/gpio.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>

#ifdef FDT
#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include <dev/gpio/gpiobusvar.h>
#include "gpiobus_if.h"

#include <dev/ow/owll.h>

#define	OW_PIN		0

#define OWC_GPIOBUS_LOCK(_sc)		mtx_lock(&(_sc)->sc_mtx)
#define	OWC_GPIOBUS_UNLOCK(_sc)		mtx_unlock(&(_sc)->sc_mtx)
#define OWC_GPIOBUS_LOCK_INIT(_sc) \
	mtx_init(&_sc->sc_mtx, device_get_nameunit(_sc->sc_dev), \
	    "owc_gpiobus", MTX_DEF)
#define OWC_GPIOBUS_LOCK_DESTROY(_sc)	mtx_destroy(&_sc->sc_mtx);

struct owc_gpiobus_softc 
{
	device_t	sc_dev;
	device_t	sc_busdev;
	struct mtx	sc_mtx;
};

static int owc_gpiobus_probe(device_t);
static int owc_gpiobus_attach(device_t);
static int owc_gpiobus_detach(device_t);

#ifdef FDT
static void
owc_gpiobus_identify(driver_t *driver, device_t bus)
{
	phandle_t w1, root;

	/*
	 * Find all the 1-wire bus pseudo-nodes that are
	 * at the top level of the FDT. Would be nice to
	 * somehow preserve the node name of these busses,
	 * but there's no good place to put it. The driver's
	 * name is used for the device name, and the 1-wire
	 * bus overwrites the description.
	 */
	root = OF_finddevice("/");
	if (root == -1)
		return;
	for (w1 = OF_child(root); w1 != 0; w1 = OF_peer(w1)) {
		if (!fdt_is_compatible_strict(w1, "w1-gpio"))
			continue;
		if (!OF_hasprop(w1, "gpios"))
			continue;
		ofw_gpiobus_add_fdt_child(bus, driver->name, w1);
	}
}
#endif

static int
owc_gpiobus_probe(device_t dev)
{
#ifdef FDT
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "w1-gpio")) {
		device_set_desc(dev, "FDT GPIO attached one-wire bus");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
#else
	device_set_desc(dev, "GPIO attached one-wire bus");
	return 0;
#endif
}

static int
owc_gpiobus_attach(device_t dev)
{
	struct owc_gpiobus_softc *sc;
	device_t *kids;
	int nkid;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_busdev = device_get_parent(dev);
	OWC_GPIOBUS_LOCK_INIT(sc);
	nkid = 0;
	if (device_get_children(dev, &kids, &nkid) == 0)
		free(kids, M_TEMP);
	if (nkid == 0)
		device_add_child(dev, "ow", -1);
	bus_generic_attach(dev);

	return (0);
}

static int
owc_gpiobus_detach(device_t dev)
{
	struct owc_gpiobus_softc *sc;

	sc = device_get_softc(dev);
	OWC_GPIOBUS_LOCK_DESTROY(sc);
	bus_generic_detach(dev);
	return (0);
}

/*
 * In the diagrams below, R is driven by the resistor pullup, M is driven by the
 * master, and S is driven by the slave / target.
 */

/*
 * These macros let what why we're doing stuff shine in the code
 * below, and let the how be confined to here.
 */
#define GETBUS(sc)	GPIOBUS_ACQUIRE_BUS((sc)->sc_busdev,	\
			    (sc)->sc_dev, GPIOBUS_WAIT)
#define RELBUS(sc)	GPIOBUS_RELEASE_BUS((sc)->sc_busdev,	\
			    (sc)->sc_dev)
#define OUTPIN(sc)	GPIOBUS_PIN_SETFLAGS((sc)->sc_busdev, \
			    (sc)->sc_dev, OW_PIN, GPIO_PIN_OUTPUT)
#define INPIN(sc)	GPIOBUS_PIN_SETFLAGS((sc)->sc_busdev, \
			    (sc)->sc_dev, OW_PIN, GPIO_PIN_INPUT)
#define GETPIN(sc, bit) GPIOBUS_PIN_GET((sc)->sc_busdev, \
			    (sc)->sc_dev, OW_PIN, bit)
#define LOW(sc)		GPIOBUS_PIN_SET((sc)->sc_busdev, \
			    (sc)->sc_dev, OW_PIN, GPIO_PIN_LOW)

/*
 * WRITE-ONE (see owll_if.m for timings) From Figure 4-1 AN-937
 *
 *		       |<---------tSLOT---->|<-tREC->|
 *	High	RRRRM  | 	RRRRRRRRRRRR|RRRRRRRRM
 *		     M |       R |     |  |	      M
 *		      M|      R	 |     |  |	       M
 *	Low	       MMMMMMM	 |     |  |    	        MMMMMM...
 *		       |<-tLOW1->|     |  |
 *		       |<------15us--->|  |
 *                     |<--------60us---->|
 */
static int
owc_gpiobus_write_one(device_t dev, struct ow_timing *t)
{
	struct owc_gpiobus_softc *sc;
	int error;

	sc = device_get_softc(dev);
	error = GETBUS(sc);
	if (error != 0)
		return error;

	critical_enter();

	/* Force low */
	OUTPIN(sc);
	LOW(sc);
	DELAY(t->t_low1);

	/* Allow resistor to float line high */
	INPIN(sc);
	DELAY(t->t_slot - t->t_low1 + t->t_rec);

	critical_exit();
	
	RELBUS(sc);
	
	return 0;
}

/*
 * WRITE-ZERO (see owll_if.m for timings) From Figure 4-2 AN-937
 *
 *		       |<---------tSLOT------>|<-tREC->|
 *	High	RRRRM  | 	            | |RRRRRRRM
 *		     M |                    | R	       M
 *		      M|       	 |     |    |R 	        M
 *	Low	       MMMMMMMMMMMMMMMMMMMMMR  	         MMMMMM...
 *     	       	       |<--15us->|     |    |
 *     	       	       |<------60us--->|    |
 *                     |<-------tLOW0------>|
 */
static int
owc_gpiobus_write_zero(device_t dev, struct ow_timing *t)
{
	struct owc_gpiobus_softc *sc;
	int error;

	sc = device_get_softc(dev);
	error = GETBUS(sc);
	if (error != 0)
		return error;

	critical_enter();

	/* Force low */
	OUTPIN(sc);
	LOW(sc);
	DELAY(t->t_low0);

	/* Allow resistor to float line high */
	INPIN(sc);
	DELAY(t->t_slot - t->t_low0 + t->t_rec);

	critical_exit();

	RELBUS(sc);
	
	return 0;
}

/*
 * READ-DATA (see owll_if.m for timings) From Figure 4-3 AN-937
 *
 *		       |<---------tSLOT------>|<-tREC->|
 *	High	RRRRM  |        rrrrrrrrrrrrrrrRRRRRRRM
 *		     M |       r            | R	       M
 *		      M|      r	        |   |R 	        M
 *	Low	       MMMMMMMSSSSSSSSSSSSSSR  	         MMMMMM...
 *     	       	       |<tLOWR>< sample	>   |
 *     	       	       |<------tRDV---->|   |
 *                                    ->|   |<-tRELEASE
 *
 * r -- allowed to pull high via the resitor when slave writes a 1-bit
 *
 */
static int
owc_gpiobus_read_data(device_t dev, struct ow_timing *t, int *bit)
{
	struct owc_gpiobus_softc *sc;
	int error, sample;
	sbintime_t then, now;

	sc = device_get_softc(dev);
	error = GETBUS(sc);
	if (error != 0)
		return error;

	/* Force low for t_lowr microseconds */
	then = sbinuptime();
	OUTPIN(sc);
	LOW(sc);
	DELAY(t->t_lowr);

	/*
	 * Slave is supposed to hold the line low for t_rdv microseconds for 0
	 * and immediately float it high for a 1. This is measured from the
	 * master's pushing the line low.
	 */
	INPIN(sc);
	critical_enter();
	do {
		now = sbinuptime();
		GETPIN(sc, &sample);
	} while (sbttous(now - then) < t->t_rdv + 2 && sample == 0);
	critical_exit();

	if (sbttons(now - then) < t->t_rdv * 1000)
		*bit = 1;
	else
		*bit = 0;

	/* Wait out the rest of t_slot */
	do {
		now = sbinuptime();
	} while ((now - then) / SBT_1US < t->t_slot);

	RELBUS(sc);
	
	return 0;
}

/*
 * RESET AND PRESENCE PULSE (see owll_if.m for timings) From Figure 4-4 AN-937
 *
 *				    |<---------tRSTH------------>|
 *	High RRRM  |		  | RRRRRRRS	       |  RRRR RRM
 *		 M |		  |R|  	   |S  	       | R	  M
 *		  M|		  R |	   | S	       |R	   M
 *	Low	   MMMMMMMM MMMMMM| |	   |  SSSSSSSSSS	    MMMMMM
 *     	       	   |<----tRSTL--->| |  	   |<-tPDL---->|
 *		   |   	       	->| |<-tR  |	       |
 *				    |<tPDH>|
 *
 * Note: for Regular Speed operations, tRSTL + tR should be less than 960us to
 * avoid interferring with other devices on the bus
 */
static int
owc_gpiobus_reset_and_presence(device_t dev, struct ow_timing *t, int *bit)
{
	struct owc_gpiobus_softc *sc;
	int error;
	int buf = -1;

	sc = device_get_softc(dev);
	error = GETBUS(sc);
	if (error != 0)
		return error;
	

	/* 
	 * Read the current state of the bus. The steady state of an idle bus is
	 * high. Badly wired buses that are missing the required pull up, or
	 * that have a short circuit to ground cause all kinds of mischief when
	 * we try to read them later. Return EIO and release the bus if the bus
	 * is currently low.
	 */
	INPIN(sc);
	GETPIN(sc, &buf);
	if (buf == 0) {
		*bit = -1;
		RELBUS(sc);
		return EIO;
	}

	critical_enter();

	/* Force low */
	OUTPIN(sc);
	LOW(sc);
	DELAY(t->t_rstl);

	/* Allow resistor to float line high and then wait for reset pulse */
	INPIN(sc);
	DELAY(t->t_pdh + t->t_pdl / 2);

	/* Read presence pulse  */
	GETPIN(sc, &buf);
	*bit = !!buf;

	critical_exit();

	DELAY(t->t_rsth - (t->t_pdh + t->t_pdl / 2));	/* Timing not critical for this one */

	/*
	 * Read the state of the bus after we've waited past the end of the rest
	 * window. It should return to high. If it is low, then we have some
	 * problem and should abort the reset.
	 */
	GETPIN(sc, &buf);
	if (buf == 0) {
		*bit = -1;
		RELBUS(sc);
		return EIO;
	}

	RELBUS(sc);

	return 0;
}

static devclass_t owc_gpiobus_devclass;

static device_method_t owc_gpiobus_methods[] = {
	/* Device interface */
#ifdef FDT
	DEVMETHOD(device_identify,	owc_gpiobus_identify),
#endif
	DEVMETHOD(device_probe,		owc_gpiobus_probe),
	DEVMETHOD(device_attach,	owc_gpiobus_attach),
	DEVMETHOD(device_detach,	owc_gpiobus_detach),

	DEVMETHOD(owll_write_one,	owc_gpiobus_write_one),
	DEVMETHOD(owll_write_zero,	owc_gpiobus_write_zero),
	DEVMETHOD(owll_read_data,	owc_gpiobus_read_data),
	DEVMETHOD(owll_reset_and_presence,	owc_gpiobus_reset_and_presence),
	{ 0, 0 }
};

static driver_t owc_gpiobus_driver = {
	"owc",
	owc_gpiobus_methods,
	sizeof(struct owc_gpiobus_softc),
};

DRIVER_MODULE(owc_gpiobus_fdt, gpiobus, owc_gpiobus_driver, owc_gpiobus_devclass, 0, 0);
MODULE_DEPEND(owc_gpiobus_fdt, ow, 1, 1, 1);
