/*	$OpenBSD: aps.c,v 1.28 2022/04/06 18:59:28 naddy Exp $	*/
/*
 * Copyright (c) 2005 Jonathan Gray <jsg@openbsd.org>
 * Copyright (c) 2008 Can Erkin Acar <canacar@openbsd.org>
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
 * A driver for the ThinkPad Active Protection System based on notes from
 * http://www.almaden.ibm.com/cs/people/marksmith/tpaps.html
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/sensors.h>
#include <sys/timeout.h>
#include <machine/bus.h>
#include <sys/event.h>

#include <dev/isa/isavar.h>

#ifdef __i386__
#include "apm.h"
#include <machine/acpiapm.h>
#include <machine/biosvar.h>
#include <machine/apmvar.h>
#endif

#if defined(APSDEBUG)
#define DPRINTF(x)		do { printf x; } while (0)
#else
#define DPRINTF(x)
#endif


/*
 * EC interface on Thinkpad Laptops, from Linux HDAPS driver notes.
 * From Renesas H8S/2140B Group Hardware Manual
 * http://documentation.renesas.com/eng/products/mpumcu/rej09b0300_2140bhm.pdf
 *
 * EC uses LPC Channel 3 registers TWR0..15
 */

/* STR3 status register */
#define APS_STR3		0x04

#define APS_STR3_IBF3B	0x80	/* Input buffer full (host->slave) */
#define APS_STR3_OBF3B	0x40	/* Output buffer full (slave->host)*/
#define APS_STR3_MWMF	0x20	/* Master write mode */
#define APS_STR3_SWMF	0x10	/* Slave write mode */


/* Base address of TWR registers */
#define APS_TWR_BASE		0x10
#define APS_TWR_RET		0x1f

/* TWR registers */
#define APS_CMD			0x00
#define APS_ARG1		0x01
#define APS_ARG2		0x02
#define APS_ARG3		0x03
#define APS_RET			0x0f

/* Sensor values */
#define APS_STATE		0x01
#define	APS_XACCEL		0x02
#define APS_YACCEL		0x04
#define APS_TEMP		0x06
#define	APS_XVAR		0x07
#define APS_YVAR		0x09
#define APS_TEMP2		0x0b
#define APS_UNKNOWN		0x0c
#define APS_INPUT		0x0d

/* write masks for I/O, send command + 0-3 arguments*/
#define APS_WRITE_0		0x0001
#define APS_WRITE_1		0x0003
#define APS_WRITE_2		0x0007
#define APS_WRITE_3		0x000f

/* read masks for I/O, read 0-3 values (skip command byte) */
#define APS_READ_0		0x0000
#define APS_READ_1		0x0002
#define APS_READ_2		0x0006
#define APS_READ_3		0x000e

#define APS_READ_RET		0x8000
#define APS_READ_ALL		0xffff

/* Bit definitions for APS_INPUT value */
#define APS_INPUT_KB		(1 << 5)
#define APS_INPUT_MS		(1 << 6)
#define APS_INPUT_LIDOPEN	(1 << 7)

#define APS_ADDR_SIZE		0x1f

struct sensor_rec {
	u_int8_t	state;
	u_int16_t	x_accel;
	u_int16_t	y_accel;
	u_int8_t	temp1;
	u_int16_t	x_var;
	u_int16_t	y_var;
	u_int8_t	temp2;
	u_int8_t	unk;
	u_int8_t	input;
};

#define APS_NUM_SENSORS		9

#define APS_SENSOR_XACCEL	0
#define APS_SENSOR_YACCEL	1
#define APS_SENSOR_XVAR		2
#define APS_SENSOR_YVAR		3
#define APS_SENSOR_TEMP1	4
#define APS_SENSOR_TEMP2	5
#define APS_SENSOR_KBACT	6
#define APS_SENSOR_MSACT	7
#define APS_SENSOR_LIDOPEN	8

struct aps_softc {
	struct device sc_dev;

	bus_space_tag_t aps_iot;
	bus_space_handle_t aps_ioh;

	struct ksensor sensors[APS_NUM_SENSORS];
	struct ksensordev sensordev;
	void (*refresh_sensor_data)(struct aps_softc *);

	struct sensor_rec aps_data;
};

int	 aps_match(struct device *, void *, void *);
void	 aps_attach(struct device *, struct device *, void *);
int	 aps_activate(struct device *, int);

int	 aps_init(bus_space_tag_t, bus_space_handle_t);
int	 aps_read_data(struct aps_softc *);
void	 aps_refresh_sensor_data(struct aps_softc *);
void	 aps_refresh(void *);
int	 aps_do_io(bus_space_tag_t, bus_space_handle_t,
		   unsigned char *, int, int);

