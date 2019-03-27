/*-
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
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

/*
 * Driver for ISL12xx family i2c realtime clocks:
 *  - ISL1209 = 2B sram, tamper/event timestamp
 *  - ISL1218 = 8B sram, DS13xx pin compatible (but not software compatible)
 *  - ISL1219 = 2B sram, tamper/event timestamp
 *  - ISL1220 = 8B sram, separate Fout
 *  - ISL1221 = 2B sram, separate Fout, tamper/event timestamp
 *
 * This driver supports only the basic RTC functionality in all these chips.
 */

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/lock.h>
#include <sys/module.h>
#include <sys/sx.h>

#ifdef FDT
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>

#include "clock_if.h"
#include "iicbus_if.h"

/*
 * All register and bit names as found in the datasheet.  When a bit name ends
 * in 'B' that stands for "bar" and it is an active-low signal; something named
 * "EVENB" implies 1=event-disable, 0=event-enable.
 */

#define	ISL12XX_SC_REG		0x00		/* RTC Seconds */

#define	ISL12XX_SR_REG		0x07		/* Status */
#define	  ISL12XX_SR_ARST	  (1u << 7)	/*   Auto-reset on status read */
#define	  ISL12XX_SR_XTOSCB	  (1u << 5)	/*   Osc disable (use ext osc) */
#define	  ISL12XX_SR_WRTC	  (1u << 4)	/*   Write RTC enable */
#define	  ISL12XX_SR_EVT	  (1u << 3)	/*   Event occurred (w0c) */
#define	  ISL12XX_SR_ALM	  (1u << 2)	/*   Alarm occurred (w0c) */
#define	  ISL12XX_SR_BAT	  (1u << 1)	/*   Running on battery (w0c) */
#define	  ISL12XX_SR_RTCF	  (1u << 0)	/*   RTC fail (power loss) */
#define	  ISL12XX_SR_W0C_BITS (ISL12XX_SR_BAT | ISL12XX_SR_ALM | ISL12XX_SR_EVT)

#define	ISL12XX_INT_REG		0x08		/* Interrupts */
#define	  ISL12XX_INT_IM	  (1u << 7)	/*   Alarm interrupt mode */
#define	  ISL12XX_INT_ALME	  (1u << 6)	/*   Alarm enable */
#define	  ISL12XX_INT_LPMODE	  (1u << 5)	/*   Low Power mode */
#define	  ISL12XX_INT_FOBATB	  (1u << 4)	/*   Fout/IRQ disabled on bat */
#define	  ISL12XX_INT_FO_SHIFT	  0		/*   Frequency output select */
#define	  ISL12XX_INT_FO_MASK	  0x0f		/*   shift and mask. */

#define	ISL12XX_EV_REG		0x09		/* Event */
#define	  ISL12XX_EV_EVIENB	  (1u << 7)	/*   Disable internal pullup */
#define	  ISL12XX_EV_EVBATB	  (1u << 6)	/*   Disable ev detect on bat */
#define	  ISL12XX_EV_RTCHLT	  (1u << 5)	/*   Halt RTC on event */
#define	  ISL12XX_EV_EVEN	  (1u << 4)	/*   Event detect enable */
#define	  ISL12XX_EV_EHYS_SHIFT	  2		/*   Event input hysteresis */
#define	  ISL12XX_EV_EHYS_MASK	  0x03		/*   selection; see datasheet */
#define	  ISL12XX_EV_ESMP_SHIFT	  0		/*   Event input sample rate */
#define	  ISL12XX_EV_ESMP_MASK	  0x03		/*   selection; see datasheet */

#define	ISL12XX_ATR_REG		0x0a		/* Analog trim (osc adjust) */

#define	ISL12XX_DTR_REG		0x0b		/* Digital trim (osc adjust) */

#define	ISL12XX_SCA_REG		0x0c		/* Alarm seconds */

#define	ISL12XX_USR1_REG	0x12		/* User byte 1 */

#define	ISL12XX_USR2_REG	0x13		/* User byte 2 */

#define	ISL12XX_SCT_REG		0x14		/* Timestamp (event) seconds */

