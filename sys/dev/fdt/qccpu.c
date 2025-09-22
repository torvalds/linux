/*	$OpenBSD: qccpu.c,v 1.4 2024/10/10 23:15:27 jsg Exp $	*/
/*
 * Copyright (c) 2023 Dale Rahn <drahn@openbsd.org>
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
#include <dev/ofw/fdt.h>

#define CPUF_ENABLE		0x000
#define CPUF_DOMAIN_STATE	0x020
#define  CPUF_DOMAIN_STATE_LVAL_M	0xff
#define  CPUF_DOMAIN_STATE_LVAL_S	0
#define CPUF_DVCS_CTRL		0x0b0
#define  CPUF_DVCS_CTRL_PER_CORE	0x1
#define CPUF_FREQ_LUT		0x100
#define  CPUF_FREQ_LUT_SRC_M		0x1
#define  CPUF_FREQ_LUT_SRC_S		30
#define  CPUF_FREQ_LUT_CORES_M		0x7
#define  CPUF_FREQ_LUT_CORES_S		16
#define  CPUF_FREQ_LUT_LVAL_M		0xff
#define  CPUF_FREQ_LUT_LVAL_S		0
#define CPUF_VOLT_LUT		0x200
#define  CPUF_VOLT_LUT_IDX_M		0x2f
#define  CPUF_VOLT_LUT_IDX_S		16
#define  CPUF_VOLT_LUT_VOLT_M		0xfff
#define  CPUF_VOLT_LUT_VOLT_S		0
#define CPUF_PERF_STATE		0x320
#define LUT_ROW_SIZE  		4

#define NUM_GROUP	2
#define MAX_LUT		40

#define XO_FREQ_HZ	19200000

struct qccpu_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh[NUM_GROUP];

	int			sc_node;

	struct clock_device	sc_cd;
	uint32_t		sc_freq[NUM_GROUP][MAX_LUT];
	int			sc_num_lut[NUM_GROUP];

	struct ksensordev       sc_sensordev;
	struct ksensor          sc_hz_sensor[NUM_GROUP];
};

#define DEVNAME(sc) (sc)->sc_dev.dv_xname

int	qccpu_match(struct device *, void *, void *);
void	qccpu_attach(struct device *, struct device *, void *);
int	qccpu_set_frequency(void *, uint32_t *, uint32_t);
uint32_t qccpu_get_frequency(void *, uint32_t *);
uint32_t qccpu_lut_to_freq(struct qccpu_softc *, int, uint32_t);
uint32_t qccpu_lut_to_cores(struct qccpu_softc *, int, uint32_t);
void	qccpu_refresh_sensor(void *arg);

void qccpu_collect_lut(struct qccpu_softc *sc, int);


const struct cfattach qccpu_ca = {
	sizeof (struct qccpu_softc), qccpu_match, qccpu_attach
};

struct cfdriver qccpu_cd = {
	NULL, "qccpu", DV_DULL
};

int
qccpu_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	return OF_is_compatible(faa->fa_node, "qcom,cpufreq-epss");
}

void
qccpu_attach(struct device *parent, struct device *self, void *aux)
{
	struct qccpu_softc *sc = (struct qccpu_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 2) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh[0])) {
		printf(": can't map registers (cluster0)\n");
		return;
	}

	if (bus_space_map(sc->sc_iot, faa->fa_reg[1].addr,
	    faa->fa_reg[1].size, 0, &sc->sc_ioh[1])) {
		printf(": can't map registers (cluster1)\n");
		return;
	}
	sc->sc_node = faa->fa_node;

	printf("\n");

	qccpu_collect_lut(sc, 0);
	qccpu_collect_lut(sc, 1);

	sc->sc_cd.cd_node = faa->fa_node;
	sc->sc_cd.cd_cookie = sc;
	sc->sc_cd.cd_get_frequency = qccpu_get_frequency;
	sc->sc_cd.cd_set_frequency = qccpu_set_frequency;
	clock_register(&sc->sc_cd);

	strlcpy(sc->sc_sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensordev.xname));

	sc->sc_hz_sensor[0].type = SENSOR_FREQ;
	sensor_attach(&sc->sc_sensordev, &sc->sc_hz_sensor[0]);
	sc->sc_hz_sensor[1].type = SENSOR_FREQ;
	sensor_attach(&sc->sc_sensordev, &sc->sc_hz_sensor[1]);
	sensordev_install(&sc->sc_sensordev);
	sensor_task_register(sc, qccpu_refresh_sensor, 1);
}

void
qccpu_collect_lut(struct qccpu_softc *sc, int group)
{
	int prev_freq = 0;
	uint32_t freq;
	int idx;
	bus_space_tag_t		iot = sc->sc_iot;
	bus_space_handle_t	ioh = sc->sc_ioh[group];

	for (idx = 0; ; idx++) {
		freq = bus_space_read_4(iot, ioh,
		    CPUF_FREQ_LUT + idx * LUT_ROW_SIZE);

		if (idx != 0 && prev_freq == freq) {
			sc->sc_num_lut[group] = idx;
			break;
		}

		sc->sc_freq[group][idx] = freq;

#ifdef DEBUG
		printf("%s: %d: %x %u\n", DEVNAME(sc), idx, freq,
		    qccpu_lut_to_freq(sc, idx, group));
#endif /* DEBUG */

		prev_freq = freq;
		if (idx >= MAX_LUT-1)
			break;
	}

	return;
}

