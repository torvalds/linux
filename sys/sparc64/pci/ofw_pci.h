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
 */

/*-
 * Copyright (c) 1998, 1999 Eduardo E. Horvath
 * Copyright (c) 2001, 2003 by Thomas Moestl <tmm@FreeBSD.org>
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
 *	from: NetBSD: psychoreg.h,v 1.14 2008/05/30 02:29:37 mrg Exp
 *
 * $FreeBSD$
 */

#ifndef _SPARC64_PCI_OFW_PCI_H_
#define	_SPARC64_PCI_OFW_PCI_H_

#include <sys/rman.h>

#include <dev/ofw/ofw_bus_subr.h>

#include "ofw_pci_if.h"

typedef uint32_t ofw_pci_intr_t;

/* PCI range child spaces. XXX: are these MI? */
#define	OFW_PCI_CS_CONFIG	0x00
#define	OFW_PCI_CS_IO		0x01
#define	OFW_PCI_CS_MEM32	0x02
#define	OFW_PCI_CS_MEM64	0x03
#define	OFW_PCI_NUM_CS		4

/* OFW device types */
#define	OFW_TYPE_PCI		"pci"
#define	OFW_TYPE_PCIE		"pciex"

struct ofw_pci_msi_addr_ranges {
	uint32_t	addr32_hi;
	uint32_t	addr32_lo;
	uint32_t	addr32_sz;
	uint32_t	addr64_hi;
	uint32_t	addr64_lo;
	uint32_t	addr64_sz;
};

#define	OFW_PCI_MSI_ADDR_RANGE_32(r) \
	(((uint64_t)(r)->addr32_hi << 32) | (uint64_t)(r)->addr32_lo)
#define	OFW_PCI_MSI_ADDR_RANGE_64(r) \
	(((uint64_t)(r)->addr64_hi << 32) | (uint64_t)(r)->addr64_lo)

struct ofw_pci_msi_eq_to_devino {
	uint32_t	eq_first;
	uint32_t	eq_count;
	uint32_t	devino_first;
};

struct ofw_pci_msi_ranges {
	uint32_t	first;
	uint32_t	count;
};

struct ofw_pci_ranges {
	uint32_t	cspace;
	uint32_t	child_hi;
	uint32_t	child_lo;
	uint32_t	phys_hi;
	uint32_t	phys_lo;
	uint32_t	size_hi;
	uint32_t	size_lo;
};

#define	OFW_PCI_RANGE_CHILD(r) \
	(((uint64_t)(r)->child_hi << 32) | (uint64_t)(r)->child_lo)
#define	OFW_PCI_RANGE_PHYS(r) \
	(((uint64_t)(r)->phys_hi << 32) | (uint64_t)(r)->phys_lo)
#define	OFW_PCI_RANGE_SIZE(r) \
	(((uint64_t)(r)->size_hi << 32) | (uint64_t)(r)->size_lo)
#define	OFW_PCI_RANGE_CS(r)	(((r)->cspace >> 24) & 0x03)

/* default values */
#define	OFW_PCI_LATENCY	64

/*
 * Common and generic parts of host-PCI-bridge support
 */

struct ofw_pci_softc {
	struct rman		sc_pci_mem_rman;
	struct rman		sc_pci_io_rman;

	bus_space_handle_t	sc_pci_bh[OFW_PCI_NUM_CS];
	bus_space_tag_t		sc_pci_cfgt;
	bus_space_tag_t		sc_pci_iot;
	bus_dma_tag_t		sc_pci_dmat;

	struct ofw_bus_iinfo	sc_pci_iinfo;

	phandle_t		sc_node;

	uint8_t			sc_pci_secbus;
	uint8_t			sc_pci_subbus;
};

int ofw_pci_attach_common(device_t dev, bus_dma_tag_t dmat, u_long iosize,
    u_long memsize);
uint32_t ofw_pci_read_config_common(device_t dev, u_int regmax, u_long offset,
    u_int bus, u_int slot, u_int func, u_int reg, int width);
void ofw_pci_write_config_common(device_t dev, u_int regmax, u_long offset,
    u_int bus, u_int slot, u_int func, u_int reg, uint32_t val, int width);
ofw_pci_intr_t ofw_pci_route_interrupt_common(device_t bridge, device_t dev,
    int pin);

void ofw_pci_dmamap_sync_stst_order_common(void);

bus_activate_resource_t ofw_pci_activate_resource;
bus_adjust_resource_t ofw_pci_adjust_resource;
bus_alloc_resource_t ofw_pci_alloc_resource;
bus_get_dma_tag_t ofw_pci_get_dma_tag;
bus_read_ivar_t ofw_pci_read_ivar;

ofw_bus_get_node_t ofw_pci_get_node;

#endif /* ! _SPARC64_PCI_OFW_PCI_H_ */
