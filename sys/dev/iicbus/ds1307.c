/*-
 * Copyright (c) 2015 Luiz Otavio O Souza <loos@FreeBSD.org>
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
 * Driver for Maxim DS1307 I2C real-time clock/calendar.
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

#include <dev/iicbus/ds1307reg.h>

#include "clock_if.h"
#include "iicbus_if.h"

struct ds1307_softc {
	device_t	sc_dev;
	struct intr_config_hook
			enum_hook;
	uint8_t		sc_ctrl;
	bool		sc_mcp7941x;
	bool		sc_use_ampm;
};

static void ds1307_start(void *);

#ifdef FDT
static const struct ofw_compat_data ds1307_compat_data[] = {
    {"dallas,ds1307",		(uintptr_t)"Dallas DS1307 RTC"},
    {"maxim,ds1307",		(uintptr_t)"Maxim DS1307 RTC"},
    {"microchip,mcp7941x",	(uintptr_t)"Microchip MCP7941x RTC"},
    { NULL, 0 }
};
#endif

static int
ds1307_read1(device_t dev, uint8_t reg, uint8_t *data)
{

	return (iicdev_readfrom(dev, reg, data, 1, IIC_INTRWAIT));
}

static int
ds1307_write1(device_t dev, uint8_t reg, uint8_t data)
{

	return (iicdev_writeto(dev, reg, &data, 1, IIC_INTRWAIT));
}

static int
ds1307_ctrl_read(struct ds1307_softc *sc)
{
	int error;

	sc->sc_ctrl = 0;
	error = ds1307_read1(sc->sc_dev, DS1307_CONTROL, &sc->sc_ctrl);
	if (error) {
		device_printf(sc->sc_dev, "cannot read from RTC.\n");
		return (error);
	}

	return (0);
}

static int
ds1307_ctrl_write(struct ds1307_softc *sc)
{
	int error;
	uint8_t ctrl;

	ctrl = sc->sc_ctrl & DS1307_CTRL_MASK;
	error = ds1307_write1(sc->sc_dev, DS1307_CONTROL, ctrl);
	if (error != 0)
		device_printf(sc->sc_dev, "cannot write to RTC.\n");

	return (error);
}

static int
ds1307_sqwe_sysctl(SYSCTL_HANDLER_ARGS)
{
	int sqwe, error, newv, sqwe_bit;
	struct ds1307_softc *sc;

	sc = (struct ds1307_softc *)arg1;
	error = ds1307_ctrl_read(sc);
	if (error != 0)
		return (error);
	if (sc->sc_mcp7941x)
		sqwe_bit = MCP7941X_CTRL_SQWE;
	else
		sqwe_bit = DS1307_CTRL_SQWE;
	sqwe = newv = (sc->sc_ctrl & sqwe_bit) ? 1 : 0;
	error = sysctl_handle_int(oidp, &newv, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (sqwe != newv) {
		sc->sc_ctrl &= ~sqwe_bit;
		if (newv)
			sc->sc_ctrl |= sqwe_bit;
		error = ds1307_ctrl_write(sc);
		if (error != 0)
			return (error);
	}

	return (error);
}

static int
ds1307_sqw_freq_sysctl(SYSCTL_HANDLER_ARGS)
{
	int ds1307_sqw_freq[] = { 1, 4096, 8192, 32768 };
	int error, freq, i, newf, tmp;
	struct ds1307_softc *sc;

	sc = (struct ds1307_softc *)arg1;
	error = ds1307_ctrl_read(sc);
	if (error != 0)
		return (error);
	tmp = (sc->sc_ctrl & DS1307_CTRL_RS_MASK);
	if (tmp >= nitems(ds1307_sqw_freq))
		tmp = nitems(ds1307_sqw_freq) - 1;
	freq = ds1307_sqw_freq[tmp];
	error = sysctl_handle_int(oidp, &freq, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (freq != ds1307_sqw_freq[tmp]) {
		newf = 0;
		for (i = 0; i < nitems(ds1307_sqw_freq); i++)
			if (freq >= ds1307_sqw_freq[i])
				newf = i;
		sc->sc_ctrl &= ~DS1307_CTRL_RS_MASK;
		sc->sc_ctrl |= newf;
		error = ds1307_ctrl_write(sc);
		if (error != 0)
			return (error);
	}

	return (error);
}

static int
ds1307_sqw_out_sysctl(SYSCTL_HANDLER_ARGS)
{
	int sqwe, error, newv;
	struct ds1307_softc *sc;

	sc = (struct ds1307_softc *)arg1;
	error = ds1307_ctrl_read(sc);
	if (error != 0)
		return (error);
	sqwe = newv = (sc->sc_ctrl & DS1307_CTRL_OUT) ? 1 : 0;
	error = sysctl_handle_int(oidp, &newv, 0, req);
	if (error != 0 || req->newptr == NULL)
		return (error);
	if (sqwe != newv) {
		sc->sc_ctrl &= ~DS1307_CTRL_OUT;
		if (newv)
			sc->sc_ctrl |= DS1307_CTRL_OUT;
		error = ds1307_ctrl_write(sc);
		if (error != 0)
			return (error);
	}

	return (error);
}

static int
ds1307_probe(device_t dev)
{
#ifdef FDT
	const struct ofw_compat_data *compat;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	compat = ofw_bus_search_compatible(dev, ds1307_compat_data);

	if (compat->ocd_str == NULL)
		return (ENXIO);

	device_set_desc(dev, (const char *)compat->ocd_data);

	return (BUS_PROBE_DEFAULT);
#else
	device_set_desc(dev, "Maxim DS1307 RTC");

	return (BUS_PROBE_NOWILDCARD);
#endif
}

static int
ds1307_attach(device_t dev)
{
	struct ds1307_softc *sc;

	sc = device_get_softc(dev);
	sc->sc_dev = dev;
	sc->enum_hook.ich_func = ds1307_start;
	sc->enum_hook.ich_arg = dev;

#ifdef FDT
	if (ofw_bus_is_compatible(dev, "microchip,mcp7941x"))
		sc->sc_mcp7941x = 1;
#endif

	/*
	 * We have to wait until interrupts are enabled.  Usually I2C read
	 * and write only works when the interrupts are available.
	 */
	if (config_intrhook_establish(&sc->enum_hook) != 0)
		return (ENOMEM);

	return (0);
}

