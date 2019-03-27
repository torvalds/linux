/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 Marcel Moolenaar
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

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>

#include <dev/ppbus/ppbconf.h>
#include <dev/ppbus/ppb_msq.h>
#include <dev/ppc/ppcvar.h>
#include <dev/ppc/ppcreg.h>

#include "ppbus_if.h"

static int ppc_pci_probe(device_t dev);

static device_method_t ppc_pci_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		ppc_pci_probe),
	DEVMETHOD(device_attach,	ppc_attach),
	DEVMETHOD(device_detach,	ppc_detach),

	/* bus interface */
	DEVMETHOD(bus_read_ivar,	ppc_read_ivar),
	DEVMETHOD(bus_write_ivar,	ppc_write_ivar),
	DEVMETHOD(bus_alloc_resource,	ppc_alloc_resource),
	DEVMETHOD(bus_release_resource,	ppc_release_resource),

	/* ppbus interface */
	DEVMETHOD(ppbus_io,		ppc_io),
	DEVMETHOD(ppbus_exec_microseq,	ppc_exec_microseq),
	DEVMETHOD(ppbus_reset_epp,	ppc_reset_epp),
	DEVMETHOD(ppbus_setmode,	ppc_setmode),
	DEVMETHOD(ppbus_ecp_sync,	ppc_ecp_sync),
	DEVMETHOD(ppbus_read,		ppc_read),
	DEVMETHOD(ppbus_write,		ppc_write),

	{ 0, 0 }
};

static driver_t ppc_pci_driver = {
	ppc_driver_name,
	ppc_pci_methods,
	sizeof(struct ppc_data),
};

struct pci_id {
	uint32_t	type;
	const char	*desc;
	int		rid;
};

static struct pci_id pci_ids[] = {
	{ 0x1020131f, "SIIG Cyber Parallel PCI (10x family)", 0x18 },
	{ 0x2020131f, "SIIG Cyber Parallel PCI (20x family)", 0x10 },
	{ 0x05111407, "Lava SP BIDIR Parallel PCI", 0x10 },
	{ 0x80001407, "Lava Computers 2SP-PCI parallel port", 0x10 },
	{ 0x84031415, "Oxford Semiconductor OX12PCI840 Parallel port", 0x10 },
	{ 0x95131415, "Oxford Semiconductor OX16PCI954 Parallel port", 0x10 },
	{ 0xc1101415, "Oxford Semiconductor OXPCIe952 Parallel port", 0x10 },
	{ 0x98059710, "NetMos NM9805 1284 Printer port", 0x10 },
	{ 0x98659710, "MosChip MCS9865 1284 Printer port", 0x10 },
	{ 0x99009710, "MosChip MCS9900 PCIe to Peripheral Controller", 0x10 },
	{ 0x99019710, "MosChip MCS9901 PCIe to Peripheral Controller", 0x10 },
	{ 0xffff }
};

static int
ppc_pci_probe(device_t dev)
{
	struct pci_id *id;
	uint32_t type;

	type = pci_get_devid(dev);
	id = pci_ids;
	while (id->type != 0xffff && id->type != type)
		id++;
	if (id->type == 0xffff)
		return (ENXIO);
	device_set_desc(dev, id->desc);
	return (ppc_probe(dev, id->rid));
}

DRIVER_MODULE(ppc, pci, ppc_pci_driver, ppc_devclass, 0, 0);
