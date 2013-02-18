#ifndef __IRQ_RENESAS_INTC_IRQPIN_H__
#define __IRQ_RENESAS_INTC_IRQPIN_H__

struct renesas_intc_irqpin_config {
	unsigned int sense_bitfield_width;
	unsigned int irq_base;
	bool control_parent;
};

#endif /* __IRQ_RENESAS_INTC_IRQPIN_H__ */
