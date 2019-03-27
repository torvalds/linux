/*-
 * Copyright (c) 2015 The FreeBSD Foundation
 * All rights reserved.
 *
 * This software was developed by Semihalf under
 * the sponsorship of the FreeBSD Foundation.
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
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/* PCIe external MAC root complex driver (PEM) for Cavium Thunder SOC */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/endian.h>

#ifdef FDT
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofw_pci.h>
#endif

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pci_host_generic.h>
#include <dev/pci/pcib_private.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <machine/smp.h>
#include <machine/intr.h>

#include <arm64/cavium/thunder_pcie_common.h>
#include <arm64/cavium/thunder_pcie_pem.h>
#include "pcib_if.h"

#define	THUNDER_PEM_DEVICE_ID		0xa020
#define	THUNDER_PEM_VENDOR_ID		0x177d

/* ThunderX specific defines */
#define	THUNDER_PEMn_REG_BASE(unit)	(0x87e0c0000000UL | ((unit) << 24))
#define	PCIERC_CFG002			0x08
#define	PCIERC_CFG006			0x18
#define	PCIERC_CFG032			0x80
#define	PCIERC_CFG006_SEC_BUS(reg)	(((reg) >> 8) & 0xFF)
#define	PEM_CFG_RD_REG_ALIGN(reg)	((reg) & ~0x3)
#define	PEM_CFG_RD_REG_DATA(val)	(((val) >> 32) & 0xFFFFFFFF)
#define	PEM_CFG_RD			0x30
#define	PEM_CFG_LINK_MASK		0x3
#define	PEM_CFG_LINK_RDY		0x3
#define	PEM_CFG_SLIX_TO_REG(slix)	((slix) << 4)
#define	SBNUM_OFFSET			0x8
#define	SBNUM_MASK			0xFF
#define	PEM_ON_REG			0x420
#define	PEM_CTL_STATUS			0x0
#define	PEM_LINK_ENABLE			(1 << 4)
#define	PEM_LINK_DLLA			(1 << 29)
#define	PEM_LINK_LT			(1 << 27)
#define	PEM_BUS_SHIFT			(24)
#define	PEM_SLOT_SHIFT			(19)
#define	PEM_FUNC_SHIFT			(16)
#define	SLIX_S2M_REGX_ACC		0x874001000000UL
#define	SLIX_S2M_REGX_ACC_SIZE		0x1000
#define	SLIX_S2M_REGX_ACC_SPACING	0x001000000000UL
#define	SLI_BASE			0x880000000000UL
#define	SLI_WINDOW_SPACING		0x004000000000UL
#define	SLI_PCI_OFFSET			0x001000000000UL
#define	SLI_NODE_SHIFT			(44)
#define	SLI_NODE_MASK			(3)
#define	SLI_GROUP_SHIFT			(40)
#define	SLI_ID_SHIFT			(24)
#define	SLI_ID_MASK			(7)
#define	SLI_PEMS_PER_GROUP		(3)
#define	SLI_GROUPS_PER_NODE		(2)
#define	SLI_PEMS_PER_NODE		(SLI_PEMS_PER_GROUP * SLI_GROUPS_PER_NODE)
#define	SLI_ACC_REG_CNT			(256)

/*
 * Each PEM device creates its own bus with
 * own address translation, so we can adjust bus addresses
 * as we want. To support 32-bit cards let's assume
 * PCI window assignment looks as following:
 *
 * 0x00000000 - 0x000FFFFF	IO
 * 0x00100000 - 0xFFFFFFFF	Memory
 */
#define	PCI_IO_BASE		0x00000000UL
#define	PCI_IO_SIZE		0x00100000UL
#define	PCI_MEMORY_BASE		PCI_IO_SIZE
#define	PCI_MEMORY_SIZE		0xFFF00000UL

#define	RID_PEM_SPACE		1

static int thunder_pem_activate_resource(device_t, device_t, int, int,
    struct resource *);
static int thunder_pem_adjust_resource(device_t, device_t, int,
    struct resource *, rman_res_t, rman_res_t);
static struct resource * thunder_pem_alloc_resource(device_t, device_t, int,
    int *, rman_res_t, rman_res_t, rman_res_t, u_int);
