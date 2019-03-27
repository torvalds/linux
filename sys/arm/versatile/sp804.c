/*-
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

#define	SP804_TIMER1_LOAD	0x00
#define	SP804_TIMER1_VALUE	0x04
#define	SP804_TIMER1_CONTROL	0x08
#define		TIMER_CONTROL_EN	(1 << 7)
#define		TIMER_CONTROL_FREERUN	(0 << 6)
#define		TIMER_CONTROL_PERIODIC	(1 << 6)
#define		TIMER_CONTROL_INTREN	(1 << 5)
#define		TIMER_CONTROL_DIV1	(0 << 2)
#define		TIMER_CONTROL_DIV16	(1 << 2)
#define		TIMER_CONTROL_DIV256	(2 << 2)
#define		TIMER_CONTROL_32BIT	(1 << 1)
#define		TIMER_CONTROL_ONESHOT	(1 << 0)
#define	SP804_TIMER1_INTCLR	0x0C
#define	SP804_TIMER1_RIS	0x10
#define	SP804_TIMER1_MIS	0x14
#define	SP804_TIMER1_BGLOAD	0x18
#define	SP804_TIMER2_LOAD	0x20
#define	SP804_TIMER2_VALUE	0x24
#define	SP804_TIMER2_CONTROL	0x28
#define	SP804_TIMER2_INTCLR	0x2C
#define	SP804_TIMER2_RIS	0x30
#define	SP804_TIMER2_MIS	0x34
#define	SP804_TIMER2_BGLOAD	0x38

#define	SP804_PERIPH_ID0	0xFE0
#define	SP804_PERIPH_ID1	0xFE4
#define	SP804_PERIPH_ID2	0xFE8
#define	SP804_PERIPH_ID3	0xFEC
#define	SP804_PRIMECELL_ID0	0xFF0
#define	SP804_PRIMECELL_ID1	0xFF4
#define	SP804_PRIMECELL_ID2	0xFF8
#define	SP804_PRIMECELL_ID3	0xFFC

#define	DEFAULT_FREQUENCY	1000000
/*
 * QEMU seems to have problem with full frequency
 */
#define	DEFAULT_DIVISOR		16
#define	DEFAULT_CONTROL_DIV	TIMER_CONTROL_DIV16

struct sp804_timer_softc {
	struct resource*	mem_res;
	struct resource*	irq_res;
	void*			intr_hl;
	uint32_t		sysclk_freq;
	bus_space_tag_t		bst;
	bus_space_handle_t	bsh;
	struct timecounter	tc;
	bool			et_enabled;
	struct eventtimer	et;
	int			timer_initialized;
};

/* Read/Write macros for Timer used as timecounter */
#define sp804_timer_tc_read_4(reg)		\
	bus_space_read_4(sc->bst, sc->bsh, reg)

#define sp804_timer_tc_write_4(reg, val)	\
	bus_space_write_4(sc->bst, sc->bsh, reg, val)

static unsigned sp804_timer_tc_get_timecount(struct timecounter *);
static void sp804_timer_delay(int, void *);

static unsigned
sp804_timer_tc_get_timecount(struct timecounter *tc)
{
	struct sp804_timer_softc *sc = tc->tc_priv;
	return 0xffffffff - sp804_timer_tc_read_4(SP804_TIMER1_VALUE);
}

static int
sp804_timer_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{
	struct sp804_timer_softc *sc = et->et_priv;
	uint32_t count, reg;

	if (first != 0) {
		sc->et_enabled = 1;

		count = ((uint32_t)et->et_frequency * first) >> 32;

		sp804_timer_tc_write_4(SP804_TIMER2_LOAD, count);
		reg = TIMER_CONTROL_32BIT | TIMER_CONTROL_INTREN |
		    TIMER_CONTROL_PERIODIC | DEFAULT_CONTROL_DIV |
		    TIMER_CONTROL_EN;
		sp804_timer_tc_write_4(SP804_TIMER2_CONTROL, reg);

		return (0);
	} 

	if (period != 0) {
		panic("period");
	}

	return (EINVAL);
}

static int
sp804_timer_stop(struct eventtimer *et)
{
	struct sp804_timer_softc *sc = et->et_priv;
	uint32_t reg;

	sc->et_enabled = 0;
	reg = sp804_timer_tc_read_4(SP804_TIMER2_CONTROL);
	reg &= ~(TIMER_CONTROL_EN);
	sp804_timer_tc_write_4(SP804_TIMER2_CONTROL, reg);

	return (0);
}

static int
sp804_timer_intr(void *arg)
{
	struct sp804_timer_softc *sc = arg;
	static uint32_t prev = 0;
	uint32_t x = 0;

	x = sp804_timer_tc_read_4(SP804_TIMER1_VALUE);

	prev =x ;
	sp804_timer_tc_write_4(SP804_TIMER2_INTCLR, 1);
	if (sc->et_enabled) {
		if (sc->et.et_active) {
			sc->et.et_event_cb(&sc->et, sc->et.et_arg);
		}
	}

	return (FILTER_HANDLED);
}

static int
sp804_timer_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (ofw_bus_is_compatible(dev, "arm,sp804")) {
		device_set_desc(dev, "SP804 System Timer");
		return (BUS_PROBE_DEFAULT);
	}

	return (ENXIO);
}

