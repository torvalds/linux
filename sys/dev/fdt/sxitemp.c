/*	$OpenBSD: sxitemp.c,v 1.9 2021/10/24 17:52:27 mpi Exp $	*/
/*
 * Copyright (c) 2017 Mark Kettenis <kettenis@openbsd.org>
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
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/ofw_misc.h>
#include <dev/ofw/ofw_pinctrl.h>
#include <dev/ofw/ofw_thermal.h>
#include <dev/ofw/fdt.h>

/* Registers */
#define THS_CTRL0			0x0000
#define  THS_CTRL0_SENSOR_ACQ(x)	((x) & 0xffff)
#define THS_CTRL2			0x0040
#define  THS_CTRL2_ADC_ACQ(x)		(((x) & 0xffff) << 16)
#define  THS_CTRL2_SENSE2_EN		(1 << 2)
#define  THS_CTRL2_SENSE1_EN		(1 << 1)
#define  THS_CTRL2_SENSE0_EN		(1 << 0)
#define THS_INT_CTRL			0x0044
#define  THS_INT_CTRL_THERMAL_PER(x)	(((x) & 0xfffff) << 12)
#define  THS_INT_CTRL_THS0_DATA_IRQ_EN	(1 << 8)
#define  THS_INT_CTRL_THS1_DATA_IRQ_EN	(1 << 9)
#define  THS_INT_CTRL_THS2_DATA_IRQ_EN	(1 << 10)
#define THS_STAT			0x0048
#define  THS_STAT_THS0_DATA_IRQ_STS	(1 << 8)
#define  THS_STAT_THS1_DATA_IRQ_STS	(1 << 9)
#define  THS_STAT_THS2_DATA_IRQ_STS	(1 << 10)
#define THS_FILTER			0x0070
#define  THS_FILTER_EN			(1 << 2)
#define  THS_FILTER_TYPE(x)		((x) & 0x3)
#define THS0_1_CDATA			0x0074
#define THS2_CDATA			0x0078
#define THS0_DATA			0x0080
#define THS1_DATA			0x0084
#define THS2_DATA			0x0088

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct sxitemp_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	void			*sc_ih;

	uint64_t		(*sc_calc_temp0)(int64_t);
	uint64_t		(*sc_calc_temp1)(int64_t);
	uint64_t		(*sc_calc_temp2)(int64_t);

	struct ksensor		sc_sensors[3];
	struct ksensordev	sc_sensordev;

	struct thermal_sensor	sc_ts;
};

int	sxitemp_match(struct device *, void *, void *);
void	sxitemp_attach(struct device *, struct device *, void *);

const struct cfattach	sxitemp_ca = {
	sizeof (struct sxitemp_softc), sxitemp_match, sxitemp_attach
};

struct cfdriver sxitemp_cd = {
	NULL, "sxitemp", DV_DULL
};

void	sxitemp_setup_calib(struct sxitemp_softc *, int);
int	sxitemp_intr(void *);
uint64_t sxitemp_h3_calc_temp(int64_t);
uint64_t sxitemp_r40_calc_temp(int64_t);
uint64_t sxitemp_a64_calc_temp(int64_t);
uint64_t sxitemp_h5_calc_temp0(int64_t);
uint64_t sxitemp_h5_calc_temp1(int64_t);
void	sxitemp_refresh_sensors(void *);
int32_t sxitemp_get_temperature(void *, uint32_t *);

int
sxitemp_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return (OF_is_compatible(faa->fa_node, "allwinner,sun8i-h3-ths") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun8i-r40-ths") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-a64-ths") ||
	    OF_is_compatible(faa->fa_node, "allwinner,sun50i-h5-ths"));
}

