/*-
 * Copyright (c) 2015-2016 Nathan Whitehorn
 * Copyright (c) 2017-2018 Semihalf
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
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/kernel.h>
#include <sys/pciio.h>
#include <sys/endian.h>
#include <sys/rman.h>
#include <sys/vmem.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofwpci.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <machine/bus.h>
#include <machine/intr_machdep.h>
#include <machine/md_var.h>

#include <vm/vm.h>
#include <vm/pmap.h>

#include "pcib_if.h"
#include "pic_if.h"
#include "iommu_if.h"
#include "opal.h"

#define	OPAL_PCI_TCE_MAX_ENTRIES	(1024*1024UL)
#define	OPAL_PCI_TCE_DEFAULT_SEG_SIZE	(16*1024*1024UL)
#define	OPAL_PCI_TCE_R			(1UL << 0)
#define	OPAL_PCI_TCE_W			(1UL << 1)
#define	PHB3_TCE_KILL_INVAL_ALL		(1UL << 63)

/*
 * Device interface.
 */
static int		opalpci_probe(device_t);
static int		opalpci_attach(device_t);

/*
 * pcib interface.
 */
static uint32_t		opalpci_read_config(device_t, u_int, u_int, u_int,
			    u_int, int);
static void		opalpci_write_config(device_t, u_int, u_int, u_int,
			    u_int, u_int32_t, int);
static int		opalpci_alloc_msi(device_t dev, device_t child,
			    int count, int maxcount, int *irqs);
static int		opalpci_release_msi(device_t dev, device_t child,
			    int count, int *irqs);
static int		opalpci_alloc_msix(device_t dev, device_t child,
			    int *irq);
static int		opalpci_release_msix(device_t dev, device_t child,
			    int irq);
static int		opalpci_map_msi(device_t dev, device_t child,
			    int irq, uint64_t *addr, uint32_t *data);
static int opalpci_route_interrupt(device_t bus, device_t dev, int pin);

/*
 * MSI PIC interface.
 */
static void opalpic_pic_enable(device_t dev, u_int irq, u_int vector, void **);
static void opalpic_pic_eoi(device_t dev, u_int irq, void *);

/* Bus interface */
static bus_dma_tag_t opalpci_get_dma_tag(device_t dev, device_t child);

/*
 * Commands
 */
#define	OPAL_M32_WINDOW_TYPE		1
#define	OPAL_M64_WINDOW_TYPE		2
#define	OPAL_IO_WINDOW_TYPE		3

#define	OPAL_RESET_PHB_COMPLETE		1
#define	OPAL_RESET_PCI_IODA_TABLE	6

#define	OPAL_DISABLE_M64		0
#define	OPAL_ENABLE_M64_SPLIT		1
#define	OPAL_ENABLE_M64_NON_SPLIT	2

#define	OPAL_EEH_ACTION_CLEAR_FREEZE_MMIO	1
#define	OPAL_EEH_ACTION_CLEAR_FREEZE_DMA	2
#define	OPAL_EEH_ACTION_CLEAR_FREEZE_ALL	3

/*
 * Constants
 */
#define OPAL_PCI_DEFAULT_PE			1

#define OPAL_PCI_BUS_SPACE_LOWADDR_32BIT	0x7FFFFFFFUL

/*
 * Driver methods.
 */
static device_method_t	opalpci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		opalpci_probe),
	DEVMETHOD(device_attach,	opalpci_attach),

	/* pcib interface */
	DEVMETHOD(pcib_read_config,	opalpci_read_config),
	DEVMETHOD(pcib_write_config,	opalpci_write_config),

	DEVMETHOD(pcib_alloc_msi,	opalpci_alloc_msi),
	DEVMETHOD(pcib_release_msi,	opalpci_release_msi),
	DEVMETHOD(pcib_alloc_msix,	opalpci_alloc_msix),
	DEVMETHOD(pcib_release_msix,	opalpci_release_msix),
	DEVMETHOD(pcib_map_msi,		opalpci_map_msi),
	DEVMETHOD(pcib_route_interrupt,	opalpci_route_interrupt),

	/* PIC interface for MSIs */
	DEVMETHOD(pic_enable,		opalpic_pic_enable),
	DEVMETHOD(pic_eoi,		opalpic_pic_eoi),

	/* Bus interface */
	DEVMETHOD(bus_get_dma_tag,	opalpci_get_dma_tag),

	DEVMETHOD_END
};

