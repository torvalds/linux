/*-
 * Copyright 2013-2015 Alexander Kabaev <kan@FreeBSD.org>
 * Copyright 2013-2015 John Wehle <john@feith.com>
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
#include <machine/hwfunc.h>

#include <dev/extres/clk/clk.h>

#include <dev/fdt/fdt_common.h>
#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

#include <mips/ingenic/jz4780_regs.h>

struct jz4780_timer_softc {
	device_t		dev;
	struct resource	*	res[4];
	void *			ih_cookie;
	struct eventtimer	et;
	struct timecounter	tc;
};

static struct resource_spec jz4780_timer_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },	/* OST */
	{ SYS_RES_IRQ,		1,	RF_ACTIVE },	/* TC5 */
	{ SYS_RES_IRQ,		2,	RF_ACTIVE },	/* TC0-4,6 */
	{ -1, 0 }
};

/*
 * devclass_get_device / device_get_softc could be used
 * to dynamically locate this, however the timers are a
 * required device which can't be unloaded so there's
 * no need for the overhead.
 */
static struct jz4780_timer_softc *jz4780_timer_sc = NULL;

#define	CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define	CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)

static unsigned
jz4780_get_timecount(struct timecounter *tc)
{
	struct jz4780_timer_softc *sc =
	    (struct jz4780_timer_softc *)tc->tc_priv;

	return CSR_READ_4(sc, JZ_OST_CNT_LO);
}

static int
jz4780_hardclock(void *arg)
{
	struct jz4780_timer_softc *sc = (struct jz4780_timer_softc *)arg;

	CSR_WRITE_4(sc, JZ_TC_TFCR, TFR_FFLAG5);
	CSR_WRITE_4(sc, JZ_TC_TECR, TESR_TCST5);

	if (sc->et.et_active)
		sc->et.et_event_cb(&sc->et, sc->et.et_arg);

	return (FILTER_HANDLED);
}

static int
jz4780_timer_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{
	struct jz4780_timer_softc *sc =
	    (struct jz4780_timer_softc *)et->et_priv;
	uint32_t ticks;

	ticks = (first * et->et_frequency) / SBT_1S;
	if (ticks == 0)
		return (EINVAL);

	CSR_WRITE_4(sc, JZ_TC_TDFR(5), ticks);
	CSR_WRITE_4(sc, JZ_TC_TCNT(5), 0);
	CSR_WRITE_4(sc, JZ_TC_TESR, TESR_TCST5);

	return (0);
}

static int
jz4780_timer_stop(struct eventtimer *et)
{
	struct jz4780_timer_softc *sc =
	    (struct jz4780_timer_softc *)et->et_priv;

	CSR_WRITE_4(sc, JZ_TC_TECR, TESR_TCST5);
	return (0);
}

static int
jz4780_timer_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "ingenic,jz4780-tcu"))
		return (ENXIO);

	device_set_desc(dev, "Ingenic JZ4780 timer");

	return (BUS_PROBE_DEFAULT);
}