const struct cfattach aps_ca = {
	sizeof(struct aps_softc),
	aps_match, aps_attach, NULL, aps_activate
};

struct cfdriver aps_cd = {
	NULL, "aps", DV_DULL
};

struct timeout aps_timeout;



/* properly communicate with the controller, writing a set of memory
 * locations and reading back another set  */
int
aps_do_io(bus_space_tag_t iot, bus_space_handle_t ioh,
	  unsigned char *buf, int wmask, int rmask)
{
	int bp, stat, n;

	DPRINTF(("aps_do_io: CMD: 0x%02x, wmask: 0x%04x, rmask: 0x%04x\n",
	       buf[0], wmask, rmask));

	/* write init byte using arbitration */     
	for (n = 0; n < 100; n++) {
		stat = bus_space_read_1(iot, ioh, APS_STR3);
		if (stat & (APS_STR3_OBF3B | APS_STR3_SWMF)) {
			bus_space_read_1(iot, ioh, APS_TWR_RET);
			continue;
		}
		bus_space_write_1(iot, ioh, APS_TWR_BASE, buf[0]);
		stat = bus_space_read_1(iot, ioh, APS_STR3);
		if (stat & (APS_STR3_MWMF))
			break;
		delay(1);
	}

	if (n == 100) {
		DPRINTF(("aps_do_io: Failed to get bus\n"));
		return (1);
	}

	/* write data bytes, init already sent */
	/* make sure last bye is always written as this will trigger slave */
	wmask |= APS_READ_RET;
	buf[APS_RET] = 0x01;

	for (n = 1, bp = 2; n < 16; bp <<= 1, n++) {
		if (wmask & bp) {
			bus_space_write_1(iot, ioh, APS_TWR_BASE + n, buf[n]);
			DPRINTF(("aps_do_io:  write %2d 0x%02x\n", n, buf[n]));
		}
	}

	for (n = 0; n < 100; n++) {
		stat = bus_space_read_1(iot, ioh, APS_STR3);
		if (stat & (APS_STR3_OBF3B))
			break;
		delay(5 * 100);
	}

	if (n == 100) {
		DPRINTF(("aps_do_io: timeout waiting response\n"));
		return (1);
	}
	/* wait for data available */
	/* make sure to read the final byte to clear status */
	rmask |= APS_READ_RET;

	/* read cmd and data bytes */
	for (n = 0, bp = 1; n < 16; bp <<= 1, n++) {
		if (rmask & bp) {
			buf[n] = bus_space_read_1(iot, ioh, APS_TWR_BASE + n);
			DPRINTF(("aps_do_io:  read %2d 0x%02x\n", n, buf[n]));
		}
	}

	return (0);
}

int
aps_match(struct device *parent, void *match, void *aux)
{
	struct isa_attach_args *ia = aux;
	bus_space_tag_t iot = ia->ia_iot;
	bus_space_handle_t ioh;
	int iobase = ia->ipa_io[0].base;
	u_int8_t cr;
	unsigned char iobuf[16];

	if (bus_space_map(iot, iobase, APS_ADDR_SIZE, 0, &ioh)) {
		DPRINTF(("aps: can't map i/o space\n"));
		return (0);
	}
	/* get APS mode */
	iobuf[APS_CMD] = 0x13;
	if (aps_do_io(iot, ioh, iobuf, APS_WRITE_0, APS_READ_1)) {
		bus_space_unmap(iot, ioh, APS_ADDR_SIZE);
		return (0);
	}

	/*
	 * Observed values from Linux driver:
	 * 0x01: T42
	 * 0x02: chip already initialised
	 * 0x03: T41
	 * 0x05: T61
	 */

	cr = iobuf[APS_ARG1];
	DPRINTF(("aps: state register 0x%x\n", cr));

	if (aps_init(iot, ioh)) {
		bus_space_unmap(iot, ioh, APS_ADDR_SIZE);
		return (0);
	}

	bus_space_unmap(iot, ioh, APS_ADDR_SIZE);

	if (iobuf[APS_RET] != 0 || cr < 1 || cr > 5) {
		DPRINTF(("aps0: unsupported state %d\n", cr));
		return (0);
	}

	ia->ipa_nio = 1;
	ia->ipa_io[0].length = APS_ADDR_SIZE;
	ia->ipa_nmem = 0;
	ia->ipa_nirq = 0;
	ia->ipa_ndrq = 0;
	return (1);
}

