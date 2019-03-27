/*-
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Copyright (c) 2011 The FreeBSD Foundation
 * All rights reserved.
 *
 * Developed by Ben Gray <ben.r.gray@gmail.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the company nor the name of the author may be used to
 *    endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/**
 * The ARM Cortex-A9 core can support a global timer plus a private and
 * watchdog timer per core.  This driver reserves memory and interrupt
 * resources for accessing both timer register sets, these resources are
 * stored globally and used to setup the timecount and eventtimer.
 *
 * The timecount timer uses the global 64-bit counter, whereas the
 * per-CPU eventtimer uses the private 32-bit counters.
 *
 *
 * REF: ARM Cortex-A9 MPCore, Technical Reference Manual (rev. r2p2)
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

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

#include <machine/machdep.h> /* For arm_set_delay */

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#include <arm/arm/mpcore_timervar.h>

/* Private (per-CPU) timer register map */
#define PRV_TIMER_LOAD                 0x0000
#define PRV_TIMER_COUNT                0x0004
#define PRV_TIMER_CTRL                 0x0008
#define PRV_TIMER_INTR                 0x000C

#define PRV_TIMER_CTR_PRESCALER_SHIFT  8
#define PRV_TIMER_CTRL_IRQ_ENABLE      (1UL << 2)
#define PRV_TIMER_CTRL_AUTO_RELOAD     (1UL << 1)
#define PRV_TIMER_CTRL_TIMER_ENABLE    (1UL << 0)

#define PRV_TIMER_INTR_EVENT           (1UL << 0)

/* Global timer register map */
#define GBL_TIMER_COUNT_LOW            0x0000
#define GBL_TIMER_COUNT_HIGH           0x0004
#define GBL_TIMER_CTRL                 0x0008
#define GBL_TIMER_INTR                 0x000C

#define GBL_TIMER_CTR_PRESCALER_SHIFT  8
#define GBL_TIMER_CTRL_AUTO_INC        (1UL << 3)
#define GBL_TIMER_CTRL_IRQ_ENABLE      (1UL << 2)
#define GBL_TIMER_CTRL_COMP_ENABLE     (1UL << 1)
#define GBL_TIMER_CTRL_TIMER_ENABLE    (1UL << 0)

#define GBL_TIMER_INTR_EVENT           (1UL << 0)

struct arm_tmr_softc {
	device_t		dev;
	int			irqrid;
	int			memrid;
	struct resource *	gbl_mem;
	struct resource *	prv_mem;
	struct resource *	prv_irq;
	uint64_t		clkfreq;
	struct eventtimer	et;
};

static struct eventtimer *arm_tmr_et;
static struct timecounter *arm_tmr_tc;
static uint64_t arm_tmr_freq;
static boolean_t arm_tmr_freq_varies;

#define	tmr_prv_read_4(sc, reg)         bus_read_4((sc)->prv_mem, reg)
#define	tmr_prv_write_4(sc, reg, val)   bus_write_4((sc)->prv_mem, reg, val)
#define	tmr_gbl_read_4(sc, reg)         bus_read_4((sc)->gbl_mem, reg)
#define	tmr_gbl_write_4(sc, reg, val)   bus_write_4((sc)->gbl_mem, reg, val)

static void arm_tmr_delay(int, void *);

static timecounter_get_t arm_tmr_get_timecount;

static struct timecounter arm_tmr_timecount = {
	.tc_name           = "MPCore",
	.tc_get_timecount  = arm_tmr_get_timecount,
	.tc_poll_pps       = NULL,
	.tc_counter_mask   = ~0u,
	.tc_frequency      = 0,
	.tc_quality        = 800,
};

#define	TMR_GBL		0x01
#define	TMR_PRV		0x02
#define	TMR_BOTH	(TMR_GBL | TMR_PRV)
#define	TMR_NONE	0

static struct ofw_compat_data compat_data[] = {
	{"arm,mpcore-timers",		TMR_BOTH}, /* Non-standard, FreeBSD. */
	{"arm,cortex-a9-global-timer",	TMR_GBL},
	{"arm,cortex-a5-global-timer",	TMR_GBL},
	{"arm,cortex-a9-twd-timer",	TMR_PRV},
	{"arm,cortex-a5-twd-timer",	TMR_PRV},
	{"arm,arm11mp-twd-timer",	TMR_PRV},
	{NULL,				TMR_NONE}
};

/**
 *	arm_tmr_get_timecount - reads the timecount (global) timer
 *	@tc: pointer to arm_tmr_timecount struct
 *
 *	We only read the lower 32-bits, the timecount stuff only uses 32-bits
 *	so (for now?) ignore the upper 32-bits.
 *
 *	RETURNS
 *	The lower 32-bits of the counter.
 */
