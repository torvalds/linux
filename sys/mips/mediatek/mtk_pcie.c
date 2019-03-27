/*-
 * Copyright (c) 2016 Stanislav Galabov.
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
#include <sys/interrupt.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/endian.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/pmap.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/pci/pcib_private.h>

#include <dev/fdt/fdt_common.h>
#include <dev/fdt/fdt_clock.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <mips/mediatek/mtk_pcie.h>
#include <mips/mediatek/mtk_soc.h>
#include <mips/mediatek/mtk_sysctl.h>
#include <mips/mediatek/fdt_reset.h>

#include "ofw_bus_if.h"
#include "pcib_if.h"
#include "pic_if.h"

/*
 * Note: We only support PCIe at the moment.
 * Most SoCs in the Ralink/Mediatek family that we target actually don't
 * support PCI anyway, with the notable exceptions being RT3662/RT3883, which
 * support both PCI and PCIe. If there exists a board based on one of them
 * which is of interest in the future it shouldn't be too hard to enable PCI
 * support for it.
 */

/* Chip specific function declarations */
static int  mtk_pcie_phy_init(device_t);
static int  mtk_pcie_phy_start(device_t);
static int  mtk_pcie_phy_stop(device_t);
static int  mtk_pcie_phy_mt7621_init(device_t);
static int  mtk_pcie_phy_mt7628_init(device_t);
static int  mtk_pcie_phy_mt7620_init(device_t);
static int  mtk_pcie_phy_rt3883_init(device_t);
static void mtk_pcie_phy_setup_slots(device_t);

/* Generic declarations */
struct mtx mtk_pci_mtx;
MTX_SYSINIT(mtk_pci_mtx, &mtk_pci_mtx, "MTK PCIe mutex", MTX_SPIN);

static int mtk_pci_intr(void *);

static struct mtk_pci_softc *mt_sc = NULL;

struct mtk_pci_range {
	u_long	base;
	u_long	len;
};

#define FDT_RANGES_CELLS	((1 + 2 + 3) * 2)

static void
mtk_pci_range_dump(struct mtk_pci_range *range)
{
#ifdef DEBUG
	printf("\n");
	printf("  base = 0x%08lx\n", range->base);
	printf("  len  = 0x%08lx\n", range->len);
#endif
}

static int
mtk_pci_ranges_decode(phandle_t node, struct mtk_pci_range *io_space,
    struct mtk_pci_range *mem_space)
{
	struct mtk_pci_range *pci_space;
	pcell_t ranges[FDT_RANGES_CELLS];
	pcell_t addr_cells, size_cells, par_addr_cells;
	pcell_t *rangesptr;
	pcell_t cell0, cell1, cell2;
	int tuple_size, tuples, i, rv, len;

	/*
	 * Retrieve 'ranges' property.
	 */
	if ((fdt_addrsize_cells(node, &addr_cells, &size_cells)) != 0)
		return (EINVAL);
	if (addr_cells != 3 || size_cells != 2)
		return (ERANGE);

	par_addr_cells = fdt_parent_addr_cells(node);
	if (par_addr_cells != 1)
		return (ERANGE);

	len = OF_getproplen(node, "ranges");
	if (len > sizeof(ranges))
		return (ENOMEM);

	if (OF_getprop(node, "ranges", ranges, sizeof(ranges)) <= 0)
		return (EINVAL);

	tuple_size = sizeof(pcell_t) * (addr_cells + par_addr_cells +
	    size_cells);
	tuples = len / tuple_size;

	/*
	 * Initialize the ranges so that we don't have to worry about
	 * having them all defined in the FDT. In particular, it is
	 * perfectly fine not to want I/O space on PCI busses.
	 */
	bzero(io_space, sizeof(*io_space));
	bzero(mem_space, sizeof(*mem_space));

	rangesptr = &ranges[0];
	for (i = 0; i < tuples; i++) {
		cell0 = fdt_data_get((void *)rangesptr, 1);
		rangesptr++;
		cell1 = fdt_data_get((void *)rangesptr, 1);
		rangesptr++;
		cell2 = fdt_data_get((void *)rangesptr, 1);
		rangesptr++;

		if (cell0 & 0x02000000) {
			pci_space = mem_space;
		} else if (cell0 & 0x01000000) {
			pci_space = io_space;
		} else {
			rv = ERANGE;
			goto out;
		}

		pci_space->base = fdt_data_get((void *)rangesptr,
		    par_addr_cells);
		rangesptr += par_addr_cells;

		pci_space->len = fdt_data_get((void *)rangesptr, size_cells);
		rangesptr += size_cells;
	}

	rv = 0;
out:
	return (rv);
}

static int
mtk_pci_ranges(phandle_t node, struct mtk_pci_range *io_space,
    struct mtk_pci_range *mem_space)
{
	int err;

	if ((err = mtk_pci_ranges_decode(node, io_space, mem_space)) != 0) {
		return (err);
	}

	mtk_pci_range_dump(io_space);
	mtk_pci_range_dump(mem_space);

	return (0);
}

static struct ofw_compat_data compat_data[] = {
	{ "ralink,rt3883-pci",		MTK_SOC_RT3883 },
	{ "mediatek,mt7620-pci",	MTK_SOC_MT7620A },
	{ "mediatek,mt7628-pci",	MTK_SOC_MT7628 },
	{ "mediatek,mt7621-pci",	MTK_SOC_MT7621 },
	{ NULL,				MTK_SOC_UNKNOWN }
};

