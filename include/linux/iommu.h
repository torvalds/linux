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
#include <linux/iova_bitmap.h>

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

struct iommu_ops;
struct iommu_group;
struct bus_type;
struct device;
struct iommu_domain;
struct iommu_domain_ops;
struct iommu_dirty_ops;
struct notifier_block;
struct iommu_sva;
struct iommu_dma_cookie;
struct iommu_dma_msi_cookie;
struct iommu_fault_param;
struct iommufd_ctx;
struct iommufd_viommu;
struct msi_desc;
struct msi_msg;

#define IOMMU_FAULT_PERM_READ	(1 << 0) /* read */
#define IOMMU_FAULT_PERM_WRITE	(1 << 1) /* write */
#define IOMMU_FAULT_PERM_EXEC	(1 << 2) /* exec */
#define IOMMU_FAULT_PERM_PRIV	(1 << 3) /* privileged */

/* Generic fault types, can be expanded IRQ remapping fault */
enum iommu_fault_type {
	IOMMU_FAULT_PAGE_REQ = 1,	/* page request fault */
};

/**
 * struct iommu_fault_page_request - Page Request data
 * @flags: encodes whether the corresponding fields are valid and whether this
 *         is the last page in group (IOMMU_FAULT_PAGE_REQUEST_* values).
 *         When IOMMU_FAULT_PAGE_RESPONSE_NEEDS_PASID is set, the page response
 *         must have the same PASID value as the page request. When it is clear,
 *         the page response should not have a PASID.
 * @pasid: Process Address Space ID
 * @grpid: Page Request Group Index
 * @perm: requested page permissions (IOMMU_FAULT_PERM_* values)
 * @addr: page address
 * @private_data: device-specific private information
 */
struct iommu_fault_page_request {
#define IOMMU_FAULT_PAGE_REQUEST_PASID_VALID	(1 << 0)
#define IOMMU_FAULT_PAGE_REQUEST_LAST_PAGE	(1 << 1)
#define IOMMU_FAULT_PAGE_RESPONSE_NEEDS_PASID	(1 << 2)
	u32	flags;
	u32	pasid;
	u32	grpid;
	u32	perm;
	u64	addr;
	u64	private_data[2];
};

/**
 * struct iommu_fault - Generic fault data
 * @type: fault type from &enum iommu_fault_type
 * @prm: Page Request message, when @type is %IOMMU_FAULT_PAGE_REQ
 */
struct iommu_fault {
	u32 type;
	struct iommu_fault_page_request prm;
};

/**
 * enum iommu_page_response_code - Return status of fault handlers
 * @IOMMU_PAGE_RESP_SUCCESS: Fault has been handled and the page tables
 *	populated, retry the access. This is "Success" in PCI PRI.
 * @IOMMU_PAGE_RESP_FAILURE: General error. Drop all subsequent faults from
 *	this device if possible. This is "Response Failure" in PCI PRI.
 * @IOMMU_PAGE_RESP_INVALID: Could not handle this fault, don't retry the
 *	access. This is "Invalid Request" in PCI PRI.
 */
enum iommu_page_response_code {
	IOMMU_PAGE_RESP_SUCCESS = 0,
	IOMMU_PAGE_RESP_INVALID,
	IOMMU_PAGE_RESP_FAILURE,
};

/**
 * struct iommu_page_response - Generic page response information
 * @pasid: Process Address Space ID
 * @grpid: Page Request Group Index
 * @code: response code from &enum iommu_page_response_code
 */
struct iommu_page_response {
	u32	pasid;
	u32	grpid;
	u32	code;
};

struct iopf_fault {
	struct iommu_fault fault;
	/* node for pending lists */
	struct list_head list;
};

struct iopf_group {
	struct iopf_fault last_fault;
	struct list_head faults;
	size_t fault_count;
	/* list node for iommu_fault_param::faults */
	struct list_head pending_node;
	struct work_struct work;
	struct iommu_attach_handle *attach_handle;
	/* The device's fault data parameter. */
	struct iommu_fault_param *fault_param;
	/* Used by handler provider to hook the group on its own lists. */
	struct list_head node;
	u32 cookie;
};

/**
 * struct iopf_queue - IO Page Fault queue
 * @wq: the fault workqueue
 * @devices: devices attached to this queue
 * @lock: protects the device list
 */
struct iopf_queue {
	struct workqueue_struct *wq;
	struct list_head devices;
	struct mutex lock;
};

/* iommu fault flags */
#define IOMMU_FAULT_READ	0x0
#define IOMMU_FAULT_WRITE	0x1

typedef int (*iommu_fault_handler_t)(struct iommu_domain *,
			struct device *, unsigned long, int, void *);

struct iommu_domain_geometry {
	dma_addr_t aperture_start; /* First address that can be mapped    */
	dma_addr_t aperture_end;   /* Last address that can be mapped     */
	bool force_aperture;       /* DMA only allowed in mappable range? */
};

enum iommu_domain_cookie_type {
	IOMMU_COOKIE_NONE,
	IOMMU_COOKIE_DMA_IOVA,
	IOMMU_COOKIE_DMA_MSI,
	IOMMU_COOKIE_FAULT_HANDLER,
	IOMMU_COOKIE_SVA,
	IOMMU_COOKIE_IOMMUFD,
};

/* Domain feature flags */
#define __IOMMU_DOMAIN_PAGING	(1U << 0)  /* Support for iommu_map/unmap */
#define __IOMMU_DOMAIN_DMA_API	(1U << 1)  /* Domain for use in DMA-API
					      implementation              */
#define __IOMMU_DOMAIN_PT	(1U << 2)  /* Domain is identity mapped   */
#define __IOMMU_DOMAIN_DMA_FQ	(1U << 3)  /* DMA-API uses flush queue    */

#define __IOMMU_DOMAIN_SVA	(1U << 4)  /* Shared process address space */
#define __IOMMU_DOMAIN_PLATFORM	(1U << 5)

#define __IOMMU_DOMAIN_NESTED	(1U << 6)  /* User-managed address space nested
					      on a stage-2 translation        */

#define IOMMU_DOMAIN_ALLOC_FLAGS ~__IOMMU_DOMAIN_DMA_FQ
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
 *	IOMMU_DOMAIN_DMA_FQ	- As above, but definitely using batched TLB
 *				  invalidation.
 *	IOMMU_DOMAIN_SVA	- DMA addresses are shared process addresses
 *				  represented by mm_struct's.
 *	IOMMU_DOMAIN_PLATFORM	- Legacy domain for drivers that do their own
 *				  dma_api stuff. Do not use in new drivers.
 */
