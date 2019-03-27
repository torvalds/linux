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
 * Driver for imx Enhanced Programmable Interval Timer, a simple free-running
 * counter device that can be used as the system timecounter.  On imx5 a second
 * instance of the device is used as the system eventtimer.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/bus.h>
#include <sys/kernel.h>
#include <sys/module.h>
#include <sys/malloc.h>
#include <sys/rman.h>
#include <sys/timeet.h>
#include <sys/timetc.h>
#include <sys/watchdog.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/machdep.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/freescale/imx/imx_ccmvar.h>
#include <arm/freescale/imx/imx_machdep.h>

#define	EPIT_CR				0x00		/* Control register */
#define	  EPIT_CR_CLKSRC_SHIFT		  24
#define	  EPIT_CR_CLKSRC_OFF		   0
#define	  EPIT_CR_CLKSRC_IPG		   1
#define	  EPIT_CR_CLKSRC_HFCLK		   2
#define	  EPIT_CR_CLKSRC_LFCLK		   3
#define	  EPIT_CR_STOPEN		  (1u << 21)
#define	  EPIT_CR_WAITEN		  (1u << 19)
#define	  EPIT_CR_DBGEN			  (1u << 18)
#define	  EPIT_CR_IOVW			  (1u << 17)
#define	  EPIT_CR_SWR			  (1u << 16)
#define	  EPIT_CR_RLD			  (1u <<  3)
#define	  EPIT_CR_OCIEN			  (1u <<  2)
#define	  EPIT_CR_ENMOD			  (1u <<  1)
#define	  EPIT_CR_EN			  (1u <<  0)

#define	EPIT_SR				0x04		/* Status register */
#define	  EPIT_SR_OCIF			  (1u << 0)

#define	EPIT_LR				0x08		/* Load register */
#define	EPIT_CMPR			0x0c		/* Compare register */
#define	EPIT_CNR			0x10		/* Counter register */

/*
 * Define event timer limits.
 *
 * In theory our minimum period is 1 tick, because to setup a oneshot we don't
 * need a read-modify-write sequence to calculate and set a compare register
 * value while the counter is running.  In practice the waveform diagrams in the
 * manual make it appear that a setting of 1 might cause it to miss the event,
 * so I'm setting the lower limit to 2 ticks.
 */
#define	ET_MIN_TICKS	2
#define	ET_MAX_TICKS	0xfffffffe

static u_int epit_tc_get_timecount(struct timecounter *tc);

struct epit_softc {
	device_t 		dev;
	struct resource *	memres;
	struct resource *	intres;
	void *			inthandle;
	uint32_t 		clkfreq;
	uint32_t 		ctlreg;
	uint32_t		period;
	struct timecounter	tc;
	struct eventtimer	et;
	bool			oneshot;
};

/*
 * Probe data.  For some reason, the standard linux dts files don't have
 * compatible properties on the epit devices (other properties are missing too,
 * like clocks, but we don't care as much about that).  So our probe routine
 * uses the name of the node (must contain "epit") and the address of the
 * registers as identifying marks.
 */
static const uint32_t imx51_epit_ioaddr[2] = {0x73fac000, 0x73fb0000};
static const uint32_t imx53_epit_ioaddr[2] = {0x53fac000, 0x53fb0000};
static const uint32_t imx6_epit_ioaddr[2]  = {0x020d0000, 0x020d4000};

/* ocd_data is number of units to instantiate on the platform */
static struct ofw_compat_data compat_data[] = {
	{"fsl,imx6ul-epit", 1},
	{"fsl,imx6sx-epit", 1},
	{"fsl,imx6q-epit",  1},
	{"fsl,imx6dl-epit", 1},
	{"fsl,imx53-epit",  2},
	{"fsl,imx51-epit",  2},
	{"fsl,imx31-epit",  2},
	{"fsl,imx27-epit",  2},
	{"fsl,imx25-epit",  2},
	{NULL,              0}
};

static inline uint32_t
RD4(struct epit_softc *sc, bus_size_t offset)
{

	return (bus_read_4(sc->memres, offset));
}

static inline void
WR4(struct epit_softc *sc, bus_size_t offset, uint32_t value)
{

	bus_write_4(sc->memres, offset, value);
}

static inline void
WR4B(struct epit_softc *sc, bus_size_t offset, uint32_t value)
{

	bus_write_4(sc->memres, offset, value);
	bus_barrier(sc->memres, offset, 4, BUS_SPACE_BARRIER_WRITE);
}

static u_int
epit_read_counter(struct epit_softc *sc)
{

	/*
	 * Hardware is a downcounter, adjust to look like it counts up for use
	 * with timecounter and DELAY.
	 */
	return (0xffffffff - RD4(sc, EPIT_CNR));
}

