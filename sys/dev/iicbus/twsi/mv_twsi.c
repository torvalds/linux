/*-
 * Copyright (C) 2008 MARVELL INTERNATIONAL LTD.
 * All rights reserved.
 *
 * Developed by Semihalf.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of MARVELL nor the names of contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Driver for the TWSI (aka I2C, aka IIC) bus controller found on Marvell
 * and Allwinner SoCs. Supports master operation only, and works in polling mode.
 *
 * Calls to DELAY() are needed per Application Note AN-179 "TWSI Software
 * Guidelines for Discovery(TM), Horizon (TM) and Feroceon(TM) Devices".
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/resource.h>

#include <machine/_inttypes.h>
#include <machine/bus.h>
#include <machine/resource.h>

#include <sys/rman.h>

#include <sys/lock.h>
#include <sys/mutex.h>

#include <dev/iicbus/iiconf.h>
#include <dev/iicbus/iicbus.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#ifdef EXT_RESOURCES
#include <dev/extres/clk/clk.h>
#endif

#include <arm/mv/mvreg.h>
#include <arm/mv/mvvar.h>
#include <dev/iicbus/twsi/twsi.h>

#include "iicbus_if.h"

#define MV_TWSI_NAME		"twsi"
#define	IICBUS_DEVNAME		"iicbus"

#define TWSI_ADDR	0x00
#define TWSI_DATA	0x04
#define TWSI_CNTR	0x08
#define TWSI_XADDR	0x10
#define TWSI_STAT	0x0c
#define TWSI_BAUD_RATE	0x0c
#define TWSI_SRST	0x1c

#define	TWSI_BAUD_RATE_RAW(C,M,N)	((C)/((10*(M+1))<<(N+1)))
#define	TWSI_BAUD_RATE_SLOW		50000	/* 50kHz */
#define	TWSI_BAUD_RATE_FAST		100000	/* 100kHz */

#define TWSI_DEBUG
#undef TWSI_DEBUG

#ifdef  TWSI_DEBUG
#define debugf(fmt, args...) do { printf("%s(): ", __func__); printf(fmt,##args); } while (0)
#else
#define debugf(fmt, args...)
#endif

static phandle_t mv_twsi_get_node(device_t, device_t);
static int mv_twsi_probe(device_t);
static int mv_twsi_attach(device_t);

static struct ofw_compat_data compat_data[] = {
	{ "mrvl,twsi",			true },
	{ "marvell,mv64xxx-i2c",	true },
	{ "marvell,mv78230-i2c",	true },
	{ NULL,				false }
};

static device_method_t mv_twsi_methods[] = {
	/* device interface */
	DEVMETHOD(device_probe,		mv_twsi_probe),
	DEVMETHOD(device_attach,	mv_twsi_attach),

	/* ofw_bus interface */
	DEVMETHOD(ofw_bus_get_node,	mv_twsi_get_node),

	DEVMETHOD_END
};

DEFINE_CLASS_1(twsi, mv_twsi_driver, mv_twsi_methods,
    sizeof(struct twsi_softc), twsi_driver);

static devclass_t mv_twsi_devclass;

DRIVER_MODULE(twsi, simplebus, mv_twsi_driver, mv_twsi_devclass, 0, 0);
DRIVER_MODULE(iicbus, twsi, iicbus_driver, iicbus_devclass, 0, 0);
MODULE_DEPEND(twsi, iicbus, 1, 1, 1);

static phandle_t
mv_twsi_get_node(device_t bus, device_t dev)
{

	/* Used by ofw_iicbus. */
	return (ofw_bus_get_node(bus));
}

static int
mv_twsi_probe(device_t dev)
{
	struct twsi_softc *sc;

	sc = device_get_softc(dev);
	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_search_compatible(dev, compat_data)->ocd_data)
		return (ENXIO);

	device_set_desc(dev, "Marvell Integrated I2C Bus Controller");
	return (BUS_PROBE_DEFAULT);
}

#define	ABSSUB(a,b)	(((a) > (b)) ? (a) - (b) : (b) - (a))
static void
mv_twsi_cal_baud_rate(struct twsi_softc *sc, const uint32_t target,
    struct twsi_baud_rate *rate)
{
	uint64_t clk;
	uint32_t cur, diff, diff0;
	int m, n, m0, n0;

	/* Calculate baud rate. */
	m0 = n0 = 4;	/* Default values on reset */
	diff0 = 0xffffffff;
#ifdef __aarch64__
	clk_get_freq(sc->clk_core, &clk);
#else
	clk = get_tclk();
#endif

	for (n = 0; n < 8; n++) {
		for (m = 0; m < 16; m++) {
			cur = TWSI_BAUD_RATE_RAW(clk,m,n);
			diff = ABSSUB(target, cur);
			if (diff < diff0) {
				m0 = m;
				n0 = n;
				diff0 = diff;
			}
		}
	}
	rate->raw = TWSI_BAUD_RATE_RAW(clk, m0, n0);
	rate->param = TWSI_BAUD_RATE_PARAM(m0, n0);
	rate->m = m0;
	rate->n = n0;
}

static int
mv_twsi_attach(device_t dev)
{
	struct twsi_softc *sc;
#ifdef __aarch64__
	int error;
#endif

	sc = device_get_softc(dev);
	sc->dev = dev;

#ifdef __aarch64__
	/* Activate clock */
	error = clk_get_by_ofw_index(dev, 0, 0, &sc->clk_core);
	if (error != 0) {
		device_printf(dev, "could not find core clock\n");
		return (error);
	}
	error = clk_enable(sc->clk_core);
	if (error != 0) {
		device_printf(dev, "could not enable core clock\n");
		return (error);
	}

	if (clk_get_by_ofw_index(dev, 0, 1, &sc->clk_reg) == 0) {
		error = clk_enable(sc->clk_reg);
		if (error != 0) {
			device_printf(dev, "could not enable core clock\n");
			return (error);
		}
	}
#endif

	mv_twsi_cal_baud_rate(sc, TWSI_BAUD_RATE_SLOW, &sc->baud_rate[IIC_SLOW]);
	mv_twsi_cal_baud_rate(sc, TWSI_BAUD_RATE_FAST, &sc->baud_rate[IIC_FAST]);
	if (bootverbose)
		device_printf(dev, "calculated baud rates are:\n"
		    " %" PRIu32 " kHz (M=%d, N=%d) for slow,\n"
		    " %" PRIu32 " kHz (M=%d, N=%d) for fast.\n",
		    sc->baud_rate[IIC_SLOW].raw / 1000,
		    sc->baud_rate[IIC_SLOW].m,
		    sc->baud_rate[IIC_SLOW].n,
		    sc->baud_rate[IIC_FAST].raw / 1000,
		    sc->baud_rate[IIC_FAST].m,
		    sc->baud_rate[IIC_FAST].n);

	sc->reg_data = TWSI_DATA;
	sc->reg_slave_addr = TWSI_ADDR;
	sc->reg_slave_ext_addr = TWSI_XADDR;
	sc->reg_control = TWSI_CNTR;
	sc->reg_status = TWSI_STAT;
	sc->reg_baud_rate = TWSI_BAUD_RATE;
	sc->reg_soft_reset = TWSI_SRST;

	return (twsi_attach(dev));
}
