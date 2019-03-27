/*-
 * Copyright (c) 2017 Ian Lepore <ian@freebsd.org>
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

/*
 * Driver for Dallas/Maxim DS13xx real-time clock/calendar chips:
 *
 * - DS1307 = Original/basic rtc + 56 bytes ram; 5v only.
 * - DS1308 = Updated 1307, available in 1.8v-5v variations.
 * - DS1337 = Like 1308, integrated xtal, 32khz output on at powerup.
 * - DS1338 = Like 1308, integrated xtal.
 * - DS1339 = Like 1337, integrated xtal, integrated trickle charger.
 * - DS1340 = Like 1338, ST M41T00 compatible.
 * - DS1341 = Like 1338, can slave-sync osc to external clock signal.
 * - DS1342 = Like 1341 but requires different xtal.
 * - DS1371 = 32-bit binary counter, watchdog timer.
 * - DS1372 = 32-bit binary counter, 64-bit unique id in rom.
 * - DS1374 = 32-bit binary counter, watchdog timer, trickle charger.
 * - DS1375 = Like 1308 but only 16 bytes ram.
 * - DS1388 = Rtc, watchdog timer, 512 bytes eeprom (not sram).
 *
 * This driver supports only basic timekeeping functions.  It provides no access
 * to or control over any other functionality provided by the chips.
 */

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/endian.h>
#include <sys/kernel.h>
#include <sys/libkern.h>
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

/*
 * I2C address 1101 000x
 */
#define	DS13xx_ADDR		0xd0

/*
 * Registers, bits within them, and masks for the various chip types.
 */

#define	DS13xx_R_NONE		0xff	/* Placeholder */

#define	DS130x_R_CONTROL	0x07
#define	DS133x_R_CONTROL	0x0e
#define	DS1340_R_CONTROL	0x07
#define	DS1341_R_CONTROL	0x0e
#define	DS1371_R_CONTROL	0x07
#define	DS1372_R_CONTROL	0x07
#define	DS1374_R_CONTROL	0x07
#define	DS1375_R_CONTROL	0x0e
#define	DS1388_R_CONTROL	0x0c

#define	DS13xx_R_SECOND		0x00
#define	DS1388_R_SECOND		0x01

#define	DS130x_R_STATUS		DS13xx_R_NONE
#define	DS133x_R_STATUS		0x0f
#define	DS1340_R_STATUS		0x09
#define	DS137x_R_STATUS		0x08
#define	DS1388_R_STATUS		0x0b

#define	DS13xx_B_STATUS_OSF	0x80	/* OSF is 1<<7 in status and sec regs */
#define	DS13xx_B_HOUR_AMPM	0x40	/* AMPM mode is bit 1<<6 */
#define	DS13xx_B_HOUR_PM	0x20	/* PM hours indicated by 1<<5 */
#define	DS13xx_B_MONTH_CENTURY	0x80	/* 21st century indicated by 1<<7 */

#define	DS13xx_M_SECOND		0x7f	/* Masks for all BCD time regs... */
#define	DS13xx_M_MINUTE		0x7f
#define	DS13xx_M_12HOUR		0x1f
#define	DS13xx_M_24HOUR		0x3f
#define	DS13xx_M_DAY		0x3f
#define	DS13xx_M_MONTH		0x1f
#define	DS13xx_M_YEAR		0xff

/*
 * The chip types we support.
 */
enum {
	TYPE_NONE,
	TYPE_DS1307,
	TYPE_DS1308,
	TYPE_DS1337,
	TYPE_DS1338,
	TYPE_DS1339,
	TYPE_DS1340,
	TYPE_DS1341,
	TYPE_DS1342,
	TYPE_DS1371,
	TYPE_DS1372,
	TYPE_DS1374,
	TYPE_DS1375,
	TYPE_DS1388,

