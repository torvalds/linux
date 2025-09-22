/* $OpenBSD: acpisony.c,v 1.10 2022/04/06 18:59:27 naddy Exp $ */
/*
 * Copyright (c) 2010 Paul Irofti <paul@irofti.net>
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

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <machine/apmvar.h>

int	acpisony_match(struct device *, void *, void *);
void	acpisony_attach(struct device *, struct device *, void *);
int	acpisony_activate(struct device *, int);
int	acpisony_notify(struct aml_node *, int, void *);

#ifdef ACPISONY_DEBUG
#define DPRINTF(x)	printf x
#else
#define DPRINTF(x)
#endif

/* Notifications */
#define	SONY_NOTIFY_FN_KEY			0x90

#define	SONY_NOTIFY_BRIGHTNESS_DOWN_PRESSED	0x85
#define	SONY_NOTIFY_BRIGHTNESS_DOWN_RELEASED	0x05
#define	SONY_NOTIFY_BRIGHTNESS_UP_PRESSED	0x86
#define	SONY_NOTIFY_BRIGHTNESS_UP_RELEASED	0x06

#define	SONY_NOTIFY_DISPLAY_SWITCH_PRESSED	0x87
#define	SONY_NOTIFY_DISPLAY_SWITCH_RELEASED	0x07

#define	SONY_NOTIFY_ZOOM_OUT_PRESSED		0x89
#define	SONY_NOTIFY_ZOOM_OUT_RELEASED		0x09

#define	SONY_NOTIFY_ZOOM_IN_PRESSED		0x8a
#define	SONY_NOTIFY_ZOOM_IN_RELEASED		0x0a

#define	SONY_NOTIFY_SUSPEND_PRESSED		0x8c
#define	SONY_NOTIFY_SUSPEND_RELEASED		0x0c

struct acpisony_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;
};

const struct cfattach acpisony_ca = {
	sizeof(struct acpisony_softc), acpisony_match, acpisony_attach,
	NULL, acpisony_activate
};

struct cfdriver acpisony_cd = {
	NULL, "acpisony", DV_DULL
};

void acpisony_notify_setup(struct acpisony_softc *);
int acpisony_set_hotkey(struct acpisony_softc *, int, int);
int acpisony_find_offset(struct acpisony_softc *, int);

void acpisony_brightness_down(struct acpisony_softc *);
int acpisony_get_brightness(struct acpisony_softc *);
void acpisony_set_brightness(struct acpisony_softc *, int);

int
acpisony_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aa = aux;
	struct cfdata		*cf = match;

	if (aa->aaa_name == NULL ||
	    strcmp(aa->aaa_name, cf->cf_driver->cd_name) != 0 ||
	    aa->aaa_table != NULL)
		return (0);

	return (1);
}

void
acpisony_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpisony_softc	*sc = (struct acpisony_softc *)self;
	struct acpi_attach_args *aa = aux;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;

	printf(": %s\n", sc->sc_devnode->name);

	/* Setup the notification masks */
	acpisony_notify_setup(sc);

	aml_register_notify(sc->sc_devnode, aa->aaa_dev,
	    acpisony_notify, sc, ACPIDEV_NOPOLL);
}

int
acpisony_activate(struct device *self, int act)
{
	struct acpisony_softc *sc = (struct acpisony_softc *)self;

	switch (act) {
	case DVACT_WAKEUP:
		acpisony_notify_setup(sc);
		break;
	}
	return 0;
}

int
acpisony_notify(struct aml_node *node, int notify, void *arg)
{
	struct acpisony_softc *sc = arg;
	int val, key = 0;

	if (notify == SONY_NOTIFY_FN_KEY) {
		notify -= 0x90;
		DPRINTF(("notify = %X", notify));

		if (notify == acpisony_find_offset(sc, 0x100)) {
			DPRINTF(("key = 0x100\n"));
			key = 0x100;
		}
		if (notify == acpisony_find_offset(sc, 0x127)) {
			DPRINTF(("key = 0x127\n"));
			key = 0x127;
		}

		if (key) {
			val = acpisony_set_hotkey(sc, key, 0x200);
			if (val < 0) {
				printf("returned val = %X", val);
				return 1;
			}
			notify = val & 0xff;

			DPRINTF(("Treat %X events, notify %X\n", key, notify));
		} else
			DPRINTF(("rfkill update, notify %X\n", notify));
	}

	switch (notify) {
	case SONY_NOTIFY_BRIGHTNESS_DOWN_PRESSED:
		DPRINTF(("br-down-pressed\n"));
		acpisony_brightness_down(sc);
		break;
	case SONY_NOTIFY_BRIGHTNESS_DOWN_RELEASED:
		DPRINTF(("br-down-released\n"));
		break;
	case SONY_NOTIFY_BRIGHTNESS_UP_PRESSED:
		DPRINTF(("br-up-pressed\n"));
		break;
	case SONY_NOTIFY_BRIGHTNESS_UP_RELEASED:
		DPRINTF(("br-up-released\n"));
		break;
	case SONY_NOTIFY_DISPLAY_SWITCH_PRESSED:
		DPRINTF(("display-pressed\n"));
		break;
	case SONY_NOTIFY_DISPLAY_SWITCH_RELEASED:
		DPRINTF(("display-released\n"));
		break;
	case SONY_NOTIFY_ZOOM_IN_PRESSED:
		DPRINTF(("zoom-in-pressed\n"));
		break;
	case SONY_NOTIFY_ZOOM_IN_RELEASED:
		DPRINTF(("zoom-in-released\n"));
		break;
	case SONY_NOTIFY_ZOOM_OUT_PRESSED:
		DPRINTF(("zoom-out-pressed\n"));
		break;
	case SONY_NOTIFY_ZOOM_OUT_RELEASED:
		DPRINTF(("zoom-out-released\n"));
		break;
	case SONY_NOTIFY_SUSPEND_PRESSED:
		DPRINTF(("suspend-pressed\n"));
#ifndef SMALL_KERNEL
		if (acpi_record_event(sc->sc_acpi, APM_USER_SUSPEND_REQ))
			acpi_addtask(sc->sc_acpi, acpi_sleep_task,
			    sc->sc_acpi, SLEEP_SUSPEND);
#endif
		break;
	case SONY_NOTIFY_SUSPEND_RELEASED:
		DPRINTF(("suspend-released\n"));
		break;
	default:
		printf("%s: unknown event 0x%02x\n", DEVNAME(sc), notify);
		break;
	}

	return 0;
}

