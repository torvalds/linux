#ifndef __ASM_SH_HW_IRQ_H
#define __ASM_SH_HW_IRQ_H

#include <asm/atomic.h>

extern atomic_t irq_err_count;

struct intc2_data {
	unsigned short irq;
	unsigned char ipr_offset, ipr_shift;
	unsigned char msk_offset, msk_shift;
	unsigned char priority;
};

struct intc2_desc {
	unsigned long prio_base;
	unsigned long msk_base;
	unsigned long mskclr_base;
	struct intc2_data *intc2_data;
	unsigned int nr_irqs;
	struct irq_chip chip;
};

void register_intc2_controller(struct intc2_desc *);
void init_IRQ_intc2(void);

#endif /* __ASM_SH_HW_IRQ_H */
