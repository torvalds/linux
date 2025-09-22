/*	$OpenBSD: stftemp.c,v 1.3 2024/10/17 01:57:18 jsg Exp $	*/
/*
 * Copyright (c) 2022 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/sensors.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_thermal.h>
#include <dev/ofw/fdt.h>

/* Registers */
#define TEMP		0x0000
#define TEMP_PD		(1 << 1)
#define TEMP_RSTN	(1 << 0)
#define TEMP_RUN	(1 << 2)
#define TEMP_DOUT_MASK	0x0fff0000
#define TEMP_DOUT_SHIFT	16

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct stftemp_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensordev;

	struct thermal_sensor	sc_ts;
};

int	stftemp_match(struct device *, void *, void *);
void	stftemp_attach(struct device *, struct device *, void *);

const struct cfattach stftemp_ca = {
	sizeof (struct stftemp_softc), stftemp_match, stftemp_attach
};

struct cfdriver stftemp_cd = {
	NULL, "stftemp", DV_DULL
};

void	stftemp_refresh_sensors(void *);
int32_t	stftemp_get_temperature(void *, uint32_t *);

int
stftemp_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "starfive,jh7100-temp") ||
	    OF_is_compatible(faa->fa_node, "starfive,jh7110-temp");
}

void
stftemp_attach(struct device *parent, struct device *self, void *aux)
{
	struct stftemp_softc *sc = (struct stftemp_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	printf("\n");

	clock_enable(faa->fa_node, "bus");
	reset_deassert(faa->fa_node, "bus");

	clock_enable(faa->fa_node, "sense");
	reset_deassert(faa->fa_node, "sense");

	/* Power down */
	HWRITE4(sc, TEMP, TEMP_PD);
	delay(1);

	/* Power up with reset asserted */
	HWRITE4(sc, TEMP, 0);
	delay(60);

	/* Deassert reset */
	HWRITE4(sc, TEMP, TEMP_RSTN);
	delay(1);

	/* Start measuring */
	HWRITE4(sc, TEMP, TEMP_RSTN | TEMP_RUN);

	/* Register sensor */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sc->sc_sensor.type = SENSOR_TEMP;
	sc->sc_sensor.flags = SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
	sensordev_install(&sc->sc_sensordev);
	sensor_task_register(sc, stftemp_refresh_sensors, 5);

	sc->sc_ts.ts_node = faa->fa_node;
	sc->sc_ts.ts_cookie = sc;
	sc->sc_ts.ts_get_temperature = stftemp_get_temperature;
	thermal_sensor_register(&sc->sc_ts);
}

int32_t
stftemp_get_temp(struct stftemp_softc *sc)
{
	int32_t value;

	value = HREAD4(sc, TEMP);
	value = (value & TEMP_DOUT_MASK) >> TEMP_DOUT_SHIFT;

	return (value * 237500) / 4094 - 81100;
}

void
stftemp_refresh_sensors(void *arg)
{
	struct stftemp_softc *sc = arg;

	sc->sc_sensor.value = 273150000 + 1000 * stftemp_get_temp(sc);
	sc->sc_sensor.flags &= ~SENSOR_FINVALID;
}

int32_t
stftemp_get_temperature(void *cookie, uint32_t *cells)
{
	return stftemp_get_temp(cookie);
}
