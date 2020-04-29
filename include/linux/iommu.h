/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2007-2008 Advanced Micro Devices, Inc.
 * Author: Joerg Roedel <joerg.roedel@amd.com>
 */

#ifndef __LINUX_IOMMU_H
#define __LINUX_IOMMU_H

#include <linux/scatterlist.h>
#include <linux/device.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/ioasid.h>
#include <uapi/linux/iommu.h>

#define IOMMU_READ	(1 << 0)
#define IOMMU_WRITE	(1 << 1)
#define IOMMU_CACHE	(1 << 2) /* DMA cache coherency */
#define IOMMU_NOEXEC	(1 << 3)
#define IOMMU_MMIO	(1 << 4) /* e.g. things like MSI doorbells */
/*
 * Where the bus hardware includes a privilege level as part of its access type
 * markings, and certain devices are capable of issuing transactions marked as
 * either 'supervisor' or 'user', the IOMMU_PRIV flag requests that the other
 * given permission flags only apply to accesses at the higher privilege level,
 * and that unprivileged transactions should have as little access as possible.
 * This would usually imply the same permissions as kernel mappings on the CPU,
 * if the IOMMU page table format is equivalent.
 */
#define IOMMU_PRIV	(1 << 5)
/*
 * Non-coherent masters can use this page protection flag to set cacheable
 * memory attributes for only a transparent outer level of cache, also known as
 * the last-level or system cache.
 */
#define IOMMU_SYS_CACHE_ONLY	(1 << 6)

struct iommu_ops;
struct iommu_group;
struct bus_type;
struct device;
struct iommu_domain;
struct notifier_block;
struct iommu_sva;
struct iommu_fault_event;

/* iommu fault flags */
#define IOMMU_FAULT_READ	0x0
#define IOMMU_FAULT_WRITE	0x1

typedef int (*iommu_fault_handler_t)(struct iommu_domain *,
			struct device *, unsigned long, int, void *);
typedef int (*iommu_mm_exit_handler_t)(struct device *dev, struct iommu_sva *,
				       void *);
typedef int (*iommu_dev_fault_handler_t)(struct iommu_fault *, void *);

struct iommu_domain_geometry {
	dma_addr_t aperture_start; /* First address that can be mapped    */
	dma_addr_t aperture_end;   /* Last address that can be mapped     */
	bool force_aperture;       /* DMA only allowed in mappable range? */
};

/* Domain feature flags */
#define __IOMMU_DOMAIN_PAGING	(1U << 0)  /* Support for iommu_map/unmap */
#define __IOMMU_DOMAIN_DMA_API	(1U << 1)  /* Domain for use in DMA-API
					      implementation              */
#define __IOMMU_DOMAIN_PT	(1U << 2)  /* Domain is identity mapped   */

/*
 * This are the possible domain-types
 *
 *	IOMMU_DOMAIN_BLOCKED	- All DMA is blocked, can be used to isolate
 *				  devices
 *	IOMMU_DOMAIN_IDENTITY	- DMA addresses are system physical addresses
 *	IOMMU_DOMAIN_UNMANAGED	- DMA mappings managed by IOMMU-API user, used
 *				  for VMs
 *	IOMMU_DOMAIN_DMA	- Internally used for DMA-API implementations.
 *				  This flag allows IOMMU drivers to implement
 *				  certain optimizations for these domains
 */
#define IOMMU_DOMAIN_BLOCKED	(0U)
#define IOMMU_DOMAIN_IDENTITY	(__IOMMU_DOMAIN_PT)
#define IOMMU_DOMAIN_UNMANAGED	(__IOMMU_DOMAIN_PAGING)
#define IOMMU_DOMAIN_DMA	(__IOMMU_DOMAIN_PAGING |	\
				 __IOMMU_DOMAIN_DMA_API)

struct iommu_domain {
	unsigned type;
	const struct iommu_ops *ops;
	unsigned long pgsize_bitmap;	/* Bitmap of page sizes in use */
	iommu_fault_handler_t handler;
	void *handler_token;
	struct iommu_domain_geometry geometry;
	void *iova_cookie;
};

enum iommu_cap {
	IOMMU_CAP_CACHE_COHERENCY,	/* IOMMU can enforce cache coherent DMA
					   transactions */
	IOMMU_CAP_INTR_REMAP,		/* IOMMU supports interrupt isolation */
	IOMMU_CAP_NOEXEC,		/* IOMMU_NOEXEC flag */
};

/*
 * Following constraints are specifc to FSL_PAMUV1:
 *  -aperture must be power of 2, and naturally aligned
 *  -number of windows must be power of 2, and address space size
 *   of each window is determined by aperture size / # of windows
 *  -the actual size of the mapped region of a window must be power
 *   of 2 starting with 4KB and physical address must be naturally
 *   aligned.
 * DOMAIN_ATTR_FSL_PAMUV1 corresponds to the above mentioned contraints.
 * The caller can invoke iommu_domain_get_attr to check if the underlying
 * iommu implementation supports these constraints.
 */

