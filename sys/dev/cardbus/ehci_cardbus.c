/*	$OpenBSD: ehci_cardbus.c,v 1.24 2024/05/24 06:26:47 jsg Exp $ */
/*	$NetBSD: ehci_cardbus.c,v 1.6.6.3 2004/09/21 13:27:25 skrll Exp $	*/

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
#include <sys/rwlock.h>
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/cardbus/cardbusvar.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>

#include <dev/usb/ehcireg.h>
#include <dev/usb/ehcivar.h>

int	ehci_cardbus_match(struct device *, void *, void *);
void	ehci_cardbus_attach(struct device *, struct device *, void *);
int	ehci_cardbus_detach(struct device *, int);

struct ehci_cardbus_softc {
	struct ehci_softc	sc;
	cardbus_chipset_tag_t	sc_cc;
	cardbus_function_tag_t	sc_cf;
	cardbus_devfunc_t	sc_ct;
	void 			*sc_ih;		/* interrupt vectoring */
};

const struct cfattach ehci_cardbus_ca = {
	sizeof(struct ehci_cardbus_softc), ehci_cardbus_match,
	ehci_cardbus_attach, ehci_cardbus_detach, ehci_activate
};

#define CARDBUS_CBMEM PCI_CBMEM
#define cardbus_findvendor pci_findvendor

int
ehci_cardbus_match(struct device *parent, void *match, void *aux)
{
	struct cardbus_attach_args *ca = (struct cardbus_attach_args *)aux;

	if (PCI_CLASS(ca->ca_class) == PCI_CLASS_SERIALBUS &&
	    PCI_SUBCLASS(ca->ca_class) == PCI_SUBCLASS_SERIALBUS_USB &&
	    PCI_INTERFACE(ca->ca_class) == PCI_INTERFACE_EHCI)
		return (1);
 
	return (0);
}

void
ehci_cardbus_attach(struct device *parent, struct device *self, void *aux)
{
	struct ehci_cardbus_softc *sc = (struct ehci_cardbus_softc *)self;
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

	/* Disable interrupts, so we don't get any spurious ones. */
	sc->sc.sc_offs = EREAD1(&sc->sc, EHCI_CAPLENGTH);
	EOWRITE2(&sc->sc, EHCI_USBINTR, 0);

	sc->sc_ih = cardbus_intr_establish(cc, cf, ca->ca_intrline,
	    IPL_USB | IPL_MPSAFE, ehci_intr, sc, devname);
	if (sc->sc_ih == NULL) {
		printf(": unable to establish interrupt\n");
		return;
	}
	printf(": irq %d\n", ca->ca_intrline);

	/* Figure out vendor for root hub descriptor. */
	vendor = cardbus_findvendor(ca->ca_id);
	sc->sc.sc_id_vendor = PCI_VENDOR(ca->ca_id);
	if (vendor)
		strlcpy(sc->sc.sc_vendor, vendor, sizeof(sc->sc.sc_vendor));
	else
		snprintf(sc->sc.sc_vendor, sizeof(sc->sc.sc_vendor),
		    "vendor 0x%04x", PCI_VENDOR(ca->ca_id));
	
	r = ehci_init(&sc->sc);
	if (r != USBD_NORMAL_COMPLETION) {
		printf("%s: init failed, error=%d\n", devname, r);

		/* Avoid spurious interrupts. */
		cardbus_intr_disestablish(sc->sc_cc, sc->sc_cf, sc->sc_ih);
		sc->sc_ih = NULL;

		return;
	}

	/* Attach usb device. */
	config_found(self, &sc->sc.sc_bus, usbctlprint);
}

int
ehci_cardbus_detach(struct device *self, int flags)
{
	struct ehci_cardbus_softc *sc = (struct ehci_cardbus_softc *)self;
	struct cardbus_devfunc *ct = sc->sc_ct;
	int rv;

	rv = ehci_detach(self, flags);
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
