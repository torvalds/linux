/*
 * include/asm-i386/irqflags.h
 *
 * IRQ flags handling
 *
 * This file gets included from lowlevel asm headers too, to provide
 * wrapped versions of the local_irq_*() APIs, based on the
 * raw_local_irq_*() macros from the lowlevel headers.
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#define raw_local_save_flags(x)	do { typecheck(unsigned long,x); __asm__ __volatile__("pushfl ; popl %0":"=g" (x): /* no input */); } while (0)
#define raw_local_irq_restore(x) do { typecheck(unsigned long,x); __asm__ __volatile__("pushl %0 ; popfl": /* no output */ :"g" (x):"memory", "cc"); } while (0)
#define raw_local_irq_disable()	__asm__ __volatile__("cli": : :"memory")
#define raw_local_irq_enable()	__asm__ __volatile__("sti": : :"memory")
/* used in the idle loop; sti takes one instruction cycle to complete */
#define raw_safe_halt()		__asm__ __volatile__("sti; hlt": : :"memory")
/* used when interrupts are already enabled or to shutdown the processor */
#define halt()			__asm__ __volatile__("hlt": : :"memory")

#define raw_irqs_disabled_flags(flags)	(!((flags) & (1<<9)))

/* For spinlocks etc */
#define raw_local_irq_save(x)	__asm__ __volatile__("pushfl ; popl %0 ; cli":"=g" (x): /* no input */ :"memory")

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
