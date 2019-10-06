/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
 *
 * Copyright 2016-2019 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef HABANALABS_H_
#define HABANALABS_H_

#include <linux/types.h>
#include <linux/ioctl.h>

/*
 * Defines that are asic-specific but constitutes as ABI between kernel driver
 * and userspace
 */
#define GOYA_KMD_SRAM_RESERVED_SIZE_FROM_START	0x8000	/* 32KB */

/*
 * Queue Numbering
 *
 * The external queues (PCI DMA channels) MUST be before the internal queues
 * and each group (PCI DMA channels and internal) must be contiguous inside
 * itself but there can be a gap between the two groups (although not
 * recommended)
 */

enum goya_queue_id {
	GOYA_QUEUE_ID_DMA_0 = 0,
	GOYA_QUEUE_ID_DMA_1 = 1,
	GOYA_QUEUE_ID_DMA_2 = 2,
	GOYA_QUEUE_ID_DMA_3 = 3,
	GOYA_QUEUE_ID_DMA_4 = 4,
	GOYA_QUEUE_ID_CPU_PQ = 5,
	GOYA_QUEUE_ID_MME = 6,	/* Internal queues start here */
	GOYA_QUEUE_ID_TPC0 = 7,
	GOYA_QUEUE_ID_TPC1 = 8,
	GOYA_QUEUE_ID_TPC2 = 9,
	GOYA_QUEUE_ID_TPC3 = 10,
	GOYA_QUEUE_ID_TPC4 = 11,
	GOYA_QUEUE_ID_TPC5 = 12,
	GOYA_QUEUE_ID_TPC6 = 13,
	GOYA_QUEUE_ID_TPC7 = 14,
	GOYA_QUEUE_ID_SIZE
};

/*
 * Engine Numbering
 *
 * Used in the "busy_engines_mask" field in `struct hl_info_hw_idle'
 */

enum goya_engine_id {
	GOYA_ENGINE_ID_DMA_0 = 0,
	GOYA_ENGINE_ID_DMA_1,
	GOYA_ENGINE_ID_DMA_2,
	GOYA_ENGINE_ID_DMA_3,
	GOYA_ENGINE_ID_DMA_4,
	GOYA_ENGINE_ID_MME_0,
	GOYA_ENGINE_ID_TPC_0,
	GOYA_ENGINE_ID_TPC_1,
	GOYA_ENGINE_ID_TPC_2,
	GOYA_ENGINE_ID_TPC_3,
	GOYA_ENGINE_ID_TPC_4,
	GOYA_ENGINE_ID_TPC_5,
	GOYA_ENGINE_ID_TPC_6,
	GOYA_ENGINE_ID_TPC_7,
	GOYA_ENGINE_ID_SIZE
};

enum hl_device_status {
	HL_DEVICE_STATUS_OPERATIONAL,
	HL_DEVICE_STATUS_IN_RESET,
	HL_DEVICE_STATUS_MALFUNCTION
};

/* Opcode for management ioctl
 *
 * HW_IP_INFO            - Receive information about different IP blocks in the
 *                         device.
 * HL_INFO_HW_EVENTS     - Receive an array describing how many times each event
 *                         occurred since the last hard reset.
 * HL_INFO_DRAM_USAGE    - Retrieve the dram usage inside the device and of the
 *                         specific context. This is relevant only for devices
 *                         where the dram is managed by the kernel driver
 * HL_INFO_HW_IDLE       - Retrieve information about the idle status of each
 *                         internal engine.
 * HL_INFO_DEVICE_STATUS - Retrieve the device's status. This opcode doesn't
 *                         require an open context.
 * HL_INFO_DEVICE_UTILIZATION - Retrieve the total utilization of the device
 *                              over the last period specified by the user.
 *                              The period can be between 100ms to 1s, in
 *                              resolution of 100ms. The return value is a
 *                              percentage of the utilization rate.
 * HL_INFO_HW_EVENTS_AGGREGATE - Receive an array describing how many times each
 *                               event occurred since the driver was loaded.
 */
#define HL_INFO_HW_IP_INFO		0
#define HL_INFO_HW_EVENTS		1
#define HL_INFO_DRAM_USAGE		2
#define HL_INFO_HW_IDLE			3
#define HL_INFO_DEVICE_STATUS		4
#define HL_INFO_DEVICE_UTILIZATION	6
#define HL_INFO_HW_EVENTS_AGGREGATE	7

#define HL_INFO_VERSION_MAX_LEN	128

