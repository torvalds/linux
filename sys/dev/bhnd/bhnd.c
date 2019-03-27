/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015-2016 Landon Fuller <landonf@FreeBSD.org>
 * Copyright (c) 2017 The FreeBSD Foundation
 * All rights reserved.
 *
 * Portions of this software were developed by Landon Fuller
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Broadcom Home Networking Division (HND) Bus Driver.
 * 
 * The Broadcom HND family of devices consists of both SoCs and host-connected
 * networking chipsets containing a common family of Broadcom IP cores,
 * including an integrated MIPS and/or ARM cores.
 * 
 * HND devices expose a nearly identical interface whether accessible over a 
 * native SoC interconnect, or when connected via a host interface such as 
 * PCIe. As a result, the majority of hardware support code should be re-usable 
 * across host drivers for HND networking chipsets, as well as FreeBSD support 
 * for Broadcom MIPS/ARM HND SoCs.
 * 
 * Earlier HND models used the siba(4) on-chip interconnect, while later models
 * use bcma(4); the programming model is almost entirely independent
 * of the actual underlying interconect.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/bhnd/cores/pmu/bhnd_pmu.h>

#include "bhnd_chipc_if.h"
#include "bhnd_nvram_if.h"

#include "bhnd.h"
#include "bhndreg.h"
#include "bhndvar.h"

#include "bhnd_private.h"

MALLOC_DEFINE(M_BHND, "bhnd", "bhnd bus data structures");

/**
 * bhnd_generic_probe_nomatch() reporting configuration.
 */
static const struct bhnd_nomatch {
	uint16_t	vendor;		/**< core designer */
	uint16_t	device;		/**< core id */
	bool		if_verbose;	/**< print when bootverbose is set. */
} bhnd_nomatch_table[] = {
	{ BHND_MFGID_ARM,	BHND_COREID_OOB_ROUTER,		true	},
	{ BHND_MFGID_ARM,	BHND_COREID_EROM,		true	},
	{ BHND_MFGID_ARM,	BHND_COREID_PL301,		true	},
	{ BHND_MFGID_ARM,	BHND_COREID_APB_BRIDGE,		true	},
	{ BHND_MFGID_ARM,	BHND_COREID_AXI_UNMAPPED,	false	},

	{ BHND_MFGID_INVALID,	BHND_COREID_INVALID,		false	}
};

static int			 bhnd_delete_children(struct bhnd_softc *sc);

/**
 * Default bhnd(4) bus driver implementation of DEVICE_ATTACH().
 *
 * This implementation calls device_probe_and_attach() for each of the device's
 * children, in bhnd probe order.
 */
int
bhnd_generic_attach(device_t dev)
{
	struct bhnd_softc	*sc;
	int			 error;

	if (device_is_attached(dev))
		return (EBUSY);

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Probe and attach all children */
	if ((error = bhnd_bus_probe_children(dev))) {
		bhnd_delete_children(sc);
		return (error);
	}

	return (0);
}

/**
 * Detach and delete all children, in reverse of their attach order.
 */
static int
bhnd_delete_children(struct bhnd_softc *sc)
{
	device_t		*devs;
	int			 ndevs;
	int			 error;

	/* Fetch children in detach order */
	error = bhnd_bus_get_children(sc->dev, &devs, &ndevs,
	    BHND_DEVICE_ORDER_DETACH);
	if (error)
		return (error);

	/* Perform detach */
	for (int i = 0; i < ndevs; i++) {
		device_t child = devs[i];

		/* Terminate on first error */
		if ((error = device_delete_child(sc->dev, child)))
			goto cleanup;
	}

cleanup:
	bhnd_bus_free_children(devs);
	return (error);
}

/**
 * Default bhnd(4) bus driver implementation of DEVICE_DETACH().
 *
 * This implementation calls device_detach() for each of the device's
 * children, in reverse bhnd probe order, terminating if any call to
 * device_detach() fails.
 */
int
bhnd_generic_detach(device_t dev)
{
	struct bhnd_softc	*sc;
	int			 error;

	if (!device_is_attached(dev))
		return (EBUSY);

	sc = device_get_softc(dev);

	if ((error = bhnd_delete_children(sc)))
		return (error);

	return (0);
}

/**
 * Default bhnd(4) bus driver implementation of DEVICE_SHUTDOWN().
 * 
 * This implementation calls device_shutdown() for each of the device's
 * children, in reverse bhnd probe order, terminating if any call to
 * device_shutdown() fails.
 */
int
bhnd_generic_shutdown(device_t dev)
{
	device_t	*devs;
	int		 ndevs;
	int		 error;

	if (!device_is_attached(dev))
		return (EBUSY);

	/* Fetch children in detach order */
	error = bhnd_bus_get_children(dev, &devs, &ndevs,
	    BHND_DEVICE_ORDER_DETACH);
	if (error)
		return (error);

	/* Perform shutdown */
	for (int i = 0; i < ndevs; i++) {
		device_t child = devs[i];

		/* Terminate on first error */
		if ((error = device_shutdown(child)))
			goto cleanup;
	}

cleanup:
	bhnd_bus_free_children(devs);
	return (error);
}

/**
 * Default bhnd(4) bus driver implementation of DEVICE_RESUME().
 *
 * This implementation calls BUS_RESUME_CHILD() for each of the device's
 * children in bhnd probe order, terminating if any call to BUS_RESUME_CHILD()
 * fails.
 */
