/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2020-2022 ARM Limited. All rights reserved.
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

#ifndef _UAPI_BASE_CSF_KERNEL_H_
#define _UAPI_BASE_CSF_KERNEL_H_

#include <linux/types.h>

/* Memory allocation, access/hint flags.
 *
 * See base_mem_alloc_flags.
 */

/* IN */
/* Read access CPU side
 */
#define BASE_MEM_PROT_CPU_RD ((base_mem_alloc_flags)1 << 0)

/* Write access CPU side
 */
#define BASE_MEM_PROT_CPU_WR ((base_mem_alloc_flags)1 << 1)

/* Read access GPU side
 */
#define BASE_MEM_PROT_GPU_RD ((base_mem_alloc_flags)1 << 2)

/* Write access GPU side
 */
#define BASE_MEM_PROT_GPU_WR ((base_mem_alloc_flags)1 << 3)

/* Execute allowed on the GPU side
 */
#define BASE_MEM_PROT_GPU_EX ((base_mem_alloc_flags)1 << 4)

/* Will be permanently mapped in kernel space.
 * Flag is only allowed on allocations originating from kbase.
 */
#define BASEP_MEM_PERMANENT_KERNEL_MAPPING ((base_mem_alloc_flags)1 << 5)

/* The allocation will completely reside within the same 4GB chunk in the GPU
 * virtual space.
 * Since this flag is primarily required only for the TLS memory which will
 * not be used to contain executable code and also not used for Tiler heap,
 * it can't be used along with BASE_MEM_PROT_GPU_EX and TILER_ALIGN_TOP flags.
 */
#define BASE_MEM_GPU_VA_SAME_4GB_PAGE ((base_mem_alloc_flags)1 << 6)

/* Userspace is not allowed to free this memory.
 * Flag is only allowed on allocations originating from kbase.
 */
#define BASEP_MEM_NO_USER_FREE ((base_mem_alloc_flags)1 << 7)

/* Must be FIXED memory. */
#define BASE_MEM_FIXED ((base_mem_alloc_flags)1 << 8)

/* Grow backing store on GPU Page Fault
 */
#define BASE_MEM_GROW_ON_GPF ((base_mem_alloc_flags)1 << 9)

/* Page coherence Outer shareable, if available
 */
#define BASE_MEM_COHERENT_SYSTEM ((base_mem_alloc_flags)1 << 10)

/* Page coherence Inner shareable
 */
#define BASE_MEM_COHERENT_LOCAL ((base_mem_alloc_flags)1 << 11)

/* IN/OUT */
/* Should be cached on the CPU, returned if actually cached
 */
#define BASE_MEM_CACHED_CPU ((base_mem_alloc_flags)1 << 12)

/* IN/OUT */
/* Must have same VA on both the GPU and the CPU
 */
#define BASE_MEM_SAME_VA ((base_mem_alloc_flags)1 << 13)

/* OUT */
/* Must call mmap to acquire a GPU address for the alloc
 */
#define BASE_MEM_NEED_MMAP ((base_mem_alloc_flags)1 << 14)

/* IN */
/* Page coherence Outer shareable, required.
 */
#define BASE_MEM_COHERENT_SYSTEM_REQUIRED ((base_mem_alloc_flags)1 << 15)

/* Protected memory
 */
#define BASE_MEM_PROTECTED ((base_mem_alloc_flags)1 << 16)

/* Not needed physical memory
 */
#define BASE_MEM_DONT_NEED ((base_mem_alloc_flags)1 << 17)

/* Must use shared CPU/GPU zone (SAME_VA zone) but doesn't require the
 * addresses to be the same
 */
#define BASE_MEM_IMPORT_SHARED ((base_mem_alloc_flags)1 << 18)

/* CSF event memory
 *
 * If Outer shareable coherence is not specified or not available, then on
 * allocation kbase will automatically use the uncached GPU mapping.
 * There is no need for the client to specify BASE_MEM_UNCACHED_GPU
 * themselves when allocating memory with the BASE_MEM_CSF_EVENT flag.
 *
 * This memory requires a permanent mapping
 *
 * See also kbase_reg_needs_kernel_mapping()
 */
#define BASE_MEM_CSF_EVENT ((base_mem_alloc_flags)1 << 19)

