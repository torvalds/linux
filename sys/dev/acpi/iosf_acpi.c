/*	$OpenBSD: iosf_acpi.c,v 1.1 2023/04/23 00:20:26 dlg Exp $ */
/*
 * Copyright (c) 2023 David Gwynne <dlg@openbsd.org>
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

#include <dev/ic/iosfvar.h>

struct iosf_acpi_softc {
	struct device		 sc_dev;
	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_node;

	bus_space_tag_t		 sc_iot;
	bus_space_handle_t	 sc_ioh;
	bus_size_t		 sc_ios;

	struct iosf_mbi		 sc_mbi;
};

static int	iosf_acpi_match(struct device *, void *, void *);
static void	iosf_acpi_attach(struct device *, struct device *, void *);

static uint32_t	iosf_acpi_mbi_mdr_rd(struct iosf_mbi *, uint32_t, uint32_t);
static void	iosf_acpi_mbi_mdr_wr(struct iosf_mbi *, uint32_t, uint32_t,
		    uint32_t);

const struct cfattach iosf_acpi_ca = {
	sizeof(struct iosf_acpi_softc), iosf_acpi_match, iosf_acpi_attach
};

static const char *iosf_hids[] = {
	"INT33BD",
	NULL
};

static int
iosf_acpi_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1)
		return 0;

	return (acpi_matchhids(aaa, iosf_hids, cf->cf_driver->cd_name));
}

static void
iosf_acpi_attach(struct device *parent, struct device *self, void *aux)
{
	struct iosf_acpi_softc *sc = (struct iosf_acpi_softc *)self;
	struct acpi_attach_args *aaa = aux;
	struct cpu_info *ci;
	int semaddr = -1;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;
	printf(" %s", sc->sc_node->name);

	sc->sc_iot = aaa->aaa_bst[0];
	sc->sc_ios = aaa->aaa_size[0];

	if (bus_space_map(sc->sc_iot, aaa->aaa_addr[0], aaa->aaa_size[0], 0,
	    &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	ci = curcpu();
	if (strcmp(cpu_vendor, "GenuineIntel") == 0 &&
	    ci->ci_family == 0x06 && ci->ci_model == 0x4c) {
		/* cherry trail, braswell */
		semaddr = 0x10e;
	}

	if (semaddr != -1) {
		printf(": mbi");

		sc->sc_mbi.mbi_dev = self;
		sc->sc_mbi.mbi_prio = 1; /* lower prio than iosf_pci ops */
		sc->sc_mbi.mbi_semaddr = semaddr;
		sc->sc_mbi.mbi_mdr_rd = iosf_acpi_mbi_mdr_rd;
		sc->sc_mbi.mbi_mdr_wr = iosf_acpi_mbi_mdr_wr;
	}

	printf("\n");
}

/*
 * mbi mdr ACPI operations
 */

#define IOSF_ACPI_MBI_MCR	0x0
#define IOSF_ACPI_MBI_MDR	0x4
#define IOSF_ACPI_MBI_MCRX	0x8

static uint32_t
iosf_acpi_mbi_mdr_rd(struct iosf_mbi *mbi, uint32_t mcr, uint32_t mcrx)
{
	struct iosf_acpi_softc *sc = (struct iosf_acpi_softc *)mbi->mbi_dev;

	if (mcrx != 0) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, 
		    IOSF_ACPI_MBI_MCRX, mcrx);
	}
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, IOSF_ACPI_MBI_MCR, mcr);

	return (bus_space_read_4(sc->sc_iot, sc->sc_ioh, IOSF_ACPI_MBI_MDR));
}

static void
iosf_acpi_mbi_mdr_wr(struct iosf_mbi *mbi, uint32_t mcr, uint32_t mcrx,
    uint32_t mdr)
{
	struct iosf_acpi_softc *sc = (struct iosf_acpi_softc *)mbi->mbi_dev;

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, IOSF_ACPI_MBI_MDR, mdr);
	if (mcrx != 0) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh,
		    IOSF_ACPI_MBI_MCRX, mcrx);
	}

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, IOSF_ACPI_MBI_MCR, mcr);
}
