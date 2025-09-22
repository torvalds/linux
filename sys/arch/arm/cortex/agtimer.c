/* $OpenBSD: agtimer.c,v 1.21 2023/09/17 14:50:51 cheloha Exp $ */
/*
 * Copyright (c) 2011 Dale Rahn <drahn@openbsd.org>
 * Copyright (c) 2013 Patrick Wildt <patrick@blueri.se>
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

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <arm/cpufunc.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>

/* registers */
#define GTIMER_CNTP_CTL_ENABLE		(1 << 0)
#define GTIMER_CNTP_CTL_IMASK		(1 << 1)
#define GTIMER_CNTP_CTL_ISTATUS		(1 << 2)

#define TIMER_FREQUENCY		24 * 1000 * 1000 /* ARM core clock */
int32_t agtimer_frequency = TIMER_FREQUENCY;

u_int agtimer_get_timecount(struct timecounter *);

static struct timecounter agtimer_timecounter = {
	.tc_get_timecount = agtimer_get_timecount,
	.tc_counter_mask = 0xffffffff,
	.tc_frequency = 0,
	.tc_name = "agtimer",
	.tc_quality = 0,
	.tc_priv = NULL,
};

struct agtimer_softc {
	struct device		sc_dev;
	int			sc_node;
	u_int32_t		sc_ticks_per_second;
	uint64_t		sc_nsec_cycle_ratio;
	uint64_t		sc_nsec_max;
};

int		agtimer_match(struct device *, void *, void *);
void		agtimer_attach(struct device *, struct device *, void *);
uint64_t	agtimer_readcnt64(void);
int		agtimer_intr(void *);
void		agtimer_cpu_initclocks(void);
void		agtimer_delay(u_int);
void		agtimer_setstatclockrate(int stathz);
void		agtimer_set_clockrate(int32_t new_frequency);
void		agtimer_startclock(void);

const struct cfattach agtimer_ca = {
	sizeof (struct agtimer_softc), agtimer_match, agtimer_attach
};

struct cfdriver agtimer_cd = {
	NULL, "agtimer", DV_DULL
};

void agtimer_rearm(void *, uint64_t);
void agtimer_trigger(void *);

struct intrclock agtimer_intrclock = {
	.ic_rearm = agtimer_rearm,
	.ic_trigger = agtimer_trigger
};

uint64_t
agtimer_readcnt64(void)
{
	uint64_t val;

	__asm volatile("mrrc p15, 0, %Q0, %R0, c14" : "=r" (val));

	return (val);
}

static inline int
agtimer_get_ctrl(void)
{
	uint32_t val;

	__asm volatile("mrc p15, 0, %0, c14, c2, 1" : "=r" (val));

	return (val);
}

static inline int
agtimer_set_ctrl(uint32_t val)
{
	__asm volatile("mcr p15, 0, %[val], c14, c2, 1" : :
	    [val] "r" (val));

	cpu_drain_writebuf();
	//isb();

	return (0);
}

static inline int
agtimer_set_tval(uint32_t val)
{
	__asm volatile("mcr p15, 0, %[val], c14, c2, 0" : :
	    [val] "r" (val));
	cpu_drain_writebuf();
	//isb();

	return (0);
}

int
agtimer_match(struct device *parent, void *cfdata, void *aux)
{
	struct fdt_attach_args *faa = (struct fdt_attach_args *)aux;

	return OF_is_compatible(faa->fa_node, "arm,armv7-timer");
}

void
agtimer_attach(struct device *parent, struct device *self, void *aux)
{
	struct agtimer_softc *sc = (struct agtimer_softc *)self;
	struct fdt_attach_args *faa = aux;

	sc->sc_node = faa->fa_node;

	agtimer_frequency =
	    OF_getpropint(sc->sc_node, "clock-frequency", agtimer_frequency);
	sc->sc_ticks_per_second = agtimer_frequency;
	sc->sc_nsec_cycle_ratio =
	    sc->sc_ticks_per_second * (1ULL << 32) / 1000000000;
	sc->sc_nsec_max = UINT64_MAX / sc->sc_nsec_cycle_ratio;
	printf(": %d kHz\n", sc->sc_ticks_per_second / 1000);

	/* XXX: disable user access */

	/*
	 * private timer and interrupts not enabled until
	 * timer configures
	 */

	arm_clock_register(agtimer_cpu_initclocks, agtimer_delay,
	    agtimer_setstatclockrate, agtimer_startclock);

	agtimer_timecounter.tc_frequency = sc->sc_ticks_per_second;
	agtimer_timecounter.tc_priv = sc;
	tc_init(&agtimer_timecounter);

	agtimer_intrclock.ic_cookie = sc;
}

