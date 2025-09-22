/*	$OpenBSD: amdpmc.c,v 1.1 2025/08/03 09:23:19 kettenis Exp $	*/
/*
 * Copyright (c) 2025 Mark Kettenis <kettenis@openbsd.org>
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

#include <machine/intr.h>
#include <machine/bus.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

/* Low Power S0 Idle DSM methods */
#define ACPI_LPS0_ENUM_FUNCTIONS 	0
#define ACPI_LPS0_GET_CONSTRAINTS	1
#define ACPI_LPS0_ENTRY			2
#define ACPI_LPS0_EXIT			3
#define ACPI_LPS0_SCREEN_OFF		4
#define ACPI_LPS0_SCREEN_ON		5

struct amdpmc_softc {
	struct device		sc_dev;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_node;
};

int	amdpmc_match(struct device *, void *, void *);
void	amdpmc_attach(struct device *, struct device *, void *);
int	amdpmc_activate(struct device *, int);

const struct cfattach amdpmc_ca = {
	sizeof (struct amdpmc_softc), amdpmc_match, amdpmc_attach,
	NULL, amdpmc_activate
};

struct cfdriver amdpmc_cd = {
	NULL, "amdpmc", DV_DULL
};

const char *amdpmc_hids[] = {
	"AMDI0005",
	"AMDI0006",
	"AMDI0007",
	"AMDI0008",
	"AMDI0009",
	"AMDI000A",
	"AMDI000B",
	NULL
};

void	amdpmc_suspend(void *);
void	amdpmc_resume(void *);

int
amdpmc_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	return acpi_matchhids(aaa, amdpmc_hids, cf->cf_driver->cd_name);
}

void
amdpmc_attach(struct device *parent, struct device *self, void *aux)
{
	struct amdpmc_softc *sc = (struct amdpmc_softc *)self;
	struct acpi_attach_args *aaa = aux;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;

	printf(": %s\n", aaa->aaa_node->name);

	sc->sc_acpi->sc_pmc_suspend = amdpmc_suspend;
	sc->sc_acpi->sc_pmc_resume = amdpmc_resume;
	sc->sc_acpi->sc_pmc_cookie = sc;
}

int
amdpmc_activate(struct device *self, int act)
{
	return 0;
}

int
amdpmc_dsm(struct acpi_softc *sc, struct aml_node *node, int func)
{
	struct aml_value cmd[4];
	struct aml_value res;

	/* e3f32452-febc-43ce-9039-932122d37721 */
	static uint8_t lps0_dsm_guid[] = {
		0x52, 0x24, 0xF3, 0xE3, 0xBC, 0xFE, 0xCE, 0x43,
		0x90, 0x39, 0x93, 0x21, 0x22, 0xD3, 0x77, 0x21,
	};

	bzero(&cmd, sizeof(cmd));
	cmd[0].type = AML_OBJTYPE_BUFFER;
	cmd[0].v_buffer = (uint8_t *)&lps0_dsm_guid;
	cmd[0].length = sizeof(lps0_dsm_guid);
	/* rev */
	cmd[1].type = AML_OBJTYPE_INTEGER;
	cmd[1].v_integer = 0;
	cmd[1].length = 1;
	/* func */
	cmd[2].type = AML_OBJTYPE_INTEGER;
	cmd[2].v_integer = func;
	cmd[2].length = 1;
	/* not used */
	cmd[3].type = AML_OBJTYPE_PACKAGE;
	cmd[3].length = 0;

	if (aml_evalname(sc, node, "_DSM", 4, cmd, &res)) {
		printf("%s: eval of _DSM at %s failed\n",
		    sc->sc_dev.dv_xname, aml_nodename(node));
		return 1;
	}
	aml_freevalue(&res);

	return 0;
}

void
amdpmc_suspend(void *cookie)
{
	struct amdpmc_softc *sc = cookie;

	if (sc->sc_acpi->sc_state != ACPI_STATE_S0)
		return;

	amdpmc_dsm(sc->sc_acpi, sc->sc_node, ACPI_LPS0_SCREEN_OFF);
	amdpmc_dsm(sc->sc_acpi, sc->sc_node, ACPI_LPS0_ENTRY);
}

void
amdpmc_resume(void *cookie)
{
	struct amdpmc_softc *sc = cookie;

	if (sc->sc_acpi->sc_state != ACPI_STATE_S0)
		return;

	amdpmc_dsm(sc->sc_acpi, sc->sc_node, ACPI_LPS0_EXIT);
	amdpmc_dsm(sc->sc_acpi, sc->sc_node, ACPI_LPS0_SCREEN_ON);
}
