/*-
 * Copyright (c) 2014 Rui Paulo <rpaulo@FreeBSD.org>
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
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/event.h>
#include <sys/selinfo.h>
#include <sys/watchdog.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/frame.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_wdt.h>

#ifdef DEBUG
#define	DPRINTF(fmt, ...)	do {	\
	printf("%s: ", __func__);	\
	printf(fmt, __VA_ARGS__);	\
} while (0)
#else
#define	DPRINTF(fmt, ...)
#endif

static device_probe_t		ti_wdt_probe;
static device_attach_t		ti_wdt_attach;
static device_detach_t		ti_wdt_detach;
static void			ti_wdt_intr(void *);
static void			ti_wdt_event(void *, unsigned int, int *);

struct ti_wdt_softc {
	struct resource 	*sc_mem_res;
	struct resource 	*sc_irq_res;
	void            	*sc_intr;
	bus_space_tag_t		sc_bt;
	bus_space_handle_t	sc_bh;
	eventhandler_tag	sc_ev_tag;
};

static device_method_t ti_wdt_methods[] = {
	DEVMETHOD(device_probe,		ti_wdt_probe),
	DEVMETHOD(device_attach,	ti_wdt_attach),
	DEVMETHOD(device_detach,	ti_wdt_detach),

	DEVMETHOD_END
};

static driver_t ti_wdt_driver = {
	"ti_wdt",
	ti_wdt_methods,
	sizeof(struct ti_wdt_softc)
};

static devclass_t ti_wdt_devclass;

DRIVER_MODULE(ti_wdt, simplebus, ti_wdt_driver, ti_wdt_devclass, 0, 0);

static __inline uint32_t
ti_wdt_reg_read(struct ti_wdt_softc *sc, uint32_t reg)
{

	return (bus_space_read_4(sc->sc_bt, sc->sc_bh, reg));
}

static __inline void
ti_wdt_reg_write(struct ti_wdt_softc *sc, uint32_t reg, uint32_t val)
{

	bus_space_write_4(sc->sc_bt, sc->sc_bh, reg, val);
}

/*
 * Wait for the write to a specific synchronised register to complete.
 */
static __inline void
ti_wdt_reg_wait(struct ti_wdt_softc *sc, uint32_t bit)
{

	while (ti_wdt_reg_read(sc, TI_WDT_WWPS) & bit)
		DELAY(10);
}

static __inline void
ti_wdt_disable(struct ti_wdt_softc *sc)
{

	DPRINTF("disabling watchdog %p\n", sc);
	ti_wdt_reg_write(sc, TI_WDT_WSPR, 0xAAAA);
	ti_wdt_reg_wait(sc, TI_W_PEND_WSPR);
	ti_wdt_reg_write(sc, TI_WDT_WSPR, 0x5555);
	ti_wdt_reg_wait(sc, TI_W_PEND_WSPR);
}

static __inline void
ti_wdt_enable(struct ti_wdt_softc *sc)
{

	DPRINTF("enabling watchdog %p\n", sc);
	ti_wdt_reg_write(sc, TI_WDT_WSPR, 0xBBBB);
	ti_wdt_reg_wait(sc, TI_W_PEND_WSPR);
	ti_wdt_reg_write(sc, TI_WDT_WSPR, 0x4444);
	ti_wdt_reg_wait(sc, TI_W_PEND_WSPR);
}

