/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD AND BSD-2-Clause-NetBSD
 *
 * Copyright (c) 2012 Yusuke Tanaka
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
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*-
 * Copyright (c) 2011 Frank Wille.
 * All rights reserved.
 *
 * Written by Frank Wille for The NetBSD Project.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for Seiko Instruments S-35390A Real-time Clock
 */

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/module.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>

#ifdef FDT
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include "clock_if.h"
#include "iicbus_if.h"

#define S390_DEVNAME		"s35390a_rtc"
#define S390_DEVCODE		0x6	/* 0110 */
/*
 * S-35390A uses 4-bit device code + 3-bit command in the slave address
 * field.  The possible combination is 0x60-0x6f including the R/W bit.
 * 0x60 means an write access to status register 1.
 */
#define S390_ADDR		(S390_DEVCODE << 4)

/* Registers are encoded into the slave address */
#define S390_STATUS1		(0 << 1)
#define S390_STATUS2		(1 << 1)
#define S390_REALTIME1		(2 << 1)
#define S390_REALTIME2		(3 << 1)
#define S390_INT1_1		(4 << 1)
#define S390_INT1_2		(5 << 1)
#define S390_CLOCKADJ		(6 << 1)
#define S390_FREE		(7 << 1)

/* Status1 bits */
#define S390_ST1_POC		(1 << 7)
#define S390_ST1_BLD		(1 << 6)
#define S390_ST1_24H		(1 << 1)
#define S390_ST1_RESET		(1 << 0)

/* Status2 bits */
#define S390_ST2_TEST		(1 << 7)

/* Realtime1 data bytes */
#define S390_RT1_NBYTES		7
#define S390_RT1_YEAR		0
#define S390_RT1_MONTH		1
#define S390_RT1_DAY		2
#define S390_RT1_WDAY		3
#define S390_RT1_HOUR		4
#define S390_RT1_MINUTE		5
#define S390_RT1_SECOND		6

struct s390rtc_softc {
	device_t	sc_dev;
	uint16_t	sc_addr;
};

/*
 * S-35390A interprets bits in each byte on SDA in reverse order.
 * bitreverse() reverses the bits in uint8_t.
 */
static const uint8_t nibbletab[] = {
	/* 0x0 0000 -> 0000 */	0x0,
	/* 0x1 0001 -> 1000 */	0x8,
	/* 0x2 0010 -> 0100 */	0x4,
	/* 0x3 0011 -> 1100 */	0xc,
	/* 0x4 0100 -> 0010 */	0x2,
	/* 0x5 0101 -> 1010 */	0xa,
	/* 0x6 0110 -> 0110 */	0x6,
	/* 0x7 0111 -> 1110 */	0xe,
	/* 0x8 1000 -> 0001 */	0x1,
	/* 0x9 1001 -> 1001 */	0x9,
	/* 0xa 1010 -> 0101 */	0x5,
	/* 0xb 1011 -> 1101 */	0xd,
	/* 0xc 1100 -> 0011 */	0x3,
	/* 0xd 1101 -> 1011 */	0xb,
	/* 0xe 1110 -> 0111 */	0x7,
	/* 0xf 1111 -> 1111 */	0xf, };

static uint8_t
bitreverse(uint8_t x)
{

	return (nibbletab[x & 0xf] << 4) | nibbletab[x >> 4];
}

static int
s390rtc_read(device_t dev, uint8_t reg, uint8_t *buf, size_t len)
{
	struct s390rtc_softc *sc = device_get_softc(dev);
	struct iic_msg msg[] = {
		{
			.slave = sc->sc_addr | reg,
			.flags = IIC_M_RD,
			.len = len,
			.buf = buf,
		},
	};
	int i;
	int error;

	error = iicbus_transfer_excl(dev, msg, 1, IIC_WAIT);
	if (error)
		return (error);

	/* this chip returns each byte in reverse order */
	for (i = 0; i < len; ++i)
		buf[i] = bitreverse(buf[i]);

	return (0);
}

static int
s390rtc_write(device_t dev, uint8_t reg, uint8_t *buf, size_t len)
{
	struct s390rtc_softc *sc = device_get_softc(dev);
	struct iic_msg msg[] = {
		{
			.slave = sc->sc_addr | reg,
			.flags = IIC_M_WR,
			.len = len,
			.buf = buf,
		},
	};
	int i;

	/* this chip expects each byte in reverse order */
	for (i = 0; i < len; ++i)
		buf[i] = bitreverse(buf[i]);

	return (iicbus_transfer_excl(dev, msg, 1, IIC_WAIT));
}

static int
s390rtc_probe(device_t dev)
{

#ifdef FDT
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "sii,s35390a"))
		return (ENXIO);
#else
	if (iicbus_get_addr(dev) != S390_ADDR) {
		if (bootverbose)
			device_printf(dev, "slave address mismatch. "
			    "(%02x != %02x)\n", iicbus_get_addr(dev),
			    S390_ADDR);
		return (ENXIO);
	}
