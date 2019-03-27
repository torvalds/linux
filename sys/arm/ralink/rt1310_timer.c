/*-
 * Copyright (c) 2011 Jakub Wojciech Klama <jceel@FreeBSD.org>
 * Copyright (c) 2015 Hiroki Mori
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
 *
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
#include <sys/timetc.h>
#include <sys/timeet.h>
#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/ralink/rt1310reg.h>
#include <arm/ralink/rt1310var.h>

struct rt1310_timer_softc {
	device_t		lt_dev;
	struct eventtimer	lt_et;
	struct resource	*	lt_res[8];
	bus_space_tag_t		lt_bst0;
	bus_space_handle_t	lt_bsh0;
	bus_space_tag_t		lt_bst1;
	bus_space_handle_t	lt_bsh1;
	bus_space_tag_t		lt_bst2;
	bus_space_handle_t	lt_bsh2;
	bus_space_tag_t		lt_bst3;
	bus_space_handle_t	lt_bsh3;
	int			lt_oneshot;
	uint32_t		lt_period;
};

static struct resource_spec rt1310_timer_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	1,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	2,	RF_ACTIVE },
	{ SYS_RES_MEMORY,	3,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		1,	RF_ACTIVE },
	{ SYS_RES_IRQ,		2,	RF_ACTIVE },
	{ -1, 0 }
};

static struct rt1310_timer_softc *timer_softc = NULL;
static int rt1310_timer_initialized = 0;
static int rt1310_timer_probe(device_t);
static int rt1310_timer_attach(device_t);
static int rt1310_timer_start(struct eventtimer *,
    sbintime_t first, sbintime_t period);
static int rt1310_timer_stop(struct eventtimer *et);
static unsigned rt1310_get_timecount(struct timecounter *);
static int rt1310_hardclock(void *);

#define	timer0_read_4(sc, reg)			\
    bus_space_read_4(sc->lt_bst0, sc->lt_bsh0, reg)
#define	timer0_write_4(sc, reg, val)		\
    bus_space_write_4(sc->lt_bst0, sc->lt_bsh0, reg, val)
#define	timer0_clear(sc)			\
    do {					\
	    timer0_write_4(sc, RT_TIMER_LOAD, 0);	\
	    timer0_write_4(sc, RT_TIMER_VALUE, 0);	\
    } while(0)

#define	timer1_read_4(sc, reg)			\
    bus_space_read_4(sc->lt_bst1, sc->lt_bsh1, reg)
#define	timer1_write_4(sc, reg, val)		\
    bus_space_write_4(sc->lt_bst1, sc->lt_bsh1, reg, val)
#define	timer1_clear(sc)			\
    do {					\
	    timer1_write_4(sc, RT_TIMER_LOAD, 0);	\
	    timer1_write_4(sc, RT_TIMER_VALUE, 0);	\
    } while(0)

#define	timer2_read_4(sc, reg)			\
    bus_space_read_4(sc->lt_bst1, sc->lt_bsh2, reg)
#define	timer2_write_4(sc, reg, val)		\
    bus_space_write_4(sc->lt_bst2, sc->lt_bsh2, reg, val)
#define	timer3_write_4(sc, reg, val)		\
    bus_space_write_4(sc->lt_bst3, sc->lt_bsh3, reg, val)


static struct timecounter rt1310_timecounter = {
	.tc_get_timecount = rt1310_get_timecount,
	.tc_name = "RT1310ATimer1",
	.tc_frequency = 0, /* will be filled later */
	.tc_counter_mask = ~0u,
	.tc_quality = 1000,
};

static int
rt1310_timer_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "rt,timer"))
		return (ENXIO);

	device_set_desc(dev, "RT1310 timer");
	return (BUS_PROBE_DEFAULT);
}

