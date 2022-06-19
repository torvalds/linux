/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2019-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _UAPI_BASE_JM_KERNEL_H_
#define _UAPI_BASE_JM_KERNEL_H_

#include <linux/types.h>
#include "../mali_base_common_kernel.h"

/* Memory allocation, access/hint flags & mask specific to JM GPU.
 *
 * See base_mem_alloc_flags.
 */

/* Used as BASE_MEM_FIXED in other backends */
#define BASE_MEM_RESERVED_BIT_8 ((base_mem_alloc_flags)1 << 8)

/**
 * BASE_MEM_RESERVED_BIT_19 - Bit 19 is reserved.
 *
 * Do not remove, use the next unreserved bit for new flags
 */
#define BASE_MEM_RESERVED_BIT_19 ((base_mem_alloc_flags)1 << 19)

/**
 * BASE_MEM_TILER_ALIGN_TOP - Memory starting from the end of the initial commit is aligned
 * to 'extension' pages, where 'extension' must be a power of 2 and no more than
 * BASE_MEM_TILER_ALIGN_TOP_EXTENSION_MAX_PAGES
 */
#define BASE_MEM_TILER_ALIGN_TOP ((base_mem_alloc_flags)1 << 20)

/* Use the GPU VA chosen by the kernel client */
#define BASE_MEM_FLAG_MAP_FIXED ((base_mem_alloc_flags)1 << 27)

/* Force trimming of JIT allocations when creating a new allocation */
#define BASEP_MEM_PERFORM_JIT_TRIM ((base_mem_alloc_flags)1 << 29)

/* Note that the number of bits used for base_mem_alloc_flags
 * must be less than BASE_MEM_FLAGS_NR_BITS !!!
 */

/* A mask of all the flags which are only valid for allocations within kbase,
 * and may not be passed from user space.
 */
#define BASEP_MEM_FLAGS_KERNEL_ONLY \
	(BASEP_MEM_PERMANENT_KERNEL_MAPPING | BASEP_MEM_NO_USER_FREE | \
	 BASE_MEM_FLAG_MAP_FIXED | BASEP_MEM_PERFORM_JIT_TRIM)

/* A mask of all currently reserved flags
 */
#define BASE_MEM_FLAGS_RESERVED \
	(BASE_MEM_RESERVED_BIT_8 | BASE_MEM_RESERVED_BIT_19)


/* Similar to BASE_MEM_TILER_ALIGN_TOP, memory starting from the end of the
 * initial commit is aligned to 'extension' pages, where 'extension' must be a power
 * of 2 and no more than BASE_MEM_TILER_ALIGN_TOP_EXTENSION_MAX_PAGES
 */
#define BASE_JIT_ALLOC_MEM_TILER_ALIGN_TOP  (1 << 0)

/**
 * BASE_JIT_ALLOC_HEAP_INFO_IS_SIZE - If set, the heap info address points
 * to a __u32 holding the used size in bytes;
 * otherwise it points to a __u64 holding the lowest address of unused memory.
 */
#define BASE_JIT_ALLOC_HEAP_INFO_IS_SIZE  (1 << 1)

/**
 * BASE_JIT_ALLOC_VALID_FLAGS - Valid set of just-in-time memory allocation flags
 *
 * Note: BASE_JIT_ALLOC_HEAP_INFO_IS_SIZE cannot be set if heap_info_gpu_addr
 * in %base_jit_alloc_info is 0 (atom with BASE_JIT_ALLOC_HEAP_INFO_IS_SIZE set
 * and heap_info_gpu_addr being 0 will be rejected).
 */
#define BASE_JIT_ALLOC_VALID_FLAGS \
	(BASE_JIT_ALLOC_MEM_TILER_ALIGN_TOP | BASE_JIT_ALLOC_HEAP_INFO_IS_SIZE)

/* Bitpattern describing the ::base_context_create_flags that can be
 * passed to base_context_init()
 */
#define BASEP_CONTEXT_CREATE_ALLOWED_FLAGS \
	(BASE_CONTEXT_CCTX_EMBEDDED | BASEP_CONTEXT_CREATE_KERNEL_FLAGS)

/*
 * Private flags used on the base context
 *
 * These start at bit 31, and run down to zero.
 *
 * They share the same space as base_context_create_flags, and so must
 * not collide with them.
 */

/* Private flag tracking whether job descriptor dumping is disabled */
#define BASEP_CONTEXT_FLAG_JOB_DUMP_DISABLED \
	((base_context_create_flags)(1 << 31))

/* Flags for base tracepoint specific to JM */
#define BASE_TLSTREAM_FLAGS_MASK (BASE_TLSTREAM_ENABLE_LATENCY_TRACEPOINTS | \
		BASE_TLSTREAM_JOB_DUMPING_ENABLED)
/*
 * Dependency stuff, keep it private for now. May want to expose it if
 * we decide to make the number of semaphores a configurable
 * option.
 */
#define BASE_JD_ATOM_COUNT              256

/* Maximum number of concurrent render passes.
 */
#define BASE_JD_RP_COUNT (256)

/* Set/reset values for a software event */
#define BASE_JD_SOFT_EVENT_SET             ((unsigned char)1)
#define BASE_JD_SOFT_EVENT_RESET           ((unsigned char)0)

/**
 * struct base_jd_udata - Per-job data
 *
 * @blob: per-job data array
 *
 * This structure is used to store per-job data, and is completely unused
 * by the Base driver. It can be used to store things such as callback
 * function pointer, data to handle job completion. It is guaranteed to be
 * untouched by the Base driver.
 */
struct base_jd_udata {
	__u64 blob[2];
};

/**
 * typedef base_jd_dep_type - Job dependency type.
 *
 * A flags field will be inserted into the atom structure to specify whether a
 * dependency is a data or ordering dependency (by putting it before/after
 * 'core_req' in the structure it should be possible to add without changing
 * the structure size).
 * When the flag is set for a particular dependency to signal that it is an
 * ordering only dependency then errors will not be propagated.
 */
typedef __u8 base_jd_dep_type;

#define BASE_JD_DEP_TYPE_INVALID  (0)       /**< Invalid dependency */
#define BASE_JD_DEP_TYPE_DATA     (1U << 0) /**< Data dependency */
#define BASE_JD_DEP_TYPE_ORDER    (1U << 1) /**< Order dependency */

