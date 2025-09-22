/*	$OpenBSD: clkbrd.c,v 1.7 2021/10/24 17:05:03 mpi Exp $	*/

/*
 * Copyright (c) 2004 Jason L. Wright (jason@thought.net)
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/malloc.h>
#include <sys/sensors.h>
#include <sys/systm.h>
#include <sys/timeout.h>

#include <machine/bus.h>
#include <machine/autoconf.h>
#include <machine/openfirm.h>

#include <sparc64/dev/fhcvar.h>
#include <sparc64/dev/clkbrdreg.h>
#include <sparc64/dev/clkbrdvar.h>

int clkbrd_match(struct device *, void *, void *);
void clkbrd_attach(struct device *, struct device *, void *);
void clkbrd_led_blink(void *, int);
void clkbrd_refresh(void *);

const struct cfattach clkbrd_ca = {
	sizeof(struct clkbrd_softc), clkbrd_match, clkbrd_attach
};

struct cfdriver clkbrd_cd = {
	NULL, "clkbrd", DV_DULL
};

int
clkbrd_match(struct device *parent, void *match, void *aux)
{
	struct fhc_attach_args *fa = aux;

	if (strcmp(fa->fa_name, "clock-board") == 0)
		return (1);
	return (0);
}

void
clkbrd_attach(struct device *parent, struct device *self, void *aux)
{
	struct clkbrd_softc *sc = (struct clkbrd_softc *)self;
	struct fhc_attach_args *fa = aux;
	int slots;
	u_int8_t r;

	sc->sc_bt = fa->fa_bustag;

	if (fa->fa_nreg < 2) {
		printf(": no registers\n");
		return;
	}

	if (fhc_bus_map(sc->sc_bt, fa->fa_reg[1].fbr_slot,
	    fa->fa_reg[1].fbr_offset, fa->fa_reg[1].fbr_size, 0,
	    &sc->sc_creg)) {
		printf(": can't map ctrl regs\n");
		return;
	}

	if (fa->fa_nreg > 2) {
		if (fhc_bus_map(sc->sc_bt, fa->fa_reg[2].fbr_slot,
		    fa->fa_reg[2].fbr_offset, fa->fa_reg[2].fbr_size, 0,
		    &sc->sc_vreg)) {
			printf(": can't map vreg\n");
			return;
		}
		sc->sc_has_vreg = 1;
	}

	slots = 4;
	r = bus_space_read_1(sc->sc_bt, sc->sc_creg, CLK_STS1);
	switch (r & 0xc0) {
	case 0x40:
		slots = 16;
		break;
	case 0xc0:
		slots = 8;
		break;
	case 0x80:
		if (sc->sc_has_vreg) {
			r = bus_space_read_1(sc->sc_bt, sc->sc_vreg, 0);
			if (r != 0 && (r & 0x80) == 0)
					slots = 5;
		}
	}

	sc->sc_blink.bl_func = clkbrd_led_blink;
	sc->sc_blink.bl_arg = sc;
	blink_led_register(&sc->sc_blink);

	/* Initialize sensor data. */
	strlcpy(sc->sc_sensordev.xname, sc->sc_dv.dv_xname,
	    sizeof(sc->sc_sensordev.xname));
	sc->sc_sensor.type = SENSOR_TEMP;

	sensor_attach(&sc->sc_sensordev, &sc->sc_sensor);

	if (sensor_task_register(sc, clkbrd_refresh, 5) == NULL) {
		printf(": unable to register update task\n");
		return;
	}

	sensordev_install(&sc->sc_sensordev);

	printf(": %d slots\n", slots);
}

void
clkbrd_led_blink(void *vsc, int on)
{
	struct clkbrd_softc *sc = vsc;
	int s;
	u_int8_t r;

	s = splhigh();
	r = bus_space_read_1(sc->sc_bt, sc->sc_creg, CLK_CTRL);
	if (on)
		r |= CLK_CTRL_RLED;
	else
		r &= ~CLK_CTRL_RLED;
	bus_space_write_1(sc->sc_bt, sc->sc_creg, CLK_CTRL, r);
	bus_space_read_1(sc->sc_bt, sc->sc_creg, CLK_CTRL);
	splx(s);
}

/*
 * Calibration table for temperature sensor.
 */
int8_t clkbrd_temp[] = {
	 0,  0,  0,  0,  0,  0,  0,  0,
	 0,  0,  0,  0,  1,  2,  4,  5,
	 7,  8, 10, 11, 12, 13, 14, 15,
	17, 18, 19, 20, 21, 22, 23, 24,
	24, 25, 26, 27, 28, 29, 29, 30,
	31, 32, 32, 33, 34, 35, 35, 36,
	37, 38, 38, 39, 40, 40, 41, 42,
	42, 43, 44, 44, 45, 46, 46, 47,
	48, 48, 49, 50, 50, 51, 52, 52,
	53, 54, 54, 55, 56, 57, 57, 58,
	59, 59, 60, 60, 61, 62, 63, 63,
	64, 65, 65, 66, 67, 68, 68, 69,
	70, 70, 71, 72, 73, 74, 74, 75,
	76, 77, 78, 78, 79, 80, 81, 82
};

const int8_t clkbrd_temp_warn = 60;
const int8_t clkbrd_temp_crit = 68;

void
clkbrd_refresh(void *arg)
{
	struct clkbrd_softc *sc = arg;
	u_int8_t val;
	int8_t temp;

	val = bus_space_read_1(sc->sc_bt, sc->sc_creg, CLK_TEMP);
	if (val == 0xff) {
		sc->sc_sensor.flags |= SENSOR_FINVALID;
		return;
	}

	if (val < sizeof(clkbrd_temp))
		temp = clkbrd_temp[val];
	else
		temp = clkbrd_temp[sizeof(clkbrd_temp) - 1];

	sc->sc_sensor.value = val * 1000000 + 273150000;
	sc->sc_sensor.flags &= ~SENSOR_FINVALID;
	sc->sc_sensor.status = SENSOR_S_OK;
	if (temp >= clkbrd_temp_warn)
		sc->sc_sensor.status = SENSOR_S_WARN;
	if (temp >= clkbrd_temp_crit)
		sc->sc_sensor.status = SENSOR_S_CRIT;
}
