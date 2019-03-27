/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003-2012 Broadcom Corporation
 * All Rights Reserved
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY BROADCOM ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL BROADCOM OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/types.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/rman.h>
#include <sys/pciio.h>

#include <machine/bus.h>
#include <machine/md_var.h>
#include <machine/intr_machdep.h>
#include <machine/cpuregs.h>

#include <vm/vm.h>
#include <vm/vm_param.h>
#include <vm/pmap.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pci_private.h>

#include <dev/uart/uart.h>
#include <dev/uart/uart_bus.h>
#include <dev/uart/uart_cpu.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <mips/nlm/hal/haldefs.h>
#include <mips/nlm/interrupt.h>
#include <mips/nlm/hal/iomap.h>
#include <mips/nlm/hal/mips-extns.h>
#include <mips/nlm/hal/pic.h>
#include <mips/nlm/hal/bridge.h>
#include <mips/nlm/hal/gbu.h>
#include <mips/nlm/hal/pcibus.h>
#include <mips/nlm/hal/uart.h>
#include <mips/nlm/xlp.h>

#include "pcib_if.h"
#include <dev/pci/pcib_private.h>
#include "pci_if.h"

static int
xlp_pci_attach(device_t dev)
{
	struct pci_devinfo *dinfo;
	device_t pcib;
	int maxslots, s, f, pcifunchigh, irq;
	int busno, node, devoffset;
	uint16_t devid;
	uint8_t hdrtype;

	/*
	 * The on-chip devices are on a bus that is almost, but not
	 * quite, completely like PCI. Add those things by hand.
	 */
	pcib = device_get_parent(dev);
	busno = pcib_get_bus(dev);
	maxslots = PCIB_MAXSLOTS(pcib);
	for (s = 0; s <= maxslots; s++) {
		pcifunchigh = 0;
		f = 0;
		hdrtype = PCIB_READ_CONFIG(pcib, busno, s, f, PCIR_HDRTYPE, 1);
		if ((hdrtype & PCIM_HDRTYPE) > PCI_MAXHDRTYPE)
			continue;
		if (hdrtype & PCIM_MFDEV)
			pcifunchigh = PCI_FUNCMAX;
		node = s / 8;
		for (f = 0; f <= pcifunchigh; f++) {
			devoffset = XLP_HDR_OFFSET(node, 0, s % 8, f);
			if (!nlm_dev_exists(devoffset))
				continue;

			/* Find if there is a desc for the SoC device */
			devid = PCIB_READ_CONFIG(pcib, busno, s, f, PCIR_DEVICE, 2);

			/* Skip devices that don't have a proper PCI header */
			switch (devid) {
			case PCI_DEVICE_ID_NLM_ICI:
			case PCI_DEVICE_ID_NLM_PIC:
			case PCI_DEVICE_ID_NLM_FMN:
			case PCI_DEVICE_ID_NLM_UART:
			case PCI_DEVICE_ID_NLM_I2C:
			case PCI_DEVICE_ID_NLM_NOR:
			case PCI_DEVICE_ID_NLM_MMC:
				continue;
			case PCI_DEVICE_ID_NLM_EHCI:
				irq = PIC_USB_IRQ(f);
				PCIB_WRITE_CONFIG(pcib, busno, s, f,
				    XLP_PCI_DEVSCRATCH_REG0 << 2,
				    (1 << 8) | irq, 4);
			}
			dinfo = pci_read_device(pcib, dev, pcib_get_domain(dev),
			    busno, s, f);
			pci_add_child(dev, dinfo);
		}
	}
	return (bus_generic_attach(dev));
}

static int
xlp_pci_probe(device_t dev)
{
	device_t pcib;

	pcib = device_get_parent(dev);
	/*
	 * Only the top level bus has SoC devices, leave the rest to
	 * Generic PCI code
	 */
	if (strcmp(device_get_nameunit(pcib), "pcib0") != 0)
		return (ENXIO);
	device_set_desc(dev, "XLP SoCbus");
	return (BUS_PROBE_DEFAULT);
}

static devclass_t pci_devclass;
static device_method_t xlp_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		xlp_pci_probe),
	DEVMETHOD(device_attach,	xlp_pci_attach),
	DEVMETHOD(bus_rescan,		bus_null_rescan),
	DEVMETHOD_END
};

DEFINE_CLASS_1(pci, xlp_pci_driver, xlp_pci_methods, sizeof(struct pci_softc),
    pci_driver);
DRIVER_MODULE(xlp_pci, pcib, xlp_pci_driver, pci_devclass, 0, 0);

