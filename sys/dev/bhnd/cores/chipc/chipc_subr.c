/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2016 Michael Zhilin <mizhka@gmail.com>
 * Copyright (c) 2015-2016 Landon Fuller <landon@landonf.org>
 * All rights reserved.
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

#include "chipc_private.h"
#include "chipcvar.h"

/**
 * Return a human-readable name for the given flash @p type.
 */
const char *
chipc_flash_name(chipc_flash type)
{
	switch (type) {
	case CHIPC_PFLASH_CFI:
		return ("CFI Flash");

	case CHIPC_SFLASH_ST:
	case CHIPC_SFLASH_AT:
		return ("SPI Flash");

	case CHIPC_QSFLASH_ST:
	case CHIPC_QSFLASH_AT:
		return ("QSPI Flash");

	case CHIPC_NFLASH:
	case CHIPC_NFLASH_4706:
		return ("NAND");

	case CHIPC_FLASH_NONE:
	default:
		return ("unknown");
	}
}

/**
 * Return the name of the bus device class used by flash @p type,
 * or NULL if @p type is unsupported.
 */
const char *
chipc_flash_bus_name(chipc_flash type)
{
	switch (type) {
	case CHIPC_PFLASH_CFI:
		return ("cfi");

	case CHIPC_SFLASH_ST:
	case CHIPC_SFLASH_AT:
		return ("spi");

	case CHIPC_QSFLASH_ST:
	case CHIPC_QSFLASH_AT:
		/* unimplemented; spi? */
		return (NULL);

	case CHIPC_NFLASH:
	case CHIPC_NFLASH_4706:
		/* unimplemented; nandbus? */
		return (NULL);

	case CHIPC_FLASH_NONE:
	default:
		return (NULL);
	}
}

/**
 * Return the name of the flash device class for SPI flash @p type,
 * or NULL if @p type does not use SPI, or is unsupported.
 */
const char *
chipc_sflash_device_name(chipc_flash type)
{
	switch (type) {
	case CHIPC_SFLASH_ST:
		return ("mx25l");

	case CHIPC_SFLASH_AT:
		return ("at45d");

	case CHIPC_QSFLASH_ST:
	case CHIPC_QSFLASH_AT:
		/* unimplemented */
		return (NULL);

	case CHIPC_PFLASH_CFI:
	case CHIPC_NFLASH:
	case CHIPC_NFLASH_4706:
	case CHIPC_FLASH_NONE:
	default:
		return (NULL);
	}
}

/**
 * Initialize child resource @p r with a virtual address, tag, and handle
 * copied from @p parent, adjusted to contain only the range defined by
 * @p offsize and @p size.
 * 
 * @param r The register to be initialized.
 * @param parent The parent bus resource that fully contains the subregion.
 * @param offset The subregion offset within @p parent.
 * @param size The subregion size.
 */
int
chipc_init_child_resource(struct resource *r,
    struct resource *parent, bhnd_size_t offset, bhnd_size_t size)
{
	bus_space_handle_t	bh, child_bh;
	bus_space_tag_t		bt;
	uintptr_t		vaddr;
	int			error;

	/* Fetch the parent resource's bus values */
	vaddr = (uintptr_t) rman_get_virtual(parent);
	bt = rman_get_bustag(parent);
	bh = rman_get_bushandle(parent);

	/* Configure child resource with offset-adjusted values */
	vaddr += offset;
	error = bus_space_subregion(bt, bh, offset, size, &child_bh);
	if (error)
		return (error);

	rman_set_virtual(r, (void *) vaddr);
	rman_set_bustag(r, bt);
	rman_set_bushandle(r, child_bh);

	return (0);
}

/**
 * Map an interrupt line to an IRQ, and then register a corresponding SYS_RES_IRQ
 * with @p child's resource list.
 *
 * @param sc chipc driver state.
 * @param child The device to set the resource on.
 * @param rid The resource ID.
 * @param intr The interrupt line to be mapped.
 * @param count The length of the resource.
 * @param port The mapping port number (ignored if not SYS_RES_MEMORY).
 * @param region The mapping region number (ignored if not SYS_RES_MEMORY).
 */
