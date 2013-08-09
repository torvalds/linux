#ifndef LINUX_MSI_H
#define LINUX_MSI_H

#include <linux/kobject.h>
#include <linux/list.h>

struct msi_msg {
	u32	address_lo;	/* low 32 bits of msi message address */
	u32	address_hi;	/* high 32 bits of msi message address */
	u32	data;		/* 16 bits of msi message data */
};

/* Helper functions */
struct irq_data;
struct msi_desc;
void mask_msi_irq(struct irq_data *data);
void unmask_msi_irq(struct irq_data *data);
void __read_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
void __get_cached_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
void __write_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
void read_msi_msg(unsigned int irq, struct msi_msg *msg);
void get_cached_msi_msg(unsigned int irq, struct msi_msg *msg);
void write_msi_msg(unsigned int irq, struct msi_msg *msg);

struct msi_desc {
	struct {
		__u8	is_msix	: 1;
		__u8	multiple: 3;	/* log2 number of messages */
		__u8	maskbit	: 1; 	/* mask-pending bit supported ?   */
		__u8	is_64	: 1;	/* Address size: 0=32bit 1=64bit  */
		__u8	pos;	 	/* Location of the msi capability */
		__u16	entry_nr;    	/* specific enabled entry 	  */
		unsigned default_irq;	/* default pre-assigned irq	  */
	} msi_attrib;

	u32 masked;			/* mask bits */
	unsigned int irq;
	unsigned int nvec_used;		/* number of messages */
	struct list_head list;

	union {
		void __iomem *mask_base;
		u8 mask_pos;
	};
	struct pci_dev *dev;

	/* Last set MSI message */
	struct msi_msg msg;

	struct kobject kobj;
};

/*
 * The arch hooks to setup up msi irqs. Those functions are
 * implemented as weak symbols so that they /can/ be overriden by
 * architecture specific code if needed.
 */
int arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *desc);
void arch_teardown_msi_irq(unsigned int irq);
int arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type);
void arch_teardown_msi_irqs(struct pci_dev *dev);
int arch_msi_check_device(struct pci_dev* dev, int nvec, int type);
void arch_restore_msi_irqs(struct pci_dev *dev, int irq);

void default_teardown_msi_irqs(struct pci_dev *dev);
void default_restore_msi_irqs(struct pci_dev *dev, int irq);

struct msi_chip {
	struct module *owner;
	struct device *dev;

	int (*setup_irq)(struct msi_chip *chip, struct pci_dev *dev,
			 struct msi_desc *desc);
	void (*teardown_irq)(struct msi_chip *chip, unsigned int irq);
	int (*check_device)(struct msi_chip *chip, struct pci_dev *dev,
			    int nvec, int type);
};

#endif /* LINUX_MSI_H */