enum iommu_attr {
	DOMAIN_ATTR_GEOMETRY,
	DOMAIN_ATTR_PAGING,
	DOMAIN_ATTR_WINDOWS,
	DOMAIN_ATTR_FSL_PAMU_STASH,
	DOMAIN_ATTR_FSL_PAMU_ENABLE,
	DOMAIN_ATTR_FSL_PAMUV1,
	DOMAIN_ATTR_NESTING,	/* two stages of translation */
	DOMAIN_ATTR_DMA_USE_FLUSH_QUEUE,
	DOMAIN_ATTR_MAX,
};

/* These are the possible reserved region types */
enum iommu_resv_type {
	/* Memory regions which must be mapped 1:1 at all times */
	IOMMU_RESV_DIRECT,
	/*
	 * Memory regions which are advertised to be 1:1 but are
	 * commonly considered relaxable in some conditions,
	 * for instance in device assignment use case (USB, Graphics)
	 */
	IOMMU_RESV_DIRECT_RELAXABLE,
	/* Arbitrary "never map this or give it to a device" address ranges */
	IOMMU_RESV_RESERVED,
	/* Hardware MSI region (untranslated) */
	IOMMU_RESV_MSI,
	/* Software-managed MSI translation window */
	IOMMU_RESV_SW_MSI,
};

/**
 * struct iommu_resv_region - descriptor for a reserved memory region
 * @list: Linked list pointers
 * @start: System physical start address of the region
 * @length: Length of the region in bytes
 * @prot: IOMMU Protection flags (READ/WRITE/...)
 * @type: Type of the reserved region
 */
struct iommu_resv_region {
	struct list_head	list;
	phys_addr_t		start;
	size_t			length;
	int			prot;
	enum iommu_resv_type	type;
};

/* Per device IOMMU features */
enum iommu_dev_features {
	IOMMU_DEV_FEAT_AUX,	/* Aux-domain feature */
	IOMMU_DEV_FEAT_SVA,	/* Shared Virtual Addresses */
};

#define IOMMU_PASID_INVALID	(-1U)

/**
 * struct iommu_sva_ops - device driver callbacks for an SVA context
 *
 * @mm_exit: called when the mm is about to be torn down by exit_mmap. After
 *           @mm_exit returns, the device must not issue any more transaction
 *           with the PASID given as argument.
 *
 *           The @mm_exit handler is allowed to sleep. Be careful about the
 *           locks taken in @mm_exit, because they might lead to deadlocks if
 *           they are also held when dropping references to the mm. Consider the
 *           following call chain:
 *           mutex_lock(A); mmput(mm) -> exit_mm() -> @mm_exit() -> mutex_lock(A)
 *           Using mmput_async() prevents this scenario.
 *
 */
struct iommu_sva_ops {
	iommu_mm_exit_handler_t mm_exit;
};

#ifdef CONFIG_IOMMU_API

/**
 * struct iommu_iotlb_gather - Range information for a pending IOTLB flush
 *
 * @start: IOVA representing the start of the range to be flushed
 * @end: IOVA representing the end of the range to be flushed (exclusive)
 * @pgsize: The interval at which to perform the flush
 *
 * This structure is intended to be updated by multiple calls to the
 * ->unmap() function in struct iommu_ops before eventually being passed
 * into ->iotlb_sync().
 */
struct iommu_iotlb_gather {
	unsigned long		start;
	unsigned long		end;
	size_t			pgsize;
};

/**
 * struct iommu_ops - iommu ops and capabilities
 * @capable: check capability
 * @domain_alloc: allocate iommu domain
 * @domain_free: free iommu domain
 * @attach_dev: attach device to an iommu domain
 * @detach_dev: detach device from an iommu domain
 * @map: map a physically contiguous memory region to an iommu domain
 * @unmap: unmap a physically contiguous memory region from an iommu domain
 * @flush_iotlb_all: Synchronously flush all hardware TLBs for this domain
 * @iotlb_sync_map: Sync mappings created recently using @map to the hardware
 * @iotlb_sync: Flush all queued ranges from the hardware TLBs and empty flush
 *            queue
 * @iova_to_phys: translate iova to physical address
 * @add_device: add device to iommu grouping
 * @remove_device: remove device from iommu grouping
 * @device_group: find iommu group for a particular device
 * @domain_get_attr: Query domain attributes
 * @domain_set_attr: Change domain attributes
 * @get_resv_regions: Request list of reserved regions for a device
 * @put_resv_regions: Free list of reserved regions for a device
 * @apply_resv_region: Temporary helper call-back for iova reserved ranges
 * @domain_window_enable: Configure and enable a particular window for a domain
 * @domain_window_disable: Disable a particular window for a domain
 * @of_xlate: add OF master IDs to iommu grouping
 * @is_attach_deferred: Check if domain attach should be deferred from iommu
 *                      driver init to device driver init (default no)
 * @dev_has/enable/disable_feat: per device entries to check/enable/disable
 *                               iommu specific features.
 * @dev_feat_enabled: check enabled feature
 * @aux_attach/detach_dev: aux-domain specific attach/detach entries.
 * @aux_get_pasid: get the pasid given an aux-domain
 * @sva_bind: Bind process address space to device
 * @sva_unbind: Unbind process address space from device
 * @sva_get_pasid: Get PASID associated to a SVA handle
 * @page_response: handle page request response
 * @cache_invalidate: invalidate translation caches
 * @sva_bind_gpasid: bind guest pasid and mm
 * @sva_unbind_gpasid: unbind guest pasid and mm
 * @def_domain_type: device default domain type, return value:
 *		- IOMMU_DOMAIN_IDENTITY: must use an identity domain
 *		- IOMMU_DOMAIN_DMA: must use a dma domain
 *		- 0: use the default setting
 * @pgsize_bitmap: bitmap of all possible supported page sizes
 * @owner: Driver module providing these ops
 */
