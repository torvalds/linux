/*	$OpenBSD: clock.c,v 1.58 2023/09/17 14:50:51 cheloha Exp $	*/
/*	$NetBSD: clock.c,v 1.1 1996/09/30 16:34:40 ws Exp $	*/

/*
 * Copyright (C) 1995, 1996 Wolfgang Solfrank.
 * Copyright (C) 1995, 1996 TooLs GmbH.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by TooLs GmbH.
 * 4. The name of TooLs GmbH may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY TOOLS GMBH ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL TOOLS GMBH BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/clockintr.h>
#include <sys/evcount.h>
#include <sys/stdint.h>
#include <sys/timetc.h>

#include <machine/autoconf.h>
#include <machine/pio.h>
#include <machine/intr.h>
#include <machine/vmparam.h>

#include <dev/clock_subr.h>
#include <dev/ofw/openfirm.h>

void dec_rearm(void *, uint64_t);
void dec_trigger(void *);
void decr_intr(struct clockframe *frame);
u_int tb_get_timecount(struct timecounter *);

/*
 * Initially we assume a processor with a bus frequency of 12.5 MHz.
 */
u_int32_t ticks_per_sec = 3125000;
u_int32_t ns_per_tick = 320;
uint64_t dec_nsec_cycle_ratio;
uint64_t dec_nsec_max;
int clock_initialized;

const struct intrclock dec_intrclock = {
	.ic_rearm = dec_rearm,
	.ic_trigger = dec_trigger
};

static struct timecounter tb_timecounter = {
	.tc_get_timecount = tb_get_timecount,
	.tc_counter_mask = 0xffffffff,
	.tc_frequency = 0,
	.tc_name = "tb",
	.tc_quality = 0,
	.tc_priv = NULL,
	.tc_user = TC_TB,
};

/* calibrate the timecounter frequency for the listed models */
static const char *calibrate_tc_models[] = {
	"PowerMac10,1"
};

time_read_t  *time_read;
time_write_t *time_write;

static struct evcount clk_count;
static int clk_irq = PPC_CLK_IRQ;

extern todr_chip_handle_t todr_handle;
struct todr_chip_handle rtc_todr;

int
rtc_gettime(struct todr_chip_handle *handle, struct timeval *tv)
{
	time_t sec;

	if (time_read == NULL)
		return ENXIO;

	(*time_read)(&sec);
	tv->tv_sec = sec - utc_offset;
	tv->tv_usec = 0;
	return 0;
}

int
rtc_settime(struct todr_chip_handle *handle, struct timeval *tv)
{
	if (time_write == NULL)
		return ENXIO;

	(*time_write)(tv->tv_sec + utc_offset);
	return 0;
}

void
decr_intr(struct clockframe *frame)
{
	struct cpu_info *ci = curcpu();
	int s;

	if (!clock_initialized)
		return;

	clk_count.ec_count++;		/* XXX not atomic */

	ppc_mtdec(UINT32_MAX >> 1);	/* clear DEC exception */

	/*
	 * We can't actually mask DEC interrupts at or above IPL_CLOCK
	 * without masking other essential interrupts.  To simulate
	 * masking, we retrigger the DEC by hand from splx(9) the next
	 * time our IPL drops below IPL_CLOCK.
	 */
	if (ci->ci_cpl >= IPL_CLOCK) {
		ci->ci_dec_deferred = 1;
		return;
	}
	ci->ci_dec_deferred = 0;

	s = splclock();
	ppc_intr_enable(1);
	clockintr_dispatch(frame);
	splx(s);
	(void) ppc_intr_disable();
}

void
cpu_initclocks(void)
{
	int intrstate;
	u_int32_t first_tb, second_tb;
	time_t first_sec, sec;
	int calibrate = 0, n;

	/* check if we should calibrate the timecounter frequency */
	for (n = 0; n < sizeof(calibrate_tc_models) /
	    sizeof(calibrate_tc_models[0]); n++) {
		if (!strcmp(calibrate_tc_models[n], hw_prod)) {
			calibrate = 1;
			break;
		}
	}

	/* if a RTC is available, calibrate the timecounter frequency */
	if (calibrate && time_read != NULL) {
		time_read(&first_sec);
		do {
			first_tb = ppc_mftbl();
			time_read(&sec);
		} while (sec == first_sec);
		first_sec = sec;
		do {
			second_tb = ppc_mftbl();
			time_read(&sec);
		} while (sec == first_sec);
		ticks_per_sec = second_tb - first_tb;
#ifdef DEBUG
		printf("tb: using measured timecounter frequency of %ld Hz\n",
		    ticks_per_sec);
#endif
	}
	ns_per_tick = 1000000000 / ticks_per_sec;

	tb_timecounter.tc_frequency = ticks_per_sec;
	tc_init(&tb_timecounter);

	rtc_todr.todr_gettime = rtc_gettime;
	rtc_todr.todr_settime = rtc_settime;
	todr_handle = &rtc_todr;

	intrstate = ppc_intr_disable();

	stathz = hz;
	profhz = stathz * 10;
	statclock_is_randomized = 1;

	dec_nsec_cycle_ratio = ticks_per_sec * (1ULL << 32) / 1000000000;
	dec_nsec_max = UINT64_MAX / dec_nsec_cycle_ratio;

	evcount_attach(&clk_count, "clock", &clk_irq);

	ppc_intr_enable(intrstate);
}

void
cpu_startclock(void)
{
	clock_initialized = 1;
	clockintr_cpu_init(&dec_intrclock);
	clockintr_trigger();
}

/*
 * Wait for about n microseconds (us) (at least!).
 */
void
delay(unsigned n)
{
	u_int64_t tb;

	tb = ppc_mftb();
	tb += (n * 1000 + ns_per_tick - 1) / ns_per_tick;
	while (tb > ppc_mftb())
		;
}

void
setstatclockrate(int newhz)
{
}

u_int
tb_get_timecount(struct timecounter *tc)
{
	return ppc_mftbl();
}

void
dec_rearm(void *unused, uint64_t nsecs)
{
	uint32_t cycles;

	if (nsecs > dec_nsec_max)
		nsecs = dec_nsec_max;
	cycles = (nsecs * dec_nsec_cycle_ratio) >> 32;
	if (cycles > UINT32_MAX >> 1)
		cycles = UINT32_MAX >> 1;
	ppc_mtdec(cycles);
}

void
dec_trigger(void *unused)
{
	int s;

	s = ppc_intr_disable();
	ppc_mtdec(0);
	ppc_mtdec(UINT32_MAX);
	ppc_intr_enable(s);
}
