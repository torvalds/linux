/*	$OpenBSD: qctsens.c,v 1.1 2023/06/27 22:38:46 patrick Exp $	*/
/*
 * Copyright (c) 2023 Patrick Wildt <patrick@blueri.se>
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
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_thermal.h>
#include <dev/ofw/fdt.h>

/* Registers (sensor block) */
#define TSENS_Sn_STATUS(n)	(0xa0 + (4 * (n)))
#define  TSENS_Sn_VALID			(1 << 21)
#define  TSENS_Sn_TEMP(x)		((x) & 0xfff)

/* Registers (config block) */
#define TSENS_HW_VER		0x00
#define TSENS_CTRL		0x04
#define  TSENS_CTRL_EN			(1 << 0)
#define  TSENS_CTRL_Sn_EN(x)		(1 << ((x) + 3))

#define TSENS_NUM_SENSORS	16

struct qctsens_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_space_handle_t	sc_ioh_conf;

	int			sc_node;

	struct ksensordev	sc_sensordev;
	struct ksensor		sc_sensor[TSENS_NUM_SENSORS];

	struct thermal_sensor	sc_ts;
};

int	qctsens_match(struct device *, void *, void *);
void	qctsens_attach(struct device *, struct device *, void *);

const struct cfattach	qctsens_ca = {
	sizeof (struct qctsens_softc), qctsens_match, qctsens_attach
};

struct cfdriver qctsens_cd = {
	NULL, "qctsens", DV_DULL
};

void	qctsens_refresh_sensors(void *);
int32_t	qctsens_get_temperature(void *, uint32_t *);
void qctsens_attach_sensors(struct qctsens_softc *);

int
qctsens_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "qcom,tsens-v2");
}

void
qctsens_attach(struct device *parent, struct device *self, void *aux)
{
	struct qctsens_softc *sc = (struct qctsens_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t reg;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers (sensors)\n");
		return;
	}
	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_ioh_conf)) {
		printf(": can't map registers (config)\n");
		return;
	}

	printf("\n");

	reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh_conf, TSENS_CTRL);
	if ((reg & TSENS_CTRL_EN) == 0)
		return;

	qctsens_attach_sensors(sc);

	sc->sc_ts.ts_node = sc->sc_node;
	sc->sc_ts.ts_cookie = sc;
	sc->sc_ts.ts_get_temperature = qctsens_get_temperature;
	thermal_sensor_register(&sc->sc_ts);
}

void
qctsens_attach_sensors(struct qctsens_softc *sc)
{
	char nodename[32];
	uint32_t propdata[4];
	uint32_t phandle, reg;
	int node, len, sidx;

	phandle = OF_getpropint(sc->sc_node, "phandle", 0);
	if (phandle == 0) {
		printf("%s: missing phandle on node\n", sc->sc_dev.dv_xname);
		return;
	}

	reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh_conf, TSENS_CTRL);
	node = OF_getnodebyname(0, "thermal-zones");
	for (node = OF_child(node); node != 0; node = OF_peer(node)) {
		len = OF_getpropintarray(node, "thermal-sensors", propdata,
		    sizeof(propdata));

		if (len != 8 || propdata[0] != phandle || propdata[1] >= 16)
			continue;

		len = OF_getprop(node, "name", nodename, sizeof(nodename));
		len = strlen(nodename);
		if (strcmp("-thermal", &nodename[len - 8]) != 0)
			continue;

		nodename[len - 8] = '\0';
		sidx = propdata[1];

		if ((reg & TSENS_CTRL_Sn_EN(sidx)) == 0)
			continue;

		strlcpy(sc->sc_sensor[sidx].desc, nodename,
		    sizeof(sc->sc_sensor[sidx].desc));
		sc->sc_sensor[sidx].type = SENSOR_TEMP;
		sc->sc_sensor[sidx].flags = SENSOR_FINVALID;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[sidx]);
	}

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sensordev_install(&sc->sc_sensordev);
	sensor_task_register(sc, qctsens_refresh_sensors, 1);
}

void
qctsens_refresh_sensors(void *arg)
{
	struct qctsens_softc *sc = arg;
	int32_t reg, temp;
	int id;

	for (id = 0; id < TSENS_NUM_SENSORS; id++) {
		if (sc->sc_sensor[id].type != SENSOR_TEMP)
			continue;
		reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    TSENS_Sn_STATUS(id));
		temp = TSENS_Sn_TEMP(reg);
		if (reg & TSENS_Sn_VALID) {
			sc->sc_sensor[id].value = 273150000 + 100000 * temp;
			sc->sc_sensor[id].flags &= ~SENSOR_FINVALID;
		} else {
			sc->sc_sensor[id].flags = SENSOR_FINVALID;
		}
	}
}

int32_t
qctsens_get_temperature(void *cookie, uint32_t *cells)
{
	struct qctsens_softc *sc = cookie;
	uint32_t id = cells[0];
	int32_t reg, temp;

	reg = bus_space_read_4(sc->sc_iot, sc->sc_ioh, TSENS_Sn_STATUS(id));
	temp = 273150000 + 100000 * TSENS_Sn_TEMP(reg);

	if (reg & TSENS_Sn_VALID)
		return temp;

	return THERMAL_SENSOR_MAX;
}
