#ifndef _ASM_IRQ_H
#define _ASM_IRQ_H

/*
 * IRQ line status macro IRQ_PER_CPU is used
 */
#define ARCH_HAS_IRQ_PER_CPU

#include <asm/arch/irq.h>

static inline int irq_canonicalize(int irq)
{  
  return irq; 
}

#endif  /* _ASM_IRQ_H */


