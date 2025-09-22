/*	$OpenBSD: sch311x.c,v 1.18 2022/04/06 18:59:29 naddy Exp $	*/
/*
 * Copyright (c) 2008 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2009 Michael Knudsen <mk@openbsd.org>
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
 * SMSC SCH3112, SCH3114, and SCH3116 LPC Super I/O driver.
 */

#include <sys/param.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/sensors.h>

#include <machine/bus.h>

#include <dev/isa/isavar.h>

/* Device identifiers */
#define SCHSIO_ID_SCH3112	0x7c
#define SCHSIO_ID_SCH3114	0x7d
#define SCHSIO_ID_SCH3116	0x7f

#define SCHSIO_IOSIZE 0x02
#define SCHSIO_PORT_CONFIG	0x00

/* These are used in configuration mode */
#define SCHSIO_PORT_INDEX	0x00
#define SCHSIO_PORT_DATA	0x01

#define SCHSIO_CONFIG_ENTER	0x55
#define SCHSIO_CONFIG_LEAVE	0xaa

/* Register definitions */
#define SCHSIO_IDX_LDEVSEL	0x07 /* Logical device select */
#define SCHSIO_IDX_DEVICE	0x20 /* Device ID */
#define SCHSIO_IDX_REV		0x21 /* Device revision */

#define SCHSIO_IDX_BASE_HI	0x60 /* Configuration base address */
#define SCHSIO_IDX_BASE_LO	0x61

/* Logical devices */
#define SCHSIO_LDEV_RUNTIME	0x0a /* holds wdog and sensors */
#define SCHSIO_LDEV_RUNTIME_SZ	0x100

/* Hardware monitor */
#define SCHSIO_HWM_INTERVAL	5 /* seconds */

/* Register access */
#define SCHSIO_HWM_INDEX	0x70
#define SCHSIO_HWM_DATA		0x71

/* Sensor definitions */
/* Voltage */
#define SCHSIO_HWM_VOLT1	0x20
#define SCHSIO_HWM_VOLT2	0x21
#define SCHSIO_HWM_VOLT3	0x22
#define SCHSIO_HWM_VOLT4	0x23
#define SCHSIO_HWM_VOLT5	0x24
#define SCHSIO_HWM_VOLT6	0x99
#define SCHSIO_HWM_VOLT7	0x9a

/* Temperature */
#define SCHSIO_HWM_TEMP1	0x26
#define SCHSIO_HWM_TEMP2	0x25
#define SCHSIO_HWM_TEMP3	0x27

/* Fan speed */
#define SCHSIO_HWM_TACH1_L	0x28
#define SCHSIO_HWM_TACH1_U	0x29
#define SCHSIO_HWM_TACH2_L	0x2a
#define SCHSIO_HWM_TACH2_U	0x2b
#define SCHSIO_HWM_TACH3_L	0x2c
#define SCHSIO_HWM_TACH3_U	0x2d

/* 11111 = 90kHz * 10^9 */
#define SCHSIO_FAN_RPM(x)	(1000000000 / ((x) * 11111) * 60)

#define SCHSIO_CONV_VOLT1	66400
#define SCHSIO_CONV_VOLT2	20000
#define SCHSIO_CONV_VOLT3	43800
#define SCHSIO_CONV_VOLT4	66400
#define SCHSIO_CONV_VOLT5	160000
#define SCHSIO_CONV_VOLT6	43800
#define SCHSIO_CONV_VOLT7	43800
#define SCHSIO_VOLT_MUV(x, k)	(1000000 * (x) / 2560000 * (k))

#define SCHSIO_TEMP_MUK(x)	(((x) + 273) * 1000000)

#define SCHSIO_SENSORS		13

#define SCHSIO_SENSOR_FAN1	0
#define SCHSIO_SENSOR_FAN2	1
#define SCHSIO_SENSOR_FAN3	2

#define SCHSIO_SENSOR_VOLT1	3
#define SCHSIO_SENSOR_VOLT2	4
#define SCHSIO_SENSOR_VOLT3	5
#define SCHSIO_SENSOR_VOLT4	6
#define SCHSIO_SENSOR_VOLT5	7
#define SCHSIO_SENSOR_VOLT6	8
#define SCHSIO_SENSOR_VOLT7	9

#define SCHSIO_SENSOR_TEMP1	10
#define SCHSIO_SENSOR_TEMP2	11
#define SCHSIO_SENSOR_TEMP3	12


/* Watchdog */

