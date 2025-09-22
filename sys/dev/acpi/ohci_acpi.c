/*	$OpenBSD: ohci_acpi.c,v 1.3 2024/10/09 00:38:25 jsg Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>

#include <machine/bus.h>
#include <machine/intr.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/usb/usb.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdivar.h>
#include <dev/usb/usb_mem.h>

#include <dev/usb/ohcireg.h>
#include <dev/usb/ohcivar.h>

struct ohci_acpi_softc {
	struct ohci_softc sc;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;
	void		*sc_ih;
};

int	ohci_acpi_match(struct device *, void *, void *);
void	ohci_acpi_attach(struct device *, struct device *, void *);

const struct cfattach ohci_acpi_ca = {
	sizeof(struct ohci_acpi_softc), ohci_acpi_match, ohci_acpi_attach,
	NULL, ohci_activate
};

void	ohci_acpi_attach_deferred(struct device *);

int
ohci_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;

	return acpi_matchcls(aaa, PCI_CLASS_SERIALBUS,
	    PCI_SUBCLASS_SERIALBUS_USB, PCI_INTERFACE_OHCI);
}

void
ohci_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct ohci_acpi_softc *sc = (struct ohci_acpi_softc *)self;
	struct acpi_attach_args *aaa = aux;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);
	printf(" irq %d", aaa->aaa_irq[0]);

	sc->sc.iot = aaa->aaa_bst[0];
	sc->sc.sc_size = aaa->aaa_size[0];
	sc->sc.sc_bus.dmatag = aaa->aaa_dmat;

	if (bus_space_map(sc->sc.iot, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc.ioh)) {
		printf(": can't map registers\n");
		return;
	}

	/* Record what interrupts were enabled by SMM/BIOS. */
	sc->sc.sc_intre = bus_space_read_4(sc->sc.iot, sc->sc.ioh,
	    OHCI_INTERRUPT_ENABLE);

	/* Disable interrupts, so we don't get any spurious ones. */
	bus_space_write_4(sc->sc.iot, sc->sc.ioh, OHCI_INTERRUPT_DISABLE,
	    OHCI_MIE);

	bus_space_barrier(sc->sc.iot, sc->sc.ioh, 0, sc->sc.sc_size,
	    BUS_SPACE_BARRIER_READ|BUS_SPACE_BARRIER_WRITE);
	bus_space_write_4(sc->sc.iot, sc->sc.ioh,
	    OHCI_INTERRUPT_DISABLE, OHCI_MIE);

	/* Map and establish the interrupt. */
	sc->sc_ih = acpi_intr_establish(aaa->aaa_irq[0], aaa->aaa_irq_flags[0],
	    IPL_USB, ohci_intr, sc, sc->sc.sc_bus.bdev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	printf(": ");

	strlcpy(sc->sc.sc_vendor, "Generic", sizeof(sc->sc.sc_vendor));

	/* Display revision and perform legacy emulation handover. */
	if (ohci_checkrev(&sc->sc) != USBD_NORMAL_COMPLETION ||
	    ohci_handover(&sc->sc) != USBD_NORMAL_COMPLETION)
		goto disestablish_ret;

	/* Ignore interrupts for now */
	sc->sc.sc_bus.dying = 1;

	config_defer(self, ohci_acpi_attach_deferred);
	return;

disestablish_ret:
	acpi_intr_disestablish(sc->sc_ih);
unmap:
	bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
	return;
}

void
ohci_acpi_attach_deferred(struct device *self)
{
	struct ohci_acpi_softc *sc = (struct ohci_acpi_softc *)self;
	usbd_status r;
	int s;

	s = splusb();
	sc->sc.sc_bus.dying = 0;
	r = ohci_init(&sc->sc);
	splx(s);

	if (r != USBD_NORMAL_COMPLETION) {
		printf("%s: init failed, error=%d\n",
		    sc->sc.sc_bus.bdev.dv_xname, r);
		acpi_intr_disestablish(sc->sc_ih);
		sc->sc_ih = NULL;
		bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
		sc->sc.sc_size = 0;
		return;
	}

	/* Attach usb device. */
	config_found(self, &sc->sc.sc_bus, usbctlprint);
}