static int thunder_pem_alloc_msi(device_t, device_t, int, int, int *);
static int thunder_pem_release_msi(device_t, device_t, int, int *);
static int thunder_pem_alloc_msix(device_t, device_t, int *);
static int thunder_pem_release_msix(device_t, device_t, int);
static int thunder_pem_map_msi(device_t, device_t, int, uint64_t *, uint32_t *);
static int thunder_pem_get_id(device_t, device_t, enum pci_id_type,
    uintptr_t *);
static int thunder_pem_attach(device_t);
static int thunder_pem_deactivate_resource(device_t, device_t, int, int,
    struct resource *);
static bus_dma_tag_t thunder_pem_get_dma_tag(device_t, device_t);
static int thunder_pem_detach(device_t);
static uint64_t thunder_pem_config_reg_read(struct thunder_pem_softc *, int);
static int thunder_pem_link_init(struct thunder_pem_softc *);
static int thunder_pem_maxslots(device_t);
static int thunder_pem_probe(device_t);
static uint32_t thunder_pem_read_config(device_t, u_int, u_int, u_int, u_int,
    int);
static int thunder_pem_read_ivar(device_t, device_t, int, uintptr_t *);
static void thunder_pem_release_all(device_t);
static int thunder_pem_release_resource(device_t, device_t, int, int,
    struct resource *);
static struct rman * thunder_pem_rman(struct thunder_pem_softc *, int);
static void thunder_pem_slix_s2m_regx_acc_modify(struct thunder_pem_softc *,
    int, int);
static void thunder_pem_write_config(device_t, u_int, u_int, u_int, u_int,
    uint32_t, int);
static int thunder_pem_write_ivar(device_t, device_t, int, uintptr_t);

/* Global handlers for SLI interface */
static bus_space_handle_t sli0_s2m_regx_base = 0;
static bus_space_handle_t sli1_s2m_regx_base = 0;

static device_method_t thunder_pem_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			thunder_pem_probe),
	DEVMETHOD(device_attach,		thunder_pem_attach),
	DEVMETHOD(device_detach,		thunder_pem_detach),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,		thunder_pem_read_ivar),
	DEVMETHOD(bus_write_ivar,		thunder_pem_write_ivar),
	DEVMETHOD(bus_alloc_resource,		thunder_pem_alloc_resource),
	DEVMETHOD(bus_release_resource,		thunder_pem_release_resource),
	DEVMETHOD(bus_adjust_resource,		thunder_pem_adjust_resource),
	DEVMETHOD(bus_activate_resource,	thunder_pem_activate_resource),
	DEVMETHOD(bus_deactivate_resource,	thunder_pem_deactivate_resource),
	DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,		bus_generic_teardown_intr),

	DEVMETHOD(bus_get_dma_tag,		thunder_pem_get_dma_tag),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,		thunder_pem_maxslots),
	DEVMETHOD(pcib_read_config,		thunder_pem_read_config),
	DEVMETHOD(pcib_write_config,		thunder_pem_write_config),
	DEVMETHOD(pcib_alloc_msix,		thunder_pem_alloc_msix),
	DEVMETHOD(pcib_release_msix,		thunder_pem_release_msix),
	DEVMETHOD(pcib_alloc_msi,		thunder_pem_alloc_msi),
	DEVMETHOD(pcib_release_msi,		thunder_pem_release_msi),
	DEVMETHOD(pcib_map_msi,			thunder_pem_map_msi),
	DEVMETHOD(pcib_get_id,			thunder_pem_get_id),

	DEVMETHOD_END
};

DEFINE_CLASS_0(pcib, thunder_pem_driver, thunder_pem_methods,
    sizeof(struct thunder_pem_softc));

static devclass_t thunder_pem_devclass;
extern struct bus_space memmap_bus;

DRIVER_MODULE(thunder_pem, pci, thunder_pem_driver, thunder_pem_devclass, 0, 0);
MODULE_DEPEND(thunder_pem, pci, 1, 1, 1);

static int
thunder_pem_maxslots(device_t dev)
{

#if 0
	/* max slots per bus acc. to standard */
	return (PCI_SLOTMAX);
#else
	/*
	 * ARM64TODO Workaround - otherwise an em(4) interface appears to be
	 * present on every PCI function on the bus to which it is connected
	 */
	return (0);
#endif
}

