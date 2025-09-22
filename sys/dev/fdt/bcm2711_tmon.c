/*
 * Copyright (c) 2020 Alastair Poole <netstar@gmail.com>
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
#include <dev/ofw/ofw_thermal.h>
#include <dev/ofw/fdt.h>

/* Registers */
#define  BCMTMON_TSENSSTAT		0x200
#define  BCMTMON_TSENSSTAT_VALID	(1 << 10)
#define  BCMTMON_TSENSSTAT_DATA(x)	((x) & 0x3ff)

#define HREAD4(sc, reg)	\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))

struct bcmtmon_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_size_t		sc_tsensstat;

	int32_t			sc_slope;
	int32_t			sc_offset;

	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensordev;

	struct thermal_sensor	sc_ts;
};

int	bcmtmon_match(struct device *, void *, void *);
void	bcmtmon_attach(struct device *, struct device *, void *);

const struct cfattach bcmtmon_ca = {
	sizeof (struct bcmtmon_softc), bcmtmon_match, bcmtmon_attach
};

struct cfdriver bcmtmon_cd = {
	NULL, "bcmtmon", DV_DULL
};

void	bcmtmon_refresh_sensors(void *);
int32_t	bcmtmon_get_temperature(void *, uint32_t *);

int
bcmtmon_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	if (OF_is_compatible(faa->fa_node, "brcm,bcm2711-avs-monitor") ||
	    OF_is_compatible(faa->fa_node, "brcm,avs-tmon-bcm2711") ||
	    OF_is_compatible(faa->fa_node, "brcm,avs-tmon-bcm2838"))
		return 10;	/* Must beat syscon(4). */

	return 0;
}

void
bcmtmon_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmtmon_softc *sc = (struct bcmtmon_softc *)self;
	struct fdt_attach_args *faa = aux;
	int node;

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

	if (OF_is_compatible(faa->fa_node, "brcm,bcm2711-avs-monitor"))
		sc->sc_tsensstat = BCMTMON_TSENSSTAT;

	sc->sc_slope = -487;
	sc->sc_offset = 410040;

	/* Register sensors. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sc->sc_sensor.type = SENSOR_TEMP;
	sc->sc_sensor.flags = SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
	sensordev_install(&sc->sc_sensordev);
	sensor_task_register(sc, bcmtmon_refresh_sensors, 5);

	for (node = OF_child(faa->fa_node); node; node = OF_peer(node)) {
		if (!OF_is_compatible(node, "brcm,bcm2711-thermal"))
			continue;
		sc->sc_ts.ts_node = node;
		sc->sc_ts.ts_cookie = sc;
		sc->sc_ts.ts_get_temperature = bcmtmon_get_temperature;
		thermal_sensor_register(&sc->sc_ts);
		/* We expect only a single sensor. */
		break;
	}
}

void
bcmtmon_refresh_sensors(void *arg)
{
	struct bcmtmon_softc *sc = arg;
	int32_t code, temp;

	code = HREAD4(sc, sc->sc_tsensstat);
	temp = sc->sc_slope * BCMTMON_TSENSSTAT_DATA(code) + sc->sc_offset;

	sc->sc_sensor.value = 273150000 + 1000 * temp;

	if (code & BCMTMON_TSENSSTAT_VALID)
		sc->sc_sensor.flags &= ~SENSOR_FINVALID;
	else
		sc->sc_sensor.flags |= SENSOR_FINVALID;
}

int32_t
bcmtmon_get_temperature(void *cookie, uint32_t *cells)
{
	struct bcmtmon_softc *sc = cookie;
	int32_t code, temp;

	code = HREAD4(sc, sc->sc_tsensstat);
	temp = sc->sc_slope * BCMTMON_TSENSSTAT_DATA(code) + sc->sc_offset;

	return temp;
}
