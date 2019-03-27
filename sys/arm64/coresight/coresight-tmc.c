/*-
 * Copyright (c) 2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract FA8750-10-C-0237
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>

#include <arm64/coresight/coresight.h>
#include <arm64/coresight/coresight-tmc.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include "coresight_if.h"

#define	TMC_DEBUG
#undef TMC_DEBUG
        
#ifdef TMC_DEBUG
#define	dprintf(fmt, ...)	printf(fmt, ##__VA_ARGS__)
#else
#define	dprintf(fmt, ...)
#endif

static struct ofw_compat_data compat_data[] = {
	{ "arm,coresight-tmc",			1 },
	{ NULL,					0 }
};

struct tmc_softc {
	struct resource			*res;
	device_t			dev;
	uint64_t			cycle;
	struct coresight_platform_data	*pdata;
	uint32_t			dev_type;
#define	CORESIGHT_UNKNOWN		0
#define	CORESIGHT_ETR			1
#define	CORESIGHT_ETF			2
	uint32_t			nev;
	struct coresight_event		*event;
	boolean_t			etf_configured;
};

static struct resource_spec tmc_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
tmc_start(device_t dev)
{
	struct tmc_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	if (bus_read_4(sc->res, TMC_CTL) & CTL_TRACECAPTEN)
		return (-1);

	/* Enable TMC */
	bus_write_4(sc->res, TMC_CTL, CTL_TRACECAPTEN);
	if ((bus_read_4(sc->res, TMC_CTL) & CTL_TRACECAPTEN) == 0)
		panic("Not enabled\n");

	do {
		reg = bus_read_4(sc->res, TMC_STS);
	} while ((reg & STS_TMCREADY) == 1);

	if ((bus_read_4(sc->res, TMC_CTL) & CTL_TRACECAPTEN) == 0)
		panic("Not enabled\n");

	return (0);
}

static int
tmc_stop(device_t dev)
{
	struct tmc_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	reg = bus_read_4(sc->res, TMC_CTL);
	reg &= ~CTL_TRACECAPTEN;
	bus_write_4(sc->res, TMC_CTL, reg);

	do {
		reg = bus_read_4(sc->res, TMC_STS);
	} while ((reg & STS_TMCREADY) == 1);

	return (0);
}

static int
tmc_configure_etf(device_t dev)
{
	struct tmc_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	do {
		reg = bus_read_4(sc->res, TMC_STS);
	} while ((reg & STS_TMCREADY) == 0);

	bus_write_4(sc->res, TMC_MODE, MODE_HW_FIFO);
	bus_write_4(sc->res, TMC_FFCR, FFCR_EN_FMT | FFCR_EN_TI);

	tmc_start(dev);

	dprintf("%s: STS %x, CTL %x, RSZ %x, RRP %x, RWP %x, "
	    "LBUFLEVEL %x, CBUFLEVEL %x\n", __func__,
	    bus_read_4(sc->res, TMC_STS),
	    bus_read_4(sc->res, TMC_CTL),
	    bus_read_4(sc->res, TMC_RSZ),
	    bus_read_4(sc->res, TMC_RRP),
	    bus_read_4(sc->res, TMC_RWP),
	    bus_read_4(sc->res, TMC_CBUFLEVEL),
	    bus_read_4(sc->res, TMC_LBUFLEVEL));

	return (0);
}

static int
tmc_configure_etr(device_t dev, struct endpoint *endp,
    struct coresight_event *event)
{
	struct tmc_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	tmc_stop(dev);

	do {
		reg = bus_read_4(sc->res, TMC_STS);
	} while ((reg & STS_TMCREADY) == 0);

	/* Configure TMC */
	bus_write_4(sc->res, TMC_MODE, MODE_CIRCULAR_BUFFER);

	reg = AXICTL_PROT_CTRL_BIT1;
	reg |= AXICTL_WRBURSTLEN_16;

	/*
	 * SG operation is broken on DragonBoard 410c
	 * reg |= AXICTL_SG_MODE;
	 */

	reg |= AXICTL_AXCACHE_OS;
	bus_write_4(sc->res, TMC_AXICTL, reg);

	reg = FFCR_EN_FMT | FFCR_EN_TI | FFCR_FON_FLIN |
	    FFCR_FON_TRIG_EVT | FFCR_TRIGON_TRIGIN;
	bus_write_4(sc->res, TMC_FFCR, reg);

	bus_write_4(sc->res, TMC_TRG, 8);

	bus_write_4(sc->res, TMC_DBALO, event->etr.low);
	bus_write_4(sc->res, TMC_DBAHI, event->etr.high);
	bus_write_4(sc->res, TMC_RSZ, event->etr.bufsize / 4);

	bus_write_4(sc->res, TMC_RRP, event->etr.low);
	bus_write_4(sc->res, TMC_RWP, event->etr.low);

	reg = bus_read_4(sc->res, TMC_STS);
	reg &= ~STS_FULL;
	bus_write_4(sc->res, TMC_STS, reg);