/* Register access */
#define SCHSIO_WDT_GPIO		0x47
#define SCHSIO_WDT_TIMEOUT	0x65
#define SCHSIO_WDT_VAL		0x66
#define SCHSIO_WDT_CFG		0x67
#define SCHSIO_WDT_CTRL		0x68

/* Bits */
#define SCHSIO_WDT_GPIO_MASK	0x0f
#define SCHSIO_WDT_GPIO_OUT	0x0e

#define SCHSIO_WDT_TO_SECONDS		(1 << 7)

#define SCHSIO_WDT_CTRL_TRIGGERED	(1 << 0)
#define SCHSIO_WDT_CFG_KBDEN		(1 << 1)
#define SCHSIO_WDT_CFG_MSEN		(1 << 2)

/* autoconf(9) flags etc. */
#define SCHSIO_CFFLAGS_WDTEN	(1 << 0)

#define DEVNAME(x) ((x)->sc_dev.dv_xname)

struct schsio_softc {
	struct device		sc_dev;

	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	bus_space_handle_t	sc_ioh_rr;

	struct ksensordev	sc_sensordev;
	struct ksensor		sc_sensor[SCHSIO_SENSORS];
};

int	schsio_probe(struct device *, void *, void *);
void	schsio_attach(struct device *, struct device *, void *);
int	schsio_activate(struct device *, int);

static __inline void schsio_config_enable(bus_space_tag_t iot,
    bus_space_handle_t ioh);
static __inline void schsio_config_disable(bus_space_tag_t iot,
    bus_space_handle_t ioh);

u_int8_t schsio_config_read(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int8_t reg);
void schsio_config_write(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int8_t reg, u_int8_t val);

/* HWM prototypes */
void schsio_hwm_init(struct schsio_softc *sc);
void schsio_hwm_update(void *arg);
u_int8_t schsio_hwm_read(struct schsio_softc *sc, u_int8_t reg);

/* Watchdog prototypes */

void schsio_wdt_init(struct schsio_softc *sc);
int schsio_wdt_cb(void *arg, int period);

const struct cfattach schsio_ca = {
	sizeof(struct schsio_softc),
	schsio_probe,
	schsio_attach,
	NULL,
	schsio_activate
};

struct cfdriver schsio_cd = {
	NULL, "schsio", DV_DULL
};

static __inline void
schsio_config_enable(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	bus_space_write_1(iot, ioh, SCHSIO_PORT_CONFIG, SCHSIO_CONFIG_ENTER);
}

static __inline void
schsio_config_disable(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	bus_space_write_1(iot, ioh, SCHSIO_PORT_CONFIG, SCHSIO_CONFIG_LEAVE);
}

u_int8_t
schsio_config_read(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int8_t reg)
{
	bus_space_write_1(iot, ioh, SCHSIO_PORT_INDEX, reg);
	return (bus_space_read_1(iot, ioh, SCHSIO_PORT_DATA));
}

void
schsio_config_write(bus_space_tag_t iot, bus_space_handle_t ioh,
    u_int8_t reg, u_int8_t val)
{
	bus_space_write_1(iot, ioh, SCHSIO_PORT_INDEX, reg);
	bus_space_write_1(iot, ioh, SCHSIO_PORT_DATA, val);
}

int
schsio_probe(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	u_int8_t reg;

	/* Match by device ID */
	iot = ia->ia_iot;
	if (bus_space_map(iot, ia->ipa_io[0].base, SCHSIO_IOSIZE, 0, &ioh))
		return (0);

	schsio_config_enable(iot, ioh);
	reg = schsio_config_read(iot, ioh, SCHSIO_IDX_DEVICE);
	schsio_config_disable(iot, ioh);

	bus_space_unmap(iot, ia->ipa_io[0].base, SCHSIO_IOSIZE);

	switch (reg) {
	case SCHSIO_ID_SCH3112:
	case SCHSIO_ID_SCH3114:
	case SCHSIO_ID_SCH3116:
		ia->ipa_nio = 1;
		ia->ipa_io[0].length = SCHSIO_IOSIZE;
		ia->ipa_nmem = 0;
		ia->ipa_nirq = 0;
		ia->ipa_ndrq = 0;
		ia->ia_aux = (void *)(u_long) reg;

		return (1);
		break;
	}

	return (0);
}

