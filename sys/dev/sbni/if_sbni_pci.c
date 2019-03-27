/*-
 * Copyright (c) 1997-2001 Granch, Ltd. All rights reserved.
 * Author: Denis I.Timofeev <timofeev@granch.ru>
 *
 * Redistributon and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
 * LIABILITY, OR TORT (INCLUDING NEIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/socket.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <machine/bus.h>
#include <machine/resource.h>
#include <sys/rman.h>
#include <sys/malloc.h>

#include <net/if.h>
#include <net/ethernet.h>
#include <net/if_arp.h>

#include <dev/pci/pcivar.h>
#include <dev/pci/pcireg.h>

#include <dev/sbni/if_sbnireg.h>
#include <dev/sbni/if_sbnivar.h>

static int	sbni_pci_probe(device_t);
static int	sbni_pci_attach(device_t);
static int	sbni_pci_detach(device_t);

static device_method_t sbni_pci_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,	sbni_pci_probe),
	DEVMETHOD(device_attach, sbni_pci_attach),
	DEVMETHOD(device_detach, sbni_pci_detach),
	{ 0, 0 }
};

static driver_t sbni_pci_driver = {
	"sbni",
	sbni_pci_methods,
	sizeof(struct sbni_softc)
};

static devclass_t sbni_pci_devclass;

DRIVER_MODULE(sbni, pci, sbni_pci_driver, sbni_pci_devclass, 0, 0);
MODULE_DEPEND(sbni, pci, 1, 1, 1);

static int
sbni_pci_probe(device_t dev)
{
	struct sbni_softc  *sc;
	u_int32_t  ports;
 
	ports = SBNI_PORTS;
	if (pci_get_vendor(dev) != SBNI_PCI_VENDOR ||
	    pci_get_device(dev) != SBNI_PCI_DEVICE)
		return (ENXIO);

	sc = device_get_softc(dev);
	if (pci_get_subdevice(dev) == 2) {
		ports <<= 1;
		sc->slave_sc = malloc(sizeof(struct sbni_softc),
				      M_DEVBUF, M_NOWAIT | M_ZERO);
		if (!sc->slave_sc)
			return (ENOMEM);
		device_set_desc(dev, "Granch SBNI12/PCI Dual adapter");
	} else
		device_set_desc(dev, "Granch SBNI12/PCI adapter");

	sc->io_rid = PCIR_BAR(0);
 	sc->io_res = bus_alloc_resource_any(dev, SYS_RES_IOPORT,
					    &sc->io_rid, RF_ACTIVE);
	if (!sc->io_res) {
		device_printf(dev, "cannot allocate io ports!\n");
		if (sc->slave_sc)
			free(sc->slave_sc, M_DEVBUF);
		return (ENOENT);
	}

	if (sc->slave_sc) {
		sc->slave_sc->io_res = sc->io_res;
		sc->slave_sc->io_off = 4;
	}
	if (sbni_probe(sc) != 0) {
		sbni_release_resources(sc);
		if (sc->slave_sc)
			free(sc->slave_sc, M_DEVBUF);
		return (ENXIO);
	}

	return (0);
}

static int
sbni_pci_attach(device_t dev)
{
	struct sbni_softc *sc;
	struct sbni_flags flags;
	int error;

	sc = device_get_softc(dev);
	sc->dev = dev;

	sc->irq_res = bus_alloc_resource_any(dev, SYS_RES_IRQ, &sc->irq_rid,
					     RF_SHAREABLE);

	if (sc->irq_res == NULL) {
		device_printf(dev, "cannot claim irq!\n");
		error = ENOENT;
		goto attach_failed;
	}

	*(u_int32_t*)&flags = 0;

	error = sbni_attach(sc, device_get_unit(dev) * 2, flags);
	if (error) {
		device_printf(dev, "cannot initialize driver\n");
		goto attach_failed;
	}
	if (sc->slave_sc) {
		error = sbni_attach(sc->slave_sc, device_get_unit(dev) * 2 + 1,
		    flags);
		if (error) {
			device_printf(dev, "cannot initialize slave\n");
			sbni_detach(sc);
			goto attach_failed;
		}
	}

	if (sc->irq_res) {
		error = bus_setup_intr(dev, sc->irq_res, INTR_TYPE_NET |
		    INTR_MPSAFE, NULL, sbni_intr, sc, &sc->irq_handle);
		if (error) {
			device_printf(dev, "bus_setup_intr\n");
			sbni_detach(sc);
			if (sc->slave_sc)
				sbni_detach(sc);
			goto attach_failed;
		}
	}
	return (0);

attach_failed:
	sbni_release_resources(sc);
	if (sc->slave_sc)
		free(sc->slave_sc, M_DEVBUF);
	return (error);
}

static int
sbni_pci_detach(device_t dev)
{
	struct sbni_softc *sc;

	sc = device_get_softc(dev);
	sbni_detach(sc);
	if (sc->slave_sc)
		sbni_detach(sc);
	
	sbni_release_resources(sc);
	if (sc->slave_sc)
		free(sc->slave_sc, M_DEVBUF);
	return (0);
}