struct iommu_ops {
	bool (*capable)(enum iommu_cap);

	/* Domain allocation and freeing by the iommu driver */
	struct iommu_domain *(*domain_alloc)(unsigned iommu_domain_type);
	void (*domain_free)(struct iommu_domain *);

	int (*attach_dev)(struct iommu_domain *domain, struct device *dev);
	void (*detach_dev)(struct iommu_domain *domain, struct device *dev);
	int (*map)(struct iommu_domain *domain, unsigned long iova,
		   phys_addr_t paddr, size_t size, int prot, gfp_t gfp);
	size_t (*unmap)(struct iommu_domain *domain, unsigned long iova,
		     size_t size, struct iommu_iotlb_gather *iotlb_gather);
	void (*flush_iotlb_all)(struct iommu_domain *domain);
	void (*iotlb_sync_map)(struct iommu_domain *domain);
	void (*iotlb_sync)(struct iommu_domain *domain,
			   struct iommu_iotlb_gather *iotlb_gather);
	phys_addr_t (*iova_to_phys)(struct iommu_domain *domain, dma_addr_t iova);
	int (*add_device)(struct device *dev);
	void (*remove_device)(struct device *dev);
	struct iommu_group *(*device_group)(struct device *dev);
	int (*domain_get_attr)(struct iommu_domain *domain,
			       enum iommu_attr attr, void *data);
	int (*domain_set_attr)(struct iommu_domain *domain,
			       enum iommu_attr attr, void *data);

	/* Request/Free a list of reserved regions for a device */
	void (*get_resv_regions)(struct device *dev, struct list_head *list);
	void (*put_resv_regions)(struct device *dev, struct list_head *list);
	void (*apply_resv_region)(struct device *dev,
				  struct iommu_domain *domain,
				  struct iommu_resv_region *region);

	/* Window handling functions */
	int (*domain_window_enable)(struct iommu_domain *domain, u32 wnd_nr,
				    phys_addr_t paddr, u64 size, int prot);
	void (*domain_window_disable)(struct iommu_domain *domain, u32 wnd_nr);

	int (*of_xlate)(struct device *dev, struct of_phandle_args *args);
	bool (*is_attach_deferred)(struct iommu_domain *domain, struct device *dev);

	/* Per device IOMMU features */
	bool (*dev_has_feat)(struct device *dev, enum iommu_dev_features f);
	bool (*dev_feat_enabled)(struct device *dev, enum iommu_dev_features f);
	int (*dev_enable_feat)(struct device *dev, enum iommu_dev_features f);
	int (*dev_disable_feat)(struct device *dev, enum iommu_dev_features f);

	/* Aux-domain specific attach/detach entries */
	int (*aux_attach_dev)(struct iommu_domain *domain, struct device *dev);
	void (*aux_detach_dev)(struct iommu_domain *domain, struct device *dev);
	int (*aux_get_pasid)(struct iommu_domain *domain, struct device *dev);

	struct iommu_sva *(*sva_bind)(struct device *dev, struct mm_struct *mm,
				      void *drvdata);
	void (*sva_unbind)(struct iommu_sva *handle);
	int (*sva_get_pasid)(struct iommu_sva *handle);

	int (*page_response)(struct device *dev,
			     struct iommu_fault_event *evt,
			     struct iommu_page_response *msg);
	int (*cache_invalidate)(struct iommu_domain *domain, struct device *dev,
				struct iommu_cache_invalidate_info *inv_info);
	int (*sva_bind_gpasid)(struct iommu_domain *domain,
			struct device *dev, struct iommu_gpasid_bind_data *data);

	int (*sva_unbind_gpasid)(struct device *dev, int pasid);

