/*
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
 */

#ifndef KFD_IOCTL_H_INCLUDED
#define KFD_IOCTL_H_INCLUDED

#include <drm/drm.h>
#include <linux/ioctl.h>

/*
 * - 1.1 - initial version
 * - 1.3 - Add SMI events support
 * - 1.4 - Indicate new SRAM EDC bit in device properties
 * - 1.5 - Add SVM API
 * - 1.6 - Query clear flags in SVM get_attr API
 * - 1.7 - Checkpoint Restore (CRIU) API
 * - 1.8 - CRIU - Support for SDMA transfers with GTT BOs
 * - 1.9 - Add available memory ioctl
 * - 1.10 - Add SMI profiler event log
 * - 1.11 - Add unified memory for ctx save/restore area
 * - 1.12 - Add DMA buf export ioctl
 * - 1.13 - Add debugger API
 * - 1.14 - Update kfd_event_data
 * - 1.15 - Enable managing mappings in compute VMs with GEM_VA ioctl
 */
#define KFD_IOCTL_MAJOR_VERSION 1
#define KFD_IOCTL_MINOR_VERSION 15

struct kfd_ioctl_get_version_args {
	__u32 major_version;	/* from KFD */
	__u32 minor_version;	/* from KFD */
};

/* For kfd_ioctl_create_queue_args.queue_type. */
#define KFD_IOC_QUEUE_TYPE_COMPUTE		0x0
#define KFD_IOC_QUEUE_TYPE_SDMA			0x1
#define KFD_IOC_QUEUE_TYPE_COMPUTE_AQL		0x2
#define KFD_IOC_QUEUE_TYPE_SDMA_XGMI		0x3

#define KFD_MAX_QUEUE_PERCENTAGE	100
#define KFD_MAX_QUEUE_PRIORITY		15

struct kfd_ioctl_create_queue_args {
	__u64 ring_base_address;	/* to KFD */
	__u64 write_pointer_address;	/* from KFD */
	__u64 read_pointer_address;	/* from KFD */
	__u64 doorbell_offset;	/* from KFD */

	__u32 ring_size;		/* to KFD */
	__u32 gpu_id;		/* to KFD */
	__u32 queue_type;		/* to KFD */
	__u32 queue_percentage;	/* to KFD */
	__u32 queue_priority;	/* to KFD */
	__u32 queue_id;		/* from KFD */

	__u64 eop_buffer_address;	/* to KFD */
	__u64 eop_buffer_size;	/* to KFD */
	__u64 ctx_save_restore_address; /* to KFD */
	__u32 ctx_save_restore_size;	/* to KFD */
	__u32 ctl_stack_size;		/* to KFD */
};

struct kfd_ioctl_destroy_queue_args {
	__u32 queue_id;		/* to KFD */
	__u32 pad;
};

struct kfd_ioctl_update_queue_args {
	__u64 ring_base_address;	/* to KFD */

	__u32 queue_id;		/* to KFD */
	__u32 ring_size;		/* to KFD */
	__u32 queue_percentage;	/* to KFD */
	__u32 queue_priority;	/* to KFD */
};

struct kfd_ioctl_set_cu_mask_args {
	__u32 queue_id;		/* to KFD */
	__u32 num_cu_mask;		/* to KFD */
	__u64 cu_mask_ptr;		/* to KFD */
};

struct kfd_ioctl_get_queue_wave_state_args {
	__u64 ctl_stack_address;	/* to KFD */
	__u32 ctl_stack_used_size;	/* from KFD */
	__u32 save_area_used_size;	/* from KFD */
	__u32 queue_id;			/* to KFD */
	__u32 pad;
};

struct kfd_ioctl_get_available_memory_args {
	__u64 available;	/* from KFD */
	__u32 gpu_id;		/* to KFD */
	__u32 pad;
};

struct kfd_dbg_device_info_entry {
	__u64 exception_status;
	__u64 lds_base;
	__u64 lds_limit;
	__u64 scratch_base;
	__u64 scratch_limit;
	__u64 gpuvm_base;
	__u64 gpuvm_limit;
	__u32 gpu_id;
	__u32 location_id;
	__u32 vendor_id;
	__u32 device_id;
	__u32 revision_id;
	__u32 subsystem_vendor_id;
	__u32 subsystem_device_id;
	__u32 fw_version;
	__u32 gfx_target_version;
	__u32 simd_count;
	__u32 max_waves_per_simd;
	__u32 array_count;
	__u32 simd_arrays_per_engine;
	__u32 num_xcc;
	__u32 capability;
	__u32 debug_prop;
};

/* For kfd_ioctl_set_memory_policy_args.default_policy and alternate_policy */
#define KFD_IOC_CACHE_POLICY_COHERENT 0
#define KFD_IOC_CACHE_POLICY_NONCOHERENT 1

struct kfd_ioctl_set_memory_policy_args {
	__u64 alternate_aperture_base;	/* to KFD */
	__u64 alternate_aperture_size;	/* to KFD */

	__u32 gpu_id;			/* to KFD */
	__u32 default_policy;		/* to KFD */
	__u32 alternate_policy;		/* to KFD */
	__u32 pad;
};

/*
 * All counters are monotonic. They are used for profiling of compute jobs.
 * The profiling is done by userspace.
 *
 * In case of GPU reset, the counter should not be affected.
 */

struct kfd_ioctl_get_clock_counters_args {
	__u64 gpu_clock_counter;	/* from KFD */
	__u64 cpu_clock_counter;	/* from KFD */
	__u64 system_clock_counter;	/* from KFD */
	__u64 system_clock_freq;	/* from KFD */

	__u32 gpu_id;		/* to KFD */
	__u32 pad;
};

struct kfd_process_device_apertures {
	__u64 lds_base;		/* from KFD */
	__u64 lds_limit;		/* from KFD */
	__u64 scratch_base;		/* from KFD */
	__u64 scratch_limit;		/* from KFD */
	__u64 gpuvm_base;		/* from KFD */
	__u64 gpuvm_limit;		/* from KFD */
	__u32 gpu_id;		/* from KFD */
	__u32 pad;
};

/*
 * AMDKFD_IOC_GET_PROCESS_APERTURES is deprecated. Use
 * AMDKFD_IOC_GET_PROCESS_APERTURES_NEW instead, which supports an
 * unlimited number of GPUs.
 */
#define NUM_OF_SUPPORTED_GPUS 7
struct kfd_ioctl_get_process_apertures_args {
	struct kfd_process_device_apertures
			process_apertures[NUM_OF_SUPPORTED_GPUS];/* from KFD */

	/* from KFD, should be in the range [1 - NUM_OF_SUPPORTED_GPUS] */
	__u32 num_of_nodes;
	__u32 pad;
};

struct kfd_ioctl_get_process_apertures_new_args {
	/* User allocated. Pointer to struct kfd_process_device_apertures
	 * filled in by Kernel
	 */
	__u64 kfd_process_device_apertures_ptr;
	/* to KFD - indicates amount of memory present in
	 *  kfd_process_device_apertures_ptr
	 * from KFD - Number of entries filled by KFD.
	 */
	__u32 num_of_nodes;
	__u32 pad;
};

#define MAX_ALLOWED_NUM_POINTS    100
#define MAX_ALLOWED_AW_BUFF_SIZE 4096
#define MAX_ALLOWED_WAC_BUFF_SIZE  128

struct kfd_ioctl_dbg_register_args {
	__u32 gpu_id;		/* to KFD */
	__u32 pad;
};

struct kfd_ioctl_dbg_unregister_args {
	__u32 gpu_id;		/* to KFD */
	__u32 pad;
};

struct kfd_ioctl_dbg_address_watch_args {
	__u64 content_ptr;		/* a pointer to the actual content */
	__u32 gpu_id;		/* to KFD */
	__u32 buf_size_in_bytes;	/*including gpu_id and buf_size */
};

struct kfd_ioctl_dbg_wave_control_args {
	__u64 content_ptr;		/* a pointer to the actual content */
	__u32 gpu_id;		/* to KFD */
	__u32 buf_size_in_bytes;	/*including gpu_id and buf_size */
};

#define KFD_INVALID_FD     0xffffffff

/* Matching HSA_EVENTTYPE */
#define KFD_IOC_EVENT_SIGNAL			0
#define KFD_IOC_EVENT_NODECHANGE		1
#define KFD_IOC_EVENT_DEVICESTATECHANGE		2
#define KFD_IOC_EVENT_HW_EXCEPTION		3
#define KFD_IOC_EVENT_SYSTEM_EVENT		4
#define KFD_IOC_EVENT_DEBUG_EVENT		5
#define KFD_IOC_EVENT_PROFILE_EVENT		6
#define KFD_IOC_EVENT_QUEUE_EVENT		7
#define KFD_IOC_EVENT_MEMORY			8

#define KFD_IOC_WAIT_RESULT_COMPLETE		0
#define KFD_IOC_WAIT_RESULT_TIMEOUT		1
#define KFD_IOC_WAIT_RESULT_FAIL		2

