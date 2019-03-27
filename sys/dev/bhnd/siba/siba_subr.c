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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/limits.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/bhnd/bhndvar.h>

#include "sibareg.h"
#include "sibavar.h"

static int	siba_register_interrupts(device_t dev, device_t child,
		    struct siba_devinfo *dinfo);
static int	siba_append_dinfo_region(struct siba_devinfo *dinfo,
		     uint8_t addridx, uint32_t base, uint32_t size,
		     uint32_t bus_reserved);

/**
 * Map a siba(4) OCP vendor code to its corresponding JEDEC JEP-106 vendor
 * code.
 * 
 * @param ocp_vendor An OCP vendor code.
 * @return The BHND_MFGID constant corresponding to @p ocp_vendor, or
 * BHND_MFGID_INVALID if the OCP vendor is unknown.
 */
uint16_t
siba_get_bhnd_mfgid(uint16_t ocp_vendor)
{
	switch (ocp_vendor) {
	case OCP_VENDOR_BCM:
		return (BHND_MFGID_BCM);
	default:
		return (BHND_MFGID_INVALID);
	}
}

/**
 * Allocate and return a new empty device info structure.
 * 
 * @param bus The requesting bus device.
 * 
 * @retval NULL if allocation failed.
 */
struct siba_devinfo *
siba_alloc_dinfo(device_t bus)
{
	struct siba_devinfo *dinfo;
	
	dinfo = malloc(sizeof(struct siba_devinfo), M_BHND, M_NOWAIT|M_ZERO);
	if (dinfo == NULL)
		return NULL;

	for (u_int i = 0; i < nitems(dinfo->cfg); i++) {
		dinfo->cfg[i] = ((struct siba_cfg_block){
			.cb_base = 0,
			.cb_size = 0,
			.cb_rid = -1,
		});
		dinfo->cfg_res[i] = NULL;
		dinfo->cfg_rid[i] = -1;
	}

	resource_list_init(&dinfo->resources);

	dinfo->pmu_state = SIBA_PMU_NONE;

	dinfo->intr = (struct siba_intr) {
		.mapped = false,
		.rid = -1
	};

	return dinfo;
}

/**
 * Initialize a device info structure previously allocated via
 * siba_alloc_dinfo, copying the provided core id.
 * 
 * @param dev The requesting bus device.
 * @param child The siba child device.
 * @param dinfo The device info instance.
 * @param core Device core info.
 * 
 * @retval 0 success
 * @retval non-zero initialization failed.
 */
int
siba_init_dinfo(device_t dev, device_t child, struct siba_devinfo *dinfo,
    const struct siba_core_id *core_id)
{
	int error;

	dinfo->core_id = *core_id;

	/* Register all address space mappings */
	for (uint8_t i = 0; i < core_id->num_admatch; i++) {
		uint32_t bus_reserved;

		/* If this is the device's core/enumeration addrespace,
		 * reserve the Sonics configuration register blocks for the
		 * use of our bus. */
		bus_reserved = 0;
		if (i == SIBA_CORE_ADDRSPACE)
			bus_reserved = core_id->num_cfg_blocks * SIBA_CFG_SIZE;

		/* Append the region info */
		error = siba_append_dinfo_region(dinfo, i,
		    core_id->admatch[i].am_base, core_id->admatch[i].am_size,
		    bus_reserved);
		if (error)
			return (error);
	}

	/* Register all interrupt(s) */
	if ((error = siba_register_interrupts(dev, child, dinfo)))
		return (error);

	return (0);
}


/**
 * Register and map all interrupts for @p dinfo.
 *
 * @param dev The siba bus device.
 * @param child The siba child device.
 * @param dinfo The device info instance on which to register all interrupt
 * entries.
 */
static int
siba_register_interrupts(device_t dev, device_t child,
     struct siba_devinfo *dinfo)
{
	int error;

	/* Is backplane interrupt distribution enabled for this core? */
	if (!dinfo->core_id.intr_en)
		return (0);

	/* Have one interrupt */
	dinfo->intr.mapped = false;
	dinfo->intr.irq = 0;
	dinfo->intr.rid = -1;