static int
rt1310_timer_attach(device_t dev)
{
	void *intrcookie;
	struct rt1310_timer_softc *sc = device_get_softc(dev);
	phandle_t node;
	uint32_t freq;

	if (timer_softc)
		return (ENXIO);

	timer_softc = sc;

	if (bus_alloc_resources(dev, rt1310_timer_spec, sc->lt_res)) {
		device_printf(dev, "could not allocate resources\n");
		return (ENXIO);
	}

	sc->lt_bst0 = rman_get_bustag(sc->lt_res[0]);
	sc->lt_bsh0 = rman_get_bushandle(sc->lt_res[0]);
	sc->lt_bst1 = rman_get_bustag(sc->lt_res[1]);
	sc->lt_bsh1 = rman_get_bushandle(sc->lt_res[1]);
	sc->lt_bst2 = rman_get_bustag(sc->lt_res[2]);
	sc->lt_bsh2 = rman_get_bushandle(sc->lt_res[2]);
	sc->lt_bst3 = rman_get_bustag(sc->lt_res[3]);
	sc->lt_bsh3 = rman_get_bushandle(sc->lt_res[3]);

	/* Timer2 interrupt */
	if (bus_setup_intr(dev, sc->lt_res[6], INTR_TYPE_CLK,
	    rt1310_hardclock, NULL, sc, &intrcookie)) {
		device_printf(dev, "could not setup interrupt handler\n");
		bus_release_resources(dev, rt1310_timer_spec, sc->lt_res);
		return (ENXIO);
	}

	/* Enable timer clock */
/*
	rt1310_pwr_write(dev, LPC_CLKPWR_TIMCLK_CTRL1,
	    LPC_CLKPWR_TIMCLK_CTRL1_TIMER0 |
	    LPC_CLKPWR_TIMCLK_CTRL1_TIMER1);
*/

	/* Get PERIPH_CLK encoded in parent bus 'bus-frequency' property */

	node = ofw_bus_get_node(dev);
	if (OF_getprop(OF_parent(node), "bus-frequency", &freq,
	    sizeof(pcell_t)) <= 0) {
		bus_release_resources(dev, rt1310_timer_spec, sc->lt_res);
		bus_teardown_intr(dev, sc->lt_res[2], intrcookie);
		device_printf(dev, "could not obtain base clock frequency\n");
		return (ENXIO);
	}

	freq = fdt32_to_cpu(freq);

	/* Set desired frequency in event timer and timecounter */
	sc->lt_et.et_frequency = (uint64_t)freq;
	rt1310_timecounter.tc_frequency = (uint64_t)freq;

	sc->lt_et.et_name = "RT1310ATimer2";
	sc->lt_et.et_flags = ET_FLAGS_PERIODIC | ET_FLAGS_ONESHOT;
	sc->lt_et.et_quality = 1000;
	sc->lt_et.et_min_period = (0x00000002LLU << 32) / sc->lt_et.et_frequency;
	sc->lt_et.et_max_period = (0xfffffffeLLU << 32) / sc->lt_et.et_frequency;
	sc->lt_et.et_start = rt1310_timer_start;
	sc->lt_et.et_stop = rt1310_timer_stop;
	sc->lt_et.et_priv = sc;

	et_register(&sc->lt_et);
	tc_init(&rt1310_timecounter);

	/* Reset and enable timecounter */

	timer0_write_4(sc, RT_TIMER_CONTROL, 0);
	timer1_write_4(sc, RT_TIMER_CONTROL, 0);
	timer2_write_4(sc, RT_TIMER_CONTROL, 0);
	timer3_write_4(sc, RT_TIMER_CONTROL, 0);

	timer1_write_4(sc, RT_TIMER_LOAD, ~0);
	timer1_write_4(sc, RT_TIMER_VALUE, ~0);
	timer1_write_4(sc, RT_TIMER_CONTROL, 
		RT_TIMER_CTRL_ENABLE | RT_TIMER_CTRL_PERIODCAL);

	/* DELAY() now can work properly */
	rt1310_timer_initialized = 1;

	return (0);
}

