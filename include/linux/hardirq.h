/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_HARDIRQ_H
#define LINUX_HARDIRQ_H

#include <linux/context_tracking_state.h>
#include <linux/preempt.h>
#include <linux/lockdep.h>
#include <linux/ftrace_irq.h>
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
		account_irq_enter_time(current);	\
		preempt_count_add(HARDIRQ_OFFSET);	\
		lockdep_hardirq_enter();		\
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
		lockdep_hardirq_exit();			\
		account_irq_exit_time(current);		\
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

#ifdef CONFIG_TINY_RCU
static inline void rcu_nmi_enter(void) { }
static inline void rcu_nmi_exit(void) { }
#else
extern void rcu_nmi_enter(void);
extern void rcu_nmi_exit(void);
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
 * nmi_enter() can nest up to 15 times; see NMI_BITS.
 */
#define nmi_enter()						\
	do {							\
		arch_nmi_enter();				\
		printk_nmi_enter();				\
		lockdep_off();					\
		BUG_ON(in_nmi() == NMI_MASK);			\
		__preempt_count_add(NMI_OFFSET + HARDIRQ_OFFSET);	\
		rcu_nmi_enter();				\
		lockdep_hardirq_enter();			\
		instrumentation_begin();			\
		ftrace_nmi_enter();				\
		instrumentation_end();				\
	} while (0)

#define nmi_exit()						\
	do {							\
		instrumentation_begin();			\
		ftrace_nmi_exit();				\
		instrumentation_end();				\
		lockdep_hardirq_exit();				\
		rcu_nmi_exit();					\
		BUG_ON(!in_nmi());				\
		__preempt_count_sub(NMI_OFFSET + HARDIRQ_OFFSET);	\
		lockdep_on();					\
		printk_nmi_exit();				\
		arch_nmi_exit();				\
	} while (0)

#endif /* LINUX_HARDIRQ_H */