static int
thunder_pem_read_ivar(device_t dev, device_t child, int index,
    uintptr_t *result)
{
	struct thunder_pem_softc *sc;
	int secondary_bus = 0;

	sc = device_get_softc(dev);

	if (index == PCIB_IVAR_BUS) {
		secondary_bus = thunder_pem_config_reg_read(sc, PCIERC_CFG006);
		*result = PCIERC_CFG006_SEC_BUS(secondary_bus);
		return (0);
	}
	if (index == PCIB_IVAR_DOMAIN) {
		*result = sc->id;
		return (0);
	}

	return (ENOENT);
}

static int
thunder_pem_write_ivar(device_t dev, device_t child, int index,
    uintptr_t value)
{

	return (ENOENT);
}

static int
thunder_pem_activate_resource(device_t dev, device_t child, int type, int rid,
    struct resource *r)
{
	int err;
	bus_addr_t paddr;
	bus_size_t psize;
	bus_space_handle_t vaddr;
	struct thunder_pem_softc *sc;

	if ((err = rman_activate_resource(r)) != 0)
		return (err);

	sc = device_get_softc(dev);

	/*
	 * If this is a memory resource, map it into the kernel.
	 */
	if (type == SYS_RES_MEMORY || type == SYS_RES_IOPORT) {
		paddr = (bus_addr_t)rman_get_start(r);
		psize = (bus_size_t)rman_get_size(r);

		paddr = range_addr_pci_to_phys(sc->ranges, paddr);

		err = bus_space_map(&memmap_bus, paddr, psize, 0, &vaddr);
		if (err != 0) {
			rman_deactivate_resource(r);
			return (err);
		}
		rman_set_bustag(r, &memmap_bus);
		rman_set_virtual(r, (void *)vaddr);
		rman_set_bushandle(r, vaddr);
	}
	return (0);
}

/*
 * This function is an exact copy of nexus_deactivate_resource()
 * Keep it up-to-date with all changes in nexus. To be removed
 * once bus-mapping interface is developed.
 */
static int
thunder_pem_deactivate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *r)
{
	bus_size_t psize;
	bus_space_handle_t vaddr;

	psize = (bus_size_t)rman_get_size(r);
	vaddr = rman_get_bushandle(r);

	if (vaddr != 0) {
		bus_space_unmap(&memmap_bus, vaddr, psize);
		rman_set_virtual(r, NULL);
		rman_set_bushandle(r, 0);
	}

	return (rman_deactivate_resource(r));
}

static int
thunder_pem_adjust_resource(device_t dev, device_t child, int type,
    struct resource *res, rman_res_t start, rman_res_t end)
{
	struct thunder_pem_softc *sc;
	struct rman *rm;

	sc = device_get_softc(dev);
#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	if (type == PCI_RES_BUS)
		return (pci_domain_adjust_bus(sc->id, child, res, start, end));
#endif

	rm = thunder_pem_rman(sc, type);
	if (rm == NULL)
		return (bus_generic_adjust_resource(dev, child, type, res,
		    start, end));
	if (!rman_is_region_manager(res, rm))
		/*
		 * This means a child device has a memory or I/O
		 * resource not from you which shouldn't happen.
		 */
		return (EINVAL);
	return (rman_adjust_resource(res, start, end));
}

static bus_dma_tag_t
thunder_pem_get_dma_tag(device_t dev, device_t child)
{
	struct thunder_pem_softc *sc;

	sc = device_get_softc(dev);
	return (sc->dmat);
}

static int
thunder_pem_alloc_msi(device_t pci, device_t child, int count, int maxcount,
    int *irqs)
{
	device_t bus;

	bus = device_get_parent(pci);
	return (PCIB_ALLOC_MSI(device_get_parent(bus), child, count, maxcount,
	    irqs));
}

static int
thunder_pem_release_msi(device_t pci, device_t child, int count, int *irqs)
{
	device_t bus;

	bus = device_get_parent(pci);
	return (PCIB_RELEASE_MSI(device_get_parent(bus), child, count, irqs));
}

static int
thunder_pem_alloc_msix(device_t pci, device_t child, int *irq)
{
	device_t bus;

	bus = device_get_parent(pci);
	return (PCIB_ALLOC_MSIX(device_get_parent(bus), child, irq));
}

static int
thunder_pem_release_msix(device_t pci, device_t child, int irq)
{
	device_t bus;

	bus = device_get_parent(pci);
	return (PCIB_RELEASE_MSIX(device_get_parent(bus), child, irq));
}