void
aps_attach(struct device *parent, struct device *self, void *aux)
{
	struct aps_softc *sc = (void *)self;
	int iobase, i;
	bus_space_tag_t iot;
	bus_space_handle_t ioh;
	struct isa_attach_args *ia = aux;

	iobase = ia->ipa_io[0].base;
	iot = sc->aps_iot = ia->ia_iot;

	if (bus_space_map(iot, iobase, APS_ADDR_SIZE, 0, &sc->aps_ioh)) {
		printf(": can't map i/o space\n");
		return;
	}

	ioh = sc->aps_ioh;

	printf("\n");

	sc->sensors[APS_SENSOR_XACCEL].type = SENSOR_INTEGER;
	snprintf(sc->sensors[APS_SENSOR_XACCEL].desc,
	    sizeof(sc->sensors[APS_SENSOR_XACCEL].desc), "X_ACCEL");

	sc->sensors[APS_SENSOR_YACCEL].type = SENSOR_INTEGER;
	snprintf(sc->sensors[APS_SENSOR_YACCEL].desc,
	    sizeof(sc->sensors[APS_SENSOR_YACCEL].desc), "Y_ACCEL");

	sc->sensors[APS_SENSOR_TEMP1].type = SENSOR_TEMP;
	sc->sensors[APS_SENSOR_TEMP2].type = SENSOR_TEMP;

	sc->sensors[APS_SENSOR_XVAR].type = SENSOR_INTEGER;
	snprintf(sc->sensors[APS_SENSOR_XVAR].desc,
	    sizeof(sc->sensors[APS_SENSOR_XVAR].desc), "X_VAR");

	sc->sensors[APS_SENSOR_YVAR].type = SENSOR_INTEGER;
	snprintf(sc->sensors[APS_SENSOR_YVAR].desc,
	    sizeof(sc->sensors[APS_SENSOR_YVAR].desc), "Y_VAR");

	sc->sensors[APS_SENSOR_KBACT].type = SENSOR_INDICATOR;
	snprintf(sc->sensors[APS_SENSOR_KBACT].desc,
	    sizeof(sc->sensors[APS_SENSOR_KBACT].desc), "Keyboard Active");

	sc->sensors[APS_SENSOR_MSACT].type = SENSOR_INDICATOR;
	snprintf(sc->sensors[APS_SENSOR_MSACT].desc,
	    sizeof(sc->sensors[APS_SENSOR_MSACT].desc), "Mouse Active");

	sc->sensors[APS_SENSOR_LIDOPEN].type = SENSOR_INDICATOR;
	snprintf(sc->sensors[APS_SENSOR_LIDOPEN].desc,
	    sizeof(sc->sensors[APS_SENSOR_LIDOPEN].desc), "Lid Open");

	/* stop hiding and report to the authorities */
	strlcpy(sc->sensordev.xname, sc->sc_dev.dv_xname,
	    sizeof(sc->sensordev.xname));
	for (i = 0; i < APS_NUM_SENSORS ; i++) {
		sensor_attach(&sc->sensordev, &sc->sensors[i]);
	}
	sensordev_install(&sc->sensordev);

	/* Refresh sensor data every 0.5 seconds */
	timeout_set(&aps_timeout, aps_refresh, sc);
	timeout_add_msec(&aps_timeout, 500);
}

int
aps_init(bus_space_tag_t iot, bus_space_handle_t ioh)
{
	unsigned char iobuf[16];


	/* command 0x17/0x81: check EC */
	iobuf[APS_CMD] = 0x17;
	iobuf[APS_ARG1] = 0x81;

	if (aps_do_io(iot, ioh, iobuf, APS_WRITE_1, APS_READ_3))
		return (1);

	if (iobuf[APS_RET] != 0 ||iobuf[APS_ARG3] != 0)
		return (2);

	/* Test values from the Linux driver */
	if ((iobuf[APS_ARG1] != 0 || iobuf[APS_ARG2] != 0x60) &&
	    (iobuf[APS_ARG1] != 1 || iobuf[APS_ARG2] != 0))
		return (3);

	/* command 0x14: set power */
	iobuf[APS_CMD] = 0x14;
	iobuf[APS_ARG1] = 0x01;

	if (aps_do_io(iot, ioh, iobuf, APS_WRITE_1, APS_READ_0))
		return (4);

	if (iobuf[APS_RET] != 0)
		return (5);

	/* command 0x10: set config (sample rate and order) */
	iobuf[APS_CMD] = 0x10;
	iobuf[APS_ARG1] = 0xc8;
	iobuf[APS_ARG2] = 0x00;
	iobuf[APS_ARG3] = 0x02;

	if (aps_do_io(iot, ioh, iobuf, APS_WRITE_3, APS_READ_0))
		return (6);

	if (iobuf[APS_RET] != 0)
		return (7);

	/* command 0x11: refresh data */
	iobuf[APS_CMD] = 0x11;
	if (aps_do_io(iot, ioh, iobuf, APS_WRITE_0, APS_READ_1))
		return (8);

	return (0);
}

