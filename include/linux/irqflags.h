/* SPDX-License-Identifier: GPL-2.0 */
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

#include <linux/typecheck.h>
#include <asm/irqflags.h>

/* Currently lockdep_softirqs_on/off is used only by lockdep */
#ifdef CONFIG_PROVE_LOCKING
  extern void lockdep_softirqs_on(unsigned long ip);
  extern void lockdep_softirqs_off(unsigned long ip);
  extern void lockdep_hardirqs_on_prepare(unsigned long ip);
  extern void lockdep_hardirqs_on(unsigned long ip);
  extern void lockdep_hardirqs_off(unsigned long ip);
#else
  static inline void lockdep_softirqs_on(unsigned long ip) { }
  static inline void lockdep_softirqs_off(unsigned long ip) { }
  static inline void lockdep_hardirqs_on_prepare(unsigned long ip) { }
  static inline void lockdep_hardirqs_on(unsigned long ip) { }
  static inline void lockdep_hardirqs_off(unsigned long ip) { }
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
  extern void trace_hardirqs_on_prepare(void);
  extern void trace_hardirqs_off_finish(void);
  extern void trace_hardirqs_on(void);
  extern void trace_hardirqs_off(void);
# define lockdep_hardirq_context(p)	((p)->hardirq_context)
# define lockdep_softirq_context(p)	((p)->softirq_context)
# define lockdep_hardirqs_enabled(p)	((p)->hardirqs_enabled)
# define lockdep_softirqs_enabled(p)	((p)->softirqs_enabled)
# define lockdep_hardirq_enter()		\
do {						\
	if (!current->hardirq_context++)	\
		current->hardirq_threaded = 0;	\
} while (0)
# define lockdep_hardirq_threaded()		\
do {						\
	current->hardirq_threaded = 1;		\
} while (0)
# define lockdep_hardirq_exit()			\
do {						\
	current->hardirq_context--;		\
} while (0)
# define lockdep_softirq_enter()		\
do {						\
	current->softirq_context++;		\
} while (0)
# define lockdep_softirq_exit()			\
do {						\
	current->softirq_context--;		\
} while (0)

# define lockdep_hrtimer_enter(__hrtimer)		\
({							\
	bool __expires_hardirq = true;			\
							\
	if (!__hrtimer->is_hard) {			\
		current->irq_config = 1;		\
		__expires_hardirq = false;		\
	}						\
	__expires_hardirq;				\
})

# define lockdep_hrtimer_exit(__expires_hardirq)	\
	do {						\
		if (!__expires_hardirq)			\
			current->irq_config = 0;	\
	} while (0)

# define lockdep_posixtimer_enter()				\
	  do {							\
		  current->irq_config = 1;			\
	  } while (0)

# define lockdep_posixtimer_exit()				\
	  do {							\
		  current->irq_config = 0;			\
	  } while (0)

# define lockdep_irq_work_enter(__work)					\
	  do {								\
		  if (!(atomic_read(&__work->flags) & IRQ_WORK_HARD_IRQ))\
			current->irq_config = 1;			\
	  } while (0)
# define lockdep_irq_work_exit(__work)					\
	  do {								\
		  if (!(atomic_read(&__work->flags) & IRQ_WORK_HARD_IRQ))\
			current->irq_config = 0;			\
	  } while (0)

#else
# define trace_hardirqs_on_prepare()		do { } while (0)
# define trace_hardirqs_off_finish()		do { } while (0)
# define trace_hardirqs_on()		do { } while (0)
# define trace_hardirqs_off()		do { } while (0)
# define lockdep_hardirq_context(p)	0
# define lockdep_softirq_context(p)	0
# define lockdep_hardirqs_enabled(p)	0
# define lockdep_softirqs_enabled(p)	0
# define lockdep_hardirq_enter()	do { } while (0)
# define lockdep_hardirq_threaded()	do { } while (0)
# define lockdep_hardirq_exit()		do { } while (0)
# define lockdep_softirq_enter()	do { } while (0)
# define lockdep_softirq_exit()		do { } while (0)
# define lockdep_hrtimer_enter(__hrtimer)	false
# define lockdep_hrtimer_exit(__context)	do { } while (0)
# define lockdep_posixtimer_enter()		do { } while (0)
# define lockdep_posixtimer_exit()		do { } while (0)
# define lockdep_irq_work_enter(__work)		do { } while (0)
# define lockdep_irq_work_exit(__work)		do { } while (0)
#endif

