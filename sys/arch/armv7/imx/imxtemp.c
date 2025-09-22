/*	$OpenBSD: imxtemp.c,v 1.8 2022/02/21 05:54:35 jsg Exp $	*/
/*
 * Copyright (c) 2014 Patrick Wildt <patrick@blueri.se>
 * Copyright (c) 2016 Mark Kettenis <kettenis@openbsd.org>
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
#include <sys/timeout.h>

#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>
#include <dev/ofw/ofw_misc.h>

/* registers */
#define TEMPMON_TEMPSENSE0			0x180
#define TEMPMON_TEMPSENSE0_SET			0x184
#define TEMPMON_TEMPSENSE0_CLR			0x188
#define TEMPMON_TEMPSENSE0_TOG			0x18c
#define TEMPMON_TEMPSENSE1			0x190
#define TEMPMON_TEMPSENSE1_SET			0x194
#define TEMPMON_TEMPSENSE1_CLR			0x198
#define TEMPMON_TEMPSENSE1_TOG			0x19c

/* bits and bytes */
#define TEMPMON_TEMPSENSE0_POWER_DOWN		(1 << 0)
#define TEMPMON_TEMPSENSE0_MEASURE_TEMP		(1 << 1)
#define TEMPMON_TEMPSENSE0_FINISHED		(1 << 2)
#define TEMPMON_TEMPSENSE0_TEMP_CNT_MASK	0xfff
#define TEMPMON_TEMPSENSE0_TEMP_CNT_SHIFT	8
#define TEMPMON_TEMPSENSE0_ALARM_VALUE_MASK	0xfff
#define TEMPMON_TEMPSENSE0_ALARM_VALUE_SHIFT	20

/* calibration registers */
#define OCOTP_ANA1				0x4e0

/* calibration bits and bytes */
#define OCOTP_ANA1_HOT_TEMP_MASK		0xff
#define OCOTP_ANA1_HOT_TEMP_SHIFT		0
#define OCOTP_ANA1_HOT_COUNT_MASK		0xfff
#define OCOTP_ANA1_HOT_COUNT_SHIFT		8
#define OCOTP_ANA1_ROOM_COUNT_MASK		0xfff
#define OCOTP_ANA1_ROOM_COUNT_SHIFT		20

#define HREAD4(sc, reg)							\
	regmap_read_4((sc)->sc_rm, (reg))
#define HWRITE4(sc, reg, val)						\
	regmap_write_4((sc)->sc_rm, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct imxtemp_softc {
	struct device		sc_dev;
	int			sc_node;
	struct regmap		*sc_rm;

	uint32_t		sc_hot_count;
	uint32_t		sc_hot_temp;
	uint32_t		sc_room_count;

	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensordev;
	struct timeout		sc_sensorto;
};

int	imxtemp_match(struct device *, void *, void *);
void	imxtemp_attach(struct device *, struct device *, void *);

const struct cfattach imxtemp_ca = {
	sizeof(struct imxtemp_softc), imxtemp_match, imxtemp_attach
};

struct cfdriver imxtemp_cd = {
	NULL, "imxtemp", DV_DULL
};

void	imxtemp_calibration(struct device *);
int32_t imxtemp_calc_temp(struct imxtemp_softc *, uint32_t);
void	imxtemp_refresh_sensors(void *);
void	imxtemp_pickup_sensors(void *);

int
imxtemp_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "fsl,imx6q-tempmon");
}

void
imxtemp_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxtemp_softc *sc = (struct imxtemp_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t phandle;

	sc->sc_node = faa->fa_node;
	phandle = OF_getpropint(sc->sc_node, "fsl,tempmon", 0);
	sc->sc_rm = regmap_byphandle(phandle);
	if (sc->sc_rm == NULL)
		return;

	printf("\n");

	config_mountroot(self, imxtemp_calibration);
}

