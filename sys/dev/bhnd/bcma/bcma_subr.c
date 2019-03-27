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
#include <sys/limits.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/bhnd/bhndvar.h>

#include "bcma_dmp.h"

#include "bcmavar.h"

/* Return the resource ID for a device's agent register allocation */
#define	BCMA_AGENT_RID(_dinfo)	\
    (BCMA_AGENT_RID_BASE + BCMA_DINFO_COREIDX(_dinfo))

 /**
 * Allocate and initialize new core config structure.
 * 
 * @param core_index Core index on the bus.
 * @param core_unit Core unit number.
 * @param vendor Core designer.
 * @param device Core identifier (e.g. part number).
 * @param hwrev Core revision.
 */
struct bcma_corecfg *
bcma_alloc_corecfg(u_int core_index, int core_unit, uint16_t vendor,
    uint16_t device, uint8_t hwrev)
{
	struct bcma_corecfg *cfg;

	cfg = malloc(sizeof(*cfg), M_BHND, M_NOWAIT);
	if (cfg == NULL)
		return NULL;

	cfg->core_info = (struct bhnd_core_info) {
		.vendor = vendor,
		.device = device,
		.hwrev = hwrev,
		.core_idx = core_index,
		.unit = core_unit
	};
	
	STAILQ_INIT(&cfg->master_ports);
	cfg->num_master_ports = 0;

	STAILQ_INIT(&cfg->dev_ports);
	cfg->num_dev_ports = 0;

	STAILQ_INIT(&cfg->bridge_ports);
	cfg->num_bridge_ports = 0;

	STAILQ_INIT(&cfg->wrapper_ports);
	cfg->num_wrapper_ports = 0;

	return (cfg);
}

/**
 * Deallocate the given core config and any associated resources.
 * 
 * @param corecfg Core info to be deallocated.
 */
void
bcma_free_corecfg(struct bcma_corecfg *corecfg)
{
	struct bcma_mport *mport, *mnext;
	struct bcma_sport *sport, *snext;

	STAILQ_FOREACH_SAFE(mport, &corecfg->master_ports, mp_link, mnext) {
		free(mport, M_BHND);
	}
	
	STAILQ_FOREACH_SAFE(sport, &corecfg->dev_ports, sp_link, snext) {
		bcma_free_sport(sport);
	}

	STAILQ_FOREACH_SAFE(sport, &corecfg->bridge_ports, sp_link, snext) {
		bcma_free_sport(sport);
	}
	
	STAILQ_FOREACH_SAFE(sport, &corecfg->wrapper_ports, sp_link, snext) {
		bcma_free_sport(sport);
	}

	free(corecfg, M_BHND);
}

/**
 * Return the @p cfg port list for @p type.
 * 
 * @param cfg The core configuration.
 * @param type The requested port type.
 */
struct bcma_sport_list *
bcma_corecfg_get_port_list(struct bcma_corecfg *cfg, bhnd_port_type type)
{
	switch (type) {
	case BHND_PORT_DEVICE:
		return (&cfg->dev_ports);
		break;
	case BHND_PORT_BRIDGE:
		return (&cfg->bridge_ports);
		break;
	case BHND_PORT_AGENT:
		return (&cfg->wrapper_ports);
		break;
	default:
		return (NULL);
	}
}

/**
 * Populate the resource list and bcma_map RIDs using the maps defined on
 * @p ports.
 * 
 * @param bus The requesting bus device.
 * @param dinfo The device info instance to be initialized.
 * @param ports The set of ports to be enumerated
 */
static void
bcma_dinfo_init_port_resource_info(device_t bus, struct bcma_devinfo *dinfo,
    struct bcma_sport_list *ports)
{
	struct bcma_map		*map;
	struct bcma_sport	*port;
	bhnd_addr_t		 end;

	STAILQ_FOREACH(port, ports, sp_link) {
		STAILQ_FOREACH(map, &port->sp_maps, m_link) {
			/*
			 * Create the corresponding device resource list entry.
			 * 
			 * We necessarily skip registration if the region's
			 * device memory range is not representable via
			 * rman_res_t.
			 * 
			 * When rman_res_t is migrated to uintmax_t, any
			 * range should be representable.
			 */
			end = map->m_base + map->m_size;
			if (map->m_base <= RM_MAX_END && end <= RM_MAX_END) {
				map->m_rid = resource_list_add_next(
				    &dinfo->resources, SYS_RES_MEMORY,
				    map->m_base, end, map->m_size);
			} else if (bootverbose) {
				device_printf(bus,
				    "core%u %s%u.%u: region %llx-%llx extends "
				        "beyond supported addressable range\n",
				    dinfo->corecfg->core_info.core_idx,
				    bhnd_port_type_name(port->sp_type),
				    port->sp_num, map->m_region_num,
				    (unsigned long long) map->m_base,
				    (unsigned long long) end);
			}
		}
	}
}



