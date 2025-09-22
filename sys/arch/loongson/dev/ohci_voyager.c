/*	$OpenBSD: ohci_voyager.c,v 1.8 2022/04/06 18:59:26 naddy Exp $	*/
/*	OpenBSD: ohci_pci.c,v 1.33 2008/06/26 05:42:17 ray Exp 	*/
/*	$NetBSD: ohci_pci.c,v 1.23 2002/10/02 16:51:47 thorpej Exp $	*/

/*
 * Copyright (c) 1998 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Lennart Augustsson (lennart@augustsson.net) at
 * Carlstedt Research & Technology.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * USB Open Host Controller driver.
 *
 * OHCI spec: http://www.intel.com/design/usb/ohci11d.pdf
 * USB spec: http://www.teleport.com/cgi-bin/mailmerge.cgi/~usb/cgiform.tpl
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/device.h>
#include <sys/proc.h>
#include <sys/queue.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>

#include <loongson/dev/voyagerreg.h>
#include <loongson/dev/voyagervar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

#include <dev/usb/ohcireg.h>
#include <dev/usb/ohcivar.h>

extern int gdium_revision;

int	ohci_voyager_match(struct device *, void *, void *);
void	ohci_voyager_attach(struct device *, struct device *, void *);
void	ohci_voyager_attach_deferred(struct device *);

struct ohci_voyager_softc {
	struct ohci_softc	sc;
	void 			*sc_ih;
};

const struct cfattach ohci_voyager_ca = {
	sizeof(struct ohci_voyager_softc),
	ohci_voyager_match, ohci_voyager_attach, NULL, ohci_activate
};

int
ohci_voyager_match(struct device *parent, void *vcf, void *aux)
{
	struct voyager_attach_args *vaa = (struct voyager_attach_args *)aux;
	struct cfdata *cf = (struct cfdata *)vcf;

	return gdium_revision == 0 &&
	    strcmp(vaa->vaa_name, cf->cf_driver->cd_name) == 0;
}

void
ohci_voyager_attach(struct device *parent, struct device *self, void *aux)
{
	struct ohci_voyager_softc *sc = (struct ohci_voyager_softc *)self;
	struct voyager_attach_args *vaa = (struct voyager_attach_args *)aux;
	struct pci_attach_args *pa = vaa->vaa_pa;
	int s;
	const char *vendor;
	char *devname = sc->sc.sc_bus.bdev.dv_xname;

	/* Map I/O registers */
	sc->sc.sc_size = VOYAGER_OHCI_SIZE;
	sc->sc.iot = vaa->vaa_mmiot;
	if (bus_space_subregion(vaa->vaa_mmiot, vaa->vaa_mmioh,
	    VOYAGER_OHCI_BASE, VOYAGER_OHCI_SIZE, &sc->sc.ioh) != 0) {
		printf(": can't map mem space\n");
		return;
	}

	/* Record what interrupts were enabled by SMM/BIOS. */
	sc->sc.sc_intre = bus_space_read_4(sc->sc.iot, sc->sc.ioh,
	    OHCI_INTERRUPT_ENABLE);

	/* Disable interrupts, so we don't get any spurious ones. */
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, OHCI_INTERRUPT_DISABLE,
	    OHCI_MIE);

	sc->sc.sc_bus.dmatag = pa->pa_dmat;

	bus_space_barrier(sc->sc.iot, sc->sc.ioh, 0, sc->sc.sc_size,
	    BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE);
	bus_space_write_4(sc->sc.iot, sc->sc.ioh,
	    OHCI_INTERRUPT_DISABLE, OHCI_MIE);

	s = splusb();
	/* establish the interrupt. */
	sc->sc_ih = voyager_intr_establish(parent, VOYAGER_INTR_USB_HOST,
	    IPL_USB, ohci_intr, sc, devname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt\n");
		splx(s);
		return;
	}
	printf(": %s, ", voyager_intr_string(sc->sc_ih));

	/* Figure out vendor for root hub descriptor. */
	vendor = pci_findvendor(pa->pa_id);
	sc->sc.sc_id_vendor = PCI_VENDOR(pa->pa_id);
	if (vendor)
		strlcpy(sc->sc.sc_vendor, vendor, sizeof (sc->sc.sc_vendor));
	else
		snprintf(sc->sc.sc_vendor, sizeof (sc->sc.sc_vendor),
		    "vendor 0x%04x", PCI_VENDOR(pa->pa_id));

	/* Display revision and perform legacy emulation handover. */
	if (ohci_checkrev(&sc->sc) != USBD_NORMAL_COMPLETION ||
	    ohci_handover(&sc->sc) != USBD_NORMAL_COMPLETION) {
		splx(s);
		return;
	}

	/* Ignore interrupts for now */
	sc->sc.sc_bus.dying = 1;

	config_defer(self, ohci_voyager_attach_deferred);

	splx(s);
}

void
ohci_voyager_attach_deferred(struct device *self)
{
	struct ohci_voyager_softc *sc = (struct ohci_voyager_softc *)self;
	usbd_status r;
	int s;

	s = splusb();

	sc->sc.sc_bus.dying = 0;
	
	r = ohci_init(&sc->sc);
	if (r != USBD_NORMAL_COMPLETION) {
		printf("%s: init failed, error=%d\n",
		    sc->sc.sc_bus.bdev.dv_xname, r);
		bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
		splx(s);
		return;
	}

	splx(s);

	/* Attach usb device. */
	config_found(self, &sc->sc.sc_bus, usbctlprint);
}
