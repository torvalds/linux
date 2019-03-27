/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1996, David Greenman
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

/*
 * Cyclades Y PCI serial interface driver
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include "opt_cy_pci_fastintr.h"

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <dev/pci/pcivar.h>

#include <dev/cy/cyvar.h>

#define	CY_PCI_BASE_ADDR0		0x10
#define	CY_PCI_BASE_ADDR1		0x14
#define	CY_PCI_BASE_ADDR2		0x18

#define	CY_PLX_9050_ICS			0x4c
#define	CY_PLX_9060_ICS			0x68
#define	CY_PLX_9050_ICS_IENABLE		0x040
#define	CY_PLX_9050_ICS_LOCAL_IENABLE	0x001
#define	CY_PLX_9050_ICS_LOCAL_IPOLARITY	0x002
#define	CY_PLX_9060_ICS_IENABLE		0x100
#define	CY_PLX_9060_ICS_LOCAL_IENABLE	0x800

/* Cyclom-Y Custom Register for PLX ID. */
#define	PLX_VER				0x3400
#define	PLX_9050			0x0b
#define	PLX_9060			0x0c
#define	PLX_9080			0x0d

static int	cy_pci_attach(device_t dev);
static int	cy_pci_probe(device_t dev);

static device_method_t cy_pci_methods[] = {
	/* Device interface. */
	DEVMETHOD(device_probe,		cy_pci_probe),
	DEVMETHOD(device_attach,	cy_pci_attach),

	{ 0, 0 }
};

static driver_t cy_pci_driver = {
	cy_driver_name,
	cy_pci_methods,
	0,
};

DRIVER_MODULE(cy, pci, cy_pci_driver, cy_devclass, 0, 0);
MODULE_DEPEND(cy, pci, 1, 1, 1);

static int
cy_pci_probe(dev)
	device_t dev;
{
	u_int32_t device_id;

	device_id = pci_get_devid(dev);
	device_id &= ~0x00060000;
	if (device_id != 0x0100120e && device_id != 0x0101120e)
		return (ENXIO);
	device_set_desc(dev, "Cyclades Cyclom-Y Serial Adapter");
	return (BUS_PROBE_DEFAULT);
}

static int
cy_pci_attach(dev)
	device_t dev;
{
	struct resource *ioport_res, *irq_res, *mem_res;
	void *irq_cookie, *vaddr, *vsc;
	u_int32_t ioport;
	int irq_setup, ioport_rid, irq_rid, mem_rid;
	u_char plx_ver;

	ioport_res = NULL;
	irq_res = NULL;
	mem_res = NULL;

	ioport_rid = CY_PCI_BASE_ADDR1;
	ioport_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT, &ioport_rid,
	    RF_ACTIVE);
	if (ioport_res == NULL) {
		device_printf(dev, "ioport resource allocation failed\n");
		goto fail;
	}
	ioport = rman_get_start(ioport_res);

	mem_rid = CY_PCI_BASE_ADDR2;
	mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &mem_rid,
	    RF_ACTIVE);
	if (mem_res == NULL) {
		device_printf(dev, "memory resource allocation failed\n");
		goto fail;
	}
	vaddr = rman_get_virtual(mem_res);

	vsc = cyattach_common(vaddr, 1);
	if (vsc == NULL) {
		device_printf(dev, "no ports found!\n");
		goto fail;
	}

	irq_rid = 0;
	irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &irq_rid,
	    RF_SHAREABLE | RF_ACTIVE);
	if (irq_res == NULL) {
		device_printf(dev, "interrupt resource allocation failed\n");
		goto fail;
	}
#ifdef CY_PCI_FASTINTR
	irq_setup = bus_setup_intr(dev, irq_res, INTR_TYPE_TTY,
	    cyintr, NULL, vsc, &irq_cookie);
#else
	irq_setup = ENXIO;
#endif
	if (irq_setup != 0)
		irq_setup = bus_setup_intr(dev, irq_res, INTR_TYPE_TTY,
		    NULL, (driver_intr_t *)cyintr, vsc, &irq_cookie);
	if (irq_setup != 0) {
		device_printf(dev, "interrupt setup failed\n");
		goto fail;
	}

	/*
	 * Enable the "local" interrupt input to generate a
	 * PCI interrupt.
	 */
	plx_ver = *((u_char *)vaddr + PLX_VER) & 0x0f;
	switch (plx_ver) {
	case PLX_9050:
		outw(ioport + CY_PLX_9050_ICS,
		    CY_PLX_9050_ICS_IENABLE | CY_PLX_9050_ICS_LOCAL_IENABLE |
		    CY_PLX_9050_ICS_LOCAL_IPOLARITY);
		break;
	case PLX_9060:
	case PLX_9080:
	default:		/* Old board, use PLX_9060 values. */
		outw(ioport + CY_PLX_9060_ICS,
		    inw(ioport + CY_PLX_9060_ICS) | CY_PLX_9060_ICS_IENABLE |
		    CY_PLX_9060_ICS_LOCAL_IENABLE);
		break;
	}

	return (0);

fail:
	if (ioport_res != NULL)
		bus_release_resource(dev, SYS_RES_IOPORT, ioport_rid,
		    ioport_res);
	if (irq_res != NULL)
		bus_release_resource(dev, SYS_RES_IRQ, irq_rid, irq_res);
	if (mem_res != NULL)
		bus_release_resource(dev, SYS_RES_MEMORY, mem_rid, mem_res);
	return (ENXIO);
}
