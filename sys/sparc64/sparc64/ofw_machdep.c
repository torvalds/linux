/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2001 by Thomas Moestl <tmm@FreeBSD.org>.
 * Copyright (c) 2005 - 2010 by Marius Strobl <marius@FreeBSD.org>.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Some Open Firmware helper functions that are likely machine dependent.
 */

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/systm.h>

#include <net/ethernet.h>

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_pci.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/idprom.h>
#include <machine/ofw_machdep.h>
#include <machine/stdarg.h>

void
OF_getetheraddr(device_t dev, u_char *addr)
{
	char buf[sizeof("true")];
	phandle_t node;
	struct idprom idp;

	if ((node = OF_finddevice("/options")) != -1 &&
	    OF_getprop(node, "local-mac-address?", buf, sizeof(buf)) > 0) {
		buf[sizeof(buf) - 1] = '\0';
		if (strcmp(buf, "true") == 0 &&
		    (node = ofw_bus_get_node(dev)) > 0 &&
		    OF_getprop(node, "local-mac-address", addr,
		    ETHER_ADDR_LEN) == ETHER_ADDR_LEN)
			return;
	}

	node = OF_peer(0);
	if (node <= 0 || OF_getprop(node, "idprom", &idp, sizeof(idp)) == -1)
		panic("Could not determine the machine Ethernet address");
	bcopy(&idp.id_ether, addr, ETHER_ADDR_LEN);
}

u_int
OF_getscsinitid(device_t dev)
{
	phandle_t node;
	uint32_t id;

	for (node = ofw_bus_get_node(dev); node != 0; node = OF_parent(node))
		if (OF_getprop(node, "scsi-initiator-id", &id,
		    sizeof(id)) > 0)
			return (id);
	return (7);
}

void
OF_panic(const char *fmt, ...)
{
	char buf[256];
	va_list ap;

	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	OF_printf("OF_panic: %s\n", buf);
	va_end(ap);
	OF_exit();
}

static __inline uint32_t
phys_hi_mask_space(const char *bus, uint32_t phys_hi)
{

	if (strcmp(bus, "ebus") == 0 || strcmp(bus, "isa") == 0)
		phys_hi &= 0x1;
	else if (strcmp(bus, "pci") == 0)
		phys_hi &= OFW_PCI_PHYS_HI_SPACEMASK;
	/* The phys.hi cells of the other busses only contain space bits. */
	return (phys_hi);
}

/*
 * Return the physical address and the bus space to use for a node
 * referenced by its package handle and the index of the register bank
 * to decode.  Intended to be used to together with sparc64_fake_bustag()
 * by console drivers in early boot only.
 * Works by mapping the address of the node's bank given in the address
 * space of its parent upward in the device tree at each bridge along the
 * path.
 * Currently only really deals with max. 64-bit addresses, i.e. addresses
 * consisting of max. 2 phys cells (phys.hi and phys.lo).  If we encounter
 * a 3 phys cells address (as with PCI addresses) we assume phys.hi can
 * be ignored except for the space bits (generally contained in phys.hi)
 * and treat phys.mid as phys.hi.
 */