#define BASE_MEM_RESERVED_BIT_20 ((base_mem_alloc_flags)1 << 20)

/* Should be uncached on the GPU, will work only for GPUs using AARCH64 mmu
 * mode. Some components within the GPU might only be able to access memory
 * that is GPU cacheable. Refer to the specific GPU implementation for more
 * details. The 3 shareability flags will be ignored for GPU uncached memory.
 * If used while importing USER_BUFFER type memory, then the import will fail
 * if the memory is not aligned to GPU and CPU cache line width.
 */
#define BASE_MEM_UNCACHED_GPU ((base_mem_alloc_flags)1 << 21)

/*
 * Bits [22:25] for group_id (0~15).
 *
 * base_mem_group_id_set() should be used to pack a memory group ID into a
 * base_mem_alloc_flags value instead of accessing the bits directly.
 * base_mem_group_id_get() should be used to extract the memory group ID from
 * a base_mem_alloc_flags value.
 */
#define BASEP_MEM_GROUP_ID_SHIFT 22
#define BASE_MEM_GROUP_ID_MASK \
	((base_mem_alloc_flags)0xF << BASEP_MEM_GROUP_ID_SHIFT)

/* Must do CPU cache maintenance when imported memory is mapped/unmapped
 * on GPU. Currently applicable to dma-buf type only.
 */
#define BASE_MEM_IMPORT_SYNC_ON_MAP_UNMAP ((base_mem_alloc_flags)1 << 26)

/* OUT */
/* Kernel side cache sync ops required */
#define BASE_MEM_KERNEL_SYNC ((base_mem_alloc_flags)1 << 28)

/* Must be FIXABLE memory: its GPU VA will be determined at a later point,
 * at which time it will be at a fixed GPU VA.
 */
#define BASE_MEM_FIXABLE ((base_mem_alloc_flags)1 << 29)

/* Number of bits used as flags for base memory management
 *
 * Must be kept in sync with the base_mem_alloc_flags flags
 */
#define BASE_MEM_FLAGS_NR_BITS 30

/* A mask of all the flags which are only valid for allocations within kbase,
 * and may not be passed from user space.
 */
#define BASEP_MEM_FLAGS_KERNEL_ONLY \
	(BASEP_MEM_PERMANENT_KERNEL_MAPPING | BASEP_MEM_NO_USER_FREE)

/* A mask for all output bits, excluding IN/OUT bits.
 */
#define BASE_MEM_FLAGS_OUTPUT_MASK BASE_MEM_NEED_MMAP

/* A mask for all input bits, including IN/OUT bits.
 */
#define BASE_MEM_FLAGS_INPUT_MASK \
	(((1 << BASE_MEM_FLAGS_NR_BITS) - 1) & ~BASE_MEM_FLAGS_OUTPUT_MASK)

/* A mask of all currently reserved flags
 */
#define BASE_MEM_FLAGS_RESERVED BASE_MEM_RESERVED_BIT_20

#define BASEP_MEM_INVALID_HANDLE (0ul)
#define BASE_MEM_MMU_DUMP_HANDLE (1ul << LOCAL_PAGE_SHIFT)
#define BASE_MEM_TRACE_BUFFER_HANDLE (2ul << LOCAL_PAGE_SHIFT)
#define BASE_MEM_MAP_TRACKING_HANDLE (3ul << LOCAL_PAGE_SHIFT)
#define BASEP_MEM_WRITE_ALLOC_PAGES_HANDLE (4ul << LOCAL_PAGE_SHIFT)
/* reserved handles ..-47<<PAGE_SHIFT> for future special handles */
#define BASEP_MEM_CSF_USER_REG_PAGE_HANDLE (47ul << LOCAL_PAGE_SHIFT)
#define BASEP_MEM_CSF_USER_IO_PAGES_HANDLE (48ul << LOCAL_PAGE_SHIFT)
#define BASE_MEM_COOKIE_BASE (64ul << LOCAL_PAGE_SHIFT)
#define BASE_MEM_FIRST_FREE_ADDRESS                                            \
	((BITS_PER_LONG << LOCAL_PAGE_SHIFT) + BASE_MEM_COOKIE_BASE)

#define KBASE_CSF_NUM_USER_IO_PAGES_HANDLE \
	((BASE_MEM_COOKIE_BASE - BASEP_MEM_CSF_USER_IO_PAGES_HANDLE) >> \
	 LOCAL_PAGE_SHIFT)

