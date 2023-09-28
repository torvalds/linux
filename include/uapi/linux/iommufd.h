/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES.
 */
#ifndef _UAPI_IOMMUFD_H
#define _UAPI_IOMMUFD_H

#include <linux/types.h>
#include <linux/ioctl.h>

#define IOMMUFD_TYPE (';')

/**
 * DOC: General ioctl format
 *
 * The ioctl interface follows a general format to allow for extensibility. Each
 * ioctl is passed in a structure pointer as the argument providing the size of
 * the structure in the first u32. The kernel checks that any structure space
 * beyond what it understands is 0. This allows userspace to use the backward
 * compatible portion while consistently using the newer, larger, structures.
 *
 * ioctls use a standard meaning for common errnos:
 *
 *  - ENOTTY: The IOCTL number itself is not supported at all
 *  - E2BIG: The IOCTL number is supported, but the provided structure has
 *    non-zero in a part the kernel does not understand.
 *  - EOPNOTSUPP: The IOCTL number is supported, and the structure is
 *    understood, however a known field has a value the kernel does not
 *    understand or support.
 *  - EINVAL: Everything about the IOCTL was understood, but a field is not
 *    correct.
 *  - ENOENT: An ID or IOVA provided does not exist.
 *  - ENOMEM: Out of memory.
 *  - EOVERFLOW: Mathematics overflowed.
 *
 * As well as additional errnos, within specific ioctls.
 */
enum {
	IOMMUFD_CMD_BASE = 0x80,
	IOMMUFD_CMD_DESTROY = IOMMUFD_CMD_BASE,
	IOMMUFD_CMD_IOAS_ALLOC,
	IOMMUFD_CMD_IOAS_ALLOW_IOVAS,
	IOMMUFD_CMD_IOAS_COPY,
	IOMMUFD_CMD_IOAS_IOVA_RANGES,
	IOMMUFD_CMD_IOAS_MAP,
	IOMMUFD_CMD_IOAS_UNMAP,
	IOMMUFD_CMD_OPTION,
	IOMMUFD_CMD_VFIO_IOAS,
	IOMMUFD_CMD_HWPT_ALLOC,
	IOMMUFD_CMD_GET_HW_INFO,
};

/**
 * struct iommu_destroy - ioctl(IOMMU_DESTROY)
 * @size: sizeof(struct iommu_destroy)
 * @id: iommufd object ID to destroy. Can be any destroyable object type.
 *
 * Destroy any object held within iommufd.
 */
struct iommu_destroy {
	__u32 size;
	__u32 id;
};
#define IOMMU_DESTROY _IO(IOMMUFD_TYPE, IOMMUFD_CMD_DESTROY)

/**
 * struct iommu_ioas_alloc - ioctl(IOMMU_IOAS_ALLOC)
 * @size: sizeof(struct iommu_ioas_alloc)
 * @flags: Must be 0
 * @out_ioas_id: Output IOAS ID for the allocated object
 *
 * Allocate an IO Address Space (IOAS) which holds an IO Virtual Address (IOVA)
 * to memory mapping.
 */
struct iommu_ioas_alloc {
	__u32 size;
	__u32 flags;
	__u32 out_ioas_id;
};
#define IOMMU_IOAS_ALLOC _IO(IOMMUFD_TYPE, IOMMUFD_CMD_IOAS_ALLOC)

/**
 * struct iommu_iova_range - ioctl(IOMMU_IOVA_RANGE)
 * @start: First IOVA
 * @last: Inclusive last IOVA
 *
 * An interval in IOVA space.
 */
struct iommu_iova_range {
	__aligned_u64 start;
	__aligned_u64 last;
};