#define KFD_SIGNAL_EVENT_LIMIT			4096

/* For kfd_event_data.hw_exception_data.reset_type. */
#define KFD_HW_EXCEPTION_WHOLE_GPU_RESET	0
#define KFD_HW_EXCEPTION_PER_ENGINE_RESET	1

/* For kfd_event_data.hw_exception_data.reset_cause. */
#define KFD_HW_EXCEPTION_GPU_HANG	0
#define KFD_HW_EXCEPTION_ECC		1

/* For kfd_hsa_memory_exception_data.ErrorType */
#define KFD_MEM_ERR_NO_RAS		0
#define KFD_MEM_ERR_SRAM_ECC		1
#define KFD_MEM_ERR_POISON_CONSUMED	2
#define KFD_MEM_ERR_GPU_HANG		3

struct kfd_ioctl_create_event_args {
	__u64 event_page_offset;	/* from KFD */
	__u32 event_trigger_data;	/* from KFD - signal events only */
	__u32 event_type;		/* to KFD */
	__u32 auto_reset;		/* to KFD */
	__u32 node_id;		/* to KFD - only valid for certain
							event types */
	__u32 event_id;		/* from KFD */
	__u32 event_slot_index;	/* from KFD */
};

struct kfd_ioctl_destroy_event_args {
	__u32 event_id;		/* to KFD */
	__u32 pad;
};

struct kfd_ioctl_set_event_args {
	__u32 event_id;		/* to KFD */
	__u32 pad;
};

struct kfd_ioctl_reset_event_args {
	__u32 event_id;		/* to KFD */
	__u32 pad;
};

struct kfd_memory_exception_failure {
	__u32 NotPresent;	/* Page not present or supervisor privilege */
	__u32 ReadOnly;	/* Write access to a read-only page */
	__u32 NoExecute;	/* Execute access to a page marked NX */
	__u32 imprecise;	/* Can't determine the	exact fault address */
};

/* memory exception data */
struct kfd_hsa_memory_exception_data {
	struct kfd_memory_exception_failure failure;
	__u64 va;
	__u32 gpu_id;
	__u32 ErrorType; /* 0 = no RAS error,
			  * 1 = ECC_SRAM,
			  * 2 = Link_SYNFLOOD (poison),
			  * 3 = GPU hang (not attributable to a specific cause),
			  * other values reserved
			  */
};

/* hw exception data */
struct kfd_hsa_hw_exception_data {
	__u32 reset_type;
	__u32 reset_cause;
	__u32 memory_lost;
	__u32 gpu_id;
};

/* hsa signal event data */
struct kfd_hsa_signal_event_data {
	__u64 last_event_age;	/* to and from KFD */
};

/* Event data */
struct kfd_event_data {
	union {
		/* From KFD */
		struct kfd_hsa_memory_exception_data memory_exception_data;
		struct kfd_hsa_hw_exception_data hw_exception_data;
		/* To and From KFD */
		struct kfd_hsa_signal_event_data signal_event_data;
	};
	__u64 kfd_event_data_ext;	/* pointer to an extension structure
					   for future exception types */
	__u32 event_id;		/* to KFD */
	__u32 pad;
};

struct kfd_ioctl_wait_events_args {
	__u64 events_ptr;		/* pointed to struct
					   kfd_event_data array, to KFD */
	__u32 num_events;		/* to KFD */
	__u32 wait_for_all;		/* to KFD */
	__u32 timeout;		/* to KFD */
	__u32 wait_result;		/* from KFD */
};

struct kfd_ioctl_set_scratch_backing_va_args {
	__u64 va_addr;	/* to KFD */
	__u32 gpu_id;	/* to KFD */
	__u32 pad;
};

struct kfd_ioctl_get_tile_config_args {
	/* to KFD: pointer to tile array */
	__u64 tile_config_ptr;
	/* to KFD: pointer to macro tile array */
	__u64 macro_tile_config_ptr;
	/* to KFD: array size allocated by user mode
	 * from KFD: array size filled by kernel
	 */
	__u32 num_tile_configs;
	/* to KFD: array size allocated by user mode
	 * from KFD: array size filled by kernel
	 */
	__u32 num_macro_tile_configs;

	__u32 gpu_id;		/* to KFD */
	__u32 gb_addr_config;	/* from KFD */
	__u32 num_banks;		/* from KFD */
	__u32 num_ranks;		/* from KFD */
	/* struct size can be extended later if needed
	 * without breaking ABI compatibility
	 */
};

struct kfd_ioctl_set_trap_handler_args {
	__u64 tba_addr;		/* to KFD */
	__u64 tma_addr;		/* to KFD */
	__u32 gpu_id;		/* to KFD */
	__u32 pad;
};

struct kfd_ioctl_acquire_vm_args {
	__u32 drm_fd;	/* to KFD */
	__u32 gpu_id;	/* to KFD */
};

/* Allocation flags: memory types */
#define KFD_IOC_ALLOC_MEM_FLAGS_VRAM		(1 << 0)
#define KFD_IOC_ALLOC_MEM_FLAGS_GTT		(1 << 1)
#define KFD_IOC_ALLOC_MEM_FLAGS_USERPTR		(1 << 2)
#define KFD_IOC_ALLOC_MEM_FLAGS_DOORBELL	(1 << 3)
#define KFD_IOC_ALLOC_MEM_FLAGS_MMIO_REMAP	(1 << 4)
/* Allocation flags: attributes/access options */
#define KFD_IOC_ALLOC_MEM_FLAGS_WRITABLE	(1 << 31)
#define KFD_IOC_ALLOC_MEM_FLAGS_EXECUTABLE	(1 << 30)
#define KFD_IOC_ALLOC_MEM_FLAGS_PUBLIC		(1 << 29)
#define KFD_IOC_ALLOC_MEM_FLAGS_NO_SUBSTITUTE	(1 << 28)
#define KFD_IOC_ALLOC_MEM_FLAGS_AQL_QUEUE_MEM	(1 << 27)
#define KFD_IOC_ALLOC_MEM_FLAGS_COHERENT	(1 << 26)
#define KFD_IOC_ALLOC_MEM_FLAGS_UNCACHED	(1 << 25)
#define KFD_IOC_ALLOC_MEM_FLAGS_EXT_COHERENT	(1 << 24)

/* Allocate memory for later SVM (shared virtual memory) mapping.
 *
 * @va_addr:     virtual address of the memory to be allocated
 *               all later mappings on all GPUs will use this address
 * @size:        size in bytes
 * @handle:      buffer handle returned to user mode, used to refer to
 *               this allocation for mapping, unmapping and freeing
 * @mmap_offset: for CPU-mapping the allocation by mmapping a render node
 *               for userptrs this is overloaded to specify the CPU address
 * @gpu_id:      device identifier
 * @flags:       memory type and attributes. See KFD_IOC_ALLOC_MEM_FLAGS above
 */
struct kfd_ioctl_alloc_memory_of_gpu_args {
	__u64 va_addr;		/* to KFD */
	__u64 size;		/* to KFD */
	__u64 handle;		/* from KFD */
	__u64 mmap_offset;	/* to KFD (userptr), from KFD (mmap offset) */
	__u32 gpu_id;		/* to KFD */
	__u32 flags;
};

/* Free memory allocated with kfd_ioctl_alloc_memory_of_gpu
 *
 * @handle: memory handle returned by alloc
 */
struct kfd_ioctl_free_memory_of_gpu_args {
	__u64 handle;		/* to KFD */
};

/* Map memory to one or more GPUs
 *
 * @handle:                memory handle returned by alloc
 * @device_ids_array_ptr:  array of gpu_ids (__u32 per device)
 * @n_devices:             number of devices in the array
 * @n_success:             number of devices mapped successfully
 *
 * @n_success returns information to the caller how many devices from
 * the start of the array have mapped the buffer successfully. It can
 * be passed into a subsequent retry call to skip those devices. For
 * the first call the caller should initialize it to 0.
 *
 * If the ioctl completes with return code 0 (success), n_success ==
 * n_devices.
 */
struct kfd_ioctl_map_memory_to_gpu_args {
	__u64 handle;			/* to KFD */
	__u64 device_ids_array_ptr;	/* to KFD */
	__u32 n_devices;		/* to KFD */
	__u32 n_success;		/* to/from KFD */
};

/* Unmap memory from one or more GPUs
 *
 * same arguments as for mapping
 */
struct kfd_ioctl_unmap_memory_from_gpu_args {
	__u64 handle;			/* to KFD */
	__u64 device_ids_array_ptr;	/* to KFD */
	__u32 n_devices;		/* to KFD */
	__u32 n_success;		/* to/from KFD */
};

/* Allocate GWS for specific queue
 *
 * @queue_id:    queue's id that GWS is allocated for
 * @num_gws:     how many GWS to allocate
 * @first_gws:   index of the first GWS allocated.
 *               only support contiguous GWS allocation
 */
