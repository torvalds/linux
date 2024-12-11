/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _UAPI_XE_DRM_H_
#define _UAPI_XE_DRM_H_

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Please note that modifications to all structs defined here are
 * subject to backwards-compatibility constraints.
 * Sections in this file are organized as follows:
 *   1. IOCTL definition
 *   2. Extension definition and helper structs
 *   3. IOCTL's Query structs in the order of the Query's entries.
 *   4. The rest of IOCTL structs in the order of IOCTL declaration.
 */

/**
 * DOC: Xe Device Block Diagram
 *
 * The diagram below represents a high-level simplification of a discrete
 * GPU supported by the Xe driver. It shows some device components which
 * are necessary to understand this API, as well as how their relations
 * to each other. This diagram does not represent real hardware::
 *
 *   ┌──────────────────────────────────────────────────────────────────┐
 *   │ ┌──────────────────────────────────────────────────┐ ┌─────────┐ │
 *   │ │        ┌───────────────────────┐   ┌─────┐       │ │ ┌─────┐ │ │
 *   │ │        │         VRAM0         ├───┤ ... │       │ │ │VRAM1│ │ │
 *   │ │        └───────────┬───────────┘   └─GT1─┘       │ │ └──┬──┘ │ │
 *   │ │ ┌──────────────────┴───────────────────────────┐ │ │ ┌──┴──┐ │ │
 *   │ │ │ ┌─────────────────────┐  ┌─────────────────┐ │ │ │ │     │ │ │
 *   │ │ │ │ ┌──┐ ┌──┐ ┌──┐ ┌──┐ │  │ ┌─────┐ ┌─────┐ │ │ │ │ │     │ │ │
 *   │ │ │ │ │EU│ │EU│ │EU│ │EU│ │  │ │RCS0 │ │BCS0 │ │ │ │ │ │     │ │ │
 *   │ │ │ │ └──┘ └──┘ └──┘ └──┘ │  │ └─────┘ └─────┘ │ │ │ │ │     │ │ │
 *   │ │ │ │ ┌──┐ ┌──┐ ┌──┐ ┌──┐ │  │ ┌─────┐ ┌─────┐ │ │ │ │ │     │ │ │
 *   │ │ │ │ │EU│ │EU│ │EU│ │EU│ │  │ │VCS0 │ │VCS1 │ │ │ │ │ │     │ │ │
 *   │ │ │ │ └──┘ └──┘ └──┘ └──┘ │  │ └─────┘ └─────┘ │ │ │ │ │     │ │ │
 *   │ │ │ │ ┌──┐ ┌──┐ ┌──┐ ┌──┐ │  │ ┌─────┐ ┌─────┐ │ │ │ │ │     │ │ │
 *   │ │ │ │ │EU│ │EU│ │EU│ │EU│ │  │ │VECS0│ │VECS1│ │ │ │ │ │ ... │ │ │
 *   │ │ │ │ └──┘ └──┘ └──┘ └──┘ │  │ └─────┘ └─────┘ │ │ │ │ │     │ │ │
 *   │ │ │ │ ┌──┐ ┌──┐ ┌──┐ ┌──┐ │  │ ┌─────┐ ┌─────┐ │ │ │ │ │     │ │ │
 *   │ │ │ │ │EU│ │EU│ │EU│ │EU│ │  │ │CCS0 │ │CCS1 │ │ │ │ │ │     │ │ │
 *   │ │ │ │ └──┘ └──┘ └──┘ └──┘ │  │ └─────┘ └─────┘ │ │ │ │ │     │ │ │
 *   │ │ │ └─────────DSS─────────┘  │ ┌─────┐ ┌─────┐ │ │ │ │ │     │ │ │
 *   │ │ │                          │ │CCS2 │ │CCS3 │ │ │ │ │ │     │ │ │
 *   │ │ │ ┌─────┐ ┌─────┐ ┌─────┐  │ └─────┘ └─────┘ │ │ │ │ │     │ │ │
 *   │ │ │ │ ... │ │ ... │ │ ... │  │                 │ │ │ │ │     │ │ │
 *   │ │ │ └─DSS─┘ └─DSS─┘ └─DSS─┘  └─────Engines─────┘ │ │ │ │     │ │ │
 *   │ │ └───────────────────────────GT0────────────────┘ │ │ └─GT2─┘ │ │
 *   │ └────────────────────────────Tile0─────────────────┘ └─ Tile1──┘ │
 *   └─────────────────────────────Device0───────┬──────────────────────┘
 *                                               │
 *                        ───────────────────────┴────────── PCI bus
 */

/**
 * DOC: Xe uAPI Overview
 *
 * This section aims to describe the Xe's IOCTL entries, its structs, and other
 * Xe related uAPI such as uevents and PMU (Platform Monitoring Unit) related
 * entries and usage.
 *
 * List of supported IOCTLs:
 *  - &DRM_IOCTL_XE_DEVICE_QUERY
 *  - &DRM_IOCTL_XE_GEM_CREATE
 *  - &DRM_IOCTL_XE_GEM_MMAP_OFFSET
 *  - &DRM_IOCTL_XE_VM_CREATE
 *  - &DRM_IOCTL_XE_VM_DESTROY
 *  - &DRM_IOCTL_XE_VM_BIND
 *  - &DRM_IOCTL_XE_EXEC_QUEUE_CREATE
 *  - &DRM_IOCTL_XE_EXEC_QUEUE_DESTROY
 *  - &DRM_IOCTL_XE_EXEC_QUEUE_GET_PROPERTY
 *  - &DRM_IOCTL_XE_EXEC
 *  - &DRM_IOCTL_XE_WAIT_USER_FENCE
 *  - &DRM_IOCTL_XE_OBSERVATION
 */

/*
 * xe specific ioctls.
 *
 * The device specific ioctl range is [DRM_COMMAND_BASE, DRM_COMMAND_END) ie
 * [0x40, 0xa0) (a0 is excluded). The numbers below are defined as offset
 * against DRM_COMMAND_BASE and should be between [0x0, 0x60).
 */
#define DRM_XE_DEVICE_QUERY		0x00
#define DRM_XE_GEM_CREATE		0x01
#define DRM_XE_GEM_MMAP_OFFSET		0x02
#define DRM_XE_VM_CREATE		0x03
#define DRM_XE_VM_DESTROY		0x04
#define DRM_XE_VM_BIND			0x05
#define DRM_XE_EXEC_QUEUE_CREATE	0x06
#define DRM_XE_EXEC_QUEUE_DESTROY	0x07
#define DRM_XE_EXEC_QUEUE_GET_PROPERTY	0x08
#define DRM_XE_EXEC			0x09
#define DRM_XE_WAIT_USER_FENCE		0x0a
#define DRM_XE_OBSERVATION		0x0b

/* Must be kept compact -- no holes */

#define DRM_IOCTL_XE_DEVICE_QUERY		DRM_IOWR(DRM_COMMAND_BASE + DRM_XE_DEVICE_QUERY, struct drm_xe_device_query)
#define DRM_IOCTL_XE_GEM_CREATE			DRM_IOWR(DRM_COMMAND_BASE + DRM_XE_GEM_CREATE, struct drm_xe_gem_create)
#define DRM_IOCTL_XE_GEM_MMAP_OFFSET		DRM_IOWR(DRM_COMMAND_BASE + DRM_XE_GEM_MMAP_OFFSET, struct drm_xe_gem_mmap_offset)
#define DRM_IOCTL_XE_VM_CREATE			DRM_IOWR(DRM_COMMAND_BASE + DRM_XE_VM_CREATE, struct drm_xe_vm_create)
#define DRM_IOCTL_XE_VM_DESTROY			DRM_IOW(DRM_COMMAND_BASE + DRM_XE_VM_DESTROY, struct drm_xe_vm_destroy)
#define DRM_IOCTL_XE_VM_BIND			DRM_IOW(DRM_COMMAND_BASE + DRM_XE_VM_BIND, struct drm_xe_vm_bind)
#define DRM_IOCTL_XE_EXEC_QUEUE_CREATE		DRM_IOWR(DRM_COMMAND_BASE + DRM_XE_EXEC_QUEUE_CREATE, struct drm_xe_exec_queue_create)
#define DRM_IOCTL_XE_EXEC_QUEUE_DESTROY		DRM_IOW(DRM_COMMAND_BASE + DRM_XE_EXEC_QUEUE_DESTROY, struct drm_xe_exec_queue_destroy)
#define DRM_IOCTL_XE_EXEC_QUEUE_GET_PROPERTY	DRM_IOWR(DRM_COMMAND_BASE + DRM_XE_EXEC_QUEUE_GET_PROPERTY, struct drm_xe_exec_queue_get_property)
#define DRM_IOCTL_XE_EXEC			DRM_IOW(DRM_COMMAND_BASE + DRM_XE_EXEC, struct drm_xe_exec)
#define DRM_IOCTL_XE_WAIT_USER_FENCE		DRM_IOWR(DRM_COMMAND_BASE + DRM_XE_WAIT_USER_FENCE, struct drm_xe_wait_user_fence)
#define DRM_IOCTL_XE_OBSERVATION		DRM_IOW(DRM_COMMAND_BASE + DRM_XE_OBSERVATION, struct drm_xe_observation_param)

