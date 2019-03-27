/*-
 * Copyright (c) 2014-2015 Luiz Otavio O Souza <loos@FreeBSD.org>
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
 * Driver for Maxim DS3231[N] real-time clock/calendar.
 */

#include "opt_platform.h"

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/clock.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/sysctl.h>

#include <dev/iicbus/iicbus.h>
#include <dev/iicbus/iiconf.h>
#ifdef FDT
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>
#endif

#include <dev/iicbus/ds3231reg.h>

#include "clock_if.h"
#include "iicbus_if.h"

struct ds3231_softc {
	device_t	sc_dev;
	int		sc_last_c;
	int		sc_year0;
	struct intr_config_hook	enum_hook;
	uint16_t	sc_addr;	/* DS3231 slave address. */
	uint8_t		sc_ctrl;
	uint8_t		sc_status;
	bool		sc_use_ampm;
};

static void ds3231_start(void *);

static int
ds3231_read1(device_t dev, uint8_t reg, uint8_t *data)
{

	return (iicdev_readfrom(dev, reg, data, 1, IIC_INTRWAIT));
}

static int
ds3231_write1(device_t dev, uint8_t reg, uint8_t data)
{

	return (iicdev_writeto(dev, reg, &data, 1, IIC_INTRWAIT));
}

static int
ds3231_ctrl_read(struct ds3231_softc *sc)
{
	int error;

	error = ds3231_read1(sc->sc_dev, DS3231_CONTROL, &sc->sc_ctrl);
	if (error) {
		device_printf(sc->sc_dev, "cannot read from RTC.\n");
		return (error);
	}
	return (0);
}

static int
ds3231_ctrl_write(struct ds3231_softc *sc)
{
	int error;
	uint8_t data;

	/* Always enable the oscillator.  Always disable both alarms. */
	data = sc->sc_ctrl & ~DS3231_CTRL_MASK;
	error = ds3231_write1(sc->sc_dev, DS3231_CONTROL, data);
	if (error != 0)
		device_printf(sc->sc_dev, "cannot write to RTC.\n");

	return (error);
}

static int
ds3231_status_read(struct ds3231_softc *sc)
{
	int error;

	error = ds3231_read1(sc->sc_dev, DS3231_STATUS, &sc->sc_status);
	if (error) {
		device_printf(sc->sc_dev, "cannot read from RTC.\n");
		return (error);
	}

	return (0);
}

static int
ds3231_status_write(struct ds3231_softc *sc, int clear_a1, int clear_a2)
{
	int error;
	uint8_t data;

	data = sc->sc_status;
	if (clear_a1 == 0)
		data |= DS3231_STATUS_A1F;
	if (clear_a2 == 0)
		data |= DS3231_STATUS_A2F;
	error = ds3231_write1(sc->sc_dev, DS3231_STATUS, data);
	if (error != 0)
		device_printf(sc->sc_dev, "cannot write to RTC.\n");

	return (error);
}

static int
ds3231_temp_read(struct ds3231_softc *sc, int *temp)
{
	int error, neg, t;
	uint8_t buf8[2];
	uint16_t buf;

	error = iicdev_readfrom(sc->sc_dev, DS3231_TEMP, buf8, sizeof(buf8),
	    IIC_INTRWAIT);
	if (error != 0)
		return (error);
	buf = (buf8[0] << 8) | (buf8[1] & 0xff);
	neg = 0;
	if (buf & DS3231_NEG_BIT) {
		buf = ~(buf & DS3231_TEMP_MASK) + 1;
		neg = 1;
	}
	*temp = ((int16_t)buf >> 8) * 10;
	t = 0;
	if (buf & DS3231_0250C)
		t += 250;
	if (buf & DS3231_0500C)
		t += 500;
	t /= 100;
	*temp += t;
	if (neg)
		*temp = -(*temp);
	*temp += TZ_ZEROC;

	return (0);
}

