/*
 * Copyright © 2014-2018 Broadcom
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef _V3D_DRM_H_
#define _V3D_DRM_H_

#include "drm.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define DRM_V3D_SUBMIT_CL                         0x00
#define DRM_V3D_WAIT_BO                           0x01
#define DRM_V3D_CREATE_BO                         0x02
#define DRM_V3D_MMAP_BO                           0x03
#define DRM_V3D_GET_PARAM                         0x04
#define DRM_V3D_GET_BO_OFFSET                     0x05
#define DRM_V3D_SUBMIT_TFU                        0x06
#define DRM_V3D_SUBMIT_CSD                        0x07
#define DRM_V3D_PERFMON_CREATE                    0x08
#define DRM_V3D_PERFMON_DESTROY                   0x09
#define DRM_V3D_PERFMON_GET_VALUES                0x0a
#define DRM_V3D_SUBMIT_CPU                        0x0b
#define DRM_V3D_PERFMON_GET_COUNTER               0x0c
#define DRM_V3D_PERFMON_SET_GLOBAL                0x0d

#define DRM_IOCTL_V3D_SUBMIT_CL           DRM_IOWR(DRM_COMMAND_BASE + DRM_V3D_SUBMIT_CL, struct drm_v3d_submit_cl)
#define DRM_IOCTL_V3D_WAIT_BO             DRM_IOWR(DRM_COMMAND_BASE + DRM_V3D_WAIT_BO, struct drm_v3d_wait_bo)
#define DRM_IOCTL_V3D_CREATE_BO           DRM_IOWR(DRM_COMMAND_BASE + DRM_V3D_CREATE_BO, struct drm_v3d_create_bo)
#define DRM_IOCTL_V3D_MMAP_BO             DRM_IOWR(DRM_COMMAND_BASE + DRM_V3D_MMAP_BO, struct drm_v3d_mmap_bo)
#define DRM_IOCTL_V3D_GET_PARAM           DRM_IOWR(DRM_COMMAND_BASE + DRM_V3D_GET_PARAM, struct drm_v3d_get_param)
#define DRM_IOCTL_V3D_GET_BO_OFFSET       DRM_IOWR(DRM_COMMAND_BASE + DRM_V3D_GET_BO_OFFSET, struct drm_v3d_get_bo_offset)
#define DRM_IOCTL_V3D_SUBMIT_TFU          DRM_IOW(DRM_COMMAND_BASE + DRM_V3D_SUBMIT_TFU, struct drm_v3d_submit_tfu)
#define DRM_IOCTL_V3D_SUBMIT_CSD          DRM_IOW(DRM_COMMAND_BASE + DRM_V3D_SUBMIT_CSD, struct drm_v3d_submit_csd)
#define DRM_IOCTL_V3D_PERFMON_CREATE      DRM_IOWR(DRM_COMMAND_BASE + DRM_V3D_PERFMON_CREATE, \
						   struct drm_v3d_perfmon_create)
#define DRM_IOCTL_V3D_PERFMON_DESTROY     DRM_IOWR(DRM_COMMAND_BASE + DRM_V3D_PERFMON_DESTROY, \
						   struct drm_v3d_perfmon_destroy)
#define DRM_IOCTL_V3D_PERFMON_GET_VALUES  DRM_IOWR(DRM_COMMAND_BASE + DRM_V3D_PERFMON_GET_VALUES, \
						   struct drm_v3d_perfmon_get_values)
#define DRM_IOCTL_V3D_SUBMIT_CPU          DRM_IOW(DRM_COMMAND_BASE + DRM_V3D_SUBMIT_CPU, struct drm_v3d_submit_cpu)
#define DRM_IOCTL_V3D_PERFMON_GET_COUNTER DRM_IOWR(DRM_COMMAND_BASE + DRM_V3D_PERFMON_GET_COUNTER, \
						   struct drm_v3d_perfmon_get_counter)
#define DRM_IOCTL_V3D_PERFMON_SET_GLOBAL  DRM_IOW(DRM_COMMAND_BASE + DRM_V3D_PERFMON_SET_GLOBAL, \
						   struct drm_v3d_perfmon_set_global)

#define DRM_V3D_SUBMIT_CL_FLUSH_CACHE             0x01
#define DRM_V3D_SUBMIT_EXTENSION		  0x02

/* struct drm_v3d_extension - ioctl extensions
 *
 * Linked-list of generic extensions where the id identify which struct is
 * pointed by ext_data. Therefore, DRM_V3D_EXT_ID_* is used on id to identify
 * the extension type.
 */