void
acpisony_notify_setup(struct acpisony_softc *sc)
{
	struct aml_value arg;

	bzero(&arg, sizeof(arg));
	arg.type = AML_OBJTYPE_INTEGER;

	arg.v_integer = 1;
	aml_evalname(sc->sc_acpi, sc->sc_devnode, "ECON", 1, &arg, NULL);

	/* Enable all events */
	arg.v_integer = 0xffff;
	aml_evalname(sc->sc_acpi, sc->sc_devnode, "SN02", 1, &arg, NULL);

	/* Enable hotkeys */
	arg.v_integer = 0x04;
	aml_evalname(sc->sc_acpi, sc->sc_devnode, "SN02", 1, &arg, NULL);
	arg.v_integer = 0x02;
	aml_evalname(sc->sc_acpi, sc->sc_devnode, "SN07", 1, &arg, NULL);
	arg.v_integer = 0x10;
	aml_evalname(sc->sc_acpi, sc->sc_devnode, "SN02", 1, &arg, NULL);
	arg.v_integer = 0x00;
	aml_evalname(sc->sc_acpi, sc->sc_devnode, "SN07", 1, &arg, NULL);
	arg.v_integer = 0x02;
	aml_evalname(sc->sc_acpi, sc->sc_devnode, "SN03", 1, &arg, NULL);
	arg.v_integer = 0x101;
	aml_evalname(sc->sc_acpi, sc->sc_devnode, "SN07", 1, &arg, NULL);
}

int
acpisony_find_offset(struct acpisony_softc *sc, int key)
{
	struct aml_value arg, res;
	int val;

	bzero(&arg, sizeof(arg));
	arg.type = AML_OBJTYPE_INTEGER;

	for (arg.v_integer = 0x20; arg.v_integer < 0x30; arg.v_integer++) {
		aml_evalname(sc->sc_acpi, sc->sc_devnode, "SN00", 1, &arg, &res);
		val = aml_val2int(&res);
		aml_freevalue(&res);
		if (val == key) {
			DPRINTF(("Matched key %X\n", val));
			return arg.v_integer - 0x20;
		}
	}

	return -1;
}

int
acpisony_set_hotkey(struct acpisony_softc *sc, int key, int val)
{
	int off, rc = -1;
	struct aml_value res, arg;

	bzero(&arg, sizeof(arg));
	arg.type = AML_OBJTYPE_INTEGER;

	off = acpisony_find_offset(sc, key);
	DPRINTF(("off = %X\n", off));
	if (off < 0)
		return rc;

	arg.v_integer = off | val;
	aml_evalname(sc->sc_acpi, sc->sc_devnode, "SN07", 1, &arg, &res);
	rc = aml_val2int(&res);
	aml_freevalue(&res);

	return rc;
}

void
acpisony_brightness_down(struct acpisony_softc *sc)
{
	int val;

	val = acpisony_get_brightness(sc);
	DPRINTF(("current value = %X", val));
	if (val > 0)
		val--;
	else
		val = 0;
	DPRINTF(("next value = %X", val));
	acpisony_set_brightness(sc, val);
}

int
acpisony_get_brightness(struct acpisony_softc *sc)
{
	struct aml_value res;
	int val;

	aml_evalname(sc->sc_acpi, sc->sc_devnode, "GBRT", 0, NULL, &res);
	val = aml_val2int(&res);
	aml_freevalue(&res);

	return val;
}

void
acpisony_set_brightness(struct acpisony_softc *sc, int level)
{
	struct aml_value arg;

	bzero(&arg, sizeof(arg));
	arg.type = AML_OBJTYPE_INTEGER;
	arg.v_integer = level;
	aml_evalname(sc->sc_acpi, sc->sc_devnode, "SBRT", 1, &arg, NULL);
	aml_freevalue(&arg);
}
