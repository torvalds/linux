/*-
 * Copyright (c) 2015-2016 Landon Fuller <landon@freebsd.org>
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
 * 
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * bhnd(4) driver mix-in providing shared common methods for
 * bhnd bus devices attached via a MIPS root nexus.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/intr.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/malloc.h>

#include <machine/bus.h>

#include <dev/bhnd/bhndvar.h>
#include <dev/bhnd/bhnd_ids.h>

#include <dev/bhnd/cores/chipc/chipcreg.h>

#include "bcm_machdep.h"
#include "bcm_mipsvar.h"

#include "bhnd_nexusvar.h"


/**
 * Default bhnd_nexus implementation of BHND_BUS_GET_SERVICE_REGISTRY().
 */
static struct bhnd_service_registry *
bhnd_nexus_get_service_registry(device_t dev, device_t child)
{
	struct bcm_platform *bp = bcm_get_platform();
	return (&bp->services);
}

/**
 * Default bhnd_nexus implementation of BHND_BUS_ACTIVATE_RESOURCE().
 */
static int
bhnd_nexus_activate_resource(device_t dev, device_t child, int type, int rid,
    struct bhnd_resource *r)
{
	int error;

	/* Always direct */
	if ((error = bus_activate_resource(child, type, rid, r->res)))
		return (error);

	r->direct = true;
	return (0);
}

/**
 * Default bhnd_nexus implementation of BHND_BUS_DEACTIVATE_RESOURCE().
 */
static int
bhnd_nexus_deactivate_resource(device_t dev, device_t child,
    int type, int rid, struct bhnd_resource *r)
{
	int error;

	/* Always direct */
	KASSERT(r->direct, ("indirect resource delegated to bhnd_nexus\n"));

	if ((error = bus_deactivate_resource(child, type, rid, r->res)))
		return (error);

	r->direct = false;
	return (0);
}

/**
 * Default bhnd_nexus implementation of BHND_BUS_IS_HW_DISABLED().
 */
static bool
bhnd_nexus_is_hw_disabled(device_t dev, device_t child)
{
	struct bcm_platform	*bp;
	struct bhnd_chipid	*cid;

	bp = bcm_get_platform();
	cid = &bp->cid;

	/* The BCM4706 low-cost package leaves secondary GMAC cores
	 * floating */
	if (cid->chip_id == BHND_CHIPID_BCM4706 &&
	    cid->chip_pkg == BHND_PKGID_BCM4706L &&
	    bhnd_get_device(child) == BHND_COREID_4706_GMAC &&
	    bhnd_get_core_unit(child) != 0)
	{
		return (true);
	}

	return (false);
}

/**
 * Default bhnd_nexus implementation of BHND_BUS_AGET_ATTACH_TYPE().
 */
static bhnd_attach_type
bhnd_nexus_get_attach_type(device_t dev, device_t child)
{
	return (BHND_ATTACH_NATIVE);
}

/**
 * Default bhnd_nexus implementation of BHND_BUS_GET_CHIPID().
 */
static const struct bhnd_chipid *
bhnd_nexus_get_chipid(device_t dev, device_t child)
{
	return (&bcm_get_platform()->cid);
}

/**
 * Default bhnd_nexus implementation of BHND_BUS_READ_BOARD_INFO().
 */
static int
bhnd_nexus_read_board_info(device_t dev, device_t child,
    struct bhnd_board_info *info)
{
	int error;

	/* Initialize with NVRAM-derived values */
	if ((error = bhnd_bus_generic_read_board_info(dev, child, info)))
		return (error);

	/* The board vendor should default to PCI_VENDOR_BROADCOM if not
	 * otherwise specified */
	if (info->board_vendor == 0)
		info->board_vendor = PCI_VENDOR_BROADCOM;

	return (0);
}

/**
 * Default bhnd_nexus implementation of BHND_BUS_MAP_INTR().
 */