static int
jz4780_timer_attach(device_t dev)
{
	struct jz4780_timer_softc *sc = device_get_softc(dev);
	pcell_t counter_freq;
	clk_t clk;

	/* There should be exactly one instance. */
	if (jz4780_timer_sc != NULL)
		return (ENXIO);

	sc->dev = dev;

	if (bus_alloc_resources(dev, jz4780_timer_spec, sc->res)) {
		device_printf(dev, "can not allocate resources for device\n");
		return (ENXIO);
	}

	counter_freq = 0;
	if (clk_get_by_name(dev, "ext", &clk) == 0) {
		uint64_t clk_freq;

		if (clk_get_freq(clk, &clk_freq) == 0)
			counter_freq = (uint32_t)clk_freq / 16;
		clk_release(clk);
	}
	if (counter_freq == 0) {
		device_printf(dev, "unable to determine ext clock frequency\n");
		/* Hardcode value we 'know' is correct */
		counter_freq = 48000000 / 16;
	}

	/*
	 * Disable the timers, select the input for each timer,
	 * clear and then start OST.
	 */

	/* Stop OST, if it happens to be running */
	CSR_WRITE_4(sc, JZ_TC_TECR, TESR_OST);
	/* Stop all other channels as well */
	CSR_WRITE_4(sc, JZ_TC_TECR, TESR_TCST0 | TESR_TCST1 | TESR_TCST2 |
	    TESR_TCST3 | TESR_TCST4 | TESR_TCST5 | TESR_TCST6 | TESR_TCST7);
	/* Clear detect mask flags */
	CSR_WRITE_4(sc, JZ_TC_TFCR, 0xFFFFFFFF);
	/* Mask all interrupts */
	CSR_WRITE_4(sc, JZ_TC_TMSR, 0xFFFFFFFF);

	/* Init counter with known data */
	CSR_WRITE_4(sc, JZ_OST_CTRL, 0);
	CSR_WRITE_4(sc, JZ_OST_CNT_LO, 0);
	CSR_WRITE_4(sc, JZ_OST_CNT_HI, 0);
	CSR_WRITE_4(sc, JZ_OST_DATA, 0xffffffff);

	/* Configure counter for external clock */
	CSR_WRITE_4(sc, JZ_OST_CTRL, OSTC_EXT_EN | OSTC_MODE | OSTC_DIV_16);

	/* Start the counter again */
	CSR_WRITE_4(sc, JZ_TC_TESR, TESR_OST);

	/* Configure TCU channel 5 similarly to OST and leave it disabled */
	CSR_WRITE_4(sc, JZ_TC_TCSR(5), TCSR_EXT_EN | TCSR_DIV_16);
	CSR_WRITE_4(sc, JZ_TC_TMCR, TMR_FMASK(5));

	if (bus_setup_intr(dev, sc->res[2], INTR_TYPE_CLK,
	    jz4780_hardclock, NULL, sc, &sc->ih_cookie)) {
		device_printf(dev, "could not setup interrupt handler\n");
		bus_release_resources(dev, jz4780_timer_spec, sc->res);
		return (ENXIO);
	}

	sc->et.et_name = "JZ4780 TCU5";
	sc->et.et_flags = ET_FLAGS_ONESHOT;
	sc->et.et_frequency = counter_freq;
	sc->et.et_quality = 1000;
	sc->et.et_min_period = (0x00000002LLU * SBT_1S) / sc->et.et_frequency;
	sc->et.et_max_period = (0x0000fffeLLU * SBT_1S) / sc->et.et_frequency;
	sc->et.et_start = jz4780_timer_start;
	sc->et.et_stop = jz4780_timer_stop;
	sc->et.et_priv = sc;

	et_register(&sc->et);

	sc->tc.tc_get_timecount = jz4780_get_timecount;
	sc->tc.tc_name = "JZ4780 OST";
	sc->tc.tc_frequency = counter_freq;
	sc->tc.tc_counter_mask = ~0u;
	sc->tc.tc_quality = 1000;
	sc->tc.tc_priv = sc;

	tc_init(&sc->tc);

	/* Now when tc is initialized, allow DELAY to find it */
	jz4780_timer_sc = sc;

	return (0);
}

static int
jz4780_timer_detach(device_t dev)
{

	return (EBUSY);
}

static device_method_t jz4780_timer_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		jz4780_timer_probe),
	DEVMETHOD(device_attach,	jz4780_timer_attach),
	DEVMETHOD(device_detach,	jz4780_timer_detach),

	DEVMETHOD_END
};

static driver_t jz4780_timer_driver = {
	"timer",
	jz4780_timer_methods,
	sizeof(struct jz4780_timer_softc),
};

static devclass_t jz4780_timer_devclass;

EARLY_DRIVER_MODULE(timer, simplebus, jz4780_timer_driver,
    jz4780_timer_devclass, 0, 0, BUS_PASS_TIMER);

void
DELAY(int usec)
{
	uint32_t counter;
	uint32_t delta, now, previous, remaining;

	/* Timer has not yet been initialized */
	if (jz4780_timer_sc == NULL) {
		for (; usec > 0; usec--)
			for (counter = 200; counter > 0; counter--) {
				/* Prevent gcc from optimizing out the loop */
				mips_rd_cause();
			}
		return;
	}
	TSENTER();

	/*
	 * Some of the other timers in the source tree do this calculation as:
	 *
	 *   usec * ((sc->tc.tc_frequency / 1000000) + 1)
	 *
	 * which gives a fairly pessimistic result when tc_frequency is an exact
	 * multiple of 1000000.  Given the data type and typical values for
	 * tc_frequency adding 999999 shouldn't overflow.
	 */
	remaining = usec * ((jz4780_timer_sc->tc.tc_frequency + 999999) /
	    1000000);

	/*
	 * We add one since the first iteration may catch the counter just
	 * as it is changing.
	 */
	remaining += 1;

	previous = jz4780_get_timecount(&jz4780_timer_sc->tc);

	for ( ; ; ) {
		now = jz4780_get_timecount(&jz4780_timer_sc->tc);

		/*
		 * If the timer has rolled over, then we have the case:
		 *
		 *   if (previous > now) {
		 *     delta = (0 - previous) + now
		 *   }
		 *
		 * which is really no different then the normal case.
		 * Both cases are simply:
		 *
		 *   delta = now - previous.
		 */
		delta = now - previous;

		if (delta >= remaining)
			break;

		previous = now;
		remaining -= delta;
	}
	TSEXIT();
}

void
platform_initclocks(void)
{

}

