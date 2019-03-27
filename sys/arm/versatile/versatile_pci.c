/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012-2017 Oleksandr Tymoshenko <gonzo@freebsd.org>
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

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/watchdog.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/pci/pcib_private.h>
#include "pcib_if.h"

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofw_pci.h>

#include <arm/versatile/versatile_scm.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#define	MEM_CORE	0
#define	MEM_BASE	1
#define	MEM_CONF_BASE	2
#define MEM_REGIONS	3

#define	PCI_CORE_IMAP0		0x00
#define	PCI_CORE_IMAP1		0x04
#define	PCI_CORE_IMAP2		0x08
#define	PCI_CORE_SELFID		0x0C
#define	PCI_CORE_SMAP0		0x10
#define	PCI_CORE_SMAP1		0x14
#define	PCI_CORE_SMAP2		0x18

#define	VERSATILE_PCI_DEV	0x030010ee
#define	VERSATILE_PCI_CLASS	0x0b400000

#define	PCI_IO_WINDOW		0x44000000
#define	PCI_IO_SIZE		0x0c000000
#define	PCI_NPREFETCH_WINDOW	0x50000000
#define	PCI_NPREFETCH_SIZE	0x10000000
#define	PCI_PREFETCH_WINDOW	0x60000000
#define	PCI_PREFETCH_SIZE	0x10000000

#define	VERSATILE_PCI_IRQ_START	27
#define	VERSATILE_PCI_IRQ_END	30