void
imxtemp_calibration(struct device *self)
{
	struct imxtemp_softc *sc = (struct imxtemp_softc *)self;
	uint32_t calibration;
	uint32_t phandle;
	struct regmap *rm;

	phandle = OF_getpropint(sc->sc_node, "fsl,tempmon-data", 0);
	rm = regmap_byphandle(phandle);
	if (rm == NULL)
		return;

	calibration = regmap_read_4(rm, OCOTP_ANA1);
	sc->sc_hot_count = (calibration >> OCOTP_ANA1_HOT_COUNT_SHIFT) &
	    OCOTP_ANA1_HOT_COUNT_MASK;
	sc->sc_hot_temp = (calibration >> OCOTP_ANA1_HOT_TEMP_SHIFT) &
	    OCOTP_ANA1_HOT_TEMP_MASK;
	sc->sc_room_count = (calibration >> OCOTP_ANA1_ROOM_COUNT_SHIFT) &
	    OCOTP_ANA1_ROOM_COUNT_MASK;

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	strlcpy(sc->sc_sensor.desc, "core",
	    sizeof(sc->sc_sensor.desc));
	sc->sc_sensor.type = SENSOR_TEMP;
	sc->sc_sensor.flags = SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
	sensordev_install(&sc->sc_sensordev);
	timeout_set(&sc->sc_sensorto, imxtemp_pickup_sensors, sc);
	sensor_task_register(sc, imxtemp_refresh_sensors, 5);
}

int32_t
imxtemp_calc_temp(struct imxtemp_softc *sc, uint32_t temp_cnt)
{
	int32_t value;

	/*
	 * Calculate the calibrated temperature based on the equation
	 * provided in the i.MX6 reference manual:
	 *
	 * Tmeas = HOT_TEMP - (Nmeas - HOT_COUNT) * ((HOT_TEMP - 25.0) /
	 *   (ROOM_COUNT - HOT_COUNT))
	 *
	 * Note that we calculate the temperature in uC to avoid loss
	 * of precision.
	 */
	value = ((sc->sc_hot_temp - 25) * 1000000) /
	    (sc->sc_room_count - sc->sc_hot_count);
	value *= (temp_cnt - sc->sc_hot_count);
	return ((sc->sc_hot_temp * 1000000) - value);
}

void
imxtemp_refresh_sensors(void *arg)
{
	struct imxtemp_softc *sc = (struct imxtemp_softc *)arg;

	timeout_del(&sc->sc_sensorto);

	/* Power on temperature sensor. */
	HCLR4(sc, TEMPMON_TEMPSENSE0, TEMPMON_TEMPSENSE0_POWER_DOWN);
	HSET4(sc, TEMPMON_TEMPSENSE0, TEMPMON_TEMPSENSE0_MEASURE_TEMP);

	/* It may require up to ~17us to complete a measurement. */
	timeout_add_usec(&sc->sc_sensorto, 25);
}

void
imxtemp_pickup_sensors(void *arg)
{
	struct imxtemp_softc *sc = (struct imxtemp_softc *)arg;
	uint32_t value;
	uint32_t temp_cnt;

	value = HREAD4(sc, TEMPMON_TEMPSENSE0);

	/* Power down temperature sensor. */
	HCLR4(sc, TEMPMON_TEMPSENSE0, TEMPMON_TEMPSENSE0_MEASURE_TEMP);
	HSET4(sc, TEMPMON_TEMPSENSE0, TEMPMON_TEMPSENSE0_POWER_DOWN);

	if ((value & TEMPMON_TEMPSENSE0_FINISHED) == 0) {
		sc->sc_sensor.flags |= SENSOR_FINVALID;
		return;
	}

	temp_cnt = (value >> TEMPMON_TEMPSENSE0_TEMP_CNT_SHIFT) &
	    TEMPMON_TEMPSENSE0_TEMP_CNT_MASK;
	sc->sc_sensor.value = imxtemp_calc_temp(sc, temp_cnt) + 273150000;
	sc->sc_sensor.flags &= ~SENSOR_FINVALID;
}
