/*-
 * Copyright (c) 2015 Ian Lepore <ian@freebsd.org>
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
 * The initial ofw_reg_to_paddr() implementation has been copied from powerpc
 * ofw_machdep.c OF_decode_addr(). It was added by Marcel Moolenaar, who did not
 * assert copyright with the addition but still deserves credit for the work.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/boot.h>
#include <sys/bus.h>
#include <sys/libkern.h>
#include <sys/reboot.h>
#include <sys/rman.h>

#include <machine/bus.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/ofw_subr.h>

static void
get_addr_props(phandle_t node, uint32_t *addrp, uint32_t *sizep, int *pcip)
{
	char type[64];
	uint32_t addr, size;
	int pci, res;

	res = OF_getencprop(node, "#address-cells", &addr, sizeof(addr));
	if (res == -1)
		addr = 2;
	res = OF_getencprop(node, "#size-cells", &size, sizeof(size));
	if (res == -1)
		size = 1;
	pci = 0;
	if (addr == 3 && size == 2) {
		res = OF_getprop(node, "device_type", type, sizeof(type));
		if (res != -1) {
			type[sizeof(type) - 1] = '\0';
			if (strcmp(type, "pci") == 0 ||
			    strcmp(type, "pciex")== 0)
				pci = 1;
		}
	}
	if (addrp != NULL)
		*addrp = addr;
	if (sizep != NULL)
		*sizep = size;
	if (pcip != NULL)
		*pcip = pci;
}

int
ofw_reg_to_paddr(phandle_t dev, int regno, bus_addr_t *paddr,
    bus_size_t *psize, pcell_t *ppci_hi)
{
	pcell_t cell[32], pci_hi;
	uint64_t addr, raddr, baddr;
	uint64_t size, rsize;
	uint32_t c, nbridge, naddr, nsize;
	phandle_t bridge, parent;
	u_int spc, rspc;
	int pci, pcib, res;

	/* Sanity checking. */
	if (dev == 0)
		return (EINVAL);
	bridge = OF_parent(dev);
	if (bridge == 0)
		return (EINVAL);
	if (regno < 0)
		return (EINVAL);
	if (paddr == NULL || psize == NULL)
		return (EINVAL);

	get_addr_props(bridge, &naddr, &nsize, &pci);
	res = OF_getencprop(dev, (pci) ? "assigned-addresses" : "reg",
	    cell, sizeof(cell));
	if (res == -1)
		return (ENXIO);
	if (res % sizeof(cell[0]))
		return (ENXIO);
	res /= sizeof(cell[0]);
	regno *= naddr + nsize;
	if (regno + naddr + nsize > res)
		return (EINVAL);
	pci_hi = pci ? cell[regno] : OFW_PADDR_NOT_PCI;
	spc = pci_hi & OFW_PCI_PHYS_HI_SPACEMASK;
	addr = 0;
	for (c = 0; c < naddr; c++)
		addr = ((uint64_t)addr << 32) | cell[regno++];
	size = 0;
	for (c = 0; c < nsize; c++)
		size = ((uint64_t)size << 32) | cell[regno++];
	/*
	 * Map the address range in the bridge's decoding window as given
	 * by the "ranges" property. If a node doesn't have such property
	 * or the property is empty, we assume an identity mapping.  The
	 * standard says a missing property indicates no possible mapping.
	 * This code is more liberal since the intended use is to get a
	 * console running early, and a printf to warn of malformed data
	 * is probably futile before the console is fully set up.
	 */
	parent = OF_parent(bridge);
	while (parent != 0) {
		get_addr_props(parent, &nbridge, NULL, &pcib);
		res = OF_getencprop(bridge, "ranges", cell, sizeof(cell));
		if (res < 1)
			goto next;
		if (res % sizeof(cell[0]))
			return (ENXIO);
		/* Capture pci_hi if we just transitioned onto a PCI bus. */
		if (pcib && pci_hi == OFW_PADDR_NOT_PCI) {
			pci_hi = cell[0];
			spc = pci_hi & OFW_PCI_PHYS_HI_SPACEMASK;
		}
		res /= sizeof(cell[0]);
		regno = 0;
		while (regno < res) {
			rspc = (pci ? cell[regno] : OFW_PADDR_NOT_PCI) &
			    OFW_PCI_PHYS_HI_SPACEMASK;
			if (rspc != spc) {
				regno += naddr + nbridge + nsize;
				continue;
			}
			raddr = 0;
			for (c = 0; c < naddr; c++)
				raddr = ((uint64_t)raddr << 32) | cell[regno++];
			rspc = (pcib)
			    ? cell[regno] & OFW_PCI_PHYS_HI_SPACEMASK
			    : OFW_PADDR_NOT_PCI;
			baddr = 0;
			for (c = 0; c < nbridge; c++)
				baddr = ((uint64_t)baddr << 32) | cell[regno++];
			rsize = 0;
			for (c = 0; c < nsize; c++)
				rsize = ((uint64_t)rsize << 32) | cell[regno++];
			if (addr < raddr || addr >= raddr + rsize)
				continue;
			addr = addr - raddr + baddr;
			if (rspc != OFW_PADDR_NOT_PCI)
				spc = rspc;
		}
	next:
		bridge = parent;
		parent = OF_parent(bridge);
		get_addr_props(bridge, &naddr, &nsize, &pci);
	}

	KASSERT(addr <= BUS_SPACE_MAXADDR,
	    ("Bus address is too large: %jx", (uintmax_t)addr));
	KASSERT(size <= BUS_SPACE_MAXSIZE,
	    ("Bus size is too large: %jx", (uintmax_t)size));

	*paddr = addr;
	*psize = size;
	if (ppci_hi != NULL)
		*ppci_hi = pci_hi;

	return (0);
}

/*
 * This is intended to be called early on, right after the OF system is
 * initialized, so pmap may not be up yet.
 */
int
ofw_parse_bootargs(void)
{
	phandle_t chosen;
	char buf[2048];		/* early stack supposedly big enough */
	int err;

	chosen = OF_finddevice("/chosen");
	if (chosen == -1)
		return (chosen);

	if ((err = OF_getprop(chosen, "bootargs", buf, sizeof(buf))) != -1) {
		boothowto |= boot_parse_cmdline(buf);
		return (0);
	}

	return (err);
}
