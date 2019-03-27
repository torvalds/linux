/*-
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Damjan Marion <dmarion@Freebsd.org>
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
#include <machine/bus.h>

#include <machine/machdep.h> /* For arm_set_delay */

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <arm/ti/ti_prcm.h>
#include <arm/ti/ti_hwmods.h>

#include "am335x_dmtreg.h"

struct am335x_dmtimer_softc {
	device_t		dev;
	int			tmr_mem_rid;
	struct resource *	tmr_mem_res;
	int			tmr_irq_rid;
	struct resource *	tmr_irq_res;
	void			*tmr_irq_handler;
	uint32_t		sysclk_freq;
	uint32_t		tclr;		/* Cached TCLR register. */
	union {
		struct timecounter tc;
		struct eventtimer et;
	} func;
	int			tmr_num;	/* Hardware unit number. */
	char			tmr_name[12];	/* "DMTimerN", N = tmr_num */
};

static struct am335x_dmtimer_softc *am335x_dmtimer_et_sc = NULL;
static struct am335x_dmtimer_softc *am335x_dmtimer_tc_sc = NULL;

static void am335x_dmtimer_delay(int, void *);

/*
 * We use dmtimer2 for eventtimer and dmtimer3 for timecounter.
 */
#define ET_TMR_NUM      2
#define TC_TMR_NUM      3

/* List of compatible strings for FDT tree */
static struct ofw_compat_data compat_data[] = {
	{"ti,am335x-timer",     1},
	{"ti,am335x-timer-1ms", 1},
	{NULL,                  0},
};

#define	DMTIMER_READ4(sc, reg)		bus_read_4((sc)->tmr_mem_res, (reg))
#define	DMTIMER_WRITE4(sc, reg, val)	bus_write_4((sc)->tmr_mem_res, (reg), (val))

static int
am335x_dmtimer_et_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{
	struct am335x_dmtimer_softc *sc;
	uint32_t initial_count, reload_count;

	sc = et->et_priv;

	/*
	 * Stop the timer before changing it.  This routine will often be called
	 * while the timer is still running, to either lengthen or shorten the
	 * current event time.  We need to ensure the timer doesn't expire while
	 * we're working with it.
	 *
	 * Also clear any pending interrupt status, because it's at least
	 * theoretically possible that we're running in a primary interrupt
	 * context now, and a timer interrupt could be pending even before we
	 * stopped the timer.  The more likely case is that we're being called
	 * from the et_event_cb() routine dispatched from our own handler, but
	 * it's not clear to me that that's the only case possible.
	 */
	sc->tclr &= ~(DMT_TCLR_START | DMT_TCLR_AUTOLOAD);
	DMTIMER_WRITE4(sc, DMT_TCLR, sc->tclr);
	DMTIMER_WRITE4(sc, DMT_IRQSTATUS, DMT_IRQ_OVF);

	if (period != 0) {
		reload_count = ((uint32_t)et->et_frequency * period) >> 32;
		sc->tclr |= DMT_TCLR_AUTOLOAD;
	} else {
		reload_count = 0;
	}

	if (first != 0)
		initial_count = ((uint32_t)et->et_frequency * first) >> 32;
	else
		initial_count = reload_count;

	/*
	 * Set auto-reload and current-count values.  This timer hardware counts
	 * up from the initial/reload value and interrupts on the zero rollover.
	 */
	DMTIMER_WRITE4(sc, DMT_TLDR, 0xFFFFFFFF - reload_count);
	DMTIMER_WRITE4(sc, DMT_TCRR, 0xFFFFFFFF - initial_count);

	/* Enable overflow interrupt, and start the timer. */
	DMTIMER_WRITE4(sc, DMT_IRQENABLE_SET, DMT_IRQ_OVF);
	sc->tclr |= DMT_TCLR_START;
	DMTIMER_WRITE4(sc, DMT_TCLR, sc->tclr);

	return (0);
}

