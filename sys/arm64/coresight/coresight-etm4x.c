/*-
 * Copyright (c) 2018 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * This software was developed by BAE Systems, the University of Cambridge
 * Computer Laboratory, and Memorial University under DARPA/AFRL contract
 * FA8650-15-C-7558 ("CADETS"), as part of the DARPA Transparent Computing
 * (TC) research program.
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

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm64/coresight/coresight.h>
#include <arm64/coresight/coresight-etm4x.h>

#include "coresight_if.h"

#define	ETM_DEBUG
#undef ETM_DEBUG
   
#ifdef ETM_DEBUG
#define	dprintf(fmt, ...)	printf(fmt, ##__VA_ARGS__)
#else
#define	dprintf(fmt, ...)
#endif

/*
 * Typical trace flow:
 *
 * CPU0 -> ETM0 -> funnel1 -> funnel0 -> ETF -> replicator -> ETR -> DRAM
 * CPU1 -> ETM1 -> funnel1 -^
 * CPU2 -> ETM2 -> funnel1 -^
 * CPU3 -> ETM3 -> funnel1 -^
 */

static struct ofw_compat_data compat_data[] = {
	{ "arm,coresight-etm4x",		1 },
	{ NULL,					0 }
};

struct etm_softc {
	struct resource			*res;
	struct coresight_platform_data	*pdata;
};

static struct resource_spec etm_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ -1, 0 }
};

static int
etm_prepare(device_t dev, struct coresight_event *event)
{
	struct etm_softc *sc;
	uint32_t reg;
	int i;

	sc = device_get_softc(dev);

	/* Configure ETM */

	/*
	 * Enable the return stack, global timestamping,
	 * Context ID, and Virtual context identifier tracing.
	 */
	reg = TRCCONFIGR_RS | TRCCONFIGR_TS;
	reg |= TRCCONFIGR_CID | TRCCONFIGR_VMID;
	reg |= TRCCONFIGR_INSTP0_LDRSTR;
	reg |= TRCCONFIGR_COND_ALL;
	bus_write_4(sc->res, TRCCONFIGR, reg);

	/* Disable all event tracing. */
	bus_write_4(sc->res, TRCEVENTCTL0R, 0);
	bus_write_4(sc->res, TRCEVENTCTL1R, 0);

	/* Disable stalling, if implemented. */
	bus_write_4(sc->res, TRCSTALLCTLR, 0);

	/* Enable trace synchronization every 4096 bytes of trace. */
	bus_write_4(sc->res, TRCSYNCPR, TRCSYNCPR_4K);

	/* Set a value for the trace ID */
	bus_write_4(sc->res, TRCTRACEIDR, event->etm.trace_id);

	/*
	 * Disable the timestamp event. The trace unit still generates
	 * timestamps due to other reasons such as trace synchronization.
	 */
	bus_write_4(sc->res, TRCTSCTLR, 0);

	/*
	 * Enable ViewInst to trace everything, with the start/stop
	 * logic started.
	 */
	reg = TRCVICTLR_SSSTATUS;

	/* The number of the single resource used to activate the event. */
	reg |= (1 << EVENT_SEL_S);

	if (event->excp_level > 2)
		return (-1);

	reg |= TRCVICTLR_EXLEVEL_NS_M;
	reg &= ~TRCVICTLR_EXLEVEL_NS(event->excp_level);
	reg |= TRCVICTLR_EXLEVEL_S_M;
	reg &= ~TRCVICTLR_EXLEVEL_S(event->excp_level);
	bus_write_4(sc->res, TRCVICTLR, reg);

	for (i = 0; i < event->naddr * 2; i++) {
		dprintf("configure range %d, address %lx\n",
		    i, event->addr[i]);
		bus_write_8(sc->res, TRCACVR(i), event->addr[i]);

		reg = 0;
		/* Secure state */
		reg |= TRCACATR_EXLEVEL_S_M;
		reg &= ~TRCACATR_EXLEVEL_S(event->excp_level);
		/* Non-secure state */
		reg |= TRCACATR_EXLEVEL_NS_M;
		reg &= ~TRCACATR_EXLEVEL_NS(event->excp_level);
		bus_write_4(sc->res, TRCACATR(i), reg);

		/* Address range is included */
		reg = bus_read_4(sc->res, TRCVIIECTLR);
		reg |= (1 << (TRCVIIECTLR_INCLUDE_S + i / 2));
		bus_write_4(sc->res, TRCVIIECTLR, reg);
	}

	/* No address filtering for ViewData. */
	bus_write_4(sc->res, TRCVDARCCTLR, 0);

	/* Clear the STATUS bit to zero */
	bus_write_4(sc->res, TRCSSCSR(0), 0);

	if (event->naddr == 0) {
		/* No address range filtering for ViewInst. */
		bus_write_4(sc->res, TRCVIIECTLR, 0);
	}

	/* No start or stop points for ViewInst. */
	bus_write_4(sc->res, TRCVISSCTLR, 0);

	/* Disable ViewData */
	bus_write_4(sc->res, TRCVDCTLR, 0);

	/* No address filtering for ViewData. */
	bus_write_4(sc->res, TRCVDSACCTLR, 0);

	return (0);
}

