/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2016-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */
/* Smart-Peripheral-Switch (SPS) API. */

#ifndef _SPS_H_
#define _SPS_H_

#include <linux/errno.h>
#include <linux/types.h>

#if defined(CONFIG_PHYS_ADDR_T_64BIT) || defined(CONFIG_ARM_LPAE)

/* Returns upper 4bits of 36bits physical address */
#define SPS_GET_UPPER_ADDR(addr) ((addr & 0xF00000000ULL) >> 32)

/* Returns 36bits physical address from 32bit address &
 * flags word
 */
#define DESC_FULL_ADDR(flags, addr) ((((phys_addr_t)flags & 0xF) << 32) | addr)

/* Returns flags word with flags and 4bit upper address
 * from flags and 36bit physical address
 */
#define DESC_FLAG_WORD(flags, addr) (((addr & 0xF00000000ULL) >> 32) | flags)

#else

#define SPS_GET_UPPER_ADDR(addr) (0)
#define DESC_FULL_ADDR(flags, addr) (addr)
#define DESC_FLAG_WORD(flags, addr) (flags)

#endif

/* Returns upper 4bits of 36bits physical address from
 * flags word
 */
#define DESC_UPPER_ADDR(flags) ((flags & 0xF))

/* Returns lower 32bits of 36bits physical address */
#define SPS_GET_LOWER_ADDR(addr) ((u32)(addr & 0xFFFFFFFF))

/* SPS device handle indicating use of system memory */
#define SPS_DEV_HANDLE_MEM       (~0x0ul>>1)

/* SPS device handle indicating use of BAM-DMA */

/* SPS device handle invalid value */
#define SPS_DEV_HANDLE_INVALID   0

/* BAM invalid IRQ value */
#define SPS_IRQ_INVALID          0

/* Invalid address value */
#define SPS_ADDR_INVALID      (0xDEADBEEF)

/* Invalid peripheral device enumeration class */
#define SPS_CLASS_INVALID     (0xDEADBEEF)

/*
 * This value specifies different configurations for an SPS connection.
 * A non-default value instructs the SPS driver to search for the configuration
 * in the fixed connection mapping table.
 */
#define SPS_CONFIG_DEFAULT       0

/*
 * This value instructs the SPS driver to use the default BAM-DMA channel
 * threshold
 */
#define SPS_DMA_THRESHOLD_DEFAULT   0

/* Flag bits supported by SPS hardware for struct sps_iovec */
#define SPS_IOVEC_FLAG_INT  0x8000  /* Generate interrupt */
#define SPS_IOVEC_FLAG_EOT  0x4000  /* Generate end-of-transfer indication */
#define SPS_IOVEC_FLAG_EOB  0x2000  /* Generate end-of-block indication */
#define SPS_IOVEC_FLAG_NWD  0x1000  /* notify when done */
#define SPS_IOVEC_FLAG_CMD  0x0800  /* command descriptor */
#define SPS_IOVEC_FLAG_LOCK  0x0400  /* pipe lock */
#define SPS_IOVEC_FLAG_UNLOCK  0x0200  /* pipe unlock */
#define SPS_IOVEC_FLAG_IMME 0x0100  /* immediate command descriptor */
#define SPS_IOVEC_FLAG_NO_SUBMIT 0x0020  /* Do not submit descriptor to HW */
#define SPS_IOVEC_FLAG_DEFAULT   0x0010  /* Use driver default */

/* Maximum descriptor/iovec size */
#define SPS_IOVEC_MAX_SIZE   (32 * 1024 - 1)  /* 32K-1 bytes due to HW limit */

/* BAM device options flags */

/*
 * BAM will be configured and enabled at boot.  Otherwise, BAM will be
 * configured and enabled when first pipe connect occurs.
 */
#define SPS_BAM_OPT_ENABLE_AT_BOOT  1UL
/* BAM IRQ is disabled */
#define SPS_BAM_OPT_IRQ_DISABLED    (1UL << 1)
/* BAM peripheral is a BAM-DMA */
#define SPS_BAM_OPT_BAMDMA          (1UL << 2)
/* BAM IRQ is registered for apps wakeup */
#define SPS_BAM_OPT_IRQ_WAKEUP      (1UL << 3)
/* Ignore external block pipe reset */
#define SPS_BAM_NO_EXT_P_RST        (1UL << 4)
/* Don't enable local clock gating */
#define SPS_BAM_NO_LOCAL_CLK_GATING (1UL << 5)
/* Don't enable writeback cancel*/
#define SPS_BAM_CANCEL_WB           (1UL << 6)
/* BAM uses SMMU */
#define SPS_BAM_SMMU_EN             (1UL << 9)
/* Confirm resource status before access BAM*/
#define SPS_BAM_RES_CONFIRM         (1UL << 7)
/* Hold memory for BAM DMUX */
#define SPS_BAM_HOLD_MEM            (1UL << 8)
/* Use cached write pointer */
#define SPS_BAM_CACHED_WP           (1UL << 10)

/* BAM device management flags */

/* BAM global device control is managed remotely */
#define SPS_BAM_MGR_DEVICE_REMOTE   1UL
/* BAM device supports multiple execution environments */
#define SPS_BAM_MGR_MULTI_EE        (1UL << 1)
/* BAM pipes are *not* allocated locally */
#define SPS_BAM_MGR_PIPE_NO_ALLOC   (1UL << 2)
/* BAM pipes are *not* configured locally */
#define SPS_BAM_MGR_PIPE_NO_CONFIG  (1UL << 3)
/* BAM pipes are *not* controlled locally */
#define SPS_BAM_MGR_PIPE_NO_CTRL    (1UL << 4)
/* "Globbed" management properties */
#define SPS_BAM_MGR_NONE            \
	(SPS_BAM_MGR_DEVICE_REMOTE | SPS_BAM_MGR_PIPE_NO_ALLOC | \
	 SPS_BAM_MGR_PIPE_NO_CONFIG | SPS_BAM_MGR_PIPE_NO_CTRL)
#define SPS_BAM_MGR_LOCAL           0
#define SPS_BAM_MGR_LOCAL_SHARED    SPS_BAM_MGR_MULTI_EE
#define SPS_BAM_MGR_REMOTE_SHARED   \
	(SPS_BAM_MGR_DEVICE_REMOTE | SPS_BAM_MGR_MULTI_EE | \
	 SPS_BAM_MGR_PIPE_NO_ALLOC)