	/* Map the interrupt */
	error = BHND_BUS_MAP_INTR(dev, child, 0 /* single intr is always 0 */,
	    &dinfo->intr.irq);
	if (error) {
		device_printf(dev, "failed mapping interrupt line for core %u: "
		    "%d\n", dinfo->core_id.core_info.core_idx, error);
		return (error);
	}
	dinfo->intr.mapped = true;

	/* Update the resource list */
	dinfo->intr.rid = resource_list_add_next(&dinfo->resources, SYS_RES_IRQ,
	    dinfo->intr.irq, dinfo->intr.irq, 1);

	return (0);
}

/**
 * Map an addrspace index to its corresponding bhnd(4) BHND_PORT_DEVICE port
 * number.
 * 
 * @param addrspace Address space index.
 */
u_int
siba_addrspace_device_port(u_int addrspace)
{
	/* The first addrspace is always mapped to device0; the remainder
	 * are mapped to device1 */
	if (addrspace == 0)
		return (0);
	else
		return (1);
}

/**
 * Map an addrspace index to its corresponding bhnd(4) BHND_PORT_DEVICE port
 * region number.
 * 
 * @param addrspace Address space index.
 */
u_int
siba_addrspace_device_region(u_int addrspace)
{
	/* The first addrspace is always mapped to device0.0; the remainder
	 * are mapped to device1.0 + (n - 1) */
	if (addrspace == 0)
		return (0);
	else
		return (addrspace - 1);
}

/**
 * Map an config block index to its corresponding bhnd(4) BHND_PORT_AGENT port
 * number.
 * 
 * @param cfg Config block index.
 */
u_int
siba_cfg_agent_port(u_int cfg)
{
	/* Always agent0 */
	return (0);
}

/**
 * Map an config block index to its corresponding bhnd(4) BHND_PORT_AGENT port
 * region number.
 * 
 * @param cfg Config block index.
 */
u_int
siba_cfg_agent_region(u_int cfg)
{
	/* Always agent0.<idx> */
	return (cfg);
}

/**
 * Return the number of bhnd(4) ports to advertise for the given
 * @p core_id and @p port_type.
 * 
 * Refer to the siba_addrspace_index() and siba_cfg_index() functions for
 * information on siba's mapping of bhnd(4) port and region identifiers.
 * 
 * @param core_id The siba core info.
 * @param port_type The bhnd(4) port type.
 */
u_int
siba_port_count(struct siba_core_id *core_id, bhnd_port_type port_type)
{
	switch (port_type) {
	case BHND_PORT_DEVICE:
		/* 0, 1, or 2 ports */
		return (min(core_id->num_admatch, 2));

	case BHND_PORT_AGENT:
		/* One agent port maps all configuration blocks */
		if (core_id->num_cfg_blocks > 0)
			return (1);

		/* Do not advertise an agent port if there are no configuration
		 * register blocks */
		return (0);

	default:
		return (0);
	}
}

/**
 * Return true if @p port of @p port_type is defined by @p core_id, false
 * otherwise.
 * 
 * @param core_id The siba core info.
 * @param port_type The bhnd(4) port type.
 * @param port The bhnd(4) port number.
 */
bool
siba_is_port_valid(struct siba_core_id *core_id, bhnd_port_type port_type,
    u_int port)
{
	/* Verify the index against the port count */
	if (siba_port_count(core_id, port_type) <= port)
		return (false);

	return (true);
}

/**
 * Return the number of bhnd(4) regions to advertise for @p core_id on the
 * @p port of @p port_type.
 * 
 * @param core_id The siba core info.
 * @param port_type The bhnd(4) port type.
 */
u_int
siba_port_region_count(struct siba_core_id *core_id, bhnd_port_type port_type,
    u_int port)
{
	/* The port must exist */
	if (!siba_is_port_valid(core_id, port_type, port))
		return (0);

	switch (port_type) {
	case BHND_PORT_DEVICE:
		/* The first address space, if any, is mapped to device0.0 */
		if (port == 0)
			return (min(core_id->num_admatch, 1));

		/* All remaining address spaces are mapped to device0.(n - 1) */
		if (port == 1 && core_id->num_admatch >= 2)
			return (core_id->num_admatch - 1);

		break;

	case BHND_PORT_AGENT:
		/* All config blocks are mapped to a single port */
		if (port == 0)
			return (core_id->num_cfg_blocks);

		break;

	default:
		break;
	}

	/* Validated above */
	panic("siba_is_port_valid() returned true for unknown %s.%u port",
	    bhnd_port_type_name(port_type), port);

}