static int
etm_init(device_t dev)
{
	struct etm_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	/* Unlocking Coresight */
	bus_write_4(sc->res, CORESIGHT_LAR, CORESIGHT_UNLOCK);

	/* Unlocking ETM */
	bus_write_4(sc->res, TRCOSLAR, 0);

	reg = bus_read_4(sc->res, TRCIDR(1));
	dprintf("ETM Version: %d.%d\n",
	    (reg & TRCIDR1_TRCARCHMAJ_M) >> TRCIDR1_TRCARCHMAJ_S,
	    (reg & TRCIDR1_TRCARCHMIN_M) >> TRCIDR1_TRCARCHMIN_S);

	return (0);
}

static int
etm_enable(device_t dev, struct endpoint *endp,
    struct coresight_event *event)
{
	struct etm_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	etm_prepare(dev, event);

	/* Enable the trace unit */
	bus_write_4(sc->res, TRCPRGCTLR, TRCPRGCTLR_EN);

	/* Wait for an IDLE bit to be LOW */
	do {
		reg = bus_read_4(sc->res, TRCSTATR);
	} while ((reg & TRCSTATR_IDLE) == 1);

	if ((bus_read_4(sc->res, TRCPRGCTLR) & TRCPRGCTLR_EN) == 0)
		panic("etm is not enabled\n");

	return (0);
}

static void
etm_disable(device_t dev, struct endpoint *endp,
    struct coresight_event *event)
{
	struct etm_softc *sc;
	uint32_t reg;

	sc = device_get_softc(dev);

	/* Disable the trace unit */
	bus_write_4(sc->res, TRCPRGCTLR, 0);

	/* Wait for an IDLE bit */
	do {
		reg = bus_read_4(sc->res, TRCSTATR);
	} while ((reg & TRCSTATR_IDLE) == 0);
}

static int
etm_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	device_set_desc(dev, "AArch64 Embedded Trace Macrocell");

	return (BUS_PROBE_DEFAULT);
}

static int
etm_attach(device_t dev)
{
	struct coresight_desc desc;
	struct etm_softc *sc;

	sc = device_get_softc(dev);

	if (bus_alloc_resources(dev, etm_spec, &sc->res) != 0) {
		device_printf(dev, "cannot allocate resources for device\n");
		return (ENXIO);
	}

	sc->pdata = coresight_get_platform_data(dev);
	desc.pdata = sc->pdata;
	desc.dev = dev;
	desc.dev_type = CORESIGHT_ETMV4;
	coresight_register(&desc);

	return (0);
}

static device_method_t etm_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		etm_probe),
	DEVMETHOD(device_attach,	etm_attach),

	/* Coresight interface */
	DEVMETHOD(coresight_init,	etm_init),
	DEVMETHOD(coresight_enable,	etm_enable),
	DEVMETHOD(coresight_disable,	etm_disable),
	DEVMETHOD_END
};

static driver_t etm_driver = {
	"etm",
	etm_methods,
	sizeof(struct etm_softc),
};

static devclass_t etm_devclass;

DRIVER_MODULE(etm, simplebus, etm_driver, etm_devclass, 0, 0);
MODULE_VERSION(etm, 1);