void
sxitemp_attach(struct device *parent, struct device *self, void *aux)
{
	struct sxitemp_softc *sc = (struct sxitemp_softc *)self;
	struct fdt_attach_args *faa = aux;
	int node = faa->fa_node;
	uint32_t enable, irq;

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

	sc->sc_ih = fdt_intr_establish(faa->fa_node, IPL_SOFTCLOCK,
	    sxitemp_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish interrupt\n");
		return;
	}

	printf("\n");

	pinctrl_byname(node, "default");

	clock_enable_all(node);
	reset_deassert_all(node);

	if (OF_is_compatible(faa->fa_node, "allwinner,sun8i-h3-ths")) {
		sc->sc_calc_temp0 = sxitemp_h3_calc_temp;
	} else if (OF_is_compatible(faa->fa_node, "allwinner,sun8i-r40-ths")) {
		sc->sc_calc_temp0 = sxitemp_r40_calc_temp;
		sc->sc_calc_temp1 = sxitemp_r40_calc_temp;
	} else if (OF_is_compatible(faa->fa_node, "allwinner,sun50i-a64-ths")) {
		sc->sc_calc_temp0 = sxitemp_a64_calc_temp;
		sc->sc_calc_temp1 = sxitemp_a64_calc_temp;
		sc->sc_calc_temp2 = sxitemp_a64_calc_temp;
	} else {
		sc->sc_calc_temp0 = sxitemp_h5_calc_temp0;
		sc->sc_calc_temp1 = sxitemp_h5_calc_temp1;
	}

	enable = irq = 0;
	if (sc->sc_calc_temp0) {
		enable |= THS_CTRL2_SENSE0_EN;
		irq |= THS_INT_CTRL_THS0_DATA_IRQ_EN;
	}
	if (sc->sc_calc_temp1) {
		enable |= THS_CTRL2_SENSE1_EN;
		irq |= THS_INT_CTRL_THS1_DATA_IRQ_EN;
	}
	if (sc->sc_calc_temp2) {
		enable |= THS_CTRL2_SENSE2_EN;
		irq |= THS_INT_CTRL_THS2_DATA_IRQ_EN;
	}

	sxitemp_setup_calib(sc, node);

	/* Start data acquisition. */
	HWRITE4(sc, THS_FILTER, THS_FILTER_EN | THS_FILTER_TYPE(1));
	HWRITE4(sc, THS_INT_CTRL, THS_INT_CTRL_THERMAL_PER(800) | irq);
	HWRITE4(sc, THS_CTRL0, THS_CTRL0_SENSOR_ACQ(31));
	HWRITE4(sc, THS_CTRL2, THS_CTRL2_ADC_ACQ(31) | enable);

	/* Register sensors. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	if (sc->sc_calc_temp0) {
		strlcpy(sc->sc_sensors[0].desc, "CPU",
		    sizeof(sc->sc_sensors[0].desc));
		sc->sc_sensors[0].type = SENSOR_TEMP;
		sc->sc_sensors[0].flags = SENSOR_FINVALID;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[0]);
	}
	if (sc->sc_calc_temp1) {
		strlcpy(sc->sc_sensors[1].desc, "GPU",
		    sizeof(sc->sc_sensors[1].desc));
		sc->sc_sensors[1].type = SENSOR_TEMP;
		sc->sc_sensors[1].flags = SENSOR_FINVALID;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[1]);
	}
	if (sc->sc_calc_temp2) {
		strlcpy(sc->sc_sensors[2].desc, "",
		    sizeof(sc->sc_sensors[2].desc));
		sc->sc_sensors[2].type = SENSOR_TEMP;
		sc->sc_sensors[2].flags = SENSOR_FINVALID;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[2]);
	}
	sensordev_install(&sc->sc_sensordev);
	sensor_task_register(sc, sxitemp_refresh_sensors, 5);

	sc->sc_ts.ts_node = node;
	sc->sc_ts.ts_cookie = sc;
	sc->sc_ts.ts_get_temperature = sxitemp_get_temperature;
	thermal_sensor_register(&sc->sc_ts);
}

void
sxitemp_setup_calib(struct sxitemp_softc *sc, int node)
{
	uint32_t calib[2];
	bus_size_t size = sizeof(calib);

	/*
	 * The size of the calibration data depends on the number of
	 * sensors.  Instead of trying to be clever, just try the
	 * possible sizes.
	 */
	while (size > 0) {
		if (nvmem_read_cell(node, "calibration", &calib, size) == 0)
			break;
		size -= sizeof(calib[0]);
	}

	if (size > 0)
		HWRITE4(sc, THS0_1_CDATA, calib[0]);
	if (size > 4)
		HWRITE4(sc, THS2_CDATA, calib[1]);
}