/**
 * DOC: Xe IOCTL Extensions
 *
 * Before detailing the IOCTLs and its structs, it is important to highlight
 * that every IOCTL in Xe is extensible.
 *
 * Many interfaces need to grow over time. In most cases we can simply
 * extend the struct and have userspace pass in more data. Another option,
 * as demonstrated by Vulkan's approach to providing extensions for forward
 * and backward compatibility, is to use a list of optional structs to
 * provide those extra details.
 *
 * The key advantage to using an extension chain is that it allows us to
 * redefine the interface more easily than an ever growing struct of
 * increasing complexity, and for large parts of that interface to be
 * entirely optional. The downside is more pointer chasing; chasing across
 * the __user boundary with pointers encapsulated inside u64.
 *
 * Example chaining:
 *
 * .. code-block:: C
 *
 *	struct drm_xe_user_extension ext3 {
 *		.next_extension = 0, // end
 *		.name = ...,
 *	};
 *	struct drm_xe_user_extension ext2 {
 *		.next_extension = (uintptr_t)&ext3,
 *		.name = ...,
 *	};
 *	struct drm_xe_user_extension ext1 {
 *		.next_extension = (uintptr_t)&ext2,
 *		.name = ...,
 *	};
 *
 * Typically the struct drm_xe_user_extension would be embedded in some uAPI
 * struct, and in this case we would feed it the head of the chain(i.e ext1),
 * which would then apply all of the above extensions.
*/

/**
 * struct drm_xe_user_extension - Base class for defining a chain of extensions
 */
struct drm_xe_user_extension {
	/**
	 * @next_extension:
	 *
	 * Pointer to the next struct drm_xe_user_extension, or zero if the end.
	 */
	__u64 next_extension;

	/**
	 * @name: Name of the extension.
	 *
	 * Note that the name here is just some integer.
	 *
	 * Also note that the name space for this is not global for the whole
	 * driver, but rather its scope/meaning is limited to the specific piece
	 * of uAPI which has embedded the struct drm_xe_user_extension.
	 */
	__u32 name;

	/**
	 * @pad: MBZ
	 *
	 * All undefined bits must be zero.
	 */
	__u32 pad;
};

/**
 * struct drm_xe_ext_set_property - Generic set property extension
 *
 * A generic struct that allows any of the Xe's IOCTL to be extended
 * with a set_property operation.
 */
struct drm_xe_ext_set_property {
	/** @base: base user extension */
	struct drm_xe_user_extension base;

	/** @property: property to set */
	__u32 property;

	/** @pad: MBZ */
	__u32 pad;

	/** @value: property value */
	__u64 value;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * struct drm_xe_engine_class_instance - instance of an engine class
 *
 * It is returned as part of the @drm_xe_engine, but it also is used as
 * the input of engine selection for both @drm_xe_exec_queue_create and
 * @drm_xe_query_engine_cycles
 *
 * The @engine_class can be:
 *  - %DRM_XE_ENGINE_CLASS_RENDER
 *  - %DRM_XE_ENGINE_CLASS_COPY
 *  - %DRM_XE_ENGINE_CLASS_VIDEO_DECODE
 *  - %DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE
 *  - %DRM_XE_ENGINE_CLASS_COMPUTE
 *  - %DRM_XE_ENGINE_CLASS_VM_BIND - Kernel only classes (not actual
 *    hardware engine class). Used for creating ordered queues of VM
 *    bind operations.
 */
struct drm_xe_engine_class_instance {
#define DRM_XE_ENGINE_CLASS_RENDER		0
#define DRM_XE_ENGINE_CLASS_COPY		1
#define DRM_XE_ENGINE_CLASS_VIDEO_DECODE	2
#define DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE	3
#define DRM_XE_ENGINE_CLASS_COMPUTE		4
#define DRM_XE_ENGINE_CLASS_VM_BIND		5
	/** @engine_class: engine class id */
	__u16 engine_class;
	/** @engine_instance: engine instance id */
	__u16 engine_instance;
	/** @gt_id: Unique ID of this GT within the PCI Device */
	__u16 gt_id;
	/** @pad: MBZ */
	__u16 pad;
};

/**
 * struct drm_xe_engine - describe hardware engine
 */
struct drm_xe_engine {
	/** @instance: The @drm_xe_engine_class_instance */
	struct drm_xe_engine_class_instance instance;

	/** @reserved: Reserved */
	__u64 reserved[3];
};

/**
 * struct drm_xe_query_engines - describe engines
 *
 * If a query is made with a struct @drm_xe_device_query where .query
 * is equal to %DRM_XE_DEVICE_QUERY_ENGINES, then the reply uses an array of
 * struct @drm_xe_query_engines in .data.
 */
struct drm_xe_query_engines {
	/** @num_engines: number of engines returned in @engines */
	__u32 num_engines;
	/** @pad: MBZ */
	__u32 pad;
	/** @engines: The returned engines for this device */
	struct drm_xe_engine engines[];
};

/**
 * enum drm_xe_memory_class - Supported memory classes.
 */
enum drm_xe_memory_class {
	/** @DRM_XE_MEM_REGION_CLASS_SYSMEM: Represents system memory. */
	DRM_XE_MEM_REGION_CLASS_SYSMEM = 0,
	/**
	 * @DRM_XE_MEM_REGION_CLASS_VRAM: On discrete platforms, this
	 * represents the memory that is local to the device, which we
	 * call VRAM. Not valid on integrated platforms.
	 */
	DRM_XE_MEM_REGION_CLASS_VRAM
};

/**
 * struct drm_xe_mem_region - Describes some region as known to
 * the driver.
 */
struct drm_xe_mem_region {
	/**
	 * @mem_class: The memory class describing this region.
	 *
	 * See enum drm_xe_memory_class for supported values.
	 */
	__u16 mem_class;
	/**
	 * @instance: The unique ID for this region, which serves as the
	 * index in the placement bitmask used as argument for
	 * &DRM_IOCTL_XE_GEM_CREATE
	 */
	__u16 instance;
	/**
	 * @min_page_size: Min page-size in bytes for this region.
	 *
	 * When the kernel allocates memory for this region, the
	 * underlying pages will be at least @min_page_size in size.
	 * Buffer objects with an allowable placement in this region must be
	 * created with a size aligned to this value.
	 * GPU virtual address mappings of (parts of) buffer objects that
	 * may be placed in this region must also have their GPU virtual
	 * address and range aligned to this value.
	 * Affected IOCTLS will return %-EINVAL if alignment restrictions are
	 * not met.
	 */
	__u32 min_page_size;
	/**
	 * @total_size: The usable size in bytes for this region.
	 */
	__u64 total_size;
	/**
	 * @used: Estimate of the memory used in bytes for this region.
	 *
	 * Requires CAP_PERFMON or CAP_SYS_ADMIN to get reliable
	 * accounting.  Without this the value here will always equal
	 * zero.
	 */
	__u64 used;
	/**
	 * @cpu_visible_size: How much of this region can be CPU
	 * accessed, in bytes.
	 *
	 * This will always be <= @total_size, and the remainder (if
	 * any) will not be CPU accessible. If the CPU accessible part
	 * is smaller than @total_size then this is referred to as a
	 * small BAR system.
	 *
	 * On systems without small BAR (full BAR), the probed_size will
	 * always equal the @total_size, since all of it will be CPU
	 * accessible.
	 *
	 * Note this is only tracked for DRM_XE_MEM_REGION_CLASS_VRAM
	 * regions (for other types the value here will always equal
	 * zero).
	 */
	__u64 cpu_visible_size;
	/**
	 * @cpu_visible_used: Estimate of CPU visible memory used, in
	 * bytes.
	 *
	 * Requires CAP_PERFMON or CAP_SYS_ADMIN to get reliable
	 * accounting. Without this the value here will always equal
	 * zero.  Note this is only currently tracked for
	 * DRM_XE_MEM_REGION_CLASS_VRAM regions (for other types the value
	 * here will always be zero).
	 */
	__u64 cpu_visible_used;
	/** @reserved: Reserved */
	__u64 reserved[6];
};

/**
 * struct drm_xe_query_mem_regions - describe memory regions
 *
 * If a query is made with a struct drm_xe_device_query where .query
 * is equal to DRM_XE_DEVICE_QUERY_MEM_REGIONS, then the reply uses
 * struct drm_xe_query_mem_regions in .data.
 */
struct drm_xe_query_mem_regions {
	/** @num_mem_regions: number of memory regions returned in @mem_regions */
	__u32 num_mem_regions;
	/** @pad: MBZ */
	__u32 pad;
	/** @mem_regions: The returned memory regions for this device */
	struct drm_xe_mem_region mem_regions[];
};

/**
 * struct drm_xe_query_config - describe the device configuration
 *
 * If a query is made with a struct drm_xe_device_query where .query
 * is equal to DRM_XE_DEVICE_QUERY_CONFIG, then the reply uses
 * struct drm_xe_query_config in .data.
 *
 * The index in @info can be:
 *  - %DRM_XE_QUERY_CONFIG_REV_AND_DEVICE_ID - Device ID (lower 16 bits)
 *    and the device revision (next 8 bits)
 *  - %DRM_XE_QUERY_CONFIG_FLAGS - Flags describing the device
 *    configuration, see list below
 *
 *    - %DRM_XE_QUERY_CONFIG_FLAG_HAS_VRAM - Flag is set if the device
 *      has usable VRAM
 *  - %DRM_XE_QUERY_CONFIG_MIN_ALIGNMENT - Minimal memory alignment
 *    required by this device, typically SZ_4K or SZ_64K
 *  - %DRM_XE_QUERY_CONFIG_VA_BITS - Maximum bits of a virtual address
 *  - %DRM_XE_QUERY_CONFIG_MAX_EXEC_QUEUE_PRIORITY - Value of the highest
 *    available exec queue priority
 */
struct drm_xe_query_config {
	/** @num_params: number of parameters returned in info */
	__u32 num_params;

