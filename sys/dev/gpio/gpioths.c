/*-
 * Copyright (c) 2016 Michael Zhilin <mizhka@freebsd.org>
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
#include <sys/kernel.h>
#include <sys/bus.h>
#include <sys/module.h>
#include <sys/errno.h>
#include <sys/systm.h>
#include <sys/sysctl.h>

#include <machine/bus.h>
#include <sys/rman.h>
#include <sys/gpio.h>
#include <machine/resource.h>

#include "gpiobus_if.h"

/*
 * GPIOTHS - Temp/Humidity sensor over GPIO, e.g. DHT11/DHT22
 * This is driver for Temperature & Humidity sensor which provides digital
 * output over single-wire protocol from embedded 8-bit microcontroller.
 * 
 * Temp/Humidity sensor can't be discovered automatically, please specify hints
 * as part of loader or kernel configuration:
 *	hint.gpioths.0.at="gpiobus0"
 *	hint.gpioths.0.pins=<PIN>
 */

#define	GPIOTHS_POLLTIME	5	/* in seconds */

#define	GPIOTHS_DHT_STARTCYCLE	20000	/* 20ms = 20000us */
#define	GPIOTHS_DHT_TIMEOUT	1000	/* 1ms = 1000us */
#define	GPIOTHS_DHT_CYCLES	41
#define	GPIOTHS_DHT_ONEBYTEMASK	0xFF
#define	GPIOTHS_DHT_TEMP_SHIFT	8
#define	GPIOTHS_DHT_HUM_SHIFT	24

struct gpioths_softc {
	device_t		 dev;
	int			 temp;
	int			 hum;
	int			 fails;
	struct sysctl_oid	*temp_oid;
	struct sysctl_oid	*hum_oid;
	struct sysctl_oid	*fails_oid;
	struct callout		 callout;
};

static devclass_t gpioths_devclass;

/* Prototypes */
static int		gpioths_probe(device_t dev);
static int		gpioths_attach(device_t dev);
static int		gpioths_detach(device_t dev);
static void		gpioths_poll(void *arg);
static int		gpioths_temp_sysctl(SYSCTL_HANDLER_ARGS);
static int		gpioths_hum_sysctl(SYSCTL_HANDLER_ARGS);
static int		gpioths_fails_sysctl(SYSCTL_HANDLER_ARGS);

/* DHT-specific methods */
static int		gpioths_dht_initread(device_t bus, device_t dev);
static int		gpioths_dht_readbytes(device_t bus, device_t dev);
static int		gpioths_dht_timeuntil(device_t bus, device_t dev,
			    uint32_t lev, uint32_t *time);

/* Implementation */
static int
gpioths_probe(device_t dev)
{
	device_set_desc(dev, "Temperature and Humidity Sensor over GPIO");
	return (0);
}

static int
gpioths_dht_timeuntil(device_t bus, device_t dev, uint32_t lev, uint32_t *time)
{
	uint32_t	cur_level;
	int		i;

	for (i = 0; i < GPIOTHS_DHT_TIMEOUT; i++) {
		GPIOBUS_PIN_GET(bus, dev, 0, &cur_level);
		if (cur_level == lev) {
			if (time != NULL)
				*time = i;
			return (0);
		}
		DELAY(1);
	}

	/* Timeout */
	return (ETIMEDOUT);
}

static int
gpioths_dht_initread(device_t bus, device_t dev)
{
	int	err;

	err = GPIOBUS_PIN_SETFLAGS(bus, dev, 0, GPIO_PIN_OUTPUT);
	if (err != 0) {
		device_printf(dev, "err(GPIOBUS_PIN_SETFLAGS, OUT) = %d\n", err);
		return (err);
	}
	DELAY(1);

	err = GPIOBUS_PIN_SET(bus, dev, 0, GPIO_PIN_LOW);
	if (err != 0) {
		device_printf(dev, "err(GPIOBUS_PIN_SET, LOW) = %d\n", err);
		return (err);
	}

	/*
	 * According to specifications we need to wait no more than 18ms
	 * to start data transfer
	 */
	DELAY(GPIOTHS_DHT_STARTCYCLE);
	err = GPIOBUS_PIN_SET(bus, dev, 0, GPIO_PIN_HIGH);
	if (err != 0) {
		device_printf(dev, "err(GPIOBUS_PIN_SET, HIGH) = %d\n", err);
		return (err);
	}

	DELAY(1);
	err = GPIOBUS_PIN_SETFLAGS(bus, dev, 0, GPIO_PIN_INPUT) ;
	if (err != 0) {
		device_printf(dev, "err(GPIOBUS_PIN_SETFLAGS, IN) = %d\n", err);
		return (err);
	}

	DELAY(1);
	return (0);
}