int
chipc_set_irq_resource(struct chipc_softc *sc, device_t child, int rid,
    u_int intr)
{
	struct chipc_devinfo	*dinfo;
	int			 error;

	KASSERT(device_get_parent(child) == sc->dev, ("not a direct child"));
	dinfo = device_get_ivars(child);

	/* We currently only support a single IRQ mapping */
	if (dinfo->irq_mapped) {
		device_printf(sc->dev, "irq already mapped for child\n");
		return (ENOMEM);
	}

	/* Map the IRQ */
	if ((error = bhnd_map_intr(sc->dev, intr, &dinfo->irq))) {
		device_printf(sc->dev, "failed to map intr %u: %d\n", intr,
		    error);
		return (error);
	}

	dinfo->irq_mapped = true;

	/* Add to child's resource list */
	error = bus_set_resource(child, SYS_RES_IRQ, rid, dinfo->irq, 1);
	if (error) {
		device_printf(sc->dev, "failed to set child irq resource %d to "
		    "%ju: %d\n", rid, dinfo->irq, error);

		bhnd_unmap_intr(sc->dev, dinfo->irq);
		return (error);
	}

	return (0);
}


/**
 * Add a SYS_RES_MEMORY resource with a given resource ID, relative to the
 * given port and region, to @p child's resource list.
 * 
 * The specified @p region's address and size will be fetched from the bhnd(4)
 * bus, and bus_set_resource() will be called with @p start added the region's
 * actual base address.
 * 
 * To use the default region values for @p start and @p count, specify
 * a @p start value of 0ul, and an end value of RMAN_MAX_END
 * 
 * @param sc chipc driver state.
 * @param child The device to set the resource on.
 * @param rid The resource ID.
 * @param start The resource start address (if SYS_RES_MEMORY, this is
 * relative to @p region's base address).
 * @param count The length of the resource.
 * @param port The mapping port number (ignored if not SYS_RES_MEMORY).
 * @param region The mapping region number (ignored if not SYS_RES_MEMORY).
 */
int
chipc_set_mem_resource(struct chipc_softc *sc, device_t child, int rid,
    rman_res_t start, rman_res_t count, u_int port, u_int region)
{
	bhnd_addr_t	region_addr;
	bhnd_size_t	region_size;
	bool		isdefault;
	int		error;

	KASSERT(device_get_parent(child) == sc->dev, ("not a direct child"));
	isdefault = RMAN_IS_DEFAULT_RANGE(start, count);

	/* Fetch region address and size */
	error = bhnd_get_region_addr(sc->dev, BHND_PORT_DEVICE, port,
	    region, &region_addr, &region_size);
	if (error) {
		device_printf(sc->dev,
		    "lookup of %s%u.%u failed: %d\n",
		    bhnd_port_type_name(BHND_PORT_DEVICE), port, region, error);
		return (error);
	}

	/* Populate defaults */
	if (isdefault) {
		start = 0;
		count = region_size;
	}

	/* Verify requested range is mappable */
	if (start > region_size || region_size - start < count) {
		device_printf(sc->dev,
		    "%s%u.%u region cannot map requested range %#jx+%#jx\n",
		    bhnd_port_type_name(BHND_PORT_DEVICE), port, region, start,
		    count);
		return (ERANGE);
	}

	return (bus_set_resource(child, SYS_RES_MEMORY, rid,
	    region_addr + start, count));
}


/*
 * Print a capability structure.
 */
void
chipc_print_caps(device_t dev, struct chipc_caps *caps)
{
#define CC_TFS(_flag) (caps->_flag ? "yes" : "no")

	device_printf(dev, "MIPSEB:  %-3s   | BP64:  %s\n",
	    CC_TFS(mipseb), CC_TFS(backplane_64));
	device_printf(dev, "UARTs:   %-3hhu   | UGPIO: %s\n",
	    caps->num_uarts, CC_TFS(uart_gpio));
	// XXX: hitting a kvprintf bug with '%#02x' not prefixing '0x' in
	// some cases, and not apply the field width in others
	device_printf(dev, "UARTClk: 0x%02x  | Flash: %u\n",
	    caps->uart_clock, caps->flash_type);
	device_printf(dev, "SPROM:   %-3s   | OTP:   %s\n",
	    CC_TFS(sprom), CC_TFS(otp_size));
	device_printf(dev, "CFIsz:   0x%02x  | OTPsz: 0x%02x\n",
	    caps->cfi_width, caps->otp_size);
	device_printf(dev, "ExtBus:  0x%02x  | PwrCtrl: %s\n",
	    caps->extbus_type, CC_TFS(pwr_ctrl));
	device_printf(dev, "PLL:     0x%02x  | JTAGM: %s\n",
	    caps->pll_type, CC_TFS(jtag_master));
	device_printf(dev, "PMU:     %-3s   | ECI:   %s\n",
	    CC_TFS(pmu), CC_TFS(eci));
	device_printf(dev, "SECI:    %-3s   | GSIO:  %s\n",
	    CC_TFS(seci), CC_TFS(gsio));
	device_printf(dev, "AOB:     %-3s   | BootROM: %s\n",
	    CC_TFS(aob), CC_TFS(boot_rom));

#undef CC_TFS
}

