/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_GENERIC_IRQFLAGS_H
#define __ASM_GENERIC_IRQFLAGS_H

/*
 * All architectures should implement at least the first two functions,
 * usually inline assembly will be the best way.
 */
#ifndef ARCH_IRQ_DISABLED
#define ARCH_IRQ_DISABLED 0
#define ARCH_IRQ_ENABLED 1
#endif

/* read interrupt enabled status */
#ifndef arch_local_save_flags
unsigned long arch_local_save_flags(void);
#endif

/* set interrupt enabled status */
#ifndef arch_local_irq_restore
void arch_local_irq_restore(unsigned long flags);
#endif

/* get status and disable interrupts */
#ifndef arch_local_irq_save
static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags;
	flags = arch_local_save_flags();
	arch_local_irq_restore(ARCH_IRQ_DISABLED);
	return flags;
}
#endif

/* test flags */
#ifndef arch_irqs_disabled_flags
static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return flags == ARCH_IRQ_DISABLED;
}
#endif

/* unconditionally enable interrupts */
#ifndef arch_local_irq_enable
static inline void arch_local_irq_enable(void)
{
	arch_local_irq_restore(ARCH_IRQ_ENABLED);
}
#endif

/* unconditionally disable interrupts */
#ifndef arch_local_irq_disable
static inline void arch_local_irq_disable(void)
{
	arch_local_irq_restore(ARCH_IRQ_DISABLED);
}
#endif

/* test hardware interrupt enable bit */
#ifndef arch_irqs_disabled
static inline int arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}
#endif

#endif /* __ASM_GENERIC_IRQFLAGS_H */
