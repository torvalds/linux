#ifndef __ASM_AVR32_IRQ_H
#define __ASM_AVR32_IRQ_H

#define NR_INTERNAL_IRQS	64

#include <asm/arch/irq.h>

#ifndef NR_IRQS
#define NR_IRQS			(NR_INTERNAL_IRQS)
#endif

#define irq_canonicalize(i)	(i)

#endif /* __ASM_AVR32_IOCTLS_H */
