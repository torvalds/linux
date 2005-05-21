#ifndef __UM_ARCHPARAM_PPC_H
#define __UM_ARCHPARAM_PPC_H

/********* Bits for asm-um/hw_irq.h **********/

struct hw_interrupt_type;

/********* Bits for asm-um/hardirq.h **********/

#define irq_enter(cpu, irq) hardirq_enter(cpu)
#define irq_exit(cpu, irq) hardirq_exit(cpu)

/********* Bits for asm-um/string.h **********/

#define __HAVE_ARCH_STRRCHR

#endif
