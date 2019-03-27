/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012, 2013 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Oleksandr Rybalko under sponsorship
 * from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1.	Redistributions of source code must retain the above copyright
 *	notice, this list of conditions and the following disclaimer.
 * 2.	Redistributions in binary form must reproduce the above copyright
 *	notice, this list of conditions and the following disclaimer in the
 *	documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/time.h>
#include <sys/bus.h>
#include <sys/resource.h>
#include <sys/rman.h>
#include <sys/watchdog.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/freescale/imx/imx_machdep.h>
#include <arm/freescale/imx/imx_wdogreg.h>

struct imx_wdog_softc {
	struct mtx		sc_mtx;
	device_t		sc_dev;
	struct resource		*sc_res[2];
	void 			*sc_ih;
	uint32_t		sc_timeout;
	bool			sc_pde_enabled;
};

static struct resource_spec imx_wdog_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	RESOURCE_SPEC_END
};

#define	MEMRES	0
#define	IRQRES	1

static struct ofw_compat_data compat_data[] = {
	{"fsl,imx6sx-wdt", 1},
	{"fsl,imx6sl-wdt", 1},
	{"fsl,imx6q-wdt",  1},
	{"fsl,imx53-wdt",  1},
	{"fsl,imx51-wdt",  1},
	{"fsl,imx50-wdt",  1},
	{"fsl,imx35-wdt",  1},
	{"fsl,imx27-wdt",  1},
	{"fsl,imx25-wdt",  1},
	{"fsl,imx21-wdt",  1},
	{NULL,             0}
};

static inline uint16_t
RD2(struct imx_wdog_softc *sc, bus_size_t offs)
{

	return (bus_read_2(sc->sc_res[MEMRES], offs));
}

static inline void
WR2(struct imx_wdog_softc *sc, bus_size_t offs, uint16_t val)
{

	bus_write_2(sc->sc_res[MEMRES], offs, val);
}

static int
imx_wdog_enable(struct imx_wdog_softc *sc, u_int timeout)
{
	uint16_t reg;

	if (timeout < 1 || timeout > 128)
		return (EINVAL);

	mtx_lock(&sc->sc_mtx);
	if (timeout != sc->sc_timeout) {
		sc->sc_timeout = timeout;
		reg = RD2(sc, WDOG_CR_REG);
		reg &= ~WDOG_CR_WT_MASK;
		reg |= ((2 * timeout - 1) << WDOG_CR_WT_SHIFT);
		WR2(sc, WDOG_CR_REG, reg | WDOG_CR_WDE);
	}
	/* Refresh counter */
	WR2(sc, WDOG_SR_REG, WDOG_SR_STEP1);
	WR2(sc, WDOG_SR_REG, WDOG_SR_STEP2);
	/* Watchdog active, can disable rom-boot watchdog. */
	if (sc->sc_pde_enabled) {
		sc->sc_pde_enabled = false;
		reg = RD2(sc, WDOG_MCR_REG);
		WR2(sc, WDOG_MCR_REG, reg & ~WDOG_MCR_PDE);
	}
	mtx_unlock(&sc->sc_mtx);

	return (0);
}

static void
imx_watchdog(void *arg, u_int cmd, int *error)
{
	struct imx_wdog_softc *sc;
	u_int timeout;

	sc = arg;
	if (cmd == 0) {
		if (bootverbose)
			device_printf(sc->sc_dev, "Can not be disabled.\n");
		*error = EOPNOTSUPP;
	} else {
		timeout = (u_int)((1ULL << (cmd & WD_INTERVAL)) / 1000000000U);
		if (imx_wdog_enable(sc, timeout) == 0)
			*error = 0;
	}
}

static int
imx_wdog_intr(void *arg)
{
	struct imx_wdog_softc *sc = arg;

	/*
	 * When configured for external reset, the actual reset is supposed to
	 * happen when some external device responds to the assertion of the
	 * WDOG_B signal by asserting the POR signal to the chip.  This
	 * interrupt handler is a backstop mechanism; it is set up to fire
	 * simultaneously with WDOG_B, and if the external reset happens we'll
	 * never actually make it to here.  If we do make it here, just trigger
	 * a software reset.  That code will see that external reset is
	 * configured, and it will wait for 1 second for it to take effect, then
	 * it will do a software reset as a fallback.
	 */
	imx_wdog_cpu_reset(BUS_SPACE_PHYSADDR(sc->sc_res[MEMRES], WDOG_CR_REG));

	return (FILTER_HANDLED); /* unreached */
}

static int
imx_wdog_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Freescale i.MX Watchdog");
	return (0);
}

static int
imx_wdog_attach(device_t dev)
{
	struct imx_wdog_softc *sc;
	pcell_t timeout;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	if (bus_alloc_resources(dev, imx_wdog_spec, sc->sc_res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	mtx_init(&sc->sc_mtx, device_get_nameunit(dev), "imx_wdt", MTX_DEF);

	/*
	 * If we're configured to assert an external reset signal, set up the
	 * hardware to do so, and install an interrupt handler whose only
	 * purpose is to backstop the external reset.  Don't worry if the
	 * interrupt setup fails, since it's only a backstop measure.
	 */
	if (ofw_bus_has_prop(sc->sc_dev, "fsl,ext-reset-output")) {
		WR2(sc, WDOG_CR_REG, WDOG_CR_WDT | RD2(sc, WDOG_CR_REG));
		bus_setup_intr(sc->sc_dev, sc->sc_res[IRQRES],
		    INTR_TYPE_MISC | INTR_MPSAFE, imx_wdog_intr, NULL, sc,
		    &sc->sc_ih);
		WR2(sc, WDOG_ICR_REG, WDOG_ICR_WIE); /* Enable, count is 0. */
	}

	/*
	 * Note whether the rom-boot so-called "power-down" watchdog is active,
	 * so we can disable it when the regular watchdog is first enabled.
	 */
	if (RD2(sc, WDOG_MCR_REG) & WDOG_MCR_PDE)
		sc->sc_pde_enabled = true;

	EVENTHANDLER_REGISTER(watchdog_list, imx_watchdog, sc, 0);

	/* If there is a timeout-sec property, activate the watchdog. */
	if (OF_getencprop(ofw_bus_get_node(sc->sc_dev), "timeout-sec",
	    &timeout, sizeof(timeout)) == sizeof(timeout)) {
		if (timeout < 1 || timeout > 128) {
			device_printf(sc->sc_dev, "ERROR: bad timeout-sec "
			    "property value %u, using 128\n", timeout);
			timeout = 128;
		}
		imx_wdog_enable(sc, timeout);
		device_printf(sc->sc_dev, "watchdog enabled using "
		    "timeout-sec property value %u\n", timeout);
	}

	/*
	 * The watchdog hardware cannot be disabled, so there's little point in
	 * coding up a detach() routine to carefully tear everything down, just
	 * make the device busy so that detach can't happen.
	 */
	device_busy(sc->sc_dev);
	return (0);
}

static device_method_t imx_wdog_methods[] = {
	DEVMETHOD(device_probe,		imx_wdog_probe),
	DEVMETHOD(device_attach,	imx_wdog_attach),
	DEVMETHOD_END
};

static driver_t imx_wdog_driver = {
	"imx_wdog",
	imx_wdog_methods,
	sizeof(struct imx_wdog_softc),
};

static devclass_t imx_wdog_devclass;

EARLY_DRIVER_MODULE(imx_wdog, simplebus, imx_wdog_driver,
    imx_wdog_devclass, 0, 0, BUS_PASS_TIMER);
SIMPLEBUS_PNP_INFO(compat_data);