int
bhnd_generic_resume(device_t dev)
{
	device_t	*devs;
	int		 ndevs;
	int		 error;

	if (!device_is_attached(dev))
		return (EBUSY);

	/* Fetch children in attach order */
	error = bhnd_bus_get_children(dev, &devs, &ndevs,
	    BHND_DEVICE_ORDER_ATTACH);
	if (error)
		return (error);

	/* Perform resume */
	for (int i = 0; i < ndevs; i++) {
		device_t child = devs[i];

		/* Terminate on first error */
		if ((error = BUS_RESUME_CHILD(device_get_parent(child), child)))
			goto cleanup;
	}

cleanup:
	bhnd_bus_free_children(devs);
	return (error);
}

/**
 * Default bhnd(4) bus driver implementation of DEVICE_SUSPEND().
 *
 * This implementation calls BUS_SUSPEND_CHILD() for each of the device's
 * children in reverse bhnd probe order. If any call to BUS_SUSPEND_CHILD()
 * fails, the suspend operation is terminated and any devices that were
 * suspended are resumed immediately by calling their BUS_RESUME_CHILD()
 * methods.
 */
int
bhnd_generic_suspend(device_t dev)
{
	device_t	*devs;
	int		 ndevs;
	int		 error;

	if (!device_is_attached(dev))
		return (EBUSY);

	/* Fetch children in detach order */
	error = bhnd_bus_get_children(dev, &devs, &ndevs,
	    BHND_DEVICE_ORDER_DETACH);
	if (error)
		return (error);

	/* Perform suspend */
	for (int i = 0; i < ndevs; i++) {
		device_t child = devs[i];
		error = BUS_SUSPEND_CHILD(device_get_parent(child), child);

		/* On error, resume suspended devices and then terminate */
		if (error) {
			for (int j = 0; j < i; j++) {
				BUS_RESUME_CHILD(device_get_parent(devs[j]),
				    devs[j]);
			}

			goto cleanup;
		}
	}

cleanup:
	bhnd_bus_free_children(devs);
	return (error);
}

/**
 * Default bhnd(4) bus driver implementation of BHND_BUS_GET_PROBE_ORDER().
 *
 * This implementation determines probe ordering based on the device's class
 * and other properties, including whether the device is serving as a host
 * bridge.
 */
int
bhnd_generic_get_probe_order(device_t dev, device_t child)
{
	switch (bhnd_get_class(child)) {
	case BHND_DEVCLASS_CC:
		/* Must be early enough to provide NVRAM access to the
		 * host bridge */
		return (BHND_PROBE_ROOT + BHND_PROBE_ORDER_FIRST);

	case BHND_DEVCLASS_CC_B:
		/* fall through */
	case BHND_DEVCLASS_PMU:
		return (BHND_PROBE_BUS + BHND_PROBE_ORDER_EARLY);

	case BHND_DEVCLASS_SOC_ROUTER:
		return (BHND_PROBE_BUS + BHND_PROBE_ORDER_LATE);

	case BHND_DEVCLASS_SOC_BRIDGE:
		return (BHND_PROBE_BUS + BHND_PROBE_ORDER_LAST);
		
	case BHND_DEVCLASS_CPU:
		return (BHND_PROBE_CPU + BHND_PROBE_ORDER_FIRST);

	case BHND_DEVCLASS_RAM:
		/* fall through */
	case BHND_DEVCLASS_MEMC:
		return (BHND_PROBE_CPU + BHND_PROBE_ORDER_EARLY);
		
	case BHND_DEVCLASS_NVRAM:
		return (BHND_PROBE_RESOURCE + BHND_PROBE_ORDER_EARLY);

	case BHND_DEVCLASS_PCI:
	case BHND_DEVCLASS_PCIE:
	case BHND_DEVCLASS_PCCARD:
	case BHND_DEVCLASS_ENET:
	case BHND_DEVCLASS_ENET_MAC:
	case BHND_DEVCLASS_ENET_PHY:
	case BHND_DEVCLASS_WLAN:
	case BHND_DEVCLASS_WLAN_MAC:
	case BHND_DEVCLASS_WLAN_PHY:
	case BHND_DEVCLASS_EROM:
	case BHND_DEVCLASS_OTHER:
	case BHND_DEVCLASS_INVALID:
		if (bhnd_bus_find_hostb_device(dev) == child)
			return (BHND_PROBE_ROOT + BHND_PROBE_ORDER_EARLY);

		return (BHND_PROBE_DEFAULT);
	default:
		return (BHND_PROBE_DEFAULT);
	}
}

/**
 * Default bhnd(4) bus driver implementation of BHND_BUS_ALLOC_PMU().
 */
