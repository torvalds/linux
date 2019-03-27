/*-
 * Copyright (c) 2016 Michal Meloun <mmel@FreeBSD.org>
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

/*
 * Nvidia Integrated PCI/PCI-Express controller driver.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/devmap.h>
#include <sys/proc.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <machine/intr.h>

#include <vm/vm.h>
#include <vm/vm_extern.h>
#include <vm/vm_kern.h>
#include <vm/pmap.h>

#include <dev/extres/clk/clk.h>
#include <dev/extres/hwreset/hwreset.h>
#include <dev/extres/phy/phy.h>
#include <dev/extres/regulator/regulator.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/ofwpci.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcib_private.h>

#include <machine/resource.h>
#include <machine/bus.h>

#include <arm/nvidia/tegra_pmc.h>

#include "ofw_bus_if.h"
#include "msi_if.h"
#include "pcib_if.h"
#include "pic_if.h"


#define	AFI_AXI_BAR0_SZ				0x000
#define	AFI_AXI_BAR1_SZ				0x004
#define	AFI_AXI_BAR2_SZ				0x008
#define	AFI_AXI_BAR3_SZ				0x00c
#define	AFI_AXI_BAR4_SZ				0x010
#define	AFI_AXI_BAR5_SZ				0x014
#define	AFI_AXI_BAR0_START			0x018
#define	AFI_AXI_BAR1_START			0x01c
#define	AFI_AXI_BAR2_START			0x020
#define	AFI_AXI_BAR3_START			0x024
#define	AFI_AXI_BAR4_START			0x028
#define	AFI_AXI_BAR5_START			0x02c
#define	AFI_FPCI_BAR0				0x030
#define	AFI_FPCI_BAR1				0x034
#define	AFI_FPCI_BAR2				0x038
#define	AFI_FPCI_BAR3				0x03c
#define	AFI_FPCI_BAR4				0x040
#define	AFI_FPCI_BAR5				0x044
#define	AFI_MSI_BAR_SZ				0x060
#define	AFI_MSI_FPCI_BAR_ST			0x064
#define	AFI_MSI_AXI_BAR_ST			0x068
#define AFI_MSI_VEC(x)				(0x06c + 4 * (x))
#define AFI_MSI_EN_VEC(x)			(0x08c + 4 * (x))
#define	 AFI_MSI_INTR_IN_REG				32
#define	 AFI_MSI_REGS					8

#define	AFI_CONFIGURATION			0x0ac
#define	 AFI_CONFIGURATION_EN_FPCI			(1 << 0)

#define	AFI_FPCI_ERROR_MASKS			0x0b0
#define	AFI_INTR_MASK				0x0b4
#define	 AFI_INTR_MASK_MSI_MASK				(1 << 8)
#define	 AFI_INTR_MASK_INT_MASK				(1 << 0)

#define	AFI_INTR_CODE				0x0b8
#define	 AFI_INTR_CODE_MASK				0xf
#define	 AFI_INTR_CODE_INT_CODE_INI_SLVERR		1
#define	 AFI_INTR_CODE_INT_CODE_INI_DECERR		2
#define	 AFI_INTR_CODE_INT_CODE_TGT_SLVERR		3
#define	 AFI_INTR_CODE_INT_CODE_TGT_DECERR		4
#define	 AFI_INTR_CODE_INT_CODE_TGT_WRERR		5
#define	 AFI_INTR_CODE_INT_CODE_SM_MSG			6
#define	 AFI_INTR_CODE_INT_CODE_DFPCI_DECERR		7
#define	 AFI_INTR_CODE_INT_CODE_AXI_DECERR		8
#define	 AFI_INTR_CODE_INT_CODE_FPCI_TIMEOUT		9
#define	 AFI_INTR_CODE_INT_CODE_PE_PRSNT_SENSE		10
#define	 AFI_INTR_CODE_INT_CODE_PE_CLKREQ_SENSE		11
#define	 AFI_INTR_CODE_INT_CODE_CLKCLAMP_SENSE		12
#define	 AFI_INTR_CODE_INT_CODE_RDY4PD_SENSE		13
#define	 AFI_INTR_CODE_INT_CODE_P2P_ERROR		14


#define	AFI_INTR_SIGNATURE			0x0bc
#define	AFI_UPPER_FPCI_ADDRESS			0x0c0
#define	AFI_SM_INTR_ENABLE			0x0c4
#define	 AFI_SM_INTR_RP_DEASSERT			(1 << 14)
#define	 AFI_SM_INTR_RP_ASSERT				(1 << 13)
#define	 AFI_SM_INTR_HOTPLUG				(1 << 12)
#define	 AFI_SM_INTR_PME				(1 << 11)
#define	 AFI_SM_INTR_FATAL_ERROR			(1 << 10)
#define	 AFI_SM_INTR_UNCORR_ERROR			(1 <<  9)
#define	 AFI_SM_INTR_CORR_ERROR				(1 <<  8)
#define	 AFI_SM_INTR_INTD_DEASSERT			(1 <<  7)
#define	 AFI_SM_INTR_INTC_DEASSERT			(1 <<  6)
#define	 AFI_SM_INTR_INTB_DEASSERT			(1 <<  5)
#define	 AFI_SM_INTR_INTA_DEASSERT			(1 <<  4)
#define	 AFI_SM_INTR_INTD_ASSERT			(1 <<  3)
#define	 AFI_SM_INTR_INTC_ASSERT			(1 <<  2)
#define	 AFI_SM_INTR_INTB_ASSERT			(1 <<  1)
#define	 AFI_SM_INTR_INTA_ASSERT			(1 <<  0)

#define	AFI_AFI_INTR_ENABLE			0x0c8
#define	 AFI_AFI_INTR_ENABLE_CODE(code)			(1 << (code))

#define	AFI_PCIE_CONFIG				0x0f8
#define	 AFI_PCIE_CONFIG_PCIE_DISABLE(x)		(1 << ((x) + 1))
#define	 AFI_PCIE_CONFIG_PCIE_DISABLE_ALL		0x6
#define	 AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_MASK	(0xf << 20)
#define	 AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_XBAR2_1	(0x0 << 20)
#define	 AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_XBAR4_1	(0x1 << 20)

#define	AFI_FUSE				0x104
#define	 AFI_FUSE_PCIE_T0_GEN2_DIS			(1 << 2)

#define	AFI_PEX0_CTRL				0x110
#define	AFI_PEX1_CTRL				0x118
#define	AFI_PEX2_CTRL				0x128
#define	 AFI_PEX_CTRL_OVERRIDE_EN			(1 << 4)
#define	 AFI_PEX_CTRL_REFCLK_EN				(1 << 3)
#define	 AFI_PEX_CTRL_CLKREQ_EN				(1 << 1)
#define	 AFI_PEX_CTRL_RST_L				(1 << 0)

#define	AFI_AXI_BAR6_SZ				0x134
#define	AFI_AXI_BAR7_SZ				0x138
#define	AFI_AXI_BAR8_SZ				0x13c
#define	AFI_AXI_BAR6_START			0x140
#define	AFI_AXI_BAR7_START			0x144
#define	AFI_AXI_BAR8_START			0x148
#define	AFI_FPCI_BAR6				0x14c
#define	AFI_FPCI_BAR7				0x150
#define	AFI_FPCI_BAR8				0x154
#define	AFI_PLLE_CONTROL			0x160
#define	 AFI_PLLE_CONTROL_BYPASS_PADS2PLLE_CONTROL	(1 << 9)
#define	 AFI_PLLE_CONTROL_BYPASS_PCIE2PLLE_CONTROL	(1 << 8)
#define	 AFI_PLLE_CONTROL_PADS2PLLE_CONTROL_EN		(1 << 1)
#define	 AFI_PLLE_CONTROL_PCIE2PLLE_CONTROL_EN		(1 << 0)

#define	AFI_PEXBIAS_CTRL			0x168

/* FPCI Address space */
#define	FPCI_MAP_IO			0xfdfc000000ULL
#define	FPCI_MAP_TYPE0_CONFIG		0xfdfc000000ULL
#define	FPCI_MAP_TYPE1_CONFIG		0xfdff000000ULL
#define	FPCI_MAP_EXT_TYPE0_CONFIG	0xfe00000000ULL
#define	FPCI_MAP_EXT_TYPE1_CONFIG	0xfe10000000ULL

