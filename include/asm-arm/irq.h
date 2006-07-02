#ifndef __ASM_ARM_IRQ_H
#define __ASM_ARM_IRQ_H

#include <asm/arch/irqs.h>

#ifndef irq_canonicalize
#define irq_canonicalize(i)	(i)
#endif

#ifndef NR_IRQS
#define NR_IRQS	128
#endif

/*
 * Use this value to indicate lack of interrupt
 * capability
 */
#ifndef NO_IRQ
#define NO_IRQ	((unsigned int)(-1))
#endif

struct irqaction;

/*
 * Migration helpers
 */
#define __IRQT_FALEDGE	IRQ_TYPE_EDGE_FALLING
#define __IRQT_RISEDGE	IRQ_TYPE_EDGE_RISING
#define __IRQT_LOWLVL	IRQ_TYPE_LEVEL_LOW
#define __IRQT_HIGHLVL	IRQ_TYPE_LEVEL_HIGH

#define IRQT_NOEDGE	(0)
#define IRQT_RISING	(__IRQT_RISEDGE)
#define IRQT_FALLING	(__IRQT_FALEDGE)
#define IRQT_BOTHEDGE	(__IRQT_RISEDGE|__IRQT_FALEDGE)
#define IRQT_LOW	(__IRQT_LOWLVL)
#define IRQT_HIGH	(__IRQT_HIGHLVL)
#define IRQT_PROBE	IRQ_TYPE_PROBE

extern void migrate_irqs(void);
#endif

