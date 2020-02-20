/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2018-2020, The Linux Foundation. All rights reserved.
 *
 */
#ifndef _MHI_H_
#define _MHI_H_

#include <linux/device.h>
#include <linux/dma-direction.h>
#include <linux/mutex.h>
#include <linux/rwlock_types.h>
#include <linux/slab.h>
#include <linux/spinlock_types.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

struct mhi_chan;
struct mhi_event;
struct mhi_ctxt;
struct mhi_cmd;
struct mhi_buf_info;

/**
 * enum mhi_callback - MHI callback
 * @MHI_CB_IDLE: MHI entered idle state
 * @MHI_CB_PENDING_DATA: New data available for client to process
 * @MHI_CB_LPM_ENTER: MHI host entered low power mode
 * @MHI_CB_LPM_EXIT: MHI host about to exit low power mode
 * @MHI_CB_EE_RDDM: MHI device entered RDDM exec env
 * @MHI_CB_EE_MISSION_MODE: MHI device entered Mission Mode exec env
 * @MHI_CB_SYS_ERROR: MHI device entered error state (may recover)
 * @MHI_CB_FATAL_ERROR: MHI device entered fatal error state
 */
enum mhi_callback {
	MHI_CB_IDLE,
	MHI_CB_PENDING_DATA,
	MHI_CB_LPM_ENTER,
	MHI_CB_LPM_EXIT,
	MHI_CB_EE_RDDM,
	MHI_CB_EE_MISSION_MODE,
	MHI_CB_SYS_ERROR,
	MHI_CB_FATAL_ERROR,
};

/**
 * enum mhi_flags - Transfer flags
 * @MHI_EOB: End of buffer for bulk transfer
 * @MHI_EOT: End of transfer
 * @MHI_CHAIN: Linked transfer
 */
enum mhi_flags {
	MHI_EOB,
	MHI_EOT,
	MHI_CHAIN,
};

/**
 * enum mhi_device_type - Device types
 * @MHI_DEVICE_XFER: Handles data transfer
 * @MHI_DEVICE_CONTROLLER: Control device
 */
enum mhi_device_type {
	MHI_DEVICE_XFER,
	MHI_DEVICE_CONTROLLER,
};

/**
 * enum mhi_ch_type - Channel types
 * @MHI_CH_TYPE_INVALID: Invalid channel type
 * @MHI_CH_TYPE_OUTBOUND: Outbound channel to the device
 * @MHI_CH_TYPE_INBOUND: Inbound channel from the device
 * @MHI_CH_TYPE_INBOUND_COALESCED: Coalesced channel for the device to combine
 *				   multiple packets and send them as a single
 *				   large packet to reduce CPU consumption
 */
enum mhi_ch_type {
	MHI_CH_TYPE_INVALID = 0,
	MHI_CH_TYPE_OUTBOUND = DMA_TO_DEVICE,
	MHI_CH_TYPE_INBOUND = DMA_FROM_DEVICE,
	MHI_CH_TYPE_INBOUND_COALESCED = 3,
};

/**
 * enum mhi_ee_type - Execution environment types
 * @MHI_EE_PBL: Primary Bootloader
 * @MHI_EE_SBL: Secondary Bootloader
 * @MHI_EE_AMSS: Modem, aka the primary runtime EE
 * @MHI_EE_RDDM: Ram dump download mode
 * @MHI_EE_WFW: WLAN firmware mode
 * @MHI_EE_PTHRU: Passthrough
 * @MHI_EE_EDL: Embedded downloader
 */
enum mhi_ee_type {
	MHI_EE_PBL,
	MHI_EE_SBL,
	MHI_EE_AMSS,
	MHI_EE_RDDM,
	MHI_EE_WFW,
	MHI_EE_PTHRU,
	MHI_EE_EDL,
	MHI_EE_MAX_SUPPORTED = MHI_EE_EDL,
	MHI_EE_DISABLE_TRANSITION, /* local EE, not related to mhi spec */
	MHI_EE_NOT_SUPPORTED,
	MHI_EE_MAX,
};

