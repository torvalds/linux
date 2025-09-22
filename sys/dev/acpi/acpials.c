/* $OpenBSD: acpials.c,v 1.4 2020/08/26 03:29:06 visa Exp $ */
/*
 * Ambient Light Sensor device driver
 * ACPI 5.0 spec section 9.2
 *
 * Copyright (c) 2016 joshua stein <jcs@openbsd.org>
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

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/acpidev.h>
#include <dev/acpi/amltypes.h>
#include <dev/acpi/dsdt.h>

#include <sys/sensors.h>

/* #define ACPIALS_DEBUG */

#ifdef ACPIALS_DEBUG
#define DPRINTF(x) printf x
#else
#define DPRINTF(x)
#endif

struct acpials_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct acpi_softc	*sc_acpi;
	struct aml_node		*sc_devnode;

	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensordev;
	struct sensor_task	*sc_sensor_task;
};

int	acpials_match(struct device *, void *, void *);
void	acpials_attach(struct device *, struct device *, void *);
int	acpials_read(struct acpials_softc *);
int	acpials_notify(struct aml_node *, int, void *);
void	acpials_addtask(void *);
void	acpials_update(void *, int);

const struct cfattach acpials_ca = {
	sizeof(struct acpials_softc),
	acpials_match,
	acpials_attach,
};

struct cfdriver acpials_cd = {
	NULL, "acpials", DV_DULL
};

const char *acpials_hids[] = {
	"ACPI0008",
	NULL
};

int
acpials_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aa = aux;
	struct cfdata *cf = match;

	/*
	 * Apple hardware will most likely have asmc(4) which also provides an
	 * illuminance sensor.
	 */
	if (hw_vendor != NULL && strncmp(hw_vendor, "Apple", 5) == 0)
		return 0;

	return (acpi_matchhids(aa, acpials_hids, cf->cf_driver->cd_name));
}

void
acpials_attach(struct device *parent, struct device *self, void *aux)
{
	struct acpials_softc *sc = (struct acpials_softc *)self;
	struct acpi_attach_args *aa = aux;
	int64_t st;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_devnode = aa->aaa_node;

	printf(": %s\n", sc->sc_devnode->name);

	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "_STA", 0, NULL, &st))
		st = STA_PRESENT | STA_ENABLED | STA_DEV_OK;
	if ((st & (STA_PRESENT | STA_ENABLED | STA_DEV_OK)) !=
	    (STA_PRESENT | STA_ENABLED | STA_DEV_OK))
		return;

	if (acpials_read(sc))
		return;

	strlcpy(sc->sc_sensordev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensordev.xname));
	strlcpy(sc->sc_sensor.desc, "ambient light sensor",
	    sizeof(sc->sc_sensor.desc));
	sc->sc_sensor.type = SENSOR_LUX;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);

	/*
	 * aml_register_notify with ACPIDEV_POLL is too slow (10 second
	 * intervals), so register the task with sensors so we can specify the
	 * interval, which will then just inject an acpi task and tell it to
	 * wakeup to handle the task.
	 */
	if (!(sc->sc_sensor_task = sensor_task_register(sc, acpials_addtask,
	    1))) {
		printf("%s: unable to register task\n", sc->sc_dev.dv_xname);
		return;
	}

	/*
	 * But also install an event handler in case AML Notify()s us of any
	 * large changes - 9.2.7
	 */
	aml_register_notify(sc->sc_devnode, aa->aaa_dev, acpials_notify,
	    sc, ACPIDEV_NOPOLL);

	sensordev_install(&sc->sc_sensordev);
}

int
acpials_read(struct acpials_softc *sc)
{
	int64_t	ali = 0;

	/* 9.2.2 - "Current ambient light illuminance reading in lux" */
	if (aml_evalinteger(sc->sc_acpi, sc->sc_devnode, "_ALI", 0, NULL,
	    &ali))
		return 1;

	sc->sc_sensor.value = (ali * 1000000);

	return 0;
}

int
acpials_notify(struct aml_node *node, int notify_type, void *arg)
{
	struct acpials_softc *sc = arg;

	DPRINTF(("%s: %s: %d\n", sc->sc_dev.dv_xname, __func__, notify_type));

	if (notify_type == 0x80)
		acpials_read(sc);

	return 0;
}

void
acpials_addtask(void *arg)
{
	struct acpials_softc *sc = arg;

	acpi_addtask(sc->sc_acpi, acpials_update, sc, 0);
	acpi_wakeup(sc->sc_acpi);
}

void
acpials_update(void *arg0, int arg1)
{
	struct acpials_softc *sc = arg0;

	if (acpials_read(sc) == 0) {
		DPRINTF(("%s: %s: %lld\n", sc->sc_dev.dv_xname, __func__,
		    sc->sc_sensor.value));
		sc->sc_sensor.flags &= ~SENSOR_FINVALID;
	} else
		sc->sc_sensor.flags |= SENSOR_FINVALID;
}
