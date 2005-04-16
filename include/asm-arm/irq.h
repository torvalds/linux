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

extern void disable_irq_nosync(unsigned int);
extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#define __IRQT_FALEDGE	(1 << 0)
#define __IRQT_RISEDGE	(1 << 1)
#define __IRQT_LOWLVL	(1 << 2)
#define __IRQT_HIGHLVL	(1 << 3)

#define IRQT_NOEDGE	(0)
#define IRQT_RISING	(__IRQT_RISEDGE)
#define IRQT_FALLING	(__IRQT_FALEDGE)
#define IRQT_BOTHEDGE	(__IRQT_RISEDGE|__IRQT_FALEDGE)
#define IRQT_LOW	(__IRQT_LOWLVL)
#define IRQT_HIGH	(__IRQT_HIGHLVL)
#define IRQT_PROBE	(1 << 4)

int set_irq_type(unsigned int irq, unsigned int type);
void disable_irq_wake(unsigned int irq);
void enable_irq_wake(unsigned int irq);
int setup_irq(unsigned int, struct irqaction *);

struct irqaction;
struct pt_regs;
int handle_IRQ_event(unsigned int, struct pt_regs *, struct irqaction *);

#endif

