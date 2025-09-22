/*	$OpenBSD: uhci_pci.c,v 1.36 2024/05/24 06:02:58 jsg Exp $	*/
/*	$NetBSD: uhci_pci.c,v 1.24 2002/10/02 16:51:58 thorpej Exp $	*/

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

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/timeout.h>
#include <sys/queue.h>

#include <machine/bus.h>

#include <dev/pci/pcivar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>

#include <dev/usb/uhcireg.h>
#include <dev/usb/uhcivar.h>

int	uhci_pci_match(struct device *, void *, void *);
void	uhci_pci_attach(struct device *, struct device *, void *);
void	uhci_pci_attach_deferred(struct device *);
int	uhci_pci_detach(struct device *, int);
int	uhci_pci_activate(struct device *, int);

struct uhci_pci_softc {
	struct uhci_softc	sc;
	pci_chipset_tag_t	sc_pc;
	pcitag_t		sc_tag;
	void 			*sc_ih;		/* interrupt vectoring */
};

const struct cfattach uhci_pci_ca = {
	sizeof(struct uhci_pci_softc), uhci_pci_match, uhci_pci_attach,
	uhci_pci_detach, uhci_pci_activate
};

int
uhci_pci_match(struct device *parent, void *match, void *aux)
{
	struct pci_attach_args *pa = (struct pci_attach_args *) aux;

	if (PCI_CLASS(pa->pa_class) == PCI_CLASS_SERIALBUS &&
	    PCI_SUBCLASS(pa->pa_class) == PCI_SUBCLASS_SERIALBUS_USB &&
	    PCI_INTERFACE(pa->pa_class) == PCI_INTERFACE_UHCI)
		return (1);
 
	return (0);
}

int
uhci_pci_activate(struct device *self, int act)
{
	struct uhci_pci_softc *sc = (struct uhci_pci_softc *)self;

	if (sc->sc.sc_size == 0)
		return 0;

	/* On resume, set legacy support attribute and enable intrs */
	switch (act) {
	case DVACT_RESUME:
		pci_conf_write(sc->sc_pc, sc->sc_tag,
		    PCI_LEGSUP, PCI_LEGSUP_USBPIRQDEN);
		bus_space_barrier(sc->sc.iot, sc->sc.ioh, 0, sc->sc.sc_size,
		    BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE);
		bus_space_write_2(sc->sc.iot, sc->sc.ioh, UHCI_INTR, 0);
		break;
	}

	return uhci_activate(self, act);
}

void
uhci_pci_attach(struct device *parent, struct device *self, void *aux)
{
	struct uhci_pci_softc *sc = (struct uhci_pci_softc *)self;
	struct pci_attach_args *pa = (struct pci_attach_args *)aux;
	pci_chipset_tag_t pc = pa->pa_pc;
	pcitag_t tag = pa->pa_tag;
	char const *intrstr;
	pci_intr_handle_t ih;
	const char *vendor;
	char *devname = sc->sc.sc_bus.bdev.dv_xname;
	int s;

	/* Map I/O registers */
	if (pci_mapreg_map(pa, PCI_CBIO, PCI_MAPREG_TYPE_IO, 0,
		    &sc->sc.iot, &sc->sc.ioh, NULL, &sc->sc.sc_size, 0)) {
		printf(": can't map i/o space\n");
		return;
	}


	/* Disable interrupts, so we don't get any spurious ones. */
	s = splhardusb();
	bus_space_write_2(sc->sc.iot, sc->sc.ioh, UHCI_INTR, 0);

	sc->sc_pc = pc;
	sc->sc_tag = tag;
	sc->sc.sc_bus.dmatag = pa->pa_dmat;

	/* Map and establish the interrupt. */
	if (pci_intr_map(pa, &ih)) {
		printf(": couldn't map interrupt\n");
		goto unmap_ret;
	}
	intrstr = pci_intr_string(pc, ih);
	sc->sc_ih = pci_intr_establish(pc, ih, IPL_USB, uhci_intr, sc,
				       devname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt");
		if (intrstr != NULL)
			printf(" at %s", intrstr);
		printf("\n");
		goto unmap_ret;
	}
	printf(": %s\n", intrstr);

	/* Set LEGSUP register to its default value. */
	pci_conf_write(pc, tag, PCI_LEGSUP, PCI_LEGSUP_USBPIRQDEN);

	switch(pci_conf_read(pc, tag, PCI_USBREV) & PCI_USBREV_MASK) {
	case PCI_USBREV_PRE_1_0:
		sc->sc.sc_bus.usbrev = USBREV_PRE_1_0;
		break;
	case PCI_USBREV_1_0:
		sc->sc.sc_bus.usbrev = USBREV_1_0;
		break;
	case PCI_USBREV_1_1:
		sc->sc.sc_bus.usbrev = USBREV_1_1;
		break;
	default:
		sc->sc.sc_bus.usbrev = USBREV_UNKNOWN;
		break;
	}

	uhci_run(&sc->sc, 0);			/* stop the controller */
						/* disable interrupts */
	bus_space_barrier(sc->sc.iot, sc->sc.ioh, 0, sc->sc.sc_size,
	    BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE);
	bus_space_write_2(sc->sc.iot, sc->sc.ioh, UHCI_INTR, 0);

	/* Figure out vendor for root hub descriptor. */
	vendor = pci_findvendor(pa->pa_id);
	sc->sc.sc_id_vendor = PCI_VENDOR(pa->pa_id);
	if (vendor)
		strlcpy(sc->sc.sc_vendor, vendor, sizeof (sc->sc.sc_vendor));
	else
		snprintf(sc->sc.sc_vendor, sizeof (sc->sc.sc_vendor),
			"vendor 0x%04x", PCI_VENDOR(pa->pa_id));

	config_defer(self, uhci_pci_attach_deferred);
	
	/* Ignore interrupts for now */
	sc->sc.sc_bus.dying = 1;

	splx(s);

	return;

unmap_ret:
	bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
	sc->sc.sc_size = 0;
	splx(s);
}

void
uhci_pci_attach_deferred(struct device *self)
{
	struct uhci_pci_softc *sc = (struct uhci_pci_softc *)self;
	char *devname = sc->sc.sc_bus.bdev.dv_xname;
	usbd_status r;
	int s;

	s = splhardusb();
	
	sc->sc.sc_bus.dying = 0;
	r = uhci_init(&sc->sc);
	if (r != USBD_NORMAL_COMPLETION) {
		printf("%s: init failed, error=%d\n", devname, r);
		goto unmap_ret;
	}
	splx(s);

	/* Attach usb device. */
	config_found(self, &sc->sc.sc_bus, usbctlprint);
	return;

unmap_ret:
	bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
	pci_intr_disestablish(sc->sc_pc, sc->sc_ih);
	sc->sc.sc_size = 0;
	splx(s);
}

int
uhci_pci_detach(struct device *self, int flags)
{
	struct uhci_pci_softc *sc = (struct uhci_pci_softc *)self;
	int rv;

	rv = uhci_detach(self, flags);
	if (rv)
		return (rv);
	if (sc->sc_ih != NULL) {
		pci_intr_disestablish(sc->sc_pc, sc->sc_ih);
		sc->sc_ih = NULL;
	}
	if (sc->sc.sc_size) {
		bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
		sc->sc.sc_size = 0;
	}

	return (0);
}
