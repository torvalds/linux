/*-
 * Copyright (c) 2017 Hiroki Mori.  All rights reserved.
 * Copyright (c) 2017 Ian Lepore.  All rights reserved.
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
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This code base on isl12xx.c
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for realtime clock EPSON RTC-8583
 */

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>

#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include "clock_if.h"
#include "iicbus_if.h"

#define	RTC8583_SC_REG		0x01		/* RTC Seconds */
#define RTC8583_USERSRAM_REG	0x10		/* User SRAM register (first) */
#define	MAX_TRANSFER		16

/*
 * A struct laid out in the same order as the time registers in the chip.
 */
struct time_regs {
	uint8_t	msec, sec, min, hour, day, month;
};

struct rtc8583_softc {
	device_t	dev;
	device_t	busdev;
	struct intr_config_hook 
			init_hook;
};

#ifdef FDT
static struct ofw_compat_data compat_data[] = {
	{"epson,rtc8583", 1},
	{NULL,           0},
};
#endif

static void	rtc8583_init(void *arg);
static int	rtc8583_probe(device_t dev);
static int	rtc8583_attach(device_t dev);
static int	rtc8583_detach(device_t dev);

static int	rtc8583_gettime(device_t dev, struct timespec *ts);
static int	rtc8583_settime(device_t dev, struct timespec *ts);

static int	rtc8583_writeto(device_t slavedev, uint8_t regaddr, 
		    void *buffer, uint16_t buflen, int waithow);

/* Implementation */

static int 
rtc8583_writeto(device_t slavedev, uint8_t regaddr, void *buffer,
    uint16_t buflen, int waithow)
{
	struct iic_msg	msgs;
	uint8_t		slaveaddr;
	uint8_t		newbuf[MAX_TRANSFER];

	slaveaddr = iicbus_get_addr(slavedev);

	newbuf[0] = regaddr;
	memcpy(newbuf + 1, buffer, buflen);
	msgs.slave = slaveaddr;
	msgs.flags = IIC_M_WR;
	msgs.len   = 1 + buflen;
	msgs.buf   = newbuf;

	return (iicbus_transfer_excl(slavedev, &msgs, 1, waithow));
}

static inline int
rtc8583_read1(struct rtc8583_softc *sc, uint8_t reg, uint8_t *data) 
{

	return (iicdev_readfrom(sc->dev, reg, data, 1, IIC_WAIT));
}

static inline int
rtc8583_write1(struct rtc8583_softc *sc, uint8_t reg, uint8_t val) 
{

	return (rtc8583_writeto(sc->dev, reg, &val, 1, IIC_WAIT));
}

static void
rtc8583_init(void *arg)
{
	struct rtc8583_softc	*sc;
	
	sc = (struct rtc8583_softc*)arg;
	config_intrhook_disestablish(&sc->init_hook);

	/*
	 * Register as a system realtime clock.
	 */
	clock_register_flags(sc->dev, 1000000, CLOCKF_SETTIME_NO_ADJ);
	clock_schedule(sc->dev, 1);
	return;
}

static int
rtc8583_probe(device_t dev)
{

#ifdef FDT
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
		device_set_desc(dev, "EPSON RTC-8583");
		return (BUS_PROBE_DEFAULT);
	}
#endif
	return (ENXIO);
}

static int
rtc8583_attach(device_t dev)
{
	struct rtc8583_softc	*sc;
	
	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->busdev = device_get_parent(sc->dev);

	/*
	 * Chip init must wait until interrupts are enabled.  Often i2c access
	 * works only when the interrupts are available.
	 */
	sc->init_hook.ich_func = rtc8583_init;
	sc->init_hook.ich_arg = sc;
	if (config_intrhook_establish(&sc->init_hook) != 0)
		return (ENOMEM);

	return (0);
}

static int
rtc8583_detach(device_t dev)
{

	clock_unregister(dev);
	return (0);
}