/**
 * Allocate the per-core agent register block for a device info structure.
 * 
 * If an agent0.0 region is not defined on @p dinfo, the device info
 * agent resource is set to NULL and 0 is returned.
 * 
 * @param bus The requesting bus device.
 * @param child The bcma child device.
 * @param dinfo The device info associated with @p child
 * 
 * @retval 0 success
 * @retval non-zero resource allocation failed.
 */
static int
bcma_dinfo_init_agent(device_t bus, device_t child, struct bcma_devinfo *dinfo)
{
	bhnd_addr_t	addr;
	bhnd_size_t	size;
	rman_res_t	r_start, r_count, r_end;
	int		error;

	KASSERT(dinfo->res_agent == NULL, ("double allocation of agent"));

	/* Verify that the agent register block exists and is
	 * mappable */
	if (bhnd_get_port_rid(child, BHND_PORT_AGENT, 0, 0) == -1)
		return (0);	/* nothing to do */

	/* Fetch the address of the agent register block */
	error = bhnd_get_region_addr(child, BHND_PORT_AGENT, 0, 0,
	    &addr, &size);
	if (error) {
		device_printf(bus, "failed fetching agent register block "
		    "address for core %u\n", BCMA_DINFO_COREIDX(dinfo));
		return (error);
	}

	/* Allocate the resource */
	r_start = addr;
	r_count = size;
	r_end = r_start + r_count - 1;

	dinfo->rid_agent = BCMA_AGENT_RID(dinfo);
	dinfo->res_agent = BHND_BUS_ALLOC_RESOURCE(bus, bus, SYS_RES_MEMORY,
	    &dinfo->rid_agent, r_start, r_end, r_count, RF_ACTIVE|RF_SHAREABLE);
	if (dinfo->res_agent == NULL) {
		device_printf(bus, "failed allocating agent register block for "
		    "core %u\n", BCMA_DINFO_COREIDX(dinfo));
		return (ENXIO);
	}

	return (0);
}

/**
 * Populate the list of interrupts for a device info structure
 * previously initialized via bcma_dinfo_alloc_agent().
 * 
 * If an agent0.0 region is not mapped on @p dinfo, the OOB interrupt bank is
 * assumed to be unavailable and 0 is returned.
 * 
 * @param bus The requesting bus device.
 * @param dinfo The device info instance to be initialized.
 */
static int
bcma_dinfo_init_intrs(device_t bus, device_t child,
    struct bcma_devinfo *dinfo)
{
	uint32_t dmpcfg, oobw;

	/* Agent block must be mapped */
	if (dinfo->res_agent == NULL)
		return (0);

	/* Agent must support OOB */
	dmpcfg = bhnd_bus_read_4(dinfo->res_agent, BCMA_DMP_CONFIG);
	if (!BCMA_DMP_GET_FLAG(dmpcfg, BCMA_DMP_CFG_OOB))
		return (0);

	/* Fetch width of the OOB interrupt bank */
	oobw = bhnd_bus_read_4(dinfo->res_agent,
	     BCMA_DMP_OOB_OUTWIDTH(BCMA_OOB_BANK_INTR));
	if (oobw >= BCMA_OOB_NUM_SEL) {
		device_printf(bus, "ignoring invalid OOBOUTWIDTH for core %u: "
		    "%#x\n", BCMA_DINFO_COREIDX(dinfo), oobw);
		return (0);
	}

	/* Fetch OOBSEL busline values and populate list of interrupt
	 * descriptors */
	for (uint32_t sel = 0; sel < oobw; sel++) {
		struct bcma_intr	*intr;
		uint32_t		 selout;
		uint8_t			 line;

		if (dinfo->num_intrs == UINT_MAX)
			return (ENOMEM);
	
		selout = bhnd_bus_read_4(dinfo->res_agent, BCMA_DMP_OOBSELOUT(
		    BCMA_OOB_BANK_INTR, sel));

		line = (selout >> BCMA_DMP_OOBSEL_SHIFT(sel)) &
		    BCMA_DMP_OOBSEL_BUSLINE_MASK;

		intr = bcma_alloc_intr(BCMA_OOB_BANK_INTR, sel, line);
		if (intr == NULL) {
			device_printf(bus, "failed allocating interrupt "
			    "descriptor %#x for core %u\n", sel,
			    BCMA_DINFO_COREIDX(dinfo));
			return (ENOMEM);
		}

		STAILQ_INSERT_HEAD(&dinfo->intrs, intr, i_link);
		dinfo->num_intrs++;
	}