static int
mtk_pci_probe(device_t dev)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	sc->socid = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	if (sc->socid == MTK_SOC_UNKNOWN)
		return (ENXIO);

	device_set_desc(dev, "MTK PCIe Controller");

	return (0);
}

static int
mtk_pci_attach(device_t dev)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);
	struct mtk_pci_range io_space, mem_space;
	phandle_t node;
	intptr_t xref;
	int i, rid;

	sc->sc_dev = dev;
	mt_sc = sc;
	sc->addr_mask = 0xffffffff;

	/* Request our memory */
	rid = 0;
	sc->pci_res[0] = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
			    RF_ACTIVE);
	if (sc->pci_res[0] == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	/* See how many interrupts we need */
	if (sc->socid == MTK_SOC_MT7621)
		sc->sc_num_irq = 3;
	else {
		sc->sc_num_irq = 1;
		sc->pci_res[2] = sc->pci_res[3] = NULL;
		sc->pci_intrhand[1] = sc->pci_intrhand[2] = NULL;
	}

	/* Request our interrupts */	
	for (i = 1; i <= sc->sc_num_irq ; i++) {
		rid = i - 1;
		sc->pci_res[i] = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
				     RF_ACTIVE);
		if (sc->pci_res[i] == NULL) {
			device_printf(dev, "could not allocate interrupt "
			    "resource %d\n", rid);
			goto cleanup_res;
		}
	}

	/* Parse our PCI 'ranges' property */
	node = ofw_bus_get_node(dev);
	xref = OF_xref_from_node(node);
	if (mtk_pci_ranges(node, &io_space, &mem_space)) {
		device_printf(dev, "could not retrieve 'ranges' data\n");
		goto cleanup_res;
	}

	/* Memory, I/O and IRQ resource limits */
	sc->sc_io_base = io_space.base;
	sc->sc_io_size = io_space.len;
	sc->sc_mem_base = mem_space.base;
	sc->sc_mem_size = mem_space.len;
	sc->sc_irq_start = MTK_PCIE0_IRQ;
	sc->sc_irq_end = MTK_PCIE2_IRQ;

	/* Init resource managers for memory, I/O and IRQ */
	sc->sc_mem_rman.rm_type = RMAN_ARRAY;
	sc->sc_mem_rman.rm_descr = "mtk pcie memory window";
	if (rman_init(&sc->sc_mem_rman) != 0 ||
	    rman_manage_region(&sc->sc_mem_rman, sc->sc_mem_base,
	    sc->sc_mem_base + sc->sc_mem_size - 1) != 0) {
		device_printf(dev, "failed to setup memory rman\n");
		goto cleanup_res;
	}

	sc->sc_io_rman.rm_type = RMAN_ARRAY;
	sc->sc_io_rman.rm_descr = "mtk pcie io window";
	if (rman_init(&sc->sc_io_rman) != 0 ||
	    rman_manage_region(&sc->sc_io_rman, sc->sc_io_base,
	    sc->sc_io_base + sc->sc_io_size - 1) != 0) {
		device_printf(dev, "failed to setup io rman\n");
		goto cleanup_res;
	}

	sc->sc_irq_rman.rm_type = RMAN_ARRAY;
	sc->sc_irq_rman.rm_descr = "mtk pcie irqs";
	if (rman_init(&sc->sc_irq_rman) != 0 ||
	    rman_manage_region(&sc->sc_irq_rman, sc->sc_irq_start,
	    sc->sc_irq_end) != 0) {
		device_printf(dev, "failed to setup irq rman\n");
		goto cleanup_res;
	}

	/* Do SoC-specific PCIe initialization */
	if (mtk_pcie_phy_init(dev)) {
		device_printf(dev, "pcie phy init failed\n");
		goto cleanup_rman;
	}

	/* Register ourselves as an interrupt controller */
	if (intr_pic_register(dev, xref) == NULL) {
		device_printf(dev, "could not register PIC\n");
		goto cleanup_rman;
	}

	/* Set up our interrupt handler */
	for (i = 1; i <= sc->sc_num_irq; i++) {
		sc->pci_intrhand[i - 1] = NULL;
		if (bus_setup_intr(dev, sc->pci_res[i], INTR_TYPE_MISC,
		    mtk_pci_intr, NULL, sc, &sc->pci_intrhand[i - 1])) {
			device_printf(dev, "could not setup intr handler %d\n",
			    i);
			goto cleanup;
		}
	}

	/* Attach our PCI child so bus enumeration can start */
	if (device_add_child(dev, "pci", -1) == NULL) {
		device_printf(dev, "could not attach pci bus\n");
		goto cleanup;
	}

	/* And finally, attach ourselves to the bus */
	if (bus_generic_attach(dev)) {
		device_printf(dev, "could not attach to bus\n");
		goto cleanup;
	}

	return (0);

cleanup:
#ifdef notyet
	intr_pic_unregister(dev, xref);
#endif
	for (i = 1; i <= sc->sc_num_irq; i++) {
		if (sc->pci_intrhand[i - 1] != NULL)
			bus_teardown_intr(dev, sc->pci_res[i],
			    sc->pci_intrhand[i - 1]);
	}
