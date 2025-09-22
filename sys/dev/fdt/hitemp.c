/*	$OpenBSD: hitemp.c,v 1.3 2022/06/28 23:43:12 naddy Exp $	*/
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
#define HI3660_OFFSET	0x040
#define HI3660_TEMP	0x01c
#define HI3660_TH	0x020
#define HI3660_LAG	0x028
#define HI3660_INT_EN	0x02c
#define HI3660_INT_CLR	0x030

#define HI3670_OFFSET	0x100
#define HI3670_INT_EN	0x064

#define HITEMP_NSENSORS	4

struct hitemp_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	bus_size_t		sc_offset;
	uint64_t		(*sc_calc_temp)(uint64_t);

	struct ksensor		sc_sensors[HITEMP_NSENSORS];
	struct ksensordev	sc_sensordev;
};

int	hitemp_match(struct device *, void *, void *);
void	hitemp_attach(struct device *, struct device *, void *);

const struct cfattach	hitemp_ca = {
	sizeof (struct hitemp_softc), hitemp_match, hitemp_attach
};

struct cfdriver hitemp_cd = {
	NULL, "hitemp", DV_DULL
};

struct hitemp_compat {
	const char *compat;
	bus_size_t offset;
	uint64_t (*calc_temp)(uint64_t);
};

uint64_t hi3660_calc_temp(uint64_t);
uint64_t hi3670_calc_temp(uint64_t);

const struct hitemp_compat hitemp_compat[] = {
	{
		"hsilicon,hi3660-tsensor",
		HI3660_OFFSET, hi3660_calc_temp
	},
	{
		"hisilicon,kirin970-tsensor",
		HI3670_OFFSET, hi3670_calc_temp
	}
};

void	hitemp_refresh_sensors(void *);

int
hitemp_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int i;

	for (i = 0; i < nitems(hitemp_compat); i++) {
		if (OF_is_compatible(faa->fa_node, hitemp_compat[i].compat))
			return 1;
	}

	return 0;
}

void
hitemp_attach(struct device *parent, struct device *self, void *aux)
{
	struct hitemp_softc *sc = (struct hitemp_softc *)self;
	struct fdt_attach_args *faa = aux;
	int i;

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

	for (i = 0; i < nitems(hitemp_compat); i++) {
		if (OF_is_compatible(faa->fa_node, hitemp_compat[i].compat)) {
			break;
		}
	}
	KASSERT(i < nitems(hitemp_compat));

	sc->sc_offset = hitemp_compat[i].offset;
	sc->sc_calc_temp = hitemp_compat[i].calc_temp;

	/* Register sensors. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	for (i = 0; i < HITEMP_NSENSORS; i++) {
		sc->sc_sensors[i].type = SENSOR_TEMP;
		sc->sc_sensors[i].flags = SENSOR_FINVALID;
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensors[i]);
	}
	sensordev_install(&sc->sc_sensordev);
	sensor_task_register(sc, hitemp_refresh_sensors, 5);
}

uint64_t
hi3660_calc_temp(uint64_t value)
{
	return 273150000 - 63780000 + value * 205000;
}

uint64_t
hi3670_calc_temp(uint64_t value)
{
	return 273150000 - 73720000 + value * 216000;
}

void
hitemp_refresh_sensors(void *arg)
{
	struct hitemp_softc *sc = arg;
	bus_size_t offset = 0;
	uint32_t value;
	uint64_t temp;
	int i;

	for (i = 0; i < HITEMP_NSENSORS; i++) {
		value = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    offset + HI3660_TEMP);
		temp = sc->sc_calc_temp(value);
		sc->sc_sensors[i].value = temp;
		sc->sc_sensors[i].flags &= ~SENSOR_FINVALID;
		offset += sc->sc_offset;
	}
}
