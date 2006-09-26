#ifndef __ASM_AVR32_IRQ_H
#define __ASM_AVR32_IRQ_H

#define NR_INTERNAL_IRQS	64
#define NR_EXTERNAL_IRQS	64
#define NR_IRQS			(NR_INTERNAL_IRQS + NR_EXTERNAL_IRQS)

#define irq_canonicalize(i)	(i)

#endif /* __ASM_AVR32_IOCTLS_H */