/**
 * Map a bhnd(4) type/port/region triplet to its associated config block index,
 * if any.
 * 
 * We map config registers to port/region identifiers as follows:
 * 
 * 	[port].[region]	[cfg register block]
 * 	agent0.0	0
 * 	agent0.1	1
 * 
 * @param port_type The bhnd(4) port type.
 * @param port The bhnd(4) port number.
 * @param region The bhnd(4) port region.
 * @param addridx On success, the corresponding addrspace index.
 * 
 * @retval 0 success
 * @retval ENOENT if the given type/port/region cannot be mapped to a
 * siba config register block.
 */
int
siba_cfg_index(struct siba_core_id *core_id, bhnd_port_type port_type,
    u_int port, u_int region, u_int *cfgidx)
{
	/* Config blocks are mapped to agent ports */
	if (port_type != BHND_PORT_AGENT)
		return (ENOENT);

	/* Port must be valid */
	if (!siba_is_port_valid(core_id, port_type, port))
		return (ENOENT);

	if (region >= core_id->num_cfg_blocks)
		return (ENOENT);

	if (region >= SIBA_MAX_CFG)
		return (ENOENT);

	/* Found */
	*cfgidx = region;
	return (0);
}

/**
 * Map an bhnd(4) type/port/region triplet to its associated config block
 * entry, if any.
 *
 * The only supported port type is BHND_PORT_DEVICE.
 * 
 * @param dinfo The device info to search for a matching address space.
 * @param type The bhnd(4) port type.
 * @param port The bhnd(4) port number.
 * @param region The bhnd(4) port region.
 */
struct siba_cfg_block *
siba_find_cfg_block(struct siba_devinfo *dinfo, bhnd_port_type type, u_int port,
    u_int region)
{
	u_int	cfgidx;
	int	error;

	/* Map to addrspace index */
	error = siba_cfg_index(&dinfo->core_id, type, port, region, &cfgidx);
	if (error)
		return (NULL);

	/* Found */
	return (&dinfo->cfg[cfgidx]);
}

/**
 * Map a bhnd(4) type/port/region triplet to its associated address space
 * index, if any.
 * 
 * For compatibility with bcma(4), we map address spaces to port/region
 * identifiers as follows:
 * 
 * 	[port.region]	[admatch index]
 * 	device0.0	0
 * 	device1.0	1
 * 	device1.1	2
 * 	device1.2	3
 * 
 * @param core_id The siba core info.
 * @param port_type The bhnd(4) port type.
 * @param port The bhnd(4) port number.
 * @param region The bhnd(4) port region.
 * @param addridx On success, the corresponding addrspace index.
 * 
 * @retval 0 success
 * @retval ENOENT if the given type/port/region cannot be mapped to a
 * siba address space.
 */
int
siba_addrspace_index(struct siba_core_id *core_id, bhnd_port_type port_type,
    u_int port, u_int region, u_int *addridx)
{
	u_int idx;

	/* Address spaces are always device ports */
	if (port_type != BHND_PORT_DEVICE)
		return (ENOENT);

	/* Port must be valid */
	if (!siba_is_port_valid(core_id, port_type, port))
		return (ENOENT);
	
	if (port == 0)
		idx = region;
	else if (port == 1)
		idx = region + 1;
	else
		return (ENOENT);

	if (idx >= core_id->num_admatch)
		return (ENOENT);

	/* Found */
	*addridx = idx;
	return (0);
}

/**
 * Map an bhnd(4) type/port/region triplet to its associated address space
 * entry, if any.
 *
 * The only supported port type is BHND_PORT_DEVICE.
 * 
 * @param dinfo The device info to search for a matching address space.
 * @param type The bhnd(4) port type.
 * @param port The bhnd(4) port number.
 * @param region The bhnd(4) port region.
 */