/**
 * typedef base_jd_core_req - Job chain hardware requirements.
 *
 * A job chain must specify what GPU features it needs to allow the
 * driver to schedule the job correctly.  By not specifying the
 * correct settings can/will cause an early job termination.  Multiple
 * values can be ORed together to specify multiple requirements.
 * Special case is ::BASE_JD_REQ_DEP, which is used to express complex
 * dependencies, and that doesn't execute anything on the hardware.
 */
typedef __u32 base_jd_core_req;

/* Requirements that come from the HW */

/* No requirement, dependency only
 */
#define BASE_JD_REQ_DEP ((base_jd_core_req)0)

/* Requires fragment shaders
 */
#define BASE_JD_REQ_FS  ((base_jd_core_req)1 << 0)

/* Requires compute shaders
 *
 * This covers any of the following GPU job types:
 * - Vertex Shader Job
 * - Geometry Shader Job
 * - An actual Compute Shader Job
 *
 * Compare this with BASE_JD_REQ_ONLY_COMPUTE, which specifies that the
 * job is specifically just the "Compute Shader" job type, and not the "Vertex
 * Shader" nor the "Geometry Shader" job type.
 */
#define BASE_JD_REQ_CS ((base_jd_core_req)1 << 1)

/* Requires tiling */
#define BASE_JD_REQ_T  ((base_jd_core_req)1 << 2)

/* Requires cache flushes */
#define BASE_JD_REQ_CF ((base_jd_core_req)1 << 3)

/* Requires value writeback */
#define BASE_JD_REQ_V  ((base_jd_core_req)1 << 4)

/* SW-only requirements - the HW does not expose these as part of the job slot
 * capabilities
 */

/* Requires fragment job with AFBC encoding */
#define BASE_JD_REQ_FS_AFBC  ((base_jd_core_req)1 << 13)

/* SW-only requirement: coalesce completion events.
 * If this bit is set then completion of this atom will not cause an event to
 * be sent to userspace, whether successful or not; completion events will be
 * deferred until an atom completes which does not have this bit set.
 *
 * This bit may not be used in combination with BASE_JD_REQ_EXTERNAL_RESOURCES.
 */
#define BASE_JD_REQ_EVENT_COALESCE ((base_jd_core_req)1 << 5)

/* SW Only requirement: the job chain requires a coherent core group. We don't
 * mind which coherent core group is used.
 */
#define BASE_JD_REQ_COHERENT_GROUP  ((base_jd_core_req)1 << 6)

/* SW Only requirement: The performance counters should be enabled only when
 * they are needed, to reduce power consumption.
 */
#define BASE_JD_REQ_PERMON               ((base_jd_core_req)1 << 7)

/* SW Only requirement: External resources are referenced by this atom.
 *
 * This bit may not be used in combination with BASE_JD_REQ_EVENT_COALESCE and
 * BASE_JD_REQ_SOFT_EVENT_WAIT.
 */
#define BASE_JD_REQ_EXTERNAL_RESOURCES   ((base_jd_core_req)1 << 8)

/* SW Only requirement: Software defined job. Jobs with this bit set will not be
 * submitted to the hardware but will cause some action to happen within the
 * driver
 */
#define BASE_JD_REQ_SOFT_JOB        ((base_jd_core_req)1 << 9)

#define BASE_JD_REQ_SOFT_DUMP_CPU_GPU_TIME      (BASE_JD_REQ_SOFT_JOB | 0x1)
#define BASE_JD_REQ_SOFT_FENCE_TRIGGER          (BASE_JD_REQ_SOFT_JOB | 0x2)
#define BASE_JD_REQ_SOFT_FENCE_WAIT             (BASE_JD_REQ_SOFT_JOB | 0x3)

/* 0x4 RESERVED for now */

/* SW only requirement: event wait/trigger job.
 *
 * - BASE_JD_REQ_SOFT_EVENT_WAIT: this job will block until the event is set.
 * - BASE_JD_REQ_SOFT_EVENT_SET: this job sets the event, thus unblocks the
 *   other waiting jobs. It completes immediately.
 * - BASE_JD_REQ_SOFT_EVENT_RESET: this job resets the event, making it
 *   possible for other jobs to wait upon. It completes immediately.
 */
#define BASE_JD_REQ_SOFT_EVENT_WAIT             (BASE_JD_REQ_SOFT_JOB | 0x5)
#define BASE_JD_REQ_SOFT_EVENT_SET              (BASE_JD_REQ_SOFT_JOB | 0x6)
#define BASE_JD_REQ_SOFT_EVENT_RESET            (BASE_JD_REQ_SOFT_JOB | 0x7)

#define BASE_JD_REQ_SOFT_DEBUG_COPY             (BASE_JD_REQ_SOFT_JOB | 0x8)

/* SW only requirement: Just In Time allocation
 *
 * This job requests a single or multiple just-in-time allocations through a
 * list of base_jit_alloc_info structure which is passed via the jc element of
 * the atom. The number of base_jit_alloc_info structures present in the
 * list is passed via the nr_extres element of the atom
 *
 * It should be noted that the id entry in base_jit_alloc_info must not
 * be reused until it has been released via BASE_JD_REQ_SOFT_JIT_FREE.
 *
 * Should this soft job fail it is expected that a BASE_JD_REQ_SOFT_JIT_FREE
 * soft job to free the JIT allocation is still made.
 *
 * The job will complete immediately.
 */
#define BASE_JD_REQ_SOFT_JIT_ALLOC              (BASE_JD_REQ_SOFT_JOB | 0x9)

/* SW only requirement: Just In Time free
 *
 * This job requests a single or multiple just-in-time allocations created by
 * BASE_JD_REQ_SOFT_JIT_ALLOC to be freed. The ID list of the just-in-time
 * allocations is passed via the jc element of the atom.
 *
 * The job will complete immediately.
 */
#define BASE_JD_REQ_SOFT_JIT_FREE               (BASE_JD_REQ_SOFT_JOB | 0xa)

/* SW only requirement: Map external resource
 *
 * This job requests external resource(s) are mapped once the dependencies
 * of the job have been satisfied. The list of external resources are
 * passed via the jc element of the atom which is a pointer to a
 * base_external_resource_list.
 */
#define BASE_JD_REQ_SOFT_EXT_RES_MAP            (BASE_JD_REQ_SOFT_JOB | 0xb)

