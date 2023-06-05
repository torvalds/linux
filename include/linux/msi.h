/* SPDX-License-Identifier: GPL-2.0 */
#ifndef LINUX_MSI_H
#define LINUX_MSI_H

/*
 * This header file contains MSI data structures and functions which are
 * only relevant for:
 *	- Interrupt core code
 *	- PCI/MSI core code
 *	- MSI interrupt domain implementations
 *	- IOMMU, low level VFIO, NTB and other justified exceptions
 *	  dealing with low level MSI details.
 *
 * Regular device drivers have no business with any of these functions and
 * especially storing MSI descriptor pointers in random code is considered
 * abuse.
 *
 * Device driver relevant functions are available in <linux/msi_api.h>
 */

#include <linux/irqdomain_defs.h>
#include <linux/cpumask.h>
#include <linux/msi_api.h>
#include <linux/xarray.h>
#include <linux/mutex.h>
#include <linux/list.h>
#include <linux/irq.h>
#include <linux/bits.h>

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

#ifndef arch_is_isolated_msi
#define arch_is_isolated_msi() false
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
struct msi_desc;
struct pci_dev;
struct platform_msi_priv_data;
struct device_attribute;
struct irq_domain;
struct irq_affinity_desc;

void __get_cached_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
#ifdef CONFIG_GENERIC_MSI_IRQ
void get_cached_msi_msg(unsigned int irq, struct msi_msg *msg);
#else
static inline void get_cached_msi_msg(unsigned int irq, struct msi_msg *msg) { }
#endif

typedef void (*irq_write_msi_msg_t)(struct msi_desc *desc,
				    struct msi_msg *msg);

/**
 * pci_msi_desc - PCI/MSI specific MSI descriptor data
 *
 * @msi_mask:	[PCI MSI]   MSI cached mask bits
 * @msix_ctrl:	[PCI MSI-X] MSI-X cached per vector control bits
 * @is_msix:	[PCI MSI/X] True if MSI-X
 * @multiple:	[PCI MSI/X] log2 num of messages allocated
 * @multi_cap:	[PCI MSI/X] log2 num of messages supported
 * @can_mask:	[PCI MSI/X] Masking supported?
 * @is_64:	[PCI MSI/X] Address size: 0=32bit 1=64bit
 * @default_irq:[PCI MSI/X] The default pre-assigned non-MSI irq
 * @mask_pos:	[PCI MSI]   Mask register position
 * @mask_base:	[PCI MSI-X] Mask register base address
 */
struct pci_msi_desc {
	union {
		u32 msi_mask;
		u32 msix_ctrl;
	};
	struct {
		u8	is_msix		: 1;
		u8	multiple	: 3;
		u8	multi_cap	: 3;
		u8	can_mask	: 1;
		u8	is_64		: 1;
		u8	is_virtual	: 1;
		unsigned default_irq;
	} msi_attrib;
	union {
		u8	mask_pos;
		void __iomem *mask_base;
	};
};

/**
 * union msi_domain_cookie - Opaque MSI domain specific data
 * @value:	u64 value store
 * @ptr:	Pointer to domain specific data
 * @iobase:	Domain specific IOmem pointer
 *
 * The content of this data is implementation defined and used by the MSI
 * domain to store domain specific information which is requried for
 * interrupt chip callbacks.
 */
union msi_domain_cookie {
	u64	value;
	void	*ptr;
	void	__iomem *iobase;
};

/**
 * struct msi_desc_data - Generic MSI descriptor data
 * @dcookie:	Cookie for MSI domain specific data which is required
 *		for irq_chip callbacks
 * @icookie:	Cookie for the MSI interrupt instance provided by
 *		the usage site to the allocation function
 *
 * The content of this data is implementation defined, e.g. PCI/IMS
 * implementations define the meaning of the data. The MSI core ignores
 * this data completely.
 */
struct msi_desc_data {
	union msi_domain_cookie		dcookie;
	union msi_instance_cookie	icookie;
};

#define MSI_MAX_INDEX		((unsigned int)USHRT_MAX)

