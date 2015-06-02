/* amdgpu_drm.h -- Public header for the amdgpu driver -*- linux-c -*-
 *
 * Copyright 2000 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Fremont, California.
 * Copyright 2002 Tungsten Graphics, Inc., Cedar Park, Texas.
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Kevin E. Martin <martin@valinux.com>
 *    Gareth Hughes <gareth@valinux.com>
 *    Keith Whitwell <keith@tungstengraphics.com>
 */

#ifndef __AMDGPU_DRM_H__
#define __AMDGPU_DRM_H__

#include <drm/drm.h>

#define DRM_AMDGPU_GEM_CREATE		0x00
#define DRM_AMDGPU_GEM_MMAP		0x01
#define DRM_AMDGPU_CTX			0x02
#define DRM_AMDGPU_BO_LIST		0x03
#define DRM_AMDGPU_CS			0x04
#define DRM_AMDGPU_INFO			0x05
#define DRM_AMDGPU_GEM_METADATA		0x06
#define DRM_AMDGPU_GEM_WAIT_IDLE	0x07
#define DRM_AMDGPU_GEM_VA		0x08
#define DRM_AMDGPU_WAIT_CS		0x09
#define DRM_AMDGPU_GEM_OP		0x10
#define DRM_AMDGPU_GEM_USERPTR		0x11

#define DRM_IOCTL_AMDGPU_GEM_CREATE	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_GEM_CREATE, union drm_amdgpu_gem_create)
#define DRM_IOCTL_AMDGPU_GEM_MMAP	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_GEM_MMAP, union drm_amdgpu_gem_mmap)
#define DRM_IOCTL_AMDGPU_CTX		DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_CTX, union drm_amdgpu_ctx)
#define DRM_IOCTL_AMDGPU_BO_LIST	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_BO_LIST, union drm_amdgpu_bo_list)
#define DRM_IOCTL_AMDGPU_CS		DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_CS, union drm_amdgpu_cs)
#define DRM_IOCTL_AMDGPU_INFO		DRM_IOW(DRM_COMMAND_BASE + DRM_AMDGPU_INFO, struct drm_amdgpu_info)
#define DRM_IOCTL_AMDGPU_GEM_METADATA	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_GEM_METADATA, struct drm_amdgpu_gem_metadata)
#define DRM_IOCTL_AMDGPU_GEM_WAIT_IDLE	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_GEM_WAIT_IDLE, union drm_amdgpu_gem_wait_idle)
#define DRM_IOCTL_AMDGPU_GEM_VA		DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_GEM_VA, union drm_amdgpu_gem_va)
#define DRM_IOCTL_AMDGPU_WAIT_CS	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_WAIT_CS, union drm_amdgpu_wait_cs)
#define DRM_IOCTL_AMDGPU_GEM_OP		DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_GEM_OP, struct drm_amdgpu_gem_op)
#define DRM_IOCTL_AMDGPU_GEM_USERPTR	DRM_IOWR(DRM_COMMAND_BASE + DRM_AMDGPU_GEM_USERPTR, struct drm_amdgpu_gem_userptr)

#define AMDGPU_GEM_DOMAIN_CPU		0x1
#define AMDGPU_GEM_DOMAIN_GTT		0x2
#define AMDGPU_GEM_DOMAIN_VRAM		0x4
#define AMDGPU_GEM_DOMAIN_GDS		0x8
#define AMDGPU_GEM_DOMAIN_GWS		0x10
#define AMDGPU_GEM_DOMAIN_OA		0x20

/* Flag that CPU access will be required for the case of VRAM domain */
#define AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED	(1 << 0)
/* Flag that CPU access will not work, this VRAM domain is invisible */
#define AMDGPU_GEM_CREATE_NO_CPU_ACCESS		(1 << 1)
/* Flag that USWC attributes should be used for GTT */
#define AMDGPU_GEM_CREATE_CPU_GTT_USWC		(1 << 2)