static int
rt1310_timer_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{
	struct rt1310_timer_softc *sc = (struct rt1310_timer_softc *)et->et_priv;
	uint32_t ticks;

	if (period == 0) {
		sc->lt_oneshot = 1;
		sc->lt_period = 0;
	} else {
		sc->lt_oneshot = 0;
		sc->lt_period = ((uint32_t)et->et_frequency * period) >> 32;
	}

	if (first == 0)
		ticks = sc->lt_period;
	else
		ticks = ((uint32_t)et->et_frequency * first) >> 32;

	/* Reset timer */
	timer2_write_4(sc, RT_TIMER_CONTROL, 0);

	/* Start timer */
	timer2_write_4(sc, RT_TIMER_LOAD, ticks);
	timer2_write_4(sc, RT_TIMER_VALUE, ticks);
	timer2_write_4(sc, RT_TIMER_CONTROL, 
		RT_TIMER_CTRL_ENABLE | RT_TIMER_CTRL_INTCTL);

	return (0);
}

static int
rt1310_timer_stop(struct eventtimer *et)
{
	struct rt1310_timer_softc *sc = (struct rt1310_timer_softc *)et->et_priv;

	timer2_write_4(sc, RT_TIMER_CONTROL, 0);

	return (0);
}

static device_method_t rt1310_timer_methods[] = {
	DEVMETHOD(device_probe,		rt1310_timer_probe),
	DEVMETHOD(device_attach,	rt1310_timer_attach),
	{ 0, 0 }
};

static driver_t rt1310_timer_driver = {
	"timer",
	rt1310_timer_methods,
	sizeof(struct rt1310_timer_softc),
};

static devclass_t rt1310_timer_devclass;

EARLY_DRIVER_MODULE(timer, simplebus, rt1310_timer_driver, rt1310_timer_devclass, 0, 0, BUS_PASS_TIMER);

static int
rt1310_hardclock(void *arg)
{
	struct rt1310_timer_softc *sc = (struct rt1310_timer_softc *)arg;

	/* Reset pending interrupt */
	timer2_write_4(sc, RT_TIMER_CONTROL, 
	    timer2_read_4(sc, RT_TIMER_CONTROL) | 0x08);
	timer2_write_4(sc, RT_TIMER_CONTROL, 
	    timer2_read_4(sc, RT_TIMER_CONTROL) & 0x1fb);

	/* Start timer again */
	if (!sc->lt_oneshot) {
		timer2_write_4(sc, RT_TIMER_LOAD, sc->lt_period);
		timer2_write_4(sc, RT_TIMER_VALUE, sc->lt_period);
		timer2_write_4(sc, RT_TIMER_CONTROL,
			RT_TIMER_CTRL_ENABLE | RT_TIMER_CTRL_INTCTL);
	}

	if (sc->lt_et.et_active)
		sc->lt_et.et_event_cb(&sc->lt_et, sc->lt_et.et_arg);

	return (FILTER_HANDLED);
}

static unsigned
rt1310_get_timecount(struct timecounter *tc)
{
	return ~timer1_read_4(timer_softc, RT_TIMER_VALUE);
}

void
DELAY(int usec)
{
	uint32_t counter;
	uint32_t first, last;
	int val = (rt1310_timecounter.tc_frequency / 1000000 + 1) * usec;

	/* Timer is not initialized yet */
	if (!rt1310_timer_initialized) {
		for (; usec > 0; usec--)
			for (counter = 100; counter > 0; counter--)
				;
		return;
	}
	TSENTER();

	first = rt1310_get_timecount(&rt1310_timecounter);
	while (val > 0) {
		last = rt1310_get_timecount(&rt1310_timecounter);
		if (last < first) {
			/* Timer rolled over */
			last = first;
		}
		
		val -= (last - first);
		first = last;
	}
	TSEXIT();
}