cleanup_rman:
	mtk_pcie_phy_stop(dev);
	rman_fini(&sc->sc_irq_rman);
	rman_fini(&sc->sc_io_rman);
	rman_fini(&sc->sc_mem_rman);
cleanup_res:
	mt_sc = NULL;
	if (sc->pci_res[0] != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, 0, sc->pci_res[0]);
	if (sc->pci_res[1] != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 0, sc->pci_res[1]);
	if (sc->pci_res[2] != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 1, sc->pci_res[2]);
	if (sc->pci_res[3] != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, 2, sc->pci_res[3]);
	return (ENXIO);
}

static int
mtk_pci_read_ivar(device_t dev, device_t child, int which,
	uintptr_t *result)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);

	switch (which) {
	case PCIB_IVAR_DOMAIN:
		*result = device_get_unit(dev);
		return (0);
	case PCIB_IVAR_BUS:
		*result = sc->sc_busno;
		return (0);
	}

	return (ENOENT);
}

static int
mtk_pci_write_ivar(device_t dev, device_t child, int which,
	uintptr_t result)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);

	switch (which) {
	case PCIB_IVAR_BUS:
		sc->sc_busno = result;
		return (0);
	}

	return (ENOENT);
}

static struct resource *
mtk_pci_alloc_resource(device_t bus, device_t child, int type, int *rid,
	rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct mtk_pci_softc *sc = device_get_softc(bus);
	struct resource *rv;
	struct rman *rm;

	switch (type) {
	case PCI_RES_BUS:
		return pci_domain_alloc_bus(0, child, rid, start, end, count,
					    flags);
	case SYS_RES_IRQ:
		rm = &sc->sc_irq_rman;
		break;
	case SYS_RES_IOPORT:
		rm = &sc->sc_io_rman;
		break;
	case SYS_RES_MEMORY:
		rm = &sc->sc_mem_rman;
		break;
	default:
		return (NULL);
	}

	rv = rman_reserve_resource(rm, start, end, count, flags, child);

	if (rv == NULL)
		return (NULL);

	rman_set_rid(rv, *rid);

	if ((flags & RF_ACTIVE) && type != SYS_RES_IRQ) {
		if (bus_activate_resource(child, type, *rid, rv)) {
			rman_release_resource(rv);
			return (NULL);
		}
	}

	return (rv);
}

static int
mtk_pci_release_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res)
{

	if (type == PCI_RES_BUS)
		return (pci_domain_release_bus(0, child, rid, res));

	return (bus_generic_release_resource(bus, child, type, rid, res));
}

static int
mtk_pci_adjust_resource(device_t bus, device_t child, int type,
    struct resource *res, rman_res_t start, rman_res_t end)
{
	struct mtk_pci_softc *sc = device_get_softc(bus);
	struct rman *rm;

	switch (type) {
	case PCI_RES_BUS:
		return pci_domain_adjust_bus(0, child, res, start, end);
	case SYS_RES_IRQ:
		rm = &sc->sc_irq_rman;
		break;
	case SYS_RES_IOPORT:
		rm = &sc->sc_io_rman;
		break;
	case SYS_RES_MEMORY:
		rm = &sc->sc_mem_rman;
		break;
	default:
		rm = NULL;
		break;
	}

	if (rm != NULL)
		return (rman_adjust_resource(res, start, end));

	return (bus_generic_adjust_resource(bus, child, type, res, start, end));
}

static inline int
mtk_idx_to_irq(int idx)
{

	return ((idx == 0) ? MTK_PCIE0_IRQ :
		(idx == 1) ? MTK_PCIE1_IRQ :
		(idx == 2) ? MTK_PCIE2_IRQ : -1);
}

static inline int
mtk_irq_to_idx(int irq)
{

	return ((irq == MTK_PCIE0_IRQ) ? 0 :
		(irq == MTK_PCIE1_IRQ) ? 1 :
		(irq == MTK_PCIE2_IRQ) ? 2 : -1);
}

static void
mtk_pci_mask_irq(void *source)
{
	MT_WRITE32(mt_sc, MTK_PCI_PCIENA,
		MT_READ32(mt_sc, MTK_PCI_PCIENA) & ~(1<<((int)source)));
}

static void
mtk_pci_unmask_irq(void *source)
{

	MT_WRITE32(mt_sc, MTK_PCI_PCIENA,
		MT_READ32(mt_sc, MTK_PCI_PCIENA) | (1<<((int)source)));
}

static int
mtk_pci_setup_intr(device_t bus, device_t child, struct resource *ires,
	int flags, driver_filter_t *filt, driver_intr_t *handler,
	void *arg, void **cookiep)
{
	struct mtk_pci_softc *sc = device_get_softc(bus);
	struct intr_event *event;
	int irq, error, irqidx;

	irq = rman_get_start(ires);

	if (irq < sc->sc_irq_start || irq > sc->sc_irq_end)
		return (EINVAL);

	irqidx = irq - sc->sc_irq_start;

	event = sc->sc_eventstab[irqidx];
	if (event == NULL) {
		error = intr_event_create(&event, (void *)irq, 0, irq,
		    mtk_pci_mask_irq, mtk_pci_unmask_irq, NULL, NULL,
		    "pci intr%d:", irq);

		if (error == 0) {
			sc->sc_eventstab[irqidx] = event;
		}
		else {
			return (error);
		}
	}

	intr_event_add_handler(event, device_get_nameunit(child), filt,
		handler, arg, intr_priority(flags), flags, cookiep);

	mtk_pci_unmask_irq((void*)irq);

	return (0);
}