static int
ti_wdt_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (ofw_bus_is_compatible(dev, "ti,omap3-wdt")) {
		device_set_desc(dev, "TI Watchdog Timer");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
ti_wdt_attach(device_t dev)
{
	struct ti_wdt_softc *sc;
	int rid;

	sc = device_get_softc(dev);
	rid = 0;
	sc->sc_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->sc_mem_res == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}
	sc->sc_bt = rman_get_bustag(sc->sc_mem_res);
	sc->sc_bh = rman_get_bushandle(sc->sc_mem_res);
	sc->sc_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (sc->sc_irq_res == NULL) {
		device_printf(dev, "could not allocate interrupt resource\n");
		ti_wdt_detach(dev);
		return (ENXIO);
	}
	if (bus_setup_intr(dev, sc->sc_irq_res, INTR_MPSAFE | INTR_TYPE_MISC,
		NULL, ti_wdt_intr, sc,  &sc->sc_intr) != 0) {
		device_printf(dev,
		    "unable to setup the interrupt handler\n");
		ti_wdt_detach(dev);
		return (ENXIO);
	}
	/* Reset, enable interrupts and stop the watchdog. */
	ti_wdt_reg_write(sc, TI_WDT_WDSC,
	    ti_wdt_reg_read(sc, TI_WDT_WDSC) | TI_WDSC_SR);
	while (ti_wdt_reg_read(sc, TI_WDT_WDSC) & TI_WDSC_SR)
		DELAY(10);
	ti_wdt_reg_write(sc, TI_WDT_WIRQENSET, TI_IRQ_EN_OVF | TI_IRQ_EN_DLY);
	ti_wdt_disable(sc);
	if (bootverbose)
		device_printf(dev, "revision: 0x%x\n",
		    ti_wdt_reg_read(sc, TI_WDT_WIDR));
	sc->sc_ev_tag = EVENTHANDLER_REGISTER(watchdog_list, ti_wdt_event, sc,
	    0);

	return (0);
}

static int
ti_wdt_detach(device_t dev)
{
	struct ti_wdt_softc *sc;

	sc = device_get_softc(dev);
	if (sc->sc_ev_tag)
		EVENTHANDLER_DEREGISTER(watchdog_list, sc->sc_ev_tag);
	if (sc->sc_intr)
		bus_teardown_intr(dev, sc->sc_irq_res, sc->sc_intr);
	if (sc->sc_irq_res)
		bus_release_resource(dev, SYS_RES_IRQ,
		    rman_get_rid(sc->sc_irq_res), sc->sc_irq_res);
	if (sc->sc_mem_res)
		bus_release_resource(dev, SYS_RES_MEMORY,
		    rman_get_rid(sc->sc_mem_res),  sc->sc_mem_res);

	return (0);
}

static void
ti_wdt_intr(void *arg)
{
	struct ti_wdt_softc *sc;

	sc = arg;
	DPRINTF("interrupt %p", sc);
	ti_wdt_reg_write(sc, TI_WDT_WIRQSTAT, TI_IRQ_EV_OVF | TI_IRQ_EV_DLY);
	/* TODO: handle interrupt */
}

static void
ti_wdt_event(void *arg, unsigned int cmd, int *error)
{
	struct ti_wdt_softc *sc;
	uint8_t s;
	uint32_t wldr;
	uint32_t ptv;

	sc = arg;
	ti_wdt_disable(sc);
	if (cmd == WD_TO_NEVER) {
		*error = 0;
		return;
	}
	DPRINTF("cmd 0x%x\n", cmd);
	cmd &= WD_INTERVAL;
	if (cmd < WD_TO_1SEC) {
		*error = EINVAL;
		return;
	}
	s = 1 << (cmd - WD_TO_1SEC);
	DPRINTF("seconds %u\n", s);
	/*
	 * Leave the pre-scaler with its default values:
	 * PTV = 0 == 2**0 == 1
	 * PRE = 1 (enabled)
	 *
	 * Compute the load register value assuming a 32kHz clock.
	 * See OVF_Rate in the WDT section of the AM335x TRM.
	 */
	ptv = 0;
	wldr = 0xffffffff - (s * (32768 / (1 << ptv))) + 1;
	DPRINTF("wldr 0x%x\n", wldr);
	ti_wdt_reg_write(sc, TI_WDT_WLDR, wldr);
	/*
	 * Trigger a timer reload.
	 */
	ti_wdt_reg_write(sc, TI_WDT_WTGR,
	    ti_wdt_reg_read(sc, TI_WDT_WTGR) + 1);
	ti_wdt_reg_wait(sc, TI_W_PEND_WTGR);
	ti_wdt_enable(sc);
	*error = 0;
}
