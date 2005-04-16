#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/config.h>
#include <linux/threads.h>
#include <linux/irq.h>
#include <asm/pda.h>
#include <asm/apic.h>

#define __ARCH_IRQ_STAT 1

/* Generate a lvalue for a pda member. Should fix softirq.c instead to use
   special access macros. This would generate better code. */ 
#define __IRQ_STAT(cpu,member) (read_pda(me)->member)

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

/*
 * 'what should we do if we get a hw irq event on an illegal vector'.
 * each architecture has to answer this themselves.
 */
static inline void ack_bad_irq(unsigned int irq)
{
	printk("unexpected IRQ trap at vector %02x\n", irq);
#ifdef CONFIG_X86_LOCAL_APIC
	/*
	 * Currently unexpected vectors happen only on SMP and APIC.
	 * We _must_ ack these because every local APIC has only N
	 * irq slots per priority level, and a 'hanging, unacked' IRQ
	 * holds up an irq slot - in excessive cases (when multiple
	 * unexpected vectors occur) that might lock up the APIC
	 * completely.
	 */
	ack_APIC_irq();
#endif
}
#endif /* __ASM_HARDIRQ_H */
