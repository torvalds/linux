/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * IOMMU user API definitions
 */

#ifndef _UAPI_IOMMU_H
#define _UAPI_IOMMU_H

#include <linux/types.h>

#define IOMMU_FAULT_PERM_READ	(1 << 0) /* read */
#define IOMMU_FAULT_PERM_WRITE	(1 << 1) /* write */
#define IOMMU_FAULT_PERM_EXEC	(1 << 2) /* exec */
#define IOMMU_FAULT_PERM_PRIV	(1 << 3) /* privileged */

/* Generic fault types, can be expanded IRQ remapping fault */
enum iommu_fault_type {
	IOMMU_FAULT_DMA_UNRECOV = 1,	/* unrecoverable fault */
	IOMMU_FAULT_PAGE_REQ,		/* page request fault */
};

enum iommu_fault_reason {
	IOMMU_FAULT_REASON_UNKNOWN = 0,

	/* Could not access the PASID table (fetch caused external abort) */
	IOMMU_FAULT_REASON_PASID_FETCH,

	/* PASID entry is invalid or has configuration errors */
	IOMMU_FAULT_REASON_BAD_PASID_ENTRY,

	/*
	 * PASID is out of range (e.g. exceeds the maximum PASID
	 * supported by the IOMMU) or disabled.
	 */
	IOMMU_FAULT_REASON_PASID_INVALID,

	/*
	 * An external abort occurred fetching (or updating) a translation
	 * table descriptor
	 */
	IOMMU_FAULT_REASON_WALK_EABT,

	/*
	 * Could not access the page table entry (Bad address),
	 * actual translation fault
	 */
	IOMMU_FAULT_REASON_PTE_FETCH,

	/* Protection flag check failed */
	IOMMU_FAULT_REASON_PERMISSION,

	/* access flag check failed */
	IOMMU_FAULT_REASON_ACCESS,

	/* Output address of a translation stage caused Address Size fault */
	IOMMU_FAULT_REASON_OOR_ADDRESS,
};

/**
 * struct iommu_fault_unrecoverable - Unrecoverable fault data
 * @reason: reason of the fault, from &enum iommu_fault_reason
 * @flags: parameters of this fault (IOMMU_FAULT_UNRECOV_* values)
 * @pasid: Process Address Space ID
 * @perm: requested permission access using by the incoming transaction
 *        (IOMMU_FAULT_PERM_* values)
 * @addr: offending page address
 * @fetch_addr: address that caused a fetch abort, if any
 */
struct iommu_fault_unrecoverable {
	__u32	reason;
#define IOMMU_FAULT_UNRECOV_PASID_VALID		(1 << 0)
#define IOMMU_FAULT_UNRECOV_ADDR_VALID		(1 << 1)
#define IOMMU_FAULT_UNRECOV_FETCH_ADDR_VALID	(1 << 2)
	__u32	flags;
	__u32	pasid;
	__u32	perm;
	__u64	addr;
	__u64	fetch_addr;
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
#define IOMMU_FAULT_PAGE_REQUEST_PRIV_DATA	(1 << 2)
#define IOMMU_FAULT_PAGE_RESPONSE_NEEDS_PASID	(1 << 3)
	__u32	flags;
	__u32	pasid;
	__u32	grpid;
	__u32	perm;
	__u64	addr;
	__u64	private_data[2];
};

/**
 * struct iommu_fault - Generic fault data
 * @type: fault type from &enum iommu_fault_type
 * @padding: reserved for future use (should be zero)
 * @event: fault event, when @type is %IOMMU_FAULT_DMA_UNRECOV
 * @prm: Page Request message, when @type is %IOMMU_FAULT_PAGE_REQ
 * @padding2: sets the fault size to allow for future extensions
 */