void
schsio_attach(struct device *parent, struct device *self, void *aux)
{
	struct schsio_softc *sc = (void *)self;
	struct isa_attach_args *ia = aux;
	u_int16_t iobase;
	u_int8_t reg0, reg1;

	/* Map ISA I/O space */
	sc->sc_iot = ia->ia_iot;
	if (bus_space_map(sc->sc_iot, ia->ipa_io[0].base,
	    SCHSIO_IOSIZE, 0, &sc->sc_ioh)) {
		printf(": can't map i/o space\n");
		return;
	}

	/* Enter configuration mode */
	schsio_config_enable(sc->sc_iot, sc->sc_ioh);

	/* Check device ID */
	reg0 = (u_int8_t)(u_long) ia->ia_aux;
	switch (reg0) {
	case SCHSIO_ID_SCH3112:
		printf(": SCH3112");
		break;
	case SCHSIO_ID_SCH3114:
		printf(": SCH3114");
		break;
	case SCHSIO_ID_SCH3116:
		printf(": SCH3116");
		break;
	}

	/* Read device revision */
	reg0 = schsio_config_read(sc->sc_iot, sc->sc_ioh, SCHSIO_IDX_REV);
	printf(" rev 0x%02x", reg0);

	/* Select runtime registers logical device */
	schsio_config_write(sc->sc_iot, sc->sc_ioh, SCHSIO_IDX_LDEVSEL,
	    SCHSIO_LDEV_RUNTIME);

	reg0 = schsio_config_read(sc->sc_iot, sc->sc_ioh,
	    SCHSIO_IDX_BASE_HI);
	reg1 = schsio_config_read(sc->sc_iot, sc->sc_ioh,
	    SCHSIO_IDX_BASE_LO);
	iobase = (reg0 << 8) | reg1;

	if (bus_space_map(sc->sc_iot, iobase, SCHSIO_LDEV_RUNTIME_SZ,
	    0, &sc->sc_ioh_rr)) {
		printf(": can't map i/o space\n");
		return;
	}

	schsio_wdt_init(sc);
	schsio_hwm_init(sc);

	printf("\n");

	/* Escape from configuration mode */
	schsio_config_disable(sc->sc_iot, sc->sc_ioh);
}

int
schsio_activate(struct device *self, int act)
{
	switch (act) {
	case DVACT_POWERDOWN:
		wdog_shutdown(self);
		break;
	}

	return (0);
}

void
schsio_hwm_init(struct schsio_softc *sc)
{
	int i;

	/* Set up sensors */
	for (i = SCHSIO_SENSOR_FAN1; i < SCHSIO_SENSOR_FAN3 + 1; i++)
		sc->sc_sensor[i].type = SENSOR_FANRPM;

	strlcpy(sc->sc_sensor[SCHSIO_SENSOR_VOLT1].desc, "+2.5V",
	    sizeof(sc->sc_sensor[SCHSIO_SENSOR_VOLT1].desc));
	strlcpy(sc->sc_sensor[SCHSIO_SENSOR_VOLT2].desc, "+1.5V (Vccp)",
	    sizeof(sc->sc_sensor[SCHSIO_SENSOR_VOLT2].desc));
	strlcpy(sc->sc_sensor[SCHSIO_SENSOR_VOLT3].desc, "+3.3V (VCC)",
	    sizeof(sc->sc_sensor[SCHSIO_SENSOR_VOLT3].desc));
	strlcpy(sc->sc_sensor[SCHSIO_SENSOR_VOLT4].desc, "+5V",
	    sizeof(sc->sc_sensor[SCHSIO_SENSOR_VOLT4].desc));
	strlcpy(sc->sc_sensor[SCHSIO_SENSOR_VOLT5].desc, "+12V",
	    sizeof(sc->sc_sensor[SCHSIO_SENSOR_VOLT5].desc));
	strlcpy(sc->sc_sensor[SCHSIO_SENSOR_VOLT6].desc, "+3.3V (VTR)",
	    sizeof(sc->sc_sensor[SCHSIO_SENSOR_VOLT6].desc));
	strlcpy(sc->sc_sensor[SCHSIO_SENSOR_VOLT7].desc, "+3V (Vbat)",
	    sizeof(sc->sc_sensor[SCHSIO_SENSOR_VOLT7].desc));
	for (i = SCHSIO_SENSOR_VOLT1; i < SCHSIO_SENSOR_VOLT7 + 1; i++)
		sc->sc_sensor[i].type = SENSOR_VOLTS_DC;

	strlcpy(sc->sc_sensor[SCHSIO_SENSOR_TEMP1].desc, "Internal",
	    sizeof(sc->sc_sensor[SCHSIO_SENSOR_TEMP1].desc));
	strlcpy(sc->sc_sensor[SCHSIO_SENSOR_TEMP2].desc, "Remote",
	    sizeof(sc->sc_sensor[SCHSIO_SENSOR_TEMP2].desc));
	strlcpy(sc->sc_sensor[SCHSIO_SENSOR_TEMP3].desc, "Remote",
	    sizeof(sc->sc_sensor[SCHSIO_SENSOR_TEMP3].desc));
	for (i = SCHSIO_SENSOR_TEMP1; i < SCHSIO_SENSOR_TEMP3 + 1; i++)
		sc->sc_sensor[i].type = SENSOR_TEMP;

	strlcpy(sc->sc_sensordev.xname, DEVNAME(sc),
	    sizeof(sc->sc_sensordev.xname));

	for (i = 0; i < SCHSIO_SENSORS; i++)
		sensor_attach(&sc->sc_sensordev, &sc->sc_sensor[i]);

	if (sensor_task_register(sc, schsio_hwm_update,
	    SCHSIO_HWM_INTERVAL) == NULL) {
		printf(": unable to register update task");
		return;
	}
	sensordev_install(&sc->sc_sensordev);
}

