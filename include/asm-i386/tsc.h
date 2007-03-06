/*
 * linux/include/asm-i386/tsc.h
 *
 * i386 TSC related functions
 */
#ifndef _ASM_i386_TSC_H
#define _ASM_i386_TSC_H

#include <asm/processor.h>

/*
 * Standard way to access the cycle counter.
 */
typedef unsigned long long cycles_t;

extern unsigned int cpu_khz;
extern unsigned int tsc_khz;

static inline cycles_t get_cycles(void)
{
	unsigned long long ret = 0;

#ifndef CONFIG_X86_TSC
	if (!cpu_has_tsc)
		return 0;
#endif

#if defined(CONFIG_X86_GENERIC) || defined(CONFIG_X86_TSC)
	rdtscll(ret);
#endif
	return ret;
}

/* Like get_cycles, but make sure the CPU is synchronized. */
static __always_inline cycles_t get_cycles_sync(void)
{
	unsigned long long ret;
#ifdef X86_FEATURE_SYNC_RDTSC
	unsigned eax;

	/*
	 * Don't do an additional sync on CPUs where we know
	 * RDTSC is already synchronous:
	 */
	alternative_io("cpuid", ASM_NOP2, X86_FEATURE_SYNC_RDTSC,
			  "=a" (eax), "0" (1) : "ebx","ecx","edx","memory");
#else
	sync_core();
#endif
	rdtscll(ret);

	return ret;
}

extern void tsc_init(void);
extern void mark_tsc_unstable(void);
extern int unsynchronized_tsc(void);
extern void init_tsc_clocksource(void);

/*
 * Boot-time check whether the TSCs are synchronized across
 * all CPUs/cores:
 */
extern void check_tsc_sync_source(int cpu);
extern void check_tsc_sync_target(void);

#endif