#define	ISL12XX_24HR_FLAG	(1u << 7)	/* Hours register 24-hr mode */
#define	ISL12XX_PM_FLAG		(1u << 5)	/* Hours register PM flag */
#define	ISL12xx_12HR_MASK	0x1f		/* Hours mask in AM/PM mode */
#define	ISL12xx_24HR_MASK	0x3f		/* Hours mask in 24-hr mode */

/*
 * A struct laid out in the same order as the time registers in the chip.
 */
struct time_regs {
	uint8_t sec, min, hour, day, month, year;
};

struct isl12xx_softc {
	device_t	dev;
	device_t	busdev;
	struct intr_config_hook 
			init_hook;
	bool		use_ampm;
};

#ifdef FDT
static struct ofw_compat_data compat_data[] = {
	{"isil,isl1209", 1},
	{"isil,isl1218", 1},
	{"isil,isl1219", 1},
	{"isil,isl1220", 1},
	{"isil,isl1221", 1},
	{NULL,           0},
};
#endif

/*
 * When doing i2c IO, indicate that we need to wait for exclusive bus ownership,
 * but that we should not wait if we already own the bus.  This lets us put
 * iicbus_acquire_bus() calls with a non-recursive wait at the entry of our API
 * functions to ensure that only one client at a time accesses the hardware for
 * the entire series of operations it takes to read or write the clock.
 */
#define	WAITFLAGS	(IIC_WAIT | IIC_RECURSIVE)

static inline int
isl12xx_read1(struct isl12xx_softc *sc, uint8_t reg, uint8_t *data) 
{

	return (iicdev_readfrom(sc->dev, reg, data, 1, WAITFLAGS));
}

static inline int
isl12xx_write1(struct isl12xx_softc *sc, uint8_t reg, uint8_t val) 
{

	return (iicdev_writeto(sc->dev, reg, &val, 1, WAITFLAGS));
}

static void
isl12xx_init(void *arg)
{
	struct isl12xx_softc *sc = arg;
	uint8_t sreg;

	config_intrhook_disestablish(&sc->init_hook);

	/*
	 * Check the clock-stopped/power-fail bit, just so we can report it to
	 * the user at boot time.
	 */
	isl12xx_read1(sc, ISL12XX_SR_REG, &sreg);
	if (sreg & ISL12XX_SR_RTCF) {
		device_printf(sc->dev,
		    "RTC clock stopped; check battery\n");
	}

	/*
	 * Register as a system realtime clock.
	 */
	clock_register_flags(sc->dev, 1000000, CLOCKF_SETTIME_NO_ADJ);
	clock_schedule(sc->dev, 1);
}

static int
isl12xx_probe(device_t dev)
{

#ifdef FDT
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data != 0) {
		device_set_desc(dev, "Intersil ISL12xx RTC");
		return (BUS_PROBE_DEFAULT);
	}
#endif
	return (ENXIO);
}

static int
isl12xx_attach(device_t dev)
{
	struct isl12xx_softc *sc = device_get_softc(dev);

	sc->dev = dev;
	sc->busdev = device_get_parent(sc->dev);

	/*
	 * Chip init must wait until interrupts are enabled.  Often i2c access
	 * works only when the interrupts are available.
	 */
	sc->init_hook.ich_func = isl12xx_init;
	sc->init_hook.ich_arg = sc;
	if (config_intrhook_establish(&sc->init_hook) != 0)
		return (ENOMEM);

	return (0);
}

static int
isl12xx_detach(device_t dev)
{

	clock_unregister(dev);
	return (0);
}

static int
isl12xx_gettime(device_t dev, struct timespec *ts)
{
	struct isl12xx_softc *sc = device_get_softc(dev);
	struct bcd_clocktime bct;
	struct time_regs tregs;
	int err;
	uint8_t hourmask, sreg;

	/*
	 * Read the status and time registers.
	 */
	if ((err = iicbus_request_bus(sc->busdev, sc->dev, IIC_WAIT)) == 0) {
		if ((err = isl12xx_read1(sc, ISL12XX_SR_REG, &sreg)) == 0) {
			err = iicdev_readfrom(sc->dev, ISL12XX_SC_REG, &tregs,
			    sizeof(tregs), WAITFLAGS);
		}
		iicbus_release_bus(sc->busdev, sc->dev);
	}
	if (err != 0)
		return (err);

	/* If power failed, we can't provide valid time. */
	if (sreg & ISL12XX_SR_RTCF)
		return (EINVAL);

	/* If chip is in AM/PM mode remember that for when we set time. */
	if (tregs.hour & ISL12XX_24HR_FLAG) {
		hourmask = ISL12xx_24HR_MASK;
	} else {
		sc->use_ampm = true;
		hourmask = ISL12xx_12HR_MASK;
	}

	bct.nsec = 0;
	bct.sec  = tregs.sec;
	bct.min  = tregs.min;
	bct.hour = tregs.hour & hourmask;
	bct.day  = tregs.day;
	bct.mon  = tregs.month;
	bct.year = tregs.year;
	bct.ispm = tregs.hour & ISL12XX_PM_FLAG;

	clock_dbgprint_bcd(sc->dev, CLOCK_DBG_READ, &bct); 
	return (clock_bcd_to_ts(&bct, ts, sc->use_ampm));
}

