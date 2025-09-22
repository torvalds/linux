/*	$OpenBSD: bcm2835_temp.c,v 1.2 2021/10/24 17:52:26 mpi Exp $	*/
/*
 * Copyright (c) 2018 Mark Kettenis <kettenis@openbsd.org>
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
#include <dev/ofw/fdt.h>

/* Registers */
#define TS_TSENSCTL		0x0000
#define TS_TSENSSTAT		0x0004
#define  TS_TSENSSTAT_VALID	(1 << 10)
#define  TS_TSENSSTAT_DATA(x)	((x) & 0x3ff)

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct bcmtemp_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int32_t			sc_slope;
	int32_t			sc_offset;

	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensordev;
};

int	bcmtemp_match(struct device *, void *, void *);
void	bcmtemp_attach(struct device *, struct device *, void *);

const struct cfattach	bcmtemp_ca = {
	sizeof (struct bcmtemp_softc), bcmtemp_match, bcmtemp_attach
};

struct cfdriver bcmtemp_cd = {
	NULL, "bcmtemp", DV_DULL
};

void	bcmtemp_refresh_sensors(void *);

int
bcmtemp_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "brcm,bcm2836-thermal") ||
	    OF_is_compatible(faa->fa_node, "brcm,bcm2837-thermal"));
}

void
bcmtemp_attach(struct device *parent, struct device *self, void *aux)
{
	struct bcmtemp_softc *sc = (struct bcmtemp_softc *)self;
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

	/* XXX fetch these from /thermal-zones. */
	sc->sc_slope = -538;
	if (OF_is_compatible(faa->fa_node, "brcm,bcm2836-thermal"))
		sc->sc_offset = 407000;
	else
		sc->sc_offset = 412000;

	/* Register sensors. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sc->sc_sensor.type = SENSOR_TEMP;
	sc->sc_sensor.flags = SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
	sensordev_install(&sc->sc_sensordev);
	sensor_task_register(sc, bcmtemp_refresh_sensors, 5);
}

void
bcmtemp_refresh_sensors(void *arg)
{
	struct bcmtemp_softc *sc = arg;
	int32_t code, temp;

	code = HREAD4(sc, TS_TSENSSTAT);
	temp = sc->sc_offset + TS_TSENSSTAT_DATA(code) * sc->sc_slope;
	sc->sc_sensor.value = 273150000 + 1000 * temp;
	if (code & TS_TSENSSTAT_VALID)
		sc->sc_sensor.flags &= ~SENSOR_FINVALID;
	else
		sc->sc_sensor.flags |= SENSOR_FINVALID;
}
