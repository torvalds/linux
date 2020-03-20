/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LIBLOCKDEP_LINUX_TRACE_IRQFLAGS_H_
#define _LIBLOCKDEP_LINUX_TRACE_IRQFLAGS_H_

# define lockdep_hardirq_context(p)	0
# define lockdep_softirq_context(p)	0
# define lockdep_hardirqs_enabled(p)	0
# define lockdep_softirqs_enabled(p)	0
# define lockdep_hardirq_enter()	do { } while (0)
# define lockdep_hardirq_exit()		do { } while (0)
# define lockdep_softirq_enter()	do { } while (0)
# define lockdep_softirq_exit()		do { } while (0)
# define INIT_TRACE_IRQFLAGS

# define stop_critical_timings() do { } while (0)
# define start_critical_timings() do { } while (0)

#define raw_local_irq_disable() do { } while (0)
#define raw_local_irq_enable() do { } while (0)
#define raw_local_irq_save(flags) ((flags) = 0)
#define raw_local_irq_restore(flags) ((void)(flags))
#define raw_local_save_flags(flags) ((flags) = 0)
#define raw_irqs_disabled_flags(flags) ((void)(flags))
#define raw_irqs_disabled() 0
#define raw_safe_halt()

#define local_irq_enable() do { } while (0)
#define local_irq_disable() do { } while (0)
#define local_irq_save(flags) ((flags) = 0)
#define local_irq_restore(flags) ((void)(flags))
#define local_save_flags(flags)	((flags) = 0)
#define irqs_disabled() (1)
#define irqs_disabled_flags(flags) ((void)(flags), 0)
#define safe_halt() do { } while (0)

#define trace_lock_release(x, y)
#define trace_lock_acquire(a, b, c, d, e, f, g)

#endif