/**
 * struct msi_desc - Descriptor structure for MSI based interrupts
 * @irq:	The base interrupt number
 * @nvec_used:	The number of vectors used
 * @dev:	Pointer to the device which uses this descriptor
 * @msg:	The last set MSI message cached for reuse
 * @affinity:	Optional pointer to a cpu affinity mask for this descriptor
 * @sysfs_attr:	Pointer to sysfs device attribute
 *
 * @write_msi_msg:	Callback that may be called when the MSI message
 *			address or data changes
 * @write_msi_msg_data:	Data parameter for the callback.
 *
 * @msi_index:	Index of the msi descriptor
 * @pci:	PCI specific msi descriptor data
 * @data:	Generic MSI descriptor data
 */
struct msi_desc {
	/* Shared device/bus type independent data */
	unsigned int			irq;
	unsigned int			nvec_used;
	struct device			*dev;
	struct msi_msg			msg;
	struct irq_affinity_desc	*affinity;
#ifdef CONFIG_IRQ_MSI_IOMMU
	const void			*iommu_cookie;
#endif
#ifdef CONFIG_SYSFS
	struct device_attribute		*sysfs_attrs;
#endif

	void (*write_msi_msg)(struct msi_desc *entry, void *data);
	void *write_msi_msg_data;

	u16				msi_index;
	union {
		struct pci_msi_desc	pci;
		struct msi_desc_data	data;
	};
};

/*
 * Filter values for the MSI descriptor iterators and accessor functions.
 */
enum msi_desc_filter {
	/* All descriptors */
	MSI_DESC_ALL,
	/* Descriptors which have no interrupt associated */
	MSI_DESC_NOTASSOCIATED,
	/* Descriptors which have an interrupt associated */
	MSI_DESC_ASSOCIATED,
};


/**
 * struct msi_dev_domain - The internals of MSI domain info per device
 * @store:		Xarray for storing MSI descriptor pointers
 * @irqdomain:		Pointer to a per device interrupt domain
 */
struct msi_dev_domain {
	struct xarray		store;
	struct irq_domain	*domain;
};

/**
 * msi_device_data - MSI per device data
 * @properties:		MSI properties which are interesting to drivers
 * @platform_data:	Platform-MSI specific data
 * @mutex:		Mutex protecting the MSI descriptor store
 * @__domains:		Internal data for per device MSI domains
 * @__iter_idx:		Index to search the next entry for iterators
 */
struct msi_device_data {
	unsigned long			properties;
	struct platform_msi_priv_data	*platform_data;
	struct mutex			mutex;
	struct msi_dev_domain		__domains[MSI_MAX_DEVICE_IRQDOMAINS];
	unsigned long			__iter_idx;
};

int msi_setup_device_data(struct device *dev);

void msi_lock_descs(struct device *dev);
void msi_unlock_descs(struct device *dev);

struct msi_desc *msi_domain_first_desc(struct device *dev, unsigned int domid,
				       enum msi_desc_filter filter);

/**
 * msi_first_desc - Get the first MSI descriptor of the default irqdomain
 * @dev:	Device to operate on
 * @filter:	Descriptor state filter
 *
 * Must be called with the MSI descriptor mutex held, i.e. msi_lock_descs()
 * must be invoked before the call.
 *
 * Return: Pointer to the first MSI descriptor matching the search
 *	   criteria, NULL if none found.
 */
static inline struct msi_desc *msi_first_desc(struct device *dev,
					      enum msi_desc_filter filter)
{
	return msi_domain_first_desc(dev, MSI_DEFAULT_DOMAIN, filter);
}

struct msi_desc *msi_next_desc(struct device *dev, unsigned int domid,
			       enum msi_desc_filter filter);

/**
 * msi_domain_for_each_desc - Iterate the MSI descriptors in a specific domain
 *
 * @desc:	struct msi_desc pointer used as iterator
 * @dev:	struct device pointer - device to iterate
 * @domid:	The id of the interrupt domain which should be walked.
 * @filter:	Filter for descriptor selection
 *
 * Notes:
 *  - The loop must be protected with a msi_lock_descs()/msi_unlock_descs()
 *    pair.
 *  - It is safe to remove a retrieved MSI descriptor in the loop.
 */
#define msi_domain_for_each_desc(desc, dev, domid, filter)			\
	for ((desc) = msi_domain_first_desc((dev), (domid), (filter)); (desc);	\
	     (desc) = msi_next_desc((dev), (domid), (filter)))

/**
 * msi_for_each_desc - Iterate the MSI descriptors in the default irqdomain
 *
 * @desc:	struct msi_desc pointer used as iterator
 * @dev:	struct device pointer - device to iterate
 * @filter:	Filter for descriptor selection
 *
 * Notes:
 *  - The loop must be protected with a msi_lock_descs()/msi_unlock_descs()
 *    pair.
 *  - It is safe to remove a retrieved MSI descriptor in the loop.
 */