/* Valid set of just-in-time memory allocation flags */
#define BASE_JIT_ALLOC_VALID_FLAGS ((__u8)0)

/* Flags to pass to ::base_context_init.
 * Flags can be ORed together to enable multiple things.
 *
 * These share the same space as BASEP_CONTEXT_FLAG_*, and so must
 * not collide with them.
 */
typedef __u32 base_context_create_flags;

/* No flags set */
#define BASE_CONTEXT_CREATE_FLAG_NONE ((base_context_create_flags)0)

/* Base context is embedded in a cctx object (flag used for CINSTR
 * software counter macros)
 */
#define BASE_CONTEXT_CCTX_EMBEDDED ((base_context_create_flags)1 << 0)

/* Base context is a 'System Monitor' context for Hardware counters.
 *
 * One important side effect of this is that job submission is disabled.
 */
#define BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED \
	((base_context_create_flags)1 << 1)

/* Base context creates a CSF event notification thread.
 *
 * The creation of a CSF event notification thread is conditional but
 * mandatory for the handling of CSF events.
 */
#define BASE_CONTEXT_CSF_EVENT_THREAD ((base_context_create_flags)1 << 2)

/* Bit-shift used to encode a memory group ID in base_context_create_flags
 */
#define BASEP_CONTEXT_MMU_GROUP_ID_SHIFT (3)

/* Bitmask used to encode a memory group ID in base_context_create_flags
 */
#define BASEP_CONTEXT_MMU_GROUP_ID_MASK \
	((base_context_create_flags)0xF << BASEP_CONTEXT_MMU_GROUP_ID_SHIFT)

/* Bitpattern describing the base_context_create_flags that can be
 * passed to the kernel
 */
#define BASEP_CONTEXT_CREATE_KERNEL_FLAGS \
	(BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED | \
	 BASEP_CONTEXT_MMU_GROUP_ID_MASK)

/* Bitpattern describing the ::base_context_create_flags that can be
 * passed to base_context_init()
 */
#define BASEP_CONTEXT_CREATE_ALLOWED_FLAGS \
	(BASE_CONTEXT_CCTX_EMBEDDED | \
	 BASE_CONTEXT_CSF_EVENT_THREAD | \
	 BASEP_CONTEXT_CREATE_KERNEL_FLAGS)

/* Enable additional tracepoints for latency measurements (TL_ATOM_READY,
 * TL_ATOM_DONE, TL_ATOM_PRIO_CHANGE, TL_ATOM_EVENT_POST)
 */
#define BASE_TLSTREAM_ENABLE_LATENCY_TRACEPOINTS (1 << 0)

/* Indicate that job dumping is enabled. This could affect certain timers
 * to account for the performance impact.
 */
#define BASE_TLSTREAM_JOB_DUMPING_ENABLED (1 << 1)

/* Enable KBase tracepoints for CSF builds */
#define BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS (1 << 2)

/* Enable additional CSF Firmware side tracepoints */
#define BASE_TLSTREAM_ENABLE_CSFFW_TRACEPOINTS (1 << 3)

#define BASE_TLSTREAM_FLAGS_MASK (BASE_TLSTREAM_ENABLE_LATENCY_TRACEPOINTS | \
		BASE_TLSTREAM_JOB_DUMPING_ENABLED | \
		BASE_TLSTREAM_ENABLE_CSF_TRACEPOINTS | \
		BASE_TLSTREAM_ENABLE_CSFFW_TRACEPOINTS)

/* Number of pages mapped into the process address space for a bound GPU
 * command queue. A pair of input/output pages and a Hw doorbell page
 * are mapped to enable direct submission of commands to Hw.
 */
#define BASEP_QUEUE_NR_MMAP_USER_PAGES ((size_t)3)

#define BASE_QUEUE_MAX_PRIORITY (15U)

/* CQS Sync object is an array of __u32 event_mem[2], error field index is 1 */
#define BASEP_EVENT_VAL_INDEX (0U)
#define BASEP_EVENT_ERR_INDEX (1U)

/* The upper limit for number of objects that could be waited/set per command.
 * This limit is now enforced as internally the error inherit inputs are
 * converted to 32-bit flags in a __u32 variable occupying a previously padding
 * field.
 */