void
schsio_hwm_update(void *arg)
{
	struct schsio_softc *sc;
	u_int16_t tach;
	int8_t temp;
	u_int8_t volt;
	u_int8_t reg0, reg1;

	sc = (struct schsio_softc *)arg;

	reg0 = schsio_hwm_read(sc, SCHSIO_HWM_TACH1_L);
	reg1 = schsio_hwm_read(sc, SCHSIO_HWM_TACH1_U);
	tach = (reg1 << 8) | reg0;
	sc->sc_sensor[SCHSIO_SENSOR_FAN1].value = SCHSIO_FAN_RPM(tach);
	sc->sc_sensor[SCHSIO_SENSOR_FAN1].flags =
	    (tach == 0xffff) ? SENSOR_FINVALID : 0;

	reg0 = schsio_hwm_read(sc, SCHSIO_HWM_TACH2_L);
	reg1 = schsio_hwm_read(sc, SCHSIO_HWM_TACH2_U);
	tach = (reg1 << 8) | reg0;
	sc->sc_sensor[SCHSIO_SENSOR_FAN2].value = SCHSIO_FAN_RPM(tach);
	sc->sc_sensor[SCHSIO_SENSOR_FAN2].flags =
	    (tach == 0xffff) ? SENSOR_FINVALID : 0;

	reg0 = schsio_hwm_read(sc, SCHSIO_HWM_TACH3_L);
	reg1 = schsio_hwm_read(sc, SCHSIO_HWM_TACH3_U);
	tach = (reg1 << 8) | reg0;
	sc->sc_sensor[SCHSIO_SENSOR_FAN3].value = SCHSIO_FAN_RPM(tach);
	sc->sc_sensor[SCHSIO_SENSOR_FAN3].flags =
	    (tach == 0xffff) ? SENSOR_FINVALID : 0;

	volt = schsio_hwm_read(sc, SCHSIO_HWM_VOLT1);
	sc->sc_sensor[SCHSIO_SENSOR_VOLT1].value =
	    SCHSIO_VOLT_MUV(volt, SCHSIO_CONV_VOLT1);

	volt = schsio_hwm_read(sc, SCHSIO_HWM_VOLT2);
	sc->sc_sensor[SCHSIO_SENSOR_VOLT2].value =
	    SCHSIO_VOLT_MUV(volt, SCHSIO_CONV_VOLT2);

	volt = schsio_hwm_read(sc, SCHSIO_HWM_VOLT3);
	sc->sc_sensor[SCHSIO_SENSOR_VOLT3].value =
	    SCHSIO_VOLT_MUV(volt, SCHSIO_CONV_VOLT3);

	volt = schsio_hwm_read(sc, SCHSIO_HWM_VOLT4);
	sc->sc_sensor[SCHSIO_SENSOR_VOLT4].value =
	    SCHSIO_VOLT_MUV(volt, SCHSIO_CONV_VOLT4);

	volt = schsio_hwm_read(sc, SCHSIO_HWM_VOLT5);
	sc->sc_sensor[SCHSIO_SENSOR_VOLT5].value =
	    SCHSIO_VOLT_MUV(volt, SCHSIO_CONV_VOLT5);

	volt = schsio_hwm_read(sc, SCHSIO_HWM_VOLT6);
	sc->sc_sensor[SCHSIO_SENSOR_VOLT6].value =
	    SCHSIO_VOLT_MUV(volt, SCHSIO_CONV_VOLT6);

	volt = schsio_hwm_read(sc, SCHSIO_HWM_VOLT7);
	sc->sc_sensor[SCHSIO_SENSOR_VOLT7].value =
	    SCHSIO_VOLT_MUV(volt, SCHSIO_CONV_VOLT7);

	temp = schsio_hwm_read(sc, SCHSIO_HWM_TEMP1);
	sc->sc_sensor[SCHSIO_SENSOR_TEMP1].value = SCHSIO_TEMP_MUK(temp);
	sc->sc_sensor[SCHSIO_SENSOR_TEMP1].flags =
	    ((uint8_t)temp == 0x80) ? SENSOR_FINVALID : 0;

	temp = schsio_hwm_read(sc, SCHSIO_HWM_TEMP2);
	sc->sc_sensor[SCHSIO_SENSOR_TEMP2].value = SCHSIO_TEMP_MUK(temp);
	sc->sc_sensor[SCHSIO_SENSOR_TEMP2].flags =
	    ((uint8_t)temp == 0x80) ? SENSOR_FINVALID : 0;

	temp = schsio_hwm_read(sc, SCHSIO_HWM_TEMP3);
	sc->sc_sensor[SCHSIO_SENSOR_TEMP3].value = SCHSIO_TEMP_MUK(temp);
	sc->sc_sensor[SCHSIO_SENSOR_TEMP3].flags =
	    ((uint8_t)temp == 0x80) ? SENSOR_FINVALID : 0;

}