	/** @pad: MBZ */
	__u32 pad;

#define DRM_XE_QUERY_CONFIG_REV_AND_DEVICE_ID	0
#define DRM_XE_QUERY_CONFIG_FLAGS			1
	#define DRM_XE_QUERY_CONFIG_FLAG_HAS_VRAM	(1 << 0)
#define DRM_XE_QUERY_CONFIG_MIN_ALIGNMENT		2
#define DRM_XE_QUERY_CONFIG_VA_BITS			3
#define DRM_XE_QUERY_CONFIG_MAX_EXEC_QUEUE_PRIORITY	4
	/** @info: array of elements containing the config info */
	__u64 info[];
};

/**
 * struct drm_xe_gt - describe an individual GT.
 *
 * To be used with drm_xe_query_gt_list, which will return a list with all the
 * existing GT individual descriptions.
 * Graphics Technology (GT) is a subset of a GPU/tile that is responsible for
 * implementing graphics and/or media operations.
 *
 * The index in @type can be:
 *  - %DRM_XE_QUERY_GT_TYPE_MAIN
 *  - %DRM_XE_QUERY_GT_TYPE_MEDIA
 */
struct drm_xe_gt {
#define DRM_XE_QUERY_GT_TYPE_MAIN		0
#define DRM_XE_QUERY_GT_TYPE_MEDIA		1
	/** @type: GT type: Main or Media */
	__u16 type;
	/** @tile_id: Tile ID where this GT lives (Information only) */
	__u16 tile_id;
	/** @gt_id: Unique ID of this GT within the PCI Device */
	__u16 gt_id;
	/** @pad: MBZ */
	__u16 pad[3];
	/** @reference_clock: A clock frequency for timestamp */
	__u32 reference_clock;
	/**
	 * @near_mem_regions: Bit mask of instances from
	 * drm_xe_query_mem_regions that are nearest to the current engines
	 * of this GT.
	 * Each index in this mask refers directly to the struct
	 * drm_xe_query_mem_regions' instance, no assumptions should
	 * be made about order. The type of each region is described
	 * by struct drm_xe_query_mem_regions' mem_class.
	 */
	__u64 near_mem_regions;
	/**
	 * @far_mem_regions: Bit mask of instances from
	 * drm_xe_query_mem_regions that are far from the engines of this GT.
	 * In general, they have extra indirections when compared to the
	 * @near_mem_regions. For a discrete device this could mean system
	 * memory and memory living in a different tile.
	 * Each index in this mask refers directly to the struct
	 * drm_xe_query_mem_regions' instance, no assumptions should
	 * be made about order. The type of each region is described
	 * by struct drm_xe_query_mem_regions' mem_class.
	 */
	__u64 far_mem_regions;
	/** @ip_ver_major: Graphics/media IP major version on GMD_ID platforms */
	__u16 ip_ver_major;
	/** @ip_ver_minor: Graphics/media IP minor version on GMD_ID platforms */
	__u16 ip_ver_minor;
	/** @ip_ver_rev: Graphics/media IP revision version on GMD_ID platforms */
	__u16 ip_ver_rev;
	/** @pad2: MBZ */
	__u16 pad2;
	/** @reserved: Reserved */
	__u64 reserved[7];
};

/**
 * struct drm_xe_query_gt_list - A list with GT description items.
 *
 * If a query is made with a struct drm_xe_device_query where .query
 * is equal to DRM_XE_DEVICE_QUERY_GT_LIST, then the reply uses struct
 * drm_xe_query_gt_list in .data.
 */
struct drm_xe_query_gt_list {
	/** @num_gt: number of GT items returned in gt_list */
	__u32 num_gt;
	/** @pad: MBZ */
	__u32 pad;
	/** @gt_list: The GT list returned for this device */
	struct drm_xe_gt gt_list[];
};

/**
 * struct drm_xe_query_topology_mask - describe the topology mask of a GT
 *
 * This is the hardware topology which reflects the internal physical
 * structure of the GPU.
 *
 * If a query is made with a struct drm_xe_device_query where .query
 * is equal to DRM_XE_DEVICE_QUERY_GT_TOPOLOGY, then the reply uses
 * struct drm_xe_query_topology_mask in .data.
 *
 * The @type can be:
 *  - %DRM_XE_TOPO_DSS_GEOMETRY - To query the mask of Dual Sub Slices
 *    (DSS) available for geometry operations. For example a query response
 *    containing the following in mask:
 *    ``DSS_GEOMETRY    ff ff ff ff 00 00 00 00``
 *    means 32 DSS are available for geometry.
 *  - %DRM_XE_TOPO_DSS_COMPUTE - To query the mask of Dual Sub Slices
 *    (DSS) available for compute operations. For example a query response
 *    containing the following in mask:
 *    ``DSS_COMPUTE    ff ff ff ff 00 00 00 00``
 *    means 32 DSS are available for compute.
 *  - %DRM_XE_TOPO_L3_BANK - To query the mask of enabled L3 banks.  This type
 *    may be omitted if the driver is unable to query the mask from the
 *    hardware.
 *  - %DRM_XE_TOPO_EU_PER_DSS - To query the mask of Execution Units (EU)
 *    available per Dual Sub Slices (DSS). For example a query response
 *    containing the following in mask:
 *    ``EU_PER_DSS    ff ff 00 00 00 00 00 00``
 *    means each DSS has 16 SIMD8 EUs. This type may be omitted if device
 *    doesn't have SIMD8 EUs.
 *  - %DRM_XE_TOPO_SIMD16_EU_PER_DSS - To query the mask of SIMD16 Execution
 *    Units (EU) available per Dual Sub Slices (DSS). For example a query
 *    response containing the following in mask:
 *    ``SIMD16_EU_PER_DSS    ff ff 00 00 00 00 00 00``
 *    means each DSS has 16 SIMD16 EUs. This type may be omitted if device
 *    doesn't have SIMD16 EUs.
 */
struct drm_xe_query_topology_mask {
	/** @gt_id: GT ID the mask is associated with */
	__u16 gt_id;

#define DRM_XE_TOPO_DSS_GEOMETRY	1
#define DRM_XE_TOPO_DSS_COMPUTE		2
#define DRM_XE_TOPO_L3_BANK		3
#define DRM_XE_TOPO_EU_PER_DSS		4
#define DRM_XE_TOPO_SIMD16_EU_PER_DSS	5
	/** @type: type of mask */
	__u16 type;

	/** @num_bytes: number of bytes in requested mask */
	__u32 num_bytes;

	/** @mask: little-endian mask of @num_bytes */
	__u8 mask[];
};

/**
 * struct drm_xe_query_engine_cycles - correlate CPU and GPU timestamps
 *
 * If a query is made with a struct drm_xe_device_query where .query is equal to
 * DRM_XE_DEVICE_QUERY_ENGINE_CYCLES, then the reply uses struct drm_xe_query_engine_cycles
 * in .data. struct drm_xe_query_engine_cycles is allocated by the user and
 * .data points to this allocated structure.
 *
 * The query returns the engine cycles, which along with GT's @reference_clock,
 * can be used to calculate the engine timestamp. In addition the
 * query returns a set of cpu timestamps that indicate when the command
 * streamer cycle count was captured.
 */
struct drm_xe_query_engine_cycles {
	/**
	 * @eci: This is input by the user and is the engine for which command
	 * streamer cycles is queried.
	 */
	struct drm_xe_engine_class_instance eci;