#ifdef DEBUG
#define dprintf(fmt, args...) do { printf("%s(): ", __func__);   \
    printf(fmt,##args); } while (0)
#else
#define dprintf(fmt, args...)
#endif

#define	versatile_pci_core_read_4(reg)	\
	bus_read_4(sc->mem_res[MEM_CORE], (reg))
#define	versatile_pci_core_write_4(reg, val)	\
	bus_write_4(sc->mem_res[MEM_CORE], (reg), (val))

#define	versatile_pci_read_4(reg)	\
	bus_read_4(sc->mem_res[MEM_BASE], (reg))
#define	versatile_pci_write_4(reg, val)	\
	bus_write_4(sc->mem_res[MEM_BASE], (reg), (val))

#define	versatile_pci_conf_read_4(reg)	\
	bus_read_4(sc->mem_res[MEM_CONF_BASE], (reg))
#define	versatile_pci_conf_write_4(reg, val)	\
	bus_write_4(sc->mem_res[MEM_CONF_BASE], (reg), (val))
#define	versatile_pci_conf_write_2(reg, val)	\
	bus_write_2(sc->mem_res[MEM_CONF_BASE], (reg), (val))
#define	versatile_pci_conf_write_1(reg, val)	\
	bus_write_1(sc->mem_res[MEM_CONF_BASE], (reg), (val))

struct versatile_pci_softc {
	struct resource*	mem_res[MEM_REGIONS];
	struct resource*	irq_res;
	void*			intr_hl;

	int			pcib_slot;

	/* Bus part */
	int			busno;
	struct rman		io_rman;
	struct rman		irq_rman;
	struct rman		mem_rman;

	struct mtx		mtx;
	struct ofw_bus_iinfo	pci_iinfo;
};

static struct resource_spec versatile_pci_mem_spec[] = {
	{ SYS_RES_MEMORY, 0, RF_ACTIVE },
	{ SYS_RES_MEMORY, 1, RF_ACTIVE },
	{ SYS_RES_MEMORY, 2, RF_ACTIVE },
	{ -1, 0, 0 }
};

static int
versatile_pci_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "arm,versatile-pci")) {
		device_set_desc(dev, "Versatile PCI controller");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
versatile_pci_attach(device_t dev)
{
	struct versatile_pci_softc *sc = device_get_softc(dev);
	int err;
	int slot;
	uint32_t vendordev_id, class_id;
	uint32_t val;
	phandle_t node;

	node = ofw_bus_get_node(dev);

	/* Request memory resources */
	err = bus_alloc_resources(dev, versatile_pci_mem_spec,
		sc->mem_res);
	if (err) {
		device_printf(dev, "Error: could not allocate memory resources\n");
		return (ENXIO);
	}

	/*
	 * Setup memory windows
	 */
	versatile_pci_core_write_4(PCI_CORE_IMAP0, (PCI_IO_WINDOW >> 28));
	versatile_pci_core_write_4(PCI_CORE_IMAP1, (PCI_NPREFETCH_WINDOW >> 28));
	versatile_pci_core_write_4(PCI_CORE_IMAP2, (PCI_PREFETCH_WINDOW >> 28));

	/*
	 * XXX: this is SDRAM offset >> 28
	 * Unused as of QEMU 1.5
	 */
	versatile_pci_core_write_4(PCI_CORE_SMAP0, (PCI_IO_WINDOW >> 28));
	versatile_pci_core_write_4(PCI_CORE_SMAP1, (PCI_NPREFETCH_WINDOW >> 28));
	versatile_pci_core_write_4(PCI_CORE_SMAP2, (PCI_NPREFETCH_WINDOW >> 28));

	versatile_scm_reg_write_4(SCM_PCICTL, 1);

	for (slot = 0; slot <= PCI_SLOTMAX; slot++) {
		vendordev_id = versatile_pci_read_4((slot << 11) + PCIR_DEVVENDOR);
		class_id = versatile_pci_read_4((slot << 11) + PCIR_REVID);
		if ((vendordev_id == VERSATILE_PCI_DEV) &&
		    (class_id == VERSATILE_PCI_CLASS))
			break;
	}

	if (slot == (PCI_SLOTMAX + 1)) {
		bus_release_resources(dev, versatile_pci_mem_spec,
		    sc->mem_res);
		device_printf(dev, "Versatile PCI core not found\n");
		return (ENXIO);
	}

	sc->pcib_slot = slot;
	device_printf(dev, "PCI core at slot #%d\n", slot);

	versatile_pci_core_write_4(PCI_CORE_SELFID, slot);
	val = versatile_pci_conf_read_4((slot << 11) + PCIR_COMMAND);
	val |= (PCIM_CMD_BUSMASTEREN | PCIM_CMD_MEMEN | PCIM_CMD_MWRICEN);
	versatile_pci_conf_write_4((slot << 11) + PCIR_COMMAND, val);

	/* Again SDRAM start >> 28  */
	versatile_pci_write_4((slot << 11) + PCIR_BAR(0), 0);
	versatile_pci_write_4((slot << 11) + PCIR_BAR(1), 0);
	versatile_pci_write_4((slot << 11) + PCIR_BAR(2), 0);

	/* Prepare resource managers */
	sc->mem_rman.rm_type = RMAN_ARRAY;
	sc->mem_rman.rm_descr = "versatile PCI memory window";
	if (rman_init(&sc->mem_rman) != 0 || 
	    rman_manage_region(&sc->mem_rman, PCI_NPREFETCH_WINDOW, 
		PCI_NPREFETCH_WINDOW + PCI_NPREFETCH_SIZE - 1) != 0) {
		panic("versatile_pci_attach: failed to set up memory rman");
	}

	bootverbose = 1;
	sc->io_rman.rm_type = RMAN_ARRAY;
	sc->io_rman.rm_descr = "versatile PCI IO window";
	if (rman_init(&sc->io_rman) != 0 || 
	    rman_manage_region(&sc->io_rman, PCI_IO_WINDOW, 
		PCI_IO_WINDOW + PCI_IO_SIZE - 1) != 0) {
		panic("versatile_pci_attach: failed to set up I/O rman");
	}

	sc->irq_rman.rm_type = RMAN_ARRAY;
	sc->irq_rman.rm_descr = "versatile PCI IRQs";
	if (rman_init(&sc->irq_rman) != 0 ||
	    rman_manage_region(&sc->irq_rman, VERSATILE_PCI_IRQ_START, 
	        VERSATILE_PCI_IRQ_END) != 0) {
		panic("versatile_pci_attach: failed to set up IRQ rman");
	}

	mtx_init(&sc->mtx, device_get_nameunit(dev), "versatilepci",
			MTX_SPIN);

	val = versatile_pci_conf_read_4((12 << 11) + PCIR_COMMAND);

	for (slot = 0; slot <= PCI_SLOTMAX; slot++) {
		vendordev_id = versatile_pci_read_4((slot << 11) + PCIR_DEVVENDOR);
		class_id = versatile_pci_read_4((slot << 11) + PCIR_REVID);

		if (slot == sc->pcib_slot)
			continue;

		if ((vendordev_id == 0xffffffff) &&
		    (class_id == 0xffffffff))
			continue;

		val = versatile_pci_conf_read_4((slot << 11) + PCIR_COMMAND);
		val |= PCIM_CMD_MEMEN | PCIM_CMD_PORTEN;
		versatile_pci_conf_write_4((slot << 11) + PCIR_COMMAND, val);
	}

	ofw_bus_setup_iinfo(node, &sc->pci_iinfo, sizeof(cell_t));

	device_add_child(dev, "pci", -1);
	return (bus_generic_attach(dev));
}

static int
versatile_pci_read_ivar(device_t dev, device_t child, int which,
    uintptr_t *result)
{
	struct versatile_pci_softc *sc = device_get_softc(dev);

	switch (which) {
	case PCIB_IVAR_DOMAIN:
		*result = 0;
		return (0);
	case PCIB_IVAR_BUS:
		*result = sc->busno;
		return (0);
	}

	return (ENOENT);
}

static int
versatile_pci_write_ivar(device_t dev, device_t child, int which,
    uintptr_t result)
{
	struct versatile_pci_softc * sc = device_get_softc(dev);

	switch (which) {
	case PCIB_IVAR_BUS:
		sc->busno = result;
		return (0);
	}

	return (ENOENT);
}

static struct resource *
versatile_pci_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{

	struct versatile_pci_softc *sc = device_get_softc(bus);
	struct resource *rv;
	struct rman *rm;

	dprintf("Alloc resources %d, %08lx..%08lx, %ld\n", type, start, end, count);

	switch (type) {
	case SYS_RES_IOPORT:
		rm = &sc->io_rman;
		break;
	case SYS_RES_IRQ:
		rm = NULL;
		break;
	case SYS_RES_MEMORY:
		rm = &sc->mem_rman;
		break;
	default:
		return (NULL);
	}

	if (rm == NULL)
		return (BUS_ALLOC_RESOURCE(device_get_parent(bus),
		    child, type, rid, start, end, count, flags));

	rv = rman_reserve_resource(rm, start, end, count, flags, child);
	if (rv == NULL)
		return (NULL);

	rman_set_rid(rv, *rid);

	if (flags & RF_ACTIVE) {
		if (bus_activate_resource(child, type, *rid, rv)) {
			rman_release_resource(rv);
			return (NULL);
		}
	}
	return (rv);
}

static int
versatile_pci_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{
	vm_offset_t vaddr;
	int res;

	switch(type) {
	case SYS_RES_MEMORY:
	case SYS_RES_IOPORT:
		vaddr = (vm_offset_t)pmap_mapdev(rman_get_start(r),
				rman_get_size(r));
		rman_set_bushandle(r, vaddr);
		rman_set_bustag(r, fdtbus_bs_tag);
		res = rman_activate_resource(r);
		break;
	case SYS_RES_IRQ:
		res = (BUS_ACTIVATE_RESOURCE(device_get_parent(bus),
		    child, type, rid, r));
		break;
	default:
		res = ENXIO;
		break;
	}

	return (res);
}

static int
versatile_pci_setup_intr(device_t bus, device_t child, struct resource *ires,
	    int flags, driver_filter_t *filt, driver_intr_t *handler,
	    void *arg, void **cookiep)
{

	return BUS_SETUP_INTR(device_get_parent(bus), bus, ires, flags,
	    filt, handler, arg, cookiep);
}

static int
versatile_pci_teardown_intr(device_t dev, device_t child, struct resource *ires,
    void *cookie)
{

	return BUS_TEARDOWN_INTR(device_get_parent(dev), dev, ires, cookie);
}

static int
versatile_pci_maxslots(device_t dev)
{

	return (PCI_SLOTMAX);
}

static int
versatile_pci_route_interrupt(device_t bus, device_t dev, int pin)
{
	struct versatile_pci_softc *sc;
	struct ofw_pci_register reg;
	uint32_t pintr, mintr[4];
	phandle_t iparent;
	int intrcells;

	sc = device_get_softc(bus);
	pintr = pin;

	bzero(&reg, sizeof(reg));
	reg.phys_hi = (pci_get_bus(dev) << OFW_PCI_PHYS_HI_BUSSHIFT) |
	    (pci_get_slot(dev) << OFW_PCI_PHYS_HI_DEVICESHIFT) |
	    (pci_get_function(dev) << OFW_PCI_PHYS_HI_FUNCTIONSHIFT);

	intrcells = ofw_bus_lookup_imap(ofw_bus_get_node(dev),
	    &sc->pci_iinfo, &reg, sizeof(reg), &pintr, sizeof(pintr),
	    mintr, sizeof(mintr), &iparent);
	if (intrcells) {
		pintr = ofw_bus_map_intr(dev, iparent, intrcells, mintr);
		return (pintr);
	}

	device_printf(bus, "could not route pin %d for device %d.%d\n",
	    pin, pci_get_slot(dev), pci_get_function(dev));
	return (PCI_INVALID_IRQ);
}

static uint32_t
versatile_pci_read_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, int bytes)
{
	struct versatile_pci_softc *sc = device_get_softc(dev);
	uint32_t data;
	uint32_t shift, mask;
	uint32_t addr;

	if (sc->pcib_slot == slot) {
		switch (bytes) {
			case 4: 
				return (0xffffffff);
				break;
			case 2:
				return (0xffff);
				break;
			case 1:
				return (0xff);
				break;
		}
	}

	addr = (bus << 16) | (slot << 11) | (func << 8) | (reg & ~3);

	/* register access is 32-bit aligned */
	shift = (reg & 3) * 8;

	/* Create a mask based on the width, post-shift */
	if (bytes == 2)
		mask = 0xffff;
	else if (bytes == 1)
		mask = 0xff;
	else
		mask = 0xffffffff;

	dprintf("%s: tag (%x, %x, %x) reg %d(%d)\n", __func__, bus, slot, 
	    func, reg, bytes);

	mtx_lock_spin(&sc->mtx);
	data = versatile_pci_conf_read_4(addr);
	mtx_unlock_spin(&sc->mtx);

	/* get request bytes from 32-bit word */
	data = (data >> shift) & mask;

	dprintf("%s: read 0x%x\n", __func__, data);

	return (data);
}

