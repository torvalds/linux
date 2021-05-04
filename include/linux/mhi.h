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
#include <linux/skbuff.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/wait.h>
#include <linux/workqueue.h>

#define MHI_MAX_OEM_PK_HASH_SEGMENTS 16

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
 * @MHI_CB_BW_REQ: Received a bandwidth switch request from device
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
	MHI_CB_BW_REQ,
};

/**
 * enum mhi_flags - Transfer flags
 * @MHI_EOB: End of buffer for bulk transfer
 * @MHI_EOT: End of transfer
 * @MHI_CHAIN: Linked transfer
 */
enum mhi_flags {
	MHI_EOB = BIT(0),
	MHI_EOT = BIT(1),
	MHI_CHAIN = BIT(2),
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
 * struct image_info - Firmware and RDDM table
 * @mhi_buf: Buffer for firmware and RDDM table
 * @entries: # of entries in table
 */
struct image_info {
	struct mhi_buf *mhi_buf;
	/* private: from internal.h */
	struct bhi_vec_entry *bhi_vec;
	/* public: */
	u32 entries;
};

/**
 * struct mhi_link_info - BW requirement
 * target_link_speed - Link speed as defined by TLS bits in LinkControl reg
 * target_link_width - Link width as defined by NLW bits in LinkStatus reg
 */
struct mhi_link_info {
	unsigned int target_link_speed;
	unsigned int target_link_width;
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
 * enum mhi_state - MHI states
 * @MHI_STATE_RESET: Reset state
 * @MHI_STATE_READY: Ready state
 * @MHI_STATE_M0: M0 state
 * @MHI_STATE_M1: M1 state
 * @MHI_STATE_M2: M2 state
 * @MHI_STATE_M3: M3 state
 * @MHI_STATE_M3_FAST: M3 Fast state
 * @MHI_STATE_BHI: BHI state
 * @MHI_STATE_SYS_ERR: System Error state
 */
enum mhi_state {
	MHI_STATE_RESET = 0x0,
	MHI_STATE_READY = 0x1,
	MHI_STATE_M0 = 0x2,
	MHI_STATE_M1 = 0x3,
	MHI_STATE_M2 = 0x4,
	MHI_STATE_M3 = 0x5,
	MHI_STATE_M3_FAST = 0x6,
	MHI_STATE_BHI = 0x7,
	MHI_STATE_SYS_ERR = 0xFF,
	MHI_STATE_MAX,
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
 * @wake-capable: Channel capable of waking up the system
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
	bool wake_capable;
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
	const struct mhi_channel_config *ch_cfg;
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
 * @debugfs_dentry: MHI controller debugfs directory
 * @regs: Base address of MHI MMIO register space (required)
 * @bhi: Points to base of MHI BHI register space
 * @bhie: Points to base of MHI BHIe register space
 * @wake_db: MHI WAKE doorbell register address
 * @iova_start: IOMMU starting address for data (required)
 * @iova_stop: IOMMU stop address for data (required)
 * @fw_image: Firmware image name for normal booting (required)
 * @edl_image: Firmware image name for emergency download mode (optional)
 * @rddm_size: RAM dump size that host should allocate for debugging purpose
 * @sbl_size: SBL image size downloaded through BHIe (optional)
 * @seg_len: BHIe vector size (optional)
 * @fbc_image: Points to firmware image buffer
 * @rddm_image: Points to RAM dump buffer
 * @mhi_chan: Points to the channel configuration table
 * @lpm_chans: List of channels that require LPM notifications
 * @irq: base irq # to request (required)
 * @max_chan: Maximum number of channels the controller supports
 * @total_ev_rings: Total # of event rings allocated
 * @hw_ev_rings: Number of hardware event rings
 * @sw_ev_rings: Number of software event rings
 * @nr_irqs: Number of IRQ allocated by bus master (required)
 * @family_number: MHI controller family number
 * @device_number: MHI controller device number
 * @major_version: MHI controller major revision number
 * @minor_version: MHI controller minor revision number
 * @serial_number: MHI controller serial number obtained from BHI
 * @oem_pk_hash: MHI controller OEM PK Hash obtained from BHI
 * @mhi_event: MHI event ring configurations table
 * @mhi_cmd: MHI command ring configurations table
 * @mhi_ctxt: MHI device context, shared memory between host and device
 * @pm_mutex: Mutex for suspend/resume operation
 * @pm_lock: Lock for protecting MHI power management state
 * @timeout_ms: Timeout in ms for state transitions
 * @pm_state: MHI power management state
 * @db_access: DB access states
 * @ee: MHI device execution environment
 * @dev_state: MHI device state
 * @dev_wake: Device wakeup count
 * @pending_pkts: Pending packets for the controller
 * @M0, M2, M3: Counters to track number of device MHI state changes
 * @transition_list: List of MHI state transitions
 * @transition_lock: Lock for protecting MHI state transition list
 * @wlock: Lock for protecting device wakeup
 * @mhi_link_info: Device bandwidth info
 * @st_worker: State transition worker
 * @hiprio_wq: High priority workqueue for MHI work such as state transitions
 * @state_event: State change event
 * @status_cb: CB function to notify power states of the device (required)
 * @wake_get: CB function to assert device wake (optional)
 * @wake_put: CB function to de-assert device wake (optional)
 * @wake_toggle: CB function to assert and de-assert device wake (optional)
 * @runtime_get: CB function to controller runtime resume (required)
 * @runtime_put: CB function to decrement pm usage (required)
 * @map_single: CB function to create TRE buffer
 * @unmap_single: CB function to destroy TRE buffer
 * @read_reg: Read a MHI register via the physical link (required)
 * @write_reg: Write a MHI register via the physical link (required)
 * @reset: Controller specific reset function (optional)
 * @buffer_len: Bounce buffer length
 * @index: Index of the MHI controller instance
 * @bounce_buf: Use of bounce buffer
 * @fbc_download: MHI host needs to do complete image transfer (optional)
 * @pre_init: MHI host needs to do pre-initialization before power up
 * @wake_set: Device wakeup set flag
 * @irq_flags: irq flags passed to request_irq (optional)
 *
 * Fields marked as (required) need to be populated by the controller driver
 * before calling mhi_register_controller(). For the fields marked as (optional)
 * they can be populated depending on the usecase.
 *
 * The following fields are present for the purpose of implementing any device
 * specific quirks or customizations for specific MHI revisions used in device
 * by the controller drivers. The MHI stack will just populate these fields
 * during mhi_register_controller():
 *  family_number
 *  device_number
 *  major_version
 *  minor_version
 */
struct mhi_controller {
	struct device *cntrl_dev;
	struct mhi_device *mhi_dev;
	struct dentry *debugfs_dentry;
	void __iomem *regs;
	void __iomem *bhi;
	void __iomem *bhie;
	void __iomem *wake_db;