#define SPS_BAM_MGR_ACCESS_MASK     SPS_BAM_MGR_NONE

/*
 * BAM security configuration
 */
#define SPS_BAM_NUM_EES             4
#define SPS_BAM_SEC_DO_NOT_CONFIG   0
#define SPS_BAM_SEC_DO_CONFIG       0x0A434553

/* BAM pipe selection */
#define SPS_BAM_PIPE(n)             (1UL << (n))

/* This enum specifies the operational mode for an SPS connection */
enum sps_mode {
	SPS_MODE_SRC = 0,  /* end point is the source (producer) */
	SPS_MODE_DEST,	   /* end point is the destination (consumer) */
};


/*
 * This enum is a set of bit flag options for SPS connection.
 * The enums should be OR'd together to create the option set
 * for the SPS connection.
 */
enum sps_option {
	/*
	 * Options to enable specific SPS hardware interrupts.
	 * These bit flags are also used to indicate interrupt source
	 * for the SPS_EVENT_IRQ event.
	 */
	SPS_O_DESC_DONE = 0x00000001,  /* Descriptor processed */
	SPS_O_INACTIVE  = 0x00000002,  /* Inactivity timeout */
	SPS_O_WAKEUP    = 0x00000004,  /* Peripheral wake up */
	SPS_O_OUT_OF_DESC = 0x00000008,/* Out of descriptors */
	SPS_O_ERROR     = 0x00000010,  /* Error */
	SPS_O_EOT       = 0x00000020,  /* End-of-transfer */
	SPS_O_RST_ERROR = 0x00000040,  /* Pipe reset unsucessful error */
	SPS_O_HRESP_ERROR = 0x00000080,/* Errorneous Hresponse by AHB MASTER */

	/* Options to enable hardware features */
	SPS_O_STREAMING = 0x00010000,  /* Enable streaming mode (no EOT) */
	/* Use MTI/SETPEND instead of BAM interrupt */
	SPS_O_IRQ_MTI   = 0x00020000,
	/* NWD bit written with EOT for BAM2BAM producer pipe */
	SPS_O_WRITE_NWD   = 0x00040000,
       /* EOT set after pipe SW offset advanced */
	SPS_O_LATE_EOT   = 0x00080000,

	/* Options to enable software features */
	/* Do not disable a pipe during disconnection */
	SPS_O_NO_DISABLE      = 0x00800000,
	/* Transfer operation should be polled */
	SPS_O_POLL      = 0x01000000,
	/* Disable queuing of transfer events for the connection end point */
	SPS_O_NO_Q      = 0x02000000,
	SPS_O_FLOWOFF   = 0x04000000,  /* Graceful halt */
	/* SPS_O_WAKEUP will be disabled after triggered */
	SPS_O_WAKEUP_IS_ONESHOT = 0x08000000,
	/**
	 * Client must read each descriptor from the FIFO
	 * using sps_get_iovec()
	 */
	SPS_O_ACK_TRANSFERS = 0x10000000,
	/* Connection is automatically enabled */
	SPS_O_AUTO_ENABLE = 0x20000000,
	/* DISABLE endpoint synchronization for config/enable/disable */
	SPS_O_NO_EP_SYNC = 0x40000000,
	/* Allow partial polling duing IRQ mode */
	SPS_O_HYBRID = 0x80000000,
	/* Allow dummy BAM connection */
	SPS_O_DUMMY_PEER = 0x00000400,
};

/**
 * This enum specifies BAM DMA channel priority.  Clients should use
 * SPS_DMA_PRI_DEFAULT unless a specific priority is required.
 */
enum sps_dma_priority {
	SPS_DMA_PRI_DEFAULT = 0,
	SPS_DMA_PRI_LOW,
	SPS_DMA_PRI_MED,
	SPS_DMA_PRI_HIGH,
};

/*
 * This enum specifies the ownership of a connection resource.
 * Remote or shared ownership is only possible/meaningful on the processor
 * that controls resource.
 */
enum sps_owner {
	SPS_OWNER_LOCAL = 0x1,	/* Resource is owned by local processor */
	SPS_OWNER_REMOTE = 0x2,	/* Resource is owned by a satellite processor */
};

/* This enum indicates the event associated with a client event trigger */
enum sps_event {
	SPS_EVENT_INVALID = 0,

	SPS_EVENT_EOT,		/* End-of-transfer */
	SPS_EVENT_DESC_DONE,	/* Descriptor processed */
	SPS_EVENT_OUT_OF_DESC,	/* Out of descriptors */
	SPS_EVENT_WAKEUP,	/* Peripheral wake up */
	SPS_EVENT_FLOWOFF,	/* Graceful halt (idle) */
	SPS_EVENT_INACTIVE,	/* Inactivity timeout */
	SPS_EVENT_ERROR,	/* Error */
	SPS_EVENT_RST_ERROR,    /* Pipe Reset unsuccessful */
	SPS_EVENT_HRESP_ERROR,  /* Errorneous Hresponse by AHB Master*/
	SPS_EVENT_MAX,
};

/*
 * This enum specifies the event trigger mode and is an argument for the
 * sps_register_event() function.
 */
enum sps_trigger {
	/* Trigger with payload for callback */
	SPS_TRIGGER_CALLBACK = 0,
	/* Trigger without payload for wait or poll */
	SPS_TRIGGER_WAIT,
};

/*
 * This enum indicates the desired halting mechanism and is an argument for the
 * sps_flow_off() function
 */
enum sps_flow_off {
	SPS_FLOWOFF_FORCED = 0,	/* Force hardware into halt state */
	/* Allow hardware to empty pipe before halting */
	SPS_FLOWOFF_GRACEFUL,
};

/*
 * This enum indicates the target memory heap and is an argument for the
 * sps_mem_alloc() function.
 */
enum sps_mem {
	SPS_MEM_LOCAL = 0,  /* SPS subsystem local (pipe) memory */
	SPS_MEM_UC,	    /* Microcontroller (ARM7) local memory */
};

/*
 * This enum indicates a timer control operation and is an argument for the
 * sps_timer_ctrl() function.
 */
