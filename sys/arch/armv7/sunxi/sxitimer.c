/*	$OpenBSD: sxitimer.c,v 1.25 2024/05/13 01:15:50 jsg Exp $	*/
/*
 * Copyright (c) 2007,2009 Dale Rahn <drahn@openbsd.org>
 * Copyright (c) 2013 Raphael Graf <r@undefined.ch>
 * Copyright (c) 2013 Artturi Alm
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
#include <sys/kernel.h>
#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/stdint.h>
#include <sys/timetc.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/intr.h>

#include <dev/fdt/sunxireg.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

#define	TIMER_IER 		0x00
#define	TIMER_ISR 		0x04
#define	TIMER_IRQ(x)		(1 << (x))

#define	TIMER_CTRL(x)		(0x10 + (0x10 * (x)))
#define	TIMER_INTV(x)		(0x14 + (0x10 * (x)))
#define	TIMER_CURR(x)		(0x18 + (0x10 * (x)))

/* A1X counter */
#define	CNT64_CTRL		0xa0
#define	CNT64_LOW		0xa4
#define	CNT64_HIGH		0xa8

#define	CNT64_CLR_EN		(1 << 0) /* clear enable */
#define	CNT64_RL_EN		(1 << 1) /* read latch enable */

#define	TIMER_ENABLE		(1 << 0)
#define	TIMER_RELOAD		(1 << 1)
#define	TIMER_CLK_SRC_MASK	(3 << 2)
#define	TIMER_OSC24M		(1 << 2)
#define	TIMER_PLL6_6		(2 << 2)
#define	TIMER_PRESC_1		(0 << 4)
#define	TIMER_PRESC_2		(1 << 4)
#define	TIMER_PRESC_4		(2 << 4)
#define	TIMER_PRESC_8		(3 << 4)
#define	TIMER_PRESC_16		(4 << 4)
#define	TIMER_PRESC_32		(5 << 4)
#define	TIMER_PRESC_64		(6 << 4)
#define	TIMER_PRESC_128		(7 << 4)
#define	TIMER_CONTINOUS		(0 << 7)
#define	TIMER_SINGLESHOT	(1 << 7)

#define	TICKTIMER		0
#define	STATTIMER		1
#define	CNTRTIMER		2

#define TIMER_SYNC		3

int	sxitimer_match(struct device *, void *, void *);
void	sxitimer_attach(struct device *, struct device *, void *);
int	sxitimer_tickintr(void *);
void	sxitimer_cpu_initclocks(void);
void	sxitimer_cpu_startclock(void);
void	sxitimer_setstatclockrate(int);
uint64_t	sxitimer_readcnt64(void);
uint32_t	sxitimer_readcnt32(void);
void	sxitimer_sync(void);
void	sxitimer_delay(u_int);

u_int sxitimer_get_timecount(struct timecounter *);

static struct timecounter sxitimer_timecounter = {
	.tc_get_timecount = sxitimer_get_timecount,
	.tc_counter_mask = 0xffffffff,
	.tc_frequency = 0,
	.tc_name = "sxitimer",
	.tc_quality = 0,
	.tc_priv = NULL,
	.tc_user = 0,
};

uint64_t sxitimer_nsec_cycle_ratio;
uint64_t sxitimer_nsec_max;

void sxitimer_rearm(void *, uint64_t);
void sxitimer_trigger(void *);

const struct intrclock sxitimer_intrclock = {
	.ic_rearm = sxitimer_rearm,
	.ic_trigger = sxitimer_trigger
};

bus_space_tag_t		sxitimer_iot;
bus_space_handle_t	sxitimer_ioh;

uint32_t sxitimer_freq[] = {
	TIMER0_FREQUENCY,
	TIMER1_FREQUENCY,
	TIMER2_FREQUENCY,
	0
};

uint32_t sxitimer_irq[] = {
	TIMER0_IRQ,
	TIMER1_IRQ,
	TIMER2_IRQ,
	0
};

