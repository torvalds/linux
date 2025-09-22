/*	$OpenBSD: pluart_acpi.c,v 1.9 2022/06/11 05:29:24 anton Exp $	*/
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
#include <sys/tty.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#undef DEVNAME
#include <dev/ic/pluartvar.h>
#include <dev/cons.h>

struct pluart_acpi_softc {
	struct pluart_softc sc;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;
	bus_addr_t	sc_addr;
	void		*sc_ih;
};

int	pluart_acpi_match(struct device *, void *, void *);
void	pluart_acpi_attach(struct device *, struct device *, void *);

const struct cfattach pluart_acpi_ca = {
	sizeof(struct pluart_acpi_softc), pluart_acpi_match, pluart_acpi_attach
};

const char *pluart_hids[] = {
	"ARMH0011",
	NULL
};

int	pluart_acpi_is_console(struct pluart_acpi_softc *);

int
pluart_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1 || aaa->aaa_nirq < 1)
		return 0;
	return acpi_matchhids(aaa, pluart_hids, cf->cf_driver->cd_name);
}

void
pluart_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct pluart_acpi_softc *sc = (struct pluart_acpi_softc *)self;
	struct acpi_attach_args *aaa = aux;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);
	printf(" irq %d", aaa->aaa_irq[0]);

	sc->sc.sc_iot = aaa->aaa_bst[0];
	sc->sc_addr = aaa->aaa_addr[0];
	if (bus_space_map(sc->sc.sc_iot, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc.sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	sc->sc_ih = acpi_intr_establish(aaa->aaa_irq[0], aaa->aaa_irq_flags[0],
	    IPL_TTY, pluart_intr, sc, sc->sc.sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	sc->sc.sc_hwflags |= COM_HW_SBSA;

	pluart_attach_common(&sc->sc, pluart_acpi_is_console(sc));
}

int
pluart_acpi_is_console(struct pluart_acpi_softc *sc)
{
	struct acpi_table_header *hdr;
	struct acpi_spcr *spcr;
	struct acpi_gas *base;
	struct acpi_q *entry;

	SIMPLEQ_FOREACH(entry, &sc->sc_acpi->sc_tables, q_next) {
		hdr = entry->q_table;
		if (strncmp(hdr->signature, SPCR_SIG,
		    sizeof(hdr->signature)) == 0) {
			spcr = entry->q_table;
			base = &spcr->base_address;
			if (base->address_space_id == GAS_SYSTEM_MEMORY &&
			    base->address == sc->sc_addr)
				return 1;
		}
	}

	return 0;
}
