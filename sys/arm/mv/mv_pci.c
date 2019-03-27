/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2008 MARVELL INTERNATIONAL LTD.
 * Copyright (c) 2010 The FreeBSD Foundation
 * Copyright (c) 2010-2015 Semihalf
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Portions of this software were developed by Semihalf
 * under sponsorship from the FreeBSD Foundation.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Marvell integrated PCI/PCI-Express controller driver.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/queue.h>
#include <sys/bus.h>
#include <sys/rman.h>
#include <sys/endian.h>
#include <sys/devmap.h>

#include <machine/fdt.h>
#include <machine/intr.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcib_private.h>

#include "ofw_bus_if.h"
#include "pcib_if.h"

#include <machine/resource.h>
#include <machine/bus.h>

#include <arm/mv/mvreg.h>
#include <arm/mv/mvvar.h>
#include <arm/mv/mvwin.h>

#ifdef DEBUG
#define debugf(fmt, args...) do { printf(fmt,##args); } while (0)
#else
#define debugf(fmt, args...)
#endif

/*
 * Code and data related to fdt-based PCI configuration.
 *
 * This stuff used to be in dev/fdt/fdt_pci.c and fdt_common.h, but it was
 * always Marvell-specific so that was deleted and the code now lives here.
 */

struct mv_pci_range {
	u_long	base_pci;
	u_long	base_parent;
	u_long	len;
};

#define FDT_RANGES_CELLS	((3 + 3 + 2) * 2)
#define PCI_SPACE_LEN		0x00400000

static void
mv_pci_range_dump(struct mv_pci_range *range)
{
#ifdef DEBUG
	printf("\n");
	printf("  base_pci = 0x%08lx\n", range->base_pci);
	printf("  base_par = 0x%08lx\n", range->base_parent);
	printf("  len      = 0x%08lx\n", range->len);
#endif
}

static int
mv_pci_ranges_decode(phandle_t node, struct mv_pci_range *io_space,
    struct mv_pci_range *mem_space)
{
	pcell_t ranges[FDT_RANGES_CELLS];
	struct mv_pci_range *pci_space;
	pcell_t addr_cells, size_cells, par_addr_cells;
	pcell_t *rangesptr;
	pcell_t cell0, cell1, cell2;
	int tuple_size, tuples, i, rv, offset_cells, len;
	int  portid, is_io_space;

	/*
	 * Retrieve 'ranges' property.
	 */
	if ((fdt_addrsize_cells(node, &addr_cells, &size_cells)) != 0)
		return (EINVAL);
	if (addr_cells != 3 || size_cells != 2)
		return (ERANGE);

	par_addr_cells = fdt_parent_addr_cells(node);
	if (par_addr_cells > 3)
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
	 * perfectly fine not to want I/O space on PCI buses.
	 */
	bzero(io_space, sizeof(*io_space));
	bzero(mem_space, sizeof(*mem_space));

	rangesptr = &ranges[0];
	offset_cells = 0;
	for (i = 0; i < tuples; i++) {
		cell0 = fdt_data_get((void *)rangesptr, 1);
		rangesptr++;
		cell1 = fdt_data_get((void *)rangesptr, 1);
		rangesptr++;
		cell2 = fdt_data_get((void *)rangesptr, 1);
		rangesptr++;
		portid = fdt_data_get((void *)(rangesptr+1), 1);

		if (cell0 & 0x02000000) {
			pci_space = mem_space;
			is_io_space = 0;
		} else if (cell0 & 0x01000000) {
			pci_space = io_space;
			is_io_space = 1;
		} else {
			rv = ERANGE;
			goto out;
		}

		if (par_addr_cells == 3) {
			/*
			 * This is a PCI subnode 'ranges'. Skip cell0 and
			 * cell1 of this entry and only use cell2.
			 */
			offset_cells = 2;
			rangesptr += offset_cells;
		}

		if ((par_addr_cells - offset_cells) > 2) {
			rv = ERANGE;
			goto out;
		}
		pci_space->base_parent = fdt_data_get((void *)rangesptr,
		    par_addr_cells - offset_cells);
		rangesptr += par_addr_cells - offset_cells;

		if (size_cells > 2) {
			rv = ERANGE;
			goto out;
		}
		pci_space->len = fdt_data_get((void *)rangesptr, size_cells);
		rangesptr += size_cells;

		pci_space->base_pci = cell2;

		if (pci_space->len == 0) {
			pci_space->len = PCI_SPACE_LEN;
			pci_space->base_parent = fdt_immr_va +
			    PCI_SPACE_LEN * ( 2 * portid + is_io_space);
		}
	}
	rv = 0;
out:
	return (rv);
}

static int
mv_pci_ranges(phandle_t node, struct mv_pci_range *io_space,
    struct mv_pci_range *mem_space)
{
	int err;

	debugf("Processing PCI node: %x\n", node);
	if ((err = mv_pci_ranges_decode(node, io_space, mem_space)) != 0) {
		debugf("could not decode parent PCI node 'ranges'\n");
		return (err);
	}

	debugf("Post fixup dump:\n");
	mv_pci_range_dump(io_space);
	mv_pci_range_dump(mem_space);
	return (0);
}

