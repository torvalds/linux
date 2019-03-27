/*-
 * Copyright (c) 2012 Ganbold Tsagaankhuu <ganbold@freebsd.org>
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
#include <machine/intr.h>
#include <machine/machdep.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <dev/extres/clk/clk.h>

#if defined(__aarch64__)
#include "opt_soc.h"
#else
#include <arm/allwinner/aw_machdep.h>
#endif

/**
 * Timer registers addr
 *
 */
#define	TIMER_IRQ_EN_REG 	0x00
#define	 TIMER_IRQ_ENABLE(x)	(1 << x)

#define	TIMER_IRQ_STA_REG 	0x04
#define	 TIMER_IRQ_PENDING(x)	(1 << x)

/*
 * On A10, A13, A20 and A31/A31s 6 timers are available
 */
#define	TIMER_CTRL_REG(x)		(0x10 + 0x10 * x)
#define	 TIMER_CTRL_START		(1 << 0)
#define	 TIMER_CTRL_AUTORELOAD		(1 << 1)
#define	 TIMER_CTRL_CLKSRC_MASK		(3 << 2)
#define	 TIMER_CTRL_OSC24M		(1 << 2)
#define	 TIMER_CTRL_PRESCALAR_MASK	(0x7 << 4)
#define	 TIMER_CTRL_PRESCALAR(x)	((x - 1) << 4)
#define	 TIMER_CTRL_MODE_MASK		(1 << 7)
#define	 TIMER_CTRL_MODE_SINGLE		(1 << 7)
#define	 TIMER_CTRL_MODE_CONTINUOUS	(0 << 7)
#define	TIMER_INTV_REG(x)		(0x14 + 0x10 * x)
#define	TIMER_CURV_REG(x)		(0x18 + 0x10 * x)

/* 64 bit counter, available in A10 and A13 */
#define	CNT64_CTRL_REG		0xa0
#define	 CNT64_CTRL_RL_EN	0x02 /* read latch enable */
#define	CNT64_LO_REG	0xa4
#define	CNT64_HI_REG	0xa8

#define	SYS_TIMER_CLKSRC	24000000 /* clock source */

enum a10_timer_type {
	A10_TIMER = 1,
	A23_TIMER,
};

struct a10_timer_softc {
	device_t 	sc_dev;
	struct resource *res[2];
	void 		*sc_ih;		/* interrupt handler */
	uint32_t 	sc_period;
	uint64_t 	timer0_freq;
	struct eventtimer	et;
	enum a10_timer_type	type;
};

#define timer_read_4(sc, reg)	\
	bus_read_4(sc->res[A10_TIMER_MEMRES], reg)
#define timer_write_4(sc, reg, val)	\
	bus_write_4(sc->res[A10_TIMER_MEMRES], reg, val)

static u_int	a10_timer_get_timecount(struct timecounter *);
static int	a10_timer_timer_start(struct eventtimer *,
    sbintime_t first, sbintime_t period);
static int	a10_timer_timer_stop(struct eventtimer *);

static uint64_t timer_read_counter64(struct a10_timer_softc *sc);
static void a10_timer_eventtimer_setup(struct a10_timer_softc *sc);

static void a23_timer_timecounter_setup(struct a10_timer_softc *sc);
static u_int a23_timer_get_timecount(struct timecounter *tc);

static int a10_timer_irq(void *);
static int a10_timer_probe(device_t);
static int a10_timer_attach(device_t);

#if defined(__arm__)
static delay_func a10_timer_delay;
#endif

static struct timecounter a10_timer_timecounter = {
	.tc_name           = "a10_timer timer0",
	.tc_get_timecount  = a10_timer_get_timecount,
	.tc_counter_mask   = ~0u,
	.tc_frequency      = 0,
	.tc_quality        = 1000,
};

static struct timecounter a23_timer_timecounter = {
	.tc_name           = "a10_timer timer0",
	.tc_get_timecount  = a23_timer_get_timecount,
	.tc_counter_mask   = ~0u,
	.tc_frequency      = 0,
	/* We want it to be selected over the arm generic timecounter */
	.tc_quality        = 2000,
};

#define	A10_TIMER_MEMRES		0
#define	A10_TIMER_IRQRES		1

static struct resource_spec a10_timer_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ -1, 0 }
};

static struct ofw_compat_data compat_data[] = {
	{"allwinner,sun4i-a10-timer", A10_TIMER},
	{"allwinner,sun8i-a23-timer", A23_TIMER},
	{NULL, 0},
};

static int
a10_timer_probe(device_t dev)
{
	struct a10_timer_softc *sc;
#if defined(__arm__)
	u_int soc_family;
#endif

	sc = device_get_softc(dev);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

#if defined(__arm__)
	/* For SoC >= A10 we have the ARM Timecounter/Eventtimer */
	soc_family = allwinner_soc_family();
	if (soc_family != ALLWINNERSOC_SUN4I &&
	    soc_family != ALLWINNERSOC_SUN5I)
		return (ENXIO);
#endif

	device_set_desc(dev, "Allwinner timer");
	return (BUS_PROBE_DEFAULT);
}

