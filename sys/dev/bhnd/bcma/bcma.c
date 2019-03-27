/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <machine/bus.h>

#include <dev/bhnd/cores/pmu/bhnd_pmu.h>

#include "bcma_dmp.h"

#include "bcma_eromreg.h"
#include "bcma_eromvar.h"

#include "bcmavar.h"

/* RID used when allocating EROM table */
#define	BCMA_EROM_RID	0

static bhnd_erom_class_t *
bcma_get_erom_class(driver_t *driver)
{
	return (&bcma_erom_parser);
}

int
bcma_probe(device_t dev)
{
	device_set_desc(dev, "BCMA BHND bus");
	return (BUS_PROBE_DEFAULT);
}

/**
 * Default bcma(4) bus driver implementation of DEVICE_ATTACH().
 * 
 * This implementation initializes internal bcma(4) state and performs
 * bus enumeration, and must be called by subclassing drivers in
 * DEVICE_ATTACH() before any other bus methods.
 */
int
bcma_attach(device_t dev)
{
	int error;

	/* Enumerate children */
	if ((error = bcma_add_children(dev))) {
		device_delete_children(dev);
		return (error);
	}

	return (0);
}

int
bcma_detach(device_t dev)
{
	return (bhnd_generic_detach(dev));
}

static device_t
bcma_add_child(device_t dev, u_int order, const char *name, int unit)
{
	struct bcma_devinfo	*dinfo;
	device_t		 child;

	child = device_add_child_ordered(dev, order, name, unit);
	if (child == NULL)
		return (NULL);

	if ((dinfo = bcma_alloc_dinfo(dev)) == NULL) {
		device_delete_child(dev, child);
		return (NULL);
	}

	device_set_ivars(child, dinfo);

	return (child);
}

static void
bcma_child_deleted(device_t dev, device_t child)
{
	struct bhnd_softc	*sc;
	struct bcma_devinfo	*dinfo;

	sc = device_get_softc(dev);

	/* Call required bhnd(4) implementation */
	bhnd_generic_child_deleted(dev, child);

	/* Free bcma device info */
	if ((dinfo = device_get_ivars(child)) != NULL)
		bcma_free_dinfo(dev, child, dinfo);

	device_set_ivars(child, NULL);
}

static int
bcma_read_ivar(device_t dev, device_t child, int index, uintptr_t *result)
{
	const struct bcma_devinfo *dinfo;
	const struct bhnd_core_info *ci;
	
	dinfo = device_get_ivars(child);
	ci = &dinfo->corecfg->core_info;
	
	switch (index) {
	case BHND_IVAR_VENDOR:
		*result = ci->vendor;
		return (0);
	case BHND_IVAR_DEVICE:
		*result = ci->device;
		return (0);
	case BHND_IVAR_HWREV:
		*result = ci->hwrev;
		return (0);
	case BHND_IVAR_DEVICE_CLASS:
		*result = bhnd_core_class(ci);
		return (0);
	case BHND_IVAR_VENDOR_NAME:
		*result = (uintptr_t) bhnd_vendor_name(ci->vendor);
		return (0);
	case BHND_IVAR_DEVICE_NAME:
		*result = (uintptr_t) bhnd_core_name(ci);
		return (0);
	case BHND_IVAR_CORE_INDEX:
		*result = ci->core_idx;
		return (0);
	case BHND_IVAR_CORE_UNIT:
		*result = ci->unit;
		return (0);
	case BHND_IVAR_PMU_INFO:
		*result = (uintptr_t) dinfo->pmu_info;
		return (0);
	default:
		return (ENOENT);
	}
}

static int
bcma_write_ivar(device_t dev, device_t child, int index, uintptr_t value)
{
	struct bcma_devinfo *dinfo;

	dinfo = device_get_ivars(child);

	switch (index) {
	case BHND_IVAR_VENDOR:
	case BHND_IVAR_DEVICE:
	case BHND_IVAR_HWREV:
	case BHND_IVAR_DEVICE_CLASS:
	case BHND_IVAR_VENDOR_NAME:
	case BHND_IVAR_DEVICE_NAME:
	case BHND_IVAR_CORE_INDEX:
	case BHND_IVAR_CORE_UNIT:
		return (EINVAL);
	case BHND_IVAR_PMU_INFO:
		dinfo->pmu_info = (void *)value;
		return (0);
	default:
		return (ENOENT);
	}
}