int
bhnd_generic_alloc_pmu(device_t dev, device_t child)
{
	struct bhnd_softc		*sc;
	struct bhnd_resource		*r;
	struct bhnd_core_clkctl		*clkctl;
	struct resource_list		*rl;
	struct resource_list_entry	*rle;
	device_t			 pmu_dev;
	bhnd_addr_t			 r_addr;
	bhnd_size_t			 r_size;
	bus_size_t			 pmu_regs;
	u_int				 max_latency;
	int				 error;

	GIANT_REQUIRED;	/* for newbus */

	if (device_get_parent(child) != dev)
		return (EINVAL);

	sc = device_get_softc(dev);
	clkctl = bhnd_get_pmu_info(child);
	pmu_regs = BHND_CLK_CTL_ST;

	/* already allocated? */
	if (clkctl != NULL) {
		panic("duplicate PMU allocation for %s",
		    device_get_nameunit(child));
	}

	/* Determine address+size of the core's PMU register block */
	error = bhnd_get_region_addr(child, BHND_PORT_DEVICE, 0, 0, &r_addr,
	    &r_size);
	if (error) {
		device_printf(sc->dev, "error fetching register block info for "
		    "%s: %d\n", device_get_nameunit(child), error);
		return (error);
	}

	if (r_size < (pmu_regs + sizeof(uint32_t))) {
		device_printf(sc->dev, "pmu offset %#jx would overrun %s "
		    "register block\n", (uintmax_t)pmu_regs,
		    device_get_nameunit(child));
		return (ENODEV);
	}

	/* Locate actual resource containing the core's register block */
	if ((rl = BUS_GET_RESOURCE_LIST(dev, child)) == NULL) {
		device_printf(dev, "NULL resource list returned for %s\n",
		    device_get_nameunit(child));
		return (ENXIO);
	}

	if ((rle = resource_list_find(rl, SYS_RES_MEMORY, 0)) == NULL) {
		device_printf(dev, "cannot locate core register resource "
		    "for %s\n", device_get_nameunit(child));
		return (ENXIO);
	}

	if (rle->res == NULL) {
		device_printf(dev, "core register resource unallocated for "
		    "%s\n", device_get_nameunit(child));
		return (ENXIO);
	}

	if (r_addr+pmu_regs < rman_get_start(rle->res) ||
	    r_addr+pmu_regs >= rman_get_end(rle->res))
	{
		device_printf(dev, "core register resource does not map PMU "
		    "registers at %#jx\n for %s\n", r_addr+pmu_regs,
		    device_get_nameunit(child));
		return (ENXIO);
	}

	/* Adjust PMU register offset relative to the actual start address
	 * of the core's register block allocation.
	 * 
	 * XXX: The saved offset will be invalid if bus_adjust_resource is
	 * used to modify the resource's start address.
	 */
	if (rman_get_start(rle->res) > r_addr)
		pmu_regs -= rman_get_start(rle->res) - r_addr;
	else
		pmu_regs -= r_addr - rman_get_start(rle->res);

	/* Retain a PMU reference for the clkctl instance state */
	pmu_dev = bhnd_retain_provider(child, BHND_SERVICE_PMU);
	if (pmu_dev == NULL) {
		device_printf(sc->dev, "PMU not found\n");
		return (ENXIO);
	}

	/* Fetch the maximum transition latency from our PMU */
	max_latency = bhnd_pmu_get_max_transition_latency(pmu_dev);

	/* Allocate a new bhnd_resource wrapping the standard resource we
	 * fetched from the resource list; we'll free this in
	 * bhnd_generic_release_pmu() */
	r = malloc(sizeof(struct bhnd_resource), M_BHND, M_NOWAIT);
	if (r == NULL) {
		bhnd_release_provider(child, pmu_dev, BHND_SERVICE_PMU);
		return (ENOMEM);
	}

	r->res = rle->res;
	r->direct = ((rman_get_flags(rle->res) & RF_ACTIVE) != 0);

	/* Allocate the clkctl instance */
	clkctl = bhnd_alloc_core_clkctl(child, pmu_dev, r, pmu_regs,
	    max_latency);
	if (clkctl == NULL) {
		free(r, M_BHND);
		bhnd_release_provider(child, pmu_dev, BHND_SERVICE_PMU);
		return (ENOMEM);
	}

	bhnd_set_pmu_info(child, clkctl);
	return (0);
}

/**
 * Default bhnd(4) bus driver implementation of BHND_BUS_RELEASE_PMU().
 */
int
bhnd_generic_release_pmu(device_t dev, device_t child)
{
	struct bhnd_softc	*sc;
	struct bhnd_core_clkctl	*clkctl;
	struct bhnd_resource	*r;
	device_t		 pmu_dev;

	GIANT_REQUIRED;	/* for newbus */
	
	sc = device_get_softc(dev);

	if (device_get_parent(child) != dev)
		return (EINVAL);

	clkctl = bhnd_get_pmu_info(child);
	if (clkctl == NULL)
		panic("pmu over-release for %s", device_get_nameunit(child));

	/* Clear all FORCE, AREQ, and ERSRC flags, unless we're already in
	 * RESET. Suspending a core clears clkctl automatically (and attempting
	 * to access the PMU registers in a suspended core will trigger a
	 * system livelock). */
	if (!bhnd_is_hw_suspended(clkctl->cc_dev)) {
		BHND_CLKCTL_LOCK(clkctl);

		/* Clear all FORCE, AREQ, and ERSRC flags */
		BHND_CLKCTL_SET_4(clkctl, 0x0, BHND_CCS_FORCE_MASK |
		    BHND_CCS_AREQ_MASK | BHND_CCS_ERSRC_REQ_MASK);

		BHND_CLKCTL_UNLOCK(clkctl);
	}

	/* Clear child's PMU info reference */
	bhnd_set_pmu_info(child, NULL);

	/* Before freeing the clkctl instance, save a pointer to resources we
	 * need to clean up manually */
	r = clkctl->cc_res;
	pmu_dev = clkctl->cc_pmu_dev;

	/* Free the clkctl instance */
	bhnd_free_core_clkctl(clkctl);

	/* Free the child's bhnd resource wrapper */
	free(r, M_BHND);

	/* Release the child's PMU provider reference */
	bhnd_release_provider(child, pmu_dev, BHND_SERVICE_PMU);

	return (0);
}

