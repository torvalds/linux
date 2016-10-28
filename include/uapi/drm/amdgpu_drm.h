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

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

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
#define DRM_IOCTL_AMDGPU_GEM_VA		DRM_IOW(DRM_COMMAND_BASE + DRM_AMDGPU_GEM_VA, struct drm_amdgpu_gem_va)
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
/* Flag that the memory should be in VRAM and cleared */
#define AMDGPU_GEM_CREATE_VRAM_CLEARED		(1 << 3)
/* Flag that create shadow bo(GTT) while allocating vram bo */
#define AMDGPU_GEM_CREATE_SHADOW		(1 << 4)
/* Flag that allocating the BO should use linear VRAM */
#define AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS	(1 << 5)

struct drm_amdgpu_gem_create_in  {
	/** the requested memory size */
	__u64 bo_size;
	/** physical start_addr alignment in bytes for some HW requirements */
	__u64 alignment;
	/** the requested memory domains */
	__u64 domains;
	/** allocation flags */
	__u64 domain_flags;
};

struct drm_amdgpu_gem_create_out  {
	/** returned GEM object handle */
	__u32 handle;
	__u32 _pad;
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
	__u32 operation;
	/** Handle of list or 0 if we want to create one */
	__u32 list_handle;
	/** Number of BOs in list  */
	__u32 bo_number;
	/** Size of each element describing BO */
	__u32 bo_info_size;
	/** Pointer to array describing BOs */
	__u64 bo_info_ptr;
};

struct drm_amdgpu_bo_list_entry {
	/** Handle of BO */
	__u32 bo_handle;
	/** New (if specified) BO priority to be used during migration */
	__u32 bo_priority;
};

struct drm_amdgpu_bo_list_out {
	/** Handle of resource list  */
	__u32 list_handle;
	__u32 _pad;
};

union drm_amdgpu_bo_list {
	struct drm_amdgpu_bo_list_in in;
	struct drm_amdgpu_bo_list_out out;
};

/* context related */
#define AMDGPU_CTX_OP_ALLOC_CTX	1
#define AMDGPU_CTX_OP_FREE_CTX	2
#define AMDGPU_CTX_OP_QUERY_STATE	3

/* GPU reset status */
#define AMDGPU_CTX_NO_RESET		0
/* this the context caused it */
#define AMDGPU_CTX_GUILTY_RESET		1
/* some other context caused it */
#define AMDGPU_CTX_INNOCENT_RESET	2
/* unknown cause */
#define AMDGPU_CTX_UNKNOWN_RESET	3

struct drm_amdgpu_ctx_in {
	/** AMDGPU_CTX_OP_* */
	__u32	op;
	/** For future use, no flags defined so far */
	__u32	flags;
	__u32	ctx_id;
	__u32	_pad;
};

union drm_amdgpu_ctx_out {
		struct {
			__u32	ctx_id;
			__u32	_pad;
		} alloc;