	return (0);
}

/**
 * Allocate and return a new empty device info structure.
 * 
 * @param bus The requesting bus device.
 * 
 * @retval NULL if allocation failed.
 */
struct bcma_devinfo *
bcma_alloc_dinfo(device_t bus)
{
	struct bcma_devinfo *dinfo;
	
	dinfo = malloc(sizeof(struct bcma_devinfo), M_BHND, M_NOWAIT|M_ZERO);
	if (dinfo == NULL)
		return (NULL);

	dinfo->corecfg = NULL;
	dinfo->res_agent = NULL;
	dinfo->rid_agent = -1;

	STAILQ_INIT(&dinfo->intrs);
	dinfo->num_intrs = 0;

	resource_list_init(&dinfo->resources);

	return (dinfo);
}

/**
 * Initialize a device info structure previously allocated via
 * bcma_alloc_dinfo, assuming ownership of the provided core
 * configuration.
 * 
 * @param bus The requesting bus device.
 * @param child The bcma child device.
 * @param dinfo The device info associated with @p child
 * @param corecfg Device core configuration; ownership of this value
 * will be assumed by @p dinfo.
 * 
 * @retval 0 success
 * @retval non-zero initialization failed.
 */
int
bcma_init_dinfo(device_t bus, device_t child, struct bcma_devinfo *dinfo,
    struct bcma_corecfg *corecfg)
{
	struct bcma_intr	*intr;
	int			 error;

	KASSERT(dinfo->corecfg == NULL, ("dinfo previously initialized"));

	/* Save core configuration value */
	dinfo->corecfg = corecfg;

	/* The device ports must always be initialized first to ensure that
	 * rid 0 maps to the first device port */
	bcma_dinfo_init_port_resource_info(bus, dinfo, &corecfg->dev_ports);
	bcma_dinfo_init_port_resource_info(bus, dinfo, &corecfg->bridge_ports);
	bcma_dinfo_init_port_resource_info(bus, dinfo, &corecfg->wrapper_ports);

	/* Now that we've defined the port resources, we can map the device's
	 * agent registers (if any) */
	if ((error = bcma_dinfo_init_agent(bus, child, dinfo)))
		goto failed;

	/* With agent registers mapped, we can populate the device's interrupt
	 * descriptors */
	if ((error = bcma_dinfo_init_intrs(bus, child, dinfo)))
		goto failed;

	/* Finally, map the interrupt descriptors */
	STAILQ_FOREACH(intr, &dinfo->intrs, i_link) {
		/* Already mapped? */
		if (intr->i_mapped)
			continue;

		/* Map the interrupt */
		error = BHND_BUS_MAP_INTR(bus, child, intr->i_sel,
		    &intr->i_irq);
		if (error) {
			device_printf(bus, "failed mapping interrupt line %u "
			    "for core %u: %d\n", intr->i_sel,
			    BCMA_DINFO_COREIDX(dinfo), error);
			goto failed;
		}

		intr->i_mapped = true;
	
		/* Add to resource list */
		intr->i_rid = resource_list_add_next(&dinfo->resources,
		    SYS_RES_IRQ, intr->i_irq, intr->i_irq, 1);
	}

	return (0);

failed:
	/* Owned by the caller on failure */
	dinfo->corecfg = NULL;

	return (error);
}

/**
 * Deallocate the given device info structure and any associated resources.
 * 
 * @param bus The requesting bus device.
 * @param dinfo Device info to be deallocated.
 */
void
bcma_free_dinfo(device_t bus, device_t child, struct bcma_devinfo *dinfo)
{
	struct bcma_intr *intr, *inext;

	resource_list_free(&dinfo->resources);

	if (dinfo->corecfg != NULL)
		bcma_free_corecfg(dinfo->corecfg);

	/* Release agent resource, if any */
	if (dinfo->res_agent != NULL) {
		bhnd_release_resource(bus, SYS_RES_MEMORY, dinfo->rid_agent,
		    dinfo->res_agent);
	}

	/* Clean up interrupt descriptors */
	STAILQ_FOREACH_SAFE(intr, &dinfo->intrs, i_link, inext) {
		STAILQ_REMOVE(&dinfo->intrs, intr, bcma_intr, i_link);

		/* Release our IRQ mapping */
		if (intr->i_mapped) {
			BHND_BUS_UNMAP_INTR(bus, child, intr->i_irq);
			intr->i_mapped = false;
		}

		bcma_free_intr(intr);
	}

	free(dinfo, M_BHND);
}


