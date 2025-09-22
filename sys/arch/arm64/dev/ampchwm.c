/*	$OpenBSD: ampchwm.c,v 1.1 2023/12/11 11:15:44 claudio Exp $ */
/*
 * Copyright (c) 2023 Claudio Jeker <claudio@openbsd.org>
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
#include <sys/types.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <sys/sensors.h>

#include <dev/acpi/acpireg.h>
#include <dev/acpi/acpivar.h>
#include <dev/acpi/amltypes.h>

int	ampchwm_match(struct device *, void *, void *);
void	ampchwm_attach(struct device *, struct device *, void *);

#define HWMON_ID			0x304d5748
#define HWMON_UNIT_CELSIUS		0x01
#define HWMON_UNIT_JOULES		0x10
#define HWMON_UNIT_MILIJOULES		0x11
#define HWMON_UNIT_MICROJOULES		0x12
#define HWMON_MAX_METRIC_COUNT		2

union metrics_hdr {
	uint64_t	data;
	struct {
		uint32_t	id;
		uint16_t	version;
		uint16_t	count;
	};
};

union metric_hdr {
	uint64_t	data[3];
	struct {
		char		label[16];
		uint8_t		unit;
		uint8_t		data_size;
		uint16_t	data_count;
		uint32_t	pad;
	};
};


struct ampchwm_softc {
	struct device			sc_dev;
	struct acpi_softc		*sc_acpi;
	struct aml_node			*sc_node;

	bus_space_tag_t			sc_iot;
	bus_space_handle_t		sc_ioh;
	size_t				sc_size;

	uint16_t			sc_count;
	struct {
		struct ksensor			*sc_sens;
		uint16_t			sc_sens_offset;
		uint16_t			sc_sens_count;
		uint16_t			sc_sens_size;
		uint16_t			sc_sens_unit;
	} 				sc_metrics[HWMON_MAX_METRIC_COUNT];

	struct ksensordev		sc_sensdev;
	struct sensor_task		*sc_sens_task;
};

const struct cfattach ampchwm_ca = {
	sizeof(struct ampchwm_softc), ampchwm_match, ampchwm_attach
};

struct cfdriver ampchwm_cd = {
	 NULL, "ampchwm", DV_DULL
};

const char *ampchwm_hids[] = {
	"AMPC0005",
	NULL
};

int	ampchwm_attach_sensors(struct ampchwm_softc *, int,
	    union metric_hdr *, uint16_t *);
void	ampchwm_refresh_sensors(void *);
void	ampchwm_update_sensor(struct ampchwm_softc *, int, int);


int
ampchwm_match(struct device *parent, void *match, void *aux)
{
	struct acpi_attach_args *aaa = aux;
	struct cfdata *cf = match;

	if (aaa->aaa_naddr < 1)
		return (0);
	return (acpi_matchhids(aaa, ampchwm_hids, cf->cf_driver->cd_name));
}

void
ampchwm_attach(struct device *parent, struct device *self, void *aux)
{
	struct ampchwm_softc	*sc = (struct ampchwm_softc *)self;
	struct acpi_attach_args *aaa = aux;
	union metrics_hdr hdr;
	union metric_hdr metric;
	uint16_t offset = 0;
	int i;

	sc->sc_acpi = (struct acpi_softc *)parent;
	sc->sc_node = aaa->aaa_node;

	printf(" %s", sc->sc_node->name);
	printf(" addr 0x%llx/0x%llx", aaa->aaa_addr[0], aaa->aaa_size[0]);

	sc->sc_iot = aaa->aaa_bst[0];
	sc->sc_size = aaa->aaa_size[0];

	if (bus_space_map(sc->sc_iot, aaa->aaa_addr[0], aaa->aaa_size[0],
	    0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	bus_space_read_region_8(sc->sc_iot, sc->sc_ioh, offset, &hdr.data, 1);

	if (hdr.id != HWMON_ID) {
		printf(": bad id %x\n", hdr.id);
		goto unmap;
	}

	printf(": ver %d", hdr.version);

	strlcpy(sc->sc_sensdev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sc_sensdev.xname));

	offset += sizeof(hdr);
	for (i = 0; i < hdr.count; i++) {
		bus_space_read_region_8(sc->sc_iot, sc->sc_ioh, offset,
		    metric.data, 3);
		if (ampchwm_attach_sensors(sc, i, &metric, &offset))
			goto unmap;
	}
	sc->sc_count = MIN(hdr.count, HWMON_MAX_METRIC_COUNT);

	sensordev_install(&sc->sc_sensdev);
	sc->sc_sens_task = sensor_task_register(sc, ampchwm_refresh_sensors, 1);
	printf("\n");

	return;
unmap:
	bus_space_unmap(sc->sc_iot, sc->sc_ioh, sc->sc_size);
	return;
}

int
ampchwm_attach_sensors(struct ampchwm_softc *sc, int num,
    union metric_hdr *metric, uint16_t *offsetp)
{
	uint16_t off = *offsetp;
	int i, count = 0;

	if (num >= HWMON_MAX_METRIC_COUNT) {
		if (num == HWMON_MAX_METRIC_COUNT)
			printf(" ignoring extra metrics");
		return 0;
	}

	off += sizeof(*metric);
	/* skip 0 values since those are disabled cores */
	for (i = 0; i < metric->data_count; i++) {
		if (bus_space_read_8(sc->sc_iot, sc->sc_ioh,
		    off + i * 8) == 0)
			continue;
		count++;
	}

	sc->sc_metrics[num].sc_sens = mallocarray(count,
	    sizeof(struct ksensor), M_DEVBUF, M_NOWAIT);
	if (sc->sc_metrics[num].sc_sens == NULL) {
		printf(" out of memory\n");
		return -1;
	}

	sc->sc_metrics[num].sc_sens_offset = off;
	sc->sc_metrics[num].sc_sens_count = count;
	sc->sc_metrics[num].sc_sens_unit = metric->unit;
	if (metric->data_size == 0)
		sc->sc_metrics[num].sc_sens_size = 8;
	else
		sc->sc_metrics[num].sc_sens_size = 4;

	for (i = 0; i < count; i++) {
		struct ksensor *s = &sc->sc_metrics[num].sc_sens[i];

		strlcpy(s->desc, metric->label, sizeof(s->desc));
		if (metric->unit == HWMON_UNIT_CELSIUS)
			s->type = SENSOR_TEMP;
		else
			s->type = SENSOR_ENERGY;
		sensor_attach(&sc->sc_sensdev, s);

		ampchwm_update_sensor(sc, num, i);
	}

	off += metric->data_count * 8;

	printf(", %d \"%s\"", count, metric->label);
	*offsetp = off;
	return 0;
}