int
mv_pci_devmap(phandle_t node, struct devmap_entry *devmap, vm_offset_t io_va,
    vm_offset_t mem_va)
{
	struct mv_pci_range io_space, mem_space;
	int error;

	if ((error = mv_pci_ranges_decode(node, &io_space, &mem_space)) != 0)
		return (error);

	devmap->pd_va = (io_va ? io_va : io_space.base_parent);
	devmap->pd_pa = io_space.base_parent;
	devmap->pd_size = io_space.len;
	devmap++;

	devmap->pd_va = (mem_va ? mem_va : mem_space.base_parent);
	devmap->pd_pa = mem_space.base_parent;
	devmap->pd_size = mem_space.len;
	return (0);
}

/*
 * Code and data related to the Marvell pcib driver.
 */

#define PCI_CFG_ENA		(1U << 31)
#define PCI_CFG_BUS(bus)	(((bus) & 0xff) << 16)
#define PCI_CFG_DEV(dev)	(((dev) & 0x1f) << 11)
#define PCI_CFG_FUN(fun)	(((fun) & 0x7) << 8)
#define PCI_CFG_PCIE_REG(reg)	((reg) & 0xfc)

#define PCI_REG_CFG_ADDR	0x0C78
#define PCI_REG_CFG_DATA	0x0C7C

#define PCIE_REG_CFG_ADDR	0x18F8
#define PCIE_REG_CFG_DATA	0x18FC
#define PCIE_REG_CONTROL	0x1A00
#define   PCIE_CTRL_LINK1X	0x00000001
#define PCIE_REG_STATUS		0x1A04
#define PCIE_REG_IRQ_MASK	0x1910

#define PCIE_CONTROL_ROOT_CMPLX	(1 << 1)
#define PCIE_CONTROL_HOT_RESET	(1 << 24)

#define PCIE_LINK_TIMEOUT	1000000

#define PCIE_STATUS_LINK_DOWN	1
#define PCIE_STATUS_DEV_OFFS	16

/* Minimum PCI Memory and I/O allocations taken from PCI spec (in bytes) */
#define PCI_MIN_IO_ALLOC	4
#define PCI_MIN_MEM_ALLOC	16

#define BITS_PER_UINT32		(NBBY * sizeof(uint32_t))

struct mv_pcib_softc {
	device_t	sc_dev;

	struct rman	sc_mem_rman;
	bus_addr_t	sc_mem_base;
	bus_addr_t	sc_mem_size;
	uint32_t	sc_mem_map[MV_PCI_MEM_SLICE_SIZE /
	    (PCI_MIN_MEM_ALLOC * BITS_PER_UINT32)];
	int		sc_win_target;
	int		sc_mem_win_attr;

	struct rman	sc_io_rman;
	bus_addr_t	sc_io_base;
	bus_addr_t	sc_io_size;
	uint32_t	sc_io_map[MV_PCI_IO_SLICE_SIZE /
	    (PCI_MIN_IO_ALLOC * BITS_PER_UINT32)];
	int		sc_io_win_attr;

	struct resource	*sc_res;
	bus_space_handle_t sc_bsh;
	bus_space_tag_t	sc_bst;
	int		sc_rid;

	struct mtx	sc_msi_mtx;
	uint32_t	sc_msi_bitmap;

	int		sc_busnr;		/* Host bridge bus number */
	int		sc_devnr;		/* Host bridge device number */
	int		sc_type;
	int		sc_mode;		/* Endpoint / Root Complex */

	int		sc_msi_supported;
	int		sc_skip_enable_procedure;
	int		sc_enable_find_root_slot;
	struct ofw_bus_iinfo	sc_pci_iinfo;

	int		ap_segment;		/* PCI domain */
};

/* Local forward prototypes */
static int mv_pcib_decode_win(phandle_t, struct mv_pcib_softc *);
static void mv_pcib_hw_cfginit(void);
static uint32_t mv_pcib_hw_cfgread(struct mv_pcib_softc *, u_int, u_int,
    u_int, u_int, int);
static void mv_pcib_hw_cfgwrite(struct mv_pcib_softc *, u_int, u_int,
    u_int, u_int, uint32_t, int);
static int mv_pcib_init(struct mv_pcib_softc *, int, int);
static int mv_pcib_init_all_bars(struct mv_pcib_softc *, int, int, int, int);
static void mv_pcib_init_bridge(struct mv_pcib_softc *, int, int, int);
static inline void pcib_write_irq_mask(struct mv_pcib_softc *, uint32_t);
static void mv_pcib_enable(struct mv_pcib_softc *, uint32_t);
static int mv_pcib_mem_init(struct mv_pcib_softc *);

/* Forward prototypes */
static int mv_pcib_probe(device_t);
static int mv_pcib_attach(device_t);

static struct resource *mv_pcib_alloc_resource(device_t, device_t, int, int *,
    rman_res_t, rman_res_t, rman_res_t, u_int);
static int mv_pcib_release_resource(device_t, device_t, int, int,
    struct resource *);
static int mv_pcib_read_ivar(device_t, device_t, int, uintptr_t *);
static int mv_pcib_write_ivar(device_t, device_t, int, uintptr_t);

static int mv_pcib_maxslots(device_t);
static uint32_t mv_pcib_read_config(device_t, u_int, u_int, u_int, u_int, int);
static void mv_pcib_write_config(device_t, u_int, u_int, u_int, u_int,
    uint32_t, int);
static int mv_pcib_route_interrupt(device_t, device_t, int);