static int
mtk_pci_teardown_intr(device_t dev, device_t child, struct resource *ires,
	void *cookie)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);
	int irq, result, irqidx;

	irq = rman_get_start(ires);
	if (irq < sc->sc_irq_start || irq > sc->sc_irq_end)
		return (EINVAL);

	irqidx = irq - sc->sc_irq_start;
	if (sc->sc_eventstab[irqidx] == NULL)
		panic("Trying to teardown unoccupied IRQ");

	mtk_pci_mask_irq((void*)irq);

	result = intr_event_remove_handler(cookie);
	if (!result)
		sc->sc_eventstab[irqidx] = NULL;
	

	return (result);
}

static inline uint32_t
mtk_pci_make_addr(int bus, int slot, int func, int reg)
{
	uint32_t addr;

	addr = ((((reg & 0xf00) >> 8) << 24) | (bus << 16) | (slot << 11) |
		(func << 8) | (reg & 0xfc) | (1 << 31));

	return (addr);
}

static int
mtk_pci_maxslots(device_t dev)
{

	return (PCI_SLOTMAX);
}

static inline int
mtk_pci_slot_has_link(device_t dev, int slot)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);

	return !!(sc->pcie_link_status & (1<<slot));
}

static uint32_t
mtk_pci_read_config(device_t dev, u_int bus, u_int slot, u_int func,
	u_int reg, int bytes)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);
	uint32_t addr = 0, data = 0;

	/* Return ~0U if slot has no link */
	if (bus == 0 && mtk_pci_slot_has_link(dev, slot) == 0) {
		return (~0U);
	}

	mtx_lock_spin(&mtk_pci_mtx);
	addr = mtk_pci_make_addr(bus, slot, func, (reg & ~3)) & sc->addr_mask;
	MT_WRITE32(sc, MTK_PCI_CFGADDR, addr);
	switch (bytes % 4) {
	case 0:
		data = MT_READ32(sc, MTK_PCI_CFGDATA);
		break;
	case 1:
		data = MT_READ8(sc, MTK_PCI_CFGDATA + (reg & 0x3));
		break;
	case 2:
		data = MT_READ16(sc, MTK_PCI_CFGDATA + (reg & 0x3));
		break;
	default:
		panic("%s(): Wrong number of bytes (%d) requested!\n",
			__FUNCTION__, bytes % 4);
	}
	mtx_unlock_spin(&mtk_pci_mtx);

	return (data);
}

static void
mtk_pci_write_config(device_t dev, u_int bus, u_int slot, u_int func,
	u_int reg, uint32_t val, int bytes)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);
	uint32_t addr = 0, data = val;

	/* Do not write if slot has no link */
	if (bus == 0 && mtk_pci_slot_has_link(dev, slot) == 0)
		return;

	mtx_lock_spin(&mtk_pci_mtx);
	addr = mtk_pci_make_addr(bus, slot, func, (reg & ~3)) & sc->addr_mask;
	MT_WRITE32(sc, MTK_PCI_CFGADDR, addr);
	switch (bytes % 4) {
	case 0:
		MT_WRITE32(sc, MTK_PCI_CFGDATA, data);
		break;
	case 1:
		MT_WRITE8(sc, MTK_PCI_CFGDATA + (reg & 0x3), data);
		break;
	case 2:
		MT_WRITE16(sc, MTK_PCI_CFGDATA + (reg & 0x3), data);
		break;
	default:
		panic("%s(): Wrong number of bytes (%d) requested!\n",
			__FUNCTION__, bytes % 4);
	}
	mtx_unlock_spin(&mtk_pci_mtx);
}

static int
mtk_pci_route_interrupt(device_t pcib, device_t device, int pin)
{
	int bus, sl, dev;

	bus = pci_get_bus(device);
	sl = pci_get_slot(device);
	dev = pci_get_device(device);

	if (bus != 0)
		panic("Unexpected bus number %d\n", bus);

	/* PCIe only */
	switch (sl) {
	case 0: return MTK_PCIE0_IRQ;
	case 1: return MTK_PCIE0_IRQ + 1;
	case 2: return MTK_PCIE0_IRQ + 2;
	default: return (-1);
	}

	return (-1);
}

static device_method_t mtk_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mtk_pci_probe),
	DEVMETHOD(device_attach,	mtk_pci_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	mtk_pci_read_ivar),
	DEVMETHOD(bus_write_ivar,	mtk_pci_write_ivar),
	DEVMETHOD(bus_alloc_resource,	mtk_pci_alloc_resource),
	DEVMETHOD(bus_release_resource,	mtk_pci_release_resource),
	DEVMETHOD(bus_adjust_resource,	mtk_pci_adjust_resource),
	DEVMETHOD(bus_activate_resource,   bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	mtk_pci_setup_intr),
	DEVMETHOD(bus_teardown_intr,	mtk_pci_teardown_intr),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,	mtk_pci_maxslots),
	DEVMETHOD(pcib_read_config,	mtk_pci_read_config),
	DEVMETHOD(pcib_write_config,	mtk_pci_write_config),
	DEVMETHOD(pcib_route_interrupt,	mtk_pci_route_interrupt),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

	/* OFW bus interface */
	DEVMETHOD(ofw_bus_get_compat,	ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,	ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,	ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,	ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,	ofw_bus_gen_get_type),

	DEVMETHOD_END
};