enum sps_timer_op {
	SPS_TIMER_OP_CONFIG = 0,
	SPS_TIMER_OP_RESET,
/*   SPS_TIMER_OP_START,   Not supported by hardware yet */
/*   SPS_TIMER_OP_STOP,    Not supported by hardware yet */
	SPS_TIMER_OP_READ,
};

/*
 * This enum indicates the inactivity timer operating mode and is an
 * argument for the sps_timer_ctrl() function.
 */
enum sps_timer_mode {
	SPS_TIMER_MODE_ONESHOT = 0,
/*   SPS_TIMER_MODE_PERIODIC,    Not supported by hardware yet */
};

/* This enum indicates the cases when callback the user of BAM */
enum sps_callback_case {
	SPS_CALLBACK_BAM_ERROR_IRQ = 1,     /* BAM ERROR IRQ */
	SPS_CALLBACK_BAM_HRESP_ERR_IRQ,	    /* Erroneous HResponse */
	SPS_CALLBACK_BAM_TIMER_IRQ,	    /* Inactivity timer */
	SPS_CALLBACK_BAM_RES_REQ,	    /* Request resource */
	SPS_CALLBACK_BAM_RES_REL,	    /* Release resource */
	SPS_CALLBACK_BAM_POLL,	            /* To poll each pipe */
};

/*
 * This enum indicates the command type in a command element
 */
enum sps_command_type {
	SPS_WRITE_COMMAND = 0,
	SPS_READ_COMMAND,
};

/**
 * struct msm_sps_platform_data - SPS Platform specific data.
 * @bamdma_restricted_pipes - Bitmask of pipes restricted from local use.
 *
 */
struct msm_sps_platform_data {
	u32 bamdma_restricted_pipes;
};

/**
 * This data type corresponds to the native I/O vector (BAM descriptor)
 * supported by SPS hardware
 *
 * @addr - Buffer physical address.
 * @size - Buffer size in bytes.
 * @flags -Flag bitmask (see SPS_IOVEC_FLAG_ #defines).
 *
 */
struct sps_iovec {
	u32 addr;
	u32 size:16;
	u32 flags:16;
};

/**
 * This data type corresponds to the native Command Element
 * supported by SPS hardware
 *
 * @addr - register address.
 * @command - command type.
 * @data - for write command: content to be written into peripheral register.
 *         for read command: dest addr to write peripheral register value to.
 * @mask - register mask.
 * @reserved - for future usage.
 *
 */
struct sps_command_element {
	u32 addr:24;
	u32 command:8;
	u32 data;
	u32 mask;
	u32 reserved;
};

/*
 * BAM device's security configuration
 */
struct sps_bam_pipe_sec_config_props {
	u32 pipe_mask;
	u32 vmid;
};

struct sps_bam_sec_config_props {
	/* Per-EE configuration - This is a pipe bit mask for each EE */
	struct sps_bam_pipe_sec_config_props ees[SPS_BAM_NUM_EES];
};

/**
 * This struct defines a BAM device. The client must memset() this struct to
 * zero before writing device information.  A value of zero for uninitialized
 * values will instruct the SPS driver to use general defaults or
 * hardware/BIOS supplied values.
 *
 *
 * @options - See SPS_BAM_OPT_* bit flag.
 * @phys_addr - BAM base physical address (not peripheral address).
 * @virt_addr - BAM base virtual address.
 * @virt_size - For virtual mapping.
 * @irq - IRQ enum for use in ISR vector install.
 * @num_pipes - number of pipes. Can be read from hardware.
 * @summing_threshold - BAM event threshold.
 *
 * @periph_class - Peripheral device enumeration class.
 * @periph_dev_id - Peripheral global device ID.
 * @periph_phys_addr - Peripheral base physical address, for BAM-DMA only.
 * @periph_virt_addr - Peripheral base virtual address.
 * @periph_virt_size - Size for virtual mapping.
 *
 * @callback - callback function for BAM user.
 * @user - pointer to user data.
 *
 * @event_threshold - Pipe event threshold.
 * @desc_size - Size (bytes) of descriptor FIFO.
 * @data_size - Size (bytes) of data FIFO.
 * @desc_mem_id - Heap ID for default descriptor FIFO allocations.
 * @data_mem_id - Heap ID for default data FIFO allocations.
 *
 * @manage - BAM device management flags (see SPS_BAM_MGR_*).
 * @restricted_pipes - Bitmask of pipes restricted from local use.
 * @ee - Local execution environment index.
 *
 * @irq_gen_addr - MTI interrupt generation address. This configuration only
 * applies to BAM rev 1 and 2 hardware. MTIs are only supported on BAMs when
 * global config is controlled by a remote processor.
 * NOTE: This address must correspond to the MTI associated with the "irq" IRQ
 * enum specified above.
 *
 * @sec_config - must be set to SPS_BAM_SEC_DO_CONFIG to perform BAM security
 * configuration.  Only the processor that manages the BAM is allowed to
 * perform the configuration. The global (top-level) BAM interrupt will be
 * assigned to the EE of the processor that manages the BAM.
 *
 * @p_sec_config_props - BAM device's security configuration
 *
 */
struct sps_bam_props {

	/* BAM device properties. */

	u32 options;
	phys_addr_t phys_addr;
	void __iomem *virt_addr;
	u32 virt_size;
	u32 irq;
	u32 num_pipes;
	u32 summing_threshold;

	/* Peripheral device properties */

	u32 periph_class;
	u32 periph_dev_id;
	phys_addr_t periph_phys_addr;
	void *periph_virt_addr;
	u32 periph_virt_size;

	/* Connection pipe parameter defaults. */

	u32 event_threshold;
	u32 desc_size;
	u32 data_size;
	u32 desc_mem_id;
	u32 data_mem_id;

	/* Feedback to BAM user */
	void (*callback)(enum sps_callback_case, void *user);
	void *user;

	/* Security properties */

	u32 manage;
	u32 restricted_pipes;
	u32 ee;

	/* Log Level property */
	u32 ipc_loglevel;

	/* BAM MTI interrupt generation */

	u32 irq_gen_addr;

	/* Security configuration properties */

	u32 sec_config;
	struct sps_bam_sec_config_props *p_sec_config_props;

	/* Logging control */

	bool constrained_logging;
	u32 logging_number;
};

/**
 *  This struct specifies memory buffer properties.
 *
 * @base - Buffer virtual address.
 * @phys_base - Buffer physical address.
 * @size - Specifies buffer size (or maximum size).
 * @min_size - If non-zero, specifies buffer minimum size.
 *
 */
