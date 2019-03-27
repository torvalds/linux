/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2010, Oleksandr Tymoshenko <gonzo@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice unmodified, this list of conditions, and the following
 *    disclaimer.
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
#include <sys/time.h>
#include <sys/clock.h>
#include <sys/resource.h>
#include <sys/systm.h>
#include <sys/rman.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <mips/atheros/pcf2123reg.h>

#include <dev/spibus/spi.h>
#include "spibus_if.h"

#include "clock_if.h"

#define	YEAR_BASE	1970
#define	PCF2123_DELAY	50

struct pcf2123_rtc_softc {
	device_t dev;
};

static int pcf2123_rtc_probe(device_t dev);
static int pcf2123_rtc_attach(device_t dev);

static int pcf2123_rtc_gettime(device_t dev, struct timespec *ts);
static int pcf2123_rtc_settime(device_t dev, struct timespec *ts);

static int
pcf2123_rtc_probe(device_t dev)
{

	device_set_desc(dev, "PCF2123 SPI RTC");
	return (0);
}

static int
pcf2123_rtc_attach(device_t dev)
{
	struct pcf2123_rtc_softc *sc;
	struct spi_command cmd;
	unsigned char rxBuf[3];
	unsigned char txBuf[3];
	int err;

	sc = device_get_softc(dev);
	sc->dev = dev;

	clock_register(dev, 1000000);

	memset(&cmd, 0, sizeof(cmd));
	memset(rxBuf, 0, sizeof(rxBuf));
	memset(txBuf, 0, sizeof(txBuf));

	/* Make sure Ctrl1 and Ctrl2 are zeroes */
	txBuf[0] = PCF2123_WRITE(PCF2123_REG_CTRL1);
	cmd.rx_cmd = rxBuf;
	cmd.tx_cmd = txBuf;
	cmd.rx_cmd_sz = sizeof(rxBuf);
	cmd.tx_cmd_sz = sizeof(txBuf);
	err = SPIBUS_TRANSFER(device_get_parent(dev), dev, &cmd);
	DELAY(PCF2123_DELAY);

	return (0);
}

static int
pcf2123_rtc_gettime(device_t dev, struct timespec *ts)
{
	struct clocktime ct;
	struct spi_command cmd;
	unsigned char rxTimedate[8];
	unsigned char txTimedate[8];
	int err;

	memset(&cmd, 0, sizeof(cmd));
	memset(rxTimedate, 0, sizeof(rxTimedate));
	memset(txTimedate, 0, sizeof(txTimedate));

	/*
	 * Counter is stopped when access to time registers is in progress
	 * So there is no need to stop/start counter
	 */
	/* Start reading from seconds */
	txTimedate[0] = PCF2123_READ(PCF2123_REG_SECONDS);
	cmd.rx_cmd = rxTimedate;
	cmd.tx_cmd = txTimedate;
	cmd.rx_cmd_sz = sizeof(rxTimedate);
	cmd.tx_cmd_sz = sizeof(txTimedate);
	err = SPIBUS_TRANSFER(device_get_parent(dev), dev, &cmd);
	DELAY(PCF2123_DELAY);

	ct.nsec = 0;
	ct.sec = FROMBCD(rxTimedate[1] & 0x7f);
	ct.min = FROMBCD(rxTimedate[2] & 0x7f);
	ct.hour = FROMBCD(rxTimedate[3] & 0x3f);

	ct.dow = FROMBCD(rxTimedate[5] & 0x3f);

	ct.day = FROMBCD(rxTimedate[4] & 0x3f);
	ct.mon = FROMBCD(rxTimedate[6] & 0x1f);
	ct.year = YEAR_BASE + FROMBCD(rxTimedate[7]);

	return (clock_ct_to_ts(&ct, ts));
}

static int
pcf2123_rtc_settime(device_t dev, struct timespec *ts)
{
	struct clocktime ct;
	struct pcf2123_rtc_softc *sc;
	struct spi_command cmd;
	unsigned char rxTimedate[8];
	unsigned char txTimedate[8];
	int err;

	sc = device_get_softc(dev);

	/* Resolution: 1 sec */
	if (ts->tv_nsec >= 500000000)
		ts->tv_sec++;
	ts->tv_nsec = 0;
	clock_ts_to_ct(ts, &ct);

	memset(&cmd, 0, sizeof(cmd));
	memset(rxTimedate, 0, sizeof(rxTimedate));
	memset(txTimedate, 0, sizeof(txTimedate));

	/* Start reading from seconds */
	cmd.rx_cmd = rxTimedate;
	cmd.tx_cmd = txTimedate;
	cmd.rx_cmd_sz = sizeof(rxTimedate);
	cmd.tx_cmd_sz = sizeof(txTimedate);

	/*
	 * Counter is stopped when access to time registers is in progress
	 * So there is no need to stop/start counter
	 */
	txTimedate[0] = PCF2123_WRITE(PCF2123_REG_SECONDS);
	txTimedate[1] = TOBCD(ct.sec);
	txTimedate[2] = TOBCD(ct.min);
	txTimedate[3] = TOBCD(ct.hour);
	txTimedate[4] = TOBCD(ct.day);
	txTimedate[5] = TOBCD(ct.dow);
	txTimedate[6] = TOBCD(ct.mon);
	txTimedate[7] = TOBCD(ct.year - YEAR_BASE);

	err = SPIBUS_TRANSFER(device_get_parent(dev), dev, &cmd);
	DELAY(PCF2123_DELAY);

	return (err);
}

static device_method_t pcf2123_rtc_methods[] = {
	DEVMETHOD(device_probe,		pcf2123_rtc_probe),
	DEVMETHOD(device_attach,	pcf2123_rtc_attach),

	DEVMETHOD(clock_gettime,	pcf2123_rtc_gettime),
	DEVMETHOD(clock_settime,	pcf2123_rtc_settime),

	{ 0, 0 },
};

static driver_t pcf2123_rtc_driver = {
	"rtc",
	pcf2123_rtc_methods,
	sizeof(struct pcf2123_rtc_softc),
};
static devclass_t pcf2123_rtc_devclass;

DRIVER_MODULE(pcf2123_rtc, spibus, pcf2123_rtc_driver, pcf2123_rtc_devclass, 0, 0);