/* SW only requirement: Unmap external resource
 *
 * This job requests external resource(s) are unmapped once the dependencies
 * of the job has been satisfied. The list of external resources are
 * passed via the jc element of the atom which is a pointer to a
 * base_external_resource_list.
 */
#define BASE_JD_REQ_SOFT_EXT_RES_UNMAP          (BASE_JD_REQ_SOFT_JOB | 0xc)

/* HW Requirement: Requires Compute shaders (but not Vertex or Geometry Shaders)
 *
 * This indicates that the Job Chain contains GPU jobs of the 'Compute
 * Shaders' type.
 *
 * In contrast to BASE_JD_REQ_CS, this does not indicate that the Job
 * Chain contains 'Geometry Shader' or 'Vertex Shader' jobs.
 */
#define BASE_JD_REQ_ONLY_COMPUTE    ((base_jd_core_req)1 << 10)

/* HW Requirement: Use the base_jd_atom::device_nr field to specify a
 * particular core group
 *
 * If both BASE_JD_REQ_COHERENT_GROUP and this flag are set, this flag
 * takes priority
 *
 * This is only guaranteed to work for BASE_JD_REQ_ONLY_COMPUTE atoms.
 */
#define BASE_JD_REQ_SPECIFIC_COHERENT_GROUP ((base_jd_core_req)1 << 11)

/* SW Flag: If this bit is set then the successful completion of this atom
 * will not cause an event to be sent to userspace
 */
#define BASE_JD_REQ_EVENT_ONLY_ON_FAILURE   ((base_jd_core_req)1 << 12)

/* SW Flag: If this bit is set then completion of this atom will not cause an
 * event to be sent to userspace, whether successful or not.
 */
#define BASEP_JD_REQ_EVENT_NEVER ((base_jd_core_req)1 << 14)

/* SW Flag: Skip GPU cache clean and invalidation before starting a GPU job.
 *
 * If this bit is set then the GPU's cache will not be cleaned and invalidated
 * until a GPU job starts which does not have this bit set or a job completes
 * which does not have the BASE_JD_REQ_SKIP_CACHE_END bit set. Do not use
 * if the CPU may have written to memory addressed by the job since the last job
 * without this bit set was submitted.
 */
#define BASE_JD_REQ_SKIP_CACHE_START ((base_jd_core_req)1 << 15)

/* SW Flag: Skip GPU cache clean and invalidation after a GPU job completes.
 *
 * If this bit is set then the GPU's cache will not be cleaned and invalidated
 * until a GPU job completes which does not have this bit set or a job starts
 * which does not have the BASE_JD_REQ_SKIP_CACHE_START bit set. Do not use
 * if the CPU may read from or partially overwrite memory addressed by the job
 * before the next job without this bit set completes.
 */
#define BASE_JD_REQ_SKIP_CACHE_END ((base_jd_core_req)1 << 16)

/* Request the atom be executed on a specific job slot.
 *
 * When this flag is specified, it takes precedence over any existing job slot
 * selection logic.
 */
#define BASE_JD_REQ_JOB_SLOT ((base_jd_core_req)1 << 17)

/* SW-only requirement: The atom is the start of a renderpass.
 *
 * If this bit is set then the job chain will be soft-stopped if it causes the
 * GPU to write beyond the end of the physical pages backing the tiler heap, and
 * committing more memory to the heap would exceed an internal threshold. It may
 * be resumed after running one of the job chains attached to an atom with
 * BASE_JD_REQ_END_RENDERPASS set and the same renderpass ID. It may be
 * resumed multiple times until it completes without memory usage exceeding the
 * threshold.
 *
 * Usually used with BASE_JD_REQ_T.
 */
#define BASE_JD_REQ_START_RENDERPASS ((base_jd_core_req)1 << 18)

/* SW-only requirement: The atom is the end of a renderpass.
 *
 * If this bit is set then the atom incorporates the CPU address of a
 * base_jd_fragment object instead of the GPU address of a job chain.
 *
 * Which job chain is run depends upon whether the atom with the same renderpass
 * ID and the BASE_JD_REQ_START_RENDERPASS bit set completed normally or
 * was soft-stopped when it exceeded an upper threshold for tiler heap memory
 * usage.
 *
 * It also depends upon whether one of the job chains attached to the atom has
 * already been run as part of the same renderpass (in which case it would have
 * written unresolved multisampled and otherwise-discarded output to temporary
 * buffers that need to be read back). The job chain for doing a forced read and
 * forced write (from/to temporary buffers) is run as many times as necessary.
 *
 * Usually used with BASE_JD_REQ_FS.
 */
#define BASE_JD_REQ_END_RENDERPASS ((base_jd_core_req)1 << 19)

/* SW-only requirement: The atom needs to run on a limited core mask affinity.
 *
 * If this bit is set then the kbase_context.limited_core_mask will be applied
 * to the affinity.
 */
#define BASE_JD_REQ_LIMITED_CORE_MASK ((base_jd_core_req)1 << 20)

/* These requirement bits are currently unused in base_jd_core_req
 */
#define BASEP_JD_REQ_RESERVED \
	(~(BASE_JD_REQ_ATOM_TYPE | BASE_JD_REQ_EXTERNAL_RESOURCES | \
	BASE_JD_REQ_EVENT_ONLY_ON_FAILURE | BASEP_JD_REQ_EVENT_NEVER | \
	BASE_JD_REQ_EVENT_COALESCE | \
	BASE_JD_REQ_COHERENT_GROUP | BASE_JD_REQ_SPECIFIC_COHERENT_GROUP | \
	BASE_JD_REQ_FS_AFBC | BASE_JD_REQ_PERMON | \
	BASE_JD_REQ_SKIP_CACHE_START | BASE_JD_REQ_SKIP_CACHE_END | \
	BASE_JD_REQ_JOB_SLOT | BASE_JD_REQ_START_RENDERPASS | \
	BASE_JD_REQ_END_RENDERPASS | BASE_JD_REQ_LIMITED_CORE_MASK))

/* Mask of all bits in base_jd_core_req that control the type of the atom.
 *
 * This allows dependency only atoms to have flags set
 */