struct drm_v3d_extension {
	__u64 next;
	__u32 id;
#define DRM_V3D_EXT_ID_MULTI_SYNC			0x01
#define DRM_V3D_EXT_ID_CPU_INDIRECT_CSD		0x02
#define DRM_V3D_EXT_ID_CPU_TIMESTAMP_QUERY		0x03
#define DRM_V3D_EXT_ID_CPU_RESET_TIMESTAMP_QUERY	0x04
#define DRM_V3D_EXT_ID_CPU_COPY_TIMESTAMP_QUERY	0x05
#define DRM_V3D_EXT_ID_CPU_RESET_PERFORMANCE_QUERY	0x06
#define DRM_V3D_EXT_ID_CPU_COPY_PERFORMANCE_QUERY	0x07
	__u32 flags; /* mbz */
};

/* struct drm_v3d_sem - wait/signal semaphore
 *
 * If binary semaphore, it only takes syncobj handle and ignores flags and
 * point fields. Point is defined for timeline syncobj feature.
 */
struct drm_v3d_sem {
	__u32 handle; /* syncobj */
	/* rsv below, for future uses */
	__u32 flags;
	__u64 point;  /* for timeline sem support */
	__u64 mbz[2]; /* must be zero, rsv */
};

/* Enum for each of the V3D queues. */
enum v3d_queue {
	V3D_BIN,
	V3D_RENDER,
	V3D_TFU,
	V3D_CSD,
	V3D_CACHE_CLEAN,
	V3D_CPU,
};

/**
 * struct drm_v3d_multi_sync - ioctl extension to add support multiples
 * syncobjs for commands submission.
 *
 * When an extension of DRM_V3D_EXT_ID_MULTI_SYNC id is defined, it points to
 * this extension to define wait and signal dependencies, instead of single
 * in/out sync entries on submitting commands. The field flags is used to
 * determine the stage to set wait dependencies.
 */
struct drm_v3d_multi_sync {
	struct drm_v3d_extension base;
	/* Array of wait and signal semaphores */
	__u64 in_syncs;
	__u64 out_syncs;

	/* Number of entries */
	__u32 in_sync_count;
	__u32 out_sync_count;

	/* set the stage (v3d_queue) to sync */
	__u32 wait_stage;

	__u32 pad; /* mbz */
};

/**
 * struct drm_v3d_submit_cl - ioctl argument for submitting commands to the 3D
 * engine.
 *
 * This asks the kernel to have the GPU execute an optional binner
 * command list, and a render command list.
 *
 * The L1T, slice, L2C, L2T, and GCA caches will be flushed before
 * each CL executes.  The VCD cache should be flushed (if necessary)
 * by the submitted CLs.  The TLB writes are guaranteed to have been
 * flushed by the time the render done IRQ happens, which is the
 * trigger for out_sync.  Any dirtying of cachelines by the job (only
 * possible using TMU writes) must be flushed by the caller using the
 * DRM_V3D_SUBMIT_CL_FLUSH_CACHE_FLAG flag.
 */
struct drm_v3d_submit_cl {
	/* Pointer to the binner command list.
	 *
	 * This is the first set of commands executed, which runs the
	 * coordinate shader to determine where primitives land on the screen,
	 * then writes out the state updates and draw calls necessary per tile
	 * to the tile allocation BO.
	 *
	 * This BCL will block on any previous BCL submitted on the
	 * same FD, but not on any RCL or BCLs submitted by other
	 * clients -- that is left up to the submitter to control
	 * using in_sync_bcl if necessary.
	 */
	__u32 bcl_start;