static int mv_pcib_alloc_msi(device_t, device_t, int, int, int *);
static int mv_pcib_map_msi(device_t, device_t, int, uint64_t *, uint32_t *);
static int mv_pcib_release_msi(device_t, device_t, int, int *);

/*
 * Bus interface definitions.
 */
static device_method_t mv_pcib_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			mv_pcib_probe),
	DEVMETHOD(device_attach,		mv_pcib_attach),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,		mv_pcib_read_ivar),
	DEVMETHOD(bus_write_ivar,		mv_pcib_write_ivar),
	DEVMETHOD(bus_alloc_resource,		mv_pcib_alloc_resource),
	DEVMETHOD(bus_release_resource,		mv_pcib_release_resource),
	DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,		bus_generic_teardown_intr),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,		mv_pcib_maxslots),
	DEVMETHOD(pcib_read_config,		mv_pcib_read_config),
	DEVMETHOD(pcib_write_config,		mv_pcib_write_config),
	DEVMETHOD(pcib_route_interrupt,		mv_pcib_route_interrupt),
	DEVMETHOD(pcib_request_feature,		pcib_request_feature_allow),

	DEVMETHOD(pcib_alloc_msi,		mv_pcib_alloc_msi),
	DEVMETHOD(pcib_release_msi,		mv_pcib_release_msi),
	DEVMETHOD(pcib_map_msi,			mv_pcib_map_msi),

	/* OFW bus interface */
	DEVMETHOD(ofw_bus_get_compat,   ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,    ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,     ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,     ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,     ofw_bus_gen_get_type),

	DEVMETHOD_END
};

static driver_t mv_pcib_driver = {
	"pcib",
	mv_pcib_methods,
	sizeof(struct mv_pcib_softc),
};

devclass_t pcib_devclass;

DRIVER_MODULE(pcib, ofwbus, mv_pcib_driver, pcib_devclass, 0, 0);
DRIVER_MODULE(pcib, pcib_ctrl, mv_pcib_driver, pcib_devclass, 0, 0);

static struct mtx pcicfg_mtx;

static int
mv_pcib_probe(device_t self)
{
	phandle_t node;

	node = ofw_bus_get_node(self);
	if (!mv_fdt_is_type(node, "pci"))
		return (ENXIO);

	if (!(ofw_bus_is_compatible(self, "mrvl,pcie") ||
	    ofw_bus_is_compatible(self, "mrvl,pci") ||
	    ofw_bus_node_is_compatible(
	    OF_parent(node), "marvell,armada-370-pcie")))
		return (ENXIO);

	if (!ofw_bus_status_okay(self))
		return (ENXIO);

	device_set_desc(self, "Marvell Integrated PCI/PCI-E Controller");
	return (BUS_PROBE_DEFAULT);
}

static int
mv_pcib_attach(device_t self)
{
	struct mv_pcib_softc *sc;
	phandle_t node, parnode;
	uint32_t val, reg0;
	int err, bus, devfn, port_id;

	sc = device_get_softc(self);
	sc->sc_dev = self;

	node = ofw_bus_get_node(self);
	parnode = OF_parent(node);

	if (OF_getencprop(node, "marvell,pcie-port", &(port_id),
	    sizeof(port_id)) <= 0) {
		/* If port ID does not exist in the FDT set value to 0 */
		if (!OF_hasprop(node, "marvell,pcie-port"))
			port_id = 0;
		else
			return(ENXIO);
	}

	sc->ap_segment = port_id;

	if (ofw_bus_node_is_compatible(node, "mrvl,pcie")) {
		sc->sc_type = MV_TYPE_PCIE;
		sc->sc_win_target = MV_WIN_PCIE_TARGET(port_id);
		sc->sc_mem_win_attr = MV_WIN_PCIE_MEM_ATTR(port_id);
		sc->sc_io_win_attr = MV_WIN_PCIE_IO_ATTR(port_id);
#if __ARM_ARCH >= 6
		sc->sc_skip_enable_procedure = 1;
#endif
	} else if (ofw_bus_node_is_compatible(parnode, "marvell,armada-370-pcie")) {
		sc->sc_type = MV_TYPE_PCIE;
		sc->sc_win_target = MV_WIN_PCIE_TARGET_ARMADA38X(port_id);
		sc->sc_mem_win_attr = MV_WIN_PCIE_MEM_ATTR_ARMADA38X(port_id);
		sc->sc_io_win_attr = MV_WIN_PCIE_IO_ATTR_ARMADA38X(port_id);
		sc->sc_enable_find_root_slot = 1;
	} else if (ofw_bus_node_is_compatible(node, "mrvl,pci")) {
		sc->sc_type = MV_TYPE_PCI;
		sc->sc_win_target = MV_WIN_PCI_TARGET;
		sc->sc_mem_win_attr = MV_WIN_PCI_MEM_ATTR;
		sc->sc_io_win_attr = MV_WIN_PCI_IO_ATTR;
	} else
		return (ENXIO);

	/*
	 * Retrieve our mem-mapped registers range.
	 */
	sc->sc_rid = 0;
	sc->sc_res = bus_alloc_resource_any(self, SYS_RES_MEMORY, &sc->sc_rid,
	    RF_ACTIVE);
	if (sc->sc_res == NULL) {
		device_printf(self, "could not map memory\n");
		return (ENXIO);
	}
	sc->sc_bst = rman_get_bustag(sc->sc_res);
	sc->sc_bsh = rman_get_bushandle(sc->sc_res);

	val = bus_space_read_4(sc->sc_bst, sc->sc_bsh, PCIE_REG_CONTROL);
	sc->sc_mode = (val & PCIE_CONTROL_ROOT_CMPLX ? MV_MODE_ROOT :
	    MV_MODE_ENDPOINT);

	/*
	 * Get PCI interrupt info.
	 */
	if (sc->sc_mode == MV_MODE_ROOT)
		ofw_bus_setup_iinfo(node, &sc->sc_pci_iinfo, sizeof(pcell_t));

	/*
	 * Configure decode windows for PCI(E) access.
	 */
	if (mv_pcib_decode_win(node, sc) != 0)
		return (ENXIO);

	mv_pcib_hw_cfginit();

	/*
	 * Enable PCIE device.
	 */
	mv_pcib_enable(sc, port_id);

	/*
	 * Memory management.
	 */
	err = mv_pcib_mem_init(sc);
	if (err)
		return (err);

	/*
	 * Preliminary bus enumeration to find first linked devices and set
	 * appropriate bus number from which should start the actual enumeration
	 */
	for (bus = 0; bus < PCI_BUSMAX; bus++) {
		for (devfn = 0; devfn < mv_pcib_maxslots(self); devfn++) {
			reg0 = mv_pcib_read_config(self, bus, devfn, devfn & 0x7, 0x0, 4);
			if (reg0 == (~0U))
				continue; /* no device */
			else {
				sc->sc_busnr = bus; /* update bus number */
				break;
			}
		}
	}

	if (sc->sc_mode == MV_MODE_ROOT) {
		err = mv_pcib_init(sc, sc->sc_busnr,
		    mv_pcib_maxslots(sc->sc_dev));
		if (err)
			goto error;

		device_add_child(self, "pci", -1);
	} else {
		sc->sc_devnr = 1;
		bus_space_write_4(sc->sc_bst, sc->sc_bsh,
		    PCIE_REG_STATUS, 1 << PCIE_STATUS_DEV_OFFS);
		device_add_child(self, "pci_ep", -1);
	}

	mtx_init(&sc->sc_msi_mtx, "msi_mtx", NULL, MTX_DEF);
	return (bus_generic_attach(self));

error:
	/* XXX SYS_RES_ should be released here */
	rman_fini(&sc->sc_mem_rman);
	rman_fini(&sc->sc_io_rman);

	return (err);
}

