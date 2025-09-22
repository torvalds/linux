/* $OpenBSD: amptimer.c,v 1.20 2023/09/17 14:50:51 cheloha Exp $ */
/*
 * Copyright (c) 2011 Dale Rahn <drahn@openbsd.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/kernel.h>
#include <sys/stdint.h>
#include <sys/timetc.h>

#include <arm/cpufunc.h>
#include <machine/bus.h>
#include <machine/intr.h>
#include <arm/cortex/cortex.h>

/* offset from periphbase */
#define GTIMER_ADDR	0x200
#define GTIMER_SIZE	0x100

/* registers */
#define GTIMER_CNT_LOW		0x00
#define GTIMER_CNT_HIGH		0x04
#define GTIMER_CTRL		0x08
#define GTIMER_CTRL_AA		(1 << 3)
#define GTIMER_CTRL_IRQ		(1 << 2)
#define GTIMER_CTRL_COMP	(1 << 1)
#define GTIMER_CTRL_TIMER	(1 << 0)
#define GTIMER_STATUS		0x0c
#define GTIMER_STATUS_EVENT	(1 << 0)
#define GTIMER_CMP_LOW		0x10
#define GTIMER_CMP_HIGH		0x14
#define GTIMER_AUTOINC		0x18

/* offset from periphbase */
#define PTIMER_ADDR		0x600
#define PTIMER_SIZE		0x100

/* registers */
#define PTIMER_LOAD		0x0
#define PTIMER_CNT		0x4
#define PTIMER_CTRL		0x8
#define PTIMER_CTRL_ENABLE	(1<<0)
#define PTIMER_CTRL_AUTORELOAD	(1<<1)
#define PTIMER_CTRL_IRQEN	(1<<2)
#define PTIMER_STATUS		0xC
#define PTIMER_STATUS_EVENT	(1<<0)

#define TIMER_FREQUENCY		396 * 1000 * 1000 /* ARM core clock */
int32_t amptimer_frequency = TIMER_FREQUENCY;

u_int amptimer_get_timecount(struct timecounter *);

static struct timecounter amptimer_timecounter = {
	.tc_get_timecount = amptimer_get_timecount,
	.tc_counter_mask = 0xffffffff,
	.tc_frequency = 0,
	.tc_name = "amptimer",
	.tc_quality = 0,
	.tc_priv = NULL,
	.tc_user = 0,
};

void amptimer_rearm(void *, uint64_t);
void amptimer_trigger(void *);

struct intrclock amptimer_intrclock = {
	.ic_rearm = amptimer_rearm,
	.ic_trigger = amptimer_trigger
};

#define MAX_ARM_CPUS	8

struct amptimer_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;
	bus_space_handle_t	sc_pioh;

	u_int32_t		sc_ticks_per_second;
	u_int64_t		sc_nsec_cycle_ratio;
	u_int64_t		sc_nsec_max;
};

int		amptimer_match(struct device *, void *, void *);
void		amptimer_attach(struct device *, struct device *, void *);
uint64_t	amptimer_readcnt64(struct amptimer_softc *sc);
int		amptimer_intr(void *);
void		amptimer_cpu_initclocks(void);
void		amptimer_delay(u_int);
void		amptimer_setstatclockrate(int stathz);
void		amptimer_set_clockrate(int32_t new_frequency);
void		amptimer_startclock(void);

/* hack - XXXX
 * gptimer connects directly to ampintc, not thru the generic
 * interface because it uses an 'internal' interrupt
 * not a peripheral interrupt.
 */
void	*ampintc_intr_establish(int, int, int, struct cpu_info *,
	    int (*)(void *), void *, char *);



const struct cfattach amptimer_ca = {
	sizeof (struct amptimer_softc), amptimer_match, amptimer_attach
};

struct cfdriver amptimer_cd = {
	NULL, "amptimer", DV_DULL
};

