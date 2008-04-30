/*
 * include/linux/irqflags.h
 *
 * IRQ flags tracing: follow the state of the hardirq and softirq flags and
 * provide callbacks for transitions between ON and OFF states.
 *
 * This file gets included from lowlevel asm headers too, to provide
 * wrapped versions of the local_irq_*() APIs, based on the
 * raw_local_irq_*() macros from the lowlevel headers.
 */
#ifndef _LINUX_TRACE_IRQFLAGS_H
#define _LINUX_TRACE_IRQFLAGS_H

#ifdef CONFIG_TRACE_IRQFLAGS
  extern void trace_hardirqs_on(void);
  extern void trace_hardirqs_off(void);
  extern void trace_softirqs_on(unsigned long ip);
  extern void trace_softirqs_off(unsigned long ip);
# define trace_hardirq_context(p)	((p)->hardirq_context)
# define trace_softirq_context(p)	((p)->softirq_context)
# define trace_hardirqs_enabled(p)	((p)->hardirqs_enabled)
# define trace_softirqs_enabled(p)	((p)->softirqs_enabled)
# define trace_hardirq_enter()	do { current->hardirq_context++; } while (0)
# define trace_hardirq_exit()	do { current->hardirq_context--; } while (0)
# define trace_softirq_enter()	do { current->softirq_context++; } while (0)
# define trace_softirq_exit()	do { current->softirq_context--; } while (0)
# define INIT_TRACE_IRQFLAGS	.softirqs_enabled = 1,
#else
# define trace_hardirqs_on()		do { } while (0)
# define trace_hardirqs_off()		do { } while (0)
# define trace_softirqs_on(ip)		do { } while (0)
# define trace_softirqs_off(ip)		do { } while (0)
# define trace_hardirq_context(p)	0
# define trace_softirq_context(p)	0
# define trace_hardirqs_enabled(p)	0
# define trace_softirqs_enabled(p)	0
# define trace_hardirq_enter()		do { } while (0)
# define trace_hardirq_exit()		do { } while (0)
# define trace_softirq_enter()		do { } while (0)
# define trace_softirq_exit()		do { } while (0)
# define INIT_TRACE_IRQFLAGS
#endif

#ifdef CONFIG_TRACE_IRQFLAGS_SUPPORT

#include <asm/irqflags.h>

#define local_irq_enable() \
	do { trace_hardirqs_on(); raw_local_irq_enable(); } while (0)
#define local_irq_disable() \
	do { raw_local_irq_disable(); trace_hardirqs_off(); } while (0)
#define local_irq_save(flags) \
	do { raw_local_irq_save(flags); trace_hardirqs_off(); } while (0)

#define local_irq_restore(flags)				\
	do {							\
		if (raw_irqs_disabled_flags(flags)) {		\
			raw_local_irq_restore(flags);		\
			trace_hardirqs_off();			\
		} else {					\
			trace_hardirqs_on();			\
			raw_local_irq_restore(flags);		\
		}						\
	} while (0)
#else /* !CONFIG_TRACE_IRQFLAGS_SUPPORT */
/*
 * The local_irq_*() APIs are equal to the raw_local_irq*()
 * if !TRACE_IRQFLAGS.
 */
# define raw_local_irq_disable()	local_irq_disable()
# define raw_local_irq_enable()		local_irq_enable()
# define raw_local_irq_save(flags)	local_irq_save(flags)
# define raw_local_irq_restore(flags)	local_irq_restore(flags)
#endif /* CONFIG_TRACE_IRQFLAGS_SUPPORT */

#ifdef CONFIG_TRACE_IRQFLAGS_SUPPORT
#define safe_halt()						\
	do {							\
		trace_hardirqs_on();				\
		raw_safe_halt();				\
	} while (0)

#define local_save_flags(flags)		raw_local_save_flags(flags)

#define irqs_disabled()						\
({								\
	unsigned long _flags;					\
								\
	raw_local_save_flags(_flags);				\
	raw_irqs_disabled_flags(_flags);			\
})

#define irqs_disabled_flags(flags)	raw_irqs_disabled_flags(flags)
#endif		/* CONFIG_X86 */

#endif