struct kfd_ioctl_alloc_queue_gws_args {
	__u32 queue_id;		/* to KFD */
	__u32 num_gws;		/* to KFD */
	__u32 first_gws;	/* from KFD */
	__u32 pad;
};

struct kfd_ioctl_get_dmabuf_info_args {
	__u64 size;		/* from KFD */
	__u64 metadata_ptr;	/* to KFD */
	__u32 metadata_size;	/* to KFD (space allocated by user)
				 * from KFD (actual metadata size)
				 */
	__u32 gpu_id;	/* from KFD */
	__u32 flags;		/* from KFD (KFD_IOC_ALLOC_MEM_FLAGS) */
	__u32 dmabuf_fd;	/* to KFD */
};

struct kfd_ioctl_import_dmabuf_args {
	__u64 va_addr;	/* to KFD */
	__u64 handle;	/* from KFD */
	__u32 gpu_id;	/* to KFD */
	__u32 dmabuf_fd;	/* to KFD */
};

struct kfd_ioctl_export_dmabuf_args {
	__u64 handle;		/* to KFD */
	__u32 flags;		/* to KFD */
	__u32 dmabuf_fd;	/* from KFD */
};

/*
 * KFD SMI(System Management Interface) events
 */
enum kfd_smi_event {
	KFD_SMI_EVENT_NONE = 0, /* not used */
	KFD_SMI_EVENT_VMFAULT = 1, /* event start counting at 1 */
	KFD_SMI_EVENT_THERMAL_THROTTLE = 2,
	KFD_SMI_EVENT_GPU_PRE_RESET = 3,
	KFD_SMI_EVENT_GPU_POST_RESET = 4,
	KFD_SMI_EVENT_MIGRATE_START = 5,
	KFD_SMI_EVENT_MIGRATE_END = 6,
	KFD_SMI_EVENT_PAGE_FAULT_START = 7,
	KFD_SMI_EVENT_PAGE_FAULT_END = 8,
	KFD_SMI_EVENT_QUEUE_EVICTION = 9,
	KFD_SMI_EVENT_QUEUE_RESTORE = 10,
	KFD_SMI_EVENT_UNMAP_FROM_GPU = 11,

	/*
	 * max event number, as a flag bit to get events from all processes,
	 * this requires super user permission, otherwise will not be able to
	 * receive event from any process. Without this flag to receive events
	 * from same process.
	 */
	KFD_SMI_EVENT_ALL_PROCESS = 64
};

enum KFD_MIGRATE_TRIGGERS {
	KFD_MIGRATE_TRIGGER_PREFETCH,
	KFD_MIGRATE_TRIGGER_PAGEFAULT_GPU,
	KFD_MIGRATE_TRIGGER_PAGEFAULT_CPU,
	KFD_MIGRATE_TRIGGER_TTM_EVICTION
};

enum KFD_QUEUE_EVICTION_TRIGGERS {
	KFD_QUEUE_EVICTION_TRIGGER_SVM,
	KFD_QUEUE_EVICTION_TRIGGER_USERPTR,
	KFD_QUEUE_EVICTION_TRIGGER_TTM,
	KFD_QUEUE_EVICTION_TRIGGER_SUSPEND,
	KFD_QUEUE_EVICTION_CRIU_CHECKPOINT,
	KFD_QUEUE_EVICTION_CRIU_RESTORE
};

enum KFD_SVM_UNMAP_TRIGGERS {
	KFD_SVM_UNMAP_TRIGGER_MMU_NOTIFY,
	KFD_SVM_UNMAP_TRIGGER_MMU_NOTIFY_MIGRATE,
	KFD_SVM_UNMAP_TRIGGER_UNMAP_FROM_CPU
};

#define KFD_SMI_EVENT_MASK_FROM_INDEX(i) (1ULL << ((i) - 1))
#define KFD_SMI_EVENT_MSG_SIZE	96

struct kfd_ioctl_smi_events_args {
	__u32 gpuid;	/* to KFD */
	__u32 anon_fd;	/* from KFD */
};

/**************************************************************************************************
 * CRIU IOCTLs (Checkpoint Restore In Userspace)
 *
 * When checkpointing a process, the userspace application will perform:
 * 1. PROCESS_INFO op to determine current process information. This pauses execution and evicts
 *    all the queues.
 * 2. CHECKPOINT op to checkpoint process contents (BOs, queues, events, svm-ranges)
 * 3. UNPAUSE op to un-evict all the queues
 *
 * When restoring a process, the CRIU userspace application will perform:
 *
 * 1. RESTORE op to restore process contents
 * 2. RESUME op to start the process
 *
 * Note: Queues are forced into an evicted state after a successful PROCESS_INFO. User
 * application needs to perform an UNPAUSE operation after calling PROCESS_INFO.
 */

enum kfd_criu_op {
	KFD_CRIU_OP_PROCESS_INFO,
	KFD_CRIU_OP_CHECKPOINT,
	KFD_CRIU_OP_UNPAUSE,
	KFD_CRIU_OP_RESTORE,
	KFD_CRIU_OP_RESUME,
};

/**
 * kfd_ioctl_criu_args - Arguments perform CRIU operation
 * @devices:		[in/out] User pointer to memory location for devices information.
 * 			This is an array of type kfd_criu_device_bucket.
 * @bos:		[in/out] User pointer to memory location for BOs information
 * 			This is an array of type kfd_criu_bo_bucket.
 * @priv_data:		[in/out] User pointer to memory location for private data
 * @priv_data_size:	[in/out] Size of priv_data in bytes
 * @num_devices:	[in/out] Number of GPUs used by process. Size of @devices array.
 * @num_bos		[in/out] Number of BOs used by process. Size of @bos array.
 * @num_objects:	[in/out] Number of objects used by process. Objects are opaque to
 *				 user application.
 * @pid:		[in/out] PID of the process being checkpointed
 * @op			[in] Type of operation (kfd_criu_op)
 *
 * Return: 0 on success, -errno on failure
 */
struct kfd_ioctl_criu_args {
	__u64 devices;		/* Used during ops: CHECKPOINT, RESTORE */
	__u64 bos;		/* Used during ops: CHECKPOINT, RESTORE */
	__u64 priv_data;	/* Used during ops: CHECKPOINT, RESTORE */
	__u64 priv_data_size;	/* Used during ops: PROCESS_INFO, RESTORE */
	__u32 num_devices;	/* Used during ops: PROCESS_INFO, RESTORE */
	__u32 num_bos;		/* Used during ops: PROCESS_INFO, RESTORE */
	__u32 num_objects;	/* Used during ops: PROCESS_INFO, RESTORE */
	__u32 pid;		/* Used during ops: PROCESS_INFO, RESUME */
	__u32 op;
};

struct kfd_criu_device_bucket {
	__u32 user_gpu_id;
	__u32 actual_gpu_id;
	__u32 drm_fd;
	__u32 pad;
};

struct kfd_criu_bo_bucket {
	__u64 addr;
	__u64 size;
	__u64 offset;
	__u64 restored_offset;    /* During restore, updated offset for BO */
	__u32 gpu_id;             /* This is the user_gpu_id */
	__u32 alloc_flags;
	__u32 dmabuf_fd;
	__u32 pad;
};

/* CRIU IOCTLs - END */
/**************************************************************************************************/

/* Register offset inside the remapped mmio page
 */
enum kfd_mmio_remap {
	KFD_MMIO_REMAP_HDP_MEM_FLUSH_CNTL = 0,
	KFD_MMIO_REMAP_HDP_REG_FLUSH_CNTL = 4,
};

/* Guarantee host access to memory */
#define KFD_IOCTL_SVM_FLAG_HOST_ACCESS 0x00000001
/* Fine grained coherency between all devices with access */
#define KFD_IOCTL_SVM_FLAG_COHERENT    0x00000002
/* Use any GPU in same hive as preferred device */
#define KFD_IOCTL_SVM_FLAG_HIVE_LOCAL  0x00000004
/* GPUs only read, allows replication */
#define KFD_IOCTL_SVM_FLAG_GPU_RO      0x00000008
/* Allow execution on GPU */
#define KFD_IOCTL_SVM_FLAG_GPU_EXEC    0x00000010
/* GPUs mostly read, may allow similar optimizations as RO, but writes fault */
#define KFD_IOCTL_SVM_FLAG_GPU_READ_MOSTLY     0x00000020
/* Keep GPU memory mapping always valid as if XNACK is disable */
#define KFD_IOCTL_SVM_FLAG_GPU_ALWAYS_MAPPED   0x00000040
/* Fine grained coherency between all devices using device-scope atomics */
#define KFD_IOCTL_SVM_FLAG_EXT_COHERENT        0x00000080

/**
 * kfd_ioctl_svm_op - SVM ioctl operations
 *
 * @KFD_IOCTL_SVM_OP_SET_ATTR: Modify one or more attributes
 * @KFD_IOCTL_SVM_OP_GET_ATTR: Query one or more attributes
 */
enum kfd_ioctl_svm_op {
	KFD_IOCTL_SVM_OP_SET_ATTR,
	KFD_IOCTL_SVM_OP_GET_ATTR
};