	/**
	 * @clockid: This is input by the user and is the reference clock id for
	 * CPU timestamp. For definition, see clock_gettime(2) and
	 * perf_event_open(2). Supported clock ids are CLOCK_MONOTONIC,
	 * CLOCK_MONOTONIC_RAW, CLOCK_REALTIME, CLOCK_BOOTTIME, CLOCK_TAI.
	 */
	__s32 clockid;

	/** @width: Width of the engine cycle counter in bits. */
	__u32 width;

	/**
	 * @engine_cycles: Engine cycles as read from its register
	 * at 0x358 offset.
	 */
	__u64 engine_cycles;

	/**
	 * @cpu_timestamp: CPU timestamp in ns. The timestamp is captured before
	 * reading the engine_cycles register using the reference clockid set by the
	 * user.
	 */
	__u64 cpu_timestamp;

	/**
	 * @cpu_delta: Time delta in ns captured around reading the lower dword
	 * of the engine_cycles register.
	 */
	__u64 cpu_delta;
};

/**
 * struct drm_xe_query_uc_fw_version - query a micro-controller firmware version
 *
 * Given a uc_type this will return the branch, major, minor and patch version
 * of the micro-controller firmware.
 */
struct drm_xe_query_uc_fw_version {
	/** @uc_type: The micro-controller type to query firmware version */
#define XE_QUERY_UC_TYPE_GUC_SUBMISSION 0
#define XE_QUERY_UC_TYPE_HUC 1
	__u16 uc_type;

	/** @pad: MBZ */
	__u16 pad;

	/** @branch_ver: branch uc fw version */
	__u32 branch_ver;
	/** @major_ver: major uc fw version */
	__u32 major_ver;
	/** @minor_ver: minor uc fw version */
	__u32 minor_ver;
	/** @patch_ver: patch uc fw version */
	__u32 patch_ver;

	/** @pad2: MBZ */
	__u32 pad2;

	/** @reserved: Reserved */
	__u64 reserved;
};

/**
 * struct drm_xe_device_query - Input of &DRM_IOCTL_XE_DEVICE_QUERY - main
 * structure to query device information
 *
 * The user selects the type of data to query among DRM_XE_DEVICE_QUERY_*
 * and sets the value in the query member. This determines the type of
 * the structure provided by the driver in data, among struct drm_xe_query_*.
 *
 * The @query can be:
 *  - %DRM_XE_DEVICE_QUERY_ENGINES
 *  - %DRM_XE_DEVICE_QUERY_MEM_REGIONS
 *  - %DRM_XE_DEVICE_QUERY_CONFIG
 *  - %DRM_XE_DEVICE_QUERY_GT_LIST
 *  - %DRM_XE_DEVICE_QUERY_HWCONFIG - Query type to retrieve the hardware
 *    configuration of the device such as information on slices, memory,
 *    caches, and so on. It is provided as a table of key / value
 *    attributes.
 *  - %DRM_XE_DEVICE_QUERY_GT_TOPOLOGY
 *  - %DRM_XE_DEVICE_QUERY_ENGINE_CYCLES
 *
 * If size is set to 0, the driver fills it with the required size for
 * the requested type of data to query. If size is equal to the required
 * size, the queried information is copied into data. If size is set to
 * a value different from 0 and different from the required size, the
 * IOCTL call returns -EINVAL.
 *
 * For example the following code snippet allows retrieving and printing
 * information about the device engines with DRM_XE_DEVICE_QUERY_ENGINES:
 *
 * .. code-block:: C
 *
 *     struct drm_xe_query_engines *engines;
 *     struct drm_xe_device_query query = {
 *         .extensions = 0,
 *         .query = DRM_XE_DEVICE_QUERY_ENGINES,
 *         .size = 0,
 *         .data = 0,
 *     };
 *     ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query);
 *     engines = malloc(query.size);
 *     query.data = (uintptr_t)engines;
 *     ioctl(fd, DRM_IOCTL_XE_DEVICE_QUERY, &query);
 *     for (int i = 0; i < engines->num_engines; i++) {
 *         printf("Engine %d: %s\n", i,
 *             engines->engines[i].instance.engine_class ==
 *                 DRM_XE_ENGINE_CLASS_RENDER ? "RENDER":
 *             engines->engines[i].instance.engine_class ==
 *                 DRM_XE_ENGINE_CLASS_COPY ? "COPY":
 *             engines->engines[i].instance.engine_class ==
 *                 DRM_XE_ENGINE_CLASS_VIDEO_DECODE ? "VIDEO_DECODE":
 *             engines->engines[i].instance.engine_class ==
 *                 DRM_XE_ENGINE_CLASS_VIDEO_ENHANCE ? "VIDEO_ENHANCE":
 *             engines->engines[i].instance.engine_class ==
 *                 DRM_XE_ENGINE_CLASS_COMPUTE ? "COMPUTE":
 *             "UNKNOWN");
 *     }
 *     free(engines);
 */
struct drm_xe_device_query {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

#define DRM_XE_DEVICE_QUERY_ENGINES		0
#define DRM_XE_DEVICE_QUERY_MEM_REGIONS		1
#define DRM_XE_DEVICE_QUERY_CONFIG		2
#define DRM_XE_DEVICE_QUERY_GT_LIST		3
#define DRM_XE_DEVICE_QUERY_HWCONFIG		4
#define DRM_XE_DEVICE_QUERY_GT_TOPOLOGY		5
#define DRM_XE_DEVICE_QUERY_ENGINE_CYCLES	6
#define DRM_XE_DEVICE_QUERY_UC_FW_VERSION	7
#define DRM_XE_DEVICE_QUERY_OA_UNITS		8
	/** @query: The type of data to query */
	__u32 query;

	/** @size: Size of the queried data */
	__u32 size;

	/** @data: Queried data is placed here */
	__u64 data;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * struct drm_xe_gem_create - Input of &DRM_IOCTL_XE_GEM_CREATE - A structure for
 * gem creation
 *
 * The @flags can be:
 *  - %DRM_XE_GEM_CREATE_FLAG_DEFER_BACKING
 *  - %DRM_XE_GEM_CREATE_FLAG_SCANOUT
 *  - %DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM - When using VRAM as a
 *    possible placement, ensure that the corresponding VRAM allocation
 *    will always use the CPU accessible part of VRAM. This is important
 *    for small-bar systems (on full-bar systems this gets turned into a
 *    noop).
 *    Note1: System memory can be used as an extra placement if the kernel
 *    should spill the allocation to system memory, if space can't be made
 *    available in the CPU accessible part of VRAM (giving the same
 *    behaviour as the i915 interface, see
 *    I915_GEM_CREATE_EXT_FLAG_NEEDS_CPU_ACCESS).
 *    Note2: For clear-color CCS surfaces the kernel needs to read the
 *    clear-color value stored in the buffer, and on discrete platforms we
 *    need to use VRAM for display surfaces, therefore the kernel requires
 *    setting this flag for such objects, otherwise an error is thrown on
 *    small-bar systems.
 *
 * @cpu_caching supports the following values:
 *  - %DRM_XE_GEM_CPU_CACHING_WB - Allocate the pages with write-back
 *    caching. On iGPU this can't be used for scanout surfaces. Currently
 *    not allowed for objects placed in VRAM.
 *  - %DRM_XE_GEM_CPU_CACHING_WC - Allocate the pages as write-combined. This
 *    is uncached. Scanout surfaces should likely use this. All objects
 *    that can be placed in VRAM must use this.
 */
struct drm_xe_gem_create {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/**
	 * @size: Size of the object to be created, must match region
	 * (system or vram) minimum alignment (&min_page_size).
	 */
	__u64 size;

	/**
	 * @placement: A mask of memory instances of where BO can be placed.
	 * Each index in this mask refers directly to the struct
	 * drm_xe_query_mem_regions' instance, no assumptions should
	 * be made about order. The type of each region is described
	 * by struct drm_xe_query_mem_regions' mem_class.
	 */
	__u32 placement;

#define DRM_XE_GEM_CREATE_FLAG_DEFER_BACKING		(1 << 0)
#define DRM_XE_GEM_CREATE_FLAG_SCANOUT			(1 << 1)
#define DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM	(1 << 2)
	/**
	 * @flags: Flags, currently a mask of memory instances of where BO can
	 * be placed
	 */
	__u32 flags;

	/**
	 * @vm_id: Attached VM, if any
	 *
	 * If a VM is specified, this BO must:
	 *
	 *  1. Only ever be bound to that VM.
	 *  2. Cannot be exported as a PRIME fd.
	 */
	__u32 vm_id;