uint32_t
qccpu_get_frequency(void *cookie, uint32_t *cells)
{
	struct qccpu_softc *sc = cookie;
	bus_space_tag_t		iot = sc->sc_iot;
	bus_space_handle_t	ioh;
	uint32_t		lval;
	uint32_t		group;

	if (cells[0] >= NUM_GROUP) {
		printf("%s: bad cell %d\n", __func__, cells[0]);
		return 0;
	}
	group = cells[0];

	ioh = sc->sc_ioh[cells[0]];

	lval = (bus_space_read_4(iot, ioh, CPUF_DOMAIN_STATE)
	    >> CPUF_DOMAIN_STATE_LVAL_S) & CPUF_DOMAIN_STATE_LVAL_M;
	return lval *XO_FREQ_HZ;
}

int
qccpu_set_frequency(void *cookie, uint32_t *cells, uint32_t freq)
{
	struct qccpu_softc *sc = cookie;
	bus_space_tag_t		iot = sc->sc_iot;
	bus_space_handle_t	ioh;
	int			index = 0;
	int			numcores, i;
	uint32_t		group;

	if (cells[0] >= NUM_GROUP) {
		printf("%s: bad cell %d\n", __func__, cells[0]);
		return 1;
	}
	group = cells[0];

	ioh = sc->sc_ioh[group];

	while (index < sc->sc_num_lut[group]) {
		if (freq == qccpu_lut_to_freq(sc, index, group))
			break;

		if (freq < qccpu_lut_to_freq(sc, index, group)) {
			/* select next slower if not match, not zero */
			if (index != 0)
				index = index - 1;
			break;
		}

		index++;
	}

#ifdef DEBUG
	printf("%s called freq %u index %d\n", __func__, freq, index);
#endif /* DEBUG */

	if ((bus_space_read_4(iot, ioh, CPUF_DVCS_CTRL) &
	    CPUF_DVCS_CTRL_PER_CORE) != 0)
		numcores = qccpu_lut_to_cores(sc, index, group);
	else
		numcores = 1;
	for (i = 0; i < numcores; i++)
		bus_space_write_4(iot, ioh, CPUF_PERF_STATE + i * 4, index);

	return 0;
}

uint32_t
qccpu_lut_to_freq(struct qccpu_softc *sc, int index, uint32_t group)
{
	return XO_FREQ_HZ *
	    ((sc->sc_freq[group][index] >> CPUF_FREQ_LUT_LVAL_S)
	     & CPUF_FREQ_LUT_LVAL_M);
}

uint32_t
qccpu_lut_to_cores(struct qccpu_softc *sc, int index, uint32_t group)
{
	return ((sc->sc_freq[group][index] >> CPUF_FREQ_LUT_CORES_S)
	    & CPUF_FREQ_LUT_CORES_M);
}

void
qccpu_refresh_sensor(void *arg)
{
        struct qccpu_softc *sc = arg;
	bus_space_tag_t		iot = sc->sc_iot;
	bus_space_handle_t	ioh;
	int		 idx;
	uint32_t	 lval;

	for (idx = 0; idx < NUM_GROUP; idx++) {
		ioh = sc->sc_ioh[idx];
		
		lval = (bus_space_read_4(iot, ioh, CPUF_DOMAIN_STATE)
		    >> CPUF_DOMAIN_STATE_LVAL_S) & CPUF_DOMAIN_STATE_LVAL_M;
		sc->sc_hz_sensor[idx].value = 1000000ULL * lval * XO_FREQ_HZ;
	}
}
