/*	$NetBSD: puc.c,v 1.7 2000/07/29 17:43:38 jlam Exp $	*/

/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-3-Clause
 *
 * Copyright (c) 2002 JF Hay.  All rights reserved.
 * Copyright (c) 2000 M. Warner Losh.
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

/*-
 * Copyright (c) 1996, 1998, 1999
 *	Christopher G. Demetriou.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by Christopher G. Demetriou
 *	for the NetBSD Project.
 * 4. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission
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
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/bus.h>
#include <sys/conf.h>
#include <sys/malloc.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/puc/puc_cfg.h>
#include <dev/puc/puc_bfe.h>
#include <dev/puc/pucdata.c>

static int puc_msi_disable;
SYSCTL_INT(_hw_puc, OID_AUTO, msi_disable, CTLFLAG_RDTUN,
    &puc_msi_disable, 0, "Disable use of MSI interrupts by puc(9)");

static const struct puc_cfg *
puc_pci_match(device_t dev, const struct puc_cfg *desc)
{
	uint16_t vendor, device;
	uint16_t subvendor, subdevice;

	vendor = pci_get_vendor(dev);
	device = pci_get_device(dev);
	subvendor = pci_get_subvendor(dev);
	subdevice = pci_get_subdevice(dev);

	while (desc->vendor != 0xffff) {
		if (desc->vendor == vendor && desc->device == device) {
			/* exact match */
			if (desc->subvendor == subvendor &&
		            desc->subdevice == subdevice)
				return (desc);
			/* wildcard match */
			if (desc->subvendor == 0xffff)
				return (desc);
		}
		desc++;
	}

	/* no match */
	return (NULL);
}

static int
puc_pci_probe(device_t dev)
{
	const struct puc_cfg *desc;

	if ((pci_read_config(dev, PCIR_HDRTYPE, 1) & PCIM_HDRTYPE) != 0)
		return (ENXIO);

	desc = puc_pci_match(dev, puc_pci_devices);
	if (desc == NULL)
		return (ENXIO);
	return (puc_bfe_probe(dev, desc));
}

static int
puc_pci_attach(device_t dev)
{
	struct puc_softc *sc;
	int error, count;

	sc = device_get_softc(dev);

	if (!puc_msi_disable) {
		count = 1;

		if (pci_alloc_msi(dev, &count) == 0) {
			sc->sc_msi = 1;
			sc->sc_irid = 1;
		}
	}

	error = puc_bfe_attach(dev);

	if (error != 0 && sc->sc_msi)
		pci_release_msi(dev);

	return (error);
}

static int
puc_pci_detach(device_t dev)
{
	struct puc_softc *sc;
	int error;

	sc = device_get_softc(dev);
	
	error = puc_bfe_detach(dev);

	if (error != 0)
		return (error);

	if (sc->sc_msi)
		error = pci_release_msi(dev);

	return (error);
}


static device_method_t puc_pci_methods[] = {
    /* Device interface */
    DEVMETHOD(device_probe,		puc_pci_probe),
    DEVMETHOD(device_attach,		puc_pci_attach),
    DEVMETHOD(device_detach,		puc_pci_detach),

    DEVMETHOD(bus_alloc_resource,	puc_bus_alloc_resource),
    DEVMETHOD(bus_release_resource,	puc_bus_release_resource),
    DEVMETHOD(bus_get_resource,		puc_bus_get_resource),
    DEVMETHOD(bus_read_ivar,		puc_bus_read_ivar),
    DEVMETHOD(bus_setup_intr,		puc_bus_setup_intr),
    DEVMETHOD(bus_teardown_intr,	puc_bus_teardown_intr),
    DEVMETHOD(bus_print_child,		puc_bus_print_child),
    DEVMETHOD(bus_child_pnpinfo_str,	puc_bus_child_pnpinfo_str),
    DEVMETHOD(bus_child_location_str,	puc_bus_child_location_str),

    DEVMETHOD_END
};

static driver_t puc_pci_driver = {
	puc_driver_name,
	puc_pci_methods,
	sizeof(struct puc_softc),
};

DRIVER_MODULE(puc, pci, puc_pci_driver, puc_devclass, 0, 0);
MODULE_PNP_INFO("U16:vendor;U16:device;U16:#;U16:#;D:#", pci, puc,
    puc_pci_devices, nitems(puc_pci_devices) - 1);