static void
mv_pcib_enable(struct mv_pcib_softc *sc, uint32_t unit)
{
	uint32_t val;
	int timeout;

	if (sc->sc_skip_enable_procedure)
		goto pcib_enable_root_mode;

	/*
	 * Check if PCIE device is enabled.
	 */
	if ((sc->sc_skip_enable_procedure == 0) &&
	    (read_cpu_ctrl(CPU_CONTROL) & CPU_CONTROL_PCIE_DISABLE(unit))) {
		write_cpu_ctrl(CPU_CONTROL, read_cpu_ctrl(CPU_CONTROL) &
		    ~(CPU_CONTROL_PCIE_DISABLE(unit)));

		timeout = PCIE_LINK_TIMEOUT;
		val = bus_space_read_4(sc->sc_bst, sc->sc_bsh,
		    PCIE_REG_STATUS);
		while (((val & PCIE_STATUS_LINK_DOWN) == 1) && (timeout > 0)) {
			DELAY(1000);
			timeout -= 1000;
			val = bus_space_read_4(sc->sc_bst, sc->sc_bsh,
			    PCIE_REG_STATUS);
		}
	}

pcib_enable_root_mode:
	if (sc->sc_mode == MV_MODE_ROOT) {
		/*
		 * Enable PCI bridge.
		 */
		val = bus_space_read_4(sc->sc_bst, sc->sc_bsh, PCIR_COMMAND);
		val |= PCIM_CMD_SERRESPEN | PCIM_CMD_BUSMASTEREN |
		    PCIM_CMD_MEMEN | PCIM_CMD_PORTEN;
		bus_space_write_4(sc->sc_bst, sc->sc_bsh, PCIR_COMMAND, val);
	}
}

static int
mv_pcib_mem_init(struct mv_pcib_softc *sc)
{
	int err;

	/*
	 * Memory management.
	 */
	sc->sc_mem_rman.rm_type = RMAN_ARRAY;
	err = rman_init(&sc->sc_mem_rman);
	if (err)
		return (err);

	sc->sc_io_rman.rm_type = RMAN_ARRAY;
	err = rman_init(&sc->sc_io_rman);
	if (err) {
		rman_fini(&sc->sc_mem_rman);
		return (err);
	}

	err = rman_manage_region(&sc->sc_mem_rman, sc->sc_mem_base,
	    sc->sc_mem_base + sc->sc_mem_size - 1);
	if (err)
		goto error;

	err = rman_manage_region(&sc->sc_io_rman, sc->sc_io_base,
	    sc->sc_io_base + sc->sc_io_size - 1);
	if (err)
		goto error;

	return (0);

error:
	rman_fini(&sc->sc_mem_rman);
	rman_fini(&sc->sc_io_rman);

	return (err);
}

static inline uint32_t
pcib_bit_get(uint32_t *map, uint32_t bit)
{
	uint32_t n = bit / BITS_PER_UINT32;

	bit = bit % BITS_PER_UINT32;
	return (map[n] & (1 << bit));
}

