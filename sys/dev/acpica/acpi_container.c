/*-
 * Copyright (c) 2017 Microsoft Corp.
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
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>

#include <contrib/dev/acpica/include/acpi.h>
#include <dev/acpica/acpivar.h>

#include "pcib_if.h"

ACPI_MODULE_NAME("CONTAINER")

static int			acpi_syscont_probe(device_t);
static int			acpi_syscont_attach(device_t);
static int			acpi_syscont_detach(device_t);
static int			acpi_syscont_alloc_msi(device_t, device_t,
				    int count, int maxcount, int *irqs);
static int			acpi_syscont_release_msi(device_t bus, device_t dev,
				    int count, int *irqs);
static int			acpi_syscont_alloc_msix(device_t bus, device_t dev,
				    int *irq);
static int			acpi_syscont_release_msix(device_t bus, device_t dev,
				    int irq);
static int			acpi_syscont_map_msi(device_t bus, device_t dev,
				    int irq, uint64_t *addr, uint32_t *data);

static device_method_t acpi_syscont_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,		acpi_syscont_probe),
    DEVMETHOD(device_attach,		acpi_syscont_attach),
    DEVMETHOD(device_detach,		acpi_syscont_detach),

    /* Bus interface */
    DEVMETHOD(bus_add_child,		bus_generic_add_child),
    DEVMETHOD(bus_print_child,		bus_generic_print_child),
    DEVMETHOD(bus_alloc_resource,	bus_generic_alloc_resource),
    DEVMETHOD(bus_release_resource,	bus_generic_release_resource),
    DEVMETHOD(bus_activate_resource,	bus_generic_activate_resource),
    DEVMETHOD(bus_deactivate_resource,	bus_generic_deactivate_resource),
    DEVMETHOD(bus_setup_intr,		bus_generic_setup_intr),
    DEVMETHOD(bus_teardown_intr,	bus_generic_teardown_intr),
#if __FreeBSD_version >= 1100000
    DEVMETHOD(bus_get_cpus,		bus_generic_get_cpus),
#endif

    /* pcib interface */
    DEVMETHOD(pcib_alloc_msi,		acpi_syscont_alloc_msi),
    DEVMETHOD(pcib_release_msi,		acpi_syscont_release_msi),
    DEVMETHOD(pcib_alloc_msix,		acpi_syscont_alloc_msix),
    DEVMETHOD(pcib_release_msix,	acpi_syscont_release_msix),
    DEVMETHOD(pcib_map_msi,		acpi_syscont_map_msi),

    DEVMETHOD_END
};

static driver_t acpi_syscont_driver = {
    "acpi_syscontainer",
    acpi_syscont_methods,
    0,
};

static devclass_t acpi_syscont_devclass;

DRIVER_MODULE(acpi_syscontainer, acpi, acpi_syscont_driver,
    acpi_syscont_devclass, NULL, NULL);
MODULE_DEPEND(acpi_syscontainer, acpi, 1, 1, 1);

static int
acpi_syscont_probe(device_t dev)
{
    static char *syscont_ids[] = { "ACPI0004", "PNP0A05", "PNP0A06", NULL };
    int rv;

    if (acpi_disabled("syscontainer"))
	return (ENXIO);
    rv = ACPI_ID_PROBE(device_get_parent(dev), dev, syscont_ids, NULL);
    if (rv <= 0)
	device_set_desc(dev, "System Container");
    return (rv);
}

static int
acpi_syscont_attach(device_t dev)
{

    bus_generic_probe(dev);
    return (bus_generic_attach(dev));
}

static int
acpi_syscont_detach(device_t dev)
{

    return (bus_generic_detach(dev));
}

static int
acpi_syscont_alloc_msi(device_t bus, device_t dev, int count, int maxcount,
    int *irqs)
{
    device_t parent = device_get_parent(bus);

    return (PCIB_ALLOC_MSI(device_get_parent(parent), dev, count, maxcount,
	irqs));
}

static int
acpi_syscont_release_msi(device_t bus, device_t dev, int count, int *irqs)
{
    device_t parent = device_get_parent(bus);

    return (PCIB_RELEASE_MSI(device_get_parent(parent), dev, count, irqs));
}

static int
acpi_syscont_alloc_msix(device_t bus, device_t dev, int *irq)
{
    device_t parent = device_get_parent(bus);

    return (PCIB_ALLOC_MSIX(device_get_parent(parent), dev, irq));
}

static int
acpi_syscont_release_msix(device_t bus, device_t dev, int irq)
{
    device_t parent = device_get_parent(bus);

    return (PCIB_RELEASE_MSIX(device_get_parent(parent), dev, irq));
}

static int
acpi_syscont_map_msi(device_t bus, device_t dev, int irq, uint64_t *addr,
    uint32_t *data)
{
    device_t parent = device_get_parent(bus);

    return (PCIB_MAP_MSI(device_get_parent(parent), dev, irq, addr, data));
}