struct siba_addrspace *
siba_find_addrspace(struct siba_devinfo *dinfo, bhnd_port_type type, u_int port,
    u_int region)
{
	u_int	addridx;
	int	error;

	/* Map to addrspace index */
	error = siba_addrspace_index(&dinfo->core_id, type, port, region,
	    &addridx);
	if (error)
		return (NULL);

	/* Found */
	if (addridx >= SIBA_MAX_ADDRSPACE)
		return (NULL);

	return (&dinfo->addrspace[addridx]);
}

/**
 * Append an address space entry to @p dinfo.
 * 
 * @param dinfo The device info entry to update.
 * @param addridx The address space index.
 * @param base The mapping's base address.
 * @param size The mapping size.
 * @param bus_reserved Number of bytes to reserve in @p size for bus use
 * when registering the resource list entry. This is used to reserve bus
 * access to the core's SIBA_CFG* register blocks.
 * 
 * @retval 0 success
 * @retval non-zero An error occurred appending the entry.
 */
static int
siba_append_dinfo_region(struct siba_devinfo *dinfo, uint8_t addridx,
    uint32_t base, uint32_t size, uint32_t bus_reserved)
{
	struct siba_addrspace	*sa;
	rman_res_t		 r_size;

	/* Verify that base + size will not overflow */
	if (size > 0 && UINT32_MAX - (size - 1) < base)
		return (ERANGE);

	/* Verify that size - bus_reserved will not underflow */
	if (size < bus_reserved)
		return (ERANGE);

	/* Must not be 0-length */
	if (size == 0)
		return (EINVAL);

	/* Must not exceed addrspace array size */
	if (addridx >= nitems(dinfo->addrspace))
		return (EINVAL);

	/* Initialize new addrspace entry */
	sa = &dinfo->addrspace[addridx];
	sa->sa_base = base;
	sa->sa_size = size;
	sa->sa_bus_reserved = bus_reserved;

	/* Populate the resource list */
	r_size = size - bus_reserved;
	sa->sa_rid = resource_list_add_next(&dinfo->resources, SYS_RES_MEMORY,
	    base, base + (r_size - 1), r_size);

	return (0);
}

/**
 * Deallocate the given device info structure and any associated resources.
 * 
 * @param dev The requesting bus device.
 * @param child The siba child device.
 * @param dinfo Device info associated with @p child to be deallocated.
 */
void
siba_free_dinfo(device_t dev, device_t child, struct siba_devinfo *dinfo)
{
	resource_list_free(&dinfo->resources);

	/* Free all mapped configuration blocks */
	for (u_int i = 0; i < nitems(dinfo->cfg); i++) {
		if (dinfo->cfg_res[i] == NULL)
			continue;

		bhnd_release_resource(dev, SYS_RES_MEMORY, dinfo->cfg_rid[i],
		    dinfo->cfg_res[i]);

		dinfo->cfg_res[i] = NULL;
		dinfo->cfg_rid[i] = -1;
	}

	/* Unmap the core's interrupt */
	if (dinfo->core_id.intr_en && dinfo->intr.mapped) {
		BHND_BUS_UNMAP_INTR(dev, child, dinfo->intr.irq);
		dinfo->intr.mapped = false;
	}

	free(dinfo, M_BHND);
}

/**
 * Return the core-enumeration-relative offset for the @p addrspace
 * SIBA_R0_ADMATCH* register.
 * 
 * @param addrspace The address space index.
 * 
 * @retval non-zero success
 * @retval 0 the given @p addrspace index is not supported.
 */
u_int
siba_admatch_offset(uint8_t addrspace)
{
	switch (addrspace) {
	case 0:
		return SB0_REG_ABS(SIBA_CFG0_ADMATCH0);
	case 1:
		return SB0_REG_ABS(SIBA_CFG0_ADMATCH1);
	case 2:
		return SB0_REG_ABS(SIBA_CFG0_ADMATCH2);
	case 3:
		return SB0_REG_ABS(SIBA_CFG0_ADMATCH3);
	default:
		return (0);
	}
}

/**
 * Parse a SIBA_R0_ADMATCH* register.
 * 
 * @param addrspace The address space index.
 * @param am The address match register value to be parsed.
 * @param[out] admatch The parsed address match descriptor
 * 
 * @retval 0 success
 * @retval non-zero a parse error occurred.
 */