static int
gpioths_dht_readbytes(device_t bus, device_t dev)
{
	struct gpioths_softc	*sc;
	uint32_t		 calibrations[GPIOTHS_DHT_CYCLES];
	uint32_t		 intervals[GPIOTHS_DHT_CYCLES];
	uint32_t		 err, avglen, value;
	uint8_t			 crc, calc;
	int			 i, offset, size;

	sc = device_get_softc(dev);

	err = gpioths_dht_initread(bus,dev);
	if (err) {
		device_printf(dev, "gpioths_dht_initread error = %d\n", err);
		goto error;
	}

	err = gpioths_dht_timeuntil(bus, dev, GPIO_PIN_LOW, NULL);
	if (err) {
		device_printf(dev, "err(START) = %d\n", err);
		goto error;
	}

	/* reading - 41 cycles */
	for (i = 0; i < GPIOTHS_DHT_CYCLES; i++) {
		err = gpioths_dht_timeuntil(bus, dev, GPIO_PIN_HIGH,
		          &calibrations[i]);
		if (err) {
			device_printf(dev, "err(CAL, %d) = %d\n", i, err);
			goto error;
		}
		err = gpioths_dht_timeuntil(bus, dev, GPIO_PIN_LOW,
			  &intervals[i]);
		if (err) {
			device_printf(dev, "err(INTERVAL, %d) = %d\n", i, err);
			goto error;
		}
	}

	err = GPIOBUS_PIN_SETFLAGS(bus, dev, 0, GPIO_PIN_OUTPUT);
	if (err != 0) {
		device_printf(dev, "err(FINAL_SETFLAGS, OUT) = %d\n", err);
		goto error;
	}
	DELAY(1);

	/* Calculate average data calibration cycle length */
	avglen = 0;
	for (i = 1; i < GPIOTHS_DHT_CYCLES; i++)
		avglen += calibrations[i];

	avglen = avglen / (GPIOTHS_DHT_CYCLES - 1);

	/* Calculate data */
	value = 0;
	offset = 1;
	size = sizeof(value) * 8;
	for (i = offset; i < size + offset; i++) {
		value <<= 1;
		if (intervals[i] > avglen)
			value += 1;
	}

	/* Calculate CRC */
	crc = 0;
	offset = sizeof(value) * 8 + 1;
	size = sizeof(crc) * 8;
	for (i = offset;  i < size + offset; i++) {
		crc <<= 1;
		if (intervals[i] > avglen)
			crc += 1;
	}

	calc = 0;
	for (i = 0; i < sizeof(value); i++)
		calc += (value >> (8*i)) & GPIOTHS_DHT_ONEBYTEMASK;

#ifdef GPIOTHS_DEBUG
	/* Debug bits */
	for (i = 0; i < GPIOTHS_DHT_CYCLES; i++)
		device_printf(dev, "%d: %d %d\n", i, calibrations[i],
		    intervals[i]);

	device_printf(dev, "len=%d, data=%x, crc=%x/%x\n", avglen, value, crc,
	    calc);
#endif /* GPIOTHS_DEBUG */

	/* CRC check */
	if (calc != crc) {
		err = -1;
		goto error;
	}

	sc->fails = 0;
	sc->temp = (value >> GPIOTHS_DHT_TEMP_SHIFT) & GPIOTHS_DHT_ONEBYTEMASK;
	sc->hum = (value >> GPIOTHS_DHT_HUM_SHIFT) & GPIOTHS_DHT_ONEBYTEMASK;

#ifdef GPIOTHS_DEBUG
	/* Debug bits */
	device_printf(dev, "fails=%d, temp=%d, hum=%d\n", sc->fails,
	    sc->temp, sc->hum);
#endif /* GPIOTHS_DEBUG */

	return (0);
error:
	sc->fails++;
	return (err);
}