struct opalpci_softc {
	struct ofw_pci_softc ofw_sc;
	uint64_t phb_id;
	vmem_t *msi_vmem;
	int msi_base;		/* Base XIVE number */
	int base_msi_irq;	/* Base IRQ assigned by FreeBSD to this PIC */
	uint64_t *tce;		/* TCE table for 1:1 mapping */
	struct resource *r_reg;
};

static devclass_t	opalpci_devclass;
DEFINE_CLASS_1(pcib, opalpci_driver, opalpci_methods,
    sizeof(struct opalpci_softc), ofw_pci_driver);
EARLY_DRIVER_MODULE(opalpci, ofwbus, opalpci_driver, opalpci_devclass, 0, 0,
    BUS_PASS_BUS);

static int
opalpci_probe(device_t dev)
{
	const char	*type;

	if (opal_check() != 0)
		return (ENXIO);

	type = ofw_bus_get_type(dev);

	if (type == NULL || (strcmp(type, "pci") != 0 &&
	    strcmp(type, "pciex") != 0))
		return (ENXIO);

	if (!OF_hasprop(ofw_bus_get_node(dev), "ibm,opal-phbid"))
		return (ENXIO); 

	device_set_desc(dev, "OPAL Host-PCI bridge");
	return (BUS_PROBE_GENERIC);
}

static void
pci_phb3_tce_invalidate_entire(struct opalpci_softc *sc)
{

	mb();
	bus_write_8(sc->r_reg, 0x210, PHB3_TCE_KILL_INVAL_ALL);
	mb();
}

/* Simple function to round to a power of 2 */
static uint64_t
round_pow2(uint64_t val)
{

	return (1 << (flsl(val + (val - 1)) - 1));
}

/*
 * Starting with skiboot 5.10 PCIe nodes have a new property,
 * "ibm,supported-tce-sizes", to denote the TCE sizes available.  This allows us
 * to avoid hard-coding the maximum TCE size allowed, and instead provide a sane
 * default (however, the "sane" default, which works for all targets, is 64k,
 * limiting us to 64GB if we have 1M entries.
 */
static uint64_t
max_tce_size(device_t dev)
{
	phandle_t node;
	cell_t sizes[64]; /* Property is a list of bit-widths, up to 64-bits */
	int count;

	node = ofw_bus_get_node(dev);

	count = OF_getencprop(node, "ibm,supported-tce-sizes",
	    sizes, sizeof(sizes));
	if (count < (int) sizeof(cell_t))
		return OPAL_PCI_TCE_DEFAULT_SEG_SIZE;

	count /= sizeof(cell_t);

	return (1ULL << sizes[count - 1]);
}

static int
opalpci_attach(device_t dev)
{
	struct opalpci_softc *sc;
	cell_t id[2], m64ranges[2], m64window[6], npe;
	phandle_t node;
	int i, err;
	uint64_t maxmem;
	uint64_t entries;
	uint64_t tce_size;
	uint64_t tce_tbl_size;
	int m64bar;
	int rid;

	sc = device_get_softc(dev);
	node = ofw_bus_get_node(dev);

	switch (OF_getproplen(node, "ibm,opal-phbid")) {
	case 8:
		OF_getencprop(node, "ibm,opal-phbid", id, 8);
		sc->phb_id = ((uint64_t)id[0] << 32) | id[1];
		break;
	case 4:
		OF_getencprop(node, "ibm,opal-phbid", id, 4);
		sc->phb_id = id[0];
		break;
	default:
		device_printf(dev, "PHB ID property had wrong length (%zd)\n",
		    OF_getproplen(node, "ibm,opal-phbid"));
		return (ENXIO);
	}

	if (bootverbose)
		device_printf(dev, "OPAL ID %#lx\n", sc->phb_id);

	rid = 0;
	sc->r_reg = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE | RF_SHAREABLE);
	if (sc->r_reg == NULL) {
		device_printf(dev, "Failed to allocate PHB[%jd] registers\n",
		    (uintmax_t)sc->phb_id);
		return (ENXIO);
	}