	/**
	 * @handle: Returned handle for the object.
	 *
	 * Object handles are nonzero.
	 */
	__u32 handle;

#define DRM_XE_GEM_CPU_CACHING_WB                      1
#define DRM_XE_GEM_CPU_CACHING_WC                      2
	/**
	 * @cpu_caching: The CPU caching mode to select for this object. If
	 * mmaping the object the mode selected here will also be used. The
	 * exception is when mapping system memory (including data evicted
	 * to system) on discrete GPUs. The caching mode selected will
	 * then be overridden to DRM_XE_GEM_CPU_CACHING_WB, and coherency
	 * between GPU- and CPU is guaranteed. The caching mode of
	 * existing CPU-mappings will be updated transparently to
	 * user-space clients.
	 */
	__u16 cpu_caching;
	/** @pad: MBZ */
	__u16 pad[3];

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * struct drm_xe_gem_mmap_offset - Input of &DRM_IOCTL_XE_GEM_MMAP_OFFSET
 */
struct drm_xe_gem_mmap_offset {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @handle: Handle for the object being mapped. */
	__u32 handle;

	/** @flags: Must be zero */
	__u32 flags;

	/** @offset: The fake offset to use for subsequent mmap call */
	__u64 offset;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * struct drm_xe_vm_create - Input of &DRM_IOCTL_XE_VM_CREATE
 *
 * The @flags can be:
 *  - %DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE
 *  - %DRM_XE_VM_CREATE_FLAG_LR_MODE - An LR, or Long Running VM accepts
 *    exec submissions to its exec_queues that don't have an upper time
 *    limit on the job execution time. But exec submissions to these
 *    don't allow any of the flags DRM_XE_SYNC_FLAG_SYNCOBJ,
 *    DRM_XE_SYNC_FLAG_TIMELINE_SYNCOBJ, DRM_XE_SYNC_FLAG_DMA_BUF,
 *    used as out-syncobjs, that is, together with DRM_XE_SYNC_FLAG_SIGNAL.
 *    LR VMs can be created in recoverable page-fault mode using
 *    DRM_XE_VM_CREATE_FLAG_FAULT_MODE, if the device supports it.
 *    If that flag is omitted, the UMD can not rely on the slightly
 *    different per-VM overcommit semantics that are enabled by
 *    DRM_XE_VM_CREATE_FLAG_FAULT_MODE (see below), but KMD may
 *    still enable recoverable pagefaults if supported by the device.
 *  - %DRM_XE_VM_CREATE_FLAG_FAULT_MODE - Requires also
 *    DRM_XE_VM_CREATE_FLAG_LR_MODE. It allows memory to be allocated on
 *    demand when accessed, and also allows per-VM overcommit of memory.
 *    The xe driver internally uses recoverable pagefaults to implement
 *    this.
 */
struct drm_xe_vm_create {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

#define DRM_XE_VM_CREATE_FLAG_SCRATCH_PAGE	(1 << 0)
#define DRM_XE_VM_CREATE_FLAG_LR_MODE	        (1 << 1)
#define DRM_XE_VM_CREATE_FLAG_FAULT_MODE	(1 << 2)
	/** @flags: Flags */
	__u32 flags;

	/** @vm_id: Returned VM ID */
	__u32 vm_id;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * struct drm_xe_vm_destroy - Input of &DRM_IOCTL_XE_VM_DESTROY
 */
struct drm_xe_vm_destroy {
	/** @vm_id: VM ID */
	__u32 vm_id;

	/** @pad: MBZ */
	__u32 pad;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * struct drm_xe_vm_bind_op - run bind operations
 *
 * The @op can be:
 *  - %DRM_XE_VM_BIND_OP_MAP
 *  - %DRM_XE_VM_BIND_OP_UNMAP
 *  - %DRM_XE_VM_BIND_OP_MAP_USERPTR
 *  - %DRM_XE_VM_BIND_OP_UNMAP_ALL
 *  - %DRM_XE_VM_BIND_OP_PREFETCH
 *
 * and the @flags can be:
 *  - %DRM_XE_VM_BIND_FLAG_READONLY - Setup the page tables as read-only
 *    to ensure write protection
 *  - %DRM_XE_VM_BIND_FLAG_IMMEDIATE - On a faulting VM, do the
 *    MAP operation immediately rather than deferring the MAP to the page
 *    fault handler. This is implied on a non-faulting VM as there is no
 *    fault handler to defer to.
 *  - %DRM_XE_VM_BIND_FLAG_NULL - When the NULL flag is set, the page
 *    tables are setup with a special bit which indicates writes are
 *    dropped and all reads return zero. In the future, the NULL flags
 *    will only be valid for DRM_XE_VM_BIND_OP_MAP operations, the BO
 *    handle MBZ, and the BO offset MBZ. This flag is intended to
 *    implement VK sparse bindings.
 */
struct drm_xe_vm_bind_op {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/**
	 * @obj: GEM object to operate on, MBZ for MAP_USERPTR, MBZ for UNMAP
	 */
	__u32 obj;

	/**
	 * @pat_index: The platform defined @pat_index to use for this mapping.
	 * The index basically maps to some predefined memory attributes,
	 * including things like caching, coherency, compression etc.  The exact
	 * meaning of the pat_index is platform specific and defined in the
	 * Bspec and PRMs.  When the KMD sets up the binding the index here is
	 * encoded into the ppGTT PTE.
	 *
	 * For coherency the @pat_index needs to be at least 1way coherent when
	 * drm_xe_gem_create.cpu_caching is DRM_XE_GEM_CPU_CACHING_WB. The KMD
	 * will extract the coherency mode from the @pat_index and reject if
	 * there is a mismatch (see note below for pre-MTL platforms).
	 *
	 * Note: On pre-MTL platforms there is only a caching mode and no
	 * explicit coherency mode, but on such hardware there is always a
	 * shared-LLC (or is dgpu) so all GT memory accesses are coherent with
	 * CPU caches even with the caching mode set as uncached.  It's only the
	 * display engine that is incoherent (on dgpu it must be in VRAM which
	 * is always mapped as WC on the CPU). However to keep the uapi somewhat
	 * consistent with newer platforms the KMD groups the different cache
	 * levels into the following coherency buckets on all pre-MTL platforms:
	 *
	 *	ppGTT UC -> COH_NONE
	 *	ppGTT WC -> COH_NONE
	 *	ppGTT WT -> COH_NONE
	 *	ppGTT WB -> COH_AT_LEAST_1WAY
	 *
	 * In practice UC/WC/WT should only ever used for scanout surfaces on
	 * such platforms (or perhaps in general for dma-buf if shared with
	 * another device) since it is only the display engine that is actually
	 * incoherent.  Everything else should typically use WB given that we
	 * have a shared-LLC.  On MTL+ this completely changes and the HW
	 * defines the coherency mode as part of the @pat_index, where
	 * incoherent GT access is possible.
	 *
	 * Note: For userptr and externally imported dma-buf the kernel expects
	 * either 1WAY or 2WAY for the @pat_index.
	 *
	 * For DRM_XE_VM_BIND_FLAG_NULL bindings there are no KMD restrictions
	 * on the @pat_index. For such mappings there is no actual memory being
	 * mapped (the address in the PTE is invalid), so the various PAT memory
	 * attributes likely do not apply.  Simply leaving as zero is one
	 * option (still a valid pat_index).
	 */
	__u16 pat_index;

	/** @pad: MBZ */
	__u16 pad;

	union {
		/**
		 * @obj_offset: Offset into the object, MBZ for CLEAR_RANGE,
		 * ignored for unbind
		 */
		__u64 obj_offset;

		/** @userptr: user pointer to bind on */
		__u64 userptr;
	};

	/**
	 * @range: Number of bytes from the object to bind to addr, MBZ for UNMAP_ALL
	 */
	__u64 range;

	/** @addr: Address to operate on, MBZ for UNMAP_ALL */
	__u64 addr;

#define DRM_XE_VM_BIND_OP_MAP		0x0
#define DRM_XE_VM_BIND_OP_UNMAP		0x1
#define DRM_XE_VM_BIND_OP_MAP_USERPTR	0x2
#define DRM_XE_VM_BIND_OP_UNMAP_ALL	0x3
#define DRM_XE_VM_BIND_OP_PREFETCH	0x4
	/** @op: Bind operation to perform */
	__u32 op;

#define DRM_XE_VM_BIND_FLAG_READONLY	(1 << 0)
#define DRM_XE_VM_BIND_FLAG_IMMEDIATE	(1 << 1)
#define DRM_XE_VM_BIND_FLAG_NULL	(1 << 2)
#define DRM_XE_VM_BIND_FLAG_DUMPABLE	(1 << 3)
	/** @flags: Bind flags */
	__u32 flags;

