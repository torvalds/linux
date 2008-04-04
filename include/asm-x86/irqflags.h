#ifndef _X86_IRQFLAGS_H_
#define _X86_IRQFLAGS_H_

#include <asm/processor-flags.h>

#ifndef __ASSEMBLY__
/*
 * Interrupt control:
 */

static inline unsigned long native_save_fl(void)
{
	unsigned long flags;

	__asm__ __volatile__(
		"# __raw_save_flags\n\t"
		"pushf ; pop %0"
		: "=g" (flags)
		: /* no input */
		: "memory"
	);

	return flags;
}

static inline void native_restore_fl(unsigned long flags)
{
	__asm__ __volatile__(
		"push %0 ; popf"
		: /* no output */
		:"g" (flags)
		:"memory", "cc"
	);
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

#endif

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

#else

static inline void raw_local_irq_disable(void)
{
	native_irq_disable();
}

static inline void raw_local_irq_enable(void)
{
	native_irq_enable();
}

#endif

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

#define ENABLE_INTERRUPTS(x)	sti
#define DISABLE_INTERRUPTS(x)	cli

#ifdef CONFIG_X86_64
#define INTERRUPT_RETURN	iretq
#define ENABLE_INTERRUPTS_SYSCALL_RET			\
			movq	%gs:pda_oldrsp, %rsp;	\
			swapgs;				\
			sysretq;
#else
#define INTERRUPT_RETURN		iret
#define ENABLE_INTERRUPTS_SYSCALL_RET	sti; sysexit
#define GET_CR0_INTO_EAX		movl %cr0, %eax
#endif


#endif /* __ASSEMBLY__ */
#endif /* CONFIG_PARAVIRT */

#ifndef __ASSEMBLY__
#define raw_local_save_flags(flags) \
		do { (flags) = __raw_local_save_flags(); } while (0)

#define raw_local_irq_save(flags) \
		do { (flags) = __raw_local_irq_save(); } while (0)

#ifdef CONFIG_X86_VSMP
static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & X86_EFLAGS_IF) || (flags & X86_EFLAGS_AC);
}
#else
static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & X86_EFLAGS_IF);
}
#endif

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

#else

#ifdef CONFIG_X86_64
/*
 * Currently paravirt can't handle swapgs nicely when we
 * don't have a stack we can rely on (such as a user space
 * stack).  So we either find a way around these or just fault
 * and emulate if a guest tries to call swapgs directly.
 *
 * Either way, this is a good way to document that we don't
 * have a reliable stack. x86_64 only.
 */
#define SWAPGS_UNSAFE_STACK	swapgs
#define ARCH_TRACE_IRQS_ON		call trace_hardirqs_on_thunk
#define ARCH_TRACE_IRQS_OFF		call trace_hardirqs_off_thunk
#define ARCH_LOCKDEP_SYS_EXIT		call lockdep_sys_exit_thunk
#define ARCH_LOCKDEP_SYS_EXIT_IRQ	\
	TRACE_IRQS_ON; \
	sti; \
	SAVE_REST; \
	LOCKDEP_SYS_EXIT; \
	RESTORE_REST; \
	cli; \
	TRACE_IRQS_OFF;

#else
#define ARCH_TRACE_IRQS_ON			\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call trace_hardirqs_on;			\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

#define ARCH_TRACE_IRQS_OFF			\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call trace_hardirqs_off;		\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

#define ARCH_LOCKDEP_SYS_EXIT			\
	pushl %eax;				\
	pushl %ecx;				\
	pushl %edx;				\
	call lockdep_sys_exit;			\
	popl %edx;				\
	popl %ecx;				\
	popl %eax;

#define ARCH_LOCKDEP_SYS_EXIT_IRQ
#endif

#ifdef CONFIG_TRACE_IRQFLAGS
#  define TRACE_IRQS_ON		ARCH_TRACE_IRQS_ON
#  define TRACE_IRQS_OFF	ARCH_TRACE_IRQS_OFF
#else
#  define TRACE_IRQS_ON
#  define TRACE_IRQS_OFF
#endif
#ifdef CONFIG_DEBUG_LOCK_ALLOC
#  define LOCKDEP_SYS_EXIT	ARCH_LOCKDEP_SYS_EXIT
#  define LOCKDEP_SYS_EXIT_IRQ	ARCH_LOCKDEP_SYS_EXIT_IRQ
# else
#  define LOCKDEP_SYS_EXIT
#  define LOCKDEP_SYS_EXIT_IRQ
# endif

#endif /* __ASSEMBLY__ */
#endif