#define IOMMU_DOMAIN_BLOCKED	(0U)
#define IOMMU_DOMAIN_IDENTITY	(__IOMMU_DOMAIN_PT)
#define IOMMU_DOMAIN_UNMANAGED	(__IOMMU_DOMAIN_PAGING)
#define IOMMU_DOMAIN_DMA	(__IOMMU_DOMAIN_PAGING |	\
				 __IOMMU_DOMAIN_DMA_API)
#define IOMMU_DOMAIN_DMA_FQ	(__IOMMU_DOMAIN_PAGING |	\
				 __IOMMU_DOMAIN_DMA_API |	\
				 __IOMMU_DOMAIN_DMA_FQ)
#define IOMMU_DOMAIN_SVA	(__IOMMU_DOMAIN_SVA)
#define IOMMU_DOMAIN_PLATFORM	(__IOMMU_DOMAIN_PLATFORM)
#define IOMMU_DOMAIN_NESTED	(__IOMMU_DOMAIN_NESTED)

struct iommu_domain {
	unsigned type;
	enum iommu_domain_cookie_type cookie_type;
	const struct iommu_domain_ops *ops;
	const struct iommu_dirty_ops *dirty_ops;
	const struct iommu_ops *owner; /* Whose domain_alloc we came from */
	unsigned long pgsize_bitmap;	/* Bitmap of page sizes in use */
	struct iommu_domain_geometry geometry;
	int (*iopf_handler)(struct iopf_group *group);

	union { /* cookie */
		struct iommu_dma_cookie *iova_cookie;
		struct iommu_dma_msi_cookie *msi_cookie;
		struct iommufd_hw_pagetable *iommufd_hwpt;
		struct {
			iommu_fault_handler_t handler;
			void *handler_token;
		};
		struct {	/* IOMMU_DOMAIN_SVA */
			struct mm_struct *mm;
			int users;
			/*
			 * Next iommu_domain in mm->iommu_mm->sva-domains list
			 * protected by iommu_sva_lock.
			 */
			struct list_head next;
		};
	};
};

static inline bool iommu_is_dma_domain(struct iommu_domain *domain)
{
	return domain->type & __IOMMU_DOMAIN_DMA_API;
}

enum iommu_cap {
	IOMMU_CAP_CACHE_COHERENCY,	/* IOMMU_CACHE is supported */
	IOMMU_CAP_NOEXEC,		/* IOMMU_NOEXEC flag */
	IOMMU_CAP_PRE_BOOT_PROTECTION,	/* Firmware says it used the IOMMU for
					   DMA protection and we should too */
	/*
	 * Per-device flag indicating if enforce_cache_coherency() will work on
	 * this device.
	 */
	IOMMU_CAP_ENFORCE_CACHE_COHERENCY,
	/*
	 * IOMMU driver does not issue TLB maintenance during .unmap, so can
	 * usefully support the non-strict DMA flush queue.
	 */
	IOMMU_CAP_DEFERRED_FLUSH,
	IOMMU_CAP_DIRTY_TRACKING,	/* IOMMU supports dirty tracking */
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
 * @free: Callback to free associated memory allocations
 */
struct iommu_resv_region {
	struct list_head	list;
	phys_addr_t		start;
	size_t			length;
	int			prot;
	enum iommu_resv_type	type;
	void (*free)(struct device *dev, struct iommu_resv_region *region);
};

struct iommu_iort_rmr_data {
	struct iommu_resv_region rr;

