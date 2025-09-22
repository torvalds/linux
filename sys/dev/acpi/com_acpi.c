/*	$OpenBSD: com_acpi.c,v 1.11 2023/04/16 11:38:42 kettenis Exp $	*/
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
#include <dev/ic/comreg.h>
#include <dev/ic/comvar.h>
#include <dev/cons.h>

#define com_usr 31	/* Synopsys DesignWare UART */

struct com_acpi_softc {
	struct com_softc sc;
	struct acpi_softc *sc_acpi;
	struct aml_node *sc_node;
	void		*sc_ih;
};

int	com_acpi_match(struct device *, void *, void *);
void	com_acpi_attach(struct device *, struct device *, void *);

const struct cfattach com_acpi_ca = {
	sizeof(struct com_acpi_softc), com_acpi_match, com_acpi_attach,
	NULL, com_activate
};

const char *com_hids[] = {
	"AMDI0020",
	"HISI0031",
	"PNP0501",
	NULL
};

int	com_acpi_is_console(struct com_acpi_softc *);
int	com_acpi_is_designware(const char *);
int	com_acpi_intr_designware(void *);

int
com_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;
	bus_space_handle_t ioh;
	bus_space_tag_t iot;
	int rv;

	if (aaa->aaa_naddr < 1 || aaa->aaa_nirq < 1)
		return 0;
	if (cf->acpidevcf_addr != aaa->aaa_addr[0] &&
	    cf->acpidevcf_addr != ACPIDEVCF_ADDR_UNK)
		return 0;

	if (!acpi_matchhids(aaa, com_hids, cf->cf_driver->cd_name))
		return 0;
	if (com_acpi_is_designware(aaa->aaa_dev))
		return 1;

	if (aaa->aaa_addr[0] == comconsaddr)
		return 1;
	iot = aaa->aaa_bst[0];
	if (bus_space_map(iot, aaa->aaa_addr[0], aaa->aaa_size[0], 0, &ioh))
		return 0;
	rv = comprobe1(iot, ioh);
	bus_space_unmap(iot, ioh, aaa->aaa_size[0]);

	return rv;
}

void
com_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct com_acpi_softc *sc = (struct com_acpi_softc *)self;
	struct acpi_attach_args *aaa = aux;
	int (*intr)(void *) = comintr;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);
	printf(" irq %d", aaa->aaa_irq[0]);

	sc->sc.sc_frequency = COM_FREQ;
	if (strcmp(aaa->aaa_dev, "AMDI0020") == 0)
		sc->sc.sc_frequency = 48000000;

	sc->sc.sc_iot = aaa->aaa_bst[0];
	sc->sc.sc_iobase = aaa->aaa_addr[0];
	sc->sc.sc_frequency = acpi_getpropint(sc->sc_node, "clock-frequency",
	    sc->sc.sc_frequency);

	if (com_acpi_is_designware(aaa->aaa_dev)) {
		intr = com_acpi_intr_designware;
		sc->sc.sc_uarttype = COM_UART_16550;
		sc->sc.sc_reg_width = acpi_getpropint(sc->sc_node,
		    "reg-io-width", 4);
		sc->sc.sc_reg_shift = acpi_getpropint(sc->sc_node,
		    "reg-shift", 2);

		if (com_acpi_is_console(sc)) {
			SET(sc->sc.sc_hwflags, COM_HW_CONSOLE);
			SET(sc->sc.sc_swflags, COM_SW_SOFTCAR);
			comconsfreq = sc->sc.sc_frequency;
			comconsrate = B115200;
		}
	}

	if (sc->sc.sc_iobase == comconsaddr) {
		sc->sc.sc_ioh = comconsioh;
	} else {
		if (bus_space_map(sc->sc.sc_iot,
		    aaa->aaa_addr[0], aaa->aaa_size[0], 0, &sc->sc.sc_ioh)) {
			printf(": can't map registers\n");
			return;
		}
	}

	sc->sc_ih = acpi_intr_establish(aaa->aaa_irq[0], aaa->aaa_irq_flags[0],
	    IPL_TTY, intr, sc, sc->sc.sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	com_attach_subr(&sc->sc);
}

int
com_acpi_is_console(struct com_acpi_softc *sc)
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
			    base->address == sc->sc.sc_iobase)
				return 1;
		}
	}

	return 0;
}

int
com_acpi_is_designware(const char *hid)
{
	return strcmp(hid, "AMDI0020") == 0 ||
	    strcmp(hid, "HISI0031") == 0;
}

int
com_acpi_intr_designware(void *cookie)
{
	struct com_acpi_softc *sc = cookie;

	com_read_reg(&sc->sc, com_usr);

	return comintr(&sc->sc);
}
