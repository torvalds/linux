/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010-2011 Juli Mallett <jmallett@FreeBSD.org>
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
 *
 * $FreeBSD$
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>

#include <sys/bus.h>
#include <sys/endian.h>
#include <sys/interrupt.h>
#include <sys/malloc.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <vm/vm.h>
#include <vm/pmap.h>
#include <vm/vm_extern.h>

#include <machine/bus.h>
#include <machine/cpu.h>

#include <contrib/octeon-sdk/cvmx.h>
#include <mips/cavium/octeon_irq.h>
#include <contrib/octeon-sdk/cvmx-pcie.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/pci/pcib_private.h>

#include <mips/cavium/octopcireg.h>
#include <mips/cavium/octopcivar.h>

#include "pcib_if.h"

#define	NPI_WRITE(addr, value)	cvmx_write64_uint32((addr) ^ 4, (value))
#define	NPI_READ(addr)		cvmx_read64_uint32((addr) ^ 4)

struct octopci_softc {
	device_t sc_dev;

	unsigned sc_domain;
	unsigned sc_bus;

	bus_addr_t sc_io_base;
	unsigned sc_io_next;
	struct rman sc_io;

	bus_addr_t sc_mem1_base;
	unsigned sc_mem1_next;
	struct rman sc_mem1;
};

static void		octopci_identify(driver_t *, device_t);
static int		octopci_probe(device_t);
static int		octopci_attach(device_t);
static int		octopci_read_ivar(device_t, device_t, int,
					  uintptr_t *);
static struct resource	*octopci_alloc_resource(device_t, device_t, int, int *,
						rman_res_t, rman_res_t,
						rman_res_t, u_int);
static int		octopci_activate_resource(device_t, device_t, int, int,
						  struct resource *);
static int	octopci_maxslots(device_t);
static uint32_t	octopci_read_config(device_t, u_int, u_int, u_int, u_int, int);
static void	octopci_write_config(device_t, u_int, u_int, u_int, u_int,
				     uint32_t, int);
static int	octopci_route_interrupt(device_t, device_t, int);

static unsigned	octopci_init_bar(device_t, unsigned, unsigned, unsigned, unsigned, uint8_t *);
static unsigned	octopci_init_device(device_t, unsigned, unsigned, unsigned, unsigned);
static unsigned	octopci_init_bus(device_t, unsigned);
static void	octopci_init_pci(device_t);
static uint64_t	octopci_cs_addr(unsigned, unsigned, unsigned, unsigned);

static void
octopci_identify(driver_t *drv, device_t parent)
{
	BUS_ADD_CHILD(parent, 0, "pcib", 0);
	if (octeon_has_feature(OCTEON_FEATURE_PCIE))
		BUS_ADD_CHILD(parent, 0, "pcib", 1);
}

static int
octopci_probe(device_t dev)
{
	if (octeon_has_feature(OCTEON_FEATURE_PCIE)) {
		device_set_desc(dev, "Cavium Octeon PCIe bridge");
		return (0);
	}

	/* Check whether we are a PCI host.  */
	if ((cvmx_sysinfo_get()->bootloader_config_flags & CVMX_BOOTINFO_CFG_FLAG_PCI_HOST) == 0)
		return (ENXIO);

	if (device_get_unit(dev) != 0)
		return (ENXIO);

	device_set_desc(dev, "Cavium Octeon PCI bridge");
	return (0);
}

