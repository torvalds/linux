/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_HTIRQ_H
#define LINUX_HTIRQ_H

struct pci_dev;
struct irq_data;

struct ht_irq_msg {
	u32	address_lo;	/* low 32 bits of the ht irq message */
	u32	address_hi;	/* high 32 bits of the it irq message */
};

typedef void (ht_irq_update_t)(struct pci_dev *dev, int irq,
			       struct ht_irq_msg *msg);

struct ht_irq_cfg {
	struct pci_dev *dev;
	 /* Update callback used to cope with buggy hardware */
	ht_irq_update_t *update;
	unsigned pos;
	unsigned idx;
	struct ht_irq_msg msg;
};

/* Helper functions.. */
void fetch_ht_irq_msg(unsigned int irq, struct ht_irq_msg *msg);
void write_ht_irq_msg(unsigned int irq, struct ht_irq_msg *msg);
void mask_ht_irq(struct irq_data *data);
void unmask_ht_irq(struct irq_data *data);

/* The arch hook for getting things started */
int arch_setup_ht_irq(int idx, int pos, struct pci_dev *dev,
		      ht_irq_update_t *update);
void arch_teardown_ht_irq(unsigned int irq);

/* For drivers of buggy hardware */
int __ht_create_irq(struct pci_dev *dev, int idx, ht_irq_update_t *update);

#endif /* LINUX_HTIRQ_H */