static struct resource_list *
bcma_get_resource_list(device_t dev, device_t child)
{
	struct bcma_devinfo *dinfo = device_get_ivars(child);
	return (&dinfo->resources);
}

static int
bcma_read_iost(device_t dev, device_t child, uint16_t *iost)
{
	uint32_t	value;
	int		error;

	if ((error = bhnd_read_config(child, BCMA_DMP_IOSTATUS, &value, 4)))
		return (error);

	/* Return only the bottom 16 bits */
	*iost = (value & BCMA_DMP_IOST_MASK);
	return (0);
}

static int
bcma_read_ioctl(device_t dev, device_t child, uint16_t *ioctl)
{
	uint32_t	value;
	int		error;

	if ((error = bhnd_read_config(child, BCMA_DMP_IOCTRL, &value, 4)))
		return (error);

	/* Return only the bottom 16 bits */
	*ioctl = (value & BCMA_DMP_IOCTRL_MASK);
	return (0);
}

static int
bcma_write_ioctl(device_t dev, device_t child, uint16_t value, uint16_t mask)
{
	struct bcma_devinfo	*dinfo;
	struct bhnd_resource	*r;
	uint32_t		 ioctl;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	dinfo = device_get_ivars(child);
	if ((r = dinfo->res_agent) == NULL)
		return (ENODEV);

	/* Write new value */
	ioctl = bhnd_bus_read_4(r, BCMA_DMP_IOCTRL);
	ioctl &= ~(BCMA_DMP_IOCTRL_MASK & mask);
	ioctl |= (value & mask);

	bhnd_bus_write_4(r, BCMA_DMP_IOCTRL, ioctl);

	/* Perform read-back and wait for completion */
	bhnd_bus_read_4(r, BCMA_DMP_IOCTRL);
	DELAY(10);

	return (0);
}

static bool
bcma_is_hw_suspended(device_t dev, device_t child)
{
	uint32_t	rst;
	uint16_t	ioctl;
	int		error;

	/* Is core held in RESET? */
	error = bhnd_read_config(child, BCMA_DMP_RESETCTRL, &rst, 4);
	if (error) {
		device_printf(child, "error reading HW reset state: %d\n",
		    error);
		return (true);
	}

	if (rst & BCMA_DMP_RC_RESET)
		return (true);

	/* Is core clocked? */
	error = bhnd_read_ioctl(child, &ioctl);
	if (error) {
		device_printf(child, "error reading HW ioctl register: %d\n",
		    error);
		return (true);
	}

	if (!(ioctl & BHND_IOCTL_CLK_EN))
		return (true);

	return (false);
}

static int
bcma_reset_hw(device_t dev, device_t child, uint16_t ioctl,
    uint16_t reset_ioctl)
{
	struct bcma_devinfo	*dinfo;
	struct bhnd_resource	*r;
	uint16_t		 clkflags;
	int			 error;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	dinfo = device_get_ivars(child);

	/* We require exclusive control over BHND_IOCTL_CLK_(EN|FORCE) */
	clkflags = BHND_IOCTL_CLK_EN | BHND_IOCTL_CLK_FORCE;
	if (ioctl & clkflags)
		return (EINVAL);

	/* Can't suspend the core without access to the agent registers */
	if ((r = dinfo->res_agent) == NULL)
		return (ENODEV);

	/* Place core into known RESET state */
	if ((error = bhnd_suspend_hw(child, reset_ioctl)))
		return (error);

	/*
	 * Leaving the core in reset:
	 * - Set the caller's IOCTL flags
	 * - Enable clocks
	 * - Force clock distribution to ensure propagation throughout the
	 *   core.
	 */
	if ((error = bhnd_write_ioctl(child, ioctl | clkflags, UINT16_MAX)))
		return (error);

	/* Bring the core out of reset */
	if ((error = bcma_dmp_write_reset(child, dinfo, 0x0)))
		return (error);

	/* Disable forced clock gating (leaving clock enabled) */
	error = bhnd_write_ioctl(child, 0x0, BHND_IOCTL_CLK_FORCE);
	if (error)
		return (error);

	return (0);
}