static int
sp804_timer_attach(device_t dev)
{
	struct sp804_timer_softc *sc = device_get_softc(dev);
	int rid = 0;
	int i;
	uint32_t id, reg;
	phandle_t node;
	pcell_t clock;

	sc->mem_res = bus_alloc_resource_any(dev, SYS_RES_MEMORY, &rid, RF_ACTIVE);
	if (sc->mem_res == NULL) {
		device_printf(dev, "could not allocate memory resource\n");
		return (ENXIO);
	}

	sc->bst = rman_get_bustag(sc->mem_res);
	sc->bsh = rman_get_bushandle(sc->mem_res);

	/* Request the IRQ resources */
	sc->irq_res =  bus_alloc_resource_any(dev, SYS_RES_IRQ, &rid, RF_ACTIVE);
	if (sc->irq_res == NULL) {
		device_printf(dev, "Error: could not allocate irq resources\n");
		return (ENXIO);
	}

	sc->sysclk_freq = DEFAULT_FREQUENCY;
	/* Get the base clock frequency */
	node = ofw_bus_get_node(dev);
	if ((OF_getencprop(node, "clock-frequency", &clock, sizeof(clock))) > 0) {
		sc->sysclk_freq = clock;
	}

	/* Setup and enable the timer */
	if (bus_setup_intr(dev, sc->irq_res, INTR_TYPE_CLK,
			sp804_timer_intr, NULL, sc,
			&sc->intr_hl) != 0) {
		bus_release_resource(dev, SYS_RES_IRQ, rid,
			sc->irq_res);
		device_printf(dev, "Unable to setup the clock irq handler.\n");
		return (ENXIO);
	}

	sp804_timer_tc_write_4(SP804_TIMER1_CONTROL, 0);
	sp804_timer_tc_write_4(SP804_TIMER2_CONTROL, 0);

	/*
	 * Timer 1, timecounter
	 */
	sc->tc.tc_frequency = sc->sysclk_freq;
	sc->tc.tc_name = "SP804-1";
	sc->tc.tc_get_timecount = sp804_timer_tc_get_timecount;
	sc->tc.tc_poll_pps = NULL;
	sc->tc.tc_counter_mask = ~0u;
	sc->tc.tc_quality = 1000;
	sc->tc.tc_priv = sc;

	sp804_timer_tc_write_4(SP804_TIMER1_VALUE, 0xffffffff);
	sp804_timer_tc_write_4(SP804_TIMER1_LOAD, 0xffffffff);
	reg = TIMER_CONTROL_PERIODIC | TIMER_CONTROL_32BIT;
	sp804_timer_tc_write_4(SP804_TIMER1_CONTROL, reg);
	reg |= TIMER_CONTROL_EN;
	sp804_timer_tc_write_4(SP804_TIMER1_CONTROL, reg);
	tc_init(&sc->tc);

	/* 
	 * Timer 2, event timer
	 */
	sc->et_enabled = 0;
	sc->et.et_name = "SP804-2";
	sc->et.et_flags = ET_FLAGS_PERIODIC | ET_FLAGS_ONESHOT;
	sc->et.et_quality = 1000;
	sc->et.et_frequency = sc->sysclk_freq / DEFAULT_DIVISOR;
	sc->et.et_min_period = (0x00000002LLU << 32) / sc->et.et_frequency;
	sc->et.et_max_period = (0xfffffffeLLU << 32) / sc->et.et_frequency;
	sc->et.et_start = sp804_timer_start;
	sc->et.et_stop = sp804_timer_stop;
	sc->et.et_priv = sc;
	et_register(&sc->et);

	id = 0;
	for (i = 3; i >= 0; i--) {
		id = (id << 8) | 
		     (sp804_timer_tc_read_4(SP804_PERIPH_ID0 + i*4) & 0xff);
	}

	device_printf(dev, "peripheral ID: %08x\n", id);

	id = 0;
	for (i = 3; i >= 0; i--) {
		id = (id << 8) | 
		     (sp804_timer_tc_read_4(SP804_PRIMECELL_ID0 + i*4) & 0xff);
	}

	arm_set_delay(sp804_timer_delay, sc);

	device_printf(dev, "PrimeCell ID: %08x\n", id);

	sc->timer_initialized = 1;

	return (0);
}

static device_method_t sp804_timer_methods[] = {
	DEVMETHOD(device_probe,		sp804_timer_probe),
	DEVMETHOD(device_attach,	sp804_timer_attach),
	{ 0, 0 }
};

static driver_t sp804_timer_driver = {
	"timer",
	sp804_timer_methods,
	sizeof(struct sp804_timer_softc),
};

static devclass_t sp804_timer_devclass;

DRIVER_MODULE(sp804_timer, simplebus, sp804_timer_driver, sp804_timer_devclass, 0, 0);

static void
sp804_timer_delay(int usec, void *arg)
{
	struct sp804_timer_softc *sc = arg;
	int32_t counts;
	uint32_t first, last;

	/* Get the number of times to count */
	counts = usec * ((sc->tc.tc_frequency / 1000000) + 1);

	first = sp804_timer_tc_get_timecount(&sc->tc);

	while (counts > 0) {
		last = sp804_timer_tc_get_timecount(&sc->tc);
		if (last == first)
			continue;
		if (last > first) {
			counts -= (int32_t)(last - first);
		} else {
			counts -= (int32_t)((0xFFFFFFFF - first) + last);
		}
		first = last;
	}
}
