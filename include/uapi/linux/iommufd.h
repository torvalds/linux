/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/* Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES.
 */
#ifndef _UAPI_IOMMUFD_H
#define _UAPI_IOMMUFD_H

#include <linux/ioctl.h>
#include <linux/types.h>

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
	IOMMUFD_CMD_IOAS_ALLOC = 0x81,
	IOMMUFD_CMD_IOAS_ALLOW_IOVAS = 0x82,
	IOMMUFD_CMD_IOAS_COPY = 0x83,
	IOMMUFD_CMD_IOAS_IOVA_RANGES = 0x84,
	IOMMUFD_CMD_IOAS_MAP = 0x85,
	IOMMUFD_CMD_IOAS_UNMAP = 0x86,
	IOMMUFD_CMD_OPTION = 0x87,
	IOMMUFD_CMD_VFIO_IOAS = 0x88,
	IOMMUFD_CMD_HWPT_ALLOC = 0x89,
	IOMMUFD_CMD_GET_HW_INFO = 0x8a,
	IOMMUFD_CMD_HWPT_SET_DIRTY_TRACKING = 0x8b,
	IOMMUFD_CMD_HWPT_GET_DIRTY_BITMAP = 0x8c,
	IOMMUFD_CMD_HWPT_INVALIDATE = 0x8d,
	IOMMUFD_CMD_FAULT_QUEUE_ALLOC = 0x8e,
	IOMMUFD_CMD_IOAS_MAP_FILE = 0x8f,
	IOMMUFD_CMD_VIOMMU_ALLOC = 0x90,
	IOMMUFD_CMD_VDEVICE_ALLOC = 0x91,
	IOMMUFD_CMD_IOAS_CHANGE_PROCESS = 0x92,
	IOMMUFD_CMD_VEVENTQ_ALLOC = 0x93,
	IOMMUFD_CMD_HW_QUEUE_ALLOC = 0x94,
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
 * struct iommu_ioas_map_file - ioctl(IOMMU_IOAS_MAP_FILE)
 * @size: sizeof(struct iommu_ioas_map_file)
 * @flags: same as for iommu_ioas_map
 * @ioas_id: same as for iommu_ioas_map
 * @fd: the memfd to map
 * @start: byte offset from start of file to map from
 * @length: same as for iommu_ioas_map
 * @iova: same as for iommu_ioas_map
 *
 * Set an IOVA mapping from a memfd file.  All other arguments and semantics
 * match those of IOMMU_IOAS_MAP.
 */
struct iommu_ioas_map_file {
	__u32 size;
	__u32 flags;
	__u32 ioas_id;
	__s32 fd;
	__aligned_u64 start;
	__aligned_u64 length;
	__aligned_u64 iova;
};
#define IOMMU_IOAS_MAP_FILE _IO(IOMMUFD_TYPE, IOMMUFD_CMD_IOAS_MAP_FILE)

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
 *    to invoke this. Value 0 (default) is user based accounting, 1 uses process
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
 * @IOMMU_HWPT_ALLOC_NEST_PARENT: If set, allocate a HWPT that can serve as
 *                                the parent HWPT in a nesting configuration.
 * @IOMMU_HWPT_ALLOC_DIRTY_TRACKING: Dirty tracking support for device IOMMU is
 *                                   enforced on device attachment
 * @IOMMU_HWPT_FAULT_ID_VALID: The fault_id field of hwpt allocation data is
 *                             valid.
 * @IOMMU_HWPT_ALLOC_PASID: Requests a domain that can be used with PASID. The
 *                          domain can be attached to any PASID on the device.
 *                          Any domain attached to the non-PASID part of the
 *                          device must also be flagged, otherwise attaching a
 *                          PASID will blocked.
 *                          For the user that wants to attach PASID, ioas is
 *                          not recommended for both the non-PASID part
 *                          and PASID part of the device.
 *                          If IOMMU does not support PASID it will return
 *                          error (-EOPNOTSUPP).
 */
enum iommufd_hwpt_alloc_flags {
	IOMMU_HWPT_ALLOC_NEST_PARENT = 1 << 0,
	IOMMU_HWPT_ALLOC_DIRTY_TRACKING = 1 << 1,
	IOMMU_HWPT_FAULT_ID_VALID = 1 << 2,
	IOMMU_HWPT_ALLOC_PASID = 1 << 3,
};

/**
 * enum iommu_hwpt_vtd_s1_flags - Intel VT-d stage-1 page table
 *                                entry attributes
 * @IOMMU_VTD_S1_SRE: Supervisor request
 * @IOMMU_VTD_S1_EAFE: Extended access enable
 * @IOMMU_VTD_S1_WPE: Write protect enable
 */
enum iommu_hwpt_vtd_s1_flags {
	IOMMU_VTD_S1_SRE = 1 << 0,
	IOMMU_VTD_S1_EAFE = 1 << 1,
	IOMMU_VTD_S1_WPE = 1 << 2,
};

/**
 * struct iommu_hwpt_vtd_s1 - Intel VT-d stage-1 page table
 *                            info (IOMMU_HWPT_DATA_VTD_S1)
 * @flags: Combination of enum iommu_hwpt_vtd_s1_flags
 * @pgtbl_addr: The base address of the stage-1 page table.
 * @addr_width: The address width of the stage-1 page table
 * @__reserved: Must be 0
 */
struct iommu_hwpt_vtd_s1 {
	__aligned_u64 flags;
	__aligned_u64 pgtbl_addr;
	__u32 addr_width;
	__u32 __reserved;
};