struct drm_amdgpu_gem_create_in  {
	/** the requested memory size */
	uint64_t bo_size;
	/** physical start_addr alignment in bytes for some HW requirements */
	uint64_t alignment;
	/** the requested memory domains */
	uint64_t domains;
	/** allocation flags */
	uint64_t domain_flags;
};

struct drm_amdgpu_gem_create_out  {
	/** returned GEM object handle */
	uint32_t handle;
	uint32_t _pad;
};

union drm_amdgpu_gem_create {
	struct drm_amdgpu_gem_create_in		in;
	struct drm_amdgpu_gem_create_out	out;
};

/** Opcode to create new residency list.  */
#define AMDGPU_BO_LIST_OP_CREATE	0
/** Opcode to destroy previously created residency list */
#define AMDGPU_BO_LIST_OP_DESTROY	1
/** Opcode to update resource information in the list */
#define AMDGPU_BO_LIST_OP_UPDATE	2

struct drm_amdgpu_bo_list_in {
	/** Type of operation */
	uint32_t operation;
	/** Handle of list or 0 if we want to create one */
	uint32_t list_handle;
	/** Number of BOs in list  */
	uint32_t bo_number;
	/** Size of each element describing BO */
	uint32_t bo_info_size;
	/** Pointer to array describing BOs */
	uint64_t bo_info_ptr;
};

struct drm_amdgpu_bo_list_entry {
	/** Handle of BO */
	uint32_t bo_handle;
	/** New (if specified) BO priority to be used during migration */
	uint32_t bo_priority;
};

struct drm_amdgpu_bo_list_out {
	/** Handle of resource list  */
	uint32_t list_handle;
	uint32_t _pad;
};

union drm_amdgpu_bo_list {
	struct drm_amdgpu_bo_list_in in;
	struct drm_amdgpu_bo_list_out out;
};

/* context related */
#define AMDGPU_CTX_OP_ALLOC_CTX	1
#define AMDGPU_CTX_OP_FREE_CTX	2
#define AMDGPU_CTX_OP_QUERY_STATE	3

#define AMDGPU_CTX_OP_STATE_RUNNING	1

/* GPU reset status */
#define AMDGPU_CTX_NO_RESET		0
#define AMDGPU_CTX_GUILTY_RESET		1 /* this the context caused it */
#define AMDGPU_CTX_INNOCENT_RESET	2 /* some other context caused it */
#define AMDGPU_CTX_UNKNOWN_RESET	3 /* unknown cause */

struct drm_amdgpu_ctx_in {
	uint32_t	op;
	uint32_t	flags;
	uint32_t	ctx_id;
	uint32_t	_pad;
};

union drm_amdgpu_ctx_out {
		struct {
			uint32_t	ctx_id;
			uint32_t	_pad;
		} alloc;

		struct {
			uint64_t	flags;
			/** Number of resets caused by this context so far. */
			uint32_t	hangs;
			/** Reset status since the last call of the ioctl. */
			uint32_t	reset_status;
		} state;
};

union drm_amdgpu_ctx {
	struct drm_amdgpu_ctx_in in;
	union drm_amdgpu_ctx_out out;
};

/*
 * This is not a reliable API and you should expect it to fail for any
 * number of reasons and have fallback path that do not use userptr to
 * perform any operation.
 */
#define AMDGPU_GEM_USERPTR_READONLY	(1 << 0)
#define AMDGPU_GEM_USERPTR_ANONONLY	(1 << 1)
#define AMDGPU_GEM_USERPTR_VALIDATE	(1 << 2)
#define AMDGPU_GEM_USERPTR_REGISTER	(1 << 3)

struct drm_amdgpu_gem_userptr {
	uint64_t		addr;
	uint64_t		size;
	uint32_t		flags;
	uint32_t		handle;
};