static int
rtc8583_gettime(device_t dev, struct timespec *ts)
{
	struct rtc8583_softc	*sc;
	struct bcd_clocktime	 bct;
	struct time_regs	 tregs;
	uint8_t			 y, ytmp, sreg;
	int 			 err;

	sc = device_get_softc(dev);

	/* Read the bcd time registers. */
	if ((err = iicdev_readfrom(sc->dev, RTC8583_SC_REG, &tregs, sizeof(tregs),
	    IIC_WAIT)) != 0)
		return (err);

	y = tregs.day >> 6;
	/* Get year from user SRAM */
	rtc8583_read1(sc, RTC8583_USERSRAM_REG,  &sreg);
	
	/* 
	 * Check if year adjustment is required.
	 * RTC has only 2 bits for year value (i.e. maximum is 4 years), so
	 * full year value is stored in user SRAM and updated manually or 
	 * by this code.
	 */
	ytmp = sreg & 0x03;
	if (ytmp != y) {
		/* shift according to difference */
		sreg += y - ytmp;
		
		/* check if overflow happened */
		if (ytmp > y)
			sreg += 4;
		
		if ((err = iicbus_request_bus(sc->busdev, sc->dev, IIC_WAIT)) != 0)
			return (err);
		rtc8583_write1(sc, RTC8583_USERSRAM_REG, sreg);
		iicbus_release_bus(sc->busdev, sc->dev);
	}

	if (!validbcd(tregs.msec))
		return (EINVAL);

        /* The 'msec' reg is actually 1/100ths, in bcd.  */
	bct.nsec = bcd2bin(tregs.msec) * 10 * 1000 * 1000;
	bct.sec  = tregs.sec;
	bct.min  = tregs.min;
	bct.hour = tregs.hour & 0x3f;
	bct.day  = tregs.day & 0x3f;
	bct.mon  = tregs.month & 0x1f;
	bct.year = bin2bcd(sreg % 100);

	clock_dbgprint_bcd(sc->dev, CLOCK_DBG_READ, &bct); 
	return (clock_bcd_to_ts(&bct, ts, false));
}

static int
rtc8583_settime(device_t dev, struct timespec *ts)
{
	struct rtc8583_softc	*sc;
	struct bcd_clocktime 	 bct;
	struct time_regs	 tregs;
	uint8_t			 sreg;
	int 			 err;

	sc = device_get_softc(dev);
	ts->tv_sec -= utc_offset();
	clock_ts_to_bcd(ts, &bct, false);
	clock_dbgprint_bcd(sc->dev, CLOCK_DBG_WRITE, &bct);

	/* The 'msec' reg is actually 1/100ths, in bcd.  */
	tregs.msec  = bin2bcd(ts->tv_nsec / (10 * 1000 * 1000));
	tregs.sec   = bct.sec;
	tregs.min   = bct.min;
	tregs.hour  = bct.hour;
	tregs.day   = bct.day | (bct.year & 0x03 << 6);
	tregs.month = bct.mon;

	if ((err = iicbus_request_bus(sc->busdev, sc->dev, IIC_WAIT)) != 0)
		return (err);
	err = rtc8583_writeto(sc->dev, RTC8583_SC_REG, &tregs,
	    sizeof(tregs), IIC_WAIT);
	sreg = bcd2bin(bct.year & 0xff);
	/* save to year to sram */
	rtc8583_write1(sc, RTC8583_USERSRAM_REG, sreg);
	iicbus_release_bus(sc->busdev, sc->dev);

	return (err);
}

static device_method_t rtc8583_methods[] = {
        /* device_if methods */
	DEVMETHOD(device_probe,		rtc8583_probe),
	DEVMETHOD(device_attach,	rtc8583_attach),
	DEVMETHOD(device_detach,	rtc8583_detach),

        /* clock_if methods */
	DEVMETHOD(clock_gettime,	rtc8583_gettime),
	DEVMETHOD(clock_settime,	rtc8583_settime),

	DEVMETHOD_END,
};

static driver_t rtc8583_driver = {
	"rtc8583",
	rtc8583_methods,
	sizeof(struct rtc8583_softc),
};
static devclass_t rtc8583_devclass;

DRIVER_MODULE(rtc8583, iicbus, rtc8583_driver, rtc8583_devclass, NULL, NULL);
MODULE_VERSION(rtc8583, 1);
MODULE_DEPEND(rtc8583, iicbus, IICBUS_MINVER, IICBUS_PREFVER, IICBUS_MAXVER);