#define BASE_JD_REQ_ATOM_TYPE \
	(BASE_JD_REQ_FS | BASE_JD_REQ_CS | BASE_JD_REQ_T | BASE_JD_REQ_CF | \
	BASE_JD_REQ_V | BASE_JD_REQ_SOFT_JOB | BASE_JD_REQ_ONLY_COMPUTE)

/**
 * BASE_JD_REQ_SOFT_JOB_TYPE - Mask of all bits in base_jd_core_req that
 * controls the type of a soft job.
 */
#define BASE_JD_REQ_SOFT_JOB_TYPE (BASE_JD_REQ_SOFT_JOB | 0x1f)

/* Returns non-zero value if core requirements passed define a soft job or
 * a dependency only job.
 */
#define BASE_JD_REQ_SOFT_JOB_OR_DEP(core_req) \
	(((core_req) & BASE_JD_REQ_SOFT_JOB) || \
	((core_req) & BASE_JD_REQ_ATOM_TYPE) == BASE_JD_REQ_DEP)

/**
 * enum kbase_jd_atom_state - Atom states
 *
 * @KBASE_JD_ATOM_STATE_UNUSED: Atom is not used.
 * @KBASE_JD_ATOM_STATE_QUEUED: Atom is queued in JD.
 * @KBASE_JD_ATOM_STATE_IN_JS:  Atom has been given to JS (is runnable/running).
 * @KBASE_JD_ATOM_STATE_HW_COMPLETED: Atom has been completed, but not yet
 *                                    handed back to job dispatcher for
 *                                    dependency resolution.
 * @KBASE_JD_ATOM_STATE_COMPLETED: Atom has been completed, but not yet handed
 *                                 back to userspace.
 */
enum kbase_jd_atom_state {
	KBASE_JD_ATOM_STATE_UNUSED,
	KBASE_JD_ATOM_STATE_QUEUED,
	KBASE_JD_ATOM_STATE_IN_JS,
	KBASE_JD_ATOM_STATE_HW_COMPLETED,
	KBASE_JD_ATOM_STATE_COMPLETED
};

/**
 * typedef base_atom_id - Type big enough to store an atom number in.
 */
typedef __u8 base_atom_id;

/**
 * struct base_dependency - base dependency
 *
 * @atom_id:         An atom number
 * @dependency_type: Dependency type
 */
struct base_dependency {
	base_atom_id atom_id;
	base_jd_dep_type dependency_type;
};

/**
 * struct base_jd_fragment - Set of GPU fragment job chains used for rendering.
 *
 * @norm_read_norm_write: Job chain for full rendering.
 *                        GPU address of a fragment job chain to render in the
 *                        circumstance where the tiler job chain did not exceed
 *                        its memory usage threshold and no fragment job chain
 *                        was previously run for the same renderpass.
 *                        It is used no more than once per renderpass.
 * @norm_read_forced_write: Job chain for starting incremental
 *                          rendering.
 *                          GPU address of a fragment job chain to render in
 *                          the circumstance where the tiler job chain exceeded
 *                          its memory usage threshold for the first time and
 *                          no fragment job chain was previously run for the
 *                          same renderpass.
 *                          Writes unresolved multisampled and normally-
 *                          discarded output to temporary buffers that must be
 *                          read back by a subsequent forced_read job chain
 *                          before the renderpass is complete.
 *                          It is used no more than once per renderpass.
 * @forced_read_forced_write: Job chain for continuing incremental
 *                            rendering.
 *                            GPU address of a fragment job chain to render in
 *                            the circumstance where the tiler job chain
 *                            exceeded its memory usage threshold again
 *                            and a fragment job chain was previously run for
 *                            the same renderpass.
 *                            Reads unresolved multisampled and
 *                            normally-discarded output from temporary buffers
 *                            written by a previous forced_write job chain and
 *                            writes the same to temporary buffers again.
 *                            It is used as many times as required until
 *                            rendering completes.
 * @forced_read_norm_write: Job chain for ending incremental rendering.
 *                          GPU address of a fragment job chain to render in the
 *                          circumstance where the tiler job chain did not
 *                          exceed its memory usage threshold this time and a
 *                          fragment job chain was previously run for the same
 *                          renderpass.
 *                          Reads unresolved multisampled and normally-discarded
 *                          output from temporary buffers written by a previous
 *                          forced_write job chain in order to complete a
 *                          renderpass.
 *                          It is used no more than once per renderpass.
 *
 * This structure is referenced by the main atom structure if
 * BASE_JD_REQ_END_RENDERPASS is set in the base_jd_core_req.
 */
struct base_jd_fragment {
	__u64 norm_read_norm_write;
	__u64 norm_read_forced_write;
	__u64 forced_read_forced_write;
	__u64 forced_read_norm_write;
};

/**
 * typedef base_jd_prio - Base Atom priority.
 *
 * Only certain priority levels are actually implemented, as specified by the
 * BASE_JD_PRIO_<...> definitions below. It is undefined to use a priority
 * level that is not one of those defined below.
 *
 * Priority levels only affect scheduling after the atoms have had dependencies
 * resolved. For example, a low priority atom that has had its dependencies
 * resolved might run before a higher priority atom that has not had its
 * dependencies resolved.
 *
 * In general, fragment atoms do not affect non-fragment atoms with
 * lower priorities, and vice versa. One exception is that there is only one
 * priority value for each context. So a high-priority (e.g.) fragment atom
 * could increase its context priority, causing its non-fragment atoms to also
 * be scheduled sooner.
 *
 * The atoms are scheduled as follows with respect to their priorities:
 * * Let atoms 'X' and 'Y' be for the same job slot who have dependencies
 *   resolved, and atom 'X' has a higher priority than atom 'Y'
 * * If atom 'Y' is currently running on the HW, then it is interrupted to
 *   allow atom 'X' to run soon after
 * * If instead neither atom 'Y' nor atom 'X' are running, then when choosing
 *   the next atom to run, atom 'X' will always be chosen instead of atom 'Y'
 * * Any two atoms that have the same priority could run in any order with
 *   respect to each other. That is, there is no ordering constraint between
 *   atoms of the same priority.
 *
 * The sysfs file 'js_ctx_scheduling_mode' is used to control how atoms are
 * scheduled between contexts. The default value, 0, will cause higher-priority
 * atoms to be scheduled first, regardless of their context. The value 1 will
 * use a round-robin algorithm when deciding which context's atoms to schedule
 * next, so higher-priority atoms can only preempt lower priority atoms within
 * the same context. See KBASE_JS_SYSTEM_PRIORITY_MODE and
 * KBASE_JS_PROCESS_LOCAL_PRIORITY_MODE for more details.
 */