/**
 * enum mhi_ch_ee_mask - Execution environment mask for channel
 * @MHI_CH_EE_PBL: Allow channel to be used in PBL EE
 * @MHI_CH_EE_SBL: Allow channel to be used in SBL EE
 * @MHI_CH_EE_AMSS: Allow channel to be used in AMSS EE
 * @MHI_CH_EE_RDDM: Allow channel to be used in RDDM EE
 * @MHI_CH_EE_PTHRU: Allow channel to be used in PTHRU EE
 * @MHI_CH_EE_WFW: Allow channel to be used in WFW EE
 * @MHI_CH_EE_EDL: Allow channel to be used in EDL EE
 */
enum mhi_ch_ee_mask {
	MHI_CH_EE_PBL = BIT(MHI_EE_PBL),
	MHI_CH_EE_SBL = BIT(MHI_EE_SBL),
	MHI_CH_EE_AMSS = BIT(MHI_EE_AMSS),
	MHI_CH_EE_RDDM = BIT(MHI_EE_RDDM),
	MHI_CH_EE_PTHRU = BIT(MHI_EE_PTHRU),
	MHI_CH_EE_WFW = BIT(MHI_EE_WFW),
	MHI_CH_EE_EDL = BIT(MHI_EE_EDL),
};

/**
 * enum mhi_er_data_type - Event ring data types
 * @MHI_ER_DATA: Only client data over this ring
 * @MHI_ER_CTRL: MHI control data and client data
 */
enum mhi_er_data_type {
	MHI_ER_DATA,
	MHI_ER_CTRL,
};

/**
 * enum mhi_db_brst_mode - Doorbell mode
 * @MHI_DB_BRST_DISABLE: Burst mode disable
 * @MHI_DB_BRST_ENABLE: Burst mode enable
 */
enum mhi_db_brst_mode {
	MHI_DB_BRST_DISABLE = 0x2,
	MHI_DB_BRST_ENABLE = 0x3,
};

/**
 * struct mhi_channel_config - Channel configuration structure for controller
 * @name: The name of this channel
 * @num: The number assigned to this channel
 * @num_elements: The number of elements that can be queued to this channel
 * @local_elements: The local ring length of the channel
 * @event_ring: The event rung index that services this channel
 * @dir: Direction that data may flow on this channel
 * @type: Channel type
 * @ee_mask: Execution Environment mask for this channel
 * @pollcfg: Polling configuration for burst mode.  0 is default.  milliseconds
	     for UL channels, multiple of 8 ring elements for DL channels
 * @doorbell: Doorbell mode
 * @lpm_notify: The channel master requires low power mode notifications
 * @offload_channel: The client manages the channel completely
 * @doorbell_mode_switch: Channel switches to doorbell mode on M0 transition
 * @auto_queue: Framework will automatically queue buffers for DL traffic
 * @auto_start: Automatically start (open) this channel
 */
struct mhi_channel_config {
	char *name;
	u32 num;
	u32 num_elements;
	u32 local_elements;
	u32 event_ring;
	enum dma_data_direction dir;
	enum mhi_ch_type type;
	u32 ee_mask;
	u32 pollcfg;
	enum mhi_db_brst_mode doorbell;
	bool lpm_notify;
	bool offload_channel;
	bool doorbell_mode_switch;
	bool auto_queue;
	bool auto_start;
};

/**
 * struct mhi_event_config - Event ring configuration structure for controller
 * @num_elements: The number of elements that can be queued to this ring
 * @irq_moderation_ms: Delay irq for additional events to be aggregated
 * @irq: IRQ associated with this ring
 * @channel: Dedicated channel number. U32_MAX indicates a non-dedicated ring
 * @priority: Priority of this ring. Use 1 for now
 * @mode: Doorbell mode
 * @data_type: Type of data this ring will process
 * @hardware_event: This ring is associated with hardware channels
 * @client_managed: This ring is client managed
 * @offload_channel: This ring is associated with an offloaded channel
 */