#if 0
	/*
	 * Reset PCI IODA table
	 */
	err = opal_call(OPAL_PCI_RESET, sc->phb_id, OPAL_RESET_PCI_IODA_TABLE,
	    1);
	if (err != 0) {
		device_printf(dev, "IODA table reset failed: %d\n", err);
		return (ENXIO);
	}
	err = opal_call(OPAL_PCI_RESET, sc->phb_id, OPAL_RESET_PHB_COMPLETE,
	    1);
	if (err < 0) {
		device_printf(dev, "PHB reset failed: %d\n", err);
		return (ENXIO);
	}
	if (err > 0) {
		while ((err = opal_call(OPAL_PCI_POLL, sc->phb_id)) > 0) {
			DELAY(1000*(err + 1)); /* Returns expected delay in ms */
		}
	}
	if (err < 0) {
		device_printf(dev, "WARNING: PHB IODA reset poll failed: %d\n", err);
	}
	err = opal_call(OPAL_PCI_RESET, sc->phb_id, OPAL_RESET_PHB_COMPLETE,
	    0);
	if (err < 0) {
		device_printf(dev, "PHB reset failed: %d\n", err);
		return (ENXIO);
	}
	if (err > 0) {
		while ((err = opal_call(OPAL_PCI_POLL, sc->phb_id)) > 0) {
			DELAY(1000*(err + 1)); /* Returns expected delay in ms */
		}
	}