static driver_t mtk_pci_driver = {
	"pcib",
	mtk_pci_methods,
	sizeof(struct mtk_pci_softc),
};

static devclass_t mtk_pci_devclass;

DRIVER_MODULE(mtk_pci, simplebus, mtk_pci_driver, mtk_pci_devclass, 0, 0);

/* Our interrupt handler */
static int
mtk_pci_intr(void *arg)
{
	struct mtk_pci_softc *sc = arg;
	struct intr_event *event;
	uint32_t reg, irq, irqidx;

	reg = MT_READ32(sc, MTK_PCI_PCIINT);

	for (irq = sc->sc_irq_start; irq <= sc->sc_irq_end; irq++) {
		if (reg & (1u<<irq)) {
			irqidx = irq - sc->sc_irq_start;
			event = sc->sc_eventstab[irqidx];
			if (!event || CK_SLIST_EMPTY(&event->ie_handlers)) {
				if (irq != 0)
					printf("Stray PCI IRQ %d\n", irq);
				continue;
			}

			intr_event_handle(event, NULL);
		}
	}

	return (FILTER_HANDLED);
}

/* PCIe SoC-specific initialization */
static int
mtk_pcie_phy_init(device_t dev)
{
	struct mtk_pci_softc *sc;

	/* Get our softc */
	sc = device_get_softc(dev);

	/* We don't know how many slots we have yet */
	sc->num_slots = 0;

	/* Handle SoC specific PCIe init */
	switch (sc->socid) {
	case MTK_SOC_MT7628: /* Fallthrough */
	case MTK_SOC_MT7688:
		if (mtk_pcie_phy_mt7628_init(dev))
			return (ENXIO);
		break;
	case MTK_SOC_MT7621:
		if (mtk_pcie_phy_mt7621_init(dev))
			return (ENXIO);
		break;
	case MTK_SOC_MT7620A:
		if (mtk_pcie_phy_mt7620_init(dev))
			return (ENXIO);
		break;
	case MTK_SOC_RT3662: /* Fallthrough */
	case MTK_SOC_RT3883:
		if (mtk_pcie_phy_rt3883_init(dev))
			return (ENXIO);
		break;
	default:
		device_printf(dev, "unsupported device %x\n", sc->socid);
		return (ENXIO);
	}

	/*
	 * If we were successful so far go and set up the PCIe slots, so we
	 * may allocate mem/io/irq resources and enumerate busses later.
	 */
	mtk_pcie_phy_setup_slots(dev);

	return (0);
}

static int
mtk_pcie_phy_start(device_t dev)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);

	if (sc->socid == MTK_SOC_MT7621 &&
	    (mtk_sysctl_get(SYSCTL_REVID) & SYSCTL_REVID_MASK) !=
	    SYSCTL_MT7621_REV_E) {
		if (fdt_reset_assert_all(dev))
			return (ENXIO);
	} else {
		if (fdt_reset_deassert_all(dev))
			return (ENXIO);
	}

	if (fdt_clock_enable_all(dev))
		return (ENXIO);

	return (0);
}

static int
mtk_pcie_phy_stop(device_t dev)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);

	if (sc->socid == MTK_SOC_MT7621 &&
	    (mtk_sysctl_get(SYSCTL_REVID) & SYSCTL_REVID_MASK) !=
	    SYSCTL_MT7621_REV_E) {
		if (fdt_reset_deassert_all(dev))
			return (ENXIO);
	} else {
		if (fdt_reset_assert_all(dev))
			return (ENXIO);
	}

	if (fdt_clock_disable_all(dev))
		return (ENXIO);

	return (0);
}

#define mtk_pcie_phy_set(_sc, _reg, _s, _n, _v)			\
	MT_WRITE32((_sc), (_reg), ((MT_READ32((_sc), (_reg)) &	\
	    (~(((1ull << (_n)) - 1) << (_s)))) | ((_v) << (_s))))

static void
mtk_pcie_phy_mt7621_bypass_pipe_rst(struct mtk_pci_softc *sc, uint32_t off)
{

	mtk_pcie_phy_set(sc, off + 0x002c, 12, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x002c,  4, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x012c, 12, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x012c,  4, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x102c, 12, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x102c,  4, 1, 1);
}

