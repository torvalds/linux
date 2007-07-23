#ifndef __ASM_SH_HW_IRQ_H
#define __ASM_SH_HW_IRQ_H

#include <linux/init.h>
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

/*
 * Enable individual interrupt mode for external IPR IRQs.
 */
void __init ipr_irq_enable_irlm(void);

typedef unsigned char intc_enum;

struct intc_vect {
	intc_enum enum_id;
	unsigned short vect;
};

#define INTC_VECT(enum_id, vect) { enum_id, vect }

struct intc_prio {
	intc_enum enum_id;
	unsigned char priority;
};

#define INTC_PRIO(enum_id, prio) { enum_id, prio }

struct intc_group {
	intc_enum enum_id;
	intc_enum *enum_ids;
};

#define INTC_GROUP(enum_id, ids...) { enum_id, (intc_enum []) { ids, 0 } }

struct intc_mask_reg {
	unsigned long set_reg, clr_reg, reg_width;
	intc_enum enum_ids[32];
};

struct intc_prio_reg {
	unsigned long reg, reg_width, field_width;
	intc_enum enum_ids[16];
};

struct intc_sense_reg {
	unsigned long reg, reg_width, field_width;
	intc_enum enum_ids[16];
};

struct intc_desc {
	struct intc_vect *vectors;
	unsigned int nr_vectors;
	struct intc_group *groups;
	unsigned int nr_groups;
	struct intc_prio *priorities;
	unsigned int nr_priorities;
	struct intc_mask_reg *mask_regs;
	unsigned int nr_mask_regs;
	struct intc_prio_reg *prio_regs;
	unsigned int nr_prio_regs;
	struct intc_sense_reg *sense_regs;
	unsigned int nr_sense_regs;
	struct irq_chip chip;
};

#define _INTC_ARRAY(a) a, sizeof(a)/sizeof(*a)
#define DECLARE_INTC_DESC(symbol, chipname, vectors, groups,		\
	priorities, mask_regs, prio_regs, sense_regs)			\
struct intc_desc symbol = {						\
	_INTC_ARRAY(vectors), _INTC_ARRAY(groups),			\
	_INTC_ARRAY(priorities),					\
	_INTC_ARRAY(mask_regs), _INTC_ARRAY(prio_regs),			\
	_INTC_ARRAY(sense_regs),					\
	.chip.name = chipname,						\
}

void __init register_intc_controller(struct intc_desc *desc);

void __init plat_irq_setup(void);

enum { IRQ_MODE_IRQ, IRQ_MODE_IRL7654, IRQ_MODE_IRL3210 };
void __init plat_irq_setup_pins(int mode);

#endif /* __ASM_SH_HW_IRQ_H */
