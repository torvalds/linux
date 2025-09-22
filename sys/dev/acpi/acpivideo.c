/*	$OpenBSD: acpivideo.c,v 1.14 2022/04/06 18:59:27 naddy Exp $	*/
/*
 * Copyright (c) 2008 Federico G. Schwindt <fgsch@openbsd.org>
 * Copyright (c) 2009 Paul Irofti <paul@irofti.net>
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
#include <sys/device.h>

#include <machine/bus.h>

#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#ifdef ACPIVIDEO_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

/* _DOS Enable/Disable Output Switching */
#define DOS_SWITCH_BY_OSPM		0
#define DOS_SWITCH_BY_BIOS		1
#define DOS_SWITCH_LOCKED		2
#define DOS_SWITCH_BY_OSPM_EXT		3
#define DOS_BRIGHTNESS_BY_OSPM		4

/* Notifications for Displays Devices */
#define NOTIFY_OUTPUT_SWITCHED		0x80
#define NOTIFY_OUTPUT_CHANGED		0x81
#define NOTIFY_OUTPUT_CYCLE_KEY		0x82
#define NOTIFY_OUTPUT_NEXT_KEY		0x83
#define NOTIFY_OUTPUT_PREV_KEY		0x84

int	acpivideo_match(struct device *, void *, void *);
void	acpivideo_attach(struct device *, struct device *, void *);
int	acpivideo_notify(struct aml_node *, int, void *);

void	acpivideo_set_policy(struct acpivideo_softc *, int);
int	acpi_foundvout(struct aml_node *, void *);
int	acpivideo_print(void *, const char *);

int	acpivideo_getpcibus(struct acpivideo_softc *, struct aml_node *);

const struct cfattach acpivideo_ca = {
	sizeof(struct acpivideo_softc), acpivideo_match, acpivideo_attach
};

struct cfdriver acpivideo_cd = {
	NULL, "acpivideo", DV_DULL
};

int
acpivideo_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args	*aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_name == NULL || strcmp(aaa->aaa_name,
	    cf->cf_driver->cd_name) != 0 || aaa->aaa_table != NULL)
		return (0);

	return (1);
}

void
acpivideo_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpivideo_softc *sc = (struct acpivideo_softc *)self;
	struct acpi_attach_args *aaa = aux;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aaa->aaa_node;

	printf(": %s\n", sc->sc_devnode->name);

	if (acpivideo_getpcibus(sc, sc->sc_devnode) == -1)
		return;

	aml_register_notify(sc->sc_devnode, aaa->aaa_dev,
	    acpivideo_notify, sc, ACPIDEV_NOPOLL);

	acpivideo_set_policy(sc,
	    DOS_SWITCH_BY_OSPM | DOS_BRIGHTNESS_BY_OSPM);

	aml_find_node(aaa->aaa_node, "_BCL", acpi_foundvout, sc);
}

int
acpivideo_notify(struct aml_node *node, int notify, void *arg)
{
	struct acpivideo_softc *sc = arg;

	switch (notify) {
	case NOTIFY_OUTPUT_SWITCHED:
	case NOTIFY_OUTPUT_CHANGED:
	case NOTIFY_OUTPUT_CYCLE_KEY:
	case NOTIFY_OUTPUT_NEXT_KEY:
	case NOTIFY_OUTPUT_PREV_KEY:
		DPRINTF(("%s: event 0x%02x\n", DEVNAME(sc), notify));
		break;
	default:
		printf("%s: unknown event 0x%02x\n", DEVNAME(sc), notify);
		break;
	}

	return (0);
}

void
acpivideo_set_policy(struct acpivideo_softc *sc, int policy)
{
	struct aml_value args, res;

	memset(&args, 0, sizeof(args));
	args.v_integer = policy;
	args.type = AML_OBJTYPE_INTEGER;

	aml_evalname(sc->sc_acpi, sc->sc_devnode, "_DOS", 1, &args, &res);
	DPRINTF(("%s: set policy to %lld\n", DEVNAME(sc), aml_val2int(&res)));

	aml_freevalue(&res);
}

int
acpi_foundvout(struct aml_node *node, void *arg)
{
	struct acpivideo_softc *sc = (struct acpivideo_softc *)arg;
	struct device *self = (struct device *)arg;
	struct acpi_attach_args aaa;
	node = node->parent;

	DPRINTF(("Inside acpi_foundvout()\n"));
	if (node->parent != sc->sc_devnode)
		return (0);

	if (aml_searchname(node, "_BCM")) {
		memset(&aaa, 0, sizeof(aaa));
		aaa.aaa_iot = sc->sc_acpi->sc_iot;
		aaa.aaa_memt = sc->sc_acpi->sc_memt;
		aaa.aaa_node = node;
		aaa.aaa_name = "acpivout";

		config_found(self, &aaa, acpivideo_print);
	}

	return (0);
}

int
acpivideo_print(void *aux, const char *pnp)
{
	struct acpi_attach_args *aa = aux;

	if (pnp) {
		if (aa->aaa_name)
			printf("%s at %s", aa->aaa_name, pnp);
		else
			return (QUIET);
	}

	return (UNCONF);
}

int
acpivideo_getpcibus(struct acpivideo_softc *sc, struct aml_node *node)
{
	/* Check if parent device has PCI mapping */
	return (node->parent && node->parent->pci) ?
		node->parent->pci->sub : -1;
}