/* Configuration space */
#define	RP_VEND_XP	0x00000F00
#define	 RP_VEND_XP_DL_UP	(1 << 30)

#define	RP_PRIV_MISC	0x00000FE0
#define	 RP_PRIV_MISC_PRSNT_MAP_EP_PRSNT (0xE << 0)
#define	 RP_PRIV_MISC_PRSNT_MAP_EP_ABSNT (0xF << 0)

#define	RP_LINK_CONTROL_STATUS			0x00000090
#define	 RP_LINK_CONTROL_STATUS_DL_LINK_ACTIVE	0x20000000
#define	 RP_LINK_CONTROL_STATUS_LINKSTAT_MASK	0x3fff0000

/* Wait 50 ms (per port) for link. */
#define	TEGRA_PCIE_LINKUP_TIMEOUT	50000

#define TEGRA_PCIB_MSI_ENABLE

#define	DEBUG
#ifdef DEBUG
#define	debugf(fmt, args...) do { printf(fmt,##args); } while (0)
#else
#define	debugf(fmt, args...)
#endif

/*
 * Configuration space format:
 *    [27:24] extended register
 *    [23:16] bus
 *    [15:11] slot (device)
 *    [10: 8] function
 *    [ 7: 0] register
 */
#define	PCI_CFG_EXT_REG(reg)	((((reg) >> 8) & 0x0f) << 24)
#define	PCI_CFG_BUS(bus)	(((bus) & 0xff) << 16)
#define	PCI_CFG_DEV(dev)	(((dev) & 0x1f) << 11)
#define	PCI_CFG_FUN(fun)	(((fun) & 0x07) << 8)
#define	PCI_CFG_BASE_REG(reg)	((reg)  & 0xff)

#define	PADS_WR4(_sc, _r, _v)	bus_write_4((_sc)-pads_mem_res, (_r), (_v))
#define	PADS_RD4(_sc, _r)	bus_read_4((_sc)->pads_mem_res, (_r))
#define	AFI_WR4(_sc, _r, _v)	bus_write_4((_sc)->afi_mem_res, (_r), (_v))
#define	AFI_RD4(_sc, _r)	bus_read_4((_sc)->afi_mem_res, (_r))

static struct {
	bus_size_t	axi_start;
	bus_size_t	fpci_start;
	bus_size_t	size;
} bars[] = {
    {AFI_AXI_BAR0_START, AFI_FPCI_BAR0, AFI_AXI_BAR0_SZ},	/* BAR 0 */
    {AFI_AXI_BAR1_START, AFI_FPCI_BAR1, AFI_AXI_BAR1_SZ},	/* BAR 1 */
    {AFI_AXI_BAR2_START, AFI_FPCI_BAR2, AFI_AXI_BAR2_SZ},	/* BAR 2 */
    {AFI_AXI_BAR3_START, AFI_FPCI_BAR3, AFI_AXI_BAR3_SZ},	/* BAR 3 */
    {AFI_AXI_BAR4_START, AFI_FPCI_BAR4, AFI_AXI_BAR4_SZ},	/* BAR 4 */
    {AFI_AXI_BAR5_START, AFI_FPCI_BAR5, AFI_AXI_BAR5_SZ},	/* BAR 5 */
    {AFI_AXI_BAR6_START, AFI_FPCI_BAR6, AFI_AXI_BAR6_SZ},	/* BAR 6 */
    {AFI_AXI_BAR7_START, AFI_FPCI_BAR7, AFI_AXI_BAR7_SZ},	/* BAR 7 */
    {AFI_AXI_BAR8_START, AFI_FPCI_BAR8, AFI_AXI_BAR8_SZ},	/* BAR 8 */
    {AFI_MSI_AXI_BAR_ST, AFI_MSI_FPCI_BAR_ST, AFI_MSI_BAR_SZ},	/* MSI 9 */
};

/* Compatible devices. */
static struct ofw_compat_data compat_data[] = {
	{"nvidia,tegra124-pcie",	1},
	{NULL,		 		0},
};

#define	TEGRA_FLAG_MSI_USED	0x0001
struct tegra_pcib_irqsrc {
	struct intr_irqsrc	isrc;
	u_int			irq;
	u_int			flags;
};

struct tegra_pcib_port {
	int		enabled;
	int 		port_idx;		/* chip port index */
	int		num_lanes;		/* number of lanes */
	bus_size_t	afi_pex_ctrl;		/* offset of afi_pex_ctrl */
	phy_t		phy;			/* port phy */

	/* Config space properties. */
	bus_addr_t	rp_base_addr;		/* PA of config window */
	bus_size_t	rp_size;		/* size of config window */
	bus_space_handle_t cfg_handle;		/* handle of config window */
};

#define	TEGRA_PCIB_MAX_PORTS	3
#define	TEGRA_PCIB_MAX_MSI	AFI_MSI_INTR_IN_REG * AFI_MSI_REGS
struct tegra_pcib_softc {
	struct ofw_pci_softc	ofw_pci;
	device_t		dev;
	struct mtx		mtx;
	struct resource		*pads_mem_res;
	struct resource		*afi_mem_res;
	struct resource		*cfg_mem_res;
	struct resource 	*irq_res;
	struct resource 	*msi_irq_res;
	void			*intr_cookie;
	void			*msi_intr_cookie;

	struct ofw_pci_range	mem_range;
	struct ofw_pci_range	pref_mem_range;
	struct ofw_pci_range	io_range;

	clk_t			clk_pex;
	clk_t			clk_afi;
	clk_t			clk_pll_e;
	clk_t			clk_cml;
	hwreset_t		hwreset_pex;
	hwreset_t		hwreset_afi;
	hwreset_t		hwreset_pcie_x;
	regulator_t		supply_avddio_pex;
	regulator_t		supply_dvddio_pex;
	regulator_t		supply_avdd_pex_pll;
	regulator_t		supply_hvdd_pex;
	regulator_t		supply_hvdd_pex_pll_e;
	regulator_t		supply_vddio_pex_ctl;
	regulator_t		supply_avdd_pll_erefe;

	vm_offset_t		msi_page;	/* VA of MSI page */
	bus_addr_t		cfg_base_addr;	/* base address of config */
	bus_size_t		cfg_cur_offs; 	/* currently mapped window */
	bus_space_handle_t 	cfg_handle;	/* handle of config window */
	bus_space_tag_t 	bus_tag;	/* tag of config window */
	int			lanes_cfg;
	int			num_ports;
	struct tegra_pcib_port *ports[TEGRA_PCIB_MAX_PORTS];
	struct tegra_pcib_irqsrc *isrcs;
};