static int
ds1307_detach(device_t dev)
{

	clock_unregister(dev);
	return (0);
}

static void
ds1307_start(void *xdev)
{
	device_t dev;
	struct ds1307_softc *sc;
	struct sysctl_ctx_list *ctx;
	struct sysctl_oid *tree_node;
	struct sysctl_oid_list *tree;
	uint8_t secs;
	uint8_t osc_en;

	dev = (device_t)xdev;
	sc = device_get_softc(dev);
	ctx = device_get_sysctl_ctx(dev);
	tree_node = device_get_sysctl_tree(dev);
	tree = SYSCTL_CHILDREN(tree_node);

	config_intrhook_disestablish(&sc->enum_hook);

	/* Check if the oscillator is disabled. */
	if (ds1307_read1(sc->sc_dev, DS1307_SECS, &secs) != 0) {
		device_printf(sc->sc_dev, "cannot read from RTC.\n");
		return;
	}
	if (sc->sc_mcp7941x)
		osc_en = 0x80;
	else
		osc_en = 0x00;

	if (((secs & DS1307_SECS_CH) ^ osc_en) != 0) {
		device_printf(sc->sc_dev,
		    "WARNING: RTC clock stopped, check the battery.\n");
	}

	/* Configuration parameters. */
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "sqwe",
	    CTLFLAG_RW | CTLTYPE_UINT | CTLFLAG_MPSAFE, sc, 0,
	    ds1307_sqwe_sysctl, "IU", "DS1307 square-wave enable");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "sqw_freq",
	    CTLFLAG_RW | CTLTYPE_UINT | CTLFLAG_MPSAFE, sc, 0,
	    ds1307_sqw_freq_sysctl, "IU",
	    "DS1307 square-wave output frequency");
	SYSCTL_ADD_PROC(ctx, tree, OID_AUTO, "sqw_out",
	    CTLFLAG_RW | CTLTYPE_UINT | CTLFLAG_MPSAFE, sc, 0,
	    ds1307_sqw_out_sysctl, "IU", "DS1307 square-wave output state");

	/*
	 * Register as a clock with 1 second resolution.  Schedule the
	 * clock_settime() method to be called just after top-of-second;
	 * resetting the time resets top-of-second in the hardware.
	 */
	clock_register_flags(dev, 1000000, CLOCKF_SETTIME_NO_ADJ);
	clock_schedule(dev, 1);
}