static int
octopci_attach(device_t dev)
{
	struct octopci_softc *sc;
	unsigned subbus;
	int error;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;

	if (octeon_has_feature(OCTEON_FEATURE_PCIE)) {
		sc->sc_domain = device_get_unit(dev);

		error = cvmx_pcie_rc_initialize(sc->sc_domain);
		if (error != 0) {
			device_printf(dev, "Failed to put PCIe bus in host mode.\n");
			return (ENXIO);
		}

		/*
		 * In RC mode, the Simple Executive programs the first bus to
		 * be numbered as bus 1, because some IDT bridges used in
		 * Octeon systems object to being attached to bus 0.
		 */
		sc->sc_bus = 1;

		sc->sc_io_base = CVMX_ADD_IO_SEG(cvmx_pcie_get_io_base_address(sc->sc_domain));
		sc->sc_io.rm_descr = "Cavium Octeon PCIe I/O Ports";

		sc->sc_mem1_base = CVMX_ADD_IO_SEG(cvmx_pcie_get_mem_base_address(sc->sc_domain));
		sc->sc_mem1.rm_descr = "Cavium Octeon PCIe Memory";
	} else {
		octopci_init_pci(dev);

		sc->sc_domain = 0;
		sc->sc_bus = 0;

		sc->sc_io_base = CVMX_ADDR_DID(CVMX_FULL_DID(CVMX_OCT_DID_PCI, CVMX_OCT_SUBDID_PCI_IO));
		sc->sc_io.rm_descr = "Cavium Octeon PCI I/O Ports";

		sc->sc_mem1_base = CVMX_ADDR_DID(CVMX_FULL_DID(CVMX_OCT_DID_PCI, CVMX_OCT_SUBDID_PCI_MEM1));
		sc->sc_mem1.rm_descr = "Cavium Octeon PCI Memory";
	}

	sc->sc_io.rm_type = RMAN_ARRAY;
	error = rman_init(&sc->sc_io);
	if (error != 0)
		return (error);

	error = rman_manage_region(&sc->sc_io, CVMX_OCT_PCI_IO_BASE,
	    CVMX_OCT_PCI_IO_BASE + CVMX_OCT_PCI_IO_SIZE);
	if (error != 0)
		return (error);

	sc->sc_mem1.rm_type = RMAN_ARRAY;
	error = rman_init(&sc->sc_mem1);
	if (error != 0)
		return (error);

	error = rman_manage_region(&sc->sc_mem1, CVMX_OCT_PCI_MEM1_BASE,
	    CVMX_OCT_PCI_MEM1_BASE + CVMX_OCT_PCI_MEM1_SIZE);
	if (error != 0)
		return (error);

	/*
	 * Next offsets for resource allocation in octopci_init_bar.
	 */
	sc->sc_io_next = 0;
	sc->sc_mem1_next = 0;

	/*
	 * Configure devices.
	 */
	octopci_write_config(dev, sc->sc_bus, 0, 0, PCIR_SUBBUS_1, 0xff, 1);
	subbus = octopci_init_bus(dev, sc->sc_bus);
	octopci_write_config(dev, sc->sc_bus, 0, 0, PCIR_SUBBUS_1, subbus, 1);

	device_add_child(dev, "pci", -1);

	return (bus_generic_attach(dev));
}

static int
octopci_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{
	struct octopci_softc *sc;
	
	sc = device_get_softc(dev);

	switch (which) {
	case PCIB_IVAR_DOMAIN:
		*result = sc->sc_domain;
		return (0);
	case PCIB_IVAR_BUS:
		*result = sc->sc_bus;
		return (0);
		
	}
	return (ENOENT);
}

static struct resource *
octopci_alloc_resource(device_t bus, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct octopci_softc *sc;
	struct resource *res;
	struct rman *rm;
	int error;

	sc = device_get_softc(bus);

	switch (type) {
	case SYS_RES_IRQ:
		res = bus_generic_alloc_resource(bus, child, type, rid, start,
		    end, count, flags);
		if (res != NULL)
			return (res);
		return (NULL);
	case SYS_RES_MEMORY:
		rm = &sc->sc_mem1;
		break;
	case SYS_RES_IOPORT:
		rm = &sc->sc_io;
		break;
	default:
		return (NULL);
	}

	res = rman_reserve_resource(rm, start, end, count, flags, child);
	if (res == NULL)
		return (NULL);

	rman_set_rid(res, *rid);
	rman_set_bustag(res, octopci_bus_space);

	switch (type) {
	case SYS_RES_MEMORY:
		rman_set_bushandle(res, sc->sc_mem1_base + rman_get_start(res));
		break;
	case SYS_RES_IOPORT:
		rman_set_bushandle(res, sc->sc_io_base + rman_get_start(res));
#if __mips_n64
		rman_set_virtual(res, (void *)rman_get_bushandle(res));
#else
		/*
		 * XXX
		 * We can't access ports via a 32-bit pointer.
		 */
		rman_set_virtual(res, NULL);
#endif
		break;
	}

	if ((flags & RF_ACTIVE) != 0) {
		error = bus_activate_resource(child, type, *rid, res);
		if (error != 0) {
			rman_release_resource(res);
			return (NULL);
		}
	}

	return (res);
}

static int
octopci_activate_resource(device_t bus, device_t child, int type, int rid,
    struct resource *res)
{
	bus_space_handle_t bh;
	int error;

