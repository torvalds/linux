/*	$OpenBSD: acpisurface.c,v 1.4 2025/06/16 20:21:33 kettenis Exp $	*/
/*
 * Copyright (c) 2018 Mike Larkin <mlarkin@openbsd.org>
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

#include "audio.h"
#include "wskbd.h"

/* #define ACPISURFACE_DEBUG */

#ifdef ACPISURFACE_DEBUG
#define DPRINTF(x...)   do { printf(x); } while(0)
#else
#define DPRINTF(x...)
#endif /* ACPISURFACE_DEBUG */

#define	SURFACE_ACCESSORY_REMOVED	0xC8
#define	SURFACE_WINDOWS_KEY_PRESSED	0xC4
#define SURFACE_WINDOWS_KEY_RELEASED	0xC5
#define SURFACE_VOLUME_UP_PRESSED	0xC0
#define SURFACE_VOLUME_UP_RELEASED	0xC1
#define SURFACE_VOLUME_DOWN_PRESSED	0xC2
#define SURFACE_VOLUME_DOWN_RELEASED	0xC3
#define SURFACE_POWER_BUTTON_PRESSED	0xC6
#define SURFACE_POWER_BUTTON_RELEASED	0xC7

struct acpisurface_softc {
	struct device		 sc_dev;

	struct acpiec_softc     *sc_ec;
	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;
};

int	surface_match(struct device *, void *, void *);
void	surface_attach(struct device *, struct device *, void *);
int	surface_hotkey(struct aml_node *, int, void *);

#if NAUDIO > 0 && NWSKBD > 0
extern int wskbd_set_mixervolume(long, long);
#endif

const struct cfattach acpisurface_ca = {
	sizeof(struct acpisurface_softc), surface_match, surface_attach,
	NULL, NULL
};

struct cfdriver acpisurface_cd = {
	NULL, "acpisurface", DV_DULL
};

const char *acpisurface_hids[] = {
	"MSHW0040",
	NULL
};

int
surface_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args	*aa = aux;
	struct cfdata *cf = match;

	if (!acpi_matchhids(aa, acpisurface_hids, cf->cf_driver->cd_name))
		return (0);

	return (1);
}

void
surface_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpisurface_softc *sc = (struct acpisurface_softc *)self;
	struct acpi_attach_args	*aa = aux;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;

	printf("\n");

	/* Run surface_hotkey on button presses */
	aml_register_notify(sc->sc_devnode, aa->aaa_dev,
	    surface_hotkey, sc, ACPIDEV_NOPOLL);
}

int
surface_hotkey(struct aml_node *node, int notify_type, void *arg)
{
	switch (notify_type) {
	case SURFACE_ACCESSORY_REMOVED:
		DPRINTF("%s: accessory removed\n", __func__);
		break;
	case SURFACE_VOLUME_UP_PRESSED:
		DPRINTF("%s: volume up pressed\n", __func__);
#if NAUDIO > 0 && NWSKBD > 0
		wskbd_set_mixervolume(1, 10);
#endif
		break;
	case SURFACE_VOLUME_UP_RELEASED:
		DPRINTF("%s: volume up released\n", __func__);
		break;
	case SURFACE_VOLUME_DOWN_PRESSED:
		DPRINTF("%s: volume down pressed\n", __func__);
#if NAUDIO > 0 && NWSKBD > 0
		wskbd_set_mixervolume(-1, 10);
#endif
		break;
	case SURFACE_VOLUME_DOWN_RELEASED:
		DPRINTF("%s: volume down released\n", __func__);
		break;
	case SURFACE_POWER_BUTTON_PRESSED:
		DPRINTF("%s: power button pressed\n", __func__);
		break;
	case SURFACE_POWER_BUTTON_RELEASED:
		DPRINTF("%s: power button released\n", __func__);
		powerbutton_event();
		break;
	case SURFACE_WINDOWS_KEY_PRESSED:
		DPRINTF("%s: windows key pressed\n", __func__);
		break;
	case SURFACE_WINDOWS_KEY_RELEASED:
		DPRINTF("%s: windows key released\n", __func__);
		break;
	default:
		DPRINTF("%s: unknown notification 0x%x\n", __func__,
		    notify_type);
	}

	return (0);
}
