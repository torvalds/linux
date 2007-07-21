
#ifndef _I386_HPET_H
#define _I386_HPET_H

#ifdef CONFIG_HPET_TIMER

/*
 * Documentation on HPET can be found at:
 *      http://www.intel.com/ial/home/sp/pcmmspec.htm
 *      ftp://download.intel.com/ial/home/sp/mmts098.pdf
 */

#define HPET_MMAP_SIZE		1024

#define HPET_ID			0x000
#define HPET_PERIOD		0x004
#define HPET_CFG		0x010
#define HPET_STATUS		0x020
#define HPET_COUNTER		0x0f0
#define HPET_T0_CFG		0x100
#define HPET_T0_CMP		0x108
#define HPET_T0_ROUTE		0x110
#define HPET_T1_CFG		0x120
#define HPET_T1_CMP		0x128
#define HPET_T1_ROUTE		0x130
#define HPET_T2_CFG		0x140
#define HPET_T2_CMP		0x148
#define HPET_T2_ROUTE		0x150

#define HPET_ID_REV		0x000000ff
#define HPET_ID_NUMBER		0x00001f00
#define HPET_ID_64BIT		0x00002000
#define HPET_ID_LEGSUP		0x00008000
#define HPET_ID_VENDOR		0xffff0000
#define	HPET_ID_NUMBER_SHIFT	8
#define HPET_ID_VENDOR_SHIFT	16

#define HPET_ID_VENDOR_8086	0x8086

#define HPET_CFG_ENABLE		0x001
#define HPET_CFG_LEGACY		0x002
#define	HPET_LEGACY_8254	2
#define	HPET_LEGACY_RTC		8

#define HPET_TN_LEVEL		0x0002
#define HPET_TN_ENABLE		0x0004
#define HPET_TN_PERIODIC	0x0008
#define HPET_TN_PERIODIC_CAP	0x0010
#define HPET_TN_64BIT_CAP	0x0020
#define HPET_TN_SETVAL		0x0040
#define HPET_TN_32BIT		0x0100
#define HPET_TN_ROUTE		0x3e00
#define HPET_TN_FSB		0x4000
#define HPET_TN_FSB_CAP		0x8000
#define HPET_TN_ROUTE_SHIFT	9

/* Max HPET Period is 10^8 femto sec as in HPET spec */
#define HPET_MAX_PERIOD		100000000UL
/*
 * Min HPET period is 10^5 femto sec just for safety. If it is less than this,
 * then 32 bit HPET counter wrapsaround in less than 0.5 sec.
 */
#define HPET_MIN_PERIOD		100000UL

/* hpet memory map physical address */
extern unsigned long hpet_address;
extern int is_hpet_enabled(void);
extern int hpet_enable(void);

#ifdef CONFIG_HPET_EMULATE_RTC

#include <linux/interrupt.h>

extern int hpet_mask_rtc_irq_bit(unsigned long bit_mask);
extern int hpet_set_rtc_irq_bit(unsigned long bit_mask);
extern int hpet_set_alarm_time(unsigned char hrs, unsigned char min,
			       unsigned char sec);
extern int hpet_set_periodic_freq(unsigned long freq);
extern int hpet_rtc_dropped_irq(void);
extern int hpet_rtc_timer_init(void);
extern irqreturn_t hpet_rtc_interrupt(int irq, void *dev_id);

#endif /* CONFIG_HPET_EMULATE_RTC */

#else

static inline int hpet_enable(void) { return 0; }

#endif /* CONFIG_HPET_TIMER */
#endif /* _I386_HPET_H */
