/*
 * include/asm-x86_64/irqflags.h
 *
 * IRQ flags handling
 *
 * This file gets included from lowlevel asm headers too, to provide
 * wrapped versions of the local_irq_*() APIs, based on the
 * raw_local_irq_*() macros from the lowlevel headers.
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#ifndef __ASSEMBLY__

/* interrupt control.. */
#define raw_local_save_flags(x)	do { warn_if_not_ulong(x); __asm__ __volatile__("# save_flags \n\t pushfq ; popq %q0":"=g" (x): /* no input */ :"memory"); } while (0)
#define raw_local_irq_restore(x) 	__asm__ __volatile__("# restore_flags \n\t pushq %0 ; popfq": /* no output */ :"g" (x):"memory", "cc")

#ifdef CONFIG_X86_VSMP
/* Interrupt control for VSMP  architecture */
#define raw_local_irq_disable()	do { unsigned long flags; raw_local_save_flags(flags); raw_local_irq_restore((flags & ~(1 << 9)) | (1 << 18)); } while (0)
#define raw_local_irq_enable()	do { unsigned long flags; raw_local_save_flags(flags); raw_local_irq_restore((flags | (1 << 9)) & ~(1 << 18)); } while (0)

#define raw_irqs_disabled_flags(flags)	\
({						\
	(flags & (1<<18)) || !(flags & (1<<9));	\
})

/* For spinlocks etc */
#define raw_local_irq_save(x)	do { raw_local_save_flags(x); raw_local_irq_restore((x & ~(1 << 9)) | (1 << 18)); } while (0)
#else  /* CONFIG_X86_VSMP */
#define raw_local_irq_disable() 	__asm__ __volatile__("cli": : :"memory")
#define raw_local_irq_enable()	__asm__ __volatile__("sti": : :"memory")

#define raw_irqs_disabled_flags(flags)	\
({						\
	!(flags & (1<<9));			\
})

/* For spinlocks etc */
#define raw_local_irq_save(x) 	do { warn_if_not_ulong(x); __asm__ __volatile__("# raw_local_irq_save \n\t pushfq ; popq %0 ; cli":"=g" (x): /* no input */ :"memory"); } while (0)
#endif

#define raw_irqs_disabled()			\
({						\
	unsigned long flags;			\
	raw_local_save_flags(flags);		\
	raw_irqs_disabled_flags(flags);		\
})

/* used in the idle loop; sti takes one instruction cycle to complete */
#define raw_safe_halt()	__asm__ __volatile__("sti; hlt": : :"memory")
/* used when interrupts are already enabled or to shutdown the processor */
#define halt()			__asm__ __volatile__("hlt": : :"memory")

#else /* __ASSEMBLY__: */
# define TRACE_IRQS_ON
# define TRACE_IRQS_OFF
#endif

#endif
