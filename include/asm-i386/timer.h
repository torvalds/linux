#ifndef _ASMi386_TIMER_H
#define _ASMi386_TIMER_H
#include <linux/init.h>
#include <linux/pm.h>

#define TICK_SIZE (tick_nsec / 1000)

void setup_pit_timer(void);
unsigned long long native_sched_clock(void);
unsigned long native_calculate_cpu_khz(void);

extern int timer_ack;
extern int no_timer_check;
extern int no_sync_cmos_clock;
extern int recalibrate_cpu_khz(void);

#ifndef CONFIG_PARAVIRT
#define calculate_cpu_khz() native_calculate_cpu_khz()
#endif

/* Accellerators for sched_clock()
 * convert from cycles(64bits) => nanoseconds (64bits)
 *  basic equation:
 *		ns = cycles / (freq / ns_per_sec)
 *		ns = cycles * (ns_per_sec / freq)
 *		ns = cycles * (10^9 / (cpu_khz * 10^3))
 *		ns = cycles * (10^6 / cpu_khz)
 *
 *	Then we use scaling math (suggested by george@mvista.com) to get:
 *		ns = cycles * (10^6 * SC / cpu_khz) / SC
 *		ns = cycles * cyc2ns_scale / SC
 *
 *	And since SC is a constant power of two, we can convert the div
 *  into a shift.
 *
 *  We can use khz divisor instead of mhz to keep a better percision, since
 *  cyc2ns_scale is limited to 10^6 * 2^10, which fits in 32 bits.
 *  (mathieu.desnoyers@polymtl.ca)
 *
 *			-johnstul@us.ibm.com "math is hard, lets go shopping!"
 */
extern unsigned long cyc2ns_scale __read_mostly;

#define CYC2NS_SCALE_FACTOR 10 /* 2^10, carefully chosen */

static inline unsigned long long cycles_2_ns(unsigned long long cyc)
{
	return (cyc * cyc2ns_scale) >> CYC2NS_SCALE_FACTOR;
}


#endif
