/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2015 Landon Fuller <landon@landonf.org>
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

/*
 * Broadcom Common PCIe-G2 Support.
 * 
 * This base driver implementation is shared by the bhnd_pcib_g2 (root complex)
 * and bhnd_pci_hostb_g2 (host bridge) drivers.
 */

#include <sys/param.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/bhnd/bhnd.h>
#include <dev/mdio/mdio.h>

#include "bhnd_pcie2_reg.h"
#include "bhnd_pcie2_var.h"

static struct bhnd_device_quirk bhnd_pcie2_quirks[];

#define	BHND_PCIE_DEV(_core, _desc, ...)				\
	BHND_DEVICE(BCM, _core, _desc, bhnd_pcie2_quirks, ## __VA_ARGS__)

static const struct bhnd_device bhnd_pcie2_devs[] = {
	BHND_PCIE_DEV(PCIE2,	"PCIe-G2 Host-PCI bridge",	BHND_DF_HOSTB),
	BHND_PCIE_DEV(PCIE2,	"PCIe-G2 PCI-BHND bridge",	BHND_DF_SOC),

	BHND_DEVICE_END
};

/* Device quirks tables */
static struct bhnd_device_quirk bhnd_pcie2_quirks[] = {
	BHND_DEVICE_QUIRK_END
};

int
bhnd_pcie2_generic_probe(device_t dev)
{
	const struct bhnd_device	*id;

	id = bhnd_device_lookup(dev, bhnd_pcie2_devs,
	    sizeof(bhnd_pcie2_devs[0]));
	if (id == NULL)
		return (ENXIO);

	bhnd_set_custom_core_desc(dev, id->desc);
	return (BUS_PROBE_DEFAULT);
}

int
bhnd_pcie2_generic_attach(device_t dev)
{
	struct bhnd_pcie2_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->quirks = bhnd_device_quirks(dev, bhnd_pcie2_devs,
	    sizeof(bhnd_pcie2_devs[0]));

	/* Allocate bus resources */
	sc->mem_res = bhnd_alloc_resource_any(dev, SYS_RES_MEMORY, &sc->mem_rid,
	    RF_ACTIVE);
	if (sc->mem_res == NULL)
		return (ENXIO);

	BHND_PCIE2_LOCK_INIT(sc);

	/* Probe and attach children */
	if ((error = bus_generic_attach(dev)))
		goto cleanup;

	return (0);

cleanup:
	bhnd_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
	BHND_PCIE2_LOCK_DESTROY(sc);

	return (error);
}

int
bhnd_pcie2_generic_detach(device_t dev)
{
	struct bhnd_pcie2_softc	*sc;
	int			 error;

	sc = device_get_softc(dev);

	if ((error = bus_generic_detach(dev)))
		return (error);

	bhnd_release_resource(dev, SYS_RES_MEMORY, sc->mem_rid, sc->mem_res);
	
	BHND_PCIE2_LOCK_DESTROY(sc);

	return (0);
}

static struct resource_list *
bhnd_pcie2_get_resource_list(device_t dev, device_t child)
{
	struct bhnd_pcie2_devinfo *dinfo;

	if (device_get_parent(child) != dev)
		return (NULL);

	dinfo = device_get_ivars(child);
	return (&dinfo->resources);
}

static device_t
bhnd_pcie2_add_child(device_t dev, u_int order, const char *name, int unit)
{
	struct bhnd_pcie2_devinfo	*dinfo;
	device_t			 child;
	
	child = device_add_child_ordered(dev, order, name, unit);
	if (child == NULL)
		return (NULL);

	dinfo = malloc(sizeof(struct bhnd_pcie2_devinfo), M_DEVBUF, M_NOWAIT);
	if (dinfo == NULL) {
		device_delete_child(dev, child);
		return (NULL);
	}

	resource_list_init(&dinfo->resources);
	
	device_set_ivars(child, dinfo);
	return (child);
}

static void
bhnd_pcie2_child_deleted(device_t dev, device_t child)
{
	struct bhnd_pcie2_devinfo *dinfo;

	if (device_get_parent(child) != dev)
		return;

	dinfo = device_get_ivars(child);
	if (dinfo != NULL) {
		resource_list_free(&dinfo->resources);
		free(dinfo, M_DEVBUF);
	}

	device_set_ivars(child, NULL);
}

int
bhnd_pcie2_generic_suspend(device_t dev)
{
	return (bus_generic_suspend(dev));
}

int
bhnd_pcie2_generic_resume(device_t dev)
{
	return (bus_generic_resume(dev));
}

/**
 * Read a 32-bit PCIe TLP/DLLP/PLP protocol register.
 * 
 * @param sc The bhndb_pci driver state.
 * @param addr The protocol register offset.
 */
uint32_t
bhnd_pcie2_read_proto_reg(struct bhnd_pcie2_softc *sc, uint32_t addr)
{
	// TODO
	return (ENXIO);
}

/**
 * Write a 32-bit PCIe TLP/DLLP/PLP protocol register value.
 * 
 * @param sc The bhndb_pci driver state.
 * @param addr The protocol register offset.
 * @param val The value to write to @p addr.
 */
void
bhnd_pcie2_write_proto_reg(struct bhnd_pcie2_softc *sc, uint32_t addr,
    uint32_t val)
{
	// TODO
	panic("unimplemented");
}

int
bhnd_pcie2_mdio_read(struct bhnd_pcie2_softc *sc, int phy, int reg)
{
	// TODO
	return (ENXIO);
}

int
bhnd_pcie2_mdio_write(struct bhnd_pcie2_softc *sc, int phy, int reg, int val)
{
	// TODO
	return (ENXIO);
}

int
bhnd_pcie2_mdio_read_ext(struct bhnd_pcie2_softc *sc, int phy, int devaddr,
    int reg)
{
	// TODO
	return (ENXIO);
}

int
bhnd_pcie2_mdio_write_ext(struct bhnd_pcie2_softc *sc, int phy, int devaddr,
    int reg, int val)
{	
	// TODO
	return (ENXIO);
}

static device_method_t bhnd_pcie2_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			bhnd_pcie2_generic_probe),
	DEVMETHOD(device_attach,		bhnd_pcie2_generic_attach),
	DEVMETHOD(device_detach,		bhnd_pcie2_generic_detach),
	DEVMETHOD(device_suspend,		bhnd_pcie2_generic_suspend),
	DEVMETHOD(device_resume,		bhnd_pcie2_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_add_child,		bhnd_pcie2_add_child),
	DEVMETHOD(bus_child_deleted,		bhnd_pcie2_child_deleted),
	DEVMETHOD(bus_print_child,		bus_generic_print_child),
	DEVMETHOD(bus_get_resource_list,	bhnd_pcie2_get_resource_list),
	DEVMETHOD(bus_get_resource,		bus_generic_rl_get_resource),
	DEVMETHOD(bus_set_resource,		bus_generic_rl_set_resource),
	DEVMETHOD(bus_delete_resource,		bus_generic_rl_delete_resource),

	DEVMETHOD(bus_alloc_resource,		bus_generic_rl_alloc_resource),
	DEVMETHOD(bus_activate_resource,        bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource,      bus_generic_deactivate_resource),
	DEVMETHOD(bus_adjust_resource,          bus_generic_adjust_resource),
	DEVMETHOD(bus_release_resource,		bus_generic_rl_release_resource),
	
	DEVMETHOD_END
};

DEFINE_CLASS_0(bhnd_pcie2, bhnd_pcie2_driver, bhnd_pcie2_methods,
   sizeof(struct bhnd_pcie2_softc));
MODULE_DEPEND(bhnd_pcie2, bhnd, 1, 1, 1);
MODULE_DEPEND(bhnd_pcie2, pci, 1, 1, 1);
MODULE_VERSION(bhnd_pcie2, 1);
