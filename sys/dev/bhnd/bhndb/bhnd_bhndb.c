/*-
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>

#include <dev/bhnd/bhnd_ids.h>
#include <dev/bhnd/bhnd.h>

#include "bhnd_pwrctl_hostb_if.h"

#include "bhndbvar.h"

/*
 * bhnd(4) driver mix-in providing a shared common methods for
 * bhnd devices attached via a bhndb bridge.
 */

static int
bhnd_bhndb_read_board_info(device_t dev, device_t child,
    struct bhnd_board_info *info)
{
	int	error;

	/* Initialize with NVRAM-derived values */
	if ((error = bhnd_bus_generic_read_board_info(dev, child, info)))
		return (error);

	/* Let the bridge fill in any additional data */
	return (BHNDB_POPULATE_BOARD_INFO(device_get_parent(dev), dev, info));
}

static bhnd_attach_type
bhnd_bhndb_get_attach_type(device_t dev, device_t child)
{
	/* It's safe to assume that a bridged device is always an adapter */
	return (BHND_ATTACH_ADAPTER);
}


static bool
bhnd_bhndb_is_hw_disabled(device_t dev, device_t child)
{
	struct bhnd_core_info core = bhnd_get_core_info(child);

	/* Delegate to parent bridge */
	return (BHNDB_IS_CORE_DISABLED(device_get_parent(dev), dev, &core));
}


static device_t
bhnd_bhndb_find_hostb_device(device_t dev)
{
	struct bhnd_core_info	 core;
	struct bhnd_core_match	 md;
	int			 error;

	/* Ask the bridge for the hostb core info */
	if ((error = BHNDB_GET_HOSTB_CORE(device_get_parent(dev), dev, &core)))
		return (NULL);

	/* Find the corresponding bus device */
	md = bhnd_core_get_match_desc(&core);
	return (bhnd_bus_match_child(dev, &md));
}

static int
bhnd_bhndb_map_intr(device_t dev, device_t child, u_int intr, rman_res_t *irq)
{
	/* Delegate to parent bridge */
	return (BHND_BUS_MAP_INTR(device_get_parent(dev), child, intr, irq));
}

static void
bhnd_bhndb_unmap_intr(device_t dev, device_t child, rman_res_t irq)
{
	/* Delegate to parent bridge */
	return (BHND_BUS_UNMAP_INTR(device_get_parent(dev), child, irq));
}

static bhnd_clksrc
bhnd_bhndb_pwrctl_get_clksrc(device_t dev, device_t child,
	bhnd_clock clock)
{
	/* Delegate to parent bridge */
	return (BHND_PWRCTL_HOSTB_GET_CLKSRC(device_get_parent(dev), child,
	    clock));
}

static int
bhnd_bhndb_pwrctl_gate_clock(device_t dev, device_t child,
	bhnd_clock clock)
{
	/* Delegate to parent bridge */
	return (BHND_PWRCTL_HOSTB_GATE_CLOCK(device_get_parent(dev), child,
	    clock));
}

static int
bhnd_bhndb_pwrctl_ungate_clock(device_t dev, device_t child,
	bhnd_clock clock)
{
	/* Delegate to parent bridge */
	return (BHND_PWRCTL_HOSTB_UNGATE_CLOCK(device_get_parent(dev), child,
	    clock));
}

static int
bhnd_bhndb_setup_intr(device_t dev, device_t child, struct resource *irq,
    int flags, driver_filter_t *filter, driver_intr_t *intr, void *arg,
    void **cookiep)
{
	device_t	core, bus;
	int		error;

	/* Find the actual bus-attached child core */
	core = child;
	while ((bus = device_get_parent(core)) != NULL) {
		if (bus == dev)
			break;

		core = bus;
	}

	KASSERT(core != NULL, ("%s is not a child of %s",
	    device_get_nameunit(child), device_get_nameunit(dev)));

	/* Ask our bridge to enable interrupt routing for the child core */
	error = BHNDB_ROUTE_INTERRUPTS(device_get_parent(dev), core);
	if (error)
		return (error);

	/* Delegate actual interrupt setup to the default bhnd bus
	 * implementation */
	return (bhnd_generic_setup_intr(dev, child, irq, flags, filter, intr,
	    arg, cookiep));
}

static device_method_t bhnd_bhndb_methods[] = {
	/* Bus interface */
	DEVMETHOD(bus_setup_intr,			bhnd_bhndb_setup_intr),

	/* BHND interface */
	DEVMETHOD(bhnd_bus_get_attach_type,		bhnd_bhndb_get_attach_type),
	DEVMETHOD(bhnd_bus_is_hw_disabled,		bhnd_bhndb_is_hw_disabled),
	DEVMETHOD(bhnd_bus_find_hostb_device,		bhnd_bhndb_find_hostb_device),
	DEVMETHOD(bhnd_bus_read_board_info,		bhnd_bhndb_read_board_info),
	DEVMETHOD(bhnd_bus_map_intr,			bhnd_bhndb_map_intr),
	DEVMETHOD(bhnd_bus_unmap_intr,			bhnd_bhndb_unmap_intr),

	/* BHND PWRCTL hostb interface */
	DEVMETHOD(bhnd_pwrctl_hostb_get_clksrc,		bhnd_bhndb_pwrctl_get_clksrc),
	DEVMETHOD(bhnd_pwrctl_hostb_gate_clock,		bhnd_bhndb_pwrctl_gate_clock),
	DEVMETHOD(bhnd_pwrctl_hostb_ungate_clock,	bhnd_bhndb_pwrctl_ungate_clock),

	DEVMETHOD_END
};

DEFINE_CLASS_0(bhnd, bhnd_bhndb_driver, bhnd_bhndb_methods, 0);