/* same meaning as the GB_TILE_MODE and GL_MACRO_TILE_MODE fields */
#define AMDGPU_TILING_ARRAY_MODE_SHIFT			0
#define AMDGPU_TILING_ARRAY_MODE_MASK			0xf
#define AMDGPU_TILING_PIPE_CONFIG_SHIFT			4
#define AMDGPU_TILING_PIPE_CONFIG_MASK			0x1f
#define AMDGPU_TILING_TILE_SPLIT_SHIFT			9
#define AMDGPU_TILING_TILE_SPLIT_MASK			0x7
#define AMDGPU_TILING_MICRO_TILE_MODE_SHIFT		12
#define AMDGPU_TILING_MICRO_TILE_MODE_MASK		0x7
#define AMDGPU_TILING_BANK_WIDTH_SHIFT			15
#define AMDGPU_TILING_BANK_WIDTH_MASK			0x3
#define AMDGPU_TILING_BANK_HEIGHT_SHIFT			17
#define AMDGPU_TILING_BANK_HEIGHT_MASK			0x3
#define AMDGPU_TILING_MACRO_TILE_ASPECT_SHIFT		19
#define AMDGPU_TILING_MACRO_TILE_ASPECT_MASK		0x3
#define AMDGPU_TILING_NUM_BANKS_SHIFT			21
#define AMDGPU_TILING_NUM_BANKS_MASK			0x3