struct hl_info_hw_ip_info {
	__u64 sram_base_address;
	__u64 dram_base_address;
	__u64 dram_size;
	__u32 sram_size;
	__u32 num_of_events;
	__u32 device_id; /* PCI Device ID */
	__u32 reserved[3];
	__u32 armcp_cpld_version;
	__u32 psoc_pci_pll_nr;
	__u32 psoc_pci_pll_nf;
	__u32 psoc_pci_pll_od;
	__u32 psoc_pci_pll_div_factor;
	__u8 tpc_enabled_mask;
	__u8 dram_enabled;
	__u8 pad[2];
	__u8 armcp_version[HL_INFO_VERSION_MAX_LEN];
};

struct hl_info_dram_usage {
	__u64 dram_free_mem;
	__u64 ctx_dram_mem;
};

struct hl_info_hw_idle {
	__u32 is_idle;
	/*
	 * Bitmask of busy engines.
	 * Bits definition is according to `enum <chip>_enging_id'.
	 */
	__u32 busy_engines_mask;
};

struct hl_info_device_status {
	__u32 status;
	__u32 pad;
};

struct hl_info_device_utilization {
	__u32 utilization;
	__u32 pad;
};

struct hl_info_args {
	/* Location of relevant struct in userspace */
	__u64 return_pointer;
	/*
	 * The size of the return value. Just like "size" in "snprintf",
	 * it limits how many bytes the kernel can write
	 *
	 * For hw_events array, the size should be
	 * hl_info_hw_ip_info.num_of_events * sizeof(__u32)
	 */
	__u32 return_size;

	/* HL_INFO_* */
	__u32 op;

	union {
		/* Context ID - Currently not in use */
		__u32 ctx_id;
		/* Period value for utilization rate (100ms - 1000ms, in 100ms
		 * resolution.
		 */
		__u32 period_ms;
	};

	__u32 pad;
};

/* Opcode to create a new command buffer */
#define HL_CB_OP_CREATE		0
/* Opcode to destroy previously created command buffer */
#define HL_CB_OP_DESTROY	1

struct hl_cb_in {
	/* Handle of CB or 0 if we want to create one */
	__u64 cb_handle;
	/* HL_CB_OP_* */
	__u32 op;
	/* Size of CB. Maximum size is 2MB. The minimum size that will be
	 * allocated, regardless of this parameter's value, is PAGE_SIZE
	 */
	__u32 cb_size;
	/* Context ID - Currently not in use */
	__u32 ctx_id;
	__u32 pad;
};

struct hl_cb_out {
	/* Handle of CB */
	__u64 cb_handle;
};

union hl_cb_args {
	struct hl_cb_in in;
	struct hl_cb_out out;
};

/*
 * This structure size must always be fixed to 64-bytes for backward
 * compatibility
 */
struct hl_cs_chunk {
	/*
	 * For external queue, this represents a Handle of CB on the Host
	 * For internal queue, this represents an SRAM or DRAM address of the
	 * internal CB
	 */
	__u64 cb_handle;
	/* Index of queue to put the CB on */
	__u32 queue_index;
	/*
	 * Size of command buffer with valid packets
	 * Can be smaller then actual CB size
	 */
	__u32 cb_size;
	/* HL_CS_CHUNK_FLAGS_* */
	__u32 cs_chunk_flags;
	/* Align structure to 64 bytes */
	__u32 pad[11];
};

#define HL_CS_FLAGS_FORCE_RESTORE	0x1

#define HL_CS_STATUS_SUCCESS		0

struct hl_cs_in {
	/* this holds address of array of hl_cs_chunk for restore phase */
	__u64 chunks_restore;
	/* this holds address of array of hl_cs_chunk for execution phase */
	__u64 chunks_execute;
	/* this holds address of array of hl_cs_chunk for store phase -
	 * Currently not in use
	 */
	__u64 chunks_store;
	/* Number of chunks in restore phase array */
	__u32 num_chunks_restore;
	/* Number of chunks in execution array */
	__u32 num_chunks_execute;
	/* Number of chunks in restore phase array - Currently not in use */
	__u32 num_chunks_store;
	/* HL_CS_FLAGS_* */
	__u32 cs_flags;
	/* Context ID - Currently not in use */
	__u32 ctx_id;
};

struct hl_cs_out {
	/*
	 * seq holds the sequence number of the CS to pass to wait ioctl. All
	 * values are valid except for 0 and ULLONG_MAX
	 */
	__u64 seq;
	/* HL_CS_STATUS_* */
	__u32 status;
	__u32 pad;
};