	TYPE_COUNT
};
static const char *desc_strings[] = {
	"",
	"Dallas/Maxim DS1307 RTC",
	"Dallas/Maxim DS1308 RTC",
	"Dallas/Maxim DS1337 RTC",
	"Dallas/Maxim DS1338 RTC",
	"Dallas/Maxim DS1339 RTC",
	"Dallas/Maxim DS1340 RTC",
	"Dallas/Maxim DS1341 RTC",
	"Dallas/Maxim DS1342 RTC",
	"Dallas/Maxim DS1371 RTC",
	"Dallas/Maxim DS1372 RTC",
	"Dallas/Maxim DS1374 RTC",
	"Dallas/Maxim DS1375 RTC",
	"Dallas/Maxim DS1388 RTC",
};
CTASSERT(nitems(desc_strings) == TYPE_COUNT);

/*
 * The time registers in the order they are laid out in hardware.
 */
struct time_regs {
	uint8_t sec, min, hour, wday, day, month, year;
};

struct ds13rtc_softc {
	device_t	dev;
	device_t	busdev;
	u_int		chiptype;	/* Type of DS13xx chip */
	uint8_t		secaddr;	/* Address of seconds register */
	uint8_t		osfaddr;	/* Address of register with OSF */
	bool		use_ampm;	/* Use AM/PM mode. */
	bool		use_century;	/* Use the Century bit. */
	bool		is_binary_counter; /* Chip has 32-bit binary counter. */
};

/*
 * We use the compat_data table to look up hint strings in the non-FDT case, so
 * define the struct locally when we don't get it from ofw_bus_subr.h.
 */
#ifdef FDT
typedef struct ofw_compat_data ds13_compat_data;
#else
typedef struct {
	const char *ocd_str;
	uintptr_t  ocd_data;
} ds13_compat_data;
#endif

static ds13_compat_data compat_data[] = {
	{"dallas,ds1307",   TYPE_DS1307},
	{"dallas,ds1308",   TYPE_DS1308},
	{"dallas,ds1337",   TYPE_DS1337},
	{"dallas,ds1338",   TYPE_DS1338},
	{"dallas,ds1339",   TYPE_DS1339},
	{"dallas,ds1340",   TYPE_DS1340},
	{"dallas,ds1341",   TYPE_DS1341},
	{"dallas,ds1342",   TYPE_DS1342},
	{"dallas,ds1371",   TYPE_DS1371},
	{"dallas,ds1372",   TYPE_DS1372},
	{"dallas,ds1374",   TYPE_DS1374},
	{"dallas,ds1375",   TYPE_DS1375},
	{"dallas,ds1388",   TYPE_DS1388},

	{NULL,              TYPE_NONE},
};

static int
read_reg(struct ds13rtc_softc *sc, uint8_t reg, uint8_t *val)
{

	return (iicdev_readfrom(sc->dev, reg, val, sizeof(*val), IIC_WAIT));
}

static int
write_reg(struct ds13rtc_softc *sc, uint8_t reg, uint8_t val)
{

	return (iicdev_writeto(sc->dev, reg, &val, sizeof(val), IIC_WAIT));
}

static int
read_timeregs(struct ds13rtc_softc *sc, struct time_regs *tregs)
{
	int err;

	if ((err = iicdev_readfrom(sc->dev, sc->secaddr, tregs,
	    sizeof(*tregs), IIC_WAIT)) != 0)
		return (err);

	return (err);
}

static int
write_timeregs(struct ds13rtc_softc *sc, struct time_regs *tregs)
{

	return (iicdev_writeto(sc->dev, sc->secaddr, tregs,
	    sizeof(*tregs), IIC_WAIT));
}

static int
read_timeword(struct ds13rtc_softc *sc, time_t *secs)
{
	int err;
	uint8_t buf[4];

	if ((err = iicdev_readfrom(sc->dev, sc->secaddr, buf, sizeof(buf),
	    IIC_WAIT)) == 0)
		*secs = le32dec(buf);

	return (err);
}

static int
write_timeword(struct ds13rtc_softc *sc, time_t secs)
{
	uint8_t buf[4];

	le32enc(buf, (uint32_t)secs);
	return (iicdev_writeto(sc->dev, sc->secaddr, buf, sizeof(buf),
	    IIC_WAIT));
}

