/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2012 Oleksandr Tymoshenko <gonzo@freebsd.org>
 * Copyright (c) 2012 Damjan Marion <dmarion@freebsd.org>
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
#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/machdep.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <machine/bus.h>

#define	BCM2835_NUM_TIMERS	4

#define	DEFAULT_TIMER		3
#define	DEFAULT_TIMER_NAME	"BCM2835-3"
#define	DEFAULT_FREQUENCY	1000000
#define	MIN_PERIOD		5LLU

#define	SYSTIMER_CS	0x00
#define	SYSTIMER_CLO	0x04
#define	SYSTIMER_CHI	0x08
#define	SYSTIMER_C0	0x0C
#define	SYSTIMER_C1	0x10
#define	SYSTIMER_C2	0x14
#define	SYSTIMER_C3	0x18

struct systimer {
	int			index;
	bool			enabled;
	struct eventtimer	et;
};

struct bcm_systimer_softc {
	struct resource*	mem_res;
	struct resource*	irq_res[BCM2835_NUM_TIMERS];
	void*			intr_hl[BCM2835_NUM_TIMERS];
	uint32_t		sysclk_freq;
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	struct systimer		st[BCM2835_NUM_TIMERS];
};

static struct resource_spec bcm_systimer_irq_spec[] = {
	{ SYS_RES_IRQ,      0,  RF_ACTIVE },
	{ SYS_RES_IRQ,      1,  RF_ACTIVE },
	{ SYS_RES_IRQ,      2,  RF_ACTIVE },
	{ SYS_RES_IRQ,      3,  RF_ACTIVE },
	{ -1,               0,  0 }
};

static struct ofw_compat_data compat_data[] = {
	{"broadcom,bcm2835-system-timer",	1},
	{"brcm,bcm2835-system-timer",		1},
	{NULL,					0}
};

static struct bcm_systimer_softc *bcm_systimer_sc = NULL;

/* Read/Write macros for Timer used as timecounter */
#define bcm_systimer_tc_read_4(reg)		\
	bus_space_read_4(bcm_systimer_sc->bst, \
		bcm_systimer_sc->bsh, reg)

#define bcm_systimer_tc_write_4(reg, val)	\
	bus_space_write_4(bcm_systimer_sc->bst, \
		bcm_systimer_sc->bsh, reg, val)

static unsigned bcm_systimer_tc_get_timecount(struct timecounter *);

static delay_func bcm_systimer_delay;

static struct timecounter bcm_systimer_tc = {
	.tc_name           = DEFAULT_TIMER_NAME,
	.tc_get_timecount  = bcm_systimer_tc_get_timecount,
	.tc_poll_pps       = NULL,
	.tc_counter_mask   = ~0u,
	.tc_frequency      = 0,
	.tc_quality        = 1000,
};

static unsigned
bcm_systimer_tc_get_timecount(struct timecounter *tc)
{
	if (bcm_systimer_sc == NULL)
		return (0);

	return bcm_systimer_tc_read_4(SYSTIMER_CLO);
}

static int
bcm_systimer_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{
	struct systimer *st = et->et_priv;
	uint32_t clo, clo1;
	uint32_t count;
	register_t s;

	if (first != 0) {

		count = ((uint32_t)et->et_frequency * first) >> 32;

		s = intr_disable();
		clo = bcm_systimer_tc_read_4(SYSTIMER_CLO);
restart:
		clo += count;
		/*
		 * Clear pending interrupts
		 */
		bcm_systimer_tc_write_4(SYSTIMER_CS, (1 << st->index));
		bcm_systimer_tc_write_4(SYSTIMER_C0 + st->index*4, clo);
		clo1 = bcm_systimer_tc_read_4(SYSTIMER_CLO);
		if ((int32_t)(clo1 - clo) >= 0) {
			count *= 2;
			clo = clo1;
			goto restart;
		}
		st->enabled = 1;
		intr_restore(s);

		return (0);
	}

	return (EINVAL);
}

static int
bcm_systimer_stop(struct eventtimer *et)
{
	struct systimer *st = et->et_priv;
	st->enabled = 0;

	return (0);
}

static int
bcm_systimer_intr(void *arg)
{
	struct systimer *st = (struct systimer *)arg;
	uint32_t cs;

	cs = bcm_systimer_tc_read_4(SYSTIMER_CS);
	if ((cs & (1 << st->index)) == 0)
		return (FILTER_STRAY);

	/* ACK interrupt */
	bcm_systimer_tc_write_4(SYSTIMER_CS, (1 << st->index));
	if (st->enabled) {
		if (st->et.et_active) {
			st->et.et_event_cb(&st->et, st->et.et_arg);
		}
	}

	return (FILTER_HANDLED);
}