		struct {
			/** For future use, no flags defined so far */
			__u64	flags;
			/** Number of resets caused by this context so far. */
			__u32	hangs;
			/** Reset status since the last call of the ioctl. */
			__u32	reset_status;
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
	__u64		addr;
	__u64		size;
	/* AMDGPU_GEM_USERPTR_* */
	__u32		flags;
	/* Resulting GEM handle */
	__u32		handle;
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
	/** GEM Object handle */
	__u32	handle;
	/** Do we want get or set metadata */
	__u32	op;
	struct {
		/** For future use, no flags defined so far */
		__u64	flags;
		/** family specific tiling info */
		__u64	tiling_info;
		__u32	data_size_bytes;
		__u32	data[64];
	} data;
};

struct drm_amdgpu_gem_mmap_in {
	/** the GEM object handle */
	__u32 handle;
	__u32 _pad;
};

struct drm_amdgpu_gem_mmap_out {
	/** mmap offset from the vma offset manager */
	__u64 addr_ptr;
};

union drm_amdgpu_gem_mmap {
	struct drm_amdgpu_gem_mmap_in   in;
	struct drm_amdgpu_gem_mmap_out out;
};

struct drm_amdgpu_gem_wait_idle_in {
	/** GEM object handle */
	__u32 handle;
	/** For future use, no flags defined so far */
	__u32 flags;
	/** Absolute timeout to wait */
	__u64 timeout;
};

struct drm_amdgpu_gem_wait_idle_out {
	/** BO status:  0 - BO is idle, 1 - BO is busy */
	__u32 status;
	/** Returned current memory domain */
	__u32 domain;
};

union drm_amdgpu_gem_wait_idle {
	struct drm_amdgpu_gem_wait_idle_in  in;
	struct drm_amdgpu_gem_wait_idle_out out;
};

struct drm_amdgpu_wait_cs_in {
	/** Command submission handle */
	__u64 handle;
	/** Absolute timeout to wait */
	__u64 timeout;
	__u32 ip_type;
	__u32 ip_instance;
	__u32 ring;
	__u32 ctx_id;
};

struct drm_amdgpu_wait_cs_out {
	/** CS status:  0 - CS completed, 1 - CS still busy */
	__u64 status;
};

union drm_amdgpu_wait_cs {
	struct drm_amdgpu_wait_cs_in in;
	struct drm_amdgpu_wait_cs_out out;
};

#define AMDGPU_GEM_OP_GET_GEM_CREATE_INFO	0
#define AMDGPU_GEM_OP_SET_PLACEMENT		1

/* Sets or returns a value associated with a buffer. */
struct drm_amdgpu_gem_op {
	/** GEM object handle */
	__u32	handle;
	/** AMDGPU_GEM_OP_* */
	__u32	op;
	/** Input or return value */
	__u64	value;
};

#define AMDGPU_VA_OP_MAP			1
#define AMDGPU_VA_OP_UNMAP			2

/* Delay the page table update till the next CS */
#define AMDGPU_VM_DELAY_UPDATE		(1 << 0)

/* Mapping flags */
/* readable mapping */
#define AMDGPU_VM_PAGE_READABLE		(1 << 1)
/* writable mapping */
#define AMDGPU_VM_PAGE_WRITEABLE	(1 << 2)
/* executable mapping, new for VI */
#define AMDGPU_VM_PAGE_EXECUTABLE	(1 << 3)

struct drm_amdgpu_gem_va {
	/** GEM object handle */
	__u32 handle;
	__u32 _pad;
	/** AMDGPU_VA_OP_* */
	__u32 operation;
	/** AMDGPU_VM_PAGE_* */
	__u32 flags;
	/** va address to assign . Must be correctly aligned.*/
	__u64 va_address;
	/** Specify offset inside of BO to assign. Must be correctly aligned.*/
	__u64 offset_in_bo;
	/** Specify mapping size. Must be correctly aligned. */
	__u64 map_size;
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
#define AMDGPU_CHUNK_ID_DEPENDENCIES	0x03

struct drm_amdgpu_cs_chunk {
	__u32		chunk_id;
	__u32		length_dw;
	__u64		chunk_data;
};

struct drm_amdgpu_cs_in {
	/** Rendering context id */
	__u32		ctx_id;
	/**  Handle of resource list associated with CS */
	__u32		bo_list_handle;
	__u32		num_chunks;
	__u32		_pad;
	/** this points to __u64 * which point to cs chunks */
	__u64		chunks;
};

struct drm_amdgpu_cs_out {
	__u64 handle;
};

union drm_amdgpu_cs {
	struct drm_amdgpu_cs_in in;
	struct drm_amdgpu_cs_out out;
};

/* Specify flags to be used for IB */

/* This IB should be submitted to CE */
#define AMDGPU_IB_FLAG_CE	(1<<0)

/* CE Preamble */
#define AMDGPU_IB_FLAG_PREAMBLE (1<<1)

struct drm_amdgpu_cs_chunk_ib {
	__u32 _pad;
	/** AMDGPU_IB_FLAG_* */
	__u32 flags;
	/** Virtual address to begin IB execution */
	__u64 va_start;
	/** Size of submission */
	__u32 ib_bytes;
	/** HW IP to submit to */
	__u32 ip_type;
	/** HW IP index of the same type to submit to  */
	__u32 ip_instance;
	/** Ring index to submit to */
	__u32 ring;
};

struct drm_amdgpu_cs_chunk_dep {
	__u32 ip_type;
	__u32 ip_instance;
	__u32 ring;
	__u32 ctx_id;
	__u64 handle;
};

struct drm_amdgpu_cs_chunk_fence {
	__u32 handle;
	__u32 offset;
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
#define AMDGPU_IDS_FLAGS_PREEMPTION     0x2

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
/* number of TTM buffer evictions */
#define AMDGPU_INFO_NUM_EVICTIONS		0x18
/* Query memory about VRAM and GTT domains */
#define AMDGPU_INFO_MEMORY			0x19
/* Query vce clock table */
#define AMDGPU_INFO_VCE_CLOCK_TABLE		0x1A

#define AMDGPU_INFO_MMR_SE_INDEX_SHIFT	0
#define AMDGPU_INFO_MMR_SE_INDEX_MASK	0xff
#define AMDGPU_INFO_MMR_SH_INDEX_SHIFT	8
#define AMDGPU_INFO_MMR_SH_INDEX_MASK	0xff

struct drm_amdgpu_query_fw {
	/** AMDGPU_INFO_FW_* */
	__u32 fw_type;
	/**
	 * Index of the IP if there are more IPs of
	 * the same type.
	 */
	__u32 ip_instance;
	/**
	 * Index of the engine. Whether this is used depends
	 * on the firmware type. (e.g. MEC, SDMA)
	 */
	__u32 index;
	__u32 _pad;
};

/* Input structure for the INFO ioctl */
struct drm_amdgpu_info {
	/* Where the return value will be stored */
	__u64 return_pointer;
	/* The size of the return value. Just like "size" in "snprintf",
	 * it limits how many bytes the kernel can write. */
	__u32 return_size;
	/* The query request id. */
	__u32 query;

