#ifndef __ASM_MACH_VR41XX_IRQ_H
#define __ASM_MACH_VR41XX_IRQ_H

#include <asm/vr41xx/irq.h> /* for MIPS_CPU_IRQ_BASE */
#ifdef CONFIG_NEC_CMBVR4133
#include <asm/vr41xx/cmbvr4133.h> /* for I8259A_IRQ_BASE */
#endif

#include_next <irq.h>

#endif /* __ASM_MACH_VR41XX_IRQ_H */