static inline void
pcib_bit_set(uint32_t *map, uint32_t bit)
{
	uint32_t n = bit / BITS_PER_UINT32;

	bit = bit % BITS_PER_UINT32;
	map[n] |= (1 << bit);
}

static inline uint32_t
pcib_map_check(uint32_t *map, uint32_t start, uint32_t bits)
{
	uint32_t i;

	for (i = start; i < start + bits; i++)
		if (pcib_bit_get(map, i))
			return (0);

	return (1);
}

static inline void
pcib_map_set(uint32_t *map, uint32_t start, uint32_t bits)
{
	uint32_t i;

	for (i = start; i < start + bits; i++)
		pcib_bit_set(map, i);
}

/*
 * The idea of this allocator is taken from ARM No-Cache memory
 * management code (sys/arm/arm/vm_machdep.c).
 */
static bus_addr_t
pcib_alloc(struct mv_pcib_softc *sc, uint32_t smask)
{
	uint32_t bits, bits_limit, i, *map, min_alloc, size;
	bus_addr_t addr = 0;
	bus_addr_t base;

	if (smask & 1) {
		base = sc->sc_io_base;
		min_alloc = PCI_MIN_IO_ALLOC;
		bits_limit = sc->sc_io_size / min_alloc;
		map = sc->sc_io_map;
		smask &= ~0x3;
	} else {
		base = sc->sc_mem_base;
		min_alloc = PCI_MIN_MEM_ALLOC;
		bits_limit = sc->sc_mem_size / min_alloc;
		map = sc->sc_mem_map;
		smask &= ~0xF;
	}

	size = ~smask + 1;
	bits = size / min_alloc;

	for (i = 0; i + bits <= bits_limit; i += bits)
		if (pcib_map_check(map, i, bits)) {
			pcib_map_set(map, i, bits);
			addr = base + (i * min_alloc);
			return (addr);
		}

	return (addr);
}

static int
mv_pcib_init_bar(struct mv_pcib_softc *sc, int bus, int slot, int func,
    int barno)
{
	uint32_t addr, bar;
	int reg, width;

	reg = PCIR_BAR(barno);

	/*
	 * Need to init the BAR register with 0xffffffff before correct
	 * value can be read.
	 */
	mv_pcib_write_config(sc->sc_dev, bus, slot, func, reg, ~0, 4);
	bar = mv_pcib_read_config(sc->sc_dev, bus, slot, func, reg, 4);
	if (bar == 0)
		return (1);

	/* Calculate BAR size: 64 or 32 bit (in 32-bit units) */
	width = ((bar & 7) == 4) ? 2 : 1;

	addr = pcib_alloc(sc, bar);
	if (!addr)
		return (-1);

	if (bootverbose)
		printf("PCI %u:%u:%u: reg %x: smask=%08x: addr=%08x\n",
		    bus, slot, func, reg, bar, addr);

	mv_pcib_write_config(sc->sc_dev, bus, slot, func, reg, addr, 4);
	if (width == 2)
		mv_pcib_write_config(sc->sc_dev, bus, slot, func, reg + 4,
		    0, 4);

	return (width);
}

static void
mv_pcib_init_bridge(struct mv_pcib_softc *sc, int bus, int slot, int func)
{
	bus_addr_t io_base, mem_base;
	uint32_t io_limit, mem_limit;
	int secbus;

	io_base = sc->sc_io_base;
	io_limit = io_base + sc->sc_io_size - 1;
	mem_base = sc->sc_mem_base;
	mem_limit = mem_base + sc->sc_mem_size - 1;

	/* Configure I/O decode registers */
	mv_pcib_write_config(sc->sc_dev, bus, slot, func, PCIR_IOBASEL_1,
	    io_base >> 8, 1);
	mv_pcib_write_config(sc->sc_dev, bus, slot, func, PCIR_IOBASEH_1,
	    io_base >> 16, 2);
	mv_pcib_write_config(sc->sc_dev, bus, slot, func, PCIR_IOLIMITL_1,
	    io_limit >> 8, 1);
	mv_pcib_write_config(sc->sc_dev, bus, slot, func, PCIR_IOLIMITH_1,
	    io_limit >> 16, 2);

	/* Configure memory decode registers */
	mv_pcib_write_config(sc->sc_dev, bus, slot, func, PCIR_MEMBASE_1,
	    mem_base >> 16, 2);
	mv_pcib_write_config(sc->sc_dev, bus, slot, func, PCIR_MEMLIMIT_1,
	    mem_limit >> 16, 2);

	/* Disable memory prefetch decode */
	mv_pcib_write_config(sc->sc_dev, bus, slot, func, PCIR_PMBASEL_1,
	    0x10, 2);
	mv_pcib_write_config(sc->sc_dev, bus, slot, func, PCIR_PMBASEH_1,
	    0x0, 4);
	mv_pcib_write_config(sc->sc_dev, bus, slot, func, PCIR_PMLIMITL_1,
	    0xF, 2);
	mv_pcib_write_config(sc->sc_dev, bus, slot, func, PCIR_PMLIMITH_1,
	    0x0, 4);

	secbus = mv_pcib_read_config(sc->sc_dev, bus, slot, func,
	    PCIR_SECBUS_1, 1);

	/* Configure buses behind the bridge */
	mv_pcib_init(sc, secbus, PCI_SLOTMAX);
}