static void
mtk_pcie_phy_mt7621_setup_ssc(struct mtk_pci_softc *sc, uint32_t off)
{
	uint32_t xtal_sel;

	xtal_sel = mtk_sysctl_get(SYSCTL_SYSCFG) >> 6;
	xtal_sel &= 0x7;

	mtk_pcie_phy_set(sc, off + 0x400, 8, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x400, 9, 2, 0);
	mtk_pcie_phy_set(sc, off + 0x000, 4, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x100, 4, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x000, 5, 1, 0);
	mtk_pcie_phy_set(sc, off + 0x100, 5, 1, 0);

	if (xtal_sel <= 5 && xtal_sel >= 3) {
		mtk_pcie_phy_set(sc, off + 0x490,  6,  2, 1);
		mtk_pcie_phy_set(sc, off + 0x4a8,  0, 12, 0x1a);
		mtk_pcie_phy_set(sc, off + 0x4a8, 16, 12, 0x1a);
	} else {
		mtk_pcie_phy_set(sc, off + 0x490,  6,  2, 0);
		if (xtal_sel >= 6) {
			mtk_pcie_phy_set(sc, off + 0x4bc,  4,  2, 0x01);
			mtk_pcie_phy_set(sc, off + 0x49c,  0, 31, 0x18000000);
			mtk_pcie_phy_set(sc, off + 0x4a4,  0, 16, 0x18d);
			mtk_pcie_phy_set(sc, off + 0x4a8,  0, 12, 0x4a);
			mtk_pcie_phy_set(sc, off + 0x4a8, 16, 12, 0x4a);
			mtk_pcie_phy_set(sc, off + 0x4a8,  0, 12, 0x11);
			mtk_pcie_phy_set(sc, off + 0x4a8, 16, 12, 0x11);
		} else {
			mtk_pcie_phy_set(sc, off + 0x4a8,  0, 12, 0x1a);
			mtk_pcie_phy_set(sc, off + 0x4a8, 16, 12, 0x1a);
		}
	}

	mtk_pcie_phy_set(sc, off + 0x4a0,  5, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x490, 22, 2, 2);
	mtk_pcie_phy_set(sc, off + 0x490, 18, 4, 6);
	mtk_pcie_phy_set(sc, off + 0x490, 12, 4, 2);
	mtk_pcie_phy_set(sc, off + 0x490,  8, 4, 1);
	mtk_pcie_phy_set(sc, off + 0x4ac, 16, 3, 0);
	mtk_pcie_phy_set(sc, off + 0x490,  1, 3, 2);

	if (xtal_sel <= 5 && xtal_sel >= 3) {
		mtk_pcie_phy_set(sc, off + 0x414, 6, 2, 1);
		mtk_pcie_phy_set(sc, off + 0x414, 5, 1, 1);
	}

	mtk_pcie_phy_set(sc, off + 0x414, 28, 2, 1);
	mtk_pcie_phy_set(sc, off + 0x040, 17, 4, 7);
	mtk_pcie_phy_set(sc, off + 0x040, 16, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x140, 17, 4, 7);
	mtk_pcie_phy_set(sc, off + 0x140, 16, 1, 1);

	mtk_pcie_phy_set(sc, off + 0x000,  5, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x100,  5, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x000,  4, 1, 0);
	mtk_pcie_phy_set(sc, off + 0x100,  4, 1, 0);
}

/* XXX: ugly, we need to fix this at some point */
#define MT7621_GPIO_CTRL0	*((volatile uint32_t *)0xbe000600)
#define MT7621_GPIO_DATA0	*((volatile uint32_t *)0xbe000620)

#define mtk_gpio_clr_set(_reg, _clr, _set)		\
	do {						\
		(_reg) = ((_reg) & (_clr)) | (_set);	\
	} while (0)

static int
mtk_pcie_phy_mt7621_init(device_t dev)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);

	/* First off, stop the PHY */
	if (mtk_pcie_phy_stop(dev))
		return (ENXIO);

	/* PCIe resets are GPIO pins */
	mtk_sysctl_clr_set(SYSCTL_GPIOMODE, MT7621_PERST_GPIO_MODE |
	    MT7621_UARTL3_GPIO_MODE, MT7621_PERST_GPIO | MT7621_UARTL3_GPIO);

	/* Set GPIO pins as outputs */
	mtk_gpio_clr_set(MT7621_GPIO_CTRL0, 0, MT7621_PCIE_RST);

	/* Assert resets to PCIe devices */
	mtk_gpio_clr_set(MT7621_GPIO_DATA0, MT7621_PCIE_RST, 0);

	/* Give everything a chance to sink in */
	DELAY(100000);

	/* Now start the PHY again */
	if (mtk_pcie_phy_start(dev))
		return (ENXIO);

	/* Wait for things to settle */
	DELAY(100000);

	/* Only apply below to REV-E hardware */
	if ((mtk_sysctl_get(SYSCTL_REVID) & SYSCTL_REVID_MASK) == 
	    SYSCTL_MT7621_REV_E)
		mtk_pcie_phy_mt7621_bypass_pipe_rst(sc, 0x9000);

	/* Setup PCIe ports 0 and 1 */
	mtk_pcie_phy_mt7621_setup_ssc(sc, 0x9000);
	/* Setup PCIe port 2 */
	mtk_pcie_phy_mt7621_setup_ssc(sc, 0xa000);

	/* Deassert resets to PCIe devices */
	mtk_gpio_clr_set(MT7621_GPIO_DATA0, 0, MT7621_PCIE_RST);

	/* Set number of slots supported */
	sc->num_slots = 3;

	/* Give it a chance to sink in */
	DELAY(100000);

	return (0);
}