/**
 * Default bhnd(4) bus driver implementation of BHND_BUS_GET_CLOCK_LATENCY().
 */
int
bhnd_generic_get_clock_latency(device_t dev, device_t child, bhnd_clock clock,
    u_int *latency)
{
	struct bhnd_core_clkctl *clkctl;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	if ((clkctl = bhnd_get_pmu_info(child)) == NULL)
		panic("no active PMU allocation");

	return (bhnd_pmu_get_clock_latency(clkctl->cc_pmu_dev, clock, latency));
}

/**
 * Default bhnd(4) bus driver implementation of BHND_BUS_GET_CLOCK_FREQ().
 */
int
bhnd_generic_get_clock_freq(device_t dev, device_t child, bhnd_clock clock,
    u_int *freq)
{
	struct bhnd_core_clkctl *clkctl;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	if ((clkctl = bhnd_get_pmu_info(child)) == NULL)
		panic("no active PMU allocation");

	return (bhnd_pmu_get_clock_freq(clkctl->cc_pmu_dev, clock, freq));
}

/**
 * Default bhnd(4) bus driver implementation of BHND_BUS_REQUEST_CLOCK().
 */
int
bhnd_generic_request_clock(device_t dev, device_t child, bhnd_clock clock)
{
	struct bhnd_softc	*sc;
	struct bhnd_core_clkctl	*clkctl;
	uint32_t		 avail;
	uint32_t		 req;
	int			 error;

	sc = device_get_softc(dev);

	if (device_get_parent(child) != dev)
		return (EINVAL);

	if ((clkctl = bhnd_get_pmu_info(child)) == NULL)
		panic("no active PMU allocation");

	BHND_ASSERT_CLKCTL_AVAIL(clkctl);

	avail = 0x0;
	req = 0x0;

	switch (clock) {
	case BHND_CLOCK_DYN:
		break;
	case BHND_CLOCK_ILP:
		req |= BHND_CCS_FORCEILP;
		break;
	case BHND_CLOCK_ALP:
		req |= BHND_CCS_FORCEALP;
		avail |= BHND_CCS_ALPAVAIL;
		break;
	case BHND_CLOCK_HT:
		req |= BHND_CCS_FORCEHT;
		avail |= BHND_CCS_HTAVAIL;
		break;
	default:
		device_printf(dev, "%s requested unknown clock: %#x\n",
		    device_get_nameunit(clkctl->cc_dev), clock);
		return (ENODEV);
	}

	BHND_CLKCTL_LOCK(clkctl);

	/* Issue request */
	BHND_CLKCTL_SET_4(clkctl, req, BHND_CCS_FORCE_MASK);

	/* Wait for clock availability */
	error = bhnd_core_clkctl_wait(clkctl, avail, avail);

	BHND_CLKCTL_UNLOCK(clkctl);

	return (error);
}

/**
 * Default bhnd(4) bus driver implementation of BHND_BUS_ENABLE_CLOCKS().
 */
int
bhnd_generic_enable_clocks(device_t dev, device_t child, uint32_t clocks)
{
	struct bhnd_softc	*sc;
	struct bhnd_core_clkctl	*clkctl;
	uint32_t		 avail;
	uint32_t		 req;
	int			 error;

	sc = device_get_softc(dev);

	if (device_get_parent(child) != dev)
		return (EINVAL);

	if ((clkctl = bhnd_get_pmu_info(child)) == NULL)
		panic("no active PMU allocation");

	BHND_ASSERT_CLKCTL_AVAIL(clkctl);

	sc = device_get_softc(dev);

	avail = 0x0;
	req = 0x0;

	/* Build clock request flags */
	if (clocks & BHND_CLOCK_DYN)		/* nothing to enable */
		clocks &= ~BHND_CLOCK_DYN;

	if (clocks & BHND_CLOCK_ILP)		/* nothing to enable */
		clocks &= ~BHND_CLOCK_ILP;

	if (clocks & BHND_CLOCK_ALP) {
		req |= BHND_CCS_ALPAREQ;
		avail |= BHND_CCS_ALPAVAIL;
		clocks &= ~BHND_CLOCK_ALP;
	}

	if (clocks & BHND_CLOCK_HT) {
		req |= BHND_CCS_HTAREQ;
		avail |= BHND_CCS_HTAVAIL;
		clocks &= ~BHND_CLOCK_HT;
	}

	/* Check for unknown clock values */
	if (clocks != 0x0) {
		device_printf(dev, "%s requested unknown clocks: %#x\n",
		    device_get_nameunit(clkctl->cc_dev), clocks);
		return (ENODEV);
	}

	BHND_CLKCTL_LOCK(clkctl);

	/* Issue request */
	BHND_CLKCTL_SET_4(clkctl, req, BHND_CCS_AREQ_MASK);

	/* Wait for clock availability */
	error = bhnd_core_clkctl_wait(clkctl, avail, avail);

	BHND_CLKCTL_UNLOCK(clkctl);

	return (error);
}

/**
 * Default bhnd(4) bus driver implementation of BHND_BUS_REQUEST_EXT_RSRC().
 */
