/*
 * include/asm-parisc/irq.h
 *
 * Copyright 2005 Matthew Wilcox <matthew@wil.cx>
 */

#ifndef _ASM_PARISC_IRQ_H
#define _ASM_PARISC_IRQ_H

#include <linux/config.h>
#include <linux/cpumask.h>
#include <asm/types.h>

#define NO_IRQ		(-1)

#ifdef CONFIG_GSC
#define GSC_IRQ_BASE	16
#define GSC_IRQ_MAX	63
#define CPU_IRQ_BASE	64
#else
#define CPU_IRQ_BASE	16
#endif

#define TIMER_IRQ	(CPU_IRQ_BASE + 0)
#define	IPI_IRQ		(CPU_IRQ_BASE + 1)
#define CPU_IRQ_MAX	(CPU_IRQ_BASE + (BITS_PER_LONG - 1))

#define NR_IRQS		(CPU_IRQ_MAX + 1)

/*
 * IRQ line status macro IRQ_PER_CPU is used
 */
#define ARCH_HAS_IRQ_PER_CPU

static __inline__ int irq_canonicalize(int irq)
{
	return (irq == 2) ? 9 : irq;
}

struct hw_interrupt_type;

/*
 * Some useful "we don't have to do anything here" handlers.  Should
 * probably be provided by the generic code.
 */
void no_ack_irq(unsigned int irq);
void no_end_irq(unsigned int irq);

extern int txn_alloc_irq(unsigned int nbits);
extern int txn_claim_irq(int);
extern unsigned int txn_alloc_data(unsigned int);
extern unsigned long txn_alloc_addr(unsigned int);
extern unsigned long txn_affinity_addr(unsigned int irq, int cpu);

extern int cpu_claim_irq(unsigned int irq, struct hw_interrupt_type *, void *);
extern int cpu_check_affinity(unsigned int irq, cpumask_t *dest);

/* soft power switch support (power.c) */
extern struct tasklet_struct power_tasklet;

#endif	/* _ASM_PARISC_IRQ_H */