struct sps_mem_buffer {
	void *base;
	phys_addr_t phys_base;
	unsigned long iova;
	u32 size;
	u32 min_size;
};

/**
 * This struct defines a connection's end point and is used as the argument
 * for the sps_connect(), sps_get_config(), and sps_set_config() functions.
 * For system mode pipe, use SPS_DEV_HANDLE_MEM for the end point that
 * corresponds to system memory.
 *
 * The client can force SPS to reserve a specific pipe on a BAM.
 * If the pipe is in use, the sps_connect/set_config() will fail.
 *
 * @source - Source BAM.
 * @src_pipe_index - BAM pipe index, 0 to 30.
 * @destination - Destination BAM.
 * @dest_pipe_index - BAM pipe index, 0 to 30.
 *
 * @mode - specifies which end (source or destination) of the connection will
 * be controlled/referenced by the client.
 *
 * @config - This value is for future use and should be set to
 * SPS_CONFIG_DEFAULT or left as default from sps_get_config().
 *
 * @options - OR'd connection end point options (see SPS_O defines).
 *
 * WARNING: The memory provided should be physically contiguous and non-cached.
 * The user can use one of the following:
 * 1. sps_alloc_mem() - allocated from pipe-memory.
 * 2. dma_alloc_coherent() - allocate coherent DMA memory.
 * 3. dma_map_single() - for using memory allocated by kmalloc().
 *
 * @desc - Descriptor FIFO.
 * @data - Data FIFO (BAM-to-BAM mode only).
 *
 * @event_thresh - Pipe event threshold or derivative.
 * @lock_group - The lock group this pipe belongs to.
 *
 * @sps_reserved - Reserved word - client must not modify.
 *
 */
struct sps_connect {
	unsigned long source;
	unsigned long source_iova;
	u32 src_pipe_index;
	unsigned long destination;
	unsigned long dest_iova;
	u32 dest_pipe_index;

	enum sps_mode mode;

	u32 config;

	enum sps_option options;

	struct sps_mem_buffer desc;
	struct sps_mem_buffer data;

	u32 event_thresh;

	u32 lock_group;

	/* SETPEND/MTI interrupt generation parameters */

	u32 irq_gen_addr;
	u32 irq_gen_data;

	u32 sps_reserved;

};

/**
 * This struct defines a satellite connection's end point.  The client of the
 * SPS driver on the satellite processor must call sps_get_config() to
 * initialize a struct sps_connect, then copy the values from the struct
 * sps_satellite to the struct sps_connect before making the sps_connect()
 * call to the satellite SPS driver.
 *
 */
struct sps_satellite {
	/**
	 * These values must be copied to either the source or destination
	 * corresponding values in the connect struct.
	 */
	phys_addr_t dev;
	u32 pipe_index;

	/**
	 * These values must be copied to the corresponding values in the
	 * connect struct
	 */
	u32 config;
	enum sps_option options;

};

/**
 * This struct defines parameters for allocation of a BAM DMA channel. The
 * client must memset() this struct to zero before writing allocation
 * information.  A value of zero for uninitialized values will instruct
 * the SPS driver to use defaults or "don't care".
 *
 * @dev - Associated BAM device handle, or SPS_DEV_HANDLE_DMA.
 *
 * @src_owner - Source owner processor ID.
 * @dest_owner - Destination owner processor ID.
 *
 */
struct sps_alloc_dma_chan {
	unsigned long dev;

	/* BAM DMA channel configuration parameters */

	u32 threshold;
	enum sps_dma_priority priority;

	/**
	 * Owner IDs are global host processor identifiers used by the system
	 * SROT when establishing execution environments.
	 */
	u32 src_owner;
	u32 dest_owner;

};

/**
 * This struct defines parameters for an allocated BAM DMA channel.
 *
 * @dev - BAM DMA device handle.
 * @dest_pipe_index - Destination/input/write pipe index.
 * @src_pipe_index - Source/output/read pipe index.
 *
 */
struct sps_dma_chan {
	unsigned long dev;
	u32 dest_pipe_index;
	u32 src_pipe_index;
};

/**
 * This struct is an argument passed payload when triggering a callback event
 * object registered for an SPS connection end point.
 *
 * @user - Pointer registered with sps_register_event().
 *
 * @event_id - Which event.
 *
 * @iovec - The associated I/O vector. If the end point is a system-mode
 * producer, the size will reflect the actual number of bytes written to the
 * buffer by the pipe. NOTE: If this I/O vector was part of a set submitted to
 * sps_transfer(), then the vector array itself will be	updated with all of
 * the actual counts.
 *
 * @user - Pointer registered with the transfer.
 *
 */
struct sps_event_notify {
	void *user;

	enum sps_event event_id;

	/* Data associated with the event */

	union {
		/* Data for SPS_EVENT_IRQ */
		struct {
			u32 mask;
		} irq;

		/* Data for SPS_EVENT_EOT or SPS_EVENT_DESC_DONE */

		struct {
			struct sps_iovec iovec;
			void *user;
		} transfer;

		/* Data for SPS_EVENT_ERROR */

		struct {
			u32 status;
		} err;

	} data;
};

/**
 * This struct defines a event registration parameters and is used as the
 * argument for the sps_register_event() function.
 *
 * @options - Event options that will trigger the event object.
 * @mode - Event trigger mode.
 *
 * @xfer_done - a pointer to a completion object. NULL if not in use.
 *
 * @callback - a callback to call on completion. NULL if not in use.
 *
 * @user - User pointer that will be provided in event callback data.
 *
 */
struct sps_register_event {
	enum sps_option options;
	enum sps_trigger mode;
	struct completion *xfer_done;
	void (*callback)(struct sps_event_notify *notify);
	void *user;
};

/**
 * This struct defines a system memory transfer's parameters and is used as the
 * argument for the sps_transfer() function.
 *
 * @iovec_phys - Physical address of I/O vectors buffer.
 * @iovec - Pointer to I/O vectors buffer.
 * @iovec_count - Number of I/O vectors.
 * @user - User pointer passed in callback event.
 *
 */
struct sps_transfer {
	phys_addr_t iovec_phys;
	struct sps_iovec *iovec;
	u32 iovec_count;
	void *user;
};