int
bhnd_generic_request_ext_rsrc(device_t dev, device_t child, u_int rsrc)
{
	struct bhnd_softc	*sc;
	struct bhnd_core_clkctl	*clkctl;
	uint32_t		 req;
	uint32_t		 avail;
	int			 error;

	sc = device_get_softc(dev);

	if (device_get_parent(child) != dev)
		return (EINVAL);

	if ((clkctl = bhnd_get_pmu_info(child)) == NULL)
		panic("no active PMU allocation");

	BHND_ASSERT_CLKCTL_AVAIL(clkctl);

	sc = device_get_softc(dev);

	if (rsrc > BHND_CCS_ERSRC_MAX)
		return (EINVAL);

	req = BHND_CCS_SET_BITS((1<<rsrc), BHND_CCS_ERSRC_REQ);
	avail = BHND_CCS_SET_BITS((1<<rsrc), BHND_CCS_ERSRC_STS);

	BHND_CLKCTL_LOCK(clkctl);

	/* Write request */
	BHND_CLKCTL_SET_4(clkctl, req, req);

	/* Wait for resource availability */
	error = bhnd_core_clkctl_wait(clkctl, avail, avail);

	BHND_CLKCTL_UNLOCK(clkctl);

	return (error);
}

/**
 * Default bhnd(4) bus driver implementation of BHND_BUS_RELEASE_EXT_RSRC().
 */
int
bhnd_generic_release_ext_rsrc(device_t dev, device_t child, u_int rsrc)
{
	struct bhnd_softc	*sc;
	struct bhnd_core_clkctl	*clkctl;
	uint32_t		 mask;

	sc = device_get_softc(dev);

	if (device_get_parent(child) != dev)
		return (EINVAL);

	if ((clkctl = bhnd_get_pmu_info(child)) == NULL)
		panic("no active PMU allocation");


	BHND_ASSERT_CLKCTL_AVAIL(clkctl);

	sc = device_get_softc(dev);

	if (rsrc > BHND_CCS_ERSRC_MAX)
		return (EINVAL);

	mask = BHND_CCS_SET_BITS((1<<rsrc), BHND_CCS_ERSRC_REQ);

	/* Clear request */
	BHND_CLKCTL_LOCK(clkctl);
	BHND_CLKCTL_SET_4(clkctl, 0x0, mask);
	BHND_CLKCTL_UNLOCK(clkctl);

	return (0);
}

/**
 * Default bhnd(4) bus driver implementation of BHND_BUS_IS_REGION_VALID().
 * 
 * This implementation assumes that port and region numbers are 0-indexed and
 * are allocated non-sparsely, using BHND_BUS_GET_PORT_COUNT() and
 * BHND_BUS_GET_REGION_COUNT() to determine if @p port and @p region fall
 * within the defined range.
 */
static bool
bhnd_generic_is_region_valid(device_t dev, device_t child,
    bhnd_port_type type, u_int port, u_int region)
{
	if (port >= bhnd_get_port_count(child, type))
		return (false);

	if (region >= bhnd_get_region_count(child, type, port))
		return (false);

	return (true);
}

/**
 * Default bhnd(4) bus driver implementation of BHND_BUS_GET_NVRAM_VAR().
 * 
 * This implementation searches @p dev for a registered NVRAM child device.
 * 
 * If no NVRAM device is registered with @p dev, the request is delegated to
 * the BHND_BUS_GET_NVRAM_VAR() method on the parent of @p dev.
 */
int
bhnd_generic_get_nvram_var(device_t dev, device_t child, const char *name,
    void *buf, size_t *size, bhnd_nvram_type type)
{
	struct bhnd_softc	*sc;
	device_t		 nvram, parent;
	int			 error;

	sc = device_get_softc(dev);

	/* If a NVRAM device is available, consult it first */
	nvram = bhnd_retain_provider(child, BHND_SERVICE_NVRAM);
	if (nvram != NULL) {
		error = BHND_NVRAM_GETVAR(nvram, name, buf, size, type);
		bhnd_release_provider(child, nvram, BHND_SERVICE_NVRAM);
		return (error);
	}

	/* Otherwise, try to delegate to parent */
	if ((parent = device_get_parent(dev)) == NULL)
		return (ENODEV);

	return (BHND_BUS_GET_NVRAM_VAR(device_get_parent(dev), child,
	    name, buf, size, type));
}

/**
 * Default bhnd(4) bus driver implementation of BUS_PRINT_CHILD().
 * 
 * This implementation requests the device's struct resource_list via
 * BUS_GET_RESOURCE_LIST.
 */
int
bhnd_generic_print_child(device_t dev, device_t child)
{
	struct resource_list	*rl;
	int			retval = 0;

	retval += bus_print_child_header(dev, child);

	rl = BUS_GET_RESOURCE_LIST(dev, child);
	
	
	if (rl != NULL) {
		retval += resource_list_print_type(rl, "mem", SYS_RES_MEMORY,
		    "%#jx");

		retval += resource_list_print_type(rl, "irq", SYS_RES_IRQ,
		    "%#jd");
	}

	retval += printf(" at core %u", bhnd_get_core_index(child));

	retval += bus_print_child_domain(dev, child);
	retval += bus_print_child_footer(dev, child);

	return (retval);
}

/**
 * Default bhnd(4) bus driver implementation of BUS_PROBE_NOMATCH().
 * 
 * This implementation requests the device's struct resource_list via
 * BUS_GET_RESOURCE_LIST.
 */