typedef __u8 base_jd_prio;

/* Medium atom priority. This is a priority higher than BASE_JD_PRIO_LOW */
#define BASE_JD_PRIO_MEDIUM  ((base_jd_prio)0)
/* High atom priority. This is a priority higher than BASE_JD_PRIO_MEDIUM and
 * BASE_JD_PRIO_LOW
 */
#define BASE_JD_PRIO_HIGH    ((base_jd_prio)1)
/* Low atom priority. */
#define BASE_JD_PRIO_LOW     ((base_jd_prio)2)
/* Real-Time atom priority. This is a priority higher than BASE_JD_PRIO_HIGH,
 * BASE_JD_PRIO_MEDIUM, and BASE_JD_PRIO_LOW
 */
#define BASE_JD_PRIO_REALTIME    ((base_jd_prio)3)

/* Invalid atom priority (max uint8_t value) */
#define BASE_JD_PRIO_INVALID ((base_jd_prio)255)

/* Count of the number of priority levels. This itself is not a valid
 * base_jd_prio setting
 */
#define BASE_JD_NR_PRIO_LEVELS 4

/**
 * struct base_jd_atom_v2 - Node of a dependency graph used to submit a
 *                          GPU job chain or soft-job to the kernel driver.
 *
 * @jc:            GPU address of a job chain or (if BASE_JD_REQ_END_RENDERPASS
 *                 is set in the base_jd_core_req) the CPU address of a
 *                 base_jd_fragment object.
 * @udata:         User data.
 * @extres_list:   List of external resources.
 * @nr_extres:     Number of external resources or JIT allocations.
 * @jit_id:        Zero-terminated array of IDs of just-in-time memory
 *                 allocations written to by the atom. When the atom
 *                 completes, the value stored at the
 *                 &struct_base_jit_alloc_info.heap_info_gpu_addr of
 *                 each allocation is read in order to enforce an
 *                 overall physical memory usage limit.
 * @pre_dep:       Pre-dependencies. One need to use SETTER function to assign
 *                 this field; this is done in order to reduce possibility of
 *                 improper assignment of a dependency field.
 * @atom_number:   Unique number to identify the atom.
 * @prio:          Atom priority. Refer to base_jd_prio for more details.
 * @device_nr:     Core group when BASE_JD_REQ_SPECIFIC_COHERENT_GROUP
 *                 specified.
 * @jobslot:       Job slot to use when BASE_JD_REQ_JOB_SLOT is specified.
 * @core_req:      Core requirements.
 * @renderpass_id: Renderpass identifier used to associate an atom that has
 *                 BASE_JD_REQ_START_RENDERPASS set in its core requirements
 *                 with an atom that has BASE_JD_REQ_END_RENDERPASS set.
 * @padding:       Unused. Must be zero.
 *
 * This structure has changed since UK 10.2 for which base_jd_core_req was a
 * __u16 value.
 *
 * In UK 10.3 a core_req field of a __u32 type was added to the end of the
 * structure, and the place in the structure previously occupied by __u16
 * core_req was kept but renamed to compat_core_req.
 *
 * From UK 11.20 - compat_core_req is now occupied by __u8 jit_id[2].
 * Compatibility with UK 10.x from UK 11.y is not handled because
 * the major version increase prevents this.
 *
 * For UK 11.20 jit_id[2] must be initialized to zero.
 */
struct base_jd_atom_v2 {
	__u64 jc;
	struct base_jd_udata udata;
	__u64 extres_list;
	__u16 nr_extres;
	__u8 jit_id[2];
	struct base_dependency pre_dep[2];
	base_atom_id atom_number;
	base_jd_prio prio;
	__u8 device_nr;
	__u8 jobslot;
	base_jd_core_req core_req;
	__u8 renderpass_id;
	__u8 padding[7];
};

/**
 * struct base_jd_atom - Same as base_jd_atom_v2, but has an extra seq_nr
 *                          at the beginning.
 *
 * @seq_nr:        Sequence number of logical grouping of atoms.
 * @jc:            GPU address of a job chain or (if BASE_JD_REQ_END_RENDERPASS
 *                 is set in the base_jd_core_req) the CPU address of a
 *                 base_jd_fragment object.
 * @udata:         User data.
 * @extres_list:   List of external resources.
 * @nr_extres:     Number of external resources or JIT allocations.
 * @jit_id:        Zero-terminated array of IDs of just-in-time memory
 *                 allocations written to by the atom. When the atom
 *                 completes, the value stored at the
 *                 &struct_base_jit_alloc_info.heap_info_gpu_addr of
 *                 each allocation is read in order to enforce an
 *                 overall physical memory usage limit.
 * @pre_dep:       Pre-dependencies. One need to use SETTER function to assign
 *                 this field; this is done in order to reduce possibility of
 *                 improper assignment of a dependency field.
 * @atom_number:   Unique number to identify the atom.
 * @prio:          Atom priority. Refer to base_jd_prio for more details.
 * @device_nr:     Core group when BASE_JD_REQ_SPECIFIC_COHERENT_GROUP
 *                 specified.
 * @jobslot:       Job slot to use when BASE_JD_REQ_JOB_SLOT is specified.
 * @core_req:      Core requirements.
 * @renderpass_id: Renderpass identifier used to associate an atom that has
 *                 BASE_JD_REQ_START_RENDERPASS set in its core requirements
 *                 with an atom that has BASE_JD_REQ_END_RENDERPASS set.
 * @padding:       Unused. Must be zero.
 */
typedef struct base_jd_atom {
	__u64 seq_nr;
	__u64 jc;
	struct base_jd_udata udata;
	__u64 extres_list;
	__u16 nr_extres;
	__u8 jit_id[2];
	struct base_dependency pre_dep[2];
	base_atom_id atom_number;
	base_jd_prio prio;
	__u8 device_nr;
	__u8 jobslot;
	base_jd_core_req core_req;
	__u8 renderpass_id;
	__u8 padding[7];
} base_jd_atom;

/* Job chain event code bits
 * Defines the bits used to create ::base_jd_event_code
 */