	/** End address of the BCL (first byte after the BCL) */
	__u32 bcl_end;

	/* Offset of the render command list.
	 *
	 * This is the second set of commands executed, which will either
	 * execute the tiles that have been set up by the BCL, or a fixed set
	 * of tiles (in the case of RCL-only blits).
	 *
	 * This RCL will block on this submit's BCL, and any previous
	 * RCL submitted on the same FD, but not on any RCL or BCLs
	 * submitted by other clients -- that is left up to the
	 * submitter to control using in_sync_rcl if necessary.
	 */
	__u32 rcl_start;

	/** End address of the RCL (first byte after the RCL) */
	__u32 rcl_end;

	/** An optional sync object to wait on before starting the BCL. */
	__u32 in_sync_bcl;
	/** An optional sync object to wait on before starting the RCL. */
	__u32 in_sync_rcl;
	/** An optional sync object to place the completion fence in. */
	__u32 out_sync;

	/* Offset of the tile alloc memory
	 *
	 * This is optional on V3D 3.3 (where the CL can set the value) but
	 * required on V3D 4.1.
	 */
	__u32 qma;

	/** Size of the tile alloc memory. */
	__u32 qms;

	/** Offset of the tile state data array. */
	__u32 qts;

	/* Pointer to a u32 array of the BOs that are referenced by the job.
	 */
	__u64 bo_handles;

	/* Number of BO handles passed in (size is that times 4). */
	__u32 bo_handle_count;

	/* DRM_V3D_SUBMIT_* properties */
	__u32 flags;

	/* ID of the perfmon to attach to this job. 0 means no perfmon. */
	__u32 perfmon_id;

	__u32 pad;

	/* Pointer to an array of ioctl extensions*/
	__u64 extensions;
};

/**
 * struct drm_v3d_wait_bo - ioctl argument for waiting for
 * completion of the last DRM_V3D_SUBMIT_CL on a BO.
 *
 * This is useful for cases where multiple processes might be
 * rendering to a BO and you want to wait for all rendering to be
 * completed.
 */
struct drm_v3d_wait_bo {
	__u32 handle;
	__u32 pad;
	__u64 timeout_ns;
};

/**
 * struct drm_v3d_create_bo - ioctl argument for creating V3D BOs.
 *
 * There are currently no values for the flags argument, but it may be
 * used in a future extension.
 */
struct drm_v3d_create_bo {
	__u32 size;
	__u32 flags;
	/** Returned GEM handle for the BO. */
	__u32 handle;
	/**
	 * Returned offset for the BO in the V3D address space.  This offset
	 * is private to the DRM fd and is valid for the lifetime of the GEM
	 * handle.
	 *
	 * This offset value will always be nonzero, since various HW
	 * units treat 0 specially.
	 */
	__u32 offset;
};

/**
 * struct drm_v3d_mmap_bo - ioctl argument for mapping V3D BOs.
 *
 * This doesn't actually perform an mmap.  Instead, it returns the
 * offset you need to use in an mmap on the DRM device node.  This
 * means that tools like valgrind end up knowing about the mapped
 * memory.
 *
 * There are currently no values for the flags argument, but it may be
 * used in a future extension.
 */
struct drm_v3d_mmap_bo {
	/** Handle for the object being mapped. */
	__u32 handle;
	__u32 flags;
	/** offset into the drm node to use for subsequent mmap call. */
	__u64 offset;
};