static int
bcma_suspend_hw(device_t dev, device_t child, uint16_t ioctl)
{
	struct bcma_devinfo	*dinfo;
	struct bhnd_resource	*r;
	uint16_t		 clkflags;
	int			 error;

	if (device_get_parent(child) != dev)
		return (EINVAL);

	dinfo = device_get_ivars(child);

	/* We require exclusive control over BHND_IOCTL_CLK_(EN|FORCE) */
	clkflags = BHND_IOCTL_CLK_EN | BHND_IOCTL_CLK_FORCE;
	if (ioctl & clkflags)
		return (EINVAL);

	/* Can't suspend the core without access to the agent registers */
	if ((r = dinfo->res_agent) == NULL)
		return (ENODEV);

	/* Wait for any pending reset operations to clear */
	if ((error = bcma_dmp_wait_reset(child, dinfo)))
		return (error);

	/* Put core into reset (if not already in reset) */
	if ((error = bcma_dmp_write_reset(child, dinfo, BCMA_DMP_RC_RESET)))
		return (error);

	/* Write core flags (and clear CLK_EN/CLK_FORCE) */
	if ((error = bhnd_write_ioctl(child, ioctl, ~clkflags)))
		return (error);

	return (0);
}

static int
bcma_read_config(device_t dev, device_t child, bus_size_t offset, void *value,
    u_int width)
{
	struct bcma_devinfo	*dinfo;
	struct bhnd_resource	*r;

	/* Must be a directly attached child core */
	if (device_get_parent(child) != dev)
		return (EINVAL);

	/* Fetch the agent registers */
	dinfo = device_get_ivars(child);
	if ((r = dinfo->res_agent) == NULL)
		return (ENODEV);

	/* Verify bounds */
	if (offset > rman_get_size(r->res))
		return (EFAULT);

	if (rman_get_size(r->res) - offset < width)
		return (EFAULT);

	switch (width) {
	case 1:
		*((uint8_t *)value) = bhnd_bus_read_1(r, offset);
		return (0);
	case 2:
		*((uint16_t *)value) = bhnd_bus_read_2(r, offset);
		return (0);
	case 4:
		*((uint32_t *)value) = bhnd_bus_read_4(r, offset);
		return (0);
	default:
		return (EINVAL);
	}
}

static int
bcma_write_config(device_t dev, device_t child, bus_size_t offset,
    const void *value, u_int width)
{
	struct bcma_devinfo	*dinfo;
	struct bhnd_resource	*r;

	/* Must be a directly attached child core */
	if (device_get_parent(child) != dev)
		return (EINVAL);

	/* Fetch the agent registers */
	dinfo = device_get_ivars(child);
	if ((r = dinfo->res_agent) == NULL)
		return (ENODEV);

	/* Verify bounds */
	if (offset > rman_get_size(r->res))
		return (EFAULT);

	if (rman_get_size(r->res) - offset < width)
		return (EFAULT);

	switch (width) {
	case 1:
		bhnd_bus_write_1(r, offset, *(const uint8_t *)value);
		return (0);
	case 2:
		bhnd_bus_write_2(r, offset, *(const uint16_t *)value);
		return (0);
	case 4:
		bhnd_bus_write_4(r, offset, *(const uint32_t *)value);
		return (0);
	default:
		return (EINVAL);
	}
}

static u_int
bcma_get_port_count(device_t dev, device_t child, bhnd_port_type type)
{
	struct bcma_devinfo *dinfo;

	/* delegate non-bus-attached devices to our parent */
	if (device_get_parent(child) != dev)
		return (BHND_BUS_GET_PORT_COUNT(device_get_parent(dev), child,
		    type));

	dinfo = device_get_ivars(child);
	switch (type) {
	case BHND_PORT_DEVICE:
		return (dinfo->corecfg->num_dev_ports);
	case BHND_PORT_BRIDGE:
		return (dinfo->corecfg->num_bridge_ports);
	case BHND_PORT_AGENT:
		return (dinfo->corecfg->num_wrapper_ports);
	default:
		device_printf(dev, "%s: unknown type (%d)\n",
		    __func__,
		    type);
		return (0);
	}
}

static u_int
bcma_get_region_count(device_t dev, device_t child, bhnd_port_type type,
    u_int port_num)
{
	struct bcma_devinfo	*dinfo;
	struct bcma_sport_list	*ports;
	struct bcma_sport	*port;

	/* delegate non-bus-attached devices to our parent */
	if (device_get_parent(child) != dev)
		return (BHND_BUS_GET_REGION_COUNT(device_get_parent(dev), child,
		    type, port_num));

	dinfo = device_get_ivars(child);
	ports = bcma_corecfg_get_port_list(dinfo->corecfg, type);
	
	STAILQ_FOREACH(port, ports, sp_link) {
		if (port->sp_num == port_num)
			return (port->sp_num_maps);
	}

	/* not found */
	return (0);
}

