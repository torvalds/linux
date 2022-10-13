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

#ifndef _UAPI_KBASE_CSF_IOCTL_H_
#define _UAPI_KBASE_CSF_IOCTL_H_

#include <asm-generic/ioctl.h>
#include <linux/types.h>

/*
 * 1.0:
 * - CSF IOCTL header separated from JM
 * 1.1:
 * - Add a new priority level BASE_QUEUE_GROUP_PRIORITY_REALTIME
 * - Add ioctl 54: This controls the priority setting.
 * 1.2:
 * - Add new CSF GPU_FEATURES register into the property structure
 *   returned by KBASE_IOCTL_GET_GPUPROPS
 * 1.3:
 * - Add __u32 group_uid member to
 *   &struct_kbase_ioctl_cs_queue_group_create.out
 * 1.4:
 * - Replace padding in kbase_ioctl_cs_get_glb_iface with
 *   instr_features member of same size
 * 1.5:
 * - Add ioctl 40: kbase_ioctl_cs_queue_register_ex, this is a new
 *   queue registration call with extended format for supporting CS
 *   trace configurations with CSF trace_command.
 * 1.6:
 * - Added new HW performance counters interface to all GPUs.
 * 1.7:
 * - Added reserved field to QUEUE_GROUP_CREATE ioctl for future use
 * 1.8:
 * - Removed Kernel legacy HWC interface
 * 1.9:
 * - Reorganization of GPU-VA memory zones, including addition of
 *   FIXED_VA zone and auto-initialization of EXEC_VA zone.
 * - Added new Base memory allocation interface
 * 1.10:
 * - First release of new HW performance counters interface.
 * 1.11:
 * - Dummy model (no mali) backend will now clear HWC values after each sample
 * 1.12:
 * - Added support for incremental rendering flag in CSG create call
 * 1.13:
 * - Added ioctl to query a register of USER page.
 * 1.14:
 * - Added support for passing down the buffer descriptor VA in tiler heap init
 * 1.15:
 * - Enable new sync_wait GE condition
 * 1.16:
 * - Remove legacy definitions:
 *   - base_jit_alloc_info_10_2
 *   - base_jit_alloc_info_11_5
 *   - kbase_ioctl_mem_jit_init_10_2
 *   - kbase_ioctl_mem_jit_init_11_5
 */

#define BASE_UK_VERSION_MAJOR 1
#define BASE_UK_VERSION_MINOR 16

/**
 * struct kbase_ioctl_version_check - Check version compatibility between
 * kernel and userspace
 *
 * @major: Major version number
 * @minor: Minor version number
 */
struct kbase_ioctl_version_check {
	__u16 major;
	__u16 minor;
};

#define KBASE_IOCTL_VERSION_CHECK_RESERVED \
	_IOWR(KBASE_IOCTL_TYPE, 0, struct kbase_ioctl_version_check)

/**
 * struct kbase_ioctl_cs_queue_register - Register a GPU command queue with the
 *                                        base back-end
 *
 * @buffer_gpu_addr: GPU address of the buffer backing the queue
 * @buffer_size: Size of the buffer in bytes
 * @priority: Priority of the queue within a group when run within a process
 * @padding: Currently unused, must be zero
 *
 * Note: There is an identical sub-section in kbase_ioctl_cs_queue_register_ex.
 *        Any change of this struct should also be mirrored to the latter.
 */
struct kbase_ioctl_cs_queue_register {
	__u64 buffer_gpu_addr;
	__u32 buffer_size;
	__u8 priority;
	__u8 padding[3];
};

#define KBASE_IOCTL_CS_QUEUE_REGISTER \
	_IOW(KBASE_IOCTL_TYPE, 36, struct kbase_ioctl_cs_queue_register)

/**
 * struct kbase_ioctl_cs_queue_kick - Kick the GPU command queue group scheduler
 *                                    to notify that a queue has been updated
 *
 * @buffer_gpu_addr: GPU address of the buffer backing the queue
 */
struct kbase_ioctl_cs_queue_kick {
	__u64 buffer_gpu_addr;
};

#define KBASE_IOCTL_CS_QUEUE_KICK \
	_IOW(KBASE_IOCTL_TYPE, 37, struct kbase_ioctl_cs_queue_kick)

/**
 * union kbase_ioctl_cs_queue_bind - Bind a GPU command queue to a group
 *
 * @in:                 Input parameters
 * @in.buffer_gpu_addr: GPU address of the buffer backing the queue
 * @in.group_handle:    Handle of the group to which the queue should be bound
 * @in.csi_index:       Index of the CSF interface the queue should be bound to
 * @in.padding:         Currently unused, must be zero
 * @out:                Output parameters
 * @out.mmap_handle:    Handle to be used for creating the mapping of CS
 *                      input/output pages
 */