uint64_t
amptimer_readcnt64(struct amptimer_softc *sc)
{
	uint32_t high0, high1, low;
	bus_space_tag_t iot = sc->sc_iot;
	bus_space_handle_t ioh = sc->sc_ioh;

	do {
		high0 = bus_space_read_4(iot, ioh, GTIMER_CNT_HIGH);
		low = bus_space_read_4(iot, ioh, GTIMER_CNT_LOW);
		high1 = bus_space_read_4(iot, ioh, GTIMER_CNT_HIGH);
	} while (high0 != high1);

	return ((((uint64_t)high1) << 32) | low);
}

int
amptimer_match(struct device *parent, void *cfdata, void *aux)
{
	if ((cpufunc_id() & CPU_ID_CORTEX_A9_MASK) == CPU_ID_CORTEX_A9)
		return (1);

	return 0;
}

void
amptimer_attach(struct device *parent, struct device *self, void *args)
{
	struct amptimer_softc *sc = (struct amptimer_softc *)self;
	struct cortex_attach_args *ia = args;
	bus_space_handle_t ioh, pioh;

	sc->sc_iot = ia->ca_iot;

	if (bus_space_map(sc->sc_iot, ia->ca_periphbase + GTIMER_ADDR,
	    GTIMER_SIZE, 0, &ioh))
		panic("amptimer_attach: bus_space_map global timer failed!");

	if (bus_space_map(sc->sc_iot, ia->ca_periphbase + PTIMER_ADDR,
	    PTIMER_SIZE, 0, &pioh))
		panic("amptimer_attach: bus_space_map priv timer failed!");

	sc->sc_ticks_per_second = amptimer_frequency;
	sc->sc_nsec_cycle_ratio =
	    sc->sc_ticks_per_second * (1ULL << 32) / 1000000000;
	sc->sc_nsec_max = UINT64_MAX / sc->sc_nsec_cycle_ratio;
	printf(": %d kHz\n", sc->sc_ticks_per_second / 1000);

	sc->sc_ioh = ioh;
	sc->sc_pioh = pioh;

	/* disable global timer */
	bus_space_write_4(sc->sc_iot, ioh, GTIMER_CTRL, 0);

	/* XXX ??? reset counters to 0 - gives us uptime in the counter */
	bus_space_write_4(sc->sc_iot, ioh, GTIMER_CNT_LOW, 0);
	bus_space_write_4(sc->sc_iot, ioh, GTIMER_CNT_HIGH, 0);

	/* enable global timer */
	bus_space_write_4(sc->sc_iot, ioh, GTIMER_CTRL, GTIMER_CTRL_TIMER);

	/* Disable private timer, clear event flag. */
	bus_space_write_4(sc->sc_iot, sc->sc_pioh, PTIMER_CTRL, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_pioh, PTIMER_STATUS,
	    PTIMER_STATUS_EVENT);

	/*
	 * private timer and interrupts not enabled until
	 * timer configures
	 */

	arm_clock_register(amptimer_cpu_initclocks, amptimer_delay,
	    amptimer_setstatclockrate, amptimer_startclock);

	amptimer_timecounter.tc_frequency = sc->sc_ticks_per_second;
	amptimer_timecounter.tc_priv = sc;
	tc_init(&amptimer_timecounter);

	amptimer_intrclock.ic_cookie = sc;
}

u_int
amptimer_get_timecount(struct timecounter *tc)
{
	struct amptimer_softc *sc = amptimer_timecounter.tc_priv;
	return bus_space_read_4(sc->sc_iot, sc->sc_ioh, GTIMER_CNT_LOW);
}

void
amptimer_rearm(void *cookie, uint64_t nsecs)
{
	struct amptimer_softc *sc = cookie;
	uint32_t cycles, reg;

	if (nsecs > sc->sc_nsec_max)
		nsecs = sc->sc_nsec_max;
	cycles = (nsecs * sc->sc_nsec_cycle_ratio) >> 32;
	if (cycles == 0)
		cycles = 1;

	/* clear old status */
	bus_space_write_4(sc->sc_iot, sc->sc_pioh, PTIMER_STATUS,
	    PTIMER_STATUS_EVENT);

	/*
	 * Make sure the private timer counter and interrupts are enabled.
	 * XXX Why wouldn't they be?
	 */
	reg = bus_space_read_4(sc->sc_iot, sc->sc_pioh, PTIMER_CTRL);
	if ((reg & (PTIMER_CTRL_ENABLE | PTIMER_CTRL_IRQEN)) !=
	    (PTIMER_CTRL_ENABLE | PTIMER_CTRL_IRQEN))
		bus_space_write_4(sc->sc_iot, sc->sc_pioh, PTIMER_CTRL,
		    (PTIMER_CTRL_ENABLE | PTIMER_CTRL_IRQEN));

	/* Start the downcounter. */
	bus_space_write_4(sc->sc_iot, sc->sc_pioh, PTIMER_LOAD, cycles);
}