static int
a10_timer_attach(device_t dev)
{
	struct a10_timer_softc *sc;
	clk_t clk;
	int err;

	sc = device_get_softc(dev);
	sc->type = ofw_bus_search_compatible(dev, compat_data)->ocd_data;

	if (bus_alloc_resources(dev, a10_timer_spec, sc->res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	sc->sc_dev = dev;

	/* Setup and enable the timer interrupt */
	err = bus_setup_intr(dev, sc->res[A10_TIMER_IRQRES], INTR_TYPE_CLK,
	    a10_timer_irq, NULL, sc, &sc->sc_ih);
	if (err != 0) {
		bus_release_resources(dev, a10_timer_spec, sc->res);
		device_printf(dev, "Unable to setup the clock irq handler, "
		    "err = %d\n", err);
		return (ENXIO);
	}

	if (clk_get_by_ofw_index(dev, 0, 0, &clk) != 0)
		sc->timer0_freq = SYS_TIMER_CLKSRC;
	else {
		if (clk_get_freq(clk, &sc->timer0_freq) != 0) {
			device_printf(dev, "Cannot get clock source frequency\n");
			return (ENXIO);
		}
	}

#if defined(__arm__)
	a10_timer_eventtimer_setup(sc);
	arm_set_delay(a10_timer_delay, sc);
	a10_timer_timecounter.tc_priv = sc;
	a10_timer_timecounter.tc_frequency = sc->timer0_freq;
	tc_init(&a10_timer_timecounter);
#elif defined(__aarch64__)
	a23_timer_timecounter_setup(sc);
#endif

	if (bootverbose) {
		device_printf(sc->sc_dev, "clock: hz=%d stathz = %d\n", hz, stathz);

		device_printf(sc->sc_dev, "event timer clock frequency %ju\n", 
		    sc->timer0_freq);
		device_printf(sc->sc_dev, "timecounter clock frequency %jd\n", 
		    a10_timer_timecounter.tc_frequency);
	}

	return (0);
}

static int
a10_timer_irq(void *arg)
{
	struct a10_timer_softc *sc;
	uint32_t val;

	sc = (struct a10_timer_softc *)arg;

	/* Clear interrupt pending bit. */
	timer_write_4(sc, TIMER_IRQ_STA_REG, TIMER_IRQ_PENDING(0));

	val = timer_read_4(sc, TIMER_CTRL_REG(0));

	/*
	 * Disabled autoreload and sc_period > 0 means 
	 * timer_start was called with non NULL first value.
	 * Now we will set periodic timer with the given period 
	 * value.
	 */
	if ((val & (1<<1)) == 0 && sc->sc_period > 0) {
		/* Update timer */
		timer_write_4(sc, TIMER_CURV_REG(0), sc->sc_period);

		/* Make periodic and enable */
		val |= TIMER_CTRL_AUTORELOAD | TIMER_CTRL_START;
		timer_write_4(sc, TIMER_CTRL_REG(0), val);
	}

	if (sc->et.et_active)
		sc->et.et_event_cb(&sc->et, sc->et.et_arg);

	return (FILTER_HANDLED);
}

/*
 * Event timer function for A10 and A13
 */

static void
a10_timer_eventtimer_setup(struct a10_timer_softc *sc)
{
	uint32_t val;

	/* Set clock source to OSC24M, 1 pre-division, continuous mode */
	val = timer_read_4(sc, TIMER_CTRL_REG(0));
	val &= ~TIMER_CTRL_PRESCALAR_MASK | ~TIMER_CTRL_MODE_MASK | ~TIMER_CTRL_CLKSRC_MASK;
	val |= TIMER_CTRL_PRESCALAR(1) | TIMER_CTRL_OSC24M;
	timer_write_4(sc, TIMER_CTRL_REG(0), val);

	/* Enable timer0 */
	val = timer_read_4(sc, TIMER_IRQ_EN_REG);
	val |= TIMER_IRQ_ENABLE(0);
	timer_write_4(sc, TIMER_IRQ_EN_REG, val);

	/* Set desired frequency in event timer and timecounter */
	sc->et.et_frequency = sc->timer0_freq;
	sc->et.et_name = "a10_timer Eventtimer";
	sc->et.et_flags = ET_FLAGS_ONESHOT | ET_FLAGS_PERIODIC;
	sc->et.et_quality = 1000;
	sc->et.et_min_period = (0x00000005LLU << 32) / sc->et.et_frequency;
	sc->et.et_max_period = (0xfffffffeLLU << 32) / sc->et.et_frequency;
	sc->et.et_start = a10_timer_timer_start;
	sc->et.et_stop = a10_timer_timer_stop;
	sc->et.et_priv = sc;
	et_register(&sc->et);
}

static int
a10_timer_timer_start(struct eventtimer *et, sbintime_t first,
    sbintime_t period)
{
	struct a10_timer_softc *sc;
	uint32_t count;
	uint32_t val;

	sc = (struct a10_timer_softc *)et->et_priv;

	if (period != 0)
		sc->sc_period = ((uint32_t)et->et_frequency * period) >> 32;
	else
		sc->sc_period = 0;
	if (first != 0)
		count = ((uint32_t)et->et_frequency * first) >> 32;
	else
		count = sc->sc_period;

	/* Update timer values */
	timer_write_4(sc, TIMER_INTV_REG(0), sc->sc_period);
	timer_write_4(sc, TIMER_CURV_REG(0), count);

	val = timer_read_4(sc, TIMER_CTRL_REG(0));
	if (period != 0) {
		/* periodic */
		val |= TIMER_CTRL_AUTORELOAD;
	} else {
		/* oneshot */
		val &= ~TIMER_CTRL_AUTORELOAD;
	}
	/* Enable timer0 */
	val |= TIMER_IRQ_ENABLE(0);
	timer_write_4(sc, TIMER_CTRL_REG(0), val);

	return (0);
}

static int
a10_timer_timer_stop(struct eventtimer *et)
{
	struct a10_timer_softc *sc;
	uint32_t val;

	sc = (struct a10_timer_softc *)et->et_priv;

	/* Disable timer0 */
	val = timer_read_4(sc, TIMER_CTRL_REG(0));
	val &= ~TIMER_CTRL_START;
	timer_write_4(sc, TIMER_CTRL_REG(0), val);

	sc->sc_period = 0;

	return (0);
}

/*
 * Timecounter functions for A23 and above
 */

static void
a23_timer_timecounter_setup(struct a10_timer_softc *sc)
{
	uint32_t val;

	/* Set clock source to OSC24M, 1 pre-division, continuous mode */
	val = timer_read_4(sc, TIMER_CTRL_REG(0));
	val &= ~TIMER_CTRL_PRESCALAR_MASK | ~TIMER_CTRL_MODE_MASK | ~TIMER_CTRL_CLKSRC_MASK;
	val |= TIMER_CTRL_PRESCALAR(1) | TIMER_CTRL_OSC24M;
	timer_write_4(sc, TIMER_CTRL_REG(0), val);

	/* Set reload value */
	timer_write_4(sc, TIMER_INTV_REG(0), ~0);
	val = timer_read_4(sc, TIMER_INTV_REG(0));

	/* Enable timer0 */
	val = timer_read_4(sc, TIMER_CTRL_REG(0));
	val |= TIMER_CTRL_AUTORELOAD | TIMER_CTRL_START;
	timer_write_4(sc, TIMER_CTRL_REG(0), val);

	val = timer_read_4(sc, TIMER_CURV_REG(0));

	a23_timer_timecounter.tc_priv = sc;
	a23_timer_timecounter.tc_frequency = sc->timer0_freq;
	tc_init(&a23_timer_timecounter);
}

static u_int
a23_timer_get_timecount(struct timecounter *tc)
{
	struct a10_timer_softc *sc;
	uint32_t val;

	sc = (struct a10_timer_softc *)tc->tc_priv;
	if (sc == NULL)
		return (0);

	val = timer_read_4(sc, TIMER_CURV_REG(0));
	/* Counter count backwards */
	return (~0u - val);
}

/*
 * Timecounter functions for A10 and A13, using the 64 bits counter
 */

static uint64_t
timer_read_counter64(struct a10_timer_softc *sc)
{
	uint32_t lo, hi;

	/* Latch counter, wait for it to be ready to read. */
	timer_write_4(sc, CNT64_CTRL_REG, CNT64_CTRL_RL_EN);
	while (timer_read_4(sc, CNT64_CTRL_REG) & CNT64_CTRL_RL_EN)
		continue;

	hi = timer_read_4(sc, CNT64_HI_REG);
	lo = timer_read_4(sc, CNT64_LO_REG);

	return (((uint64_t)hi << 32) | lo);
}

#if defined(__arm__)
static void
a10_timer_delay(int usec, void *arg)
{
	struct a10_timer_softc *sc = arg;
	uint64_t end, now;

	now = timer_read_counter64(sc);
	end = now + (sc->timer0_freq / 1000000) * (usec + 1);

	while (now < end)
		now = timer_read_counter64(sc);
}
#endif

static u_int
a10_timer_get_timecount(struct timecounter *tc)
{

	if (tc->tc_priv == NULL)
		return (0);

	return ((u_int)timer_read_counter64(tc->tc_priv));
}

static device_method_t a10_timer_methods[] = {
	DEVMETHOD(device_probe,		a10_timer_probe),
	DEVMETHOD(device_attach,	a10_timer_attach),

	DEVMETHOD_END
};

static driver_t a10_timer_driver = {
	"a10_timer",
	a10_timer_methods,
	sizeof(struct a10_timer_softc),
};

static devclass_t a10_timer_devclass;

EARLY_DRIVER_MODULE(a10_timer, simplebus, a10_timer_driver, a10_timer_devclass, 0, 0,
    BUS_PASS_TIMER + BUS_PASS_ORDER_MIDDLE);