static int
tegra_pcib_maxslots(device_t dev)
{
	return (16);
}

static int
tegra_pcib_route_interrupt(device_t bus, device_t dev, int pin)
{
	struct tegra_pcib_softc *sc;
	u_int irq;

	sc = device_get_softc(bus);
	irq = intr_map_clone_irq(rman_get_start(sc->irq_res));
	device_printf(bus, "route pin %d for device %d.%d to %u\n",
		      pin, pci_get_slot(dev), pci_get_function(dev),
		      irq);

	return (irq);
}

static int
tegra_pcbib_map_cfg(struct tegra_pcib_softc *sc, u_int bus, u_int slot,
    u_int func, u_int reg)
{
	bus_size_t offs;
	int rv;

	offs = sc->cfg_base_addr;
	offs |= PCI_CFG_BUS(bus) | PCI_CFG_DEV(slot) | PCI_CFG_FUN(func) |
	    PCI_CFG_EXT_REG(reg);
	if ((sc->cfg_handle != 0) && (sc->cfg_cur_offs == offs))
		return (0);
	if (sc->cfg_handle != 0)
		bus_space_unmap(sc->bus_tag, sc->cfg_handle, 0x800);

	rv = bus_space_map(sc->bus_tag, offs, 0x800, 0, &sc->cfg_handle);
	if (rv != 0)
		device_printf(sc->dev, "Cannot map config space\n");
	else
		sc->cfg_cur_offs = offs;
	return (rv);
}

static uint32_t
tegra_pcib_read_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, int bytes)
{
	struct tegra_pcib_softc *sc;
	bus_space_handle_t hndl;
	uint32_t off;
	uint32_t val;
	int rv, i;

	sc = device_get_softc(dev);
	if (bus == 0) {
		if (func != 0)
			return (0xFFFFFFFF);
		for (i = 0; i < TEGRA_PCIB_MAX_PORTS; i++) {
			if ((sc->ports[i] != NULL) &&
			    (sc->ports[i]->port_idx == slot)) {
				hndl = sc->ports[i]->cfg_handle;
				off = reg & 0xFFF;
				break;
			}
		}
		if (i >= TEGRA_PCIB_MAX_PORTS)
			return (0xFFFFFFFF);
	} else {
		rv = tegra_pcbib_map_cfg(sc, bus, slot, func, reg);
		if (rv != 0)
			return (0xFFFFFFFF);
		hndl = sc->cfg_handle;
		off = PCI_CFG_BASE_REG(reg);
	}

	val = bus_space_read_4(sc->bus_tag, hndl, off & ~3);
	switch (bytes) {
	case 4:
		break;
	case 2:
		if (off & 3)
			val >>= 16;
		val &= 0xffff;
		break;
	case 1:
		val >>= ((off & 3) << 3);
		val &= 0xff;
		break;
	}
	return val;
}

static void
tegra_pcib_write_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, uint32_t val, int bytes)
{
	struct tegra_pcib_softc *sc;
	bus_space_handle_t hndl;
	uint32_t off;
	uint32_t val2;
	int rv, i;

	sc = device_get_softc(dev);
	if (bus == 0) {
		if (func != 0)
			return;
		for (i = 0; i < TEGRA_PCIB_MAX_PORTS; i++) {
			if ((sc->ports[i] != NULL) &&
			    (sc->ports[i]->port_idx == slot)) {
				hndl = sc->ports[i]->cfg_handle;
				off = reg & 0xFFF;
				break;
			}
		}
		if (i >= TEGRA_PCIB_MAX_PORTS)
			return;
	} else {
		rv = tegra_pcbib_map_cfg(sc, bus, slot, func, reg);
		if (rv != 0)
			return;
		hndl = sc->cfg_handle;
		off = PCI_CFG_BASE_REG(reg);
	}

	switch (bytes) {
	case 4:
		bus_space_write_4(sc->bus_tag, hndl, off, val);
		break;
	case 2:
		val2 = bus_space_read_4(sc->bus_tag, hndl, off & ~3);
		val2 &= ~(0xffff << ((off & 3) << 3));
		val2 |= ((val & 0xffff) << ((off & 3) << 3));
		bus_space_write_4(sc->bus_tag, hndl, off & ~3, val2);
		break;
	case 1:
		val2 = bus_space_read_4(sc->bus_tag, hndl, off & ~3);
		val2 &= ~(0xff << ((off & 3) << 3));
		val2 |= ((val & 0xff) << ((off & 3) << 3));
		bus_space_write_4(sc->bus_tag, hndl, off & ~3, val2);
		break;
	}
}

static int tegra_pci_intr(void *arg)
{
	struct tegra_pcib_softc *sc = arg;
	uint32_t code, signature;

	code = bus_read_4(sc->afi_mem_res, AFI_INTR_CODE) & AFI_INTR_CODE_MASK;
	signature = bus_read_4(sc->afi_mem_res, AFI_INTR_SIGNATURE);
	bus_write_4(sc->afi_mem_res, AFI_INTR_CODE, 0);
	if (code == AFI_INTR_CODE_INT_CODE_SM_MSG)
		return(FILTER_STRAY);

	printf("tegra_pci_intr: code %x sig %x\n", code, signature);
	return (FILTER_HANDLED);
}

/* -----------------------------------------------------------------------
 *
 * 	PCI MSI interface
 */
static int
tegra_pcib_alloc_msi(device_t pci, device_t child, int count, int maxcount,
    int *irqs)
{
	phandle_t msi_parent;

	/* XXXX ofw_bus_msimap() don't works for Tegra DT.
	ofw_bus_msimap(ofw_bus_get_node(pci), pci_get_rid(child), &msi_parent,
	    NULL);
	*/
	msi_parent = OF_xref_from_node(ofw_bus_get_node(pci));
	return (intr_alloc_msi(pci, child, msi_parent, count, maxcount,
	    irqs));
}

static int
tegra_pcib_release_msi(device_t pci, device_t child, int count, int *irqs)
{
	phandle_t msi_parent;

	/* XXXX ofw_bus_msimap() don't works for Tegra DT.
	ofw_bus_msimap(ofw_bus_get_node(pci), pci_get_rid(child), &msi_parent,
	    NULL);
	*/
	msi_parent = OF_xref_from_node(ofw_bus_get_node(pci));
	return (intr_release_msi(pci, child, msi_parent, count, irqs));
}

static int
tegra_pcib_map_msi(device_t pci, device_t child, int irq, uint64_t *addr,
    uint32_t *data)
{
	phandle_t msi_parent;

	/* XXXX ofw_bus_msimap() don't works for Tegra DT.
	ofw_bus_msimap(ofw_bus_get_node(pci), pci_get_rid(child), &msi_parent,
	    NULL);
	*/
	msi_parent = OF_xref_from_node(ofw_bus_get_node(pci));
	return (intr_map_msi(pci, child, msi_parent, irq, addr, data));
}

#ifdef TEGRA_PCIB_MSI_ENABLE

/* --------------------------------------------------------------------------
 *
 * Interrupts
 *
 */

static inline void
tegra_pcib_isrc_mask(struct tegra_pcib_softc *sc,
     struct tegra_pcib_irqsrc *tgi, uint32_t val)
{
	uint32_t reg;
	int offs, bit;

	offs = tgi->irq / AFI_MSI_INTR_IN_REG;
	bit = 1 << (tgi->irq % AFI_MSI_INTR_IN_REG);