/**
 * Allocate and initialize a new interrupt descriptor.
 * 
 * @param bank OOB bank.
 * @param sel OOB selector.
 * @param line OOB bus line.
 */
struct bcma_intr *
bcma_alloc_intr(uint8_t bank, uint8_t sel, uint8_t line)
{
	struct bcma_intr *intr;

	if (bank >= BCMA_OOB_NUM_BANKS)
		return (NULL);

	if (sel >= BCMA_OOB_NUM_SEL)
		return (NULL);

	if (line >= BCMA_OOB_NUM_BUSLINES)
		return (NULL);

	intr = malloc(sizeof(*intr), M_BHND, M_NOWAIT);
	if (intr == NULL)
		return (NULL);

	intr->i_bank = bank;
	intr->i_sel = sel;
	intr->i_busline = line;
	intr->i_mapped = false;
	intr->i_irq = 0;

	return (intr);
}

/**
 * Deallocate all resources associated with the given interrupt descriptor.
 * 
 * @param intr Interrupt descriptor to be deallocated.
 */
void
bcma_free_intr(struct bcma_intr *intr)
{
	KASSERT(!intr->i_mapped, ("interrupt %u still mapped", intr->i_sel));

	free(intr, M_BHND);
}

/**
 * Allocate and initialize new slave port descriptor.
 * 
 * @param port_num Per-core port number.
 * @param port_type Port type.
 */
struct bcma_sport *
bcma_alloc_sport(bcma_pid_t port_num, bhnd_port_type port_type)
{
	struct bcma_sport *sport;
	
	sport = malloc(sizeof(struct bcma_sport), M_BHND, M_NOWAIT);
	if (sport == NULL)
		return NULL;
	
	sport->sp_num = port_num;
	sport->sp_type = port_type;
	sport->sp_num_maps = 0;
	STAILQ_INIT(&sport->sp_maps);

	return sport;
}

/**
 * Deallocate all resources associated with the given port descriptor.
 * 
 * @param sport Port descriptor to be deallocated.
 */
void
bcma_free_sport(struct bcma_sport *sport) {
	struct bcma_map *map, *mapnext;

	STAILQ_FOREACH_SAFE(map, &sport->sp_maps, m_link, mapnext) {
		free(map, M_BHND);
	}

	free(sport, M_BHND);
}


/**
 * Given a bcma(4) child's device info, spin waiting for the device's DMP
 * resetstatus register to clear.
 * 
 * @param child The bcma(4) child device.
 * @param dinfo The @p child device info.
 * 
 * @retval 0 success
 * @retval ENODEV if @p dinfo does not map an agent register resource.
 * @retval ETIMEDOUT if timeout occurs
 */
int
bcma_dmp_wait_reset(device_t child, struct bcma_devinfo *dinfo)
{
	uint32_t rst;

	if (dinfo->res_agent == NULL)
		return (ENODEV);

	/* 300us should be long enough, but there are references to this
	 * requiring up to 10ms when performing reset of an 80211 core
	 * after a MAC PSM microcode watchdog event. */
	for (int i = 0; i < 10000; i += 10) {
		rst = bhnd_bus_read_4(dinfo->res_agent, BCMA_DMP_RESETSTATUS);
		if (rst == 0)
			return (0);

		DELAY(10);
	}

	device_printf(child, "BCMA_DMP_RESETSTATUS timeout\n");
	return (ETIMEDOUT);
}

/**
 * Set the bcma(4) child's DMP resetctrl register value, and then wait
 * for all backplane operations to complete.
 * 
 * @param child The bcma(4) child device.
 * @param dinfo The @p child device info.
 * @param value The new ioctrl value to set.
 * 
 * @retval 0 success
 * @retval ENODEV if @p dinfo does not map an agent register resource.
 * @retval ETIMEDOUT if timeout occurs waiting for reset completion
 */
int
bcma_dmp_write_reset(device_t child, struct bcma_devinfo *dinfo, uint32_t value)
{
	uint32_t rst;

	if (dinfo->res_agent == NULL)
		return (ENODEV);

	/* Already in requested reset state? */
	rst = bhnd_bus_read_4(dinfo->res_agent, BCMA_DMP_RESETCTRL);
	if (rst == value)
		return (0);

	bhnd_bus_write_4(dinfo->res_agent, BCMA_DMP_RESETCTRL, value);
	bhnd_bus_read_4(dinfo->res_agent, BCMA_DMP_RESETCTRL); /* read-back */
	DELAY(10);

	return (bcma_dmp_wait_reset(child, dinfo));
}