static int
am335x_dmtimer_et_stop(struct eventtimer *et)
{
	struct am335x_dmtimer_softc *sc;

	sc = et->et_priv;

	/* Stop timer, disable and clear interrupt. */
	sc->tclr &= ~(DMT_TCLR_START | DMT_TCLR_AUTOLOAD);
	DMTIMER_WRITE4(sc, DMT_TCLR, sc->tclr);
	DMTIMER_WRITE4(sc, DMT_IRQENABLE_CLR, DMT_IRQ_OVF);
	DMTIMER_WRITE4(sc, DMT_IRQSTATUS, DMT_IRQ_OVF);
	return (0);
}

static int
am335x_dmtimer_et_intr(void *arg)
{
	struct am335x_dmtimer_softc *sc;

	sc = arg;

	/* Ack the interrupt, and invoke the callback if it's still enabled. */
	DMTIMER_WRITE4(sc, DMT_IRQSTATUS, DMT_IRQ_OVF);
	if (sc->func.et.et_active)
		sc->func.et.et_event_cb(&sc->func.et, sc->func.et.et_arg);

	return (FILTER_HANDLED);
}

static int
am335x_dmtimer_et_init(struct am335x_dmtimer_softc *sc)
{
	KASSERT(am335x_dmtimer_et_sc == NULL, ("already have an eventtimer"));

	/*
	 * Setup eventtimer interrupt handling.  Panic if anything goes wrong,
	 * because the system just isn't going to run without an eventtimer.
	 */
	sc->tmr_irq_res = bus_alloc_resource_any(sc->dev, SYS_RES_IRQ,
	    &sc->tmr_irq_rid, RF_ACTIVE);
	if (sc->tmr_irq_res == NULL)
		panic("am335x_dmtimer: could not allocate irq resources");
	if (bus_setup_intr(sc->dev, sc->tmr_irq_res, INTR_TYPE_CLK,
	    am335x_dmtimer_et_intr, NULL, sc, &sc->tmr_irq_handler) != 0)
		panic("am335x_dmtimer: count not setup irq handler");

	sc->func.et.et_name = sc->tmr_name;
	sc->func.et.et_flags = ET_FLAGS_PERIODIC | ET_FLAGS_ONESHOT;
	sc->func.et.et_quality = 500;
	sc->func.et.et_frequency = sc->sysclk_freq;
	sc->func.et.et_min_period =
	    ((0x00000005LLU << 32) / sc->func.et.et_frequency);
	sc->func.et.et_max_period =
	    (0xfffffffeLLU << 32) / sc->func.et.et_frequency;
	sc->func.et.et_start = am335x_dmtimer_et_start;
	sc->func.et.et_stop = am335x_dmtimer_et_stop;
	sc->func.et.et_priv = sc;

	am335x_dmtimer_et_sc = sc;
	et_register(&sc->func.et);

	return (0);
}

static unsigned
am335x_dmtimer_tc_get_timecount(struct timecounter *tc)
{
	struct am335x_dmtimer_softc *sc;

	sc = tc->tc_priv;

	return (DMTIMER_READ4(sc, DMT_TCRR));
}

static int
am335x_dmtimer_tc_init(struct am335x_dmtimer_softc *sc)
{
	KASSERT(am335x_dmtimer_tc_sc == NULL, ("already have a timecounter"));

	/* Set up timecounter, start it, register it. */
	DMTIMER_WRITE4(sc, DMT_TSICR, DMT_TSICR_RESET);
	while (DMTIMER_READ4(sc, DMT_TIOCP_CFG) & DMT_TIOCP_RESET)
		continue;

	sc->tclr |= DMT_TCLR_START | DMT_TCLR_AUTOLOAD;
	DMTIMER_WRITE4(sc, DMT_TLDR, 0);
	DMTIMER_WRITE4(sc, DMT_TCRR, 0);
	DMTIMER_WRITE4(sc, DMT_TCLR, sc->tclr);

	sc->func.tc.tc_name           = sc->tmr_name;
	sc->func.tc.tc_get_timecount  = am335x_dmtimer_tc_get_timecount;
	sc->func.tc.tc_counter_mask   = ~0u;
	sc->func.tc.tc_frequency      = sc->sysclk_freq;
	sc->func.tc.tc_quality        = 500;
	sc->func.tc.tc_priv           = sc;

	am335x_dmtimer_tc_sc = sc;
	tc_init(&sc->func.tc);

	arm_set_delay(am335x_dmtimer_delay, sc);

	return (0);
}