	union {
		struct {
			__u32 id;
			__u32 _pad;
		} mode_crtc;

		struct {
			/** AMDGPU_HW_IP_* */
			__u32 type;
			/**
			 * Index of the IP if there are more IPs of the same
			 * type. Ignored by AMDGPU_INFO_HW_IP_COUNT.
			 */
			__u32 ip_instance;
		} query_hw_ip;

		struct {
			__u32 dword_offset;
			/** number of registers to read */
			__u32 count;
			__u32 instance;
			/** For future use, no flags defined so far */
			__u32 flags;
		} read_mmr_reg;

		struct drm_amdgpu_query_fw query_fw;
	};
};

struct drm_amdgpu_info_gds {
	/** GDS GFX partition size */
	__u32 gds_gfx_partition_size;
	/** GDS compute partition size */
	__u32 compute_partition_size;
	/** total GDS memory size */
	__u32 gds_total_size;
	/** GWS size per GFX partition */
	__u32 gws_per_gfx_partition;
	/** GSW size per compute partition */
	__u32 gws_per_compute_partition;
	/** OA size per GFX partition */
	__u32 oa_per_gfx_partition;
	/** OA size per compute partition */
	__u32 oa_per_compute_partition;
	__u32 _pad;
};

struct drm_amdgpu_info_vram_gtt {
	__u64 vram_size;
	__u64 vram_cpu_accessible_size;
	__u64 gtt_size;
};

struct drm_amdgpu_heap_info {
	/** max. physical memory */
	__u64 total_heap_size;

	/** Theoretical max. available memory in the given heap */
	__u64 usable_heap_size;