	/* Stream IDs associated with IORT RMR entry */
	const u32 *sids;
	u32 num_sids;
};

/**
 * enum iommu_dev_features - Per device IOMMU features
 * @IOMMU_DEV_FEAT_SVA: Shared Virtual Addresses
 * @IOMMU_DEV_FEAT_IOPF: I/O Page Faults such as PRI or Stall. Generally
 *			 enabling %IOMMU_DEV_FEAT_SVA requires
 *			 %IOMMU_DEV_FEAT_IOPF, but some devices manage I/O Page
 *			 Faults themselves instead of relying on the IOMMU. When
 *			 supported, this feature must be enabled before and
 *			 disabled after %IOMMU_DEV_FEAT_SVA.
 *
 * Device drivers enable a feature using iommu_dev_enable_feature().
 */
enum iommu_dev_features {
	IOMMU_DEV_FEAT_SVA,
	IOMMU_DEV_FEAT_IOPF,
};

#define IOMMU_NO_PASID	(0U) /* Reserved for DMA w/o PASID */
#define IOMMU_FIRST_GLOBAL_PASID	(1U) /*starting range for allocation */
#define IOMMU_PASID_INVALID	(-1U)
typedef unsigned int ioasid_t;

/* Read but do not clear any dirty bits */
#define IOMMU_DIRTY_NO_CLEAR (1 << 0)

#ifdef CONFIG_IOMMU_API

/**
 * struct iommu_iotlb_gather - Range information for a pending IOTLB flush
 *
 * @start: IOVA representing the start of the range to be flushed
 * @end: IOVA representing the end of the range to be flushed (inclusive)
 * @pgsize: The interval at which to perform the flush
 * @freelist: Removed pages to free after sync
 * @queued: Indicates that the flush will be queued
 *
 * This structure is intended to be updated by multiple calls to the
 * ->unmap() function in struct iommu_ops before eventually being passed
 * into ->iotlb_sync(). Drivers can add pages to @freelist to be freed after
 * ->iotlb_sync() or ->iotlb_flush_all() have cleared all cached references to
 * them. @queued is set to indicate when ->iotlb_flush_all() will be called
 * later instead of ->iotlb_sync(), so drivers may optimise accordingly.
 */
struct iommu_iotlb_gather {
	unsigned long		start;
	unsigned long		end;
	size_t			pgsize;
	struct list_head	freelist;
	bool			queued;
};

/**
 * struct iommu_dirty_bitmap - Dirty IOVA bitmap state
 * @bitmap: IOVA bitmap
 * @gather: Range information for a pending IOTLB flush
 */
struct iommu_dirty_bitmap {
	struct iova_bitmap *bitmap;
	struct iommu_iotlb_gather *gather;
};

/**
 * struct iommu_dirty_ops - domain specific dirty tracking operations
 * @set_dirty_tracking: Enable or Disable dirty tracking on the iommu domain
 * @read_and_clear_dirty: Walk IOMMU page tables for dirtied PTEs marshalled
 *                        into a bitmap, with a bit represented as a page.
 *                        Reads the dirty PTE bits and clears it from IO
 *                        pagetables.
 */
struct iommu_dirty_ops {
	int (*set_dirty_tracking)(struct iommu_domain *domain, bool enabled);
	int (*read_and_clear_dirty)(struct iommu_domain *domain,
				    unsigned long iova, size_t size,
				    unsigned long flags,
				    struct iommu_dirty_bitmap *dirty);
};

/**
 * struct iommu_user_data - iommu driver specific user space data info
 * @type: The data type of the user buffer
 * @uptr: Pointer to the user buffer for copy_from_user()
 * @len: The length of the user buffer in bytes
 *
 * A user space data is an uAPI that is defined in include/uapi/linux/iommufd.h
 * @type, @uptr and @len should be just copied from an iommufd core uAPI struct.
 */
struct iommu_user_data {
	unsigned int type;
	void __user *uptr;
	size_t len;
};

/**
 * struct iommu_user_data_array - iommu driver specific user space data array
 * @type: The data type of all the entries in the user buffer array
 * @uptr: Pointer to the user buffer array
 * @entry_len: The fixed-width length of an entry in the array, in bytes
 * @entry_num: The number of total entries in the array
 *
 * The user buffer includes an array of requests with format defined in
 * include/uapi/linux/iommufd.h
 */
struct iommu_user_data_array {
	unsigned int type;
	void __user *uptr;
	size_t entry_len;
	u32 entry_num;
};

/**
 * __iommu_copy_struct_from_user - Copy iommu driver specific user space data
 * @dst_data: Pointer to an iommu driver specific user data that is defined in
 *            include/uapi/linux/iommufd.h
 * @src_data: Pointer to a struct iommu_user_data for user space data info
 * @data_type: The data type of the @dst_data. Must match with @src_data.type
 * @data_len: Length of current user data structure, i.e. sizeof(struct _dst)
 * @min_len: Initial length of user data structure for backward compatibility.
 *           This should be offsetofend using the last member in the user data
 *           struct that was initially added to include/uapi/linux/iommufd.h
 */
static inline int __iommu_copy_struct_from_user(
	void *dst_data, const struct iommu_user_data *src_data,
	unsigned int data_type, size_t data_len, size_t min_len)
{
	if (src_data->type != data_type)
		return -EINVAL;
	if (WARN_ON(!dst_data || !src_data))
		return -EINVAL;
	if (src_data->len < min_len || data_len < src_data->len)
		return -EINVAL;
	return copy_struct_from_user(dst_data, data_len, src_data->uptr,
				     src_data->len);
}

/**
 * iommu_copy_struct_from_user - Copy iommu driver specific user space data
 * @kdst: Pointer to an iommu driver specific user data that is defined in
 *        include/uapi/linux/iommufd.h
 * @user_data: Pointer to a struct iommu_user_data for user space data info
 * @data_type: The data type of the @kdst. Must match with @user_data->type
 * @min_last: The last memember of the data structure @kdst points in the
 *            initial version.
 * Return 0 for success, otherwise -error.
 */
#define iommu_copy_struct_from_user(kdst, user_data, data_type, min_last) \
	__iommu_copy_struct_from_user(kdst, user_data, data_type,         \
				      sizeof(*kdst),                      \
				      offsetofend(typeof(*kdst), min_last))

/**
 * __iommu_copy_struct_from_user_array - Copy iommu driver specific user space
 *                                       data from an iommu_user_data_array
 * @dst_data: Pointer to an iommu driver specific user data that is defined in
 *            include/uapi/linux/iommufd.h
 * @src_array: Pointer to a struct iommu_user_data_array for a user space array
 * @data_type: The data type of the @dst_data. Must match with @src_array.type
 * @index: Index to the location in the array to copy user data from
 * @data_len: Length of current user data structure, i.e. sizeof(struct _dst)
 * @min_len: Initial length of user data structure for backward compatibility.
 *           This should be offsetofend using the last member in the user data
 *           struct that was initially added to include/uapi/linux/iommufd.h
 */
static inline int __iommu_copy_struct_from_user_array(
	void *dst_data, const struct iommu_user_data_array *src_array,
	unsigned int data_type, unsigned int index, size_t data_len,
	size_t min_len)
{
	struct iommu_user_data src_data;

	if (WARN_ON(!src_array || index >= src_array->entry_num))
		return -EINVAL;
	if (!src_array->entry_num)
		return -EINVAL;
	src_data.uptr = src_array->uptr + src_array->entry_len * index;
	src_data.len = src_array->entry_len;
	src_data.type = src_array->type;

	return __iommu_copy_struct_from_user(dst_data, &src_data, data_type,
					     data_len, min_len);
}

/**
 * iommu_copy_struct_from_user_array - Copy iommu driver specific user space
 *                                     data from an iommu_user_data_array
 * @kdst: Pointer to an iommu driver specific user data that is defined in
 *        include/uapi/linux/iommufd.h
 * @user_array: Pointer to a struct iommu_user_data_array for a user space
 *              array
 * @data_type: The data type of the @kdst. Must match with @user_array->type
 * @index: Index to the location in the array to copy user data from
 * @min_last: The last member of the data structure @kdst points in the
 *            initial version.
 *
 * Copy a single entry from a user array. Return 0 for success, otherwise
 * -error.
 */
#define iommu_copy_struct_from_user_array(kdst, user_array, data_type, index, \
					  min_last)                           \
	__iommu_copy_struct_from_user_array(                                  \
		kdst, user_array, data_type, index, sizeof(*(kdst)),          \
		offsetofend(typeof(*(kdst)), min_last))

/**
 * iommu_copy_struct_from_full_user_array - Copy iommu driver specific user
 *         space data from an iommu_user_data_array
 * @kdst: Pointer to an iommu driver specific user data that is defined in
 *        include/uapi/linux/iommufd.h
 * @kdst_entry_size: sizeof(*kdst)
 * @user_array: Pointer to a struct iommu_user_data_array for a user space
 *              array
 * @data_type: The data type of the @kdst. Must match with @user_array->type
 *
 * Copy the entire user array. kdst must have room for kdst_entry_size *
 * user_array->entry_num bytes. Return 0 for success, otherwise -error.
 */
static inline int
iommu_copy_struct_from_full_user_array(void *kdst, size_t kdst_entry_size,
				       struct iommu_user_data_array *user_array,
				       unsigned int data_type)
{
	unsigned int i;
	int ret;

	if (user_array->type != data_type)
		return -EINVAL;
	if (!user_array->entry_num)
		return -EINVAL;
	if (likely(user_array->entry_len == kdst_entry_size)) {
		if (copy_from_user(kdst, user_array->uptr,
				   user_array->entry_num *
					   user_array->entry_len))
			return -EFAULT;
	}

	/* Copy item by item */
	for (i = 0; i != user_array->entry_num; i++) {
		ret = copy_struct_from_user(
			kdst + kdst_entry_size * i, kdst_entry_size,
			user_array->uptr + user_array->entry_len * i,
			user_array->entry_len);
		if (ret)
			return ret;
	}
	return 0;
}