u_int
agtimer_get_timecount(struct timecounter *tc)
{
	return agtimer_readcnt64();
}

void
agtimer_rearm(void *cookie, uint64_t nsecs)
{
	struct agtimer_softc *sc = cookie;
	uint32_t cycles;

	if (nsecs > sc->sc_nsec_max)
		nsecs = sc->sc_nsec_max;
	cycles = (nsecs * sc->sc_nsec_cycle_ratio) >> 32;
	if (cycles > INT32_MAX)
		cycles = INT32_MAX;
	agtimer_set_tval(cycles);
}

void
agtimer_trigger(void *unused)
{
	agtimer_set_tval(0);
}

int
agtimer_intr(void *frame)
{
	return clockintr_dispatch(frame);
}

void
agtimer_set_clockrate(int32_t new_frequency)
{
	struct agtimer_softc	*sc = agtimer_cd.cd_devs[0];

	agtimer_frequency = new_frequency;

	if (sc == NULL)
		return;

	sc->sc_ticks_per_second = agtimer_frequency;
	sc->sc_nsec_cycle_ratio =
	    sc->sc_ticks_per_second * (1ULL << 32) / 1000000000;
	sc->sc_nsec_max = UINT64_MAX / sc->sc_nsec_cycle_ratio;

	agtimer_timecounter.tc_frequency = sc->sc_ticks_per_second;

	printf("agtimer0: adjusting clock: new rate %d kHz\n",
	    sc->sc_ticks_per_second / 1000);
}

void
agtimer_cpu_initclocks(void)
{
	struct agtimer_softc	*sc = agtimer_cd.cd_devs[0];

	stathz = hz;
	profhz = stathz * 10;
	statclock_is_randomized = 1;

	if (sc->sc_ticks_per_second != agtimer_frequency) {
		agtimer_set_clockrate(agtimer_frequency);
	}

	/* Setup secure and non-secure timer IRQs. */
	arm_intr_establish_fdt_idx(sc->sc_node, 0, IPL_CLOCK,
	    agtimer_intr, NULL, "tick");
	arm_intr_establish_fdt_idx(sc->sc_node, 1, IPL_CLOCK,
	    agtimer_intr, NULL, "tick");
}

void
agtimer_delay(u_int usecs)
{
	u_int32_t		clock, oclock, delta, delaycnt;
	volatile int		j;
	int			csec, usec;

	if (usecs > (0x80000000 / agtimer_frequency)) {
		csec = usecs / 10000;
		usec = usecs % 10000;

		delaycnt = (agtimer_frequency / 100) * csec +
		    (agtimer_frequency / 100) * usec / 10000;
	} else {
		delaycnt = agtimer_frequency * usecs / 1000000;
	}
	if (delaycnt <= 1)
		for (j = 100; j > 0; j--)
			;

	oclock = agtimer_readcnt64();
	while (1) {
		for (j = 100; j > 0; j--)
			;
		clock = agtimer_readcnt64();
		delta = clock - oclock;
		if (delta > delaycnt)
			break;
	}
}

void
agtimer_setstatclockrate(int newhz)
{
}

void
agtimer_startclock(void)
{
	uint32_t reg;

	clockintr_cpu_init(&agtimer_intrclock);

	reg = agtimer_get_ctrl();
	reg &= ~GTIMER_CNTP_CTL_IMASK;
	reg |= GTIMER_CNTP_CTL_ENABLE;
	agtimer_set_tval(INT32_MAX);
	agtimer_set_ctrl(reg);

	clockintr_trigger();
}

void
agtimer_init(void)
{
	uint32_t id_pfr1, cntfrq = 0;

	/* Check for Generic Timer support. */
	__asm volatile("mrc p15, 0, %0, c0, c1, 1" : "=r"(id_pfr1));
	if ((id_pfr1 & 0x000f0000) == 0x00010000)
		__asm volatile("mrc p15, 0, %0, c14, c0, 0" : "=r" (cntfrq));

	if (cntfrq != 0) {
		agtimer_frequency = cntfrq;
		arm_clock_register(NULL, agtimer_delay, NULL, NULL);
	}
}