void
amptimer_trigger(void *cookie)
{
	struct amptimer_softc *sc = cookie;

	/* Clear event flag, then arm counter to fire immediately. */
	bus_space_write_4(sc->sc_iot, sc->sc_pioh, PTIMER_STATUS,
	    PTIMER_STATUS_EVENT);
	bus_space_write_4(sc->sc_iot, sc->sc_pioh, PTIMER_LOAD, 1);
}

int
amptimer_intr(void *frame)
{
	return clockintr_dispatch(frame);
}

void
amptimer_set_clockrate(int32_t new_frequency)
{
	struct amptimer_softc	*sc = amptimer_cd.cd_devs[0];

	amptimer_frequency = new_frequency;

	if (sc == NULL)
		return;

	sc->sc_ticks_per_second = amptimer_frequency;
	sc->sc_nsec_cycle_ratio =
	    sc->sc_ticks_per_second * (1ULL << 32) / 1000000000;
	sc->sc_nsec_max = UINT64_MAX / sc->sc_nsec_cycle_ratio;

	amptimer_timecounter.tc_frequency = sc->sc_ticks_per_second;

	printf("amptimer0: adjusting clock: new rate %d kHz\n",
	    sc->sc_ticks_per_second / 1000);
}

void
amptimer_cpu_initclocks(void)
{
	struct amptimer_softc	*sc = amptimer_cd.cd_devs[0];

	stathz = hz;
	profhz = hz * 10;
	statclock_is_randomized = 1;

	if (sc->sc_ticks_per_second != amptimer_frequency) {
		amptimer_set_clockrate(amptimer_frequency);
	}

	/* establish interrupts */
	/* XXX - irq */
	ampintc_intr_establish(29, IST_EDGE_RISING, IPL_CLOCK,
	    NULL, amptimer_intr, NULL, "tick");

	/* Enable private timer counter and interrupt. */
	bus_space_write_4(sc->sc_iot, sc->sc_pioh, PTIMER_CTRL,
	    (PTIMER_CTRL_ENABLE | PTIMER_CTRL_IRQEN));
}

void
amptimer_delay(u_int usecs)
{
	struct amptimer_softc	*sc = amptimer_cd.cd_devs[0];
	u_int32_t		clock, oclock, delta, delaycnt;
	volatile int		j;
	int			csec, usec;

	if (usecs > (0x80000000 / (sc->sc_ticks_per_second))) {
		csec = usecs / 10000;
		usec = usecs % 10000;

		delaycnt = (sc->sc_ticks_per_second / 100) * csec +
		    (sc->sc_ticks_per_second / 100) * usec / 10000;
	} else {
		delaycnt = sc->sc_ticks_per_second * usecs / 1000000;
	}
	if (delaycnt <= 1)
		for (j = 100; j > 0; j--)
			;

	oclock = bus_space_read_4(sc->sc_iot, sc->sc_ioh, GTIMER_CNT_LOW);
	while (1) {
		for (j = 100; j > 0; j--)
			;
		clock = bus_space_read_4(sc->sc_iot, sc->sc_ioh,
		    GTIMER_CNT_LOW);
		delta = clock - oclock;
		if (delta > delaycnt)
			break;
	}
}

void
amptimer_setstatclockrate(int newhz)
{
}

void
amptimer_startclock(void)
{
	clockintr_cpu_init(&amptimer_intrclock);
	clockintr_trigger();
}