/**
 * Allocate and initialize new region record.
 * 
 * @param sc Driver instance state.
 * @param type The port type to query.
 * @param port The port number to query.
 * @param region The region number to query.
 */
struct chipc_region *
chipc_alloc_region(struct chipc_softc *sc, bhnd_port_type type,
    u_int port, u_int region)
{
	struct chipc_region	*cr;
	int			 error;

	/* Don't bother allocating a chipc_region if init will fail */
	if (!bhnd_is_region_valid(sc->dev, type, port, region))
		return (NULL);

	/* Allocate and initialize region info */
	cr = malloc(sizeof(*cr), M_BHND, M_NOWAIT);
	if (cr == NULL)
		return (NULL);

	cr->cr_port_type = type;
	cr->cr_port_num = port;
	cr->cr_region_num = region;
	cr->cr_res = NULL;
	cr->cr_refs = 0;
	cr->cr_act_refs = 0;

	error = bhnd_get_region_addr(sc->dev, type, port, region, &cr->cr_addr,
	    &cr->cr_count);
	if (error) {
		device_printf(sc->dev,
		    "fetching chipc region address failed: %d\n", error);
		goto failed;
	}

	cr->cr_end = cr->cr_addr + cr->cr_count - 1;

	/* Fetch default resource ID for this region. Not all regions have an
	 * assigned rid, in which case this will return -1 */
	cr->cr_rid = bhnd_get_port_rid(sc->dev, type, port, region);

	return (cr);

failed:
	device_printf(sc->dev, "chipc region alloc failed for %s%u.%u\n",
	    bhnd_port_type_name(type), port, region);
	free(cr, M_BHND);
	return (NULL);
}

/**
 * Deallocate the given region record and its associated resource, if any.
 *
 * @param sc Driver instance state.
 * @param cr Region record to be deallocated.
 */
void
chipc_free_region(struct chipc_softc *sc, struct chipc_region *cr)
{
	KASSERT(cr->cr_refs == 0,
	    ("chipc %s%u.%u region has %u active references",
	     bhnd_port_type_name(cr->cr_port_type), cr->cr_port_num,
	     cr->cr_region_num, cr->cr_refs));

	if (cr->cr_res != NULL) {
		bhnd_release_resource(sc->dev, SYS_RES_MEMORY, cr->cr_res_rid,
		    cr->cr_res);
	}

	free(cr, M_BHND);
}

/**
 * Locate the region mapping the given range, if any. Returns NULL if no
 * valid region is found.
 * 
 * @param sc Driver instance state.
 * @param start start of address range.
 * @param end end of address range.
 */
struct chipc_region *
chipc_find_region(struct chipc_softc *sc, rman_res_t start, rman_res_t end)
{
	struct chipc_region *cr;

	if (start > end)
		return (NULL);

	STAILQ_FOREACH(cr, &sc->mem_regions, cr_link) {
		if (start < cr->cr_addr || end > cr->cr_end)
			continue;

		/* Found */
		return (cr);
	}

	/* Not found */
	return (NULL);
}

/**
 * Locate a region mapping by its bhnd-assigned resource id (as returned by
 * bhnd_get_port_rid).
 * 
 * @param sc Driver instance state.
 * @param rid Resource ID to query for.
 */
struct chipc_region *
chipc_find_region_by_rid(struct chipc_softc *sc, int rid)
{
	struct chipc_region	*cr;
	int			 port_rid;

	STAILQ_FOREACH(cr, &sc->mem_regions, cr_link) {
		port_rid = bhnd_get_port_rid(sc->dev, cr->cr_port_type,
		    cr->cr_port_num, cr->cr_region_num);
		if (port_rid == -1 || port_rid != rid)
			continue;

		/* Found */
		return (cr);
	}

	/* Not found */
	return (NULL);
}

