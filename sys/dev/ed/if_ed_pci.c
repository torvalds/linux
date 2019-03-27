/*-
 * SPDX-License-Identifier: BSD-4-Clause
 *
 * Copyright (c) 1996 Stefan Esser <se@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice immediately at the beginning of the file, without modification,
 *    this list of conditions, and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Absolutely no warranty of function or purpose is made by the author
 *    Stefan Esser.
 * 4. Modifications may be freely made to this file if the above conditions
 *    are met.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/kernel.h>

#include <sys/module.h>
#include <sys/bus.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <machine/resource.h>

#include <net/if.h>
#include <net/if_arp.h>
#include <net/if_media.h>
#include <net/if_mib.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>

#include <dev/ed/if_edvar.h>
#include <dev/ed/rtl80x9reg.h>

static struct _pcsid
{
	uint32_t	type;
	const char	*desc;
} pci_ids[] =
{
	{ 0x140111f6, "Compex RL2000" },
	{ 0x005812c3, "Holtek HT80232" },
	{ 0x30008e2e, "KTI ET32P2" },
	{ 0x50004a14, "NetVin NV5000SC" },
	{ 0x09401050, "ProLAN" },
	{ ED_RTL8029_PCI_ID, "RealTek 8029" }, /* Needs realtek full duplex */
	{ 0x0e3410bd, "Surecom NE-34" },
	{ 0x09261106, "VIA VT86C926" },
	{ 0x19808c4a, "Winbond W89C940" },
	{ 0x5a5a1050, "Winbond W89C940F" },
#if 0
	/* some Holtek needs special lovin', disabled by default */
	/* The Holtek can report/do full duplex, but that's unimplemented */
	{ 0x559812c3, "Holtek HT80229" },	/* Only 32-bit I/O, Holtek fdx, STOP_PG_60? */
#endif
	{ 0x00000000, NULL }
};

static int	ed_pci_probe(device_t);
static int	ed_pci_attach(device_t);

static int
ed_pci_probe(device_t dev)
{
	uint32_t	type = pci_get_devid(dev);
	struct _pcsid	*ep =pci_ids;

	while (ep->type && ep->type != type)
		++ep;
	if (ep->desc == NULL)
		return (ENXIO);
	device_set_desc(dev, ep->desc);
	return (BUS_PROBE_DEFAULT);
}

static int
ed_pci_attach(device_t dev)
{
	struct	ed_softc *sc = device_get_softc(dev);
	int	error = ENXIO;

	/*
	 * Probe RTL8029 cards, but allow failure and try as a generic
	 * ne-2000.  QEMU 0.9 and earlier use the RTL8029 PCI ID, but
	 * are areally just generic ne-2000 cards.
	 */
	if (pci_get_devid(dev) == ED_RTL8029_PCI_ID)
		error = ed_probe_RTL80x9(dev, PCIR_BAR(0), 0);
	if (error)
		error = ed_probe_Novell(dev, PCIR_BAR(0),
		    ED_FLAGS_FORCE_16BIT_MODE);
	if (error) {
		ed_release_resources(dev);
		return (error);
	}
	ed_Novell_read_mac(sc);

	error = ed_alloc_irq(dev, 0, RF_SHAREABLE);
	if (error) {
		ed_release_resources(dev);
		return (error);
	}
	if (sc->sc_media_ioctl == NULL)
		ed_gen_ifmedia_init(sc);
	error = ed_attach(dev);
	if (error) {
		ed_release_resources(dev);
		return (error);
	}
	error = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_NET | INTR_MPSAFE,
	    NULL, edintr, sc, &sc->irq_handle);
	if (error)
		ed_release_resources(dev);
	return (error);
}

static device_method_t ed_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		ed_pci_probe),
	DEVMETHOD(device_attach,	ed_pci_attach),
	DEVMETHOD(device_detach,	ed_detach),

	{ 0, 0 }
};

static driver_t ed_pci_driver = {
	"ed",
	ed_pci_methods,
	sizeof(struct ed_softc),
};

DRIVER_MODULE(ed, pci, ed_pci_driver, ed_devclass, 0, 0);
MODULE_DEPEND(ed, pci, 1, 1, 1);
MODULE_DEPEND(ed, ether, 1, 1, 1);
MODULE_PNP_INFO("W32:vendor/device;D:#", pci, ed, pci_ids,
    nitems(pci_ids) - 1);
