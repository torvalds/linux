/* $OpenBSD: acpicbkbd.c,v 1.3 2022/04/06 18:59:27 naddy Exp $ */
/*
 * Copyright (c) 2016 joshua stein <jcs@openbsd.org>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
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
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <dev/wscons/wsconsio.h>

/* #define ACPICBKBD_DEBUG */

#ifdef ACPICBKBD_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

#define ACPICBKBD_MAX_BACKLIGHT	100

struct acpicbkbd_softc {
	struct device		sc_dev;
	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	uint64_t		sc_backlight;
};

int	acpicbkbd_match(struct device *, void *, void *);
void	acpicbkbd_attach(struct device *, struct device *, void *);
int	acpicbkbd_activate(struct device *, int);

int	acpicbkbd_get_backlight(struct wskbd_backlight *);
int	acpicbkbd_set_backlight(struct wskbd_backlight *);
void	acpicbkbd_write_backlight(void *, int);
extern int (*wskbd_get_backlight)(struct wskbd_backlight *);
extern int (*wskbd_set_backlight)(struct wskbd_backlight *);

const struct cfattach acpicbkbd_ca = {
	sizeof(struct acpicbkbd_softc),
	acpicbkbd_match,
	acpicbkbd_attach,
	NULL,
	acpicbkbd_activate,
};

struct cfdriver acpicbkbd_cd = {
	NULL, "acpicbkbd", DV_DULL
};

const char *acpicbkbd_hids[] = {
	"GOOG0002",
	NULL
};

int
acpicbkbd_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	return acpi_matchhids(aaa, acpicbkbd_hids, cf->cf_driver->cd_name);
}

void
acpicbkbd_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpicbkbd_softc	*sc = (struct acpicbkbd_softc *)self;
	struct acpi_attach_args *aa = aux;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;

	printf(": %s", sc->sc_devnode->name);

	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "KBQC",
	    0, NULL, &sc->sc_backlight) == 0) {
		wskbd_get_backlight = acpicbkbd_get_backlight;
		wskbd_set_backlight = acpicbkbd_set_backlight;
	} else
		printf(", no backlight control");

	printf("\n");
}

int
acpicbkbd_activate(struct device *self, int act)
{
	struct acpicbkbd_softc	*sc = (struct acpicbkbd_softc *)self;

	switch (act) {
	case DVACT_WAKEUP:
		/* restore backlight to pre-suspend value */
		acpi_addtask(sc->sc_acpi, acpicbkbd_write_backlight, sc, 0);

		break;
	}
	return (0);
}

int
acpicbkbd_get_backlight(struct wskbd_backlight *kbl)
{
	struct acpicbkbd_softc *sc = acpicbkbd_cd.cd_devs[0];

	KASSERT(sc != NULL);

	kbl->min = 0;
	kbl->max = ACPICBKBD_MAX_BACKLIGHT;
	kbl->curval = sc->sc_backlight;

	return 0;
}

int
acpicbkbd_set_backlight(struct wskbd_backlight *kbl)
{
	struct acpicbkbd_softc *sc = acpicbkbd_cd.cd_devs[0];

	KASSERT(sc != NULL);

	if (kbl->curval > ACPICBKBD_MAX_BACKLIGHT)
		return EINVAL;

	sc->sc_backlight = kbl->curval;

	acpi_addtask(sc->sc_acpi, acpicbkbd_write_backlight, sc, 0);
	acpi_wakeup(sc->sc_acpi);

	return 0;
}

void
acpicbkbd_write_backlight(void *arg0, int arg1)
{
	struct acpicbkbd_softc *sc = arg0;
	struct aml_value arg;

	DPRINTF(("%s: writing backlight of %lld\n", sc->sc_dev.dv_xname,
	    sc->sc_backlight));

	memset(&arg, 0, sizeof(arg));
	arg.type = AML_OBJTYPE_INTEGER;
	arg.v_integer = sc->sc_backlight;
	aml_evalname(sc->sc_acpi, sc->sc_devnode, "KBCM", 1, &arg, NULL);
}