static void
mtk_pcie_phy_mt7628_setup(struct mtk_pci_softc *sc, uint32_t off)
{
	uint32_t xtal_sel;

	xtal_sel = mtk_sysctl_get(SYSCTL_SYSCFG) >> 6;
	xtal_sel &= 0x1;

	mtk_pcie_phy_set(sc, off + 0x400,  8, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x400,  9, 2, 0);
	mtk_pcie_phy_set(sc, off + 0x000,  4, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x000,  5, 1, 0);
	mtk_pcie_phy_set(sc, off + 0x4ac, 16, 3, 3);

	if (xtal_sel == 1) {
		mtk_pcie_phy_set(sc, off + 0x4bc, 24,  8, 0x7d);
		mtk_pcie_phy_set(sc, off + 0x490, 12,  4, 0x08);
		mtk_pcie_phy_set(sc, off + 0x490,  6,  2, 0x01);
		mtk_pcie_phy_set(sc, off + 0x4c0,  0, 32, 0x1f400000);
		mtk_pcie_phy_set(sc, off + 0x4a4,  0, 16, 0x013d);
		mtk_pcie_phy_set(sc, off + 0x4a8, 16, 16, 0x74);
		mtk_pcie_phy_set(sc, off + 0x4a8,  0, 16, 0x74);
	} else {
		mtk_pcie_phy_set(sc, off + 0x4bc, 24,  8, 0x64);
		mtk_pcie_phy_set(sc, off + 0x490, 12,  4, 0x0a);
		mtk_pcie_phy_set(sc, off + 0x490,  6,  2, 0x00);
		mtk_pcie_phy_set(sc, off + 0x4c0,  0, 32, 0x19000000);
		mtk_pcie_phy_set(sc, off + 0x4a4,  0, 16, 0x018d);
		mtk_pcie_phy_set(sc, off + 0x4a8, 16, 16, 0x4a);
		mtk_pcie_phy_set(sc, off + 0x4a8,  0, 16, 0x4a);
	}

	mtk_pcie_phy_set(sc, off + 0x498, 0, 8, 5);
	mtk_pcie_phy_set(sc, off + 0x000, 5, 1, 1);
	mtk_pcie_phy_set(sc, off + 0x000, 4, 1, 0);
}

static int
mtk_pcie_phy_mt7628_init(device_t dev)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);

	/* Set PCIe reset to normal mode */
	mtk_sysctl_clr_set(SYSCTL_GPIOMODE, MT7628_PERST_GPIO_MODE,
	    MT7628_PERST);

	/* Start the PHY */
	if (mtk_pcie_phy_start(dev))
		return (ENXIO);

	/* Give it a chance to sink in */
	DELAY(100000);

	/* Setup the PHY */
	mtk_pcie_phy_mt7628_setup(sc, 0x9000);

	/* Deassert PCIe device reset */
	MT_CLR_SET32(sc, MTK_PCI_PCICFG, MTK_PCI_RESET, 0);

	/* Set number of slots supported */
	sc->num_slots = 1;

	return (0);
}

static int
mtk_pcie_phy_mt7620_wait_busy(struct mtk_pci_softc *sc)
{
	uint32_t reg_value, retry;

	reg_value = retry = 0;

	while (retry++ < MT7620_MAX_RETRIES) {
		reg_value = MT_READ32(sc, MT7620_PCIE_PHY_CFG);
		if (reg_value & PHY_BUSY)
			DELAY(100000);
		else
			break;
	}

	if (retry >= MT7620_MAX_RETRIES)
		return (ENXIO);

	return (0);
}

static int
mtk_pcie_phy_mt7620_set(struct mtk_pci_softc *sc, uint32_t reg,
    uint32_t val)
{
	uint32_t reg_val;

	if (mtk_pcie_phy_mt7620_wait_busy(sc))
		return (ENXIO);

	reg_val = PHY_MODE_WRITE | ((reg & 0xff) << PHY_ADDR_OFFSET) |
	    (val & 0xff);
	MT_WRITE32(sc, MT7620_PCIE_PHY_CFG, reg_val);
	DELAY(1000);

	if (mtk_pcie_phy_mt7620_wait_busy(sc))
		return (ENXIO);

	return (0);
}

static int
mtk_pcie_phy_mt7620_init(device_t dev)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);

	/*
	 * The below sets the PCIe PHY to bypass the PCIe DLL and enables
	 * "elastic buffer control", whatever that may be...
	 */
	if (mtk_pcie_phy_mt7620_set(sc, 0x00, 0x80) ||
	    mtk_pcie_phy_mt7620_set(sc, 0x01, 0x04) ||
	    mtk_pcie_phy_mt7620_set(sc, 0x68, 0x84))
		return (ENXIO);

	/* Stop PCIe */
	if (mtk_pcie_phy_stop(dev))
		return (ENXIO);

	/* Restore PPLL to a sane state before going on */
	mtk_sysctl_clr_set(MT7620_PPLL_DRV, LC_CKDRVPD, PDRV_SW_SET);

	/* No PCIe on the MT7620N */
	if (!(mtk_sysctl_get(SYSCTL_REVID) & MT7620_PKG_BGA)) {
		device_printf(dev, "PCIe disabled for MT7620N\n");
		mtk_sysctl_clr_set(MT7620_PPLL_CFG0, 0, PPLL_SW_SET);
		mtk_sysctl_clr_set(MT7620_PPLL_CFG1, 0, PPLL_PD);
		return (ENXIO);
	}

	/* PCIe device reset pin is in normal mode */
	mtk_sysctl_clr_set(SYSCTL_GPIOMODE, MT7620_PERST_GPIO_MODE,
	    MT7620_PERST);

	/* Enable PCIe now */
	if (mtk_pcie_phy_start(dev))
		return (ENXIO);

	/* Give it a chance to sink in */
	DELAY(100000);

	/* If PLL is not locked - bail */
	if (!(mtk_sysctl_get(MT7620_PPLL_CFG1) & PPLL_LOCKED)) {
		device_printf(dev, "no PPLL not lock\n");
		mtk_pcie_phy_stop(dev);
		return (ENXIO);
	}

	/* Configure PCIe PLL */
	mtk_sysctl_clr_set(MT7620_PPLL_DRV, LC_CKDRVOHZ | LC_CKDRVHZ,
	    LC_CKDRVPD | PDRV_SW_SET);

	/* and give it a chance to settle */
	DELAY(100000);

	/* Deassert PCIe device reset */
	MT_CLR_SET32(sc, MTK_PCI_PCICFG, MTK_PCI_RESET, 0);

	/* MT7620 supports one PCIe slot */
	sc->num_slots = 1;

	return (0);
}