/**
 * struct iommu_hwpt_arm_smmuv3 - ARM SMMUv3 nested STE
 *                                (IOMMU_HWPT_DATA_ARM_SMMUV3)
 *
 * @ste: The first two double words of the user space Stream Table Entry for
 *       the translation. Must be little-endian.
 *       Allowed fields: (Refer to "5.2 Stream Table Entry" in SMMUv3 HW Spec)
 *       - word-0: V, Cfg, S1Fmt, S1ContextPtr, S1CDMax
 *       - word-1: EATS, S1DSS, S1CIR, S1COR, S1CSH, S1STALLD
 *
 * -EIO will be returned if @ste is not legal or contains any non-allowed field.
 * Cfg can be used to select a S1, Bypass or Abort configuration. A Bypass
 * nested domain will translate the same as the nesting parent. The S1 will
 * install a Context Descriptor Table pointing at userspace memory translated
 * by the nesting parent.
 */
struct iommu_hwpt_arm_smmuv3 {
	__aligned_le64 ste[2];
};

/**
 * enum iommu_hwpt_data_type - IOMMU HWPT Data Type
 * @IOMMU_HWPT_DATA_NONE: no data
 * @IOMMU_HWPT_DATA_VTD_S1: Intel VT-d stage-1 page table
 * @IOMMU_HWPT_DATA_ARM_SMMUV3: ARM SMMUv3 Context Descriptor Table
 */
enum iommu_hwpt_data_type {
	IOMMU_HWPT_DATA_NONE = 0,
	IOMMU_HWPT_DATA_VTD_S1 = 1,
	IOMMU_HWPT_DATA_ARM_SMMUV3 = 2,
};

/**
 * struct iommu_hwpt_alloc - ioctl(IOMMU_HWPT_ALLOC)
 * @size: sizeof(struct iommu_hwpt_alloc)
 * @flags: Combination of enum iommufd_hwpt_alloc_flags
 * @dev_id: The device to allocate this HWPT for
 * @pt_id: The IOAS or HWPT or vIOMMU to connect this HWPT to
 * @out_hwpt_id: The ID of the new HWPT
 * @__reserved: Must be 0
 * @data_type: One of enum iommu_hwpt_data_type
 * @data_len: Length of the type specific data
 * @data_uptr: User pointer to the type specific data
 * @fault_id: The ID of IOMMUFD_FAULT object. Valid only if flags field of
 *            IOMMU_HWPT_FAULT_ID_VALID is set.
 * @__reserved2: Padding to 64-bit alignment. Must be 0.
 *
 * Explicitly allocate a hardware page table object. This is the same object
 * type that is returned by iommufd_device_attach() and represents the
 * underlying iommu driver's iommu_domain kernel object.
 *
 * A kernel-managed HWPT will be created with the mappings from the given
 * IOAS via the @pt_id. The @data_type for this allocation must be set to
 * IOMMU_HWPT_DATA_NONE. The HWPT can be allocated as a parent HWPT for a
 * nesting configuration by passing IOMMU_HWPT_ALLOC_NEST_PARENT via @flags.
 *
 * A user-managed nested HWPT will be created from a given vIOMMU (wrapping a
 * parent HWPT) or a parent HWPT via @pt_id, in which the parent HWPT must be
 * allocated previously via the same ioctl from a given IOAS (@pt_id). In this
 * case, the @data_type must be set to a pre-defined type corresponding to an
 * I/O page table type supported by the underlying IOMMU hardware. The device
 * via @dev_id and the vIOMMU via @pt_id must be associated to the same IOMMU
 * instance.
 *
 * If the @data_type is set to IOMMU_HWPT_DATA_NONE, @data_len and
 * @data_uptr should be zero. Otherwise, both @data_len and @data_uptr
 * must be given.
 */
struct iommu_hwpt_alloc {
	__u32 size;
	__u32 flags;
	__u32 dev_id;
	__u32 pt_id;
	__u32 out_hwpt_id;
	__u32 __reserved;
	__u32 data_type;
	__u32 data_len;
	__aligned_u64 data_uptr;
	__u32 fault_id;
	__u32 __reserved2;
};
#define IOMMU_HWPT_ALLOC _IO(IOMMUFD_TYPE, IOMMUFD_CMD_HWPT_ALLOC)

/**
 * enum iommu_hw_info_vtd_flags - Flags for VT-d hw_info
 * @IOMMU_HW_INFO_VTD_ERRATA_772415_SPR17: If set, disallow read-only mappings
 *                                         on a nested_parent domain.
 *                                         https://www.intel.com/content/www/us/en/content-details/772415/content-details.html
 */
enum iommu_hw_info_vtd_flags {
	IOMMU_HW_INFO_VTD_ERRATA_772415_SPR17 = 1 << 0,
};

/**
 * struct iommu_hw_info_vtd - Intel VT-d hardware information
 *
 * @flags: Combination of enum iommu_hw_info_vtd_flags
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
 * struct iommu_hw_info_arm_smmuv3 - ARM SMMUv3 hardware information
 *                                   (IOMMU_HW_INFO_TYPE_ARM_SMMUV3)
 *
 * @flags: Must be set to 0
 * @__reserved: Must be 0
 * @idr: Implemented features for ARM SMMU Non-secure programming interface
 * @iidr: Information about the implementation and implementer of ARM SMMU,
 *        and architecture version supported
 * @aidr: ARM SMMU architecture version
 *
 * For the details of @idr, @iidr and @aidr, please refer to the chapters
 * from 6.3.1 to 6.3.6 in the SMMUv3 Spec.
 *
 * This reports the raw HW capability, and not all bits are meaningful to be
 * read by userspace. Only the following fields should be used:
 *
 * idr[0]: ST_LEVEL, TERM_MODEL, STALL_MODEL, TTENDIAN , CD2L, ASID16, TTF
 * idr[1]: SIDSIZE, SSIDSIZE
 * idr[3]: BBML, RIL
 * idr[5]: VAX, GRAN64K, GRAN16K, GRAN4K
 *
 * - S1P should be assumed to be true if a NESTED HWPT can be created
 * - VFIO/iommufd only support platforms with COHACC, it should be assumed to be
 *   true.
 * - ATS is a per-device property. If the VMM describes any devices as ATS
 *   capable in ACPI/DT it should set the corresponding idr.
 *
 * This list may expand in future (eg E0PD, AIE, PBHA, D128, DS etc). It is
 * important that VMMs do not read bits outside the list to allow for
 * compatibility with future kernels. Several features in the SMMUv3
 * architecture are not currently supported by the kernel for nesting: HTTU,
 * BTM, MPAM and others.
 */
