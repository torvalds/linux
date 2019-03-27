/*
 * Copyright (c) 2013 Roger Pau Monné <roger.pau@citrix.com>
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS AS IS'' AND
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
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/smp.h>

#include <contrib/dev/acpica/include/acpi.h>

#include <dev/acpica/acpivar.h>

#include <x86/init.h>
#include <machine/nexusvar.h>
#include <machine/intr_machdep.h>

#include <xen/xen-os.h>
#include <xen/xen_intr.h>
#include <xen/xen_msi.h>

#include "pcib_if.h"

/*
 * Xen nexus(4) driver.
 */
static int
nexus_xen_probe(device_t dev)
{

	if (!xen_pv_domain())
		return (ENXIO);

	return (BUS_PROBE_SPECIFIC);
}

static int
nexus_xen_attach(device_t dev)
{
	int error;
	device_t acpi_dev = NULL;

	nexus_init_resources();
	bus_generic_probe(dev);

	if (xen_initial_domain()) {
		/* Disable some ACPI devices that are not usable by Dom0 */
		acpi_cpu_disabled = true;
		acpi_hpet_disabled = true;
		acpi_timer_disabled = true;

		acpi_dev = BUS_ADD_CHILD(dev, 10, "acpi", 0);
		if (acpi_dev == NULL)
			panic("Unable to add ACPI bus to Xen Dom0");
	}

	error = bus_generic_attach(dev);
	if (xen_initial_domain() && (error == 0))
		acpi_install_wakeup_handler(device_get_softc(acpi_dev));

	return (error);
}

static int
nexus_xen_config_intr(device_t dev, int irq, enum intr_trigger trig,
    enum intr_polarity pol)
{
	int ret;

	/*
	 * ISA and PCI intline IRQs are not preregistered on Xen, so
	 * intercept calls to configure those and register them on the fly.
	 */
	if ((irq < first_msi_irq) && (intr_lookup_source(irq) == NULL)) {
		ret = xen_register_pirq(irq, trig, pol);
		if (ret != 0)
			return (ret);
		nexus_add_irq(irq);
	}
	return (intr_config_intr(irq, trig, pol));
}

static int
nexus_xen_alloc_msix(device_t pcib, device_t dev, int *irq)
{

	return (xen_msix_alloc(dev, irq));
}

static int
nexus_xen_release_msix(device_t pcib, device_t dev, int irq)
{

	return (xen_msix_release(irq));
}

static int
nexus_xen_alloc_msi(device_t pcib, device_t dev, int count, int maxcount, int *irqs)
{

	return (xen_msi_alloc(dev, count, maxcount, irqs));
}

static int
nexus_xen_release_msi(device_t pcib, device_t dev, int count, int *irqs)
{

	return (xen_msi_release(irqs, count));
}

static int
nexus_xen_map_msi(device_t pcib, device_t dev, int irq, uint64_t *addr, uint32_t *data)
{

	return (xen_msi_map(irq, addr, data));
}

static device_method_t nexus_xen_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		nexus_xen_probe),
	DEVMETHOD(device_attach,	nexus_xen_attach),

	/* INTR */
	DEVMETHOD(bus_config_intr,	nexus_xen_config_intr),

	/* MSI */
	DEVMETHOD(pcib_alloc_msi,	nexus_xen_alloc_msi),
	DEVMETHOD(pcib_release_msi,	nexus_xen_release_msi),
	DEVMETHOD(pcib_alloc_msix,	nexus_xen_alloc_msix),
	DEVMETHOD(pcib_release_msix,	nexus_xen_release_msix),
	DEVMETHOD(pcib_map_msi,		nexus_xen_map_msi),

	{ 0, 0 }
};

DEFINE_CLASS_1(nexus, nexus_xen_driver, nexus_xen_methods, 1, nexus_driver);
static devclass_t nexus_devclass;

DRIVER_MODULE(nexus_xen, root, nexus_xen_driver, nexus_devclass, 0, 0);
