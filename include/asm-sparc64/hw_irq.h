#ifndef __ASM_SPARC64_HW_IRQ_H
#define __ASM_SPARC64_HW_IRQ_H

extern void hw_resend_irq(struct hw_interrupt_type *handler, unsigned int virt_irq);

#endif
