/*
 * Machine dependent access functions for RTC registers.
 */
#ifndef _ASM_MC146818RTC_H
#define _ASM_MC146818RTC_H

#include <asm/io.h>
#include <asm/system.h>
#include <asm/processor.h>
#include <linux/mc146818rtc.h>

#ifndef RTC_PORT
#define RTC_PORT(x)	(0x70 + (x))
#define RTC_ALWAYS_BCD	1	/* RTC operates in binary mode */
#endif

#ifdef __HAVE_ARCH_CMPXCHG
/*
 * This lock provides nmi access to the CMOS/RTC registers.  It has some
 * special properties.  It is owned by a CPU and stores the index register
 * currently being accessed (if owned).  The idea here is that it works
 * like a normal lock (normally).  However, in an NMI, the NMI code will
 * first check to see if its CPU owns the lock, meaning that the NMI
 * interrupted during the read/write of the device.  If it does, it goes ahead
 * and performs the access and then restores the index register.  If it does
 * not, it locks normally.
 *
 * Note that since we are working with NMIs, we need this lock even in
 * a non-SMP machine just to mark that the lock is owned.
 *
 * This only works with compare-and-swap.  There is no other way to
 * atomically claim the lock and set the owner.
 */
#include <linux/smp.h>
extern volatile unsigned long cmos_lock;

/*
 * All of these below must be called with interrupts off, preempt
 * disabled, etc.
 */

static inline void lock_cmos(unsigned char reg)
{
	unsigned long new;
	new = ((smp_processor_id()+1) << 8) | reg;
	for (;;) {
		if (cmos_lock) {
			cpu_relax();
			continue;
		}
		if (__cmpxchg(&cmos_lock, 0, new, sizeof(cmos_lock)) == 0)
			return;
	}
}

static inline void unlock_cmos(void)
{
	cmos_lock = 0;
}
static inline int do_i_have_lock_cmos(void)
{
	return (cmos_lock >> 8) == (smp_processor_id()+1);
}
static inline unsigned char current_lock_cmos_reg(void)
{
	return cmos_lock & 0xff;
}
#define lock_cmos_prefix(reg) \
	do {					\
		unsigned long cmos_flags;	\
		local_irq_save(cmos_flags);	\
		lock_cmos(reg)
#define lock_cmos_suffix(reg) \
		unlock_cmos();			\
		local_irq_restore(cmos_flags);	\
	} while (0)
#else
#define lock_cmos_prefix(reg) do {} while (0)
#define lock_cmos_suffix(reg) do {} while (0)
#define lock_cmos(reg)
#define unlock_cmos()
#define do_i_have_lock_cmos() 0
#define current_lock_cmos_reg() 0
#endif

/*
 * The yet supported machines all access the RTC index register via
 * an ISA port access but the way to access the date register differs ...
 */
#define CMOS_READ(addr) rtc_cmos_read(addr)
#define CMOS_WRITE(val, addr) rtc_cmos_write(val, addr)
unsigned char rtc_cmos_read(unsigned char addr);
void rtc_cmos_write(unsigned char val, unsigned char addr);

#define RTC_IRQ 8

#endif /* _ASM_MC146818RTC_H */
