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

#include <sys/pciio.h>
#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pci_private.h>

#include <xen/xen-os.h>
#include <xen/hypervisor.h>
#include <xen/xen_pci.h>

#include "pcib_if.h"
#include "pci_if.h"

void
xen_pci_enable_msi_method(device_t dev, device_t child, uint64_t address,
     uint16_t data)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	struct pcicfg_msi *msi = &dinfo->cfg.msi;

	/* Enable MSI in the control register. */
	msi->msi_ctrl |= PCIM_MSICTRL_MSI_ENABLE;
	pci_write_config(child, msi->msi_location + PCIR_MSI_CTRL,
	    msi->msi_ctrl, 2);
}

void
xen_pci_disable_msi_method(device_t dev, device_t child)
{
	struct pci_devinfo *dinfo = device_get_ivars(child);
	struct pcicfg_msi *msi = &dinfo->cfg.msi;

	msi->msi_ctrl &= ~PCIM_MSICTRL_MSI_ENABLE;
	pci_write_config(child, msi->msi_location + PCIR_MSI_CTRL,
	    msi->msi_ctrl, 2);
}

void
xen_pci_child_added_method(device_t dev, device_t child)
{
	struct pci_devinfo *dinfo;
	struct physdev_pci_device_add add_pci;
	int error;

	dinfo = device_get_ivars(child);
	KASSERT((dinfo != NULL),
	    ("xen_pci_add_child_method called with NULL dinfo"));

	bzero(&add_pci, sizeof(add_pci));
	add_pci.seg = dinfo->cfg.domain;
	add_pci.bus = dinfo->cfg.bus;
	add_pci.devfn = (dinfo->cfg.slot << 3) | dinfo->cfg.func;
	error = HYPERVISOR_physdev_op(PHYSDEVOP_pci_device_add, &add_pci);
	if (error)
		panic("unable to add device bus %u devfn %u error: %d\n",
		    add_pci.bus, add_pci.devfn, error);
}