/**
 * struct iommu_ioas_iova_ranges - ioctl(IOMMU_IOAS_IOVA_RANGES)
 * @size: sizeof(struct iommu_ioas_iova_ranges)
 * @ioas_id: IOAS ID to read ranges from
 * @num_iovas: Input/Output total number of ranges in the IOAS
 * @__reserved: Must be 0
 * @allowed_iovas: Pointer to the output array of struct iommu_iova_range
 * @out_iova_alignment: Minimum alignment required for mapping IOVA
 *
 * Query an IOAS for ranges of allowed IOVAs. Mapping IOVA outside these ranges
 * is not allowed. num_iovas will be set to the total number of iovas and
 * the allowed_iovas[] will be filled in as space permits.
 *
 * The allowed ranges are dependent on the HW path the DMA operation takes, and
 * can change during the lifetime of the IOAS. A fresh empty IOAS will have a
 * full range, and each attached device will narrow the ranges based on that
 * device's HW restrictions. Detaching a device can widen the ranges. Userspace
 * should query ranges after every attach/detach to know what IOVAs are valid
 * for mapping.
 *
 * On input num_iovas is the length of the allowed_iovas array. On output it is
 * the total number of iovas filled in. The ioctl will return -EMSGSIZE and set
 * num_iovas to the required value if num_iovas is too small. In this case the
 * caller should allocate a larger output array and re-issue the ioctl.
 *
 * out_iova_alignment returns the minimum IOVA alignment that can be given
 * to IOMMU_IOAS_MAP/COPY. IOVA's must satisfy::
 *
 *   starting_iova % out_iova_alignment == 0
 *   (starting_iova + length) % out_iova_alignment == 0
 *
 * out_iova_alignment can be 1 indicating any IOVA is allowed. It cannot
 * be higher than the system PAGE_SIZE.
 */
struct iommu_ioas_iova_ranges {
	__u32 size;
	__u32 ioas_id;
	__u32 num_iovas;
	__u32 __reserved;
	__aligned_u64 allowed_iovas;
	__aligned_u64 out_iova_alignment;
};
#define IOMMU_IOAS_IOVA_RANGES _IO(IOMMUFD_TYPE, IOMMUFD_CMD_IOAS_IOVA_RANGES)

/**
 * struct iommu_ioas_allow_iovas - ioctl(IOMMU_IOAS_ALLOW_IOVAS)
 * @size: sizeof(struct iommu_ioas_allow_iovas)
 * @ioas_id: IOAS ID to allow IOVAs from
 * @num_iovas: Input/Output total number of ranges in the IOAS
 * @__reserved: Must be 0
 * @allowed_iovas: Pointer to array of struct iommu_iova_range
 *
 * Ensure a range of IOVAs are always available for allocation. If this call
 * succeeds then IOMMU_IOAS_IOVA_RANGES will never return a list of IOVA ranges
 * that are narrower than the ranges provided here. This call will fail if
 * IOMMU_IOAS_IOVA_RANGES is currently narrower than the given ranges.
 *
 * When an IOAS is first created the IOVA_RANGES will be maximally sized, and as
 * devices are attached the IOVA will narrow based on the device restrictions.
 * When an allowed range is specified any narrowing will be refused, ie device
 * attachment can fail if the device requires limiting within the allowed range.
 *
 * Automatic IOVA allocation is also impacted by this call. MAP will only
 * allocate within the allowed IOVAs if they are present.
 *
 * This call replaces the entire allowed list with the given list.
 */
struct iommu_ioas_allow_iovas {
	__u32 size;
	__u32 ioas_id;
	__u32 num_iovas;
	__u32 __reserved;
	__aligned_u64 allowed_iovas;
};
#define IOMMU_IOAS_ALLOW_IOVAS _IO(IOMMUFD_TYPE, IOMMUFD_CMD_IOAS_ALLOW_IOVAS)

/**
 * enum iommufd_ioas_map_flags - Flags for map and copy
 * @IOMMU_IOAS_MAP_FIXED_IOVA: If clear the kernel will compute an appropriate
 *                             IOVA to place the mapping at
 * @IOMMU_IOAS_MAP_WRITEABLE: DMA is allowed to write to this mapping
 * @IOMMU_IOAS_MAP_READABLE: DMA is allowed to read from this mapping
 */
enum iommufd_ioas_map_flags {
	IOMMU_IOAS_MAP_FIXED_IOVA = 1 << 0,
	IOMMU_IOAS_MAP_WRITEABLE = 1 << 1,
	IOMMU_IOAS_MAP_READABLE = 1 << 2,
};