static int
xlp_pcib_probe(device_t dev)
{

	if (ofw_bus_is_compatible(dev, "netlogic,xlp-pci")) {
		device_set_desc(dev, "XLP PCI bus");
		return (BUS_PROBE_DEFAULT);
	}
	return (ENXIO);
}

static int
xlp_pcib_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{

	switch (which) {
	case PCIB_IVAR_DOMAIN:
		*result = 0;
		return (0);
	case PCIB_IVAR_BUS:
		*result = 0;
		return (0);
	}
	return (ENOENT);
}

static int
xlp_pcib_write_ivar(device_t dev, device_t child, int which, uintptr_t result)
{
	switch (which) {
	case PCIB_IVAR_DOMAIN:
		return (EINVAL);
	case PCIB_IVAR_BUS:
		return (EINVAL);
	}
	return (ENOENT);
}

static int
xlp_pcib_maxslots(device_t dev)
{

	return (PCI_SLOTMAX);
}

static u_int32_t
xlp_pcib_read_config(device_t dev, u_int b, u_int s, u_int f,
    u_int reg, int width)
{
	uint32_t data = 0;
	uint64_t cfgaddr;
	int	regindex = reg/sizeof(uint32_t);

	cfgaddr = nlm_pcicfg_base(XLP_HDR_OFFSET(0, b, s, f));
	if ((width == 2) && (reg & 1))
		return 0xFFFFFFFF;
	else if ((width == 4) && (reg & 3))
		return 0xFFFFFFFF;

	/*
	 * The intline and int pin of SoC devices are DOA, except
	 * for bridges (slot %8 == 1).
	 * use the values we stashed in a writable PCI scratch reg.
	 */
	if (b == 0 && regindex == 0xf && s % 8 > 1)
		regindex = XLP_PCI_DEVSCRATCH_REG0;

	data = nlm_read_pci_reg(cfgaddr, regindex);
	if (width == 1)
		return ((data >> ((reg & 3) << 3)) & 0xff);
	else if (width == 2)
		return ((data >> ((reg & 3) << 3)) & 0xffff);
	else
		return (data);
}

static void
xlp_pcib_write_config(device_t dev, u_int b, u_int s, u_int f,
    u_int reg, u_int32_t val, int width)
{
	uint64_t cfgaddr;
	uint32_t data = 0;
	int	regindex = reg / sizeof(uint32_t);

	cfgaddr = nlm_pcicfg_base(XLP_HDR_OFFSET(0, b, s, f));
	if ((width == 2) && (reg & 1))
		return;
	else if ((width == 4) && (reg & 3))
		return;

	if (width == 1) {
		data = nlm_read_pci_reg(cfgaddr, regindex);
		data = (data & ~(0xff << ((reg & 3) << 3))) |
		    (val << ((reg & 3) << 3));
	} else if (width == 2) {
		data = nlm_read_pci_reg(cfgaddr, regindex);
		data = (data & ~(0xffff << ((reg & 3) << 3))) |
		    (val << ((reg & 3) << 3));
	} else {
		data = val;
	}

	/*
	 * use shadow reg for intpin/intline which are dead
	 */
	if (b == 0 && regindex == 0xf && s % 8 > 1)
		regindex = XLP_PCI_DEVSCRATCH_REG0;
	nlm_write_pci_reg(cfgaddr, regindex, data);
}

/*
 * Enable byte swap in hardware when compiled big-endian.
 * Programs a link's PCIe SWAP regions from the link's IO and MEM address
 * ranges.
 */
static void
xlp_pcib_hardware_swap_enable(int node, int link)
{
#if BYTE_ORDER == BIG_ENDIAN
	uint64_t bbase, linkpcibase;
	uint32_t bar;
	int pcieoffset;

	pcieoffset = XLP_IO_PCIE_OFFSET(node, link);
	if (!nlm_dev_exists(pcieoffset))
		return;

	bbase = nlm_get_bridge_regbase(node);
	linkpcibase = nlm_pcicfg_base(pcieoffset);
	bar = nlm_read_bridge_reg(bbase, BRIDGE_PCIEMEM_BASE0 + link);
	nlm_write_pci_reg(linkpcibase, PCIE_BYTE_SWAP_MEM_BASE, bar);

	bar = nlm_read_bridge_reg(bbase, BRIDGE_PCIEMEM_LIMIT0 + link);
	nlm_write_pci_reg(linkpcibase, PCIE_BYTE_SWAP_MEM_LIM, bar | 0xFFF);

	bar = nlm_read_bridge_reg(bbase, BRIDGE_PCIEIO_BASE0 + link);
	nlm_write_pci_reg(linkpcibase, PCIE_BYTE_SWAP_IO_BASE, bar);

	bar = nlm_read_bridge_reg(bbase, BRIDGE_PCIEIO_LIMIT0 + link);
	nlm_write_pci_reg(linkpcibase, PCIE_BYTE_SWAP_IO_LIM, bar | 0xFFF);
#endif
}