#endif

	/*
	 * Map all devices on the bus to partitionable endpoint one until
	 * such time as we start wanting to do things like bhyve.
	 */
	err = opal_call(OPAL_PCI_SET_PE, sc->phb_id, OPAL_PCI_DEFAULT_PE,
	    0, OPAL_PCI_BUS_ANY, OPAL_IGNORE_RID_DEVICE_NUMBER,
	    OPAL_IGNORE_RID_FUNC_NUMBER, OPAL_MAP_PE);
	if (err != 0) {
		device_printf(dev, "PE mapping failed: %d\n", err);
		return (ENXIO);
	}

	/*
	 * Turn on MMIO, mapped to PE 1
	 */
	if (OF_getencprop(node, "ibm,opal-num-pes", &npe, 4) != 4)
		npe = 1;
	for (i = 0; i < npe; i++) {
		err = opal_call(OPAL_PCI_MAP_PE_MMIO_WINDOW, sc->phb_id,
		    OPAL_PCI_DEFAULT_PE, OPAL_M32_WINDOW_TYPE, 0, i);
		if (err != 0)
			device_printf(dev, "MMIO %d map failed: %d\n", i, err);
	}

	if (OF_getencprop(node, "ibm,opal-available-m64-ranges",
	    m64ranges, sizeof(m64ranges)) == sizeof(m64ranges))
		m64bar = m64ranges[0];
	else
	    m64bar = 0;

	/* XXX: multiple M64 windows? */
	if (OF_getencprop(node, "ibm,opal-m64-window",
	    m64window, sizeof(m64window)) == sizeof(m64window)) {
		opal_call(OPAL_PCI_PHB_MMIO_ENABLE, sc->phb_id,
		    OPAL_M64_WINDOW_TYPE, m64bar, 0);
		opal_call(OPAL_PCI_SET_PHB_MEM_WINDOW, sc->phb_id,
		    OPAL_M64_WINDOW_TYPE, m64bar /* index */, 
		    ((uint64_t)m64window[2] << 32) | m64window[3], 0,
		    ((uint64_t)m64window[4] << 32) | m64window[5]);
		opal_call(OPAL_PCI_MAP_PE_MMIO_WINDOW, sc->phb_id,
		    OPAL_PCI_DEFAULT_PE, OPAL_M64_WINDOW_TYPE,
		    m64bar /* index */, 0);
		opal_call(OPAL_PCI_PHB_MMIO_ENABLE, sc->phb_id,
		    OPAL_M64_WINDOW_TYPE, m64bar, OPAL_ENABLE_M64_NON_SPLIT);
	}

	/*
	 * Enable IOMMU for PE1 - map everything 1:1 using
	 * segments of max_tce_size size
	 */
	tce_size = max_tce_size(dev);
	maxmem = roundup2(powerpc_ptob(Maxmem), tce_size);
	entries = round_pow2(maxmem / tce_size);
	tce_tbl_size = max(entries * sizeof(uint64_t), 4096);
	if (entries > OPAL_PCI_TCE_MAX_ENTRIES)
		panic("POWERNV supports only %jdGB of memory space\n",
		    (uintmax_t)((OPAL_PCI_TCE_MAX_ENTRIES * tce_size) >> 30));
	if (bootverbose)
		device_printf(dev, "Mapping 0-%#jx for DMA\n", (uintmax_t)maxmem);
	sc->tce = contigmalloc(tce_tbl_size,
	    M_DEVBUF, M_NOWAIT | M_ZERO, 0,
	    BUS_SPACE_MAXADDR, tce_tbl_size, 0);
	if (sc->tce == NULL)
		panic("Failed to allocate TCE memory for PHB %jd\n",
		    (uintmax_t)sc->phb_id);

	for (i = 0; i < entries; i++)
		sc->tce[i] = (i * tce_size) | OPAL_PCI_TCE_R | OPAL_PCI_TCE_W;

	/* Map TCE for every PE. It seems necessary for Power8 */
	for (i = 0; i < npe; i++) {
		err = opal_call(OPAL_PCI_MAP_PE_DMA_WINDOW, sc->phb_id,
		    i, (i << 1),
		    1, pmap_kextract((uint64_t)&sc->tce[0]),
		    tce_tbl_size, tce_size);
		if (err != 0) {
			device_printf(dev, "DMA IOMMU mapping failed: %d\n", err);
			return (ENXIO);
		}

		err = opal_call(OPAL_PCI_MAP_PE_DMA_WINDOW_REAL, sc->phb_id,
		    i, (i << 1) + 1,
		    (1UL << 59), maxmem);
		if (err != 0) {
			device_printf(dev, "DMA 64b bypass mapping failed: %d\n", err);
			return (ENXIO);
		}
	}

	/*
	 * Invalidate all previous TCE entries.
	 *
	 * TODO: add support for other PHBs than PHB3
	 */
	pci_phb3_tce_invalidate_entire(sc);

	/*
	 * Get MSI properties
	 */
	sc->msi_vmem = NULL;
	if (OF_getproplen(node, "ibm,opal-msi-ranges") > 0) {
		cell_t msi_ranges[2];
		OF_getencprop(node, "ibm,opal-msi-ranges",
		    msi_ranges, sizeof(msi_ranges));
		sc->msi_base = msi_ranges[0];

		sc->msi_vmem = vmem_create("OPAL MSI", msi_ranges[0],
		    msi_ranges[1], 1, 16, M_BESTFIT | M_WAITOK);

		sc->base_msi_irq = powerpc_register_pic(dev,
		    OF_xref_from_node(node),
		    msi_ranges[0] + msi_ranges[1], 0, FALSE);

		if (bootverbose)
			device_printf(dev, "Supports %d MSIs starting at %d\n",
			    msi_ranges[1], msi_ranges[0]);
	}

	/* Create the parent DMA tag */
	/*
	 * Constrain it to POWER8 PHB (ioda2) for now.  It seems to mess up on
	 * POWER9 systems.
	 */
	if (ofw_bus_is_compatible(dev, "ibm,ioda2-phb")) {
		err = bus_dma_tag_create(bus_get_dma_tag(dev), /* parent */
		    1, 0,				/* alignment, bounds */
		    OPAL_PCI_BUS_SPACE_LOWADDR_32BIT,	/* lowaddr */
		    BUS_SPACE_MAXADDR_32BIT,		/* highaddr */
		    NULL, NULL,				/* filter, filterarg */
		    BUS_SPACE_MAXSIZE,			/* maxsize */
		    BUS_SPACE_UNRESTRICTED,		/* nsegments */
		    BUS_SPACE_MAXSIZE,			/* maxsegsize */
		    0,					/* flags */
		    NULL, NULL,				/* lockfunc, lockarg */
		    &sc->ofw_sc.sc_dmat);
		if (err != 0) {
			device_printf(dev, "Failed to create DMA tag\n");
			return (err);
		}
	}

	/*
	 * General OFW PCI attach
	 */
	err = ofw_pci_init(dev);
	if (err != 0)
		return (err);

	/*
	 * Unfreeze non-config-space PCI operations. Let this fail silently
	 * if e.g. there is no current freeze.
	 */
	opal_call(OPAL_PCI_EEH_FREEZE_CLEAR, sc->phb_id, OPAL_PCI_DEFAULT_PE,
	    OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);

	/*
	 * OPAL stores 64-bit BARs in a special property rather than "ranges"
	 */
	if (OF_getencprop(node, "ibm,opal-m64-window",
	    m64window, sizeof(m64window)) == sizeof(m64window)) {
		struct ofw_pci_range *rp;

		sc->ofw_sc.sc_nrange++;
		sc->ofw_sc.sc_range = realloc(sc->ofw_sc.sc_range,
		    sc->ofw_sc.sc_nrange * sizeof(sc->ofw_sc.sc_range[0]),
		    M_DEVBUF, M_WAITOK);
		rp = &sc->ofw_sc.sc_range[sc->ofw_sc.sc_nrange-1];
		rp->pci_hi = OFW_PCI_PHYS_HI_SPACE_MEM64 |
		    OFW_PCI_PHYS_HI_PREFETCHABLE;
		rp->pci = ((uint64_t)m64window[0] << 32) | m64window[1];
		rp->host = ((uint64_t)m64window[2] << 32) | m64window[3];
		rp->size = ((uint64_t)m64window[4] << 32) | m64window[5];
		rman_manage_region(&sc->ofw_sc.sc_mem_rman, rp->pci,
		   rp->pci + rp->size - 1);
	}

	return (ofw_pci_attach(dev));
}