void
bhnd_generic_probe_nomatch(device_t dev, device_t child)
{
	struct resource_list		*rl;
	const struct bhnd_nomatch	*nm;
	bool				 report;

	/* Fetch reporting configuration for this device */
	report = true;
	for (nm = bhnd_nomatch_table; nm->device != BHND_COREID_INVALID; nm++) {
		if (nm->vendor != bhnd_get_vendor(child))
			continue;

		if (nm->device != bhnd_get_device(child))
			continue;

		report = false;
		if (bootverbose && nm->if_verbose)
			report = true;
		break;
	}
	
	if (!report)
		return;

	/* Print the non-matched device info */
	device_printf(dev, "<%s %s, rev %hhu>", bhnd_get_vendor_name(child),
		bhnd_get_device_name(child), bhnd_get_hwrev(child));

	rl = BUS_GET_RESOURCE_LIST(dev, child);
	if (rl != NULL) {
		resource_list_print_type(rl, "mem", SYS_RES_MEMORY, "%#jx");
		resource_list_print_type(rl, "irq", SYS_RES_IRQ, "%#jd");
	}

	printf(" at core %u (no driver attached)\n",
	    bhnd_get_core_index(child));
}

/**
 * Default implementation of BUS_CHILD_PNPINFO_STR().
 */
static int
bhnd_child_pnpinfo_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	if (device_get_parent(child) != dev) {
		return (BUS_CHILD_PNPINFO_STR(device_get_parent(dev), child,
		    buf, buflen));
	}

	snprintf(buf, buflen, "vendor=0x%hx device=0x%hx rev=0x%hhx",
	    bhnd_get_vendor(child), bhnd_get_device(child),
	    bhnd_get_hwrev(child));

	return (0);
}

/**
 * Default implementation of BUS_CHILD_LOCATION_STR().
 */
static int
bhnd_child_location_str(device_t dev, device_t child, char *buf,
    size_t buflen)
{
	bhnd_addr_t	addr;
	bhnd_size_t	size;
	
	if (device_get_parent(child) != dev) {
		return (BUS_CHILD_LOCATION_STR(device_get_parent(dev), child,
		    buf, buflen));
	}


	if (bhnd_get_region_addr(child, BHND_PORT_DEVICE, 0, 0, &addr, &size)) {
		/* No device default port/region */
		if (buflen > 0)
			*buf = '\0';
		return (0);
	}

	snprintf(buf, buflen, "port0.0=0x%llx", (unsigned long long) addr);
	return (0);
}

/**
 * Default bhnd(4) bus driver implementation of BUS_CHILD_DELETED().
 * 
 * This implementation manages internal bhnd(4) state, and must be called
 * by subclassing drivers.
 */
void
bhnd_generic_child_deleted(device_t dev, device_t child)
{
	struct bhnd_softc	*sc;

	sc = device_get_softc(dev);

	/* Free device info */
	if (bhnd_get_pmu_info(child) != NULL) {
		/* Releasing PMU requests automatically would be nice,
		 * but we can't reference per-core PMU register
		 * resource after driver detach */
		panic("%s leaked device pmu state\n",
		    device_get_nameunit(child));
	}
}

/**
 * Helper function for implementing BUS_SUSPEND_CHILD().
 *
 * TODO: Power management
 * 
 * If @p child is not a direct child of @p dev, suspension is delegated to
 * the @p dev parent.
 */
int
bhnd_generic_suspend_child(device_t dev, device_t child)
{
	if (device_get_parent(child) != dev)
		BUS_SUSPEND_CHILD(device_get_parent(dev), child);

	return bus_generic_suspend_child(dev, child);
}

/**
 * Helper function for implementing BUS_RESUME_CHILD().
 *
 * TODO: Power management
 * 
 * If @p child is not a direct child of @p dev, suspension is delegated to
 * the @p dev parent.
 */
int
bhnd_generic_resume_child(device_t dev, device_t child)
{
	if (device_get_parent(child) != dev)
		BUS_RESUME_CHILD(device_get_parent(dev), child);

	return bus_generic_resume_child(dev, child);
}


/**
 * Default bhnd(4) bus driver implementation of BUS_SETUP_INTR().
 *
 * This implementation of BUS_SETUP_INTR() will delegate interrupt setup
 * to the parent of @p dev, if any.
 */
int
bhnd_generic_setup_intr(device_t dev, device_t child, struct resource *irq,
    int flags, driver_filter_t *filter, driver_intr_t *intr, void *arg,
    void **cookiep)
{
	return (bus_generic_setup_intr(dev, child, irq, flags, filter, intr,
	    arg, cookiep));
}

/*
 * Delegate all indirect I/O to the parent device. When inherited by
 * non-bridged bus implementations, resources will never be marked as
 * indirect, and these methods will never be called.
 */
#define	BHND_IO_READ(_type, _name, _method)				\
static _type								\
bhnd_read_ ## _name (device_t dev, device_t child,			\
    struct bhnd_resource *r, bus_size_t offset)				\
{									\
	return (BHND_BUS_READ_ ## _method(				\
		    device_get_parent(dev), child, r, offset));		\
}

#define	BHND_IO_WRITE(_type, _name, _method)				\
static void								\
bhnd_write_ ## _name (device_t dev, device_t child,			\
    struct bhnd_resource *r, bus_size_t offset, _type value)		\
{									\
	return (BHND_BUS_WRITE_ ## _method(				\
		    device_get_parent(dev), child, r, offset,		\
		    value));	\
}