#define msi_for_each_desc(desc, dev, filter)					\
	msi_domain_for_each_desc((desc), (dev), MSI_DEFAULT_DOMAIN, (filter))

#define msi_desc_to_dev(desc)		((desc)->dev)

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

int msi_domain_insert_msi_desc(struct device *dev, unsigned int domid,
			       struct msi_desc *init_desc);
/**
 * msi_insert_msi_desc - Allocate and initialize a MSI descriptor in the
 *			 default irqdomain and insert it at @init_desc->msi_index
 * @dev:	Pointer to the device for which the descriptor is allocated
 * @init_desc:	Pointer to an MSI descriptor to initialize the new descriptor
 *
 * Return: 0 on success or an appropriate failure code.
 */
static inline int msi_insert_msi_desc(struct device *dev, struct msi_desc *init_desc)
{
	return msi_domain_insert_msi_desc(dev, MSI_DEFAULT_DOMAIN, init_desc);
}

void msi_domain_free_msi_descs_range(struct device *dev, unsigned int domid,
				     unsigned int first, unsigned int last);

/**
 * msi_free_msi_descs_range - Free a range of MSI descriptors of a device
 *			      in the default irqdomain
 *
 * @dev:	Device for which to free the descriptors
 * @first:	Index to start freeing from (inclusive)
 * @last:	Last index to be freed (inclusive)
 */
static inline void msi_free_msi_descs_range(struct device *dev, unsigned int first,
					    unsigned int last)
{
	msi_domain_free_msi_descs_range(dev, MSI_DEFAULT_DOMAIN, first, last);
}

/**
 * msi_free_msi_descs - Free all MSI descriptors of a device in the default irqdomain
 * @dev:	Device to free the descriptors
 */
static inline void msi_free_msi_descs(struct device *dev)
{
	msi_free_msi_descs_range(dev, 0, MSI_MAX_INDEX);
}

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
#ifdef CONFIG_SYSFS
int msi_device_populate_sysfs(struct device *dev);
void msi_device_destroy_sysfs(struct device *dev);
#else /* CONFIG_SYSFS */
static inline int msi_device_populate_sysfs(struct device *dev) { return 0; }
static inline void msi_device_destroy_sysfs(struct device *dev) { }
#endif /* !CONFIG_SYSFS */
#endif /* CONFIG_PCI_MSI_ARCH_FALLBACKS */

/*
 * The restore hook is still available even for fully irq domain based
 * setups. Courtesy to XEN/X86.
 */
bool arch_restore_msi_irqs(struct pci_dev *dev);

#ifdef CONFIG_GENERIC_MSI_IRQ

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
 * @msi_prepare:	Prepare the allocation of the interrupts in the domain
 * @prepare_desc:	Optional function to prepare the allocated MSI descriptor
 *			in the domain
 * @set_desc:		Set the msi descriptor for an interrupt
 * @domain_alloc_irqs:	Optional function to override the default allocation
 *			function.
 * @domain_free_irqs:	Optional function to override the default free
 *			function.
 * @msi_post_free:	Optional function which is invoked after freeing
 *			all interrupts.
 *
 * @get_hwirq, @msi_init and @msi_free are callbacks used by the underlying
 * irqdomain.
 *
 * @msi_check, @msi_prepare, @prepare_desc and @set_desc are callbacks used by the
 * msi_domain_alloc/free_irqs*() variants.
 *
 * @domain_alloc_irqs, @domain_free_irqs can be used to override the
 * default allocation/free functions (__msi_domain_alloc/free_irqs). This
 * is initially for a wrapper around XENs seperate MSI universe which can't
 * be wrapped into the regular irq domains concepts by mere mortals.  This
 * allows to universally use msi_domain_alloc/free_irqs without having to
 * special case XEN all over the place.
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
	int		(*msi_prepare)(struct irq_domain *domain,
				       struct device *dev, int nvec,
				       msi_alloc_info_t *arg);
	void		(*prepare_desc)(struct irq_domain *domain, msi_alloc_info_t *arg,
					struct msi_desc *desc);
	void		(*set_desc)(msi_alloc_info_t *arg,
				    struct msi_desc *desc);
	int		(*domain_alloc_irqs)(struct irq_domain *domain,
					     struct device *dev, int nvec);
	void		(*domain_free_irqs)(struct irq_domain *domain,
					    struct device *dev);
	void		(*msi_post_free)(struct irq_domain *domain,
					 struct device *dev);
};