static int
bcma_get_port_rid(device_t dev, device_t child, bhnd_port_type port_type,
    u_int port_num, u_int region_num)
{
	struct bcma_devinfo	*dinfo;
	struct bcma_map		*map;
	struct bcma_sport_list	*ports;
	struct bcma_sport	*port;
	
	dinfo = device_get_ivars(child);
	ports = bcma_corecfg_get_port_list(dinfo->corecfg, port_type);

	STAILQ_FOREACH(port, ports, sp_link) {
		if (port->sp_num != port_num)
			continue;

		STAILQ_FOREACH(map, &port->sp_maps, m_link)
			if (map->m_region_num == region_num)
				return map->m_rid;
	}

	return -1;
}

static int
bcma_decode_port_rid(device_t dev, device_t child, int type, int rid,
    bhnd_port_type *port_type, u_int *port_num, u_int *region_num)
{
	struct bcma_devinfo	*dinfo;
	struct bcma_map		*map;
	struct bcma_sport_list	*ports;
	struct bcma_sport	*port;

	dinfo = device_get_ivars(child);

	/* Ports are always memory mapped */
	if (type != SYS_RES_MEMORY)
		return (EINVAL);

	/* Starting with the most likely device list, search all three port
	 * lists */
	bhnd_port_type types[] = {
	    BHND_PORT_DEVICE, 
	    BHND_PORT_AGENT,
	    BHND_PORT_BRIDGE
	};

	for (int i = 0; i < nitems(types); i++) {
		ports = bcma_corecfg_get_port_list(dinfo->corecfg, types[i]);

		STAILQ_FOREACH(port, ports, sp_link) {
			STAILQ_FOREACH(map, &port->sp_maps, m_link) {
				if (map->m_rid != rid)
					continue;

				*port_type = port->sp_type;
				*port_num = port->sp_num;
				*region_num = map->m_region_num;
				return (0);
			}
		}
	}

	return (ENOENT);
}

static int
bcma_get_region_addr(device_t dev, device_t child, bhnd_port_type port_type,
    u_int port_num, u_int region_num, bhnd_addr_t *addr, bhnd_size_t *size)
{
	struct bcma_devinfo	*dinfo;
	struct bcma_map		*map;
	struct bcma_sport_list	*ports;
	struct bcma_sport	*port;
	
	dinfo = device_get_ivars(child);
	ports = bcma_corecfg_get_port_list(dinfo->corecfg, port_type);

	/* Search the port list */
	STAILQ_FOREACH(port, ports, sp_link) {
		if (port->sp_num != port_num)
			continue;

		STAILQ_FOREACH(map, &port->sp_maps, m_link) {
			if (map->m_region_num != region_num)
				continue;

			/* Found! */
			*addr = map->m_base;
			*size = map->m_size;
			return (0);
		}
	}

	return (ENOENT);
}

/**
 * Default bcma(4) bus driver implementation of BHND_BUS_GET_INTR_COUNT().
 */
u_int
bcma_get_intr_count(device_t dev, device_t child)
{
	struct bcma_devinfo *dinfo;

	/* delegate non-bus-attached devices to our parent */
	if (device_get_parent(child) != dev)
		return (BHND_BUS_GET_INTR_COUNT(device_get_parent(dev), child));

	dinfo = device_get_ivars(child);
	return (dinfo->num_intrs);
}

/**
 * Default bcma(4) bus driver implementation of BHND_BUS_GET_INTR_IVEC().
 */
int
bcma_get_intr_ivec(device_t dev, device_t child, u_int intr, u_int *ivec)
{
	struct bcma_devinfo	*dinfo;
	struct bcma_intr	*desc;

	/* delegate non-bus-attached devices to our parent */
	if (device_get_parent(child) != dev) {
		return (BHND_BUS_GET_INTR_IVEC(device_get_parent(dev), child,
		    intr, ivec));
	}

	dinfo = device_get_ivars(child);

	STAILQ_FOREACH(desc, &dinfo->intrs, i_link) {
		if (desc->i_sel == intr) {
			*ivec = desc->i_busline;
			return (0);
		}
	}

	/* Not found */
	return (ENXIO);
}

/**
 * Scan the device enumeration ROM table, adding all valid discovered cores to
 * the bus.
 * 
 * @param bus The bcma bus.
 */