static int
mtk_pcie_phy_rt3883_init(device_t dev)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);

	/* Enable PCI host mode and PCIe RC mode */
	mtk_sysctl_clr_set(SYSCTL_SYSCFG1, 0, RT3883_PCI_HOST_MODE |
	    RT3883_PCIE_RC_MODE);

	/* Enable PCIe PHY */
	if (mtk_pcie_phy_start(dev))
		return (ENXIO);

	/* Disable PCI, we only support PCIe for now */
	mtk_sysctl_clr_set(SYSCTL_RSTCTRL, 0, RT3883_PCI_RST);
	mtk_sysctl_clr_set(SYSCTL_CLKCFG1, RT3883_PCI_CLK, 0);

	/* Give things a chance to sink in */
	DELAY(500000);

	/* Set PCIe port number to 0 and lift PCIe reset */
	MT_WRITE32(sc, MTK_PCI_PCICFG, 0);

	/* Configure PCI Arbiter */
	MT_WRITE32(sc, MTK_PCI_ARBCTL, 0x79);

	/* We have a single PCIe slot */
	sc->num_slots = 1;

	return (0);
}

static void
mtk_pcie_phy_setup_slots(device_t dev)
{
	struct mtk_pci_softc *sc = device_get_softc(dev);
	uint32_t bar0_val, val;
	int i;

	/* Disable all PCIe interrupts */
	MT_WRITE32(sc, MTK_PCI_PCIENA, 0);

	/* Default bar0_val is 64M, enabled */
	bar0_val = 0x03FF0001;

	/* But we override it to 2G, enabled for some SoCs */
	if (sc->socid == MTK_SOC_MT7620A || sc->socid == MTK_SOC_MT7628 ||
	    sc->socid == MTK_SOC_MT7688 || sc->socid == MTK_SOC_MT7621)
		bar0_val = 0x7FFF0001;

	/* We still don't know which slots have linked up */
	sc->pcie_link_status = 0;

	/* XXX: I am not sure if this delay is really necessary */
	DELAY(500000);

	/*
	 * See which slots have links and mark them.
	 * Set up all slots' BARs and make them look like PCIe bridges.
	 */
	for (i = 0; i < sc->num_slots; i++) {
		/* If slot has link - mark it */
		if (MT_READ32(sc, MTK_PCIE_STATUS(i)) & 1)
			sc->pcie_link_status |= (1<<i);
		else
			continue;

		/* Generic slot configuration follows */

		/* We enable BAR0 */
		MT_WRITE32(sc, MTK_PCIE_BAR0SETUP(i), bar0_val);
		/* and disable BAR1 */
		MT_WRITE32(sc, MTK_PCIE_BAR1SETUP(i), 0);
		/* Internal memory base has no offset */
		MT_WRITE32(sc, MTK_PCIE_IMBASEBAR0(i), 0);
		/* We're a PCIe bridge */
		MT_WRITE32(sc, MTK_PCIE_CLASS(i), 0x06040001);

		val = mtk_pci_read_config(dev, 0, i, 0, 0x4, 4);
		mtk_pci_write_config(dev, 0, i, 0, 0x4, val | 0x4, 4);
		val = mtk_pci_read_config(dev, 0, i, 0, 0x70c, 4);
		val &= ~(0xff << 8);
		val |= (0x50 << 8);
		mtk_pci_write_config(dev, 0, i, 0, 0x70c, val, 4);

		mtk_pci_write_config(dev, 0, i, 0, PCIR_IOBASEL_1, 0xff, 1);
		mtk_pci_write_config(dev, 0, i, 0, PCIR_IOBASEH_1, 0xffff, 2);
		mtk_pci_write_config(dev, 0, i, 0, PCIR_IOLIMITL_1, 0, 1);
		mtk_pci_write_config(dev, 0, i, 0, PCIR_IOLIMITH_1, 0, 2);
		mtk_pci_write_config(dev, 0, i, 0, PCIR_MEMBASE_1, 0xffff, 2);
		mtk_pci_write_config(dev, 0, i, 0, PCIR_MEMLIMIT_1, 0, 2);
		mtk_pci_write_config(dev, 0, i, 0, PCIR_PMBASEL_1, 0xffff, 2);
		mtk_pci_write_config(dev, 0, i, 0, PCIR_PMBASEH_1, 0xffffffff,
		    4);
		mtk_pci_write_config(dev, 0, i, 0, PCIR_PMLIMITL_1, 0, 2);
		mtk_pci_write_config(dev, 0, i, 0, PCIR_PMLIMITH_1, 0, 4);
	}
}