/** kfd_ioctl_svm_location - Enum for preferred and prefetch locations
 *
 * GPU IDs are used to specify GPUs as preferred and prefetch locations.
 * Below definitions are used for system memory or for leaving the preferred
 * location unspecified.
 */
enum kfd_ioctl_svm_location {
	KFD_IOCTL_SVM_LOCATION_SYSMEM = 0,
	KFD_IOCTL_SVM_LOCATION_UNDEFINED = 0xffffffff
};

/**
 * kfd_ioctl_svm_attr_type - SVM attribute types
 *
 * @KFD_IOCTL_SVM_ATTR_PREFERRED_LOC: gpuid of the preferred location, 0 for
 *                                    system memory
 * @KFD_IOCTL_SVM_ATTR_PREFETCH_LOC: gpuid of the prefetch location, 0 for
 *                                   system memory. Setting this triggers an
 *                                   immediate prefetch (migration).
 * @KFD_IOCTL_SVM_ATTR_ACCESS:
 * @KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE:
 * @KFD_IOCTL_SVM_ATTR_NO_ACCESS: specify memory access for the gpuid given
 *                                by the attribute value
 * @KFD_IOCTL_SVM_ATTR_SET_FLAGS: bitmask of flags to set (see
 *                                KFD_IOCTL_SVM_FLAG_...)
 * @KFD_IOCTL_SVM_ATTR_CLR_FLAGS: bitmask of flags to clear
 * @KFD_IOCTL_SVM_ATTR_GRANULARITY: migration granularity
 *                                  (log2 num pages)
 */
enum kfd_ioctl_svm_attr_type {
	KFD_IOCTL_SVM_ATTR_PREFERRED_LOC,
	KFD_IOCTL_SVM_ATTR_PREFETCH_LOC,
	KFD_IOCTL_SVM_ATTR_ACCESS,
	KFD_IOCTL_SVM_ATTR_ACCESS_IN_PLACE,
	KFD_IOCTL_SVM_ATTR_NO_ACCESS,
	KFD_IOCTL_SVM_ATTR_SET_FLAGS,
	KFD_IOCTL_SVM_ATTR_CLR_FLAGS,
	KFD_IOCTL_SVM_ATTR_GRANULARITY
};

/**
 * kfd_ioctl_svm_attribute - Attributes as pairs of type and value
 *
 * The meaning of the @value depends on the attribute type.
 *
 * @type: attribute type (see enum @kfd_ioctl_svm_attr_type)
 * @value: attribute value
 */
struct kfd_ioctl_svm_attribute {
	__u32 type;
	__u32 value;
};

/**
 * kfd_ioctl_svm_args - Arguments for SVM ioctl
 *
 * @op specifies the operation to perform (see enum
 * @kfd_ioctl_svm_op).  @start_addr and @size are common for all
 * operations.
 *
 * A variable number of attributes can be given in @attrs.
 * @nattr specifies the number of attributes. New attributes can be
 * added in the future without breaking the ABI. If unknown attributes
 * are given, the function returns -EINVAL.
 *
 * @KFD_IOCTL_SVM_OP_SET_ATTR sets attributes for a virtual address
 * range. It may overlap existing virtual address ranges. If it does,
 * the existing ranges will be split such that the attribute changes
 * only apply to the specified address range.
 *
 * @KFD_IOCTL_SVM_OP_GET_ATTR returns the intersection of attributes
 * over all memory in the given range and returns the result as the
 * attribute value. If different pages have different preferred or
 * prefetch locations, 0xffffffff will be returned for
 * @KFD_IOCTL_SVM_ATTR_PREFERRED_LOC or
 * @KFD_IOCTL_SVM_ATTR_PREFETCH_LOC resepctively. For
 * @KFD_IOCTL_SVM_ATTR_SET_FLAGS, flags of all pages will be
 * aggregated by bitwise AND. That means, a flag will be set in the
 * output, if that flag is set for all pages in the range. For
 * @KFD_IOCTL_SVM_ATTR_CLR_FLAGS, flags of all pages will be
 * aggregated by bitwise NOR. That means, a flag will be set in the
 * output, if that flag is clear for all pages in the range.
 * The minimum migration granularity throughout the range will be
 * returned for @KFD_IOCTL_SVM_ATTR_GRANULARITY.
 *
 * Querying of accessibility attributes works by initializing the
 * attribute type to @KFD_IOCTL_SVM_ATTR_ACCESS and the value to the
 * GPUID being queried. Multiple attributes can be given to allow
 * querying multiple GPUIDs. The ioctl function overwrites the
 * attribute type to indicate the access for the specified GPU.
 */
struct kfd_ioctl_svm_args {
	__u64 start_addr;
	__u64 size;
	__u32 op;
	__u32 nattr;
	/* Variable length array of attributes */
	struct kfd_ioctl_svm_attribute attrs[];
};

/**
 * kfd_ioctl_set_xnack_mode_args - Arguments for set_xnack_mode
 *
 * @xnack_enabled:       [in/out] Whether to enable XNACK mode for this process
 *
 * @xnack_enabled indicates whether recoverable page faults should be
 * enabled for the current process. 0 means disabled, positive means
 * enabled, negative means leave unchanged. If enabled, virtual address
 * translations on GFXv9 and later AMD GPUs can return XNACK and retry
 * the access until a valid PTE is available. This is used to implement
 * device page faults.
 *
 * On output, @xnack_enabled returns the (new) current mode (0 or
 * positive). Therefore, a negative input value can be used to query
 * the current mode without changing it.
 *
 * The XNACK mode fundamentally changes the way SVM managed memory works
 * in the driver, with subtle effects on application performance and
 * functionality.
 *
 * Enabling XNACK mode requires shader programs to be compiled
 * differently. Furthermore, not all GPUs support changing the mode
 * per-process. Therefore changing the mode is only allowed while no
 * user mode queues exist in the process. This ensure that no shader
 * code is running that may be compiled for the wrong mode. And GPUs
 * that cannot change to the requested mode will prevent the XNACK
 * mode from occurring. All GPUs used by the process must be in the
 * same XNACK mode.
 *
 * GFXv8 or older GPUs do not support 48 bit virtual addresses or SVM.
 * Therefore those GPUs are not considered for the XNACK mode switch.
 *
 * Return: 0 on success, -errno on failure
 */
struct kfd_ioctl_set_xnack_mode_args {
	__s32 xnack_enabled;
};

/* Wave launch override modes */
enum kfd_dbg_trap_override_mode {
	KFD_DBG_TRAP_OVERRIDE_OR = 0,
	KFD_DBG_TRAP_OVERRIDE_REPLACE = 1
};

/* Wave launch overrides */
enum kfd_dbg_trap_mask {
	KFD_DBG_TRAP_MASK_FP_INVALID = 1,
	KFD_DBG_TRAP_MASK_FP_INPUT_DENORMAL = 2,
	KFD_DBG_TRAP_MASK_FP_DIVIDE_BY_ZERO = 4,
	KFD_DBG_TRAP_MASK_FP_OVERFLOW = 8,
	KFD_DBG_TRAP_MASK_FP_UNDERFLOW = 16,
	KFD_DBG_TRAP_MASK_FP_INEXACT = 32,
	KFD_DBG_TRAP_MASK_INT_DIVIDE_BY_ZERO = 64,
	KFD_DBG_TRAP_MASK_DBG_ADDRESS_WATCH = 128,
	KFD_DBG_TRAP_MASK_DBG_MEMORY_VIOLATION = 256,
	KFD_DBG_TRAP_MASK_TRAP_ON_WAVE_START = (1 << 30),
	KFD_DBG_TRAP_MASK_TRAP_ON_WAVE_END = (1 << 31)
};

/* Wave launch modes */
enum kfd_dbg_trap_wave_launch_mode {
	KFD_DBG_TRAP_WAVE_LAUNCH_MODE_NORMAL = 0,
	KFD_DBG_TRAP_WAVE_LAUNCH_MODE_HALT = 1,
	KFD_DBG_TRAP_WAVE_LAUNCH_MODE_DEBUG = 3
};

/* Address watch modes */
enum kfd_dbg_trap_address_watch_mode {
	KFD_DBG_TRAP_ADDRESS_WATCH_MODE_READ = 0,
	KFD_DBG_TRAP_ADDRESS_WATCH_MODE_NONREAD = 1,
	KFD_DBG_TRAP_ADDRESS_WATCH_MODE_ATOMIC = 2,
	KFD_DBG_TRAP_ADDRESS_WATCH_MODE_ALL = 3
};

/* Additional wave settings */
enum kfd_dbg_trap_flags {
	KFD_DBG_TRAP_FLAG_SINGLE_MEM_OP = 1,
};