struct sxitimer_softc {
	struct device		sc_dev;
};

const struct cfattach sxitimer_ca = {
	sizeof (struct sxitimer_softc), sxitimer_match, sxitimer_attach
};

struct cfdriver sxitimer_cd = {
	NULL, "sxitimer", DV_DULL
};

int
sxitimer_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	int node;

	node = OF_finddevice("/");
	if (!OF_is_compatible(node, "allwinner,sun4i-a10") &&
	    !OF_is_compatible(node, "allwinner,sun5i-a10s") &&
	    !OF_is_compatible(node, "allwinner,sun5i-a13"))
		return 0;

	return OF_is_compatible(faa->fa_node, "allwinner,sun4i-a10-timer");
}

void
sxitimer_attach(struct device *parent, struct device *self, void *aux)
{
	struct fdt_attach_args *faa = aux;

	KASSERT(faa->fa_nreg > 0);

	sxitimer_iot = faa->fa_iot;
	if (bus_space_map(sxitimer_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sxitimer_ioh))
		panic("%s: bus_space_map failed!", __func__);

	/* clear counter, loop until ready */
	bus_space_write_4(sxitimer_iot, sxitimer_ioh, CNT64_CTRL,
	    CNT64_CLR_EN); /* XXX as a side-effect counter clk src=OSC24M */
	while (bus_space_read_4(sxitimer_iot, sxitimer_ioh, CNT64_CTRL)
	    & CNT64_CLR_EN)
		continue;

	/* stop timer, and set clk src */
	bus_space_write_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_CTRL(TICKTIMER), TIMER_OSC24M);

	sxitimer_nsec_cycle_ratio =
	    sxitimer_freq[TICKTIMER] * (1ULL << 32) / 1000000000;
	sxitimer_nsec_max = UINT64_MAX / sxitimer_nsec_cycle_ratio;

	stathz = hz;
	profhz = stathz * 10;
	statclock_is_randomized = 1;

	/* stop timer, and set clk src */
	bus_space_write_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_CTRL(CNTRTIMER), TIMER_OSC24M);
	bus_space_write_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_INTV(CNTRTIMER), UINT32_MAX);

	sxitimer_timecounter.tc_frequency = sxitimer_freq[CNTRTIMER];
	tc_init(&sxitimer_timecounter);

	arm_clock_register(sxitimer_cpu_initclocks, sxitimer_delay,
	    sxitimer_setstatclockrate, sxitimer_cpu_startclock);

	printf(": %d kHz", sxitimer_freq[CNTRTIMER] / 1000);

	printf("\n");
}

/*
 * would be interesting to play with trigger mode while having one timer
 * in 32kHz mode, and the other timer running in sysclk mode and use
 * the high resolution speeds (matters more for delay than tick timer)
 */

void
sxitimer_cpu_initclocks(void)
{
	uint32_t isr, ier, ctrl;

	/* establish interrupt */
	arm_intr_establish(sxitimer_irq[TICKTIMER], IPL_CLOCK,
	    sxitimer_tickintr, NULL, "tick");

	/* clear timer interrupt pending bits */
	isr = bus_space_read_4(sxitimer_iot, sxitimer_ioh, TIMER_ISR);
	isr |= TIMER_IRQ(TICKTIMER);
	bus_space_write_4(sxitimer_iot, sxitimer_ioh, TIMER_ISR, isr);

	/* enable timer IRQ */
	ier = bus_space_read_4(sxitimer_iot, sxitimer_ioh, TIMER_IER);
	ier |= TIMER_IRQ(TICKTIMER);
	bus_space_write_4(sxitimer_iot, sxitimer_ioh, TIMER_IER, ier);

	/* enable timers */
	ctrl = bus_space_read_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_CTRL(CNTRTIMER));
	bus_space_write_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_CTRL(CNTRTIMER),
	    ctrl | TIMER_ENABLE | TIMER_RELOAD | TIMER_CONTINOUS);
}