enum drm_v3d_param {
	DRM_V3D_PARAM_V3D_UIFCFG,
	DRM_V3D_PARAM_V3D_HUB_IDENT1,
	DRM_V3D_PARAM_V3D_HUB_IDENT2,
	DRM_V3D_PARAM_V3D_HUB_IDENT3,
	DRM_V3D_PARAM_V3D_CORE0_IDENT0,
	DRM_V3D_PARAM_V3D_CORE0_IDENT1,
	DRM_V3D_PARAM_V3D_CORE0_IDENT2,
	DRM_V3D_PARAM_SUPPORTS_TFU,
	DRM_V3D_PARAM_SUPPORTS_CSD,
	DRM_V3D_PARAM_SUPPORTS_CACHE_FLUSH,
	DRM_V3D_PARAM_SUPPORTS_PERFMON,
	DRM_V3D_PARAM_SUPPORTS_MULTISYNC_EXT,
	DRM_V3D_PARAM_SUPPORTS_CPU_QUEUE,
	DRM_V3D_PARAM_MAX_PERF_COUNTERS,
	DRM_V3D_PARAM_SUPPORTS_SUPER_PAGES,
	DRM_V3D_PARAM_GLOBAL_RESET_COUNTER,
	DRM_V3D_PARAM_CONTEXT_RESET_COUNTER,
};

struct drm_v3d_get_param {
	__u32 param;
	__u32 pad;
	__u64 value;
};

/**
 * Returns the offset for the BO in the V3D address space for this DRM fd.
 * This is the same value returned by drm_v3d_create_bo, if that was called
 * from this DRM fd.
 */
struct drm_v3d_get_bo_offset {
	__u32 handle;
	__u32 offset;
};

struct drm_v3d_submit_tfu {
	__u32 icfg;
	__u32 iia;
	__u32 iis;
	__u32 ica;
	__u32 iua;
	__u32 ioa;
	__u32 ios;
	__u32 coef[4];
	/* First handle is the output BO, following are other inputs.
	 * 0 for unused.
	 */
	__u32 bo_handles[4];
	/* sync object to block on before running the TFU job.  Each TFU
	 * job will execute in the order submitted to its FD.  Synchronization
	 * against rendering jobs requires using sync objects.
	 */
	__u32 in_sync;
	/* Sync object to signal when the TFU job is done. */
	__u32 out_sync;

	__u32 flags;

	/* Pointer to an array of ioctl extensions*/
	__u64 extensions;

	struct {
		__u32 ioc;
		__u32 pad;
	} v71;
};

/* Submits a compute shader for dispatch.  This job will block on any
 * previous compute shaders submitted on this fd, and any other
 * synchronization must be performed with in_sync/out_sync.
 */
struct drm_v3d_submit_csd {
	__u32 cfg[7];
	__u32 coef[4];

	/* Pointer to a u32 array of the BOs that are referenced by the job.
	 */
	__u64 bo_handles;

	/* Number of BO handles passed in (size is that times 4). */
	__u32 bo_handle_count;

	/* sync object to block on before running the CSD job.  Each
	 * CSD job will execute in the order submitted to its FD.
	 * Synchronization against rendering/TFU jobs or CSD from
	 * other fds requires using sync objects.
	 */
	__u32 in_sync;
	/* Sync object to signal when the CSD job is done. */
	__u32 out_sync;

	/* ID of the perfmon to attach to this job. 0 means no perfmon. */
	__u32 perfmon_id;

	/* Pointer to an array of ioctl extensions*/
	__u64 extensions;

	__u32 flags;

	__u32 pad;
};

/**
 * struct drm_v3d_indirect_csd - ioctl extension for the CPU job to create an
 * indirect CSD
 *
 * When an extension of DRM_V3D_EXT_ID_CPU_INDIRECT_CSD id is defined, it
 * points to this extension to define a indirect CSD submission. It creates a
 * CPU job linked to a CSD job. The CPU job waits for the indirect CSD
 * dependencies and, once they are signaled, it updates the CSD job config
 * before allowing the CSD job execution.
 */
struct drm_v3d_indirect_csd {
	struct drm_v3d_extension base;

	/* Indirect CSD */
	struct drm_v3d_submit_csd submit;

	/* Handle of the indirect BO, that should be also attached to the
	 * indirect CSD.
	 */
	__u32 indirect;

