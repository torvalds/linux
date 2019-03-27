/*-
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

/*
 * Amlogic aml8726 timer driver.
 *
 * 16 bit Timer A is used for the event timer / hard clock.
 * 32 bit Timer E is used for the timecounter / DELAY.
 *
 * The current implementation doesn't use Timers B-D.  Another approach is
 * to split the timers between the cores implementing per cpu event timers.
 *
 * The timers all share the MUX register which requires a mutex to serialize
 * access.  The mutex is also used to avoid potential problems between the
 * interrupt handler and timer_start / timer_stop.
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

#include <dev/ofw/ofw_bus.h>
#include <dev/ofw/ofw_bus_subr.h>

struct aml8726_timer_softc {
	device_t		dev;
	struct resource	*	res[2];
	struct mtx		mtx;
	void *			ih_cookie;
	struct eventtimer	et;
	uint32_t		first_ticks;
	uint32_t		period_ticks;
	struct timecounter	tc;
};

static struct resource_spec aml8726_timer_spec[] = {
	{ SYS_RES_MEMORY,	0,	RF_ACTIVE },
	{ SYS_RES_IRQ,		0,	RF_ACTIVE },	/* INT_TIMER_A */
	{ -1, 0 }
};

/*
 * devclass_get_device / device_get_softc could be used
 * to dynamically locate this, however the timers are a
 * required device which can't be unloaded so there's
 * no need for the overhead.
 */
static struct aml8726_timer_softc *aml8726_timer_sc = NULL;

#define	AML_TIMER_LOCK(sc)		mtx_lock_spin(&(sc)->mtx)
#define	AML_TIMER_UNLOCK(sc)		mtx_unlock_spin(&(sc)->mtx)
#define	AML_TIMER_LOCK_INIT(sc)		\
    mtx_init(&(sc)->mtx, device_get_nameunit((sc)->dev),	\
    "timer", MTX_SPIN)
#define	AML_TIMER_LOCK_DESTROY(sc)	mtx_destroy(&(sc)->mtx);

#define	AML_TIMER_MUX_REG		0
#define	AML_TIMER_INPUT_1us		0
#define	AML_TIMER_INPUT_10us		1
#define	AML_TIMER_INPUT_100us		2
#define	AML_TIMER_INPUT_1ms		3
#define	AML_TIMER_INPUT_MASK		3
#define	AML_TIMER_A_INPUT_MASK		3
#define	AML_TIMER_A_INPUT_SHIFT		0
#define	AML_TIMER_B_INPUT_MASK		(3 << 2)
#define	AML_TIMER_B_INPUT_SHIFT		2
#define	AML_TIMER_C_INPUT_MASK		(3 << 4)
#define	AML_TIMER_C_INPUT_SHIFT		4
#define	AML_TIMER_D_INPUT_MASK		(3 << 6)
#define	AML_TIMER_D_INPUT_SHIFT		6
#define	AML_TIMER_E_INPUT_SYS		0
#define	AML_TIMER_E_INPUT_1us		1
#define	AML_TIMER_E_INPUT_10us		2
#define	AML_TIMER_E_INPUT_100us		3
#define	AML_TIMER_E_INPUT_1ms		4
#define	AML_TIMER_E_INPUT_MASK		(7 << 8)
#define	AML_TIMER_E_INPUT_SHIFT		8
#define	AML_TIMER_A_PERIODIC		(1 << 12)
#define	AML_TIMER_B_PERIODIC		(1 << 13)
#define	AML_TIMER_C_PERIODIC		(1 << 14)
#define	AML_TIMER_D_PERIODIC		(1 << 15)
#define	AML_TIMER_A_EN			(1 << 16)
#define	AML_TIMER_B_EN			(1 << 17)
#define	AML_TIMER_C_EN			(1 << 18)
#define	AML_TIMER_D_EN			(1 << 19)
#define	AML_TIMER_E_EN			(1 << 20)
#define	AML_TIMER_A_REG			4
#define	AML_TIMER_B_REG			8
#define	AML_TIMER_C_REG			12
#define	AML_TIMER_D_REG			16
#define	AML_TIMER_E_REG			20