static int
bcm_systimer_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_search_compatible(dev, compat_data)->ocd_data == 0)
		return (ENXIO);

	return (BUS_PROBE_DEFAULT);
}

static int
bcm_systimer_attach(device_t dev)
{
	struct bcm_systimer_softc *sc = device_get_softc(dev);
	int err;
	int rid = 0;

	if (bcm_systimer_sc != NULL)
		return (EINVAL);

	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	sc->bst = rman_get_bustag(sc->mem_res);
	sc->bsh = rman_get_bushandle(sc->mem_res);

	/* Request the IRQ resources */
	err = bus_alloc_resources(dev, bcm_systimer_irq_spec,
		sc->irq_res);
	if (err) {
		device_printf(dev, "Error: could not allocate irq resources\n");
		return (ENXIO);
	}

	/* TODO: get frequency from FDT */
	sc->sysclk_freq = DEFAULT_FREQUENCY;

	/* Setup and enable the timer */
	if (bus_setup_intr(dev, sc->irq_res[DEFAULT_TIMER], INTR_TYPE_CLK,
			bcm_systimer_intr, NULL, &sc->st[DEFAULT_TIMER],
			&sc->intr_hl[DEFAULT_TIMER]) != 0) {
		bus_release_resources(dev, bcm_systimer_irq_spec,
			sc->irq_res);
		device_printf(dev, "Unable to setup the clock irq handler.\n");
		return (ENXIO);
	}

	sc->st[DEFAULT_TIMER].index = DEFAULT_TIMER;
	sc->st[DEFAULT_TIMER].enabled = 0;
	sc->st[DEFAULT_TIMER].et.et_name = DEFAULT_TIMER_NAME;
	sc->st[DEFAULT_TIMER].et.et_flags = ET_FLAGS_ONESHOT;
	sc->st[DEFAULT_TIMER].et.et_quality = 1000;
	sc->st[DEFAULT_TIMER].et.et_frequency = sc->sysclk_freq;
	sc->st[DEFAULT_TIMER].et.et_min_period =
	    (MIN_PERIOD << 32) / sc->st[DEFAULT_TIMER].et.et_frequency + 1;
	sc->st[DEFAULT_TIMER].et.et_max_period =
	    (0x7ffffffeLLU << 32) / sc->st[DEFAULT_TIMER].et.et_frequency;
	sc->st[DEFAULT_TIMER].et.et_start = bcm_systimer_start;
	sc->st[DEFAULT_TIMER].et.et_stop = bcm_systimer_stop;
	sc->st[DEFAULT_TIMER].et.et_priv = &sc->st[DEFAULT_TIMER];
	et_register(&sc->st[DEFAULT_TIMER].et);

	bcm_systimer_sc = sc;

	if (device_get_unit(dev) == 0)
		arm_set_delay(bcm_systimer_delay, sc);

	bcm_systimer_tc.tc_frequency = DEFAULT_FREQUENCY;
	tc_init(&bcm_systimer_tc);

	return (0);
}

static device_method_t bcm_systimer_methods[] = {
	DEVMETHOD(device_probe,		bcm_systimer_probe),
	DEVMETHOD(device_attach,	bcm_systimer_attach),
	{ 0, 0 }
};

static driver_t bcm_systimer_driver = {
	"systimer",
	bcm_systimer_methods,
	sizeof(struct bcm_systimer_softc),
};

static devclass_t bcm_systimer_devclass;

DRIVER_MODULE(bcm_systimer, simplebus, bcm_systimer_driver, bcm_systimer_devclass, 0, 0);

static void
bcm_systimer_delay(int usec, void *arg)
{
	struct bcm_systimer_softc *sc;
	int32_t counts;
	uint32_t first, last;

	sc = (struct bcm_systimer_softc *) arg;

	/* Get the number of times to count */
	counts = usec * (bcm_systimer_tc.tc_frequency / 1000000) + 1;

	first = bcm_systimer_tc_read_4(SYSTIMER_CLO);

	while (counts > 0) {
		last = bcm_systimer_tc_read_4(SYSTIMER_CLO);
		if (last == first)
			continue;
		if (last>first) {
			counts -= (int32_t)(last - first);
		} else {
			counts -= (int32_t)((0xFFFFFFFF - first) + last);
		}
		first = last;
	}
}
