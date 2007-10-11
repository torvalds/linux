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
#include <asm/processor-flags.h>

#ifndef __ASSEMBLY__
static inline unsigned long native_save_fl(void)
{
	unsigned long f;
	asm volatile("pushfl ; popl %0":"=g" (f): /* no input */);
	return f;
}

static inline void native_restore_fl(unsigned long f)
{
	asm volatile("pushl %0 ; popfl": /* no output */
			     :"g" (f)
			     :"memory", "cc");
}

static inline void native_irq_disable(void)
{
	asm volatile("cli": : :"memory");
}

static inline void native_irq_enable(void)
{
	asm volatile("sti": : :"memory");
}

static inline void native_safe_halt(void)
{
	asm volatile("sti; hlt": : :"memory");
}

static inline void native_halt(void)
{
	asm volatile("hlt": : :"memory");
}
#endif	/* __ASSEMBLY__ */

#ifdef CONFIG_PARAVIRT
#include <asm/paravirt.h>
#else
#ifndef __ASSEMBLY__

static inline unsigned long __raw_local_save_flags(void)
{
	return native_save_fl();
}

static inline void raw_local_irq_restore(unsigned long flags)
{
	native_restore_fl(flags);
}

static inline void raw_local_irq_disable(void)
{
	native_irq_disable();
}

static inline void raw_local_irq_enable(void)
{
	native_irq_enable();
}

/*
 * Used in the idle loop; sti takes one instruction cycle
 * to complete:
 */
static inline void raw_safe_halt(void)
{
	native_safe_halt();
}

/*
 * Used when interrupts are already enabled or to
 * shutdown the processor:
 */
static inline void halt(void)
{
	native_halt();
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
	return !(flags & X86_EFLAGS_IF);
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

#ifdef CONFIG_DEBUG_LOCK_ALLOC
# define LOCKDEP_SYS_EXIT			\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call lockdep_sys_exit;			\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;
#else
# define LOCKDEP_SYS_EXIT
#endif

#endif
