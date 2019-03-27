/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2013 Rui Paulo <rpaulo@FreeBSD.org>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/frame.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include <arm/ti/ti_mbox.h>
#include <arm/ti/ti_prcm.h>

#include "mbox_if.h"

#ifdef DEBUG
#define	DPRINTF(fmt, ...)	do {	\
	printf("%s: ", __func__);	\
	printf(fmt, __VA_ARGS__);	\
} while (0)
#else
#define	DPRINTF(fmt, ...)
#endif

static device_probe_t		ti_mbox_probe;
static device_attach_t		ti_mbox_attach;
static device_detach_t		ti_mbox_detach;
static void			ti_mbox_intr(void *);
static int			ti_mbox_read(device_t, int, uint32_t *);
static int			ti_mbox_write(device_t, int, uint32_t);

struct ti_mbox_softc {
	struct mtx		sc_mtx;
	struct resource		*sc_mem_res;
	struct resource		*sc_irq_res;
	void			*sc_intr;
	bus_space_tag_t		sc_bt;
	bus_space_handle_t	sc_bh;
};

#define	TI_MBOX_LOCK(sc)	mtx_lock(&(sc)->sc_mtx)
#define	TI_MBOX_UNLOCK(sc)	mtx_unlock(&(sc)->sc_mtx)

static device_method_t ti_mbox_methods[] = {
	DEVMETHOD(device_probe,		ti_mbox_probe),
	DEVMETHOD(device_attach,	ti_mbox_attach),
	DEVMETHOD(device_detach,	ti_mbox_detach),

	DEVMETHOD(mbox_read,		ti_mbox_read),
	DEVMETHOD(mbox_write,		ti_mbox_write),

	DEVMETHOD_END
};

static driver_t ti_mbox_driver = {
	"ti_mbox",
	ti_mbox_methods,
	sizeof(struct ti_mbox_softc)
};

static devclass_t ti_mbox_devclass;

DRIVER_MODULE(ti_mbox, simplebus, ti_mbox_driver, ti_mbox_devclass, 0, 0);

static __inline uint32_t
ti_mbox_reg_read(struct ti_mbox_softc *sc, uint16_t reg)
{
	return (bus_space_read_4(sc->sc_bt, sc->sc_bh, reg));
}

static __inline void
ti_mbox_reg_write(struct ti_mbox_softc *sc, uint16_t reg, uint32_t val)
{
	bus_space_write_4(sc->sc_bt, sc->sc_bh, reg, val);
}

static int
ti_mbox_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "ti,omap4-mailbox")) {
		device_set_desc(dev, "TI System Mailbox");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
ti_mbox_attach(device_t dev)
{
	struct ti_mbox_softc *sc;
	int rid, delay, chan;
	uint32_t rev, sysconfig;

	if (ti_prcm_clk_enable(MAILBOX0_CLK) != 0) {
		device_printf(dev, "could not enable MBOX clock\n");
		return (ENXIO);
	}
	sc = device_get_softc(dev);
	rid = 0;
	mtx_init(&sc->sc_mtx, "TI mbox", NULL, MTX_DEF);
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}
	sc->sc_bt = rman_get_bustag(sc->sc_mem_res);
	sc->sc_bh = rman_get_bushandle(sc->sc_mem_res);
	rid = 0;
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->sc_irq_res == NULL) {
		device_printf(dev, "could not allocate interrupt resource\n");
		ti_mbox_detach(dev);
		return (ENXIO);
	}
	if (bus_setup_intr(dev, sc->sc_irq_res, INTR_MPSAFE | INTR_TYPE_MISC,
	    NULL, ti_mbox_intr, sc, &sc->sc_intr) != 0) {
		device_printf(dev, "unable to setup the interrupt handler\n");
		ti_mbox_detach(dev);
		return (ENXIO);
	}
	/*
	 * Reset the controller.
	 */
	sysconfig = ti_mbox_reg_read(sc, TI_MBOX_SYSCONFIG);
	DPRINTF("initial sysconfig %d\n", sysconfig);
	sysconfig |= TI_MBOX_SYSCONFIG_SOFTRST;
	delay = 100;
	while (ti_mbox_reg_read(sc, TI_MBOX_SYSCONFIG) & 
	    TI_MBOX_SYSCONFIG_SOFTRST) {
		delay--;
		DELAY(10);
	}
	if (delay == 0) {
		device_printf(dev, "controller reset failed\n");
		ti_mbox_detach(dev);
		return (ENXIO);
	}
	/*
	 * Enable smart idle mode.
	 */
	ti_mbox_reg_write(sc, TI_MBOX_SYSCONFIG,
	    ti_mbox_reg_read(sc, TI_MBOX_SYSCONFIG) | TI_MBOX_SYSCONFIG_SMARTIDLE);
	rev = ti_mbox_reg_read(sc, TI_MBOX_REVISION);
	DPRINTF("rev %d\n", rev);
	device_printf(dev, "revision %d.%d\n", (rev >> 8) & 0x4, rev & 0x40);
	/*
	 * Enable message interrupts.
	 */
	for (chan = 0; chan < 8; chan++)
		ti_mbox_reg_write(sc, TI_MBOX_IRQENABLE_SET(chan), 1);

	return (0);
}

static int
ti_mbox_detach(device_t dev)
{
	struct ti_mbox_softc *sc;

	sc = device_get_softc(dev);
	if (sc->sc_intr)
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_intr);
	if (sc->sc_irq_res)
		bus_release_resource(dev, SYS_RES_IRQ, rman_get_rid(sc->sc_irq_res),
		    sc->sc_irq_res);
	if (sc->sc_mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY, rman_get_rid(sc->sc_mem_res),
		    sc->sc_mem_res);

	return (0);
}

static void
ti_mbox_intr(void *arg)
{
	struct ti_mbox_softc *sc;

	sc = arg;
	DPRINTF("interrupt %p", sc);
}

static int
ti_mbox_read(device_t dev, int chan, uint32_t *data)
{
	struct ti_mbox_softc *sc;

	if (chan < 0 || chan > 7)
		return (EINVAL);
	sc = device_get_softc(dev);

	return (ti_mbox_reg_read(sc, TI_MBOX_MESSAGE(chan)));
}

static int
ti_mbox_write(device_t dev, int chan, uint32_t data)
{
	int limit = 500;
	struct ti_mbox_softc *sc;

	if (chan < 0 || chan > 7)
		return (EINVAL);
	sc = device_get_softc(dev);
	TI_MBOX_LOCK(sc);
	/* XXX implement interrupt method */
	while (ti_mbox_reg_read(sc, TI_MBOX_FIFOSTATUS(chan)) == 1 && 
	    limit--) {
		DELAY(10);
	}
	if (limit == 0) {
		device_printf(dev, "FIFOSTAUS%d stuck\n", chan);
		TI_MBOX_UNLOCK(sc);
		return (EAGAIN);
	}
	ti_mbox_reg_write(sc, TI_MBOX_MESSAGE(chan), data);

	return (0);
}
