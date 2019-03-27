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

/* Common PCIe functions for Cavium Thunder SOC */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/bus.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#ifdef FDT
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/ofw_pci.h>
#endif

#include <sys/pciio.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pci_private.h>
#include <dev/pci/pcib_private.h>
#include <dev/pci/pci_host_generic.h>
#ifdef FDT
#include <dev/pci/pci_host_generic_fdt.h>
#endif

#include "thunder_pcie_common.h"

MALLOC_DEFINE(M_THUNDER_PCIE, "Thunder PCIe driver", "Thunder PCIe driver memory");

#define	THUNDER_CFG_BASE_TO_ECAM(x)	((((x) >> 36UL) & 0x3) | (((x) >> 42UL) & 0x4))

uint32_t
range_addr_is_pci(struct pcie_range *ranges, uint64_t addr, uint64_t size)
{
	struct pcie_range *r;
	int tuple;

	for (tuple = 0; tuple < MAX_RANGES_TUPLES; tuple++) {
		r = &ranges[tuple];
		if (addr >= r->pci_base &&
		    addr < (r->pci_base + r->size) &&
		    size < r->size) {
			/* Address is within PCI range */
			return (1);
		}
	}

	/* Address is outside PCI range */
	return (0);
}

uint32_t
range_addr_is_phys(struct pcie_range *ranges, uint64_t addr, uint64_t size)
{
	struct pcie_range *r;
	int tuple;

	for (tuple = 0; tuple < MAX_RANGES_TUPLES; tuple++) {
		r = &ranges[tuple];
		if (addr >= r->phys_base &&
		    addr < (r->phys_base + r->size) &&
		    size < r->size) {
			/* Address is within Physical range */
			return (1);
		}
	}

	/* Address is outside Physical range */
	return (0);
}

uint64_t
range_addr_phys_to_pci(struct pcie_range *ranges, uint64_t phys_addr)
{
	struct pcie_range *r;
	uint64_t offset;
	int tuple;

	/* Find physical address corresponding to given bus address */
	for (tuple = 0; tuple < MAX_RANGES_TUPLES; tuple++) {
		r = &ranges[tuple];
		if (phys_addr >= r->phys_base &&
		    phys_addr < (r->phys_base + r->size)) {
			/* Given phys addr is in this range.
			 * Translate phys addr to bus addr.
			 */
			offset = phys_addr - r->phys_base;
			return (r->pci_base + offset);
		}
	}
	return (0);
}

uint64_t
range_addr_pci_to_phys(struct pcie_range *ranges, uint64_t pci_addr)
{
	struct pcie_range *r;
	uint64_t offset;
	int tuple;

	/* Find physical address corresponding to given bus address */
	for (tuple = 0; tuple < MAX_RANGES_TUPLES; tuple++) {
		r = &ranges[tuple];
		if (pci_addr >= r->pci_base &&
		    pci_addr < (r->pci_base + r->size)) {
			/* Given pci addr is in this range.
			 * Translate bus addr to phys addr.
			 */
			offset = pci_addr - r->pci_base;
			return (r->phys_base + offset);
		}
	}
	return (0);
}

int
thunder_pcie_identify_ecam(device_t dev, int *ecam)
{
	rman_res_t start;

	/* Check if we're running on Cavium ThunderX */
	if (!CPU_MATCH(CPU_IMPL_MASK | CPU_PART_MASK,
	    CPU_IMPL_CAVIUM, CPU_PART_THUNDERX, 0, 0))
		return (EINVAL);

	start = bus_get_resource_start(dev, SYS_RES_MEMORY, 0);
	*ecam = THUNDER_CFG_BASE_TO_ECAM(start);

	device_printf(dev, "ThunderX quirk, setting ECAM to %d\n", *ecam);

	return (0);
}

#ifdef THUNDERX_PASS_1_1_ERRATA
struct resource *
thunder_pcie_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	pci_addr_t map, testval;

	/*
	 * If Enhanced Allocation is not used, we can't allocate any random
	 * range. All internal devices have hardcoded place where they can
	 * be located within PCI address space. Fortunately, we can read
	 * this value from BAR.
	 */
	if (((type == SYS_RES_IOPORT) || (type == SYS_RES_MEMORY)) &&
	    RMAN_IS_DEFAULT_RANGE(start, end)) {

		/* Read BAR manually to get resource address and size */
		pci_read_bar(child, *rid, &map, &testval, NULL);

		/* Mask the information bits */
		if (PCI_BAR_MEM(map))
			map &= PCIM_BAR_MEM_BASE;
		else
			map &= PCIM_BAR_IO_BASE;

		if (PCI_BAR_MEM(testval))
			testval &= PCIM_BAR_MEM_BASE;
		else
			testval &= PCIM_BAR_IO_BASE;

		start = map;
		end = start + count - 1;
	}

	return (pci_host_generic_core_alloc_resource(dev, child, type, rid,
	    start, end, count, flags));
}
#endif