/* Trap exceptions */
enum kfd_dbg_trap_exception_code {
	EC_NONE = 0,
	/* per queue */
	EC_QUEUE_WAVE_ABORT = 1,
	EC_QUEUE_WAVE_TRAP = 2,
	EC_QUEUE_WAVE_MATH_ERROR = 3,
	EC_QUEUE_WAVE_ILLEGAL_INSTRUCTION = 4,
	EC_QUEUE_WAVE_MEMORY_VIOLATION = 5,
	EC_QUEUE_WAVE_APERTURE_VIOLATION = 6,
	EC_QUEUE_PACKET_DISPATCH_DIM_INVALID = 16,
	EC_QUEUE_PACKET_DISPATCH_GROUP_SEGMENT_SIZE_INVALID = 17,
	EC_QUEUE_PACKET_DISPATCH_CODE_INVALID = 18,
	EC_QUEUE_PACKET_RESERVED = 19,
	EC_QUEUE_PACKET_UNSUPPORTED = 20,
	EC_QUEUE_PACKET_DISPATCH_WORK_GROUP_SIZE_INVALID = 21,
	EC_QUEUE_PACKET_DISPATCH_REGISTER_INVALID = 22,
	EC_QUEUE_PACKET_VENDOR_UNSUPPORTED = 23,
	EC_QUEUE_PREEMPTION_ERROR = 30,
	EC_QUEUE_NEW = 31,
	/* per device */
	EC_DEVICE_QUEUE_DELETE = 32,
	EC_DEVICE_MEMORY_VIOLATION = 33,
	EC_DEVICE_RAS_ERROR = 34,
	EC_DEVICE_FATAL_HALT = 35,
	EC_DEVICE_NEW = 36,
	/* per process */
	EC_PROCESS_RUNTIME = 48,
	EC_PROCESS_DEVICE_REMOVE = 49,
	EC_MAX
};

/* Mask generated by ecode in kfd_dbg_trap_exception_code */
#define KFD_EC_MASK(ecode)	(1ULL << (ecode - 1))

/* Masks for exception code type checks below */
#define KFD_EC_MASK_QUEUE	(KFD_EC_MASK(EC_QUEUE_WAVE_ABORT) |	\
				 KFD_EC_MASK(EC_QUEUE_WAVE_TRAP) |	\
				 KFD_EC_MASK(EC_QUEUE_WAVE_MATH_ERROR) |	\
				 KFD_EC_MASK(EC_QUEUE_WAVE_ILLEGAL_INSTRUCTION) |	\
				 KFD_EC_MASK(EC_QUEUE_WAVE_MEMORY_VIOLATION) |	\
				 KFD_EC_MASK(EC_QUEUE_WAVE_APERTURE_VIOLATION) |	\
				 KFD_EC_MASK(EC_QUEUE_PACKET_DISPATCH_DIM_INVALID) |	\
				 KFD_EC_MASK(EC_QUEUE_PACKET_DISPATCH_GROUP_SEGMENT_SIZE_INVALID) |	\
				 KFD_EC_MASK(EC_QUEUE_PACKET_DISPATCH_CODE_INVALID) |	\
				 KFD_EC_MASK(EC_QUEUE_PACKET_RESERVED) |	\
				 KFD_EC_MASK(EC_QUEUE_PACKET_UNSUPPORTED) |	\
				 KFD_EC_MASK(EC_QUEUE_PACKET_DISPATCH_WORK_GROUP_SIZE_INVALID) |	\
				 KFD_EC_MASK(EC_QUEUE_PACKET_DISPATCH_REGISTER_INVALID) |	\
				 KFD_EC_MASK(EC_QUEUE_PACKET_VENDOR_UNSUPPORTED)	|	\
				 KFD_EC_MASK(EC_QUEUE_PREEMPTION_ERROR)	|	\
				 KFD_EC_MASK(EC_QUEUE_NEW))
#define KFD_EC_MASK_DEVICE	(KFD_EC_MASK(EC_DEVICE_QUEUE_DELETE) |		\
				 KFD_EC_MASK(EC_DEVICE_RAS_ERROR) |		\
				 KFD_EC_MASK(EC_DEVICE_FATAL_HALT) |		\
				 KFD_EC_MASK(EC_DEVICE_MEMORY_VIOLATION) |	\
				 KFD_EC_MASK(EC_DEVICE_NEW))
#define KFD_EC_MASK_PROCESS	(KFD_EC_MASK(EC_PROCESS_RUNTIME) |	\
				 KFD_EC_MASK(EC_PROCESS_DEVICE_REMOVE))

/* Checks for exception code types for KFD search */
#define KFD_DBG_EC_TYPE_IS_QUEUE(ecode)					\
			(!!(KFD_EC_MASK(ecode) & KFD_EC_MASK_QUEUE))
#define KFD_DBG_EC_TYPE_IS_DEVICE(ecode)				\
			(!!(KFD_EC_MASK(ecode) & KFD_EC_MASK_DEVICE))
#define KFD_DBG_EC_TYPE_IS_PROCESS(ecode)				\
			(!!(KFD_EC_MASK(ecode) & KFD_EC_MASK_PROCESS))


/* Runtime enable states */
enum kfd_dbg_runtime_state {
	DEBUG_RUNTIME_STATE_DISABLED = 0,
	DEBUG_RUNTIME_STATE_ENABLED = 1,
	DEBUG_RUNTIME_STATE_ENABLED_BUSY = 2,
	DEBUG_RUNTIME_STATE_ENABLED_ERROR = 3
};

/* Runtime enable status */
struct kfd_runtime_info {
	__u64 r_debug;
	__u32 runtime_state;
	__u32 ttmp_setup;
};

/* Enable modes for runtime enable */
#define KFD_RUNTIME_ENABLE_MODE_ENABLE_MASK	1
#define KFD_RUNTIME_ENABLE_MODE_TTMP_SAVE_MASK	2

/**
 * kfd_ioctl_runtime_enable_args - Arguments for runtime enable
 *
 * Coordinates debug exception signalling and debug device enablement with runtime.
 *
 * @r_debug - pointer to user struct for sharing information between ROCr and the debuggger
 * @mode_mask - mask to set mode
 *	KFD_RUNTIME_ENABLE_MODE_ENABLE_MASK - enable runtime for debugging, otherwise disable
 *	KFD_RUNTIME_ENABLE_MODE_TTMP_SAVE_MASK - enable trap temporary setup (ignore on disable)
 * @capabilities_mask - mask to notify runtime on what KFD supports
 *
 * Return - 0 on SUCCESS.
 *	  - EBUSY if runtime enable call already pending.
 *	  - EEXIST if user queues already active prior to call.
 *	    If process is debug enabled, runtime enable will enable debug devices and
 *	    wait for debugger process to send runtime exception EC_PROCESS_RUNTIME
 *	    to unblock - see kfd_ioctl_dbg_trap_args.
 *
 */
struct kfd_ioctl_runtime_enable_args {
	__u64 r_debug;
	__u32 mode_mask;
	__u32 capabilities_mask;
};

/* Queue information */
struct kfd_queue_snapshot_entry {
	__u64 exception_status;
	__u64 ring_base_address;
	__u64 write_pointer_address;
	__u64 read_pointer_address;
	__u64 ctx_save_restore_address;
	__u32 queue_id;
	__u32 gpu_id;
	__u32 ring_size;
	__u32 queue_type;
	__u32 ctx_save_restore_area_size;
	__u32 reserved;
};

/* Queue status return for suspend/resume */
#define KFD_DBG_QUEUE_ERROR_BIT		30
#define KFD_DBG_QUEUE_INVALID_BIT	31
#define KFD_DBG_QUEUE_ERROR_MASK	(1 << KFD_DBG_QUEUE_ERROR_BIT)
#define KFD_DBG_QUEUE_INVALID_MASK	(1 << KFD_DBG_QUEUE_INVALID_BIT)

/* Context save area header information */
struct kfd_context_save_area_header {
	struct {
		__u32 control_stack_offset;
		__u32 control_stack_size;
		__u32 wave_state_offset;
		__u32 wave_state_size;
	} wave_state;
	__u32 debug_offset;
	__u32 debug_size;
	__u64 err_payload_addr;
	__u32 err_event_id;
	__u32 reserved1;
};

/*
 * Debug operations
 *
 * For specifics on usage and return values, see documentation per operation
 * below.  Otherwise, generic error returns apply:
 *	- ESRCH if the process to debug does not exist.
 *
 *	- EINVAL (with KFD_IOC_DBG_TRAP_ENABLE exempt) if operation
 *		 KFD_IOC_DBG_TRAP_ENABLE has not succeeded prior.
 *		 Also returns this error if GPU hardware scheduling is not supported.
 *
 *	- EPERM (with KFD_IOC_DBG_TRAP_DISABLE exempt) if target process is not
 *		 PTRACE_ATTACHED.  KFD_IOC_DBG_TRAP_DISABLE is exempt to allow
 *		 clean up of debug mode as long as process is debug enabled.
 *
 *	- EACCES if any DBG_HW_OP (debug hardware operation) is requested when
 *		 AMDKFD_IOC_RUNTIME_ENABLE has not succeeded prior.
 *
 *	- ENODEV if any GPU does not support debugging on a DBG_HW_OP call.
 *
 *	- Other errors may be returned when a DBG_HW_OP occurs while the GPU
 *	  is in a fatal state.
 *
 */