	int (*def_domain_type)(struct device *dev);

	unsigned long pgsize_bitmap;
	struct module *owner;
};

/**
 * struct iommu_device - IOMMU core representation of one IOMMU hardware
 *			 instance
 * @list: Used by the iommu-core to keep a list of registered iommus
 * @ops: iommu-ops for talking to this iommu
 * @dev: struct device for sysfs handling
 */
struct iommu_device {
	struct list_head list;
	const struct iommu_ops *ops;
	struct fwnode_handle *fwnode;
	struct device *dev;
};

/**
 * struct iommu_fault_event - Generic fault event
 *
 * Can represent recoverable faults such as a page requests or
 * unrecoverable faults such as DMA or IRQ remapping faults.
 *
 * @fault: fault descriptor
 * @list: pending fault event list, used for tracking responses
 */
struct iommu_fault_event {
	struct iommu_fault fault;
	struct list_head list;
};

/**
 * struct iommu_fault_param - per-device IOMMU fault data
 * @handler: Callback function to handle IOMMU faults at device level
 * @data: handler private data
 * @faults: holds the pending faults which needs response
 * @lock: protect pending faults list
 */
struct iommu_fault_param {
	iommu_dev_fault_handler_t handler;
	void *data;
	struct list_head faults;
	struct mutex lock;
};

/**
 * struct dev_iommu - Collection of per-device IOMMU data
 *
 * @fault_param: IOMMU detected device fault reporting data
 * @fwspec:	 IOMMU fwspec data
 * @priv:	 IOMMU Driver private data
 *
 * TODO: migrate other per device data pointers under iommu_dev_data, e.g.
 *	struct iommu_group	*iommu_group;
 */
struct dev_iommu {
	struct mutex lock;
	struct iommu_fault_param	*fault_param;
	struct iommu_fwspec		*fwspec;
	void				*priv;
};

int  iommu_device_register(struct iommu_device *iommu);
void iommu_device_unregister(struct iommu_device *iommu);
int  iommu_device_sysfs_add(struct iommu_device *iommu,
			    struct device *parent,
			    const struct attribute_group **groups,
			    const char *fmt, ...) __printf(4, 5);
void iommu_device_sysfs_remove(struct iommu_device *iommu);
int  iommu_device_link(struct iommu_device   *iommu, struct device *link);
void iommu_device_unlink(struct iommu_device *iommu, struct device *link);

static inline void __iommu_device_set_ops(struct iommu_device *iommu,
					  const struct iommu_ops *ops)
{
	iommu->ops = ops;
}

#define iommu_device_set_ops(iommu, ops)				\
do {									\
	struct iommu_ops *__ops = (struct iommu_ops *)(ops);		\
	__ops->owner = THIS_MODULE;					\
	__iommu_device_set_ops(iommu, __ops);				\
} while (0)

static inline void iommu_device_set_fwnode(struct iommu_device *iommu,
					   struct fwnode_handle *fwnode)
{
	iommu->fwnode = fwnode;
}

static inline struct iommu_device *dev_to_iommu_device(struct device *dev)
{
	return (struct iommu_device *)dev_get_drvdata(dev);
}

static inline void iommu_iotlb_gather_init(struct iommu_iotlb_gather *gather)
{
	*gather = (struct iommu_iotlb_gather) {
		.start	= ULONG_MAX,
	};
}

#define IOMMU_GROUP_NOTIFY_ADD_DEVICE		1 /* Device added */
#define IOMMU_GROUP_NOTIFY_DEL_DEVICE		2 /* Pre Device removed */
#define IOMMU_GROUP_NOTIFY_BIND_DRIVER		3 /* Pre Driver bind */
#define IOMMU_GROUP_NOTIFY_BOUND_DRIVER		4 /* Post Driver bind */
#define IOMMU_GROUP_NOTIFY_UNBIND_DRIVER	5 /* Pre Driver unbind */
#define IOMMU_GROUP_NOTIFY_UNBOUND_DRIVER	6 /* Post Driver unbind */

extern int bus_set_iommu(struct bus_type *bus, const struct iommu_ops *ops);
extern bool iommu_present(struct bus_type *bus);
extern bool iommu_capable(struct bus_type *bus, enum iommu_cap cap);
extern struct iommu_domain *iommu_domain_alloc(struct bus_type *bus);
extern struct iommu_group *iommu_group_get_by_id(int id);
extern void iommu_domain_free(struct iommu_domain *domain);
extern int iommu_attach_device(struct iommu_domain *domain,
			       struct device *dev);
extern void iommu_detach_device(struct iommu_domain *domain,
				struct device *dev);
extern int iommu_cache_invalidate(struct iommu_domain *domain,
				  struct device *dev,
				  struct iommu_cache_invalidate_info *inv_info);
extern int iommu_sva_bind_gpasid(struct iommu_domain *domain,
		struct device *dev, struct iommu_gpasid_bind_data *data);
