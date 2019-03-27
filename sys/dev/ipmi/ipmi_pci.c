/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2006 IronPort Systems Inc. <ambrisko@ironport.com>
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
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/condvar.h>
#include <sys/eventhandler.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/rman.h>
#include <sys/selinfo.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#ifdef LOCAL_MODULE
#include <ipmivars.h>
#else
#include <dev/ipmi/ipmivars.h>
#endif

static int ipmi_pci_probe(device_t dev);
static int ipmi_pci_attach(device_t dev);

static struct ipmi_ident
{
	u_int16_t	vendor;
	u_int16_t	device;
	char		*desc;
} ipmi_identifiers[] = {
	{0x1028, 0x000d, "Dell PE2650 SMIC interface"},
	{0, 0, 0}
};

const char *
ipmi_pci_match(uint16_t vendor, uint16_t device)
{
	struct ipmi_ident *m;

	for (m = ipmi_identifiers; m->vendor != 0; m++)
		if (m->vendor == vendor && m->device == device)
			return (m->desc);
	return (NULL);
}

static int
ipmi_pci_probe(device_t dev)
{
	const char *desc;

	if (ipmi_attached)
		return (ENXIO);

	desc = ipmi_pci_match(pci_get_vendor(dev), pci_get_device(dev));
	if (desc != NULL) {
		device_set_desc(dev, desc);
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
ipmi_pci_attach(device_t dev)
{
	struct ipmi_softc *sc = device_get_softc(dev);
	struct ipmi_get_info info;
	const char *mode;
	int error, type;

	/* Look for an IPMI entry in the SMBIOS table. */
	if (!ipmi_smbios_identify(&info))
		return (ENXIO);

	sc->ipmi_dev = dev;

	switch (info.iface_type) {
	case KCS_MODE:
		mode = "KCS";
		break;
	case SMIC_MODE:
		mode = "SMIC";
		break;
	case BT_MODE:
		device_printf(dev, "BT mode is unsupported\n");
		return (ENXIO);
	default:
		device_printf(dev, "No IPMI interface found\n");
		return (ENXIO);
	}

	device_printf(dev, "%s mode found at %s 0x%jx alignment 0x%x on %s\n",
	    mode, info.io_mode ? "io" : "mem",
	    (uintmax_t)info.address, info.offset,
	    device_get_name(device_get_parent(dev)));
	if (info.io_mode)
		type = SYS_RES_IOPORT;
	else
		type = SYS_RES_MEMORY;

	sc->ipmi_io_rid = PCIR_BAR(0);
	sc->ipmi_io_res[0] = bus_alloc_resource_any(dev, type,
	    &sc->ipmi_io_rid, RF_ACTIVE);
	sc->ipmi_io_type = type;
	sc->ipmi_io_spacing = info.offset;

	if (sc->ipmi_io_res[0] == NULL) {
		device_printf(dev, "couldn't configure pci io res\n");
		return (ENXIO);
	}

	sc->ipmi_irq_rid = 0;
	sc->ipmi_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->ipmi_irq_rid, RF_SHAREABLE | RF_ACTIVE);

	switch (info.iface_type) {
	case KCS_MODE:
		error = ipmi_kcs_attach(sc);
		if (error)
			goto bad;
		break;
	case SMIC_MODE:
		error = ipmi_smic_attach(sc);
		if (error)
			goto bad;
		break;
	}
	error = ipmi_attach(dev);
	if (error)
		goto bad;

	return (0);
bad:
	ipmi_release_resources(dev);
	return (error);
}

static device_method_t ipmi_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,     ipmi_pci_probe),
	DEVMETHOD(device_attach,    ipmi_pci_attach),
	DEVMETHOD(device_detach,    ipmi_detach),
	{ 0, 0 }
};

static driver_t ipmi_pci_driver = {
	"ipmi",
	ipmi_methods,
	sizeof(struct ipmi_softc)
};

DRIVER_MODULE(ipmi_pci, pci, ipmi_pci_driver, ipmi_devclass, 0, 0);

/* Native IPMI on PCI driver. */

static int
ipmi2_pci_probe(device_t dev)
{

	if (pci_get_class(dev) == PCIC_SERIALBUS &&
	    pci_get_subclass(dev) == PCIS_SERIALBUS_IPMI) {
		device_set_desc(dev, "IPMI System Interface");
		return (BUS_PROBE_GENERIC);
	}

	return (ENXIO);
}

static int
ipmi2_pci_attach(device_t dev)
{
	struct ipmi_softc *sc;
	int error, iface, type;

	sc = device_get_softc(dev);
	sc->ipmi_dev = dev;

	/* Interface is determined by progif. */
	switch (pci_get_progif(dev)) {
	case PCIP_SERIALBUS_IPMI_SMIC:
		iface = SMIC_MODE;
		break;
	case PCIP_SERIALBUS_IPMI_KCS:
		iface = KCS_MODE;
		break;
	case PCIP_SERIALBUS_IPMI_BT:
		iface = BT_MODE;
		device_printf(dev, "BT interface unsupported\n");
		return (ENXIO);
	default:
		device_printf(dev, "Unsupported interface: %d\n",
		    pci_get_progif(dev));
		return (ENXIO);
	}

	/* Check the BAR to determine our resource type. */
	sc->ipmi_io_rid = PCIR_BAR(0);
	if (PCI_BAR_IO(pci_read_config(dev, PCIR_BAR(0), 4)))
		type = SYS_RES_IOPORT;
	else
		type = SYS_RES_MEMORY;
	sc->ipmi_io_type = type;
	sc->ipmi_io_spacing = 1;
	sc->ipmi_io_res[0] = bus_alloc_resource_any(dev, type,
	    &sc->ipmi_io_rid, RF_ACTIVE);
	if (sc->ipmi_io_res[0] == NULL) {
		device_printf(dev, "couldn't map ports/memory\n");
		return (ENXIO);
	}

	sc->ipmi_irq_rid = 0;
	sc->ipmi_irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ,
	    &sc->ipmi_irq_rid, RF_SHAREABLE | RF_ACTIVE);

	switch (iface) {
	case KCS_MODE:
		device_printf(dev, "using KSC interface\n");

		/*
		 * We have to examine the resource directly to determine the
		 * alignment.
		 */
		if (!ipmi_kcs_probe_align(sc)) {
			device_printf(dev, "Unable to determine alignment\n");
			error = ENXIO;
			goto bad;
		}

		error = ipmi_kcs_attach(sc);
		if (error)
			goto bad;
		break;
	case SMIC_MODE:
		device_printf(dev, "using SMIC interface\n");

		error = ipmi_smic_attach(sc);
		if (error)
			goto bad;
		break;
	}
	error = ipmi_attach(dev);
	if (error)
		goto bad;

	return (0);
bad:
	ipmi_release_resources(dev);
	return (error);
}

static device_method_t ipmi2_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,     ipmi2_pci_probe),
	DEVMETHOD(device_attach,    ipmi2_pci_attach),
	DEVMETHOD(device_detach,    ipmi_detach),
	{ 0, 0 }
};

static driver_t ipmi2_pci_driver = {
	"ipmi",
	ipmi2_methods,
	sizeof(struct ipmi_softc)
};

DRIVER_MODULE(ipmi2_pci, pci, ipmi2_pci_driver, ipmi_devclass, 0, 0);