union kbase_ioctl_cs_queue_bind {
	struct {
		__u64 buffer_gpu_addr;
		__u8 group_handle;
		__u8 csi_index;
		__u8 padding[6];
	} in;
	struct {
		__u64 mmap_handle;
	} out;
};

#define KBASE_IOCTL_CS_QUEUE_BIND \
	_IOWR(KBASE_IOCTL_TYPE, 39, union kbase_ioctl_cs_queue_bind)

/**
 * struct kbase_ioctl_cs_queue_register_ex - Register a GPU command queue with the
 *                                           base back-end in extended format,
 *                                           involving trace buffer configuration
 *
 * @buffer_gpu_addr: GPU address of the buffer backing the queue
 * @buffer_size: Size of the buffer in bytes
 * @priority: Priority of the queue within a group when run within a process
 * @padding: Currently unused, must be zero
 * @ex_offset_var_addr: GPU address of the trace buffer write offset variable
 * @ex_buffer_base: Trace buffer GPU base address for the queue
 * @ex_buffer_size: Size of the trace buffer in bytes
 * @ex_event_size: Trace event write size, in log2 designation
 * @ex_event_state: Trace event states configuration
 * @ex_padding: Currently unused, must be zero
 *
 * Note: There is an identical sub-section at the start of this struct to that
 *        of @ref kbase_ioctl_cs_queue_register. Any change of this sub-section
 *        must also be mirrored to the latter. Following the said sub-section,
 *        the remaining fields forms the extension, marked with ex_*.
 */
struct kbase_ioctl_cs_queue_register_ex {
	__u64 buffer_gpu_addr;
	__u32 buffer_size;
	__u8 priority;
	__u8 padding[3];
	__u64 ex_offset_var_addr;
	__u64 ex_buffer_base;
	__u32 ex_buffer_size;
	__u8 ex_event_size;
	__u8 ex_event_state;
	__u8 ex_padding[2];
};

#define KBASE_IOCTL_CS_QUEUE_REGISTER_EX \
	_IOW(KBASE_IOCTL_TYPE, 40, struct kbase_ioctl_cs_queue_register_ex)

/**
 * struct kbase_ioctl_cs_queue_terminate - Terminate a GPU command queue
 *
 * @buffer_gpu_addr: GPU address of the buffer backing the queue
 */
struct kbase_ioctl_cs_queue_terminate {
	__u64 buffer_gpu_addr;
};

#define KBASE_IOCTL_CS_QUEUE_TERMINATE \
	_IOW(KBASE_IOCTL_TYPE, 41, struct kbase_ioctl_cs_queue_terminate)

/**
 * union kbase_ioctl_cs_queue_group_create_1_6 - Create a GPU command queue
 *                                               group
 * @in:               Input parameters
 * @in.tiler_mask:    Mask of tiler endpoints the group is allowed to use.
 * @in.fragment_mask: Mask of fragment endpoints the group is allowed to use.
 * @in.compute_mask:  Mask of compute endpoints the group is allowed to use.
 * @in.cs_min:        Minimum number of CSs required.
 * @in.priority:      Queue group's priority within a process.
 * @in.tiler_max:     Maximum number of tiler endpoints the group is allowed
 *                    to use.
 * @in.fragment_max:  Maximum number of fragment endpoints the group is
 *                    allowed to use.
 * @in.compute_max:   Maximum number of compute endpoints the group is allowed
 *                    to use.
 * @in.padding:       Currently unused, must be zero
 * @out:              Output parameters
 * @out.group_handle: Handle of a newly created queue group.
 * @out.padding:      Currently unused, must be zero
 * @out.group_uid:    UID of the queue group available to base.
 */
union kbase_ioctl_cs_queue_group_create_1_6 {
	struct {
		__u64 tiler_mask;
		__u64 fragment_mask;
		__u64 compute_mask;
		__u8 cs_min;
		__u8 priority;
		__u8 tiler_max;
		__u8 fragment_max;
		__u8 compute_max;
		__u8 padding[3];

	} in;
	struct {
		__u8 group_handle;
		__u8 padding[3];
		__u32 group_uid;
	} out;
};

#define KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_6                                  \
	_IOWR(KBASE_IOCTL_TYPE, 42, union kbase_ioctl_cs_queue_group_create_1_6)