static int
mv_pcib_init(struct mv_pcib_softc *sc, int bus, int maxslot)
{
	int slot, func, maxfunc, error;
	uint8_t hdrtype, command, class, subclass;

	for (slot = 0; slot <= maxslot; slot++) {
		maxfunc = 0;
		for (func = 0; func <= maxfunc; func++) {
			hdrtype = mv_pcib_read_config(sc->sc_dev, bus, slot,
			    func, PCIR_HDRTYPE, 1);

			if ((hdrtype & PCIM_HDRTYPE) > PCI_MAXHDRTYPE)
				continue;

			if (func == 0 && (hdrtype & PCIM_MFDEV))
				maxfunc = PCI_FUNCMAX;

			command = mv_pcib_read_config(sc->sc_dev, bus, slot,
			    func, PCIR_COMMAND, 1);
			command &= ~(PCIM_CMD_MEMEN | PCIM_CMD_PORTEN);
			mv_pcib_write_config(sc->sc_dev, bus, slot, func,
			    PCIR_COMMAND, command, 1);

			error = mv_pcib_init_all_bars(sc, bus, slot, func,
			    hdrtype);

			if (error)
				return (error);

			command |= PCIM_CMD_BUSMASTEREN | PCIM_CMD_MEMEN |
			    PCIM_CMD_PORTEN;
			mv_pcib_write_config(sc->sc_dev, bus, slot, func,
			    PCIR_COMMAND, command, 1);

			/* Handle PCI-PCI bridges */
			class = mv_pcib_read_config(sc->sc_dev, bus, slot,
			    func, PCIR_CLASS, 1);
			subclass = mv_pcib_read_config(sc->sc_dev, bus, slot,
			    func, PCIR_SUBCLASS, 1);

			if (class != PCIC_BRIDGE ||
			    subclass != PCIS_BRIDGE_PCI)
				continue;

			mv_pcib_init_bridge(sc, bus, slot, func);
		}
	}

	/* Enable all ABCD interrupts */
	pcib_write_irq_mask(sc, (0xF << 24));

	return (0);
}

static int
mv_pcib_init_all_bars(struct mv_pcib_softc *sc, int bus, int slot,
    int func, int hdrtype)
{
	int maxbar, bar, i;

	maxbar = (hdrtype & PCIM_HDRTYPE) ? 0 : 6;
	bar = 0;

	/* Program the base address registers */
	while (bar < maxbar) {
		i = mv_pcib_init_bar(sc, bus, slot, func, bar);
		bar += i;
		if (i < 0) {
			device_printf(sc->sc_dev,
			    "PCI IO/Memory space exhausted\n");
			return (ENOMEM);
		}
	}

	return (0);
}

static struct resource *
mv_pcib_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct mv_pcib_softc *sc = device_get_softc(dev);
	struct rman *rm = NULL;
	struct resource *res;

	switch (type) {
	case SYS_RES_IOPORT:
		rm = &sc->sc_io_rman;
		break;
	case SYS_RES_MEMORY:
		rm = &sc->sc_mem_rman;
		break;
#ifdef PCI_RES_BUS
	case PCI_RES_BUS:
		return (pci_domain_alloc_bus(sc->ap_segment, child, rid, start,
		    end, count, flags));
#endif
	default:
		return (BUS_ALLOC_RESOURCE(device_get_parent(dev), dev,
		    type, rid, start, end, count, flags));
	}

	if (RMAN_IS_DEFAULT_RANGE(start, end)) {
		start = sc->sc_mem_base;
		end = sc->sc_mem_base + sc->sc_mem_size - 1;
		count = sc->sc_mem_size;
	}

	if ((start < sc->sc_mem_base) || (start + count - 1 != end) ||
	    (end > sc->sc_mem_base + sc->sc_mem_size - 1))
		return (NULL);

	res = rman_reserve_resource(rm, start, end, count, flags, child);
	if (res == NULL)
		return (NULL);

	rman_set_rid(res, *rid);
	rman_set_bustag(res, fdtbus_bs_tag);
	rman_set_bushandle(res, start);

	if (flags & RF_ACTIVE)
		if (bus_activate_resource(child, type, *rid, res)) {
			rman_release_resource(res);
			return (NULL);
		}

	return (res);
}

static int
mv_pcib_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *res)
{
#ifdef PCI_RES_BUS
	struct mv_pcib_softc *sc = device_get_softc(dev);

	if (type == PCI_RES_BUS)
		return (pci_domain_release_bus(sc->ap_segment, child, rid, res));
#endif
	if (type != SYS_RES_IOPORT && type != SYS_RES_MEMORY)
		return (BUS_RELEASE_RESOURCE(device_get_parent(dev), child,
		    type, rid, res));

	return (rman_release_resource(res));
}

static int
mv_pcib_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
	struct mv_pcib_softc *sc = device_get_softc(dev);

	switch (which) {
	case PCIB_IVAR_BUS:
		*result = sc->sc_busnr;
		return (0);
	case PCIB_IVAR_DOMAIN:
		*result = device_get_unit(dev);
		return (0);
	}

	return (ENOENT);
}

static int
mv_pcib_write_ivar(device_t dev, device_t child, int which, uintptr_t value)
{
	struct mv_pcib_softc *sc = device_get_softc(dev);

	switch (which) {
	case PCIB_IVAR_BUS:
		sc->sc_busnr = value;
		return (0);
	}

	return (ENOENT);
}

