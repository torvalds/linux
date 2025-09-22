/*	$OpenBSD: environ.c,v 1.2 2021/10/24 17:05:03 mpi Exp $	*/

/*
 * Copyright (c) 2008 Mark Kettenis
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

/*
 * Driver for environment device on Enterprise 3000/4000/5000/6000
 * and Enterprise 3500/4500/5500/6500.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/sensors.h>
#include <sys/systm.h>

#include <machine/bus.h>
#include <machine/autoconf.h>

#include <sparc64/dev/fhcvar.h>

struct environ_softc {
	struct device		sc_dv;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	struct ksensor		sc_sensor;
	struct ksensordev	sc_sensordev;
};

int	environ_match(struct device *, void *, void *);
void	environ_attach(struct device *, struct device *, void *);
void	environ_refresh(void *);

const struct cfattach environ_ca = {
	sizeof(struct environ_softc), environ_match, environ_attach
};

struct cfdriver environ_cd = {
	NULL, "environ", DV_DULL
};

int
environ_match(struct device *parent, void *cf, void *aux)
{
	struct fhc_attach_args *fa = aux;

	if (strcmp("environment", fa->fa_name) == 0)
		return (1);
	return (0);
}

void
environ_attach(struct device *parent, struct device *self, void *aux)
{
	struct environ_softc *sc = (void *)self;
	struct fhc_attach_args *fa = aux;

	sc->sc_iot = fa->fa_bustag;
	if (fhc_bus_map(sc->sc_iot, fa->fa_reg[0].fbr_slot,
	    fa->fa_reg[0].fbr_offset, fa->fa_reg[0].fbr_size, 0,
	    &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	/* First reading allegedly gives garbage. */
	bus_space_read_1(sc->sc_iot, sc->sc_ioh, 0);
	delay(30);

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dv.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sc->sc_sensor.type = SENSOR_TEMP;

	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);

	if (sensor_task_register(sc, environ_refresh, 5) == NULL) {
		printf(": unable to register update task\n");
		return;
	}

	sensordev_install(&sc->sc_sensordev);

	printf("\n");
}

/*
 * Calibration table for 2nd generation CPU boards.
 */
int8_t environ_cpu2_temp[] = {
	-17, -16, -15, -14, -13, -12, -11, -10,
	 -9,  -8,  -7,  -6,  -5,  -4,  -3,  -2,
	 -1,   0,   1,   2,   3,   4,   5,   6,
	  7,   8,   9,  10,  11,  12,  13,  13,
	 14,  15,  16,  16,  17,  18,  18,  19,
	 20,  20,  21,  22,  22,  23,  24,  24,
	 25,  25,  26,  26,  27,  27,  28,  28,
	 29,  30,  30,  31,  31,  32,  32,  33,
	 33,  34,  34,  35,  35,  36,  36,  37,
	 37,  37,  38,  38,  39,  39,  40,  40,
	 41,  41,  42,  42,  43,  43,  43,  44,
	 44,  45,  45,  46,  46,  46,  47,  47,
	 48,  48,  49,  49,  50,  50,  50,  51,
	 51,  52,  52,  53,  53,  53,  54,  54,
	 55,  55,  56,  56,  56,  57,  57,  58,
	 58,  59,  59,  59,  60,  60,  61,  61,
	 62,  62,  63,  63,  63,  64,  64,  65,
	 65,  66,  66,  67,  67,  68,  68,  68,
	 69,  69,  70,  70,  71,  71,  72,  72,
	 73,  73,  74,  74,  75,  75,  76,  76,
	 77,  77,  78,  78,  79,  79,  80,  80,
	 81,  81,  82,  83,  83,  84,  84,  85,
	 85,  86,  87,  87,  88,  88,  89,  90,
	 90,  91,  92,  92,  93,  94,  94,  95,
	 96,  96,  97,  98,  99,  99, 100, 101,
	102, 103, 103, 104, 105, 106, 107, 108,
	109, 110
};

void
environ_refresh(void *arg)
{
	struct environ_softc *sc = arg;
	u_int8_t val;
	int8_t temp;

	val = bus_space_read_1(sc->sc_iot, sc->sc_ioh, 0);

	if (val == 0xff) {
		sc->sc_sensor.flags |= SENSOR_FINVALID;
		return;
	}

	if (val < sizeof(environ_cpu2_temp))
		temp = environ_cpu2_temp[val];
	else
		temp = 110;

	sc->sc_sensor.value = val * 1000000 + 273150000;
	sc->sc_sensor.flags &= ~SENSOR_FINVALID;
}