/**
 * union kbase_ioctl_cs_queue_group_create - Create a GPU command queue group
 * @in:               Input parameters
 * @in.tiler_mask:    Mask of tiler endpoints the group is allowed to use.
 * @in.fragment_mask: Mask of fragment endpoints the group is allowed to use.
 * @in.compute_mask:  Mask of compute endpoints the group is allowed to use.
 * @in.cs_min:        Minimum number of CSs required.
 * @in.priority:      Queue group's priority within a process.
 * @in.tiler_max:     Maximum number of tiler endpoints the group is allowed
 *                    to use.
 * @in.fragment_max:  Maximum number of fragment endpoints the group is
 *                    allowed to use.
 * @in.compute_max:   Maximum number of compute endpoints the group is allowed
 *                    to use.
 * @in.csi_handlers:  Flags to signal that the application intends to use CSI
 *                    exception handlers in some linear buffers to deal with
 *                    the given exception types.
 * @in.padding:       Currently unused, must be zero
 * @out:              Output parameters
 * @out.group_handle: Handle of a newly created queue group.
 * @out.padding:      Currently unused, must be zero
 * @out.group_uid:    UID of the queue group available to base.
 */
union kbase_ioctl_cs_queue_group_create {
	struct {
		__u64 tiler_mask;
		__u64 fragment_mask;
		__u64 compute_mask;
		__u8 cs_min;
		__u8 priority;
		__u8 tiler_max;
		__u8 fragment_max;
		__u8 compute_max;
		__u8 csi_handlers;
		__u8 padding[2];
		/**
		 * @in.dvs_buf: buffer for deferred vertex shader
		 */
		__u64 dvs_buf;
	} in;
	struct {
		__u8 group_handle;
		__u8 padding[3];
		__u32 group_uid;
	} out;
};

#define KBASE_IOCTL_CS_QUEUE_GROUP_CREATE                                      \
	_IOWR(KBASE_IOCTL_TYPE, 58, union kbase_ioctl_cs_queue_group_create)

/**
 * struct kbase_ioctl_cs_queue_group_term - Terminate a GPU command queue group
 *
 * @group_handle: Handle of the queue group to be terminated
 * @padding: Padding to round up to a multiple of 8 bytes, must be zero
 */
struct kbase_ioctl_cs_queue_group_term {
	__u8 group_handle;
	__u8 padding[7];
};

#define KBASE_IOCTL_CS_QUEUE_GROUP_TERMINATE \
	_IOW(KBASE_IOCTL_TYPE, 43, struct kbase_ioctl_cs_queue_group_term)

#define KBASE_IOCTL_CS_EVENT_SIGNAL \
	_IO(KBASE_IOCTL_TYPE, 44)

typedef __u8 base_kcpu_queue_id; /* We support up to 256 active KCPU queues */

/**
 * struct kbase_ioctl_kcpu_queue_new - Create a KCPU command queue
 *
 * @id: ID of the new command queue returned by the kernel
 * @padding: Padding to round up to a multiple of 8 bytes, must be zero
 */
struct kbase_ioctl_kcpu_queue_new {
	base_kcpu_queue_id id;
	__u8 padding[7];
};

#define KBASE_IOCTL_KCPU_QUEUE_CREATE \
	_IOR(KBASE_IOCTL_TYPE, 45, struct kbase_ioctl_kcpu_queue_new)

/**
 * struct kbase_ioctl_kcpu_queue_delete - Destroy a KCPU command queue
 *
 * @id: ID of the command queue to be destroyed
 * @padding: Padding to round up to a multiple of 8 bytes, must be zero
 */
struct kbase_ioctl_kcpu_queue_delete {
	base_kcpu_queue_id id;
	__u8 padding[7];
};

#define KBASE_IOCTL_KCPU_QUEUE_DELETE \
	_IOW(KBASE_IOCTL_TYPE, 46, struct kbase_ioctl_kcpu_queue_delete)

/**
 * struct kbase_ioctl_kcpu_queue_enqueue - Enqueue commands into the KCPU queue
 *
 * @addr: Memory address of an array of struct base_kcpu_queue_command
 * @nr_commands: Number of commands in the array
 * @id: kcpu queue identifier, returned by KBASE_IOCTL_KCPU_QUEUE_CREATE ioctl
 * @padding: Padding to round up to a multiple of 8 bytes, must be zero
 */
struct kbase_ioctl_kcpu_queue_enqueue {
	__u64 addr;
	__u32 nr_commands;
	base_kcpu_queue_id id;
	__u8 padding[3];
};

#define KBASE_IOCTL_KCPU_QUEUE_ENQUEUE \
	_IOW(KBASE_IOCTL_TYPE, 47, struct kbase_ioctl_kcpu_queue_enqueue)