u_int8_t
schsio_hwm_read(struct schsio_softc *sc, u_int8_t reg)
{
	bus_space_write_1(sc->sc_iot, sc->sc_ioh_rr, SCHSIO_HWM_INDEX, reg);
	return (bus_space_read_1(sc->sc_iot, sc->sc_ioh_rr, SCHSIO_HWM_DATA));
}

void
schsio_wdt_init(struct schsio_softc *sc)
{
	u_int8_t reg;

	reg = bus_space_read_1(sc->sc_iot, sc->sc_ioh_rr, SCHSIO_WDT_GPIO);
	if ((reg & SCHSIO_WDT_GPIO_MASK) != SCHSIO_WDT_GPIO_OUT) {
		if (sc->sc_dev.dv_cfdata->cf_flags & SCHSIO_CFFLAGS_WDTEN) {
			reg &= ~0x0f;
			reg |= SCHSIO_WDT_GPIO_OUT;
			bus_space_write_1(sc->sc_iot, sc->sc_ioh_rr,
			    SCHSIO_WDT_GPIO, reg);
		}
		else {
			printf(", watchdog disabled");
			return;
		}
	}

	/* First of all, make sure the wdt is disabled */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh_rr, SCHSIO_WDT_VAL, 0);

	/* Clear triggered status */
	reg = bus_space_read_1(sc->sc_iot, sc->sc_ioh_rr, SCHSIO_WDT_CTRL);
	if (reg & SCHSIO_WDT_CTRL_TRIGGERED) {
		printf(", warning: watchdog triggered");
		reg &= ~SCHSIO_WDT_CTRL_TRIGGERED;
		bus_space_write_1(sc->sc_iot, sc->sc_ioh_rr,
		    SCHSIO_WDT_CTRL, reg);
	}

	/* Disable wdt reset by mouse and kbd */
	reg = bus_space_read_1(sc->sc_iot, sc->sc_ioh_rr, SCHSIO_WDT_CFG);
	reg &= ~(SCHSIO_WDT_CFG_MSEN | SCHSIO_WDT_CFG_KBDEN);
	bus_space_write_1(sc->sc_iot, sc->sc_ioh_rr, SCHSIO_WDT_CFG, reg);

	wdog_register(schsio_wdt_cb, sc);
}

int
schsio_wdt_cb(void *arg, int period)
{
	struct schsio_softc *sc;
	uint8_t val, minute, reg;

	sc = (struct schsio_softc *)arg;

	if (period > 255) {
		val = period / 60;
		minute = 1;
	} else {
		val = period;
		minute = 0;
	}

	/* Set unit */
	reg = bus_space_read_1(sc->sc_iot, sc->sc_ioh_rr,
	    SCHSIO_WDT_TIMEOUT);
	if (!minute)
		reg |= SCHSIO_WDT_TO_SECONDS;
	else
		reg &= ~SCHSIO_WDT_TO_SECONDS;

	bus_space_write_1(sc->sc_iot, sc->sc_ioh_rr, SCHSIO_WDT_TIMEOUT,
	    reg);

	/* Set value */
	bus_space_write_1(sc->sc_iot, sc->sc_ioh_rr, SCHSIO_WDT_VAL, val);

	if (!minute)
		return val;
	else
		return val * 60;
}

