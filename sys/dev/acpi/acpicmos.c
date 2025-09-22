/*	$OpenBSD: acpicmos.c,v 1.3 2025/09/16 12:18:10 hshoexer Exp $	*/
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

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/dsdt.h>

#include <dev/ic/mc146818reg.h>

struct acpicmos_softc {
	struct device sc_dev;
	struct aml_node *sc_node;
};

int	acpicmos_match(struct device *, void *, void *);
void	acpicmos_attach(struct device *, struct device *, void *);

const struct cfattach acpicmos_ca = {
	sizeof(struct acpicmos_softc), acpicmos_match, acpicmos_attach
};

struct cfdriver acpicmos_cd = {
	NULL, "acpicmos", DV_DULL, CD_COCOVM
};

const char *acpicmos_hids[] = {
	"PNP0B00",
	"PNP0B01",
	"PNP0B02",
	NULL
};

int	acpicmos_opreg_handler(void *, int, uint64_t, int, uint64_t *);

int
acpicmos_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	return acpi_matchhids(aaa, acpicmos_hids, cf->cf_driver->cd_name);
}

void
acpicmos_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpicmos_softc *sc = (struct acpicmos_softc *)self;
	struct acpi_attach_args *aaa = aux;

	printf("\n");

	sc->sc_node = aaa->aaa_node;
	aml_register_regionspace(sc->sc_node, ACPI_OPREG_CMOS,
	    sc, acpicmos_opreg_handler);
}


int
acpicmos_opreg_handler(void *cookie, int iodir, uint64_t address, int size,
    uint64_t *value)
{
	/* Only allow 8-bit access. */
	if (size != 8 || address > 0xff)
		return -1;

	if (iodir == ACPI_IOREAD)
		*value = mc146818_read(NULL, address);
	else
		mc146818_write(NULL, address, *value);


	return 0;
}
