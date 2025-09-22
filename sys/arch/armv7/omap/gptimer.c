/* $OpenBSD: gptimer.c,v 1.23 2023/09/17 14:50:51 cheloha Exp $ */
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
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
#include <sys/clockintr.h>
#include <sys/kernel.h>
#include <sys/evcount.h>
#include <sys/device.h>
#include <sys/stdint.h>
#include <sys/timetc.h>
#include <machine/bus.h>
#include <armv7/armv7/armv7var.h>
#include <armv7/omap/prcmvar.h>

#include <machine/intr.h>

/* registers */
#define	GP_TIDR		0x000
#define		GP_TIDR_REV	0xff
#define GP_TIOCP_CFG	0x010
#define 	GP_TIOCP_CFG_CLKA	0x000000300
#define 	GP_TIOCP_CFG_EMUFREE	0x000000020
#define 	GP_TIOCP_CFG_IDLEMODE	0x000000018
#define 	GP_TIOCP_CFG_ENAPWAKEUP	0x000000004
#define 	GP_TIOCP_CFG_SOFTRESET	0x000000002
#define 	GP_TIOCP_CFG_AUTOIDLE	0x000000001
#define	GP_TISTAT	0x014
#define 	GP_TISTAT_RESETDONE	0x000000001
#define	GP_TISR		0x018
#define		GP_TISTAT_TCAR		0x00000004
#define		GP_TISTAT_OVF		0x00000002
#define		GP_TISTAT_MATCH		0x00000001
#define GP_TIER		0x1c
#define		GP_TIER_TCAR_EN		0x4
#define		GP_TIER_OVF_EN		0x2
#define		GP_TIER_MAT_EN		0x1
#define	GP_TWER		0x020
#define		GP_TWER_TCAR_EN		0x00000004
#define		GP_TWER_OVF_EN		0x00000002
#define		GP_TWER_MAT_EN		0x00000001
#define	GP_TCLR		0x024
#define		GP_TCLR_GPO		(1<<14)
#define		GP_TCLR_CAPT		(1<<13)
#define		GP_TCLR_PT		(1<<12)
#define		GP_TCLR_TRG		(3<<10)
#define		GP_TCLR_TRG_O		(1<<10)
#define		GP_TCLR_TRG_OM		(2<<10)
#define		GP_TCLR_TCM		(3<<8)
#define		GP_TCLR_TCM_RISE	(1<<8)
#define		GP_TCLR_TCM_FALL	(2<<8)
#define		GP_TCLR_TCM_BOTH	(3<<8)
#define		GP_TCLR_SCPWM		(1<<7)
#define		GP_TCLR_CE		(1<<6)
#define		GP_TCLR_PRE		(1<<5)
#define		GP_TCLR_PTV		(7<<2)
#define		GP_TCLR_AR		(1<<1)
#define		GP_TCLR_ST		(1<<0)
#define	GP_TCRR		0x028				/* counter */
#define	GP_TLDR		0x02c				/* reload */
#define	GP_TTGR		0x030
#define	GP_TWPS		0x034
#define		GP_TWPS_TCLR	0x01
#define		GP_TWPS_TCRR	0x02
#define		GP_TWPS_TLDR	0x04
#define		GP_TWPS_TTGR	0x08
#define		GP_TWPS_TMAR	0x10
#define		GP_TWPS_ALL	0x1f
#define	GP_TMAR		0x038
#define	GP_TCAR		0x03C
#define	GP_TSICR	0x040
#define		GP_TSICR_POSTED		0x00000002
#define		GP_TSICR_SFT		0x00000001
#define	GP_TCAR2	0x044

#define TIMER_FREQUENCY			32768	/* 32kHz is used, selectable */

void gptimer_attach(struct device *parent, struct device *self, void *args);
int gptimer_intr(void *frame);
void gptimer_wait(int reg);
void gptimer_cpu_initclocks(void);
void gptimer_cpu_startclock(void);
void gptimer_delay(u_int);
void gptimer_reset_tisr(void);
void gptimer_setstatclockrate(int newhz);

bus_space_tag_t gptimer_iot;
bus_space_handle_t gptimer_ioh0, gptimer_ioh1;
int gptimer_irq = 0;

u_int gptimer_get_timecount(struct timecounter *);

static struct timecounter gptimer_timecounter = {
	.tc_get_timecount = gptimer_get_timecount,
	.tc_counter_mask = 0xffffffff,
	.tc_frequency = 0,
	.tc_name = "gptimer",
	.tc_quality = 0,
	.tc_priv = NULL,
	.tc_user = 0,
};

uint64_t gptimer_nsec_cycle_ratio;
uint64_t gptimer_nsec_max;

void gptimer_rearm(void *, uint64_t);
void gptimer_trigger(void *);

const struct intrclock gptimer_intrclock = {
	.ic_rearm = gptimer_rearm,
	.ic_trigger = gptimer_trigger
};

const struct cfattach	gptimer_ca = {
	sizeof (struct device), NULL, gptimer_attach
};

struct cfdriver gptimer_cd = {
	NULL, "gptimer", DV_DULL
};