static uint32_t
opalpci_read_config(device_t dev, u_int bus, u_int slot, u_int func, u_int reg,
    int width)
{
	struct opalpci_softc *sc;
	uint64_t config_addr;
	uint8_t byte;
	uint16_t half;
	uint32_t word;
	int error;

	sc = device_get_softc(dev);

	config_addr = (bus << 8) | ((slot & 0x1f) << 3) | (func & 0x7);

	switch (width) {
	case 1:
		error = opal_call(OPAL_PCI_CONFIG_READ_BYTE, sc->phb_id,
		    config_addr, reg, vtophys(&byte));
		word = byte;
		break;
	case 2:
		error = opal_call(OPAL_PCI_CONFIG_READ_HALF_WORD, sc->phb_id,
		    config_addr, reg, vtophys(&half));
		word = half;
		break;
	case 4:
		error = opal_call(OPAL_PCI_CONFIG_READ_WORD, sc->phb_id,
		    config_addr, reg, vtophys(&word));
		break;
	default:
		error = OPAL_SUCCESS;
		word = 0xffffffff;
	}

	/*
	 * Poking config state for non-existant devices can make
	 * the host bridge hang up. Clear any errors.
	 *
	 * XXX: Make this conditional on the existence of a freeze
	 */
	opal_call(OPAL_PCI_EEH_FREEZE_CLEAR, sc->phb_id, OPAL_PCI_DEFAULT_PE,
	    OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);
	
	if (error != OPAL_SUCCESS)
		word = 0xffffffff;

	return (word);
}

static void
opalpci_write_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, uint32_t val, int width)
{
	struct opalpci_softc *sc;
	uint64_t config_addr;
	int error = OPAL_SUCCESS;

	sc = device_get_softc(dev);

	config_addr = (bus << 8) | ((slot & 0x1f) << 3) | (func & 0x7);

	switch (width) {
	case 1:
		error = opal_call(OPAL_PCI_CONFIG_WRITE_BYTE, sc->phb_id,
		    config_addr, reg, val);
		break;
	case 2:
		error = opal_call(OPAL_PCI_CONFIG_WRITE_HALF_WORD, sc->phb_id,
		    config_addr, reg, val);
		break;
	case 4:
		error = opal_call(OPAL_PCI_CONFIG_WRITE_WORD, sc->phb_id,
		    config_addr, reg, val);
		break;
	}

	if (error != OPAL_SUCCESS) {
		/*
		 * Poking config state for non-existant devices can make
		 * the host bridge hang up. Clear any errors.
		 */
		opal_call(OPAL_PCI_EEH_FREEZE_CLEAR, sc->phb_id,
		    OPAL_PCI_DEFAULT_PE, OPAL_EEH_ACTION_CLEAR_FREEZE_ALL);
	}
}