union hl_cs_args {
	struct hl_cs_in in;
	struct hl_cs_out out;
};

struct hl_wait_cs_in {
	/* Command submission sequence number */
	__u64 seq;
	/* Absolute timeout to wait in microseconds */
	__u64 timeout_us;
	/* Context ID - Currently not in use */
	__u32 ctx_id;
	__u32 pad;
};

#define HL_WAIT_CS_STATUS_COMPLETED	0
#define HL_WAIT_CS_STATUS_BUSY		1
#define HL_WAIT_CS_STATUS_TIMEDOUT	2
#define HL_WAIT_CS_STATUS_ABORTED	3
#define HL_WAIT_CS_STATUS_INTERRUPTED	4

struct hl_wait_cs_out {
	/* HL_WAIT_CS_STATUS_* */
	__u32 status;
	__u32 pad;
};

union hl_wait_cs_args {
	struct hl_wait_cs_in in;
	struct hl_wait_cs_out out;
};

/* Opcode to alloc device memory */
#define HL_MEM_OP_ALLOC			0
/* Opcode to free previously allocated device memory */
#define HL_MEM_OP_FREE			1
/* Opcode to map host memory */
#define HL_MEM_OP_MAP			2
/* Opcode to unmap previously mapped host memory */
#define HL_MEM_OP_UNMAP			3

/* Memory flags */
#define HL_MEM_CONTIGUOUS	0x1
#define HL_MEM_SHARED		0x2
#define HL_MEM_USERPTR		0x4

struct hl_mem_in {
	union {
		/* HL_MEM_OP_ALLOC- allocate device memory */
		struct {
			/* Size to alloc */
			__u64 mem_size;
		} alloc;

		/* HL_MEM_OP_FREE - free device memory */
		struct {
			/* Handle returned from HL_MEM_OP_ALLOC */
			__u64 handle;
		} free;

		/* HL_MEM_OP_MAP - map device memory */
		struct {
			/*
			 * Requested virtual address of mapped memory.
			 * The driver will try to map the requested region to
			 * this hint address, as long as the address is valid
			 * and not already mapped. The user should check the
			 * returned address of the IOCTL to make sure he got
			 * the hint address. Passing 0 here means that the
			 * driver will choose the address itself.
			 */
			__u64 hint_addr;
			/* Handle returned from HL_MEM_OP_ALLOC */
			__u64 handle;
		} map_device;

		/* HL_MEM_OP_MAP - map host memory */
		struct {
			/* Address of allocated host memory */
			__u64 host_virt_addr;
			/*
			 * Requested virtual address of mapped memory.
			 * The driver will try to map the requested region to
			 * this hint address, as long as the address is valid
			 * and not already mapped. The user should check the
			 * returned address of the IOCTL to make sure he got
			 * the hint address. Passing 0 here means that the
			 * driver will choose the address itself.
			 */
			__u64 hint_addr;
			/* Size of allocated host memory */
			__u64 mem_size;
		} map_host;

		/* HL_MEM_OP_UNMAP - unmap host memory */
		struct {
			/* Virtual address returned from HL_MEM_OP_MAP */
			__u64 device_virt_addr;
		} unmap;
	};

	/* HL_MEM_OP_* */
	__u32 op;
	/* HL_MEM_* flags */
	__u32 flags;
	/* Context ID - Currently not in use */
	__u32 ctx_id;
	__u32 pad;
};

struct hl_mem_out {
	union {
		/*
		 * Used for HL_MEM_OP_MAP as the virtual address that was
		 * assigned in the device VA space.
		 * A value of 0 means the requested operation failed.
		 */
		__u64 device_virt_addr;

		/*
		 * Used for HL_MEM_OP_ALLOC. This is the assigned
		 * handle for the allocated memory
		 */
		__u64 handle;
	};
};

union hl_mem_args {
	struct hl_mem_in in;
	struct hl_mem_out out;
};

#define HL_DEBUG_MAX_AUX_VALUES		10

struct hl_debug_params_etr {
	/* Address in memory to allocate buffer */
	__u64 buffer_address;

	/* Size of buffer to allocate */
	__u64 buffer_size;

	/* Sink operation mode: SW fifo, HW fifo, Circular buffer */
	__u32 sink_mode;
	__u32 pad;
};

struct hl_debug_params_etf {
	/* Address in memory to allocate buffer */
	__u64 buffer_address;

	/* Size of buffer to allocate */
	__u64 buffer_size;