static unsigned
arm_tmr_get_timecount(struct timecounter *tc)
{
	struct arm_tmr_softc *sc;

	sc = tc->tc_priv;
	return (tmr_gbl_read_4(sc, GBL_TIMER_COUNT_LOW));
}

/**
 *	arm_tmr_start - starts the eventtimer (private) timer
 *	@et: pointer to eventtimer struct
 *	@first: the number of seconds and fractional sections to trigger in
 *	@period: the period (in seconds and fractional sections) to set
 *
 *	If the eventtimer is required to be in oneshot mode, period will be
 *	NULL and first will point to the time to trigger.  If in periodic mode
 *	period will contain the time period and first may optionally contain
 *	the time for the first period.
 *
 *	RETURNS
 *	Always returns 0
 */
static int
arm_tmr_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{
	struct arm_tmr_softc *sc;
	uint32_t load, count;
	uint32_t ctrl;

	sc = et->et_priv;
	tmr_prv_write_4(sc, PRV_TIMER_CTRL, 0);
	tmr_prv_write_4(sc, PRV_TIMER_INTR, PRV_TIMER_INTR_EVENT);

	ctrl = PRV_TIMER_CTRL_IRQ_ENABLE | PRV_TIMER_CTRL_TIMER_ENABLE;

	if (period != 0) {
		load = ((uint32_t)et->et_frequency * period) >> 32;
		ctrl |= PRV_TIMER_CTRL_AUTO_RELOAD;
	} else
		load = 0;

	if (first != 0)
		count = (uint32_t)((et->et_frequency * first) >> 32);
	else
		count = load;

	tmr_prv_write_4(sc, PRV_TIMER_LOAD, load);
	tmr_prv_write_4(sc, PRV_TIMER_COUNT, count);
	tmr_prv_write_4(sc, PRV_TIMER_CTRL, ctrl);

	return (0);
}

/**
 *	arm_tmr_stop - stops the eventtimer (private) timer
 *	@et: pointer to eventtimer struct
 *
 *	Simply stops the private timer by clearing all bits in the ctrl register.
 *
 *	RETURNS
 *	Always returns 0
 */
static int
arm_tmr_stop(struct eventtimer *et)
{
	struct arm_tmr_softc *sc;

	sc = et->et_priv;
	tmr_prv_write_4(sc, PRV_TIMER_CTRL, 0);
	tmr_prv_write_4(sc, PRV_TIMER_INTR, PRV_TIMER_INTR_EVENT);
	return (0);
}

/**
 *	arm_tmr_intr - ISR for the eventtimer (private) timer
 *	@arg: pointer to arm_tmr_softc struct
 *
 *	Clears the event register and then calls the eventtimer callback.
 *
 *	RETURNS
 *	Always returns FILTER_HANDLED
 */
static int
arm_tmr_intr(void *arg)
{
	struct arm_tmr_softc *sc;

	sc = arg;
	tmr_prv_write_4(sc, PRV_TIMER_INTR, PRV_TIMER_INTR_EVENT);
	if (sc->et.et_active)
		sc->et.et_event_cb(&sc->et, sc->et.et_arg);
	return (FILTER_HANDLED);
}




/**
 *	arm_tmr_probe - timer probe routine
 *	@dev: new device
 *
 *	The probe function returns success when probed with the fdt compatible
 *	string set to "arm,mpcore-timers".
 *
 *	RETURNS
 *	BUS_PROBE_DEFAULT if the fdt device is compatible, otherwise ENXIO.
 */
static int
arm_tmr_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == TMR_NONE)
		return (ENXIO);

	device_set_desc(dev, "ARM MPCore Timers");
	return (BUS_PROBE_DEFAULT);
}

static int
attach_tc(struct arm_tmr_softc *sc)
{
	int rid;

	if (arm_tmr_tc != NULL)
		return (EBUSY);

	rid = sc->memrid;
	sc->gbl_mem = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY, &rid,
	    RF_ACTIVE);
	if (sc->gbl_mem == NULL) {
		device_printf(sc->dev, "could not allocate gbl mem resources\n");
		return (ENXIO);
	}
	tmr_gbl_write_4(sc, GBL_TIMER_CTRL, 0x00000000);

	arm_tmr_timecount.tc_frequency = sc->clkfreq;
	arm_tmr_timecount.tc_priv = sc;
	tc_init(&arm_tmr_timecount);
	arm_tmr_tc = &arm_tmr_timecount;

	tmr_gbl_write_4(sc, GBL_TIMER_CTRL, GBL_TIMER_CTRL_TIMER_ENABLE);

	return (0);
}