	/**
	 * @prefetch_mem_region_instance: Memory region to prefetch VMA to.
	 * It is a region instance, not a mask.
	 * To be used only with %DRM_XE_VM_BIND_OP_PREFETCH operation.
	 */
	__u32 prefetch_mem_region_instance;

	/** @pad2: MBZ */
	__u32 pad2;

	/** @reserved: Reserved */
	__u64 reserved[3];
};

/**
 * struct drm_xe_vm_bind - Input of &DRM_IOCTL_XE_VM_BIND
 *
 * Below is an example of a minimal use of @drm_xe_vm_bind to
 * asynchronously bind the buffer `data` at address `BIND_ADDRESS` to
 * illustrate `userptr`. It can be synchronized by using the example
 * provided for @drm_xe_sync.
 *
 * .. code-block:: C
 *
 *     data = aligned_alloc(ALIGNMENT, BO_SIZE);
 *     struct drm_xe_vm_bind bind = {
 *         .vm_id = vm,
 *         .num_binds = 1,
 *         .bind.obj = 0,
 *         .bind.obj_offset = to_user_pointer(data),
 *         .bind.range = BO_SIZE,
 *         .bind.addr = BIND_ADDRESS,
 *         .bind.op = DRM_XE_VM_BIND_OP_MAP_USERPTR,
 *         .bind.flags = 0,
 *         .num_syncs = 1,
 *         .syncs = &sync,
 *         .exec_queue_id = 0,
 *     };
 *     ioctl(fd, DRM_IOCTL_XE_VM_BIND, &bind);
 *
 */
struct drm_xe_vm_bind {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @vm_id: The ID of the VM to bind to */
	__u32 vm_id;

	/**
	 * @exec_queue_id: exec_queue_id, must be of class DRM_XE_ENGINE_CLASS_VM_BIND
	 * and exec queue must have same vm_id. If zero, the default VM bind engine
	 * is used.
	 */
	__u32 exec_queue_id;

	/** @pad: MBZ */
	__u32 pad;

	/** @num_binds: number of binds in this IOCTL */
	__u32 num_binds;

	union {
		/** @bind: used if num_binds == 1 */
		struct drm_xe_vm_bind_op bind;

		/**
		 * @vector_of_binds: userptr to array of struct
		 * drm_xe_vm_bind_op if num_binds > 1
		 */
		__u64 vector_of_binds;
	};

	/** @pad2: MBZ */
	__u32 pad2;

	/** @num_syncs: amount of syncs to wait on */
	__u32 num_syncs;

	/** @syncs: pointer to struct drm_xe_sync array */
	__u64 syncs;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * struct drm_xe_exec_queue_create - Input of &DRM_IOCTL_XE_EXEC_QUEUE_CREATE
 *
 * The example below shows how to use @drm_xe_exec_queue_create to create
 * a simple exec_queue (no parallel submission) of class
 * &DRM_XE_ENGINE_CLASS_RENDER.
 *
 * .. code-block:: C
 *
 *     struct drm_xe_engine_class_instance instance = {
 *         .engine_class = DRM_XE_ENGINE_CLASS_RENDER,
 *     };
 *     struct drm_xe_exec_queue_create exec_queue_create = {
 *          .extensions = 0,
 *          .vm_id = vm,
 *          .num_bb_per_exec = 1,
 *          .num_eng_per_bb = 1,
 *          .instances = to_user_pointer(&instance),
 *     };
 *     ioctl(fd, DRM_IOCTL_XE_EXEC_QUEUE_CREATE, &exec_queue_create);
 *
 */
struct drm_xe_exec_queue_create {
#define DRM_XE_EXEC_QUEUE_EXTENSION_SET_PROPERTY		0
#define   DRM_XE_EXEC_QUEUE_SET_PROPERTY_PRIORITY		0
#define   DRM_XE_EXEC_QUEUE_SET_PROPERTY_TIMESLICE		1

	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @width: submission width (number BB per exec) for this exec queue */
	__u16 width;

	/** @num_placements: number of valid placements for this exec queue */
	__u16 num_placements;

	/** @vm_id: VM to use for this exec queue */
	__u32 vm_id;

	/** @flags: MBZ */
	__u32 flags;

	/** @exec_queue_id: Returned exec queue ID */
	__u32 exec_queue_id;

	/**
	 * @instances: user pointer to a 2-d array of struct
	 * drm_xe_engine_class_instance
	 *
	 * length = width (i) * num_placements (j)
	 * index = j + i * width
	 */
	__u64 instances;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * struct drm_xe_exec_queue_destroy - Input of &DRM_IOCTL_XE_EXEC_QUEUE_DESTROY
 */
struct drm_xe_exec_queue_destroy {
	/** @exec_queue_id: Exec queue ID */
	__u32 exec_queue_id;

	/** @pad: MBZ */
	__u32 pad;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * struct drm_xe_exec_queue_get_property - Input of &DRM_IOCTL_XE_EXEC_QUEUE_GET_PROPERTY
 *
 * The @property can be:
 *  - %DRM_XE_EXEC_QUEUE_GET_PROPERTY_BAN
 */
struct drm_xe_exec_queue_get_property {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @exec_queue_id: Exec queue ID */
	__u32 exec_queue_id;

#define DRM_XE_EXEC_QUEUE_GET_PROPERTY_BAN	0
	/** @property: property to get */
	__u32 property;

	/** @value: property value */
	__u64 value;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * struct drm_xe_sync - sync object
 *
 * The @type can be:
 *  - %DRM_XE_SYNC_TYPE_SYNCOBJ
 *  - %DRM_XE_SYNC_TYPE_TIMELINE_SYNCOBJ
 *  - %DRM_XE_SYNC_TYPE_USER_FENCE
 *
 * and the @flags can be:
 *  - %DRM_XE_SYNC_FLAG_SIGNAL
 *
 * A minimal use of @drm_xe_sync looks like this:
 *
 * .. code-block:: C
 *
 *     struct drm_xe_sync sync = {
 *         .flags = DRM_XE_SYNC_FLAG_SIGNAL,
 *         .type = DRM_XE_SYNC_TYPE_SYNCOBJ,
 *     };
 *     struct drm_syncobj_create syncobj_create = { 0 };
 *     ioctl(fd, DRM_IOCTL_SYNCOBJ_CREATE, &syncobj_create);
 *     sync.handle = syncobj_create.handle;
 *         ...
 *         use of &sync in drm_xe_exec or drm_xe_vm_bind
 *         ...
 *     struct drm_syncobj_wait wait = {
 *         .handles = &sync.handle,
 *         .timeout_nsec = INT64_MAX,
 *         .count_handles = 1,
 *         .flags = 0,
 *         .first_signaled = 0,
 *         .pad = 0,
 *     };
 *     ioctl(fd, DRM_IOCTL_SYNCOBJ_WAIT, &wait);
 */
struct drm_xe_sync {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

#define DRM_XE_SYNC_TYPE_SYNCOBJ		0x0
#define DRM_XE_SYNC_TYPE_TIMELINE_SYNCOBJ	0x1
#define DRM_XE_SYNC_TYPE_USER_FENCE		0x2
	/** @type: Type of the this sync object */
	__u32 type;

#define DRM_XE_SYNC_FLAG_SIGNAL	(1 << 0)
	/** @flags: Sync Flags */
	__u32 flags;

	union {
		/** @handle: Handle for the object */
		__u32 handle;

		/**
		 * @addr: Address of user fence. When sync is passed in via exec
		 * IOCTL this is a GPU address in the VM. When sync passed in via
		 * VM bind IOCTL this is a user pointer. In either case, it is
		 * the users responsibility that this address is present and
		 * mapped when the user fence is signalled. Must be qword
		 * aligned.
		 */
		__u64 addr;
	};

	/**
	 * @timeline_value: Input for the timeline sync object. Needs to be
	 * different than 0 when used with %DRM_XE_SYNC_FLAG_TIMELINE_SYNCOBJ.
	 */
	__u64 timeline_value;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * struct drm_xe_exec - Input of &DRM_IOCTL_XE_EXEC
 *
 * This is an example to use @drm_xe_exec for execution of the object
 * at BIND_ADDRESS (see example in @drm_xe_vm_bind) by an exec_queue
 * (see example in @drm_xe_exec_queue_create). It can be synchronized
 * by using the example provided for @drm_xe_sync.
 *
 * .. code-block:: C
 *
 *     struct drm_xe_exec exec = {
 *         .exec_queue_id = exec_queue,
 *         .syncs = &sync,
 *         .num_syncs = 1,
 *         .address = BIND_ADDRESS,
 *         .num_batch_buffer = 1,
 *     };
 *     ioctl(fd, DRM_IOCTL_XE_EXEC, &exec);
 *
 */
struct drm_xe_exec {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @exec_queue_id: Exec queue ID for the batch buffer */
	__u32 exec_queue_id;

	/** @num_syncs: Amount of struct drm_xe_sync in array. */
	__u32 num_syncs;