void
ampchwm_refresh_sensors(void *arg)
{
	struct ampchwm_softc *sc = arg;
	int num, i;

	for (num = 0; num < sc->sc_count; num++)
		for (i = 0; i < sc->sc_metrics[num].sc_sens_count; i++)
			ampchwm_update_sensor(sc, num, i);
}

void
ampchwm_update_sensor(struct ampchwm_softc *sc, int num, int i)
{
	struct ksensor *s;
	uint64_t v;

	KASSERT(i < sc->sc_metrics[num].sc_sens_count);

	s = &sc->sc_metrics[num].sc_sens[i];
	if (sc->sc_metrics[num].sc_sens_size == 8) {
		v = bus_space_read_8(sc->sc_iot, sc->sc_ioh,
		    sc->sc_metrics[num].sc_sens_offset + i * sizeof(v));
	} else {
		v = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    sc->sc_metrics[num].sc_sens_offset + i * sizeof(v));
	}

	if (v == 0) {
		s->flags = SENSOR_FUNKNOWN;
		s->status = SENSOR_S_UNKNOWN;
	} else {
		s->flags = 0;
		s->status = SENSOR_S_OK;
	}

	switch (sc->sc_metrics[num].sc_sens_unit) {
	case HWMON_UNIT_CELSIUS:
		s->value = v * 1000 * 1000 + 273150000;
		break;
	case HWMON_UNIT_JOULES:
		v *= 1000;
	case HWMON_UNIT_MILIJOULES:
		v *= 1000;
	case HWMON_UNIT_MICROJOULES:
		s->value = v;
		break;
	}
}