extern int iommu_sva_unbind_gpasid(struct iommu_domain *domain,
				struct device *dev, ioasid_t pasid);
extern struct iommu_domain *iommu_get_domain_for_dev(struct device *dev);
extern struct iommu_domain *iommu_get_dma_domain(struct device *dev);
extern int iommu_map(struct iommu_domain *domain, unsigned long iova,
		     phys_addr_t paddr, size_t size, int prot);
extern int iommu_map_atomic(struct iommu_domain *domain, unsigned long iova,
			    phys_addr_t paddr, size_t size, int prot);
extern size_t iommu_unmap(struct iommu_domain *domain, unsigned long iova,
			  size_t size);
extern size_t iommu_unmap_fast(struct iommu_domain *domain,
			       unsigned long iova, size_t size,
			       struct iommu_iotlb_gather *iotlb_gather);
extern size_t iommu_map_sg(struct iommu_domain *domain, unsigned long iova,
			   struct scatterlist *sg,unsigned int nents, int prot);
extern size_t iommu_map_sg_atomic(struct iommu_domain *domain,
				  unsigned long iova, struct scatterlist *sg,
				  unsigned int nents, int prot);
extern phys_addr_t iommu_iova_to_phys(struct iommu_domain *domain, dma_addr_t iova);
extern void iommu_set_fault_handler(struct iommu_domain *domain,
			iommu_fault_handler_t handler, void *token);

extern void iommu_get_resv_regions(struct device *dev, struct list_head *list);
extern void iommu_put_resv_regions(struct device *dev, struct list_head *list);
extern void generic_iommu_put_resv_regions(struct device *dev,
					   struct list_head *list);
extern int iommu_request_dm_for_dev(struct device *dev);
extern int iommu_request_dma_domain_for_dev(struct device *dev);
extern void iommu_set_default_passthrough(bool cmd_line);
extern void iommu_set_default_translated(bool cmd_line);
extern bool iommu_default_passthrough(void);
extern struct iommu_resv_region *
iommu_alloc_resv_region(phys_addr_t start, size_t length, int prot,
			enum iommu_resv_type type);
extern int iommu_get_group_resv_regions(struct iommu_group *group,
					struct list_head *head);

extern int iommu_attach_group(struct iommu_domain *domain,
			      struct iommu_group *group);
extern void iommu_detach_group(struct iommu_domain *domain,
			       struct iommu_group *group);
extern struct iommu_group *iommu_group_alloc(void);
extern void *iommu_group_get_iommudata(struct iommu_group *group);
extern void iommu_group_set_iommudata(struct iommu_group *group,
				      void *iommu_data,
				      void (*release)(void *iommu_data));
extern int iommu_group_set_name(struct iommu_group *group, const char *name);
extern int iommu_group_add_device(struct iommu_group *group,
				  struct device *dev);
extern void iommu_group_remove_device(struct device *dev);
extern int iommu_group_for_each_dev(struct iommu_group *group, void *data,
				    int (*fn)(struct device *, void *));
extern struct iommu_group *iommu_group_get(struct device *dev);
extern struct iommu_group *iommu_group_ref_get(struct iommu_group *group);
extern void iommu_group_put(struct iommu_group *group);
extern int iommu_group_register_notifier(struct iommu_group *group,
					 struct notifier_block *nb);
extern int iommu_group_unregister_notifier(struct iommu_group *group,
					   struct notifier_block *nb);
extern int iommu_register_device_fault_handler(struct device *dev,
					iommu_dev_fault_handler_t handler,
					void *data);

extern int iommu_unregister_device_fault_handler(struct device *dev);

extern int iommu_report_device_fault(struct device *dev,
				     struct iommu_fault_event *evt);
extern int iommu_page_response(struct device *dev,
			       struct iommu_page_response *msg);

extern int iommu_group_id(struct iommu_group *group);
extern struct iommu_group *iommu_group_get_for_dev(struct device *dev);
extern struct iommu_domain *iommu_group_default_domain(struct iommu_group *);

extern int iommu_domain_get_attr(struct iommu_domain *domain, enum iommu_attr,
				 void *data);
extern int iommu_domain_set_attr(struct iommu_domain *domain, enum iommu_attr,
				 void *data);

/* Window handling function prototypes */
extern int iommu_domain_window_enable(struct iommu_domain *domain, u32 wnd_nr,
				      phys_addr_t offset, u64 size,
				      int prot);
extern void iommu_domain_window_disable(struct iommu_domain *domain, u32 wnd_nr);

extern int report_iommu_fault(struct iommu_domain *domain, struct device *dev,
			      unsigned long iova, int flags);

static inline void iommu_flush_tlb_all(struct iommu_domain *domain)
{
	if (domain->ops->flush_iotlb_all)
		domain->ops->flush_iotlb_all(domain);
}