#define BASEP_KCPU_CQS_MAX_NUM_OBJS ((size_t)32)

/**
 * enum base_kcpu_command_type - Kernel CPU queue command type.
 * @BASE_KCPU_COMMAND_TYPE_FENCE_SIGNAL:       fence_signal,
 * @BASE_KCPU_COMMAND_TYPE_FENCE_WAIT:         fence_wait,
 * @BASE_KCPU_COMMAND_TYPE_CQS_WAIT:           cqs_wait,
 * @BASE_KCPU_COMMAND_TYPE_CQS_SET:            cqs_set,
 * @BASE_KCPU_COMMAND_TYPE_CQS_WAIT_OPERATION: cqs_wait_operation,
 * @BASE_KCPU_COMMAND_TYPE_CQS_SET_OPERATION:  cqs_set_operation,
 * @BASE_KCPU_COMMAND_TYPE_MAP_IMPORT:         map_import,
 * @BASE_KCPU_COMMAND_TYPE_UNMAP_IMPORT:       unmap_import,
 * @BASE_KCPU_COMMAND_TYPE_UNMAP_IMPORT_FORCE: unmap_import_force,
 * @BASE_KCPU_COMMAND_TYPE_JIT_ALLOC:          jit_alloc,
 * @BASE_KCPU_COMMAND_TYPE_JIT_FREE:           jit_free,
 * @BASE_KCPU_COMMAND_TYPE_GROUP_SUSPEND:      group_suspend,
 * @BASE_KCPU_COMMAND_TYPE_ERROR_BARRIER:      error_barrier,
 */
enum base_kcpu_command_type {
	BASE_KCPU_COMMAND_TYPE_FENCE_SIGNAL,
	BASE_KCPU_COMMAND_TYPE_FENCE_WAIT,
	BASE_KCPU_COMMAND_TYPE_CQS_WAIT,
	BASE_KCPU_COMMAND_TYPE_CQS_SET,
	BASE_KCPU_COMMAND_TYPE_CQS_WAIT_OPERATION,
	BASE_KCPU_COMMAND_TYPE_CQS_SET_OPERATION,
	BASE_KCPU_COMMAND_TYPE_MAP_IMPORT,
	BASE_KCPU_COMMAND_TYPE_UNMAP_IMPORT,
	BASE_KCPU_COMMAND_TYPE_UNMAP_IMPORT_FORCE,
	BASE_KCPU_COMMAND_TYPE_JIT_ALLOC,
	BASE_KCPU_COMMAND_TYPE_JIT_FREE,
	BASE_KCPU_COMMAND_TYPE_GROUP_SUSPEND,
	BASE_KCPU_COMMAND_TYPE_ERROR_BARRIER
};

/**
 * enum base_queue_group_priority - Priority of a GPU Command Queue Group.
 * @BASE_QUEUE_GROUP_PRIORITY_HIGH:     GPU Command Queue Group is of high
 *                                      priority.
 * @BASE_QUEUE_GROUP_PRIORITY_MEDIUM:   GPU Command Queue Group is of medium
 *                                      priority.
 * @BASE_QUEUE_GROUP_PRIORITY_LOW:      GPU Command Queue Group is of low
 *                                      priority.
 * @BASE_QUEUE_GROUP_PRIORITY_REALTIME: GPU Command Queue Group is of real-time
 *                                      priority.
 * @BASE_QUEUE_GROUP_PRIORITY_COUNT:    Number of GPU Command Queue Group
 *                                      priority levels.
 *
 * Currently this is in order of highest to lowest, but if new levels are added
 * then those new levels may be out of order to preserve the ABI compatibility
 * with previous releases. At that point, ensure assignment to
 * the 'priority' member in &kbase_queue_group is updated to ensure it remains
 * a linear ordering.
 *
 * There should be no gaps in the enum, otherwise use of
 * BASE_QUEUE_GROUP_PRIORITY_COUNT in kbase must be updated.
 */
enum base_queue_group_priority {
	BASE_QUEUE_GROUP_PRIORITY_HIGH = 0,
	BASE_QUEUE_GROUP_PRIORITY_MEDIUM,
	BASE_QUEUE_GROUP_PRIORITY_LOW,
	BASE_QUEUE_GROUP_PRIORITY_REALTIME,
	BASE_QUEUE_GROUP_PRIORITY_COUNT
};