#define	CSR_WRITE_4(sc, reg, val)	bus_write_4((sc)->res[0], reg, (val))
#define	CSR_READ_4(sc, reg)		bus_read_4((sc)->res[0], reg)

static unsigned
aml8726_get_timecount(struct timecounter *tc)
{
	struct aml8726_timer_softc *sc =
	    (struct aml8726_timer_softc *)tc->tc_priv;

	return CSR_READ_4(sc, AML_TIMER_E_REG);
}

static int
aml8726_hardclock(void *arg)
{
	struct aml8726_timer_softc *sc = (struct aml8726_timer_softc *)arg;

	AML_TIMER_LOCK(sc);

	if (sc->first_ticks != 0 && sc->period_ticks != 0) {
		sc->first_ticks = 0;

		CSR_WRITE_4(sc, AML_TIMER_A_REG, sc->period_ticks);
		CSR_WRITE_4(sc, AML_TIMER_MUX_REG,
		    (CSR_READ_4(sc, AML_TIMER_MUX_REG) |
		    AML_TIMER_A_PERIODIC | AML_TIMER_A_EN));
	}

	AML_TIMER_UNLOCK(sc);

	if (sc->et.et_active)
		sc->et.et_event_cb(&sc->et, sc->et.et_arg);

	return (FILTER_HANDLED);
}

static int
aml8726_timer_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{
	struct aml8726_timer_softc *sc =
	    (struct aml8726_timer_softc *)et->et_priv;
	uint32_t first_ticks;
	uint32_t period_ticks;
	uint32_t periodic;
	uint32_t ticks;

	first_ticks = (first * et->et_frequency) / SBT_1S;
	period_ticks = (period * et->et_frequency) / SBT_1S;

	if (first_ticks != 0) {
		ticks = first_ticks;
		periodic = 0;

	} else {
		ticks = period_ticks;
		periodic = AML_TIMER_A_PERIODIC;
	}

	if (ticks == 0)
		return (EINVAL);

	AML_TIMER_LOCK(sc);

	sc->first_ticks = first_ticks;
	sc->period_ticks = period_ticks;

	CSR_WRITE_4(sc, AML_TIMER_A_REG, ticks);
	CSR_WRITE_4(sc, AML_TIMER_MUX_REG,
	    ((CSR_READ_4(sc, AML_TIMER_MUX_REG) & ~AML_TIMER_A_PERIODIC) |
	    AML_TIMER_A_EN | periodic));

	AML_TIMER_UNLOCK(sc);

	return (0);
}

static int
aml8726_timer_stop(struct eventtimer *et)
{
	struct aml8726_timer_softc *sc =
	    (struct aml8726_timer_softc *)et->et_priv;

	AML_TIMER_LOCK(sc);

	CSR_WRITE_4(sc, AML_TIMER_MUX_REG,
	    (CSR_READ_4(sc, AML_TIMER_MUX_REG) & ~AML_TIMER_A_EN));

	AML_TIMER_UNLOCK(sc);

	return (0);
}

static int
aml8726_timer_probe(device_t dev)
{

	if (!ofw_bus_status_okay(dev))
		return (ENXIO);

	if (!ofw_bus_is_compatible(dev, "amlogic,meson6-timer"))
		return (ENXIO);

	device_set_desc(dev, "Amlogic aml8726 timer");

	return (BUS_PROBE_DEFAULT);
}