	/** @syncs: Pointer to struct drm_xe_sync array. */
	__u64 syncs;

	/**
	 * @address: address of batch buffer if num_batch_buffer == 1 or an
	 * array of batch buffer addresses
	 */
	__u64 address;

	/**
	 * @num_batch_buffer: number of batch buffer in this exec, must match
	 * the width of the engine
	 */
	__u16 num_batch_buffer;

	/** @pad: MBZ */
	__u16 pad[3];

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * struct drm_xe_wait_user_fence - Input of &DRM_IOCTL_XE_WAIT_USER_FENCE
 *
 * Wait on user fence, XE will wake-up on every HW engine interrupt in the
 * instances list and check if user fence is complete::
 *
 *	(*addr & MASK) OP (VALUE & MASK)
 *
 * Returns to user on user fence completion or timeout.
 *
 * The @op can be:
 *  - %DRM_XE_UFENCE_WAIT_OP_EQ
 *  - %DRM_XE_UFENCE_WAIT_OP_NEQ
 *  - %DRM_XE_UFENCE_WAIT_OP_GT
 *  - %DRM_XE_UFENCE_WAIT_OP_GTE
 *  - %DRM_XE_UFENCE_WAIT_OP_LT
 *  - %DRM_XE_UFENCE_WAIT_OP_LTE
 *
 * and the @flags can be:
 *  - %DRM_XE_UFENCE_WAIT_FLAG_ABSTIME
 *  - %DRM_XE_UFENCE_WAIT_FLAG_SOFT_OP
 *
 * The @mask values can be for example:
 *  - 0xffu for u8
 *  - 0xffffu for u16
 *  - 0xffffffffu for u32
 *  - 0xffffffffffffffffu for u64
 */
struct drm_xe_wait_user_fence {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/**
	 * @addr: user pointer address to wait on, must qword aligned
	 */
	__u64 addr;

#define DRM_XE_UFENCE_WAIT_OP_EQ	0x0
#define DRM_XE_UFENCE_WAIT_OP_NEQ	0x1
#define DRM_XE_UFENCE_WAIT_OP_GT	0x2
#define DRM_XE_UFENCE_WAIT_OP_GTE	0x3
#define DRM_XE_UFENCE_WAIT_OP_LT	0x4
#define DRM_XE_UFENCE_WAIT_OP_LTE	0x5
	/** @op: wait operation (type of comparison) */
	__u16 op;

#define DRM_XE_UFENCE_WAIT_FLAG_ABSTIME	(1 << 0)
	/** @flags: wait flags */
	__u16 flags;

	/** @pad: MBZ */
	__u32 pad;

	/** @value: compare value */
	__u64 value;

	/** @mask: comparison mask */
	__u64 mask;

	/**
	 * @timeout: how long to wait before bailing, value in nanoseconds.
	 * Without DRM_XE_UFENCE_WAIT_FLAG_ABSTIME flag set (relative timeout)
	 * it contains timeout expressed in nanoseconds to wait (fence will
	 * expire at now() + timeout).
	 * When DRM_XE_UFENCE_WAIT_FLAG_ABSTIME flat is set (absolute timeout) wait
	 * will end at timeout (uses system MONOTONIC_CLOCK).
	 * Passing negative timeout leads to neverending wait.
	 *
	 * On relative timeout this value is updated with timeout left
	 * (for restarting the call in case of signal delivery).
	 * On absolute timeout this value stays intact (restarted call still
	 * expire at the same point of time).
	 */
	__s64 timeout;

	/** @exec_queue_id: exec_queue_id returned from xe_exec_queue_create_ioctl */
	__u32 exec_queue_id;

	/** @pad2: MBZ */
	__u32 pad2;

	/** @reserved: Reserved */
	__u64 reserved[2];
};

/**
 * enum drm_xe_observation_type - Observation stream types
 */
enum drm_xe_observation_type {
	/** @DRM_XE_OBSERVATION_TYPE_OA: OA observation stream type */
	DRM_XE_OBSERVATION_TYPE_OA,
};

/**
 * enum drm_xe_observation_op - Observation stream ops
 */
enum drm_xe_observation_op {
	/** @DRM_XE_OBSERVATION_OP_STREAM_OPEN: Open an observation stream */
	DRM_XE_OBSERVATION_OP_STREAM_OPEN,

	/** @DRM_XE_OBSERVATION_OP_ADD_CONFIG: Add observation stream config */
	DRM_XE_OBSERVATION_OP_ADD_CONFIG,

	/** @DRM_XE_OBSERVATION_OP_REMOVE_CONFIG: Remove observation stream config */
	DRM_XE_OBSERVATION_OP_REMOVE_CONFIG,
};

/**
 * struct drm_xe_observation_param - Input of &DRM_XE_OBSERVATION
 *
 * The observation layer enables multiplexing observation streams of
 * multiple types. The actual params for a particular stream operation are
 * supplied via the @param pointer (use __copy_from_user to get these
 * params).
 */
struct drm_xe_observation_param {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;
	/** @observation_type: observation stream type, of enum @drm_xe_observation_type */
	__u64 observation_type;
	/** @observation_op: observation stream op, of enum @drm_xe_observation_op */
	__u64 observation_op;
	/** @param: Pointer to actual stream params */
	__u64 param;
};

/**
 * enum drm_xe_observation_ioctls - Observation stream fd ioctl's
 *
 * Information exchanged between userspace and kernel for observation fd
 * ioctl's is stream type specific
 */
enum drm_xe_observation_ioctls {
	/** @DRM_XE_OBSERVATION_IOCTL_ENABLE: Enable data capture for an observation stream */
	DRM_XE_OBSERVATION_IOCTL_ENABLE = _IO('i', 0x0),

	/** @DRM_XE_OBSERVATION_IOCTL_DISABLE: Disable data capture for a observation stream */
	DRM_XE_OBSERVATION_IOCTL_DISABLE = _IO('i', 0x1),

	/** @DRM_XE_OBSERVATION_IOCTL_CONFIG: Change observation stream configuration */
	DRM_XE_OBSERVATION_IOCTL_CONFIG = _IO('i', 0x2),

	/** @DRM_XE_OBSERVATION_IOCTL_STATUS: Return observation stream status */
	DRM_XE_OBSERVATION_IOCTL_STATUS = _IO('i', 0x3),

	/** @DRM_XE_OBSERVATION_IOCTL_INFO: Return observation stream info */
	DRM_XE_OBSERVATION_IOCTL_INFO = _IO('i', 0x4),
};

/**
 * enum drm_xe_oa_unit_type - OA unit types
 */
enum drm_xe_oa_unit_type {
	/**
	 * @DRM_XE_OA_UNIT_TYPE_OAG: OAG OA unit. OAR/OAC are considered
	 * sub-types of OAG. For OAR/OAC, use OAG.
	 */
	DRM_XE_OA_UNIT_TYPE_OAG,

	/** @DRM_XE_OA_UNIT_TYPE_OAM: OAM OA unit */
	DRM_XE_OA_UNIT_TYPE_OAM,
};

/**
 * struct drm_xe_oa_unit - describe OA unit
 */
struct drm_xe_oa_unit {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @oa_unit_id: OA unit ID */
	__u32 oa_unit_id;

	/** @oa_unit_type: OA unit type of @drm_xe_oa_unit_type */
	__u32 oa_unit_type;

	/** @capabilities: OA capabilities bit-mask */
	__u64 capabilities;
#define DRM_XE_OA_CAPS_BASE		(1 << 0)
#define DRM_XE_OA_CAPS_SYNCS		(1 << 1)

	/** @oa_timestamp_freq: OA timestamp freq */
	__u64 oa_timestamp_freq;

	/** @reserved: MBZ */
	__u64 reserved[4];

	/** @num_engines: number of engines in @eci array */
	__u64 num_engines;

