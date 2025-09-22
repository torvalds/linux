/*	$OpenBSD: imxtmu.c,v 1.3 2021/10/24 17:52:26 mpi Exp $	*/
/*
 * Copyright (c) 2018 Patrick Wildt <patrick@blueri.se>
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
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>
#include <dev/ofw/ofw_clock.h>

/* i.MX8MQ registers */
#define TMU_MQ_TMR				0x000
#define  TMU_MQ_TMR_ME					(1U << 31)
#define  TMU_MQ_TMR_ALPF				(0x3 << 26)
#define  TMU_MQ_TMR_SENSOR(x)				(1 << (15 - (x)))
#define TMU_MQ_TMTMIR				0x008
#define  TMU_MQ_TMTMIR_DEFAULT				0xf
#define TMU_MQ_TIER				0x020
#define TMU_MQ_TTCFGR				0x080
#define TMU_MQ_TSCFGR				0x084
#define TMU_MQ_TRITSR(x)			(0x100 + ((x) * 0x10))
#define TMU_MQ_TTR0CR				0xf10
#define TMU_MQ_TTR1CR				0xf14
#define TMU_MQ_TTR2CR				0xf18
#define TMU_MQ_TTR3CR				0xf1c

/* i.MX8MM registers */
#define TMU_MM_TER				0x000
#define  TMU_MM_TER_EN					(1U << 31)
#define TMU_MM_TRITSR				0x020
#define  TMU_MM_TRITSR_LOW_LIMIT			10

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

struct imxtmu_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	int			sc_sensorid;

	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensordev;
	struct timeout		sc_sensorto;
};

int	imxtmu_match(struct device *, void *, void *);
void	imxtmu_attach(struct device *, struct device *, void *);

void	imxtmu_mm_refresh_sensors(void *);
void	imxtmu_mq_refresh_sensors(void *);

const struct cfattach imxtmu_ca = {
	sizeof(struct imxtmu_softc), imxtmu_match, imxtmu_attach
};

struct cfdriver imxtmu_cd = {
	NULL, "imxtmu", DV_DULL
};

int
imxtmu_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "fsl,imx8mm-tmu") ||
	    OF_is_compatible(faa->fa_node, "fsl,imx8mq-tmu");
}

void
imxtmu_attach(struct device *parent, struct device *self, void *aux)
{
	struct imxtmu_softc *sc = (struct imxtmu_softc *)self;
	struct fdt_attach_args *faa = aux;
	uint32_t range[4], *calibration;
	int i, len;

	if (faa->fa_nreg < 1)
		return;

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	printf("\n");

	clock_enable_all(faa->fa_node);

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	strlcpy(sc->sc_sensor.desc, "core",
	    sizeof(sc->sc_sensor.desc));
	sc->sc_sensor.type = SENSOR_TEMP;
	sc->sc_sensor.flags = SENSOR_FINVALID;
	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);
	sensordev_install(&sc->sc_sensordev);

	if (OF_is_compatible(faa->fa_node, "fsl,imx8mm-tmu")) {
		HSET4(sc, TMU_MM_TER, TMU_MM_TER_EN);
		sensor_task_register(sc, imxtmu_mm_refresh_sensors, 5);
	}

	if (OF_is_compatible(faa->fa_node, "fsl,imx8mq-tmu")) {
		/*
		 * XXX: This thermal unit can show temperatures per node, and
		 * XXX: the thermal-zones can reference this.  But since we do
		 * XXX: not register ourselves with such a infrastructure we can
		 * XXX: live with just extracting sensor 0: the CPU.
		 */
		sc->sc_sensorid = 0;

		HWRITE4(sc, TMU_MQ_TIER, 0);
		HWRITE4(sc, TMU_MQ_TMTMIR, TMU_MQ_TMTMIR_DEFAULT);
		HWRITE4(sc, TMU_MQ_TMR, 0);

		if (OF_getpropintarray(faa->fa_node, "fsl,tmu-range", range,
		    sizeof(range)) != sizeof(range))
			return;

		HWRITE4(sc, TMU_MQ_TTR0CR, range[0]);
		HWRITE4(sc, TMU_MQ_TTR1CR, range[1]);
		HWRITE4(sc, TMU_MQ_TTR2CR, range[2]);
		HWRITE4(sc, TMU_MQ_TTR3CR, range[3]);

		len = OF_getproplen(faa->fa_node, "fsl,tmu-calibration");
		if (len <= 0 || (len % 8) != 0)
			return;

		calibration = malloc(len, M_TEMP, M_WAITOK);
		OF_getpropintarray(faa->fa_node, "fsl,tmu-calibration", calibration,
		    len);
		for (i = 0; i < (len / 4); i += 2) {
			HWRITE4(sc, TMU_MQ_TTCFGR, calibration[i + 0]);
			HWRITE4(sc, TMU_MQ_TSCFGR, calibration[i + 1]);
		}
		free(calibration, M_TEMP, len);

		HWRITE4(sc, TMU_MQ_TMR, TMU_MQ_TMR_SENSOR(sc->sc_sensorid) |
		    TMU_MQ_TMR_ME | TMU_MQ_TMR_ALPF);

		sensor_task_register(sc, imxtmu_mq_refresh_sensors, 5);
	}
}

void
imxtmu_mm_refresh_sensors(void *arg)
{
	struct imxtmu_softc *sc = (struct imxtmu_softc *)arg;
	uint32_t value;

	value = HREAD4(sc, TMU_MM_TRITSR);
	if (value < TMU_MM_TRITSR_LOW_LIMIT)
		return;
	value = (value & 0xff) * 1000000;

	sc->sc_sensor.value = value + 273150000;
	sc->sc_sensor.flags &= ~SENSOR_FINVALID;
}

void
imxtmu_mq_refresh_sensors(void *arg)
{
	struct imxtmu_softc *sc = (struct imxtmu_softc *)arg;
	uint32_t value;

	value = HREAD4(sc, TMU_MQ_TRITSR(sc->sc_sensorid));
	value = (value & 0xff) * 1000000;

	sc->sc_sensor.value = value + 273150000;
	sc->sc_sensor.flags &= ~SENSOR_FINVALID;
}