enum {
	BASE_JD_SW_EVENT_KERNEL = (1u << 15), /* Kernel side event */
	BASE_JD_SW_EVENT = (1u << 14), /* SW defined event */
	/* Event indicates success (SW events only) */
	BASE_JD_SW_EVENT_SUCCESS = (1u << 13),
	BASE_JD_SW_EVENT_JOB = (0u << 11), /* Job related event */
	BASE_JD_SW_EVENT_BAG = (1u << 11), /* Bag related event */
	BASE_JD_SW_EVENT_INFO = (2u << 11), /* Misc/info event */
	BASE_JD_SW_EVENT_RESERVED = (3u << 11),	/* Reserved event type */
	/* Mask to extract the type from an event code */
	BASE_JD_SW_EVENT_TYPE_MASK = (3u << 11)
};

/**
 * enum base_jd_event_code - Job chain event codes
 *
 * @BASE_JD_EVENT_RANGE_HW_NONFAULT_START: Start of hardware non-fault status
 *                                         codes.
 *                                         Obscurely, BASE_JD_EVENT_TERMINATED
 *                                         indicates a real fault, because the
 *                                         job was hard-stopped.
 * @BASE_JD_EVENT_NOT_STARTED: Can't be seen by userspace, treated as
 *                             'previous job done'.
 * @BASE_JD_EVENT_STOPPED:     Can't be seen by userspace, becomes
 *                             TERMINATED, DONE or JOB_CANCELLED.
 * @BASE_JD_EVENT_TERMINATED:  This is actually a fault status code - the job
 *                             was hard stopped.
 * @BASE_JD_EVENT_ACTIVE: Can't be seen by userspace, jobs only returned on
 *                        complete/fail/cancel.
 * @BASE_JD_EVENT_RANGE_HW_NONFAULT_END: End of hardware non-fault status codes.
 *                                       Obscurely, BASE_JD_EVENT_TERMINATED
 *                                       indicates a real fault,
 *                                       because the job was hard-stopped.
 * @BASE_JD_EVENT_RANGE_HW_FAULT_OR_SW_ERROR_START: Start of hardware fault and
 *                                                  software error status codes.
 * @BASE_JD_EVENT_RANGE_HW_FAULT_OR_SW_ERROR_END: End of hardware fault and
 *                                                software error status codes.
 * @BASE_JD_EVENT_RANGE_SW_SUCCESS_START: Start of software success status
 *                                        codes.
 * @BASE_JD_EVENT_RANGE_SW_SUCCESS_END: End of software success status codes.
 * @BASE_JD_EVENT_RANGE_KERNEL_ONLY_START: Start of kernel-only status codes.
 *                                         Such codes are never returned to
 *                                         user-space.
 * @BASE_JD_EVENT_RANGE_KERNEL_ONLY_END: End of kernel-only status codes.
 * @BASE_JD_EVENT_DONE: atom has completed successfull
 * @BASE_JD_EVENT_JOB_CONFIG_FAULT: Atom dependencies configuration error which
 *                                  shall result in a failed atom
 * @BASE_JD_EVENT_JOB_POWER_FAULT:  The job could not be executed because the
 *                                  part of the memory system required to access
 *                                  job descriptors was not powered on
 * @BASE_JD_EVENT_JOB_READ_FAULT:   Reading a job descriptor into the Job
 *                                  manager failed
 * @BASE_JD_EVENT_JOB_WRITE_FAULT:  Writing a job descriptor from the Job
 *                                  manager failed
 * @BASE_JD_EVENT_JOB_AFFINITY_FAULT: The job could not be executed because the
 *                                    specified affinity mask does not intersect
 *                                    any available cores
 * @BASE_JD_EVENT_JOB_BUS_FAULT:    A bus access failed while executing a job
 * @BASE_JD_EVENT_INSTR_INVALID_PC: A shader instruction with an illegal program
 *                                  counter was executed.
 * @BASE_JD_EVENT_INSTR_INVALID_ENC: A shader instruction with an illegal
 *                                  encoding was executed.
 * @BASE_JD_EVENT_INSTR_TYPE_MISMATCH: A shader instruction was executed where
 *                                  the instruction encoding did not match the
 *                                  instruction type encoded in the program
 *                                  counter.
 * @BASE_JD_EVENT_INSTR_OPERAND_FAULT: A shader instruction was executed that
 *                                  contained invalid combinations of operands.
 * @BASE_JD_EVENT_INSTR_TLS_FAULT:  A shader instruction was executed that tried
 *                                  to access the thread local storage section
 *                                  of another thread.
 * @BASE_JD_EVENT_INSTR_ALIGN_FAULT: A shader instruction was executed that
 *                                  tried to do an unsupported unaligned memory
 *                                  access.
 * @BASE_JD_EVENT_INSTR_BARRIER_FAULT: A shader instruction was executed that
 *                                  failed to complete an instruction barrier.
 * @BASE_JD_EVENT_DATA_INVALID_FAULT: Any data structure read as part of the job
 *                                  contains invalid combinations of data.
 * @BASE_JD_EVENT_TILE_RANGE_FAULT: Tile or fragment shading was asked to
 *                                  process a tile that is entirely outside the
 *                                  bounding box of the frame.
 * @BASE_JD_EVENT_STATE_FAULT:      Matches ADDR_RANGE_FAULT. A virtual address
 *                                  has been found that exceeds the virtual
 *                                  address range.
 * @BASE_JD_EVENT_OUT_OF_MEMORY:    The tiler ran out of memory when executing a job.
 * @BASE_JD_EVENT_UNKNOWN:          If multiple jobs in a job chain fail, only
 *                                  the first one the reports an error will set
 *                                  and return full error information.
 *                                  Subsequent failing jobs will not update the
 *                                  error status registers, and may write an
 *                                  error status of UNKNOWN.
 * @BASE_JD_EVENT_DELAYED_BUS_FAULT: The GPU received a bus fault for access to
 *                                  physical memory where the original virtual
 *                                  address is no longer available.
 * @BASE_JD_EVENT_SHAREABILITY_FAULT: Matches GPU_SHAREABILITY_FAULT. A cache
 *                                  has detected that the same line has been
 *                                  accessed as both shareable and non-shareable
 *                                  memory from inside the GPU.
 * @BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL1: A memory access hit an invalid table
 *                                  entry at level 1 of the translation table.
 * @BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL2: A memory access hit an invalid table
 *                                  entry at level 2 of the translation table.
 * @BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL3: A memory access hit an invalid table
 *                                  entry at level 3 of the translation table.
 * @BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL4: A memory access hit an invalid table
 *                                  entry at level 4 of the translation table.
 * @BASE_JD_EVENT_PERMISSION_FAULT: A memory access could not be allowed due to
 *                                  the permission flags set in translation
 *                                  table
 * @BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL1: A bus fault occurred while reading
 *                                  level 0 of the translation tables.
 * @BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL2: A bus fault occurred while reading
 *                                  level 1 of the translation tables.
 * @BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL3: A bus fault occurred while reading
 *                                  level 2 of the translation tables.
 * @BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL4: A bus fault occurred while reading
 *                                  level 3 of the translation tables.
 * @BASE_JD_EVENT_ACCESS_FLAG:      Matches ACCESS_FLAG_0. A memory access hit a
 *                                  translation table entry with the ACCESS_FLAG
 *                                  bit set to zero in level 0 of the
 *                                  page table, and the DISABLE_AF_FAULT flag
 *                                  was not set.
 * @BASE_JD_EVENT_MEM_GROWTH_FAILED: raised for JIT_ALLOC atoms that failed to
 *                                   grow memory on demand
 * @BASE_JD_EVENT_JOB_CANCELLED: raised when this atom was hard-stopped or its
 *                               dependencies failed
 * @BASE_JD_EVENT_JOB_INVALID: raised for many reasons, including invalid data
 *                             in the atom which overlaps with
 *                             BASE_JD_EVENT_JOB_CONFIG_FAULT, or if the
 *                             platform doesn't support the feature specified in
 *                             the atom.
 * @BASE_JD_EVENT_DRV_TERMINATED: this is a special event generated to indicate
 *                                to userspace that the KBase context has been
 *                                destroyed and Base should stop listening for
 *                                further events
 * @BASE_JD_EVENT_REMOVED_FROM_NEXT: raised when an atom that was configured in
 *                                   the GPU has to be retried (but it has not
 *                                   started) due to e.g., GPU reset
 * @BASE_JD_EVENT_END_RP_DONE: this is used for incremental rendering to signal
 *                             the completion of a renderpass. This value
 *                             shouldn't be returned to userspace but I haven't
 *                             seen where it is reset back to JD_EVENT_DONE.
 *
 * HW and low-level SW events are represented by event codes.
 * The status of jobs which succeeded are also represented by
 * an event code (see @BASE_JD_EVENT_DONE).
 * Events are usually reported as part of a &struct base_jd_event.
 *
 * The event codes are encoded in the following way:
 * * 10:0  - subtype
 * * 12:11 - type
 * * 13    - SW success (only valid if the SW bit is set)
 * * 14    - SW event (HW event if not set)
 * * 15    - Kernel event (should never be seen in userspace)
 *
 * Events are split up into ranges as follows:
 * * BASE_JD_EVENT_RANGE_<description>_START
 * * BASE_JD_EVENT_RANGE_<description>_END
 *
 * code is in <description>'s range when:
 * BASE_JD_EVENT_RANGE_<description>_START <= code <
 *   BASE_JD_EVENT_RANGE_<description>_END
 *
 * Ranges can be asserted for adjacency by testing that the END of the previous
 * is equal to the START of the next. This is useful for optimizing some tests
 * for range.
 *
 * A limitation is that the last member of this enum must explicitly be handled
 * (with an assert-unreachable statement) in switch statements that use
 * variables of this type. Otherwise, the compiler warns that we have not
 * handled that enum value.
 */