static void
versatile_pci_write_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, uint32_t data, int bytes)
{

	struct versatile_pci_softc *sc = device_get_softc(dev);
	uint32_t addr;

	dprintf("%s: tag (%x, %x, %x) reg %d(%d)\n", __func__, bus, slot,
	    func, reg, bytes);

	if (sc->pcib_slot == slot)
		return;

	addr = (bus << 16) | (slot << 11) | (func << 8) | reg;
	mtx_lock_spin(&sc->mtx);
	switch (bytes) {
		case 4: 
			versatile_pci_conf_write_4(addr, data);
			break;
		case 2:
			versatile_pci_conf_write_2(addr, data);
			break;
		case 1:
			versatile_pci_conf_write_1(addr, data);
			break;
	}
	mtx_unlock_spin(&sc->mtx);
}

static device_method_t versatile_pci_methods[] = {
	DEVMETHOD(device_probe,		versatile_pci_probe),
	DEVMETHOD(device_attach,	versatile_pci_attach),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	versatile_pci_read_ivar),
	DEVMETHOD(bus_write_ivar,	versatile_pci_write_ivar),
	DEVMETHOD(bus_alloc_resource,	versatile_pci_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource, versatile_pci_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	versatile_pci_setup_intr),
	DEVMETHOD(bus_teardown_intr,	versatile_pci_teardown_intr),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,	versatile_pci_maxslots),
	DEVMETHOD(pcib_read_config,	versatile_pci_read_config),
	DEVMETHOD(pcib_write_config,	versatile_pci_write_config),
	DEVMETHOD(pcib_route_interrupt,	versatile_pci_route_interrupt),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

	DEVMETHOD_END
};

static driver_t versatile_pci_driver = {
	"pcib",
	versatile_pci_methods,
	sizeof(struct versatile_pci_softc),
};

static devclass_t versatile_pci_devclass;

DRIVER_MODULE(versatile_pci, simplebus, versatile_pci_driver, versatile_pci_devclass, 0, 0);
