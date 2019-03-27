/*-
 * Copyright (c) 2015 Ruslan Bukin <br@bsdpad.com>
 * Copyright (c) 2014 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under
 * the sponsorship of the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* Generic ECAM PCIe driver */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/rman.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/endian.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcib_private.h>
#include <dev/pci/pci_host_generic.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include "pcib_if.h"

/* Assembling ECAM Configuration Address */
#define	PCIE_BUS_SHIFT		20
#define	PCIE_SLOT_SHIFT		15
#define	PCIE_FUNC_SHIFT		12
#define	PCIE_BUS_MASK		0xFF
#define	PCIE_SLOT_MASK		0x1F
#define	PCIE_FUNC_MASK		0x07
#define	PCIE_REG_MASK		0xFFF

#define	PCIE_ADDR_OFFSET(bus, slot, func, reg)			\
	((((bus) & PCIE_BUS_MASK) << PCIE_BUS_SHIFT)	|	\
	(((slot) & PCIE_SLOT_MASK) << PCIE_SLOT_SHIFT)	|	\
	(((func) & PCIE_FUNC_MASK) << PCIE_FUNC_SHIFT)	|	\
	((reg) & PCIE_REG_MASK))

/* Forward prototypes */

static uint32_t generic_pcie_read_config(device_t dev, u_int bus, u_int slot,
    u_int func, u_int reg, int bytes);
static void generic_pcie_write_config(device_t dev, u_int bus, u_int slot,
    u_int func, u_int reg, uint32_t val, int bytes);
static int generic_pcie_maxslots(device_t dev);
static int generic_pcie_read_ivar(device_t dev, device_t child, int index,
    uintptr_t *result);
static int generic_pcie_write_ivar(device_t dev, device_t child, int index,
    uintptr_t value);

int
pci_host_generic_core_attach(device_t dev)
{
	struct generic_pcie_core_softc *sc;
	int error;
	int rid;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Create the parent DMA tag to pass down the coherent flag */
	error = bus_dma_tag_create(bus_get_dma_tag(dev), /* parent */
	    1, 0,				/* alignment, bounds */
	    BUS_SPACE_MAXADDR,			/* lowaddr */
	    BUS_SPACE_MAXADDR,			/* highaddr */
	    NULL, NULL,				/* filter, filterarg */
	    BUS_SPACE_MAXSIZE,			/* maxsize */
	    BUS_SPACE_UNRESTRICTED,		/* nsegments */
	    BUS_SPACE_MAXSIZE,			/* maxsegsize */
	    sc->coherent ? BUS_DMA_COHERENT : 0, /* flags */
	    NULL, NULL,				/* lockfunc, lockarg */
	    &sc->dmat);
	if (error != 0)
		return (error);

	rid = 0;
	sc->res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->res == NULL) {
		device_printf(dev, "could not map memory.\n");
		return (ENXIO);
	}

	sc->bst = rman_get_bustag(sc->res);
	sc->bsh = rman_get_bushandle(sc->res);

	sc->mem_rman.rm_type = RMAN_ARRAY;
	sc->mem_rman.rm_descr = "PCIe Memory";
	sc->io_rman.rm_type = RMAN_ARRAY;
	sc->io_rman.rm_descr = "PCIe IO window";

	/* Initialize rman and allocate memory regions */
	error = rman_init(&sc->mem_rman);
	if (error) {
		device_printf(dev, "rman_init() failed. error = %d\n", error);
		return (error);
	}

	error = rman_init(&sc->io_rman);
	if (error) {
		device_printf(dev, "rman_init() failed. error = %d\n", error);
		return (error);
	}

	return (0);
}

static uint32_t
generic_pcie_read_config(device_t dev, u_int bus, u_int slot,
    u_int func, u_int reg, int bytes)
{
	struct generic_pcie_core_softc *sc;
	bus_space_handle_t h;
	bus_space_tag_t	t;
	uint64_t offset;
	uint32_t data;

	sc = device_get_softc(dev);
	if ((bus < sc->bus_start) || (bus > sc->bus_end))
		return (~0U);
	if ((slot > PCI_SLOTMAX) || (func > PCI_FUNCMAX) ||
	    (reg > PCIE_REGMAX))
		return (~0U);

	offset = PCIE_ADDR_OFFSET(bus - sc->bus_start, slot, func, reg);
	t = sc->bst;
	h = sc->bsh;

	switch (bytes) {
	case 1:
		data = bus_space_read_1(t, h, offset);
		break;
	case 2:
		data = le16toh(bus_space_read_2(t, h, offset));
		break;
	case 4:
		data = le32toh(bus_space_read_4(t, h, offset));
		break;
	default:
		return (~0U);
	}

	return (data);
}

