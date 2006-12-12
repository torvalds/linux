/*
 *  linux/include/linux/nmi.h
 */
#ifndef LINUX_NMI_H
#define LINUX_NMI_H

#include <linux/sched.h>
#include <asm/irq.h>

/**
 * touch_nmi_watchdog - restart NMI watchdog timeout.
 * 
 * If the architecture supports the NMI watchdog, touch_nmi_watchdog()
 * may be used to reset the timeout - for code which intentionally
 * disables interrupts for a long time. This call is stateless.
 */
#ifdef ARCH_HAS_NMI_WATCHDOG
#include <asm/nmi.h>
extern void touch_nmi_watchdog(void);
#else
# define touch_nmi_watchdog() touch_softlockup_watchdog()
#endif

#ifndef trigger_all_cpu_backtrace
#define trigger_all_cpu_backtrace() do { } while (0)
#endif

#endif