static int
aml8726_timer_attach(device_t dev)
{
	struct aml8726_timer_softc *sc = device_get_softc(dev);

	/* There should be exactly one instance. */
	if (aml8726_timer_sc != NULL)
		return (ENXIO);

	sc->dev = dev;

	if (bus_alloc_resources(dev, aml8726_timer_spec, sc->res)) {
		device_printf(dev, "can not allocate resources for device\n");
		return (ENXIO);
	}

	/*
	 * Disable the timers, select the input for each timer,
	 * clear timer E, and then enable timer E.
	 */
	CSR_WRITE_4(sc, AML_TIMER_MUX_REG,
	    ((CSR_READ_4(sc, AML_TIMER_MUX_REG) &
	    ~(AML_TIMER_A_EN | AML_TIMER_A_INPUT_MASK |
	    AML_TIMER_E_EN | AML_TIMER_E_INPUT_MASK)) |
	    (AML_TIMER_INPUT_1us << AML_TIMER_A_INPUT_SHIFT) |
	    (AML_TIMER_E_INPUT_1us << AML_TIMER_E_INPUT_SHIFT)));

	CSR_WRITE_4(sc, AML_TIMER_E_REG, 0);

	CSR_WRITE_4(sc, AML_TIMER_MUX_REG,
	    (CSR_READ_4(sc, AML_TIMER_MUX_REG) | AML_TIMER_E_EN));

	/*
	 * Initialize the mutex prior to installing the interrupt handler
	 * in case of a spurious interrupt.
	 */
	AML_TIMER_LOCK_INIT(sc);

	if (bus_setup_intr(dev, sc->res[1], INTR_TYPE_CLK,
	    aml8726_hardclock, NULL, sc, &sc->ih_cookie)) {
		device_printf(dev, "could not setup interrupt handler\n");
		bus_release_resources(dev, aml8726_timer_spec, sc->res);
		AML_TIMER_LOCK_DESTROY(sc);
		return (ENXIO);
	}

	aml8726_timer_sc = sc;

	sc->et.et_name = "aml8726 timer A";
	sc->et.et_flags = ET_FLAGS_PERIODIC | ET_FLAGS_ONESHOT;
	sc->et.et_frequency = 1000000;
	sc->et.et_quality = 1000;
	sc->et.et_min_period = (0x00000002LLU * SBT_1S) / sc->et.et_frequency;
	sc->et.et_max_period = (0x0000fffeLLU * SBT_1S) / sc->et.et_frequency;
	sc->et.et_start = aml8726_timer_start;
	sc->et.et_stop = aml8726_timer_stop;
	sc->et.et_priv = sc;

	et_register(&sc->et);

	sc->tc.tc_get_timecount = aml8726_get_timecount;
	sc->tc.tc_name = "aml8726 timer E";
	sc->tc.tc_frequency = 1000000;
	sc->tc.tc_counter_mask = ~0u;
	sc->tc.tc_quality = 1000;
	sc->tc.tc_priv = sc;

	tc_init(&sc->tc);

	return (0);
}

static int
aml8726_timer_detach(device_t dev)
{

	return (EBUSY);
}

static device_method_t aml8726_timer_methods[] = {
	/* Device interface */
	DEVMETHOD(device_probe,		aml8726_timer_probe),
	DEVMETHOD(device_attach,	aml8726_timer_attach),
	DEVMETHOD(device_detach,	aml8726_timer_detach),

	DEVMETHOD_END
};

static driver_t aml8726_timer_driver = {
	"timer",
	aml8726_timer_methods,
	sizeof(struct aml8726_timer_softc),
};

static devclass_t aml8726_timer_devclass;

EARLY_DRIVER_MODULE(timer, simplebus, aml8726_timer_driver,
    aml8726_timer_devclass, 0, 0, BUS_PASS_TIMER);

void
DELAY(int usec)
{
	uint32_t counter;
	uint32_t delta, now, previous, remaining;

	/* Timer has not yet been initialized */
	if (aml8726_timer_sc == NULL) {
		for (; usec > 0; usec--)
			for (counter = 200; counter > 0; counter--) {
				/* Prevent gcc from optimizing out the loop */
				cpufunc_nullop();
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
	remaining = usec * ((aml8726_timer_sc->tc.tc_frequency + 999999) /
	    1000000);

	/*
	 * We add one since the first iteration may catch the counter just
	 * as it is changing.
	 */
	remaining += 1;

	previous = aml8726_get_timecount(&aml8726_timer_sc->tc);

	for ( ; ; ) {
		now = aml8726_get_timecount(&aml8726_timer_sc->tc);

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