static int
thunder_pem_map_msi(device_t pci, device_t child, int irq, uint64_t *addr,
    uint32_t *data)
{
	device_t bus;

	bus = device_get_parent(pci);
	return (PCIB_MAP_MSI(device_get_parent(bus), child, irq, addr, data));
}

static int
thunder_pem_get_id(device_t pci, device_t child, enum pci_id_type type,
    uintptr_t *id)
{
	int bsf;
	int pem;

	if (type != PCI_ID_MSI)
		return (pcib_get_id(pci, child, type, id));

	bsf = pci_get_rid(child);

	/* PEM (PCIe MAC/root complex) number is equal to domain */
	pem = pci_get_domain(child);

	/*
	 * Set appropriate device ID (passed by the HW along with
	 * the transaction to memory) for different root complex
	 * numbers using hard-coded domain portion for each group.
	 */
	if (pem < 3)
		*id = (0x1 << PCI_RID_DOMAIN_SHIFT) | bsf;
	else if (pem < 6)
		*id = (0x3 << PCI_RID_DOMAIN_SHIFT) | bsf;
	else if (pem < 9)
		*id = (0x9 << PCI_RID_DOMAIN_SHIFT) | bsf;
	else if (pem < 12)
		*id = (0xB << PCI_RID_DOMAIN_SHIFT) | bsf;
	else
		return (ENXIO);

	return (0);
}

static int
thunder_pem_identify(device_t dev)
{
	struct thunder_pem_softc *sc;
	rman_res_t start;

	sc = device_get_softc(dev);
	start = rman_get_start(sc->reg);

	/* Calculate PEM designations from its address */
	sc->node = (start >> SLI_NODE_SHIFT) & SLI_NODE_MASK;
	sc->id = ((start >> SLI_ID_SHIFT) & SLI_ID_MASK) +
	    (SLI_PEMS_PER_NODE * sc->node);
	sc->sli = sc->id % SLI_PEMS_PER_GROUP;
	sc->sli_group = (sc->id / SLI_PEMS_PER_GROUP) % SLI_GROUPS_PER_NODE;
	sc->sli_window_base = SLI_BASE |
	    (((uint64_t)sc->node) << SLI_NODE_SHIFT) |
	    ((uint64_t)sc->sli_group << SLI_GROUP_SHIFT);
	sc->sli_window_base += SLI_WINDOW_SPACING * sc->sli;

	return (0);
}

static void
thunder_pem_slix_s2m_regx_acc_modify(struct thunder_pem_softc *sc,
    int sli_group, int slix)
{
	uint64_t regval;
	bus_space_handle_t handle = 0;

	KASSERT(slix >= 0 && slix <= SLI_ACC_REG_CNT, ("Invalid SLI index"));

	if (sli_group == 0)
		handle = sli0_s2m_regx_base;
	else if (sli_group == 1)
		handle = sli1_s2m_regx_base;
	else
		device_printf(sc->dev, "SLI group is not correct\n");

	if (handle) {
		/* Clear lower 32-bits of the SLIx register */
		regval = bus_space_read_8(sc->reg_bst, handle,
		    PEM_CFG_SLIX_TO_REG(slix));
		regval &= ~(0xFFFFFFFFUL);
		bus_space_write_8(sc->reg_bst, handle,
		    PEM_CFG_SLIX_TO_REG(slix), regval);
	}
}

static int
thunder_pem_link_init(struct thunder_pem_softc *sc)
{
	uint64_t regval;

	/* check whether PEM is safe to access. */
	regval = bus_space_read_8(sc->reg_bst, sc->reg_bsh, PEM_ON_REG);
	if ((regval & PEM_CFG_LINK_MASK) != PEM_CFG_LINK_RDY) {
		device_printf(sc->dev, "PEM%d is not ON\n", sc->id);
		return (ENXIO);
	}

	regval = bus_space_read_8(sc->reg_bst, sc->reg_bsh, PEM_CTL_STATUS);
	regval |= PEM_LINK_ENABLE;
	bus_space_write_8(sc->reg_bst, sc->reg_bsh, PEM_CTL_STATUS, regval);

	/* Wait 1ms as per Cavium specification */
	DELAY(1000);

	regval = thunder_pem_config_reg_read(sc, PCIERC_CFG032);

	if (((regval & PEM_LINK_DLLA) == 0) || ((regval & PEM_LINK_LT) != 0)) {
		device_printf(sc->dev, "PCIe RC: Port %d Link Timeout\n",
		    sc->id);
		return (ENXIO);
	}

	return (0);
}