/**
 * struct msi_domain_info - MSI interrupt domain data
 * @flags:		Flags to decribe features and capabilities
 * @bus_token:		The domain bus token
 * @hwsize:		The hardware table size or the software index limit.
 *			If 0 then the size is considered unlimited and
 *			gets initialized to the maximum software index limit
 *			by the domain creation code.
 * @ops:		The callback data structure
 * @chip:		Optional: associated interrupt chip
 * @chip_data:		Optional: associated interrupt chip data
 * @handler:		Optional: associated interrupt flow handler
 * @handler_data:	Optional: associated interrupt flow handler data
 * @handler_name:	Optional: associated interrupt flow handler name
 * @data:		Optional: domain specific data
 */
struct msi_domain_info {
	u32				flags;
	enum irq_domain_bus_token	bus_token;
	unsigned int			hwsize;
	struct msi_domain_ops		*ops;
	struct irq_chip			*chip;
	void				*chip_data;
	irq_flow_handler_t		handler;
	void				*handler_data;
	const char			*handler_name;
	void				*data;
};

/**
 * struct msi_domain_template - Template for MSI device domains
 * @name:	Storage for the resulting name. Filled in by the core.
 * @chip:	Interrupt chip for this domain
 * @ops:	MSI domain ops
 * @info:	MSI domain info data
 */
struct msi_domain_template {
	char			name[48];
	struct irq_chip		chip;
	struct msi_domain_ops	ops;
	struct msi_domain_info	info;
};

/*
 * Flags for msi_domain_info
 *
 * Bit 0-15:	Generic MSI functionality which is not subject to restriction
 *		by parent domains
 *
 * Bit 16-31:	Functionality which depends on the underlying parent domain and
 *		can be masked out by msi_parent_ops::init_dev_msi_info() when
 *		a device MSI domain is initialized.
 */
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
	/* Needs early activate, required for PCI */
	MSI_FLAG_ACTIVATE_EARLY		= (1 << 2),
	/*
	 * Must reactivate when irq is started even when
	 * MSI_FLAG_ACTIVATE_EARLY has been set.
	 */
	MSI_FLAG_MUST_REACTIVATE	= (1 << 3),
	/* Populate sysfs on alloc() and destroy it on free() */
	MSI_FLAG_DEV_SYSFS		= (1 << 4),
	/* Allocate simple MSI descriptors */
	MSI_FLAG_ALLOC_SIMPLE_MSI_DESCS	= (1 << 5),
	/* Free MSI descriptors */
	MSI_FLAG_FREE_MSI_DESCS		= (1 << 6),
	/*
	 * Quirk to handle MSI implementations which do not provide
	 * masking. Currently known to affect x86, but has to be partially
	 * handled in the core MSI code.
	 */
	MSI_FLAG_NOMASK_QUIRK		= (1 << 7),

	/* Mask for the generic functionality */
	MSI_GENERIC_FLAGS_MASK		= GENMASK(15, 0),

	/* Mask for the domain specific functionality */
	MSI_DOMAIN_FLAGS_MASK		= GENMASK(31, 16),

	/* Support multiple PCI MSI interrupts */
	MSI_FLAG_MULTI_PCI_MSI		= (1 << 16),
	/* Support PCI MSIX interrupts */
	MSI_FLAG_PCI_MSIX		= (1 << 17),
	/* Is level-triggered capable, using two messages */
	MSI_FLAG_LEVEL_CAPABLE		= (1 << 18),
	/* MSI-X entries must be contiguous */
	MSI_FLAG_MSIX_CONTIGUOUS	= (1 << 19),
	/* PCI/MSI-X vectors can be dynamically allocated/freed post MSI-X enable */
	MSI_FLAG_PCI_MSIX_ALLOC_DYN	= (1 << 20),
	/* Support for PCI/IMS */
	MSI_FLAG_PCI_IMS		= (1 << 21),
};

/**
 * struct msi_parent_ops - MSI parent domain callbacks and configuration info
 *
 * @supported_flags:	Required: The supported MSI flags of the parent domain
 * @prefix:		Optional: Prefix for the domain and chip name
 * @init_dev_msi_info:	Required: Callback for MSI parent domains to setup parent
 *			domain specific domain flags, domain ops and interrupt chip
 *			callbacks when a per device domain is created.
 */