enum base_jd_event_code {
	/* HW defined exceptions */
	BASE_JD_EVENT_RANGE_HW_NONFAULT_START = 0,

	/* non-fatal exceptions */
	BASE_JD_EVENT_NOT_STARTED = 0x00,
	BASE_JD_EVENT_DONE = 0x01,
	BASE_JD_EVENT_STOPPED = 0x03,
	BASE_JD_EVENT_TERMINATED = 0x04,
	BASE_JD_EVENT_ACTIVE = 0x08,

	BASE_JD_EVENT_RANGE_HW_NONFAULT_END = 0x40,
	BASE_JD_EVENT_RANGE_HW_FAULT_OR_SW_ERROR_START = 0x40,

	/* job exceptions */
	BASE_JD_EVENT_JOB_CONFIG_FAULT = 0x40,
	BASE_JD_EVENT_JOB_POWER_FAULT = 0x41,
	BASE_JD_EVENT_JOB_READ_FAULT = 0x42,
	BASE_JD_EVENT_JOB_WRITE_FAULT = 0x43,
	BASE_JD_EVENT_JOB_AFFINITY_FAULT = 0x44,
	BASE_JD_EVENT_JOB_BUS_FAULT = 0x48,
	BASE_JD_EVENT_INSTR_INVALID_PC = 0x50,
	BASE_JD_EVENT_INSTR_INVALID_ENC = 0x51,
	BASE_JD_EVENT_INSTR_TYPE_MISMATCH = 0x52,
	BASE_JD_EVENT_INSTR_OPERAND_FAULT = 0x53,
	BASE_JD_EVENT_INSTR_TLS_FAULT = 0x54,
	BASE_JD_EVENT_INSTR_BARRIER_FAULT = 0x55,
	BASE_JD_EVENT_INSTR_ALIGN_FAULT = 0x56,
	BASE_JD_EVENT_DATA_INVALID_FAULT = 0x58,
	BASE_JD_EVENT_TILE_RANGE_FAULT = 0x59,
	BASE_JD_EVENT_STATE_FAULT = 0x5A,
	BASE_JD_EVENT_OUT_OF_MEMORY = 0x60,
	BASE_JD_EVENT_UNKNOWN = 0x7F,

	/* GPU exceptions */
	BASE_JD_EVENT_DELAYED_BUS_FAULT = 0x80,
	BASE_JD_EVENT_SHAREABILITY_FAULT = 0x88,