/**
 * struct iommu_ioas_map - ioctl(IOMMU_IOAS_MAP)
 * @size: sizeof(struct iommu_ioas_map)
 * @flags: Combination of enum iommufd_ioas_map_flags
 * @ioas_id: IOAS ID to change the mapping of
 * @__reserved: Must be 0
 * @user_va: Userspace pointer to start mapping from
 * @length: Number of bytes to map
 * @iova: IOVA the mapping was placed at. If IOMMU_IOAS_MAP_FIXED_IOVA is set
 *        then this must be provided as input.
 *
 * Set an IOVA mapping from a user pointer. If FIXED_IOVA is specified then the
 * mapping will be established at iova, otherwise a suitable location based on
 * the reserved and allowed lists will be automatically selected and returned in
 * iova.
 *
 * If IOMMU_IOAS_MAP_FIXED_IOVA is specified then the iova range must currently
 * be unused, existing IOVA cannot be replaced.
 */
struct iommu_ioas_map {
	__u32 size;
	__u32 flags;
	__u32 ioas_id;
	__u32 __reserved;
	__aligned_u64 user_va;
	__aligned_u64 length;
	__aligned_u64 iova;
};
#define IOMMU_IOAS_MAP _IO(IOMMUFD_TYPE, IOMMUFD_CMD_IOAS_MAP)

/**
 * struct iommu_ioas_copy - ioctl(IOMMU_IOAS_COPY)
 * @size: sizeof(struct iommu_ioas_copy)
 * @flags: Combination of enum iommufd_ioas_map_flags
 * @dst_ioas_id: IOAS ID to change the mapping of
 * @src_ioas_id: IOAS ID to copy from
 * @length: Number of bytes to copy and map
 * @dst_iova: IOVA the mapping was placed at. If IOMMU_IOAS_MAP_FIXED_IOVA is
 *            set then this must be provided as input.
 * @src_iova: IOVA to start the copy
 *
 * Copy an already existing mapping from src_ioas_id and establish it in
 * dst_ioas_id. The src iova/length must exactly match a range used with
 * IOMMU_IOAS_MAP.
 *
 * This may be used to efficiently clone a subset of an IOAS to another, or as a
 * kind of 'cache' to speed up mapping. Copy has an efficiency advantage over
 * establishing equivalent new mappings, as internal resources are shared, and
 * the kernel will pin the user memory only once.
 */
struct iommu_ioas_copy {
	__u32 size;
	__u32 flags;
	__u32 dst_ioas_id;
	__u32 src_ioas_id;
	__aligned_u64 length;
	__aligned_u64 dst_iova;
	__aligned_u64 src_iova;
};
#define IOMMU_IOAS_COPY _IO(IOMMUFD_TYPE, IOMMUFD_CMD_IOAS_COPY)

/**
 * struct iommu_ioas_unmap - ioctl(IOMMU_IOAS_UNMAP)
 * @size: sizeof(struct iommu_ioas_unmap)
 * @ioas_id: IOAS ID to change the mapping of
 * @iova: IOVA to start the unmapping at
 * @length: Number of bytes to unmap, and return back the bytes unmapped
 *
 * Unmap an IOVA range. The iova/length must be a superset of a previously
 * mapped range used with IOMMU_IOAS_MAP or IOMMU_IOAS_COPY. Splitting or
 * truncating ranges is not allowed. The values 0 to U64_MAX will unmap
 * everything.
 */
struct iommu_ioas_unmap {
	__u32 size;
	__u32 ioas_id;
	__aligned_u64 iova;
	__aligned_u64 length;
};
#define IOMMU_IOAS_UNMAP _IO(IOMMUFD_TYPE, IOMMUFD_CMD_IOAS_UNMAP)