struct msi_parent_ops {
	u32		supported_flags;
	const char	*prefix;
	bool		(*init_dev_msi_info)(struct device *dev, struct irq_domain *domain,
					     struct irq_domain *msi_parent_domain,
					     struct msi_domain_info *msi_child_info);
};

bool msi_parent_init_dev_msi_info(struct device *dev, struct irq_domain *domain,
				  struct irq_domain *msi_parent_domain,
				  struct msi_domain_info *msi_child_info);

int msi_domain_set_affinity(struct irq_data *data, const struct cpumask *mask,
			    bool force);

struct irq_domain *msi_create_irq_domain(struct fwnode_handle *fwnode,
					 struct msi_domain_info *info,
					 struct irq_domain *parent);

bool msi_create_device_irq_domain(struct device *dev, unsigned int domid,
				  const struct msi_domain_template *template,
				  unsigned int hwsize, void *domain_data,
				  void *chip_data);
void msi_remove_device_irq_domain(struct device *dev, unsigned int domid);

bool msi_match_device_irq_domain(struct device *dev, unsigned int domid,
				 enum irq_domain_bus_token bus_token);

int msi_domain_alloc_irqs_range_locked(struct device *dev, unsigned int domid,
				       unsigned int first, unsigned int last);
int msi_domain_alloc_irqs_range(struct device *dev, unsigned int domid,
				unsigned int first, unsigned int last);
int msi_domain_alloc_irqs_all_locked(struct device *dev, unsigned int domid, int nirqs);

struct msi_map msi_domain_alloc_irq_at(struct device *dev, unsigned int domid, unsigned int index,
				       const struct irq_affinity_desc *affdesc,
				       union msi_instance_cookie *cookie);

void msi_domain_free_irqs_range_locked(struct device *dev, unsigned int domid,
				       unsigned int first, unsigned int last);
void msi_domain_free_irqs_range(struct device *dev, unsigned int domid,
				unsigned int first, unsigned int last);
void msi_domain_free_irqs_all_locked(struct device *dev, unsigned int domid);
void msi_domain_free_irqs_all(struct device *dev, unsigned int domid);

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
void msi_domain_depopulate_descs(struct device *dev, int virq, int nvec);

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

int platform_msi_device_domain_alloc(struct irq_domain *domain, unsigned int virq,
				     unsigned int nr_irqs);
void platform_msi_device_domain_free(struct irq_domain *domain, unsigned int virq,
				     unsigned int nvec);
void *platform_msi_get_host_data(struct irq_domain *domain);

bool msi_device_has_isolated_msi(struct device *dev);
#else /* CONFIG_GENERIC_MSI_IRQ */
static inline bool msi_device_has_isolated_msi(struct device *dev)
{
	/*
	 * Arguably if the platform does not enable MSI support then it has
	 * "isolated MSI", as an interrupt controller that cannot receive MSIs
	 * is inherently isolated by our definition. The default definition for
	 * arch_is_isolated_msi() is conservative and returns false anyhow.
	 */
	return arch_is_isolated_msi();
}
#endif /* CONFIG_GENERIC_MSI_IRQ */

/* PCI specific interfaces */
#ifdef CONFIG_PCI_MSI
struct pci_dev *msi_desc_to_pci_dev(struct msi_desc *desc);
void pci_write_msi_msg(unsigned int irq, struct msi_msg *msg);
void __pci_read_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
void __pci_write_msi_msg(struct msi_desc *entry, struct msi_msg *msg);
void pci_msi_mask_irq(struct irq_data *data);
void pci_msi_unmask_irq(struct irq_data *data);
struct irq_domain *pci_msi_create_irq_domain(struct fwnode_handle *fwnode,
					     struct msi_domain_info *info,
					     struct irq_domain *parent);
u32 pci_msi_domain_get_msi_rid(struct irq_domain *domain, struct pci_dev *pdev);
struct irq_domain *pci_msi_get_device_domain(struct pci_dev *pdev);
#else /* CONFIG_PCI_MSI */
static inline struct irq_domain *pci_msi_get_device_domain(struct pci_dev *pdev)
{
	return NULL;
}
static inline void pci_write_msi_msg(unsigned int irq, struct msi_msg *msg) { }
#endif /* !CONFIG_PCI_MSI */

#endif /* LINUX_MSI_H */