static inline void
pcib_write_irq_mask(struct mv_pcib_softc *sc, uint32_t mask)
{

	if (sc->sc_type != MV_TYPE_PCIE)
		return;

	bus_space_write_4(sc->sc_bst, sc->sc_bsh, PCIE_REG_IRQ_MASK, mask);
}

static void
mv_pcib_hw_cfginit(void)
{
	static int opened = 0;

	if (opened)
		return;

	mtx_init(&pcicfg_mtx, "pcicfg", NULL, MTX_SPIN);
	opened = 1;
}

static uint32_t
mv_pcib_hw_cfgread(struct mv_pcib_softc *sc, u_int bus, u_int slot,
    u_int func, u_int reg, int bytes)
{
	uint32_t addr, data, ca, cd;

	ca = (sc->sc_type != MV_TYPE_PCI) ?
	    PCIE_REG_CFG_ADDR : PCI_REG_CFG_ADDR;
	cd = (sc->sc_type != MV_TYPE_PCI) ?
	    PCIE_REG_CFG_DATA : PCI_REG_CFG_DATA;
	addr = PCI_CFG_ENA | PCI_CFG_BUS(bus) | PCI_CFG_DEV(slot) |
	    PCI_CFG_FUN(func) | PCI_CFG_PCIE_REG(reg);

	mtx_lock_spin(&pcicfg_mtx);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, ca, addr);

	data = ~0;
	switch (bytes) {
	case 1:
		data = bus_space_read_1(sc->sc_bst, sc->sc_bsh,
		    cd + (reg & 3));
		break;
	case 2:
		data = le16toh(bus_space_read_2(sc->sc_bst, sc->sc_bsh,
		    cd + (reg & 2)));
		break;
	case 4:
		data = le32toh(bus_space_read_4(sc->sc_bst, sc->sc_bsh,
		    cd));
		break;
	}
	mtx_unlock_spin(&pcicfg_mtx);
	return (data);
}

static void
mv_pcib_hw_cfgwrite(struct mv_pcib_softc *sc, u_int bus, u_int slot,
    u_int func, u_int reg, uint32_t data, int bytes)
{
	uint32_t addr, ca, cd;

	ca = (sc->sc_type != MV_TYPE_PCI) ?
	    PCIE_REG_CFG_ADDR : PCI_REG_CFG_ADDR;
	cd = (sc->sc_type != MV_TYPE_PCI) ?
	    PCIE_REG_CFG_DATA : PCI_REG_CFG_DATA;
	addr = PCI_CFG_ENA | PCI_CFG_BUS(bus) | PCI_CFG_DEV(slot) |
	    PCI_CFG_FUN(func) | PCI_CFG_PCIE_REG(reg);

	mtx_lock_spin(&pcicfg_mtx);
	bus_space_write_4(sc->sc_bst, sc->sc_bsh, ca, addr);

	switch (bytes) {
	case 1:
		bus_space_write_1(sc->sc_bst, sc->sc_bsh,
		    cd + (reg & 3), data);
		break;
	case 2:
		bus_space_write_2(sc->sc_bst, sc->sc_bsh,
		    cd + (reg & 2), htole16(data));
		break;
	case 4:
		bus_space_write_4(sc->sc_bst, sc->sc_bsh,
		    cd, htole32(data));
		break;
	}
	mtx_unlock_spin(&pcicfg_mtx);
}

static int
mv_pcib_maxslots(device_t dev)
{
	struct mv_pcib_softc *sc = device_get_softc(dev);

	return ((sc->sc_type != MV_TYPE_PCI) ? 1 : PCI_SLOTMAX);
}

static int
mv_pcib_root_slot(device_t dev, u_int bus, u_int slot, u_int func)
{
	struct mv_pcib_softc *sc = device_get_softc(dev);
	uint32_t vendor, device;

	/* On platforms other than Armada38x, root link is always at slot 0 */
	if (!sc->sc_enable_find_root_slot)
		return (slot == 0);

	vendor = mv_pcib_hw_cfgread(sc, bus, slot, func, PCIR_VENDOR,
	    PCIR_VENDOR_LENGTH);
	device = mv_pcib_hw_cfgread(sc, bus, slot, func, PCIR_DEVICE,
	    PCIR_DEVICE_LENGTH) & MV_DEV_FAMILY_MASK;

	return (vendor == PCI_VENDORID_MRVL && device == MV_DEV_ARMADA38X);
}

static uint32_t
mv_pcib_read_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, int bytes)
{
	struct mv_pcib_softc *sc = device_get_softc(dev);

	/* Return ~0 if link is inactive or trying to read from Root */
	if ((bus_space_read_4(sc->sc_bst, sc->sc_bsh, PCIE_REG_STATUS) &
	    PCIE_STATUS_LINK_DOWN) || mv_pcib_root_slot(dev, bus, slot, func))
		return (~0U);

	return (mv_pcib_hw_cfgread(sc, bus, slot, func, reg, bytes));
}

static void
mv_pcib_write_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, uint32_t val, int bytes)
{
	struct mv_pcib_softc *sc = device_get_softc(dev);

	/* Return if link is inactive or trying to write to Root */
	if ((bus_space_read_4(sc->sc_bst, sc->sc_bsh, PCIE_REG_STATUS) &
	    PCIE_STATUS_LINK_DOWN) || mv_pcib_root_slot(dev, bus, slot, func))
		return;

	mv_pcib_hw_cfgwrite(sc, bus, slot, func, reg, val, bytes);
}