static void
ds13rtc_start(void *arg)
{
	struct ds13rtc_softc *sc;
	uint8_t ctlreg, statreg;

	sc = arg;

	/*
	 * Every chip in this family can be usefully initialized by writing 0 to
	 * the control register, except DS1375 which has an external oscillator
	 * controlled by values in the ctlreg that we know nothing about, so
	 * we'd best leave them alone.  For all other chips, writing 0 enables
	 * the oscillator, disables signals/outputs in battery-backed mode
	 * (saves power) and disables features like watchdog timers and alarms.
	 */
	switch (sc->chiptype) {
	case TYPE_DS1307:
	case TYPE_DS1308:
	case TYPE_DS1338:
	case TYPE_DS1340:
	case TYPE_DS1371:
	case TYPE_DS1372:
	case TYPE_DS1374:
		ctlreg = DS130x_R_CONTROL;
		break;
	case TYPE_DS1337:
	case TYPE_DS1339:
		ctlreg = DS133x_R_CONTROL;
		break;
	case TYPE_DS1341:
	case TYPE_DS1342:
		ctlreg = DS1341_R_CONTROL;
		break;
	case TYPE_DS1375:
		ctlreg = DS13xx_R_NONE;
		break;
	case TYPE_DS1388:
		ctlreg = DS1388_R_CONTROL;
		break;
	default:
		device_printf(sc->dev, "missing init code for this chiptype\n");
		return;
	}
	if (ctlreg != DS13xx_R_NONE)
		write_reg(sc, ctlreg, 0);

	/*
	 * Common init.  Read the OSF/CH status bit and report stopped clocks to
	 * the user.  The status bit will be cleared the first time we write
	 * valid time to the chip (and must not be cleared before that).
	 */
	if (read_reg(sc, sc->osfaddr, &statreg) != 0) {
		device_printf(sc->dev, "cannot read RTC clock status bit\n");
		return;
	}
	if (statreg & DS13xx_B_STATUS_OSF) {
		device_printf(sc->dev, 
		    "WARNING: RTC battery failed; time is invalid\n");
	}

	/*
	 * Figure out whether the chip is configured for AM/PM mode.  On all
	 * chips that do AM/PM mode, the flag bit is in the hours register,
	 * which is secaddr+2.
	 */
	if ((sc->chiptype != TYPE_DS1340) && !sc->is_binary_counter) {
		if (read_reg(sc, sc->secaddr + 2, &statreg) != 0) {
			device_printf(sc->dev,
			    "cannot read RTC clock AM/PM bit\n");
			return;
		}
		if (statreg & DS13xx_B_HOUR_AMPM)
			sc->use_ampm = true;
	}

	/*
	 * Everything looks good if we make it to here; register as an RTC.
	 * Schedule RTC updates to happen just after top-of-second.
	 */
	clock_register_flags(sc->dev, 1000000, CLOCKF_SETTIME_NO_ADJ);
	clock_schedule(sc->dev, 1);
}