	if (val != 0)
		AFI_WR4(sc, AFI_MSI_VEC(offs), bit);
	reg = AFI_RD4(sc, AFI_MSI_EN_VEC(offs));
	if (val !=  0)
		reg |= bit;
	else
		reg &= ~bit;
	AFI_WR4(sc, AFI_MSI_EN_VEC(offs), reg);
}

static int
tegra_pcib_msi_intr(void *arg)
{
	u_int irq, i, bit, reg;
	struct tegra_pcib_softc *sc;
	struct trapframe *tf;
	struct tegra_pcib_irqsrc *tgi;

	sc = (struct tegra_pcib_softc *)arg;
	tf = curthread->td_intr_frame;

	for (i = 0; i < AFI_MSI_REGS; i++) {
		reg = AFI_RD4(sc, AFI_MSI_VEC(i));
		/* Handle one vector. */
		while (reg != 0) {
			bit = ffs(reg) - 1;
			/* Send EOI */
			AFI_WR4(sc, AFI_MSI_VEC(i), 1 << bit);
			irq = i * AFI_MSI_INTR_IN_REG + bit;
			tgi = &sc->isrcs[irq];
			if (intr_isrc_dispatch(&tgi->isrc, tf) != 0) {
				/* Disable stray. */
				tegra_pcib_isrc_mask(sc, tgi, 0);
				device_printf(sc->dev,
				    "Stray irq %u disabled\n", irq);
			}
			reg = AFI_RD4(sc, AFI_MSI_VEC(i));
		}
	}
	return (FILTER_HANDLED);
}

static int
tegra_pcib_msi_attach(struct tegra_pcib_softc *sc)
{
	int error;
	uint32_t irq;
	const char *name;

	sc->isrcs = malloc(sizeof(*sc->isrcs) * TEGRA_PCIB_MAX_MSI, M_DEVBUF,
	    M_WAITOK | M_ZERO);

	name = device_get_nameunit(sc->dev);
	for (irq = 0; irq < TEGRA_PCIB_MAX_MSI; irq++) {
		sc->isrcs[irq].irq = irq;
		error = intr_isrc_register(&sc->isrcs[irq].isrc,
		    sc->dev, 0, "%s,%u", name, irq);
		if (error != 0)
			return (error); /* XXX deregister ISRCs */
	}
	if (intr_msi_register(sc->dev,
	    OF_xref_from_node(ofw_bus_get_node(sc->dev))) != 0)
		return (ENXIO);

	return (0);
}

static int
tegra_pcib_msi_detach(struct tegra_pcib_softc *sc)
{

	/*
	 *  There has not been established any procedure yet
	 *  how to detach PIC from living system correctly.
	 */
	device_printf(sc->dev, "%s: not implemented yet\n", __func__);
	return (EBUSY);
}


static void
tegra_pcib_msi_disable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct tegra_pcib_softc *sc;
	struct tegra_pcib_irqsrc *tgi;

	sc = device_get_softc(dev);
	tgi = (struct tegra_pcib_irqsrc *)isrc;
	tegra_pcib_isrc_mask(sc, tgi, 0);
}

static void
tegra_pcib_msi_enable_intr(device_t dev, struct intr_irqsrc *isrc)
{
	struct tegra_pcib_softc *sc;
	struct tegra_pcib_irqsrc *tgi;

	sc = device_get_softc(dev);
	tgi = (struct tegra_pcib_irqsrc *)isrc;
	tegra_pcib_isrc_mask(sc, tgi, 1);
}

/* MSI interrupts are edge trigered -> do nothing */
static void
tegra_pcib_msi_post_filter(device_t dev, struct intr_irqsrc *isrc)
{
}

static void
tegra_pcib_msi_post_ithread(device_t dev, struct intr_irqsrc *isrc)
{
}

static void
tegra_pcib_msi_pre_ithread(device_t dev, struct intr_irqsrc *isrc)
{
}

static int
tegra_pcib_msi_setup_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct tegra_pcib_softc *sc;
	struct tegra_pcib_irqsrc *tgi;

	sc = device_get_softc(dev);
	tgi = (struct tegra_pcib_irqsrc *)isrc;

	if (data == NULL || data->type != INTR_MAP_DATA_MSI)
		return (ENOTSUP);

	if (isrc->isrc_handlers == 0)
		tegra_pcib_msi_enable_intr(dev, isrc);

	return (0);
}

static int
tegra_pcib_msi_teardown_intr(device_t dev, struct intr_irqsrc *isrc,
    struct resource *res, struct intr_map_data *data)
{
	struct tegra_pcib_softc *sc;
	struct tegra_pcib_irqsrc *tgi;

	sc = device_get_softc(dev);
	tgi = (struct tegra_pcib_irqsrc *)isrc;

	if (isrc->isrc_handlers == 0)
		tegra_pcib_isrc_mask(sc, tgi, 0);
	return (0);
}


static int
tegra_pcib_msi_alloc_msi(device_t dev, device_t child, int count, int maxcount,
    device_t *pic, struct intr_irqsrc **srcs)
{
	struct tegra_pcib_softc *sc;
	int i, irq, end_irq;
	bool found;

	KASSERT(powerof2(count), ("%s: bad count", __func__));
	KASSERT(powerof2(maxcount), ("%s: bad maxcount", __func__));

	sc = device_get_softc(dev);
	mtx_lock(&sc->mtx);

	found = false;
	for (irq = 0; (irq + count - 1) < TEGRA_PCIB_MAX_MSI; irq++) {
		/* Start on an aligned interrupt */
		if ((irq & (maxcount - 1)) != 0)
			continue;

		/* Assume we found a valid range until shown otherwise */
		found = true;

		/* Check this range is valid */
		for (end_irq = irq; end_irq < irq + count; end_irq++) {
			/* This is already used */
			if ((sc->isrcs[end_irq].flags & TEGRA_FLAG_MSI_USED) ==
			    TEGRA_FLAG_MSI_USED) {
				found = false;
				break;
			}
		}

		if (found)
			break;
	}

	/* Not enough interrupts were found */
	if (!found || irq == (TEGRA_PCIB_MAX_MSI - 1)) {
		mtx_unlock(&sc->mtx);
		return (ENXIO);
	}

	for (i = 0; i < count; i++) {
		/* Mark the interrupt as used */
		sc->isrcs[irq + i].flags |= TEGRA_FLAG_MSI_USED;

	}
	mtx_unlock(&sc->mtx);

	for (i = 0; i < count; i++)
		srcs[i] = (struct intr_irqsrc *)&sc->isrcs[irq + i];
	*pic = device_get_parent(dev);
	return (0);
}

static int
tegra_pcib_msi_release_msi(device_t dev, device_t child, int count,
    struct intr_irqsrc **isrc)
{
	struct tegra_pcib_softc *sc;
	struct tegra_pcib_irqsrc *ti;
	int i;

	sc = device_get_softc(dev);
	mtx_lock(&sc->mtx);
	for (i = 0; i < count; i++) {
		ti = (struct tegra_pcib_irqsrc *)isrc[i];

		KASSERT((ti->flags & TEGRA_FLAG_MSI_USED) == TEGRA_FLAG_MSI_USED,
		    ("%s: Trying to release an unused MSI-X interrupt",
		    __func__));

		ti->flags &= ~TEGRA_FLAG_MSI_USED;
	}
	mtx_unlock(&sc->mtx);
	return (0);
}