#endif
	device_set_desc(dev, "Seiko Instruments S-35390A RTC");

	return (BUS_PROBE_DEFAULT);
}

static void
s390rtc_start(void *arg)
{
	device_t dev;
	uint8_t reg;
	int error;

	dev = arg;

	/* Reset the chip and turn on 24h mode, after power-off or battery. */
	error = s390rtc_read(dev, S390_STATUS1, &reg, 1);
	if (error) {
		device_printf(dev, "%s: cannot read status1 register\n",
		     __func__);
		return;
	}
	if (reg & (S390_ST1_POC | S390_ST1_BLD)) {
		reg |= S390_ST1_24H | S390_ST1_RESET;
		error = s390rtc_write(dev, S390_STATUS1, &reg, 1);
		if (error) {
			device_printf(dev,
			    "%s: cannot initialize\n", __func__);
			return;
		}
	}

	/* Disable the test mode, when enabled. */
	error = s390rtc_read(dev, S390_STATUS2, &reg, 1);
	if (error) {
		device_printf(dev, "%s: cannot read status2 register\n",
		    __func__);
		return;
	}
	if (reg & S390_ST2_TEST) {
		reg &= ~S390_ST2_TEST;
		error = s390rtc_write(dev, S390_STATUS2, &reg, 1);
		if (error) {
			device_printf(dev,
			    "%s: cannot disable the test mode\n", __func__);
			return;
		}
	}

	clock_register(dev, 1000000);   /* 1 second resolution */
}

static int
s390rtc_attach(device_t dev)
{
	struct s390rtc_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_addr = iicbus_get_addr(dev);

	config_intrhook_oneshot(s390rtc_start, dev);

	return (0);
}

static int
s390rtc_detach(device_t dev)
{

	clock_unregister(dev);
	return (0);
}

static int
s390rtc_gettime(device_t dev, struct timespec *ts)
{
	uint8_t bcd[S390_RT1_NBYTES];
	struct bcd_clocktime bct;
	int error;

	error = s390rtc_read(dev, S390_REALTIME1, bcd, S390_RT1_NBYTES);
	if (error) {
		device_printf(dev, "%s: cannot read realtime1 register\n",
		    __func__);
		return (error);
	}

	/*
	 * Convert the register values into something useable.
	 */
	bct.nsec = 0;
	bct.sec  = bcd[S390_RT1_SECOND];
	bct.min  = bcd[S390_RT1_MINUTE];
	bct.hour = bcd[S390_RT1_HOUR] & 0x3f;
	bct.day  = bcd[S390_RT1_DAY];
	bct.dow  = bcd[S390_RT1_WDAY] & 0x07;
	bct.mon  = bcd[S390_RT1_MONTH];
	bct.year = bcd[S390_RT1_YEAR];

	clock_dbgprint_bcd(dev, CLOCK_DBG_READ, &bct); 
	return (clock_bcd_to_ts(&bct, ts, false));
}

static int
s390rtc_settime(device_t dev, struct timespec *ts)
{
	uint8_t bcd[S390_RT1_NBYTES];
	struct bcd_clocktime bct;

	clock_ts_to_bcd(ts, &bct, false);
	clock_dbgprint_bcd(dev, CLOCK_DBG_WRITE, &bct); 

	/*
	 * Convert our time representation into something the S-xx390
	 * can understand.
	 */
	bcd[S390_RT1_SECOND] = bct.sec;
	bcd[S390_RT1_MINUTE] = bct.min;
	bcd[S390_RT1_HOUR]   = bct.hour;
	bcd[S390_RT1_DAY]    = bct.day;
	bcd[S390_RT1_WDAY]   = bct.dow;
	bcd[S390_RT1_MONTH]  = bct.mon;
	bcd[S390_RT1_YEAR]   = bct.year & 0xff;

	return (s390rtc_write(dev, S390_REALTIME1, bcd, S390_RT1_NBYTES));
}

static device_method_t s390rtc_methods[] = {
	DEVMETHOD(device_probe,		s390rtc_probe),
	DEVMETHOD(device_attach,	s390rtc_attach),
	DEVMETHOD(device_detach,	s390rtc_detach),

	DEVMETHOD(clock_gettime,	s390rtc_gettime),
	DEVMETHOD(clock_settime,	s390rtc_settime),

	DEVMETHOD_END
};

static driver_t s390rtc_driver = {
	S390_DEVNAME,
	s390rtc_methods,
	sizeof(struct s390rtc_softc),
};
static devclass_t s390rtc_devclass;

DRIVER_MODULE(s35390a, iicbus, s390rtc_driver, s390rtc_devclass, NULL, NULL);
MODULE_VERSION(s35390a, 1);
MODULE_DEPEND(s35390a, iicbus, 1, 1, 1);
