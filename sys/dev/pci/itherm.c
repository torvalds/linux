/*
 * Copyright (c) 2010 Mike Larkin <mlarkin@openbsd.org>
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
 * Intel 3400 thermal sensor controller driver
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <dev/pci/pcireg.h>
#include <dev/pci/pcivar.h>
#include <dev/pci/pcidevs.h>

/*
 * Intel 5 series (3400) Thermal Sensor Data
 * See Intel document 322169-004 (January 2012)
 */
#define	ITHERM_NUM_SENSORS		12
#define	ITHERM_SENSOR_THERMOMETER	0
#define	ITHERM_SENSOR_CORETEMP1		1
#define	ITHERM_SENSOR_CORETEMP2		2
#define	ITHERM_SENSOR_COREENERGY	3
#define	ITHERM_SENSOR_GPUTEMP		4
#define	ITHERM_SENSOR_MAXPROCTEMP	5
#define	ITHERM_SENSOR_DIMMTEMP1		6
#define	ITHERM_SENSOR_DIMMTEMP2		7
#define	ITHERM_SENSOR_DIMMTEMP3		8
#define	ITHERM_SENSOR_DIMMTEMP4		9
#define	ITHERM_SENSOR_GPUTEMP_ABSOLUTE	10
#define	ITHERM_SENSOR_PCHTEMP_ABSOLUTE	11

/* Section 22.2 of datasheet */
#define	ITHERM_TSE	0x1	/* TS enable */
#define	ITHERM_TSTR	0x3	/* TS thermometer read */
#define	ITHERM_TRC	0x1A	/* TS reporting control */
#define	ITHERM_CTV1	0x30	/* TS core temp value 1 */
#define	ITHERM_CTV2	0x32	/* TS core temp value 2 */
#define	ITHERM_CEV1	0x34	/* TS core energy value 1 */
#define	ITHERM_MGTV	0x58	/* mem/GPU temp value */
#define	ITHERM_PTV	0x60	/* TS CPU temp value */
#define	ITHERM_DTV	0xAC	/* DIMM temp values */
#define	ITHERM_ITV	0xD8	/* Internal temp values */

#define	ITHERM_TEMP_READ_ENABLE		0xFF
#define	ITHERM_TDR_ENABLE		0x1000
#define	ITHERM_SECOND_CORE_ENABLE	0x8000

#define	ITHERM_TSE_ENABLE	0xB8	/* magic number in datasheet */

#define	ITHERM_CTV_INVALID	0x8000
#define	ITHERM_CTV_INT_MASK	0x3FC0	/* higher 8 bits */
#define	ITHERM_CTV_FRAC_MASK	0x003F	/* lower 6 bits */

#define	ITHERM_REFRESH_INTERVAL 5

struct itherm_softc {
	struct device		sc_dev;

	bus_addr_t		sc_addr;
	bus_space_tag_t		iot;
	bus_space_handle_t	ioh;
	bus_size_t		size;

	int64_t			energy_prev;

	struct ksensor sensors[ITHERM_NUM_SENSORS];
	struct ksensordev sensordev;
	void (*refresh_sensor_data)(struct itherm_softc *);
};

#define IREAD1(sc, a) bus_space_read_1((sc)->iot, (sc)->ioh, (a))
#define IREAD2(sc, a) bus_space_read_2((sc)->iot, (sc)->ioh, (a))
#define IREAD4(sc, a) bus_space_read_4((sc)->iot, (sc)->ioh, (a))
#define IWRITE1(sc, a, x) bus_space_write_1((sc)->iot, (sc)->ioh, (a), (x))
#define IWRITE2(sc, a, x) bus_space_write_2((sc)->iot, (sc)->ioh, (a), (x))

int  itherm_probe(struct device *, void *, void *);
void itherm_attach(struct device *, struct device *, void *);
void itherm_refresh(void *);
void itherm_enable(struct itherm_softc *);
void itherm_refresh_sensor_data(struct itherm_softc *);
int  itherm_activate(struct device *, int);
void itherm_bias_temperature_sensor(struct ksensor *);

const struct pci_matchid itherm_devices[] = {
	{ PCI_VENDOR_INTEL, PCI_PRODUCT_INTEL_3400_THERMAL }
};

struct cfdriver itherm_cd = {
	NULL, "itherm", DV_DULL
};

const struct cfattach itherm_ca = {
	sizeof(struct itherm_softc), itherm_probe, itherm_attach, NULL,
	itherm_activate
};

