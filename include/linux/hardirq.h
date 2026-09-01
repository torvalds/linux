/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_HARDIRQ_H
#define LINUX_HARDIRQ_H

#include <linux/context_tracking_state.h>
#include <linux/preempt.h>
#include <linux/lockdep.h>
#include <linux/ftrace_irq.h>
#include <linux/sched.h>
#include <linux/vtime.h>
#include <asm/hardirq.h>

extern void synchronize_irq(unsigned int irq);
extern bool synchronize_hardirq(unsigned int irq);

#ifdef CONFIG_NO_HZ_FULL
void __rcu_irq_enter_check_tick(void);
#else
static inline void __rcu_irq_enter_check_tick(void) { }
#endif

static __always_inline void rcu_irq_enter_check_tick(void)
{
	if (context_tracking_enabled())
		__rcu_irq_enter_check_tick();
}

/*
 * It is safe to do non-atomic ops on ->hardirq_context,
 * because NMI handlers may not preempt and the ops are
 * always balanced, so the interrupted value of ->hardirq_context
 * will always be restored.
 */
#define __irq_enter()					\
	do {						\
		preempt_count_add(HARDIRQ_OFFSET);	\
		lockdep_hardirq_enter();		\
		account_hardirq_enter(current);		\
	} while (0)

/*
 * Like __irq_enter() without time accounting for fast
 * interrupts, e.g. reschedule IPI where time accounting
 * is more expensive than the actual interrupt.
 */
#define __irq_enter_raw()				\
	do {						\
		preempt_count_add(HARDIRQ_OFFSET);	\
		lockdep_hardirq_enter();		\
	} while (0)

/*
 * Enter irq context (on NO_HZ, update jiffies):
 */
void irq_enter(void);
/*
 * Like irq_enter(), but RCU is already watching.
 */
void irq_enter_rcu(void);

/*
 * Exit irq context without processing softirqs:
 */
#define __irq_exit()					\
	do {						\
		account_hardirq_exit(current);		\
		lockdep_hardirq_exit();			\
		preempt_count_sub(HARDIRQ_OFFSET);	\
	} while (0)

/*
 * Like __irq_exit() without time accounting
 */
#define __irq_exit_raw()				\
	do {						\
		lockdep_hardirq_exit();			\
		preempt_count_sub(HARDIRQ_OFFSET);	\
	} while (0)

/*
 * Exit irq context and process softirqs if needed:
 */
void irq_exit(void);

/*
 * Like irq_exit(), but return with RCU watching.
 */
void irq_exit_rcu(void);

#ifndef arch_nmi_enter
#define arch_nmi_enter()	do { } while (0)
#define arch_nmi_exit()		do { } while (0)
#endif

#ifdef CONFIG_HAS_SEPARATE_PREEMPT_RESCHED_BITS
static __always_inline void __preempt_count_nmi_enter(void)
{
	__preempt_count_add(NMI_OFFSET + HARDIRQ_OFFSET);
}

static __always_inline void __preempt_count_nmi_exit(void)
{
	__preempt_count_sub(NMI_OFFSET + HARDIRQ_OFFSET);
}
#else
DECLARE_PER_CPU(unsigned int, nmi_nesting);

#define __preempt_count_nmi_enter()				\
	do {							\
		__preempt_count_add(HARDIRQ_OFFSET);		\
		/* Maximum NMI nesting is 15. */		\
		BUG_ON(__this_cpu_read(nmi_nesting) >= 15);	\
		__this_cpu_inc(nmi_nesting);			\
		preempt_count_set(preempt_count() | NMI_MASK);  \
	} while (0)

#define __preempt_count_nmi_exit()				\
	do {							\
		__preempt_count_sub(HARDIRQ_OFFSET);		\
		if (!__this_cpu_dec_return(nmi_nesting))	\
			preempt_count_set(preempt_count() & ~NMI_MASK); \
	} while (0)

#endif

/*
 * NMI vs Tracing
 * --------------
 *
 * We must not land in a tracer until (or after) we've changed preempt_count
 * such that in_nmi() becomes true. To that effect all NMI C entry points must
 * be marked 'notrace' and call nmi_enter() as soon as possible.
 */

/*
 * nmi_enter() can nest - nesting is tracked in a per-CPU counter.
 */
#define __nmi_enter()						\
	do {							\
		lockdep_off();					\
		arch_nmi_enter();				\
		__preempt_count_nmi_enter();			\
	} while (0)

#define nmi_enter()						\
	do {							\
		__nmi_enter();					\
		lockdep_hardirq_enter();			\
		ct_nmi_enter();					\
		instrumentation_begin();			\
		ftrace_nmi_enter();				\
		instrumentation_end();				\
	} while (0)

#define __nmi_exit()						\
	do {							\
		BUG_ON(!in_nmi());				\
		__preempt_count_nmi_exit();			\
		arch_nmi_exit();				\
		lockdep_on();					\
	} while (0)

#define nmi_exit()						\
	do {							\
		instrumentation_begin();			\
		ftrace_nmi_exit();				\
		instrumentation_end();				\
		ct_nmi_exit();					\
		lockdep_hardirq_exit();				\
		__nmi_exit();					\
	} while (0)

#endif /* LINUX_HARDIRQ_H */