/**
 * struct iommu_ops - iommu ops and capabilities
 * @capable: check capability
 * @hw_info: report iommu hardware information. The data buffer returned by this
 *           op is allocated in the iommu driver and freed by the caller after
 *           use. The information type is one of enum iommu_hw_info_type defined
 *           in include/uapi/linux/iommufd.h.
 * @domain_alloc: allocate and return an iommu domain if success. Otherwise
 *                NULL is returned. The domain is not fully initialized until
 *                the caller iommu_domain_alloc() returns.
 * @domain_alloc_paging_flags: Allocate an iommu domain corresponding to the
 *                     input parameters as defined in
 *                     include/uapi/linux/iommufd.h. The @user_data can be
 *                     optionally provided, the new domain must support
 *                     __IOMMU_DOMAIN_PAGING. Upon failure, ERR_PTR must be
 *                     returned.
 * @domain_alloc_paging: Allocate an iommu_domain that can be used for
 *                       UNMANAGED, DMA, and DMA_FQ domain types. This is the
 *                       same as invoking domain_alloc_paging_flags() with
 *                       @flags=0, @user_data=NULL. A driver should implement
 *                       only one of the two ops.
 * @domain_alloc_sva: Allocate an iommu_domain for Shared Virtual Addressing.
 * @domain_alloc_nested: Allocate an iommu_domain for nested translation.
 * @probe_device: Add device to iommu driver handling
 * @release_device: Remove device from iommu driver handling
 * @probe_finalize: Do final setup work after the device is added to an IOMMU
 *                  group and attached to the groups domain
 * @device_group: find iommu group for a particular device
 * @get_resv_regions: Request list of reserved regions for a device
 * @of_xlate: add OF master IDs to iommu grouping
 * @is_attach_deferred: Check if domain attach should be deferred from iommu
 *                      driver init to device driver init (default no)
 * @dev_enable/disable_feat: per device entries to enable/disable
 *                               iommu specific features.
 * @page_response: handle page request response
 * @def_domain_type: device default domain type, return value:
 *		- IOMMU_DOMAIN_IDENTITY: must use an identity domain
 *		- IOMMU_DOMAIN_DMA: must use a dma domain
 *		- 0: use the default setting
 * @default_domain_ops: the default ops for domains
 * @viommu_alloc: Allocate an iommufd_viommu on a physical IOMMU instance behind
 *                the @dev, as the set of virtualization resources shared/passed
 *                to user space IOMMU instance. And associate it with a nesting
 *                @parent_domain. The @viommu_type must be defined in the header
 *                include/uapi/linux/iommufd.h
 *                It is required to call iommufd_viommu_alloc() helper for
 *                a bundled allocation of the core and the driver structures,
 *                using the given @ictx pointer.
 * @pgsize_bitmap: bitmap of all possible supported page sizes
 * @owner: Driver module providing these ops
 * @identity_domain: An always available, always attachable identity
 *                   translation.
 * @blocked_domain: An always available, always attachable blocking
 *                  translation.
 * @default_domain: If not NULL this will always be set as the default domain.
 *                  This should be an IDENTITY/BLOCKED/PLATFORM domain.
 *                  Do not use in new drivers.
 * @user_pasid_table: IOMMU driver supports user-managed PASID table. There is
 *                    no user domain for each PASID and the I/O page faults are
 *                    forwarded through the user domain attached to the device
 *                    RID.
 */
struct iommu_ops {
	bool (*capable)(struct device *dev, enum iommu_cap);
	void *(*hw_info)(struct device *dev, u32 *length, u32 *type);

	/* Domain allocation and freeing by the iommu driver */
	struct iommu_domain *(*domain_alloc)(unsigned iommu_domain_type);
	struct iommu_domain *(*domain_alloc_paging_flags)(
		struct device *dev, u32 flags,
		const struct iommu_user_data *user_data);
	struct iommu_domain *(*domain_alloc_paging)(struct device *dev);
	struct iommu_domain *(*domain_alloc_sva)(struct device *dev,
						 struct mm_struct *mm);
	struct iommu_domain *(*domain_alloc_nested)(
		struct device *dev, struct iommu_domain *parent, u32 flags,
		const struct iommu_user_data *user_data);

	struct iommu_device *(*probe_device)(struct device *dev);
	void (*release_device)(struct device *dev);
	void (*probe_finalize)(struct device *dev);
	struct iommu_group *(*device_group)(struct device *dev);

	/* Request/Free a list of reserved regions for a device */
	void (*get_resv_regions)(struct device *dev, struct list_head *list);

	int (*of_xlate)(struct device *dev, const struct of_phandle_args *args);
	bool (*is_attach_deferred)(struct device *dev);

	/* Per device IOMMU features */
	int (*dev_enable_feat)(struct device *dev, enum iommu_dev_features f);
	int (*dev_disable_feat)(struct device *dev, enum iommu_dev_features f);

	void (*page_response)(struct device *dev, struct iopf_fault *evt,
			      struct iommu_page_response *msg);

	int (*def_domain_type)(struct device *dev);

	struct iommufd_viommu *(*viommu_alloc)(
		struct device *dev, struct iommu_domain *parent_domain,
		struct iommufd_ctx *ictx, unsigned int viommu_type);

	const struct iommu_domain_ops *default_domain_ops;
	unsigned long pgsize_bitmap;
	struct module *owner;
	struct iommu_domain *identity_domain;
	struct iommu_domain *blocked_domain;
	struct iommu_domain *release_domain;
	struct iommu_domain *default_domain;
	u8 user_pasid_table:1;
};