int
aps_read_data(struct aps_softc *sc)
{
	bus_space_tag_t iot = sc->aps_iot;
	bus_space_handle_t ioh = sc->aps_ioh;
	unsigned char iobuf[16];

	/* command 0x11: refresh data */
	iobuf[APS_CMD] = 0x11;
	if (aps_do_io(iot, ioh, iobuf, APS_WRITE_0, APS_READ_ALL))
		return (1);

	sc->aps_data.state = iobuf[APS_STATE];
	sc->aps_data.x_accel = iobuf[APS_XACCEL] + 256 * iobuf[APS_XACCEL + 1];
	sc->aps_data.y_accel = iobuf[APS_YACCEL] + 256 * iobuf[APS_YACCEL + 1];
	sc->aps_data.temp1 = iobuf[APS_TEMP];
	sc->aps_data.x_var = iobuf[APS_XVAR] + 256 * iobuf[APS_XVAR + 1];
	sc->aps_data.y_var = iobuf[APS_YVAR] + 256 * iobuf[APS_YVAR + 1];
	sc->aps_data.temp2 = iobuf[APS_TEMP2];
	sc->aps_data.input = iobuf[APS_INPUT];

	return (0);
}

void
aps_refresh_sensor_data(struct aps_softc *sc)
{
	int64_t temp;
	int i;
#if NAPM > 0
	extern int lid_action;
	extern int apm_lidclose;
#endif

	if (aps_read_data(sc))
		return;

	for (i = 0; i < APS_NUM_SENSORS; i++) {
		sc->sensors[i].flags &= ~SENSOR_FINVALID;
	}

	sc->sensors[APS_SENSOR_XACCEL].value = sc->aps_data.x_accel;
	sc->sensors[APS_SENSOR_YACCEL].value = sc->aps_data.y_accel;

	/* convert to micro (mu) degrees */
	temp = sc->aps_data.temp1 * 1000000;	
	/* convert to kelvin */
	temp += 273150000; 
	sc->sensors[APS_SENSOR_TEMP1].value = temp;

	/* convert to micro (mu) degrees */
	temp = sc->aps_data.temp2 * 1000000;	
	/* convert to kelvin */
	temp += 273150000; 
	sc->sensors[APS_SENSOR_TEMP2].value = temp;

	sc->sensors[APS_SENSOR_XVAR].value = sc->aps_data.x_var;
	sc->sensors[APS_SENSOR_YVAR].value = sc->aps_data.y_var;
	sc->sensors[APS_SENSOR_KBACT].value =
	    (sc->aps_data.input &  APS_INPUT_KB) ? 1 : 0;
	sc->sensors[APS_SENSOR_MSACT].value =
	    (sc->aps_data.input & APS_INPUT_MS) ? 1 : 0;
#if NAPM > 0
	if (lid_action &&
	    (sc->sensors[APS_SENSOR_LIDOPEN].value == 1) &&
	    (sc->aps_data.input & APS_INPUT_LIDOPEN) == 0)
		/* Inform APM that the lid has closed */
		apm_lidclose = 1;
#endif
	sc->sensors[APS_SENSOR_LIDOPEN].value =
	    (sc->aps_data.input & APS_INPUT_LIDOPEN) ? 1 : 0;
}

void
aps_refresh(void *arg)
{
	struct aps_softc *sc = (struct aps_softc *)arg;

	aps_refresh_sensor_data(sc);
	timeout_add_msec(&aps_timeout, 500);
}

int
aps_activate(struct device *self, int act)
{
	struct aps_softc *sc = (struct aps_softc *)self;
	bus_space_tag_t iot = sc->aps_iot;
	bus_space_handle_t ioh = sc->aps_ioh;
	unsigned char iobuf[16];

	/* check if we bombed during attach */
	if (!timeout_initialized(&aps_timeout))
		return (0);

	switch (act) {
	case DVACT_SUSPEND:
		timeout_del(&aps_timeout);
		break;
	case DVACT_RESUME:
		/*
		 * Redo the init sequence on resume, because APS is 
		 * as forgetful as it is deaf.
		 */

		/* get APS mode */
		iobuf[APS_CMD] = 0x13;
		aps_do_io(iot, ioh, iobuf, APS_WRITE_0, APS_READ_1);

		aps_init(iot, ioh);
		timeout_add_msec(&aps_timeout, 500);
		break;
	}
	return (0);
}