static int
tegra_pcib_msi_map_msi(device_t dev, device_t child, struct intr_irqsrc *isrc,
    uint64_t *addr, uint32_t *data)
{
	struct tegra_pcib_softc *sc = device_get_softc(dev);
	struct tegra_pcib_irqsrc *ti = (struct tegra_pcib_irqsrc *)isrc;

	*addr = vtophys(sc->msi_page);
	*data = ti->irq;
	return (0);
}
#endif

/* ------------------------------------------------------------------- */
static bus_size_t
tegra_pcib_pex_ctrl(struct tegra_pcib_softc *sc, int port)
{
	if (port >= TEGRA_PCIB_MAX_PORTS)
		panic("invalid port number: %d\n", port);

	if (port == 0)
		return (AFI_PEX0_CTRL);
	else if (port == 1)
		return (AFI_PEX1_CTRL);
	else if (port == 2)
		return (AFI_PEX2_CTRL);
	else
		panic("invalid port number: %d\n", port);
}

static int
tegra_pcib_enable_fdt_resources(struct tegra_pcib_softc *sc)
{
	int rv;

	rv = hwreset_assert(sc->hwreset_pcie_x);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot assert 'pcie_x' reset\n");
		return (rv);
	}
	rv = hwreset_assert(sc->hwreset_afi);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot assert  'afi' reset\n");
		return (rv);
	}
	rv = hwreset_assert(sc->hwreset_pex);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot assert  'pex' reset\n");
		return (rv);
	}

	tegra_powergate_power_off(TEGRA_POWERGATE_PCX);

	/* Power supplies. */
	rv = regulator_enable(sc->supply_avddio_pex);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'avddio_pex' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_dvddio_pex);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'dvddio_pex' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_avdd_pex_pll);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'avdd-pex-pll' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_hvdd_pex);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'hvdd-pex-supply' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_hvdd_pex_pll_e);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'hvdd-pex-pll-e-supply' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_vddio_pex_ctl);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'vddio-pex-ctl' regulator\n");
		return (rv);
	}
	rv = regulator_enable(sc->supply_avdd_pll_erefe);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot enable 'avdd-pll-erefe-supply' regulator\n");
		return (rv);
	}

	rv = tegra_powergate_sequence_power_up(TEGRA_POWERGATE_PCX,
	    sc->clk_pex, sc->hwreset_pex);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable 'PCX' powergate\n");
		return (rv);
	}

	rv = hwreset_deassert(sc->hwreset_afi);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot unreset 'afi' reset\n");
		return (rv);
	}

	rv = clk_enable(sc->clk_afi);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable 'afi' clock\n");
		return (rv);
	}
	rv = clk_enable(sc->clk_cml);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable 'cml' clock\n");
		return (rv);
	}
	rv = clk_enable(sc->clk_pll_e);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable 'pll_e' clock\n");
		return (rv);
	}
	return (0);
}

static struct tegra_pcib_port *
tegra_pcib_parse_port(struct tegra_pcib_softc *sc, phandle_t node)
{
	struct tegra_pcib_port *port;
	uint32_t tmp[5];
	char tmpstr[6];
	int rv;

	port = malloc(sizeof(struct tegra_pcib_port), M_DEVBUF, M_WAITOK);

	rv = OF_getprop(node, "status", tmpstr, sizeof(tmpstr));
	if (rv <= 0 || strcmp(tmpstr, "okay") == 0 ||
	   strcmp(tmpstr, "ok") == 0)
		port->enabled = 1;
	else
		port->enabled = 0;

	rv = OF_getencprop(node, "assigned-addresses", tmp, sizeof(tmp));
	if (rv != sizeof(tmp)) {
		device_printf(sc->dev, "Cannot parse assigned-address: %d\n",
		    rv);
		goto fail;
	}
	port->rp_base_addr = tmp[2];
	port->rp_size = tmp[4];
	port->port_idx = OFW_PCI_PHYS_HI_DEVICE(tmp[0]) - 1;
	if (port->port_idx >= TEGRA_PCIB_MAX_PORTS) {
		device_printf(sc->dev, "Invalid port index: %d\n",
		    port->port_idx);
		goto fail;
	}
	/* XXX - TODO:
	 * Implement proper function for parsing pci "reg" property:
	 *  - it have PCI bus format
	 *  - its relative to matching "assigned-addresses"
	 */
	rv = OF_getencprop(node, "reg", tmp, sizeof(tmp));
	if (rv != sizeof(tmp)) {
		device_printf(sc->dev, "Cannot parse reg: %d\n", rv);
		goto fail;
	}
	port->rp_base_addr += tmp[2];

	rv = OF_getencprop(node, "nvidia,num-lanes", &port->num_lanes,
	    sizeof(port->num_lanes));
	if (rv != sizeof(port->num_lanes)) {
		device_printf(sc->dev, "Cannot parse nvidia,num-lanes: %d\n",
		    rv);
		goto fail;
	}
	if (port->num_lanes > 4) {
		device_printf(sc->dev, "Invalid nvidia,num-lanes: %d\n",
		    port->num_lanes);
		goto fail;
	}

	port->afi_pex_ctrl = tegra_pcib_pex_ctrl(sc, port->port_idx);
	sc->lanes_cfg |= port->num_lanes << (4 * port->port_idx);

	/* Phy. */
	rv = phy_get_by_ofw_name(sc->dev, node, "pcie-0", &port->phy);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'pcie-0' phy for port %d\n",
		    port->port_idx);
		goto fail;
	}

	return (port);
fail:
	free(port, M_DEVBUF);
	return (NULL);
}


static int
tegra_pcib_parse_fdt_resources(struct tegra_pcib_softc *sc, phandle_t node)
{
	phandle_t child;
	struct tegra_pcib_port *port;
	int rv;

	/* Power supplies. */
	rv = regulator_get_by_ofw_property(sc->dev, 0, "avddio-pex-supply",
	    &sc->supply_avddio_pex);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'avddio-pex' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "dvddio-pex-supply",
	     &sc->supply_dvddio_pex);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'dvddio-pex' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "avdd-pex-pll-supply",
	     &sc->supply_avdd_pex_pll);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'avdd-pex-pll' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "hvdd-pex-supply",
	     &sc->supply_hvdd_pex);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'hvdd-pex' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "hvdd-pex-pll-e-supply",
	     &sc->supply_hvdd_pex_pll_e);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'hvdd-pex-pll-e' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "vddio-pex-ctl-supply",
	    &sc->supply_vddio_pex_ctl);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'vddio-pex-ctl' regulator\n");
		return (ENXIO);
	}
	rv = regulator_get_by_ofw_property(sc->dev, 0, "avdd-pll-erefe-supply",
	     &sc->supply_avdd_pll_erefe);
	if (rv != 0) {
		device_printf(sc->dev,
		    "Cannot get 'avdd-pll-erefe' regulator\n");
		return (ENXIO);
	}

	/* Resets. */
	rv = hwreset_get_by_ofw_name(sc->dev, 0, "pex", &sc->hwreset_pex);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'pex' reset\n");
		return (ENXIO);
	}
	rv = hwreset_get_by_ofw_name(sc->dev, 0, "afi", &sc->hwreset_afi);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'afi' reset\n");
		return (ENXIO);
	}
	rv = hwreset_get_by_ofw_name(sc->dev, 0, "pcie_x", &sc->hwreset_pcie_x);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'pcie_x' reset\n");
		return (ENXIO);
	}

	/* Clocks. */
	rv = clk_get_by_ofw_name(sc->dev, 0, "pex", &sc->clk_pex);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'pex' clock\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "afi", &sc->clk_afi);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'afi' clock\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "pll_e", &sc->clk_pll_e);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'pll_e' clock\n");
		return (ENXIO);
	}
	rv = clk_get_by_ofw_name(sc->dev, 0, "cml", &sc->clk_cml);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot get 'cml' clock\n");
		return (ENXIO);
	}

	/* Ports */
	sc->num_ports = 0;
	for (child = OF_child(node); child != 0; child = OF_peer(child)) {
		port = tegra_pcib_parse_port(sc, child);
		if (port == NULL) {
			device_printf(sc->dev, "Cannot parse PCIe port node\n");
			return (ENXIO);
		}
		sc->ports[sc->num_ports++] = port;
	}

	return (0);
}