/**
 * struct iommu_domain_ops - domain specific operations
 * @attach_dev: attach an iommu domain to a device
 *  Return:
 * * 0		- success
 * * EINVAL	- can indicate that device and domain are incompatible due to
 *		  some previous configuration of the domain, in which case the
 *		  driver shouldn't log an error, since it is legitimate for a
 *		  caller to test reuse of existing domains. Otherwise, it may
 *		  still represent some other fundamental problem
 * * ENOMEM	- out of memory
 * * ENOSPC	- non-ENOMEM type of resource allocation failures
 * * EBUSY	- device is attached to a domain and cannot be changed
 * * ENODEV	- device specific errors, not able to be attached
 * * <others>	- treated as ENODEV by the caller. Use is discouraged
 * @set_dev_pasid: set or replace an iommu domain to a pasid of device. The pasid of
 *                 the device should be left in the old config in error case.
 * @map_pages: map a physically contiguous set of pages of the same size to
 *             an iommu domain.
 * @unmap_pages: unmap a number of pages of the same size from an iommu domain
 * @flush_iotlb_all: Synchronously flush all hardware TLBs for this domain
 * @iotlb_sync_map: Sync mappings created recently using @map to the hardware
 * @iotlb_sync: Flush all queued ranges from the hardware TLBs and empty flush
 *            queue
 * @cache_invalidate_user: Flush hardware cache for user space IO page table.
 *                         The @domain must be IOMMU_DOMAIN_NESTED. The @array
 *                         passes in the cache invalidation requests, in form
 *                         of a driver data structure. The driver must update
 *                         array->entry_num to report the number of handled
 *                         invalidation requests. The driver data structure
 *                         must be defined in include/uapi/linux/iommufd.h
 * @iova_to_phys: translate iova to physical address
 * @enforce_cache_coherency: Prevent any kind of DMA from bypassing IOMMU_CACHE,
 *                           including no-snoop TLPs on PCIe or other platform
 *                           specific mechanisms.
 * @set_pgtable_quirks: Set io page table quirks (IO_PGTABLE_QUIRK_*)
 * @free: Release the domain after use.
 */
struct iommu_domain_ops {
	int (*attach_dev)(struct iommu_domain *domain, struct device *dev);
	int (*set_dev_pasid)(struct iommu_domain *domain, struct device *dev,
			     ioasid_t pasid, struct iommu_domain *old);

	int (*map_pages)(struct iommu_domain *domain, unsigned long iova,
			 phys_addr_t paddr, size_t pgsize, size_t pgcount,
			 int prot, gfp_t gfp, size_t *mapped);
	size_t (*unmap_pages)(struct iommu_domain *domain, unsigned long iova,
			      size_t pgsize, size_t pgcount,
			      struct iommu_iotlb_gather *iotlb_gather);

	void (*flush_iotlb_all)(struct iommu_domain *domain);
	int (*iotlb_sync_map)(struct iommu_domain *domain, unsigned long iova,
			      size_t size);
	void (*iotlb_sync)(struct iommu_domain *domain,
			   struct iommu_iotlb_gather *iotlb_gather);
	int (*cache_invalidate_user)(struct iommu_domain *domain,
				     struct iommu_user_data_array *array);

	phys_addr_t (*iova_to_phys)(struct iommu_domain *domain,
				    dma_addr_t iova);

	bool (*enforce_cache_coherency)(struct iommu_domain *domain);
	int (*set_pgtable_quirks)(struct iommu_domain *domain,
				  unsigned long quirks);

	void (*free)(struct iommu_domain *domain);
};

/**
 * struct iommu_device - IOMMU core representation of one IOMMU hardware
 *			 instance
 * @list: Used by the iommu-core to keep a list of registered iommus
 * @ops: iommu-ops for talking to this iommu
 * @dev: struct device for sysfs handling
 * @singleton_group: Used internally for drivers that have only one group
 * @max_pasids: number of supported PASIDs
 */
struct iommu_device {
	struct list_head list;
	const struct iommu_ops *ops;
	struct fwnode_handle *fwnode;
	struct device *dev;
	struct iommu_group *singleton_group;
	u32 max_pasids;
};

/**
 * struct iommu_fault_param - per-device IOMMU fault data
 * @lock: protect pending faults list
 * @users: user counter to manage the lifetime of the data
 * @rcu: rcu head for kfree_rcu()
 * @dev: the device that owns this param
 * @queue: IOPF queue
 * @queue_list: index into queue->devices
 * @partial: faults that are part of a Page Request Group for which the last
 *           request hasn't been submitted yet.
 * @faults: holds the pending faults which need response
 */
struct iommu_fault_param {
	struct mutex lock;
	refcount_t users;
	struct rcu_head rcu;

	struct device *dev;
	struct iopf_queue *queue;
	struct list_head queue_list;

	struct list_head partial;
	struct list_head faults;
};

/**
 * struct dev_iommu - Collection of per-device IOMMU data
 *
 * @fault_param: IOMMU detected device fault reporting data
 * @fwspec:	 IOMMU fwspec data
 * @iommu_dev:	 IOMMU device this device is linked to
 * @priv:	 IOMMU Driver private data
 * @max_pasids:  number of PASIDs this device can consume
 * @attach_deferred: the dma domain attachment is deferred
 * @pci_32bit_workaround: Limit DMA allocations to 32-bit IOVAs
 * @require_direct: device requires IOMMU_RESV_DIRECT regions
 * @shadow_on_flush: IOTLB flushes are used to sync shadow tables
 *
 * TODO: migrate other per device data pointers under iommu_dev_data, e.g.
 *	struct iommu_group	*iommu_group;
 */
struct dev_iommu {
	struct mutex lock;
	struct iommu_fault_param __rcu	*fault_param;
	struct iommu_fwspec		*fwspec;
	struct iommu_device		*iommu_dev;
	void				*priv;
	u32				max_pasids;
	u32				attach_deferred:1;
	u32				pci_32bit_workaround:1;
	u32				require_direct:1;
	u32				shadow_on_flush:1;
};

int iommu_device_register(struct iommu_device *iommu,
			  const struct iommu_ops *ops,
			  struct device *hwdev);
void iommu_device_unregister(struct iommu_device *iommu);
int  iommu_device_sysfs_add(struct iommu_device *iommu,
			    struct device *parent,
			    const struct attribute_group **groups,
			    const char *fmt, ...) __printf(4, 5);
void iommu_device_sysfs_remove(struct iommu_device *iommu);
int  iommu_device_link(struct iommu_device   *iommu, struct device *link);
void iommu_device_unlink(struct iommu_device *iommu, struct device *link);
int iommu_deferred_attach(struct device *dev, struct iommu_domain *domain);

static inline struct iommu_device *dev_to_iommu_device(struct device *dev)
{
	return (struct iommu_device *)dev_get_drvdata(dev);
}

/**
 * iommu_get_iommu_dev - Get iommu_device for a device
 * @dev: an end-point device
 *
 * Note that this function must be called from the iommu_ops
 * to retrieve the iommu_device for a device, which the core code
 * guarentees it will not invoke the op without an attached iommu.
 */
static inline struct iommu_device *__iommu_get_iommu_dev(struct device *dev)
{
	return dev->iommu->iommu_dev;
}

#define iommu_get_iommu_dev(dev, type, member) \
	container_of(__iommu_get_iommu_dev(dev), type, member)

static inline void iommu_iotlb_gather_init(struct iommu_iotlb_gather *gather)
{
	*gather = (struct iommu_iotlb_gather) {
		.start	= ULONG_MAX,
		.freelist = LIST_HEAD_INIT(gather->freelist),
	};
}