void
sxitimer_cpu_startclock(void)
{
	/* start clock interrupt cycle */
	clockintr_cpu_init(&sxitimer_intrclock);
	clockintr_trigger();
}

int
sxitimer_tickintr(void *frame)
{
	splassert(IPL_CLOCK);	

	/* clear timer pending interrupt bit */
	bus_space_write_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_ISR, TIMER_IRQ(TICKTIMER));

	return clockintr_dispatch(frame);
}

uint64_t
sxitimer_readcnt64(void)
{
	uint32_t low, high;

	/* latch counter, loop until ready */
	bus_space_write_4(sxitimer_iot, sxitimer_ioh, CNT64_CTRL, CNT64_RL_EN);
	while (bus_space_read_4(sxitimer_iot, sxitimer_ioh, CNT64_CTRL)
	    & CNT64_RL_EN)
		continue;

	/*
	 * A10 usermanual doesn't mention anything about order, but fwiw
	 * iirc. A20 manual mentions that low should be read first.
	 */
	/* XXX check above */
	low = bus_space_read_4(sxitimer_iot, sxitimer_ioh, CNT64_LOW);
	high = bus_space_read_4(sxitimer_iot, sxitimer_ioh, CNT64_HIGH);
	return (uint64_t)high << 32 | low;
}

uint32_t
sxitimer_readcnt32(void)
{
	return bus_space_read_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_CURR(CNTRTIMER));
}

void
sxitimer_sync(void)
{
	uint32_t now = sxitimer_readcnt32();

	while ((now - sxitimer_readcnt32()) < TIMER_SYNC)
		CPU_BUSY_CYCLE();
}

void
sxitimer_delay(u_int usecs)
{
	uint64_t oclock, timeout;

	oclock = sxitimer_readcnt64();
	timeout = oclock + (COUNTER_FREQUENCY / 1000000) * usecs;

	while (oclock < timeout)
		oclock = sxitimer_readcnt64();
}

void
sxitimer_setstatclockrate(int newhz)
{
}

u_int
sxitimer_get_timecount(struct timecounter *tc)
{
	return (u_int)UINT_MAX - sxitimer_readcnt32();
}

void
sxitimer_rearm(void *unused, uint64_t nsecs)
{
	uint32_t ctrl, cycles;

	if (nsecs > sxitimer_nsec_max)
		nsecs = sxitimer_nsec_max;
	cycles = (nsecs * sxitimer_nsec_cycle_ratio) >> 32;
	if (cycles < 10)
		cycles = 10;	/* XXX Why do we need to round up to 10? */

	ctrl = bus_space_read_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_CTRL(TICKTIMER));
	bus_space_write_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_CTRL(TICKTIMER), ctrl & ~TIMER_ENABLE);

	sxitimer_sync();

	bus_space_write_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_INTV(TICKTIMER), cycles);

	ctrl = bus_space_read_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_CTRL(TICKTIMER));
	bus_space_write_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_CTRL(TICKTIMER),
	    ctrl | TIMER_ENABLE | TIMER_RELOAD | TIMER_SINGLESHOT);
}

void
sxitimer_trigger(void *unused)
{
	uint32_t ctrl;

	ctrl = bus_space_read_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_CTRL(TICKTIMER));
	bus_space_write_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_CTRL(TICKTIMER), ctrl & ~TIMER_ENABLE);

	sxitimer_sync();

	/* XXX Why do we need to round up to 10? */
	bus_space_write_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_INTV(TICKTIMER), 10);

	ctrl = bus_space_read_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_CTRL(TICKTIMER));
	bus_space_write_4(sxitimer_iot, sxitimer_ioh,
	    TIMER_CTRL(TICKTIMER),
	    ctrl | TIMER_ENABLE | TIMER_RELOAD | TIMER_SINGLESHOT);
}
