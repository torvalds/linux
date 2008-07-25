/*
 * include/asm-sparc/irqflags.h
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

extern void raw_local_irq_restore(unsigned long);
extern unsigned long __raw_local_irq_save(void);
extern void raw_local_irq_enable(void);

static inline unsigned long getipl(void)
{
        unsigned long retval;

        __asm__ __volatile__("rd        %%psr, %0" : "=r" (retval));
        return retval;
}

#define raw_local_save_flags(flags) ((flags) = getipl())
#define raw_local_irq_save(flags)   ((flags) = __raw_local_irq_save())
#define raw_local_irq_disable()     ((void) __raw_local_irq_save())
#define raw_irqs_disabled()         ((getipl() & PSR_PIL) != 0)

static inline int raw_irqs_disabled_flags(unsigned long flags)
{
        return ((flags & PSR_PIL) != 0);
}

#endif /* (__ASSEMBLY__) */

#endif /* !(_ASM_IRQFLAGS_H) */