extern bool device_iommu_capable(struct device *dev, enum iommu_cap cap);
extern bool iommu_group_has_isolated_msi(struct iommu_group *group);
struct iommu_domain *iommu_paging_domain_alloc_flags(struct device *dev, unsigned int flags);
static inline struct iommu_domain *iommu_paging_domain_alloc(struct device *dev)
{
	return iommu_paging_domain_alloc_flags(dev, 0);
}
extern void iommu_domain_free(struct iommu_domain *domain);
extern int iommu_attach_device(struct iommu_domain *domain,
			       struct device *dev);
extern void iommu_detach_device(struct iommu_domain *domain,
				struct device *dev);
extern struct iommu_domain *iommu_get_domain_for_dev(struct device *dev);
extern struct iommu_domain *iommu_get_dma_domain(struct device *dev);
extern int iommu_map(struct iommu_domain *domain, unsigned long iova,
		     phys_addr_t paddr, size_t size, int prot, gfp_t gfp);
extern size_t iommu_unmap(struct iommu_domain *domain, unsigned long iova,
			  size_t size);
extern size_t iommu_unmap_fast(struct iommu_domain *domain,
			       unsigned long iova, size_t size,
			       struct iommu_iotlb_gather *iotlb_gather);
extern ssize_t iommu_map_sg(struct iommu_domain *domain, unsigned long iova,
			    struct scatterlist *sg, unsigned int nents,
			    int prot, gfp_t gfp);
extern phys_addr_t iommu_iova_to_phys(struct iommu_domain *domain, dma_addr_t iova);
extern void iommu_set_fault_handler(struct iommu_domain *domain,
			iommu_fault_handler_t handler, void *token);

extern void iommu_get_resv_regions(struct device *dev, struct list_head *list);
extern void iommu_put_resv_regions(struct device *dev, struct list_head *list);
extern void iommu_set_default_passthrough(bool cmd_line);
extern void iommu_set_default_translated(bool cmd_line);
extern bool iommu_default_passthrough(void);
extern struct iommu_resv_region *
iommu_alloc_resv_region(phys_addr_t start, size_t length, int prot,
			enum iommu_resv_type type, gfp_t gfp);
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

extern int iommu_group_id(struct iommu_group *group);
extern struct iommu_domain *iommu_group_default_domain(struct iommu_group *);

int iommu_set_pgtable_quirks(struct iommu_domain *domain,
		unsigned long quirks);

void iommu_set_dma_strict(void);

extern int report_iommu_fault(struct iommu_domain *domain, struct device *dev,
			      unsigned long iova, int flags);

static inline void iommu_flush_iotlb_all(struct iommu_domain *domain)
{
	if (domain->ops->flush_iotlb_all)
		domain->ops->flush_iotlb_all(domain);
}

static inline void iommu_iotlb_sync(struct iommu_domain *domain,
				  struct iommu_iotlb_gather *iotlb_gather)
{
	if (domain->ops->iotlb_sync)
		domain->ops->iotlb_sync(domain, iotlb_gather);

	iommu_iotlb_gather_init(iotlb_gather);
}

/**
 * iommu_iotlb_gather_is_disjoint - Checks whether a new range is disjoint
 *
 * @gather: TLB gather data
 * @iova: start of page to invalidate
 * @size: size of page to invalidate
 *
 * Helper for IOMMU drivers to check whether a new range and the gathered range
 * are disjoint. For many IOMMUs, flushing the IOMMU in this case is better
 * than merging the two, which might lead to unnecessary invalidations.
 */
static inline
bool iommu_iotlb_gather_is_disjoint(struct iommu_iotlb_gather *gather,
				    unsigned long iova, size_t size)
{
	unsigned long start = iova, end = start + size - 1;

	return gather->end != 0 &&
		(end + 1 < gather->start || start > gather->end + 1);
}


/**
 * iommu_iotlb_gather_add_range - Gather for address-based TLB invalidation
 * @gather: TLB gather data
 * @iova: start of page to invalidate
 * @size: size of page to invalidate
 *
 * Helper for IOMMU drivers to build arbitrarily-sized invalidation commands
 * where only the address range matters, and simply minimising intermediate
 * syncs is preferred.
 */
static inline void iommu_iotlb_gather_add_range(struct iommu_iotlb_gather *gather,
						unsigned long iova, size_t size)
{
	unsigned long end = iova + size - 1;

	if (gather->start > iova)
		gather->start = iova;
	if (gather->end < end)
		gather->end = end;
}

/**
 * iommu_iotlb_gather_add_page - Gather for page-based TLB invalidation
 * @domain: IOMMU domain to be invalidated
 * @gather: TLB gather data
 * @iova: start of page to invalidate
 * @size: size of page to invalidate
 *
 * Helper for IOMMU drivers to build invalidation commands based on individual
 * pages, or with page size/table level hints which cannot be gathered if they
 * differ.
 */
static inline void iommu_iotlb_gather_add_page(struct iommu_domain *domain,
					       struct iommu_iotlb_gather *gather,
					       unsigned long iova, size_t size)
{
	/*
	 * If the new page is disjoint from the current range or is mapped at
	 * a different granularity, then sync the TLB so that the gather
	 * structure can be rewritten.
	 */
	if ((gather->pgsize && gather->pgsize != size) ||
	    iommu_iotlb_gather_is_disjoint(gather, iova, size))
		iommu_iotlb_sync(domain, gather);

	gather->pgsize = size;
	iommu_iotlb_gather_add_range(gather, iova, size);
}

static inline bool iommu_iotlb_gather_queued(struct iommu_iotlb_gather *gather)
{
	return gather && gather->queued;
}

static inline void iommu_dirty_bitmap_init(struct iommu_dirty_bitmap *dirty,
					   struct iova_bitmap *bitmap,
					   struct iommu_iotlb_gather *gather)
{
	if (gather)
		iommu_iotlb_gather_init(gather);

	dirty->bitmap = bitmap;
	dirty->gather = gather;
}

static inline void iommu_dirty_bitmap_record(struct iommu_dirty_bitmap *dirty,
					     unsigned long iova,
					     unsigned long length)
{
	if (dirty->bitmap)
		iova_bitmap_set(dirty->bitmap, iova, length);

	if (dirty->gather)
		iommu_iotlb_gather_add_range(dirty->gather, iova, length);
}

/* PCI device grouping function */
extern struct iommu_group *pci_device_group(struct device *dev);
/* Generic device grouping function */
extern struct iommu_group *generic_device_group(struct device *dev);
/* FSL-MC device grouping function */
struct iommu_group *fsl_mc_device_group(struct device *dev);
extern struct iommu_group *generic_single_device_group(struct device *dev);