	/* Offset within the BO where the workgroup counts are stored */
	__u32 offset;

	/* Workgroups size */
	__u32 wg_size;

	/* Indices of the uniforms with the workgroup dispatch counts
	 * in the uniform stream. If the uniform rewrite is not needed,
	 * the offset must be 0xffffffff.
	 */
	__u32 wg_uniform_offsets[3];
};

/**
 * struct drm_v3d_timestamp_query - ioctl extension for the CPU job to calculate
 * a timestamp query
 *
 * When an extension DRM_V3D_EXT_ID_TIMESTAMP_QUERY is defined, it points to
 * this extension to define a timestamp query submission. This CPU job will
 * calculate the timestamp query and update the query value within the
 * timestamp BO. Moreover, it will signal the timestamp syncobj to indicate
 * query availability.
 */
struct drm_v3d_timestamp_query {
	struct drm_v3d_extension base;

	/* Array of queries' offsets within the timestamp BO for their value */
	__u64 offsets;

	/* Array of timestamp's syncobjs to indicate its availability */
	__u64 syncs;

	/* Number of queries */
	__u32 count;

	/* mbz */
	__u32 pad;
};

/**
 * struct drm_v3d_reset_timestamp_query - ioctl extension for the CPU job to
 * reset timestamp queries
 *
 * When an extension DRM_V3D_EXT_ID_CPU_RESET_TIMESTAMP_QUERY is defined, it
 * points to this extension to define a reset timestamp submission. This CPU
 * job will reset the timestamp queries based on value offset of the first
 * query. Moreover, it will reset the timestamp syncobj to reset query
 * availability.
 */
struct drm_v3d_reset_timestamp_query {
	struct drm_v3d_extension base;

	/* Array of timestamp's syncobjs to indicate its availability */
	__u64 syncs;

	/* Offset of the first query within the timestamp BO for its value */
	__u32 offset;

	/* Number of queries */
	__u32 count;
};

/**
 * struct drm_v3d_copy_timestamp_query - ioctl extension for the CPU job to copy
 * query results to a buffer
 *
 * When an extension DRM_V3D_EXT_ID_CPU_COPY_TIMESTAMP_QUERY is defined, it
 * points to this extension to define a copy timestamp query submission. This
 * CPU job will copy the timestamp queries results to a BO with the offset
 * and stride defined in the extension.
 */
struct drm_v3d_copy_timestamp_query {
	struct drm_v3d_extension base;

	/* Define if should write to buffer using 64 or 32 bits */
	__u8 do_64bit;

	/* Define if it can write to buffer even if the query is not available */
	__u8 do_partial;

	/* Define if it should write availability bit to buffer */
	__u8 availability_bit;

	/* mbz */
	__u8 pad;

	/* Offset of the buffer in the BO */
	__u32 offset;

	/* Stride of the buffer in the BO */
	__u32 stride;

	/* Number of queries */
	__u32 count;

	/* Array of queries' offsets within the timestamp BO for their value */
	__u64 offsets;

	/* Array of timestamp's syncobjs to indicate its availability */
	__u64 syncs;
};

/**
 * struct drm_v3d_reset_performance_query - ioctl extension for the CPU job to
 * reset performance queries
 *
 * When an extension DRM_V3D_EXT_ID_CPU_RESET_PERFORMANCE_QUERY is defined, it
 * points to this extension to define a reset performance submission. This CPU
 * job will reset the performance queries by resetting the values of the
 * performance monitors. Moreover, it will reset the syncobj to reset query
 * availability.
 */
struct drm_v3d_reset_performance_query {
	struct drm_v3d_extension base;

	/* Array of performance queries's syncobjs to indicate its availability */
	__u64 syncs;

	/* Number of queries */
	__u32 count;

	/* Number of performance monitors */
	__u32 nperfmons;

	/* Array of u64 user-pointers that point to an array of kperfmon_ids */
	__u64 kperfmon_ids;
};