	tmc_start(dev);

	return (0);
}

static int
tmc_init(device_t dev)
{
	struct tmc_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	/* Unlock Coresight */
	bus_write_4(sc->res, CORESIGHT_LAR, CORESIGHT_UNLOCK);

	/* Unlock TMC */
	bus_write_4(sc->res, TMC_LAR, CORESIGHT_UNLOCK);

	reg = bus_read_4(sc->res, TMC_DEVID);
	reg &= DEVID_CONFIGTYPE_M;
	switch (reg) {
	case DEVID_CONFIGTYPE_ETR:
		sc->dev_type = CORESIGHT_ETR;
		dprintf(dev, "ETR configuration found\n");
		break;
	case DEVID_CONFIGTYPE_ETF:
		sc->dev_type = CORESIGHT_ETF;
		dprintf(dev, "ETF configuration found\n");
		if (sc->etf_configured == false) {
			tmc_configure_etf(dev);
			sc->etf_configured = true;
		}
		break;
	default:
		sc->dev_type = CORESIGHT_UNKNOWN;
		break;
	}

	return (0);
}

static int
tmc_enable(device_t dev, struct endpoint *endp,
    struct coresight_event *event)
{
	struct tmc_softc *sc;
	uint32_t nev;

	sc = device_get_softc(dev);

	if (sc->dev_type == CORESIGHT_ETF)
		return (0);

	KASSERT(sc->dev_type == CORESIGHT_ETR,
	    ("Wrong dev_type"));

	/*
	 * Multiple CPUs can call this same time.
	 * We allow only one running configuration.
	 */

	if (event->etr.flags & ETR_FLAG_ALLOCATE) {
		event->etr.flags &= ~ETR_FLAG_ALLOCATE;
		nev = atomic_fetchadd_int(&sc->nev, 1);
		if (nev == 0) {
			sc->event = event;
			tmc_stop(dev);
			tmc_configure_etr(dev, endp, event);
			tmc_start(dev);
		}
	}

	return (0);
}

static void
tmc_disable(device_t dev, struct endpoint *endp,
    struct coresight_event *event)
{
	struct tmc_softc *sc;
	uint32_t nev;

	sc = device_get_softc(dev);

	/* ETF configuration is static */
	if (sc->dev_type == CORESIGHT_ETF)
		return;

	KASSERT(sc->dev_type == CORESIGHT_ETR, ("Wrong dev_type"));

	if (event->etr.flags & ETR_FLAG_RELEASE) {
		event->etr.flags &= ~ETR_FLAG_RELEASE;
		nev = atomic_fetchadd_int(&sc->nev, -1);
		if (nev == 1) {
			tmc_stop(dev);
			sc->event = NULL;
		}
	}
}

static int
tmc_read(device_t dev, struct endpoint *endp,
    struct coresight_event *event)
{
	struct tmc_softc *sc;
	uint32_t cur_ptr;

	sc = device_get_softc(dev);

	if (sc->dev_type == CORESIGHT_ETF)
		return (0);

	/*
	 * Ensure the event we are reading information for
	 * is currently configured one.
	 */
	if (sc->event != event)
		return (0);

	if (bus_read_4(sc->res, TMC_STS) & STS_FULL) {
		event->etr.offset = 0;
		event->etr.cycle++;
		tmc_stop(dev);
		tmc_start(dev);
	} else {
		cur_ptr = bus_read_4(sc->res, TMC_RWP);
		event->etr.offset = (cur_ptr - event->etr.low);
	}

	return (0);
}

static int
tmc_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "Coresight Trace Memory Controller (TMC)");

	return (BUS_PROBE_DEFAULT);
}

static int
tmc_attach(device_t dev)
{
	struct coresight_desc desc;
	struct tmc_softc *sc;

	sc = device_get_softc(dev);

	sc->dev = dev;

	if (bus_alloc_resources(dev, tmc_spec, &sc->res) != 0) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	sc->pdata = coresight_get_platform_data(dev);
	desc.pdata = sc->pdata;
	desc.dev = dev;
	desc.dev_type = CORESIGHT_TMC;
	coresight_register(&desc);

	return (0);
}

static device_method_t tmc_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		tmc_probe),
	DEVMETHOD(device_attach,	tmc_attach),

	/* Coresight interface */
	DEVMETHOD(coresight_init,	tmc_init),
	DEVMETHOD(coresight_enable,	tmc_enable),
	DEVMETHOD(coresight_disable,	tmc_disable),
	DEVMETHOD(coresight_read,	tmc_read),
	DEVMETHOD_END
};

static driver_t tmc_driver = {
	"tmc",
	tmc_methods,
	sizeof(struct tmc_softc),
};

static devclass_t tmc_devclass;

DRIVER_MODULE(tmc, simplebus, tmc_driver, tmc_devclass, 0, 0);
MODULE_VERSION(tmc, 1);