struct iommu_hw_info_arm_smmuv3 {
	__u32 flags;
	__u32 __reserved;
	__u32 idr[6];
	__u32 iidr;
	__u32 aidr;
};

/**
 * struct iommu_hw_info_tegra241_cmdqv - NVIDIA Tegra241 CMDQV Hardware
 *         Information (IOMMU_HW_INFO_TYPE_TEGRA241_CMDQV)
 *
 * @flags: Must be 0
 * @version: Version number for the CMDQ-V HW for PARAM bits[03:00]
 * @log2vcmdqs: Log2 of the total number of VCMDQs for PARAM bits[07:04]
 * @log2vsids: Log2 of the total number of SID replacements for PARAM bits[15:12]
 * @__reserved: Must be 0
 *
 * VMM can use these fields directly in its emulated global PARAM register. Note
 * that only one Virtual Interface (VINTF) should be exposed to a VM, i.e. PARAM
 * bits[11:08] should be set to 0 for log2 of the total number of VINTFs.
 */
struct iommu_hw_info_tegra241_cmdqv {
	__u32 flags;
	__u8 version;
	__u8 log2vcmdqs;
	__u8 log2vsids;
	__u8 __reserved;
};

/**
 * enum iommu_hw_info_type - IOMMU Hardware Info Types
 * @IOMMU_HW_INFO_TYPE_NONE: Output by the drivers that do not report hardware
 *                           info
 * @IOMMU_HW_INFO_TYPE_DEFAULT: Input to request for a default type
 * @IOMMU_HW_INFO_TYPE_INTEL_VTD: Intel VT-d iommu info type
 * @IOMMU_HW_INFO_TYPE_ARM_SMMUV3: ARM SMMUv3 iommu info type
 * @IOMMU_HW_INFO_TYPE_TEGRA241_CMDQV: NVIDIA Tegra241 CMDQV (extension for ARM
 *                                     SMMUv3) info type
 */
enum iommu_hw_info_type {
	IOMMU_HW_INFO_TYPE_NONE = 0,
	IOMMU_HW_INFO_TYPE_DEFAULT = 0,
	IOMMU_HW_INFO_TYPE_INTEL_VTD = 1,
	IOMMU_HW_INFO_TYPE_ARM_SMMUV3 = 2,
	IOMMU_HW_INFO_TYPE_TEGRA241_CMDQV = 3,
};

/**
 * enum iommufd_hw_capabilities
 * @IOMMU_HW_CAP_DIRTY_TRACKING: IOMMU hardware support for dirty tracking
 *                               If available, it means the following APIs
 *                               are supported:
 *
 *                                   IOMMU_HWPT_GET_DIRTY_BITMAP
 *                                   IOMMU_HWPT_SET_DIRTY_TRACKING
 *
 * @IOMMU_HW_CAP_PCI_PASID_EXEC: Execute Permission Supported, user ignores it
 *                               when the struct
 *                               iommu_hw_info::out_max_pasid_log2 is zero.
 * @IOMMU_HW_CAP_PCI_PASID_PRIV: Privileged Mode Supported, user ignores it
 *                               when the struct
 *                               iommu_hw_info::out_max_pasid_log2 is zero.
 */
enum iommufd_hw_capabilities {
	IOMMU_HW_CAP_DIRTY_TRACKING = 1 << 0,
	IOMMU_HW_CAP_PCI_PASID_EXEC = 1 << 1,
	IOMMU_HW_CAP_PCI_PASID_PRIV = 1 << 2,
};

/**
 * enum iommufd_hw_info_flags - Flags for iommu_hw_info
 * @IOMMU_HW_INFO_FLAG_INPUT_TYPE: If set, @in_data_type carries an input type
 *                                 for user space to request for a specific info
 */
enum iommufd_hw_info_flags {
	IOMMU_HW_INFO_FLAG_INPUT_TYPE = 1 << 0,
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
 * @in_data_type: This shares the same field with @out_data_type, making it be
 *                a bidirectional field. When IOMMU_HW_INFO_FLAG_INPUT_TYPE is
 *                set, an input type carried via this @in_data_type field will
 *                be valid, requesting for the info data to the given type. If
 *                IOMMU_HW_INFO_FLAG_INPUT_TYPE is unset, any input value will
 *                be seen as IOMMU_HW_INFO_TYPE_DEFAULT
 * @out_data_type: Output the iommu hardware info type as defined in the enum
 *                 iommu_hw_info_type.
 * @out_capabilities: Output the generic iommu capability info type as defined
 *                    in the enum iommu_hw_capabilities.
 * @out_max_pasid_log2: Output the width of PASIDs. 0 means no PASID support.
 *                      PCI devices turn to out_capabilities to check if the
 *                      specific capabilities is supported or not.
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
	union {
		__u32 in_data_type;
		__u32 out_data_type;
	};
	__u8 out_max_pasid_log2;
	__u8 __reserved[3];
	__aligned_u64 out_capabilities;
};
#define IOMMU_GET_HW_INFO _IO(IOMMUFD_TYPE, IOMMUFD_CMD_GET_HW_INFO)