static int
thunder_pem_init(struct thunder_pem_softc *sc)
{
	int i, retval = 0;

	retval = thunder_pem_link_init(sc);
	if (retval) {
		device_printf(sc->dev, "%s failed\n", __func__);
		return retval;
	}

	/* To support 32-bit PCIe devices, set S2M_REGx_ACC[BA]=0x0 */
	for (i = 0; i < SLI_ACC_REG_CNT; i++) {
		thunder_pem_slix_s2m_regx_acc_modify(sc, sc->sli_group, i);
	}

	return (retval);
}

static uint64_t
thunder_pem_config_reg_read(struct thunder_pem_softc *sc, int reg)
{
	uint64_t data;

	/* Write to ADDR register */
	bus_space_write_8(sc->reg_bst, sc->reg_bsh, PEM_CFG_RD,
	    PEM_CFG_RD_REG_ALIGN(reg));
	bus_space_barrier(sc->reg_bst, sc->reg_bsh, PEM_CFG_RD, 8,
	    BUS_SPACE_BARRIER_READ | BUS_SPACE_BARRIER_WRITE);
	/* Read from DATA register */
	data = PEM_CFG_RD_REG_DATA(bus_space_read_8(sc->reg_bst, sc->reg_bsh,
	    PEM_CFG_RD));

	return (data);
}

static uint32_t
thunder_pem_read_config(device_t dev, u_int bus, u_int slot,
    u_int func, u_int reg, int bytes)
{
	uint64_t offset;
	uint32_t data;
	struct thunder_pem_softc *sc;
	bus_space_tag_t	t;
	bus_space_handle_t h;

	if ((bus > PCI_BUSMAX) || (slot > PCI_SLOTMAX) ||
	    (func > PCI_FUNCMAX) || (reg > PCIE_REGMAX))
		return (~0U);

	sc = device_get_softc(dev);

	/* Calculate offset */
	offset = (bus << PEM_BUS_SHIFT) | (slot << PEM_SLOT_SHIFT) |
	    (func << PEM_FUNC_SHIFT);
	t = sc->reg_bst;
	h = sc->pem_sli_base;

	bus_space_map(sc->reg_bst, sc->sli_window_base + offset,
	    PCIE_REGMAX, 0, &h);

	switch (bytes) {
	case 1:
		data = bus_space_read_1(t, h, reg);
		break;
	case 2:
		data = le16toh(bus_space_read_2(t, h, reg));
		break;
	case 4:
		data = le32toh(bus_space_read_4(t, h, reg));
		break;
	default:
		data = ~0U;
		break;
	}

	bus_space_unmap(sc->reg_bst, h, PCIE_REGMAX);

	return (data);
}

static void
thunder_pem_write_config(device_t dev, u_int bus, u_int slot,
    u_int func, u_int reg, uint32_t val, int bytes)
{
	uint64_t offset;
	struct thunder_pem_softc *sc;
	bus_space_tag_t	t;
	bus_space_handle_t h;

	if ((bus > PCI_BUSMAX) || (slot > PCI_SLOTMAX) ||
	    (func > PCI_FUNCMAX) || (reg > PCIE_REGMAX))
		return;

	sc = device_get_softc(dev);

	/* Calculate offset */
	offset = (bus << PEM_BUS_SHIFT) | (slot << PEM_SLOT_SHIFT) |
	    (func << PEM_FUNC_SHIFT);
	t = sc->reg_bst;
	h = sc->pem_sli_base;

	bus_space_map(sc->reg_bst, sc->sli_window_base + offset,
	    PCIE_REGMAX, 0, &h);

	switch (bytes) {
	case 1:
		bus_space_write_1(t, h, reg, val);
		break;
	case 2:
		bus_space_write_2(t, h, reg, htole16(val));
		break;
	case 4:
		bus_space_write_4(t, h, reg, htole32(val));
		break;
	default:
		break;
	}

	bus_space_unmap(sc->reg_bst, h, PCIE_REGMAX);
}

static struct resource *
thunder_pem_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct thunder_pem_softc *sc = device_get_softc(dev);
	struct rman *rm = NULL;
	struct resource *res;
	device_t parent_dev;

