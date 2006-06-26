#ifndef _H8300_IRQ_H_
#define _H8300_IRQ_H_

#include <asm/ptrace.h>

#if defined(__H8300H__)
#define NR_IRQS 64
#define EXT_IRQ0 12
#define EXT_IRQ1 13
#define EXT_IRQ2 14
#define EXT_IRQ3 15
#define EXT_IRQ4 16
#define EXT_IRQ5 17
#define EXT_IRQ6 18
#define EXT_IRQ7 19
#define EXT_IRQS 5

#include <asm/regs306x.h>
#define h8300_clear_isr(irq)                                                \
do {                                                                        \
	if (irq >= EXT_IRQ0 && irq <= EXT_IRQ5)                             \
		*(volatile unsigned char *)ISR &= ~(1 << (irq - EXT_IRQ0)); \
} while(0)

#define IER_REGS *(volatile unsigned char *)IER
#endif
#if defined(CONFIG_CPU_H8S)
#define NR_IRQS 128
#define EXT_IRQ0 16
#define EXT_IRQ1 17
#define EXT_IRQ2 18
#define EXT_IRQ3 19
#define EXT_IRQ4 20
#define EXT_IRQ5 21
#define EXT_IRQ6 22
#define EXT_IRQ7 23
#define EXT_IRQ8 24
#define EXT_IRQ9 25
#define EXT_IRQ10 26
#define EXT_IRQ11 27
#define EXT_IRQ12 28
#define EXT_IRQ13 29
#define EXT_IRQ14 30
#define EXT_IRQ15 31
#define EXT_IRQS 15

#include <asm/regs267x.h>
#define h8300_clear_isr(irq)                                                 \
do {                                                                         \
	if (irq >= EXT_IRQ0 && irq <= EXT_IRQ15)                             \
		*(volatile unsigned short *)ISR &= ~(1 << (irq - EXT_IRQ0)); \
} while(0)

#define IER_REGS *(volatile unsigned short *)IER
#endif

static __inline__ int irq_canonicalize(int irq)
{
	return irq;
}

extern void enable_irq(unsigned int);
extern void disable_irq(unsigned int);
#define disable_irq_nosync(x)	disable_irq(x)

#endif /* _H8300_IRQ_H_ */
