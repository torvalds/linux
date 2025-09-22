/*	$OpenBSD: sxits.c,v 1.3 2021/10/24 17:52:27 mpi Exp $	*/
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_thermal.h>
#include <dev/ofw/fdt.h>

/* Registers */
#define TP_CTRL0			0x00
#define  TP_CTRL0_ADC_CLK_DIVIDER(x)	(((x) & 0x3) << 20)
#define  TP_CTRL0_FS_DIV(x)		(((x) & 0xf) << 16)
#define  TP_CTRL0_TACQ(x)		((x) & 0xffff)
#define TP_CTRL1			0x04
#define  TP_CTRL1_TP_MODE_EN		(1 << 4)
#define TP_CTRL3			0x0c
#define  TP_CTRL3_FILTER_EN		(1 << 2)
#define  TP_CTRL3_FILTER_TYPE(x)	((x) & 0x3)
#define TP_TPR				0x18
#define  TP_TPR_TEMP_EN			(1 << 16)
#define  TP_TPR_TEMP_PER(x)		((x) & 0xffff)
#define TEMP_DATA			0x20

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct sxits_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint16_t		sc_offset;
	uint16_t		sc_scale;

	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensordev;

	struct thermal_sensor	sc_ts;
};

int	sxits_match(struct device *, void *, void *);
void	sxits_attach(struct device *, struct device *, void *);

const struct cfattach	sxits_ca = {
	sizeof (struct sxits_softc), sxits_match, sxits_attach
};

struct cfdriver sxits_cd = {
	NULL, "sxits", DV_DULL
};

void	sxits_refresh_sensors(void *);
int32_t sxits_get_temperature(void *, uint32_t *);

int
sxits_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "allwinner,sun4i-a10-ts") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun5i-a13-ts"));
}

void
sxits_attach(struct device *parent, struct device *self, void *aux)
{
	struct sxits_softc *sc = (struct sxits_softc *)self;
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

	if (OF_is_compatible(faa->fa_node, "allwinner,sun4i-a10-ts")) {
		sc->sc_offset = 1932;
		sc->sc_scale = 133;
	} else {
		sc->sc_offset = 1447;
		sc->sc_scale = 100;
	}

	/* Start data acquisition. */
	HWRITE4(sc, TP_CTRL0, TP_CTRL0_ADC_CLK_DIVIDER(2) |
	    TP_CTRL0_FS_DIV(7) | TP_CTRL0_TACQ(63));
	HWRITE4(sc, TP_CTRL1, TP_CTRL1_TP_MODE_EN);
	HWRITE4(sc, TP_CTRL3, TP_CTRL3_FILTER_EN | TP_CTRL3_FILTER_TYPE(1));
	HWRITE4(sc, TP_TPR, TP_TPR_TEMP_EN | TP_TPR_TEMP_PER(800));

	/* Register sensors. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sc->sc_sensor.type = SENSOR_TEMP;
	sc->sc_sensor.flags = SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
	sensordev_install(&sc->sc_sensordev);
	sensor_task_register(sc, sxits_refresh_sensors, 5);

	sc->sc_ts.ts_node = faa->fa_node;
	sc->sc_ts.ts_cookie = sc;
	sc->sc_ts.ts_get_temperature = sxits_get_temperature;
	thermal_sensor_register(&sc->sc_ts);
}

void
sxits_refresh_sensors(void *arg)
{
	struct sxits_softc *sc = arg;
	uint32_t data, temp;

	data = HREAD4(sc, TEMP_DATA);
	if (data == 0) {
		sc->sc_sensor.flags |= SENSOR_FINVALID;
		return;
	}

	temp = (data - sc->sc_offset) * sc->sc_scale;
	sc->sc_sensor.value = temp * 1000 + 273150000;
	sc->sc_sensor.flags &= ~SENSOR_FINVALID;
}

int32_t
sxits_get_temperature(void *cookie, uint32_t *cells)
{
	struct sxits_softc *sc = cookie;
	uint32_t data;

	data = HREAD4(sc, TEMP_DATA);
	if (data == 0)
		return THERMAL_SENSOR_MAX;

	return (data - sc->sc_offset) * sc->sc_scale;
}