#define	BHND_IO_MISC(_type, _op, _method)				\
static void								\
bhnd_ ## _op (device_t dev, device_t child,				\
    struct bhnd_resource *r, bus_size_t offset, _type datap,		\
    bus_size_t count)							\
{									\
	BHND_BUS_ ## _method(device_get_parent(dev), child, r,		\
	    offset, datap, count);					\
}	

#define	BHND_IO_METHODS(_type, _size)					\
	BHND_IO_READ(_type, _size, _size)				\
	BHND_IO_WRITE(_type, _size, _size)				\
									\
	BHND_IO_READ(_type, stream_ ## _size, STREAM_ ## _size)		\
	BHND_IO_WRITE(_type, stream_ ## _size, STREAM_ ## _size)	\
									\
	BHND_IO_MISC(_type*, read_multi_ ## _size,			\
	    READ_MULTI_ ## _size)					\
	BHND_IO_MISC(_type*, write_multi_ ## _size,			\
	    WRITE_MULTI_ ## _size)					\
									\
	BHND_IO_MISC(_type*, read_multi_stream_ ## _size,		\
	   READ_MULTI_STREAM_ ## _size)					\
	BHND_IO_MISC(_type*, write_multi_stream_ ## _size,		\
	   WRITE_MULTI_STREAM_ ## _size)				\
									\
	BHND_IO_MISC(_type, set_multi_ ## _size, SET_MULTI_ ## _size)	\
	BHND_IO_MISC(_type, set_region_ ## _size, SET_REGION_ ## _size)	\
									\
	BHND_IO_MISC(_type*, read_region_ ## _size,			\
	    READ_REGION_ ## _size)					\
	BHND_IO_MISC(_type*, write_region_ ## _size,			\
	    WRITE_REGION_ ## _size)					\
									\
	BHND_IO_MISC(_type*, read_region_stream_ ## _size,		\
	    READ_REGION_STREAM_ ## _size)				\
	BHND_IO_MISC(_type*, write_region_stream_ ## _size,		\
	    WRITE_REGION_STREAM_ ## _size)				\

BHND_IO_METHODS(uint8_t, 1);
BHND_IO_METHODS(uint16_t, 2);
BHND_IO_METHODS(uint32_t, 4);

static void 
bhnd_barrier(device_t dev, device_t child, struct bhnd_resource *r,
    bus_size_t offset, bus_size_t length, int flags)
{
	BHND_BUS_BARRIER(device_get_parent(dev), child, r, offset, length,
	    flags);
}

static device_method_t bhnd_methods[] = {
	/* Device interface */ \
	DEVMETHOD(device_attach,		bhnd_generic_attach),
	DEVMETHOD(device_detach,		bhnd_generic_detach),
	DEVMETHOD(device_shutdown,		bhnd_generic_shutdown),
	DEVMETHOD(device_suspend,		bhnd_generic_suspend),
	DEVMETHOD(device_resume,		bhnd_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_child_deleted,		bhnd_generic_child_deleted),
	DEVMETHOD(bus_probe_nomatch,		bhnd_generic_probe_nomatch),
	DEVMETHOD(bus_print_child,		bhnd_generic_print_child),
	DEVMETHOD(bus_child_pnpinfo_str,	bhnd_child_pnpinfo_str),
	DEVMETHOD(bus_child_location_str,	bhnd_child_location_str),

	DEVMETHOD(bus_suspend_child,		bhnd_generic_suspend_child),
	DEVMETHOD(bus_resume_child,		bhnd_generic_resume_child),

	DEVMETHOD(bus_set_resource,		bus_generic_rl_set_resource),
	DEVMETHOD(bus_get_resource,		bus_generic_rl_get_resource),
	DEVMETHOD(bus_delete_resource,		bus_generic_rl_delete_resource),
	DEVMETHOD(bus_alloc_resource,		bus_generic_rl_alloc_resource),
	DEVMETHOD(bus_adjust_resource,		bus_generic_adjust_resource),
	DEVMETHOD(bus_release_resource,		bus_generic_rl_release_resource),
	DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	bus_generic_deactivate_resource),

	DEVMETHOD(bus_setup_intr,		bhnd_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,		bus_generic_teardown_intr),
	DEVMETHOD(bus_config_intr,		bus_generic_config_intr),
	DEVMETHOD(bus_bind_intr,		bus_generic_bind_intr),
	DEVMETHOD(bus_describe_intr,		bus_generic_describe_intr),

	DEVMETHOD(bus_get_dma_tag,		bus_generic_get_dma_tag),

	/* BHND interface */
	DEVMETHOD(bhnd_bus_get_chipid,		bhnd_bus_generic_get_chipid),
	DEVMETHOD(bhnd_bus_is_hw_disabled,	bhnd_bus_generic_is_hw_disabled),

	DEVMETHOD(bhnd_bus_get_probe_order,	bhnd_generic_get_probe_order),

	DEVMETHOD(bhnd_bus_alloc_pmu,		bhnd_generic_alloc_pmu),
	DEVMETHOD(bhnd_bus_release_pmu,		bhnd_generic_release_pmu),
	DEVMETHOD(bhnd_bus_request_clock,	bhnd_generic_request_clock),
	DEVMETHOD(bhnd_bus_enable_clocks,	bhnd_generic_enable_clocks),
	DEVMETHOD(bhnd_bus_request_ext_rsrc,	bhnd_generic_request_ext_rsrc),
	DEVMETHOD(bhnd_bus_release_ext_rsrc,	bhnd_generic_release_ext_rsrc),
	DEVMETHOD(bhnd_bus_get_clock_latency,	bhnd_generic_get_clock_latency),
	DEVMETHOD(bhnd_bus_get_clock_freq,	bhnd_generic_get_clock_freq),

	DEVMETHOD(bhnd_bus_is_region_valid,	bhnd_generic_is_region_valid),
	DEVMETHOD(bhnd_bus_get_nvram_var,	bhnd_generic_get_nvram_var),

	/* BHND interface (bus I/O) */
	DEVMETHOD(bhnd_bus_read_1,		bhnd_read_1),
	DEVMETHOD(bhnd_bus_read_2,		bhnd_read_2),
	DEVMETHOD(bhnd_bus_read_4,		bhnd_read_4),
	DEVMETHOD(bhnd_bus_write_1,		bhnd_write_1),
	DEVMETHOD(bhnd_bus_write_2,		bhnd_write_2),
	DEVMETHOD(bhnd_bus_write_4,		bhnd_write_4),

	DEVMETHOD(bhnd_bus_read_stream_1,	bhnd_read_stream_1),
	DEVMETHOD(bhnd_bus_read_stream_2,	bhnd_read_stream_2),
	DEVMETHOD(bhnd_bus_read_stream_4,	bhnd_read_stream_4),
	DEVMETHOD(bhnd_bus_write_stream_1,	bhnd_write_stream_1),
	DEVMETHOD(bhnd_bus_write_stream_2,	bhnd_write_stream_2),
	DEVMETHOD(bhnd_bus_write_stream_4,	bhnd_write_stream_4),

	DEVMETHOD(bhnd_bus_read_multi_1,	bhnd_read_multi_1),
	DEVMETHOD(bhnd_bus_read_multi_2,	bhnd_read_multi_2),
	DEVMETHOD(bhnd_bus_read_multi_4,	bhnd_read_multi_4),
	DEVMETHOD(bhnd_bus_write_multi_1,	bhnd_write_multi_1),
	DEVMETHOD(bhnd_bus_write_multi_2,	bhnd_write_multi_2),
	DEVMETHOD(bhnd_bus_write_multi_4,	bhnd_write_multi_4),
	
	DEVMETHOD(bhnd_bus_read_multi_stream_1,	bhnd_read_multi_stream_1),
	DEVMETHOD(bhnd_bus_read_multi_stream_2,	bhnd_read_multi_stream_2),
	DEVMETHOD(bhnd_bus_read_multi_stream_4,	bhnd_read_multi_stream_4),
	DEVMETHOD(bhnd_bus_write_multi_stream_1,bhnd_write_multi_stream_1),
	DEVMETHOD(bhnd_bus_write_multi_stream_2,bhnd_write_multi_stream_2),
	DEVMETHOD(bhnd_bus_write_multi_stream_4,bhnd_write_multi_stream_4),

	DEVMETHOD(bhnd_bus_set_multi_1,		bhnd_set_multi_1),
	DEVMETHOD(bhnd_bus_set_multi_2,		bhnd_set_multi_2),
	DEVMETHOD(bhnd_bus_set_multi_4,		bhnd_set_multi_4),

	DEVMETHOD(bhnd_bus_set_region_1,	bhnd_set_region_1),
	DEVMETHOD(bhnd_bus_set_region_2,	bhnd_set_region_2),
	DEVMETHOD(bhnd_bus_set_region_4,	bhnd_set_region_4),

	DEVMETHOD(bhnd_bus_read_region_1,	bhnd_read_region_1),
	DEVMETHOD(bhnd_bus_read_region_2,	bhnd_read_region_2),
	DEVMETHOD(bhnd_bus_read_region_4,	bhnd_read_region_4),
	DEVMETHOD(bhnd_bus_write_region_1,	bhnd_write_region_1),
	DEVMETHOD(bhnd_bus_write_region_2,	bhnd_write_region_2),
	DEVMETHOD(bhnd_bus_write_region_4,	bhnd_write_region_4),

	DEVMETHOD(bhnd_bus_read_region_stream_1,bhnd_read_region_stream_1),
	DEVMETHOD(bhnd_bus_read_region_stream_2,bhnd_read_region_stream_2),
	DEVMETHOD(bhnd_bus_read_region_stream_4,bhnd_read_region_stream_4),
	DEVMETHOD(bhnd_bus_write_region_stream_1, bhnd_write_region_stream_1),
	DEVMETHOD(bhnd_bus_write_region_stream_2, bhnd_write_region_stream_2),
	DEVMETHOD(bhnd_bus_write_region_stream_4, bhnd_write_region_stream_4),

	DEVMETHOD(bhnd_bus_barrier,			bhnd_barrier),

	DEVMETHOD_END
};

devclass_t bhnd_devclass;	/**< bhnd bus. */
devclass_t bhnd_hostb_devclass;	/**< bhnd bus host bridge. */
devclass_t bhnd_nvram_devclass;	/**< bhnd NVRAM device */

DEFINE_CLASS_0(bhnd, bhnd_driver, bhnd_methods, sizeof(struct bhnd_softc));
MODULE_VERSION(bhnd, 1);