#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	if (type == PCI_RES_BUS)
		return (pci_domain_alloc_bus(sc->id, child, rid, start,  end,
		    count, flags));
#endif
	rm = thunder_pem_rman(sc, type);
	if (rm == NULL) {
		/* Find parent device. On ThunderX we know an exact path. */
		parent_dev = device_get_parent(device_get_parent(dev));
		return (BUS_ALLOC_RESOURCE(parent_dev, dev, type, rid, start,
		    end, count, flags));
	}


	if (!RMAN_IS_DEFAULT_RANGE(start, end)) {
		/*
		 * We might get PHYS addresses here inherited from EFI.
		 * Convert to PCI if necessary.
		 */
		if (range_addr_is_phys(sc->ranges, start, count)) {
			start = range_addr_phys_to_pci(sc->ranges, start);
			end = start + count - 1;
		}

	}

	if (bootverbose) {
		device_printf(dev,
		    "thunder_pem_alloc_resource: start=%#lx, end=%#lx, count=%#lx\n",
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
	if (bootverbose) {
		device_printf(dev, "%s FAIL: type=%d, rid=%d, "
		    "start=%016lx, end=%016lx, count=%016lx, flags=%x\n",
		    __func__, type, *rid, start, end, count, flags);
	}

	return (NULL);
}

static int
thunder_pem_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *res)
{
	device_t parent_dev;
#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	struct thunder_pem_softc *sc = device_get_softc(dev);

	if (type == PCI_RES_BUS)
		return (pci_domain_release_bus(sc->id, child, rid, res));
#endif
	/* Find parent device. On ThunderX we know an exact path. */
	parent_dev = device_get_parent(device_get_parent(dev));

	if ((type != SYS_RES_MEMORY) && (type != SYS_RES_IOPORT))
		return (BUS_RELEASE_RESOURCE(parent_dev, child,
		    type, rid, res));

	return (rman_release_resource(res));
}

static struct rman *
thunder_pem_rman(struct thunder_pem_softc *sc, int type)
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

static int
thunder_pem_probe(device_t dev)
{
	uint16_t pci_vendor_id;
	uint16_t pci_device_id;

	pci_vendor_id = pci_get_vendor(dev);
	pci_device_id = pci_get_device(dev);

	if ((pci_vendor_id == THUNDER_PEM_VENDOR_ID) &&
	    (pci_device_id == THUNDER_PEM_DEVICE_ID)) {
		device_set_desc_copy(dev, THUNDER_PEM_DESC);
		return (0);
	}

	return (ENXIO);
}

