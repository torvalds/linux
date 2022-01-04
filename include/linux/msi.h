/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MSI_H
#define LINUX_MSI_H

#include <linux/kobject.h>
#include <linux/list.h>
#include <asm/msi.h>

/* Dummy shadow structures if an architecture does not define them */
#ifndef arch_msi_msg_addr_lo
typedef struct arch_msi_msg_addr_lo {
	u32	address_lo;
} __attribute__ ((packed)) arch_msi_msg_addr_lo_t;
#endif

#ifndef arch_msi_msg_addr_hi
typedef struct arch_msi_msg_addr_hi {
	u32	address_hi;
} __attribute__ ((packed)) arch_msi_msg_addr_hi_t;
#endif

#ifndef arch_msi_msg_data
typedef struct arch_msi_msg_data {
	u32	data;
} __attribute__ ((packed)) arch_msi_msg_data_t;
#endif

/**
 * msi_msg - Representation of a MSI message
 * @address_lo:		Low 32 bits of msi message address
 * @arch_addrlo:	Architecture specific shadow of @address_lo
 * @address_hi:		High 32 bits of msi message address
 *			(only used when device supports it)
 * @arch_addrhi:	Architecture specific shadow of @address_hi
 * @data:		MSI message data (usually 16 bits)
 * @arch_data:		Architecture specific shadow of @data
 */
struct msi_msg {
	union {
		u32			address_lo;
		arch_msi_msg_addr_lo_t	arch_addr_lo;
	};
	union {
		u32			address_hi;
		arch_msi_msg_addr_hi_t	arch_addr_hi;
	};
	union {
		u32			data;
		arch_msi_msg_data_t	arch_data;
	};
};

extern int pci_msi_ignore_mask;
/* Helper functions */
struct irq_data;
struct msi_desc;
struct pci_dev;
struct platform_msi_priv_data;
void __get_cached_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
#ifdef CONFIG_GENERIC_MSI_IRQ
void get_cached_msi_msg(unsigned int irq, struct msi_msg *msg);
#else
static inline void get_cached_msi_msg(unsigned int irq, struct msi_msg *msg)
{
}
#endif

typedef void (*irq_write_msi_msg_t)(struct msi_desc *desc,
				    struct msi_msg *msg);

/**
 * platform_msi_desc - Platform device specific msi descriptor data
 * @msi_priv_data:	Pointer to platform private data
 * @msi_index:		The index of the MSI descriptor for multi MSI
 */
struct platform_msi_desc {
	struct platform_msi_priv_data	*msi_priv_data;
	u16				msi_index;
};

/**
 * fsl_mc_msi_desc - FSL-MC device specific msi descriptor data
 * @msi_index:		The index of the MSI descriptor
 */
struct fsl_mc_msi_desc {
	u16				msi_index;
};

/**
 * ti_sci_inta_msi_desc - TISCI based INTA specific msi descriptor data
 * @dev_index: TISCI device index
 */
struct ti_sci_inta_msi_desc {
	u16	dev_index;
};

/**
 * struct msi_desc - Descriptor structure for MSI based interrupts
 * @list:	List head for management
 * @irq:	The base interrupt number
 * @nvec_used:	The number of vectors used
 * @dev:	Pointer to the device which uses this descriptor
 * @msg:	The last set MSI message cached for reuse
 * @affinity:	Optional pointer to a cpu affinity mask for this descriptor
 *
 * @write_msi_msg:	Callback that may be called when the MSI message
 *			address or data changes
 * @write_msi_msg_data:	Data parameter for the callback.
 *
 * @msi_mask:	[PCI MSI]   MSI cached mask bits
 * @msix_ctrl:	[PCI MSI-X] MSI-X cached per vector control bits
 * @is_msix:	[PCI MSI/X] True if MSI-X
 * @multiple:	[PCI MSI/X] log2 num of messages allocated
 * @multi_cap:	[PCI MSI/X] log2 num of messages supported
 * @maskbit:	[PCI MSI/X] Mask-Pending bit supported?
 * @is_64:	[PCI MSI/X] Address size: 0=32bit 1=64bit
 * @entry_nr:	[PCI MSI/X] Entry which is described by this descriptor
 * @default_irq:[PCI MSI/X] The default pre-assigned non-MSI irq
 * @mask_pos:	[PCI MSI]   Mask register position
 * @mask_base:	[PCI MSI-X] Mask register base address
 * @platform:	[platform]  Platform device specific msi descriptor data
 * @fsl_mc:	[fsl-mc]    FSL MC device specific msi descriptor data
 * @inta:	[INTA]	    TISCI based INTA specific msi descriptor data
 */