struct base_kcpu_command_fence_info {
	__u64 fence;
};

struct base_cqs_wait_info {
	__u64 addr;
	__u32 val;
	__u32 padding;
};

struct base_kcpu_command_cqs_wait_info {
	__u64 objs;
	__u32 nr_objs;
	__u32 inherit_err_flags;
};

struct base_cqs_set {
	__u64 addr;
};

struct base_kcpu_command_cqs_set_info {
	__u64 objs;
	__u32 nr_objs;
	__u32 padding;
};

/**
 * typedef basep_cqs_data_type - Enumeration of CQS Data Types
 *
 * @BASEP_CQS_DATA_TYPE_U32: The Data Type of a CQS Object's value
 *                           is an unsigned 32-bit integer
 * @BASEP_CQS_DATA_TYPE_U64: The Data Type of a CQS Object's value
 *                           is an unsigned 64-bit integer
 */
typedef enum PACKED {
	BASEP_CQS_DATA_TYPE_U32 = 0,
	BASEP_CQS_DATA_TYPE_U64 = 1,
} basep_cqs_data_type;

/**
 * typedef basep_cqs_wait_operation_op - Enumeration of CQS Object Wait
 *                                Operation conditions
 *
 * @BASEP_CQS_WAIT_OPERATION_LE: CQS Wait Operation indicating that a
 *                                wait will be satisfied when a CQS Object's
 *                                value is Less than or Equal to
 *                                the Wait Operation value
 * @BASEP_CQS_WAIT_OPERATION_GT: CQS Wait Operation indicating that a
 *                                wait will be satisfied when a CQS Object's
 *                                value is Greater than the Wait Operation value
 */
typedef enum {
	BASEP_CQS_WAIT_OPERATION_LE = 0,
	BASEP_CQS_WAIT_OPERATION_GT = 1,
} basep_cqs_wait_operation_op;

struct base_cqs_wait_operation_info {
	__u64 addr;
	__u64 val;
	__u8 operation;
	__u8 data_type;
	__u8 padding[6];
};

/**
 * struct base_kcpu_command_cqs_wait_operation_info - structure which contains information
 *		about the Timeline CQS wait objects
 *
 * @objs:              An array of Timeline CQS waits.
 * @nr_objs:           Number of Timeline CQS waits in the array.
 * @inherit_err_flags: Bit-pattern for the CQSs in the array who's error field
 *                     to be served as the source for importing into the
 *                     queue's error-state.
 */
struct base_kcpu_command_cqs_wait_operation_info {
	__u64 objs;
	__u32 nr_objs;
	__u32 inherit_err_flags;
};

/**
 * typedef basep_cqs_set_operation_op - Enumeration of CQS Set Operations
 *
 * @BASEP_CQS_SET_OPERATION_ADD: CQS Set operation for adding a value
 *                                to a synchronization object
 * @BASEP_CQS_SET_OPERATION_SET: CQS Set operation for setting the value
 *                                of a synchronization object
 */
typedef enum {
	BASEP_CQS_SET_OPERATION_ADD = 0,
	BASEP_CQS_SET_OPERATION_SET = 1,
} basep_cqs_set_operation_op;

struct base_cqs_set_operation_info {
	__u64 addr;
	__u64 val;
	__u8 operation;
	__u8 data_type;
	__u8 padding[6];
};

/**
 * struct base_kcpu_command_cqs_set_operation_info - structure which contains information
 *		about the Timeline CQS set objects
 *
 * @objs:    An array of Timeline CQS sets.
 * @nr_objs: Number of Timeline CQS sets in the array.
 * @padding: Structure padding, unused bytes.
 */
struct base_kcpu_command_cqs_set_operation_info {
	__u64 objs;
	__u32 nr_objs;
	__u32 padding;
};

/**
 * struct base_kcpu_command_import_info - structure which contains information
 *		about the imported buffer.
 *
 * @handle:	Address of imported user buffer.
 */
struct base_kcpu_command_import_info {
	__u64 handle;
};

/**
 * struct base_kcpu_command_jit_alloc_info - structure which contains
 *		information about jit memory allocation.
 *
 * @info:	An array of elements of the
 *		struct base_jit_alloc_info type.
 * @count:	The number of elements in the info array.
 * @padding:	Padding to a multiple of 64 bits.
 */