static int
tegra_pcib_decode_ranges(struct tegra_pcib_softc *sc,
    struct ofw_pci_range *ranges, int nranges)
{
	int i;

	for (i = 2; i < nranges; i++) {
		if ((ranges[i].pci_hi & OFW_PCI_PHYS_HI_SPACEMASK)  ==
		    OFW_PCI_PHYS_HI_SPACE_IO) {
			if (sc->io_range.size != 0) {
				device_printf(sc->dev,
				    "Duplicated IO range found in DT\n");
				return (ENXIO);
			}
			sc->io_range = ranges[i];
		}
		if (((ranges[i].pci_hi & OFW_PCI_PHYS_HI_SPACEMASK) ==
		    OFW_PCI_PHYS_HI_SPACE_MEM32))  {
			if (ranges[i].pci_hi & OFW_PCI_PHYS_HI_PREFETCHABLE) {
				if (sc->pref_mem_range.size != 0) {
					device_printf(sc->dev,
					    "Duplicated memory range found "
					    "in DT\n");
					return (ENXIO);
				}
				sc->pref_mem_range = ranges[i];
			} else {
				if (sc->mem_range.size != 0) {
					device_printf(sc->dev,
					    "Duplicated memory range found "
					    "in DT\n");
					return (ENXIO);
				}
				sc->mem_range = ranges[i];
			}
		}
	}
	if ((sc->io_range.size == 0) || (sc->mem_range.size == 0)
	    || (sc->pref_mem_range.size == 0)) {
		device_printf(sc->dev,
		    " Not all required ranges are found in DT\n");
		return (ENXIO);
	}
	return (0);
}

/*
 * Hardware config.
 */
static int
tegra_pcib_wait_for_link(struct tegra_pcib_softc *sc,
    struct tegra_pcib_port *port)
{
	uint32_t reg;
	int i;


	/* Setup link detection. */
	reg = tegra_pcib_read_config(sc->dev, 0, port->port_idx, 0,
	    RP_PRIV_MISC, 4);
	reg &= ~RP_PRIV_MISC_PRSNT_MAP_EP_ABSNT;
	reg |= RP_PRIV_MISC_PRSNT_MAP_EP_PRSNT;
	tegra_pcib_write_config(sc->dev, 0, port->port_idx, 0,
	    RP_PRIV_MISC, reg, 4);

	for (i = TEGRA_PCIE_LINKUP_TIMEOUT; i > 0; i--) {
		reg = tegra_pcib_read_config(sc->dev, 0, port->port_idx, 0,
		    RP_VEND_XP, 4);
		if (reg & RP_VEND_XP_DL_UP)
				break;
		DELAY(1);

	}
	if (i <= 0)
		return (ETIMEDOUT);

	for (i = TEGRA_PCIE_LINKUP_TIMEOUT; i > 0; i--) {
		reg = tegra_pcib_read_config(sc->dev, 0, port->port_idx, 0,
		    RP_LINK_CONTROL_STATUS, 4);
		if (reg & RP_LINK_CONTROL_STATUS_DL_LINK_ACTIVE)
				break;

		DELAY(1);
	}
	if (i <= 0)
		return (ETIMEDOUT);
	return (0);
}

static void
tegra_pcib_port_enable(struct tegra_pcib_softc *sc, int port_num)
{
	struct tegra_pcib_port *port;
	uint32_t reg;
	int rv;

	port = sc->ports[port_num];

	/* Put port to reset. */
	reg = AFI_RD4(sc, port->afi_pex_ctrl);
	reg &= ~AFI_PEX_CTRL_RST_L;
	AFI_WR4(sc, port->afi_pex_ctrl, reg);
	AFI_RD4(sc, port->afi_pex_ctrl);
	DELAY(10);

	/* Enable clocks. */
	reg |= AFI_PEX_CTRL_REFCLK_EN;
	reg |= AFI_PEX_CTRL_CLKREQ_EN;
	reg |= AFI_PEX_CTRL_OVERRIDE_EN;
	AFI_WR4(sc, port->afi_pex_ctrl, reg);
	AFI_RD4(sc, port->afi_pex_ctrl);
	DELAY(100);

	/* Release reset. */
	reg |= AFI_PEX_CTRL_RST_L;
	AFI_WR4(sc, port->afi_pex_ctrl, reg);

	rv = tegra_pcib_wait_for_link(sc, port);
	if (bootverbose)
		device_printf(sc->dev, " port %d (%d lane%s): Link is %s\n",
			 port->port_idx, port->num_lanes,
			 port->num_lanes > 1 ? "s": "",
			 rv == 0 ? "up": "down");
}


static void
tegra_pcib_port_disable(struct tegra_pcib_softc *sc, uint32_t port_num)
{
	struct tegra_pcib_port *port;
	uint32_t reg;

	port = sc->ports[port_num];

	/* Put port to reset. */
	reg = AFI_RD4(sc, port->afi_pex_ctrl);
	reg &= ~AFI_PEX_CTRL_RST_L;
	AFI_WR4(sc, port->afi_pex_ctrl, reg);
	AFI_RD4(sc, port->afi_pex_ctrl);
	DELAY(10);

	/* Disable clocks. */
	reg &= ~AFI_PEX_CTRL_CLKREQ_EN;
	reg &= ~AFI_PEX_CTRL_REFCLK_EN;
	AFI_WR4(sc, port->afi_pex_ctrl, reg);

	if (bootverbose)
		device_printf(sc->dev, " port %d (%d lane%s): Disabled\n",
			 port->port_idx, port->num_lanes,
			 port->num_lanes > 1 ? "s": "");
}

static void
tegra_pcib_set_bar(struct tegra_pcib_softc *sc, int bar, uint32_t axi,
    uint64_t fpci, uint32_t size, int is_memory)
{
	uint32_t fpci_reg;
	uint32_t axi_reg;
	uint32_t size_reg;

	axi_reg = axi & ~0xFFF;
	size_reg = size >> 12;
	fpci_reg = (uint32_t)(fpci >> 8) & ~0xF;
	fpci_reg |= is_memory ? 0x1 : 0x0;
	AFI_WR4(sc, bars[bar].axi_start, axi_reg);
	AFI_WR4(sc, bars[bar].size, size_reg);
	AFI_WR4(sc, bars[bar].fpci_start, fpci_reg);
}