static int
ds13rtc_gettime(device_t dev, struct timespec *ts)
{
	struct bcd_clocktime bct;
	struct time_regs tregs;
	struct ds13rtc_softc *sc;
	int err;
	uint8_t statreg, hourmask;

	sc = device_get_softc(dev);

	/* Read the OSF/CH bit; if the clock stopped we can't provide time. */
	if ((err = read_reg(sc, sc->osfaddr, &statreg)) != 0) {
		return (err);
	}
	if (statreg & DS13xx_B_STATUS_OSF)
		return (EINVAL); /* hardware is good, time is not. */

	/* If the chip counts time in binary, we just read and return it. */
	if (sc->is_binary_counter) {
		ts->tv_nsec = 0;
		return (read_timeword(sc, &ts->tv_sec));
	}

	/*
	 * Chip counts in BCD, read and decode it...
	 */
	if ((err = read_timeregs(sc, &tregs)) != 0) {
		device_printf(dev, "cannot read RTC time\n");
		return (err);
	}

	if (sc->use_ampm)
		hourmask = DS13xx_M_12HOUR;
	else
		hourmask = DS13xx_M_24HOUR;

	bct.nsec = 0;
	bct.ispm = tregs.hour  & DS13xx_B_HOUR_PM;
	bct.sec  = tregs.sec   & DS13xx_M_SECOND;
	bct.min  = tregs.min   & DS13xx_M_MINUTE;
	bct.hour = tregs.hour  & hourmask;
	bct.day  = tregs.day   & DS13xx_M_DAY;
	bct.mon  = tregs.month & DS13xx_M_MONTH;
	bct.year = tregs.year  & DS13xx_M_YEAR;

	/*
	 * If this chip has a century bit, honor it.  Otherwise let
	 * clock_ct_to_ts() infer the century from the 2-digit year.
	 */
	if (sc->use_century)
		bct.year += (tregs.month & DS13xx_B_MONTH_CENTURY) ? 0x100 : 0;

	clock_dbgprint_bcd(sc->dev, CLOCK_DBG_READ, &bct); 
	err = clock_bcd_to_ts(&bct, ts, sc->use_ampm);

	return (err);
}

static int
ds13rtc_settime(device_t dev, struct timespec *ts)
{
	struct bcd_clocktime bct;
	struct time_regs tregs;
	struct ds13rtc_softc *sc;
	int err;
	uint8_t cflag, statreg, pmflags;

	sc = device_get_softc(dev);

	/*
	 * We request a timespec with no resolution-adjustment.  That also
	 * disables utc adjustment, so apply that ourselves.
	 */
	ts->tv_sec -= utc_offset();

	/* If the chip counts time in binary, store tv_sec and we're done. */
	if (sc->is_binary_counter)
		return (write_timeword(sc, ts->tv_sec));

	clock_ts_to_bcd(ts, &bct, sc->use_ampm);
	clock_dbgprint_bcd(sc->dev, CLOCK_DBG_WRITE, &bct); 

	/* If the chip is in AMPM mode deal with the PM flag. */
	pmflags = 0;
	if (sc->use_ampm) {
		pmflags = DS13xx_B_HOUR_AMPM;
		if (bct.ispm)
			pmflags |= DS13xx_B_HOUR_PM;
	}

	/* If the chip has a century bit, set it as needed. */
	cflag = 0;
	if (sc->use_century) {
		if (bct.year >= 2000)
			cflag |= DS13xx_B_MONTH_CENTURY;
	}

	tregs.sec   = bct.sec;
	tregs.min   = bct.min;
	tregs.hour  = bct.hour | pmflags;
	tregs.day   = bct.day;
	tregs.month = bct.mon | cflag;
	tregs.year  = bct.year & 0xff;
	tregs.wday  = bct.dow;

	/*
	 * Set the time.  Reset the OSF bit if it is on and it is not part of
	 * the time registers (in which case writing time resets it).
	 */
	if ((err = write_timeregs(sc, &tregs)) != 0)
		goto errout;
	if (sc->osfaddr != sc->secaddr) {
		if ((err = read_reg(sc, sc->osfaddr, &statreg)) != 0)
			goto errout;
		if (statreg & DS13xx_B_STATUS_OSF) {
			statreg &= ~DS13xx_B_STATUS_OSF;
			err = write_reg(sc, sc->osfaddr, statreg);
		}
	}

errout:

	if (err != 0)
		device_printf(dev, "cannot update RTC time\n");

	return (err);
}

static int
ds13rtc_get_chiptype(device_t dev)
{
#ifdef FDT

	return (ofw_bus_search_compatible(dev, compat_data)->ocd_data);
#else
	ds13_compat_data *cdata;
	const char *htype; 

	/*
	 * We can only attach if provided a chiptype hint string.
	 */
	if (resource_string_value(device_get_name(dev), 
	    device_get_unit(dev), "compatible", &htype) != 0)
		return (TYPE_NONE);

	/*
	 * Loop through the ofw compat data comparing the hinted chip type to
	 * the compat strings.
	 */
	for (cdata = compat_data; cdata->ocd_str != NULL; ++cdata) {
		if (strcmp(htype, cdata->ocd_str) == 0)
			break;
	}
	return (cdata->ocd_data);
#endif
}

