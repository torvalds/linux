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

#define KFD_IOCTL_MAJOR_VERSION 1
#define KFD_IOCTL_MINOR_VERSION 1

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

/* memory exception data*/
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

/* Event data */
struct kfd_event_data {
	union {
		struct kfd_hsa_memory_exception_data memory_exception_data;
		struct kfd_hsa_hw_exception_data hw_exception_data;
	};				/* From KFD */
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

/* Register offset inside the remapped mmio page
 */
enum kfd_mmio_remap {
	KFD_MMIO_REMAP_HDP_MEM_FLUSH_CNTL = 0,
	KFD_MMIO_REMAP_HDP_REG_FLUSH_CNTL = 4,
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

#define AMDKFD_IOC_DBG_REGISTER			\
		AMDKFD_IOW(0x0D, struct kfd_ioctl_dbg_register_args)

#define AMDKFD_IOC_DBG_UNREGISTER		\
		AMDKFD_IOW(0x0E, struct kfd_ioctl_dbg_unregister_args)

#define AMDKFD_IOC_DBG_ADDRESS_WATCH		\
		AMDKFD_IOW(0x0F, struct kfd_ioctl_dbg_address_watch_args)

#define AMDKFD_IOC_DBG_WAVE_CONTROL		\
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

#define AMDKFD_COMMAND_START		0x01
#define AMDKFD_COMMAND_END		0x1F

#endif
