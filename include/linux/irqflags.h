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
#include <asm/percpu.h>

/* Currently lockdep_softirqs_on/off is used only by lockdep */
#ifdef CONFIG_PROVE_LOCKING
  extern void lockdep_softirqs_on(unsigned long ip);
  extern void lockdep_softirqs_off(unsigned long ip);
  extern void lockdep_hardirqs_on_prepare(void);
  extern void lockdep_hardirqs_on(unsigned long ip);
  extern void lockdep_hardirqs_off(unsigned long ip);
#else
  static inline void lockdep_softirqs_on(unsigned long ip) { }
  static inline void lockdep_softirqs_off(unsigned long ip) { }
  static inline void lockdep_hardirqs_on_prepare(void) { }
  static inline void lockdep_hardirqs_on(unsigned long ip) { }
  static inline void lockdep_hardirqs_off(unsigned long ip) { }
#endif

#ifdef CONFIG_TRACE_IRQFLAGS

/* Per-task IRQ trace events information. */
struct irqtrace_events {
	unsigned int	irq_events;
	unsigned long	hardirq_enable_ip;
	unsigned long	hardirq_disable_ip;
	unsigned int	hardirq_enable_event;
	unsigned int	hardirq_disable_event;
	unsigned long	softirq_disable_ip;
	unsigned long	softirq_enable_ip;
	unsigned int	softirq_disable_event;
	unsigned int	softirq_enable_event;
};

DECLARE_PER_CPU(int, hardirqs_enabled);
DECLARE_PER_CPU(int, hardirq_context);

extern void trace_hardirqs_on_prepare(void);
extern void trace_hardirqs_off_finish(void);
extern void trace_hardirqs_on(void);
extern void trace_hardirqs_off(void);

# define lockdep_hardirq_context()	(raw_cpu_read(hardirq_context))
# define lockdep_softirq_context(p)	((p)->softirq_context)
# define lockdep_hardirqs_enabled()	(this_cpu_read(hardirqs_enabled))
# define lockdep_softirqs_enabled(p)	((p)->softirqs_enabled)
# define lockdep_hardirq_enter()			\
do {							\
	if (__this_cpu_inc_return(hardirq_context) == 1)\
		current->hardirq_threaded = 0;		\
} while (0)
# define lockdep_hardirq_threaded()		\
do {						\
	current->hardirq_threaded = 1;		\
} while (0)
# define lockdep_hardirq_exit()			\
do {						\
	__this_cpu_dec(hardirq_context);	\
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

# define lockdep_irq_work_enter(_flags)					\
	  do {								\
		  if (!((_flags) & IRQ_WORK_HARD_IRQ))			\
			current->irq_config = 1;			\
	  } while (0)
# define lockdep_irq_work_exit(_flags)					\
	  do {								\
		  if (!((_flags) & IRQ_WORK_HARD_IRQ))			\
			current->irq_config = 0;			\
	  } while (0)

#else
# define trace_hardirqs_on_prepare()		do { } while (0)
# define trace_hardirqs_off_finish()		do { } while (0)
# define trace_hardirqs_on()			do { } while (0)
# define trace_hardirqs_off()			do { } while (0)
# define lockdep_hardirq_context()		0
# define lockdep_softirq_context(p)		0
# define lockdep_hardirqs_enabled()		0
# define lockdep_softirqs_enabled(p)		0
# define lockdep_hardirq_enter()		do { } while (0)
# define lockdep_hardirq_threaded()		do { } while (0)
# define lockdep_hardirq_exit()			do { } while (0)
# define lockdep_softirq_enter()		do { } while (0)
# define lockdep_softirq_exit()			do { } while (0)
# define lockdep_hrtimer_enter(__hrtimer)	false
# define lockdep_hrtimer_exit(__context)	do { } while (0)
# define lockdep_posixtimer_enter()		do { } while (0)
# define lockdep_posixtimer_exit()		do { } while (0)
# define lockdep_irq_work_enter(__work)		do { } while (0)
# define lockdep_irq_work_exit(__work)		do { } while (0)
#endif

#if defined(CONFIG_TRACE_IRQFLAGS) && !defined(CONFIG_PREEMPT_RT)
# define lockdep_softirq_enter()		\
do {						\
	current->softirq_context++;		\
} while (0)
# define lockdep_softirq_exit()			\
do {						\
	current->softirq_context--;		\
} while (0)

#else
# define lockdep_softirq_enter()		do { } while (0)
# define lockdep_softirq_exit()			do { } while (0)
#endif

#if defined(CONFIG_IRQSOFF_TRACER) || \
	defined(CONFIG_PREEMPT_TRACER)
 extern void stop_critical_timings(void);
 extern void start_critical_timings(void);
#else
# define stop_critical_timings() do { } while (0)
# define start_critical_timings() do { } while (0)
#endif

#ifdef CONFIG_DEBUG_IRQFLAGS
extern void warn_bogus_irq_restore(void);
#define raw_check_bogus_irq_restore()			\
	do {						\
		if (unlikely(!arch_irqs_disabled()))	\
			warn_bogus_irq_restore();	\
	} while (0)
#else
#define raw_check_bogus_irq_restore() do { } while (0)
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
		raw_check_bogus_irq_restore();		\
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

#define local_irq_enable()				\
	do {						\
		trace_hardirqs_on();			\
		raw_local_irq_enable();			\
	} while (0)

#define local_irq_disable()				\
	do {						\
		bool was_disabled = raw_irqs_disabled();\
		raw_local_irq_disable();		\
		if (!was_disabled)			\
			trace_hardirqs_off();		\
	} while (0)

#define local_irq_save(flags)				\
	do {						\
		raw_local_irq_save(flags);		\
		if (!raw_irqs_disabled_flags(flags))	\
			trace_hardirqs_off();		\
	} while (0)

#define local_irq_restore(flags)			\
	do {						\
		if (!raw_irqs_disabled_flags(flags))	\
			trace_hardirqs_on();		\
		raw_local_irq_restore(flags);		\
	} while (0)

#define safe_halt()				\
	do {					\
		trace_hardirqs_on();		\
		raw_safe_halt();		\
	} while (0)


#else /* !CONFIG_TRACE_IRQFLAGS */

#define local_irq_enable()	do { raw_local_irq_enable(); } while (0)
#define local_irq_disable()	do { raw_local_irq_disable(); } while (0)
#define local_irq_save(flags)	do { raw_local_irq_save(flags); } while (0)
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