static int
ds13rtc_probe(device_t dev)
{
	int chiptype, goodrv;

#ifdef FDT
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	goodrv = BUS_PROBE_GENERIC;
#else
	goodrv = BUS_PROBE_NOWILDCARD;
#endif

	chiptype = ds13rtc_get_chiptype(dev);
	if (chiptype == TYPE_NONE)
		return (ENXIO);

	device_set_desc(dev, desc_strings[chiptype]);
	return (goodrv);
}

static int
ds13rtc_attach(device_t dev)
{
	struct ds13rtc_softc *sc;

	sc = device_get_softc(dev);
	sc->dev = dev;
	sc->busdev = device_get_parent(dev);

	/*
	 * We need to know what kind of chip we're driving.
	 */
	if ((sc->chiptype = ds13rtc_get_chiptype(dev)) == TYPE_NONE) {
		device_printf(dev, "impossible: cannot determine chip type\n");
		return (ENXIO);
	}

	/* The seconds register is in the same place on all except DS1388. */
	if (sc->chiptype == TYPE_DS1388) 
		sc->secaddr = DS1388_R_SECOND;
	else
		sc->secaddr = DS13xx_R_SECOND;

	/*
	 * The OSF/CH (osc failed/clock-halted) bit appears in different
	 * registers for different chip types.  The DS1375 has no OSF indicator
	 * because it has no internal oscillator; we just point to an always-
	 * zero bit in the status register for that chip.
	 */
	switch (sc->chiptype) {
	case TYPE_DS1307:
	case TYPE_DS1308:
	case TYPE_DS1338:
		sc->osfaddr = DS13xx_R_SECOND;
		break;
	case TYPE_DS1337:
	case TYPE_DS1339:
	case TYPE_DS1341:
	case TYPE_DS1342:
	case TYPE_DS1375:
		sc->osfaddr = DS133x_R_STATUS;
		sc->use_century = true;
		break;
	case TYPE_DS1340:
		sc->osfaddr = DS1340_R_STATUS;
		break;
	case TYPE_DS1371:
	case TYPE_DS1372:
	case TYPE_DS1374:
		sc->osfaddr = DS137x_R_STATUS;
		sc->is_binary_counter = true;
		break;
	case TYPE_DS1388:
		sc->osfaddr = DS1388_R_STATUS;
		break;
	}

	/*
	 * We have to wait until interrupts are enabled.  Sometimes I2C read
	 * and write only works when the interrupts are available.
	 */
	config_intrhook_oneshot(ds13rtc_start, sc);

	return (0);
}

static int
ds13rtc_detach(device_t dev)
{

	clock_unregister(dev);
	return (0);
}

static device_method_t ds13rtc_methods[] = {
	DEVMETHOD(device_probe,		ds13rtc_probe),
	DEVMETHOD(device_attach,	ds13rtc_attach),
	DEVMETHOD(device_detach,	ds13rtc_detach),

	DEVMETHOD(clock_gettime,	ds13rtc_gettime),
	DEVMETHOD(clock_settime,	ds13rtc_settime),

	DEVMETHOD_END
};

static driver_t ds13rtc_driver = {
	"ds13rtc",
	ds13rtc_methods,
	sizeof(struct ds13rtc_softc),
};

static devclass_t ds13rtc_devclass;

DRIVER_MODULE(ds13rtc, iicbus, ds13rtc_driver, ds13rtc_devclass, NULL, NULL);
MODULE_VERSION(ds13rtc, 1);
MODULE_DEPEND(ds13rtc, iicbus, IICBB_MINVER, IICBB_PREFVER, IICBB_MAXVER);