/**
 * This struct defines a timer control operation parameters and is used as an
 * argument for the sps_timer_ctrl() function.
 *
 * @op - Timer control operation.
 * @timeout_msec - Inactivity timeout (msec).
 *
 */
struct sps_timer_ctrl {
	enum sps_timer_op op;

	/**
	 * The following configuration parameters must be set when the timer
	 * control operation is SPS_TIMER_OP_CONFIG.
	 */
	enum sps_timer_mode mode;
	u32 timeout_msec;
};

/**
 * This struct defines a timer control operation result and is used as an
 * argument for the sps_timer_ctrl() function.
 */
struct sps_timer_result {
	u32 current_timer;
};


/*----------------------------------------------------------------------------
 * Functions specific to sps interface
 * ---------------------------------------------------------------------------
 */
struct sps_pipe;	/* Forward declaration */

#if IS_ENABLED(CONFIG_SPS)
/**
 * Register a BAM device
 *
 * This function registers a BAM device with the SPS driver. For each
 *peripheral that includes a BAM, the peripheral driver must register
 * the BAM with the SPS driver.
 *
 * A requirement is that the peripheral driver must remain attached
 * to the SPS driver until the BAM is deregistered. Otherwise, the
 * system may attempt to unload the SPS driver. BAM registrations would
 * be lost.
 *
 * @bam_props - Pointer to struct for BAM device properties.
 *
 * @dev_handle - Device handle will be written to this location (output).
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_register_bam_device(const struct sps_bam_props *bam_props,
			    unsigned long *dev_handle);

/**
 * Deregister a BAM device
 *
 * This function deregisters a BAM device from the SPS driver. The peripheral
 * driver should deregister a BAM when the peripheral driver is shut down or
 * when BAM use should be disabled.
 *
 * A BAM cannot be deregistered if any of its pipes is in an active connection.
 *
 * When all BAMs have been deregistered, the system is free to unload the
 * SPS driver.
 *
 * @dev_handle - BAM device handle.
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_deregister_bam_device(unsigned long dev_handle);

/**
 * Allocate client state context
 *
 * This function allocate and initializes a client state context struct.
 *
 * @return pointer to client state context
 *
 */
struct sps_pipe *sps_alloc_endpoint(void);