struct base_kcpu_command_jit_alloc_info {
	__u64 info;
	__u8 count;
	__u8 padding[7];
};

/**
 * struct base_kcpu_command_jit_free_info - structure which contains
 *		information about jit memory which is to be freed.
 *
 * @ids:	An array containing the JIT IDs to free.
 * @count:	The number of elements in the ids array.
 * @padding:	Padding to a multiple of 64 bits.
 */
struct base_kcpu_command_jit_free_info {
	__u64 ids;
	__u8 count;
	__u8 padding[7];
};

/**
 * struct base_kcpu_command_group_suspend_info - structure which contains
 *		suspend buffer data captured for a suspended queue group.
 *
 * @buffer:		Pointer to an array of elements of the type char.
 * @size:		Number of elements in the @buffer array.
 * @group_handle:	Handle to the mapping of CSG.
 * @padding:		padding to a multiple of 64 bits.
 */
struct base_kcpu_command_group_suspend_info {
	__u64 buffer;
	__u32 size;
	__u8 group_handle;
	__u8 padding[3];
};


/**
 * struct base_kcpu_command - kcpu command.
 * @type:	type of the kcpu command, one enum base_kcpu_command_type
 * @padding:	padding to a multiple of 64 bits
 * @info:	structure which contains information about the kcpu command;
 *		actual type is determined by @p type
 * @info.fence:              Fence
 * @info.cqs_wait:           CQS wait
 * @info.cqs_set:            CQS set
 * @info.cqs_wait_operation: CQS wait operation
 * @info.cqs_set_operation:  CQS set operation
 * @info.import:             import
 * @info.jit_alloc:          JIT allocation
 * @info.jit_free:           JIT deallocation
 * @info.suspend_buf_copy:   suspend buffer copy
 * @info.sample_time:        sample time
 * @info.padding:            padding
 */
struct base_kcpu_command {
	__u8 type;
	__u8 padding[sizeof(__u64) - sizeof(__u8)];
	union {
		struct base_kcpu_command_fence_info fence;
		struct base_kcpu_command_cqs_wait_info cqs_wait;
		struct base_kcpu_command_cqs_set_info cqs_set;
		struct base_kcpu_command_cqs_wait_operation_info cqs_wait_operation;
		struct base_kcpu_command_cqs_set_operation_info cqs_set_operation;
		struct base_kcpu_command_import_info import;
		struct base_kcpu_command_jit_alloc_info jit_alloc;
		struct base_kcpu_command_jit_free_info jit_free;
		struct base_kcpu_command_group_suspend_info suspend_buf_copy;
		__u64 padding[2]; /* No sub-struct should be larger */
	} info;
};

/**
 * struct basep_cs_stream_control - CSI capabilities.
 *
 * @features: Features of this stream
 * @padding:  Padding to a multiple of 64 bits.
 */
struct basep_cs_stream_control {
	__u32 features;
	__u32 padding;
};

/**
 * struct basep_cs_group_control - CSG interface capabilities.
 *
 * @features:     Features of this group
 * @stream_num:   Number of streams in this group
 * @suspend_size: Size in bytes of the suspend buffer for this group
 * @padding:      Padding to a multiple of 64 bits.
 */
struct basep_cs_group_control {
	__u32 features;
	__u32 stream_num;
	__u32 suspend_size;
	__u32 padding;
};

/**
 * struct base_gpu_queue_group_error_fatal_payload - Unrecoverable fault
 *        error information associated with GPU command queue group.
 *
 * @sideband:     Additional information of the unrecoverable fault.
 * @status:       Unrecoverable fault information.
 *                This consists of exception type (least significant byte) and
 *                data (remaining bytes). One example of exception type is
 *                CS_INVALID_INSTRUCTION (0x49).
 * @padding:      Padding to make multiple of 64bits
 */
struct base_gpu_queue_group_error_fatal_payload {
	__u64 sideband;
	__u32 status;
	__u32 padding;
};

/**
 * struct base_gpu_queue_error_fatal_payload - Unrecoverable fault
 *        error information related to GPU command queue.
 *
 * @sideband:     Additional information about this unrecoverable fault.
 * @status:       Unrecoverable fault information.
 *                This consists of exception type (least significant byte) and
 *                data (remaining bytes). One example of exception type is
 *                CS_INVALID_INSTRUCTION (0x49).
 * @csi_index:    Index of the CSF interface the queue is bound to.
 * @padding:      Padding to make multiple of 64bits
 */
