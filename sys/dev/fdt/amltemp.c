/*	$OpenBSD: amltemp.c,v 1.2 2021/10/24 17:52:26 mpi Exp $	*/
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
#include <sys/systm.h>
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

/* Calibration */
#define TS_CALIB_VALID_MASK	0x8c000000
#define TS_CALIB_SIGN_MASK	0x00008000
#define TS_CALIB_VALUE_MASK	0x000003ff

/* Registers */
#define TS_CFG_REG1	0x01
#define  TS_CFG_REG1_ANA_EN_VCM	(1 << 10)
#define  TS_CFG_REG1_ANA_EN_VBG	(1 << 9)
#define  TS_CFG_REG1_FILTER_EN	(1 << 5)
#define  TS_CFG_REG1_DEM_EN	(1 << 3)
#define  TS_CFG_REG1_ANA_CH_SEL	3
#define TS_STAT0	0x10
#define  TS_STAT0_CODE_MASK	0xffff

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg) << 2))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg) << 2, (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct amltemp_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	int			sc_node;
	int32_t			sc_calib;

	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensordev;

	struct thermal_sensor	sc_ts;
};

int	amltemp_match(struct device *, void *, void *);
void	amltemp_attach(struct device *, struct device *, void *);

const struct cfattach	amltemp_ca = {
	sizeof (struct amltemp_softc), amltemp_match, amltemp_attach
};

struct cfdriver amltemp_cd = {
	NULL, "amltemp", DV_DULL
};

void	amltemp_attachhook(struct device *);
int32_t amltemp_calc_temp(struct amltemp_softc *, int32_t);
void	amltemp_refresh_sensors(void *);
int32_t	amltemp_get_temperature(void *, uint32_t *);

int
amltemp_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "amlogic,g12a-thermal");
}

void
amltemp_attach(struct device *parent, struct device *self, void *aux)
{
	struct amltemp_softc *sc = (struct amltemp_softc *)self;
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

	sc->sc_node = faa->fa_node;
	printf("\n");

	config_mountroot(self, amltemp_attachhook);
}

void
amltemp_attachhook(struct device *self)
{
	struct amltemp_softc *sc = (struct amltemp_softc *)self;
	struct regmap *rm;
	bus_addr_t offset;
	const char *name;
	uint32_t ao_secure;

	if (OF_is_compatible(sc->sc_node, "amlogic,g12a-cpu-thermal")) {
		offset = 0x128;
		name = "CPU";
	} else if (OF_is_compatible(sc->sc_node, "amlogic,g12a-ddr-thermal")) {
		offset = 0x0f0;
		name = "DDR";
	} else {
		printf("%s: unknown sensor\n", sc->sc_dev.dv_xname);
		return;
	}

	/* Extract calibration value. */
	ao_secure = OF_getpropint(sc->sc_node, "amlogic,ao-secure", 0);
	rm = regmap_byphandle(ao_secure);
	if (rm == NULL) {
		printf("%s: no calibration info\n", sc->sc_dev.dv_xname);
		return;
	}
	sc->sc_calib = regmap_read_4(rm, offset);
	if ((sc->sc_calib & TS_CALIB_VALID_MASK) == 0) {
		printf("%s: invalid calibration\n", sc->sc_dev.dv_xname);
		return;
	}
	if (sc->sc_calib & TS_CALIB_SIGN_MASK)
		sc->sc_calib = ~(sc->sc_calib & TS_CALIB_VALUE_MASK) + 1;
	else
		sc->sc_calib = (sc->sc_calib & TS_CALIB_VALUE_MASK);

	/* Enable hardware. */
	clock_enable_all(sc->sc_node);
	HSET4(sc, TS_CFG_REG1, TS_CFG_REG1_ANA_EN_VCM |
	    TS_CFG_REG1_ANA_EN_VBG | TS_CFG_REG1_FILTER_EN |
	    TS_CFG_REG1_DEM_EN | TS_CFG_REG1_ANA_CH_SEL);

	/* Register sensor. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	strlcpy(sc->sc_sensor.desc, name, sizeof(sc->sc_sensor.desc));
	sc->sc_sensor.type = SENSOR_TEMP;
	sc->sc_sensor.flags = SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
	sensordev_install(&sc->sc_sensordev);
	sensor_task_register(sc, amltemp_refresh_sensors, 5);

	sc->sc_ts.ts_node = sc->sc_node;
	sc->sc_ts.ts_cookie = sc;
	sc->sc_ts.ts_get_temperature = amltemp_get_temperature;
	thermal_sensor_register(&sc->sc_ts);
}

int32_t
amltemp_calc_temp(struct amltemp_softc *sc, int32_t code)
{
	const uint32_t A = 9411;
	const uint32_t B = 3159;
	const uint32_t m = 424;
	const uint32_t n = 324;
	int64_t tmp1, tmp2;
	
	tmp1 = (code * m) / 100;
	tmp2 = (code * n) / 100;
	tmp1 = (tmp1 * (1 << 16)) / ((1 << 16) + tmp2);
	tmp1 = ((tmp1 + sc->sc_calib) * A) / (1 << 16);
	return (tmp1 - B) * 100;
}

void
amltemp_refresh_sensors(void *arg)
{
	struct amltemp_softc *sc = arg;
	int32_t code, temp;

	code = HREAD4(sc, TS_STAT0);
	temp = amltemp_calc_temp(sc, code & TS_STAT0_CODE_MASK);

	sc->sc_sensor.value = 273150000 + 1000 * temp;
	sc->sc_sensor.flags &= ~SENSOR_FINVALID;
}

int32_t
amltemp_get_temperature(void *cookie, uint32_t *cells)
{
	struct amltemp_softc *sc = cookie;
	int32_t code;

	code = HREAD4(sc, TS_STAT0);
	return amltemp_calc_temp(sc, code & TS_STAT0_CODE_MASK);
}
