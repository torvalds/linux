/*	$OpenBSD: dmtimer.c,v 1.22 2023/09/17 14:50:51 cheloha Exp $	*/
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
 * Copyright (c) 2013 Raphael Graf <r@undefined.ch>
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

/*
 *	WARNING - this timer initialization has not been checked
 *	to see if it will do _ANYTHING_ sane if the omap enters
 *	low power mode.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/clockintr.h>
#include <sys/evcount.h>
#include <sys/device.h>
#include <sys/stdint.h>
#include <sys/timetc.h>
#include <machine/bus.h>
#include <armv7/armv7/armv7var.h>
#include <armv7/omap/prcmvar.h>

#include <machine/intr.h>

/* registers */
#define	DM_TIDR		0x000
#define		DM_TIDR_MAJOR		0x00000700
#define		DM_TIDR_MINOR		0x0000003f
#define	DM_TIOCP_CFG	0x010
#define 	DM_TIOCP_CFG_IDLEMODE	(3<<2)
#define 	DM_TIOCP_CFG_EMUFREE	(1<<1)
#define 	DM_TIOCP_CFG_SOFTRESET	(1<<0)
#define	DM_TISR		0x028
#define		DM_TISR_TCAR		(1<<2)
#define		DM_TISR_OVF		(1<<1)
#define		DM_TISR_MAT		(1<<0)
#define DM_TIER		0x2c
#define		DM_TIER_TCAR_EN		(1<<2)
#define		DM_TIER_OVF_EN		(1<<1)
#define		DM_TIER_MAT_EN		(1<<0)
#define DM_TIECR	0x30
#define		DM_TIECR_TCAR_EN	(1<<2)
#define		DM_TIECR_OVF_EN		(1<<1)
#define		DM_TIECR_MAT_EN		(1<<0)
#define	DM_TWER		0x034
#define		DM_TWER_TCAR_EN		(1<<2)
#define		DM_TWER_OVF_EN		(1<<1)
#define		DM_TWER_MAT_EN		(1<<0)
#define	DM_TCLR		0x038
#define		DM_TCLR_GPO		(1<<14)
#define		DM_TCLR_CAPT		(1<<13)
#define		DM_TCLR_PT		(1<<12)
#define		DM_TCLR_TRG		(3<<10)
#define		DM_TCLR_TRG_O		(1<<10)
#define		DM_TCLR_TRG_OM		(2<<10)
#define		DM_TCLR_TCM		(3<<8)
#define		DM_TCLR_TCM_RISE	(1<<8)
#define		DM_TCLR_TCM_FALL	(2<<8)
#define		DM_TCLR_TCM_BOTH	(3<<8)
#define		DM_TCLR_SCPWM		(1<<7)
#define		DM_TCLR_CE		(1<<6)
#define		DM_TCLR_PRE		(1<<5)
#define		DM_TCLR_PTV		(7<<2)
#define		DM_TCLR_AR		(1<<1)
#define		DM_TCLR_ST		(1<<0)
#define	DM_TCRR		0x03c
#define	DM_TLDR		0x040
#define	DM_TTGR		0x044
#define	DM_TWPS		0x048
#define		DM_TWPS_TMAR		(1<<4)
#define		DM_TWPS_TTGR		(1<<3)
#define		DM_TWPS_TLDR		(1<<2)
#define		DM_TWPS_TCLR		(1<<0)
#define		DM_TWPS_TCRR		(1<<1)
#define		DM_TWPS_ALL		0x1f
#define	DM_TMAR		0x04c
#define	DM_TCAR		0x050
#define	DM_TSICR	0x054
#define		DM_TSICR_POSTED		(1<<2)
#define		DM_TSICR_SFT		(1<<1)
#define	DM_TCAR2	0x058

#define TIMER_FREQUENCY			32768	/* 32kHz is used, selectable */
#define MAX_TIMERS			2

void dmtimer_attach(struct device *parent, struct device *self, void *args);
int dmtimer_intr(void *frame);
void dmtimer_reset_tisr(void);
void dmtimer_wait(int reg);
void dmtimer_cpu_initclocks(void);
void dmtimer_cpu_startclock(void);
void dmtimer_delay(u_int);
void dmtimer_setstatclockrate(int newhz);

u_int dmtimer_get_timecount(struct timecounter *);

static struct timecounter dmtimer_timecounter = {
	.tc_get_timecount = dmtimer_get_timecount,
	.tc_counter_mask = 0xffffffff,
	.tc_frequency = 0,
	.tc_name = "dmtimer",
	.tc_quality = 0,
	.tc_priv = NULL,
};