/**
 * Free client state context
 *
 * This function de-initializes and free a client state context struct.
 *
 * @ctx - client context for SPS connection end point
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_free_endpoint(struct sps_pipe *h);

/**
 * Get the configuration parameters for an SPS connection end point
 *
 * This function retrieves the configuration parameters for an SPS connection
 * end point.
 * This function may be called before the end point is connected (before
 * sps_connect is called). This allows the client to specify parameters before
 * the connection is established.
 *
 * The client must call this function to fill it's struct sps_connect
 * struct before modifying values and passing the struct to sps_set_config().
 *
 * @h - client context for SPS connection end point
 *
 * @config - Pointer to buffer for the end point's configuration parameters.
 * Must not be NULL.
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_get_config(struct sps_pipe *h, struct sps_connect *config);

/**
 * Allocate memory from the SPS Pipe-Memory.
 *
 * @h - client context for SPS connection end point
 *
 * @mem - memory type - N/A.
 *
 * @mem_buffer - Pointer to struct for allocated memory properties.
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_alloc_mem(struct sps_pipe *h, enum sps_mem mem,
		  struct sps_mem_buffer *mem_buffer);

/**
 * Free memory from the SPS Pipe-Memory.
 *
 * @h - client context for SPS connection end point
 *
 * @mem_buffer - Pointer to struct for allocated memory properties.
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_free_mem(struct sps_pipe *h, struct sps_mem_buffer *mem_buffer);

/**
 * Connect an SPS connection end point
 *
 * This function creates a connection between two SPS peripherals or between
 * an SPS peripheral and the local host processor (via system memory, end
 *point SPS_DEV_HANDLE_MEM). Establishing the connection includes
 * initialization of the SPS hardware and allocation of any other connection
 * resources (buffer memory, etc.).
 *
 * This function requires the client to specify both the source and
 * destination end points of the SPS connection. However, the handle
 * returned applies only to the end point of the connection that the client
 * controls. The end point under control must be specified by the
 * enum sps_mode mode argument, either SPS_MODE_SRC, SPS_MODE_DEST, or
 * SPS_MODE_CTL. Note that SPS_MODE_CTL is only supported for I/O
 * accelerator connections, and only a limited set of control operations are
 * allowed (TBD).
 *
 * For a connection involving system memory
 * (SPS_DEV_HANDLE_MEM), the peripheral end point must be
 * specified. For example, SPS_MODE_SRC must be specified for a
 * BAM-to-system connection, since the BAM pipe is the data
 * producer.
 *
 * For a specific peripheral-to-peripheral connection, there may be more than
 * one required configuration. For example, there might be high-performance
 * and low-power configurations for a connection between the two peripherals.
 * The config argument allows the client to specify different configurations,
 * which may require different system resource allocations and hardware
 * initialization.
 *
 * A client is allowed to create one and only one connection for its
 * struct sps_pipe. The handle is used to identify the connection end point
 * in subsequent SPS driver calls. A specific connection source or
 * destination end point can be associated with one and only one
 * struct sps_pipe.
 *
 * The client must establish an open device handle to the SPS. To do so, the
 * client must attach to the SPS driver and open the SPS device by calling
 * the following functions.
 *
 * @h - client context for SPS connection end point
 *
 * @connect - Pointer to connection parameters
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_connect(struct sps_pipe *h, struct sps_connect *connect);

/**
 * Disconnect an SPS connection end point
 *
 * This function disconnects an SPS connection end point.
 * The SPS hardware associated with that end point will be disabled.
 * For a connection involving system memory (SPS_DEV_HANDLE_MEM), all
 * connection resources are deallocated. For a peripheral-to-peripheral
 * connection, the resources associated with the connection will not be
 * deallocated until both end points are closed.
 *
 * The client must call sps_connect() for the handle before calling
 * this function.
 *
 * @h - client context for SPS connection end point
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_disconnect(struct sps_pipe *h);

/**
 * Register an event object for an SPS connection end point
 *
 * This function registers a callback event object for an SPS connection end
 *point. The registered event object will be triggered for the set of
 * events specified in reg->options that are enabled for the end point.
 *
 * There can only be one registered event object for each event. If an event
 * object is already registered for an event, it will be replaced. If
 *reg->event handle is NULL, then any registered event object for the
 * event will be deregistered. Option bits in reg->options not associated
 * with events are ignored.
 *
 * The client must call sps_connect() for the handle before calling
 * this function.
 *
 * @h - client context for SPS connection end point
 *
 * @reg - Pointer to event registration parameters
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_register_event(struct sps_pipe *h, struct sps_register_event *reg);

/**
 * Perform a single DMA transfer on an SPS connection end point
 *
 * This function submits a DMA transfer request consisting of a single buffer
 * for an SPS connection end point associated with a peripheral-to/from-memory
 * connection. The request will be submitted immediately to hardware if the
 * hardware is idle (data flow off, no other pending transfers). Otherwise, it
 * will be queued for later handling in the SPS driver work loop.
 *
 * The data buffer must be DMA ready. The client is responsible for insuring
 *physically contiguous memory, cache maintenance, and memory barrier. For
 * more information, see Appendix A.
 *
 * The client must not modify the data buffer until the completion indication is
 * received.
 *
 * This function cannot be used if transfer queuing is disabled (see option
 * SPS_O_NO_Q). The client must set the SPS_O_EOT option to receive a callback
 * event trigger when the transfer is complete. The SPS driver will insure the
 * appropriate flags in the I/O vectors are set to generate the completion
 * indication.
 *
 * The return value from this function may indicate that an error occurred.
 * Possible causes include invalid arguments.
 *
 * @h - client context for SPS connection end point
 *
 * @addr - Physical address of buffer to transfer.
 *
 * WARNING: The memory provided	should be physically contiguous and
 * non-cached.
 *
 * The user can use one of the following:
 * 1. sps_alloc_mem() - allocated from pipe-memory.
 * 2. dma_alloc_coherent() - allocate DMA memory.
 * 3. dma_map_single() for memory allocated by kmalloc().
 *
 * @size - Size in bytes of buffer to transfer
 *
 * @user - User pointer that will be returned to user as part of
 *  event payload
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_transfer_one(struct sps_pipe *h, phys_addr_t addr, u32 size,
		     void *user, u32 flags);

/**
 * Read event queue for an SPS connection end point
 *
 * This function reads event queue for an SPS connection end point.
 *
 * @h - client context for SPS connection end point
 *
 * @event - pointer to client's event data buffer
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_get_event(struct sps_pipe *h, struct sps_event_notify *event);

/**
 * Get processed I/O vector (completed transfers)
 *
 * This function fetches the next processed I/O vector.
 *
 * @h - client context for SPS connection end point
 *
 * @iovec - Pointer to I/O vector struct (output).
 * This struct will be zeroed if there are no more processed I/O vectors.
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_get_iovec(struct sps_pipe *h, struct sps_iovec *iovec);

/**
 * Enable an SPS connection end point
 *
 * This function enables an SPS connection end point.
 *
 * @h - client context for SPS connection end point
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_flow_on(struct sps_pipe *h);

/**
 * Disable an SPS connection end point
 *
 * This function disables an SPS connection end point.
 *
 * @h - client context for SPS connection end point
 *
 * @mode - Desired mode for disabling pipe data flow
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_flow_off(struct sps_pipe *h, enum sps_flow_off mode);

/**
 * Perform a Multiple DMA transfer on an SPS connection end point
 *
 * This function submits a DMA transfer request for an SPS connection end point
 * associated with a peripheral-to/from-memory connection. The request will be
 * submitted immediately to hardware if the hardware is idle (data flow off, no
 * other pending transfers). Otherwise, it will be queued for later handling in
 * the SPS driver work loop.
 *
 * The data buffers referenced by the I/O vectors must be DMA ready.
 * The client is responsible for insuring physically contiguous memory,
 * any cache maintenance, and memory barrier. For more information,
 * see Appendix A.
 *
 * The I/O vectors must specify physical addresses for the referenced buffers.
 *
 * The client must not modify the data buffers referenced by I/O vectors until
 * the completion indication is received.
 *
 * If transfer queuing is disabled (see option SPS_O_NO_Q), the client is
 * responsible for setting the appropriate flags in the I/O vectors to generate
 * the completion indication. Also, the client is responsible for enabling the
 * appropriate connection callback event options for completion indication (see
 * sps_connect(), sps_set_config()).
 *
 * If transfer queuing is enabled, the client must set the SPS_O_EOT option to
 * receive a callback event trigger when the transfer is complete. The SPS
 * driver will insure the appropriate flags in the I/O vectors are set to
 * generate the completion indication. The client must not set any flags in the
 * I/O vectors, as this may cause the SPS driver to become out of sync with the
 * hardware.
 *
 * The return value from this function may indicate that an error occurred.
 * Possible causes include invalid arguments. If transfer queuing is disabled,
 * an error will occur if the pipe is already processing a transfer.
 *
 * @h - client context for SPS connection end point
 *
 * @transfer - Pointer to transfer parameter struct
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_transfer(struct sps_pipe *h, struct sps_transfer *transfer);

/**
 * Determine whether an SPS connection end point FIFO is empty
 *
 * This function returns the empty state of an SPS connection end point.
 *
 * @h - client context for SPS connection end point
 *
 * @empty - pointer to client's empty status word (boolean)
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_is_pipe_empty(struct sps_pipe *h, u32 *empty);

/**
 * Reset an SPS BAM device
 *
 * This function resets an SPS BAM device.
 *
 * @dev - device handle for the BAM
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_device_reset(unsigned long dev);

/**
 * Set the configuration parameters for an SPS connection end point
 *
 * This function sets the configuration parameters for an SPS connection
 * end point. This function may be called before the end point is connected
 * (before sps_connect is called). This allows the client to specify
 *parameters before the connection is established. The client is allowed
 * to pre-allocate resources and override driver defaults.
 *
 * The client must call sps_get_config() to fill it's struct sps_connect
 * struct before modifying values and passing the struct to this function.
 * Only those parameters that differ from the current configuration will
 * be processed.
 *
 * @h - client context for SPS connection end point
 *
 * @config - Pointer to the end point's new configuration parameters.
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_set_config(struct sps_pipe *h, struct sps_connect *config);

/**
 * Set ownership of an SPS connection end point
 *
 * This function sets the ownership of an SPS connection end point to
 * either local (default) or non-local. This function is used to
 * retrieve the struct sps_connect data that must be used by a
 * satellite processor when calling sps_connect().
 *
 * Non-local ownership is only possible/meaningful on the processor
 * that controls resource allocations (apps processor). Setting ownership
 * to non-local on a satellite processor will fail.
 *
 * Setting ownership from non-local to local will succeed only if the
 * owning satellite processor has properly brought the end point to
 * an idle condition.
 *
 * This function will succeed if the connection end point is already in
 * the specified ownership state.
 *
 * @h - client context for SPS connection end point
 *
 * @owner - New ownership of the connection end point
 *
 * @connect - Pointer to buffer for satellite processor connect data.
 *  Can be NULL to avoid retrieving the connect data. Will be ignored
 *  if the end point ownership is set to local.
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_set_owner(struct sps_pipe *h, enum sps_owner owner,
		  struct sps_satellite *connect);

#ifdef CONFIG_SPS_SUPPORT_BAMDMA
/**
 * Allocate a BAM DMA channel
 *
 * This function allocates a BAM DMA channel. A "BAM DMA" is a special
 * DMA peripheral with a BAM front end. The DMA peripheral acts as a conduit
 * for data to flow into a consumer pipe and then out of a producer pipe.
 * It's primarily purpose is to serve as a path for interprocessor communication
 * that allows each processor to control and protect it's own memory space.
 *
 * @alloc - Pointer to struct for BAM DMA channel allocation properties.
 *
 * @chan - Allocated channel information will be written to this
 *  location (output).
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_alloc_dma_chan(const struct sps_alloc_dma_chan *alloc,
		       struct sps_dma_chan *chan);

/**
 * Free a BAM DMA channel
 *
 * This function frees a BAM DMA channel.
 *
 * @chan - Pointer to information for channel to free
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_free_dma_chan(struct sps_dma_chan *chan);

/**
 * Get the BAM handle for BAM-DMA.
 *
 * The BAM handle should be use as source/destination in the sps_connect().
 *
 * @return handle on success, zero on error
 *
 */