static void
epit_do_delay(int usec, void *arg)
{
	struct epit_softc *sc = arg;
	uint64_t curcnt, endcnt, startcnt, ticks;

	/*
	 * Calculate the tick count with 64-bit values so that it works for any
	 * clock frequency.  Loop until the hardware count reaches start+ticks.
	 * If the 32-bit hardware count rolls over while we're looping, just
	 * manually do a carry into the high bits after each read; don't worry
	 * that doing this on each loop iteration is inefficient -- we're trying
	 * to waste time here.
	 */
	ticks = 1 + ((uint64_t)usec * sc->clkfreq) / 1000000;
	curcnt = startcnt = epit_read_counter(sc);
	endcnt = startcnt + ticks;
	while (curcnt < endcnt) {
		curcnt = epit_read_counter(sc);
		if (curcnt < startcnt)
			curcnt += 1ULL << 32;
	}
}

static u_int
epit_tc_get_timecount(struct timecounter *tc)
{

	return (epit_read_counter(tc->tc_priv));
}

static int
epit_tc_attach(struct epit_softc *sc)
{

	/* When the counter hits zero, reload with 0xffffffff.  Start it. */
	WR4(sc, EPIT_LR, 0xffffffff);
	WR4(sc, EPIT_CR, sc->ctlreg | EPIT_CR_EN);

	/* Register as a timecounter. */
	sc->tc.tc_name          = "EPIT";
	sc->tc.tc_quality       = 1000;
	sc->tc.tc_frequency     = sc->clkfreq;
	sc->tc.tc_counter_mask  = 0xffffffff;
	sc->tc.tc_get_timecount = epit_tc_get_timecount;
	sc->tc.tc_priv          = sc;
	tc_init(&sc->tc);

	/* We are the DELAY() implementation. */
	arm_set_delay(epit_do_delay, sc);

	return (0);
}

static int
epit_et_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{
	struct epit_softc *sc;
	uint32_t ticks;

	sc = (struct epit_softc *)et->et_priv;

	/*
	 * Disable the timer and clear any pending status.  The timer may be
	 * running or may have just expired if we're called to reschedule the
	 * next event before the previous event time arrives.
	 */
	WR4(sc, EPIT_CR, sc->ctlreg);
	WR4(sc, EPIT_SR, EPIT_SR_OCIF);
	if (period != 0) {
		sc->oneshot = false;
		ticks = ((uint32_t)et->et_frequency * period) >> 32;
	} else if (first != 0) {
		sc->oneshot = true;
		ticks = ((uint32_t)et->et_frequency * first) >> 32;
	} else {
		return (EINVAL);
	}

	/* Set the countdown load register and start the timer. */
	WR4(sc, EPIT_LR, ticks);
	WR4B(sc, EPIT_CR, sc->ctlreg | EPIT_CR_EN);

	return (0);
}

static int
epit_et_stop(struct eventtimer *et)
{
	struct epit_softc *sc;

	sc = (struct epit_softc *)et->et_priv;

	/* Disable the timer and clear any pending status. */
	WR4(sc, EPIT_CR, sc->ctlreg);
	WR4B(sc, EPIT_SR, EPIT_SR_OCIF);

	return (0);
}

static int
epit_intr(void *arg)
{
	struct epit_softc *sc;
	uint32_t status;

	sc = arg;

	/*
	 * Disable a one-shot timer until a new event is scheduled so that the
	 * counter doesn't wrap and fire again.  Do this before clearing the
	 * status since a short period would make it fire again really soon.
	 *
	 * Clear interrupt status before invoking event callbacks.  The callback
	 * often sets up a new one-shot timer event and if the interval is short
	 * enough it can fire before we get out of this function.  If we cleared
	 * at the bottom we'd miss the interrupt and hang until the clock wraps.
	 */
	if (sc->oneshot)
		WR4(sc, EPIT_CR, sc->ctlreg);

	status = RD4(sc, EPIT_SR);
	WR4B(sc, EPIT_SR, status);

	if ((status & EPIT_SR_OCIF) == 0)
		return (FILTER_STRAY);

	if (sc->et.et_active)
		sc->et.et_event_cb(&sc->et, sc->et.et_arg);

	return (FILTER_HANDLED);
}

static int
epit_et_attach(struct epit_softc *sc)
{
	int err, rid;

	rid = 0;
	sc->intres = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ, &rid,
	    RF_ACTIVE);
	if (sc->intres == NULL) {
		device_printf(sc->dev, "could not allocate interrupt\n");
		return (ENXIO);
	}

	err = bus_setup_intr(sc->dev, sc->intres, INTR_TYPE_CLK | INTR_MPSAFE,
	    epit_intr, NULL, sc, &sc->inthandle);
	if (err != 0) {
		device_printf(sc->dev, "unable to setup the irq handler\n");
		return (err);
	}

	/* To be an eventtimer, we need interrupts enabled. */
	sc->ctlreg |= EPIT_CR_OCIEN;

	/* Register as an eventtimer. */
	sc->et.et_name = "EPIT";
	sc->et.et_flags = ET_FLAGS_ONESHOT | ET_FLAGS_PERIODIC;
	sc->et.et_quality = 1000;
	sc->et.et_frequency = sc->clkfreq;
	sc->et.et_min_period = ((uint64_t)ET_MIN_TICKS  << 32) / sc->clkfreq;
	sc->et.et_max_period = ((uint64_t)ET_MAX_TICKS  << 32) / sc->clkfreq;
	sc->et.et_start = epit_et_start;
	sc->et.et_stop = epit_et_stop;
	sc->et.et_priv = sc;
	et_register(&sc->et);

	return (0);
}

