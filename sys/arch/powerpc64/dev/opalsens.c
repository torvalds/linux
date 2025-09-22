/*	$OpenBSD: opalsens.c,v 1.3 2022/04/06 18:59:27 naddy Exp $	*/
/*
 * Copyright (c) 2020 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/device.h>
#include <sys/sensors.h>
#include <sys/systm.h>

#include <machine/fdt.h>
#include <machine/opal.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

struct opalsens_softc {
	struct device		sc_dev;
	uint32_t		sc_data;

	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensordev;
};

int	opalsens_match(struct device *, void *, void *);
void	opalsens_attach(struct device *, struct device *, void *);

const struct cfattach opalsens_ca = {
	sizeof (struct opalsens_softc), opalsens_match, opalsens_attach
};

struct cfdriver opalsens_cd = {
	NULL, "opalsens", DV_DULL
};

void	opalsens_refresh(void *);

int
opalsens_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "ibm,opal-sensor");
}

void
opalsens_attach(struct device *parent, struct device *self, void *aux)
{
	struct opalsens_softc *sc = (struct opalsens_softc *)self;
	struct fdt_attach_args *faa = aux;
	char name[32], type[32], label[32];

	sc->sc_data = OF_getpropint(faa->fa_node, "sensor-data", 0);

	name[0] = 0;
	OF_getprop(faa->fa_node, "name", name, sizeof(name));
	name[sizeof(name) - 1] = 0;

	printf(": \"%s\"", name);

	type[0] = 0;
	OF_getprop(faa->fa_node, "sensor-type", type, sizeof(type));
	type[sizeof(type) - 1] = 0;

	if (strcmp(type, "curr") == 0)
		sc->sc_sensor.type = SENSOR_AMPS;
	else if (strcmp(type, "energy") == 0)
		sc->sc_sensor.type = SENSOR_ENERGY;
	else if (strcmp(type, "in") == 0)
		sc->sc_sensor.type = SENSOR_VOLTS_DC;
	else if (strcmp(type, "power") == 0)
		sc->sc_sensor.type = SENSOR_WATTS;
	else if (strcmp(type, "temp") == 0)
		sc->sc_sensor.type = SENSOR_TEMP;
	else {
		printf(", unsupported sensor type \"%s\"\n", type);
		return;
	}

	label[0] = 0;
	OF_getprop(faa->fa_node, "label", label, sizeof(label));
	label[sizeof(label) - 1] = 0;

	strlcpy(sc->sc_sensor.desc, label, sizeof(sc->sc_sensor.desc));

	printf("\n");

	/* Register sensor. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sc->sc_sensor.flags = SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
	sensordev_install(&sc->sc_sensordev);
	sensor_task_register(sc, opalsens_refresh, 5);
}

void
opalsens_refresh(void *arg)
{
	struct opalsens_softc *sc = arg;
	uint64_t value;
	int64_t error;

	error = opal_sensor_read_u64(sc->sc_data, 0, opal_phys(&value));
	if (error == OPAL_SUCCESS)
		sc->sc_sensor.flags &= ~SENSOR_FINVALID;
	else
		sc->sc_sensor.flags |= SENSOR_FINVALID;

	switch (sc->sc_sensor.type) {
	case SENSOR_AMPS:
	case SENSOR_VOLTS_DC:
		sc->sc_sensor.value = value * 1000;
		break;
	case SENSOR_WATTS:
		sc->sc_sensor.value = value * 1000000;
		break;
	case SENSOR_TEMP:
		/* Firmware reports 0 for unpopulated DIMM slots. */
		if (value == 0)
			sc->sc_sensor.flags |= SENSOR_FINVALID;
		else
			sc->sc_sensor.value = 273150000 + value * 1000000;
		break;
	default:
		sc->sc_sensor.value = value;
		break;
	}
}