static void
generic_pcie_write_config(device_t dev, u_int bus, u_int slot,
    u_int func, u_int reg, uint32_t val, int bytes)
{
	struct generic_pcie_core_softc *sc;
	bus_space_handle_t h;
	bus_space_tag_t t;
	uint64_t offset;

	sc = device_get_softc(dev);
	if ((bus < sc->bus_start) || (bus > sc->bus_end))
		return;
	if ((slot > PCI_SLOTMAX) || (func > PCI_FUNCMAX) ||
	    (reg > PCIE_REGMAX))
		return;

	offset = PCIE_ADDR_OFFSET(bus - sc->bus_start, slot, func, reg);

	t = sc->bst;
	h = sc->bsh;

	switch (bytes) {
	case 1:
		bus_space_write_1(t, h, offset, val);
		break;
	case 2:
		bus_space_write_2(t, h, offset, htole16(val));
		break;
	case 4:
		bus_space_write_4(t, h, offset, htole32(val));
		break;
	default:
		return;
	}
}

static int
generic_pcie_maxslots(device_t dev)
{

	return (31); /* max slots per bus acc. to standard */
}

static int
generic_pcie_read_ivar(device_t dev, device_t child, int index,
    uintptr_t *result)
{
	struct generic_pcie_core_softc *sc;

	sc = device_get_softc(dev);

	if (index == PCIB_IVAR_BUS) {
		*result = sc->bus_start;
		return (0);

	}

	if (index == PCIB_IVAR_DOMAIN) {
		*result = sc->ecam;
		return (0);
	}

	if (bootverbose)
		device_printf(dev, "ERROR: Unknown index %d.\n", index);
	return (ENOENT);
}

static int
generic_pcie_write_ivar(device_t dev, device_t child, int index,
    uintptr_t value)
{

	return (ENOENT);
}

static struct rman *
generic_pcie_rman(struct generic_pcie_core_softc *sc, int type)
{

	switch (type) {
	case SYS_RES_IOPORT:
		return (&sc->io_rman);
	case SYS_RES_MEMORY:
		return (&sc->mem_rman);
	default:
		break;
	}

	return (NULL);
}

int
pci_host_generic_core_release_resource(device_t dev, device_t child, int type,
    int rid, struct resource *res)
{
	struct generic_pcie_core_softc *sc;
	struct rman *rm;

	sc = device_get_softc(dev);

#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	if (type == PCI_RES_BUS) {
		return (pci_domain_release_bus(sc->ecam, child, rid, res));
	}
#endif

	rm = generic_pcie_rman(sc, type);
	if (rm != NULL) {
		KASSERT(rman_is_region_manager(res, rm), ("rman mismatch"));
		rman_release_resource(res);
	}

	return (bus_generic_release_resource(dev, child, type, rid, res));
}

struct resource *
pci_host_generic_core_alloc_resource(device_t dev, device_t child, int type,
    int *rid, rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct generic_pcie_core_softc *sc;
	struct resource *res;
	struct rman *rm;

	sc = device_get_softc(dev);

#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	if (type == PCI_RES_BUS) {
		return (pci_domain_alloc_bus(sc->ecam, child, rid, start, end,
		    count, flags));
	}
#endif

	rm = generic_pcie_rman(sc, type);
	if (rm == NULL)
		return (BUS_ALLOC_RESOURCE(device_get_parent(dev), child,
		    type, rid, start, end, count, flags));

	if (bootverbose) {
		device_printf(dev,
		    "rman_reserve_resource: start=%#jx, end=%#jx, count=%#jx\n",
		    start, end, count);
	}

	res = rman_reserve_resource(rm, start, end, count, flags, child);
	if (res == NULL)
		goto fail;

	rman_set_rid(res, *rid);

	if (flags & RF_ACTIVE)
		if (bus_activate_resource(child, type, *rid, res)) {
			rman_release_resource(res);
			goto fail;
		}

	return (res);