struct base_gpu_queue_error_fatal_payload {
	__u64 sideband;
	__u32 status;
	__u8 csi_index;
	__u8 padding[3];
};

/**
 * enum base_gpu_queue_group_error_type - GPU Fatal error type.
 *
 * @BASE_GPU_QUEUE_GROUP_ERROR_FATAL:       Fatal error associated with GPU
 *                                          command queue group.
 * @BASE_GPU_QUEUE_GROUP_QUEUE_ERROR_FATAL: Fatal error associated with GPU
 *                                          command queue.
 * @BASE_GPU_QUEUE_GROUP_ERROR_TIMEOUT:     Fatal error associated with
 *                                          progress timeout.
 * @BASE_GPU_QUEUE_GROUP_ERROR_TILER_HEAP_OOM: Fatal error due to running out
 *                                             of tiler heap memory.
 * @BASE_GPU_QUEUE_GROUP_ERROR_FATAL_COUNT: The number of fatal error types
 *
 * This type is used for &struct_base_gpu_queue_group_error.error_type.
 */
enum base_gpu_queue_group_error_type {
	BASE_GPU_QUEUE_GROUP_ERROR_FATAL = 0,
	BASE_GPU_QUEUE_GROUP_QUEUE_ERROR_FATAL,
	BASE_GPU_QUEUE_GROUP_ERROR_TIMEOUT,
	BASE_GPU_QUEUE_GROUP_ERROR_TILER_HEAP_OOM,
	BASE_GPU_QUEUE_GROUP_ERROR_FATAL_COUNT
};

/**
 * struct base_gpu_queue_group_error - Unrecoverable fault information
 * @error_type:          Error type of @base_gpu_queue_group_error_type
 *                       indicating which field in union payload is filled
 * @padding:             Unused bytes for 64bit boundary
 * @payload:             Input Payload
 * @payload.fatal_group: Unrecoverable fault error associated with
 *                       GPU command queue group
 * @payload.fatal_queue: Unrecoverable fault error associated with command queue
 */
struct base_gpu_queue_group_error {
	__u8 error_type;
	__u8 padding[7];
	union {
		struct base_gpu_queue_group_error_fatal_payload fatal_group;
		struct base_gpu_queue_error_fatal_payload fatal_queue;
	} payload;
};

/**
 * enum base_csf_notification_type - Notification type
 *
 * @BASE_CSF_NOTIFICATION_EVENT:                 Notification with kernel event
 * @BASE_CSF_NOTIFICATION_GPU_QUEUE_GROUP_ERROR: Notification with GPU fatal
 *                                               error
 * @BASE_CSF_NOTIFICATION_CPU_QUEUE_DUMP:        Notification with dumping cpu
 *                                               queue
 * @BASE_CSF_NOTIFICATION_COUNT:                 The number of notification type
 *
 * This type is used for &struct_base_csf_notification.type.
 */
enum base_csf_notification_type {
	BASE_CSF_NOTIFICATION_EVENT = 0,
	BASE_CSF_NOTIFICATION_GPU_QUEUE_GROUP_ERROR,
	BASE_CSF_NOTIFICATION_CPU_QUEUE_DUMP,
	BASE_CSF_NOTIFICATION_COUNT
};

/**
 * struct base_csf_notification - Event or error notification
 *
 * @type:                      Notification type of @base_csf_notification_type
 * @padding:                   Padding for 64bit boundary
 * @payload:                   Input Payload
 * @payload.align:             To fit the struct into a 64-byte cache line
 * @payload.csg_error:         CSG error
 * @payload.csg_error.handle:  Handle of GPU command queue group associated with
 *                             fatal error
 * @payload.csg_error.padding: Padding
 * @payload.csg_error.error:   Unrecoverable fault error
 *
 */
struct base_csf_notification {
	__u8 type;
	__u8 padding[7];
	union {
		struct {
			__u8 handle;
			__u8 padding[7];
			struct base_gpu_queue_group_error error;
		} csg_error;

		__u8 align[56];
	} payload;
};

#endif /* _UAPI_BASE_CSF_KERNEL_H_ */