	/* Sink operation mode: SW fifo, HW fifo, Circular buffer */
	__u32 sink_mode;
	__u32 pad;
};

struct hl_debug_params_stm {
	/* Two bit masks for HW event and Stimulus Port */
	__u64 he_mask;
	__u64 sp_mask;

	/* Trace source ID */
	__u32 id;

	/* Frequency for the timestamp register */
	__u32 frequency;
};

struct hl_debug_params_bmon {
	/* Two address ranges that the user can request to filter */
	__u64 start_addr0;
	__u64 addr_mask0;

	__u64 start_addr1;
	__u64 addr_mask1;

	/* Capture window configuration */
	__u32 bw_win;
	__u32 win_capture;

	/* Trace source ID */
	__u32 id;
	__u32 pad;
};

struct hl_debug_params_spmu {
	/* Event types selection */
	__u64 event_types[HL_DEBUG_MAX_AUX_VALUES];

	/* Number of event types selection */
	__u32 event_types_num;
	__u32 pad;
};

/* Opcode for ETR component */
#define HL_DEBUG_OP_ETR		0
/* Opcode for ETF component */
#define HL_DEBUG_OP_ETF		1
/* Opcode for STM component */
#define HL_DEBUG_OP_STM		2
/* Opcode for FUNNEL component */
#define HL_DEBUG_OP_FUNNEL	3
/* Opcode for BMON component */
#define HL_DEBUG_OP_BMON	4
/* Opcode for SPMU component */
#define HL_DEBUG_OP_SPMU	5
/* Opcode for timestamp (deprecated) */
#define HL_DEBUG_OP_TIMESTAMP	6
/* Opcode for setting the device into or out of debug mode. The enable
 * variable should be 1 for enabling debug mode and 0 for disabling it
 */
#define HL_DEBUG_OP_SET_MODE	7

struct hl_debug_args {
	/*
	 * Pointer to user input structure.
	 * This field is relevant to specific opcodes.
	 */
	__u64 input_ptr;
	/* Pointer to user output structure */
	__u64 output_ptr;
	/* Size of user input structure */
	__u32 input_size;
	/* Size of user output structure */
	__u32 output_size;
	/* HL_DEBUG_OP_* */
	__u32 op;
	/*
	 * Register index in the component, taken from the debug_regs_index enum
	 * in the various ASIC header files
	 */
	__u32 reg_idx;
	/* Enable/disable */
	__u32 enable;
	/* Context ID - Currently not in use */
	__u32 ctx_id;
};

/*
 * Various information operations such as:
 * - H/W IP information
 * - Current dram usage
 *
 * The user calls this IOCTL with an opcode that describes the required
 * information. The user should supply a pointer to a user-allocated memory
 * chunk, which will be filled by the driver with the requested information.
 *
 * The user supplies the maximum amount of size to copy into the user's memory,
 * in order to prevent data corruption in case of differences between the
 * definitions of structures in kernel and userspace, e.g. in case of old
 * userspace and new kernel driver
 */
#define HL_IOCTL_INFO	\
		_IOWR('H', 0x01, struct hl_info_args)

/*
 * Command Buffer
 * - Request a Command Buffer
 * - Destroy a Command Buffer
 *
 * The command buffers are memory blocks that reside in DMA-able address
 * space and are physically contiguous so they can be accessed by the device
 * directly. They are allocated using the coherent DMA API.
 *
 * When creating a new CB, the IOCTL returns a handle of it, and the user-space
 * process needs to use that handle to mmap the buffer so it can access them.
 *
 */
#define HL_IOCTL_CB		\
		_IOWR('H', 0x02, union hl_cb_args)

