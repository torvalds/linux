/*	$OpenBSD: ohci_cardbus.c,v 1.24 2024/05/24 06:26:47 jsg Exp $ */
/*	$NetBSD: ohci_cardbus.c,v 1.19 2004/08/02 19:14:28 mycroft Exp $	*/

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
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/cardbus/cardbusvar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>

#include <dev/usb/ohcireg.h>
#include <dev/usb/ohcivar.h>

int	ohci_cardbus_match(struct device *, void *, void *);
void	ohci_cardbus_attach(struct device *, struct device *, void *);
int	ohci_cardbus_detach(struct device *, int);

struct ohci_cardbus_softc {
	struct ohci_softc	sc;
	cardbus_chipset_tag_t	sc_cc;
	cardbus_function_tag_t	sc_cf;
	cardbus_devfunc_t	sc_ct;
	void 			*sc_ih;		/* interrupt vectoring */
};

const struct cfattach ohci_cardbus_ca = {
	sizeof(struct ohci_cardbus_softc), ohci_cardbus_match,
	    ohci_cardbus_attach, ohci_cardbus_detach, ohci_activate
};

#define CARDBUS_CBMEM PCI_CBMEM
#define cardbus_findvendor pci_findvendor

int
ohci_cardbus_match(struct device *parent, void *match, void *aux)
{
	struct cardbus_attach_args *ca = (struct cardbus_attach_args *)aux;

	if (PCI_CLASS(ca->ca_class) == PCI_CLASS_SERIALBUS &&
	    PCI_SUBCLASS(ca->ca_class) == PCI_SUBCLASS_SERIALBUS_USB &&
	    PCI_INTERFACE(ca->ca_class) == PCI_INTERFACE_OHCI)
		return (1);
 
	return (0);
}

void
ohci_cardbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct ohci_cardbus_softc *sc = (struct ohci_cardbus_softc *)self;
	struct cardbus_attach_args *ca = aux;
	cardbus_devfunc_t ct = ca->ca_ct;
	cardbus_chipset_tag_t cc = ct->ct_cc;
	pci_chipset_tag_t pc = ca->ca_pc;
	cardbus_function_tag_t cf = ct->ct_cf;
	pcireg_t csr;
	usbd_status r;
	const char *vendor;
	const char *devname = sc->sc.sc_bus.bdev.dv_xname;

	/* Map I/O registers */
	if (Cardbus_mapreg_map(ct, CARDBUS_CBMEM, PCI_MAPREG_TYPE_MEM, 0,
			   &sc->sc.iot, &sc->sc.ioh, NULL, &sc->sc.sc_size)) {
		printf(": can't map mem space\n");
		return;
	}

	/* Disable interrupts, so we don't get any spurious ones. */
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, OHCI_INTERRUPT_DISABLE,
			  OHCI_MIE);

	sc->sc_cc = cc;
	sc->sc_cf = cf;
	sc->sc_ct = ct;
	sc->sc.sc_bus.dmatag = ca->ca_dmat;

	(ct->ct_cf->cardbus_ctrl)(cc, CARDBUS_MEM_ENABLE);
	(ct->ct_cf->cardbus_ctrl)(cc, CARDBUS_BM_ENABLE);

	/* Enable the device. */
	csr = pci_conf_read(pc, ca->ca_tag,
				PCI_COMMAND_STATUS_REG);
	pci_conf_write(pc, ca->ca_tag, PCI_COMMAND_STATUS_REG,
		       csr | PCI_COMMAND_MASTER_ENABLE
			   | PCI_COMMAND_MEM_ENABLE);

	sc->sc_ih = cardbus_intr_establish(cc, cf, ca->ca_intrline,
					   IPL_USB, ohci_intr, sc, devname);
	if (sc->sc_ih == NULL) {
		printf(": couldn't establish interrupt\n");
		return;
	}
	printf(": irq %d, ", ca->ca_intrline);

	/* Figure out vendor for root hub descriptor. */
	vendor = cardbus_findvendor(ca->ca_id);
	sc->sc.sc_id_vendor = PCI_VENDOR(ca->ca_id);
	if (vendor)
		strlcpy(sc->sc.sc_vendor, vendor, sizeof(sc->sc.sc_vendor));
	else
		snprintf(sc->sc.sc_vendor, sizeof(sc->sc.sc_vendor),
		    "vendor 0x%04x", PCI_VENDOR(ca->ca_id));

	/* Display revision and perform legacy emulation handover. */
	if (ohci_checkrev(&sc->sc) != USBD_NORMAL_COMPLETION ||
	    ohci_handover(&sc->sc) != USBD_NORMAL_COMPLETION) {
		cardbus_intr_disestablish(sc->sc_cc, sc->sc_cf, sc->sc_ih);
		sc->sc_ih = 0;
		return;
	}

	r = ohci_init(&sc->sc);
	if (r != USBD_NORMAL_COMPLETION) {
		printf("%s: init failed, error=%d\n", devname, r);

		/* Avoid spurious interrupts. */
		cardbus_intr_disestablish(sc->sc_cc, sc->sc_cf, sc->sc_ih);
		sc->sc_ih = 0;

		return;
	}

	/* Attach usb device. */
	config_found(self, &sc->sc.sc_bus, usbctlprint);
}

int
ohci_cardbus_detach(struct device *self, int flags)
{
	struct ohci_cardbus_softc *sc = (struct ohci_cardbus_softc *)self;
	struct cardbus_devfunc *ct = sc->sc_ct;
	int rv;

	rv = ohci_detach(self, flags);
	if (rv)
		return (rv);

	if (sc->sc_ih != NULL) {
		cardbus_intr_disestablish(sc->sc_cc, sc->sc_cf, sc->sc_ih);
		sc->sc_ih = NULL;
	}
	if (sc->sc.sc_size) {
		Cardbus_mapreg_unmap(ct, CARDBUS_CBMEM, sc->sc.iot,
		    sc->sc.ioh, sc->sc.sc_size);
		sc->sc.sc_size = 0;
	}

	return (0);
}
