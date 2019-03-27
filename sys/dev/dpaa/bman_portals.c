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
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/proc.h>
#include <sys/pcpu.h>
#include <sys/sched.h>

#include <machine/bus.h>
#include <machine/tlb.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <powerpc/mpc85xx/mpc85xx.h>

#include "bman.h"
#include "portals.h"

t_Handle bman_portal_setup(struct bman_softc *);

struct dpaa_portals_softc *bp_sc;

int
bman_portals_attach(device_t dev)
{
	struct dpaa_portals_softc *sc;

	sc = bp_sc = device_get_softc(dev);
	
	/* Map bman portal to physical address space */
	if (law_enable(OCP85XX_TGTIF_BMAN, sc->sc_dp_pa, sc->sc_dp_size)) {
		bman_portals_detach(dev);
		return (ENXIO);
	}
	/* Set portal properties for XX_VirtToPhys() */
	XX_PortalSetInfo(dev);

	return (bus_generic_attach(dev));
}

int
bman_portals_detach(device_t dev)
{
	struct dpaa_portals_softc *sc;
	int i;

	bp_sc = NULL;
	sc = device_get_softc(dev);

	for (i = 0; i < ARRAY_SIZE(sc->sc_dp); i++) {
		if (sc->sc_dp[i].dp_ph != NULL) {
			thread_lock(curthread);
			sched_bind(curthread, i);
			thread_unlock(curthread);

			BM_PORTAL_Free(sc->sc_dp[i].dp_ph);

			thread_lock(curthread);
			sched_unbind(curthread);
			thread_unlock(curthread);
		}

		if (sc->sc_dp[i].dp_ires != NULL) {
			XX_DeallocIntr((uintptr_t)sc->sc_dp[i].dp_ires);
			bus_release_resource(dev, SYS_RES_IRQ,
			    sc->sc_dp[i].dp_irid, sc->sc_dp[i].dp_ires);
		}
	}
	for (i = 0; i < ARRAY_SIZE(sc->sc_rres); i++) {
		if (sc->sc_rres[i] != NULL)
			bus_release_resource(dev, SYS_RES_MEMORY,
			    sc->sc_rrid[i],
			    sc->sc_rres[i]);
	}

	return (0);
}

t_Handle
bman_portal_setup(struct bman_softc *bsc)
{
	struct dpaa_portals_softc *sc;
	t_BmPortalParam bpp;
	t_Handle portal;
	unsigned int cpu;
	uintptr_t p;

	/* Return NULL if we're not ready or while detach */
	if (bp_sc == NULL)
		return (NULL);

	sc = bp_sc;

	sched_pin();
	portal = NULL;
	cpu = PCPU_GET(cpuid);

	/* Check if portal is ready */
	while (atomic_cmpset_acq_ptr((uintptr_t *)&sc->sc_dp[cpu].dp_ph,
	    0, -1) == 0) {
		p = atomic_load_acq_ptr((uintptr_t *)&sc->sc_dp[cpu].dp_ph);

		/* Return if portal is already initialized */
		if (p != 0 && p != -1) {
			sched_unpin();
			return ((t_Handle)p);
		}

		/* Not inititialized and "owned" by another thread */
		thread_lock(curthread);
		mi_switch(SW_VOL, NULL);
		thread_unlock(curthread);
	}

	/* Map portal registers */
	dpaa_portal_map_registers(sc);

	/* Configure and initialize portal */
	bpp.ceBaseAddress = rman_get_bushandle(sc->sc_rres[0]);
	bpp.ciBaseAddress = rman_get_bushandle(sc->sc_rres[1]);
	bpp.h_Bm = bsc->sc_bh;
	bpp.swPortalId = cpu;
	bpp.irq = (uintptr_t)sc->sc_dp[cpu].dp_ires;

	portal = BM_PORTAL_Config(&bpp);
	if (portal == NULL)
		goto err;

	if (BM_PORTAL_Init(portal) != E_OK)
		goto err;

	atomic_store_rel_ptr((uintptr_t *)&sc->sc_dp[cpu].dp_ph, (uintptr_t)portal);
	
	sched_unpin();

	return (portal);

err:
	if (portal != NULL)
		BM_PORTAL_Free(portal);

	atomic_store_rel_ptr((uintptr_t *)&sc->sc_dp[cpu].dp_ph, 0);
	sched_unpin();

	return (NULL);
}