static int
mv_pcib_route_interrupt(device_t bus, device_t dev, int pin)
{
	struct mv_pcib_softc *sc;
	struct ofw_pci_register reg;
	uint32_t pintr, mintr[4];
	int icells;
	phandle_t iparent;

	sc = device_get_softc(bus);
	pintr = pin;

	/* Fabricate imap information in case this isn't an OFW device */
	bzero(&reg, sizeof(reg));
	reg.phys_hi = (pci_get_bus(dev) << OFW_PCI_PHYS_HI_BUSSHIFT) |
	    (pci_get_slot(dev) << OFW_PCI_PHYS_HI_DEVICESHIFT) |
	    (pci_get_function(dev) << OFW_PCI_PHYS_HI_FUNCTIONSHIFT);

	icells = ofw_bus_lookup_imap(ofw_bus_get_node(dev), &sc->sc_pci_iinfo,
	    &reg, sizeof(reg), &pintr, sizeof(pintr), mintr, sizeof(mintr),
	    &iparent);
	if (icells > 0)
		return (ofw_bus_map_intr(dev, iparent, icells, mintr));

	/* Maybe it's a real interrupt, not an intpin */
	if (pin > 4)
		return (pin);

	device_printf(bus, "could not route pin %d for device %d.%d\n",
	    pin, pci_get_slot(dev), pci_get_function(dev));
	return (PCI_INVALID_IRQ);
}

static int
mv_pcib_decode_win(phandle_t node, struct mv_pcib_softc *sc)
{
	struct mv_pci_range io_space, mem_space;
	device_t dev;
	int error;

	dev = sc->sc_dev;

	if ((error = mv_pci_ranges(node, &io_space, &mem_space)) != 0) {
		device_printf(dev, "could not retrieve 'ranges' data\n");
		return (error);
	}

	/* Configure CPU decoding windows */
	error = decode_win_cpu_set(sc->sc_win_target,
	    sc->sc_io_win_attr, io_space.base_parent, io_space.len, ~0);
	if (error < 0) {
		device_printf(dev, "could not set up CPU decode "
		    "window for PCI IO\n");
		return (ENXIO);
	}
	error = decode_win_cpu_set(sc->sc_win_target,
	    sc->sc_mem_win_attr, mem_space.base_parent, mem_space.len,
	    mem_space.base_parent);
	if (error < 0) {
		device_printf(dev, "could not set up CPU decode "
		    "windows for PCI MEM\n");
		return (ENXIO);
	}

	sc->sc_io_base = io_space.base_parent;
	sc->sc_io_size = io_space.len;

	sc->sc_mem_base = mem_space.base_parent;
	sc->sc_mem_size = mem_space.len;

	return (0);
}

static int
mv_pcib_map_msi(device_t dev, device_t child, int irq, uint64_t *addr,
    uint32_t *data)
{
	struct mv_pcib_softc *sc;

	sc = device_get_softc(dev);
	if (!sc->sc_msi_supported)
		return (ENOTSUP);

	irq = irq - MSI_IRQ;

	/* validate parameters */
	if (isclr(&sc->sc_msi_bitmap, irq)) {
		device_printf(dev, "invalid MSI 0x%x\n", irq);
		return (EINVAL);
	}

#if __ARM_ARCH >= 6 
	mv_msi_data(irq, addr, data);
#endif

	debugf("%s: irq: %d addr: %jx data: %x\n",
	    __func__, irq, *addr, *data);

	return (0);
}

static int
mv_pcib_alloc_msi(device_t dev, device_t child, int count,
    int maxcount __unused, int *irqs)
{
	struct mv_pcib_softc *sc;
	u_int start = 0, i;

	sc = device_get_softc(dev);
	if (!sc->sc_msi_supported)
		return (ENOTSUP);

	if (powerof2(count) == 0 || count > MSI_IRQ_NUM)
		return (EINVAL);

	mtx_lock(&sc->sc_msi_mtx);

	for (start = 0; (start + count) < MSI_IRQ_NUM; start++) {
		for (i = start; i < start + count; i++) {
			if (isset(&sc->sc_msi_bitmap, i))
				break;
		}
		if (i == start + count)
			break;
	}

	if ((start + count) == MSI_IRQ_NUM) {
		mtx_unlock(&sc->sc_msi_mtx);
		return (ENXIO);
	}

	for (i = start; i < start + count; i++) {
		setbit(&sc->sc_msi_bitmap, i);
		*irqs++ = MSI_IRQ + i;
	}
	debugf("%s: start: %x count: %x\n", __func__, start, count);

	mtx_unlock(&sc->sc_msi_mtx);
	return (0);
}

static int
mv_pcib_release_msi(device_t dev, device_t child, int count, int *irqs)
{
	struct mv_pcib_softc *sc;
	u_int i;

	sc = device_get_softc(dev);
	if(!sc->sc_msi_supported)
		return (ENOTSUP);

	mtx_lock(&sc->sc_msi_mtx);

	for (i = 0; i < count; i++)
		clrbit(&sc->sc_msi_bitmap, irqs[i] - MSI_IRQ);

	mtx_unlock(&sc->sc_msi_mtx);
	return (0);
}