static int
xlp_pcib_attach(device_t dev)
{
	int node, link;

	/* enable hardware swap on all nodes/links */
	for (node = 0; node < XLP_MAX_NODES; node++)
		for (link = 0; link < 4; link++)
			xlp_pcib_hardware_swap_enable(node, link);

	device_add_child(dev, "pci", -1);
	bus_generic_attach(dev);
	return (0);
}

/*
 * XLS PCIe can have upto 4 links, and each link has its on IRQ
 * Find the link on which the device is on
 */
static int
xlp_pcie_link(device_t pcib, device_t dev)
{
	device_t parent, tmp;

	/* find the lane on which the slot is connected to */
	tmp = dev;
	while (1) {
		parent = device_get_parent(tmp);
		if (parent == NULL || parent == pcib) {
			device_printf(dev, "Cannot find parent bus\n");
			return (-1);
		}
		if (strcmp(device_get_nameunit(parent), "pci0") == 0)
			break;
		tmp = parent;
	}
	return (pci_get_function(tmp));
}

static int
xlp_alloc_msi(device_t pcib, device_t dev, int count, int maxcount, int *irqs)
{
	int i, link;

	/*
	 * Each link has 32 MSIs that can be allocated, but for now
	 * we only support one device per link.
	 * msi_alloc() equivalent is needed when we start supporting
	 * bridges on the PCIe link.
	 */
	link = xlp_pcie_link(pcib, dev);
	if (link == -1)
		return (ENXIO);

	/*
	 * encode the irq so that we know it is a MSI interrupt when we
	 * setup interrupts
	 */
	for (i = 0; i < count; i++)
		irqs[i] = 64 + link * 32 + i;

	return (0);
}

static int
xlp_release_msi(device_t pcib, device_t dev, int count, int *irqs)
{
	return (0);
}

static int
xlp_map_msi(device_t pcib, device_t dev, int irq, uint64_t *addr,
    uint32_t *data)
{
	int link;

	if (irq < 64) {
		device_printf(dev, "%s: map_msi for irq %d  - ignored",
		    device_get_nameunit(pcib), irq);
		return (ENXIO);
	}
	link = (irq - 64) / 32;
	*addr = MIPS_MSI_ADDR(0);
	*data = MIPS_MSI_DATA(PIC_PCIE_IRQ(link));
	return (0);
}

static void
bridge_pcie_ack(int irq, void *arg)
{
	uint32_t node,reg;
	uint64_t base;

	node = nlm_nodeid();
	reg = PCIE_MSI_STATUS;

	switch (irq) {
		case PIC_PCIE_0_IRQ:
			base = nlm_pcicfg_base(XLP_IO_PCIE0_OFFSET(node));
			break;
		case PIC_PCIE_1_IRQ:
			base = nlm_pcicfg_base(XLP_IO_PCIE1_OFFSET(node));
			break;
		case PIC_PCIE_2_IRQ:
			base = nlm_pcicfg_base(XLP_IO_PCIE2_OFFSET(node));
			break;
		case PIC_PCIE_3_IRQ:
			base = nlm_pcicfg_base(XLP_IO_PCIE3_OFFSET(node));
			break;
		default:
			return;
	}

	nlm_write_pci_reg(base, reg, 0xFFFFFFFF);
	return;
}