struct msi_desc {
	/* Shared device/bus type independent data */
	struct list_head		list;
	unsigned int			irq;
	unsigned int			nvec_used;
	struct device			*dev;
	struct msi_msg			msg;
	struct irq_affinity_desc	*affinity;
#ifdef CONFIG_IRQ_MSI_IOMMU
	const void			*iommu_cookie;
#endif

	void (*write_msi_msg)(struct msi_desc *entry, void *data);
	void *write_msi_msg_data;

	union {
		/* PCI MSI/X specific data */
		struct {
			union {
				u32 msi_mask;
				u32 msix_ctrl;
			};
			struct {
				u8	is_msix		: 1;
				u8	multiple	: 3;
				u8	multi_cap	: 3;
				u8	maskbit		: 1;
				u8	is_64		: 1;
				u8	is_virtual	: 1;
				u16	entry_nr;
				unsigned default_irq;
			} msi_attrib;
			union {
				u8	mask_pos;
				void __iomem *mask_base;
			};
		};

		/*
		 * Non PCI variants add their data structure here. New
		 * entries need to use a named structure. We want
		 * proper name spaces for this. The PCI part is
		 * anonymous for now as it would require an immediate
		 * tree wide cleanup.
		 */
		struct platform_msi_desc platform;
		struct fsl_mc_msi_desc fsl_mc;
		struct ti_sci_inta_msi_desc inta;
	};
};

/* Helpers to hide struct msi_desc implementation details */
#define msi_desc_to_dev(desc)		((desc)->dev)
#define dev_to_msi_list(dev)		(&(dev)->msi_list)
#define first_msi_entry(dev)		\
	list_first_entry(dev_to_msi_list((dev)), struct msi_desc, list)
#define for_each_msi_entry(desc, dev)	\
	list_for_each_entry((desc), dev_to_msi_list((dev)), list)
#define for_each_msi_entry_safe(desc, tmp, dev)	\
	list_for_each_entry_safe((desc), (tmp), dev_to_msi_list((dev)), list)
#define for_each_msi_vector(desc, __irq, dev)				\
	for_each_msi_entry((desc), (dev))				\
		if ((desc)->irq)					\
			for (__irq = (desc)->irq;			\
			     __irq < ((desc)->irq + (desc)->nvec_used);	\
			     __irq++)

#ifdef CONFIG_IRQ_MSI_IOMMU
static inline const void *msi_desc_get_iommu_cookie(struct msi_desc *desc)
{
	return desc->iommu_cookie;
}

static inline void msi_desc_set_iommu_cookie(struct msi_desc *desc,
					     const void *iommu_cookie)
{
	desc->iommu_cookie = iommu_cookie;
}
#else
static inline const void *msi_desc_get_iommu_cookie(struct msi_desc *desc)
{
	return NULL;
}

static inline void msi_desc_set_iommu_cookie(struct msi_desc *desc,
					     const void *iommu_cookie)
{
}
#endif

#ifdef CONFIG_PCI_MSI
#define first_pci_msi_entry(pdev)	first_msi_entry(&(pdev)->dev)
#define for_each_pci_msi_entry(desc, pdev)	\
	for_each_msi_entry((desc), &(pdev)->dev)

struct pci_dev *msi_desc_to_pci_dev(struct msi_desc *desc);
void *msi_desc_to_pci_sysdata(struct msi_desc *desc);
void pci_write_msi_msg(unsigned int irq, struct msi_msg *msg);
#else /* CONFIG_PCI_MSI */
static inline void *msi_desc_to_pci_sysdata(struct msi_desc *desc)
{
	return NULL;
}
static inline void pci_write_msi_msg(unsigned int irq, struct msi_msg *msg)
{
}
#endif /* CONFIG_PCI_MSI */