int
siba_parse_admatch(uint32_t am, struct siba_admatch *admatch)
{
	u_int am_type;
	
	/* Extract the base address and size */
	am_type = SIBA_REG_GET(am, AM_TYPE);
	switch (am_type) {
	case 0:
		/* Type 0 entries are always enabled, and do not support
		 * negative matching */
		admatch->am_base = am & SIBA_AM_BASE0_MASK;
		admatch->am_size = 1 << (SIBA_REG_GET(am, AM_ADINT0) + 1);
		admatch->am_enabled = true;
		admatch->am_negative = false;
		break;
	case 1:
		admatch->am_base = am & SIBA_AM_BASE1_MASK;
		admatch->am_size = 1 << (SIBA_REG_GET(am, AM_ADINT1) + 1);
		admatch->am_enabled = ((am & SIBA_AM_ADEN) != 0);
		admatch->am_negative = ((am & SIBA_AM_ADNEG) != 0);
		break;
	case 2:
		admatch->am_base = am & SIBA_AM_BASE2_MASK;
		admatch->am_size = 1 << (SIBA_REG_GET(am, AM_ADINT2) + 1);
		admatch->am_enabled = ((am & SIBA_AM_ADEN) != 0);
		admatch->am_negative = ((am & SIBA_AM_ADNEG) != 0);
		break;
	default:
		return (EINVAL);
	}

	return (0);
}

/**
 * Write @p value to @p dev's CFG0 target/initiator state register, performing
 * required read-back and waiting for completion.
 * 
 * @param dev The siba(4) child device.
 * @param reg The CFG0 state register to write (e.g. SIBA_CFG0_TMSTATELOW,
 * SIBA_CFG0_IMSTATE)
 * @param value The value to write to @p reg.
 * @param mask The mask of bits to be included from @p value.
 */
void
siba_write_target_state(device_t dev, struct siba_devinfo *dinfo,
    bus_size_t reg, uint32_t value, uint32_t mask)
{
	struct bhnd_resource	*r;
	uint32_t		 rval;

	r = dinfo->cfg_res[0];

	KASSERT(r != NULL, ("%s missing CFG0 mapping",
	    device_get_nameunit(dev)));
	KASSERT(reg <= SIBA_CFG_SIZE-4, ("%s invalid CFG0 register offset %#jx",
	    device_get_nameunit(dev), (uintmax_t)reg));

	rval = bhnd_bus_read_4(r, reg);
	rval &= ~mask;
	rval |= (value & mask);

	bhnd_bus_write_4(r, reg, rval);
	bhnd_bus_read_4(r, reg); /* read-back */
	DELAY(1);
}

/**
 * Spin for up to @p usec waiting for @p dev's CFG0 target/initiator state
 * register value to be equal to @p value after applying @p mask bits to both
 * values.
 * 
 * @param dev The siba(4) child device to wait on.
 * @param dinfo The @p dev's device info
 * @param reg The state register to read (e.g. SIBA_CFG0_TMSTATEHIGH,
 * SIBA_CFG0_IMSTATE)
 * @param value The value against which @p reg will be compared.
 * @param mask The mask to be applied when comparing @p value with @p reg.
 * @param usec The maximum number of microseconds to wait for completion.
 * 
 * @retval 0 if SIBA_TMH_BUSY is cleared prior to the @p usec timeout.
 * @retval ENODEV if SIBA_CFG0 is not mapped by @p dinfo.
 * @retval ETIMEDOUT if a timeout occurs.
 */
int
siba_wait_target_state(device_t dev, struct siba_devinfo *dinfo, bus_size_t reg,
    uint32_t value, uint32_t mask, u_int usec)
{
	struct bhnd_resource	*r;
	uint32_t		 rval;

	if ((r = dinfo->cfg_res[0]) == NULL)
		return (ENODEV);

	value &= mask;
	for (int i = 0; i < usec; i += 10) {
		rval = bhnd_bus_read_4(r, reg);
		if ((rval & mask) == value)
			return (0);

		DELAY(10);
	}

	return (ETIMEDOUT);
}
