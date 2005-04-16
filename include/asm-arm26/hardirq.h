#ifndef __ASM_HARDIRQ_H
#define __ASM_HARDIRQ_H

#include <linux/config.h>
#include <linux/cache.h>
#include <linux/threads.h>

typedef struct {
	unsigned int __softirq_pending;
} ____cacheline_aligned irq_cpustat_t;

#include <linux/irq_cpustat.h>	/* Standard mappings for irq_cpustat_t above */

#define HARDIRQ_BITS	8

/*
 * The hardirq mask has to be large enough to have space
 * for potentially all IRQ sources in the system nesting
 * on a single CPU:
 */
#if (1 << HARDIRQ_BITS) < NR_IRQS
# error HARDIRQ_BITS is too low!
#endif

#define irq_enter()		(preempt_count() += HARDIRQ_OFFSET)

#ifndef CONFIG_SMP

extern asmlinkage void __do_softirq(void);

#define irq_exit()                                                      \
        do {                                                            \
                preempt_count() -= IRQ_EXIT_OFFSET;                     \
                if (!in_interrupt() && local_softirq_pending())         \
                        __do_softirq();                                 \
                preempt_enable_no_resched();                            \
        } while (0)
#endif


#endif /* __ASM_HARDIRQ_H */