struct msi_desc *alloc_msi_entry(struct device *dev, int nvec,
				 const struct irq_affinity_desc *affinity);
void free_msi_entry(struct msi_desc *entry);
void __pci_read_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
void __pci_write_msi_msg(struct msi_desc *entry, struct msi_msg *msg);

void pci_msi_mask_irq(struct irq_data *data);
void pci_msi_unmask_irq(struct irq_data *data);

const struct attribute_group **msi_populate_sysfs(struct device *dev);
void msi_destroy_sysfs(struct device *dev,
		       const struct attribute_group **msi_irq_groups);

/*
 * The arch hooks to setup up msi irqs. Default functions are implemented
 * as weak symbols so that they /can/ be overriden by architecture specific
 * code if needed. These hooks can only be enabled by the architecture.
 *
 * If CONFIG_PCI_MSI_ARCH_FALLBACKS is not selected they are replaced by
 * stubs with warnings.
 */
#ifdef CONFIG_PCI_MSI_ARCH_FALLBACKS
int arch_setup_msi_irq(struct pci_dev *dev, struct msi_desc *desc);
void arch_teardown_msi_irq(unsigned int irq);
int arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type);
void arch_teardown_msi_irqs(struct pci_dev *dev);
#else
static inline int arch_setup_msi_irqs(struct pci_dev *dev, int nvec, int type)
{
	WARN_ON_ONCE(1);
	return -ENODEV;
}

static inline void arch_teardown_msi_irqs(struct pci_dev *dev)
{
	WARN_ON_ONCE(1);
}
#endif

/*
 * The restore hooks are still available as they are useful even
 * for fully irq domain based setups. Courtesy to XEN/X86.
 */
void arch_restore_msi_irqs(struct pci_dev *dev);
void default_restore_msi_irqs(struct pci_dev *dev);

#ifdef CONFIG_GENERIC_MSI_IRQ_DOMAIN

#include <linux/irqhandler.h>

struct irq_domain;
struct irq_domain_ops;
struct irq_chip;
struct device_node;
struct fwnode_handle;
struct msi_domain_info;

/**
 * struct msi_domain_ops - MSI interrupt domain callbacks
 * @get_hwirq:		Retrieve the resulting hw irq number
 * @msi_init:		Domain specific init function for MSI interrupts
 * @msi_free:		Domain specific function to free a MSI interrupts
 * @msi_check:		Callback for verification of the domain/info/dev data
 * @msi_prepare:	Prepare the allocation of the interrupts in the domain
 * @msi_finish:		Optional callback to finalize the allocation
 * @set_desc:		Set the msi descriptor for an interrupt
 * @handle_error:	Optional error handler if the allocation fails
 * @domain_alloc_irqs:	Optional function to override the default allocation
 *			function.
 * @domain_free_irqs:	Optional function to override the default free
 *			function.
 *
 * @get_hwirq, @msi_init and @msi_free are callbacks used by
 * msi_create_irq_domain() and related interfaces
 *
 * @msi_check, @msi_prepare, @msi_finish, @set_desc and @handle_error
 * are callbacks used by msi_domain_alloc_irqs() and related
 * interfaces which are based on msi_desc.
 *
 * @domain_alloc_irqs, @domain_free_irqs can be used to override the
 * default allocation/free functions (__msi_domain_alloc/free_irqs). This
 * is initially for a wrapper around XENs seperate MSI universe which can't
 * be wrapped into the regular irq domains concepts by mere mortals.  This
 * allows to universally use msi_domain_alloc/free_irqs without having to
 * special case XEN all over the place.
 *
 * Contrary to other operations @domain_alloc_irqs and @domain_free_irqs
 * are set to the default implementation if NULL and even when
 * MSI_FLAG_USE_DEF_DOM_OPS is not set to avoid breaking existing users and
 * because these callbacks are obviously mandatory.
 *
 * This is NOT meant to be abused, but it can be useful to build wrappers
 * for specialized MSI irq domains which need extra work before and after
 * calling __msi_domain_alloc_irqs()/__msi_domain_free_irqs().
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
	int		(*domain_alloc_irqs)(struct irq_domain *domain,
					     struct device *dev, int nvec);
	void		(*domain_free_irqs)(struct irq_domain *domain,
					    struct device *dev);
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
	/* Support multiple PCI MSI interrupts */
	MSI_FLAG_MULTI_PCI_MSI		= (1 << 2),
	/* Support PCI MSIX interrupts */
	MSI_FLAG_PCI_MSIX		= (1 << 3),
	/* Needs early activate, required for PCI */
	MSI_FLAG_ACTIVATE_EARLY		= (1 << 4),
	/*
	 * Must reactivate when irq is started even when
	 * MSI_FLAG_ACTIVATE_EARLY has been set.
	 */
	MSI_FLAG_MUST_REACTIVATE	= (1 << 5),
	/* Is level-triggered capable, using two messages */
	MSI_FLAG_LEVEL_CAPABLE		= (1 << 6),
};

