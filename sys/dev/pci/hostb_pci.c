/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 1997, Stefan Esser <se@freebsd.org>
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

/*
 * Provide a device to "eat" the host->pci bridge devices that show up
 * on PCI buses and stop them showing up twice on the probes.  This also
 * stops them showing up as 'none' in pciconf -l.  If the host bridge
 * provides an AGP capability then we create a child agp device for the
 * agp GART driver to attach to.
 */
static int
pci_hostb_probe(device_t dev)
{
	u_int32_t id;

	id = pci_get_devid(dev);

	switch (id) {

	/* VIA VT82C596 Power Management Function */
	case 0x30501106:
		return (ENXIO);

	default:
		break;
	}

	if (pci_get_class(dev) == PCIC_BRIDGE &&
	    pci_get_subclass(dev) == PCIS_BRIDGE_HOST) {
		device_set_desc(dev, "Host to PCI bridge");
		device_quiet(dev);
		return (-10000);
	}
	return (ENXIO);
}

static int
pci_hostb_attach(device_t dev)
{

	bus_generic_probe(dev);

	/*
	 * If AGP capabilities are present on this device, then create
	 * an AGP child.
	 */
	if (pci_find_cap(dev, PCIY_AGP, NULL) == 0)
		device_add_child(dev, "agp", -1);
	bus_generic_attach(dev);
	return (0);
}

/* Bus interface. */

static int
pci_hostb_read_ivar(device_t dev, device_t child, int which, uintptr_t *result)
{

	return (BUS_READ_IVAR(device_get_parent(dev), dev, which, result));
}

static int
pci_hostb_write_ivar(device_t dev, device_t child, int which, uintptr_t value)
{

	return (EINVAL);
}

static struct resource *
pci_hostb_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{

	return (bus_alloc_resource(dev, type, rid, start, end, count, flags));
}

static int
pci_hostb_release_resource(device_t dev, device_t child, int type, int rid,
    struct resource *r)
{

	return (bus_release_resource(dev, type, rid, r));
}

/* PCI interface. */

static uint32_t
pci_hostb_read_config(device_t dev, device_t child, int reg, int width)
{

	return (pci_read_config(dev, reg, width));
}

static void
pci_hostb_write_config(device_t dev, device_t child, int reg, 
    uint32_t val, int width)
{

	pci_write_config(dev, reg, val, width);
}

static int
pci_hostb_enable_busmaster(device_t dev, device_t child)
{

	device_printf(dev, "child %s requested pci_enable_busmaster\n",
	    device_get_nameunit(child));
	return (pci_enable_busmaster(dev));
}

static int
pci_hostb_disable_busmaster(device_t dev, device_t child)
{

	device_printf(dev, "child %s requested pci_disable_busmaster\n",
	    device_get_nameunit(child));
	return (pci_disable_busmaster(dev));
}

static int
pci_hostb_enable_io(device_t dev, device_t child, int space)
{

	device_printf(dev, "child %s requested pci_enable_io\n",
	    device_get_nameunit(child));
	return (pci_enable_io(dev, space));
}

static int
pci_hostb_disable_io(device_t dev, device_t child, int space)
{

	device_printf(dev, "child %s requested pci_disable_io\n",
	    device_get_nameunit(child));
	return (pci_disable_io(dev, space));
}

static int
pci_hostb_set_powerstate(device_t dev, device_t child, int state)
{

	device_printf(dev, "child %s requested pci_set_powerstate\n",
	    device_get_nameunit(child));
	return (pci_set_powerstate(dev, state));
}

static int
pci_hostb_get_powerstate(device_t dev, device_t child)
{

	device_printf(dev, "child %s requested pci_get_powerstate\n",
	    device_get_nameunit(child));
	return (pci_get_powerstate(dev));
}

static int
pci_hostb_assign_interrupt(device_t dev, device_t child)
{

	device_printf(dev, "child %s requested pci_assign_interrupt\n",
	    device_get_nameunit(child));
	return (PCI_ASSIGN_INTERRUPT(device_get_parent(dev), dev));
}

static int
pci_hostb_find_cap(device_t dev, device_t child, int capability,
    int *capreg)
{

	return (pci_find_cap(dev, capability, capreg));
}

static int
pci_hostb_find_next_cap(device_t dev, device_t child, int capability,
    int start, int *capreg)
{

	return (pci_find_next_cap(dev, capability, start, capreg));
}

static int
pci_hostb_find_extcap(device_t dev, device_t child, int capability,
    int *capreg)
{

	return (pci_find_extcap(dev, capability, capreg));
}

static int
pci_hostb_find_next_extcap(device_t dev, device_t child, int capability,
    int start, int *capreg)
{

	return (pci_find_next_extcap(dev, capability, start, capreg));
}

static int
pci_hostb_find_htcap(device_t dev, device_t child, int capability,
    int *capreg)
{

	return (pci_find_htcap(dev, capability, capreg));
}

static int
pci_hostb_find_next_htcap(device_t dev, device_t child, int capability,
    int start, int *capreg)
{

	return (pci_find_next_htcap(dev, capability, start, capreg));
}

static device_method_t pci_hostb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		pci_hostb_probe),
	DEVMETHOD(device_attach,	pci_hostb_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	pci_hostb_read_ivar),
	DEVMETHOD(bus_write_ivar,	pci_hostb_write_ivar),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	DEVMETHOD(bus_alloc_resource,	pci_hostb_alloc_resource),
	DEVMETHOD(bus_release_resource,	pci_hostb_release_resource),
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),

	/* PCI interface */
	DEVMETHOD(pci_read_config,	pci_hostb_read_config),
	DEVMETHOD(pci_write_config,	pci_hostb_write_config),
	DEVMETHOD(pci_enable_busmaster,	pci_hostb_enable_busmaster),
	DEVMETHOD(pci_disable_busmaster, pci_hostb_disable_busmaster),
	DEVMETHOD(pci_enable_io,	pci_hostb_enable_io),
	DEVMETHOD(pci_disable_io,	pci_hostb_disable_io),
	DEVMETHOD(pci_get_powerstate,	pci_hostb_get_powerstate),
	DEVMETHOD(pci_set_powerstate,	pci_hostb_set_powerstate),
	DEVMETHOD(pci_assign_interrupt,	pci_hostb_assign_interrupt),
	DEVMETHOD(pci_find_cap,		pci_hostb_find_cap),
	DEVMETHOD(pci_find_next_cap,	pci_hostb_find_next_cap),
	DEVMETHOD(pci_find_extcap,	pci_hostb_find_extcap),
	DEVMETHOD(pci_find_next_extcap,	pci_hostb_find_next_extcap),
	DEVMETHOD(pci_find_htcap,	pci_hostb_find_htcap),
	DEVMETHOD(pci_find_next_htcap,	pci_hostb_find_next_htcap),

	{ 0, 0 }
};

static driver_t pci_hostb_driver = {
	"hostb",
	pci_hostb_methods,
	1,
};

static devclass_t pci_hostb_devclass;

DRIVER_MODULE(hostb, pci, pci_hostb_driver, pci_hostb_devclass, 0, 0);
