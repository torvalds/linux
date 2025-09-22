/*	$OpenBSD: ahci_acpi.c,v 1.6 2024/10/09 00:38:25 jsg Exp $	*/
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
#include <sys/malloc.h>
#include <sys/systm.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#undef DEVNAME
#include <dev/ic/ahcireg.h>
#include <dev/ic/ahcivar.h>

struct ahci_acpi_softc {
	struct ahci_softc sc;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;
	void 		*sc_ih;
};

int	ahci_acpi_match(struct device *, void *, void *);
void	ahci_acpi_attach(struct device *, struct device *, void *);

const struct cfattach ahci_acpi_ca = {
	sizeof(struct ahci_acpi_softc), ahci_acpi_match, ahci_acpi_attach,
	NULL, ahci_activate
};

int
ahci_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;

	if (aaa->aaa_naddr < 1 || aaa->aaa_nirq < 1)
		return 0;
	return acpi_matchcls(aaa, PCI_CLASS_MASS_STORAGE,
	    PCI_SUBCLASS_MASS_STORAGE_SATA, PCI_INTERFACE_SATA_AHCI10);
}

void
ahci_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct ahci_acpi_softc *sc = (struct ahci_acpi_softc *)self;
	struct acpi_attach_args *aaa = aux;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);
	printf(" irq %d", aaa->aaa_irq[0]);

	sc->sc.sc_iot = aaa->aaa_bst[0];
	sc->sc.sc_ios = aaa->aaa_size[0];
	sc->sc.sc_dmat = aaa->aaa_dmat;

	if (bus_space_map(sc->sc.sc_iot, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc.sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_ih = acpi_intr_establish(aaa->aaa_irq[0], aaa->aaa_irq_flags[0],
	    IPL_BIO, ahci_intr, sc, sc->sc.sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	printf(":");

	if (ahci_attach(&sc->sc) != 0) {
		/* error printed by ahci_attach */
		goto irq;
	}

	return;

irq:
	return;
}