/*
 * enum iommufd_hwpt_set_dirty_tracking_flags - Flags for steering dirty
 *                                              tracking
 * @IOMMU_HWPT_DIRTY_TRACKING_ENABLE: Enable dirty tracking
 */
enum iommufd_hwpt_set_dirty_tracking_flags {
	IOMMU_HWPT_DIRTY_TRACKING_ENABLE = 1,
};

/**
 * struct iommu_hwpt_set_dirty_tracking - ioctl(IOMMU_HWPT_SET_DIRTY_TRACKING)
 * @size: sizeof(struct iommu_hwpt_set_dirty_tracking)
 * @flags: Combination of enum iommufd_hwpt_set_dirty_tracking_flags
 * @hwpt_id: HW pagetable ID that represents the IOMMU domain
 * @__reserved: Must be 0
 *
 * Toggle dirty tracking on an HW pagetable.
 */
struct iommu_hwpt_set_dirty_tracking {
	__u32 size;
	__u32 flags;
	__u32 hwpt_id;
	__u32 __reserved;
};
#define IOMMU_HWPT_SET_DIRTY_TRACKING _IO(IOMMUFD_TYPE, \
					  IOMMUFD_CMD_HWPT_SET_DIRTY_TRACKING)

/**
 * enum iommufd_hwpt_get_dirty_bitmap_flags - Flags for getting dirty bits
 * @IOMMU_HWPT_GET_DIRTY_BITMAP_NO_CLEAR: Just read the PTEs without clearing
 *                                        any dirty bits metadata. This flag
 *                                        can be passed in the expectation
 *                                        where the next operation is an unmap
 *                                        of the same IOVA range.
 *
 */
enum iommufd_hwpt_get_dirty_bitmap_flags {
	IOMMU_HWPT_GET_DIRTY_BITMAP_NO_CLEAR = 1,
};

/**
 * struct iommu_hwpt_get_dirty_bitmap - ioctl(IOMMU_HWPT_GET_DIRTY_BITMAP)
 * @size: sizeof(struct iommu_hwpt_get_dirty_bitmap)
 * @hwpt_id: HW pagetable ID that represents the IOMMU domain
 * @flags: Combination of enum iommufd_hwpt_get_dirty_bitmap_flags
 * @__reserved: Must be 0
 * @iova: base IOVA of the bitmap first bit
 * @length: IOVA range size
 * @page_size: page size granularity of each bit in the bitmap
 * @data: bitmap where to set the dirty bits. The bitmap bits each
 *        represent a page_size which you deviate from an arbitrary iova.
 *
 * Checking a given IOVA is dirty:
 *
 *  data[(iova / page_size) / 64] & (1ULL << ((iova / page_size) % 64))
 *
 * Walk the IOMMU pagetables for a given IOVA range to return a bitmap
 * with the dirty IOVAs. In doing so it will also by default clear any
 * dirty bit metadata set in the IOPTE.
 */
struct iommu_hwpt_get_dirty_bitmap {
	__u32 size;
	__u32 hwpt_id;
	__u32 flags;
	__u32 __reserved;
	__aligned_u64 iova;
	__aligned_u64 length;
	__aligned_u64 page_size;
	__aligned_u64 data;
};
#define IOMMU_HWPT_GET_DIRTY_BITMAP _IO(IOMMUFD_TYPE, \
					IOMMUFD_CMD_HWPT_GET_DIRTY_BITMAP)

/**
 * enum iommu_hwpt_invalidate_data_type - IOMMU HWPT Cache Invalidation
 *                                        Data Type
 * @IOMMU_HWPT_INVALIDATE_DATA_VTD_S1: Invalidation data for VTD_S1
 * @IOMMU_VIOMMU_INVALIDATE_DATA_ARM_SMMUV3: Invalidation data for ARM SMMUv3
 */
enum iommu_hwpt_invalidate_data_type {
	IOMMU_HWPT_INVALIDATE_DATA_VTD_S1 = 0,
	IOMMU_VIOMMU_INVALIDATE_DATA_ARM_SMMUV3 = 1,
};

/**
 * enum iommu_hwpt_vtd_s1_invalidate_flags - Flags for Intel VT-d
 *                                           stage-1 cache invalidation
 * @IOMMU_VTD_INV_FLAGS_LEAF: Indicates whether the invalidation applies
 *                            to all-levels page structure cache or just
 *                            the leaf PTE cache.
 */
enum iommu_hwpt_vtd_s1_invalidate_flags {
	IOMMU_VTD_INV_FLAGS_LEAF = 1 << 0,
};

/**
 * struct iommu_hwpt_vtd_s1_invalidate - Intel VT-d cache invalidation
 *                                       (IOMMU_HWPT_INVALIDATE_DATA_VTD_S1)
 * @addr: The start address of the range to be invalidated. It needs to
 *        be 4KB aligned.
 * @npages: Number of contiguous 4K pages to be invalidated.
 * @flags: Combination of enum iommu_hwpt_vtd_s1_invalidate_flags
 * @__reserved: Must be 0
 *
 * The Intel VT-d specific invalidation data for user-managed stage-1 cache
 * invalidation in nested translation. Userspace uses this structure to
 * tell the impacted cache scope after modifying the stage-1 page table.
 *
 * Invalidating all the caches related to the page table by setting @addr
 * to be 0 and @npages to be U64_MAX.
 *
 * The device TLB will be invalidated automatically if ATS is enabled.
 */