	switch (type) {
	case SYS_RES_IRQ:
		error = bus_generic_activate_resource(bus, child, type, rid,
						      res);
		if (error != 0)
			return (error);
		return (0);
	case SYS_RES_MEMORY:
	case SYS_RES_IOPORT:
		error = bus_space_map(rman_get_bustag(res),
		    rman_get_bushandle(res), rman_get_size(res), 0, &bh);
		if (error != 0)
			return (error);
		rman_set_bushandle(res, bh);
		break;
	default:
		return (ENXIO);
	}

	error = rman_activate_resource(res);
	if (error != 0)
		return (error);
	return (0);
}

static int
octopci_maxslots(device_t dev)
{
	return (PCI_SLOTMAX);
}

static uint32_t
octopci_read_config(device_t dev, u_int bus, u_int slot, u_int func, u_int reg,
    int bytes)
{
	struct octopci_softc *sc;
	uint64_t addr;
	uint32_t data;

	sc = device_get_softc(dev);

	if (octeon_has_feature(OCTEON_FEATURE_PCIE)) {
		if (bus == 0 && slot == 0 && func == 0)
			return ((uint32_t)-1);

		switch (bytes) {
		case 4:
			return (cvmx_pcie_config_read32(sc->sc_domain, bus, slot, func, reg));
		case 2:
			return (cvmx_pcie_config_read16(sc->sc_domain, bus, slot, func, reg));
		case 1:
			return (cvmx_pcie_config_read8(sc->sc_domain, bus, slot, func, reg));
		default:
			return ((uint32_t)-1);
		}
	}

	addr = octopci_cs_addr(bus, slot, func, reg);

	switch (bytes) {
	case 4:
		data = le32toh(cvmx_read64_uint32(addr));
		return (data);
	case 2:
		data = le16toh(cvmx_read64_uint16(addr));
		return (data);
	case 1:
		data = cvmx_read64_uint8(addr);
		return (data);
	default:
		return ((uint32_t)-1);
	}
}

static void
octopci_write_config(device_t dev, u_int bus, u_int slot, u_int func,
    u_int reg, uint32_t data, int bytes)
{
	struct octopci_softc *sc;
	uint64_t addr;

	sc = device_get_softc(dev);

	if (octeon_has_feature(OCTEON_FEATURE_PCIE)) {
		switch (bytes) {
		case 4:
			cvmx_pcie_config_write32(sc->sc_domain, bus, slot, func, reg, data);
			return;
		case 2:
			cvmx_pcie_config_write16(sc->sc_domain, bus, slot, func, reg, data);
			return;
		case 1:
			cvmx_pcie_config_write8(sc->sc_domain, bus, slot, func, reg, data);
			return;
		default:
			return;
		}
	}

	addr = octopci_cs_addr(bus, slot, func, reg);

	switch (bytes) {
	case 4:
		cvmx_write64_uint32(addr, htole32(data));
		return;
	case 2:
		cvmx_write64_uint16(addr, htole16(data));
		return;
	case 1:
		cvmx_write64_uint8(addr, data);
		return;
	default:
		return;
	}
}

static int
octopci_route_interrupt(device_t dev, device_t child, int pin)
{
	struct octopci_softc *sc;
	unsigned bus, slot, func;
	unsigned irq;

	sc = device_get_softc(dev);

	if (octeon_has_feature(OCTEON_FEATURE_PCIE))
		return (OCTEON_IRQ_PCI_INT0 + pin - 1);

        bus = pci_get_bus(child);
        slot = pci_get_slot(child);
        func = pci_get_function(child);

	/*
	 * Board types we have to know at compile-time.
	 */
#if defined(OCTEON_BOARD_CAPK_0100ND)
	if (bus == 0 && slot == 12 && func == 0)
		return (OCTEON_IRQ_PCI_INT2);
#endif

	/*
	 * For board types we can determine at runtime.
	 */
	switch (cvmx_sysinfo_get()->board_type) {
#if defined(OCTEON_VENDOR_LANNER)
	case CVMX_BOARD_TYPE_CUST_LANNER_MR955:
		return (OCTEON_IRQ_PCI_INT0 + pin - 1);
	case CVMX_BOARD_TYPE_CUST_LANNER_MR320:
		if (slot < 32) {
			if (slot == 3 || slot == 9)
				irq = pin;
			else
				irq = pin - 1;
			return (OCTEON_IRQ_PCI_INT0 + (irq & 3));
		}
		break;
#endif
	default:
		break;
	}

	irq = slot + pin - 3;

	return (OCTEON_IRQ_PCI_INT0 + (irq & 3));
}