static int
tegra_pcib_enable(struct tegra_pcib_softc *sc)
{
	int rv;
	int i;
	uint32_t reg;

	rv = tegra_pcib_enable_fdt_resources(sc);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot enable FDT resources\n");
		return (rv);
	}
	/* Enable PLLE control. */
	reg = AFI_RD4(sc, AFI_PLLE_CONTROL);
	reg &= ~AFI_PLLE_CONTROL_BYPASS_PADS2PLLE_CONTROL;
	reg |= AFI_PLLE_CONTROL_PADS2PLLE_CONTROL_EN;
	AFI_WR4(sc, AFI_PLLE_CONTROL, reg);

	/* Set bias pad. */
	AFI_WR4(sc, AFI_PEXBIAS_CTRL, 0);

	/* Configure mode and ports. */
	reg = AFI_RD4(sc, AFI_PCIE_CONFIG);
	reg &= ~AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_MASK;
	if (sc->lanes_cfg == 0x14) {
		if (bootverbose)
			device_printf(sc->dev,
			    "Using x1,x4 configuration\n");
		reg |= AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_XBAR4_1;
	} else if (sc->lanes_cfg == 0x12) {
		if (bootverbose)
			device_printf(sc->dev,
			    "Using x1,x2 configuration\n");
		reg |= AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_XBAR2_1;
	} else {
		device_printf(sc->dev,
		    "Unsupported lanes configuration: 0x%X\n", sc->lanes_cfg);
	}
	reg |= AFI_PCIE_CONFIG_PCIE_DISABLE_ALL;
	for (i = 0; i < TEGRA_PCIB_MAX_PORTS; i++) {
		if ((sc->ports[i] != NULL))
			reg &=
			 ~AFI_PCIE_CONFIG_PCIE_DISABLE(sc->ports[i]->port_idx);
	}
	AFI_WR4(sc, AFI_PCIE_CONFIG, reg);

	/* Enable Gen2 support. */
	reg = AFI_RD4(sc, AFI_FUSE);
	reg &= ~AFI_FUSE_PCIE_T0_GEN2_DIS;
	AFI_WR4(sc, AFI_FUSE, reg);

	for (i = 0; i < TEGRA_PCIB_MAX_PORTS; i++) {
		if (sc->ports[i] != NULL) {
			rv = phy_enable(sc->ports[i]->phy);
			if (rv != 0) {
				device_printf(sc->dev,
				    "Cannot enable phy for port %d\n",
				    sc->ports[i]->port_idx);
				return (rv);
			}
		}
	}


	rv = hwreset_deassert(sc->hwreset_pcie_x);
	if (rv != 0) {
		device_printf(sc->dev, "Cannot unreset  'pci_x' reset\n");
		return (rv);
	}

	/* Enable config space. */
	reg = AFI_RD4(sc, AFI_CONFIGURATION);
	reg |= AFI_CONFIGURATION_EN_FPCI;
	AFI_WR4(sc, AFI_CONFIGURATION, reg);

	/* Enable AFI errors. */
	reg = 0;
	reg |= AFI_AFI_INTR_ENABLE_CODE(AFI_INTR_CODE_INT_CODE_INI_SLVERR);
	reg |= AFI_AFI_INTR_ENABLE_CODE(AFI_INTR_CODE_INT_CODE_INI_DECERR);
	reg |= AFI_AFI_INTR_ENABLE_CODE(AFI_INTR_CODE_INT_CODE_TGT_SLVERR);
	reg |= AFI_AFI_INTR_ENABLE_CODE(AFI_INTR_CODE_INT_CODE_TGT_DECERR);
	reg |= AFI_AFI_INTR_ENABLE_CODE(AFI_INTR_CODE_INT_CODE_TGT_WRERR);
	reg |= AFI_AFI_INTR_ENABLE_CODE(AFI_INTR_CODE_INT_CODE_SM_MSG);
	reg |= AFI_AFI_INTR_ENABLE_CODE(AFI_INTR_CODE_INT_CODE_DFPCI_DECERR);
	reg |= AFI_AFI_INTR_ENABLE_CODE(AFI_INTR_CODE_INT_CODE_AXI_DECERR);
	reg |= AFI_AFI_INTR_ENABLE_CODE(AFI_INTR_CODE_INT_CODE_FPCI_TIMEOUT);
	reg |= AFI_AFI_INTR_ENABLE_CODE(AFI_INTR_CODE_INT_CODE_PE_PRSNT_SENSE);
	reg |= AFI_AFI_INTR_ENABLE_CODE(AFI_INTR_CODE_INT_CODE_PE_CLKREQ_SENSE);
	reg |= AFI_AFI_INTR_ENABLE_CODE(AFI_INTR_CODE_INT_CODE_CLKCLAMP_SENSE);
	reg |= AFI_AFI_INTR_ENABLE_CODE(AFI_INTR_CODE_INT_CODE_RDY4PD_SENSE);
	reg |= AFI_AFI_INTR_ENABLE_CODE(AFI_INTR_CODE_INT_CODE_P2P_ERROR);
	AFI_WR4(sc, AFI_AFI_INTR_ENABLE, reg);
	AFI_WR4(sc, AFI_SM_INTR_ENABLE, 0xffffffff);

	/* Enable INT, disable MSI. */
	AFI_WR4(sc, AFI_INTR_MASK, AFI_INTR_MASK_INT_MASK);

	/* Mask all FPCI errors. */
	AFI_WR4(sc, AFI_FPCI_ERROR_MASKS, 0);

	/* Setup AFI translation windows. */
	/* BAR 0 - type 1 extended configuration. */
	tegra_pcib_set_bar(sc, 0, rman_get_start(sc->cfg_mem_res),
	   FPCI_MAP_EXT_TYPE1_CONFIG, rman_get_size(sc->cfg_mem_res), 0);

	/* BAR 1 - downstream I/O. */
	tegra_pcib_set_bar(sc, 1, sc->io_range.host, FPCI_MAP_IO,
	    sc->io_range.size, 0);

	/* BAR 2 - downstream prefetchable memory 1:1. */
	tegra_pcib_set_bar(sc, 2, sc->pref_mem_range.host,
	    sc->pref_mem_range.host, sc->pref_mem_range.size, 1);

	/* BAR 3 - downstream not prefetchable memory 1:1 .*/
	tegra_pcib_set_bar(sc, 3, sc->mem_range.host,
	    sc->mem_range.host, sc->mem_range.size, 1);

	/* BAR 3-8 clear. */
	tegra_pcib_set_bar(sc, 4, 0, 0, 0, 0);
	tegra_pcib_set_bar(sc, 5, 0, 0, 0, 0);
	tegra_pcib_set_bar(sc, 6, 0, 0, 0, 0);
	tegra_pcib_set_bar(sc, 7, 0, 0, 0, 0);
	tegra_pcib_set_bar(sc, 8, 0, 0, 0, 0);

	/* MSI BAR - clear. */
	tegra_pcib_set_bar(sc, 9, 0, 0, 0, 0);
	return(0);
}

#ifdef TEGRA_PCIB_MSI_ENABLE
static int
tegra_pcib_attach_msi(device_t dev)
{
	struct tegra_pcib_softc *sc;
	uint32_t reg;
	int i, rv;

	sc = device_get_softc(dev);

	sc->msi_page = kmem_alloc_contig(PAGE_SIZE, M_WAITOK, 0,
	    BUS_SPACE_MAXADDR, PAGE_SIZE, 0, VM_MEMATTR_DEFAULT);

	/* MSI BAR */
	tegra_pcib_set_bar(sc, 9, vtophys(sc->msi_page), vtophys(sc->msi_page),
	    PAGE_SIZE, 0);

	/* Disble and clear all interrupts. */
	for (i = 0; i < AFI_MSI_REGS; i++) {
		AFI_WR4(sc, AFI_MSI_EN_VEC(i), 0);
		AFI_WR4(sc, AFI_MSI_VEC(i), 0xFFFFFFFF);
	}
	rv = bus_setup_intr(dev, sc->msi_irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
	    tegra_pcib_msi_intr, NULL, sc, &sc->msi_intr_cookie);
	if (rv != 0) {
		device_printf(dev, "cannot setup MSI interrupt handler\n");
		rv = ENXIO;
		goto out;
	}

	if (tegra_pcib_msi_attach(sc) != 0) {
		device_printf(dev, "WARNING: unable to attach PIC\n");
		tegra_pcib_msi_detach(sc);
		goto out;
	}

	/* Unmask  MSI interrupt. */
	reg = AFI_RD4(sc, AFI_INTR_MASK);
	reg |= AFI_INTR_MASK_MSI_MASK;
	AFI_WR4(sc, AFI_INTR_MASK, reg);

out:
	return (rv);
}
#endif