static int
mips_platform_pcib_setup_intr(device_t dev, device_t child,
    struct resource *irq, int flags, driver_filter_t *filt,
    driver_intr_t *intr, void *arg, void **cookiep)
{
	int error = 0;
	int xlpirq;

	error = rman_activate_resource(irq);
	if (error)
		return error;
	if (rman_get_start(irq) != rman_get_end(irq)) {
		device_printf(dev, "Interrupt allocation %ju != %ju\n",
		    rman_get_start(irq), rman_get_end(irq));
		return (EINVAL);
	}
	xlpirq = rman_get_start(irq);
	if (xlpirq == 0)
		return (0);

	if (strcmp(device_get_name(dev), "pcib") != 0)
		return (0);

	/*
	 * temporary hack for MSI, we support just one device per
	 * link, and assign the link interrupt to the device interrupt
	 */
	if (xlpirq >= 64) {
		int node, val, link;
		uint64_t base;

		xlpirq -= 64;
		if (xlpirq % 32 != 0)
			return (0);

		node = nlm_nodeid();
		link = xlpirq / 32;
		base = nlm_pcicfg_base(XLP_IO_PCIE_OFFSET(node,link));

		/* MSI Interrupt Vector enable at bridge's configuration */
		nlm_write_pci_reg(base, PCIE_MSI_EN, PCIE_MSI_VECTOR_INT_EN);

		val = nlm_read_pci_reg(base, PCIE_INT_EN0);
		/* MSI Interrupt enable at bridge's configuration */
		nlm_write_pci_reg(base, PCIE_INT_EN0,
		    (val | PCIE_MSI_INT_EN));

		/* legacy interrupt disable at bridge */
		val = nlm_read_pci_reg(base, PCIE_BRIDGE_CMD);
		nlm_write_pci_reg(base, PCIE_BRIDGE_CMD,
		    (val | PCIM_CMD_INTxDIS));

		/* MSI address update at bridge */
		nlm_write_pci_reg(base, PCIE_BRIDGE_MSI_ADDRL,
		    MSI_MIPS_ADDR_BASE);
		nlm_write_pci_reg(base, PCIE_BRIDGE_MSI_ADDRH, 0);

		val = nlm_read_pci_reg(base, PCIE_BRIDGE_MSI_CAP);
		/* MSI capability enable at bridge */
		nlm_write_pci_reg(base, PCIE_BRIDGE_MSI_CAP,
		    (val | (PCIM_MSICTRL_MSI_ENABLE << 16) |
		        (PCIM_MSICTRL_MMC_32 << 16)));
		xlpirq = PIC_PCIE_IRQ(link);
	}

	/* if it is for real PCIe, we need to ack at bridge too */
	if (xlpirq >= PIC_PCIE_IRQ(0) && xlpirq <= PIC_PCIE_IRQ(3))
		xlp_set_bus_ack(xlpirq, bridge_pcie_ack, NULL);
	cpu_establish_hardintr(device_get_name(child), filt, intr, arg,
	    xlpirq, flags, cookiep);

	return (0);
}

static int
mips_platform_pcib_teardown_intr(device_t dev, device_t child,
    struct resource *irq, void *cookie)
{
	if (strcmp(device_get_name(child), "pci") == 0) {
		/* if needed reprogram the pic to clear pcix related entry */
		device_printf(dev, "teardown intr\n");
	}
	return (bus_generic_teardown_intr(dev, child, irq, cookie));
}

static int
mips_pcib_route_interrupt(device_t bus, device_t dev, int pin)
{
	int f, d;

	/*
	 * Validate requested pin number.
	 */
	if ((pin < 1) || (pin > 4))
		return (255);

	if (pci_get_bus(dev) == 0 &&
	    pci_get_vendor(dev) == PCI_VENDOR_NETLOGIC) {
		f = pci_get_function(dev);
		d = pci_get_slot(dev) % 8;

		/*
		 * For PCIe links, return link IRT, for other SoC devices
		 * get the IRT from its PCIe header
		 */
		if (d == 1)
			return (PIC_PCIE_IRQ(f));
		else
			return (255);	/* use intline, don't reroute */
	} else {
		/* Regular PCI devices */
		return (PIC_PCIE_IRQ(xlp_pcie_link(bus, dev)));
	}
}

static device_method_t xlp_pcib_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe, xlp_pcib_probe),
	DEVMETHOD(device_attach, xlp_pcib_attach),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar, xlp_pcib_read_ivar),
	DEVMETHOD(bus_write_ivar, xlp_pcib_write_ivar),
	DEVMETHOD(bus_alloc_resource, bus_generic_alloc_resource),
	DEVMETHOD(bus_release_resource, bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr, mips_platform_pcib_setup_intr),
	DEVMETHOD(bus_teardown_intr, mips_platform_pcib_teardown_intr),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots, xlp_pcib_maxslots),
	DEVMETHOD(pcib_read_config, xlp_pcib_read_config),
	DEVMETHOD(pcib_write_config, xlp_pcib_write_config),
	DEVMETHOD(pcib_route_interrupt, mips_pcib_route_interrupt),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

	DEVMETHOD(pcib_alloc_msi, xlp_alloc_msi),
	DEVMETHOD(pcib_release_msi, xlp_release_msi),
	DEVMETHOD(pcib_map_msi, xlp_map_msi),

	DEVMETHOD_END
};

static driver_t xlp_pcib_driver = {
	"pcib",
	xlp_pcib_methods,
	1, /* no softc */
};

static devclass_t pcib_devclass;
DRIVER_MODULE(xlp_pcib, simplebus, xlp_pcib_driver, pcib_devclass, 0, 0);
