/*-
 * Copyright (c) 2015-2017 Ruslan Bukin <br@bsdpad.com>
 * All rights reserved.
 *
 * Portions of this software were developed by SRI International and the
 * University of Cambridge Computer Laboratory under DARPA/AFRL contract
 * FA8750-10-C-0237 ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Portions of this software were developed by the University of Cambridge
 * Computer Laboratory as part of the CTSRD Project, with support from the
 * UK Higher Education Innovation Fund (HEIF).
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

/*
 * RISC-V Timer
 */

#include "opt_platform.h"

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

#include <sys/proc.h>

#include <machine/bus.h>
#include <machine/cpu.h>
#include <machine/intr.h>
#include <machine/asm.h>
#include <machine/trap.h>
#include <machine/sbi.h>

#define	DEFAULT_FREQ	10000000

#define	TIMER_COUNTS		0x00
#define	TIMER_MTIMECMP(cpu)	(cpu * 8)

struct riscv_timer_softc {
	void			*ih;
	uint32_t		clkfreq;
	struct eventtimer	et;
};

static struct riscv_timer_softc *riscv_timer_sc = NULL;

static timecounter_get_t riscv_timer_get_timecount;

static struct timecounter riscv_timer_timecount = {
	.tc_name           = "RISC-V Timecounter",
	.tc_get_timecount  = riscv_timer_get_timecount,
	.tc_poll_pps       = NULL,
	.tc_counter_mask   = ~0u,
	.tc_frequency      = 0,
	.tc_quality        = 1000,
};

static inline uint64_t
get_cycles(void)
{
	uint64_t cycles;

	__asm __volatile("rdtime %0" : "=r" (cycles));

	return (cycles);
}

static long
get_counts(struct riscv_timer_softc *sc)
{
	uint64_t counts;

	counts = get_cycles();

	return (counts);
}

static unsigned
riscv_timer_get_timecount(struct timecounter *tc)
{
	struct riscv_timer_softc *sc;

	sc = tc->tc_priv;

	return (get_counts(sc));
}

static int
riscv_timer_start(struct eventtimer *et, sbintime_t first, sbintime_t period)
{
	uint64_t counts;

	if (first != 0) {
		counts = ((uint32_t)et->et_frequency * first) >> 32;
		sbi_set_timer(get_cycles() + counts);
		csr_set(sie, SIE_STIE);

		return (0);
	}

	return (EINVAL);

}

static int
riscv_timer_stop(struct eventtimer *et)
{

	/* TODO */

	return (0);
}

static int
riscv_timer_intr(void *arg)
{
	struct riscv_timer_softc *sc;

	sc = (struct riscv_timer_softc *)arg;

	csr_clear(sip, SIP_STIP);

	if (sc->et.et_active)
		sc->et.et_event_cb(&sc->et, sc->et.et_arg);

	return (FILTER_HANDLED);
}

static int
riscv_timer_probe(device_t dev)
{

	device_set_desc(dev, "RISC-V Timer");

	return (BUS_PROBE_DEFAULT);
}

static int
riscv_timer_attach(device_t dev)
{
	struct riscv_timer_softc *sc;
	int error;

	sc = device_get_softc(dev);
	if (riscv_timer_sc)
		return (ENXIO);

	if (device_get_unit(dev) != 0)
		return ENXIO;

	sc->clkfreq = DEFAULT_FREQ;
	if (sc->clkfreq == 0) {
		device_printf(dev, "No clock frequency specified\n");
		return (ENXIO);
	}

	riscv_timer_sc = sc;

	/* Setup IRQs handler */
	error = riscv_setup_intr(device_get_nameunit(dev), riscv_timer_intr,
	    NULL, sc, IRQ_TIMER_SUPERVISOR, INTR_TYPE_CLK, &sc->ih);
	if (error) {
		device_printf(dev, "Unable to alloc int resource.\n");
		return (ENXIO);
	}

	riscv_timer_timecount.tc_frequency = sc->clkfreq;
	riscv_timer_timecount.tc_priv = sc;
	tc_init(&riscv_timer_timecount);

	sc->et.et_name = "RISC-V Eventtimer";
	sc->et.et_flags = ET_FLAGS_ONESHOT | ET_FLAGS_PERCPU;
	sc->et.et_quality = 1000;

	sc->et.et_frequency = sc->clkfreq;
	sc->et.et_min_period = (0x00000002LLU << 32) / sc->et.et_frequency;
	sc->et.et_max_period = (0xfffffffeLLU << 32) / sc->et.et_frequency;
	sc->et.et_start = riscv_timer_start;
	sc->et.et_stop = riscv_timer_stop;
	sc->et.et_priv = sc;
	et_register(&sc->et);

	return (0);
}

static device_method_t riscv_timer_methods[] = {
	DEVMETHOD(device_probe,		riscv_timer_probe),
	DEVMETHOD(device_attach,	riscv_timer_attach),
	{ 0, 0 }
};

static driver_t riscv_timer_driver = {
	"timer",
	riscv_timer_methods,
	sizeof(struct riscv_timer_softc),
};

static devclass_t riscv_timer_devclass;

EARLY_DRIVER_MODULE(timer, nexus, riscv_timer_driver, riscv_timer_devclass,
    0, 0, BUS_PASS_TIMER + BUS_PASS_ORDER_MIDDLE);

void
DELAY(int usec)
{
	int64_t counts, counts_per_usec;
	uint64_t first, last;

	/*
	 * Check the timers are setup, if not just
	 * use a for loop for the meantime
	 */
	if (riscv_timer_sc == NULL) {
		for (; usec > 0; usec--)
			for (counts = 200; counts > 0; counts--)
				/*
				 * Prevent the compiler from optimizing
				 * out the loop
				 */
				cpufunc_nullop();
		return;
	}
	TSENTER();

	/* Get the number of times to count */
	counts_per_usec = ((riscv_timer_timecount.tc_frequency / 1000000) + 1);

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

	first = get_counts(riscv_timer_sc);

	while (counts > 0) {
		last = get_counts(riscv_timer_sc);
		counts -= (int64_t)(last - first);
		first = last;
	}
	TSEXIT();
}