static unsigned
octopci_init_bar(device_t dev, unsigned b, unsigned s, unsigned f, unsigned barnum, uint8_t *commandp)
{
	struct octopci_softc *sc;
	uint64_t bar;
	unsigned size;
	int barsize;

	sc = device_get_softc(dev);

	octopci_write_config(dev, b, s, f, PCIR_BAR(barnum), 0xffffffff, 4);
	bar = octopci_read_config(dev, b, s, f, PCIR_BAR(barnum), 4);

	if (bar == 0) {
		/* Bar not implemented; got to next bar.  */
		return (barnum + 1);
	}

	if (PCI_BAR_IO(bar)) {
		size = ~(bar & PCIM_BAR_IO_BASE) + 1;

		sc->sc_io_next = roundup2(sc->sc_io_next, size);
		if (sc->sc_io_next + size > CVMX_OCT_PCI_IO_SIZE) {
			device_printf(dev, "%02x.%02x:%02x: no ports for BAR%u.\n",
			    b, s, f, barnum);
			return (barnum + 1);
		}
		octopci_write_config(dev, b, s, f, PCIR_BAR(barnum),
		    CVMX_OCT_PCI_IO_BASE + sc->sc_io_next, 4);
		sc->sc_io_next += size;

		/*
		 * Enable I/O ports.
		 */
		*commandp |= PCIM_CMD_PORTEN;

		return (barnum + 1);
	} else {
		if (PCIR_BAR(barnum) == PCIR_BIOS) {
			/*
			 * ROM BAR is always 32-bit.
			 */
			barsize = 1;
		} else {
			switch (bar & PCIM_BAR_MEM_TYPE) {
			case PCIM_BAR_MEM_64:
				/*
				 * XXX
				 * High 32 bits are all zeroes for now.
				 */
				octopci_write_config(dev, b, s, f, PCIR_BAR(barnum + 1), 0, 4);
				barsize = 2;
				break;
			default:
				barsize = 1;
				break;
			}
		}

		size = ~(bar & (uint32_t)PCIM_BAR_MEM_BASE) + 1;

		sc->sc_mem1_next = roundup2(sc->sc_mem1_next, size);
		if (sc->sc_mem1_next + size > CVMX_OCT_PCI_MEM1_SIZE) {
			device_printf(dev, "%02x.%02x:%02x: no memory for BAR%u.\n",
			    b, s, f, barnum);
			return (barnum + barsize);
		}
		octopci_write_config(dev, b, s, f, PCIR_BAR(barnum),
		    CVMX_OCT_PCI_MEM1_BASE + sc->sc_mem1_next, 4);
		sc->sc_mem1_next += size;

		/*
		 * Enable memory access.
		 */
		*commandp |= PCIM_CMD_MEMEN;

		return (barnum + barsize);
	}
}

