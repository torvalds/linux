/*
 * Common time prototypes and such for all ppc machines.
 *
 * Written by Cort Dougan (cort@fsmlabs.com) to merge
 * Paul Mackerras' version and mine for PReP and Pmac.
 */

#ifdef __KERNEL__
#ifndef __ASM_TIME_H__
#define __ASM_TIME_H__

#include <linux/types.h>
#include <linux/rtc.h>
#include <linux/threads.h>

#include <asm/reg.h>

/* time.c */
extern unsigned tb_ticks_per_jiffy;
extern unsigned tb_to_us;
extern unsigned tb_last_stamp;
extern unsigned long disarm_decr[NR_CPUS];

extern void to_tm(int tim, struct rtc_time * tm);
extern time_t last_rtc_update;

extern void set_dec_cpu6(unsigned int val);

int via_calibrate_decr(void);

/* Accessor functions for the decrementer register.
 * The 4xx doesn't even have a decrementer.  I tried to use the
 * generic timer interrupt code, which seems OK, with the 4xx PIT
 * in auto-reload mode.  The problem is PIT stops counting when it
 * hits zero.  If it would wrap, we could use it just like a decrementer.
 */
static __inline__ unsigned int get_dec(void)
{
#if defined(CONFIG_40x)
	return (mfspr(SPRN_PIT));
#else
	return (mfspr(SPRN_DEC));
#endif
}

static __inline__ void set_dec(unsigned int val)
{
#if defined(CONFIG_40x)
	return;		/* Have to let it auto-reload */
#elif defined(CONFIG_8xx_CPU6)
	set_dec_cpu6(val);
#else
	mtspr(SPRN_DEC, val);
#endif
}

/* Accessor functions for the timebase (RTC on 601) registers. */
/* If one day CONFIG_POWER is added just define __USE_RTC as 1 */
#ifdef CONFIG_6xx
extern __inline__ int __attribute_pure__ __USE_RTC(void) {
	return (mfspr(SPRN_PVR)>>16) == 1;
}
#else
#define __USE_RTC() 0
#endif

extern __inline__ unsigned long get_tbl(void) {
	unsigned long tbl;
#if defined(CONFIG_403GCX)
	asm volatile("mfspr %0, 0x3dd" : "=r" (tbl));
#else
	asm volatile("mftb %0" : "=r" (tbl));
#endif
	return tbl;
}

extern __inline__ unsigned long get_tbu(void) {
	unsigned long tbl;
#if defined(CONFIG_403GCX)
	asm volatile("mfspr %0, 0x3dc" : "=r" (tbl));
#else
	asm volatile("mftbu %0" : "=r" (tbl));
#endif
	return tbl;
}

extern __inline__ void set_tb(unsigned int upper, unsigned int lower)
{
	mtspr(SPRN_TBWL, 0);
	mtspr(SPRN_TBWU, upper);
	mtspr(SPRN_TBWL, lower);
}

extern __inline__ unsigned long get_rtcl(void) {
	unsigned long rtcl;
	asm volatile("mfrtcl %0" : "=r" (rtcl));
	return rtcl;
}

extern __inline__ unsigned long get_rtcu(void)
{
	unsigned long rtcu;
	asm volatile("mfrtcu %0" : "=r" (rtcu));
	return rtcu;
}

extern __inline__ unsigned get_native_tbl(void) {
	if (__USE_RTC())
		return get_rtcl();
	else
	  	return get_tbl();
}

/* On machines with RTC, this function can only be used safely
 * after the timestamp and for 1 second. It is only used by gettimeofday
 * however so it should not matter.
 */
extern __inline__ unsigned tb_ticks_since(unsigned tstamp) {
	if (__USE_RTC()) {
		int delta = get_rtcl() - tstamp;
		return delta<0 ? delta + 1000000000 : delta;
	} else {
        	return get_tbl() - tstamp;
	}
}

#if 0
extern __inline__ unsigned long get_bin_rtcl(void) {
      unsigned long rtcl, rtcu1, rtcu2;
      asm volatile("\
1:    mfrtcu  %0\n\
      mfrtcl  %1\n\
      mfrtcu  %2\n\
      cmpw    %0,%2\n\
      bne-    1b\n"
      : "=r" (rtcu1), "=r" (rtcl), "=r" (rtcu2)
      : : "cr0");
      return rtcu2*1000000000+rtcl;
}

extern __inline__ unsigned binary_tbl(void) {
      if (__USE_RTC())
              return get_bin_rtcl();
      else
              return get_tbl();
}
#endif

/* Use mulhwu to scale processor timebase to timeval */
/* Specifically, this computes (x * y) / 2^32.  -- paulus */
#define mulhwu(x,y) \
({unsigned z; asm ("mulhwu %0,%1,%2" : "=r" (z) : "r" (x), "r" (y)); z;})

unsigned mulhwu_scale_factor(unsigned, unsigned);

#define account_process_vtime(tsk)		do { } while (0)
#define calculate_steal_time()			do { } while (0)
#define snapshot_timebases()			do { } while (0)

#endif /* __ASM_TIME_H__ */
#endif /* __KERNEL__ */
