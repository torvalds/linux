/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
 * Copyright (c) 2000, Michael Smith <msmith@freebsd.org>
 * Copyright (c) 2000, BSDi
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/kernel.h>
#include <sys/mutex.h>
#include <sys/sysctl.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>
#include <vm/vm.h>
#include <vm/pmap.h>
#include <machine/pci_cfgreg.h>

enum {
	CFGMECH_NONE = 0,
	CFGMECH_1,
	CFGMECH_PCIE,
};

static uint32_t	pci_docfgregread(int bus, int slot, int func, int reg,
		    int bytes);
static int	pciereg_cfgread(int bus, unsigned slot, unsigned func,
		    unsigned reg, unsigned bytes);
static void	pciereg_cfgwrite(int bus, unsigned slot, unsigned func,
		    unsigned reg, int data, unsigned bytes);
static int	pcireg_cfgread(int bus, int slot, int func, int reg, int bytes);
static void	pcireg_cfgwrite(int bus, int slot, int func, int reg, int data, int bytes);

SYSCTL_DECL(_hw_pci);

static int cfgmech;
static vm_offset_t pcie_base;
static int pcie_minbus, pcie_maxbus;
static uint32_t pcie_badslots;
static struct mtx pcicfg_mtx;
MTX_SYSINIT(pcicfg_mtx, &pcicfg_mtx, "pcicfg_mtx", MTX_SPIN);
static int mcfg_enable = 1;
SYSCTL_INT(_hw_pci, OID_AUTO, mcfg, CTLFLAG_RDTUN, &mcfg_enable, 0,
    "Enable support for PCI-e memory mapped config access");

/* 
 * Initialise access to PCI configuration space 
 */
int
pci_cfgregopen(void)
{
	uint64_t pciebar;
	uint16_t did, vid;

	if (cfgmech != CFGMECH_NONE)
		return (1);
	cfgmech = CFGMECH_1;

	/*
	 * Grope around in the PCI config space to see if this is a
	 * chipset that is capable of doing memory-mapped config cycles.
	 * This also implies that it can do PCIe extended config cycles.
	 */

	/* Check for supported chipsets */
	vid = pci_cfgregread(0, 0, 0, PCIR_VENDOR, 2);
	did = pci_cfgregread(0, 0, 0, PCIR_DEVICE, 2);
	switch (vid) {
	case 0x8086:
		switch (did) {
		case 0x3590:
		case 0x3592:
			/* Intel 7520 or 7320 */
			pciebar = pci_cfgregread(0, 0, 0, 0xce, 2) << 16;
			pcie_cfgregopen(pciebar, 0, 255);
			break;
		case 0x2580:
		case 0x2584:
		case 0x2590:
			/* Intel 915, 925, or 915GM */
			pciebar = pci_cfgregread(0, 0, 0, 0x48, 4);
			pcie_cfgregopen(pciebar, 0, 255);
			break;
		}
	}

	return (1);
}

static uint32_t
pci_docfgregread(int bus, int slot, int func, int reg, int bytes)
{

	if (cfgmech == CFGMECH_PCIE &&
	    (bus >= pcie_minbus && bus <= pcie_maxbus) &&
	    (bus != 0 || !(1 << slot & pcie_badslots)))
		return (pciereg_cfgread(bus, slot, func, reg, bytes));
	else
		return (pcireg_cfgread(bus, slot, func, reg, bytes));
}

/* 
 * Read configuration space register
 */
u_int32_t
pci_cfgregread(int bus, int slot, int func, int reg, int bytes)
{
	uint32_t line;

	if (cfgmech == CFGMECH_NONE)
		return (0xffffffff);

	/*
	 * Some BIOS writers seem to want to ignore the spec and put
	 * 0 in the intline rather than 255 to indicate none.  Some use
	 * numbers in the range 128-254 to indicate something strange and
	 * apparently undocumented anywhere.  Assume these are completely bogus
	 * and map them to 255, which the rest of the PCI code recognizes as
	 * as an invalid IRQ.
	 */
	if (reg == PCIR_INTLINE && bytes == 1) {
		line = pci_docfgregread(bus, slot, func, PCIR_INTLINE, 1);
		if (line == 0 || line >= 128)
			line = PCI_INVALID_IRQ;
		return (line);
	}
	return (pci_docfgregread(bus, slot, func, reg, bytes));
}

/* 
 * Write configuration space register 
 */
void
pci_cfgregwrite(int bus, int slot, int func, int reg, u_int32_t data, int bytes)
{

	if (cfgmech == CFGMECH_NONE)
		return;

	if (cfgmech == CFGMECH_PCIE &&
	    (bus >= pcie_minbus && bus <= pcie_maxbus) &&
	    (bus != 0 || !(1 << slot & pcie_badslots)))
		pciereg_cfgwrite(bus, slot, func, reg, data, bytes);
	else
		pcireg_cfgwrite(bus, slot, func, reg, data, bytes);
}

/* 
 * Configuration space access using direct register operations
 */

/* enable configuration space accesses and return data port address */
static int
pci_cfgenable(unsigned bus, unsigned slot, unsigned func, int reg, int bytes)
{
	int dataport = 0;

	if (bus <= PCI_BUSMAX && slot <= PCI_SLOTMAX && func <= PCI_FUNCMAX &&
	    (unsigned)reg <= PCI_REGMAX && bytes != 3 &&
	    (unsigned)bytes <= 4 && (reg & (bytes - 1)) == 0) {
		outl(CONF1_ADDR_PORT, (1U << 31) | (bus << 16) | (slot << 11) 
		    | (func << 8) | (reg & ~0x03));
		dataport = CONF1_DATA_PORT + (reg & 0x03);
	}
	return (dataport);
}