static unsigned
octopci_init_device(device_t dev, unsigned b, unsigned s, unsigned f, unsigned secbus)
{
	unsigned barnum, bars;
	uint8_t brctl;
	uint8_t class, subclass;
	uint8_t command;
	uint8_t hdrtype;

	/* Read header type (again.)  */
	hdrtype = octopci_read_config(dev, b, s, f, PCIR_HDRTYPE, 1);

	/*
	 * Disable memory and I/O while programming BARs.
	 */
	command = octopci_read_config(dev, b, s, f, PCIR_COMMAND, 1);
	command &= ~(PCIM_CMD_MEMEN | PCIM_CMD_PORTEN);
	octopci_write_config(dev, b, s, f, PCIR_COMMAND, command, 1);

	DELAY(10000);

	/* Program BARs.  */
	switch (hdrtype & PCIM_HDRTYPE) {
	case PCIM_HDRTYPE_NORMAL:
		bars = 6;
		break;
	case PCIM_HDRTYPE_BRIDGE:
		bars = 2;
		break;
	case PCIM_HDRTYPE_CARDBUS:
		bars = 0;
		break;
	default:
		device_printf(dev, "%02x.%02x:%02x: invalid header type %#x\n",
		    b, s, f, hdrtype);
		return (secbus);
	}

	barnum = 0;
	while (barnum < bars)
		barnum = octopci_init_bar(dev, b, s, f, barnum, &command);

	/* Enable bus mastering.  */
	command |= PCIM_CMD_BUSMASTEREN;

	/* Enable whatever facilities the BARs require.  */
	octopci_write_config(dev, b, s, f, PCIR_COMMAND, command, 1);

	DELAY(10000);

	/* 
	 * Set cache line size.  On Octeon it should be 128 bytes,
	 * but according to Linux some Intel bridges have trouble
	 * with values over 64 bytes, so use 64 bytes.
	 */
	octopci_write_config(dev, b, s, f, PCIR_CACHELNSZ, 16, 1);

	/* Set latency timer.  */
	octopci_write_config(dev, b, s, f, PCIR_LATTIMER, 48, 1);

	/* Board-specific or device-specific fixups and workarounds.  */
	switch (cvmx_sysinfo_get()->board_type) {
#if defined(OCTEON_VENDOR_LANNER)
	case CVMX_BOARD_TYPE_CUST_LANNER_MR955:
		if (b == 1 && s == 7 && f == 0) {
			bus_addr_t busaddr, unitbusaddr;
			uint32_t bar;
			uint32_t tmp;
			unsigned unit;

			/*
			 * Set Tx DMA power.
			 */
			bar = octopci_read_config(dev, b, s, f,
			    PCIR_BAR(3), 4);
			busaddr = CVMX_ADDR_DID(CVMX_FULL_DID(CVMX_OCT_DID_PCI,
			    CVMX_OCT_SUBDID_PCI_MEM1));
			busaddr += (bar & (uint32_t)PCIM_BAR_MEM_BASE);
			for (unit = 0; unit < 4; unit++) {
				unitbusaddr = busaddr + 0x430 + (unit << 8);
				tmp = le32toh(cvmx_read64_uint32(unitbusaddr));
				tmp &= ~0x700;
				tmp |= 0x300;
				cvmx_write64_uint32(unitbusaddr, htole32(tmp));
			}
		}
		break;
#endif
	default:
		break;
	}

	/* Configure PCI-PCI bridges.  */
	class = octopci_read_config(dev, b, s, f, PCIR_CLASS, 1);
	if (class != PCIC_BRIDGE)
		return (secbus);

	subclass = octopci_read_config(dev, b, s, f, PCIR_SUBCLASS, 1);
	if (subclass != PCIS_BRIDGE_PCI)
		return (secbus);

	/* Enable memory and I/O access.  */
	command |= PCIM_CMD_MEMEN | PCIM_CMD_PORTEN;
	octopci_write_config(dev, b, s, f, PCIR_COMMAND, command, 1);

	/* Enable errors and parity checking.  Do a bus reset.  */
	brctl = octopci_read_config(dev, b, s, f, PCIR_BRIDGECTL_1, 1);
	brctl |= PCIB_BCR_PERR_ENABLE | PCIB_BCR_SERR_ENABLE;

	/* Perform a secondary bus reset.  */
	brctl |= PCIB_BCR_SECBUS_RESET;
	octopci_write_config(dev, b, s, f, PCIR_BRIDGECTL_1, brctl, 1);
	DELAY(100000);
	brctl &= ~PCIB_BCR_SECBUS_RESET;
	octopci_write_config(dev, b, s, f, PCIR_BRIDGECTL_1, brctl, 1);

	secbus++;

	/* Program memory and I/O ranges.  */
	octopci_write_config(dev, b, s, f, PCIR_MEMBASE_1,
	    CVMX_OCT_PCI_MEM1_BASE >> 16, 2);
	octopci_write_config(dev, b, s, f, PCIR_MEMLIMIT_1,
	    (CVMX_OCT_PCI_MEM1_BASE + CVMX_OCT_PCI_MEM1_SIZE - 1) >> 16, 2);

	octopci_write_config(dev, b, s, f, PCIR_IOBASEL_1,
	    CVMX_OCT_PCI_IO_BASE >> 8, 1);
	octopci_write_config(dev, b, s, f, PCIR_IOBASEH_1,
	    CVMX_OCT_PCI_IO_BASE >> 16, 2);

	octopci_write_config(dev, b, s, f, PCIR_IOLIMITL_1,
	    (CVMX_OCT_PCI_IO_BASE + CVMX_OCT_PCI_IO_SIZE - 1) >> 8, 1);
	octopci_write_config(dev, b, s, f, PCIR_IOLIMITH_1,
	    (CVMX_OCT_PCI_IO_BASE + CVMX_OCT_PCI_IO_SIZE - 1) >> 16, 2);

	/* Program prefetchable memory decoder.  */
	/* XXX */

	/* Probe secondary/subordinate buses.  */
	octopci_write_config(dev, b, s, f, PCIR_PRIBUS_1, b, 1);
	octopci_write_config(dev, b, s, f, PCIR_SECBUS_1, secbus, 1);
	octopci_write_config(dev, b, s, f, PCIR_SUBBUS_1, 0xff, 1);

	/* Perform a secondary bus reset.  */
	brctl |= PCIB_BCR_SECBUS_RESET;
	octopci_write_config(dev, b, s, f, PCIR_BRIDGECTL_1, brctl, 1);
	DELAY(100000);
	brctl &= ~PCIB_BCR_SECBUS_RESET;
	octopci_write_config(dev, b, s, f, PCIR_BRIDGECTL_1, brctl, 1);

	/* Give the bus time to settle now before reading configspace.  */
	DELAY(100000);

	secbus = octopci_init_bus(dev, secbus);

	octopci_write_config(dev, b, s, f, PCIR_SUBBUS_1, secbus, 1);

	return (secbus);
}