static int
am335x_dmtimer_probe(device_t dev)
{
	char strbuf[32];
	int tmr_num;

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	/*
	 * Get the hardware unit number (the N from ti,hwmods="timerN").
	 * If this isn't the hardware unit we're going to use for either the
	 * eventtimer or the timecounter, no point in instantiating the device.
	 */
	tmr_num = ti_hwmods_get_unit(dev, "timer");
	if (tmr_num != ET_TMR_NUM && tmr_num != TC_TMR_NUM)
		return (ENXIO);

	snprintf(strbuf, sizeof(strbuf), "AM335x DMTimer%d", tmr_num);
	device_set_desc_copy(dev, strbuf);

	return(BUS_PROBE_DEFAULT);
}

static int
am335x_dmtimer_attach(device_t dev)
{
	struct am335x_dmtimer_softc *sc;
	clk_ident_t timer_id;
	int err;

	sc = device_get_softc(dev);
	sc->dev = dev;

	/* Get the base clock frequency. */
	if ((err = ti_prcm_clk_get_source_freq(SYS_CLK, &sc->sysclk_freq)) != 0)
		return (err);

	/* Enable clocks and power on the device. */
	if ((timer_id = ti_hwmods_get_clock(dev)) == INVALID_CLK_IDENT)
		return (ENXIO);
	if ((err = ti_prcm_clk_set_source(timer_id, SYSCLK_CLK)) != 0)
		return (err);
	if ((err = ti_prcm_clk_enable(timer_id)) != 0)
		return (err);

	/* Request the memory resources. */
	sc->tmr_mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY,
	    &sc->tmr_mem_rid, RF_ACTIVE);
	if (sc->tmr_mem_res == NULL) {
		return (ENXIO);
	}

	sc->tmr_num = ti_hwmods_get_unit(dev, "timer");
	snprintf(sc->tmr_name, sizeof(sc->tmr_name), "DMTimer%d", sc->tmr_num);

	/*
	 * Go set up either a timecounter or eventtimer.  We wouldn't have
	 * attached if we weren't one or the other.
	 */
	if (sc->tmr_num == ET_TMR_NUM)
		am335x_dmtimer_et_init(sc);
	else if (sc->tmr_num == TC_TMR_NUM)
		am335x_dmtimer_tc_init(sc);
	else
		panic("am335x_dmtimer: bad timer number %d", sc->tmr_num);

	return (0);
}

static device_method_t am335x_dmtimer_methods[] = {
	DEVMETHOD(device_probe,		am335x_dmtimer_probe),
	DEVMETHOD(device_attach,	am335x_dmtimer_attach),
	{ 0, 0 }
};

static driver_t am335x_dmtimer_driver = {
	"am335x_dmtimer",
	am335x_dmtimer_methods,
	sizeof(struct am335x_dmtimer_softc),
};

static devclass_t am335x_dmtimer_devclass;

DRIVER_MODULE(am335x_dmtimer, simplebus, am335x_dmtimer_driver, am335x_dmtimer_devclass, 0, 0);
MODULE_DEPEND(am335x_dmtimer, am335x_prcm, 1, 1, 1);

static void
am335x_dmtimer_delay(int usec, void *arg)
{
	struct am335x_dmtimer_softc *sc = arg;
	int32_t counts;
	uint32_t first, last;

	/* Get the number of times to count */
	counts = (usec + 1) * (sc->sysclk_freq / 1000000);

	first = DMTIMER_READ4(sc, DMT_TCRR);

	while (counts > 0) {
		last = DMTIMER_READ4(sc, DMT_TCRR);
		if (last > first) {
			counts -= (int32_t)(last - first);
		} else {
			counts -= (int32_t)((0xFFFFFFFF - first) + last);
		}
		first = last;
	}
}