/* disable configuration space accesses */
static void
pci_cfgdisable(void)
{

	/*
	 * Do nothing.  Writing a 0 to the address port can apparently
	 * confuse some bridges and cause spurious access failures.
	 */
}

static int
pcireg_cfgread(int bus, int slot, int func, int reg, int bytes)
{
	int data = -1;
	int port;

	mtx_lock_spin(&pcicfg_mtx);
	port = pci_cfgenable(bus, slot, func, reg, bytes);
	if (port != 0) {
		switch (bytes) {
		case 1:
			data = inb(port);
			break;
		case 2:
			data = inw(port);
			break;
		case 4:
			data = inl(port);
			break;
		}
		pci_cfgdisable();
	}
	mtx_unlock_spin(&pcicfg_mtx);
	return (data);
}

static void
pcireg_cfgwrite(int bus, int slot, int func, int reg, int data, int bytes)
{
	int port;

	mtx_lock_spin(&pcicfg_mtx);
	port = pci_cfgenable(bus, slot, func, reg, bytes);
	if (port != 0) {
		switch (bytes) {
		case 1:
			outb(port, data);
			break;
		case 2:
			outw(port, data);
			break;
		case 4:
			outl(port, data);
			break;
		}
		pci_cfgdisable();
	}
	mtx_unlock_spin(&pcicfg_mtx);
}

int
pcie_cfgregopen(uint64_t base, uint8_t minbus, uint8_t maxbus)
{
	uint32_t val1, val2;
	int slot;

	if (!mcfg_enable)
		return (0);

	if (minbus != 0)
		return (0);

	if (bootverbose)
		printf("PCIe: Memory Mapped configuration base @ 0x%lx\n",
		    base);

	/* XXX: We should make sure this really fits into the direct map. */
	pcie_base = (vm_offset_t)pmap_mapdev_pciecfg(base, (maxbus + 1) << 20);
	pcie_minbus = minbus;
	pcie_maxbus = maxbus;
	cfgmech = CFGMECH_PCIE;

	/*
	 * On some AMD systems, some of the devices on bus 0 are
	 * inaccessible using memory-mapped PCI config access.  Walk
	 * bus 0 looking for such devices.  For these devices, we will
	 * fall back to using type 1 config access instead.
	 */
	if (pci_cfgregopen() != 0) {
		for (slot = 0; slot <= PCI_SLOTMAX; slot++) {
			val1 = pcireg_cfgread(0, slot, 0, 0, 4);
			if (val1 == 0xffffffff)
				continue;

			val2 = pciereg_cfgread(0, slot, 0, 0, 4);
			if (val2 != val1)
				pcie_badslots |= (1 << slot);
		}
	}

	return (1);
}

#define PCIE_VADDR(base, reg, bus, slot, func)	\
	((base)				+	\
	((((bus) & 0xff) << 20)		|	\
	(((slot) & 0x1f) << 15)		|	\
	(((func) & 0x7) << 12)		|	\
	((reg) & 0xfff)))

/*
 * AMD BIOS And Kernel Developer's Guides for CPU families starting with 10h
 * have a requirement that all accesses to the memory mapped PCI configuration
 * space are done using AX class of registers.
 * Since other vendors do not currently have any contradicting requirements
 * the AMD access pattern is applied universally.
 */

static int
pciereg_cfgread(int bus, unsigned slot, unsigned func, unsigned reg,
    unsigned bytes)
{
	vm_offset_t va;
	int data = -1;

	if (bus < pcie_minbus || bus > pcie_maxbus || slot > PCI_SLOTMAX ||
	    func > PCI_FUNCMAX || reg > PCIE_REGMAX)
		return (-1);

	va = PCIE_VADDR(pcie_base, reg, bus, slot, func);

	switch (bytes) {
	case 4:
		__asm("movl %1, %0" : "=a" (data)
		    : "m" (*(volatile uint32_t *)va));
		break;
	case 2:
		__asm("movzwl %1, %0" : "=a" (data)
		    : "m" (*(volatile uint16_t *)va));
		break;
	case 1:
		__asm("movzbl %1, %0" : "=a" (data)
		    : "m" (*(volatile uint8_t *)va));
		break;
	}

	return (data);
}

static void
pciereg_cfgwrite(int bus, unsigned slot, unsigned func, unsigned reg, int data,
    unsigned bytes)
{
	vm_offset_t va;

	if (bus < pcie_minbus || bus > pcie_maxbus || slot > PCI_SLOTMAX ||
	    func > PCI_FUNCMAX || reg > PCIE_REGMAX)
		return;

	va = PCIE_VADDR(pcie_base, reg, bus, slot, func);

	switch (bytes) {
	case 4:
		__asm("movl %1, %0" : "=m" (*(volatile uint32_t *)va)
		    : "a" (data));
		break;
	case 2:
		__asm("movw %1, %0" : "=m" (*(volatile uint16_t *)va)
		    : "a" ((uint16_t)data));
		break;
	case 1:
		__asm("movb %1, %0" : "=m" (*(volatile uint8_t *)va)
		    : "a" ((uint8_t)data));
		break;
	}
}
