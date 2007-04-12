#ifndef LINUX_MSI_H
#define LINUX_MSI_H

struct msi_msg {
	u32	address_lo;	/* low 32 bits of msi message address */
	u32	address_hi;	/* high 32 bits of msi message address */
	u32	data;		/* 16 bits of msi message data */
};

/* Helper functions */
extern void mask_msi_irq(unsigned int irq);
extern void unmask_msi_irq(unsigned int irq);
extern void read_msi_msg(unsigned int irq, struct msi_msg *msg);
extern void write_msi_msg(unsigned int irq, struct msi_msg *msg);

struct msi_desc {
	struct {
		__u8	type	: 5; 	/* {0: unused, 5h:MSI, 11h:MSI-X} */
		__u8	maskbit	: 1; 	/* mask-pending bit supported ?   */
		__u8	masked	: 1;
		__u8	is_64	: 1;	/* Address size: 0=32bit 1=64bit  */
		__u8	pos;	 	/* Location of the msi capability */
		__u16	entry_nr;    	/* specific enabled entry 	  */
		unsigned default_irq;	/* default pre-assigned irq	  */
	}msi_attrib;

	struct {
		__u16	head;
		__u16	tail;
	}link;

	void __iomem *mask_base;
	struct pci_dev *dev;

	/* Last set MSI message */
	struct msi_msg msg;
};

/*
 * The arch hook for setup up msi irqs
 */
int arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *desc);
void arch_teardown_msi_irq(unsigned int irq);


#endif /* LINUX_MSI_H */