void dmtimer_rearm(void *, uint64_t);
void dmtimer_trigger(void *);

struct intrclock dmtimer_intrclock = {
	.ic_rearm = dmtimer_rearm,
	.ic_trigger = dmtimer_trigger
};

bus_space_handle_t dmtimer_ioh0;
int dmtimer_irq = 0;

struct dmtimer_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh[MAX_TIMERS];
	u_int32_t		sc_irq;
	u_int32_t		sc_ticks_per_second;
	u_int64_t		sc_nsec_cycle_ratio;
	u_int64_t		sc_nsec_max;
};

const struct cfattach	dmtimer_ca = {
	sizeof (struct dmtimer_softc), NULL, dmtimer_attach
};

struct cfdriver dmtimer_cd = {
	NULL, "dmtimer", DV_DULL
};

void
dmtimer_attach(struct device *parent, struct device *self, void *args)
{
	struct dmtimer_softc	*sc = (struct dmtimer_softc *)self;
	struct armv7_attach_args *aa = args;
	bus_space_handle_t	ioh;
	u_int32_t		rev, cfg;

	sc->sc_iot = aa->aa_iot;

	if (bus_space_map(sc->sc_iot, aa->aa_dev->mem[0].addr,
	    aa->aa_dev->mem[0].size, 0, &ioh))
		panic("%s: bus_space_map failed!", __func__);


	prcm_setclock(1, PRCM_CLK_SPEED_32);
	prcm_setclock(2, PRCM_CLK_SPEED_32);
	prcm_enablemodule(PRCM_TIMER2);
	prcm_enablemodule(PRCM_TIMER3);

	/* reset */
	bus_space_write_4(sc->sc_iot, ioh, DM_TIOCP_CFG,
	    DM_TIOCP_CFG_SOFTRESET);
	while (bus_space_read_4(sc->sc_iot, ioh, DM_TIOCP_CFG)
	    & DM_TIOCP_CFG_SOFTRESET)
		;

	if (self->dv_unit == 0) {
		dmtimer_ioh0 = ioh;
		dmtimer_irq = aa->aa_dev->irq[0];
		/* enable write posted mode */
		bus_space_write_4(sc->sc_iot, ioh, DM_TSICR, DM_TSICR_POSTED);
		/* stop timer */
		bus_space_write_4(sc->sc_iot, ioh, DM_TCLR, 0);
	} else if (self->dv_unit == 1) {
		/* start timer because it is used in delay */
		/* interrupts and posted mode are disabled */
		sc->sc_irq = dmtimer_irq;
		sc->sc_ioh[0] = dmtimer_ioh0;
		sc->sc_ioh[1] = ioh;

		bus_space_write_4(sc->sc_iot, ioh, DM_TCRR, 0);
		bus_space_write_4(sc->sc_iot, ioh, DM_TLDR, 0);
		bus_space_write_4(sc->sc_iot, ioh, DM_TCLR,
		    DM_TCLR_AR | DM_TCLR_ST);

		dmtimer_timecounter.tc_frequency = TIMER_FREQUENCY;
		dmtimer_timecounter.tc_priv = sc;
		tc_init(&dmtimer_timecounter);
		arm_clock_register(dmtimer_cpu_initclocks, dmtimer_delay,
		    dmtimer_setstatclockrate, dmtimer_cpu_startclock);
	}
	else
		panic("attaching too many dmtimers at 0x%lx",
		    aa->aa_dev->mem[0].addr);

	/* set IDLEMODE to smart-idle */
	cfg = bus_space_read_4(sc->sc_iot, ioh, DM_TIOCP_CFG);
	bus_space_write_4(sc->sc_iot, ioh, DM_TIOCP_CFG,
	    (cfg & ~DM_TIOCP_CFG_IDLEMODE) | 0x02);

	rev = bus_space_read_4(sc->sc_iot, ioh, DM_TIDR);
	printf(" rev %d.%d\n", (rev & DM_TIDR_MAJOR) >> 8, rev & DM_TIDR_MINOR);
}

int
dmtimer_intr(void *frame)
{
	dmtimer_reset_tisr();		/* clear pending interrupts */
	clockintr_dispatch(frame);
	return 1;
}

/*
 * would be interesting to play with trigger mode while having one timer
 * in 32kHz mode, and the other timer running in sysclk mode and use
 * the high resolution speeds (matters more for delay than tick timer
 */

