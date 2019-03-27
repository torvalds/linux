/*-
 * Copyright (c) 2011 Nathan Whitehorn
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

#ifndef _DEV_OFW_OFWPCI_H_
#define	_DEV_OFW_OFWPCI_H_

/*
 * Export class definition for inheritance purposes
 */
DECLARE_CLASS(ofw_pci_driver);

struct ofw_pci_cell_info {
	pcell_t host_address_cells;
	pcell_t pci_address_cell;
	pcell_t size_cells;
 };

struct ofw_pci_range {
	uint32_t	pci_hi;
	uint64_t	pci;
	uint64_t	host;
	uint64_t	size;
};

/*
 * Quirks for some adapters
 */
enum {
	OFW_PCI_QUIRK_RANGES_ON_CHILDREN = 1,
};

struct ofw_pci_softc {
	device_t	sc_dev;
	phandle_t	sc_node;
	int		sc_bus;
	int		sc_initialized;
	int		sc_quirks;
	int		sc_have_pmem;

	struct ofw_pci_range		*sc_range;
	int				sc_nrange;
	uint64_t			sc_range_mask;
	struct ofw_pci_cell_info	*sc_cell_info;

	struct rman			sc_io_rman;
	struct rman			sc_mem_rman;
	struct rman			sc_pmem_rman;
	bus_space_tag_t			sc_memt;
	bus_dma_tag_t			sc_dmat;
	int				sc_pci_domain;

	struct ofw_bus_iinfo		sc_pci_iinfo;
};

int ofw_pci_init(device_t);
int ofw_pci_attach(device_t);
int ofw_pci_read_ivar(device_t, device_t, int, uintptr_t *);
int ofw_pci_write_ivar(device_t, device_t, int, uintptr_t);
int ofw_pci_route_interrupt(device_t, device_t, int);
int ofw_pci_nranges(phandle_t, struct ofw_pci_cell_info *);

#endif /* _DEV_OFW_OFWPCI_H_ */
