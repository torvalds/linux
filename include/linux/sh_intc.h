#ifndef __SH_INTC_H
#define __SH_INTC_H

#include <linux/ioport.h>

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

struct intc_subgroup {
	unsigned long reg, reg_width;
	intc_enum parent_id;
	intc_enum enum_ids[32];
};

struct intc_mask_reg {
	unsigned long set_reg, clr_reg, reg_width;
	intc_enum enum_ids[32];
#ifdef CONFIG_INTC_BALANCING
	unsigned long dist_reg;
#endif
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

#ifdef CONFIG_INTC_BALANCING
#define INTC_SMP_BALANCING(reg)	.dist_reg = (reg)
#else
#define INTC_SMP_BALANCING(reg)
#endif

#ifdef CONFIG_SMP
#define INTC_SMP(stride, nr)	.smp = (stride) | ((nr) << 8)
#else
#define INTC_SMP(stride, nr)
#endif

struct intc_hw_desc {
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
	struct intc_mask_reg *ack_regs;
	unsigned int nr_ack_regs;
	struct intc_subgroup *subgroups;
	unsigned int nr_subgroups;
};

#define _INTC_ARRAY(a) a, a == NULL ? 0 : sizeof(a)/sizeof(*a)

#define INTC_HW_DESC(vectors, groups, mask_regs,	\
		     prio_regs,	sense_regs, ack_regs)	\
{							\
	_INTC_ARRAY(vectors), _INTC_ARRAY(groups),	\
	_INTC_ARRAY(mask_regs), _INTC_ARRAY(prio_regs),	\
	_INTC_ARRAY(sense_regs), _INTC_ARRAY(ack_regs),	\
}

struct intc_desc {
	char *name;
	struct resource *resource;
	unsigned int num_resources;
	intc_enum force_enable;
	intc_enum force_disable;
	struct intc_hw_desc hw;
};

#define DECLARE_INTC_DESC(symbol, chipname, vectors, groups,		\
	mask_regs, prio_regs, sense_regs)				\
struct intc_desc symbol __initdata = {					\
	.name = chipname,						\
	.hw = INTC_HW_DESC(vectors, groups, mask_regs,			\
			   prio_regs, sense_regs, NULL),		\
}

#define DECLARE_INTC_DESC_ACK(symbol, chipname, vectors, groups,	\
	mask_regs, prio_regs, sense_regs, ack_regs)			\
struct intc_desc symbol __initdata = {					\
	.name = chipname,						\
	.hw = INTC_HW_DESC(vectors, groups, mask_regs,			\
			   prio_regs, sense_regs, ack_regs),		\
}

int register_intc_controller(struct intc_desc *desc);
void reserve_intc_vectors(struct intc_vect *vectors, unsigned int nr_vecs);
int intc_set_priority(unsigned int irq, unsigned int prio);
int intc_irq_lookup(const char *chipname, intc_enum enum_id);
void intc_finalize(void);

#ifdef CONFIG_INTC_USERIMASK
int register_intc_userimask(unsigned long addr);
#else
static inline int register_intc_userimask(unsigned long addr)
{
	return 0;
}
#endif

int reserve_irq_vector(unsigned int irq);
void reserve_irq_legacy(void);

#endif /* __SH_INTC_H */
