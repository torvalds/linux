/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 1999, 2000 Matthew R. Green
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	from: NetBSD: ebus.c,v 1.52 2008/05/29 14:51:26 mrg Exp
 */
/*-
 * Copyright (c) 2001, 2003 Thomas Moestl <tmm@FreeBSD.org>
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
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Helper functions which can be used in both ISA and EBus code.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>

#include <dev/ofw/ofw_bus_subr.h>
#include <dev/ofw/openfirm.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <sparc64/pci/ofw_pci.h>
#include <sparc64/isa/ofw_isa.h>

#include "pcib_if.h"

int
ofw_isa_range_restype(struct isa_ranges *range)
{
	int ps = ISA_RANGE_PS(range);

	switch (ps) {
	case OFW_PCI_CS_IO:
		return (SYS_RES_IOPORT);
	case OFW_PCI_CS_MEM32:
		return (SYS_RES_MEMORY);
	default:
		panic("ofw_isa_range_restype: illegal space %x", ps);
	}

}

/* XXX: this only supports PCI as parent bus right now. */
int
ofw_isa_range_map(struct isa_ranges *range, int nrange, u_long *start,
    u_long *end, int *which)
{
	struct isa_ranges *r;
	uint64_t offs, cstart, cend;
	int i;

	for (i = 0; i < nrange; i++) {
		r = &range[i];
		cstart = ISA_RANGE_CHILD(r);
		cend = cstart + r->size - 1;
		if (*start < cstart || *start > cend)
			continue;
		if (*end < cstart || *end > cend) {
			panic("ofw_isa_map_iorange: iorange crosses PCI "
			    "ranges (%#lx not in %#lx - %#lx)", *end, cstart,
			    cend);
		}
		offs = ISA_RANGE_PHYS(r);
		*start = *start + offs - cstart;
		*end  = *end + offs - cstart;
		if (which != NULL)
			*which = i;
		return (ofw_isa_range_restype(r));
	}
	panic("ofw_isa_map_iorange: could not map range %#lx - %#lx",
	    *start, *end);
}

ofw_pci_intr_t
ofw_isa_route_intr(device_t bridge, phandle_t node, struct ofw_bus_iinfo *ii,
    ofw_isa_intr_t intr)
{
	struct isa_regs reg;
	device_t pbridge;
	ofw_isa_intr_t mintr;

	pbridge = device_get_parent(device_get_parent(bridge));
	/*
	 * If we get a match from using the map, the resulting INO is
	 * fully specified, so we may not continue to map.
	 */
	if (!ofw_bus_lookup_imap(node, ii, &reg, sizeof(reg),
	    &intr, sizeof(intr), &mintr, sizeof(mintr), NULL)) {
		/* Try routing at the parent bridge. */
		mintr = PCIB_ROUTE_INTERRUPT(pbridge, bridge, intr);
	}
	return (mintr);
}
