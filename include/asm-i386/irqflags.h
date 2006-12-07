/*
 * include/asm-i386/irqflags.h
 *
 * IRQ flags handling
 *
 * This file gets included from lowlevel asm headers too, to provide
 * wrapped versions of the local_irq_*() APIs, based on the
 * raw_local_irq_*() functions from the lowlevel headers.
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#ifndef __ASSEMBLY__

static inline unsigned long __raw_local_save_flags(void)
{
	unsigned long flags;

	__asm__ __volatile__(
		"pushfl ; popl %0"
		: "=g" (flags)
		: /* no input */
	);

	return flags;
}

static inline void raw_local_irq_restore(unsigned long flags)
{
	__asm__ __volatile__(
		"pushl %0 ; popfl"
		: /* no output */
		:"g" (flags)
		:"memory", "cc"
	);
}

static inline void raw_local_irq_disable(void)
{
	__asm__ __volatile__("cli" : : : "memory");
}

static inline void raw_local_irq_enable(void)
{
	__asm__ __volatile__("sti" : : : "memory");
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

/*
 * For spinlocks, etc:
 */
static inline unsigned long __raw_local_irq_save(void)
{
	unsigned long flags = __raw_local_save_flags();

	raw_local_irq_disable();

	return flags;
}

#else
#define DISABLE_INTERRUPTS(clobbers)	cli
#define ENABLE_INTERRUPTS(clobbers)	sti
#define ENABLE_INTERRUPTS_SYSEXIT	sti; sysexit
#define INTERRUPT_RETURN		iret
#define GET_CR0_INTO_EAX		movl %cr0, %eax
#endif /* __ASSEMBLY__ */
#endif /* CONFIG_PARAVIRT */

#ifndef __ASSEMBLY__
#define raw_local_save_flags(flags) \
		do { (flags) = __raw_local_save_flags(); } while (0)

#define raw_local_irq_save(flags) \
		do { (flags) = __raw_local_irq_save(); } while (0)

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & (1 << 9));
}

static inline int raw_irqs_disabled(void)
{
	unsigned long flags = __raw_local_save_flags();

	return raw_irqs_disabled_flags(flags);
}
#endif /* __ASSEMBLY__ */

/*
 * Do the CPU's IRQ-state tracing from assembly code. We call a
 * C function, so save all the C-clobbered registers:
 */
#ifdef CONFIG_TRACE_IRQFLAGS

# define TRACE_IRQS_ON				\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call trace_hardirqs_on;			\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

# define TRACE_IRQS_OFF				\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call trace_hardirqs_off;		\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

#else
# define TRACE_IRQS_ON
# define TRACE_IRQS_OFF
#endif

#endif
