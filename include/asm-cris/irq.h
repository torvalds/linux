#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

#include <asm/arch/irq.h>

extern __inline__ int irq_canonicalize(int irq)
{  
  return irq; 
}

extern void disable_irq(unsigned int);
extern void enable_irq(unsigned int);

#define disable_irq_nosync      disable_irq
#define enable_irq_nosync       enable_irq

struct irqaction;
struct pt_regs;
int handle_IRQ_event(unsigned int, struct pt_regs *, struct irqaction *);

#endif  /* _ASM_IRQ_H */