/**
 * Retain a reference to a chipc_region, allocating and activating the
 * backing resource as required.
 * 
 * @param sc chipc driver instance state
 * @param cr region to retain.
 * @param flags specify RF_ALLOCATED to retain an allocation reference,
 * RF_ACTIVE to retain an activation reference.
 */
int
chipc_retain_region(struct chipc_softc *sc, struct chipc_region *cr, int flags)
{
	int error;

	KASSERT(!(flags &~ (RF_ACTIVE|RF_ALLOCATED)), ("unsupported flags"));

	CHIPC_LOCK(sc);

	/* Handle allocation */
	if (flags & RF_ALLOCATED) {
		/* If this is the first reference, allocate the resource */
		if (cr->cr_refs == 0) {
			KASSERT(cr->cr_res == NULL,
			    ("non-NULL resource has refcount"));

			/* Fetch initial resource ID */			
			if ((cr->cr_res_rid = cr->cr_rid) == -1) {
				CHIPC_UNLOCK(sc);
				return (EINVAL);
			}

			/* Allocate resource */
			cr->cr_res = bhnd_alloc_resource(sc->dev,
			    SYS_RES_MEMORY, &cr->cr_res_rid, cr->cr_addr,
			    cr->cr_end, cr->cr_count, RF_SHAREABLE);
			if (cr->cr_res == NULL) {
				CHIPC_UNLOCK(sc);
				return (ENXIO);
			}
		}
		
		/* Increment allocation refcount */
		cr->cr_refs++;
	}


	/* Handle activation */
	if (flags & RF_ACTIVE) {
		KASSERT(cr->cr_refs > 0,
		    ("cannot activate unallocated resource"));

		/* If this is the first reference, activate the resource */
		if (cr->cr_act_refs == 0) {
			error = bhnd_activate_resource(sc->dev, SYS_RES_MEMORY,
			    cr->cr_res_rid, cr->cr_res);
			if (error) {
				/* Drop any allocation reference acquired
				 * above */
				CHIPC_UNLOCK(sc);
				chipc_release_region(sc, cr,
				    flags &~ RF_ACTIVE);
				return (error);
			}
		}

		/* Increment activation refcount */
		cr->cr_act_refs++;
	}

	CHIPC_UNLOCK(sc);
	return (0);
}

/**
 * Release a reference to a chipc_region, deactivating and releasing the
 * backing resource if the reference count hits zero.
 * 
 * @param sc chipc driver instance state
 * @param cr region to retain.
 * @param flags specify RF_ALLOCATED to release an allocation reference,
 * RF_ACTIVE to release an activation reference.
 */
int
chipc_release_region(struct chipc_softc *sc, struct chipc_region *cr,
    int flags)
{
	int	error;

	CHIPC_LOCK(sc);
	error = 0;

	KASSERT(cr->cr_res != NULL, ("release on NULL region resource"));

	if (flags & RF_ACTIVE) {
		KASSERT(cr->cr_act_refs > 0, ("RF_ACTIVE over-released"));
		KASSERT(cr->cr_act_refs <= cr->cr_refs,
		     ("RF_ALLOCATED released with RF_ACTIVE held"));

		/* If this is the last reference, deactivate the resource */
		if (cr->cr_act_refs == 1) {
			error = bhnd_deactivate_resource(sc->dev,
			    SYS_RES_MEMORY, cr->cr_res_rid, cr->cr_res);
			if (error)
				goto done;
		}

		/* Drop our activation refcount */
		cr->cr_act_refs--;
	}

	if (flags & RF_ALLOCATED) {
		KASSERT(cr->cr_refs > 0, ("overrelease of refs"));
		/* If this is the last reference, release the resource */
		if (cr->cr_refs == 1) {
			error = bhnd_release_resource(sc->dev, SYS_RES_MEMORY,
			    cr->cr_res_rid, cr->cr_res);
			if (error)
				goto done;

			cr->cr_res = NULL;
		}

		/* Drop our allocation refcount */
		cr->cr_refs--;
	}

done:
	CHIPC_UNLOCK(sc);
	return (error);
}