/**
 * struct iommu_fwspec - per-device IOMMU instance data
 * @iommu_fwnode: firmware handle for this device's IOMMU
 * @flags: IOMMU_FWSPEC_* flags
 * @num_ids: number of associated device IDs
 * @ids: IDs which this device may present to the IOMMU
 *
 * Note that the IDs (and any other information, really) stored in this structure should be
 * considered private to the IOMMU device driver and are not to be used directly by IOMMU
 * consumers.
 */
struct iommu_fwspec {
	struct fwnode_handle	*iommu_fwnode;
	u32			flags;
	unsigned int		num_ids;
	u32			ids[];
};

/* ATS is supported */
#define IOMMU_FWSPEC_PCI_RC_ATS			(1 << 0)
/* CANWBS is supported */
#define IOMMU_FWSPEC_PCI_RC_CANWBS		(1 << 1)

/*
 * An iommu attach handle represents a relationship between an iommu domain
 * and a PASID or RID of a device. It is allocated and managed by the component
 * that manages the domain and is stored in the iommu group during the time the
 * domain is attached.
 */
struct iommu_attach_handle {
	struct iommu_domain		*domain;
};

/**
 * struct iommu_sva - handle to a device-mm bond
 */
struct iommu_sva {
	struct iommu_attach_handle	handle;
	struct device			*dev;
	refcount_t			users;
};

struct iommu_mm_data {
	u32			pasid;
	struct list_head	sva_domains;
};

int iommu_fwspec_init(struct device *dev, struct fwnode_handle *iommu_fwnode);
int iommu_fwspec_add_ids(struct device *dev, const u32 *ids, int num_ids);

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
	if (dev->iommu)
		return dev->iommu->priv;
	else
		return NULL;
}

void dev_iommu_priv_set(struct device *dev, void *priv);

extern struct mutex iommu_probe_device_lock;
int iommu_probe_device(struct device *dev);

int iommu_dev_enable_feature(struct device *dev, enum iommu_dev_features f);
int iommu_dev_disable_feature(struct device *dev, enum iommu_dev_features f);

int iommu_device_use_default_domain(struct device *dev);
void iommu_device_unuse_default_domain(struct device *dev);

int iommu_group_claim_dma_owner(struct iommu_group *group, void *owner);
void iommu_group_release_dma_owner(struct iommu_group *group);
bool iommu_group_dma_owner_claimed(struct iommu_group *group);

int iommu_device_claim_dma_owner(struct device *dev, void *owner);
void iommu_device_release_dma_owner(struct device *dev);

int iommu_attach_device_pasid(struct iommu_domain *domain,
			      struct device *dev, ioasid_t pasid,
			      struct iommu_attach_handle *handle);
void iommu_detach_device_pasid(struct iommu_domain *domain,
			       struct device *dev, ioasid_t pasid);
ioasid_t iommu_alloc_global_pasid(struct device *dev);
void iommu_free_global_pasid(ioasid_t pasid);
#else /* CONFIG_IOMMU_API */

struct iommu_ops {};
struct iommu_group {};
struct iommu_fwspec {};
struct iommu_device {};
struct iommu_fault_param {};
struct iommu_iotlb_gather {};
struct iommu_dirty_bitmap {};
struct iommu_dirty_ops {};

static inline bool device_iommu_capable(struct device *dev, enum iommu_cap cap)
{
	return false;
}

static inline struct iommu_domain *iommu_paging_domain_alloc_flags(struct device *dev,
						     unsigned int flags)
{
	return ERR_PTR(-ENODEV);
}

static inline struct iommu_domain *iommu_paging_domain_alloc(struct device *dev)
{
	return ERR_PTR(-ENODEV);
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
			    phys_addr_t paddr, size_t size, int prot, gfp_t gfp)
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

static inline ssize_t iommu_map_sg(struct iommu_domain *domain,
				   unsigned long iova, struct scatterlist *sg,
				   unsigned int nents, int prot, gfp_t gfp)
{
	return -ENODEV;
}

static inline void iommu_flush_iotlb_all(struct iommu_domain *domain)
{
}

static inline void iommu_iotlb_sync(struct iommu_domain *domain,
				  struct iommu_iotlb_gather *iotlb_gather)
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

static inline int iommu_group_id(struct iommu_group *group)
{
	return -ENODEV;
}

static inline int iommu_set_pgtable_quirks(struct iommu_domain *domain,
		unsigned long quirks)
{
	return 0;
}

static inline int iommu_device_register(struct iommu_device *iommu,
					const struct iommu_ops *ops,
					struct device *hwdev)
{
	return -ENODEV;
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

static inline bool iommu_iotlb_gather_queued(struct iommu_iotlb_gather *gather)
{
	return false;
}

static inline void iommu_dirty_bitmap_init(struct iommu_dirty_bitmap *dirty,
					   struct iova_bitmap *bitmap,
					   struct iommu_iotlb_gather *gather)
{
}

static inline void iommu_dirty_bitmap_record(struct iommu_dirty_bitmap *dirty,
					     unsigned long iova,
					     unsigned long length)
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
				    struct fwnode_handle *iommu_fwnode)
{
	return -ENODEV;
}

