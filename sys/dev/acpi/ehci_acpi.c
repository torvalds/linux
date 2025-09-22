/*	$OpenBSD: ehci_acpi.c,v 1.4 2024/10/09 00:38:26 jsg Exp $	*/
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

#include <dev/usb/ehcireg.h>
#include <dev/usb/ehcivar.h>

struct ehci_acpi_softc {
	struct ehci_softc sc;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;
	void		*sc_ih;
};

int	ehci_acpi_match(struct device *, void *, void *);
void	ehci_acpi_attach(struct device *, struct device *, void *);

const struct cfattach ehci_acpi_ca = {
	sizeof(struct ehci_acpi_softc), ehci_acpi_match, ehci_acpi_attach,
	NULL, ehci_activate
};

const char *ehci_hids[] = {
	"PNP0D20",
	"PNP0D25",
	NULL
};

int
ehci_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1 || aaa->aaa_nirq < 1)
		return 0;
	return acpi_matchhids(aaa, ehci_hids, cf->cf_driver->cd_name);
}

void
ehci_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct ehci_acpi_softc *sc = (struct ehci_acpi_softc *)self;
	struct acpi_attach_args *aaa = aux;
	int error;

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

	/* Disable interrupts, so we don't get any spurious ones. */
	sc->sc.sc_offs = EREAD1(&sc->sc, EHCI_CAPLENGTH);
	EOWRITE2(&sc->sc, EHCI_USBINTR, 0);

	sc->sc_ih = acpi_intr_establish(aaa->aaa_irq[0], aaa->aaa_irq_flags[0],
	    IPL_USB, ehci_intr, sc, sc->sc.sc_bus.bdev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		goto unmap;
	}

	printf("\n");

	strlcpy(sc->sc.sc_vendor, "Generic", sizeof(sc->sc.sc_vendor));
	error = ehci_init(&sc->sc);
	if (error) {
		printf("%s: init failed, error=%d\n",
		    sc->sc.sc_bus.bdev.dv_xname, error);
		goto disestablish_ret;
	}

	/* Attach usb device. */
	config_found(self, &sc->sc.sc_bus, usbctlprint);

	return;

disestablish_ret:
	acpi_intr_disestablish(sc->sc_ih);
unmap:
	bus_space_unmap(sc->sc.iot, sc->sc.ioh, sc->sc.sc_size);
	return;
}
