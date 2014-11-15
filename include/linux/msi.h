#ifndef LINUX_MSI_H
#define LINUX_MSI_H

#include <linux/kobject.h>
#include <linux/list.h>

struct msi_msg {
	u32	address_lo;	/* low 32 bits of msi message address */
	u32	address_hi;	/* high 32 bits of msi message address */
	u32	data;		/* 16 bits of msi message data */
};

extern int pci_msi_ignore_mask;
/* Helper functions */
struct irq_data;
struct msi_desc;
void __get_cached_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
void get_cached_msi_msg(unsigned int irq, struct msi_msg *msg);

struct msi_desc {
	struct {
		__u8	is_msix	: 1;
		__u8	multiple: 3;	/* log2 num of messages allocated */
		__u8	multi_cap : 3;	/* log2 num of messages supported */
		__u8	maskbit	: 1;	/* mask-pending bit supported ? */
		__u8	is_64	: 1;	/* Address size: 0=32bit 1=64bit */
		__u16	entry_nr;	/* specific enabled entry */
		unsigned default_irq;	/* default pre-assigned irq */
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
};

/* Helpers to hide struct msi_desc implementation details */
#define msi_desc_to_dev(desc)		(&(desc)->dev.dev)
#define dev_to_msi_list(dev)		(&to_pci_dev((dev))->msi_list)
#define first_msi_entry(dev)		\
	list_first_entry(dev_to_msi_list((dev)), struct msi_desc, list)
#define for_each_msi_entry(desc, dev)	\
	list_for_each_entry((desc), dev_to_msi_list((dev)), list)

#ifdef CONFIG_PCI_MSI
#define first_pci_msi_entry(pdev)	first_msi_entry(&(pdev)->dev)
#define for_each_pci_msi_entry(desc, pdev)	\
	for_each_msi_entry((desc), &(pdev)->dev)

static inline struct pci_dev *msi_desc_to_pci_dev(struct msi_desc *desc)
{
	return desc->dev;
}
#endif /* CONFIG_PCI_MSI */

void __pci_read_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
void __pci_write_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
void pci_write_msi_msg(unsigned int irq, struct msi_msg *msg);

u32 __pci_msix_desc_mask_irq(struct msi_desc *desc, u32 flag);
u32 __pci_msi_desc_mask_irq(struct msi_desc *desc, u32 mask, u32 flag);
void pci_msi_mask_irq(struct irq_data *data);
void pci_msi_unmask_irq(struct irq_data *data);

/* Conversion helpers. Should be removed after merging */
static inline void __write_msi_msg(struct msi_desc *entry, struct msi_msg *msg)
{
	__pci_write_msi_msg(entry, msg);
}
static inline void write_msi_msg(int irq, struct msi_msg *msg)
{
	pci_write_msi_msg(irq, msg);
}
static inline void mask_msi_irq(struct irq_data *data)
{
	pci_msi_mask_irq(data);
}
static inline void unmask_msi_irq(struct irq_data *data)
{
	pci_msi_unmask_irq(data);
}

/*
 * The arch hooks to setup up msi irqs. Those functions are
 * implemented as weak symbols so that they /can/ be overriden by
 * architecture specific code if needed.
 */
int arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *desc);
void arch_teardown_msi_irq(unsigned int irq);
int arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type);
void arch_teardown_msi_irqs(struct pci_dev *dev);
void arch_restore_msi_irqs(struct pci_dev *dev);

void default_teardown_msi_irqs(struct pci_dev *dev);
void default_restore_msi_irqs(struct pci_dev *dev);

struct msi_controller {
	struct module *owner;
	struct device *dev;
	struct device_node *of_node;
	struct list_head list;

	int (*setup_irq)(struct msi_controller *chip, struct pci_dev *dev,
			 struct msi_desc *desc);
	void (*teardown_irq)(struct msi_controller *chip, unsigned int irq);
};

#ifdef CONFIG_GENERIC_MSI_IRQ_DOMAIN

#include <linux/irqhandler.h>
#include <asm/msi.h>

struct irq_domain;
struct irq_chip;
struct device_node;
struct msi_domain_info;

/**
 * struct msi_domain_ops - MSI interrupt domain callbacks
 * @get_hwirq:		Retrieve the resulting hw irq number
 * @msi_init:		Domain specific init function for MSI interrupts
 * @msi_free:		Domain specific function to free a MSI interrupts
 * @msi_check:		Callback for verification of the domain/info/dev data
 * @msi_prepare:	Prepare the allocation of the interrupts in the domain
 * @msi_finish:		Optional callbacl to finalize the allocation
 * @set_desc:		Set the msi descriptor for an interrupt
 * @handle_error:	Optional error handler if the allocation fails
 *
 * @get_hwirq, @msi_init and @msi_free are callbacks used by
 * msi_create_irq_domain() and related interfaces
 *
 * @msi_check, @msi_prepare, @msi_finish, @set_desc and @handle_error
 * are callbacks used by msi_irq_domain_alloc_irqs() and related
 * interfaces which are based on msi_desc.
 */