struct iommu_hwpt_vtd_s1_invalidate {
	__aligned_u64 addr;
	__aligned_u64 npages;
	__u32 flags;
	__u32 __reserved;
};

/**
 * struct iommu_viommu_arm_smmuv3_invalidate - ARM SMMUv3 cache invalidation
 *         (IOMMU_VIOMMU_INVALIDATE_DATA_ARM_SMMUV3)
 * @cmd: 128-bit cache invalidation command that runs in SMMU CMDQ.
 *       Must be little-endian.
 *
 * Supported command list only when passing in a vIOMMU via @hwpt_id:
 *     CMDQ_OP_TLBI_NSNH_ALL
 *     CMDQ_OP_TLBI_NH_VA
 *     CMDQ_OP_TLBI_NH_VAA
 *     CMDQ_OP_TLBI_NH_ALL
 *     CMDQ_OP_TLBI_NH_ASID
 *     CMDQ_OP_ATC_INV
 *     CMDQ_OP_CFGI_CD
 *     CMDQ_OP_CFGI_CD_ALL
 *
 * -EIO will be returned if the command is not supported.
 */
struct iommu_viommu_arm_smmuv3_invalidate {
	__aligned_le64 cmd[2];
};

/**
 * struct iommu_hwpt_invalidate - ioctl(IOMMU_HWPT_INVALIDATE)
 * @size: sizeof(struct iommu_hwpt_invalidate)
 * @hwpt_id: ID of a nested HWPT or a vIOMMU, for cache invalidation
 * @data_uptr: User pointer to an array of driver-specific cache invalidation
 *             data.
 * @data_type: One of enum iommu_hwpt_invalidate_data_type, defining the data
 *             type of all the entries in the invalidation request array. It
 *             should be a type supported by the hwpt pointed by @hwpt_id.
 * @entry_len: Length (in bytes) of a request entry in the request array
 * @entry_num: Input the number of cache invalidation requests in the array.
 *             Output the number of requests successfully handled by kernel.
 * @__reserved: Must be 0.
 *
 * Invalidate iommu cache for user-managed page table or vIOMMU. Modifications
 * on a user-managed page table should be followed by this operation, if a HWPT
 * is passed in via @hwpt_id. Other caches, such as device cache or descriptor
 * cache can be flushed if a vIOMMU is passed in via the @hwpt_id field.
 *
 * Each ioctl can support one or more cache invalidation requests in the array
 * that has a total size of @entry_len * @entry_num.
 *
 * An empty invalidation request array by setting @entry_num==0 is allowed, and
 * @entry_len and @data_uptr would be ignored in this case. This can be used to
 * check if the given @data_type is supported or not by kernel.
 */
struct iommu_hwpt_invalidate {
	__u32 size;
	__u32 hwpt_id;
	__aligned_u64 data_uptr;
	__u32 data_type;
	__u32 entry_len;
	__u32 entry_num;
	__u32 __reserved;
};
#define IOMMU_HWPT_INVALIDATE _IO(IOMMUFD_TYPE, IOMMUFD_CMD_HWPT_INVALIDATE)

/**
 * enum iommu_hwpt_pgfault_flags - flags for struct iommu_hwpt_pgfault
 * @IOMMU_PGFAULT_FLAGS_PASID_VALID: The pasid field of the fault data is
 *                                   valid.
 * @IOMMU_PGFAULT_FLAGS_LAST_PAGE: It's the last fault of a fault group.
 */
enum iommu_hwpt_pgfault_flags {
	IOMMU_PGFAULT_FLAGS_PASID_VALID		= (1 << 0),
	IOMMU_PGFAULT_FLAGS_LAST_PAGE		= (1 << 1),
};

/**
 * enum iommu_hwpt_pgfault_perm - perm bits for struct iommu_hwpt_pgfault
 * @IOMMU_PGFAULT_PERM_READ: request for read permission
 * @IOMMU_PGFAULT_PERM_WRITE: request for write permission
 * @IOMMU_PGFAULT_PERM_EXEC: (PCIE 10.4.1) request with a PASID that has the
 *                           Execute Requested bit set in PASID TLP Prefix.
 * @IOMMU_PGFAULT_PERM_PRIV: (PCIE 10.4.1) request with a PASID that has the
 *                           Privileged Mode Requested bit set in PASID TLP
 *                           Prefix.
 */
enum iommu_hwpt_pgfault_perm {
	IOMMU_PGFAULT_PERM_READ			= (1 << 0),
	IOMMU_PGFAULT_PERM_WRITE		= (1 << 1),
	IOMMU_PGFAULT_PERM_EXEC			= (1 << 2),
	IOMMU_PGFAULT_PERM_PRIV			= (1 << 3),
};

/**
 * struct iommu_hwpt_pgfault - iommu page fault data
 * @flags: Combination of enum iommu_hwpt_pgfault_flags
 * @dev_id: id of the originated device
 * @pasid: Process Address Space ID
 * @grpid: Page Request Group Index
 * @perm: Combination of enum iommu_hwpt_pgfault_perm
 * @__reserved: Must be 0.
 * @addr: Fault address
 * @length: a hint of how much data the requestor is expecting to fetch. For
 *          example, if the PRI initiator knows it is going to do a 10MB
 *          transfer, it could fill in 10MB and the OS could pre-fault in
 *          10MB of IOVA. It's default to 0 if there's no such hint.
 * @cookie: kernel-managed cookie identifying a group of fault messages. The
 *          cookie number encoded in the last page fault of the group should
 *          be echoed back in the response message.
 */