static int
ds3231_temp_sysctl(SYSCTL_HANDLER_ARGS)
{
	int error, temp;
	struct ds3231_softc *sc;

	sc = (struct ds3231_softc *)arg1;
	if (ds3231_temp_read(sc, &temp) != 0)
		return (EIO);
	error = sysctl_handle_int(oidp, &temp, 0, req);

	return (error);
}

static int
ds3231_conv_sysctl(SYSCTL_HANDLER_ARGS)
{
	int error, conv, newc;
	struct ds3231_softc *sc;

	sc = (struct ds3231_softc *)arg1;
	error = ds3231_ctrl_read(sc);
	if (error != 0)
		return (error);
	newc = conv = (sc->sc_ctrl & DS3231_CTRL_CONV) ? 1 : 0;
	error = sysctl_handle_int(oidp, &newc, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (conv == 0 && newc != 0) {
		error = ds3231_status_read(sc);
		if (error != 0)
			return (error);
		if (sc->sc_status & DS3231_STATUS_BUSY)
			return (0);
		sc->sc_ctrl |= DS3231_CTRL_CONV;
		error = ds3231_ctrl_write(sc);
		if (error != 0)
			return (error);
	}

	return (error);
}

static int
ds3231_bbsqw_sysctl(SYSCTL_HANDLER_ARGS)
{
	int bbsqw, error, newb;
	struct ds3231_softc *sc;

	sc = (struct ds3231_softc *)arg1;
	error = ds3231_ctrl_read(sc);
	if (error != 0)
		return (error);
	bbsqw = newb = (sc->sc_ctrl & DS3231_CTRL_BBSQW) ? 1 : 0;
	error = sysctl_handle_int(oidp, &newb, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (bbsqw != newb) {
		sc->sc_ctrl &= ~DS3231_CTRL_BBSQW;
		if (newb)
			sc->sc_ctrl |= DS3231_CTRL_BBSQW;
		error = ds3231_ctrl_write(sc);
		if (error != 0)
			return (error);
	}

	return (error);
}

static int
ds3231_sqw_freq_sysctl(SYSCTL_HANDLER_ARGS)
{
	int ds3231_sqw_freq[] = { 1, 1024, 4096, 8192 };
	int error, freq, i, newf, tmp;
	struct ds3231_softc *sc;

	sc = (struct ds3231_softc *)arg1;
	error = ds3231_ctrl_read(sc);
	if (error != 0)
		return (error);
	tmp = (sc->sc_ctrl & DS3231_CTRL_RS_MASK) >> DS3231_CTRL_RS_SHIFT;
	if (tmp >= nitems(ds3231_sqw_freq))
		tmp = nitems(ds3231_sqw_freq) - 1;
	freq = ds3231_sqw_freq[tmp];
	error = sysctl_handle_int(oidp, &freq, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (freq != ds3231_sqw_freq[tmp]) {
		newf = 0;
		for (i = 0; i < nitems(ds3231_sqw_freq); i++)
			if (freq >= ds3231_sqw_freq[i])
				newf = i;
		sc->sc_ctrl &= ~DS3231_CTRL_RS_MASK;
		sc->sc_ctrl |= newf << DS3231_CTRL_RS_SHIFT;
		error = ds3231_ctrl_write(sc);
		if (error != 0)
			return (error);
	}

	return (error);
}

static int
ds3231_str_sqw_mode(char *buf)
{
	int len, rtrn;

	rtrn = -1;
	len = strlen(buf);
	if ((len > 2 && strncasecmp("interrupt", buf, len) == 0) ||
	    (len > 2 && strncasecmp("int", buf, len) == 0)) {
		rtrn = 1;
	} else if ((len > 2 && strncasecmp("square-wave", buf, len) == 0) ||
	    (len > 2 && strncasecmp("sqw", buf, len) == 0)) {
		rtrn = 0;
	}

	return (rtrn);
}

static int
ds3231_sqw_mode_sysctl(SYSCTL_HANDLER_ARGS)
{
	char buf[16];
	int error, mode, newm;
	struct ds3231_softc *sc;

	sc = (struct ds3231_softc *)arg1;
	error = ds3231_ctrl_read(sc);
	if (error != 0)
		return (error);
	if (sc->sc_ctrl & DS3231_CTRL_INTCN) {
		mode = 1;
		strlcpy(buf, "interrupt", sizeof(buf));
	} else {
		mode = 0;
		strlcpy(buf, "square-wave", sizeof(buf));
	}
	error = sysctl_handle_string(oidp, buf, sizeof(buf), req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	newm = ds3231_str_sqw_mode(buf);
	if (newm != -1 && mode != newm) {
		sc->sc_ctrl &= ~DS3231_CTRL_INTCN;
		if (newm == 1)
			sc->sc_ctrl |= DS3231_CTRL_INTCN;
		error = ds3231_ctrl_write(sc);
		if (error != 0)
			return (error);
	}

	return (error);
}

static int
ds3231_en32khz_sysctl(SYSCTL_HANDLER_ARGS)
{
	int error, en32khz, tmp;
	struct ds3231_softc *sc;

	sc = (struct ds3231_softc *)arg1;
	error = ds3231_status_read(sc);
	if (error != 0)
		return (error);
	tmp = en32khz = (sc->sc_status & DS3231_STATUS_EN32KHZ) ? 1 : 0;
	error = sysctl_handle_int(oidp, &en32khz, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (en32khz != tmp) {
		sc->sc_status &= ~DS3231_STATUS_EN32KHZ;
		if (en32khz)
			sc->sc_status |= DS3231_STATUS_EN32KHZ;
		error = ds3231_status_write(sc, 0, 0);
		if (error != 0)
			return (error);
	}

	return (error);
}

static int
ds3231_probe(device_t dev)
{

#ifdef FDT
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);
	if (!ofw_bus_is_compatible(dev, "maxim,ds3231"))
		return (ENXIO);
#endif
	device_set_desc(dev, "Maxim DS3231 RTC");

	return (BUS_PROBE_DEFAULT);
}

static int
ds3231_attach(device_t dev)
{
	struct ds3231_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->sc_addr = iicbus_get_addr(dev);
	sc->sc_last_c = -1;
	sc->sc_year0 = 0;
	sc->enum_hook.ich_func = ds3231_start;
	sc->enum_hook.ich_arg = dev;

	/*
	 * We have to wait until interrupts are enabled.  Usually I2C read
	 * and write only works when the interrupts are available.
	 */
	if (config_intrhook_establish(&sc->enum_hook) != 0)
		return (ENOMEM);

	return (0);
}

static int
ds3231_detach(device_t dev)
{

	clock_unregister(dev);
	return (0);
}

static void
ds3231_start(void *xdev)
{
	device_t dev;
	struct ds3231_softc *sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree_node;
	struct sysctl_oid_list *tree;

	dev = (device_t)xdev;
	sc = device_get_softc(dev);
	ctx = device_get_sysctl_ctx(dev);
	tree_node = device_get_sysctl_tree(dev);
	tree = SYSCTL_CHILDREN(tree_node);

	config_intrhook_disestablish(&sc->enum_hook);
	if (ds3231_ctrl_read(sc) != 0)
		return;
	if (ds3231_status_read(sc) != 0)
		return;
	/*
	 * Warn if the clock stopped, but don't restart it until the first
	 * clock_settime() call.
	 */
	if (sc->sc_status & DS3231_STATUS_OSF) {
		device_printf(sc->sc_dev,
		    "WARNING: RTC clock stopped, check the battery.\n");
	}

	/*
	 * Ack any pending alarm interrupts and clear the EOSC bit to ensure the
	 * clock runs even when on battery power.  Do not give up if these
	 * writes fail, because a factory-fresh chip is in a special mode that
	 * disables much of the chip to save battery power, and the only thing
	 * that gets it out of that mode is writing to the time registers.  In
	 * these pristine chips, the EOSC and alarm bits are zero already, so
	 * the first valid write of time will get everything running properly.
	 */
	ds3231_status_write(sc, 1, 1);
	ds3231_ctrl_write(sc);

	/* Temperature. */
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "temperature",
	    CTLTYPE_INT | CTLFLAG_RD | CTLFLAG_MPSAFE, sc, 0,
	    ds3231_temp_sysctl, "IK", "Current temperature");
	/* Configuration parameters. */
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "temp_conv",
	    CTLFLAG_RW | CTLTYPE_UINT | CTLFLAG_MPSAFE, sc, 0,
	    ds3231_conv_sysctl, "IU",
	    "DS3231 start a new temperature converstion");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "bbsqw",
	    CTLFLAG_RW | CTLTYPE_UINT | CTLFLAG_MPSAFE, sc, 0,
	    ds3231_bbsqw_sysctl, "IU",
	    "DS3231 battery-backed square-wave output enable");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "sqw_freq",
	    CTLFLAG_RW | CTLTYPE_UINT | CTLFLAG_MPSAFE, sc, 0,
	    ds3231_sqw_freq_sysctl, "IU",
	    "DS3231 square-wave output frequency");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "sqw_mode",
	    CTLFLAG_RW | CTLTYPE_STRING | CTLFLAG_MPSAFE, sc, 0,
	    ds3231_sqw_mode_sysctl, "A", "DS3231 SQW output mode control");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "32khz_enable",
	    CTLFLAG_RW | CTLTYPE_UINT | CTLFLAG_MPSAFE, sc, 0,
	    ds3231_en32khz_sysctl, "IU", "DS3231 enable the 32kHz output");

	/*
	 * Register as a clock with 1 second resolution.  Schedule the
	 * clock_settime() method to be called just after top-of-second;
	 * resetting the time resets top-of-second in the hardware.
	 */
	clock_register_flags(dev, 1000000, CLOCKF_SETTIME_NO_ADJ);
	clock_schedule(dev, 1);
}