static int
thunder_pem_attach(device_t dev)
{
	devclass_t pci_class;
	device_t parent;
	struct thunder_pem_softc *sc;
	int error;
	int rid;
	int tuple;
	uint64_t base, size;
	struct rman *rman;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Allocate memory for resource */
	pci_class = devclass_find("pci");
	parent = device_get_parent(dev);
	if (device_get_devclass(parent) == pci_class)
		rid = PCIR_BAR(0);
	else
		rid = RID_PEM_SPACE;

	sc->reg = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &rid, RF_ACTIVE);
	if (sc->reg == NULL) {
		device_printf(dev, "Failed to allocate resource\n");
		return (ENXIO);
	}
	sc->reg_bst = rman_get_bustag(sc->reg);
	sc->reg_bsh = rman_get_bushandle(sc->reg);

	/* Create the parent DMA tag to pass down the coherent flag */
	error = bus_dma_tag_create(bus_get_dma_tag(dev), /* parent */
	    1, 0,			/* alignment, bounds */
	    BUS_SPACE_MAXADDR,		/* lowaddr */
	    BUS_SPACE_MAXADDR,		/* highaddr */
	    NULL, NULL,			/* filter, filterarg */
	    BUS_SPACE_MAXSIZE,		/* maxsize */
	    BUS_SPACE_UNRESTRICTED,	/* nsegments */
	    BUS_SPACE_MAXSIZE,		/* maxsegsize */
	    BUS_DMA_COHERENT,		/* flags */
	    NULL, NULL,			/* lockfunc, lockarg */
	    &sc->dmat);
	if (error != 0)
		return (error);

	/* Map SLI, do it only once */
	if (!sli0_s2m_regx_base) {
		bus_space_map(sc->reg_bst, SLIX_S2M_REGX_ACC,
		    SLIX_S2M_REGX_ACC_SIZE, 0, &sli0_s2m_regx_base);
	}
	if (!sli1_s2m_regx_base) {
		bus_space_map(sc->reg_bst, SLIX_S2M_REGX_ACC +
		    SLIX_S2M_REGX_ACC_SPACING, SLIX_S2M_REGX_ACC_SIZE, 0,
		    &sli1_s2m_regx_base);
	}

	if ((sli0_s2m_regx_base == 0) || (sli1_s2m_regx_base == 0)) {
		device_printf(dev,
		    "bus_space_map failed to map slix_s2m_regx_base\n");
		goto fail;
	}

	/* Identify PEM */
	if (thunder_pem_identify(dev) != 0)
		goto fail;

	/* Initialize rman and allocate regions */
	sc->mem_rman.rm_type = RMAN_ARRAY;
	sc->mem_rman.rm_descr = "PEM PCIe Memory";
	error = rman_init(&sc->mem_rman);
	if (error != 0) {
		device_printf(dev, "memory rman_init() failed. error = %d\n",
		    error);
		goto fail;
	}
	sc->io_rman.rm_type = RMAN_ARRAY;
	sc->io_rman.rm_descr = "PEM PCIe IO";
	error = rman_init(&sc->io_rman);
	if (error != 0) {
		device_printf(dev, "IO rman_init() failed. error = %d\n",
		    error);
		goto fail_mem;
	}

	/*
	 * We ignore the values that may have been provided in FDT
	 * and configure ranges according to the below formula
	 * for all types of devices. This is because some DTBs provided
	 * by EFI do not have proper ranges property or don't have them
	 * at all.
	 */
	/* Fill memory window */
	sc->ranges[0].pci_base = PCI_MEMORY_BASE;
	sc->ranges[0].size = PCI_MEMORY_SIZE;
	sc->ranges[0].phys_base = sc->sli_window_base + SLI_PCI_OFFSET +
	    sc->ranges[0].pci_base;
	sc->ranges[0].flags = SYS_RES_MEMORY;

	/* Fill IO window */
	sc->ranges[1].pci_base = PCI_IO_BASE;
	sc->ranges[1].size = PCI_IO_SIZE;
	sc->ranges[1].phys_base = sc->sli_window_base + SLI_PCI_OFFSET +
	    sc->ranges[1].pci_base;
	sc->ranges[1].flags = SYS_RES_IOPORT;

	for (tuple = 0; tuple < MAX_RANGES_TUPLES; tuple++) {
		base = sc->ranges[tuple].pci_base;
		size = sc->ranges[tuple].size;
		if (size == 0)
			continue; /* empty range element */

		rman = thunder_pem_rman(sc, sc->ranges[tuple].flags);
		if (rman != NULL)
			error = rman_manage_region(rman, base,
			    base + size - 1);
		else
			error = EINVAL;
		if (error) {
			device_printf(dev,
			    "rman_manage_region() failed. error = %d\n", error);
			rman_fini(&sc->mem_rman);
			return (error);
		}
		if (bootverbose) {
			device_printf(dev,
			    "\tPCI addr: 0x%jx, CPU addr: 0x%jx, Size: 0x%jx, Flags:0x%jx\n",
			    sc->ranges[tuple].pci_base,
			    sc->ranges[tuple].phys_base,
			    sc->ranges[tuple].size,
			    sc->ranges[tuple].flags);
		}
	}

	if (thunder_pem_init(sc)) {
		device_printf(dev, "Failure during PEM init\n");
		goto fail_io;
	}

	device_add_child(dev, "pci", -1);

	return (bus_generic_attach(dev));

fail_io:
	rman_fini(&sc->io_rman);
fail_mem:
	rman_fini(&sc->mem_rman);
fail:
	bus_free_resource(dev, SYS_RES_MEMORY, sc->reg);
	return (ENXIO);
}

static void
thunder_pem_release_all(device_t dev)
{
	struct thunder_pem_softc *sc;

	sc = device_get_softc(dev);

	rman_fini(&sc->io_rman);
	rman_fini(&sc->mem_rman);

	if (sc->reg != NULL)
		bus_free_resource(dev, SYS_RES_MEMORY, sc->reg);
}

static int
thunder_pem_detach(device_t dev)
{

	thunder_pem_release_all(dev);

	return (0);
}