int
sxitemp_intr(void *arg)
{
	struct sxitemp_softc *sc = arg;
	uint32_t cell, stat;
	int rc = 0;

	stat = HREAD4(sc, THS_STAT);
	HWRITE4(sc, THS_STAT, stat);

	if (stat & THS_STAT_THS0_DATA_IRQ_STS) {
		cell = 0;
		thermal_sensor_update(&sc->sc_ts, &cell);
		rc = 1;
	}
	if (stat & THS_STAT_THS1_DATA_IRQ_STS) {
		cell = 1;
		thermal_sensor_update(&sc->sc_ts, &cell);
		rc = 1;
	}
	if (stat & THS_STAT_THS2_DATA_IRQ_STS) {
		cell = 2;
		thermal_sensor_update(&sc->sc_ts, &cell);
		rc = 1;
	}

	return rc;
}

uint64_t
sxitemp_h3_calc_temp(int64_t data)
{
	/* From BSP since the H3 Data Sheet isn't accurate. */
	return 217000000 - data * 1000000000 / 8253;
}

uint64_t
sxitemp_r40_calc_temp(int64_t data)
{
	/* From BSP as the R40 User Manual says T.B.D. */
	return -112500 * data + 250000000;
}

uint64_t
sxitemp_a64_calc_temp(int64_t data)
{
	/* From BSP as the A64 User Manual isn't correct. */
	return (2170000000000 - data * 1000000000) / 8560;
}

uint64_t
sxitemp_h5_calc_temp0(int64_t data)
{
	if (data > 0x500)
		return -119100 * data + 223000000;
	else
		return -145200 * data + 259000000;
}

uint64_t
sxitemp_h5_calc_temp1(int64_t data)
{
	if (data > 0x500)
		return -119100 * data + 223000000;
	else
		return -159000 * data + 276000000;
}

void
sxitemp_refresh_sensors(void *arg)
{
	struct sxitemp_softc *sc = arg;
	uint32_t data;

	if (sc->sc_calc_temp0) {
		data = HREAD4(sc, THS0_DATA);
		sc->sc_sensors[0].value = sc->sc_calc_temp0(data) + 273150000;
		sc->sc_sensors[0].flags &= ~SENSOR_FINVALID;
	}

	if (sc->sc_calc_temp1) {
		data = HREAD4(sc, THS1_DATA);
		sc->sc_sensors[1].value = sc->sc_calc_temp1(data) + 273150000;
		sc->sc_sensors[1].flags &= ~SENSOR_FINVALID;
	}

	if (sc->sc_calc_temp2) {
		data = HREAD4(sc, THS2_DATA);
		sc->sc_sensors[2].value = sc->sc_calc_temp2(data) + 273150000;
		sc->sc_sensors[2].flags &= ~SENSOR_FINVALID;
	}
}

int32_t
sxitemp_get_temperature(void *cookie, uint32_t *cells)
{
	struct sxitemp_softc *sc = cookie;
	uint32_t idx = cells[0];
	uint32_t data;

	if (idx == 0 && sc->sc_calc_temp0) {
		data = HREAD4(sc, THS0_DATA);
		return sc->sc_calc_temp0(data) / 1000;
	} else if (idx == 1 && sc->sc_calc_temp1) {
		data = HREAD4(sc, THS1_DATA);
		return sc->sc_calc_temp1(data) / 1000;
	} else if (idx == 2 && sc->sc_calc_temp2) {
		data = HREAD4(sc, THS2_DATA);
		return sc->sc_calc_temp2(data) / 1000;
	}

	return THERMAL_SENSOR_MAX;
}
