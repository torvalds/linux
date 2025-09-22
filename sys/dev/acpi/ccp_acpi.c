/*	$OpenBSD: ccp_acpi.c,v 1.4 2022/04/06 18:59:27 naddy Exp $	*/
/*
 * Copyright (c) 2019 Mark Kettenis
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

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/ic/ccpvar.h>

struct ccp_acpi_softc {
	struct ccp_softc sc;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;
};

int	ccp_acpi_match(struct device *, void *, void *);
void	ccp_acpi_attach(struct device *, struct device *, void *);

const struct cfattach ccp_acpi_ca = {
	sizeof(struct ccp_acpi_softc), ccp_acpi_match, ccp_acpi_attach
};

const char *ccp_hids[] = {
	"AMDI0C00",
	NULL
};

int
ccp_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1)
		return 0;
	return acpi_matchhids(aaa, ccp_hids, cf->cf_driver->cd_name);
}

void
ccp_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct ccp_acpi_softc *sc = (struct ccp_acpi_softc *)self;
	struct acpi_attach_args *aaa = aux;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);

	sc->sc.sc_iot = aaa->aaa_bst[0];
	if (bus_space_map(sc->sc.sc_iot, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc.sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	ccp_attach(&sc->sc);
}
