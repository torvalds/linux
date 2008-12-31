#ifndef __SH_INTC_H
#define __SH_INTC_H

typedef unsigned char intc_enum;

struct intc_vect {
	intc_enum enum_id;
	unsigned short vect;
};

#define INTC_VECT(enum_id, vect) { enum_id, vect }
#define INTC_IRQ(enum_id, irq) INTC_VECT(enum_id, irq2evt(irq))

struct intc_group {
	intc_enum enum_id;
	intc_enum enum_ids[32];
};

#define INTC_GROUP(enum_id, ids...) { enum_id, { ids } }

struct intc_mask_reg {
	unsigned long set_reg, clr_reg, reg_width;
	intc_enum enum_ids[32];
#ifdef CONFIG_SMP
	unsigned long smp;
#endif
};

struct intc_prio_reg {
	unsigned long set_reg, clr_reg, reg_width, field_width;
	intc_enum enum_ids[16];
#ifdef CONFIG_SMP
	unsigned long smp;
#endif
};

struct intc_sense_reg {
	unsigned long reg, reg_width, field_width;
	intc_enum enum_ids[16];
};

#ifdef CONFIG_SMP
#define INTC_SMP(stride, nr) .smp = (stride) | ((nr) << 8)
#else
#define INTC_SMP(stride, nr)
#endif

struct intc_desc {
	struct intc_vect *vectors;
	unsigned int nr_vectors;
	struct intc_group *groups;
	unsigned int nr_groups;
	struct intc_mask_reg *mask_regs;
	unsigned int nr_mask_regs;
	struct intc_prio_reg *prio_regs;
	unsigned int nr_prio_regs;
	struct intc_sense_reg *sense_regs;
	unsigned int nr_sense_regs;
	char *name;
#if defined(CONFIG_CPU_SH3) || defined(CONFIG_CPU_SH4A)
	struct intc_mask_reg *ack_regs;
	unsigned int nr_ack_regs;
#endif
};

#define _INTC_ARRAY(a) a, sizeof(a)/sizeof(*a)
#define DECLARE_INTC_DESC(symbol, chipname, vectors, groups,		\
	mask_regs, prio_regs, sense_regs)				\
struct intc_desc symbol __initdata = {					\
	_INTC_ARRAY(vectors), _INTC_ARRAY(groups),			\
	_INTC_ARRAY(mask_regs), _INTC_ARRAY(prio_regs),			\
	_INTC_ARRAY(sense_regs),					\
	chipname,							\
}

#if defined(CONFIG_CPU_SH3) || defined(CONFIG_CPU_SH4A)
#define DECLARE_INTC_DESC_ACK(symbol, chipname, vectors, groups,	\
	mask_regs, prio_regs, sense_regs, ack_regs)			\
struct intc_desc symbol __initdata = {					\
	_INTC_ARRAY(vectors), _INTC_ARRAY(groups),			\
	_INTC_ARRAY(mask_regs), _INTC_ARRAY(prio_regs),			\
	_INTC_ARRAY(sense_regs),					\
	chipname,							\
	_INTC_ARRAY(ack_regs),						\
}
#endif

void __init register_intc_controller(struct intc_desc *desc);
int intc_set_priority(unsigned int irq, unsigned int prio);

#endif /* __SH_INTC_H */
