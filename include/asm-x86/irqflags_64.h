/*
 * include/asm-x86_64/irqflags.h
 *
 * IRQ flags handling
 *
 * This file gets included from lowlevel asm headers too, to provide
 * wrapped versions of the local_irq_*() APIs, based on the
 * raw_local_irq_*() functions from the lowlevel headers.
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H
#include <asm/processor-flags.h>

#ifndef __ASSEMBLY__
/*
 * Interrupt control:
 */

static inline unsigned long __raw_local_save_flags(void)
{
	unsigned long flags;

	__asm__ __volatile__(
		"# __raw_save_flags\n\t"
		"pushfq ; popq %q0"
		: "=g" (flags)
		: /* no input */
		: "memory"
	);

	return flags;
}

#define raw_local_save_flags(flags) \
		do { (flags) = __raw_local_save_flags(); } while (0)

static inline void raw_local_irq_restore(unsigned long flags)
{
	__asm__ __volatile__(
		"pushq %0 ; popfq"
		: /* no output */
		:"g" (flags)
		:"memory", "cc"
	);
}

#ifdef CONFIG_X86_VSMP

/*
 * Interrupt control for the VSMP architecture:
 */

static inline void raw_local_irq_disable(void)
{
	unsigned long flags = __raw_local_save_flags();

	raw_local_irq_restore((flags & ~X86_EFLAGS_IF) | X86_EFLAGS_AC);
}

static inline void raw_local_irq_enable(void)
{
	unsigned long flags = __raw_local_save_flags();

	raw_local_irq_restore((flags | X86_EFLAGS_IF) & (~X86_EFLAGS_AC));
}

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & X86_EFLAGS_IF) || (flags & X86_EFLAGS_AC);
}

#else /* CONFIG_X86_VSMP */

static inline void raw_local_irq_disable(void)
{
	__asm__ __volatile__("cli" : : : "memory");
}

static inline void raw_local_irq_enable(void)
{
	__asm__ __volatile__("sti" : : : "memory");
}

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & X86_EFLAGS_IF);
}

#endif

/*
 * For spinlocks, etc.:
 */

static inline unsigned long __raw_local_irq_save(void)
{
	unsigned long flags = __raw_local_save_flags();

	raw_local_irq_disable();

	return flags;
}

#define raw_local_irq_save(flags) \
		do { (flags) = __raw_local_irq_save(); } while (0)

static inline int raw_irqs_disabled(void)
{
	unsigned long flags = __raw_local_save_flags();

	return raw_irqs_disabled_flags(flags);
}

/*
 * makes the traced hardirq state match with the machine state
 *
 * should be a rarely used function, only in places where its
 * otherwise impossible to know the irq state, like in traps.
 */
static inline void trace_hardirqs_fixup_flags(unsigned long flags)
{
	if (raw_irqs_disabled_flags(flags))
		trace_hardirqs_off();
	else
		trace_hardirqs_on();
}

static inline void trace_hardirqs_fixup(void)
{
	unsigned long flags = __raw_local_save_flags();

	trace_hardirqs_fixup_flags(flags);
}
/*
 * Used in the idle loop; sti takes one instruction cycle
 * to complete:
 */
static inline void raw_safe_halt(void)
{
	__asm__ __volatile__("sti; hlt" : : : "memory");
}

/*
 * Used when interrupts are already enabled or to
 * shutdown the processor:
 */
static inline void halt(void)
{
	__asm__ __volatile__("hlt": : :"memory");
}

#else /* __ASSEMBLY__: */
# ifdef CONFIG_TRACE_IRQFLAGS
#  define TRACE_IRQS_ON		call trace_hardirqs_on_thunk
#  define TRACE_IRQS_OFF	call trace_hardirqs_off_thunk
# else
#  define TRACE_IRQS_ON
#  define TRACE_IRQS_OFF
# endif
# ifdef CONFIG_DEBUG_LOCK_ALLOC
#  define LOCKDEP_SYS_EXIT	call lockdep_sys_exit_thunk
#  define LOCKDEP_SYS_EXIT_IRQ	\
	TRACE_IRQS_ON; \
	sti; \
	SAVE_REST; \
	LOCKDEP_SYS_EXIT; \
	RESTORE_REST; \
	cli; \
	TRACE_IRQS_OFF;
# else
#  define LOCKDEP_SYS_EXIT
#  define LOCKDEP_SYS_EXIT_IRQ
# endif
#endif

#endif