enum kfd_dbg_trap_operations {
	KFD_IOC_DBG_TRAP_ENABLE = 0,
	KFD_IOC_DBG_TRAP_DISABLE = 1,
	KFD_IOC_DBG_TRAP_SEND_RUNTIME_EVENT = 2,
	KFD_IOC_DBG_TRAP_SET_EXCEPTIONS_ENABLED = 3,
	KFD_IOC_DBG_TRAP_SET_WAVE_LAUNCH_OVERRIDE = 4,  /* DBG_HW_OP */
	KFD_IOC_DBG_TRAP_SET_WAVE_LAUNCH_MODE = 5,      /* DBG_HW_OP */
	KFD_IOC_DBG_TRAP_SUSPEND_QUEUES = 6,		/* DBG_HW_OP */
	KFD_IOC_DBG_TRAP_RESUME_QUEUES = 7,		/* DBG_HW_OP */
	KFD_IOC_DBG_TRAP_SET_NODE_ADDRESS_WATCH = 8,	/* DBG_HW_OP */
	KFD_IOC_DBG_TRAP_CLEAR_NODE_ADDRESS_WATCH = 9,	/* DBG_HW_OP */
	KFD_IOC_DBG_TRAP_SET_FLAGS = 10,
	KFD_IOC_DBG_TRAP_QUERY_DEBUG_EVENT = 11,
	KFD_IOC_DBG_TRAP_QUERY_EXCEPTION_INFO = 12,
	KFD_IOC_DBG_TRAP_GET_QUEUE_SNAPSHOT = 13,
	KFD_IOC_DBG_TRAP_GET_DEVICE_SNAPSHOT = 14
};

/**
 * kfd_ioctl_dbg_trap_enable_args
 *
 *     Arguments for KFD_IOC_DBG_TRAP_ENABLE.
 *
 *     Enables debug session for target process. Call @op KFD_IOC_DBG_TRAP_DISABLE in
 *     kfd_ioctl_dbg_trap_args to disable debug session.
 *
 *     @exception_mask (IN)	- exceptions to raise to the debugger
 *     @rinfo_ptr      (IN)	- pointer to runtime info buffer (see kfd_runtime_info)
 *     @rinfo_size     (IN/OUT)	- size of runtime info buffer in bytes
 *     @dbg_fd	       (IN)	- fd the KFD will nofify the debugger with of raised
 *				  exceptions set in exception_mask.
 *
 *     Generic errors apply (see kfd_dbg_trap_operations).
 *     Return - 0 on SUCCESS.
 *		Copies KFD saved kfd_runtime_info to @rinfo_ptr on enable.
 *		Size of kfd_runtime saved by the KFD returned to @rinfo_size.
 *            - EBADF if KFD cannot get a reference to dbg_fd.
 *            - EFAULT if KFD cannot copy runtime info to rinfo_ptr.
 *            - EINVAL if target process is already debug enabled.
 *
 */
struct kfd_ioctl_dbg_trap_enable_args {
	__u64 exception_mask;
	__u64 rinfo_ptr;
	__u32 rinfo_size;
	__u32 dbg_fd;
};

/**
 * kfd_ioctl_dbg_trap_send_runtime_event_args
 *
 *
 *     Arguments for KFD_IOC_DBG_TRAP_SEND_RUNTIME_EVENT.
 *     Raises exceptions to runtime.
 *
 *     @exception_mask (IN) - exceptions to raise to runtime
 *     @gpu_id	       (IN) - target device id
 *     @queue_id       (IN) - target queue id
 *
 *     Generic errors apply (see kfd_dbg_trap_operations).
 *     Return - 0 on SUCCESS.
 *	      - ENODEV if gpu_id not found.
 *		If exception_mask contains EC_PROCESS_RUNTIME, unblocks pending
 *		AMDKFD_IOC_RUNTIME_ENABLE call - see kfd_ioctl_runtime_enable_args.
 *		All other exceptions are raised to runtime through err_payload_addr.
 *		See kfd_context_save_area_header.
 */
struct kfd_ioctl_dbg_trap_send_runtime_event_args {
	__u64 exception_mask;
	__u32 gpu_id;
	__u32 queue_id;
};

/**
 * kfd_ioctl_dbg_trap_set_exceptions_enabled_args
 *
 *     Arguments for KFD_IOC_SET_EXCEPTIONS_ENABLED
 *     Set new exceptions to be raised to the debugger.
 *
 *     @exception_mask (IN) - new exceptions to raise the debugger
 *
 *     Generic errors apply (see kfd_dbg_trap_operations).
 *     Return - 0 on SUCCESS.
 */
struct kfd_ioctl_dbg_trap_set_exceptions_enabled_args {
	__u64 exception_mask;
};

/**
 * kfd_ioctl_dbg_trap_set_wave_launch_override_args
 *
 *     Arguments for KFD_IOC_DBG_TRAP_SET_WAVE_LAUNCH_OVERRIDE
 *     Enable HW exceptions to raise trap.
 *
 *     @override_mode	     (IN)     - see kfd_dbg_trap_override_mode
 *     @enable_mask	     (IN/OUT) - reference kfd_dbg_trap_mask.
 *					IN is the override modes requested to be enabled.
 *					OUT is referenced in Return below.
 *     @support_request_mask (IN/OUT) - reference kfd_dbg_trap_mask.
 *					IN is the override modes requested for support check.
 *					OUT is referenced in Return below.
 *
 *     Generic errors apply (see kfd_dbg_trap_operations).
 *     Return - 0 on SUCCESS.
 *		Previous enablement is returned in @enable_mask.
 *		Actual override support is returned in @support_request_mask.
 *	      - EINVAL if override mode is not supported.
 *	      - EACCES if trap support requested is not actually supported.
 *		i.e. enable_mask (IN) is not a subset of support_request_mask (OUT).
 *		Otherwise it is considered a generic error (see kfd_dbg_trap_operations).
 */
struct kfd_ioctl_dbg_trap_set_wave_launch_override_args {
	__u32 override_mode;
	__u32 enable_mask;
	__u32 support_request_mask;
	__u32 pad;
};

/**
 * kfd_ioctl_dbg_trap_set_wave_launch_mode_args
 *
 *     Arguments for KFD_IOC_DBG_TRAP_SET_WAVE_LAUNCH_MODE
 *     Set wave launch mode.
 *
 *     @mode (IN) - see kfd_dbg_trap_wave_launch_mode
 *
 *     Generic errors apply (see kfd_dbg_trap_operations).
 *     Return - 0 on SUCCESS.
 */
struct kfd_ioctl_dbg_trap_set_wave_launch_mode_args {
	__u32 launch_mode;
	__u32 pad;
};

/**
 * kfd_ioctl_dbg_trap_suspend_queues_ags
 *
 *     Arguments for KFD_IOC_DBG_TRAP_SUSPEND_QUEUES
 *     Suspend queues.
 *
 *     @exception_mask	(IN) - raised exceptions to clear
 *     @queue_array_ptr (IN) - pointer to array of queue ids (u32 per queue id)
 *			       to suspend
 *     @num_queues	(IN) - number of queues to suspend in @queue_array_ptr
 *     @grace_period	(IN) - wave time allowance before preemption
 *			       per 1K GPU clock cycle unit
 *
 *     Generic errors apply (see kfd_dbg_trap_operations).
 *     Destruction of a suspended queue is blocked until the queue is
 *     resumed.  This allows the debugger to access queue information and
 *     the its context save area without running into a race condition on
 *     queue destruction.
 *     Automatically copies per queue context save area header information
 *     into the save area base
 *     (see kfd_queue_snapshot_entry and kfd_context_save_area_header).
 *
 *     Return - Number of queues suspended on SUCCESS.
 *	.	KFD_DBG_QUEUE_ERROR_MASK and KFD_DBG_QUEUE_INVALID_MASK masked
 *		for each queue id in @queue_array_ptr array reports unsuccessful
 *		suspend reason.
 *		KFD_DBG_QUEUE_ERROR_MASK = HW failure.
 *		KFD_DBG_QUEUE_INVALID_MASK = queue does not exist, is new or
 *		is being destroyed.
 */
struct kfd_ioctl_dbg_trap_suspend_queues_args {
	__u64 exception_mask;
	__u64 queue_array_ptr;
	__u32 num_queues;
	__u32 grace_period;
};

/**
 * kfd_ioctl_dbg_trap_resume_queues_args
 *
 *     Arguments for KFD_IOC_DBG_TRAP_RESUME_QUEUES
 *     Resume queues.
 *
 *     @queue_array_ptr (IN) - pointer to array of queue ids (u32 per queue id)
 *			       to resume
 *     @num_queues	(IN) - number of queues to resume in @queue_array_ptr
 *
 *     Generic errors apply (see kfd_dbg_trap_operations).
 *     Return - Number of queues resumed on SUCCESS.
 *		KFD_DBG_QUEUE_ERROR_MASK and KFD_DBG_QUEUE_INVALID_MASK mask
 *		for each queue id in @queue_array_ptr array reports unsuccessful
 *		resume reason.
 *		KFD_DBG_QUEUE_ERROR_MASK = HW failure.
 *		KFD_DBG_QUEUE_INVALID_MASK = queue does not exist.
 */