struct iommu_hwpt_pgfault {
	__u32 flags;
	__u32 dev_id;
	__u32 pasid;
	__u32 grpid;
	__u32 perm;
	__u32 __reserved;
	__aligned_u64 addr;
	__u32 length;
	__u32 cookie;
};

/**
 * enum iommufd_page_response_code - Return status of fault handlers
 * @IOMMUFD_PAGE_RESP_SUCCESS: Fault has been handled and the page tables
 *                             populated, retry the access. This is the
 *                             "Success" defined in PCI 10.4.2.1.
 * @IOMMUFD_PAGE_RESP_INVALID: Could not handle this fault, don't retry the
 *                             access. This is the "Invalid Request" in PCI
 *                             10.4.2.1.
 */
enum iommufd_page_response_code {
	IOMMUFD_PAGE_RESP_SUCCESS = 0,
	IOMMUFD_PAGE_RESP_INVALID = 1,
};

/**
 * struct iommu_hwpt_page_response - IOMMU page fault response
 * @cookie: The kernel-managed cookie reported in the fault message.
 * @code: One of response code in enum iommufd_page_response_code.
 */
struct iommu_hwpt_page_response {
	__u32 cookie;
	__u32 code;
};

/**
 * struct iommu_fault_alloc - ioctl(IOMMU_FAULT_QUEUE_ALLOC)
 * @size: sizeof(struct iommu_fault_alloc)
 * @flags: Must be 0
 * @out_fault_id: The ID of the new FAULT
 * @out_fault_fd: The fd of the new FAULT
 *
 * Explicitly allocate a fault handling object.
 */
struct iommu_fault_alloc {
	__u32 size;
	__u32 flags;
	__u32 out_fault_id;
	__u32 out_fault_fd;
};
#define IOMMU_FAULT_QUEUE_ALLOC _IO(IOMMUFD_TYPE, IOMMUFD_CMD_FAULT_QUEUE_ALLOC)

/**
 * enum iommu_viommu_type - Virtual IOMMU Type
 * @IOMMU_VIOMMU_TYPE_DEFAULT: Reserved for future use
 * @IOMMU_VIOMMU_TYPE_ARM_SMMUV3: ARM SMMUv3 driver specific type
 * @IOMMU_VIOMMU_TYPE_TEGRA241_CMDQV: NVIDIA Tegra241 CMDQV (extension for ARM
 *                                    SMMUv3) enabled ARM SMMUv3 type
 */
enum iommu_viommu_type {
	IOMMU_VIOMMU_TYPE_DEFAULT = 0,
	IOMMU_VIOMMU_TYPE_ARM_SMMUV3 = 1,
	IOMMU_VIOMMU_TYPE_TEGRA241_CMDQV = 2,
};

/**
 * struct iommu_viommu_tegra241_cmdqv - NVIDIA Tegra241 CMDQV Virtual Interface
 *                                      (IOMMU_VIOMMU_TYPE_TEGRA241_CMDQV)
 * @out_vintf_mmap_offset: mmap offset argument for VINTF's page0
 * @out_vintf_mmap_length: mmap length argument for VINTF's page0
 *
 * Both @out_vintf_mmap_offset and @out_vintf_mmap_length are reported by kernel
 * for user space to mmap the VINTF page0 from the host physical address space
 * to the guest physical address space so that a guest kernel can directly R/W
 * access to the VINTF page0 in order to control its virtual command queues.
 */
struct iommu_viommu_tegra241_cmdqv {
	__aligned_u64 out_vintf_mmap_offset;
	__aligned_u64 out_vintf_mmap_length;
};

/**
 * struct iommu_viommu_alloc - ioctl(IOMMU_VIOMMU_ALLOC)
 * @size: sizeof(struct iommu_viommu_alloc)
 * @flags: Must be 0
 * @type: Type of the virtual IOMMU. Must be defined in enum iommu_viommu_type
 * @dev_id: The device's physical IOMMU will be used to back the virtual IOMMU
 * @hwpt_id: ID of a nesting parent HWPT to associate to
 * @out_viommu_id: Output virtual IOMMU ID for the allocated object
 * @data_len: Length of the type specific data
 * @__reserved: Must be 0
 * @data_uptr: User pointer to a driver-specific virtual IOMMU data
 *
 * Allocate a virtual IOMMU object, representing the underlying physical IOMMU's
 * virtualization support that is a security-isolated slice of the real IOMMU HW
 * that is unique to a specific VM. Operations global to the IOMMU are connected
 * to the vIOMMU, such as:
 * - Security namespace for guest owned ID, e.g. guest-controlled cache tags
 * - Non-device-affiliated event reporting, e.g. invalidation queue errors
 * - Access to a sharable nesting parent pagetable across physical IOMMUs
 * - Virtualization of various platforms IDs, e.g. RIDs and others
 * - Delivery of paravirtualized invalidation
 * - Direct assigned invalidation queues
 * - Direct assigned interrupts
 */
struct iommu_viommu_alloc {
	__u32 size;
	__u32 flags;
	__u32 type;
	__u32 dev_id;
	__u32 hwpt_id;
	__u32 out_viommu_id;
	__u32 data_len;
	__u32 __reserved;
	__aligned_u64 data_uptr;
};
#define IOMMU_VIOMMU_ALLOC _IO(IOMMUFD_TYPE, IOMMUFD_CMD_VIOMMU_ALLOC)