static int
bhnd_nexus_map_intr(device_t dev, device_t child, u_int intr, rman_res_t *irq)
{
	struct bcm_mips_intr_map_data	*imd;
	u_int				 ivec;
	uintptr_t			 xref;
	int				 error;

	/* Fetch the backplane interrupt vector */
	if ((error = bhnd_get_intr_ivec(child, intr, &ivec))) {
		device_printf(dev, "error fetching ivec for intr %u: %d\n",
		    intr, error);
		return (error);
	}

	/* Determine our interrupt domain */
	xref = BHND_BUS_GET_INTR_DOMAIN(dev, child, false);
	KASSERT(xref != 0, ("missing interrupt domain"));

	/* Allocate our map data */
	imd = (struct bcm_mips_intr_map_data *)intr_alloc_map_data(
	    INTR_MAP_DATA_BCM_MIPS, sizeof(*imd), M_WAITOK | M_ZERO);
	imd->ivec = ivec;

	/* Map the IRQ */
	*irq = intr_map_irq(NULL, xref, &imd->mdata);
	return (0);
}

/**
 * Default bhnd_nexus implementation of BHND_BUS_UNMAP_INTR().
 */
static void
bhnd_nexus_unmap_intr(device_t dev, device_t child, rman_res_t irq)
{
	if (irq > UINT_MAX)
		panic("invalid irq: %ju", (uintmax_t)irq);

	intr_unmap_irq(irq);
}

/**
 * Default bhnd_nexus implementation of BHND_BUS_GET_DMA_TRANSLATION().
 */
static int
bhnd_nexus_get_dma_translation(device_t dev, device_t child,
    u_int width, uint32_t flags, bus_dma_tag_t *dmat,
    struct bhnd_dma_translation *translation)
{
	struct bcm_platform *bp = bcm_get_platform();

	/* We don't (currently) support any flags */
	if (flags != 0x0)
		return (ENOENT);

	KASSERT(width > 0 && width <= BHND_DMA_ADDR_64BIT,
	    ("invalid width %u", width));

	/* Is the requested width supported? */
	if (width > BHND_DMA_ADDR_32BIT) {
		/* Backplane must support 64-bit addressing */
		if (!(bp->cid.chip_caps & BHND_CAP_BP64))
			width = BHND_DMA_ADDR_32BIT;
	}

	/* No DMA address translation required */
	if (dmat != NULL)
		*dmat = bus_get_dma_tag(dev);

	if (translation != NULL) {
		*translation = (struct bhnd_dma_translation) {
			.base_addr	= 0x0,
			.addr_mask	= BHND_DMA_ADDR_BITMASK(width),
			.addrext_mask	= 0
		};
	}

	return (0);
}

static device_method_t bhnd_nexus_methods[] = {
	/* bhnd interface */
	DEVMETHOD(bhnd_bus_get_service_registry,bhnd_nexus_get_service_registry),
	DEVMETHOD(bhnd_bus_register_provider,	bhnd_bus_generic_sr_register_provider),
	DEVMETHOD(bhnd_bus_deregister_provider,	bhnd_bus_generic_sr_deregister_provider),
	DEVMETHOD(bhnd_bus_retain_provider,	bhnd_bus_generic_sr_retain_provider),
	DEVMETHOD(bhnd_bus_release_provider,	bhnd_bus_generic_sr_release_provider),
	DEVMETHOD(bhnd_bus_activate_resource,	bhnd_nexus_activate_resource),
	DEVMETHOD(bhnd_bus_deactivate_resource, bhnd_nexus_deactivate_resource),
	DEVMETHOD(bhnd_bus_is_hw_disabled,	bhnd_nexus_is_hw_disabled),
	DEVMETHOD(bhnd_bus_get_attach_type,	bhnd_nexus_get_attach_type),
	DEVMETHOD(bhnd_bus_get_chipid,		bhnd_nexus_get_chipid),
	DEVMETHOD(bhnd_bus_get_dma_translation,	bhnd_nexus_get_dma_translation),
	DEVMETHOD(bhnd_bus_get_intr_domain,	bhnd_bus_generic_get_intr_domain),
	DEVMETHOD(bhnd_bus_map_intr,		bhnd_nexus_map_intr),
	DEVMETHOD(bhnd_bus_read_board_info,	bhnd_nexus_read_board_info),
	DEVMETHOD(bhnd_bus_unmap_intr,		bhnd_nexus_unmap_intr),

	DEVMETHOD_END
};

DEFINE_CLASS_0(bhnd, bhnd_nexus_driver, bhnd_nexus_methods,
    sizeof(struct bhnd_softc));