static int
attach_et(struct arm_tmr_softc *sc)
{
	void *ihl;
	int irid, mrid;

	if (arm_tmr_et != NULL)
		return (EBUSY);

	mrid = sc->memrid;
	sc->prv_mem = bus_alloc_resource_any(sc->dev, SYS_RES_MEMORY, &mrid,
	    RF_ACTIVE);
	if (sc->prv_mem == NULL) {
		device_printf(sc->dev, "could not allocate prv mem resources\n");
		return (ENXIO);
	}
	tmr_prv_write_4(sc, PRV_TIMER_CTRL, 0x00000000);

	irid = sc->irqrid;
	sc->prv_irq = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ, &irid, RF_ACTIVE);
	if (sc->prv_irq == NULL) {
		bus_release_resource(sc->dev, SYS_RES_MEMORY, mrid, sc->prv_mem);
		device_printf(sc->dev, "could not allocate prv irq resources\n");
		return (ENXIO);
	}

	if (bus_setup_intr(sc->dev, sc->prv_irq, INTR_TYPE_CLK, arm_tmr_intr,
			NULL, sc, &ihl) != 0) {
		bus_release_resource(sc->dev, SYS_RES_MEMORY, mrid, sc->prv_mem);
		bus_release_resource(sc->dev, SYS_RES_IRQ, irid, sc->prv_irq);
		device_printf(sc->dev, "unable to setup the et irq handler.\n");
		return (ENXIO);
	}

	/*
	 * Setup and register the eventtimer.  Most event timers set their min
	 * and max period values to some value calculated from the clock
	 * frequency.  We might not know yet what our runtime clock frequency
	 * will be, so we just use some safe values.  A max of 2 seconds ensures
	 * that even if our base clock frequency is 2GHz (meaning a 4GHz CPU),
	 * we won't overflow our 32-bit timer count register.  A min of 20
	 * nanoseconds is pretty much completely arbitrary.
	 */
	sc->et.et_name = "MPCore";
	sc->et.et_flags = ET_FLAGS_PERIODIC | ET_FLAGS_ONESHOT | ET_FLAGS_PERCPU;
	sc->et.et_quality = 1000;
	sc->et.et_frequency = sc->clkfreq;
	sc->et.et_min_period = nstosbt(20);
	sc->et.et_max_period =  2 * SBT_1S;
	sc->et.et_start = arm_tmr_start;
	sc->et.et_stop = arm_tmr_stop;
	sc->et.et_priv = sc;
	et_register(&sc->et);
	arm_tmr_et = &sc->et;

	return (0);
}

/**
 *	arm_tmr_attach - attaches the timer to the simplebus
 *	@dev: new device
 *
 *	Reserves memory and interrupt resources, stores the softc structure
 *	globally and registers both the timecount and eventtimer objects.
 *
 *	RETURNS
 *	Zero on success or ENXIO if an error occuried.
 */
static int
arm_tmr_attach(device_t dev)
{
	struct arm_tmr_softc *sc;
	phandle_t node;
	pcell_t clock;
	int et_err, tc_err, tmrtype;

	sc = device_get_softc(dev);
	sc->dev = dev;

	if (arm_tmr_freq_varies) {
		sc->clkfreq = arm_tmr_freq;
	} else {
		if (arm_tmr_freq != 0) {
			sc->clkfreq = arm_tmr_freq;
		} else {
			/* Get the base clock frequency */
			node = ofw_bus_get_node(dev);
			if ((OF_getencprop(node, "clock-frequency", &clock,
			    sizeof(clock))) <= 0) {
				device_printf(dev, "missing clock-frequency "
				    "attribute in FDT\n");
				return (ENXIO);
			}
			sc->clkfreq = clock;
		}
	}

	tmrtype = ofw_bus_search_compatible(dev, compat_data)->ocd_data;
	tc_err = ENXIO;
	et_err = ENXIO;

	/*
	 * If we're handling the global timer and it is fixed-frequency, set it
	 * up to use as a timecounter.  If it's variable frequency it won't work
	 * as a timecounter.  We also can't use it for DELAY(), so hopefully the
	 * platform provides its own implementation. If it doesn't, ours will
	 * get used, but since the frequency isn't set, it will only use the
	 * bogus loop counter.
	 */
	if (tmrtype & TMR_GBL) {
		if (!arm_tmr_freq_varies)
			tc_err = attach_tc(sc);
		else if (bootverbose)
			device_printf(sc->dev,
			    "not using variable-frequency device as timecounter\n");
		sc->memrid++;
		sc->irqrid++;
	}

	/* If we are handling the private timer, set it up as an eventtimer. */
	if (tmrtype & TMR_PRV) {
		et_err = attach_et(sc);
	}

	/*
	 * If we didn't successfully set up a timecounter or eventtimer then we
	 * didn't actually attach at all, return error.
	 */
	if (tc_err != 0 && et_err != 0) {
		return (ENXIO);
	}

#ifdef PLATFORM
	/*
	 * We can register as the DELAY() implementation only if we successfully
	 * set up the global timer.
	 */
	if (tc_err == 0)
		arm_set_delay(arm_tmr_delay, sc);
#endif

	return (0);
}