struct mhi_event_config {
	u32 num_elements;
	u32 irq_moderation_ms;
	u32 irq;
	u32 channel;
	u32 priority;
	enum mhi_db_brst_mode mode;
	enum mhi_er_data_type data_type;
	bool hardware_event;
	bool client_managed;
	bool offload_channel;
};

/**
 * struct mhi_controller_config - Root MHI controller configuration
 * @max_channels: Maximum number of channels supported
 * @timeout_ms: Timeout value for operations. 0 means use default
 * @buf_len: Size of automatically allocated buffers. 0 means use default
 * @num_channels: Number of channels defined in @ch_cfg
 * @ch_cfg: Array of defined channels
 * @num_events: Number of event rings defined in @event_cfg
 * @event_cfg: Array of defined event rings
 * @use_bounce_buf: Use a bounce buffer pool due to limited DDR access
 * @m2_no_db: Host is not allowed to ring DB in M2 state
 */
struct mhi_controller_config {
	u32 max_channels;
	u32 timeout_ms;
	u32 buf_len;
	u32 num_channels;
	struct mhi_channel_config *ch_cfg;
	u32 num_events;
	struct mhi_event_config *event_cfg;
	bool use_bounce_buf;
	bool m2_no_db;
};

/**
 * struct mhi_controller - Master MHI controller structure
 * @cntrl_dev: Pointer to the struct device of physical bus acting as the MHI
 *            controller (required)
 * @mhi_dev: MHI device instance for the controller
 * @regs: Base address of MHI MMIO register space (required)
 * @iova_start: IOMMU starting address for data (required)
 * @iova_stop: IOMMU stop address for data (required)
 * @fw_image: Firmware image name for normal booting (required)
 * @edl_image: Firmware image name for emergency download mode (optional)
 * @sbl_size: SBL image size downloaded through BHIe (optional)
 * @seg_len: BHIe vector size (optional)
 * @mhi_chan: Points to the channel configuration table
 * @lpm_chans: List of channels that require LPM notifications
 * @irq: base irq # to request (required)
 * @max_chan: Maximum number of channels the controller supports
 * @total_ev_rings: Total # of event rings allocated
 * @hw_ev_rings: Number of hardware event rings
 * @sw_ev_rings: Number of software event rings
 * @nr_irqs_req: Number of IRQs required to operate (optional)
 * @nr_irqs: Number of IRQ allocated by bus master (required)
 * @mhi_event: MHI event ring configurations table
 * @mhi_cmd: MHI command ring configurations table
 * @mhi_ctxt: MHI device context, shared memory between host and device
 * @pm_mutex: Mutex for suspend/resume operation
 * @pm_lock: Lock for protecting MHI power management state
 * @timeout_ms: Timeout in ms for state transitions
 * @pm_state: MHI power management state
 * @db_access: DB access states
 * @ee: MHI device execution environment
 * @dev_wake: Device wakeup count
 * @pending_pkts: Pending packets for the controller
 * @transition_list: List of MHI state transitions
 * @transition_lock: Lock for protecting MHI state transition list
 * @wlock: Lock for protecting device wakeup
 * @st_worker: State transition worker
 * @fw_worker: Firmware download worker
 * @syserr_worker: System error worker
 * @state_event: State change event
 * @status_cb: CB function to notify power states of the device (required)
 * @link_status: CB function to query link status of the device (required)
 * @wake_get: CB function to assert device wake (optional)
 * @wake_put: CB function to de-assert device wake (optional)
 * @wake_toggle: CB function to assert and de-assert device wake (optional)
 * @runtime_get: CB function to controller runtime resume (required)
 * @runtimet_put: CB function to decrement pm usage (required)
 * @buffer_len: Bounce buffer length
 * @bounce_buf: Use of bounce buffer
 * @fbc_download: MHI host needs to do complete image transfer (optional)
 * @pre_init: MHI host needs to do pre-initialization before power up
 * @wake_set: Device wakeup set flag
 *
 * Fields marked as (required) need to be populated by the controller driver
 * before calling mhi_register_controller(). For the fields marked as (optional)
 * they can be populated depending on the usecase.
 */