static inline void iommu_tlb_sync(struct iommu_domain *domain,
				  struct iommu_iotlb_gather *iotlb_gather)
{
	if (domain->ops->iotlb_sync)
		domain->ops->iotlb_sync(domain, iotlb_gather);

	iommu_iotlb_gather_init(iotlb_gather);
}

static inline void iommu_iotlb_gather_add_page(struct iommu_domain *domain,
					       struct iommu_iotlb_gather *gather,
					       unsigned long iova, size_t size)
{
	unsigned long start = iova, end = start + size;

	/*
	 * If the new page is disjoint from the current range or is mapped at
	 * a different granularity, then sync the TLB so that the gather
	 * structure can be rewritten.
	 */
	if (gather->pgsize != size ||
	    end < gather->start || start > gather->end) {
		if (gather->pgsize)
			iommu_tlb_sync(domain, gather);
		gather->pgsize = size;
	}

	if (gather->end < end)
		gather->end = end;

	if (gather->start > start)
		gather->start = start;
}

/* PCI device grouping function */
extern struct iommu_group *pci_device_group(struct device *dev);
/* Generic device grouping function */
extern struct iommu_group *generic_device_group(struct device *dev);
/* FSL-MC device grouping function */
struct iommu_group *fsl_mc_device_group(struct device *dev);

/**
 * struct iommu_fwspec - per-device IOMMU instance data
 * @ops: ops for this device's IOMMU
 * @iommu_fwnode: firmware handle for this device's IOMMU
 * @iommu_priv: IOMMU driver private data for this device
 * @num_pasid_bits: number of PASID bits supported by this device
 * @num_ids: number of associated device IDs
 * @ids: IDs which this device may present to the IOMMU
 */
struct iommu_fwspec {
	const struct iommu_ops	*ops;
	struct fwnode_handle	*iommu_fwnode;
	u32			flags;
	u32			num_pasid_bits;
	unsigned int		num_ids;
	u32			ids[];
};

/* ATS is supported */
#define IOMMU_FWSPEC_PCI_RC_ATS			(1 << 0)

/**
 * struct iommu_sva - handle to a device-mm bond
 */
struct iommu_sva {
	struct device			*dev;
	const struct iommu_sva_ops	*ops;
};

int iommu_fwspec_init(struct device *dev, struct fwnode_handle *iommu_fwnode,
		      const struct iommu_ops *ops);
void iommu_fwspec_free(struct device *dev);
int iommu_fwspec_add_ids(struct device *dev, u32 *ids, int num_ids);
const struct iommu_ops *iommu_ops_from_fwnode(struct fwnode_handle *fwnode);

static inline struct iommu_fwspec *dev_iommu_fwspec_get(struct device *dev)
{
	if (dev->iommu)
		return dev->iommu->fwspec;
	else
		return NULL;
}

static inline void dev_iommu_fwspec_set(struct device *dev,
					struct iommu_fwspec *fwspec)
{
	dev->iommu->fwspec = fwspec;
}

static inline void *dev_iommu_priv_get(struct device *dev)
{
	return dev->iommu->priv;
}

static inline void dev_iommu_priv_set(struct device *dev, void *priv)
{
	dev->iommu->priv = priv;
}

int iommu_probe_device(struct device *dev);
void iommu_release_device(struct device *dev);

bool iommu_dev_has_feature(struct device *dev, enum iommu_dev_features f);
int iommu_dev_enable_feature(struct device *dev, enum iommu_dev_features f);
int iommu_dev_disable_feature(struct device *dev, enum iommu_dev_features f);
bool iommu_dev_feature_enabled(struct device *dev, enum iommu_dev_features f);
int iommu_aux_attach_device(struct iommu_domain *domain, struct device *dev);
void iommu_aux_detach_device(struct iommu_domain *domain, struct device *dev);
int iommu_aux_get_pasid(struct iommu_domain *domain, struct device *dev);

struct iommu_sva *iommu_sva_bind_device(struct device *dev,
					struct mm_struct *mm,
					void *drvdata);
void iommu_sva_unbind_device(struct iommu_sva *handle);
int iommu_sva_set_ops(struct iommu_sva *handle,
		      const struct iommu_sva_ops *ops);
int iommu_sva_get_pasid(struct iommu_sva *handle);

#else /* CONFIG_IOMMU_API */

struct iommu_ops {};
struct iommu_group {};
struct iommu_fwspec {};
struct iommu_device {};
struct iommu_fault_param {};
struct iommu_iotlb_gather {};

static inline bool iommu_present(struct bus_type *bus)
{
	return false;
}

static inline bool iommu_capable(struct bus_type *bus, enum iommu_cap cap)
{
	return false;
}

static inline struct iommu_domain *iommu_domain_alloc(struct bus_type *bus)
{
	return NULL;
}

static inline struct iommu_group *iommu_group_get_by_id(int id)
{
	return NULL;
}

