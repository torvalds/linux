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

struct ipr_data {
	unsigned char irq;
	unsigned char ipr_idx;		/* Index for the IPR registered */
	unsigned char shift;		/* Number of bits to shift the data */
	unsigned char priority;		/* The priority */
};

struct ipr_desc {
	unsigned long *ipr_offsets;
	unsigned int nr_offsets;
	struct ipr_data *ipr_data;
	unsigned int nr_irqs;
	struct irq_chip chip;
};

void register_ipr_controller(struct ipr_desc *);
void init_IRQ_ipr(void);

/*
 * Enable individual interrupt mode for external IPR IRQs.
 */
void ipr_irq_enable_irlm(void);

#endif /* __ASM_SH_HW_IRQ_H */