static int
ds1307_gettime(device_t dev, struct timespec *ts)
{
	int error;
	struct bcd_clocktime bct;
	struct ds1307_softc *sc;
	uint8_t data[7], hourmask, st_mask;

	sc = device_get_softc(dev);
	error = iicdev_readfrom(sc->sc_dev, DS1307_SECS, data, sizeof(data),
	    IIC_INTRWAIT);
	if (error != 0) {
		device_printf(dev, "cannot read from RTC.\n");
		return (error);
	}

	/* If the clock halted, we don't have good data. */
	if (sc->sc_mcp7941x)
		st_mask = 0x80;
	else
		st_mask = 0x00;

	if (((data[DS1307_SECS] & DS1307_SECS_CH) ^ st_mask) != 0)
		return (EINVAL);

	/* If chip is in AM/PM mode remember that. */
	if (data[DS1307_HOUR] & DS1307_HOUR_USE_AMPM) {
		sc->sc_use_ampm = true;
		hourmask = DS1307_HOUR_MASK_12HR;
	} else
		hourmask = DS1307_HOUR_MASK_24HR;

	bct.nsec = 0;
	bct.ispm = (data[DS1307_HOUR] & DS1307_HOUR_IS_PM) != 0;
	bct.sec  = data[DS1307_SECS]  & DS1307_SECS_MASK;
	bct.min  = data[DS1307_MINS]  & DS1307_MINS_MASK;
	bct.hour = data[DS1307_HOUR]  & hourmask;
	bct.day  = data[DS1307_DATE]  & DS1307_DATE_MASK;
	bct.mon  = data[DS1307_MONTH] & DS1307_MONTH_MASK;
	bct.year = data[DS1307_YEAR]  & DS1307_YEAR_MASK;

	clock_dbgprint_bcd(sc->sc_dev, CLOCK_DBG_READ, &bct); 
	return (clock_bcd_to_ts(&bct, ts, sc->sc_use_ampm));
}

static int
ds1307_settime(device_t dev, struct timespec *ts)
{
	struct bcd_clocktime bct;
	struct ds1307_softc *sc;
	int error, year;
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
		pmflags = DS1307_HOUR_USE_AMPM;
		if (bct.ispm)
			pmflags |= DS1307_HOUR_IS_PM;
	} else
		pmflags = 0;

	data[DS1307_SECS]    = bct.sec;
	data[DS1307_MINS]    = bct.min;
	data[DS1307_HOUR]    = bct.hour | pmflags;
	data[DS1307_DATE]    = bct.day;
	data[DS1307_WEEKDAY] = bct.dow;
	data[DS1307_MONTH]   = bct.mon;
	data[DS1307_YEAR]    = bct.year & 0xff;
	if (sc->sc_mcp7941x) {
		data[DS1307_SECS] |= MCP7941X_SECS_ST;
		data[DS1307_WEEKDAY] |= MCP7941X_WEEKDAY_VBATEN;
		year = bcd2bin(bct.year >> 8) * 100 + bcd2bin(bct.year & 0xff);
		if ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)
			data[DS1307_MONTH] |= MCP7941X_MONTH_LPYR;
	}
	/* Write the time back to RTC. */
	error = iicdev_writeto(sc->sc_dev, DS1307_SECS, data, sizeof(data),
	    IIC_INTRWAIT);
	if (error != 0)
		device_printf(dev, "cannot write to RTC.\n");

	return (error);
}

static device_method_t ds1307_methods[] = {
	DEVMETHOD(device_probe,		ds1307_probe),
	DEVMETHOD(device_attach,	ds1307_attach),
	DEVMETHOD(device_detach,	ds1307_detach),

	DEVMETHOD(clock_gettime,	ds1307_gettime),
	DEVMETHOD(clock_settime,	ds1307_settime),

	DEVMETHOD_END
};

static driver_t ds1307_driver = {
	"ds1307",
	ds1307_methods,
	sizeof(struct ds1307_softc),
};

static devclass_t ds1307_devclass;

DRIVER_MODULE(ds1307, iicbus, ds1307_driver, ds1307_devclass, NULL, NULL);
MODULE_VERSION(ds1307, 1);
MODULE_DEPEND(ds1307, iicbus, 1, 1, 1);