	/* MMU exceptions */
	BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL1 = 0xC1,
	BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL2 = 0xC2,
	BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL3 = 0xC3,
	BASE_JD_EVENT_TRANSLATION_FAULT_LEVEL4 = 0xC4,
	BASE_JD_EVENT_PERMISSION_FAULT = 0xC8,
	BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL1 = 0xD1,
	BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL2 = 0xD2,
	BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL3 = 0xD3,
	BASE_JD_EVENT_TRANSTAB_BUS_FAULT_LEVEL4 = 0xD4,
	BASE_JD_EVENT_ACCESS_FLAG = 0xD8,

	/* SW defined exceptions */
	BASE_JD_EVENT_MEM_GROWTH_FAILED =
		BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_JOB | 0x000,
	BASE_JD_EVENT_JOB_CANCELLED =
		BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_JOB | 0x002,
	BASE_JD_EVENT_JOB_INVALID =
		BASE_JD_SW_EVENT | BASE_JD_SW_EVENT_JOB | 0x003,

	BASE_JD_EVENT_RANGE_HW_FAULT_OR_SW_ERROR_END = BASE_JD_SW_EVENT |
		BASE_JD_SW_EVENT_RESERVED | 0x3FF,

	BASE_JD_EVENT_RANGE_SW_SUCCESS_START = BASE_JD_SW_EVENT |
		BASE_JD_SW_EVENT_SUCCESS | 0x000,

	BASE_JD_EVENT_DRV_TERMINATED = BASE_JD_SW_EVENT |
		BASE_JD_SW_EVENT_SUCCESS | BASE_JD_SW_EVENT_INFO | 0x000,

	BASE_JD_EVENT_RANGE_SW_SUCCESS_END = BASE_JD_SW_EVENT |
		BASE_JD_SW_EVENT_SUCCESS | BASE_JD_SW_EVENT_RESERVED | 0x3FF,

	BASE_JD_EVENT_RANGE_KERNEL_ONLY_START = BASE_JD_SW_EVENT |
		BASE_JD_SW_EVENT_KERNEL | 0x000,
	BASE_JD_EVENT_REMOVED_FROM_NEXT = BASE_JD_SW_EVENT |
		BASE_JD_SW_EVENT_KERNEL | BASE_JD_SW_EVENT_JOB | 0x000,
	BASE_JD_EVENT_END_RP_DONE = BASE_JD_SW_EVENT |
		BASE_JD_SW_EVENT_KERNEL | BASE_JD_SW_EVENT_JOB | 0x001,

	BASE_JD_EVENT_RANGE_KERNEL_ONLY_END = BASE_JD_SW_EVENT |
		BASE_JD_SW_EVENT_KERNEL | BASE_JD_SW_EVENT_RESERVED | 0x3FF
};

/**
 * struct base_jd_event_v2 - Event reporting structure
 *
 * @event_code:  event code of type @ref base_jd_event_code.
 * @atom_number: the atom number that has completed.
 * @padding:     padding.
 * @udata:       user data.
 *
 * This structure is used by the kernel driver to report information
 * about GPU events. They can either be HW-specific events or low-level
 * SW events, such as job-chain completion.
 *
 * The event code contains an event type field which can be extracted
 * by ANDing with BASE_JD_SW_EVENT_TYPE_MASK.
 */
struct base_jd_event_v2 {
	__u32 event_code;
	base_atom_id atom_number;
	__u8 padding[3];
	struct base_jd_udata udata;
};

/**
 * struct base_dump_cpu_gpu_counters - Structure for
 *                                     BASE_JD_REQ_SOFT_DUMP_CPU_GPU_COUNTERS
 *                                     jobs.
 * @system_time:   gpu timestamp
 * @cycle_counter: gpu cycle count
 * @sec:           cpu time(sec)
 * @usec:          cpu time(usec)
 * @padding:       padding
 *
 * This structure is stored into the memory pointed to by the @jc field
 * of &struct base_jd_atom.
 *
 * It must not occupy the same CPU cache line(s) as any neighboring data.
 * This is to avoid cases where access to pages containing the structure
 * is shared between cached and un-cached memory regions, which would
 * cause memory corruption.
 */

struct base_dump_cpu_gpu_counters {
	__u64 system_time;
	__u64 cycle_counter;
	__u64 sec;
	__u32 usec;
	__u8 padding[36];
};

/**
 * struct mali_base_gpu_core_props - GPU core props info
 *
 * @product_id: Pro specific value.
 * @version_status: Status of the GPU release. No defined values, but starts at
 *   0 and increases by one for each release status (alpha, beta, EAC, etc.).
 *   4 bit values (0-15).
 * @minor_revision: Minor release number of the GPU. "P" part of an "RnPn"
 *   release number.
 *   8 bit values (0-255).
 * @major_revision: Major release number of the GPU. "R" part of an "RnPn"
 *   release number.
 *   4 bit values (0-15).
 * @padding: padding to align to 8-byte
 * @gpu_freq_khz_max: The maximum GPU frequency. Reported to applications by
 *   clGetDeviceInfo()
 * @log2_program_counter_size: Size of the shader program counter, in bits.
 * @texture_features: TEXTURE_FEATURES_x registers, as exposed by the GPU. This
 *   is a bitpattern where a set bit indicates that the format is supported.
 *   Before using a texture format, it is recommended that the corresponding
 *   bit be checked.
 * @gpu_available_memory_size: Theoretical maximum memory available to the GPU.
 *   It is unlikely that a client will be able to allocate all of this memory
 *   for their own purposes, but this at least provides an upper bound on the
 *   memory available to the GPU.
 *   This is required for OpenCL's clGetDeviceInfo() call when
 *   CL_DEVICE_GLOBAL_MEM_SIZE is requested, for OpenCL GPU devices. The
 *   client will not be expecting to allocate anywhere near this value.
 * @num_exec_engines: The number of execution engines. Only valid for tGOX
 *   (Bifrost) GPUs, where GPU_HAS_REG_CORE_FEATURES is defined. Otherwise,
 *   this is always 0.
 */
struct mali_base_gpu_core_props {
	__u32 product_id;
	__u16 version_status;
	__u16 minor_revision;
	__u16 major_revision;
	__u16 padding;
	__u32 gpu_freq_khz_max;
	__u32 log2_program_counter_size;
	__u32 texture_features[BASE_GPU_NUM_TEXTURE_FEATURES_REGISTERS];
	__u64 gpu_available_memory_size;
	__u8 num_exec_engines;
};

#endif /* _UAPI_BASE_JM_KERNEL_H_ */