/**
 * union kbase_ioctl_cs_tiler_heap_init - Initialize chunked tiler memory heap
 * @in:                Input parameters
 * @in.chunk_size:     Size of each chunk.
 * @in.initial_chunks: Initial number of chunks that heap will be created with.
 * @in.max_chunks:     Maximum number of chunks that the heap is allowed to use.
 * @in.target_in_flight: Number of render-passes that the driver should attempt to
 *                     keep in flight for which allocation of new chunks is
 *                     allowed.
 * @in.group_id:       Group ID to be used for physical allocations.
 * @in.padding:        Padding
 * @in.buf_desc_va:    Buffer descriptor GPU VA for tiler heap reclaims.
 * @out:               Output parameters
 * @out.gpu_heap_va:   GPU VA (virtual address) of Heap context that was set up
 *                     for the heap.
 * @out.first_chunk_va: GPU VA of the first chunk allocated for the heap,
 *                     actually points to the header of heap chunk and not to
 *                     the low address of free memory in the chunk.
 */
union kbase_ioctl_cs_tiler_heap_init {
	struct {
		__u32 chunk_size;
		__u32 initial_chunks;
		__u32 max_chunks;
		__u16 target_in_flight;
		__u8 group_id;
		__u8 padding;
		__u64 buf_desc_va;
	} in;
	struct {
		__u64 gpu_heap_va;
		__u64 first_chunk_va;
	} out;
};

#define KBASE_IOCTL_CS_TILER_HEAP_INIT \
	_IOWR(KBASE_IOCTL_TYPE, 48, union kbase_ioctl_cs_tiler_heap_init)

/**
 * union kbase_ioctl_cs_tiler_heap_init_1_13 - Initialize chunked tiler memory heap,
 *                                             earlier version upto 1.13
 * @in:                Input parameters
 * @in.chunk_size:     Size of each chunk.
 * @in.initial_chunks: Initial number of chunks that heap will be created with.
 * @in.max_chunks:     Maximum number of chunks that the heap is allowed to use.
 * @in.target_in_flight: Number of render-passes that the driver should attempt to
 *                     keep in flight for which allocation of new chunks is
 *                     allowed.
 * @in.group_id:       Group ID to be used for physical allocations.
 * @in.padding:        Padding
 * @out:               Output parameters
 * @out.gpu_heap_va:   GPU VA (virtual address) of Heap context that was set up
 *                     for the heap.
 * @out.first_chunk_va: GPU VA of the first chunk allocated for the heap,
 *                     actually points to the header of heap chunk and not to
 *                     the low address of free memory in the chunk.
 */
union kbase_ioctl_cs_tiler_heap_init_1_13 {
	struct {
		__u32 chunk_size;
		__u32 initial_chunks;
		__u32 max_chunks;
		__u16 target_in_flight;
		__u8 group_id;
		__u8 padding;
	} in;
	struct {
		__u64 gpu_heap_va;
		__u64 first_chunk_va;
	} out;
};

#define KBASE_IOCTL_CS_TILER_HEAP_INIT_1_13                                                        \
	_IOWR(KBASE_IOCTL_TYPE, 48, union kbase_ioctl_cs_tiler_heap_init_1_13)

/**
 * struct kbase_ioctl_cs_tiler_heap_term - Terminate a chunked tiler heap
 *                                         instance
 *
 * @gpu_heap_va: GPU VA of Heap context that was set up for the heap.
 */
struct kbase_ioctl_cs_tiler_heap_term {
	__u64 gpu_heap_va;
};

#define KBASE_IOCTL_CS_TILER_HEAP_TERM \
	_IOW(KBASE_IOCTL_TYPE, 49, struct kbase_ioctl_cs_tiler_heap_term)

/**
 * union kbase_ioctl_cs_get_glb_iface - Request the global control block
 *                                        of CSF interface capabilities
 *
 * @in:                    Input parameters
 * @in.max_group_num:      The maximum number of groups to be read. Can be 0, in
 *                         which case groups_ptr is unused.
 * @in.max_total_stream_num: The maximum number of CSs to be read. Can be 0, in
 *                         which case streams_ptr is unused.
 * @in.groups_ptr:         Pointer where to store all the group data (sequentially).
 * @in.streams_ptr:        Pointer where to store all the CS data (sequentially).
 * @out:                   Output parameters
 * @out.glb_version:       Global interface version.
 * @out.features:          Bit mask of features (e.g. whether certain types of job
 *                         can be suspended).
 * @out.group_num:         Number of CSGs supported.
 * @out.prfcnt_size:       Size of CSF performance counters, in bytes. Bits 31:16
 *                         hold the size of firmware performance counter data
 *                         and 15:0 hold the size of hardware performance counter
 *                         data.
 * @out.total_stream_num:  Total number of CSs, summed across all groups.
 * @out.instr_features:    Instrumentation features. Bits 7:4 hold the maximum
 *                         size of events. Bits 3:0 hold the offset update rate.
 *                         (csf >= 1.1.0)
 *
 */
