/*	$OpenBSD: acpisectwo.c,v 1.1 2024/07/30 19:47:06 mglocker Exp $ */
/*
 * Copyright (c) 2024 Marcus Glocker <mglocker@openbsd.org>
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

#include <dev/acpi/acpivar.h>
#include <dev/acpi/dsdt.h>

//#define ACPISECTWO_DEBUG
#ifdef ACPISECTWO_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

#define ACPISECTWO_REGIONSPACE_BAT	0xa1

struct acpisectwo_softc {
	struct device		 sc_dev;
	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_node;
};

int	acpisectwo_match(struct device *, void *, void *);
void	acpisectwo_attach(struct device *, struct device *, void *);

const struct cfattach acpisectwo_ca = {
	sizeof(struct acpisectwo_softc), acpisectwo_match, acpisectwo_attach
};

struct cfdriver acpisectwo_cd = {
	NULL, "acpisectwo", DV_DULL
};

int	acpisectwo_bat_opreg_handler(void *, int, uint64_t, int, uint64_t *);

int
acpisectwo_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aa = aux;
	struct cfdata *cf = match;

	if (aa->aaa_name == NULL ||
	    strcmp(aa->aaa_name, cf->cf_driver->cd_name) != 0 ||
	    aa->aaa_table != NULL)
		return 0;

	return 1;
}

void
acpisectwo_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpisectwo_softc *sc = (struct acpisectwo_softc *)self;
	struct acpi_attach_args *aa = aux;

	printf("\n");

	sc->sc_node = aa->aaa_node;

	aml_register_regionspace(sc->sc_node, ACPISECTWO_REGIONSPACE_BAT, sc,
	    acpisectwo_bat_opreg_handler);
}

int
acpisectwo_bat_opreg_handler(void *cookie, int iodir, uint64_t address,
    int size, uint64_t *value)
{
	DPRINTF(("%s: iodir=%d, address=0x%llx, size=%d\n",
	    __func__, iodir, address, size));

	*value = 0;

	return 0;
}