static inline int iommu_fwspec_add_ids(struct device *dev, u32 *ids,
				       int num_ids)
{
	return -ENODEV;
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

static inline struct iommu_fwspec *dev_iommu_fwspec_get(struct device *dev)
{
	return NULL;
}

static inline int iommu_device_use_default_domain(struct device *dev)
{
	return 0;
}

static inline void iommu_device_unuse_default_domain(struct device *dev)
{
}

static inline int
iommu_group_claim_dma_owner(struct iommu_group *group, void *owner)
{
	return -ENODEV;
}

static inline void iommu_group_release_dma_owner(struct iommu_group *group)
{
}

static inline bool iommu_group_dma_owner_claimed(struct iommu_group *group)
{
	return false;
}

static inline void iommu_device_release_dma_owner(struct device *dev)
{
}

static inline int iommu_device_claim_dma_owner(struct device *dev, void *owner)
{
	return -ENODEV;
}

static inline int iommu_attach_device_pasid(struct iommu_domain *domain,
					    struct device *dev, ioasid_t pasid,
					    struct iommu_attach_handle *handle)
{
	return -ENODEV;
}

static inline void iommu_detach_device_pasid(struct iommu_domain *domain,
					     struct device *dev, ioasid_t pasid)
{
}

static inline ioasid_t iommu_alloc_global_pasid(struct device *dev)
{
	return IOMMU_PASID_INVALID;
}

static inline void iommu_free_global_pasid(ioasid_t pasid) {}
#endif /* CONFIG_IOMMU_API */

#ifdef CONFIG_IRQ_MSI_IOMMU
#ifdef CONFIG_IOMMU_API
int iommu_dma_prepare_msi(struct msi_desc *desc, phys_addr_t msi_addr);
#else
static inline int iommu_dma_prepare_msi(struct msi_desc *desc,
					phys_addr_t msi_addr)
{
	return 0;
}
#endif /* CONFIG_IOMMU_API */
#endif /* CONFIG_IRQ_MSI_IOMMU */

#if IS_ENABLED(CONFIG_LOCKDEP) && IS_ENABLED(CONFIG_IOMMU_API)
void iommu_group_mutex_assert(struct device *dev);
#else
static inline void iommu_group_mutex_assert(struct device *dev)
{
}
#endif

/**
 * iommu_map_sgtable - Map the given buffer to the IOMMU domain
 * @domain:	The IOMMU domain to perform the mapping
 * @iova:	The start address to map the buffer
 * @sgt:	The sg_table object describing the buffer
 * @prot:	IOMMU protection bits
 *
 * Creates a mapping at @iova for the buffer described by a scatterlist
 * stored in the given sg_table object in the provided IOMMU domain.
 */
static inline ssize_t iommu_map_sgtable(struct iommu_domain *domain,
			unsigned long iova, struct sg_table *sgt, int prot)
{
	return iommu_map_sg(domain, iova, sgt->sgl, sgt->orig_nents, prot,
			    GFP_KERNEL);
}

#ifdef CONFIG_IOMMU_DEBUGFS
extern	struct dentry *iommu_debugfs_dir;
void iommu_debugfs_setup(void);
#else
static inline void iommu_debugfs_setup(void) {}
#endif

#ifdef CONFIG_IOMMU_DMA
int iommu_get_msi_cookie(struct iommu_domain *domain, dma_addr_t base);
#else /* CONFIG_IOMMU_DMA */
static inline int iommu_get_msi_cookie(struct iommu_domain *domain, dma_addr_t base)
{
	return -ENODEV;
}
#endif	/* CONFIG_IOMMU_DMA */

/*
 * Newer generations of Tegra SoCs require devices' stream IDs to be directly programmed into
 * some registers. These are always paired with a Tegra SMMU or ARM SMMU, for which the contents
 * of the struct iommu_fwspec are known. Use this helper to formalize access to these internals.
 */
#define TEGRA_STREAM_ID_BYPASS 0x7f

static inline bool tegra_dev_iommu_get_stream_id(struct device *dev, u32 *stream_id)
{
#ifdef CONFIG_IOMMU_API
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (fwspec && fwspec->num_ids == 1) {
		*stream_id = fwspec->ids[0] & 0xffff;
		return true;
	}
#endif

	return false;
}

#ifdef CONFIG_IOMMU_MM_DATA
static inline void mm_pasid_init(struct mm_struct *mm)
{
	/*
	 * During dup_mm(), a new mm will be memcpy'd from an old one and that makes
	 * the new mm and the old one point to a same iommu_mm instance. When either
	 * one of the two mms gets released, the iommu_mm instance is freed, leaving
	 * the other mm running into a use-after-free/double-free problem. To avoid
	 * the problem, zeroing the iommu_mm pointer of a new mm is needed here.
	 */
	mm->iommu_mm = NULL;
}

static inline bool mm_valid_pasid(struct mm_struct *mm)
{
	return READ_ONCE(mm->iommu_mm);
}

static inline u32 mm_get_enqcmd_pasid(struct mm_struct *mm)
{
	struct iommu_mm_data *iommu_mm = READ_ONCE(mm->iommu_mm);

	if (!iommu_mm)
		return IOMMU_PASID_INVALID;
	return iommu_mm->pasid;
}

void mm_pasid_drop(struct mm_struct *mm);
struct iommu_sva *iommu_sva_bind_device(struct device *dev,
					struct mm_struct *mm);
void iommu_sva_unbind_device(struct iommu_sva *handle);
u32 iommu_sva_get_pasid(struct iommu_sva *handle);
#else
static inline struct iommu_sva *
iommu_sva_bind_device(struct device *dev, struct mm_struct *mm)
{
	return ERR_PTR(-ENODEV);
}

static inline void iommu_sva_unbind_device(struct iommu_sva *handle)
{
}

static inline u32 iommu_sva_get_pasid(struct iommu_sva *handle)
{
	return IOMMU_PASID_INVALID;
}
static inline void mm_pasid_init(struct mm_struct *mm) {}
static inline bool mm_valid_pasid(struct mm_struct *mm) { return false; }

static inline u32 mm_get_enqcmd_pasid(struct mm_struct *mm)
{
	return IOMMU_PASID_INVALID;
}

static inline void mm_pasid_drop(struct mm_struct *mm) {}
#endif /* CONFIG_IOMMU_SVA */

#ifdef CONFIG_IOMMU_IOPF
int iopf_queue_add_device(struct iopf_queue *queue, struct device *dev);
void iopf_queue_remove_device(struct iopf_queue *queue, struct device *dev);
int iopf_queue_flush_dev(struct device *dev);
struct iopf_queue *iopf_queue_alloc(const char *name);
void iopf_queue_free(struct iopf_queue *queue);
int iopf_queue_discard_partial(struct iopf_queue *queue);
void iopf_free_group(struct iopf_group *group);
int iommu_report_device_fault(struct device *dev, struct iopf_fault *evt);
void iopf_group_response(struct iopf_group *group,
			 enum iommu_page_response_code status);
#else
static inline int
iopf_queue_add_device(struct iopf_queue *queue, struct device *dev)
{
	return -ENODEV;
}

static inline void
iopf_queue_remove_device(struct iopf_queue *queue, struct device *dev)
{
}

static inline int iopf_queue_flush_dev(struct device *dev)
{
	return -ENODEV;
}

static inline struct iopf_queue *iopf_queue_alloc(const char *name)
{
	return NULL;
}

static inline void iopf_queue_free(struct iopf_queue *queue)
{
}

static inline int iopf_queue_discard_partial(struct iopf_queue *queue)
{
	return -ENODEV;
}

static inline void iopf_free_group(struct iopf_group *group)
{
}

static inline int
iommu_report_device_fault(struct device *dev, struct iopf_fault *evt)
{
	return -ENODEV;
}

static inline void iopf_group_response(struct iopf_group *group,
				       enum iommu_page_response_code status)
{
}
#endif /* CONFIG_IOMMU_IOPF */
#endif /* __LINUX_IOMMU_H */
