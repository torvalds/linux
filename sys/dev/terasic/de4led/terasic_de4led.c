/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Robert N. M. Watson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/bus.h>
#include <sys/lock.h>
#include <sys/mutex.h>
#include <sys/rman.h>

#include <machine/bus.h>
#include <machine/resource.h>

#include <dev/led/led.h>
#include <dev/terasic/de4led/terasic_de4led.h>

devclass_t	terasic_de4led_devclass;

static void
terasic_de4led_update(struct terasic_de4led_softc *sc)
{

	TERASIC_DE4LED_LOCK_ASSERT(sc);

	bus_write_1(sc->tdl_res, TERASIC_DE4LED_OFF_LED, sc->tdl_bits);
}

static void
led_update(struct terasic_de4led_softc *sc, int bit, int onoff)
{

	TERASIC_DE4LED_LOCK(sc);
	TERASIC_DE4LED_SETLED(sc, bit, onoff);
	terasic_de4led_update(sc);
	TERASIC_DE4LED_UNLOCK(sc);
}

static void
led_0(void *arg, int onoff)
{

	led_update(arg, 0, onoff);
}

static void
led_1(void *arg, int onoff)
{

	led_update(arg, 1, onoff);
}

static void
led_2(void *arg, int onoff)
{

	led_update(arg, 2, onoff);
}

static void
led_3(void *arg, int onoff)
{

	led_update(arg, 3, onoff);
}

static void
led_4(void *arg, int onoff)
{

	led_update(arg, 4, onoff);
}

static void
led_5(void *arg, int onoff)
{

	led_update(arg, 5, onoff);
}

static void
led_6(void *arg, int onoff)
{

	led_update(arg, 6, onoff);
}

static void
led_7(void *arg, int onoff)
{

	led_update(arg, 7, onoff);
}

void
terasic_de4led_attach(struct terasic_de4led_softc *sc)
{
	const char *cmd;

	TERASIC_DE4LED_LOCK_INIT(sc);

	/*
	 * Clear the LED array before we start.
	 */
	TERASIC_DE4LED_LOCK(sc);
	TERASIC_DE4LED_CLEARBAR(sc);
	terasic_de4led_update(sc);
	TERASIC_DE4LED_UNLOCK(sc);

	/*
	 * Register the LED array with led(4).
	 */
	sc->tdl_leds[0] = led_create(led_0, sc, "de4led_0");
	sc->tdl_leds[1] = led_create(led_1, sc, "de4led_1");
	sc->tdl_leds[2] = led_create(led_2, sc, "de4led_2");
	sc->tdl_leds[3] = led_create(led_3, sc, "de4led_3");
	sc->tdl_leds[4] = led_create(led_4, sc, "de4led_4");
	sc->tdl_leds[5] = led_create(led_5, sc, "de4led_5");
	sc->tdl_leds[6] = led_create(led_6, sc, "de4led_6");
	sc->tdl_leds[7] = led_create(led_7, sc, "de4led_7");

	if (resource_string_value(device_get_name(sc->tdl_dev),
	    sc->tdl_unit, "de4led_0_cmd", &cmd) == 0)
		led_set("de4led_0", cmd);
	if (resource_string_value(device_get_name(sc->tdl_dev),
	    sc->tdl_unit, "de4led_1_cmd", &cmd) == 0)
		led_set("de4led_1", cmd);
	if (resource_string_value(device_get_name(sc->tdl_dev),
	    sc->tdl_unit, "de4led_2_cmd", &cmd) == 0)
		led_set("de4led_2", cmd);
	if (resource_string_value(device_get_name(sc->tdl_dev),
	    sc->tdl_unit, "de4led_3_cmd", &cmd) == 0)
		led_set("de4led_3", cmd);
	if (resource_string_value(device_get_name(sc->tdl_dev),
	    sc->tdl_unit, "de4led_4_cmd", &cmd) == 0)
		led_set("de4led_4", cmd);
	if (resource_string_value(device_get_name(sc->tdl_dev),
	    sc->tdl_unit, "de4led_5_cmd", &cmd) == 0)
		led_set("de4led_5", cmd);
	if (resource_string_value(device_get_name(sc->tdl_dev),
	    sc->tdl_unit, "de4led_6_cmd", &cmd) == 0)
		led_set("de4led_6", cmd);
	if (resource_string_value(device_get_name(sc->tdl_dev),
	    sc->tdl_unit, "de4led_7_cmd", &cmd) == 0)
		led_set("de4led_7", cmd);
}

void
terasic_de4led_detach(struct terasic_de4led_softc *sc)
{
	int i;

	for (i = 0; i < 8; i++) {
		led_destroy(sc->tdl_leds[i]);
		sc->tdl_leds[i] = NULL;
	}
	TERASIC_DE4LED_LOCK(sc);
	TERASIC_DE4LED_CLEARBAR(sc);
	terasic_de4led_update(sc);
	TERASIC_DE4LED_UNLOCK(sc);
	TERASIC_DE4LED_LOCK_DESTROY(sc);
}