void
gptimer_attach(struct device *parent, struct device *self, void *args)
{
	struct armv7_attach_args *aa = args;
	bus_space_handle_t ioh;
	u_int32_t rev;

	gptimer_iot = aa->aa_iot;
	if (bus_space_map(gptimer_iot, aa->aa_dev->mem[0].addr,
	    aa->aa_dev->mem[0].size, 0, &ioh))
		panic("gptimer_attach: bus_space_map failed!");

	rev = bus_space_read_4(gptimer_iot, ioh, GP_TIDR);

	printf(" rev %d.%d\n", rev >> 4 & 0xf, rev & 0xf);
	if (self->dv_unit == 0) {
		gptimer_ioh0 = ioh;
		gptimer_irq = aa->aa_dev->irq[0];
		bus_space_write_4(gptimer_iot, gptimer_ioh0, GP_TCLR, 0);
	} else if (self->dv_unit == 1) {
		/* start timer because it is used in delay */
		gptimer_ioh1 = ioh;
		bus_space_write_4(gptimer_iot, gptimer_ioh1, GP_TCRR, 0);
		gptimer_wait(GP_TWPS_ALL);
		bus_space_write_4(gptimer_iot, gptimer_ioh1, GP_TLDR, 0);
		gptimer_wait(GP_TWPS_ALL);
		bus_space_write_4(gptimer_iot, gptimer_ioh1, GP_TCLR,
		    GP_TCLR_AR | GP_TCLR_ST);
		gptimer_wait(GP_TWPS_ALL);

		gptimer_timecounter.tc_frequency = TIMER_FREQUENCY;
		tc_init(&gptimer_timecounter);
	}
	else
		panic("attaching too many gptimers at 0x%lx",
		    aa->aa_dev->mem[0].addr);

	arm_clock_register(gptimer_cpu_initclocks, gptimer_delay,
	    gptimer_setstatclockrate, gptimer_cpu_startclock);
}

int
gptimer_intr(void *frame)
{
	clockintr_dispatch(frame);
	return 1;
}

/*
 * would be interesting to play with trigger mode while having one timer
 * in 32kHz mode, and the other timer running in sysclk mode and use
 * the high resolution speeds (matters more for delay than tick timer
 */

void
gptimer_cpu_initclocks(void)
{
	stathz = hz;
	profhz = stathz * 10;
	statclock_is_randomized = 1;

	gptimer_nsec_cycle_ratio = TIMER_FREQUENCY * (1ULL << 32) / 1000000000;
	gptimer_nsec_max = UINT64_MAX / gptimer_nsec_cycle_ratio;

	prcm_setclock(1, PRCM_CLK_SPEED_32);
	prcm_setclock(2, PRCM_CLK_SPEED_32);

	/* establish interrupts */
	arm_intr_establish(gptimer_irq, IPL_CLOCK, gptimer_intr,
	    NULL, "tick");

	/* setup timer 0 (hardware timer 2) */
	/* reset? - XXX */
	gptimer_wait(GP_TWPS_ALL);
	bus_space_write_4(gptimer_iot, gptimer_ioh0, GP_TIER, GP_TIER_OVF_EN);
	gptimer_wait(GP_TWPS_ALL);
	bus_space_write_4(gptimer_iot, gptimer_ioh0, GP_TWER, GP_TWER_OVF_EN);
	gptimer_wait(GP_TWPS_ALL);
}

void
gptimer_cpu_startclock(void)
{
	/* start the clock interrupt cycle */
	clockintr_cpu_init(&gptimer_intrclock);
	clockintr_trigger();
}

void
gptimer_wait(int reg)
{
	while (bus_space_read_4(gptimer_iot, gptimer_ioh0, GP_TWPS) & reg)
		;
}

/*
 * Clear all interrupt status bits.
 */
void
gptimer_reset_tisr(void)
{
	u_int32_t tisr;

	tisr = bus_space_read_4(gptimer_iot, gptimer_ioh0, GP_TISR);
	bus_space_write_4(gptimer_iot, gptimer_ioh0, GP_TISR, tisr);
}

void
gptimer_rearm(void *unused, uint64_t nsecs)
{
	uint32_t cycles;
	u_long s;

	if (nsecs > gptimer_nsec_max)
		nsecs = gptimer_nsec_max;
	cycles = (nsecs * gptimer_nsec_cycle_ratio) >> 32;

	s = intr_disable();
	gptimer_reset_tisr();
        bus_space_write_4(gptimer_iot, gptimer_ioh0, GP_TCRR,
	    UINT32_MAX - cycles);
	bus_space_write_4(gptimer_iot, gptimer_ioh0, GP_TCLR, GP_TCLR_ST);
	gptimer_wait(GP_TWPS_ALL);
	intr_restore(s);
}

void
gptimer_trigger(void *unused)
{
	u_long s;

	s = intr_disable();

	/* stop timer. */
	bus_space_write_4(gptimer_iot, gptimer_ioh0, GP_TCLR, 0);
	gptimer_wait(GP_TWPS_ALL);

	/* clear interrupt status bits. */
	gptimer_reset_tisr();

	/* set shortest possible timeout. */
	bus_space_write_4(gptimer_iot, gptimer_ioh0, GP_TCRR, UINT32_MAX);

	/* start timer, wait for writes to post. */
	bus_space_write_4(gptimer_iot, gptimer_ioh0, GP_TCLR, GP_TCLR_ST);
	gptimer_wait(GP_TWPS_ALL);

	intr_restore(s);
}

void
gptimer_delay(u_int usecs)
{
	u_int32_t clock, oclock, delta, delaycnt;
	volatile int j;
	int csec, usec;

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

	if (gptimer_ioh1 == 0) {
		/* BAH */
		for (; usecs > 0; usecs--)
			for (j = 100; j > 0; j--)
				;
		return;
	}
	oclock = bus_space_read_4(gptimer_iot, gptimer_ioh1, GP_TCRR);
	while (1) {
		for (j = 100; j > 0; j--)
			;
		clock = bus_space_read_4(gptimer_iot, gptimer_ioh1, GP_TCRR);
		delta = clock - oclock;
		if (delta > delaycnt)
			break;
	}
	
}

void
gptimer_setstatclockrate(int newhz)
{
}


u_int
gptimer_get_timecount(struct timecounter *tc)
{
	return bus_space_read_4(gptimer_iot, gptimer_ioh1, GP_TCRR);
}