fail:
	device_printf(dev, "%s FAIL: type=%d, rid=%d, "
	    "start=%016jx, end=%016jx, count=%016jx, flags=%x\n",
	    __func__, type, *rid, start, end, count, flags);

	return (NULL);
}

static int
generic_pcie_activate_resource(device_t dev, device_t child, int type,
    int rid, struct resource *r)
{
	struct generic_pcie_core_softc *sc;
	uint64_t phys_base;
	uint64_t pci_base;
	uint64_t size;
	int found;
	int res;
	int i;

	sc = device_get_softc(dev);

	if ((res = rman_activate_resource(r)) != 0)
		return (res);

	switch (type) {
	case SYS_RES_IOPORT:
		found = 0;
		for (i = 0; i < MAX_RANGES_TUPLES; i++) {
			pci_base = sc->ranges[i].pci_base;
			phys_base = sc->ranges[i].phys_base;
			size = sc->ranges[i].size;

			if ((rid > pci_base) && (rid < (pci_base + size))) {
				found = 1;
				break;
			}
		}
		if (found) {
			rman_set_start(r, rman_get_start(r) + phys_base);
			rman_set_end(r, rman_get_end(r) + phys_base);
			res = BUS_ACTIVATE_RESOURCE(device_get_parent(dev),
			    child, type, rid, r);
		} else {
			device_printf(dev,
			    "Failed to activate IOPORT resource\n");
			res = 0;
		}
		break;
	case SYS_RES_MEMORY:
	case SYS_RES_IRQ:
		res = BUS_ACTIVATE_RESOURCE(device_get_parent(dev), child,
		    type, rid, r);
		break;
	default:
		break;
	}

	return (res);
}

static int
generic_pcie_deactivate_resource(device_t dev, device_t child, int type,
    int rid, struct resource *r)
{
	int res;

	if ((res = rman_deactivate_resource(r)) != 0)
		return (res);

	switch (type) {
	case SYS_RES_IOPORT:
	case SYS_RES_MEMORY:
	case SYS_RES_IRQ:
		res = BUS_DEACTIVATE_RESOURCE(device_get_parent(dev), child,
		    type, rid, r);
		break;
	default:
		break;
	}

	return (res);
}

static int
generic_pcie_adjust_resource(device_t dev, device_t child, int type,
    struct resource *res, rman_res_t start, rman_res_t end)
{
	struct generic_pcie_core_softc *sc;
	struct rman *rm;

	sc = device_get_softc(dev);
#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	if (type == PCI_RES_BUS)
		return (pci_domain_adjust_bus(sc->ecam, child, res, start,
		    end));
#endif

	rm = generic_pcie_rman(sc, type);
	if (rm != NULL)
		return (rman_adjust_resource(res, start, end));
	return (bus_generic_adjust_resource(dev, child, type, res, start, end));
}

static bus_dma_tag_t
generic_pcie_get_dma_tag(device_t dev, device_t child)
{
	struct generic_pcie_core_softc *sc;

	sc = device_get_softc(dev);
	return (sc->dmat);
}

static device_method_t generic_pcie_methods[] = {
	DEVMETHOD(device_attach,		pci_host_generic_core_attach),
	DEVMETHOD(bus_read_ivar,		generic_pcie_read_ivar),
	DEVMETHOD(bus_write_ivar,		generic_pcie_write_ivar),
	DEVMETHOD(bus_alloc_resource,		pci_host_generic_core_alloc_resource),
	DEVMETHOD(bus_adjust_resource,		generic_pcie_adjust_resource),
	DEVMETHOD(bus_activate_resource,	generic_pcie_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	generic_pcie_deactivate_resource),
	DEVMETHOD(bus_release_resource,		pci_host_generic_core_release_resource),
	DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,		bus_generic_teardown_intr),

	DEVMETHOD(bus_get_dma_tag,		generic_pcie_get_dma_tag),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,		generic_pcie_maxslots),
	DEVMETHOD(pcib_read_config,		generic_pcie_read_config),
	DEVMETHOD(pcib_write_config,		generic_pcie_write_config),

	DEVMETHOD_END
};

DEFINE_CLASS_0(pcib, generic_pcie_core_driver,
    generic_pcie_methods, sizeof(struct generic_pcie_core_softc));
