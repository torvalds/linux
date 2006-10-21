#ifndef LINUX_HTIRQ_H
#define LINUX_HTIRQ_H

/* Helper functions.. */
void write_ht_irq_low(unsigned int irq, u32 data);
void write_ht_irq_high(unsigned int irq, u32 data);
u32  read_ht_irq_low(unsigned int irq);
u32  read_ht_irq_high(unsigned int irq);
void mask_ht_irq(unsigned int irq);
void unmask_ht_irq(unsigned int irq);

/* The arch hook for getting things started */
int arch_setup_ht_irq(unsigned int irq, struct pci_dev *dev);

#endif /* LINUX_HTIRQ_H */
