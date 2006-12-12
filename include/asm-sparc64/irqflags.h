/*
 * include/asm-sparc64/irqflags.h
 *
 * IRQ flags handling
 *
 * This file gets included from lowlevel asm headers too, to provide
 * wrapped versions of the local_irq_*() APIs, based on the
 * raw_local_irq_*() functions from the lowlevel headers.
 */
#ifndef _ASM_IRQFLAGS_H
#define _ASM_IRQFLAGS_H

#ifndef __ASSEMBLY__

static inline unsigned long __raw_local_save_flags(void)
{
	unsigned long flags;

	__asm__ __volatile__(
		"rdpr	%%pil, %0"
		: "=r" (flags)
	);

	return flags;
}

#define raw_local_save_flags(flags) \
		do { (flags) = __raw_local_save_flags(); } while (0)

static inline void raw_local_irq_restore(unsigned long flags)
{
	__asm__ __volatile__(
		"wrpr	%0, %%pil"
		: /* no output */
		: "r" (flags)
		: "memory"
	);
}

static inline void raw_local_irq_disable(void)
{
	__asm__ __volatile__(
		"wrpr	15, %%pil"
		: /* no outputs */
		: /* no inputs */
		: "memory"
	);
}

static inline void raw_local_irq_enable(void)
{
	__asm__ __volatile__(
		"wrpr	0, %%pil"
		: /* no outputs */
		: /* no inputs */
		: "memory"
	);
}

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
	return (flags > 0);
}

static inline int raw_irqs_disabled(void)
{
	unsigned long flags = __raw_local_save_flags();

	return raw_irqs_disabled_flags(flags);
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

#define raw_local_irq_save(flags) \
		do { (flags) = __raw_local_irq_save(); } while (0)

#endif /* (__ASSEMBLY__) */

#endif /* !(_ASM_IRQFLAGS_H) */
