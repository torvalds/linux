/*
 * Copyright (c) 2014 Roger Pau Monné <roger.pau@citrix.com>
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
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/module.h>

#include <sys/pciio.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pci_private.h>

#include <xen/xen-os.h>
#include <xen/hypervisor.h>
#include <xen/xen_pci.h>

#include "pcib_if.h"
#include "pci_if.h"

static int
xen_pci_probe(device_t dev)
{

	if (!xen_pv_domain())
		return (ENXIO);

	device_set_desc(dev, "Xen PCI bus");

	return (BUS_PROBE_DEFAULT);
}

static device_method_t xen_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		xen_pci_probe),

	/* PCI interface overwrites */
	DEVMETHOD(pci_enable_msi,	xen_pci_enable_msi_method),
	DEVMETHOD(pci_disable_msi,	xen_pci_disable_msi_method),
	DEVMETHOD(pci_child_added,	xen_pci_child_added_method),

	DEVMETHOD_END
};

static devclass_t pci_devclass;

DEFINE_CLASS_1(pci, xen_pci_driver, xen_pci_methods, sizeof(struct pci_softc),
    pci_driver);
DRIVER_MODULE(xen_pci, pcib, xen_pci_driver, pci_devclass, 0, 0);
MODULE_DEPEND(xen_pci, pci, 1, 1, 1);
MODULE_VERSION(xen_pci, 1);