/**
 * struct iommu_vdevice_alloc - ioctl(IOMMU_VDEVICE_ALLOC)
 * @size: sizeof(struct iommu_vdevice_alloc)
 * @viommu_id: vIOMMU ID to associate with the virtual device
 * @dev_id: The physical device to allocate a virtual instance on the vIOMMU
 * @out_vdevice_id: Object handle for the vDevice. Pass to IOMMU_DESTORY
 * @virt_id: Virtual device ID per vIOMMU, e.g. vSID of ARM SMMUv3, vDeviceID
 *           of AMD IOMMU, and vRID of Intel VT-d
 *
 * Allocate a virtual device instance (for a physical device) against a vIOMMU.
 * This instance holds the device's information (related to its vIOMMU) in a VM.
 * User should use IOMMU_DESTROY to destroy the virtual device before
 * destroying the physical device (by closing vfio_cdev fd). Otherwise the
 * virtual device would be forcibly destroyed on physical device destruction,
 * its vdevice_id would be permanently leaked (unremovable & unreusable) until
 * iommu fd closed.
 */
struct iommu_vdevice_alloc {
	__u32 size;
	__u32 viommu_id;
	__u32 dev_id;
	__u32 out_vdevice_id;
	__aligned_u64 virt_id;
};
#define IOMMU_VDEVICE_ALLOC _IO(IOMMUFD_TYPE, IOMMUFD_CMD_VDEVICE_ALLOC)

/**
 * struct iommu_ioas_change_process - ioctl(VFIO_IOAS_CHANGE_PROCESS)
 * @size: sizeof(struct iommu_ioas_change_process)
 * @__reserved: Must be 0
 *
 * This transfers pinned memory counts for every memory map in every IOAS
 * in the context to the current process.  This only supports maps created
 * with IOMMU_IOAS_MAP_FILE, and returns EINVAL if other maps are present.
 * If the ioctl returns a failure status, then nothing is changed.
 *
 * This API is useful for transferring operation of a device from one process
 * to another, such as during userland live update.
 */
struct iommu_ioas_change_process {
	__u32 size;
	__u32 __reserved;
};

#define IOMMU_IOAS_CHANGE_PROCESS \
	_IO(IOMMUFD_TYPE, IOMMUFD_CMD_IOAS_CHANGE_PROCESS)

/**
 * enum iommu_veventq_flag - flag for struct iommufd_vevent_header
 * @IOMMU_VEVENTQ_FLAG_LOST_EVENTS: vEVENTQ has lost vEVENTs
 */
enum iommu_veventq_flag {
	IOMMU_VEVENTQ_FLAG_LOST_EVENTS = (1U << 0),
};

/**
 * struct iommufd_vevent_header - Virtual Event Header for a vEVENTQ Status
 * @flags: Combination of enum iommu_veventq_flag
 * @sequence: The sequence index of a vEVENT in the vEVENTQ, with a range of
 *            [0, INT_MAX] where the following index of INT_MAX is 0
 *
 * Each iommufd_vevent_header reports a sequence index of the following vEVENT:
 *
 * +----------------------+-------+----------------------+-------+---+-------+
 * | header0 {sequence=0} | data0 | header1 {sequence=1} | data1 |...| dataN |
 * +----------------------+-------+----------------------+-------+---+-------+
 *
 * And this sequence index is expected to be monotonic to the sequence index of
 * the previous vEVENT. If two adjacent sequence indexes has a delta larger than
 * 1, it means that delta - 1 number of vEVENTs has lost, e.g. two lost vEVENTs:
 *
 * +-----+----------------------+-------+----------------------+-------+-----+
 * | ... | header3 {sequence=3} | data3 | header6 {sequence=6} | data6 | ... |
 * +-----+----------------------+-------+----------------------+-------+-----+
 *
 * If a vEVENT lost at the tail of the vEVENTQ and there is no following vEVENT
 * providing the next sequence index, an IOMMU_VEVENTQ_FLAG_LOST_EVENTS header
 * would be added to the tail, and no data would follow this header:
 *
 * +--+----------------------+-------+-----------------------------------------+
 * |..| header3 {sequence=3} | data3 | header4 {flags=LOST_EVENTS, sequence=4} |
 * +--+----------------------+-------+-----------------------------------------+
 */
struct iommufd_vevent_header {
	__u32 flags;
	__u32 sequence;
};

/**
 * enum iommu_veventq_type - Virtual Event Queue Type
 * @IOMMU_VEVENTQ_TYPE_DEFAULT: Reserved for future use
 * @IOMMU_VEVENTQ_TYPE_ARM_SMMUV3: ARM SMMUv3 Virtual Event Queue
 * @IOMMU_VEVENTQ_TYPE_TEGRA241_CMDQV: NVIDIA Tegra241 CMDQV Extension IRQ
 */
enum iommu_veventq_type {
	IOMMU_VEVENTQ_TYPE_DEFAULT = 0,
	IOMMU_VEVENTQ_TYPE_ARM_SMMUV3 = 1,
	IOMMU_VEVENTQ_TYPE_TEGRA241_CMDQV = 2,
};

/**
 * struct iommu_vevent_arm_smmuv3 - ARM SMMUv3 Virtual Event
 *                                  (IOMMU_VEVENTQ_TYPE_ARM_SMMUV3)
 * @evt: 256-bit ARM SMMUv3 Event record, little-endian.
 *       Reported event records: (Refer to "7.3 Event records" in SMMUv3 HW Spec)
 *       - 0x04 C_BAD_STE
 *       - 0x06 F_STREAM_DISABLED
 *       - 0x08 C_BAD_SUBSTREAMID
 *       - 0x0a C_BAD_CD
 *       - 0x10 F_TRANSLATION
 *       - 0x11 F_ADDR_SIZE
 *       - 0x12 F_ACCESS
 *       - 0x13 F_PERMISSION
 *
 * StreamID field reports a virtual device ID. To receive a virtual event for a
 * device, a vDEVICE must be allocated via IOMMU_VDEVICE_ALLOC.
 */