unsigned long sps_dma_get_bam_handle(void);

/**
 * Free the BAM handle for BAM-DMA.
 *
 */
void sps_dma_free_bam_handle(unsigned long h);
#else
static inline int sps_alloc_dma_chan(const struct sps_alloc_dma_chan *alloc,
		       struct sps_dma_chan *chan)
{
	return -EPERM;
}

static inline int sps_free_dma_chan(struct sps_dma_chan *chan)
{
	return -EPERM;
}

static inline unsigned long sps_dma_get_bam_handle(void)
{
	return 0;
}

static inline void sps_dma_free_bam_handle(unsigned long h)
{
}
#endif

/**
 * Get number of free transfer entries for an SPS connection end point
 *
 * This function returns the number of free transfer entries for an
 * SPS connection end point.
 *
 * @h - client context for SPS connection end point
 *
 * @count - pointer to count status
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_get_free_count(struct sps_pipe *h, u32 *count);

/**
 * Perform timer control
 *
 * This function performs timer control operations.
 *
 * @h - client context for SPS connection end point
 *
 * @timer_ctrl - Pointer to timer control specification
 *
 * @timer_result - Pointer to buffer for timer operation result.
 *  This argument can be NULL if no result is expected for the operation.
 *  If non-NULL, the current timer value will always provided.
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_timer_ctrl(struct sps_pipe *h,
		   struct sps_timer_ctrl *timer_ctrl,
		   struct sps_timer_result *timer_result);

/**
 * Find the handle of a BAM device based on the physical address
 *
 * This function finds a BAM device in the BAM registration list that
 * matches the specified physical address, and returns its handle.
 *
 * @phys_addr - physical address of the BAM
 *
 * @h - device handle of the BAM
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_phy2h(phys_addr_t phys_addr, unsigned long *handle);

/**
 * Setup desc/data FIFO for bam-to-bam connection
 *
 * @mem_buffer - Pointer to struct for allocated memory properties.
 *
 * @addr - address of FIFO
 *
 * @size - FIFO size
 *
 * @use_offset - use address offset instead of absolute address
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_setup_bam2bam_fifo(struct sps_mem_buffer *mem_buffer,
		  u32 addr, u32 size, int use_offset);

/**
 * Get the number of unused descriptors in the descriptor FIFO
 * of a pipe
 *
 * @h - client context for SPS connection end point
 *
 * @desc_num - number of unused descriptors
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_get_unused_desc_num(struct sps_pipe *h, u32 *desc_num);

/**
 * Get the debug info of BAM registers and descriptor FIFOs
 *
 * @dev - BAM device handle
 *
 * @option - debugging option
 *
 * @para - parameter used for an option (such as pipe combination)
 *
 * @tb_sel - testbus selection
 *
 * @desc_sel - selection of descriptors
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_get_bam_debug_info(unsigned long dev, u32 option, u32 para,
		u32 tb_sel, u32 desc_sel);

/**
 * Vote for or relinquish BAM DMA clock
 *
 * @clk_on - to turn on or turn off the clock
 *
 * @return 0 on success, negative value on error
 *
 */
int sps_ctrl_bam_dma_clk(bool clk_on);

/*
 * sps_pipe_reset - reset a pipe of a BAM.
 * @dev:	BAM device handle
 * @pipe:	pipe index
 *
 * This function resets a pipe of a BAM.
 *
 * Return: 0 on success, negative value on error
 */
int sps_pipe_reset(unsigned long dev, u32 pipe);

/*
 * sps_pipe_disable - disable a pipe of a BAM.
 * @dev:	BAM device handle
 * @pipe:	pipe index
 *
 * This function disables a pipe of a BAM.
 *
 * Return: 0 on success, negative value on error
 */
int sps_pipe_disable(unsigned long dev, u32 pipe);

/*
 * sps_pipe_pending_desc - checking pending descriptor.
 * @dev:	BAM device handle
 * @pipe:	pipe index
 * @pending:	indicate if there is any pending descriptor.
 *
 * This function checks if a pipe of a BAM has any pending descriptor.
 *
 * Return: 0 on success, negative value on error
 */
int sps_pipe_pending_desc(unsigned long dev, u32 pipe, bool *pending);