static unsigned
octopci_init_bus(device_t dev, unsigned b)
{
	unsigned s, f;
	uint8_t hdrtype;
	unsigned secbus;

	secbus = b;

	for (s = 0; s <= PCI_SLOTMAX; s++) {
		for (f = 0; f <= PCI_FUNCMAX; f++) {
			hdrtype = octopci_read_config(dev, b, s, f, PCIR_HDRTYPE, 1);

			if (hdrtype == 0xff) {
				if (f == 0)
					break; /* Next slot.  */
				continue; /* Next function.  */
			}

			secbus = octopci_init_device(dev, b, s, f, secbus);

			if (f == 0 && (hdrtype & PCIM_MFDEV) == 0)
				break; /* Next slot.  */
		}
	}

	return (secbus);
}

static uint64_t
octopci_cs_addr(unsigned bus, unsigned slot, unsigned func, unsigned reg)
{
	octeon_pci_config_space_address_t pci_addr;

	pci_addr.u64 = 0;
	pci_addr.s.upper = 2;
	pci_addr.s.io = 1;
	pci_addr.s.did = 3;
	pci_addr.s.subdid = CVMX_OCT_SUBDID_PCI_CFG;
	pci_addr.s.endian_swap = 1;
	pci_addr.s.bus = bus;
	pci_addr.s.dev = slot;
	pci_addr.s.func = func;
	pci_addr.s.reg = reg;

	return (pci_addr.u64);
}