static int
epit_probe(device_t dev)
{
	struct resource *memres;
	rman_res_t ioaddr;
	int num_units, rid, unit;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	/*
	 * The FDT data for imx5 and imx6 EPIT hardware is missing or broken,
	 * but it may get fixed some day, so first just do a normal check.  We
	 * return success if the compatible string matches and we haven't
	 * already instantiated the number of units needed on this platform.
	 */
	unit = device_get_unit(dev);
	num_units = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	if (unit < num_units) {
		device_set_desc(dev, "i.MX EPIT timer");
		return (BUS_PROBE_DEFAULT);
	}

	/*
	 * No compat string match, but for imx6 all the data we need is in the
	 * node except the compat string, so do our own compatibility check
	 * using the device name of the node and the register block address.
	 */
	if (strstr(ofw_bus_get_name(dev), "epit") == NULL)
		return (ENXIO);

	rid = 0;
	memres = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_UNMAPPED);
	if (memres == NULL)
		return (ENXIO);
	ioaddr = rman_get_start(memres);
	bus_free_resource(dev, SYS_RES_MEMORY, memres);

	if (imx_soc_family() == 6) {
		if (unit > 0)
			return (ENXIO);
		if (ioaddr != imx6_epit_ioaddr[unit])
			return (ENXIO);
	} else {
		if (unit > 1)
			return (ENXIO);
		switch (imx_soc_type()) {
		case IMXSOC_51:
			if (ioaddr != imx51_epit_ioaddr[unit])
				return (ENXIO);
			break;
		case IMXSOC_53:
			if (ioaddr != imx53_epit_ioaddr[unit])
				return (ENXIO);
			break;
		default:
			return (ENXIO);
		}
		/*
		 * XXX Right now we have no way to handle the fact that the
		 * entire EPIT node is missing, which means no interrupt data.
		 */
		return (ENXIO);
	}

	device_set_desc(dev, "i.MX EPIT timer");
	return (BUS_PROBE_DEFAULT);
}

static int
epit_attach(device_t dev)
{
	struct epit_softc *sc;
	int err, rid;
	uint32_t clksrc;

	sc = device_get_softc(dev);
	sc->dev = dev;

	rid = 0;
	sc->memres = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->memres == NULL) {
		device_printf(sc->dev, "could not allocate registers\n");
		return (ENXIO);
	}

	/*
	 * For now, use ipg (66 MHz).  Some day we should get this from fdt.
	 */
	clksrc = EPIT_CR_CLKSRC_IPG;

	switch (clksrc) {
	default:
		device_printf(dev, 
		    "Unsupported clock source '%d', using IPG\n", clksrc);
                /* FALLTHROUGH */
	case EPIT_CR_CLKSRC_IPG:
		sc->clkfreq = imx_ccm_ipg_hz();
		break;
	case EPIT_CR_CLKSRC_HFCLK:
		sc->clkfreq = imx_ccm_perclk_hz();
		break;
	case EPIT_CR_CLKSRC_LFCLK:
		sc->clkfreq = 32768;
		break;
	}

	/*
	 * Init: stop operations and clear all options, then set up options and
	 * clock source, then do a soft-reset and wait for it to complete.
	 */
	WR4(sc, EPIT_CR, 0);

	sc->ctlreg =
	    (clksrc << EPIT_CR_CLKSRC_SHIFT) |  /* Use selected clock */
	    EPIT_CR_ENMOD  |                    /* Reload counter on enable */
	    EPIT_CR_RLD    |                    /* Reload counter from LR */
	    EPIT_CR_STOPEN |                    /* Run in STOP mode */
	    EPIT_CR_WAITEN |                    /* Run in WAIT mode */
	    EPIT_CR_DBGEN;                      /* Run in DEBUG mode */

	WR4B(sc, EPIT_CR, sc->ctlreg | EPIT_CR_SWR);
	while (RD4(sc, EPIT_CR) & EPIT_CR_SWR)
		continue;

	/*
	 * Unit 0 is the timecounter, 1 (if instantiated) is the eventtimer.
	 */
	if (device_get_unit(sc->dev) == 0)
		err = epit_tc_attach(sc);
	else
		err = epit_et_attach(sc);

	return (err);
}

static device_method_t epit_methods[] = {
	DEVMETHOD(device_probe,		epit_probe),
	DEVMETHOD(device_attach,	epit_attach),

	DEVMETHOD_END
};

static driver_t epit_driver = {
	"imx_epit",
	epit_methods,
	sizeof(struct epit_softc),
};

static devclass_t epit_devclass;

EARLY_DRIVER_MODULE(imx_epit, simplebus, epit_driver, epit_devclass, 0,
    0, BUS_PASS_TIMER);
