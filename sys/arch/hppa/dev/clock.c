/*	$OpenBSD: clock.c,v 1.39 2023/09/17 14:50:51 cheloha Exp $	*/

/*
 * Copyright (c) 1998-2003 Michael Shalayeff
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR OR HIS RELATIVES BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF MIND, USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>
#include <sys/clockintr.h>
#include <sys/stdint.h>
#include <sys/timetc.h>

#include <dev/clock_subr.h>

#include <machine/pdc.h>
#include <machine/iomod.h>
#include <machine/psl.h>
#include <machine/intr.h>
#include <machine/reg.h>
#include <machine/cpufunc.h>
#include <machine/autoconf.h>

uint64_t itmr_nsec_cycle_ratio;
uint64_t itmr_nsec_max;

u_int	itmr_get_timecount(struct timecounter *);
int	itmr_intr(void *);
void	itmr_rearm(void *, uint64_t);
void	itmr_trigger(void);
void	itmr_trigger_masked(void);
void	itmr_trigger_wrapper(void *);

struct timecounter itmr_timecounter = {
	.tc_get_timecount = itmr_get_timecount,
	.tc_counter_mask = 0xffffffff,
	.tc_frequency = 0,
	.tc_name = "itmr",
	.tc_quality = 0,
	.tc_priv = NULL,
	.tc_user = 0,
};

const struct intrclock itmr_intrclock = {
	.ic_rearm = itmr_rearm,
	.ic_trigger = itmr_trigger_wrapper
};

extern todr_chip_handle_t todr_handle;
struct todr_chip_handle pdc_todr;

int
pdc_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	struct pdc_tod tod PDC_ALIGNMENT;
	int error;

	if ((error = pdc_call((iodcio_t)pdc, 1, PDC_TOD, PDC_TOD_READ,
	    &tod, 0, 0, 0, 0, 0))) {
		printf("clock: failed to fetch (%d)\n", error);
		return EIO;
	}

	tv->tv_sec = tod.sec;
	tv->tv_usec = tod.usec;
	return 0;
}

int
pdc_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	int error;

	if ((error = pdc_call((iodcio_t)pdc, 1, PDC_TOD, PDC_TOD_WRITE,
	    tv->tv_sec, tv->tv_usec))) {
		printf("clock: failed to save (%d)\n", error);
		return EIO;
	}

	return 0;
}

void
cpu_initclocks(void)
{
	uint64_t itmr_freq = PAGE0->mem_10msec * 100;

	pdc_todr.todr_gettime = pdc_gettime;
	pdc_todr.todr_settime = pdc_settime;
	todr_handle = &pdc_todr;

	itmr_timecounter.tc_frequency = itmr_freq;
	tc_init(&itmr_timecounter);

	stathz = hz;
	profhz = stathz * 10;
	statclock_is_randomized = 1;

	itmr_nsec_cycle_ratio = itmr_freq * (1ULL << 32) / 1000000000;
	itmr_nsec_max = UINT64_MAX / itmr_nsec_cycle_ratio;
}

void
cpu_startclock(void)
{
	clockintr_cpu_init(&itmr_intrclock);
	clockintr_trigger();
}

int
itmr_intr(void *v)
{
	clockintr_dispatch(v);
	return (1);
}

void
setstatclockrate(int newhz)
{
}

u_int
itmr_get_timecount(struct timecounter *tc)
{
	u_long __itmr;

	mfctl(CR_ITMR, __itmr);
	return (__itmr);
}

/*
 * Program the next clock interrupt, making sure it will
 * indeed happen in the future.  This is done with interrupts
 * disabled to avoid a possible race.
 */
void
itmr_rearm(void *unused, uint64_t nsecs)
{
	uint32_t cycles, t0, t1;
	register_t eiem, eirr;

	if (nsecs > itmr_nsec_max)
		nsecs = itmr_nsec_max;
	cycles = (nsecs * itmr_nsec_cycle_ratio) >> 32;

	eiem = hppa_intr_disable();
	mfctl(CR_ITMR, t0);
	mtctl(t0 + cycles, CR_ITMR);
	mfctl(CR_ITMR, t1);
	mfctl(CR_EIRR, eirr);

	/*
	 * If at least "cycles" ITMR ticks have elapsed and the interrupt
	 * isn't pending, we missed.  Fall back to itmr_trigger_masked().
	 */
	if (cycles <= t1 - t0) {
		if (!ISSET(eirr, 1U << 31))
			itmr_trigger_masked();
	}
	hppa_intr_enable(eiem);
}

void
itmr_trigger(void)
{
	register_t eiem;

	eiem = hppa_intr_disable();
	itmr_trigger_masked();
	hppa_intr_enable(eiem);
}

/* Trigger our own ITMR interrupt by setting EIR{0}. */
void
itmr_trigger_masked(void)
{
	struct iomod *cpu = (struct iomod *)curcpu()->ci_hpa;

	cpu->io_eir = 0;
	__asm volatile ("sync" ::: "memory");
}

void
itmr_trigger_wrapper(void *unused)
{
	itmr_trigger();
}