	dma_addr_t iova_start;
	dma_addr_t iova_stop;
	const char *fw_image;
	const char *edl_image;
	size_t rddm_size;
	size_t sbl_size;
	size_t seg_len;
	struct image_info *fbc_image;
	struct image_info *rddm_image;
	struct mhi_chan *mhi_chan;
	struct list_head lpm_chans;
	int *irq;
	u32 max_chan;
	u32 total_ev_rings;
	u32 hw_ev_rings;
	u32 sw_ev_rings;
	u32 nr_irqs;
	u32 family_number;
	u32 device_number;
	u32 major_version;
	u32 minor_version;
	u32 serial_number;
	u32 oem_pk_hash[MHI_MAX_OEM_PK_HASH_SEGMENTS];

	struct mhi_event *mhi_event;
	struct mhi_cmd *mhi_cmd;
	struct mhi_ctxt *mhi_ctxt;

	struct mutex pm_mutex;
	rwlock_t pm_lock;
	u32 timeout_ms;
	u32 pm_state;
	u32 db_access;
	enum mhi_ee_type ee;
	enum mhi_state dev_state;
	atomic_t dev_wake;
	atomic_t pending_pkts;
	u32 M0, M2, M3;
	struct list_head transition_list;
	spinlock_t transition_lock;
	spinlock_t wlock;
	struct mhi_link_info mhi_link_info;
	struct work_struct st_worker;
	struct workqueue_struct *hiprio_wq;
	wait_queue_head_t state_event;