static void
octopci_init_pci(device_t dev)
{
	cvmx_npi_mem_access_subid_t npi_mem_access_subid;
	cvmx_npi_pci_int_arb_cfg_t npi_pci_int_arb_cfg;
	cvmx_npi_ctl_status_t npi_ctl_status;
	cvmx_pci_ctl_status_2_t pci_ctl_status_2;
	cvmx_pci_cfg56_t pci_cfg56;
	cvmx_pci_cfg22_t pci_cfg22;
	cvmx_pci_cfg16_t pci_cfg16;
	cvmx_pci_cfg19_t pci_cfg19;
	cvmx_pci_cfg01_t pci_cfg01;
	unsigned i;

	/*
	 * Reset the PCI bus.
	 */
	cvmx_write_csr(CVMX_CIU_SOFT_PRST, 0x1);
	cvmx_read_csr(CVMX_CIU_SOFT_PRST);

	DELAY(2000);

	npi_ctl_status.u64 = 0;
	npi_ctl_status.s.max_word = 1;
	npi_ctl_status.s.timer = 1;
	cvmx_write_csr(CVMX_NPI_CTL_STATUS, npi_ctl_status.u64);

	/*
	 * Set host mode.
	 */
	switch (cvmx_sysinfo_get()->board_type) {
#if defined(OCTEON_VENDOR_LANNER)
	case CVMX_BOARD_TYPE_CUST_LANNER_MR320:
	case CVMX_BOARD_TYPE_CUST_LANNER_MR955:
		/* 32-bit PCI-X */
		cvmx_write_csr(CVMX_CIU_SOFT_PRST, 0x0);
		break;
#endif
	default:
		/* 64-bit PCI-X */
		cvmx_write_csr(CVMX_CIU_SOFT_PRST, 0x4);
		break;
	}
	cvmx_read_csr(CVMX_CIU_SOFT_PRST);

	DELAY(2000);

	/*
	 * Enable BARs and configure big BAR mode.
	 */
	pci_ctl_status_2.u32 = 0;
	pci_ctl_status_2.s.bb1_hole = 5; /* 256MB hole in BAR1 */
	pci_ctl_status_2.s.bb1_siz = 1; /* BAR1 is 2GB */
	pci_ctl_status_2.s.bb_ca = 1; /* Bypass cache for big BAR */
	pci_ctl_status_2.s.bb_es = 1; /* Do big BAR byte-swapping */
	pci_ctl_status_2.s.bb1 = 1; /* BAR1 is big */
	pci_ctl_status_2.s.bb0 = 1; /* BAR0 is big */
	pci_ctl_status_2.s.bar2pres = 1; /* BAR2 present */
	pci_ctl_status_2.s.pmo_amod = 1; /* Round-robin priority */
	pci_ctl_status_2.s.tsr_hwm = 1;
	pci_ctl_status_2.s.bar2_enb = 1; /* Enable BAR2 */
	pci_ctl_status_2.s.bar2_esx = 1; /* Do BAR2 byte-swapping */
	pci_ctl_status_2.s.bar2_cax = 1; /* Bypass cache for BAR2 */

	NPI_WRITE(CVMX_NPI_PCI_CTL_STATUS_2, pci_ctl_status_2.u32);

	DELAY(2000);

	pci_ctl_status_2.u32 = NPI_READ(CVMX_NPI_PCI_CTL_STATUS_2);

	device_printf(dev, "%u-bit PCI%s bus.\n",
	    pci_ctl_status_2.s.ap_64ad ? 64 : 32,
	    pci_ctl_status_2.s.ap_pcix ? "-X" : "");

	/*
	 * Set up transaction splitting, etc., parameters.
	 */
	pci_cfg19.u32 = 0;
	pci_cfg19.s.mrbcm = 1;
	if (pci_ctl_status_2.s.ap_pcix) {
		pci_cfg19.s.mdrrmc = 0;
		pci_cfg19.s.tdomc = 4;
	} else {
		pci_cfg19.s.mdrrmc = 2;
		pci_cfg19.s.tdomc = 1;
	}
	NPI_WRITE(CVMX_NPI_PCI_CFG19, pci_cfg19.u32);
	NPI_READ(CVMX_NPI_PCI_CFG19);

	/*
	 * Set up PCI error handling and memory access.
	 */
	pci_cfg01.u32 = 0;
	pci_cfg01.s.fbbe = 1;
	pci_cfg01.s.see = 1;
	pci_cfg01.s.pee = 1;
	pci_cfg01.s.me = 1;
	pci_cfg01.s.msae = 1;
	if (pci_ctl_status_2.s.ap_pcix) {
		pci_cfg01.s.fbb = 0;
	} else {
		pci_cfg01.s.fbb = 1;
	}
	NPI_WRITE(CVMX_NPI_PCI_CFG01, pci_cfg01.u32);
	NPI_READ(CVMX_NPI_PCI_CFG01);

	/*
	 * Enable the Octeon bus arbiter.
	 */
	npi_pci_int_arb_cfg.u64 = 0;
	npi_pci_int_arb_cfg.s.en = 1;
	cvmx_write_csr(CVMX_NPI_PCI_INT_ARB_CFG, npi_pci_int_arb_cfg.u64);

	/*
	 * Disable master latency timer.
	 */
	pci_cfg16.u32 = 0;
	pci_cfg16.s.mltd = 1;
	NPI_WRITE(CVMX_NPI_PCI_CFG16, pci_cfg16.u32);
	NPI_READ(CVMX_NPI_PCI_CFG16);

	/*
	 * Configure master arbiter.
	 */
	pci_cfg22.u32 = 0;
	pci_cfg22.s.flush = 1;
	pci_cfg22.s.mrv = 255;
	NPI_WRITE(CVMX_NPI_PCI_CFG22, pci_cfg22.u32);
	NPI_READ(CVMX_NPI_PCI_CFG22);

	/*
	 * Set up PCI-X capabilities.
	 */
	if (pci_ctl_status_2.s.ap_pcix) {
		pci_cfg56.u32 = 0;
		pci_cfg56.s.most = 3;
		pci_cfg56.s.roe = 1; /* Enable relaxed ordering */
		pci_cfg56.s.dpere = 1;
		pci_cfg56.s.ncp = 0xe8;
		pci_cfg56.s.pxcid = 7;
		NPI_WRITE(CVMX_NPI_PCI_CFG56, pci_cfg56.u32);
		NPI_READ(CVMX_NPI_PCI_CFG56);
	}

	NPI_WRITE(CVMX_NPI_PCI_READ_CMD_6, 0x22);
	NPI_READ(CVMX_NPI_PCI_READ_CMD_6);
	NPI_WRITE(CVMX_NPI_PCI_READ_CMD_C, 0x33);
	NPI_READ(CVMX_NPI_PCI_READ_CMD_C);
	NPI_WRITE(CVMX_NPI_PCI_READ_CMD_E, 0x33);
	NPI_READ(CVMX_NPI_PCI_READ_CMD_E);

	/*
	 * Configure MEM1 sub-DID access.
	 */
	npi_mem_access_subid.u64 = 0;
	npi_mem_access_subid.s.esr = 1; /* Byte-swap on read */
	npi_mem_access_subid.s.esw = 1; /* Byte-swap on write */
	switch (cvmx_sysinfo_get()->board_type) {
#if defined(OCTEON_VENDOR_LANNER)
	case CVMX_BOARD_TYPE_CUST_LANNER_MR955:
		npi_mem_access_subid.s.shortl = 1;
		break;
#endif
	default:
		break;
	}
	cvmx_write_csr(CVMX_NPI_MEM_ACCESS_SUBID3, npi_mem_access_subid.u64);

	/*
	 * Configure BAR2.  Linux says this has to come first.
	 */
	NPI_WRITE(CVMX_NPI_PCI_CFG08, 0x00000000);
	NPI_READ(CVMX_NPI_PCI_CFG08);
	NPI_WRITE(CVMX_NPI_PCI_CFG09, 0x00000080);
	NPI_READ(CVMX_NPI_PCI_CFG09);

	/*
	 * Disable BAR1 IndexX.
	 */
	for (i = 0; i < 32; i++) {
		NPI_WRITE(CVMX_NPI_PCI_BAR1_INDEXX(i), 0);
		NPI_READ(CVMX_NPI_PCI_BAR1_INDEXX(i));
	}

	/*
	 * Configure BAR0 and BAR1.
	 */
	NPI_WRITE(CVMX_NPI_PCI_CFG04, 0x00000000);
	NPI_READ(CVMX_NPI_PCI_CFG04);
	NPI_WRITE(CVMX_NPI_PCI_CFG05, 0x00000000);
	NPI_READ(CVMX_NPI_PCI_CFG05);

	NPI_WRITE(CVMX_NPI_PCI_CFG06, 0x80000000);
	NPI_READ(CVMX_NPI_PCI_CFG06);
	NPI_WRITE(CVMX_NPI_PCI_CFG07, 0x00000000);
	NPI_READ(CVMX_NPI_PCI_CFG07);

	/*
	 * Clear PCI interrupts.
	 */
	cvmx_write_csr(CVMX_NPI_PCI_INT_SUM2, 0xffffffffffffffffull);
}

static device_method_t octopci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_identify,	octopci_identify),
	DEVMETHOD(device_probe,		octopci_probe),
	DEVMETHOD(device_attach,	octopci_attach),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	octopci_read_ivar),
	DEVMETHOD(bus_alloc_resource,	octopci_alloc_resource),
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
	DEVMETHOD(bus_activate_resource,octopci_activate_resource),
	DEVMETHOD(bus_deactivate_resource,bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	DEVMETHOD(bus_add_child,	bus_generic_add_child),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,	octopci_maxslots),
	DEVMETHOD(pcib_read_config,	octopci_read_config),
	DEVMETHOD(pcib_write_config,	octopci_write_config),
	DEVMETHOD(pcib_route_interrupt,	octopci_route_interrupt),
	DEVMETHOD(pcib_request_feature,	pcib_request_feature_allow),

	DEVMETHOD_END
};

static driver_t octopci_driver = {
	"pcib",
	octopci_methods,
	sizeof(struct octopci_softc),
};
static devclass_t octopci_devclass;
DRIVER_MODULE(octopci, ciu, octopci_driver, octopci_devclass, 0, 0);
