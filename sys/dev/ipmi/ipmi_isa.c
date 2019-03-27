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

#include <machine/pci_cfgreg.h>
#include <dev/pci/pcireg.h>

#include <isa/isavar.h>

#ifdef LOCAL_MODULE
#include <ipmi.h>
#include <ipmivars.h>
#else
#include <sys/ipmi.h>
#include <dev/ipmi/ipmivars.h>
#endif

static void
ipmi_isa_identify(driver_t *driver, device_t parent)
{
	struct ipmi_get_info info;
	uint32_t devid;

	if (ipmi_smbios_identify(&info) && info.iface_type != SSIF_MODE &&
	    device_find_child(parent, "ipmi", -1) == NULL) {
		/*
		 * XXX: Hack alert.  On some broken systems, the IPMI
		 * interface is described via SMBIOS, but the actual
		 * IO resource is in a PCI device BAR, so we have to let
		 * the PCI device attach ipmi instead.  In that case don't
		 * create an isa ipmi device.  For now we hardcode the list
		 * of bus, device, function tuples.
		 */
		devid = pci_cfgregread(0, 4, 2, PCIR_DEVVENDOR, 4);
		if (devid != 0xffffffff &&
		    ipmi_pci_match(devid & 0xffff, devid >> 16) != NULL)
			return;
		BUS_ADD_CHILD(parent, 0, "ipmi", -1);
	}
}

static int
ipmi_isa_probe(device_t dev)
{

	/*
	 * Give other drivers precedence.  Unfortunately, this doesn't
	 * work if we have an SMBIOS table that duplicates a PCI device
	 * that's later on the bus than the PCI-ISA bridge.
	 */
	if (ipmi_attached)
		return (ENXIO);

	/* Skip any PNP devices. */
	if (isa_get_logicalid(dev) != 0)
		return (ENXIO);

	device_set_desc(dev, "IPMI System Interface");
	return (BUS_PROBE_DEFAULT);
}

static int
ipmi_hint_identify(device_t dev, struct ipmi_get_info *info)
{
	const char *mode, *name;
	int i, unit, val;

	/* We require at least a "mode" hint. */
	name = device_get_name(dev);
	unit = device_get_unit(dev);
	if (resource_string_value(name, unit, "mode", &mode) != 0)
		return (0);

	/* Set the mode and default I/O resources for each mode. */
	bzero(info, sizeof(struct ipmi_get_info));
	if (strcasecmp(mode, "KCS") == 0) {
		info->iface_type = KCS_MODE;
		info->address = 0xca2;
		info->io_mode = 1;
		info->offset = 1;
	} else if (strcasecmp(mode, "SMIC") == 0) {
		info->iface_type = SMIC_MODE;
		info->address = 0xca9;
		info->io_mode = 1;
		info->offset = 1;
	} else if (strcasecmp(mode, "BT") == 0) {
		info->iface_type = BT_MODE;
		info->address = 0xe4;
		info->io_mode = 1;
		info->offset = 1;
	} else {
		device_printf(dev, "Invalid mode %s\n", mode);
		return (0);
	}

	/*
	 * Kill any resources that isahint.c might have setup for us
	 * since it will conflict with how we do resources.
	 */
	for (i = 0; i < 2; i++) {
		bus_delete_resource(dev, SYS_RES_MEMORY, i);
		bus_delete_resource(dev, SYS_RES_IOPORT, i);
	}

	/* Allow the I/O address to be overriden via hints. */
	if (resource_int_value(name, unit, "port", &val) == 0 && val != 0) {
		info->address = val;
		info->io_mode = 1;
	} else if (resource_int_value(name, unit, "maddr", &val) == 0 &&
	    val != 0) {
		info->address = val;
		info->io_mode = 0;
	}

	/* Allow the spacing to be overriden. */
	if (resource_int_value(name, unit, "spacing", &val) == 0) {
		switch (val) {
		case 8:
			info->offset = 1;
			break;
		case 16:
			info->offset = 2;
			break;
		case 32:
			info->offset = 4;
			break;
		default:
			device_printf(dev, "Invalid register spacing\n");
			return (0);
		}
	}
	return (1);
}

static int
ipmi_isa_attach(device_t dev)
{
	struct ipmi_softc *sc = device_get_softc(dev);
	struct ipmi_get_info info;
	const char *mode;
	int count, error, i, type;

	/*
	 * Pull info out of the SMBIOS table.  If that doesn't work, use
	 * hints to enumerate a device.
	 */
	if (!ipmi_smbios_identify(&info) &&
	    !ipmi_hint_identify(dev, &info))
		return (ENXIO);

	switch (info.iface_type) {
	case KCS_MODE:
		count = 2;
		mode = "KCS";
		break;
	case SMIC_MODE:
		count = 3;
		mode = "SMIC";
		break;
	case BT_MODE:
		device_printf(dev, "BT mode is unsupported\n");
		return (ENXIO);
	default:
		return (ENXIO);
	}
	error = 0;
	sc->ipmi_dev = dev;

	device_printf(dev, "%s mode found at %s 0x%jx alignment 0x%x on %s\n",
	    mode, info.io_mode ? "io" : "mem",
	    (uintmax_t)info.address, info.offset,
	    device_get_name(device_get_parent(dev)));
	if (info.io_mode)
		type = SYS_RES_IOPORT;
	else
		type = SYS_RES_MEMORY;

	sc->ipmi_io_type = type;
	sc->ipmi_io_spacing = info.offset;
	if (info.offset == 1) {
		sc->ipmi_io_rid = 0;
		sc->ipmi_io_res[0] = bus_alloc_resource(dev, type,
		    &sc->ipmi_io_rid, info.address, info.address + count - 1,
		    count, RF_ACTIVE);
		if (sc->ipmi_io_res[0] == NULL) {
			device_printf(dev, "couldn't configure I/O resource\n");
			return (ENXIO);
		}
	} else {
		for (i = 0; i < count; i++) {
			sc->ipmi_io_rid = i;
			sc->ipmi_io_res[i] = bus_alloc_resource(dev, type,
			    &sc->ipmi_io_rid, info.address + i * info.offset,
			    info.address + i * info.offset, 1, RF_ACTIVE);
			if (sc->ipmi_io_res[i] == NULL) {
				device_printf(dev,
				    "couldn't configure I/O resource\n");
				error = ENXIO;
				sc->ipmi_io_rid = 0;
				goto bad;
			}
		}
		sc->ipmi_io_rid = 0;
	}

	if (info.irq != 0) {
		sc->ipmi_irq_rid = 0;
		sc->ipmi_irq_res = bus_alloc_resource(dev, SYS_RES_IRQ,
		    &sc->ipmi_irq_rid, info.irq, info.irq, 1,
		    RF_SHAREABLE | RF_ACTIVE);
	}

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
	DEVMETHOD(device_identify,      ipmi_isa_identify),
	DEVMETHOD(device_probe,		ipmi_isa_probe),
	DEVMETHOD(device_attach,	ipmi_isa_attach),
	DEVMETHOD(device_detach,	ipmi_detach),
	{ 0, 0 }
};

static driver_t ipmi_isa_driver = {
	"ipmi",
	ipmi_methods,
	sizeof(struct ipmi_softc),
};

DRIVER_MODULE(ipmi_isa, isa, ipmi_isa_driver, ipmi_devclass, 0, 0);