	void (*status_cb)(struct mhi_controller *mhi_cntrl,
			  enum mhi_callback cb);
	void (*wake_get)(struct mhi_controller *mhi_cntrl, bool override);
	void (*wake_put)(struct mhi_controller *mhi_cntrl, bool override);
	void (*wake_toggle)(struct mhi_controller *mhi_cntrl);
	int (*runtime_get)(struct mhi_controller *mhi_cntrl);
	void (*runtime_put)(struct mhi_controller *mhi_cntrl);
	int (*map_single)(struct mhi_controller *mhi_cntrl,
			  struct mhi_buf_info *buf);
	void (*unmap_single)(struct mhi_controller *mhi_cntrl,
			     struct mhi_buf_info *buf);
	int (*read_reg)(struct mhi_controller *mhi_cntrl, void __iomem *addr,
			u32 *out);
	void (*write_reg)(struct mhi_controller *mhi_cntrl, void __iomem *addr,
			  u32 val);
	void (*reset)(struct mhi_controller *mhi_cntrl);

	size_t buffer_len;
	int index;
	bool bounce_buf;
	bool fbc_download;
	bool pre_init;
	bool wake_set;
	unsigned long irq_flags;
};

/**
 * struct mhi_device - Structure representing an MHI device which binds
 *                     to channels or is associated with controllers
 * @id: Pointer to MHI device ID struct
 * @name: Name of the associated MHI device
 * @mhi_cntrl: Controller the device belongs to
 * @ul_chan: UL channel for the device
 * @dl_chan: DL channel for the device
 * @dev: Driver model device node for the MHI device
 * @dev_type: MHI device type
 * @ul_chan_id: MHI channel id for UL transfer
 * @dl_chan_id: MHI channel id for DL transfer
 * @dev_wake: Device wakeup counter
 */
struct mhi_device {
	const struct mhi_device_id *id;
	const char *name;
	struct mhi_controller *mhi_cntrl;
	struct mhi_chan *ul_chan;
	struct mhi_chan *dl_chan;
	struct device dev;
	enum mhi_device_type dev_type;
	int ul_chan_id;
	int dl_chan_id;
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

/**
 * struct mhi_buf - MHI Buffer description
 * @buf: Virtual address of the buffer
 * @name: Buffer label. For offload channel, configurations name must be:
 *        ECA - Event context array data
 *        CCA - Channel context array data
 * @dma_addr: IOMMU address of the buffer
 * @len: # of bytes
 */
struct mhi_buf {
	void *buf;
	const char *name;
	dma_addr_t dma_addr;
	size_t len;
};

/**
 * struct mhi_driver - Structure representing a MHI client driver
 * @probe: CB function for client driver probe function
 * @remove: CB function for client driver remove function
 * @ul_xfer_cb: CB function for UL data transfer
 * @dl_xfer_cb: CB function for DL data transfer
 * @status_cb: CB functions for asynchronous status
 * @driver: Device driver model driver
 */
struct mhi_driver {
	const struct mhi_device_id *id_table;
	int (*probe)(struct mhi_device *mhi_dev,
		     const struct mhi_device_id *id);
	void (*remove)(struct mhi_device *mhi_dev);
	void (*ul_xfer_cb)(struct mhi_device *mhi_dev,
			   struct mhi_result *result);
	void (*dl_xfer_cb)(struct mhi_device *mhi_dev,
			   struct mhi_result *result);
	void (*status_cb)(struct mhi_device *mhi_dev, enum mhi_callback mhi_cb);
	struct device_driver driver;
};

#define to_mhi_driver(drv) container_of(drv, struct mhi_driver, driver)
#define to_mhi_device(dev) container_of(dev, struct mhi_device, dev)

/**
 * mhi_alloc_controller - Allocate the MHI Controller structure
 * Allocate the mhi_controller structure using zero initialized memory
 */
struct mhi_controller *mhi_alloc_controller(void);

/**
 * mhi_free_controller - Free the MHI Controller structure
 * Free the mhi_controller structure which was previously allocated
 */
void mhi_free_controller(struct mhi_controller *mhi_cntrl);

/**
 * mhi_register_controller - Register MHI controller
 * @mhi_cntrl: MHI controller to register
 * @config: Configuration to use for the controller
 */
int mhi_register_controller(struct mhi_controller *mhi_cntrl,
			const struct mhi_controller_config *config);

/**
 * mhi_unregister_controller - Unregister MHI controller
 * @mhi_cntrl: MHI controller to unregister
 */
void mhi_unregister_controller(struct mhi_controller *mhi_cntrl);

/*
 * module_mhi_driver() - Helper macro for drivers that don't do
 * anything special other than using default mhi_driver_register() and
 * mhi_driver_unregister().  This eliminates a lot of boilerplate.
 * Each module may only use this macro once.
 */
#define module_mhi_driver(mhi_drv) \
	module_driver(mhi_drv, mhi_driver_register, \
		      mhi_driver_unregister)

/*
 * Macro to avoid include chaining to get THIS_MODULE
 */
#define mhi_driver_register(mhi_drv) \
	__mhi_driver_register(mhi_drv, THIS_MODULE)

/**
 * __mhi_driver_register - Register driver with MHI framework
 * @mhi_drv: Driver associated with the device
 * @owner: The module owner
 */
int __mhi_driver_register(struct mhi_driver *mhi_drv, struct module *owner);

/**
 * mhi_driver_unregister - Unregister a driver for mhi_devices
 * @mhi_drv: Driver associated with the device
 */
void mhi_driver_unregister(struct mhi_driver *mhi_drv);

/**
 * mhi_set_mhi_state - Set MHI device state
 * @mhi_cntrl: MHI controller
 * @state: State to set
 */
void mhi_set_mhi_state(struct mhi_controller *mhi_cntrl,
		       enum mhi_state state);

/**
 * mhi_notify - Notify the MHI client driver about client device status
 * @mhi_dev: MHI device instance
 * @cb_reason: MHI callback reason
 */
void mhi_notify(struct mhi_device *mhi_dev, enum mhi_callback cb_reason);

/**
 * mhi_get_free_desc_count - Get transfer ring length
 * Get # of TD available to queue buffers
 * @mhi_dev: Device associated with the channels
 * @dir: Direction of the channel
 */
int mhi_get_free_desc_count(struct mhi_device *mhi_dev,
				enum dma_data_direction dir);

/**
 * mhi_prepare_for_power_up - Do pre-initialization before power up.
 *                            This is optional, call this before power up if
 *                            the controller does not want bus framework to
 *                            automatically free any allocated memory during
 *                            shutdown process.
 * @mhi_cntrl: MHI controller
 */
int mhi_prepare_for_power_up(struct mhi_controller *mhi_cntrl);

/**
 * mhi_async_power_up - Start MHI power up sequence
 * @mhi_cntrl: MHI controller
 */
int mhi_async_power_up(struct mhi_controller *mhi_cntrl);

/**
 * mhi_sync_power_up - Start MHI power up sequence and wait till the device
 *                     enters valid EE state
 * @mhi_cntrl: MHI controller
 */
int mhi_sync_power_up(struct mhi_controller *mhi_cntrl);

/**
 * mhi_power_down - Start MHI power down sequence
 * @mhi_cntrl: MHI controller
 * @graceful: Link is still accessible, so do a graceful shutdown process
 */
void mhi_power_down(struct mhi_controller *mhi_cntrl, bool graceful);

/**
 * mhi_unprepare_after_power_down - Free any allocated memory after power down
 * @mhi_cntrl: MHI controller
 */
void mhi_unprepare_after_power_down(struct mhi_controller *mhi_cntrl);

/**
 * mhi_pm_suspend - Move MHI into a suspended state
 * @mhi_cntrl: MHI controller
 */
int mhi_pm_suspend(struct mhi_controller *mhi_cntrl);

/**
 * mhi_pm_resume - Resume MHI from suspended state
 * @mhi_cntrl: MHI controller
 */
int mhi_pm_resume(struct mhi_controller *mhi_cntrl);

/**
 * mhi_download_rddm_image - Download ramdump image from device for
 *                           debugging purpose.
 * @mhi_cntrl: MHI controller
 * @in_panic: Download rddm image during kernel panic
 */
int mhi_download_rddm_image(struct mhi_controller *mhi_cntrl, bool in_panic);

/**
 * mhi_force_rddm_mode - Force device into rddm mode
 * @mhi_cntrl: MHI controller
 */
int mhi_force_rddm_mode(struct mhi_controller *mhi_cntrl);

/**
 * mhi_get_exec_env - Get BHI execution environment of the device
 * @mhi_cntrl: MHI controller
 */
enum mhi_ee_type mhi_get_exec_env(struct mhi_controller *mhi_cntrl);

/**
 * mhi_get_mhi_state - Get MHI state of the device
 * @mhi_cntrl: MHI controller
 */
enum mhi_state mhi_get_mhi_state(struct mhi_controller *mhi_cntrl);

/**
 * mhi_soc_reset - Trigger a device reset. This can be used as a last resort
 *		   to reset and recover a device.
 * @mhi_cntrl: MHI controller
 */
void mhi_soc_reset(struct mhi_controller *mhi_cntrl);

/**
 * mhi_device_get - Disable device low power mode
 * @mhi_dev: Device associated with the channel
 */
void mhi_device_get(struct mhi_device *mhi_dev);

/**
 * mhi_device_get_sync - Disable device low power mode. Synchronously
 *                       take the controller out of suspended state
 * @mhi_dev: Device associated with the channel
 */
int mhi_device_get_sync(struct mhi_device *mhi_dev);

/**
 * mhi_device_put - Re-enable device low power mode
 * @mhi_dev: Device associated with the channel
 */
void mhi_device_put(struct mhi_device *mhi_dev);

/**
 * mhi_prepare_for_transfer - Setup channel for data transfer
 * @mhi_dev: Device associated with the channels
 */
int mhi_prepare_for_transfer(struct mhi_device *mhi_dev);

/**
 * mhi_unprepare_from_transfer - Unprepare the channels
 * @mhi_dev: Device associated with the channels
 */
void mhi_unprepare_from_transfer(struct mhi_device *mhi_dev);

/**
 * mhi_poll - Poll for any available data in DL direction
 * @mhi_dev: Device associated with the channels
 * @budget: # of events to process
 */
int mhi_poll(struct mhi_device *mhi_dev, u32 budget);

/**
 * mhi_queue_dma - Send or receive DMA mapped buffers from client device
 *                 over MHI channel
 * @mhi_dev: Device associated with the channels
 * @dir: DMA direction for the channel
 * @mhi_buf: Buffer for holding the DMA mapped data
 * @len: Buffer length
 * @mflags: MHI transfer flags used for the transfer
 */
int mhi_queue_dma(struct mhi_device *mhi_dev, enum dma_data_direction dir,
		  struct mhi_buf *mhi_buf, size_t len, enum mhi_flags mflags);

/**
 * mhi_queue_buf - Send or receive raw buffers from client device over MHI
 *                 channel
 * @mhi_dev: Device associated with the channels
 * @dir: DMA direction for the channel
 * @buf: Buffer for holding the data
 * @len: Buffer length
 * @mflags: MHI transfer flags used for the transfer
 */
int mhi_queue_buf(struct mhi_device *mhi_dev, enum dma_data_direction dir,
		  void *buf, size_t len, enum mhi_flags mflags);

/**
 * mhi_queue_skb - Send or receive SKBs from client device over MHI channel
 * @mhi_dev: Device associated with the channels
 * @dir: DMA direction for the channel
 * @skb: Buffer for holding SKBs
 * @len: Buffer length
 * @mflags: MHI transfer flags used for the transfer
 */
int mhi_queue_skb(struct mhi_device *mhi_dev, enum dma_data_direction dir,
		  struct sk_buff *skb, size_t len, enum mhi_flags mflags);

/**
 * mhi_queue_is_full - Determine whether queueing new elements is possible
 * @mhi_dev: Device associated with the channels
 * @dir: DMA direction for the channel
 */
bool mhi_queue_is_full(struct mhi_device *mhi_dev, enum dma_data_direction dir);

#endif /* _MHI_H_ */