struct mhi_controller {
	struct device *cntrl_dev;
	struct mhi_device *mhi_dev;
	void __iomem *regs;
	dma_addr_t iova_start;
	dma_addr_t iova_stop;
	const char *fw_image;
	const char *edl_image;
	size_t sbl_size;
	size_t seg_len;
	struct mhi_chan *mhi_chan;
	struct list_head lpm_chans;
	int *irq;
	u32 max_chan;
	u32 total_ev_rings;
	u32 hw_ev_rings;
	u32 sw_ev_rings;
	u32 nr_irqs_req;
	u32 nr_irqs;

	struct mhi_event *mhi_event;
	struct mhi_cmd *mhi_cmd;
	struct mhi_ctxt *mhi_ctxt;

	struct mutex pm_mutex;
	rwlock_t pm_lock;
	u32 timeout_ms;
	u32 pm_state;
	u32 db_access;
	enum mhi_ee_type ee;
	atomic_t dev_wake;
	atomic_t pending_pkts;
	struct list_head transition_list;
	spinlock_t transition_lock;
	spinlock_t wlock;
	struct work_struct st_worker;
	struct work_struct fw_worker;
	struct work_struct syserr_worker;
	wait_queue_head_t state_event;

	void (*status_cb)(struct mhi_controller *mhi_cntrl,
			  enum mhi_callback cb);
	int (*link_status)(struct mhi_controller *mhi_cntrl);
	void (*wake_get)(struct mhi_controller *mhi_cntrl, bool override);
	void (*wake_put)(struct mhi_controller *mhi_cntrl, bool override);
	void (*wake_toggle)(struct mhi_controller *mhi_cntrl);
	int (*runtime_get)(struct mhi_controller *mhi_cntrl);
	void (*runtime_put)(struct mhi_controller *mhi_cntrl);

	size_t buffer_len;
	bool bounce_buf;
	bool fbc_download;
	bool pre_init;
	bool wake_set;
};

/**
 * struct mhi_device - Structure representing a MHI device which binds
 *                     to channels
 * @id: Pointer to MHI device ID struct
 * @chan_name: Name of the channel to which the device binds
 * @mhi_cntrl: Controller the device belongs to
 * @ul_chan: UL channel for the device
 * @dl_chan: DL channel for the device
 * @dev: Driver model device node for the MHI device
 * @dev_type: MHI device type
 * @dev_wake: Device wakeup counter
 */
struct mhi_device {
	const struct mhi_device_id *id;
	const char *chan_name;
	struct mhi_controller *mhi_cntrl;
	struct mhi_chan *ul_chan;
	struct mhi_chan *dl_chan;
	struct device dev;
	enum mhi_device_type dev_type;
	u32 dev_wake;
};

/**
 * struct mhi_result - Completed buffer information
 * @buf_addr: Address of data buffer
 * @bytes_xferd: # of bytes transferred
 * @dir: Channel direction
 * @transaction_status: Status of last transaction
 */
struct mhi_result {
	void *buf_addr;
	size_t bytes_xferd;
	enum dma_data_direction dir;
	int transaction_status;
};

#define to_mhi_device(dev) container_of(dev, struct mhi_device, dev)

/**
 * mhi_register_controller - Register MHI controller
 * @mhi_cntrl: MHI controller to register
 * @config: Configuration to use for the controller
 */
int mhi_register_controller(struct mhi_controller *mhi_cntrl,
			    struct mhi_controller_config *config);

/**
 * mhi_unregister_controller - Unregister MHI controller
 * @mhi_cntrl: MHI controller to unregister
 */
void mhi_unregister_controller(struct mhi_controller *mhi_cntrl);

#endif /* _MHI_H_ */