int
itherm_probe(struct device *parent, void *match, void *aux)
{
	return (pci_matchbyid((struct pci_attach_args *)aux, itherm_devices,
	    sizeof(itherm_devices)/sizeof(itherm_devices[0])));
}

void
itherm_attach(struct device *parent, struct device *self, void *aux)
{
	struct itherm_softc *sc = (struct itherm_softc *)self;
	struct pci_attach_args *pa = aux;
	int i;
	pcireg_t v;

	v = pci_conf_read(pa->pa_pc, pa->pa_tag, PCI_MAPREG_START);
	v &= PCI_MAPREG_TYPE_MASK | PCI_MAPREG_MEM_TYPE_MASK;
	if (pci_mapreg_map(pa, PCI_MAPREG_START,
	    v, 0, &sc->iot, &sc->ioh, NULL, &sc->size, 0)) {
		printf(": can't map mem space\n");
		return;
	}

	sc->sensors[ITHERM_SENSOR_THERMOMETER].type = SENSOR_TEMP;
	sc->sensors[ITHERM_SENSOR_CORETEMP1].type = SENSOR_TEMP;
	sc->sensors[ITHERM_SENSOR_CORETEMP2].type = SENSOR_TEMP;
	sc->sensors[ITHERM_SENSOR_COREENERGY].type = SENSOR_WATTS;
	sc->sensors[ITHERM_SENSOR_GPUTEMP].type = SENSOR_TEMP;
	sc->sensors[ITHERM_SENSOR_MAXPROCTEMP].type = SENSOR_TEMP;
	sc->sensors[ITHERM_SENSOR_DIMMTEMP1].type = SENSOR_TEMP;
	sc->sensors[ITHERM_SENSOR_DIMMTEMP2].type = SENSOR_TEMP;
	sc->sensors[ITHERM_SENSOR_DIMMTEMP3].type = SENSOR_TEMP;
	sc->sensors[ITHERM_SENSOR_DIMMTEMP4].type = SENSOR_TEMP;
	sc->sensors[ITHERM_SENSOR_GPUTEMP_ABSOLUTE].type = SENSOR_TEMP;
	sc->sensors[ITHERM_SENSOR_PCHTEMP_ABSOLUTE].type = SENSOR_TEMP;

	strlcpy(sc->sensors[ITHERM_SENSOR_THERMOMETER].desc,
	    "Thermometer",
	    sizeof(sc->sensors[ITHERM_SENSOR_THERMOMETER].desc));

	strlcpy(sc->sensors[ITHERM_SENSOR_CORETEMP1].desc,
	    "Core 1",
	    sizeof(sc->sensors[ITHERM_SENSOR_CORETEMP1].desc));

	strlcpy(sc->sensors[ITHERM_SENSOR_CORETEMP2].desc,
	    "Core 2",
	    sizeof(sc->sensors[ITHERM_SENSOR_CORETEMP2].desc));

	strlcpy(sc->sensors[ITHERM_SENSOR_COREENERGY].desc,
	    "CPU power consumption",
	    sizeof(sc->sensors[ITHERM_SENSOR_COREENERGY].desc));

	strlcpy(sc->sensors[ITHERM_SENSOR_GPUTEMP].desc,
	    "GPU/Memory Controller Temp",
	    sizeof(sc->sensors[ITHERM_SENSOR_GPUTEMP].desc));

	strlcpy(sc->sensors[ITHERM_SENSOR_MAXPROCTEMP].desc,
	    "CPU/GPU Max temp",
	    sizeof(sc->sensors[ITHERM_SENSOR_MAXPROCTEMP].desc));

	strlcpy(sc->sensors[ITHERM_SENSOR_DIMMTEMP1].desc,
	    "DIMM 1",
	    sizeof(sc->sensors[ITHERM_SENSOR_DIMMTEMP1].desc));

	strlcpy(sc->sensors[ITHERM_SENSOR_DIMMTEMP2].desc,
	    "DIMM 2",
	    sizeof(sc->sensors[ITHERM_SENSOR_DIMMTEMP2].desc));

	strlcpy(sc->sensors[ITHERM_SENSOR_DIMMTEMP3].desc,
	    "DIMM 3",
	    sizeof(sc->sensors[ITHERM_SENSOR_DIMMTEMP3].desc));

	strlcpy(sc->sensors[ITHERM_SENSOR_DIMMTEMP4].desc,
	    "DIMM 4",
	    sizeof(sc->sensors[ITHERM_SENSOR_DIMMTEMP4].desc));

	strlcpy(sc->sensors[ITHERM_SENSOR_GPUTEMP_ABSOLUTE].desc,
	    "GPU/Memory controller abs.",
	    sizeof(sc->sensors[ITHERM_SENSOR_GPUTEMP_ABSOLUTE].desc));

	strlcpy(sc->sensors[ITHERM_SENSOR_PCHTEMP_ABSOLUTE].desc,
	    "PCH abs.",
	    sizeof(sc->sensors[ITHERM_SENSOR_PCHTEMP_ABSOLUTE].desc));

	strlcpy(sc->sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sensordev.xname));

	itherm_enable(sc);

	for (i = 0; i < ITHERM_NUM_SENSORS; i++)
		sensor_attach(&sc->sensordev, &sc->sensors[i]);

	sensordev_install(&sc->sensordev);
	sensor_task_register(sc, itherm_refresh, ITHERM_REFRESH_INTERVAL);

	printf("\n");

	return;
}