/*
 * sps_bam_process_irq - process IRQ of a BAM.
 * @dev:	BAM device handle
 *
 * This function processes any pending IRQ of a BAM.
 *
 * Return: 0 on success, negative value on error
 */
int sps_bam_process_irq(unsigned long dev);

/*
 * sps_bam_enable_irqs - enable IRQs of a BAM.
 * @dev:	BAM device handle
 *
 * This function enables all IRQs of a BAM.
 *
 * Return: 0 on success, negative value on error
 */
int sps_bam_enable_irqs(unsigned long dev);

/*
 * sps_bam_disable_irqs - disable IRQs of a BAM.
 * @dev:	BAM device handle
 *
 * This function disables all IRQs of a BAM.
 *
 * Return: 0 on success, negative value on error
 */
int sps_bam_disable_irqs(unsigned long dev);

/*
 * sps_get_bam_addr - get address info of a BAM.
 * @dev:	BAM device handle
 * @base:	beginning address
 * @size:	address range size
 *
 * This function returns the address info of a BAM.
 *
 * Return: 0 on success, negative value on error
 */
int sps_get_bam_addr(unsigned long dev, phys_addr_t *base,
				u32 *size);

/*
 * sps_pipe_inject_zlt - inject a ZLT with EOT.
 * @dev:	BAM device handle
 * @pipe_index:	pipe index
 *
 * This function injects a ZLT with EOT for a pipe of a BAM.
 *
 * Return: 0 on success, negative value on error
 */
int sps_pipe_inject_zlt(unsigned long dev, u32 pipe_index);
#else
static inline int sps_register_bam_device(const struct sps_bam_props
			*bam_props, unsigned long *dev_handle)
{
	return -EPERM;
}

static inline int sps_deregister_bam_device(unsigned long dev_handle)
{
	return -EPERM;
}

static inline struct sps_pipe *sps_alloc_endpoint(void)
{
	return NULL;
}

static inline int sps_free_endpoint(struct sps_pipe *h)
{
	return -EPERM;
}

static inline int sps_get_config(struct sps_pipe *h, struct sps_connect *config)
{
	return -EPERM;
}

static inline int sps_alloc_mem(struct sps_pipe *h, enum sps_mem mem,
		  struct sps_mem_buffer *mem_buffer)
{
	return -EPERM;
}

static inline int sps_free_mem(struct sps_pipe *h,
				struct sps_mem_buffer *mem_buffer)
{
	return -EPERM;
}

static inline int sps_connect(struct sps_pipe *h, struct sps_connect *connect)
{
	return -EPERM;
}

static inline int sps_disconnect(struct sps_pipe *h)
{
	return -EPERM;
}

static inline int sps_register_event(struct sps_pipe *h,
					struct sps_register_event *reg)
{
	return -EPERM;
}

static inline int sps_transfer_one(struct sps_pipe *h, phys_addr_t addr,
					u32 size, void *user, u32 flags)
{
	return -EPERM;
}

static inline int sps_get_event(struct sps_pipe *h,
				struct sps_event_notify *event)
{
	return -EPERM;
}

static inline int sps_get_iovec(struct sps_pipe *h, struct sps_iovec *iovec)
{
	return -EPERM;
}

static inline int sps_flow_on(struct sps_pipe *h)
{
	return -EPERM;
}

static inline int sps_flow_off(struct sps_pipe *h, enum sps_flow_off mode)
{
	return -EPERM;
}

static inline int sps_transfer(struct sps_pipe *h,
				struct sps_transfer *transfer)
{
	return -EPERM;
}

static inline int sps_is_pipe_empty(struct sps_pipe *h, u32 *empty)
{
	return -EPERM;
}

static inline int sps_device_reset(unsigned long dev)
{
	return -EPERM;
}

static inline int sps_set_config(struct sps_pipe *h, struct sps_connect *config)
{
	return -EPERM;
}

static inline int sps_set_owner(struct sps_pipe *h, enum sps_owner owner,
		  struct sps_satellite *connect)
{
	return -EPERM;
}

static inline int sps_get_free_count(struct sps_pipe *h, u32 *count)
{
	return -EPERM;
}

static inline int sps_alloc_dma_chan(const struct sps_alloc_dma_chan *alloc,
		       struct sps_dma_chan *chan)
{
	return -EPERM;
}

static inline int sps_free_dma_chan(struct sps_dma_chan *chan)
{
	return -EPERM;
}

static inline unsigned long sps_dma_get_bam_handle(void)
{
	return 0;
}

static inline void sps_dma_free_bam_handle(unsigned long h)
{
}

static inline int sps_timer_ctrl(struct sps_pipe *h,
		   struct sps_timer_ctrl *timer_ctrl,
		   struct sps_timer_result *timer_result)
{
	return -EPERM;
}

static inline int sps_phy2h(phys_addr_t phys_addr, unsigned long *handle)
{
	return -EPERM;
}

static inline int sps_setup_bam2bam_fifo(struct sps_mem_buffer *mem_buffer,
		  u32 addr, u32 size, int use_offset)
{
	return -EPERM;
}

static inline int sps_get_unused_desc_num(struct sps_pipe *h, u32 *desc_num)
{
	return -EPERM;
}

static inline int sps_get_bam_debug_info(unsigned long dev, u32 option,
		u32 para, u32 tb_sel, u32 desc_sel)
{
	return -EPERM;
}

static inline int sps_ctrl_bam_dma_clk(bool clk_on)
{
	return -EPERM;
}

static inline int sps_pipe_reset(unsigned long dev, u32 pipe)
{
	return -EPERM;
}

static inline int sps_pipe_disable(unsigned long dev, u32 pipe)
{
	return -EPERM;
}

static inline int sps_pipe_pending_desc(unsigned long dev, u32 pipe,
					bool *pending)
{
	return -EPERM;
}

static inline int sps_bam_process_irq(unsigned long dev)
{
	return -EPERM;
}

static inline int sps_bam_enable_irqs(unsigned long dev)
{
	return -EPERM;
}

static inline int sps_bam_disable_irqs(unsigned long dev)
{
	return -EPERM;
}

static inline int sps_get_bam_addr(unsigned long dev, phys_addr_t *base,
				u32 *size)
{
	return -EPERM;
}

static inline int sps_pipe_inject_zlt(unsigned long dev, u32 pipe_index)
{
	return -EPERM;
}
#endif

#endif /* _SPS_H_ */
