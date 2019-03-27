/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2003 John Baldwin <jhb@FreeBSD.org>
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

/*
 * Host to PCI and PCI to PCI bridge drivers that use the MP Table to route
 * interrupts from PCI devices to I/O APICs.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcib_private.h>
#include <x86/mptable.h>
#include <x86/legacyvar.h>
#include <machine/pci_cfgreg.h>

#include "pcib_if.h"

/* Host to PCI bridge driver. */

static int
mptable_hostb_probe(device_t dev)
{

	if (pci_cfgregopen() == 0)
		return (ENXIO);
	if (mptable_pci_probe_table(pcib_get_bus(dev)) != 0)
		return (ENXIO);
	device_set_desc(dev, "MPTable Host-PCI bridge");
	return (0);
}

static int
mptable_hostb_attach(device_t dev)
{

#ifdef NEW_PCIB
	mptable_pci_host_res_init(dev);
#endif
	device_add_child(dev, "pci", -1);
	return (bus_generic_attach(dev));
}

#ifdef NEW_PCIB
static int
mptable_is_isa_range(rman_res_t start, rman_res_t end)
{

	if (end >= 0x10000)
		return (0);
	if ((start & 0xfc00) != (end & 0xfc00))
		return (0);
	start &= ~0xfc00;
	end &= ~0xfc00;
	return (start >= 0x100 && end <= 0x3ff);
}

static int
mptable_is_vga_range(rman_res_t start, rman_res_t end)
{
	if (end >= 0x10000)
		return (0);
	if ((start & 0xfc00) != (end & 0xfc00))
		return (0);
	start &= ~0xfc00;
	end &= ~0xfc00;
	return (pci_is_vga_ioport_range(start, end));
}

static struct resource *
mptable_hostb_alloc_resource(device_t dev, device_t child, int type, int *rid,
    rman_res_t start, rman_res_t end, rman_res_t count, u_int flags)
{
	struct mptable_hostb_softc *sc;

#ifdef PCI_RES_BUS
	if (type == PCI_RES_BUS)
		return (pci_domain_alloc_bus(0, child, rid, start, end, count,
		    flags));
#endif
	sc = device_get_softc(dev);
	if (type == SYS_RES_IOPORT && start + count - 1 == end) {
		if (mptable_is_isa_range(start, end)) {
			switch (sc->sc_decodes_isa_io) {
			case -1:
				return (NULL);
			case 1:
				return (bus_generic_alloc_resource(dev, child,
				    type, rid, start, end, count, flags));
			default:
				break;
			}
		}
		if (mptable_is_vga_range(start, end)) {
			switch (sc->sc_decodes_vga_io) {
			case -1:
				return (NULL);
			case 1:
				return (bus_generic_alloc_resource(dev, child,
				    type, rid, start, end, count, flags));
			default:
				break;
			}
		}
	}
	start = hostb_alloc_start(type, start, end, count);
	return (pcib_host_res_alloc(&sc->sc_host_res, child, type, rid, start,
	    end, count, flags));
}

static int
mptable_hostb_adjust_resource(device_t dev, device_t child, int type,
    struct resource *r, rman_res_t start, rman_res_t end)
{
	struct mptable_hostb_softc *sc;

#ifdef PCI_RES_BUS
	if (type == PCI_RES_BUS)
		return (pci_domain_adjust_bus(0, child, r, start, end));
#endif
	sc = device_get_softc(dev);
	return (pcib_host_res_adjust(&sc->sc_host_res, child, type, r, start,
	    end));
}
#endif

static device_method_t mptable_hostb_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mptable_hostb_probe),
	DEVMETHOD(device_attach,	mptable_hostb_attach),
	DEVMETHOD(device_shutdown,	bus_generic_shutdown),
	DEVMETHOD(device_suspend,	bus_generic_suspend),
	DEVMETHOD(device_resume,	bus_generic_resume),

	/* Bus interface */
	DEVMETHOD(bus_read_ivar,	legacy_pcib_read_ivar),
	DEVMETHOD(bus_write_ivar,	legacy_pcib_write_ivar),
#ifdef NEW_PCIB
	DEVMETHOD(bus_alloc_resource,	mptable_hostb_alloc_resource),
	DEVMETHOD(bus_adjust_resource,	mptable_hostb_adjust_resource),
#else
	DEVMETHOD(bus_alloc_resource,	legacy_pcib_alloc_resource),
	DEVMETHOD(bus_adjust_resource,	bus_generic_adjust_resource),
#endif
#if defined(NEW_PCIB) && defined(PCI_RES_BUS)
	DEVMETHOD(bus_release_resource,	legacy_pcib_release_resource),
#else
	DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
#endif
	DEVMETHOD(bus_activate_resource, bus_generic_activate_resource),
	DEVMETHOD(bus_deactivate_resource, bus_generic_deactivate_resource),
	DEVMETHOD(bus_setup_intr,	bus_generic_setup_intr),
	DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),

	/* pcib interface */
	DEVMETHOD(pcib_maxslots,	legacy_pcib_maxslots),
	DEVMETHOD(pcib_read_config,	legacy_pcib_read_config),
	DEVMETHOD(pcib_write_config,	legacy_pcib_write_config),
	DEVMETHOD(pcib_route_interrupt,	mptable_pci_route_interrupt),
	DEVMETHOD(pcib_alloc_msi,	legacy_pcib_alloc_msi),
	DEVMETHOD(pcib_release_msi,	pcib_release_msi),
	DEVMETHOD(pcib_alloc_msix,	legacy_pcib_alloc_msix),
	DEVMETHOD(pcib_release_msix,	pcib_release_msix),
	DEVMETHOD(pcib_map_msi,		legacy_pcib_map_msi),

	DEVMETHOD_END
};

static devclass_t hostb_devclass;

DEFINE_CLASS_0(pcib, mptable_hostb_driver, mptable_hostb_methods,
    sizeof(struct mptable_hostb_softc));
DRIVER_MODULE(mptable_pcib, legacy, mptable_hostb_driver, hostb_devclass, 0, 0);

/* PCI to PCI bridge driver. */

static int
mptable_pcib_probe(device_t dev)
{
	int bus;

	if ((pci_get_class(dev) != PCIC_BRIDGE) ||
	    (pci_get_subclass(dev) != PCIS_BRIDGE_PCI))
		return (ENXIO);
	bus = pci_read_config(dev, PCIR_SECBUS_1, 1);
	if (bus == 0)
		return (ENXIO);
	if (mptable_pci_probe_table(bus) != 0)
		return (ENXIO);
	device_set_desc(dev, "MPTable PCI-PCI bridge");
	return (-1000);
}

static device_method_t mptable_pcib_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		mptable_pcib_probe),

	/* pcib interface */
	DEVMETHOD(pcib_route_interrupt,	mptable_pci_route_interrupt),

	{0, 0}
};

static devclass_t pcib_devclass;

DEFINE_CLASS_1(pcib, mptable_pcib_driver, mptable_pcib_pci_methods,
    sizeof(struct pcib_softc), pcib_driver);
DRIVER_MODULE(mptable_pcib, pci, mptable_pcib_driver, pcib_devclass, 0, 0);