static device_method_t arm_tmr_methods[] = {
	DEVMETHOD(device_probe,		arm_tmr_probe),
	DEVMETHOD(device_attach,	arm_tmr_attach),
	{ 0, 0 }
};

static driver_t arm_tmr_driver = {
	"mp_tmr",
	arm_tmr_methods,
	sizeof(struct arm_tmr_softc),
};

static devclass_t arm_tmr_devclass;

EARLY_DRIVER_MODULE(mp_tmr, simplebus, arm_tmr_driver, arm_tmr_devclass, 0, 0,
    BUS_PASS_TIMER + BUS_PASS_ORDER_MIDDLE);
EARLY_DRIVER_MODULE(mp_tmr, ofwbus, arm_tmr_driver, arm_tmr_devclass, 0, 0,
    BUS_PASS_TIMER + BUS_PASS_ORDER_MIDDLE);

/*
 * Handle a change in clock frequency.  The mpcore timer runs at half the CPU
 * frequency.  When the CPU frequency changes due to power-saving or thermal
 * management, the platform-specific code that causes the frequency change calls
 * this routine to inform the clock driver, and we in turn inform the event
 * timer system, which actually updates the value in et->frequency for us and
 * reschedules the current event(s) in a way that's atomic with respect to
 * start/stop/intr code that may be running on various CPUs at the time of the
 * call.
 *
 * This routine can also be called by a platform's early init code.  If the
 * value passed is ARM_TMR_FREQUENCY_VARIES, that will cause the attach() code
 * to register as an eventtimer, but not a timecounter.  If the value passed in
 * is any other non-zero value it is used as the fixed frequency for the timer.
 */
void
arm_tmr_change_frequency(uint64_t newfreq)
{

	if (newfreq == ARM_TMR_FREQUENCY_VARIES) {
		arm_tmr_freq_varies = true;
		return;
	}

	arm_tmr_freq = newfreq;
	if (arm_tmr_et != NULL)
		et_change_frequency(arm_tmr_et, newfreq);
}

static void
arm_tmr_delay(int usec, void *arg)
{
	struct arm_tmr_softc *sc = arg;
	int32_t counts_per_usec;
	int32_t counts;
	uint32_t first, last;

	/* Get the number of times to count */
	counts_per_usec = ((arm_tmr_timecount.tc_frequency / 1000000) + 1);

	/*
	 * Clamp the timeout at a maximum value (about 32 seconds with
	 * a 66MHz clock). *Nobody* should be delay()ing for anywhere
	 * near that length of time and if they are, they should be hung
	 * out to dry.
	 */
	if (usec >= (0x80000000U / counts_per_usec))
		counts = (0x80000000U / counts_per_usec) - 1;
	else
		counts = usec * counts_per_usec;

	first = tmr_gbl_read_4(sc, GBL_TIMER_COUNT_LOW);

	while (counts > 0) {
		last = tmr_gbl_read_4(sc, GBL_TIMER_COUNT_LOW);
		counts -= (int32_t)(last - first);
		first = last;
	}
}

#ifndef PLATFORM
/**
 *	DELAY - Delay for at least usec microseconds.
 *	@usec: number of microseconds to delay by
 *
 *	This function is called all over the kernel and is suppose to provide a
 *	consistent delay.  This function may also be called before the console
 *	is setup so no printf's can be called here.
 *
 *	RETURNS:
 *	nothing
 */
void
DELAY(int usec)
{
	struct arm_tmr_softc *sc;
	int32_t counts;

	TSENTER();
	/* Check the timers are setup, if not just use a for loop for the meantime */
	if (arm_tmr_tc == NULL || arm_tmr_timecount.tc_frequency == 0) {
		for (; usec > 0; usec--)
			for (counts = 200; counts > 0; counts--)
				cpufunc_nullop();	/* Prevent gcc from optimizing
							 * out the loop
							 */
	} else {
		sc = arm_tmr_tc->tc_priv;
		arm_tmr_delay(usec, sc);
	}
	TSEXIT();
}
#endif