static inline void iommu_domain_free(struct iommu_domain *domain)
{
}

static inline int iommu_attach_device(struct iommu_domain *domain,
				      struct device *dev)
{
	return -ENODEV;
}

static inline void iommu_detach_device(struct iommu_domain *domain,
				       struct device *dev)
{
}

static inline struct iommu_domain *iommu_get_domain_for_dev(struct device *dev)
{
	return NULL;
}

static inline int iommu_map(struct iommu_domain *domain, unsigned long iova,
			    phys_addr_t paddr, size_t size, int prot)
{
	return -ENODEV;
}

static inline int iommu_map_atomic(struct iommu_domain *domain,
				   unsigned long iova, phys_addr_t paddr,
				   size_t size, int prot)
{
	return -ENODEV;
}

static inline size_t iommu_unmap(struct iommu_domain *domain,
				 unsigned long iova, size_t size)
{
	return 0;
}

static inline size_t iommu_unmap_fast(struct iommu_domain *domain,
				      unsigned long iova, int gfp_order,
				      struct iommu_iotlb_gather *iotlb_gather)
{
	return 0;
}

static inline size_t iommu_map_sg(struct iommu_domain *domain,
				  unsigned long iova, struct scatterlist *sg,
				  unsigned int nents, int prot)
{
	return 0;
}

static inline size_t iommu_map_sg_atomic(struct iommu_domain *domain,
				  unsigned long iova, struct scatterlist *sg,
				  unsigned int nents, int prot)
{
	return 0;
}

static inline void iommu_flush_tlb_all(struct iommu_domain *domain)
{
}

static inline void iommu_tlb_sync(struct iommu_domain *domain,
				  struct iommu_iotlb_gather *iotlb_gather)
{
}

static inline int iommu_domain_window_enable(struct iommu_domain *domain,
					     u32 wnd_nr, phys_addr_t paddr,
					     u64 size, int prot)
{
	return -ENODEV;
}

static inline void iommu_domain_window_disable(struct iommu_domain *domain,
					       u32 wnd_nr)
{
}

static inline phys_addr_t iommu_iova_to_phys(struct iommu_domain *domain, dma_addr_t iova)
{
	return 0;
}

static inline void iommu_set_fault_handler(struct iommu_domain *domain,
				iommu_fault_handler_t handler, void *token)
{
}

static inline void iommu_get_resv_regions(struct device *dev,
					struct list_head *list)
{
}

static inline void iommu_put_resv_regions(struct device *dev,
					struct list_head *list)
{
}

static inline int iommu_get_group_resv_regions(struct iommu_group *group,
					       struct list_head *head)
{
	return -ENODEV;
}

static inline int iommu_request_dm_for_dev(struct device *dev)
{
	return -ENODEV;
}

static inline int iommu_request_dma_domain_for_dev(struct device *dev)
{
	return -ENODEV;
}

static inline void iommu_set_default_passthrough(bool cmd_line)
{
}

static inline void iommu_set_default_translated(bool cmd_line)
{
}

static inline bool iommu_default_passthrough(void)
{
	return true;
}

static inline int iommu_attach_group(struct iommu_domain *domain,
				     struct iommu_group *group)
{
	return -ENODEV;
}

static inline void iommu_detach_group(struct iommu_domain *domain,
				      struct iommu_group *group)
{
}

static inline struct iommu_group *iommu_group_alloc(void)
{
	return ERR_PTR(-ENODEV);
}

static inline void *iommu_group_get_iommudata(struct iommu_group *group)
{
	return NULL;
}

static inline void iommu_group_set_iommudata(struct iommu_group *group,
					     void *iommu_data,
					     void (*release)(void *iommu_data))
{
}

static inline int iommu_group_set_name(struct iommu_group *group,
				       const char *name)
{
	return -ENODEV;
}

static inline int iommu_group_add_device(struct iommu_group *group,
					 struct device *dev)
{
	return -ENODEV;
}

static inline void iommu_group_remove_device(struct device *dev)
{
}

static inline int iommu_group_for_each_dev(struct iommu_group *group,
					   void *data,
					   int (*fn)(struct device *, void *))
{
	return -ENODEV;
}

static inline struct iommu_group *iommu_group_get(struct device *dev)
{
	return NULL;
}

static inline void iommu_group_put(struct iommu_group *group)
{
}

static inline int iommu_group_register_notifier(struct iommu_group *group,
						struct notifier_block *nb)
{
	return -ENODEV;
}

static inline int iommu_group_unregister_notifier(struct iommu_group *group,
						  struct notifier_block *nb)
{
	return 0;
}

static inline
int iommu_register_device_fault_handler(struct device *dev,
					iommu_dev_fault_handler_t handler,
					void *data)
{
	return -ENODEV;
}

static inline int iommu_unregister_device_fault_handler(struct device *dev)
{
	return 0;
}