/**
 * struct drm_v3d_copy_performance_query - ioctl extension for the CPU job to copy
 * performance query results to a buffer
 *
 * When an extension DRM_V3D_EXT_ID_CPU_COPY_PERFORMANCE_QUERY is defined, it
 * points to this extension to define a copy performance query submission. This
 * CPU job will copy the performance queries results to a BO with the offset
 * and stride defined in the extension.
 */
struct drm_v3d_copy_performance_query {
	struct drm_v3d_extension base;

	/* Define if should write to buffer using 64 or 32 bits */
	__u8 do_64bit;

	/* Define if it can write to buffer even if the query is not available */
	__u8 do_partial;

	/* Define if it should write availability bit to buffer */
	__u8 availability_bit;

	/* mbz */
	__u8 pad;

	/* Offset of the buffer in the BO */
	__u32 offset;

	/* Stride of the buffer in the BO */
	__u32 stride;

	/* Number of performance monitors */
	__u32 nperfmons;

	/* Number of performance counters related to this query pool */
	__u32 ncounters;

	/* Number of queries */
	__u32 count;

	/* Array of performance queries's syncobjs to indicate its availability */
	__u64 syncs;

	/* Array of u64 user-pointers that point to an array of kperfmon_ids */
	__u64 kperfmon_ids;
};

struct drm_v3d_submit_cpu {
	/* Pointer to a u32 array of the BOs that are referenced by the job.
	 *
	 * For DRM_V3D_EXT_ID_CPU_INDIRECT_CSD, it must contain only one BO,
	 * that contains the workgroup counts.
	 *
	 * For DRM_V3D_EXT_ID_TIMESTAMP_QUERY, it must contain only one BO,
	 * that will contain the timestamp.
	 *
	 * For DRM_V3D_EXT_ID_CPU_RESET_TIMESTAMP_QUERY, it must contain only
	 * one BO, that contains the timestamp.
	 *
	 * For DRM_V3D_EXT_ID_CPU_COPY_TIMESTAMP_QUERY, it must contain two
	 * BOs. The first is the BO where the timestamp queries will be written
	 * to. The second is the BO that contains the timestamp.
	 *
	 * For DRM_V3D_EXT_ID_CPU_RESET_PERFORMANCE_QUERY, it must contain no
	 * BOs.
	 *
	 * For DRM_V3D_EXT_ID_CPU_COPY_PERFORMANCE_QUERY, it must contain one
	 * BO, where the performance queries will be written.
	 */
	__u64 bo_handles;

	/* Number of BO handles passed in (size is that times 4). */
	__u32 bo_handle_count;

	__u32 flags;

	/* Pointer to an array of ioctl extensions*/
	__u64 extensions;
};

/* The performance counters index represented by this enum are deprecated and
 * must no longer be used. These counters are only valid for V3D 4.2.
 *
 * In order to check for performance counter information,
 * use DRM_IOCTL_V3D_PERFMON_GET_COUNTER.
 *
 * Don't use V3D_PERFCNT_NUM to retrieve the maximum number of performance
 * counters. You should use DRM_IOCTL_V3D_GET_PARAM with the following
 * parameter: DRM_V3D_PARAM_MAX_PERF_COUNTERS.
 */