static int
ds3231_gettime(device_t dev, struct timespec *ts)
{
	int c, error;
	struct bcd_clocktime bct;
	struct ds3231_softc *sc;
	uint8_t data[7], hourmask;

	sc = device_get_softc(dev);

	/* If the clock halted, we don't have good data. */
	if ((error = ds3231_status_read(sc)) != 0) {
		device_printf(dev, "cannot read from RTC.\n");
		return (error);
	}
	if (sc->sc_status & DS3231_STATUS_OSF)
		return (EINVAL);

	error = iicdev_readfrom(sc->sc_dev, DS3231_SECS, data, sizeof(data),
	    IIC_INTRWAIT);
	if (error != 0) {
		device_printf(dev, "cannot read from RTC.\n");
		return (error);
	}

	/* If chip is in AM/PM mode remember that. */
	if (data[DS3231_HOUR] & DS3231_HOUR_USE_AMPM) {
		sc->sc_use_ampm = true;
		hourmask = DS3231_HOUR_MASK_12HR;
	} else
		hourmask = DS3231_HOUR_MASK_24HR;

	bct.nsec = 0;
	bct.sec  = data[DS3231_SECS]  & DS3231_SECS_MASK;
	bct.min  = data[DS3231_MINS]  & DS3231_MINS_MASK;
	bct.hour = data[DS3231_HOUR]  & hourmask;
	bct.day  = data[DS3231_DATE]  & DS3231_DATE_MASK;
	bct.mon  = data[DS3231_MONTH] & DS3231_MONTH_MASK;
	bct.year = data[DS3231_YEAR]  & DS3231_YEAR_MASK;
	bct.ispm = data[DS3231_HOUR]  & DS3231_HOUR_IS_PM;

	/*
	 * If the century flag has toggled since we last saw it, there has been
	 * a century rollover.  If this is the first time we're seeing it,
	 * remember the state so we can preserve its polarity on writes.
	 */
	c = (data[DS3231_MONTH] & DS3231_C_MASK) ? 1 : 0;
	if (sc->sc_last_c == -1)
		sc->sc_last_c = c;
	else if (c != sc->sc_last_c) {
		sc->sc_year0 += 0x100;
		sc->sc_last_c = c;
	}
	bct.year |= sc->sc_year0;

	clock_dbgprint_bcd(sc->sc_dev, CLOCK_DBG_READ, &bct); 
	return (clock_bcd_to_ts(&bct, ts, sc->sc_use_ampm));
}