void
itherm_enable(struct itherm_softc *sc)
{
	sc->energy_prev = 0;

	/* Enable thermal sensor */
	IWRITE1(sc, ITHERM_TSE, ITHERM_TSE_ENABLE);

	/* Enable thermal reporting */
	IWRITE2(sc, ITHERM_TRC, (ITHERM_TEMP_READ_ENABLE |
	    ITHERM_TDR_ENABLE | ITHERM_SECOND_CORE_ENABLE));
}

int
itherm_activate(struct device *self, int act)
{
	struct itherm_softc *sc = (struct itherm_softc *)self;

	switch (act) {
	case DVACT_RESUME:
		itherm_enable(sc);
		break;
	}

	return (0);
}

void
itherm_refresh_sensor_data(struct itherm_softc *sc)
{
	u_int16_t data;
	int64_t energy;
	u_int32_t i;

	/* Thermometer sensor */
	sc->sensors[ITHERM_SENSOR_THERMOMETER].value =
	    IREAD1(sc, ITHERM_TSTR);

	itherm_bias_temperature_sensor(
	    &sc->sensors[ITHERM_SENSOR_THERMOMETER]);

	/*
	 * The Intel 3400 Thermal Sensor has separate sensors for each
	 * core, reported as a 16 bit value. Bits 13:6 are the integer
	 * part of the temperature in C and bits 5:0 are the fractional
	 * part of the temperature, in 1/64 degree C intervals.
	 * Bit 15 is used to indicate an invalid temperature
	 */

	/* Core 1 temperature */
    	data = IREAD2(sc, ITHERM_CTV1);
	if (data & ITHERM_CTV_INVALID)
		sc->sensors[ITHERM_SENSOR_CORETEMP1].flags |=
		    SENSOR_FINVALID;
	else {
		sc->sensors[ITHERM_SENSOR_CORETEMP1].flags &=
		    ~SENSOR_FINVALID;
		sc->sensors[ITHERM_SENSOR_CORETEMP1].value =
		    (data & ITHERM_CTV_INT_MASK) >> 6;
		sc->sensors[ITHERM_SENSOR_CORETEMP1].value *=
		    1000000;
		data &= ITHERM_CTV_FRAC_MASK;
		data *= 1000000 / 64;
		sc->sensors[ITHERM_SENSOR_CORETEMP1].value +=
		    data;
		itherm_bias_temperature_sensor(
		    &sc->sensors[ITHERM_SENSOR_CORETEMP1]);
	}

	/* Core 2 temperature */
    	data = IREAD2(sc, ITHERM_CTV2);
	if (data & ITHERM_CTV_INVALID)
		sc->sensors[ITHERM_SENSOR_CORETEMP2].flags |=
		    SENSOR_FINVALID;
	else {
		sc->sensors[ITHERM_SENSOR_CORETEMP2].flags &=
		    ~SENSOR_FINVALID;
		sc->sensors[ITHERM_SENSOR_CORETEMP2].value =
		    (data & ITHERM_CTV_INT_MASK) >> 6;
		sc->sensors[ITHERM_SENSOR_CORETEMP2].value *=
		    1000000;
		data &= ITHERM_CTV_FRAC_MASK;
		data *= 1000000 / 64;
		sc->sensors[ITHERM_SENSOR_CORETEMP2].value +=
		    data;
		itherm_bias_temperature_sensor(
		    &sc->sensors[ITHERM_SENSOR_CORETEMP2]);
	}

	/*
	 * The core energy sensor reports the number of Joules
	 * of energy consumed by the processor since powerup.
	 * This number is scaled by 65535 and is continually
	 * increasing, so we save the old value and compute
	 * the difference for the Watt sensor value.
	 */

	i = IREAD4(sc, ITHERM_CEV1);
	/* Convert to Joules per interval */
	energy = (i / 65535);
	energy = energy - sc->energy_prev;
	sc->energy_prev = (i / 65535);
	/* Convert to Joules per second */
	energy = energy / ITHERM_REFRESH_INTERVAL;
	/* Convert to micro Joules per second (micro Watts) */
	energy = energy * 1000 * 1000;

	sc->sensors[ITHERM_SENSOR_COREENERGY].value = energy;

	/*
	 * XXX - the GPU temp is reported as a 64 bit value with no
	 * documented structure. Disabled for now
	 */
	sc->sensors[ITHERM_SENSOR_GPUTEMP].flags |= SENSOR_FINVALID;
#if 0
	bus_space_read_multi_4(sc->iot, sc->ioh, ITHERM_MGTV,
	    (u_int32_t *)&sc->sensors[ITHERM_SENSOR_GPUTEMP].value, 2);
	sc->sensors[ITHERM_SENSOR_GPUTEMP].value *= 1000000;
	sc->sensors[ITHERM_SENSOR_GPUTEMP].value += 273150000;
#endif

	/* Max processor temperature */
	sc->sensors[ITHERM_SENSOR_MAXPROCTEMP].value =
	    IREAD1(sc, ITHERM_PTV) * 1000000;
	itherm_bias_temperature_sensor(
	    &sc->sensors[ITHERM_SENSOR_MAXPROCTEMP]);

	/* DIMM 1 */
	sc->sensors[ITHERM_SENSOR_DIMMTEMP1].value =
	    IREAD1(sc, ITHERM_DTV) * 1000000;
	itherm_bias_temperature_sensor(
	    &sc->sensors[ITHERM_SENSOR_DIMMTEMP1]);

	/* DIMM 2 */
	sc->sensors[ITHERM_SENSOR_DIMMTEMP2].value =
	    IREAD1(sc, ITHERM_DTV+1) * 1000000;
	itherm_bias_temperature_sensor(
	    &sc->sensors[ITHERM_SENSOR_DIMMTEMP2]);

	/* DIMM 3 */
	sc->sensors[ITHERM_SENSOR_DIMMTEMP3].value =
	    IREAD1(sc, ITHERM_DTV+2) * 1000000;
	itherm_bias_temperature_sensor(
	    &sc->sensors[ITHERM_SENSOR_DIMMTEMP3]);

	/* DIMM 4 */
	sc->sensors[ITHERM_SENSOR_DIMMTEMP4].value =
	    IREAD1(sc, ITHERM_DTV+3) * 1000000;
	itherm_bias_temperature_sensor(
	    &sc->sensors[ITHERM_SENSOR_DIMMTEMP4]);

	/* GPU Temperature */
	sc->sensors[ITHERM_SENSOR_GPUTEMP_ABSOLUTE].value =
	    IREAD1(sc, ITHERM_ITV+1) * 1000000;
	itherm_bias_temperature_sensor(
	    &sc->sensors[ITHERM_SENSOR_GPUTEMP_ABSOLUTE]);

	/* PCH Temperature */
	sc->sensors[ITHERM_SENSOR_PCHTEMP_ABSOLUTE].value =
	    IREAD1(sc, ITHERM_ITV) * 1000000;
	itherm_bias_temperature_sensor(
	    &sc->sensors[ITHERM_SENSOR_PCHTEMP_ABSOLUTE]);
}

void
itherm_bias_temperature_sensor(struct ksensor *sensor)
{
	if (sensor->value == 0 || sensor->value == 0xff)
		sensor->flags |= SENSOR_FINVALID;
	else
		sensor->flags &= ~SENSOR_FINVALID;

	/* Bias anyway from degC to degK, even if invalid */
	sensor->value += 273150000;
}

void
itherm_refresh(void *arg)
{
	struct itherm_softc *sc = (struct itherm_softc *)arg;

	itherm_refresh_sensor_data(sc);
}