static inline
int iommu_report_device_fault(struct device *dev, struct iommu_fault_event *evt)
{
	return -ENODEV;
}

static inline int iommu_page_response(struct device *dev,
				      struct iommu_page_response *msg)
{
	return -ENODEV;
}

static inline int iommu_group_id(struct iommu_group *group)
{
	return -ENODEV;
}

static inline int iommu_domain_get_attr(struct iommu_domain *domain,
					enum iommu_attr attr, void *data)
{
	return -EINVAL;
}

static inline int iommu_domain_set_attr(struct iommu_domain *domain,
					enum iommu_attr attr, void *data)
{
	return -EINVAL;
}

static inline int  iommu_device_register(struct iommu_device *iommu)
{
	return -ENODEV;
}

static inline void iommu_device_set_ops(struct iommu_device *iommu,
					const struct iommu_ops *ops)
{
}

static inline void iommu_device_set_fwnode(struct iommu_device *iommu,
					   struct fwnode_handle *fwnode)
{
}

static inline struct iommu_device *dev_to_iommu_device(struct device *dev)
{
	return NULL;
}

static inline void iommu_iotlb_gather_init(struct iommu_iotlb_gather *gather)
{
}

static inline void iommu_iotlb_gather_add_page(struct iommu_domain *domain,
					       struct iommu_iotlb_gather *gather,
					       unsigned long iova, size_t size)
{
}

static inline void iommu_device_unregister(struct iommu_device *iommu)
{
}

static inline int  iommu_device_sysfs_add(struct iommu_device *iommu,
					  struct device *parent,
					  const struct attribute_group **groups,
					  const char *fmt, ...)
{
	return -ENODEV;
}

static inline void iommu_device_sysfs_remove(struct iommu_device *iommu)
{
}

static inline int iommu_device_link(struct device *dev, struct device *link)
{
	return -EINVAL;
}

static inline void iommu_device_unlink(struct device *dev, struct device *link)
{
}

static inline int iommu_fwspec_init(struct device *dev,
				    struct fwnode_handle *iommu_fwnode,
				    const struct iommu_ops *ops)
{
	return -ENODEV;
}

static inline void iommu_fwspec_free(struct device *dev)
{
}

static inline int iommu_fwspec_add_ids(struct device *dev, u32 *ids,
				       int num_ids)
{
	return -ENODEV;
}

static inline
const struct iommu_ops *iommu_ops_from_fwnode(struct fwnode_handle *fwnode)
{
	return NULL;
}

static inline bool
iommu_dev_has_feature(struct device *dev, enum iommu_dev_features feat)
{
	return false;
}

static inline bool
iommu_dev_feature_enabled(struct device *dev, enum iommu_dev_features feat)
{
	return false;
}

static inline int
iommu_dev_enable_feature(struct device *dev, enum iommu_dev_features feat)
{
	return -ENODEV;
}

static inline int
iommu_dev_disable_feature(struct device *dev, enum iommu_dev_features feat)
{
	return -ENODEV;
}

static inline int
iommu_aux_attach_device(struct iommu_domain *domain, struct device *dev)
{
	return -ENODEV;
}

static inline void
iommu_aux_detach_device(struct iommu_domain *domain, struct device *dev)
{
}

static inline int
iommu_aux_get_pasid(struct iommu_domain *domain, struct device *dev)
{
	return -ENODEV;
}

static inline struct iommu_sva *
iommu_sva_bind_device(struct device *dev, struct mm_struct *mm, void *drvdata)
{
	return NULL;
}

static inline void iommu_sva_unbind_device(struct iommu_sva *handle)
{
}

static inline int iommu_sva_set_ops(struct iommu_sva *handle,
				    const struct iommu_sva_ops *ops)
{
	return -EINVAL;
}

static inline int iommu_sva_get_pasid(struct iommu_sva *handle)
{
	return IOMMU_PASID_INVALID;
}

static inline int
iommu_cache_invalidate(struct iommu_domain *domain,
		       struct device *dev,
		       struct iommu_cache_invalidate_info *inv_info)
{
	return -ENODEV;
}
static inline int iommu_sva_bind_gpasid(struct iommu_domain *domain,
				struct device *dev, struct iommu_gpasid_bind_data *data)
{
	return -ENODEV;
}

static inline int iommu_sva_unbind_gpasid(struct iommu_domain *domain,
					   struct device *dev, int pasid)
{
	return -ENODEV;
}

static inline struct iommu_fwspec *dev_iommu_fwspec_get(struct device *dev)
{
	return NULL;
}
#endif /* CONFIG_IOMMU_API */

#ifdef CONFIG_IOMMU_DEBUGFS
extern	struct dentry *iommu_debugfs_dir;
void iommu_debugfs_setup(void);
#else
static inline void iommu_debugfs_setup(void) {}
#endif

#endif /* __LINUX_IOMMU_H */