struct kfd_ioctl_dbg_trap_resume_queues_args {
	__u64 queue_array_ptr;
	__u32 num_queues;
	__u32 pad;
};

/**
 * kfd_ioctl_dbg_trap_set_node_address_watch_args
 *
 *     Arguments for KFD_IOC_DBG_TRAP_SET_NODE_ADDRESS_WATCH
 *     Sets address watch for device.
 *
 *     @address	(IN)  - watch address to set
 *     @mode    (IN)  - see kfd_dbg_trap_address_watch_mode
 *     @mask    (IN)  - watch address mask
 *     @gpu_id  (IN)  - target gpu to set watch point
 *     @id      (OUT) - watch id allocated
 *
 *     Generic errors apply (see kfd_dbg_trap_operations).
 *     Return - 0 on SUCCESS.
 *		Allocated watch ID returned to @id.
 *	      - ENODEV if gpu_id not found.
 *	      - ENOMEM if watch IDs can be allocated
 */
struct kfd_ioctl_dbg_trap_set_node_address_watch_args {
	__u64 address;
	__u32 mode;
	__u32 mask;
	__u32 gpu_id;
	__u32 id;
};

/**
 * kfd_ioctl_dbg_trap_clear_node_address_watch_args
 *
 *     Arguments for KFD_IOC_DBG_TRAP_CLEAR_NODE_ADDRESS_WATCH
 *     Clear address watch for device.
 *
 *     @gpu_id  (IN)  - target device to clear watch point
 *     @id      (IN) - allocated watch id to clear
 *
 *     Generic errors apply (see kfd_dbg_trap_operations).
 *     Return - 0 on SUCCESS.
 *	      - ENODEV if gpu_id not found.
 *	      - EINVAL if watch ID has not been allocated.
 */
struct kfd_ioctl_dbg_trap_clear_node_address_watch_args {
	__u32 gpu_id;
	__u32 id;
};

/**
 * kfd_ioctl_dbg_trap_set_flags_args
 *
 *     Arguments for KFD_IOC_DBG_TRAP_SET_FLAGS
 *     Sets flags for wave behaviour.
 *
 *     @flags (IN/OUT) - IN = flags to enable, OUT = flags previously enabled
 *
 *     Generic errors apply (see kfd_dbg_trap_operations).
 *     Return - 0 on SUCCESS.
 *	      - EACCESS if any debug device does not allow flag options.
 */
struct kfd_ioctl_dbg_trap_set_flags_args {
	__u32 flags;
	__u32 pad;
};

/**
 * kfd_ioctl_dbg_trap_query_debug_event_args
 *
 *     Arguments for KFD_IOC_DBG_TRAP_QUERY_DEBUG_EVENT
 *
 *     Find one or more raised exceptions. This function can return multiple
 *     exceptions from a single queue or a single device with one call. To find
 *     all raised exceptions, this function must be called repeatedly until it
 *     returns -EAGAIN. Returned exceptions can optionally be cleared by
 *     setting the corresponding bit in the @exception_mask input parameter.
 *     However, clearing an exception prevents retrieving further information
 *     about it with KFD_IOC_DBG_TRAP_QUERY_EXCEPTION_INFO.
 *
 *     @exception_mask (IN/OUT) - exception to clear (IN) and raised (OUT)
 *     @gpu_id	       (OUT)    - gpu id of exceptions raised
 *     @queue_id       (OUT)    - queue id of exceptions raised
 *
 *     Generic errors apply (see kfd_dbg_trap_operations).
 *     Return - 0 on raised exception found
 *              Raised exceptions found are returned in @exception mask
 *              with reported source id returned in @gpu_id or @queue_id.
 *            - EAGAIN if no raised exception has been found
 */
struct kfd_ioctl_dbg_trap_query_debug_event_args {
	__u64 exception_mask;
	__u32 gpu_id;
	__u32 queue_id;
};

/**
 * kfd_ioctl_dbg_trap_query_exception_info_args
 *
 *     Arguments KFD_IOC_DBG_TRAP_QUERY_EXCEPTION_INFO
 *     Get additional info on raised exception.
 *
 *     @info_ptr	(IN)	 - pointer to exception info buffer to copy to
 *     @info_size	(IN/OUT) - exception info buffer size (bytes)
 *     @source_id	(IN)     - target gpu or queue id
 *     @exception_code	(IN)     - target exception
 *     @clear_exception	(IN)     - clear raised @exception_code exception
 *				   (0 = false, 1 = true)
 *
 *     Generic errors apply (see kfd_dbg_trap_operations).
 *     Return - 0 on SUCCESS.
 *              If @exception_code is EC_DEVICE_MEMORY_VIOLATION, copy @info_size(OUT)
 *		bytes of memory exception data to @info_ptr.
 *              If @exception_code is EC_PROCESS_RUNTIME, copy saved
 *              kfd_runtime_info to @info_ptr.
 *              Actual required @info_ptr size (bytes) is returned in @info_size.
 */
struct kfd_ioctl_dbg_trap_query_exception_info_args {
	__u64 info_ptr;
	__u32 info_size;
	__u32 source_id;
	__u32 exception_code;
	__u32 clear_exception;
};

/**
 * kfd_ioctl_dbg_trap_get_queue_snapshot_args
 *
 *     Arguments KFD_IOC_DBG_TRAP_GET_QUEUE_SNAPSHOT
 *     Get queue information.
 *
 *     @exception_mask	 (IN)	  - exceptions raised to clear
 *     @snapshot_buf_ptr (IN)	  - queue snapshot entry buffer (see kfd_queue_snapshot_entry)
 *     @num_queues	 (IN/OUT) - number of queue snapshot entries
 *         The debugger specifies the size of the array allocated in @num_queues.
 *         KFD returns the number of queues that actually existed. If this is
 *         larger than the size specified by the debugger, KFD will not overflow
 *         the array allocated by the debugger.
 *
 *     @entry_size	 (IN/OUT) - size per entry in bytes
 *         The debugger specifies sizeof(struct kfd_queue_snapshot_entry) in
 *         @entry_size. KFD returns the number of bytes actually populated per
 *         entry. The debugger should use the KFD_IOCTL_MINOR_VERSION to determine,
 *         which fields in struct kfd_queue_snapshot_entry are valid. This allows
 *         growing the ABI in a backwards compatible manner.
 *         Note that entry_size(IN) should still be used to stride the snapshot buffer in the
 *         event that it's larger than actual kfd_queue_snapshot_entry.
 *
 *     Generic errors apply (see kfd_dbg_trap_operations).
 *     Return - 0 on SUCCESS.
 *              Copies @num_queues(IN) queue snapshot entries of size @entry_size(IN)
 *              into @snapshot_buf_ptr if @num_queues(IN) > 0.
 *              Otherwise return @num_queues(OUT) queue snapshot entries that exist.
 */
struct kfd_ioctl_dbg_trap_queue_snapshot_args {
	__u64 exception_mask;
	__u64 snapshot_buf_ptr;
	__u32 num_queues;
	__u32 entry_size;
};

/**
 * kfd_ioctl_dbg_trap_get_device_snapshot_args
 *
 *     Arguments for KFD_IOC_DBG_TRAP_GET_DEVICE_SNAPSHOT
 *     Get device information.
 *
 *     @exception_mask	 (IN)	  - exceptions raised to clear
 *     @snapshot_buf_ptr (IN)	  - pointer to snapshot buffer (see kfd_dbg_device_info_entry)
 *     @num_devices	 (IN/OUT) - number of debug devices to snapshot
 *         The debugger specifies the size of the array allocated in @num_devices.
 *         KFD returns the number of devices that actually existed. If this is
 *         larger than the size specified by the debugger, KFD will not overflow
 *         the array allocated by the debugger.
 *
 *     @entry_size	 (IN/OUT) - size per entry in bytes
 *         The debugger specifies sizeof(struct kfd_dbg_device_info_entry) in
 *         @entry_size. KFD returns the number of bytes actually populated. The
 *         debugger should use KFD_IOCTL_MINOR_VERSION to determine, which fields
 *         in struct kfd_dbg_device_info_entry are valid. This allows growing the
 *         ABI in a backwards compatible manner.
 *         Note that entry_size(IN) should still be used to stride the snapshot buffer in the
 *         event that it's larger than actual kfd_dbg_device_info_entry.
 *
 *     Generic errors apply (see kfd_dbg_trap_operations).
 *     Return - 0 on SUCCESS.
 *              Copies @num_devices(IN) device snapshot entries of size @entry_size(IN)
 *              into @snapshot_buf_ptr if @num_devices(IN) > 0.
 *              Otherwise return @num_devices(OUT) queue snapshot entries that exist.
 */
