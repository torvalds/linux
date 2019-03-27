/*-
 * Copyright (c) 2012 Semihalf.
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

#include "opt_platform.h"
#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/proc.h>
#include <sys/pcpu.h>
#include <sys/rman.h>
#include <sys/sched.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/resource.h>
#include <machine/tlb.h>

#include <contrib/ncsw/inc/error_ext.h>
#include <contrib/ncsw/inc/xx_ext.h>

#include "portals.h"


int
dpaa_portal_alloc_res(device_t dev, struct dpaa_portals_devinfo *di, int cpu)
{
	struct dpaa_portals_softc *sc = device_get_softc(dev);
	struct resource_list_entry *rle;
	int err;
	struct resource_list *res;

	/* Check if MallocSmart allocator is ready */
	if (XX_MallocSmartInit() != E_OK)
		return (ENXIO);

	res = &di->di_res;

	/*
	 * Allocate memory.
	 * Reserve only one pair of CE/CI virtual memory regions
	 * for all CPUs, in order to save the space.
	 */
	if (sc->sc_rres[0] == NULL) {
		/* Cache enabled area */
		rle = resource_list_find(res, SYS_RES_MEMORY, 0);
		sc->sc_rrid[0] = 0;
		sc->sc_rres[0] = bus_alloc_resource(dev,
		    SYS_RES_MEMORY, &sc->sc_rrid[0], rle->start + sc->sc_dp_pa,
		    rle->end + sc->sc_dp_pa, rle->count, RF_ACTIVE);
		if (sc->sc_rres[0] == NULL) {
			device_printf(dev,
			    "Could not allocate cache enabled memory.\n");
			return (ENXIO);
		}
		tlb1_set_entry(rman_get_bushandle(sc->sc_rres[0]),
		    rle->start + sc->sc_dp_pa, rle->count, _TLB_ENTRY_MEM);
		/* Cache inhibited area */
		rle = resource_list_find(res, SYS_RES_MEMORY, 1);
		sc->sc_rrid[1] = 1;
		sc->sc_rres[1] = bus_alloc_resource(dev,
		    SYS_RES_MEMORY, &sc->sc_rrid[1], rle->start + sc->sc_dp_pa,
		    rle->end + sc->sc_dp_pa, rle->count, RF_ACTIVE);
		if (sc->sc_rres[1] == NULL) {
			device_printf(dev,
			    "Could not allocate cache inhibited memory.\n");
			bus_release_resource(dev, SYS_RES_MEMORY,
			    sc->sc_rrid[0], sc->sc_rres[0]);
			return (ENXIO);
		}
		tlb1_set_entry(rman_get_bushandle(sc->sc_rres[1]),
		    rle->start + sc->sc_dp_pa, rle->count, _TLB_ENTRY_IO);
		sc->sc_dp[cpu].dp_regs_mapped = 1;
	}
	/* Acquire portal's CE_PA and CI_PA */
	rle = resource_list_find(res, SYS_RES_MEMORY, 0);
	sc->sc_dp[cpu].dp_ce_pa = rle->start + sc->sc_dp_pa;
	sc->sc_dp[cpu].dp_ce_size = rle->count;
	rle = resource_list_find(res, SYS_RES_MEMORY, 1);
	sc->sc_dp[cpu].dp_ci_pa = rle->start + sc->sc_dp_pa;
	sc->sc_dp[cpu].dp_ci_size = rle->count;

	/* Allocate interrupts */
	rle = resource_list_find(res, SYS_RES_IRQ, 0);
	sc->sc_dp[cpu].dp_irid = 0;
	sc->sc_dp[cpu].dp_ires = bus_alloc_resource(dev,
	    SYS_RES_IRQ, &sc->sc_dp[cpu].dp_irid, rle->start, rle->end,
	    rle->count, RF_ACTIVE);
	/* Save interrupt number for later use */
	sc->sc_dp[cpu].dp_intr_num = rle->start;

	if (sc->sc_dp[cpu].dp_ires == NULL) {
		device_printf(dev, "Could not allocate irq.\n");
		return (ENXIO);
	}

	err = XX_PreallocAndBindIntr((uintptr_t)sc->sc_dp[cpu].dp_ires, cpu);

	if (err != E_OK) {
		device_printf(dev, "Could not prealloc and bind interrupt\n");
		bus_release_resource(dev, SYS_RES_IRQ,
		    sc->sc_dp[cpu].dp_irid, sc->sc_dp[cpu].dp_ires);
		sc->sc_dp[cpu].dp_ires = NULL;
		return (ENXIO);
	}

#if 0
	err = bus_generic_config_intr(dev, rle->start, di->di_intr_trig,
	    di->di_intr_pol);
	if (err != 0) {
		device_printf(dev, "Could not configure interrupt\n");
		bus_release_resource(dev, SYS_RES_IRQ,
		    sc->sc_dp[cpu].dp_irid, sc->sc_dp[cpu].dp_ires);
		sc->sc_dp[cpu].dp_ires = NULL;
		return (err);
	}
#endif

	return (0);
}

void
dpaa_portal_map_registers(struct dpaa_portals_softc *sc)
{
	unsigned int cpu;

	sched_pin();
	cpu = PCPU_GET(cpuid);
	if (sc->sc_dp[cpu].dp_regs_mapped)
		goto out;

	tlb1_set_entry(rman_get_bushandle(sc->sc_rres[0]),
	    sc->sc_dp[cpu].dp_ce_pa, sc->sc_dp[cpu].dp_ce_size,
	    _TLB_ENTRY_MEM);
	tlb1_set_entry(rman_get_bushandle(sc->sc_rres[1]),
	    sc->sc_dp[cpu].dp_ci_pa, sc->sc_dp[cpu].dp_ci_size,
	    _TLB_ENTRY_IO);

	sc->sc_dp[cpu].dp_regs_mapped = 1;

out:
	sched_unpin();
}
