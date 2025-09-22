/*	$OpenBSD: acpige.c,v 1.2 2022/04/06 18:59:27 naddy Exp $	*/
/*
 * Copyright (c) 2020 Patrick Wildt <patrick@blueri.se>
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

struct acpige_softc;
struct acpige_irq {
	struct acpige_softc	*ai_sc;
	void			*ai_ih;
	uint32_t		ai_irq;
};

struct acpige_softc {
	struct device		sc_dev;
	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;
	struct acpige_irq	*sc_irq;
};

int	acpige_match(struct device *, void *, void *);
void	acpige_attach(struct device *, struct device *, void *);

int	acpige_intr(void *);
void	acpige_event_task(void *, int);

const struct cfattach acpige_ca = {
	sizeof(struct acpige_softc), acpige_match, acpige_attach
};

struct cfdriver acpige_cd = {
	NULL, "acpige", DV_DULL
};

const char *acpige_hids[] = {
	"ACPI0013",
	NULL
};

int
acpige_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	return acpi_matchhids(aaa, acpige_hids, cf->cf_driver->cd_name);
}

void
acpige_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpige_softc *sc = (struct acpige_softc *)self;
	struct acpi_attach_args *aaa = aux;
	struct acpige_irq *ai;
	int i;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aaa->aaa_node;

	if (aaa->aaa_nirq < 1) {
		printf(": no interrupt\n");
		return;
	}

	sc->sc_irq = mallocarray(aaa->aaa_nirq, sizeof(struct acpige_irq),
	    M_DEVBUF, M_WAITOK);

	for (i = 0; i < aaa->aaa_nirq; i++) {
		printf("%s %d", i ? "," : " irq", aaa->aaa_irq[i]);
		ai = &sc->sc_irq[i];
		ai->ai_sc = sc;
		ai->ai_irq = aaa->aaa_irq[i];
		ai->ai_ih = acpi_intr_establish(aaa->aaa_irq[i],
		    aaa->aaa_irq_flags[i], IPL_BIO, acpige_intr, ai,
		    sc->sc_dev.dv_xname);
		if (ai->ai_ih == NULL) {
			printf(": can't establish interrupt\n");
			return;
		}
	}

	printf("\n");
}

int
acpige_intr(void *arg)
{
	struct acpige_irq *ai = arg;
	struct acpige_softc *sc = ai->ai_sc;

	acpi_addtask(sc->sc_acpi, acpige_event_task, sc, ai->ai_irq);
	acpi_wakeup(sc->sc_acpi);
	return 1;
}

void
acpige_event_task(void *arg0, int irq)
{
	struct acpige_softc *sc = arg0;
	struct aml_value cmd;

	memset(&cmd, 0, sizeof cmd);
	cmd.v_integer = irq;
	cmd.type = AML_OBJTYPE_INTEGER;
	aml_evalname(sc->sc_acpi, sc->sc_devnode, "_EVT", 1, &cmd, NULL);
}