union kbase_ioctl_cs_get_glb_iface {
	struct {
		__u32 max_group_num;
		__u32 max_total_stream_num;
		__u64 groups_ptr;
		__u64 streams_ptr;
	} in;
	struct {
		__u32 glb_version;
		__u32 features;
		__u32 group_num;
		__u32 prfcnt_size;
		__u32 total_stream_num;
		__u32 instr_features;
	} out;
};

#define KBASE_IOCTL_CS_GET_GLB_IFACE \
	_IOWR(KBASE_IOCTL_TYPE, 51, union kbase_ioctl_cs_get_glb_iface)

struct kbase_ioctl_cs_cpu_queue_info {
	__u64 buffer;
	__u64 size;
};

#define KBASE_IOCTL_VERSION_CHECK \
	_IOWR(KBASE_IOCTL_TYPE, 52, struct kbase_ioctl_version_check)

#define KBASE_IOCTL_CS_CPU_QUEUE_DUMP \
	_IOW(KBASE_IOCTL_TYPE, 53, struct kbase_ioctl_cs_cpu_queue_info)

/**
 * union kbase_ioctl_mem_alloc_ex - Allocate memory on the GPU
 * @in: Input parameters
 * @in.va_pages: The number of pages of virtual address space to reserve
 * @in.commit_pages: The number of physical pages to allocate
 * @in.extension: The number of extra pages to allocate on each GPU fault which grows the region
 * @in.flags: Flags
 * @in.fixed_address: The GPU virtual address requested for the allocation,
 *                    if the allocation is using the BASE_MEM_FIXED flag.
 * @in.extra: Space for extra parameters that may be added in the future.
 * @out: Output parameters
 * @out.flags: Flags
 * @out.gpu_va: The GPU virtual address which is allocated
 */
union kbase_ioctl_mem_alloc_ex {
	struct {
		__u64 va_pages;
		__u64 commit_pages;
		__u64 extension;
		__u64 flags;
		__u64 fixed_address;
		__u64 extra[3];
	} in;
	struct {
		__u64 flags;
		__u64 gpu_va;
	} out;
};

#define KBASE_IOCTL_MEM_ALLOC_EX _IOWR(KBASE_IOCTL_TYPE, 59, union kbase_ioctl_mem_alloc_ex)

/**
 * union kbase_ioctl_read_user_page - Read a register of USER page
 *
 * @in:               Input parameters.
 * @in.offset:        Register offset in USER page.
 * @in.padding:       Padding to round up to a multiple of 8 bytes, must be zero.
 * @out:              Output parameters.
 * @out.val_lo:       Value of 32bit register or the 1st half of 64bit register to be read.
 * @out.val_hi:       Value of the 2nd half of 64bit register to be read.
 */
union kbase_ioctl_read_user_page {
	struct {
		__u32 offset;
		__u32 padding;
	} in;
	struct {
		__u32 val_lo;
		__u32 val_hi;
	} out;
};

#define KBASE_IOCTL_READ_USER_PAGE _IOWR(KBASE_IOCTL_TYPE, 60, union kbase_ioctl_read_user_page)

/***************
 * test ioctls *
 ***************/
#if MALI_UNIT_TEST
/* These ioctls are purely for test purposes and are not used in the production
 * driver, they therefore may change without notice
 */

/**
 * struct kbase_ioctl_cs_event_memory_write - Write an event memory address
 * @cpu_addr: Memory address to write
 * @value: Value to write
 * @padding: Currently unused, must be zero
 */
struct kbase_ioctl_cs_event_memory_write {
	__u64 cpu_addr;
	__u8 value;
	__u8 padding[7];
};

/**
 * union kbase_ioctl_cs_event_memory_read - Read an event memory address
 * @in: Input parameters
 * @in.cpu_addr: Memory address to read
 * @out: Output parameters
 * @out.value: Value read
 * @out.padding: Currently unused, must be zero
 */
union kbase_ioctl_cs_event_memory_read {
	struct {
		__u64 cpu_addr;
	} in;
	struct {
		__u8 value;
		__u8 padding[7];
	} out;
};

#endif /* MALI_UNIT_TEST */

#endif /* _UAPI_KBASE_CSF_IOCTL_H_ */