	/**
	 * Number of bytes allocated in the heap. This includes all processes
	 * and private allocations in the kernel. It changes when new buffers
	 * are allocated, freed, and moved. It cannot be larger than
	 * heap_size.
	 */
	__u64 heap_usage;

	/**
	 * Theoretical possible max. size of buffer which
	 * could be allocated in the given heap
	 */
	__u64 max_allocation;
};

struct drm_amdgpu_memory_info {
	struct drm_amdgpu_heap_info vram;
	struct drm_amdgpu_heap_info cpu_accessible_vram;
	struct drm_amdgpu_heap_info gtt;
};

struct drm_amdgpu_info_firmware {
	__u32 ver;
	__u32 feature;
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
	__u32 device_id;
	/** Internal chip revision: A0, A1, etc.) */
	__u32 chip_rev;
	__u32 external_rev;
	/** Revision id in PCI Config space */
	__u32 pci_rev;
	__u32 family;
	__u32 num_shader_engines;
	__u32 num_shader_arrays_per_engine;
	/* in KHz */
	__u32 gpu_counter_freq;
	__u64 max_engine_clock;
	__u64 max_memory_clock;
	/* cu information */
	__u32 cu_active_number;
	__u32 cu_ao_mask;
	__u32 cu_bitmap[4][4];
	/** Render backend pipe mask. One render backend is CB+DB. */
	__u32 enabled_rb_pipes_mask;
	__u32 num_rb_pipes;
	__u32 num_hw_gfx_contexts;
	__u32 _pad;
	__u64 ids_flags;
	/** Starting virtual address for UMDs. */
	__u64 virtual_address_offset;
	/** The maximum virtual address */
	__u64 virtual_address_max;
	/** Required alignment of virtual addresses. */
	__u32 virtual_address_alignment;
	/** Page table entry - fragment size */
	__u32 pte_fragment_size;
	__u32 gart_page_size;
	/** constant engine ram size*/
	__u32 ce_ram_size;
	/** video memory type info*/
	__u32 vram_type;
	/** video memory bit width*/
	__u32 vram_bit_width;
	/* vce harvesting instance */
	__u32 vce_harvest_config;
};

struct drm_amdgpu_info_hw_ip {
	/** Version of h/w IP */
	__u32  hw_ip_version_major;
	__u32  hw_ip_version_minor;
	/** Capabilities */
	__u64  capabilities_flags;
	/** command buffer address start alignment*/
	__u32  ib_start_alignment;
	/** command buffer size alignment*/
	__u32  ib_size_alignment;
	/** Bitmask of available rings. Bit 0 means ring 0, etc. */
	__u32  available_rings;
	__u32  _pad;
};

#define AMDGPU_VCE_CLOCK_TABLE_ENTRIES		6

struct drm_amdgpu_info_vce_clock_table_entry {
	/** System clock */
	__u32 sclk;
	/** Memory clock */
	__u32 mclk;
	/** VCE clock */
	__u32 eclk;
	__u32 pad;
};

struct drm_amdgpu_info_vce_clock_table {
	struct drm_amdgpu_info_vce_clock_table_entry entries[AMDGPU_VCE_CLOCK_TABLE_ENTRIES];
	__u32 num_valid_entries;
	__u32 pad;
};

/*
 * Supported GPU families
 */
#define AMDGPU_FAMILY_UNKNOWN			0
#define AMDGPU_FAMILY_SI			110 /* Hainan, Oland, Verde, Pitcairn, Tahiti */
#define AMDGPU_FAMILY_CI			120 /* Bonaire, Hawaii */
#define AMDGPU_FAMILY_KV			125 /* Kaveri, Kabini, Mullins */
#define AMDGPU_FAMILY_VI			130 /* Iceland, Tonga */
#define AMDGPU_FAMILY_CZ			135 /* Carrizo, Stoney */

#if defined(__cplusplus)
}
#endif

#endif