struct iommu_fault {
	__u32	type;
	__u32	padding;
	union {
		struct iommu_fault_unrecoverable event;
		struct iommu_fault_page_request prm;
		__u8 padding2[56];
	};
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
 * @argsz: User filled size of this data
 * @version: API version of this structure
 * @flags: encodes whether the corresponding fields are valid
 *         (IOMMU_FAULT_PAGE_RESPONSE_* values)
 * @pasid: Process Address Space ID
 * @grpid: Page Request Group Index
 * @code: response code from &enum iommu_page_response_code
 */
struct iommu_page_response {
	__u32	argsz;
#define IOMMU_PAGE_RESP_VERSION_1	1
	__u32	version;
#define IOMMU_PAGE_RESP_PASID_VALID	(1 << 0)
	__u32	flags;
	__u32	pasid;
	__u32	grpid;
	__u32	code;
};

/* defines the granularity of the invalidation */
enum iommu_inv_granularity {
	IOMMU_INV_GRANU_DOMAIN,	/* domain-selective invalidation */
	IOMMU_INV_GRANU_PASID,	/* PASID-selective invalidation */
	IOMMU_INV_GRANU_ADDR,	/* page-selective invalidation */
	IOMMU_INV_GRANU_NR,	/* number of invalidation granularities */
};

/**
 * struct iommu_inv_addr_info - Address Selective Invalidation Structure
 *
 * @flags: indicates the granularity of the address-selective invalidation
 * - If the PASID bit is set, the @pasid field is populated and the invalidation
 *   relates to cache entries tagged with this PASID and matching the address
 *   range.
 * - If ARCHID bit is set, @archid is populated and the invalidation relates
 *   to cache entries tagged with this architecture specific ID and matching
 *   the address range.
 * - Both PASID and ARCHID can be set as they may tag different caches.
 * - If neither PASID or ARCHID is set, global addr invalidation applies.
 * - The LEAF flag indicates whether only the leaf PTE caching needs to be
 *   invalidated and other paging structure caches can be preserved.
 * @pasid: process address space ID
 * @archid: architecture-specific ID
 * @addr: first stage/level input address
 * @granule_size: page/block size of the mapping in bytes
 * @nb_granules: number of contiguous granules to be invalidated
 */
struct iommu_inv_addr_info {
#define IOMMU_INV_ADDR_FLAGS_PASID	(1 << 0)
#define IOMMU_INV_ADDR_FLAGS_ARCHID	(1 << 1)
#define IOMMU_INV_ADDR_FLAGS_LEAF	(1 << 2)
	__u32	flags;
	__u32	archid;
	__u64	pasid;
	__u64	addr;
	__u64	granule_size;
	__u64	nb_granules;
};

/**
 * struct iommu_inv_pasid_info - PASID Selective Invalidation Structure
 *
 * @flags: indicates the granularity of the PASID-selective invalidation
 * - If the PASID bit is set, the @pasid field is populated and the invalidation
 *   relates to cache entries tagged with this PASID and matching the address
 *   range.
 * - If the ARCHID bit is set, the @archid is populated and the invalidation
 *   relates to cache entries tagged with this architecture specific ID and
 *   matching the address range.
 * - Both PASID and ARCHID can be set as they may tag different caches.
 * - At least one of PASID or ARCHID must be set.
 * @pasid: process address space ID
 * @archid: architecture-specific ID
 */
struct iommu_inv_pasid_info {
#define IOMMU_INV_PASID_FLAGS_PASID	(1 << 0)
#define IOMMU_INV_PASID_FLAGS_ARCHID	(1 << 1)
	__u32	flags;
	__u32	archid;
	__u64	pasid;
};

/**
 * struct iommu_cache_invalidate_info - First level/stage invalidation
 *     information
 * @argsz: User filled size of this data
 * @version: API version of this structure
 * @cache: bitfield that allows to select which caches to invalidate
 * @granularity: defines the lowest granularity used for the invalidation:
 *     domain > PASID > addr
 * @padding: reserved for future use (should be zero)
 * @pasid_info: invalidation data when @granularity is %IOMMU_INV_GRANU_PASID
 * @addr_info: invalidation data when @granularity is %IOMMU_INV_GRANU_ADDR
 *
 * Not all the combinations of cache/granularity are valid:
 *
 * +--------------+---------------+---------------+---------------+
 * | type /       |   DEV_IOTLB   |     IOTLB     |      PASID    |
 * | granularity  |               |               |      cache    |
 * +==============+===============+===============+===============+
 * | DOMAIN       |       N/A     |       Y       |       Y       |
 * +--------------+---------------+---------------+---------------+
 * | PASID        |       Y       |       Y       |       Y       |
 * +--------------+---------------+---------------+---------------+
 * | ADDR         |       Y       |       Y       |       N/A     |
 * +--------------+---------------+---------------+---------------+
 *
 * Invalidations by %IOMMU_INV_GRANU_DOMAIN don't take any argument other than
 * @version and @cache.
 *
 * If multiple cache types are invalidated simultaneously, they all
 * must support the used granularity.
 */
struct iommu_cache_invalidate_info {
	__u32	argsz;
#define IOMMU_CACHE_INVALIDATE_INFO_VERSION_1 1
	__u32	version;
/* IOMMU paging structure cache */
#define IOMMU_CACHE_INV_TYPE_IOTLB	(1 << 0) /* IOMMU IOTLB */
#define IOMMU_CACHE_INV_TYPE_DEV_IOTLB	(1 << 1) /* Device IOTLB */
#define IOMMU_CACHE_INV_TYPE_PASID	(1 << 2) /* PASID cache */
#define IOMMU_CACHE_INV_TYPE_NR		(3)
	__u8	cache;
	__u8	granularity;
	__u8	padding[6];
	union {
		struct iommu_inv_pasid_info pasid_info;
		struct iommu_inv_addr_info addr_info;
	} granu;
};

/**
 * struct iommu_gpasid_bind_data_vtd - Intel VT-d specific data on device and guest
 * SVA binding.
 *
 * @flags:	VT-d PASID table entry attributes
 * @pat:	Page attribute table data to compute effective memory type
 * @emt:	Extended memory type
 *
 * Only guest vIOMMU selectable and effective options are passed down to
 * the host IOMMU.
 */
struct iommu_gpasid_bind_data_vtd {
#define IOMMU_SVA_VTD_GPASID_SRE	(1 << 0) /* supervisor request */
#define IOMMU_SVA_VTD_GPASID_EAFE	(1 << 1) /* extended access enable */
#define IOMMU_SVA_VTD_GPASID_PCD	(1 << 2) /* page-level cache disable */
#define IOMMU_SVA_VTD_GPASID_PWT	(1 << 3) /* page-level write through */
#define IOMMU_SVA_VTD_GPASID_EMTE	(1 << 4) /* extended mem type enable */
#define IOMMU_SVA_VTD_GPASID_CD		(1 << 5) /* PASID-level cache disable */
#define IOMMU_SVA_VTD_GPASID_WPE	(1 << 6) /* Write protect enable */
#define IOMMU_SVA_VTD_GPASID_LAST	(1 << 7)
	__u64 flags;
	__u32 pat;
	__u32 emt;
};

#define IOMMU_SVA_VTD_GPASID_MTS_MASK	(IOMMU_SVA_VTD_GPASID_CD | \
					 IOMMU_SVA_VTD_GPASID_EMTE | \
					 IOMMU_SVA_VTD_GPASID_PCD |  \
					 IOMMU_SVA_VTD_GPASID_PWT)