static int
tegra_pcib_probe(device_t dev)
{
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
		device_set_desc(dev, "Nvidia Integrated PCI/PCI-E Controller");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
tegra_pcib_attach(device_t dev)
{
	struct tegra_pcib_softc *sc;
	phandle_t node;
	int rv;
	int rid;
	struct tegra_pcib_port *port;
	int i;

	sc = device_get_softc(dev);
	sc->dev = dev;
	mtx_init(&sc->mtx, "msi_mtx", NULL, MTX_DEF);

	node = ofw_bus_get_node(dev);

	rv = tegra_pcib_parse_fdt_resources(sc, node);
	if (rv != 0) {
		device_printf(dev, "Cannot get FDT resources\n");
		return (rv);
	}

	/* Allocate bus_space resources. */
	rid = 0;
	sc->pads_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->pads_mem_res == NULL) {
		device_printf(dev, "Cannot allocate PADS register\n");
		rv = ENXIO;
		goto out;
	}
	/*
	 * XXX - FIXME
	 * tag for config space is not filled when RF_ALLOCATED flag is used.
	 */
	sc->bus_tag = rman_get_bustag(sc->pads_mem_res);

	rid = 1;
	sc->afi_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->afi_mem_res == NULL) {
		device_printf(dev, "Cannot allocate AFI register\n");
		rv = ENXIO;
		goto out;
	}

	rid = 2;
	sc->cfg_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid,
	    RF_ALLOCATED);
	if (sc->cfg_mem_res == NULL) {
		device_printf(dev, "Cannot allocate config space memory\n");
		rv = ENXIO;
		goto out;
	}
	sc->cfg_base_addr = rman_get_start(sc->cfg_mem_res);


	/* Map RP slots */
	for (i = 0; i < TEGRA_PCIB_MAX_PORTS; i++) {
		if (sc->ports[i] == NULL)
			continue;
		port = sc->ports[i];
		rv = bus_space_map(sc->bus_tag, port->rp_base_addr,
		    port->rp_size, 0, &port->cfg_handle);
		if (rv != 0) {
			device_printf(sc->dev, "Cannot allocate memory for "
			    "port: %d\n", i);
			rv = ENXIO;
			goto out;
		}
	}

	/*
	 * Get PCI interrupt
	 */
	rid = 0;
	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE | RF_SHAREABLE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "Cannot allocate IRQ resources\n");
		rv = ENXIO;
		goto out;
	}

	rid = 1;
	sc->msi_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "Cannot allocate MSI IRQ resources\n");
		rv = ENXIO;
		goto out;
	}

	sc->ofw_pci.sc_range_mask = 0x3;
	rv = ofw_pci_init(dev);
	if (rv != 0)
		goto out;

	rv = tegra_pcib_decode_ranges(sc, sc->ofw_pci.sc_range,
	    sc->ofw_pci.sc_nrange);
	if (rv != 0)
		goto out;

	if (bus_setup_intr(dev, sc->irq_res, INTR_TYPE_BIO | INTR_MPSAFE,
		    tegra_pci_intr, NULL, sc, &sc->intr_cookie)) {
		device_printf(dev, "cannot setup interrupt handler\n");
		rv = ENXIO;
		goto out;
	}

	/*
	 * Enable PCIE device.
	 */
	rv = tegra_pcib_enable(sc);
	if (rv != 0)
		goto out;
	for (i = 0; i < TEGRA_PCIB_MAX_PORTS; i++) {
		if (sc->ports[i] == NULL)
			continue;
		if (sc->ports[i]->enabled)
			tegra_pcib_port_enable(sc, i);
		else
			tegra_pcib_port_disable(sc, i);
	}

#ifdef TEGRA_PCIB_MSI_ENABLE
	rv = tegra_pcib_attach_msi(dev);
	if (rv != 0)
		 goto out;
#endif
	device_add_child(dev, "pci", -1);

	return (bus_generic_attach(dev));

out:

	return (rv);
}


static device_method_t tegra_pcib_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			tegra_pcib_probe),
	DEVMETHOD(device_attach,		tegra_pcib_attach),

	/* Bus interface */
	DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,		bus_generic_teardown_intr),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,		tegra_pcib_maxslots),
	DEVMETHOD(pcib_read_config,		tegra_pcib_read_config),
	DEVMETHOD(pcib_write_config,		tegra_pcib_write_config),
	DEVMETHOD(pcib_route_interrupt,		tegra_pcib_route_interrupt),
	DEVMETHOD(pcib_alloc_msi,		tegra_pcib_alloc_msi),
	DEVMETHOD(pcib_release_msi,		tegra_pcib_release_msi),
	DEVMETHOD(pcib_map_msi,			tegra_pcib_map_msi),
	DEVMETHOD(pcib_request_feature,		pcib_request_feature_allow),

#ifdef TEGRA_PCIB_MSI_ENABLE
	/* MSI/MSI-X */
	DEVMETHOD(msi_alloc_msi,		tegra_pcib_msi_alloc_msi),
	DEVMETHOD(msi_release_msi,		tegra_pcib_msi_release_msi),
	DEVMETHOD(msi_map_msi,			tegra_pcib_msi_map_msi),

	/* Interrupt controller interface */
	DEVMETHOD(pic_disable_intr,		tegra_pcib_msi_disable_intr),
	DEVMETHOD(pic_enable_intr,		tegra_pcib_msi_enable_intr),
	DEVMETHOD(pic_setup_intr,		tegra_pcib_msi_setup_intr),
	DEVMETHOD(pic_teardown_intr,		tegra_pcib_msi_teardown_intr),
	DEVMETHOD(pic_post_filter,		tegra_pcib_msi_post_filter),
	DEVMETHOD(pic_post_ithread,		tegra_pcib_msi_post_ithread),
	DEVMETHOD(pic_pre_ithread,		tegra_pcib_msi_pre_ithread),
#endif

	/* OFW bus interface */
	DEVMETHOD(ofw_bus_get_compat,		ofw_bus_gen_get_compat),
	DEVMETHOD(ofw_bus_get_model,		ofw_bus_gen_get_model),
	DEVMETHOD(ofw_bus_get_name,		ofw_bus_gen_get_name),
	DEVMETHOD(ofw_bus_get_node,		ofw_bus_gen_get_node),
	DEVMETHOD(ofw_bus_get_type,		ofw_bus_gen_get_type),

	DEVMETHOD_END
};

static devclass_t pcib_devclass;
DEFINE_CLASS_1(pcib, tegra_pcib_driver, tegra_pcib_methods,
    sizeof(struct tegra_pcib_softc), ofw_pci_driver);
DRIVER_MODULE(pcib, simplebus, tegra_pcib_driver, pcib_devclass,
    NULL, NULL);