struct iommu_vevent_arm_smmuv3 {
	__aligned_le64 evt[4];
};

/**
 * struct iommu_vevent_tegra241_cmdqv - Tegra241 CMDQV IRQ
 *                                      (IOMMU_VEVENTQ_TYPE_TEGRA241_CMDQV)
 * @lvcmdq_err_map: 128-bit logical vcmdq error map, little-endian.
 *                  (Refer to register LVCMDQ_ERR_MAPs per VINTF )
 *
 * The 128-bit register value from HW exclusively reflect the error bits for a
 * Virtual Interface represented by a vIOMMU object. Read and report directly.
 */
struct iommu_vevent_tegra241_cmdqv {
	__aligned_le64 lvcmdq_err_map[2];
};

/**
 * struct iommu_veventq_alloc - ioctl(IOMMU_VEVENTQ_ALLOC)
 * @size: sizeof(struct iommu_veventq_alloc)
 * @flags: Must be 0
 * @viommu_id: virtual IOMMU ID to associate the vEVENTQ with
 * @type: Type of the vEVENTQ. Must be defined in enum iommu_veventq_type
 * @veventq_depth: Maximum number of events in the vEVENTQ
 * @out_veventq_id: The ID of the new vEVENTQ
 * @out_veventq_fd: The fd of the new vEVENTQ. User space must close the
 *                  successfully returned fd after using it
 * @__reserved: Must be 0
 *
 * Explicitly allocate a virtual event queue interface for a vIOMMU. A vIOMMU
 * can have multiple FDs for different types, but is confined to one per @type.
 * User space should open the @out_veventq_fd to read vEVENTs out of a vEVENTQ,
 * if there are vEVENTs available. A vEVENTQ will lose events due to overflow,
 * if the number of the vEVENTs hits @veventq_depth.
 *
 * Each vEVENT in a vEVENTQ encloses a struct iommufd_vevent_header followed by
 * a type-specific data structure, in a normal case:
 *
 * +-+---------+-------+---------+-------+-----+---------+-------+-+
 * | | header0 | data0 | header1 | data1 | ... | headerN | dataN | |
 * +-+---------+-------+---------+-------+-----+---------+-------+-+
 *
 * unless a tailing IOMMU_VEVENTQ_FLAG_LOST_EVENTS header is logged (refer to
 * struct iommufd_vevent_header).
 */
struct iommu_veventq_alloc {
	__u32 size;
	__u32 flags;
	__u32 viommu_id;
	__u32 type;
	__u32 veventq_depth;
	__u32 out_veventq_id;
	__u32 out_veventq_fd;
	__u32 __reserved;
};
#define IOMMU_VEVENTQ_ALLOC _IO(IOMMUFD_TYPE, IOMMUFD_CMD_VEVENTQ_ALLOC)

/**
 * enum iommu_hw_queue_type - HW Queue Type
 * @IOMMU_HW_QUEUE_TYPE_DEFAULT: Reserved for future use
 * @IOMMU_HW_QUEUE_TYPE_TEGRA241_CMDQV: NVIDIA Tegra241 CMDQV (extension for ARM
 *                                      SMMUv3) Virtual Command Queue (VCMDQ)
 */
enum iommu_hw_queue_type {
	IOMMU_HW_QUEUE_TYPE_DEFAULT = 0,
	/*
	 * TEGRA241_CMDQV requirements (otherwise, allocation will fail)
	 * - alloc starts from the lowest @index=0 in ascending order
	 * - destroy starts from the last allocated @index in descending order
	 * - @base_addr must be aligned to @length in bytes and mapped in IOAS
	 * - @length must be a power of 2, with a minimum 32 bytes and a maximum
	 *   2 ^ idr[1].CMDQS * 16 bytes (use GET_HW_INFO call to read idr[1]
	 *   from struct iommu_hw_info_arm_smmuv3)
	 * - suggest to back the queue memory with contiguous physical pages or
	 *   a single huge page with alignment of the queue size, and limit the
	 *   emulated vSMMU's IDR1.CMDQS to log2(huge page size / 16 bytes)
	 */
	IOMMU_HW_QUEUE_TYPE_TEGRA241_CMDQV = 1,
};

/**
 * struct iommu_hw_queue_alloc - ioctl(IOMMU_HW_QUEUE_ALLOC)
 * @size: sizeof(struct iommu_hw_queue_alloc)
 * @flags: Must be 0
 * @viommu_id: Virtual IOMMU ID to associate the HW queue with
 * @type: One of enum iommu_hw_queue_type
 * @index: The logical index to the HW queue per virtual IOMMU for a multi-queue
 *         model
 * @out_hw_queue_id: The ID of the new HW queue
 * @nesting_parent_iova: Base address of the queue memory in the guest physical
 *                       address space
 * @length: Length of the queue memory
 *
 * Allocate a HW queue object for a vIOMMU-specific HW-accelerated queue, which
 * allows HW to access a guest queue memory described using @nesting_parent_iova
 * and @length.
 *
 * A vIOMMU can allocate multiple queues, but it must use a different @index per
 * type to separate each allocation, e.g::
 *
 *     Type1 HW queue0, Type1 HW queue1, Type2 HW queue0, ...
 */
struct iommu_hw_queue_alloc {
	__u32 size;
	__u32 flags;
	__u32 viommu_id;
	__u32 type;
	__u32 index;
	__u32 out_hw_queue_id;
	__aligned_u64 nesting_parent_iova;
	__aligned_u64 length;
};
#define IOMMU_HW_QUEUE_ALLOC _IO(IOMMUFD_TYPE, IOMMUFD_CMD_HW_QUEUE_ALLOC)
#endif
