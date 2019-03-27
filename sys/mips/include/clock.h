/*
 * Garrett Wollman, September 1994.
 * This file is in the public domain.
 * Kernel interface to machine-dependent clock driver.
 *
 *	JNPR: clock.h,v 1.6.2.1 2007/08/29 09:36:05 girish
 *	from: src/sys/alpha/include/clock.h,v 1.5 1999/12/29 04:27:55 peter
 * $FreeBSD$
 */

#ifndef _MACHINE_CLOCK_H_
#define	_MACHINE_CLOCK_H_

#include <sys/bus.h>

#ifdef _KERNEL

extern int cpu_clock;

#define wall_cmos_clock 0
#define adjkerntz 0

/*
 * Default is to assume a CPU pipeline clock of 100Mhz, and
 * that CP0_COUNT increments every 2 cycles.
 */
#define MIPS_DEFAULT_HZ		(100 * 1000 * 1000)

void	mips_timer_early_init(uint64_t clock_hz);
void	mips_timer_init_params(uint64_t, int);

extern uint64_t	counter_freq;
extern int	clocks_running;

/*
 * The 'platform_timecounter' pointer may be used to register a
 * platform-specific timecounter.
 *
 * A default timecounter based on the CP0 COUNT register is always registered.
 */
extern struct timecounter *platform_timecounter;

#endif

#endif /* !_MACHINE_CLOCK_H_ */
