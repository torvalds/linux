
#ifndef _I386_HPET_H
#define _I386_HPET_H

#ifdef CONFIG_HPET_TIMER

#include <linux/errno.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/param.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/smp.h>

#include <asm/io.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/msr.h>
#include <asm/delay.h>
#include <asm/mpspec.h>
#include <asm/uaccess.h>
#include <asm/processor.h>

#include <linux/timex.h>

/*
 * Documentation on HPET can be found at:
 *      http://www.intel.com/ial/home/sp/pcmmspec.htm
 *      ftp://download.intel.com/ial/home/sp/mmts098.pdf
 */

#define HPET_MMAP_SIZE	1024

#define HPET_ID		0x000
#define HPET_PERIOD	0x004
#define HPET_CFG	0x010
#define HPET_STATUS	0x020
#define HPET_COUNTER	0x0f0
#define HPET_T0_CFG	0x100
#define HPET_T0_CMP	0x108
#define HPET_T0_ROUTE	0x110
#define HPET_T1_CFG	0x120
#define HPET_T1_CMP	0x128
#define HPET_T1_ROUTE	0x130
#define HPET_T2_CFG	0x140
#define HPET_T2_CMP	0x148
#define HPET_T2_ROUTE	0x150

#define HPET_ID_LEGSUP	0x00008000
#define HPET_ID_NUMBER	0x00001f00
#define HPET_ID_REV	0x000000ff
#define	HPET_ID_NUMBER_SHIFT	8

#define HPET_CFG_ENABLE	0x001
#define HPET_CFG_LEGACY	0x002
#define	HPET_LEGACY_8254	2
#define	HPET_LEGACY_RTC		8

#define HPET_TN_ENABLE		0x004
#define HPET_TN_PERIODIC	0x008
#define HPET_TN_PERIODIC_CAP	0x010
#define HPET_TN_SETVAL		0x040
#define HPET_TN_32BIT		0x100

/* Use our own asm for 64 bit multiply/divide */
#define ASM_MUL64_REG(eax_out,edx_out,reg_in,eax_in) 			\
		__asm__ __volatile__("mull %2" 				\
				:"=a" (eax_out), "=d" (edx_out) 	\
				:"r" (reg_in), "0" (eax_in))

#define ASM_DIV64_REG(eax_out,edx_out,reg_in,eax_in,edx_in) 		\
		__asm__ __volatile__("divl %2" 				\
				:"=a" (eax_out), "=d" (edx_out) 	\
				:"r" (reg_in), "0" (eax_in), "1" (edx_in))

#define KERNEL_TICK_USEC 	(1000000UL/HZ)	/* tick value in microsec */
/* Max HPET Period is 10^8 femto sec as in HPET spec */
#define HPET_MAX_PERIOD (100000000UL)
/*
 * Min HPET period is 10^5 femto sec just for safety. If it is less than this,
 * then 32 bit HPET counter wrapsaround in less than 0.5 sec.
 */
#define HPET_MIN_PERIOD (100000UL)
#define HPET_TICK_RATE  (HZ * 100000UL)

extern unsigned long hpet_address;	/* hpet memory map physical address */
extern int is_hpet_enabled(void);

#ifdef CONFIG_X86_64
extern unsigned long hpet_tick;	/* hpet clks count per tick */
extern int hpet_use_timer;
extern int hpet_rtc_timer_init(void);
extern int hpet_enable(void);
extern int is_hpet_capable(void);
extern int hpet_readl(unsigned long a);
#else
extern int hpet_enable(void);
#endif

#ifdef CONFIG_HPET_EMULATE_RTC
extern int hpet_mask_rtc_irq_bit(unsigned long bit_mask);
extern int hpet_set_rtc_irq_bit(unsigned long bit_mask);
extern int hpet_set_alarm_time(unsigned char hrs, unsigned char min, unsigned char sec);
extern int hpet_set_periodic_freq(unsigned long freq);
extern int hpet_rtc_dropped_irq(void);
extern int hpet_rtc_timer_init(void);
extern irqreturn_t hpet_rtc_interrupt(int irq, void *dev_id);
#endif /* CONFIG_HPET_EMULATE_RTC */

#else

static inline int hpet_enable(void) { return 0; }

#endif /* CONFIG_HPET_TIMER */
#endif /* _I386_HPET_H */