/**
 * struct iommu_gpasid_bind_data - Information about device and guest PASID binding
 * @argsz:	User filled size of this data
 * @version:	Version of this data structure
 * @format:	PASID table entry format
 * @flags:	Additional information on guest bind request
 * @gpgd:	Guest page directory base of the guest mm to bind
 * @hpasid:	Process address space ID used for the guest mm in host IOMMU
 * @gpasid:	Process address space ID used for the guest mm in guest IOMMU
 * @addr_width:	Guest virtual address width
 * @padding:	Reserved for future use (should be zero)
 * @vtd:	Intel VT-d specific data
 *
 * Guest to host PASID mapping can be an identity or non-identity, where guest
 * has its own PASID space. For non-identify mapping, guest to host PASID lookup
 * is needed when VM programs guest PASID into an assigned device. VMM may
 * trap such PASID programming then request host IOMMU driver to convert guest
 * PASID to host PASID based on this bind data.
 */
struct iommu_gpasid_bind_data {
	__u32 argsz;
#define IOMMU_GPASID_BIND_VERSION_1	1
	__u32 version;
#define IOMMU_PASID_FORMAT_INTEL_VTD	1
#define IOMMU_PASID_FORMAT_LAST		2
	__u32 format;
	__u32 addr_width;
#define IOMMU_SVA_GPASID_VAL	(1 << 0) /* guest PASID valid */
	__u64 flags;
	__u64 gpgd;
	__u64 hpasid;
	__u64 gpasid;
	__u8  padding[8];
	/* Vendor specific data */
	union {
		struct iommu_gpasid_bind_data_vtd vtd;
	} vendor;
};

#endif /* _UAPI_IOMMU_H */