#define AMDGPU_TILING_SET(field, value) \
	(((value) & AMDGPU_TILING_##field##_MASK) << AMDGPU_TILING_##field##_SHIFT)
#define AMDGPU_TILING_GET(value, field) \
	(((value) >> AMDGPU_TILING_##field##_SHIFT) & AMDGPU_TILING_##field##_MASK)

#define AMDGPU_GEM_METADATA_OP_SET_METADATA                  1
#define AMDGPU_GEM_METADATA_OP_GET_METADATA                  2

/** The same structure is shared for input/output */
struct drm_amdgpu_gem_metadata {
	uint32_t	handle;		/* GEM Object handle */
	uint32_t	op;		/** Do we want get or set metadata */
	struct {
		uint64_t	flags;
		uint64_t	tiling_info; /* family specific tiling info */
		uint32_t	data_size_bytes;
		uint32_t	data[64];
	} data;
};

struct drm_amdgpu_gem_mmap_in {
	uint32_t handle;		/** the GEM object handle */
	uint32_t _pad;
};

struct drm_amdgpu_gem_mmap_out {
	uint64_t addr_ptr;	/** mmap offset from the vma offset manager */
};

union drm_amdgpu_gem_mmap {
	struct drm_amdgpu_gem_mmap_in   in;
	struct drm_amdgpu_gem_mmap_out out;
};

struct drm_amdgpu_gem_wait_idle_in {
	uint32_t handle;   	/* GEM object handle */
	uint32_t flags;
	uint64_t timeout; 	/* Timeout to wait. If 0 then returned immediately with the status */
};

struct drm_amdgpu_gem_wait_idle_out {
	uint32_t status;	/*   BO status:  0 - BO is idle, 1 - BO is busy */
	uint32_t domain; /*  Returned current memory domain */
};

union drm_amdgpu_gem_wait_idle {
	struct drm_amdgpu_gem_wait_idle_in  in;
	struct drm_amdgpu_gem_wait_idle_out out;
};

struct drm_amdgpu_wait_cs_in {
	uint64_t handle;
	uint64_t timeout;
	uint32_t ip_type;
	uint32_t ip_instance;
	uint32_t ring;
	uint32_t ctx_id;
};

struct drm_amdgpu_wait_cs_out {
	uint64_t status;
};

union drm_amdgpu_wait_cs {
	struct drm_amdgpu_wait_cs_in in;
	struct drm_amdgpu_wait_cs_out out;
};

/* Sets or returns a value associated with a buffer. */
struct drm_amdgpu_gem_op {
	uint32_t	handle; /* buffer */
	uint32_t	op;     /* AMDGPU_GEM_OP_* */
	uint64_t	value;  /* input or return value */
};

#define AMDGPU_GEM_OP_GET_GEM_CREATE_INFO	0
#define AMDGPU_GEM_OP_SET_PLACEMENT		1

#define AMDGPU_VA_OP_MAP			1
#define AMDGPU_VA_OP_UNMAP			2

#define AMDGPU_VA_RESULT_OK			0
#define AMDGPU_VA_RESULT_ERROR			1
#define AMDGPU_VA_RESULT_VA_INVALID_ALIGNMENT	2

/* Mapping flags */
/* readable mapping */
#define AMDGPU_VM_PAGE_READABLE		(1 << 1)
/* writable mapping */
#define AMDGPU_VM_PAGE_WRITEABLE	(1 << 2)
/* executable mapping, new for VI */
#define AMDGPU_VM_PAGE_EXECUTABLE	(1 << 3)

struct drm_amdgpu_gem_va_in {
	/* GEM object handle */
	uint32_t handle;
	uint32_t _pad;
	/* map or unmap*/
	uint32_t operation;
	/* specify mapping flags */
	uint32_t flags;
	/* va address to assign . Must be correctly aligned.*/
	uint64_t va_address;
	/* Specify offset inside of BO to assign. Must be correctly aligned.*/
	uint64_t offset_in_bo;
	/* Specify mapping size. If 0 and offset is 0 then map the whole BO.*/
	/* Must be correctly aligned. */
	uint64_t map_size;
};

struct drm_amdgpu_gem_va_out {
	uint32_t result;
	uint32_t _pad;
};

union drm_amdgpu_gem_va {
	struct drm_amdgpu_gem_va_in  in;
	struct drm_amdgpu_gem_va_out out;
};

#define AMDGPU_HW_IP_GFX          0
#define AMDGPU_HW_IP_COMPUTE      1
#define AMDGPU_HW_IP_DMA          2
#define AMDGPU_HW_IP_UVD          3
#define AMDGPU_HW_IP_VCE          4
#define AMDGPU_HW_IP_NUM          5

#define AMDGPU_HW_IP_INSTANCE_MAX_COUNT 1

#define AMDGPU_CHUNK_ID_IB		0x01
#define AMDGPU_CHUNK_ID_FENCE		0x02
struct drm_amdgpu_cs_chunk {
	uint32_t		chunk_id;
	uint32_t		length_dw;
	uint64_t		chunk_data;
};

struct drm_amdgpu_cs_in {
	/** Rendering context id */
	uint32_t		ctx_id;
	/**  Handle of resource list associated with CS */
	uint32_t		bo_list_handle;
	uint32_t		num_chunks;
	uint32_t		_pad;
	/* this points to uint64_t * which point to cs chunks */
	uint64_t		chunks;
};

struct drm_amdgpu_cs_out {
	uint64_t handle;
};

union drm_amdgpu_cs {
       struct drm_amdgpu_cs_in in;
       struct drm_amdgpu_cs_out out;
};

/* Specify flags to be used for IB */

/* This IB should be submitted to CE */
#define AMDGPU_IB_FLAG_CE	(1<<0)

/* GDS is used by this IB */
#define AMDGPU_IB_FLAG_GDS	(1<<1)

/* CE Preamble */
#define AMDGPU_IB_FLAG_PREAMBLE (1<<2)

struct drm_amdgpu_cs_chunk_ib {
	uint32_t _pad;
	uint32_t flags;		/* IB Flags */
	uint64_t va_start;	/* Virtual address to begin IB execution */
	uint32_t ib_bytes;	/* Size of submission */
	uint32_t ip_type;	/* HW IP to submit to */
	uint32_t ip_instance;	/* HW IP index of the same type to submit to  */
	uint32_t ring;		/* Ring index to submit to */
};

struct drm_amdgpu_cs_chunk_fence {
	uint32_t handle;
	uint32_t offset;
};

struct drm_amdgpu_cs_chunk_data {
	union {
		struct drm_amdgpu_cs_chunk_ib		ib_data;
		struct drm_amdgpu_cs_chunk_fence	fence_data;
	};
};

/**
 *  Query h/w info: Flag that this is integrated (a.h.a. fusion) GPU
 *
 */
#define AMDGPU_IDS_FLAGS_FUSION         0x1

/* indicate if acceleration can be working */
#define AMDGPU_INFO_ACCEL_WORKING		0x00
/* get the crtc_id from the mode object id? */
#define AMDGPU_INFO_CRTC_FROM_ID		0x01
/* query hw IP info */
#define AMDGPU_INFO_HW_IP_INFO			0x02
/* query hw IP instance count for the specified type */
#define AMDGPU_INFO_HW_IP_COUNT			0x03
/* timestamp for GL_ARB_timer_query */
#define AMDGPU_INFO_TIMESTAMP			0x05
/* Query the firmware version */
#define AMDGPU_INFO_FW_VERSION			0x0e
	/* Subquery id: Query VCE firmware version */
	#define AMDGPU_INFO_FW_VCE		0x1
	/* Subquery id: Query UVD firmware version */
	#define AMDGPU_INFO_FW_UVD		0x2
	/* Subquery id: Query GMC firmware version */
	#define AMDGPU_INFO_FW_GMC		0x03
	/* Subquery id: Query GFX ME firmware version */
	#define AMDGPU_INFO_FW_GFX_ME		0x04
	/* Subquery id: Query GFX PFP firmware version */
	#define AMDGPU_INFO_FW_GFX_PFP		0x05
	/* Subquery id: Query GFX CE firmware version */
	#define AMDGPU_INFO_FW_GFX_CE		0x06
	/* Subquery id: Query GFX RLC firmware version */
	#define AMDGPU_INFO_FW_GFX_RLC		0x07
	/* Subquery id: Query GFX MEC firmware version */
	#define AMDGPU_INFO_FW_GFX_MEC		0x08
	/* Subquery id: Query SMC firmware version */
	#define AMDGPU_INFO_FW_SMC		0x0a
	/* Subquery id: Query SDMA firmware version */
	#define AMDGPU_INFO_FW_SDMA		0x0b
/* number of bytes moved for TTM migration */
#define AMDGPU_INFO_NUM_BYTES_MOVED		0x0f
/* the used VRAM size */
#define AMDGPU_INFO_VRAM_USAGE			0x10
/* the used GTT size */
#define AMDGPU_INFO_GTT_USAGE			0x11
/* Information about GDS, etc. resource configuration */
#define AMDGPU_INFO_GDS_CONFIG			0x13
/* Query information about VRAM and GTT domains */
#define AMDGPU_INFO_VRAM_GTT			0x14
/* Query information about register in MMR address space*/
#define AMDGPU_INFO_READ_MMR_REG		0x15
/* Query information about device: rev id, family, etc. */
#define AMDGPU_INFO_DEV_INFO			0x16
/* visible vram usage */
#define AMDGPU_INFO_VIS_VRAM_USAGE		0x17

#define AMDGPU_INFO_MMR_SE_INDEX_SHIFT	0
#define AMDGPU_INFO_MMR_SE_INDEX_MASK	0xff
#define AMDGPU_INFO_MMR_SH_INDEX_SHIFT	8
#define AMDGPU_INFO_MMR_SH_INDEX_MASK	0xff

/* Input structure for the INFO ioctl */
struct drm_amdgpu_info {
	/* Where the return value will be stored */
	uint64_t return_pointer;
	/* The size of the return value. Just like "size" in "snprintf",
	 * it limits how many bytes the kernel can write. */
	uint32_t return_size;
	/* The query request id. */
	uint32_t query;

	union {
		struct {
			uint32_t id;
			uint32_t _pad;
		} mode_crtc;

		struct {
			/** AMDGPU_HW_IP_* */
			uint32_t type;
			/**
			 * Index of the IP if there are more IPs of the same type.
			 * Ignored by AMDGPU_INFO_HW_IP_COUNT.
			 */
			uint32_t ip_instance;
		} query_hw_ip;

		struct {
			uint32_t dword_offset;
			uint32_t count; /* number of registers to read */
			uint32_t instance;
			uint32_t flags;
		} read_mmr_reg;

		struct {
			/** AMDGPU_INFO_FW_* */
			uint32_t fw_type;
			/** Index of the IP if there are more IPs of the same type. */
			uint32_t ip_instance;
			/**
			 * Index of the engine. Whether this is used depends
			 * on the firmware type. (e.g. MEC, SDMA)
			 */
			uint32_t index;
			uint32_t _pad;
		} query_fw;
	};
};

struct drm_amdgpu_info_gds {
	/** GDS GFX partition size */
	uint32_t gds_gfx_partition_size;
	/** GDS compute partition size */
	uint32_t compute_partition_size;
	/** total GDS memory size */
	uint32_t gds_total_size;
	/** GWS size per GFX partition */
	uint32_t gws_per_gfx_partition;
	/** GSW size per compute partition */
	uint32_t gws_per_compute_partition;
	/** OA size per GFX partition */
	uint32_t oa_per_gfx_partition;
	/** OA size per compute partition */
	uint32_t oa_per_compute_partition;
	uint32_t _pad;
};

struct drm_amdgpu_info_vram_gtt {
	uint64_t vram_size;
	uint64_t vram_cpu_accessible_size;
	uint64_t gtt_size;
};

struct drm_amdgpu_info_firmware {
	uint32_t ver;
	uint32_t feature;
};

#define AMDGPU_VRAM_TYPE_UNKNOWN 0
#define AMDGPU_VRAM_TYPE_GDDR1 1
#define AMDGPU_VRAM_TYPE_DDR2  2
#define AMDGPU_VRAM_TYPE_GDDR3 3
#define AMDGPU_VRAM_TYPE_GDDR4 4
#define AMDGPU_VRAM_TYPE_GDDR5 5
#define AMDGPU_VRAM_TYPE_HBM   6
#define AMDGPU_VRAM_TYPE_DDR3  7

struct drm_amdgpu_info_device {
	/** PCI Device ID */
	uint32_t device_id;
	/** Internal chip revision: A0, A1, etc.) */
	uint32_t chip_rev;
	uint32_t external_rev;
	/** Revision id in PCI Config space */
	uint32_t pci_rev;
	uint32_t family;
	uint32_t num_shader_engines;
	uint32_t num_shader_arrays_per_engine;
	uint32_t gpu_counter_freq; /* in KHz */
	uint64_t max_engine_clock; /* in KHz */
	uint64_t max_memory_clock; /* in KHz */
	/* cu information */
	uint32_t cu_active_number;
	uint32_t cu_ao_mask;
	uint32_t cu_bitmap[4][4];
	/** Render backend pipe mask. One render backend is CB+DB. */
	uint32_t enabled_rb_pipes_mask;
	uint32_t num_rb_pipes;
	uint32_t num_hw_gfx_contexts;
	uint32_t _pad;
	uint64_t ids_flags;
	/** Starting virtual address for UMDs. */
	uint64_t virtual_address_offset;
	/** The maximum virtual address */
	uint64_t virtual_address_max;
	/** Required alignment of virtual addresses. */
	uint32_t virtual_address_alignment;
	/** Page table entry - fragment size */
	uint32_t pte_fragment_size;
	uint32_t gart_page_size;
	/** constant engine ram size*/
	uint32_t ce_ram_size;
	/** video memory type infro*/
	uint32_t vram_type;
	/** video memory bit width*/
	uint32_t vram_bit_width;
};

struct drm_amdgpu_info_hw_ip {
	/** Version of h/w IP */
	uint32_t  hw_ip_version_major;
	uint32_t  hw_ip_version_minor;
	/** Capabilities */
	uint64_t  capabilities_flags;
	/** command buffer address start alignment*/
	uint32_t  ib_start_alignment;
	/** command buffer size alignment*/
	uint32_t  ib_size_alignment;
	/** Bitmask of available rings. Bit 0 means ring 0, etc. */
	uint32_t  available_rings;
	uint32_t  _pad;
};

/*
 * Supported GPU families
 */
#define AMDGPU_FAMILY_UNKNOWN			0
#define AMDGPU_FAMILY_CI			120 /* Bonaire, Hawaii */
#define AMDGPU_FAMILY_KV			125 /* Kaveri, Kabini, Mullins */
#define AMDGPU_FAMILY_VI			130 /* Iceland, Tonga */
#define AMDGPU_FAMILY_CZ			135 /* Carrizo */

#endif