static void
gpioths_poll(void *arg)
{
	struct gpioths_softc	*sc;
	device_t		 dev;

	dev = (device_t)arg;
	sc = device_get_softc(dev);

	gpioths_dht_readbytes(device_get_parent(dev), dev);
	callout_schedule(&sc->callout, GPIOTHS_POLLTIME * hz);
}

static int
gpioths_temp_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct gpioths_softc	*sc;
	int			 value;

	sc = (struct gpioths_softc*)arg1;
	value = sc->temp;

	return (sysctl_handle_int(oidp, &value, 0, req));
}

static int
gpioths_hum_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct gpioths_softc	*sc;
	int			 value;

	sc = (struct gpioths_softc*)arg1;
	value = sc->hum;

	return (sysctl_handle_int(oidp, &value, 0, req));
}


static int
gpioths_fails_sysctl(SYSCTL_HANDLER_ARGS)
{
	struct gpioths_softc	*sc;
	int			 value;

	sc = (struct gpioths_softc*)arg1;
	value = sc->fails;

	return (sysctl_handle_int(oidp, &value, 0, req));
}

static int
gpioths_attach(device_t dev)
{
	struct gpioths_softc	*sc;
	struct sysctl_ctx_list	*ctx;
	struct sysctl_oid	*tree;

	sc = device_get_softc(dev);
	ctx = device_get_sysctl_ctx(dev);
	tree = device_get_sysctl_tree(dev);

	sc->dev = dev;

	callout_init(&sc->callout, 1);
	callout_reset(&sc->callout, GPIOTHS_POLLTIME * hz, gpioths_poll, dev);

	sc->temp_oid = SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "temperature", CTLTYPE_INT | CTLFLAG_RD, sc, 0,
	    gpioths_temp_sysctl, "I", "temperature(C)");

	sc->hum_oid = SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "humidity", CTLTYPE_INT | CTLFLAG_RD, sc, 0,
	    gpioths_hum_sysctl, "I", "humidity(%)");

	sc->fails_oid = SYSCTL_ADD_PROC(ctx, SYSCTL_CHILDREN(tree), OID_AUTO,
	    "fails", CTLTYPE_INT | CTLFLAG_RD, sc, 0,
	    gpioths_fails_sysctl, "I", "fails since last successful read");

	return (0);
}

static int
gpioths_detach(device_t dev)
{

	return (0);
}

/* DDB bits */
#include "opt_ddb.h"
#ifdef DDB
#include <ddb/ddb.h>
#include <ddb/db_lex.h>
#include <sys/cons.h>

static struct command_table db_gpioths_table = LIST_HEAD_INITIALIZER(db_t4_table);
_DB_SET(_show, gpioths, NULL, db_show_table, 0, &db_gpioths_table);

DB_FUNC(read, db_show_gpiothsread, db_gpioths_table, CS_OWN, NULL)
{
	device_t	dev;
	int		t;
	int		init;

	init = 0;
	t = db_read_token();
	if (t == tIDENT) {
		dev = device_lookup_by_name(db_tok_string);
		init = 1;
	}

	db_skip_to_eol();

	if (init)
		db_printf("read: 0x%x\n",
		    gpioths_dht_readbytes(dev, device_get_parent(dev)));
	else
		db_printf("usage: show gpioths read <gpiothsdevice>\n");

return;
}
#endif /* DDB */

/* Driver bits */
static device_method_t gpioths_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,			gpioths_probe),
	DEVMETHOD(device_attach,		gpioths_attach),
	DEVMETHOD(device_detach,		gpioths_detach),

	DEVMETHOD_END
};

DEFINE_CLASS_0(gpioths, gpioths_driver, gpioths_methods, sizeof(struct gpioths_softc));
DRIVER_MODULE(gpioths, gpiobus, gpioths_driver, gpioths_devclass, 0, 0);