void
dmtimer_cpu_initclocks(void)
{
	struct dmtimer_softc	*sc = dmtimer_cd.cd_devs[1];

	stathz = hz;
	profhz = stathz * 10;
	statclock_is_randomized = 1;

	sc->sc_ticks_per_second = TIMER_FREQUENCY; /* 32768 */
	sc->sc_nsec_cycle_ratio =
	    sc->sc_ticks_per_second * (1ULL << 32) / 1000000000;
	sc->sc_nsec_max = UINT64_MAX / sc->sc_nsec_cycle_ratio;
	dmtimer_intrclock.ic_cookie = sc;

	/* establish interrupts */
	arm_intr_establish(sc->sc_irq, IPL_CLOCK, dmtimer_intr,
	    NULL, "tick");

	/* setup timer 0 */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh[0], DM_TLDR, 0);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh[0], DM_TIER, DM_TIER_OVF_EN);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh[0], DM_TWER, DM_TWER_OVF_EN);
}

void
dmtimer_cpu_startclock(void)
{
	/* start the clock interrupt cycle */
	clockintr_cpu_init(&dmtimer_intrclock);
	clockintr_trigger();
}

void
dmtimer_wait(int reg)
{
	struct dmtimer_softc	*sc = dmtimer_cd.cd_devs[1];
	while (bus_space_read_4(sc->sc_iot, sc->sc_ioh[0], DM_TWPS) & reg)
		;
}

/*
 * Clear all interrupt status bits.
 */
void
dmtimer_reset_tisr(void)
{
	struct dmtimer_softc *sc = dmtimer_cd.cd_devs[1];
	u_int32_t tisr;

	tisr = bus_space_read_4(sc->sc_iot, sc->sc_ioh[0], DM_TISR);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh[0], DM_TISR, tisr);
}

void
dmtimer_delay(u_int usecs)
{
	struct dmtimer_softc	*sc = dmtimer_cd.cd_devs[1];
	u_int32_t		clock, oclock, delta, delaycnt;
	volatile int		j;
	int			csec, usec;

	if (usecs > (0x80000000 / (TIMER_FREQUENCY))) {
		csec = usecs / 10000;
		usec = usecs % 10000;

		delaycnt = (TIMER_FREQUENCY / 100) * csec +
		    (TIMER_FREQUENCY / 100) * usec / 10000;
	} else {
		delaycnt = TIMER_FREQUENCY * usecs / 1000000;
	}
	if (delaycnt <= 1)
		for (j = 100; j > 0; j--)
			;

	if (sc->sc_ioh[1] == 0) {
		/* BAH */
		for (; usecs > 0; usecs--)
			for (j = 100; j > 0; j--)
				;
		return;
	}
	oclock = bus_space_read_4(sc->sc_iot, sc->sc_ioh[1], DM_TCRR);
	while (1) {
		for (j = 100; j > 0; j--)
			;
		clock = bus_space_read_4(sc->sc_iot, sc->sc_ioh[1], DM_TCRR);
		delta = clock - oclock;
		if (delta > delaycnt)
			break;
	}
	
}

void
dmtimer_setstatclockrate(int newhz)
{
}


u_int
dmtimer_get_timecount(struct timecounter *tc)
{
	struct dmtimer_softc *sc = dmtimer_timecounter.tc_priv;
	
	return bus_space_read_4(sc->sc_iot, sc->sc_ioh[1], DM_TCRR);
}

void
dmtimer_rearm(void *cookie, uint64_t nsecs)
{
	struct dmtimer_softc *sc = cookie;
	uint32_t cycles;

	if (nsecs > sc->sc_nsec_max)
		nsecs = sc->sc_nsec_max;
	cycles = (nsecs * sc->sc_nsec_cycle_ratio) >> 32;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh[0], DM_TCRR,
	    UINT32_MAX - cycles);
	bus_space_write_4(sc->sc_iot, sc->sc_ioh[0], DM_TCLR, DM_TCLR_ST);
	dmtimer_wait(DM_TWPS_ALL);
}

void
dmtimer_trigger(void *cookie)
{
	struct dmtimer_softc *sc = cookie;

	/* stop timer */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh[0], DM_TCLR, 0);

	dmtimer_reset_tisr();	/* clear pending interrupts */

	/* set shortest possible timeout */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh[0], DM_TCRR, UINT32_MAX);
	dmtimer_wait(DM_TWPS_ALL);

	/* start timer */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh[0], DM_TCLR, DM_TCLR_ST);
	dmtimer_wait(DM_TWPS_ALL);
}