/**
 * enum iommufd_option - ioctl(IOMMU_OPTION_RLIMIT_MODE) and
 *                       ioctl(IOMMU_OPTION_HUGE_PAGES)
 * @IOMMU_OPTION_RLIMIT_MODE:
 *    Change how RLIMIT_MEMLOCK accounting works. The caller must have privilege
 *    to invoke this. Value 0 (default) is user based accouting, 1 uses process
 *    based accounting. Global option, object_id must be 0
 * @IOMMU_OPTION_HUGE_PAGES:
 *    Value 1 (default) allows contiguous pages to be combined when generating
 *    iommu mappings. Value 0 disables combining, everything is mapped to
 *    PAGE_SIZE. This can be useful for benchmarking.  This is a per-IOAS
 *    option, the object_id must be the IOAS ID.
 */
enum iommufd_option {
	IOMMU_OPTION_RLIMIT_MODE = 0,
	IOMMU_OPTION_HUGE_PAGES = 1,
};

/**
 * enum iommufd_option_ops - ioctl(IOMMU_OPTION_OP_SET) and
 *                           ioctl(IOMMU_OPTION_OP_GET)
 * @IOMMU_OPTION_OP_SET: Set the option's value
 * @IOMMU_OPTION_OP_GET: Get the option's value
 */
enum iommufd_option_ops {
	IOMMU_OPTION_OP_SET = 0,
	IOMMU_OPTION_OP_GET = 1,
};

/**
 * struct iommu_option - iommu option multiplexer
 * @size: sizeof(struct iommu_option)
 * @option_id: One of enum iommufd_option
 * @op: One of enum iommufd_option_ops
 * @__reserved: Must be 0
 * @object_id: ID of the object if required
 * @val64: Option value to set or value returned on get
 *
 * Change a simple option value. This multiplexor allows controlling options
 * on objects. IOMMU_OPTION_OP_SET will load an option and IOMMU_OPTION_OP_GET
 * will return the current value.
 */
struct iommu_option {
	__u32 size;
	__u32 option_id;
	__u16 op;
	__u16 __reserved;
	__u32 object_id;
	__aligned_u64 val64;
};
#define IOMMU_OPTION _IO(IOMMUFD_TYPE, IOMMUFD_CMD_OPTION)

/**
 * enum iommufd_vfio_ioas_op - IOMMU_VFIO_IOAS_* ioctls
 * @IOMMU_VFIO_IOAS_GET: Get the current compatibility IOAS
 * @IOMMU_VFIO_IOAS_SET: Change the current compatibility IOAS
 * @IOMMU_VFIO_IOAS_CLEAR: Disable VFIO compatibility
 */
enum iommufd_vfio_ioas_op {
	IOMMU_VFIO_IOAS_GET = 0,
	IOMMU_VFIO_IOAS_SET = 1,
	IOMMU_VFIO_IOAS_CLEAR = 2,
};

/**
 * struct iommu_vfio_ioas - ioctl(IOMMU_VFIO_IOAS)
 * @size: sizeof(struct iommu_vfio_ioas)
 * @ioas_id: For IOMMU_VFIO_IOAS_SET the input IOAS ID to set
 *           For IOMMU_VFIO_IOAS_GET will output the IOAS ID
 * @op: One of enum iommufd_vfio_ioas_op
 * @__reserved: Must be 0
 *
 * The VFIO compatibility support uses a single ioas because VFIO APIs do not
 * support the ID field. Set or Get the IOAS that VFIO compatibility will use.
 * When VFIO_GROUP_SET_CONTAINER is used on an iommufd it will get the
 * compatibility ioas, either by taking what is already set, or auto creating
 * one. From then on VFIO will continue to use that ioas and is not effected by
 * this ioctl. SET or CLEAR does not destroy any auto-created IOAS.
 */
struct iommu_vfio_ioas {
	__u32 size;
	__u32 ioas_id;
	__u16 op;
	__u16 __reserved;
};
#define IOMMU_VFIO_IOAS _IO(IOMMUFD_TYPE, IOMMUFD_CMD_VFIO_IOAS)

/**
 * enum iommufd_hwpt_alloc_flags - Flags for HWPT allocation
 * @IOMMU_HWPT_ALLOC_NEST_PARENT: If set, allocate a domain which can serve
 *                                as the parent domain in the nesting
 *                                configuration.
 */
enum iommufd_hwpt_alloc_flags {
	IOMMU_HWPT_ALLOC_NEST_PARENT = 1 << 0,
};