	/** @eci: engines attached to this OA unit */
	struct drm_xe_engine_class_instance eci[];
};

/**
 * struct drm_xe_query_oa_units - describe OA units
 *
 * If a query is made with a struct drm_xe_device_query where .query
 * is equal to DRM_XE_DEVICE_QUERY_OA_UNITS, then the reply uses struct
 * drm_xe_query_oa_units in .data.
 *
 * OA unit properties for all OA units can be accessed using a code block
 * such as the one below:
 *
 * .. code-block:: C
 *
 *	struct drm_xe_query_oa_units *qoa;
 *	struct drm_xe_oa_unit *oau;
 *	u8 *poau;
 *
 *	// malloc qoa and issue DRM_XE_DEVICE_QUERY_OA_UNITS. Then:
 *	poau = (u8 *)&qoa->oa_units[0];
 *	for (int i = 0; i < qoa->num_oa_units; i++) {
 *		oau = (struct drm_xe_oa_unit *)poau;
 *		// Access 'struct drm_xe_oa_unit' fields here
 *		poau += sizeof(*oau) + oau->num_engines * sizeof(oau->eci[0]);
 *	}
 */
struct drm_xe_query_oa_units {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;
	/** @num_oa_units: number of OA units returned in oau[] */
	__u32 num_oa_units;
	/** @pad: MBZ */
	__u32 pad;
	/**
	 * @oa_units: struct @drm_xe_oa_unit array returned for this device.
	 * Written below as a u64 array to avoid problems with nested flexible
	 * arrays with some compilers
	 */
	__u64 oa_units[];
};

/**
 * enum drm_xe_oa_format_type - OA format types as specified in PRM/Bspec
 * 52198/60942
 */
enum drm_xe_oa_format_type {
	/** @DRM_XE_OA_FMT_TYPE_OAG: OAG report format */
	DRM_XE_OA_FMT_TYPE_OAG,
	/** @DRM_XE_OA_FMT_TYPE_OAR: OAR report format */
	DRM_XE_OA_FMT_TYPE_OAR,
	/** @DRM_XE_OA_FMT_TYPE_OAM: OAM report format */
	DRM_XE_OA_FMT_TYPE_OAM,
	/** @DRM_XE_OA_FMT_TYPE_OAC: OAC report format */
	DRM_XE_OA_FMT_TYPE_OAC,
	/** @DRM_XE_OA_FMT_TYPE_OAM_MPEC: OAM SAMEDIA or OAM MPEC report format */
	DRM_XE_OA_FMT_TYPE_OAM_MPEC,
	/** @DRM_XE_OA_FMT_TYPE_PEC: PEC report format */
	DRM_XE_OA_FMT_TYPE_PEC,
};

/**
 * enum drm_xe_oa_property_id - OA stream property id's
 *
 * Stream params are specified as a chain of @drm_xe_ext_set_property
 * struct's, with @property values from enum @drm_xe_oa_property_id and
 * @drm_xe_user_extension base.name set to @DRM_XE_OA_EXTENSION_SET_PROPERTY.
 * @param field in struct @drm_xe_observation_param points to the first
 * @drm_xe_ext_set_property struct.
 *
 * Exactly the same mechanism is also used for stream reconfiguration using the
 * @DRM_XE_OBSERVATION_IOCTL_CONFIG observation stream fd ioctl, though only a
 * subset of properties below can be specified for stream reconfiguration.
 */
enum drm_xe_oa_property_id {
#define DRM_XE_OA_EXTENSION_SET_PROPERTY	0
	/**
	 * @DRM_XE_OA_PROPERTY_OA_UNIT_ID: ID of the OA unit on which to open
	 * the OA stream, see @oa_unit_id in 'struct
	 * drm_xe_query_oa_units'. Defaults to 0 if not provided.
	 */
	DRM_XE_OA_PROPERTY_OA_UNIT_ID = 1,

	/**
	 * @DRM_XE_OA_PROPERTY_SAMPLE_OA: A value of 1 requests inclusion of raw
	 * OA unit reports or stream samples in a global buffer attached to an
	 * OA unit.
	 */
	DRM_XE_OA_PROPERTY_SAMPLE_OA,

	/**
	 * @DRM_XE_OA_PROPERTY_OA_METRIC_SET: OA metrics defining contents of OA
	 * reports, previously added via @DRM_XE_OBSERVATION_OP_ADD_CONFIG.
	 */
	DRM_XE_OA_PROPERTY_OA_METRIC_SET,

	/** @DRM_XE_OA_PROPERTY_OA_FORMAT: OA counter report format */
	DRM_XE_OA_PROPERTY_OA_FORMAT,
	/*
	 * OA_FORMAT's are specified the same way as in PRM/Bspec 52198/60942,
	 * in terms of the following quantities: a. enum @drm_xe_oa_format_type
	 * b. Counter select c. Counter size and d. BC report. Also refer to the
	 * oa_formats array in drivers/gpu/drm/xe/xe_oa.c.
	 */
#define DRM_XE_OA_FORMAT_MASK_FMT_TYPE		(0xffu << 0)
#define DRM_XE_OA_FORMAT_MASK_COUNTER_SEL	(0xffu << 8)
#define DRM_XE_OA_FORMAT_MASK_COUNTER_SIZE	(0xffu << 16)
#define DRM_XE_OA_FORMAT_MASK_BC_REPORT		(0xffu << 24)

	/**
	 * @DRM_XE_OA_PROPERTY_OA_PERIOD_EXPONENT: Requests periodic OA unit
	 * sampling with sampling frequency proportional to 2^(period_exponent + 1)
	 */
	DRM_XE_OA_PROPERTY_OA_PERIOD_EXPONENT,

	/**
	 * @DRM_XE_OA_PROPERTY_OA_DISABLED: A value of 1 will open the OA
	 * stream in a DISABLED state (see @DRM_XE_OBSERVATION_IOCTL_ENABLE).
	 */
	DRM_XE_OA_PROPERTY_OA_DISABLED,

	/**
	 * @DRM_XE_OA_PROPERTY_EXEC_QUEUE_ID: Open the stream for a specific
	 * @exec_queue_id. OA queries can be executed on this exec queue.
	 */
	DRM_XE_OA_PROPERTY_EXEC_QUEUE_ID,

	/**
	 * @DRM_XE_OA_PROPERTY_OA_ENGINE_INSTANCE: Optional engine instance to
	 * pass along with @DRM_XE_OA_PROPERTY_EXEC_QUEUE_ID or will default to 0.
	 */
	DRM_XE_OA_PROPERTY_OA_ENGINE_INSTANCE,

	/**
	 * @DRM_XE_OA_PROPERTY_NO_PREEMPT: Allow preemption and timeslicing
	 * to be disabled for the stream exec queue.
	 */
	DRM_XE_OA_PROPERTY_NO_PREEMPT,

	/**
	 * @DRM_XE_OA_PROPERTY_NUM_SYNCS: Number of syncs in the sync array
	 * specified in @DRM_XE_OA_PROPERTY_SYNCS
	 */
	DRM_XE_OA_PROPERTY_NUM_SYNCS,

	/**
	 * @DRM_XE_OA_PROPERTY_SYNCS: Pointer to struct @drm_xe_sync array
	 * with array size specified via @DRM_XE_OA_PROPERTY_NUM_SYNCS. OA
	 * configuration will wait till input fences signal. Output fences
	 * will signal after the new OA configuration takes effect. For
	 * @DRM_XE_SYNC_TYPE_USER_FENCE, @addr is a user pointer, similar
	 * to the VM bind case.
	 */
	DRM_XE_OA_PROPERTY_SYNCS,
};

/**
 * struct drm_xe_oa_config - OA metric configuration
 *
 * Multiple OA configs can be added using @DRM_XE_OBSERVATION_OP_ADD_CONFIG. A
 * particular config can be specified when opening an OA stream using
 * @DRM_XE_OA_PROPERTY_OA_METRIC_SET property.
 */
struct drm_xe_oa_config {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @uuid: String formatted like "%\08x-%\04x-%\04x-%\04x-%\012x" */
	char uuid[36];

	/** @n_regs: Number of regs in @regs_ptr */
	__u32 n_regs;

	/**
	 * @regs_ptr: Pointer to (register address, value) pairs for OA config
	 * registers. Expected length of buffer is: (2 * sizeof(u32) * @n_regs).
	 */
	__u64 regs_ptr;
};

/**
 * struct drm_xe_oa_stream_status - OA stream status returned from
 * @DRM_XE_OBSERVATION_IOCTL_STATUS observation stream fd ioctl. Userspace can
 * call the ioctl to query stream status in response to EIO errno from
 * observation fd read().
 */
struct drm_xe_oa_stream_status {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @oa_status: OA stream status (see Bspec 46717/61226) */
	__u64 oa_status;
#define DRM_XE_OASTATUS_MMIO_TRG_Q_FULL		(1 << 3)
#define DRM_XE_OASTATUS_COUNTER_OVERFLOW	(1 << 2)
#define DRM_XE_OASTATUS_BUFFER_OVERFLOW		(1 << 1)
#define DRM_XE_OASTATUS_REPORT_LOST		(1 << 0)

	/** @reserved: reserved for future use */
	__u64 reserved[3];
};

/**
 * struct drm_xe_oa_stream_info - OA stream info returned from
 * @DRM_XE_OBSERVATION_IOCTL_INFO observation stream fd ioctl
 */
struct drm_xe_oa_stream_info {
	/** @extensions: Pointer to the first extension struct, if any */
	__u64 extensions;

	/** @oa_buf_size: OA buffer size */
	__u64 oa_buf_size;

	/** @reserved: reserved for future use */
	__u64 reserved[3];
};

#if defined(__cplusplus)
}
#endif

#endif /* _UAPI_XE_DRM_H_ */