int
OF_decode_addr(phandle_t node, int bank, int *space, bus_addr_t *addr)
{
	char name[32];
	uint64_t cend, cstart, end, phys, pphys, sz, start;
	pcell_t addrc, szc, paddrc;
	phandle_t bus, lbus, pbus;
	uint32_t banks[10 * 5];	/* 10 PCI banks */
	uint32_t cspc, pspc, spc;
	int i, j, nbank;

	/*
	 * In general the addresses are contained in the "reg" property
	 * of a node.  The first address in the "reg" property of a PCI
	 * node however is the address of its configuration registers in
	 * the configuration space of the host bridge.  Additional entries
	 * denote the memory and I/O addresses.  For relocatable addresses
	 * the "reg" property contains the BAR, for non-relocatable
	 * addresses it contains the absolute PCI address.  The PCI-only
	 * "assigned-addresses" property however always contains the
	 * absolute PCI addresses.
	 * The "assigned-addresses" and "reg" properties are arrays of
	 * address structures consisting of #address-cells 32-bit phys
	 * cells and #size-cells 32-bit size cells.  If a parent lacks
	 * the "#address-cells" or "#size-cells" property the default
	 * for #address-cells to use is 2 and for #size-cells 1.
	 */
	bus = OF_parent(node);
	if (bus == 0)
		return (ENXIO);
	if (OF_getprop(bus, "name", name, sizeof(name)) == -1)
		return (ENXIO);
	name[sizeof(name) - 1] = '\0';
	if (OF_getprop(bus, "#address-cells", &addrc, sizeof(addrc)) == -1)
		addrc = 2;
	if (OF_getprop(bus, "#size-cells", &szc, sizeof(szc)) == -1)
		szc = 1;
	if (addrc < 2 || addrc > 3 || szc < 1 || szc > 2)
		return (ENXIO);
	if (strcmp(name, "pci") == 0) {
		if (addrc > 3)
			return (ENXIO);
		nbank = OF_getprop(node, "assigned-addresses", &banks,
		    sizeof(banks));
	} else {
		if (addrc > 2)
			return (ENXIO);
		nbank = OF_getprop(node, "reg", &banks, sizeof(banks));
	}
	if (nbank == -1)
		return (ENXIO);
	nbank /= sizeof(banks[0]) * (addrc + szc);
	if (bank < 0 || bank > nbank - 1)
		return (ENXIO);
	bank *= addrc + szc;
	spc = phys_hi_mask_space(name, banks[bank]);
	/* Skip the high cell for 3-cell addresses. */
	bank += addrc - 2;
	phys = 0;
	for (i = 0; i < MIN(2, addrc); i++)
		phys = ((uint64_t)phys << 32) | banks[bank++];
	sz = 0;
	for (i = 0; i < szc; i++)
		sz = ((uint64_t)sz << 32) | banks[bank++];
	start = phys;
	end = phys + sz - 1;

	/*
	 * Map upward in the device tree at every bridge we encounter
	 * using their "ranges" properties.
	 * The "ranges" property of a bridge is an array of a structure
	 * consisting of that bridge's #address-cells 32-bit child-phys
	 * cells, its parent bridge #address-cells 32-bit parent-phys
	 * cells and that bridge's #size-cells 32-bit size cells.
	 * If a bridge doesn't have a "ranges" property no mapping is
	 * necessary at that bridge.
	 */
	cspc = 0;
	lbus = bus;
	while ((pbus = OF_parent(bus)) != 0) {
		if (OF_getprop(pbus, "#address-cells", &paddrc,
		    sizeof(paddrc)) == -1)
			paddrc = 2;
		if (paddrc < 2 || paddrc > 3)
			return (ENXIO);
		nbank = OF_getprop(bus, "ranges", &banks, sizeof(banks));
		if (nbank == -1) {
			if (OF_getprop(pbus, "name", name, sizeof(name)) == -1)
				return (ENXIO);
			name[sizeof(name) - 1] = '\0';
			goto skip;
		}
		if (OF_getprop(bus, "#size-cells", &szc, sizeof(szc)) == -1)
			szc = 1;
		if (szc < 1 || szc > 2)
			return (ENXIO);
		nbank /= sizeof(banks[0]) * (addrc + paddrc + szc);
		bank = 0;
		for (i = 0; i < nbank; i++) {
			cspc = phys_hi_mask_space(name, banks[bank]);
			if (cspc != spc) {
				bank += addrc + paddrc + szc;
				continue;
			}
			/* Skip the high cell for 3-cell addresses. */
			bank += addrc - 2;
			phys = 0;
			for (j = 0; j < MIN(2, addrc); j++)
				phys = ((uint64_t)phys << 32) | banks[bank++];
			pspc = banks[bank];
			/* Skip the high cell for 3-cell addresses. */
			bank += paddrc - 2;
			pphys = 0;
			for (j = 0; j < MIN(2, paddrc); j++)
				pphys =
				    ((uint64_t)pphys << 32) | banks[bank++];
			sz = 0;
			for (j = 0; j < szc; j++)
				sz = ((uint64_t)sz << 32) | banks[bank++];
			cstart = phys;
			cend = phys + sz - 1;
			if (start < cstart || start > cend)
				continue;
			if (end < cstart || end > cend)
				return (ENXIO);
			if (OF_getprop(pbus, "name", name, sizeof(name)) == -1)
				return (ENXIO);
			name[sizeof(name) - 1] = '\0';
			spc = phys_hi_mask_space(name, pspc);
			start += pphys - cstart;
			end += pphys - cstart;
			break;
		}
		if (i == nbank)
			return (ENXIO);
		lbus = bus;
 skip:
		addrc = paddrc;
		bus = pbus;
	}

	*addr = start;
	/* Determine the bus space based on the last bus we mapped. */
	if (OF_parent(lbus) == 0) {
		*space = NEXUS_BUS_SPACE;
		return (0);
	}
	if (OF_getprop(lbus, "name", name, sizeof(name)) == -1)
		return (ENXIO);
	name[sizeof(name) - 1] = '\0';
	if (strcmp(name, "central") == 0 || strcmp(name, "ebus") == 0 ||
	    strcmp(name, "upa") == 0) {
		*space = NEXUS_BUS_SPACE;
		return (0);
	} else if (strcmp(name, "pci") == 0) {
		switch (cspc) {
		case OFW_PCI_PHYS_HI_SPACE_IO:
			*space = PCI_IO_BUS_SPACE;
			return (0);
		case OFW_PCI_PHYS_HI_SPACE_MEM32:
			*space = PCI_MEMORY_BUS_SPACE;
			return (0);
		}
	} else if (strcmp(name, "sbus") == 0) {
		*space = SBUS_BUS_SPACE;
		return (0);
	}
	return (ENXIO);
}