/**
 * struct iommu_hwpt_alloc - ioctl(IOMMU_HWPT_ALLOC)
 * @size: sizeof(struct iommu_hwpt_alloc)
 * @flags: Combination of enum iommufd_hwpt_alloc_flags
 * @dev_id: The device to allocate this HWPT for
 * @pt_id: The IOAS to connect this HWPT to
 * @out_hwpt_id: The ID of the new HWPT
 * @__reserved: Must be 0
 *
 * Explicitly allocate a hardware page table object. This is the same object
 * type that is returned by iommufd_device_attach() and represents the
 * underlying iommu driver's iommu_domain kernel object.
 *
 * A HWPT will be created with the IOVA mappings from the given IOAS.
 */
struct iommu_hwpt_alloc {
	__u32 size;
	__u32 flags;
	__u32 dev_id;
	__u32 pt_id;
	__u32 out_hwpt_id;
	__u32 __reserved;
};
#define IOMMU_HWPT_ALLOC _IO(IOMMUFD_TYPE, IOMMUFD_CMD_HWPT_ALLOC)

/**
 * struct iommu_hw_info_vtd - Intel VT-d hardware information
 *
 * @flags: Must be 0
 * @__reserved: Must be 0
 *
 * @cap_reg: Value of Intel VT-d capability register defined in VT-d spec
 *           section 11.4.2 Capability Register.
 * @ecap_reg: Value of Intel VT-d capability register defined in VT-d spec
 *            section 11.4.3 Extended Capability Register.
 *
 * User needs to understand the Intel VT-d specification to decode the
 * register value.
 */
struct iommu_hw_info_vtd {
	__u32 flags;
	__u32 __reserved;
	__aligned_u64 cap_reg;
	__aligned_u64 ecap_reg;
};

/**
 * enum iommu_hw_info_type - IOMMU Hardware Info Types
 * @IOMMU_HW_INFO_TYPE_NONE: Used by the drivers that do not report hardware
 *                           info
 * @IOMMU_HW_INFO_TYPE_INTEL_VTD: Intel VT-d iommu info type
 */
enum iommu_hw_info_type {
	IOMMU_HW_INFO_TYPE_NONE,
	IOMMU_HW_INFO_TYPE_INTEL_VTD,
};

/**
 * struct iommu_hw_info - ioctl(IOMMU_GET_HW_INFO)
 * @size: sizeof(struct iommu_hw_info)
 * @flags: Must be 0
 * @dev_id: The device bound to the iommufd
 * @data_len: Input the length of a user buffer in bytes. Output the length of
 *            data that kernel supports
 * @data_uptr: User pointer to a user-space buffer used by the kernel to fill
 *             the iommu type specific hardware information data
 * @out_data_type: Output the iommu hardware info type as defined in the enum
 *                 iommu_hw_info_type.
 * @__reserved: Must be 0
 *
 * Query an iommu type specific hardware information data from an iommu behind
 * a given device that has been bound to iommufd. This hardware info data will
 * be used to sync capabilities between the virtual iommu and the physical
 * iommu, e.g. a nested translation setup needs to check the hardware info, so
 * a guest stage-1 page table can be compatible with the physical iommu.
 *
 * To capture an iommu type specific hardware information data, @data_uptr and
 * its length @data_len must be provided. Trailing bytes will be zeroed if the
 * user buffer is larger than the data that kernel has. Otherwise, kernel only
 * fills the buffer using the given length in @data_len. If the ioctl succeeds,
 * @data_len will be updated to the length that kernel actually supports,
 * @out_data_type will be filled to decode the data filled in the buffer
 * pointed by @data_uptr. Input @data_len == zero is allowed.
 */
struct iommu_hw_info {
	__u32 size;
	__u32 flags;
	__u32 dev_id;
	__u32 data_len;
	__aligned_u64 data_uptr;
	__u32 out_data_type;
	__u32 __reserved;
};
#define IOMMU_GET_HW_INFO _IO(IOMMUFD_TYPE, IOMMUFD_CMD_GET_HW_INFO)
#endif