enum {
	V3D_PERFCNT_FEP_VALID_PRIMTS_NO_PIXELS,
	V3D_PERFCNT_FEP_VALID_PRIMS,
	V3D_PERFCNT_FEP_EZ_NFCLIP_QUADS,
	V3D_PERFCNT_FEP_VALID_QUADS,
	V3D_PERFCNT_TLB_QUADS_STENCIL_FAIL,
	V3D_PERFCNT_TLB_QUADS_STENCILZ_FAIL,
	V3D_PERFCNT_TLB_QUADS_STENCILZ_PASS,
	V3D_PERFCNT_TLB_QUADS_ZERO_COV,
	V3D_PERFCNT_TLB_QUADS_NONZERO_COV,
	V3D_PERFCNT_TLB_QUADS_WRITTEN,
	V3D_PERFCNT_PTB_PRIM_VIEWPOINT_DISCARD,
	V3D_PERFCNT_PTB_PRIM_CLIP,
	V3D_PERFCNT_PTB_PRIM_REV,
	V3D_PERFCNT_QPU_IDLE_CYCLES,
	V3D_PERFCNT_QPU_ACTIVE_CYCLES_VERTEX_COORD_USER,
	V3D_PERFCNT_QPU_ACTIVE_CYCLES_FRAG,
	V3D_PERFCNT_QPU_CYCLES_VALID_INSTR,
	V3D_PERFCNT_QPU_CYCLES_TMU_STALL,
	V3D_PERFCNT_QPU_CYCLES_SCOREBOARD_STALL,
	V3D_PERFCNT_QPU_CYCLES_VARYINGS_STALL,
	V3D_PERFCNT_QPU_IC_HIT,
	V3D_PERFCNT_QPU_IC_MISS,
	V3D_PERFCNT_QPU_UC_HIT,
	V3D_PERFCNT_QPU_UC_MISS,
	V3D_PERFCNT_TMU_TCACHE_ACCESS,
	V3D_PERFCNT_TMU_TCACHE_MISS,
	V3D_PERFCNT_VPM_VDW_STALL,
	V3D_PERFCNT_VPM_VCD_STALL,
	V3D_PERFCNT_BIN_ACTIVE,
	V3D_PERFCNT_RDR_ACTIVE,
	V3D_PERFCNT_L2T_HITS,
	V3D_PERFCNT_L2T_MISSES,
	V3D_PERFCNT_CYCLE_COUNT,
	V3D_PERFCNT_QPU_CYCLES_STALLED_VERTEX_COORD_USER,
	V3D_PERFCNT_QPU_CYCLES_STALLED_FRAGMENT,
	V3D_PERFCNT_PTB_PRIMS_BINNED,
	V3D_PERFCNT_AXI_WRITES_WATCH_0,
	V3D_PERFCNT_AXI_READS_WATCH_0,
	V3D_PERFCNT_AXI_WRITE_STALLS_WATCH_0,
	V3D_PERFCNT_AXI_READ_STALLS_WATCH_0,
	V3D_PERFCNT_AXI_WRITE_BYTES_WATCH_0,
	V3D_PERFCNT_AXI_READ_BYTES_WATCH_0,
	V3D_PERFCNT_AXI_WRITES_WATCH_1,
	V3D_PERFCNT_AXI_READS_WATCH_1,
	V3D_PERFCNT_AXI_WRITE_STALLS_WATCH_1,
	V3D_PERFCNT_AXI_READ_STALLS_WATCH_1,
	V3D_PERFCNT_AXI_WRITE_BYTES_WATCH_1,
	V3D_PERFCNT_AXI_READ_BYTES_WATCH_1,
	V3D_PERFCNT_TLB_PARTIAL_QUADS,
	V3D_PERFCNT_TMU_CONFIG_ACCESSES,
	V3D_PERFCNT_L2T_NO_ID_STALL,
	V3D_PERFCNT_L2T_COM_QUE_STALL,
	V3D_PERFCNT_L2T_TMU_WRITES,
	V3D_PERFCNT_TMU_ACTIVE_CYCLES,
	V3D_PERFCNT_TMU_STALLED_CYCLES,
	V3D_PERFCNT_CLE_ACTIVE,
	V3D_PERFCNT_L2T_TMU_READS,
	V3D_PERFCNT_L2T_CLE_READS,
	V3D_PERFCNT_L2T_VCD_READS,
	V3D_PERFCNT_L2T_TMUCFG_READS,
	V3D_PERFCNT_L2T_SLC0_READS,
	V3D_PERFCNT_L2T_SLC1_READS,
	V3D_PERFCNT_L2T_SLC2_READS,
	V3D_PERFCNT_L2T_TMU_W_MISSES,
	V3D_PERFCNT_L2T_TMU_R_MISSES,
	V3D_PERFCNT_L2T_CLE_MISSES,
	V3D_PERFCNT_L2T_VCD_MISSES,
	V3D_PERFCNT_L2T_TMUCFG_MISSES,
	V3D_PERFCNT_L2T_SLC0_MISSES,
	V3D_PERFCNT_L2T_SLC1_MISSES,
	V3D_PERFCNT_L2T_SLC2_MISSES,
	V3D_PERFCNT_CORE_MEM_WRITES,
	V3D_PERFCNT_L2T_MEM_WRITES,
	V3D_PERFCNT_PTB_MEM_WRITES,
	V3D_PERFCNT_TLB_MEM_WRITES,
	V3D_PERFCNT_CORE_MEM_READS,
	V3D_PERFCNT_L2T_MEM_READS,
	V3D_PERFCNT_PTB_MEM_READS,
	V3D_PERFCNT_PSE_MEM_READS,
	V3D_PERFCNT_TLB_MEM_READS,
	V3D_PERFCNT_GMP_MEM_READS,
	V3D_PERFCNT_PTB_W_MEM_WORDS,
	V3D_PERFCNT_TLB_W_MEM_WORDS,
	V3D_PERFCNT_PSE_R_MEM_WORDS,
	V3D_PERFCNT_TLB_R_MEM_WORDS,
	V3D_PERFCNT_TMU_MRU_HITS,
	V3D_PERFCNT_COMPUTE_ACTIVE,
	V3D_PERFCNT_NUM,
};

