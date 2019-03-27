/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Oleksandr Tymoshenko <gonzo@freebsd.org>
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
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/interrupt.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/malloc.h>
#include <sys/smp.h>
#include <sys/pmc.h>
#include <sys/pmckern.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>

#include <contrib/octeon-sdk/cvmx.h>
#include <mips/cavium/octeon_irq.h>

struct octeon_pmc_softc {
	struct rman irq_rman;
	struct resource *octeon_pmc_irq;
};

static void		octeon_pmc_identify(driver_t *, device_t);
static int		octeon_pmc_probe(device_t);
static int		octeon_pmc_attach(device_t);
static int		octeon_pmc_intr(void *);

static void
octeon_pmc_identify(driver_t *drv, device_t parent)
{
	if (octeon_has_feature(OCTEON_FEATURE_USB))
		BUS_ADD_CHILD(parent, 0, "pmc", 0);
}

static int
octeon_pmc_probe(device_t dev)
{
	if (device_get_unit(dev) != 0)
		return (ENXIO);

	device_set_desc(dev, "Cavium Octeon Performance Counters");
	return (BUS_PROBE_NOWILDCARD);
}

static int
octeon_pmc_attach(device_t dev)
{
	struct octeon_pmc_softc *sc;
	int error;
	int rid;

	sc = device_get_softc(dev);

	rid = 0;
	sc->octeon_pmc_irq = bus_alloc_resource(dev, 
	    SYS_RES_IRQ, &rid, OCTEON_PMC_IRQ,
	    OCTEON_PMC_IRQ, 1, RF_ACTIVE);

	if (sc->octeon_pmc_irq == NULL) {
		device_printf(dev, "could not allocate irq%d\n", OCTEON_PMC_IRQ);
		return (ENXIO);
	}

	error = bus_setup_intr(dev, sc->octeon_pmc_irq, 
	    INTR_TYPE_MISC, octeon_pmc_intr, NULL, sc, NULL);
	if (error != 0) {
		device_printf(dev, "bus_setup_intr failed: %d\n", error);
		return (error);
	}

	return (0);
}

static int
octeon_pmc_intr(void *arg)
{
	struct trapframe *tf = PCPU_GET(curthread)->td_intr_frame;

	if (pmc_intr)
		(*pmc_intr)(PCPU_GET(tf);

	return (FILTER_HANDLED);
}

static device_method_t octeon_pmc_methods[] = {
	DEVMETHOD(device_identify,	octeon_pmc_identify),
	DEVMETHOD(device_probe,		octeon_pmc_probe),
	DEVMETHOD(device_attach,	octeon_pmc_attach),
	{ 0, 0 }
};

static driver_t octeon_pmc_driver = {
	"pmc",
	octeon_pmc_methods,
	sizeof(struct octeon_pmc_softc),
};
static devclass_t octeon_pmc_devclass;
DRIVER_MODULE(octeon_pmc, nexus, octeon_pmc_driver, octeon_pmc_devclass, 0, 0);
