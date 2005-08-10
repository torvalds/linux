/*
 * Common time prototypes and such for all ppc machines.
 *
 * Written by Cort Dougan (cort@cs.nmt.edu) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef __PPC64_TIME_H
#define __PPC64_TIME_H

#ifdef __KERNEL__
#include <linux/config.h>
#include <linux/types.h>
#include <linux/mc146818rtc.h>

#include <asm/processor.h>
#include <asm/paca.h>
#include <asm/iSeries/HvCall.h>

/* time.c */
extern unsigned long tb_ticks_per_jiffy;
extern unsigned long tb_ticks_per_usec;
extern unsigned long tb_ticks_per_sec;
extern unsigned long tb_to_xs;
extern unsigned      tb_to_us;
extern unsigned long tb_last_stamp;

struct rtc_time;
extern void to_tm(int tim, struct rtc_time * tm);
extern time_t last_rtc_update;

void generic_calibrate_decr(void);
void setup_default_decr(void);

/* Some sane defaults: 125 MHz timebase, 1GHz processor */
extern unsigned long ppc_proc_freq;
#define DEFAULT_PROC_FREQ	(DEFAULT_TB_FREQ * 8)
extern unsigned long ppc_tb_freq;
#define DEFAULT_TB_FREQ		125000000UL

/*
 * By putting all of this stuff into a single struct we 
 * reduce the number of cache lines touched by do_gettimeofday.
 * Both by collecting all of the data in one cache line and
 * by touching only one TOC entry
 */
struct gettimeofday_vars {
	unsigned long tb_to_xs;
	unsigned long stamp_xsec;
	unsigned long tb_orig_stamp;
};

struct gettimeofday_struct {
	unsigned long tb_ticks_per_sec;
	struct gettimeofday_vars vars[2];
	struct gettimeofday_vars * volatile varp;
	unsigned      var_idx;
	unsigned      tb_to_us;
};

struct div_result {
	unsigned long result_high;
	unsigned long result_low;
};

int via_calibrate_decr(void);

static __inline__ unsigned long get_tb(void)
{
	return mftb();
}

/* Accessor functions for the decrementer register. */
static __inline__ unsigned int get_dec(void)
{
	return (mfspr(SPRN_DEC));
}

static __inline__ void set_dec(int val)
{
#ifdef CONFIG_PPC_ISERIES
	struct paca_struct *lpaca = get_paca();
	int cur_dec;

	if (lpaca->lppaca.shared_proc) {
		lpaca->lppaca.virtual_decr = val;
		cur_dec = get_dec();
		if (cur_dec > val)
			HvCall_setVirtualDecr();
	} else
#endif
		mtspr(SPRN_DEC, val);
}

static inline unsigned long tb_ticks_since(unsigned long tstamp)
{
	return get_tb() - tstamp;
}

#define mulhwu(x,y) \
({unsigned z; asm ("mulhwu %0,%1,%2" : "=r" (z) : "r" (x), "r" (y)); z;})
#define mulhdu(x,y) \
({unsigned long z; asm ("mulhdu %0,%1,%2" : "=r" (z) : "r" (x), "r" (y)); z;})


unsigned mulhwu_scale_factor(unsigned, unsigned);
void div128_by_32( unsigned long dividend_high, unsigned long dividend_low,
		   unsigned divisor, struct div_result *dr );

/* Used to store Processor Utilization register (purr) values */

struct cpu_usage {
        u64 current_tb;  /* Holds the current purr register values */
};

DECLARE_PER_CPU(struct cpu_usage, cpu_usage_array);

#endif /* __KERNEL__ */
#endif /* __PPC64_TIME_H */