#define DRM_V3D_MAX_PERF_COUNTERS                 32

struct drm_v3d_perfmon_create {
	__u32 id;
	__u32 ncounters;
	__u8 counters[DRM_V3D_MAX_PERF_COUNTERS];
};

struct drm_v3d_perfmon_destroy {
	__u32 id;
};

/*
 * Returns the values of the performance counters tracked by this
 * perfmon (as an array of ncounters u64 values).
 *
 * No implicit synchronization is performed, so the user has to
 * guarantee that any jobs using this perfmon have already been
 * completed  (probably by blocking on the seqno returned by the
 * last exec that used the perfmon).
 */
struct drm_v3d_perfmon_get_values {
	__u32 id;
	__u32 pad;
	__u64 values_ptr;
};

#define DRM_V3D_PERFCNT_MAX_NAME 64
#define DRM_V3D_PERFCNT_MAX_CATEGORY 32
#define DRM_V3D_PERFCNT_MAX_DESCRIPTION 256

/**
 * struct drm_v3d_perfmon_get_counter - ioctl to get the description of a
 * performance counter
 *
 * As userspace needs to retrieve information about the performance counters
 * available, this IOCTL allows users to get information about a performance
 * counter (name, category and description).
 */
struct drm_v3d_perfmon_get_counter {
	/*
	 * Counter ID
	 *
	 * Must be smaller than the maximum number of performance counters, which
	 * can be retrieve through DRM_V3D_PARAM_MAX_PERF_COUNTERS.
	 */
	__u8 counter;

	/* Name of the counter */
	__u8 name[DRM_V3D_PERFCNT_MAX_NAME];

	/* Category of the counter */
	__u8 category[DRM_V3D_PERFCNT_MAX_CATEGORY];

	/* Description of the counter */
	__u8 description[DRM_V3D_PERFCNT_MAX_DESCRIPTION];

	/* mbz */
	__u8 reserved[7];
};

#define DRM_V3D_PERFMON_CLEAR_GLOBAL    0x0001

/**
 * struct drm_v3d_perfmon_set_global - ioctl to define a global performance
 * monitor
 *
 * The global performance monitor will be used for all jobs. If a global
 * performance monitor is defined, jobs with a self-defined performance
 * monitor won't be allowed.
 */
struct drm_v3d_perfmon_set_global {
	__u32 flags;
	__u32 id;
};

#if defined(__cplusplus)
}
#endif

#endif /* _V3D_DRM_H_ */