#if defined(CONFIG_IRQSOFF_TRACER) || \
	defined(CONFIG_PREEMPT_TRACER)
 extern void stop_critical_timings(void);
 extern void start_critical_timings(void);
#else
# define stop_critical_timings() do { } while (0)
# define start_critical_timings() do { } while (0)
#endif

/*
 * Wrap the arch provided IRQ routines to provide appropriate checks.
 */
#define raw_local_irq_disable()		arch_local_irq_disable()
#define raw_local_irq_enable()		arch_local_irq_enable()
#define raw_local_irq_save(flags)			\
	do {						\
		typecheck(unsigned long, flags);	\
		flags = arch_local_irq_save();		\
	} while (0)
#define raw_local_irq_restore(flags)			\
	do {						\
		typecheck(unsigned long, flags);	\
		arch_local_irq_restore(flags);		\
	} while (0)
#define raw_local_save_flags(flags)			\
	do {						\
		typecheck(unsigned long, flags);	\
		flags = arch_local_save_flags();	\
	} while (0)
#define raw_irqs_disabled_flags(flags)			\
	({						\
		typecheck(unsigned long, flags);	\
		arch_irqs_disabled_flags(flags);	\
	})
#define raw_irqs_disabled()		(arch_irqs_disabled())
#define raw_safe_halt()			arch_safe_halt()

/*
 * The local_irq_*() APIs are equal to the raw_local_irq*()
 * if !TRACE_IRQFLAGS.
 */
#ifdef CONFIG_TRACE_IRQFLAGS
#define local_irq_enable() \
	do { trace_hardirqs_on(); raw_local_irq_enable(); } while (0)
#define local_irq_disable() \
	do { raw_local_irq_disable(); trace_hardirqs_off(); } while (0)
#define local_irq_save(flags)				\
	do {						\
		raw_local_irq_save(flags);		\
		trace_hardirqs_off();			\
	} while (0)


#define local_irq_restore(flags)			\
	do {						\
		if (raw_irqs_disabled_flags(flags)) {	\
			raw_local_irq_restore(flags);	\
			trace_hardirqs_off();		\
		} else {				\
			trace_hardirqs_on();		\
			raw_local_irq_restore(flags);	\
		}					\
	} while (0)

#define safe_halt()				\
	do {					\
		trace_hardirqs_on();		\
		raw_safe_halt();		\
	} while (0)


#else /* !CONFIG_TRACE_IRQFLAGS */

#define local_irq_enable()	do { raw_local_irq_enable(); } while (0)
#define local_irq_disable()	do { raw_local_irq_disable(); } while (0)
#define local_irq_save(flags)					\
	do {							\
		raw_local_irq_save(flags);			\
	} while (0)
#define local_irq_restore(flags) do { raw_local_irq_restore(flags); } while (0)
#define safe_halt()		do { raw_safe_halt(); } while (0)

#endif /* CONFIG_TRACE_IRQFLAGS */

#define local_save_flags(flags)	raw_local_save_flags(flags)

/*
 * Some architectures don't define arch_irqs_disabled(), so even if either
 * definition would be fine we need to use different ones for the time being
 * to avoid build issues.
 */
#ifdef CONFIG_TRACE_IRQFLAGS_SUPPORT
#define irqs_disabled()					\
	({						\
		unsigned long _flags;			\
		raw_local_save_flags(_flags);		\
		raw_irqs_disabled_flags(_flags);	\
	})
#else /* !CONFIG_TRACE_IRQFLAGS_SUPPORT */
#define irqs_disabled()	raw_irqs_disabled()
#endif /* CONFIG_TRACE_IRQFLAGS_SUPPORT */

#define irqs_disabled_flags(flags) raw_irqs_disabled_flags(flags)

#endif