static int
ds3231_settime(device_t dev, struct timespec *ts)
{
	int error;
	struct bcd_clocktime bct;
	struct ds3231_softc *sc;
	uint8_t data[7];
	uint8_t pmflags;

	sc = device_get_softc(dev);

	/*
	 * We request a timespec with no resolution-adjustment.  That also
	 * disables utc adjustment, so apply that ourselves.
	 */
	ts->tv_sec -= utc_offset();
	clock_ts_to_bcd(ts, &bct, sc->sc_use_ampm);
	clock_dbgprint_bcd(sc->sc_dev, CLOCK_DBG_WRITE, &bct); 

	/* If the chip is in AM/PM mode, adjust hour and set flags as needed. */
	if (sc->sc_use_ampm) {
		pmflags = DS3231_HOUR_USE_AMPM;
		if (bct.ispm)
			pmflags |= DS3231_HOUR_IS_PM;
	} else
		pmflags = 0;

	data[DS3231_SECS]    = bct.sec;
	data[DS3231_MINS]    = bct.min;
	data[DS3231_HOUR]    = bct.hour | pmflags;
	data[DS3231_DATE]    = bct.day;
	data[DS3231_WEEKDAY] = bct.dow + 1;
	data[DS3231_MONTH]   = bct.mon;
	data[DS3231_YEAR]    = bct.year & 0xff;
	if (sc->sc_last_c)
		data[DS3231_MONTH] |= DS3231_C_MASK;

	/* Write the time back to RTC. */
	error = iicdev_writeto(dev, DS3231_SECS, data, sizeof(data),
	    IIC_INTRWAIT);
	if (error != 0) {
		device_printf(dev, "cannot write to RTC.\n");
		return (error);
	}

	/*
	 * Unlike most hardware, the osc-was-stopped bit does not clear itself
	 * after setting the time, it has to be manually written to zero.
	 */
	if (sc->sc_status & DS3231_STATUS_OSF) {
		if ((error = ds3231_status_read(sc)) != 0) {
			device_printf(dev, "cannot read from RTC.\n");
			return (error);
		}
		sc->sc_status &= ~DS3231_STATUS_OSF;
		if ((error = ds3231_status_write(sc, 0, 0)) != 0) {
			device_printf(dev, "cannot write to RTC.\n");
			return (error);
		}
	}

	return (error);
}

static device_method_t ds3231_methods[] = {
	DEVMETHOD(device_probe,		ds3231_probe),
	DEVMETHOD(device_attach,	ds3231_attach),
	DEVMETHOD(device_detach,	ds3231_detach),

	DEVMETHOD(clock_gettime,	ds3231_gettime),
	DEVMETHOD(clock_settime,	ds3231_settime),

	DEVMETHOD_END
};

static driver_t ds3231_driver = {
	"ds3231",
	ds3231_methods,
	sizeof(struct ds3231_softc),
};

static devclass_t ds3231_devclass;

DRIVER_MODULE(ds3231, iicbus, ds3231_driver, ds3231_devclass, NULL, NULL);
MODULE_VERSION(ds3231, 1);
MODULE_DEPEND(ds3231, iicbus, 1, 1, 1);