static int
opalpci_route_interrupt(device_t bus, device_t dev, int pin)
{

	return (pin);
}

static int
opalpci_alloc_msi(device_t dev, device_t child, int count, int maxcount,
    int *irqs)
{
	struct opalpci_softc *sc;
	vmem_addr_t start;
	phandle_t xref;
	int err, i;

	sc = device_get_softc(dev);
	if (sc->msi_vmem == NULL)
		return (ENODEV);

	err = vmem_xalloc(sc->msi_vmem, count, powerof2(count), 0, 0,
	    VMEM_ADDR_MIN, VMEM_ADDR_MAX, M_BESTFIT | M_WAITOK, &start);

	if (err)
		return (err);

	xref = OF_xref_from_node(ofw_bus_get_node(dev));
	for (i = 0; i < count; i++)
		irqs[i] = MAP_IRQ(xref, start + i);

	return (0);
}

static int
opalpci_release_msi(device_t dev, device_t child, int count, int *irqs)
{
	struct opalpci_softc *sc;

	sc = device_get_softc(dev);
	if (sc->msi_vmem == NULL)
		return (ENODEV);

	vmem_xfree(sc->msi_vmem, irqs[0] - sc->base_msi_irq, count);
	return (0);
}

static int
opalpci_alloc_msix(device_t dev, device_t child, int *irq)
{
	return (opalpci_alloc_msi(dev, child, 1, 1, irq));
}

static int
opalpci_release_msix(device_t dev, device_t child, int irq)
{
	return (opalpci_release_msi(dev, child, 1, &irq));
}

static int
opalpci_map_msi(device_t dev, device_t child, int irq, uint64_t *addr,
    uint32_t *data)
{
	struct opalpci_softc *sc;
	struct pci_devinfo *dinfo;
	int err, xive;

	sc = device_get_softc(dev);
	if (sc->msi_vmem == NULL)
		return (ENODEV);

	xive = irq - sc->base_msi_irq - sc->msi_base;
	opal_call(OPAL_PCI_SET_XIVE_PE, sc->phb_id, OPAL_PCI_DEFAULT_PE, xive);

	dinfo = device_get_ivars(child);
	if (dinfo->cfg.msi.msi_alloc > 0 &&
	    (dinfo->cfg.msi.msi_ctrl & PCIM_MSICTRL_64BIT) == 0) {
		uint32_t msi32;
		err = opal_call(OPAL_GET_MSI_32, sc->phb_id,
		    OPAL_PCI_DEFAULT_PE, xive, 1, vtophys(&msi32),
		    vtophys(data));
		*addr = be32toh(msi32);
	} else {
		err = opal_call(OPAL_GET_MSI_64, sc->phb_id,
		    OPAL_PCI_DEFAULT_PE, xive, 1, vtophys(addr), vtophys(data));
		*addr = be64toh(*addr);
	}
	*data = be32toh(*data);

	if (bootverbose && err != 0)
		device_printf(child, "OPAL MSI mapping error: %d\n", err);

	return ((err == 0) ? 0 : ENXIO);
}

static void
opalpic_pic_enable(device_t dev, u_int irq, u_int vector, void **priv)
{
	struct opalpci_softc *sc = device_get_softc(dev);

	PIC_ENABLE(root_pic, irq, vector, priv);
	opal_call(OPAL_PCI_MSI_EOI, sc->phb_id, irq, priv);
}

static void opalpic_pic_eoi(device_t dev, u_int irq, void *priv)
{
	struct opalpci_softc *sc;

	sc = device_get_softc(dev);
	opal_call(OPAL_PCI_MSI_EOI, sc->phb_id, irq);

	PIC_EOI(root_pic, irq, priv);
}

static bus_dma_tag_t
opalpci_get_dma_tag(device_t dev, device_t child)
{
	struct opalpci_softc *sc;

	sc = device_get_softc(dev);
	return (sc->ofw_sc.sc_dmat);
}
