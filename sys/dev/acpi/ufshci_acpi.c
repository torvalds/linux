/*	$OpenBSD: ufshci_acpi.c,v 1.3 2024/10/08 00:46:29 jsg Exp $ */
/*
 * Copyright (c) 2022 Marcus Glocker <mglocker@openbsd.org>
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

#include <scsi/scsi_all.h>
#include <scsi/scsi_disk.h>
#include <scsi/scsiconf.h>

#include <dev/ic/ufshcivar.h>

struct ufshci_acpi_softc {
	struct ufshci_softc	 sc;
	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_node;
	void			*sc_ih;
};

int	ufshci_acpi_match(struct device *, void *, void *);
void	ufshci_acpi_attach(struct device *, struct device *, void *);

const struct cfattach ufshci_acpi_ca = {
	sizeof(struct ufshci_acpi_softc), ufshci_acpi_match, ufshci_acpi_attach,
	NULL, ufshci_activate
};

const char *ufshci_hids[] = {
	"QCOM24A5",
	NULL
};

int
ufshci_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1)
		return 0;

	return acpi_matchhids(aaa, ufshci_hids, cf->cf_driver->cd_name);
}

void
ufshci_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct ufshci_acpi_softc *sc = (struct ufshci_acpi_softc *)self;
	struct acpi_attach_args *aaa = aux;
	int error;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	if (aaa->aaa_nirq < 1) {
		printf(": no interrupt\n");
		return;
	}

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);
	printf(" irq %d", aaa->aaa_irq[0]);

	sc->sc.sc_iot = aaa->aaa_bst[0];
	sc->sc.sc_ios = aaa->aaa_size[0];
	sc->sc.sc_dmat = aaa->aaa_dmat;

	if (bus_space_map(sc->sc.sc_iot, aaa->aaa_addr[0], aaa->aaa_size[0], 0,
	    &sc->sc.sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_ih = acpi_intr_establish(aaa->aaa_irq[0], aaa->aaa_irq_flags[0],
	    IPL_BIO, ufshci_intr, sc, sc->sc.sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	error = ufshci_attach(&sc->sc);
	if (error) {
		printf("%s: attach failed, error=%d\n",
		    sc->sc.sc_dev.dv_xname, error);
		return;
	}
}