/*
 * Command Submission
 *
 * To submit work to the device, the user need to call this IOCTL with a set
 * of JOBS. That set of JOBS constitutes a CS object.
 * Each JOB will be enqueued on a specific queue, according to the user's input.
 * There can be more then one JOB per queue.
 *
 * The CS IOCTL will receive three sets of JOBS. One set is for "restore" phase,
 * a second set is for "execution" phase and a third set is for "store" phase.
 * The JOBS on the "restore" phase are enqueued only after context-switch
 * (or if its the first CS for this context). The user can also order the
 * driver to run the "restore" phase explicitly
 *
 * There are two types of queues - external and internal. External queues
 * are DMA queues which transfer data from/to the Host. All other queues are
 * internal. The driver will get completion notifications from the device only
 * on JOBS which are enqueued in the external queues.
 *
 * For jobs on external queues, the user needs to create command buffers
 * through the CB ioctl and give the CB's handle to the CS ioctl. For jobs on
 * internal queues, the user needs to prepare a "command buffer" with packets
 * on either the SRAM or DRAM, and give the device address of that buffer to
 * the CS ioctl.
 *
 * This IOCTL is asynchronous in regard to the actual execution of the CS. This
 * means it returns immediately after ALL the JOBS were enqueued on their
 * relevant queues. Therefore, the user mustn't assume the CS has been completed
 * or has even started to execute.
 *
 * Upon successful enqueue, the IOCTL returns a sequence number which the user
 * can use with the "Wait for CS" IOCTL to check whether the handle's CS
 * external JOBS have been completed. Note that if the CS has internal JOBS
 * which can execute AFTER the external JOBS have finished, the driver might
 * report that the CS has finished executing BEFORE the internal JOBS have
 * actually finish executing.
 *
 * Even though the sequence number increments per CS, the user can NOT
 * automatically assume that if CS with sequence number N finished, then CS
 * with sequence number N-1 also finished. The user can make this assumption if
 * and only if CS N and CS N-1 are exactly the same (same CBs for the same
 * queues).
 */
#define HL_IOCTL_CS			\
		_IOWR('H', 0x03, union hl_cs_args)

/*
 * Wait for Command Submission
 *
 * The user can call this IOCTL with a handle it received from the CS IOCTL
 * to wait until the handle's CS has finished executing. The user will wait
 * inside the kernel until the CS has finished or until the user-requeusted
 * timeout has expired.
 *
 * The return value of the IOCTL is a standard Linux error code. The possible
 * values are:
 *
 * EINTR     - Kernel waiting has been interrupted, e.g. due to OS signal
 *             that the user process received
 * ETIMEDOUT - The CS has caused a timeout on the device
 * EIO       - The CS was aborted (usually because the device was reset)
 * ENODEV    - The device wants to do hard-reset (so user need to close FD)
 *
 * The driver also returns a custom define inside the IOCTL which can be:
 *
 * HL_WAIT_CS_STATUS_COMPLETED   - The CS has been completed successfully (0)
 * HL_WAIT_CS_STATUS_BUSY        - The CS is still executing (0)
 * HL_WAIT_CS_STATUS_TIMEDOUT    - The CS has caused a timeout on the device
 *                                 (ETIMEDOUT)
 * HL_WAIT_CS_STATUS_ABORTED     - The CS was aborted, usually because the
 *                                 device was reset (EIO)
 * HL_WAIT_CS_STATUS_INTERRUPTED - Waiting for the CS was interrupted (EINTR)
 *
 */

#define HL_IOCTL_WAIT_CS			\
		_IOWR('H', 0x04, union hl_wait_cs_args)

/*
 * Memory
 * - Map host memory to device MMU
 * - Unmap host memory from device MMU
 *
 * This IOCTL allows the user to map host memory to the device MMU
 *
 * For host memory, the IOCTL doesn't allocate memory. The user is supposed
 * to allocate the memory in user-space (malloc/new). The driver pins the
 * physical pages (up to the allowed limit by the OS), assigns a virtual
 * address in the device VA space and initializes the device MMU.
 *
 * There is an option for the user to specify the requested virtual address.
 *
 */
#define HL_IOCTL_MEMORY		\
		_IOWR('H', 0x05, union hl_mem_args)

/*
 * Debug
 * - Enable/disable the ETR/ETF/FUNNEL/STM/BMON/SPMU debug traces
 *
 * This IOCTL allows the user to get debug traces from the chip.
 *
 * Before the user can send configuration requests of the various
 * debug/profile engines, it needs to set the device into debug mode.
 * This is because the debug/profile infrastructure is shared component in the
 * device and we can't allow multiple users to access it at the same time.
 *
 * Once a user set the device into debug mode, the driver won't allow other
 * users to "work" with the device, i.e. open a FD. If there are multiple users
 * opened on the device, the driver won't allow any user to debug the device.
 *
 * For each configuration request, the user needs to provide the register index
 * and essential data such as buffer address and size.
 *
 * Once the user has finished using the debug/profile engines, he should
 * set the device into non-debug mode, i.e. disable debug mode.
 *
 * The driver can decide to "kick out" the user if he abuses this interface.
 *
 */
#define HL_IOCTL_DEBUG		\
		_IOWR('H', 0x06, struct hl_debug_args)

#define HL_COMMAND_START	0x01
#define HL_COMMAND_END		0x07

#endif /* HABANALABS_H_ */