int
bcma_add_children(device_t bus)
{
	bhnd_erom_t			*erom;
	struct bcma_erom		*bcma_erom;
	struct bhnd_erom_io		*eio;
	const struct bhnd_chipid	*cid;
	struct bcma_corecfg		*corecfg;
	struct bcma_devinfo		*dinfo;
	device_t			 child;
	int				 error;

	cid = BHND_BUS_GET_CHIPID(bus, bus);
	corecfg = NULL;

	/* Allocate our EROM parser */
	eio = bhnd_erom_iores_new(bus, BCMA_EROM_RID);
	erom = bhnd_erom_alloc(&bcma_erom_parser, cid, eio);
	if (erom == NULL) {
		bhnd_erom_io_fini(eio);
		return (ENODEV);
	}

	/* Add all cores. */
	bcma_erom = (struct bcma_erom *)erom;
	while ((error = bcma_erom_next_corecfg(bcma_erom, &corecfg)) == 0) {
		/* Add the child device */
		child = BUS_ADD_CHILD(bus, 0, NULL, -1);
		if (child == NULL) {
			error = ENXIO;
			goto cleanup;
		}

		/* Initialize device ivars */
		dinfo = device_get_ivars(child);
		if ((error = bcma_init_dinfo(bus, child, dinfo, corecfg)))
			goto cleanup;

		/* The dinfo instance now owns the corecfg value */
		corecfg = NULL;

		/* If pins are floating or the hardware is otherwise
		 * unpopulated, the device shouldn't be used. */
		if (bhnd_is_hw_disabled(child))
			device_disable(child);

		/* Issue bus callback for fully initialized child. */
		BHND_BUS_CHILD_ADDED(bus, child);
	}

	/* EOF while parsing cores is expected */
	if (error == ENOENT)
		error = 0;
	
cleanup:
	bhnd_erom_free(erom);

	if (corecfg != NULL)
		bcma_free_corecfg(corecfg);

	if (error)
		device_delete_children(bus);

	return (error);
}


static device_method_t bcma_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			bcma_probe),
	DEVMETHOD(device_attach,		bcma_attach),
	DEVMETHOD(device_detach,		bcma_detach),
	
	/* Bus interface */
	DEVMETHOD(bus_add_child,		bcma_add_child),
	DEVMETHOD(bus_child_deleted,		bcma_child_deleted),
	DEVMETHOD(bus_read_ivar,		bcma_read_ivar),
	DEVMETHOD(bus_write_ivar,		bcma_write_ivar),
	DEVMETHOD(bus_get_resource_list,	bcma_get_resource_list),

	/* BHND interface */
	DEVMETHOD(bhnd_bus_get_erom_class,	bcma_get_erom_class),
	DEVMETHOD(bhnd_bus_read_ioctl,		bcma_read_ioctl),
	DEVMETHOD(bhnd_bus_write_ioctl,		bcma_write_ioctl),
	DEVMETHOD(bhnd_bus_read_iost,		bcma_read_iost),
	DEVMETHOD(bhnd_bus_is_hw_suspended,	bcma_is_hw_suspended),
	DEVMETHOD(bhnd_bus_reset_hw,		bcma_reset_hw),
	DEVMETHOD(bhnd_bus_suspend_hw,		bcma_suspend_hw),
	DEVMETHOD(bhnd_bus_read_config,		bcma_read_config),
	DEVMETHOD(bhnd_bus_write_config,	bcma_write_config),
	DEVMETHOD(bhnd_bus_get_port_count,	bcma_get_port_count),
	DEVMETHOD(bhnd_bus_get_region_count,	bcma_get_region_count),
	DEVMETHOD(bhnd_bus_get_port_rid,	bcma_get_port_rid),
	DEVMETHOD(bhnd_bus_decode_port_rid,	bcma_decode_port_rid),
	DEVMETHOD(bhnd_bus_get_region_addr,	bcma_get_region_addr),
	DEVMETHOD(bhnd_bus_get_intr_count,	bcma_get_intr_count),
	DEVMETHOD(bhnd_bus_get_intr_ivec,	bcma_get_intr_ivec),

	DEVMETHOD_END
};

DEFINE_CLASS_1(bhnd, bcma_driver, bcma_methods, sizeof(struct bcma_softc), bhnd_driver);
MODULE_VERSION(bcma, 1);
MODULE_DEPEND(bcma, bhnd, 1, 1, 1);
