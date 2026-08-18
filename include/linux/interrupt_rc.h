/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __LINUX_INTERRUPT_RC_H
#define __LINUX_INTERRUPT_RC_H

/*
 * include/linux/interrupt_rc.h - refcounted local processor interrupt
 * management.
 *
 * Since the implementation of this API currently depends on
 * local_irq_save()/local_irq_restore(), we split this into its own header to
 * make it easier to include without hitting circular header dependencies.
 */

#include <linux/irqflags.h>
#include <linux/preempt.h>
#include <linux/processor.h>
#include <linux/smp.h>

#ifndef MODULE
/* Per-CPU interrupt disabling state for local_interrupt_{disable,enable}(). */
DECLARE_PER_CPU(unsigned long, local_interrupt_disable_state);

static __always_inline void __local_interrupt_disable(void)
{
	unsigned long flags;

	local_irq_save(flags);
	raw_cpu_write(local_interrupt_disable_state, flags);
}

static __always_inline void __local_interrupt_enable(void)
{
	unsigned long flags = raw_cpu_read(local_interrupt_disable_state);

	local_irq_restore(flags);
}

#ifndef INSTANTIATE_EXPORTED_INTERRUPT_DISABLE
static __always_inline void _local_interrupt_disable(void)
{
	__local_interrupt_disable();
}

static __always_inline void _local_interrupt_enable(void)
{
	__local_interrupt_enable();
}
#else
extern void _local_interrupt_disable(void);
extern void _local_interrupt_enable(void);
#endif

#else /* !MODULE */
extern void _local_interrupt_disable(void);
extern void _local_interrupt_enable(void);
#endif /* !MODULE */

static inline void local_interrupt_disable(void)
{
	int new_count;

	WARN_ON_ONCE(in_nmi());

	new_count = hardirq_disable_enter();

	/* Interrupts can happen here, but it's OK, see __irq_exit_rcu(). */

	if ((new_count & HARDIRQ_DISABLE_MASK) == HARDIRQ_DISABLE_OFFSET)
		_local_interrupt_disable();
}

static inline void local_interrupt_enable(void)
{
	int new_count;

	new_count = hardirq_disable_exit();

	if ((new_count & HARDIRQ_DISABLE_MASK) == 0)
		_local_interrupt_enable();
}

#endif /* !__LINUX_INTERRUPT_RC_H */
