#ifndef _ALPHA_HW_IRQ_H
#define _ALPHA_HW_IRQ_H

#include <linux/config.h>

static inline void hw_resend_irq(struct hw_interrupt_type *h, unsigned int i) {}

extern volatile unsigned long irq_err_count;

#ifdef CONFIG_ALPHA_GENERIC
#define ACTUAL_NR_IRQS	alpha_mv.nr_irqs
#else
#define ACTUAL_NR_IRQS	NR_IRQS
#endif

#endif