struct msi_domain_ops {
	irq_hw_number_t	(*get_hwirq)(struct msi_domain_info *info,
				     msi_alloc_info_t *arg);
	int		(*msi_init)(struct irq_domain *domain,
				    struct msi_domain_info *info,
				    unsigned int virq, irq_hw_number_t hwirq,
				    msi_alloc_info_t *arg);
	void		(*msi_free)(struct irq_domain *domain,
				    struct msi_domain_info *info,
				    unsigned int virq);
	int		(*msi_check)(struct irq_domain *domain,
				     struct msi_domain_info *info,
				     struct device *dev);
	int		(*msi_prepare)(struct irq_domain *domain,
				       struct device *dev, int nvec,
				       msi_alloc_info_t *arg);
	void		(*msi_finish)(msi_alloc_info_t *arg, int retval);
	void		(*set_desc)(msi_alloc_info_t *arg,
				    struct msi_desc *desc);
	int		(*handle_error)(struct irq_domain *domain,
					struct msi_desc *desc, int error);
};

/**
 * struct msi_domain_info - MSI interrupt domain data
 * @flags:		Flags to decribe features and capabilities
 * @ops:		The callback data structure
 * @chip:		Optional: associated interrupt chip
 * @chip_data:		Optional: associated interrupt chip data
 * @handler:		Optional: associated interrupt flow handler
 * @handler_data:	Optional: associated interrupt flow handler data
 * @handler_name:	Optional: associated interrupt flow handler name
 * @data:		Optional: domain specific data
 */
struct msi_domain_info {
	u32			flags;
	struct msi_domain_ops	*ops;
	struct irq_chip		*chip;
	void			*chip_data;
	irq_flow_handler_t	handler;
	void			*handler_data;
	const char		*handler_name;
	void			*data;
};

/* Flags for msi_domain_info */
enum {
	/*
	 * Init non implemented ops callbacks with default MSI domain
	 * callbacks.
	 */
	MSI_FLAG_USE_DEF_DOM_OPS	= (1 << 0),
	/*
	 * Init non implemented chip callbacks with default MSI chip
	 * callbacks.
	 */
	MSI_FLAG_USE_DEF_CHIP_OPS	= (1 << 1),
	/* Build identity map between hwirq and irq */
	MSI_FLAG_IDENTITY_MAP		= (1 << 2),
	/* Support multiple PCI MSI interrupts */
	MSI_FLAG_MULTI_PCI_MSI		= (1 << 3),
	/* Support PCI MSIX interrupts */
	MSI_FLAG_PCI_MSIX		= (1 << 4),
};

int msi_domain_set_affinity(struct irq_data *data, const struct cpumask *mask,
			    bool force);

struct irq_domain *msi_create_irq_domain(struct device_node *of_node,
					 struct msi_domain_info *info,
					 struct irq_domain *parent);
int msi_domain_alloc_irqs(struct irq_domain *domain, struct device *dev,
			  int nvec);
void msi_domain_free_irqs(struct irq_domain *domain, struct device *dev);
struct msi_domain_info *msi_get_domain_info(struct irq_domain *domain);

#endif /* CONFIG_GENERIC_MSI_IRQ_DOMAIN */

#ifdef CONFIG_PCI_MSI_IRQ_DOMAIN
void pci_msi_domain_write_msg(struct irq_data *irq_data, struct msi_msg *msg);
struct irq_domain *pci_msi_create_irq_domain(struct device_node *node,
					     struct msi_domain_info *info,
					     struct irq_domain *parent);
int pci_msi_domain_alloc_irqs(struct irq_domain *domain, struct pci_dev *dev,
			      int nvec, int type);
void pci_msi_domain_free_irqs(struct irq_domain *domain, struct pci_dev *dev);
struct irq_domain *pci_msi_create_default_irq_domain(struct device_node *node,
		 struct msi_domain_info *info, struct irq_domain *parent);

irq_hw_number_t pci_msi_domain_calc_hwirq(struct pci_dev *dev,
					  struct msi_desc *desc);
int pci_msi_domain_check_cap(struct irq_domain *domain,
			     struct msi_domain_info *info, struct device *dev);
#endif /* CONFIG_PCI_MSI_IRQ_DOMAIN */

#endif /* LINUX_MSI_H */