int msi_domain_set_affinity(struct irq_data *data, const struct cpumask *mask,
			    bool force);

struct irq_domain *msi_create_irq_domain(struct fwnode_handle *fwnode,
					 struct msi_domain_info *info,
					 struct irq_domain *parent);
int __msi_domain_alloc_irqs(struct irq_domain *domain, struct device *dev,
			    int nvec);
int msi_domain_alloc_irqs(struct irq_domain *domain, struct device *dev,
			  int nvec);
void __msi_domain_free_irqs(struct irq_domain *domain, struct device *dev);
void msi_domain_free_irqs(struct irq_domain *domain, struct device *dev);
struct msi_domain_info *msi_get_domain_info(struct irq_domain *domain);

struct irq_domain *platform_msi_create_irq_domain(struct fwnode_handle *fwnode,
						  struct msi_domain_info *info,
						  struct irq_domain *parent);
int platform_msi_domain_alloc_irqs(struct device *dev, unsigned int nvec,
				   irq_write_msi_msg_t write_msi_msg);
void platform_msi_domain_free_irqs(struct device *dev);

/* When an MSI domain is used as an intermediate domain */
int msi_domain_prepare_irqs(struct irq_domain *domain, struct device *dev,
			    int nvec, msi_alloc_info_t *args);
int msi_domain_populate_irqs(struct irq_domain *domain, struct device *dev,
			     int virq, int nvec, msi_alloc_info_t *args);
struct irq_domain *
__platform_msi_create_device_domain(struct device *dev,
				    unsigned int nvec,
				    bool is_tree,
				    irq_write_msi_msg_t write_msi_msg,
				    const struct irq_domain_ops *ops,
				    void *host_data);

#define platform_msi_create_device_domain(dev, nvec, write, ops, data)	\
	__platform_msi_create_device_domain(dev, nvec, false, write, ops, data)
#define platform_msi_create_device_tree_domain(dev, nvec, write, ops, data) \
	__platform_msi_create_device_domain(dev, nvec, true, write, ops, data)

int platform_msi_domain_alloc(struct irq_domain *domain, unsigned int virq,
			      unsigned int nr_irqs);
void platform_msi_domain_free(struct irq_domain *domain, unsigned int virq,
			      unsigned int nvec);
void *platform_msi_get_host_data(struct irq_domain *domain);
#endif /* CONFIG_GENERIC_MSI_IRQ_DOMAIN */

#ifdef CONFIG_PCI_MSI_IRQ_DOMAIN
void pci_msi_domain_write_msg(struct irq_data *irq_data, struct msi_msg *msg);
struct irq_domain *pci_msi_create_irq_domain(struct fwnode_handle *fwnode,
					     struct msi_domain_info *info,
					     struct irq_domain *parent);
int pci_msi_domain_check_cap(struct irq_domain *domain,
			     struct msi_domain_info *info, struct device *dev);
u32 pci_msi_domain_get_msi_rid(struct irq_domain *domain, struct pci_dev *pdev);
struct irq_domain *pci_msi_get_device_domain(struct pci_dev *pdev);
bool pci_dev_has_special_msi_domain(struct pci_dev *pdev);
#else
static inline struct irq_domain *pci_msi_get_device_domain(struct pci_dev *pdev)
{
	return NULL;
}
#endif /* CONFIG_PCI_MSI_IRQ_DOMAIN */

#endif /* LINUX_MSI_H */