static int
isl12xx_settime(device_t dev, struct timespec *ts)
{
	struct isl12xx_softc *sc = device_get_softc(dev);
	struct bcd_clocktime bct;
	struct time_regs tregs;
	int err;
	uint8_t ampmflags, sreg;

	/*
	 * We request a timespec with no resolution-adjustment.  That also
	 * disables utc adjustment, so apply that ourselves.
	 */
	ts->tv_sec -= utc_offset();
	ts->tv_nsec = 0;
	clock_ts_to_bcd(ts, &bct, sc->use_ampm);
	clock_dbgprint_bcd(sc->dev, CLOCK_DBG_WRITE, &bct); 

	/* If the chip is in AM/PM mode, set flags as needed. */
	if (!sc->use_ampm)
		ampmflags = ISL12XX_24HR_FLAG;
	else
		ampmflags = bct.ispm ? ISL12XX_PM_FLAG : 0;

	tregs.sec   = bct.sec;
	tregs.min   = bct.min;
	tregs.hour  = bct.hour | ampmflags;
	tregs.day   = bct.day;
	tregs.month = bct.mon;
	tregs.year  = bct.year % 100;

	/*
	 * To set the time we have to set the WRTC enable bit in the control
	 * register, then write the time regs, then clear the WRTC bit.  While
	 * doing so we have to be careful to not write a 0 to any sreg bit which
	 * is "write 0 to clear". One of those bits could get set between
	 * reading and writing the register.  All those bits ignore attempts to
	 * write a 1, so just always OR-in all the W0C bits to be sure we never
	 * accidentally clear one.  We hold ownership of the i2c bus for the
	 * whole read-modify-write sequence.
	 */
	if ((err = iicbus_request_bus(sc->busdev, sc->dev, IIC_WAIT)) != 0)
		return (err);
	if ((err = isl12xx_read1(sc, ISL12XX_SR_REG, &sreg)) == 0) {
		sreg |= ISL12XX_SR_WRTC | ISL12XX_SR_W0C_BITS;
		if ((err = isl12xx_write1(sc, ISL12XX_SR_REG, sreg)) == 0) {
			err = iicdev_writeto(sc->dev, ISL12XX_SC_REG, &tregs,
			    sizeof(tregs), WAITFLAGS);
			sreg &= ~ISL12XX_SR_WRTC;
			isl12xx_write1(sc, ISL12XX_SR_REG, sreg);
		}
	}
	iicbus_release_bus(sc->busdev, sc->dev);

	return (err);
}

static device_method_t isl12xx_methods[] = {
        /* device_if methods */
	DEVMETHOD(device_probe,		isl12xx_probe),
	DEVMETHOD(device_attach,	isl12xx_attach),
	DEVMETHOD(device_detach,	isl12xx_detach),

        /* clock_if methods */
	DEVMETHOD(clock_gettime,	isl12xx_gettime),
	DEVMETHOD(clock_settime,	isl12xx_settime),

	DEVMETHOD_END,
};

static driver_t isl12xx_driver = {
	"isl12xx",
	isl12xx_methods,
	sizeof(struct isl12xx_softc),
};
static devclass_t isl12xx_devclass;

DRIVER_MODULE(isl12xx, iicbus, isl12xx_driver, isl12xx_devclass, NULL, NULL);
MODULE_VERSION(isl12xx, 1);
MODULE_DEPEND(isl12xx, iicbus, IICBUS_MINVER, IICBUS_PREFVER, IICBUS_MAXVER);