struct kfd_ioctl_dbg_trap_device_snapshot_args {
	__u64 exception_mask;
	__u64 snapshot_buf_ptr;
	__u32 num_devices;
	__u32 entry_size;
};

/**
 * kfd_ioctl_dbg_trap_args
 *
 * Arguments to debug target process.
 *
 *     @pid - target process to debug
 *     @op  - debug operation (see kfd_dbg_trap_operations)
 *
 *     @op determines which union struct args to use.
 *     Refer to kern docs for each kfd_ioctl_dbg_trap_*_args struct.
 */
struct kfd_ioctl_dbg_trap_args {
	__u32 pid;
	__u32 op;

	union {
		struct kfd_ioctl_dbg_trap_enable_args enable;
		struct kfd_ioctl_dbg_trap_send_runtime_event_args send_runtime_event;
		struct kfd_ioctl_dbg_trap_set_exceptions_enabled_args set_exceptions_enabled;
		struct kfd_ioctl_dbg_trap_set_wave_launch_override_args launch_override;
		struct kfd_ioctl_dbg_trap_set_wave_launch_mode_args launch_mode;
		struct kfd_ioctl_dbg_trap_suspend_queues_args suspend_queues;
		struct kfd_ioctl_dbg_trap_resume_queues_args resume_queues;
		struct kfd_ioctl_dbg_trap_set_node_address_watch_args set_node_address_watch;
		struct kfd_ioctl_dbg_trap_clear_node_address_watch_args clear_node_address_watch;
		struct kfd_ioctl_dbg_trap_set_flags_args set_flags;
		struct kfd_ioctl_dbg_trap_query_debug_event_args query_debug_event;
		struct kfd_ioctl_dbg_trap_query_exception_info_args query_exception_info;
		struct kfd_ioctl_dbg_trap_queue_snapshot_args queue_snapshot;
		struct kfd_ioctl_dbg_trap_device_snapshot_args device_snapshot;
	};
};

#define AMDKFD_IOCTL_BASE 'K'
#define AMDKFD_IO(nr)			_IO(AMDKFD_IOCTL_BASE, nr)
#define AMDKFD_IOR(nr, type)		_IOR(AMDKFD_IOCTL_BASE, nr, type)
#define AMDKFD_IOW(nr, type)		_IOW(AMDKFD_IOCTL_BASE, nr, type)
#define AMDKFD_IOWR(nr, type)		_IOWR(AMDKFD_IOCTL_BASE, nr, type)

#define AMDKFD_IOC_GET_VERSION			\
		AMDKFD_IOR(0x01, struct kfd_ioctl_get_version_args)

#define AMDKFD_IOC_CREATE_QUEUE			\
		AMDKFD_IOWR(0x02, struct kfd_ioctl_create_queue_args)

#define AMDKFD_IOC_DESTROY_QUEUE		\
		AMDKFD_IOWR(0x03, struct kfd_ioctl_destroy_queue_args)

#define AMDKFD_IOC_SET_MEMORY_POLICY		\
		AMDKFD_IOW(0x04, struct kfd_ioctl_set_memory_policy_args)

#define AMDKFD_IOC_GET_CLOCK_COUNTERS		\
		AMDKFD_IOWR(0x05, struct kfd_ioctl_get_clock_counters_args)

#define AMDKFD_IOC_GET_PROCESS_APERTURES	\
		AMDKFD_IOR(0x06, struct kfd_ioctl_get_process_apertures_args)

#define AMDKFD_IOC_UPDATE_QUEUE			\
		AMDKFD_IOW(0x07, struct kfd_ioctl_update_queue_args)

#define AMDKFD_IOC_CREATE_EVENT			\
		AMDKFD_IOWR(0x08, struct kfd_ioctl_create_event_args)

#define AMDKFD_IOC_DESTROY_EVENT		\
		AMDKFD_IOW(0x09, struct kfd_ioctl_destroy_event_args)

#define AMDKFD_IOC_SET_EVENT			\
		AMDKFD_IOW(0x0A, struct kfd_ioctl_set_event_args)

#define AMDKFD_IOC_RESET_EVENT			\
		AMDKFD_IOW(0x0B, struct kfd_ioctl_reset_event_args)

#define AMDKFD_IOC_WAIT_EVENTS			\
		AMDKFD_IOWR(0x0C, struct kfd_ioctl_wait_events_args)

#define AMDKFD_IOC_DBG_REGISTER_DEPRECATED	\
		AMDKFD_IOW(0x0D, struct kfd_ioctl_dbg_register_args)

#define AMDKFD_IOC_DBG_UNREGISTER_DEPRECATED	\
		AMDKFD_IOW(0x0E, struct kfd_ioctl_dbg_unregister_args)

#define AMDKFD_IOC_DBG_ADDRESS_WATCH_DEPRECATED	\
		AMDKFD_IOW(0x0F, struct kfd_ioctl_dbg_address_watch_args)

#define AMDKFD_IOC_DBG_WAVE_CONTROL_DEPRECATED	\
		AMDKFD_IOW(0x10, struct kfd_ioctl_dbg_wave_control_args)

#define AMDKFD_IOC_SET_SCRATCH_BACKING_VA	\
		AMDKFD_IOWR(0x11, struct kfd_ioctl_set_scratch_backing_va_args)

#define AMDKFD_IOC_GET_TILE_CONFIG                                      \
		AMDKFD_IOWR(0x12, struct kfd_ioctl_get_tile_config_args)

#define AMDKFD_IOC_SET_TRAP_HANDLER		\
		AMDKFD_IOW(0x13, struct kfd_ioctl_set_trap_handler_args)

#define AMDKFD_IOC_GET_PROCESS_APERTURES_NEW	\
		AMDKFD_IOWR(0x14,		\
			struct kfd_ioctl_get_process_apertures_new_args)

#define AMDKFD_IOC_ACQUIRE_VM			\
		AMDKFD_IOW(0x15, struct kfd_ioctl_acquire_vm_args)

#define AMDKFD_IOC_ALLOC_MEMORY_OF_GPU		\
		AMDKFD_IOWR(0x16, struct kfd_ioctl_alloc_memory_of_gpu_args)

#define AMDKFD_IOC_FREE_MEMORY_OF_GPU		\
		AMDKFD_IOW(0x17, struct kfd_ioctl_free_memory_of_gpu_args)

#define AMDKFD_IOC_MAP_MEMORY_TO_GPU		\
		AMDKFD_IOWR(0x18, struct kfd_ioctl_map_memory_to_gpu_args)

#define AMDKFD_IOC_UNMAP_MEMORY_FROM_GPU	\
		AMDKFD_IOWR(0x19, struct kfd_ioctl_unmap_memory_from_gpu_args)

#define AMDKFD_IOC_SET_CU_MASK		\
		AMDKFD_IOW(0x1A, struct kfd_ioctl_set_cu_mask_args)

#define AMDKFD_IOC_GET_QUEUE_WAVE_STATE		\
		AMDKFD_IOWR(0x1B, struct kfd_ioctl_get_queue_wave_state_args)

#define AMDKFD_IOC_GET_DMABUF_INFO		\
		AMDKFD_IOWR(0x1C, struct kfd_ioctl_get_dmabuf_info_args)

#define AMDKFD_IOC_IMPORT_DMABUF		\
		AMDKFD_IOWR(0x1D, struct kfd_ioctl_import_dmabuf_args)

#define AMDKFD_IOC_ALLOC_QUEUE_GWS		\
		AMDKFD_IOWR(0x1E, struct kfd_ioctl_alloc_queue_gws_args)

#define AMDKFD_IOC_SMI_EVENTS			\
		AMDKFD_IOWR(0x1F, struct kfd_ioctl_smi_events_args)

#define AMDKFD_IOC_SVM	AMDKFD_IOWR(0x20, struct kfd_ioctl_svm_args)

#define AMDKFD_IOC_SET_XNACK_MODE		\
		AMDKFD_IOWR(0x21, struct kfd_ioctl_set_xnack_mode_args)

#define AMDKFD_IOC_CRIU_OP			\
		AMDKFD_IOWR(0x22, struct kfd_ioctl_criu_args)

#define AMDKFD_IOC_AVAILABLE_MEMORY		\
		AMDKFD_IOWR(0x23, struct kfd_ioctl_get_available_memory_args)

#define AMDKFD_IOC_EXPORT_DMABUF		\
		AMDKFD_IOWR(0x24, struct kfd_ioctl_export_dmabuf_args)

#define AMDKFD_IOC_RUNTIME_ENABLE		\
		AMDKFD_IOWR(0x25, struct kfd_ioctl_runtime_enable_args)

#define AMDKFD_IOC_DBG_TRAP			\
		AMDKFD_IOWR(0x26, struct kfd_ioctl_dbg_trap_args)

#define AMDKFD_COMMAND_START		0x01
#define AMDKFD_COMMAND_END		0x27

#endif
